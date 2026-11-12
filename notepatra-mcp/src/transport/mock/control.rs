// SPDX-License-Identifier: GPL-3.0-or-later
//! Mock answers for the v0.1.133 Phase 3 verbs (git writes, Noter
//! management, settings, data, REST). Result SHAPES mirror the C++ bridge;
//! the state is a small in-memory stand-in, and writes go through the same
//! simulated approval card as every other mock write.

use serde_json::{json, Map, Value};

use super::{MockConnection, MockEditor, MockNote, TransportError};

fn err(msg: impl Into<String>) -> TransportError {
    TransportError(msg.into())
}

fn s<'a>(a: &'a Value, k: &str) -> &'a str {
    a.get(k).and_then(Value::as_str).unwrap_or("")
}

const PREF_KEYS: &[(&str, &str)] = &[
    ("font_size", "int:6:48"),
    ("font_family", "string"),
    ("tab_width", "int:1:16"),
    ("use_spaces", "bool"),
    ("auto_indent", "bool"),
    ("word_wrap", "bool"),
    ("show_line_numbers", "bool"),
    ("show_indent_guides", "bool"),
    ("highlight_current_line", "bool"),
    ("show_edge", "bool"),
    ("edge_column", "int:20:500"),
    ("caret_width", "int:1:3"),
    ("smooth_font", "bool"),
    (
        "fold_style",
        "enum:BoxedTree|CircleTree|Plain|Boxed|Circle|None",
    ),
    ("show_bookmark_margin", "bool"),
    ("show_document_rulers", "bool"),
    ("show_crosshair", "bool"),
    ("auto_complete", "bool"),
    ("auto_complete_threshold", "int:1:10"),
    ("default_eol", "enum:Unix|Windows|Mac"),
    ("autosave_interval_sec", "int:1:300"),
    ("theme", "enum:System|Light|Dark|Monokai"),
    ("hide_toolbar", "bool"),
    ("tabs_closable", "bool"),
    ("double_click_to_close_tab", "bool"),
    ("show_welcome_on_startup", "bool"),
];

const MOCK_REPO: &str = "/home/user/project";
const CONFLICT_FILE: &str = "src/merge.rs";

pub(super) struct State {
    branch: String,
    branches: Vec<String>,
    staged: Vec<String>,
    modified: Vec<String>,
    untracked: Vec<String>,
    conflicts: Vec<String>,
    conflict_text: String,
    stashes: usize,
    commits: usize,
    prefs: Map<String, Value>,
    workspace: String,
    trash: Vec<MockNote>,
    reminder_seq: usize,
    recent_cleared: bool,
}

impl Default for State {
    fn default() -> Self {
        let mut prefs = Map::new();
        for (k, t) in PREF_KEYS {
            let v = match *t {
                "bool" => json!(!k.starts_with("show_crosshair") && !k.starts_with("hide")),
                "string" => json!("Monospace"),
                _ if t.starts_with("int") => json!(match *k {
                    "font_size" => 11,
                    "tab_width" => 4,
                    "edge_column" => 120,
                    "caret_width" => 2,
                    "auto_complete_threshold" => 3,
                    _ => 5,
                }),
                _ => json!(t
                    .trim_start_matches("enum:")
                    .split('|')
                    .next()
                    .unwrap_or("")),
            };
            prefs.insert((*k).into(), v);
        }
        Self {
            branch: "main".into(),
            branches: vec!["main".into(), "feature/lexer".into()],
            staged: vec!["src/tools.rs".into()],
            modified: vec!["src/transport/socket.rs".into(), "src/lib.rs".into()],
            untracked: vec!["notes/scratch.md".into(), ".env".into()],
            conflicts: vec![CONFLICT_FILE.into()],
            conflict_text:
                "fn a() {}\n<<<<<<< HEAD\nlet x = 1;\n=======\nlet x = 2;\n>>>>>>> feature/lexer\n"
                    .into(),
            stashes: 0,
            commits: 0,
            prefs,
            workspace: MOCK_REPO.into(),
            trash: Vec::new(),
            reminder_seq: 4,
            recent_cleared: false,
        }
    }
}

impl State {
    pub(super) fn next_reminder_id(&mut self) -> String {
        self.reminder_seq += 1;
        format!("r{}", self.reminder_seq)
    }
}

