// SPDX-License-Identifier: GPL-3.0-or-later

//! Property/fuzz harness for the HTML formatter.
//!
//! Oracle: html5ever (dev-dependency only, never linked into the shipped
//! static library) parses input and output as a browser would; the two DOMs
//! must be identical except for whitespace-only differences that the
//! rendering collapses next to block-level elements (see `normalize`).
//!
//! Case counts: `NP_HTML_FUZZ_CASES` (default 20000 per generator family),
//! seed: `NP_HTML_FUZZ_SEED` (default fixed), threads: `NP_HTML_FUZZ_THREADS`
//! (default 4). Long run:
//!
//! ```text
//! NP_HTML_FUZZ_CASES=2000000 NP_HTML_FUZZ_THREADS=6 \
//!   cargo test --release html_fmt::fuzz -- --nocapture --test-threads=1
//! ```

use super::*;
use html5ever::interface::{ElementFlags, NodeOrText, QuirksMode, TreeSink};
use html5ever::tendril::{StrTendril, TendrilSink};
use html5ever::{parse_document, Attribute, ExpandedName, QualName};
use std::borrow::Cow;
use std::cell::{Cell, RefCell};
use std::rc::{Rc, Weak};
use std::time::{Duration, Instant};

// ═══════════════════════════════════════════════════════════
// Minimal DOM sink for html5ever
// ═══════════════════════════════════════════════════════════

enum Data {
    Document,
    Doctype(String, String, String),
    Text(RefCell<String>),
    Comment(String),
    Pi(String, String),
    Element {
        name: QualName,
        attrs: RefCell<Vec<Attribute>>,
        template: Option<Handle>,
    },
}

struct Node {
    data: Data,
    parent: Cell<Option<Weak<Node>>>,
    children: RefCell<Vec<Handle>>,
}

type Handle = Rc<Node>;

fn new_node(data: Data) -> Handle {
    Rc::new(Node {
        data,
        parent: Cell::new(None),
        children: RefCell::new(Vec::new()),
    })
}

fn parent_of(n: &Handle) -> Option<Handle> {
    let w = n.parent.take();
    let p = w.as_ref().and_then(|w| w.upgrade());
    n.parent.set(w);
    p
}

fn detach(n: &Handle) {
    if let Some(p) = parent_of(n) {
        p.children.borrow_mut().retain(|c| !Rc::ptr_eq(c, n));
    }
    n.parent.set(None);
}

fn append_node(parent: &Handle, child: Handle) {
    detach(&child);
    child.parent.set(Some(Rc::downgrade(parent)));
    parent.children.borrow_mut().push(child);
}

struct Sink {
    doc: Handle,
}

impl TreeSink for Sink {
    type Handle = Handle;
    type Output = Handle;
    type ElemName<'a>
        = ExpandedName<'a>
    where
        Self: 'a;

