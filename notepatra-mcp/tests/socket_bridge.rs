// SPDX-License-Identifier: GPL-3.0-or-later
//! Drives SocketEditor against a fake C++ bridge: a std-thread UnixListener
//! speaking the greeting + newline-delimited verb protocol.
#![cfg(unix)]

use std::io::{BufRead, BufReader, Write};
use std::os::unix::net::{UnixListener, UnixStream};
use std::path::{Path, PathBuf};
use std::sync::atomic::{AtomicU32, Ordering};
use std::thread::JoinHandle;
use std::time::Duration;

use notepatra_mcp::transport::socket::{SocketEditor, NOT_RUNNING};
use notepatra_mcp::transport::{EditorTransport, TabSelector};
use serde_json::{json, Value};

const GREETING: &str = r#"{"notepatra_mcp":1,"app":"Notepatra","version":"0.1.118"}"#;

/// Unique, short socket path (unix socket paths are capped near 108 bytes,
/// so the system temp dir is used rather than a long per-session scratchpad).
fn temp_socket_path() -> PathBuf {
    static N: AtomicU32 = AtomicU32::new(0);
    let n = N.fetch_add(1, Ordering::Relaxed);
    std::env::temp_dir().join(format!("np-mcp-test-{}-{n}.sock", std::process::id()))
}

/// Binds a listener, then runs `behavior` on the first accepted connection.
fn spawn_bridge(behavior: impl FnOnce(UnixStream) + Send + 'static) -> (PathBuf, JoinHandle<()>) {
    let path = temp_socket_path();
    let listener = UnixListener::bind(&path).expect("bind fake bridge socket");
    let handle = std::thread::spawn(move || {
        let (stream, _) = listener.accept().expect("accept");
        behavior(stream);
    });
    (path, handle)
}

fn editor_for(path: &Path) -> SocketEditor {
    SocketEditor::with_socket_path(path.to_str().unwrap())
        .with_timeouts(Duration::from_secs(1), Duration::from_secs(1))
}

fn finish(path: PathBuf, handle: JoinHandle<()>) {
    handle.join().expect("bridge thread panicked");
    let _ = std::fs::remove_file(path);
}

#[test]
fn greeting_then_round_trips_with_sequential_ids() {
    let (path, handle) = spawn_bridge(|stream| {
        let mut reader = BufReader::new(stream.try_clone().expect("clone"));
        let mut stream = stream;
        // Greeting before payload: the bridge speaks first.
        writeln!(stream, "{GREETING}").unwrap();
        for expected_id in 1..=2u64 {
            let mut line = String::new();
            reader.read_line(&mut line).unwrap();
            let req: Value = serde_json::from_str(&line).expect("request is JSON");
            assert_eq!(req["id"], expected_id, "ids must be sequential");
            assert!(req["args"].is_object());
            let result = match req["verb"].as_str().unwrap() {
                "app_info" => {
                    assert_eq!(req["args"], json!({}));
                    json!({ "name": "Notepatra", "version": "0.1.118",
                            "edition": "Full", "platform": "linux" })
                }
                "open_file" => {
                    assert_eq!(req["args"], json!({ "path": "/tmp/x.txt" }));
                    // Exact bridge shape (src/mcp_bridge.cpp verbOpenFile).
                    json!({ "opened": true, "tab_index": 4 })
                }
                other => panic!("unexpected verb {other}"),
            };
            let resp = json!({ "id": expected_id, "ok": true, "result": result });
            writeln!(stream, "{resp}").unwrap();
        }
    });
    let mut ed = editor_for(&path);
    let info = ed.app_info().expect("app_info round-trip");
    assert_eq!(info["edition"], "Full");
    // Second call reuses the connection with the next id, and the typed
    // wave-1 parsing maps {opened,tab_index} into the tab index.
    let index = ed.open_file("/tmp/x.txt").expect("open_file round-trip");
    assert_eq!(index, 4);
    finish(path, handle);
}

