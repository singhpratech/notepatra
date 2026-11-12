#!/usr/bin/env bash
# test-macos-bundle-closure.sh — offline regression lock for
# scripts/macos-bundle-closure.sh. Runs on Linux (and on macOS CI) with no
# Xcode tools: `otool`, `install_name_tool`, `codesign` and `brew` are PATH
# shims over a fake .app tree whose "Mach-O" files are 4 real magic bytes
# followed by text lines (ID / DEP / WEAK / RPATH). The otool shim renders
# them in real `otool -l` layout, LC_LOAD_DYLINKER included as a decoy; the
# install_name_tool shim edits them and, like the real tool, is a SILENT
# no-op when `-change OLD` does not match.
#
# What it proves:
#   1. the pure classifier (np_classify_dep) on fixture strings: system paths,
#      frameworks, @rpath, @loader_path, @executable_path (incl. a nested
#      helper .app), Homebrew opt + Cellar paths, foreign paths, and @rpath
#      names that only "resolve" by escaping the bundle via symlink or "..";
#   2. the three v0.1.131-class bugs — @rpath/libbrotlicommon.1.dylib,
#      @rpath/libsharpyuv.0.dylib (x3), libssl -> Cellar libcrypto — plus a
#      missing Homebrew libpng, a missing @executable_path/../Frameworks leaf
#      and a Homebrew-style @loader_path/../lib/<leaf>, are reported UNRESOLVED before the fix and resolved after it,
#      with every modified file re-signed and a second fix run a no-op;
#   3. a clean bundle passes; a main executable without the Frameworks rpath
#      fails; an unfixable dep fails; a parser that sees no load commands
#      fails; and an environment without otool FAILS instead of passing.
#
# CLOSURE_SH=/path/to/script overrides the script under test.
set -u
cd "$(dirname "$0")/.."
CLOSURE_SH="${CLOSURE_SH:-$PWD/scripts/macos-bundle-closure.sh}"
case "$CLOSURE_SH" in /*) ;; *) CLOSURE_SH="$PWD/$CLOSURE_SH" ;; esac
[ -f "$CLOSURE_SH" ] || { echo "FAIL: $CLOSURE_SH missing"; exit 1; }

pass=0
fail=0
ok() { echo "  PASS  $1"; pass=$((pass + 1)); }
bad() { echo "  FAIL  $1"; fail=$((fail + 1)); }

WORK=$(mktemp -d)
trap 'rm -rf "$WORK"' EXIT
WORK=$(cd -P "$WORK" && pwd -P)
STUB="$WORK/stub"
NOOTOOL="$WORK/stub-no-otool"
BREW="$WORK/homebrew"
LOG="$WORK/tool-calls.log"
mkdir -p "$STUB" "$NOOTOOL" "$BREW"
: >"$LOG"
export NP_STUB_LOG="$LOG" NP_FAKE_BREW="$BREW"

# ── PATH shims ────────────────────────────────────────────────────────────
cat >"$STUB/otool" <<'SH'
#!/usr/bin/env bash
[ "${1:-}" = -l ] && [ $# -eq 2 ] || { echo "otool stub: only 'otool -l FILE'" >&2; exit 64; }
f=$2
[ -f "$f" ] || { echo "error: $f: No such file" >&2; exit 1; }
magic=$(od -An -tx1 -N4 "$f" | tr -d ' \n')
[ "$magic" = cffaedfe ] || { echo "$f: is not an object file"; exit 1; }
echo "$f:"
n=0
while IFS=' ' read -r kind val; do
    case "$kind" in
        ID) printf 'Load command %d\n          cmd LC_ID_DYLIB\n      cmdsize 56\n         name %s (offset 24)\n   time stamp 1 Thu Jan  1 00:00:01 1970\n      current version 1.0.0\ncompatibility version 1.0.0\n' "$n" "$val" ;;
        DEP) printf 'Load command %d\n          cmd LC_LOAD_DYLIB\n      cmdsize 56\n         name %s (offset 24)\n   time stamp 2 Thu Jan  1 00:00:02 1970\n      current version 1.0.0\ncompatibility version 1.0.0\n' "$n" "$val" ;;
        WEAK) printf 'Load command %d\n          cmd LC_LOAD_WEAK_DYLIB\n      cmdsize 56\n         name %s (offset 24)\n   time stamp 2 Thu Jan  1 00:00:02 1970\n' "$n" "$val" ;;
        RPATH) printf 'Load command %d\n          cmd LC_RPATH\n      cmdsize 40\n         path %s (offset 12)\n' "$n" "$val" ;;
        DYLD) printf 'Load command %d\n          cmd LC_LOAD_DYLINKER\n      cmdsize 32\n         name /usr/lib/dyld (offset 12)\n' "$n" ;;
        *) continue ;;
    esac
    n=$((n + 1))
done < <(tail -n +2 "$f")
SH
cat >"$STUB/install_name_tool" <<'SH'
#!/usr/bin/env bash
echo "install_name_tool $*" >>"$NP_STUB_LOG"
case "${1:-}" in
    -id) [ $# -eq 3 ] || exit 64; new=$2; f=$3; old="" ; mode=id ;;
    -change) [ $# -eq 4 ] || exit 64; old=$2; new=$3; f=$4; mode=change ;;
    *) echo "install_name_tool stub: unsupported $*" >&2; exit 64 ;;
esac
[ -f "$f" ] || { echo "error: $f not found" >&2; exit 1; }
[ -w "$f" ] || { echo "error: can't open $f for writing (Permission denied)" >&2; exit 1; }
tmp="$f.int$$"
if [ "$mode" = id ]; then
    { head -n 1 "$f"; echo "ID $new"; tail -n +2 "$f" | grep -v '^ID '; } >"$tmp"
else
    # Exact match only; a mismatch is a silent no-op, exactly like the real tool.
    LC_ALL=C OLD="$old" NEW="$new" awk 'NR > 1 && ($0 == "DEP " ENVIRON["OLD"]) { print "DEP " ENVIRON["NEW"]; next }
         NR > 1 && ($0 == "WEAK " ENVIRON["OLD"]) { print "WEAK " ENVIRON["NEW"]; next }
         { print }' "$f" >"$tmp"
fi
cat "$tmp" >"$f" && rm -f "$tmp"
SH
cat >"$STUB/codesign" <<'SH'
#!/usr/bin/env bash
echo "codesign $*" >>"$NP_STUB_LOG"
SH
cat >"$STUB/brew" <<'SH'
#!/usr/bin/env bash
[ "${1:-}" = --prefix ] && [ $# -eq 1 ] || { echo "brew stub: only --prefix" >&2; exit 64; }
echo "$NP_FAKE_BREW"
SH
chmod +x "$STUB"/*
cp "$STUB/install_name_tool" "$STUB/codesign" "$STUB/brew" "$NOOTOOL/"
PATH_STUB="$STUB:/usr/bin:/bin"
PATH_NOOTOOL="$NOOTOOL:/usr/bin:/bin"

# ── fixture builders ──────────────────────────────────────────────────────
# mk_macho PATH LINE... — fake Mach-O: 64-bit LE magic, then "KIND value" lines.
mk_macho() {
    local f="$1"
    shift
    mkdir -p "${f%/*}"
    {
        printf '\317\372\355\376\n'
        for l in "$@"; do printf '%s\n' "$l"; done
    } >"$f"
}

# Fake Homebrew prefix: real files in Cellar, reached through opt/ and lib/
# symlinks the way Homebrew lays them out.
mk_brew() {
    mk_macho "$BREW/Cellar/brotli/1.1.0/lib/libbrotlicommon.1.1.0.dylib" \
        "ID /opt/homebrew/opt/brotli/lib/libbrotlicommon.1.dylib" "DEP /usr/lib/libSystem.B.dylib" \
        "X marker cellar-brotli-1.1.0"
    mkdir -p "$BREW/opt/brotli/lib"
    ln -s ../../../Cellar/brotli/1.1.0/lib/libbrotlicommon.1.1.0.dylib "$BREW/opt/brotli/lib/libbrotlicommon.1.dylib"
    mk_macho "$BREW/Cellar/webp/1.5.0/lib/libsharpyuv.0.1.1.dylib" \
        "ID /opt/homebrew/opt/webp/lib/libsharpyuv.0.dylib" "DEP /usr/lib/libSystem.B.dylib"
    mkdir -p "$BREW/lib"
    ln -s ../Cellar/webp/1.5.0/lib/libsharpyuv.0.1.1.dylib "$BREW/lib/libsharpyuv.0.dylib"
    mk_macho "$BREW/Cellar/libpng/1.6.44/lib/libpng16.16.dylib" \
        "ID /opt/homebrew/opt/libpng/lib/libpng16.16.dylib" "DEP /usr/lib/libz.1.dylib" "DEP /usr/lib/libSystem.B.dylib"
    mkdir -p "$BREW/opt/libpng/lib"
    ln -s ../../../Cellar/libpng/1.6.44/lib/libpng16.16.dylib "$BREW/opt/libpng/lib/libpng16.16.dylib"
    mk_macho "$BREW/Cellar/zstd/1.5.6/lib/libzstd.1.5.6.dylib" \
        "ID /opt/homebrew/opt/zstd/lib/libzstd.1.dylib" "DEP /usr/lib/libSystem.B.dylib"
    mkdir -p "$BREW/opt/zstd/lib"
    ln -s ../../../Cellar/zstd/1.5.6/lib/libzstd.1.5.6.dylib "$BREW/opt/zstd/lib/libzstd.1.dylib"
    mk_macho "$BREW/Cellar/pcre2/10.44/lib/libpcre2-16.0.dylib" \
        "ID /opt/homebrew/opt/pcre2/lib/libpcre2-16.0.dylib" "DEP /usr/lib/libSystem.B.dylib"
    mkdir -p "$BREW/opt/pcre2/lib"
    ln -s ../../../Cellar/pcre2/10.44/lib/libpcre2-16.0.dylib "$BREW/opt/pcre2/lib/libpcre2-16.0.dylib"
}

# mk_bundle DIR clean|broken [no-fw-rpath]
mk_bundle() {
    local app="$1" kind="$2" opt="${3:-}" C="$1/Contents" FW="$1/Contents/Frameworks"
    local fwrpath="RPATH @executable_path/../Frameworks"
    [ "$opt" = no-fw-rpath ] && fwrpath="RPATH @executable_path/../NotFrameworks"
    mkdir -p "$C/MacOS" "$C/Resources" "$FW"
    cat >"$C/Info.plist" <<'PL'
<?xml version="1.0" encoding="UTF-8"?>
<plist version="1.0"><dict>
	<key>CFBundleExecutable</key>
	<string>Notepatra</string>
</dict></plist>
PL
    printf '[Paths]\nPlugins = PlugIns\n' >"$C/Resources/qt.conf"
    mk_macho "$C/MacOS/Notepatra" "DYLD" "$fwrpath" "RPATH @executable_path" \
        "RPATH /Users/runner/work/notepatra/notepatra/vendor/duckdb" \
        "DEP @rpath/QtCore.framework/Versions/5/QtCore" "DEP @rpath/libqscintilla2_qt5.15.dylib" \
        "DEP @rpath/libduckdb.dylib" "DEP /usr/lib/libc++.1.dylib" \
        "DEP /System/Library/Frameworks/Cocoa.framework/Versions/A/Cocoa"
    mk_macho "$C/MacOS/libduckdb.dylib" "ID @rpath/libduckdb.dylib" "DEP /usr/lib/libSystem.B.dylib"
    mk_macho "$FW/QtCore.framework/Versions/5/QtCore" "ID @rpath/QtCore.framework/Versions/5/QtCore" \
        "DEP /usr/lib/libSystem.B.dylib"
    ln -s 5 "$FW/QtCore.framework/Versions/Current"
    ln -s Versions/Current/QtCore "$FW/QtCore.framework/QtCore"
    mk_macho "$FW/libqscintilla2_qt5.15.dylib" "ID @rpath/libqscintilla2_qt5.15.dylib" \
        "DEP @rpath/QtGui.framework/Versions/5/QtGui" "DEP /usr/lib/libSystem.B.dylib"
    mk_macho "$FW/libcrypto.3.dylib" "ID @rpath/libcrypto.3.dylib" "DEP /usr/lib/libSystem.B.dylib"
    mk_macho "$FW/QtWebEngineCore.framework/Versions/5/Helpers/QtWebEngineProcess.app/Contents/MacOS/QtWebEngineProcess" \
        "DYLD" "DEP @executable_path/../../../../../../../QtCore.framework/Versions/5/QtCore" \
        "DEP /usr/lib/libSystem.B.dylib"
    mk_macho "$C/PlugIns/imageformats/libqwebp.dylib" "DEP @rpath/libwebp.7.dylib" \
        "DEP @rpath/libwebpdemux.2.dylib" "DEP @rpath/libwebpmux.3.dylib" \
        "DEP @rpath/QtGui.framework/Versions/5/QtGui" "DEP /usr/lib/libSystem.B.dylib"
    mk_macho "$C/PlugIns/platforms/libqcocoa.dylib" "DEP @rpath/QtGui.framework/Versions/5/QtGui" \
        "DEP @rpath/libfreetype.6.dylib" "DEP /System/Library/Frameworks/AppKit.framework/Versions/C/AppKit" \
        "DEP /usr/lib/libSystem.B.dylib"
    if [ "$kind" = clean ]; then
        mk_macho "$FW/QtGui.framework/Versions/5/QtGui" "ID @rpath/QtGui.framework/Versions/5/QtGui" \
            "DEP @rpath/QtCore.framework/Versions/5/QtCore" \
            "DEP @executable_path/../Frameworks/libpcre2-16.0.dylib" "DEP /usr/lib/libSystem.B.dylib"
        mk_macho "$FW/libpcre2-16.0.dylib" "ID @rpath/libpcre2-16.0.dylib" "DEP /usr/lib/libSystem.B.dylib"
        mk_macho "$FW/libfreetype.6.dylib" "ID @rpath/libfreetype.6.dylib" \
            "DEP @loader_path/libbrotlidec.1.dylib" "DEP @rpath/libpng16.16.dylib" "DEP /usr/lib/libz.1.dylib"
        mk_macho "$FW/libpng16.16.dylib" "ID @rpath/libpng16.16.dylib" "DEP /usr/lib/libSystem.B.dylib"
        mk_macho "$FW/libbrotlidec.1.dylib" "ID @rpath/libbrotlidec.1.dylib" "DEP @rpath/libbrotlicommon.1.dylib" \
            "DEP /usr/lib/libSystem.B.dylib"
        mk_macho "$FW/libbrotlicommon.1.dylib" "ID @rpath/libbrotlicommon.1.dylib" "DEP /usr/lib/libSystem.B.dylib"
        mk_macho "$FW/libsharpyuv.0.dylib" "ID @rpath/libsharpyuv.0.dylib" "DEP /usr/lib/libSystem.B.dylib"
        mk_macho "$FW/libssl.3.dylib" "ID @rpath/libssl.3.dylib" "DEP @rpath/libcrypto.3.dylib" \
            "DEP /usr/lib/libSystem.B.dylib"
    else
        # The shipped v0.1.131 shapes.
        mk_macho "$FW/QtGui.framework/Versions/5/QtGui" "ID @rpath/QtGui.framework/Versions/5/QtGui" \
            "DEP @rpath/QtCore.framework/Versions/5/QtCore" \
            "DEP @executable_path/../Frameworks/libpcre2-16.0.dylib" "DEP /usr/lib/libSystem.B.dylib"
        mk_macho "$FW/libfreetype.6.dylib" "ID @rpath/libfreetype.6.dylib" \
            "DEP @loader_path/libbrotlidec.1.dylib" "DEP /opt/homebrew/opt/libpng/lib/libpng16.16.dylib" \
            "DEP /usr/lib/libz.1.dylib"
        mk_macho "$FW/libbrotlidec.1.dylib" "ID @rpath/libbrotlidec.1.dylib" "DEP @rpath/libbrotlicommon.1.dylib" \
            "DEP /usr/lib/libSystem.B.dylib"
        mk_macho "$FW/libssl.3.dylib" "ID @rpath/libssl.3.dylib" \
            "DEP /opt/homebrew/Cellar/openssl@3/3.5.9/lib/libcrypto.3.dylib" "DEP /usr/lib/libSystem.B.dylib"
        # Homebrew-relocated relative ref that only works inside the Cellar.
        mk_macho "$FW/libtiff.6.dylib" "ID @rpath/libtiff.6.dylib" \
            "DEP @loader_path/../lib/libzstd.1.dylib" "DEP /usr/lib/libSystem.B.dylib"
    fi
    for w in libwebp.7 libwebpdemux.2 libwebpmux.3; do
        mk_macho "$FW/$w.dylib" "ID @rpath/$w.dylib" "DEP @rpath/libsharpyuv.0.dylib" "DEP /usr/lib/libSystem.B.dylib"
    done
}

run_closure() { # PATH_VALUE ARGS... -> sets OUT, RC
    OUT=$(PATH="$1" bash "$CLOSURE_SH" "${@:2}" 2>&1)
    RC=$?
}
has() { printf '%s\n' "$OUT" | grep -Fxq -- "$1"; }
count_unresolved() { printf '%s\n' "$OUT" | grep -c '^UNRESOLVED: '; }

mk_brew

# ── 1. pure classifier ────────────────────────────────────────────────────
echo "== pure classifier (np_classify_dep) =="
CL="$WORK/classify/Notepatra.app"
mk_bundle "$CL" broken
mk_macho "$WORK/classify/outside.dylib" "DEP /usr/lib/libSystem.B.dylib"
ln -s ../../../outside.dylib "$CL/Contents/Frameworks/libescape.dylib"
(
    # shellcheck source=scripts/macos-bundle-closure.sh
    . "$CLOSURE_SH"
    R=$(cd -P "$CL" && pwd -P)
    FWD="$R/Contents/Frameworks"
    MAC="$R/Contents/MacOS"
    HELPER="$FWD/QtWebEngineCore.framework/Versions/5/Helpers/QtWebEngineProcess.app/Contents/MacOS/QtWebEngineProcess"
    t=0
    expect() { # LABEL DEP REF DIRS EXPECTED
        local got
        got=$(np_classify_dep "$2" "$3" "$R" "$4")
        if [ "$got" = "$5" ]; then echo "  PASS  classify: $1"; else
            echo "  FAIL  classify: $1"
            echo "        dep=$2"
            echo "        got=[$got] want=[$5]"
            t=$((t + 1))
        fi
    }
    T=$(printf '\t')
    expect "libSystem is system" /usr/lib/libSystem.B.dylib "$MAC/Notepatra" "$FWD" "system${T}-"
    expect "AppKit is system" /System/Library/Frameworks/AppKit.framework/Versions/C/AppKit "$MAC/Notepatra" "$FWD" "system${T}-"
    expect "@rpath framework in bundle" @rpath/QtCore.framework/Versions/5/QtCore "$MAC/Notepatra" "$FWD" \
        "bundled${T}$FWD/QtCore.framework/Versions/5/QtCore"
    expect "brotli: @rpath/libbrotlicommon missing" @rpath/libbrotlicommon.1.dylib "$FWD/libbrotlidec.1.dylib" "$FWD" \
        "rpath-missing${T}libbrotlicommon.1.dylib"
    expect "sharpyuv: @rpath/libsharpyuv missing" @rpath/libsharpyuv.0.dylib "$FWD/libwebp.7.dylib" "$FWD" \
        "rpath-missing${T}libsharpyuv.0.dylib"
    expect "openssl: Cellar libcrypto is homebrew (not existence-based)" \
        /opt/homebrew/Cellar/openssl@3/3.5.9/lib/libcrypto.3.dylib "$FWD/libssl.3.dylib" "$FWD" \
        "homebrew${T}/opt/homebrew/Cellar/openssl@3/3.5.9/lib/libcrypto.3.dylib"
    expect "Intel prefix is homebrew" /usr/local/opt/zstd/lib/libzstd.1.dylib "$FWD/libfreetype.6.dylib" "$FWD" \
        "homebrew${T}/usr/local/opt/zstd/lib/libzstd.1.dylib"
    expect "@loader_path sibling" @loader_path/libbrotlidec.1.dylib "$FWD/libfreetype.6.dylib" "$FWD" \
        "bundled${T}$FWD/libbrotlidec.1.dylib"
    expect "@loader_path missing" @loader_path/../Frameworks/libnope.dylib "$FWD/libfreetype.6.dylib" "$FWD" \
        "relative-missing${T}$FWD/../Frameworks/libnope.dylib"
    expect "@executable_path from a plugin -> Contents/MacOS" \
        @executable_path/../Frameworks/QtCore.framework/Versions/5/QtCore "$R/Contents/PlugIns/platforms/libqcocoa.dylib" "$FWD" \
        "bundled${T}$FWD/QtCore.framework/Versions/5/QtCore"
    expect "@executable_path in nested helper .app uses the helper's MacOS" \
        @executable_path/../../../../../../../QtCore.framework/Versions/5/QtCore "$HELPER" "$FWD" \
        "bundled${T}$FWD/QtCore.framework/Versions/5/QtCore"
    expect "same helper string from a main-process lib does not resolve" \
        @executable_path/../../../../../../../QtCore.framework/Versions/5/QtCore "$FWD/libssl.3.dylib" "$FWD" \
        "relative-missing${T}$MAC/../../../../../../../QtCore.framework/Versions/5/QtCore"
    expect "CI build path is foreign" /Users/runner/work/notepatra/vendor/duckdb/libduckdb.dylib "$MAC/Notepatra" "$FWD" \
        "foreign${T}/Users/runner/work/notepatra/vendor/duckdb/libduckdb.dylib"
    expect "bare leaf is foreign" libbare.dylib "$MAC/Notepatra" "$FWD" "foreign${T}libbare.dylib"
    expect "symlink escaping the bundle is NOT bundled" @rpath/libescape.dylib "$FWD/libfreetype.6.dylib" "$FWD" \
        "rpath-missing${T}libescape.dylib"
    expect "@rpath/.. escaping the bundle is NOT bundled" @rpath/../../../outside.dylib "$MAC/Notepatra" "$FWD" \
        "rpath-missing${T}../../../outside.dylib"
    expect "libduckdb without the MacOS rpath" @rpath/libduckdb.dylib "$MAC/Notepatra" "$FWD" \
        "rpath-missing${T}libduckdb.dylib"
    expect "libduckdb with the @executable_path rpath" @rpath/libduckdb.dylib "$MAC/Notepatra" "$FWD
$MAC" "bundled${T}$MAC/libduckdb.dylib"

    # otool -l parser: decoy LC_LOAD_DYLINKER, LC_ID_DYLIB, a path with spaces.
    # (Fixture goes to a file first: macOS bash 3.2 mis-parses a here-doc
    # nested inside $(...).)
    cat >"$WORK/otool-l.txt" <<'OT'
/x/libfoo.dylib:
Load command 0
          cmd LC_ID_DYLIB
      cmdsize 48
         name @rpath/libfoo.dylib (offset 24)
Load command 1
          cmd LC_LOAD_DYLINKER
      cmdsize 32
         name /usr/lib/dyld (offset 12)
Load command 2
          cmd LC_LOAD_DYLIB
      cmdsize 64
         name /opt/homebrew/Cellar/a b/lib/libsp ace.dylib (offset 24)
   time stamp 2 Thu Jan  1 00:00:02 1970
Load command 3
          cmd LC_LOAD_WEAK_DYLIB
      cmdsize 56
         name /usr/lib/libweak.dylib (offset 24)
Load command 4
          cmd LC_RPATH
      cmdsize 40
         path @executable_path/../Frameworks (offset 12)
Load command 5
      cmd LC_SEGMENT_64
  cmdsize 72
  segname __LINKEDIT
OT
    got=$(np_parse_load_commands <"$WORK/otool-l.txt")
    want="ID${T}@rpath/libfoo.dylib
DEP${T}/opt/homebrew/Cellar/a b/lib/libsp ace.dylib
DEP${T}/usr/lib/libweak.dylib
RPATH${T}@executable_path/../Frameworks"
    if [ "$got" = "$want" ]; then echo "  PASS  parser: otool -l -> ID/DEP/RPATH, dyld decoy ignored, spaces kept"; else
        echo "  FAIL  parser: got:"
        printf '%s\n' "$got" | sed 's/^/        /'
        t=$((t + 1))
    fi
    exit "$t"
) >"$WORK/pure.out"
pure_rc=$?
cat "$WORK/pure.out"
pass=$((pass + $(grep -c '^  PASS' "$WORK/pure.out")))
fail=$((fail + $(grep -c '^  FAIL' "$WORK/pure.out")))
[ "$pure_rc" -eq "$(grep -c '^  FAIL' "$WORK/pure.out")" ] || bad "pure section exit code disagrees with its FAIL lines"

# ── 2. the shipped v0.1.131 bundle: unresolved before, resolved after ─────
echo "== broken bundle (v0.1.131 shapes) =="
B="$WORK/broken/Notepatra.app"
mk_bundle "$B" broken
run_closure "$PATH_STUB" "$B" --check-only
echo "$OUT" | sed 's/^/    | /'
[ "$RC" -eq 1 ] && ok "check-only exits 1 on the broken bundle" || bad "check-only rc=$RC on the broken bundle (want 1)"
for line in \
    "UNRESOLVED: @rpath/libbrotlicommon.1.dylib  <- Contents/Frameworks/libbrotlidec.1.dylib  [rpath-missing]" \
    "UNRESOLVED: @rpath/libsharpyuv.0.dylib  <- Contents/Frameworks/libwebp.7.dylib  [rpath-missing]" \
    "UNRESOLVED: @rpath/libsharpyuv.0.dylib  <- Contents/Frameworks/libwebpdemux.2.dylib  [rpath-missing]" \
    "UNRESOLVED: @rpath/libsharpyuv.0.dylib  <- Contents/Frameworks/libwebpmux.3.dylib  [rpath-missing]" \
    "UNRESOLVED: /opt/homebrew/Cellar/openssl@3/3.5.9/lib/libcrypto.3.dylib  <- Contents/Frameworks/libssl.3.dylib  [homebrew]" \
    "UNRESOLVED: /opt/homebrew/opt/libpng/lib/libpng16.16.dylib  <- Contents/Frameworks/libfreetype.6.dylib  [homebrew]" \
    "UNRESOLVED: @executable_path/../Frameworks/libpcre2-16.0.dylib  <- Contents/Frameworks/QtGui.framework/Versions/5/QtGui  [relative-missing]" \
    "UNRESOLVED: @loader_path/../lib/libzstd.1.dylib  <- Contents/Frameworks/libtiff.6.dylib  [relative-missing]"; do
    has "$line" && ok "reported: ${line#UNRESOLVED: }" || bad "missing report line: $line"
done
n=$(count_unresolved)
[ "$n" -eq 8 ] && ok "exactly 8 unresolved (no false positives: system, framework, @loader_path, libduckdb, helper .app, dyld decoy)" ||
    bad "expected 8 UNRESOLVED lines, got $n"
printf '%s\n' "$OUT" | grep -q '^note: LC_RPATH outside bundle (ignored): /Users/runner/' &&
    ok "absolute CI rpath on the main binary is noted, not trusted" || bad "outside-bundle rpath not noted"

: >"$LOG"
run_closure "$PATH_STUB" "$B"
echo "$OUT" | sed 's/^/    | /'
[ "$RC" -eq 0 ] && ok "fix mode exits 0" || bad "fix mode rc=$RC (want 0)"
printf '%s\n' "$OUT" | tail -n 1 | grep -q '^closure OK: ' && ok "fix mode ends with a green closure check" || bad "fix mode did not end with 'closure OK'"
FWB="$B/Contents/Frameworks"
for leaf in libbrotlicommon.1.dylib libsharpyuv.0.dylib libpng16.16.dylib libpcre2-16.0.dylib libzstd.1.dylib; do
    if [ -f "$FWB/$leaf" ] && [ ! -L "$FWB/$leaf" ] && grep -qxF "ID @rpath/$leaf" "$FWB/$leaf"; then
        ok "bundled $leaf as a real file with id @rpath/$leaf"
    else
        bad "$leaf not bundled correctly"
    fi
    grep -q "^codesign --force --sign - $FWB/$leaf\$" "$LOG" && ok "re-signed $leaf" || bad "$leaf not re-signed"
done
grep -qxF "X marker cellar-brotli-1.1.0" "$FWB/libbrotlicommon.1.dylib" &&
    ok "libbrotlicommon came from the Cellar file behind the opt/ symlink" || bad "libbrotlicommon content not from Homebrew Cellar"
grep -qxF "DEP @rpath/libcrypto.3.dylib" "$FWB/libssl.3.dylib" && ! grep -q Cellar "$FWB/libssl.3.dylib" &&
    ok "libssl -> @rpath/libcrypto.3.dylib (Cellar path gone)" || bad "libssl still references the Cellar libcrypto"
grep -qxF "DEP @rpath/libpng16.16.dylib" "$FWB/libfreetype.6.dylib" &&
    ok "libfreetype -> @rpath/libpng16.16.dylib" || bad "libfreetype libpng ref not rewritten"
grep -qxF "DEP @rpath/libzstd.1.dylib" "$FWB/libtiff.6.dylib" &&
    ok "libtiff @loader_path/../lib/libzstd -> @rpath/libzstd.1.dylib" || bad "libtiff zstd ref not rewritten"
grep -qxF "DEP @executable_path/../Frameworks/libpcre2-16.0.dylib" "$FWB/QtGui.framework/Versions/5/QtGui" &&
    ok "QtGui's @executable_path/../Frameworks ref left as is (bundling the leaf was enough)" || bad "QtGui pcre2 ref was rewritten needlessly"
for f in libssl.3.dylib libfreetype.6.dylib libtiff.6.dylib; do
    grep -q "^codesign --force --sign - $FWB/$f\$" "$LOG" && ok "re-signed modified $f" || bad "modified $f not re-signed"
done
if grep -q "^install_name_tool -change .*libcrypto.3.dylib $FWB/libssl.3.dylib\$" "$LOG"; then
    ok "libssl rewrite went through install_name_tool -change"
else
    bad "no install_name_tool -change on libssl"
fi
grep -q "libcrypto" "$LOG" && ! grep -q "^install_name_tool -id @rpath/libcrypto" "$LOG" &&
    ok "already-bundled libcrypto was reused, not re-copied" || bad "libcrypto was re-copied"

run_closure "$PATH_STUB" "$B" --check-only
[ "$RC" -eq 0 ] && [ "$(count_unresolved)" -eq 0 ] && ok "check-only is green after the fix" ||
    { bad "check-only after fix rc=$RC"; echo "$OUT" | sed 's/^/    | /'; }

: >"$LOG"
run_closure "$PATH_STUB" "$B"
[ "$RC" -eq 0 ] && [ ! -s "$LOG" ] && ok "second fix run is a no-op (fixed point; no tool writes, no re-signs)" ||
    { bad "second fix run rc=$RC, tool calls:"; sed 's/^/    | /' "$LOG"; }

# ── 3. clean bundle, guards, and failure modes ────────────────────────────
echo "== clean bundle and failure modes =="
CB="$WORK/clean/Notepatra.app"
mk_bundle "$CB" clean
run_closure "$PATH_STUB" "$CB" --check-only
[ "$RC" -eq 0 ] && printf '%s\n' "$OUT" | grep -q '^closure OK: 19 Mach-O files' &&
    ok "clean bundle passes check-only (19 Mach-O files; qt.conf/Info.plist skipped)" ||
    { bad "clean bundle rc=$RC"; echo "$OUT" | sed 's/^/    | /'; }

NR="$WORK/norpath/Notepatra.app"
mk_bundle "$NR" clean no-fw-rpath
run_closure "$PATH_STUB" "$NR" --check-only
[ "$RC" -eq 1 ] && printf '%s\n' "$OUT" | grep -q 'has no LC_RPATH @executable_path/../Frameworks' &&
    ok "main executable without @executable_path/../Frameworks rpath fails" || bad "missing main rpath not caught (rc=$RC)"

UF="$WORK/unfixable/Notepatra.app"
mk_bundle "$UF" clean
mk_macho "$UF/Contents/PlugIns/sqldrivers/libqsqlodbc.dylib" "DEP @rpath/libnowhere.1.dylib" "DEP /usr/lib/libSystem.B.dylib"
run_closure "$PATH_STUB" "$UF"
[ "$RC" -eq 1 ] && has "UNRESOLVED: @rpath/libnowhere.1.dylib  <- Contents/PlugIns/sqldrivers/libqsqlodbc.dylib  [rpath-missing]" &&
    ok "a dep Homebrew cannot supply fails fix mode with an UNRESOLVED line" ||
    { bad "unfixable dep: rc=$RC"; echo "$OUT" | sed 's/^/    | /'; }

VB="$WORK/vacuous/Notepatra.app"
mk_bundle "$VB" clean
mk_macho "$VB/Contents/PlugIns/styles/libqmacstyle.dylib" "ID @rpath/libqmacstyle.dylib"
run_closure "$PATH_STUB" "$VB" --check-only
[ "$RC" -eq 2 ] && printf '%s\n' "$OUT" | grep -q 'reported no LC_LOAD_DYLIB' &&
    ok "a Mach-O with zero parsed deps aborts (parser vacuity guard)" || bad "vacuity guard: rc=$RC"

if PATH="$PATH_NOOTOOL" command -v otool >/dev/null; then
    bad "cannot build a no-otool PATH on this machine (otool found in /usr/bin or /bin)"
else
    run_closure "$PATH_NOOTOOL" "$CB" --check-only
    [ "$RC" -ne 0 ] && printf '%s\n' "$OUT" | grep -q 'otool not found' && ! printf '%s\n' "$OUT" | grep -q 'closure OK' &&
        ok "no otool on PATH: check-only fails (rc=$RC) instead of passing" || bad "no-otool: rc=$RC out=$OUT"
    run_closure "$PATH_NOOTOOL" "$B"
    [ "$RC" -ne 0 ] && printf '%s\n' "$OUT" | grep -q 'otool not found' &&
        ok "no otool on PATH: fix mode fails (rc=$RC)" || bad "no-otool fix: rc=$RC"
fi

run_closure "$PATH_STUB" "$WORK/does-not-exist.app" --check-only
[ "$RC" -eq 2 ] && ok "non-bundle path fails (rc=2)" || bad "non-bundle path rc=$RC"

echo
echo "macos-bundle-closure self-test: $pass passed, $fail failed"
[ "$fail" -eq 0 ]