    fn finish(self) -> Handle {
        self.doc
    }
    fn parse_error(&self, _msg: Cow<'static, str>) {}
    fn get_document(&self) -> Handle {
        self.doc.clone()
    }
    fn elem_name<'a>(&'a self, target: &'a Handle) -> ExpandedName<'a> {
        match &target.data {
            Data::Element { name, .. } => name.expanded(),
            _ => panic!("not an element"),
        }
    }
    fn create_element(&self, name: QualName, attrs: Vec<Attribute>, flags: ElementFlags) -> Handle {
        new_node(Data::Element {
            name,
            attrs: RefCell::new(attrs),
            template: if flags.template {
                Some(new_node(Data::Document))
            } else {
                None
            },
        })
    }
    fn create_comment(&self, text: StrTendril) -> Handle {
        new_node(Data::Comment(text.to_string()))
    }
    fn create_pi(&self, target: StrTendril, data: StrTendril) -> Handle {
        new_node(Data::Pi(target.to_string(), data.to_string()))
    }
    fn append(&self, parent: &Handle, child: NodeOrText<Handle>) {
        match child {
            NodeOrText::AppendNode(n) => append_node(parent, n),
            NodeOrText::AppendText(t) => {
                if let Some(last) = parent.children.borrow().last() {
                    if let Data::Text(s) = &last.data {
                        s.borrow_mut().push_str(&t);
                        return;
                    }
                }
                append_node(parent, new_node(Data::Text(RefCell::new(t.to_string()))));
            }
        }
    }
    fn append_based_on_parent_node(
        &self,
        element: &Handle,
        prev: &Handle,
        child: NodeOrText<Handle>,
    ) {
        if parent_of(element).is_some() {
            self.append_before_sibling(element, child);
        } else {
            self.append(prev, child);
        }
    }
    fn append_doctype_to_document(
        &self,
        name: StrTendril,
        public_id: StrTendril,
        system_id: StrTendril,
    ) {
        append_node(
            &self.doc,
            new_node(Data::Doctype(
                name.to_string(),
                public_id.to_string(),
                system_id.to_string(),
            )),
        );
    }
    fn get_template_contents(&self, target: &Handle) -> Handle {
        match &target.data {
            Data::Element {
                template: Some(t), ..
            } => t.clone(),
            _ => panic!("not a template"),
        }
    }
    fn same_node(&self, x: &Handle, y: &Handle) -> bool {
        Rc::ptr_eq(x, y)
    }
    fn set_quirks_mode(&self, _mode: QuirksMode) {}
    fn append_before_sibling(&self, sibling: &Handle, child: NodeOrText<Handle>) {
        let parent = parent_of(sibling).expect("sibling without parent");
        let idx = parent
            .children
            .borrow()
            .iter()
            .position(|c| Rc::ptr_eq(c, sibling))
            .unwrap();
        let node = match child {
            NodeOrText::AppendText(t) => {
                if idx > 0 {
                    let prev = parent.children.borrow()[idx - 1].clone();
                    if let Data::Text(s) = &prev.data {
                        s.borrow_mut().push_str(&t);
                        return;
                    }
                }
                new_node(Data::Text(RefCell::new(t.to_string())))
            }
            NodeOrText::AppendNode(n) => n,
        };
        detach(&node);
        let idx = parent
            .children
            .borrow()
            .iter()
            .position(|c| Rc::ptr_eq(c, sibling))
            .unwrap();
        node.parent.set(Some(Rc::downgrade(&parent)));
        parent.children.borrow_mut().insert(idx, node);
    }
    fn add_attrs_if_missing(&self, target: &Handle, attrs: Vec<Attribute>) {
        if let Data::Element {
            attrs: existing, ..
        } = &target.data
        {
            let mut ex = existing.borrow_mut();
            for a in attrs {
                if !ex.iter().any(|e| e.name == a.name) {
                    ex.push(a);
                }
            }
        }
    }
    fn remove_from_parent(&self, target: &Handle) {
        detach(target);
    }
    fn reparent_children(&self, node: &Handle, new_parent: &Handle) {
        let kids: Vec<Handle> = std::mem::take(&mut *node.children.borrow_mut());
        for k in kids {
            k.parent.set(Some(Rc::downgrade(new_parent)));
            new_parent.children.borrow_mut().push(k);
        }
    }
}

fn parse(html: &str) -> Handle {
    let sink = Sink {
        doc: new_node(Data::Document),
    };
    parse_document(sink, Default::default()).one(StrTendril::from(html))
}

// ═══════════════════════════════════════════════════════════
// Normalised DOM dump
// ═══════════════════════════════════════════════════════════

fn el_info(n: &Handle) -> Option<(&QualName, bool)> {
    match &n.data {
        Data::Element { name, .. } => Some((name, name.ns == html5ever::ns!(html))),
        _ => None,
    }
}

fn attr(n: &Handle, a: &str) -> Option<String> {
    match &n.data {
        Data::Element { attrs, .. } => attrs
            .borrow()
            .iter()
            .find(|x| x.name.ns == html5ever::ns!() && &*x.name.local == a)
            .map(|x| x.value.to_string()),
        _ => None,
    }
}

/// A block-level element under the UA stylesheet, not hidden, no inline style.
fn oracle_block(n: &Handle) -> bool {
    match el_info(n) {
        Some((name, true)) => {
            is_block(&name.local) && attr(n, "hidden").is_none() && attr(n, "style").is_none()
        }
        _ => false,
    }
}

/// Parents whose whitespace-only text children are never rendered.
fn ws_invisible_parent(n: &Handle) -> bool {
    match el_info(n) {
        Some((name, true)) => matches!(
            &*name.local,
            "html" | "head" | "table" | "tbody" | "thead" | "tfoot" | "tr" | "colgroup" | "select"
        ),
        Some(_) => false,
        None => matches!(n.data, Data::Document),
    }
}

/// Elements whose subtree must be compared byte for byte.
fn sensitive(n: &Handle) -> bool {
    match el_info(n) {
        Some((name, true)) => {
            matches!(
                &*name.local,
                "pre"
                    | "listing"
                    | "xmp"
                    | "plaintext"
                    | "textarea"
                    | "script"
                    | "style"
                    | "title"
                    | "noscript"
                    | "noembed"
                    | "noframes"
                    | "iframe"
                    | "template"
            ) || attr(n, "style").is_some_and(|s| s.to_ascii_lowercase().contains("white-space"))
        }
        Some(_) => true,
        None => false,
    }
}

fn short_ns(ns: &html5ever::Namespace) -> String {
    match &**ns {
        "http://www.w3.org/1999/xhtml" | "" => String::new(),
        "http://www.w3.org/2000/svg" => "svg:".into(),
        "http://www.w3.org/1998/Math/MathML" => "math:".into(),
        other => format!("{{{}}}", other),
    }
}

fn is_html_ws(c: char) -> bool {
    matches!(c, '\t' | '\n' | '\x0C' | '\r' | ' ')
}

fn dump(n: &Handle, sens: bool, out: &mut String, depth: usize) {
    let pad = " ".repeat(depth.min(400));
    match &n.data {
        Data::Document => out.push_str("#document\n"),
        Data::Doctype(a, b, c) => {
            out.push_str(&format!("{}<!DOCTYPE {:?} {:?} {:?}>\n", pad, a, b, c))
        }
        Data::Text(t) => out.push_str(&format!("{}{:?}\n", pad, t.borrow())),
        Data::Comment(c) => out.push_str(&format!("{}<!--{:?}-->\n", pad, c)),
        Data::Pi(a, b) => out.push_str(&format!("{}<?{:?} {:?}>\n", pad, a, b)),
        Data::Element {
            name,
            attrs,
            template,
        } => {
            out.push_str(&format!("{}<{}{}", pad, short_ns(&name.ns), name.local));
            for a in attrs.borrow().iter() {
                out.push_str(&format!(
                    " {}{}={:?}",
                    short_ns(&a.name.ns),
                    a.name.local,
                    &*a.value
                ));
            }
            out.push_str(">\n");
            if let Some(t) = template {
                out.push_str(&format!("{} #content\n", pad));
                dump_children(t, true, out, depth + 2, false);
            }
        }
    }
    let s = sens || sensitive(n);
    let is_block_parent = matches!(n.data, Data::Document) || oracle_block(n);
    dump_children(n, s, out, depth + 1, is_block_parent);
}

fn dump_children(n: &Handle, sens: bool, out: &mut String, depth: usize, block_parent: bool) {
    let kids = n.children.borrow();
    let invisible = ws_invisible_parent(n);
    for (k, c) in kids.iter().enumerate() {
        if let Data::Text(t) = &c.data {
            if !sens {
                let t = t.borrow();
                if invisible && t.chars().all(is_html_ws) {
                    continue;
                }
                // Comments and whitespace-only text are transparent: a
                // whitespace run split by them still collapses as one.
                let transparent = |x: &&Handle| match &x.data {
                    Data::Comment(_) => true,
                    Data::Text(t) => t.borrow().chars().all(is_html_ws),
                    _ => false,
                };
                let prev = kids[..k].iter().rev().find(|x| !transparent(x));
                let next = kids[k + 1..].iter().find(|x| !transparent(x));
                let drop_lead = match prev {
                    Some(p) => oracle_block(p),
                    None => block_parent,
                };
                let drop_trail = match next {
                    Some(p) => oracle_block(p),
                    None => block_parent,
                };
                let mut s: &str = &t;
                if drop_lead {
                    s = s.trim_start_matches(is_html_ws);
                }
                if drop_trail {
                    s = s.trim_end_matches(is_html_ws);
                }
                if s.is_empty() {
                    continue;
                }
                out.push_str(&format!("{}{:?}\n", " ".repeat(depth.min(400)), s));
                continue;
            }
        }
        dump(c, sens, out, depth);
    }
}

fn normalized_dom(html: &str) -> String {
    let doc = parse(html);
    let mut s = String::new();
    dump(&doc, false, &mut s, 0);
    // Break the Rc parent/child web explicitly to keep memory flat.
    s
}

fn first_diff(a: &str, b: &str) -> String {
    let al: Vec<&str> = a.lines().collect();
    for (k, (x, y)) in a.lines().zip(b.lines()).enumerate() {
        if x != y {
            let ctx = al[k.saturating_sub(6)..k].join("\n");
            return format!("line {}:\n{}\n  input : {}\n  output: {}", k + 1, ctx, x, y);
        }
    }
    format!(
        "length differs: input {} lines, output {} lines",
        a.lines().count(),
        b.lines().count()
    )
}

// ═══════════════════════════════════════════════════════════
// Invariants
// ═══════════════════════════════════════════════════════════

fn skeleton(s: &str) -> Vec<u8> {
    s.bytes().filter(|&c| !is_ws(c)).collect()
}

#[derive(Default, Clone, Copy)]
struct Stats {
    cases: u64,
    declined: u64,
    unchanged: u64,
    changed: u64,
}

impl Stats {
    fn add(&mut self, o: Stats) {
        self.cases += o.cases;
        self.declined += o.declined;
        self.unchanged += o.unchanged;
        self.changed += o.changed;
    }
}

/// Check every invariant on one input. Err = description of the defect.
fn check(input: &str, stats: &mut Stats) -> Result<(), String> {
    stats.cases += 1;
    let r = std::panic::catch_unwind(|| (format_html_report(input, 2), minify_html_report(input)));
    let (pretty, mini) = match r {
        Ok(x) => x,
        Err(_) => return Err("panic".into()),
    };
    if let Some(msg) = &pretty.message {
        stats.declined += 1;
        if pretty.text != input {
            return Err("declined but output differs from input".into());
        }
        if !msg.contains("line ") || !msg.contains("column ") {
            return Err(format!("decline message without position: {}", msg));
        }
        if mini.message.is_none() || mini.text != input {
            return Err("minify disagrees with format about declining".into());
        }
        return Ok(());
    }
    if mini.message.is_some() {
        return Err("minify declined, format did not".into());
    }
    for (label, out) in [("format", &pretty.text), ("minify", &mini.text)] {
        if skeleton(out) != skeleton(input) {
            return Err(format!("{}: non-whitespace bytes changed", label));
        }
        let again = if label == "format" {
            format_html_report(out, 2)
        } else {
            minify_html_report(out)
        };
        if again.message.is_some() {
            return Err(format!(
                "{}: output is declined on second pass: {:?}",
                label, again.message
            ));
        }
        if &again.text != out {
            return Err(format!("{}: not idempotent", label));
        }
    }
    // Pretty and minify must agree on which gaps are safe.
    if minify_html_report(&pretty.text).text != mini.text {
        return Err("minify(format(x)) != minify(x)".into());
    }
    if pretty.text == input && mini.text == input {
        stats.unchanged += 1;
        return Ok(());
    }
    stats.changed += 1;
    let a = normalized_dom(input);
    for (label, out) in [("format", &pretty.text), ("minify", &mini.text)] {
        if out == input {
            continue;
        }
        let b = normalized_dom(out);
        if a != b {
            return Err(format!("{}: DOM differs: {}", label, first_diff(&a, &b)));
        }
    }
    Ok(())
}

/// Shrink a failing input while it still fails with the same defect class.
fn minimize(input: &str) -> String {
    let class = |s: &str| -> Option<String> {
        let mut st = Stats::default();
        check(s, &mut st)
            .err()
            .map(|e| e.split(':').next().unwrap_or("").to_string())
    };
    let target = match class(input) {
        Some(c) => c,
        None => return input.to_string(),
    };
    let mut cur: Vec<char> = input.chars().collect();
    let mut chunk = cur.len() / 2;
    while chunk >= 1 {
        let mut i = 0;
        let mut progressed = false;
        while i + chunk <= cur.len() {
            let mut cand = cur.clone();
            cand.drain(i..i + chunk);
            let s: String = cand.iter().collect();
            if class(&s).as_deref() == Some(target.as_str()) {
                cur = cand;
                progressed = true;
            } else {
                i += chunk;
            }
        }
        if !progressed {
            chunk /= 2;
        }
    }
    cur.into_iter().collect()
}

// ═══════════════════════════════════════════════════════════
// Generators
// ═══════════════════════════════════════════════════════════

struct Rng(u64);

impl Rng {
    fn new(seed: u64) -> Self {
        Rng(seed ^ 0x9E37_79B9_7F4A_7C15)
    }
    fn next(&mut self) -> u64 {
        // splitmix64
        self.0 = self.0.wrapping_add(0x9E37_79B9_7F4A_7C15);
        let mut z = self.0;
        z = (z ^ (z >> 30)).wrapping_mul(0xBF58_476D_1CE4_E5B9);
        z = (z ^ (z >> 27)).wrapping_mul(0x94D0_49BB_1331_11EB);
        z ^ (z >> 31)
    }
    fn below(&mut self, n: usize) -> usize {
        (self.next() % n.max(1) as u64) as usize
    }
    fn chance(&mut self, pct: u32) -> bool {
        self.below(100) < pct as usize
    }
    fn pick<'a, T>(&mut self, xs: &'a [T]) -> &'a T {
        &xs[self.below(xs.len())]
    }
}