#[test]
fn wave1_typed_responses_parse() {
    // Every fixture below is the EXACT shape src/mcp_bridge.cpp emits.
    let (path, handle) = spawn_bridge(|stream| {
        let mut reader = BufReader::new(stream.try_clone().expect("clone"));
        let mut stream = stream;
        writeln!(stream, "{GREETING}").unwrap();
        for _ in 0..5 {
            let mut line = String::new();
            if reader.read_line(&mut line).unwrap() == 0 {
                return;
            }
            let req: Value = serde_json::from_str(&line).unwrap();
            let result = match req["verb"].as_str().unwrap() {
                "list_open_tabs" => json!({ "tabs": [
                    { "index": 0, "title": "a.rs", "path": "/a.rs", "modified": true },
                    { "index": 1, "title": "Untitled 1", "path": "", "modified": false },
                ]}),
                "read_tab" => {
                    // The bridge reads "index" (NOT "tab_index") or "title".
                    assert_eq!(req["args"], json!({ "index": 1 }));
                    json!({ "title": "Untitled 1", "path": "", "text": "scratch\n" })
                }
                "get_selection" => {
                    json!({ "text": "fn x()", "tab_index": 0 })
                }
                "search_project" => {
                    assert_eq!(req["args"], json!({ "query": "x", "max_results": 10 }));
                    json!({
                        "results": [
                            { "path": "/a.rs", "line": 3, "text": "let x = 1;" },
                            { "path": "", "line": 1, "text": "x marks the spot" },
                        ],
                        "truncated": false
                    })
                }
                "find_in_tab" => {
                    // The bridge reads the tab from "index" here too.
                    assert_eq!(req["args"], json!({ "query": "x", "index": 0 }));
                    json!({ "matches": [ { "line": 3, "text": "let x = 1;" } ],
                            "truncated": false })
                }
                other => panic!("unexpected verb {other}"),
            };
            let resp = json!({ "id": req["id"], "ok": true, "result": result });
            writeln!(stream, "{resp}").unwrap();
        }
    });
    let ed = editor_for(&path);
    let tabs = ed.list_open_tabs().expect("list_open_tabs");
    assert_eq!(tabs.len(), 2);
    assert_eq!(tabs[0].title, "a.rs");
    assert!(tabs[0].modified);
    assert_eq!(tabs[1].path, None); // "" on the wire maps to None
    let content = ed.read_tab(TabSelector::Index(1)).expect("read_tab");
    assert_eq!(content.title, "Untitled 1");
    assert_eq!(content.path, None);
    assert_eq!(content.text, "scratch\n");
    assert!(!content.truncated);
    let sel = ed.get_selection().expect("get_selection");
    assert_eq!(sel.text, "fn x()");
    assert_eq!(sel.tab_index, 0);
    let found = ed.search_project("x", 10, false).expect("search_project");
    assert_eq!(found.results.len(), 2);
    assert_eq!(found.results[0].path, "/a.rs");
    assert_eq!(found.results[0].line, 3);
    assert_eq!(found.results[1].path, "");
    assert!(!found.truncated);
    let matches = ed.find_in_tab(Some(0), "x", false).expect("find_in_tab");
    assert_eq!(matches["matches"][0]["line"], 3);
    finish(path, handle);
}

#[test]
fn legacy_lenient_shapes_are_rejected() {
    // The old client accepted bare arrays and a "hits" key; the contract is
    // exact-match now — anything but the bridge's shape is malformed.
    let (path, handle) = spawn_bridge(|stream| {
        let mut reader = BufReader::new(stream.try_clone().expect("clone"));
        let mut stream = stream;
        writeln!(stream, "{GREETING}").unwrap();
        for _ in 0..3 {
            let mut line = String::new();
            if reader.read_line(&mut line).unwrap() == 0 {
                return;
            }
            let req: Value = serde_json::from_str(&line).unwrap();
            let result = match req["verb"].as_str().unwrap() {
                // Bare array instead of {"tabs":[...]}.
                "list_open_tabs" => json!([
                    { "index": 0, "title": "a.rs", "path": "/a.rs", "modified": true },
                ]),
                // "hits" instead of "results".
                "search_project" => json!({ "hits": [], "truncated": false }),
                // Selection with a "title" but no "tab_index".
                "get_selection" => json!({ "text": "x", "title": "a.rs" }),
                other => panic!("unexpected verb {other}"),
            };
            let resp = json!({ "id": req["id"], "ok": true, "result": result });
            writeln!(stream, "{resp}").unwrap();
        }
    });
    let ed = editor_for(&path);
    let err = ed.list_open_tabs().unwrap_err();
    assert!(err.0.contains("\"tabs\""), "unexpected error: {}", err.0);
    let err = ed.search_project("q", 5, false).unwrap_err();
    assert!(err.0.contains("\"results\""), "unexpected error: {}", err.0);
    let err = ed.get_selection().unwrap_err();
    assert!(
        err.0.contains("\"tab_index\""),
        "unexpected error: {}",
        err.0
    );
    finish(path, handle);
}

