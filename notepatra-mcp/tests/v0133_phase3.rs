// SPDX-License-Identifier: GPL-3.0-or-later
//! v0.1.133 Phase 3 tools (git writes, Noter management, settings, data,
//! REST, leftovers): schema, tiers, validation and mock round trips,
//! including Deny and timeout for every write.

use notepatra_mcp::server::Server;
use notepatra_mcp::tools::{definitions, ACT_TOOLS, EXTERNAL_WRITE_TOOLS, READ_TOOLS, WRITE_TOOLS};
use notepatra_mcp::transport::mock::{ApprovalMode, MockEditor};
use serde_json::{json, Value};

const READS: &[&str] = &[
    "list_hunks",
    "list_conflicts",
    "search_notes",
    "get_preferences",
    "describe_table",
    "test_connection",
    "parse_http_file",
    "curl_to_request",
    "request_to_curl",
    "hex_dump",
];
const ACTS: &[&str] = &["open_connections_dialog", "open_schema_browser"];
const WRITES: &[&str] = &[
    "git_stage",
    "git_unstage",
    "git_commit",
    "git_branch_create",
    "git_checkout",
    "git_stash",
    "git_discard",
    "git_fetch",
    "git_pull",
    "git_push",
    "git_hunk",
    "resolve_conflict",
    "rename_note",
    "trash_note",
    "restore_note",
    "delete_note_permanently",
    "delete_reminder",
    "snooze_reminder",
    "dismiss_reminder",
    "export_note",
    "set_preference",
    "set_workspace_folder",
    "execute_sql",
    "add_connection",
    "update_connection",
    "delete_connection",
    "rest_request",
    "clear_recent_files",
];

fn run(editor: MockEditor, calls: &[(&str, Value)]) -> Vec<Value> {
    let lines: Vec<String> = calls
        .iter()
        .enumerate()
        .map(|(i, (name, args))| {
            json!({ "jsonrpc": "2.0", "id": i + 1, "method": "tools/call",
                    "params": { "name": name, "arguments": args } })
            .to_string()
        })
        .collect();
    let input = lines.join("\n") + "\n";
    let mut out = Vec::new();
    Server::new(editor).run(input.as_bytes(), &mut out).unwrap();
    String::from_utf8(out)
        .unwrap()
        .lines()
        .map(|l| serde_json::from_str(l).unwrap())
        .collect()
}

fn text(r: &Value) -> &str {
    let c = r["result"]["content"].as_array().expect("content");
    c.last().unwrap()["text"].as_str().unwrap()
}

fn ok(r: &Value) -> Value {
    assert_eq!(r["result"]["isError"], false, "unexpected error: {r}");
    serde_json::from_str(text(r)).unwrap_or_else(|_| panic!("not JSON: {}", text(r)))
}

fn tool_err(r: &Value) -> String {
    assert_eq!(r["result"]["isError"], true, "expected a tool error: {r}");
    text(r).to_string()
}

fn invalid(r: &Value) {
    assert_eq!(r["error"]["code"], -32602, "expected -32602: {r}");
}

fn with_mode(mode: ApprovalMode) -> MockEditor {
    let mut e = MockEditor::default();
    e.set_approval(mode);
    e
}

