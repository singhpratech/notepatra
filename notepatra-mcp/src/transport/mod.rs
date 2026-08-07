// SPDX-License-Identifier: GPL-3.0-or-later
use std::fmt;

use serde_json::Value;

pub mod endpoint;
pub mod mock;
pub mod socket;

/// One entry of `list_open_tabs` — wire shape
/// `{index,title,path,modified,editable}`. The editor always sends `path` as a
/// string; `""` (untitled tab) maps to `None` here so tool output can omit it.
/// `editable` (v0.1.121) is `false` for tabs that are not editable text
/// buffers (Welcome page, Diagram canvas, Noter panel); older editors omit it,
/// so it defaults to `true` on the wire (see socket `tab_info_from`).
#[derive(Debug, Clone, serde::Serialize)]
pub struct TabInfo {
    pub index: usize,
    pub title: String,
    #[serde(skip_serializing_if = "Option::is_none")]
    pub path: Option<String>,
    pub modified: bool,
    pub editable: bool,
    /// Stable tab id (v0.1.126, NP-13). `None` against an editor older than
    /// 0.1.126, which published no id at all — callers fall back to `index`.
    #[serde(skip_serializing_if = "Option::is_none")]
    pub id: Option<i64>,
}

/// `read_tab` result — wire shape `{title,path,text}` plus `truncated:true`
/// only when the editor capped the text (absent otherwise).
#[derive(Debug, Clone, serde::Serialize)]
pub struct TabContent {
    pub title: String,
    #[serde(skip_serializing_if = "Option::is_none")]
    pub path: Option<String>,
    pub text: String,
    pub truncated: bool,
    /// Full length of the buffer before any cap, so a truncated caller knows
    /// how much it did not get (v0.1.126, NP-10).
    pub total_chars: usize,
    #[serde(skip_serializing_if = "Option::is_none")]
    pub id: Option<i64>,
}

/// Marker appended to tab text when the editor capped the read (the bridge
/// truncates `read_tab` at 5 MB).
pub const TRUNCATION_MARKER: &str = "\n[truncated at 5 MB]";

impl TabContent {
    /// Tab text with [`TRUNCATION_MARKER`] appended when the editor capped
    /// the read. Used by both the `read_tab` tool and `resources/read` so a
    /// client always sees that content is incomplete.
    pub fn text_with_marker(&self) -> String {
        if self.truncated {
            format!("{}{}", self.text, TRUNCATION_MARKER)
        } else {
            self.text.clone()
        }
    }
}

/// One entry of `search_project` results — wire shape `{path,line,text}`.
/// `path` is `""` for hits in untitled tab buffers.
#[derive(Debug, Clone, serde::Serialize)]
pub struct SearchHit {
    pub path: String,
    /// 1-based line number.
    pub line: usize,
    pub text: String,
}

/// `search_project` result — wire shape
/// `{results:[SearchHit],truncated,workspace_searched,scope,notice?}`.
///
/// v0.1.126 (NP-07): the last three used to be DROPPED here. The C++ bridge
/// has sent `workspace_searched` and `scope` since v0.1.125 — the changelog
/// promised them and the editor delivered them — but this struct declared only
/// two fields, so serde discarded the rest before the tool result was built
/// and no client ever saw them. Two layers, one contract, and only one of them
/// was updated.
#[derive(Debug, Clone, serde::Serialize)]
pub struct SearchResults {
    pub results: Vec<SearchHit>,
    pub truncated: bool,
    /// False means the workspace was never walked — open tabs only.
    pub workspace_searched: bool,
    /// `"tabs_and_workspace"` or `"open_tabs_only"`.
    pub scope: String,
    /// Why the scope was reduced, when it was. Zero results is a SUCCESS with
    /// a notice, never an error (NP-08).
    #[serde(skip_serializing_if = "Option::is_none")]
    pub notice: Option<String>,
}

