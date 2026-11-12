#!/usr/bin/env bash
# linux-launch-smoke.sh — start a PACKAGED Notepatra for real (xcb platform on
# a private Xvfb, not offscreen) and fail if Qt or the dynamic loader could
# not load what the package ships.
#
#   bash scripts/linux-launch-smoke.sh <notepatra-binary | Notepatra-*.AppImage> [seconds=8]
#
# Why: v0.1.131 shipped macOS DMGs that died at startup because CI only ever
# ran build-tree binaries, and `--version` returns before QApplication exists,
# so it never loads a platform plugin. This is the Linux counterpart of
# scripts/macos-launch-smoke.sh: point it at the binary as a user would have
# it (extracted tarball, installed .deb, extracted .rpm, the .AppImage file).
#
# What it does:
#   1. Static check: `ldd` over the binary and every ELF shared object under
#      the package root (the binary's directory; for an AppImage, its
#      --appimage-extract tree). Fails on "not found", and on any library that
#      resolves outside both the package root and the system library dirs
#      (/lib*, /usr/lib*): on a CI runner the build tree still exists, and a
#      build-tree RUNPATH would otherwise hide a library the package forgot.
#   2. Launch: a private Xvfb (-displayfd picks a free display; the caller's
#      DISPLAY is never used), isolated HOME and XDG dirs, QT_QPA_PLATFORM=xcb,
#      QT_DEBUG_PLUGINS=1, the startup update check pre-disabled in the
#      isolated QSettings file. Runs SECONDS, then SIGTERM to the app's own
#      process group (SIGKILL after 3 s). An AppImage is started with
#      --appimage-extract-and-run (runners have no FUSE).
#
# Fails (exit 1) when:
#   a. ldd reports "not found", or a library resolves outside the package
#      root and the system library dirs;
#   b. the log contains "Could not load|find the Qt platform plugin",
#      "cannot open shared object file", "error while loading shared
#      libraries" or "Cannot load library" (QT_DEBUG_PLUGINS: a plugin Qt
#      tried and failed to dlopen);
#   c. the xcb platform plugin was never loaded, or was loaded from outside
#      the package root and the system library dirs;
#   d. the process exited on its own before the deadline (any status: an
#      early clean exit means no window stayed up, nothing was proven), or
#      died by a signal other than our own TERM/KILL;
#   e. the app's crash handler left recovery/.crash_flag in the isolated
#      config dir;
#   f. the process group survives SIGKILL.
# Exit 2: not Linux, bad arguments, the target is missing, or a tool this
# check needs (Xvfb, ldd, setsid) is missing. A checker that cannot run fails.
#
# What it cannot catch: a dependency that the runner has installed but a
# user's machine lacks (the tarballs and .rpm rely on system Qt). The .deb is
# therefore installed into a clean container by the CI step that calls this.
set -u

target="${1:-}"
secs="${2:-8}"
[ "$(uname -s)" = Linux ] || { echo "ERROR: Linux only"; exit 2; }
[ -n "$target" ] || { echo "ERROR: usage: $0 <binary|AppImage> [seconds]"; exit 2; }
case "$secs" in '' | *[!0-9]*) echo "ERROR: seconds must be an integer"; exit 2 ;; esac
[ -f "$target" ] || { echo "ERROR: $target does not exist"; exit 2; }
target=$(readlink -f "$target") || exit 2
[ -x "$target" ] || { echo "ERROR: $target is not executable"; exit 2; }
for tool in Xvfb ldd setsid; do
    command -v "$tool" >/dev/null 2>&1 || { echo "ERROR: $tool not found (install xvfb / libc-bin / util-linux)"; exit 2; }
done

work=$(mktemp -d) || exit 2
app_pid=""
xvfb_pid=""
cleanup() {
    [ -n "$app_pid" ] && kill -KILL -- "-$app_pid" 2>/dev/null
    [ -n "$xvfb_pid" ] && kill -TERM "$xvfb_pid" 2>/dev/null
    rm -rf "$work"
}
trap cleanup EXIT

failures=0
fail() { echo "::error::linux launch smoke: $*"; failures=$((failures + 1)); }

is_appimage=0
case "$target" in *.AppImage) is_appimage=1 ;; esac

# ── 1. static loader check ──────────────────────────────────────────────
if [ "$is_appimage" = 1 ]; then
    mkdir -p "$work/extract"
    if ! (cd "$work/extract" && "$target" --appimage-extract >/dev/null 2>&1); then
        echo "ERROR: $target --appimage-extract failed"
        exit 2
    fi
    root="$work/extract/squashfs-root"
    main_elf=""
    for cand in "$root/usr/bin/notepatra" "$root/AppRun"; do
        if [ -f "$cand" ] && [ "$(head -c 4 "$cand" | od -An -c | tr -d ' ')" = '177ELF' ]; then
            main_elf=$(readlink -f "$cand")
            break
        fi
    done
    [ -n "$main_elf" ] || { echo "ERROR: no ELF notepatra binary inside $target"; exit 2; }
