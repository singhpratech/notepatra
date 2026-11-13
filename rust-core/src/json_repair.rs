// SPDX-License-Identifier: GPL-3.0-or-later

//! Conservative JSON repair.
//!
//! The rule: a repair may only make a change whose meaning is not in doubt.
//! Everything is done as small edits on the ORIGINAL text (the rest of the
//! document, including its formatting, is untouched), every edit is listed
//! in the report with its line and column, and the result is re-validated by
//! the strict scanner before it is returned. When the right repair is a
//! guess, nothing is changed and the report says exactly what and where.
//!
//! Repaired (unambiguous):
//!   trailing comma before `]` / `}`          → removed
//!   missing comma between two values/members → inserted
//!   missing colon between a key and a value  → inserted
//!   'single-quoted' and “curly-quoted” strings → "double-quoted"
//!                                              (content re-escaped)
//!   unquoted identifier keys `{a: 1}`         → `{"a": 1}`
//!   // and /* */ comments                     → removed
//!   Python True / False / None                → true / false / null
//!   raw control characters inside strings     → escaped (\n, \t, \u0001)
//!   `\'` inside a string                       → `'`
//!   a leading byte-order mark                  → kept (it is accepted)
//!
//! Declined (ambiguous, reported with position):
//!   unclosed brackets at the end of the document (missing closer, or a
//!   truncated file? the two look identical), stray or mismatched closers,
//!   NaN / Infinity / undefined (no JSON equivalent), unquoted string
//!   values, empty elements `[1,,2]`, malformed numbers, invalid escapes,
//!   unterminated strings and comments, a second top-level value.

use crate::json_fmt::{
    classify_word, describe_char, escape_for, expected_value, found_at, ident_end, line_cols, scan,
    scan_number, Bareword, JsonError,
};

struct Edit {
    start: usize,
    end: usize,
    text: String,
}

#[derive(Clone, Copy, PartialEq, Eq)]
enum St {
    Value,
    ValueOrClose,
    KeyOrClose,
    Key,
    Colon,
    CommaOrClose,
    End,
}

struct R<'a> {
    s: &'a str,
    b: &'a [u8],
    i: usize,
    edits: Vec<Edit>,
    fixes: Vec<(usize, String)>,
    /// end of the last complete key or value (where a missing ',' / ':' goes)
    prev_end: usize,
    /// lowest offset a whitespace-trimming edit may reach back to
    floor: usize,
}

const CURLY_OPEN_D: char = '\u{201C}'; // “
const CURLY_CLOSE_D: char = '\u{201D}'; // ”
const CURLY_LOW_D: char = '\u{201E}'; // „
const CURLY_OPEN_S: char = '\u{2018}'; // ‘
const CURLY_CLOSE_S: char = '\u{2019}'; // ’

fn closer_of(open: u8) -> u8 {
    if open == b'{' {
        b'}'
    } else {
        b']'
    }
}

impl<'a> R<'a> {
    fn edit(&mut self, start: usize, end: usize, text: impl Into<String>) {
        self.edits.push(Edit {
            start,
            end,
            text: text.into(),
        });
        self.floor = self.floor.max(end);
    }
    fn fix(&mut self, at: usize, msg: impl Into<String>) {
        self.fixes.push((at, msg.into()));
    }
    fn ch(&self) -> Option<char> {
        self.s.get(self.i..).and_then(|r| r.chars().next())
    }
    fn err(&self, at: usize, msg: impl Into<String>) -> JsonError {
        JsonError::new(at, msg)
    }

