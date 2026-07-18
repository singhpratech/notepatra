// SPDX-License-Identifier: GPL-3.0-or-later
//! Full JSON-RPC round-trips through the server loop with in-memory I/O.

use std::sync::Mutex;

use notepatra_mcp::server::{Server, LATEST_PROTOCOL_VERSION};
use notepatra_mcp::transport::mock::{ApprovalMode, MockEditor};
use serde_json::{json, Value};

/// Serializes the tests that read or write the NOTEPATRA_MCP_VERSION env
/// var (Server::new reads it, set_var is process-global).
static VERSION_ENV_LOCK: Mutex<()> = Mutex::new(());

/// Feeds newline-delimited messages through Server::run (over the given
/// editor) and parses the newline-delimited responses.
fn run_lines_with(editor: MockEditor, lines: &[String]) -> Vec<Value> {
    let input = lines.join("\n") + "\n";
    let mut out: Vec<u8> = Vec::new();
    let mut server = Server::new(editor);
    server
        .run(input.as_bytes(), &mut out)
        .expect("server loop failed");
    String::from_utf8(out)
        .expect("non-UTF-8 output")
        .lines()
        .map(|l| serde_json::from_str(l).expect("response line is not valid JSON"))
        .collect()
}

fn run_lines(lines: &[String]) -> Vec<Value> {
    run_lines_with(MockEditor::default(), lines)
}

fn initialize_line(id: u64, protocol_version: &str) -> String {
    json!({
        "jsonrpc": "2.0", "id": id, "method": "initialize",
        "params": {
            "protocolVersion": protocol_version,
            "capabilities": {},
            "clientInfo": { "name": "test-client", "version": "0.0.0" }
        }
    })
    .to_string()
}

fn call_line(id: u64, tool: &str, arguments: Value) -> String {
    json!({
        "jsonrpc": "2.0", "id": id, "method": "tools/call",
        "params": { "name": tool, "arguments": arguments }
    })
    .to_string()
}

#[test]
fn initialize_handshake() {
    let _guard = VERSION_ENV_LOCK.lock().unwrap_or_else(|e| e.into_inner());
    let responses = run_lines(&[
        initialize_line(1, LATEST_PROTOCOL_VERSION),
        json!({ "jsonrpc": "2.0", "method": "notifications/initialized" }).to_string(),
    ]);
    // The initialized notification must produce no response.
    assert_eq!(responses.len(), 1);
    let r = &responses[0]["result"];
    assert_eq!(responses[0]["id"], 1);
    assert_eq!(r["protocolVersion"], LATEST_PROTOCOL_VERSION);
    assert_eq!(r["serverInfo"]["name"], "notepatra-mcp");
    // Default version comes from Cargo.toml (no env override).
    assert_eq!(r["serverInfo"]["version"], "0.1.120");
    assert!(r["capabilities"]["tools"].is_object());
    assert!(r["capabilities"]["resources"].is_object());
    assert!(r["capabilities"]["prompts"].is_object());
    // listChanged is not supported anywhere — it must be absent, not false.
    for cap in ["tools", "resources", "prompts"] {
        assert!(r["capabilities"][cap].get("listChanged").is_none());
    }
}

#[test]
fn initialize_unknown_version_counter_offers_latest() {
    let responses = run_lines(&[initialize_line(1, "1999-12-31")]);
    assert_eq!(
        responses[0]["result"]["protocolVersion"],
        LATEST_PROTOCOL_VERSION
    );
}

#[test]
fn ping_returns_empty_result() {
    let responses =
        run_lines(&[json!({ "jsonrpc": "2.0", "id": 7, "method": "ping" }).to_string()]);
    assert_eq!(responses[0]["result"], json!({}));
}

#[test]
fn tools_list_shape() {
    let responses = run_lines(&[
        initialize_line(1, LATEST_PROTOCOL_VERSION),
        json!({ "jsonrpc": "2.0", "id": 2, "method": "tools/list" }).to_string(),
    ]);
    let tools = responses[1]["result"]["tools"]
        .as_array()
        .expect("tools must be an array");
    let names: Vec<&str> = tools.iter().map(|t| t["name"].as_str().unwrap()).collect();
    assert_eq!(
        names,
        [
            "open_file",
            "list_open_tabs",
            "read_tab",
            "search_project",
            "get_selection",
            "get_status",
            "app_info",
            "list_recent_files",
            "find_in_tab",
            "new_tab",
            "goto_line",
            "set_language",
            "compare_tabs",
            "format_json",
            "format_sql",
            "format_html",
            "list_notes",
            "read_note",
            "insert_text",
            "replace_selection",
            "apply_edit",
            "save_tab",
            // v0.1.119 — 13 new tools (read / act / write tiers)
            "list_reminders",
            "git_status",
            "git_diff",
            "git_log",
            "git_show",
            "git_branch",
            "validate_npd",
            "run_sql",
            "open_note",
            "create_note",
            "append_note",
            "set_reminder",
            "export_diagram",
            // p0a
            "list_languages",
            "get_capabilities",
            // phase 1
            "create_diagram",
            "get_diagram_source",
            "set_diagram_source",
            "open_noter",
            // phase 2 — data-analyst + charts
            "list_connections",
            "run_query",
            "list_tables",
            "open_data_analyst",
            "render_chart",
            "export_query_results",
            "export_chart"
        ]
    );
    assert_eq!(names.len(), 48);
    for tool in tools {
        assert!(tool["description"].as_str().is_some_and(|d| !d.is_empty()));
        let schema = &tool["inputSchema"];
        assert_eq!(schema["type"], "object");
        assert_eq!(schema["additionalProperties"], false);
        assert!(schema["required"].is_array());
        assert!(schema["properties"].is_object());
    }
}

#[test]
fn tools_call_happy_paths() {
    let responses = run_lines(&[
        initialize_line(1, LATEST_PROTOCOL_VERSION),
        call_line(2, "list_open_tabs", json!({})),
        call_line(3, "read_tab", json!({ "tab_index": 0 })),
        call_line(4, "read_tab", json!({ "title": "NOTES.md" })),
        call_line(
            5,
            "search_project",
            json!({ "query": "lexer", "max_results": 10 }),
        ),
        call_line(6, "get_selection", json!({})),
        call_line(7, "open_file", json!({ "path": "/tmp/new_file.txt" })),
    ]);
    for r in &responses[1..] {
        assert_eq!(r["result"]["isError"], false, "unexpected error in {r}");
        assert_eq!(r["result"]["content"][0]["type"], "text");
    }
    // list_open_tabs: text payload is JSON with the three mock tabs.
    let tabs: Value = serde_json::from_str(
        responses[1]["result"]["content"][0]["text"]
            .as_str()
            .unwrap(),
    )
    .unwrap();
    assert_eq!(tabs.as_array().unwrap().len(), 3);
    assert_eq!(tabs[1]["title"], "NOTES.md");
    // read_tab by index returns the raw content.
    assert!(responses[2]["result"]["content"][0]["text"]
        .as_str()
        .unwrap()
        .contains("hello from notepatra"));
    assert!(responses[3]["result"]["content"][0]["text"]
        .as_str()
        .unwrap()
        .contains("Release notes"));
    // search_project finds the mock NOTES.md line — bridge shape:
    // {"results":[{path,line,text}],"truncated"}.
    let hits: Value = serde_json::from_str(
        responses[4]["result"]["content"][0]["text"]
            .as_str()
            .unwrap(),
    )
    .unwrap();
    assert_eq!(hits["results"][0]["path"], "/home/user/project/NOTES.md");
    assert_eq!(hits["results"][0]["line"], 3);
    assert_eq!(hits["truncated"], false);
    // get_selection returns tab index + text (no title on the wire).
    let sel: Value = serde_json::from_str(
        responses[5]["result"]["content"][0]["text"]
            .as_str()
            .unwrap(),
    )
    .unwrap();
    assert_eq!(sel["tab_index"], 0);
    assert!(sel["text"].as_str().unwrap().contains("println!"));
    // open_file reports the new tab.
    assert!(responses[6]["result"]["content"][0]["text"]
        .as_str()
        .unwrap()
        .contains("/tmp/new_file.txt"));
}

