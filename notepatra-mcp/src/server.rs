// SPDX-License-Identifier: GPL-3.0-or-later
use std::collections::HashMap;
use std::io::{self, BufRead, Write};

use serde_json::{json, Map, Value};

use crate::prompts;
use crate::tools::{self, CallOutcome};
use crate::transport::{EditorTransport, TabSelector, TransportError};

// Latest MCP spec revision known when this scaffold was written
// (modelcontextprotocol.io). Assumption noted per scaffold brief: if a newer
// revision exists, bump this list — negotiation logic is already generic.
pub const LATEST_PROTOCOL_VERSION: &str = "2025-06-18";
pub const SUPPORTED_PROTOCOL_VERSIONS: [&str; 3] = ["2025-06-18", "2025-03-26", "2024-11-05"];

pub const SERVER_NAME: &str = "notepatra-mcp";

// JSON-RPC 2.0 error codes.
const PARSE_ERROR: i64 = -32700;
const INVALID_REQUEST: i64 = -32600;
const METHOD_NOT_FOUND: i64 = -32601;
const INVALID_PARAMS: i64 = -32602;
const INTERNAL_ERROR: i64 = -32603;
/// MCP-defined "resource not found" error (spec 2025-06-18).
const RESOURCE_NOT_FOUND: i64 = -32002;

const TAB_URI_PREFIX: &str = "notepatra://tab/";
const NOTE_URI_PREFIX: &str = "notepatra://note/";

pub struct Server<T: EditorTransport> {
    transport: T,
    /// serverInfo.version, resolved once at startup: NOTEPATRA_MCP_VERSION
    /// wins over this crate's own version if it is set.
    ///
    /// NOTE: nothing in the repo currently SETS that variable — the editor does
    /// not spawn the sidecar (MCP clients launch it), so in every shipped
    /// configuration this is CARGO_PKG_VERSION. The override is kept as an
    /// escape hatch for a packager who ships a differently-versioned binary.
    version: String,
}

impl<T: EditorTransport> Server<T> {
    pub fn new(transport: T) -> Self {
        let version = std::env::var("NOTEPATRA_MCP_VERSION")
            .unwrap_or_else(|_| env!("CARGO_PKG_VERSION").to_string());
        Self { transport, version }
    }

    /// Newline-delimited JSON-RPC 2.0 over any reader/writer (the MCP stdio
    /// transport). Returns cleanly on EOF — that is the spec's shutdown signal.
    pub fn run<R: BufRead, W: Write>(&mut self, reader: R, mut writer: W) -> io::Result<()> {
        for line in reader.lines() {
            let line = line?;
            if line.trim().is_empty() {
                continue;
            }
            if let Some(response) = self.handle_line(&line) {
                serde_json::to_writer(&mut writer, &response)?;
                writer.write_all(b"\n")?;
                writer.flush()?;
            }
        }
        Ok(())
    }

    /// One inbound message -> at most one outbound message (None for
    /// notifications and client responses, which must not be answered).
    pub fn handle_line(&mut self, line: &str) -> Option<Value> {
        let msg: Value = match serde_json::from_str(line) {
            Ok(v) => v,
            Err(_) => return Some(error_response(Value::Null, PARSE_ERROR, "Parse error")),
        };
        let Value::Object(obj) = msg else {
            // JSON-RPC batching was removed from MCP in the 2025-06-18
            // revision; arrays and scalars are invalid requests.
            return Some(error_response(
                Value::Null,
                INVALID_REQUEST,
                "Invalid Request",
            ));
        };
        // MCP forbids null request ids; treat one like an absent id.
        let id = obj.get("id").cloned().filter(|v| !v.is_null());
        let method = obj.get("method").and_then(Value::as_str).map(str::to_owned);
        match (method, id) {
            // A response from the client (result/error, no method): we issue no
            // server->client requests yet, so nothing to route. No reply.
            (None, Some(_)) if obj.contains_key("result") || obj.contains_key("error") => None,
            (None, id) => Some(error_response(
                id.unwrap_or(Value::Null),
                INVALID_REQUEST,
                "Invalid Request",
            )),
            // Notifications never get a response. notifications/initialized is
            // acknowledged implicitly; unknown notifications are ignored per spec.
            (Some(_), None) => None,
            (Some(m), Some(id)) => {
                if obj.get("jsonrpc").and_then(Value::as_str) != Some("2.0") {
                    return Some(error_response(
                        id,
                        INVALID_REQUEST,
                        "Invalid Request: jsonrpc must be \"2.0\"",
                    ));
                }
                Some(self.handle_request(&m, obj.get("params"), id))
            }
        }
    }

