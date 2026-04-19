#!/bin/bash
# Notepatra Uninstaller — https://notepatra.org
# Usage:  curl -fsSL https://notepatra.org/uninstall.sh | sh
#
# Detects whether you're on macOS or Linux and removes everything that
# the matching install.sh would have created. Leaves alone:
#   - Files you edited with Notepatra
#   - Ollama and any models you pulled
#   - System Qt5 / QScintilla packages (shared with other apps)

set -e

INSTALL_DIR="$HOME/.local/bin"
APP_NAME="notepatra"

echo ""
echo "  ╔══════════════════════════════════════╗"
echo "  ║       Notepatra Uninstaller          ║"
echo "  ╚══════════════════════════════════════╝"
echo ""

OS=$(uname -s | tr '[:upper:]' '[:lower:]')

if [ "$OS" = "darwin" ]; then
    echo "  🍎 macOS detected"
    echo ""

    if [ -d /Applications/Notepatra.app ]; then
        echo "  Removing /Applications/Notepatra.app..."
        if [ -w /Applications ]; then
            rm -rf /Applications/Notepatra.app
        else
            sudo rm -rf /Applications/Notepatra.app
        fi
    else
        echo "  (no app found at /Applications/Notepatra.app)"
    fi

    if [ -L "$INSTALL_DIR/notepatra" ] || [ -f "$INSTALL_DIR/notepatra" ]; then
        echo "  Removing CLI symlink at $INSTALL_DIR/notepatra..."
        rm -f "$INSTALL_DIR/notepatra"
    fi

    echo "  Removing user config + cache..."
    rm -rf "$HOME/.config/notepatra"
    rm -rf "$HOME/Library/Preferences/com.notepatra.editor.plist"
    rm -rf "$HOME/Library/Saved Application State/com.notepatra.editor.savedState"
    rm -rf "$HOME/Library/Caches/com.notepatra.editor"

    echo ""
    echo "  ✅ Notepatra removed."
    echo ""
    echo "  Left alone (manage these yourself):"
    echo "    • Files you edited with Notepatra"
    echo "    • Ollama and any models you pulled"

elif [ "$OS" = "linux" ]; then
    echo "  🐧 Linux detected"
    echo ""

    if [ -f "$INSTALL_DIR/notepatra" ] || [ -L "$INSTALL_DIR/notepatra" ]; then
        echo "  Removing $INSTALL_DIR/notepatra..."
        rm -f "$INSTALL_DIR/notepatra"
    else
        echo "  (no binary found at $INSTALL_DIR/notepatra)"
    fi

    echo "  Removing desktop entry..."
    rm -f "$HOME/.local/share/applications/notepatra.desktop"

    echo "  Removing icons (all sizes)..."
    for sz in 16 32 48 64 128 256; do
        rm -f "$HOME/.local/share/icons/hicolor/${sz}x${sz}/apps/notepatra.png"
    done
    gtk-update-icon-cache "$HOME/.local/share/icons/hicolor" 2>/dev/null || true
    update-desktop-database "$HOME/.local/share/applications" 2>/dev/null || true

    echo "  Removing user config + cache + recovery..."
    rm -rf "$HOME/.config/notepatra"

    echo ""
    echo "  ✅ Notepatra removed."
    echo ""
    echo "  Left alone (intentional):"
    echo "    • System Qt5/QScintilla packages — shared with other apps"
    echo "    • Files you edited with Notepatra"
    echo "    • Ollama and any models you pulled"

else
    echo "  ❌ Unsupported OS: $OS"
    echo ""
    echo "  Windows users: Settings → Apps → Installed apps → Notepatra → Uninstall"
    echo "  Or run: irm https://notepatra.org/uninstall.ps1 | iex"
    exit 1
fi

echo ""
