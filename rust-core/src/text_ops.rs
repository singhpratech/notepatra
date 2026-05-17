// SPDX-License-Identifier: GPL-3.0-or-later

//! Text operations — sort, dedupe, trim, reverse, case conversion.
//! All operations are allocation-safe and handle huge inputs via iterators.

use std::cmp::Reverse;

/// Sort lines. mode: 0=asc, 1=desc, 2=int_asc, 3=int_desc, 4=len_asc, 5=len_desc
pub fn sort_lines(text: &str, mode: i32) -> String {
    let mut lines: Vec<&str> = text.lines().collect();

    match mode {
        0 => lines.sort_unstable_by_key(|s| s.to_lowercase()),
        1 => lines.sort_unstable_by_key(|s| Reverse(s.to_lowercase())),
        2 => lines.sort_unstable_by_key(|s| parse_int(s)),
        3 => lines.sort_unstable_by_key(|s| Reverse(parse_int(s))),
        4 => lines.sort_unstable_by_key(|s| s.len()),
        5 => lines.sort_unstable_by_key(|s| Reverse(s.len())),
        _ => lines.sort_unstable(),
    }

    lines.join("\n")
}

/// Remove duplicates. mode: 0=all, 1=consecutive only
pub fn remove_duplicates(text: &str, mode: i32) -> String {
    let lines: Vec<&str> = text.lines().collect();

    if mode == 1 {
        // Consecutive only
        let mut result = Vec::with_capacity(lines.len());
        for line in &lines {
            if result.last().is_none_or(|last: &&str| *last != *line) {
                result.push(*line);
            }
        }
        result.join("\n")
    } else {
        // All duplicates
        let mut seen = std::collections::HashSet::with_capacity(lines.len());
        let mut result = Vec::with_capacity(lines.len());
        for line in &lines {
            if seen.insert(*line) {
                result.push(*line);
            }
        }
        result.join("\n")
    }
}

/// Remove empty lines. mode: 0=truly empty, 1=blank (whitespace-only too)
pub fn remove_empty_lines(text: &str, mode: i32) -> String {
    text.lines()
        .filter(|line| {
            if mode == 1 {
                !line.trim().is_empty()
            } else {
                !line.is_empty()
            }
        })
        .collect::<Vec<&str>>()
        .join("\n")
}

/// Trim lines. mode: 0=trailing, 1=leading, 2=both
pub fn trim_lines(text: &str, mode: i32) -> String {
    text.lines()
        .map(|line| match mode {
            0 => line.trim_end(),
            1 => line.trim_start(),
            _ => line.trim(),
        })
        .collect::<Vec<&str>>()
        .join("\n")
}

/// Reverse line order.
pub fn reverse_lines(text: &str) -> String {
    let mut lines: Vec<&str> = text.lines().collect();
    lines.reverse();
    lines.join("\n")
}

/// Join lines with separator.
pub fn join_lines(text: &str, separator: &str) -> String {
    text.lines().collect::<Vec<&str>>().join(separator)
}

/// Convert case. mode: 0=upper, 1=lower, 2=title, 3=sentence, 4=invert
pub fn convert_case(text: &str, mode: i32) -> String {
    match mode {
        0 => text.to_uppercase(),
        1 => text.to_lowercase(),
        2 => title_case(text),
        3 => sentence_case(text),
        4 => text
            .chars()
            .map(|c| {
                if c.is_uppercase() {
                    c.to_lowercase().collect::<String>()
                } else {
                    c.to_uppercase().collect::<String>()
                }
            })
            .collect(),
        _ => text.to_string(),
    }
}

/// Convert whitespace. mode: 0=tab_to_space, 1=space_to_tab
pub fn convert_whitespace(text: &str, tab_width: usize, mode: i32) -> String {
    let spaces = " ".repeat(tab_width);
    if mode == 0 {
        text.replace('\t', &spaces)
    } else {
        text.replace(&spaces, "\t")
    }
}

fn title_case(text: &str) -> String {
    let mut result = String::with_capacity(text.len());
    let mut capitalize_next = true;
    for c in text.chars() {
        if capitalize_next && c.is_alphabetic() {
            result.extend(c.to_uppercase());
            capitalize_next = false;
        } else {
            result.push(if !capitalize_next && c.is_alphabetic() {
                c.to_lowercase().next().unwrap_or(c)
            } else {
                c
            });
            if c.is_whitespace() || c == '-' || c == '_' {
                capitalize_next = true;
            }
        }
    }
    result
}

fn sentence_case(text: &str) -> String {
    let mut result = String::with_capacity(text.len());
    let mut capitalize_next = true;
    for c in text.chars() {
        if capitalize_next && c.is_alphabetic() {
            result.extend(c.to_uppercase());
            capitalize_next = false;
        } else {
            result.push(c.to_lowercase().next().unwrap_or(c));
            if c == '.' || c == '!' || c == '?' || c == '\n' {
                capitalize_next = true;
            }
        }
    }
    result
}

fn parse_int(s: &str) -> i64 {
    s.trim().parse::<i64>().unwrap_or(i64::MAX)
}