    fn handle_request(&mut self, method: &str, params: Option<&Value>, id: Value) -> Value {
        match method {
            "initialize" => initialize(params, id, &self.version, self.transport.is_mock()),
            "ping" => result_response(id, json!({})),
            "tools/list" => list_page(params, id, json!({ "tools": tools::definitions() })),
            "tools/call" => self.tools_call(params, id),
            "resources/list" => self.resources_list(id),
            "resources/read" => self.resources_read(params, id),
            "prompts/list" => list_page(params, id, json!({ "prompts": prompts::definitions() })),
            "prompts/get" => self.prompts_get(params, id),
            other => error_response(id, METHOD_NOT_FOUND, &format!("Method not found: {other}")),
        }
    }

    /// Open tabs and Noter notes as MCP resources. Notes are best-effort:
    /// an editor build without Noter still lists its tabs.
    fn resources_list(&mut self, id: Value) -> Value {
        let mut resources = Vec::new();
        let tabs = match self.transport.list_open_tabs() {
            Ok(tabs) => tabs,
            Err(e) => return error_response(id, INTERNAL_ERROR, &e.0),
        };
        for t in tabs {
            resources.push(json!({
                "uri": format!("{TAB_URI_PREFIX}{}", t.index),
                "name": t.title,
                "mimeType": "text/plain",
            }));
        }
        if let Ok(listing) = self.transport.list_notes() {
            if let Some(notes) = listing.get("notes").and_then(Value::as_array) {
                // Root-relative note IDs: the URI carries the note's basename
                // (list_notes reports absolute paths). Duplicate basenames
                // fall back to the URI-encoded full path so every note stays
                // addressable; resources/read maps IDs back via list_notes.
                let mut counts: HashMap<&str, usize> = HashMap::new();
                for n in notes {
                    if let Some(file) = n.get("file").and_then(Value::as_str) {
                        *counts.entry(note_basename(file)).or_insert(0) += 1;
                    }
                }
                for n in notes {
                    if let (Some(file), Some(title)) = (
                        n.get("file").and_then(Value::as_str),
                        n.get("title").and_then(Value::as_str),
                    ) {
                        let base = note_basename(file);
                        let note_id = if counts.get(base).copied().unwrap_or(0) > 1 {
                            percent_encode(file)
                        } else {
                            percent_encode(base)
                        };
                        resources.push(json!({
                            "uri": format!("{NOTE_URI_PREFIX}{note_id}"),
                            "name": title,
                            "mimeType": "text/plain",
                        }));
                    }
                }
            }
        }
        result_response(id, json!({ "resources": resources }))
    }

    /// Maps a decoded note ID (basename, or an absolute path from the
    /// collision fallback) back to the note's absolute path, which is what
    /// the bridge's `read_note` verb takes. Ambiguous basenames resolve to
    /// nothing — those notes are only addressable by their full-path URI.
    fn resolve_note_file(&mut self, note_id: &str) -> Result<Option<String>, TransportError> {
        let listing = self.transport.list_notes()?;
        let files: Vec<String> = listing
            .get("notes")
            .and_then(Value::as_array)
            .map(|notes| {
                notes
                    .iter()
                    .filter_map(|n| n.get("file").and_then(Value::as_str))
                    .map(str::to_string)
                    .collect()
            })
            .unwrap_or_default();
        if files.iter().any(|f| f == note_id) {
            return Ok(Some(note_id.to_string()));
        }
        let mut matches = files.iter().filter(|f| note_basename(f) == note_id);
        match (matches.next(), matches.next()) {
            (Some(f), None) => Ok(Some(f.clone())),
            _ => Ok(None),
        }
    }

    fn resources_read(&mut self, params: Option<&Value>, id: Value) -> Value {
        let Some(uri) = params
            .and_then(|p| p.get("uri"))
            .and_then(Value::as_str)
            .map(str::to_owned)
        else {
            return error_response(id, INVALID_PARAMS, "Invalid params: missing uri");
        };
        let text = if let Some(index) = uri.strip_prefix(TAB_URI_PREFIX) {
            let Ok(index) = index.parse::<usize>() else {
                return resource_not_found(id, &uri);
            };
            match self.transport.read_tab(TabSelector::Index(index), None) {
                // resources/read has no structured truncation field, so the
                // "[truncated at 5 MB]" marker rides in the text itself —
                // same as the read_tab tool (kept simple on purpose).
                Ok(content) => content.text_with_marker(),
                Err(e) => return read_failure(id, &uri, e.0),
            }
        } else if let Some(raw_id) = uri.strip_prefix(NOTE_URI_PREFIX) {
            // Note URIs carry a root-relative ID (basename, or the
            // URI-encoded absolute path on basename collisions); map it back
            // to the absolute path the bridge's read_note verb expects.
            let Some(note_id) = percent_decode(raw_id) else {
                return resource_not_found(id, &uri);
            };
            let file = match self.resolve_note_file(&note_id) {
                Ok(Some(f)) => f,
                Ok(None) => return resource_not_found(id, &uri),
                Err(e) => return read_failure(id, &uri, e.0),
            };
            match self.transport.read_note(&file) {
                Ok(v) => v
                    .get("text")
                    .and_then(Value::as_str)
                    .unwrap_or_default()
                    .to_string(),
                Err(e) => return read_failure(id, &uri, e.0),
            }
        } else {
            return resource_not_found(id, &uri);
        };
        result_response(
            id,
            json!({ "contents": [{ "uri": uri, "mimeType": "text/plain", "text": text }] }),
        )
    }

