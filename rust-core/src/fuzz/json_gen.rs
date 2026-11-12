// SPDX-License-Identifier: GPL-3.0-or-later

//! Valid-JSON generator (as a list of pieces, so corruptions can be applied at
//! known token positions) and the corruption catalogue for repair testing.

use super::Rng;

#[derive(Clone, Copy, PartialEq, Eq, Debug)]
pub enum K {
    Open,
    Close,
    Comma,
    Colon,
    Key,
    Str,
    Num,
    Lit,
    Ws,
    Junk, // inserted by a corruption
}

#[derive(Clone, Debug)]
pub struct Piece {
    pub k: K,
    pub text: String,
}

#[derive(Clone, Debug, Default)]
pub struct Doc {
    pub pieces: Vec<Piece>,
}

impl Doc {
    pub fn render(&self) -> String {
        let mut s = String::new();
        for p in &self.pieces {
            s.push_str(&p.text);
        }
        s
    }
    fn push(&mut self, k: K, t: impl Into<String>) {
        self.pieces.push(Piece { k, text: t.into() });
    }
}

pub struct GenOpts {
    pub max_depth: usize,
    pub max_items: usize,
    pub lone_surrogates: bool,
}

pub fn gen_doc(rng: &mut Rng, o: &GenOpts) -> Doc {
    let mut d = Doc::default();
    if rng.chance(1, 4) {
        ws(rng, &mut d);
    }
    // Mostly containers at top level (that is what people format), but
    // scalars too.
    if rng.chance(1, 10) {
        scalar(rng, &mut d, o);
    } else {
        container(rng, &mut d, o, 0);
    }
    if rng.chance(1, 4) {
        ws(rng, &mut d);
    }
    d
}

fn ws(rng: &mut Rng, d: &mut Doc) {
    const W: &[&str] = &[" ", "\n", "\r\n", "\t", "\n    ", "\r\n  ", "  ", "\n\n"];
    d.push(K::Ws, *rng.pick(W));
}

fn maybe_ws(rng: &mut Rng, d: &mut Doc) {
    if rng.chance(1, 3) {
        ws(rng, d);
    }
}

fn container(rng: &mut Rng, d: &mut Doc, o: &GenOpts, depth: usize) {
    let obj = rng.chance(1, 2);
    d.push(K::Open, if obj { "{" } else { "[" });
    let n = if depth >= o.max_depth {
        0
    } else {
        rng.below(o.max_items + 1)
    };
    // duplicate keys on purpose sometimes
    let mut last_key: Option<String> = None;
    for i in 0..n {
        maybe_ws(rng, d);
        if obj {
            let k = match &last_key {
                Some(k) if rng.chance(1, 8) => k.clone(),
                _ => string_raw(rng, o, true),
            };
            last_key = Some(k.clone());
            d.push(K::Key, k);
            maybe_ws(rng, d);
            d.push(K::Colon, ":");
            maybe_ws(rng, d);
        }
        if rng.chance(1, 3) && depth < o.max_depth {
            container(rng, d, o, depth + 1);
        } else {
            scalar(rng, d, o);
        }
        maybe_ws(rng, d);
        if i + 1 < n {
            d.push(K::Comma, ",");
        }
    }
    maybe_ws(rng, d);
    d.push(K::Close, if obj { "}" } else { "]" });
}

fn scalar(rng: &mut Rng, d: &mut Doc, o: &GenOpts) {
    match rng.below(10) {
        0..=3 => {
            let s = string_raw(rng, o, false);
            d.push(K::Str, s)
        }
        4..=7 => d.push(K::Num, number(rng)),
        _ => d.push(K::Lit, *rng.pick(&["true", "false", "null"])),
    }
}

