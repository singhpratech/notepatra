// SPDX-License-Identifier: GPL-3.0-or-later
//! Transport for a running Notepatra editor over its DEDICATED MCP bridge
//! socket (v0.1.118). This is NOT the single-instance remote-open socket —
//! the C++ side ships a separate QLocalServer just for MCP.
//!
//! Wire contract (agreed with the C++ bridge, v0.1.118):
//!
//! * Endpoint name: `"notepatra-" + first 16 hex of SHA-1(UTF-8 home dir) +
//!   "-mcp"` — the single-instance name (`SingleInstance::serverName()`) plus
//!   an `-mcp` suffix, so each user gets a distinct bridge. Qt materializes it
//!   at `$TMPDIR/<name>` (default `/tmp/<name>`) on Unix and as the named pipe
//!   `\\.\pipe\<name>` on Windows.
//! * Greeting before payload (proof-of-life law): on accept, the editor sends
//!   ONE greeting line `{"notepatra_mcp":1,"app":"Notepatra","version":...}`.
//!   The client MUST read and validate it before sending anything — receiving
//!   it proves the editor's event loop is pumping, so a request can never sit
//!   queued behind a hung-but-alive editor.
//! * Requests: `{"id":N,"verb":str,"args":{}}`, newline-delimited, sequential
//!   ids. Responses: `{"id":N,"ok":true,"result":...}` or
//!   `{"id":N,"ok":false,"error":str}`. Round-trips are blocking with a read
//!   timeout (5s; 15s for `search_project`).
//! * Editor unreachable (connect refused / socket missing) surfaces as the
//!   clean tool error "Notepatra is not running (start the editor first)".
//!
//! Transports: Unix domain socket (`std::os::unix::net::UnixStream`, native
//! read timeouts) and, since v0.1.119, the Windows named pipe at
//! `\\.\pipe\<name>` opened as a byte-stream file (`std::fs::OpenOptions`).
//! Because `std::fs::File` exposes no per-read timeout on Windows, the pipe
//! client runs a dedicated reader thread and enforces the same 5s/15s/130s
//! windows with `mpsc::recv_timeout`. No extra dependencies — std only.
//!
//! ASSUMPTION (flag for C++ reconciliation): the Windows endpoint name equals
//! Qt's `QLocalServer` name verbatim under the `\\.\pipe\` prefix, and the
//! HOME-equivalent used for the SHA-1 name derivation is what
//! `QDir::homePath()` returns on Windows — `%USERPROFILE%` with FORWARD
//! slashes. The Rust side hashes `$USERPROFILE` verbatim; if Qt normalizes
//! separators differently the two names would diverge. Runtime behavior on
//! Windows is UNVERIFIED here (Linux dev host); only cross-compilation is
//! checked.

#[cfg(any(unix, windows))]
use std::cell::RefCell;
use std::time::Duration;

use serde_json::{json, Value};

use super::{
    EditorTransport, SearchHit, SearchResults, Selection, TabContent, TabInfo, TabSelector,
    TransportError,
};

/// Exact user-facing message when the bridge socket cannot be reached.
pub const NOT_RUNNING: &str = "Notepatra is not running (start the editor first)";

const DEFAULT_TIMEOUT: Duration = Duration::from_secs(5);
const SEARCH_TIMEOUT: Duration = Duration::from_secs(15);
/// Write verbs block inside the editor on a human approval card for up to
/// 120 s, so the socket read must outlast that window.
const APPROVAL_TIMEOUT: Duration = Duration::from_secs(130);

/// The approval-gated write verbs (must match the C++ bridge's card-gated
/// verb set exactly). v0.1.119 adds create_note/append_note/set_reminder/
/// export_diagram to the v0.1.118 quartet; phase 1 adds set_diagram_source.
const WRITE_VERBS: [&str; 11] = [
    "insert_text",
    "replace_selection",
    "apply_edit",
    "save_tab",
    "create_note",
    "append_note",
    "set_reminder",
    "export_diagram",
    "set_diagram_source",
    // phase 2
    "export_query_results",
    "export_chart",
];

/// Mirrors `SingleInstance::serverName()` in src/singleinstance.cpp.
pub fn server_name(home_dir: &str) -> String {
    let digest = sha1(home_dir.as_bytes());
    let hex: String = digest.iter().map(|b| format!("{b:02x}")).collect();
    format!("notepatra-{}", &hex[..16])
}

/// The dedicated MCP bridge endpoint name: single-instance name + "-mcp".
pub fn mcp_server_name(home_dir: &str) -> String {
    format!("{}-mcp", server_name(home_dir))
}

