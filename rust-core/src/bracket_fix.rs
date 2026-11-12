// SPDX-License-Identifier: GPL-3.0-or-later

//! Bracket checker and fixer for (), [] and {}.
//!
//! v0.1.133 rewrite (pinned by `fuzz::bracket_props`). What the fixer is
//! allowed to do, exactly:
//!
//!   * INSERT a closing bracket for an opener that is never closed, or that
//!     is still open when an enclosing bracket closes;
//!   * DELETE a closing bracket that has no opener of its kind;
//!   * nothing else. No other character is added, removed or moved, so the
//!     text with all brackets stripped is identical before and after, and
//!     every change is listed with its line and column.
//!
//! Brackets inside strings and comments do not count. The text has no
//! language attached, so it is read four ways — C-style (`//`, `/* */`,
//! "..", 'c', `template ${..}`, /regex/), hash-style (`#` comments, '..',
//! "..", """..""" and heredocs), dash-style (SQL/Lua `--` comments) and as
//! plain text — and the reading that needs the FEWEST changes wins. If any
//! reading is already balanced the text is left exactly as it is: the fixer
//! can never make balanced input worse.
//!
//! Where a missing closer goes is a guess, so it is a careful one: an
//! opener with code after it on its line is closed at the end of that line
//! (`foo(a, b` -> `foo(a, b)`, `if (x {` -> `if (x) {`); an opener that ends
//! its line is closed where the indentation drops back (on the blank line
//! the deleted closer left behind, when there is one); otherwise right
//! before the bracket that forces it closed. The result is re-checked, and
//! if it is not balanced the fixer falls back to the plainest placement,
//! then declines.
//!
//! Every pass is linear (2M nested or stray brackets take milliseconds) and
//! nothing recurses.

const MAX_INPUT_BYTES: usize = 50 * 1024 * 1024; // 50 MB
const NONE: usize = usize::MAX;
const MAX_REPORT_LINES: usize = 200;

#[derive(Clone, Copy, PartialEq, Eq, Debug)]
pub enum Dialect {
    CLike,
    Hash,
    Dash,
    Raw,
}

const DIALECTS: [Dialect; 4] = [Dialect::CLike, Dialect::Hash, Dialect::Dash, Dialect::Raw];

impl Dialect {
    fn describe(self) -> &'static str {
        match self {
            Dialect::CLike => "C-style code: \"strings\", 'c' literals, `templates`, /regex/, // and /* */ comments skipped",
            Dialect::Hash => "script code: \"strings\", 'strings', \"\"\"triple\"\"\" strings, heredocs and # comments skipped",
            Dialect::Dash => "SQL/Lua-style code: 'strings', \"identifiers\", -- and /* */ comments skipped",
            Dialect::Raw => "plain text: every bracket counts",
        }
    }
}

fn kind(c: u8) -> usize {
    match c {
        b'(' | b')' => 0,
        b'[' | b']' => 1,
        _ => 2,
    }
}
fn is_open(c: u8) -> bool {
    matches!(c, b'(' | b'[' | b'{')
}
fn closer_of(c: u8) -> u8 {
    match c {
        b'(' => b')',
        b'[' => b']',
        _ => b'}',
    }
}
fn opener_of(c: u8) -> u8 {
    match c {
        b')' => b'(',
        b']' => b'[',
        _ => b'{',
    }
}

/// Template-hole delimiters (`${` and its `}`) in the token stream: they
/// bound brackets but are not brackets the fixer may touch.
const HOLE_OPEN: u8 = b'$';
const HOLE_CLOSE: u8 = b'%';

// ── lexer ─────────────────────────────────────────────────────────────────

struct Lexed {
    /// (offset, byte) of every bracket that is code, plus hole markers
    toks: Vec<(usize, u8)>,
    line_start: Vec<usize>,
    indent: Vec<u32>,
    /// first non-blank byte of the line, if it starts a code token
    first_tok: Vec<usize>,
    /// the line's first token is a comment
    first_is_comment: Vec<bool>,
    /// offset just past the line's last code token (comments excluded)
    code_end: Vec<usize>,
    /// spans skipped as strings/comments (for keyword counting)
    skips: Vec<(usize, usize)>,
}

struct Lx<'a> {
    b: &'a [u8],
    out: Lexed,
    line: usize,
    seen: Vec<bool>,
    prev_sig: u8,
    /// per quote byte: the line on which a same-line closing search failed
    no_close_line: [usize; 3],
    heredoc_fail: Vec<Vec<u8>>,
    heredoc_budget: usize,
    triple_fail: [bool; 2],
    /// a search for "*/" / "]]" already ran off the end: later ones would too
    block_fail: bool,
    long_fail: bool,
}

impl<'a> Lx<'a> {
    fn sync(&mut self, pos: usize) {
        let ls = &self.out.line_start;
        while self.line + 1 < ls.len() && ls[self.line + 1] <= pos {
            self.line += 1;
        }
    }
    /// A code token spanning [a, e) on the current line.
    fn code(&mut self, a: usize, e: usize) {
        self.sync(a);
        let l = self.line;
        if !self.seen[l] {
            self.seen[l] = true;
            self.out.first_tok[l] = a;
        }
        self.sync(e.saturating_sub(1).max(a));
        self.out.code_end[self.line] = e;
        let l2 = self.line;
        // a multi-line token: the lines it covers do not start with code
        for s in &mut self.seen[l + 1..=l2] {
            *s = true;
        }
    }
    /// A skipped span (string or comment) [a, e).
    fn skip(&mut self, a: usize, e: usize, comment: bool) {
        self.out.skips.push((a, e));
        self.sync(a);
        let l = self.line;
        if !self.seen[l] {
            self.seen[l] = true;
            if comment {
                self.out.first_is_comment[l] = true;
            } else {
                self.out.first_tok[l] = a;
            }
        }
        if !comment {
            // a string is a code token for placement purposes
            self.sync(e.saturating_sub(1).max(a));
            self.out.code_end[self.line] = e;
        } else {
            self.sync(e.saturating_sub(1).max(a));
        }
        let l2 = self.line;
        for s in &mut self.seen[l + 1..=l2] {
            *s = true;
        }
    }

