// SPDX-License-Identifier: GPL-3.0-or-later
use serde_json::{json, Map, Value};

use crate::transport::{EditorTransport, TabSelector};

const DEFAULT_MAX_RESULTS: usize = 50;

pub enum CallOutcome {
    /// Tool succeeded; string becomes the text content block.
    Ok(String),
    /// Tool-level failure; per spec this is an `isError: true` result.
    ToolError(String),
    /// Maps to JSON-RPC -32602.
    InvalidParams(String),
    /// Maps to JSON-RPC -32602 ("Unknown tool: X" per spec).
    UnknownTool(String),
}

fn no_args_schema() -> Value {
    json!({
        "type": "object",
        "properties": {},
        "required": [],
        "additionalProperties": false
    })
}

fn format_tool(name: &str, kind_label: &str, extra: &str) -> Value {
    json!({
        "name": name,
        "description": format!(
            "Format {kind_label} text and return the formatted result.{extra} \
             Use before inserting {kind_label} into the editor, or to clean up \
             {kind_label} the user pasted."
        ),
        "inputSchema": {
            "type": "object",
            "properties": {
                "text": { "type": "string", "description": format!("The raw {kind_label} text to format") }
            },
            "required": ["text"],
            "additionalProperties": false
        }
    })
}

pub fn definitions() -> Value {
    json!([
        {
            "name": "open_file",
            "description": "Open a file in the Notepatra editor (new tab, or focus the tab that already has it). Use when the user asks to open or show a file.",
            "inputSchema": {
                "type": "object",
                "properties": {
                    "path": { "type": "string", "description": "Absolute path of the file to open" }
                },
                "required": ["path"],
                "additionalProperties": false
            }
        },
        {
            "name": "list_open_tabs",
            "description": "List the tabs currently open in the editor with index, title, file path, and modified state. Use first to discover what the user has open before reading or comparing tabs.",
            "inputSchema": no_args_schema()
        },
        {
            "name": "read_tab",
            "description": "Read the full text content of one open tab, selected by index or by title (provide exactly one). Use when you need a document's contents; use list_open_tabs first if you don't know the index.",
            "inputSchema": {
                "type": "object",
                "properties": {
                    "tab_index": { "type": "integer", "minimum": 0, "description": "Zero-based tab index" },
                    "title": { "type": "string", "description": "Exact tab title" }
                },
                "required": [],
                "additionalProperties": false
            }
        },
        {
            "name": "search_project",
            "description": "Search ALL open tabs plus files under the workspace folder for a literal substring (case-insensitive); returns matching lines with file path and line number. Use to locate text when you don't know which tab or file it is in; use find_in_tab for a single tab.",
            "inputSchema": {
                "type": "object",
                "properties": {
                    "query": { "type": "string", "description": "Literal text to search for" },
                    "max_results": { "type": "integer", "minimum": 1, "description": "Maximum matches to return (default 50)" }
                },
                "required": ["query"],
                "additionalProperties": false
            }
        },
        {
            "name": "get_selection",
            "description": "Get the currently selected text in the editor, with the tab it belongs to. Use when the user refers to \"this\" or \"the selected\" text.",
            "inputSchema": no_args_schema()
        },
        {
            "name": "get_status",
            "description": "Get the editor's current state: active tab, file path, language, encoding, and cursor position. Use first when you need context about what the user is working on right now.",
            "inputSchema": no_args_schema()
        },
        {
            "name": "app_info",
            "description": "Get Notepatra application info: name, version, edition (Lite/Full), and platform. Use when behavior depends on the editor version or edition.",
            "inputSchema": no_args_schema()
        },
        {
            "name": "list_recent_files",
            "description": "List recently opened file paths. Use to find something the user worked on before that is no longer in an open tab, then open it with open_file.",
            "inputSchema": no_args_schema()
        },
        {
            "name": "find_in_tab",
            "description": "Find a literal substring inside ONE open tab (defaults to the active tab); returns matching lines and a truncation flag. Use search_project instead to search across all tabs.",
            "inputSchema": {
                "type": "object",
                "properties": {
                    "tab_index": { "type": "integer", "minimum": 0, "description": "Zero-based tab index (default: active tab)" },
                    "query": { "type": "string", "description": "Literal text to search for" }
                },
                "required": ["query"],
                "additionalProperties": false
            }
        },
        {
            "name": "new_tab",
            "description": "Create a new untitled tab, optionally pre-filled with text. Use to hand generated or transformed content to the user inside the editor instead of pasting it into chat.",
            "inputSchema": {
                "type": "object",
                "properties": {
                    "text": { "type": "string", "description": "Initial content for the new tab (default: empty)" }
                },
                "required": [],
                "additionalProperties": false
            }
        },
        {
            "name": "goto_line",
            "description": "Move the editor cursor to a 1-based line number in a tab (defaults to the active tab). Use to point the user at a specific location, e.g. after finding a match.",
            "inputSchema": {
                "type": "object",
                "properties": {
                    "line": { "type": "integer", "minimum": 1, "description": "1-based line number" },
                    "tab_index": { "type": "integer", "minimum": 0, "description": "Zero-based tab index (default: active tab)" }
                },
                "required": ["line"],
                "additionalProperties": false
            }
        },
        {
            "name": "set_language",
            "description": "Set the syntax-highlighting language of a tab (defaults to the active tab). Use after new_tab, or when a file is highlighted with the wrong language.",
            "inputSchema": {
                "type": "object",
                "properties": {
                    "language": { "type": "string", "description": "Language name as shown in Notepatra's Language menu (e.g. \"Python\", \"SQL\")" },
                    "tab_index": { "type": "integer", "minimum": 0, "description": "Zero-based tab index (default: active tab)" }
                },
                "required": ["language"],
                "additionalProperties": false
            }
        },
        {
            "name": "compare_tabs",
            "description": "Open Notepatra's side-by-side Compare view for two open tabs. Use when the user wants to diff two documents they have open.",
            "inputSchema": {
                "type": "object",
                "properties": {
                    "index_a": { "type": "integer", "minimum": 0, "description": "Zero-based index of the first tab" },
                    "index_b": { "type": "integer", "minimum": 0, "description": "Zero-based index of the second tab" }
                },
                "required": ["index_a", "index_b"],
                "additionalProperties": false
            }
        },
        format_tool("format_json", "JSON", " Fails on invalid JSON."),
        format_tool("format_sql", "SQL", ""),
        format_tool("format_html", "HTML", ""),
        {
            "name": "list_notes",
            "description": "List the user's Noter notes with title, file name, and last-modified time. Use before read_note to discover which notes exist.",
            "inputSchema": no_args_schema()
        },
        {
            "name": "read_note",
            "description": "Read one Noter note by its file path (as returned by list_notes). Use to bring the user's meeting or personal notes into the conversation.",
            "inputSchema": {
                "type": "object",
                "properties": {
                    "file": { "type": "string", "description": "Absolute note file path from list_notes (ends in .html)" }
                },
                "required": ["file"],
                "additionalProperties": false
            }
        }
    ])
}