/// Where Qt places the QLocalServer endpoint for `name` on this platform.
pub fn socket_path(name: &str) -> String {
    if cfg!(windows) {
        format!(r"\\.\pipe\{name}")
    } else {
        let tmp = std::env::var("TMPDIR").unwrap_or_else(|_| "/tmp".into());
        format!("{}/{name}", tmp.trim_end_matches('/'))
    }
}

pub struct SocketEditor {
    path: String,
    base_timeout: Duration,
    search_timeout: Duration,
    approval_timeout: Duration,
    #[cfg(any(unix, windows))]
    conn: RefCell<Option<Conn>>,
}

impl SocketEditor {
    /// Targets the current user's bridge socket (derived from the home dir).
    pub fn new() -> Self {
        // The name derivation hashes the same string Qt's
        // SingleInstance::serverName() hashes: `QDir::homePath()`. On Windows
        // that is %USERPROFILE% with FORWARD slashes, so normalize before
        // hashing (flag: reconcile with the C++ derivation if it differs).
        #[cfg(windows)]
        let home = std::env::var("USERPROFILE")
            .unwrap_or_default()
            .replace('\\', "/");
        #[cfg(not(windows))]
        let home = std::env::var("HOME")
            .or_else(|_| std::env::var("USERPROFILE"))
            .unwrap_or_default();
        Self::with_socket_path(socket_path(&mcp_server_name(&home)))
    }

    /// Targets an explicit socket path (tests use this with a fake bridge).
    pub fn with_socket_path(path: impl Into<String>) -> Self {
        Self {
            path: path.into(),
            base_timeout: DEFAULT_TIMEOUT,
            search_timeout: SEARCH_TIMEOUT,
            approval_timeout: APPROVAL_TIMEOUT,
            #[cfg(any(unix, windows))]
            conn: RefCell::new(None),
        }
    }

    /// Overrides the read timeouts (tests shrink them to keep suites fast).
    pub fn with_timeouts(mut self, base: Duration, search: Duration) -> Self {
        self.base_timeout = base;
        self.search_timeout = search;
        self
    }

    /// Overrides the approval-window read timeout for the write verbs
    /// (tests shrink it to keep suites fast).
    pub fn with_approval_timeout(mut self, approval: Duration) -> Self {
        self.approval_timeout = approval;
        self
    }

    pub fn socket_path(&self) -> &str {
        &self.path
    }

    /// One blocking request/response round-trip; lazily connects (greeting
    /// validated first) and drops the connection on any failure so the next
    /// call reconnects from scratch. Same code path on Unix (domain socket)
    /// and Windows (named pipe) — the platform difference lives entirely in
    /// [`Conn`].
    #[cfg(any(unix, windows))]
    fn call(&self, verb: &str, args: Value) -> Result<Value, TransportError> {
        let timeout = if verb == "search_project" {
            self.search_timeout
        } else if WRITE_VERBS.contains(&verb) {
            // The editor holds the response until the user answers the
            // approval card (up to 120 s) — wait it out.
            self.approval_timeout
        } else {
            self.base_timeout
        };
        let mut guard = self.conn.borrow_mut();
        if guard.is_none() {
            *guard = Some(Conn::establish(&self.path, self.base_timeout)?);
        }
        let conn = guard.as_mut().expect("connection just established");
        match conn.round_trip(verb, args, timeout) {
            Ok(v) => Ok(v),
            Err(e) => {
                *guard = None;
                Err(e)
            }
        }
    }

    /// Genuinely unsupported platforms (neither Unix nor Windows, e.g. wasm):
    /// there is no local IPC endpoint to reach the editor over.
    #[cfg(not(any(unix, windows)))]
    fn call(&self, _verb: &str, _args: Value) -> Result<Value, TransportError> {
        Err(TransportError(format!(
            "the Notepatra MCP bridge socket is not supported on this platform; \
             run notepatra-mcp without --socket to use the built-in mock editor \
             (expected endpoint: {})",
            self.path
        )))
    }
}

impl Default for SocketEditor {
    fn default() -> Self {
        Self::new()
    }
}

// ── Shared wire helpers (both transports) ──────────────────────────────────

