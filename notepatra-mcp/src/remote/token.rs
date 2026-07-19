// SPDX-License-Identifier: GPL-3.0-or-later
//! Token storage. The SERVER persists ONLY the SHA-256 of each issued token;
//! the plaintext lives only on the pairing CLIENT. Both files are 0600 (dir
//! 0700) on Unix. On Windows the per-user %APPDATA% ACLs already exclude other
//! users; on top of that, `icacls` strips inheritance and re-grants only the
//! owning account (best-effort, failures ignored — same posture as the Unix
//! `let _ = set_permissions`). No token is ever logged or placed in argv.

use std::fs::{self, OpenOptions};
use std::io::{self, BufRead, BufReader, Write};
use std::path::{Path, PathBuf};

use serde_json::{json, Value};

use super::scope::Scope;
use super::{random_hex, sha256_hex};

/// Config root (parent of the `mcp-remote` dir). The definition moved to the
/// UNGATED [`crate::config_dir`] once endpoint discovery — a DEFAULT-build
/// concern — needed the same directory; re-exported here so
/// `remote::token::config_root` keeps working for the gateway.
pub use crate::config_dir::config_root;

/// One authorized token record, server side.
#[derive(Debug, Clone)]
pub struct ClientToken {
    pub token: String,
    pub scope: Scope,
}

/// Reads/writes the two token files under `<root>/mcp-remote/`.
pub struct TokenStore {
    dir: PathBuf,
}

impl TokenStore {
    /// Store rooted directly at `dir` (created if needed). Tests pass a tempdir.
    pub fn at(dir: impl Into<PathBuf>) -> io::Result<Self> {
        let dir = dir.into();
        fs::create_dir_all(&dir)?;
        set_dir_private(&dir);
        Ok(Self { dir })
    }

    /// The real store: `<config_root>/mcp-remote/`.
    pub fn from_env() -> io::Result<Self> {
        Self::at(config_root().join("mcp-remote"))
    }

    fn authorized_path(&self) -> PathBuf {
        self.dir.join("authorized_tokens.jsonl")
    }

    fn client_path(&self) -> PathBuf {
        self.dir.join("client_tokens.jsonl")
    }

    // ── Server side ──────────────────────────────────────────────────────────

    /// Generates a 256-bit token, persists ONLY its SHA-256 with `scope`, and
    /// returns the plaintext (the caller hands it to the pairing client and
    /// then drops it — it is never stored server-side).
    pub fn issue(&self, scope: Scope) -> io::Result<String> {
        let token = random_hex(32); // 32 bytes → 64 hex
        let rec = json!({
            "sha256": sha256_hex(token.as_bytes()),
            "scope": scope.as_str(),
            "created": unix_now(),
        });
        append_line(&self.authorized_path(), &rec.to_string())?;
        Ok(token)
    }

    /// Resolves a presented token to its granted scope by SHA-256 lookup, or
    /// `None` if unknown (the caller then fails closed to read_only).
    pub fn lookup(&self, token: &str) -> io::Result<Option<Scope>> {
        let want = sha256_hex(token.as_bytes());
        let path = self.authorized_path();
        if !path.exists() {
            return Ok(None);
        }
        for line in read_lines(&path)? {
            let v: Value = match serde_json::from_str(&line) {
                Ok(v) => v,
                Err(_) => continue,
            };
            if v.get("sha256").and_then(Value::as_str) == Some(want.as_str()) {
                return Ok(v
                    .get("scope")
                    .and_then(Value::as_str)
                    .and_then(Scope::parse));
            }
        }
        Ok(None)
    }

    // ── Client side ──────────────────────────────────────────────────────────

    /// Stores the plaintext token for `url` (0600). Last write wins on lookup.
    pub fn store_client(&self, url: &str, token: &str, scope: Scope) -> io::Result<()> {
        let rec = json!({ "url": url, "token": token, "scope": scope.as_str() });
        append_line(&self.client_path(), &rec.to_string())
    }

