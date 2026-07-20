// SPDX-License-Identifier: GPL-3.0-or-later
//! Minimal std-only HTTP/1.1 — just enough for the private gateway protocol.
//! Server side parses a request; client side (used by `connect`/`pair`) issues
//! one POST and reads one response. No chunked encoding, no keep-alive pooling
//! on the client (one connection per request, `Connection: close`); the server
//! honors keep-alive so a `connect` front-end can reuse its socket.

use std::io::{self, BufRead, BufReader, Write};
use std::net::{SocketAddr, TcpStream, ToSocketAddrs};
use std::time::Duration;

// ── Client deadlines ───────────────────────────────────────────────────────
//
// Every socket op below carries one. A blocking read with no timeout is not a
// slow test, it is a HANG: the integration suite that drives this client runs
// on the Windows CI job, where an unbounded `read_line` waiting on a wedged
// gateway handler burns the whole job budget and reports nothing. A deadline
// turns that into a fast, attributable red.

/// TCP connect ceiling. The gateway is loopback-only, so a healthy connect is
/// sub-millisecond; this only bounds a listener that accepted the SYN and then
/// went away.
pub const CONNECT_TIMEOUT: Duration = Duration::from_secs(10);

/// Response ceiling. Deliberately GENEROUS: a remote tool call can legitimately
/// sit behind the local approval card a human has not answered yet. It is set
/// above the transport's own 130 s approval window on purpose, so the inner
/// timeout fires first and the agent gets the specific "approval timed out"
/// message rather than this generic one.
pub const READ_TIMEOUT: Duration = Duration::from_secs(180);

/// Request-send ceiling. Unblocking a write needs the peer to drain; a gateway
/// that stops reading must fail here rather than park the caller forever.
pub const WRITE_TIMEOUT: Duration = Duration::from_secs(30);

/// Hard ceiling on an inbound request body. The gateway's JSON-RPC requests are
/// small (tool RESPONSES, which can be large, are written OUT, never read here),
/// so this bounds a pre-auth attacker `Content-Length` from forcing a giant
/// allocation (memory DoS). An over-limit request is refused before any body
/// bytes are allocated; the router answers 413.
pub const MAX_REQUEST_BODY: usize = 16 * 1024 * 1024;

/// A parsed inbound HTTP request. Header names are lowercased.
pub struct HttpRequest {
    pub method: String,
    pub path: String,
    pub headers: Vec<(String, String)>,
    pub body: Vec<u8>,
}

impl HttpRequest {
    /// Reads one request. `Ok(None)` on a clean connection close before any
    /// bytes (keep-alive loop end). A malformed request line still parses to a
    /// request with an empty method/path — the router answers 400.
    pub fn read<R: BufRead>(r: &mut R) -> io::Result<Option<HttpRequest>> {
        let mut request_line = String::new();
        if r.read_line(&mut request_line)? == 0 {
            return Ok(None); // EOF at message boundary
        }
        let request_line = request_line.trim_end_matches(['\r', '\n']);
        if request_line.is_empty() {
            return Ok(None);
        }
        let mut parts = request_line.split_whitespace();
        let method = parts.next().unwrap_or_default().to_string();
        let path = parts.next().unwrap_or_default().to_string();

        let mut headers = Vec::new();
        loop {
            let mut line = String::new();
            if r.read_line(&mut line)? == 0 {
                break; // EOF mid-headers
            }
            let line = line.trim_end_matches(['\r', '\n']);
            if line.is_empty() {
                break; // end of headers
            }
            if let Some((k, v)) = line.split_once(':') {
                headers.push((k.trim().to_ascii_lowercase(), v.trim().to_string()));
            }
        }

        let len = header_of(&headers, "content-length")
            .and_then(|v| v.parse::<usize>().ok())
            .unwrap_or(0);
        if len > MAX_REQUEST_BODY {
            // Refuse BEFORE allocating: a bogus Content-Length must not OOM us.
            return Err(io::Error::new(
                io::ErrorKind::InvalidData,
                "request body exceeds limit",
            ));
        }
        let mut body = vec![0u8; len];
        if len > 0 {
            r.read_exact(&mut body)?;
        }
        Ok(Some(HttpRequest {
            method,
            path,
            headers,
            body,
        }))
    }

    pub fn header(&self, name: &str) -> Option<&str> {
        header_of(&self.headers, name)
    }

    /// The bearer token from `Authorization: Bearer <token>`, if present.
    pub fn bearer(&self) -> Option<&str> {
        self.header("authorization")
            .and_then(|v| {
                v.strip_prefix("Bearer ")
                    .or_else(|| v.strip_prefix("bearer "))
            })
            .map(str::trim)
    }

