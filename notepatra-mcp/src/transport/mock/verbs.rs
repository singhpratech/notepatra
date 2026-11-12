// SPDX-License-Identifier: GPL-3.0-or-later
//! Mock answers for the verbs reached through
//! [`EditorTransport::call_verb`](super::super::EditorTransport::call_verb)
//! (v0.1.133). Result SHAPES mirror the C++ bridge (src/mcp_bridge.cpp);
//! the transforms are simple stand-ins for the editor's real engines, and
//! every result is labelled as mock data by the server.

use std::collections::{BTreeSet, HashMap};

use serde_json::{json, Map, Value};

use super::{EditorTransport, MockEditor, TabRef, TabSelector, TransportError};

#[derive(Default)]
pub(super) struct State {
    tabs: HashMap<i64, TabExtra>,
    view: Map<String, Value>,
}

#[derive(Default)]
struct TabExtra {
    read_only: bool,
    bookmarks: BTreeSet<usize>,
    eol: Option<String>,
    encoding: Option<String>,
    undo: Vec<String>,
    redo: Vec<String>,
}

fn err(msg: impl Into<String>) -> TransportError {
    TransportError(msg.into())
}

fn s<'a>(a: &'a Value, k: &str) -> &'a str {
    a.get(k).and_then(Value::as_str).unwrap_or("")
}

fn b(a: &Value, k: &str, def: bool) -> bool {
    a.get(k).and_then(Value::as_bool).unwrap_or(def)
}

fn n(a: &Value, k: &str, def: i64) -> i64 {
    a.get(k).and_then(Value::as_i64).unwrap_or(def)
}

/// tab_id / tab_index (or the bridge's "index") from an args object.
pub(super) fn tab_ref(a: &Value) -> TabRef {
    TabRef {
        index: a
            .get("tab_index")
            .or_else(|| a.get("index"))
            .and_then(Value::as_u64)
            .map(|i| i as usize),
        id: a.get("tab_id").and_then(Value::as_i64),
    }
}

/// 1-based column of the first literal match, honouring case / whole word.
pub(super) fn find_col(line: &str, q: &str, match_case: bool, whole_word: bool) -> Option<usize> {
    if q.is_empty() {
        return None;
    }
    let (hay, needle) = if match_case {
        (line.to_string(), q.to_string())
    } else {
        (line.to_lowercase(), q.to_lowercase())
    };
    let is_word = |c: char| c.is_alphanumeric() || c == '_';
    let mut from = 0;
    while let Some(off) = hay[from..].find(&needle) {
        let at = from + off;
        let end = at + needle.len();
        let left_ok = hay[..at].chars().next_back().is_none_or(|c| !is_word(c));
        let right_ok = hay[end..].chars().next().is_none_or(|c| !is_word(c));
        if !whole_word || (left_ok && right_ok) {
            return Some(hay[..at].chars().count() + 1);
        }
        from = at + hay[at..].chars().next().map_or(1, char::len_utf8);
    }
    None
}

fn line_op(op: &str, text: &str) -> String {
    let trailing_nl = text.ends_with('\n');
    let mut lines: Vec<String> = text.lines().map(str::to_string).collect();
    match op {
        "sort_asc" => lines.sort(),
        "sort_desc" => {
            lines.sort();
            lines.reverse();
        }
        "sort_int_asc" | "sort_int_desc" => {
            lines.sort_by_key(|l| l.trim().parse::<i64>().unwrap_or(i64::MAX));
            if op == "sort_int_desc" {
                lines.reverse();
            }
        }
        "sort_len_asc" => lines.sort_by_key(|l| l.chars().count()),
        "sort_len_desc" => lines.sort_by_key(|l| std::cmp::Reverse(l.chars().count())),
        "dedupe" => {
            let mut seen = std::collections::HashSet::new();
            lines.retain(|l| seen.insert(l.clone()));
        }
        "dedupe_consecutive" => lines.dedup(),
        "remove_empty" => lines.retain(|l| !l.is_empty()),
        "remove_blank" => lines.retain(|l| !l.trim().is_empty()),
        "join" => return lines.iter().map(|l| l.trim()).collect::<Vec<_>>().join(" "),
        "reverse" => lines.reverse(),
        "trim_trailing" => lines.iter_mut().for_each(|l| *l = l.trim_end().into()),
        "trim_leading" => lines.iter_mut().for_each(|l| *l = l.trim_start().into()),
        "trim_both" => lines.iter_mut().for_each(|l| *l = l.trim().into()),
        "tab_to_space" => lines.iter_mut().for_each(|l| *l = l.replace('\t', "    ")),
        "space_to_tab" => lines.iter_mut().for_each(|l| *l = l.replace("    ", "\t")),
        _ => {}
    }
    let mut out = lines.join("\n");
    if trailing_nl {
        out.push('\n');
    }
    out
}