    /// The most recently stored token for `url`, if any.
    pub fn load_client(&self, url: &str) -> io::Result<Option<ClientToken>> {
        let path = self.client_path();
        if !path.exists() {
            return Ok(None);
        }
        let mut found = None;
        for line in read_lines(&path)? {
            let v: Value = match serde_json::from_str(&line) {
                Ok(v) => v,
                Err(_) => continue,
            };
            if v.get("url").and_then(Value::as_str) == Some(url) {
                if let (Some(t), Some(s)) = (
                    v.get("token").and_then(Value::as_str),
                    v.get("scope")
                        .and_then(Value::as_str)
                        .and_then(Scope::parse),
                ) {
                    found = Some(ClientToken {
                        token: t.to_string(),
                        scope: s,
                    });
                }
            }
        }
        Ok(found)
    }
}

fn unix_now() -> u64 {
    std::time::SystemTime::now()
        .duration_since(std::time::UNIX_EPOCH)
        .map(|d| d.as_secs())
        .unwrap_or(0)
}

/// Appends one newline-terminated line to a 0600 file (created private).
fn append_line(path: &Path, line: &str) -> io::Result<()> {
    let mut opts = OpenOptions::new();
    opts.create(true).append(true);
    #[cfg(unix)]
    {
        use std::os::unix::fs::OpenOptionsExt;
        opts.mode(0o600);
    }
    let mut f = opts.open(path)?;
    // Re-assert 0600 even if the file pre-existed with looser perms.
    set_file_private(path);
    f.write_all(line.as_bytes())?;
    f.write_all(b"\n")?;
    f.flush()
}

fn read_lines(path: &Path) -> io::Result<Vec<String>> {
    let f = fs::File::open(path)?;
    BufReader::new(f).lines().collect()
}

#[cfg(unix)]
fn set_dir_private(dir: &Path) {
    use std::os::unix::fs::PermissionsExt;
    let _ = fs::set_permissions(dir, fs::Permissions::from_mode(0o700));
}
/// Windows defense-in-depth: `%APPDATA%` is already per-user, but an explicit
/// ACL removes inherited grants (BUILTIN\Users, Everyone) that a customized
/// profile root can still propagate. `icacls` ships with Windows, so this
/// costs ZERO new dependencies — the alternative is a winapi crate, which the
/// std-only rule forbids. Best-effort by design: any failure leaves the
/// inherited (per-user) ACL in place, exactly as the Unix arm ignores a failed
/// `set_permissions`.
#[cfg(windows)]
fn set_dir_private(dir: &Path) {
    let Ok(user) = std::env::var("USERNAME") else {
        return;
    };
    // /inheritance:r drops inherited ACEs; /grant:r replaces (not appends) the
    // user's ACE. (OI)(CI)F = full control, inherited by files and subdirs.
    let _ = std::process::Command::new("icacls")
        .arg(dir)
        .arg("/inheritance:r")
        .arg("/grant:r")
        .arg(format!("{user}:(OI)(CI)F"))
        .output();
}

#[cfg(not(any(unix, windows)))]
fn set_dir_private(_dir: &Path) {}

#[cfg(unix)]
fn set_file_private(path: &Path) {
    use std::os::unix::fs::PermissionsExt;
    let _ = fs::set_permissions(path, fs::Permissions::from_mode(0o600));
}
/// File counterpart of [`set_dir_private`] — no inheritance flags, since a
/// file has nothing to propagate to.
#[cfg(windows)]
fn set_file_private(path: &Path) {
    let Ok(user) = std::env::var("USERNAME") else {
        return;
    };
    let _ = std::process::Command::new("icacls")
        .arg(path)
        .arg("/inheritance:r")
        .arg("/grant:r")
        .arg(format!("{user}:F"))
        .output();
}

