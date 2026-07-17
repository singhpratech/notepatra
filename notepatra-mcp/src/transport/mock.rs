// SPDX-License-Identifier: GPL-3.0-or-later
use serde_json::{json, Value};

use super::{
    EditorTransport, SearchHit, SearchResults, Selection, TabContent, TabInfo, TabSelector,
    TransportError,
};

/// Cap mirrored from the C++ bridge (kMaxFindMatches in src/mcp_bridge.cpp):
/// find_in_tab stops after this many matches and sets `truncated`.
const FIND_IN_TAB_CAP: usize = 500;

struct MockTab {
    title: String,
    path: Option<String>,
    content: String,
    modified: bool,
    language: String,
}

struct MockNote {
    title: String,
    file: String,
    modified_iso: String,
    text: String,
}

/// In-memory fake editor used by default and in tests.
pub struct MockEditor {
    tabs: Vec<MockTab>,
    /// (tab index, selected text)
    selection: (usize, String),
    notes: Vec<MockNote>,
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
                },
                MockTab {
                    title: "NOTES.md".into(),
                    path: Some("/home/user/project/NOTES.md".into()),
                    content: "# Release notes\n\n- fix the lexer edge case\n- ship v0.1.118\n"
                        .into(),
                    modified: true,
                    language: "Markdown".into(),
                },
                MockTab {
                    title: "Untitled 1".into(),
                    path: None,
                    content: "scratch buffer\n".into(),
                    modified: false,
                    language: "Plain Text".into(),
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
            truncated: false,
        })
    }

    fn search_project(
        &self,
        query: &str,
        max_results: usize,
    ) -> Result<SearchResults, TransportError> {
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

    fn find_in_tab(&self, tab_index: Option<usize>, query: &str) -> Result<Value, TransportError> {
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
}