const BLOCK: &[&str] = &[
    "div",
    "p",
    "section",
    "article",
    "header",
    "footer",
    "nav",
    "main",
    "aside",
    "h1",
    "h2",
    "h3",
    "blockquote",
    "address",
    "figure",
    "figcaption",
    "details",
    "summary",
    "dl",
    "dt",
    "dd",
    "ul",
    "ol",
    "li",
    "form",
    "fieldset",
    "legend",
    "center",
    "dialog",
    "search",
    "hgroup",
    "menu",
];
const INLINE: &[&str] = &[
    "span", "a", "b", "i", "em", "strong", "code", "small", "s", "u", "sub", "sup", "abbr",
    "label", "q", "cite", "mark", "time", "kbd", "var", "font", "nobr", "button", "bdi", "data",
    "ruby", "rt", "rp", "my-el", "x-foo", "Span", "DIV", "output", "video", "audio", "canvas",
    "object", "marquee", "isindex", "menuitem", "datalist", "map", "slot",
];
const VOID: &[&str] = &[
    "br", "img", "input", "wbr", "hr", "embed", "source", "track", "param", "area", "col", "meta",
    "link", "base", "keygen", "image", "BR",
];
const RAW: &[&str] = &[
    "pre", "listing", "xmp", "textarea", "title", "script", "style", "noscript", "iframe",
    "noembed", "noframes",
];
const WS: &[&str] = &[
    "", "", "", " ", "\n", "\n  ", "\t", "\r\n", "\n\n", "  \n    ", "\x0C", "\r",
];
const WORDS: &[&str] = &[
    "hello",
    "world",
    "a",
    "x",
    "&amp;",
    "&nbsp;",
    "&lt;b&gt;",
    "&#x20;",
    "&notanentity;",
    "&",
    "{{ user.name }}",
    "{% if x %}",
    "{% endif %}",
    "{# c #}",
    "a < b",
    "x>y",
    "é",
    "中文",
    "🎨",
    "\u{a0}",
    "it's",
    "\"q\"",
    "[[x]]",
    "${v}",
    "@click",
    "a\0b",
    "  spaced  ",
    "\u{feff}",
];
const ATTRS: &[&str] = &[
    "id=main",
    "class=\"a b\"",
    "hidden",
    "style=\"color:red\"",
    "style=\"display:inline-block\"",
    "style=\"white-space:pre\"",
    "data-x='a > b'",
    "title=\"x>y<z\"",
    "@click=\"go()\"",
    ":prop=\"v\"",
    "[prop]=\"v\"",
    "(click)=\"f()\"",
    "*ngIf=\"ok\"",
    "#ref",
    "v-if=\"a && b\"",
    "CLASS=Up",
    "data-json='{\"a\":\"</b>\"}'",
    "disabled",
    "x=\"\"",
    "y=''",
    "z=",
    "href=\"?a=1&b=2\"",
    "a=b=c",
    "type=hidden",
    "color=red",
    "open",
    "selected",
    "encoding=\"text/html\"",
    "id=main",
    "onclick=\"if(a<b){}\"",
];
const SCRIPT_BODIES: &[&str] = &[
    "\n  var a = 1;\n  if (a<b && c>d) {}\n",
    "var s = '</div>'; var t = \"<p>\";",
    "<!-- if (x) { } -->",
    "<!-- <script> var x = '</script>'; </script> -->",
    "x = '</scriptx>'; y = '</SCRIPT'",
    "{\"a\": \"</b>\", \"c\": [1, 2]}",
    "  ",
    "",
    "document.write('<script></scr' + 'ipt>');",
    "a = 1 /* </p> */\n// <!--\n",
];
const COMMENTS: &[&str] = &[
    "<!-- c -->",
    "<!---->",
    "<!-->",
    "<!--->",
    "<!-- a --!>",
    "<!--[if IE]><p>x</p><![endif]-->",
    "<![if !IE]>",
    "<![endif]>",
    "<!-- <script> -->",
    "<?xml version=\"1.0\"?>",
    "<!ELEMENT x>",
    "<![CDATA[ x ]]>",
    "</ bogus>",
    "<!-- -- -->",
    "<!--<!-- nested -->",
];