/// Validates the editor's proof-of-life greeting line (JSON with
/// `notepatra_mcp:1`). Shared by both transports so the greeting law is
/// enforced identically on Unix and Windows.
#[cfg(any(unix, windows))]
fn validate_greeting(line: &str) -> Result<(), TransportError> {
    let v: Value = serde_json::from_str(line).map_err(|_| {
        TransportError("unexpected greeting from the editor bridge socket (not valid JSON)".into())
    })?;
    if v.get("notepatra_mcp").and_then(Value::as_u64) != Some(1) {
        return Err(TransportError(
            "unexpected greeting from the editor bridge socket \
             (missing notepatra_mcp:1 — wrong socket or incompatible editor)"
                .into(),
        ));
    }
    Ok(())
}

/// Serializes one newline-terminated request line.
#[cfg(any(unix, windows))]
fn build_request(id: u64, verb: &str, args: &Value) -> Result<String, TransportError> {
    let mut line = serde_json::to_string(&json!({ "id": id, "verb": verb, "args": args }))
        .map_err(|e| TransportError(format!("request serialization failed: {e}")))?;
    line.push('\n');
    Ok(line)
}

/// Parses a response line, checking the id and unwrapping the `ok`/`result`/
/// `error` envelope (errors pass through verbatim).
#[cfg(any(unix, windows))]
fn parse_response(resp: &str, id: u64, verb: &str) -> Result<Value, TransportError> {
    let v: Value = serde_json::from_str(resp)
        .map_err(|_| TransportError(format!("malformed response from the editor for {verb}")))?;
    if v.get("id").and_then(Value::as_u64) != Some(id) {
        return Err(TransportError(format!(
            "editor response id mismatch for {verb} (expected {id})"
        )));
    }
    match v.get("ok").and_then(Value::as_bool) {
        Some(true) => Ok(v.get("result").cloned().unwrap_or(Value::Null)),
        Some(false) => Err(TransportError(
            v.get("error")
                .and_then(Value::as_str)
                .unwrap_or("editor reported an unspecified error")
                .to_string(),
        )),
        None => Err(TransportError(format!(
            "malformed response from the editor for {verb} (missing \"ok\")"
        ))),
    }
}

// ── Unix domain socket transport ───────────────────────────────────────────

#[cfg(unix)]
struct Conn {
    reader: std::io::BufReader<std::os::unix::net::UnixStream>,
    next_id: u64,
}

#[cfg(unix)]
impl Conn {
    fn establish(path: &str, timeout: Duration) -> Result<Self, TransportError> {
        use std::os::unix::net::UnixStream;

        let stream =
            UnixStream::connect(path).map_err(|_| TransportError(NOT_RUNNING.to_string()))?;
        stream
            .set_read_timeout(Some(timeout))
            .map_err(|e| TransportError(format!("cannot set socket timeout: {e}")))?;
        let mut reader = std::io::BufReader::new(stream);
        // Proof-of-life: the editor speaks first. Nothing is sent until the
        // greeting has been read and validated.
        let greeting = read_line_from(&mut reader, "the editor's greeting")?;
        validate_greeting(&greeting)?;
        Ok(Self { reader, next_id: 1 })
    }

    fn round_trip(
        &mut self,
        verb: &str,
        args: Value,
        timeout: Duration,
    ) -> Result<Value, TransportError> {
        use std::io::Write;

        let id = self.next_id;
        self.next_id += 1;
        self.reader
            .get_ref()
            .set_read_timeout(Some(timeout))
            .map_err(|e| TransportError(format!("cannot set socket timeout: {e}")))?;
        let line = build_request(id, verb, &args)?;
        let mut writer = self.reader.get_ref();
        writer
            .write_all(line.as_bytes())
            .and_then(|()| writer.flush())
            .map_err(|e| TransportError(format!("editor connection lost while sending: {e}")))?;
        let resp = read_line_from(&mut self.reader, &format!("the {verb} response"))?;
        parse_response(&resp, id, verb)
    }
}

#[cfg(unix)]
fn read_line_from(
    reader: &mut std::io::BufReader<std::os::unix::net::UnixStream>,
    what: &str,
) -> Result<String, TransportError> {
    use std::io::BufRead;

    let mut line = String::new();
    match reader.read_line(&mut line) {
        Ok(0) => Err(TransportError(format!(
            "the editor closed the connection while waiting for {what}"
        ))),
        Ok(_) => Ok(line),
        Err(e)
            if e.kind() == std::io::ErrorKind::WouldBlock
                || e.kind() == std::io::ErrorKind::TimedOut =>
        {
            Err(TransportError(format!(
                "timed out waiting for {what} from the editor"
            )))
        }
        Err(e) => Err(TransportError(format!(
            "editor connection failed while waiting for {what}: {e}"
        ))),
    }
}

