// SPDX-License-Identifier: GPL-3.0-or-later

//! SSH key generation + OS-entropy draws, exposed to C++ via FFI.
//!
//! All randomness comes from `rand_core::OsRng` (getrandom → the OS CSPRNG).
//! Pure Rust (RustCrypto `ssh-key`); no shelling out to `ssh-keygen`.

use libc::{c_char, c_int, size_t};
use rand_core::{OsRng, RngCore};
use ssh_key::private::{PrivateKey, RsaKeypair};
use ssh_key::{Algorithm, EcdsaCurve, HashAlg, LineEnding};
use std::ffi::{CStr, CString};
use std::panic::{catch_unwind, AssertUnwindSafe};
use std::ptr;
use zeroize::Zeroize;

/// Key material handed to C++. See notepad_core.h for the contract.
#[repr(C)]
pub struct SshKeyResult {
    pub ok: c_int,
    pub private_pem: *mut c_char,
    pub private_len: size_t,
    pub public_line: *mut c_char,
    pub public_len: size_t,
    pub fingerprint: *mut c_char,
    pub fingerprint_len: size_t,
    pub error_msg: *mut c_char,
}

// Alg selector, mirrored in the C++ combo box order.
const ALG_ED25519: c_int = 0;
const ALG_P256: c_int = 1;
const ALG_P384: c_int = 2;
const ALG_RSA: c_int = 3;

fn empty_result() -> SshKeyResult {
    SshKeyResult {
        ok: 0,
        private_pem: ptr::null_mut(),
        private_len: 0,
        public_line: ptr::null_mut(),
        public_len: 0,
        fingerprint: ptr::null_mut(),
        fingerprint_len: 0,
        error_msg: ptr::null_mut(),
    }
}

fn err_result(msg: &str) -> SshKeyResult {
    let mut r = empty_result();
    r.error_msg = CString::new(msg).unwrap_or_default().into_raw();
    r
}

// Hand a byte buffer to C++ as a Box<[u8]> + explicit length, like
// npc_load_file does — never a CString (no O(N) NUL scan, no truncation).
fn alloc_bytes(src: &[u8]) -> (*mut c_char, size_t) {
    let boxed = src.to_vec().into_boxed_slice();
    let len = boxed.len();
    (Box::into_raw(boxed) as *mut c_char, len)
}

// Reclaim a buffer from alloc_bytes, wiping it first.
unsafe fn free_zeroed(p: *mut c_char, len: size_t) {
    if p.is_null() || len == 0 {
        return;
    }
    let mut boxed = unsafe { Box::from_raw(ptr::slice_from_raw_parts_mut(p as *mut u8, len)) };
    boxed.zeroize();
}

// Borrow a C string as bytes; NULL reads as empty.
unsafe fn opt_bytes<'a>(p: *const c_char) -> &'a [u8] {
    if p.is_null() {
        b""
    } else {
        unsafe { CStr::from_ptr(p) }.to_bytes()
    }
}

/// Generate an OpenSSH key pair. Never panics across the FFI boundary.
///
/// # Safety
/// `comment` and `passphrase` must be NUL-terminated or NULL.
#[no_mangle]
pub unsafe extern "C" fn npc_ssh_keygen(
    alg: c_int,
    bits: c_int,
    comment: *const c_char,
    passphrase: *const c_char,
) -> SshKeyResult {
    let comment_bytes = unsafe { opt_bytes(comment) };
    let pass_bytes = unsafe { opt_bytes(passphrase) };

    let outcome = catch_unwind(AssertUnwindSafe(|| {
        generate(alg, bits, comment_bytes, pass_bytes)
    }));

    match outcome {
        Ok(Ok(r)) => r,
        Ok(Err(msg)) => err_result(&msg),
        Err(_) => err_result("Key generation failed unexpectedly."),
    }
}

