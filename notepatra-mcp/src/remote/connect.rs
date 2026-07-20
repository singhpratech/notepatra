// SPDX-License-Identifier: GPL-3.0-or-later
//! `connect <url>` — a stdio front-end that forwards JSON-RPC to a REMOTE
//! gateway over HTTP with the stored bearer token. Lets an existing stdio MCP
//! client config reach a remote editor by only swapping the sidecar's args.
//! The remote-machine story this phase is an SSH port-forward to the loopback
//! gateway (`ssh -L 8080:127.0.0.1:8080 host`), then `connect http://127.0.0.1:8080`.

use std::io::{self, BufRead, Write};

use super::http;
use super::token::TokenStore;

/// Reads newline-delimited JSON-RPC from stdin, POSTs each to `<url>/rpc` with
/// the stored token, and writes each response body (one line) to stdout —
/// mirroring `Server::run`. `204 No Content` (notifications) produce no output.
pub fn run(url: &str) -> io::Result<()> {
    if url.starts_with("https://") {
        eprintln!("notepatra-mcp connect: TLS not supported yet (Phase 3b); use http:// over an SSH port-forward");
        std::process::exit(2);
    }
    if !url.starts_with("http://") {
        eprintln!("notepatra-mcp connect: url must start with http://");
        std::process::exit(2);
    }
    let base = url.trim_end_matches('/').to_string();

    let store = TokenStore::from_env()?;
    let token = store.load_client(&base)?.map(|c| c.token);
    if token.is_none() {
        eprintln!(
            "notepatra-mcp connect: no stored token for {base}; run `notepatra-mcp pair` against it first"
        );
    }

    let stdin = io::stdin().lock();
    let stdout = io::stdout();
    for line in stdin.lines() {
        let line = line?;
        if line.trim().is_empty() {
            continue;
        }
        let resp = match http::post_json(&base, "/rpc", token.as_deref(), line.as_bytes()) {
            Ok(r) => r,
            Err(e) => {
                eprintln!("notepatra-mcp connect: request failed: {e}");
                continue;
            }
        };
        // 204 = notification (no reply owed). Anything else carries a body.
        if resp.status == 204 || resp.body.is_empty() {
            continue;
        }
        let mut out = stdout.lock();
        out.write_all(&resp.body)?;
        out.write_all(b"\n")?;
        out.flush()?;
    }
    Ok(())
}
