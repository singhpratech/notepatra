<p align="center">
  <a href="https://notepatra.org"><img src="resources/notepatra-256.png" alt="Notepatra — click for notepatra.org" width="128" height="128"></a>
  <h1 align="center"><a href="https://notepatra.org" style="text-decoration:none;color:inherit;">Notepatra</a></h1>
  <p align="center"><em>The first code editor built for the AI era.</em></p>
  <p align="center">
    <strong>C++ + Rust</strong> · <strong>3–5 MB native executable</strong> · <strong>Zero Electron</strong> · <strong>100+ file types</strong> · <strong>Local AI formatters</strong>
  </p>
  <p align="center">
    <a href="https://notepatra.org">Website</a> ·
    <a href="https://github.com/singhpratech/notepatra/releases/latest">Download</a> ·
    <a href="#features">Features</a> ·
    <a href="#the-story">The Story</a> ·
    <a href="#install">Install</a> ·
    <a href="#plugins">Plugins</a> ·
    <a href="#ai-powered">AI</a> ·
    <a href="SECURITY.md">Security</a> ·
    <a href="CHANGELOG.md">Changelog</a>
  </p>
  <p align="center">
    <a href="https://github.com/singhpratech/notepatra/actions/workflows/build.yml"><img src="https://github.com/singhpratech/notepatra/actions/workflows/build.yml/badge.svg" alt="Build"></a>
    <a href="https://github.com/singhpratech/notepatra/actions/workflows/codeql.yml"><img src="https://github.com/singhpratech/notepatra/actions/workflows/codeql.yml/badge.svg" alt="CodeQL"></a>
    <a href="https://github.com/singhpratech/notepatra/releases/latest"><img src="https://img.shields.io/github/v/release/singhpratech/notepatra?color=39FF14&label=release" alt="Release"></a>
    <a href="https://github.com/singhpratech/notepatra/blob/main/LICENSE"><img src="https://img.shields.io/badge/license-GPL--3.0-blue" alt="License"></a>
    <a href="SECURITY.md"><img src="https://img.shields.io/badge/security-disclosure%20policy-39FF14" alt="Security"></a>
  </p>
  <p align="center">
    <a href="https://github.com/singhpratech/notepatra/releases"><img src="https://img.shields.io/github/downloads/singhpratech/notepatra/total?color=39FF14&label=total%20downloads" alt="Total downloads"></a>
    <a href="https://github.com/singhpratech/notepatra/stargazers"><img src="https://img.shields.io/github/stars/singhpratech/notepatra?style=flat&color=39FF14" alt="GitHub stars"></a>
    <a href="https://github.com/singhpratech/notepatra/network/members"><img src="https://img.shields.io/github/forks/singhpratech/notepatra?style=flat" alt="Forks"></a>
    <a href="https://github.com/singhpratech/notepatra/issues"><img src="https://img.shields.io/github/issues/singhpratech/notepatra?color=blue" alt="Open issues"></a>
    <a href="https://github.com/singhpratech/notepatra/commits/main"><img src="https://img.shields.io/github/last-commit/singhpratech/notepatra" alt="Last commit"></a>
  </p>
</p>

---

## The Story

I'm Prateek Singh. A developer who spent years on Linux watching Windows users open Notepad++ and fix things in seconds — broken JSON, messy SQL, tangled HTML — while I was stuck with Wine hacks or bloated Electron editors eating 500 MB of RAM to show a text file.

Every text editor told me to pick two: **fast**, **powerful**, or **native**. Vim is fast but cryptic. VS Code is powerful but heavy. Notepad++ is both — but it only runs on Windows.

So I built Notepatra.

Not a port. Not a wrapper. Not "Notepad++ but on Linux." Something new — **for everyone**.

I took what made Notepad++ legendary — the speed, the simplicity, the "it just works" feeling — and asked: **what would Notepad++ look like if it was built today, in 2026, when AI is part of every developer's workflow?**