fn generate(
    alg: c_int,
    bits: c_int,
    comment: &[u8],
    passphrase: &[u8],
) -> Result<SshKeyResult, String> {
    let comment = std::str::from_utf8(comment)
        .map_err(|_| "Comment is not valid UTF-8.".to_string())?
        .to_string();

    let mut key = match alg {
        ALG_ED25519 => PrivateKey::random(&mut OsRng, Algorithm::Ed25519)
            .map_err(|e| format!("Ed25519 key generation failed: {e}"))?,
        ALG_P256 => PrivateKey::random(
            &mut OsRng,
            Algorithm::Ecdsa {
                curve: EcdsaCurve::NistP256,
            },
        )
        .map_err(|e| format!("ECDSA P-256 key generation failed: {e}"))?,
        ALG_P384 => PrivateKey::random(
            &mut OsRng,
            Algorithm::Ecdsa {
                curve: EcdsaCurve::NistP384,
            },
        )
        .map_err(|e| format!("ECDSA P-384 key generation failed: {e}"))?,
        ALG_RSA => {
            // PrivateKey::random ignores the bit size for RSA and always makes
            // 4096, so build the keypair directly to honour the caller's choice.
            if !matches!(bits, 2048 | 3072 | 4096) {
                return Err(format!(
                    "RSA key size must be 2048, 3072 or 4096 bits (got {bits})."
                ));
            }
            let pair = RsaKeypair::random(&mut OsRng, bits as usize)
                .map_err(|e| format!("RSA key generation failed: {e}"))?;
            PrivateKey::from(pair)
        }
        _ => {
            return Err(format!(
                "Unknown key algorithm {alg} (expected 0=Ed25519, 1=P-256, 2=P-384, 3=RSA)."
            ))
        }
    };

    if !comment.is_empty() {
        key.set_comment(comment);
    }

    // Public line and fingerprint are the same before and after encryption.
    let mut public_line = key
        .public_key()
        .to_openssh()
        .map_err(|e| format!("Could not encode the public key: {e}"))?;
    public_line.push('\n');
    let fingerprint = key.fingerprint(HashAlg::Sha256).to_string();

    let key = if passphrase.is_empty() {
        key
    } else {
        key.encrypt(&mut OsRng, passphrase)
            .map_err(|e| format!("Could not encrypt the private key: {e}"))?
    };

    let pem = key
        .to_openssh(LineEnding::LF)
        .map_err(|e| format!("Could not encode the private key: {e}"))?;

    let (private_pem, private_len) = alloc_bytes(pem.as_bytes());
    let (public_ptr, public_len) = alloc_bytes(public_line.as_bytes());
    let (fp_ptr, fp_len) = alloc_bytes(fingerprint.as_bytes());
    public_line.zeroize();

    Ok(SshKeyResult {
        ok: 1,
        private_pem,
        private_len,
        public_line: public_ptr,
        public_len,
        fingerprint: fp_ptr,
        fingerprint_len: fp_len,
        error_msg: ptr::null_mut(),
    })
}

/// Wipe and release the buffers in an SshKeyResult.
///
/// `error_msg` is a CString and is NOT touched here — free it with
/// `npc_free_string`, as the header states.
///
/// # Safety
/// Call at most once per result, with pointers this crate handed out.
#[no_mangle]
pub unsafe extern "C" fn npc_free_ssh_key(r: SshKeyResult) {
    unsafe {
        free_zeroed(r.private_pem, r.private_len);
        free_zeroed(r.public_line, r.public_len);
        free_zeroed(r.fingerprint, r.fingerprint_len);
    }
}

/// Fill `buf[0..len)` from the OS CSPRNG. Returns 1 on success, 0 on failure.
///
/// # Safety
/// `buf` must be writable for `len` bytes.
#[no_mangle]
pub unsafe extern "C" fn npc_random_bytes(buf: *mut u8, len: size_t) -> c_int {
    if buf.is_null() {
        return 0;
    }
    if len == 0 {
        return 1;
    }
    // Draw into scratch first so a failed draw leaves the caller's buffer alone.
    let mut scratch = vec![0u8; len];
    if OsRng.try_fill_bytes(&mut scratch).is_err() {
        scratch.zeroize();
        return 0;
    }
    unsafe { ptr::copy_nonoverlapping(scratch.as_ptr(), buf, len) };
    scratch.zeroize();
    1
}

#[cfg(test)]
mod tests {
    use super::*;
    use ssh_key::PublicKey;

    struct Gen {
        ok: bool,
        private: String,
        public: String,
        fingerprint: String,
        error: String,
    }

    fn gen(alg: c_int, bits: c_int, comment: &str, passphrase: &str) -> Gen {
        let c = CString::new(comment).unwrap();
        let p = CString::new(passphrase).unwrap();
        let r = unsafe { npc_ssh_keygen(alg, bits, c.as_ptr(), p.as_ptr()) };

        let take = |ptr: *mut c_char, len: size_t| -> String {
            if ptr.is_null() || len == 0 {
                return String::new();
            }
            let s = unsafe { std::slice::from_raw_parts(ptr as *const u8, len) };
            String::from_utf8(s.to_vec()).unwrap()
        };
        let out = Gen {
            ok: r.ok == 1,
            private: take(r.private_pem, r.private_len),
            public: take(r.public_line, r.public_len),
            fingerprint: take(r.fingerprint, r.fingerprint_len),
            error: if r.error_msg.is_null() {
                String::new()
            } else {
                unsafe { CStr::from_ptr(r.error_msg) }
                    .to_string_lossy()
                    .into_owned()
            },
        };
        let err_ptr = r.error_msg;
        unsafe { npc_free_ssh_key(r) };
        unsafe { crate::npc_free_string(err_ptr) };
        out
    }