    /// Same-line quoted string starting at i; None if it does not close on
    /// this line (then the quote is just a character, e.g. an apostrophe).
    fn same_line_string(&mut self, i: usize, q: u8, escapes: bool) -> Option<usize> {
        let qi = match q {
            b'"' => 0,
            b'\'' => 1,
            _ => 2,
        };
        self.sync(i);
        if self.no_close_line[qi] == self.line {
            return None;
        }
        let b = self.b;
        let mut j = i + 1;
        while j < b.len() && b[j] != b'\n' {
            if escapes && b[j] == b'\\' {
                j += 2;
                continue;
            }
            if b[j] == q {
                return Some(j + 1);
            }
            j += 1;
        }
        // No closer from here to the end of the line; a later quote on this
        // line would scan the same suffix with the same escape parity.
        self.no_close_line[qi] = self.line;
        None
    }

    fn find(&self, from: usize, pat: &[u8]) -> Option<usize> {
        let b = self.b;
        if from > b.len() {
            return None;
        }
        // memchr-style first byte search keeps this fast
        let mut i = from;
        while i + pat.len() <= b.len() {
            i += b[i..].iter().position(|&c| c == pat[0])?;
            if i + pat.len() <= b.len() && &b[i..i + pat.len()] == pat {
                return Some(i);
            }
            i += 1;
        }
        None
    }

    fn eol(&self, i: usize) -> usize {
        self.b[i..]
            .iter()
            .position(|&c| c == b'\n')
            .map(|p| i + p)
            .unwrap_or(self.b.len())
    }

    /// `/regex/flags` on one line starting at i, if this `/` can start one.
    fn regex(&self, i: usize) -> Option<usize> {
        if !b"(,=:[!&|?{};+-*%<>~^\n".contains(&self.prev_sig) {
            return None;
        }
        let b = self.b;
        if matches!(b.get(i + 1), Some(b'/' | b'*' | b' ' | b'\n') | None) {
            return None;
        }
        let mut j = i + 1;
        let mut class = false;
        // regex literals are short; the bound keeps a line full of '/' linear
        let limit = (i + 1024).min(b.len());
        while j < limit && b[j] != b'\n' {
            match b[j] {
                b'\\' => j += 1,
                b'[' => class = true,
                b']' => class = false,
                b'/' if !class => {
                    j += 1;
                    while j < b.len() && b[j].is_ascii_alphabetic() {
                        j += 1;
                    }
                    return Some(j);
                }
                _ => {}
            }
            j += 1;
        }
        None
    }

    /// Heredoc `<<WORD`, `<<-WORD`, `<<~WORD`, `<<'WORD'`: returns the end
    /// of the word (with its quote) and the end of the terminator line.
    fn heredoc(&mut self, i: usize) -> Option<(usize, usize)> {
        let b = self.b;
        let mut j = i + 2;
        let indented = matches!(b.get(j), Some(b'-' | b'~'));
        if indented {
            j += 1;
        }
        let quote = match b.get(j) {
            Some(&q @ (b'\'' | b'"')) => {
                j += 1;
                Some(q)
            }
            _ => None,
        };
        let ws = j;
        while j < b.len()
            && (b[j].is_ascii_uppercase() || b[j] == b'_' || (j > ws && b[j].is_ascii_digit()))
        {
            j += 1;
        }
        if j == ws {
            return None;
        }
        let word = &b[ws..j];
        if let Some(q) = quote {
            if b.get(j) != Some(&q) {
                return None;
            }
        }
        let word_end = j + quote.is_some() as usize;
        if self.heredoc_budget == 0 || self.heredoc_fail.iter().any(|w| w == word) {
            return None;
        }
        let mut line_start = self.eol(i) + 1;
        while line_start < b.len() {
            let e = self.eol(line_start);
            let mut l = &b[line_start..e];
            if l.last() == Some(&b'\r') {
                l = &l[..l.len() - 1];
            }
            if indented {
                while let Some((&c, rest)) = l.split_first() {
                    if c == b' ' || c == b'\t' {
                        l = rest;
                    } else {
                        break;
                    }
                }
            }
            if l == word {
                return Some((word_end, e));
            }
            line_start = e + 1;
        }
        self.heredoc_budget -= 1;
        self.heredoc_fail.push(word.to_vec());
        None
    }
}

fn split_lines(s: &str) -> (Vec<usize>, Vec<u32>) {
    let b = s.as_bytes();
    let mut starts = vec![0usize];
    for (i, &c) in b.iter().enumerate() {
        if c == b'\n' {
            starts.push(i + 1);
        }
    }
    let indent = starts
        .iter()
        .map(|&st| {
            let mut w = 0u32;
            for &c in &b[st..] {
                match c {
                    b' ' => w += 1,
                    b'\t' => w += 4,
                    _ => break,
                }
            }
            w
        })
        .collect();
    (starts, indent)
}

