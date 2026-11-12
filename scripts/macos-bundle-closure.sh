#!/usr/bin/env bash
# macos-bundle-closure.sh — make (or prove) a macOS .app's dylib closure is
# self-contained: every load command of every Mach-O in the bundle must name a
# system library or a file that exists INSIDE the bundle.
#
#   bash scripts/macos-bundle-closure.sh <App.app>               # fix, then check
#   bash scripts/macos-bundle-closure.sh <App.app> --check-only  # check only
#
# Why this exists (v0.1.131): both macOS DMGs died at startup with
#   Could not load the Qt platform plugin "cocoa"
# because PlugIns/platforms/libqcocoa.dylib -> libfreetype.6.dylib ->
# libbrotlidec.1.dylib -> @rpath/libbrotlicommon.1.dylib, and libbrotlicommon
# was never copied into Contents/Frameworks. Homebrew's freetype had started
# linking brotli between two of our builds; macdeployqt copied libbrotlidec but
# does not follow the @rpath deps of non-Qt dylibs. The old CI audit only
# grepped for "/opt/homebrew|/usr/local" absolute paths, so an unresolved
# @rpath name sailed through, and `--version` never loads the cocoa plugin.
# Same class, older: libwebp* -> @rpath/libsharpyuv.0.dylib (image plugins),
# and libssl.3.dylib -> /opt/homebrew/Cellar/openssl@3/<ver>/lib/libcrypto.3.dylib
# (a Cellar path the old `install_name_tool -change $OPENSSL_PREFIX/...` line
# never matched — install_name_tool -change is a silent no-op on a mismatch).
#
# Fix mode, repeated until a pass changes nothing (max 10 passes):
#   @rpath/<leaf>.dylib not found  -> copy the real file (symlinks followed)
#                                     from `brew --prefix`/{lib,opt/*/lib}
#                                     into Contents/Frameworks, -id @rpath/<leaf>
#   @rpath/<X>.framework/... absent -> copy the framework the same way
#   /opt/homebrew/... or /usr/local/... (any absolute Homebrew ref, Cellar or
#                                     opt) -> bundle it if absent, then
#                                     -change <old> @rpath/<leaf> on the referrer
#   @executable_path/../Frameworks/<leaf> missing -> copy <leaf> into Frameworks
#   other @loader_path/@executable_path target missing (Homebrew's
#                                     @loader_path/../lib/<leaf>) -> bundle the
#                                     leaf, -change the ref to @rpath/<leaf>
# Every file it modifies is re-signed ad hoc (codesign --force --sign -):
# arm64 dyld refuses a Mach-O whose signature was invalidated by
# install_name_tool. Then the check below runs and decides the exit code.
#
# Check mode (also the last step of fix mode):
#   - system refs (/usr/lib/*, /System/*) are accepted without a file test
#     (on macOS 11+ they live only in the dyld shared cache);
#   - @rpath/X resolves if X exists under one of: Contents/Frameworks, the
#     main executable's LC_RPATH dirs that lie inside the bundle (the Full
#     build keeps libduckdb.dylib in Contents/MacOS via an @executable_path
#     rpath), or the referencing file's own in-bundle LC_RPATH dirs;
#   - @executable_path/ and @loader_path/ resolve against Contents/MacOS (or a
#     nested helper .app's MacOS dir) and the referrer's dir respectively;
#   - the resolved file must exist and its real path (symlinks followed) must
#     stay inside the bundle;
#   - ANY other absolute path is unresolved — /opt/homebrew/... exists on the
#     CI runner but not on a user's Mac, so existence on this machine proves
#     nothing;
#   - the main executable must carry LC_RPATH @executable_path/../Frameworks
#     (macdeployqt adds it) or none of the @rpath refs resolve at runtime.
# Each failure prints `UNRESOLVED: <dep>  <- <referencing file>  [class]`.
#
# Exit: 0 closure complete · 1 unresolved deps / missing main rpath /
#       fix could not converge · 2 cannot run (missing otool, bad bundle,
#       parser saw no load commands). A checker that cannot run never passes.
#
# Portability: macOS ships bash 3.2 — no associative arrays, no mapfile, no
# ${x,,}; only POSIX/BSD flags for find/od/sed/awk/cp/readlink.
#
# The pure helpers (np_classify_dep and friends) do no tool calls, so
# scripts/test-macos-bundle-closure.sh sources this file and exercises them on
# Linux, plus runs the whole script against a fake bundle with PATH shims.