fn convert_case(mode: &str, text: &str) -> String {
    match mode {
        "upper" => text.to_uppercase(),
        "lower" => text.to_lowercase(),
        "invert" => text
            .chars()
            .map(|c| {
                if c.is_uppercase() {
                    c.to_lowercase().next().unwrap_or(c)
                } else {
                    c.to_uppercase().next().unwrap_or(c)
                }
            })
            .collect(),
        // proper / sentence: capitalise the first letter of each word / text.
        "proper" => text
            .split(' ')
            .map(|w| {
                let mut c = w.chars();
                c.next()
                    .map(|f| f.to_uppercase().collect::<String>() + &c.as_str().to_lowercase())
                    .unwrap_or_default()
            })
            .collect::<Vec<_>>()
            .join(" "),
        _ => {
            let lower = text.to_lowercase();
            let mut c = lower.chars();
            c.next()
                .map(|f| f.to_uppercase().collect::<String>() + c.as_str())
                .unwrap_or_default()
        }
    }
}

const B64: &[u8; 64] = b"ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

fn base64_encode(input: &[u8]) -> String {
    let mut out = String::new();
    for chunk in input.chunks(3) {
        let v = (u32::from(chunk[0]) << 16)
            | (u32::from(*chunk.get(1).unwrap_or(&0)) << 8)
            | u32::from(*chunk.get(2).unwrap_or(&0));
        for i in 0..4 {
            if i <= chunk.len() {
                out.push(B64[((v >> (18 - 6 * i)) & 63) as usize] as char);
            } else {
                out.push('=');
            }
        }
    }
    out
}

fn base64_decode(input: &str) -> Result<String, TransportError> {
    let mut bits = 0u32;
    let mut nbits = 0;
    let mut out = Vec::new();
    for c in input
        .bytes()
        .filter(|c| !c.is_ascii_whitespace() && *c != b'=')
    {
        let v = B64
            .iter()
            .position(|x| *x == c)
            .ok_or_else(|| err("invalid base64"))?;
        bits = (bits << 6) | v as u32;
        nbits += 6;
        if nbits >= 8 {
            nbits -= 8;
            out.push((bits >> nbits) as u8);
        }
    }
    String::from_utf8(out).map_err(|_| err("decoded bytes are not UTF-8"))
}

fn url_encode(input: &str) -> String {
    input
        .bytes()
        .map(|c| match c {
            b'A'..=b'Z' | b'a'..=b'z' | b'0'..=b'9' | b'-' | b'_' | b'.' | b'~' => {
                (c as char).to_string()
            }
            _ => format!("%{c:02X}"),
        })
        .collect()
}

fn url_decode(input: &str) -> String {
    let b = input.as_bytes();
    let mut out = Vec::new();
    let mut i = 0;
    while i < b.len() {
        if b[i] == b'%' && i + 2 < b.len() {
            if let Ok(v) = u8::from_str_radix(&input[i + 1..i + 3], 16) {
                out.push(v);
                i += 3;
                continue;
            }
        }
        out.push(if b[i] == b'+' { b' ' } else { b[i] });
        i += 1;
    }
    String::from_utf8_lossy(&out).into_owned()
}

/// Not a real digest: the mock has no hash crates. Right length, stable.
fn fake_digest(text: &str, algo: &str) -> String {
    let len = match algo {
        "md5" => 32,
        "sha1" => 40,
        "sha512" => 128,
        _ => 64,
    };
    let mut h: u64 = 0xcbf2_9ce4_8422_2325;
    let mut out = String::new();
    while out.len() < len {
        for byte in text.bytes() {
            h ^= u64::from(byte);
            h = h.wrapping_mul(0x0100_0000_01b3);
        }
        h = h.wrapping_add(out.len() as u64);
        out.push_str(&format!("{h:016x}"));
    }
    out.truncate(len);
    out
}

fn bracket_report(text: &str) -> String {
    let mut stack = Vec::new();
    let mut report = String::new();
    for (ln, line) in text.lines().enumerate() {
        for (col, c) in line.chars().enumerate() {
            match c {
                '(' | '[' | '{' => stack.push((c, ln + 1, col + 1)),
                ')' | ']' | '}' => {
                    let want = match c {
                        ')' => '(',
                        ']' => '[',
                        _ => '{',
                    };
                    if stack.last().map(|x| x.0) == Some(want) {
                        stack.pop();
                    } else {
                        report.push_str(&format!(
                            "Line {}, Col {}: Unexpected '{c}'\n",
                            ln + 1,
                            col + 1
                        ));
                    }
                }
                _ => {}
            }
        }
    }
    for (c, l, col) in stack {
        report.push_str(&format!("Line {l}, Col {col}: Unclosed '{c}'\n"));
    }
    if report.is_empty() {
        report.push_str("No bracket problems found.\n");
    }
    report
}