    // Ed25519 / P-256 / P-384 everywhere; RSA only where it is cheap enough.
    fn fast_algs() -> Vec<(c_int, c_int, &'static str)> {
        vec![
            (ALG_ED25519, 0, "ssh-ed25519"),
            (ALG_P256, 0, "ecdsa-sha2-nistp256"),
            (ALG_P384, 0, "ecdsa-sha2-nistp384"),
        ]
    }

    fn all_algs() -> Vec<(c_int, c_int, &'static str)> {
        let mut v = fast_algs();
        v.push((ALG_RSA, 2048, "ssh-rsa"));
        v
    }

    #[test]
    fn every_alg_round_trips_through_ssh_key() {
        for (alg, bits, prefix) in all_algs() {
            let g = gen(alg, bits, "round-trip@notepatra", "");
            assert!(g.ok, "alg {alg} failed: {}", g.error);
            assert!(g.error.is_empty());

            assert!(
                g.private.starts_with("-----BEGIN OPENSSH PRIVATE KEY-----"),
                "alg {alg} private header wrong"
            );
            assert!(g.private.ends_with('\n'));
            assert!(g
                .private
                .trim_end()
                .ends_with("-----END OPENSSH PRIVATE KEY-----"));

            let parsed = PrivateKey::from_openssh(&g.private)
                .unwrap_or_else(|e| panic!("alg {alg} private did not parse: {e}"));

            assert!(
                g.public.starts_with(prefix),
                "alg {alg} public line starts with {:?}",
                &g.public[..prefix.len().min(g.public.len())]
            );
            assert!(g.public.ends_with('\n'));
            let pub_parsed = PublicKey::from_openssh(g.public.trim_end())
                .unwrap_or_else(|e| panic!("alg {alg} public did not parse: {e}"));

            assert!(g.fingerprint.starts_with("SHA256:"), "alg {alg} fp prefix");
            assert_eq!(
                parsed.fingerprint(HashAlg::Sha256).to_string(),
                g.fingerprint,
                "alg {alg} private fingerprint mismatch"
            );
            assert_eq!(
                pub_parsed.fingerprint(HashAlg::Sha256).to_string(),
                g.fingerprint,
                "alg {alg} public fingerprint mismatch"
            );
        }
    }

    #[test]
    fn comment_round_trips_into_both_halves() {
        let comment = "prateek@notepatra-desk";
        for (alg, bits, _) in all_algs() {
            let g = gen(alg, bits, comment, "");
            assert!(g.ok, "{}", g.error);
            let parsed = PrivateKey::from_openssh(&g.private).unwrap();
            assert_eq!(parsed.comment(), comment, "alg {alg} private comment");
            let pubkey = PublicKey::from_openssh(g.public.trim_end()).unwrap();
            assert_eq!(pubkey.comment(), comment, "alg {alg} public comment");
            assert!(
                g.public.trim_end().ends_with(comment),
                "alg {alg} comment missing from the authorized_keys line"
            );
        }
    }

    #[test]
    fn empty_comment_leaves_the_public_line_at_two_fields() {
        let g = gen(ALG_ED25519, 0, "", "");
        assert!(g.ok, "{}", g.error);
        assert_eq!(g.public.split_whitespace().count(), 2);
    }

    #[test]
    fn passphrase_encrypts_and_only_the_right_one_opens_it() {
        for (alg, bits, _) in fast_algs() {
            let g = gen(alg, bits, "locked@notepatra", "correct horse battery");
            assert!(g.ok, "{}", g.error);

            let parsed = PrivateKey::from_openssh(&g.private).unwrap();
            assert!(parsed.is_encrypted(), "alg {alg} was not encrypted");

            assert!(
                parsed.decrypt("wrong horse battery").is_err(),
                "alg {alg} decrypted with the wrong passphrase"
            );
            let opened = parsed
                .decrypt("correct horse battery")
                .unwrap_or_else(|e| panic!("alg {alg} would not decrypt: {e}"));
            assert_eq!(opened.comment(), "locked@notepatra");
            assert_eq!(
                opened.fingerprint(HashAlg::Sha256).to_string(),
                g.fingerprint
            );
        }
    }