    /// Skip whitespace and comments (comments become deletion edits).
    fn skip(&mut self) -> Result<(), JsonError> {
        loop {
            while self.i < self.b.len() && matches!(self.b[self.i], b' ' | b'\t' | b'\n' | b'\r') {
                self.i += 1;
            }
            if self.b.get(self.i) != Some(&b'/') {
                return Ok(());
            }
            let start = self.i;
            match self.b.get(self.i + 1) {
                Some(b'/') => {
                    let mut e = start;
                    while e < self.b.len() && !matches!(self.b[e], b'\n' | b'\r') {
                        e += 1;
                    }
                    // also drop the spaces before an end-of-line comment
                    let mut ws = start;
                    while ws > self.floor && matches!(self.b[ws - 1], b' ' | b'\t') {
                        ws -= 1;
                    }
                    self.edit(ws, e, "");
                    self.fix(start, "removed a // comment (JSON has no comments)");
                    self.i = e;
                }
                Some(b'*') => {
                    let close = self.s[start + 2..]
                        .find("*/")
                        .map(|p| start + 2 + p + 2)
                        .ok_or_else(|| {
                            self.err(
                                start,
                                "unterminated /* comment: no closing */ before the end of the document",
                            )
                        })?;
                    // keep two tokens apart only if they would otherwise fuse
                    let wordish =
                        |c: u8| c.is_ascii_alphanumeric() || b"_$.+-\"'".contains(&c) || c >= 0x80;
                    let fuse = start > 0
                        && close < self.b.len()
                        && wordish(self.b[start - 1])
                        && wordish(self.b[close]);
                    let repl = if fuse { " " } else { "" };
                    self.edit(start, close, repl);
                    self.fix(start, "removed a /* */ comment (JSON has no comments)");
                    self.i = close;
                }
                _ => return Ok(()),
            }
        }
    }

    /// Double-quoted string at self.i; returns its end. Raw control chars
    /// are escaped, `\'` becomes `'`, anything else invalid declines.
    fn dq_string(&mut self) -> Result<usize, JsonError> {
        let start = self.i;
        let mut i = start + 1;
        loop {
            let c = match self.b.get(i) {
                None => return Err(self.unterminated(start, "string")),
                Some(&c) => c,
            };
            match c {
                b'"' => return Ok(i + 1),
                b'\\' => match self.b.get(i + 1) {
                    None => return Err(self.unterminated(start, "string")),
                    Some(b'"' | b'\\' | b'/' | b'b' | b'f' | b'n' | b'r' | b't') => i += 2,
                    Some(b'u') => {
                        self.hex4(i)?;
                        i += 6;
                    }
                    Some(b'\'') => {
                        self.edit(i, i + 2, "'");
                        self.fix(
                            i,
                            "replaced \\' with ' inside a string (\\' is not a JSON escape)",
                        );
                        i += 2;
                    }
                    Some(_) => return Err(self.bad_escape(i)),
                },
                c if c < 0x20 => {
                    self.edit(i, i + 1, escape_for(c));
                    self.fix(
                        i,
                        format!(
                            "escaped {} inside a string as {}",
                            describe_char(c as char),
                            escape_for(c)
                        ),
                    );
                    i += 1;
                }
                _ => i += 1,
            }
        }
    }

    fn hex4(&self, backslash: usize) -> Result<(), JsonError> {
        for k in 0..4 {
            match self.b.get(backslash + 2 + k) {
                Some(h) if h.is_ascii_hexdigit() => {}
                None => return Err(self.unterminated(backslash, "string")),
                Some(_) => {
                    return Err(self.err(
                        backslash + 2 + k,
                        format!(
                            "invalid \\u escape: expected 4 hex digits but found {}",
                            found_at(self.s, backslash + 2 + k)
                        ),
                    ))
                }
            }
        }
        Ok(())
    }

    fn bad_escape(&self, backslash: usize) -> JsonError {
        let c = self.s[backslash + 1..].chars().next().unwrap_or('?');
        self.err(
            backslash + 1,
            format!(
                "invalid escape '\\{c}' in string: cannot tell what character was meant (JSON allows \\\" \\\\ \\/ \\b \\f \\n \\r \\t \\uXXXX)"
            ),
        )
    }

