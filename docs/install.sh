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
    umask 077
    TMPDIR=$(mktemp -d)
    case "$RELEASE_URL" in
        *.tar.gz) PKG_PATH="$TMPDIR/notepatra.tar.gz" ;;
        *.dmg) PKG_PATH="$TMPDIR/notepatra.dmg" ;;
        *) PKG_PATH="$TMPDIR/notepatra.pkg" ;;
    esac
    MOUNT_POINT=""
    curl --proto '=https' --tlsv1.2 -fsSL "$RELEASE_URL" -o "$PKG_PATH"
    # SHA-256 verification — HARD REQUIRED. Refuse install if checksums unreachable
    # or the artifact is not listed in them. (Previously soft-fail: an attacker who
    # could MITM SHA256SUMS while letting the binary through bypassed verification.)
    if ! curl --proto '=https' --tlsv1.2 -fsSL "$SHA_URL" -o "$TMPDIR/SHA256SUMS" 2>/dev/null \
       && ! curl --proto '=https' --tlsv1.2 -fsSL "$SHA_FALLBACK_URL" -o "$TMPDIR/SHA256SUMS" 2>/dev/null; then
        echo ""
        echo "  ❌ Could not fetch SHA256SUMS for release $RELEASE_TAG"
        echo "     Tried: $SHA_URL"
        echo "     Tried: $SHA_FALLBACK_URL"
        echo "  Refusing to install an unverified binary."
        echo ""
        rm -rf "$TMPDIR"
        exit 1
    fi
    EXPECTED=$(grep "$ARTIFACT" "$TMPDIR/SHA256SUMS" | awk '{print $1}' | head -1)
    if [ -z "$EXPECTED" ]; then
        echo ""
        echo "  ❌ Artifact '$ARTIFACT' is not listed in SHA256SUMS for $RELEASE_TAG"
        echo "  Refusing to install an unverified binary."
        echo ""
        rm -rf "$TMPDIR"
        exit 1
    fi
    verify_sha256 "$PKG_PATH" "$EXPECTED"
    # Optional cosign verification — strong gate if cosign is on PATH
    if command -v cosign >/dev/null 2>&1; then
        if curl --proto '=https' --tlsv1.2 -fsSL "${RELEASE_URL}.sig" -o "$PKG_PATH.sig" 2>/dev/null \
           && curl --proto '=https' --tlsv1.2 -fsSL "${RELEASE_URL}.pem" -o "$PKG_PATH.pem" 2>/dev/null; then
            if cosign verify-blob \
                --certificate "$PKG_PATH.pem" \
                --signature "$PKG_PATH.sig" \
                --certificate-identity-regexp "^https://github.com/$REPO/\\.github/workflows/.+@refs/tags/$RELEASE_TAG$" \
                --certificate-oidc-issuer "https://token.actions.githubusercontent.com" \
                "$PKG_PATH" >/dev/null 2>&1; then
                echo "  ✓ Cosign signature verified"
            else
                echo ""
                echo "  ❌ Cosign signature verification failed for $RELEASE_TAG"
                echo "  Refusing to install."
                echo ""
                rm -rf "$TMPDIR"
                exit 1
            fi
        else
            echo "  ⚠ Cosign present but signature files unavailable — falling back to SHA-only"
        fi
    fi
    cleanup_macos_installer() {
        if [ -n "$MOUNT_POINT" ] && [ -d "$MOUNT_POINT" ]; then
            hdiutil detach "$MOUNT_POINT" -quiet >/dev/null 2>&1 || true
        fi
        rm -rf "$TMPDIR"
    }
    trap cleanup_macos_installer EXIT

    # Pick an install target. Prefer /Applications (what Mac users expect),
    # fall back to ~/Applications (first-class macOS user-local location) if
    # /Applications is not writable AND sudo is not available. This lets the
    # installer succeed on both multi-user machines (admin → sudo prompt) and
    # locked-down corp laptops (no admin → clean user-local install).
    APP_DIR="/Applications"
    SUDO=""
    if [ ! -w "$APP_DIR" ]; then
        if command -v sudo >/dev/null 2>&1 && sudo -n true 2>/dev/null; then
            SUDO="sudo"
        elif command -v sudo >/dev/null 2>&1; then
            # Try interactive sudo — curl|sh still has /dev/tty even with
            # piped stdin, so sudo can prompt for a password there.
            echo "  /Applications requires admin access — prompting for sudo..."
            if sudo -v </dev/tty 2>/dev/null; then
                SUDO="sudo"
            else
                echo "  sudo not available — falling back to ~/Applications"
                APP_DIR="$HOME/Applications"
                mkdir -p "$APP_DIR"
            fi
        else
            APP_DIR="$HOME/Applications"
            mkdir -p "$APP_DIR"
        fi
    fi
    APP_PATH="$APP_DIR/Notepatra.app"

    case "$RELEASE_URL" in
        *.dmg)
            echo "  Mounting disk image..."
            MOUNT_POINT=$(hdiutil attach "$PKG_PATH" -nobrowse -readonly | awk '/\/Volumes\// {print substr($0, index($0, "/Volumes/"))}' | tail -1)
            if [ -z "$MOUNT_POINT" ] || [ ! -d "$MOUNT_POINT/Notepatra.app" ]; then
                echo "  ❌ Could not mount Notepatra disk image or find Notepatra.app"
                exit 1
            fi
            echo "  Installing Notepatra.app to $APP_DIR..."
            $SUDO rm -rf "$APP_PATH" 2>/dev/null
            $SUDO ditto "$MOUNT_POINT/Notepatra.app" "$APP_PATH"
            ;;
        *.tar.gz)
            cd "$TMPDIR"
            tar xzf "$PKG_PATH"
            if [ ! -d "Notepatra.app" ]; then
                echo "  ❌ Archive did not contain Notepatra.app"
                exit 1
            fi
            echo "  Installing Notepatra.app to $APP_DIR..."
            $SUDO rm -rf "$APP_PATH" 2>/dev/null
            $SUDO ditto "$TMPDIR/Notepatra.app" "$APP_PATH"
            ;;
        *)
            echo "  ❌ Unsupported macOS package format: $RELEASE_URL"
            exit 1
            ;;
    esac

    # ─── macOS Gatekeeper / Tahoe workaround ─────────────────────────────
    # Notepatra is an unsigned-by-apple open-source GPL project. We do NOT
    # pay $99/year for a Developer ID, so the bundle ships with an ad-hoc
    # signature + embedded entitlements. Modern macOS (especially Tahoe 26)
    # will otherwise silently refuse to launch it on double-click.
    #
    # This section does the bulletproof clean-up:
    #   1. Strip ALL extended attributes, not just com.apple.quarantine.
    #      Tahoe sometimes adds com.apple.provenance or com.apple.macl
    #      which also trip Gatekeeper.
    #   2. Re-sign ad-hoc with hardened runtime + our entitlements plist
    #      (embedded in the bundle at Contents/Resources/entitlements.plist
    #      by the build). This satisfies Tahoe's launch-time probe.
    #   3. Warm up launchd with `open` so the first run through Finder
    #      doesn't get silently denied.
    echo "  Clearing ALL xattrs + re-signing bundle for Gatekeeper..."
    $SUDO xattr -cr "$APP_PATH" 2>/dev/null || true
    $SUDO xattr -dr com.apple.quarantine "$APP_PATH" 2>/dev/null || true
    $SUDO xattr -dr com.apple.provenance "$APP_PATH" 2>/dev/null || true
    $SUDO xattr -dr com.apple.macl "$APP_PATH" 2>/dev/null || true

    # Prefer bundled entitlements if our installer shipped them
    ENTITLEMENTS_IN_BUNDLE="$APP_PATH/Contents/Resources/entitlements.plist"
    if [ -f "$ENTITLEMENTS_IN_BUNDLE" ]; then
        $SUDO codesign --force --deep --sign - \
            --options runtime \
            --entitlements "$ENTITLEMENTS_IN_BUNDLE" \
            --timestamp=none \
            "$APP_PATH" 2>/dev/null || \
        $SUDO codesign --force --deep --sign - "$APP_PATH" 2>/dev/null || true
    else
        $SUDO codesign --force --deep --sign - "$APP_PATH" 2>/dev/null || true
    fi

    # Verify the signature — if codesign -v fails, bundle is broken and
    # Tahoe will never launch it no matter what the user does.
    if ! codesign -v "$APP_PATH" 2>/dev/null; then
        echo "  ⚠ codesign verification failed — trying deeper re-sign..."
        $SUDO find "$APP_PATH/Contents/Frameworks" -type f \
            \( -name "*.dylib" -o -name "*.so" \) 2>/dev/null | \
            while read -r lib; do
                $SUDO codesign --force --sign - --timestamp=none "$lib" 2>/dev/null || true
            done
        $SUDO codesign --force --deep --sign - "$APP_PATH" 2>/dev/null || true
    fi

    # Symlink the CLI binary so `notepatra` works from any terminal
    if [ -x "$APP_PATH/Contents/MacOS/notepatra" ]; then
        mkdir -p "$INSTALL_DIR"
        ln -sf "$APP_PATH/Contents/MacOS/notepatra" "$INSTALL_DIR/notepatra"
        if ! echo "$PATH" | grep -q "$INSTALL_DIR"; then
            SHELL_RC="$HOME/.zshrc"
            [ -f "$HOME/.bash_profile" ] && SHELL_RC="$HOME/.bash_profile"
            echo "export PATH=\"$INSTALL_DIR:\$PATH\"" >> "$SHELL_RC"
            echo "  Added $INSTALL_DIR to PATH in $(basename $SHELL_RC)"
        fi
    fi

    # Detect macOS major version to tailor the instructions. Tahoe needs
    # a specific right-click → Open dance that older macOS didn't.
    MACOS_VER=$(sw_vers -productVersion 2>/dev/null | cut -d. -f1)
    echo ""
    echo "  ✅ Installed to $APP_PATH"
    echo ""

    if [ "$MACOS_VER" -ge 26 ] 2>/dev/null; then
        # macOS Tahoe or newer — strict Gatekeeper
        echo "  🔒 macOS Tahoe detected. First-launch requires one extra step:"
        echo ""
        echo "     METHOD A (easiest — opens it right now):"
        echo "       open \"$APP_PATH\""
        echo ""
        echo "     METHOD B (Finder):"
        echo "       1. Open Finder → Applications"
        echo "       2. RIGHT-click Notepatra → Open"
        echo "       3. Click 'Open' in the warning dialog"
        echo "       After this, double-click works normally."
        echo ""
        echo "     METHOD C (if both fail — nuke Gatekeeper for this app only):"
        echo "       sudo spctl --add \"$APP_PATH\""
        echo "       sudo xattr -cr \"$APP_PATH\""
    elif [ "$MACOS_VER" -ge 15 ] 2>/dev/null; then
        # Sequoia
        echo "  Open with:  open \"$APP_PATH\""
        echo "  Or: right-click Notepatra.app → Open → Open"
        echo ""
        echo "  CLI:  notepatra  (open a new terminal first)"
    else
        echo "  Open from Launchpad, Finder, or run:"
        echo "     open \"$APP_PATH\""
        echo "     notepatra      # CLI shortcut (open a new terminal first)"
    fi
    echo ""
    echo "  💡 Why this extra step? Apple charges \$99/yr for 'Developer ID'"
    echo "     code signing + notarization. Notepatra is free GPL software"
    echo "     and doesn't pay Apple that tax. You only do this ONCE per"
    echo "     install."
