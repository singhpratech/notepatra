// SPDX-License-Identifier: GPL-3.0-or-later
//! Drives SocketEditor against a fake C++ bridge: a std-thread UnixListener
//! speaking the greeting + newline-delimited verb protocol.
#![cfg(unix)]

use std::io::{BufRead, BufReader, Write};
use std::os::unix::net::{UnixListener, UnixStream};
use std::path::{Path, PathBuf};
use std::sync::atomic::{AtomicU32, Ordering};
use std::sync::mpsc;
use std::time::Duration;

mod common;
use common::{finish, Bridge, Watchdog};

/// Names this suite's tracker file (`np-socket-bridge-tracker.log`).
const SUITE: &str = "socket-bridge";

use notepatra_mcp::transport::socket::{SocketEditor, NOT_RUNNING};
use notepatra_mcp::transport::{EditorTransport, TabRef, TabSelector};
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
///
/// HONEST RESIDUAL: `listener.accept()` is unbounded and std offers no accept
/// timeout, so a client that never connects would park this thread forever.
/// Its deadline is enforced one level up instead — by [`finish`]'s
/// `recv_timeout` in [`cleanup`] and, for a block on the TEST thread, by the
/// [`Watchdog`]. The listener is bound BEFORE this function returns, so the
/// client's connect can never race the thread's startup.
fn spawn_bridge(behavior: impl FnOnce(UnixStream) + Send + 'static) -> (PathBuf, Bridge) {
    let path = temp_socket_path();
    let listener = UnixListener::bind(&path).expect("bind fake bridge socket");
    let (done_tx, done) = mpsc::channel::<()>();
    let handle = std::thread::spawn(move || {
        let (stream, _) = listener.accept().expect("accept");
        behavior(stream);
        // LAST act: unblocks `finish`. On a panic this send never happens, but
        // `done_tx` drops during unwind and `finish` treats Disconnected as
        // "finished", so the panic is re-raised by the join.
        let _ = done_tx.send(());
    });
    (path, Bridge::new(handle, done))
}

fn editor_for(path: &Path) -> SocketEditor {
    SocketEditor::with_socket_path(path.to_str().unwrap())
        .with_timeouts(Duration::from_secs(1), Duration::from_secs(1))
}

/// Bounded join (never a bare `join()`) plus socket-file removal.
fn cleanup(path: PathBuf, bridge: Bridge, label: &str) {
    finish(bridge, label);
    let _ = std::fs::remove_file(path);
}

