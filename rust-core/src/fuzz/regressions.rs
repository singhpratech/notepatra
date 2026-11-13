// SPDX-License-Identifier: GPL-3.0-or-later

//! Minimized regression cases, one per defect class the fuzz harness found
//! in the pre-v0.1.133 JSON tools and bracket fixer. Every case was
//! red-state verified: it FAILS against the original modules and passes
//! against the rewrite.

use crate::bracket_fix::{check_brackets, fix_brackets};
use crate::json_fmt::{fix_json_with_report, format_json, json_parse_error, minify_json};

fn mini(s: &str) -> String {
    // whitespace outside strings removed, for layout-independent compares
    let mut out = String::new();
    let mut in_s = false;
    let mut esc = false;
    for c in s.chars() {
        if in_s {
            out.push(c);
            if esc {
                esc = false;
            } else if c == '\\' {
                esc = true;
            } else if c == '"' {
                in_s = false;
            }
        } else if c == '"' {
            in_s = true;
            out.push(c);
        } else if !c.is_whitespace() {
            out.push(c);
        }
    }
    out
}

#[test]
fn j01_format_keeps_negative_zero() {
    assert_eq!(mini(&format_json("-0", 2)), "-0");
}
#[test]
fn j02_format_keeps_exponent_text() {
    assert_eq!(mini(&format_json("[1E+2]", 2)), "[1E+2]");
}
#[test]
fn j03_format_keeps_big_integer() {
    let s = "[123456789012345678901234567890]";
    assert_eq!(mini(&format_json(s, 2)), s);
}
#[test]
fn j04_format_keeps_duplicate_keys() {
    let s = r#"{"a":1,"a":2}"#;
    assert_eq!(mini(&format_json(s, 2)), s);
}
#[test]
fn j05_format_keeps_escapes() {
    let s = r#"["é\/"]"#;
    assert_eq!(mini(&format_json(s, 2)), s);
}
#[test]
fn j06_format_never_invents_closers() {
    assert_eq!(format_json("[1,2", 2), "[1,2");
}
#[test]
fn j07_minify_never_strips_spaces_inside_strings() {
    let s = "{\"a\": \"x y\"";
    assert_eq!(minify_json(s), s);
}
#[test]
fn j08_bom_is_valid_and_kept() {
    assert_eq!(json_parse_error("\u{FEFF}[]"), "");
    assert!(format_json("\u{FEFF}[1]", 2).starts_with('\u{FEFF}'));
}
#[test]
fn j09_error_position_points_at_the_fault() {
    let e = json_parse_error("[\"a\nb\"]");
    assert!(e.contains("line 1, column 4"), "{e}");
}
#[test]
fn j10_error_column_counts_chars_not_bytes() {
    let e = json_parse_error("{\"é\":1 @}");
    assert!(e.contains("line 1, column 8"), "{e}");
}
#[test]
fn j11_no_trailing_whitespace_in_format_output() {
    let out = format_json("\r\n  [1e400\n\n,0,[]\n   ,true\n    ]", 2);
    assert!(!out.lines().any(|l| l.ends_with(' ')), "{out:?}");
}
#[test]
fn r01_repair_never_edits_inside_strings_trailing_comma() {
    let (out, _) = fix_json_with_report("{\"a\": \"1,]\", }");
    assert!(out.contains("\"1,]\""), "{out}");
}
#[test]
fn r02_repair_never_edits_inside_strings_keys() {
    let (out, _) = fix_json_with_report("{k: \"x, b: y\"}");
    assert!(out.contains("\"x, b: y\""), "{out}");
}
#[test]
fn r03_repair_declines_unclosed_document() {
    let (out, rep) = fix_json_with_report("[1,2");
    assert_eq!(out, "[1,2");
    assert!(rep.contains("line 1, column 5"), "{rep}");
}
#[test]
fn r04_repair_leaves_valid_json_alone() {
    let s = "\u{FEFF}[\":/*é\"]";
    assert_eq!(fix_json_with_report(s).0, s);
}
#[test]
fn r05_repair_removes_comments() {
    assert_eq!(fix_json_with_report("{\"a\":1 // c\n}").0, "{\"a\":1\n}");
}
#[test]
fn r06_repair_python_literals() {
    assert_eq!(fix_json_with_report("[True, None]").0, "[true, null]");
}
#[test]
fn r07_repair_decline_is_specific() {
    let (_, rep) = fix_json_with_report("[1] [2]");
    assert!(rep.contains("line 1, column 5"), "{rep}");
}
#[test]
fn r08_repair_never_returns_changed_but_invalid() {
    // old: "fixed" text that is still invalid JSON
    let s = "{\"a\": [1, 2 /* c */ ]";
    let (out, _) = fix_json_with_report(s);
    assert!(out == s || json_parse_error(&out).is_empty(), "{out}");
}
#[test]
fn r09_repair_does_not_turn_truncation_into_data() {
    // old: "[{}\n\n,-0]" truncated to "[" came back as "[]"
    assert_eq!(fix_json_with_report("[").0, "[");
}
#[test]
fn b01_block_comment_bracket_not_counted() {
    assert_eq!(fix_brackets("f(x) /* ( */"), "f(x) /* ( */");
}
#[test]
fn b02_hash_comment_bracket_not_counted() {
    assert_eq!(fix_brackets("#("), "#(");
}
#[test]
fn b03_check_no_false_if_fi_issue() {
    assert!(check_brackets("if (x) { y(); }").contains("No issues"));
}
#[test]
fn b04_interleaved_kinds() {
    assert_eq!(fix_brackets("y = [bar(]"), "y = [bar()]");
}
#[test]
fn b05_stray_closer_not_balanced_by_opener_at_document_start() {
    assert_eq!(fix_brackets("a + b)"), "a + b");
}
#[test]
fn b06_idempotent() {
    let x = "n2 = bar( /* \" */ i;";
    let once = fix_brackets(x);
    assert_eq!(fix_brackets(&once), once);
}
#[test]
fn b07_apostrophe_in_comment_does_not_open_string() {
    assert_eq!(fix_brackets("// don't (\nf(x"), "// don't (\nf(x)");
}
#[test]
fn b08_regex_literal_not_counted() {
    let s = "re = /[(]+/g; f(re)";
    assert_eq!(fix_brackets(s), s);
}
#[test]
fn b09_template_hole_counted_text_not() {
    let s = "const s = `a ( ${ f([1]) } ]`;";
    assert_eq!(fix_brackets(s), s);
}
#[test]
fn b10_check_reports_positions_as_line_column() {
    let r = check_brackets("foo(a, b\nbar]");
    assert!(r.contains("line 1, column 4"), "{r}");
}
