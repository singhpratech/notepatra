// SPDX-License-Identifier: GPL-3.0-or-later

//! HTML formatter: re-indents HTML without ever changing what it means.
//!
//! The only thing this module ever changes is a run of whitespace that sits
//! *between two markup tokens* (tags, comments, the doctype) and that the
//! browser provably does not render. Every tag, attribute, comment, text
//! run, entity and raw-text body (`<script>`, `<style>`, `<textarea>`,
//! `<title>`, ...) is copied byte for byte. When it cannot prove a document
//! is safe, it returns the input unchanged together with a message that
//! names what it found and where (line and column), never a half-formatted
//! document.
//!
//! How it decides:
//!
//! * A real HTML5 tokenizer (WHATWG states, including the script-data
//!   escape states, RCDATA/RAWTEXT end-tag rules, all the comment end forms
//!   and CDATA in foreign content) splits the input into tokens.
//! * A small model of the HTML5 tree builder follows the tokens: it knows
//!   the implied `<html>`/`<head>`/`<body>`, optional end tags (`li`, `p`,
//!   `dd`, `dt`, `option`, `tr`, `td`, ...), implied `<tbody>`, tables,
//!   `<select>`, `<template>`, SVG/MathML and the 256-level depth cap.
//!   Wherever the real tree builder would do something the model does not
//!   follow exactly (mis-nested formatting tags, stray end tags, foster
//!   parenting out of tables, nested forms, ...), the formatter declines.
//!   That matters because whitespace is not inert to the tree builder: a
//!   whitespace character can re-open a mis-nested `<b>` and re-parent
//!   everything after it.
//! * A whitespace gap may be rewritten only when the browser does not
//!   render it with the default (UA) stylesheet: it touches a block-level
//!   element (`div`, `p`, `li`, `table`, `h1`, ...; not one carrying
//!   `hidden` or an inline `style`) on at least one side, or it sits where
//!   whitespace is never rendered (inside `<head>`, directly inside table
//!   structure, inside `<select>`). Comments are transparent for that test.
//! * Gaps are never touched inside `pre`, `listing`, `textarea`, `xmp`,
//!   `plaintext`, any element whose inline style mentions `white-space`,
//!   SVG/MathML, `<template>` contents, or after `</body>` when content
//!   follows it. Whitespace between inline elements (`<b>a</b> <i>b</i>`)
//!   is never added, removed or changed.
//!
//! Assumption, stated rather than hidden: an author stylesheet can change
//! any element's `display` or `white-space` (an `inline-block` menu of `li`
//! elements, a `.msg { white-space: pre-wrap }`). The formatter assumes the
//! default rendering of each element, as every HTML formatter must.

const MAX_HTML_INPUT_BYTES: usize = 50 * 1024 * 1024;

/// Deepest element nesting the formatter follows. Browsers flatten very
/// deep trees (Chromium stops nesting at 512) in ways other parsers do not,
/// so deeper documents are declined rather than guessed at.
const MAX_DEPTH: usize = 256;

/// Result of a format or minify request.
pub struct HtmlOutcome {
    /// The formatted document, or the input unchanged when `message` is set.
    pub text: String,
    /// Why the input was left unchanged (with line and column), if it was.
    pub message: Option<String>,
}

#[derive(Clone, Copy, PartialEq, Eq, Debug)]
enum Mode {
    Pretty(usize),
    Minify,
}

/// Legacy entry point: the formatted text, or the input unchanged when the
/// formatter declines.
pub fn format_html(input: &str, indent_size: usize) -> String {
    format_html_report(input, indent_size).text
}

/// Re-indent with `indent_size` spaces per level (clamped to 0..=16).
pub fn format_html_report(input: &str, indent_size: usize) -> HtmlOutcome {
    run(input, Mode::Pretty(indent_size.min(16)))
}

/// Remove every whitespace gap the browser does not render.
pub fn minify_html_report(input: &str) -> HtmlOutcome {
    run(input, Mode::Minify)
}

// ═══════════════════════════════════════════════════════════
// Small helpers
// ═══════════════════════════════════════════════════════════

/// HTML whitespace for the tokenizer and tree builder (CR counts: the input
/// stream preprocessor turns it into LF).
#[inline]
fn is_ws(c: u8) -> bool {
    matches!(c, b'\t' | b'\n' | b'\x0C' | b'\r' | b' ')
}

fn find_byte(b: &[u8], from: usize, c: u8) -> Option<usize> {
    b.get(from..)?
        .iter()
        .position(|&x| x == c)
        .map(|k| from + k)
}

fn find_seq(b: &[u8], from: usize, pat: &[u8]) -> Option<usize> {
    if from >= b.len() {
        return None;
    }
    let first = pat[0];
    let mut i = from;
    while let Some(k) = find_byte(b, i, first) {
        if b[k..].starts_with(pat) {
            return Some(k);
        }
        i = k + 1;
    }
    None
}

fn starts_with_ci(b: &[u8], at: usize, pat: &[u8]) -> bool {
    b.len() >= at + pat.len() && b[at..at + pat.len()].eq_ignore_ascii_case(pat)
}

/// 1-based line and column (in characters) of byte offset `pos`. CRLF, LF
/// and a lone CR each end a line, as in the editor.
fn line_col(b: &[u8], pos: usize) -> (usize, usize) {
    let mut line = 1;
    let mut col = 1;
    let end = pos.min(b.len());
    let mut i = 0;
    while i < end {
        match b[i] {
            b'\r' => {
                line += 1;
                col = 1;
                if i + 1 < end && b[i + 1] == b'\n' {
                    i += 1;
                }
            }
            b'\n' => {
                line += 1;
                col = 1;
            }
            c if (c & 0xC0) != 0x80 => col += 1,
            _ => {}
        }
        i += 1;
    }
    (line, col)
}

fn at(b: &[u8], pos: usize) -> String {
    let (l, c) = line_col(b, pos);
    format!("line {}, column {}", l, c)
}

/// A decline: byte offset plus a description of what was found there.
struct Decline {
    pos: usize,
    what: String,
}

type R<T> = Result<T, Decline>;

fn decline<T>(pos: usize, what: impl Into<String>) -> R<T> {
    Err(Decline {
        pos,
        what: what.into(),
    })
}

// ═══════════════════════════════════════════════════════════
// Element classes
// ═══════════════════════════════════════════════════════════

/// Elements the UA stylesheet renders as blocks (display block, list-item,
/// table or a table part). Whitespace next to one of these is not rendered.
pub(crate) fn is_block(name: &str) -> bool {
    matches!(
        name,
        "address"
            | "article"
            | "aside"
            | "blockquote"
            | "body"
            | "caption"
            | "center"
            | "col"
            | "colgroup"
            | "dd"
            | "details"
            | "dir"
            | "div"
            | "dl"
            | "dt"
            | "fieldset"
            | "figcaption"
            | "figure"
            | "footer"
            | "form"
            | "h1"
            | "h2"
            | "h3"
            | "h4"
            | "h5"
            | "h6"
            | "header"
            | "hgroup"
            | "hr"
            | "html"
            | "legend"
            | "li"
            | "listing"
            | "main"
            | "menu"
            | "nav"
            | "ol"
            | "p"
            | "plaintext"
            | "pre"
            | "search"
            | "section"
            | "summary"
            | "table"
            | "tbody"
            | "td"
            | "tfoot"
            | "th"
            | "thead"
            | "tr"
            | "ul"
            | "xmp"
    )
}

fn is_void(name: &str) -> bool {
    matches!(
        name,
        "area"
            | "base"
            | "basefont"
            | "bgsound"
            | "br"
            | "col"
            | "embed"
            | "hr"
            | "image"
            | "img"
            | "input"
            | "keygen"
            | "link"
            | "meta"
            | "param"
            | "source"
            | "track"
            | "wbr"
    )
}

fn is_formatting(name: &str) -> bool {
    matches!(
        name,
        "a" | "b"
            | "big"
            | "code"
            | "em"
            | "font"
            | "i"
            | "nobr"
            | "s"
            | "small"
            | "strike"
            | "strong"
            | "tt"
            | "u"
    )
}

/// The tree builder's "special" category (HTML namespace).
fn is_special(name: &str) -> bool {
    matches!(
        name,
        "address"
            | "applet"
            | "area"
            | "article"
            | "aside"
            | "base"
            | "basefont"
            | "bgsound"
            | "blockquote"
            | "body"
            | "br"
            | "button"
            | "caption"
            | "center"
            | "col"
            | "colgroup"
            | "dd"
            | "details"
            | "dir"
            | "div"
            | "dl"
            | "dt"
            | "embed"
            | "fieldset"
            | "figcaption"
            | "figure"
            | "footer"
            | "form"
            | "frame"
            | "frameset"
            | "h1"
            | "h2"
            | "h3"
            | "h4"
            | "h5"
            | "h6"
            | "head"
            | "header"
            | "hgroup"
            | "hr"
            | "html"
            | "iframe"
            | "img"
            | "input"
            | "keygen"
            | "li"
            | "link"
            | "listing"
            | "main"
            | "marquee"
            | "menu"
            | "meta"
            | "nav"
            | "noembed"
            | "noframes"
            | "noscript"
            | "object"
            | "ol"
            | "p"
            | "param"
            | "plaintext"
            | "pre"
            | "script"
            | "search"
            | "section"
            | "select"
            | "source"
            | "style"
            | "summary"
            | "table"
            | "tbody"
            | "td"
            | "template"
            | "textarea"
            | "tfoot"
            | "th"
            | "thead"
            | "title"
            | "tr"
            | "track"
            | "ul"
            | "wbr"
            | "xmp"
    )
}

/// End tags the tree builder closes implicitly ("generate implied end tags").
fn is_implied_end(name: &str) -> bool {
    matches!(
        name,
        "dd" | "dt" | "li" | "optgroup" | "option" | "p" | "rb" | "rp" | "rt" | "rtc"
    )
}

