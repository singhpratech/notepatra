// SPDX-License-Identifier: GPL-3.0-or-later
//! v0.1.133 tools (tabs/files, editing, view, pure): schema, argument
//! validation, tiers, and mock round trips for every one of them.

use notepatra_mcp::server::Server;
use notepatra_mcp::tools::{definitions, ACT_TOOLS, READ_TOOLS, WRITE_TOOLS};
use notepatra_mcp::transport::mock::{ApprovalMode, MockEditor};
use serde_json::{json, Value};

const NEW_TOOLS: &[&str] = &[
    "activate_tab",
    "close_tab",
    "reload_tab",
    "rename_file",
    "save_all",
    "set_read_only",
    "undo",
    "redo",
    "transform_lines",
    "convert_case",
    "comment_lines",
    "line_edit",
    "set_eol",
    "set_encoding",
    "bookmarks",
    "fold",
    "set_view",
    "set_theme",
    "set_preview",
    "compare_with_file",
    "check_brackets",
    "fix_brackets",
    "hash_text",
    "encode_text",
    "diff_text",
    "repair_json",
    "markdown_convert",
    "mermaid_to_npd",
    "list_symbols",
    "generate_password",
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

/// The payload block (the mock prefixes a [MOCK DATA] notice block).
fn text(r: &Value) -> &str {
    let c = r["result"]["content"].as_array().expect("content");
    c.last().unwrap()["text"].as_str().unwrap()
}

fn json_of(r: &Value) -> Value {
    serde_json::from_str(text(r)).unwrap_or_else(|_| panic!("not JSON: {}", text(r)))
}

fn ok(r: &Value) -> Value {
    assert_eq!(r["result"]["isError"], false, "unexpected error: {r}");
    json_of(r)
}

fn invalid(r: &Value) {
    assert_eq!(r["error"]["code"], -32602, "expected -32602: {r}");
}

#[test]
fn every_new_tool_is_listed_tiered_and_well_formed() {
    let defs = definitions();
    let tools = defs.as_array().unwrap();
    for name in NEW_TOOLS {
        let t = tools
            .iter()
            .find(|t| t["name"] == *name)
            .unwrap_or_else(|| panic!("{name} missing"));
        let schema = &t["inputSchema"];
        assert_eq!(schema["type"], "object");
        assert_eq!(schema["additionalProperties"], false, "{name}");
        // Every required key is a declared property.
        for r in schema["required"].as_array().unwrap() {
            assert!(
                schema["properties"].get(r.as_str().unwrap()).is_some(),
                "{name}: {r}"
            );
        }
        let desc = t["description"].as_str().unwrap();
        assert!(desc.len() > 60, "{name}: description too thin");
        let tiers = [READ_TOOLS, ACT_TOOLS, WRITE_TOOLS]
            .iter()
            .filter(|l| l.contains(name))
            .count();
        assert_eq!(tiers, 1, "{name} must be in exactly one tier");
        // The approval sentence is on the write tools and only there.
        assert_eq!(
            desc.contains("click Approve"),
            WRITE_TOOLS.contains(name),
            "{name}: approval wording vs tier"
        );
    }
    assert_eq!(tools.len(), 79);
}

#[test]
fn tiers_are_deliberate() {
    for t in [
        "check_brackets",
        "hash_text",
        "diff_text",
        "generate_password",
        "list_symbols",
    ] {
        assert!(READ_TOOLS.contains(&t), "{t}");
    }
    for t in [
        "activate_tab",
        "set_view",
        "set_theme",
        "bookmarks",
        "fold",
        "compare_with_file",
    ] {
        assert!(ACT_TOOLS.contains(&t), "{t}");
    }
    // Anything that can change or discard document content is a write.
    for t in [
        "close_tab",
        "reload_tab",
        "undo",
        "transform_lines",
        "set_encoding",
        "save_all",
    ] {
        assert!(WRITE_TOOLS.contains(&t), "{t}");
    }
}

#[test]
fn generate_password_states_where_the_secret_goes() {
    let defs = definitions();
    let t = defs
        .as_array()
        .unwrap()
        .iter()
        .find(|t| t["name"] == "generate_password")
        .unwrap();
    assert!(t["description"]
        .as_str()
        .unwrap()
        .contains("model provider"));
}

#[test]
fn mock_round_trips_for_every_new_tool() {
    let mut ed = MockEditor::default();
    ed.add_diagram_tab("flow.npd", "diagram flow\n");
    let r = run(
        ed,
        &[
            ("activate_tab", json!({ "tab_id": 2 })),
            ("set_read_only", json!({ "tab_index": 0, "on": true })),
            (
                "bookmarks",
                json!({ "action": "toggle", "line": 2, "tab_id": 1 }),
            ),
            ("bookmarks", json!({ "action": "list", "tab_id": 1 })),
            ("fold", json!({ "action": "fold_all", "tab_id": 1 })),
            ("set_view", json!({ "word_wrap": true, "zoom": 14 })),
            ("set_theme", json!({ "theme": "Dark" })),
            ("set_preview", json!({ "on": true, "tab_id": 2 })),
            (
                "compare_with_file",
                json!({ "tab_id": 1, "path": "/home/user/project/old.rs" }),
            ),
            ("transform_lines", json!({ "tab_id": 2, "op": "sort_asc" })),
            ("undo", json!({ "tab_id": 2 })),
            ("redo", json!({ "tab_id": 2 })),
            (
                "convert_case",
                json!({ "tab_id": 2, "mode": "upper", "scope": "document" }),
            ),
            (
                "comment_lines",
                json!({ "tab_id": 1, "start_line": 1, "end_line": 2 }),
            ),
            (
                "line_edit",
                json!({ "tab_id": 1, "line": 1, "op": "duplicate" }),
            ),
            ("set_eol", json!({ "tab_id": 1, "eol": "crlf" })),
            (
                "set_encoding",
                json!({ "tab_id": 1, "encoding": "UTF-16 LE" }),
            ),
            ("check_brackets", json!({ "text": "fn x( {" })),
            ("fix_brackets", json!({ "text": "fn x( {" })),
            ("hash_text", json!({ "text": "abc", "algo": "sha1" })),
            (
                "encode_text",
                json!({ "text": "hi there", "op": "base64_encode" }),
            ),
            (
                "diff_text",
                json!({ "left": { "text": "a\nb\n" }, "right": { "tab_id": 1 } }),
            ),
            ("repair_json", json!({ "text": "[1,2" })),
            (
                "markdown_convert",
                json!({ "text": "a,b\n1,2", "op": "table" }),
            ),
            ("mermaid_to_npd", json!({ "text": "graph TD\nA-->B\n" })),
            (
                "list_symbols",
                json!({ "text": "struct App;\nfn main() {}\n", "language": "Rust" }),
            ),
            ("generate_password", json!({ "length": 12, "count": 3 })),
            ("rename_file", json!({ "tab_id": 1, "new_name": "app.rs" })),
            ("reload_tab", json!({ "tab_id": 1 })),
            ("save_all", json!({})),
            ("close_tab", json!({ "tab_id": 3 })),
        ],
    );
    for (k, resp) in r.iter().enumerate() {
        assert_eq!(resp["result"]["isError"], false, "call #{k}: {resp}");
    }
    assert_eq!(json_of(&r[0])["tab_id"], 2);
    assert_eq!(json_of(&r[1])["read_only"], true);
    assert_eq!(json_of(&r[3])["lines"], json!([2]));
    assert_eq!(json_of(&r[5])["zoom"], 14);
    assert_eq!(json_of(&r[9])["changed"], true);
    assert_eq!(json_of(&r[10])["steps"], 1);
    assert_eq!(json_of(&r[12])["changed"], true);
    assert_eq!(json_of(&r[16])["encoding"], "UTF-16 LE");
    assert!(json_of(&r[17])["report"]
        .as_str()
        .unwrap()
        .contains("Unclosed"));
    assert_eq!(json_of(&r[18])["changed"], true);
    assert_eq!(json_of(&r[19])["hash"].as_str().unwrap().len(), 40);
    assert_eq!(json_of(&r[20])["text"], "aGkgdGhlcmU=");
    assert_eq!(json_of(&r[21])["identical"], false);
    assert!(json_of(&r[23])["text"]
        .as_str()
        .unwrap()
        .starts_with("| a | b |"));
    assert!(json_of(&r[24])["source"]
        .as_str()
        .unwrap()
        .starts_with("diagram"));
    assert!(json_of(&r[25])["symbols"]
        .as_array()
        .unwrap()
        .iter()
        .any(|s| s["name"] == "main"));
    assert_eq!(json_of(&r[26])["passwords"].as_array().unwrap().len(), 3);
    assert!(json_of(&r[27])["path"]
        .as_str()
        .unwrap()
        .ends_with("/app.rs"));
    assert_eq!(json_of(&r[30])["closed"], true);
}

#[test]
fn write_tools_honour_deny_and_timeout() {
    for (mode, expected) in [
        (ApprovalMode::Deny, "denied by user"),
        (ApprovalMode::Timeout, "approval timed out"),
    ] {
        let mut ed = MockEditor::default();
        ed.set_approval(mode);
        let r = run(
            ed,
            &[
                ("transform_lines", json!({ "tab_id": 2, "op": "reverse" })),
                (
                    "convert_case",
                    json!({ "tab_id": 2, "mode": "lower", "scope": "document" }),
                ),
                (
                    "comment_lines",
                    json!({ "tab_id": 1, "start_line": 1, "end_line": 1 }),
                ),
                (
                    "line_edit",
                    json!({ "tab_id": 1, "line": 1, "op": "delete" }),
                ),
                ("set_eol", json!({ "tab_id": 1, "eol": "lf" })),
                ("set_encoding", json!({ "tab_id": 1, "encoding": "UTF-8" })),
                ("rename_file", json!({ "tab_id": 1, "new_name": "x.rs" })),
                ("save_all", json!({})),
                ("close_tab", json!({ "tab_id": 2, "discard_unsaved": true })),
                ("reload_tab", json!({ "tab_id": 2 })),
                (
                    "apply_edit",
                    json!({ "tab_id": 1, "find": "MAIN", "replace": "x", "match_case": false }),
                ),
            ],
        );
        for resp in &r {
            assert_eq!(resp["result"]["isError"], true, "{resp}");
            assert_eq!(text(resp), expected);
        }
    }
    // Nothing those denied calls named was changed: the same reads still see
    // the originals (fresh editor, same fixture).
}

#[test]
fn unmodified_close_and_reload_need_no_card() {
    let mut ed = MockEditor::default();
    ed.set_approval(ApprovalMode::Deny);
    let r = run(
        ed,
        &[
            ("reload_tab", json!({ "tab_id": 1 })),
            ("close_tab", json!({ "tab_id": 1 })),
            ("close_tab", json!({ "tab_id": 2 })), // modified, no discard
        ],
    );
    assert_eq!(ok(&r[0])["reloaded"], true);
    assert_eq!(ok(&r[1])["closed"], true);
    assert_eq!(r[2]["result"]["isError"], true);
    assert!(text(&r[2]).contains("unsaved changes"));
}

#[test]
fn malformed_arguments_are_invalid_params() {
    let r = run(
        MockEditor::default(),
        &[
            ("activate_tab", json!({})),
            ("activate_tab", json!({ "tab_id": 1, "tab_index": 0 })),
            ("transform_lines", json!({ "op": "shuffle" })),
            (
                "transform_lines",
                json!({ "op": "sort_asc", "scope": "page" }),
            ),
            ("convert_case", json!({})),
            ("comment_lines", json!({ "start_line": 5, "end_line": 2 })),
            ("comment_lines", json!({ "start_line": 0, "end_line": 2 })),
            ("line_edit", json!({ "line": 1, "op": "copy" })),
            ("set_eol", json!({ "eol": "LF" })),
            ("set_encoding", json!({ "encoding": "EBCDIC" })),
            ("undo", json!({ "steps": 0 })),
            ("redo", json!({ "steps": 101 })),
            ("rename_file", json!({ "new_name": "../x" })),
            ("rename_file", json!({ "new_name": "a/b" })),
            ("save_all", json!({ "tab_id": 1 })),
            ("bookmarks", json!({ "action": "toggle" })),
            ("fold", json!({ "action": "collapse" })),
            ("set_view", json!({})),
            ("set_view", json!({ "zoom": 99 })),
            ("set_theme", json!({ "theme": "Solarized" })),
            ("set_preview", json!({})),
            ("compare_with_file", json!({})),
            ("check_brackets", json!({})),
            ("check_brackets", json!({ "text": "x", "tab_id": 1 })),
            ("hash_text", json!({ "text": "x", "algo": "crc32" })),
            ("encode_text", json!({ "text": "x" })),
            ("diff_text", json!({ "left": { "text": "a" } })),
            ("diff_text", json!({ "left": {}, "right": { "text": "b" } })),
            ("markdown_convert", json!({ "text": "x", "op": "pdf" })),
            ("generate_password", json!({ "length": 3 })),
            ("generate_password", json!({ "classes": [] })),
            ("generate_password", json!({ "classes": ["emoji"] })),
            ("generate_password", json!({ "separator": "--" })),
            ("set_read_only", json!({ "on": "yes" })),
            ("close_tab", json!({ "tab_id": 1, "discard_unsaved": 1 })),
        ],
    );
    for resp in &r {
        invalid(resp);
    }
}

#[test]
fn stale_tab_id_is_an_error_never_the_active_tab() {
    let r = run(
        MockEditor::default(),
        &[
            (
                "transform_lines",
                json!({ "tab_id": 999, "op": "sort_asc" }),
            ),
            ("activate_tab", json!({ "tab_id": 999 })),
            ("hash_text", json!({ "tab_id": 999 })),
            ("bookmarks", json!({ "tab_id": 999, "action": "list" })),
        ],
    );
    for resp in &r {
        assert_eq!(resp["result"]["isError"], true, "{resp}");
        assert!(text(resp).contains("was closed"), "{resp}");
    }
}

#[test]
fn extended_existing_tools() {
    let r = run(
        MockEditor::default(),
        &[
            (
                "new_tab",
                json!({ "text": "x", "language": "Python", "title": "scratch.py" }),
            ),
            ("create_diagram", json!({ "template": "er" })),
            ("create_diagram", json!({ "template": "er", "source": "x" })),
            (
                "find_in_tab",
                json!({ "tab_index": 0, "query": "MAIN", "match_case": true }),
            ),
            (
                "find_in_tab",
                json!({ "tab_index": 0, "query": "main", "whole_word": true }),
            ),
            (
                "search_project",
                json!({ "query": "Release", "match_case": true }),
            ),
            (
                "format_json",
                json!({ "text": "{\"a\": 1}", "minify": true }),
            ),
            (
                "format_sql",
                json!({ "text": "select 1", "keyword_case": "lower" }),
            ),
            ("format_html", json!({ "text": "<p>x</p>", "minify": true })),
            (
                "apply_edit",
                json!({ "find": "MAIN", "replace": "$1", "match_case": false, "tab_index": 0 }),
            ),
            ("read_tab", json!({ "tab_index": 0 })),
            (
                "apply_edit",
                json!({ "find": "release", "replace": "R", "match_case": false, "scope": "all_open", "all": true }),
            ),
            (
                "apply_edit",
                json!({ "find": "x", "replace": "y", "scope": "all_open", "tab_id": 1 }),
            ),
        ],
    );
    let nt = ok(&r[0]);
    assert_eq!(nt["language"], "Python");
    assert_eq!(nt["title"], "scratch.py");
    assert!(ok(&r[1])["tab_id"].as_i64().is_some());
    invalid(&r[2]);
    assert!(ok(&r[3])["matches"].as_array().unwrap().is_empty());
    let ww = ok(&r[4]);
    assert_eq!(ww["matches"][0]["column"], 4);
    assert!(ok(&r[5])["results"]
        .as_array()
        .unwrap()
        .iter()
        .all(|h| h["column"].is_u64()));
    assert_eq!(text(&r[6]), "{\"a\":1}");
    assert_eq!(text(&r[7]).trim_end(), "select 1");
    invalid(&r[8]); // html has no minify option
    assert_eq!(ok(&r[9])["count"], 1);
    // A literal replacement is inserted as-is: "$1" stays "$1".
    assert!(text(&r[10]).contains("fn $1()"), "{}", text(&r[10]));
    assert!(!ok(&r[11])["tabs"].as_array().unwrap().is_empty());
    invalid(&r[12]);
}
