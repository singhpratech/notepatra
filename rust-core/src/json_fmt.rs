// SPDX-License-Identifier: GPL-3.0-or-later

//! JSON tools: strict validation, pretty-print and minify, plus the repairer
//! (in `json_repair`).
//!
//! The contract (v0.1.133 rewrite, pinned by `fuzz::json_props`):
//!
//! * Format and Minify only ever change WHITESPACE. Every token is copied
//!   from the input byte for byte: key order, duplicate keys, number text
//!   (`1.0`, `-0`, `1e400`, 30-digit integers) and string escapes
//!   (`\u00e9` stays `\u00e9`) are all preserved. There is no parse into a
//!   value tree, so nothing can be rounded, deduplicated or re-escaped.
//! * Invalid input is never "formatted": `format_json` / `minify_json` return
//!   it unchanged and `json_parse_error` says exactly what is wrong and
//!   where ("line L, column C: expected ',' or ']' ... but found '@'").
//!   The previous version silently repaired (`[1,2` became `[1,2]`) and, when
//!   that failed, fell back to a whitespace-stripping printer that also
//!   stripped spaces INSIDE strings.
//! * Repair is a separate, explicit action (`fix_json_with_report`) that
//!   either produces valid JSON with the same meaning or makes no change and
//!   says why.
//! * No recursion anywhere (the nesting stack is a Vec), so 1M levels of
//!   `[` is a clean error, not a stack overflow; every pass is linear.
//!
//! Positions are reported as 1-based line and column, where `\n`, `\r\n` and
//! a lone `\r` end a line and columns count characters (not bytes).

use crate::json_repair;

const MAX_INPUT_BYTES: usize = 50 * 1024 * 1024; // 50 MB
/// Pretty-printing deeply nested input multiplies its size by the depth
/// (each line carries depth x indent spaces); refuse beyond this.
const MAX_PRETTY_BYTES: usize = 256 * 1024 * 1024;
const UTF8_BOM: &str = "\u{FEFF}";

// ── errors and positions ──────────────────────────────────────────────────

#[derive(Debug, Clone, PartialEq, Eq)]
pub struct JsonError {
    /// Byte offset of the offending character (input length = end of input).
    pub offset: usize,
    pub msg: String,
}

impl JsonError {
    pub fn new(offset: usize, msg: impl Into<String>) -> Self {
        JsonError {
            offset,
            msg: msg.into(),
        }
    }
    /// "line L, column C: <msg>"
    pub fn describe(&self, input: &str) -> String {
        let (l, c) = line_col(input, self.offset);
        format!("line {l}, column {c}: {}", self.msg)
    }
}

/// 1-based (line, column) of a byte offset; columns count chars.
pub fn line_col(s: &str, off: usize) -> (usize, usize) {
    let mut v = line_cols(s, &[off]);
    v.pop().unwrap_or((1, 1))
}

/// (line, column) for many offsets in one sweep. `offs` must be ascending.
pub fn line_cols(s: &str, offs: &[usize]) -> Vec<(usize, usize)> {
    let b = s.as_bytes();
    let mut out = Vec::with_capacity(offs.len());
    let (mut line, mut col, mut i) = (1usize, 1usize, 0usize);
    for &target in offs {
        let target = target.min(b.len());
        while i < target {
            let c = b[i];
            if c == b'\n' || (c == b'\r' && b.get(i + 1) != Some(&b'\n')) {
                line += 1;
                col = 1;
            } else if c == b'\r' || (0x80..0xC0).contains(&c) {
                // '\r' of a CRLF, or a UTF-8 continuation byte
            } else {
                col += 1;
            }
            i += 1;
        }
        out.push((line, col));
    }
    out
}

/// Human description of the character at `i` ("'@'", "U+00A0 (no-break
/// space)", "end of input").
pub fn found_at(s: &str, i: usize) -> String {
    match s.get(i..).and_then(|r| r.chars().next()) {
        None => "end of input".into(),
        Some(c) => describe_char(c),
    }
}

pub fn describe_char(c: char) -> String {
    match c {
        '\n' => "a line break".into(),
        '\r' => "a carriage return".into(),
        '\t' => "a tab".into(),
        '\0' => "a NUL character (U+0000)".into(),
        '\u{A0}' => "a no-break space (U+00A0)".into(),
        '\u{FEFF}' => "a byte order mark (U+FEFF)".into(),
        '\u{201C}' | '\u{201D}' | '\u{2018}' | '\u{2019}' => {
            format!("a curly quote '{c}' (U+{:04X})", c as u32)
        }
        c if c.is_control() || c.is_whitespace() => format!("U+{:04X}", c as u32),
        c => format!("'{c}'"),
    }
}

// ── the strict scanner ────────────────────────────────────────────────────

