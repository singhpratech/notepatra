// SPDX-License-Identifier: GPL-3.0-or-later
//! The per-user Notepatra config directory, mirroring the C++
//! `Config::appConfigDir()` (src/config.h:413) EXACTLY — the editor writes
//! `mcp-endpoint.json` there and this sidecar reads it, so any divergence
//! silently breaks endpoint discovery.
//!
//! Deliberately UNGATED (not behind `--features remote`): the DEFAULT std-only
//! build must be able to locate `mcp-endpoint.json`. `remote::token`
//! re-exports this so the gateway keeps its existing API.

use std::path::PathBuf;

/// Config root (also the parent of the remote gateway's `mcp-remote` dir).
/// `NOTEPATRA_MCP_CONFIG_DIR` overrides everything (tests point it at a
/// tempdir); otherwise the platform per-user config dir.
///
/// This function NEVER creates the directory — the editor's `appConfigDir()`
/// already mkpaths it, and a reader that materialized the path could mask a
/// "no editor ever ran here" state.
pub fn config_root() -> PathBuf {
    if let Ok(d) = std::env::var("NOTEPATRA_MCP_CONFIG_DIR") {
        return PathBuf::from(d);
    }
    // Every branch is pure env-var + path arithmetic, so `cfg!` (not `#[cfg]`)
    // keeps them all type-checked on every host — the same style
    // `transport::socket::socket_path` uses. Only one is ever live.
    if cfg!(windows) {
        // config.h:413 — %APPDATA%\Notepatra, falling back to the literal
        // Roaming path under %USERPROFILE% when APPDATA is unset (services,
        // stripped environments). A "." fallback would point the reader at the
        // CWD, where the editor never writes.
        match std::env::var("APPDATA") {
            Ok(appdata) if !appdata.is_empty() => PathBuf::from(appdata).join("Notepatra"),
            _ => PathBuf::from(std::env::var("USERPROFILE").unwrap_or_else(|_| ".".into()))
                .join("AppData")
                .join("Roaming")
                .join("Notepatra"),
        }
    } else if cfg!(target_os = "macos") {
        PathBuf::from(std::env::var("HOME").unwrap_or_else(|_| ".".into()))
            .join("Library")
            .join("Application Support")
            .join("Notepatra")
    } else if cfg!(unix) {
        match std::env::var("XDG_CONFIG_HOME") {
            Ok(x) if !x.is_empty() => PathBuf::from(x).join("notepatra"),
            _ => PathBuf::from(std::env::var("HOME").unwrap_or_else(|_| ".".into()))
                .join(".config")
                .join("notepatra"),
        }
    } else {
        PathBuf::from(".").join("notepatra")
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    /// The override wins on every platform — the whole test suite depends on
    /// it, and so does the `mcp-endpoint.json` discovery test.
    #[test]
    fn env_override_wins() {
        // Serialized against other env-mutating tests only by being the sole
        // reader/writer of this variable inside this module.
        let key = "NOTEPATRA_MCP_CONFIG_DIR";
        let prev = std::env::var(key).ok();
        std::env::set_var(key, "/tmp/np-mcp-config-root-test");
        assert_eq!(config_root(), PathBuf::from("/tmp/np-mcp-config-root-test"));
        match prev {
            Some(v) => std::env::set_var(key, v),
            None => std::env::remove_var(key),
        }
    }
}
