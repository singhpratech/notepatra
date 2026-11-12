#!/usr/bin/env bash
# macos-launch-smoke.sh — start a built Notepatra.app for real (cocoa platform,
# not offscreen) and fail if Qt or dyld could not load what the bundle ships.
#
#   bash scripts/macos-launch-smoke.sh <App.app> [seconds=8]
#
# Why: v0.1.131 shipped two DMGs that died at startup with
#   Could not load the Qt platform plugin "cocoa"
# and CI's only runtime check was `--version`, which returns before
# QApplication exists and so never loads a platform plugin.
#
# What it does: runs Contents/MacOS/<exe> directly (not `open`, so we own the
# PID and its stderr) with QT_DEBUG_PLUGINS=1 and an isolated HOME, lets it run
# SECONDS, then SIGTERM (SIGKILL after 3 s). macOS has no `timeout`, hence the
# background PID + sleep loop.
#
# Startup behaviour this relies on (src/main.cpp, src/singleinstance.cpp,
# src/mainwindow.cpp):
#   - the single-instance server name is a hash of QDir::homePath(), so the
#     isolated HOME gets its own server: no forwarding to another instance,
#     no exit-0-after-forward, no stale socket from a previous run;
#   - the fresh HOME has no session.json and no crash flag, so no restore and
#     no "closed unexpectedly" notice (notices are non-modal anyway);
#   - the silent update check fires 3 s after start. It is pre-disabled below
#     via `defaults write` (best-effort: QSettings uses CFPreferences, which
#     may not follow $HOME; if the write misses, the check runs silently, is
#     capped at 8 s by the app, and cannot open a dialog for a build whose
#     version is not older than the latest release).
#
# Fails (exit 1) when:
#   a. the log contains "Could not load the Qt platform plugin";
#   b. the log contains "Library not loaded" (dyld) or "Cannot load library"
#      (QT_DEBUG_PLUGINS: any plugin Qt tried and failed to dlopen);
#   c. the cocoa plugin was never loaded, or was loaded from OUTSIDE the
#      bundle (a missing/ignored qt.conf would silently fall back to
#      Homebrew's plugin dir on the runner and hide a broken bundle);
#   d. the process exited on its own before the deadline — non-zero, or zero
#      (an early clean exit means no window stayed up: nothing was proven);
#   e. the process survives SIGKILL.
# Exit 2: not macOS, or the bundle/executable is missing.
#
# The one tolerated case: GitHub's macOS runners run jobs inside a logged-in
# Aqua session, so a window can be created. If a runner ever lacks a
# WindowServer connection, AppKit aborts AFTER Qt has already loaded the cocoa
# plugin. That is accepted (with a ::warning) only when ALL of: the process
# exited early, cocoa loaded from inside the bundle, no load error from (a)/(b)
# appears, and the log carries a WindowServer-connection error signature.
# A plugin-load failure is never tolerated.
#
# What it cannot catch: an absolute /opt/homebrew reference still resolves on
# the runner (Homebrew is installed there). That class is caught statically by
# scripts/macos-bundle-closure.sh --check-only, which must run as well.
set -u

app="${1:-}"
secs="${2:-8}"
[ "$(uname -s)" = Darwin ] || { echo "ERROR: macOS only"; exit 2; }
[ -n "$app" ] && [ -d "$app/Contents/MacOS" ] || { echo "ERROR: usage: $0 <App.app> [seconds]"; exit 2; }
case "$secs" in '' | *[!0-9]*) echo "ERROR: seconds must be an integer"; exit 2 ;; esac

root=$(cd -P "$app" && pwd -P) || exit 2
exe_name=$(/usr/libexec/PlistBuddy -c 'Print :CFBundleExecutable' "$root/Contents/Info.plist") ||
    { echo "ERROR: cannot read CFBundleExecutable from $root/Contents/Info.plist"; exit 2; }
bin="$root/Contents/MacOS/$exe_name"
[ -x "$bin" ] || { echo "ERROR: $bin is not an executable"; exit 2; }

work=$(mktemp -d) || exit 2
trap 'rm -rf "$work"' EXIT
smoke_home="$work/home"
log="$work/launch.log"
mkdir -p "$smoke_home"

echo "launch smoke: $bin (${secs}s, QT_DEBUG_PLUGINS=1, HOME=$smoke_home)"
echo "session manager: $(launchctl managername 2>&1)  (Aqua = GUI session)"

