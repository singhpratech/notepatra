// SPDX-License-Identifier: GPL-3.0-or-later
//! Argument parsing + wiring for the three remote subcommands (`serve`, `pair`,
//! `connect`). All additive — the existing bare-stdio / `--socket` modes are
//! untouched and dispatched before this module is ever reached.

use std::io::{self, Write};
use std::net::SocketAddr;
use std::sync::{Arc, Mutex};

use serde_json::{json, Value};

use crate::server::Server;
use crate::transport::mock::MockEditor;
use crate::transport::socket::SocketEditor;

use super::gateway;
use super::http;
use super::pairing::{self, PairingState};
use super::scope::Scope;
use super::token::TokenStore;

/// Entry point from main.rs for a remote subcommand.
pub fn run(mode: &str) -> io::Result<()> {
    let args: Vec<String> = std::env::args().skip(2).collect();
    match mode {
        "serve" => serve(&args),
        "pair" => pair(&args),
        "connect" => connect(&args),
        _ => {
            eprintln!("notepatra-mcp: unknown remote mode {mode:?}");
            std::process::exit(2);
        }
    }
}

fn flag_value<'a>(args: &'a [String], name: &str) -> Option<&'a str> {
    args.iter()
        .position(|a| a == name)
        .and_then(|i| args.get(i + 1))
        .map(String::as_str)
}

fn has_flag(args: &[String], name: &str) -> bool {
    args.iter().any(|a| a == name)
}

// ── serve ────────────────────────────────────────────────────────────────────

fn serve(args: &[String]) -> io::Result<()> {
    let port: u16 = flag_value(args, "--port")
        .and_then(|p| p.parse().ok())
        .unwrap_or(0); // 0 = ephemeral, actual port printed
    let max_scope = flag_value(args, "--max-scope")
        .and_then(Scope::parse)
        .unwrap_or(Scope::WriteRequest);
    // Opt-in deny-all posture: refuse unauthenticated /rpc entirely instead of
    // serving the read-only floor. Off by default (brief's read floor).
    let require_token = has_flag(args, "--require-token");

    let addr: SocketAddr = format!("127.0.0.1:{port}")
        .parse()
        .expect("literal loopback addr");
    let listener = gateway::bind_loopback(addr)?;
    let bound = listener.local_addr()?;

    let tokens = Arc::new(TokenStore::from_env()?);
    let pairing = Arc::new(Mutex::new(PairingState::new(
        max_scope,
        pairing::DEFAULT_TTL,
        pairing::DEFAULT_ATTEMPTS,
    )));

    // The code is printed to STDOUT; a token is NEVER printed or logged.
    {
        let p = pairing.lock().unwrap_or_else(|e| e.into_inner());
        println!("listening on http://{bound}");
        println!(
            "pairing code: {} (valid {}s, {} attempts, single use)",
            p.code(),
            pairing::DEFAULT_TTL.as_secs(),
            pairing::DEFAULT_ATTEMPTS,
        );
        let _ = io::stdout().flush();
    }

    // Backend: --socket reaches the running editor, else the in-memory mock.
    if has_flag(args, "--socket") {
        let server = Arc::new(Mutex::new(Server::new(SocketEditor::new())));
        gateway::accept_loop(listener, server, tokens, pairing, require_token)
    } else {
        let server = Arc::new(Mutex::new(Server::new(MockEditor::default())));
        gateway::accept_loop(listener, server, tokens, pairing, require_token)
    }
}

// ── pair ─────────────────────────────────────────────────────────────────────

fn pair(args: &[String]) -> io::Result<()> {
    let port: u16 = flag_value(args, "--port")
        .and_then(|p| p.parse().ok())
        .unwrap_or(0);
    if port == 0 {
        eprintln!("notepatra-mcp pair: --port <N> is required (the serve port)");
        std::process::exit(2);
    }
    let requested = flag_value(args, "--scope")
        .and_then(Scope::parse)
        .unwrap_or(Scope::WriteRequest);
    let base = format!("http://127.0.0.1:{port}");

    // The one-time code is not a secret token: accept it via --code, otherwise
    // prompt on stderr and read one line from stdin.
    let code = match flag_value(args, "--code") {
        Some(c) => c.to_string(),
        None => {
            eprint!("enter pairing code: ");
            let _ = io::stderr().flush();
            let mut line = String::new();
            io::stdin().read_line(&mut line)?;
            line.trim().to_string()
        }
    };

    // 1) start → nonce.
    let start = http::post_json(&base, "/pair/start", None, b"{}")?;
    let start_body: Value = serde_json::from_slice(&start.body).unwrap_or(Value::Null);
    if start.status != 200 {
        eprintln!(
            "notepatra-mcp pair: start rejected: {}",
            start_body
                .get("error")
                .and_then(Value::as_str)
                .unwrap_or("unknown")
        );
        std::process::exit(1);
    }
    let pair_id = start_body
        .get("pair_id")
        .and_then(Value::as_str)
        .unwrap_or("");
    let nonce = start_body
        .get("nonce")
        .and_then(Value::as_str)
        .unwrap_or("");

    // 2) prove knowledge of the code via HMAC(code, nonce).
    let Some(mac) = pairing::client_mac(&code, nonce) else {
        eprintln!("notepatra-mcp pair: server sent a malformed nonce");
        std::process::exit(1);
    };
    let complete_body = json!({
        "pair_id": pair_id,
        "mac": mac,
        "scope": requested.as_str(),
    })
    .to_string();
    let complete = http::post_json(&base, "/pair/complete", None, complete_body.as_bytes())?;
    let cb: Value = serde_json::from_slice(&complete.body).unwrap_or(Value::Null);
    if complete.status != 200 {
        eprintln!(
            "notepatra-mcp pair: pairing failed: {}",
            cb.get("error").and_then(Value::as_str).unwrap_or("unknown")
        );
        std::process::exit(1);
    }

    // 3) store the plaintext token (0600) — never printed, never in argv.
    let token = cb.get("token").and_then(Value::as_str).unwrap_or("");
    let scope = cb
        .get("scope")
        .and_then(Value::as_str)
        .and_then(Scope::parse)
        .unwrap_or(requested);
    let store = TokenStore::from_env()?;
    store.store_client(&base, token, scope)?;
    println!("paired (scope: {}); token stored", scope.as_str());
    Ok(())
}

// ── connect ──────────────────────────────────────────────────────────────────

fn connect(args: &[String]) -> io::Result<()> {
    // The URL is the first non-flag argument.
    let Some(url) = args.iter().find(|a| !a.starts_with("--")) else {
        eprintln!("notepatra-mcp connect: usage: connect <url>  (e.g. http://127.0.0.1:8080)");
        std::process::exit(2);
    };
    super::connect::run(url)
}
