#!/usr/bin/env bash
# build-rpm.sh — wrap the pre-built notepatra binary into a Fedora/RHEL
# .rpm package.  Mirrors the .deb layout: /opt/notepatra/ binary +
# /usr/bin/notepatra symlink, hicolor icons, .desktop file.
#
# Usage:
#   build-rpm.sh <version> <arch> <build_dir>
#
#   version    e.g. 0.1.72
#   arch       x86_64 | aarch64   (Fedora arch names, not Debian's amd64/arm64)
#   build_dir  directory containing the already-built notepatra binary
#
# Output: notepatra-<version>-1.<arch>.rpm in the cwd.
#
# We use the binary-tarball approach (Source0 is a pre-built tarball) so
# rpmbuild doesn't actually compile anything — keeps the spec simple and
# avoids dragging the whole build toolchain into the RPM Docker image.

set -euo pipefail

VERSION="${1:?need version (e.g. 0.1.72)}"
ARCH="${2:?need arch (x86_64 or aarch64)}"
BUILD_DIR="${3:?need build_dir (location of compiled notepatra)}"

REPO_ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
SPEC_IN="$REPO_ROOT/installers/rpm/notepatra.spec.in"

if [ ! -x "$BUILD_DIR/notepatra" ]; then
    echo "ERROR: $BUILD_DIR/notepatra not found or not executable" >&2
    exit 2
fi

case "$ARCH" in
    x86_64|aarch64) ;;
    *) echo "ERROR: unsupported arch '$ARCH' (use x86_64 or aarch64)" >&2; exit 2 ;;
esac

WORK="$(mktemp -d -t notepatra-rpm-XXXXXX)"
trap 'rm -rf "$WORK"' EXIT

echo "=== build-rpm: notepatra $VERSION $ARCH ==="
echo "    binary : $BUILD_DIR/notepatra"
echo "    work   : $WORK"

# ── Stage the source tarball that rpmbuild expects ────────────────────
SRC_DIR="$WORK/notepatra-$VERSION"
mkdir -p "$SRC_DIR/icons"

cp "$BUILD_DIR/notepatra" "$SRC_DIR/notepatra"
chmod 0755 "$SRC_DIR/notepatra"

if command -v patchelf >/dev/null 2>&1; then
    patchelf --set-rpath '$ORIGIN/lib' "$SRC_DIR/notepatra"
fi

if [ -f "$REPO_ROOT/vendor/duckdb/libduckdb.so" ]; then
    cp "$REPO_ROOT/vendor/duckdb/libduckdb.so" "$SRC_DIR/libduckdb.so"
else
    echo "WARN: vendor/duckdb/libduckdb.so missing — Data mode will not work" >&2
    : > "$SRC_DIR/libduckdb.so"  # empty file so the spec %files glob still matches
fi

# Bundle libqscintilla2_qt5.so.15 so the same upstream-built binary works
# on Fedora — see notepatra.spec.in for the why.  The build host (CI
# ubuntu-24.04 runner OR this dev box) is where we lift it from.
QSC_SRC=""
for CAND in \
    /usr/lib/x86_64-linux-gnu/libqscintilla2_qt5.so.15 \
    /usr/lib/aarch64-linux-gnu/libqscintilla2_qt5.so.15 \
    /lib/x86_64-linux-gnu/libqscintilla2_qt5.so.15 \
    /lib/aarch64-linux-gnu/libqscintilla2_qt5.so.15
do
    if [ -e "$CAND" ]; then
        QSC_SRC=$(readlink -f "$CAND")
        break
    fi
done
if [ -n "$QSC_SRC" ] && [ -f "$QSC_SRC" ]; then
    QSC_REAL_BASENAME=$(basename "$QSC_SRC")
    cp "$QSC_SRC" "$SRC_DIR/$QSC_REAL_BASENAME"
else
    echo "WARN: libqscintilla2_qt5.so.15 not found — RPM may not run on Fedora" >&2
fi

# .desktop file
cat > "$SRC_DIR/notepatra.desktop" <<EOF
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

for SIZE in 16 24 32 48 64 128 256 512 1024; do
    SRC="$REPO_ROOT/resources/notepatra-${SIZE}.png"
    [ -f "$SRC" ] && cp "$SRC" "$SRC_DIR/icons/notepatra-${SIZE}.png" || true
done

# ── Build the source tarball rpmbuild needs ───────────────────────────
TARBALL="$WORK/SOURCES/notepatra-$VERSION.tar.gz"
mkdir -p "$WORK/SOURCES" "$WORK/BUILD" "$WORK/RPMS" "$WORK/SPECS" "$WORK/SRPMS" "$WORK/BUILDROOT"
tar -czf "$TARBALL" -C "$WORK" "notepatra-$VERSION"

# ── Materialise the spec file ─────────────────────────────────────────
CHANGELOG_DATE=$(LC_TIME=C date '+%a %b %d %Y')
sed -e "s/@VERSION@/$VERSION/g" \
    -e "s/@CHANGELOG_DATE@/$CHANGELOG_DATE/g" \
    "$SPEC_IN" > "$WORK/SPECS/notepatra.spec"

# ── Run rpmbuild ──────────────────────────────────────────────────────
rpmbuild --define "_topdir $WORK" \
         --target "$ARCH" \
         -bb "$WORK/SPECS/notepatra.spec" 2>&1 | tail -25

# Collect the artefact and drop it next to the .deb in the cwd.
OUT=$(find "$WORK/RPMS" -name "notepatra-${VERSION}-1.*.rpm" -print -quit)
if [ -z "$OUT" ]; then
    echo "ERROR: rpmbuild produced no output" >&2
    exit 1
fi
cp "$OUT" "./notepatra-${VERSION}-1.${ARCH}.rpm"

echo ""
echo "=== Built: notepatra-${VERSION}-1.${ARCH}.rpm ==="
ls -lh "./notepatra-${VERSION}-1.${ARCH}.rpm"
echo ""
echo "=== Info ==="
# `|| true` guards: head -N closing the pipe triggers SIGPIPE on rpm,
# which under `set -o pipefail` would fail the release CI on what is
# purely a diagnostic print.
rpm -qip "./notepatra-${VERSION}-1.${ARCH}.rpm" 2>&1 | head -20 || true
echo ""
echo "=== Top 20 files ==="
rpm -qlp "./notepatra-${VERSION}-1.${ARCH}.rpm" 2>&1 | head -20 || true
