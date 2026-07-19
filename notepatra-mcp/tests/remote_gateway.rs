// SPDX-License-Identifier: GPL-3.0-or-later
//! Phase 3a integration — the loopback gateway end-to-end, offline, driven over
//! real HTTP against a MockEditor backend (no running editor needed). Compiles
//! ONLY with `--features remote` (see the `[[test]]` entry in Cargo.toml).

use std::path::PathBuf;
use std::sync::atomic::{AtomicU64, Ordering};
use std::sync::{Arc, Mutex};

use notepatra_mcp::remote::gateway::{accept_loop, bind_loopback};
use notepatra_mcp::remote::http;
use notepatra_mcp::remote::pairing::{self, PairingState};
use notepatra_mcp::remote::scope::Scope;
use notepatra_mcp::remote::token::TokenStore;
use notepatra_mcp::server::Server;
use notepatra_mcp::transport::mock::MockEditor;
use serde_json::{json, Value};

const CODE: &str = "13572468";

static SEQ: AtomicU64 = AtomicU64::new(0);

fn unique_dir() -> PathBuf {
    let n = SEQ.fetch_add(1, Ordering::Relaxed);
    let nanos = std::time::SystemTime::now()
        .duration_since(std::time::UNIX_EPOCH)
        .unwrap()
        .as_nanos();
    let mut p = std::env::temp_dir();
    p.push(format!("np-mcp-gw-{}-{n}-{nanos}", std::process::id()));
    p
}

struct Harness {
    base: String,
    tokens: Arc<TokenStore>,
    dir: PathBuf,
}

/// Boots a gateway on an ephemeral loopback port with the given pairing session
/// (default posture: unauthenticated requests degrade to the read-only floor).
fn start(pairing: PairingState) -> Harness {
    start_opts(pairing, false)
}

/// As `start`, but with an explicit `require_token` posture.
fn start_opts(pairing: PairingState, require_token: bool) -> Harness {
    let dir = unique_dir();
    let tokens = Arc::new(TokenStore::at(&dir).unwrap());
    let listener = bind_loopback("127.0.0.1:0".parse().unwrap()).unwrap();
    let port = listener.local_addr().unwrap().port();
    let server = Arc::new(Mutex::new(Server::new(MockEditor::default())));
    let pairing = Arc::new(Mutex::new(pairing));

    let (s, t, p) = (server.clone(), tokens.clone(), pairing.clone());
    std::thread::spawn(move || {
        let _ = accept_loop(listener, s, t, p, require_token);
    });

    Harness {
        base: format!("http://127.0.0.1:{port}"),
        tokens,
        dir,
    }
}

fn default_pairing() -> PairingState {
    PairingState::with_code(
        CODE.into(),
        Scope::WriteRequest,
        pairing::DEFAULT_TTL,
        pairing::DEFAULT_ATTEMPTS,
    )
}

fn rpc(base: &str, token: Option<&str>, body: Value) -> (u16, Value) {
    let resp = http::post_json(base, "/rpc", token, body.to_string().as_bytes()).unwrap();
    let v = if resp.body.is_empty() {
        Value::Null
    } else {
        serde_json::from_slice(&resp.body).unwrap()
    };
    (resp.status, v)
}

fn call(id: u64, name: &str, args: Value) -> Value {
    json!({
        "jsonrpc": "2.0", "id": id, "method": "tools/call",
        "params": { "name": name, "arguments": args }
    })
}

/// Runs the full pairing handshake over HTTP; returns the issued token.
fn do_pair(base: &str, code: &str, scope: Scope) -> (u16, Value) {
    let start = http::post_json(base, "/pair/start", None, b"{}").unwrap();
    let sb: Value = serde_json::from_slice(&start.body).unwrap();
    if start.status != 200 {
        return (start.status, sb);
    }
    let pair_id = sb["pair_id"].as_str().unwrap();
    let nonce = sb["nonce"].as_str().unwrap();
    let mac = pairing::client_mac(code, nonce).unwrap();
    let complete = http::post_json(
        base,
        "/pair/complete",
        None,
        json!({ "pair_id": pair_id, "mac": mac, "scope": scope.as_str() })
            .to_string()
            .as_bytes(),
    )
    .unwrap();
    let cb: Value = serde_json::from_slice(&complete.body).unwrap();
    (complete.status, cb)
}

#[test]
fn pair_happy_path_then_write_reaches_backend() {
    let h = start(default_pairing());
    let (status, body) = do_pair(&h.base, CODE, Scope::WriteRequest);
    assert_eq!(status, 200, "pairing failed: {body}");
    let token = body["token"].as_str().unwrap();
    assert_eq!(body["scope"], "write_request");

    // A write verb at write_request scope is FORWARDED — the mock approves by
    // default (raises no card) and returns ok, proving it reached the backend.
    let (s, resp) = rpc(
        &h.base,
        Some(token),
        call(1, "insert_text", json!({ "text": "hi" })),
    );
    assert_eq!(s, 200);
    assert!(resp.get("error").is_none(), "unexpected error: {resp}");
    assert_eq!(resp["result"]["isError"], false, "resp: {resp}");
}