#[test]
fn silent_bridge_times_out_cleanly() {
    let (path, handle) = spawn_bridge(|stream| {
        // Accept, say nothing, and hold the socket open past the client's
        // timeout so the failure is a timeout, not a closed connection.
        std::thread::sleep(Duration::from_millis(600));
        drop(stream);
    });
    let ed = SocketEditor::with_socket_path(path.to_str().unwrap())
        .with_timeouts(Duration::from_millis(200), Duration::from_millis(200));
    let err = ed.list_open_tabs().unwrap_err();
    assert!(
        err.0.contains("timed out") && err.0.contains("greeting"),
        "unexpected error: {}",
        err.0
    );
    finish(path, handle);
}

#[test]
fn editor_not_running_is_a_clean_error() {
    let path = temp_socket_path(); // never bound
    let ed = editor_for(&path);
    let err = ed.list_open_tabs().unwrap_err();
    assert_eq!(err.0, NOT_RUNNING);
    let err = ed.get_status().unwrap_err();
    assert_eq!(err.0, NOT_RUNNING);
}

#[test]
fn invalid_greeting_is_rejected_before_any_send() {
    let (path, handle) = spawn_bridge(|stream| {
        let mut reader = BufReader::new(stream.try_clone().expect("clone"));
        let mut stream = stream;
        writeln!(stream, r#"{{"hello":"world"}}"#).unwrap();
        // Proof-of-life law: the client must hang up without sending anything.
        let mut line = String::new();
        let n = reader.read_line(&mut line).unwrap();
        assert_eq!(n, 0, "client sent a payload after a bad greeting: {line}");
    });
    let ed = editor_for(&path);
    let err = ed.app_info().unwrap_err();
    assert!(err.0.contains("greeting"), "unexpected error: {}", err.0);
    drop(ed); // closes the socket so the bridge's read_line sees EOF
    finish(path, handle);
}

#[test]
fn write_verbs_wait_out_the_approval_window() {
    let (path, handle) = spawn_bridge(|stream| {
        let mut reader = BufReader::new(stream.try_clone().expect("clone"));
        let mut stream = stream;
        writeln!(stream, "{GREETING}").unwrap();
        for _ in 0..4 {
            let mut line = String::new();
            if reader.read_line(&mut line).unwrap() == 0 {
                return;
            }
            let req: Value = serde_json::from_str(&line).unwrap();
            let result = match req["verb"].as_str().unwrap() {
                "insert_text" => {
                    assert_eq!(
                        req["args"],
                        json!({ "text": "hi", "tab_index": 1, "line": 2, "col": 3 })
                    );
                    // Longer than the base timeout, within the approval
                    // window: proves write verbs get the long timeout.
                    std::thread::sleep(Duration::from_millis(300));
                    json!({ "ok": true, "tab_index": 1 })
                }
                "replace_selection" => {
                    assert_eq!(req["args"], json!({ "text": "x" }));
                    json!({ "ok": true })
                }
                "apply_edit" => {
                    // "all" is always sent explicitly.
                    assert_eq!(
                        req["args"],
                        json!({ "find": "a", "replace": "b", "all": false, "tab_index": 0 })
                    );
                    json!({ "ok": true, "count": 2 })
                }
                "save_tab" => {
                    assert_eq!(req["args"], json!({}));
                    json!({ "ok": true })
                }
                other => panic!("unexpected verb {other}"),
            };
            let resp = json!({ "id": req["id"], "ok": true, "result": result });
            writeln!(stream, "{resp}").unwrap();
        }
    });
    let mut ed = SocketEditor::with_socket_path(path.to_str().unwrap())
        .with_timeouts(Duration::from_millis(100), Duration::from_millis(100))
        .with_approval_timeout(Duration::from_secs(2));
    let v = ed
        .insert_text("hi", Some(1), Some(2), Some(3))
        .expect("insert_text survives the approval wait");
    assert_eq!(v, json!({ "ok": true, "tab_index": 1 }));
    let v = ed.replace_selection("x", None).expect("replace_selection");
    assert_eq!(v, json!({ "ok": true }));
    let v = ed.apply_edit("a", "b", Some(0), false).expect("apply_edit");
    assert_eq!(v["count"], 2);
    let v = ed.save_tab(None).expect("save_tab");
    assert_eq!(v, json!({ "ok": true }));
    finish(path, handle);
}

#[test]
fn approval_denial_and_timeout_errors_pass_through_verbatim() {
    // One connection per error: the client drops the connection after any
    // error round-trip, and the fake bridge accepts only once.
    for expected in ["denied by user", "approval timed out"] {
        let (path, handle) = spawn_bridge(move |stream| {
            let mut reader = BufReader::new(stream.try_clone().expect("clone"));
            let mut stream = stream;
            writeln!(stream, "{GREETING}").unwrap();
            let mut line = String::new();
            reader.read_line(&mut line).unwrap();
            let req: Value = serde_json::from_str(&line).unwrap();
            assert_eq!(req["verb"], "save_tab");
            let resp = json!({ "id": req["id"], "ok": false, "error": expected });
            writeln!(stream, "{resp}").unwrap();
        });
        let mut ed = editor_for(&path);
        let err = ed.save_tab(None).unwrap_err();
        assert_eq!(err.0, expected);
        finish(path, handle);
    }
}

// ---------------------------------------------------------------------------
// v0.1.119 "MCP depth" — new verb wire shapes
// ---------------------------------------------------------------------------

#[test]
fn v0119_read_verbs_send_exact_arg_keys() {
    let (path, handle) = spawn_bridge(|stream| {
        let mut reader = BufReader::new(stream.try_clone().expect("clone"));
        let mut stream = stream;
        writeln!(stream, "{GREETING}").unwrap();
        for _ in 0..11 {
            let mut line = String::new();
            if reader.read_line(&mut line).unwrap() == 0 {
                return;
            }
            let req: Value = serde_json::from_str(&line).unwrap();
            let result = match req["verb"].as_str().unwrap() {
                "list_reminders" => {
                    assert_eq!(req["args"], json!({}));
                    json!({ "reminders": [] })
                }
                "git_status" => {
                    assert_eq!(req["args"], json!({}));
                    json!({ "output": "On branch main\n" })
                }
                "git_diff" => {
                    // Whole-tree diff sends no args; a scoped diff sends "path".
                    assert!(req["args"] == json!({}) || req["args"] == json!({ "path": "a.rs" }));
                    json!({ "output": "" })
                }
                "git_log" => {
                    assert_eq!(req["args"], json!({ "limit": 20 }));
                    json!({ "output": "" })
                }
                "git_show" => {
                    assert_eq!(req["args"], json!({ "ref": "HEAD" }));
                    json!({ "output": "commit HEAD\n" })
                }
                "git_branch" => {
                    assert_eq!(req["args"], json!({}));
                    json!({ "output": "* main\n" })
                }
                "validate_npd" => {
                    // Exactly one selector: "tab_index" or "source".
                    assert!(
                        req["args"] == json!({ "tab_index": 2 })
                            || req["args"] == json!({ "source": "x" })
                    );
                    json!({ "valid": true, "errors": [] })
                }
                "run_sql" => {
                    assert!(
                        req["args"] == json!({ "sql": "SELECT 1" })
                            || req["args"] == json!({ "sql": "SELECT 1", "csv_path": "/d.csv" })
                    );
                    json!({ "columns": [], "rows": [], "truncated": false, "engine": "duckdb" })
                }
                other => panic!("unexpected verb {other}"),
            };
            let resp = json!({ "id": req["id"], "ok": true, "result": result });
            writeln!(stream, "{resp}").unwrap();
        }
    });
    let ed = editor_for(&path);
    ed.list_reminders().expect("list_reminders");
    ed.git_status().expect("git_status");
    ed.git_diff(None).expect("git_diff whole tree");
    ed.git_diff(Some("a.rs")).expect("git_diff path");
    ed.git_log(20).expect("git_log");
    ed.git_show("HEAD").expect("git_show");
    ed.git_branch().expect("git_branch");
    ed.validate_npd(Some(2), None).expect("validate_npd tab");
    ed.validate_npd(None, Some("x"))
        .expect("validate_npd source");
    ed.run_sql("SELECT 1", None).expect("run_sql");
    ed.run_sql("SELECT 1", Some("/d.csv")).expect("run_sql csv");
    finish(path, handle);
}

#[test]
fn v0119_act_and_write_verbs_send_exact_arg_keys() {
    let (path, handle) = spawn_bridge(|stream| {
        let mut reader = BufReader::new(stream.try_clone().expect("clone"));
        let mut stream = stream;
        writeln!(stream, "{GREETING}").unwrap();
        for _ in 0..5 {
            let mut line = String::new();
            if reader.read_line(&mut line).unwrap() == 0 {
                return;
            }
            let req: Value = serde_json::from_str(&line).unwrap();
            let result = match req["verb"].as_str().unwrap() {
                "open_note" => {
                    assert_eq!(req["args"], json!({ "file": "/n.html" }));
                    json!({ "opened": true, "title": "N" })
                }
                "create_note" => {
                    assert_eq!(req["args"], json!({ "title": "T", "body": "B" }));
                    json!({ "file": "/n2.html", "title": "T" })
                }
                "append_note" => {
                    assert_eq!(req["args"], json!({ "file": "/n.html", "text": "more" }));
                    json!({ "file": "/n.html" })
                }
                "set_reminder" => {
                    assert_eq!(
                        req["args"],
                        json!({ "file": "/n.html", "due_iso": "2026-07-20T09:00:00Z" })
                    );
                    json!({ "file": "/n.html", "due_iso": "2026-07-20T09:00:00Z" })
                }
                "export_diagram" => {
                    assert_eq!(
                        req["args"],
                        json!({ "tab_index": 1, "path": "/o.png", "format": "png" })
                    );
                    json!({ "path": "/o.png" })
                }
                other => panic!("unexpected verb {other}"),
            };
            let resp = json!({ "id": req["id"], "ok": true, "result": result });
            writeln!(stream, "{resp}").unwrap();
        }
    });
    // Short approval timeout keeps the write verbs snappy against the fake.
    let mut ed = SocketEditor::with_socket_path(path.to_str().unwrap())
        .with_timeouts(Duration::from_secs(1), Duration::from_secs(1))
        .with_approval_timeout(Duration::from_secs(2));
    let opened = ed.open_note("/n.html").expect("open_note");
    assert_eq!(opened["title"], "N");
    let created = ed.create_note("T", "B").expect("create_note");
    assert_eq!(created["file"], "/n2.html");
    ed.append_note("/n.html", "more").expect("append_note");
    ed.set_reminder("/n.html", "2026-07-20T09:00:00Z")
        .expect("set_reminder");
    let exported = ed
        .export_diagram(1, "/o.png", "png")
        .expect("export_diagram");
    assert_eq!(exported["path"], "/o.png");
    finish(path, handle);
}

#[test]
fn regex_flag_is_serialized_only_when_true() {
    let (path, handle) = spawn_bridge(|stream| {
        let mut reader = BufReader::new(stream.try_clone().expect("clone"));
        let mut stream = stream;
        writeln!(stream, "{GREETING}").unwrap();
        for _ in 0..4 {
            let mut line = String::new();
            if reader.read_line(&mut line).unwrap() == 0 {
                return;
            }
            let req: Value = serde_json::from_str(&line).unwrap();
            let result = match req["verb"].as_str().unwrap() {
                "find_in_tab" => {
                    // find_in_tab addresses the tab via "index"; regex only
                    // present when true.
                    if req["args"]["regex"].is_boolean() {
                        assert_eq!(
                            req["args"],
                            json!({ "query": "a.*b", "index": 0, "regex": true })
                        );
                    } else {
                        assert_eq!(req["args"], json!({ "query": "lit", "index": 0 }));
                        assert!(req["args"].get("regex").is_none());
                    }
                    json!({ "matches": [], "truncated": false })
                }
                "search_project" => {
                    if req["args"]["regex"].is_boolean() {
                        assert_eq!(
                            req["args"],
                            json!({ "query": "a.*b", "max_results": 50, "regex": true })
                        );
                    } else {
                        assert_eq!(req["args"], json!({ "query": "lit", "max_results": 50 }));
                        assert!(req["args"].get("regex").is_none());
                    }
                    json!({ "results": [], "truncated": false })
                }
                other => panic!("unexpected verb {other}"),
            };
            let resp = json!({ "id": req["id"], "ok": true, "result": result });
            writeln!(stream, "{resp}").unwrap();
        }
    });
    let ed = editor_for(&path);
    ed.find_in_tab(Some(0), "a.*b", true).expect("regex find");
    ed.find_in_tab(Some(0), "lit", false).expect("literal find");
    ed.search_project("a.*b", 50, true).expect("regex search");
    ed.search_project("lit", 50, false).expect("literal search");
    finish(path, handle);
}

#[test]
fn bridge_error_response_maps_to_transport_error() {
    let (path, handle) = spawn_bridge(|stream| {
        let mut reader = BufReader::new(stream.try_clone().expect("clone"));
        let mut stream = stream;
        writeln!(stream, "{GREETING}").unwrap();
        let mut line = String::new();
        reader.read_line(&mut line).unwrap();
        let req: Value = serde_json::from_str(&line).unwrap();
        let resp = json!({ "id": req["id"], "ok": false, "error": "no note named \"x.md\"" });
        writeln!(stream, "{resp}").unwrap();
    });
    let ed = editor_for(&path);
    let err = ed.read_note("x.md").unwrap_err();
    assert_eq!(err.0, "no note named \"x.md\"");
    finish(path, handle);
}