#[test]
fn tool_level_error_is_iserror_result_not_protocol_error() {
    let responses = run_lines(&[call_line(1, "read_tab", json!({ "tab_index": 99 }))]);
    let r = &responses[0]["result"];
    assert_eq!(r["isError"], true);
    assert!(r["content"][0]["text"].as_str().unwrap().contains("no tab"));
    assert!(responses[0].get("error").is_none());
}

#[test]
fn unknown_tool_is_invalid_params_error() {
    let responses = run_lines(&[call_line(1, "delete_everything", json!({}))]);
    assert_eq!(responses[0]["error"]["code"], -32602);
    assert!(responses[0]["error"]["message"]
        .as_str()
        .unwrap()
        .contains("Unknown tool: delete_everything"));
}

#[test]
fn malformed_arguments_are_invalid_params_errors() {
    let responses = run_lines(&[
        call_line(1, "open_file", json!({})), // missing required
        call_line(2, "read_tab", json!({ "tab_index": 0, "title": "x" })), // both selectors
        call_line(3, "search_project", json!({ "query": "x", "bogus": 1 })), // extra key
        call_line(
            4,
            "search_project",
            json!({ "query": "x", "max_results": 0 }),
        ), // < 1
    ]);
    for r in &responses {
        assert_eq!(r["error"]["code"], -32602, "expected -32602 in {r}");
    }
}

#[test]
fn unknown_method_is_method_not_found() {
    let responses =
        run_lines(&[json!({ "jsonrpc": "2.0", "id": 5, "method": "bogus/method" }).to_string()]);
    assert_eq!(responses[0]["error"]["code"], -32601);
    assert_eq!(responses[0]["id"], 5);
}

#[test]
fn malformed_json_is_parse_error_with_null_id() {
    let responses = run_lines(&["{this is not json".to_string()]);
    assert_eq!(responses[0]["error"]["code"], -32700);
    assert_eq!(responses[0]["id"], Value::Null);
}

#[test]
fn non_object_message_is_invalid_request() {
    let responses = run_lines(&["[1,2,3]".to_string()]);
    assert_eq!(responses[0]["error"]["code"], -32600);
}

#[test]
fn unknown_notification_is_ignored_and_eof_shuts_down() {
    // run_lines returning at all proves graceful EOF shutdown.
    let responses = run_lines(&[
        json!({ "jsonrpc": "2.0", "method": "notifications/cancelled", "params": {} }).to_string(),
    ]);
    assert!(responses.is_empty());
}

// ---------------------------------------------------------------------------
// Wave-2 tools (v0.1.118)
// ---------------------------------------------------------------------------

fn text_of(response: &Value) -> &str {
    response["result"]["content"][0]["text"]
        .as_str()
        .expect("text content block")
}

fn json_of(response: &Value) -> Value {
    serde_json::from_str(text_of(response)).expect("tool text payload is JSON")
}

#[test]
fn wave2_tools_call_happy_paths() {
    let responses = run_lines(&[
        initialize_line(1, LATEST_PROTOCOL_VERSION),
        call_line(2, "get_status", json!({})),
        call_line(3, "app_info", json!({})),
        call_line(4, "list_recent_files", json!({})),
        call_line(
            5,
            "find_in_tab",
            json!({ "query": "lexer", "tab_index": 1 }),
        ),
        call_line(6, "new_tab", json!({ "text": "generated content\n" })),
        call_line(7, "goto_line", json!({ "line": 2, "tab_index": 0 })),
        call_line(
            8,
            "set_language",
            json!({ "language": "SQL", "tab_index": 2 }),
        ),
        call_line(9, "compare_tabs", json!({ "index_a": 0, "index_b": 1 })),
        call_line(10, "list_notes", json!({})),
        call_line(
            11,
            "read_note",
            json!({ "file": "/home/user/Documents/Notepatra/Noter/release-checklist.html" }),
        ),
    ]);
    for r in &responses[1..] {
        assert_eq!(r["result"]["isError"], false, "unexpected error in {r}");
    }
    let status = json_of(&responses[1]);
    assert_eq!(status["title"], "main.rs");
    assert_eq!(status["language"], "Rust");
    assert_eq!(status["encoding"], "UTF-8");
    assert!(status["cursor_line"].is_u64());
    let info = json_of(&responses[2]);
    assert_eq!(info["name"], "Notepatra");
    assert!(info["version"].as_str().is_some_and(|v| !v.is_empty()));
    assert!(info["edition"].as_str().is_some());
    assert!(info["platform"].as_str().is_some());
    let recent = json_of(&responses[3]);
    assert!(!recent["files"].as_array().unwrap().is_empty());
    let found = json_of(&responses[4]);
    assert_eq!(found["matches"][0]["line"], 3);
    assert_eq!(found["truncated"], false);
    // Mock has "Untitled 1" open, so the new tab lands at index 3.
    let new_tab = json_of(&responses[5]);
    assert_eq!(new_tab["tab_index"], 3);
    // goto_line/set_language echo the resolved tab per the bridge shape.
    let went = json_of(&responses[6]);
    assert_eq!(went["ok"], true);
    assert_eq!(went["tab_index"], 0);
    assert_eq!(went["line"], 2);
    let lang = json_of(&responses[7]);
    assert_eq!(lang["ok"], true);
    assert_eq!(lang["tab_index"], 2);
    assert_eq!(lang["language"], "SQL");
    assert_eq!(json_of(&responses[8])["opened"], true);
    let notes = json_of(&responses[9]);
    let list = notes["notes"].as_array().unwrap();
    assert_eq!(list.len(), 4);
    assert_eq!(list[0]["title"], "Standup 2026-07-15");
    assert!(list[0]["file"].as_str().unwrap().ends_with(".html"));
    assert!(list[0]["modified_iso"].as_str().unwrap().contains('T'));
    let note = json_of(&responses[10]);
    assert_eq!(note["title"], "Release checklist");
    assert!(note["text"].as_str().unwrap().contains("release-check.sh"));
}