fn lex(s: &str, d: Dialect) -> Lexed {
    let b = s.as_bytes();
    let (line_start, indent) = split_lines(s);
    let n = line_start.len();
    let mut x = Lx {
        b,
        out: Lexed {
            toks: Vec::new(),
            line_start,
            indent,
            first_tok: vec![NONE; n],
            first_is_comment: vec![false; n],
            code_end: vec![NONE; n],
            skips: Vec::new(),
        },
        line: 0,
        seen: vec![false; n],
        prev_sig: b'\n',
        no_close_line: [NONE; 3],
        heredoc_fail: Vec::new(),
        heredoc_budget: 64,
        triple_fail: [false; 2],
        block_fail: false,
        long_fail: false,
    };
    // template holes: brace depth at which each open `${` resumes the
    // template when its `}` arrives
    let mut holes: Vec<usize> = Vec::new();
    let mut braces = 0usize;
    let mut i = 0usize;
    while i < b.len() {
        let c = b[i];
        if c == b'\n' {
            x.prev_sig = b'\n';
            i += 1;
            continue;
        }
        if c == b' ' || c == b'\t' || c == b'\r' {
            i += 1;
            continue;
        }
        let next = b.get(i + 1).copied();
        // comments
        let line_comment = match d {
            Dialect::CLike => c == b'/' && next == Some(b'/'),
            Dialect::Hash => {
                c == b'#' && (i == 0 || matches!(b[i - 1], b' ' | b'\t' | b'\n' | b'\r'))
            }
            Dialect::Dash => c == b'-' && next == Some(b'-'),
            Dialect::Raw => false,
        };
        if line_comment {
            let mut e = x.eol(i);
            if d == Dialect::Dash && b[i..].starts_with(b"--[[") && !x.long_fail {
                match x.find(i + 4, b"]]") {
                    Some(p) => e = p + 2,
                    None => x.long_fail = true,
                }
            }
            x.skip(i, e, true);
            i = e;
            continue;
        }
        if c == b'/'
            && next == Some(b'*')
            && matches!(d, Dialect::CLike | Dialect::Dash)
            && !x.block_fail
        {
            match x.find(i + 2, b"*/") {
                Some(p) => {
                    x.skip(i, p + 2, true);
                    i = p + 2;
                    continue;
                }
                None => x.block_fail = true,
            }
        }
        // strings
        if d == Dialect::Hash && (c == b'"' || c == b'\'') && b[i..].starts_with(&[c, c, c]) {
            let ti = (c == b'\'') as usize;
            if !x.triple_fail[ti] {
                if let Some(p) = x.find(i + 3, &[c, c, c]) {
                    x.skip(i, p + 3, false);
                    x.prev_sig = b'"';
                    i = p + 3;
                    continue;
                }
                x.triple_fail[ti] = true;
            }
        }
        if (c == b'"' || c == b'\'') && d != Dialect::Raw {
            if let Some(e) = x.same_line_string(i, c, d != Dialect::Dash) {
                x.skip(i, e, false);
                x.prev_sig = b'"';
                i = e;
                continue;
            }
        }
        if d == Dialect::Hash && c == b'<' && next == Some(b'<') {
            if let Some((we, e)) = x.heredoc(i) {
                x.code(i, i + 2);
                x.skip(i + 2, we, false);
                // the body is data, not a place to put brackets
                x.skip(we, e, true);
                i = e;
                continue;
            }
        }
        if d == Dialect::CLike {
            if c == b'/' {
                if let Some(e) = x.regex(i) {
                    x.skip(i, e, false);
                    x.prev_sig = b'"';
                    i = e;
                    continue;
                }
            }
            let resume_template = c == b'}' && holes.last() == Some(&braces);
            if c == b'`' || resume_template {
                if resume_template {
                    holes.pop();
                    x.out.toks.push((i, HOLE_CLOSE));
                    x.code(i, i + 1);
                    i += 1;
                }
                let start = if resume_template { i } else { i + 1 };
                // scan the template body up to its end or the next hole
                let mut j = start;
                let mut hole = false;
                let mut closed = false;
                while j < b.len() {
                    match b[j] {
                        b'\\' => j += 2,
                        b'`' => {
                            closed = true;
                            j += 1;
                            break;
                        }
                        b'$' if b.get(j + 1) == Some(&b'{') => {
                            hole = true;
                            break;
                        }
                        _ => j += 1,
                    }
                }
                let j = j.min(b.len());
                if !closed && !hole && !resume_template {
                    // unterminated: the backtick is just a character
                    x.code(i, i + 1);
                    x.prev_sig = c;
                    i += 1;
                    continue;
                }
                x.skip(i, j, false);
                if hole {
                    x.out.toks.push((j, HOLE_OPEN));
                    holes.push(braces);
                    x.code(j, j + 2);
                    x.prev_sig = b'{';
                    i = j + 2;
                } else {
                    x.prev_sig = b'"';
                    i = j;
                }
                continue;
            }
        }
        // code
        match c {
            b'(' | b')' | b'[' | b']' | b'{' | b'}' => {
                if c == b'{' {
                    braces += 1;
                } else if c == b'}' {
                    braces = braces.saturating_sub(1);
                }
                x.out.toks.push((i, c));
                x.code(i, i + 1);
                x.prev_sig = c;
                i += 1;
            }
            _ => {
                let w = if c < 0x80 {
                    1
                } else {
                    s[i..].chars().next().map(|ch| ch.len_utf8()).unwrap_or(1)
                };
                x.code(i, i + w);
                x.prev_sig = if c.is_ascii_alphanumeric() || c == b'_' || c >= 0x80 {
                    b'a'
                } else {
                    c
                };
                i += w;
            }
        }
    }
    x.out
}

// ── planner ───────────────────────────────────────────────────────────────

#[derive(Clone, Copy, Debug)]
enum EditKind {
    /// insert `ch` before byte `pos`, closing the opener at `opener`
    Insert { ch: u8, opener: usize },
    /// delete the stray closer at `pos`
    Delete,
}

#[derive(Clone, Copy, Debug)]
struct Edit {
    pos: usize,
    kind: EditKind,
    /// why: the closer whose arrival forced this (NONE = end of text)
    forced_by: usize,
}

struct Open {
    off: usize,
    ch: u8,
    /// direct children as (start, end_ok): p is inside iff start < p < end_ok
    children: Vec<(usize, usize)>,
}

#[derive(Clone, Copy, PartialEq, Eq)]
enum Mode {
    /// decisions only (insert positions are not computed)
    Count,
    /// every insertion right before the bracket/end that forces it
    Simple,
    /// line- and indentation-aware placement
    Smart,
}

struct Planner<'a> {
    b: &'a [u8],
    lx: &'a Lexed,
    next_le: Vec<usize>,
    prev_code_line: Vec<usize>,
    mode: Mode,
}