pub fn call(
    transport: &mut dyn EditorTransport,
    name: &str,
    args: &Map<String, Value>,
) -> CallOutcome {
    match name {
        "open_file" => open_file(transport, args),
        "list_open_tabs" => list_open_tabs(transport, args),
        "read_tab" => read_tab(transport, args),
        "search_project" => search_project(transport, args),
        "get_selection" => get_selection(transport, args),
        "get_status" => no_arg_json(args, || transport.get_status()),
        "app_info" => no_arg_json(args, || transport.app_info()),
        "list_recent_files" => no_arg_json(args, || transport.list_recent_files()),
        "find_in_tab" => find_in_tab(transport, args),
        "new_tab" => new_tab(transport, args),
        "goto_line" => goto_line(transport, args),
        "set_language" => set_language(transport, args),
        "compare_tabs" => compare_tabs(transport, args),
        "format_json" => format_text(transport, args, "json"),
        "format_sql" => format_text(transport, args, "sql"),
        "format_html" => format_text(transport, args, "html"),
        "list_notes" => no_arg_json(args, || transport.list_notes()),
        "read_note" => read_note(transport, args),
        other => CallOutcome::UnknownTool(other.to_string()),
    }
}

/// Enforces the schemas' `additionalProperties: false` at runtime.
fn reject_extras(args: &Map<String, Value>, allowed: &[&str]) -> Result<(), CallOutcome> {
    for key in args.keys() {
        if !allowed.contains(&key.as_str()) {
            return Err(CallOutcome::InvalidParams(format!(
                "unexpected argument {key:?}"
            )));
        }
    }
    Ok(())
}