#[test]
fn set_language_is_reflected_in_status() {
    // Contract: picking a language actually changes the tab's language.
    let responses = run_lines(&[
        call_line(1, "goto_line", json!({ "line": 1, "tab_index": 2 })), // focus tab 2
        call_line(2, "set_language", json!({ "language": "Python" })),
        call_line(3, "get_status", json!({})),
    ]);
    let status = json_of(&responses[2]);
    assert_eq!(status["tab_index"], 2);
    assert_eq!(status["language"], "Python");
}

#[test]
fn format_tools_return_formatted_text() {
    let responses = run_lines(&[
        call_line(1, "format_json", json!({ "text": "{\"b\":1,\"a\":[2,3]}" })),
        call_line(2, "format_sql", json!({ "text": "select 1   \nfrom t  " })),
        call_line(3, "format_html", json!({ "text": "<p>hi</p>   " })),
    ]);
    for r in &responses {
        assert_eq!(r["result"]["isError"], false, "unexpected error in {r}");
    }
    let pretty = text_of(&responses[0]);
    assert!(pretty.contains("\"b\": 1"), "not pretty-printed: {pretty}");
    assert!(pretty.ends_with('\n'));
    assert_eq!(text_of(&responses[1]), "select 1\nfrom t\n");
    assert_eq!(text_of(&responses[2]), "<p>hi</p>\n");
}

#[test]
fn wave2_tool_level_errors_are_iserror_results() {
    let responses = run_lines(&[
        call_line(1, "find_in_tab", json!({ "query": "x", "tab_index": 99 })),
        call_line(2, "read_note", json!({ "file": "no-such-note.md" })),
        call_line(3, "format_json", json!({ "text": "{not json" })),
        call_line(4, "compare_tabs", json!({ "index_a": 1, "index_b": 1 })),
    ]);
    for r in &responses {
        assert_eq!(r["result"]["isError"], true, "expected isError in {r}");
        assert!(r.get("error").is_none());
    }
    assert!(text_of(&responses[0]).contains("no tab at index 99"));
    assert!(text_of(&responses[1]).contains("no note named"));
    assert!(text_of(&responses[2]).contains("invalid JSON"));
    assert!(text_of(&responses[3]).contains("two different tabs"));
}

#[test]
fn wave2_malformed_arguments_are_invalid_params_errors() {
    let responses = run_lines(&[
        call_line(1, "find_in_tab", json!({})), // missing query
        call_line(2, "goto_line", json!({ "line": 0 })), // < 1
        call_line(3, "goto_line", json!({})),   // missing line
        call_line(4, "compare_tabs", json!({ "index_a": 0 })), // missing index_b
        call_line(5, "set_language", json!({})), // missing language
        call_line(6, "format_json", json!({})), // missing text
        call_line(7, "read_note", json!({ "file": 3 })), // mistyped
        call_line(8, "get_status", json!({ "bogus": 1 })), // extra key
        call_line(9, "new_tab", json!({ "text": 5 })), // mistyped
    ]);
    for r in &responses {
        assert_eq!(r["error"]["code"], -32602, "expected -32602 in {r}");
    }
}

// ---------------------------------------------------------------------------
// Resources (spec 2025-06-18)
// ---------------------------------------------------------------------------

fn request_line(id: u64, method: &str, params: Value) -> String {
    json!({ "jsonrpc": "2.0", "id": id, "method": method, "params": params }).to_string()
}

#[test]
fn resources_list_exposes_tabs_and_notes() {
    let responses = run_lines(&[request_line(1, "resources/list", json!({}))]);
    let resources = responses[0]["result"]["resources"].as_array().unwrap();
    // 3 mock tabs + 4 mock notes.
    assert_eq!(resources.len(), 7);
    assert_eq!(resources[0]["uri"], "notepatra://tab/0");
    assert_eq!(resources[0]["name"], "main.rs");
    // Note URIs are root-relative: the basename of the absolute path that
    // list_notes reports (full-path fallback only on basename collisions).
    assert_eq!(
        resources[3]["uri"],
        "notepatra://note/standup-2026-07-15.html"
    );
    assert_eq!(resources[3]["name"], "Standup 2026-07-15");
    for r in resources {
        assert_eq!(r["mimeType"], "text/plain");
    }
}

#[test]
fn resources_read_round_trips_tab_and_note() {
    let responses = run_lines(&[
        request_line(1, "resources/read", json!({ "uri": "notepatra://tab/1" })),
        request_line(
            2,
            "resources/read",
            json!({ "uri": "notepatra://note/meeting-design.html" }),
        ),
    ]);
    let tab = &responses[0]["result"]["contents"][0];
    assert_eq!(tab["uri"], "notepatra://tab/1");
    assert_eq!(tab["mimeType"], "text/plain");
    assert!(tab["text"].as_str().unwrap().contains("Release notes"));
    let note = &responses[1]["result"]["contents"][0];
    assert_eq!(note["uri"], "notepatra://note/meeting-design.html");
    assert!(note["text"].as_str().unwrap().contains("dark theme parity"));
}

#[test]
fn unknown_resources_are_not_found_errors() {
    let responses = run_lines(&[
        request_line(1, "resources/read", json!({ "uri": "file:///etc/passwd" })),
        request_line(2, "resources/read", json!({ "uri": "notepatra://tab/99" })),
        request_line(3, "resources/read", json!({ "uri": "notepatra://tab/xyz" })),
        request_line(
            4,
            "resources/read",
            json!({ "uri": "notepatra://note/ghost.md" }),
        ),
    ]);
    for r in &responses {
        assert_eq!(r["error"]["code"], -32002, "expected -32002 in {r}");
        assert_eq!(r["error"]["message"], "Resource not found");
        assert!(r["error"]["data"]["uri"].as_str().is_some());
    }
}

#[test]
fn resources_read_without_uri_is_invalid_params() {
    let responses = run_lines(&[request_line(1, "resources/read", json!({}))]);
    assert_eq!(responses[0]["error"]["code"], -32602);
}

// ---------------------------------------------------------------------------
// Prompts (spec 2025-06-18)
// ---------------------------------------------------------------------------

#[test]
fn prompts_list_shape() {
    let responses = run_lines(&[request_line(1, "prompts/list", json!({}))]);
    let prompts = responses[0]["result"]["prompts"].as_array().unwrap();
    let names: Vec<&str> = prompts
        .iter()
        .map(|p| p["name"].as_str().unwrap())
        .collect();
    assert_eq!(
        names,
        [
            "review-current-file",
            "explain-selection",
            "summarize-notes"
        ]
    );
    for p in prompts {
        assert!(p["description"].as_str().is_some_and(|d| !d.is_empty()));
        assert!(p["arguments"].is_array());
    }
}

fn prompt_text(response: &Value) -> &str {
    let messages = response["result"]["messages"].as_array().unwrap();
    assert_eq!(messages.len(), 1);
    assert_eq!(messages[0]["role"], "user");
    assert_eq!(messages[0]["content"]["type"], "text");
    messages[0]["content"]["text"].as_str().unwrap()
}