/// Start tags that close an open `<p>` (when one is in button scope).
fn closes_p(name: &str) -> bool {
    matches!(
        name,
        "address"
            | "article"
            | "aside"
            | "blockquote"
            | "center"
            | "details"
            | "dialog"
            | "dir"
            | "div"
            | "dl"
            | "fieldset"
            | "figcaption"
            | "figure"
            | "footer"
            | "header"
            | "hgroup"
            | "main"
            | "menu"
            | "nav"
            | "ol"
            | "p"
            | "search"
            | "section"
            | "summary"
            | "ul"
            | "h1"
            | "h2"
            | "h3"
            | "h4"
            | "h5"
            | "h6"
            | "pre"
            | "listing"
            | "form"
            | "li"
            | "dd"
            | "dt"
            | "plaintext"
            | "table"
            | "hr"
            | "xmp"
    )
}

/// Elements that push a marker onto the list of active formatting elements.
fn is_marker(name: &str) -> bool {
    matches!(
        name,
        "applet" | "caption" | "marquee" | "object" | "td" | "th"
    )
}

fn is_heading(name: &str) -> bool {
    matches!(name, "h1" | "h2" | "h3" | "h4" | "h5" | "h6")
}

/// HTML tags that break out of SVG/MathML back into HTML.
fn is_foreign_breakout(name: &str) -> bool {
    matches!(
        name,
        "b" | "big"
            | "blockquote"
            | "body"
            | "br"
            | "center"
            | "code"
            | "dd"
            | "div"
            | "dl"
            | "dt"
            | "em"
            | "embed"
            | "h1"
            | "h2"
            | "h3"
            | "h4"
            | "h5"
            | "h6"
            | "head"
            | "hr"
            | "i"
            | "img"
            | "li"
            | "listing"
            | "menu"
            | "meta"
            | "nobr"
            | "ol"
            | "p"
            | "pre"
            | "ruby"
            | "s"
            | "small"
            | "span"
            | "strong"
            | "strike"
            | "sub"
            | "sup"
            | "table"
            | "tt"
            | "u"
            | "ul"
            | "var"
    )
}

fn is_table_structure(name: &str) -> bool {
    matches!(
        name,
        "caption" | "col" | "colgroup" | "tbody" | "td" | "tfoot" | "th" | "thead" | "tr"
    )
}

// ═══════════════════════════════════════════════════════════
// Tokenizer pieces
// ═══════════════════════════════════════════════════════════

/// What the tree builder tells the tokenizer to do after a start tag.
#[derive(Clone, Copy, PartialEq, Eq, Debug)]
enum Content {
    Data,
    /// RCDATA / RAWTEXT: text until the matching end tag.
    RawUntilEnd,
    Script,
    Plaintext,
}

struct Tag {
    name: String,
    self_closing: bool,
    hidden: bool,
    has_style: bool,
    style_white_space: bool,
    font_breakout: bool,
}

/// Parse a tag from just after `<` (start tag) or `</` (end tag) following
/// the tag-name and attribute states. `None` = EOF inside the tag (the
/// browser drops such a tag). Returns the tag and the offset after `>`.
fn parse_tag(b: &[u8], mut i: usize) -> Option<(Tag, usize)> {
    let len = b.len();
    let n0 = i;
    while i < len && !is_ws(b[i]) && b[i] != b'/' && b[i] != b'>' {
        i += 1;
    }
    if i >= len {
        return None;
    }
    // Lowercasing ASCII bytes and splitting at ASCII delimiters keeps UTF-8 valid.
    let name = String::from_utf8_lossy(&b[n0..i].to_ascii_lowercase()).into_owned();
    let mut tag = Tag {
        name,
        self_closing: false,
        hidden: false,
        has_style: false,
        style_white_space: false,
        font_breakout: false,
    };
    loop {
        // Before attribute name (also after attribute name / after value).
        while i < len && is_ws(b[i]) {
            i += 1;
        }
        if i >= len {
            return None;
        }
        match b[i] {
            b'>' => return Some((tag, i + 1)),
            b'/' => {
                // Self-closing start tag state.
                i += 1;
                if i >= len {
                    return None;
                }
                if b[i] == b'>' {
                    tag.self_closing = true;
                    return Some((tag, i + 1));
                }
                continue;
            }
            _ => {}
        }
        // Attribute name: the first character is always part of the name
        // (even '='), the rest stops at whitespace, '/', '>' or '='.
        let a0 = i;
        i += 1;
        while i < len && !is_ws(b[i]) && b[i] != b'/' && b[i] != b'>' && b[i] != b'=' {
            i += 1;
        }
        let a1 = i;
        while i < len && is_ws(b[i]) {
            i += 1;
        }
        if i >= len {
            return None;
        }
        let mut value: &[u8] = b"";
        if b[i] == b'=' {
            i += 1;
            while i < len && is_ws(b[i]) {
                i += 1;
            }
            if i >= len {
                return None;
            }
            match b[i] {
                q @ (b'"' | b'\'') => {
                    let v0 = i + 1;
                    let k = find_byte(b, v0, q)?;
                    value = &b[v0..k];
                    i = k + 1;
                }
                b'>' => {}
                _ => {
                    let v0 = i;
                    while i < len && !is_ws(b[i]) && b[i] != b'>' {
                        i += 1;
                    }
                    if i >= len {
                        return None;
                    }
                    value = &b[v0..i];
                }
            }
        }
        let an = &b[a0..a1];
        if an.eq_ignore_ascii_case(b"hidden") {
            tag.hidden = true;
        } else if an.eq_ignore_ascii_case(b"style") {
            // Duplicate attributes are dropped by the parser: first one wins.
            if !tag.has_style {
                tag.has_style = true;
                tag.style_white_space = value
                    .windows(11)
                    .any(|w| w.eq_ignore_ascii_case(b"white-space"));
            }
        } else if an.eq_ignore_ascii_case(b"color")
            || an.eq_ignore_ascii_case(b"face")
            || an.eq_ignore_ascii_case(b"size")
        {
            tag.font_breakout = true;
        }
    }
}

/// End of a comment that starts at `i` (just after `<!--`): offset after the
/// terminating `>`, or EOF. Covers `<!-->`, `<!--->`, `-->` and `--!>`.
fn comment_end(b: &[u8], mut i: usize) -> usize {
    let len = b.len();
    // Comment start / comment start dash states.
    if i < len && b[i] == b'>' {
        return i + 1;
    }
    if i + 1 < len && b[i] == b'-' && b[i + 1] == b'>' {
        return i + 2;
    }
    #[derive(Clone, Copy)]
    enum S {
        Comment,
        EndDash,
        End,
        EndBang,
    }
    let mut s = S::Comment;
    if i < len && b[i] == b'-' {
        s = S::EndDash;
        i += 1;
    }
    while i < len {
        let c = b[i];
        s = match s {
            S::Comment => {
                if c == b'-' {
                    S::EndDash
                } else {
                    S::Comment
                }
            }
            S::EndDash => {
                if c == b'-' {
                    S::End
                } else {
                    S::Comment
                }
            }
            S::End => match c {
                b'>' => return i + 1,
                b'!' => S::EndBang,
                b'-' => S::End,
                _ => S::Comment,
            },
            S::EndBang => match c {
                b'-' => S::EndDash,
                b'>' => return i + 1,
                _ => S::Comment,
            },
        };
        i += 1;
    }
    len
}

/// `</name` followed by whitespace, `/` or `>` at `i` (ASCII case-insensitive).
fn is_appropriate_end(b: &[u8], i: usize, name: &[u8]) -> bool {
    b.len() > i + 2 + name.len()
        && b[i] == b'<'
        && b[i + 1] == b'/'
        && b[i + 2..i + 2 + name.len()].eq_ignore_ascii_case(name)
        && matches!(
            b[i + 2 + name.len()],
            b'\t' | b'\n' | b'\x0C' | b'\r' | b' ' | b'/' | b'>'
        )
}

/// RCDATA / RAWTEXT: offset of the `<` of the matching end tag, if any.
fn find_raw_end(b: &[u8], from: usize, name: &[u8]) -> Option<usize> {
    let mut i = from;
    while let Some(k) = find_byte(b, i, b'<') {
        if is_appropriate_end(b, k, name) {
            return Some(k);
        }
        i = k + 1;
    }
    None
}

fn alpha_run(b: &[u8], mut i: usize) -> usize {
    while i < b.len() && b[i].is_ascii_alphabetic() {
        i += 1;
    }
    i
}

/// Script data, including the escaped and double-escaped states that make
/// `<!-- <script> </script> -->` inside a script not end it.
fn find_script_end(b: &[u8], from: usize) -> Option<usize> {
    #[derive(Clone, Copy, PartialEq)]
    enum S {
        Data,
        Esc,
        EscDash,
        EscDashDash,
        DEsc,
        DEscDash,
        DEscDashDash,
    }
    let len = b.len();
    let mut s = S::Data;
    let mut i = from;
    while i < len {
        let c = b[i];
        match s {
            S::Data => {
                if c == b'<' {
                    if is_appropriate_end(b, i, b"script") {
                        return Some(i);
                    }
                    if b[i..].starts_with(b"<!--") {
                        s = S::EscDashDash;
                        i += 4;
                        continue;
                    }
                }
            }
            S::Esc | S::EscDash | S::EscDashDash => {
                if c == b'-' {
                    s = match s {
                        S::Esc => S::EscDash,
                        _ => S::EscDashDash,
                    };
                } else if c == b'<' {
                    // Script data escaped less-than sign state.
                    if is_appropriate_end(b, i, b"script") {
                        return Some(i);
                    }
                    if i + 1 < len && b[i + 1].is_ascii_alphabetic() {
                        // Double escape start.
                        let e = alpha_run(b, i + 1);
                        if e < len
                            && matches!(b[e], b'\t' | b'\n' | b'\x0C' | b'\r' | b' ' | b'/' | b'>')
                            && b[i + 1..e].eq_ignore_ascii_case(b"script")
                        {
                            s = S::DEsc;
                        } else {
                            s = S::Esc;
                        }
                        i = e;
                        continue;
                    }
                    s = S::Esc;
                } else if c == b'>' && s == S::EscDashDash {
                    s = S::Data;
                } else {
                    s = S::Esc;
                }
            }
            S::DEsc | S::DEscDash | S::DEscDashDash => {
                if c == b'-' {
                    s = match s {
                        S::DEsc => S::DEscDash,
                        _ => S::DEscDashDash,
                    };
                } else if c == b'<' {
                    if i + 1 < len && b[i + 1] == b'/' {
                        // Double escape end.
                        let e = alpha_run(b, i + 2);
                        if e < len
                            && matches!(b[e], b'\t' | b'\n' | b'\x0C' | b'\r' | b' ' | b'/' | b'>')
                            && b[i + 2..e].eq_ignore_ascii_case(b"script")
                        {
                            s = S::Esc;
                        } else {
                            s = S::DEsc;
                        }
                        i = e;
                        continue;
                    }
                    s = S::DEsc;
                } else if c == b'>' && s == S::DEscDashDash {
                    s = S::Data;
                } else {
                    s = S::DEsc;
                }
            }
        }
        i += 1;
    }
    None
}

