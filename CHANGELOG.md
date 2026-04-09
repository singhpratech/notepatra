# Changelog

All notable changes to Notepatra will be documented in this file.
Format follows [Keep a Changelog](https://keepachangelog.com/) and [Semantic Versioning](https://semver.org/).

---

## [0.1.4] — 2026-04-09

### Added
- 🪟 **Proper Windows installer** — `notepatra-setup-0.1.4.exe` (NSIS MUI2). v0.1.0–v0.1.3 only shipped a portable zip, so Notepatra never appeared in Windows Settings → Apps → Installed apps and couldn't be uninstalled the normal way. The new installer:
  - Installs to `%LOCALAPPDATA%\Notepatra` per-user (no UAC prompt)
  - Writes the `HKCU\…\Uninstall\Notepatra` registry key with `DisplayName`, `DisplayVersion`, `Publisher`, `InstallLocation`, `DisplayIcon`, `UninstallString`, `EstimatedSize`, `URLInfoAbout`, `HelpLink` so it appears properly in "Installed apps"
  - Generates `uninstall.exe` that removes files + registry + shortcuts + PATH entry
  - Creates Start Menu shortcuts under "Notepatra" + optional Desktop shortcut
  - Optionally adds `notepatra` to user PATH (Components page lets user opt out)
  - Cosign-signed and SLSA-attested same as the zip
- 🪟 **`install.ps1` (PowerShell one-liner installer) now also writes the Uninstall registry key** and drops an `uninstall.ps1` next to the binary, so users who run `irm https://notepatra.org/install.ps1 | iex` also get a proper "Installed apps" entry without having to download the .exe installer.
- 📊 **Live download counter on the website footer** — pulls from the GitHub Releases API on every page load and shows per-platform totals (🐧 Linux / 🍎 macOS / 🪟 Windows) using the platform icons. Sums every binary across every release (excluding `.sig`/`.pem` signature siblings).
- 📦 The portable `.zip` is still available on the release page for users who want it (USB drives, restricted environments, or "I don't want anything in my registry").

### Fixed
- 🔗 Stale `v0.1.0` references on the website homepage (JSON-LD `softwareVersion`, hero badge, SHA256SUMS link) bumped to current version.

### Verifying this release
Same as 0.1.3 — SHA-256, cosign, SLSA. The new installer `.exe` is included in `SHA256SUMS` and has its own `.sig`/`.pem` cosign artifacts.

---

## [0.1.3] — 2026-04-09

### Fixed
- 🎨 **C++ / C / C# / JavaScript preprocessor directives now paint brown-bold on all platforms.** v0.1.2's palette function matched on `description().contains("preproc")` but the QScintilla lexer description is literally `"Pre-processor block"` (with a hyphen), so the substring check missed it and `#include` / `#define` / `#if` lines rendered in default black instead of Notepad++'s `#804000` brown. Now also matches `"pre-proc"` and `"processor"` as substrings.
- 🧪 **Extracted the Notepad++ palette into `src/npp_palette.{h,cpp}`** as a free function so it can be unit-tested without pulling in the full Editor + Rust core link graph. `Editor::applyNotepadPlusPalette` now delegates to the free function.

### Added
- ✅ **`test_palette.cpp`** — per-style color verification test that instantiates every QsciLexer we ship (C++, JS, Python, SQL, JSON, Bash, Markdown, …), applies the Notepad++ palette, and asserts exact RGB colors + bold/italic for keyword / comment / number / string / operator / preprocessor / secondary-keyword styles. Caught the preprocessor-hyphen bug above.
- ✅ **`test_ollama.cpp`** — end-to-end test that hits a real local Ollama daemon to verify `OllamaClient::isAvailable()` (QEventLoop+QTimer probe), `OllamaClient::listModels()` parses `/api/tags` correctly, and `setModel()` / `model()` round-trip. Skipped (exit 0) if Ollama isn't running so CI stays green on runners without Ollama.

### Verifying this release
Same pipeline as 0.1.2 — SHA-256, cosign, SLSA. See `SECURITY.md`.

---

## [0.1.2] — 2026-04-09

### Fixed
- 🎨 **Syntax highlighting on Windows now paints keywords, strings, comments, numbers, and operators.** v0.1.1's Windows binary loaded the right QScintilla lexers but rendered almost nothing visually — keywords stayed black, strings stayed black, comments stayed black. The root cause was that QScintilla's built-in default per-style colors aren't reliably set on Windows when a lexer is assigned cold. Notepatra now explicitly paints every style slot in the Notepad++ default palette (keywords blue+bold, comments green+italic, strings gray, numbers orange, operators bold, preprocessor brown) using each lexer's own `description()` so the palette applies to all 40+ languages without hard-coding per-lexer style constants. Switching language via the Language menu now highlights immediately.
- 🤖 **AI Assistant: Ollama model list is now dynamic.** Previous versions hard-coded a list that usually didn't match what the user actually had installed (e.g. the default was `qwen3.5:9b` but many users have `qwen2.5:7b` or `llama3.2`). The panel now calls Ollama's `/api/tags` endpoint on open and populates the dropdown with whatever models are actually installed. Added a ↻ refresh button and a status line that shows "Ollama: N models detected" or an error ("Ollama not running. Start it: ollama serve") instead of silently failing.
- 🤖 **Ollama availability probe reliability on Windows.** `QNetworkReply::waitForReadyRead()` is unreliable for localhost sockets on Windows; replaced with a `QEventLoop` + `QTimer` pattern that has a hard 3-second timeout and properly reports whether the probe finished.
- ⌨ **Ctrl+B now actually jumps between matching braces** (Notepad++ "Go to Matching Brace"). Previously it only highlighted the pair and selected the range — the caret never moved, so pressing it again did nothing visible. Now the caret moves to the matching brace so pressing Ctrl+B again swivels back to the original position.
- 📝 Removed the cursor-movement hook that auto-cleared brace highlight on every tiny caret move — that hook was hiding Ctrl+B's effect instantly.

### Verifying this release
Same as 0.1.1 — SHA-256 checksums in `SHA256SUMS`, cosign signatures on every artifact, SLSA build provenance attestations. See `SECURITY.md`.

---

## [0.1.1] — 2026-04-06

### Fixed
- 🪟 **Windows: Markdown / SQL / JSON syntax highlighting now works.** v0.1.0's Windows binary used a third-party CMake wrapper for QScintilla that was missing the Lexilla bindings. The lexer C++ classes existed but were no-op stubs at runtime. Switched back to Riverbank's official QScintilla 2.14.1 source via qmake/nmake — same source path Linux and macOS already used.
- 🪟 **Windows: notepatra.exe now has an embedded icon and version info.** v0.1.0 shipped without `resources/notepatra.rc` and `resources/notepatra.ico`, so MSVC's resource compiler had nothing to embed. Generated a multi-resolution `.ico` (16/24/32/48/64/128/256) and a Windows resource script with full VERSIONINFO (CompanyName, FileDescription, LegalCopyright, ProductVersion). Right-click → Properties → Details now shows real metadata.
- 🍎 **macOS: Notepatra.app now has an embedded icon.** Generated a proper `notepatra.icns` with all required Apple icon sizes (16/32/128/256/512/1024) plus retina @2x variants. Finder now shows the real icon, not the generic app icon.
- 🐧 **Linux: launcher icon now resolves through hicolor theme.** v0.1.0's `install.sh` set `Icon=accessories-text-editor` in the desktop entry, so launchers showed a generic editor icon. Now downloads the Notepatra PNGs into `~/.local/share/icons/hicolor/<size>x<size>/apps/notepatra.png` for sizes 16/32/48/64/128/256 and runs `gtk-update-icon-cache`. Desktop entry uses `Icon=notepatra` which resolves through the hicolor theme search path.
- 📝 **JSON files with CRLF line endings were misclassified as JavaScript.** `editor.cpp` had a CRLF-blind heuristic: `trimmed.startsWith("{\n")`. On Windows, JSON files almost always have CRLF line endings, so the second character is `\r` not `\n`. Replaced with a CRLF-agnostic check. Affects all platforms.
- 📝 Empty `#ifdef HAS_LEXER_COFFEESCRIPT` block in `editor.cpp` had no body — added the missing `QsciLexerCoffeeScript` instantiation.
- 📝 The Diff lexer was incorrectly wrapped in `#ifdef HAS_LEXER_D` (the D language guard) — removed the wrong wrap. Diff highlighting now works.

### Added
- 🛡 **`SECURITY.md`** with full vulnerability disclosure policy, threat model, and verification guide for SHA-256 + cosign + SLSA.
- 🛡 **RFC 9116 `/.well-known/security.txt`** with contact, policy, and acknowledgments URLs.
- 🛡 **GitHub private vulnerability reporting** enabled.
- 🔐 **SHA-256 checksums** generated for every release artifact and shipped as `SHA256SUMS`.
- 🔐 **Cosign signatures (Sigstore)** — keyless OIDC signing tied to the official `singhpratech/notepatra` GitHub Actions workflow, certs recorded in the public Rekor transparency log.
- 🔐 **SLSA build provenance attestations** — cryptographically links each binary to the git commit + workflow file + runner environment.
- 🔐 **`install.sh` and `install.ps1` now verify SHA-256 before extracting** and refuse to install on mismatch.
- ✅ **Lexer smoke test** (`test_lexers.cpp`) runs in CI on all 3 platforms via a CMake target. Catches the class of bug where a lexer is instantiated but is a no-op stub.
- ✅ **Windows runtime smoke test** — `notepatra.exe --version` actually runs in CI after DLL bundling.
- 🔍 **CodeQL static analysis** workflow runs on every PR + weekly cron with `security-extended` query pack.
- 🤖 **Dependabot weekly** scans for cargo and github-actions updates.
- 📊 **Daily download stats** — `https://notepatra.org/stats.json` updated automatically every 06:00 UTC.
- 🪪 **Plain-English warranty disclaimer** in README and website footer (GPL-3.0 §15 and §16 in plain English).
- 📜 **Version History section** on the website (Notepad++ style, append-only).
- 🚦 **IndexNow ping workflow** — notifies Bing/Yandex/Seznam/Naver on every docs/ push.
- 🌐 **Website security headers** — Content-Security-Policy, X-Content-Type-Options, Referrer-Policy, Permissions-Policy via meta tags.

### Changed
- 🐧 **macOS Intel** is no longer shipped as a pre-built binary. Apple stopped selling Intel Macs in 2023 and the GitHub Actions `macos-13` runner has been unreliable. macOS Apple Silicon (M1/M2/M3/M4) covers all current Macs. Intel users can build from source.
- 🪟 **Windows download is bigger** because it now bundles the full Qt runtime + the Qt platforms plugin manually (instead of relying on `windeployqt`'s unreliable transitive dependency scan). Trade-off: ~5 MB larger zip in exchange for the binary actually starting on every Windows install.
- 📦 **Per-version release notes** — every release on GitHub Releases now uses `release_notes/<tag>.md` as its body instead of auto-generated commit titles.

### Honest about what's still NOT done (in `SECURITY.md`)
- ❌ Authenticode code signing on Windows (requires ~$300/year EV cert)
- ❌ Apple notarization on macOS ($99/year Apple Developer)
- ❌ LSP support (planned for v0.2.0)
- ❌ Linux ARM64 builds (planned for v0.1.2)
- ❌ Pinning every GitHub Action by commit SHA (Dependabot helps)

### Verifying this release

```sh
# Quick: SHA-256
sha256sum -c SHA256SUMS --ignore-missing

# Better: Sigstore
cosign verify-blob \
  --certificate-identity-regexp '^https://github.com/singhpratech/notepatra/' \
  --certificate-oidc-issuer 'https://token.actions.githubusercontent.com' \
  --certificate notepatra-linux-x64.tar.gz.pem \
  --signature  notepatra-linux-x64.tar.gz.sig \
  notepatra-linux-x64.tar.gz

# Best: SLSA
gh attestation verify notepatra-linux-x64.tar.gz --owner singhpratech
```

Full guide in [SECURITY.md](SECURITY.md).

---

## [0.1.0] — 2026-04-06

### First Public Release

**Core**
- Native C++ (Qt5 + QScintilla) editor with Rust core library
- 100+ file types with 44 language lexers
- Memory-mapped file I/O handles files up to 2 GB (full visibility, no truncation)
- Aho-Corasick search engine (Rust) for fast literal matching
- Myers diff algorithm (Rust) for file comparison
- 5.1 MB standalone binary
- 105/105 automated tests passing
- Cross-platform: Linux, macOS (Intel + Apple Silicon), Windows

**Editor**
- Tabbed editing with drag, reorder, middle-click close, double-click new tab
- Tab right-click: Close, Close Others, Close Left/Right, Save, Save As, Rename, Copy Full Path, Copy Filename, Copy Directory, Open Folder, Open Terminal, Read-Only, Color Tag (7 colors + custom)
- 3 themes: Light, Dark, Monokai (Settings > Theme, persisted)
- Session persistence — reopens all files, cursor positions, window size on restart
- Crash recovery — auto-saves unsaved work every 10 seconds, restores on crash
- File change detection — notifies when external programs modify open files
- Recent Files menu (persisted across sessions)
- Drag-and-drop file open
- Double-click word highlight (all occurrences in orange)
- Ctrl+B brace matching — highlights both braces + selects everything between
- Macro recording — Start (Ctrl+Shift+R), Stop (Ctrl+Shift+T), Playback (Ctrl+Shift+P), Run Multiple Times, Save/Load .macro files
- Code folding, bookmarks, auto-complete, indent guides, line numbers
- Custom scrollbars (rounded, modern)
- Pastel green current line highlight
- Word count in status bar
- Encoding conversion (UTF-8, ANSI, ISO-8859-1, UTF-16)
- Git gutter auto-refresh on save

**Languages (44 lexers)**
- Python, JavaScript, CoffeeScript, C, C++, C#, D, Java, HTML/PHP, CSS, XML, JSON, SQL, Bash, Batch, Ruby, Perl, Lua, TCL, Fortran, Fortran77, MATLAB, Octave, IDL, NASM, MASM, Verilog, VHDL, TeX, PostScript, POV-Ray, Spice, AVS, Properties, PO, IntelHex, SRecord, Markdown, YAML, Diff, Pascal, CMake, Makefile
- SQL variants: T-SQL, PL/SQL, MySQL, PostgreSQL, SQLite

**Plugins (inbuilt)**
- JSON Tools — Format, Minify, Fix+Format (Rust), AI Fix (Ollama)
  - Fixes: missing braces, trailing commas, single quotes, unquoted keys, missing `{` after `[`, nested object detection
  - Preserves original key order
  - Detailed fix report showing every issue found
- HTML Tools — Format (2/4 spaces), Minify, Fix+Format, AI Fix (Ollama)
  - Fixes: unclosed tags, missing closers, broken nesting
- Bracket Tools — Check (with line numbers), Auto-Fix, AI Fix (Ollama)
  - Detects: mismatched (), [], {}, begin/end, if/fi, do/done
- SQL Formatter — UPPERCASE/lowercase keywords, configurable indent
- Compare / Diff — pick any two tabs or tab vs file
  - Side-by-side Scintilla editors with syntax highlighting
  - +/- markers, Prev/Next navigation
  - Ignore whitespace, case, empty lines
- Git Integration — changed files panel, branch display, Push/Pull/Refresh, Open on GitHub, git gutter margins

**AI Integration (Ollama — local, private, no cloud)**
- AI Assistant panel (Ctrl+Shift+A) with 8 actions: Explain, Find Bugs, Refactor, Write Tests, Add Comments, Generate Docs, Optimize, Translate
- AI Fix button in JSON, HTML, and Bracket tools — hybrid approach (regex for speed, AI for intelligence)
- Ollama status indicator (green/red dot) with model selector dropdown
- Default model: qwen3.5:9b, also supports: gemma4:e4b, llama3.2:3b, codellama:7b, deepseek-coder-v2:16b, mistral:7b, starcoder2:7b
- Setup instructions shown when Ollama not available

**Search**
- 5-tab Find/Replace: Find, Replace, Find in Files, Mark, Go to Line/Offset
- 3 search modes: Normal, Extended (\n, \r, \t, \xNN), Regular expression
- Find All in Current Document — results in bottom panel, double-click to jump
- Find All in All Opened Documents
- Replace All in All Opened Documents
- Find in Files — recursive directory search with file filters
- Mark All with visual indicators
- Search history (last 20 searches)

**Features**
- Built-in Terminal — opens as tab, real bash/cmd/zsh commands, cd works
- REST Client — send HTTP requests, see responses with pretty JSON
- Hex Editor — color-coded hex dump (offset, hex bytes, ASCII columns)
- Markdown Converter — selection to table, list, code block, bold, italic, link, heading, HTML-to-markdown
- File Explorer sidebar (Ctrl+Shift+E)
- Function List panel — lists functions/classes, double-click to navigate
- Preferences dialog (6 tabs: General, Editing, Margins, New Document, Tab Settings, Auto-Completion)

**Plugin System**
- User plugins via shared libraries (.so Linux, .dylib macOS, .dll Windows)
- Simple C API: export `notepatra_plugin_name()` and `notepatra_plugin_run()` to create a plugin
- Plugins appear in menu with author/version info

**CLI**
- `notepatra --version` — show version
- `notepatra --help` — show usage
- `notepatra --line N file` — open file at line N
- `notepatra --theme Dark` — start with specific theme
- `notepatra file1 file2 ...` — open multiple files

**Install**
- One-command install: `curl -fsSL https://notepatra.org/install.sh | sh`
- Windows: `irm https://notepatra.org/install.ps1 | iex`
- GitHub Actions CI builds Linux x64, macOS ARM64, macOS x64, Windows x64
- Auto-creates GitHub Releases with downloadable binaries on tag push

**Security**
- Zero API keys or secrets in code or git history
- Ollama connects only to localhost (no external network)
- Plugin loading restricted to user-controlled directory
- Crash handler catches SIGSEGV/SIGABRT, saves recovery data
- No telemetry, no analytics, no phone-home

---

_Envisioned by Prateek Singh. Inspired by Notepad++. Built by Claude._
