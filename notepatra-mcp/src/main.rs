// SPDX-License-Identifier: GPL-3.0-or-later
use notepatra_mcp::server::Server;
use notepatra_mcp::transport::{mock::MockEditor, socket::SocketEditor};

fn main() -> std::io::Result<()> {
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
