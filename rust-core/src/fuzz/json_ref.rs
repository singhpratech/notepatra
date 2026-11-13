// SPDX-License-Identifier: GPL-3.0-or-later

//! Independent reference JSON reader for the harness. Written separately
//! from `json_fmt` on purpose — the formatter must not grade itself.
//!
//! Stricter than serde_json about what "same meaning" is: object members are
//! an ordered list (key order and duplicate keys count), numbers are their
//! exact source TEXT, strings are compared as decoded UTF-16 code units (so a
//! lone surrogate escape is representable and `\u00e9` == `é`).

#[derive(Debug, Clone, PartialEq)]
pub enum RVal {
    Null,
    Bool(bool),
    Num(String),
    Str(Vec<u16>),
    Arr(Vec<RVal>),
    Obj(Vec<(Vec<u16>, RVal)>),
}

/// Raw lexical token of a valid document: structural char, exact string
/// source (with quotes), exact number text, or literal.
pub fn ref_tokens(s: &str) -> Option<Vec<String>> {
    let mut p = P::new(s);
    let mut toks = Vec::new();
    p.value_tokens(&mut toks, 0)?;
    p.ws();
    if p.i != p.b.len() {
        return None;
    }
    Some(toks)
}

/// Parse a complete document (an optional leading U+FEFF is ignored, as
/// RFC 8259 §8.1 permits). `None` = invalid.
pub fn ref_parse(s: &str) -> Option<RVal> {
    let mut p = P::new(s);
    let v = p.value(0)?;
    p.ws();
    if p.i != p.b.len() {
        return None;
    }
    Some(v)
}

const MAX_DEPTH: usize = 3000;

struct P<'a> {
    b: &'a [u8],
    i: usize,
}

