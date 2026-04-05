<p align="center">
  <h1 align="center">Notepatra</h1>
  <p align="center"><em>The first code editor built for the AI era.</em></p>
  <p align="center">
    <strong>C++ + Rust</strong> · <strong>4.9 MB binary</strong> · <strong>Zero Electron</strong> · <strong>60+ languages</strong> · <strong>AI-powered formatters</strong>
  </p>
  <p align="center">
    <a href="#features">Features</a> · <a href="#the-story">The Story</a> · <a href="#install">Install</a> · <a href="#plugins">Plugins</a> · <a href="#ai-powered">AI Powered</a> · <a href="#contributing">Contributing</a>
  </p>
</p>

---

## The Story

I'm Prateek Singh. A developer who spent years on Linux watching Windows users open Notepad++ and fix things in seconds — broken JSON, messy SQL, tangled HTML — while I was stuck with Wine hacks or bloated Electron editors eating 500 MB of RAM to show a text file.

Every text editor told me to pick two: **fast**, **powerful**, or **native**. Vim is fast but cryptic. VS Code is powerful but heavy. Notepad++ is both — but it only runs on Windows.

So I built Notepatra.

Not a port. Not a wrapper. Not "Notepad++ but on Linux." Something new — **for everyone**.

I took what made Notepad++ legendary — the speed, the simplicity, the "it just works" feeling — and asked: **what would Notepad++ look like if it was built today, in 2026, when AI is part of every developer's workflow?**

The answer: a 4.9 MB native binary with a Rust-powered core, Scintilla editing engine, and local AI integration. An editor that can fix your broken JSON with regex in milliseconds — and when regex isn't enough, it asks your local AI to figure it out. No cloud. No telemetry. No subscription. Just you and your code.

Notepatra started on Linux — because that's where the gap was. But great tools shouldn't have borders. **Notepatra runs on Linux, Windows, and macOS.** Same codebase. Same features. Same 4.9 MB. No one gets left behind.

**Notepatra isn't trying to replace Notepad++. It's what I wish existed — on every platform.**

---

## Features

### Editor — Battle-tested basics done right
- **60+ file types** with full syntax highlighting — Python, Rust, Go, C/C++, Java, JavaScript, TypeScript, SQL, HTML, CSS, JSON, YAML, Markdown, Bash, and many more
- **Tabbed editing** — drag, reorder, middle-click close, double-click empty area for new tab
- **Tab right-click menu** — Close, Close Others, Close Left/Right, Save, Rename, Copy Full Path, Copy Filename, Copy Directory, Open Folder, Open Terminal, Read-Only toggle, **Color Tag** (7 colors + custom)
- **Session persistence** — close Notepatra, reopen tomorrow, same files, same cursor positions, same window size
- **Crash recovery** — if Notepatra crashes (it shouldn't, but life happens), your unsaved work is recovered on next launch
- **File change detection** — someone else edits your file? Notepatra asks: reload or keep yours?
- **2.5 GB file support** — memory-mapped I/O via Rust, opens massive files without choking
- **Double-click word highlight** — double-click any word, all occurrences light up in orange
- **Ctrl+B brace matching** — jump between matching `{}` `[]` `()`, highlights both braces + selects everything between
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
- Powered by Rust `sqlformat` crate

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
- **Model selector dropdown** — pick any installed model

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
| **Built-in Terminal** | `Ctrl+`` — opens as a tab, runs real bash commands |
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
| **Features** | `Ctrl+`` | Terminal |
| | `Ctrl+Shift+A` | AI Assistant |
| | `Ctrl+Shift+R` | REST Client |
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
- **Result**: the speed of C++, the safety of Rust, in a 4.9 MB binary

---

## Install

### Download

| Platform | Download | Status |
|---|---|---|
| **Linux** (Ubuntu/Mint/Debian) | [Build from source](#build-from-source) | Available |
| **Windows** | [Build from source](#build-from-source) | Available (cross-platform code ready) |
| **macOS** | [Build from source](#build-from-source) | Available (cross-platform code ready) |

### Build from source (Linux — Ubuntu/Mint/Debian)
```bash
# Dependencies
sudo apt install cmake qtbase5-dev libqscintilla2-qt5-dev libqt5network5-dev

# Rust
curl --proto '=https' --tlsv1.2 -sSf https://sh.rustup.rs | sh
source ~/.cargo/env

# Build
git clone https://github.com/singhpratech/notepatra.git
cd notepatra
cd rust-core && cargo build --release && cd ..
mkdir build && cd build
cmake ..
make -j$(nproc)

# Run
./notepad-pp-linux
```

### Desktop entry
```bash
cp notepad-pp-linux ~/.local/bin/notepatra
cat > ~/.local/share/applications/notepatra.desktop << EOF
[Desktop Entry]
Name=Notepatra
Comment=Native C++/Rust code editor with AI-powered formatters
Exec=$HOME/.local/bin/notepatra %F
Icon=accessories-text-editor
Terminal=false
Type=Application
Categories=Development;TextEditor;
EOF
```

---

## Plugin System

Drop a `.so` shared library in `~/.config/notepatra/plugins/` and restart.

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
g++ -shared -fPIC -o myplugin.so myplugin.cpp
cp myplugin.so ~/.config/notepatra/plugins/
```

---

## Why not just use...?

| Editor | Why Notepatra instead |
|---|---|
| **Notepad++** | Windows only. No AI. No Rust safety. No Mac. |
| **VS Code** | 300+ MB Electron app. Telemetry. Slow on large files. |
| **Vim/Neovim** | Steep learning curve. Not everyone wants modal editing. |
| **Sublime Text** | Proprietary. $99. No AI. No built-in formatters. |
| **Kate/Gedit** | Linux only. No AI. No JSON/HTML fixer. Limited plugins. |
| **Notepatra** | 4.9 MB. Native on Linux, Windows, Mac. AI formatters. Rust core. 2.5 GB files. Free forever. |

---

<p align="center">
  <strong>Envisioned by Prateek Singh. Inspired by Notepad++. Built by Claude.</strong>
</p>
<p align="center">
  <em>"I didn't build Notepatra because the world needed another text editor.<br>I built it because every developer — on Linux, Windows, or Mac — deserves one that's fast, free, and smart."</em>
</p>
