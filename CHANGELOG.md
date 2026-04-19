# Changelog

All notable changes to Notepatra will be documented in this file.
Format follows [Keep a Changelog](https://keepachangelog.com/) and [Semantic Versioning](https://semver.org/).

---

## [0.1.11] — 2026-04-11

Hotfix release — v0.1.10 shipped a macOS DMG that crashed on launch.

### Fixed
- 💥 **Fix macOS crash-at-launch on v0.1.10 DMG.** The bundled `libqscintilla2_qt5.15.dylib` still had absolute Homebrew paths in its `LC_LOAD_DYLIB` entries (e.g. `/opt/homebrew/*/QtPrintSupport.framework/Versions/5/QtPrintSupport` — yes, with a literal `*` in the path, a known QScintilla-on-Homebrew quirk where qmake bakes its rpath glob into the install_name). dyld failed with `Library not loaded: ... Reason: tried: ... (no such file)` the moment Notepatra.app launched on any user's Mac. The `Create DMG` CI step now:
  1. Runs `macdeployqt` first so the main binary gets an `@executable_path/../Frameworks` rpath.
  2. Copies QScintilla into `Contents/Frameworks/` **after** macdeployqt.
  3. Runs `install_name_tool -id @rpath/libqscintilla2_qt5.15.dylib` on the copy.
  4. Walks `otool -L` output for libqscintilla2 and rewrites every `/opt/homebrew/*` or `/usr/local/*` `LC_LOAD_DYLIB` entry to `@rpath/...` — catches framework-style references and plain dylib deps.
  5. Force-copies `QtPrintSupport.framework` into the bundle if macdeployqt skipped it (it normally does, since the main notepatra binary does not link QtPrintSupport directly — only libqscintilla2 does, and macdeployqt's dep walker doesn't see into dylibs it doesn't recognize).
  6. Runs the same install_name rewriting on QtPrintSupport.
  7. Audits the final bundle with `otool -L | grep /opt/homebrew` and fails loudly if any leftover absolute Homebrew path is still present.
  8. Re-signs the whole bundle ad-hoc (`codesign --force --deep --sign -`) so all modified load commands have a fresh signature and dyld accepts them.

### Changed
- 🌐 **Homepage simplified** — the "Uninstall" section has been moved off `notepatra.org/` and lives on the documentation site at `notepatra.org/docs.html#uninstall` only. Keeps the landing page focused on download + features.

### Verifying this release
Same as previous — SHA-256, cosign, SLSA. See SECURITY.md.


## [0.1.10] — 2026-04-11

### Fixed
- 🍎 **macOS installer is no longer broken.** `install.sh` used to blindly save every download as `notepatra.tar.gz` and run `tar xzf` on it, which failed with `tar: Error opening archive: Unrecognized archive format` when the release asset was a `.dmg` — i.e. every macOS install since we moved to DMG. The script now inspects the download URL, writes the correct file extension, mounts DMGs with `hdiutil attach`, and copies `Notepatra.app` out with `ditto`. Verified end-to-end on macOS Tahoe.
- 🔐 **macOS Tahoe / Sequoia / Sonoma Gatekeeper "Notepatra is damaged" error.** Because we're not (yet) Apple-notarized, the quarantine xattr that `curl` stamps on the DMG causes Gatekeeper to refuse to launch the bundle. The installer now runs `xattr -dr com.apple.quarantine` and `codesign --force --deep --sign -` after copying the app, which re-anchors the bundle and lets it open on first double-click. Verified on Tahoe.
- 🔗 **`notepatra` CLI shortcut on macOS.** After installing the .app, the installer now symlinks `/Applications/Notepatra.app/Contents/MacOS/notepatra` into `~/.local/bin/notepatra` and ensures `~/.local/bin` is on `$PATH` in `.zshrc` / `.bash_profile`, so `notepatra file.py` just works from any terminal.

### Added
- 🧹 **`uninstall.sh` and `uninstall.ps1`** served from https://notepatra.org. One-liner clean removal for every supported install path:
  ```
  curl -fsSL https://notepatra.org/uninstall.sh | sh         # macOS + Linux
  irm     https://notepatra.org/uninstall.ps1 | iex          # Windows
  ```
  On macOS this removes `/Applications/Notepatra.app`, the `~/.local/bin/notepatra` symlink, and `~/Library/Preferences/com.notepatra.editor.plist` / saved-state / caches. On Linux it removes the binary, the `.desktop` entry, every hicolor icon size, `~/.config/notepatra`, and runs `gtk-update-icon-cache` + `update-desktop-database`. On Windows it calls the NSIS uninstaller if present, then manually wipes `%LOCALAPPDATA%\Notepatra`, Start Menu + Desktop shortcuts, the user PATH entry, the HKCU Uninstall registry key, and `%APPDATA%\Notepatra`. Explicitly leaves Ollama, any pulled models, system Qt5 packages, and files you edited alone.
- 📄 **Dedicated "Uninstall" section** on https://notepatra.org with three per-platform cards covering *every* install path — install.sh / manual tarball / build-from-source on Linux; install.sh / DMG drag on macOS; NSIS installer / install.ps1 / portable zip on Windows. Every card lists the exact `rm` / registry commands and a "Left alone" list so users know what the uninstaller does and does not touch. Plus a Privacy-guarantee callout: "Notepatra never sends telemetry. Uninstalling removes *only* the binary, shortcuts, and your local config — nothing was ever phoned home, so there's nothing to revoke."

### Changed
- 🎨 **Website re-themed to match Claude.ai.** The dark "matrix" theme is replaced with Anthropic's warm "Clay" palette — bone `#F5F4EE` page background, `#FAF9F5` card surfaces, Clay orange `#CC785C` accent, near-black warm text `#141413`, Söhne / Inter font stack, hairline `#E8E6DC` borders. All section backgrounds, gradients, code blocks, install tabs, primary/secondary buttons, and hero gradient retuned. Legacy CSS variable names (`--green`, `--blue`, `--darker`, etc.) kept as aliases so ~hundreds of existing element rules didn't need to be rewritten.

### Verifying this release
Same as previous — SHA-256 in `SHA256SUMS`, cosign keyless signatures, SLSA build provenance. See `SECURITY.md`.


## [0.1.9] — 2026-04-11

### Added
- 📚 **Comprehensive documentation site** at `docs/docs.html` — Anthropic-style layout with fixed sidebar, scroll-spy navigation, 40+ anchored sections covering every plugin, every keyboard shortcut, AI pipeline internals, and build-from-source recipes for Linux/macOS/Windows.
- 🔤 **Centralized font system** — new `src/fonts.h` exposes `notepatraUiFont()` and `notepatraCodeFont()` so every panel, editor, and dialog picks the same soothing base font (JetBrains Mono / Cascadia Code / Menlo / Consolas with platform fallbacks) and size. No more mismatched fonts between the editor and the plugin panels.
- 🧰 **Lexer utilities module** — new `src/lexerutils.{h,cpp}` consolidates per-language lexer resolution and style application that was previously duplicated across editor.cpp and mainwindow.cpp.
- 📏 **Editor rulers + crosshair overlay** — optional vertical ruler bands at configurable columns (80/100/120) and a horizontal/vertical crosshair that follows the caret. Toggled via View menu.
- 🎤 **Voice input in AI Assistant** — new microphone button on the chat panel records audio via `arecord`, transcribes via local whisper.cpp / `whisper` CLI, and pastes the transcript into the input line. Graceful fall-back to a text error bubble if neither tool is installed.
- 🧪 **`test_compare_widget.cpp`** — new CTest target that exercises the paired-row + character-level diff logic on ~20 scenarios (pure inserts, pure deletes, modifications, mixed blocks, trailing newline edge-cases).
- 🐧 **Linux ARM64 build target** in CI — v0.1.9 now ships a fourth artifact `notepatra-linux-arm64.tar.gz` alongside Linux x64, macOS ARM64, and Windows x64.

### Changed
- 🪚 **Massive internal refactor (~3200 lines changed across 25 files).** Non-functional split of god-files into focused units: font helpers out of mainwindow into `fonts.h`, lexer matching out of editor into `lexerutils`, compare widget rewritten as a single `CompareWidget` class with a real `CompareNavBar`, git panel rewritten to stream `git status --porcelain=v2 -z` + `git diff --numstat` instead of parsing human-readable output.
- 🎨 **Plugin panels share a unified `FormatterPanel` base** — JSON Tools, HTML Tools, Bracket Tools, and SQL Formatter all inherit the same status banner, session log, action button layout, and Show Diff button wiring. Adding a new formatter plugin is now ~50 lines.
- 📜 **Git panel** — now supports stage/unstage at line granularity (not just file), diff preview per file, inline commit message editor, and a push/pull/fetch toolbar. Replaces the previous read-only git status list.
- 🤖 **AI Assistant chat** — transcript stored as a proper `QVector<ChatMessage>` with `Role::User / Assistant / Error` so re-rendering is deterministic. Streaming tokens now accumulate into `m_currentAssistantText` instead of appending directly to the HTML, which fixes a rare duplicate-token bug when the model emitted `<think>` blocks mid-stream.
- 🎨 **Notepad++ palette** — `applyNotepadPlusPalette()` now also re-applies the `STYLE_BRACELIGHT` / `STYLE_BRACEBAD` colors after `setLexer()` so brace-match highlighting survives language switches (previously the lexer reset them to default).

### Fixed
- Brace highlighting disappeared after switching language — re-applied in `editor.cpp::applyLexer` after `setLexer()`.
- Horizontal scrollbar in Compare panels no longer feedback-loops when one side is narrower than the other.
- About dialog version was hardcoded — now reads `QApplication::applicationVersion()` from the `NOTEPATRA_VERSION` compile define.
- JSON Tools: broken input was being auto-pretty-printed on paste, mangling the very content the user wanted to fix. `FormatterPanel::setInput` no longer auto-formats.

### Verifying this release
Same as previous — SHA-256 in `SHA256SUMS`, cosign keyless signatures, SLSA build provenance attestations. See `SECURITY.md`.


## [0.1.8] — 2026-04-09

### Fixed
- 🎨 **Compare panel rewritten as a true ComparePlus-style side-by-side diff.** Inspired by Pavel Nedev's [ComparePlus](https://github.com/pnedev/comparePlus) plugin for Notepad++. Specifically:
  - **Modified rows are paired** at the same visual line — when the diff produces N consecutive deletes followed by M consecutive adds, they're merged into `min(N,M)` paired modified rows (kind=3) instead of being shown as separate delete-then-add blocks.
  - **Character-level highlighting** within modified rows — common-prefix + common-suffix detection finds the EXACT changed characters. Only those bytes get the colored indicator (red on left, green on right). The rest of the line stays plain.
  - **Soft `#FFFBE6` pale yellow** background on modified rows so the character-level red/green stands out.
  - **Soft mint `#D4F4D4`** for added lines, **soft salmon `#F4D4D4`** for deleted, **light blue `#E8F0F8`** placeholder background on the empty side.
  - **Symbol margin** (18px wide) with per-row icons: pink `~` Circle marker for modified, green `+` Plus marker for added, red `−` Minus marker for deleted.
  - **Custom per-row line numbers** via `TextMargin` so the LEFT panel shows the original LEFT-source line numbers and the RIGHT panel shows the original RIGHT-source line numbers — they diverge cleanly when there are insertions/deletions. Empty placeholder rows show a green `+` instead of a number.
  - **No syntax highlighting on context lines** — soft `#606060` mid-gray text everywhere so the diff markers are the only thing that draws the eye.
  - **Soft `#A0A0A0` line numbers** on `#F8F8F8` margin background.
  - **Both vertical AND horizontal scrollbars synced** — drag either and both panels move together.
- 🪟 **`Plugins → ComparePlus`** added as a separate menu entry alongside `Compare (inbuilt)`. Both use the same shared `openComparePicker()` helper and the same `CompareWidget` — different tab labels so users can have multiple compare tabs open at once and tell them apart.
- 📊 **Show Diff button works for ANY action** in JSON / HTML / Bracket Tools — Format / Minify / Fix+Format / AI Fix all populate the panel's `m_lastFixInput` / `m_lastFixOutput` via `recordFix()` so the diff button enables for every transformation, not just AI Fix.
- 🤖 **AI Fix (Ollama) in JSON Tools actually works for thinking models like Qwen3 / DeepSeek-R1.** v0.1.7 sent the prompt and waited for tokens, but if the model emitted `<think>...</think>` reasoning before the JSON, the cleanup pipeline ran `RustCore::formatJson` on the whole thing (including the thinking blocks) and the parse failed → user saw nothing. Three-layer fix:
  1. **`OllamaClient::generate()` now passes `"think": false`** in the `/api/generate` request body. Modern Ollama honors this and skips thinking entirely. Older Ollama / non-thinking models ignore the field harmlessly.
  2. **System prompt also appends `/no_think`** as a belt-and-braces signal — some models honor this slash-command instead of the API field.
  3. **Defensive `<think>...</think>` regex strip** in the JSON Tools cleanup pipeline catches any thinking that leaks through.
  4. **Leading-prose trim** finds the first `{` or `[` and discards anything before it, so models that say "Here is the fixed JSON: {...}" still produce parseable output.
- 🤖 **AI Fix availability check no longer races.** v0.1.7 used `OllamaStatus::isAvailable()` which returned a stale cached value because the constructor's async `/api/tags` fetch hadn't completed yet. v0.1.8 uses `OllamaClient::isAvailable()` which is synchronous (3-second QEventLoop + QTimer) and returns the actual current state.

### Added
- 🤖 **"Show Diff" button in JSON Tools** — appears next to the AI Fix button, disabled until an AI Fix completes. Click it to open a side-by-side Compare tab showing the **original (broken) JSON on the left** and the **AI-fixed JSON on the right**. Built on the existing `CompareWidget` so colors + diff navigation work the same as the regular Compare plugin. Answers the user's question "what was changed?".
- 🤖 **JSON Tools status banner now shows the change summary** after AI Fix: `✓ AI fix complete — 98 chars (was 60, +38), 8 lines (was 2). Click 'Show Diff' to see changes.`
- 🤖 **"Show thinking" checkbox in JSON Tools and AI Assistant panels.** Default OFF for JSON Tools (thinking breaks the JSON parser); user can toggle ON if they want to see reasoning. Default OFF for AI Assistant too — toggle ON for explanation tasks where reasoning is useful.
- 💬 **AI Assistant rewritten as a proper chat-bubble UI.**
  - User prompts → right-aligned blue bubbles with `YOU` header
  - Assistant responses → left-aligned gray bubbles with the model name as header (`qwen3.5:9b`) and a teal left border
  - Streaming tokens flow into the active assistant bubble in real time
  - **Clear chat** button to wipe history
  - **Show thinking** toggle in the same row
  - Auto-scroll to bottom on new tokens
  - Errors render as red error bubbles
  - The chat persists across multiple sends so users can have a multi-turn conversation
- 🧪 **`test_aifix.cpp`** — new end-to-end test that hits a real local Ollama daemon, runs the EXACT same cleanup pipeline the JSON Tools button triggers, and verifies the result is valid JSON. Skipped (exit 0) if Ollama isn't running so CI stays green on runners without it.
- 🛠 **`scripts/bump_version.sh` no longer mangles the keyboard shortcuts table** — the version-history table insertion now requires a `| Version | Date | Highlights |` header match before inserting into the next `|---|---|---|` separator, so it can't accidentally clobber other 3-column tables.
- 📋 **Session log with smart change descriptions** — every Format / Minify / Fix+Format / AI Fix click logs an entry with the action name, before/after sizes, and a per-character description like `+2 commas, +1 brace, −4 single→double` so users see exactly what each operation changed without opening the diff view.
- 🗄 **SQL Formatter dialect dropdown** — ANSI / T-SQL (SQL Server) / PL/SQL (Oracle) / MySQL / PostgreSQL / SQLite. Each dialect adds its own keyword set on top of ANSI keywords so dialect-specific keywords paint blue when typed or pasted.
- 📎 **Attach button in AI Assistant** accepts ANY file: images (base64 → vision models), PDF (`pdftotext`), DOCX/PPTX/XLSX (`unzip` + tag strip), text/code (raw, capped at 100 KB). Vision models like llava, llama3.2-vision, qwen2-vl actually see the image; non-vision models silently ignore the field.
- ⌨ **Smart strict prompt** — model-agnostic prompt that works with Qwen3, Gemma, Llama, Mistral, Codellama, Phi, DeepSeek-R1. For Gemma/Phi-like models that ignore system prompts, the rules are also folded into the user prompt. Tells the model to PRESERVE original line order, key order, indentation — make MINIMAL changes — no reformatting.
- 🐛 **`notepatra --version`** no longer hard-coded — driven by `NOTEPATRA_VERSION` compile-time define from `CMakeLists.txt project()`. About dialog also reads it.
- 🪟 **`Plugins → ComparePlus`** menu entry — separate from `Compare (inbuilt)`, both share the same picker + widget.
- 🧪 **`test_fmtpanel_diff.cpp`** — direct unit test of `FormatterPanel::recordFix()` proving the Show Diff button enables for any action including AI Fix.
- 📁 **10 broken JSON test files** in `test-files/test01–10*.json` — verified end-to-end that AI Fix produces valid parseable JSON for all of them.

### Verified locally on Linux GUI before shipping
1. Built notepatra binary with all changes
2. Launched under `$DISPLAY=:10.0` with a broken JSON file
3. Drove the GUI via `xdotool`: opened Plugins menu → JSON Tools → captured Scintilla panel via Xlib `get_image`
4. Clicked AI Fix button (window-relative coordinates)
5. Captured 18 seconds of frames during the Ollama response
6. **Verified**: status banner went `✓ 86 chars formatted on open` → `Asking qwen3.5:9b to fix the JSON...` → `✓ AI fix complete — 98 chars`. Panel content went from broken JSON to fixed JSON with quotes around `age`, comma between `"reading"`/`"hiking"`, multi-line array.

### Verifying this release
Same as previous — SHA-256, cosign, SLSA. See `SECURITY.md`.


## [0.1.7] — 2026-04-09

### Fixed
- 🪟 **Windows MSVC compile error in `sqlfmtpanel.cpp`.** v0.1.6 introduced the SQL dialect dropdown which uses `SendScintilla(SCI_SETKEYWORDS, …, const char*)`. On MSVC x64 the call was ambiguous between two overloads — `(unsigned int, unsigned long, void*)` and `(unsigned int, uintptr_t, const char*)` — because `unsigned long` is 32-bit but `uintptr_t` is 64-bit on Windows, so neither was a strictly better match. Cast the wParam to `(uintptr_t)0` to make the `(uintptr_t, const char*)` overload an exact match. Linux/GCC accepted the original call because `unsigned long` and `uintptr_t` are the same width on Linux x64. v0.1.6 built green on Linux + macOS but failed Windows CI before publishing a release.

### Carryover from v0.1.6 (which never published)
All v0.1.6 work ships in this release — see the v0.1.6 entry below for the full list.

### Verifying this release
Same as previous — SHA-256, cosign, SLSA. See `SECURITY.md`.


## [0.1.6] — 2026-04-09

### Fixed
- 🐛 **JSON Tools / HTML Tools / Bracket Tools panels now show real-time feedback on every button press.** Previously the format buttons silently did nothing if the editor was empty when the panel was opened — `m_inputText` was set ONCE at panel-open time and any subsequent paste into the editable Scintilla output panel was ignored. Now `inputText()` reads the current panel content first, falls back to the seeded input. Every button shows status: `"Running Format on N chars..."` → `"✓ Format done — N chars, M lines"` or a clear error.
- 🐛 **JSON Tools panel: text typed into the panel was rendering white-on-white** (same root cause as the v0.1.1 Windows lexer bug). Fixed by calling `applyNotepadPlusPalette()` on the panel's lexer + explicit black foreground / white paper fallback.
- 🐛 **AI Fix (Ollama) in JSON Tools now reports status clearly.** Was silently failing if Ollama wasn't running. Now: re-checks Ollama on click, shows "Ollama not running" or "No model selected" messages, and prints `"✓ AI fix complete — N chars"` on success.
- 🎨 **Default font feels less aggressive.** Bumped Consolas from 11pt to 10pt across editor, formatter panels, SQL panel, and compare view. Removed bold from operators (`+`, `-`, `=` etc), preprocessor (`#include`, `#define`), class names, function names, and HTML tags — Notepad++'s default theme only bolds keywords + Markdown headers, not everything. Page now feels lighter.
- 🐛 **Compare picker now lists unsaved tabs with a `● unsaved` marker** so users can compare in-progress work without saving first. Untitled tabs marked `● untitled`.

### Added
- 🗄 **SQL Formatter panel now has a Dialect dropdown** with ANSI SQL, T-SQL (SQL Server), PL/SQL (Oracle), MySQL, PostgreSQL, SQLite. Each dialect adds its own keyword set on top of ANSI keywords (DECLARE/MERGE/OUTPUT for T-SQL, PLS_INTEGER/SYSDATE for PL/SQL, AUTO_INCREMENT/MEDIUMINT for MySQL, etc.) so dialect-specific keywords paint blue when typed or pasted. Switching dialects re-colourises the visible buffer immediately.
- 📊 **BIG status banner in JSON / HTML / SQL formatter panels** — colored, bold, 36px tall, replaces the silent old behavior. Every button click updates it with progress + result counts, every error shows a red banner with the reason.

### Verifying this release
Same as previous — SHA-256, cosign, SLSA. See `SECURITY.md`.


## [0.1.5] — 2026-04-09

### Fixed
- 🪟 **NSIS installer script now compiles and ships.** v0.1.4's `installers/windows.nsi` failed in CI with `Invalid command: "${GetSize}"` because `FileFunc.nsh` was included but `GetSize` was never `!insertmacro`-d, and the `!include` lines were at the bottom of the script (NSIS is single-pass — every macro must be inserted before its first use). Moved all `!include` and `!insertmacro` directives (`MUI2`, `LogicLib`, `FileFunc/GetSize`, `WordFunc/WordReplace/un.WordReplace`) to the top of the file.

### Added (carryover from v0.1.4 — never published a Release)
- 🪟 **Proper Windows installer** — `notepatra-setup-0.1.5.exe` with HKCU Uninstall registry entry, generated `uninstall.exe`, Start Menu + Desktop shortcuts, optional PATH integration. See v0.1.4 entry below for full details.
- 🪟 `install.ps1` PowerShell installer also writes the Uninstall registry key + drops `uninstall.ps1`.
- 📊 Live 3-platform download counter on website footer using 🐧 / 🍎 / 🪟 icons.
- 🐛 `notepatra --version` no longer hard-coded to v0.1.0 — driven by `NOTEPATRA_VERSION` compile-time define from CMake project version.
- 🛠 `scripts/bump_version.sh` — single-command release bump (used for the first time on this release).

### Verifying this release
Same as previous — SHA-256, cosign, SLSA. See `SECURITY.md`.


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