else
    root=$(dirname "$target")
    main_elf="$target"
fi
root=$(readlink -f "$root")

is_system_path() {
    case "$1" in /lib/* | /lib64/* | /lib32/* | /usr/lib/* | /usr/lib64/* | /usr/lib32/* | /usr/libx32/*) return 0 ;; esac
    return 1
}
is_allowed_path() {
    local p
    p=$(readlink -m "$1")
    is_system_path "$p" && return 0
    case "$p" in "$root"/*) return 0 ;; esac
    return 1
}

elfs=("$main_elf")
while IFS= read -r -d '' f; do
    [ "$(readlink -f "$f")" = "$main_elf" ] && continue
    [ "$(head -c 4 "$f" | od -An -c | tr -d ' ')" = '177ELF' ] && elfs+=("$f")
done < <(find "$root" -type f -name '*.so*' -print0 2>/dev/null)

# Bundled libs are loaded into a process that already holds its siblings, so
# resolve them with the package's lib dirs on the path; the main binary gets
# no help (its RUNPATH must stand on its own).
bundled_dirs=$(for f in "${elfs[@]:1}"; do dirname "$f"; done | sort -u | paste -sd: -)
ldd_bad=0
for f in "${elfs[@]}"; do
    if [ "$f" = "$main_elf" ]; then
        out=$(env -u LD_LIBRARY_PATH -u LD_PRELOAD ldd "$f" 2>&1)
    else
        out=$(env -u LD_PRELOAD LD_LIBRARY_PATH="$bundled_dirs" ldd "$f" 2>&1)
    fi
    rc=$?
    if [ "$f" = "$main_elf" ] && [ "$rc" -ne 0 ]; then
        fail "ldd failed on the main binary $f: $out"
        ldd_bad=1
        continue
    fi
    if printf '%s\n' "$out" | grep -q 'not found'; then
        fail "unresolved libraries for ${f#"$root"/}:"
        printf '%s\n' "$out" | grep 'not found' | sed 's/^/    /'
        ldd_bad=1
    fi
    while IFS= read -r resolved; do
        [ -n "$resolved" ] || continue
        if ! is_allowed_path "$resolved"; then
            fail "${f#"$root"/} resolves $resolved outside the package ($root) and the system library dirs"
            ldd_bad=1
        fi
    done < <(printf '%s\n' "$out" | sed -n 's/^.*=> \(\/[^ ]*\) (0x.*$/\1/p')
done
[ "$ldd_bad" = 0 ] && echo "ldd: ${#elfs[@]} ELF file(s) under $root, all dependencies resolve inside the package or system lib dirs"

# ── 2. launch on a private Xvfb ─────────────────────────────────────────
smoke_home="$work/home"
log="$work/launch.log"
mkdir -p "$smoke_home" "$work/config/Notepatra" "$work/data" "$work/cache" "$work/state" "$work/tmp" "$work/run"
chmod 700 "$work/run"
# QSettings("Notepatra","Notepatra") on Linux = $XDG_CONFIG_HOME/Notepatra/Notepatra.conf
printf '[updates]\ncheckOnStartup=false\n' >"$work/config/Notepatra/Notepatra.conf"

Xvfb -displayfd 3 -screen 0 1280x800x24 -nolisten tcp 3>"$work/displayno" >"$work/xvfb.log" 2>&1 &
xvfb_pid=$!
n=0
while [ ! -s "$work/displayno" ] && [ "$n" -lt 100 ]; do
    kill -0 "$xvfb_pid" 2>/dev/null || break
    sleep 0.1
    n=$((n + 1))
done
display_no=$(tr -dc '0-9' <"$work/displayno" 2>/dev/null)
if [ -z "$display_no" ]; then
    echo "ERROR: private Xvfb did not start"
    cat "$work/xvfb.log"
    exit 2
fi

launch=("$target")
[ "$is_appimage" = 1 ] && launch+=(--appimage-extract-and-run)

echo "launch smoke: $target (${secs}s, xcb on private Xvfb :$display_no, QT_DEBUG_PLUGINS=1, HOME=$smoke_home)"
env -u QT_QPA_PLATFORM -u QT_PLUGIN_PATH -u QT_QPA_PLATFORM_PLUGIN_PATH \
    -u LD_LIBRARY_PATH -u LD_PRELOAD -u WAYLAND_DISPLAY -u DBUS_SESSION_BUS_ADDRESS -u SESSION_MANAGER \
    DISPLAY=":$display_no" QT_QPA_PLATFORM=xcb QT_DEBUG_PLUGINS=1 \
    HOME="$smoke_home" XDG_CONFIG_HOME="$work/config" XDG_DATA_HOME="$work/data" \
    XDG_CACHE_HOME="$work/cache" XDG_STATE_HOME="$work/state" XDG_RUNTIME_DIR="$work/run" \
    TMPDIR="$work/tmp" \
    setsid "${launch[@]}" </dev/null >"$log" 2>&1 &
app_pid=$! # setsid does not fork here (a background job is not a group leader): this is the app

exited_early=0
rc=0
i=0
while [ "$i" -lt "$secs" ]; do
    sleep 1
    i=$((i + 1))
    if ! kill -0 "$app_pid" 2>/dev/null; then
        exited_early=1
        wait "$app_pid"
        rc=$?
        break
    fi
done

survived=0
end_rc=""
if [ "$exited_early" = 0 ]; then
    kill -TERM -- "-$app_pid" 2>/dev/null || true
    j=0
    while [ "$j" -lt 3 ] && kill -0 "$app_pid" 2>/dev/null; do
        sleep 1
        j=$((j + 1))
    done
    if kill -0 "$app_pid" 2>/dev/null; then
        kill -KILL -- "-$app_pid" 2>/dev/null || true
        sleep 1
        kill -0 "$app_pid" 2>/dev/null && survived=1
    fi
    wait "$app_pid" 2>/dev/null
    end_rc=$?
fi
[ "$survived" = 1 ] || app_pid=""

echo "=== launch log (filtered: plugin loads, errors) ==="
grep -E 'loaded library|Cannot load library|Could not (load|find) the Qt platform plugin|no Qt platform plugin|cannot open shared object file|error while loading shared libraries|qt\.qpa|rror' "$log" | head -80 || echo "(no matching lines)"
echo "=== end (full log: $(wc -l <"$log" | tr -d ' ') lines) ==="

if grep -qE 'Could not (load|find) the Qt platform plugin|no Qt platform plugin could be initialized' "$log"; then
    fail "Qt platform plugin failed to load"
fi
# Bare-name probes ("Cannot load library Xcursor: ...") are Qt's optional
# system-lib lookups with a fallback; a plugin or package lib has a path.
load_errs=$(grep -E 'cannot open shared object file|error while loading shared libraries|Cannot load library' "$log" |
    grep -vE '^Cannot load library [^/ ]+: ')
if [ -n "$load_errs" ]; then
    fail "a library or plugin failed to load:"
    printf '%s\n' "$load_errs" | head -20
fi
xcb_line=$(grep -E 'loaded library ".*/libqxcb\.so"' "$log" | head -1)
if [ -z "$xcb_line" ]; then
    fail "the xcb platform plugin was never loaded (QT_DEBUG_PLUGINS printed no 'loaded library .../libqxcb.so')"
else
    xcb_path=${xcb_line#*loaded library \"}
    xcb_path=${xcb_path%%\"*}
    # extract-and-run unpacks under TMPDIR, i.e. inside $work
    case "$(readlink -m "$xcb_path")" in
        "$work"/tmp/*) echo "xcb loaded from the AppImage: ok ($xcb_path)" ;;
        *)
            if is_allowed_path "$xcb_path"; then
                echo "xcb loaded: ok ($xcb_path)"
            else
                fail "xcb plugin loaded from outside the package and system dirs: $xcb_path"
            fi
            ;;
    esac
fi
crash_flag="$work/config/notepatra/recovery/.crash_flag"
[ -e "$crash_flag" ] && fail "the app's crash handler fired (recovery/.crash_flag written)"
[ "$survived" = 1 ] && fail "process group $app_pid survived SIGKILL"

if [ "$exited_early" = 1 ]; then
    if [ "$rc" -gt 128 ]; then
        fail "process died by signal $((rc - 128)) after ${i}s"
    elif [ "$rc" -ne 0 ]; then
        fail "process exited on its own with status $rc after ${i}s"
    else
        fail "process exited on its own with status 0 after ${i}s — no window stayed up, nothing was verified"
    fi
elif [ -n "$end_rc" ] && [ "$end_rc" -gt 128 ] && [ "$end_rc" -ne 143 ] && [ "$end_rc" -ne 137 ]; then
    fail "process died by signal $((end_rc - 128)) while shutting down"
fi

if [ "$failures" -gt 0 ]; then
    echo "=== full launch log ==="
    cat "$log"
    exit 1
fi
echo "launch smoke OK: ran ${secs}s on xcb, no load errors, no crash"