# Best-effort, see header. Failure here is not a test failure.
if ! HOME="$smoke_home" defaults write com.notepatra.Notepatra updates.checkOnStartup -bool false; then
    echo "note: could not pre-disable the startup update check; it will run silently"
fi

env -u QT_QPA_PLATFORM -u DYLD_LIBRARY_PATH -u DYLD_FALLBACK_LIBRARY_PATH \
    -u DYLD_FRAMEWORK_PATH -u DYLD_INSERT_LIBRARIES \
    HOME="$smoke_home" QT_DEBUG_PLUGINS=1 \
    "$bin" </dev/null >"$log" 2>&1 &
pid=$!

exited_early=0
rc=0
i=0
while [ "$i" -lt "$secs" ]; do
    sleep 1
    i=$((i + 1))
    if ! kill -0 "$pid" 2>/dev/null; then # kill -0 on a dead PID prints "No such process"
        exited_early=1
        wait "$pid"
        rc=$?
        break
    fi
done

survived=0
if [ "$exited_early" = 0 ]; then
    kill -TERM "$pid" 2>/dev/null || true # it may exit between the check and the signal
    j=0
    while [ "$j" -lt 3 ] && kill -0 "$pid" 2>/dev/null; do
        sleep 1
        j=$((j + 1))
    done
    if kill -0 "$pid" 2>/dev/null; then
        kill -KILL "$pid" 2>/dev/null || true
        sleep 1
        kill -0 "$pid" 2>/dev/null && survived=1
    fi
    wait "$pid" 2>/dev/null || true # status is our own signal; not meaningful
fi

echo "=== launch log (filtered: plugin loads, errors) ==="
grep -E 'loaded library|Cannot load library|Library not loaded|Could not load the Qt platform plugin|qt\.qpa|WindowServer|CGS|_RegisterApplication|rror' "$log" | head -80 || echo "(no matching lines)"
echo "=== end (full log: $(wc -l <"$log" | tr -d ' ') lines) ==="

failures=0
tolerated=0
fail() { echo "::error::launch smoke: $*"; failures=$((failures + 1)); }

if grep -q 'Could not load the Qt platform plugin' "$log"; then
    fail "Qt platform plugin failed to load"
fi
if grep -qE 'Library not loaded|Cannot load library' "$log"; then
    fail "a library or plugin failed to load:"
    grep -E 'Library not loaded|Cannot load library' "$log" | head -20
fi
cocoa_line=$(grep -E 'loaded library ".*/libqcocoa\.dylib"' "$log" | head -1)
if [ -z "$cocoa_line" ]; then
    fail "the cocoa platform plugin was never loaded (QT_DEBUG_PLUGINS printed no 'loaded library .../libqcocoa.dylib')"
else
    # Compare canonical directories, not strings: Qt may print a path with
    # "/../" segments depending on how qt.conf's prefix was resolved.
    cocoa_path=${cocoa_line#*loaded library \"}
    cocoa_path=${cocoa_path%%\"*}
    cocoa_dir=""
    [ -d "${cocoa_path%/*}" ] && cocoa_dir=$(cd -P "${cocoa_path%/*}" && pwd -P)
    if [ "$cocoa_dir" = "$root/Contents/PlugIns/platforms" ]; then
        echo "cocoa loaded from the bundle: ok ($cocoa_path)"
    else
        fail "cocoa plugin loaded from outside the bundle: $cocoa_line"
    fi
fi
[ "$survived" = 1 ] && fail "process $pid survived SIGKILL"

if [ "$exited_early" = 1 ]; then
    if [ "$failures" = 0 ] && [ "$rc" -ne 0 ] &&
        grep -qE 'WindowServer|_RegisterApplication|CGSDefaultConnection|kCGErrorFailure' "$log"; then
        echo "::warning::launch smoke: no WindowServer connection on this runner (exit $rc after ${i}s); cocoa itself loaded from the bundle — tolerated"
        tolerated=1
    elif [ "$rc" -ne 0 ]; then
        fail "process exited on its own with status $rc after ${i}s"
    else
        fail "process exited on its own with status 0 after ${i}s — no window stayed up, nothing was verified"
    fi
fi

if [ "$failures" -gt 0 ]; then
    echo "=== full launch log ==="
    cat "$log"
    exit 1
fi
if [ "$tolerated" = 1 ]; then
    echo "launch smoke OK (degraded): cocoa loaded from the bundle, no load errors; no window could be created on this runner"
else
    echo "launch smoke OK: ran ${secs}s, cocoa loaded from the bundle, no load errors"
fi
