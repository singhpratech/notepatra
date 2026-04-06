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
            aarch64) echo "ARM Linux not yet supported. Build from source."; exit 1 ;;
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

# Create install directory
mkdir -p "$INSTALL_DIR"

if [ "$OS" = "darwin" ]; then
    # macOS — download and mount DMG or extract app
    TMPDIR=$(mktemp -d)
    curl -sL "$RELEASE_URL" -o "$TMPDIR/notepatra.tar.gz"
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
    cd "$TMPDIR"
    tar xzf notepatra.tar.gz
    chmod +x notepatra
    mv notepatra "$INSTALL_DIR/"

    # Add to PATH if needed
    if ! echo "$PATH" | grep -q "$INSTALL_DIR"; then
        echo "export PATH=\"$INSTALL_DIR:\$PATH\"" >> "$HOME/.bashrc"
        echo "  Added $INSTALL_DIR to PATH in .bashrc"
    fi

    # Create desktop entry
    mkdir -p "$HOME/.local/share/applications"
    cat > "$HOME/.local/share/applications/notepatra.desktop" << EOF
[Desktop Entry]
Name=Notepatra
Comment=Native C++/Rust code editor with AI-powered formatters
Exec=$INSTALL_DIR/notepatra %F
Icon=accessories-text-editor
Terminal=false
Type=Application
Categories=Development;TextEditor;
MimeType=text/plain;
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