struct Gen<'r> {
    r: &'r mut Rng,
    out: String,
    budget: usize,
    /// Extra share (percent) of `<template>`, tables and formatting tags,
    /// for the family that stresses the active-formatting/marker logic.
    tricky: usize,
}

impl Gen<'_> {
    fn ws(&mut self) {
        let w = *self.r.pick(WS);
        self.out.push_str(w);
    }
    fn attrs(&mut self) {
        let n = self.r.below(4);
        for _ in 0..n {
            self.out.push(' ');
            let a = *self.r.pick(ATTRS);
            self.out.push_str(a);
        }
        if self.r.chance(5) {
            self.out.push_str("  ");
        }
    }
    fn open(&mut self, name: &str) {
        self.out.push('<');
        self.out.push_str(name);
        self.attrs();
        if self.r.chance(4) {
            self.out.push('/');
        }
        self.out.push('>');
    }
    fn close(&mut self, name: &str) {
        self.out.push_str("</");
        self.out.push_str(name);
        if self.r.chance(3) {
            self.out.push(' ');
        }
        self.out.push('>');
    }
    fn text(&mut self) {
        let n = 1 + self.r.below(3);
        for k in 0..n {
            if k > 0 {
                self.out.push(' ');
            }
            let w = *self.r.pick(WORDS);
            self.out.push_str(w);
        }
    }
    fn raw(&mut self, name: &str) {
        self.open(name);
        match name {
            "script" => {
                let s = *self.r.pick(SCRIPT_BODIES);
                self.out.push_str(s);
            }
            "style" => self.out.push_str(if self.r.chance(50) {
                "\n p > a { x: '</p>' }\n"
            } else {
                "a{}"
            }),
            "pre" | "listing" => {
                self.out.push_str("\n  pre  text\n");
                if self.r.chance(50) {
                    self.out.push_str("<b> bold </b>\n");
                }
            }
            _ => {
                if self.r.chance(50) {
                    self.out.push('\n');
                }
                self.text();
                if self.r.chance(30) {
                    self.out.push_str(" </p> <b>");
                }
            }
        }
        self.close(name);
    }
    fn table(&mut self, depth: usize) {
        self.open("table");
        self.ws();
        if self.r.chance(20) {
            self.out.push_str("<caption>cap</caption>");
            self.ws();
        }
        if self.r.chance(15) {
            self.out.push_str("<colgroup><col><col span=2></colgroup>");
            self.ws();
        }
        let explicit_body = self.r.chance(50);
        if explicit_body {
            let sec = *self.r.pick(&["tbody", "thead", "tfoot"]);
            self.open(sec);
            self.ws();
        }
        for _ in 0..1 + self.r.below(3) {
            self.open("tr");
            self.ws();
            for _ in 0..1 + self.r.below(3) {
                let cell = *self.r.pick(&["td", "th"]);
                self.open(cell);
                self.ws();
                self.children(depth + 1);
                if self.r.chance(75) {
                    self.close(cell);
                }
                self.ws();
            }
            if self.r.chance(70) {
                self.close("tr");
            }
            self.ws();
        }
        if explicit_body && self.r.chance(70) {
            self.out.push_str("</tbody>");
        }
        self.ws();
        self.close("table");
    }
    fn svg(&mut self) {
        let root = *self.r.pick(&["svg", "math", "SVG"]);
        self.open(root);
        self.ws();
        let kids: &[&str] = if root == "math" {
            &["mi", "mo", "mrow", "mn", "annotation-xml", "mglyph"]
        } else {
            &[
                "g",
                "path",
                "circle",
                "text",
                "title",
                "foreignObject",
                "desc",
                "style",
                "script",
                "rect",
            ]
        };
        for _ in 0..self.r.below(4) {
            let k = *self.r.pick(kids);
            if self.r.chance(40) {
                self.out.push('<');
                self.out.push_str(k);
                self.out.push_str(" d=\"M0 0\"/>");
            } else {
                self.out.push('<');
                self.out.push_str(k);
                self.out.push('>');
                if self.r.chance(50) {
                    self.text();
                }
                if self.r.chance(20) {
                    self.out.push_str("<![CDATA[ a < b ]]>");
                }
                if self.r.chance(10) {
                    self.out.push_str("<b>x</b>");
                }
                self.close(k);
            }
            self.ws();
        }
        self.close(root);
    }
    fn select(&mut self) {
        self.open("select");
        self.ws();
        for _ in 0..self.r.below(4) {
            if self.r.chance(20) {
                self.out.push_str("<optgroup label=g>");
                self.ws();
            }
            self.open("option");
            self.text();
            if self.r.chance(60) {
                self.close("option");
            }
            self.ws();
        }
        self.close("select");
    }
    fn element(&mut self, depth: usize) {
        if self.budget == 0 {
            self.text();
            return;
        }
        self.budget -= 1;
        let roll = self.r.below(100);
        if depth > 6 || roll < 10 {
            let v = *self.r.pick(VOID);
            self.open(v);
            return;
        }
        if roll < 18 {
            let n = *self.r.pick(RAW);
            self.raw(n);
            return;
        }
        if roll < 23 {
            self.table(depth);
            return;
        }
        if roll < 27 {
            self.svg();
            return;
        }
        if roll < 30 {
            self.select();
            return;
        }
        if roll < 33 {
            let c = *self.r.pick(COMMENTS);
            self.out.push_str(c);
            return;
        }
        if roll < 35 + self.tricky {
            if self.tricky > 0 && self.r.chance(50) {
                let f = *self.r.pick(&[
                    "b", "a", "u", "font", "nobr", "object", "marquee", "td", "select",
                ]);
                self.open(f);
                self.children(depth + 1);
                if self.r.chance(60) {
                    self.close(f);
                }
                return;
            }
            self.out.push_str("<template>");
            self.children(depth + 1);
            self.out.push_str("</template>");
            return;
        }
        let name = if roll < 65 {
            *self.r.pick(BLOCK)
        } else {
            *self.r.pick(INLINE)
        };
        self.open(name);
        self.children(depth + 1);
        let optional = matches!(name, "li" | "p" | "dt" | "dd" | "option");
        if !optional || self.r.chance(60) {
            self.close(name);
        }
    }
    fn children(&mut self, depth: usize) {
        let n = self.r.below(5);
        for _ in 0..n {
            self.ws();
            if self.r.chance(30) {
                self.text();
            } else {
                self.element(depth);
            }
        }
        self.ws();
    }
    fn document(&mut self) {
        let shape = self.r.below(4);
        if shape == 0 {
            self.children(0);
            return;
        }
        if self.r.chance(80) {
            self.out.push_str(self.r.pick(&[
                "<!DOCTYPE html>",
                "<!doctype html>",
                "<!DOCTYPE html PUBLIC \"-//W3C//DTD XHTML 1.0 Strict//EN\" \"x\">",
            ]));
            self.ws();
        }
        if self.r.chance(80) {
            self.out.push_str("<html lang=en>");
            self.ws();
        }
        if self.r.chance(70) {
            self.out.push_str("<head>");
            self.ws();
            self.out.push_str("<meta charset=\"utf-8\">");
            self.ws();
            self.out.push_str("<title>T &amp; t</title>");
            self.ws();
            if self.r.chance(30) {
                self.out.push_str("<style>body{}</style>");
                self.ws();
            }
            if self.r.chance(30) {
                self.out.push_str("<script src=a.js></script>");
                self.ws();
            }
            if self.r.chance(80) {
                self.out.push_str("</head>");
            }
            self.ws();
        }
        if self.r.chance(80) {
            self.out.push_str("<body class=x>");
        }
        self.children(0);
        if self.r.chance(70) {
            self.out.push_str("</body>");
            self.ws();
        }
        if self.r.chance(20) {
            self.out.push_str("<!-- after body -->");
            self.ws();
        }
        if self.r.chance(70) {
            self.out.push_str("</html>");
        }
        if self.r.chance(10) {
            self.ws();
            self.out.push_str("<script>late()</script>");
        }
        if self.r.chance(50) {
            self.out.push('\n');
        }
    }
}

