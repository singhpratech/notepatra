//! Notepad++ Linux — Rust core library
//!
//! Memory-safe backend for file I/O, text processing, encoding, search, hashing.
//! Exposed to C++ via `extern "C"` FFI.
#![allow(clippy::missing_safety_doc)]

mod bracket_fix;
mod diff;
mod encoding;
mod file_io;
mod hash;
mod html_fmt;
mod json_fmt;
mod search;
mod sql_fmt;
mod text_ops;

use libc::{c_char, c_int, size_t};
use std::ffi::{CStr, CString};
use std::ptr;
use std::slice;

// ═══════════════════════════════════════════════════════════
// FFI result types
// ═══════════════════════════════════════════════════════════

/// Result of loading a file.
#[repr(C)]
pub struct FileLoadResult {
    /// UTF-8 text content (caller must free with npc_free_string)
    pub text: *mut c_char,
    /// Length of text in bytes
    pub text_len: size_t,
    /// Detected encoding name (caller must free with npc_free_string)
    pub encoding: *mut c_char,
    /// EOL mode: 0=Unix(LF), 1=Windows(CRLF), 2=Mac(CR)
    pub eol_mode: c_int,
    /// File size in bytes
    pub file_size: u64,
    /// 0=ok, 1=binary, 2=too_large, 3=error, 4=oom
    pub status: c_int,
    /// Error message if status != 0 (caller must free with npc_free_string)
    pub error_msg: *mut c_char,
    /// 1 if file was truncated due to size
    pub truncated: c_int,
}

/// Result of a search operation.
#[repr(C)]
pub struct SearchResult {
    /// Array of match positions (byte offsets), caller frees with npc_free_matches
    pub positions: *mut size_t,
    /// Number of matches
    pub count: size_t,
}

/// Result of a text operation.
#[repr(C)]
pub struct TextResult {
    pub text: *mut c_char,
    pub text_len: size_t,
}

/// Result of a hash operation.
#[repr(C)]
pub struct HashResult {
    pub hex: *mut c_char,
}

// ═══════════════════════════════════════════════════════════
// File I/O
// ═══════════════════════════════════════════════════════════

/// Load a file with memory mapping for large files.
/// Returns FileLoadResult. Caller must free strings with npc_free_string.
#[no_mangle]
pub unsafe extern "C" fn npc_load_file(path: *const c_char) -> FileLoadResult {
    let path = match unsafe { CStr::from_ptr(path) }.to_str() {
        Ok(s) => s,
        Err(_) => return error_result("Invalid path encoding"),
    };
    file_io::load_file(path)
}

/// Save text to a file.
/// Returns 0 on success, -1 on error.
#[no_mangle]
pub unsafe extern "C" fn npc_save_file(
    path: *const c_char,
    text: *const c_char,
    text_len: size_t,
    encoding: *const c_char,
) -> c_int {
    let path = match unsafe { CStr::from_ptr(path) }.to_str() {
        Ok(s) => s,
        Err(_) => return -1,
    };
    let text_bytes = unsafe { slice::from_raw_parts(text as *const u8, text_len) };
    let enc = unsafe { CStr::from_ptr(encoding) }
        .to_str()
        .unwrap_or("UTF-8");
    file_io::save_file(path, text_bytes, enc)
}

// ═══════════════════════════════════════════════════════════
// Text operations
// ═══════════════════════════════════════════════════════════

/// Sort lines. mode: 0=asc, 1=desc, 2=int_asc, 3=int_desc, 4=length_asc, 5=length_desc
#[no_mangle]
pub unsafe extern "C" fn npc_sort_lines(
    text: *const c_char,
    text_len: size_t,
    mode: c_int,
) -> TextResult {
    let input = unsafe { str_from_raw(text, text_len) };
    let result = text_ops::sort_lines(input, mode);
    text_to_result(result)
}

/// Remove duplicate lines. mode: 0=all, 1=consecutive
#[no_mangle]
pub unsafe extern "C" fn npc_remove_duplicates(
    text: *const c_char,
    text_len: size_t,
    mode: c_int,
) -> TextResult {
    let input = unsafe { str_from_raw(text, text_len) };
    let result = text_ops::remove_duplicates(input, mode);
    text_to_result(result)
}

/// Remove empty lines. mode: 0=empty, 1=blank(whitespace-only)
#[no_mangle]
pub unsafe extern "C" fn npc_remove_empty_lines(
    text: *const c_char,
    text_len: size_t,
    mode: c_int,
) -> TextResult {
    let input = unsafe { str_from_raw(text, text_len) };
    let result = text_ops::remove_empty_lines(input, mode);
    text_to_result(result)
}