/// `get_selection` result — wire shape `{text,tab_index}`. The editor sends
/// `tab_index: -1` when it cannot attribute the selection to a tab.
#[derive(Debug, Clone, serde::Serialize)]
pub struct Selection {
    pub tab_index: i64,
    pub text: String,
}

#[derive(Debug, Clone, Copy)]
pub enum TabSelector<'a> {
    Index(usize),
    Title(&'a str),
    /// Stable id from `list_open_tabs` (v0.1.126, NP-13).
    Id(i64),
}

/// Which tab a verb acts on, for the verbs that take an OPTIONAL target
/// (absent = the focused tab).
///
/// v0.1.126 (NP-13). Every write verb used to address its target by
/// `tab_index` alone. That index is positional: close a tab to its left and it
/// silently re-points at a different document, so an assistant that read
/// `list_open_tabs` and wrote a few seconds later could land its edit in the
/// wrong file — and out-of-range was the LUCKY case, because a
/// shifted-but-valid index wrote silently. `id` survives the shift.
///
/// Both fields unset means "the focused tab", which is the historical default.
#[derive(Debug, Clone, Copy, Default, PartialEq, Eq)]
pub struct TabRef {
    pub index: Option<usize>,
    pub id: Option<i64>,
}

impl TabRef {
    pub fn index(i: usize) -> Self {
        Self {
            index: Some(i),
            id: None,
        }
    }

    pub fn id(id: i64) -> Self {
        Self {
            index: None,
            id: Some(id),
        }
    }

    pub fn is_unset(&self) -> bool {
        self.index.is_none() && self.id.is_none()
    }

    /// Write the selector into a bridge args object. `index_key` is `"index"`
    /// for the read verbs and `"tab_index"` for the write verbs — two
    /// spellings that already shipped, so both stay. `tab_id` wins when set,
    /// matching McpBridge::resolveTab's precedence exactly.
    pub fn apply(&self, args: &mut Value, index_key: &str) {
        if let Some(id) = self.id {
            args["tab_id"] = Value::from(id);
        } else if let Some(i) = self.index {
            args[index_key] = Value::from(i);
        }
    }
}

impl From<Option<usize>> for TabRef {
    fn from(index: Option<usize>) -> Self {
        Self { index, id: None }
    }
}

/// Editor-side failure; surfaces to MCP clients as an `isError: true` tool
/// result (per spec, tool execution errors are results, not protocol errors).
#[derive(Debug)]
pub struct TransportError(pub String);

impl fmt::Display for TransportError {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        f.write_str(&self.0)
    }
}

impl std::error::Error for TransportError {}

