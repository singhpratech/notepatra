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
pub trait EditorTransport {
    /// Returns the tab index the file landed in.
    fn open_file(&mut self, path: &str) -> Result<usize, TransportError>;
    fn list_open_tabs(&self) -> Result<Vec<TabInfo>, TransportError>;
    fn read_tab(&self, selector: TabSelector<'_>) -> Result<TabContent, TransportError>;
    fn search_project(
        &self,
        query: &str,
        max_results: usize,
    ) -> Result<SearchResults, TransportError>;
    fn get_selection(&self) -> Result<Selection, TransportError>;

    // Wave-2 verbs (v0.1.118).
    fn get_status(&self) -> Result<Value, TransportError>;
    fn app_info(&self) -> Result<Value, TransportError>;
    fn list_recent_files(&self) -> Result<Value, TransportError>;
    fn find_in_tab(&self, tab_index: Option<usize>, query: &str) -> Result<Value, TransportError>;
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
}