/// Trim lines. mode: 0=trailing, 1=leading, 2=both
#[no_mangle]
pub unsafe extern "C" fn npc_trim_lines(
    text: *const c_char,
    text_len: size_t,
    mode: c_int,
) -> TextResult {
    let input = unsafe { str_from_raw(text, text_len) };
    let result = text_ops::trim_lines(input, mode);
    text_to_result(result)
}

/// Reverse line order.
#[no_mangle]
pub unsafe extern "C" fn npc_reverse_lines(text: *const c_char, text_len: size_t) -> TextResult {
    let input = unsafe { str_from_raw(text, text_len) };
    let result = text_ops::reverse_lines(input);
    text_to_result(result)
}

/// Join lines with a separator.
#[no_mangle]
pub unsafe extern "C" fn npc_join_lines(
    text: *const c_char,
    text_len: size_t,
    separator: *const c_char,
) -> TextResult {
    let input = unsafe { str_from_raw(text, text_len) };
    let sep = unsafe { CStr::from_ptr(separator) }.to_str().unwrap_or(" ");
    let result = text_ops::join_lines(input, sep);
    text_to_result(result)
}

/// Convert case. mode: 0=upper, 1=lower, 2=title, 3=sentence, 4=invert
#[no_mangle]
pub unsafe extern "C" fn npc_convert_case(
    text: *const c_char,
    text_len: size_t,
    mode: c_int,
) -> TextResult {
    let input = unsafe { str_from_raw(text, text_len) };
    let result = text_ops::convert_case(input, mode);
    text_to_result(result)
}

/// Convert tabs to spaces or vice versa. mode: 0=tab_to_space, 1=space_to_tab
#[no_mangle]
pub unsafe extern "C" fn npc_convert_whitespace(
    text: *const c_char,
    text_len: size_t,
    tab_width: c_int,
    mode: c_int,
) -> TextResult {
    let input = unsafe { str_from_raw(text, text_len) };
    let result = text_ops::convert_whitespace(input, tab_width as usize, mode);
    text_to_result(result)
}

// ═══════════════════════════════════════════════════════════
// Search
// ═══════════════════════════════════════════════════════════

/// Find all matches. Returns byte offset positions.
/// is_regex: 1=regex, 0=literal. case_sensitive: 1=yes, 0=no. whole_word: 1=yes, 0=no.
#[no_mangle]
pub unsafe extern "C" fn npc_find_all(
    text: *const c_char,
    text_len: size_t,
    pattern: *const c_char,
    is_regex: c_int,
    case_sensitive: c_int,
    whole_word: c_int,
) -> SearchResult {
    let haystack = unsafe { str_from_raw(text, text_len) };
    let needle = match unsafe { CStr::from_ptr(pattern) }.to_str() {
        Ok(s) => s,
        Err(_) => {
            return SearchResult {
                positions: ptr::null_mut(),
                count: 0,
            }
        }
    };
    search::find_all(
        haystack,
        needle,
        is_regex != 0,
        case_sensitive != 0,
        whole_word != 0,
    )
}

/// Count matches.
#[no_mangle]
pub unsafe extern "C" fn npc_count_matches(
    text: *const c_char,
    text_len: size_t,
    pattern: *const c_char,
    is_regex: c_int,
    case_sensitive: c_int,
) -> size_t {
    let haystack = unsafe { str_from_raw(text, text_len) };
    let needle = match unsafe { CStr::from_ptr(pattern) }.to_str() {
        Ok(s) => s,
        Err(_) => return 0,
    };
    search::count_matches(haystack, needle, is_regex != 0, case_sensitive != 0)
}

/// Replace all matches.
#[no_mangle]
pub unsafe extern "C" fn npc_replace_all(
    text: *const c_char,
    text_len: size_t,
    pattern: *const c_char,
    replacement: *const c_char,
    is_regex: c_int,
    case_sensitive: c_int,
) -> TextResult {
    let haystack = unsafe { str_from_raw(text, text_len) };
    let needle = match unsafe { CStr::from_ptr(pattern) }.to_str() {
        Ok(s) => s,
        Err(_) => return text_to_result(haystack.to_string()),
    };
    let repl = match unsafe { CStr::from_ptr(replacement) }.to_str() {
        Ok(s) => s,
        Err(_) => return text_to_result(haystack.to_string()),
    };
    let result = search::replace_all(haystack, needle, repl, is_regex != 0, case_sensitive != 0);
    text_to_result(result)
}

// ═══════════════════════════════════════════════════════════
// Hashing
// ═══════════════════════════════════════════════════════════