fn is_blank_line(lx: &Lexed, l: usize) -> bool {
    lx.first_tok[l] == NONE && !lx.first_is_comment[l] && lx.code_end[l] == NONE
}

impl<'a> Planner<'a> {
    fn new(b: &'a [u8], lx: &'a Lexed, mode: Mode) -> Self {
        if mode == Mode::Count {
            return Planner {
                b,
                lx,
                next_le: Vec::new(),
                prev_code_line: Vec::new(),
                mode,
            };
        }
        let n = lx.line_start.len();
        let eligible = |l: usize| lx.first_tok[l] != NONE && !lx.first_is_comment[l];
        // next_le[l]: first eligible line after l whose indent <= indent[l]
        let mut next_le = vec![NONE; n];
        let mut st: Vec<usize> = Vec::new();
        for l in (0..n).rev() {
            let q = lx.indent[l];
            // topmost (= nearest) entry with indent <= q
            // st (bottom..top) holds ever nearer lines with strictly
            // increasing indents, so the nearest line with indent <= q is
            // the last one of the qualifying prefix
            let idx = st.partition_point(|&m| lx.indent[m] <= q);
            if idx > 0 {
                next_le[l] = st[idx - 1];
            }
            if eligible(l) {
                while let Some(&t) = st.last() {
                    if lx.indent[t] >= q {
                        st.pop();
                    } else {
                        break;
                    }
                }
                st.push(l);
            }
        }
        let mut prev_code_line = vec![NONE; n];
        let mut last = NONE;
        for (l, slot) in prev_code_line.iter_mut().enumerate() {
            if lx.code_end[l] != NONE {
                last = l;
            }
            *slot = last;
        }
        Planner {
            b,
            lx,
            next_le,
            prev_code_line,
            mode,
        }
    }

    fn line_of(&self, off: usize) -> usize {
        self.lx.line_start.partition_point(|&s| s <= off) - 1
    }

    fn inside_child(u: &Open, p: usize) -> Option<(usize, usize)> {
        let idx = u.children.partition_point(|c| c.0 < p);
        if idx > 0 {
            let c = u.children[idx - 1];
            if p < c.1 {
                return Some(c);
            }
        }
        None
    }

    /// Where to insert the closer for `u`, which must close at or before f.
    fn place(&self, u: &Open, f: usize) -> usize {
        if self.mode == Mode::Count {
            return f;
        }
        let b = self.b;
        let lx = self.lx;
        let lo = u.off + 1;
        let lu = self.line_of(u.off);
        let ce = lx.code_end[lu];
        let fix_child = |p: usize| match Self::inside_child(u, p) {
            Some(c) => c.1,
            None => p,
        };
        if self.mode == Mode::Smart && ce != NONE {
            let last_on_line = ce <= u.off + 1;
            let cont = !last_on_line && b",\\+-*/=&|.?".contains(&b[ce - 1]);
            if !last_on_line && !cont {
                // inline: close at the end of the opener's line
                let mut p = ce.min(f);
                if let Some(c) = Self::inside_child(u, p) {
                    p = c.0;
                }
                while p > lo && matches!(b[p - 1], b' ' | b'\t' | b';' | b':') {
                    p -= 1;
                }
                let p = fix_child(p);
                if p >= lo && p <= f {
                    return p;
                }
            } else if let Some(p) = self.block_place(u, f, lu) {
                let p = fix_child(p);
                if p >= lo && p <= f {
                    return p;
                }
            }
        }
        // fallback: right before f, pulled back over trailing blanks to the
        // end of the last code line
        let mut p = f;
        while p > lo && matches!(b[p - 1], b' ' | b'\t') {
            p -= 1;
        }
        if p > lo && matches!(b[p - 1], b'\n' | b'\r') {
            let l = self.line_of(p - 1);
            let pl = self.prev_code_line[l];
            if pl != NONE && pl >= lu {
                let q = lx.code_end[pl];
                if q != NONE && q >= lo && q <= f {
                    p = q;
                }
            }
        }
        fix_child(p.max(lo)).min(f).max(lo)
    }

    fn block_place(&self, u: &Open, f: usize, lu: usize) -> Option<usize> {
        let lx = self.lx;
        let b = self.b;
        let n = lx.line_start.len();
        // the line where the indentation drops back to the opener's level
        let (dline, dpos) = match self.next_le[lu] {
            NONE => {
                if f != b.len() {
                    return None;
                }
                // virtual line at end of text
                if *lx.line_start.last()? == b.len() && n - 1 > lu {
                    (n - 1, b.len())
                } else {
                    (n, b.len())
                }
            }
            d => (d, lx.first_tok[d]),
        };
        if dpos > f {
            return None;
        }
        if dpos == f {
            return Some(f);
        }
        let starts_kw = |pos: usize| {
            let w = &b[pos..];
            [
                &b"else"[..],
                b"catch",
                b"finally",
                b"elif",
                b"except",
                b"while",
            ]
            .iter()
            .any(|kw| {
                w.starts_with(kw) && !w.get(kw.len()).is_some_and(|c| c.is_ascii_alphanumeric())
            })
        };
        if u.ch == b'{' {
            // `} else {` with the `}` gone leaves ` else {`: one column deeper
            // than the opener's line, so it is not the dedent line itself
            let lim = dline.min(lu + 200).min(n);
            for l in lu + 1..lim {
                let ft = lx.first_tok[l];
                if ft != NONE
                    && ft <= f
                    && !lx.first_is_comment[l]
                    && lx.indent[l] <= lx.indent[lu] + 1
                    && starts_kw(ft)
                {
                    // put it where the `}` was: before the leftover space
                    let extra = lx.indent[l] == lx.indent[lu] + 1 && b[ft - 1] == b' ';
                    return Some(if extra { ft - 1 } else { ft });
                }
            }
            if dline < n && starts_kw(dpos) {
                return Some(dpos);
            }
        }
        // the blank line a deleted closer leaves behind
        let bl = dline - 1;
        if bl > lu && bl < n && is_blank_line(lx, bl) && lx.line_start[bl] < b.len() {
            let mut e = if dline < n {
                lx.line_start[dline] - 1
            } else {
                b.len()
            };
            if e > lx.line_start[bl] && b[e - 1] == b'\r' {
                e -= 1;
            }
            // after the blank line's own indentation
            return Some(e);
        }
        let pl = self.prev_code_line[bl.min(n - 1)];
        if pl != NONE && pl > lu {
            return Some(lx.code_end[pl]);
        }
        None
    }