/// Server-side code the HTML tokenizer would cut in half (`<?php ... ?>`,
/// `<% ... %>`): whitespace inside it may be significant to the server.
fn find_server_code(b: &[u8]) -> Option<(usize, &'static str)> {
    let mut i = 0;
    while let Some(k) = find_byte(b, i, b'<') {
        match b.get(k + 1) {
            Some(b'?') if !starts_with_ci(b, k + 2, b"xml") => {
                return Some((k, "server-side code `<?`"));
            }
            Some(b'%') => return Some((k, "server-side template code `<%`")),
            _ => {}
        }
        i = k + 1;
    }
    None
}

// ═══════════════════════════════════════════════════════════
// Tree-builder model
// ═══════════════════════════════════════════════════════════

#[derive(Clone, Copy, PartialEq, Eq, Debug)]
enum Ns {
    Html,
    Svg,
    Math,
}

struct El {
    name: String,
    ns: Ns,
    explicit: bool,
    boundary: bool,
    frozen: bool,
    pos: usize,
}

#[derive(Clone, Copy, PartialEq, Eq, Debug)]
enum IMode {
    Initial,
    BeforeHtml,
    BeforeHead,
    InHead,
    AfterHead,
    InBody,
    AfterBody,
    AfterAfterBody,
}

/// Where whitespace after a token lands.
#[derive(Clone, Copy, PartialEq, Eq, Debug)]
enum GapState {
    /// Rendered normally: rewritable only next to a block boundary.
    Normal,
    /// Never rendered (head, table structure, select): always rewritable.
    Free,
    /// Whitespace-sensitive or not modelled: never touched.
    Frozen,
}

/// Classification of one markup token.
struct Out {
    indent: usize,
    /// Whitespace just before this token is not rendered.
    lb: bool,
    /// Whitespace just after this token is not rendered.
    rb: bool,
    content: Content,
}

struct Model<'a> {
    b: &'a [u8],
    mode: IMode,
    stack: Vec<El>,
    depth: usize,
    frozen: usize,
    foreign: usize,
    selects: usize,
    tablish: usize,
    template_depth: usize,
    form_open: bool,
    tail_clean: bool,
    in_tail: bool,
    /// Elements popped so far (to see whether a token closed anything).
    pops: usize,
    /// A formatting start tag (`<b>`, `<a>`, ...) appeared inside the
    /// current outermost `<template>`.
    tmpl_fmt_seen: bool,
    /// A marker start tag (`<td>`, `<object>`, ...) appeared inside it.
    tmpl_marker_seen: bool,
    /// No start tag seen yet in the innermost template (its first element
    /// picks the template's insertion mode).
    tmpl_first: bool,
    /// A formatting element was open when the outermost `<template>` started.
    tmpl_outer_fmt: bool,
}

fn tag_desc(name: &str) -> String {
    format!("`<{}>`", name)
}

fn end_desc(name: &str) -> String {
    format!("`</{}>`", name)
}

impl<'a> Model<'a> {
    fn new(b: &'a [u8]) -> Self {
        Model {
            b,
            mode: IMode::Initial,
            stack: Vec::new(),
            depth: 0,
            frozen: 0,
            foreign: 0,
            selects: 0,
            tablish: 0,
            template_depth: 0,
            form_open: false,
            tail_clean: true,
            in_tail: false,
            pops: 0,
            tmpl_fmt_seen: false,
            tmpl_marker_seen: false,
            tmpl_first: false,
            tmpl_outer_fmt: false,
        }
    }

    fn where_(&self, pos: usize) -> String {
        at(self.b, pos)
    }

    fn top(&self) -> Option<&El> {
        self.stack.last()
    }

    fn top_is(&self, name: &str) -> bool {
        matches!(self.top(), Some(e) if e.ns == Ns::Html && e.name == name)
    }

    fn top_boundary(&self) -> bool {
        self.top().is_some_and(|e| e.boundary)
    }

    fn push(&mut self, el: El) -> R<()> {
        if self.stack.len() >= MAX_DEPTH {
            return decline(
                el.pos,
                format!(
                    "elements are nested more than {} levels deep here; browsers \
                     flatten very deep trees, so the formatter does not touch it",
                    MAX_DEPTH
                ),
            );
        }
        if el.explicit {
            self.depth += 1;
        }
        if el.frozen {
            self.frozen += 1;
        }
        if el.ns != Ns::Html {
            self.foreign += 1;
        } else {
            match el.name.as_str() {
                "select" => self.selects += 1,
                "table" | "caption" | "colgroup" | "tbody" | "thead" | "tfoot" | "tr" | "td"
                | "th" => self.tablish += 1,
                _ => {}
            }
        }
        self.stack.push(el);
        Ok(())
    }

    fn pop(&mut self) -> El {
        let el = self.stack.pop().expect("pop on empty stack");
        self.pops += 1;
        if el.explicit {
            self.depth -= 1;
        }
        if el.frozen {
            self.frozen -= 1;
        }
        if el.ns != Ns::Html {
            self.foreign -= 1;
        } else {
            match el.name.as_str() {
                "select" => self.selects -= 1,
                "table" | "caption" | "colgroup" | "tbody" | "thead" | "tfoot" | "tr" | "td"
                | "th" => self.tablish -= 1,
                _ => {}
            }
        }
        el
    }

    fn html_el(&self, t: &Tag, pos: usize) -> El {
        El {
            name: t.name.clone(),
            ns: Ns::Html,
            explicit: true,
            boundary: is_block(&t.name) && !t.hidden && !t.has_style,
            frozen: matches!(t.name.as_str(), "pre" | "listing") || t.style_white_space,
            pos,
        }
    }

    fn implied(&self, name: &str, pos: usize) -> El {
        El {
            name: name.to_string(),
            ns: Ns::Html,
            explicit: false,
            boundary: is_block(name),
            frozen: false,
            pos,
        }
    }

    /// Is any formatting element open? (A marker left behind by a template
    /// shifts every later "clear to the last marker", which can expose even
    /// a formatting element opened before an enclosing marker.)
    fn formatting_open(&self) -> bool {
        self.stack
            .iter()
            .any(|e| e.ns == Ns::Html && is_formatting(&e.name))
    }

    fn gap_state(&self) -> GapState {
        if self.template_depth > 0 || self.foreign > 0 || self.frozen > 0 {
            return GapState::Frozen;
        }
        match self.mode {
            IMode::Initial
            | IMode::BeforeHtml
            | IMode::BeforeHead
            | IMode::InHead
            | IMode::AfterHead => GapState::Free,
            _ => match self.top() {
                Some(e)
                    if e.ns == Ns::Html
                        && matches!(
                            e.name.as_str(),
                            "table" | "tbody" | "thead" | "tfoot" | "tr" | "colgroup" | "select"
                        ) =>
                {
                    GapState::Free
                }
                _ => GapState::Normal,
            },
        }
    }

    /// Is an HTML element named `name` in the given scope?
    fn in_scope(&self, name: &str, extra: &[&str]) -> bool {
        for e in self.stack.iter().rev() {
            if e.ns == Ns::Html && e.name == name {
                return true;
            }
            if e.ns != Ns::Html {
                if matches!(
                    e.name.as_str(),
                    "mi" | "mo"
                        | "mn"
                        | "ms"
                        | "mtext"
                        | "annotation-xml"
                        | "foreignobject"
                        | "desc"
                        | "title"
                ) {
                    return false;
                }
                continue;
            }
            if matches!(
                e.name.as_str(),
                "applet"
                    | "caption"
                    | "html"
                    | "table"
                    | "td"
                    | "th"
                    | "marquee"
                    | "object"
                    | "template"
            ) || extra.contains(&e.name.as_str())
            {
                return false;
            }
        }
        false
    }

    /// Pop elements in the implied-end set (except `except`). Returns the
    /// element that was current before popping, for the boundary test.
    fn pop_implied(&mut self, except: &str) {
        while let Some(e) = self.top() {
            if e.ns == Ns::Html && is_implied_end(&e.name) && e.name != except {
                self.pop();
            } else {
                break;
            }
        }
    }

    /// "Close a p element" when a `<p>` is in button scope. Declines when
    /// that would implicitly close anything but optional-end-tag elements.
    fn close_p(&mut self, pos: usize, by: &str) -> R<()> {
        if !self.stack.iter().any(|e| e.ns == Ns::Html && e.name == "p") {
            return Ok(());
        }
        if !self.in_scope("p", &["button"]) {
            return Ok(());
        }
        self.pop_implied("p");
        if !self.top_is("p") {
            let top = self.top().map(|e| (e.name.clone(), e.pos)).unwrap();
            return decline(
                pos,
                format!(
                    "{} closes the open `<p>`, which still contains an unclosed {} ({}); \
                     browsers close it implicitly here. Add {} where it belongs",
                    tag_desc(by),
                    tag_desc(&top.0),
                    self.where_(top.1),
                    end_desc(&top.0)
                ),
            );
        }
        self.pop();
        Ok(())
    }

    /// Close the cell or caption at stack index `k` and everything inside
    /// it. The tree builder then clears formatting elements back to ONE
    /// marker: if an `<object>`, `<applet>` or `<marquee>` (each pushes a
    /// marker) is still open inside, the cell's own marker is left behind
    /// and a formatting element can later be re-created out of nowhere.
    fn close_cell(&mut self, k: usize, pos: usize, by: &str) -> R<()> {
        if let Some(e) = self.stack[k + 1..].iter().find(|e| {
            e.ns == Ns::Html && matches!(e.name.as_str(), "applet" | "marquee" | "object")
        }) {
            return decline(
                pos,
                format!(
                    "{} closes the table cell while {} from {} inside it is still open; \
                     add {} first",
                    by,
                    tag_desc(&e.name),
                    self.where_(e.pos),
                    end_desc(&e.name)
                ),
            );
        }
        while self.stack.len() > k {
            self.pop();
        }
        Ok(())
    }