/// Stand-in for the credential deny-list (keys, .env, keystores).
fn secret(path: &str) -> bool {
    let name = path.rsplit('/').next().unwrap_or(path);
    name == ".env"
        || name.starts_with(".env.")
        || name.starts_with("id_rsa")
        || name.starts_with("id_ed25519")
        || name.ends_with(".pem")
        || name.ends_with(".key")
        || path.contains(".ssh/")
        || path.contains(".aws/")
}

fn rel_path(p: &str) -> Result<String, TransportError> {
    if p.starts_with('-') {
        return Err(err(format!(
            "refusing path '{p}': a path may not start with '-'"
        )));
    }
    let rel = p
        .strip_prefix(&format!("{MOCK_REPO}/"))
        .unwrap_or(p)
        .trim_start_matches("./")
        .to_string();
    if rel.starts_with('/') || rel.split('/').any(|c| c == "..") || rel.starts_with(".git") {
        return Err(err(format!(
            "path is outside the workspace repository: {p}"
        )));
    }
    if secret(&rel) {
        return Err(err(format!(
            "refusing {rel}: it matches the credential deny-list (SSH/GPG/AWS keys, .env, \
             keystores, Terraform state)"
        )));
    }
    Ok(rel)
}

fn paths(a: &Value) -> Result<Vec<String>, TransportError> {
    a.get("paths")
        .and_then(Value::as_array)
        .ok_or_else(|| err("paths must be a non-empty array of file paths"))?
        .iter()
        .map(|p| rel_path(p.as_str().unwrap_or("")))
        .collect()
}

fn conflict_regions(text: &str) -> Vec<Value> {
    let lines: Vec<&str> = text.lines().collect();
    let mut out = Vec::new();
    let mut i = 0;
    while i < lines.len() {
        if let Some(ours_label) = lines[i].strip_prefix("<<<<<<<") {
            let start = i;
            let sep = (i..lines.len()).find(|&k| lines[k].starts_with("======="));
            let end = sep.and_then(|s| (s..lines.len()).find(|&k| lines[k].starts_with(">>>>>>>")));
            if let (Some(sep), Some(end)) = (sep, end) {
                let ours: String = lines[start + 1..sep]
                    .iter()
                    .map(|l| format!("{l}\n"))
                    .collect();
                let theirs: String = lines[sep + 1..end]
                    .iter()
                    .map(|l| format!("{l}\n"))
                    .collect();
                out.push(json!({
                    "index": out.len(), "start_line": start + 1, "separator_line": sep + 1,
                    "end_line": end + 1, "ours_label": ours_label.trim(),
                    "theirs_label": lines[end].trim_start_matches('>').trim(),
                    "ours": ours, "theirs": theirs,
                }));
                i = end + 1;
                continue;
            }
        }
        i += 1;
    }
    out
}

impl MockEditor {
    fn note_index(&self, file: &str) -> Result<usize, TransportError> {
        self.notes
            .iter()
            .position(|n| n.file == file)
            .ok_or_else(|| err(format!("not a note file: {file}")))
    }

    fn conn_index(&self, name: &str) -> Result<usize, TransportError> {
        self.connections
            .iter()
            .position(|c| c.name == name)
            .ok_or_else(|| err(format!("no connection named: {name}")))
    }

