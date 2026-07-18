// SPDX-License-Identifier: GPL-3.0-or-later
//! Token storage. The SERVER persists ONLY the SHA-256 of each issued token;
//! the plaintext lives only on the pairing CLIENT. Both files are 0600 (dir
//! 0700) on Unix; Windows relies on per-user %APPDATA% ACLs (documented no-op).
//! No token is ever logged or placed in argv.

use std::fs::{self, OpenOptions};
use std::io::{self, BufRead, BufReader, Write};
use std::path::{Path, PathBuf};

use serde_json::{json, Value};

use super::scope::Scope;
use super::{random_hex, sha256_hex};

/// Config root (parent of the `mcp-remote` dir). `NOTEPATRA_MCP_CONFIG_DIR`
/// overrides everything (tests point it at a tempdir); otherwise the platform
/// per-user config dir, mirroring the C++ `Config::appConfigDir()` layout
/// (exact casing reconciled in 3b when the editor mints the gateway secret —
/// 3a's files are sidecar-private, so the choice is not yet load-bearing).
pub fn config_root() -> PathBuf {
    if let Ok(d) = std::env::var("NOTEPATRA_MCP_CONFIG_DIR") {
        return PathBuf::from(d);
    }
    #[cfg(target_os = "windows")]
    {
        let base = std::env::var("APPDATA").unwrap_or_else(|_| ".".into());
        return PathBuf::from(base).join("Notepatra");
    }
    #[cfg(target_os = "macos")]
    {
        let home = std::env::var("HOME").unwrap_or_else(|_| ".".into());
        return PathBuf::from(home)
            .join("Library")
            .join("Application Support")
            .join("Notepatra");
    }
    #[cfg(all(unix, not(target_os = "macos")))]
    {
        if let Ok(x) = std::env::var("XDG_CONFIG_HOME") {
            if !x.is_empty() {
                return PathBuf::from(x).join("notepatra");
            }
        }
        let home = std::env::var("HOME").unwrap_or_else(|_| ".".into());
        return PathBuf::from(home).join(".config").join("notepatra");
    }
    #[cfg(not(any(unix, windows)))]
    {
        PathBuf::from(".").join("notepatra")
    }
}

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
                    v.get("scope").and_then(Value::as_str).and_then(Scope::parse),
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
#[cfg(not(unix))]
fn set_dir_private(_dir: &Path) {}

#[cfg(unix)]
fn set_file_private(path: &Path) {
    use std::os::unix::fs::PermissionsExt;
    let _ = fs::set_permissions(path, fs::Permissions::from_mode(0o600));
}
#[cfg(not(unix))]
fn set_file_private(_path: &Path) {}

#[cfg(test)]
mod tests {
    use super::*;

    fn tmp() -> PathBuf {
        let mut p = std::env::temp_dir();
        p.push(format!("np-mcp-tok-{}-{}", std::process::id(), random_hex(6)));
        p
    }

    #[test]
    fn only_sha256_persisted_server_side() {
        let store = TokenStore::at(tmp()).unwrap();
        let token = store.issue(Scope::WriteRequest).unwrap();
        let raw = fs::read_to_string(store.authorized_path()).unwrap();
        // Plaintext token must NOT appear in the server file; its hash must.
        assert!(!raw.contains(&token), "plaintext token leaked into server file");
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
}
