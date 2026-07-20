// SPDX-License-Identifier: GPL-3.0-or-later
//! Phase 3a — the feature-gated REMOTE GATEWAY CORE.
//!
//! This whole tree is compiled ONLY under `--features remote`; the default
//! build (`cargo build`) never touches it and pulls in zero new crates. Gating
//! lives at a single choke point — `#[cfg(feature = "remote")] pub mod remote`
//! in lib.rs — so the default dependency graph stays byte-identical to HEAD.
//!
//! ## What this is
//! An OPT-IN, LOOPBACK-ONLY gateway that lets an existing stdio MCP client
//! config reach a *locally running* Notepatra editor over a small HTTP/JSON-RPC
//! protocol by only swapping the sidecar's args (`connect <url>` instead of the
//! bare stdio server). The remote-machine story this phase is an SSH port-
//! forward to the loopback gateway — there is NO network binding beyond
//! 127.0.0.1 here.
//!
//! ## Wire protocol (PRIVATE gateway protocol — NOT MCP streamable-HTTP)
//! Minimal HTTP/1.1, one JSON-RPC message per `POST /rpc`:
//! ```text
//! POST /rpc HTTP/1.1
//! Authorization: Bearer <64-hex token>   (omitted when unpaired)
//! Content-Type: application/json
//! Content-Length: N
//!
//! {"jsonrpc":"2.0","id":1,"method":"tools/call",...}
//! ```
//! The response is `200` + the JSON-RPC response body (JSON-RPC-level errors —
//! auth/scope rejections — ride INSIDE a 200 so the `connect` front-end just
//! pipes bodies to stdout). Notifications (`handle_line` → None) answer `204`.
//! Pairing uses `POST /pair/start` then `POST /pair/complete`.
//!
//! ## Security posture (this phase)
//! * Bind is LOOPBACK-ONLY — [`gateway::bind_loopback`] refuses any non-loopback
//!   address. The editor itself NEVER listens on the network; only this opt-in
//!   sidecar binds, and only on 127.0.0.1.
//! * Auth is FAIL-CLOSED — a request with no/invalid token is served at
//!   [`Scope::ReadOnly`] at most; act/write verbs are rejected. Identity is
//!   never elevated on a missing token.
//! * READ-FLOOR EXPOSURE: with the default posture, an UNPAIRED request still
//!   reaches the full read tier — including `read_tab`, `git_*`, and `run_sql`.
//!   Anyone who can reach the loopback port (a local user, or a peer via the
//!   documented `ssh -L` forward) reads open buffers, git history, and SQL
//!   results with no credential. That read tier is only as tight as the editor
//!   bridge's untrusted-SQL denylist (DuckDB `read_text`/`read_csv_auto`/`glob`
//!   are file-reading SELECTs — an editor-side concern, out of this Rust phase).
//!   Operators exposing the port beyond their own machine should start
//!   `serve --require-token`, which refuses every unauthenticated `/rpc`
//!   (no read floor). Hardening the read floor by default is a Phase 3b (LAN)
//!   decision for the brief author.
//! * The APPROVAL INVARIANT is untouched — a `write_request`-scoped call is only
//!   FORWARDED to the editor bridge, which raises the LOCAL approval card on the
//!   host exactly as today. The gateway never sees or answers an approval.
//! * MITM is OUT OF SCOPE this phase: loopback-only means no untrusted network
//!   path. TLS (rustls) + cert-pinning for LAN binding is the NEXT phase (3b).

pub mod cli;
pub mod connect;
pub mod gateway;
pub mod http;
pub mod pairing;
pub mod scope;
pub mod token;

pub use scope::{Scope, Tier};

/// JSON-RPC error code for a call whose tier exceeds the connection's scope.
/// Chosen in the MCP/implementation-defined server range (-32000..-32099).
pub const SCOPE_DENIED: i64 = -32001;

// ── Small crypto/random helpers (only exist under the `remote` feature) ──────

/// Fills `buf` with OS randomness. Panics only if the OS RNG is unavailable,
/// which on a supported platform indicates a broken system — there is no safe
/// fallback for security-critical bytes.
pub(crate) fn random_bytes(buf: &mut [u8]) {
    getrandom::getrandom(buf).expect("OS RNG unavailable");
}

/// Lowercase hex of `bytes`.
pub(crate) fn to_hex(bytes: &[u8]) -> String {
    let mut s = String::with_capacity(bytes.len() * 2);
    for b in bytes {
        s.push_str(&format!("{b:02x}"));
    }
    s
}

/// `n` random bytes as a lowercase hex string (token = `random_hex(32)` → 64 hex).
pub(crate) fn random_hex(n: usize) -> String {
    let mut b = vec![0u8; n];
    random_bytes(&mut b);
    to_hex(&b)
}

/// SHA-256 of `data` as lowercase hex (server stores ONLY this for a token).
pub(crate) fn sha256_hex(data: &[u8]) -> String {
    use sha2::{Digest, Sha256};
    to_hex(&Sha256::digest(data))
}

/// A one-time 8-digit pairing code, uniformly distributed (rejection sampling
/// removes modulo bias: 4_200_000_000 = largest multiple of 1e8 ≤ u32::MAX+1).
pub(crate) fn gen_pairing_code() -> String {
    loop {
        let mut b = [0u8; 4];
        random_bytes(&mut b);
        let x = u32::from_le_bytes(b);
        if x < 4_200_000_000 {
            return format!("{:08}", x % 100_000_000);
        }
    }
}