    /// True unless the client asked to close (HTTP/1.1 default is keep-alive).
    pub fn keep_alive(&self) -> bool {
        !self
            .header("connection")
            .map(|c| c.eq_ignore_ascii_case("close"))
            .unwrap_or(false)
    }
}

fn header_of<'a>(headers: &'a [(String, String)], name: &str) -> Option<&'a str> {
    headers
        .iter()
        .find(|(k, _)| k == name)
        .map(|(_, v)| v.as_str())
}

/// Writes a JSON response. `body` is sent verbatim with a `Content-Length`;
/// `204` carries an empty body.
pub fn write_response<W: Write>(
    w: &mut W,
    status: u16,
    reason: &str,
    body: &[u8],
    keep_alive: bool,
) -> io::Result<()> {
    let conn = if keep_alive { "keep-alive" } else { "close" };
    write!(
        w,
        "HTTP/1.1 {status} {reason}\r\n\
         Content-Type: application/json\r\n\
         Content-Length: {}\r\n\
         Connection: {conn}\r\n\r\n",
        body.len()
    )?;
    w.write_all(body)?;
    w.flush()
}

/// A parsed client-side response.
#[derive(Debug)]
pub struct HttpResponse {
    pub status: u16,
    pub body: Vec<u8>,
}

/// Issues one `POST {path}` to `base_url` (e.g. `http://127.0.0.1:8080`),
/// optionally bearer-authenticated, and reads the whole response. Only plain
/// `http://` is supported — `https://` is rejected (TLS arrives in Phase 3b).
pub fn post_json(
    base_url: &str,
    path: &str,
    token: Option<&str>,
    body: &[u8],
) -> io::Result<HttpResponse> {
    if base_url.starts_with("https://") {
        return Err(io::Error::new(
            io::ErrorKind::Unsupported,
            "TLS not supported yet (Phase 3b); use http:// over an SSH port-forward",
        ));
    }
    let hostport = base_url
        .strip_prefix("http://")
        .ok_or_else(|| io::Error::new(io::ErrorKind::InvalidInput, "url must start with http://"))?
        .trim_end_matches('/');

    let mut stream = connect_with_deadline(hostport)?;
    let mut head = format!(
        "POST {path} HTTP/1.1\r\n\
         Host: {hostport}\r\n\
         Content-Type: application/json\r\n\
         Content-Length: {}\r\n\
         Connection: close\r\n",
        body.len()
    );
    if let Some(t) = token {
        head.push_str(&format!("Authorization: Bearer {t}\r\n"));
    }
    head.push_str("\r\n");
    stream
        .write_all(head.as_bytes())
        .and_then(|()| stream.write_all(body))
        .and_then(|()| stream.flush())
        .map_err(|e| explain_timeout(e, "sending the request to"))?;

    read_response(&mut BufReader::new(stream))
        .map_err(|e| explain_timeout(e, "waiting for a response from"))
}

/// Rewrites a bare socket-timeout errno into something an operator can act on.
///
/// std surfaces an elapsed `SO_RCVTIMEO`/`SO_SNDTIMEO` as `WouldBlock`, which
/// prints as "Resource temporarily unavailable (os error 11)" — technically
/// true and completely useless in a CI log. A deadline that fires must say WHAT
/// timed out and for how long, or it just trades a silent hang for a cryptic
/// red.
fn explain_timeout(e: io::Error, what: &str) -> io::Error {
    match e.kind() {
        io::ErrorKind::WouldBlock | io::ErrorKind::TimedOut => io::Error::new(
            io::ErrorKind::TimedOut,
            format!(
                "timed out {what} the gateway: it accepted the connection but \
                 never completed the exchange (a wedged handler). Deadlines: \
                 connect {CONNECT_TIMEOUT:?}, write {WRITE_TIMEOUT:?}, read \
                 {READ_TIMEOUT:?}."
            ),
        ),
        _ => e,
    }
}

/// Connects to `hostport` with a bounded connect, then arms read AND write
/// deadlines before a single byte is exchanged.
///
/// `TcpStream::connect` is NOT used: it takes a `ToSocketAddrs` and applies no
/// deadline. `connect_timeout` takes a resolved `SocketAddr`, so resolution
/// happens here and each candidate address is tried in turn (a host may resolve
/// to both v4 and v6; only the last error is worth reporting).
///
/// HONEST RESIDUAL: `to_socket_addrs` performs DNS, and std exposes no timeout
/// for it. For this client that is not a live risk — the gateway is
/// loopback-only, so the input is a literal `127.0.0.1:port` that resolves
/// without touching the network. Bounding DNS would need a resolver thread and
/// buys nothing here.
fn connect_with_deadline(hostport: &str) -> io::Result<TcpStream> {
    let addrs: Vec<SocketAddr> = hostport.to_socket_addrs()?.collect();
    let mut last_err = None;
    for addr in &addrs {
        match TcpStream::connect_timeout(addr, CONNECT_TIMEOUT) {
            Ok(s) => {
                s.set_read_timeout(Some(READ_TIMEOUT))?;
                s.set_write_timeout(Some(WRITE_TIMEOUT))?;
                return Ok(s);
            }
            Err(e) => last_err = Some(e),
        }
    }
    Err(last_err.unwrap_or_else(|| {
        io::Error::new(
            io::ErrorKind::NotFound,
            format!("no address resolved for {hostport}"),
        )
    }))
}