fn fix_brackets(text: &str) -> String {
    let mut stack = Vec::new();
    for c in text.chars() {
        match c {
            '(' => stack.push(')'),
            '[' => stack.push(']'),
            '{' => stack.push('}'),
            ')' | ']' | '}' if stack.last() == Some(&c) => {
                stack.pop();
            }
            _ => {}
        }
    }
    let mut out = text.to_string();
    while let Some(c) = stack.pop() {
        out.push(c);
    }
    out
}

impl MockEditor {
    fn extra(&mut self, i: usize) -> &mut TabExtra {
        let id = self.tabs[i].id;
        self.v133.tabs.entry(id).or_default()
    }

    /// Records the pre-edit text so undo/redo have something to restore.
    fn edit_tab(&mut self, i: usize, new_text: String) -> bool {
        if self.tabs[i].content == new_text {
            return false;
        }
        let old = std::mem::replace(&mut self.tabs[i].content, new_text);
        self.tabs[i].modified = true;
        let x = self.extra(i);
        x.undo.push(old);
        x.redo.clear();
        true
    }

    fn with_tab(&self, mut r: Value, i: usize) -> Value {
        r["tab_index"] = json!(i);
        r["tab_id"] = json!(self.tabs[i].id);
        r
    }

    fn editable(&self, i: usize) -> Result<(), TransportError> {
        if self.tabs[i].editable {
            Ok(())
        } else {
            Err(err(format!(
                "tab {i} is not an editable text buffer (it is \"{}\")",
                self.tabs[i].title
            )))
        }
    }

    /// `text` or a tab's buffer, as the bridge's textOrTab does.
    fn text_or_tab(&self, a: &Value) -> Result<(String, Option<usize>), TransportError> {
        if let Some(t) = a.get("text").and_then(Value::as_str) {
            return Ok((t.to_string(), None));
        }
        let i = self.resolve(tab_ref(a))?;
        self.editable(i)?;
        Ok((self.tabs[i].content.clone(), Some(i)))
    }