fn required_str<'a>(args: &'a Map<String, Value>, key: &str) -> Result<&'a str, CallOutcome> {
    match args.get(key) {
        Some(Value::String(s)) => Ok(s),
        Some(_) => Err(CallOutcome::InvalidParams(format!(
            "{key} must be a string"
        ))),
        None => Err(CallOutcome::InvalidParams(format!("{key} is required"))),
    }
}

fn optional_str<'a>(
    args: &'a Map<String, Value>,
    key: &str,
) -> Result<Option<&'a str>, CallOutcome> {
    match args.get(key) {
        None => Ok(None),
        Some(Value::String(s)) => Ok(Some(s)),
        Some(_) => Err(CallOutcome::InvalidParams(format!(
            "{key} must be a string"
        ))),
    }
}

fn required_index(args: &Map<String, Value>, key: &str) -> Result<usize, CallOutcome> {
    match args.get(key) {
        Some(v) => v.as_u64().map(|n| n as usize).ok_or_else(|| {
            CallOutcome::InvalidParams(format!("{key} must be a non-negative integer"))
        }),
        None => Err(CallOutcome::InvalidParams(format!("{key} is required"))),
    }
}

fn optional_index(args: &Map<String, Value>, key: &str) -> Result<Option<usize>, CallOutcome> {
    match args.get(key) {
        None => Ok(None),
        Some(v) => v.as_u64().map(|n| Some(n as usize)).ok_or_else(|| {
            CallOutcome::InvalidParams(format!("{key} must be a non-negative integer"))
        }),
    }
}

fn json_text<T: serde::Serialize>(value: &T) -> CallOutcome {
    match serde_json::to_string_pretty(value) {
        Ok(s) => CallOutcome::Ok(s),
        Err(e) => CallOutcome::ToolError(format!("serialization failed: {e}")),
    }
}

fn to_outcome(result: Result<Value, crate::transport::TransportError>) -> CallOutcome {
    match result {
        Ok(v) => json_text(&v),
        Err(e) => CallOutcome::ToolError(e.0),
    }
}

/// Shared shape for the argument-less tools that pass the editor's JSON
/// result straight through.
fn no_arg_json(
    args: &Map<String, Value>,
    f: impl FnOnce() -> Result<Value, crate::transport::TransportError>,
) -> CallOutcome {
    if let Err(e) = reject_extras(args, &[]) {
        return e;
    }
    to_outcome(f())
}

fn open_file(transport: &mut dyn EditorTransport, args: &Map<String, Value>) -> CallOutcome {
    if let Err(e) = reject_extras(args, &["path"]) {
        return e;
    }
    let path = match required_str(args, "path") {
        Ok(p) => p,
        Err(e) => return e,
    };
    match transport.open_file(path) {
        Ok(index) => CallOutcome::Ok(format!("Opened {path} as tab {index}")),
        Err(e) => CallOutcome::ToolError(e.0),
    }
}

fn list_open_tabs(transport: &mut dyn EditorTransport, args: &Map<String, Value>) -> CallOutcome {
    if let Err(e) = reject_extras(args, &[]) {
        return e;
    }
    match transport.list_open_tabs() {
        Ok(tabs) => json_text(&tabs),
        Err(e) => CallOutcome::ToolError(e.0),
    }
}

fn read_tab(transport: &mut dyn EditorTransport, args: &Map<String, Value>) -> CallOutcome {
    if let Err(e) = reject_extras(args, &["tab_index", "title"]) {
        return e;
    }
    let selector = match (args.get("tab_index"), args.get("title")) {
        (Some(_), Some(_)) => {
            return CallOutcome::InvalidParams(
                "provide exactly one of tab_index or title, not both".into(),
            )
        }
        (None, None) => {
            return CallOutcome::InvalidParams("provide tab_index or title".into());
        }
        (Some(v), None) => match v.as_u64() {
            Some(i) => TabSelector::Index(i as usize),
            None => {
                return CallOutcome::InvalidParams(
                    "tab_index must be a non-negative integer".into(),
                )
            }
        },
        (None, Some(v)) => match v.as_str() {
            Some(t) => TabSelector::Title(t),
            None => return CallOutcome::InvalidParams("title must be a string".into()),
        },
    };
    match transport.read_tab(selector) {
        Ok(content) => CallOutcome::Ok(content.text),
        Err(e) => CallOutcome::ToolError(e.0),
    }
}

