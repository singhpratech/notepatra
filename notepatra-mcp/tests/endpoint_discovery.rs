// SPDX-License-Identifier: GPL-3.0-or-later
//! End-to-end endpoint discovery: the editor publishes `mcp-endpoint.json`,
//! the sidecar reads it, dials it first, and falls through to the next
//! candidate when it is stale.
//!
//! Unix-only because it needs a real `UnixListener` to prove the fall-through
//! against a LIVE peer. The pure validation matrix (kinds, prefixes, garbage)
//! lives in the `transport::endpoint` unit tests and runs on every platform.
#![cfg(unix)]

use std::io::{BufRead, BufReader, Write};
use std::os::unix::net::{UnixListener, UnixStream};
use std::path::PathBuf;
use std::thread::JoinHandle;
use std::time::Duration;

use notepatra_mcp::transport::endpoint::published_endpoint;
use notepatra_mcp::transport::socket::{SocketEditor, NOT_RUNNING};
use notepatra_mcp::transport::EditorTransport;
use serde_json::{json, Value};

const GREETING: &str = r#"{"notepatra_mcp":1,"app":"Notepatra","version":"0.1.120"}"#;

/// Unique, SHORT socket path — unix socket paths cap near 108 bytes, so the
/// system temp dir is used rather than the (long) config tempdir.
fn temp_socket_path() -> PathBuf {
    std::env::temp_dir().join(format!("np-mcp-ep-{}.sock", std::process::id()))
}

/// Binds a listener, then serves the greeting + one `app_info` request on the
/// first accepted connection. Mirrors the helper in tests/socket_bridge.rs.
fn spawn_bridge(path: &PathBuf) -> JoinHandle<()> {
    let _ = std::fs::remove_file(path);
    let listener = UnixListener::bind(path).expect("bind fake bridge socket");
    std::thread::spawn(move || {
        let (stream, _) = listener.accept().expect("accept");
        serve_app_info(stream);
    })
}

fn serve_app_info(stream: UnixStream) {
    let mut reader = BufReader::new(stream.try_clone().expect("clone"));
    let mut stream = stream;
    // Greeting before payload: the bridge speaks first.
    writeln!(stream, "{GREETING}").unwrap();
    let mut line = String::new();
    reader.read_line(&mut line).unwrap();
    let req: Value = serde_json::from_str(&line).expect("request is JSON");
    assert_eq!(req["verb"], "app_info");
    let resp = json!({
        "id": req["id"],
        "ok": true,
        "result": {"name": "Notepatra", "version": "0.1.120",
                   "edition": "Full", "platform": "linux"},
    });
    writeln!(stream, "{resp}").unwrap();
}

/// ONE test on purpose: `NOTEPATRA_MCP_CONFIG_DIR` is process-global state, so
/// splitting these steps into separate `#[test]` fns would let the parallel
/// test threads clobber each other's env.
#[test]
fn endpoint_file_is_discovered_and_stale_entries_fall_through() {
    // (a) A private config dir that the editor would have written into.
    let config_dir = std::env::temp_dir().join(format!("np-mcp-epcfg-{}", std::process::id()));
    std::fs::create_dir_all(&config_dir).expect("create config dir");
    std::env::set_var("NOTEPATRA_MCP_CONFIG_DIR", &config_dir);
    let endpoint_json = config_dir.join("mcp-endpoint.json");

    // (b) A fake bridge at a path the computed guess would never produce —
    // exactly the macOS situation the published file exists to solve.
    let live = temp_socket_path();
    let live_str = live.to_str().unwrap().to_string();
    let handle = spawn_bridge(&live);

    // (c) Publication is read back verbatim.
    let record = json!({
        "notepatra_mcp_endpoint": 1,
        "kind": "unix_socket",
        "value": live_str,
        "name": "notepatra-0123456789abcdef-mcp",
        "pid": std::process::id(),
        "version": "0.1.120",
    });
    std::fs::write(&endpoint_json, format!("{record}\n")).expect("write endpoint file");
    assert_eq!(published_endpoint().as_deref(), Some(live_str.as_str()));

    // (d) A STALE first candidate (crashed editor) must not shadow a live
    // later one — only connect failure advances, and it does.
    let ed = SocketEditor::with_candidates(vec![
        "/nonexistent/stale-endpoint".to_string(),
        live_str.clone(),
    ])
    .with_timeouts(Duration::from_secs(1), Duration::from_secs(1));
    let info = ed.app_info().expect("live later candidate must be reached");
    assert_eq!(info["name"], "Notepatra");
    handle.join().expect("bridge thread panicked");
    let _ = std::fs::remove_file(&live);

    // (e) Every candidate dead → the EXACT user-facing message, not a
    // per-candidate io error.
    let dead = SocketEditor::with_candidates(vec![
        "/nonexistent/one".to_string(),
        "/nonexistent/two".to_string(),
    ])
    .with_timeouts(Duration::from_secs(1), Duration::from_secs(1));
    assert_eq!(dead.app_info().unwrap_err().0, NOT_RUNNING);

    // (f) No file (editor shut down cleanly) → nothing published.
    std::fs::remove_file(&endpoint_json).expect("remove endpoint file");
    assert_eq!(published_endpoint(), None);

    // (g) Oversized file → rejected without being parsed.
    std::fs::write(&endpoint_json, "x".repeat(64 * 1024 + 1)).expect("write oversized");
    assert_eq!(published_endpoint(), None);

    // (h) The other platform's kind is never accepted here.
    let foreign = json!({
        "notepatra_mcp_endpoint": 1,
        "kind": "named_pipe",
        "value": r"\\.\pipe\notepatra-0123456789abcdef-mcp",
    });
    std::fs::write(&endpoint_json, format!("{foreign}\n")).expect("write foreign kind");
    assert_eq!(published_endpoint(), None);

    // (i) A FIFO planted at the endpoint path must be REFUSED, not read. Its
    // metadata reports len 0, so it slips past the size gate; without the
    // is_file() guard read_to_string would block forever and hang every sidecar
    // startup. Guard the assertion with a watchdog thread so a regression fails
    // this test LOUDLY instead of hanging CI (a hang is not a red).
    #[cfg(unix)]
    {
        std::fs::remove_file(&endpoint_json).expect("clear before fifo");
        // Shell out rather than link libc: the crate is deliberately
        // dependency-free, and this is a test-only one-shot.
        let made = std::process::Command::new("mkfifo")
            .arg(&endpoint_json)
            .status()
            .map(|s| s.success())
            .unwrap_or(false);
        if made {
            let (tx, rx) = std::sync::mpsc::channel();
            std::thread::spawn(move || {
                let _ = tx.send(published_endpoint());
            });
            match rx.recv_timeout(Duration::from_secs(5)) {
                Ok(v) => assert_eq!(v, None, "a FIFO must be refused, not parsed"),
                Err(_) => panic!(
                    "published_endpoint() BLOCKED on a FIFO — the is_file() guard \
                     in transport::endpoint is missing or broken"
                ),
            }
        }
    }

    std::env::remove_var("NOTEPATRA_MCP_CONFIG_DIR");
    let _ = std::fs::remove_dir_all(&config_dir);
}
