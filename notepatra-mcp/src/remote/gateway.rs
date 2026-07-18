// SPDX-License-Identifier: GPL-3.0-or-later
//! The loopback gateway: a std `TcpListener` on 127.0.0.1, thread-per-
//! connection, no async runtime. Each request is authenticated (fail-closed to
//! read_only), scope-checked at DISPATCH, then forwarded to the backend
//! `EditorTransport` through the existing `Server` machinery.
//!
//! ## Concurrency
//! One shared `Arc<Mutex<Server<T>>>` across connection threads. `SocketEditor`
//! is `Send` but not `Sync` (its `RefCell` connection), so a `Mutex` — never an
//! `RwLock` — is the correct sharing primitive, and serializing requests
//! matches the single editor bridge anyway. KNOWN TRADE-OFF: a write verb
//! blocking up to ~120 s on the local approval card holds the lock and stalls
//! other requests. Acceptable for a single remote client in 3a; Phase 3b can
//! move to per-connection backends.
//!
//! ## Approval invariant
//! The gateway NEVER approves anything. A `write_request`-scoped call is merely
//! forwarded to the editor bridge, which raises the LOCAL approval card on the
//! host exactly as today.

use std::io::{self, BufReader, Write};
use std::net::{SocketAddr, TcpListener, TcpStream};
use std::sync::{Arc, Mutex};
use std::time::Instant;

use serde_json::{json, Value};

use crate::server::Server;
use crate::transport::EditorTransport;

use super::http::{self, HttpRequest};
use super::pairing::PairingState;
use super::scope::{self, Scope};
use super::token::TokenStore;
use super::SCOPE_DENIED;

/// Binds `addr` — but ONLY if it is a loopback address. The check lives here
/// (defense-in-depth for a future LAN flag) so it is unit-testable independent
/// of the CLI, which only ever constructs `127.0.0.1:{port}`.
pub fn bind_loopback(addr: SocketAddr) -> io::Result<TcpListener> {
    if !addr.ip().is_loopback() {
        return Err(io::Error::new(
            io::ErrorKind::InvalidInput,
            format!(
                "refusing to bind non-loopback address {addr}; the gateway is \
                 loopback-only this phase (LAN binding + TLS is Phase 3b)"
            ),
        ));
    }
    TcpListener::bind(addr)
}

/// Shared per-connection context.
struct Ctx<T: EditorTransport + Send + 'static> {
    server: Arc<Mutex<Server<T>>>,
    tokens: Arc<TokenStore>,
    pairing: Arc<Mutex<PairingState>>,
    /// When true, an unauthenticated (no/invalid token) `/rpc` is refused
    /// outright instead of degrading to read_only. Default false preserves the
    /// brief's read-only floor; operators exposing the loopback port (e.g. an
    /// SSH forward) can opt into a deny-all posture with `serve --require-token`.
    require_token: bool,
}

impl<T: EditorTransport + Send + 'static> Clone for Ctx<T> {
    fn clone(&self) -> Self {
        Self {
            server: self.server.clone(),
            tokens: self.tokens.clone(),
            pairing: self.pairing.clone(),
            require_token: self.require_token,
        }
    }
}

/// Accept loop. Blocks forever, spawning a thread per connection. Backend and
/// state are shared via `Arc`; callers keep their own `Arc` clones to inspect
/// state (tests read the token store after pairing).
pub fn accept_loop<T: EditorTransport + Send + 'static>(
    listener: TcpListener,
    server: Arc<Mutex<Server<T>>>,
    tokens: Arc<TokenStore>,
    pairing: Arc<Mutex<PairingState>>,
    require_token: bool,
) -> io::Result<()> {
    let ctx = Ctx {
        server,
        tokens,
        pairing,
        require_token,
    };
    for stream in listener.incoming() {
        match stream {
            Ok(stream) => {
                let ctx = ctx.clone();
                std::thread::spawn(move || {
                    let _ = handle_conn(stream, ctx);
                });
            }
            // A single failed accept must not kill the gateway.
            Err(_) => continue,
        }
    }
    Ok(())
}