#[derive(Clone, Copy, PartialEq, Eq, Debug)]
pub enum Tok {
    Open(u8),
    Close(u8),
    Colon,
    Comma,
    /// string, number or literal; the span is the exact source text
    Scalar,
}

#[derive(Clone, Copy, PartialEq, Eq)]
enum Expect {
    Value,
    /// right after '[': a value or ']'
    ValueOrClose,
    /// right after '{': a key or '}'
    KeyOrClose,
    /// after ',' inside an object
    Key,
    Colon,
    CommaOrClose,
    End,
}

fn is_ws(c: u8) -> bool {
    matches!(c, b' ' | b'\t' | b'\n' | b'\r')
}

fn closer_of(open: u8) -> u8 {
    if open == b'{' {
        b'}'
    } else {
        b']'
    }
}

fn container_name(open: u8) -> &'static str {
    if open == b'{' {
        "object"
    } else {
        "array"
    }
}

/// Validate `s` and report every token (with its byte span and the nesting
/// depth after it) to `f`. A leading UTF-8 BOM is accepted and skipped
/// (RFC 8259 §8.1 lets parsers ignore it).
pub fn scan(s: &str, mut f: impl FnMut(Tok, usize, usize, usize)) -> Result<(), JsonError> {
    let b = s.as_bytes();
    let mut i = if s.starts_with(UTF8_BOM) { 3 } else { 0 };
    let mut stack: Vec<(u8, usize)> = Vec::new();
    let mut st = Expect::Value;
    let mut last_comma = 0usize;
    loop {
        while i < b.len() && is_ws(b[i]) {
            i += 1;
        }
        if i >= b.len() {
            break;
        }
        let c = b[i];
        match st {
            Expect::Value | Expect::ValueOrClose => {
                if c == b']' && st == Expect::ValueOrClose {
                    stack.pop();
                    f(Tok::Close(c), i, i + 1, stack.len());
                    i += 1;
                    st = after_value(&stack);
                    continue;
                }
                match c {
                    b'{' | b'[' => {
                        stack.push((c, i));
                        f(Tok::Open(c), i, i + 1, stack.len());
                        i += 1;
                        st = if c == b'{' {
                            Expect::KeyOrClose
                        } else {
                            Expect::ValueOrClose
                        };
                    }
                    b'"' => {
                        let e = scan_string(s, i)?;
                        f(Tok::Scalar, i, e, stack.len());
                        i = e;
                        st = after_value(&stack);
                    }
                    b'-' | b'0'..=b'9' => {
                        let e = scan_number(s, i)?;
                        f(Tok::Scalar, i, e, stack.len());
                        i = e;
                        st = after_value(&stack);
                    }
                    b'a'..=b'z' | b'A'..=b'Z' | b'_' | b'$' => {
                        let e = scan_literal(s, i)?;
                        f(Tok::Scalar, i, e, stack.len());
                        i = e;
                        st = after_value(&stack);
                    }
                    b']' | b'}'
                        if st == Expect::Value
                            && last_comma > 0
                            && stack.last().map(|x| closer_of(x.0)) == Some(c) =>
                    {
                        return Err(JsonError::new(
                            last_comma - 1,
                            format!(
                                "trailing comma: a value must follow ',' but the array closes with '{}' (remove the comma)",
                                c as char
                            ),
                        ));
                    }
                    _ => return Err(expected_value(s, i, &stack)),
                }
                last_comma = 0;
            }
            Expect::KeyOrClose | Expect::Key => {
                if c == b'}' && st == Expect::KeyOrClose {
                    stack.pop();
                    f(Tok::Close(c), i, i + 1, stack.len());
                    i += 1;
                    st = after_value(&stack);
                    continue;
                }
                if c == b'"' {
                    let e = scan_string(s, i)?;
                    f(Tok::Scalar, i, e, stack.len());
                    i = e;
                    st = Expect::Colon;
                    continue;
                }
                if c == b'}' {
                    return Err(JsonError::new(
                        last_comma - 1,
                        "trailing comma: a key must follow ',' but the object closes with '}' (remove the comma)",
                    ));
                }
                return Err(expected_key(s, i));
            }
            Expect::Colon => {
                if c == b':' {
                    f(Tok::Colon, i, i + 1, stack.len());
                    i += 1;
                    st = Expect::Value;
                    last_comma = 0;
                    continue;
                }
                let hint = if starts_value(c) || c == b'\'' {
                    " (missing ':'?)"
                } else {
                    ""
                };
                return Err(JsonError::new(
                    i,
                    format!(
                        "expected ':' after the object key but found {}{hint}",
                        found_at(s, i)
                    ),
                ));
            }
            Expect::CommaOrClose => {
                let (open, open_at) = *stack.last().expect("CommaOrClose implies a container");
                if c == b',' {
                    f(Tok::Comma, i, i + 1, stack.len());
                    i += 1;
                    last_comma = i;
                    st = if open == b'{' {
                        Expect::Key
                    } else {
                        Expect::Value
                    };
                    continue;
                }
                if c == closer_of(open) {
                    stack.pop();
                    f(Tok::Close(c), i, i + 1, stack.len());
                    i += 1;
                    st = after_value(&stack);
                    continue;
                }
                let what = if open == b'{' {
                    "an object member"
                } else {
                    "an array element"
                };
                if c == b']' || c == b'}' {
                    let (l, col) = line_col(s, open_at);
                    return Err(JsonError::new(
                        i,
                        format!(
                            "expected ',' or '{}' to close the {} opened at line {l}, column {col}, but found '{}'",
                            closer_of(open) as char,
                            container_name(open),
                            c as char
                        ),
                    ));
                }
                let hint = if starts_value(c) || c == b'\'' {
                    " (missing comma?)"
                } else {
                    ""
                };
                return Err(JsonError::new(
                    i,
                    format!(
                        "expected ',' or '{}' after {what} but found {}{hint}",
                        closer_of(open) as char,
                        found_at(s, i)
                    ),
                ));
            }
            Expect::End => {
                return Err(JsonError::new(
                    i,
                    format!(
                        "unexpected {} after the end of the JSON document (only one top-level value is allowed)",
                        found_at(s, i)
                    ),
                ));
            }
        }
    }
    if st == Expect::End {
        return Ok(());
    }
    let end = b.len();
    if let Some(&(open, at)) = stack.last() {
        let (l, c) = line_col(s, at);
        let more = if stack.len() > 1 {
            format!(" ({} brackets are still open in total)", stack.len())
        } else {
            String::new()
        };
        return Err(JsonError::new(
            end,
            format!(
                "unexpected end of input: the {} opened at line {l}, column {c} is never closed with '{}'{more}",
                container_name(open),
                closer_of(open) as char
            ),
        ));
    }
    Err(JsonError::new(end, "empty document: expected a JSON value"))
}