/// One valid call per write tool, against the default mock state.
fn valid_write_calls() -> Vec<(&'static str, Value)> {
    vec![
        ("git_stage", json!({ "paths": ["src/lib.rs"] })),
        ("git_unstage", json!({ "paths": ["src/tools.rs"] })),
        ("git_commit", json!({ "message": "fix: lexer" })),
        ("git_branch_create", json!({ "name": "topic" })),
        ("git_checkout", json!({ "name": "feature/lexer" })),
        ("git_stash", json!({ "action": "push", "message": "wip" })),
        ("git_discard", json!({ "paths": ["src/lib.rs"] })),
        ("git_fetch", json!({})),
        ("git_pull", json!({})),
        ("git_push", json!({})),
        (
            "git_hunk",
            json!({ "path": "src/lib.rs", "hunk_index": 0, "action": "stage" }),
        ),
        (
            "resolve_conflict",
            json!({ "path": "src/merge.rs", "region": 0, "take": "theirs" }),
        ),
        (
            "rename_note",
            json!({ "file": "/home/user/Documents/Notepatra/Noter/old-scratch.html", "new_title": "Ideas" }),
        ),
        (
            "trash_note",
            json!({ "file": "/home/user/Documents/Notepatra/Noter/old-scratch.html" }),
        ),
        ("delete_reminder", json!({ "id": "r1" })),
        ("snooze_reminder", json!({ "id": "r2", "minutes": 15 })),
        ("dismiss_reminder", json!({ "id": "r3" })),
        (
            "export_note",
            json!({ "file": "/home/user/Documents/Notepatra/Noter/release-checklist.html", "format": "markdown", "path": "/tmp/out.md" }),
        ),
        ("set_preference", json!({ "key": "tab_width", "value": 2 })),
        (
            "set_workspace_folder",
            json!({ "path": "/home/user/project" }),
        ),
        (
            "execute_sql",
            json!({ "connection_name": "demo", "sql": "INSERT INTO t VALUES (1)" }),
        ),
        (
            "add_connection",
            json!({ "name": "pg", "driver": "QPSQL", "host": "db.local", "database": "app", "username": "me" }),
        ),
        (
            "update_connection",
            json!({ "name": "demo", "database": "/tmp/x.db" }),
        ),
        ("delete_connection", json!({ "name": "demo" })),
        (
            "rest_request",
            json!({ "method": "GET", "url": "http://127.0.0.1:8080/health" }),
        ),
        ("clear_recent_files", json!({})),
    ]
}

#[test]
fn every_phase3_tool_is_listed_tiered_and_well_formed() {
    let defs = definitions();
    let tools = defs.as_array().unwrap();
    assert_eq!(tools.len(), 119);
    for name in READS.iter().chain(ACTS).chain(WRITES) {
        let t = tools
            .iter()
            .find(|t| t["name"] == *name)
            .unwrap_or_else(|| panic!("{name} missing"));
        let schema = &t["inputSchema"];
        assert_eq!(schema["type"], "object", "{name}");
        assert_eq!(schema["additionalProperties"], false, "{name}");
        for r in schema["required"].as_array().unwrap() {
            assert!(
                schema["properties"].get(r.as_str().unwrap()).is_some(),
                "{name}: {r}"
            );
        }
        let desc = t["description"].as_str().unwrap();
        assert!(desc.len() > 60, "{name}: description too thin");
        assert_eq!(
            desc.contains("click Approve"),
            WRITE_TOOLS.contains(name),
            "{name}"
        );
    }
    for n in READS {
        assert!(READ_TOOLS.contains(n), "{n} must be READ");
    }
    for n in ACTS {
        assert!(ACT_TOOLS.contains(n), "{n} must be ACT");
    }
    for n in WRITES {
        assert!(WRITE_TOOLS.contains(n), "{n} must be WRITE");
    }
    assert_eq!(READS.len() + ACTS.len() + WRITES.len(), 40);
}

#[test]
fn writes_that_leave_the_editor_are_marked_external() {
    for t in [
        "execute_sql",
        "rest_request",
        "git_fetch",
        "git_pull",
        "git_push",
    ] {
        assert!(EXTERNAL_WRITE_TOOLS.contains(&t), "{t}");
    }
    assert_eq!(EXTERNAL_WRITE_TOOLS.len(), 5);
}

#[test]
fn descriptions_name_the_safety_rules() {
    let defs = definitions();
    let d = |n: &str| {
        defs.as_array()
            .unwrap()
            .iter()
            .find(|t| t["name"] == n)
            .unwrap()["description"]
            .as_str()
            .unwrap()
            .to_string()
    };
    assert!(d("git_commit").contains("never commits unstaged"));
    assert!(d("git_discard").contains("never deleted"));
    assert!(d("git_push").contains("never forced"));
    assert!(d("delete_note_permanently").contains("CANNOT be undone"));
    assert!(d("add_connection").contains("WITHOUT any password"));
    assert!(d("update_connection").contains("CLEARS the stored password"));
    assert!(d("execute_sql").contains("refused even with approval"));
    assert!(d("set_preference").contains("AI or cloud keys"));
    assert!(d("set_workspace_folder").contains("home folder"));
    assert!(d("rest_request").contains("http and https only"));
}

