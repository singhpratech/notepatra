// SPDX-License-Identifier: GPL-3.0-or-later
//! Drives SocketEditor against a fake C++ bridge on a REAL Windows named pipe:
//! a std-thread server speaking the greeting + newline-delimited verb protocol.
//! The unix twin lives in tests/socket_bridge.rs; this file is the only place
//! the `#[cfg(windows)]` `Conn` (reader thread + `mpsc::recv_timeout`) is
//! exercised at runtime — the Windows CI job runs `cargo test --release`.
//!
//! std alone cannot CREATE a named pipe, only open one, so the server half is
//! three lines of `extern "system"` against kernel32 — always linked on
//! Windows, so the default build's dependency graph stays untouched (no
//! winapi / windows-sys). Test-only unsafe.
//!
//! HONEST RESIDUAL: this proves the CLIENT half against a byte-mode Win32 pipe
//! created with default security. It does NOT cover QLocalServer's server-side
//! behavior — its security descriptor / `setSocketOptions(UserAccessOption)`
//! ACL, its own framing choices, or its connection backlog. Those are covered
//! only by the C++ `test_mcp_bridge` suite and by live use of the editor.
#![cfg(windows)]

use std::io::{BufRead, BufReader, Write};
use std::os::windows::io::FromRawHandle;
use std::sync::atomic::{AtomicU32, Ordering};
use std::thread::JoinHandle;
use std::time::{Duration, Instant};

use notepatra_mcp::transport::socket::{SocketEditor, NOT_RUNNING};
use notepatra_mcp::transport::{EditorTransport, TabSelector};
use serde_json::{json, Value};

const GREETING: &str = r#"{"notepatra_mcp":1,"app":"Notepatra","version":"0.1.120"}"#;

#[link(name = "kernel32")]
extern "system" {
    fn CreateNamedPipeW(
        name: *const u16,
        dw_open_mode: u32,
        dw_pipe_mode: u32,
        n_max_instances: u32,
        n_out_buffer: u32,
        n_in_buffer: u32,
        n_default_timeout: u32,
        security: *mut core::ffi::c_void,
    ) -> isize;
    fn ConnectNamedPipe(h: isize, overlapped: *mut core::ffi::c_void) -> i32;
}

const PIPE_ACCESS_DUPLEX: u32 = 0x0000_0003;
/// PIPE_TYPE_BYTE | PIPE_READMODE_BYTE | PIPE_WAIT — all zero. Byte mode
/// deliberately mirrors Qt's QLocalServer pipes: a message-mode pipe would
/// hide framing bugs the real editor would expose.
const PIPE_BYTE_MODE_WAIT: u32 = 0x0;
const PIPE_BUF_BYTES: u32 = 64 * 1024;
const INVALID_HANDLE_VALUE: isize = -1;

/// Unique pipe name for this process + call. Pipe names live in a global
/// kernel namespace, so the pid keeps concurrent CI jobs from colliding.
fn unique_pipe_name() -> String {
    static N: AtomicU32 = AtomicU32::new(0);
    let n = N.fetch_add(1, Ordering::Relaxed);
    format!(r"\\.\pipe\np-mcp-test-{}-{n}", std::process::id())
}

/// Creates the pipe instance BEFORE returning (so the client's open can never
/// race the server thread's startup), then runs `behavior` on the connected
/// pipe once a client attaches.
fn spawn_pipe_bridge(
    behavior: impl FnOnce(std::fs::File) + Send + 'static,
) -> (String, JoinHandle<()>) {
    let name = unique_pipe_name();
    let wide: Vec<u16> = name.encode_utf16().chain(std::iter::once(0)).collect();
    let handle = unsafe {
        CreateNamedPipeW(
            wide.as_ptr(),
            PIPE_ACCESS_DUPLEX,
            PIPE_BYTE_MODE_WAIT,
            1, // one instance: each test gets its own pipe name
            PIPE_BUF_BYTES,
            PIPE_BUF_BYTES,
            0, // default timeout only matters for WaitNamedPipe
            core::ptr::null_mut(),
        )
    };
    assert_ne!(
        handle,
        INVALID_HANDLE_VALUE,
        "CreateNamedPipeW({name}) failed: {}",
        std::io::Error::last_os_error()
    );
    let join = std::thread::spawn(move || {
        // Any return is "proceed": a 0 return with ERROR_PIPE_CONNECTED means
        // the client got in between create and connect, and a genuine failure
        // surfaces immediately as an I/O error in `behavior`.
        unsafe { ConnectNamedPipe(handle, core::ptr::null_mut()) };
        let file = unsafe { std::fs::File::from_raw_handle(handle as *mut _) };
        behavior(file);
    });
    (name, join)
}

fn editor_for(pipe: &str) -> SocketEditor {
    SocketEditor::with_socket_path(pipe)
        .with_timeouts(Duration::from_secs(1), Duration::from_secs(1))
}