    fn unclosed_decline<T>(&self, pos: usize, closing: &str, target: &str) -> R<T> {
        let top = self.top().map(|e| (e.name.clone(), e.pos));
        match top {
            Some((n, p)) => decline(
                pos,
                format!(
                    "{} would also close the still-open {} from {}; browsers repair \
                     this by restructuring the tree. Close {} first",
                    closing,
                    tag_desc(&n),
                    self.where_(p),
                    tag_desc(&n)
                ),
            ),
            None => decline(pos, format!("{} has no open {}", closing, tag_desc(target))),
        }
    }

    // ── Comments, doctype, text ─────────────────────────────

    fn doctype(&mut self, pos: usize) -> R<()> {
        if self.mode != IMode::Initial {
            return decline(
                pos,
                "`<!DOCTYPE>` is not at the start of the document; browsers ignore it there",
            );
        }
        self.mode = IMode::BeforeHtml;
        Ok(())
    }

    fn comment(&mut self) {
        if self.mode == IMode::Initial {
            // A comment does not leave the initial mode, but nothing here
            // depends on that distinction except the doctype check.
        }
    }

    /// Non-whitespace text at `pos`.
    fn text(&mut self, pos: usize) -> R<()> {
        loop {
            if self.template_depth > 0 || self.foreign > 0 || self.selects > 0 {
                return Ok(());
            }
            match self.mode {
                IMode::Initial | IMode::BeforeHtml => {
                    self.push(self.implied("html", pos))?;
                    self.mode = IMode::BeforeHead;
                }
                IMode::BeforeHead => {
                    self.push(self.implied("head", pos))?;
                    self.mode = IMode::InHead;
                }
                IMode::InHead => {
                    self.pop();
                    self.mode = IMode::AfterHead;
                }
                IMode::AfterHead => {
                    self.push(self.implied("body", pos))?;
                    self.mode = IMode::InBody;
                }
                IMode::AfterBody | IMode::AfterAfterBody => {
                    self.tail_clean = false;
                    self.mode = IMode::InBody;
                }
                IMode::InBody => {
                    if let Some(e) = self.top() {
                        if e.ns == Ns::Html
                            && matches!(
                                e.name.as_str(),
                                "table" | "tbody" | "thead" | "tfoot" | "tr" | "colgroup"
                            )
                        {
                            let n = e.name.clone();
                            return decline(
                                pos,
                                format!(
                                    "text directly inside {} (not in a cell); browsers move it \
                                     out in front of the table",
                                    tag_desc(&n)
                                ),
                            );
                        }
                    }
                    return Ok(());
                }
            }
        }
    }

    // ── Start tags ──────────────────────────────────────────

    fn start(&mut self, t: &Tag, pos: usize) -> R<Out> {
        if self.template_depth > 0 {
            return self.template_start(t, pos);
        }
        if self.foreign > 0 {
            return self.foreign_start(t, pos);
        }
        loop {
            match self.mode {
                IMode::Initial | IMode::BeforeHtml => {
                    if t.name == "html" {
                        let el = self.html_el(t, pos);
                        let boundary = el.boundary;
                        self.push(el)?;
                        self.mode = IMode::BeforeHead;
                        return Ok(self.out_simple(0, boundary));
                    }
                    self.push(self.implied("html", pos))?;
                    self.mode = IMode::BeforeHead;
                }
                IMode::BeforeHead => {
                    if t.name == "html" {
                        return self.dup_html(pos);
                    }
                    if t.name == "head" {
                        let indent = self.depth;
                        let el = self.html_el(t, pos);
                        self.push(el)?;
                        self.mode = IMode::InHead;
                        return Ok(self.out_simple(indent, false));
                    }
                    self.push(self.implied("head", pos))?;
                    self.mode = IMode::InHead;
                }
                IMode::InHead => {
                    if let Some(o) = self.head_element(t, pos)? {
                        return Ok(o);
                    }
                    if t.name == "head" {
                        return decline(
                            pos,
                            "a second `<head>` tag; browsers ignore it, so the formatter \
                             leaves the document alone",
                        );
                    }
                    if t.name == "html" {
                        return self.dup_html(pos);
                    }
                    self.pop();
                    self.mode = IMode::AfterHead;
                }
                IMode::AfterHead => {
                    if t.name == "body" {
                        let indent = self.depth;
                        let el = self.html_el(t, pos);
                        let boundary = el.boundary;
                        self.push(el)?;
                        self.mode = IMode::InBody;
                        return Ok(self.out_simple(indent, boundary));
                    }
                    if t.name == "frameset" {
                        return decline(pos, "`<frameset>` documents are not supported");
                    }
                    if t.name == "head" {
                        return decline(
                            pos,
                            "`<head>` after the head was closed is ignored by browsers",
                        );
                    }
                    if t.name == "html" {
                        return self.dup_html(pos);
                    }
                    // "After head" sends these back into the head; not `<noscript>`.
                    if t.name != "noscript" {
                        if let Some(o) = self.head_element(t, pos)? {
                            return Ok(o);
                        }
                    }
                    self.push(self.implied("body", pos))?;
                    self.mode = IMode::InBody;
                }
                IMode::AfterBody | IMode::AfterAfterBody => {
                    if t.name == "html" {
                        return self.dup_html(pos);
                    }
                    self.tail_clean = false;
                    self.mode = IMode::InBody;
                }
                IMode::InBody => return self.body_start(t, pos),
            }
        }
    }

    fn dup_html<T>(&self, pos: usize) -> R<T> {
        decline(
            pos,
            "a second `<html>` tag; browsers merge its attributes into the first one",
        )
    }

    fn out_simple(&self, indent: usize, boundary: bool) -> Out {
        Out {
            indent,
            lb: boundary,
            rb: boundary,
            content: Content::Data,
        }
    }

    /// Elements processed by the "in head" rules. Returns None if `t` is
    /// not one of them.
    fn head_element(&mut self, t: &Tag, pos: usize) -> R<Option<Out>> {
        let indent = self.depth;
        let content = match t.name.as_str() {
            "base" | "basefont" | "bgsound" | "link" | "meta" => Content::Data,
            "title" => Content::RawUntilEnd,
            "noscript" | "noframes" | "style" => Content::RawUntilEnd,
            "script" => Content::Script,
            "template" => {
                let outer_fmt = self.formatting_open();
                let el = self.html_el(t, pos);
                self.push(El {
                    boundary: false,
                    ..el
                })?;
                self.template_depth = 1;
                self.tmpl_fmt_seen = false;
                self.tmpl_marker_seen = false;
                self.tmpl_first = true;
                self.tmpl_outer_fmt = outer_fmt;
                return Ok(Some(self.out_simple(indent, false)));
            }
            _ => return Ok(None),
        };
        Ok(Some(Out {
            indent,
            lb: false,
            rb: false,
            content,
        }))
    }

    fn template_start(&mut self, t: &Tag, pos: usize) -> R<Out> {
        // A template whose content starts with `<col>` parses in "column
        // group" mode, where `<script>`, `<style>`, ... are ignored, so the
        // tokenizer never switches to raw text for them. Head elements are
        // inserted without choosing the mode, so they do not count as first.
        let head_like = matches!(
            t.name.as_str(),
            "base"
                | "basefont"
                | "bgsound"
                | "link"
                | "meta"
                | "noframes"
                | "script"
                | "style"
                | "template"
                | "title"
        );
        if !head_like && std::mem::take(&mut self.tmpl_first) && t.name == "col" {
            return decline(
                pos,
                "`<template>` whose content starts with `<col>`: browsers ignore most \
                 tags after it, which this formatter does not model",
            );
        }
        // `</template>` clears formatting elements only back to the LAST
        // marker. With an unclosed `<td>`/`<object>`/... inside, a formatting
        // element from inside or around the template can survive it and be
        // re-created later by whitespace. Which end tags the browser honours
        // inside a template depends on its insertion mode, so the formatter
        // does not try to follow it: formatting elements and markers in or
        // around one template are declined.
        self.tmpl_fmt_seen |= is_formatting(&t.name);
        self.tmpl_marker_seen |= is_marker(&t.name);
        if self.tmpl_marker_seen && (self.tmpl_fmt_seen || self.tmpl_outer_fmt) {
            return decline(
                pos,
                format!(
                    "{} inside a `<template>` that also has formatting elements (such as \
                     `<b>` or `<a>`) in or around it next to table cells, `<object>`, \
                     `<applet>` or `<marquee>`; browsers can carry a formatting element \
                     past the end of the template, which this formatter does not model",
                    tag_desc(&t.name)
                ),
            );
        }
        let content = match t.name.as_str() {
            "template" => {
                self.template_depth += 1;
                self.tmpl_first = true;
                Content::Data
            }
            "svg" | "math" => {
                return decline(
                    pos,
                    format!(
                        "{} inside `<template>` is not supported by the formatter",
                        tag_desc(&t.name)
                    ),
                )
            }
            "title" | "textarea" | "style" | "xmp" | "iframe" | "noembed" | "noframes"
            | "noscript" => Content::RawUntilEnd,
            "script" => Content::Script,
            "plaintext" => Content::Plaintext,
            _ => Content::Data,
        };
        Ok(Out {
            indent: self.depth,
            lb: false,
            rb: false,
            content,
        })
    }