pub fn number(rng: &mut Rng) -> String {
    const FIXED: &[&str] = &[
        "0",
        "-0",
        "-0.0",
        "1.0",
        "1e400",
        "-1E-400",
        "1E+2",
        "2.5e-308",
        "123456789012345678901234567890",
        "0.1000000000000000055511151231257827",
        "9007199254740993",
        "18446744073709551616",
        "-9223372036854775809",
        "3.141592653589793238462643383279",
        "1e-7",
        "100",
    ];
    if rng.chance(1, 3) {
        return rng.pick(FIXED).to_string();
    }
    let mut s = String::new();
    if rng.chance(1, 4) {
        s.push('-');
    }
    let wide = rng.chance(1, 6);
    let ndig = 1 + rng.below(if wide { 30 } else { 6 });
    if rng.chance(1, 8) {
        s.push('0');
    } else {
        s.push((b'1' + rng.below(9) as u8) as char);
        for _ in 1..ndig {
            s.push((b'0' + rng.below(10) as u8) as char);
        }
    }
    if rng.chance(1, 3) {
        s.push('.');
        for _ in 0..1 + rng.below(20) {
            s.push((b'0' + rng.below(10) as u8) as char);
        }
    }
    if rng.chance(1, 5) {
        s.push(*rng.pick(&['e', 'E']));
        if rng.chance(1, 2) {
            s.push(*rng.pick(&['+', '-']));
        }
        for _ in 0..1 + rng.below(3) {
            s.push((b'0' + rng.below(10) as u8) as char);
        }
    }
    s
}

/// Raw source of a JSON string token, quotes included. `key` biases towards
/// identifier-shaped content (so the unquoted-key corruption has targets).
pub fn string_raw(rng: &mut Rng, o: &GenOpts, key: bool) -> String {
    if key && rng.chance(1, 2) {
        const FIRST: &[u8] = b"abcxyzABC_$";
        const REST: &[u8] = b"abcxyz019_$";
        let mut s = String::from("\"");
        s.push(*rng.pick(FIRST) as char);
        for _ in 0..rng.below(8) {
            s.push(*rng.pick(REST) as char);
        }
        s.push('"');
        return s;
    }
    const PLAIN: &[&str] = &[
        "a", "b", "z", " ", "hello", "[", "]", "{", "}", ",", ":", "'", "//", "/*", "*/", "#", "é",
        "中", "😀", "\u{201C}", "\u{2019}", "true", "1", "-", "e", "\u{7F}", "\u{2028}",
        "\u{FEFF}", "NaN", "None",
    ];
    const ESC: &[&str] = &[
        "\\\"",
        "\\\\",
        "\\/",
        "\\b",
        "\\f",
        "\\n",
        "\\r",
        "\\t",
        "\\u00e9",
        "\\u0000",
        "\\u001F",
        "\\uD83D\\uDE00",
        "\\u0022",
        "\\u005C",
        "\\n",
        "\\t",
    ];
    let mut s = String::from("\"");
    let n = rng.below(10);
    for _ in 0..n {
        if rng.chance(1, 4) {
            s.push_str(rng.pick(ESC));
        } else if o.lone_surrogates && rng.chance(1, 40) {
            s.push_str(rng.pick(&["\\ud800", "\\uDC00", "\\uDBFF"]));
        } else {
            s.push_str(rng.pick(PLAIN));
        }
    }
    s.push('"');
    s
}

// ── corruption catalogue ──────────────────────────────────────────────────

#[derive(Clone, Copy, Debug, PartialEq, Eq, PartialOrd, Ord)]
pub enum Corruption {
    TrailingComma,
    MissingComma,
    SingleQuotes,
    UnquotedKey,
    MissingCloserAtEnd,
    ExtraCloser,
    MissingColon,
    LineComment,
    BlockComment,
    PyLiteral,
    NanInf,
    RawNewline,
    Truncate,
    Bom,
    SmartQuotes,
    DeleteChar,
    InsertChar,
}

pub const ALL: &[Corruption] = &[
    Corruption::TrailingComma,
    Corruption::MissingComma,
    Corruption::SingleQuotes,
    Corruption::UnquotedKey,
    Corruption::MissingCloserAtEnd,
    Corruption::ExtraCloser,
    Corruption::MissingColon,
    Corruption::LineComment,
    Corruption::BlockComment,
    Corruption::PyLiteral,
    Corruption::NanInf,
    Corruption::RawNewline,
    Corruption::Truncate,
    Corruption::Bom,
    Corruption::SmartQuotes,
    Corruption::DeleteChar,
    Corruption::InsertChar,
];

fn indices(d: &Doc, f: impl Fn(&Piece) -> bool) -> Vec<usize> {
    d.pieces
        .iter()
        .enumerate()
        .filter(|(_, p)| f(p))
        .map(|(i, _)| i)
        .collect()
}

fn prev_token(d: &Doc, i: usize) -> Option<usize> {
    (0..i).rev().find(|&j| d.pieces[j].k != K::Ws)
}

/// Content (between the quotes) of a raw string token.
fn inner(raw: &str) -> &str {
    &raw[1..raw.len() - 1]
}

