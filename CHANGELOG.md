# Changelog

All notable changes to Notepatra will be documented in this file.
Format follows [Keep a Changelog](https://keepachangelog.com/) and [Semantic Versioning](https://semver.org/).

> **Gaps in the version number timeline:** v0.1.4 and v0.1.6 were tagged but never published a GitHub Release (CI failures — NSIS macro bug and Windows MSVC C2666 respectively); their content shipped in v0.1.5 and v0.1.7. v0.1.11 was prepared with a macOS dylib install_name hotfix but was rolled forward into v0.1.12 to reduce release churn — the v0.1.11 changes (install_name rewriting, QtPrintSupport force-copy, otool Homebrew-path audit, ad-hoc re-sign) ship as part of v0.1.12.

---

## [0.1.35] — 2026-04-26

Coding Mode is now AGENTIC — the AI can read files and list directories on its own. Works with **every backend**: Ollama (local), llama.cpp (local), and any OpenAI-compatible service (OpenRouter, OpenAI, Anthropic via proxy, vLLM, LM Studio).

### Added
- 🤖 **Agentic Coding Mode.** When Coding Mode is on AND a tool-capable model is selected, the chat request now carries a `tools: [read_file, list_dir]` array. The model can call `read_file("src/main.rs")` to read a workspace file or `list_dir("src")` to list a directory. Notepatra runs an agent loop: parse `tool_calls` frames out of the streaming response, execute each call against the workspace, render an inline `🔧 read_file (path) → 247 lines` card, feed the result back to the model, repeat until the model returns plain content. Multi-turn round-tripping supported.
- 🤖 **`src/ai_tools.{h,cpp}`** — new module with two tools and a layered path-safety model. `read_file` returns `cat -n` line-numbered content with offset/limit pagination (default 1500 lines, 2000-char-per-line truncation, NUL-byte binary detection). `list_dir` returns one level of entries with type+size, depth=1, hidden files included, .git/node_modules/build/target/dist/.venv/__pycache__/.cache/.gradle/DerivedData/.idea/.vs filtered out. Cap 500 entries.
- 🛡 **Three-layer path-safety** (defense in depth, per multi-editor security research):
  1. **Workspace anchor** — every tool path is canonicalized via `QFileInfo::canonicalFilePath()` (resolves symlinks). The result must equal the workspace root OR start with `canonical(workspaceRoot) + "/"`. Reject otherwise with `error_kind: outside_workspace`.
  2. **Hardcoded deny-list** — even paths INSIDE the workspace are refused if they match secret patterns: `~/.ssh/`, `~/.gnupg/`, `~/.aws/`, `~/.netrc`, `~/.npmrc`, `~/.pypirc`, `~/.docker/config.json`, `/etc/passwd`, `/etc/shadow`, `*.pem`, `*.key`, `*.pfx`, `*.p12`, `id_rsa`, `id_ed25519`, `id_ecdsa`, `id_dsa`, `authorized_keys`, `known_hosts`. Catches the symlink-to-secret case where the workspace contains a symlink pointing at `~/.ssh/id_rsa`.
  3. **Structured errors** — every tool returns `{"ok":false,"error_kind":"...","message":"..."}` so the model can recover (try a different path, give up gracefully, summarise) rather than crash the conversation.
- 🌐 **All-backends support** — works with Ollama (`/api/chat` with `tools` field), llama.cpp `llama-server` (with `--jinja`), LM Studio, vLLM, OpenRouter, OpenAI API, Anthropic via OpenRouter, Google Gemini via OpenRouter, etc. Each backend has its own wire-format quirks; the parser handles them all:
  - Ollama: atomic NDJSON tool_calls frames, `arguments` is a JSON object (not string), no `id` field — synthesized client-side.
  - OpenAI-compat: SSE delta accumulation, `arguments` arrives as fragments per `index` until `finish_reason: tool_calls`, has unique `id`s.
- 🤖 **Model allowlist** for tool-capable models — Ollama: qwen3, qwen2.5, llama3.1+, mistral-nemo, command-r+, hermes3, granite3, gpt-oss, deepseek-v3/r1, kimi-k2, glm-4, mixtral, devstral, lfm2, ministral, nemotron. Cloud: gpt-4/4o/turbo/3.5/o1/o3, claude-3+, gemini-1.5/2, mistral-large/medium/small, openai/*, anthropic/*, google/gemini, openrouter/*, x-ai/grok, deepseek/*. For OpenAI-compat backends tools are sent unconditionally — the server ignores them for non-tool models.
- 🛡 **Tool-call budget** — 25 hard cap per user turn (matches Cursor/Aider). When exhausted the model gets a structured error telling it to summarise and stop, preventing runaway loops.
- 🌡 **Temperature pinned to 0.1** for tool-bearing requests. Per Ollama/multi-editor research: high temperature produces malformed JSON in `arguments` even on tool-trained models. 0.1 is the documented sweet spot.
- 📐 **Conditional anti-tool-call layer** in `ai_systemprompt.cpp`. The pre-existing layer ("you have no tools, do not produce JSON tool calls") is now suppressed when tools are actually attached — telling the model "no tools exist" while sending tool definitions produces contradictory guidance. Replaced with a brief tool-mode preamble telling the model the tools are available and to use the structured `tool_calls` field.

### Refactored
- 🔧 **`OllamaClient::generate()`** gains a `const QJsonArray &tools = QJsonArray()` parameter. When tools is non-empty, the Ollama path switches to `/api/chat` (the messages-array endpoint that supports tools) instead of `/api/generate`. The OpenAI-compat path adds `body["tools"]` and `body["tool_choice"] = "auto"`.
- 🔧 **`OllamaClient` new method `continueWithToolResults()`** — agent-loop continuation. Appends the assistant's tool-call turn + each tool result to `m_messages` and re-POSTs to keep the conversation flowing.
- 🔧 **`OllamaClient` new signal `toolCallReceived(id, name, args)`** — emitted when tool_calls land in either backend's stream. AIPanel's agent loop listens for it.
- 🔧 **`AIPanel`** gains `handleToolCall()` + `flushPendingToolResults()` agent-loop slots, plus `m_pendingToolResults` / `m_toolCallsTotal` / `m_toolsActiveThisTurn` / `m_lastSystemPromptForTools` / `m_lastToolsArray` state. Renders inline 🔧 tool-call cards via the new `aiAddToolCallCard()` helper.

### Notes
- **15 / 15 regression tests pass.** New `test_ai_tools` covers 68 path-safety + tool-execution assertions: hardcoded deny-list pattern matching, workspace anchor enforcement, traversal blocking, read_file pagination, binary detection, list_dir junk-dir filtering, JSON-Schema tool registry shape, model allowlist coverage.
- **Coding Mode now means agentic mode.** Pre-v0.1.35 it just forced code-only output; now it ALSO unlocks file reading. Outside Coding Mode the chat panel behaves exactly as before — anti-tool-call layer stays on, no tools are sent.
- **No new dependencies.** Pure Qt5 + the existing rust_core. Tools `read_file`/`list_dir` are implemented in C++ using QFile/QDir/QFileInfo.
- v0.1.36+ deferred: `search` tool (ripgrep dependency), `write_file`/`apply_diff` (write-side tools require an approval UX that's its own feature).

---

## [0.1.34] — 2026-04-26

White-fold-margin Dark-theme bug across the formatter panels — fixed. New structural test prevents regression.

### Fixed
- 🎨 **White fold-margin strip on Dark theme** — the SQL Formatter, JSON Tools, HTML Tools, and Bracket Tools panels all called `setFolding(BoxedTreeFoldStyle, 2)` to enable code folding but never `setFoldMarginColors()`. QScintilla's default fold-margin colour is white, so on Dark theme users saw a stark white vertical strip between the line numbers and the editor body. Reported via Windows screenshot of the SQL Formatter panel. Fixed in `src/sqlfmtpanel.cpp` (added `setMarginsBackgroundColor`, `setMarginsForegroundColor`, AND `setFoldMarginColors`) and `src/fmtpanel.cpp` (added `setFoldMarginColors` in both the constructor and the `onThemeChanged` reapply path).

### Added
- ✅ **Structural fold-margin pairing test** in `test_palette.cpp`. Reads `src/sqlfmtpanel.cpp`, `src/fmtpanel.cpp`, `src/compare.cpp`, `src/editor.cpp` at test time and verifies that any file calling `setFolding(...)` with a real fold style (not `NoFoldStyle`) ALSO calls `setFoldMarginColors(...)`. Catches the v0.1.34 regression class — anyone adding a new editor panel with code folding will fail CI if they forget to theme the fold margin.

### Notes
- 14 / 14 regression tests pass. test_palette: 60 / 60 colour + structural checks (was 56; +4 fold-margin pairing checks).
- Why the structural check instead of a runtime check: Scintilla doesn't expose `SCI_GETFOLDMARGINCOLOUR` (only the SET command exists), so we can't round-trip the colour. Reading the source files at test time catches the bug class equally well and runs faster.
- No new dependencies, no schema changes, no installer changes.

---

## [0.1.33] — 2026-04-26

Linux emoji rendering fix + comprehensive per-language brand-palette test coverage. Two additional palette bugs uncovered by the new assertions and fixed in the same release.

### Fixed
- 🐧 **Linux: emoji in AI Assistant responses rendered as tofu `□`.** `Hello! 👋` from a model came back as `Hello! □` because Linux text fonts (Inter, Noto Sans, DejaVu, etc.) don't ship emoji glyphs and Notepatra's CSS family chains in `src/fonts.h` didn't list any emoji font as a fallback. Qt's HarfBuzz text shaper had nothing to fall back to for SMP-plane codepoints. Fixed by appending an emoji fallback chain (`Apple Color Emoji` → `Segoe UI Emoji` → `Noto Color Emoji` → `Twemoji Mozilla` → `Twitter Color Emoji` → `Symbola`) to both `notepatraUiCssFamily()` and `notepatraCodeCssFamily()`. Windows already worked because Segoe UI auto-pairs with Segoe UI Emoji at the OS level.
- 🐛 **Python built-ins / keyword-set-2 painted as default text instead of the brand teal.** `QsciLexerPython` style 14 description is "Highlighted identifier" — the substring `identifier` hit `npp_palette.cpp`'s identifier matcher BEFORE any keyword matcher, so built-ins fell through to `npText`. Added a `d.contains("highlighted")` branch before the identifier matcher so style 14 picks up `npKeyword2` correctly. Fix surfaced via the new test_palette assertion that `print` / `len` / `range` should paint teal `#267F99` light / `#4EC9B0` dark.
- 🐛 **SQL user-defined keyword slots painted as default text instead of magenta.** `QsciLexerSQL` style 19/20 are "User defined 1/2" (the secondary keyword slots where SSMS-style `INT` / `VARCHAR` / `COUNT` would live). The description "user defined 1" doesn't contain `keyword`, so the keyword sub-branch logic missed them. Same `d.contains("highlighted")` branch was extended to also catch `user defined` / `user-defined` so SQL style 19/20 now route to `npKeyword2` (= SSMS magenta `#FF00FF` light / `#C586C0` dark per the SQL brand override).

### Added
- ✅ **Comprehensive per-language palette test coverage** in `test_palette.cpp`. Was 38 colour checks; now 56. New assertions cover:
  - PowerShell ISE signature: `$variable` style 5 = `#FF4500`, cmdlet style 9 = `#0000FF`, alias style 10 = `#0080FF`
  - Python brand: class-name style 8 = amber `#795E26`, function-name style 9 = amber, built-in / set-2 style 14 = teal `#267F99`
  - SQL brand: user-defined keyword style 19 = magenta `#FF00FF`
  - JavaScript brand: secondary keyword style 16 = teal `#267F99`
  - Font fallback chain: both UI and code CSS chains contain `Apple Color Emoji`, `Segoe UI Emoji`, `Noto Color Emoji`
- ✅ **`check_color()` helper** in `test_palette.cpp` — saves typing the same 5-line if/else-with-printf pattern for every per-language colour assertion.
- ✅ **`LexerPowerShell` and the 5 other Notepatra-local lexer .cpp files** added to `test_palette` CMake target's source list so PowerShell style assertions can actually instantiate the lexer.

### Notes
- 14 / 14 regression tests pass. test_palette went from 38 → 56 colour checks.
- The two palette bugs (Python built-ins + SQL user-defined) were live for one release (v0.1.32) before the new tests caught them — exactly the kind of issue the user complained about ("how come we're missing testing"). v0.1.33 closes the gap.
- No new dependencies, no schema changes, no installer changes.

---

## [0.1.32] — 2026-04-26

PowerShell highlighting hotfix + per-language brand palettes (Python, SQL, JSON, JS/TS, C/C++, Bash) so commonly-used languages no longer all look like generic blue+violet. Multi-editor research across Notepad++, VS Code Dark+/Light+, PowerShell ISE, SSMS, PyCharm Darcula, and Sublime Mariana drove the per-language colour choices.

### Fixed
- 🐛 **PowerShell `New-Object`, `Get-Item`, etc. were rendering as default text.** Root cause: `LexerPowerShell::keywords(int set)` had its sets misordered against Scintilla's `SCI_SETKEYWORDS` slots. QScintilla's 1-based set N maps to Scintilla idx N-1; the old code put PowerShell comparison operators at idx 1 (Cmdlets slot), the Approved-Verbs list at idx 2 (Aliases slot), and the full Verb-Noun cmdlet names like `New-Object` at idx 4 (User1 slot). The User1 description "User-defined word 1" wasn't caught by any palette matcher, so cmdlets fell through to default text colour. Now: set 1 → Keywords (control flow), set 2 → Cmdlets (full Verb-Noun + verbs), set 3 → Aliases, set 5 → User1 (.NET type names for `[type]` literals). `New-Object`, `Get-ChildItem`, and friends now paint as cmdlets correctly.

### Changed
- 🎨 **PowerShell ISE canonical palette.** `$variable` paints OrangeRed `#FF4500` (the single most-recognisable PS colour, identical in Microsoft's ISE and the official "PowerShell ISE" theme bundled with the VS Code PowerShell extension). Cmdlets (`Get-Item`, `New-Object`) paint pure blue `#0000FF` light / `#9CDCFE` dark — also Microsoft's ISE convention. Aliases paint a distinct lighter cyan `#0080FF` light / `#9CDCFE` dark.
- 🎨 **Python brand palette.** Keywords stay blue (consensus across N++ and VS Code; PyCharm's orange is the outlier). Secondary keywords (built-ins, types) shift to teal `#267F99` light / `#4EC9B0` dark — VS Code Dark+ canonical for Python types. Class/function names paint amber `#795E26` light / `#DCDCAA` dark.
- 🎨 **SQL brand palette — SSMS signature.** Keywords blue (`SELECT`, `FROM`, `WHERE`), keyword2 paints **MAGENTA** `#FF00FF` light / `#C586C0` dark — system functions (`COUNT`, `SUM`) and data types (`INT`, `VARCHAR`, `DATETIME`) get the magenta accent that SSMS users expect. Per Microsoft's SSMS colour-coding documentation.
- 🎨 **JSON brand palette — VS Code Light+/Dark+ canonical.** Property keys (style 4) now paint `#0451A5` darker JSON-blue light / `#9CDCFE` light-blue dark — the de-facto JSON expectation. Booleans (true/false/null) paint `#0000FF` light / `#569CD6` dark (VS Code constant.language). Strings remain grey/rose, numbers orange/cyan — five clearly distinct token kinds per JSON file.
- 🎨 **JavaScript / TypeScript / CoffeeScript brand palette — VS Code Dark+ canonical.** Keywords blue, types teal `#4EC9B0` dark / `#267F99` light, function/class names amber `#DCDCAA` dark / `#795E26` light. The de-facto industry default.
- 🎨 **C / C++ / C# secondary keywords retuned to VS Code teal.** Light `#267F99`, dark `#4EC9B0`. v0.1.31 had used the N++ canonical violet for secondary keywords, but C-family is so dominantly used in VS Code that users expect VS Code's teal types.
- 🎨 **Bash secondary keywords stay violet.** Built-ins like `echo`, `cd`, `read` get the generic violet so they read as a distinct class from primary control-flow keywords.

### Added
- 🎨 **`d.contains("property")` matcher in npp_palette.cpp.** JSON property keys (`"name":`), CSS property names (`color:`), YAML keys, TOML keys all now paint as `npKeyword2` so the per-language brand colour controls the property hue. Without this matcher, JSON property keys fell through to default text.

### Refactored
- 🎨 **`src/lexer_powershell.cpp`**: `keywords(int set)` rewritten with explicit 1-based-to-Scintilla-idx-N-1 documentation. Operators (`-eq`, `-ne`) removed from the keyword sets entirely — Scintilla's PowerShell lexer detects `-word` patterns natively as operators, no keyword set needed.
- 🎨 **`src/npp_palette.cpp`**: PowerShell-specific `npVariable` / `npCmdlet` / `npAlias` updated to ISE canonical hex codes; per-language brand branches added for Python, SQL, JS/TS/CoffeeScript, C/C++/C#, Bash; JSON brand updated to VS Code Light+/Dark+; `property` matcher added before `identifier` matcher.
- 🎨 **`test_lexers_v0125.cpp`**: PowerShell keyword-set assertions rewritten to verify the correct ordering. Added: `New-Object` / `Get-Item` / `Where-Object` in set 2; `gci` in set 3; `string` / `datetime` in set 5; set 4 returns nullptr (Functions slot intentionally empty).
- 🎨 **`test_palette.cpp`**: C++ secondary keyword expectation updated to VS Code teal `#267F99` (was `#8000FF` violet); JSON keyword expectation updated to `NP_KEYWORD` (`#0000FF`, was `#0451A5`); new assertion for JSON property key `#0451A5`.

### Notes
- 14 / 14 regression tests pass. PowerShell fix verified on the actual Windows screenshot the user shared.
- Multi-editor research summary: see release_notes/v0.1.32.md for the full Notepad++ vs VS Code Dark+ vs VS Code Light+ vs PowerShell ISE vs SSMS vs PyCharm Darcula vs Sublime Mariana hex tables across 11 languages that drove these decisions.
- No new dependencies, no schema changes, no installer changes.

---

## [0.1.31] — 2026-04-26

Notepad++ canonical palette overhaul + persistent post-stream stats + privacy hotfix for website screenshots. Three user-reported issues addressed in one ship.

### Changed
- 🎨 **Palette overhaul to Notepad++ canonical 9-hue scheme.** v0.1.30 user complaint was *"keywords + actual syntax are all just shades of blue"*. Root cause: identifiers were painted blue (`#001080` light / `#9CDCFE` dark) so identifiers, keywords, and types blurred together. Operators stayed at default text colour with no distinguishing weight or hue. v0.1.31 fixes the three bugs at once:
  1. **Identifiers paint as default text colour** (black on light, sand-grey on dark) — they read as "names", not as keywords. Single change kills the "all blue" effect on every C-family language.
  2. **Operators paint navy bold** (`#000080` light) / **olive bold** (`#9F9D6D` dark) — distinct hue *and* distinct font weight, so `+ - = ( ) { } ;` don't blend with identifier text.
  3. **Secondary keywords (types) paint vivid violet `#8000FF`** (light) / **sage `#CEDF99`** (dark) — N++ canonical hues, clearly NOT blue.
- 🎨 **Light theme now uses N++ stylers.model.xml canonical hues:** keywords `#0000FF` bold, types `#8000FF` violet, comments `#008000` italic, numbers `#FF8000` orange, strings `#808080` grey, operators `#000080` navy bold, preprocessor `#804000` brown, classes `#7F0000` maroon, identifiers default text. Hex codes verified directly against `notepad-plus-plus/notepad-plus-plus` master `PowerEditor/src/stylers.model.xml`.
- 🎨 **Dark theme now uses Zenburn-derived hues from N++ DarkModeDefault.xml:** keywords `#DFC47D` warm sand bold, types `#CEDF99` sage, comments `#7F9F7F` sage-green italic, numbers `#8CD0D3` cyan, strings `#CC9393` rose, operators `#9F9D6D` olive bold, preprocessor `#FFCFAF` peach, classes `#DCDCAA` sandy-yellow, decorators `#93E0E3` light-cyan italic, identifiers `#DCDCCC` default text. Each token kind sits on a distinct hue arc — no more "all blue shades" effect.
- 🎨 **PowerShell-specific style hues retuned to N++ canonical:** variables default text (was identifier-blue), cmdlets violet `#8000FF` light / peach `#FFCFAF` dark (was olive light / sandy-yellow dark), aliases cyan `#0080FF` light / sage `#CEDF99` dark.
- 🎨 **Per-language brand overrides pruned** — removed redundant entries that just re-stated the generic blue/teal pattern (TypeScript, JavaScript, Python, C/C++, C#, SQL, Bash, Batch, Perl, Lua, Pascal, Makefile, YAML, CoffeeScript). The new generic palette covers them with clearer differentiation than the old per-language teal-types overrides did. Kept for brand identity: Rust (rust-amber), Go (Go-cyan), Swift (Xcode-pink), Kotlin (Darcula-orange), Java (IntelliJ navy), D (red brick), HTML/PHP, CSS, XML, JSON, Markdown, CMake, Ruby.

### Added
- 🤖 **Streaming stats persist after the response completes.** v0.1.30 added the live `⏱ N tok · X tok/s · Y s` label during generation but it disappeared the moment the stream ended. v0.1.31 keeps it on the bubble: `endAssistantBubble` now seeds the final streaming counts (`finalTokens`, `finalElapsedMs`) onto the `ChatMessage` so `aiAddAssistantCard` can render the same `⏱ N tok · X tok/s · Y s` line on every assistant card forever — through chat history, theme switches, transcript re-renders. When `responseStats` arrives moments later with canonical Ollama-reported `eval_count` / `prompt_eval_count`, those numbers overwrite the seeded estimates and the card re-renders with the canonical values.
- 🤖 **`aiAddAssistantCard` renders the stats line below the model header.** Same format as the live label: `⏱ N tok · X tok/s · Y s` (full triple) / `⏱ N tok · Y s` (no tok/s if elapsed < 200 ms) / `⏱ Y s` (only elapsed if no tokens reported). Hidden when no stats reported (placeholder cards during stream start, before the first token).

### Fixed
- 📦 **Privacy: website screenshots leaked filesystem paths.** User flagged that `docs/screenshots/tour.gif` (the "Notepatra in 60 seconds" hero) showed `/home/<user>/Documents/notepad-linux-native` in the Project Search Folder field across 13 of 15 frames, plus internal launch-prep filenames from the `outreach/` directory we untracked weeks ago. Two static screenshots (`editor-dark.png`, `ai-assistant.png`) had the same leak; `editor-dark.png` additionally exposed paths to a separate personal project's `.env.local` file in the Recent Files panel. v0.1.31 hotfix: removes all three leaky files from the repo, replaces them with two clean re-captures (`welcome-clean.png` shows the Welcome tab without recent files visible, `editor-code.png` shows pure C++ source with no path chrome), removes the "See it in action" GIF section entirely until a clean re-capture is ready, and updates `.gitignore` to keep stray screenshot capture dirs out of the public repo.
- 📚 **About dialog claim "100+ languages" → "100+ file types · 48 language lexers".** Audited actual count: 48 distinct lexer classes (21 always-on QScintilla + 21 optional QScintilla + 6 Notepatra-local), routing 51 language identifiers across 100+ file extensions. README and website bumped from "44" → "48" everywhere except historical changelog entries.

### Refactored
- 🎨 **`src/npp_palette.cpp`**: rewrote the generic palette block with documented N++-source citations; pruned 10 redundant per-language overrides; added bold-weight to operator branch; identifier branch now returns `npText` instead of an accent blue.
- 🎨 **`test_palette.cpp`**: updated `NP_COMMENT` (`#0E8D0E` → `#008000`), `NP_OPERATOR` (`#000000` → `#000080` + bold expectation), C++ secondary-keyword assertion (`#267F99` teal → `#8000FF` violet), and final summary message.
- 🤖 **`src/aipanel.cpp`**: `endAssistantBubble` captures `finalTokens` + `finalElapsedMs` before tearing down the live label; seeds them onto the new `ChatMessage` if the streaming counters reported anything. `aiAddAssistantCard` renders a stats label below the divider when the message has any reported stats.
- 📚 **`src/mainwindow.cpp`**: About dialog tagline updated to reflect actual count.
- 📚 **`docs/docs.html`**: themes-palette section rewritten with the canonical hex codes per token kind, citing N++ master stylers.

### Notes
- 14 / 14 regression tests pass (test_palette + 13 others).
- All assertions updated to match the new canonical colors.
- Bold + italic provide a second differentiation axis on top of hue — useful for users with reduced colour perception.
- No new dependencies, no schema changes, no installer changes.

---

## [0.1.30] — 2026-04-26

Live streaming token-rate stats on the AI assistant bubble during generation. v0.1.26 added per-response stats (`1234 tok · 123.4 tok/s · 2.3 s`) but only after the response completed. v0.1.30 makes those stats stream live during generation so users see throughput as it happens — no more "is it still working?" mystery during long responses.

### Added
- 🤖 **Live streaming stats label on the assistant card.** Shows `⏱ 145 tok · 23.4 tok/s · 6.3 s` updating every 250 ms while the model is producing output. Sits between the card header and the streaming body. Three display modes:
  - First few hundred ms (no tokens yet): `⏱ generating… 0.3 s`
  - First token through 200 ms: `⏱ N tok · X s` (tok/s suppressed to avoid divide-by-tiny-number noise)
  - 200 ms onwards: `⏱ N tok · Y tok/s · Z s` (full triple)
- 🤖 **Static post-completion stats preserved.** When generation finishes, the live label disappears and the bubble re-renders with the canonical `eval_count` / `prompt_eval_count` / wall-clock from Ollama's done frame baked into the bubble header (the v0.1.26 mechanism). So the chat history retains the final stats.
- 🤖 **Stop-button cleanup.** Hitting Stop during streaming now ends the assistant bubble cleanly (`OllamaClient::cancel()` disconnects + aborts the reply silently with no finished/error signal, which previously left the streaming card frozen mid-state with the timer still ticking).

### Refactored
- 🤖 **`AIPanel::beginAssistantBubble()`** creates the streaming-stats `QLabel` inside the assistant card's vertical layout, starts a 250 ms `QTimer` that recomputes the stats string from `m_streamingTokenCount` + `QDateTime::currentMSecsSinceEpoch() - m_streamingStartMs`.
- 🤖 **`AIPanel::streamIntoAssistantBubble()`** increments `m_streamingTokenCount` on every received token. Ollama emits one streamed chunk per token (roughly), so this is a close estimate that gets overwritten by the canonical count from `responseStats` after the stream completes.
- 🤖 **`AIPanel::endAssistantBubble()`** stops the timer and `deleteLater()`s the live-stats label.

### Notes
- The 4 Hz refresh rate (250 ms) was chosen so the display feels live but doesn't burn CPU on long responses. Each tick is just a string format + `QLabel::setText()` — sub-millisecond.
- 14 / 14 regression tests pass.
- No new dependencies, no schema changes, no installer changes.

---

## [0.1.29] — 2026-04-26

CI hotfix: `test_palette` was hardcoded against v0.1.26's palette and failed on every platform after v0.1.27 introduced per-language accent colours. Updated test assertions to match v0.1.27's intent.

### Fixed
- 🧪 **`test_palette` now passes against the v0.1.27 per-language palette.** Two assertions were checking for hardcoded colour values that v0.1.27's per-language tuning intentionally changed:
  - **C++ secondary keywords (style 16, `SCE_C_WORD2`)**: was asserting `#800080` purple, but v0.1.27's C++ branch sets it to `#267F99` teal (matches VS Code default for type names). Updated assertion to expect teal.
  - **JSON keyword (style 11, `true`/`false`/`null`)**: was asserting generic `#0000FF`, but v0.1.27's JSON branch sets it to `#0451A5` JSON-key blue (matches VS Code default JSON theme). Added a new `NP_JSON_KEYWORD` constant and updated assertion.
- 🚢 **Unblocks v0.1.27 + v0.1.28 release pipeline.** Both releases compiled cleanly on every platform but failed the regression suite at the test_palette step → release-publish job was skipped → no GitHub release published. v0.1.29 contains both the v0.1.27 lexer overhaul and the v0.1.28 dark-theme palette fix bundled with this CI fix, so the in-app updater will pull this directly (skipping v0.1.27 and v0.1.28 which never published).

### Notes
- **No code changes** — same per-language palettes from v0.1.27 / v0.1.28, same lexer dispatch, same comprehensive keyword sets. Only the test assertions were updated to match the new palette intent.
- **Dark theme palette fixes from v0.1.28 are bundled** (Lua / Perl / D / Ruby readability).
- **All 6 v0.1.26 lexer comprehensive keyword sets** from v0.1.27 are bundled.
- 14 / 14 regression tests pass.

---

## [0.1.28] — 2026-04-26

Hotfix for v0.1.27's dark-theme palette readability bugs. Four languages were assigned dark-only colour values that became unreadable on `#1E1E1E` background (e.g. Lua navy `#000080` is invisible on dark grey). Fixed with proper 3-way `monokai / dark / light` differentiation.

### Fixed
- 🌙 **Lua keywords readable on dark theme.** Was using `#000080` navy on both dark and light. Light theme keeps the classic navy (matches lua.org); dark theme now uses `#4FC1FF` (the bright Lua-blue lua.org actually uses on its dark code blocks). Light navy stays unchanged.
- 🌙 **Perl keywords readable on dark theme.** Was using `#39457E` camel-blue on both. Light theme keeps the perl.org brand colour; dark theme uses VS Code's `#569CD6` keyword blue with `#9CDCFE` for secondary keywords.
- 🌙 **D keywords readable on dark theme.** Was using `#B03A2E` brick-red on both. Light theme keeps the dlang.org brand red; dark theme uses `#FF6E6E` coral plus `#4EC9B0` teal for std-lib types.
- 🌙 **Ruby keywords softer on dark theme.** Was using `#CC342D` ruby-brand red on both, which works on light but burns the eyes on dark. Light theme keeps the brand red; dark theme uses `#FF7B72` (VS Code's ruby grammar default — softer salmon).
- 🌙 **All 23 per-language palette branches audited** for proper dark/light/monokai compatibility. Every keyword + type colour now has explicit values for all three themes (or shares a colour deliberately, like Go's `#00ADD8` cyan which works on both backgrounds).

### Notes
- Languages NOT in the per-language switch (TCL / Diff / Fortran / Verilog / VHDL / TeX / PostScript / POV / Spice / AVS / Properties / PO / IntelHex / SREC / Octave / Matlab / IDL / MASM / NASM / ASM) fall back to the generic blue+purple palette which was already 3-way themed correctly.
- No code changes outside `src/npp_palette.cpp` — same 14 lexers, same keyword sets, same lexer dispatch. Only colour values updated.

---

## [0.1.27] — 2026-04-26

Comprehensive lexer + palette overhaul covering all 44+ languages Notepatra supports. Fixes the v0.1.26 PowerShell user complaint ("not highlighting properly") and applies the same level of polish to every other language.

### Added
- 🎨 **Per-language accent palettes for 23 languages** in `src/npp_palette.cpp`. Each language now has visually distinct keyword + type colours that match the language's home IDE / docs site:
  - **Rust** → rust-amber `#DEA584` keywords (rust-analyzer / IntelliJ Rust default)
  - **Go** → Go-logo cyan `#00ADD8` keywords (go.dev playground)
  - **Swift** → Xcode pink `#FC5FA3` keywords + Swift orange `#F05138` for attributes
  - **Kotlin** → Darcula orange `#CC7832` keywords + gold `#FFC66D` types
  - **TypeScript / JavaScript / C / C++ / C# / SQL** → VS Code Dark+/Light+ defaults
  - **Java** → IntelliJ navy keywords / Darcula orange in dark
  - **HTML / PHP / CSS / XML** → tag/attribute orange-blue split
  - **JSON / YAML** → JSON-key blue
  - **Python** → blue keywords + teal types
  - **Ruby** → ruby-red `#CC342D`
  - **Perl** → camel-blue
  - **Lua** → navy
  - **Bash / Batch / CoffeeScript / D / Markdown / Pascal / CMake / Makefile** → tuned per-language
  - 20+ niche languages (TCL / Diff / Fortran / Verilog / VHDL / TeX / etc.) fall back to the generic blue+purple palette which always looked fine.
- 🎨 **5 new style-kind matchers** in `npp_palette.cpp` apply colours to lexer styles that previously fell through to plain text:
  - `variable` (PowerShell `$var`, Bash `$var`)
  - `cmdlet` (PowerShell `Get-Process` etc.)
  - `alias` (PowerShell `ls`, `gci`, `iex` etc.)
  - `here-string` (PowerShell `@"..."@`, Ruby/Perl heredocs)
  - `comment doc keyword` (`@param`, `@return`, `.SYNOPSIS`, `- Parameter:`)
  - `identifier` (modern IDE convention: soft accent rather than plain black/white)
- 🎨 **Comprehensive keyword sets** for the 6 v0.1.26 lexers, sourced from official language references and verified against rustdoc / go.dev / docs.swift.org / typescriptlang.org / kotlinlang.org / Microsoft PowerShell docs:
  - **PowerShell**: 5 keyword sets — 47 statement keywords; 60+ comparison operators (case-sensitive `c-` and case-insensitive `i-` variants); 87 cmdlet verbs (full Microsoft Approved Verbs list); ~120 cmdlet aliases (`ls cd cp mv rm cat ps gci iex` etc.); ~80 common cmdlets (`Where-Object`, `Get-Process`, `Set-Content`) plus .NET type names for `[type]` literal highlighting.
  - **Rust**: strict + reserved + 2024-edition keywords (`gen`, `union`); comprehensive std-lib types (`Vec Option Result Box Rc Arc Mutex`), trait set (`Send Sync Sized Copy Clone Drop Default Iterator IntoIterator FromIterator From Into TryFrom AsRef Deref`), conversion + comparison + arithmetic-operator traits.
  - **Go**: exactly 25 reserved words alphabetically; predeclared identifiers including Go 1.18+ generics (`any`, `comparable`) and Go 1.21+ promotions (`clear`, `min`, `max`).
  - **Swift**: Swift 6 keywords across all categories (declaration / statement / expression / pattern / concurrency / macros / ownership); std-lib types (`Bool Int String Array Optional Result Sequence Collection Codable Sendable Task Actor`); doc-comment keywords (`Parameter Returns Throws Note Warning`); SwiftUI property wrappers (`State Binding ObservedObject Published @ViewBuilder`).
  - **TypeScript**: ES reserved + TS-specific (`interface type as is asserts infer satisfies accessor using intrinsic readonly keyof never unknown`); comprehensive utility types (`Record Partial Required Readonly Pick Omit Exclude Extract NonNullable Awaited Uppercase Lowercase`); typed-array constructors; common DOM globals.
  - **Kotlin**: hard + soft + modifier keywords across all three categories (`val var fun when sealed data inline crossinline lateinit suspend`); std-lib types including unsigned arrays (`UByteArray UShortArray UIntArray ULongArray`), coroutines (`Job Deferred Flow Channel CoroutineScope Dispatchers`); KDoc tags.

### Fixed
- 🪟 **PowerShell highlighting now actually works.** The user reported `.ps1` files weren't highlighting properly even though v0.1.26 added a PowerShell lexer. Root cause: `LexerPowerShell` produces SCE_POWERSHELL_VARIABLE / CMDLET / ALIAS / HERE_STRING styles, but `applyNotepadPlusPalette()` only knew about generic style names like "keyword" / "comment" / "string" — anything PowerShell-specific fell through to default text colour. Now the palette recognises `variable`, `cmdlet`, `alias`, `here-string`, and `comment doc keyword` substrings in `description()` strings, giving every PowerShell style a distinct colour. Same fix benefits Bash (`$var` variables now highlighted) and any future custom lexer that uses these style kinds.
- 🎨 **Identifier styling improved across every lexer.** Previously identifiers fell through to plain text colour (black on light, white on dark), making them blend with operators and brackets. Now they get a soft accent — `#9CDCFE` on dark, `#001080` on light, matching VS Code's identifier colour. Better visual hierarchy across all 44 languages with zero per-language code changes (purely a generic-matcher addition).

### Notes
- **No behaviour change for non-keyword content.** Strings, numbers, comments, operators, regex literals, decorators, attributes, classes, functions all keep their existing v0.1.26 colours. Only keyword + type accents are tuned per language.
- **Tests**: 14/14 regression tests pass, including `test_lexers_v0125` (86 assertions) which validates the comprehensive keyword sets for each of the 6 new lexers.
- **No new dependencies, no schema changes, no installer changes.** Pure source-side polish.

---

## [0.1.26] — 2026-04-25

Major lexer + terminal + AI panel polish release. Six new language lexers (PowerShell, Rust, Go, Swift, TypeScript, Kotlin), 16 additional language extensions added, terminal upgraded with full 256-colour and 24-bit truecolour support plus italic/faint/strikethrough, AI panel gets per-response tokens + elapsed time stats, Coding Mode "Think" checkbox now greys out instead of vanishing, mystery circle in input bar suppressed.

### Added
- 🎨 **Six new lexer subclasses** for languages QScintilla doesn't ship lexers for. Each fills a real-world UX gap where Notepatra was previously routing files to the wrong lexer:
  - `LexerPowerShell` (subclass `QsciLexer`, wraps Scintilla `SCLEX_POWERSHELL` lexer id 88) — `.ps1` / `.psm1` / `.psd1` files now get proper PowerShell syntax highlighting (variables `$var`, here-strings `@"..."@`, cmdlets `Get-Process`, comparison operators `-eq -ne -lt -gt`). **Was previously highlighted as Batch (cmd.exe) which is catastrophically wrong** — `REM` was treated as a comment in PowerShell files, `$variables` weren't recognised at all.
  - `LexerRust` (subclass `QsciLexerCPP`) — `.rs` files. Rust keywords `fn`, `let`, `mut`, `impl`, `pub`, `crate`, `match`, `async`, `await`, `dyn`, `unsafe`, `where` and standard-library types (`Vec`, `Option`, `Result`, `Box`, `Rc`, `Arc`, `Mutex`, `Send`, `Sync`, etc.) are now highlighted. Was previously rendered as plain identifiers under the C++ lexer.
  - `LexerGo` (subclass `QsciLexerCPP`) — `.go` files. Go's 25 keywords (`func`, `package`, `import`, `defer`, `go`, `chan`, `interface`, `select`, `fallthrough`, etc.) plus predeclared identifiers (`true`, `false`, `nil`, `iota`, `make`, `new`, `len`, `cap`, `append`, `copy`, `panic`, `recover`).
  - `LexerSwift` (subclass `QsciLexerCPP`) — `.swift` files. Swift 6 keywords including concurrency (`actor`, `async`, `distributed`, `isolated`), declarations (`func`, `var`, `let`, `class`, `struct`, `protocol`, `extension`), pattern matching (`guard`, `where`), and standard-library types (`Bool`, `Int`, `String`, `Optional`, `Array`, `Dictionary`, `Result`).
  - `LexerTypeScript` (subclass `QsciLexerJavaScript`) — `.ts` / `.tsx` / `.mts` / `.cts` files. TypeScript-only keywords (`interface`, `type`, `as`, `is`, `readonly`, `keyof`, `never`, `unknown`, `infer`, `satisfies`) plus utility types (`Partial`, `Required`, `Pick`, `Omit`, `Record`, `Awaited`).
  - `LexerKotlin` (subclass `QsciLexerJava`) — `.kt` / `.kts` / `.ktm` files. Kotlin hard + soft + modifier keywords (`fun`, `val`, `var`, `data`, `sealed`, `when`, `suspend`, `inline`, `crossinline`, `companion`, `lateinit`, `tailrec`).
- 🎨 **16 new language extensions** routed to closest-fit existing lexer with custom keyword overrides where impactful:
  - Web / mobile: `.dart` (JavaScript)
  - Systems / native: `.zig`, `.zon`, `.vlang`, `.odin` (C++)
  - JVM: `.scala`, `.sc`, `.groovy`, `.gradle` (Java) — `.kt` / `.kts` now go to LexerKotlin
  - Functional: `.hs`, `.lhs` (Bash), `.ml`, `.mli` (Bash), `.fs`, `.fsx`, `.fsi` (C#), `.clj`, `.cljs`, `.cljc`, `.edn` (Lua), `.elm` (Bash)
  - Scripting: `.nim`, `.nims` (Python), `.jl` (Python), `.awk` (Bash), `.cr` (Ruby)
  - Erlang ecosystem: `.ex`, `.exs` (Ruby), `.erl`, `.hrl` (Perl)
- 🎨 **`.r` files now route to Bash lexer** (was Octave). R uses `#` comments which Bash handles correctly. A dedicated R lexer with `<-`, `%>%`, and language-specific keywords is planned for v0.1.27+.
- 📺 **Terminal: 256-colour palette support** (`38;5;N` for FG, `48;5;N` for BG). Generates the xterm 256-colour cube (16-231) + grayscale ramp (232-255). Tools that use this: `bat`, `eza`, `fzf`, `delta`, `gh`, `rich`, modern `cargo`/`npm`. Previously these tools printed `38;5;N` fragments as literal text in Notepatra's terminal — now they render as proper colours.
- 📺 **Terminal: 24-bit truecolour support** (`38;2;R;G;B` for FG, `48;2;R;G;B` for BG). Channel values clamped to `[0, 255]`. Modern CLIs increasingly emit truecolour by default.
- 📺 **Terminal: italic (`3`)**, **faint (`2`, rendered as `opacity: 0.6`)**, **strikethrough (`9`)** SGR codes now render correctly.
- 📺 **Terminal: cancel-attribute codes** (`22` cancel bold/faint, `23` not-italic, `24` not-underline, `29` not-strike, `39` default FG, `49` default BG). Tools that toggle attributes mid-line (e.g. `man`, `git diff`) now render correctly without "stuck" styling.
- 🤖 **AI Assistant: per-response tokens + elapsed time** displayed in every assistant bubble header. Format: `1234 tok · 123.4 tok/s · 2.3 s`. Captured from Ollama's `eval_count` / `prompt_eval_count` / `total_duration` fields and OpenAI-compatible `usage.completion_tokens` / `usage.prompt_tokens`. Wall-clock elapsed time is measured via `QDateTime::currentMSecsSinceEpoch()` between `generate()` and `finished()`, so it works on every backend regardless of whether stats are reported. Tokens-per-second is only shown for runs > 200 ms (avoids divide-by-tiny-number noise).
- 🧪 **Two new regression test suites** with 113 total assertions:
  - `test_lexers_v0125.cpp` (86 assertions) — verifies each new lexer reports the correct `language()` name, `lexer()` Scintilla name (PowerShell only), and non-empty keyword sets containing language-specific keywords. Verifies extension map routes `.ps1`, `.rs`, `.go`, `.swift`, `.kt`, `.ts`, etc. to the right language. Verifies `createLexerForLanguage()` returns the correct subclass.
  - `test_terminal_ansi.cpp` (27 assertions) — verifies `ansiToHtml()` handles 16-colour FG/BG, bright FG/BG, bold, italic, faint, strikethrough, underline, cancel codes (22/23/24/29), 256-colour palette (cube origin, grayscale endpoints, mid-cube), truecolour with channel clamping, real-world `bat`-style 38;5;N keyword highlighting, reset between sequences, unknown CSI sequences (J, H) not breaking the parser, HTML escaping preserved for `<`, `>`, `&`.

### Fixed
- 🪟 **Windows: `Think` checkbox no longer disappears when Coding Mode is enabled.** Previously `setVisible(!checked)` was called on the thinking checkbox when Coding Mode toggled on, which made it vanish — confusing UX. Now the checkbox stays visible but `setEnabled(!checked)` greys it out, and the tooltip explains why ("Disabled while Coding Mode is on — Coding Mode forces code-only output, so reasoning blocks would interfere with the [Apply] button paste"). Coding Mode behaviour itself is byte-identical to v0.1.25.
- 🪟 **Windows: mystery circle in AI input bar suppressed.** `m_customInput->setCornerWidget(nullptr)` removes Qt's default scroll-area corner widget which the Windows native style was painting as a small grey dot at the bottom-right of the input box. Plus zero-height styling on the vertical scrollbar's add/sub-line buttons to silence any size-grip remnant.
- 📏 **Documentation: README and website now show actual v0.1.25 release asset sizes** instead of over-promoted estimates. 6 size strings corrected across README + 6 across website. Added a clear "Download size vs installed size" callout: download is the `.msi` / `.dmg` / `.tar.gz` file, installed footprint on Windows is ~75-85 MB because the MSI extracts bundled Qt5 + QScintilla DLLs out of the compressed payload (normal for any Qt-based installer). Previously: Linux x64 was advertised at 2.9 MB, actually 2.8; macOS DMG at 26.7 MB, actually 25.5; Windows MSI at 45.6 MB, actually 43.5; etc.

### Refactored
- 🤖 **`OllamaClient::generate()` now records start time via `QDateTime::currentMSecsSinceEpoch()`** and emits a new `responseStats(int promptTokens, int evalTokens, qint64 elapsedMs)` signal alongside `finished(QString)`. Token counts default to `-1` if the backend doesn't report them; elapsed time is always populated.
- 🤖 **`AIPanel::ChatMessage` struct** gains `promptTokens`, `evalTokens`, `elapsedMs` fields. `renderTranscript()` reads them and renders the stats line in the assistant bubble header when populated.

### Notes
- **No behaviour change for Coding Mode users.** `CodingStrict` intent prompt remains byte-identical to v0.1.25. Coding Mode + any quick-action still produces code-only output, preserved indentation, no markdown fences, ready to paste.
- **No behaviour change for non-tool-calling models** (Llama 3.2, Gemma 2, Phi-3.5, Claude, GPT-4) which were already chatting normally in v0.1.25.
- **Anti-tool-call system prompt layer** from v0.1.25 retained. Combined with the v0.1.26 lexer fixes, AI-generated code now opens with proper highlighting via the [Apply] flow regardless of language.

---

## [0.1.25] — 2026-04-25

AI Assistant prompt-engineering overhaul. Fixes a long-standing failure where tool-calling fine-tuned models — Qwen3 / Qwen3.5 (all sizes) / Hermes-3 / Llama 3.1+ Instruct / Mistral Large / Command R / GLM-4 / GPT-OSS — would respond to casual chat input like `hi` with hallucinated JSON tool calls (`{"command": "echo ...", "output": "..."}`) instead of greeting back. Root cause was a combination of (a) no anti-tool-call instruction in the system prompt, (b) workspace context being attached to every request including casual greetings, and (c) the workspace block header literally beginning with `# Workspace context` which tool-calling models pattern-matched as an agent-framework prompt and decided to respond in tool-call format.

### Added
- 🤖 **`src/ai_systemprompt.{h,cpp}` — layered system-prompt builder.** Replaces the old binary "chat prompt vs Coding Mode prompt" with a 4-layer composition: identity + anti-tool-call + mode-specific + language hint. Pure functions, unit-testable, no QtWidgets dependency.
- 🤖 **`AiSystemPrompt::Intent` enum** — `Chat`, `Explain`, `Transform`, `CodingStrict`. Coding Mode now maps to `CodingStrict` with a byte-identical prompt body to v0.1.24's Coding Mode prompt — existing Coding Mode users see no behaviour change. Quick-action buttons (`explain` / `bugs` / `docs`) map to `Explain`; (`refactor` / `optimize` / `tests` / `comment` / `translate`) map to `Transform`; `custom` action with Coding Mode OFF maps to `Chat`.
- 🤖 **Anti-tool-call layer** in every system prompt: explicitly tells the model "this is a chat interface with no executable tools", names the forbidden output shapes (`{"command":...,"output":...}` and `{"name":...,"arguments":...}`), and instructs the model to describe what it would do in plain language instead of producing tool calls. Adds ~30 tokens per request — harmless for non-tool-calling models (Llama 3.2, Gemma 2, Phi-3.5, Claude, GPT-4) which ignore the redundant instruction.
- 🤖 **`AiSystemPrompt::shouldAttachWorkspace()` heuristic gate** for the workspace-context block. Returns `false` for: Coding Mode (code-only output doesn't need project tree), Explain/Transform with a non-empty selection (selection IS the context), Chat with a non-empty selection (focus on selection), Chat with a casual short message ("hi", "thanks", "ok"). Returns `true` for project-level questions detected via project keywords (`project|workspace|codebase|directory|folder|repo|files|tree`) or code-shape signals (`{}();=` punctuation, code keywords like `class`/`function`/`def`/`import`, file-extension mentions). Default conservative: when in doubt, skip — the anti-tool-call layer catches any drift.

### Fixed
- 🤖 **Casual chat ("hi") with `qwen3.5:0.8b` / `qwen3.5:2b` / Qwen3 / Hermes / Llama 3.1+ no longer produces `{"command":...,"output":...}` JSON tool calls.** The previous prompt sent the workspace context block on every message, including bare greetings. Tool-calling models pattern-matched the agent-frame-shaped header and emitted JSON because that's what their training data taught them to do. With the new builder + workspace gate + anti-tool-call layer, a casual `hi` now gets a plain-language `Hello! How can I help with your code?` response across every model family tested (Qwen3.5 0.8B/2B/4B/9B, Llama 3.1, Hermes-3, plus the existing-working Llama 3.2 / Gemma 2 / Claude path).
- 🤖 **Workspace context header rephrased** from `# Workspace context (for reference — do not echo back)` to `[Project info -- background context for the user's question, not a command]`. Less "agent frame"-shaped, less likely to trigger tool-call mode in models that pattern-match on common agent-framework prompt structures (LangChain, ReAct, OpenAI tools, Anthropic tools).
- 🤖 **Reduced wasted prompt tokens for casual chat.** Previously every message got the workspace block prepended (often 1-3 KB of file-tree + open-tab content). For `hi` / `thanks` / `ok` and similar short conversational replies, the block is no longer attached — typical reduction is 1-3 KB per message which translates to faster TTFT (time-to-first-token) and lower latency on small local models.

### Notes
- **No behaviour change for Coding Mode users.** `CodingStrict` intent prompt is byte-identical to the previous Coding Mode prompt. Coding Mode + any quick-action still produces code-only output with preserved indentation, no markdown fences, ready to paste. Anti-tool-call layer is added as cheap insurance but doesn't affect code output (code != tool calls).
- **No behaviour change for `Explain` / `Transform` quick-actions on selected code.** The selection is still treated as the focal context; the workspace block is now correctly skipped (it was redundant noise before — the model only needed to look at the selection).
- **Project-level chat ("show me my files", "what's in this codebase?") still gets workspace context.** The heuristic detects project keywords and flips `shouldAttachWorkspace()` to true.
- **No change to thinking-mode handling.** The `Think` checkbox still defaults OFF, `/no_think` is still appended to the system prompt for thinking-capable models (Qwen3, Qwen3.5, DeepSeek-R1, GLM-4-Plus). That code path (`src/ollama.cpp::generate()`) is untouched.
- **No change to vision/multimodal handling.** Image attachments via `imagesBase64` still work identically; the anti-tool-call layer applies to text output regardless of input modality.
- **No version-info or installer changes.** Same Windows / Linux / macOS binaries as v0.1.24 except for the AI panel behaviour. The Windows mojibake fixes from v0.1.24 are bundled here too (FileDescription, NSIS LegalCopyright, install.ps1 UTF-8 console).

---

## [0.1.24] — 2026-04-25

Windows-only mojibake cleanup. Two distinct UTF-8-vs-codepage bugs that surfaced on a fresh Windows 11 install: the file-association description showing `Notepatra â€" native code editor` in Explorer's "Open with" menu, and the `irm | iex` PowerShell installer banner rendering box-drawing chars as `â`/`âˆ` garbage.

### Fixed
- 🪟 **"Open with" menu no longer shows `Notepatra â€" native code editor`.** Root cause: `resources/notepatra.rc` was saved as UTF-8 (no BOM) and contained two non-ASCII characters in the resource strings — an em-dash `—` (UTF-8 `0xE2 0x80 0x94`) in `FileDescription` and a copyright sign `©` (UTF-8 `0xC2 0xA9`) in `LegalCopyright`. The Microsoft Resource Compiler (`rc.exe`) defaults to the system codepage (cp1252) when reading source files without a BOM, so each multi-byte UTF-8 sequence got reinterpreted as multiple cp1252 chars (`â€"` for the em-dash, `Â©` for the copyright). Those mojibake bytes then got transcoded to UTF-16 and embedded in the `VERSIONINFO` block — which Windows Explorer reads as Unicode and displays verbatim. Fixed by switching both strings to ASCII-only and dropping the em-dash entirely (no hyphen substitute either — the natural-language phrase reads fine without a separator): `Notepatra native code editor for the AI era` and `Copyright 2026 Prateek Singh. GPL-3.0.`. ASCII bytes survive any codepage interpretation untouched.
- 🪟 **`irm https://notepatra.org/install.ps1 | iex` banner no longer renders as `â` garbage.** Two-part fix in `docs/install.ps1`:
  1. Set `[Console]::OutputEncoding = [System.Text.Encoding]::UTF8` and `$OutputEncoding = [System.Text.Encoding]::UTF8` near the top of the script (wrapped in `try {} catch {}` so it degrades gracefully on legacy PowerShell hosts). Without this, Windows PowerShell 5.1 — which is what `irm | iex` actually runs in on a default Win11 box — falls back to the legacy OEM codepage for `Write-Host` output, mangling every multi-byte UTF-8 sequence in the script's strings (the box banner, the em-dashes in warning messages, the final `✅` success line).
  2. Belt-and-suspenders: replaced the Unicode box-drawing banner (`╔═╗║╚═╝`) with ASCII (`+===+|+`). Even if the encoding setup fails on some exotic PowerShell host, the banner stays readable.

### Notes
- Linux / macOS users are unaffected. The `.rc` file is only compiled on Windows targets, and `install.sh` runs in UTF-8 terminals by default on those platforms.
- This is a small release: no C++ source changes, no behaviour changes, no schema changes. Just two text-encoding fixes that close out the Windows polish thread that started in v0.1.20.

---

## [0.1.23] — 2026-04-25

Critical dark-theme fix on top of v0.1.22. Users with `Config::theme = "System"` on a dark OS got dark app chrome but a white editor body inside Editor / SQL Formatter / JSON / HTML / Bracket / Markdown panels — visible in the user's screenshot as a glaring white block under the dark "SQL Formatter" header.

### Fixed
- 🌙 **Editor body no longer paints white on dark theme when `theme = "System"`.** `Config::theme` stays as the literal user preference forever (we don't rewrite "System" → "Dark"/"Light" at startup so the next launch can re-detect the OS preference). Chrome stylesheets went through `applyThemeToAll(resolveTheme(name))` which DID resolve "System" — but `npIsDarkTheme()`, `applyNotepadPlusPalette()`, and `Editor::applyLexer()` compared `Config::theme` directly against `"Dark"`. `"System" == "Dark"` is false → every "is this dark?" check fell through to LIGHT-mode colours. Result: dark chrome, white editor.
- 🌙 **`theme_detect.h::npResolvedThemeName()` introduced** — returns "Light" / "Dark" / "Monokai" by resolving "System" via `detectSystemTheme()`. `npIsDarkTheme()` now uses it. Every panel that reads `Config::theme` for lexer paint now resolves first.
- 🌙 **`Editor::applyLexer()` no longer hardcodes `setPaper(QColor("#FFFFFF"))`** — paper now tracks the resolved theme so Editor instances paint dark on dark.

### Notes
- Linux user perspective: nothing changes if you're on an explicit "Light"/"Dark"/"Monokai" theme; the bug only affected "System".
- Windows / macOS user perspective: same — only "System" theme on a dark OS was affected.

---

## [0.1.22] — 2026-04-25

UI polish release for Windows. v0.1.21 introduced explicit `setSpacing()` to fix layout collisions, but the user's Windows screenshots showed several remaining truncation bugs that needed sizing fixes too. This release bundles those.

### Fixed
- 🪟 **AI dock close button no longer renders as `âœ•` mojibake on Windows.** The previous Unicode glyph (`U+2715`) wasn't in every default Windows font's coverage, so it fell back to UTF-8-bytes-as-CP1252 garbage. Switched to `QStyle::SP_TitleBarCloseButton` — the OS draws its own X icon (the same one Qt uses on its window decorations), guaranteed to render. Same fix for the Compare ✕ Close button: standard icon + plain ASCII "Close" text.
- 🪟 **Tool-panel top-line headers no longer clip on Windows.** Every tool-panel header (`SQL Formatter`, `Terminal`, `Function List`, `SOURCE CONTROL`, JSON / HTML / Bracket title, `Markdown Preview`, `Search Results`, Compare's left/right file headers) was `setFixedHeight(20-24)` which on Windows' larger font metrics + bold ascenders/descenders left the text overlapping with the row below. Switched all of them to `setMinimumHeight(26-28)` with 4 px top/bottom padding so the label can grow as Windows fonts demand. Linux Fusion / GTK keeps using its shorter computed height.
- 🪟 **Find/Replace buttons no longer truncate on Windows** ("Find All in Current Document" → "Find All in Current"; "Find All in All Opened Documents" → "nd All in All Opene"). `setFixedWidth(170/200)` was tight with Windows fonts; switched to `setMinimumWidth(210/230)` and bumped the dialog floor 580→660 px so buttons grow as needed.
- 🪟 **JSON / HTML / Bracket / SQL Formatter "Copy Output" no longer clips at the right edge** (was rendering "Copv Outout"). Right `contentsMargin` on the button row bumped 8→16 px so the rightmost button stays clear of the panel boundary.
- 🪟 **AI panel error messages no longer truncate at the right edge.** Long URLs in errors like `Error transferring https://api.openai.com/v1/v1/models · server replied:` used to wrap off the right edge; `m_statusLabel` was `setFixedHeight(14)` with no word wrap. Switched to `setWordWrap(true)` + `setMinimumHeight(18)` + `MinimumExpanding` vertical size policy so multi-line errors render in full.
- 🪟 **SQL Formatter Model dropdown no longer shows "ot connected" instead of "(not connected)".** `QComboBox` `setMinimumWidth(150)` was tight; bumped to 200 + `setSizeAdjustPolicy(AdjustToContents)` so the combo also grows for long model names like `llama3.1:70b-instruct-q4_K_M`.

### Notes
- Linux user perspective: nothing changes; layouts / fonts / heights are unaffected by these tweaks.
- macOS user perspective: nothing changes; macOS native font metrics already fit.
- Windows user perspective: every panel's headers, button rows, and the AI / SQL status displays now read in full instead of cropping at the right edge.

---

## [0.1.21] — 2026-04-25

UI polish release. Bundles two Windows-specific layout fixes that landed on `main` after v0.1.20: option-row label/checkbox collisions on every tool panel, and the AI / Compare close ✕ buttons getting clipped on Windows fonts. No Linux or macOS user sees a difference; the Windows behaviour is what changes.

### Fixed
- 🪟 **Tool-panel option rows no longer collide on Windows.** `QHBoxLayout` default spacing is ~6 px on Linux's Fusion / GTK styles but **0 px** on Windows Vista / Windows 11 styles, so any row that didn't call `setSpacing()` rendered fine on Linux and ran widgets together on Windows (the visible bug looked like `"UPPERCASE keywordsIndent:"`). Explicit `setSpacing(8 / 10 / 12)` added on every collision-prone row:
  - SQL Formatter — Dialect / UPPERCASE checkbox / Indent / Format / AI Fix / Copy Output row.
  - JSON / HTML / Bracket formatters (FormatPanel) — Show Diff / Copy Output button row.
  - Compare toolbar — Prev / Next / Recompare / Unlock Editing / Ignore-spaces / Ignore-case / Ignore-empty-lines / Stats / Close.
  - Find/Replace dialog — Find tab options, Replace tab options, Find-In-Files options, Search-Mode group, Mark tab options.
- 🪟 **AI dock close ✕ no longer clips on Windows.** The button used `U+00D7` (×, multiplication sign) at 18 px on a 28×28 button with `0 8px` padding — only 12 px of horizontal room for the glyph, and on Windows fonts the right edge of the × was visibly cropped against the hover background. Switched to `U+2715` (✕, a real X), widened the button to 36×28, zeroed padding, dropped the font to 16 px / weight 600.
- 🪟 **Compare ✕ Close button widened 70 → 84 px.** The fixed width was tight on Windows where button-chrome padding ate into the `"✕ Close"` label.

### Notes
- Linux user perspective: nothing changes; layouts already had ~6 px implicit spacing from Fusion/GTK style.
- macOS user perspective: nothing changes; macOS native style also gives sane defaults.
- Windows user perspective: every option row in the affected panels now reads with consistent gaps between widgets, and ✕ glyphs fully render.

---

## [0.1.20] — 2026-04-24

Follow-up to v0.1.19 focused on two user-reported Windows UX papercuts: double-click opened a new clone per file instead of reusing the running window, and the in-app updater's "Download & install" step stalled on "app already running" because it didn't close Notepatra before handing off to msiexec.

### Added
- 🪟 **Single-instance file-open bridge** — `main.cpp` now probes a per-user `QLocalServer` (`notepatra-<sha1(home)>`) before creating `QApplication`. If a primary is already running, the second invocation forwards its file args + `--line` as JSON and exits in ~220 ms. The primary's `newConnection` handler calls `MainWindow::handleRemoteOpen`, which opens each file as a tab, jumps to line, and `raise()`/`activateWindow()`s the window so the user's double-click feels instant. `-n` / `--new` bypasses both probe and listen for users who genuinely want a second independent window.

### Changed
- 🪟 **Updater Windows install path now closes Notepatra before handoff** — the "Ready to Install" dialog on Windows explicitly says the running copy will close so the installer can replace it, reminds the user to save open files, and the confirm button is relabelled `Close Notepatra & Install`. After `QProcess::startDetached` launches msiexec / setup.exe, `QTimer::singleShot(500, qApp, quit)` quits the app so Windows doesn't refuse the overwrite with "app already running." macOS (DMG drag) and Linux (folder open) paths are untouched — neither holds a handle on the running binary, so no quit is needed.

### Fixed
- 🪟 **"Double-clicking a file spawns a new app window" on Windows** — Explorer / right-click → Open with Notepatra now routes through the single-instance bridge and opens as a new tab in the already-running window.
- 🪟 **"App already running" stall when installing updates from inside Notepatra** — updater now closes the running copy before spawning the installer, so msiexec / NSIS gets a clean shot at `notepatra.exe`.

---

## [0.1.19] — 2026-04-24

Follow-up to v0.1.18 focused on a user-reported Windows-specific Project Search hang + a clean Notepatra wordmark across every OS-level label.

### Added
- 🔎 **Project Search per-file 30-second watchdog** — any single file that takes more than 30 s to open/read/scan is bailed on and the worker moves to the next file. Prevents one pathological file from wedging the whole scan at N % forever. Watchdog is checked inside the per-line streaming loop so large logs exit promptly.
- 🔎 **Live "⏳ stalled on: <path>" diagnostic** — if the scan progress (`done`, `total`) stays unchanged for 20 UI ticks (~2 s), the status label appends the path of any file a worker thread is currently holding. Actionable diagnostic instead of a frozen bar.
- 🔎 **Windows OneDrive / cloud-placeholder skip** — worker calls `GetFileAttributesW()` and skips files with `FILE_ATTRIBUTE_RECALL_ON_OPEN` / `RECALL_ON_DATA_ACCESS` / `OFFLINE`. Cloud-only placeholders never trigger a background WAN download when Search tries to open them. #1 root cause of the "Search froze at 12 %" report.

### Changed
- 🎨 **Comment colour on Light theme**: `#008000` → `#0E8D0E`. More prominent green against Clay paper.
- 🎨 **Comment colour on Dark theme**: `#6A9955` → `#A9B665`. Gruvbox-style olive — warm and distinct against `#1E1E1E` instead of blending with the accent teal. Monokai unchanged.
- 📏 **Progress ticker fires every 4 files** (was every 8) — smoother progress bar motion.
- 🏷️ **"Notepatra" wordmark cleanup** — OS-level labels now read just `Notepatra`, no dashes, no em-dashes, no taglines. Affects: Windows MSI `Description`, NSIS `BrandingText`, `license.rtf` title, docs footer, About dialog, Welcome hero, README.

### Fixed
- 🪟 **Windows Project Search hang around 10-20 %** — two independent causes addressed (OneDrive placeholders triggering WAN downloads, single files holding workers forever). See above.
- 🪟 **Windows unicode mojibake** — em-dashes in installer labels were being read as CP1252 by older Windows views, rendering "Notepatra — Native..." as "Notepatra(her) native...". Replaced em-dashes with ASCII hyphens in every `.wxs` / `.nsi` / `.rtf` string users actually see.

---

## [0.1.18] — 2026-04-24

SQL formatter becomes a real Claude-style AST pretty-printer (11 dialects) · Git panel rewrite turns the "status viewer" into a usable workflow tool · runtime theme switching stops producing dark-on-dark melt in every panel.

### Added
- 🧱 **SQL Claude-style AST pretty-printer** — `format_sql` replaced with an AST walker built on the `sqlparser` crate (v0.52). 11 dialects (ANSI, PostgreSQL, MySQL, SQLite, MS SQL, Snowflake, BigQuery, Redshift, ClickHouse, DuckDB, Hive). Claude-style indentation: `SELECT` columns on their own lines, `JOIN` clauses indented one level, `CASE WHEN` laddered, CTEs broken at commas. UPPERCASE/lowercase preserved through the walk, not a regex pass. Graceful fallback to whitespace normalization on parse error.
- 🤖 **AI Fix button on the SQL panel** — dialect-aware syntax repair. Sends the query + selected dialect to your LLM (Ollama / OpenAI-compatible / llama.cpp) with a constrained prompt: "fix syntax for dialect X, don't rewrite logic, return only the corrected SQL." Streams back inline. Useful for PostgreSQL pasted into MySQL tabs, or syntax errors where the parser message alone is unhelpful.
- 🌳 **Git panel — staged/unstaged tree with inline +/− buttons** per row. Click a file for the diff. Async `QProcess` backend reads `git status --porcelain=v2 --branch` for correct rename/copy/submodule detection.
- 🌳 **Git branch chip with ahead/behind counter** — header shows `main ↑3 ↓1` when diverged; click to see the full branch list.
- 🌳 **Git commit box with Ctrl+Enter** — live line-count indicator, blocks commit with a clear message when nothing is staged.
- 🌳 **Git sync row** — one-click Pull / Push / Fetch with live ahead/behind refresh.
- 🌳 **Git collapsible history + stash menu** — click-to-expand recent commits under the sync row; stash / pop / list / drop with message preserved.
- 🎨 **Runtime theme propagation — `MainWindow::themeChanged()` signal + `onThemeChanged()` slot on every panel** — AI Assistant, Project Search, Terminal, REST Client, Git Panel, Compare (diff), Markdown Preview, Hex Editor. Every panel's stylesheet blocks moved into an `applyPalette()` helper that re-reads `npPalette()` each time. Switch theme → every panel repaints immediately, no restart.
- 🧪 **`test_sqlfmt.cpp`** — 33 assertions across 11 dialects. 28 pass hard, 5 flagged aspirational (parser edge cases `sqlparser` itself doesn't round-trip).

### Changed
- 🧱 **`npc_format_sql` FFI is now 5-arg** — added a `dialect` parameter (default `"ansi"` preserves prior behavior). Callers that used the 4-arg form need to pass a dialect string or accept the ANSI default.

### Fixed
- 🎨 **Dark-on-dark melt in REST status badge, Git branch chip, Project Search scroll area** when switching themes at runtime — the cached palette fields weren't being refreshed, so the new palette's text ran against the old background. `onThemeChanged()` slots now re-cache the fields before re-styling.
- 🌳 **GitPanel rename/copy/submodule detection** — the old status parser used `git status --porcelain` (v1) which loses rename-target info. Switched to porcelain v2.

### Internal
- 🧪 **`ctest -j` is now 12/12 pass in 1.3 s** — added `test_sqlfmt` as the 12th test.
- 🦀 **Rust `sqlparser` crate added to `rust-core/Cargo.toml`** (v0.52). `rust_core` static lib linked by every test target that touches SQL formatting (`test_sqlfmt`, and transitively `test_ai_fix`).

---

## [0.1.17] — 2026-04-24

Quality-and-correctness pass on the two most-used features: **Project Search** (latent bug preventing it from running at all) and **updates** (previously dumped you at the GitHub release page — now it's Notepad++-style one-click download with SHA-256 verification and native-installer handoff).

### Added
- 🔄 **Safe in-app updater** — `src/updater.{h,cpp}`, ~360 lines. `pickAssetForPlatform()` picks the right artifact per OS/arch (Linux AppImage, macOS DMG, Windows MSI with NSIS/portable-zip fallbacks). Stream-downloads to `~/Downloads/<name>.part`, fetches `SHA256SUMS`, verifies, POSIX-atomic-renames `.part` → final name, hands off to the OS installer (`msiexec /i` on Windows, Finder drag on macOS, opens Downloads on Linux). **Running binary is NEVER modified by Notepatra itself** — only the OS installer you click through may replace it. 18-case regression test covers the asset picker + SHA256SUMS parser (including malformed hashes, `.sig` exclusion, no-false-positive substring matches).
- 🔎 **Project Search honest 0→100% progress bar** — linear from `scanned / total`, no bouncing. Full live status line: `Searching — 48 / 212 files (22%) · 28,340 lines · 3 matches · 180 ms elapsed`.
- 🔎 **Project Search live-ticking elapsed time** — 10 Hz UI timer refreshes elapsed-ms between worker events so it visibly scrolls instead of jumping per-file.
- 🔎 **Project Search rolling elapsed format** — ms → s → min → h automatically.
- 🔎 **Project Search page-level scroll** — whole tab lives in a `QScrollArea`, match tree grows to fit content, page scrollbar takes over. One scroll, not two. Matches VS Code / Cursor / Sublime.
- 🔎 **Project Search right-click context menu** on a match: Copy location (path:line:col), Copy full path, Copy match line text. Parent rows show full absolute paths.
- 🔎 **Project Search instant Cancel** — immediate "Cancelling…" feedback, worker bails at next `m_cancel.load()` checkpoint, post-cancel events ignored via phase guard so a tail event can't resume the UI.
- 🧩 **Help menu direct GitHub links** — Notepatra on GitHub, Latest Release, Report an Issue.
- 🎨 **Website vertical-timeline release cards** — every release before the latest one is a one-line row; click-to-expand. Keyboard accessible (Enter/Space).
- 🧪 **`test_updater.cpp`** (18 cases) + **`test_projectsearch_ui.cpp`** (5 headless UI assertions).

### Changed
- 💬 **AI Assistant chat bubbles rewritten as real Qt widgets** — previously rendered as HTML inside a `QTextBrowser`, which collapsed into the dark background on dark themes. Now `QFrame`-based bubbles with palette-driven backgrounds. Multi-line `QPlainTextEdit` input with Enter/Shift+Enter. Close (×) button on the dock that hides but preserves the session.
- 🪟 **Windows MSI / NSIS / portable-zip register the "Edit with Notepatra" right-click shell menu** — HKCU-only, no admin. Bundled `register-associations.bat` / `unregister-associations.bat` for portable-zip users.
- ✍️ **Dropped the "Notepad++ alternative" framing** site-wide — Notepatra is an original product in its own right.
- 📏 **Honest download sizes** — ~4 MB bare binary (not the previous blanket "5 MB"). Per-platform: ~2 MB Linux tar.gz · ~22 MB macOS DMG · ~40 MB Windows MSI/zip.
- 🎨 **Holistic theme pass** — REST Client, Terminal chrome, Find/Replace, SQL panel all wired to the central `npPalette()`.

### Fixed
- 🔎 **Project Search worker was never called** — `QMetaObject::invokeMethod(m_worker, "search", Q_ARG(ProjectSearchWorker::Params, p))` was silently failing because the type-name string `"ProjectSearchWorker::Params"` didn't match the slot's unqualified `"Params"`. Qt logged the mismatch to stderr; the search simply never ran. Switched to the lambda overload. Every search from the UI now actually runs.
- 🔎 **Ctrl+Shift+F (Find in Files) wired correctly** — menu said "Find in Files…" but called `showFind()` (tab 0) instead of `showFindInFiles()` (tab 2). One-char copy-paste bug, silent for a month.
- 🍎 **macOS TLS failure in the updater** — `macdeployqt` doesn't copy `libssl.3.dylib` / `libcrypto.3.dylib`, so `QNetworkAccessManager` couldn't reach `https://github.com`. CI now Homebrew-installs `openssl@3`, copies the dylibs into the bundle, runs `install_name_tool` to rewrite `@rpath/libssl.3.dylib` → `@executable_path/../Frameworks/libssl.3.dylib` + updates the `QtNetwork.framework` install names.
- 🪟 **Windows TLS failure in the updater** — OpenSSL DLLs weren't bundled by `windeployqt`. CI now downloads `openssl-1.1.x-Win64` from FireDaemon (after aqtinstall `tools_openssl_x64` was removed from Qt's repo and Chocolatey `openssl.light` was also removed) and copies `libssl-1_1-x64.dll` / `libcrypto-1_1-x64.dll` alongside `notepatra.exe`.
- 🧹 **Daily `docs/stats.json` workflow deleted** — was red for weeks with `E2BIG` (`$RELEASES_JSON` crossing `ARG_MAX` when passed via argv). The site's download counter reads the GitHub API at page-load; nothing consumed `stats.json` anyway.
- 🧾 **About dialog** — removed Don Ho mention per user request; trimmed to website + source links only (removed Releases / Issues links — those belong in Help menu).

### Internal
- 🧪 **`ctest -j`** up to 11/11 pass (`test_projectsearch` 27 + `test_updater` 18 + `test_projectsearch_ui` 5 added).

---

## [0.1.16] — 2026-04-20

AI Assistant becomes a proper Cursor-style dock · Project Search finally lives up to "Rust-powered" with a 10–50× speedup · terminal runs `claude` / `codex` / REPLs inline via PTY · CI back to green.

### Added
- 🤖 **AI Assistant lives in a right-side dock** (`Ctrl+Shift+A`) — persistent chat that survives tab switches. No more spawning a new editor tab per session; one conversation, always in the same place.
- 🤖 **Whole-workspace awareness** — every prompt carries the selection (or full file), every other open editor tab, the workspace root, AND a flat listing of every file under it. The AI can reason about files you haven't opened, Cursor-style. Budget-capped so small 3B models don't overflow. Skips `.git` / `node_modules` / `target` / `dist` / `__pycache__`.
- 🤖 **Coding Mode morphs the whole panel** — top strip turns accent-green with a "⌘ Coding Mode" badge + accent underline; chat body flips to monospace; quick-action grid and Insert/Replace/Copy row hide for a clean chat view. Chat history is preserved across toggles (not reset).
- 🤖 **Per-code-block "⧉ Copy code" buttons** inside every assistant reply — copy just that snippet, not the whole message. Plus a whole-response pill in each bubble header.
- 🖥️ **Terminal runs interactive CLIs inline via PTY** — `claude`, `codex`, `aider`, `gh`, `python` / `ipython`, `node`, `ssh`, `mosh`, `psql`, `mysql`, `sqlite3`, `gdb`, `lldb` now get a real TTY via `script(1)` wrapping. Input box flips to stdin-feed mode; prompt changes to `<cmd> ▷` so the mode is obvious.
- 🔎 **Rust aho-corasick wired into Project Search** — literal searches on files ≤ 8 MB go through `RustCore::findAll()` (the same aho-corasick crate ripgrep uses), mapping byte-offsets to `(line, col)` via an `upper_bound` over a pre-built line-start index. 5–50× faster than the old C++ line loop.
- 🔎 **Parallel file scanning** via `QtConcurrent::blockingMap` — Project Search now uses every CPU core. On a 4-core laptop ~3–4× faster than the serial version.
- 🎉 **Welcome tab shows the Notepatra logo** above the headline. `loadWelcomeLogo()` falls back through `QIcon::fromTheme("notepatra")` → exe-relative paths → `~/.local/share/icons/hicolor/*/apps/notepatra.png`.

### Changed
- 🎨 **AI panel header declutter** — removed redundant "Backend:" / "Model:" labels; shortened "Coding Mode" → "Coding", "Show thinking" → "Think"; the 8-button quick-action grid + Insert/Replace/Copy row now collapse behind a "▸ Quick actions" chevron by default.
- 🎨 **"Copy" in assistant bubbles upgraded** from a subtle underline-on-hover link to an accent-filled pill button — discoverability up, friction down. Softened "YOU" → "You" on user bubbles.
- 🔎 **Project Search dir-walk pruning** — skips `.git` / `.svn` / `.hg` / `node_modules` / `bower_components` / `target` / `build` / `dist` / `out` / `bin` / `obj` / `__pycache__` / `.venv` / `.cache` / `.tox` / `.mypy_cache` / `.pytest_cache` / `.ruff_cache` / `.next` / `.nuxt` / `.turbo` / `.angular` / `.gradle` / `.idea` / `.vscode` / `vendor` / `Pods` / `DerivedData` / `.terraform` / `coverage` / `.nyc_output` up-front, before walking. On this repo: 3,512 files walked → 212. Matches ripgrep's default skip list.
- ✍️ **Marketing-vs-reality honesty pass** — the README claim "Aho-Corasick search engine (Rust) — faster than regex for literal patterns" was shadowed before v0.1.16 (projectsearch.cpp never actually called the Rust core). Now it's true.
- 🪟 **`Ctrl+Shift+A` toggles the AI dock** without auto-opening the file explorer. Explorer only appears when Coding Mode is explicitly ticked.
- 📝 **.desktop file renamed** to `Notepatra` with an "inspired by Notepad++" comment (no more "clone of" framing).

### Fixed
- 🧪 **CI "Build Notepatra" is green again** on Linux + Linux-ARM. `test_llamacpp` and `test_ai_context` are now added to the explicit `cmake --build --target` list in `.github/workflows/build.yml` — before, CTest tried to run executables the job never built, so every recent push was red with "test_llamacpp ***Not Run***".
- 🖥️ **Terminal alignment fixes** — replaced `QTextEdit::append()` (which forces a blank line between writes) with cursor-positioned `insertHtml` + explicit `<br>`, so streamed output, prompt echos, and `[exit N]` markers line up on consecutive rows. Bare `cd` now means `$HOME` to match zsh/bash.
- 🖼️ **Website's "Notepatra in 60 seconds" GIF is now real** — replaced the play-button placeholder with `docs/screenshots/tour.gif` captured from a running Notepatra (Welcome tab with logo → editor with syntax highlighting → Project Search with live results → AI Assistant with Ollama auto-detected).

### Internal
- 🧪 **New test: `test_ai_context`** — 21 assertions covering the workspace-context block builder. Runs headless, no Qt Widgets dependency.
- 🏗️ **Split `src/ai_context.{h,cpp}`** out of `aipanel.cpp` — pure, testable module; no QtWidgets/QtNetwork/QScintilla dependency.

---

## [0.1.15] — 2026-04-20

Quality-of-life polish on top of v0.1.14. Theme-aware everywhere · AI Coding Mode + Backend picker · colourful ANSI terminal · modern REST client · real screenshots on the website.

### Added
- 🤖 **AI Coding Mode toggle** — ON = system prompt becomes "return ONLY modified code, no prose, no fences, preserve indentation"; Replace Selection drops clean code straight into the editor (Cursor-style).
- 🤖 **AI Backend picker** in the top bar — seven one-click presets: Ollama · llama.cpp (GGUF) · OpenRouter (cloud) · LM Studio · Jan · OpenAI · Custom. Selecting auto-fills the URL and refreshes the model list. OpenRouter now two clicks instead of buried in Settings.
- 🖥️ **Terminal ANSI SGR parser** — renders ls / grep / git / cargo / npm output in colour instead of raw `\033[32m` gibberish. Injects `CLICOLOR=1`, `FORCE_COLOR=1`, `TERM=xterm-256color`. Honours `$SHELL` env var and shows which shell is running in the banner.
- 🖥️ **zsh-style terminal prompt** — Clay-orange directory name + teal ❯ in a rounded pill, path collapsed to `~` or `.../dir1/dir2` for deep paths.
- 🌐 **Modern REST Client** — Postman/Thunder-Client layout: method dropdown + URL input + Send + Headers/Body tabs + colour-coded status badge (2xx green / 3xx blue / 4xx amber / 5xx red) showing `200 OK · 43 ms · 1.2 KB`.
- 💾 **Git SOURCE CONTROL chrome** — branch pill, commit message box, Ctrl+Enter commits, 17 old buttons collapsed behind ↻↓↑⋯ toolbar with `⋯ More` dropdown.
- 🖱️ **System theme default** for fresh installs — detects macOS `AppleInterfaceStyle`, Windows `AppsUseLightTheme`, GNOME `color-scheme`. Settings → Theme adds "System (follow OS)".
- 🆘 **Help → "How the AI Assistant works…"** entry with setup guide for all three backends.
- 📂 **Tools menu section headers** — ── AI ── · ── Search ── · ── Workflow ── · ── Formatters ──.
- 🖼️ **Real screenshots on notepatra.org** — captured via xdotool + gnome-screenshot; replaced the CSS illustration.
- 🧪 **test_llamacpp** regression test — self-contained mock OpenAI server verifies the llama.cpp / OpenAI-compat wire format (7/7 tests pass).

### Changed
- 🎨 **Every panel is now theme-aware**: AI Assistant · Search Results · Markdown Preview · Hex Editor · Formatter panels · Compare · Welcome (rebuilds on theme switch). Light theme uses Clay palette; Dark uses VS-Code-like greys.
- 🌐 **Website AI promoted** — AI is the first nav tab, AI section moved above Features, AI Assistant is the anchor card in the Features grid.

### Fixed
- AI Assistant hardcoded dark colours — now picks up Light/Dark from Config::theme.
- Search Results tree hardcoded light colours — was unreadable in Dark mode.
- Markdown Preview mixed dark/light colours — now consistent in both themes.
- Hex Editor info label hardcoded light — matches panel theme.


## [0.1.14] — 2026-04-20

The **MSI release** — Windows `notepatra-0.1.14.msi` finally ships after four WiX debug cycles (CNDL0005 → LGHT0091 → LGHT0094 → LGHT0130). Plus a wave of UX polish that stacked up since v0.1.13.

### Added
- 🪟 **Windows MSI installer** (`notepatra-0.1.14.msi`, ~37 MB). Per-machine install, `MajorUpgrade` via stable `UpgradeCode`, 27 file-type associations via unified `Notepatra.Document` ProgId, system PATH entry, Start Menu shortcut, optional Desktop shortcut, launch-after-install checkbox, Add/Remove Programs entry with icon + help URL + home URL.
- 🔍 **Project Search** — new top-level tool. `Tools → Project Search` / `Ctrl+Shift+G`. Threaded recursive search across file names AND contents. No size limit (streams line-by-line), any text-based language, exact `line:col` coordinates, double-click jumps caret to the matched character, skips binary files via NUL-byte heuristic, cancellable anytime.
- 🤖 **Universal local AI** — `Settings → Preferences → AI` now lets users pick any of: Ollama (default), llama.cpp (loads GGUF directly), or OpenAI-compat (LM Studio, Jan, vLLM, KoboldCpp, llamafile, text-generation-webui, OpenRouter, OpenAI itself). New `OllamaClient::Backend` enum dispatches per backend. Bearer-token auth for authed endpoints via `Config::aiApiKey`.
- 🎉 **Welcome tab** — first-launch UX with hero, quick actions (New / Open / Open Folder), recent files, 3×3 feature grid (each card launches its menu action), 16-shortcut keyboard reference, theme-aware, rebuilds on theme switch.
- 💾 **VS Code-style Git panel** — SOURCE CONTROL header, branch pill, commit-message `QPlainTextEdit`, big green Commit button, `Ctrl+Enter` commits.
- 🖱️ **Clickable status bar** — Language / Encoding / EOL indicators pop the matching change-menu at cursor.
- 📅 **Edit → Insert** — five date/time formats (Ctrl+F5).
- 🧰 **Portable-zip file associations** — `notepatra-windows-x64.zip` bundles `register-associations.bat` / `unregister-associations.bat`; one double-click adds Notepatra to Windows "Open with" for 28 extensions (HKCU only, no admin).

### Changed
- 📂 **Menu reorganisation**: `Tools` = every built-in feature · `Plugins` = user-installable extensions only · `Utilities` = small helpers (Hash, Measurement) · `Help` (was `?`).
- 🎨 **Theme-aware formatter panels** — JSON / HTML / Bracket / SQL tabs now read `Config::theme` at construction. Light Clay palette or Dark grays to match the editor.
- 🔀 **Compare polish** — files-identical popup + ✕ Close button + line:col pinpoint jumps + word-level LCS diff with dark mode.
- 🌐 **Website** — new Apple-inspired product showcase + pillars, scroll progress bar, sticky download CTA, scroll-triggered fade-ins, animated stat counters, full llama.cpp + OpenAI-compat documentation, truth-audit of all stats.

### Fixed
- Status bar `Pos` indicator was showing line number twice (`%1` used for both fields).
- Duplicate `Ctrl+Shift+S` shortcut — Save All moved to `Ctrl+Alt+S`.
- Auto-complete `Config::autoComplete` and `Config::autoCompleteThreshold` settings now actually take effect.
- Macro recording state survived tab switches — now ends cleanly.
- `CompareDialog` memory leak — added `Qt::WA_DeleteOnClose`.
- Welcome tab card-clipping — replaced `QPushButton` with `ClickableCard` QFrame subclass.
- macOS Tahoe silent-launch — Info.plist, entitlements.plist, hardened-runtime re-sign.

### Verifying this release
Same as previous — SHA-256, cosign, SLSA. See [SECURITY.md](https://github.com/singhpratech/notepatra/blob/main/SECURITY.md).


## [0.1.13] — 2026-04-20

Follow-up to v0.1.12 — ships the features that landed on `main` right after the v0.1.12 tag: Welcome tab, compare Files-identical popup + Close button, WiX MSI build fix, portable-zip file-association helpers, and the Apple-inspired website polish. No behaviour changes to what v0.1.12 already delivered — this release is purely additive.

### Added
- 🎉 **Welcome tab on first launch.** New users no longer see a blank "new 1" editor with no idea what Notepatra does. The Welcome tab shows: hero + tagline + version, three quick actions (New / Open file / Open folder), clickable recent files list, a 3×3 feature grid (AI · Terminal · Compare · JSON · HTML · SQL · Bracket · REST · Git) where clicking any card launches the feature directly, a 16-shortcut keyboard reference, a tip about Ollama setup, and a "Don't show again" checkbox that persists to `Config::showWelcomeOnStartup`. Theme-aware.
- 🔀 **Compare: "Files are identical" popup + ✕ Close button.** When the two files have zero differences the stats label now shows "✓ Files are identical — N lines" in green, and a modal dialog pops up once ("The two files are identical. No differences found.") with "Close comparison" / "Keep open" buttons. A new ✕ Close button on the compare toolbar works in both contexts (dialog window or tab) via a new `CompareWidget::closeRequested()` signal. Matches Notepad++'s ComparePlus behaviour.
- 🪟 **Windows MSI installer actually builds.** The WiX `CNDL0005` schema error at `installers/windows.wxs:77` (invalid `util:InternetShortcut` nested inside `<File>`) is fixed. The MSI step was silent-passing via `continue-on-error: true` in v0.1.12 so no `.msi` artifact shipped; v0.1.13 ships `notepatra-0.1.13.msi` alongside the NSIS `.exe` and portable `.zip`.
- 🧰 **`register-associations.bat` / `unregister-associations.bat`** bundled inside `notepatra-windows-x64.zip`. Portable-zip users can double-click these to register Notepatra in Windows "Open with" for 28 file extensions (HKCU only, no admin). Same UX Notepad++ gives portable users.

### Changed
- 🎨 **Website polish (Apple-inspired).** `notepatra.org` gets a CSS-only product showcase below the hero (macOS-style traffic-light chrome, file tree, syntax-highlighted code, AI Assistant panel with Ollama status indicator, subtle 3D tilt) and a new "Designed for people who write code" pillars row (⚡ Instant · 🛡 Private · ✦ Everything built-in). Hero typography refined with tighter letter-spacing (-0.03em) and larger max font-size. Palette unchanged — still Clay.
- 📚 **docs.html consistency sweep.** Removed every remaining `v0.1.9` reference from the documentation site (`notepatra-setup-0.1.9.exe` → `notepatra-setup-0.1.13.exe`, topbar "v0.1.9 docs" → "v0.1.13 docs").


## [0.1.12] — 2026-04-20

Major quality-of-life release. Fixes the macOS Tahoe silent-launch failure, adds enterprise-grade Windows MSI installer, upgrades the file compare to word-level diff with dark-mode support, and makes AI features CPU-friendly for 16 GB laptops. Five UI bug fixes included.

### Fixed
- 🍎 **macOS Tahoe (26.0) silent launch failure.** Ad-hoc-signed apps without hardened runtime + entitlements + proper Info.plist keys would silently refuse to launch on double-click — no error dialog, nothing happens. Shipped without paying Apple's $99/yr Developer ID tax: added `resources/Info.plist.in` with `LSMinimumSystemVersion=11.0`, `NSPrincipalClass=NSApplication`, `NSHighResolutionCapable`, `LSArchitecturePriority=[arm64, x86_64]`, file-type associations for 12 UTIs; added `resources/entitlements.plist` with `cs.allow-jit`, `cs.allow-unsigned-executable-memory`, `cs.disable-library-validation`, `cs.allow-dyld-environment-variables`, `files.user-selected.read-write`, `network.client`; CI now re-signs every bundled dylib / framework / plugin with `--options runtime` first, then signs the main bundle with `--options runtime --entitlements` and verifies via `codesign -v`; `install.sh` now strips ALL xattrs (quarantine + provenance + macl), re-signs with hardened runtime + embedded entitlements, and shows Tahoe-specific right-click-Open instructions based on `sw_vers -productVersion`.
- 🔧 **Status bar `Pos` indicator was showing the line number instead of the character offset.** Bug was in `statusbar.cpp:56` — `QString("Ln : %1   Col : %2   Pos : %1")` used `%1` twice. Now threads the real character position through `cursorPositionUpdated(line, col, pos)` via `SendScintilla(SCI_GETCURRENTPOS)`.
- ⌨️ **Duplicate `Ctrl+Shift+S` shortcut.** Both "Save As..." and "Save All" used it — only the first registered. Save All moved to `Ctrl+Alt+S`.
- ✏️ **Auto-completion settings had no effect.** `Config::autoComplete` and `Config::autoCompleteThreshold` existed but `editor.cpp` hardcoded `setAutoCompletionThreshold(3)`. Now reads Config and passes `AcsNone` threshold -1 when disabled.
- 🧠 **Macro recording state survived across tab switches.** Recording on tab A would continue firing on tab B because the current-tab-changed signal didn't stop recording. Now ends recording cleanly, saves the macro, and updates menu state.
- 🪟 **CompareDialog memory leak.** `dlg->show()` with no deletion. Added `Qt::WA_DeleteOnClose`.

### Added
- 🔀 **Word-level intra-line compare diff.** Previous impl used primitive common-prefix + common-suffix matching — a single middle-token change would flag the entire rest of the line. New impl tokenizes each line by word boundaries (letters/digits/underscore runs, punctuation as single tokens), runs a DP-based LCS on the token arrays, and highlights the specific tokens that differ. Removed tokens highlighted in red on the LEFT pane; added tokens highlighted in green on the RIGHT pane. Matches ComparePlus (Notepad++) convention. Pure-whitespace tokens are skipped so only meaningful words light up.
- 🌓 **Compare dark-mode support.** Entire compare panel (nav bar, line markers, margins, headers, splitter, symbol margin, intra-line indicators) now reads `Config::theme` and switches between a high-saturation dark palette (`#1E4D2B` forest green / `#5A1D1D` dark crimson / `#4A3A10` amber brown) and a punchier light palette (`#C8F0C4` / `#FBCBCB` / `#FFECB0`). Colours picked to be visually distinct from each other AND from typical syntax highlighting.
- 🪟 **Windows MSI installer.** `installers/windows.wxs` — WiX v3 authoring with per-machine install, `MajorUpgrade` via stable `UpgradeCode`, ARP entries with icon + help URL, file-type associations for 26 extensions (`.txt`, `.log`, `.md`, `.json`, `.xml`, `.yaml`, `.ini`, `.conf`, `.cfg`, `.csv`, `.py`, `.js`, `.ts`, `.cpp`, `.c`, `.h`, `.hpp`, `.rs`, `.go`, `.java`, `.cs`, `.sql`, `.html`, `.css`, `.sh`, `.ps1`), system PATH entry, Start Menu shortcut, optional Desktop shortcut, launch-after-install checkbox. CI builds it via `heat.exe → candle → light` with `continue-on-error: true` so NSIS + portable zip still ship if WiX has a hiccup. MSI gets SHA256SUMS entry, cosign signature, SLSA provenance, attached to the GitHub release.
- 🤖 **CPU-friendly Ollama defaults for 16 GB laptops.** `num_ctx` lowered from 8k to 4k (saves ~5 GB on 7B models), `num_predict` capped at 2048, `keep_alive: "5m"` added so the model stays loaded in RAM between prompts (re-loading a 3B model from disk takes 10-15s on CPU). AI panel auto-picks the smallest installed model on first run, priority: `qwen2.5-coder:3b → qwen2.5:3b → gemma2:2b → gemma3:4b → llama3.2:3b → phi3.5:3.8b → 7B models`. "No models installed" error now lists 5 small-model pull commands with sizes instead of just recommending `qwen2.5:7b`.

### Changed
- 📄 **Uninstall script at repo root.** `uninstall.sh` copied alongside `install.sh` for symmetry — users no longer have to hunt inside `docs/`.
- 🏷️ **GitHub repo metadata.** Description rewritten to `Native C++/Rust Notepad++ alternative for Linux/Windows/macOS — 5 MB binary, 100+ file types, AI-powered formatters via local Ollama, free forever.`; homepage wired to `https://notepatra.org`; 20 topics added (`notepad`, `notepad-plus-plus`, `text-editor`, `code-editor`, `ai`, `ollama`, `local-ai`, `cpp`, `rust`, `qt5`, `qscintilla`, `cross-platform`, `linux`, `windows`, `macos`, `lightweight`, `developer-tools`, `ide`, `free`, `open-source`).
- 🗺️ **Sitemap expanded** — added `docs.html`, `#install`, `#compare` URLs; bumped `lastmod` dates.
- 📖 **README + website** updated to document word-level compare + MSI installer row + Tahoe install instructions.


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
