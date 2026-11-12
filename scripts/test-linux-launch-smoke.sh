#!/usr/bin/env bash
# test-linux-launch-smoke.sh — self-test for scripts/linux-launch-smoke.sh.
#
# Builds tiny fake "apps" with cc (no Qt needed) that reproduce each failure
# shape the smoke test must catch, plus clean controls that must pass, and
# asserts the script's exit code for each (0 OK, 1 failed, 2 cannot run).
# Needs cc and Xvfb; a self-test that cannot run fails (exit 1).
set -u

here=$(cd "$(dirname "$0")" && pwd)
smoke="$here/linux-launch-smoke.sh"
[ "$(uname -s)" = Linux ] || { echo "self-test: Linux only"; exit 1; }
command -v cc >/dev/null 2>&1 || { echo "self-test: FAIL — cc not found"; exit 1; }
command -v Xvfb >/dev/null 2>&1 || { echo "self-test: FAIL — Xvfb not found (apt install xvfb)"; exit 1; }

fx=$(mktemp -d) || exit 1
trap 'rm -rf "$fx"' EXIT
pass=0
failn=0
ok() { echo "  PASS $1"; pass=$((pass + 1)); }
bad() { echo "  FAIL $1"; failn=$((failn + 1)); }

cat >"$fx/app.c" <<'EOF'
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#ifdef WITH_DEP
int fakedep(void);
#endif
static void xcb(void) {
    fprintf(stderr, "QFactoryLoader: loaded library \"/usr/lib/x86_64-linux-gnu/qt5/plugins/platforms/libqxcb.so\"\n");
    fflush(stderr);
}
int main(void) {
#ifdef WITH_DEP
    if (fakedep() != 42) return 3;
#endif
    const char *d = getenv("DISPLAY"), *p = getenv("QT_QPA_PLATFORM");
    if (!d || d[0] != ':' || !p || strcmp(p, "xcb") != 0) {
        fprintf(stderr, "qt.qpa.plugin: Could not load the Qt platform plugin \"xcb\" in \"\" even though it was found.\n");
        abort();
    }
    char sock[64];
    snprintf(sock, sizeof sock, "/tmp/.X11-unix/X%s", d + 1);
    struct stat st;
    if (stat(sock, &st) != 0) { fprintf(stderr, "no X server socket %s\n", sock); return 4; }
#if defined(MODE_PLUGIN_ERR)
    fprintf(stderr, "qt.qpa.plugin: Could not load the Qt platform plugin \"xcb\" in \"\" even though it was found.\n");
    abort();
#elif defined(MODE_EXIT0)
    xcb();
    return 0;
#elif defined(MODE_SEGV)
    xcb();
    sleep(1);
    raise(SIGSEGV);
#elif defined(MODE_CRASHFLAG)
    xcb();
    char path[4096];
    snprintf(path, sizeof path, "%s/notepatra", getenv("XDG_CONFIG_HOME"));
    mkdir(path, 0700);
    strcat(path, "/recovery");
    mkdir(path, 0700);
    strcat(path, "/.crash_flag");
    FILE *f = fopen(path, "w");
    if (f) fclose(f);
#elif defined(MODE_NOXCB)
    fprintf(stderr, "running without a platform plugin line\n");
#elif defined(MODE_PLUGIN_DLOPEN)
    xcb();
    fprintf(stderr, "Cannot load library /x/plugins/imageformats/libqfoo.so: (libfoo.so.1: cannot open shared object file: No such file or directory)\n");
#elif defined(MODE_BARE_PROBE)
    xcb();
    fprintf(stderr, "Cannot load library Xcursor: (Xcursor: cannot open shared object file: No such file or directory)\n");
#else
    xcb();
#endif
#ifdef MODE_IGNORE_TERM
    signal(SIGTERM, SIG_IGN);
#endif
    for (;;) pause();
}
EOF
printf 'int fakedep(void) { return 42; }\n' >"$fx/dep.c"

build() { # build <dir> <name> [extra cc args...]
    local dir=$1 name=$2
    shift 2
    mkdir -p "$dir"
    cc -O0 -o "$dir/$name" "$fx/app.c" "$@" || { echo "self-test: FAIL — cc could not build $name"; exit 1; }
}
expect() { # expect <label> <want-exit> <target> <log-pattern>
    local label=$1 want=$2 target=$3 pat=$4 got
    bash "$smoke" "$target" 3 >"$fx/out.log" 2>&1
    got=$?
    if [ "$got" = "$want" ] && grep -qE -- "$pat" "$fx/out.log"; then
        ok "$label (exit $got)"
    else
        bad "$label: wanted exit $want + /$pat/, got $got"
        sed 's/^/      /' "$fx/out.log" | tail -25
    fi
}

