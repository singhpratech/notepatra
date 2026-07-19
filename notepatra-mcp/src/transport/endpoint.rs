// SPDX-License-Identifier: GPL-3.0-or-later
//! Endpoint discovery — reads `<config_root>/mcp-endpoint.json`, the file the
//! editor publishes after `QLocalServer::listen()` succeeds, naming the
//! ACTUAL bound endpoint.
//!
//! Why this exists: the computed guess in [`super::socket::socket_path`] is
//! `$TMPDIR||/tmp + /<name>`, but on macOS Qt binds under
//! `NSTemporaryDirectory()` — `/private/var/folders/.../T/` — which the guess
//! never reproduces. The published value is verbatim truth; the guess stays as
//! the fallback for older editors that don't publish.
//!
//! Staleness is NOT our problem to detect. An editor crash leaves the file
//! behind by design; the connect attempt is the only liveness test, so this
//! module never looks at `pid`, never stats mtimes, never retries, and never
//! creates the file or its directory. One cheap read, then out.

use std::path::PathBuf;

use serde_json::Value;

/// Refuse to read anything larger than this — the real file is ~200 bytes, so
/// a huge one is corruption or a hostile plant, not an endpoint record.
const MAX_ENDPOINT_FILE: u64 = 64 * 1024;

/// `<config_root>/mcp-endpoint.json` — the same path the C++ writer uses.
pub fn endpoint_file() -> PathBuf {
    crate::config_dir::config_root().join("mcp-endpoint.json")
}

