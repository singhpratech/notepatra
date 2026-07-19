// SPDX-License-Identifier: GPL-3.0-or-later
use notepatra_mcp::server::Server;
use notepatra_mcp::transport::{mock::MockEditor, socket::SocketEditor};

fn main() -> std::io::Result<()> {
    // Phase 3a subcommands (serve/pair/connect) are dispatched FIRST and only
    // when they are the first argument, so every existing invocation — no args,
    // `--socket`, anything else — reaches the unchanged stdio path below,
    // byte-for-byte identical to HEAD.
    if let Some(mode @ ("serve" | "pair" | "connect")) = std::env::args().nth(1).as_deref() {
        return run_remote_mode(mode);
    }

    // `--socket` targets the running editor over its dedicated MCP bridge
    // socket; default is the in-memory mock so any MCP client can exercise
    // the protocol without a running editor.
    let use_socket = std::env::args().any(|a| a == "--socket");
    let stdin = std::io::stdin().lock();
    let stdout = std::io::stdout().lock();
    if use_socket {
        Server::new(SocketEditor::new()).run(stdin, stdout)
    } else {
        Server::new(MockEditor::default()).run(stdin, stdout)
    }
}

#[cfg(feature = "remote")]
fn run_remote_mode(mode: &str) -> std::io::Result<()> {
    notepatra_mcp::remote::cli::run(mode)
}

#[cfg(not(feature = "remote"))]
fn run_remote_mode(_mode: &str) -> std::io::Result<()> {
    eprintln!(
        "notepatra-mcp: built without remote support; \
         rebuild with `cargo build --features remote`"
    );
    std::process::exit(2);
}
