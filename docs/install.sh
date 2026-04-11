#!/bin/bash
# Notepatra Installer — https://notepatra.org
# Usage: curl -fsSL https://notepatra.org/install.sh | sh

set -e

REPO="singhpratech/notepatra"
INSTALL_DIR="$HOME/.local/bin"
APP_NAME="notepatra"

echo ""
echo "  ╔══════════════════════════════════════╗"
echo "  ║         Notepatra Installer          ║"
echo "  ║   The code editor for the AI era     ║"
echo "  ╚══════════════════════════════════════╝"
echo ""

# Detect OS and arch
OS=$(uname -s | tr '[:upper:]' '[:lower:]')
ARCH=$(uname -m)

case "$OS" in
    linux)
        case "$ARCH" in
            x86_64) ARTIFACT="notepatra-linux-x64" ;;
            aarch64|arm64) ARTIFACT="notepatra-linux-arm64" ;;
            *) echo "Unsupported architecture: $ARCH"; exit 1 ;;
        esac
        ;;
    darwin)
        case "$ARCH" in
            arm64) ARTIFACT="notepatra-macos-arm64" ;;
            x86_64) ARTIFACT="notepatra-macos-x64" ;;
            *) echo "Unsupported architecture: $ARCH"; exit 1 ;;
        esac
        ;;
    *)
        echo "Unsupported OS: $OS"
        echo "For Windows, download from: https://github.com/$REPO/releases"
        exit 1
        ;;
esac

echo "  OS:   $OS ($ARCH)"
echo "  File: $ARTIFACT"
echo ""

# Get latest release URL
echo "  Fetching latest release..."
RELEASE_URL=$(curl -sL "https://api.github.com/repos/$REPO/releases/latest" | \
    grep "browser_download_url.*${ARTIFACT}" | \
    head -1 | cut -d '"' -f 4)

if [ -z "$RELEASE_URL" ]; then
    echo ""
    echo "  No release found. Downloading from latest build..."
    echo ""
    echo "  Building from source instead:"
    echo ""
    echo "    # Dependencies"
    if [ "$OS" = "linux" ]; then
        echo "    sudo apt install cmake qtbase5-dev libqscintilla2-qt5-dev"
    elif [ "$OS" = "darwin" ]; then
        echo "    brew install qt@5 cmake"
    fi
    echo "    curl --proto '=https' --tlsv1.2 -sSf https://sh.rustup.rs | sh"
    echo "    source ~/.cargo/env"
    echo ""
    echo "    # Build"
    echo "    git clone https://github.com/$REPO.git"
    echo "    cd notepatra"
    echo "    cd rust-core && cargo build --release && cd .."
    echo "    mkdir build && cd build && cmake .. && make -j\$(nproc)"
    echo "    ./notepatra"
    echo ""
    exit 0
fi

echo "  Downloading: $RELEASE_URL"

# Helper: SHA-256 verification
verify_sha256() {
    local file="$1"
    local expected="$2"
    local actual=""
    if command -v sha256sum >/dev/null 2>&1; then
        actual=$(sha256sum "$file" | awk '{print $1}')
    elif command -v shasum >/dev/null 2>&1; then
        actual=$(shasum -a 256 "$file" | awk '{print $1}')
    else
        echo "  ⚠ No sha256sum or shasum available — skipping checksum verification"
        return 0
    fi
    if [ "$actual" != "$expected" ]; then
        echo ""
        echo "  ❌ SHA-256 mismatch — refusing to install."
        echo "     expected: $expected"
        echo "     actual:   $actual"
        echo "     file:     $file"
        echo ""
        echo "  This means the download was corrupted, MITM'd, or the release was tampered with."
        echo "  Report at: https://github.com/$REPO/issues/new"
        exit 1
    fi
    echo "  ✓ SHA-256 verified"
}

# Fetch SHA256SUMS for the same release tag (sibling of $RELEASE_URL)
RELEASE_TAG=$(echo "$RELEASE_URL" | sed -E 's|.*/download/([^/]+)/.*|\1|')
SHA_URL="https://github.com/$REPO/releases/download/$RELEASE_TAG/SHA256SUMS"
SHA_FALLBACK_URL="https://notepatra.org/SHA256SUMS.${RELEASE_TAG}.txt"

# Create install directory
mkdir -p "$INSTALL_DIR"