fn read_response<R: BufRead>(r: &mut R) -> io::Result<HttpResponse> {
    let mut status_line = String::new();
    r.read_line(&mut status_line)?;
    let status = status_line
        .split_whitespace()
        .nth(1)
        .and_then(|s| s.parse::<u16>().ok())
        .ok_or_else(|| io::Error::new(io::ErrorKind::InvalidData, "malformed status line"))?;

    let mut content_length: Option<usize> = None;
    loop {
        let mut line = String::new();
        if r.read_line(&mut line)? == 0 {
            break;
        }
        let line = line.trim_end_matches(['\r', '\n']);
        if line.is_empty() {
            break;
        }
        if let Some((k, v)) = line.split_once(':') {
            if k.trim().eq_ignore_ascii_case("content-length") {
                content_length = v.trim().parse().ok();
            }
        }
    }

    let body = match content_length {
        Some(n) => {
            let mut b = vec![0u8; n];
            r.read_exact(&mut b)?;
            b
        }
        None => {
            // No length: server used Connection: close, read to EOF.
            let mut b = Vec::new();
            r.read_to_end(&mut b)?;
            b
        }
    };
    Ok(HttpResponse { status, body })
}

#[cfg(test)]
mod tests {
    use super::*;
    use std::io::Cursor;

    #[test]
    fn parses_post_with_body() {
        let raw = "POST /rpc HTTP/1.1\r\n\
                   Host: 127.0.0.1\r\n\
                   Content-Type: application/json\r\n\
                   Content-Length: 13\r\n\
                   Authorization: Bearer abc123\r\n\r\n\
                   {\"ping\":true}";
        let mut c = Cursor::new(raw.as_bytes());
        let req = HttpRequest::read(&mut c).unwrap().unwrap();
        assert_eq!(req.method, "POST");
        assert_eq!(req.path, "/rpc");
        assert_eq!(req.body, b"{\"ping\":true}");
        assert_eq!(req.bearer(), Some("abc123"));
        assert!(req.keep_alive());
    }

    #[test]
    fn headers_are_case_insensitive() {
        let raw = "POST /x HTTP/1.1\r\nCONTENT-LENGTH: 2\r\nConnection: close\r\n\r\nhi";
        let mut c = Cursor::new(raw.as_bytes());
        let req = HttpRequest::read(&mut c).unwrap().unwrap();
        assert_eq!(req.body, b"hi");
        assert!(!req.keep_alive());
    }

    #[test]
    fn garbage_yields_empty_method_path() {
        let mut c = Cursor::new(b"garbage line\r\n\r\n".to_vec());
        let req = HttpRequest::read(&mut c).unwrap().unwrap();
        assert_eq!(req.method, "garbage");
        assert_eq!(req.path, "line");
        // A request line with no recognizable path is what the router 400s on;
        // here the second token is a path-shaped word, so cover the true-empty
        // case too:
        let mut c2 = Cursor::new(b"OOPS\r\n\r\n".to_vec());
        let req2 = HttpRequest::read(&mut c2).unwrap().unwrap();
        assert_eq!(req2.path, "");
    }

    #[test]
    fn rejects_oversized_content_length() {
        // A giant Content-Length is refused before the body is even allocated —
        // note the request carries no body bytes at all here.
        let raw = format!(
            "POST /rpc HTTP/1.1\r\nContent-Length: {}\r\n\r\n",
            MAX_REQUEST_BODY + 1
        );
        let mut c = Cursor::new(raw.into_bytes());
        // HttpRequest is not Debug, so match rather than unwrap_err().
        match HttpRequest::read(&mut c) {
            Err(e) => assert_eq!(e.kind(), io::ErrorKind::InvalidData),
            Ok(_) => panic!("oversized Content-Length must be refused"),
        }
    }

    #[test]
    fn clean_eof_is_none() {
        let mut c = Cursor::new(b"".to_vec());
        assert!(HttpRequest::read(&mut c).unwrap().is_none());
    }

    #[test]
    fn rejects_https() {
        let e = post_json("https://example", "/rpc", None, b"{}").unwrap_err();
        assert_eq!(e.kind(), io::ErrorKind::Unsupported);
    }
}