fn search_project(transport: &mut dyn EditorTransport, args: &Map<String, Value>) -> CallOutcome {
    if let Err(e) = reject_extras(args, &["query", "max_results"]) {
        return e;
    }
    let query = match required_str(args, "query") {
        Ok(q) => q,
        Err(e) => return e,
    };
    let max_results = match args.get("max_results") {
        None => DEFAULT_MAX_RESULTS,
        Some(v) => match v.as_u64() {
            Some(n) if n >= 1 => n as usize,
            _ => return CallOutcome::InvalidParams("max_results must be an integer >= 1".into()),
        },
    };
    match transport.search_project(query, max_results) {
        Ok(results) => json_text(&results),
        Err(e) => CallOutcome::ToolError(e.0),
    }
}

fn get_selection(transport: &mut dyn EditorTransport, args: &Map<String, Value>) -> CallOutcome {
    if let Err(e) = reject_extras(args, &[]) {
        return e;
    }
    match transport.get_selection() {
        Ok(sel) => json_text(&sel),
        Err(e) => CallOutcome::ToolError(e.0),
    }
}

fn find_in_tab(transport: &mut dyn EditorTransport, args: &Map<String, Value>) -> CallOutcome {
    if let Err(e) = reject_extras(args, &["tab_index", "query"]) {
        return e;
    }
    let query = match required_str(args, "query") {
        Ok(q) => q,
        Err(e) => return e,
    };
    let tab_index = match optional_index(args, "tab_index") {
        Ok(i) => i,
        Err(e) => return e,
    };
    to_outcome(transport.find_in_tab(tab_index, query))
}

fn new_tab(transport: &mut dyn EditorTransport, args: &Map<String, Value>) -> CallOutcome {
    if let Err(e) = reject_extras(args, &["text"]) {
        return e;
    }
    let text = match optional_str(args, "text") {
        Ok(t) => t,
        Err(e) => return e,
    };
    to_outcome(transport.new_tab(text))
}

fn goto_line(transport: &mut dyn EditorTransport, args: &Map<String, Value>) -> CallOutcome {
    if let Err(e) = reject_extras(args, &["line", "tab_index"]) {
        return e;
    }
    let line = match required_index(args, "line") {
        Ok(n) if n >= 1 => n,
        Ok(_) => return CallOutcome::InvalidParams("line must be an integer >= 1".into()),
        Err(e) => return e,
    };
    let tab_index = match optional_index(args, "tab_index") {
        Ok(i) => i,
        Err(e) => return e,
    };
    to_outcome(transport.goto_line(line, tab_index))
}

fn set_language(transport: &mut dyn EditorTransport, args: &Map<String, Value>) -> CallOutcome {
    if let Err(e) = reject_extras(args, &["language", "tab_index"]) {
        return e;
    }
    let language = match required_str(args, "language") {
        Ok(l) => l,
        Err(e) => return e,
    };
    let tab_index = match optional_index(args, "tab_index") {
        Ok(i) => i,
        Err(e) => return e,
    };
    to_outcome(transport.set_language(language, tab_index))
}

fn compare_tabs(transport: &mut dyn EditorTransport, args: &Map<String, Value>) -> CallOutcome {
    if let Err(e) = reject_extras(args, &["index_a", "index_b"]) {
        return e;
    }
    let index_a = match required_index(args, "index_a") {
        Ok(i) => i,
        Err(e) => return e,
    };
    let index_b = match required_index(args, "index_b") {
        Ok(i) => i,
        Err(e) => return e,
    };
    to_outcome(transport.compare_tabs(index_a, index_b))
}

fn format_text(
    transport: &mut dyn EditorTransport,
    args: &Map<String, Value>,
    kind: &str,
) -> CallOutcome {
    if let Err(e) = reject_extras(args, &["text"]) {
        return e;
    }
    let text = match required_str(args, "text") {
        Ok(t) => t,
        Err(e) => return e,
    };
    match transport.format_text(kind, text) {
        // Unwrap {text} so the tool returns the formatted text itself.
        Ok(v) => match v.get("text").and_then(Value::as_str) {
            Some(t) => CallOutcome::Ok(t.to_string()),
            None => CallOutcome::ToolError(
                "malformed editor response (missing or mistyped \"text\")".into(),
            ),
        },
        Err(e) => CallOutcome::ToolError(e.0),
    }
}

fn read_note(transport: &mut dyn EditorTransport, args: &Map<String, Value>) -> CallOutcome {
    if let Err(e) = reject_extras(args, &["file"]) {
        return e;
    }
    let file = match required_str(args, "file") {
        Ok(f) => f,
        Err(e) => return e,
    };
    to_outcome(transport.read_note(file))
}