    pub(super) fn mock_control(&mut self, verb: &str, a: &Value) -> Result<Value, TransportError> {
        match verb {
            // ── git ──
            "list_hunks" => {
                let rel = rel_path(s(a, "path"))?;
                if !self.control.modified.contains(&rel) {
                    return Ok(json!({ "path": rel, "hunks": [] }));
                }
                Ok(json!({ "path": rel, "hunks": [{
                    "index": 0, "header": "@@ -1,3 +1,4 @@", "old_start": 1, "old_lines": 3,
                    "new_start": 1, "new_lines": 4,
                    "diff": "@@ -1,3 +1,4 @@\n line one\n+added line\n line two\n line three",
                }]}))
            }
            "list_conflicts" => {
                if a.get("path").is_none() {
                    let files: Vec<Value> = self
                        .control
                        .conflicts
                        .iter()
                        .map(|f| json!({ "path": f, "regions": conflict_regions(&self.control.conflict_text).len() }))
                        .collect();
                    return Ok(json!({ "files": files }));
                }
                let rel = rel_path(s(a, "path"))?;
                let regions = if rel == CONFLICT_FILE {
                    conflict_regions(&self.control.conflict_text)
                } else {
                    Vec::new()
                };
                Ok(json!({ "path": rel, "regions": regions }))
            }
            "git_stage" => {
                let ps = paths(a)?;
                for p in &ps {
                    let c = &self.control;
                    if !c.modified.contains(p)
                        && !c.untracked.contains(p)
                        && !c.conflicts.contains(p)
                    {
                        return Err(err(format!(
                            "nothing to stage for {p} (no changes in the working tree)"
                        )));
                    }
                }
                self.check_approval()?;
                for p in &ps {
                    let c = &mut self.control;
                    c.modified.retain(|x| x != p);
                    c.untracked.retain(|x| x != p);
                    c.conflicts.retain(|x| x != p);
                    if !c.staged.contains(p) {
                        c.staged.push(p.clone());
                    }
                }
                Ok(json!({ "staged": ps }))
            }
            "git_unstage" => {
                let ps = paths(a)?;
                if let Some(p) = ps.iter().find(|p| !self.control.staged.contains(p)) {
                    return Err(err(format!("{p} is not staged")));
                }
                self.check_approval()?;
                for p in &ps {
                    self.control.staged.retain(|x| x != p);
                    self.control.modified.push(p.clone());
                }
                Ok(json!({ "unstaged": ps }))
            }
            "git_discard" => {
                let ps = paths(a)?;
                for p in &ps {
                    if self.control.untracked.contains(p) {
                        return Err(err(format!(
                            "{p} is untracked: git_discard never deletes untracked files"
                        )));
                    }
                    if !self.control.modified.contains(p) {
                        return Err(err(format!(
                            "{p} has no uncommitted working-tree changes to discard"
                        )));
                    }
                }
                self.check_approval()?;
                self.control.modified.retain(|x| !ps.contains(x));
                Ok(json!({ "discarded": ps }))
            }
            "git_commit" => {
                if self.control.staged.is_empty() {
                    return Err(err("nothing is staged: stage files with git_stage first (git_commit never commits unstaged changes)"));
                }
                self.check_approval()?;
                self.control.commits += 1;
                let files = std::mem::take(&mut self.control.staged);
                Ok(
                    json!({ "commit": format!("c0ffee{}", self.control.commits), "branch": self.control.branch, "files": files }),
                )
            }
            "git_branch_create" => {
                let name = s(a, "name").to_string();
                if name.starts_with('-') || name.contains("..") || name.contains(' ') {
                    return Err(err(format!("invalid branch name: {name}")));
                }
                if self.control.branches.contains(&name) {
                    return Err(err(format!("branch '{name}' already exists")));
                }
                self.check_approval()?;
                self.control.branches.push(name.clone());
                let sw = a.get("switch").and_then(Value::as_bool).unwrap_or(false);
                if sw {
                    self.control.branch = name.clone();
                }
                Ok(json!({ "branch": name, "switched": sw }))
            }
            "git_checkout" => {
                let name = s(a, "name").to_string();
                if !self.control.branches.contains(&name) {
                    return Err(err(format!("no local branch named '{name}'")));
                }
                if name == self.control.branch {
                    return Err(err(format!("already on branch '{name}'")));
                }
                self.check_approval()?;
                self.control.branch = name.clone();
                Ok(json!({ "branch": name }))
            }
            "git_stash" => {
                let action = s(a, "action").to_string();
                let c = &self.control;
                if action == "push"
                    && c.staged.is_empty()
                    && c.modified.is_empty()
                    && c.untracked.is_empty()
                {
                    return Err(err("nothing to stash: the working tree is clean"));
                }
                if action != "push" && c.stashes == 0 {
                    return Err(err(format!("there is no stash to {action}")));
                }
                self.check_approval()?;
                let c = &mut self.control;
                match action.as_str() {
                    "push" => {
                        c.stashes += 1;
                        c.staged.clear();
                        c.modified.clear();
                        c.untracked.clear();
                    }
                    "pop" => c.stashes -= 1,
                    _ => {}
                }
                Ok(json!({ "action": action, "output": "" }))
            }
            "git_fetch" | "git_pull" | "git_push" => {
                if a.as_object().is_some_and(|m| !m.is_empty()) {
                    return Err(err(format!("{verb} takes no arguments")));
                }
                self.check_approval()?;
                Ok(
                    json!({ "ok": true, "branch": self.control.branch, "output": "Everything up-to-date" }),
                )
            }
            "git_hunk" => {
                let rel = rel_path(s(a, "path"))?;
                let idx = a.get("hunk_index").and_then(Value::as_u64).unwrap_or(0);
                if !self.control.modified.contains(&rel) || idx != 0 {
                    return Err(err(format!(
                        "{rel} has no hunk {idx} (list_hunks shows them)"
                    )));
                }
                self.check_approval()?;
                if s(a, "action") == "revert" {
                    self.control.modified.retain(|x| x != &rel);
                } else if !self.control.staged.contains(&rel) {
                    self.control.staged.push(rel.clone());
                }
                Ok(json!({ "path": rel, "hunk_index": idx, "action": s(a, "action") }))
            }
            "resolve_conflict" => {
                let rel = rel_path(s(a, "path"))?;
                let regions = if rel == CONFLICT_FILE {
                    conflict_regions(&self.control.conflict_text)
                } else {
                    Vec::new()
                };
                let i = a.get("region").and_then(Value::as_u64).unwrap_or(0) as usize;
                let Some(r) = regions.get(i) else {
                    return Err(err(format!(
                        "{rel} has {} conflict region(s); region {i} does not exist",
                        regions.len()
                    )));
                };
                self.check_approval()?;
                let take = s(a, "take");
                let mut repl = String::new();
                if take != "theirs" {
                    repl.push_str(r["ours"].as_str().unwrap_or(""));
                }
                if take != "ours" {
                    repl.push_str(r["theirs"].as_str().unwrap_or(""));
                }
                let lines: Vec<&str> = self.control.conflict_text.lines().collect();
                let start = r["start_line"].as_u64().unwrap_or(1) as usize - 1;
                let end = r["end_line"].as_u64().unwrap_or(1) as usize;
                let mut text: String = lines[..start].iter().map(|l| format!("{l}\n")).collect();
                text.push_str(&repl);
                text.extend(lines[end..].iter().map(|l| format!("{l}\n")));
                self.control.conflict_text = text;
                let remaining = conflict_regions(&self.control.conflict_text).len();
                Ok(json!({ "path": rel, "remaining": remaining }))
            }
            // ── Noter ──
            "list_notes" => {
                let mut notes: Vec<Value> = self
                    .notes
                    .iter()
                    .map(|n| json!({ "title": n.title, "file": n.file, "modified_iso": n.modified_iso, "trashed": false }))
                    .collect();
                notes.extend(self.control.trash.iter().map(|n| {
                    json!({ "title": n.title, "file": n.file, "modified_iso": n.modified_iso, "trashed": true })
                }));
                Ok(json!({ "notes": notes }))
            }
            "search_notes" => {
                let terms: Vec<String> = s(a, "query")
                    .split_whitespace()
                    .map(str::to_lowercase)
                    .collect();
                let max = a.get("max_results").and_then(Value::as_u64).unwrap_or(20) as usize;
                let hits: Vec<Value> = self
                    .notes
                    .iter()
                    .filter(|n| {
                        let hay = format!("{}\n{}", n.title, n.text).to_lowercase();
                        !terms.is_empty() && terms.iter().all(|t| hay.contains(t.as_str()))
                    })
                    .take(max)
                    .map(|n| json!({ "file": n.file, "title": n.title, "snippet": n.text.lines().take(2).collect::<Vec<_>>().join(" "), "modified_iso": n.modified_iso }))
                    .collect();
                Ok(json!({ "results": hits, "truncated": false }))
            }
            "rename_note" => {
                let i = self.note_index(s(a, "file"))?;
                let title = s(a, "new_title").trim().to_string();
                self.check_approval()?;
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
                let dir = self.notes[i]
                    .file
                    .rsplit_once('/')
                    .map_or("", |(d, _)| d)
                    .to_string();
                self.notes[i].file = format!("{dir}/{}.html", slug.trim_matches('-'));
                self.notes[i].title = title.clone();
                Ok(json!({ "file": self.notes[i].file, "title": title }))
            }
            "trash_note" => {
                let i = self.note_index(s(a, "file"))?;
                self.check_approval()?;
                let mut n = self.notes.remove(i);
                let name = n.file.rsplit('/').next().unwrap_or("note.html").to_string();
                n.file = format!(
                    "{}/Trash/.trashed-1700000000000-{name}",
                    super::MOCK_NOTER_ROOT
                );
                let file = n.file.clone();
                self.control.trash.push(n);
                Ok(json!({ "file": file, "trashed": true }))
            }
            "restore_note" | "delete_note_permanently" => {
                let file = s(a, "file");
                let Some(i) = self.control.trash.iter().position(|n| n.file == file) else {
                    return Err(err(format!(
                        "'{file}' is not in the Noter Trash; {verb} only works on trashed notes"
                    )));
                };
                self.check_approval()?;
                let mut n = self.control.trash.remove(i);
                if verb == "delete_note_permanently" {
                    return Ok(json!({ "deleted": true }));
                }
                let name = n
                    .file
                    .rsplit('/')
                    .next()
                    .unwrap_or("")
                    .trim_start_matches(".trashed-1700000000000-")
                    .to_string();
                n.file = format!("{}/Inbox/{name}", super::MOCK_NOTER_ROOT);
                let out = json!({ "file": n.file });
                self.notes.push(n);
                Ok(out)
            }
            "delete_reminder" | "snooze_reminder" | "dismiss_reminder" => {
                let id = s(a, "id");
                let Some(i) = self.reminders.iter().position(|r| r.id == id) else {
                    return Err(err(format!(
                        "no reminder with id {id} (list_reminders shows the ids)"
                    )));
                };
                self.check_approval()?;
                match verb {
                    "delete_reminder" => {
                        self.reminders.remove(i);
                        Ok(json!({ "id": id, "deleted": true }))
                    }
                    "dismiss_reminder" => {
                        self.reminders.remove(i);
                        Ok(json!({ "id": id, "dismissed": true }))
                    }
                    _ => {
                        let minutes = a.get("minutes").and_then(Value::as_u64).unwrap_or(10);
                        self.reminders[i].bucket = "Today";
                        Ok(json!({ "id": id, "snoozed_until_iso": format!("now+{minutes}m") }))
                    }
                }
            }
            "export_note" => {
                self.note_index(s(a, "file"))?;
                if s(a, "format") == "pdf" {
                    return Err(err(
                        "export format 'pdf' is not available in this build (available: markdown)",
                    ));
                }
                self.check_approval()?;
                Ok(json!({ "path": s(a, "path") }))
            }
            // ── settings ──
            "get_preferences" => {
                let keys: Vec<Value> = PREF_KEYS
                    .iter()
                    .map(|(k, t)| json!({ "key": k, "type": t }))
                    .collect();
                Ok(json!({ "preferences": self.control.prefs, "keys": keys }))
            }
            "set_preference" => {
                let key = s(a, "key");
                let Some((_, t)) = PREF_KEYS.iter().find(|(k, _)| *k == key) else {
                    let allowed: Vec<&str> = PREF_KEYS.iter().map(|(k, _)| *k).collect();
                    return Err(err(format!(
                        "unknown or protected preference key '{key}'; settable keys: {}",
                        allowed.join(", ")
                    )));
                };
                let v = a.get("value").cloned().unwrap_or(Value::Null);
                let ok = match *t {
                    "bool" => v.is_boolean(),
                    "string" => v.as_str().is_some_and(|x| !x.trim().is_empty()),
                    _ if t.starts_with("int") => {
                        let parts: Vec<i64> = t
                            .split(':')
                            .skip(1)
                            .filter_map(|x| x.parse().ok())
                            .collect();
                        v.as_i64().is_some_and(|n| n >= parts[0] && n <= parts[1])
                    }
                    _ => v
                        .as_str()
                        .is_some_and(|x| t.trim_start_matches("enum:").split('|').any(|e| e == x)),
                };
                if !ok {
                    return Err(err(format!("{key} must match {t}")));
                }
                if self.control.prefs.get(key) == Some(&v) {
                    return Ok(json!({ "key": key, "value": v, "changed": false }));
                }
                self.check_approval()?;
                self.control.prefs.insert(key.into(), v.clone());
                Ok(json!({ "key": key, "value": v, "changed": true }))
            }
            "set_workspace_folder" => {
                let p = s(a, "path").trim_end_matches('/');
                if p.is_empty() || p == "/home/user" || p == "/home" || p == "/" {
                    return Err(err(format!(
                        "refusing to make {} the workspace folder: it is the filesystem root or your home folder",
                        if p.is_empty() { "/" } else { p }
                    )));
                }
                if ["/.ssh", "/.gnupg", "/.config", "/.aws"]
                    .iter()
                    .any(|d| p.contains(d))
                {
                    return Err(err(format!("refusing to make {p} the workspace folder: it holds credentials or app settings")));
                }
                self.check_approval()?;
                self.control.workspace = p.to_string();
                Ok(json!({ "workspace": p }))
            }
            // ── data ──
            "describe_table" => {
                self.conn_index(s(a, "connection_name"))?;
                let t = s(a, "table");
                if !["invoices", "customers"].contains(&t) {
                    return Err(err(format!("no table named {t} (list_tables shows them)")));
                }
                Ok(json!({
                    "connection_name": s(a, "connection_name"), "schema": "main", "table": t,
                    "type": "BASE TABLE", "keys_known": true, "primary_key": ["id"],
                    "columns": [
                        { "name": "id", "type": "INTEGER", "primary_key": true },
                        { "name": "name", "type": "TEXT", "primary_key": false },
                    ],
                }))
            }
            "test_connection" => {
                self.conn_index(s(a, "connection_name"))?;
                Ok(json!({ "connection_name": s(a, "connection_name"), "ok": true }))
            }
            "execute_sql" => {
                self.conn_index(s(a, "connection_name"))?;
                let sql = s(a, "sql");
                let upper = sql.to_ascii_uppercase();
                let stmts = sql.split(';').filter(|x| !x.trim().is_empty()).count();
                if stmts > 1 {
                    return Err(err(format!("execute_sql runs exactly one statement; this text has {stmts} (stacked statements are refused)")));
                }
                if upper.contains("/*")
                    || upper.contains("XP_CMDSHELL")
                    || upper.trim_start().starts_with("COPY")
                    || upper.trim_start().starts_with("ATTACH")
                    || upper.contains("READ_TEXT(")
                {
                    return Err(err(
                        "refused by the SQL gate (approval cannot override this)",
                    ));
                }
                let head = upper.split_whitespace().next().unwrap_or("");
                if ["SELECT", "WITH", "SHOW", "EXPLAIN"].contains(&head) {
                    return Err(err(
                        "this statement does not change data; run it with run_query instead",
                    ));
                }
                self.check_approval()?;
                Ok(json!({ "ok": true, "connection_name": s(a, "connection_name"), "executed": 1 }))
            }
            "add_connection" | "update_connection" => {
                let name = s(a, "name").to_string();
                let existing = self.connections.iter().position(|c| c.name == name);
                if verb == "add_connection" && existing.is_some() {
                    return Err(err(format!("a connection named '{name}' already exists")));
                }
                if verb == "update_connection" && existing.is_none() {
                    return Err(err(format!("no connection named: {name}")));
                }
                self.check_approval()?;
                let mut c = match existing {
                    Some(i) => self.connections.remove(i),
                    None => MockConnection {
                        name: name.clone(),
                        driver: String::new(),
                        host: String::new(),
                        port: 0,
                        database: String::new(),
                        username: String::new(),
                        has_password: false,
                    },
                };
                let server_changed = ["driver", "host", "port", "username"]
                    .iter()
                    .any(|k| a.get(*k).is_some());
                if let Some(v) = a.get("new_name").and_then(Value::as_str) {
                    c.name = v.into();
                }
                if let Some(v) = a.get("driver").and_then(Value::as_str) {
                    c.driver = v.into();
                }
                if let Some(v) = a.get("host").and_then(Value::as_str) {
                    c.host = v.into();
                }
                if let Some(v) = a.get("port").and_then(Value::as_i64) {
                    c.port = v;
                }
                if let Some(v) = a.get("database").and_then(Value::as_str) {
                    c.database = v.into();
                }
                if let Some(v) = a.get("username").and_then(Value::as_str) {
                    c.username = v.into();
                }
                if verb == "add_connection" || server_changed {
                    c.has_password = false;
                }
                let out = json!({
                    "name": c.name, "driver": c.driver, "host": c.host, "port": c.port,
                    "database": c.database, "username": c.username,
                    "has_password": c.has_password, "has_options": false, "saved": true,
                    "note": "No password, token or connection options are stored by MCP. If the database needs one, the user enters it in Manage Connections (open_connections_dialog opens it).",
                });
                self.connections.push(c);
                Ok(out)
            }
            "delete_connection" => {
                let i = self.conn_index(s(a, "name"))?;
                self.check_approval()?;
                let c = self.connections.remove(i);
                Ok(json!({ "deleted": c.name }))
            }
            "open_connections_dialog" | "open_schema_browser" => Ok(json!({ "opened": true })),
            // ── REST ──
            "parse_http_file" => {
                let text = match a.get("text").and_then(Value::as_str) {
                    Some(t) => t.to_string(),
                    None => {
                        let i = self.resolve(super::verbs::tab_ref(a))?;
                        self.tabs[i].content.clone()
                    }
                };
                let mut reqs = Vec::new();
                for (n, line) in text.lines().enumerate() {
                    let mut parts = line.split_whitespace();
                    let (Some(m), Some(u)) = (parts.next(), parts.next()) else {
                        continue;
                    };
                    if ["GET", "POST", "PUT", "PATCH", "DELETE", "HEAD", "OPTIONS"].contains(&m) {
                        reqs.push(json!({ "index": reqs.len(), "name": "", "method": m, "url": u, "line": n + 1 }));
                    }
                }
                Ok(json!({ "requests": reqs, "file_variables": [] }))
            }
            "curl_to_request" => {
                let t = s(a, "text");
                if !t.trim_start().starts_with("curl") {
                    return Err(err("not a curl command"));
                }
                let url = t
                    .split_whitespace()
                    .find(|w| w.starts_with("http"))
                    .unwrap_or("")
                    .trim_matches('\'');
                let method = if t.contains("-X POST") || t.contains(" -d ") {
                    "POST"
                } else {
                    "GET"
                };
                Ok(json!({ "method": method, "url": url, "headers": [], "body": "", "notes": [] }))
            }
            "request_to_curl" => {
                Ok(json!({ "text": format!("curl -X {} '{}'", s(a, "method"), s(a, "url")) }))
            }
            "rest_request" => {
                self.check_approval()?;
                Ok(json!({
                    "status": 200, "reason": "OK", "headers": ["Content-Type: application/json"],
                    "content_type": "application/json", "size": 11, "body": "{\"ok\":true}",
                    "truncated": false, "binary": false, "time_ms": 12,
                }))
            }
            // ── leftovers ──
            "hex_dump" => {
                let bytes: Vec<u8> = if let Some(p) = a.get("path").and_then(Value::as_str) {
                    if secret(p) {
                        return Err(err(format!(
                            "refusing to read {p}: it matches the credential deny-list"
                        )));
                    }
                    b"mock file bytes\n".to_vec()
                } else {
                    let i = self.resolve(super::verbs::tab_ref(a))?;
                    self.tabs[i].content.as_bytes().to_vec()
                };
                let off = a.get("offset").and_then(Value::as_u64).unwrap_or(0) as usize;
                let len = a.get("length").and_then(Value::as_u64).unwrap_or(4096) as usize;
                let chunk: Vec<u8> = bytes.iter().skip(off).take(len).copied().collect();
                let hex: Vec<String> = chunk
                    .chunks(16)
                    .enumerate()
                    .map(|(n, c)| {
                        let h: Vec<String> = c.iter().map(|b| format!("{b:02x}")).collect();
                        let asc: String = c
                            .iter()
                            .map(|&b| {
                                if (0x20..0x7f).contains(&b) {
                                    b as char
                                } else {
                                    '.'
                                }
                            })
                            .collect();
                        format!("{:08x}  {:<48} |{asc}|", off + n * 16, h.join(" "))
                    })
                    .collect();
                Ok(json!({
                    "offset": off, "length": chunk.len(), "total_size": bytes.len(),
                    "source": "mock", "hex": hex.join("\n"),
                    "truncated": off + chunk.len() < bytes.len(),
                }))
            }
            "clear_recent_files" => {
                if self.control.recent_cleared {
                    return Ok(json!({ "cleared": 0 }));
                }
                self.check_approval()?;
                self.control.recent_cleared = true;
                Ok(json!({ "cleared": 2 }))
            }
            other => Err(err(format!("unknown verb: {other}"))),
        }
    }
}