#[test]
fn greeting_then_round_trips_with_sequential_ids() {
    let _wd = Watchdog::new(SUITE, "greeting_then_round_trips_with_sequential_ids");
    let (path, bridge) = spawn_bridge(|stream| {
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
    cleanup(
        path,
        bridge,
        "greeting_then_round_trips_with_sequential_ids",
    );
}

#[test]
fn wave1_typed_responses_parse() {
    let _wd = Watchdog::new(SUITE, "wave1_typed_responses_parse");
    // Every fixture below is the EXACT shape src/mcp_bridge.cpp emits.
    let (path, bridge) = spawn_bridge(|stream| {
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
                        "truncated": false,
                        // v0.1.126 (NP-07): the bridge has sent these two
                        // since v0.1.125 and this layer silently dropped them.
                        "workspace_searched": true,
                        "scope": "tabs_and_workspace"
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
    let content = ed.read_tab(TabSelector::Index(1), None).expect("read_tab");
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
    // NP-07: the coverage fields must survive the wire → struct hop. The
    // struct declared only `results` and `truncated`, so serde discarded the
    // rest and every client saw a project-wide search it could not verify.
    assert!(
        found.workspace_searched,
        "workspace_searched was dropped between the bridge and the tool layer"
    );
    assert_eq!(found.scope, "tabs_and_workspace");
    let matches = ed
        .find_in_tab(TabRef::index(0), None, "x", false)
        .expect("find_in_tab");
    assert_eq!(matches["matches"][0]["line"], 3);
    cleanup(path, bridge, "wave1_typed_responses_parse");
}

#[test]
fn legacy_lenient_shapes_are_rejected() {
    let _wd = Watchdog::new(SUITE, "legacy_lenient_shapes_are_rejected");
    // The old client accepted bare arrays and a "hits" key; the contract is
    // exact-match now — anything but the bridge's shape is malformed.
    let (path, bridge) = spawn_bridge(|stream| {
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
    cleanup(path, bridge, "legacy_lenient_shapes_are_rejected");
}

#[test]
fn silent_bridge_times_out_cleanly() {
    let _wd = Watchdog::new(SUITE, "silent_bridge_times_out_cleanly");
    let (path, bridge) = spawn_bridge(|stream| {
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
    cleanup(path, bridge, "silent_bridge_times_out_cleanly");
}

#[test]
fn editor_not_running_is_a_clean_error() {
    let _wd = Watchdog::new(SUITE, "editor_not_running_is_a_clean_error");
    let path = temp_socket_path(); // never bound
    let ed = editor_for(&path);
    let err = ed.list_open_tabs().unwrap_err();
    assert_eq!(err.0, NOT_RUNNING);
    let err = ed.get_status().unwrap_err();
    assert_eq!(err.0, NOT_RUNNING);
}

#[test]
fn invalid_greeting_is_rejected_before_any_send() {
    let _wd = Watchdog::new(SUITE, "invalid_greeting_is_rejected_before_any_send");
    let (path, bridge) = spawn_bridge(|stream| {
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
    cleanup(path, bridge, "invalid_greeting_is_rejected_before_any_send");
}

#[test]
fn write_verbs_wait_out_the_approval_window() {
    let _wd = Watchdog::new(SUITE, "write_verbs_wait_out_the_approval_window");
    let (path, bridge) = spawn_bridge(|stream| {
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
        .insert_text("hi", TabRef::index(1), Some(2), Some(3))
        .expect("insert_text survives the approval wait");
    assert_eq!(v, json!({ "ok": true, "tab_index": 1 }));
    let v = ed
        .replace_selection("x", TabRef::default())
        .expect("replace_selection");
    assert_eq!(v, json!({ "ok": true }));
    let v = ed
        .apply_edit("a", "b", TabRef::index(0), false)
        .expect("apply_edit");
    assert_eq!(v["count"], 2);
    let v = ed.save_tab(TabRef::default(), None).expect("save_tab");
    assert_eq!(v, json!({ "ok": true }));
    cleanup(path, bridge, "write_verbs_wait_out_the_approval_window");
}

#[test]
fn approval_denial_and_timeout_errors_pass_through_verbatim() {
    let _wd = Watchdog::new(
        SUITE,
        "approval_denial_and_timeout_errors_pass_through_verbatim",
    );
    // One connection per error: the client drops the connection after any
    // error round-trip, and the fake bridge accepts only once.
    for expected in ["denied by user", "approval timed out"] {
        let (path, bridge) = spawn_bridge(move |stream| {
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
        let err = ed.save_tab(TabRef::default(), None).unwrap_err();
        assert_eq!(err.0, expected);
        cleanup(
            path,
            bridge,
            "approval_denial_and_timeout_errors_pass_through_verbatim",
        );
    }
}

// ---------------------------------------------------------------------------
// v0.1.119 "MCP depth" — new verb wire shapes
// ---------------------------------------------------------------------------

#[test]
fn v0119_read_verbs_send_exact_arg_keys() {
    let _wd = Watchdog::new(SUITE, "v0119_read_verbs_send_exact_arg_keys");
    let (path, bridge) = spawn_bridge(|stream| {
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
    ed.validate_npd(TabRef::index(2), None)
        .expect("validate_npd tab");
    ed.validate_npd(TabRef::default(), Some("x"))
        .expect("validate_npd source");
    ed.run_sql("SELECT 1", None).expect("run_sql");
    ed.run_sql("SELECT 1", Some("/d.csv")).expect("run_sql csv");
    cleanup(path, bridge, "v0119_read_verbs_send_exact_arg_keys");
}

#[test]
fn v0119_act_and_write_verbs_send_exact_arg_keys() {
    let _wd = Watchdog::new(SUITE, "v0119_act_and_write_verbs_send_exact_arg_keys");
    let (path, bridge) = spawn_bridge(|stream| {
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
        .export_diagram(TabRef::index(1), "/o.png", "png")
        .expect("export_diagram");
    assert_eq!(exported["path"], "/o.png");
    cleanup(
        path,
        bridge,
        "v0119_act_and_write_verbs_send_exact_arg_keys",
    );
}

#[test]
fn regex_flag_is_serialized_only_when_true() {
    let _wd = Watchdog::new(SUITE, "regex_flag_is_serialized_only_when_true");
    let (path, bridge) = spawn_bridge(|stream| {
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
    ed.find_in_tab(TabRef::index(0), None, "a.*b", true)
        .expect("regex find");
    ed.find_in_tab(TabRef::index(0), None, "lit", false)
        .expect("literal find");
    ed.search_project("a.*b", 50, true).expect("regex search");
    ed.search_project("lit", 50, false).expect("literal search");
    cleanup(path, bridge, "regex_flag_is_serialized_only_when_true");
}

#[test]
fn bridge_error_response_maps_to_transport_error() {
    let _wd = Watchdog::new(SUITE, "bridge_error_response_maps_to_transport_error");
    let (path, bridge) = spawn_bridge(|stream| {
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
    cleanup(
        path,
        bridge,
        "bridge_error_response_maps_to_transport_error",
    );
}

#[test]
fn p0a_read_verbs_send_empty_args_and_parse_replies() {
    let _wd = Watchdog::new(SUITE, "p0a_read_verbs_send_empty_args_and_parse_replies");
    let (path, bridge) = spawn_bridge(|stream| {
        let mut reader = BufReader::new(stream.try_clone().expect("clone"));
        let mut stream = stream;
        writeln!(stream, "{GREETING}").unwrap();
        for expected_id in 1..=2u64 {
            let mut line = String::new();
            reader.read_line(&mut line).unwrap();
            let req: Value = serde_json::from_str(&line).unwrap();
            assert_eq!(req["id"], expected_id);
            assert_eq!(req["args"], json!({}), "both p0a verbs are no-arg");
            let result = match req["verb"].as_str().unwrap() {
                "list_languages" => json!({ "languages": ["Plain Text", "Python", "C++"] }),
                // Exact bridge shape (src/mcp_bridge.cpp verbGetCapabilities):
                // no tool_count / tiers on the wire — the tool layer adds them.
                "get_capabilities" => json!({
                    "edition": "Full", "platform": "linux", "version": "0.1.120",
                    "features": { "duckdb": true, "webengine": true, "noter": true }
                }),
                other => panic!("unexpected verb {other}"),
            };
            let resp = json!({ "id": expected_id, "ok": true, "result": result });
            writeln!(stream, "{resp}").unwrap();
        }
    });
    let ed = editor_for(&path);
    let langs = ed.list_languages().expect("list_languages round-trip");
    assert_eq!(langs["languages"][1], "Python");
    let caps = ed.get_capabilities().expect("get_capabilities round-trip");
    assert_eq!(caps["edition"], "Full");
    assert_eq!(caps["features"]["duckdb"], true);
    assert!(caps.get("tool_count").is_none()); // wire shape has no tool_count
    cleanup(
        path,
        bridge,
        "p0a_read_verbs_send_empty_args_and_parse_replies",
    );
}

#[test]
fn phase1_verbs_send_exact_arg_keys() {
    let _wd = Watchdog::new(SUITE, "phase1_verbs_send_exact_arg_keys");
    let (path, bridge) = spawn_bridge(|stream| {
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
                "create_diagram" => {
                    // Optional keys present only when supplied.
                    assert!(
                        req["args"] == json!({})
                            || req["args"]
                                == json!({ "source": "diagram flow\n", "title": "flow" })
                    );
                    json!({ "tab_index": 4, "valid": true, "errors": [] })
                }
                "get_diagram_source" => {
                    assert_eq!(req["args"], json!({ "tab_index": 4 }));
                    json!({ "source": "diagram flow\n" })
                }
                "set_diagram_source" => {
                    assert_eq!(
                        req["args"],
                        json!({ "tab_index": 4, "source": "diagram er\n" })
                    );
                    json!({ "ok": true, "tab_index": 4, "valid": true, "errors": [] })
                }
                "open_noter" => {
                    assert_eq!(req["args"], json!({}));
                    json!({ "opened": true })
                }
                other => panic!("unexpected verb {other}"),
            };
            let resp = json!({ "id": req["id"], "ok": true, "result": result });
            writeln!(stream, "{resp}").unwrap();
        }
    });
    let mut ed = SocketEditor::with_socket_path(path.to_str().unwrap())
        .with_timeouts(Duration::from_secs(1), Duration::from_secs(1))
        .with_approval_timeout(Duration::from_secs(2));
    let created = ed
        .create_diagram(Some("diagram flow\n"), Some("flow"))
        .expect("create_diagram");
    assert_eq!(created["tab_index"], 4);
    ed.create_diagram(None, None).expect("create_diagram bare");
    let src = ed
        .get_diagram_source(TabRef::index(4))
        .expect("get_diagram_source");
    assert_eq!(src["source"], "diagram flow\n");
    let set = ed
        .set_diagram_source(TabRef::index(4), "diagram er\n")
        .expect("set_diagram_source");
    assert_eq!(set["ok"], true);
    assert_eq!(ed.open_noter().expect("open_noter")["opened"], true);
    cleanup(path, bridge, "phase1_verbs_send_exact_arg_keys");
}

#[test]
fn phase2_verbs_send_exact_arg_keys() {
    let _wd = Watchdog::new(SUITE, "phase2_verbs_send_exact_arg_keys");
    let (path, bridge) = spawn_bridge(|stream| {
        let mut reader = BufReader::new(stream.try_clone().expect("clone"));
        let mut stream = stream;
        writeln!(stream, "{GREETING}").unwrap();
        for _ in 0..7 {
            let mut line = String::new();
            if reader.read_line(&mut line).unwrap() == 0 {
                return;
            }
            let req: Value = serde_json::from_str(&line).unwrap();
            let result = match req["verb"].as_str().unwrap() {
                "list_connections" => {
                    assert_eq!(req["args"], json!({}));
                    json!({ "connections": [] })
                }
                "run_query" => {
                    assert_eq!(
                        req["args"],
                        json!({ "connection_name": "demo", "sql": "SELECT 1", "max_rows": 50 })
                    );
                    json!({ "columns": [], "rows": [], "truncated": false, "engine": "sqlite" })
                }
                "list_tables" => {
                    assert_eq!(req["args"], json!({ "connection_name": "demo" }));
                    json!({ "tables": ["t"] })
                }
                "open_data_analyst" => {
                    assert_eq!(req["args"], json!({}));
                    json!({ "opened": true })
                }
                "render_chart" => {
                    // No title key when None.
                    assert_eq!(req["args"], json!({ "spec": { "mark": "bar" } }));
                    assert!(req["args"].get("title").is_none());
                    json!({ "chart_id": "c1", "rendered": true })
                }
                "export_query_results" => {
                    assert_eq!(
                        req["args"],
                        json!({
                            "connection_name": "demo", "sql": "SELECT 1",
                            "path": "/tmp/o.csv", "format": "csv"
                        })
                    );
                    json!({ "ok": true, "path": "/tmp/o.csv", "rows": 1 })
                }
                "export_chart" => {
                    // No scale key when None.
                    assert_eq!(
                        req["args"],
                        json!({ "spec": { "mark": "bar" }, "path": "/tmp/c.png", "format": "png" })
                    );
                    assert!(req["args"].get("scale").is_none());
                    json!({ "path": "/tmp/c.png" })
                }
                other => panic!("unexpected verb {other}"),
            };
            let resp = json!({ "id": req["id"], "ok": true, "result": result });
            writeln!(stream, "{resp}").unwrap();
        }
    });
    let mut ed = SocketEditor::with_socket_path(path.to_str().unwrap())
        .with_timeouts(Duration::from_secs(1), Duration::from_secs(1))
        .with_approval_timeout(Duration::from_secs(2));
    ed.list_connections().expect("list_connections");
    ed.run_query("demo", "SELECT 1", Some(50))
        .expect("run_query");
    ed.list_tables("demo").expect("list_tables");
    ed.open_data_analyst().expect("open_data_analyst");
    let spec = json!({ "mark": "bar" });
    ed.render_chart(&spec, None).expect("render_chart");
    ed.export_query_results("demo", "SELECT 1", "/tmp/o.csv", "csv", None)
        .expect("export_query_results");
    ed.export_chart(&spec, "/tmp/c.png", "png", None)
        .expect("export_chart");
    cleanup(path, bridge, "phase2_verbs_send_exact_arg_keys");
}