    fn foreign_start(&mut self, t: &Tag, pos: usize) -> R<Out> {
        let top = self.top().expect("foreign content without an element");
        let top_ns = top.ns;
        let top_name = top.name.clone();
        let html_ip = match top_ns {
            Ns::Svg => matches!(top_name.as_str(), "foreignobject" | "desc" | "title"),
            Ns::Math => top_name == "annotation-xml",
            Ns::Html => false,
        };
        let text_ip =
            top_ns == Ns::Math && matches!(top_name.as_str(), "mi" | "mo" | "mn" | "ms" | "mtext");
        if html_ip || (text_ip && !matches!(t.name.as_str(), "mglyph" | "malignmark")) {
            return decline(
                pos,
                format!(
                    "{} inside SVG/MathML {} is parsed as HTML; the formatter does not \
                     support that mix",
                    tag_desc(&t.name),
                    tag_desc(&top_name)
                ),
            );
        }
        if is_foreign_breakout(&t.name) || (t.name == "font" && t.font_breakout) {
            return decline(
                pos,
                format!(
                    "HTML {} inside SVG/MathML; browsers close the SVG/MathML element here",
                    tag_desc(&t.name)
                ),
            );
        }
        let ns = match t.name.as_str() {
            "svg" => Ns::Svg,
            "math" => Ns::Math,
            _ => top_ns,
        };
        if !t.self_closing {
            self.push(El {
                name: t.name.clone(),
                ns,
                explicit: true,
                boundary: false,
                frozen: false,
                pos,
            })?;
        }
        Ok(Out {
            indent: self.depth,
            lb: false,
            rb: false,
            content: Content::Data,
        })
    }

    /// Nearest table-ish element: (index, name).
    fn table_ctx(&self) -> Option<(usize, &str)> {
        if self.tablish == 0 {
            return None;
        }
        for (k, e) in self.stack.iter().enumerate().rev() {
            if e.ns != Ns::Html {
                continue;
            }
            match e.name.as_str() {
                "table" | "caption" | "colgroup" | "tbody" | "thead" | "tfoot" | "tr" | "td"
                | "th" => return Some((k, e.name.as_str())),
                "html" | "template" => return None,
                _ => {}
            }
        }
        None
    }

    fn body_start(&mut self, t: &Tag, pos: usize) -> R<Out> {
        if self.selects > 0 {
            return self.select_start(t, pos);
        }
        if let Some((k, name)) = self.table_ctx() {
            let is_top = k + 1 == self.stack.len();
            match name {
                "table" | "tbody" | "thead" | "tfoot" | "tr" => {
                    if !is_top {
                        return decline(pos, "unexpected content inside a table");
                    }
                    return self.table_start(t, pos);
                }
                "colgroup" => {
                    if !is_top {
                        return decline(pos, "unexpected content inside `<colgroup>`");
                    }
                    if t.name == "col" {
                        return Ok(self.out_simple(self.depth, true));
                    }
                    if t.name == "template" {
                        return Ok(self.head_element(t, pos)?.unwrap());
                    }
                    self.pop();
                    return self.body_start(t, pos);
                }
                _ => {
                    // In cell / in caption.
                    if is_table_structure(&t.name) {
                        self.close_cell(k, pos, &tag_desc(&t.name))?;
                        return self.body_start(t, pos);
                    }
                }
            }
        }
        if let Some(o) = self.head_element(t, pos)? {
            return Ok(o);
        }
        let name = t.name.as_str();
        let top_before_boundary = self.top_boundary();
        let n_before = self.stack.len();
        match name {
            "html" => return self.dup_html(pos),
            "body" => {
                return decline(
                    pos,
                    "a second `<body>` tag; browsers merge its attributes into the first one",
                )
            }
            "frameset" | "frame" => return decline(pos, "frames are not supported"),
            "head" => {
                return decline(pos, "`<head>` inside the body is ignored by browsers");
            }
            "caption" | "col" | "colgroup" | "tbody" | "td" | "tfoot" | "th" | "thead" | "tr" => {
                return decline(
                    pos,
                    format!(
                        "{} outside a `<table>`; browsers drop the tag",
                        tag_desc(name)
                    ),
                )
            }
            "table"
                if self.stack.iter().any(|e| e.ns == Ns::Html && e.name == "p")
                    && self.in_scope("p", &["button"]) =>
            {
                return decline(
                    pos,
                    "`<table>` inside an open `<p>`; browsers nest it or close the \
                     paragraph depending on the doctype. Close the `<p>` first",
                );
            }
            _ => {}
        }
        if closes_p(name) {
            self.close_p(pos, name)?;
        }
        match name {
            "li" | "dd" | "dt" => {
                let targets: &[&str] = if name == "li" { &["li"] } else { &["dd", "dt"] };
                let mut found = None;
                for (k, e) in self.stack.iter().enumerate().rev() {
                    if e.ns == Ns::Html && targets.contains(&e.name.as_str()) {
                        found = Some(k);
                        break;
                    }
                    if e.ns != Ns::Html
                        || (is_special(&e.name)
                            && !matches!(e.name.as_str(), "address" | "div" | "p"))
                    {
                        // `<search>` joined the "special" list in 2023; older
                        // parsers walk past it and close the outer item.
                        if e.name == "search"
                            && self.stack[..k]
                                .iter()
                                .any(|x| x.ns == Ns::Html && targets.contains(&x.name.as_str()))
                        {
                            return decline(
                                pos,
                                format!(
                                    "{} inside `<search>` inside an open {}: browsers and \
                                     parsers disagree on whether this closes the outer one; \
                                     add the end tag",
                                    tag_desc(name),
                                    tag_desc(targets[0])
                                ),
                            );
                        }
                        break;
                    }
                }
                if let Some(k) = found {
                    let closed = self.stack[k].name.clone();
                    self.pop_implied(&closed);
                    if self.stack.len() != k + 1 {
                        let top = self.top().map(|e| (e.name.clone(), e.pos)).unwrap();
                        return decline(
                            pos,
                            format!(
                                "{} implicitly closes the previous {} from {}, which still \
                                 contains an unclosed {} ({}); add {}",
                                tag_desc(name),
                                tag_desc(&closed),
                                self.where_(self.stack[k].pos),
                                tag_desc(&top.0),
                                self.where_(top.1),
                                end_desc(&top.0)
                            ),
                        );
                    }
                    self.pop();
                }
                // `<li>`/`<dd>`/`<dt>` also close a `<p>` (done above).
            }
            "h1" | "h2" | "h3" | "h4" | "h5" | "h6" => {
                if self
                    .top()
                    .is_some_and(|e| e.ns == Ns::Html && is_heading(&e.name))
                {
                    self.pop();
                }
            }
            "form" => {
                if self.form_open {
                    return decline(
                        pos,
                        "`<form>` inside another form; browsers ignore the inner `<form>` tag",
                    );
                }
                self.form_open = true;
            }
            "a" => {
                for e in self.stack.iter().rev() {
                    if e.ns == Ns::Html
                        && matches!(
                            e.name.as_str(),
                            "td" | "th" | "caption" | "applet" | "marquee" | "object" | "template"
                        )
                    {
                        break;
                    }
                    if e.ns == Ns::Html && e.name == "a" {
                        return decline(
                            pos,
                            format!(
                                "`<a>` inside the still-open `<a>` from {}; browsers close the \
                                 first link and restructure the tree",
                                self.where_(e.pos)
                            ),
                        );
                    }
                }
            }
            "nobr" => {
                if self.in_scope("nobr", &[]) {
                    return decline(pos, "`<nobr>` inside an open `<nobr>`");
                }
            }
            "button" => {
                if self.in_scope("button", &[]) {
                    return decline(
                        pos,
                        "`<button>` inside an open `<button>`; browsers close the first one",
                    );
                }
            }
            "option" | "optgroup" => {
                if self.top_is("option") {
                    self.pop();
                }
            }
            "rb" | "rtc" | "rp" | "rt" => {
                // With a `<ruby>` in scope these close optional-end-tag
                // elements (including a stray `<p>`, `<dt>`, ...).
                if self.in_scope("ruby", &[]) {
                    self.pop_implied(if matches!(name, "rp" | "rt") {
                        "rtc"
                    } else {
                        ""
                    });
                }
            }
            "isindex" | "menuitem" => {
                return decline(pos, format!("obsolete {} element", tag_desc(name)));
            }
            _ => {}
        }
        // Whitespace before this tag lands in the element that was current
        // before any implicit close: it is unrendered if that element (when
        // closed here) is a block, and this tag starts a block.
        let popped = self.stack.len() < n_before;
        let el = self.html_el(t, pos);
        let own = el.boundary;
        let lb = own && (!popped || top_before_boundary);
        let indent = self.depth;
        let content = match name {
            "textarea" | "xmp" | "iframe" | "noembed" | "noscript" => Content::RawUntilEnd,
            "plaintext" => Content::Plaintext,
            _ => Content::Data,
        };
        if name == "svg" || name == "math" {
            if !t.self_closing {
                self.push(El {
                    name: t.name.clone(),
                    ns: if name == "svg" { Ns::Svg } else { Ns::Math },
                    explicit: true,
                    boundary: false,
                    frozen: false,
                    pos,
                })?;
            }
            return Ok(Out {
                indent,
                lb: false,
                rb: false,
                content,
            });
        }
        let void = is_void(name);
        if content == Content::RawUntilEnd {
            // Raw-text element: not pushed; its end tag is read with it.
            return Ok(Out {
                indent,
                lb,
                rb: own,
                content,
            });
        }
        if !void {
            self.push(el)?;
            if content == Content::Plaintext {
                // Everything after is text; nothing to model.
            }
        }
        Ok(Out {
            indent,
            lb,
            rb: own,
            content,
        })
    }

    fn select_start(&mut self, t: &Tag, pos: usize) -> R<Out> {
        match t.name.as_str() {
            "option" => {
                if self.top_is("option") {
                    self.pop();
                }
            }
            "optgroup" => {
                if self.top_is("option") {
                    self.pop();
                }
                if self.top_is("optgroup") {
                    self.pop();
                }
            }
            other => {
                return decline(
                    pos,
                    format!(
                        "{} inside `<select>`; browsers parse select contents with special \
                         rules, so only `<option>` and `<optgroup>` are supported there",
                        tag_desc(other)
                    ),
                )
            }
        }
        let indent = self.depth;
        let el = self.html_el(t, pos);
        self.push(el)?;
        Ok(self.out_simple(indent, false))
    }