echo "self-test: linux-launch-smoke.sh"

build "$fx/good" notepatra
expect "clean app stays up on xcb" 0 "$fx/good/notepatra" "launch smoke OK"

build "$fx/ignterm" notepatra -DMODE_IGNORE_TERM
expect "app ignoring SIGTERM is killed, still OK" 0 "$fx/ignterm/notepatra" "launch smoke OK"

mkdir -p "$fx/bundled/lib"
cc -shared -fPIC -o "$fx/bundled/lib/libfakedep.so" "$fx/dep.c" || exit 1
build "$fx/bundled" notepatra -DWITH_DEP -L"$fx/bundled/lib" -lfakedep '-Wl,-rpath,$ORIGIN/lib'
expect "bundled lib resolved via \$ORIGIN/lib" 0 "$fx/bundled/notepatra" "2 ELF file"

mkdir -p "$fx/missing/lib"
cp "$fx/bundled/lib/libfakedep.so" "$fx/missing/lib/"
build "$fx/missing" notepatra -DWITH_DEP -L"$fx/missing/lib" -lfakedep '-Wl,-rpath,$ORIGIN/lib'
rm "$fx/missing/lib/libfakedep.so"
expect "package forgot a library (ldd not found + loader error)" 1 "$fx/missing/notepatra" "libfakedep.so => not found"

mkdir -p "$fx/outside/lib" "$fx/foreign"
cp "$fx/bundled/lib/libfakedep.so" "$fx/outside/lib/"
build "$fx/foreign" notepatra -DWITH_DEP -L"$fx/outside/lib" -lfakedep "-Wl,-rpath,$fx/outside/lib"
expect "library resolved from a build-tree path outside the package" 1 "$fx/foreign/notepatra" "outside the package"

build "$fx/plugin" notepatra -DMODE_PLUGIN_ERR
expect "Could not load the Qt platform plugin" 1 "$fx/plugin/notepatra" "Qt platform plugin failed to load"
build "$fx/dlopen" notepatra -DMODE_PLUGIN_DLOPEN
expect "a plugin failed to dlopen (Cannot load library)" 1 "$fx/dlopen/notepatra" "a library or plugin failed to load"
build "$fx/exit0" notepatra -DMODE_EXIT0
expect "early clean exit proves nothing" 1 "$fx/exit0/notepatra" "exited on its own with status 0"
build "$fx/segv" notepatra -DMODE_SEGV
expect "crash (SIGSEGV) during startup" 1 "$fx/segv/notepatra" "died by signal 11"
build "$fx/crashflag" notepatra -DMODE_CRASHFLAG
expect "crash handler left recovery/.crash_flag" 1 "$fx/crashflag/notepatra" "crash handler fired"
build "$fx/probe" notepatra -DMODE_BARE_PROBE
expect "Qt's optional bare-name probe (Xcursor) is not a failure" 0 "$fx/probe/notepatra" "launch smoke OK"
build "$fx/noxcb" notepatra -DMODE_NOXCB
expect "xcb plugin never loaded" 1 "$fx/noxcb/notepatra" "xcb platform plugin was never loaded"

expect "missing target cannot run" 2 "$fx/does-not-exist" "does not exist"
mkdir -p "$fx/noxvfb-bin"
for t in bash env uname readlink dirname find od head tr sed grep paste sort mktemp ldd setsid sleep wc cat rm mkdir chmod; do
    p=$(command -v "$t") && ln -s "$p" "$fx/noxvfb-bin/$t"
done
PATH="$fx/noxvfb-bin" bash "$smoke" "$fx/good/notepatra" 3 >"$fx/out.log" 2>&1
got=$?
if [ "$got" = 2 ] && grep -q 'Xvfb not found' "$fx/out.log"; then
    ok "no Xvfb: exits 2 (cannot run), never falls back to another display"
else
    bad "no Xvfb: wanted exit 2, got $got"
    cat "$fx/out.log"
fi

echo "self-test: $pass passed, $failn failed"
[ "$failn" = 0 ]
