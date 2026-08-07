// SPDX-License-Identifier: GPL-3.0-or-later

//! JSON Formatter + Fixer — pretty-print, minify, and auto-fix broken JSON.
//! Handles: missing braces, trailing commas, single quotes, unquoted keys,
//! deeply nested structures, preserves all data.
//!
//! v0.1.48 hardening pass:
//!   • All regexes compile once via `OnceLock` (no per-call rebuild) and
//!     fail gracefully — if a pattern ever fails to compile, that pass
//!     is skipped instead of panicking across the C ABI boundary.
//!   • `pretty_print()` no longer panics on serialization errors — falls
//!     back to `manual_pretty_print` so the user always gets *something*.
//!   • Hard 50 MB input cap to avoid OOM on pathological input.
//!   • Recursion-guard via `manual_pretty_print` indent-saturating math.

use regex::{Captures, Regex};
use serde::Serialize;
use std::sync::OnceLock;

const MAX_INPUT_BYTES: usize = 50 * 1024 * 1024; // 50 MB

// ── Compiled-once regex helpers ──────────────────────────────────────────

fn re_arr_obj() -> Option<&'static Regex> {
    static R: OnceLock<Option<Regex>> = OnceLock::new();
    R.get_or_init(|| Regex::new(r#"\[\s*([a-zA-Z_"'][a-zA-Z0-9_"']*)\s*:"#).ok())
        .as_ref()
}

fn re_missing_brace() -> Option<&'static Regex> {
    static R: OnceLock<Option<Regex>> = OnceLock::new();
    R.get_or_init(|| Regex::new(r#"([:,]\s*)([a-zA-Z_]\w*)\s*:\s*([a-zA-Z_]\w*)\s*:"#).ok())
        .as_ref()
}

fn re_key() -> Option<&'static Regex> {
    static R: OnceLock<Option<Regex>> = OnceLock::new();
    R.get_or_init(|| Regex::new(r#"(?m)([\{\[,]\s*|\n\s*)([a-zA-Z_$][a-zA-Z0-9_$]*)\s*:"#).ok())
        .as_ref()
}

fn re_trailing_array() -> Option<&'static Regex> {
    static R: OnceLock<Option<Regex>> = OnceLock::new();
    R.get_or_init(|| Regex::new(r",\s*\]").ok()).as_ref()
}

fn re_trailing_object() -> Option<&'static Regex> {
    static R: OnceLock<Option<Regex>> = OnceLock::new();
    R.get_or_init(|| Regex::new(r",\s*\}").ok()).as_ref()
}

// ─────────────────────────────────────────────────────────────────────────

pub fn format_json(input: &str, indent: usize) -> String {
    if input.len() > MAX_INPUT_BYTES {
        return format!(
            "/* Input too large for JSON formatter ({} bytes, max {} bytes). */\n{}",
            input.len(),
            MAX_INPUT_BYTES,
            input
        );
    }
    // Try as-is first
    if let Ok(val) = serde_json::from_str::<serde_json::Value>(input) {
        return pretty_print(&val, indent);
    }
    // Try fixing then formatting
    let fixed = fix_json(input);
    if let Ok(val) = serde_json::from_str::<serde_json::Value>(&fixed) {
        return pretty_print(&val, indent);
    }
    // Last resort — manual pretty print
    manual_pretty_print(input, indent)
}

/// Strict RFC-8259 validation: `""` when `input` is valid JSON, otherwise a
/// human-readable parse error carrying the position.
///
/// [`format_json`] deliberately REPAIRS what it is given — that is the whole
/// point of the editor's JSON panel, where a human sees the result and can
/// undo it. Over MCP there is no human in that loop: v0.1.125 turned `[1,2`
/// into `[1,2]` and reported success, so a truncated config file came back
/// syntactically valid and semantically invented. Callers that must not
/// silently repair (the MCP `format_json` tool) validate first through here.
///
/// This never repairs and never allocates a parsed value beyond the check.
pub fn json_parse_error(input: &str) -> String {
    if input.len() > MAX_INPUT_BYTES {
        return format!(
            "input too large for the JSON parser ({} bytes, max {} bytes)",
            input.len(),
            MAX_INPUT_BYTES
        );
    }
    match serde_json::from_str::<serde::de::IgnoredAny>(input) {
        Ok(_) => String::new(),
        // serde_json's Display already reads "expected value at line 1 column 5".
        Err(e) => e.to_string(),
    }
}

pub fn minify_json(input: &str) -> String {
    if input.len() > MAX_INPUT_BYTES {
        return input.to_string();
    }
    let fixed = fix_json(input);
    if let Ok(val) = serde_json::from_str::<serde_json::Value>(&fixed) {
        serde_json::to_string(&val).unwrap_or_else(|_| input.to_string())
    } else {
        input.replace(|c: char| c.is_whitespace(), "")
    }
}

pub fn fix_json(input: &str) -> String {
    fix_json_with_report(input).0
}

pub fn fix_json_with_report(input: &str) -> (String, String) {
    if input.len() > MAX_INPUT_BYTES {
        return (
            input.to_string(),
            format!(
                "Input too large ({} bytes, max {} bytes). No changes applied.",
                input.len(),
                MAX_INPUT_BYTES
            ),
        );
    }

    // If already valid, return as-is
    if serde_json::from_str::<serde_json::Value>(input).is_ok() {
        return (
            input.to_string(),
            "JSON is already valid. No fixes needed.".to_string(),
        );
    }

    let mut report = Vec::new();
    let mut fixes = 0;

    // Process character by character to handle all edge cases
    let chars: Vec<char> = input.chars().collect();
    let len = chars.len();

    // Track state
    let mut in_single_string = false;
    let mut in_double_string = false;
    let mut single_quote_count = 0;

    // First pass: convert single quotes to double quotes character by character
    let mut pass1 = String::with_capacity(len + 100);
    let mut i = 0;
    while i < len {
        let ch = chars[i];

        if in_double_string {
            if ch == '"' && (i == 0 || chars[i - 1] != '\\') {
                in_double_string = false;
            }
            pass1.push(ch);
        } else if in_single_string {
            if ch == '\'' && (i == 0 || chars[i - 1] != '\\') {
                in_single_string = false;
                pass1.push('"'); // close with double quote
                single_quote_count += 1;
            } else if ch == '"' {
                pass1.push('\\');
                pass1.push('"'); // escape double quotes inside single-quoted string
            } else {
                pass1.push(ch);
            }
        } else {
            if ch == '\'' {
                in_single_string = true;
                pass1.push('"'); // open with double quote
            } else if ch == '"' {
                in_double_string = true;
                pass1.push(ch);
            } else {
                pass1.push(ch);
            }
        }
        i += 1;
    }
    if single_quote_count > 0 {
        report.push(format!(
            "Converted {} single-quoted string(s) to double quotes",
            single_quote_count
        ));
        fixes += 1;
    }

    // Pass 1.5a: Fix [ followed by key: — means [{ missing
    if let Some(re) = re_arr_obj() {
        let before_arr = pass1.clone();
        let arr_obj_count = re.find_iter(&before_arr).count();
        pass1 = re
            .replace_all(&pass1, |caps: &Captures| format!("[{{{}: ", &caps[1]))
            .to_string();
        if pass1 != before_arr {
            report.push(format!(
                "Inserted {} missing {{ after [ for array objects",
                arr_obj_count
            ));
            fixes += 1;
        }
    }

    // Pass 1.5b: Fix missing { after key: when followed by another key:
    if let Some(re) = re_missing_brace() {
        let mut missing_brace_count = 0;
        for _ in 0..10 {
            let before = pass1.clone();
            pass1 = re
                .replace_all(&pass1, |caps: &Captures| {
                    format!("{}{}: {{{}: ", &caps[1], &caps[2], &caps[3])
                })
                .to_string();
            if pass1 == before {
                break;
            }
            missing_brace_count += 1;
        }
        if missing_brace_count > 0 {
            report.push(format!(
                "Inserted {} missing {{ brace(s) for nested objects",
                missing_brace_count
            ));
            fixes += 1;
        }
    }

    // Second pass: quote unquoted keys
    let mut pass2 = pass1;
    let mut total_keys_fixed = 0;
    let mut all_keys: Vec<String> = Vec::new();
    if let Some(re) = re_key() {
        for _ in 0..20 {
            let before = pass2.clone();
            let keys_this_pass: Vec<String> = re
                .captures_iter(&before)
                .filter(|c| {
                    let key = &c[2];
                    key != "true" && key != "false" && key != "null"
                })
                .map(|c| c[2].to_string())
                .collect();

            if keys_this_pass.is_empty() {
                break;
            }

            pass2 = re
                .replace_all(&pass2, |caps: &Captures| {
                    let key = &caps[2];
                    if key == "true" || key == "false" || key == "null" {
                        caps[0].to_string()
                    } else {
                        format!("{}\"{}\":", &caps[1], key)
                    }
                })
                .to_string();

            total_keys_fixed += keys_this_pass.len();
            all_keys.extend(keys_this_pass);

            if pass2 == before {
                break;
            }
        }
    }

    if total_keys_fixed > 0 {
        let sample: Vec<String> = all_keys
            .iter()
            .take(10)
            .map(|k| format!("\"{}\"", k))
            .collect();
        report.push(format!(
            "Quoted {} unquoted key(s): {}{}",
            total_keys_fixed,
            sample.join(", "),
            if total_keys_fixed > 10 { " ..." } else { "" }
        ));
        fixes += 1;
    }

    let mut pass3 = pass2;

    // Third pass: fix missing braces/brackets in correct nesting order
    let mut stack: Vec<char> = Vec::new();
    let mut in_str = false;
    let mut prev = '\0';
    for ch in pass3.chars() {
        if ch == '"' && prev != '\\' {
            in_str = !in_str;
        }
        if !in_str {
            match ch {
                '{' | '[' => stack.push(ch),
                '}' if stack.last() == Some(&'{') => {
                    stack.pop();
                }
                ']' if stack.last() == Some(&'[') => {
                    stack.pop();
                }
                _ => {}
            }
        }
        prev = ch;
    }
    if !stack.is_empty() {
        let mut closers = String::new();
        for opener in stack.iter().rev() {
            closers.push(match opener {
                '{' => '}',
                '[' => ']',
                _ => unreachable!(),
            });
        }
        report.push(format!(
            "Added {} missing closer(s) at end: {}",
            closers.len(),
            closers
        ));
        fixes += 1;
        pass3.push_str(&closers);
    }

    // Fourth pass: NOW remove trailing commas (after closers are added)
    if let (Some(re_arr), Some(re_obj)) = (re_trailing_array(), re_trailing_object()) {
        let mut total_trail = 0;
        for _ in 0..10 {
            let before = pass3.clone();
            total_trail += re_arr.find_iter(&before).count() + re_obj.find_iter(&before).count();
            pass3 = re_arr.replace_all(&pass3, "]").to_string();
            pass3 = re_obj.replace_all(&pass3, "}").to_string();
            if pass3 == before {
                break;
            }
        }
        if total_trail > 0 {
            report.push(format!("Removed {} trailing comma(s)", total_trail));
            fixes += 1;
        }
    }

    // Check if result is valid — also try formatting to confirm
    let valid = serde_json::from_str::<serde_json::Value>(&pass3).is_ok()
        || serde_json::from_str::<serde_json::Value>(pass3.trim()).is_ok();

    let summary = if fixes == 0 {
        "Could not identify specific issues. JSON may have structural problems.".to_string()
    } else {
        let status = if valid {
            "JSON is now VALID."
        } else {
            "JSON may still have issues — try manual review."
        };
        format!(
            "Fixed {} issue(s):\n\n{}\n\n{}",
            fixes,
            report.join("\n"),
            status
        )
    };

    (pass3, summary)
}

fn pretty_print(val: &serde_json::Value, indent: usize) -> String {
    let indent_bytes = " ".repeat(indent).into_bytes();
    let buf = Vec::new();
    let formatter = serde_json::ser::PrettyFormatter::with_indent(&indent_bytes);
    let mut ser = serde_json::Serializer::with_formatter(buf, formatter);
    // Serialization of a serde_json::Value should never fail, but if the
    // serializer rejects something (e.g. NaN with a strict feature flag),
    // fall back to compact-then-manual instead of panicking across the C
    // ABI boundary.
    if val.serialize(&mut ser).is_err() {
        return manual_pretty_print(&val.to_string(), indent);
    }
    String::from_utf8(ser.into_inner()).unwrap_or_default()
}

fn manual_pretty_print(input: &str, indent_size: usize) -> String {
    let mut result = String::with_capacity(input.len() * 2);
    let mut indent = 0usize;
    let mut in_string = false;
    let mut prev = '\0';

    for ch in input.chars() {
        if ch == '"' && prev != '\\' {
            in_string = !in_string;
            result.push(ch);
        } else if in_string {
            result.push(ch);
        } else {
            match ch {
                '{' | '[' => {
                    result.push(ch);
                    result.push('\n');
                    indent += indent_size;
                    result.push_str(&" ".repeat(indent));
                }
                '}' | ']' => {
                    result.push('\n');
                    indent = indent.saturating_sub(indent_size);
                    result.push_str(&" ".repeat(indent));
                    result.push(ch);
                }
                ',' => {
                    result.push(',');
                    result.push('\n');
                    result.push_str(&" ".repeat(indent));
                }
                ':' => result.push_str(": "),
                ' ' | '\t' | '\n' | '\r' => {}
                _ => result.push(ch),
            }
        }
        prev = ch;
    }
    result
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn empty_input_does_not_panic() {
        assert_eq!(format_json("", 4), manual_pretty_print("", 4));
        assert_eq!(minify_json(""), "");
        let (out, _) = fix_json_with_report("");
        // Empty input is technically invalid JSON, so fix_json keeps it.
        assert_eq!(out, "");
    }

    #[test]
    fn already_valid_passes_through() {
        let s = r#"{"a":1,"b":[2,3]}"#;
        let pretty = format_json(s, 2);
        assert!(pretty.contains("\"a\""));
        assert!(pretty.contains("\"b\""));
    }

    #[test]
    fn missing_close_brace_is_added() {
        let (out, _) = fix_json_with_report(r#"{"a": 1"#);
        assert_eq!(out, r#"{"a": 1}"#);
    }

    #[test]
    fn trailing_comma_is_stripped() {
        let (out, _) = fix_json_with_report(r#"{"a": 1,}"#);
        assert_eq!(out, r#"{"a": 1}"#);
    }

    #[test]
    fn unquoted_key_is_quoted() {
        let (out, _) = fix_json_with_report(r#"{a: 1}"#);
        assert!(out.contains(r#""a""#));
    }

    #[test]
    fn deeply_nested_does_not_overflow() {
        let input = format!("{}{}", "[".repeat(1000), "]".repeat(1000));
        let pretty = format_json(&input, 2);
        // Should not panic, and should produce some output.
        assert!(!pretty.is_empty());
    }

    #[test]
    fn over_max_size_is_safe() {
        let input = "{".repeat(MAX_INPUT_BYTES + 1);
        let out = format_json(&input, 2);
        assert!(out.starts_with("/* Input too large"));
    }

    #[test]
    fn mixed_quotes_in_string_value() {
        let (out, _) = fix_json_with_report(r#"{"key": "value with 'apostrophe'"}"#);
        // Should preserve the apostrophes inside the string value.
        assert!(out.contains("apostrophe"));
    }

    // ─── Real-world JSON patterns ────────────────────────────────────────
    //
    // Verifies that common real-world JSON shapes (API responses, config
    // files, JSONL, surrogate-pair emoji, BOM, etc.) format and minify
    // without crashing or losing content.

    #[test]
    fn rw_npm_package_json() {
        let s = r#"{"name":"foo","version":"1.0.0","scripts":{"test":"jest","build":"tsc"},"dependencies":{"react":"^18.0.0","lodash":"^4.17.0"}}"#;
        let pretty = format_json(s, 2);
        assert!(pretty.contains("\"name\""));
        assert!(pretty.contains("\"react\""));
        // Round-trip: parse the pretty form back.
        assert!(serde_json::from_str::<serde_json::Value>(&pretty).is_ok());
    }

    #[test]
    fn rw_api_response_with_arrays() {
        let s = r#"{"page":1,"per_page":10,"total":250,"results":[{"id":1,"name":"a"},{"id":2,"name":"b"}]}"#;
        let pretty = format_json(s, 4);
        assert!(serde_json::from_str::<serde_json::Value>(&pretty).is_ok());
    }

    #[test]
    fn rw_geojson_feature_collection() {
        let s = r#"{"type":"FeatureCollection","features":[{"type":"Feature","geometry":{"type":"Point","coordinates":[-122.4,37.8]},"properties":{"name":"SF"}}]}"#;
        let pretty = format_json(s, 2);
        assert!(pretty.contains("FeatureCollection"));
        assert!(pretty.contains("coordinates"));
    }

    #[test]
    fn rw_json_schema() {
        let s = r#"{"$schema":"https://json-schema.org/draft/2020-12/schema","type":"object","properties":{"name":{"type":"string"},"age":{"type":"integer","minimum":0}},"required":["name"]}"#;
        let pretty = format_json(s, 2);
        assert!(pretty.contains("$schema"));
        assert!(pretty.contains("required"));
    }

    #[test]
    fn rw_single_primitive_passes() {
        for s in &["null", "42", "true", "false", "\"hello\"", "3.14"] {
            let pretty = format_json(s, 2);
            assert!(
                !pretty.is_empty(),
                "primitive '{}' produced empty output",
                s
            );
        }
    }

    #[test]
    fn rw_emoji_in_strings() {
        // Emoji must round-trip — both BMP (😀) and astral.
        let s = r#"{"name":"Alice 👋","emoji":"🎉🚀","flag":"🇺🇸"}"#;
        let pretty = format_json(s, 2);
        assert!(pretty.contains("👋"), "lost wave emoji in:\n{}", pretty);
        assert!(
            pretty.contains("🇺🇸"),
            "lost flag (regional indicator pair) in:\n{}",
            pretty
        );
        // Round-trip
        let parsed: serde_json::Value = serde_json::from_str(&pretty).expect("valid JSON");
        assert_eq!(parsed["name"], "Alice 👋");
    }

    #[test]
    fn rw_unicode_rtl_and_cjk() {
        let s = r#"{"ar":"مرحبا","he":"שלום","zh":"你好","ja":"こんにちは","ko":"안녕하세요"}"#;
        let pretty = format_json(s, 2);
        assert!(pretty.contains("مرحبا"), "lost Arabic");
        assert!(pretty.contains("שלום"), "lost Hebrew");
        assert!(pretty.contains("你好"), "lost Chinese");
    }

    #[test]
    fn rw_escaped_chars_preserved() {
        let s = r#"{"path":"C:\\Users\\test","quote":"He said \"hi\"","newline":"line1\nline2","tab":"a\tb"}"#;
        let pretty = format_json(s, 2);
        let parsed: serde_json::Value = serde_json::from_str(&pretty).expect("valid JSON");
        assert_eq!(parsed["path"], "C:\\Users\\test");
        assert_eq!(parsed["quote"], "He said \"hi\"");
        assert_eq!(parsed["newline"], "line1\nline2");
    }

    #[test]
    fn rw_fix_python_dict_paste() {
        // User pastes a Python dict — single quotes, True/False, no quoted keys.
        let s = r#"{'name': 'Alice', 'age': 30, 'active': True, 'manager': None}"#;
        let (fixed, _) = fix_json_with_report(s);
        // Should fix at least the quotes; True/False/None aren't normalized
        // by our fixer but the structural fix is the priority.
        assert!(
            fixed.contains("\"name\""),
            "unquoted key not fixed:\n{}",
            fixed
        );
        assert!(
            fixed.contains("\"Alice\""),
            "single→double quote conversion failed:\n{}",
            fixed
        );
    }

    #[test]
    fn rw_fix_trailing_commas_at_every_level() {
        let s = r#"{"a":[1,2,3,],"b":{"x":1,},"c":[{"k":"v",},],}"#;
        let (fixed, _) = fix_json_with_report(s);
        assert!(
            serde_json::from_str::<serde_json::Value>(&fixed).is_ok(),
            "trailing commas not all fixed:\n{}",
            fixed
        );
    }

    #[test]
    fn rw_fix_unquoted_keys_nested() {
        let s = r#"{a: 1, b: {c: 2, d: [{e: 3}]}}"#;
        let (fixed, _) = fix_json_with_report(s);
        assert!(
            serde_json::from_str::<serde_json::Value>(&fixed).is_ok(),
            "nested unquoted keys not fixed:\n{}",
            fixed
        );
    }

    #[test]
    fn rw_long_array_does_not_crash() {
        let mut s = String::from("[");
        for i in 0..1000 {
            if i > 0 {
                s.push(',');
            }
            s.push_str(&i.to_string());
        }
        s.push(']');
        let pretty = format_json(&s, 2);
        // Should produce 1000 lines + brackets.
        assert!(pretty.lines().count() > 500);
    }

    #[test]
    fn rw_truncated_json_does_not_panic() {
        let s = "{\"key\": \"value\", \"nested\": {\"deeper\":";
        let (out, _) = fix_json_with_report(s);
        // Should produce some closure attempt without panicking.
        assert!(!out.is_empty());
    }

    #[test]
    fn rw_html_pasted_as_json_does_not_crash() {
        let s = "<html><body>not json</body></html>";
        let pretty = format_json(s, 2);
        // Manual fallback should handle this without panic.
        assert!(!pretty.is_empty() || pretty.is_empty()); // just don't crash
    }

    #[test]
    fn rw_minify_preserves_unicode() {
        let s = r#"{"emoji":"🎉","text":"Hello 你好"}"#;
        let mini = minify_json(s);
        assert!(mini.contains("🎉"));
        assert!(mini.contains("你好"));
        assert!(!mini.contains('\n'));
    }

    #[test]
    fn rw_jsonl_first_line_is_valid() {
        // JSONL — newline-delimited JSON. Our formatter only handles a
        // single document but should at least format the first object.
        let s = r#"{"id":1,"name":"a"}
{"id":2,"name":"b"}
{"id":3,"name":"c"}"#;
        let pretty = format_json(s, 2);
        // Either formats just first doc OR returns reasonable text — must
        // not crash, must contain at least one of the records.
        assert!(pretty.contains("name") || pretty.contains("id"));
    }

    #[test]
    fn garbage_does_not_crash() {
        let (out, _) = fix_json_with_report("][}{][}{[][");
        // Should produce SOME output (possibly still broken) without panic.
        assert!(!out.is_empty());
    }

    // ── v0.1.126 · NP-09 ─────────────────────────────────────────────
    //
    // These are the exact inputs from the v0.1.125 retest, where every one of
    // them came back through the MCP `format_json` tool with isError:false.
    // The worst was `[1,2` -> `[1,2]`: a truncated config file turned
    // syntactically valid and semantically invented, so an assistant that
    // formatted then wrote it back fabricated data the user never had.
    #[test]
    fn json_parse_error_refuses_what_the_fixer_would_invent() {
        // The repairer's output is still what the JSON PANEL wants — assert
        // that first, so this test also pins that we did not "fix" the panel.
        assert_eq!(
            format_json("[1,2", 2).replace(char::is_whitespace, ""),
            "[1,2]"
        );

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
            // serde_json reports a position; without one the caller cannot act.
            assert!(
                err.contains("line") || err.contains("column") || err.contains("EOF"),
                "parse error carries no position: {err:?} for {bad:?}"
            );
        }
    }

    #[test]
    fn json_parse_error_accepts_every_valid_shape() {
        // Vacuity guard: a validator that refused everything would pass the
        // test above. Top-level scalars are valid JSON per RFC 8259 and must
        // not be collateral damage.
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
        ] {
            assert_eq!(json_parse_error(good), "", "rejected valid JSON: {good:?}");
        }
    }
}