    /// Plan the edits. One stack pass; a closer that does not match the
    /// innermost opener is either a stray (deleted) or proof that the
    /// openers above its partner were never closed (closers inserted). It
    /// is a stray when the very next bracket closes the innermost opener
    /// (`[ ( ] ) ]`: the first `]` is extra), or when nothing it could
    /// close is open.
    fn plan(&self) -> Vec<Edit> {
        let mut edits: Vec<Edit> = Vec::new();
        let mut stack: Vec<Open> = Vec::new();
        let mut counts: Vec<[usize; 3]> = vec![[0; 3]];
        let close_top = |stack: &mut Vec<Open>,
                         counts: &mut Vec<[usize; 3]>,
                         edits: &mut Vec<Edit>,
                         f: usize,
                         forced_by: usize| {
            let u = stack.pop().expect("non-empty");
            let p = self.place(&u, f);
            edits.push(Edit {
                pos: p,
                kind: EditKind::Insert {
                    ch: closer_of(u.ch),
                    opener: u.off,
                },
                forced_by,
            });
            if let Some(c) = counts.last_mut() {
                c[kind(u.ch)] -= 1;
            }
            if self.mode != Mode::Count {
                if let Some(parent) = stack.last_mut() {
                    parent.children.push((u.off, p));
                }
            }
        };
        let toks = &self.lx.toks;
        for (ti, &(q, c)) in toks.iter().enumerate() {
            match c {
                HOLE_OPEN => {
                    stack.push(Open {
                        off: q,
                        ch: HOLE_OPEN,
                        children: Vec::new(),
                    });
                    counts.push([0; 3]);
                }
                HOLE_CLOSE => {
                    while stack.last().is_some_and(|t| t.ch != HOLE_OPEN) {
                        close_top(&mut stack, &mut counts, &mut edits, q, q);
                    }
                    if let Some(h) = stack.pop() {
                        counts.pop();
                        if let Some(parent) = stack.last_mut() {
                            parent.children.push((h.off, q + 1));
                        }
                    }
                }
                c if is_open(c) => {
                    if let Some(k) = counts.last_mut() {
                        k[kind(c)] += 1;
                    }
                    stack.push(Open {
                        off: q,
                        ch: c,
                        children: Vec::new(),
                    });
                }
                c => {
                    let open = opener_of(c);
                    let have = counts.last().map(|k| k[kind(c)]).unwrap_or(0);
                    let top = stack.last().map(|t| t.ch).unwrap_or(0);
                    let next_closes_top =
                        top != open && next_level_closer(toks, ti + 1) == Some(closer_of(top));
                    if have == 0 || next_closes_top {
                        edits.push(Edit {
                            pos: q,
                            kind: EditKind::Delete,
                            forced_by: q,
                        });
                        continue;
                    }
                    while stack.last().is_some_and(|t| t.ch != open) {
                        close_top(&mut stack, &mut counts, &mut edits, q, q);
                    }
                    let u = stack.pop().expect("count says an opener is open");
                    if let Some(k) = counts.last_mut() {
                        k[kind(c)] -= 1;
                    }
                    if self.mode != Mode::Count {
                        if let Some(parent) = stack.last_mut() {
                            parent.children.push((u.off, q + 1));
                        }
                    }
                }
            }
        }
        let end = self.b.len();
        while let Some(t) = stack.last() {
            if t.ch == HOLE_OPEN {
                stack.pop();
                counts.pop();
                continue;
            }
            close_top(&mut stack, &mut counts, &mut edits, end, NONE);
        }
        edits
    }
}

/// The first closer after token `from` that is not inside a group opened
/// after `from` (looking at most 256 tokens ahead).
fn next_level_closer(toks: &[(usize, u8)], from: usize) -> Option<u8> {
    let mut depth = 0usize;
    for &(_, c) in toks.iter().skip(from).take(256) {
        match c {
            HOLE_OPEN | HOLE_CLOSE => return None,
            c if is_open(c) => depth += 1,
            c if depth > 0 => {
                let _ = c;
                depth -= 1;
            }
            c => return Some(c),
        }
    }
    None
}

/// Number of changes a reading needs (same decisions as the planner,
/// without placement).
fn edit_count(s: &str, lx: &Lexed) -> usize {
    Planner::new(s.as_bytes(), lx, Mode::Count).plan().len()
}

fn apply(s: &str, edits: &[Edit]) -> String {
    let mut ord: Vec<&Edit> = edits.iter().collect();
    // insertions before a deletion at the same offset; otherwise keep the
    // generation order (inner closers before outer ones)
    ord.sort_by_key(|e| (e.pos, matches!(e.kind, EditKind::Delete)));
    let mut out = String::with_capacity(s.len() + edits.len());
    let mut cur = 0usize;
    for e in ord {
        out.push_str(&s[cur..e.pos.max(cur)]);
        cur = cur.max(e.pos);
        match e.kind {
            EditKind::Insert { ch, .. } => out.push(ch as char),
            EditKind::Delete => cur = e.pos + 1,
        }
    }
    out.push_str(&s[cur..]);
    out
}

// ── public API ────────────────────────────────────────────────────────────

pub struct BracketFix {
    pub text: String,
    pub report: String,
}

struct Reading {
    d: Dialect,
    lx: Lexed,
    n: usize,
    /// strings/comments/heredocs that contain brackets: how much of the
    /// text this reading explains away
    evidence: usize,
}

