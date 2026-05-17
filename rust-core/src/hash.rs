// SPDX-License-Identifier: GPL-3.0-or-later

//! Cryptographic hashing — MD5, SHA-1, SHA-256, SHA-512.

use md5::Md5;
use sha1::Sha1;
use sha2::{Digest, Sha256, Sha512};

fn hex_encode(bytes: &[u8]) -> String {
    let mut s = String::with_capacity(bytes.len() * 2);
    for b in bytes {
        use std::fmt::Write;
        let _ = write!(s, "{:02x}", b);
    }
    s
}

/// Compute hash. algo: 0=md5, 1=sha1, 2=sha256, 3=sha512
pub fn compute_hash(data: &[u8], algo: i32) -> String {
    match algo {
        0 => hex_encode(Md5::digest(data).as_slice()),
        1 => hex_encode(Sha1::digest(data).as_slice()),
        2 => hex_encode(Sha256::digest(data).as_slice()),
        3 => hex_encode(Sha512::digest(data).as_slice()),
        _ => String::from("unsupported algorithm"),
    }
}