fn gen_doc(r: &mut Rng) -> String {
    gen_doc_with(r, 0)
}

fn gen_doc_with(r: &mut Rng, tricky: usize) -> String {
    let budget = 4 + r.below(40);
    let mut g = Gen {
        r,
        out: String::new(),
        budget,
        tricky,
    };
    g.document();
    g.out
}

fn char_boundary(s: &str, mut i: usize) -> usize {
    i = i.min(s.len());
    while !s.is_char_boundary(i) {
        i -= 1;
    }
    i
}

/// Apply ONE corruption to a (mostly valid) document.
fn mutate(r: &mut Rng, s: &str) -> String {
    if s.is_empty() {
        return s.to_string();
    }
    let positions = |pat: &str| -> Vec<usize> { s.match_indices(pat).map(|(i, _)| i).collect() };
    match r.below(9) {
        0 => {
            // Drop a closing tag.
            let ends = positions("</");
            if ends.is_empty() {
                return s.to_string();
            }
            let p = *r.pick(&ends);
            let e = s[p..].find('>').map_or(s.len(), |e| p + e + 1);
            format!("{}{}", &s[..p], &s[e..])
        }
        1 => {
            // Drop a '>'.
            let gts = positions(">");
            if gts.is_empty() {
                return s.to_string();
            }
            let p = *r.pick(&gts);
            format!("{}{}", &s[..p], &s[p + 1..])
        }
        2 => {
            // Unbalanced quote inside a tag.
            let lts = positions("<");
            if lts.is_empty() {
                return s.to_string();
            }
            let p = *r.pick(&lts);
            let q = char_boundary(s, p + 1 + r.below(8));
            format!(
                "{}{}{}",
                &s[..q],
                if r.chance(50) { "\"" } else { "'" },
                &s[q..]
            )
        }
        3 => {
            // Stray closing tag.
            let p = char_boundary(s, r.below(s.len() + 1));
            let t = *r.pick(&[
                "</div>",
                "</p>",
                "</b>",
                "</li>",
                "</td>",
                "</span>",
                "</table>",
                "</select>",
                "</body>",
                "</html>",
                "</br>",
                "</>",
            ]);
            format!("{}{}{}", &s[..p], t, &s[p..])
        }
        4 => {
            // Mis-nest: insert an inline open/close pair straddling a boundary.
            let p = char_boundary(s, r.below(s.len() + 1));
            let q = char_boundary(s, p + r.below(s.len() - p + 1));
            let t = *r.pick(&["b", "i", "a", "em", "span", "font", "u"]);
            format!("{}<{}>{}</{}>{}", &s[..p], t, &s[p..q], t, &s[q..]).replacen(
                &format!("</{}>", t),
                "",
                if r.chance(30) { 1 } else { 0 },
            )
        }
        5 => {
            // Truncate.
            let p = char_boundary(s, r.below(s.len() + 1));
            s[..p].to_string()
        }
        6 => {
            // Insert a random fragment.
            let p = char_boundary(s, r.below(s.len() + 1));
            let f = *r.pick(SOUP);
            format!("{}{}{}", &s[..p], f, &s[p..])
        }
        7 => {
            // Delete a range.
            let p = char_boundary(s, r.below(s.len() + 1));
            let q = char_boundary(s, p + r.below(12));
            format!("{}{}", &s[..p], &s[q..])
        }
        _ => {
            // Duplicate a range.
            let p = char_boundary(s, r.below(s.len() + 1));
            let q = char_boundary(s, p + r.below(30));
            format!("{}{}{}", &s[..q], &s[p..q], &s[q..])
        }
    }
}

const SOUP: &[&str] = &[
    "<",
    ">",
    "</",
    "/>",
    "<!--",
    "-->",
    "--!>",
    "<!",
    "<?",
    "?>",
    "<![CDATA[",
    "]]>",
    "<!DOCTYPE html>",
    "div",
    "p",
    "li",
    "b",
    "i",
    "a",
    "pre",
    "script",
    "style",
    "textarea",
    "title",
    "table",
    "tr",
    "td",
    "svg",
    "math",
    "template",
    "select",
    "option",
    "=",
    "\"",
    "'",
    " ",
    "\n",
    "\r\n",
    "\t",
    "x",
    "é",
    "&amp;",
    "&",
    "\0",
    "{{",
    "}}",
    "<p>",
    "</p>",
    "<div>",
    "</div>",
    "<b>",
    "</b>",
    "<i>",
    "</i>",
    "<li>",
    "</li>",
    "<br>",
    "<script>",
    "</script>",
    "<pre>",
    "</pre>",
    "<table>",
    "<td>",
    "<tr>",
    "</table>",
    "<span>",
    "</span>",
    "<ul>",
    "</ul>",
    "<svg>",
    "</svg>",
    "<select>",
    "<option>",
    "<textarea>",
    "</textarea>",
    "<html>",
    "<body>",
    "</body>",
    "<head>",
    "</head>",
    "<a href=x>",
    "</a>",
    "<hr>",
    "<h1>",
    "</h2>",
    "<!-- c -->",
    "<template>",
    "</template>",
    "<title>",
    "</title>",
    "<style>",
    "</style>",
    "<form>",
    "</form>",
    "<button>",
    "<nobr>",
    "<dd>",
    "<dt>",
    "<caption>",
    "<col>",
    "<colgroup>",
    "<tbody>",
    "<plaintext>",
    "<xmp>",
    "<listing>",
    "<iframe>",
    "<noscript>",
    "<math>",
    "<mi>",
    "<foreignObject>",
    "<desc>",
    "<image>",
    "<frameset>",
    "<rt>",
    "<rp>",
    "<ruby>",
    "\x0C",
    "\r",
    "<p hidden>",
    "<div style=x>",
    "<div/>",
    "<DIV>",
    "</DIV >",
];

fn gen_soup(r: &mut Rng) -> String {
    let n = r.below(40);
    let mut s = String::new();
    for _ in 0..n {
        s.push_str(r.pick(SOUP));
    }
    s
}