fn readings(s: &str) -> Vec<Reading> {
    let b = s.as_bytes();
    DIALECTS
        .iter()
        .map(|&d| {
            let lx = lex(s, d);
            let n = edit_count(s, &lx);
            let evidence = lx
                .skips
                .iter()
                .filter(|(a, e)| b[*a..*e].iter().any(|c| b"()[]{}".contains(c)))
                .count();
            Reading { d, lx, n, evidence }
        })
        .collect()
}

/// The code readings (never plain text: it would happily "balance" code by
/// counting brackets inside strings) that need the fewest changes, most
/// evidence first. Empty when some reading — plain text included — is
/// already balanced.
fn candidates(rs: &[Reading]) -> Vec<&Reading> {
    if rs.iter().any(|r| r.n == 0) {
        return Vec::new();
    }
    let code: Vec<&Reading> = rs.iter().filter(|r| r.d != Dialect::Raw).collect();
    let min = code.iter().map(|r| r.n).min().unwrap_or(0);
    let mut tied: Vec<&Reading> = code.into_iter().filter(|r| r.n == min).collect();
    tied.sort_by_key(|r| std::cmp::Reverse(r.evidence));
    tied
}

fn pos_text(s: &str, offs: &[usize]) -> Vec<(usize, usize)> {
    crate::json_fmt::line_cols(s, offs)
}

pub fn fix_brackets_full(input: &str) -> BracketFix {
    if input.len() > MAX_INPUT_BYTES {
        return BracketFix {
            text: input.to_string(),
            report: format!(
                "Input too large for the bracket fixer ({} bytes, max {} bytes). No changes made.",
                input.len(),
                MAX_INPUT_BYTES
            ),
        };
    }
    let rs = readings(input);
    let cands = candidates(&rs);
    if cands.is_empty() {
        return BracketFix {
            text: input.to_string(),
            report: "All brackets matched. No changes needed.".into(),
        };
    }
    // Prefer a result that is balanced under EVERY equally good reading
    // (then it does not matter which language this really is); otherwise
    // the best-evidenced reading's own balanced result.
    let mut fallback: Option<(Dialect, String, Vec<Edit>)> = None;
    for r in &cands {
        for mode in [Mode::Smart, Mode::Simple] {
            let edits = Planner::new(input.as_bytes(), &r.lx, mode).plan();
            let out = apply(input, &edits);
            if edit_count(&out, &lex(&out, r.d)) != 0 {
                continue;
            }
            if cands
                .iter()
                .all(|o| o.d == r.d || edit_count(&out, &lex(&out, o.d)) == 0)
            {
                let report = describe_edits(input, r.d, &edits);
                return BracketFix { text: out, report };
            }
            if fallback.is_none() {
                fallback = Some((r.d, out, edits));
            }
        }
    }
    if let Some((d, out, edits)) = fallback {
        let report = describe_edits(input, d, &edits);
        return BracketFix { text: out, report };
    }
    BracketFix {
        text: input.to_string(),
        report: format!(
            "No changes made: could not place the missing brackets safely (read as {}).\n\n{}",
            cands[0].d.describe(),
            check_brackets(input)
        ),
    }
}

fn describe_edits(s: &str, d: Dialect, edits: &[Edit]) -> String {
    let mut ord: Vec<&Edit> = edits.iter().collect();
    ord.sort_by_key(|e| e.pos);
    let shown = &ord[..ord.len().min(MAX_REPORT_LINES)];
    let mut offs: Vec<usize> = Vec::new();
    for e in shown {
        offs.push(e.pos);
        if let EditKind::Insert { opener, .. } = e.kind {
            offs.push(opener);
        }
    }
    offs.sort_unstable();
    offs.dedup();
    let pos = pos_text(s, &offs);
    let at = |o: usize| pos[offs.binary_search(&o).unwrap_or(0)];
    let mut r = format!(
        "Fixed {} bracket problem(s), reading the text as {}:\n",
        edits.len(),
        d.describe()
    );
    for e in shown {
        let (l, c) = at(e.pos);
        match e.kind {
            EditKind::Insert { ch, opener } => {
                let (ol, oc) = at(opener);
                r.push_str(&format!(
                    "\nline {l}, column {c}: inserted '{}' to close the '{}' at line {ol}, column {oc}",
                    ch as char,
                    opener_of(ch) as char
                ));
            }
            EditKind::Delete => {
                let ch = s.as_bytes()[e.pos];
                r.push_str(&format!(
                    "\nline {l}, column {c}: removed '{}' (no '{}' is open there)",
                    ch as char,
                    opener_of(ch) as char
                ));
            }
        }
    }
    if edits.len() > shown.len() {
        r.push_str(&format!("\n... and {} more", edits.len() - shown.len()));
    }
    r.push_str(
        "\n\nWhere a missing bracket belongs is a guess: check the inserted ones (Show Diff).",
    );
    r
}

pub fn fix_brackets(input: &str) -> String {
    fix_brackets_full(input).text
}

/// Report of what `fix_brackets` changes (or why it changes nothing).
pub fn fix_brackets_report(input: &str) -> String {
    fix_brackets_full(input).report
}