// ── Windows named-pipe transport (v0.1.119) ────────────────────────────────
//
// `std::fs::File` over a named pipe is a byte stream with no per-read timeout,
// so a dedicated reader thread pumps lines into a channel and the caller uses
// `recv_timeout` to honor the same 5s/15s/130s windows the Unix path gets from
// `set_read_timeout`. std-only; no winapi. UNVERIFIED at runtime (built and
// checked on Linux via `--target x86_64-pc-windows-gnu`).

/// One item from the pipe reader thread.
#[cfg(windows)]
enum LineMsg {
    /// A complete newline-delimited line.
    Line(String),
    /// The editor closed its end (read returned 0).
    Eof,
    /// The read failed; carries the OS error text.
    Err(String),
}

#[cfg(windows)]
struct Conn {
    /// Write half of the pipe (the reader thread owns a cloned read half).
    writer: std::fs::File,
    rx: std::sync::mpsc::Receiver<LineMsg>,
    /// Detached on drop; the thread exits when the pipe closes.
    _reader: std::thread::JoinHandle<()>,
    next_id: u64,
}

#[cfg(windows)]
impl Conn {
    fn establish(path: &str, timeout: Duration) -> Result<Self, TransportError> {
        use std::fs::OpenOptions;
        use std::io::{BufRead, BufReader};
        use std::sync::mpsc;

        // A named pipe is opened for both directions as a plain file. A
        // missing/unavailable pipe means the editor isn't running (or isn't
        // serving the bridge) — the same clean error as a refused Unix socket.
        let file = OpenOptions::new()
            .read(true)
            .write(true)
            .open(path)
            .map_err(|_| TransportError(NOT_RUNNING.to_string()))?;
        let read_half = file
            .try_clone()
            .map_err(|e| TransportError(format!("cannot clone pipe handle: {e}")))?;

        let (tx, rx) = mpsc::channel::<LineMsg>();
        let reader = std::thread::spawn(move || {
            let mut buf = BufReader::new(read_half);
            loop {
                let mut line = String::new();
                match buf.read_line(&mut line) {
                    Ok(0) => {
                        let _ = tx.send(LineMsg::Eof);
                        break;
                    }
                    Ok(_) => {
                        // Receiver gone (Conn dropped) — stop pumping.
                        if tx.send(LineMsg::Line(line)).is_err() {
                            break;
                        }
                    }
                    Err(e) => {
                        let _ = tx.send(LineMsg::Err(e.to_string()));
                        break;
                    }
                }
            }
        });

        let conn = Self {
            writer: file,
            rx,
            _reader: reader,
            next_id: 1,
        };
        // Proof-of-life: read + validate the greeting BEFORE sending anything.
        let greeting = conn.recv_line(timeout, "the editor's greeting")?;
        validate_greeting(&greeting)?;
        Ok(conn)
    }

    /// Blocks up to `timeout` for the next line from the reader thread.
    fn recv_line(&self, timeout: Duration, what: &str) -> Result<String, TransportError> {
        use std::sync::mpsc::RecvTimeoutError;
        match self.rx.recv_timeout(timeout) {
            Ok(LineMsg::Line(l)) => Ok(l),
            Ok(LineMsg::Eof) | Err(RecvTimeoutError::Disconnected) => Err(TransportError(format!(
                "the editor closed the connection while waiting for {what}"
            ))),
            Ok(LineMsg::Err(e)) => Err(TransportError(format!(
                "editor connection failed while waiting for {what}: {e}"
            ))),
            Err(RecvTimeoutError::Timeout) => Err(TransportError(format!(
                "timed out waiting for {what} from the editor"
            ))),
        }
    }

    fn round_trip(
        &mut self,
        verb: &str,
        args: Value,
        timeout: Duration,
    ) -> Result<Value, TransportError> {
        use std::io::Write;

        let id = self.next_id;
        self.next_id += 1;
        let line = build_request(id, verb, &args)?;
        self.writer
            .write_all(line.as_bytes())
            .and_then(|()| self.writer.flush())
            .map_err(|e| TransportError(format!("editor connection lost while sending: {e}")))?;
        let resp = self.recv_line(timeout, &format!("the {verb} response"))?;
        parse_response(&resp, id, verb)
    }
}

fn malformed(field: &str) -> TransportError {
    TransportError(format!(
        "malformed editor response (missing or mistyped {field:?})"
    ))
}

