// SPDX-License-Identifier: GPL-3.0-or-later

//! Balanced code-like text generator for the bracket fixer, plus small
//! reference "is it balanced?" readers that understand exactly the
//! constructs the generator emits (independent of `bracket_fix`).

use super::Rng;

#[derive(Clone, Copy, PartialEq, Eq, Debug)]
pub enum Flavor {
    /// C / JS / Rust / Java style: `//`, `/* */`, "..", 'c', `template ${}`, /regex/
    CLike,
    /// Python / shell style: `#` comments, "..", '..', """..""", heredocs
    Hash,
}

pub struct Code {
    pub text: String,
    /// Byte offsets of brackets that are CODE (not in strings/comments) and
    /// are fair game for a delete mutation.
    pub brackets: Vec<usize>,
    /// Byte offsets between code tokens where inserting a bracket lands in
    /// code.
    pub gaps: Vec<usize>,
}

struct G<'a> {
    rng: &'a mut Rng,
    flavor: Flavor,
    out: String,
    brackets: Vec<usize>,
    gaps: Vec<usize>,
    budget: usize,
}

impl G<'_> {
    fn gap(&mut self) {
        self.gaps.push(self.out.len());
    }
    fn open(&mut self, c: char) {
        self.brackets.push(self.out.len());
        self.out.push(c);
    }
    fn close(&mut self, c: char) {
        self.brackets.push(self.out.len());
        self.out.push(c);
    }
    fn s(&mut self, t: &str) {
        self.out.push_str(t);
    }
    fn nl(&mut self, indent: usize) {
        self.out.push('\n');
        for _ in 0..indent {
            self.out.push_str("    ");
        }
    }

    fn ident(&mut self) {
        let w = *self.rng.pick(&[
            "foo",
            "bar",
            "x",
            "y",
            "i",
            "value",
            "dont",
            "it_s",
            "n2",
            "self.items",
        ]);
        self.s(w);
    }

    fn string_lit(&mut self) {
        const BITS: &[&str] = &[
            "(", ")", "[", "]", "{", "}", "don't", " ", "\\\"", "\\\\", "//", "/*", "#", "`", "${",
            "text", "é", "\\n",
        ];
        self.s("\"");
        for _ in 0..self.rng.below(6) {
            let b = *self.rng.pick(BITS);
            self.s(b);
        }
        self.s("\"");
    }

    fn single_lit(&mut self) {
        match self.flavor {
            Flavor::CLike => {
                let c = *self.rng.pick(&[
                    "'('", "')'", "'{'", "'}'", "'['", "']'", "'\\''", "'\"'", "'a'",
                ]);
                self.s(c);
            }
            Flavor::Hash => {
                let c = *self
                    .rng
                    .pick(&["'('", "'a ) b'", "'{x}'", "'it\\'s ]'", "'\"['", "''"]);
                self.s(c);
            }
        }
    }

    fn comment(&mut self, indent: usize) {
        match self.flavor {
            Flavor::CLike => {
                if self.rng.chance(1, 2) {
                    let c = *self.rng.pick(&[
                        "// see (note",
                        "// don't { here",
                        "// ] [ ) (",
                        "// \"unterminated",
                        "//",
                    ]);
                    self.s(" ");
                    self.s(c);
                    self.nl(indent);
                } else {
                    let c = *self.rng.pick(&[
                        "/* ( */",
                        "/* it's [ */",
                        "/* multi\n   line { */",
                        "/**/",
                        "/* \" */",
                    ]);
                    self.s(" ");
                    self.s(c);
                    self.s(" ");
                }
            }
            Flavor::Hash => {
                let c = *self.rng.pick(&[
                    "# see (note",
                    "# don't { here",
                    "# ] [ ) (",
                    "# \"unterminated",
                    "#",
                ]);
                self.s("  ");
                self.s(c);
                self.nl(indent);
            }
        }
    }

    fn special(&mut self, indent: usize) {
        match self.flavor {
            Flavor::CLike => match self.rng.below(2) {
                0 => {
                    // template literal with a code hole
                    self.s("`a ( [ ");
                    self.s("${");
                    self.expr(2, indent);
                    self.s("}");
                    self.s(" ] {`");
                }
                _ => {
                    // regex literal after '='
                    self.s("re = ");
                    let r = *self.rng.pick(&[
                        "/[(]+/g",
                        "/\\)x{2}\\//",
                        "/[{}]/",
                        "/a(b)c/",
                        "/[\\]]/",
                    ]);
                    self.s(r);
                }
            },
            Flavor::Hash => match self.rng.below(2) {
                0 => {
                    let t = *self.rng.pick(&[
                        "\"\"\"doc ( [\n  { more \"quoted\" ]\"\"\"",
                        "'''a ) '' b\n c'''",
                    ]);
                    self.s(t);
                }
                _ => {
                    self.s("cat <<EOF");
                    self.out.push('\n');
                    self.s("raw ( text [ here");
                    self.out.push('\n');
                    self.s("EOF");
                    self.nl(indent);
                }
            },
        }
    }

    fn expr(&mut self, depth: usize, indent: usize) {
        self.budget = self.budget.saturating_sub(1);
        if depth == 0 || self.budget == 0 {
            match self.rng.below(5) {
                0 => self.string_lit(),
                1 => self.single_lit(),
                2 => self.s("42"),
                _ => self.ident(),
            }
            return;
        }
        match self.rng.below(9) {
            0 | 1 => {
                // call f(a, b)
                self.ident();
                self.open('(');
                let n = self.rng.below(3);
                for k in 0..n {
                    if k > 0 {
                        self.s(", ");
                    }
                    self.gap();
                    self.expr(depth - 1, indent);
                    self.gap();
                }
                self.close(')');
            }
            2 => {
                self.ident();
                self.open('[');
                self.gap();
                self.expr(depth - 1, indent);
                self.gap();
                self.close(']');
            }
            3 => {
                self.open('[');
                let n = self.rng.below(4);
                for k in 0..n {
                    if k > 0 {
                        self.s(", ");
                    }
                    self.gap();
                    self.expr(depth - 1, indent);
                }
                self.gap();
                self.close(']');
            }
            4 => {
                self.open('{');
                self.s(" ");
                self.gap();
                self.ident();
                self.s(": ");
                self.expr(depth - 1, indent);
                self.s(" ");
                self.gap();
                self.close('}');
            }
            5 => {
                // multi-line argument list
                self.ident();
                self.open('(');
                let n = 1 + self.rng.below(3);
                for k in 0..n {
                    self.nl(indent + 1);
                    self.gap();
                    self.expr(depth - 1, indent + 1);
                    if k + 1 < n {
                        self.s(",");
                    }
                }
                self.nl(indent);
                self.close(')');
            }
            6 => {
                self.open('(');
                self.expr(depth - 1, indent);
                self.s(" + ");
                self.gap();
                self.expr(depth - 1, indent);
                self.close(')');
            }
            7 => self.comment_then_expr(depth, indent),
            _ => {
                self.expr(depth - 1, indent);
                self.s(" * ");
                self.expr(depth - 1, indent);
            }
        }
    }

    fn comment_then_expr(&mut self, depth: usize, indent: usize) {
        self.ident();
        self.open('(');
        self.comment(indent + 1);
        self.gap();
        self.expr(depth - 1, indent + 1);
        self.close(')');
    }

    fn stmt(&mut self, depth: usize, indent: usize) {
        self.budget = self.budget.saturating_sub(1);
        match self.rng.below(8) {
            0 | 1 if depth > 0 && self.flavor == Flavor::CLike => {
                // if (cond) {\n ... \n}
                self.s("if ");
                self.open('(');
                self.expr(1, indent);
                self.close(')');
                self.s(" ");
                self.open('{');
                let n = 1 + self.rng.below(3);
                for _ in 0..n {
                    self.nl(indent + 1);
                    self.gap();
                    self.stmt(depth - 1, indent + 1);
                }
                self.nl(indent);
                self.close('}');
                if self.rng.chance(1, 3) {
                    self.s(" else ");
                    self.open('{');
                    self.nl(indent + 1);
                    self.stmt(0, indent + 1);
                    self.nl(indent);
                    self.close('}');
                }
            }
            0 | 1 if depth > 0 => {
                // python-ish block
                self.s("def ");
                self.ident();
                self.open('(');
                self.ident();
                self.close(')');
                self.s(":");
                let n = 1 + self.rng.below(3);
                for _ in 0..n {
                    self.nl(indent + 1);
                    self.gap();
                    self.stmt(depth - 1, indent + 1);
                }
            }
            2 => {
                self.comment(indent);
                self.gap();
                self.ident();
                self.s(" = ");
                self.expr(2, indent);
                self.end();
            }
            3 => {
                self.special(indent);
                self.end();
            }
            _ => {
                self.ident();
                self.s(" = ");
                self.gap();
                self.expr(depth.min(3), indent);
                self.gap();
                self.end();
            }
        }
    }

    fn end(&mut self) {
        if self.flavor == Flavor::CLike {
            self.s(";");
        }
    }
}

