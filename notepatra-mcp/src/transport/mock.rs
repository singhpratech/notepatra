// SPDX-License-Identifier: GPL-3.0-or-later
use serde_json::{json, Value};

use super::{
    EditorTransport, SearchHit, SearchResults, Selection, TabContent, TabInfo, TabSelector,
    TransportError,
};

/// Cap mirrored from the C++ bridge (kMaxFindMatches in src/mcp_bridge.cpp):
/// find_in_tab stops after this many matches and sets `truncated`.
const FIND_IN_TAB_CAP: usize = 500;

/// Mock Noter root: create_note echoes note paths under this directory so
/// `notepatra-mcp` (no --socket) demos the full write surface believably.
const MOCK_NOTER_ROOT: &str = "/home/user/Documents/Notepatra/Noter";

/// Minimal, dependency-free pattern check standing in for the editor's Qt
/// regex validation: rejects unbalanced `()[]{}` and a dangling trailing
/// backslash. Enough to exercise the "server rejects invalid patterns"
/// contract without pulling in a regex crate.
fn validate_regex(pattern: &str) -> Result<(), TransportError> {
    let invalid = || TransportError("invalid regular expression pattern".into());
    let mut stack: Vec<char> = Vec::new();
    let mut escaped = false;
    for ch in pattern.chars() {
        if escaped {
            escaped = false;
            continue;
        }
        match ch {
            '\\' => escaped = true,
            '(' | '[' | '{' => stack.push(ch),
            ')' if stack.pop() == Some('(') => {}
            ']' if stack.pop() == Some('[') => {}
            '}' if stack.pop() == Some('{') => {}
            // A closer that didn't match its opener (the guard's pop already
            // consumed the mismatched element, which is fine — we bail here).
            ')' | ']' | '}' => return Err(invalid()),
            _ => {}
        }
    }
    if escaped || !stack.is_empty() {
        return Err(invalid());
    }
    Ok(())
}

/// Verbatim bridge error when the user clicks Deny on the approval card.
pub const DENIED_BY_USER: &str = "denied by user";
/// Verbatim bridge error when the approval card times out (120 s).
pub const APPROVAL_TIMED_OUT: &str = "approval timed out";

/// Simulated outcome of the in-editor approval card for the write tools.
#[derive(Debug, Clone, Copy, Default, PartialEq, Eq)]
pub enum ApprovalMode {
    /// The user clicks Approve (default: keeps happy paths friction-free).
    #[default]
    Approve,
    /// The user clicks Deny — every write verb fails with [`DENIED_BY_USER`].
    Deny,
    /// The card times out — every write verb fails with
    /// [`APPROVAL_TIMED_OUT`] (no real waiting: tests stay fast).
    Timeout,
}

struct MockTab {
    title: String,
    path: Option<String>,
    content: String,
    modified: bool,
    language: String,
    truncated: bool,
}

struct MockNote {
    title: String,
    file: String,
    modified_iso: String,
    text: String,
}

struct MockReminder {
    file: String,
    title: String,
    due_iso: String,
    bucket: &'static str,
}

/// In-memory fake editor used by default and in tests.
pub struct MockEditor {
    tabs: Vec<MockTab>,
    /// (tab index, selected text)
    selection: (usize, String),
    notes: Vec<MockNote>,
    reminders: Vec<MockReminder>,
    approval: ApprovalMode,
}

fn language_for(path: &str) -> String {
    match path.rsplit('.').next() {
        Some("rs") => "Rust".into(),
        Some("md") => "Markdown".into(),
        Some("json") => "JSON".into(),
        Some("sql") => "SQL".into(),
        _ => "Plain Text".into(),
    }
}