fn required_str(v: &Value, field: &str) -> Result<String, TransportError> {
    v.get(field)
        .and_then(Value::as_str)
        .map(str::to_string)
        .ok_or_else(|| malformed(field))
}

/// The bridge always sends `path` as a string; `""` (untitled tab) maps to
/// `None`.
fn path_field(v: &Value, field: &str) -> Result<Option<String>, TransportError> {
    let s = required_str(v, field)?;
    Ok(if s.is_empty() { None } else { Some(s) })
}

/// One `{index,title,path,modified}` entry from `list_open_tabs`. Exact
/// shape only — every field is required, as the C++ bridge always sends all
/// four.
fn tab_info_from(v: &Value) -> Result<TabInfo, TransportError> {
    Ok(TabInfo {
        index: v
            .get("index")
            .and_then(Value::as_u64)
            .ok_or_else(|| malformed("index"))? as usize,
        title: required_str(v, "title")?,
        path: path_field(v, "path")?,
        modified: v
            .get("modified")
            .and_then(Value::as_bool)
            .ok_or_else(|| malformed("modified"))?,
    })
}

impl EditorTransport for SocketEditor {
    fn open_file(&mut self, path: &str) -> Result<usize, TransportError> {
        // Wire result: {"opened":true,"tab_index":N}.
        let v = self.call("open_file", json!({ "path": path }))?;
        if v.get("opened").and_then(Value::as_bool) != Some(true) {
            return Err(malformed("opened"));
        }
        Ok(v.get("tab_index")
            .and_then(Value::as_u64)
            .ok_or_else(|| malformed("tab_index"))? as usize)
    }

    fn list_open_tabs(&self) -> Result<Vec<TabInfo>, TransportError> {
        // Wire result: {"tabs":[{index,title,path,modified}]} — exactly.
        let v = self.call("list_open_tabs", json!({}))?;
        v.get("tabs")
            .and_then(Value::as_array)
            .ok_or_else(|| malformed("tabs"))?
            .iter()
            .map(tab_info_from)
            .collect()
    }

    fn read_tab(&self, selector: TabSelector<'_>) -> Result<TabContent, TransportError> {
        // Request takes "index" (NOT "tab_index") or "title"; result is
        // {"title","path","text"} with "truncated":true only when capped.
        let args = match selector {
            TabSelector::Index(i) => json!({ "index": i }),
            TabSelector::Title(t) => json!({ "title": t }),
        };
        let v = self.call("read_tab", args)?;
        Ok(TabContent {
            title: required_str(&v, "title")?,
            path: path_field(&v, "path")?,
            text: required_str(&v, "text")?,
            truncated: v.get("truncated").and_then(Value::as_bool).unwrap_or(false),
        })
    }

    fn search_project(
        &self,
        query: &str,
        max_results: usize,
        regex: bool,
    ) -> Result<SearchResults, TransportError> {
        // Wire result: {"results":[{path,line,text}],"truncated":bool}.
        // "regex" is sent only when true so the wire stays minimal.
        let mut args = json!({ "query": query, "max_results": max_results });
        if regex {
            args["regex"] = json!(true);
        }
        let v = self.call("search_project", args)?;
        let results = v
            .get("results")
            .and_then(Value::as_array)
            .ok_or_else(|| malformed("results"))?
            .iter()
            .map(|h| {
                Ok(SearchHit {
                    path: required_str(h, "path")?,
                    line: h
                        .get("line")
                        .and_then(Value::as_u64)
                        .ok_or_else(|| malformed("line"))? as usize,
                    text: required_str(h, "text")?,
                })
            })
            .collect::<Result<Vec<_>, TransportError>>()?;
        Ok(SearchResults {
            results,
            truncated: v
                .get("truncated")
                .and_then(Value::as_bool)
                .ok_or_else(|| malformed("truncated"))?,
        })
    }

    fn get_selection(&self) -> Result<Selection, TransportError> {
        // Wire result: {"text","tab_index"} — tab_index may be -1.
        let v = self.call("get_selection", json!({}))?;
        Ok(Selection {
            tab_index: v
                .get("tab_index")
                .and_then(Value::as_i64)
                .ok_or_else(|| malformed("tab_index"))?,
            text: required_str(&v, "text")?,
        })
    }

    // Wave-2 verbs pass the editor's JSON result through verbatim; the wire
    // shape (documented on EditorTransport) is the single source of truth.

    fn get_status(&self) -> Result<Value, TransportError> {
        self.call("get_status", json!({}))
    }

    fn app_info(&self) -> Result<Value, TransportError> {
        self.call("app_info", json!({}))
    }