fn after_value(stack: &[(u8, usize)]) -> Expect {
    if stack.is_empty() {
        Expect::End
    } else {
        Expect::CommaOrClose
    }
}

fn starts_value(c: u8) -> bool {
    matches!(c, b'{' | b'[' | b'"' | b'-' | b'0'..=b'9' | b'a'..=b'z' | b'A'..=b'Z')
}

pub(crate) fn expected_value(s: &str, i: usize, stack: &[(u8, usize)]) -> JsonError {
    let b = s.as_bytes();
    let rest = &s[i..];
    let msg = match b[i] {
        b'\'' => "single-quoted strings are not valid JSON (use double quotes)".to_string(),
        b'/' => "comments are not allowed in JSON".to_string(),
        b'+' => "a JSON number cannot start with '+'".to_string(),
        b'.' => "a JSON number needs a digit before '.' (write 0.5, not .5)".to_string(),
        b',' => "missing value: found ',' where a value was expected".to_string(),
        b']' | b'}' if stack.is_empty() => {
            format!("unexpected '{}': no array or object is open", b[i] as char)
        }
        b']' | b'}' => format!("expected a value but found '{}'", b[i] as char),
        _ if rest.starts_with('\u{201C}') || rest.starts_with('\u{201D}') => {
            "curly quotes are not JSON string delimiters (use straight double quotes)".into()
        }
        _ => format!(
            "expected a value (string, number, object, array, true, false or null) but found {}",
            found_at(s, i)
        ),
    };
    JsonError::new(i, msg)
}

fn expected_key(s: &str, i: usize) -> JsonError {
    let b = s.as_bytes();
    let msg = match b[i] {
        b'\'' => "object keys must use double quotes, not single quotes".to_string(),
        b'a'..=b'z' | b'A'..=b'Z' | b'_' | b'$' => {
            let w = ident_end(b, i);
            format!(
                "object key '{}' must be a string in double quotes",
                &s[i..w]
            )
        }
        b'/' => "comments are not allowed in JSON".to_string(),
        b',' => "missing key: found ',' where an object key was expected".to_string(),
        _ => format!(
            "expected an object key in double quotes but found {}",
            found_at(s, i)
        ),
    };
    JsonError::new(i, msg)
}

pub(crate) fn ident_end(b: &[u8], mut i: usize) -> usize {
    while i < b.len() && (b[i].is_ascii_alphanumeric() || b[i] == b'_' || b[i] == b'$') {
        i += 1;
    }
    i
}