    fn prompts_get(&mut self, params: Option<&Value>, id: Value) -> Value {
        let Some(name) = params.and_then(|p| p.get("name")).and_then(Value::as_str) else {
            return error_response(id, INVALID_PARAMS, "Invalid params: missing prompt name");
        };
        match prompts::get(&mut self.transport, name) {
            Ok(result) => result_response(id, result),
            Err(prompts::GetError::Unknown(name)) => error_response(
                id,
                INVALID_PARAMS,
                &format!("Invalid params: unknown prompt: {name}"),
            ),
            Err(prompts::GetError::Editor(msg)) => error_response(id, INTERNAL_ERROR, &msg),
        }
    }

    fn tools_call(&mut self, params: Option<&Value>, id: Value) -> Value {
        let Some(Value::Object(p)) = params else {
            return error_response(id, INVALID_PARAMS, "Invalid params: expected an object");
        };
        let Some(name) = p.get("name").and_then(Value::as_str) else {
            return error_response(id, INVALID_PARAMS, "Invalid params: missing tool name");
        };
        let empty = Map::new();
        let args = match p.get("arguments") {
            None => &empty,
            Some(Value::Object(m)) => m,
            Some(_) => {
                return error_response(
                    id,
                    INVALID_PARAMS,
                    "Invalid params: arguments must be an object",
                )
            }
        };
        let mock = self.transport.is_mock();
        match tools::call(&mut self.transport, name, args) {
            CallOutcome::Ok(text) => result_response(id, tool_result(mock, text, false)),
            CallOutcome::ToolError(text) => result_response(id, tool_result(mock, text, true)),
            CallOutcome::InvalidParams(msg) => {
                error_response(id, INVALID_PARAMS, &format!("Invalid params: {msg}"))
            }
            CallOutcome::UnknownTool(name) => {
                error_response(id, INVALID_PARAMS, &format!("Unknown tool: {name}"))
            }
        }
    }
}

/// Answer a `*/list` method, rejecting a pagination cursor we never issued
/// (v0.1.126, NP-12).
///
/// This server returns every entry in one page and never sets `nextCursor`, so
/// the only cursor a client can send is one it invented or one left over from
/// a different server. v0.1.125 IGNORED it and returned the full list, which
/// is indistinguishable from "your cursor was valid and this is page 2" — a
/// paginating client would loop or silently double-count. MCP specifies
/// -32602 for an invalid cursor, so say so.
fn list_page(params: Option<&Value>, id: Value, result: Value) -> Value {
    if let Some(Value::Object(p)) = params {
        if let Some(c) = p.get("cursor") {
            if !c.is_null() {
                return error_response(
                    id,
                    INVALID_PARAMS,
                    "Invalid params: unknown cursor — this server returns every \
                     entry in a single page and never issues a nextCursor",
                );
            }
        }
    }
    result_response(id, result)
}

/// Build a `tools/call` result, marking it when the data is fabricated
/// (v0.1.126, NP-06).
///
/// Without `--socket` the server answers from an in-memory demo editor. That
/// is a feature — any MCP client can exercise the protocol with nothing
/// installed — but v0.1.125 said so only in `--help`, and the consumer here is
/// a language model that never runs `--help`. Every mock response was
/// `isError:false` with no marker of any kind, so a config that lost its
/// `--socket` produced an assistant confidently describing three files that do
/// not exist, on POSIX paths, on a Windows box. The failure was silent and
/// shaped exactly like success.
///
/// The notice is its own CONTENT BLOCK rather than a prefix on the payload.
/// Most of this surface returns JSON as text; prefixing would have corrupted
/// every one of those results to fix a labelling problem. A separate block
/// reaches the model (clients concatenate text blocks) and leaves the payload
/// byte-identical. `_meta.mock` carries the same fact for code.
fn tool_result(is_mock: bool, text: String, is_error: bool) -> Value {
    if !is_mock {
        return json!({
            "content": [{ "type": "text", "text": text }],
            "isError": is_error,
        });
    }
    json!({
        "content": [
            { "type": "text", "text": MOCK_NOTICE },
            { "type": "text", "text": text },
        ],
        "isError": is_error,
        "_meta": { "mock": true },
    })
}

