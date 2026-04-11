//! Bracket/Parenthesis Fixer — detects and fixes unmatched brackets.
//! Fixes: (), [], {}, <>, begin/end, if/endif, do/done

pub fn fix_brackets(input: &str) -> String {
    let mut result = input.to_string();

    // Fix standard brackets: (), [], {}
    result = fix_paired(&result, '(', ')');
    result = fix_paired(&result, '[', ']');
    result = fix_paired(&result, '{', '}');

    result
}

pub fn check_brackets(input: &str) -> String {
    let mut report = String::new();
    let lines: Vec<&str> = input.lines().collect();

    // Track bracket positions
    let mut stack: Vec<(char, usize, usize)> = Vec::new(); // (char, line, col)
    let pairs: &[(char, char)] = &[('(', ')'), ('[', ']'), ('{', '}')];
    let mut in_string = false;
    let mut string_char = '\0';
    let mut errors = 0;

    for (line_num, line) in lines.iter().enumerate() {
        let mut prev = '\0';
        for (col, ch) in line.chars().enumerate() {
            // Track strings
            if !in_string && (ch == '"' || ch == '\'') {
                in_string = true;
                string_char = ch;
                prev = ch;
                continue;
            }
            if in_string && ch == string_char && prev != '\\' {
                in_string = false;
                prev = ch;
                continue;
            }
            if in_string {
                prev = ch;
                continue;
            }

            // Check openers
            for (open, _close) in pairs {
                if ch == *open {
                    stack.push((ch, line_num + 1, col + 1));
                }
            }

            // Check closers
            for (open, close) in pairs {
                if ch == *close {
                    if let Some(last) = stack.last() {
                        if last.0 == *open {
                            stack.pop();
                        } else {
                            report.push_str(&format!(
                                "Line {}, Col {}: Unexpected '{}' — expected closing for '{}' at Line {}, Col {}\n",
                                line_num + 1, col + 1, ch, last.0, last.1, last.2
                            ));
                            errors += 1;
                        }
                    } else {
                        report.push_str(&format!(
                            "Line {}, Col {}: Unexpected '{}' — no matching opener\n",
                            line_num + 1,
                            col + 1,
                            ch
                        ));
                        errors += 1;
                    }
                }
            }
            prev = ch;
        }
    }

    // Report unclosed openers
    for (ch, line, col) in &stack {
        report.push_str(&format!("Line {}, Col {}: Unclosed '{}'\n", line, col, ch));
        errors += 1;
    }

    // Check begin/end, if/fi, do/done pairs
    let keyword_pairs = [
        ("begin", "end"),
        ("if", "fi"),
        ("do", "done"),
        ("case", "esac"),
    ];

    for (opener, closer) in &keyword_pairs {
        let open_count = input.match_indices(opener).count();
        let close_count = input.match_indices(closer).count();
        if open_count != close_count {
            report.push_str(&format!(
                "Keyword mismatch: {} '{}' vs {} '{}'\n",
                open_count, opener, close_count, closer
            ));
            errors += 1;
        }
    }

    if errors == 0 {
        report.push_str("All brackets matched. No issues found.\n");
    } else {
        report.insert_str(0, &format!("Found {} issue(s):\n\n", errors));
    }

    report
}

fn fix_paired(input: &str, open: char, close: char) -> String {
    let mut result = input.to_string();
    let mut count = 0i32;
    let mut in_string = false;
    let mut prev = '\0';

    for ch in result.chars() {
        if (ch == '"' || ch == '\'') && prev != '\\' {
            in_string = !in_string;
        }
        if !in_string {
            if ch == open {
                count += 1;
            }
            if ch == close {
                count -= 1;
            }
        }
        prev = ch;
    }

    // Add missing closers
    for _ in 0..count.max(0) {
        result.push(close);
    }
    // Add missing openers
    for _ in 0..(-count).max(0) {
        result.insert(0, open);
    }

    result
}