    #[test]
    fn encrypted_key_uses_aes256_ctr_with_bcrypt() {
        let g = gen(ALG_ED25519, 0, "", "hunter2");
        assert!(g.ok, "{}", g.error);
        let parsed = PrivateKey::from_openssh(&g.private).unwrap();
        assert_eq!(parsed.cipher().to_string(), "aes256-ctr");
        // OpenSSH's own default: bcrypt-pbkdf, 16 rounds, 16-byte salt.
        match parsed.kdf() {
            ssh_key::Kdf::Bcrypt { salt, rounds } => {
                assert_eq!(*rounds, 16);
                assert_eq!(salt.len(), 16);
            }
            other => panic!("expected bcrypt KDF, got {other:?}"),
        }
    }

    #[test]
    fn bad_algorithm_reports_an_error_without_buffers() {
        for alg in [-1, 4, 99] {
            let g = gen(alg, 2048, "x", "");
            assert!(!g.ok, "alg {alg} unexpectedly succeeded");
            assert!(g.private.is_empty() && g.public.is_empty() && g.fingerprint.is_empty());
            assert!(g.error.contains("Unknown key algorithm"), "{}", g.error);
        }
    }

    #[test]
    fn bad_rsa_bit_size_reports_an_error() {
        for bits in [0, 512, 1024, 2047, 3000, 8192] {
            let g = gen(ALG_RSA, bits, "x", "");
            assert!(!g.ok, "RSA {bits} unexpectedly succeeded");
            assert!(g.private.is_empty());
            assert!(g.error.contains("2048, 3072 or 4096"), "{}", g.error);
        }
    }

    #[test]
    fn null_comment_and_passphrase_are_treated_as_empty() {
        let r = unsafe { npc_ssh_keygen(ALG_ED25519, 0, ptr::null(), ptr::null()) };
        assert_eq!(r.ok, 1);
        assert!(r.error_msg.is_null());
        let pem = unsafe { std::slice::from_raw_parts(r.private_pem as *const u8, r.private_len) };
        let pem = String::from_utf8(pem.to_vec()).unwrap();
        assert!(PrivateKey::from_openssh(&pem).unwrap().comment().is_empty());
        unsafe { npc_free_ssh_key(r) };
    }

    #[test]
    fn two_ed25519_draws_differ() {
        let a = gen(ALG_ED25519, 0, "same@host", "");
        let b = gen(ALG_ED25519, 0, "same@host", "");
        assert!(a.ok && b.ok);
        assert_ne!(a.private, b.private);
        assert_ne!(a.public, b.public);
        assert_ne!(a.fingerprint, b.fingerprint);
    }

    #[test]
    fn random_bytes_fills_a_kilobyte_and_never_repeats() {
        let mut a = vec![0u8; 1024];
        let mut b = vec![0u8; 1024];
        assert_eq!(unsafe { npc_random_bytes(a.as_mut_ptr(), a.len()) }, 1);
        assert_eq!(unsafe { npc_random_bytes(b.as_mut_ptr(), b.len()) }, 1);
        assert_ne!(a, b);
        assert!(a.iter().any(|&x| x != 0), "1 KB draw was all zero");
        assert!(b.iter().any(|&x| x != 0), "1 KB draw was all zero");
        // A CSPRNG kilobyte should touch most byte values; a stuck source will not.
        let distinct = a.iter().collect::<std::collections::HashSet<_>>().len();
        assert!(
            distinct > 128,
            "only {distinct} distinct byte values in 1 KB"
        );
    }

    #[test]
    fn random_bytes_rejects_null_and_accepts_zero_length() {
        assert_eq!(unsafe { npc_random_bytes(ptr::null_mut(), 16) }, 0);
        let mut one = [7u8; 1];
        assert_eq!(unsafe { npc_random_bytes(one.as_mut_ptr(), 0) }, 1);
        assert_eq!(one[0], 7, "zero-length draw must not touch the buffer");
    }

    // ── Ground truth: OpenSSH's own ssh-keygen must agree with us ──────────
    #[cfg(unix)]
    fn have_ssh_keygen() -> bool {
        std::process::Command::new("ssh-keygen")
            .arg("-h")
            .output()
            .is_ok()
    }