#[test]
fn greeting_then_round_trips_with_sequential_ids() {
    let (pipe, handle) = spawn_pipe_bridge(|file| {
        let mut reader = BufReader::new(file.try_clone().expect("clone pipe handle"));
        let mut file = file;
        // Greeting before payload: the bridge speaks first.
        writeln!(file, "{GREETING}").unwrap();
        for expected_id in 1..=2u64 {
            let mut line = String::new();
            reader.read_line(&mut line).unwrap();
            let req: Value = serde_json::from_str(&line).expect("request is JSON");
            assert_eq!(req["id"], expected_id, "ids must be sequential");
            assert!(req["args"].is_object());
            let result = match req["verb"].as_str().unwrap() {
                "app_info" => {
                    assert_eq!(req["args"], json!({}));
                    json!({ "name": "Notepatra", "version": "0.1.120",
                            "edition": "Full", "platform": "windows" })
                }
                "open_file" => {
                    assert_eq!(req["args"], json!({ "path": "/tmp/x.txt" }));
                    // Exact bridge shape (src/mcp_bridge.cpp verbOpenFile).
                    json!({ "opened": true, "tab_index": 4 })
                }
                other => panic!("unexpected verb {other}"),
            };
            let resp = json!({ "id": expected_id, "ok": true, "result": result });
            writeln!(file, "{resp}").unwrap();
        }
    });
    let mut ed = editor_for(&pipe);
    let info = ed.app_info().expect("app_info round-trip");
    assert_eq!(info["edition"], "Full");
    // Second call reuses the same pipe connection with the next id, proving
    // the reader thread survives between round-trips.
    let index = ed.open_file("/tmp/x.txt").expect("open_file round-trip");
    assert_eq!(index, 4);
    handle.join().expect("bridge thread panicked");
}

#[test]
fn missing_pipe_is_not_running() {
    // Never created: opening it must surface the clean tool error, not an
    // OS-error string leaking into the agent's transcript.
    let pipe = format!(r"\\.\pipe\np-mcp-definitely-absent-{}", std::process::id());
    let ed = editor_for(&pipe);
    let err = ed.list_open_tabs().unwrap_err();
    assert_eq!(err.0, NOT_RUNNING);
    let err = ed.get_status().unwrap_err();
    assert_eq!(err.0, NOT_RUNNING);
}

#[test]
fn invalid_greeting_rejected() {
    let (pipe, handle) = spawn_pipe_bridge(|file| {
        let mut reader = BufReader::new(file.try_clone().expect("clone pipe handle"));
        let mut file = file;
        writeln!(file, r#"{{"hello":true}}"#).unwrap();
        // Proof-of-life law: the client must hang up without sending anything.
        // std maps ERROR_BROKEN_PIPE to Ok(0), so a disconnect reads as EOF.
        let mut line = String::new();
        let n = reader.read_line(&mut line).unwrap_or(0);
        assert_eq!(n, 0, "client sent a payload after a bad greeting: {line}");
    });
    let ed = editor_for(&pipe);
    let err = ed.read_tab(TabSelector::Index(0)).unwrap_err();
    assert!(
        err.0.contains("unexpected greeting"),
        "unexpected error: {}",
        err.0
    );
    drop(ed); // closes the client handle so the bridge's read sees EOF
    handle.join().expect("bridge thread panicked");
}

#[test]
fn response_timeout_enforced() {
    // The Windows path has no socket read timeout — it fakes one with a reader
    // thread plus `recv_timeout`. This pins that plumbing: a bridge that reads
    // the request and then goes silent must fail FAST, never hang the agent.
    let (pipe, handle) = spawn_pipe_bridge(|file| {
        let mut reader = BufReader::new(file.try_clone().expect("clone pipe handle"));
        let mut file = file;
        writeln!(file, "{GREETING}").unwrap();
        let mut line = String::new();
        let _ = reader.read_line(&mut line);
        // Hold the pipe open, well past the client's 1 s window, so the
        // failure is a TIMEOUT and not a closed connection.
        std::thread::sleep(Duration::from_secs(3));
    });
    let ed = editor_for(&pipe);
    let started = Instant::now();
    let err = ed.list_open_tabs().unwrap_err();
    let elapsed = started.elapsed();
    assert!(
        err.0.contains("timed out waiting"),
        "unexpected error: {}",
        err.0
    );
    assert!(
        elapsed < Duration::from_millis(2500),
        "recv_timeout did not fire: took {elapsed:?} against a 1 s timeout"
    );
    handle.join().expect("bridge thread panicked");
}

#[test]
fn editor_close_detected() {
    let (pipe, handle) = spawn_pipe_bridge(|file| {
        let mut file = file;
        writeln!(file, "{GREETING}").unwrap();
        // Returning drops the server handle: the editor went away mid-session.
    });
    let ed = editor_for(&pipe);
    let err = ed.list_open_tabs().unwrap_err();
    // Which side notices first is a genuine race — the request write may hit
    // the broken pipe before the reader thread's queued EOF is drained — so
    // both wordings are accepted. What is NOT acceptable (and is what this
    // test guards) is a hang or a timeout: a dead editor must be reported at
    // once, so the message must not be the timeout one.
    assert!(
        err.0.contains("closed the connection") || err.0.contains("connection lost"),
        "unexpected error: {}",
        err.0
    );
    assert!(
        !err.0.contains("timed out"),
        "a closed pipe must fail immediately, not time out: {}",
        err.0
    );
    handle.join().expect("bridge thread panicked");
}
