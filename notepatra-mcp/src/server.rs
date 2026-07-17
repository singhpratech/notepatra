// SPDX-License-Identifier: GPL-3.0-or-later
use std::io::{self, BufRead, Write};

use serde_json::{json, Map, Value};

use crate::prompts;
use crate::tools::{self, CallOutcome};
use crate::transport::{EditorTransport, TabSelector};

// Latest MCP spec revision known when this scaffold was written
// (modelcontextprotocol.io). Assumption noted per scaffold brief: if a newer
// revision exists, bump this list — negotiation logic is already generic.
pub const LATEST_PROTOCOL_VERSION: &str = "2025-06-18";
pub const SUPPORTED_PROTOCOL_VERSIONS: [&str; 3] = ["2025-06-18", "2025-03-26", "2024-11-05"];

pub const SERVER_NAME: &str = "notepatra-mcp";
pub const SERVER_VERSION: &str = "0.1.0";

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
}

impl<T: EditorTransport> Server<T> {
    pub fn new(transport: T) -> Self {
        Self { transport }
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
            "initialize" => initialize(params, id),
            "ping" => result_response(id, json!({})),
            "tools/list" => result_response(id, json!({ "tools": tools::definitions() })),
            "tools/call" => self.tools_call(params, id),
            "resources/list" => self.resources_list(id),
            "resources/read" => self.resources_read(params, id),
            "prompts/list" => result_response(id, json!({ "prompts": prompts::definitions() })),
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
                for n in notes {
                    if let (Some(file), Some(title)) = (
                        n.get("file").and_then(Value::as_str),
                        n.get("title").and_then(Value::as_str),
                    ) {
                        resources.push(json!({
                            "uri": format!("{NOTE_URI_PREFIX}{file}"),
                            "name": title,
                            "mimeType": "text/plain",
                        }));
                    }
                }
            }
        }
        result_response(id, json!({ "resources": resources }))
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
            match self.transport.read_tab(TabSelector::Index(index)) {
                Ok(content) => content.text,
                Err(e) => return read_failure(id, &uri, e.0),
            }
        } else if let Some(file) = uri.strip_prefix(NOTE_URI_PREFIX) {
            match self.transport.read_note(file) {
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
        match tools::call(&mut self.transport, name, args) {
            CallOutcome::Ok(text) => result_response(
                id,
                json!({ "content": [{ "type": "text", "text": text }], "isError": false }),
            ),
            CallOutcome::ToolError(text) => result_response(
                id,
                json!({ "content": [{ "type": "text", "text": text }], "isError": true }),
            ),
            CallOutcome::InvalidParams(msg) => {
                error_response(id, INVALID_PARAMS, &format!("Invalid params: {msg}"))
            }
            CallOutcome::UnknownTool(name) => {
                error_response(id, INVALID_PARAMS, &format!("Unknown tool: {name}"))
            }
        }
    }
}

fn initialize(params: Option<&Value>, id: Value) -> Value {
    let requested = params
        .and_then(|p| p.get("protocolVersion"))
        .and_then(Value::as_str);
    // Version negotiation per spec: echo a supported requested version,
    // otherwise counter-offer our latest and let the client decide.
    let version = match requested {
        Some(v) if SUPPORTED_PROTOCOL_VERSIONS.contains(&v) => v,
        _ => LATEST_PROTOCOL_VERSION,
    };
    result_response(
        id,
        json!({
            "protocolVersion": version,
            // listChanged is deliberately not advertised on any capability:
            // the server never emits list-change notifications.
            "capabilities": { "tools": {}, "resources": {}, "prompts": {} },
            "serverInfo": { "name": SERVER_NAME, "version": SERVER_VERSION }
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