fn gen_case(r: &mut Rng, family: usize) -> String {
    match family {
        0 => gen_doc(r),
        1 => {
            let d = gen_doc(r);
            mutate(r, &d)
        }
        2 => gen_soup(r),
        _ => {
            let d = gen_doc_with(r, 25);
            if r.chance(50) {
                mutate(r, &d)
            } else {
                d
            }
        }
    }
}

fn env_u64(name: &str, default: u64) -> u64 {
    std::env::var(name)
        .ok()
        .and_then(|v| v.parse().ok())
        .unwrap_or(default)
}

/// Run `cases` cases of each family; panics with seed and minimized input
/// on the first defect.
fn run_fuzz(cases: u64, seed: u64, threads: usize) -> Stats {
    use rayon::prelude::*;
    let pool = rayon::ThreadPoolBuilder::new()
        .num_threads(threads)
        .build()
        .unwrap();
    let chunks: u64 = (threads as u64 * 8).max(1);
    let mut total = Stats::default();
    for family in 0..4usize {
        let t0 = Instant::now();
        let per = cases.div_ceil(chunks);
        let results: Vec<Result<Stats, String>> = pool.install(|| {
            (0..chunks)
                .into_par_iter()
                .map(|c| {
                    let mut st = Stats::default();
                    let lo = c * per;
                    let hi = ((c + 1) * per).min(cases);
                    for n in lo..hi {
                        let case_seed = seed
                            .wrapping_mul(0x100_0000_01B3)
                            .wrapping_add((family as u64) << 56)
                            .wrapping_add(n);
                        let mut r = Rng::new(case_seed);
                        let input = gen_case(&mut r, family);
                        if let Err(e) = check(&input, &mut st) {
                            let min = minimize(&input);
                            return Err(format!(
                                "family {} case {} (seed {:#x}): {}\nminimized input: {:?}\nfull input: {:?}\nminimized error: {:?}",
                                family,
                                n,
                                case_seed,
                                e,
                                min,
                                input,
                                check(&min, &mut Stats::default()).err()
                            ));
                        }
                    }
                    Ok(st)
                })
                .collect()
        });
        let mut fam = Stats::default();
        for r in results {
            match r {
                Ok(s) => fam.add(s),
                Err(e) => panic!("{}", e),
            }
        }
        eprintln!(
            "html fuzz family {}: {} cases in {:.1?} — changed {}, unchanged {}, declined {}",
            family,
            fam.cases,
            t0.elapsed(),
            fam.changed,
            fam.unchanged,
            fam.declined
        );
        total.add(fam);
    }
    total
}

#[test]
fn fuzz_generated_documents() {
    let cases = env_u64("NP_HTML_FUZZ_CASES", 20_000);
    let seed = env_u64("NP_HTML_FUZZ_SEED", 0x5EED_0001);
    let threads = env_u64("NP_HTML_FUZZ_THREADS", 4) as usize;
    let st = run_fuzz(cases, seed, threads);
    // Vacuity guard: the clean-document family must mostly get formatted,
    // not declined, or the invariants prove nothing.
    assert!(
        st.changed > st.cases / 10,
        "too few documents formatted: {:?}",
        (st.changed, st.cases)
    );
}

// ═══════════════════════════════════════════════════════════
// Hand-built real-world corpus
// ═══════════════════════════════════════════════════════════

const LANDING: &str = r#"<!DOCTYPE html>
<html lang="en">
<head>
<meta charset="utf-8"><meta name="viewport" content="width=device-width, initial-scale=1">
<title>Acme &mdash; Build faster</title>
<link rel="stylesheet" href="/css/site.css">
<style>
  .hero { padding: 4rem 0 }
  a > span::after { content: "</a>" }
</style>
<script async src="https://example.com/gtag.js"></script>
<script>
  window.dataLayer = window.dataLayer || [];
  function gtag(){dataLayer.push(arguments);} if (a < b && b > c) { gtag('js', new Date()); }
</script>
</head>
<body class="home">
<header class="site-header"><nav aria-label="Main"><ul class="menu"><li><a href="/">Home</a></li><li><a href="/pricing">Pricing</a></li><li class="active"><a href="/docs">Docs</a></li></ul></nav></header>
<main>
<section class="hero"><h1>Build <em>faster</em> with Acme</h1><p>Ship in <strong>minutes</strong>, not days. <a class="btn" href="/start">Get started</a> <a class="btn btn-ghost" href="/demo">Watch demo</a></p></section>
<section id="features"><div class="grid"><div class="card"><img src="a.svg" alt="A"><h3>Fast</h3><p>Really&nbsp;fast.</p></div><div class="card"><img src="b.svg" alt="B"><h3>Safe</h3><p>No surprises.<br>Ever.</p></div></div></section>
<form action="/signup" method="post"><label for="email">Email</label> <input id="email" name="email" type="email" required> <button type="submit">Sign up</button></form>
</main>
<footer><p>&copy; 2026 Acme Inc. &middot; <a href="/privacy">Privacy</a></p></footer>
<!--[if lt IE 9]><script src="html5shiv.js"></script><![endif]-->
</body>
</html>
"#;

const TABLES: &str = r#"<table class="data">
<caption>Quarterly results</caption>
<colgroup><col><col span="2"></colgroup>
<thead><tr><th>Region<th>Q1<th>Q2
<tbody>
<tr><td>North<td>1,200<td>1,350
<tr><td>South<td><b>980</b><td>1,010
<tr><td colspan=3><table><tr><td>nested</td></tr></table></td></tr>
<tfoot><tr><td>Total<td>2,180<td>2,360</td></tr></tfoot>
</table>"#;

const INLINE_SVG: &str = r#"<div class="icon-row"><svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 24 24" width="24" height="24"><title>Home</title><path d="M3 12l9-9 9 9"/><g fill="none"><circle cx="12" cy="12" r="3"/></g><text x="2" y="20">  label  </text></svg> <span>Home</span></div>
<p>Math: <math><mi>x</mi><mo>=</mo><mn>2</mn></math>.</p>"#;

const TRICKY_SCRIPT: &str = r#"<div id="app">
<script>
  var html = "<div>" + "</div>"; // closing tags in strings
  var c = "<!--"; var d = "-->";
  if (x < 10 && y > 2) { document.write("<script>a()</scr" + "ipt>"); }
</script>
<script type="application/ld+json">{"@context":"https://schema.org","name":"</p>"}</script>
<script><!-- legacy
  document.write("<script>inner()<\/script>");
//--></script>
<p>after scripts</p>
</div>"#;

const TEMPLATE_FILE: &str = r#"{% extends "base.html" %}
{% block content %}
<ul class="items">
{% for item in items %}
  <li class="{{ item.cls }}">{{ item.name|title }}{% if item.new %} <span class="badge">new</span>{% endif %}</li>
{% endfor %}
</ul>
<div v-if="ready" @click="go(1)" :class="{a: b > c}"><input [(ngModel)]="name" #box (keyup)="0"><p>{{ greeting }}</p></div>
{% endblock %}"#;

