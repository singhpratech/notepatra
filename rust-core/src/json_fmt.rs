//! JSON Formatter + Fixer — pretty-print, minify, and auto-fix broken JSON.
//! Handles: missing braces, trailing commas, single quotes, unquoted keys,
//! deeply nested structures, preserves all data.

use regex::{Captures, Regex};
use serde::Serialize;

pub fn format_json(input: &str, indent: usize) -> String {
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

pub fn minify_json(input: &str) -> String {
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
    // Pattern: "[ 'key':" or "[ key:" means "[{key:" — missing { after [
    let re_arr_obj = Regex::new(r#"\[\s*([a-zA-Z_"'][a-zA-Z0-9_"']*)\s*:"#).unwrap();
    let before_arr = pass1.clone();
    let arr_obj_count = re_arr_obj.find_iter(&before_arr).count();
    pass1 = re_arr_obj
        .replace_all(&pass1, |caps: &Captures| format!("[{{{}: ", &caps[1]))
        .to_string();
    if pass1 != before_arr {
        report.push(format!(
            "Inserted {} missing {{ after [ for array objects",
            arr_obj_count
        ));
        fixes += 1;
    }

    // Pass 1.5b: Fix missing { after key: when followed by another key:
    // Pattern: "key: word:" means "key: {word:" — missing opening brace
    let re_missing_brace =
        Regex::new(r#"([:,]\s*)([a-zA-Z_]\w*)\s*:\s*([a-zA-Z_]\w*)\s*:"#).unwrap();
    let mut missing_brace_count = 0;
    for _ in 0..10 {
        let before = pass1.clone();
        pass1 = re_missing_brace
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

    // Second pass: quote unquoted keys
    // Run multiple passes because fixing one key might expose the next
    let mut pass2 = pass1;
    let mut total_keys_fixed = 0;
    let mut all_keys: Vec<String> = Vec::new();
    let re_key = Regex::new(r#"(?m)([\{\[,]\s*|\n\s*)([a-zA-Z_$][a-zA-Z0-9_$]*)\s*:"#).unwrap();

    for _ in 0..20 {
        let before = pass2.clone();
        let keys_this_pass: Vec<String> = re_key
            .captures_iter(&before)
            .filter(|c| {
                // Don't re-quote already quoted keys or values like true/false/null
                let key = &c[2];
                key != "true" && key != "false" && key != "null"
            })
            .map(|c| c[2].to_string())
            .collect();

        if keys_this_pass.is_empty() {
            break;
        }

        pass2 = re_key
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
                '}' => {
                    if stack.last() == Some(&'{') {
                        stack.pop();
                    }
                }
                ']' => {
                    if stack.last() == Some(&'[') {
                        stack.pop();
                    }
                }
                _ => {}
            }
        }
        prev = ch;
    }
    // Close unclosed brackets in reverse order (LIFO — correct nesting)
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
    let mut total_trail = 0;
    let re_trailing_array = Regex::new(r",\s*\]").unwrap();
    let re_trailing_object = Regex::new(r",\s*\}").unwrap();
    for _ in 0..10 {
        let before = pass3.clone();
        total_trail += re_trailing_array.find_iter(&before).count()
            + re_trailing_object.find_iter(&before).count();
        pass3 = re_trailing_array.replace_all(&pass3, "]").to_string();
        pass3 = re_trailing_object.replace_all(&pass3, "}").to_string();
        if pass3 == before {
            break;
        }
    }
    if total_trail > 0 {
        report.push(format!("Removed {} trailing comma(s)", total_trail));
        fixes += 1;
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
    val.serialize(&mut ser).unwrap();
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