#[test]
fn every_write_round_trips_and_is_denied_and_times_out() {
    for (name, args) in valid_write_calls() {
        let r = run(MockEditor::default(), &[(name, args.clone())]);
        ok(&r[0]);
        let r = run(with_mode(ApprovalMode::Deny), &[(name, args.clone())]);
        assert_eq!(tool_err(&r[0]), "denied by user", "{name}");
        let r = run(with_mode(ApprovalMode::Timeout), &[(name, args)]);
        assert_eq!(tool_err(&r[0]), "approval timed out", "{name}");
    }
}

#[test]
fn git_flow_in_the_mock() {
    let r = run(
        MockEditor::default(),
        &[
            (
                "git_stage",
                json!({ "paths": ["src/lib.rs", "./notes/scratch.md"] }),
            ),
            ("git_commit", json!({ "message": "two files" })),
            ("git_commit", json!({ "message": "again" })),
            ("git_stage", json!({ "paths": [".env"] })),
            ("git_stage", json!({ "paths": ["-p"] })),
            ("git_stage", json!({ "paths": ["../outside.txt"] })),
            ("git_discard", json!({ "paths": ["notes/scratch.md"] })),
            ("list_conflicts", json!({ "path": "src/merge.rs" })),
            (
                "resolve_conflict",
                json!({ "path": "src/merge.rs", "region": 0, "take": "ours" }),
            ),
            ("list_conflicts", json!({ "path": "src/merge.rs" })),
            ("list_hunks", json!({ "path": "src/transport/socket.rs" })),
            ("git_checkout", json!({ "name": "nope" })),
        ],
    );
    assert_eq!(
        ok(&r[0])["staged"],
        json!(["src/lib.rs", "notes/scratch.md"])
    );
    let c = ok(&r[1]);
    assert_eq!(c["files"].as_array().unwrap().len(), 3);
    assert!(tool_err(&r[2]).contains("nothing is staged"));
    assert!(tool_err(&r[3]).contains("credential deny-list"));
    assert!(tool_err(&r[4]).contains("may not start with '-'"));
    assert!(tool_err(&r[5]).contains("outside"));
    assert!(tool_err(&r[6]).contains("no uncommitted"));
    assert_eq!(ok(&r[7])["regions"].as_array().unwrap().len(), 1);
    assert_eq!(ok(&r[8])["remaining"], 0);
    assert!(ok(&r[9])["regions"].as_array().unwrap().is_empty());
    assert_eq!(ok(&r[10])["hunks"][0]["index"], 0);
    assert!(tool_err(&r[11]).contains("no local branch"));
}

#[test]
fn git_arguments_are_validated_before_the_editor() {
    let r = run(
        MockEditor::default(),
        &[
            ("git_stage", json!({ "paths": [] })),
            ("git_stage", json!({ "paths": "src/lib.rs" })),
            ("git_stage", json!({ "paths": [""] })),
            ("git_commit", json!({ "message": "   " })),
            ("git_push", json!({ "force": true })),
            ("git_stash", json!({ "action": "drop" })),
            ("git_stash", json!({ "action": "pop", "message": "x" })),
            (
                "git_hunk",
                json!({ "path": "a", "hunk_index": -1, "action": "stage" }),
            ),
            (
                "resolve_conflict",
                json!({ "path": "a", "region": 0, "take": "mine" }),
            ),
            ("git_diff", json!({ "staged": "yes" })),
        ],
    );
    for (i, resp) in r.iter().enumerate() {
        assert_eq!(resp["error"]["code"], -32602, "call #{i}: {resp}");
    }
    let r = run(
        MockEditor::default(),
        &[("git_diff", json!({ "staged": true }))],
    );
    assert_eq!(ok(&r[0])["staged"], true);
}

