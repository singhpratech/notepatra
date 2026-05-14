//! File I/O — memory-mapped loading, encoding detection, safe saving.

use crate::FileLoadResult;
use encoding_rs::*;
use libc::c_int;
use memmap2::Mmap;
use std::ffi::CString;
use std::fs::{self, File};
use std::ptr;

const MAX_EDITOR_BUFFER: u64 = 2_000_000_000; // 2 GB — full file visibility, no truncation
const MAX_FILE_SUPPORTED: u64 = 10 * 1024 * 1024 * 1024; // 10 GB

// v0.1.78 — encoding detection rebuilt to match Notepad++ behaviour:
//   - BOM detection runs BEFORE the null-byte binary heuristic so UTF-16 text
//     (50% nulls by design) is no longer flagged as binary.
//   - UTF-32 LE/BE BOM is detected first (its 4-byte BOM 'FF FE 00 00' would
//     otherwise be mis-matched against UTF-16 LE BOM 'FF FE').
//   - No-BOM UTF-16 is sniffed via even/odd null-column ratio (PowerShell
//     Out-File and some Java exports produce BOM-less UTF-16).
//   - Encoding labels round-trip through the C++ save path with BOM intact
//     ("UTF-16 LE BOM" / "UTF-16 BE BOM" / "UTF-32 LE BOM" / "UTF-32 BE BOM").
#[derive(Clone, Copy, PartialEq, Eq, Debug)]
enum DetectedEnc {
    Utf8,
    Utf8Bom,
    Utf16LeBom,
    Utf16BeBom,
    Utf16LeNoBom,
    Utf16BeNoBom,
    Utf32LeBom,
    Utf32BeBom,
    Other(&'static Encoding),
}

impl DetectedEnc {
    fn label(self) -> &'static str {
        match self {
            DetectedEnc::Utf8 => "UTF-8",
            DetectedEnc::Utf8Bom => "UTF-8 BOM",
            DetectedEnc::Utf16LeBom => "UTF-16 LE BOM",
            DetectedEnc::Utf16BeBom => "UTF-16 BE BOM",
            DetectedEnc::Utf16LeNoBom => "UTF-16 LE",
            DetectedEnc::Utf16BeNoBom => "UTF-16 BE",
            DetectedEnc::Utf32LeBom => "UTF-32 LE BOM",
            DetectedEnc::Utf32BeBom => "UTF-32 BE BOM",
            DetectedEnc::Other(e) => e.name(),
        }
    }
}

pub fn load_file(path: &str) -> FileLoadResult {
    let metadata = match fs::metadata(path) {
        Ok(m) => m,
        Err(e) => return error(3, &format!("Cannot open: {}", e)),
    };

    let file_size = metadata.len();
    if file_size > MAX_FILE_SUPPORTED {
        return error(2, "File is larger than the 10 GB safety limit.");
    }

    // Open and memory-map the file (mmap is lazy — doesn't use RAM until read)
    let file = match File::open(path) {
        Ok(f) => f,
        Err(e) => return error(3, &format!("Cannot open: {}", e)),
    };

    // Zero-byte file: mmap of length 0 fails on some platforms. Short-circuit
    // with an empty buffer so empty files open cleanly.
    if file_size == 0 {
        return make_result("", "UTF-8", 0, 0, 0, 0);
    }

    let mmap = match unsafe { Mmap::map(&file) } {
        Ok(m) => m,
        Err(e) => return error(3, &format!("Cannot mmap: {}", e)),
    };

    // Cap what we send to the editor at MAX_EDITOR_BUFFER (QScintilla limit)
    // Files of ANY size can be opened — we just show the first 2GB
    let truncated;
    let data: &[u8] = if file_size > MAX_EDITOR_BUFFER {
        truncated = 1;
        &mmap[..MAX_EDITOR_BUFFER as usize]
    } else {
        truncated = 0;
        &mmap[..]
    };

    let sample = &data[..data.len().min(4096)];

    // Detect encoding FIRST so the binary check can respect text-file BOMs
    // and UTF-16/32 column-of-nulls patterns. Pre-v0.1.78 the null-byte
    // heuristic ran first and mis-flagged every UTF-16 text file as binary.
    let detected = detect_encoding(sample);

    // Binary detection — skipped for UTF-16 / UTF-32 because their text legitimately
    // contains 0x00 in every-other / three-of-four bytes for ASCII content.
    let is_text_with_nulls = matches!(
        detected,
        DetectedEnc::Utf16LeBom
            | DetectedEnc::Utf16BeBom
            | DetectedEnc::Utf16LeNoBom
            | DetectedEnc::Utf16BeNoBom
            | DetectedEnc::Utf32LeBom
            | DetectedEnc::Utf32BeBom
    );
    if !is_text_with_nulls {
        let null_count = sample.iter().filter(|&&b| b == 0).count();
        if null_count > sample.len() / 10 {
            let msg = format!(
                "[Binary file — {} bytes]\n\nThis file appears to be binary.",
                file_size
            );
            return make_result(&msg, "BINARY", 0, file_size, 1, 0);
        }
    }

    let result_text = decode_with(detected, data);

    let mut result_text = result_text;

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

    // Detect EOL — on UTF-16/32 the sample has interleaved nulls so naive
    // byte-windowing wouldn't match; check the decoded text instead.
    let eol_mode = if result_text.contains("\r\n") {
        1 // CRLF
    } else if result_text.contains('\r') {
        2 // CR
    } else {
        0 // LF
    };

    make_result(
        &result_text,
        detected.label(),
        eol_mode,
        file_size,
        0,
        truncated,
    )
}