#[cfg(not(any(unix, windows)))]
fn set_file_private(_path: &Path) {}

#[cfg(test)]
mod tests {
    use super::*;

    fn tmp() -> PathBuf {
        let mut p = std::env::temp_dir();
        p.push(format!(
            "np-mcp-tok-{}-{}",
            std::process::id(),
            random_hex(6)
        ));
        p
    }

    #[test]
    fn only_sha256_persisted_server_side() {
        let store = TokenStore::at(tmp()).unwrap();
        let token = store.issue(Scope::WriteRequest).unwrap();
        let raw = fs::read_to_string(store.authorized_path()).unwrap();
        // Plaintext token must NOT appear in the server file; its hash must.
        assert!(
            !raw.contains(&token),
            "plaintext token leaked into server file"
        );
        assert!(raw.contains(&sha256_hex(token.as_bytes())));
    }

    #[test]
    fn lookup_roundtrip() {
        let store = TokenStore::at(tmp()).unwrap();
        let t = store.issue(Scope::ReadAct).unwrap();
        assert_eq!(store.lookup(&t).unwrap(), Some(Scope::ReadAct));
        assert_eq!(store.lookup("deadbeef").unwrap(), None);
    }

    #[test]
    fn client_store_roundtrip() {
        let store = TokenStore::at(tmp()).unwrap();
        store
            .store_client("http://127.0.0.1:9", "tok", Scope::ReadOnly)
            .unwrap();
        let got = store.load_client("http://127.0.0.1:9").unwrap().unwrap();
        assert_eq!(got.token, "tok");
        assert_eq!(got.scope, Scope::ReadOnly);
        assert!(store.load_client("http://elsewhere").unwrap().is_none());
    }

    #[cfg(unix)]
    #[test]
    fn files_are_0600() {
        use std::os::unix::fs::PermissionsExt;
        let store = TokenStore::at(tmp()).unwrap();
        store.issue(Scope::ReadOnly).unwrap();
        store
            .store_client("http://x", "t", Scope::ReadOnly)
            .unwrap();
        for p in [store.authorized_path(), store.client_path()] {
            let mode = fs::metadata(&p).unwrap().permissions().mode() & 0o777;
            assert_eq!(mode, 0o600, "{p:?} mode was {mode:o}");
        }
        let dmode = fs::metadata(&store.dir).unwrap().permissions().mode() & 0o777;
        assert_eq!(dmode, 0o700);
    }

    /// Windows counterpart of `files_are_0600`. Two assertions: the ACL work
    /// must not break normal I/O (a botched `icacls` could lock the process
    /// out of its own files), and when `icacls` is queryable the resulting ACL
    /// must not list the broad principals that inheritance would have added.
    /// Skips the ACL assertion when `icacls` itself is unavailable so a locked
    /// down CI image reports a pass, not a spurious failure.
    #[cfg(windows)]
    #[test]
    fn windows_acls_do_not_break_io_and_drop_broad_principals() {
        let store = TokenStore::at(tmp()).unwrap();
        let t = store.issue(Scope::ReadAct).unwrap();
        store
            .store_client("http://127.0.0.1:9", "tok", Scope::ReadOnly)
            .unwrap();
        assert_eq!(store.lookup(&t).unwrap(), Some(Scope::ReadAct));
        assert_eq!(
            store
                .load_client("http://127.0.0.1:9")
                .unwrap()
                .unwrap()
                .token,
            "tok"
        );

        let path = store.authorized_path();
        if let Ok(out) = std::process::Command::new("icacls").arg(&path).output() {
            if out.status.success() {
                let acl = String::from_utf8_lossy(&out.stdout);
                assert!(
                    !acl.contains("BUILTIN\\Users"),
                    "inherited BUILTIN\\Users ACE survived: {acl}"
                );
                assert!(
                    !acl.contains("Everyone"),
                    "Everyone ACE present on a token file: {acl}"
                );
            }
        }
    }
}