#[test]
fn prompts_get_embeds_editor_state() {
    let responses = run_lines(&[
        request_line(1, "prompts/get", json!({ "name": "review-current-file" })),
        request_line(2, "prompts/get", json!({ "name": "explain-selection" })),
        request_line(3, "prompts/get", json!({ "name": "summarize-notes" })),
    ]);
    // review-current-file embeds the active tab (mock: main.rs).
    let review = prompt_text(&responses[0]);
    assert!(review.contains("main.rs"));
    assert!(review.contains("hello from notepatra"));
    // explain-selection embeds the selected text (the wire carries no tab
    // title for a selection, so none is asserted).
    let explain = prompt_text(&responses[1]);
    assert!(explain.contains("println!"));
    // summarize-notes embeds at most 3 notes: the 4th mock note is excluded.
    let summarize = prompt_text(&responses[2]);
    assert!(summarize.contains("Standup"));
    assert!(summarize.contains("Release checklist"));
    assert!(summarize.contains("Design sync"));
    assert!(!summarize.contains("old ideas"));
    assert!(summarize.contains("1 more note"));
    for r in &responses {
        assert!(r["result"]["description"].as_str().is_some());
    }
}

#[test]
fn unknown_prompt_is_invalid_params_error() {
    let responses = run_lines(&[
        request_line(1, "prompts/get", json!({ "name": "no-such-prompt" })),
        request_line(2, "prompts/get", json!({})),
    ]);
    for r in &responses {
        assert_eq!(r["error"]["code"], -32602, "expected -32602 in {r}");
    }
    assert!(responses[0]["error"]["message"]
        .as_str()
        .unwrap()
        .contains("unknown prompt: no-such-prompt"));
}

// ---------------------------------------------------------------------------
// Write tools (v0.1.118) — approval-gated in the editor
// ---------------------------------------------------------------------------

const APPROVAL_SENTENCE: &str = "Requires the user to click Approve on a card inside Notepatra; \
     the call blocks until they respond (up to 2 minutes) and returns an error if denied \
     or timed out.";

#[test]
fn write_tool_descriptions_state_the_approval_gate() {
    let responses =
        run_lines(&[json!({ "jsonrpc": "2.0", "id": 1, "method": "tools/list" }).to_string()]);
    let tools = responses[0]["result"]["tools"].as_array().unwrap();
    assert_eq!(tools.len(), 48);
    let write_tools = [
        "insert_text",
        "replace_selection",
        "apply_edit",
        "save_tab",
        "create_note",
        "append_note",
        "set_reminder",
        "export_diagram",
        "set_diagram_source",
        "export_query_results",
        "export_chart",
    ];
    for tool in tools {
        let name = tool["name"].as_str().unwrap();
        let description = tool["description"].as_str().unwrap();
        if write_tools.contains(&name) {
            assert!(
                description.contains(APPROVAL_SENTENCE),
                "{name} must state the approval gate: {description}"
            );
        } else {
            assert!(
                !description.contains("click Approve"),
                "read tool {name} must not claim an approval gate"
            );
        }
    }
    // search_project documents the server-side result cap.
    let search = tools
        .iter()
        .find(|t| t["name"] == "search_project")
        .unwrap();
    assert!(search["description"]
        .as_str()
        .unwrap()
        .contains("caps results at 200"));
}

#[test]
fn write_tools_happy_paths() {
    let responses = run_lines(&[
        call_line(
            1,
            "insert_text",
            json!({ "text": "// header\n", "tab_index": 0, "line": 1, "col": 1 }),
        ),
        call_line(2, "read_tab", json!({ "tab_index": 0 })),
        call_line(
            3,
            "replace_selection",
            json!({ "text": "eprintln!(\"replaced\");" }),
        ),
        call_line(4, "read_tab", json!({ "tab_index": 0 })),
        call_line(
            5,
            "apply_edit",
            json!({ "find": "lexer", "replace": "parser", "tab_index": 1 }),
        ),
        call_line(
            6,
            "apply_edit",
            json!({ "find": "- ", "replace": "* ", "tab_index": 1, "all": true }),
        ),
        call_line(7, "read_tab", json!({ "tab_index": 1 })),
        call_line(8, "save_tab", json!({ "tab_index": 1 })),
        call_line(9, "list_open_tabs", json!({})),
    ]);
    for r in &responses {
        assert_eq!(r["result"]["isError"], false, "unexpected error in {r}");
    }
    // insert_text echoes the resolved tab.
    let inserted = json_of(&responses[0]);
    assert_eq!(inserted["ok"], true);
    assert_eq!(inserted["tab_index"], 0);
    assert!(text_of(&responses[1]).starts_with("// header\nfn main()"));
    // replace_selection swaps the selected text in the active tab.
    assert_eq!(json_of(&responses[2]), json!({ "ok": true }));
    let after_replace = text_of(&responses[3]);
    assert!(after_replace.contains("eprintln!(\"replaced\");"));
    assert!(!after_replace.contains("println!(\"hello"));
    // apply_edit: first-only by default, all:true replaces every occurrence.
    let first_only = json_of(&responses[4]);
    assert_eq!(first_only["ok"], true);
    assert_eq!(first_only["count"], 1);
    let all = json_of(&responses[5]);
    assert_eq!(all["ok"], true);
    assert_eq!(all["count"], 2);
    let notes_tab = text_of(&responses[6]);
    assert!(notes_tab.contains("parser edge case"));
    assert!(notes_tab.contains("* fix"));
    assert!(notes_tab.contains("* ship"));
    // save_tab clears the modified flag (the user-visible contract).
    assert_eq!(json_of(&responses[7]), json!({ "ok": true }));
    let tabs = json_of(&responses[8]);
    assert_eq!(tabs[1]["modified"], false);
}

#[test]
fn denied_write_tools_return_the_verbatim_error() {
    let mut editor = MockEditor::default();
    editor.set_approval(ApprovalMode::Deny);
    let responses = run_lines_with(
        editor,
        &[
            call_line(1, "insert_text", json!({ "text": "x" })),
            call_line(2, "replace_selection", json!({ "text": "x" })),
            call_line(3, "apply_edit", json!({ "find": "a", "replace": "b" })),
            call_line(4, "save_tab", json!({})),
            // Read tools are NOT approval-gated: they keep working on deny.
            call_line(5, "read_tab", json!({ "tab_index": 0 })),
        ],
    );
    for r in &responses[..4] {
        assert_eq!(r["result"]["isError"], true, "expected isError in {r}");
        assert_eq!(text_of(r), "denied by user");
        assert!(r.get("error").is_none());
    }
    assert_eq!(responses[4]["result"]["isError"], false);
}

#[test]
fn timed_out_approvals_return_the_verbatim_error() {
    let mut editor = MockEditor::default();
    editor.set_approval(ApprovalMode::Timeout);
    let responses = run_lines_with(
        editor,
        &[
            call_line(1, "insert_text", json!({ "text": "x" })),
            call_line(2, "replace_selection", json!({ "text": "x" })),
            call_line(3, "apply_edit", json!({ "find": "a", "replace": "b" })),
            call_line(4, "save_tab", json!({})),
        ],
    );
    for r in &responses {
        assert_eq!(r["result"]["isError"], true, "expected isError in {r}");
        assert_eq!(text_of(r), "approval timed out");
    }
}