/// Scan a double-quoted string starting at `start` (the quote). Returns the
/// offset just past the closing quote.
pub fn scan_string(s: &str, start: usize) -> Result<usize, JsonError> {
    let b = s.as_bytes();
    let mut i = start + 1;
    while i < b.len() {
        match b[i] {
            b'"' => return Ok(i + 1),
            b'\\' => {
                let e = match b.get(i + 1) {
                    None => break,
                    Some(&e) => e,
                };
                match e {
                    b'"' | b'\\' | b'/' | b'b' | b'f' | b'n' | b'r' | b't' => i += 2,
                    b'u' => {
                        for k in 0..4 {
                            match b.get(i + 2 + k) {
                                Some(h) if h.is_ascii_hexdigit() => {}
                                None => return Err(unterminated(s, start)),
                                Some(_) => {
                                    return Err(JsonError::new(
                                        i + 2 + k,
                                        format!(
                                            "invalid \\u escape: expected 4 hex digits but found {}",
                                            found_at(s, i + 2 + k)
                                        ),
                                    ))
                                }
                            }
                        }
                        i += 6;
                    }
                    _ => {
                        return Err(JsonError::new(
                            i + 1,
                            format!(
                                "invalid escape '\\{}' in string (JSON allows \\\" \\\\ \\/ \\b \\f \\n \\r \\t \\uXXXX)",
                                s[i + 1..].chars().next().unwrap_or('?')
                            ),
                        ))
                    }
                }
            }
            c if c < 0x20 => {
                return Err(JsonError::new(
                    i,
                    format!(
                        "unescaped control character in string: {} must be written as {}",
                        describe_char(c as char),
                        escape_for(c)
                    ),
                ))
            }
            _ => i += 1,
        }
    }
    Err(unterminated(s, start))
}

pub(crate) fn escape_for(c: u8) -> String {
    match c {
        b'\n' => "\\n".into(),
        b'\r' => "\\r".into(),
        b'\t' => "\\t".into(),
        0x08 => "\\b".into(),
        0x0C => "\\f".into(),
        c => format!("\\u{:04X}", c),
    }
}

fn unterminated(s: &str, start: usize) -> JsonError {
    let (l, c) = line_col(s, start);
    JsonError::new(
        s.len(),
        format!("unexpected end of input inside the string that starts at line {l}, column {c} (missing closing '\"')"),
    )
}

/// Strict JSON number starting at `start`; returns its end.
pub fn scan_number(s: &str, start: usize) -> Result<usize, JsonError> {
    let b = s.as_bytes();
    let mut i = start;
    if b[i] == b'-' {
        i += 1;
        if s[i..].starts_with("Infinity") {
            return Err(JsonError::new(
                start,
                "-Infinity is not valid JSON (JSON has no infinities; use null or a string)",
            ));
        }
    }
    match b.get(i) {
        Some(b'0') => {
            i += 1;
            if matches!(b.get(i), Some(b'0'..=b'9')) {
                return Err(JsonError::new(
                    i,
                    "leading zeros are not allowed in JSON numbers",
                ));
            }
        }
        Some(b'1'..=b'9') => {
            while matches!(b.get(i), Some(b'0'..=b'9')) {
                i += 1;
            }
        }
        _ => {
            return Err(JsonError::new(
                i,
                format!("expected a digit after '-' but found {}", found_at(s, i)),
            ))
        }
    }
    if b.get(i) == Some(&b'.') {
        i += 1;
        if !matches!(b.get(i), Some(b'0'..=b'9')) {
            return Err(JsonError::new(
                i,
                format!(
                    "expected a digit after the decimal point but found {}",
                    found_at(s, i)
                ),
            ));
        }
        while matches!(b.get(i), Some(b'0'..=b'9')) {
            i += 1;
        }
    }
    if matches!(b.get(i), Some(b'e' | b'E')) {
        i += 1;
        if matches!(b.get(i), Some(b'+' | b'-')) {
            i += 1;
        }
        if !matches!(b.get(i), Some(b'0'..=b'9')) {
            return Err(JsonError::new(
                i,
                format!(
                    "expected a digit in the exponent but found {}",
                    found_at(s, i)
                ),
            ));
        }
        while matches!(b.get(i), Some(b'0'..=b'9')) {
            i += 1;
        }
    }
    Ok(i)
}