impl Default for MockEditor {
    fn default() -> Self {
        Self {
            tabs: vec![
                MockTab {
                    title: "main.rs".into(),
                    path: Some("/home/user/project/src/main.rs".into()),
                    content: "fn main() {\n    println!(\"hello from notepatra\");\n}\n".into(),
                    modified: false,
                    language: "Rust".into(),
                    truncated: false,
                },
                MockTab {
                    title: "NOTES.md".into(),
                    path: Some("/home/user/project/NOTES.md".into()),
                    content: "# Release notes\n\n- fix the lexer edge case\n- ship v0.1.118\n"
                        .into(),
                    modified: true,
                    language: "Markdown".into(),
                    truncated: false,
                },
                MockTab {
                    title: "Untitled 1".into(),
                    path: None,
                    content: "scratch buffer\n".into(),
                    modified: false,
                    language: "Plain Text".into(),
                    truncated: false,
                },
            ],
            selection: (0, "println!(\"hello from notepatra\");".into()),
            // `file` mirrors the bridge: the note's ABSOLUTE path (Noter
            // stores notes as .html under Documents/Notepatra/Noter).
            notes: vec![
                MockNote {
                    title: "Standup 2026-07-15".into(),
                    file: "/home/user/Documents/Notepatra/Noter/standup-2026-07-15.html".into(),
                    modified_iso: "2026-07-15T09:30:00Z".into(),
                    text: "# Standup\n\n- shipped lexer fix\n- next: MCP bridge\n".into(),
                },
                MockNote {
                    title: "Release checklist".into(),
                    file: "/home/user/Documents/Notepatra/Noter/release-checklist.html".into(),
                    modified_iso: "2026-07-14T18:02:11Z".into(),
                    text: "# Release checklist\n\n- run release-check.sh\n- tag v0.1.118\n".into(),
                },
                MockNote {
                    title: "Meeting with design".into(),
                    file: "/home/user/Documents/Notepatra/Noter/meeting-design.html".into(),
                    modified_iso: "2026-07-10T15:00:00Z".into(),
                    text: "# Design sync\n\n- dark theme parity\n".into(),
                },
                MockNote {
                    title: "Old scratch".into(),
                    file: "/home/user/Documents/Notepatra/Noter/old-scratch.html".into(),
                    modified_iso: "2026-06-01T08:00:00Z".into(),
                    text: "old ideas\n".into(),
                },
            ],
            // One reminder in each of the four temporal buckets so
            // list_reminders demos the full bucketing without a real clock.
            reminders: vec![
                MockReminder {
                    file: "/home/user/Documents/Notepatra/Noter/release-checklist.html".into(),
                    title: "Release checklist".into(),
                    due_iso: "2026-07-16T09:00:00Z".into(),
                    bucket: "Overdue",
                },
                MockReminder {
                    file: "/home/user/Documents/Notepatra/Noter/standup-2026-07-15.html".into(),
                    title: "Standup 2026-07-15".into(),
                    due_iso: "2026-07-17T17:00:00Z".into(),
                    bucket: "Today",
                },
                MockReminder {
                    file: "/home/user/Documents/Notepatra/Noter/meeting-design.html".into(),
                    title: "Meeting with design".into(),
                    due_iso: "2026-07-20T15:00:00Z".into(),
                    bucket: "This week",
                },
                MockReminder {
                    file: "/home/user/Documents/Notepatra/Noter/old-scratch.html".into(),
                    title: "Old scratch".into(),
                    due_iso: "2026-08-30T08:00:00Z".into(),
                    bucket: "Later",
                },
            ],
            approval: ApprovalMode::Approve,
        }
    }
}

impl MockEditor {
    fn info(&self, index: usize) -> TabInfo {
        let t = &self.tabs[index];
        TabInfo {
            index,
            title: t.title.clone(),
            path: t.path.clone(),
            modified: t.modified,
        }
    }

    fn check_index(&self, index: usize) -> Result<usize, TransportError> {
        if index < self.tabs.len() {
            Ok(index)
        } else {
            Err(TransportError(format!(
                "no tab at index {index} ({} open)",
                self.tabs.len()
            )))
        }
    }

    /// Resolves an optional index against the active tab.
    fn resolve(&self, tab_index: Option<usize>) -> Result<usize, TransportError> {
        self.check_index(tab_index.unwrap_or(self.selection.0))
    }

