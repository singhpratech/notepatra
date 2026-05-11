#!/usr/bin/env bash
# build-appimage.sh — produce Notepatra-<version>-<arch>.AppImage from a
# pre-built notepatra binary.
#
# Usage:
#   build-appimage.sh <version> <arch> <build_dir>
#
#   version    e.g. 0.1.72
#   arch       x86_64 | aarch64
#   build_dir  directory containing the already-built notepatra binary
#
# Strategy: stage an AppDir, then drive linuxdeploy + linuxdeploy-plugin-qt
# to harvest every transitively-needed shared library + Qt plugin into
# the AppDir, then squashfs it into a single-file AppImage.
#
# Requires: linuxdeploy + linuxdeploy-plugin-qt available on PATH OR
# downloadable from the GitHub continuous-release.  The CI matrix
# downloads them in a separate prep step; this script just runs them.

set -euo pipefail

VERSION="${1:?need version (e.g. 0.1.72)}"
ARCH="${2:?need arch (x86_64 or aarch64)}"
BUILD_DIR="${3:?need build_dir (location of compiled notepatra)}"

REPO_ROOT="$(cd "$(dirname "$0")/../.." && pwd)"

case "$ARCH" in
    x86_64|aarch64) ;;
    *) echo "ERROR: unsupported arch '$ARCH'" >&2; exit 2 ;;
esac

if [ ! -x "$BUILD_DIR/notepatra" ]; then
    echo "ERROR: $BUILD_DIR/notepatra not found or not executable" >&2
    exit 2
fi

WORK="$(mktemp -d -t notepatra-appimage-XXXXXX)"
trap 'rm -rf "$WORK"' EXIT

APPDIR="$WORK/AppDir"
mkdir -p "$APPDIR/usr/bin"
mkdir -p "$APPDIR/usr/lib"
mkdir -p "$APPDIR/usr/share/applications"
mkdir -p "$APPDIR/usr/share/icons/hicolor/256x256/apps"
mkdir -p "$APPDIR/usr/share/icons/hicolor/scalable/apps"

echo "=== build-appimage: Notepatra $VERSION $ARCH ==="
echo "    binary  : $BUILD_DIR/notepatra"
echo "    appdir  : $APPDIR"

# ── Stage binary + vendored libs in the AppDir layout linuxdeploy
#    expects.  $ORIGIN/../lib will pull libduckdb in at runtime.
install -m 0755 "$BUILD_DIR/notepatra" "$APPDIR/usr/bin/notepatra"

if [ -f "$REPO_ROOT/vendor/duckdb/libduckdb.so" ]; then
    install -m 0644 "$REPO_ROOT/vendor/duckdb/libduckdb.so" "$APPDIR/usr/lib/libduckdb.so"
fi

# Reset RPATH so linuxdeploy's own rewrite takes effect.  Without this,
# our build-machine-baked RPATH overrides linuxdeploy's $ORIGIN paths.
if command -v patchelf >/dev/null 2>&1; then
    patchelf --remove-rpath "$APPDIR/usr/bin/notepatra" 2>/dev/null || true
fi

# ── .desktop + icons (linuxdeploy needs at least one .desktop in the
#    AppDir root or under usr/share/applications/ to derive AppRun) ──
DESKTOP="$APPDIR/usr/share/applications/notepatra.desktop"
cat > "$DESKTOP" <<EOF
[Desktop Entry]
Name=Notepatra
GenericName=Code Editor
Comment=Notepad++-style native code editor for the AI era
Exec=notepatra %F
Icon=notepatra
Terminal=false
Type=Application
Categories=Development;TextEditor;IDE;
MimeType=text/plain;text/x-python;text/x-csrc;text/x-c++src;text/html;text/css;text/javascript;application/json;application/xml;
StartupWMClass=notepatra
Keywords=editor;code;notepad;notepatra;ai;ollama;
EOF

# linuxdeploy wants the icon both at /usr/share/icons/hicolor/<size>/...
# AND at the AppDir root (which it uses to derive the AppImage thumbnail).
if [ -f "$REPO_ROOT/resources/notepatra-256.png" ]; then
    install -m 0644 "$REPO_ROOT/resources/notepatra-256.png" \
                    "$APPDIR/usr/share/icons/hicolor/256x256/apps/notepatra.png"
fi
if [ -f "$REPO_ROOT/resources/notepatra-1024.png" ]; then
    install -m 0644 "$REPO_ROOT/resources/notepatra-1024.png" \
                    "$APPDIR/usr/share/icons/hicolor/scalable/apps/notepatra.png"
fi
# AppDir root icon — linuxdeploy uses this for the AppImage thumbnail.
if [ -f "$REPO_ROOT/resources/notepatra-256.png" ]; then
    install -m 0644 "$REPO_ROOT/resources/notepatra-256.png" "$APPDIR/notepatra.png"