pub fn gen_code(rng: &mut Rng, flavor: Flavor, size: usize) -> Code {
    let mut g = G {
        rng,
        flavor,
        out: String::new(),
        brackets: Vec::new(),
        gaps: Vec::new(),
        budget: size,
    };
    let n = 1 + g.rng.below(4);
    for k in 0..n {
        if k > 0 {
            g.nl(0);
            if g.rng.chance(1, 3) {
                g.nl(0);
            }
        }
        g.gap();
        let d = 1 + g.rng.below(3);
        g.stmt(d, 0);
    }
    if g.rng.chance(1, 2) {
        g.out.push('\n');
    }
    g.gap();
    Code {
        text: g.out,
        brackets: g.brackets,
        gaps: g.gaps,
    }
}

pub fn is_bracket(c: char) -> bool {
    matches!(c, '(' | ')' | '[' | ']' | '{' | '}')
}

pub fn strip_brackets(s: &str) -> String {
    s.chars().filter(|c| !is_bracket(*c)).collect()
}

fn balanced_seq(seq: &[u8]) -> bool {
    let mut st: Vec<u8> = Vec::new();
    for &c in seq {
        match c {
            b'(' | b'[' | b'{' => st.push(c),
            _ => {
                let want = match c {
                    b')' => b'(',
                    b']' => b'[',
                    _ => b'{',
                };
                if st.pop() != Some(want) {
                    return false;
                }
            }
        }
    }
    st.is_empty()
}