pub fn check_brackets(input: &str) -> String {
    if input.len() > MAX_INPUT_BYTES {
        return format!(
            "Input too large for bracket checker ({} bytes, max {} bytes).\n",
            input.len(),
            MAX_INPUT_BYTES
        );
    }
    let rs = readings(input);
    let cands = candidates(&rs);
    let balanced = rs.iter().find(|r| r.n == 0);
    let r = cands.first().copied().or(balanced).unwrap_or(&rs[0]);
    let (d, lx, n) = (r.d, &r.lx, if cands.is_empty() { 0 } else { r.n });
    let mut lines: Vec<(usize, String)> = Vec::new();
    if n > 0 {
        let edits = Planner::new(input.as_bytes(), lx, Mode::Simple).plan();
        let mut offs: Vec<usize> = Vec::new();
        for e in edits.iter().take(MAX_REPORT_LINES) {
            offs.push(e.pos);
            if let EditKind::Insert { opener, .. } = e.kind {
                offs.push(opener);
            }
            if e.forced_by != NONE {
                offs.push(e.forced_by);
            }
        }
        offs.sort_unstable();
        offs.dedup();
        let pos = pos_text(input, &offs);
        let at = |o: usize| pos[offs.binary_search(&o).unwrap_or(0)];
        for e in edits.iter().take(MAX_REPORT_LINES) {
            match e.kind {
                EditKind::Delete => {
                    let ch = input.as_bytes()[e.pos];
                    let (l, c) = at(e.pos);
                    lines.push((
                        e.pos,
                        format!(
                            "line {l}, column {c}: '{}' has no matching '{}'",
                            ch as char,
                            opener_of(ch) as char
                        ),
                    ));
                }
                EditKind::Insert { ch, opener } => {
                    let (l, c) = at(opener);
                    let o = opener_of(ch) as char;
                    let msg = if e.forced_by == NONE {
                        format!("line {l}, column {c}: '{o}' is never closed")
                    } else {
                        let (fl, fc) = at(e.forced_by);
                        let fch = match input.as_bytes()[e.forced_by] {
                            // a template hole's closing brace
                            b'}' => '}',
                            c => c as char,
                        };
                        format!(
                            "line {l}, column {c}: '{o}' is not closed before the '{fch}' at line {fl}, column {fc} (expected '{}')",
                            ch as char
                        )
                    };
                    lines.push((opener, msg));
                }
            }
        }
        lines.sort_by_key(|x| x.0);
    }
    let mut report = String::new();
    let mut issues = n;
    for (_, l) in &lines {
        report.push_str(l);
        report.push('\n');
    }
    if n > lines.len() {
        report.push_str(&format!("... and {} more\n", n - lines.len()));
    }
    let kw = keyword_notes(input, lx);
    issues += kw.len();
    for k in &kw {
        report.push_str(k);
        report.push('\n');
    }
    if issues == 0 {
        "All brackets matched. No issues found.\n".to_string()
    } else {
        format!(
            "Found {issues} issue(s) (read as {}):\n\n{report}",
            d.describe()
        )
    }
}

/// Shell keyword pairs, only when the text is evidently shell (it contains
/// the closing keyword at all): `if` in C or Python is not a missing `fi`.
fn keyword_notes(input: &str, lx: &Lexed) -> Vec<String> {
    let mut v = Vec::new();
    for (opener, closer) in [("if", "fi"), ("case", "esac"), ("do", "done")] {
        let close_n = count_word(input, closer, &lx.skips);
        if close_n == 0 {
            continue;
        }
        let open_n = count_word(input, opener, &lx.skips);
        if open_n != close_n {
            v.push(format!(
                "keyword mismatch: {open_n} '{opener}' but {close_n} '{closer}' (shell)"
            ));
        }
    }
    v
}

// Counts occurrences of `word` outside skipped spans that aren't preceded or
// followed by a word character. Avoids "redo" matching "do".
fn count_word(haystack: &str, needle: &str, skips: &[(usize, usize)]) -> usize {
    let bytes = haystack.as_bytes();
    let nb = needle.as_bytes();
    let mut count = 0usize;
    let mut si = 0usize;
    let mut i = 0;
    while i + nb.len() <= bytes.len() {
        while si < skips.len() && skips[si].1 <= i {
            si += 1;
        }
        if si < skips.len() && skips[si].0 <= i {
            i = skips[si].1;
            continue;
        }
        if &bytes[i..i + nb.len()] == nb {
            let before_ok = i == 0 || !is_word_byte(bytes[i - 1]);
            let after_ok = i + nb.len() == bytes.len() || !is_word_byte(bytes[i + nb.len()]);
            if before_ok && after_ok {
                count += 1;
                i += nb.len();
                continue;
            }
        }
        i += 1;
    }
    count
}