/// Split raw string content into units: either one escape sequence (`\x` or
/// `\uXXXX`) or one char.
fn units(content: &str) -> Vec<&str> {
    let mut v = Vec::new();
    let b = content.as_bytes();
    let mut i = 0;
    while i < b.len() {
        if b[i] == b'\\' {
            let n = if b.get(i + 1) == Some(&b'u') { 6 } else { 2 };
            v.push(&content[i..i + n]);
            i += n;
        } else {
            let ch = content[i..].chars().next().unwrap();
            v.push(&content[i..i + ch.len_utf8()]);
            i += ch.len_utf8();
        }
    }
    v
}

/// Apply one corruption; `None` when the doc has no target for it.
pub fn corrupt(rng: &mut Rng, d: &Doc, c: Corruption) -> Option<Doc> {
    let mut d = d.clone();
    match c {
        Corruption::TrailingComma => {
            let cands: Vec<usize> = indices(&d, |p| p.k == K::Close)
                .into_iter()
                .filter(|&i| prev_token(&d, i).map(|j| d.pieces[j].k != K::Open) == Some(true))
                .collect();
            if cands.is_empty() {
                return None;
            }
            let i = *rng.pick(&cands);
            let j = prev_token(&d, i).unwrap();
            let at = if rng.chance(1, 2) { j + 1 } else { i };
            d.pieces.insert(
                at,
                Piece {
                    k: K::Junk,
                    text: ",".into(),
                },
            );
        }
        Corruption::MissingComma => {
            let cands = indices(&d, |p| p.k == K::Comma);
            if cands.is_empty() {
                return None;
            }
            let i = *rng.pick(&cands);
            d.pieces.remove(i);
        }
        Corruption::MissingColon => {
            let cands = indices(&d, |p| p.k == K::Colon);
            if cands.is_empty() {
                return None;
            }
            let i = *rng.pick(&cands);
            d.pieces.remove(i);
        }
        Corruption::SingleQuotes => {
            let cands = indices(&d, |p| p.k == K::Key || p.k == K::Str);
            if cands.is_empty() {
                return None;
            }
            let i = *rng.pick(&cands);
            let mut s = String::from("'");
            for u in units(inner(&d.pieces[i].text)) {
                match u {
                    "\\\"" => s.push('"'),
                    "'" => s.push_str("\\'"),
                    _ => s.push_str(u),
                }
            }
            s.push('\'');
            d.pieces[i].text = s;
            d.pieces[i].k = K::Junk;
        }
        Corruption::SmartQuotes => {
            let cands = indices(&d, |p| p.k == K::Key || p.k == K::Str);
            if cands.is_empty() {
                return None;
            }
            let i = *rng.pick(&cands);
            let s = format!("\u{201C}{}\u{201D}", inner(&d.pieces[i].text));
            d.pieces[i].text = s;
            d.pieces[i].k = K::Junk;
        }
        Corruption::UnquotedKey => {
            let cands = indices(&d, |p| {
                p.k == K::Key && {
                    let s = inner(&p.text);
                    let mut cs = s.chars();
                    matches!(cs.next(), Some(c) if c.is_ascii_alphabetic() || c == '_' || c == '$')
                        && cs.all(|c| c.is_ascii_alphanumeric() || c == '_' || c == '$')
                }
            });
            if cands.is_empty() {
                return None;
            }
            let i = *rng.pick(&cands);
            let s = inner(&d.pieces[i].text).to_string();
            d.pieces[i].text = s;
            d.pieces[i].k = K::Junk;
        }
        Corruption::MissingCloserAtEnd => {
            let last = (0..d.pieces.len())
                .rev()
                .find(|&j| d.pieces[j].k != K::Ws)?;
            if d.pieces[last].k != K::Close {
                return None;
            }
            d.pieces.remove(last);
        }
        Corruption::ExtraCloser => {
            let at = rng.below(d.pieces.len() + 1);
            d.pieces.insert(
                at,
                Piece {
                    k: K::Junk,
                    text: rng.pick(&["]", "}"]).to_string(),
                },
            );
        }
        Corruption::LineComment => {
            let at = rng.below(d.pieces.len() + 1);
            let t = *rng.pick(&[
                "// note\n",
                "// ] } \" ' , :\n",
                "//\n",
                "  // trailing {\r\n",
                "// x // y */\n",
            ]);
            d.pieces.insert(
                at,
                Piece {
                    k: K::Junk,
                    text: t.into(),
                },
            );
        }
        Corruption::BlockComment => {
            let at = rng.below(d.pieces.len() + 1);
            let t = *rng.pick(&[
                "/* c */",
                "/* ] } \" ' */",
                "/**/",
                "/* multi\n line { */",
                "/* // */",
            ]);
            d.pieces.insert(
                at,
                Piece {
                    k: K::Junk,
                    text: t.into(),
                },
            );
        }
        Corruption::PyLiteral => {
            let cands = indices(&d, |p| p.k == K::Lit);
            if cands.is_empty() {
                return None;
            }
            let i = *rng.pick(&cands);
            let t = match d.pieces[i].text.as_str() {
                "true" => "True",
                "false" => "False",
                _ => "None",
            };
            d.pieces[i].text = t.into();
            d.pieces[i].k = K::Junk;
        }
        Corruption::NanInf => {
            let cands = indices(&d, |p| p.k == K::Num);
            if cands.is_empty() {
                return None;
            }
            let i = *rng.pick(&cands);
            d.pieces[i].text = rng.pick(&["NaN", "Infinity", "-Infinity"]).to_string();
            d.pieces[i].k = K::Junk;
        }
        Corruption::RawNewline => {
            let cands = indices(&d, |p| {
                (p.k == K::Key || p.k == K::Str)
                    && units(inner(&p.text))
                        .iter()
                        .any(|u| *u == "\\n" || *u == "\\t" || *u == "\\r")
            });
            if cands.is_empty() {
                return None;
            }
            let i = *rng.pick(&cands);
            let us = units(inner(&d.pieces[i].text));
            let targets: Vec<usize> = us
                .iter()
                .enumerate()
                .filter(|(_, u)| **u == "\\n" || **u == "\\t" || **u == "\\r")
                .map(|(j, _)| j)
                .collect();
            let t = *rng.pick(&targets);
            let mut s = String::from("\"");
            for (j, u) in us.iter().enumerate() {
                if j == t {
                    s.push(match *u {
                        "\\n" => '\n',
                        "\\t" => '\t',
                        _ => '\r',
                    });
                } else {
                    s.push_str(u);
                }
            }
            s.push('"');
            d.pieces[i].text = s;
            d.pieces[i].k = K::Junk;
        }
        Corruption::Truncate => {
            let s = d.render();
            let cuts: Vec<usize> = s
                .char_indices()
                .map(|(i, _)| i)
                .filter(|&i| i > 0)
                .collect();
            if cuts.is_empty() {
                return None;
            }
            let at = *rng.pick(&cuts);
            return Some(Doc {
                pieces: vec![Piece {
                    k: K::Junk,
                    text: s[..at].to_string(),
                }],
            });
        }
        Corruption::Bom => {
            d.pieces.insert(
                0,
                Piece {
                    k: K::Junk,
                    text: "\u{FEFF}".into(),
                },
            );
        }
        Corruption::DeleteChar | Corruption::InsertChar => {
            let s = d.render();
            let idx: Vec<usize> = s.char_indices().map(|(i, _)| i).collect();
            if idx.is_empty() {
                return None;
            }
            let at = *rng.pick(&idx);
            let out = if c == Corruption::DeleteChar {
                let ch = s[at..].chars().next().unwrap();
                format!("{}{}", &s[..at], &s[at + ch.len_utf8()..])
            } else {
                let ins = *rng.pick(&[
                    "{", "}", "[", "]", ",", ":", "\"", "'", "a", "1", " ", "\n", "/", "\\", "-",
                    ".",
                ]);
                format!("{}{}{}", &s[..at], ins, &s[at..])
            };
            return Some(Doc {
                pieces: vec![Piece {
                    k: K::Junk,
                    text: out,
                }],
            });
        }
    }
    Some(d)
}

/// Positions (byte offsets in the rendered doc) that are token boundaries
/// outside strings, and positions strictly inside string contents.
pub fn boundaries(d: &Doc) -> (Vec<usize>, Vec<usize>) {
    let mut outside = Vec::new();
    let mut inside = Vec::new();
    let mut off = 0;
    for p in &d.pieces {
        outside.push(off);
        if p.k == K::Key || p.k == K::Str {
            for (i, _) in p.text.char_indices().skip(1) {
                inside.push(off + i);
            }
        }
        off += p.text.len();
    }
    outside.push(off);
    (outside, inside)
}
