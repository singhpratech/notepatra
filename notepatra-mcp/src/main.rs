// SPDX-License-Identifier: GPL-3.0-or-later
use notepatra_mcp::server::Server;
use notepatra_mcp::transport::{mock::MockEditor, socket::SocketEditor};

const USAGE: &str = "\
notepatra-mcp — Model Context Protocol server for the Notepatra editor

USAGE:
    notepatra-mcp [--socket]
    notepatra-mcp <serve|pair|connect>          (requires the `remote` feature)

OPTIONS:
    --socket        Drive the RUNNING editor over its local MCP bridge. Without
                    this the server answers from an in-memory MOCK whose data is
                    fabricated — useful for exercising the protocol, never for
                    real work.
    -h, --help      Print this help and exit.
    -V, --version   Print the version and exit.

This is a stdio server: with no flags it speaks JSON-RPC on stdin/stdout and
will appear to hang if you run it by hand. That is expected — it is meant to be
launched by an MCP client, not a terminal.

Docs: https://notepatra.org/mcp.html";

fn main() -> std::io::Result<()> {
    // Phase 3a subcommands (serve/pair/connect) are dispatched FIRST and only
    // when they are the first argument, so every existing invocation — no args,
    // `--socket`, anything else — reaches the unchanged stdio path below.
    if let Some(mode @ ("serve" | "pair" | "connect")) = std::env::args().nth(1).as_deref() {
        return run_remote_mode(mode);
    }

    // A MISTYPED subcommand must fail loudly too.
    //
    // Rejecting only bad flags left half the hang in place: USAGE advertises
    // bare-word subcommands, so `notepatra-mcp sevre` fell straight through to
    // the stdio loop and blocked on stdin forever — the same "looks crashed"
    // symptom, reached by the other spelling mistake.
    if let Some(first) = std::env::args().nth(1) {
        if !first.starts_with('-') && !matches!(first.as_str(), "serve" | "pair" | "connect") {
            eprintln!("notepatra-mcp: unknown subcommand '{first}'");
            eprintln!("Expected one of: serve, pair, connect");
            eprintln!("Try 'notepatra-mcp --help' for usage.");
            std::process::exit(2);
        }
    }

    // Discovery flags, and rejection of anything else that LOOKS like a flag.
    //
    // Before v0.1.125 none of this existed: every unrecognised argument fell
    // through to the stdio loop below, which blocks reading stdin. So
    // `--version` printed nothing and hung, and a typo like `--sokcet` silently
    // started a MOCK server instead of failing. Both read as a crashed program.
    // An unknown flag must be a loud error, never a hang.
    for arg in std::env::args().skip(1) {
        match arg.as_str() {
            "-h" | "--help" => {
                println!("{USAGE}");
                return Ok(());
            }
            "-V" | "--version" => {
                println!("notepatra-mcp {}", env!("CARGO_PKG_VERSION"));
                return Ok(());
            }
            "--socket" => {}
            // Bare words are left alone: a client may pass a path or an
            // extra positional we do not own. Only flag-shaped typos are
            // fatal, because those are ours to get wrong.
            other if other.starts_with('-') => {
                eprintln!("notepatra-mcp: unrecognised option '{other}'");
                eprintln!("Try 'notepatra-mcp --help' for usage.");
                std::process::exit(2);
            }
            _ => {}
        }
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