fn is_word_byte(b: u8) -> bool {
    b.is_ascii_alphanumeric() || b == b'_'
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn empty_input_is_unchanged() {
        assert_eq!(fix_brackets(""), "");
        assert!(check_brackets("").contains("No issues"));
    }

    #[test]
    fn already_balanced_is_unchanged() {
        let s = "fn x() { let y = [1, 2, 3]; }";
        assert_eq!(fix_brackets(s), s);
    }

    #[test]
    fn missing_close_paren_added() {
        assert_eq!(fix_brackets("fn(a, b"), "fn(a, b)");
        assert_eq!(fix_brackets("[1, 2"), "[1, 2]");
        assert_eq!(fix_brackets("{ x: 1"), "{ x: 1}");
    }

    #[test]
    fn stray_closer_is_removed_not_balanced_by_a_prepended_opener() {
        // The old fixer prepended '(' at the very start of the document.
        assert_eq!(fix_brackets(")"), "");
        assert_eq!(fix_brackets("a + b)"), "a + b");
    }

    #[test]
    fn interleaved_kinds_are_fixed_properly() {
        // Old: counted each kind separately and produced "[( ])".
        assert_eq!(fix_brackets("( ]"), "( )");
        assert_eq!(fix_brackets("y = [bar(]"), "y = [bar()]");
    }

    #[test]
    fn placement_heuristics() {
        let cases: &[(&str, &str)] = &[
            ("if (x {\n  y();\n}", "if (x) {\n  y();\n}"),
            ("foo(a, b;\nbar();", "foo(a, b);\nbar();"),
            (
                "fn a() {\n    x();\n\nfn b() {}\n",
                "fn a() {\n    x();\n}\nfn b() {}\n",
            ),
            (
                "if (a) {\n  x();\n else {\n  y();\n}",
                "if (a) {\n  x();\n} else {\n  y();\n}",
            ),
            ("foo({\n  a: 1\n);", "foo({\n  a: 1\n});"),
            (
                "call(a, // note (\n  b);\nx(1",
                "call(a, // note (\n  b);\nx(1)",
            ),
            ("def f(a:\n    return a", "def f(a):\n    return a"),
            ("x = {\n  'a': 1,\n", "x = {\n  'a': 1,\n}"),
        ];
        for (input, want) in cases {
            assert_eq!(
                &fix_brackets(input),
                want,
                "input {input:?}\n{}",
                fix_brackets_report(input)
            );
        }
    }

    #[test]
    fn strings_comments_templates_regex_heredocs_are_skipped() {
        for s in [
            "f(\"(\")",
            "f('(')",
            "f(x) // ) unbalanced in a comment (",
            "f(x) /* ( \n [ */",
            "const s = `a ( ${ f([1]) } ]`;",
            "re = /[(]+\\//g; f(re)",
            "x = 1  # see (note\ny = f(x)",
            "s = \"\"\"doc (\n [ \"\"\"\nf(s)",
            "cat <<EOF\nraw ( text\nEOF\nf()",
            "SELECT f(x) -- note (\nFROM t",
            "let s = \"don't\"; f('a')",
            "print(\"it's (\") ",
            "char c = ')'; f(c);",
        ] {
            assert_eq!(fix_brackets(s), s, "touched balanced {s:?}");
            assert!(
                check_brackets(s).contains("No issues"),
                "{s:?}: {}",
                check_brackets(s)
            );
        }
    }

    #[test]
    fn apostrophes_in_prose_do_not_open_strings() {
        // Old: a ' opened a "string" that ran to the next ' anywhere below.
        let s = "// don't (\nf(x";
        assert_eq!(fix_brackets(s), "// don't (\nf(x)");
    }

    #[test]
    fn report_says_what_and_where() {
        let r = fix_brackets_report("foo(a, b\nbar]");
        assert!(
            r.contains("line 1, column 9: inserted ')' to close the '(' at line 1, column 4"),
            "{r}"
        );
        assert!(
            r.contains("line 2, column 4: removed ']' (no '[' is open there)"),
            "{r}"
        );
        let c = check_brackets("foo(a, b\nbar]");
        assert!(c.contains("line 1, column 4: '(' is never closed"), "{c}");
        assert!(
            c.contains("line 2, column 4: ']' has no matching '['"),
            "{c}"
        );
        let m = check_brackets("[1, (2]");
        assert!(m.contains("line 1, column 5: '(' is not closed before the ']' at line 1, column 7 (expected ')')"), "{m}");
    }

    #[test]
    fn huge_open_run_is_linear() {
        let input = ")".repeat(100_000);
        assert_eq!(fix_brackets(&input), "");
        let input = "(".repeat(100_000);
        assert_eq!(fix_brackets(&input).len(), 200_000);
    }

    #[test]
    fn over_max_size_is_unchanged() {
        let input = "(".repeat(MAX_INPUT_BYTES + 1);
        assert!(fix_brackets(&input) == input);
    }

    #[test]
    fn check_word_boundary_keywords() {
        let r = check_brackets("redo if x then\nfi");
        assert!(r.contains("No issues"), "{r}");
        // `if` in C is not a shell `if` waiting for `fi`
        assert!(check_brackets("if (x) { y(); }").contains("No issues"));
        let r = check_brackets("if [ x ]; then\n  if y; then z; fi\n");
        assert!(r.contains("keyword mismatch: 2 'if' but 1 'fi'"), "{r}");
        // keywords inside strings and comments do not count
        assert!(check_brackets("echo 'if'; fi=1 # if\nif x; then y; fi").contains("No issues"));
    }

    fn is_balanced(s: &str) -> bool {
        check_brackets(s).contains("No issues found")
    }

    #[test]
    fn rw_languages_balanced() {
        for s in [
            "def greet(name):\n    print(f'Hello {name}')",
            "const Comp = () => <div>{items.map(x => <Item key={x}/>)}</div>;",
            "type Dict<K extends string, V> = Record<K, V>;",
            "let v: Vec<i32> = Vec::<i32>::new();",
            "#define MIN(a, b) ((a) < (b) ? (a) : (b))",
            "func main() {\n    fmt.Println(\"Hello\")\n}",
            "new Thread(new Runnable() {\n    @Override\n    public void run() {\n    }\n}).start();",
            "if [ $? -eq 0 ]; then\n    echo 'ok'\nfi",
            "SELECT COUNT(*) FROM (SELECT * FROM logs WHERE ts > NOW()) t;",
            "$block = {\n    Write-Host 'test'\n}\n& $block",
            "(defun factorial (n)\n  (if (<= n 1)\n    1\n    (* n (factorial (- n 1)))))",
            "let numbers = [1, 2, 3]\nnumbers.map { $0 * 2 }.filter { $0 > 2 }",
            "<?php\nfunction test($x) {\n    return $x * 2;\n}\n?>",
            "fn main() { let s = \"hello 👋 world 🎉\"; println!(\"{}\", s); }",
            "fn 数値(値: i32) -> i32 { 値 + 1 }",
            "(normal parens ⟨fancy angle⟩ here)",
            "const s = `hello ${name} world ${1 + 2}`",
            "\"string with \\\"escaped\\\" quotes\" + (1 + 2)",
            "fn f<'a>(x: &'a str) -> &'a str { x }",
            "t[#t + 1] = v",
            "a { color: #fff; }",
        ] {
            assert!(is_balanced(s), "{s:?}: {}", check_brackets(s));
            assert_eq!(fix_brackets(s), s);
        }
    }

    #[test]
    fn rw_broken_code_is_fixed_and_balanced() {
        for s in [
            "def greet(name\n    print(f'Hello {name}')",
            "func process(data []byte {\n    return nil",
            "SELECT * FROM users WHERE (id > 5 AND status = 'active'",
            "(define (add x y) (+ x y))\n(map add '(1 2",
        ] {
            let fixed = fix_brackets(s);
            assert!(is_balanced(&fixed), "{s:?} -> {fixed:?}");
        }
    }
}