fn handle_conn<T: EditorTransport + Send + 'static>(
    stream: TcpStream,
    ctx: Ctx<T>,
) -> io::Result<()> {
    let mut writer = stream.try_clone()?;
    let mut reader = BufReader::new(stream);
    loop {
        let req = match HttpRequest::read(&mut reader) {
            Ok(Some(r)) => r,
            Ok(None) => break, // client closed
            // An over-limit Content-Length (or other malformed framing) surfaces
            // as InvalidData: answer 413 and drop the connection rather than
            // allocating for it.
            Err(e) if e.kind() == io::ErrorKind::InvalidData => {
                let _ = http::write_response(
                    &mut writer,
                    413,
                    "Payload Too Large",
                    b"{\"error\":\"request body too large\"}",
                    false,
                );
                break;
            }
            Err(e) => return Err(e),
        };
        let keep_alive = req.keep_alive();
        route(&req, &ctx, &mut writer)?;
        if !keep_alive {
            break;
        }
    }
    Ok(())
}

fn route<T: EditorTransport + Send + 'static>(
    req: &HttpRequest,
    ctx: &Ctx<T>,
    w: &mut impl Write,
) -> io::Result<()> {
    if req.method != "POST" {
        return http::write_response(w, 400, "Bad Request", b"{\"error\":\"POST only\"}", true);
    }
    match req.path.as_str() {
        "/pair/start" => pair_start(ctx, w),
        "/pair/complete" => pair_complete(req, ctx, w),
        "/rpc" => rpc(req, ctx, w),
        _ => http::write_response(w, 404, "Not Found", b"{\"error\":\"unknown path\"}", true),
    }
}

fn pair_start<T: EditorTransport + Send + 'static>(
    ctx: &Ctx<T>,
    w: &mut impl Write,
) -> io::Result<()> {
    let mut pairing = ctx.pairing.lock().unwrap_or_else(|e| e.into_inner());
    match pairing.start(Instant::now()) {
        Ok((pair_id, nonce)) => {
            let body = json!({ "pair_id": pair_id, "nonce": nonce }).to_string();
            http::write_response(w, 200, "OK", body.as_bytes(), true)
        }
        Err(e) => {
            let body = json!({ "error": e.message() }).to_string();
            http::write_response(w, 403, "Forbidden", body.as_bytes(), true)
        }
    }
}

fn pair_complete<T: EditorTransport + Send + 'static>(
    req: &HttpRequest,
    ctx: &Ctx<T>,
    w: &mut impl Write,
) -> io::Result<()> {
    let body: Value = serde_json::from_slice(&req.body).unwrap_or(Value::Null);
    let pair_id = body.get("pair_id").and_then(Value::as_str).unwrap_or("");
    let mac = body.get("mac").and_then(Value::as_str).unwrap_or("");
    let requested = body
        .get("scope")
        .and_then(Value::as_str)
        .and_then(Scope::parse)
        .unwrap_or(Scope::WriteRequest);

    let granted = {
        let mut pairing = ctx.pairing.lock().unwrap_or_else(|e| e.into_inner());
        pairing.complete(pair_id, mac, requested, Instant::now())
    };
    match granted {
        Ok(scope) => {
            // Issue the token only AFTER a verified handshake; server persists
            // its SHA-256 only.
            match ctx.tokens.issue(scope) {
                Ok(token) => {
                    let resp = json!({ "token": token, "scope": scope.as_str() }).to_string();
                    http::write_response(w, 200, "OK", resp.as_bytes(), true)
                }
                Err(e) => {
                    let resp = json!({ "error": format!("token storage failed: {e}") }).to_string();
                    http::write_response(w, 500, "Internal Server Error", resp.as_bytes(), true)
                }
            }
        }
        Err(e) => {
            let resp = json!({ "error": e.message() }).to_string();
            http::write_response(w, 401, "Unauthorized", resp.as_bytes(), true)
        }
    }
}