    #[cfg(unix)]
    fn check_against_ssh_keygen(alg: c_int, bits: c_int) {
        use std::io::Write;
        use std::os::unix::fs::PermissionsExt;

        let g = gen(alg, bits, "groundtruth@notepatra", "");
        assert!(g.ok, "{}", g.error);

        let dir = tempfile::tempdir().unwrap();
        let path = dir.path().join("id_test");
        let mut f = std::fs::File::create(&path).unwrap();
        f.write_all(g.private.as_bytes()).unwrap();
        f.flush().unwrap();
        drop(f);
        std::fs::set_permissions(&path, std::fs::Permissions::from_mode(0o600)).unwrap();

        let y = std::process::Command::new("ssh-keygen")
            .arg("-y")
            .arg("-f")
            .arg(&path)
            .output()
            .unwrap();
        assert!(
            y.status.success(),
            "ssh-keygen -y failed for alg {alg}: {}",
            String::from_utf8_lossy(&y.stderr)
        );
        let derived = String::from_utf8_lossy(&y.stdout);
        let derived: Vec<&str> = derived.split_whitespace().take(2).collect();
        let ours: Vec<&str> = g.public.split_whitespace().take(2).collect();
        assert_eq!(derived, ours, "ssh-keygen -y disagrees for alg {alg}");

        let l = std::process::Command::new("ssh-keygen")
            .arg("-lf")
            .arg(&path)
            .output()
            .unwrap();
        assert!(
            l.status.success(),
            "ssh-keygen -lf failed for alg {alg}: {}",
            String::from_utf8_lossy(&l.stderr)
        );
        let listed = String::from_utf8_lossy(&l.stdout);
        let fp = listed
            .split_whitespace()
            .find(|t| t.starts_with("SHA256:"))
            .unwrap_or_else(|| panic!("no SHA256 field in {listed:?}"));
        assert_eq!(fp, g.fingerprint, "ssh-keygen -lf disagrees for alg {alg}");
    }

    #[test]
    #[cfg(unix)]
    fn openssh_agrees_on_public_key_and_fingerprint() {
        if !have_ssh_keygen() {
            eprintln!("ssh-keygen not present — ground-truth check skipped");
            return;
        }
        for (alg, bits, _) in fast_algs() {
            check_against_ssh_keygen(alg, bits);
        }
    }

    #[test]
    #[cfg(unix)]
    fn openssh_opens_our_passphrase_protected_key() {
        use std::io::Write;
        use std::os::unix::fs::PermissionsExt;
        if !have_ssh_keygen() {
            eprintln!("ssh-keygen not present — ground-truth check skipped");
            return;
        }
        let pass = "correct horse battery";
        let g = gen(ALG_ED25519, 0, "locked@notepatra", pass);
        assert!(g.ok, "{}", g.error);

        let dir = tempfile::tempdir().unwrap();
        let path = dir.path().join("id_locked");
        let mut f = std::fs::File::create(&path).unwrap();
        f.write_all(g.private.as_bytes()).unwrap();
        drop(f);
        std::fs::set_permissions(&path, std::fs::Permissions::from_mode(0o600)).unwrap();

        let good = std::process::Command::new("ssh-keygen")
            .args(["-y", "-P", pass, "-f"])
            .arg(&path)
            .output()
            .unwrap();
        assert!(
            good.status.success(),
            "ssh-keygen could not open our encrypted key: {}",
            String::from_utf8_lossy(&good.stderr)
        );
        let derived = String::from_utf8_lossy(&good.stdout);
        let derived: Vec<&str> = derived.split_whitespace().take(2).collect();
        let ours: Vec<&str> = g.public.split_whitespace().take(2).collect();
        assert_eq!(derived, ours);

        let bad = std::process::Command::new("ssh-keygen")
            .args(["-y", "-P", "wrong horse battery", "-f"])
            .arg(&path)
            .output()
            .unwrap();
        assert!(
            !bad.status.success(),
            "ssh-keygen opened the key with the wrong passphrase"
        );
    }

    #[test]
    #[cfg(all(unix, not(debug_assertions)))]
    fn openssh_agrees_on_rsa() {
        if !have_ssh_keygen() {
            eprintln!("ssh-keygen not present — ground-truth check skipped");
            return;
        }
        check_against_ssh_keygen(ALG_RSA, 2048);
    }

    #[test]
    #[cfg(not(debug_assertions))]
    fn rsa_4096_generation_timing() {
        let t = std::time::Instant::now();
        let g = gen(ALG_RSA, 4096, "timing@notepatra", "");
        let elapsed = t.elapsed();
        assert!(g.ok, "{}", g.error);
        eprintln!("RSA-4096 generation (release): {:.2?}", elapsed);
        assert!(g.public.starts_with("ssh-rsa "));
    }
}