    fn list_recent_files(&self) -> Result<Value, TransportError> {
        self.call("list_recent_files", json!({}))
    }

    fn find_in_tab(
        &self,
        tab_index: Option<usize>,
        query: &str,
        regex: bool,
    ) -> Result<Value, TransportError> {
        // The bridge reads the tab from "index" (NOT "tab_index") here.
        let mut args = json!({ "query": query });
        if let Some(i) = tab_index {
            args["index"] = json!(i);
        }
        // "regex" is sent only when true so the wire stays minimal.
        if regex {
            args["regex"] = json!(true);
        }
        self.call("find_in_tab", args)
    }

    fn new_tab(&mut self, text: Option<&str>) -> Result<Value, TransportError> {
        let args = match text {
            Some(t) => json!({ "text": t }),
            None => json!({}),
        };
        self.call("new_tab", args)
    }

    fn goto_line(
        &mut self,
        line: usize,
        tab_index: Option<usize>,
    ) -> Result<Value, TransportError> {
        let mut args = json!({ "line": line });
        if let Some(i) = tab_index {
            args["tab_index"] = json!(i);
        }
        self.call("goto_line", args)
    }

    fn set_language(
        &mut self,
        language: &str,
        tab_index: Option<usize>,
    ) -> Result<Value, TransportError> {
        let mut args = json!({ "language": language });
        if let Some(i) = tab_index {
            args["tab_index"] = json!(i);
        }
        self.call("set_language", args)
    }

    fn compare_tabs(&mut self, index_a: usize, index_b: usize) -> Result<Value, TransportError> {
        self.call(
            "compare_tabs",
            json!({ "index_a": index_a, "index_b": index_b }),
        )
    }

    fn format_text(&self, kind: &str, text: &str) -> Result<Value, TransportError> {
        self.call("format_text", json!({ "kind": kind, "text": text }))
    }

    fn list_notes(&self) -> Result<Value, TransportError> {
        self.call("list_notes", json!({}))
    }

    fn read_note(&self, file: &str) -> Result<Value, TransportError> {
        self.call("read_note", json!({ "file": file }))
    }

    // Write verbs (v0.1.118): the editor gates each one behind an in-app
    // approval card, so these round-trips use the long approval timeout (see
    // `call`). "denied by user" / "approval timed out" arrive as ordinary
    // ok:false errors and pass through verbatim.

    fn insert_text(
        &mut self,
        text: &str,
        tab_index: Option<usize>,
        line: Option<usize>,
        col: Option<usize>,
    ) -> Result<Value, TransportError> {
        let mut args = json!({ "text": text });
        if let Some(i) = tab_index {
            args["tab_index"] = json!(i);
        }
        if let Some(l) = line {
            args["line"] = json!(l);
        }
        if let Some(c) = col {
            args["col"] = json!(c);
        }
        self.call("insert_text", args)
    }

    fn replace_selection(
        &mut self,
        text: &str,
        tab_index: Option<usize>,
    ) -> Result<Value, TransportError> {
        let mut args = json!({ "text": text });
        if let Some(i) = tab_index {
            args["tab_index"] = json!(i);
        }
        self.call("replace_selection", args)
    }

    fn apply_edit(
        &mut self,
        find: &str,
        replace: &str,
        tab_index: Option<usize>,
        all: bool,
    ) -> Result<Value, TransportError> {
        // "all" is always sent explicitly so the wire shape is deterministic.
        let mut args = json!({ "find": find, "replace": replace, "all": all });
        if let Some(i) = tab_index {
            args["tab_index"] = json!(i);
        }
        self.call("apply_edit", args)
    }

    fn save_tab(&mut self, tab_index: Option<usize>) -> Result<Value, TransportError> {
        let args = match tab_index {
            Some(i) => json!({ "tab_index": i }),
            None => json!({}),
        };
        self.call("save_tab", args)
    }

    // v0.1.119 read verbs — pass the editor's JSON through verbatim.

    fn list_reminders(&self) -> Result<Value, TransportError> {
        self.call("list_reminders", json!({}))
    }

    fn git_status(&self) -> Result<Value, TransportError> {
        self.call("git_status", json!({}))
    }

    fn git_diff(&self, path: Option<&str>) -> Result<Value, TransportError> {
        let args = match path {
            Some(p) => json!({ "path": p }),
            None => json!({}),
        };
        self.call("git_diff", args)
    }

