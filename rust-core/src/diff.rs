//! File compare — line-by-line diff using the Myers diff algorithm.

use libc::c_char;
use similar::{ChangeTag, TextDiff};
use std::ffi::CString;

/// A single diff hunk for FFI.
#[repr(C)]
pub struct DiffLine {
    /// 0=equal, 1=insert, 2=delete
    pub tag: i32,
    /// Line number in left file (0 if insert)
    pub left_line: i32,
    /// Line number in right file (0 if delete)
    pub right_line: i32,
    /// The text content (caller frees with npc_free_string)
    pub text: *mut c_char,
}

/// Result of a diff operation.
#[repr(C)]
pub struct DiffResult {
    pub lines: *mut DiffLine,
    pub count: usize,
    /// Summary stats
    pub added: i32,
    pub removed: i32,
    pub changed: i32,
}

pub fn compute_diff(left: &str, right: &str) -> DiffResult {
    let diff = TextDiff::from_lines(left, right);

    let mut result_lines: Vec<DiffLine> = Vec::new();
    let mut added = 0i32;
    let mut removed = 0i32;
    let mut left_line = 0i32;
    let mut right_line = 0i32;

    for change in diff.iter_all_changes() {
        let tag = match change.tag() {
            ChangeTag::Equal => {
                left_line += 1;
                right_line += 1;
                0
            }
            ChangeTag::Insert => {
                right_line += 1;
                added += 1;
                1
            }
            ChangeTag::Delete => {
                left_line += 1;
                removed += 1;
                2
            }
        };

        let text_str = change.as_str().unwrap_or("");
        let text_cstr = CString::new(text_str.trim_end_matches('\n'))
            .unwrap_or_default()
            .into_raw();

        result_lines.push(DiffLine {
            tag,
            left_line: if tag == 1 { 0 } else { left_line },
            right_line: if tag == 2 { 0 } else { right_line },
            text: text_cstr,
        });
    }

    let changed = added.min(removed);
    let count = result_lines.len();

    let mut boxed = result_lines.into_boxed_slice();
    let ptr = boxed.as_mut_ptr();
    std::mem::forget(boxed);

    DiffResult {
        lines: ptr,
        count,
        added,
        removed,
        changed,
    }
}
