# Notepatra

**The first editor built for the AI era.** A blazing-fast native code editor for Linux, powered by C++ and Rust.

## Why Notepatra?

Notepad++ was built for 2003. VS Code was built for 2015. **Notepatra is built for 2026.**

- **4.6 MB binary** — no Electron, no Python, no runtime
- **Rust-powered core** — memory-mapped I/O, zero-copy file loading, parallel search
- **Handles 2.5 GB files** — Notepad++ chokes at 400 MB
- **60+ languages** — syntax highlighting out of the box
- **Plugin system** — drop a `.so` file and go
- **Native Linux** — no Wine, no compatibility layers

## Features

### Editor
- 60+ file types with syntax highlighting (Python, Rust, Go, C/C++, Java, JS/TS, HTML, CSS, SQL, and many more)
- Tabbed editing with close, reorder, middle-click close, double-click new tab
- Code folding, bookmarks, auto-complete, brace matching
- Line numbers, indent guides, word wrap
- Double-click word highlight (all occurrences in dark green)
- Pastel green current line indicator
- Custom scrollbars

### Built-in Terminal
Split pane terminal below the editor. `Ctrl+`` to toggle.

### Markdown Preview
Live rendered preview panel for `.md` files. `Ctrl+Shift+M` to toggle.

### Git Integration
Changed lines shown in the editor margin:
- Green = added lines
- Yellow = modified lines
- Red = deleted lines

### Hex Editor
View any file as a hex dump with offset, hex bytes, and ASCII columns.
Color-coded: null bytes, newlines, printable, non-printable.

### File Compare
Side-by-side diff view with sync scrolling.
- Compare with external file
- Compare two open tabs
- Powered by Rust Myers diff algorithm

### SQL Formatter
Format SQL with uppercase or lowercase keywords, configurable indentation.

### Advanced Find/Replace
- 5 tabs: Find, Replace, Find in Files, Mark, Go to
- 3 search modes: Normal, Extended (`\n`, `\r`, `\t`, `\xNN`), Regular expression
- Find/Replace in all opened documents
- Find in Files with directory recursion
- Mark all occurrences with visual indicators
- Search history (last 20 searches)

### Tools
- Hash: MD5, SHA-1, SHA-256, SHA-512
- Base64 encode/decode
- URL encode/decode

### Plugin System
Drop `.so` shared libraries in `~/.config/notepatra/plugins/`:

```c
extern "C" {
    const char* notepatra_plugin_name()    { return "My Plugin"; }
    const char* notepatra_plugin_run(const char* text, int len) {
        // transform text and return result
    }
}
```

Compile with: `g++ -shared -fPIC -o myplugin.so myplugin.cpp`

## Architecture

```
┌─────────────────────────────────────────┐
│           C++ Layer (Qt5 + QScintilla)  │
│  UI, menus, tabs, dialogs, editor       │
├─────────────────────────────────────────┤
│              C FFI boundary             │
├─────────────────────────────────────────┤
│           Rust Core Library             │
│  File I/O (mmap), search (Aho-Corasick)│
│  diff (Myers), SQL format, encoding,   │
│  hash, base64, text operations          │
└─────────────────────────────────────────┘
```

## Build

### Requirements
- CMake 3.16+
- Qt5 (`qtbase5-dev`, `libqt5printsupport5`)
- QScintilla (`libqscintilla2-qt5-dev`)
- Rust toolchain (`rustup`)
- g++ 13+

### Build steps
```bash
# Install dependencies (Ubuntu/Mint)
sudo apt install cmake qtbase5-dev libqscintilla2-qt5-dev

# Install Rust
curl --proto '=https' --tlsv1.2 -sSf https://sh.rustup.rs | sh

# Build
cd notepad-linux-native
source ~/.cargo/env
cd rust-core && cargo build --release && cd ..
mkdir build && cd build
cmake ..
make -j$(nproc)

# Run
./notepad-pp-linux
```

## Roadmap

See [ROADMAP.md](ROADMAP.md) for the full version plan.

### Coming in v1.2 (AI Era)
- Ollama integration (local AI assistant)
- AI autocomplete
- Prompt workbench (`.prompt` files)
- AI text transforms

### Coming in v2.0 (Connected)
- Remote file editing (SSH/SFTP)
- Live collaboration
- Visual regex builder
- REST client (`.http` files)

## License

MIT

---

*Envisioned by Prateek Singh. Inspired by Notepad++. Built by Claude.*
