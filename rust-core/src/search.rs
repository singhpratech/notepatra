// SPDX-License-Identifier: GPL-3.0-or-later

//! Search engine — literal and regex search, parallel for large texts.

use crate::SearchResult;
use aho_corasick::AhoCorasick;
use regex::RegexBuilder;
use std::ptr;

pub fn find_all(
    haystack: &str,
    needle: &str,
    is_regex: bool,
    case_sensitive: bool,
    whole_word: bool,
) -> SearchResult {
    if needle.is_empty() {
        return SearchResult {
            positions: ptr::null_mut(),
            count: 0,
        };
    }

    let positions: Vec<usize> = if is_regex {
        match RegexBuilder::new(needle)
            .case_insensitive(!case_sensitive)
            .build()
        {
            Ok(re) => re.find_iter(haystack).map(|m| m.start()).collect(),
            Err(_) => Vec::new(),
        }
    } else if whole_word {
        let pattern = format!(r"\b{}\b", regex::escape(needle));
        match RegexBuilder::new(&pattern)
            .case_insensitive(!case_sensitive)
            .build()
        {
            Ok(re) => re.find_iter(haystack).map(|m| m.start()).collect(),
            Err(_) => Vec::new(),
        }
    } else if case_sensitive {
        // Aho-Corasick for fast literal search
        let ac = AhoCorasick::new([needle]).unwrap();
        ac.find_iter(haystack).map(|m| m.start()).collect()
    } else {
        let lower_haystack = haystack.to_lowercase();
        let lower_needle = needle.to_lowercase();
        let ac = AhoCorasick::new([lower_needle.as_str()]).unwrap();
        ac.find_iter(&lower_haystack).map(|m| m.start()).collect()
    };

    let count = positions.len();
    if count == 0 {
        return SearchResult {
            positions: ptr::null_mut(),
            count: 0,
        };
    }

    let mut boxed = positions.into_boxed_slice();
    let ptr = boxed.as_mut_ptr();
    std::mem::forget(boxed);

    SearchResult {
        positions: ptr,
        count,
    }
}

pub fn count_matches(haystack: &str, needle: &str, is_regex: bool, case_sensitive: bool) -> usize {
    if needle.is_empty() {
        return 0;
    }

    if is_regex {
        match RegexBuilder::new(needle)
            .case_insensitive(!case_sensitive)
            .build()
        {
            Ok(re) => re.find_iter(haystack).count(),
            Err(_) => 0,
        }
    } else if case_sensitive {
        let ac = AhoCorasick::new([needle]).unwrap();
        ac.find_iter(haystack).count()
    } else {
        let lower = haystack.to_lowercase();
        let lower_needle = needle.to_lowercase();
        let ac = AhoCorasick::new([lower_needle.as_str()]).unwrap();
        ac.find_iter(&lower).count()
    }
}

pub fn replace_all(
    haystack: &str,
    needle: &str,
    replacement: &str,
    is_regex: bool,
    case_sensitive: bool,
) -> String {
    if needle.is_empty() {
        return haystack.to_string();
    }

    if is_regex {
        match RegexBuilder::new(needle)
            .case_insensitive(!case_sensitive)
            .build()
        {
            Ok(re) => re.replace_all(haystack, replacement).into_owned(),
            Err(_) => haystack.to_string(),
        }
    } else if case_sensitive {
        haystack.replace(needle, replacement)
    } else {
        // Case-insensitive literal replace
        let re = RegexBuilder::new(&regex::escape(needle))
            .case_insensitive(true)
            .build()
            .unwrap();
        re.replace_all(haystack, replacement).into_owned()
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    // Copy the positions out, then release them exactly the way the FFI
    // consumer would (Vec::from_raw_parts — the mirror of find_all's
    // into_boxed_slice + forget).
    fn positions_of(result: SearchResult) -> Vec<usize> {
        if result.positions.is_null() || result.count == 0 {
            return Vec::new();
        }
        unsafe { Vec::from_raw_parts(result.positions, result.count, result.count) }
    }

    #[test]
    fn find_all_literal_case_sensitive() {
        let r = find_all("foo bar foo Foo", "foo", false, true, false);
        assert_eq!(positions_of(r), vec![0, 8]);
    }

    #[test]
    fn find_all_literal_case_insensitive() {
        let r = find_all("foo bar foo Foo", "foo", false, false, false);
        assert_eq!(positions_of(r), vec![0, 8, 12]);
    }

    #[test]
    fn find_all_regex() {
        let r = find_all("a1 b22 c333", r"\d+", true, true, false);
        assert_eq!(positions_of(r), vec![1, 4, 8]);
    }

    #[test]
    fn find_all_whole_word() {
        // "cat" must not match inside "catalog" / "concat".
        let r = find_all("cat catalog concat cat", "cat", false, true, true);
        assert_eq!(positions_of(r), vec![0, 19]);
    }

    #[test]
    fn find_all_empty_needle_returns_null() {
        let r = find_all("anything", "", false, true, false);
        assert!(r.positions.is_null());
        assert_eq!(r.count, 0);
    }

    #[test]
    fn find_all_invalid_regex_is_no_match() {
        let r = find_all("abc", "[unclosed", true, true, false);
        assert!(r.positions.is_null());
        assert_eq!(r.count, 0);
    }

    #[test]
    fn count_and_replace_agree() {
        assert_eq!(count_matches("x y x y x", "x", false, true), 3);
        assert_eq!(replace_all("x y x y x", "x", "z", false, true), "z y z y z");
        // Case-insensitive literal replace hits every casing.
        assert_eq!(replace_all("Ab ab AB", "ab", "-", false, false), "- - -");
    }
}