impl<'a> P<'a> {
    fn new(s: &'a str) -> Self {
        let b = s.as_bytes();
        let i = if b.starts_with("\u{FEFF}".as_bytes()) {
            3
        } else {
            0
        };
        P { b, i }
    }
    fn ws(&mut self) {
        while self.i < self.b.len() && matches!(self.b[self.i], b' ' | b'\t' | b'\n' | b'\r') {
            self.i += 1;
        }
    }
    fn peek(&self) -> Option<u8> {
        self.b.get(self.i).copied()
    }
    fn value(&mut self, depth: usize) -> Option<RVal> {
        if depth > MAX_DEPTH {
            return None;
        }
        self.ws();
        match self.peek()? {
            b'{' => {
                self.i += 1;
                let mut members = Vec::new();
                self.ws();
                if self.peek()? == b'}' {
                    self.i += 1;
                    return Some(RVal::Obj(members));
                }
                loop {
                    self.ws();
                    if self.peek()? != b'"' {
                        return None;
                    }
                    let k = self.string()?;
                    self.ws();
                    if self.peek()? != b':' {
                        return None;
                    }
                    self.i += 1;
                    let v = self.value(depth + 1)?;
                    members.push((k, v));
                    self.ws();
                    match self.peek()? {
                        b',' => self.i += 1,
                        b'}' => {
                            self.i += 1;
                            return Some(RVal::Obj(members));
                        }
                        _ => return None,
                    }
                }
            }
            b'[' => {
                self.i += 1;
                let mut items = Vec::new();
                self.ws();
                if self.peek()? == b']' {
                    self.i += 1;
                    return Some(RVal::Arr(items));
                }
                loop {
                    items.push(self.value(depth + 1)?);
                    self.ws();
                    match self.peek()? {
                        b',' => self.i += 1,
                        b']' => {
                            self.i += 1;
                            return Some(RVal::Arr(items));
                        }
                        _ => return None,
                    }
                }
            }
            b'"' => Some(RVal::Str(self.string()?)),
            b't' => self.lit("true", RVal::Bool(true)),
            b'f' => self.lit("false", RVal::Bool(false)),
            b'n' => self.lit("null", RVal::Null),
            _ => {
                let start = self.i;
                self.number()?;
                Some(RVal::Num(
                    String::from_utf8(self.b[start..self.i].to_vec()).ok()?,
                ))
            }
        }
    }
    fn value_tokens(&mut self, out: &mut Vec<String>, depth: usize) -> Option<()> {
        if depth > MAX_DEPTH {
            return None;
        }
        self.ws();
        let start = self.i;
        match self.peek()? {
            b'{' | b'[' => {
                let open = self.peek()?;
                let close = if open == b'{' { b'}' } else { b']' };
                self.i += 1;
                out.push((open as char).to_string());
                self.ws();
                if self.peek()? == close {
                    self.i += 1;
                    out.push((close as char).to_string());
                    return Some(());
                }
                loop {
                    if open == b'{' {
                        self.ws();
                        let ks = self.i;
                        if self.peek()? != b'"' {
                            return None;
                        }
                        self.string()?;
                        out.push(String::from_utf8(self.b[ks..self.i].to_vec()).ok()?);
                        self.ws();
                        if self.peek()? != b':' {
                            return None;
                        }
                        self.i += 1;
                        out.push(":".into());
                    }
                    self.value_tokens(out, depth + 1)?;
                    self.ws();
                    let c = self.peek()?;
                    self.i += 1;
                    if c == b',' {
                        out.push(",".into());
                    } else if c == close {
                        out.push((close as char).to_string());
                        return Some(());
                    } else {
                        return None;
                    }
                }
            }
            b'"' => {
                self.string()?;
            }
            b't' => {
                self.lit("true", RVal::Null)?;
            }
            b'f' => {
                self.lit("false", RVal::Null)?;
            }
            b'n' => {
                self.lit("null", RVal::Null)?;
            }
            _ => self.number()?,
        }
        out.push(String::from_utf8(self.b[start..self.i].to_vec()).ok()?);
        Some(())
    }
    fn lit(&mut self, word: &str, v: RVal) -> Option<RVal> {
        if self.b[self.i..].starts_with(word.as_bytes()) {
            self.i += word.len();
            Some(v)
        } else {
            None
        }
    }
    fn digits(&mut self) -> usize {
        let s = self.i;
        while self.i < self.b.len() && self.b[self.i].is_ascii_digit() {
            self.i += 1;
        }
        self.i - s
    }
    fn number(&mut self) -> Option<()> {
        if self.peek() == Some(b'-') {
            self.i += 1;
        }
        match self.peek()? {
            b'0' => self.i += 1,
            b'1'..=b'9' => {
                self.digits();
            }
            _ => return None,
        }
        if self.peek() == Some(b'.') {
            self.i += 1;
            if self.digits() == 0 {
                return None;
            }
        }
        if matches!(self.peek(), Some(b'e' | b'E')) {
            self.i += 1;
            if matches!(self.peek(), Some(b'+' | b'-')) {
                self.i += 1;
            }
            if self.digits() == 0 {
                return None;
            }
        }
        Some(())
    }
    fn string(&mut self) -> Option<Vec<u16>> {
        // at '"'
        self.i += 1;
        let mut out: Vec<u16> = Vec::new();
        loop {
            let c = *self.b.get(self.i)?;
            match c {
                b'"' => {
                    self.i += 1;
                    return Some(out);
                }
                b'\\' => {
                    let e = *self.b.get(self.i + 1)?;
                    self.i += 2;
                    let u = match e {
                        b'"' => 0x22,
                        b'\\' => 0x5C,
                        b'/' => 0x2F,
                        b'b' => 0x08,
                        b'f' => 0x0C,
                        b'n' => 0x0A,
                        b'r' => 0x0D,
                        b't' => 0x09,
                        b'u' => {
                            let h = std::str::from_utf8(self.b.get(self.i..self.i + 4)?).ok()?;
                            if !h.bytes().all(|x| x.is_ascii_hexdigit()) {
                                return None;
                            }
                            self.i += 4;
                            u16::from_str_radix(h, 16).ok()?
                        }
                        _ => return None,
                    };
                    out.push(u);
                }
                0..=0x1F => return None,
                c => {
                    // one UTF-8 scalar (the input is a &str and we only ever
                    // advance by whole chars, so this slice is well-formed)
                    let n = match c {
                        0x00..=0x7F => 1,
                        0xC0..=0xDF => 2,
                        0xE0..=0xEF => 3,
                        _ => 4,
                    };
                    let ch = std::str::from_utf8(self.b.get(self.i..self.i + n)?)
                        .ok()?
                        .chars()
                        .next()?;
                    let mut buf = [0u16; 2];
                    out.extend_from_slice(ch.encode_utf16(&mut buf));
                    self.i += ch.len_utf8();
                }
            }
        }
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn ref_parser_basics() {
        assert!(ref_parse("{\"a\":[1,2.5e3,-0,true,null,\"\\u00e9\"]}").is_some());
        assert!(ref_parse("[1,]").is_none());
        assert!(ref_parse("01").is_none());
        assert!(ref_parse("\"\\ud800\"").is_some());
        assert!(ref_parse("\u{FEFF}[]").is_some());
        assert!(ref_parse("[\"a\nb\"]").is_none());
        assert_eq!(
            ref_parse("{\"a\":1,\"a\":2}"),
            Some(RVal::Obj(vec![
                (vec![97], RVal::Num("1".into())),
                (vec![97], RVal::Num("2".into()))
            ]))
        );
        assert_eq!(
            ref_tokens("{ \"a\" : [ 1.0 ] }").unwrap(),
            vec!["{", "\"a\"", ":", "[", "1.0", "]", "}"]
        );
    }
}
