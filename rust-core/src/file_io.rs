//! File I/O — memory-mapped loading, encoding detection, safe saving.

use crate::FileLoadResult;
use encoding_rs::*;
use libc::c_int;
use memmap2::Mmap;
use std::ffi::CString;
use std::fs::{self, File};
use std::io::Write;
use std::ptr;

const LARGE_THRESHOLD: u64 = 50 * 1024 * 1024;        // 50 MB — disable syntax highlighting
const HUGE_THRESHOLD: u64 = 500 * 1024 * 1024;        // 500 MB — minimal features
const MAX_EDITOR_BUFFER: u64 = 2_000_000_000;          // 2 GB — full file visibility, no truncation
const MAX_FILE_SUPPORTED: u64 = 10 * 1024 * 1024 * 1024; // 10 GB

pub fn load_file(path: &str) -> FileLoadResult {
    let metadata = match fs::metadata(path) {
        Ok(m) => m,
        Err(e) => return error(3, &format!("Cannot open: {}", e)),
    };

    let file_size = metadata.len();
    let size_mb = file_size as f64 / (1024.0 * 1024.0);
    let size_gb = file_size as f64 / (1024.0 * 1024.0 * 1024.0);

    // Open and memory-map the file (mmap is lazy — doesn't use RAM until read)
    let file = match File::open(path) {
        Ok(f) => f,
        Err(e) => return error(3, &format!("Cannot open: {}", e)),
    };

    let mmap = match unsafe { Mmap::map(&file) } {
        Ok(m) => m,
        Err(e) => return error(3, &format!("Cannot mmap: {}", e)),
    };

    // Cap what we send to the editor at MAX_EDITOR_BUFFER (QScintilla limit)
    // Files of ANY size can be opened — we just show the first 512MB
    let truncated;
    let data: &[u8] = if file_size > MAX_EDITOR_BUFFER {
        truncated = 1;
        &mmap[..MAX_EDITOR_BUFFER as usize]
    } else {
        truncated = 0;
        &mmap[..]
    };

    let sample = &data[..data.len().min(4096)];

    // Binary detection
    let null_count = sample.iter().filter(|&&b| b == 0).count();
    if null_count > sample.len() / 10 {
        let msg = format!(
            "[Binary file — {} bytes]\n\nThis file appears to be binary.",
            file_size
        );
        return make_result(&msg, "BINARY", 0, file_size, 1, 0);
    }

    // Detect encoding using encoding_rs (ICU-quality, zero-alloc detection)
    let (encoding, _confident) = detect_encoding(sample);

    // Decode
    let (text, _enc_used, _had_errors) = encoding.decode(data);

    let mut result_text = text.into_owned();

    // Add truncation notice
    if truncated != 0 {
        result_text.push_str(&format!(
            "\n\n{}\n[TRUNCATED — showing first {} MB of {:.0} MB]\n{}\n",
            "=".repeat(60),
            MAX_EDITOR_BUFFER / (1024 * 1024),
            file_size as f64 / (1024.0 * 1024.0),
            "=".repeat(60),
        ));
    }

    // Detect EOL
    let eol_mode = if sample.windows(2).any(|w| w == b"\r\n") {
        1 // CRLF
    } else if sample.contains(&b'\r') {
        2 // CR
    } else {
        0 // LF
    };

    let enc_name = encoding.name();

    make_result(&result_text, enc_name, eol_mode, file_size, 0, truncated)
}

pub fn save_file(path: &str, text: &[u8], enc_name: &str) -> c_int {
    // Resolve encoding
    let text_str = match std::str::from_utf8(text) {
        Ok(s) => s,
        Err(_) => {
            // Write raw bytes
            return match fs::write(path, text) {
                Ok(_) => 0,
                Err(_) => -1,
            };
        }
    };

    let encoding = Encoding::for_label(enc_name.as_bytes()).unwrap_or(UTF_8);

    if encoding == UTF_8 {
        match fs::write(path, text_str.as_bytes()) {
            Ok(_) => 0,
            Err(_) => -1,
        }
    } else {
        let (encoded, _, _) = encoding.encode(text_str);
        match fs::write(path, &*encoded) {
            Ok(_) => 0,
            Err(_) => -1,
        }
    }
}

fn detect_encoding(sample: &[u8]) -> (&'static Encoding, bool) {
    // BOM detection
    if sample.starts_with(&[0xEF, 0xBB, 0xBF]) {
        return (UTF_8, true);
    }
    if sample.starts_with(&[0xFF, 0xFE]) {
        return (UTF_16LE, true);
    }
    if sample.starts_with(&[0xFE, 0xFF]) {
        return (UTF_16BE, true);
    }

    // Try UTF-8 validation (fast path)
    if std::str::from_utf8(sample).is_ok() {
        return (UTF_8, true);
    }

    // Fallback: try common encodings
    for enc in &[WINDOWS_1252, SHIFT_JIS, EUC_JP, GB18030] {
        let (_, _, had_errors) = enc.decode(sample);
        if !had_errors {
            return (*enc, false);
        }
    }

    (UTF_8, false)
}

fn make_result(
    text: &str,
    enc: &str,
    eol: c_int,
    size: u64,
    status: c_int,
    truncated: c_int,
) -> FileLoadResult {
    let text_len = text.len();
    FileLoadResult {
        text: CString::new(text).unwrap_or_default().into_raw(),
        text_len,
        encoding: CString::new(enc).unwrap_or_default().into_raw(),
        eol_mode: eol,
        file_size: size,
        status,
        error_msg: ptr::null_mut(),
        truncated,
    }
}

fn error(status: c_int, msg: &str) -> FileLoadResult {
    FileLoadResult {
        text: ptr::null_mut(),
        text_len: 0,
        encoding: ptr::null_mut(),
        eol_mode: 0,
        file_size: 0,
        status,
        error_msg: CString::new(msg).unwrap_or_default().into_raw(),
        truncated: 0,
    }
}
