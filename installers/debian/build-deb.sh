#!/usr/bin/env bash
# build-deb.sh — assemble notepatra*.deb for either flavor.
#
# Usage:
#   build-deb.sh <version> <arch> <flavor> <build_dir>
#
#   version    e.g. 0.1.72
#   arch       amd64 | arm64
#   flavor     regular  → notepatra_<version>_<arch>.deb
#              local-ai → notepatra-local-ai_<version>_<arch>.deb
#   build_dir  directory containing the already-built notepatra binary
#              (typically build-v172 or build-v172-nocloud)
#
# Output: <package>_<version>_<arch>.deb in the current working dir.
#
# Both flavors install to the same paths so that admins choosing between
# them swap one .deb in for the other (Conflicts/Replaces fields in
# control take care of the transition).  The on-disk command stays
# `notepatra` either way — only the binary's capabilities change.

set -euo pipefail

VERSION="${1:?need version (e.g. 0.1.72)}"
ARCH="${2:?need arch (amd64 or arm64)}"
FLAVOR="${3:?need flavor (regular or local-ai)}"
BUILD_DIR="${4:?need build_dir (location of compiled notepatra)}"

REPO_ROOT="$(cd "$(dirname "$0")/../.." && pwd)"

case "$FLAVOR" in
    regular)
        PKG="notepatra"
        DESC_SHORT="Notepad++-style native code editor for the AI era"
        ;;
    local-ai)
        PKG="notepatra-local-ai"
        DESC_SHORT="Cloud-free Notepatra — local & private-network LLM endpoints only"
        ;;
    *)
        echo "ERROR: unknown flavor '$FLAVOR' (expected: regular | local-ai)" >&2
        exit 2
        ;;
esac

if [ ! -x "$BUILD_DIR/notepatra" ]; then
    echo "ERROR: $BUILD_DIR/notepatra not found or not executable" >&2
    exit 2
fi

STAGE="$(mktemp -d -t notepatra-deb-XXXXXX)"
trap 'rm -rf "$STAGE"' EXIT

echo "=== build-deb: $PKG $VERSION $ARCH ($FLAVOR flavor) ==="
echo "    binary : $BUILD_DIR/notepatra"
echo "    stage  : $STAGE"

# ── Layout ────────────────────────────────────────────────────────────
#   /opt/notepatra/notepatra            ← the binary (patchelf'd RPATH)
#   /opt/notepatra/lib/libduckdb.so     ← vendored DuckDB
#   /usr/bin/notepatra                  ← symlink → /opt/notepatra/notepatra
#   /usr/share/applications/notepatra.desktop
#   /usr/share/icons/hicolor/<size>x<size>/apps/notepatra.png × hicolor sizes
#   /usr/share/doc/<PKG>/{copyright,changelog.Debian.gz}
#
mkdir -p "$STAGE/DEBIAN"
mkdir -p "$STAGE/opt/notepatra/lib"
mkdir -p "$STAGE/usr/bin"
mkdir -p "$STAGE/usr/share/applications"
mkdir -p "$STAGE/usr/share/doc/$PKG"

install -m 0755 "$BUILD_DIR/notepatra" "$STAGE/opt/notepatra/notepatra"

# patchelf — drop the dev-machine RUNPATH (e.g. /home/...vendor/duckdb)
# and re-point it at the in-package lib directory.  $ORIGIN expands at
# load time to the directory of the binary, which is /opt/notepatra/.
if command -v patchelf >/dev/null 2>&1; then
    patchelf --set-rpath '$ORIGIN/lib' "$STAGE/opt/notepatra/notepatra"
else
    echo "WARN: patchelf not installed — binary may fail to find libduckdb.so" >&2
fi

# Ship libduckdb.so alongside the binary (DuckDB is vendored upstream).
if [ -f "$REPO_ROOT/vendor/duckdb/libduckdb.so" ]; then
    install -m 0644 "$REPO_ROOT/vendor/duckdb/libduckdb.so" \
                    "$STAGE/opt/notepatra/lib/libduckdb.so"
else
    echo "WARN: vendor/duckdb/libduckdb.so missing — Data mode will not work" >&2
fi

