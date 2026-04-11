//! Cryptographic hashing — MD5, SHA-1, SHA-256, SHA-512.

use md5::Md5;
use sha1::Sha1;
use sha2::{Digest, Sha256, Sha512};

/// Compute hash. algo: 0=md5, 1=sha1, 2=sha256, 3=sha512
pub fn compute_hash(data: &[u8], algo: i32) -> String {
    match algo {
        0 => format!("{:x}", Md5::digest(data)),
        1 => format!("{:x}", Sha1::digest(data)),
        2 => format!("{:x}", Sha256::digest(data)),
        3 => format!("{:x}", Sha512::digest(data)),
        _ => String::from("unsupported algorithm"),
    }
}