/// Compute hash. algo: 0=md5, 1=sha1, 2=sha256, 3=sha512
#[no_mangle]
pub unsafe extern "C" fn npc_hash(
    data: *const c_char,
    data_len: size_t,
    algo: c_int,
) -> HashResult {
    let bytes = unsafe { slice::from_raw_parts(data as *const u8, data_len) };
    let hex = hash::compute_hash(bytes, algo);
    HashResult {
        hex: CString::new(hex).unwrap_or_default().into_raw(),
    }
}

/// Base64 encode.
#[no_mangle]
pub unsafe extern "C" fn npc_base64_encode(data: *const c_char, data_len: size_t) -> TextResult {
    let bytes = unsafe { slice::from_raw_parts(data as *const u8, data_len) };
    let result = encoding::base64_encode(bytes);
    text_to_result(result)
}

/// Base64 decode.
#[no_mangle]
pub unsafe extern "C" fn npc_base64_decode(data: *const c_char, data_len: size_t) -> TextResult {
    let bytes = unsafe { slice::from_raw_parts(data as *const u8, data_len) };
    let result = encoding::base64_decode(bytes);
    text_to_result(result)
}

/// URL encode.
#[no_mangle]
pub unsafe extern "C" fn npc_url_encode(text: *const c_char, text_len: size_t) -> TextResult {
    let input = unsafe { str_from_raw(text, text_len) };
    text_to_result(encoding::url_encode(input))
}

/// URL decode.
#[no_mangle]
pub unsafe extern "C" fn npc_url_decode(text: *const c_char, text_len: size_t) -> TextResult {
    let input = unsafe { str_from_raw(text, text_len) };
    text_to_result(encoding::url_decode(input))
}

// ═══════════════════════════════════════════════════════════
// Diff
// ═══════════════════════════════════════════════════════════

pub use diff::{DiffLine, DiffResult};

/// Compute line diff between two texts.
#[no_mangle]
pub unsafe extern "C" fn npc_diff(
    left: *const c_char,
    left_len: size_t,
    right: *const c_char,
    right_len: size_t,
) -> DiffResult {
    let l = unsafe { str_from_raw(left, left_len) };
    let r = unsafe { str_from_raw(right, right_len) };
    diff::compute_diff(l, r)
}

/// Free a DiffResult.
#[no_mangle]
pub unsafe extern "C" fn npc_free_diff(result: DiffResult) {
    if !result.lines.is_null() && result.count > 0 {
        unsafe {
            let slice = Vec::from_raw_parts(result.lines, result.count, result.count);
            for line in &slice {
                if !line.text.is_null() {
                    let _ = CString::from_raw(line.text);
                }
            }
        }
    }
}

// ═══════════════════════════════════════════════════════════
// SQL Formatter
// ═══════════════════════════════════════════════════════════

/// Format SQL. indent_width: spaces per indent. uppercase: 1=yes, 0=no.
/// dialect: "ansi" | "postgres" | "mysql" | "mssql" | "sqlite" | "plsql"
/// (NULL or empty = "ansi").
#[no_mangle]
pub unsafe extern "C" fn npc_format_sql(
    text: *const c_char,
    text_len: size_t,
    indent_width: c_int,
    uppercase: c_int,
    dialect: *const c_char,
) -> TextResult {
    let input = unsafe { str_from_raw(text, text_len) };
    let dialect_str = if dialect.is_null() {
        "ansi"
    } else {
        unsafe { CStr::from_ptr(dialect) }
            .to_str()
            .unwrap_or("ansi")
    };
    let result =
        sql_fmt::format_sql_dialect(input, indent_width as usize, uppercase != 0, dialect_str);
    text_to_result(result)
}

/// v0.1.49 — Compact / one-line-where-possible SQL formatter. Same dialect
/// support as `npc_format_sql`; only the line-break policy differs. Useful
/// when the user wants a tight rendering instead of the Claude-style
/// expanded form.
#[no_mangle]
pub unsafe extern "C" fn npc_format_sql_compact(
    text: *const c_char,
    text_len: size_t,
    indent_width: c_int,
    uppercase: c_int,
    dialect: *const c_char,
) -> TextResult {
    let input = unsafe { str_from_raw(text, text_len) };
    let dialect_str = if dialect.is_null() {
        "ansi"
    } else {
        unsafe { CStr::from_ptr(dialect) }
            .to_str()
            .unwrap_or("ansi")
    };
    let result =
        sql_fmt::format_sql_compact(input, indent_width as usize, uppercase != 0, dialect_str);
    text_to_result(result)
}

// ═══════════════════════════════════════════════════════════
// JSON Formatter + Fixer
// ═══════════════════════════════════════════════════════════

