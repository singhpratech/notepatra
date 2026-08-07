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
// Nothing here may wait on a child unbounded. Some arguments are SUPPOSED to
// block: built `--features remote`, `serve` binds a port and never returns, and
// CI runs the suite twice — once default, once with that feature. The first cut
// of this file called `.output()` on `serve`, which waits for an EOF that never
// comes; it passed locally on the default build and wedged four CI runners for
// 25 minutes each. Every spawn below is time-capped and killed.

use std::io::Read;
use std::process::{Command, Stdio};
use std::time::{Duration, Instant};

struct Outcome {
    /// `false` means the child was still running when the budget expired and we
    /// killed it. For a subcommand that legitimately blocks, that is a pass.
    exited: bool,
    code: Option<i32>,
    stdout: String,
    stderr: String,
}

fn run_bounded(args: &[&str], budget: Duration) -> Outcome {
    let mut child = Command::new(env!("CARGO_BIN_EXE_notepatra-mcp"))
        .args(args)
        .stdin(Stdio::null())
        .stdout(Stdio::piped())
        .stderr(Stdio::piped())
        .spawn()
        .expect("failed to spawn notepatra-mcp");

    // Drain both pipes on their own threads. A child that fills one of them
    // would block before exiting, which would turn the bounded wait below into
    // a lie about why it did not finish.
    let mut out_pipe = child.stdout.take().expect("stdout was piped");
    let mut err_pipe = child.stderr.take().expect("stderr was piped");
    let out_reader = std::thread::spawn(move || {
        let mut s = String::new();
        let _ = out_pipe.read_to_string(&mut s);
        s
    });
    let err_reader = std::thread::spawn(move || {
        let mut s = String::new();
        let _ = err_pipe.read_to_string(&mut s);
        s
    });

    let deadline = Instant::now() + budget;
    let status = loop {
        match child.try_wait().expect("try_wait on the child failed") {
            Some(st) => break Some(st),
            None if Instant::now() >= deadline => {
                let _ = child.kill();
                let _ = child.wait();
                break None;
            }
            None => std::thread::sleep(Duration::from_millis(25)),
        }
    };

    Outcome {
        exited: status.is_some(),
        code: status.and_then(|s| s.code()),
        stdout: out_reader.join().unwrap_or_default(),
        stderr: err_reader.join().unwrap_or_default(),
    }
}

/// For arguments that MUST exit. The budget is generous because it should never
/// be reached; when it is, the failure names a hang instead of timing out the job.
fn run(args: &[&str]) -> Outcome {
    let o = run_bounded(args, Duration::from_secs(30));
    assert!(
        o.exited,
        "`{}` hung for 30s — it must exit, not fall into the stdio loop",
        args.join(" ")
    );
    o
}

#[test]
fn help_and_version_print_and_exit_zero() {
    for flag in ["-h", "--help"] {
        let o = run(&[flag]);
        assert_eq!(o.code, Some(0), "{flag} should exit 0");
        assert!(
            o.stdout.contains("USAGE"),
            "{flag} printed no usage block: {}",
            o.stdout
        );
    }
    for flag in ["-V", "--version"] {
        let o = run(&[flag]);
        assert_eq!(o.code, Some(0), "{flag} should exit 0");
        assert!(
            o.stdout.contains(env!("CARGO_PKG_VERSION")),
            "{flag} printed no version: {}",
            o.stdout
        );
    }
}

#[test]
fn a_mistyped_subcommand_exits_two_instead_of_hanging() {
    // USAGE advertises bare-word subcommands, so this is a realistic typo —
    // and it is the half that survived the first fix, which rejected only
    // flag-shaped arguments.
    let o = run(&["sevre"]);
    assert_eq!(o.code, Some(2), "unknown subcommand must exit 2");
    assert!(
        o.stderr.contains("unknown subcommand") && o.stderr.contains("sevre"),
        "the error must name what was typed: {}",
        o.stderr
    );
    assert!(
        o.stderr.contains("serve"),
        "the error must list the real subcommands: {}",
        o.stderr
    );
}

#[test]
fn a_mistyped_flag_exits_two_rather_than_starting_a_mock() {
    // The dangerous half: `--sokcet` used to start the in-memory MOCK, so the
    // client got fabricated tabs that looked like the user's real editor.
    let o = run(&["--sokcet"]);
    assert_eq!(o.code, Some(2), "unknown flag must exit 2");
    assert!(
        o.stderr.contains("--sokcet"),
        "the error must name the offending flag: {}",
        o.stderr
    );
}

#[test]
fn the_real_subcommands_are_not_swallowed_by_the_typo_guard() {
    // Vacuity guard for the two tests above: if the guard rejected everything,
    // they would pass while the binary was broken.
    //
    // Two legitimate outcomes, and which one you get depends on the build. A
    // std-only build exits 2 with "built without remote support"; a `--features
    // remote` build accepts the subcommand and blocks. Still running when the
    // budget expires IS the pass — the typo guard exits before it can bind
    // anything, so a live process proves the argument was understood.
    for mode in ["serve", "pair", "connect"] {
        let o = run_bounded(&[mode], Duration::from_secs(3));
        assert!(
            !o.stderr.contains("unknown subcommand"),
            "{mode} is a real subcommand and must not hit the typo guard: {}",
            o.stderr
        );
        if o.exited {
            assert_eq!(
                o.code,
                Some(2),
                "{mode} exited {:?} — the only clean exit here is the \
                 built-without-remote refusal: {}",
                o.code,
                o.stderr
            );
        }
    }
}