const EMAIL: &str = r##"<table width="100%" cellpadding="0" cellspacing="0" border="0" bgcolor="#f4f4f4"><tr><td align="center"><table width="600" style="max-width:600px"><tr><td style="padding:20px;font-family:Arial"><font face="Arial" size="2">Hi <b>Sam</b>,</font><br><br>Your order <a href="https://x.example/o?id=1&amp;t=2">#1001</a> shipped.</td></tr><tr><td><img src="logo.png" width="120" alt=""></td></tr></table></td></tr></table>"##;

const MINIFIED: &str = r##"<!doctype html><html><head><meta charset=utf-8><title>M</title><link rel=stylesheet href=s.css></head><body><div class=wrap><header><h1>Title</h1><p>Lead <a href=#>link</a>.</p></header><ul><li>one<li>two<li>three</ul><dl><dt>T<dd>D<dt>U<dd>E</dl><p>para one<p>para two<pre>  keep   this
  exactly</pre><select name=s><option value=1>One<option value=2 selected>Two</select><textarea>  raw
</textarea></div></body></html>"##;

fn corpus() -> Vec<(&'static str, &'static str)> {
    vec![
        ("landing page", LANDING),
        ("table-heavy page", TABLES),
        ("inline SVG and MathML", INLINE_SVG),
        ("script with </ and <!--", TRICKY_SCRIPT),
        ("Jinja/Vue/Angular template", TEMPLATE_FILE),
        ("email table layout", EMAIL),
        ("minified one-liner", MINIFIED),
    ]
}

#[test]
fn real_world_corpus() {
    for (label, doc) in corpus() {
        let mut st = Stats::default();
        if let Err(e) = check(doc, &mut st) {
            panic!("{}: {}\nminimized: {:?}", label, e, minimize(doc));
        }
        let o = format_html_report(doc, 2);
        eprintln!(
            "corpus {:<28} {}",
            label,
            match &o.message {
                Some(m) => format!("DECLINED: {}", m),
                None if o.text == doc => "unchanged".to_string(),
                None => format!(
                    "formatted ({} -> {} lines)",
                    doc.lines().count(),
                    o.text.lines().count()
                ),
            }
        );
    }
    // These shapes are all valid HTML the formatter must handle, not decline.
    for (label, doc) in corpus() {
        assert!(
            format_html_report(doc, 2).message.is_none(),
            "{} was declined: {:?}",
            label,
            format_html_report(doc, 2).message
        );
    }
}

#[test]
fn docs_html_corpus() {
    let dir = std::path::Path::new(env!("CARGO_MANIFEST_DIR")).join("../docs");
    let Ok(rd) = std::fs::read_dir(&dir) else {
        eprintln!("docs/ not found; skipped");
        return;
    };
    let mut files: Vec<_> = rd
        .filter_map(|e| e.ok())
        .map(|e| e.path())
        .filter(|p| p.extension().is_some_and(|x| x == "html"))
        .collect();
    files.sort();
    for p in files {
        let doc = std::fs::read_to_string(&p).unwrap();
        let mut st = Stats::default();
        let name = p.file_name().unwrap().to_string_lossy().to_string();
        if let Err(e) = check(&doc, &mut st) {
            panic!("{}: {}", name, e);
        }
        let o = format_html_report(&doc, 2);
        eprintln!(
            "docs/{:<16} {}",
            name,
            match &o.message {
                Some(m) => format!("DECLINED: {}", m),
                None => format!(
                    "ok, {} of {} lines changed",
                    o.text
                        .lines()
                        .zip(doc.lines())
                        .filter(|(a, b)| a != b)
                        .count()
                        + o.text.lines().count().abs_diff(doc.lines().count()),
                    doc.lines().count()
                ),
            }
        );
    }
}

// ═══════════════════════════════════════════════════════════
// Robustness and performance
// ═══════════════════════════════════════════════════════════

fn timed(label: &str, input: &str, limit: Duration) {
    let t = Instant::now();
    let o = format_html_report(input, 2);
    let m = minify_html_report(input);
    let el = t.elapsed();
    eprintln!(
        "{:<36} {:>9} bytes in {:>9.1?} ({})",
        label,
        input.len(),
        el,
        o.message.as_deref().map_or("formatted", |_| "declined")
    );
    assert!(el < limit, "{} took {:?} (limit {:?})", label, el, limit);
    if o.message.is_some() {
        assert_eq!(o.text, input);
        assert_eq!(m.text, input);
    } else {
        assert_eq!(skeleton(&o.text), skeleton(input));
    }
}

#[test]
fn pathological_inputs_are_fast_and_safe() {
    let mb = 1 << 20;
    timed("empty", "", Duration::from_secs(1));
    timed(
        "BOM + CRLF",
        "\u{feff}<div>\r\n<p>x</p>\r\n</div>\r\n",
        Duration::from_secs(1),
    );
    timed("NULs", "<div>\0<p\0>\0</p>\0</div>", Duration::from_secs(1));
    let one_line: String = "<div class=row><span>cell</span><b>x</b></div>".repeat(6 * mb / 44);
    timed("6 MB single line", &one_line, Duration::from_secs(5));
    let blocks: String = "<p>x</p>".repeat(8 * mb / 8);
    timed("8 MB of <p>", &blocks, Duration::from_secs(5));
    let attr = format!(
        "<div data-x=\"{}\"><p title={}>t</p></div>",
        "a>".repeat(mb),
        "b".repeat(mb)
    );
    timed("2 MB attribute values", &attr, Duration::from_secs(2));
    let deep = "<div>".repeat(20_000) + &"</div>".repeat(20_000);
    timed("20k nesting", &deep, Duration::from_secs(2));
    let deep_ok = "<div>".repeat(250) + &"<li>".repeat(400_000);
    timed("li walk under 250 divs", &deep_ok, Duration::from_secs(10));
    let comments = "<!--".repeat(mb);
    timed("1M unterminated <!--", &comments, Duration::from_secs(2));
    let lts = "<".repeat(4 * mb);
    timed("4M '<'", &lts, Duration::from_secs(2));
    let scripts = format!("<script>{}</script>", "<!--<script>".repeat(mb / 4));
    timed("escaped script states", &scripts, Duration::from_secs(2));
    let unterminated_attr = format!("<div title=\"{}", "x".repeat(4 * mb));
    timed(
        "unterminated quote 4 MB",
        &unterminated_attr,
        Duration::from_secs(2),
    );
    let mut garbage = Vec::new();
    let mut r = Rng::new(7);
    for _ in 0..2 * mb {
        garbage.push((r.next() & 0xFF) as u8);
    }
    let garbage = String::from_utf8_lossy(&garbage).into_owned();
    timed(
        "2 MB random bytes (lossy UTF-8)",
        &garbage,
        Duration::from_secs(2),
    );

    // Linearity: doubling the input must not quadruple the time.
    let unit = "<ul><li><a href=x>a</a></li><li>b</li></ul><p>t <b>u</b></p>\n";
    let small = unit.repeat(20_000);
    let big = unit.repeat(80_000);
    let t1 = Instant::now();
    let _ = format_html_report(&small, 2);
    let d1 = t1.elapsed();
    let t2 = Instant::now();
    let _ = format_html_report(&big, 2);
    let d2 = t2.elapsed();
    eprintln!(
        "linearity: 4x input took {:.1}x time",
        d2.as_secs_f64() / d1.as_secs_f64().max(1e-9)
    );
    assert!(
        d2 < d1 * 12 + Duration::from_millis(50),
        "superlinear: {:?} vs {:?}",
        d1,
        d2
    );
}