# Bundle libqscintilla2_qt5.so.15 (and its SONAME chain).  Even on Debian
# proper apt resolution would pick up libqscintilla2-qt5-15 from main, but
# we ship our own copy for two reasons:
#   1. Cross-distro safety — the same binary tarball ships in the RPM too
#      where Fedora's QScintilla packaging has subtly different exported
#      symbols (e.g. QsciLexerNASM constructor mangling).  Bundling the
#      one we linked against guarantees ABI match on every distro.
#   2. Upstream Notepatra builds against QScintilla 2.14.1's specific
#      lexer set (NASM, Markdown, etc.).  Distros that ship older 2.13
#      builds would crash on missing symbols.
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
    # Copy the real file (e.g. libqscintilla2_qt5.so.15.2.1) and create
    # the SONAME-chain symlinks expected by the dynamic linker.
    QSC_REAL_BASENAME=$(basename "$QSC_SRC")        # libqscintilla2_qt5.so.15.2.1
    install -m 0644 "$QSC_SRC" "$STAGE/opt/notepatra/lib/$QSC_REAL_BASENAME"
    ln -sf "$QSC_REAL_BASENAME" "$STAGE/opt/notepatra/lib/libqscintilla2_qt5.so.15.2"
    ln -sf "$QSC_REAL_BASENAME" "$STAGE/opt/notepatra/lib/libqscintilla2_qt5.so.15"
else
    echo "WARN: libqscintilla2_qt5.so.15 not found on build host — RPM/deb may break on other distros" >&2
fi

# The user-facing command is `notepatra` regardless of flavor — only the
# binary's capabilities differ between the two .debs.  Use a relative
# symlink so the package is fully portable when copied around.
ln -sf /opt/notepatra/notepatra "$STAGE/usr/bin/notepatra"

# ── .desktop file ─────────────────────────────────────────────────────
cat > "$STAGE/usr/share/applications/notepatra.desktop" <<EOF
[Desktop Entry]
Name=Notepatra
GenericName=Code Editor
Comment=$DESC_SHORT
Exec=notepatra %F
Icon=notepatra
Terminal=false
Type=Application
Categories=Development;TextEditor;IDE;
MimeType=text/plain;text/x-python;text/x-csrc;text/x-c++src;text/html;text/css;text/javascript;application/json;application/xml;
StartupWMClass=notepatra
Keywords=editor;code;notepad;notepatra;ai;ollama;
EOF

# ── icons (hicolor) ───────────────────────────────────────────────────
for SIZE in 16 24 32 48 64 128 256 512; do
    SRC="$REPO_ROOT/resources/notepatra-${SIZE}.png"
    if [ -f "$SRC" ]; then
        mkdir -p "$STAGE/usr/share/icons/hicolor/${SIZE}x${SIZE}/apps"
        install -m 0644 "$SRC" \
                "$STAGE/usr/share/icons/hicolor/${SIZE}x${SIZE}/apps/notepatra.png"
    fi
done
# 1024 goes under the non-standard but widely-accepted /scalable/ slot
if [ -f "$REPO_ROOT/resources/notepatra-1024.png" ]; then
    mkdir -p "$STAGE/usr/share/icons/hicolor/scalable/apps"
    install -m 0644 "$REPO_ROOT/resources/notepatra-1024.png" \
            "$STAGE/usr/share/icons/hicolor/scalable/apps/notepatra.png"
fi

# ── DEBIAN/control ────────────────────────────────────────────────────
# Installed-Size is in kB and counts only payload (not control files).
INSTALLED_SIZE=$(du -sk --apparent-size "$STAGE/opt" "$STAGE/usr" 2>/dev/null \
                  | awk 'BEGIN{s=0}{s+=$1}END{print s}')

# Cross-flavor conflict — only one of the two can be installed at a time.
case "$FLAVOR" in
    regular)  CONFLICTS_WITH="notepatra-local-ai" ;;
    local-ai) CONFLICTS_WITH="notepatra" ;;
esac

{
cat <<EOF
Package: $PKG
Version: $VERSION
Section: editors
Priority: optional
Architecture: $ARCH
Maintainer: Prateek Singh <hi@notepatra.org>
Installed-Size: $INSTALLED_SIZE
Provides: notepatra-editor
Conflicts: $CONFLICTS_WITH
Replaces: $CONFLICTS_WITH
Depends: libqt5core5a (>= 5.12), libqt5gui5 (>= 5.12), libqt5widgets5 (>= 5.12), libqt5network5 (>= 5.12), libqt5printsupport5 (>= 5.12), libqt5sql5 (>= 5.12), libqt5sql5-sqlite (>= 5.12), libqt5concurrent5 (>= 5.12), libqt5charts5 (>= 5.12), libstdc++6, libc6
Recommends: libqt5webengine5, libqt5webenginewidgets5
Suggests: ollama
Homepage: https://notepatra.org
Description: $DESC_SHORT
 Notepatra is a native C++/Rust code editor inspired by Notepad++,
 rebuilt for the AI era.  Local AI integration via Ollama and llama.cpp
 ships out of the box; an OpenAI-compatible URL field is supported for
 self-hosted servers (LM Studio, Jan, vLLM, text-generation-webui).
EOF
if [ "$FLAVOR" = "local-ai" ]; then
cat <<EOF
 .
 This is the CLOUD-FREE flavor: the binary physically refuses to talk
 to any non-private network destination.  All public LLM endpoints
 (OpenAI, Anthropic, Mistral, Gemini, OpenRouter, ...) are blocked at
 the network layer, and the UI strips the cloud-URL paste box.  Ollama,
 llama.cpp, LM Studio, Jan, and self-hosted servers on the LAN
 continue to work.  Pick this build for regulated industries, air-
 gapped fleets, or data-sovereignty compliance.
EOF
fi
} > "$STAGE/DEBIAN/control"

