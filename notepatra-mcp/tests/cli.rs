// SPDX-License-Identifier: GPL-3.0-or-later
//
// Command-line surface of the `notepatra-mcp` binary.
//
// This is a stdio server: with no arguments it blocks reading stdin, which is
// correct when an MCP client launched it and looks exactly like a crash when a
// human ran it in a terminal. Before v0.1.125 EVERY unrecognised argument fell
// into that loop — `--version` printed nothing and hung, `--sokcet` silently
// started a MOCK server against fabricated data, and a mistyped subcommand like
// `sevre` hung too. The rule these tests pin: an argument we do not understand
// is a loud exit, never a hang and never a silent fallback.
//
// Every child gets a null stdin, so a regression cannot wedge the test suite —
// it would hit EOF and exit 0, which is still a failure against `code == 2`.

use std::process::{Command, Stdio};

fn run(args: &[&str]) -> (i32, String, String) {
    let out = Command::new(env!("CARGO_BIN_EXE_notepatra-mcp"))
        .args(args)
        .stdin(Stdio::null())
        .output()
        .expect("failed to spawn notepatra-mcp");
    (
        out.status.code().unwrap_or(-1),
        String::from_utf8_lossy(&out.stdout).into_owned(),
        String::from_utf8_lossy(&out.stderr).into_owned(),
    )
}

#[test]
fn help_and_version_print_and_exit_zero() {
    for flag in ["-h", "--help"] {
        let (code, stdout, _) = run(&[flag]);
        assert_eq!(code, 0, "{flag} should exit 0");
        assert!(
            stdout.contains("USAGE"),
            "{flag} printed no usage block: {stdout}"
        );
    }
    for flag in ["-V", "--version"] {
        let (code, stdout, _) = run(&[flag]);
        assert_eq!(code, 0, "{flag} should exit 0");
        assert!(
            stdout.contains(env!("CARGO_PKG_VERSION")),
            "{flag} printed no version: {stdout}"
        );
    }
}

#[test]
fn a_mistyped_subcommand_exits_two_instead_of_hanging() {
    // USAGE advertises bare-word subcommands, so this is a realistic typo —
    // and it is the half that survived the first fix, which rejected only
    // flag-shaped arguments.
    let (code, _, stderr) = run(&["sevre"]);
    assert_eq!(code, 2, "unknown subcommand must exit 2, got {code}");
    assert!(
        stderr.contains("unknown subcommand") && stderr.contains("sevre"),
        "the error must name what was typed: {stderr}"
    );
    assert!(
        stderr.contains("serve"),
        "the error must list the real subcommands: {stderr}"
    );
}

#[test]
fn a_mistyped_flag_exits_two_rather_than_starting_a_mock() {
    // The dangerous half: `--sokcet` used to start the in-memory MOCK, so the
    // client got fabricated tabs that looked like the user's real editor.
    let (code, _, stderr) = run(&["--sokcet"]);
    assert_eq!(code, 2, "unknown flag must exit 2, got {code}");
    assert!(
        stderr.contains("--sokcet"),
        "the error must name the offending flag: {stderr}"
    );
}

#[test]
fn the_real_subcommands_are_not_swallowed_by_the_typo_guard() {
    // Vacuity guard for the two tests above: if the guard rejected everything,
    // they would pass while the binary was broken. Built without the `remote`
    // feature these exit 2 as well, but with a DIFFERENT message — so assert on
    // the message, not the code.
    for mode in ["serve", "pair", "connect"] {
        let (_, _, stderr) = run(&[mode]);
        assert!(
            !stderr.contains("unknown subcommand"),
            "{mode} is a real subcommand and must not hit the typo guard: {stderr}"
        );
    }
}
