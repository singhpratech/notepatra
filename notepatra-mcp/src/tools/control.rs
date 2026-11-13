// SPDX-License-Identifier: GPL-3.0-or-later
//! v0.1.133 Phase 3: git writes, Noter management, settings, data and REST.
//!
//! Same contract as editing.rs: validate here (a malformed call is -32602
//! before anything reaches the editor), then forward the same arguments to
//! the bridge verb of the same name. The bridge validates again, applies the
//! deny-lists, words the approval card and runs the app's own code.

use serde_json::{json, Map, Value};

use super::editing::{
    add, bad, check, forward, one_of, opt, req, tab_props, text_or_tab_props, tool, with, Arg,
    Kind, TabRule, TAB_SEL,
};
use super::{CallOutcome, APPROVAL_NOTE};
use crate::transport::EditorTransport;

const STASH_ACTIONS: &[&str] = &["push", "pop", "apply"];
const HUNK_ACTIONS: &[&str] = &["stage", "revert"];
const TAKES: &[&str] = &["ours", "theirs", "both"];
const EXPORT_FORMATS: &[&str] = &["markdown", "pdf"];
pub(super) const DRIVERS: &[&str] = &["QSQLITE", "QPSQL", "QMYSQL", "QODBC", "DUCKDB"];
pub(super) const METHODS: &[&str] = &["GET", "POST", "PUT", "PATCH", "DELETE", "HEAD", "OPTIONS"];
const MAX_I32: i64 = i32::MAX as i64;

const GIT_SCOPE: &str = "Runs in the git repository of the workspace folder only (set_workspace_folder or File > Open Folder sets it), with no terminal prompt and a timeout.";
const PATHS_DESC: &str = "Repository-relative (or absolute, inside the repository) file paths, 1 to 100. A path starting with '-', escaping the repository, inside .git or on the credential deny-list is refused.";
const NOTE_FILE_DESC: &str =
    "The note's file as list_notes / search_notes return it (absolute, or relative to the Noter folder)";