if [ "$OS" = "darwin" ]; then
    # macOS — download and mount DMG or extract app
    TMPDIR=$(mktemp -d)
    curl -sL "$RELEASE_URL" -o "$TMPDIR/notepatra.tar.gz"
    # Verify SHA-256 if checksums file is available
    if curl -fsSL "$SHA_URL" -o "$TMPDIR/SHA256SUMS" 2>/dev/null \
       || curl -fsSL "$SHA_FALLBACK_URL" -o "$TMPDIR/SHA256SUMS" 2>/dev/null; then
        EXPECTED=$(grep "$ARTIFACT" "$TMPDIR/SHA256SUMS" | awk '{print $1}' | head -1)
        if [ -n "$EXPECTED" ]; then
            verify_sha256 "$TMPDIR/notepatra.tar.gz" "$EXPECTED"
        fi
    fi
    cd "$TMPDIR"
    tar xzf notepatra.tar.gz
    if [ -d "Notepatra.app" ]; then
        echo "  Installing Notepatra.app to /Applications..."
        cp -R Notepatra.app /Applications/
        echo ""
        echo "  ✅ Installed! Open from Applications or run:"
        echo "     open /Applications/Notepatra.app"
    fi
    rm -rf "$TMPDIR"
else
    # Linux — download binary
    TMPDIR=$(mktemp -d)
    curl -sL "$RELEASE_URL" -o "$TMPDIR/notepatra.tar.gz"
    # Verify SHA-256 if checksums file is available
    if curl -fsSL "$SHA_URL" -o "$TMPDIR/SHA256SUMS" 2>/dev/null \
       || curl -fsSL "$SHA_FALLBACK_URL" -o "$TMPDIR/SHA256SUMS" 2>/dev/null; then
        EXPECTED=$(grep "$ARTIFACT" "$TMPDIR/SHA256SUMS" | awk '{print $1}' | head -1)
        if [ -n "$EXPECTED" ]; then
            verify_sha256 "$TMPDIR/notepatra.tar.gz" "$EXPECTED"
        fi
    fi
    cd "$TMPDIR"
    tar xzf notepatra.tar.gz
    chmod +x notepatra
    mv notepatra "$INSTALL_DIR/"

    # Add to PATH if needed
    if ! echo "$PATH" | grep -q "$INSTALL_DIR"; then
        echo "export PATH=\"$INSTALL_DIR:\$PATH\"" >> "$HOME/.bashrc"
        echo "  Added $INSTALL_DIR to PATH in .bashrc"
    fi

    # Install icons to the standard hicolor theme so the launcher and .desktop
    # file can find them at all sizes (16/32/48/64/128/256).
    ICON_BASE_URL="https://raw.githubusercontent.com/$REPO/main/resources"
    for sz in 16 32 48 64 128 256; do
        ICON_DIR="$HOME/.local/share/icons/hicolor/${sz}x${sz}/apps"
        mkdir -p "$ICON_DIR"
        curl -fsSL "$ICON_BASE_URL/notepatra-${sz}.png" -o "$ICON_DIR/notepatra.png" 2>/dev/null || true
    done
    gtk-update-icon-cache "$HOME/.local/share/icons/hicolor" 2>/dev/null || true

    # Create desktop entry — Icon=notepatra resolves via hicolor theme search
    mkdir -p "$HOME/.local/share/applications"
    cat > "$HOME/.local/share/applications/notepatra.desktop" << EOF
[Desktop Entry]
Name=Notepatra
GenericName=Code Editor
Comment=Native C++/Rust code editor with AI-powered formatters
Exec=$INSTALL_DIR/notepatra %F
Icon=notepatra
Terminal=false
Type=Application
StartupNotify=true
StartupWMClass=Notepatra
Categories=Development;TextEditor;Utility;
MimeType=text/plain;text/x-c;text/x-c++;text/x-python;application/json;text/markdown;text/x-shellscript;
Keywords=editor;text;code;notepad;ide;
EOF
    update-desktop-database "$HOME/.local/share/applications/" 2>/dev/null || true

    rm -rf "$TMPDIR"

    echo ""
    echo "  ✅ Installed to $INSTALL_DIR/notepatra"
    echo ""
    echo "  Run:  notepatra"
    echo "  Or find 'Notepatra' in your app menu."
fi

echo ""
echo "  Envisioned by Prateek Singh. Built by Claude."
echo ""
