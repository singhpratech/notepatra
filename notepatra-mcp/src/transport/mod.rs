// SPDX-License-Identifier: GPL-3.0-or-later
use std::fmt;

use serde_json::Value;

pub mod mock;
pub mod socket;

/// One entry of `list_open_tabs` — wire shape `{index,title,path,modified}`.
/// The editor always sends `path` as a string; `""` (untitled tab) maps to
/// `None` here so tool output can omit it.
#[derive(Debug, Clone, serde::Serialize)]
pub struct TabInfo {
    pub index: usize,
    pub title: String,
    #[serde(skip_serializing_if = "Option::is_none")]
    pub path: Option<String>,
    pub modified: bool,
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

/// `search_project` result — wire shape `{results:[SearchHit],truncated}`.
#[derive(Debug, Clone, serde::Serialize)]
pub struct SearchResults {
    pub results: Vec<SearchHit>,
    pub truncated: bool,
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
/// * `goto_line` → `{ok,tab_index,line}`
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
/// * `save_tab` → `{ok:true}`
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
pub trait EditorTransport {
    /// Returns the tab index the file landed in.
    fn open_file(&mut self, path: &str) -> Result<usize, TransportError>;
    fn list_open_tabs(&self) -> Result<Vec<TabInfo>, TransportError>;
    fn read_tab(&self, selector: TabSelector<'_>) -> Result<TabContent, TransportError>;
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
    fn find_in_tab(
        &self,
        tab_index: Option<usize>,
        query: &str,
        regex: bool,
    ) -> Result<Value, TransportError>;
    fn new_tab(&mut self, text: Option<&str>) -> Result<Value, TransportError>;
    fn goto_line(&mut self, line: usize, tab_index: Option<usize>)
        -> Result<Value, TransportError>;
    fn set_language(
        &mut self,
        language: &str,
        tab_index: Option<usize>,
    ) -> Result<Value, TransportError>;
    fn compare_tabs(&mut self, index_a: usize, index_b: usize) -> Result<Value, TransportError>;
    fn format_text(&self, kind: &str, text: &str) -> Result<Value, TransportError>;
    fn list_notes(&self) -> Result<Value, TransportError>;
    fn read_note(&self, file: &str) -> Result<Value, TransportError>;

    // Write verbs (v0.1.118) — human-approval-gated in the editor.
    fn insert_text(
        &mut self,
        text: &str,
        tab_index: Option<usize>,
        line: Option<usize>,
        col: Option<usize>,
    ) -> Result<Value, TransportError>;
    fn replace_selection(
        &mut self,
        text: &str,
        tab_index: Option<usize>,
    ) -> Result<Value, TransportError>;
    fn apply_edit(
        &mut self,
        find: &str,
        replace: &str,
        tab_index: Option<usize>,
        all: bool,
    ) -> Result<Value, TransportError>;
    fn save_tab(&mut self, tab_index: Option<usize>) -> Result<Value, TransportError>;

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
    fn validate_npd(
        &self,
        tab_index: Option<usize>,
        source: Option<&str>,
    ) -> Result<Value, TransportError>;
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
        tab_index: usize,
        path: &str,
        format: &str,
    ) -> Result<Value, TransportError>;

    // Phase 1 — diagram control + Noter panel.
    fn create_diagram(
        &mut self,
        source: Option<&str>,
        title: Option<&str>,
    ) -> Result<Value, TransportError>;
    fn get_diagram_source(&self, tab_index: usize) -> Result<Value, TransportError>;
    /// Approval-gated in the editor, like the other write verbs.
    fn set_diagram_source(
        &mut self,
        tab_index: usize,
        source: &str,
    ) -> Result<Value, TransportError>;
    fn open_noter(&mut self) -> Result<Value, TransportError>;
}