/// Editor surface exposed over the MCP bridge. Result shapes mirror
/// src/mcp_bridge.cpp EXACTLY — the C++ bridge is the source of truth.
///
/// Wave-1 methods return typed structs (see their definitions above for the
/// wire shapes); `open_file` returns the new tab index from the editor's
/// `{opened,tab_index}` result. Wave-2 methods return the editor's JSON
/// result verbatim:
///
/// * `get_status` → `{tab_index,title,path,language,encoding,cursor_line,cursor_col,edition,version}`
///   (`tab_index` is -1 when no tab is active)
/// * `app_info` → `{name,version,edition,platform}`
/// * `list_recent_files` → `{files:[str]}`
/// * `find_in_tab` → `{matches:[{line,text}],truncated}`
/// * `new_tab` → `{tab_index}`
/// * `goto_line` → `{ok,tab_index,line,requested_line,clamped}` — `line` is
///   where the cursor LANDED, not what was asked for: a line past end-of-file
///   clamps to the last line and sets `clamped:true`. Check it before issuing a
///   cursor-relative write.
/// * `set_language` → `{ok,tab_index,language}`
/// * `compare_tabs` → `{opened}`
/// * `format_text` → `{text}`
/// * `list_notes` → `{notes:[{title,file,modified_iso}]}` (`file` is the
///   note's absolute path, native separators)
/// * `read_note` → `{title,text}`
///
/// Write verbs (v0.1.118) block inside the editor on a human approval card
/// (up to 120 s) before touching the buffer; a denial or timeout surfaces as
/// the VERBATIM editor errors `"denied by user"` / `"approval timed out"`:
///
/// * `insert_text` → `{ok:true,tab_index:N}`
/// * `replace_selection` → `{ok:true}`
/// * `apply_edit` → `{ok:true,count:N}`
/// * `save_tab` → `{saved:true,tab_index:N,path:str}` (the editor bridge; the
///   mock returns `{ok:true}`). An optional `path` "Save As"es to a new file.
///
/// v0.1.119 ("MCP depth") adds 13 verbs. All pass the editor's JSON result
/// through verbatim. Shapes below are BYTE-EXACT from the C++ bridge doc
/// comment in src/mcp_bridge.cpp (the single source of truth, reconciled
/// 2026-07-17):
///
/// Read tier:
/// * `list_reminders` → `{reminders:[{note_file,note_title,due_iso,bucket}]}`
///   where `bucket` is one of `"Overdue"|"Today"|"This week"|"Later"` (the
///   bridge computes the bucket against LOCAL time; capitalization exact)
/// * `git_status` / `git_diff` (args `{path?}`) / `git_log`
///   (args `{limit?}`, default 20, capped at 100) / `git_show` (args `{ref}`)
///   / `git_branch` — each returns `{output:str}` (raw `git` CLI text)
/// * `validate_npd` (args `{tab_index}` XOR `{source}` — the bridge keeps the
///   `tab_index` key here, NOT `index`) → `{valid:bool,errors:[{line,message}]}`
/// * `run_sql` (args `{sql,csv_path?}`, SELECT-only — the editor rejects any
///   non-read statement) → `{columns:[str],rows:[[…]],truncated:bool,engine:str}`
/// * `list_languages` → `{languages:[str]}` (canonical Language-menu tokens)   (p0a)
/// * `get_capabilities` → `{edition,platform,version,features:{duckdb,webengine,noter}}`
///   (p0a; the TOOL layer augments the result with `tool_count` and `tiers`
///   before it reaches the client — the sidecar owns the tool surface)
///
/// Act tier:
/// * `open_note` (args `{file}`) → `{opened:bool,title:str}`
///
/// Write tier (same human-approval card + verbatim deny/timeout errors as the
/// v0.1.118 write verbs):
/// * `create_note` (args `{title,body}`) → `{file:str,title:str}`
/// * `append_note` (args `{file,text}`) → `{file:str}`
/// * `set_reminder` (args `{file,due_iso}`) → `{file:str,due_iso:str}`
/// * `export_diagram` (args `{tab_index,path,format}`) → `{path:str}`
///
/// Phase 1 (diagram control + Noter panel):
/// * `create_diagram` (args `{source?,title?}`) → `{tab_index,valid,errors:[{line,message}]}`
///   (ACT — the tab is created even when the source is invalid .npd)
/// * `get_diagram_source` (args `{tab_index}`) → `{source}` (READ; errors when
///   the tab is not a DiagramEditor)
/// * `open_noter` → `{opened:true}` (ACT — reveals/focuses the Noter tab)
/// * `set_diagram_source` (args `{tab_index,source}`) →
///   `{ok:true,tab_index,valid,errors}` (WRITE — same approval card + verbatim
///   deny/timeout errors as the other write verbs)
///
/// Phase 2 (data-analyst + charts) — verbatim JSON passthrough:
///
/// Read tier:
/// * `list_connections` → `{connections:[{name,driver,database,read_only}]}`
///   (never passwords)
/// * `run_query` (args `{connection_name,sql,max_rows?}`) →
///   `{columns:[str],rows:[[…]],truncated:bool,engine:str}` — SELECT-only over
///   a SAVED connection; mutations are rejected by the editor
/// * `list_tables` (args `{connection_name}`) → `{tables:[str]}`
///
/// Act tier:
/// * `open_data_analyst` → `{opened:true}`
/// * `render_chart` (args `{spec,title?}`) → `{chart_id:str,rendered:bool}`;
///   Lite editions error with `"charts require the Full edition (WebEngine)"`
///
/// Write tier (same human-approval card + verbatim deny/timeout errors):
/// * `export_query_results` (args `{connection_name,sql,path,format,max_rows?}`)
///   → `{ok:true,path:str,rows:int}`
/// * `export_chart` (args `{spec,path,format,scale?}`) → `{path:str}`;
///   Full/WebEngine only (same gate error as render_chart)
pub trait EditorTransport {
    /// True when this transport FABRICATES its answers instead of talking to a
    /// running editor (v0.1.126, NP-06).
    ///
    /// `notepatra-mcp` with no `--socket` serves an in-memory demo editor. That
    /// is deliberate — it lets any MCP client exercise the protocol with
    /// nothing installed — but nothing said so ON THE WIRE. `--help` warned
    /// humans; the consumer is a language model that never reads `--help`. So
    /// a config with a dropped `--socket` produced an assistant confidently
    /// discussing three files that do not exist, and the failure looked
    /// exactly like success. Every tool result now carries a marker.
    fn is_mock(&self) -> bool {
        false
    }