    /// Start tag while the current node is table, tbody/thead/tfoot or tr.
    fn table_start(&mut self, t: &Tag, pos: usize) -> R<Out> {
        let name = t.name.as_str();
        let pop_to = |m: &mut Self, keep: &[&str]| {
            while let Some(e) = m.top() {
                if e.ns == Ns::Html && keep.contains(&e.name.as_str()) {
                    break;
                }
                m.pop();
            }
        };
        match name {
            "caption" | "colgroup" | "tbody" | "thead" | "tfoot" | "col" => {
                pop_to(self, &["table"]);
                if name == "col" {
                    self.push(self.implied("colgroup", pos))?;
                    return Ok(self.out_simple(self.depth, true));
                }
            }
            "tr" => {
                pop_to(self, &["tbody", "thead", "tfoot", "table"]);
                if self.top_is("table") {
                    self.push(self.implied("tbody", pos))?;
                }
            }
            "td" | "th" => {
                pop_to(self, &["tr", "tbody", "thead", "tfoot", "table"]);
                if self.top_is("table") {
                    self.push(self.implied("tbody", pos))?;
                }
                if !self.top_is("tr") {
                    self.push(self.implied("tr", pos))?;
                }
            }
            "style" | "script" | "template" => {
                return Ok(self.head_element(t, pos)?.unwrap());
            }
            "table" => {
                return decline(
                    pos,
                    "`<table>` directly inside a table (not in a cell); browsers close the \
                     outer table here",
                )
            }
            _ => {
                let top = self.top().map(|e| e.name.clone()).unwrap_or_default();
                return decline(
                    pos,
                    format!(
                        "{} directly inside {} (not in a cell); browsers move it out in front \
                         of the table",
                        tag_desc(name),
                        tag_desc(&top)
                    ),
                );
            }
        }
        let indent = self.depth;
        let el = self.html_el(t, pos);
        let own = el.boundary;
        self.push(el)?;
        Ok(Out {
            indent,
            lb: own,
            rb: own,
            content: Content::Data,
        })
    }

    // ── End tags ────────────────────────────────────────────

    fn end(&mut self, t: &Tag, pos: usize) -> R<Out> {
        let name = t.name.as_str();
        if self.template_depth > 0 {
            if name == "template" {
                self.template_depth -= 1;
                if self.template_depth == 0 {
                    // The template element itself is on the model stack.
                    let el = self.pop();
                    debug_assert_eq!(el.name, "template");
                    return Ok(self.out_simple(self.depth, false));
                }
            }
            return Ok(self.out_simple(self.depth, false));
        }
        if self.foreign > 0 {
            if matches!(name, "br" | "p") {
                return decline(
                    pos,
                    format!(
                        "{} inside SVG/MathML; browsers break out of the SVG/MathML element here",
                        end_desc(name)
                    ),
                );
            }
            let mut k = self.stack.len();
            while k > 0 {
                k -= 1;
                let e = &self.stack[k];
                if e.ns == Ns::Html {
                    return decline(
                        pos,
                        format!(
                            "{} closes an SVG/MathML element that was never closed; add the \
                             missing end tag",
                            end_desc(name)
                        ),
                    );
                }
                if e.name == name {
                    while self.stack.len() > k {
                        self.pop();
                    }
                    return Ok(self.out_simple(self.depth, false));
                }
            }
            return decline(pos, format!("{} has no open element", end_desc(name)));
        }
        loop {
            match self.mode {
                IMode::Initial | IMode::BeforeHtml => {
                    if !matches!(name, "head" | "body" | "html" | "br") {
                        return self.stray_end(name, pos);
                    }
                    self.push(self.implied("html", pos))?;
                    self.mode = IMode::BeforeHead;
                }
                IMode::BeforeHead => {
                    if !matches!(name, "head" | "body" | "html" | "br") {
                        return self.stray_end(name, pos);
                    }
                    self.push(self.implied("head", pos))?;
                    self.mode = IMode::InHead;
                }
                IMode::InHead => {
                    if name == "head" {
                        self.pop();
                        self.mode = IMode::AfterHead;
                        return Ok(self.out_simple(self.depth, false));
                    }
                    if !matches!(name, "body" | "html" | "br") {
                        return self.stray_end(name, pos);
                    }
                    self.pop();
                    self.mode = IMode::AfterHead;
                }
                IMode::AfterHead => {
                    if !matches!(name, "body" | "html" | "br") {
                        return self.stray_end(name, pos);
                    }
                    self.push(self.implied("body", pos))?;
                    self.mode = IMode::InBody;
                }
                IMode::AfterBody => {
                    if name == "html" {
                        self.mode = IMode::AfterAfterBody;
                        return Ok(Out {
                            indent: 0,
                            lb: self.top_is("body") && self.top_boundary(),
                            rb: self.top_is("body") && self.top_boundary(),
                            content: Content::Data,
                        });
                    }
                    if name == "body" {
                        return decline(pos, "a second `</body>`; browsers ignore it");
                    }
                    self.tail_clean = false;
                    self.mode = IMode::InBody;
                }
                IMode::AfterAfterBody => {
                    self.tail_clean = false;
                    self.mode = IMode::InBody;
                }
                IMode::InBody => return self.body_end(name, pos),
            }
        }
    }

    fn stray_end<T>(&self, name: &str, pos: usize) -> R<T> {
        decline(
            pos,
            format!(
                "stray {} with no matching open element; browsers ignore it",
                end_desc(name)
            ),
        )
    }

    fn body_end(&mut self, name: &str, pos: usize) -> R<Out> {
        let top_before_boundary = self.top_boundary();
        if self.selects > 0 {
            return self.select_end(name, pos);
        }
        if let Some((k, ctx)) = self.table_ctx() {
            let ctx = ctx.to_string();
            let is_top = k + 1 == self.stack.len();
            match ctx.as_str() {
                "table" | "tbody" | "thead" | "tfoot" | "tr" | "colgroup" => {
                    if !is_top {
                        return decline(pos, "unexpected content inside a table");
                    }
                    return self.table_end(name, pos);
                }
                _ => {
                    // In cell / caption.
                    match name {
                        "td" | "th" | "caption" => {
                            if ctx != name {
                                return decline(
                                    pos,
                                    format!(
                                        "{} while the open cell is {} from {}; browsers ignore it",
                                        end_desc(name),
                                        tag_desc(&ctx),
                                        self.where_(self.stack[k].pos)
                                    ),
                                );
                            }
                            let el_boundary = self.stack[k].boundary;
                            self.close_cell(k, pos, &end_desc(name))?;
                            return Ok(Out {
                                indent: self.depth,
                                lb: true,
                                rb: el_boundary,
                                content: Content::Data,
                            });
                        }
                        "table" | "tbody" | "thead" | "tfoot" | "tr" => {
                            if ctx == "caption" && name != "table" {
                                return decline(
                                    pos,
                                    format!(
                                        "{} inside `<caption>`; browsers ignore it",
                                        end_desc(name)
                                    ),
                                );
                            }
                            self.close_cell(k, pos, &end_desc(name))?;
                            return self.table_end(name, pos);
                        }
                        "body" | "col" | "colgroup" | "html" => {
                            return decline(
                                pos,
                                format!(
                                    "{} inside the table cell from {}; browsers ignore it",
                                    end_desc(name),
                                    self.where_(self.stack[k].pos)
                                ),
                            );
                        }
                        _ => {}
                    }
                }
            }
        }
        match name {
            "body" | "html" => {
                if !self.in_scope("body", &[]) {
                    return self.stray_end(name, pos);
                }
                let at_body = self.top_is("body");
                self.in_tail = true;
                self.tail_clean = self.tail_clean && at_body && self.stack.len() == 2;
                self.mode = if name == "body" {
                    IMode::AfterBody
                } else {
                    IMode::AfterAfterBody
                };
                return Ok(Out {
                    indent: self.depth.saturating_sub(1),
                    lb: top_before_boundary,
                    rb: at_body && top_before_boundary,
                    content: Content::Data,
                });
            }
            "br" => {
                return decline(
                    pos,
                    "`</br>` is turned into a `<br>` element by browsers; write `<br>`",
                )
            }
            "form" => {
                self.form_open = false;
            }
            "template" => return self.stray_end(name, pos),
            _ => {}
        }
        let special = is_special(name) || is_heading(name) || name == "p";
        if is_formatting(name) || !special {
            // Adoption agency / "any other end tag": only the trivial case.
            if !self.top_is(name) {
                if self
                    .stack
                    .iter()
                    .any(|e| e.ns == Ns::Html && e.name == name)
                {
                    return self.unclosed_decline(pos, &end_desc(name), name);
                }
                return self.stray_end(name, pos);
            }
            let el = self.pop();
            return Ok(Out {
                indent: self.depth,
                lb: el.boundary,
                rb: el.boundary,
                content: Content::Data,
            });
        }
        let present = if is_heading(name) {
            ["h1", "h2", "h3", "h4", "h5", "h6"]
                .iter()
                .any(|h| self.in_scope(h, &[]))
        } else {
            let extra: &[&str] = match name {
                "p" => &["button"],
                "li" => &["ol", "ul"],
                _ => &[],
            };
            self.in_scope(name, extra)
        };
        if !present {
            if name == "p" {
                return decline(
                    pos,
                    "stray `</p>` with no open paragraph; browsers insert an empty `<p></p>` here",
                );
            }
            return self.stray_end(name, pos);
        }
        self.pop_implied(name);
        let matches_top = if is_heading(name) {
            self.top()
                .is_some_and(|e| e.ns == Ns::Html && is_heading(&e.name))
        } else {
            self.top_is(name)
        };
        if !matches_top {
            return self.unclosed_decline(pos, &end_desc(name), name);
        }
        if is_heading(name) && !self.top_is(name) {
            let top = self.top().map(|e| (e.name.clone(), e.pos)).unwrap();
            return decline(
                pos,
                format!(
                    "{} closes {} from {}; use the matching end tag",
                    end_desc(name),
                    tag_desc(&top.0),
                    self.where_(top.1)
                ),
            );
        }
        let el = self.pop();
        Ok(Out {
            indent: self.depth,
            lb: el.boundary && top_before_boundary,
            rb: el.boundary,
            content: Content::Data,
        })
    }