pub fn save_file(path: &str, text: &[u8], enc_name: &str) -> c_int {
    // v0.1.78 — the C++ saveFile in editor.cpp now handles every encoding
    // including BOM-prefixed UTF-16 / UTF-32 directly, so this Rust path is
    // only kept as the UTF-8 fast path / fallback.
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

fn detect_encoding(sample: &[u8]) -> DetectedEnc {
    // UTF-32 BOMs are 4 bytes — must be checked FIRST because UTF-32 LE BOM
    // (FF FE 00 00) shares its first 2 bytes with UTF-16 LE BOM (FF FE).
    if sample.starts_with(&[0xFF, 0xFE, 0x00, 0x00]) {
        return DetectedEnc::Utf32LeBom;
    }
    if sample.starts_with(&[0x00, 0x00, 0xFE, 0xFF]) {
        return DetectedEnc::Utf32BeBom;
    }

    // 3-byte and 2-byte BOMs
    if sample.starts_with(&[0xEF, 0xBB, 0xBF]) {
        return DetectedEnc::Utf8Bom;
    }
    if sample.starts_with(&[0xFF, 0xFE]) {
        return DetectedEnc::Utf16LeBom;
    }
    if sample.starts_with(&[0xFE, 0xFF]) {
        return DetectedEnc::Utf16BeBom;
    }

    // No-BOM UTF-16 sniff runs BEFORE the UTF-8 fast path because UTF-16 LE
    // ASCII bytes (e.g. 0x53 0x00 0x45 0x00 …) accidentally parse as valid
    // UTF-8 (NUL is a legal UTF-8 byte), which would mis-classify them as
    // UTF-8 with embedded NULs and then trip the binary check downstream.
    //
    // ASCII-dominant UTF-16 LE has 0x00 at every odd byte (positions 1,3,5,…)
    // and non-zero at every even byte; UTF-16 BE is the mirror. Matches
    // Notepad++/uchardet behaviour for BOM-less PowerShell /
    // Java -Dfile.encoding=UTF-16 output.
    if sample.len() >= 32 {
        let (mut even_nulls, mut odd_nulls) = (0usize, 0usize);
        for (i, &b) in sample.iter().enumerate() {
            if b == 0 {
                if i % 2 == 0 {
                    even_nulls += 1;
                } else {
                    odd_nulls += 1;
                }
            }
        }
        let half = sample.len() / 2;
        if half > 0 {
            let odd_ratio = odd_nulls as f64 / half as f64;
            let even_ratio = even_nulls as f64 / half as f64;
            if odd_ratio > 0.8 && even_ratio < 0.1 {
                return DetectedEnc::Utf16LeNoBom;
            }
            if even_ratio > 0.8 && odd_ratio < 0.1 {
                return DetectedEnc::Utf16BeNoBom;
            }
        }
    }

    // Pure UTF-8 (no BOM). Runs AFTER the UTF-16 sniff so that UTF-16 LE
    // ASCII bytes (which happen to satisfy from_utf8) don't get mis-claimed.
    if std::str::from_utf8(sample).is_ok() {
        return DetectedEnc::Utf8;
    }

    // Fallback: try common 8-bit encodings
    for enc in &[WINDOWS_1252, SHIFT_JIS, EUC_JP, GB18030] {
        let (_, _, had_errors) = enc.decode(sample);
        if !had_errors {
            return DetectedEnc::Other(*enc);
        }
    }

    DetectedEnc::Other(UTF_8)
}

fn decode_with(det: DetectedEnc, data: &[u8]) -> String {
    match det {
        DetectedEnc::Utf8 | DetectedEnc::Utf8Bom => {
            // encoding_rs UTF_8.decode strips the BOM automatically.
            let (text, _, _) = UTF_8.decode(data);
            text.into_owned()
        }
        DetectedEnc::Utf16LeBom | DetectedEnc::Utf16LeNoBom => {
            let (text, _, _) = UTF_16LE.decode(data);
            text.into_owned()
        }
        DetectedEnc::Utf16BeBom | DetectedEnc::Utf16BeNoBom => {
            let (text, _, _) = UTF_16BE.decode(data);
            text.into_owned()
        }
        DetectedEnc::Utf32LeBom => decode_utf32_le(&data[4..]),
        DetectedEnc::Utf32BeBom => decode_utf32_be(&data[4..]),
        DetectedEnc::Other(enc) => {
            let (text, _, _) = enc.decode(data);
            text.into_owned()
        }
    }
}

fn decode_utf32_le(data: &[u8]) -> String {
    data.chunks_exact(4)
        .filter_map(|c| {
            let cp = u32::from_le_bytes([c[0], c[1], c[2], c[3]]);
            char::from_u32(cp)
        })
        .collect()
}

fn decode_utf32_be(data: &[u8]) -> String {
    data.chunks_exact(4)
        .filter_map(|c| {
            let cp = u32::from_be_bytes([c[0], c[1], c[2], c[3]]);
            char::from_u32(cp)
        })
        .collect()
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

#[cfg(test)]
mod tests {
    use super::*;
    use std::io::Write;
    use tempfile::NamedTempFile;

    fn write_tmp(bytes: &[u8]) -> NamedTempFile {
        let mut f = NamedTempFile::new().unwrap();
        f.write_all(bytes).unwrap();
        f.flush().unwrap();
        f
    }

    fn label_of(text_bytes: &[u8]) -> String {
        let f = write_tmp(text_bytes);
        let r = load_file(f.path().to_str().unwrap());
        unsafe {
            std::ffi::CStr::from_ptr(r.encoding)
                .to_string_lossy()
                .into_owned()
        }
    }

    fn text_of(text_bytes: &[u8]) -> String {
        let f = write_tmp(text_bytes);
        let r = load_file(f.path().to_str().unwrap());
        unsafe {
            std::ffi::CStr::from_ptr(r.text)
                .to_string_lossy()
                .into_owned()
        }
    }

    fn status_of(text_bytes: &[u8]) -> c_int {
        let f = write_tmp(text_bytes);
        load_file(f.path().to_str().unwrap()).status
    }

    #[test]
    fn utf8_plain() {
        assert_eq!(label_of(b"hello\nworld\n"), "UTF-8");
        assert_eq!(text_of(b"hello\nworld\n"), "hello\nworld\n");
    }

    #[test]
    fn utf8_with_bom() {
        let mut bytes = b"\xEF\xBB\xBF".to_vec();
        bytes.extend_from_slice(b"USE [master]\nGO\n");
        assert_eq!(label_of(&bytes), "UTF-8 BOM");
        assert_eq!(text_of(&bytes), "USE [master]\nGO\n");
    }

    #[test]
    fn utf16_le_bom_sql_export() {
        // Mirror of SQL Server Generate-Scripts default: UTF-16 LE BOM + CRLF.
        let mut bytes = vec![0xFF, 0xFE];
        for ch in "USE [master]\r\nGO\r\n".chars() {
            let code = ch as u32 as u16;
            bytes.push((code & 0xFF) as u8);
            bytes.push((code >> 8) as u8);
        }
        assert_eq!(label_of(&bytes), "UTF-16 LE BOM");
        assert_eq!(text_of(&bytes), "USE [master]\r\nGO\r\n");
        assert_eq!(status_of(&bytes), 0, "must NOT be flagged as binary");
    }

    #[test]
    fn utf16_be_bom() {
        let mut bytes = vec![0xFE, 0xFF];
        for ch in "hello".chars() {
            let code = ch as u32 as u16;
            bytes.push((code >> 8) as u8);
            bytes.push((code & 0xFF) as u8);
        }
        assert_eq!(label_of(&bytes), "UTF-16 BE BOM");
        assert_eq!(text_of(&bytes), "hello");
    }

    #[test]
    fn utf16_le_no_bom_sniffed() {
        // 32+ bytes of ASCII text encoded UTF-16 LE without a BOM.
        let mut bytes = Vec::new();
        for ch in "SELECT * FROM users WHERE id = 1;".chars() {
            let code = ch as u32 as u16;
            bytes.push((code & 0xFF) as u8);
            bytes.push((code >> 8) as u8);
        }
        assert_eq!(label_of(&bytes), "UTF-16 LE");
        assert!(text_of(&bytes).contains("SELECT"));
    }

    #[test]
    fn utf16_be_no_bom_sniffed() {
        let mut bytes = Vec::new();
        for ch in "SELECT * FROM users WHERE id = 1;".chars() {
            let code = ch as u32 as u16;
            bytes.push((code >> 8) as u8);
            bytes.push((code & 0xFF) as u8);
        }
        assert_eq!(label_of(&bytes), "UTF-16 BE");
    }

    #[test]
    fn utf32_le_bom() {
        let mut bytes = vec![0xFF, 0xFE, 0x00, 0x00];
        for ch in "abc".chars() {
            let cp = ch as u32;
            bytes.extend_from_slice(&cp.to_le_bytes());
        }
        assert_eq!(label_of(&bytes), "UTF-32 LE BOM");
        assert_eq!(text_of(&bytes), "abc");
    }

    #[test]
    fn utf32_be_bom() {
        let mut bytes = vec![0x00, 0x00, 0xFE, 0xFF];
        for ch in "abc".chars() {
            let cp = ch as u32;
            bytes.extend_from_slice(&cp.to_be_bytes());
        }
        assert_eq!(label_of(&bytes), "UTF-32 BE BOM");
        assert_eq!(text_of(&bytes), "abc");
    }

    #[test]
    fn real_binary_still_refused() {
        // PNG magic + IHDR-style bytes — well above 10% nulls, no text BOM.
        let png_sig = [
            0x89, 0x50, 0x4E, 0x47, 0x0D, 0x0A, 0x1A, 0x0A, 0x00, 0x00, 0x00, 0x0D, 0x49, 0x48,
            0x44, 0x52, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x01, 0x00, 0x08, 0x06, 0x00, 0x00,
            0x00, 0x1F, 0x15, 0xC4,
        ];
        let mut bytes = Vec::new();
        for _ in 0..40 {
            bytes.extend_from_slice(&png_sig);
        }
        assert_eq!(status_of(&bytes), 1, "real binary must still be flagged");
        assert_eq!(label_of(&bytes), "BINARY");
    }

    #[test]
    fn empty_file_opens_clean() {
        assert_eq!(status_of(b""), 0);
        assert_eq!(label_of(b""), "UTF-8");
        assert_eq!(text_of(b""), "");
    }

    #[test]
    fn windows_1252_fallback() {
        // Latin-1 / Windows-1252 bytes that are not valid UTF-8 (0xE9 = é).
        let bytes = b"caf\xE9 menu\n";
        let label = label_of(bytes);
        assert!(
            label == "windows-1252" || label == "Windows-1252",
            "got {}",
            label
        );
    }
}