    /// Returns the tab index the file landed in.
    fn open_file(&mut self, path: &str) -> Result<usize, TransportError>;
    fn list_open_tabs(&self) -> Result<Vec<TabInfo>, TransportError>;
    /// `max_bytes` caps the returned text below the editor's own 5 MB ceiling
    /// (v0.1.126, NP-10).
    fn read_tab(
        &self,
        selector: TabSelector<'_>,
        max_bytes: Option<usize>,
    ) -> Result<TabContent, TransportError>;
    fn search_project(
        &self,
        query: &str,
        max_results: usize,
        regex: bool,
    ) -> Result<SearchResults, TransportError>;
    fn get_selection(&self) -> Result<Selection, TransportError>;

    // Wave-2 verbs (v0.1.118).
    fn get_status(&self) -> Result<Value, TransportError>;
    fn app_info(&self) -> Result<Value, TransportError>;
    fn list_recent_files(&self) -> Result<Value, TransportError>;
    /// `title` selects the tab by name, as `read_tab` has always allowed
    /// (v0.1.126, NP-11).
    fn find_in_tab(
        &self,
        tab: TabRef,
        title: Option<&str>,
        query: &str,
        regex: bool,
    ) -> Result<Value, TransportError>;
    fn new_tab(&mut self, text: Option<&str>) -> Result<Value, TransportError>;
    fn goto_line(&mut self, line: usize, tab: TabRef) -> Result<Value, TransportError>;
    /// v0.1.121 (issue #5): move the selection to a 1-based line/column range
    /// (ACT — no approval card). Result `{ok:true,tab_index}`.
    fn select_range(
        &mut self,
        tab: TabRef,
        start_line: usize,
        start_col: usize,
        end_line: usize,
        end_col: usize,
    ) -> Result<Value, TransportError>;
    fn set_language(&mut self, language: &str, tab: TabRef) -> Result<Value, TransportError>;
    fn compare_tabs(&mut self, index_a: usize, index_b: usize) -> Result<Value, TransportError>;
    fn format_text(&self, kind: &str, text: &str) -> Result<Value, TransportError>;
    fn list_notes(&self) -> Result<Value, TransportError>;
    fn read_note(&self, file: &str) -> Result<Value, TransportError>;

    // Write verbs (v0.1.118) — human-approval-gated in the editor.
    fn insert_text(
        &mut self,
        text: &str,
        tab: TabRef,
        line: Option<usize>,
        col: Option<usize>,
    ) -> Result<Value, TransportError>;
    fn replace_selection(&mut self, text: &str, tab: TabRef) -> Result<Value, TransportError>;
    fn apply_edit(
        &mut self,
        find: &str,
        replace: &str,
        tab: TabRef,
        all: bool,
    ) -> Result<Value, TransportError>;
    /// `path` (v0.1.121) turns this into a "Save As" to a NEW absolute path;
    /// the editor validates it (absolute, parent folder exists) before showing
    /// the approval card. `None` saves the tab to its existing file.
    fn save_tab(&mut self, tab: TabRef, path: Option<&str>) -> Result<Value, TransportError>;