fi

# Mirror the .desktop to the AppDir root — linuxdeploy historically
# required this; modern versions accept either location, both works.
cp "$DESKTOP" "$APPDIR/notepatra.desktop"

# ── linuxdeploy: download if not on PATH ─────────────────────────────
download_tool() {
    local url="$1" out="$2"
    if [ -x "$out" ]; then return 0; fi
    echo "  downloading $(basename "$out") ..."
    curl -fSL "$url" -o "$out"
    chmod +x "$out"
}

LD_DIR="$WORK/tools"
mkdir -p "$LD_DIR"

LDEPLOY="$LD_DIR/linuxdeploy-${ARCH}.AppImage"
LDEPLOY_QT="$LD_DIR/linuxdeploy-plugin-qt-${ARCH}.AppImage"

download_tool \
    "https://github.com/linuxdeploy/linuxdeploy/releases/download/continuous/linuxdeploy-${ARCH}.AppImage" \
    "$LDEPLOY"
download_tool \
    "https://github.com/linuxdeploy/linuxdeploy-plugin-qt/releases/download/continuous/linuxdeploy-plugin-qt-${ARCH}.AppImage" \
    "$LDEPLOY_QT"

# linuxdeploy AppImages need fuse on the host OR --appimage-extract-and-run.
# CI runners typically have no fuse, so extract-and-run unconditionally.
# APPIMAGE_EXTRACT_AND_RUN=1 propagates into linuxdeploy's invocation of
# its child plugin AppImages too — without it, linuxdeploy-plugin-qt
# fails with "exit code 127" trying to mount itself.
export APPIMAGE_EXTRACT_AND_RUN=1
export NO_STRIP=1
export QML_SOURCES_PATHS="$REPO_ROOT/src"  # so the qt-plugin discovers QML imports (none today, but future-proof)
# Tell linuxdeploy-plugin-qt where qmake is (default is "qmake" on PATH,
# fine on Ubuntu where qt5-qmake provides it).
if command -v qmake >/dev/null 2>&1; then
    export QMAKE=$(command -v qmake)
elif command -v qmake-qt5 >/dev/null 2>&1; then
    export QMAKE=$(command -v qmake-qt5)
fi

# Tell linuxdeploy where to find non-system libs the binary refs
# (libduckdb.so is vendored, not in /usr/lib).  Without this it fails
# resolving the binary's DT_NEEDED list with "Could not find dependency".
LIB_HINT_DIRS=""
[ -d "$REPO_ROOT/vendor/duckdb" ] && LIB_HINT_DIRS="$REPO_ROOT/vendor/duckdb"
export LD_LIBRARY_PATH="${LIB_HINT_DIRS}${LIB_HINT_DIRS:+:}${LD_LIBRARY_PATH:-}"

# Also pre-collect the libraries we want to bundle explicitly via
# --library — this is more deterministic than relying on
# LD_LIBRARY_PATH introspection.
LIBRARY_FLAGS=()
[ -f "$REPO_ROOT/vendor/duckdb/libduckdb.so" ] && LIBRARY_FLAGS+=(--library "$REPO_ROOT/vendor/duckdb/libduckdb.so")
for CAND in \
    /usr/lib/x86_64-linux-gnu/libqscintilla2_qt5.so.15 \
    /usr/lib/aarch64-linux-gnu/libqscintilla2_qt5.so.15 \
    /lib/x86_64-linux-gnu/libqscintilla2_qt5.so.15 \
    /lib/aarch64-linux-gnu/libqscintilla2_qt5.so.15
do
    if [ -e "$CAND" ]; then
        LIBRARY_FLAGS+=(--library "$CAND")
        break
    fi
done

echo "=== running linuxdeploy --output appimage (qt-plugin + bundle) ==="
( cd "$WORK" && \
  ARCH="$ARCH" "$LDEPLOY" \
        --appdir "$APPDIR" \
        --plugin qt \
        --desktop-file "$DESKTOP" \
        --icon-file "$APPDIR/notepatra.png" \
        "${LIBRARY_FLAGS[@]}" \
        --output appimage \
        2>&1 | tail -25 )

# linuxdeploy outputs e.g. "Notepatra-x86_64.AppImage" in the cwd of the
# command (which we set to $WORK).  Rename it to include version.
SRC_AI=$(ls "$WORK"/Notepatra*.AppImage 2>/dev/null | head -1)
if [ -z "$SRC_AI" ] || [ ! -f "$SRC_AI" ]; then
    echo "ERROR: linuxdeploy did not produce an .AppImage" >&2
    exit 1
fi
DEST="Notepatra-${VERSION}-${ARCH}.AppImage"
cp "$SRC_AI" "$DEST"
chmod +x "$DEST"

echo ""
echo "=== Built: $DEST ==="
ls -lh "$DEST"