    fn git_log(&self, limit: usize) -> Result<Value, TransportError> {
        self.call("git_log", json!({ "limit": limit }))
    }

    fn git_show(&self, git_ref: &str) -> Result<Value, TransportError> {
        self.call("git_show", json!({ "ref": git_ref }))
    }

    fn git_branch(&self) -> Result<Value, TransportError> {
        self.call("git_branch", json!({}))
    }

    fn validate_npd(
        &self,
        tab_index: Option<usize>,
        source: Option<&str>,
    ) -> Result<Value, TransportError> {
        // Exactly one selector is set (enforced by the tool layer). Wire keys
        // match the tool's arg names ("tab_index" / "source").
        let args = match (tab_index, source) {
            (Some(i), None) => json!({ "tab_index": i }),
            (None, Some(s)) => json!({ "source": s }),
            _ => {
                return Err(TransportError(
                    "provide exactly one of tab_index or source".into(),
                ))
            }
        };
        self.call("validate_npd", args)
    }

    fn run_sql(&self, sql: &str, csv_path: Option<&str>) -> Result<Value, TransportError> {
        let mut args = json!({ "sql": sql });
        if let Some(p) = csv_path {
            args["csv_path"] = json!(p);
        }
        self.call("run_sql", args)
    }

    // Phase 0A read verbs.

    fn list_languages(&self) -> Result<Value, TransportError> {
        self.call("list_languages", json!({}))
    }

    fn get_capabilities(&self) -> Result<Value, TransportError> {
        self.call("get_capabilities", json!({}))
    }

    // v0.1.119 act verb.

    fn open_note(&mut self, file: &str) -> Result<Value, TransportError> {
        self.call("open_note", json!({ "file": file }))
    }

    // v0.1.119 write verbs — approval-gated (long timeout via `call`).

    fn create_note(&mut self, title: &str, body: &str) -> Result<Value, TransportError> {
        self.call("create_note", json!({ "title": title, "body": body }))
    }

    fn append_note(&mut self, file: &str, text: &str) -> Result<Value, TransportError> {
        self.call("append_note", json!({ "file": file, "text": text }))
    }

    fn set_reminder(&mut self, file: &str, due_iso: &str) -> Result<Value, TransportError> {
        self.call("set_reminder", json!({ "file": file, "due_iso": due_iso }))
    }

    fn export_diagram(
        &mut self,
        tab_index: usize,
        path: &str,
        format: &str,
    ) -> Result<Value, TransportError> {
        self.call(
            "export_diagram",
            json!({ "tab_index": tab_index, "path": path, "format": format }),
        )
    }

    // Phase 1 verbs — verbatim JSON passthrough.

    fn create_diagram(
        &mut self,
        source: Option<&str>,
        title: Option<&str>,
    ) -> Result<Value, TransportError> {
        // Optional keys are sent only when present so the wire stays minimal.
        let mut args = json!({});
        if let Some(s) = source {
            args["source"] = json!(s);
        }
        if let Some(t) = title {
            args["title"] = json!(t);
        }
        self.call("create_diagram", args)
    }

    fn get_diagram_source(&self, tab_index: usize) -> Result<Value, TransportError> {
        self.call("get_diagram_source", json!({ "tab_index": tab_index }))
    }

    fn set_diagram_source(
        &mut self,
        tab_index: usize,
        source: &str,
    ) -> Result<Value, TransportError> {
        self.call(
            "set_diagram_source",
            json!({ "tab_index": tab_index, "source": source }),
        )
    }

    fn open_noter(&mut self) -> Result<Value, TransportError> {
        self.call("open_noter", json!({}))
    }

    // Phase 2 — data-analyst + charts. Optional keys are sent only when
    // present so the wire stays minimal (mirrors create_diagram).

    fn list_connections(&self) -> Result<Value, TransportError> {
        self.call("list_connections", json!({}))
    }

    fn run_query(
        &self,
        connection_name: &str,
        sql: &str,
        max_rows: Option<usize>,
    ) -> Result<Value, TransportError> {
        let mut args = json!({ "connection_name": connection_name, "sql": sql });
        if let Some(n) = max_rows {
            args["max_rows"] = json!(n);
        }
        self.call("run_query", args)
    }

    fn list_tables(&self, connection_name: &str) -> Result<Value, TransportError> {
        self.call("list_tables", json!({ "connection_name": connection_name }))
    }

    fn open_data_analyst(&mut self) -> Result<Value, TransportError> {
        self.call("open_data_analyst", json!({}))
    }