The answer: a tiny native executable (3 MB on Mac/Windows, 5 MB on Linux) with a Rust-powered core, Scintilla editing engine, and local AI integration. An editor that can fix your broken JSON with regex in milliseconds — and when regex isn't enough, it asks your local AI to figure it out. No cloud. No telemetry. No subscription. Just you and your code.

Notepatra started on Linux — because that's where the gap was. But great tools shouldn't have borders. **Notepatra runs on Linux, Windows, and macOS.** Same codebase. Same features. No one gets left behind.

**Notepatra isn't trying to replace Notepad++. It's what I wish existed — on every platform.**

---

## Features

### Editor — Battle-tested basics done right
- **100+ file types** with full syntax highlighting across **44 language lexers** — Python, Rust, Go, C/C++, Java, JavaScript, TypeScript, SQL, HTML, CSS, JSON, YAML, Markdown, Bash, Fortran, VHDL, Verilog, MATLAB, LaTeX, and many more
- **Tabbed editing** — drag, reorder, middle-click close, double-click empty area for new tab
- **Tab right-click menu** — Close, Close Others, Close Left/Right, Save, Rename, Copy Full Path, Copy Filename, Copy Directory, Open Folder, Open Terminal, Read-Only toggle, **Color Tag** (7 colors + custom)
- **3 themes** — Light, Dark, Monokai (Settings > Theme)
- **Session persistence** — close Notepatra, reopen tomorrow, same files, same cursor positions, same window size
- **Crash recovery** — if Notepatra crashes (it shouldn't, but life happens), your unsaved work is recovered on next launch
- **File change detection** — someone else edits your file? Notepatra asks: reload or keep yours?
- **2 GB file support** — memory-mapped I/O via Rust, opens massive files without truncation
- **Double-click word highlight** — double-click any word, all occurrences light up in orange
- **Ctrl+B brace matching** — jump between matching `{}` `[]` `()`, highlights both braces + selects everything between
- **Macro recording** — Start Recording (Ctrl+Shift+R), Stop, Playback (Ctrl+Shift+P), Run Multiple Times, Save/Load macros
- **Code folding**, **bookmarks**, **auto-complete**, **indent guides**, **line numbers**
- **Custom scrollbars** — clean, modern, rounded

### Search — Find anything, anywhere
- **5-tab Find/Replace dialog** — Find, Replace, Find in Files, Mark, Go to
- **3 search modes** — Normal, Extended (`\n`, `\r`, `\t`, `\xNN`), Regular expression
- **Find All in Current Document** — results appear in bottom panel, double-click any result to jump to that exact line
- **Find All in All Opened Documents** — search across every open tab at once
- **Replace All in All Opened Documents** — one click, every file updated
- **Find in Files** — search entire directories recursively with file filters
- **Mark All** — highlight every occurrence with visual indicators
- **Aho-Corasick search engine** (Rust) — faster than regex for literal patterns

### Plugins — The real power

Every plugin opens in its own tab. Real UI, not just a menu click.

#### JSON Tools (inbuilt)
| Button | What it does |
|---|---|
| **Format** | Pretty-print with preserved key order |
| **Minify** | Compact to one line |
| **Fix + Format** | Rust-powered auto-repair: fixes missing braces, trailing commas, single quotes, unquoted keys, missing `{` after `[` — shows detailed report of every fix |
| **AI Fix (Ollama)** | Sends truly broken JSON to your local AI — fixes what regex can't |

#### HTML Tools (inbuilt)
| Button | What it does |
|---|---|
| **Format (2/4 spaces)** | Proper HTML indentation |
| **Minify** | Strip all whitespace |
| **Fix + Format** | Auto-close unclosed tags, detect missing closers, report issues |
| **AI Fix (Ollama)** | AI repairs broken nesting, malformed tags, missing attributes |

#### Bracket Tools (inbuilt)
| Button | What it does |
|---|---|
| **Check** | Detailed report: line numbers of every mismatched `()` `[]` `{}`, keyword mismatches (`begin`/`end`, `if`/`fi`) |
| **Auto-Fix** | Adds missing closers in correct nesting order |
| **AI Fix (Ollama)** | AI understands your code structure and fixes all bracket issues |

#### SQL Formatter (inbuilt)
- Format with **UPPERCASE** or **lowercase** keywords
- Configurable indent width
- Supports T-SQL, PL/SQL, MySQL, PostgreSQL, SQLite

#### Compare / Diff (inbuilt)
- Pick **any two tabs** or **any tab vs file on disk**
- Side-by-side **Scintilla editors** with syntax highlighting
- `+` green for added, `-` red for deleted
- **Prev/Next diff** navigation
- **Ignore whitespace**, **ignore case**, **ignore empty lines** checkboxes
- Powered by Rust Myers diff algorithm

#### Git Integration (inbuilt)
- **Changed files panel** — shows added, modified, deleted files with colors
- **Branch name** display
- **Push / Pull / Refresh** buttons
- **Open on GitHub** — opens your repo in browser
- **Git gutter** — green/yellow/red markers in editor margin for changed lines

### AI Powered — Local, private, no cloud

Every AI feature uses **Ollama** running on YOUR machine. Nothing leaves your computer. No API key needed. No subscription.

**Ollama Status Bar** in every AI tool shows:
- 🟢 Green dot = Ollama running, model ready
- 🔴 Red dot = not running, shows setup steps
- **Model selector dropdown** — pick any installed model (qwen3.5:9b, gemma4:e4b, llama3.2, codellama, mistral, starcoder2)

#### AI Assistant (Ctrl+Shift+A)
Opens as a tab. Select code, then:
- **Explain** — what does this code do?
- **Find Bugs** — spots issues, suggests fixes
- **Refactor** — cleaner, more readable code
- **Write Tests** — generates unit tests
- **Add Comments** — annotates your code
- **Generate Docs** — adds docstrings/JSDoc
- **Optimize** — performance improvements
- **Translate** — convert between languages
- **Custom prompt** — ask anything

Click **"Insert at Cursor"** or **"Replace Selection"** to use the AI output.

#### AI Setup
```bash
# 1. Install Ollama
curl -fsSL https://ollama.com/install.sh | sh

# 2. Pull a model
ollama pull qwen3.5:9b

# 3. Start the server
ollama serve
```
That's it. Every AI feature in Notepatra now works.

### More Features

| Feature | Shortcut |
|---|---|
| **Built-in Terminal** | `Ctrl+`` — opens as a tab, runs real commands |
| **REST Client** | `Ctrl+Shift+R` — send HTTP requests, see responses with pretty JSON |
| **Hex Editor** | View > Hex Editor — color-coded hex dump of any binary file |
| **Markdown Converter** | Features > Markdown — convert selection to table, list, code block, bold, link, heading, or strip HTML to markdown |
| **File Explorer** | `Ctrl+Shift+E` — tree view sidebar |
| **Function List** | View > Function List — lists all functions/classes, double-click to navigate |
| **Preferences** | Settings > Preferences — 6 tabs of configuration |

### Keyboard Shortcuts

| Category | Shortcut | Action |
|---|---|---|
| **File** | `Ctrl+N` | New |
| | `Ctrl+O` | Open |
| | `Ctrl+S` | Save |
| | `Ctrl+W` | Close tab |
| **Edit** | `Ctrl+D` | Duplicate line |
| | `Ctrl+Shift+K` | Delete line |
| | `Ctrl+/` | Toggle comment |
| | `Ctrl+Shift+U` | UPPERCASE |
| | `Ctrl+U` | lowercase |
| **Search** | `Ctrl+F` | Find |
| | `Ctrl+H` | Replace |
| | `F3` / `Shift+F3` | Find Next / Previous |
| | `Ctrl+G` | Go to line |
| | `Ctrl+B` | Go to matching brace |
| | `Ctrl+F2` / `F2` | Toggle / Next bookmark |
| **View** | `F11` | Full screen |
| | `Ctrl+=` / `Ctrl+-` | Zoom in / out |
| | `Alt+0` | Fold all |
| **Macro** | `Ctrl+Shift+R` | Start recording |
| | `Ctrl+Shift+T` | Stop recording |
| | `Ctrl+Shift+P` | Playback |
| **Features** | `Ctrl+`` | Terminal |
| | `Ctrl+Shift+A` | AI Assistant |
| | `Ctrl+Shift+E` | File Explorer |
| **Tabs** | `Ctrl+Tab` | Next tab |
| | Middle-click | Close tab |
| | Double-click empty | New tab |

---

## Architecture

```
┌──────────────────────────────────────────────┐
│            C++ Layer (Qt5 + QScintilla)       │
│   UI · Menus · Tabs · Dialogs · Editor       │
│   Terminal · AI Panel · Compare · Plugins     │
├──────────────────────────────────────────────┤
│                C FFI boundary                 │
├──────────────────────────────────────────────┤
│            Rust Core Library                  │
│   File I/O (mmap) · Search (Aho-Corasick)    │
│   Diff (Myers) · JSON/HTML/SQL Formatters    │
│   Bracket Fixer · Hash · Base64 · Encoding   │
└──────────────────────────────────────────────┘
```

**Why this hybrid?**
- **C++** because Qt and QScintilla are C++ — zero friction for UI
- **Rust** because file I/O, text processing, and parsing must never crash — Rust's ownership system guarantees memory safety
- **Result**: the speed of C++, the safety of Rust. The bare executable is **5.1 MB on Linux**, **3.0 MB on Windows**, **2.7 MB on macOS Apple Silicon** — the difference comes from compiler optimization (MSVC and clang strip more aggressively in release mode than gcc does). Downloads are 1.8 / 48 / 22 MB (Linux / Windows / Mac) once Qt is bundled for portability on the platforms that don't ship it system-wide.

---

## Install

### One-command install

**Linux / macOS:**
```bash
curl -fsSL https://notepatra.org/install.sh | sh
```

**Windows (PowerShell):**
```powershell
irm https://notepatra.org/install.ps1 | iex
```

That's it. Auto-detects your OS, downloads the right binary, installs it, adds to PATH, creates shortcuts.

### Or download manually — [Latest release: v0.1.0](https://github.com/singhpratech/notepatra/releases/latest)

| Platform | Download | Size | What's inside |
|---|---|---|---|
| 🐧 **Linux x64** | [`.tar.gz`](https://github.com/singhpratech/notepatra/releases/latest) | **1.8 MB** | Bare `notepatra` binary. Qt5 from your distro. |
| 🍎 **macOS Apple Silicon** (M1–M4) | [`.dmg`](https://github.com/singhpratech/notepatra/releases/latest) | **22 MB** | `Notepatra.app` with Qt frameworks bundled. Drag to Applications. |
| 🪟 **Windows x64** | [`.zip`](https://github.com/singhpratech/notepatra/releases/latest) | **48 MB** | `notepatra.exe` + Qt DLLs + QScintilla DLL. Unzip and run anywhere. |

**Why are the download sizes different?** The bare `notepatra` executable is **5.1 MB on Linux**, **3.0 MB on Windows**, and **2.7 MB on macOS Apple Silicon** (clang and MSVC strip more aggressively than gcc). On Linux, Qt5 is a standard system package (`apt install qtbase5-dev libqscintilla2-qt5-dev`), so the download is just the binary — 1.8 MB compressed. On macOS and Windows, Qt isn't pre-installed, so we bundle the Qt frameworks/DLLs alongside the executable for portability — same approach Krita, Kdenlive, and every cross-platform Qt app uses. Even with Qt bundled, Notepatra is still **6× smaller than VS Code** on Windows and **14× smaller** on Linux.

> macOS Intel: not shipped pre-built. Apple stopped selling Intel Macs in 2023 and the GitHub Actions `macos-13` runner has been unreliable. Intel Mac users — `git clone` and run `./build.sh`. Builds in ~3 minutes.

### Verify your download

Every release ships with **SHA-256 checksums**, **Sigstore (cosign) signatures**, and **SLSA build provenance**. The `install.sh` and `install.ps1` scripts above already verify SHA-256 automatically and refuse to install on mismatch — but if you downloaded manually you should verify yourself.

```bash
# Linux / macOS — checksum
curl -sL -O https://github.com/singhpratech/notepatra/releases/download/v0.1.0/SHA256SUMS
sha256sum -c SHA256SUMS --ignore-missing

# Anywhere — cosign verify (Sigstore)
cosign verify-blob \
  --certificate-identity-regexp '^https://github.com/singhpratech/notepatra/' \
  --certificate-oidc-issuer 'https://token.actions.githubusercontent.com' \
  --certificate notepatra-linux-x64.tar.gz.pem \
  --signature  notepatra-linux-x64.tar.gz.sig \
  notepatra-linux-x64.tar.gz

# Anywhere — SLSA build provenance
gh attestation verify notepatra-linux-x64.tar.gz --owner singhpratech
```

Full instructions, threat model, and disclosure policy in [SECURITY.md](SECURITY.md).

### Build from source

<details>
<summary>Linux (Ubuntu/Mint/Debian)</summary>

```bash
sudo apt install cmake qtbase5-dev libqscintilla2-qt5-dev
curl --proto '=https' --tlsv1.2 -sSf https://sh.rustup.rs | sh
source ~/.cargo/env
git clone https://github.com/singhpratech/notepatra.git
cd notepatra
cd rust-core && cargo build --release && cd ..
mkdir build && cd build && cmake .. && make -j$(nproc)
./notepatra
```
</details>

<details>
<summary>macOS</summary>

```bash
brew install qt@5 cmake
curl --proto '=https' --tlsv1.2 -sSf https://sh.rustup.rs | sh
source ~/.cargo/env
git clone https://github.com/singhpratech/notepatra.git
cd notepatra
# Build QScintilla from source (brew's version links Qt6)
./build.sh
```
</details>

<details>
<summary>Windows (MSVC)</summary>

**Prerequisites**
1. **Visual Studio 2022** with the *"Desktop development with C++"* workload
2. **Qt 5.15.2** for `msvc2019_64` — install via [Qt Online Installer](https://www.qt.io/download-qt-installer) or [aqtinstall](https://github.com/miurahr/aqtinstall)
3. **CMake** ≥ 3.16 — `winget install Kitware.CMake` or [cmake.org/download](https://cmake.org/download)
4. **Rust** stable — [rustup.rs](https://rustup.rs)

**Build QScintilla via the CMake wrapper** *(once)*
```powershell
git clone --depth 1 https://github.com/farleyrunkel/QScintilla.git $env:TEMP\qsci-src
cmake -S $env:TEMP\qsci-src -B $env:TEMP\qsci-src\build -G "Visual Studio 17 2022" -A x64 `
  -DCMAKE_BUILD_TYPE=Release `
  "-DCMAKE_PREFIX_PATH=C:\Qt\5.15.2\msvc2019_64" `
  "-DCMAKE_INSTALL_PREFIX=$env:TEMP\qsci-install"
cmake --build $env:TEMP\qsci-src\build --config Release
cmake --install $env:TEMP\qsci-src\build --config Release
```

**Build Notepatra**
```powershell
git clone https://github.com/singhpratech/notepatra.git
cd notepatra
cd rust-core; cargo build --release; cd ..
mkdir build; cd build
cmake .. -G "Visual Studio 17 2022" -A x64 `
  "-DCMAKE_PREFIX_PATH=C:\Qt\5.15.2\msvc2019_64" `
  "-DQSCINTILLA_INCLUDE=$env:TEMP\qsci-install\include" `
  "-DQSCINTILLA_LIB=$env:TEMP\qsci-install\lib\qscintilla2_qt5.lib"
cmake --build . --config Release
```

**Bundle Qt + QScintilla DLLs next to the exe**
```powershell
mkdir notepatra-win
copy build\Release\notepatra.exe notepatra-win\
windeployqt notepatra-win\notepatra.exe
copy $env:TEMP\qsci-install\bin\qscintilla2_qt5.dll notepatra-win\
.\notepatra-win\notepatra.exe
```

> If you hit `LNK2019 unresolved external symbol QsciScintilla::staticMetaObject` — verify `CMakeLists.txt` defines `QSCINTILLA_DLL` for Windows targets. Without it, MSVC won't emit `__declspec(dllimport)` and the linker will fail to resolve symbols against the import library. This is the gotcha that took 12 CI iterations to find.
</details>

---

## Plugin System

Drop a shared library in `~/.config/notepatra/plugins/` and restart.
- Linux: `.so` files
- macOS: `.dylib` files
- Windows: `.dll` files

### Write your own plugin in 30 seconds:

```cpp
// myplugin.cpp
extern "C" {
    const char* notepatra_plugin_name()    { return "My Plugin"; }
    const char* notepatra_plugin_version() { return "1.0"; }
    const char* notepatra_plugin_author()  { return "Your Name"; }

    char* notepatra_plugin_run(const char* text, int len) {
        // Your magic here — transform text, return malloc'd result
        // Return NULL to keep text unchanged
    }
}
```

```bash
# Linux
g++ -shared -fPIC -o myplugin.so myplugin.cpp

# macOS
clang++ -shared -o myplugin.dylib myplugin.cpp

# Windows
cl /LD myplugin.cpp /Fe:myplugin.dll
```

---

## Why not just use...?

| Editor | Download size | Native | Local AI | Built-in JSON fixer | 2 GB files | Linux | Win | Mac | Free |
|---|---|---|---|---|---|---|---|---|---|
| **Notepad++** | ~4 MB | ✓ | ✗ | plugin only | ✗ | ✗ | ✓ | ✗ | ✓ |
| **VS Code** | ~300 MB | ✗ Electron | extension | extension | ✗ | ✓ | ✓ | ✓ | ✓ |
| **Vim / Neovim** | ~3 MB | ✓ | ✗ | ✗ | ✓ | ✓ | ✓ | ✓ | ✓ |
| **Sublime Text** | ~30 MB | ✓ | ✗ | ✗ | ✓ | ✓ | ✓ | ✓ | $99 |
| **Kate / Gedit** | ~30 MB | ✓ | ✗ | ✗ | ✓ | ✓ | ✗ | ✗ | ✓ |
| **Notepatra** | **1.8 / 22 / 48 MB** | ✓ C++/Rust | ✓ Ollama | ✓ regex + AI | ✓ Rust mmap | ✓ | ✓ | ✓ | ✓ GPL-3 |

> *Notepatra download sizes are Linux / macOS / Windows. Linux is just the binary (Qt is system-installed). Mac and Windows include bundled Qt. The bare `notepatra` executable inside is 5.1 MB on Linux, 3.0 MB on Windows, 2.7 MB on macOS — different compilers, different optimization.*

---

## Tests

105/105 automated tests passing across 20 categories:
Window, Menus, Editor, Rust Core (Text, Search, Hash, JSON, HTML, Brackets, SQL, Diff, File I/O), Find/Replace, Languages, Bookmarks, Zoom, View, Tabs, Edge Cases, Config.

---

## Releases

Notepatra follows [Keep a Changelog](https://keepachangelog.com/) and [Semantic Versioning](https://semver.org/). Every release is tagged, signed, and published to GitHub Releases with binaries for all three platforms.

| Version | Date | Highlights |
|---|---|---|
| [**v0.1.0**](https://github.com/singhpratech/notepatra/releases/tag/v0.1.0) | 2026-04-06 | First public release. Linux x64, macOS Apple Silicon, Windows x64. 100+ file types, 44 lexers, AI formatters, Compare plugin, Git integration, 105/105 tests. |

See the full [CHANGELOG](CHANGELOG.md) for every change in every release, or browse the [version history on notepatra.org](https://notepatra.org#versions).

---

<p align="center">
  <strong>Envisioned by Prateek Singh. Inspired by Notepad++. Built by Claude.</strong>
</p>
<p align="center">
  <em>"I didn't build Notepatra because the world needed another text editor.<br>I built it because every developer — on Linux, Windows, or Mac — deserves one that's fast, free, and smart."</em>
</p>

<p align="center">
  © 2026 Prateek Singh. All rights reserved.<br>
  Source code licensed under <a href="LICENSE">GNU General Public License v3.0</a>.
</p>

---

## License, warranty, and liability

Notepatra is licensed under the **[GNU General Public License v3.0](LICENSE)**. The full legal text lives in the [`LICENSE`](LICENSE) file at the root of this repo. The short version, in plain English:

**You are free to:**
- Run Notepatra for any purpose, commercial or personal, on any number of machines.
- Read, study, and modify the source code.
- Redistribute the source or your modified versions, provided you also distribute them under GPL-3.0 and provide the source.

**You must:**
- Keep the copyright notice intact when redistributing.
- License any derivative work under GPL-3.0 (the "copyleft" requirement).
- Make the source code available to anyone you distribute a binary to.

**You may not:**
- Distribute Notepatra (or anything based on it) under a proprietary license.
- Strip out or sue over the no-warranty / no-liability clauses below.

### Disclaimer of warranty (GPL §15, plain English)

> **Notepatra is provided "AS IS", without warranty of any kind, express or implied, including but not limited to the warranties of merchantability, fitness for a particular purpose, and non-infringement. The entire risk as to the quality and performance of the program is with you. Should the program prove defective, you assume the cost of all necessary servicing, repair, or correction.**

In other words: I built this in good faith, I run the test suite on every commit, every release is checksummed and signed — but I am one person on the internet shipping a free editor. **Verify your downloads**, **back up your work**, and **don't blame me** if something breaks.

### Limitation of liability (GPL §16, plain English)

> **In no event will Prateek Singh, contributors, or anyone else who modifies or conveys Notepatra be liable to you for any damages — general, special, incidental, or consequential — arising from the use or inability to use the program, including but not limited to loss of data, data being rendered inaccurate, losses sustained by you or third parties, or a failure of the program to operate with any other program — even if I have been advised of the possibility of such damages.**

In other words: if Notepatra eats your file, crashes during a deadline, or fails to highlight your YAML — that's on you. The whole point of the GPL is that you can read the source, fix it, and ship the fix back to everyone. There is no SLA, no support contract, no money changing hands.

### What this means in practice

| Scenario | Who is responsible |
|---|---|
| You download the wrong file because of a typo and it bricks your shell | You — verify SHA-256 first ([SECURITY.md](SECURITY.md)) |
| You install a third-party plugin that exfiltrates your code | You — read plugin source before loading |
| Notepatra crashes and your unsaved file is gone | You — but crash recovery saves every 10s, check `~/.config/notepatra/recovery/` |
| Notepatra has an actual security bug | Report it via [private vulnerability disclosure](https://github.com/singhpratech/notepatra/security/advisories/new), I'll fix it |
| You modified Notepatra and it broke your customer's production | You — that's literally why GPL ships with §15 |

### Third-party components and their licenses

Notepatra links against several open-source libraries. None of them ask for money. Their licenses live in the source trees they ship from:

| Component | License | Purpose |
|---|---|---|
| **Qt 5.15** (LGPL-3) | LGPL-3.0 | UI framework |
| **QScintilla 2.14** (GPL-3) | GPL-3.0 | Editor widget + lexers (Notepatra inherits GPL-3 from this) |
| **Scintilla / Lexilla** (HPND) | HPND | Editing engine bundled with QScintilla |
| **Rust crates** (mostly MIT/Apache-2.0) | MIT/Apache-2.0 dual | Memory-safe core: file I/O, search, JSON/HTML/SQL fixers, diff |
| **Ollama** (when used for AI) | MIT | Optional local AI runtime — runs separately, you install it yourself |

The combination is GPL-3.0 because QScintilla is GPL-3.0 and the GPL is contagious. If you don't like that, the alternative would be to pay Riverbank Computing for a commercial QScintilla license — Notepatra doesn't.