#[test]
fn random_bytes_never_panic() {
    let mut r = Rng::new(env_u64("NP_HTML_FUZZ_SEED", 0x5EED_0001) ^ 0xB17E);
    let n = env_u64("NP_HTML_FUZZ_CASES", 20_000) / 4;
    let mut st = Stats::default();
    for _ in 0..n {
        let len = r.below(200);
        let bytes: Vec<u8> = (0..len)
            .map(|_| {
                let c = r.below(16);
                match c {
                    0 => b'<',
                    1 => b'>',
                    2 => b'/',
                    3 => b'!',
                    4 => b'-',
                    5 => b' ',
                    6 => b'\n',
                    7 => b'"',
                    8 => b'p',
                    9 => b'd',
                    10 => b'=',
                    11 => 0,
                    12 => 0xC3,
                    _ => (r.next() & 0xFF) as u8,
                }
            })
            .collect();
        let s = String::from_utf8_lossy(&bytes).into_owned();
        if let Err(e) = check(&s, &mut st) {
            panic!("{}\nminimized: {:?}\ninput: {:?}", e, minimize(&s), s);
        }
    }
}

// ═══════════════════════════════════════════════════════════
// Fixed regression cases
// ═══════════════════════════════════════════════════════════

/// Each input broke the pre-rewrite formatter (it put every token on its
/// own line): the DOM a browser builds from its output differed from the
/// input's. Red-state verified against that implementation.
const OLD_FORMATTER_DEFECTS: &[(&str, &str)] = &[
    (
        "space added between inline elements",
        "<p><b>a</b><i>b</i></p>",
    ),
    (
        "space removed around inline text",
        "<p>Hello <b>world</b> again</p>",
    ),
    ("pre re-indented", "<div><pre>  a\n    b</pre></div>"),
    (
        "textarea re-indented",
        "<form><textarea>  x\n  y</textarea></form>",
    ),
    (
        "script `a<b` read as a tag",
        "<div><script>if (a<b) x();</script></div>",
    ),
    (
        "script string `</div>` read as a tag",
        "<div><script>var s = '</div>';</script></div>",
    ),
    ("comment cut at an inner `>`", "<div><!-- a > b --></div>"),
    (
        "attribute value cut at `>`",
        "<div title=\"a>b\"><p>x</p></div>",
    ),
    (
        "conditional comment split",
        "<div><!--[if IE]><p>x</p><![endif]--></div>",
    ),
    (
        "title text trimmed",
        "<html><head><title> A  B </title></head></html>",
    ),
    ("lone `<` in text", "<p>a < b</p>"),
    (
        "whitespace re-opens mis-nested <b>",
        "<div><p><b>x</p><p>y</p></div>",
    ),
    (
        "leading space in inline trimmed",
        "<div><span> a</span></div>",
    ),
];

/// Defects the fuzzer found in intermediate versions of the rewrite, each
/// minimized; kept so they stay fixed.
const REWRITE_DEFECTS: &[(&str, &str)] = &[
    ("empty gap inside a raw-text element", "<meta><title></title>"),
    ("empty gap inside a raw-text element (2)", "<!><script></script>"),
    ("whitespace before </body> merges with text after it", "<code></code>\n</body>>"),
    ("implicit cell close inside a non-block", "<table><th><h><td>"),
    (
        "comment-transparent neighbour is a dirty tail",
        "<script></script><u><data><select></select></data></u><!></html>>",
    ),
    (
        "second </body> re-cleaned the tail",
        "<fieldset><legend><blockquote></blockquote></legend></fieldset><my-el></my-el></body>></body>",
    ),
    ("styled <body> treated as a block", "<!><body style=\"\"></html>"),
    (
        "styled <body> before </html>",
        "<meta><title></title><style></style><script></script><body style></body></html>",
    ),
    (
        "<noscript> after </head> goes to the body",
        "<!><title></title><style></style></head><noscript></noscript><h>",
    ),
    (
        "<search> special-ness differs between parsers",
        "<dd><strong><h3><address></address></h3><search><dt><l>",
    ),
    ("<rt> closes optional end tags", "<ruby><dt><style></style><rt></dt></\n"),
    ("formatting element leaks out of <template>", "><template><s><object></template><div>"),
    ("template marker leak (td)", "<table><td><strong><template><td></template></table><y>"),
    (
        "template marker leak (ignored </td>)",
        "<table><td><strong><template><td><table></td></template></table><y>",
    ),
    (
        "template marker leak exposes a formatting element below a marker",
        "<marquee><a><object><template><object></template></object></a></marquee><p>x</p>",
    ),
    (
        "<script> ignored in a template that starts with <col>",
        "<cite><template><col><script><!--</script></template></cite></html>",
    ),
    (
        "cell closed over an open <object> leaves a marker behind",
        "<table><th><small><table><th><object></table></table>\r</html>",
    ),
    (
        "stray </a> in a template cancelled an open <u>",
        "><template><u></a><marquee></template><summary>",
    ),
    (
        "</u> ignored inside a <select> in a template",
        "><template><u><select></u><marquee></template><summary>",
    ),
    (
        "<meta> before <col> does not pick the template mode",
        "<template><meta><col><script><!--</script></template><u>",
    ),
];

/// `NP_HTML_FUZZ_REASONS=1`: print which decline messages the clean
/// generator triggers most (to spot over-declining). No assertions.
#[test]
fn decline_reason_histogram() {
    if std::env::var("NP_HTML_FUZZ_REASONS").is_err() {
        return;
    }
    let mut counts: std::collections::BTreeMap<String, u64> = Default::default();
    let n = env_u64("NP_HTML_FUZZ_CASES", 20_000);
    for k in 0..n {
        let mut r = Rng::new(k);
        let doc = gen_doc(&mut r);
        if let Some(m) = format_html_report(&doc, 2).message {
            // Strip the position and anything quoted after the first tag.
            let reason = m.split(": ").skip(1).collect::<Vec<_>>().join(": ");
            let key: String = reason
                .split_whitespace()
                .filter(|w| {
                    !w.starts_with("line")
                        && !w.starts_with("column")
                        && !w.chars().next().is_some_and(|c| c.is_ascii_digit())
                })
                .take(9)
                .collect::<Vec<_>>()
                .join(" ");
            *counts.entry(key).or_default() += 1;
        }
    }
    let mut v: Vec<_> = counts.into_iter().collect();
    v.sort_by_key(|a| std::cmp::Reverse(a.1));
    for (k, c) in v.iter().take(40) {
        eprintln!("{:>8}  {}", c, k);
    }
}

#[test]
fn regression_cases() {
    for (label, input) in OLD_FORMATTER_DEFECTS.iter().chain(REWRITE_DEFECTS) {
        let mut st = Stats::default();
        if let Err(e) = check(input, &mut st) {
            panic!("{}: {}\ninput: {:?}", label, e, input);
        }
    }
    // The old-formatter shapes are valid HTML except the mis-nested one:
    // they must be handled, not dodged by declining.
    for (label, input) in OLD_FORMATTER_DEFECTS {
        let declined = format_html_report(input, 2).message.is_some();
        assert_eq!(declined, label.contains("mis-nested"), "{}", label);
    }
}