# ── pure helpers (no otool / install_name_tool / codesign / brew) ─────────

NP_TAB=$(printf '\t')

# np_realpath PATH — print PATH with every symlink resolved; fail if it does
# not exist. readlink without -f and cd -P: both exist on macOS and Linux.
np_realpath() {
    local p="$1" n=0 link dir
    while [ -L "$p" ]; do
        n=$((n + 1))
        [ "$n" -le 40 ] || return 1
        link=$(readlink "$p") || return 1
        case "$link" in
            /*) p="$link" ;;
            *) p="${p%/*}/$link" ;;
        esac
    done
    [ -e "$p" ] || return 1
    if [ -d "$p" ]; then
        # Whole-path canonicalisation, so a trailing ".." cannot survive
        # lexically ("<root>/x/../.." must not look like it is under <root>/).
        (cd -P "$p" && pwd -P) || return 1
        return 0
    fi
    dir="${p%/*}"
    [ -n "$dir" ] || dir=/
    [ -d "$dir" ] || return 1
    dir=$(cd -P "$dir" && pwd -P) || return 1
    if [ "$dir" = / ]; then
        printf '/%s\n' "${p##*/}"
    else
        printf '%s/%s\n' "$dir" "${p##*/}"
    fi
}

# np_in_bundle CANONICAL_PATH CANONICAL_ROOT — true if PATH is inside ROOT.
np_in_bundle() {
    case "$1" in
        "$2"/*) return 0 ;;
        *) return 1 ;;
    esac
}

# np_exe_dir_for REF ROOT — the directory @executable_path means for code in
# REF: Contents/MacOS of the innermost nested .app that contains REF (e.g. the
# QtWebEngineProcess helper), else ROOT/Contents/MacOS.
np_exe_dir_for() {
    local ref="$1" root="$2" rel nested
    rel="${ref#"$root"/}"
    case "$rel" in
        */*.app/Contents/*)
            nested="${rel%.app/Contents/*}"
            printf '%s/%s.app/Contents/MacOS\n' "$root" "$nested"
            ;;
        *) printf '%s/Contents/MacOS\n' "$root" ;;
    esac
}

# np_expand_loader_path ENTRY LOADER_DIR EXE_DIR — expand an @loader_path /
# @executable_path prefix; absolute entries come back unchanged; anything else
# (bare relative, @rpath) prints nothing.
np_expand_loader_path() {
    local e="$1" loader="$2" exe="$3"
    case "$e" in
        @executable_path) printf '%s\n' "$exe" ;;
        @executable_path/*) printf '%s/%s\n' "$exe" "${e#@executable_path/}" ;;
        @loader_path) printf '%s\n' "$loader" ;;
        @loader_path/*) printf '%s/%s\n' "$loader" "${e#@loader_path/}" ;;
        /*) printf '%s\n' "$e" ;;
        *) : ;;
    esac
}

# np_classify_dep DEP REF ROOT RPATH_DIRS
#   DEP        load-command string (LC_LOAD_DYLIB & friends)
#   REF        absolute path of the Mach-O that carries it
#   ROOT       canonical absolute path of the .app
#   RPATH_DIRS newline-separated absolute dirs to try for @rpath/
# Prints "<class>\t<detail>":
#   system           -  /usr/lib/*, /System/*
#   bundled          <real path inside ROOT>
#   rpath-missing    <path after @rpath/>
#   relative-missing <expanded @executable_path/@loader_path target>
#   homebrew         <dep>   absolute /opt/homebrew or /usr/local ref
#   foreign          <dep>   any other absolute or bare ref
np_classify_dep() {
    local dep="$1" ref="$2" root="$3" rpath_dirs="$4" rel d cand real target
    case "$dep" in
        /usr/lib/* | /System/*)
            printf 'system\t-\n'
            return 0
            ;;
        @rpath/*)
            rel="${dep#@rpath/}"
            while IFS= read -r d; do
                [ -n "$d" ] || continue
                cand="$d/$rel"
                if real=$(np_realpath "$cand") && np_in_bundle "$real" "$root"; then
                    printf 'bundled\t%s\n' "$real"
                    return 0
                fi
            done <<EOF
$rpath_dirs
EOF
            printf 'rpath-missing\t%s\n' "$rel"
            return 0
            ;;
        @executable_path/* | @loader_path/*)
            target=$(np_expand_loader_path "$dep" "${ref%/*}" "$(np_exe_dir_for "$ref" "$root")")
            if real=$(np_realpath "$target") && np_in_bundle "$real" "$root"; then
                printf 'bundled\t%s\n' "$real"
            else
                printf 'relative-missing\t%s\n' "$target"
            fi
            return 0
            ;;
        /opt/homebrew/* | /usr/local/*)
            printf 'homebrew\t%s\n' "$dep"
            return 0
            ;;
        *)
            printf 'foreign\t%s\n' "$dep"
            return 0
            ;;
    esac
}

# np_is_macho_magic HEX — the first 4 bytes, as lowercase hex without spaces.
np_is_macho_magic() {
    case "$1" in
        cffaedfe | cefaedfe | feedfacf | feedface | cafebabe | cafebabf | bebafeca | bfbafeca) return 0 ;;
        *) return 1 ;;
    esac
}

# np_parse_load_commands — stdin: `otool -l` output. stdout: one line per
# interesting command, "DEP\t<name>", "ID\t<name>" or "RPATH\t<path>".
# (otool -l, not -L: -L mixes a dylib's own LC_ID_DYLIB into the dependency
# list and does not show LC_RPATH.)
np_parse_load_commands() {
    awk '
        $1 == "cmd" { cmd = $2; next }
        $1 == "name" && (cmd == "LC_LOAD_DYLIB" || cmd == "LC_LOAD_WEAK_DYLIB" ||
                         cmd == "LC_REEXPORT_DYLIB" || cmd == "LC_LAZY_LOAD_DYLIB" ||
                         cmd == "LC_LOAD_UPWARD_DYLIB" || cmd == "LC_ID_DYLIB") {
            s = $0
            sub(/^[ \t]*name[ \t]+/, "", s)
            sub(/[ \t]+\(offset [0-9]+\)[ \t]*$/, "", s)
            print ((cmd == "LC_ID_DYLIB") ? "ID" : "DEP") "\t" s
            next
        }
        $1 == "path" && cmd == "LC_RPATH" {
            s = $0
            sub(/^[ \t]*path[ \t]+/, "", s)
            sub(/[ \t]+\(offset [0-9]+\)[ \t]*$/, "", s)
            print "RPATH\t" s
            next
        }
    '
}

# ── tool-using layer ──────────────────────────────────────────────────────

np_die() {
    echo "ERROR: $*" >&2
    exit 2
}

np_rel() { printf '%s\n' "${1#"$NP_ROOT"/}"; }

# np_load_commands FILE — parsed, de-duplicated load commands (fat files list
# each arch). Fails (exit 2) if otool fails or finds no dependency at all:
# every real Mach-O links at least libSystem, so zero DEP lines means the
# parser no longer understands otool's output, and a silent pass would lie.
np_load_commands() {
    local f="$1" raw parsed
    raw=$(otool -l "$f") || np_die "otool -l failed on $(np_rel "$f")"
    parsed=$(printf '%s\n' "$raw" | np_parse_load_commands | sort -u)
    case "$parsed" in
        *"DEP${NP_TAB}"*) ;;
        *) np_die "otool -l reported no LC_LOAD_DYLIB for $(np_rel "$f") — parser out of date with otool output?" ;;
    esac
    printf '%s\n' "$parsed"
}

# np_list_macho — every Mach-O regular file under Contents/ (dSYMs, .o skipped),
# written to $NP_TMP/macho. Detects by magic bytes, not by name.
np_list_macho() {
    local f magic
    find "$NP_ROOT/Contents" -type f ! -path '*.dSYM/*' ! -name '*.o' >"$NP_TMP/allfiles" ||
        np_die "find failed under $NP_ROOT/Contents"
    : >"$NP_TMP/macho"
    while IFS= read -r f; do
        [ -r "$f" ] || np_die "unreadable file in bundle: $(np_rel "$f")"
        magic=$(od -An -tx1 -N4 "$f" | tr -d ' \n') || np_die "od failed on $(np_rel "$f")"
        if np_is_macho_magic "$magic"; then
            printf '%s\n' "$f" >>"$NP_TMP/macho"
        fi
    done <"$NP_TMP/allfiles"
    sort -o "$NP_TMP/macho" "$NP_TMP/macho"
    [ -s "$NP_TMP/macho" ] || np_die "no Mach-O files found under $NP_ROOT/Contents"
}

# np_rpath_dirs_for REF LOADCMDS — newline list of in-bundle dirs for @rpath.
np_rpath_dirs_for() {
    local ref="$1" cmds="$2" kind val dir real
    printf '%s\n' "$NP_FW"
    printf '%s\n' "$NP_MAIN_RPATH_DIRS"
    while IFS="$NP_TAB" read -r kind val; do
        [ "$kind" = RPATH ] || continue
        dir=$(np_expand_loader_path "$val" "${ref%/*}" "$(np_exe_dir_for "$ref" "$NP_ROOT")")
        [ -n "$dir" ] || continue
        if real=$(np_realpath "$dir") && np_in_bundle "$real" "$NP_ROOT"; then
            printf '%s\n' "$real"
        fi
    done <<EOF
$cmds
EOF
}

# Find the main executable: CFBundleExecutable from an XML Info.plist, else
# the bundle's name without ".app".
np_find_main_exe() {
    local name="" plist="$NP_ROOT/Contents/Info.plist"
    if [ -f "$plist" ]; then
        name=$(awk '
            /<key>CFBundleExecutable<\/key>/ { want = 1 }
            want && /<string>/ {
                s = $0
                sub(/.*<key>CFBundleExecutable<\/key>/, "", s)
                if (s ~ /<string>/) {
                    sub(/.*<string>/, "", s); sub(/<\/string>.*/, "", s)
                    print s; exit
                }
            }
        ' "$plist")
    fi
    if [ -z "$name" ] || [ ! -f "$NP_ROOT/Contents/MacOS/$name" ]; then
        name="${NP_ROOT##*/}"
        name="${name%.app}"
    fi
    NP_MAIN="$NP_ROOT/Contents/MacOS/$name"
    [ -f "$NP_MAIN" ] || np_die "main executable not found (tried Info.plist CFBundleExecutable and Contents/MacOS/$name)"
}