/// What a bare word in value position means, for messages and for repair.
pub(crate) enum Bareword {
    Literal,
    /// Python spelling of a JSON literal: (json text)
    Python(&'static str),
    /// No JSON equivalent (NaN, Infinity, undefined)
    NoEquivalent,
    Other,
}

pub(crate) fn classify_word(w: &str) -> Bareword {
    match w {
        "true" | "false" | "null" => Bareword::Literal,
        "True" => Bareword::Python("true"),
        "False" => Bareword::Python("false"),
        "None" => Bareword::Python("null"),
        "NaN" | "Infinity" | "undefined" => Bareword::NoEquivalent,
        _ => Bareword::Other,
    }
}

fn scan_literal(s: &str, start: usize) -> Result<usize, JsonError> {
    let b = s.as_bytes();
    let e = ident_end(b, start);
    let w = &s[start..e];
    match classify_word(w) {
        Bareword::Literal => return Ok(e),
        Bareword::Python(j) => {
            return Err(JsonError::new(
                start,
                format!("'{w}' is Python, not JSON (write {j})"),
            ))
        }
        Bareword::NoEquivalent => {
            return Err(JsonError::new(
                start,
                format!("'{w}' is not valid JSON (JSON has no NaN, Infinity or undefined; use null or a string)"),
            ))
        }
        Bareword::Other => {}
    }
    for lit in ["true", "false", "null"] {
        if lit.starts_with(w) {
            // "tr" followed by something else: the fault is right after it
            return Err(JsonError::new(
                e,
                format!(
                    "incomplete literal '{w}' (expected '{lit}') followed by {}",
                    found_at(s, e)
                ),
            ));
        }
        if w.starts_with(lit) {
            return Err(JsonError::new(
                start + lit.len(),
                format!(
                    "unexpected {} after '{lit}'",
                    found_at(s, start + lit.len())
                ),
            ));
        }
    }
    Err(JsonError::new(
        start,
        format!("unquoted text '{w}' is not a JSON value (strings need double quotes)"),
    ))
}

// ── public API ────────────────────────────────────────────────────────────

fn too_large(input: &str) -> Option<JsonError> {
    (input.len() > MAX_INPUT_BYTES).then(|| {
        JsonError::new(
            0,
            format!(
                "input too large for the JSON tools ({} bytes, max {} bytes)",
                input.len(),
                MAX_INPUT_BYTES
            ),
        )
    })
}

/// Strict RFC-8259 validation: `""` when `input` is valid JSON, otherwise
/// "line L, column C: <what was found, what was expected>".
///
/// Over MCP there is no human in the loop: v0.1.125 turned `[1,2` into
/// `[1,2]` and reported success, so the MCP `format_json` tool validates
/// first through here. Since the v0.1.133 rewrite Format itself never
/// repairs either, so the two always agree.
pub fn json_parse_error(input: &str) -> String {
    if let Some(e) = too_large(input) {
        return e.msg;
    }
    match scan(input, |_, _, _, _| {}) {
        Ok(()) => String::new(),
        Err(e) => e.describe(input),
    }
}

/// Pretty-print, changing whitespace only.
pub fn pretty(input: &str, indent: usize) -> Result<String, JsonError> {
    if let Some(e) = too_large(input) {
        return Err(e);
    }
    let indent = indent.min(16);
    let b = input.as_bytes();
    // Valid JSON has no raw line breaks inside strings, so any "\r\n" is
    // whitespace: keep the file's line-ending style.
    let nl: &str = match input.find('\n') {
        Some(p) if p > 0 && b[p - 1] == b'\r' => "\r\n",
        _ => "\n",
    };
    let trailing_nl = {
        let t = input.trim_end_matches([' ', '\t']);
        t.ends_with('\n') || t.ends_with('\r')
    };

    // Pass 1: validate and measure, so deep nesting is refused before any
    // allocation instead of after writing gigabytes of indentation.
    let mut size = input.len() + nl.len();
    let mut pending_open = false;
    let mut deepest = 0usize;
    scan(input, |t, _, _, depth| {
        let line = nl.len() + indent * depth;
        match t {
            Tok::Open(_) => {
                if pending_open {
                    size = size.saturating_add(line);
                }
                pending_open = true;
                deepest = deepest.max(depth);
            }
            Tok::Close(_) => {
                if !pending_open {
                    size = size.saturating_add(line);
                }
                pending_open = false;
            }
            Tok::Comma => size = size.saturating_add(line),
            Tok::Colon => size += 1,
            Tok::Scalar => {
                if pending_open {
                    size = size.saturating_add(line);
                }
                pending_open = false;
            }
        }
    })?;
    if size > MAX_PRETTY_BYTES {
        return Err(JsonError::new(
            0,
            format!(
                "pretty-printing would produce about {} MB (the nesting is {} levels deep); Minify still works on this document",
                size / (1024 * 1024),
                deepest
            ),
        ));
    }

    // Pass 2: write.
    let mut out = String::with_capacity(size);
    if input.starts_with(UTF8_BOM) {
        out.push_str(UTF8_BOM);
    }
    let newline = |out: &mut String, depth: usize| {
        out.push_str(nl);
        for _ in 0..depth * indent {
            out.push(' ');
        }
    };
    let mut pending: Option<usize> = None; // depth of a just-opened container
    scan(input, |t, a, z, depth| {
        if let Some(d) = pending.take() {
            if let Tok::Close(c) = t {
                out.push(c as char);
                return;
            }
            newline(&mut out, d);
        }
        match t {
            Tok::Open(c) => {
                out.push(c as char);
                pending = Some(depth);
            }
            Tok::Close(c) => {
                newline(&mut out, depth);
                out.push(c as char);
            }
            Tok::Comma => {
                out.push(',');
                newline(&mut out, depth);
            }
            Tok::Colon => out.push_str(": "),
            Tok::Scalar => out.push_str(&input[a..z]),
        }
    })?;
    if trailing_nl {
        out.push_str(nl);
    }
    Ok(out)
}

/// Minify, removing whitespace between tokens only.
pub fn minify(input: &str) -> Result<String, JsonError> {
    if let Some(e) = too_large(input) {
        return Err(e);
    }
    let mut out = String::with_capacity(input.len());
    if input.starts_with(UTF8_BOM) {
        out.push_str(UTF8_BOM);
    }
    scan(input, |t, a, z, _| match t {
        Tok::Open(c) | Tok::Close(c) => out.push(c as char),
        Tok::Colon => out.push(':'),
        Tok::Comma => out.push(','),
        Tok::Scalar => out.push_str(&input[a..z]),
    })?;
    Ok(out)
}

/// FFI-level Format: the pretty-printed text, or the input UNCHANGED when it
/// is not valid JSON (the caller learns why from `json_parse_error`).
pub fn format_json(input: &str, indent: usize) -> String {
    pretty(input, indent).unwrap_or_else(|_| input.to_string())
}

/// FFI-level Minify: minified text, or the input UNCHANGED when invalid.
pub fn minify_json(input: &str) -> String {
    minify(input).unwrap_or_else(|_| input.to_string())
}

pub fn fix_json(input: &str) -> String {
    fix_json_with_report(input).0
}

/// Repair: `(text, report)`. The text is either valid JSON with exactly the
/// meaning the input evidently had, or the input unchanged — in which case
/// the report says what blocked the repair and where.
pub fn fix_json_with_report(input: &str) -> (String, String) {
    if let Some(e) = too_large(input) {
        return (input.to_string(), format!("{}. No changes made.", e.msg));
    }
    if scan(input, |_, _, _, _| {}).is_ok() {
        return (
            input.to_string(),
            "JSON is already valid. No fixes needed.".to_string(),
        );
    }
    json_repair::repair_with_report(input)
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn empty_input_does_not_panic() {
        assert_eq!(format_json("", 4), "");
        assert_eq!(minify_json(""), "");
        let (out, rep) = fix_json_with_report("");
        assert_eq!(out, "");
        assert!(rep.contains("empty document"), "{rep}");
    }

    #[test]
    fn already_valid_passes_through() {
        let s = r#"{"a":1,"b":[2,3]}"#;
        assert_eq!(
            format_json(s, 2),
            "{\n  \"a\": 1,\n  \"b\": [\n    2,\n    3\n  ]\n}"
        );
    }

    #[test]
    fn output_matches_the_previous_serde_layout() {
        // Same shape serde_json's PrettyFormatter produced, so users see no
        // layout change for ordinary documents.
        let s = r#"{"a":{},"b":[],"c":[{"d":null}],"e":"x"}"#;
        let v: serde_json::Value = serde_json::from_str(s).unwrap();
        let buf = Vec::new();
        let fmt = serde_json::ser::PrettyFormatter::with_indent(b"    ");
        let mut ser = serde_json::Serializer::with_formatter(buf, fmt);
        serde::Serialize::serialize(&v, &mut ser).unwrap();
        let want = String::from_utf8(ser.into_inner()).unwrap();
        assert_eq!(format_json(s, 4), want);
    }

    /// Everything serde_json used to rewrite is now preserved byte for byte.
    #[test]
    fn format_preserves_token_text_exactly() {
        let s = r#"{"n":[1.0,-0,1e400,123456789012345678901234567890,0.1000000000000000055511151231257827,1E+2],"s":"\u00e9\/\ud800","a":1,"a":2}"#;
        let out = format_json(s, 2);
        for t in [
            "1.0",
            "-0",
            "1e400",
            "123456789012345678901234567890",
            "0.1000000000000000055511151231257827",
            "1E+2",
            r#""\u00e9\/\ud800""#,
        ] {
            assert!(out.contains(t), "lost {t} in {out}");
        }
        assert_eq!(out.matches("\"a\"").count(), 2, "duplicate key dropped");
        assert_eq!(minify_json(&out), s);
    }

    #[test]
    fn missing_close_brace_is_not_invented() {
        // `{"a": 1` may be a forgotten '}' or a file cut off mid-way; the
        // repairer cannot know, so it changes nothing and says where.
        let (out, rep) = fix_json_with_report(r#"{"a": 1"#);
        assert_eq!(out, r#"{"a": 1"#);
        assert!(rep.contains("line 1, column 8"), "{rep}");
        assert!(rep.contains("still open"), "{rep}");
    }

    #[test]
    fn trailing_comma_is_stripped() {
        let (out, _) = fix_json_with_report(r#"{"a": 1,}"#);
        assert_eq!(out, r#"{"a": 1}"#);
    }

    #[test]
    fn unquoted_key_is_quoted() {
        let (out, _) = fix_json_with_report(r#"{a: 1}"#);
        assert_eq!(out, r#"{"a": 1}"#);
    }

    #[test]
    fn deeply_nested_does_not_overflow() {
        let input = format!("{}{}", "[".repeat(1000), "]".repeat(1000));
        let pretty = format_json(&input, 2);
        assert_eq!(minify_json(&pretty), input);
        let deep = "[".repeat(1_000_000);
        assert!(json_parse_error(&deep).contains("never closed"));
    }

    #[test]
    fn over_max_size_is_safe() {
        let input = "{".repeat(MAX_INPUT_BYTES + 1);
        assert!(format_json(&input, 2) == input);
        assert!(json_parse_error(&input).starts_with("input too large"));
    }

    #[test]
    fn negative_or_huge_indent_is_clamped() {
        let out = format_json("[1]", usize::MAX);
        assert_eq!(out, format!("[\n{}1\n]", " ".repeat(16)));
    }

    #[test]
    fn mixed_quotes_in_string_value() {
        let s = r#"{"key": "value with 'apostrophe'"}"#;
        let (out, _) = fix_json_with_report(s);
        assert_eq!(out, s);
    }

    #[test]
    fn crlf_and_trailing_newline_are_kept() {
        let s = "{\r\n\"a\": [1, 2]\r\n}\r\n";
        assert_eq!(
            format_json(s, 2),
            "{\r\n  \"a\": [\r\n    1,\r\n    2\r\n  ]\r\n}\r\n"
        );
        assert_eq!(format_json("[1]", 2), "[\n  1\n]");
    }

    #[test]
    fn bom_is_accepted_and_kept() {
        let s = "\u{FEFF}{\"a\":1}";
        assert_eq!(json_parse_error(s), "");
        assert_eq!(format_json(s, 2), "\u{FEFF}{\n  \"a\": 1\n}");
        assert_eq!(minify_json(s), s);
    }

    // ── specific error messages ──────────────────────────────────────────

    fn err(s: &str) -> String {
        json_parse_error(s)
    }

    #[test]
    fn errors_say_what_and_where() {
        let cases: &[(&str, &str)] = &[
            ("[1 2]", "line 1, column 4: expected ',' or ']' after an array element but found '2' (missing comma?)"),
            ("{\"a\":1,}", "line 1, column 7: trailing comma"),
            ("[1,2,]", "line 1, column 5: trailing comma"),
            ("{\"a\":[1,}", "line 1, column 9: expected a value but found '}'"),
            ("{a:1}", "line 1, column 2: object key 'a' must be a string in double quotes"),
            ("{'a':1}", "line 1, column 2: object keys must use double quotes"),
            ("['a']", "line 1, column 2: single-quoted strings are not valid JSON"),
            ("{\"a\" 1}", "line 1, column 6: expected ':' after the object key but found '1' (missing ':'?)"),
            ("[True]", "line 1, column 2: 'True' is Python, not JSON (write true)"),
            ("[None]", "write null"),
            ("[NaN]", "'NaN' is not valid JSON"),
            ("[-Infinity]", "-Infinity is not valid JSON"),
            ("[01]", "line 1, column 3: leading zeros"),
            ("[1.]", "line 1, column 4: expected a digit after the decimal point but found ']'"),
            ("[1e]", "expected a digit in the exponent"),
            ("[.5]", "write 0.5"),
            ("[+1]", "cannot start with '+'"),
            ("[\"a\nb\"]", "line 1, column 4: unescaped control character in string: a line break must be written as \\n"),
            ("[\"\\x\"]", "line 1, column 4: invalid escape '\\x'"),
            ("[\"\\u12G4\"]", "line 1, column 7: invalid \\u escape: expected 4 hex digits but found 'G'"),
            ("[1] [2]", "line 1, column 5: unexpected '[' after the end of the JSON document"),
            ("{\"a\":[1}", "line 1, column 8: expected ',' or ']' to close the array opened at line 1, column 6, but found '}'"),
            ("[1, // c\n2]", "line 1, column 5: comments are not allowed in JSON"),
            ("{\"a\":1", "line 1, column 7: unexpected end of input: the object opened at line 1, column 1 is never closed with '}'"),
            ("[\"abc", "line 1, column 6: unexpected end of input inside the string that starts at line 1, column 2"),
            ("", "line 1, column 1: empty document"),
            ("]", "unexpected ']': no array or object is open"),
            ("[tru]", "line 1, column 5: incomplete literal 'tru' (expected 'true') followed by ']'"),
            ("[truex]", "line 1, column 6: unexpected 'x' after 'true'"),
            ("[hello]", "unquoted text 'hello' is not a JSON value"),
            ("[\u{201C}a\u{201D}]", "curly quotes are not JSON string delimiters"),
            ("[1,\u{A0}2]", "found a no-break space (U+00A0)"),
            ("{\"é\":\"€\" @}", "line 1, column 10: expected ',' or '}'"),
            ("[\r\n1,\r\n@]", "line 3, column 1:"),
        ];
        for (input, want) in cases {
            let got = err(input);
            assert!(
                got.contains(want),
                "{input:?}\n  want ..{want}..\n  got  {got}"
            );
        }
    }

    #[test]
    fn json_parse_error_refuses_what_the_fixer_would_invent() {
        // v0.1.126 NP-09: `[1,2` came back from the MCP format_json tool as
        // `[1,2]` with isError:false. Since v0.1.133 Format never invents
        // anything on ANY surface: invalid input comes back unchanged.
        assert_eq!(format_json("[1,2", 2), "[1,2");
        for bad in [
            "{oops",
            "not json at all",
            "[1,2",
            "{\"a\":1,}",
            "",
            "{\"a\":}",
        ] {
            let err = json_parse_error(bad);
            assert!(!err.is_empty(), "accepted invalid JSON: {bad:?}");
            assert!(
                err.contains("line") && err.contains("column"),
                "parse error carries no position: {err:?} for {bad:?}"
            );
            assert_eq!(format_json(bad, 2), bad);
            assert_eq!(minify_json(bad), bad);
        }
    }

    #[test]
    fn json_parse_error_accepts_every_valid_shape() {
        for good in [
            "{}",
            "[]",
            "{\"a\": 1}",
            "[1, 2]",
            "\"bare string\"",
            "42",
            "true",
            "null",
            "{\"nested\": {\"deep\": [1, {\"x\": null}]}}",
            " \t\r\n[ ] \n",
        ] {
            assert_eq!(json_parse_error(good), "", "rejected valid JSON: {good:?}");
        }
    }

    // ─── Real-world JSON patterns ────────────────────────────────────────

    #[test]
    fn rw_npm_package_json() {
        let s = r#"{"name":"foo","version":"1.0.0","scripts":{"test":"jest","build":"tsc"},"dependencies":{"react":"^18.0.0","lodash":"^4.17.0"}}"#;
        let pretty = format_json(s, 2);
        assert!(pretty.contains("\"react\": \"^18.0.0\""));
        assert!(serde_json::from_str::<serde_json::Value>(&pretty).is_ok());
    }

    #[test]
    fn rw_emoji_and_scripts_round_trip() {
        let s = r#"{"name":"Alice 👋","flag":"🇺🇸","ar":"مرحبا","zh":"你好"}"#;
        let pretty = format_json(s, 2);
        assert_eq!(minify_json(&pretty), s);
    }

    #[test]
    fn rw_escaped_chars_preserved() {
        let s = r#"{"path":"C:\\Users\\test","quote":"He said \"hi\"","newline":"line1\nline2"}"#;
        assert_eq!(minify_json(&format_json(s, 2)), s);
    }

    #[test]
    fn rw_fix_python_dict_paste() {
        let s = r#"{'name': 'Alice', 'age': 30, 'active': True, 'manager': None}"#;
        let (fixed, rep) = fix_json_with_report(s);
        assert_eq!(
            fixed, r#"{"name": "Alice", "age": 30, "active": true, "manager": null}"#,
            "{rep}"
        );
    }

    #[test]
    fn rw_fix_trailing_commas_at_every_level() {
        let s = r#"{"a":[1,2,3,],"b":{"x":1,},"c":[{"k":"v",},],}"#;
        let (fixed, _) = fix_json_with_report(s);
        assert_eq!(fixed, r#"{"a":[1,2,3],"b":{"x":1},"c":[{"k":"v"}]}"#);
    }

    #[test]
    fn rw_fix_unquoted_keys_nested() {
        let s = r#"{a: 1, b: {c: 2, d: [{e: 3}]}}"#;
        let (fixed, _) = fix_json_with_report(s);
        assert_eq!(fixed, r#"{"a": 1, "b": {"c": 2, "d": [{"e": 3}]}}"#);
    }

    #[test]
    fn rw_jsonl_is_reported_not_mangled() {
        let s = "{\"id\":1}\n{\"id\":2}";
        assert_eq!(format_json(s, 2), s);
        assert!(json_parse_error(s).starts_with("line 2, column 1: unexpected '{' after the end"));
    }

    #[test]
    fn garbage_does_not_crash() {
        let (out, rep) = fix_json_with_report("][}{][}{[][");
        assert_eq!(out, "][}{][}{[][");
        assert!(rep.contains("line 1, column 1"), "{rep}");
    }
}