    /// Test hook: simulate the user's response to the in-editor approval
    /// card for all subsequent write tools (default: Approve).
    pub fn set_approval(&mut self, mode: ApprovalMode) {
        self.approval = mode;
    }

    /// Test hook: mark a tab so reads report `truncated: true` (the real
    /// editor caps read_tab at 5 MB).
    pub fn simulate_truncated_tab(&mut self, index: usize) {
        self.tabs[index].truncated = true;
    }

    /// Test hook: add a Noter note (used to exercise basename collisions in
    /// resource URIs).
    pub fn add_note(&mut self, title: &str, file: &str, text: &str) {
        self.notes.push(MockNote {
            title: title.to_string(),
            file: file.to_string(),
            modified_iso: "2026-07-16T00:00:00Z".into(),
            text: text.to_string(),
        });
    }

    /// The simulated approval-card outcome every write verb goes through.
    fn check_approval(&self) -> Result<(), TransportError> {
        match self.approval {
            ApprovalMode::Approve => Ok(()),
            ApprovalMode::Deny => Err(TransportError(DENIED_BY_USER.into())),
            ApprovalMode::Timeout => Err(TransportError(APPROVAL_TIMED_OUT.into())),
        }
    }

    /// Next "Untitled N" label: scan the max existing N, never recount by
    /// position (labels must survive closes of earlier untitled tabs).
    fn next_untitled_title(&self) -> String {
        let max_n = self
            .tabs
            .iter()
            .filter_map(|t| t.title.strip_prefix("Untitled "))
            .filter_map(|n| n.parse::<usize>().ok())
            .max()
            .unwrap_or(0);
        format!("Untitled {}", max_n + 1)
    }
}

impl EditorTransport for MockEditor {
    fn open_file(&mut self, path: &str) -> Result<usize, TransportError> {
        if let Some(i) = self
            .tabs
            .iter()
            .position(|t| t.path.as_deref() == Some(path))
        {
            return Ok(i);
        }
        let title = path.rsplit('/').next().unwrap_or(path).to_string();
        self.tabs.push(MockTab {
            title,
            path: Some(path.to_string()),
            content: String::new(),
            modified: false,
            language: language_for(path),
            truncated: false,
        });
        Ok(self.tabs.len() - 1)
    }

    fn list_open_tabs(&self) -> Result<Vec<TabInfo>, TransportError> {
        Ok((0..self.tabs.len()).map(|i| self.info(i)).collect())
    }

    fn read_tab(&self, selector: TabSelector<'_>) -> Result<TabContent, TransportError> {
        let index = match selector {
            TabSelector::Index(i) => self.check_index(i)?,
            TabSelector::Title(title) => self
                .tabs
                .iter()
                .position(|t| t.title == title)
                .ok_or_else(|| TransportError(format!("no tab titled {title:?}")))?,
        };
        let t = &self.tabs[index];
        Ok(TabContent {
            title: t.title.clone(),
            path: t.path.clone(),
            text: t.content.clone(),
            truncated: t.truncated,
        })
    }