/// The Phase 3 tool definitions, appended to the existing list.
pub(super) fn definitions() -> Vec<Value> {
    let s = |d: &str| json!({ "type": "string", "description": d });
    let b = |d: &str| json!({ "type": "boolean", "description": d });
    let e = |list: &[&str], d: &str| json!({ "type": "string", "enum": list, "description": d });
    let int = |lo: i64, hi: i64, d: &str| json!({ "type": "integer", "minimum": lo, "maximum": hi, "description": d });
    let paths = json!({ "type": "array", "items": { "type": "string" }, "minItems": 1, "maxItems": 100, "description": PATHS_DESC });
    let none = Map::new;
    let one = |k: &str, v: Value| {
        let mut m = Map::new();
        m.insert(k.into(), v);
        m
    };
    let conn_fields = |m: Map<String, Value>| {
        let m = add(m, "driver", e(DRIVERS, "Qt SQL driver: QSQLITE, QPSQL (PostgreSQL), QMYSQL, QODBC (SQL Server and other ODBC sources) or DUCKDB (Full edition)"));
        let m = add(
            m,
            "host",
            s("Server host name or address (network drivers)"),
        );
        let m = add(
            m,
            "port",
            int(0, 65535, "Server port; 0 = the driver's default"),
        );
        let m = add(
            m,
            "database",
            s("Database name, or for QSQLITE / DUCKDB an absolute file path (or :memory:)"),
        );
        add(m, "username", s("User name for the server. There is no password field: the user types it in Manage Connections."))
    };
    vec![
        // ── git ──
        tool(
            "list_hunks",
            format!("List the changed hunks of one file in the working tree versus HEAD (the editor's change gutter), numbered from 0, each with its @@ header and diff text. Use before git_hunk. Credential-deny-listed files are refused. {GIT_SCOPE}"),
            one("path", s("File path, repository-relative or absolute")),
            &["path"],
        ),
        tool(
            "list_conflicts",
            format!("Merge conflicts. Without path: the conflicted files with how many <<<<<<< regions each has. With path: that file's regions, numbered from 0, with 1-based start/separator/end lines, the ours/theirs labels and both sides' text (read from the open tab when the file is open). Use before resolve_conflict. {GIT_SCOPE}"),
            one("path", s("File path, repository-relative or absolute (optional)")),
            &[],
        ),
        tool(
            "git_stage",
            format!("Stage files (git add -- <paths>) for the next commit. Each path must have changes in the working tree; credential files (keys, .env, keystores) are refused before any card. Returns {{staged}}. {GIT_SCOPE} {APPROVAL_NOTE}"),
            one("paths", paths.clone()),
            &["paths"],
        ),
        tool(
            "git_unstage",
            format!("Unstage files (git reset -- <paths>); the working copy keeps its changes. Each path must be staged. Returns {{unstaged}}. {GIT_SCOPE} {APPROVAL_NOTE}"),
            one("paths", paths.clone()),
            &["paths"],
        ),
        tool(
            "git_commit",
            format!("Commit the files that are ALREADY staged (stage them with git_stage first) with this message; hooks run normally and git's own error is returned if one fails. Refused when nothing is staged: this tool never commits unstaged changes (no commit -a). The card lists the staged files and the message; if the staged set changes before approval, nothing is committed. Returns {{commit, branch, files}}. {GIT_SCOPE} {APPROVAL_NOTE}"),
            one("message", s("The commit message")),
            &["message"],
        ),
        tool(
            "git_branch_create",
            format!("Create a local branch at the current commit; with switch:true also check it out (git checkout -b). The name must be a valid branch name not starting with '-' and must not exist yet. Returns {{branch, switched}}. {GIT_SCOPE} {APPROVAL_NOTE}"),
            add(one("name", s("New branch name")), "switch", b("Also switch to the new branch (default false)")),
            &["name"],
        ),
        tool(
            "git_checkout",
            format!("Switch to an existing local branch (git checkout <name> --, never forced). If local changes would be overwritten git refuses and its message is returned. Open tabs whose files change on disk go through the editor's usual reload prompt, which never drops unsaved edits. Returns {{branch}}. {GIT_SCOPE} {APPROVAL_NOTE}"),
            one("name", s("Local branch name (git_branch lists them)")),
            &["name"],
        ),
        tool(
            "git_stash",
            format!("Stash: push sets aside all changes including untracked files (git stash push -u, optional message); pop re-applies the latest stash and drops it if it applies cleanly; apply re-applies it and keeps it. Returns {{action, output}}. {GIT_SCOPE} {APPROVAL_NOTE}"),
            add(one("action", e(STASH_ACTIONS, "push, pop or apply")), "message", s("Stash message (push only)")),
            &["action"],
        ),
        tool(
            "git_discard",
            format!("DISCARD the uncommitted working-tree changes of tracked files (git checkout -- <paths>); each file goes back to its staged or last committed content, staged changes are kept. This loses data and the card says so. Untracked files are refused, never deleted (there is no git clean). Returns {{discarded}}. {GIT_SCOPE} {APPROVAL_NOTE}"),
            one("paths", paths),
            &["paths"],
        ),
        tool(
            "git_fetch",
            format!("Fetch from the remote of the current branch's configured upstream (network). No arguments: never another remote or refspec. git's error text is returned on failure; 60 s timeout. Returns {{ok, branch, output}}. {GIT_SCOPE} {APPROVAL_NOTE}"),
            none(),
            &[],
        ),
        tool(
            "git_pull",
            format!("Pull the current branch's configured upstream with --ff-only (network): refused by git when the branches have diverged, never a merge or rebase. 60 s timeout. Returns {{ok, branch, output}}. {GIT_SCOPE} {APPROVAL_NOTE}"),
            none(),
            &[],
        ),
        tool(
            "git_push",
            format!("Push the current branch to its configured upstream branch only (network); never forced, never another branch or remote. git's rejection (non-fast-forward, auth) is returned as the error; 60 s timeout. Returns {{ok, branch, output}}. {GIT_SCOPE} {APPROVAL_NOTE}"),
            none(),
            &[],
        ),
        tool(
            "git_hunk",
            format!("Stage one hunk into the index, or revert it in the working copy (data loss, said on the card), by its list_hunks index. If the file's hunks change before approval, nothing is applied. Returns {{path, hunk_index, action}}. {GIT_SCOPE} {APPROVAL_NOTE}"),
            {
                let m = one("path", s("File path, repository-relative or absolute"));
                let m = add(m, "hunk_index", int(0, 100000, "0-based hunk number from list_hunks"));
                add(m, "action", e(HUNK_ACTIONS, "stage or revert"))
            },
            &["path", "hunk_index", "action"],
        ),
        tool(
            "resolve_conflict",
            format!("Resolve one merge-conflict region by its list_conflicts index, keeping ours, theirs or both (ours then theirs), then save the file in its own encoding. The card shows the text that replaces the region. Refused when the file has unsaved edits in a tab, or when the regions change before approval. Returns {{path, remaining}}. {GIT_SCOPE} {APPROVAL_NOTE}"),
            {
                let m = one("path", s("File path, repository-relative or absolute"));
                let m = add(m, "region", int(0, 100000, "0-based region number from list_conflicts"));
                add(m, "take", e(TAKES, "ours, theirs or both"))
            },
            &["path", "region", "take"],
        ),
        // ── Noter ──
        tool(
            "search_notes",
            "Search the TEXT of every Noter note (not Trash) the way the Noter sidebar search does: every whitespace-separated term must appear, case-insensitive, in the title or body. Returns {results:[{file, title, snippet, modified_iso}], truncated}.".into(),
            add(one("query", s("Search terms")), "max_results", int(1, 100, "Most results to return (default 20)")),
            &["query"],
        ),
        tool(
            "rename_note",
            format!("Rename a Noter note: the title inside the note and the file name both change, like the sidebar's Rename (an open note follows the rename; its autosave keeps saving to the new file). Returns {{file, title}}. {APPROVAL_NOTE}"),
            add(one("file", s(NOTE_FILE_DESC)), "new_title", s("The new title (up to 200 characters)")),
            &["file", "new_title"],
        ),
        tool(
            "trash_note",
            format!("Move a Noter note (and its backups) to the Noter Trash, like the sidebar's Move to Trash; restore_note brings it back. Returns {{file}}: its path in Trash. {APPROVAL_NOTE}"),
            one("file", s(NOTE_FILE_DESC)),
            &["file"],
        ),
        tool(
            "restore_note",
            format!("Restore a note from the Noter Trash to the Inbox. file is a trashed note's path (trash_note returns it; list_notes with include_trash lists them). Returns {{file}}. {APPROVAL_NOTE}"),
            one("file", s("The trashed note's path")),
            &["file"],
        ),
        tool(
            "delete_note_permanently",
            format!("Permanently delete a note that is already in the Noter Trash (only there, as in the UI), with its backup copies. This CANNOT be undone and the card says so. Returns {{deleted}}. {APPROVAL_NOTE}"),
            one("file", s("The trashed note's path")),
            &["file"],
        ),
        tool(
            "delete_reminder",
            format!("Delete a Noter reminder by its id from list_reminders. Returns {{id, deleted}}. {APPROVAL_NOTE}"),
            one("id", s("Reminder id from list_reminders")),
            &["id"],
        ),
        tool(
            "snooze_reminder",
            format!("Snooze a Noter reminder for minutes (default 10) from now. Returns {{id, snoozed_until_iso}}. {APPROVAL_NOTE}"),
            add(one("id", s("Reminder id from list_reminders")), "minutes", int(1, 10080, "Minutes to snooze (default 10, at most a week)")),
            &["id"],
        ),
        tool(
            "dismiss_reminder",
            format!("Dismiss a Noter reminder so it does not fire again. Returns {{id, dismissed}}. {APPROVAL_NOTE}"),
            one("id", s("Reminder id from list_reminders")),
            &["id"],
        ),
        tool(
            "export_note",
            format!("Export a Noter note to a file: markdown (images go to a <path>.assets folder) or pdf. pdf exists only where this build has the renderer (Full edition); otherwise the call says so. path must be absolute with an existing parent folder; protected destinations (shell start-up files, git hooks, credential stores) are refused and an existing file is flagged as OVERWRITE on the card. Returns {{path}}. {APPROVAL_NOTE}"),
            {
                let m = one("file", s(NOTE_FILE_DESC));
                let m = add(m, "format", e(EXPORT_FORMATS, "markdown or pdf"));
                add(m, "path", s("Absolute destination file path"))
            },
            &["file", "format", "path"],
        ),
        // ── settings ──
        tool(
            "get_preferences",
            "Read the editor and view preferences an MCP client may change (font, tabs, wrap, margins, theme, autosave interval and the like) with each key's type and allowed range. AI, cloud, network, plugin, update and history settings are never included.".into(),
            none(),
            &[],
        ),
        tool(
            "set_preference",
            format!("Change one preference from the get_preferences allow-list; it is saved and applied to every open editor at once, as Preferences > OK does. The value is checked against the key's type and range. Any other key (AI or cloud keys and URLs, update, plugin or network settings, recent files) is refused. Tab width and spaces-vs-tabs are the tab_width and use_spaces keys. Returns {{key, value, changed}}. {APPROVAL_NOTE}"),
            add(one("key", s("A key from get_preferences")), "value", json!({ "description": "New value: boolean, integer or string as the key's type says" })),
            &["key", "value"],
        ),
        tool(
            "set_workspace_folder",
            format!("Make a folder the workspace (File > Open Folder as Workspace). This widens what the read tools (search_project, git tools, csv_path) can see, so the filesystem root, your home folder itself or any folder containing it, credential and settings folders (~/.ssh, ~/.gnupg, ~/.config, ...) and system folders are refused. The path is canonicalised first. Returns {{workspace}}. {APPROVAL_NOTE}"),
            one("path", s("Absolute path of an existing folder")),
            &["path"],
        ),
        // ── data ──
        tool(
            "describe_table",
            "Describe one table of a saved connection through the schema browser's introspection: {schema, table, type, columns:[{name, type, primary_key?}], keys_known, primary_key?}. Primary keys are reported only where the catalog says (SQLite); keys_known tells you. table may be schema.table.".into(),
            add(one("connection_name", s("Saved connection name (list_connections)")), "table", s("Table name, or schema.table")),
            &["connection_name", "table"],
        ),
        tool(
            "test_connection",
            "Try to open a saved connection with its stored settings and report {ok, error?}; the error text never contains the password or key=value secrets. This opens a real, authenticated connection to the database server (an action, not a read: remote clients need the read_act scope). A SQLite/DuckDB file that does not exist is reported, never created. 20 s timeout.".into(),
            one("connection_name", s("Saved connection name (list_connections)")),
            &["connection_name"],
        ),
        tool(
            "execute_sql",
            format!("Run ONE statement that changes data (INSERT, UPDATE, DELETE, CREATE TABLE, ...) on a saved connection. The editor's SQL gate classifies it first: stacked statements are refused, read-only queries are refused (use run_query), and statements the gate refuses outright are refused even with approval: ambiguous comments, U& escapes, file/URL/network reads, side-effecting functions, xp_cmdshell-style procedures, COPY, ATTACH, LOAD, SET, transaction control and CREATE FUNCTION/EXTENSION and the like. The card shows the connection, its driver/database/host and the whole statement. 60 s timeout. Returns {{ok, executed, columns?, rows?}}. {APPROVAL_NOTE}"),
            add(one("connection_name", s("Saved connection name (list_connections)")), "sql", s("Exactly one SQL statement")),
            &["connection_name", "sql"],
        ),
        tool(
            "add_connection",
            format!("Save a new database connection WITHOUT any password, token or options string: those are never accepted or returned over MCP. If the database needs a password, the user types it in Manage Connections (open_connections_dialog). File databases must be an absolute path off the credential deny-list. Returns the saved fields and a note. {APPROVAL_NOTE}"),
            conn_fields(one("name", s("Unique connection name"))),
            &["name", "driver", "database"],
        ),
        tool(
            "update_connection",
            format!("Change fields of a saved connection (never its password or options). Changing the driver, host, port or user name CLEARS the stored password and options, so a stored secret can never be sent to a different server; the card says so. Returns the saved fields. {APPROVAL_NOTE}"),
            conn_fields(add(one("name", s("The connection to change")), "new_name", s("Rename it to this"))),
            &["name"],
        ),
        tool(
            "delete_connection",
            format!("Delete a saved database connection (the database itself is untouched). Returns {{deleted}}. {APPROVAL_NOTE}"),
            one("name", s("The connection to delete")),
            &["name"],
        ),
        tool(
            "open_connections_dialog",
            "Open the Manage Connections window (non-modal), where the user can add passwords and test connections. Returns {opened}.".into(),
            none(),
            &[],
        ),
        tool(
            "open_schema_browser",
            "Open the database schema browser window (non-modal) over the saved connections. Returns {opened}.".into(),
            none(),
            &[],
        ),
        // ── REST ──
        tool(
            "parse_http_file",
            "Parse .http / .rest text (or an open tab) the way the REST client does: {requests:[{index, name, method, url, line, start_line, end_line, header_names, body_chars, variables}], file_variables}. URLs are returned with {{variables}} NOT expanded; header values are not returned. line is the 1-based line to pass to rest_request.".into(),
            text_or_tab_props(),
            &[],
        ),
        tool(
            "curl_to_request",
            "Convert a curl command line into {method, url, headers:[{name, value}], body, notes} with the REST client's importer (notes lists options it ignored). Nothing is sent.".into(),
            one("text", s("The curl command")),
            &["text"],
        ),
        tool(
            "request_to_curl",
            "Build a curl command line from a request, as the REST client's Copy as cURL does. Returns {text}. Nothing is sent.".into(),
            {
                let m = one("method", e(METHODS, "HTTP method"));
                let m = add(m, "url", s("Request URL"));
                let m = add(m, "headers", json!({ "type": "object", "additionalProperties": { "type": "string" }, "description": "Header name to value" }));
                add(m, "body", s("Request body"))
            },
            &["method", "url"],
        ),
        tool(
            "rest_request",
            format!("Send one HTTP request through the REST client: either method + url (+ headers, body), or a .http tab (tab_id / tab_index) and the 1-based line of a request in it, whose {{{{variables}}}} are resolved with the file's variables and the environment remembered for its folder. The request is resolved ONCE, when the card is built, and exactly that request is sent on Approve (never re-resolved). http and https only; loopback and private addresses are allowed (local development) and the card shows the resolved host, the path, header NAMES, the body size and the environment. The REST Client tab shows the response; its editable request fields are not changed. Redirects are not followed; 30 s timeout; recorded in the REST history unexpanded, with secret headers, URL user-info, token-like query values and secret-named JSON/form body fields as <redacted> (other bodies kept only as small text). Returns {{status, reason, headers, content_type, size, body, truncated, binary, time_ms}}; the body is capped at 256 KB and binary bodies are summarised. {APPROVAL_NOTE}"),
            {
                let mut m = tab_props(false);
                m.insert("line".into(), int(1, MAX_I32, "1-based line inside the request block (with a tab)"));
                m.insert("method".into(), e(METHODS, "HTTP method (without a tab)"));
                m.insert("url".into(), s("http:// or https:// URL (without a tab)"));
                m.insert("headers".into(), json!({ "type": "object", "additionalProperties": { "type": "string" }, "description": "Header name to value (without a tab)" }));
                m.insert("body".into(), s("Request body (without a tab)"));
                m
            },
            &[],
        ),
        // ── leftovers ──
        tool(
            "hex_dump",
            "Hex dump of bytes: an open tab's file on disk (its text as UTF-8 when untitled), or a file inside the workspace folder by path (credential files and paths outside the workspace are refused). At most 65536 bytes per call from offset. Returns {offset, length, total_size, source, hex, truncated} with 16 bytes per line, xxd style.".into(),
            {
                let mut m = tab_props(false);
                m.insert("path".into(), s("A file inside the workspace folder (instead of a tab)"));
                m.insert("offset".into(), int(0, MAX_I32, "Byte offset to start at (default 0)"));
                m.insert("length".into(), int(1, 65536, "Bytes to dump (default 4096, at most 65536)"));
                m
            },
            &[],
        ),
        tool(
            "clear_recent_files",
            format!("Clear the File > Recent Files list (the files themselves are not touched). Returns {{cleared}}: how many entries were removed. {APPROVAL_NOTE}"),
            none(),
            &[],
        ),
    ]
}