#[test]
fn write_tools_malformed_arguments_are_invalid_params_errors() {
    let responses = run_lines(&[
        call_line(1, "insert_text", json!({})), // missing text
        call_line(2, "insert_text", json!({ "text": "x", "line": 0 })), // line < 1
        call_line(3, "insert_text", json!({ "text": "x", "col": 0 })), // col < 1
        call_line(4, "insert_text", json!({ "text": 7 })), // mistyped
        call_line(5, "replace_selection", json!({})), // missing text
        call_line(6, "apply_edit", json!({ "find": "a" })), // missing replace
        call_line(
            7,
            "apply_edit",
            json!({ "find": "a", "replace": "b", "all": 1 }),
        ), // mistyped all
        call_line(8, "save_tab", json!({ "tab_index": -1 })), // negative
        call_line(9, "save_tab", json!({ "bogus": true })), // extra key
    ]);
    for r in &responses {
        assert_eq!(r["error"]["code"], -32602, "expected -32602 in {r}");
    }
}

#[test]
fn write_tool_errors_on_bad_tabs_are_iserror_results() {
    let responses = run_lines(&[
        call_line(1, "insert_text", json!({ "text": "x", "tab_index": 99 })),
        call_line(2, "save_tab", json!({ "tab_index": 99 })),
    ]);
    for r in &responses {
        assert_eq!(r["result"]["isError"], true, "expected isError in {r}");
        assert!(text_of(r).contains("no tab at index 99"));
    }
}

// ---------------------------------------------------------------------------
// E2E-report gap fixes (v0.1.118)
// ---------------------------------------------------------------------------

#[test]
fn truncated_reads_carry_the_marker_in_tool_and_resource_text() {
    let mut editor = MockEditor::default();
    editor.simulate_truncated_tab(0);
    let responses = run_lines_with(
        editor,
        &[
            call_line(1, "read_tab", json!({ "tab_index": 0 })),
            request_line(2, "resources/read", json!({ "uri": "notepatra://tab/0" })),
            // An untruncated tab must NOT carry the marker.
            call_line(3, "read_tab", json!({ "tab_index": 1 })),
        ],
    );
    assert!(text_of(&responses[0]).ends_with("\n[truncated at 5 MB]"));
    assert!(responses[1]["result"]["contents"][0]["text"]
        .as_str()
        .unwrap()
        .ends_with("\n[truncated at 5 MB]"));
    assert!(!text_of(&responses[2]).contains("[truncated at 5 MB]"));
}

#[test]
fn duplicate_note_basenames_fall_back_to_encoded_full_paths() {
    let mut editor = MockEditor::default();
    editor.add_note(
        "Standup (archived)",
        "/home/user/Documents/Notepatra/Noter/archive/standup-2026-07-15.html",
        "archived standup\n",
    );
    let archived_uri = format!(
        "notepatra://note/{}",
        "/home/user/Documents/Notepatra/Noter/archive/standup-2026-07-15.html".replace('/', "%2F")
    );
    let original_uri = format!(
        "notepatra://note/{}",
        "/home/user/Documents/Notepatra/Noter/standup-2026-07-15.html".replace('/', "%2F")
    );
    let responses = run_lines_with(
        editor,
        &[
            request_line(1, "resources/list", json!({})),
            request_line(2, "resources/read", json!({ "uri": archived_uri.clone() })),
            request_line(3, "resources/read", json!({ "uri": original_uri.clone() })),
            // The ambiguous basename URI resolves to neither collided note.
            request_line(
                4,
                "resources/read",
                json!({ "uri": "notepatra://note/standup-2026-07-15.html" }),
            ),
        ],
    );
    let resources = responses[0]["result"]["resources"].as_array().unwrap();
    // 3 tabs + 5 notes.
    assert_eq!(resources.len(), 8);
    let uris: Vec<&str> = resources
        .iter()
        .map(|r| r["uri"].as_str().unwrap())
        .collect();
    assert!(uris.contains(&original_uri.as_str()), "uris: {uris:?}");
    assert!(uris.contains(&archived_uri.as_str()), "uris: {uris:?}");
    assert!(
        !uris.contains(&"notepatra://note/standup-2026-07-15.html"),
        "colliding basename must not be listed bare: {uris:?}"
    );
    // Non-colliding notes keep their basename URIs.
    assert!(uris.contains(&"notepatra://note/release-checklist.html"));
    assert!(responses[1]["result"]["contents"][0]["text"]
        .as_str()
        .unwrap()
        .contains("archived standup"));
    assert!(responses[2]["result"]["contents"][0]["text"]
        .as_str()
        .unwrap()
        .contains("shipped lexer fix"));
    assert_eq!(responses[3]["error"]["code"], -32002);
}

#[test]
fn serverinfo_version_env_override_wins() {
    let _guard = VERSION_ENV_LOCK.lock().unwrap_or_else(|e| e.into_inner());
    std::env::set_var("NOTEPATRA_MCP_VERSION", "9.9.9-test");
    let responses = run_lines(&[initialize_line(1, LATEST_PROTOCOL_VERSION)]);
    std::env::remove_var("NOTEPATRA_MCP_VERSION");
    assert_eq!(
        responses[0]["result"]["serverInfo"]["version"],
        "9.9.9-test"
    );
}

// ---------------------------------------------------------------------------
// v0.1.119 "MCP depth" — 13 new tools + regex flag
// ---------------------------------------------------------------------------

const RELEASE_NOTE: &str = "/home/user/Documents/Notepatra/Noter/release-checklist.html";

