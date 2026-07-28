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
            lengths: ptr::null_mut(),
            count: 0,
        };
    }

    // Spans, not bare starts. Callers highlight a RANGE, and with a regex the
    // match length has nothing to do with the pattern length — Mark All used to
    // assume they were equal and painted `\d+` as three characters every time.
    let spans: Vec<(usize, usize)> = if is_regex {
        match RegexBuilder::new(needle)
            .case_insensitive(!case_sensitive)
            .build()
        {
            Ok(re) => re
                .find_iter(haystack)
                .map(|m| (m.start(), m.end()))
                .collect(),
            Err(_) => Vec::new(),
        }
    } else if whole_word {
        let pattern = format!(r"\b{}\b", regex::escape(needle));
        match RegexBuilder::new(&pattern)
            .case_insensitive(!case_sensitive)
            .build()
        {
            Ok(re) => re
                .find_iter(haystack)
                .map(|m| (m.start(), m.end()))
                .collect(),
            Err(_) => Vec::new(),
        }
    } else if case_sensitive {
        // Aho-Corasick for fast literal search
        let ac = AhoCorasick::new([needle]).unwrap();
        ac.find_iter(haystack)
            .map(|m| (m.start(), m.end()))
            .collect()
    } else {
        // Searches the ORIGINAL haystack with a case-insensitive regex over the
        // escaped needle. The previous implementation lowercased the whole
        // haystack and returned offsets into that copy — but str::to_lowercase
        // is not length-preserving in UTF-8 (U+0130 grows 2->3 bytes, U+212A
        // shrinks 3->1, U+212B 3->2, U+1E9E 3->2), so a single such character
        // anywhere earlier in the file shifted every subsequent highlight, and
        // the error accumulated.
        match RegexBuilder::new(&regex::escape(needle))
            .case_insensitive(true)
            .build()
        {
            Ok(re) => re
                .find_iter(haystack)
                .map(|m| (m.start(), m.end()))
                .collect(),
            Err(_) => Vec::new(),
        }
    };

    spans_to_result(spans)
}

