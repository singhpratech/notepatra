// SPDX-License-Identifier: GPL-3.0-or-later
//! Shared hang containment for the integration suites.
//!
//! WHY THIS EXISTS: a hung CI job is strictly worse than a failing one. A red
//! test names its culprit in seconds; a hang burns the full job budget
//! (~80 minutes on the Windows runner) and reports NOTHING. Every suite that
//! drives a real transport — named pipes (`pipe_bridge`), unix sockets
//! (`socket_bridge`), loopback TCP (`remote_gateway`) — can block on a peer
//! that never answers, so all three arm the same two-layer net:
//!
//!   1. [`finish`] — a BOUNDED wait for a helper thread. Never `join()` bare:
//!      `join` is unbounded, and a server closure parked in `read_line` turns
//!      it into a permanent hang.
//!   2. [`Watchdog`] — the backstop for the case no bounded join can catch: a
//!      regression that blocks the TEST thread itself (a write that never
//!      returns, a socket read with no deadline). It aborts the binary so CI
//!      reports a failure instead of hanging.
//!
//! Living in `tests/common/` (a directory module, not a `tests/*.rs` file) it
//! is compiled into each suite that says `mod common;` and is NOT itself
//! auto-discovered as a test target.
#![allow(dead_code)] // each suite uses a subset; the unused half must not warn

use std::io::Write;
use std::path::PathBuf;
use std::sync::atomic::{AtomicBool, Ordering};
use std::sync::mpsc::{Receiver, RecvTimeoutError};
use std::sync::Arc;
use std::thread::JoinHandle;
use std::time::{Duration, Instant};

/// How long a helper thread may take to finish after the client is done.
/// Healthy runs are sub-second; the deliberate 3 s server sleep in
/// `pipe_bridge::response_timeout_enforced` is the slowest legitimate case.
pub const JOIN_BUDGET: Duration = Duration::from_secs(15);

/// Backstop deadline for a test that blocks its OWN thread. Must stay
/// comfortably above [`JOIN_BUDGET`] so the bounded join reports first — the
/// join produces an attributable panic, the watchdog only an abort.
pub const WATCHDOG_SECS: u64 = 30;

/// `<temp>/np-<suite>-tracker.log`. CI prints every `np-*-tracker.log` in an
/// `if: always()` step.
pub fn tracker_path(suite: &str) -> PathBuf {
    std::env::temp_dir().join(format!("np-{suite}-tracker.log"))
}

/// Append one line to this suite's tracker file, flushed and closed per line.
///
/// WHY A FILE: when this binary aborts, libtest's captured stdout/stderr is
/// DISCARDED — that is exactly how a previous CI incident lost the name of the
/// hanging test and cost multiple 80-minute cycles. A file survives `abort()`
/// and survives capture. The same localizer pattern rescued the C++
/// offscreen-Windows teardown crash. An abort therefore leaves "ENTER x" +
/// "TIMEOUT x" with no "LEAVE x", naming the culprit in one log read.
/// (A `finish` timeout unwinds normally, so it DOES write "LEAVE"; that path
/// needs no tracker, since libtest attributes the panic to the test itself.)
/// ONE `write_all` of a pre-formatted line, never `writeln!`. `File` is
/// unbuffered, so `writeln!(f, "{event} {label}")` issues a separate syscall
/// per format fragment; under the default parallel harness those interleave and
/// the tracker becomes unreadable garbage ("ENTERENTERENTER bridge_error..."),
/// destroying the one artifact that survives an abort. A single append-mode
/// write is atomic, so lines from concurrent tests stay whole.
fn track(suite: &str, event: &str, label: &str) {
    let line = format!("{event} {label}\n");
    if let Ok(mut f) = std::fs::OpenOptions::new()
        .create(true)
        .append(true)
        .open(tracker_path(suite))
    {
        let _ = f.write_all(line.as_bytes());
        let _ = f.flush();
    }
}

/// Fails the whole test binary FAST if a test overruns, instead of letting it
/// hang. This is the LAST line of defence, not the first: waits that CAN be
/// bounded are bounded at their own call site by [`finish`] or by a socket
/// timeout. What only the watchdog catches is a block on the test thread.
///
/// `abort()` rather than `panic!` on purpose: the panic would land on the
/// watchdog's own thread, where libtest cannot attribute it to the test, and
/// the blocked thread would keep the binary alive regardless. Because abort
/// throws away captured output, the name goes to the tracker FILE too.
pub struct Watchdog {
    done: Arc<AtomicBool>,
    suite: &'static str,
    label: &'static str,
}

impl Watchdog {
    /// Arms the watchdog for `label` and records "ENTER label".
    pub fn new(suite: &'static str, label: &'static str) -> Self {
        Self::with_secs(suite, label, WATCHDOG_SECS)
    }

    pub fn with_secs(suite: &'static str, label: &'static str, secs: u64) -> Self {
        track(suite, "ENTER", label);
        let done = Arc::new(AtomicBool::new(false));
        let flag = done.clone();
        std::thread::spawn(move || {
            let deadline = Instant::now() + Duration::from_secs(secs);
            while Instant::now() < deadline {
                if flag.load(Ordering::Relaxed) {
                    return;
                }
                std::thread::sleep(Duration::from_millis(50));
            }
            track(suite, "TIMEOUT", label);
            eprintln!(
                "{suite}: {label} exceeded {secs}s — a transport call is BLOCKED \
                 (no peer attached, or a peer that never answered). Aborting so \
                 CI reports a failure rather than hanging."
            );
            std::process::abort();
        });
        Self { done, suite, label }
    }
}

impl Drop for Watchdog {
    fn drop(&mut self) {
        self.done.store(true, Ordering::Relaxed);
        track(self.suite, "LEAVE", self.label);
    }
}

/// A running fake bridge: its thread plus the channel it signals on completion.
pub struct Bridge {
    handle: JoinHandle<()>,
    done: Receiver<()>,
}

impl Bridge {
    /// `done` must be signalled as the thread's LAST act (see [`finish`]).
    pub fn new(handle: JoinHandle<()>, done: Receiver<()>) -> Self {
        Self { handle, done }
    }
}

/// Waits for a bridge thread with a DEADLINE, then surfaces its panics.
///
/// No bare `join()` may remain in these suites. `join()` is unbounded, and a
/// server closure blocked in `read_line` (waiting for a client teardown that
/// never comes) turns it into a permanent hang — and a hang is not a red, it is
/// a full-budget CI burn that reports nothing. The completion channel is
/// signalled as the thread's LAST act, so once `recv_timeout` returns the
/// `join` is instantaneous and exists only to re-raise assertion failures.
pub fn finish(bridge: Bridge, label: &str) {
    match bridge.done.recv_timeout(JOIN_BUDGET) {
        // Sent on the way out, or the sender was dropped by an unwinding
        // panic — either way the thread is finished, so join to surface it.
        Ok(()) | Err(RecvTimeoutError::Disconnected) => {}
        Err(RecvTimeoutError::Timeout) => panic!(
            "{label}: the bridge thread did not finish within {JOIN_BUDGET:?} — \
             it is blocked (almost certainly in read_line, waiting for a client \
             EOF that never arrived)"
        ),
    }
    bridge.handle.join().expect("bridge thread panicked");
}