#[test]
fn noter_flow_in_the_mock() {
    let file = "/home/user/Documents/Notepatra/Noter/old-scratch.html";
    let r = run(
        MockEditor::default(),
        &[
            ("search_notes", json!({ "query": "lexer fix" })),
            ("trash_note", json!({ "file": file })),
            ("list_notes", json!({ "include_trash": true })),
            (
                "delete_note_permanently",
                json!({ "file": "/home/user/Documents/Notepatra/Noter/release-checklist.html" }),
            ),
            ("list_reminders", json!({})),
            ("snooze_reminder", json!({ "id": "r9" })),
            (
                "export_note",
                json!({ "file": "/home/user/Documents/Notepatra/Noter/release-checklist.html", "format": "pdf", "path": "/tmp/x.pdf" }),
            ),
        ],
    );
    let hits = ok(&r[0])["results"].as_array().unwrap().clone();
    assert_eq!(hits.len(), 1);
    assert_eq!(hits[0]["title"], "Standup 2026-07-15");
    let trashed = ok(&r[1])["file"].as_str().unwrap().to_string();
    assert!(trashed.contains("/Trash/.trashed-"));
    let notes = ok(&r[2])["notes"].as_array().unwrap().clone();
    assert!(notes
        .iter()
        .any(|n| n["file"] == trashed.as_str() && n["trashed"] == true));
    assert!(tool_err(&r[3]).contains("not in the Noter Trash"));
    assert!(ok(&r[4])["reminders"][0]["id"].is_string());
    assert!(tool_err(&r[5]).contains("no reminder with id"));
    assert!(tool_err(&r[6]).contains("not available in this build"));
    // Restore, then delete permanently only from Trash.
    let r = run(
        MockEditor::default(),
        &[
            ("trash_note", json!({ "file": file })),
            (
                "restore_note",
                json!({ "file": "/home/user/Documents/Notepatra/Noter/Trash/.trashed-1700000000000-old-scratch.html" }),
            ),
        ],
    );
    assert!(ok(&r[1])["file"]
        .as_str()
        .unwrap()
        .ends_with("/Inbox/old-scratch.html"));
    let r = run(
        MockEditor::default(),
        &[
            ("snooze_reminder", json!({ "id": "r1", "minutes": 0 })),
            (
                "export_note",
                json!({ "file": file, "format": "markdown", "path": "relative.md" }),
            ),
            ("rename_note", json!({ "file": file })),
        ],
    );
    for resp in &r {
        invalid(resp);
    }
}

#[test]
fn settings_allow_list_and_workspace_refusals() {
    let r = run(
        MockEditor::default(),
        &[
            ("get_preferences", json!({})),
            (
                "set_preference",
                json!({ "key": "aiApiKey", "value": "sk" }),
            ),
            ("set_preference", json!({ "key": "tab_width", "value": 99 })),
            ("set_preference", json!({ "key": "font_size", "value": 14 })),
            ("set_workspace_folder", json!({ "path": "/home/user" })),
            ("set_workspace_folder", json!({ "path": "/" })),
            ("set_workspace_folder", json!({ "path": "/home/user/.ssh" })),
            (
                "set_preference",
                json!({ "key": "tab_width", "value": [1] }),
            ),
        ],
    );
    let prefs = ok(&r[0]);
    assert!(prefs["preferences"].get("tab_width").is_some());
    assert!(prefs["preferences"].get("aiApiKey").is_none());
    let e = tool_err(&r[1]);
    assert!(e.contains("unknown or protected") && !e.contains("aiBaseUrl"));
    assert!(tool_err(&r[2]).contains("tab_width"));
    assert_eq!(ok(&r[3])["changed"], true);
    assert!(tool_err(&r[4]).contains("home folder"));
    assert!(tool_err(&r[5]).contains("root"));
    assert!(tool_err(&r[6]).contains("credentials"));
    invalid(&r[7]);
}