    fn unterminated(&self, start: usize, what: &str) -> JsonError {
        let (l, c) = crate::json_fmt::line_col(self.s, start);
        self.err(
            self.b.len(),
            format!("the {what} that starts at line {l}, column {c} is never closed (missing closing quote, or the file is cut off)"),
        )
    }

    /// A string delimited by something other than '"' (single or curly
    /// quotes): rewritten as one double-quoted token with the same content.
    fn other_string(&mut self, closers: &[char], label: &str) -> Result<usize, JsonError> {
        let start = self.i;
        let open_len = self.ch().map(|c| c.len_utf8()).unwrap_or(1);
        let mut out = String::from("\"");
        let mut i = start + open_len;
        loop {
            let c = match self.s.get(i..).and_then(|r| r.chars().next()) {
                None => return Err(self.unterminated(start, label)),
                Some(c) => c,
            };
            if closers.contains(&c) {
                let end = i + c.len_utf8();
                out.push('"');
                self.edit(start, end, out);
                self.fix(
                    start,
                    format!("converted a {label} to a double-quoted string"),
                );
                return Ok(end);
            }
            match c {
                '"' => {
                    out.push_str("\\\"");
                    i += 1;
                }
                '\\' => match self.b.get(i + 1) {
                    None => return Err(self.unterminated(start, label)),
                    Some(b'\'') => {
                        out.push('\'');
                        i += 2;
                    }
                    Some(&e @ (b'"' | b'\\' | b'/' | b'b' | b'f' | b'n' | b'r' | b't')) => {
                        out.push('\\');
                        out.push(e as char);
                        i += 2;
                    }
                    Some(b'u') => {
                        self.hex4(i)?;
                        out.push_str(&self.s[i..i + 6]);
                        i += 6;
                    }
                    Some(_) => return Err(self.bad_escape(i)),
                },
                '\n' | '\r' => {
                    let (l, col) = crate::json_fmt::line_col(self.s, start);
                    return Err(self.err(
                        i,
                        format!("line break inside the {label} that starts at line {l}, column {col}: it is probably missing its closing quote"),
                    ));
                }
                c if (c as u32) < 0x20 => {
                    out.push_str(&escape_for(c as u8));
                    i += 1;
                }
                c => {
                    out.push(c);
                    i += c.len_utf8();
                }
            }
        }
    }

    /// Try to read a string in any accepted quoting at self.i.
    fn any_string(&mut self) -> Option<Result<usize, JsonError>> {
        match self.ch()? {
            '"' => Some(self.dq_string()),
            '\'' => Some(self.other_string(&['\''], "single-quoted string")),
            CURLY_OPEN_D | CURLY_CLOSE_D => {
                Some(self.other_string(&[CURLY_CLOSE_D], "curly-quoted string"))
            }
            CURLY_LOW_D => {
                Some(self.other_string(&[CURLY_OPEN_D, CURLY_CLOSE_D], "curly-quoted string"))
            }
            CURLY_OPEN_S => Some(self.other_string(&[CURLY_CLOSE_S], "curly-quoted string")),
            _ => None,
        }
    }

    fn starts_string(&self) -> bool {
        matches!(
            self.ch(),
            Some('"' | '\'' | CURLY_OPEN_D | CURLY_CLOSE_D | CURLY_LOW_D | CURLY_OPEN_S)
        )
    }

    fn starts_word(&self) -> bool {
        matches!(
            self.b.get(self.i),
            Some(b'a'..=b'z' | b'A'..=b'Z' | b'_' | b'$')
        )
    }

    fn starts_value(&self) -> bool {
        self.starts_string()
            || self.starts_word()
            || matches!(self.b.get(self.i), Some(b'{' | b'[' | b'-' | b'0'..=b'9'))
    }

    fn starts_key(&self) -> bool {
        self.starts_string() || self.starts_word()
    }
}