# Main executable rpaths: must include @executable_path/../Frameworks; the
# in-bundle ones become extra @rpath search dirs for every file.
np_main_rpaths() {
    local cmds kind val dir real has_fw=0
    cmds=$(np_load_commands "$NP_MAIN") || exit 2
    NP_MAIN_RPATH_DIRS=""
    while IFS="$NP_TAB" read -r kind val; do
        [ "$kind" = RPATH ] || continue
        case "$val" in
            @executable_path/../Frameworks | @executable_path/../Frameworks/ | \
                @loader_path/../Frameworks | @loader_path/../Frameworks/) has_fw=1 ;;
        esac
        dir=$(np_expand_loader_path "$val" "$NP_ROOT/Contents/MacOS" "$NP_ROOT/Contents/MacOS")
        if [ -n "$dir" ] && real=$(np_realpath "$dir") && np_in_bundle "$real" "$NP_ROOT"; then
            NP_MAIN_RPATH_DIRS="$NP_MAIN_RPATH_DIRS$real
"
        fi
    done <<EOF
$cmds
EOF
    NP_MAIN_HAS_FW_RPATH=$has_fw
}

# ── fix mode ──────────────────────────────────────────────────────────────

# np_init_brew — resolve `brew --prefix` once, in the main shell (np_die in a
# $(...) subshell would only kill the subshell). No brew = nothing can be
# bundled; the final check then reports what stayed unresolved.
np_init_brew() {
    NP_BREW=""
    if command -v brew >/dev/null; then
        NP_BREW=$(brew --prefix) || np_die "brew --prefix failed"
        [ -d "$NP_BREW" ] || np_die "brew --prefix gave '$NP_BREW', not a directory"
        echo "homebrew prefix: $NP_BREW"
    else
        echo "warning: brew not on PATH — missing dylibs cannot be bundled, only reported"
    fi
}