    // v0.1.119 read verbs — verbatim JSON passthrough (shapes documented on
    // the trait doc comment above; the C++ bridge is the source of truth).
    fn list_reminders(&self) -> Result<Value, TransportError>;
    fn git_status(&self) -> Result<Value, TransportError>;
    fn git_diff(&self, path: Option<&str>) -> Result<Value, TransportError>;
    fn git_log(&self, limit: usize) -> Result<Value, TransportError>;
    fn git_show(&self, git_ref: &str) -> Result<Value, TransportError>;
    fn git_branch(&self) -> Result<Value, TransportError>;
    /// Exactly one of `tab_index` / `source` is `Some` (enforced by the tool
    /// layer); the bridge validates a .npd document and reports parse errors.
    fn validate_npd(&self, tab: TabRef, source: Option<&str>) -> Result<Value, TransportError>;
    /// SELECT-only: the editor rejects any non-read statement with a verbatim
    /// error that passes straight through.
    fn run_sql(&self, sql: &str, csv_path: Option<&str>) -> Result<Value, TransportError>;

    // Phase 0A read verbs — verbatim JSON passthrough.
    fn list_languages(&self) -> Result<Value, TransportError>;
    fn get_capabilities(&self) -> Result<Value, TransportError>;

    // v0.1.119 act verb — opens a Noter note in a tab (not approval-gated).
    fn open_note(&mut self, file: &str) -> Result<Value, TransportError>;

    // v0.1.119 write verbs — human-approval-gated in the editor, same as the
    // v0.1.118 write tier ("denied by user" / "approval timed out" verbatim).
    fn create_note(&mut self, title: &str, body: &str) -> Result<Value, TransportError>;
    fn append_note(&mut self, file: &str, text: &str) -> Result<Value, TransportError>;
    fn set_reminder(&mut self, file: &str, due_iso: &str) -> Result<Value, TransportError>;
    fn export_diagram(
        &mut self,
        tab: TabRef,
        path: &str,
        format: &str,
    ) -> Result<Value, TransportError>;

    // Phase 1 — diagram control + Noter panel.
    fn create_diagram(
        &mut self,
        source: Option<&str>,
        title: Option<&str>,
    ) -> Result<Value, TransportError>;
    fn get_diagram_source(&self, tab: TabRef) -> Result<Value, TransportError>;
    /// Approval-gated in the editor, like the other write verbs.
    fn set_diagram_source(&mut self, tab: TabRef, source: &str) -> Result<Value, TransportError>;
    fn open_noter(&mut self) -> Result<Value, TransportError>;

    // Phase 2 — data-analyst + charts (verbatim JSON passthrough).
    fn list_connections(&self) -> Result<Value, TransportError>;
    fn run_query(
        &self,
        connection_name: &str,
        sql: &str,
        max_rows: Option<usize>,
    ) -> Result<Value, TransportError>;
    fn list_tables(&self, connection_name: &str) -> Result<Value, TransportError>;
    fn open_data_analyst(&mut self) -> Result<Value, TransportError>;
    fn render_chart(&mut self, spec: &Value, title: Option<&str>) -> Result<Value, TransportError>;
    /// Approval-gated in the editor, like the other write verbs.
    fn export_query_results(
        &mut self,
        connection_name: &str,
        sql: &str,
        path: &str,
        format: &str,
        max_rows: Option<usize>,
    ) -> Result<Value, TransportError>;
    /// Approval-gated in the editor, like the other write verbs.
    fn export_chart(
        &mut self,
        spec: &Value,
        path: &str,
        format: &str,
        scale: Option<usize>,
    ) -> Result<Value, TransportError>;
}