else
    # Linux — download binary
    umask 077
    TMPDIR=$(mktemp -d)
    curl --proto '=https' --tlsv1.2 -fsSL "$RELEASE_URL" -o "$TMPDIR/notepatra.tar.gz"
    # SHA-256 verification — HARD REQUIRED. (See macOS block above for rationale.)
    if ! curl --proto '=https' --tlsv1.2 -fsSL "$SHA_URL" -o "$TMPDIR/SHA256SUMS" 2>/dev/null \
       && ! curl --proto '=https' --tlsv1.2 -fsSL "$SHA_FALLBACK_URL" -o "$TMPDIR/SHA256SUMS" 2>/dev/null; then
        echo ""
        echo "  ❌ Could not fetch SHA256SUMS for release $RELEASE_TAG"
        echo "     Tried: $SHA_URL"
        echo "     Tried: $SHA_FALLBACK_URL"
        echo "  Refusing to install an unverified binary."
        echo ""
        rm -rf "$TMPDIR"
        exit 1
    fi
    EXPECTED=$(grep "$ARTIFACT" "$TMPDIR/SHA256SUMS" | awk '{print $1}' | head -1)
    if [ -z "$EXPECTED" ]; then
        echo ""
        echo "  ❌ Artifact '$ARTIFACT' is not listed in SHA256SUMS for $RELEASE_TAG"
        echo "  Refusing to install an unverified binary."
        echo ""
        rm -rf "$TMPDIR"
        exit 1
    fi
    verify_sha256 "$TMPDIR/notepatra.tar.gz" "$EXPECTED"
    # Optional cosign verification — strong gate if cosign is on PATH
    if command -v cosign >/dev/null 2>&1; then
        if curl --proto '=https' --tlsv1.2 -fsSL "${RELEASE_URL}.sig" -o "$TMPDIR/notepatra.tar.gz.sig" 2>/dev/null \
           && curl --proto '=https' --tlsv1.2 -fsSL "${RELEASE_URL}.pem" -o "$TMPDIR/notepatra.tar.gz.pem" 2>/dev/null; then
            if cosign verify-blob \
                --certificate "$TMPDIR/notepatra.tar.gz.pem" \
                --signature "$TMPDIR/notepatra.tar.gz.sig" \
                --certificate-identity-regexp "^https://github.com/$REPO/\\.github/workflows/.+@refs/tags/$RELEASE_TAG$" \
                --certificate-oidc-issuer "https://token.actions.githubusercontent.com" \
                "$TMPDIR/notepatra.tar.gz" >/dev/null 2>&1; then
                echo "  ✓ Cosign signature verified"
            else
                echo ""
                echo "  ❌ Cosign signature verification failed for $RELEASE_TAG"
                echo "  Refusing to install."
                echo ""
                rm -rf "$TMPDIR"
                exit 1
            fi
        else
            echo "  ⚠ Cosign present but signature files unavailable — falling back to SHA-only"
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
# StartupNotify=false because Notepatra is a single-instance app: when a
# second invocation forwards its file path via IPC and exits, no new window
# is mapped, so the WM's launch-feedback spinner would tick until timeout.
# Spawn-and-IPC is fast (<150ms) so users don't miss the spinner.
StartupNotify=false
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