#[test]
fn v0119_read_tools_happy_paths() {
    let responses = run_lines(&[
        call_line(1, "list_reminders", json!({})),
        call_line(2, "git_status", json!({})),
        call_line(3, "git_diff", json!({})),
        call_line(4, "git_diff", json!({ "path": "src/tools.rs" })),
        call_line(5, "git_log", json!({})),
        call_line(6, "git_log", json!({ "limit": 2 })),
        call_line(7, "git_show", json!({ "ref": "4d32cc8" })),
        call_line(8, "git_branch", json!({})),
        call_line(9, "validate_npd", json!({ "source": "node a\nnode b\n" })),
        call_line(10, "run_sql", json!({ "sql": "SELECT id, name FROM t" })),
    ]);
    for r in &responses {
        assert_eq!(r["result"]["isError"], false, "unexpected error in {r}");
    }
    // list_reminders: four temporal buckets, each bound to a note file
    // (bridge keys: note_file / note_title).
    let reminders = json_of(&responses[0]);
    let list = reminders["reminders"].as_array().unwrap();
    assert_eq!(list.len(), 4);
    let buckets: Vec<&str> = list.iter().map(|r| r["bucket"].as_str().unwrap()).collect();
    assert_eq!(buckets, ["Overdue", "Today", "This week", "Later"]);
    assert!(list[0]["note_file"].as_str().unwrap().ends_with(".html"));
    assert!(list[0]["note_title"].as_str().is_some());
    assert!(list[0]["due_iso"].as_str().unwrap().contains('T'));
    // git verbs: raw CLI text under a single "output" key.
    assert!(json_of(&responses[1])["output"]
        .as_str()
        .unwrap()
        .contains("branch"));
    assert!(json_of(&responses[2])["output"]
        .as_str()
        .unwrap()
        .contains("diff --git"));
    assert!(json_of(&responses[3])["output"]
        .as_str()
        .unwrap()
        .contains("src/tools.rs"));
    // git_log: default limit returns text; limit=2 caps to two lines.
    assert!(!json_of(&responses[4])["output"]
        .as_str()
        .unwrap()
        .is_empty());
    let log2 = json_of(&responses[5]);
    assert_eq!(log2["output"].as_str().unwrap().trim().lines().count(), 2);
    // git_show: echoes the ref inside the output text.
    assert!(json_of(&responses[6])["output"]
        .as_str()
        .unwrap()
        .contains("4d32cc8"));
    // git_branch: output lists branches.
    assert!(json_of(&responses[7])["output"]
        .as_str()
        .unwrap()
        .contains("main"));
    // validate_npd on clean source: valid, no errors.
    let npd = json_of(&responses[8]);
    assert_eq!(npd["valid"], true);
    assert!(npd["errors"].as_array().unwrap().is_empty());
    // run_sql SELECT: columns + rows + truncated + engine.
    let sql = json_of(&responses[9]);
    assert_eq!(sql["columns"], json!(["id", "name"]));
    assert_eq!(sql["truncated"], false);
    assert!(sql["engine"].as_str().is_some());
    assert_eq!(sql["rows"].as_array().unwrap().len(), 2);
}

#[test]
fn validate_npd_reports_parse_errors_and_reads_a_tab() {
    let responses = run_lines(&[
        // Source with a malformed "!!" directive on line 2.
        call_line(
            1,
            "validate_npd",
            json!({ "source": "ok line\n!! bad directive\n" }),
        ),
        // Also works against an open tab (the mock's main.rs at index 0).
        call_line(2, "validate_npd", json!({ "tab_index": 0 })),
    ]);
    let npd = json_of(&responses[0]);
    assert_eq!(npd["valid"], false);
    let errors = npd["errors"].as_array().unwrap();
    assert_eq!(errors.len(), 1);
    assert_eq!(errors[0]["line"], 2);
    assert_eq!(errors[0]["message"], "unknown directive");
    // Reading a tab's (valid) content parses clean.
    assert_eq!(json_of(&responses[1])["valid"], true);
}

#[test]
fn validate_npd_requires_exactly_one_selector() {
    let responses = run_lines(&[
        call_line(1, "validate_npd", json!({})), // neither
        call_line(2, "validate_npd", json!({ "tab_index": 0, "source": "x" })), // both
    ]);
    for r in &responses {
        assert_eq!(r["error"]["code"], -32602, "expected -32602 in {r}");
    }
}

#[test]
fn run_sql_rejects_non_select_statements() {
    let responses = run_lines(&[
        call_line(1, "run_sql", json!({ "sql": "UPDATE t SET x = 1" })),
        call_line(2, "run_sql", json!({ "sql": "DROP TABLE t" })),
    ]);
    for r in &responses {
        assert_eq!(r["result"]["isError"], true, "expected isError in {r}");
        assert!(text_of(r).contains("SELECT"));
    }
}

#[test]
fn open_note_opens_a_tab_and_errors_on_unknown() {
    let responses = run_lines(&[
        call_line(1, "open_note", json!({ "file": RELEASE_NOTE })),
        call_line(2, "open_note", json!({ "file": "/no/such/note.html" })),
    ]);
    let opened = json_of(&responses[0]);
    assert_eq!(opened["opened"], true);
    // Bridge shape: {opened, title}.
    assert!(opened["title"].as_str().is_some());
    assert_eq!(responses[1]["result"]["isError"], true);
    assert!(text_of(&responses[1]).contains("no note named"));
}

#[test]
fn v0119_write_tools_happy_paths() {
    let responses = run_lines(&[
        call_line(
            1,
            "create_note",
            json!({ "title": "New Note", "body": "hello body" }),
        ),
        call_line(
            2,
            "append_note",
            json!({ "file": RELEASE_NOTE, "text": "\n- one more" }),
        ),
        call_line(
            3,
            "set_reminder",
            json!({ "file": RELEASE_NOTE, "due_iso": "2026-07-20T09:00:00Z" }),
        ),
        call_line(
            4,
            "export_diagram",
            json!({ "tab_index": 0, "path": "/tmp/out.png", "format": "png" }),
        ),
        // The created note now exists and is readable.
        call_line(5, "list_reminders", json!({})),
    ]);
    for r in &responses {
        assert_eq!(r["result"]["isError"], false, "unexpected error in {r}");
    }
    // Bridge write-verb result shapes (v0.1.119, byte-exact with mcp_bridge).
    let created = json_of(&responses[0]);
    assert!(created["file"].as_str().unwrap().ends_with(".html"));
    assert!(created["file"].as_str().unwrap().contains("Noter"));
    assert_eq!(created["title"], "New Note");
    assert_eq!(json_of(&responses[1]), json!({ "file": RELEASE_NOTE }));
    assert_eq!(
        json_of(&responses[2]),
        json!({ "file": RELEASE_NOTE, "due_iso": "2026-07-20T09:00:00Z" })
    );
    assert_eq!(json_of(&responses[3]), json!({ "path": "/tmp/out.png" }));
    // set_reminder added a fifth reminder.
    assert_eq!(
        json_of(&responses[4])["reminders"]
            .as_array()
            .unwrap()
            .len(),
        5
    );
}

#[test]
fn v0119_write_tool_descriptions_state_the_approval_gate() {
    // Belt-and-suspenders alongside the count test: the four new write tools
    // carry the exact approval sentence.
    let responses =
        run_lines(&[json!({ "jsonrpc": "2.0", "id": 1, "method": "tools/list" }).to_string()]);
    let tools = responses[0]["result"]["tools"].as_array().unwrap();
    for name in [
        "create_note",
        "append_note",
        "set_reminder",
        "export_diagram",
    ] {
        let tool = tools.iter().find(|t| t["name"] == name).unwrap();
        assert!(
            tool["description"]
                .as_str()
                .unwrap()
                .contains(APPROVAL_SENTENCE),
            "{name} must state the approval gate"
        );
    }
    // The new read/act tools must NOT claim an approval gate.
    for name in ["list_reminders", "git_status", "run_sql", "open_note"] {
        let tool = tools.iter().find(|t| t["name"] == name).unwrap();
        assert!(
            !tool["description"]
                .as_str()
                .unwrap()
                .contains("click Approve"),
            "{name} must not claim an approval gate"
        );
    }
}