// ── handlers ────────────────────────────────────────────────────────────

fn fwd(
    t: &mut dyn EditorTransport,
    verb: &str,
    a: &Map<String, Value>,
    spec: &[Arg],
) -> CallOutcome {
    forward(t, verb, a, spec, TabRule::None)
}

fn strings_nonblank(a: &Map<String, Value>, keys: &[&str]) -> Result<(), CallOutcome> {
    for k in keys {
        if let Some(v) = a.get(*k).and_then(Value::as_str) {
            if v.trim().is_empty() {
                return Err(bad(format!("{k} must not be empty")));
            }
        }
    }
    Ok(())
}

/// Every Phase 3 tool, dispatched by name (tools.rs lists each one).
pub(super) fn call(t: &mut dyn EditorTransport, name: &str, a: &Map<String, Value>) -> CallOutcome {
    use Kind::*;
    if let Err(e) = strings_nonblank(
        a,
        &[
            "path",
            "name",
            "message",
            "file",
            "new_title",
            "id",
            "query",
            "key",
            "connection_name",
            "table",
            "sql",
            "url",
            "database",
        ],
    ) {
        return e;
    }
    match name {
        "list_hunks" => fwd(t, name, a, &[req("path", Str)]),
        "list_conflicts" => fwd(t, name, a, &[opt("path", Str)]),
        "git_stage" | "git_unstage" | "git_discard" => fwd(t, name, a, &[req("paths", StrArray)]),
        "git_commit" => fwd(t, name, a, &[req("message", Str)]),
        "git_branch_create" => fwd(t, name, a, &[req("name", Str), opt("switch", Bool)]),
        "git_checkout" => fwd(t, name, a, &[req("name", Str)]),
        "git_stash" => {
            if a.contains_key("message") && a.get("action").and_then(Value::as_str) != Some("push")
            {
                return bad("message is only for action push".into());
            }
            fwd(
                t,
                name,
                a,
                &[req("action", Enum(STASH_ACTIONS)), opt("message", Str)],
            )
        }
        "git_fetch" | "git_pull" | "git_push" => fwd(t, name, a, &[]),
        "git_hunk" => fwd(
            t,
            name,
            a,
            &[
                req("path", Str),
                req("hunk_index", Int(0, 100000)),
                req("action", Enum(HUNK_ACTIONS)),
            ],
        ),
        "resolve_conflict" => fwd(
            t,
            name,
            a,
            &[
                req("path", Str),
                req("region", Int(0, 100000)),
                req("take", Enum(TAKES)),
            ],
        ),
        "search_notes" => fwd(
            t,
            name,
            a,
            &[req("query", Str), opt("max_results", Int(1, 100))],
        ),
        "rename_note" => fwd(t, name, a, &[req("file", Str), req("new_title", Str)]),
        "trash_note" | "restore_note" | "delete_note_permanently" => {
            fwd(t, name, a, &[req("file", Str)])
        }
        "delete_reminder" | "dismiss_reminder" => fwd(t, name, a, &[req("id", Str)]),
        "snooze_reminder" => fwd(t, name, a, &[req("id", Str), opt("minutes", Int(1, 10080))]),
        "export_note" => {
            if let Some(p) = a.get("path").and_then(Value::as_str) {
                if !is_absolute(p) {
                    return bad("path must be absolute".into());
                }
            }
            fwd(
                t,
                name,
                a,
                &[
                    req("file", Str),
                    req("format", Enum(EXPORT_FORMATS)),
                    req("path", Str),
                ],
            )
        }
        "get_preferences" => fwd(t, name, a, &[]),
        "set_preference" => {
            if a.get("value")
                .is_some_and(|v| v.is_null() || v.is_array() || v.is_object())
            {
                return bad("value must be a boolean, integer or string".into());
            }
            fwd(t, name, a, &[req("key", Str), req("value", Any)])
        }
        "set_workspace_folder" => {
            if let Some(p) = a.get("path").and_then(Value::as_str) {
                if !is_absolute(p) {
                    return bad("path must be absolute".into());
                }
            }
            fwd(t, name, a, &[req("path", Str)])
        }
        "describe_table" => fwd(
            t,
            name,
            a,
            &[req("connection_name", Str), req("table", Str)],
        ),
        "test_connection" => fwd(t, name, a, &[req("connection_name", Str)]),
        "execute_sql" => fwd(t, name, a, &[req("connection_name", Str), req("sql", Str)]),
        "add_connection" | "update_connection" => {
            let mut spec = vec![
                req("name", Str),
                opt("driver", Enum(DRIVERS)),
                opt("host", Str),
                opt("port", Int(0, 65535)),
                opt("database", Str),
                opt("username", Str),
            ];
            if name == "add_connection" {
                spec[1] = req("driver", Enum(DRIVERS));
                spec[4] = req("database", Str);
            } else {
                spec.push(opt("new_name", Str));
                if a.len() < 2 {
                    return bad("give at least one field to change".into());
                }
            }
            fwd(t, name, a, &spec)
        }
        "delete_connection" => fwd(t, name, a, &[req("name", Str)]),
        "open_connections_dialog" | "open_schema_browser" => fwd(t, name, a, &[]),
        "parse_http_file" => {
            let spec = with(&TAB_SEL, [opt("text", Str)]);
            forward(t, name, a, &spec, TabRule::TextOrTab)
        }
        "curl_to_request" => fwd(t, name, a, &[req("text", Str)]),
        "request_to_curl" => {
            if let Err(e) = headers_ok(a) {
                return e;
            }
            fwd(
                t,
                name,
                a,
                &[
                    req("method", Enum(METHODS)),
                    req("url", Str),
                    opt("headers", Object),
                    opt("body", Str),
                ],
            )
        }
        "rest_request" => rest_request(t, a),
        "hex_dump" => {
            let spec = with(
                &TAB_SEL,
                [
                    opt("path", Str),
                    opt("offset", Int(0, MAX_I32)),
                    opt("length", Int(1, 65536)),
                ],
            );
            if let Err(e) =
                check(a, &spec).and_then(|()| one_of(a, &["tab_id", "tab_index", "path"], true))
            {
                return e;
            }
            forward(t, name, a, &spec, TabRule::None)
        }
        "clear_recent_files" => fwd(t, name, a, &[]),
        other => CallOutcome::UnknownTool(other.to_string()),
    }
}