/// Repair `s`. Ok = (valid JSON, list of (offset, what was done)).
fn repair(s: &str) -> Result<(String, Vec<(usize, String)>), JsonError> {
    let b = s.as_bytes();
    let mut r = R {
        s,
        b,
        i: if s.starts_with('\u{FEFF}') { 3 } else { 0 },
        edits: Vec::new(),
        fixes: Vec::new(),
        prev_end: 0,
        floor: 0,
    };
    r.floor = r.i;
    let mut stack: Vec<(u8, usize)> = Vec::new();
    let mut st = St::Value;
    let mut after_comma: Option<usize> = None;
    let mut key_was_unquoted = false;

    let after_value = |stack: &Vec<(u8, usize)>| {
        if stack.is_empty() {
            St::End
        } else {
            St::CommaOrClose
        }
    };

    loop {
        r.skip()?;
        if r.i >= b.len() {
            break;
        }
        let c = b[r.i];
        match st {
            St::Value | St::ValueOrClose => {
                if c == b']' && st == St::ValueOrClose {
                    stack.pop();
                    r.i += 1;
                    r.prev_end = r.i;
                    st = after_value(&stack);
                    continue;
                }
                if (c == b']' || c == b'}') && after_comma.is_some() {
                    let comma = after_comma.take().unwrap_or(0);
                    let (open, _) = *stack.last().unwrap_or(&(b'[', 0));
                    if c != closer_of(open) {
                        return Err(r.err(
                            r.i,
                            format!(
                                "expected a value or '{}' after ',' but found '{}'",
                                closer_of(open) as char,
                                c as char
                            ),
                        ));
                    }
                    r.edit(comma, comma + 1, "");
                    r.fix(
                        comma,
                        format!("removed a trailing comma before '{}'", c as char),
                    );
                    stack.pop();
                    r.i += 1;
                    r.prev_end = r.i;
                    st = after_value(&stack);
                    continue;
                }
                if c == b',' {
                    return Err(r.err(
                        r.i,
                        if after_comma.is_some() {
                            "empty element: two commas in a row (cannot tell what value is missing)"
                        } else {
                            "missing value: found ',' where a value was expected"
                        },
                    ));
                }
                after_comma = None;
                match c {
                    b'{' | b'[' => {
                        stack.push((c, r.i));
                        r.i += 1;
                        st = if c == b'{' {
                            St::KeyOrClose
                        } else {
                            St::ValueOrClose
                        };
                        continue;
                    }
                    b'-' | b'0'..=b'9' => {
                        r.i = scan_number(s, r.i)?;
                    }
                    _ if r.starts_word() => {
                        let start = r.i;
                        let e = ident_end(b, start);
                        let w = &s[start..e];
                        match classify_word(w) {
                            Bareword::Literal => {}
                            Bareword::Python(j) => {
                                r.edit(start, e, j);
                                r.fix(start, format!("replaced Python '{w}' with {j}"));
                            }
                            Bareword::NoEquivalent => {
                                return Err(r.err(
                                    start,
                                    format!("'{w}' has no JSON equivalent; replace it yourself (null, a number, or a string)"),
                                ))
                            }
                            Bareword::Other => {
                                return Err(r.err(
                                    start,
                                    format!("unquoted text '{w}': cannot tell what value was meant (strings need double quotes)"),
                                ))
                            }
                        }
                        r.i = e;
                    }
                    _ => match r.any_string() {
                        Some(res) => r.i = res?,
                        None => return Err(expected_value(s, r.i, &stack)),
                    },
                }
                r.prev_end = r.i;
                st = after_value(&stack);
            }
            St::KeyOrClose | St::Key => {
                if c == b'}' {
                    if let Some(comma) = after_comma.take() {
                        r.edit(comma, comma + 1, "");
                        r.fix(comma, "removed a trailing comma before '}'");
                    }
                    stack.pop();
                    r.i += 1;
                    r.prev_end = r.i;
                    st = after_value(&stack);
                    continue;
                }
                if c == b',' {
                    return Err(r.err(r.i, "empty member: two commas in a row inside an object"));
                }
                after_comma = None;
                key_was_unquoted = r.starts_word();
                if r.starts_word() {
                    let start = r.i;
                    let e = ident_end(b, start);
                    r.edit(start, start, "\"");
                    r.edit(e, e, "\"");
                    r.fix(start, format!("quoted the key '{}'", &s[start..e]));
                    r.i = e;
                } else if let Some(res) = r.any_string() {
                    r.i = res?;
                } else {
                    return Err(r.err(
                        r.i,
                        format!("expected an object key but found {}", found_at(s, r.i)),
                    ));
                }
                r.prev_end = r.i;
                st = St::Colon;
            }
            St::Colon => {
                if c == b':' {
                    r.i += 1;
                    st = St::Value;
                    continue;
                }
                if r.starts_value() && key_was_unquoted {
                    return Err(r.err(
                        r.i,
                        "the unquoted key is not followed by ':': cannot tell where the key ends and the value begins",
                    ));
                }
                if r.starts_value() {
                    let at = r.prev_end;
                    r.edit(at, at, ":");
                    r.fix(at, "inserted a missing ':' after the key");
                    st = St::Value;
                    continue;
                }
                return Err(r.err(
                    r.i,
                    format!(
                        "expected ':' and a value after the object key but found {}",
                        found_at(s, r.i)
                    ),
                ));
            }
            St::CommaOrClose => {
                let (open, open_at) = *stack.last().expect("container");
                if c == b',' {
                    after_comma = Some(r.i);
                    r.i += 1;
                    st = if open == b'{' { St::Key } else { St::Value };
                    continue;
                }
                if c == closer_of(open) {
                    stack.pop();
                    r.i += 1;
                    r.prev_end = r.i;
                    st = after_value(&stack);
                    continue;
                }
                if c == b']' || c == b'}' {
                    let (l, col) = crate::json_fmt::line_col(s, open_at);
                    return Err(r.err(
                        r.i,
                        format!(
                            "found '{}' but the {} opened at line {l}, column {col} needs '{}': cannot tell which bracket is wrong",
                            c as char,
                            if open == b'{' { "object" } else { "array" },
                            closer_of(open) as char
                        ),
                    ));
                }
                let next_ok = if open == b'{' {
                    r.starts_key()
                } else {
                    r.starts_value()
                };
                // Only across a line break: a comma lost at the end of a
                // line is the common hand-edit. On one line, `[1 2]` is as
                // likely a split number and `[[]"x"]` a moved bracket.
                let gap_has_newline = b[r.prev_end..r.i].iter().any(|&c| c == b'\n' || c == b'\r');
                if next_ok && gap_has_newline {
                    let at = r.prev_end;
                    r.edit(at, at, ",");
                    r.fix(at, "inserted a missing ',' at the end of the line");
                    st = if open == b'{' { St::Key } else { St::Value };
                    continue;
                }
                if next_ok {
                    return Err(r.err(
                        r.i,
                        format!(
                            "expected ',' or '{}' but found {}: a comma may be missing, but on one line that is a guess (it could also be a misplaced bracket or a split value)",
                            closer_of(open) as char,
                            found_at(s, r.i)
                        ),
                    ));
                }
                return Err(r.err(
                    r.i,
                    format!(
                        "expected ',' or '{}' but found {}",
                        closer_of(open) as char,
                        found_at(s, r.i)
                    ),
                ));
            }
            St::End => {
                return Err(r.err(
                    r.i,
                    format!(
                        "unexpected {} after the end of the JSON document (only one top-level value is allowed)",
                        found_at(s, r.i)
                    ),
                ));
            }
        }
    }

    if st != St::End {
        if stack.is_empty() {
            return Err(r.err(b.len(), "empty document: there is no JSON value to repair"));
        }
        let closers: String = stack
            .iter()
            .rev()
            .map(|(o, _)| closer_of(*o) as char)
            .collect();
        let shown: Vec<String> = stack
            .iter()
            .rev()
            .take(3)
            .map(|(o, at)| {
                let (l, c) = crate::json_fmt::line_col(s, *at);
                format!("'{}' from line {l}, column {c}", *o as char)
            })
            .collect();
        let advice = if closers.len() <= 20 {
            format!("If nothing is missing, add \"{closers}\" at the end")
        } else {
            format!(
                "If nothing is missing, add {} closing brackets at the end",
                closers.len()
            )
        };
        return Err(r.err(
            b.len(),
            format!(
                "the document ends while {} bracket(s) are still open ({}{}). {advice}; if the file was cut off, the rest of the data is missing and cannot be recovered, so nothing was changed",
                stack.len(),
                shown.join(", "),
                if stack.len() > 3 { ", ..." } else { "" }
            ),
        ));
    }

    // Apply edits (insertions before deletions at the same offset).
    let mut edits = r.edits;
    edits.sort_by_key(|e| (e.start, e.end));
    let mut out = String::with_capacity(s.len() + edits.len() * 2);
    let mut cur = 0usize;
    for e in &edits {
        if e.start < cur {
            return Err(JsonError::new(
                e.start,
                "internal error: overlapping repairs (please report this input); nothing was changed",
            ));
        }
        out.push_str(&s[cur..e.start]);
        out.push_str(&e.text);
        cur = e.end;
    }
    out.push_str(&s[cur..]);
    if let Err(e) = scan(&out, |_, _, _, _| {}) {
        return Err(JsonError::new(
            0,
            format!(
                "internal error: the repair did not produce valid JSON ({}); nothing was changed",
                e.describe(&out)
            ),
        ));
    }
    let mut fixes = r.fixes;
    fixes.sort_by_key(|f| f.0);
    Ok((out, fixes))
}