# ── DEBIAN maintainer scripts ─────────────────────────────────────────
cat > "$STAGE/DEBIAN/postinst" <<'POSTINST'
#!/bin/sh
set -e
if command -v update-desktop-database >/dev/null 2>&1; then
    update-desktop-database -q || true
fi
if command -v gtk-update-icon-cache >/dev/null 2>&1; then
    gtk-update-icon-cache -q -f -t /usr/share/icons/hicolor 2>/dev/null || true
fi
if command -v update-mime-database >/dev/null 2>&1; then
    update-mime-database /usr/share/mime 2>/dev/null || true
fi
exit 0
POSTINST
chmod 0755 "$STAGE/DEBIAN/postinst"

cat > "$STAGE/DEBIAN/postrm" <<'POSTRM'
#!/bin/sh
set -e
case "$1" in
    remove|purge|upgrade|disappear|abort-install|abort-upgrade|failed-upgrade)
        if command -v update-desktop-database >/dev/null 2>&1; then
            update-desktop-database -q || true
        fi
        if command -v gtk-update-icon-cache >/dev/null 2>&1; then
            gtk-update-icon-cache -q -f -t /usr/share/icons/hicolor 2>/dev/null || true
        fi
        ;;
esac
exit 0
POSTRM
chmod 0755 "$STAGE/DEBIAN/postrm"

# ── copyright (DEP-5) ─────────────────────────────────────────────────
cat > "$STAGE/usr/share/doc/$PKG/copyright" <<EOF
Format: https://www.debian.org/doc/packaging-manuals/copyright-format/1.0/
Upstream-Name: Notepatra
Upstream-Contact: Prateek Singh <hi@notepatra.org>
Source: https://github.com/singhpratech/notepatra

Files: *
Copyright: 2025-2026 Prateek Singh
License: GPL-3.0+

License: GPL-3.0+
 This program is free software: you can redistribute it and/or modify
 it under the terms of the GNU General Public License as published by
 the Free Software Foundation, either version 3 of the License, or
 (at your option) any later version.
 .
 This program is distributed in the hope that it will be useful,
 but WITHOUT ANY WARRANTY; without even the implied warranty of
 MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 GNU General Public License for more details.
 .
 You should have received a copy of the GNU General Public License
 along with this program.  If not, see <http://www.gnu.org/licenses/>.
 .
 On Debian systems, the complete text of the GPL-3 license can be
 found in /usr/share/common-licenses/GPL-3.

Files: vendor/duckdb/*
Copyright: 2018-2026 DuckDB Foundation
License: MIT
EOF

# ── changelog.Debian.gz ───────────────────────────────────────────────
{
    echo "$PKG ($VERSION) stable; urgency=medium"
    echo
    echo "  * Notepatra v$VERSION ($FLAVOR flavor)"
    echo
    echo " -- Prateek Singh <hi@notepatra.org>  $(date -R)"
} > "$STAGE/usr/share/doc/$PKG/changelog.Debian"
gzip -9n "$STAGE/usr/share/doc/$PKG/changelog.Debian"

# ── build the .deb ────────────────────────────────────────────────────
OUTPUT_DEB="${PKG}_${VERSION}_${ARCH}.deb"

# fakeroot is what lets non-root users emit a .deb whose payload claims
# to be owned by root:root (which is what dpkg expects for system files).
# --root-owner-group is the modern equivalent that doesn't need fakeroot,
# but we keep fakeroot for older toolchains.
if command -v fakeroot >/dev/null 2>&1; then
    fakeroot dpkg-deb --build --root-owner-group "$STAGE" "$OUTPUT_DEB"
else
    dpkg-deb --build --root-owner-group "$STAGE" "$OUTPUT_DEB"
fi

echo ""
echo "=== Built: $OUTPUT_DEB ==="
ls -lh "$OUTPUT_DEB"
echo ""
echo "=== Control ==="
# `|| true` guards: these are diagnostic prints; the pipe-into-head will
# trigger SIGPIPE on dpkg-deb under `set -o pipefail` once head -25
# closes the pipe.  We do NOT want that to fail the whole release CI.
dpkg-deb --info "$OUTPUT_DEB" 2>&1 | sed -n '1,30p' || true
echo ""
echo "=== Top 25 files ==="
dpkg-deb --contents "$OUTPUT_DEB" 2>&1 | head -25 || true