/// Hand the spans across the FFI as two parallel arrays. Both are reclaimed by
/// `npc_free_matches`, which must free exactly as many as this allocates.
fn spans_to_result(spans: Vec<(usize, usize)>) -> SearchResult {
    let count = spans.len();
    if count == 0 {
        return SearchResult {
            positions: ptr::null_mut(),
            lengths: ptr::null_mut(),
            count: 0,
        };
    }

    let mut starts: Vec<usize> = Vec::with_capacity(count);
    let mut lengths: Vec<usize> = Vec::with_capacity(count);
    for (start, end) in spans {
        starts.push(start);
        lengths.push(end - start);
    }

    let mut boxed_starts = starts.into_boxed_slice();
    let mut boxed_lengths = lengths.into_boxed_slice();
    let starts_ptr = boxed_starts.as_mut_ptr();
    let lengths_ptr = boxed_lengths.as_mut_ptr();
    std::mem::forget(boxed_starts);
    std::mem::forget(boxed_lengths);

    SearchResult {
        positions: starts_ptr,
        lengths: lengths_ptr,
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
        // Same engine as find_all's case-insensitive branch. Counting a
        // lowercased copy usually agreed, but "usually" means the count and the
        // highlights could disagree on exactly the inputs where it matters.
        match RegexBuilder::new(&regex::escape(needle))
            .case_insensitive(true)
            .build()
        {
            Ok(re) => re.find_iter(haystack).count(),
            Err(_) => 0,
        }
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
        // NoExpand is load-bearing, not decoration.
        //
        // This is a LITERAL replace that happens to be implemented with the
        // regex engine. Passing `&str` as the replacement makes the engine read
        // it as a substitution TEMPLATE, so `$100` parsed as capture group 100
        // and expanded to nothing — silently deleting the user's replacement.
        // `$var`, `${HOME}` and even `a$b` all lost text the same way. And
        // because Match case defaults to OFF, this was the branch that ran out
        // of the box, across every open tab.
        //
        // NoExpand disables template parsing entirely. Escaping `$` to `$$`
        // would also work but is one more rule to get right at every call site;
        // this makes expansion impossible rather than merely unlikely.
        let re = RegexBuilder::new(&regex::escape(needle))
            .case_insensitive(true)
            .build()
            .unwrap();
        re.replace_all(haystack, regex::NoExpand(replacement))
            .into_owned()
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
        let starts = unsafe { Vec::from_raw_parts(result.positions, result.count, result.count) };
        if !result.lengths.is_null() {
            drop(unsafe { Vec::from_raw_parts(result.lengths, result.count, result.count) });
        }
        starts
    }

    /// (start, length) pairs — what a caller needs to highlight a range.
    fn spans_of(result: SearchResult) -> Vec<(usize, usize)> {
        if result.positions.is_null() || result.count == 0 {
            return Vec::new();
        }
        let starts = unsafe { Vec::from_raw_parts(result.positions, result.count, result.count) };
        let lengths = unsafe { Vec::from_raw_parts(result.lengths, result.count, result.count) };
        starts.into_iter().zip(lengths).collect()
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

    // ── a literal replacement is literal, even when it contains `$` ────────
    //
    // These all silently LOST TEXT. The case-insensitive literal branch runs
    // through the regex engine, which read the replacement as a substitution
    // template: `$100` parsed as capture group 100 and expanded to nothing.
    // Match case defaults to OFF in the dialog, so this was the branch that ran
    // out of the box, and Replace All in All Open Documents repeated it across
    // every tab.
    #[test]
    fn case_insensitive_literal_replace_keeps_dollar_signs() {
        // The headline case: a price.
        assert_eq!(
            replace_all("cost = X", "X", "$100", false, false),
            "cost = $100"
        );
        // A shell/PHP variable.
        assert_eq!(replace_all("X", "X", "$var", false, false), "$var");
        // Braced form — parses as a named group.
        assert_eq!(replace_all("X", "X", "${HOME}", false, false), "${HOME}");
        // A lone trailing `$` and a doubled one.
        assert_eq!(replace_all("X", "X", "a$b", false, false), "a$b");
        assert_eq!(replace_all("X", "X", "$$", false, false), "$$");
    }

    // The case-sensitive path already used str::replace and was always literal.
    // Pinning it means the two paths can never silently diverge again.
    #[test]
    fn both_literal_replace_paths_agree_on_dollars() {
        let sensitive = replace_all("tag", "tag", "$5", false, true);
        let insensitive = replace_all("tag", "tag", "$5", false, false);
        assert_eq!(sensitive, "$5");
        assert_eq!(sensitive, insensitive);
    }

    // Regex mode is the one place `$1` SHOULD expand — that is the feature.
    #[test]
    fn regex_replace_still_expands_capture_groups() {
        assert_eq!(
            replace_all("john smith", r"(\w+) (\w+)", "$2 $1", true, true),
            "smith john"
        );
    }

    // ── offsets must index the ORIGINAL text ──────────────────────────────
    //
    // The old case-insensitive literal search lowercased the whole haystack and
    // returned offsets into that copy. str::to_lowercase is not
    // length-preserving in UTF-8, so every such character shifted every later
    // highlight. U+212A KELVIN SIGN is 3 bytes and lowercases to 'k', 1 byte.
    #[test]
    fn case_insensitive_offsets_index_the_original_string() {
        let haystack = "\u{212A}test";
        assert_eq!(haystack.len(), 3 + 4);
        let r = find_all(haystack, "test", false, false, false);
        // 3, not 1: "test" begins after the three bytes of U+212A.
        assert_eq!(positions_of(r), vec![3]);
    }

    #[test]
    fn case_insensitive_offsets_survive_a_growing_lowercase() {
        // U+0130 is 2 bytes and lowercases to 3 ("i" + U+0307), shifting the
        // other way.
        let haystack = "\u{0130}xy";
        let r = find_all(haystack, "xy", false, false, false);
        assert_eq!(positions_of(r), vec![2]);
    }

    // ── match LENGTHS, not pattern lengths ────────────────────────────────
    //
    // Mark All highlighted `needle.len()` bytes per hit. Under a regex the
    // match length has nothing to do with the pattern length, so `\d+`
    // (3 pattern bytes) painted 3 characters over a 4-digit number.
    #[test]
    fn regex_matches_report_their_own_length() {
        let r = find_all("a1234b 56 c", r"\d+", true, true, false);
        assert_eq!(spans_of(r), vec![(1, 4), (7, 2)]);
    }

    #[test]
    fn literal_matches_report_their_length() {
        let r = find_all("one two one", "one", false, true, false);
        assert_eq!(spans_of(r), vec![(0, 3), (8, 3)]);
    }

    // Case-insensitive hits can differ in byte length from the needle when the
    // matched text is a different case form, so the length must come from the
    // match itself.
    #[test]
    fn whole_word_matches_report_their_length() {
        let r = find_all("cat cathode cat", "cat", false, true, true);
        assert_eq!(spans_of(r), vec![(0, 3), (12, 3)]);
    }
}