    fn render_chart(
        &mut self,
        spec: &Value,
        title: Option<&str>,
    ) -> Result<Value, TransportError> {
        let mut args = json!({ "spec": spec.clone() });
        if let Some(t) = title {
            args["title"] = json!(t);
        }
        self.call("render_chart", args)
    }

    fn export_query_results(
        &mut self,
        connection_name: &str,
        sql: &str,
        path: &str,
        format: &str,
        max_rows: Option<usize>,
    ) -> Result<Value, TransportError> {
        let mut args = json!({
            "connection_name": connection_name,
            "sql": sql,
            "path": path,
            "format": format,
        });
        if let Some(n) = max_rows {
            args["max_rows"] = json!(n);
        }
        self.call("export_query_results", args)
    }

    fn export_chart(
        &mut self,
        spec: &Value,
        path: &str,
        format: &str,
        scale: Option<usize>,
    ) -> Result<Value, TransportError> {
        let mut args = json!({ "spec": spec.clone(), "path": path, "format": format });
        if let Some(s) = scale {
            args["scale"] = json!(s);
        }
        self.call("export_chart", args)
    }
}

/// Minimal SHA-1 (only for the server-name derivation above; deps are
/// intentionally limited to serde + serde_json).
fn sha1(data: &[u8]) -> [u8; 20] {
    let mut h: [u32; 5] = [
        0x6745_2301,
        0xEFCD_AB89,
        0x98BA_DCFE,
        0x1032_5476,
        0xC3D2_E1F0,
    ];
    let bit_len = (data.len() as u64).wrapping_mul(8);
    let mut msg = data.to_vec();
    msg.push(0x80);
    while msg.len() % 64 != 56 {
        msg.push(0);
    }
    msg.extend_from_slice(&bit_len.to_be_bytes());
    for chunk in msg.chunks_exact(64) {
        let mut w = [0u32; 80];
        for (i, word) in chunk.chunks_exact(4).enumerate() {
            w[i] = u32::from_be_bytes([word[0], word[1], word[2], word[3]]);
        }
        for i in 16..80 {
            w[i] = (w[i - 3] ^ w[i - 8] ^ w[i - 14] ^ w[i - 16]).rotate_left(1);
        }
        let (mut a, mut b, mut c, mut d, mut e) = (h[0], h[1], h[2], h[3], h[4]);
        for (i, &wi) in w.iter().enumerate() {
            let (f, k) = match i {
                0..=19 => ((b & c) | (!b & d), 0x5A82_7999u32),
                20..=39 => (b ^ c ^ d, 0x6ED9_EBA1),
                40..=59 => ((b & c) | (b & d) | (c & d), 0x8F1B_BCDC),
                _ => (b ^ c ^ d, 0xCA62_C1D6),
            };
            let tmp = a
                .rotate_left(5)
                .wrapping_add(f)
                .wrapping_add(e)
                .wrapping_add(k)
                .wrapping_add(wi);
            e = d;
            d = c;
            c = b.rotate_left(30);
            b = a;
            a = tmp;
        }
        h[0] = h[0].wrapping_add(a);
        h[1] = h[1].wrapping_add(b);
        h[2] = h[2].wrapping_add(c);
        h[3] = h[3].wrapping_add(d);
        h[4] = h[4].wrapping_add(e);
    }
    let mut out = [0u8; 20];
    for (i, word) in h.iter().enumerate() {
        out[i * 4..i * 4 + 4].copy_from_slice(&word.to_be_bytes());
    }
    out
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn sha1_test_vector() {
        // FIPS 180-1 vector: SHA1("abc").
        let hex: String = sha1(b"abc").iter().map(|b| format!("{b:02x}")).collect();
        assert_eq!(hex, "a9993e364706816aba3e25717850c26c9cd0d89d");
    }

    #[test]
    fn server_name_shape() {
        let name = server_name("/home/someone");
        assert!(name.starts_with("notepatra-"));
        assert_eq!(name.len(), "notepatra-".len() + 16);
    }

    #[test]
    fn mcp_server_name_has_suffix() {
        let name = mcp_server_name("/home/someone");
        assert_eq!(name, format!("{}-mcp", server_name("/home/someone")));
        assert!(name.ends_with("-mcp"));
    }

    #[cfg(unix)]
    #[test]
    fn unreachable_socket_is_not_running() {
        let ed = SocketEditor::with_socket_path("/nonexistent/notepatra-bridge.sock");
        let err = ed.list_open_tabs().unwrap_err();
        assert_eq!(err.0, NOT_RUNNING);
    }
}