    pub(super) fn mock_verb(&mut self, verb: &str, a: &Value) -> Result<Value, TransportError> {
        match verb {
            // ── extended forms of existing tools ──
            "new_tab" => {
                let mut r = self.new_tab(a.get("text").and_then(Value::as_str))?;
                let i = self.tabs.len() - 1;
                if let Some(l) = a.get("language").and_then(Value::as_str) {
                    self.tabs[i].language = l.to_string();
                    r["language"] = json!(l);
                }
                if let Some(t) = a.get("title").and_then(Value::as_str) {
                    self.tabs[i].title = t.to_string();
                    r["title"] = json!(t);
                }
                Ok(r)
            }
            "create_diagram" => {
                let src = match s(a, "template") {
                    "er" => "diagram er\nentity user\n",
                    "system" => "diagram system\nservice api\n",
                    _ => "diagram flow\nnode start (Start)\n",
                };
                self.create_diagram(Some(src), a.get("title").and_then(Value::as_str))
            }
            "format_text" => {
                let kind = s(a, "kind").to_string();
                let text = s(a, "text").to_string();
                if kind == "html" && b(a, "minify", false) {
                    let min: Vec<&str> = text.lines().map(str::trim).collect();
                    return Ok(json!({ "text": min.concat() }));
                }
                if kind == "json" && b(a, "minify", false) {
                    let v: Value = serde_json::from_str(&text)
                        .map_err(|e| err(format!("invalid JSON: {e}")))?;
                    return Ok(json!({ "text": v.to_string() }));
                }
                let mut r = self.format_text(&kind, &text)?;
                if kind == "sql" && s(a, "keyword_case") == "lower" {
                    let t = r["text"].as_str().unwrap_or("").to_lowercase();
                    r["text"] = json!(t);
                }
                Ok(r)
            }
            "find_in_tab" => {
                let i = match a.get("title").and_then(Value::as_str) {
                    Some(t) => self.resolve_selector(TabSelector::Title(t))?,
                    None => self.resolve(tab_ref(a))?,
                };
                let q = s(a, "query");
                let regex = b(a, "regex", false);
                if regex {
                    super::validate_regex(q)?;
                }
                let mc = b(a, "match_case", regex);
                let ww = b(a, "whole_word", false);
                let matches: Vec<Value> = self.tabs[i]
                    .content
                    .lines()
                    .enumerate()
                    .filter_map(|(n, l)| {
                        find_col(l, q, mc, ww)
                            .map(|c| json!({ "line": n + 1, "column": c, "text": l.trim() }))
                    })
                    .take(super::FIND_IN_TAB_CAP)
                    .collect();
                Ok(json!({ "matches": matches, "truncated": false }))
            }
            "search_project" => {
                let q = s(a, "query");
                let regex = b(a, "regex", false);
                if regex {
                    super::validate_regex(q)?;
                }
                let mc = b(a, "match_case", regex);
                let ww = b(a, "whole_word", false);
                let max = n(a, "max_results", 50).max(1) as usize;
                let mut results = Vec::new();
                for t in &self.tabs {
                    for (n, l) in t.content.lines().enumerate() {
                        if let Some(c) = find_col(l, q, mc, ww) {
                            results.push(json!({
                                "path": t.path.clone().unwrap_or_default(),
                                "line": n + 1, "column": c, "text": l.trim(),
                            }));
                        }
                    }
                }
                let truncated = results.len() >= max;
                results.truncate(max);
                Ok(json!({
                    "results": results, "truncated": truncated,
                    "workspace_searched": false, "scope": "open_tabs_only",
                    "notice": "Mock editor: only the in-memory demo tabs were searched.",
                }))
            }
            "apply_edit" => self.mock_apply_edit(a),
            // ── tabs and files ──
            "activate_tab" => {
                let i = self.resolve(tab_ref(a))?;
                self.selection = (i, String::new());
                Ok(self.with_tab(json!({ "active": true }), i))
            }
            "close_tab" => {
                let i = self.resolve(tab_ref(a))?;
                let id = self.tabs[i].id;
                if self.tabs[i].modified {
                    if !b(a, "discard_unsaved", false) {
                        return Err(err(format!(
                            "tab '{}' has unsaved changes. Save it first (save_tab), or pass \
                             discard_unsaved:true to ask the user to approve losing them.",
                            self.tabs[i].title
                        )));
                    }
                    self.check_approval()?;
                }
                self.tabs.remove(i);
                if self.selection.0 >= self.tabs.len() {
                    self.selection = (0, String::new());
                }
                if self.tabs.is_empty() {
                    self.new_tab(None)?;
                }
                Ok(json!({ "closed": true, "tab_index": i, "tab_id": id }))
            }
            "reload_tab" => {
                let i = self.resolve(tab_ref(a))?;
                if self.tabs[i].path.is_none() {
                    return Err(err(format!(
                        "tab '{}' has no file on disk",
                        self.tabs[i].title
                    )));
                }
                if self.tabs[i].modified {
                    self.check_approval()?;
                }
                self.tabs[i].modified = false;
                Ok(self.with_tab(json!({ "reloaded": true }), i))
            }
            "rename_file" => {
                let i = self.resolve(tab_ref(a))?;
                let new_name = s(a, "new_name").to_string();
                let Some(path) = self.tabs[i].path.clone() else {
                    return Err(err("the tab has no file on disk to rename"));
                };
                let dir = path.rsplit_once('/').map_or("", |(d, _)| d).to_string();
                let dest = format!("{dir}/{new_name}");
                if self
                    .tabs
                    .iter()
                    .any(|t| t.path.as_deref() == Some(dest.as_str()))
                {
                    return Err(err(format!(
                        "a file named '{new_name}' already exists in that folder"
                    )));
                }
                self.check_approval()?;
                self.tabs[i].path = Some(dest.clone());
                self.tabs[i].title = new_name;
                self.tabs[i].modified = false;
                Ok(self.with_tab(json!({ "path": dest }), i))
            }
            "save_all" => {
                let mut untitled = Vec::new();
                let mut targets = Vec::new();
                for (i, t) in self.tabs.iter().enumerate() {
                    if !t.modified || !t.editable {
                        continue;
                    }
                    match &t.path {
                        Some(p) => targets.push((i, p.clone())),
                        None => untitled.push(t.title.clone()),
                    }
                }
                if !targets.is_empty() {
                    self.check_approval()?;
                }
                let saved: Vec<String> = targets
                    .into_iter()
                    .map(|(i, p)| {
                        self.tabs[i].modified = false;
                        p
                    })
                    .collect();
                Ok(json!({
                    "saved": saved, "failed": [],
                    "skipped_untitled": untitled, "skipped_protected": [],
                }))
            }
            "set_read_only" => {
                let i = self.resolve(tab_ref(a))?;
                self.editable(i)?;
                let on = b(a, "on", false);
                self.extra(i).read_only = on;
                Ok(self.with_tab(json!({ "read_only": on }), i))
            }
            // ── editing (approval-gated in the editor) ──
            "undo" | "redo" => {
                let i = self.resolve(tab_ref(a))?;
                self.editable(i)?;
                self.check_approval()?;
                let steps = n(a, "steps", 1).max(1) as usize;
                let mut done = 0;
                for _ in 0..steps {
                    let x = self.extra(i);
                    let popped = if verb == "undo" {
                        x.undo.pop()
                    } else {
                        x.redo.pop()
                    };
                    let Some(text) = popped else { break };
                    let cur = std::mem::replace(&mut self.tabs[i].content, text);
                    let x = self.extra(i);
                    if verb == "undo" {
                        x.redo.push(cur);
                    } else {
                        x.undo.push(cur);
                    }
                    done += 1;
                }
                if done == 0 {
                    return Err(err(format!("nothing to {verb}")));
                }
                self.tabs[i].modified = true;
                Ok(self.with_tab(json!({ "steps": done }), i))
            }
            "transform_lines" | "convert_case" => {
                let i = self.resolve(tab_ref(a))?;
                self.editable(i)?;
                let default_scope = if verb == "convert_case" {
                    "selection"
                } else {
                    "document"
                };
                let scope = a
                    .get("scope")
                    .and_then(Value::as_str)
                    .unwrap_or(default_scope);
                let content = self.tabs[i].content.clone();
                let target = if scope == "selection" {
                    let sel = self.selection.1.clone();
                    if self.selection.0 != i || sel.is_empty() || !content.contains(&sel) {
                        return Err(err("no selection"));
                    }
                    sel
                } else {
                    content.clone()
                };
                let out = if verb == "convert_case" {
                    convert_case(s(a, "mode"), &target)
                } else {
                    line_op(s(a, "op"), &target)
                };
                self.check_approval()?;
                let new_text = if scope == "selection" {
                    content.replacen(&target, &out, 1)
                } else {
                    out
                };
                let changed = self.edit_tab(i, new_text);
                Ok(self.with_tab(json!({ "changed": changed }), i))
            }
            "comment_lines" => {
                let i = self.resolve(tab_ref(a))?;
                self.editable(i)?;
                let (from, to) = (n(a, "start_line", 1) as usize, n(a, "end_line", 1) as usize);
                let mut lines: Vec<String> =
                    self.tabs[i].content.lines().map(str::to_string).collect();
                if to > lines.len() || from < 1 || to < from {
                    return Err(err(format!(
                        "lines {from}-{to} are outside the document ({} lines)",
                        lines.len()
                    )));
                }
                let marker = if self.tabs[i].language == "Python" {
                    "#"
                } else {
                    "//"
                };
                self.check_approval()?;
                let all_commented = lines[from - 1..to]
                    .iter()
                    .all(|l| l.trim_start().starts_with(marker));
                let action = a.get("action").and_then(Value::as_str).unwrap_or("toggle");
                let uncomment = action == "uncomment" || (action == "toggle" && all_commented);
                for l in &mut lines[from - 1..to] {
                    if uncomment {
                        *l = l.replacen(&format!("{marker} "), "", 1);
                    } else if !l.trim_start().starts_with(marker) {
                        *l = format!("{marker} {l}");
                    }
                }
                let mut text = lines.join("\n");
                if self.tabs[i].content.ends_with('\n') {
                    text.push('\n');
                }
                let changed = self.edit_tab(i, text);
                Ok(self.with_tab(json!({ "changed": changed }), i))
            }
            "line_edit" => {
                let i = self.resolve(tab_ref(a))?;
                self.editable(i)?;
                let l = n(a, "line", 1) as usize;
                let mut lines: Vec<String> =
                    self.tabs[i].content.lines().map(str::to_string).collect();
                if l < 1 || l > lines.len() {
                    return Err(err(format!(
                        "line {l} is past the end ({} lines)",
                        lines.len()
                    )));
                }
                let op = s(a, "op");
                if (op == "move_up" && l == 1) || (op == "move_down" && l == lines.len()) {
                    return Err(err(format!("line {l} cannot move further")));
                }
                self.check_approval()?;
                match op {
                    "duplicate" => lines.insert(l, lines[l - 1].clone()),
                    "delete" => {
                        lines.remove(l - 1);
                    }
                    "move_up" => lines.swap(l - 2, l - 1),
                    _ => lines.swap(l - 1, l),
                }
                let count = lines.len();
                let mut text = lines.join("\n");
                if self.tabs[i].content.ends_with('\n') {
                    text.push('\n');
                }
                self.edit_tab(i, text);
                Ok(self.with_tab(json!({ "line_count": count }), i))
            }
            "set_eol" => {
                let i = self.resolve(tab_ref(a))?;
                self.editable(i)?;
                self.check_approval()?;
                let name = match s(a, "eol") {
                    "crlf" => "Windows (CR LF)",
                    "cr" => "Macintosh (CR)",
                    _ => "Unix (LF)",
                };
                self.extra(i).eol = Some(name.into());
                if b(a, "convert", true) {
                    self.tabs[i].modified = true;
                }
                Ok(self.with_tab(json!({ "eol": name }), i))
            }
            "set_encoding" => {
                let i = self.resolve(tab_ref(a))?;
                self.editable(i)?;
                let enc = s(a, "encoding").to_string();
                if s(a, "mode") == "reinterpret" {
                    if self.tabs[i].path.is_none() {
                        return Err(err(
                            "reinterpret needs a file on disk; this tab has never been saved",
                        ));
                    }
                    if self.tabs[i].modified && !b(a, "discard_unsaved", false) {
                        return Err(err(format!(
                            "tab '{}' has unsaved changes and reinterpret reloads it from \
                             disk. Pass discard_unsaved:true to ask the user to approve losing \
                             them.",
                            self.tabs[i].title
                        )));
                    }
                    self.check_approval()?;
                    self.tabs[i].modified = false;
                } else {
                    self.check_approval()?;
                    self.tabs[i].modified = true;
                }
                self.extra(i).encoding = Some(enc.clone());
                Ok(self.with_tab(json!({ "encoding": enc }), i))
            }
            // ── view ──
            "bookmarks" => {
                let i = self.resolve(tab_ref(a))?;
                self.editable(i)?;
                let lines = self.tabs[i].content.lines().count();
                let mut r = json!({});
                match s(a, "action") {
                    "toggle" => {
                        let l = n(a, "line", 1) as usize;
                        if l > lines {
                            return Err(err(format!("line {l} is past the end ({lines} lines)")));
                        }
                        let x = self.extra(i);
                        let on = if x.bookmarks.remove(&l) {
                            false
                        } else {
                            x.bookmarks.insert(l);
                            true
                        };
                        r["line"] = json!(l);
                        r["bookmarked"] = json!(on);
                    }
                    "clear" => self.extra(i).bookmarks.clear(),
                    _ => {}
                }
                let marks: Vec<usize> = self.extra(i).bookmarks.iter().copied().collect();
                r["lines"] = json!(marks);
                Ok(self.with_tab(r, i))
            }
            "fold" => {
                let i = self.resolve(tab_ref(a))?;
                self.editable(i)?;
                Ok(self.with_tab(json!({ "action": s(a, "action") }), i))
            }
            "set_preview" => {
                let i = self.resolve(tab_ref(a))?;
                Ok(self.with_tab(json!({ "on": b(a, "on", false) }), i))
            }
            "compare_with_file" => {
                let i = self.resolve(tab_ref(a))?;
                self.editable(i)?;
                let p = s(a, "path");
                if !p.starts_with('/') && !p.contains(":\\") {
                    return Err(err("path must be absolute"));
                }
                if p.contains("/.ssh/") || p.ends_with(".pem") || p.ends_with("/.env") {
                    return Err(err(format!(
                        "refusing to read {p}: it matches the credential deny-list"
                    )));
                }
                Ok(self.with_tab(json!({ "opened": true }), i))
            }
            "set_view" => {
                let obj = a.as_object().cloned().unwrap_or_default();
                for (k, v) in obj {
                    self.v133.view.insert(k, v);
                }
                Ok(Value::Object(self.v133.view.clone()))
            }
            "set_theme" => Ok(json!({ "theme": s(a, "theme") })),
            // ── pure tools ──
            "check_brackets" => {
                let (t, i) = self.text_or_tab(a)?;
                self.pure(json!({ "report": bracket_report(&t) }), i)
            }
            "fix_brackets" => {
                let (t, i) = self.text_or_tab(a)?;
                let fixed = fix_brackets(&t);
                self.pure(
                    json!({ "fixed": fixed, "changed": fixed != t, "report": bracket_report(&t) }),
                    i,
                )
            }
            "hash_text" => {
                let (t, i) = self.text_or_tab(a)?;
                let algo = a.get("algo").and_then(Value::as_str).unwrap_or("sha256");
                self.pure(json!({ "algo": algo, "hash": fake_digest(&t, algo) }), i)
            }
            "encode_text" => {
                let (t, i) = self.text_or_tab(a)?;
                let out = match s(a, "op") {
                    "base64_encode" => base64_encode(t.as_bytes()),
                    "base64_decode" => base64_decode(&t)?,
                    "url_encode" => url_encode(&t),
                    _ => url_decode(&t),
                };
                self.pure(json!({ "text": out }), i)
            }
            "diff_text" => {
                let mut sides = Vec::new();
                for side in ["left", "right"] {
                    let v = a.get(side).cloned().unwrap_or(Value::Null);
                    sides.push(
                        self.text_or_tab(&v)
                            .map_err(|e| err(format!("{side}: {}", e.0)))?
                            .0,
                    );
                }
                let (l, r): (Vec<&str>, Vec<&str>) =
                    (sides[0].lines().collect(), sides[1].lines().collect());
                let mut hunks = Vec::new();
                let (mut added, mut removed) = (0, 0);
                for k in 0..l.len().max(r.len()) {
                    let (a_, b_) = (l.get(k), r.get(k));
                    if a_ == b_ {
                        continue;
                    }
                    let mut lines = Vec::new();
                    if let Some(x) = a_ {
                        lines.push(json!({ "op": "-", "text": x }));
                        removed += 1;
                    }
                    if let Some(y) = b_ {
                        lines.push(json!({ "op": "+", "text": y }));
                        added += 1;
                    }
                    hunks.push(json!({
                        "left_line": if a_.is_some() { k + 1 } else { 0 },
                        "right_line": if b_.is_some() { k + 1 } else { 0 },
                        "lines": lines,
                    }));
                }
                Ok(json!({
                    "identical": hunks.is_empty(), "added": added, "removed": removed,
                    "hunks": hunks, "truncated": false,
                }))
            }
            "repair_json" => {
                let (t, i) = self.text_or_tab(a)?;
                // Like the editor: only an unambiguous fault (a trailing comma)
                // is repaired; missing closers are declined with a report.
                let (fixed, report) = match serde_json::from_str::<Value>(&t) {
                    Ok(_) => (t.clone(), "No problems found.".to_string()),
                    Err(_) => {
                        let trimmed = t.replace(",]", "]").replace(",}", "}");
                        if serde_json::from_str::<Value>(&trimmed).is_ok() {
                            (trimmed, "Removed trailing commas.".to_string())
                        } else {
                            (t.clone(), format!("Declined: {}", bracket_report(&t)))
                        }
                    }
                };
                let repaired = fixed != t && serde_json::from_str::<Value>(&fixed).is_ok();
                self.pure(
                    json!({ "fixed": fixed, "repaired": repaired, "report": report }),
                    i,
                )
            }
            "markdown_convert" => {
                let (t, i) = self.text_or_tab(a)?;
                let out = match s(a, "op") {
                    "bold" => format!("**{t}**"),
                    "italic" => format!("*{t}*"),
                    "link" => format!("[{t}](url)"),
                    "heading" => format!("## {t}"),
                    "code_block" => format!("```\n{t}\n```"),
                    "list" => t
                        .lines()
                        .filter(|l| !l.is_empty())
                        .map(|l| format!("- {}\n", l.trim()))
                        .collect(),
                    "table" => {
                        let rows: Vec<&str> = t.lines().filter(|l| !l.is_empty()).collect();
                        if rows.is_empty() {
                            return Err(err("nothing to convert"));
                        }
                        let mut out = String::new();
                        for (k, row) in rows.iter().enumerate() {
                            let cols: Vec<&str> = row.split([',', '\t']).collect();
                            out.push_str(&format!("| {} |\n", cols.join(" | ")));
                            if k == 0 {
                                out.push('|');
                                out.push_str(&" --- |".repeat(cols.len()));
                                out.push('\n');
                            }
                        }
                        out
                    }
                    _ => {
                        let mut out = String::new();
                        let mut in_tag = false;
                        for c in t.chars() {
                            match c {
                                '<' => in_tag = true,
                                '>' => in_tag = false,
                                _ if !in_tag => out.push(c),
                                _ => {}
                            }
                        }
                        out
                    }
                };
                self.pure(json!({ "text": out }), i)
            }
            "mermaid_to_npd" => {
                let (t, i) = self.text_or_tab(a)?;
                let body: String = t
                    .lines()
                    .skip(1)
                    .map(|l| format!("# {}\n", l.trim()))
                    .collect();
                self.pure(json!({ "source": format!("diagram flow\n{body}") }), i)
            }
            "list_symbols" => {
                let (t, i) = self.text_or_tab(a)?;
                let lang = a
                    .get("language")
                    .and_then(Value::as_str)
                    .map(str::to_string)
                    .or_else(|| i.map(|i| self.tabs[i].language.clone()))
                    .unwrap_or_default();
                let mut syms = Vec::new();
                for (k, l) in t.lines().enumerate() {
                    let l = l.trim_start();
                    for (kw, kind) in [
                        ("class ", "class"),
                        ("struct ", "class"),
                        ("fn ", "function"),
                        ("def ", "function"),
                        ("function ", "function"),
                    ] {
                        if let Some(rest) = l.strip_prefix(kw) {
                            let name: String = rest
                                .chars()
                                .take_while(|c| c.is_alphanumeric() || *c == '_')
                                .collect();
                            if !name.is_empty() {
                                syms.push(json!({ "name": name, "kind": kind, "line": k + 1 }));
                            }
                            break;
                        }
                    }
                }
                self.pure(json!({ "language": lang, "symbols": syms }), i)
            }
            "generate_password" => {
                let count = n(a, "count", 1).max(1) as usize;
                let mut seed = std::time::SystemTime::now()
                    .duration_since(std::time::UNIX_EPOCH)
                    .map_or(7, |d| d.as_nanos() as u64);
                let mut next = move || {
                    seed ^= seed << 13;
                    seed ^= seed >> 7;
                    seed ^= seed << 17;
                    seed
                };
                let pw: Vec<String> = (0..count)
                    .map(|_| {
                        if s(a, "mode") == "passphrase" {
                            let words = ["correct", "horse", "battery", "staple", "orbit", "maple"];
                            (0..n(a, "words", 6))
                                .map(|_| words[(next() % 6) as usize])
                                .collect::<Vec<_>>()
                                .join(a.get("separator").and_then(Value::as_str).unwrap_or("-"))
                        } else {
                            let alphabet =
                                b"abcdefghijkmnpqrstuvwxyzABCDEFGHJKLMNPQRSTUVWXYZ23456789!#%+";
                            (0..n(a, "length", 20))
                                .map(|_| {
                                    alphabet[(next() % alphabet.len() as u64) as usize] as char
                                })
                                .collect()
                        }
                    })
                    .collect();
                Ok(json!({ "passwords": pw }))
            }
            "git_diff" => {
                let mut r = self.git_diff(a.get("path").and_then(Value::as_str))?;
                if b(a, "staged", false) {
                    r["staged"] = json!(true);
                }
                Ok(r)
            }
            other => self.mock_control(other, a),
        }
    }