#[no_mangle]
pub unsafe extern "C" fn npc_format_json(
    text: *const c_char,
    text_len: size_t,
    indent: c_int,
) -> TextResult {
    let input = unsafe { str_from_raw(text, text_len) };
    text_to_result(json_fmt::format_json(input, indent as usize))
}

#[no_mangle]
pub unsafe extern "C" fn npc_minify_json(text: *const c_char, text_len: size_t) -> TextResult {
    let input = unsafe { str_from_raw(text, text_len) };
    text_to_result(json_fmt::minify_json(input))
}

#[no_mangle]
pub unsafe extern "C" fn npc_fix_json(text: *const c_char, text_len: size_t) -> TextResult {
    let input = unsafe { str_from_raw(text, text_len) };
    text_to_result(json_fmt::fix_json(input))
}

/// Fix JSON and return a report of what was fixed.
/// Returns the report (not the fixed JSON — use npc_fix_json for the fixed text).
#[no_mangle]
pub unsafe extern "C" fn npc_fix_json_report(text: *const c_char, text_len: size_t) -> TextResult {
    let input = unsafe { str_from_raw(text, text_len) };
    let (_fixed, report) = json_fmt::fix_json_with_report(input);
    text_to_result(report)
}

// ═══════════════════════════════════════════════════════════
// HTML Formatter
// ═══════════════════════════════════════════════════════════

#[no_mangle]
pub unsafe extern "C" fn npc_format_html(
    text: *const c_char,
    text_len: size_t,
    indent: c_int,
) -> TextResult {
    let input = unsafe { str_from_raw(text, text_len) };
    text_to_result(html_fmt::format_html(input, indent as usize))
}

// ═══════════════════════════════════════════════════════════
// Bracket Fixer
// ═══════════════════════════════════════════════════════════

#[no_mangle]
pub unsafe extern "C" fn npc_fix_brackets(text: *const c_char, text_len: size_t) -> TextResult {
    let input = unsafe { str_from_raw(text, text_len) };
    text_to_result(bracket_fix::fix_brackets(input))
}

#[no_mangle]
pub unsafe extern "C" fn npc_check_brackets(text: *const c_char, text_len: size_t) -> TextResult {
    let input = unsafe { str_from_raw(text, text_len) };
    text_to_result(bracket_fix::check_brackets(input))
}

// ═══════════════════════════════════════════════════════════
// Memory management — C++ must call these to free Rust allocations
// ═══════════════════════════════════════════════════════════

#[no_mangle]
pub unsafe extern "C" fn npc_free_string(s: *mut c_char) {
    if !s.is_null() {
        unsafe {
            let _ = CString::from_raw(s);
        }
    }
}

/// Free a buffer allocated by `npc_load_file` for the FileLoadResult.text
/// field. v0.1.87 — file_io.rs no longer round-trips through CString
/// (saves 118 MB heap allocation + O(N) NUL-scan on a 118 MB file). The
/// text buffer is a `Box<[u8]>` so we must reclaim it as such, not via
/// CString::from_raw which assumes NUL-termination and would mis-compute
/// the size.
#[no_mangle]
pub unsafe extern "C" fn npc_free_file_text(text: *mut c_char, text_len: size_t) {
    if !text.is_null() && text_len > 0 {
        unsafe {
            let _ = Box::from_raw(ptr::slice_from_raw_parts_mut(text as *mut u8, text_len));
        }
    }
}

#[no_mangle]
pub unsafe extern "C" fn npc_free_matches(result: SearchResult) {
    if !result.positions.is_null() && result.count > 0 {
        unsafe {
            let _ = Vec::from_raw_parts(result.positions, result.count, result.count);
        }
    }
}

#[no_mangle]
pub unsafe extern "C" fn npc_free_text_result(result: TextResult) {
    npc_free_string(result.text);
}

// ═══════════════════════════════════════════════════════════
// Internal helpers
// ═══════════════════════════════════════════════════════════

unsafe fn str_from_raw<'a>(ptr: *const c_char, len: size_t) -> &'a str {
    let bytes = slice::from_raw_parts(ptr as *const u8, len);
    std::str::from_utf8_unchecked(bytes)
}

fn text_to_result(s: String) -> TextResult {
    let len = s.len();
    let cstr = CString::new(s).unwrap_or_default();
    TextResult {
        text: cstr.into_raw(),
        text_len: len,
    }
}

fn error_result(msg: &str) -> FileLoadResult {
    FileLoadResult {
        text: ptr::null_mut(),
        text_len: 0,
        encoding: ptr::null_mut(),
        eol_mode: 0,
        file_size: 0,
        status: 3,
        error_msg: CString::new(msg).unwrap_or_default().into_raw(),
        truncated: 0,
    }
}