    fn select_end(&mut self, name: &str, pos: usize) -> R<Out> {
        match name {
            "option" => {
                if !self.top_is("option") {
                    return self.stray_end(name, pos);
                }
                self.pop();
            }
            "optgroup" => {
                if self.top_is("option")
                    && self.stack.len() >= 2
                    && self.stack[self.stack.len() - 2].name == "optgroup"
                {
                    self.pop();
                }
                if !self.top_is("optgroup") {
                    return self.stray_end(name, pos);
                }
                self.pop();
            }
            "select" => {
                while !self.top_is("select") {
                    if self.top_is("option") || self.top_is("optgroup") {
                        self.pop();
                    } else {
                        return decline(pos, "unexpected content inside `<select>`");
                    }
                }
                self.pop();
            }
            _ => {
                return decline(
                    pos,
                    format!(
                        "{} inside `<select>`; browsers ignore it there",
                        end_desc(name)
                    ),
                )
            }
        }
        Ok(self.out_simple(self.depth, false))
    }

    /// End tag while the current node is table, a row group, tr or colgroup.
    fn table_end(&mut self, name: &str, pos: usize) -> R<Out> {
        if self.top_is("colgroup") {
            if name == "colgroup" {
                self.pop();
                return Ok(self.out_simple(self.depth, true));
            }
            if name == "col" {
                return self.stray_end(name, pos);
            }
            self.pop();
        }
        match name {
            "tr" => {
                if !self.top_is("tr") {
                    return self.stray_end(name, pos);
                }
            }
            "tbody" | "thead" | "tfoot" => {
                if self.top_is("tr") {
                    // Only if the named section is the one that is open.
                    let n = self.stack.len();
                    if n < 2 || self.stack[n - 2].name != name {
                        return self.stray_end(name, pos);
                    }
                    self.pop();
                }
                if !self.top_is(name) {
                    return self.stray_end(name, pos);
                }
            }
            "table" => {
                while !self.top_is("table") {
                    self.pop();
                }
            }
            _ => {
                return decline(
                    pos,
                    format!(
                        "{} directly inside a table (not in a cell); browsers ignore it",
                        end_desc(name)
                    ),
                )
            }
        }
        let el = self.pop();
        Ok(Out {
            indent: self.depth,
            lb: true,
            rb: el.boundary,
            content: Content::Data,
        })
    }
}

// ═══════════════════════════════════════════════════════════
// Driver
// ═══════════════════════════════════════════════════════════

#[derive(Clone, Copy, PartialEq, Eq, Debug)]
enum Kind {
    /// Whitespace-only text between markup (a candidate gap).
    Gap,
    /// Any other text, including raw-text element bodies.
    Text,
    Comment,
    Markup,
}

struct Item {
    start: usize,
    end: usize,
    kind: Kind,
    lb: bool,
    rb: bool,
    indent: usize,
    after: GapState,
    tail: bool,
}

fn ws_only(s: &[u8]) -> bool {
    s.iter().all(|&c| is_ws(c))
}

fn run(input: &str, mode: Mode) -> HtmlOutcome {
    let b = input.as_bytes();
    let unchanged = |msg: String| HtmlOutcome {
        text: input.to_string(),
        message: Some(msg),
    };
    if b.len() > MAX_HTML_INPUT_BYTES {
        return unchanged(format!(
            "HTML formatter left the text unchanged: it is {} bytes, the limit is {} bytes",
            b.len(),
            MAX_HTML_INPUT_BYTES
        ));
    }
    match analyse(b) {
        Ok((items, tail_clean)) => HtmlOutcome {
            text: render(b, &items, tail_clean, mode),
            message: None,
        },
        Err(d) => unchanged(format!(
            "HTML formatter left the text unchanged. {}: {}.",
            at(b, d.pos),
            d.what
        )),
    }
}

fn analyse(b: &[u8]) -> R<(Vec<Item>, bool)> {
    if let Some((pos, what)) = find_server_code(b) {
        return decline(
            pos,
            format!(
                "{} found; whitespace inside server-side code can be significant, so \
                 template files are not re-indented",
                what
            ),
        );
    }
    let len = b.len();
    let mut m = Model::new(b);
    let mut items: Vec<Item> = Vec::new();
    let mut i = 0;

    let push_text = |m: &mut Model, items: &mut Vec<Item>, s: usize, e: usize| -> R<()> {
        if s >= e {
            return Ok(());
        }
        let slice = &b[s..e];
        if ws_only(slice) && !slice.contains(&b'\x0C') {
            items.push(Item {
                start: s,
                end: e,
                kind: Kind::Gap,
                lb: false,
                rb: false,
                indent: 0,
                after: GapState::Frozen,
                tail: m.in_tail,
            });
            return Ok(());
        }
        if !ws_only(slice) {
            let first = s + slice.iter().position(|&c| !is_ws(c)).unwrap_or(0);
            m.text(first)?;
        }
        items.push(Item {
            start: s,
            end: e,
            kind: Kind::Text,
            lb: false,
            rb: false,
            indent: 0,
            after: GapState::Frozen,
            tail: m.in_tail,
        });
        Ok(())
    };

    while i < len {
        // Data state: scan text up to the next markup token.
        let text_start = i;
        let mut j = i;
        let mut markup: Option<usize> = None;
        while let Some(k) = find_byte(b, j, b'<') {
            let next = b.get(k + 1).copied();
            let is_markup = match next {
                Some(c) if c.is_ascii_alphabetic() => true,
                Some(b'!') | Some(b'?') => true,
                Some(b'/') => k + 2 < len,
                _ => false,
            };
            if is_markup {
                markup = Some(k);
                break;
            }
            j = k + 1;
        }
        let k = match markup {
            Some(k) => k,
            None => {
                push_text(&mut m, &mut items, text_start, len)?;
                break;
            }
        };
        push_text(&mut m, &mut items, text_start, k)?;

        let next = b[k + 1];
        let mut item = Item {
            start: k,
            end: k,
            kind: Kind::Markup,
            lb: false,
            rb: false,
            indent: m.depth,
            after: GapState::Frozen,
            tail: m.in_tail,
        };
        let mut raw: Option<(Content, String, bool, usize)> = None;
        match next {
            b'!' => {
                if b[k + 2..].starts_with(b"--") {
                    item.end = comment_end(b, k + 4);
                    item.kind = Kind::Comment;
                    m.comment();
                } else if starts_with_ci(b, k + 2, b"doctype") {
                    item.end = find_byte(b, k + 2, b'>').map_or(len, |e| e + 1);
                    m.doctype(k)?;
                    item.lb = true;
                    item.rb = true;
                } else if b[k + 2..].starts_with(b"[CDATA[")
                    && m.foreign > 0
                    && m.template_depth == 0
                {
                    item.end = find_seq(b, k + 9, b"]]>").map_or(len, |e| e + 3);
                    item.kind = Kind::Comment;
                } else {
                    item.end = find_byte(b, k + 2, b'>').map_or(len, |e| e + 1);
                    item.kind = Kind::Comment;
                    m.comment();
                }
            }
            b'?' => {
                item.end = find_byte(b, k + 2, b'>').map_or(len, |e| e + 1);
                item.kind = Kind::Comment;
                m.comment();
            }
            b'/' => {
                let c = b[k + 2];
                if c.is_ascii_alphabetic() {
                    let (tag, end) = match parse_tag(b, k + 2) {
                        Some(x) => x,
                        None => {
                            return decline(
                                k,
                                "end tag is never closed with `>` before the end of the text; \
                                 browsers drop it",
                            )
                        }
                    };
                    item.end = end;
                    // Whitespace before an end tag becomes the last child of
                    // the element that is current now: unrendered only if
                    // that element is a block.
                    let c_boundary = m.top_boundary();
                    let o = m.end(&tag, k)?;
                    item.lb = o.lb && c_boundary;
                    item.rb = o.rb;
                    item.indent = o.indent;
                } else if c == b'>' {
                    return decline(k, "`</>` is discarded by browsers");
                } else {
                    item.end = find_byte(b, k + 2, b'>').map_or(len, |e| e + 1);
                    item.kind = Kind::Comment;
                    m.comment();
                }
            }
            _ => {
                let (tag, end) = match parse_tag(b, k + 1) {
                    Some(x) => x,
                    None => {
                        return decline(
                            k,
                            "tag is never closed with `>` before the end of the text (an \
                             unbalanced quote in an attribute does this); browsers drop it",
                        )
                    }
                };
                item.end = end;
                // If this start tag implicitly closes the current element,
                // the whitespace before it ends up as that element's last
                // child, so that element must be a block as well.
                let c_boundary = m.top_boundary();
                let pops0 = m.pops;
                let o = m.start(&tag, k)?;
                item.lb = o.lb && (m.pops == pops0 || c_boundary);
                item.rb = o.rb;
                item.indent = o.indent;
                if o.content != Content::Data {
                    raw = Some((o.content, tag.name.clone(), o.rb, o.indent));
                }
            }
        }
        // Nothing between a raw-text start tag and its body is a gap.
        item.after = if raw.is_some() {
            GapState::Frozen
        } else {
            m.gap_state()
        };
        item.tail = m.in_tail;
        i = item.end;
        items.push(item);

        if let Some((content, name, boundary, indent)) = raw {
            let body_end = match content {
                Content::Script => find_script_end(b, i),
                Content::RawUntilEnd => find_raw_end(b, i, name.as_bytes()),
                Content::Plaintext | Content::Data => None,
            };
            let e = body_end.unwrap_or(len);
            if e > i {
                items.push(Item {
                    start: i,
                    end: e,
                    kind: Kind::Text,
                    lb: false,
                    rb: false,
                    indent: 0,
                    after: GapState::Frozen,
                    tail: m.in_tail,
                });
            }
            i = e;
            if let Some(e) = body_end {
                let end = match parse_tag(b, e + 2) {
                    Some((_, end)) => end,
                    None => {
                        return decline(
                            e,
                            format!(
                                "{} is never closed with `>` before the end of the text",
                                end_desc(&name)
                            ),
                        )
                    }
                };
                if name == "template" {
                    unreachable!();
                }
                items.push(Item {
                    start: e,
                    end,
                    kind: Kind::Markup,
                    lb: boundary,
                    rb: boundary,
                    indent,
                    after: m.gap_state(),
                    tail: m.in_tail,
                });
                i = end;
            }
        }
    }
    Ok((items, m.tail_clean))
}

