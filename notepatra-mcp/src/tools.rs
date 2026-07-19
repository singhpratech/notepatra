// SPDX-License-Identifier: GPL-3.0-or-later
use serde_json::{json, Map, Value};

use crate::transport::{EditorTransport, TabSelector};

const DEFAULT_MAX_RESULTS: usize = 50;
/// Server-side cap mirrored from the C++ bridge: search_project never
/// returns more than this many matches, so larger requests are clamped
/// client-side instead of asking for results that cannot come back.
const MAX_RESULTS_CAP: usize = 200;

/// Tier partition of the tool surface. get_capabilities reports these sizes;
/// tests/protocol.rs asserts the three lists partition definitions() exactly,
/// so a new tool that isn't tiered here fails the suite.
pub const READ_TOOLS: &[&str] = &[
    "list_open_tabs",
    "read_tab",
    "get_selection",
    "get_status",
    "app_info",
    "list_recent_files",
    "find_in_tab",
    "search_project",
    "list_notes",
    "read_note",
    "list_reminders",
    "git_status",
    "git_diff",
    "git_log",
    "git_show",
    "git_branch",
    "validate_npd",
    "run_sql",
    "list_languages",
    "get_capabilities",
    "get_diagram_source",
    // phase 2
    "list_connections",
    "run_query",
    "list_tables",
];
pub const ACT_TOOLS: &[&str] = &[
    "open_file",
    "new_tab",
    "goto_line",
    "set_language",
    "compare_tabs",
    "format_json",
    "format_sql",
    "format_html",
    "open_note",
    "create_diagram",
    "open_noter",
    // phase 2
    "open_data_analyst",
    "render_chart",
];
pub const WRITE_TOOLS: &[&str] = &[
    "insert_text",
    "replace_selection",
    "apply_edit",
    "save_tab",
    "create_note",
    "append_note",
    "set_reminder",
    "export_diagram",
    "set_diagram_source",
    // phase 2
    "export_query_results",
    "export_chart",
];