pub const MOCK_NOTICE: &str =
    "[MOCK DATA] No editor is connected. notepatra-mcp is running without \
--socket, so everything in the next content block is FABRICATED demo content, \
not the user's real files. Do not act on it or describe it as the user's data; \
tell the user to add --socket to the server command.";

/// Last path segment, tolerant of both separators (the bridge sends native
/// separators — backslashes on Windows).
fn note_basename(path: &str) -> &str {
    path.rsplit(['/', '\\']).next().unwrap_or(path)
}

/// RFC 3986 percent-encoding of everything outside the unreserved set, so a
/// full absolute path survives inside a URI path segment.
fn percent_encode(s: &str) -> String {
    let mut out = String::with_capacity(s.len());
    for b in s.bytes() {
        match b {
            b'A'..=b'Z' | b'a'..=b'z' | b'0'..=b'9' | b'-' | b'_' | b'.' | b'~' => {
                out.push(b as char)
            }
            _ => out.push_str(&format!("%{b:02X}")),
        }
    }
    out
}

/// Inverse of [`percent_encode`]; `None` on malformed escapes or invalid
/// UTF-8 (callers treat that as an unknown resource).
fn percent_decode(s: &str) -> Option<String> {
    let mut bytes = Vec::with_capacity(s.len());
    let mut iter = s.bytes();
    while let Some(b) = iter.next() {
        if b == b'%' {
            let hex = [iter.next()?, iter.next()?];
            let hex = std::str::from_utf8(&hex).ok()?;
            bytes.push(u8::from_str_radix(hex, 16).ok()?);
        } else {
            bytes.push(b);
        }
    }
    String::from_utf8(bytes).ok()
}

fn initialize(params: Option<&Value>, id: Value, server_version: &str, is_mock: bool) -> Value {
    let requested = params
        .and_then(|p| p.get("protocolVersion"))
        .and_then(Value::as_str);
    // Version negotiation per spec: echo a supported requested version,
    // otherwise counter-offer our latest and let the client decide.
    let protocol_version = match requested {
        Some(v) if SUPPORTED_PROTOCOL_VERSIONS.contains(&v) => v,
        _ => LATEST_PROTOCOL_VERSION,
    };
    result_response(
        id,
        json!({
            "protocolVersion": protocol_version,
            // listChanged is deliberately not advertised on any capability:
            // the server never emits list-change notifications.
            "capabilities": { "tools": {}, "resources": {}, "prompts": {} },
            // NP-06: the very first thing a client learns should say whether
            // it is talking to a real editor. `instructions` is the one field
            // in `initialize` that many clients put into the model's context.
            "serverInfo": { "name": SERVER_NAME, "version": server_version, "mock": is_mock },
            "instructions": if is_mock {
                "This server is running WITHOUT --socket: it is not connected to a \
                 running Notepatra editor and every tool returns fabricated demo \
                 data. Do not present any of it as the user's real files."
            } else {
                "Connected to a running Notepatra editor. Tabs are addressed by \
                 `tab_id` (stable, from list_open_tabs) or `tab_index` \
                 (positional, changes when tabs close) — prefer tab_id for writes."
            }
        }),
    )
}

fn resource_not_found(id: Value, uri: &str) -> Value {
    json!({
        "jsonrpc": "2.0", "id": id,
        "error": {
            "code": RESOURCE_NOT_FOUND,
            "message": "Resource not found",
            "data": { "uri": uri }
        }
    })
}

/// A read that failed inside the editor: "not running" is an internal error,
/// anything else (bad index, unknown note) is a missing resource.
fn read_failure(id: Value, uri: &str, msg: String) -> Value {
    if msg == crate::transport::socket::NOT_RUNNING {
        error_response(id, INTERNAL_ERROR, &msg)
    } else {
        let mut resp = resource_not_found(id, uri);
        resp["error"]["data"]["detail"] = Value::String(msg);
        resp
    }
}

fn result_response(id: Value, result: Value) -> Value {
    json!({ "jsonrpc": "2.0", "id": id, "result": result })
}

fn error_response(id: Value, code: i64, message: &str) -> Value {
    json!({ "jsonrpc": "2.0", "id": id, "error": { "code": code, "message": message } })
}
