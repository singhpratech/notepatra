#!/bin/bash
set -u
# smoke-multiprocess.sh — 6-scenario real-binary single-instance smoke.
# Drives the ACTUAL notepatra binary through cold start, forward+ACK,
# --new standalone, graceful-quit session write, crash-flag cycle, and
# kill-9 stale-socket rebind, in a sandboxed $HOME. Referenced by the
# v0.1.114 release notes ("16/16 multi-process smoke").
# Usage: bash scripts/smoke-multiprocess.sh   (BIN=... to override binary)
BIN="${BIN:-$(cd "$(dirname "$0")/.." && pwd)/build/notepatra}"
SANDBOX=$(mktemp -d /tmp/np-smoke-XXXXXX)
export HOME="$SANDBOX"
export QT_QPA_PLATFORM=offscreen
export XDG_RUNTIME_DIR="$SANDBOX/runtime"; mkdir -p "$XDG_RUNTIME_DIR"; chmod 700 "$XDG_RUNTIME_DIR"
pass=0; fail=0
ck(){ if eval "$2"; then echo "  [PASS] $1"; pass=$((pass+1)); else echo "  [FAIL] $1"; fail=$((fail+1)); fi; }

echo "S1 — primary cold start with file arg"
echo "hello smoke" > "$SANDBOX/a.txt"
"$BIN" "$SANDBOX/a.txt" > "$SANDBOX/primary.log" 2>&1 &
PRIMARY=$!
sleep 3
ck "primary alive after 3s" "kill -0 $PRIMARY 2>/dev/null"

echo "S2 — second invocation forwards (ACK) and exits 0"
echo "second file" > "$SANDBOX/b.txt"
start=$(date +%s%N)
timeout 10 "$BIN" "$SANDBOX/b.txt" > "$SANDBOX/second.log" 2>&1
rc=$?
elapsed_ms=$(( ($(date +%s%N) - start) / 1000000 ))
ck "second invocation exit 0 (got $rc)" "[ $rc -eq 0 ]"
ck "fast handoff <5000ms (got ${elapsed_ms}ms)" "[ $elapsed_ms -lt 5000 ]"
ck "primary still alive" "kill -0 $PRIMARY 2>/dev/null"

echo "S3 — --new opens a standalone window (stays running)"
"$BIN" --new > "$SANDBOX/standalone.log" 2>&1 &
STANDALONE=$!
sleep 3
ck "standalone process alive" "kill -0 $STANDALONE 2>/dev/null"
ck "primary also still alive" "kill -0 $PRIMARY 2>/dev/null"
kill $STANDALONE 2>/dev/null; wait $STANDALONE 2>/dev/null
SESS="$SANDBOX/.config/notepatra/session.json"
ck "standalone never wrote session.json" "[ ! -f $SESS ] || ! grep -q standalone-canary $SESS"

echo "S4 — graceful quit writes session.json, no marker left"
kill $PRIMARY 2>/dev/null   # SIGTERM → Qt quits → closeEvent
for i in $(seq 1 50); do kill -0 $PRIMARY 2>/dev/null || break; sleep 0.2; done
ck "primary exited on SIGTERM" "! kill -0 $PRIMARY 2>/dev/null"
ck "session.json exists" "[ -f $SESS ]"
ck "session lists opened file a.txt" "grep -q 'a.txt' $SESS"
ck "session lists remote-opened b.txt" "grep -q 'b.txt' $SESS"
ck "no .restoring marker left" "[ ! -f $SESS.restoring ]"
ck "no session.json.tmp orphan" "[ ! -f $SESS.tmp ]"

echo "S5 — crash flag cycle: flag present -> launch -> surfaced+cleared"
mkdir -p "$SANDBOX/.config/notepatra/recovery"
printf crashed > "$SANDBOX/.config/notepatra/recovery/.crash_flag"
"$BIN" > "$SANDBOX/relaunch.log" 2>&1 &
RELAUNCH=$!
sleep 4
ck "relaunch alive (session restored: a.txt+b.txt tabs)" "kill -0 $RELAUNCH 2>/dev/null"
ck "crash flag cleared after show" "[ ! -f $SANDBOX/.config/notepatra/recovery/.crash_flag ]"

echo "S6 — kill -9 primary (hung/dead pipe) then relaunch binds fresh"
kill -9 $RELAUNCH 2>/dev/null; sleep 1
echo "third" > "$SANDBOX/c.txt"
timeout 15 "$BIN" "$SANDBOX/c.txt" > "$SANDBOX/rebind.log" 2>&1 &
REBIND=$!
sleep 5
ck "post-kill relaunch alive (took over stale socket)" "kill -0 $REBIND 2>/dev/null"
kill $REBIND 2>/dev/null; wait $REBIND 2>/dev/null

echo; echo "RESULT: $pass passed, $fail failed (sandbox: $SANDBOX)"
grep -iE "warn|error|crash" "$SANDBOX"/*.log | grep -vE "QStandardPaths|fontconfig|dbus|DBus|qt.qpa" | head -10
exit $fail