# np_locate_in_brew NAME — first of <brew>/lib/NAME, <brew>/opt/*/lib/NAME
# (file or .framework dir), symlinks resolved.
np_locate_in_brew() {
    local name="$1" cand real
    [ -n "$NP_BREW" ] || return 1
    for cand in "$NP_BREW/lib/$name" "$NP_BREW"/opt/*/lib/"$name"; do
        if real=$(np_realpath "$cand"); then
            printf '%s\n' "$real"
            return 0
        fi
    done
    return 1
}

np_mark_modified() { printf '%s\n' "$1" >>"$NP_TMP/modified"; }

# np_bundle_dylib LEAF [PREFERRED_SRC] — copy into Frameworks/LEAF and set id.
np_bundle_dylib() {
    local leaf="$1" pref="${2:-}" src="" dst="$NP_FW/$1"
    if [ -n "$pref" ]; then
        src=$(np_realpath "$pref") || src=""
    fi
    if [ -z "$src" ]; then
        src=$(np_locate_in_brew "$leaf") || {
            echo "  cannot fix: $leaf not found under ${NP_BREW:-<no brew>}/{lib,opt/*/lib}"
            return 1
        }
    fi
    echo "  bundle: $leaf  (from $src)"
    cp "$src" "$dst" || np_die "cp $src -> $dst failed"
    chmod u+w "$dst" || np_die "chmod u+w $dst failed"
    install_name_tool -id "@rpath/$leaf" "$dst" || np_die "install_name_tool -id failed on $dst"
    np_mark_modified "$dst"
}