    fn search_project(
        &self,
        query: &str,
        max_results: usize,
        regex: bool,
    ) -> Result<SearchResults, TransportError> {
        // The real regex engine lives in the editor (Qt QRegularExpression);
        // the mock only validates the pattern so callers can exercise the
        // "server rejects invalid patterns" contract, then falls back to
        // case-insensitive substring matching (mock simplification).
        if regex {
            validate_regex(query)?;
        }
        // Bridge semantics: hits carry the tab's file path ("" for untitled
        // buffers), 1-based line numbers, and a truncation flag.
        let mut results = Vec::new();
        'outer: for tab in &self.tabs {
            for (n, line) in tab.content.lines().enumerate() {
                if line.to_lowercase().contains(&query.to_lowercase()) {
                    results.push(SearchHit {
                        path: tab.path.clone().unwrap_or_default(),
                        line: n + 1,
                        text: line.trim().to_string(),
                    });
                    if results.len() >= max_results {
                        break 'outer;
                    }
                }
            }
        }
        // Same rule as the bridge: hitting the cap flags truncation even if
        // the very last match was the final one.
        let truncated = results.len() >= max_results;
        Ok(SearchResults { results, truncated })
    }

    fn get_selection(&self) -> Result<Selection, TransportError> {
        let (tab_index, ref text) = self.selection;
        Ok(Selection {
            tab_index: tab_index as i64,
            text: text.clone(),
        })
    }

    fn get_status(&self) -> Result<Value, TransportError> {
        let i = self.selection.0;
        let t = &self.tabs[i];
        Ok(json!({
            "tab_index": i,
            "title": t.title,
            "path": t.path,
            "language": t.language,
            "encoding": "UTF-8",
            "cursor_line": 2,
            "cursor_col": 5,
            "edition": "Lite",
            "version": "0.1.118",
        }))
    }

    fn app_info(&self) -> Result<Value, TransportError> {
        Ok(json!({
            "name": "Notepatra",
            "version": "0.1.118",
            "edition": "Lite",
            "platform": std::env::consts::OS,
        }))
    }

    fn list_recent_files(&self) -> Result<Value, TransportError> {
        Ok(json!({
            "files": [
                "/home/user/project/src/main.rs",
                "/home/user/project/NOTES.md",
                "/home/user/project/Cargo.toml",
            ]
        }))
    }

    fn find_in_tab(
        &self,
        tab_index: Option<usize>,
        query: &str,
        regex: bool,
    ) -> Result<Value, TransportError> {
        if regex {
            validate_regex(query)?;
        }
        let i = self.resolve(tab_index)?;
        let mut matches = Vec::new();
        let mut truncated = false;
        // Bridge semantics: case-insensitive substring, trimmed line text.
        for (n, line) in self.tabs[i].content.lines().enumerate() {
            if line.to_lowercase().contains(&query.to_lowercase()) {
                if matches.len() >= FIND_IN_TAB_CAP {
                    truncated = true;
                    break;
                }
                matches.push(json!({ "line": n + 1, "text": line.trim() }));
            }
        }
        Ok(json!({ "matches": matches, "truncated": truncated }))
    }

    fn new_tab(&mut self, text: Option<&str>) -> Result<Value, TransportError> {
        self.tabs.push(MockTab {
            title: self.next_untitled_title(),
            path: None,
            content: text.unwrap_or_default().to_string(),
            modified: text.is_some_and(|t| !t.is_empty()),
            language: "Plain Text".into(),
            truncated: false,
        });
        Ok(json!({ "tab_index": self.tabs.len() - 1 }))
    }

    fn goto_line(
        &mut self,
        line: usize,
        tab_index: Option<usize>,
    ) -> Result<Value, TransportError> {
        let i = self.resolve(tab_index)?;
        if line == 0 {
            return Err(TransportError("line numbers are 1-based".into()));
        }
        self.selection.0 = i;
        Ok(json!({ "ok": true, "tab_index": i, "line": line }))
    }

    fn set_language(
        &mut self,
        language: &str,
        tab_index: Option<usize>,
    ) -> Result<Value, TransportError> {
        let i = self.resolve(tab_index)?;
        self.tabs[i].language = language.to_string();
        Ok(json!({ "ok": true, "tab_index": i, "language": language }))
    }

    fn compare_tabs(&mut self, index_a: usize, index_b: usize) -> Result<Value, TransportError> {
        self.check_index(index_a)?;
        self.check_index(index_b)?;
        if index_a == index_b {
            return Err(TransportError("compare needs two different tabs".into()));
        }
        Ok(json!({ "opened": true }))
    }

    fn format_text(&self, kind: &str, text: &str) -> Result<Value, TransportError> {
        let formatted = match kind {
            "json" => {
                let v: Value = serde_json::from_str(text)
                    .map_err(|e| TransportError(format!("invalid JSON: {e}")))?;
                let mut s = serde_json::to_string_pretty(&v)
                    .map_err(|e| TransportError(format!("serialization failed: {e}")))?;
                s.push('\n');
                s
            }
            // The real formatter lives in the editor; the mock only normalizes
            // trailing whitespace so round-trips are observable in tests.
            "sql" | "html" => {
                let mut s: String = text
                    .lines()
                    .map(|l| l.trim_end())
                    .collect::<Vec<_>>()
                    .join("\n");
                s.push('\n');
                s
            }
            other => return Err(TransportError(format!("unknown format kind {other:?}"))),
        };
        Ok(json!({ "text": formatted }))
    }

    fn list_notes(&self) -> Result<Value, TransportError> {
        let notes: Vec<Value> = self
            .notes
            .iter()
            .map(|n| json!({ "title": n.title, "file": n.file, "modified_iso": n.modified_iso }))
            .collect();
        Ok(json!({ "notes": notes }))
    }

    fn read_note(&self, file: &str) -> Result<Value, TransportError> {
        self.notes
            .iter()
            .find(|n| n.file == file)
            .map(|n| json!({ "title": n.title, "text": n.text }))
            .ok_or_else(|| TransportError(format!("no note named {file:?}")))
    }

    // Write verbs: each one first passes through the simulated approval card
    // (approve by default; deny/timeout via `set_approval`).

    fn insert_text(
        &mut self,
        text: &str,
        tab_index: Option<usize>,
        line: Option<usize>,
        col: Option<usize>,
    ) -> Result<Value, TransportError> {
        self.check_approval()?;
        let i = self.resolve(tab_index)?;
        let t = &mut self.tabs[i];
        match line {
            // No explicit position: the cursor stand-in is end-of-document.
            None => t.content.push_str(text),
            Some(l) => {
                let mut lines: Vec<String> = t.content.split('\n').map(String::from).collect();
                if l > lines.len() {
                    return Err(TransportError(format!("no line {l} in tab {i}")));
                }
                let target = &mut lines[l - 1];
                // 1-based column, clamped to the line end (bridge semantics).
                let cpos = col
                    .unwrap_or(1)
                    .saturating_sub(1)
                    .min(target.chars().count());
                let byte = target
                    .char_indices()
                    .nth(cpos)
                    .map_or(target.len(), |(b, _)| b);
                target.insert_str(byte, text);
                t.content = lines.join("\n");
            }
        }
        t.modified = true;
        Ok(json!({ "ok": true, "tab_index": i }))
    }

    fn replace_selection(
        &mut self,
        text: &str,
        tab_index: Option<usize>,
    ) -> Result<Value, TransportError> {
        self.check_approval()?;
        let i = self.resolve(tab_index)?;
        // The mock models the selection as (tab, text): swap the first
        // occurrence of the selected text and select the replacement.
        let sel_text = self.selection.1.clone();
        let t = &mut self.tabs[i];
        if !sel_text.is_empty() {
            if let Some(pos) = t.content.find(&sel_text) {
                t.content.replace_range(pos..pos + sel_text.len(), text);
            }
        }
        t.modified = true;
        self.selection = (i, text.to_string());
        Ok(json!({ "ok": true }))
    }

    fn apply_edit(
        &mut self,
        find: &str,
        replace: &str,
        tab_index: Option<usize>,
        all: bool,
    ) -> Result<Value, TransportError> {
        self.check_approval()?;
        let i = self.resolve(tab_index)?;
        if find.is_empty() {
            return Err(TransportError("find must not be empty".into()));
        }
        let t = &mut self.tabs[i];
        let count = if all {
            t.content.matches(find).count()
        } else {
            usize::from(t.content.contains(find))
        };
        if count > 0 {
            t.content = if all {
                t.content.replace(find, replace)
            } else {
                t.content.replacen(find, replace, 1)
            };
            t.modified = true;
        }
        Ok(json!({ "ok": true, "count": count }))
    }

    fn save_tab(&mut self, tab_index: Option<usize>) -> Result<Value, TransportError> {
        self.check_approval()?;
        let i = self.resolve(tab_index)?;
        self.tabs[i].modified = false;
        Ok(json!({ "ok": true }))
    }

    // ── v0.1.119 read verbs ────────────────────────────────────────────

    fn list_reminders(&self) -> Result<Value, TransportError> {
        let reminders: Vec<Value> = self
            .reminders
            .iter()
            .map(|r| {
                json!({
                    "note_file": r.file,
                    "note_title": r.title,
                    "due_iso": r.due_iso,
                    "bucket": r.bucket,
                })
            })
            .collect();
        Ok(json!({ "reminders": reminders }))
    }

    // The git verbs return the raw `git` CLI text under a single `output`
    // key (the C++ bridge shells out and passes stdout through verbatim).

    fn git_status(&self) -> Result<Value, TransportError> {
        Ok(json!({
            "output": "On branch v0119-mcp-depth\n\
                       Changes to be committed:\n\tmodified:   src/tools.rs\n\
                       Changes not staged for commit:\n\tmodified:   src/transport/socket.rs\n\
                       Untracked files:\n\tnotes/scratch.md\n",
        }))
    }

    fn git_diff(&self, path: Option<&str>) -> Result<Value, TransportError> {
        let target = path.unwrap_or("src/tools.rs");
        let output = format!(
            "diff --git a/{target} b/{target}\n\
             --- a/{target}\n\
             +++ b/{target}\n\
             @@ -1,3 +1,4 @@\n\
             \x20// SPDX-License-Identifier: GPL-3.0-or-later\n\
             +// v0.1.119: MCP depth\n\
             \x20use serde_json::Value;\n"
        );
        Ok(json!({ "output": output }))
    }

    fn git_log(&self, limit: usize) -> Result<Value, TransportError> {
        let all = [
            "4d32cc8 ci: harden NSIS install fallback",
            "0a180a9 docs: bare-binary size bump",
            "1859cab v0.1.116: Compare readability",
        ];
        // Mirror `git log -n <limit> --oneline`: one line per commit.
        let output = all
            .iter()
            .take(limit)
            .copied()
            .collect::<Vec<_>>()
            .join("\n");
        Ok(json!({ "output": format!("{output}\n") }))
    }

    fn git_show(&self, git_ref: &str) -> Result<Value, TransportError> {
        Ok(json!({
            "output": format!(
                "commit {git_ref}\nAuthor: Prateek Singh\nDate: 2026-07-17\n\n\
                 \x20   ci: harden NSIS install fallback — retry choco + fail fast\n\n\
                 diff --git a/.github/workflows/ci.yml b/.github/workflows/ci.yml\n\
                 @@ -10,2 +10,3 @@\n\
                 +      # {git_ref}: retry + fail fast\n"
            ),
        }))
    }

    fn git_branch(&self) -> Result<Value, TransportError> {
        Ok(json!({
            "output": "  main\n* v0119-mcp-depth\n",
        }))
    }

    fn validate_npd(
        &self,
        tab_index: Option<usize>,
        source: Option<&str>,
    ) -> Result<Value, TransportError> {
        // The tool layer guarantees exactly one selector is present.
        let text = match (tab_index, source) {
            (Some(i), None) => self.tabs[self.check_index(i)?].content.clone(),
            (None, Some(s)) => s.to_string(),
            _ => {
                return Err(TransportError(
                    "provide exactly one of tab_index or source".into(),
                ))
            }
        };
        // Believable stand-in for the editor's npd parser: a line beginning
        // with "!!" is treated as a malformed directive (one error case).
        let mut errors = Vec::new();
        for (n, line) in text.lines().enumerate() {
            if line.trim_start().starts_with("!!") {
                // Bridge error shape: {line, message} (no column).
                errors.push(json!({
                    "line": n + 1,
                    "message": "unknown directive",
                }));
            }
        }
        Ok(json!({ "valid": errors.is_empty(), "errors": errors }))
    }

    fn run_sql(&self, sql: &str, _csv_path: Option<&str>) -> Result<Value, TransportError> {
        // The editor enforces SELECT-only; the mock mirrors that rejection so
        // the "non-read statements are rejected" contract is observable.
        let head = sql
            .split_whitespace()
            .next()
            .unwrap_or("")
            .to_ascii_uppercase();
        if head != "SELECT" && head != "WITH" {
            return Err(TransportError(
                "only read-only SELECT statements are allowed".into(),
            ));
        }
        Ok(json!({
            "columns": [ "id", "name" ],
            "rows": [ [1, "alpha"], [2, "bravo"] ],
            "truncated": false,
            "engine": "duckdb",
        }))
    }

    // ── v0.1.119 act verb ──────────────────────────────────────────────

    fn open_note(&mut self, file: &str) -> Result<Value, TransportError> {
        if !self.notes.iter().any(|n| n.file == file) {
            return Err(TransportError(format!("no note named {file:?}")));
        }
        // Opening a note surfaces it as a new tab in the editor.
        let title = self
            .notes
            .iter()
            .find(|n| n.file == file)
            .map(|n| n.title.clone())
            .unwrap_or_default();
        self.tabs.push(MockTab {
            title: title.clone(),
            path: Some(file.to_string()),
            content: String::new(),
            modified: false,
            language: "HTML".into(),
            truncated: false,
        });
        // Bridge result shape: {opened, title}.
        Ok(json!({ "opened": true, "title": title }))
    }

    // ── v0.1.119 write verbs — approval-gated ──────────────────────────

    fn create_note(&mut self, title: &str, body: &str) -> Result<Value, TransportError> {
        self.check_approval()?;
        // Echo a plausible slugged .html path under the mock Noter root.
        let slug: String = title
            .chars()
            .map(|c| {
                if c.is_ascii_alphanumeric() {
                    c.to_ascii_lowercase()
                } else {
                    '-'
                }
            })
            .collect();
        let slug = slug.trim_matches('-').to_string();
        let file = format!("{MOCK_NOTER_ROOT}/{slug}.html");
        self.notes.push(MockNote {
            title: title.to_string(),
            file: file.clone(),
            modified_iso: "2026-07-17T00:00:00Z".into(),
            text: body.to_string(),
        });
        // Bridge result shape: {file, title}.
        Ok(json!({ "file": file, "title": title }))
    }

    fn append_note(&mut self, file: &str, text: &str) -> Result<Value, TransportError> {
        self.check_approval()?;
        let note = self
            .notes
            .iter_mut()
            .find(|n| n.file == file)
            .ok_or_else(|| TransportError(format!("no note named {file:?}")))?;
        note.text.push_str(text);
        // Bridge result shape: {file}.
        Ok(json!({ "file": file }))
    }

    fn set_reminder(&mut self, file: &str, due_iso: &str) -> Result<Value, TransportError> {
        self.check_approval()?;
        let note = self
            .notes
            .iter()
            .find(|n| n.file == file)
            .ok_or_else(|| TransportError(format!("no note named {file:?}")))?;
        self.reminders.push(MockReminder {
            file: file.to_string(),
            title: note.title.clone(),
            due_iso: due_iso.to_string(),
            bucket: "Later",
        });
        // Bridge result shape: {file, due_iso}.
        Ok(json!({ "file": file, "due_iso": due_iso }))
    }

    fn export_diagram(
        &mut self,
        tab_index: usize,
        path: &str,
        format: &str,
    ) -> Result<Value, TransportError> {
        self.check_approval()?;
        self.check_index(tab_index)?;
        if format != "png" && format != "pdf" {
            return Err(TransportError(format!(
                "unsupported export format {format:?} (expected png or pdf)"
            )));
        }
        // Bridge result shape: {path}.
        Ok(json!({ "path": path }))
    }
}