#[test]
fn no_token_is_read_only_and_write_is_denied() {
    let h = start(default_pairing());

    // tools/list is filtered to the read tier (24 tools).
    let (s, resp) = rpc(
        &h.base,
        None,
        json!({ "jsonrpc": "2.0", "id": 1, "method": "tools/list" }),
    );
    assert_eq!(s, 200);
    let tools = resp["result"]["tools"].as_array().unwrap();
    assert_eq!(
        tools.len(),
        24,
        "read_only should see exactly the read tier"
    );
    let names: Vec<&str> = tools.iter().map(|t| t["name"].as_str().unwrap()).collect();
    assert!(names.contains(&"read_tab"));
    assert!(
        !names.contains(&"insert_text"),
        "write tool leaked into read_only list"
    );
    assert!(
        !names.contains(&"open_file"),
        "act tool leaked into read_only list"
    );

    // A read verb works unauthenticated.
    let (_s, r) = rpc(
        &h.base,
        None,
        call(2, "read_tab", json!({ "tab_index": 0 })),
    );
    assert_eq!(
        r["result"]["isError"], false,
        "read_tab should succeed: {r}"
    );

    // A write verb is rejected at DISPATCH with -32001, backend untouched.
    let (_s, w) = rpc(
        &h.base,
        None,
        call(3, "insert_text", json!({ "text": "x" })),
    );
    assert_eq!(w["error"]["code"], -32001, "expected scope denial: {w}");
    assert!(w.get("result").is_none());
}

#[test]
fn garbage_bearer_degrades_to_read_only() {
    let h = start(default_pairing());
    // An invalid token behaves exactly like no token (brief's degrade rule).
    let (_s, w) = rpc(
        &h.base,
        Some("deadbeefdeadbeef"),
        call(1, "insert_text", json!({ "text": "x" })),
    );
    assert_eq!(
        w["error"]["code"], -32001,
        "invalid token must not elevate: {w}"
    );
}

#[test]
fn read_act_token_allows_act_denies_write() {
    let h = start(default_pairing());
    // Seed a read_act token directly (same store the gateway reads).
    let token = h.tokens.issue(Scope::ReadAct).unwrap();

    // open_file is ACT → allowed.
    let (_s, a) = rpc(
        &h.base,
        Some(&token),
        call(1, "open_file", json!({ "path": "/tmp/np-x.txt" })),
    );
    assert_eq!(
        a["result"]["isError"], false,
        "open_file should be allowed: {a}"
    );

    // save_tab is WRITE → denied at read_act.
    let (_s, w) = rpc(&h.base, Some(&token), call(2, "save_tab", json!({})));
    assert_eq!(
        w["error"]["code"], -32001,
        "save_tab must be denied at read_act: {w}"
    );
}

#[test]
fn notification_yields_204_no_body() {
    let h = start(default_pairing());
    let resp = http::post_json(
        &h.base,
        "/rpc",
        None,
        json!({ "jsonrpc": "2.0", "method": "notifications/initialized" })
            .to_string()
            .as_bytes(),
    )
    .unwrap();
    assert_eq!(resp.status, 204);
    assert!(resp.body.is_empty());
}

#[test]
fn server_persists_only_hashed_token() {
    let h = start(default_pairing());
    let (status, body) = do_pair(&h.base, CODE, Scope::WriteRequest);
    assert_eq!(status, 200);
    let token = body["token"].as_str().unwrap();

    let server_file = h.dir.join("authorized_tokens.jsonl");
    let raw = std::fs::read_to_string(&server_file).unwrap();
    assert!(
        !raw.contains(token),
        "plaintext token must NOT be persisted server-side"
    );

    #[cfg(unix)]
    {
        use std::os::unix::fs::PermissionsExt;
        let mode = std::fs::metadata(&server_file)
            .unwrap()
            .permissions()
            .mode()
            & 0o777;
        assert_eq!(mode, 0o600, "server token file must be 0600");
    }
}

#[test]
fn wrong_code_pairing_is_rejected() {
    let h = start(default_pairing());
    let (status, body) = do_pair(&h.base, "00000000", Scope::WriteRequest);
    assert_eq!(status, 401, "wrong code must not issue a token: {body}");
    assert!(body.get("token").is_none());
}

#[test]
fn requested_scope_capped_by_serve_max_scope() {
    // serve started with --max-scope read_only: a write_request request is
    // capped to read_only even with a correct code.
    let h = start(PairingState::with_code(
        CODE.into(),
        Scope::ReadOnly,
        pairing::DEFAULT_TTL,
        pairing::DEFAULT_ATTEMPTS,
    ));
    let (status, body) = do_pair(&h.base, CODE, Scope::WriteRequest);
    assert_eq!(status, 200, "{body}");
    assert_eq!(
        body["scope"], "read_only",
        "granted scope must be min(requested, max)"
    );
}

#[test]
fn require_token_refuses_unauthenticated_reads() {
    // --require-token removes the read floor: even a READ verb is denied without
    // a valid token, and a notification is silently dropped (204).
    let h = start_opts(default_pairing(), true);

    let (_s, r) = rpc(
        &h.base,
        None,
        call(1, "read_tab", json!({ "tab_index": 0 })),
    );
    assert_eq!(
        r["error"]["code"], -32001,
        "unauthenticated read must be refused: {r}"
    );
    assert!(r.get("result").is_none());

    let (s, w) = rpc(
        &h.base,
        Some("deadbeefdeadbeef"),
        call(2, "insert_text", json!({ "text": "x" })),
    );
    assert_eq!(s, 200);
    assert_eq!(
        w["error"]["code"], -32001,
        "invalid token must be refused: {w}"
    );

    // A VALID token still works (read here; writes still hit the local card).
    let token = h.tokens.issue(Scope::ReadOnly).unwrap();
    let (_s, ok) = rpc(
        &h.base,
        Some(&token),
        call(3, "read_tab", json!({ "tab_index": 0 })),
    );
    assert_eq!(
        ok["result"]["isError"], false,
        "authed read must pass: {ok}"
    );
}

#[test]
fn loopback_only_bind() {
    assert!(bind_loopback("0.0.0.0:0".parse().unwrap()).is_err());
    assert!(bind_loopback("127.0.0.1:0".parse().unwrap()).is_ok());
}