# np_bundle_framework NAME.framework INNER [PREFERRED_DIR] — copy a framework
# directory (symlinks inside kept as symlinks) and set its binary's id.
np_bundle_framework() {
    local fwname="$1" inner="$2" pref="${3:-}" src=""
    if [ -n "$pref" ] && [ -d "$pref" ]; then
        src=$(np_realpath "$pref") || src=""
    fi
    if [ -z "$src" ]; then
        src=$(np_locate_in_brew "$fwname") || {
            echo "  cannot fix: $fwname not found under ${NP_BREW:-<no brew>}/{lib,opt/*/lib}"
            return 1
        }
    fi
    echo "  bundle: $fwname  (from $src)"
    rm -rf "${NP_FW:?}/$fwname"
    cp -R "$src" "$NP_FW/$fwname" || np_die "cp -R $src failed"
    chmod -R u+w "$NP_FW/$fwname" || np_die "chmod -R u+w $NP_FW/$fwname failed"
    if [ -f "$NP_FW/$fwname/$inner" ]; then
        install_name_tool -id "@rpath/$fwname/$inner" "$NP_FW/$fwname/$inner" ||
            np_die "install_name_tool -id failed on $fwname/$inner"
        np_mark_modified "$NP_FW/$fwname/$inner"
    else
        echo "  cannot fix: copied $fwname has no $inner"
        return 1
    fi
}