/// Validates a published endpoint record and returns its `value` (the actual
/// bound endpoint). Pure — no I/O — so every rejection path is unit-testable
/// on every platform. ANY validation failure returns `None`: the caller then
/// behaves exactly as if the file did not exist.
pub fn parse_endpoint(contents: &str) -> Option<String> {
    let v: Value = serde_json::from_str(contents).ok()?;
    // Marker first: this rejects any unrelated JSON that happens to live at
    // this path before we interpret any other field.
    if v.get("notepatra_mcp_endpoint").and_then(Value::as_u64) != Some(1) {
        return None;
    }
    let want_kind = if cfg!(windows) {
        "named_pipe"
    } else {
        "unix_socket"
    };
    if v.get("kind").and_then(Value::as_str) != Some(want_kind) {
        return None;
    }
    // `as_str` also rejects a non-string value (e.g. an integer), which must
    // never reach a connect call.
    let value = v.get("value").and_then(Value::as_str)?;
    if value.is_empty() {
        return None;
    }
    // Shape check per platform: an absolute socket path on Unix, a pipe NAME
    // (not a filesystem path) on Windows.
    let shape_ok = if cfg!(windows) {
        value.starts_with(r"\\.\pipe\")
    } else {
        value.starts_with('/')
    };
    if !shape_ok {
        return None;
    }
    Some(value.to_string())
}

/// The published endpoint, or `None` when absent/oversized/invalid. A single
/// cheap `metadata` + `read_to_string`; it can never block or hang, so calling
/// it on the `SocketEditor::new()` path is free.
pub fn published_endpoint() -> Option<String> {
    let path = endpoint_file();
    let meta = std::fs::metadata(&path).ok()?;
    // Regular files ONLY. A FIFO planted at this path reports len 0 — it would
    // sail through the size gate and then block read_to_string FOREVER, hanging
    // every sidecar startup. Same-user write access to the config dir is needed
    // to plant one (so no privilege boundary is crossed), but the no-hang
    // guarantee above is absolute and must not depend on the dir's contents.
    if !meta.is_file() {
        return None;
    }
    // Size-gate BEFORE reading so a planted multi-gigabyte file can't be
    // slurped into memory just to be rejected.
    if meta.len() > MAX_ENDPOINT_FILE {
        return None;
    }
    let contents = std::fs::read_to_string(&path).ok()?;
    parse_endpoint(&contents)
}

/// Connection order: the published endpoint first (verbatim truth), then the
/// computed guess. Exact duplicates are collapsed so a correct guess isn't
/// dialed twice.
pub fn candidates(published: Option<String>, guess: String) -> Vec<String> {
    let mut out: Vec<String> = Vec::with_capacity(2);
    if let Some(p) = published {
        out.push(p);
    }
    if !out.contains(&guess) {
        out.push(guess);
    }
    out
}

#[cfg(test)]
mod tests {
    use super::*;

    /// A record whose `kind` matches the platform under test — the two shapes
    /// are mutually exclusive, so each is asserted only where it is valid.
    #[test]
    fn valid_record_for_this_platform() {
        if cfg!(windows) {
            let json = r#"{"notepatra_mcp_endpoint":1,"kind":"named_pipe","value":"\\\\.\\pipe\\notepatra-0123456789abcdef-mcp","name":"notepatra-0123456789abcdef-mcp","pid":42,"version":"0.1.120"}"#;
            assert_eq!(
                parse_endpoint(json).as_deref(),
                Some(r"\\.\pipe\notepatra-0123456789abcdef-mcp")
            );
        } else {
            let json = r#"{"notepatra_mcp_endpoint":1,"kind":"unix_socket","value":"/private/var/folders/ab/T/notepatra-0123456789abcdef-mcp","name":"notepatra-0123456789abcdef-mcp","pid":42,"version":"0.1.120"}"#;
            assert_eq!(
                parse_endpoint(json).as_deref(),
                Some("/private/var/folders/ab/T/notepatra-0123456789abcdef-mcp")
            );
        }
    }

    /// The other platform's `kind` must be rejected, not coerced.
    #[test]
    fn wrong_kind_for_this_platform_is_rejected() {
        let unix_rec =
            r#"{"notepatra_mcp_endpoint":1,"kind":"unix_socket","value":"/tmp/notepatra-mcp"}"#;
        let win_rec =
            r#"{"notepatra_mcp_endpoint":1,"kind":"named_pipe","value":"\\\\.\\pipe\\np-mcp"}"#;
        let foreign = if cfg!(windows) { unix_rec } else { win_rec };
        assert_eq!(parse_endpoint(foreign), None);
    }

    #[test]
    fn missing_marker_is_rejected() {
        assert_eq!(
            parse_endpoint(r#"{"kind":"unix_socket","value":"/tmp/np-mcp"}"#),
            None
        );
        assert_eq!(
            parse_endpoint(
                r#"{"notepatra_mcp_endpoint":2,"kind":"unix_socket","value":"/tmp/np-mcp"}"#
            ),
            None
        );
    }

    #[test]
    fn empty_or_missing_value_is_rejected() {
        let kind = if cfg!(windows) {
            "named_pipe"
        } else {
            "unix_socket"
        };
        assert_eq!(
            parse_endpoint(&format!(
                r#"{{"notepatra_mcp_endpoint":1,"kind":"{kind}","value":""}}"#
            )),
            None
        );
        assert_eq!(
            parse_endpoint(&format!(
                r#"{{"notepatra_mcp_endpoint":1,"kind":"{kind}"}}"#
            )),
            None
        );
    }

    /// A relative Unix path or a bare Windows name would be dialed against the
    /// CWD / the wrong namespace — reject the shape outright.
    #[test]
    fn wrong_path_prefix_is_rejected() {
        let kind = if cfg!(windows) {
            "named_pipe"
        } else {
            "unix_socket"
        };
        assert_eq!(
            parse_endpoint(&format!(
                r#"{{"notepatra_mcp_endpoint":1,"kind":"{kind}","value":"relative/notepatra-mcp"}}"#
            )),
            None
        );
    }

    #[test]
    fn garbage_json_is_rejected() {
        assert_eq!(parse_endpoint(""), None);
        assert_eq!(parse_endpoint("not json at all"), None);
        assert_eq!(parse_endpoint("{"), None);
        assert_eq!(parse_endpoint("[1,2,3]"), None);
    }

    /// `value` must be a string; an integer must not be stringified.
    #[test]
    fn integer_typed_value_is_rejected() {
        let kind = if cfg!(windows) {
            "named_pipe"
        } else {
            "unix_socket"
        };
        assert_eq!(
            parse_endpoint(&format!(
                r#"{{"notepatra_mcp_endpoint":1,"kind":"{kind}","value":12345}}"#
            )),
            None
        );
    }

    #[test]
    fn candidates_order_and_dedup() {
        assert_eq!(
            candidates(Some("/a".into()), "/b".into()),
            vec!["/a".to_string(), "/b".to_string()]
        );
        assert_eq!(candidates(None, "/b".into()), vec!["/b".to_string()]);
        // A published value equal to the guess is dialed once, not twice.
        assert_eq!(candidates(Some("/b".into()), "/b".into()), vec!["/b"]);
    }
}