/// Reference balance check for generator output (and mutations of it).
pub fn ref_balanced(s: &str, flavor: Flavor) -> bool {
    let b = s.as_bytes();
    let mut seq: Vec<u8> = Vec::new();
    // template nesting: stack of brace depths at which a `${` hole opened
    let mut holes: Vec<usize> = Vec::new();
    let mut brace_depth = 0usize;
    let mut i = 0;
    let mut in_template = false;
    while i < b.len() {
        let c = b[i];
        if in_template {
            if c == b'\\' {
                i += 2;
                continue;
            }
            if c == b'`' {
                in_template = false;
                i += 1;
                continue;
            }
            if c == b'$' && b.get(i + 1) == Some(&b'{') {
                holes.push(brace_depth);
                in_template = false;
                i += 2;
                continue;
            }
            i += 1;
            continue;
        }
        match (flavor, c) {
            (_, b'"') if flavor == Flavor::Hash && b[i..].starts_with(b"\"\"\"") => {
                let end = find(b, i + 3, b"\"\"\"");
                i = end + 3;
            }
            (_, b'\'') if flavor == Flavor::Hash && b[i..].starts_with(b"'''") => {
                let end = find(b, i + 3, b"'''");
                i = end + 3;
            }
            (_, b'"') | (_, b'\'') => {
                let q = c;
                i += 1;
                while i < b.len() && b[i] != q {
                    if b[i] == b'\\' {
                        i += 1;
                    }
                    i += 1;
                }
                i += 1;
            }
            (Flavor::CLike, b'/') if b.get(i + 1) == Some(&b'/') => {
                while i < b.len() && b[i] != b'\n' {
                    i += 1;
                }
            }
            (Flavor::CLike, b'/') if b.get(i + 1) == Some(&b'*') => {
                let end = find(b, i + 2, b"*/");
                i = end + 2;
            }
            (Flavor::CLike, b'/') if i >= 2 && &b[i - 2..i] == b"= " => {
                // regex literal
                i += 1;
                let mut class = false;
                while i < b.len() {
                    match b[i] {
                        b'\\' => i += 1,
                        b'[' => class = true,
                        b']' => class = false,
                        b'/' if !class => break,
                        _ => {}
                    }
                    i += 1;
                }
                i += 1;
            }
            (Flavor::CLike, b'`') => {
                in_template = true;
                i += 1;
            }
            (Flavor::CLike, b'}') if holes.last() == Some(&brace_depth) => {
                holes.pop();
                in_template = true;
                i += 1;
            }
            (Flavor::Hash, b'#') => {
                while i < b.len() && b[i] != b'\n' {
                    i += 1;
                }
            }
            (Flavor::Hash, b'<') if b[i..].starts_with(b"<<EOF\n") => {
                let end = find(b, i, b"\nEOF\n");
                i = end + 4;
            }
            (_, b'(' | b'[' | b'{' | b')' | b']' | b'}') => {
                if c == b'{' {
                    brace_depth += 1;
                } else if c == b'}' {
                    brace_depth = brace_depth.saturating_sub(1);
                }
                seq.push(c);
                i += 1;
            }
            _ => i += 1,
        }
    }
    !in_template && holes.is_empty() && balanced_seq(&seq)
}

fn find(b: &[u8], from: usize, pat: &[u8]) -> usize {
    if from >= b.len() {
        return b.len();
    }
    b[from..]
        .windows(pat.len())
        .position(|w| w == pat)
        .map(|p| from + p)
        .unwrap_or(b.len())
}