# np_change REF OLD NEW — rewrite one load command and PROVE it changed:
# install_name_tool -change exits 0 when OLD does not match (that silent
# no-op is how the old libssl line shipped a Cellar path for releases).
np_change() {
    local ref="$1" old="$2" new="$3" after
    chmod u+w "$ref" || np_die "chmod u+w $ref failed"
    install_name_tool -change "$old" "$new" "$ref" || np_die "install_name_tool -change failed on $(np_rel "$ref")"
    after=$(np_load_commands "$ref") || exit 2
    if printf '%s\n' "$after" | grep -Fxq "DEP${NP_TAB}$old"; then
        np_die "install_name_tool -change left '$old' in $(np_rel "$ref") (silent no-op)"
    fi
    echo "  rewrite: $old -> $new  in $(np_rel "$ref")"
    np_mark_modified "$ref"
}

# np_fix_one REF DEP CLASS DETAIL RPATH_DIRS — returns 0 if it changed something
# that now resolves, 1 if it could not.
np_fix_one() {
    local ref="$1" dep="$2" class="$3" detail="$4" dirs="$5"
    local leaf fwname inner prefix new recheck dir
    case "$class" in
        rpath-missing)
            case "$detail" in
                *.framework/*)
                    fwname="${detail%%/*}"
                    case "$fwname" in *.framework) ;; *) return 1 ;; esac
                    inner="${detail#*/}"
                    np_bundle_framework "$fwname" "$inner" || return 1
                    ;;
                */*) return 1 ;;
                *) np_bundle_dylib "$detail" || return 1 ;;
            esac
            ;;
        relative-missing)
            # @executable_path/../Frameworks/<leaf> (macdeployqt shape): bundling
            # the leaf is enough. Any other @loader_path/@executable_path
            # target (e.g. Homebrew's @loader_path/../lib/<leaf>) is bundled
            # and the ref rewritten to @rpath/<leaf>.
            case "$detail" in
                *.framework/*)
                    prefix="${detail%%.framework/*}"
                    fwname="${prefix##*/}.framework"
                    inner="${detail#*.framework/}"
                    new="@rpath/$fwname/$inner"
                    recheck=$(np_classify_dep "$new" "$ref" "$NP_ROOT" "$dirs")
                    if [ "${recheck%%"$NP_TAB"*}" != bundled ]; then
                        np_bundle_framework "$fwname" "$inner" || return 1
                    fi
                    ;;
                *)
                    leaf="${detail##*/}"
                    new="@rpath/$leaf"
                    recheck=$(np_classify_dep "$new" "$ref" "$NP_ROOT" "$dirs")
                    if [ "${recheck%%"$NP_TAB"*}" != bundled ]; then
                        np_bundle_dylib "$leaf" || return 1
                    fi
                    ;;
            esac
            recheck=$(np_classify_dep "$dep" "$ref" "$NP_ROOT" "$dirs")
            [ "${recheck%%"$NP_TAB"*}" = bundled ] && return 0
            recheck=$(np_classify_dep "$new" "$ref" "$NP_ROOT" "$dirs")
            [ "${recheck%%"$NP_TAB"*}" = bundled ] || {
                echo "  cannot fix: $new still does not resolve for $(np_rel "$ref")"
                return 1
            }
            np_change "$ref" "$dep" "$new"
            return 0
            ;;
        homebrew)
            case "$dep" in
                *.framework/*)
                    prefix="${dep%%.framework/*}"
                    fwname="${prefix##*/}.framework"
                    inner="${dep#*.framework/}"
                    new="@rpath/$fwname/$inner"
                    recheck=$(np_classify_dep "$new" "$ref" "$NP_ROOT" "$dirs")
                    if [ "${recheck%%"$NP_TAB"*}" != bundled ]; then
                        np_bundle_framework "$fwname" "$inner" "$prefix.framework" || return 1
                    fi
                    ;;
                *)
                    leaf="${dep##*/}"
                    new="@rpath/$leaf"
                    recheck=$(np_classify_dep "$new" "$ref" "$NP_ROOT" "$dirs")
                    if [ "${recheck%%"$NP_TAB"*}" != bundled ]; then
                        np_bundle_dylib "$leaf" "$dep" || return 1
                    fi
                    ;;
            esac
            recheck=$(np_classify_dep "$new" "$ref" "$NP_ROOT" "$dirs")
            [ "${recheck%%"$NP_TAB"*}" = bundled ] || {
                echo "  cannot fix: $new still does not resolve for $(np_rel "$ref")"
                return 1
            }
            np_change "$ref" "$dep" "$new"
            return 0
            ;;
        *) return 1 ;;
    esac
    recheck=$(np_classify_dep "$dep" "$ref" "$NP_ROOT" "$dirs")
    if [ "${recheck%%"$NP_TAB"*}" = bundled ]; then
        return 0
    fi
    echo "  cannot fix: $dep still unresolved after bundling (for $(np_rel "$ref"))"
    return 1
}