#[test]
fn data_tools_never_take_or_return_secrets() {
    let r = run(
        MockEditor::default(),
        &[
            (
                "add_connection",
                json!({ "name": "pg", "driver": "QPSQL", "database": "app", "password": "hunter2" }),
            ),
            (
                "add_connection",
                json!({ "name": "pg", "driver": "QPSQL", "database": "app", "options": "s3_secret=x" }),
            ),
            (
                "add_connection",
                json!({ "name": "pg", "driver": "ORACLE", "database": "app" }),
            ),
            ("update_connection", json!({ "name": "demo" })),
            (
                "add_connection",
                json!({ "name": "pg", "driver": "QPSQL", "database": "app" }),
            ),
            (
                "describe_table",
                json!({ "connection_name": "demo", "table": "invoices" }),
            ),
            ("test_connection", json!({ "connection_name": "demo" })),
            (
                "execute_sql",
                json!({ "connection_name": "demo", "sql": "DELETE FROM a; DROP TABLE b" }),
            ),
            (
                "execute_sql",
                json!({ "connection_name": "demo", "sql": "SELECT 1" }),
            ),
            (
                "execute_sql",
                json!({ "connection_name": "demo", "sql": "EXEC xp_cmdshell 'dir'" }),
            ),
        ],
    );
    invalid(&r[0]);
    invalid(&r[1]);
    invalid(&r[2]);
    invalid(&r[3]);
    let added = ok(&r[4]);
    assert!(added.get("password").is_none() && added.get("options").is_none());
    assert_eq!(added["has_password"], false);
    assert!(added["note"]
        .as_str()
        .unwrap()
        .contains("Manage Connections"));
    assert_eq!(ok(&r[5])["primary_key"], json!(["id"]));
    assert_eq!(ok(&r[6])["ok"], true);
    assert!(tool_err(&r[7]).contains("exactly one statement"));
    assert!(tool_err(&r[8]).contains("run_query"));
    assert!(tool_err(&r[9]).contains("cannot override"));
}

#[test]
fn rest_tools_validate_and_round_trip() {
    let r = run(
        MockEditor::default(),
        &[
            (
                "rest_request",
                json!({ "method": "GET", "url": "file:///etc/passwd" }),
            ),
            (
                "rest_request",
                json!({ "method": "GET", "url": "http://x", "tab_id": 1, "line": 1 }),
            ),
            ("rest_request", json!({ "tab_id": 1 })),
            (
                "rest_request",
                json!({ "method": "GET", "url": "http://x", "headers": { "Bad Name": "v" } }),
            ),
            (
                "rest_request",
                json!({ "method": "GET", "url": "http://x", "headers": { "X": "a\r\nInjected: 1" } }),
            ),
            (
                "rest_request",
                json!({ "method": "TRACE", "url": "http://x" }),
            ),
            (
                "parse_http_file",
                json!({ "text": "GET http://{{host}}/a\n\n###\nPOST http://x/b\n" }),
            ),
            (
                "curl_to_request",
                json!({ "text": "curl -X POST https://api.example.com/v1" }),
            ),
            (
                "request_to_curl",
                json!({ "method": "GET", "url": "https://example.com" }),
            ),
            ("hex_dump", json!({ "tab_id": 1, "length": 16 })),
            ("hex_dump", json!({ "path": "/home/user/project/.env" })),
            ("hex_dump", json!({ "tab_id": 1, "path": "/x" })),
            ("hex_dump", json!({ "tab_id": 1, "length": 70000 })),
        ],
    );
    for resp in &r[..6] {
        invalid(resp);
    }
    let parsed = ok(&r[6]);
    assert_eq!(parsed["requests"][0]["url"], "http://{{host}}/a");
    assert_eq!(parsed["requests"][1]["line"], 4);
    assert_eq!(ok(&r[7])["method"], "POST");
    assert!(ok(&r[8])["text"].as_str().unwrap().starts_with("curl"));
    let hex = ok(&r[9]);
    assert_eq!(hex["length"], 16);
    assert!(hex["hex"].as_str().unwrap().starts_with("00000000  66 6e"));
    assert!(tool_err(&r[10]).contains("deny-list"));
    invalid(&r[11]);
    invalid(&r[12]);
}
