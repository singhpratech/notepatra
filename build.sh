#!/usr/bin/env bash
#
# Cross-platform build script for Notepatra
# Detects the current OS and builds accordingly.
#
set -euo pipefail

PROJECT_DIR="$(cd "$(dirname "$0")" && pwd)"
BUILD_DIR="$PROJECT_DIR/build"
JOBS="${JOBS:-$(nproc 2>/dev/null || sysctl -n hw.ncpu 2>/dev/null || echo 4)}"

# ── Detect platform ──
detect_platform() {
    case "$(uname -s)" in
        Linux*)   PLATFORM="linux"   ;;
        Darwin*)  PLATFORM="macos"   ;;
        MINGW*|MSYS*|CYGWIN*) PLATFORM="windows" ;;
        *)        PLATFORM="unknown" ;;
    esac
    echo "Detected platform: $PLATFORM"
}

# ── Check dependencies ──
check_deps() {
    local missing=()

    command -v cmake  >/dev/null 2>&1 || missing+=("cmake")
    command -v cargo  >/dev/null 2>&1 || missing+=("cargo (Rust)")

    if [ "$PLATFORM" = "linux" ]; then
        command -v qmake >/dev/null 2>&1 || command -v qmake-qt5 >/dev/null 2>&1 || missing+=("qt5 (qmake)")
    elif [ "$PLATFORM" = "macos" ]; then
        command -v qmake >/dev/null 2>&1 || missing+=("qt5 (brew install qt@5)")
    elif [ "$PLATFORM" = "windows" ]; then
        command -v qmake >/dev/null 2>&1 || missing+=("qt5")
    fi

    if [ ${#missing[@]} -gt 0 ]; then
        echo "ERROR: Missing dependencies: ${missing[*]}"
        echo ""
        echo "Install instructions:"
        case "$PLATFORM" in
            linux)
                echo "  sudo apt install cmake build-essential qt5-qmake qtbase5-dev libqscintilla2-qt5-dev"
                echo "  curl --proto '=https' --tlsv1.2 -sSf https://sh.rustup.rs | sh"
                ;;
            macos)
                echo "  brew install cmake qt@5 qscintilla2"
                echo "  curl --proto '=https' --tlsv1.2 -sSf https://sh.rustup.rs | sh"
                ;;
            windows)
                echo "  Install Qt5 from qt.io, Rust from rustup.rs, CMake from cmake.org"
                echo "  Ensure all are on PATH"
                ;;
        esac
        exit 1
    fi
    echo "All dependencies found."
}

# ── Build Rust core ──
build_rust() {
    echo ""
    echo "=== Building Rust core library ==="
    cd "$PROJECT_DIR/rust-core"
    cargo build --release
    cd "$PROJECT_DIR"
}

# ── Build C++ / Qt ──
build_cpp() {
    echo ""
    echo "=== Building C++/Qt application ==="

    mkdir -p "$BUILD_DIR"
    cd "$BUILD_DIR"

    local cmake_args=(-DCMAKE_BUILD_TYPE=Release)

    case "$PLATFORM" in
        macos)
            # Homebrew Qt5 path
            local qt5_dir
            qt5_dir="$(brew --prefix qt@5 2>/dev/null || echo "/usr/local/opt/qt@5")"
            if [ -d "$qt5_dir" ]; then
                cmake_args+=("-DCMAKE_PREFIX_PATH=$qt5_dir")
            fi
            ;;
        windows)
            # Use Ninja if available for faster builds on Windows
            if command -v ninja >/dev/null 2>&1; then
                cmake_args+=(-G "Ninja")
            fi
            ;;
    esac

    cmake "${cmake_args[@]}" "$PROJECT_DIR"
    cmake --build . --parallel "$JOBS"

    cd "$PROJECT_DIR"
}

# ── Print result ──
print_result() {
    echo ""
    echo "=== Build complete ==="
    case "$PLATFORM" in
        linux)
            echo "Binary: $BUILD_DIR/notepatra"
            echo "Run:    $BUILD_DIR/notepatra"
            ;;
        macos)
            echo "App:    $BUILD_DIR/Notepatra.app"
            echo "Run:    open $BUILD_DIR/Notepatra.app"
            ;;
        windows)
            echo "Binary: $BUILD_DIR/Release/notepatra.exe (or $BUILD_DIR/notepatra.exe)"
            echo "Run:    $BUILD_DIR/notepatra.exe"
            ;;
    esac
}

# ── Main ──
main() {
    echo "==============================="
    echo "  Notepatra Build Script"
    echo "==============================="

    detect_platform

    if [ "$PLATFORM" = "unknown" ]; then
        echo "ERROR: Unsupported platform: $(uname -s)"
        exit 1
    fi

    check_deps
    build_rust
    build_cpp
    print_result
}

main "$@"