np_fix_pass() {
    local f cmds dirs kind dep res class detail changes=0
    np_list_macho
    while IFS= read -r f; do
        cmds=$(np_load_commands "$f") || exit 2
        dirs=$(np_rpath_dirs_for "$f" "$cmds")
        while IFS="$NP_TAB" read -r kind dep; do
            [ "$kind" = DEP ] || continue
            res=$(np_classify_dep "$dep" "$f" "$NP_ROOT" "$dirs")
            class="${res%%"$NP_TAB"*}"
            detail="${res#*"$NP_TAB"}"
            case "$class" in system | bundled) continue ;; esac
            # </dev/null: nothing in the fix path may eat this loop's input.
            if np_fix_one "$f" "$dep" "$class" "$detail" "$dirs" </dev/null; then
                changes=$((changes + 1))
            fi
        done <<EOF
$cmds
EOF
    done <"$NP_TMP/macho"
    NP_PASS_CHANGES=$changes
}

np_resign_modified() {
    local f
    [ -s "$NP_TMP/modified" ] || return 0
    sort -u -o "$NP_TMP/modified" "$NP_TMP/modified"
    echo "=== ad-hoc re-sign of $(wc -l <"$NP_TMP/modified" | tr -d ' ') modified file(s) ==="
    while IFS= read -r f; do
        codesign --force --sign - "$f" || np_die "codesign --force --sign - failed on $(np_rel "$f")"
        echo "  signed: $(np_rel "$f")"
    done <"$NP_TMP/modified"
}

# ── check ─────────────────────────────────────────────────────────────────