#[test]
fn v0119_write_tools_denied_and_timed_out_pass_through_verbatim() {
    for (mode, expected) in [
        (ApprovalMode::Deny, "denied by user"),
        (ApprovalMode::Timeout, "approval timed out"),
    ] {
        let mut editor = MockEditor::default();
        editor.set_approval(mode);
        let responses = run_lines_with(
            editor,
            &[
                call_line(1, "create_note", json!({ "title": "t", "body": "b" })),
                call_line(
                    2,
                    "append_note",
                    json!({ "file": RELEASE_NOTE, "text": "x" }),
                ),
                call_line(
                    3,
                    "set_reminder",
                    json!({ "file": RELEASE_NOTE, "due_iso": "2026-07-20T09:00:00Z" }),
                ),
                call_line(
                    4,
                    "export_diagram",
                    json!({ "tab_index": 0, "path": "/tmp/o.pdf", "format": "pdf" }),
                ),
            ],
        );
        for r in &responses {
            assert_eq!(r["result"]["isError"], true, "expected isError in {r}");
            assert_eq!(text_of(r), expected);
        }
    }
}

#[test]
fn v0119_write_tools_malformed_arguments_are_invalid_params() {
    let responses = run_lines(&[
        call_line(1, "create_note", json!({ "title": "t" })), // missing body
        call_line(2, "append_note", json!({ "file": RELEASE_NOTE })), // missing text
        call_line(3, "set_reminder", json!({ "file": RELEASE_NOTE })), // missing due_iso
        call_line(
            4,
            "export_diagram",
            json!({ "tab_index": 0, "path": "/tmp/o.gif", "format": "gif" }),
        ), // bad format
        call_line(
            5,
            "export_diagram",
            json!({ "path": "/tmp/o.png", "format": "png" }),
        ), // missing tab_index
        call_line(6, "git_show", json!({})),                  // missing ref
        call_line(7, "git_log", json!({ "limit": 0 })),       // < 1
        call_line(8, "run_sql", json!({})),                   // missing sql
        call_line(9, "open_note", json!({ "file": 3 })),      // mistyped
        call_line(10, "list_reminders", json!({ "bogus": 1 })), // extra key
    ]);
    for r in &responses {
        assert_eq!(r["error"]["code"], -32602, "expected -32602 in {r}");
    }
}

#[test]
fn regex_flag_is_validated_on_find_and_search() {
    let responses = run_lines(&[
        // Valid regex flag: accepted (mock falls back to substring matching).
        call_line(
            1,
            "find_in_tab",
            json!({ "query": "main", "tab_index": 0, "regex": true }),
        ),
        call_line(
            2,
            "search_project",
            json!({ "query": "lexer", "regex": true }),
        ),
        // Invalid patterns: the server rejects them as tool errors.
        call_line(
            3,
            "find_in_tab",
            json!({ "query": "unbalanced(", "tab_index": 0, "regex": true }),
        ),
        call_line(
            4,
            "search_project",
            json!({ "query": "bad[", "regex": true }),
        ),
        // regex:false (default) never validates — a bare "(" is a literal.
        call_line(
            5,
            "find_in_tab",
            json!({ "query": "(", "tab_index": 0, "regex": false }),
        ),
    ]);
    assert_eq!(responses[0]["result"]["isError"], false);
    assert_eq!(responses[1]["result"]["isError"], false);
    assert_eq!(responses[2]["result"]["isError"], true);
    assert!(text_of(&responses[2]).contains("invalid regular expression"));
    assert_eq!(responses[3]["result"]["isError"], true);
    assert!(text_of(&responses[3]).contains("invalid regular expression"));
    assert_eq!(responses[4]["result"]["isError"], false);
}

#[test]
fn search_project_clamps_max_results_to_200() {
    // 250 matching lines in a fresh tab; asking for 1000 must clamp to the
    // editor's 200-result cap and flag truncation.
    let body = (0..250)
        .map(|i| format!("zz {i}"))
        .collect::<Vec<_>>()
        .join("\n");
    let responses = run_lines(&[
        call_line(1, "new_tab", json!({ "text": body })),
        call_line(
            2,
            "search_project",
            json!({ "query": "zz", "max_results": 1000 }),
        ),
    ]);
    let hits = json_of(&responses[1]);
    assert_eq!(hits["results"].as_array().unwrap().len(), 200);
    assert_eq!(hits["truncated"], true);
}

#[test]
fn p0a_list_languages_and_get_capabilities() {
    let responses = run_lines(&[
        initialize_line(1, LATEST_PROTOCOL_VERSION),
        call_line(2, "list_languages", json!({})),
        call_line(3, "get_capabilities", json!({})),
        call_line(4, "get_capabilities", json!({ "bogus": 1 })),
    ]);
    // list_languages: canonical tokens, exact case.
    assert_eq!(responses[1]["result"]["isError"], false);
    let langs: Value =
        serde_json::from_str(responses[1]["result"]["content"][0]["text"].as_str().unwrap())
            .unwrap();
    let arr = langs["languages"].as_array().unwrap();
    assert_eq!(arr[0], "Plain Text");
    assert!(arr.iter().any(|l| l == "Python"));
    assert!(arr.iter().any(|l| l == "C++"));
    // get_capabilities: editor fields pass through; tool_count and tiers are
    // injected by the tool layer and MUST match the definitions list.
    assert_eq!(responses[2]["result"]["isError"], false);
    let caps: Value =
        serde_json::from_str(responses[2]["result"]["content"][0]["text"].as_str().unwrap())
            .unwrap();
    let expected = notepatra_mcp::tools::definitions().as_array().unwrap().len();
    assert_eq!(caps["tool_count"], expected as u64);
    assert_eq!(caps["tool_count"], 48);
    assert_eq!(caps["tiers"]["read"], 24);
    assert_eq!(caps["tiers"]["act"], 13);
    assert_eq!(caps["tiers"]["write"], 11);
    assert!(caps["features"]["duckdb"].is_boolean());
    assert!(caps["features"]["webengine"].is_boolean());
    assert!(caps["features"]["noter"].is_boolean());
    assert_eq!(caps["edition"], "Lite"); // mock's canned edition
    // Extra argument → -32602 (additionalProperties: false enforced).
    assert_eq!(responses[3]["error"]["code"], -32602);
}

#[test]
fn p0a_tier_lists_partition_the_tool_surface() {
    use notepatra_mcp::tools::{definitions, ACT_TOOLS, READ_TOOLS, WRITE_TOOLS};
    let defs = definitions();
    let names: std::collections::BTreeSet<String> = defs
        .as_array()
        .unwrap()
        .iter()
        .map(|t| t["name"].as_str().unwrap().to_string())
        .collect();
    let mut tiered: Vec<&str> = Vec::new();
    tiered.extend(READ_TOOLS);
    tiered.extend(ACT_TOOLS);
    tiered.extend(WRITE_TOOLS);
    // No tool in two tiers, no tool untiered, no phantom tier entry.
    assert_eq!(tiered.len(), names.len());
    let tiered_set: std::collections::BTreeSet<String> =
        tiered.iter().map(|s| s.to_string()).collect();
    assert_eq!(tiered_set, names);
}