/// Mandatory sentence on every write tool's description: these tools are
/// gated behind an in-editor human approval card.
const APPROVAL_NOTE: &str = "Requires the user to click Approve on a card inside Notepatra; \
     the call blocks until they respond (up to 2 minutes) and returns an error if denied \
     or timed out.";

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
            "description": "Search ALL open tabs plus files under the workspace folder for a literal substring (case-insensitive); returns matching lines with file path and line number. The editor caps results at 200 per search; larger max_results values are clamped to 200. Use to locate text when you don't know which tab or file it is in; use find_in_tab for a single tab.",
            "inputSchema": {
                "type": "object",
                "properties": {
                    "query": { "type": "string", "description": "Literal text to search for (or a regular expression when regex is true)" },
                    "max_results": { "type": "integer", "minimum": 1, "description": "Maximum matches to return (default 50; values above 200 are clamped to the editor's 200-result cap)" },
                    "regex": { "type": "boolean", "description": "When true, query is a Qt-compatible regular expression; the server rejects an invalid pattern (default false: literal substring)" }
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
                    "query": { "type": "string", "description": "Literal text to search for (or a regular expression when regex is true)" },
                    "regex": { "type": "boolean", "description": "When true, query is a Qt-compatible regular expression; the server rejects an invalid pattern (default false: literal substring)" }
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
        },
        {
            "name": "insert_text",
            "description": format!(
                "Insert text into an open tab (default: the active tab) at the cursor, \
                 or at an explicit 1-based line/column. {APPROVAL_NOTE}"
            ),
            "inputSchema": {
                "type": "object",
                "properties": {
                    "text": { "type": "string", "description": "The text to insert" },
                    "tab_index": { "type": "integer", "minimum": 0, "description": "Zero-based tab index (default: active tab)" },
                    "line": { "type": "integer", "minimum": 1, "description": "1-based line to insert at (default: current cursor position)" },
                    "col": { "type": "integer", "minimum": 1, "description": "1-based column to insert at (default: start of the line)" }
                },
                "required": ["text"],
                "additionalProperties": false
            }
        },
        {
            "name": "replace_selection",
            "description": format!(
                "Replace the currently selected text in a tab (default: the active tab) \
                 with new text. {APPROVAL_NOTE}"
            ),
            "inputSchema": {
                "type": "object",
                "properties": {
                    "text": { "type": "string", "description": "The replacement text" },
                    "tab_index": { "type": "integer", "minimum": 0, "description": "Zero-based tab index (default: active tab)" }
                },
                "required": ["text"],
                "additionalProperties": false
            }
        },
        {
            "name": "apply_edit",
            "description": format!(
                "Find-and-replace a literal string in one open tab (default: the active \
                 tab): replaces the first occurrence, or every occurrence with all=true, \
                 and returns the replacement count. {APPROVAL_NOTE}"
            ),
            "inputSchema": {
                "type": "object",
                "properties": {
                    "find": { "type": "string", "description": "Literal text to find" },
                    "replace": { "type": "string", "description": "Replacement text" },
                    "tab_index": { "type": "integer", "minimum": 0, "description": "Zero-based tab index (default: active tab)" },
                    "all": { "type": "boolean", "description": "Replace every occurrence (default false: first occurrence only)" }
                },
                "required": ["find", "replace"],
                "additionalProperties": false
            }
        },
        {
            "name": "save_tab",
            "description": format!(
                "Save an open tab (default: the active tab) to its file on disk. \
                 {APPROVAL_NOTE}"
            ),
            "inputSchema": {
                "type": "object",
                "properties": {
                    "tab_index": { "type": "integer", "minimum": 0, "description": "Zero-based tab index (default: active tab)" }
                },
                "required": [],
                "additionalProperties": false
            }
        },
        // ── v0.1.119 read tier ──────────────────────────────────────────
        {
            "name": "list_reminders",
            "description": "List the user's Noter reminders, each tagged with a time bucket (Overdue, Today, This week, or Later), the note file it is bound to, and its due time. Use to see what the user has scheduled before acting on it.",
            "inputSchema": no_args_schema()
        },
        {
            "name": "git_status",
            "description": "Get the Git status of the workspace repository: current branch, ahead/behind counts, and staged, unstaged, and untracked paths. Use to see what has changed before diffing or committing.",
            "inputSchema": no_args_schema()
        },
        {
            "name": "git_diff",
            "description": "Get the Git diff of the workspace repository (all changes, or just one path). Use after git_status to inspect exactly what changed.",
            "inputSchema": {
                "type": "object",
                "properties": {
                    "path": { "type": "string", "description": "Restrict the diff to this repo-relative or absolute path (default: the whole working tree)" }
                },
                "required": [],
                "additionalProperties": false
            }
        },
        {
            "name": "git_log",
            "description": "List recent Git commits (hash, author, date, subject), most recent first. Use to review history before referencing a commit with git_show.",
            "inputSchema": {
                "type": "object",
                "properties": {
                    "limit": { "type": "integer", "minimum": 1, "description": "How many commits to return (default 20; values above 100 are clamped to 100)" }
                },
                "required": [],
                "additionalProperties": false
            }
        },
        {
            "name": "git_show",
            "description": "Show one Git commit by ref (hash, branch, or tag): author, date, message, and diff. Use to inspect a specific commit found via git_log.",
            "inputSchema": {
                "type": "object",
                "properties": {
                    "ref": { "type": "string", "description": "Commit reference: a hash, branch, or tag" }
                },
                "required": ["ref"],
                "additionalProperties": false
            }
        },
        {
            "name": "git_branch",
            "description": "List the workspace repository's local branches and which one is currently checked out. Use to orient before referencing branches.",
            "inputSchema": no_args_schema()
        },
        {
            "name": "validate_npd",
            "description": "Validate a Notepatra Diagram (.npd) document and return whether it parses plus any errors with line and column. Provide exactly one of tab_index (validate an open tab) or source (validate a string). Use before export_diagram to confirm the diagram is well-formed.",
            "inputSchema": {
                "type": "object",
                "properties": {
                    "tab_index": { "type": "integer", "minimum": 0, "description": "Zero-based index of an open tab holding the .npd source" },
                    "source": { "type": "string", "description": "The .npd document text to validate directly" }
                },
                "required": [],
                "additionalProperties": false
            }
        },
        {
            "name": "run_sql",
            "description": "Run a read-only SQL query against the editor's data engine and return columns and rows. SELECT-only: any non-read statement (INSERT/UPDATE/DELETE/DDL) is rejected by the editor and returns an error. Optionally query a CSV file by path instead of the open database.",
            "inputSchema": {
                "type": "object",
                "properties": {
                    "sql": { "type": "string", "description": "A read-only SQL SELECT (or WITH) statement; non-read statements are rejected by the editor" },
                    "csv_path": { "type": "string", "description": "Absolute path of a CSV file to query instead of the open database (default: the editor's current data source)" }
                },
                "required": ["sql"],
                "additionalProperties": false
            }
        },
        // ── v0.1.119 act tier ───────────────────────────────────────────
        {
            "name": "open_note",
            "description": "Open a Noter note in the editor by its file path (as returned by list_notes or list_reminders). Use to bring a note on screen for the user to read or edit.",
            "inputSchema": {
                "type": "object",
                "properties": {
                    "file": { "type": "string", "description": "Absolute note file path (ends in .html)" }
                },
                "required": ["file"],
                "additionalProperties": false
            }
        },
        // ── v0.1.119 write tier ─────────────────────────────────────────
        {
            "name": "create_note",
            "description": format!(
                "Create a new Noter note with a title and body and return its file path. \
                 {APPROVAL_NOTE}"
            ),
            "inputSchema": {
                "type": "object",
                "properties": {
                    "title": { "type": "string", "description": "The note's title" },
                    "body": { "type": "string", "description": "The note's initial body text" }
                },
                "required": ["title", "body"],
                "additionalProperties": false
            }
        },
        {
            "name": "append_note",
            "description": format!(
                "Append text to an existing Noter note, identified by its file path. \
                 {APPROVAL_NOTE}"
            ),
            "inputSchema": {
                "type": "object",
                "properties": {
                    "file": { "type": "string", "description": "Absolute note file path from list_notes (ends in .html)" },
                    "text": { "type": "string", "description": "Text to append to the note" }
                },
                "required": ["file", "text"],
                "additionalProperties": false
            }
        },
        {
            "name": "set_reminder",
            "description": format!(
                "Set a reminder on a Noter note (identified by its file path) for a due time. \
                 {APPROVAL_NOTE}"
            ),
            "inputSchema": {
                "type": "object",
                "properties": {
                    "file": { "type": "string", "description": "Absolute note file path from list_notes (ends in .html)" },
                    "due_iso": { "type": "string", "description": "The reminder's due time as an ISO 8601 timestamp (e.g. 2026-07-20T15:00:00Z)" }
                },
                "required": ["file", "due_iso"],
                "additionalProperties": false
            }
        },
        {
            "name": "export_diagram",
            "description": format!(
                "Export the Notepatra Diagram (.npd) in an open tab to an image or PDF at a path on disk. \
                 {APPROVAL_NOTE}"
            ),
            "inputSchema": {
                "type": "object",
                "properties": {
                    "tab_index": { "type": "integer", "minimum": 0, "description": "Zero-based index of the tab holding the .npd diagram" },
                    "path": { "type": "string", "description": "Absolute output path for the exported file" },
                    "format": { "type": "string", "enum": ["png", "pdf"], "description": "Export format: png or pdf" }
                },
                "required": ["tab_index", "path", "format"],
                "additionalProperties": false
            }
        },
        // ── p0a read tier ───────────────────────────────────────────────
        {
            "name": "list_languages",
            "description": "List the canonical language names Notepatra's syntax highlighting accepts (exactly as shown in the Language menu). Use before set_language when unsure of the exact name.",
            "inputSchema": no_args_schema()
        },
        {
            "name": "get_capabilities",
            "description": "Get the editor's capability profile: edition (Lite/Full), platform, version, tool count, tool tiers, and feature flags (duckdb, webengine, noter). Use to self-diagnose what this Notepatra build supports before relying on edition-specific tools like run_sql over CSV.",
            "inputSchema": no_args_schema()
        },
        // ── phase 1: diagram control + Noter panel ─────────────────────
        {
            "name": "create_diagram",
            "description": "Create a new Notepatra Diagram (.npd) tab in the editor, optionally pre-filled with source, and focus it. The tab is created even if the source has parse errors (the canvas shows its parse state); the result reports valid plus any errors with line numbers. Use set_diagram_source to edit it and export_diagram to render it to a file.",
            "inputSchema": {
                "type": "object",
                "properties": {
                    "source": { "type": "string", "description": "Initial .npd document text (default: an empty diagram)" },
                    "title": { "type": "string", "description": "Tab title (default \"Diagram\")" }
                },
                "required": [],
                "additionalProperties": false
            }
        },
        {
            "name": "get_diagram_source",
            "description": "Read the .npd source of an open Diagram tab by index. Errors if the tab is not a Diagram tab; use list_open_tabs to find it.",
            "inputSchema": {
                "type": "object",
                "properties": {
                    "tab_index": { "type": "integer", "minimum": 0, "description": "Zero-based index of the Diagram tab" }
                },
                "required": ["tab_index"],
                "additionalProperties": false
            }
        },
        {
            "name": "set_diagram_source",
            "description": format!(
                "Replace the entire .npd source of an open Diagram tab; the canvas \
                 re-renders immediately and the result reports valid plus any parse \
                 errors. {APPROVAL_NOTE}"
            ),
            "inputSchema": {
                "type": "object",
                "properties": {
                    "tab_index": { "type": "integer", "minimum": 0, "description": "Zero-based index of the Diagram tab" },
                    "source": { "type": "string", "description": "The new .npd document text (replaces the whole source)" }
                },
                "required": ["tab_index", "source"],
                "additionalProperties": false
            }
        },
        {
            "name": "open_noter",
            "description": "Open (or focus) the Noter panel tab in the editor — the same surface the user gets from the Noter menu. Use before pointing the user at their notes.",
            "inputSchema": no_args_schema()
        },
        // ── phase 2: data-analyst + charts ─────────────────────────────
        {
            "name": "list_connections",
            "description": "List the user's SAVED database connections (name, driver, database, read_only) — never passwords. These are the named PostgreSQL / MySQL / SQL Server / SQLite / DuckDB connections managed in the Data Analyst panel, which run_query can read but run_sql cannot reach. Use before run_query or list_tables to discover which connections exist.",
            "inputSchema": no_args_schema()
        },
        {
            "name": "run_query",
            "description": "Run a read-only SQL query against a SAVED named connection (PostgreSQL / MySQL / SQL Server / SQLite / DuckDB) and return columns and rows. SELECT-only: mutations (INSERT/UPDATE/DELETE/DDL) are rejected by the editor. Passwords are never involved. Unlike run_sql, which only reaches the built-in scratch engine, this reaches the user's real saved databases. DuckDB-driver connections require the Full edition.",
            "inputSchema": {
                "type": "object",
                "properties": {
                    "connection_name": { "type": "string", "description": "Name of a saved connection (see list_connections)" },
                    "sql": { "type": "string", "description": "A read-only SQL SELECT (or WITH) statement; non-read statements are rejected by the editor" },
                    "max_rows": { "type": "integer", "minimum": 1, "description": "Maximum rows to return (default 200; clamped to the editor's 200-row cap)" }
                },
                "required": ["connection_name", "sql"],
                "additionalProperties": false
            }
        },
        {
            "name": "list_tables",
            "description": "List the user tables available over a saved named connection (see list_connections). Use to discover the schema before writing a run_query SELECT.",
            "inputSchema": {
                "type": "object",
                "properties": {
                    "connection_name": { "type": "string", "description": "Name of a saved connection (see list_connections)" }
                },
                "required": ["connection_name"],
                "additionalProperties": false
            }
        },
        {
            "name": "open_data_analyst",
            "description": "Reveal and focus the editor's AI dock in Data Analyst mode — the surface for querying saved connections and rendering charts. Use to bring the Data Analyst on screen for the user.",
            "inputSchema": no_args_schema()
        },
        {
            "name": "render_chart",
            "description": "Render a chart inline in the Data Analyst transcript and return its id. The spec may be a Vega-Lite v5 spec, or the simplified {type,x,y,data} form the editor translates. Requires the Full edition (WebEngine); the Lite edition returns an error.",
            "inputSchema": {
                "type": "object",
                "properties": {
                    "spec": { "type": "object", "description": "A Vega-Lite v5 spec, or the simplified {type,x,y,data} form the editor translates" },
                    "title": { "type": "string", "description": "Optional title shown above the chart card" }
                },
                "required": ["spec"],
                "additionalProperties": false
            }
        },
        {
            "name": "export_query_results",
            "description": format!(
                "Run a read-only SELECT against a saved named connection and write the \
                 results to a file on disk as CSV or JSON. Mutations are rejected. \
                 {APPROVAL_NOTE}"
            ),
            "inputSchema": {
                "type": "object",
                "properties": {
                    "connection_name": { "type": "string", "description": "Name of a saved connection (see list_connections)" },
                    "sql": { "type": "string", "description": "A read-only SQL SELECT (or WITH) statement" },
                    "path": { "type": "string", "description": "Absolute output path for the exported file" },
                    "format": { "type": "string", "enum": ["csv", "json"], "description": "Serialization format: csv or json" },
                    "max_rows": { "type": "integer", "minimum": 1, "description": "Maximum rows to export (default 10000; capped at 100000)" }
                },
                "required": ["connection_name", "sql", "path", "format"],
                "additionalProperties": false
            }
        },
        {
            "name": "export_chart",
            "description": format!(
                "Render a chart off-screen and export it to a file on disk as PNG, SVG, \
                 self-contained HTML, or the Vega-Lite spec JSON. The spec may be a \
                 Vega-Lite v5 spec or the simplified {{type,x,y,data}} form. Requires the \
                 Full edition (WebEngine). {APPROVAL_NOTE}"
            ),
            "inputSchema": {
                "type": "object",
                "properties": {
                    "spec": { "type": "object", "description": "A Vega-Lite v5 spec, or the simplified {type,x,y,data} form the editor translates" },
                    "path": { "type": "string", "description": "Absolute output path for the exported file" },
                    "format": { "type": "string", "enum": ["png", "svg", "html", "spec"], "description": "Export format: png, svg, html, or spec (Vega-Lite JSON)" },
                    "scale": { "type": "integer", "minimum": 1, "maximum": 4, "description": "PNG raster scale factor 1-4 (default 2; ignored for non-png formats)" }
                },
                "required": ["spec", "path", "format"],
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
        "insert_text" => insert_text(transport, args),
        "replace_selection" => replace_selection(transport, args),
        "apply_edit" => apply_edit(transport, args),
        "save_tab" => save_tab(transport, args),
        // v0.1.119
        "list_reminders" => no_arg_json(args, || transport.list_reminders()),
        "git_status" => no_arg_json(args, || transport.git_status()),
        "git_diff" => git_diff(transport, args),
        "git_log" => git_log(transport, args),
        "git_show" => git_show(transport, args),
        "git_branch" => no_arg_json(args, || transport.git_branch()),
        "validate_npd" => validate_npd(transport, args),
        "run_sql" => run_sql(transport, args),
        "open_note" => open_note(transport, args),
        "create_note" => create_note(transport, args),
        "append_note" => append_note(transport, args),
        "set_reminder" => set_reminder(transport, args),
        "export_diagram" => export_diagram(transport, args),
        // p0a
        "list_languages" => no_arg_json(args, || transport.list_languages()),
        "get_capabilities" => get_capabilities(transport, args),
        // phase 1
        "create_diagram" => create_diagram(transport, args),
        "get_diagram_source" => get_diagram_source(transport, args),
        "set_diagram_source" => set_diagram_source(transport, args),
        "open_noter" => open_noter(transport, args),
        // phase 2
        "list_connections" => no_arg_json(args, || transport.list_connections()),
        "run_query" => run_query(transport, args),
        "list_tables" => list_tables(transport, args),
        "open_data_analyst" => no_arg_json(args, || transport.open_data_analyst()),
        "render_chart" => render_chart(transport, args),
        "export_query_results" => export_query_results(transport, args),
        "export_chart" => export_chart(transport, args),
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

/// Optional 1-based integer (line/column numbers).
fn optional_one_based(args: &Map<String, Value>, key: &str) -> Result<Option<usize>, CallOutcome> {
    match optional_index(args, key)? {
        Some(0) => Err(CallOutcome::InvalidParams(format!(
            "{key} must be an integer >= 1"
        ))),
        v => Ok(v),
    }
}

fn optional_bool(args: &Map<String, Value>, key: &str, default: bool) -> Result<bool, CallOutcome> {
    match args.get(key) {
        None => Ok(default),
        Some(Value::Bool(b)) => Ok(*b),
        Some(_) => Err(CallOutcome::InvalidParams(format!(
            "{key} must be a boolean"
        ))),
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
        // Surface the wire's truncated flag in the text itself — MCP text
        // content has no side channel for it.
        Ok(content) => CallOutcome::Ok(content.text_with_marker()),
        Err(e) => CallOutcome::ToolError(e.0),
    }
}

fn search_project(transport: &mut dyn EditorTransport, args: &Map<String, Value>) -> CallOutcome {
    if let Err(e) = reject_extras(args, &["query", "max_results", "regex"]) {
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
    let regex = match optional_bool(args, "regex", false) {
        Ok(r) => r,
        Err(e) => return e,
    };
    // Clamp to the editor's server-side cap rather than requesting results
    // that can never come back.
    let max_results = max_results.min(MAX_RESULTS_CAP);
    match transport.search_project(query, max_results, regex) {
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
    if let Err(e) = reject_extras(args, &["tab_index", "query", "regex"]) {
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
    let regex = match optional_bool(args, "regex", false) {
        Ok(r) => r,
        Err(e) => return e,
    };
    to_outcome(transport.find_in_tab(tab_index, query, regex))
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

// Write tools: approval-gated in the editor. "denied by user" and "approval
// timed out" arrive as transport errors and pass through VERBATIM as
// isError tool results via to_outcome.

fn insert_text(transport: &mut dyn EditorTransport, args: &Map<String, Value>) -> CallOutcome {
    if let Err(e) = reject_extras(args, &["text", "tab_index", "line", "col"]) {
        return e;
    }
    let text = match required_str(args, "text") {
        Ok(t) => t,
        Err(e) => return e,
    };
    let tab_index = match optional_index(args, "tab_index") {
        Ok(i) => i,
        Err(e) => return e,
    };
    let line = match optional_one_based(args, "line") {
        Ok(l) => l,
        Err(e) => return e,
    };
    let col = match optional_one_based(args, "col") {
        Ok(c) => c,
        Err(e) => return e,
    };
    to_outcome(transport.insert_text(text, tab_index, line, col))
}

fn replace_selection(
    transport: &mut dyn EditorTransport,
    args: &Map<String, Value>,
) -> CallOutcome {
    if let Err(e) = reject_extras(args, &["text", "tab_index"]) {
        return e;
    }
    let text = match required_str(args, "text") {
        Ok(t) => t,
        Err(e) => return e,
    };
    let tab_index = match optional_index(args, "tab_index") {
        Ok(i) => i,
        Err(e) => return e,
    };
    to_outcome(transport.replace_selection(text, tab_index))
}

fn apply_edit(transport: &mut dyn EditorTransport, args: &Map<String, Value>) -> CallOutcome {
    if let Err(e) = reject_extras(args, &["find", "replace", "tab_index", "all"]) {
        return e;
    }
    let find = match required_str(args, "find") {
        Ok(f) => f,
        Err(e) => return e,
    };
    let replace = match required_str(args, "replace") {
        Ok(r) => r,
        Err(e) => return e,
    };
    let tab_index = match optional_index(args, "tab_index") {
        Ok(i) => i,
        Err(e) => return e,
    };
    let all = match optional_bool(args, "all", false) {
        Ok(a) => a,
        Err(e) => return e,
    };
    to_outcome(transport.apply_edit(find, replace, tab_index, all))
}

fn save_tab(transport: &mut dyn EditorTransport, args: &Map<String, Value>) -> CallOutcome {
    if let Err(e) = reject_extras(args, &["tab_index"]) {
        return e;
    }
    let tab_index = match optional_index(args, "tab_index") {
        Ok(i) => i,
        Err(e) => return e,
    };
    to_outcome(transport.save_tab(tab_index))
}

// ── v0.1.119 read-tier handlers ─────────────────────────────────────────────

/// git_log's `limit`: 1..=100, default 20 (mirrors the tool description).
const GIT_LOG_DEFAULT_LIMIT: usize = 20;
const GIT_LOG_MAX_LIMIT: usize = 100;

fn git_diff(transport: &mut dyn EditorTransport, args: &Map<String, Value>) -> CallOutcome {
    if let Err(e) = reject_extras(args, &["path"]) {
        return e;
    }
    let path = match optional_str(args, "path") {
        Ok(p) => p,
        Err(e) => return e,
    };
    to_outcome(transport.git_diff(path))
}

fn git_log(transport: &mut dyn EditorTransport, args: &Map<String, Value>) -> CallOutcome {
    if let Err(e) = reject_extras(args, &["limit"]) {
        return e;
    }
    let limit = match args.get("limit") {
        None => GIT_LOG_DEFAULT_LIMIT,
        Some(v) => match v.as_u64() {
            Some(n) if n >= 1 => (n as usize).min(GIT_LOG_MAX_LIMIT),
            _ => return CallOutcome::InvalidParams("limit must be an integer >= 1".into()),
        },
    };
    to_outcome(transport.git_log(limit))
}

fn git_show(transport: &mut dyn EditorTransport, args: &Map<String, Value>) -> CallOutcome {
    if let Err(e) = reject_extras(args, &["ref"]) {
        return e;
    }
    let git_ref = match required_str(args, "ref") {
        Ok(r) => r,
        Err(e) => return e,
    };
    to_outcome(transport.git_show(git_ref))
}

fn validate_npd(transport: &mut dyn EditorTransport, args: &Map<String, Value>) -> CallOutcome {
    if let Err(e) = reject_extras(args, &["tab_index", "source"]) {
        return e;
    }
    // Exactly one selector, mirroring read_tab's index-or-title contract.
    match (args.get("tab_index"), args.get("source")) {
        (Some(_), Some(_)) => {
            return CallOutcome::InvalidParams(
                "provide exactly one of tab_index or source, not both".into(),
            )
        }
        (None, None) => {
            return CallOutcome::InvalidParams("provide tab_index or source".into());
        }
        _ => {}
    }
    let tab_index = match optional_index(args, "tab_index") {
        Ok(i) => i,
        Err(e) => return e,
    };
    let source = match optional_str(args, "source") {
        Ok(s) => s,
        Err(e) => return e,
    };
    to_outcome(transport.validate_npd(tab_index, source))
}

fn run_sql(transport: &mut dyn EditorTransport, args: &Map<String, Value>) -> CallOutcome {
    if let Err(e) = reject_extras(args, &["sql", "csv_path"]) {
        return e;
    }
    let sql = match required_str(args, "sql") {
        Ok(s) => s,
        Err(e) => return e,
    };
    let csv_path = match optional_str(args, "csv_path") {
        Ok(p) => p,
        Err(e) => return e,
    };
    to_outcome(transport.run_sql(sql, csv_path))
}

// ── v0.1.119 act-tier handler ───────────────────────────────────────────────

fn open_note(transport: &mut dyn EditorTransport, args: &Map<String, Value>) -> CallOutcome {
    if let Err(e) = reject_extras(args, &["file"]) {
        return e;
    }
    let file = match required_str(args, "file") {
        Ok(f) => f,
        Err(e) => return e,
    };
    to_outcome(transport.open_note(file))
}

// ── v0.1.119 write-tier handlers (approval-gated in the editor) ──────────────

fn create_note(transport: &mut dyn EditorTransport, args: &Map<String, Value>) -> CallOutcome {
    if let Err(e) = reject_extras(args, &["title", "body"]) {
        return e;
    }
    let title = match required_str(args, "title") {
        Ok(t) => t,
        Err(e) => return e,
    };
    let body = match required_str(args, "body") {
        Ok(b) => b,
        Err(e) => return e,
    };
    to_outcome(transport.create_note(title, body))
}

fn append_note(transport: &mut dyn EditorTransport, args: &Map<String, Value>) -> CallOutcome {
    if let Err(e) = reject_extras(args, &["file", "text"]) {
        return e;
    }
    let file = match required_str(args, "file") {
        Ok(f) => f,
        Err(e) => return e,
    };
    let text = match required_str(args, "text") {
        Ok(t) => t,
        Err(e) => return e,
    };
    to_outcome(transport.append_note(file, text))
}

fn set_reminder(transport: &mut dyn EditorTransport, args: &Map<String, Value>) -> CallOutcome {
    if let Err(e) = reject_extras(args, &["file", "due_iso"]) {
        return e;
    }
    let file = match required_str(args, "file") {
        Ok(f) => f,
        Err(e) => return e,
    };
    let due_iso = match required_str(args, "due_iso") {
        Ok(d) => d,
        Err(e) => return e,
    };
    to_outcome(transport.set_reminder(file, due_iso))
}

fn export_diagram(transport: &mut dyn EditorTransport, args: &Map<String, Value>) -> CallOutcome {
    if let Err(e) = reject_extras(args, &["tab_index", "path", "format"]) {
        return e;
    }
    let tab_index = match required_index(args, "tab_index") {
        Ok(i) => i,
        Err(e) => return e,
    };
    let path = match required_str(args, "path") {
        Ok(p) => p,
        Err(e) => return e,
    };
    let format = match required_str(args, "format") {
        Ok(f) => f,
        Err(e) => return e,
    };
    if format != "png" && format != "pdf" {
        return CallOutcome::InvalidParams("format must be \"png\" or \"pdf\"".into());
    }
    to_outcome(transport.export_diagram(tab_index, path, format))
}

// ── p0a read-tier handlers ──────────────────────────────────────────────────

fn get_capabilities(transport: &mut dyn EditorTransport, args: &Map<String, Value>) -> CallOutcome {
    if let Err(e) = reject_extras(args, &[]) {
        return e;
    }
    match transport.get_capabilities() {
        Ok(mut v) => {
            // tool_count and tiers are DERIVED here, where the tool surface
            // lives — the editor cannot know the sidecar's tool list.
            if let Value::Object(m) = &mut v {
                let count = definitions().as_array().map_or(0, |a| a.len());
                m.insert("tool_count".into(), json!(count));
                m.insert(
                    "tiers".into(),
                    json!({
                        "read": READ_TOOLS.len(),
                        "act": ACT_TOOLS.len(),
                        "write": WRITE_TOOLS.len(),
                    }),
                );
            }
            json_text(&v)
        }
        Err(e) => CallOutcome::ToolError(e.0),
    }
}

// ── Phase 1 handlers ────────────────────────────────────────────────────────

fn create_diagram(transport: &mut dyn EditorTransport, args: &Map<String, Value>) -> CallOutcome {
    if let Err(e) = reject_extras(args, &["source", "title"]) {
        return e;
    }
    let source = match optional_str(args, "source") {
        Ok(s) => s,
        Err(e) => return e,
    };
    let title = match optional_str(args, "title") {
        Ok(t) => t,
        Err(e) => return e,
    };
    to_outcome(transport.create_diagram(source, title))
}

fn get_diagram_source(
    transport: &mut dyn EditorTransport,
    args: &Map<String, Value>,
) -> CallOutcome {
    if let Err(e) = reject_extras(args, &["tab_index"]) {
        return e;
    }
    let tab_index = match required_index(args, "tab_index") {
        Ok(i) => i,
        Err(e) => return e,
    };
    to_outcome(transport.get_diagram_source(tab_index))
}

// Approval-gated in the editor; deny/timeout errors pass through verbatim.
fn set_diagram_source(
    transport: &mut dyn EditorTransport,
    args: &Map<String, Value>,
) -> CallOutcome {
    if let Err(e) = reject_extras(args, &["tab_index", "source"]) {
        return e;
    }
    let tab_index = match required_index(args, "tab_index") {
        Ok(i) => i,
        Err(e) => return e,
    };
    let source = match required_str(args, "source") {
        Ok(s) => s,
        Err(e) => return e,
    };
    to_outcome(transport.set_diagram_source(tab_index, source))
}

fn open_noter(transport: &mut dyn EditorTransport, args: &Map<String, Value>) -> CallOutcome {
    if let Err(e) = reject_extras(args, &[]) {
        return e;
    }
    to_outcome(transport.open_noter())
}

// ── Phase 2 handlers — data-analyst + charts ────────────────────────────────

/// run_query's `max_rows` cap mirrors the editor's 200-row ceiling.
const RUN_QUERY_ROWS_CAP: usize = 200;
/// export_query_results hard cap (mirrors the bridge's kExportRowsMax).
const EXPORT_ROWS_CAP: usize = 100_000;

fn required_object<'a>(
    args: &'a Map<String, Value>,
    key: &str,
) -> Result<&'a Map<String, Value>, CallOutcome> {
    match args.get(key) {
        Some(Value::Object(o)) => Ok(o),
        Some(_) => Err(CallOutcome::InvalidParams(format!(
            "{key} must be an object"
        ))),
        None => Err(CallOutcome::InvalidParams(format!("{key} is required"))),
    }
}

fn run_query(transport: &mut dyn EditorTransport, args: &Map<String, Value>) -> CallOutcome {
    if let Err(e) = reject_extras(args, &["connection_name", "sql", "max_rows"]) {
        return e;
    }
    let connection_name = match required_str(args, "connection_name") {
        Ok(c) => c,
        Err(e) => return e,
    };
    let sql = match required_str(args, "sql") {
        Ok(s) => s,
        Err(e) => return e,
    };
    let max_rows = match args.get("max_rows") {
        None => None,
        Some(v) => match v.as_u64() {
            Some(n) if n >= 1 => Some((n as usize).min(RUN_QUERY_ROWS_CAP)),
            _ => return CallOutcome::InvalidParams("max_rows must be an integer >= 1".into()),
        },
    };
    to_outcome(transport.run_query(connection_name, sql, max_rows))
}

fn list_tables(transport: &mut dyn EditorTransport, args: &Map<String, Value>) -> CallOutcome {
    if let Err(e) = reject_extras(args, &["connection_name"]) {
        return e;
    }
    let connection_name = match required_str(args, "connection_name") {
        Ok(c) => c,
        Err(e) => return e,
    };
    to_outcome(transport.list_tables(connection_name))
}

fn render_chart(transport: &mut dyn EditorTransport, args: &Map<String, Value>) -> CallOutcome {
    if let Err(e) = reject_extras(args, &["spec", "title"]) {
        return e;
    }
    if let Err(e) = required_object(args, "spec") {
        return e;
    }
    let spec = args.get("spec").cloned().unwrap_or(Value::Null);
    let title = match optional_str(args, "title") {
        Ok(t) => t,
        Err(e) => return e,
    };
    to_outcome(transport.render_chart(&spec, title))
}

// Approval-gated in the editor; deny/timeout errors pass through verbatim.
fn export_query_results(
    transport: &mut dyn EditorTransport,
    args: &Map<String, Value>,
) -> CallOutcome {
    if let Err(e) = reject_extras(
        args,
        &["connection_name", "sql", "path", "format", "max_rows"],
    ) {
        return e;
    }
    let connection_name = match required_str(args, "connection_name") {
        Ok(c) => c,
        Err(e) => return e,
    };
    let sql = match required_str(args, "sql") {
        Ok(s) => s,
        Err(e) => return e,
    };
    let path = match required_str(args, "path") {
        Ok(p) => p,
        Err(e) => return e,
    };
    let format = match required_str(args, "format") {
        Ok(f) => f,
        Err(e) => return e,
    };
    if format != "csv" && format != "json" {
        return CallOutcome::InvalidParams("format must be \"csv\" or \"json\"".into());
    }
    let max_rows = match args.get("max_rows") {
        None => None,
        Some(v) => match v.as_u64() {
            Some(n) if n >= 1 => Some((n as usize).min(EXPORT_ROWS_CAP)),
            _ => return CallOutcome::InvalidParams("max_rows must be an integer >= 1".into()),
        },
    };
    to_outcome(transport.export_query_results(connection_name, sql, path, format, max_rows))
}

// Approval-gated in the editor; deny/timeout errors pass through verbatim.
fn export_chart(transport: &mut dyn EditorTransport, args: &Map<String, Value>) -> CallOutcome {
    if let Err(e) = reject_extras(args, &["spec", "path", "format", "scale"]) {
        return e;
    }
    if let Err(e) = required_object(args, "spec") {
        return e;
    }
    let spec = args.get("spec").cloned().unwrap_or(Value::Null);
    let path = match required_str(args, "path") {
        Ok(p) => p,
        Err(e) => return e,
    };
    let format = match required_str(args, "format") {
        Ok(f) => f,
        Err(e) => return e,
    };
    if !["png", "svg", "html", "spec"].contains(&format) {
        return CallOutcome::InvalidParams(
            "format must be \"png\", \"svg\", \"html\", or \"spec\"".into(),
        );
    }
    let scale = match args.get("scale") {
        None => None,
        Some(v) => match v.as_u64() {
            Some(n) if (1..=4).contains(&n) => Some(n as usize),
            _ => {
                return CallOutcome::InvalidParams(
                    "scale must be an integer between 1 and 4".into(),
                )
            }
        },
    };
    to_outcome(transport.export_chart(&spec, path, format, scale))
}