fn is_absolute(p: &str) -> bool {
    p.starts_with('/') || p.starts_with('\\') || (p.len() > 2 && p.as_bytes()[1] == b':')
}

fn headers_ok(a: &Map<String, Value>) -> Result<(), CallOutcome> {
    if let Some(Value::Object(h)) = a.get("headers") {
        for (k, v) in h {
            let name_ok = !k.is_empty()
                && k.chars()
                    .all(|c| c.is_ascii_alphanumeric() || "!#$%&'*+.^_`|~-".contains(c));
            if !name_ok {
                return Err(bad(format!("invalid header name: {k:?}")));
            }
            match v.as_str() {
                Some(s) if !s.contains('\n') && !s.contains('\r') => {}
                _ => return Err(bad(format!("header {k} must be a single-line string"))),
            }
        }
    }
    Ok(())
}

fn rest_request(t: &mut dyn EditorTransport, a: &Map<String, Value>) -> CallOutcome {
    let spec: Vec<Arg> = with(
        &TAB_SEL,
        [
            opt("line", Kind::Int(1, MAX_I32)),
            opt("method", Kind::Enum(METHODS)),
            opt("url", Kind::Str),
            opt("headers", Kind::Object),
            opt("body", Kind::Str),
        ],
    );
    if let Err(e) = check(a, &spec) {
        return e;
    }
    let tab = a.contains_key("tab_id") || a.contains_key("tab_index");
    let manual = ["method", "url", "headers", "body"]
        .iter()
        .any(|k| a.contains_key(*k));
    if tab && manual {
        return bad(
            "give either method + url (and headers / body) or a .http tab and line, not both"
                .into(),
        );
    }
    if tab {
        if let Err(e) = one_of(a, &["tab_id", "tab_index"], true) {
            return e;
        }
        if !a.contains_key("line") {
            return bad("line is required with a tab".into());
        }
    } else {
        if a.contains_key("line") {
            return bad("line needs a tab (tab_id or tab_index)".into());
        }
        if !a.contains_key("method") || !a.contains_key("url") {
            return bad("give method and url, or a .http tab and line".into());
        }
        if let Some(u) = a.get("url").and_then(Value::as_str) {
            let lower = u.trim().to_ascii_lowercase();
            if lower.contains("://")
                && !lower.starts_with("http://")
                && !lower.starts_with("https://")
            {
                return bad("only http:// and https:// URLs can be sent".into());
            }
        }
        if let Err(e) = headers_ok(a) {
            return e;
        }
    }
    forward(t, "rest_request", a, &spec, TabRule::None)
}