fn render(b: &[u8], items: &[Item], tail_clean: bool, mode: Mode) -> String {
    let n = items.len();
    // Effective right-boundary of the nearest markup token to the left of
    // each item (comments and gaps are transparent, text is not).
    let mut left_rb = vec![false; n];
    let mut cur = false;
    for (k, it) in items.iter().enumerate() {
        left_rb[k] = cur;
        match it.kind {
            Kind::Markup => cur = it.rb && !(it.tail && !tail_clean),
            Kind::Text => cur = false,
            Kind::Comment | Kind::Gap => {}
        }
    }
    let mut right_lb = vec![false; n];
    cur = false;
    for k in (0..n).rev() {
        right_lb[k] = cur;
        match items[k].kind {
            Kind::Markup => cur = items[k].lb && !(items[k].tail && !tail_clean),
            Kind::Text => cur = false,
            Kind::Comment | Kind::Gap => {}
        }
    }
    let eol: &str = if b.windows(2).any(|w| w == b"\r\n") {
        "\r\n"
    } else {
        "\n"
    };
    let mut out: Vec<u8> = Vec::with_capacity(b.len() + b.len() / 4);
    let is_tagish = |k: usize| matches!(items[k].kind, Kind::Markup | Kind::Comment);
    // Rewrite one gap (possibly empty) that sits right after item `l`, whose
    // effective neighbours are `l_eff`/`r_eff`, before a token at `indent`.
    // Content after `</body>` is appended to the open element, after any
    // whitespace before `</body>`: so both sides of the tail freeze unless
    // the tail holds nothing but whitespace, comments and `</html>`.
    let gap = |out: &mut Vec<u8>, src: &[u8], l: &Item, r: &Item, l_eff: bool, r_eff: bool| {
        let state = l.after;
        let indent = r.indent;
        let modifiable = state != GapState::Frozen
            && !((l.tail || r.tail) && !tail_clean)
            && (state == GapState::Free || l_eff || r_eff);
        if !modifiable {
            out.extend_from_slice(src);
            return;
        }
        if let Mode::Pretty(unit) = mode {
            let breaks = src
                .iter()
                .enumerate()
                .filter(|&(p, &c)| c == b'\n' || (c == b'\r' && src.get(p + 1) != Some(&b'\n')))
                .count();
            out.extend_from_slice(eol.as_bytes());
            if breaks >= 2 {
                out.extend_from_slice(eol.as_bytes());
            }
            out.resize(out.len() + unit * indent, b' ');
        }
    };
    for (k, it) in items.iter().enumerate() {
        let src = &b[it.start..it.end];
        match it.kind {
            Kind::Gap if k > 0 && k + 1 < n && is_tagish(k - 1) && is_tagish(k + 1) => {
                gap(
                    &mut out,
                    src,
                    &items[k - 1],
                    &items[k + 1],
                    left_rb[k],
                    right_lb[k],
                );
            }
            Kind::Markup | Kind::Comment if k > 0 && is_tagish(k - 1) => {
                // Two tokens with nothing between them: an empty gap.
                let r_eff = if it.kind == Kind::Markup {
                    it.lb && !(it.tail && !tail_clean)
                } else {
                    right_lb[k]
                };
                gap(&mut out, b"", &items[k - 1], it, left_rb[k], r_eff);
                out.extend_from_slice(src);
            }
            _ => out.extend_from_slice(src),
        }
    }
    // Only whole whitespace runs were replaced, so the bytes stay UTF-8.
    String::from_utf8(out).expect("formatter produced invalid UTF-8")
}

#[cfg(test)]
#[path = "html_fmt_fuzz.rs"]
mod fuzz;

#[cfg(test)]
mod tests {
    use super::*;

    fn ok(input: &str) -> String {
        let o = format_html_report(input, 2);
        assert!(
            o.message.is_none(),
            "declined: {:?}\ninput: {}",
            o.message,
            input
        );
        o.text
    }

    #[test]
    fn empty_input_does_not_panic() {
        assert_eq!(format_html("", 2), "");
    }

    #[test]
    fn whitespace_only_is_untouched() {
        assert_eq!(ok("   \n  \t  "), "   \n  \t  ");
    }

    #[test]
    fn html5_full_page() {
        let s = "<!DOCTYPE html><html lang=\"en\"><head><meta charset=\"UTF-8\"><title>Test</title></head><body><h1>Hello</h1></body></html>";
        let out = ok(s);
        assert_eq!(
            out,
            "<!DOCTYPE html>\n<html lang=\"en\">\n  <head>\n    <meta charset=\"UTF-8\">\n    \
             <title>Test</title>\n  </head>\n  <body>\n    <h1>Hello</h1>\n  </body>\n</html>"
        );
    }

    #[test]
    fn list_items_get_their_own_lines() {
        assert_eq!(
            ok("<ul><li>One</li><li>Two</li></ul>"),
            "<ul>\n  <li>One</li>\n  <li>Two</li>\n</ul>"
        );
    }

    #[test]
    fn inline_whitespace_is_never_touched() {
        for s in [
            "<p><b>a</b><i>b</i></p>",
            "<p><b>a</b> <i>b</i></p>",
            "<p>Hello <b>world</b>!</p>",
            "<span>a</span>\n\n\n<span>b</span>",
        ] {
            let out = ok(s);
            assert_eq!(
                out.replace("\n  ", "").replace('\n', ""),
                s.replace('\n', "")
            );
            assert!(out.contains(&s[3..s.len() - 4]), "{:?} -> {:?}", s, out);
        }
    }

    #[test]
    fn pre_textarea_script_style_are_byte_identical() {
        let s = "<div><pre>\n  a  \n <b> x </b>\n</pre><textarea>\n  t </textarea>\
                 <script>\n if (a<b) { x = '</div>'; }\n</script><style>\n p { }\n</style></div>";
        let out = ok(s);
        for part in [
            "<pre>\n  a  \n <b> x </b>\n</pre>",
            "<textarea>\n  t </textarea>",
            "<script>\n if (a<b) { x = '</div>'; }\n</script>",
            "<style>\n p { }\n</style>",
        ] {
            assert!(out.contains(part), "lost {:?} in {:?}", part, out);
        }
    }

    #[test]
    fn void_elements_not_indented() {
        let out = ok("<div><br><img src=\"x\"><hr><meta charset=\"utf-8\"></div>");
        assert!(out.ends_with("\n</div>"), "{}", out);
        assert!(
            out.contains("<br><img src=\"x\">"),
            "inline voids must stay together: {}",
            out
        );
    }

    #[test]
    fn comments_preserved() {
        let s = "<!-- header --><div><!-- nested --><p>x</p></div><!-- footer -->";
        let out = ok(s);
        assert!(out.contains("<!-- header -->"));
        assert!(out.contains("<!-- nested -->"));
        assert!(out.contains("<!-- footer -->"));
    }

    #[test]
    fn entities_and_attributes_verbatim() {
        let s = "<div data-x='a > b' ID=Main class=\"x\"  hidden><p>&amp; &lt;tag&gt; &copy; &#x1F600; &nbsp;</p></div>";
        let out = ok(s);
        assert!(out.contains("<div data-x='a > b' ID=Main class=\"x\"  hidden>"));
        assert!(out.contains("&amp; &lt;tag&gt; &copy; &#x1F600; &nbsp;"));
    }

    #[test]
    fn unicode_survives() {
        let s = "<div title=\"🚀\"><p dir=\"rtl\">مرحبا</p><p>你好 こんにちは 🎨</p></div>";
        let out = ok(s);
        for w in ["🚀", "مرحبا", "你好", "🎨"] {
            assert!(out.contains(w));
        }
    }

    #[test]
    fn unclosed_tags_at_eof_are_fine() {
        let out = ok("<div><p>foo<span>bar");
        assert!(out.contains("<p>foo<span>bar"));
    }

    #[test]
    fn misnested_inline_declines_with_position() {
        let o = format_html_report("<div>\n  <p><b>x</p>\n</div>", 2);
        let msg = o.message.expect("must decline");
        assert_eq!(o.text, "<div>\n  <p><b>x</p>\n</div>");
        assert!(msg.contains("line 2, column 10"), "{}", msg);
        assert!(msg.contains("`<b>`"), "{}", msg);
    }

    #[test]
    fn deeply_nested_does_not_overflow() {
        let s = format!("{}body{}", "<div>".repeat(200), "</div>".repeat(200));
        let out = ok(&s);
        assert!(out.lines().count() >= 399);
        let deep = format!("{}x", "<div>".repeat(20_000));
        let o = format_html_report(&deep, 2);
        assert!(o.message.unwrap().contains("nested more than"));
        assert_eq!(o.text, deep);
    }

    #[test]
    fn over_max_size_is_unchanged_with_message() {
        let input = "<div>".repeat(MAX_HTML_INPUT_BYTES / 5 + 1);
        let o = format_html_report(&input, 2);
        assert!(o.message.unwrap().contains("limit"));
        assert!(o.text == input);
    }

    #[test]
    fn crlf_documents_get_crlf_line_breaks() {
        let s = "<!DOCTYPE html>\r\n<html>\r\n<body>\r\n<p>X</p>\n</body>\r\n</html>";
        let out = ok(s);
        assert!(out.contains("<html>\r\n  <body>"), "{:?}", out);
    }

    #[test]
    fn cdata_and_svg_untouched() {
        let s = "<div><svg viewBox=\"0 0 1 1\"><![CDATA[ x ]]> <circle r=\"1\"/> <text> a </text></svg></div>";
        let out = ok(s);
        assert!(out.contains(
            "<svg viewBox=\"0 0 1 1\"><![CDATA[ x ]]> <circle r=\"1\"/> <text> a </text></svg>"
        ));
    }

    #[test]
    fn server_code_declines() {
        let o = format_html_report("<div>\n<?php echo 1; ?>\n</div>", 2);
        assert!(o.message.unwrap().contains("line 2, column 1"));
    }

    #[test]
    fn idempotent_already_formatted() {
        let original = "<!DOCTYPE html><html><body><p>X</p></body></html>";
        let once = ok(original);
        assert_eq!(once, ok(&once));
    }

    #[test]
    fn minify_removes_only_safe_gaps() {
        let o = minify_html_report("<ul>\n  <li>a</li>\n  <li><b>x</b> <i>y</i></li>\n</ul>\n");
        assert_eq!(o.text, "<ul><li>a</li><li><b>x</b> <i>y</i></li></ul>\n");
    }
}