fn rpc<T: EditorTransport + Send + 'static>(
    req: &HttpRequest,
    ctx: &Ctx<T>,
    w: &mut impl Write,
) -> io::Result<()> {
    // AUTH — fail-closed. A present-but-invalid token degrades to read_only,
    // exactly as an absent one (per the phase brief). ALTERNATIVE considered: a
    // presented-but-invalid token could hard-401 so a client notices token
    // revocation; the brief specifies degrade, implemented here. `authed` tracks
    // whether a VALID token was presented, for the opt-in --require-token gate.
    let (scope, authed) = match req.bearer() {
        Some(tok) => match ctx.tokens.lookup(tok).unwrap_or(None) {
            Some(s) => (s, true),
            None => (Scope::ReadOnly, false),
        },
        None => (Scope::ReadOnly, false),
    };

    let line = match std::str::from_utf8(&req.body) {
        Ok(s) => s,
        Err(_) => {
            return http::write_response(w, 400, "Bad Request", b"{\"error\":\"body not UTF-8\"}", true)
        }
    };

    // SCOPE ENFORCEMENT at dispatch (not just in tools/list). Parse the message
    // to gate tools/call BEFORE it can reach the backend.
    let parsed: Option<Value> = serde_json::from_str(line).ok();
    let method = parsed
        .as_ref()
        .and_then(|v| v.get("method"))
        .and_then(Value::as_str);
    let id = parsed
        .as_ref()
        .and_then(|v| v.get("id"))
        .cloned()
        .filter(|v| !v.is_null());

    // --require-token: refuse any unauthenticated request outright (no read
    // floor). An id-bearing request gets a JSON-RPC auth error; a notification
    // is silently dropped (204). Backend is never touched.
    if ctx.require_token && !authed {
        if let Some(id) = id.clone() {
            let err = json!({
                "jsonrpc": "2.0",
                "id": id,
                "error": {
                    "code": SCOPE_DENIED,
                    "message": "authentication required (gateway started with --require-token)"
                }
            })
            .to_string();
            return http::write_response(w, 200, "OK", err.as_bytes(), true);
        }
        return http::write_response(w, 204, "No Content", b"", true);
    }

    if method == Some("tools/call") {
        if let Some(name) = parsed
            .as_ref()
            .and_then(|v| v.get("params"))
            .and_then(|p| p.get("name"))
            .and_then(Value::as_str)
        {
            if let Some(tier) = scope::tier_of(name) {
                // A tools/call WITHOUT an id is a notification: handle_line
                // drops it unexecuted, so no scope risk and no reply is owed.
                if !scope.allows(tier) {
                    if let Some(id) = id.clone() {
                        let err = json!({
                            "jsonrpc": "2.0",
                            "id": id,
                            "error": {
                                "code": SCOPE_DENIED,
                                "message": format!(
                                    "insufficient scope: {name} requires {}; this connection is {}",
                                    tier_name(tier), scope.as_str()
                                )
                            }
                        })
                        .to_string();
                        return http::write_response(w, 200, "OK", err.as_bytes(), true);
                    }
                    // Notification over-scope: silently dropped, backend untouched.
                    return http::write_response(w, 204, "No Content", b"", true);
                }
            }
        }
    }

    // FORWARD through the existing server machinery.
    let response = {
        let mut server = ctx.server.lock().unwrap_or_else(|e| e.into_inner());
        server.handle_line(line)
    };

    match response {
        None => http::write_response(w, 204, "No Content", b"", true),
        Some(mut resp) => {
            // tools/list: filter the advertised set to the connection's scope.
            if method == Some("tools/list") {
                scope::filter_tools_list(scope, &mut resp);
            }
            let body = resp.to_string();
            http::write_response(w, 200, "OK", body.as_bytes(), true)
        }
    }
}

fn tier_name(t: super::scope::Tier) -> &'static str {
    use super::scope::Tier;
    match t {
        Tier::Read => "read",
        Tier::Act => "read_act",
        Tier::Write => "write_request",
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn refuses_non_loopback() {
        assert!(bind_loopback("0.0.0.0:0".parse().unwrap()).is_err());
        assert!(bind_loopback("8.8.8.8:0".parse().unwrap()).is_err());
    }

    #[test]
    fn accepts_loopback() {
        let l = bind_loopback("127.0.0.1:0".parse().unwrap()).expect("loopback binds");
        assert!(l.local_addr().unwrap().ip().is_loopback());
    }
}