const MAX_REPORT_LINES: usize = 200;

/// `(text, report)`: repaired text, or the input unchanged with the reason.
pub fn repair_with_report(input: &str) -> (String, String) {
    match repair(input) {
        Ok((out, fixes)) => {
            let shown = &fixes[..fixes.len().min(MAX_REPORT_LINES)];
            let offs: Vec<usize> = shown.iter().map(|f| f.0).collect();
            let pos = line_cols(input, &offs);
            let mut rep = format!(
                "Repaired {} issue(s); the result is valid JSON with the same content:\n",
                fixes.len()
            );
            for ((_, msg), (l, c)) in shown.iter().zip(pos) {
                rep.push_str(&format!("\nline {l}, column {c}: {msg}"));
            }
            if fixes.len() > shown.len() {
                rep.push_str(&format!("\n... and {} more", fixes.len() - shown.len()));
            }
            (out, rep)
        }
        Err(e) => (
            input.to_string(),
            format!(
                "No changes made: this JSON cannot be repaired without guessing.\n\n{}",
                e.describe(input)
            ),
        ),
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    fn fixed(s: &str) -> String {
        repair_with_report(s).0
    }
    fn report(s: &str) -> String {
        repair_with_report(s).1
    }

    #[test]
    fn each_repair_class() {
        let cases: &[(&str, &str)] = &[
            ("[1,2,]", "[1,2]"),
            ("{\"a\":1,}", "{\"a\":1}"),
            ("[1\n2]", "[1,\n2]"),
            ("{\"a\":1\r\n \"b\":2}", "{\"a\":1,\r\n \"b\":2}"),
            ("{\"a\" 1}", "{\"a\": 1}"),
            ("['it\\'s', 'say \"hi\"']", "[\"it's\", \"say \\\"hi\\\"\"]"),
            ("{a: 1, $b_2: 2}", "{\"a\": 1, \"$b_2\": 2}"),
            ("[1, // one\n 2]", "[1,\n 2]"),
            ("[1, /* x */ 2]", "[1,  2]"),
            ("[1/**/,2]", "[1,2]"),
            ("[True, False, None]", "[true, false, null]"),
            ("[\"a\tb\"]", "[\"a\\tb\"]"),
            ("[\"a\nb\"]", "[\"a\\nb\"]"),
            ("[\"it\\'s\"]", "[\"it's\"]"),
            ("[\u{201C}a\u{201D}]", "[\"a\"]"),
            ("[\u{2018}a\u{2019}]", "[\"a\"]"),
            ("\u{FEFF}[1,]", "\u{FEFF}[1]"),
        ];
        for (input, want) in cases {
            let (got, rep) = repair_with_report(input);
            assert_eq!(&got, want, "input {input:?}\nreport: {rep}");
            assert!(rep.starts_with("Repaired"), "{rep}");
        }
    }

    #[test]
    fn each_decline_class_names_the_spot() {
        let cases: &[(&str, &str)] = &[
            ("[1,2", "line 1, column 5: the document ends while 1 bracket(s) are still open ('[' from line 1, column 1). If nothing is missing, add \"]\" at the end"),
            ("{\"a\":[1", "add \"]}\" at the end"),
            ("[1]]", "line 1, column 4: unexpected ']' after the end"),
            ("{\"a\":1]", "line 1, column 7: found ']' but the object opened at line 1, column 1 needs '}'"),
            ("[NaN]", "line 1, column 2: 'NaN' has no JSON equivalent"),
            ("[-Infinity]", "-Infinity is not valid JSON"),
            ("[hello]", "unquoted text 'hello'"),
            ("[1,,2]", "line 1, column 4: empty element"),
            ("[01]", "leading zeros"),
            ("[\"\\x\"]", "line 1, column 4: invalid escape '\\x'"),
            ("[\"abc", "the string that starts at line 1, column 2 is never closed"),
            ("['abc\n']", "line break inside the single-quoted string"),
            ("[1 /* x", "unterminated /* comment"),
            ("[1] [2]", "unexpected '['"),
            ("", "empty document"),
            ("{\"a\":}", "expected a value"),
            ("[1 2]", "line 1, column 4: expected ',' or ']' but found '2': a comma may be missing, but on one line that is a guess"),
            ("[[]\"x\"]", "a comma may be missing"),
            ("{a 1}", "the unquoted key is not followed by ':'"),
        ];
        for (input, want) in cases {
            let (got, rep) = repair_with_report(input);
            assert_eq!(&got, input, "changed {input:?}");
            assert!(rep.starts_with("No changes made"), "{rep}");
            assert!(
                rep.contains(want),
                "{input:?}\n  want ..{want}..\n  got  {rep}"
            );
        }
    }

    #[test]
    fn strings_are_never_edited_by_structural_repairs() {
        // The old regex repairer rewrote text INSIDE strings: ",]" became
        // "]" and "x, b: y" got a quoted "b". Content must survive.
        assert_eq!(fixed("{\"a\": \"1,]\", }"), "{\"a\": \"1,]\" }");
        assert_eq!(
            fixed("{k: \"x, b: y\", 'q': '[a: 1'}"),
            "{\"k\": \"x, b: y\", \"q\": \"[a: 1\"}"
        );
        assert_eq!(fixed("[\"//not a comment\",]"), "[\"//not a comment\"]");
    }

    #[test]
    fn report_lists_every_change_with_position() {
        let r = report("{a: 1,\n  'b': True,}");
        assert!(r.contains("line 1, column 2: quoted the key 'a'"), "{r}");
        assert!(
            r.contains("line 2, column 3: converted a single-quoted string"),
            "{r}"
        );
        assert!(
            r.contains("line 2, column 8: replaced Python 'True' with true"),
            "{r}"
        );
        assert!(
            r.contains("line 2, column 12: removed a trailing comma"),
            "{r}"
        );
        assert!(r.starts_with("Repaired 4 issue(s)"), "{r}");
    }
}