np_check() {
    local f cmds dirs kind val res class detail nfiles=0 nrefs=0 nsys=0 nbun=0 nbad=0 real dir
    np_list_macho
    if [ "$NP_MAIN_HAS_FW_RPATH" != 1 ]; then
        echo "UNRESOLVED: main executable $(np_rel "$NP_MAIN") has no LC_RPATH @executable_path/../Frameworks — no @rpath ref can resolve at runtime (did macdeployqt stop adding it? fix with install_name_tool -add_rpath)"
        nbad=$((nbad + 1))
    fi
    while IFS= read -r f; do
        nfiles=$((nfiles + 1))
        cmds=$(np_load_commands "$f") || exit 2
        dirs=$(np_rpath_dirs_for "$f" "$cmds")
        while IFS="$NP_TAB" read -r kind val; do
            case "$kind" in
                RPATH)
                    # Informational: an rpath outside the bundle is ignored for
                    # resolution here, but on a Mac WITH Homebrew it can mask a
                    # missing library — which is how this class hides locally.
                    dir=$(np_expand_loader_path "$val" "${f%/*}" "$(np_exe_dir_for "$f" "$NP_ROOT")")
                    if [ -n "$dir" ] && real=$(np_realpath "$dir") && np_in_bundle "$real" "$NP_ROOT"; then
                        :
                    else
                        echo "note: LC_RPATH outside bundle (ignored): $val  in $(np_rel "$f")"
                    fi
                    ;;
                DEP)
                    nrefs=$((nrefs + 1))
                    res=$(np_classify_dep "$val" "$f" "$NP_ROOT" "$dirs")
                    class="${res%%"$NP_TAB"*}"
                    detail="${res#*"$NP_TAB"}"
                    case "$class" in
                        system) nsys=$((nsys + 1)) ;;
                        bundled) nbun=$((nbun + 1)) ;;
                        *)
                            echo "UNRESOLVED: $val  <- $(np_rel "$f")  [$class]"
                            nbad=$((nbad + 1))
                            ;;
                    esac
                    ;;
            esac
        done <<EOF
$cmds
EOF
    done <"$NP_TMP/macho"
    if [ "$nbad" -gt 0 ]; then
        echo "closure FAILED: $nbad unresolved ($nfiles Mach-O files, $nrefs load refs: $nsys system, $nbun in-bundle)"
        return 1
    fi
    echo "closure OK: $nfiles Mach-O files, $nrefs load refs ($nsys system, $nbun in-bundle), 0 unresolved"
    return 0
}

# ── main ──────────────────────────────────────────────────────────────────

np_main() {
    set -uo pipefail
    local app="" check_only=0 a pass=0
    for a in "$@"; do
        case "$a" in
            --check-only) check_only=1 ;;
            -h | --help)
                sed -n '2,8p' "$0"
                return 0
                ;;
            -*) np_die "unknown option: $a" ;;
            *)
                [ -z "$app" ] || np_die "only one bundle path allowed"
                app="$a"
                ;;
        esac
    done
    [ -n "$app" ] || np_die "usage: $0 <App.app> [--check-only]"
    [ -d "$app/Contents" ] || np_die "$app is not an app bundle (no Contents/)"

    command -v otool >/dev/null ||
        np_die "otool not found on PATH — this check needs macOS (Xcode command line tools); refusing to report a pass it could not compute"
    if [ "$check_only" = 0 ]; then
        command -v install_name_tool >/dev/null || np_die "install_name_tool not found on PATH (fix mode)"
        command -v codesign >/dev/null || np_die "codesign not found on PATH (fix mode)"
    fi

    NP_ROOT=$(cd -P "$app" && pwd -P) || np_die "cannot resolve $app"
    NP_FW="$NP_ROOT/Contents/Frameworks"
    NP_TMP=$(mktemp -d) || np_die "mktemp failed"
    trap 'rm -rf "$NP_TMP"' EXIT
    : >"$NP_TMP/modified"

    np_find_main_exe
    echo "bundle: $NP_ROOT"
    echo "main executable: $(np_rel "$NP_MAIN")"

    if [ "$check_only" = 0 ]; then
        mkdir -p "$NP_FW" || np_die "cannot create $NP_FW"
        np_init_brew
        while :; do
            pass=$((pass + 1))
            if [ "$pass" -gt 10 ]; then
                echo "fix FAILED: still changing after 10 passes"
                np_resign_modified
                return 1
            fi
            np_main_rpaths
            echo "=== fix pass $pass ==="
            np_fix_pass
            echo "  pass $pass: $NP_PASS_CHANGES change(s)"
            [ "$NP_PASS_CHANGES" -gt 0 ] || break
        done
        np_resign_modified
    fi

    np_main_rpaths
    echo "=== closure check ==="
    np_check
}

# Run only when executed, not when sourced by the self-test.
if [ "${BASH_SOURCE[0]}" = "$0" ]; then
    np_main "$@"
    exit $?
fi