    fn pure(&self, r: Value, tab: Option<usize>) -> Result<Value, TransportError> {
        Ok(match tab {
            Some(i) => self.with_tab(r, i),
            None => r,
        })
    }

    fn mock_apply_edit(&mut self, a: &Value) -> Result<Value, TransportError> {
        let find = s(a, "find").to_string();
        let repl = s(a, "replace").to_string();
        let all = b(a, "all", false);
        let regex = b(a, "regex", false);
        if regex {
            super::validate_regex(&find)?;
        }
        let mc = b(a, "match_case", true);
        let ww = b(a, "whole_word", false);
        let targets: Vec<usize> = if s(a, "scope") == "all_open" {
            (0..self.tabs.len())
                .filter(|&i| self.tabs[i].editable)
                .collect()
        } else {
            let i = self.resolve(tab_ref(a))?;
            self.editable(i)?;
            vec![i]
        };
        // Literal stand-in for both modes: the replacement is inserted as-is.
        let count_in = |text: &str| -> usize {
            text.lines()
                .map(|l| {
                    let mut c = 0;
                    let mut rest = l.to_string();
                    while let Some(col) = find_col(&rest, &find, mc, ww) {
                        c += 1;
                        let byte = rest.char_indices().nth(col - 1).map_or(0, |(b, _)| b);
                        rest = rest[byte + find.len()..].to_string();
                    }
                    c
                })
                .sum()
        };
        let counts: Vec<(usize, usize)> = targets
            .iter()
            .map(|&i| (i, count_in(&self.tabs[i].content)))
            .filter(|(_, c)| *c > 0)
            .collect();
        if counts.is_empty() {
            return Err(err("no match"));
        }
        self.check_approval()?;
        let mut tabs = Vec::new();
        let mut total = 0;
        for (i, c) in counts {
            let text = self.tabs[i].content.clone();
            let new_text = if mc && !ww {
                if all {
                    text.replace(&find, &repl)
                } else {
                    text.replacen(&find, &repl, 1)
                }
            } else {
                // Case-insensitive / whole-word: rebuild line by line.
                let mut out = String::new();
                let mut left = if all { usize::MAX } else { 1 };
                for line in text.split_inclusive('\n') {
                    let mut l = line.to_string();
                    let mut from = 0;
                    while left > 0 {
                        let Some(col) = find_col(&l[from..], &find, mc, ww) else {
                            break;
                        };
                        let at = from + l[from..].char_indices().nth(col - 1).map_or(0, |(b, _)| b);
                        l.replace_range(at..at + find.len(), &repl);
                        from = at + repl.len();
                        left -= 1;
                    }
                    out.push_str(&l);
                }
                out
            };
            let done = if all { c } else { 1 };
            self.edit_tab(i, new_text);
            total += done;
            tabs.push(json!({ "tab_index": i, "tab_id": self.tabs[i].id, "title": self.tabs[i].title, "count": done }));
        }
        let mut r = json!({ "count": total });
        if s(a, "scope") == "all_open" {
            r["tabs"] = json!(tabs);
        } else if let Some(t) = tabs.first() {
            r["tab_index"] = t["tab_index"].clone();
            r["tab_id"] = t["tab_id"].clone();
        }
        Ok(r)
    }
}