#[test]
fn phase1_diagram_and_noter_tools() {
    let responses = run_lines(&[
        initialize_line(1, LATEST_PROTOCOL_VERSION),
        call_line(2, "create_diagram", json!({ "source": "diagram flow\n", "title": "flow" })),
        call_line(3, "get_diagram_source", json!({ "tab_index": 3 })),
        call_line(4, "set_diagram_source", json!({ "tab_index": 3, "source": "diagram er\n" })),
        call_line(5, "get_diagram_source", json!({ "tab_index": 3 })),
        call_line(6, "get_diagram_source", json!({ "tab_index": 0 })), // not a diagram
        call_line(7, "create_diagram", json!({ "source": "!!bad\n" })), // invalid still creates
        call_line(8, "open_noter", json!({})),
    ]);
    let created = json_of(&responses[1]);
    assert_eq!(created["tab_index"], 3);
    assert_eq!(created["valid"], true);
    assert_eq!(json_of(&responses[2])["source"], "diagram flow\n");
    let set = json_of(&responses[3]);
    assert_eq!(set["ok"], true);
    assert_eq!(set["tab_index"], 3);
    assert_eq!(set["valid"], true);
    assert_eq!(json_of(&responses[4])["source"], "diagram er\n");
    assert_eq!(responses[5]["result"]["isError"], true);
    assert!(text_of(&responses[5]).contains("not a diagram"));
    let bad = json_of(&responses[6]);
    assert_eq!(bad["valid"], false);
    assert!(bad["errors"].as_array().is_some_and(|e| !e.is_empty()));
    assert_eq!(json_of(&responses[7])["opened"], true);
}

#[test]
fn phase1_set_diagram_source_is_approval_gated_but_create_is_not() {
    let mut editor = MockEditor::default();
    editor.set_approval(ApprovalMode::Deny);
    let responses = run_lines_with(
        editor,
        &[
            call_line(1, "create_diagram", json!({ "source": "diagram flow\n" })), // ACT: unaffected
            call_line(2, "set_diagram_source", json!({ "tab_index": 3, "source": "x" })),
        ],
    );
    assert_eq!(responses[0]["result"]["isError"], false);
    assert_eq!(responses[1]["result"]["isError"], true);
    assert_eq!(text_of(&responses[1]), "denied by user");
}

#[test]
fn phase1_malformed_arguments_are_invalid_params() {
    let responses = run_lines(&[
        call_line(1, "get_diagram_source", json!({})),                    // missing tab_index
        call_line(2, "set_diagram_source", json!({ "tab_index": 0 })),    // missing source
        call_line(3, "create_diagram", json!({ "source": 7 })),           // mistyped
        call_line(4, "open_noter", json!({ "bogus": true })),             // extra key
    ]);
    for r in &responses {
        assert_eq!(r["error"]["code"], -32602, "expected -32602 in {r}");
    }
}

// ---------------------------------------------------------------------------
// Phase 2 — data-analyst + charts (7 new tools, 41 → 48)
// ---------------------------------------------------------------------------

#[test]
fn phase2_data_tools_happy_paths() {
    let responses = run_lines(&[
        call_line(1, "list_connections", json!({})),
        call_line(2, "run_query", json!({ "connection_name": "demo", "sql": "SELECT id, name FROM t" })),
        call_line(3, "list_tables", json!({ "connection_name": "demo" })),
        call_line(4, "open_data_analyst", json!({})),
    ]);
    for r in &responses {
        assert_eq!(r["result"]["isError"], false, "unexpected error in {r}");
    }
    // list_connections: sanitized entry, NO password leak.
    let conns = json_of(&responses[0]);
    let list = conns["connections"].as_array().unwrap();
    assert_eq!(list[0]["name"], "demo");
    assert_eq!(list[0]["read_only"], true);
    assert!(list[0].get("password").is_none());
    // run_query over a saved connection: columns + rows + engine.
    let q = json_of(&responses[1]);
    assert_eq!(q["columns"], json!(["id", "name"]));
    assert_eq!(q["rows"].as_array().unwrap().len(), 2);
    assert!(q["engine"].as_str().is_some());
    // list_tables.
    let t = json_of(&responses[2]);
    assert!(t["tables"].as_array().unwrap().contains(&json!("invoices")));
    // open_data_analyst.
    assert_eq!(json_of(&responses[3])["opened"], true);
}

#[test]
fn phase2_run_query_rejects_mutations_and_unknown_connections() {
    let responses = run_lines(&[
        call_line(1, "run_query", json!({ "connection_name": "demo", "sql": "UPDATE t SET x = 1" })),
        call_line(2, "run_query", json!({ "connection_name": "nope", "sql": "SELECT 1" })),
    ]);
    assert_eq!(responses[0]["result"]["isError"], true);
    assert!(text_of(&responses[0]).contains("SELECT"));
    assert_eq!(responses[1]["result"]["isError"], true);
    assert!(text_of(&responses[1]).contains("no connection named"));
}

#[test]
fn phase2_render_chart_returns_id() {
    let responses = run_lines(&[call_line(
        1,
        "render_chart",
        json!({ "spec": { "mark": "bar" }, "title": "sales" }),
    )]);
    assert_eq!(responses[0]["result"]["isError"], false);
    let c = json_of(&responses[0]);
    assert!(c["chart_id"].as_str().is_some());
    assert_eq!(c["rendered"], true);
}

#[test]
fn phase2_write_tools_approval_gate() {
    // Approve (default): both write verbs succeed.
    let responses = run_lines(&[
        call_line(1, "export_query_results", json!({
            "connection_name": "demo", "sql": "SELECT 1", "path": "/tmp/out.csv", "format": "csv"
        })),
        call_line(2, "export_chart", json!({
            "spec": { "mark": "bar" }, "path": "/tmp/chart.png", "format": "png"
        })),
    ]);
    let eqr = json_of(&responses[0]);
    assert_eq!(eqr["ok"], true);
    assert_eq!(eqr["path"], "/tmp/out.csv");
    assert_eq!(json_of(&responses[1])["path"], "/tmp/chart.png");
    // Deny: both fail with the verbatim editor error.
    let mut editor = MockEditor::default();
    editor.set_approval(ApprovalMode::Deny);
    let denied = run_lines_with(editor, &[
        call_line(1, "export_query_results", json!({
            "connection_name": "demo", "sql": "SELECT 1", "path": "/tmp/out.csv", "format": "csv"
        })),
        call_line(2, "export_chart", json!({
            "spec": { "mark": "bar" }, "path": "/tmp/chart.png", "format": "png"
        })),
    ]);
    for r in &denied {
        assert_eq!(r["result"]["isError"], true);
        assert_eq!(text_of(r), "denied by user");
    }
}

#[test]
fn phase2_malformed_arguments_are_invalid_params() {
    let responses = run_lines(&[
        call_line(1, "run_query", json!({ "connection_name": "demo" })),      // missing sql
        call_line(2, "export_chart", json!({ "spec": { "mark": "bar" }, "path": "/tmp/x.gif", "format": "gif" })),
        call_line(3, "render_chart", json!({ "spec": "not-an-object" })),     // spec not object
        call_line(4, "export_chart", json!({ "spec": { "mark": "bar" }, "path": "/tmp/x.png", "format": "png", "scale": 9 })),
    ]);
    for r in &responses {
        assert_eq!(r["error"]["code"], -32602, "expected -32602 in {r}");
    }
}
