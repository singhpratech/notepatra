# Changelog

All notable changes to Notepatra will be documented in this file.
Format follows [Keep a Changelog](https://keepachangelog.com/) and [Semantic Versioning](https://semver.org/).

> **Gaps in the version number timeline:** v0.1.4 and v0.1.6 were tagged but never published a GitHub Release (CI failures — NSIS macro bug and Windows MSVC C2666 respectively); their content shipped in v0.1.5 and v0.1.7. v0.1.11 was prepared with a macOS dylib install_name hotfix but was rolled forward into v0.1.12 to reduce release churn — the v0.1.11 changes (install_name rewriting, QtPrintSupport force-copy, otool Homebrew-path audit, ad-hoc re-sign) ship as part of v0.1.12.

---

## [0.1.90] — 2026-05-17

**Chart UX revamp — Vega-Lite v5 path for every chart type in Full flavor**, multi-metric overlay, dual-axis, universal facet, PNG/SVG/HTML/Spec export, offline (no JSDelivr in the loop), and a beefed-up Data Analyst playbook so the AI does real multi-step analysis.

### Added
- **`src/chart_spec_to_vega.{h,cpp}` (NEW)** — pure-function translator mapping Notepatra's simplified chart spec to Vega-Lite v5. No Qt widget deps — fully unit-testable.
- **Five new chart types** — `heatmap`, `density`, `regression-line`, `faceted-bar`, `error-bar`. Vega-only in Full; Lite shows "Install Charts Pack" stub.
- **Multi-metric overlay** — `"y": ["a", "b", "c"]` overlays metrics via Vega fold transform.
- **Dual-axis** — `"y2": "secondary_col"` produces a layered spec with independent right-axis Y scale.
- **Universal facet** — `"facet"` / `"row"` + `"column"` keys wrap any chart type in `{facet, spec}` via `applyFacetIfRequested()`. Composes with multi-metric overlay.
- **Export menu** — Export… dropdown replaces Save-as-PNG: PNG 1x/2x/4x, SVG, self-contained HTML, Spec JSON.
- **Offline vega-embed bundle** — `resources/vega.qrc` ships `vega.min.js` + `vega-lite.min.js` + `vega-embed.min.js` (828 KB, Full flavor only). Loaded via `qrc:///vega/*.js` — air-gapped renders, no CDN beacon.
- **DataAnalyst ANALYTICAL DEPTH** — system prompt section pushing the model through Discover → Aggregate → Decompose → Compare → Surface anomalies, plus a SQL cookbook (time bucketing, rolling windows, percent-of-total, YoY via LAG, cohort fixing, NTILE, percentile-fenced outliers) and a MULTI-DIMENSIONAL QUESTIONS section.

### Changed
- **`src/chartrender.{h,cpp}`** — dispatcher prefers Vega path when WebEngine + type supported; falls back to QtCharts otherwise. Theme bridge reads `QPalette::Window` lightness directly (no Config dep so tests stay headless-friendly).
- **`src/chart_modal.{h,cpp}`** — Export… menu wired into Vega async-export pipeline via per-call window-slot polling. QMenu QSS scoped locally to the spawning widget.
- **`src/charts/vega_chart_renderer.{h,cpp}`** — added async export API (`exportPngAsync`/`exportSvgAsync`/`exportHtmlAsync`/`exportSpecAsync`) using unique `window._notepatra_export_<slot>` polled via `runJavaScript`. Unified `m_lastSpec` field across Lite stub and Full renderer.

### Tests
- **`test_chart_spec_to_vega.cpp` (NEW)** — 50 assertions on the translator's user-visible contract: every supported type produces valid Vega-Lite v5; multi-metric overlay emits fold transform; dual-axis produces layered spec with independent y scale; universal facet wraps `{facet, spec}`; theme bridge flips colours; auto-routing `type:"bar"` + array-y to grouped-bar variant. **All 50/50 pass.**
- **`test_ai_dataanalyst.cpp`** — prompt-size cap bumped 12 KB → 18 KB to accommodate the expanded DataAnalyst playbook.
- **`ctest`** — 33/33 pass.

### Meta
- New memory rules: pure-function translator pattern · qrc-bundled WebEngine assets · async-export window-slot pattern · universal facet wrapper.

---

## [0.1.89] — 2026-05-16

**Save As detail view now shows a "Date Created" column** — the last piece of the v0.1.88 dialog UX request. v0.1.88 first attempt crashed Qt's tree view on Ctrl+S; v0.1.89 ships the column via the supported `QIdentityProxyModel` pattern with proper index mapping.

### Added
- **`src/savedialogfsmodel.h/cpp` (NEW)** — `SaveDialogDateCreatedProxy`, an identity proxy that adds one extra column with `QFileInfo::birthTime()` + ctime fallback + em-dash for non-ext4 filesystems.
- **`src/mainwindow.cpp` `configureSaveDialogUx()`** — installs the proxy on both `saveFileAs()` and close-tab Save dialogs.

### Why a proxy and not a `QFileSystemModel` subclass
`QFileDialog` builds its own `QFileSystemModel` internally; the supported extension point is `setProxyModel()`. Subclassing the model and `tv->setModel()` would break the dialog's path-bar, name-editing, and selection wiring that's hard-coded to the d-pointer's model.

---

## [0.1.88] — 2026-05-15

**Save As file-type dropdown now actually drives the saved extension.** v0.1.87 user-reported same day: dropdown populated with 72 entries but picking "Python" + typing `foo` still saved as `foo` (no extension). Root cause: `QFileDialog::setDefaultSuffix` was never wired so the selected filter didn't drive the extension.

### Fixed
- **`src/mainwindow.cpp` (saveFileAs + closeTab)** — wire `setDefaultSuffix` from the preselected filter at init AND from the `filterSelected` signal so switching mid-dialog stays in sync. Post-Accept safety net via `applySaveAsFilterSuffix()` for platform dialogs that silently ignore `setDefaultSuffix`.
- **`src/lexerutils.{h,cpp}`** — new `firstExtensionFromFilter("Python (*.py *.pyw *.pyx)") → "py"` extractor and `applySaveAsFilterSuffix(path, filter) → path-with-extension` safety net helper. Handles bare-name filters (Dockerfile / CMakeLists.txt) and "All Files (*)" correctly.

### Added
- **`test_save_as_filters.cpp`** — 16-assertion regression that drives the user-visible end-to-end contract: "select Python → save → file ends in .py". Caught the gap v0.1.87 shipped through — proxy assertions ("function called", "list populated") instead of contract assertions ("the bytes on disk match the selected filter").

### Meta
- New memory rule: **test the user-visible contract, NOT proxy properties**. Every bug fix / enhancement now needs a regression test that asserts the one-sentence user contract.

32/32 ctest pass (was 31 in v0.1.87). No other v0.1.87 changes affected.

---

## [0.1.87] — 2026-05-15

**Save As file-type dropdown + large-file load speed-up.** Two user-reported pain points: dead Save As dropdown vs Notepad++, and 100+ MB files slower than the "up to 2 GB" promise.

### Fixed

- **`src/mainwindow.cpp:1655,1680` Save As file-type dropdown was dead** — only filter passed was `"All Files (*)"`, so the bottom-of-dialog dropdown had one entry. New `buildSaveAsFilters()` helper in `src/lexerutils.{h,cpp}` returns ~72 language entries: `Python (*.py *.pyw *.pyx)`, `Markdown (*.md *.markdown *.mkd *.rmd *.rst)`, `JSON (*.json *.jsonc *.geojson *.webmanifest *.har)`, `SQL (*.sql *.ddl *.dml *.pgsql *.plsql *.tsql *.mysql *.sqlite)`, every supported language. The filter matching the current tab's language is pre-selected. Same fix in `closeTab()` "Save before close?" prompt which had NO filter at all.

### Performance

- **`rust-core/src/file_io.rs` UTF-8 fast path** — pre-fix, every file load called `UTF_8.decode(mmap_bytes)` which allocates a fresh `String` even when the mmap bytes are already valid UTF-8. Now validates via `std::str::from_utf8` (SIMD-accelerated) and passes the mmap bytes directly. Saves ~118 MB heap allocation + ~250 ms on a 118 MB file. UTF-16 / UTF-32 / Windows-1252 paths unchanged (still go through `decode_with`).
- **`rust-core/src/file_io.rs` EOL detection bounded to first 64 KB** — pre-fix, two full-text `contains()` scans of 118 MB for ~300 ms. New `detect_eol_in_bytes` is a single byte-level scan over the first 64 KB. Files with mixed EOL within the first 64 KB classify correctly; mixed EOL only after byte 65537 was never going to be common.
- **`rust-core/src/file_io.rs` + `rust-core/src/lib.rs` drop CString round-trip in FFI** — `FileLoadResult.text` was allocated via `CString::new(text).into_raw()` which scans for embedded NUL + allocates + copies + appends a NUL byte. C++ already reads via `(ptr, len)` — never needed the NUL. Now allocates a `Box<[u8]>` (no scan, no extra copy). New `npc_free_file_text(ptr, len)` reclaims the boxed slice by length. Saves another ~118 MB heap allocation + ~250 ms.
- **`src/editor.cpp` large-file editing gates** — for files > 50 MB also disable word wrap, indent guides, and edge column. Word wrap recalculates per edit; on a 118 MB file this stuttered the typing cursor. Auto-completion + brace matching gating from earlier carries forward.
- **`src/findreplace.cpp` Mark All capped to 10000 highlights on files > 50 MB** — `SCI_INDICATORFILLRANGE` invalidates a paint per call; on a 118 MB file with 100k matches the redraw stalled the UI. Result label reports the truncation: "Marked first 10000 of N occurrence(s) — capped for performance on large files".

Combined impact on a 118 MB UTF-8 file: peak RAM 700 MB → ~360 MB; load time 1.5 s → ~600 ms.

### Documentation

- New `rust-core/include/notepad_core.h` declaration for `npc_free_file_text`.
- Tests: `rust-core/src/file_io.rs` test helper `text_of` updated to read via length-prefixed buffer (was using `CStr::from_ptr` which assumed NUL-termination).

### Out of scope

- **Chunked `SCI_APPENDTEXT` load** — would drop another 236 MB of peak RAM (eliminates Qt UTF-16 detour). Needs mid-chunk BOM / encoding-boundary handling, too much surface area for same-day patch.
- **CI `strip --strip-all` step** — drops binary from 9.7 MB → 7.06 MB. Queued in `project_next_release_ci_strip_step.md`.

---

## [0.1.86] — 2026-05-15

**Factual-audit gate, BGR-bug sweep, download-size truth-up.** Correctness patch — no new features.

### Fixed

- **`src/findreplace.cpp:770` Find→Mark All indicator color** — passed raw `0x0000FF` to `SCI_INDICSETFORE` (intended blue, byte-swapped to render as red). Now BGR-packed for Tailwind blue-500 `#3B82F6`. Same root cause as v0.1.85's editor.cpp double-click highlight fix.
- **`src/merge_helper_widget.cpp:185` Merge-conflict annotation text color** — passed raw `0x00204050` (intended `#204050` dark blue, byte-swapped to render as olive-brown). Now `0x00504020` (BGR-packed for `#204050`). Pale-gold paper at line 183 was already correctly BGR-packed.
- **Download-size claims on `docs/index.html` / `docs/docs.html` / `README.md`** — 6 marketing-copy locations carried inflated sizes (3.6 / 28.7 / 42.5 / 33.7 / 38.4 MB) that didn't match actual GitHub-release-asset bytes. Reconciled to actual (3.5 / 26.9 / 40.6 / 32.2 / 36.7 MB — within ±0.05 MB of `gh release view` output). README install-table at lines 262-267 was already correct and used as the cross-check reference.
- **"~9 MB stripped" claim in live marketing** — bare binary is actually 9.71 MB AND not stripped (CI has no `strip` step). Live copy now reads "under 10 MB (~9.7 MB on Linux x64)" without the misleading "stripped" word. Forensic release-notes rows preserved unchanged.

### Added

- **`scripts/verify-download-sizes.sh`** — new gate that calls `gh release view "v$VERSION" --json assets`, then for each artifact finds the closest "X.Y MB" claim across the three docs and asserts drift ≤ ±0.15 MB. Also verifies the "stripped" claim by downloading the Linux x64 tarball and running `file ... | grep ", stripped$"`. Wired into `scripts/release-check.sh` as a Phase 2 step.

### Documentation

- **New memory `feedback_factual_audit_must_be_a_gate.md`** — post-mortem of the v0.1.85 audit miss. `feedback_release_factual_audit.md` existed since v0.1.80, but the operator skipped the audit at release time. Lesson: any memory that asks "remember to run check X every release" where X is mechanical (grep / API call / file inspection) MUST also exist as a script under `release-check.sh`. Same pattern as `feedback_memory_must_be_a_gate_not_a_post_it`.
- **New memory `project_next_release_ci_strip_step.md`** — TODO to add `strip --strip-all` to the Linux x64 CI workflow. Drops bare binary from 9.71 MB → 7.06 MB.

### Out of scope

- CI strip step itself — separate release where the change can be verified against live artifacts.
- Other 5 audit categories from `feedback_release_factual_audit.md` (architecture claims, install one-liner style, etc.) — spot-checked manually, no drift found.

---

## [0.1.85] — 2026-05-15

**Markdown Light palette overhaul, Claude-Code-blue text selection, neon-orange match highlight, and a real Scintilla byte-order bug fix.**

### Changed

- **`src/npp_palette.cpp` Markdown Light theme** — full palette replacement. After landing Solarized in v0.1.84 (then iterating through jewel-tone purple/magenta/blue on the user's couch), settled on the SSMS / Notepatra-SQL canonical palette. H1 magenta `#FF00FF`, H2 blue `#0000FF`, H3 maroon `#7F0000`, H4 navy `#000080`, H5 orange `#FF8000`, H6 green `#008000`. Body `#1f1f1f`, bold `#000000`, blockquote `#808080` italic. Reusing the SQL palette gives MD the same hue-family vocabulary the user already knows. Dark + Monokai MD palettes unchanged.
- **`src/npp_palette.cpp` Markdown inline code** — chip foreground `#1E293B` (Tailwind slate-800) on paper `#CBD5E1` (slate-300). Denser, more present "code pill" feel than the v0.1.84 red-on-cream pass.
- **`src/themes.h` Light theme selection** — `#ADD6FF` (VS Code Light+ pale wash) → `#5BC8FA` (Claude Code prompt-blue / Tailwind sky-300 cyan-tinted). Vivid enough to read as actual selection while keeping body text legible on top.

### Fixed

- **`src/editor.cpp:664-679` Scintilla indicator byte-order bug (real)** — `SCI_INDICSETFORE` expects a Win32 COLORREF (`0x00BBGGRR`), but the prior code passed `QColor::rgb() & 0xFFFFFF` directly, which is Qt's RGB order. The double-click word-match indicator had been rendering the wrong color since the feature was added — `#E8A848` clay-orange was painting as a light blue. Now BGR-packed correctly via `(blue << 16) | (green << 8) | red`. Indicator 9 also bumped to neon-orange `#FF5500` (alpha 160 fill / 255 outline) so matches pop on the page.

### Out of scope

- Dark + Monokai Markdown palettes — verified unchanged in this release.
- Selection colors for Dark + Monokai themes — Light was the one flagged as feeling pale.
- A unified BGR helper for indicator setters across editor / findreplace / compare — fix scoped to editor.cpp for now; can refactor later.

---

## [0.1.84] — 2026-05-15

**Biggest syntax-highlighting refresh since v0.1.31.** Every supported language got reserved-word coverage updated from primary vendor sources, two brand-new lexers (Plain Text + CSV) replace previous monochrome fallbacks, and the Markdown / YAML / CSS palettes got long-overdue contrast and tint work across Light + Dark + Monokai themes.

### Added

- **`src/sql_keywords.h`** — comprehensive SQL keyword union synthesised from six primary vendor sources: T-SQL (learn.microsoft.com), PostgreSQL (Appendix C), MySQL (dev.mysql.com), **DuckDB** (duckdb.org/docs/sql/keywords_and_identifiers — first release with full DuckDB SQL coverage incl. SUMMARIZE, EXCLUDE, ASOF, POSITIONAL, BERNOULLI, HUGEINT family, read_csv/read_parquet table funcs), SQLite, Oracle. Three union strings (RESERVED / BUILTIN_FUNCTIONS / BUILTIN_TYPES) feed `SCI_SETKEYWORDS` slots 0/1/4.
- **`src/lang_keywords.h`** — primary-source keyword constants for ~50 languages. Mainstream compiled (Rust, Go, C23, C++23, C#, Java 21, Kotlin, Swift, Zig, D); dynamic / scripting (Python 3.12 incl. `type` soft keyword, JavaScript ES2024, TypeScript, Ruby, PHP 8, Perl, Lua 5.4, Bash, PowerShell PascalCase cmdlets, CoffeeScript, Tcl); niche / modern (Dart, Solidity, Vala, Hack, Julia, R, Protobuf proto3, F#, Scala 3, Groovy, Apex, GDScript Godot 4, Nim, Crystal, Elixir, Cython, Mojo, HCL, Thrift, GraphQL); markup / data / config (HTML5 — 113 elements + standard attrs, XML, CSS — ~370 properties + at-rules + pseudo-classes/elements, Markdown, YAML 1.2, TOML, JSON, JSON5, Dockerfile, Makefile, Diff, Fish, Nushell, Gitconfig, BibTeX, LaTeX, PostScript, Pascal, Fortran 2018, MATLAB/Octave, SystemVerilog IEEE 1800-2017, VHDL, NASM/MASM).
- **`src/lexerutils.{h,cpp}::populateExtraKeywords(QsciScintilla*, const QString& lang)`** — sends `SCI_SETKEYWORDS` to the editor right after `setLexer()` for every primary-source-researched language. Called from `Editor::applyLexer()` between `setLexer()` and `applyNotepadPlusPalette()`.
- **`src/lexer_plaintext.{h,cpp}`** — `LexerPlainText` (NEW) custom `QsciLexerCustom` subclass. Regex-paints URLs (http/https/www), email addresses, numbers + currency, ALL-CAPS heading lines (3+ uppercase words on their own line), double-quoted / single-quoted / backtick-fenced strings. Replaces the v0.1.83 monochrome `.txt` fallback. Hard-cap `paint()` closure routes every `setStyling` call through a `wantBytes` counter to guarantee cumulative bytes never exceed `(end - start)`.
- **`src/lexer_csv.{h,cpp}`** — `LexerCsv` (NEW) custom Scintilla lexer for `.csv` / `.tsv`. Header row painted bold, data cells alternate Column A / Column B colours for striped-table visual, separators distinct (operator colour), quoted fields including `""` escapes as strings, numeric cells distinct from text cells, `#`-prefixed comment lines styled separately. Same hard-cap pattern as Plain Text.
- **`samples/`** — 74 rich exemplar files (one per supported lexer) totalling ~128 KB. All synthetic — `alice@example.com` / Alice/Bob/Carol placeholders / RFC-5737 doc IPs only. Acts as eyeball-verification corpus and palette regression fixture.

### Fixed

- **`src/lexer_extras.cpp`** — 9 confirmed keyword bugs from primary-source audit. Dart adds `augment` + `Record` (Dart 3); Solidity adds `blockhash` + `blobhash` (EIP-4844, Solidity 0.8.24+); Julia adds `outer` + `public` (1.11) + `Float16`; Protobuf adds WKT siblings (`BytesValue`, `UInt32Value`, `UInt64Value`); F# adds `ValueOption` + `ValueTuple` (4.5+); HCL adds template directives `if` / `else` / `endfor` / `endif`; GDScript adds `@rpc` + `@tool` + `@warning_ignore` + `namespace` + `Rect2i` + `Vector4i`; Mojo adds `ref` (24.x reference binding); Groovy fixes typo `yields`→`yield` and adds `volatile`. R and Crystal verified already correct.
- **`src/npp_palette.cpp` Markdown branch** — H1–H6 contrast gradient. Previously all six header levels shared a single `npKeyword` colour via the generic `d.contains("header")` matcher. Now H1 strongest bold, H6 lightest italic; uses existing per-theme palette variables so Light / Dark / Monokai inherit automatically.
- **`src/npp_palette.cpp` Markdown branch** — inline `code` and ```code blocks``` get a paper-tint chip (`#F4F4F4` Light / `#2A2A2A` Dark / `#3E3D32` Monokai) so they visually read as code chips, not just coloured text.
- **`src/npp_palette.cpp` YAML branch** — unquoted values (style 0 / Default) recoloured to a desaturated value-tint (`#5A5A5A` Light / `#B8B89F` Dark / `#E6DB74` Monokai). Previously values rendered as pure black/sand because QsciLexerYAML doesn't tokenize them as strings; they now pop visually distinct from keys (still JSON-blue) and prose.
- **`src/npp_palette.cpp` Markdown Default-style** — body prose recoloured to a softer tint (`#2D2D2D` Light / `#C8C8B8` Dark) so structural tokens stand out from prose without competing.
- **`src/npp_palette.cpp` CSS matcher chain** — new branch for `d.contains("pseudo")` → `npClassName`. `:hover` / `:focus-within` / `:has` / `::before` / `::after` etc. previously fell through to identifier-default; now distinct.
- **`CMakeLists.txt` test infrastructure** — three previously-stale "Not Run" tests (`test_network_policy`, `test_chart_types`, `test_fontpack`) switched from bare `add_test()` to `notepatra_add_qt_test()` so they auto-build in the `notepatra_all_tests` meta-target and get `QT_QPA_PLATFORM=offscreen` for free. Four dependent test executables (`test_options_actually_work`, `test_lexers_v0125`, `test_lexer_smoke`, `test_compare_widget`) updated to link `src/lexer_plaintext.cpp` + `src/lexer_csv.cpp` for the new constructors. `test_lexers_v0125.cpp` Plain Text assertion inverted to match v0.1.84 behaviour. **31/31 ctest pass** (was 28/31).
- **`CMakeLists.txt` HEADERS list** — `src/lexer_plaintext.h` + `src/lexer_csv.h` added so AUTOMOC generates their `Q_OBJECT` vtables (the project uses an explicit HEADERS list rather than GLOB, so new Q_OBJECT classes must be registered).

### Changed

- **`src/editor.cpp::applyLexer`** — calls `populateExtraKeywords(this, language)` between `setLexer(lexer)` and `::applyNotepadPlusPalette(lexer, font, themeName)` so curated keyword lists are loaded before palette colours are applied.
- **Extension map** — `.txt` → `Plain Text` (now triggers `LexerPlainText`), `.csv` / `.tsv` → `CSV` (triggers `LexerCsv`), `.svg` → `XML`, `.html5` → `HTML`.

---

## [0.1.83] — 2026-05-14

**Docs-only release. Same binary as v0.1.82. Sweeps eleven user-facing v0.1.81 strings on the website that v0.1.82 left stale, and wires a stale-version-ref scanner into `release-check.sh` so the same drift can't ship silently again.**

### Fixed (docs)

- **`docs/index.html`** — eleven user-facing v0.1.81 strings swept to v0.1.83 (hero badge, sticky download CTA `aria-label` + visible text, main download section label, both "Get Notepatra v…" buttons, JSON-LD FAQ entries "Latest v0.1.81 download sizes" + "as of v0.1.81", page-body lead "As of v0.1.81: 226 file types · 92 language lexers", "Latest v0.1.81 download sizes" body prose, "as of v0.1.81 — Python, JavaScript, …" lexer paragraph).
- **`docs/index.html`** — JSON-LD `softwareVersion` 0.1.82 → 0.1.83.
- **`docs/index.html`** — latest version-card swapped from v0.1.82 → v0.1.83 (per the v0.1.76 single-card website policy).
- **`docs/docs.html`** — six v0.1.82 references swept to v0.1.83 (page tag header "v0.1.82 docs", "Latest release is v0.1.82", four "as of v0.1.82" prose lines).
- **`README.md`** — v0.1.82 references swept to v0.1.83 in download sizes / `.deb` filename / `--version` self-id / hero introduction. New v0.1.83 row added at the top of the releases table.

### Added (gate)

- **`scripts/stale-text-check.sh`** — new "stale version-ref sweep" section. Enumerates the user-facing phrases that must always contain the current `$VERSION` and fails the release if any contains an older version. Templates use a `__V__` placeholder substituted twice: once with the dot-escaped current version literal, once with the any-version regex `0\.1\.[0-9]+`. Match counts are compared per phrase. CHANGELOG entries / release-notes / past release-card descriptions are out of scope (only the enumerated user-facing phrases are scanned).

### Unchanged from v0.1.82

- All security hardening from v0.1.82 carries forward unchanged: install-script hard-fail SHA, cosign verification on install + updater, 38/38 actions SHA-pinned, `install-canary.yml`, AI Base URL validation, credential scrubber patterns, tag protection ruleset, Dependabot security updates, `cargo clippy -D warnings` + `cargo fmt --check` + `cargo audit --deny warnings` release gates, tab-numbering fix.
- 31/31 ctest pass. Same 51 signed artifacts shipped per platform.

---

## [0.1.82] — 2026-05-14

**Security hardening sweep (6 attack surfaces audited) + tab-numbering fix. No UX changes beyond the AI Base URL warn-confirm dialog. Safe drop-in upgrade.**

### Fixed

- **`src/mainwindow.cpp::newFile()` + `updateTabTitle()`** — untitled tab names no longer "go back" when a lower-indexed tab is closed, and no longer collide with names restored from session. Two cooperating bugs: (1) `updateTabTitle()` re-derived the displayed name from `index + 1` on every modification event, so closing "new 1" made the next keystroke in the existing "new 2" rename it to "new 1"; (2) `m_newCount` was a plain `int = 0` that didn't survive restart, so session restore brought back "new 5" then Ctrl+N created "new 2" — lower than the visible saved tab. `newFile()` now scans visible labels and assigns `max(existing N) + 1`; `updateTabTitle()` preserves the assigned label for untitled tabs (only toggles the `*` modification marker).

### Changed (security)

- **`docs/install.sh` + `docs/install.ps1` + top-level `install.sh`** — SHA-256 verification is now hard-fail. Previously the script soft-failed (silently skipped verification) if the `SHA256SUMS` fetch returned a network error or 404. An attacker on a hostile network who could block just the sums file while letting the binary through would bypass verification entirely. Both scripts now refuse to install if sums are unreachable or the artifact is not listed. `curl --proto '=https' --tlsv1.2` on every fetch, `umask 077` before `mktemp -d`.
- **`docs/install.sh` + `docs/install.ps1` + `src/updater.cpp`** — when `cosign` is on `PATH`, the install scripts and auto-updater now run `cosign verify-blob` with the cert-identity pinned to a literal workflow + tag (was: a regex matching any workflow in the repo). Releases pre-v0.1.60 without `.sig`/`.pem` assets gracefully fall back to SHA-only.
- **`src/preferences.cpp`** — AI Base URL validation. Rejects malformed URLs, rejects plain `http://` to public hosts (API key would be sent in plaintext), and pops a warn-confirm dialog for any host outside the vendor allowlist (OpenAI, OpenRouter, Anthropic, Google AI, Ollama, Mistral, Groq, Cohere, Azure OpenAI). Closes a known API-key-exfiltration phishing vector ("use this faster mirror" URLs).
- **`src/credscrub.cpp`** — seven new credential patterns redacted before prompts leave the app: Cloudflare `CFPAT-`, DigitalOcean `dop_v1_`, npm `npm_`, Twilio `SK…`/`AC…`, Azure Storage `AccountKey=`, GCP service-account JSON, HTTP `X-API-Key` / `Authorization` headers.
- **`.github/workflows/build.yml` + `codeql.yml` + `quality.yml`** — all 38 of 38 `uses:` lines are now SHA-pinned (was 0/38). Highest-risk three (`actions/attest-build-provenance`, `softprops/action-gh-release`, `sigstore/cosign-installer`) pinned first. Six `dtolnay/rust-toolchain@stable` (a *branch* ref) also pinned by SHA.
- **`.github/workflows/install-canary.yml`** — new daily + on-release workflow that diffs the live `notepatra.org/install.sh`/`.ps1` against the repo's `docs/` copies. Opens an issue on drift (catches Pages / DNS / registrar compromise).
- **`SECURITY.md`** — reconciled with the live repo ruleset. Previously claimed "branch protection: required reviews" — the ruleset has `required_approving_review_count: 0` (solo-maintainer pattern). The documented cosign verify command is also tightened from a permissive regex to a literal workflow + tag pin.
- **`scripts/release-check.sh`** — three new required gates: `cargo clippy --all-targets --all-features -- -D warnings`, `cargo fmt --check`, `cargo audit --deny warnings` (auto-installs `cargo-audit` if missing).
- **Repo settings (via `gh api`)** — Dependabot security updates enabled (was disabled). Tag protection ruleset added for `refs/tags/v*` (no deletion, no force-push, signed-tag required).
- **`src/fontpack.h` + `fontpack.cpp`** — added `expectedSha256` field to `Entry`; the installer verifies downloads against the pinned hash when set. SHAs themselves will be populated per font in follow-up commits.

### Internal

- **`docs/index.html`** — removed the stale `YOUR_BING_VERIFICATION_TOKEN_HERE` Bing Webmaster placeholder meta tag.

### What still needs the maintainer

Five external actions can't be done from CI and are tracked in
`SECURITY.md`: hardware-key (FIDO2) 2FA on the GitHub account, CAA DNS
record on `notepatra.org`, DNSSEC + registrar-lock at the registrar,
CT-log monitoring.

---

## [0.1.81] — 2026-05-14

**Polish + housekeeping release. Linux updater dialog fix + GitHub Actions / RustCrypto / sqlformat dependency refresh. No new features, no UX changes.**

### Fixed

- **`src/updater.cpp::installReleaseInteractive()`** — Linux confirm dialog is now file-suffix-aware. Pre-v0.1.81 it hard-coded *"move the new AppImage into place"* regardless of what was downloaded. We ship the `.AppImage` for Linux x86_64 only; on **Linux ARM64** the picker (`pickAssetForPlatform()`) falls back to `.tar.gz` at priority 30, so ARM64 users were told to move an AppImage they didn't have. (The original symptom in issue #12 was on v0.1.17, which shipped no AppImage at all — the same fallback path.) Now `.appimage` gets the original wording (plus a `chmod +x` reminder for file managers that strip the executable bit); `.tar.gz` / `.tgz` / `.tar.xz` gets a copy-paste-ready `tar xzf … && mv notepatra ~/.local/bin/` example; anything else gets a generic fallback. Closes #12 (finding 2). Note: the dialog is rendered by the *currently-running* binary, so Linux ARM64 users still see the old "AppImage" text when upgrading v0.1.80 → v0.1.81; the fix kicks in from v0.1.81 → v0.1.82 onward. Linux x86_64 users get the AppImage path (correct text both before and after).

### Changed (dependency refresh)

- **GitHub Actions matrix bumped past the Node 20 cliff.** `actions/checkout` v4 → v6 (#5), `actions/download-artifact` v4 → v8 (#4), `github/codeql-action` v3 → v4 (#8). Same release pipeline, same 51 signed artifacts, same cosign + SLSA guarantees. `actions/upload-artifact` v4 → v7 (PR #2) is still queued — rolls in with v0.1.82.
- **RustCrypto family to 0.11.** `md-5` 0.10 → 0.11 (#6), `sha1` 0.10 → 0.11 (#7), `sha2` 0.10 → 0.11 (#11). The 0.10 → 0.11 release moved the digest output type from `generic-array::GenericArray` to `hybrid-array::Array`, which no longer implements `LowerHex`. Updated `rust-core/src/hash.rs::compute_hash()` to call `.as_slice()` and run a small `hex_encode()` helper instead of `format!("{:x}", …)`. **MD5 / SHA-1 / SHA-256 / SHA-512 hex output is byte-identical to v0.1.80** — the wire format hasn't changed.
- **`sqlformat` 0.3.5 → 0.5.0** (#9). The upstream `FormatOptions` struct grew six new fields (`dialect`, `inline`, `joins_as_top_level`, …); switched the struct literal in `rust-core/src/sql_fmt.rs` to use `..FormatOptions::default()` so future field additions don't keep breaking this site. The three fields we drive from user prefs (`indent`, `uppercase`, `lines_between_queries`) stay explicit.
- **`libc` 0.2.183 → 0.2.184** (#10). Routine patch bump.

### Internal

- **`cargo fmt` sweep** across `rust-core/` — `bracket_fix.rs`, `file_io.rs`, `json_fmt.rs`, `lib.rs`, `sql_fmt.rs` had pre-existing formatting drift (191 LoC reformatted, zero behaviour changes). Surfaced by the `rust-quality` CI gate on the dep-upgrade PR.
- **`cargo clippy -- -D warnings` clean-up.** Four pre-existing lints fixed: `explicit_auto_deref` in `file_io.rs`, `if_same_then_else` and `redundant_closure` in `sql_fmt.rs`, two `collapsible_match` instances in `json_fmt.rs`. All discovered via the CI gate; no new lints from this release.

---

## [0.1.80] — 2026-05-14

**Two paper-cut fixes: Windows `.txt` icons stay as Notepad's after install + Search panel Clear button hides the ✕ along with the results.**

### Fixed

- **`installers/windows.wxs`** — `<ProgId>` now sets `Icon="NotepatraExe" IconIndex="0"`, which generates the missing `HKCR\Notepatra.Document\DefaultIcon\(Default) = "...\notepatra.exe,0"` registry value. Pre-v0.1.80 the MSI registered the ProgId tree for `.txt` (and every other extension) but without `DefaultIcon`. For most extensions Windows synthesised a fallback icon from the verb target; for `.txt` specifically Windows has a special-cased fallback to the cached `txtfile`/Notepad icon, so `.txt` files kept the old Notepad icon after Notepatra was set as default. Fix matches what the sideloaded `installers/register-associations.bat` already wrote (`reg add "...\Notepatra.Document\DefaultIcon" /ve /d "\"%EXE%\",0" /f`).
- **`src/searchresults.cpp::clear()`** — clicking **Clear** in the Project-Search results header no longer hides the ✕ close button. Pre-v0.1.80 `clear()` hid both Clear and ✕, on the (v0.1.46-era) assumption that "empty results panel + floating ✕" was UI noise; in practice that collapsed two distinct user actions (Clear = wipe, ✕ = dismiss) into one state with no way back. Only the Clear button hides itself now (nothing left to clear); the ✕ stays visible so the user can dismiss the empty panel themselves.

---

## [0.1.79] — 2026-05-14

**Double-click-from-file-manager focus handoff fix — Linux X11 and Windows.**

### Fixed

- **`src/mainwindow.cpp::handleRemoteOpen` (Linux X11)** — double-clicking a file in Nemo / Files / any X11 file manager now transfers focus to the running Notepatra reliably. Pre-v0.1.79 the new tab opened in the background while the file manager stayed focused. Faithful port of wmctrl's `activate_window` sequence: `_NET_ACTIVE_WINDOW` ClientMessage (source=2 / pager) + `xcb_map_window` + `xcb_configure_window(STACK_MODE_ABOVE)` + round-trip fence (`xcb_get_input_focus_reply`) before disconnect. The `xcb_configure_window` ConfigureRequest path is what bypasses Cinnamon/Muffin's focus-stealing prevention — plain activate ClientMessages alone get demoted to `_NET_WM_STATE_DEMANDS_ATTENTION`.
- **`install.sh` / `docs/install.sh`** — `StartupNotify=false` in the generated `.desktop` stops the infinite busy-cursor spinner that ticked until the WM's 15 s timeout when the single-instance bridge forwarded the path and exited without mapping a window. Spec-compliant `remove: ID="<id>"` startup-notify message sequence also wired in `src/main.cpp::sendStartupNotifyComplete` (with quoted format per the freedesktop spec and a round-trip fence) for other DEs that respect the protocol — Cinnamon ignores it in practice, so the `.desktop` flag is what fixes it for the most affected user base.
- **`src/main.cpp` + `src/mainwindow.cpp` (Windows)** — second-instance process calls `AllowSetForegroundWindow(ASFW_ANY)` before exiting so the running instance's `SetForegroundWindow()` succeeds instead of just flashing the taskbar (Explorer hands foreground rights to the newly spawned process, not the running one). Belt-and-braces TOPMOST flip (`HWND_TOPMOST` → `HWND_NOTOPMOST` with `SWP_NOACTIVATE`) guarantees z-order, then `SetForegroundWindow`.
- **`src/mainwindow.h`** — `handleRemoteOpen` signature extended to take an optional `startupId` `QByteArray`. Captured at `main()` entry before `QApplication`'s constructor strips `DESKTOP_STARTUP_ID` from the env, forwarded over the IPC payload, then applied as `_NET_STARTUP_ID` property on the running instance's window for compliant DEs to match against their launch trackers.

### Changed

- **`CMakeLists.txt`** — `libxcb` linked explicitly on Linux (it was a transitive Qt5 GUI dep before). Notepatra's main.cpp + mainwindow.cpp now use xcb directly for the X11 activate sequence and startup-notify completion.

---

## [0.1.78] — 2026-05-13

**Encoding & file-open fixes — UTF-16 / UTF-32 BOM parity with Notepad++.**

### Fixed

- **`rust-core/src/file_io.rs`** — BOM detection now runs before the null-byte binary heuristic, so UTF-16 LE text (50 % nulls by design, e.g. SQL Server `Generate Scripts` exports, `sqlcmd -o`, PowerShell `Out-File`, Java `-Dfile.encoding=UTF-16`) is no longer mis-flagged as binary. UTF-32 LE/BE BOMs are detected before UTF-16 LE so the shared `FF FE` prefix isn't claimed by the wrong codec. No-BOM UTF-16 is sniffed via even/odd null-column ratio for BOM-less PowerShell / Java output.
- **`rust-core/src/file_io.rs`** — manual UTF-32 LE/BE decoder added because `encoding_rs` intentionally doesn't support UTF-32. Notepatra now opens UTF-32 files that previously rendered as garbled UTF-16.
- **`src/editor.cpp`** — `saveFile()` and `reloadWithEncoding()` preserve UTF-16 / UTF-32 BOMs on round-trip. Pre-v0.1.78 the BOM was silently stripped on save because `QTextCodec("UTF-16LE")` emits no BOM. Files now round-trip byte-for-byte the way Notepad++ does.

### Added

- **`src/mainwindow.cpp`** — Encoding menu now lists the new BOM variants: `UTF-16 LE BOM`, `UTF-16 BE BOM`, `UTF-32 LE BOM`, `UTF-32 BE BOM`.
- **`rust-core/src/file_io.rs`** — 11 new unit tests covering UTF-8, UTF-8 BOM, UTF-16 LE/BE with and without BOM, UTF-32 LE/BE BOM, real binary refusal, empty files, and Windows-1252 fallback.
- **`CMakeLists.txt`** — `rust_core_tests` ctest target wires `cargo test --release` into `scripts/release-check.sh`, so the encoding suite runs before every future tag.

---

## [0.1.77] — 2026-05-12

**MSI hotfix — finishes the v0.1.74 work so notepatra and notepatra-local-ai install side-by-side cleanly.**

### Fixed

- **`installers/windows.wxs`** — variant-scoped ProgId and shell-verb names. v0.1.74 made Component GUIDs unique per flavor, but both flavors still wrote to the same `HKCR\Notepatra.Document`, `HKCR\*\shell\Edit with Notepatra`, and `HKCR\Directory\shell\Open in Notepatra` registry trees. Windows refused to refcount these across two distinct products (different ProductCode / UpgradeCode) and surfaced the Repair/Remove maintenance UI for whichever MSI was installed second. v0.1.77 routes the local-ai variant to `Notepatra.LocalAI.Document` / `Edit with Notepatra Local AI` / `Open in Notepatra Local AI` — non-overlapping HKCR subtrees, no maintenance prompt.

### Recovery for users on v0.1.72 – v0.1.76

Legacy installs registered the shared HKCR keys with now-orphaned component GUIDs. Before installing v0.1.77, force-uninstall by UpgradeCode:

```
msiexec /x {B7C8D9E0-1F2A-3B4C-5D6E-7F8A9B0C1D2E} /qb
msiexec /x {8D5E3C42-1F9A-4B7E-9D6C-1A2B3C4D5E6F} /qb
```

then install `notepatra-0.1.77.msi` and (optionally) `notepatra-local-ai-0.1.77.msi`. v0.1.77+ upgrades cleanly forward.

---

## [0.1.76] — 2026-05-11

**Expanded chart catalogue (4 → 12 types), hover tooltips, modal viewer with PNG export, AI picks the right chart for the data.**

### Added

- **Chart modal viewer (`src/chart_modal.{h,cpp}`)** — click the ⛶ button in the corner of any inline chart to open a 960×640 dialog with the chart re-rendered at full size, plus **Save as PNG…** (HiDPI-correct, defaults to `~/Pictures/`), **Copy image** (system clipboard), and **Close**. Works for every QtCharts-backed type.
- **Hover tooltips on every chart** — `QToolTip::showText()` wired to each series after construction. Bars show `<series>\n<category>: <value>`, XY series show `x · y`, pie/donut show `<label>\n<value> (<pct>%)`, boxplot shows the 5-quantile summary.
- **8 new chart types in `src/chartrender.cpp`** — `area`, `horizontal-bar`, `stacked-bar`, `stacked-horizontal-bar`, `grouped-bar`, `donut`, `histogram`, `boxplot`. All render via QtCharts (`QAreaSeries`, `QHorizontalBarSeries`, `QStackedBarSeries`, `QHorizontalStackedBarSeries`, `QBoxPlotSeries`), so they work in **both the lite build (~9 MB, no WebEngine) and the full build**. The `generate_chart` Vega-Lite tool path is unchanged for advanced specs (heatmaps, layered, geo-shapes).
- **AI chart-selection guidance in `src/ai_systemprompt.cpp`** — rewrote the Data Analyst chart section with explicit **data-shape → chart-type mapping** rules: time series → `line` / `area`, categorical comparison → `bar` / `horizontal-bar` / `grouped-bar`, distribution → `histogram`, distribution-across-groups → `boxplot`, composition → `donut` / `stacked-bar`, correlation → `scatter`. The AI now picks the right chart for the data instead of defaulting to bar for everything.
- **Multi-series spec format** — `grouped-bar` / `stacked-bar` / `stacked-horizontal-bar` take `y` as an **array** of value column names (e.g. `"y":["q1","q2","q3","q4"]`). Single-series types unchanged.
- **Histogram** — auto-bins a numeric column (default 20 bins, configurable via `bins`). Title defaults to `"Distribution of <col>"` if unset.
- **Boxplot** — groups rows by `x` (category column), then computes min / Q1 / median / Q3 / max for the `y` (numeric column) per group.
- **`test_chart_types.cpp`** — 26 sub-checks covering every legacy + new chart type. Asserts each renders a non-null `QWidget`, `looksLikeChartSpec()` accepts every type in the catalogue, and unsupported types return `nullptr` + a descriptive error.
- **`docs/regression-log.md`** — new v0.1.76 entry.

### Tests

- 30/30 ctest pass (up from 29).

---

## [0.1.75] — 2026-05-11

**Runtime font-pack downloader — 27 premium open-source fonts available on demand. Binary stays at ~9 MB.**

### Added

- **`Settings → Manage Fonts…` dialog** — new menu entry that opens a curated catalogue of ~27 open-source fonts grouped into four categories: Code · Monospace (JetBrains Mono, Fira Code, Cascadia Code, Source Code Pro, IBM Plex Mono, Hack, Geist Mono, Inconsolata, Roboto Mono, Fira Mono, Noto Sans Mono, Space Mono, Anonymous Pro), UI · Sans-serif (Inter variable, Roboto, Manrope), Serif · Prose (Source Serif 4 variable, Merriweather), Display · Distinctive (Victor Mono Italic, Comic Mono). Per-row checkbox, install / remove / select-all buttons, progress bar. Each entry shows family · variant · size · license · origin · installed status.
- **`src/fontpack.{h,cpp}`** — 27-entry manifest (every entry SIL OFL 1.1, Apache 2.0, or MIT), `NotepatraFontPack::Installer` queued downloader with progress + cancel signals, `loadInstalledFonts()` startup scanner that registers every `*.ttf` / `*.otf` in `~/.local/share/notepatra/fonts/` with `QFontDatabase` immediately, no restart. Files are pulled from pinned upstream HTTPS URLs on each project's GitHub repo — Notepatra hosts no bytes itself.
- **`src/fontpack_dialog.{h,cpp}`** — Qt5 `QDialog` with grouped `QTreeWidget`, category headers, license + size display, async network install via `QNetworkAccessManager`. ~250-line dialog.
- **Startup hook in `src/main.cpp`** — `NotepatraFontPack::loadInstalledFonts()` runs right after `QApplication` construction so `notepatraDefaultCodeFamily()` / `notepatraDefaultUiFamily()` in `src/fonts.h` can resolve to user-installed fonts on first paint.
- **`test_fontpack.cpp`** — 23 sub-checks. Manifest validity (no duplicate filenames, all-HTTPS URLs, every entry has a license + origin + category, six industry-standard families present by name), path helpers, plus an end-to-end load test that copies a system TTF into `fontsDir()` and asserts `loadInstalledFonts()` registers it.
- **`docs/regression-log.md`** — new v0.1.75 entry catalogues the test's coverage.

### Architecture rationale

- **Bare binary stays lite** — no bytes bundled. Fonts download to `AppDataLocation/fonts` only when the user opens the dialog and ticks selections. Honours the project's `"Bare binary stays small — heavy features on-demand"` rule (same pattern as the Charts Pack runtime download).
- **HTTPS-only** — every URL in the manifest is enforced HTTPS by `test_fontpack.cpp`. Network attackers can't substitute alternate bytes mid-stream.
- **Air-gapped friendly** — for `notepatra-local-ai` or any environment where `github.com` is blocked, users can drop TTFs into the font-pack directory manually; the startup hook picks them up regardless of how they got there.

### Tests

- 29/29 ctest pass (up from 28 — `test_fontpack` adds cleanly).

---

## [0.1.74] — 2026-05-11

**`notepatra-local-ai` MSI upgrade fix + website cleanup. No editor / AI / core changes.**

### Fixed

- **`notepatra-local-ai` MSI no longer drops into Repair / Remove maintenance UI when upgrading from v0.1.72 / v0.1.73.** Three `<Component>` elements in `installers/windows.wxs` (`MainExecutable`, `ApplicationShortcut`, `DesktopShortcut`) used hardcoded Component GUIDs that were shared between the regular and local-ai flavors. With both flavors installed side-by-side, Windows Installer reference-counted them as the *same* component — when MajorUpgrade tried to release v0.1.72/v0.1.73 components, the regular install's reference held them, breaking the upgrade flow. Fixed by switching all three to `Guid="*"` — WiX auto-generates a deterministic GUID from each component's KeyPath, which differs between flavors (different `INSTALLFOLDER` names). Also added `AllowSameVersionUpgrades="yes"` to `<MajorUpgrade>` so re-running the same-version MSI runs the install flow instead of the maintenance UI. Regular MSI users were unaffected and the v0.1.74 regular MSI ships with the same fix as a no-op upgrade.

### Website

- **`docs/index.html` no longer shows slim "horizontal line" rows for older releases.** Previous policy demoted past releases to one-line link rows; from v0.1.74 onward the website carries **only the latest release detail card** and one "See every release on GitHub" link. v0.1.0–v0.1.73 live exclusively on GitHub Releases. 82-line deletion.

### Deferred to v0.1.75

- **Runtime font-pack downloader** (Settings → Manage Fonts… with ~25 premium open-source fonts fetched on demand to `~/.local/share/notepatra/fonts/`). Source files drafted; held until end-to-end UI verification on Linux + Windows + macOS.

---

## [0.1.73] — 2026-05-11

**AI dock blank-on-reopen fix (patch on v0.1.72).**

### Fixed

- **Red ✕ close button no longer leaves the dock un-reopenable.**  Pre-v0.1.73 the × handler called `parentWidget()->setVisible(false)` directly, skipping `Config::aiDockVisible` persistence, the toolbar button checked-state sync, and the splitter rebalance bookkeeping.  After × close, clicking the toolbar AI button to reopen sometimes left the dock at 0 px wide → user saw a blank zero-width dock.  Now the × button emits a new `closeDockRequested` signal that MainWindow routes through `setAiDockVisible(false)` — same code path as the toolbar toggle.
- **`rebalanceAiDockSplit()` rescues 0-px slots.**  After a full hide → show cycle Qt's `QSplitter` leaves the hidden widget's slot at ~0 px, and `setVisible(true)` alone doesn't restore it.  v0.1.72 bailed out of the re-split unconditionally on `m_aiDockSizedOnce` (intent: preserve user splitter drags mid-session), so the rescue never ran.  Now the bail only triggers if the current slot is > 40 px wide (deliberate user drag); below that we re-apply the 60/40 default.

### Tests

- `test_ai_fullscreen_exit.cpp` extended with S16 (hide-show cycle restores dock width) and S17 (red ✕ close → toolbar re-open works) — both FAIL on v0.1.72 and PASS on v0.1.73.
- 53 sub-checks in `test_ai_fullscreen_exit` (up from 47 in v0.1.72), 28/28 ctest pass.

### Carry-forward

- v0.1.72 installer matrix (.deb / .rpm / AppImage / dual MSI / cloud-free `notepatra-local-ai`) unchanged.
- Network policy + `test_network_policy.cpp` 49-case allowlist unchanged.

---

## [0.1.72] — 2026-05-11

**Enterprise-ready Linux installers + cloud-free `notepatra-local-ai` build.**

### Added — Fleet-grade Linux installers

- **`.deb`** (Debian / Ubuntu / Mint / Pop!_OS, x64 + ARM64) at `/opt/notepatra/` with `/usr/bin/notepatra` symlink, hicolor icons, `.desktop` registration. `sudo apt install ./notepatra_0.1.72_amd64.deb`.
- **`.rpm`** (Fedora / RHEL / CentOS Stream / Rocky / Alma, x64 + ARM64). Bundles QScintilla 2.14.1 to defuse Fedora's slightly-different qscintilla-qt5 packaging. `sudo dnf install ./notepatra-0.1.72-1.x86_64.rpm`.
- **`Notepatra-0.1.72-x86_64.AppImage`** universal Linux build (Arch / openSUSE Tumbleweed / any glibc 2.38+). Built with linuxdeploy + linuxdeploy-plugin-qt.

### Added — `notepatra-local-ai` cloud-free build

- New CMake flag `-DNOTEPATRA_NO_CLOUD=ON` builds a binary that physically refuses to talk to public LLM endpoints. UI strips cloud preset shortcuts; `QNetworkAccessManager` requests gated by a 49-case unit-tested allowlist (`NotepatraNetworkPolicy::isPrivateNetworkHost`). Three independent enforcement layers: UI strip, stored-config migration, per-request gate.
- Shipped as `notepatra-local-ai_0.1.72_amd64.deb` / `_arm64.deb` (Debian/Ubuntu) and `notepatra-local-ai-0.1.72.msi` (Windows, per-machine, separate UpgradeCode + install dir).
- `notepatra --version` self-identifies: `Notepatra v0.1.72 (cloud-free / local-ai)`.
- For regulated industries (finance, healthcare, legal, gov), data-sovereignty regions, air-gapped fleets.

### Added — New files

- `src/network_policy.{h,cpp}` — pure helper, name-based allowlist check, no DNS lookups.
- `test_network_policy.cpp` — 49 table-driven cases.
- `installers/debian/build-deb.sh` — single script, both flavors.
- `installers/rpm/{notepatra.spec.in, build-rpm.sh}` — pre-built-binary RPM.
- `installers/appimage/build-appimage.sh` — linuxdeploy harness.
- `release_notes/v0.1.72.md`.

### Changed

- `CMakeLists.txt`: project VERSION 0.1.71 → 0.1.72. New `NOTEPATRA_NO_CLOUD` option.
- `installers/windows.wxs`: parameterized via WiX `<?ifdef NoCloud ?>` — passing `-dNoCloud` to `candle.exe` produces the Local AI variant MSI (different ProductName, UpgradeCode, install dir).
- `src/ollama.cpp`: every QNAM call site adds a no-op-or-refuse gate. Macros compile out completely in the regular build.
- `src/aipanel.cpp`: backend dropdown wraps cloud entries in `#ifndef NOTEPATRA_NO_CLOUD`. Stored-config migration on panel construction.
- `src/main.cpp`: `--version` output adds the cloud-free suffix when built with `NOTEPATRA_NO_CLOUD`.
- `README.md`: new "Admin / Fleet install" section.
- `docs/index.html`: v0.1.72 detail card; v0.1.71 demoted to slim link row.
- `.github/workflows/build.yml`: 50+ new lines across `build-linux`, `build-linux-arm`, `build-windows`, and `release` jobs to build / sign / publish the new artifacts.

### Tests

- 28/28 ctest pass on both regular and cloud-free build flavors (was 27/27 in v0.1.71).
- Local Docker verification matrix on Ubuntu 24.04, Fedora 40, and Debian 12 (for the AppImage glibc compat boundary).

### Not in this release (deferred to v0.1.73+)

- macOS DMG flavor of local-ai (notarization cost for a second variant DMG).
- AppImage ARM64 (linuxdeploy aarch64 plugin maturity).
- AppImage built on older Ubuntu (22.04 / glibc 2.35) for broader distro coverage.

---

## [0.1.71] — 2026-05-11

**AI Interaction Log — audit every cloud + local LLM exchange, 7-day rotation, opt-out-able, credential-scrubbed.**

### Added — `Tools → AI Interaction Log…`

* Every request/response that hits any cloud (OpenRouter, OpenAI, Azure OpenAI, Ollama Cloud) or local (Ollama, llama.cpp, LM Studio / Jan / vLLM via OpenAI-compat) backend is recorded into `~/.config/notepatra/ai-logs/interactions.db` (SQLite, WAL mode). Schema captures: timestamp, session id, backend tag, model, mode (chat/coding/data), role (user/system/assistant/tool_call/tool_result), full content, tool name + args + result, prompt + eval tokens, elapsed ms, error string.
* Rows older than 7 days are pruned on every app start; total DB file size capped at 50 MB (oldest rows dropped if exceeded). Cheap (~ms) SQLite DELETE; no-op when logging is disabled.
* New viewer dialog: filter by backend / mode / model, table view with row-click → full content panel, **Export JSON** button (up to 10,000 events into a single pretty-printed file), **Prune now** button, **Toggle "Log AI interactions"** to opt out in-place.

### Changed — `Config::aiInteractionLogging` default ON

* Privacy-as-transparency: by default Notepatra records what it sends so the user can audit it. Flip off from the dialog or set `aiInteractionLogging:false` in `config.json` — the recorder becomes a no-op and the database file isn't even opened.

### Added — credential scrubber before write

* Every value written to the DB runs through a regex pass that masks: `Bearer …` tokens, OpenAI `sk-…`, Anthropic `sk-ant-…`, GitHub PATs (`ghp_/ghs_/gho_/ghu_/ghr_…`), AWS access keys (`AKIA…`), Google API keys (`AIza…`), and `-----BEGIN ... PRIVATE KEY-----` PEM blocks. False positives just blank a value; false negatives would let a credential leak — the regex set is intentionally aggressive.

### Added — `OllamaClient::setMode("chat"|"coding"|"data")`

* Cheap setter, no protocol impact. AIPanel calls it before each send so the log can filter by mode.

### Hooked — `OllamaClient::generate()` recording

* Records the outgoing user prompt + system prompt before the HTTP request leaves.
* Records the assistant completion at every `responseStats` emit site: Ollama `/api/generate` + `/api/chat` done frame, OpenAI-compat `[DONE]` frame, OpenAI-compat `finish_reason` frame. Each resolves the backend tag from `m_baseUrl` (`openai.azure.com` → `azure-openai`, `api.openai.com` → `openai`, `ollama.com` → `ollama-cloud`, `openrouter.ai` → `openrouter`, else `openai-compat`).

### Added — `MainWindow::checkCrashRecovery()` pre-hook

* `AiInteractionLog::pruneOld()` runs once on startup before session restore so users come back to a tidy DB.

### Files

* `src/ai_interaction_log.{h,cpp}` — public namespace API, SQLite glue, scrubber, query/prune.
* `src/ai_log_dialog.{h,cpp}` — read-only Qt viewer with filters + export.
* `src/config.h` — new bool field `aiInteractionLogging = true` + JSON load/save.
* `src/ollama.{h,cpp}` — new `m_mode` field + `setMode()` + record sites at every completion path.
* `src/aipanel.cpp` — calls `setMode()` before each send.
* `src/mainwindow.cpp` — Tools menu entry; pruneOld at startup.

---

## [0.1.70] — 2026-05-11

**AI cleanup + Notepad++-style session persistence + Data Mode actually knows what's attached.**

### Changed — app close never prompts; unsaved buffers restore silently

* `closeEvent` no longer loops modified tabs asking Save / Discard / Cancel. All open tabs — file-backed and untitled, modified and pristine — are serialised to `~/.config/notepatra/session/session.json` (full unsaved content + cursor + tab name + modified flag).
* Relaunch reads `session.json` and silently re-creates every tab. Modified file-backed tabs reopen the file then overlay the unsaved buffer; untitled tabs are recreated as new buffers with their content.
* Tab close (the `X` on a tab) **still** shows the Save / Discard / Cancel dialog — that's the explicit-save decision point. App close is now an unconditional commit.
* The legacy `recovery_*.txt` crash files + "Restore recovered files?" prompt now only run as a fallback when `session.json` is absent. With session.json present, recovery files + crash flag are wiped on launch so you can't get the double-restore that bolted an extra `[recovered]` tab on top of the silently-restored session.
* `autoSaveRecovery()` removed from the 10 s autosave tick — `saveSession()` carries the full unsaved content now.

### Removed — hard-coded model capability gates + amber dropdown coloring

* `appendErrorBubble(...isn't strong enough for the Data Analyst mode…)` — gone. The substring allowlist behind it (`modelCapableOfDataAnalysis`) rotted with every release (Gemma 4 26B got rejected as "too small" despite being a 17 GB multimodal model with native tool calling).
* `appendErrorBubble(...doesn't support tool calls…)` — gone. Same rationale; the model / backend will refuse if it actually can't comply.
* The `⚠ … is too small` banner near the Data mode action bar — gone.
* `decorateModelsByMode()` reduced to a no-op clear. Dropdown rows render in the default foreground colour; no second-guessing tooltips.
* Memory saved (`feedback_no_hardcoded_model_allowlists.md`) so future sessions don't reintroduce these. Context gates (no folder open → Coding refuses; no DB connection → Data refuses) stay — those are about workspace state, not model identity.

### Added — Data Mode "attached" chip + schema injected into system prompt

* Visible green pill below the Manage Connections / Browse Schemas row: `✓ Attached: <conn name> (N tables) · <conn 2> (M tables) — visible to the model via query_sql.` Connection that won't open renders in red. Zero connections shows an amber "no databases attached" nudge.
* Every Data-mode send now embeds the saved connections + their first ~30 user tables into the system prompt with: *"These are the ONLY data sources available via `query_sql`. Do NOT speculate about other data sources or the editor workspace."* Fixes the "I see a variety of files and directories in your workspace" hallucination where the model answered about the file tree when asked about the database.
* New `DbConnections::listTables(record)` helper — per-driver introspection (`sqlite_master`, `INFORMATION_SCHEMA.tables` for PostgreSQL/MySQL, `sys.tables` for SQL Server via QODBC). Drives both the chip and the prompt injection.

### Changed — chat-bubble breathing room + auto-fit

* Bubble inner padding bumped: assistant 14/16 → 22/24, user 10/16 → 14/20. Inter-bubble gap 14 → 26 px. Card outer margin 14/12 → 20/18. Chat container margin 12 → 18. Line-height 1.55 → 1.7.
* Tables + blockquotes get explicit CSS rules (previously fell through to QTextBrowser defaults and rendered cramped).
* Chart widgets: 14 px spacer above, 10 px below, 280 px minimum height.
* `QTextDocument::setDocumentMargin(0)` kills Qt's hidden 4 px frame margin that was padding every bubble. Removed the `+ 8 px` slop on the height calculation — that was the empty band users were seeing under single-line replies.
* Viewport-resize event filter re-fits every bubble's height when the panel/splitter width changes, so the body refits on dock resize instead of freezing at first-render height.

### Fixed — QODBC connection string composition

* SQL Server connections now build `DRIVER=…;SERVER=host,port;DATABASE=…;UID=…;PWD=…;` and pass it via `setDatabaseName()` instead of the per-field setters (which don't compose for QODBC). This is what makes the SQL Server local Docker preset actually connect.
* SQL Server local preset (case 2 in the Manage Connections preset dropdown) wired with port 14330, `NotepatraTest` DB, `sa` user — for use with `docker/sql-server-local.yml`. Password ships in the yml + setup script for local testing only; not for production.

### Fixed — conversation history threading in Ollama backend

* When prior messages exist, `Ollama::generate()` now uses `/api/chat` and splices the conversation history between the system prompt and the current user message. Pre-v0.1.70 the array was reset to `[]` on every send, so the model never saw the conversation it was in and replied as if every turn was the first.

### Added — SVG icons replacing emoji codepoints

* `resources/icons/` — 14 hand-crafted SVGs (bot, git-branch, database, image, file, file-text, lock, unlock, lightbulb, wrench, x, check, folder, alert-triangle) registered via `resources/icons.qrc`.
* Replaces emoji codepoints (U+1F300+) in chrome that tofu'd on systems without a color emoji font (Linux being the worst case).

---

## [0.1.69] — 2026-05-10

**Critical fix — AI Coding / Data mode no longer locks tool buttons.**

### Fixed — Coding / Data mode + tool button click = nothing visible

* Coding Mode and Data Mode auto-fullscreen the AI dock by emitting `fullscreenToggled(true)` directly from the mode-toggle slot (the expand `⛶` button is also hidden in those modes). Pre-v0.1.69, clicking any tool button (Project Search, Terminal, REST Client, JSON / HTML / Bracket Tools, SQL Formatter, Compare, Welcome) while in those modes appeared to do nothing — the tab was added but `m_tabs` stayed hidden behind the splitter squashed to 100% AI dock. App looked frozen / locked up.
* `src/aipanel.cpp:5468` — `AIPanel::forceExitFullscreen()` only un-checked the expand button. In Coding / Data mode the button was never checked, so the function was a no-op. Fix: emit `fullscreenToggled(false)` directly when the button isn't checked. MainWindow's restore handler is idempotent.

### Added — integration test that catches this class of bug

* `test_ai_fullscreen_exit.cpp` extended from 14 to **23 sub-checks across 6 scenarios**. Two new scenarios (S5: Coding + Project Search, S6: Data + Terminal) drive the Coding / Data mode path under offscreen Qt and assert `m_tabs->isVisible()` after the tool action triggers. They FAIL on pre-v0.1.69 code and PASS on the fix. Runs in CI on every commit — future regressions in any auto-fullscreen entry path will fail before shipping. The v0.1.67 / v0.1.68 cycle leaked this bug because no test exercised the actual UI state machine; this commit closes that.

---

## [0.1.68] — 2026-05-10

**AI dock auto-exits fullscreen on editor tab switches too (closes v0.1.67 gap).**

### Fixed — AI dock blocked editor tabs when fullscreen

* v0.1.67 wired `exitAiFullscreenIfActive()` into 14 **tool-tab** open sites (Project Search, Terminal, Git, REST Client, SQL Formatter, JSON/HTML/Bracket Tools, Compare, Welcome), but **editor-tab switching** (Ctrl+Tab, clicking tabs in the tab bar, double-clicking a Project Search result, File > Open) still left the newly-focused tab hidden behind the fullscreen AI dock. The focus shift was real but invisible, so the editor felt frozen.
* `src/mainwindow.{h,cpp}` — the `QTabWidget::currentChanged` slot now calls `exitAiFullscreenIfActive()` on every tab-focus change, gated by a one-shot `m_skipAiAutoExitOnNextTabChange` flag. `newFile()` (Ctrl+N) sets the flag immediately before `setCurrentIndex(idx)` to preserve the **v0.1.61 background-tab UX rule** — Ctrl+N while the AI dock is fullscreen creates the new editor in the background so the user doesn't lose AI flow. `openFile()` deliberately does NOT set the flag, so user-initiated file opens correctly collapse the dock.
* The 14 explicit `exitAiFullscreenIfActive()` calls inside the v0.1.67 tool-tab handlers remain in place as belt-and-braces no-ops (the helper short-circuits when fullscreen isn't active).
* All 26 deterministic regression tests still pass on the lite build.

---

## [0.1.67] — 2026-05-10

**Three independent AI threads + tool-tab fullscreen auto-exit + release-engineering hardening.**

### Fixed — AI dock blocked tool tabs when fullscreen

* `src/aipanel.{h,cpp}` — new `AIPanel::forceExitFullscreen()` public slot that drives the same code path as the fullscreen-toggle button (sets `m_aiExpandBtn->setChecked(false)`).
* `src/mainwindow.{h,cpp}` — new `exitAiFullscreenIfActive()` helper that checks whether the AI dock is currently in fullscreen (`m_aiSavedSiblingVisibility` non-empty) and, if so, calls the slot above. Instrumented at 14 tool-tab open sites: Project Search, Terminal, REST Client, SQL Formatter, Git diff, Git, JSON Tools (parent + Compare child), HTML Tools (parent + Compare child), Bracket Tools (parent + Compare child), Compare picker, Welcome.
* Editor-tab creation (Ctrl+N) deliberately untouched — the v0.1.61 background-tab UX (create-in-background while AI is fullscreen so the user doesn't lose AI flow) is preserved.
* Hex Editor opens as a modal dialog, not a tab, so no instrumentation was needed there.

### Fixed — cross-mode chat contamination

* `src/aipanel.{cpp,h}` — the single shared `m_messages` vector (one chat session per workspace, used by all three mode toggles) is replaced with **three independent vectors**: `m_chatMessages`, `m_codingMessages`, `m_dataMessages`. Each mode now keeps its own conversation history, so flipping from Coding → Data no longer mixes the previous mode's bubbles with the new mode's system prompt. Closes the UX bug where the AI's first reply after a mode swap referenced the previous mode's tools or topic.
* New private helper `activeMessages()` returns a reference to whichever vector matches the currently checked mode button. Every read/write to chat state — `appendUserBubble`, `appendErrorBubble`, `beginAssistantBubble`, `endAssistantBubble`, `renderTranscript`, `responseStats` handler, copy-link handlers, the data + coding welcome-card gates — now goes through it.

### Changed — mode-switch behaviour

* `applyModeWithCancel` (the toggle handler shared by all three mode buttons) now:
  1. Cancels any in-flight stream so it doesn't keep pumping tokens into the rebuilt chat surface.
  2. Force-saves the outgoing mode's bubbles to disk (`saveChatHistoryNow()`) so a kill-and-restart doesn't lose recent activity to the 2-second debounce.
  3. Calls `renderTranscript()`, which wipes every bubble widget from `m_chatLayout` and re-adds bubbles from the new active vector.

### Changed — `clearChat()` (Reset button)

* Clears **only the active mode's vector**, not all three. The other two modes retain their history. Force-saves immediately.

### Changed — on-disk chat-history JSON schema (v1 → v2)

```json
// Old (v1)
{ "version": 1, "messages": [...] }

// New (v2)
{ "version": 2, "chat": [...], "coding": [...], "data": [...] }
```

* `saveChatHistory()` writes the v2 form.
* `loadChatHistory()` migrates old files automatically: `{version: 1, messages: [...]}` and bare top-level arrays both land in `m_chatMessages`; coding + data start empty. No user action required.
* Roll-off (1 MB on-disk cap) rotates `chat → coding → data` when trimming, so heavy use of one mode doesn't starve the others.

### Added — `saveChatHistoryNow()`

* New private method that cancels the pending 2 s debounce timer and saves synchronously. Called at every key transition (user submit, assistant stream end, mode switch, Reset). The debounce stays as a backstop for streaming-chunk flurries (token frames fire 20+ times/second).

### Added — `test_ai_chat_history.cpp`

* New regression suite, 61 sub-checks across four sections:
  * Cross-mode partitioning — activeMessages() swaps the right vector; appending in one mode doesn't leak into the other two; switching back resurfaces the original messages.
  * `clearChat()` clears only the active vector.
  * Save + reload round-trips every field (text, role, model, promptTokens, evalTokens, elapsedMs) verbatim across all three vectors.
  * v1 migration — flat-array dump and `{version: 1, messages: [...]}` both land in `m_chatMessages`.
* Constructs a real `AIPanel` under `QT_QPA_PLATFORM=offscreen`. Adds an `AIPanelTestAccess` friend class so tests can drive the three private vectors and the mode buttons without leaking those internals into the production API.
* Wired into `notepatra_all_tests`. Total deterministic test count: 25 → 26. All pass on the lite build.

### Changed — release engineering (from the 4 parallel verification agents)

* `.github/workflows/build.yml` — Linux x64 + ARM `-full` flavor builds now run `QT_QPA_PLATFORM=offscreen ./notepatra --version` *after* the `ldd | grep webengine` check. v0.1.63's silent-failure incident was a runtime crash behind `#ifdef NOTEPATRA_WITH_WEBENGINE`; the old gate caught compile/link failures but not boot-broken binaries. New gate catches both.
* `.github/workflows/build.yml` — test-step `cmake .. -DBUILD_TESTING=ON` reconfigure now re-asserts `-DCMAKE_BUILD_TYPE=Release` so a future generator switch (Ninja Multi-Config) can't silently send Debug binaries through ctest.
* `.github/workflows/build.yml` — release job pinned to `ubuntu-24.04` (was `ubuntu-latest`), matching every other Linux job in the file so cosign / SLSA action runtimes can't shift under us.
* `.github/workflows/build.yml` — stale comments fixed: Chocolatey openssl.light reference (replaced by FireDaemon OpenSSL 1.1 download earlier in the job); the never-delivered "macOS / Windows full-flavor ship in v0.1.65" promise (full flavor is Linux-only by design — Charts Pack downloads on-demand on the other OSes).
* `scripts/post-release-verify.sh` — switched from standalone `jq` to gh's built-in `--jq` (gojq embedded). Same pattern `scripts/stale-text-check.sh` already uses; drops one external dependency from the release-day toolchain.
* `src/themes.h` — Windows registry path comment no longer ends in `\`, which the preprocessor treats as line-continuation inside `//` and cascades 12 `-Wcomment` warnings through every TU that includes themes.h.
* `src/lexer_extras.cpp` — `Q_UNUSED(set)` on `LexerGitignore::keywords` (pattern-only lexer with no keyword sets — the parameter is intentionally unused).

### Files changed

**Three independent AI threads:**
* `CMakeLists.txt` — version 0.1.66 → 0.1.67; new test target.
* `src/aipanel.h` — replaced `m_messages`; added activeMessages() / saveChatHistoryNow() / AIPanelTestAccess friend.
* `src/aipanel.cpp` — all 32 m_messages references rewritten; persistence layer rewritten for v2 schema with v1 + v0 migration; mode-switch handler force-saves before re-render.
* `test_ai_chat_history.cpp` — new.

**Auto-exit AI dock fullscreen:**
* `src/aipanel.{cpp,h}` — `forceExitFullscreen()` slot.
* `src/mainwindow.{cpp,h}` — `exitAiFullscreenIfActive()` helper + 14 call-site instrumentations.

**Release engineering:**
* `.github/workflows/build.yml`, `scripts/post-release-verify.sh`, `src/themes.h`, `src/lexer_extras.cpp` (see above).

**Docs + meta:**
* `release_notes/v0.1.67.md` — this release.
* `README.md` + `docs/index.html` + `docs/docs.html` — version refs bumped.

---

## [0.1.66] — 2026-05-10

**Manage Connections UX + local SQL Server harness + website cleanup.**

### Added — Manage Connections preset dropdown

* `src/dbconnections.{h,cpp}` — new Preset dropdown at the top of the connection form. Seven templates: SQL Server (localhost ODBC), SQL Server Express (named instance), Azure SQL Database, PostgreSQL (localhost), MySQL/MariaDB (localhost), SQLite (file), DuckDB (file or `:memory:`). Each preset fills driver / port / host / database / options with sensible defaults; user can edit anything afterwards.

### Added — Per-driver smart defaults

* `onDriverChanged()` auto-fills the default port (1433 / 5432 / 3306) when it's still 0, sets per-driver placeholders on every field, shows SQL Server-specific Options template (`DRIVER={ODBC Driver 18 for SQL Server};Encrypt=no`).
* New `m_driverHint` QLabel below the form. Amber + per-OS install commands when the Qt SQL plugin is missing (covers Microsoft msodbcsql18 setup for Linux + macOS + Windows). Green usage tip when the plugin is present.

### Added — Local SQL Server Docker harness

* `docker/sql-server-local.yml` — `mcr.microsoft.com/mssql/server:2022-latest` bound to 127.0.0.1:1433. Localhost-only by design (not 0.0.0.0).
* `scripts/sql-server-local-setup.sh` — one-command spin-up: brings container up, waits for healthy, seeds `NotepatraTest` DB with customers/products/orders tables (5 + 5 + 9 rows), registers a `sql-server-local` connection in `~/.config/notepatra/db-connections.json`. `--teardown` and `--wipe` flags for cleanup. Apple Silicon note (image is x64-only, runs under Rosetta/QEMU at ~3× speed).

### Added — Comprehensive "How to connect" documentation

* `docs/docs.html` — new "How to connect — step by step (v0.1.66+)" section under Data Analyst Mode. Per-database walkthroughs for SQL Server (local Docker + remote + Windows Auth + Azure SQL TLS), PostgreSQL (local + managed sslmode + Unix-socket), MySQL/MariaDB (local + managed SSL_CA + Unix-socket), SQLite (file picker), DuckDB (multi-mode Database field).
* Driver-availability matrix per OS (Linux apt / macOS brew / Windows bundled).
* Troubleshooting list for five most common connection failures.

### Changed — Website cleanup

* `docs/index.html` — removed the obsolete FAQ entry about v0.1.23 → v0.1.24 Windows MuiCache cleanup (only affected upgrades from a specific pre-fix release; no longer relevant). Historical version cards (every release pre-v0.1.65, ~60 cards, ~2500 lines of HTML) replaced with a single "See every release on GitHub →" link block. Latest version card stays inline. Page weight dropped 54% (4787 → 2201 lines).

### Fixed — Version history housekeeping

* v0.1.63 and v0.1.64 GitHub tags now have notes-only releases ("rolled into v0.1.65") so the Releases page shows a continuous v0.1.62 → v0.1.66 ladder with no gaps. The notes-only releases carry no binaries — they point users at v0.1.65 / v0.1.66 for artifacts.

---

## [0.1.65] — 2026-05-10

**Hotfix: full-flavor build crashed in CI on `QJsonDocument(QJsonValue)`.**

### Fixed — `src/charts/vega_chart_renderer.cpp` WebEngine path

* The WebEngine code path used `QJsonDocument(QJsonValue(QString))` to encode the spec as a JS string literal — but Qt5's `QJsonDocument` has no `QJsonValue` constructor (only `QJsonObject` / `QJsonArray`). Replaced with a 1-element-array wrap + `mid(1, size-2)` slice, which produces the same JS-safe string literal regardless of what control chars / quotes / backslashes the Vega-Lite spec contains.
* This bug crashed v0.1.63 CI on Linux x64 + ARM + Windows silently (only macOS happened to compile under brew qt@5's patchset). The v0.1.63 GitHub Release never published. v0.1.64 inherited the bug; the new `-full` flavor build pass would have failed identically. v0.1.65 fixes it and re-cuts.

### Same behavior as v0.1.64

* All v0.1.64 lite-mode-default + Charts Pack prompt + plugin_loader scaffolding ships unchanged.

---

## [0.1.64] — 2026-05-10

**Lite mode by default — bare binary stays small, charts move to an on-demand pack.**

### Changed — `NOTEPATRA_WITH_WEBENGINE` default flipped OFF

* `CMakeLists.txt` — option default `ON` → `OFF`. The default binary no longer links `Qt5::WebEngineWidgets` and drops from ~95 MB (with WebEngine bundled) to **9.2 MB on Linux x64**. Same speed, same features, minus the inline Vega-Lite renderer.

### Added — proper "Charts Pack required" card

* `src/charts/vega_chart_renderer.cpp` lite-mode path rewritten. Old behavior: `setSpec()` emitted `renderError("WebEngine support disabled at build time")` → small italic red label. New behavior: theme-aware card with title "📊 Chart rendering requires the Charts Pack", description from `plugin_loader::packDescription()`, size + download metadata, and two buttons: **[Install charts pack]** + **[View JSON instead]**.
* New signals on `VegaChartRenderer`: `installRequested()`, `viewJsonRequested(QJsonObject)`. New accessor `isLiteStub()`.

### Added — `src/plugin_loader.{h,cpp}` scaffolding

* `NotepatraPlugins::isInstalled(name)`, `installPath(name)`, `pluginDir()`, `packDescription(name)`, `approximateDownloadSize(name)`, `manualInstallDocUrl(name)`. Registered packs: `charts` (95 MB, ships in v0.1.64 `-full` Linux flavors), `pdf` (12 MB, v0.1.66 candidate).
* Uses `QStandardPaths::AppDataLocation` so the plugin root lands at `~/.local/share/notepatra/plugins/` on Linux, `~/Library/Application Support/notepatra/plugins/` on macOS, `%APPDATA%/notepatra/plugins/` on Windows.

### Added — `AIPanel::openChartsPackInstall()`

* Slot wired to `VegaChartRenderer::installRequested`. Shows a `QMessageBox` explaining the lite/full flavor split with platform-specific download instructions, then opens the GitHub Releases page for the current tag in the user's default browser. macOS / Windows users see honest messaging: "Full build for {platform} ships in v0.1.65."

### Added — Linux full-flavor CI artifacts

* `.github/workflows/build.yml` — Linux x64 and ARM jobs each run a second build pass with `-DNOTEPATRA_WITH_WEBENGINE=ON`. New artifacts:
  * `notepatra-linux-x64-full.tar.gz` — bundles QtWebEngine
  * `notepatra-linux-arm64-full.tar.gz` — bundles QtWebEngine
* SHA-256, cosign signing (keyless OIDC), and SLSA build provenance all extended to cover the new artifacts.

### Added — `test_vega_chart` lite-mode coverage

* `test_vega_chart.cpp` — two new subcases: lite-mode button → signal wiring (find both `QPushButton`s, click each, assert signals fire exactly once with the round-tripped spec) and `plugin_loader` sanity (`isInstalled(charts)` matches build-time flag, unknown pack names return false, `approximateDownloadSize > 0`, doc URL non-empty).
* Updated existing subcase: lite-mode `setSpec()` must NOT emit `renderError` anymore (missing pack is a feature-gap, not an error).
* All 25 tests pass on the lite build.

### Deferred to v0.1.65

* macOS + Windows full-flavor CI artifacts (need careful `macdeployqt` / `windeployqt` second-pass work plus testing across the OpenSSL / QScintilla / signing pipeline).
* In-app HTTP-download install (stream the pack into `pluginDir()`, verify SHA-256, load via Qt plugin shim so the main binary can dlopen WebEngine at runtime without re-linking).
* "Explain this chart" modal + export PNG/SVG/Excel buttons.

---

## [0.1.63] — 2026-05-10

**Data Analyst charts — Vega-Lite renderer scaffold.**

### Added — `generate_chart` AI tool

* New `generate_chart(spec, title?, id?)` tool — `spec` is a Vega-Lite JSON object. Validated for `mark` + `encoding` keys (relaxed for composite specs using `layer` / `hconcat` / `vconcat` / `repeat` / `facet`). 100 KB spec size cap.

### Added — `VegaChartRenderer` widget

* `src/charts/vega_chart_renderer.{h,cpp}` — `QWidget` subclass with dual paths. With `NOTEPATRA_WITH_WEBENGINE=ON` (default): embeds `QWebEngineView`, loads inline HTML shell that imports vega/vega-lite/vega-embed from JSDelivr. With OFF: stub `QLabel` showing rebuild hint.
* Public API: `setSpec(QJsonObject)`, `chartId()`, `exportPng(scaleFactor=2)`. Signals: `renderReady()`, `renderError(QString)`.

### Added — inline chart rendering in the chat

* `handleToolCall` `generate_chart` branch builds a `QFrame#chartCard` hosting the renderer + optional title + error label. Inserted into the chat content layout inline (not in a separate panel).

### Added — CI installs Qt5 WebEngine on every runner

* `.github/workflows/build.yml` — Linux x64 + ARM jobs add `qtwebengine5-dev` + `libqt5webenginewidgets5` to apt; Windows `install-qt-action` modules `'qtcharts'` → `'qtcharts qtwebengine'`; macOS brew qt@5 already includes WebEngine.

### Build / test

* New `test_vega_chart` — 5 subcases (construction, setSpec, registry contains generate_chart, valid/rejected specs, composite specs).
* `test_ai_tools` hardcoded `tools.size() == 12` relaxed to `>= 12` (now 13 with generate_chart).
* Stale-text audit: 21/21 surfaces match canonicals.

### Deferred to v0.1.64+

* On-demand QtWebEngine plugin architecture so bare binary stays ~9 MB.
* PPTX writer (PNG-in-pptx via miniz).
* "Explain this chart" modal with PNG / SVG / Excel export.
* Poppler-Qt5 PDF→PNG rendering for vision models.

---

## [0.1.62] — 2026-05-10

**VS Code-parity Git in Coding mode.**

The "real" Source Control work the v0.1.61 release notes promised, coordinated by two parallel implementation agents in isolated worktrees.

### Added — per-hunk stage / revert from the editor gutter

* New `GutterHunkPopup` — click any green / red / blue line marker to anchor a popup showing the hunk's before-vs-after content (embedded `DiffView`) plus **Stage hunk** / **Revert hunk** / **Copy diff** buttons.
* New `src/git_hunk_apply.{h,cpp}` with patch synthesizer + `git apply --cached --whitespace=nowarn` wrapper. Inline safety rails (workspace-anchor + canonical-path + credentials deny-list + 5000-line hunk cap).
* `src/editor.{h,cpp}` enables margin-3 sensitivity and routes `marginClicked` → hunk lookup → popup at clicked line's screen position.
* `src/gitgutter.{h,cpp}` exposes `DiffHunk` + `hunksForFile()` / `hunkIndexForLine()`.

### Added — `CompareWidget` per-hunk Stage / Revert buttons

* `CompareWidget::setGitContext(repoRoot, filePath)` enables a scrollable hunk strip — one row per contiguous diff-row run, with **Stage hunk** / **Revert hunk** / **Jump →** routing via new signals.

### Added — marker-based merge resolution helper

* New `src/merge_helper.{h,cpp}` — `scanConflicts(buffer)` returns regions of column-0 `<<<<<<<` / `=======` / `>>>>>>>` markers; `applyResolution()` rewrites the buffer for Ours / Theirs / Both.
* New `src/merge_helper_widget.{h,cpp}` — renders Take buttons per region + QScintilla annotation labels.
* `GitPanel` — `UU`-status files now show a **Resolve** button instead of +/-.

### Build / test

* 23/23 deterministic regression tests pass.
* `git_hunk_apply.cpp` inlines its path-safety check so `test_options_actually_work` doesn't transitively pull QSql / dbconnections.
* Stale-text audit: 21/21 surfaces match canonicals.

---

## [0.1.61] — 2026-05-10

**UX overhaul: smarter gating, vision drops, coding redesign.**

Thirteen distinct improvements driven by a single user session. Coordinated by five parallel agents (four research, two implementation in isolated worktrees). Item 9 (data charts) deferred to v0.1.63 with a complete library evaluation; full VS Code-parity Git Source Control in Coding mode lands in v0.1.62.

### Added — bottom segmented Chat/Compose/Agent toggle (item 8)

* Replaced the top QTabWidget split between Chat and Composer with a **single unified conversation surface** and a **3-segment toggle at the bottom of the panel** — matches the iOS/Slack keyboard-accessory mental model that Continue.dev, Copilot Chat, and Cursor 3.0 all converged on. `ChatModeSegment` enum (`Chat=0`, `Compose=1`, `Agent=2`) replaces `m_chatTabs->currentWidget()` checks; `m_editPlan` reparented to `m_chatContent` and surfaced inline when the agent emits dry_run write_file / apply_diff.
* `composerActive` (which forces `dry_run: true` on write tools) keyed off the new segment enum — Compose can fire even outside Coding intent.

### Added — vision-aware drag-and-drop (item 10)

* Drag image (PNG/JPG/WEBP/GIF/BMP) or document (PDF/DOCX/PPTX) onto the AI dock → auto-detects whether the currently-selected model supports vision; on refusal, styled error bubble lists alternatives: local `qwen2.5vl:7b` / `gemma3:4b`, cloud `claude-sonnet-4-6` / `gpt-5` / `gemini-2.5-flash`.
* Detection strategy: Ollama via `/api/show` capability cache (conservative-false on empty cache to avoid silent-drop trap); cloud via May-2026 prefix allowlist (Claude 3.5+, GPT-4o/4.1/4.5/5/o4, Gemini 1.5+).
* `setAcceptDrops(true)` + `dragEnterEvent`/`dropEvent` overrides on AIPanel. `attachFile()` and `dropEvent` share `acceptAttachment()` so the refusal logic isn't duplicated.

### Added — context guards at sendPrompt (item 4)

* Coding mode without a workspace → friendly assistant card pointing at File → Open Folder.
* Data mode without a saved DB connection → pointer to Manage Connections, with DuckDB hint for raw CSV/Parquet.

### Added — runtime capability response (item 11)

* Coding mode + non-tool-capable model → refusal listing tool-capable alternatives (local: qwen2.5-coder family, llama3.1, mistral-nemo; cloud: claude-sonnet-4-5, gpt-5, gpt-4o, gemini-2.5-flash).
* Data mode + insufficient model → refusal recommending qwen2.5-coder:14b local / claude-sonnet-4-5 / gpt-5 / gemini-2.5-pro / deepseek-r1.
* Complements existing dropdown amber/green decoration — runtime guard catches the case where the user picks an amber model anyway.

### Added — file-explorer Coding-only with hide/unhide (item 6)

* Explorer sidebar shows when Coding mode toggles ON; hides on Chat/Data.
* New `HiddenPathProxy : QSortFilterProxyModel` filters by absolute-path set; right-click → "Hide \"X\"" persists to `Config::explorerHiddenPaths`; "Show hidden (N)" undoes.
* Four show-sites (File → Open Folder, View → Folder as Workspace, GitPanel `repositoryOpened`, `toggleAiDock`) gated on Coding mode.

### Added — copy buttons on user bubbles (item 5)

* Widget-rendered bubble (during streaming) gets a `⧉ copy` link below each user message.
* HTML-rendered transcript (history reload) routes through the existing `copy://message/N` handler in `handleChatLink`.
* Matches the assistant card's long-standing copy button.

### Added — bonus chrome consolidations

* Coding **and** Data modes auto-fullscreen (v0.1.57 did Coding only).
* ⛶ expand button hidden in Coding/Data — those modes are AI-first, only ✕ remains. Chat keeps the toggle.
* 🔒 Share file checkbox visible only in Coding (Chat/Data have no open-file concept worth gating).

### Fixed — smart input gating (item 1)

* `updateInputAvailability()` now also fires on `QEvent::EnabledChange` via an event filter on `m_modelCombo`, eliminating the v0.1.59 race where the dropdown enabled after `currentTextChanged` so the input stayed disabled until manual wiggle/Reset.
* Placeholder copy rewritten with concrete next-action commands per state (`ollama serve`, `ollama pull qwen2.5-coder:7b`, click ⚙).

### Removed — standalone Git toolbar shortcut (item 2 MVP)

* The red "Git" feature shortcut on the toolbar is gone. Plugins → Git Integration (inbuilt) still opens the existing tab-based panel.
* Full VS Code-parity Source Control in Coding mode lands in **v0.1.62** — research complete (per-hunk gutter popup, atomic `git apply --cached`, branch picker, sync, marker-based conflict helper); 90% of infrastructure already exists.

### Verified — Ctrl+N in fullscreen (item 7)

* Pressing Ctrl+N while the AI dock is fullscreen creates a background editor tab without exiting fullscreen. Lexer auto-attaches by extension on save. (No code change needed — verified end-to-end.)

### Process — parallel agents + worktree isolation (item 12)

* Four research agents (VS Code git architecture, chat-panel UI patterns, vision-model APIs, charting library evaluation) — reports inform v0.1.62 (git + vision PDF rendering) and v0.1.63 (charts).
* Two implementation agents in isolated worktrees (items 8 + 10) merged back into `feature/v0.1.61-bigfeatures` — both auto-merged cleanly without conflict.
* Test-scenarios agent produced a 38-case manual plan (Sections A golden paths, B edge cases, C regression hot spots, D performance smoke).

### Tests

* 23/23 deterministic regression tests pass.
* `scripts/stale-text-check.sh` returns 21/21 surfaces matching canonicals.
* `scripts/release-check.sh` clean for v0.1.61.

---

## [0.1.60] — 2026-05-10

**Stale-text drift killed — About dialog matches reality + wired audit.**

A 24-hour follow-up to v0.1.59.

### Fixed — App About dialog now states the real feature counts

* `src/mainwindow.cpp:3411` had been hardcoded at `100+ file types · 48 language lexers` and `Local AI via Ollama / llama.cpp / OpenAI-compatible` since v0.1.31. The version *number* itself flowed correctly via the `NOTEPATRA_VERSION` compile define, but the **feature counts in the body string did not**. Updated to `226 file extensions · 92 language lexers` and `6 AI backends — Ollama / llama.cpp / OpenRouter / Ollama Cloud / OpenAI / Azure OpenAI`.
* The Language-menu code comment in `src/mainwindow.cpp:2043` was also stale (`"78 languages"`, frozen at v0.1.55) — fixed to `"92 lexers / 226 file extensions"`.
* The **GitHub repo description** (rendered on github.com and in every search snippet) was at `~4 MB bare binary, 100+ file types`, frozen at maybe v0.1.20-era. Updated via `gh repo edit` to match the App About body and the ~9 MB binary reality.

### Added — `scripts/stale-text-check.sh`, wired into `release-check.sh`

The fix above is two surfaces this release; the `100+ file types` string had survived three intervening releases. Manual checklists rot. Wired checks don't. New `scripts/stale-text-check.sh`:

* Canonical counts live at the top of the script (`LEXER_COUNT=92 / FILE_EXT_COUNT=226 / BACKEND_COUNT=6 / BACKEND_LIST="Ollama / llama.cpp / OpenRouter / Ollama Cloud / OpenAI / Azure OpenAI"`).
* 21 grep-asserts across every surface that has ever drifted: App About body · README intro line · README releases-table row · `docs/index.html` stat cards · `docs/index.html` meta description · `docs/index.html` LATEST version-card · `docs/docs.html` "ships N language lexers" line · `docs/docs.html` "Latest release is" callout · `CHANGELOG.md` top entry · `release_notes/vX.md` existence · **live GitHub repo description** queried via `gh repo view`.
* Wired into `scripts/release-check.sh` as a sub-step under `── stale-text audit (feature counts) ──`. Every release preflight runs the audit automatically.

When a count actually changes in the future (a lexer is added, a backend retires, etc.), bump the constant at the top of the script *and* every surface in the same commit. The script names which surface still has the old value if you miss one.

### Changed — misc stale-text cleanups

* `.github/ISSUE_TEMPLATE/bug_report.yml` — version placeholder bumped from `v0.1.8` (frozen since April) to `v0.1.60`.
* `SECURITY.md` — five SHA-256 / cosign / SLSA verification examples bumped from `releases/download/v0.1.0/` to `releases/download/v0.1.60/` so copy-paste commands work without first noticing the URL is for the very first release.
* `AGENTS.md` — local-build test command replaced an outdated five-test list with the `notepatra_all_tests` meta-target so contributors don't miss any of the 26 test binaries.

### Build / test

* 21/21 deterministic regression tests pass.
* `scripts/stale-text-check.sh` returns 21/21 surfaces matching canonicals.
* `scripts/release-check.sh` returns clean for v0.1.60.
* Bare binary unchanged — pure copy + script changes, no new deps.

---

## [0.1.59] — 2026-05-10

**Input gating when no model is ready + author-link refresh.**

### Fixed — chat input refuses keystrokes until a real model is selected

* The chat input + Send button now disable across **all three modes** (Chat / Coding / Data) whenever the model dropdown shows a placeholder/error label (`(detecting…)`, `(Ollama offline)`, `(no models installed)`, `(API key required)`). Until now the input was always editable, so users typed an entire prompt before learning at Send-time that no model was usable.
* Implementation: a single `m_modelCombo currentTextChanged` connect routes to `AIPanel::updateInputAvailability()`, which keys off the parenthesis convention every placeholder entry already uses — so one helper covers all 9 dropdown-disable call sites without touching them.
* State-specific placeholders explain *why* the field is disabled (e.g. `Ollama is offline — start it ('ollama serve') and click ↻ Refresh.`, `No models installed — 'ollama pull qwen2.5-coder:7b' then click ↻.`, `API key required — open ⚙ to add one, then pick a model.`).
* The Send button gains a `:disabled` rule (45% label opacity) so the dock visibly signals the action is unavailable, not just unresponsive.
* Defense in depth: the existing `sendPrompt()` guard at `aipanel.cpp:2747` (clear chat + error bubble on placeholder model) stays as a backstop.

### Changed — "Prateek Singh" credit links to author's blog

* The in-app About dialog (`src/mainwindow.cpp`), the website homepage footer (`docs/index.html`), and the docs-page footer (`docs/docs.html`) all now link `Prateek Singh` to <https://theaivibe.org/about> instead of a bare GitHub profile (or, in `docs.html`, plain text). GitHub remains on the About dialog as a separate "Source" line and on the site footer source-code links — navigation parity preserved.

### Tests

21/21 deterministic tests pass. Manual verification across Light + Dark themes: input + Send toggle correctly across Ollama-stop / start cycles and across all three mode toggles.

### Stats

3 source files changed (`src/aipanel.{cpp,h}` + `src/mainwindow.cpp`), plus docs + CMakeLists VERSION bump. Bare binary unchanged at **7.73 MB stripped**.

---

## [0.1.58] — 2026-05-09

**Composer wiring complete — Slices B/C/D land end-to-end (dry_run → Edit Plan → Apply).**

### Fixed — Composer tab is no longer a placeholder

* The v0.1.57 Composer tab body was a literal placeholder QLabel ("Composer — coming in Slice B/C/D"). EditPlanList + DiffView were compiled but never instantiated. v0.1.58 drops `EditPlanList` into the Composer body and wires the full pipeline:
* **dry_run interception** — when `write_file` or `apply_diff` returns `{dry_run: true, proposed: {path, before, after, mode}}`, AIPanel's `handleToolCall` routes the proposal to `EditPlanList::addEdit` instead of touching disk. The dock auto-switches to the Composer tab so the queued edit is visible.
* **Apply pipeline** — `EditPlanList::applyRequested` is connected to a new `AIPanel::applyComposerEdits` slot. Each (absPath, afterText) pair is written atomically (`.tmp + rename`), then `fileWrittenByAgent` fires so the editor opens or reloads the file. Failures surface as a single grouped error bubble; partial success leaves the failed rows visible for retry.
* **Workspace-relative row labels** — `EditPlanList::setWorkspaceRoot` is synced from `setWorkspaceContext`, so per-row paths render `src/foo.cpp` instead of `/home/.../project/src/foo.cpp`.
* **System prompt closes the loop** — `composerMode=true` is now passed to `AiSystemPrompt::build` / `buildWithProjectContext` whenever the user is on the Composer tab in CodingStrict intent. The existing `composerModeLayer` instructs the model to ALWAYS pass `dry_run: true`, so file writes never bypass the Edit Plan review step.

### Tests

21/21 deterministic tests pass. test_aifix needs a live Ollama.

### Stats

2 files changed (`src/aipanel.cpp` + `src/aipanel.h`), +172 / −23 lines. Bare binary unchanged at **~9 MB stripped** (Composer wiring is pure C++ Qt glue, no new heavy deps).

---

## [0.1.57] — 2026-05-09

**Coding-mode revamp + agentic git tools + multi-cursor + fullscreen AI dock + Windows mojibake fix.**

> Largest release since v0.1.55. v0.1.56 was prepared but never released — its content (Windows mojibake fix, multi-cursor scaffolding, fullscreen AI dock, banner / chat-input / llama.cpp polish) is folded into v0.1.57.

### Added — Coding mode revamp (Cursor-style Composer)

* **Composer tab** — separate scrollback + input from the regular Chat tab; appears when Coding Mode is on. Top of the AI dock now shows `[ Chat | Composer ]` tabs.
* **Edit Plan list** — every file the model wants to change in this turn surfaces as a panel of unified-diff cards with **per-hunk Accept / Reject checkboxes**. Bottom action bar: *Apply All* / *Apply Selected* / *Reject All*. New `EditPlanList` and `DiffView` widgets in `src/edit_plan.cpp/h` and `src/diff_view.cpp/h`.
* **`dry_run:true` system layer for Composer** — the model's `write_file` and `apply_diff` calls return proposed-edit payloads instead of touching disk; the agent loop routes them to the Edit Plan instead of the live filesystem.
* **Auto-fullscreen on Coding** — toggling Coding Mode expands the AI dock to fill the window; toggling back to Chat / Data restores the previous splitter width.
* **Coding-mode welcome card** — branch name · ahead/behind upstream · modified file count + quick-action buttons. Renders only when a workspace is open.
* **`@file` mention picker** — typing `@` in the chat input opens a fuzzy-matched workspace files popup. Selected files are prepended to the prompt as real content (not paths).
* **Ctrl+I inline edit** — select code in the editor, press Ctrl+I, type the change in plain English. AI returns the replacement with a side-by-side diff preview; Apply replaces the selection. New `InlineEditDialog` in `src/inlineedit.cpp/h`.

### Added — Agentic git tools (read-only)

Five new tools wired into the agentic loop, **read-only by design** (no writes, no resets, no force-push paths):

* `git_status`
* `git_diff`
* `git_log`
* `git_branch_list`
* `git_show`

Implemented as a `GitTools` namespace (`src/git_tools.cpp/h`) with `QProcess` + 5 s timeout per call, hardcoded verb argv, path-safety on cwd, and `git_show` commit-arg sanitization to refuse anything that isn't a 4–40-char hex SHA. The model can answer "what's my git status?" / "show me the diff on branch foo" by calling these autonomously.

### Added — Multi-cursor editing

* **Alt+drag** for column-select rectangles
* **Ctrl+click** to add a cursor at any caret position
* Type once → same edit lands at every selection simultaneously
* Esc collapses to a single caret

Wired through QScintilla: `SCI_SETMULTIPLESELECTION`, `SCI_SETADDITIONALSELECTIONTYPING`, `SCI_SETMULTIPASTE = SC_MULTIPASTE_EACH`, `SCI_SETRECTANGULARSELECTIONMODIFIER = SCMOD_ALT`, `SCI_SETADDITIONALSELALPHA = 90`.

### Added — Mode-aware model dropdown coloring

The model dropdown now colors entries by capability per active mode: in Coding mode, models without tool-calling render in amber (`#E67E22`) with a hover tooltip; in Data mode, models that lack the recommended Data-Analyst capability surface render amber the same way. Tool-capable / Data-capable models stay accented in green.

### Fixed — Windows mojibake (every emoji and special char)

Every emoji in the UI was rendering as garbage on Windows: 📊 → `ðŸ"š`, 🔒 → `ðŸ""`, 📌 → `ðŸ"Œ`, … → `â€¦`, ≈ → `â‰ˆ`, · → `Â·`. Root cause: MSVC reads C++ source as the system code page (Windows-1252 on en-US) by default. **Fix:** `add_compile_options(/utf-8)` for MSVC in CMakeLists. Linux + macOS were unaffected.

### Fixed — UI polish from v0.1.56 prep

* **Chat input auto-grows** for multi-line messages (40 px → 140 px max), driven by document height (was using the buggy `blockCount()`). Auto-collapses to 40 px after Send.
* **llama.cpp dropdown** — when llama-server isn't running and no GGUF is loaded, the curated 12-model catalog now reads as suggestions to download: disabled header item `— llama-server not running · pick one to download —` at the top, every catalog row prefixed with `↓ `.
* **Banner truncation** — Data Mode capability banner moved to its own row + `setHeightForWidth(true)` + `QSizePolicy::Ignored` horizontal so the panel resizes without clipping when the dock is narrow.
* **llama.cpp install probe** — distinguishes "installed but not running" from "not installed" via `QStandardPaths::findExecutable`, so the status hint is actionable.

### Fixed — Crash hardening

Null-pointer guards, network timeouts, and structured error surfacing across `aipanel.cpp`, `ollama.cpp`, and `ai_tools.cpp`. Backend disappears mid-stream? Cloud key revoked? Tool returns malformed JSON? The dock shows a clean error and stays alive — no crash, no white screen, no lost session history. ~100 hardening annotations marked `// hardening:`.

### Tests

24 → 25 test files (added `test_edit_plan.cpp`). New cases cover dry-run write_file/apply_diff, all 5 git tools' success + error paths, EditPlan model add/remove/select, DiffView rendering for empty / single-line / multi-hunk diffs.

### Stats

24 commits since v0.1.55, 28 files changed, +4,390 / −244 lines. Bare binary **~9 MB stripped** (8.93 MB Linux x64) — basically flat from v0.1.55. New widget code is C++ Qt and compiles tight.

---

## [0.1.55] — 2026-05-09

**DuckDB engine, Azure OpenAI, Ollama Cloud, 80+ language lexers, privacy toggle, credential scrubber.**

### Added — Cloud AI

* 4 cloud backends with per-provider key slots (no cross-provider bleed): **OpenRouter**, **OpenAI**, **Ollama Cloud**, **Azure OpenAI**.
* New AI Settings dialog with Test/Save/Forget per provider.
* Searchable model dropdown — type `xai`/`grok`, `claude`, `gpt`, `kimi`, `gemini` to surface the right group.
* Ollama `/api/show` capability probe — new tool-trained models work without a Notepatra release.
* OpenRouter `reasoning` parameter routing — Think checkbox now actually works for Claude / o-series / Gemini.

### Added — DuckDB native engine for Data mode

`libduckdb-1.1.3` linked dynamically (RAII C wrapper, structurally leak-proof, streaming row callback, schema introspection, httpfs/S3/Parquet/CSV/JSON view registration). New `DUCKDB` driver in DB Connections panel.

### Added — Database Tree dialog (Browse Schemas...)

Connection → schemas → tables → columns, lazy-loaded, right-click "Send schema to AI" / "Sample 10 rows" / "Copy SELECT *".

### Added — 32 dedicated language lexers (47 → 80+)

Dart · Solidity · Zig · Vala · Hack · Julia · R · Protobuf · F# · HCL/Terraform · Thrift · GraphQL · GDScript · Nim · Cython · Mojo · Crystal · Elixir · Scala · Groovy · Apex · Jinja · Liquid · Twig · Dockerfile · Fish · Nushell · TOML · DotEnv · Gitignore · JSON5 · BibTeX. Each keyword table verified against the official spec by parallel research agents. Comment toggling (Ctrl+Q / Ctrl+Shift+Q) wired for every new language.

### Added — Privacy

* **Credential scrubber** catches 14 vendor patterns (OpenRouter / Anthropic / OpenAI / GitHub / GitLab / AWS / Slack / Stripe / SendGrid / Google / JWT / PEM / generic key=value) before any text leaves the machine.
* New 🔒 **Share file with AI** toggle (default OFF, Coding mode only) — Chat / Data modes never see file content.
* Welcome page emoji fallback chain — Linux tofu boxes (□) for icons replaced with real glyphs.

### Added — Multi-file analyst context

`.notepatra/data-analyst/` directory loader (instructions / data-dictionary / business-rules / KPIs / sample-queries).

### Tests

21/21 pass — added `test_credscrub` (14 vendor patterns) + `test_duckdb` (streaming + RAII + schema).

### Binary size

**~9 MB** Linux x64 stripped (basically flat from v0.1.54 — DuckDB is a separate `.so` users install once).

---

## [0.1.54] — 2026-05-08

**Backend dropdown clean-up + AI panel wiring fixes + Search icon polish.**

### Removed — three rarely-used backend dropdown entries

* **`Custom`** — promised free-form OpenAI-compat URLs but offered nowhere
  in the AI panel chrome to type the URL, so picking it was a no-op.
  Power users running vLLM / KoboldCpp / text-generation-webui /
  llamafile on a non-standard port can still reach those backends by
  picking `llama.cpp` and setting `aiBaseUrl` via Settings → Preferences
  → AI.
* **`LM Studio`** and **`Jan`** — both are GUI Electron apps that just
  wrap llama.cpp's HTTP server. Ollama covers the easy local case and
  llama.cpp covers the power-user case; the curated catalog of 12 GGUF
  models in the model dropdown means users don't need a separate "GUI
  catalog" app. Removing these reduces dropdown noise + maintenance
  burden when LM Studio / Jan rev their port or API.

The dropdown is now exactly **4 entries**: Ollama / llama.cpp (GGUF) /
OpenRouter (cloud) / OpenAI.

### Fixed — Ollama not re-detected after switching back from cloud

Pre-fix, the backend-change handler did `if (!cfg.aiBaseUrl.isEmpty())
m_ollama->setBaseUrl(...)`, so switching cloud → Ollama (where
`aiBaseUrl` is cleared) skipped the call and `m_ollama` kept pointing at
the cloud URL. The `/api/tags` probe then went to the wrong host and
returned no models, leaving the dropdown stuck on "(Ollama offline)".
The fix: always call `setBaseUrl` with the explicit URL OR the backend's
documented default (`localhost:11434` for Ollama, `localhost:8080` for
llama.cpp).

### Fixed — model dropdown disabled for backends that don't need a probe

`OllamaClient::modelsError` previously cleared and disabled the combo
unconditionally, killing the curated catalog logic added in v0.1.53.
Now dispatched by backend: `llama.cpp` / OpenRouter / OpenAI keep their
catalog visible (with an actionable status hint) even when the live
probe fails.

### Changed — Data Analyst banner uses family names instead of version pins

Pre-fix the banner read "*Try Claude Sonnet 4.5 / GPT-5 / Gemini 2.5
Pro*" — names that go stale every couple of months as new model versions
land. Now it reads "*Try a strong local code model (Qwen-Coder /
DeepSeek-Coder / Llama 7B+) or a frontier cloud model (Claude / GPT /
Gemini)*". Family names stay accurate for years.

`AiTools::suggestedModelsForDataAnalysis()` updated the same way.

### Changed — Banner colours theme-aware

Old: hard-coded brown background (`#553B19`) + cream text (`#FFD49A`) —
fine on Dark, muddy/illegible on Light. New: theme-reactive — Light
gets cream `#FFF1D6` bg + dark amber `#7A4A0E` text + soft border;
Dark gets brown bg + cream text. Both highly readable.

### Fixed — Project Search toolbar icon's lens edge anti-aliased into the rounded corner at 150 % DPI

The magnifying-glass lens used `center = 0.40 × width`, `radius =
0.28 × width`, putting its top-left stroke pixel at logical
`(3.6, 3.6)` — only `0.36 px` inside the rounded square's corner curve.
At 150 % DPI antialiasing smeared the corner curve and the lens stroke
into the same physical pixel and it read as "lens clipped at top". Bumped
to `center = 0.44`, `radius = 0.25` — top-left stroke pixel now sits
`2.0 logical px` inside the corner, well clear of the curve at every DPI.

### Added — `OllamaClient::modelsListedRich(QJsonArray)` signal

For OpenAI-compat backends (OpenRouter / OpenAI), `listModels()` now
also emits the raw `data` array from `/v1/models`, carrying pricing,
context length, and provider metadata. Ground-work for the upcoming
v0.1.55 live model picker UX (grouped by provider, price column,
24 h disk cache).

### Tests

C++ 19 / 19 + Rust 119 / 119 still pass. Build clean.

---

## [0.1.53] — 2026-05-08

**Curated model lists per backend + Data Analyst welcome card.**

### Added — curated model lists

The model dropdown previously showed whatever the live `/v1/models` endpoint
returned: useful for Ollama (your pulled models) but useless for OpenRouter
(100+ unsorted models) or for `llama.cpp` (just the loaded one). Now each
backend gets a curated catalog when relevant:

* **`llama.cpp` backend**: 12 popular GGUF models (Qwen2.5-Coder 1.5B / 7B /
  14B, Qwen2.5 7B, Llama 3.2 3B / 3.1 8B, Phi-4 14B, Gemma 2 2B / 9B,
  Mistral 7B v0.3, DeepSeek-Coder-V2-Lite, StarCoder2 3B). Each entry's
  tooltip carries the HuggingFace direct-download URL. The currently-loaded
  model (if `llama-server` is running) appears at the top with a `●`
  marker; the catalog follows below as recommended picks.
* **OpenRouter backend**: 13 cross-provider picks — Claude Sonnet 4.5 / Opus
  4.5 / Haiku 4.5, GPT-5 / GPT-5 mini / GPT-4o / o1-mini, Gemini 2.5 Pro /
  Flash, DeepSeek R1, Llama 3.3 70B, Qwen2.5-Coder 32B, Mistral Large.
  Tooltip shows price per million tokens where known. Live `/v1/models`
  output is appended below.
* **OpenAI direct backend**: 6 official models — GPT-5 / GPT-5 mini /
  GPT-4o / GPT-4o mini / o1 / o1-mini.
* **LM Studio + Jan**: unchanged from before — show whatever the local
  server is serving via `/v1/models`. Each app has its own download UI;
  Notepatra doesn't duplicate it.

### Added — Data Analyst welcome card (`AIPanel`)

When the user toggles into Data Analyst mode on a fresh chat, a styled
orange card now appears at the top of the chat area showing:

* **Title**: "📊  Data Analyst Mode"
* **One-paragraph explainer**
* **Three clickable example prompts** (chips) — clicking fills the input
  with the example
* **Connection status** — number of saved DB connections + a "Manage
  Connections…" button that opens the CRUD dialog
* **Model capability indicator** — "✓ capable for Data mode" (green) or
  "⚠ too small for multi-table SQL" (orange) with a recommended fix
  (`ollama pull qwen2.5-coder:14b` for local users, or a frontier cloud
  model)
* **Hide button** — sticky via `Config::aiHideDataWelcome`

The card removes itself once the chat has any content (one prompt sent)
and re-appears if the user clicks Reset to clear the chat.

### Fixed — capability banner now mentions a local fix

Pre-fix, the orange "model too small" banner only suggested cloud models
("Try Claude Sonnet / GPT-5 / Gemini 2.5 Pro"), making local-Ollama users
think they had to pay for a cloud API. New banner: "Try
`ollama pull qwen2.5-coder:14b` (~9 GB) or a cloud model
(Claude Sonnet 4.5 / GPT-5 / Gemini 2.5)". One tight line that fits
alongside the welcome card.

### Tests

C++ 19 / 19 + Rust 119 / 119 still pass. Build clean, no warnings.

---

## [0.1.52] — 2026-05-08

**Toolbar icon HiDPI rendering + Project Search button visibility.**

### Fixed — toolbar icons stayed pixelated at 150 % DPI

v0.1.50's `Qt::AA_UseHighDpiPixmaps` was the right global flag, but
`makeFeatureIcon()` was still rasterizing each toolbar icon (Search /
AI / Terminal / Compare / JSON / HTML / SQL / Brackets / REST / Git)
into a fixed `QPixmap(32, 32)`. On a 150 % display Qt then bilinear-
scaled the 32-px pixmap to ~48 device px → blurry. The fix:

* Multiply backing-store size by `qApp->devicePixelRatio()` so the
  pixmap holds enough pixels for the actual display density.
* Tag the pixmap with `setDevicePixelRatio(dpr)` so Qt treats it as
  logical 32×32 (no second scale on draw).
* `painter.scale(dpr, dpr)` so every `drawXxxFeatureGlyph()` helper's
  "32" still means "32 logical px"; the rounded-rect, gradient, and
  glyph render at sub-pixel precision into the high-density backing
  store.

Net effect: at 100 % the icon looks identical to before; at 125 % /
150 % / 175 % / 200 % it now stays sharp. **Buttons are not affected**
— all of v0.1.49's per-button minimum-width fixes are preserved, and
v0.1.50's `AA_EnableHighDpiScaling` + `PassThrough` policy still apply.

### Fixed — Project Search Cancel + Clear history button labels invisible

User reported the labels were nearly invisible on every theme. Cause:
the previous styling used `p.textPrimary` for enabled state (dark grey
on Light, light grey on Dark) and `#AAA` for disabled. On Light theme,
`#AAA` on a beige background read as "blank rectangle". Both buttons
now use a strong orange (`#E67E22`) with bold weight on every theme
state — clearly visible on Light, Dark, and Monokai. Hover brightens
to `#FFA94D`; disabled dims to `#C97B3F` (still visible).

### Tests

C++ 19 / 19 + Rust 119 / 119 still pass. Build clean.

---

## [0.1.50] — 2026-05-08

**HiDPI / fractional-zoom support — fixes Windows 125 % / 150 % / 175 %.**

User reported Bracket Tools / HTML Tools / SQL Tools button labels were
still being cut on Windows even after v0.1.49's per-button minimum-width
fix. Root cause: Notepatra had **no Qt application attributes set for
HiDPI scaling at all**. Qt 5.15's default scale-factor rounding policy
is `Round`, so on Windows at 150 % display zoom the app actually rendered
at **200 %** — every button, font, and icon was 33 % wider than the
layout was sized for. At 125 % it rendered at 100 % (everything tiny).
At 175 % it rendered at 200 % (same as 150 %).

### Fixed — three Qt attributes set BEFORE QApplication construction

```cpp
QGuiApplication::setHighDpiScaleFactorRoundingPolicy(
    Qt::HighDpiScaleFactorRoundingPolicy::PassThrough);
QCoreApplication::setAttribute(Qt::AA_EnableHighDpiScaling, true);
QCoreApplication::setAttribute(Qt::AA_UseHighDpiPixmaps, true);
```

* **`AA_EnableHighDpiScaling`** opts the app into Qt's logical-pixel
  coordinate system. `setFixedHeight(26)` now means "26 logical px";
  Qt translates to device pixels based on each monitor's DPI.
* **`HighDpiScaleFactorRoundingPolicy::PassThrough`** uses the OS scale
  factor exactly (1.25, 1.5, 1.75 …) instead of rounding to integers.
* **`AA_UseHighDpiPixmaps`** opts into @2x bitmap variants for QIcon /
  QPixmap so toolbar icons + the leaf-circuit logo stay crisp on
  Retina / 200 % displays.

### Side-effect for Linux + macOS

Linux distros that report fractional fontconfig DPI (typically 1.25 ×
on a 1080p screen with `Xft.dpi: 120`) also get exact-DPI rendering
instead of rounded-to-100 %. macOS Retina was already handled by Qt's
default macOS path; nothing changes there.

### Tests

C++ 19 / 19 + Rust 119 / 119 still pass. Build clean.

---

## [0.1.49] — 2026-05-08

**Windows button-truncation fixes + Compact SQL formatter.**

### Fixed — button labels truncated on Windows

User reported multi-word button labels (`Format (2 spaces)`, `Generate Docs`,
`Add Comments`, `Write Tests`, `Insert at Cursor`, `Replace Selection`, …)
got cut off with `…` on Windows. Cause: Segoe UI (Windows default) is
~20% wider than DejaVu Sans Mono (Linux default) for the same point size,
and these buttons had `setFixedHeight(N)` but no minimum width, so layout
squeeze on narrow rows truncated the text. Fix: every relevant button now
calls `setMinimumWidth(fontMetrics().horizontalAdvance(text) + 22..28)` so
the natural label width + a safety margin is always honoured. Linux/macOS
unchanged because the natural width was already wide enough.

Touched:
- `FormatterPanel::addButton` (covers JSON / HTML / Bracket Tools' Format,
  Minify, Fix+Format, Check, Auto-Fix buttons)
- `FormatterPanel`'s built-in `Show Diff` and `Copy Output` buttons
- AI panel quick-action rows: Explain · Find Bugs · Refactor · Write Tests
  · Add Comments · Generate Docs · Optimize · Translate · Fix JSON · Fix
  HTML · Fix SQL · Insert at Cursor · Replace Selection · Copy
- SQL Formatter panel: Format · Compact · AI Fix (Ollama) · Copy Output

### Added — SQL Formatter "Compact" button

A second button next to "Format" emits a one-line-where-possible rendering
of the same parsed AST. Short queries stay on one line; long queries
break only at major clause boundaries (`SELECT` / `FROM` / `WHERE` /
`GROUP BY` / `ORDER BY` / `RETURNING`). Same dialect coverage as Format
(T-SQL, PostgreSQL, MySQL, SQLite, Oracle, ANSI). New Rust public fn
`format_sql_compact` + new FFI `npc_format_sql_compact` + new C++ wrapper
`RustCore::formatSqlCompact`. 4 new Rust unit tests verify: short SELECT
stays single line, long SELECT breaks at clauses, PostgreSQL UPSERT
round-trips, T-SQL `TOP` is preserved.

### Tests

- Rust unit tests: **119 / 119 pass** (was 115; 4 new for compact SQL).
- C++ tests: **19 / 19 pass**.

---

## [0.1.48] — 2026-05-08

**AI Assistant UX overhaul + JSON / HTML / Bracket / SQL hardening pass.**

### Changed — AI Assistant panel

- 🎛️ **3-way mode segmented selector replaces the cluttered checkbox row.**
  The top toolbar was previously seven controls jammed into one line
  (`Ollama ▾ · model ▾ · ↻ · ☐ Coding · ☐ Data · ☐ Think · Reset`).
  Now split cleanly into two rows: model selection on top, mode picker
  `[ Chat | Coding | Data ]` plus `Think` checkbox below. Chat is the
  explicit default ("no flag" used to be implicit and unlabeled).
- 📐 **AI dock opens at ~50% of window width by default.** Previously it
  opened narrow and the user had to drag the splitter every session.
  First-toggle-per-session sizing; manual drag is respected after.
- 🐛 **Fixed two dead-code `connect()` calls** in the AI panel constructor:
  `connect(m_dataMode, …)` and `connect(m_manageConnsBtn, …)` were both
  wired BEFORE their widgets were constructed (they were `nullptr`
  pointers), so toggling the Data checkbox or clicking "Manage
  Connections…" went through fallback paths instead of the proper
  handlers. Reordered so the connects fire correctly.
- 🪟 **Coding Mode now actually opens the file explorer.** When the AI
  dock had been sized to 50/50, the splitter slot for the explorer was 0,
  so `setVisible(true)` produced a zero-width strip. Re-allocates 220 px
  from the editor tabs when Coding Mode is engaged.

### Changed — JSON / HTML / Bracket Tools (AI Fix prompt parity)

- 🛡️ **HTML Tools and Bracket Tools AI Fix prompts brought up to JSON
  Tools' strict "minimal change" framework.** User reported the AI was
  occasionally adding new tags/fields/code statements when fixing
  format. New prompts use the same numbered-rules pattern JSON has:
  "PRESERVE all content", "Do NOT add new tags", "Do NOT 'improve'
  the code", explicit Gemma/Phi-aware rules-in-user-prompt.
- ✨ **HTML Tools + Bracket Tools now have full output cleanup pipeline
  parity with JSON:** strip `<think>` blocks, strip ``` fences, strip
  prose prefixes, empty-response fallback with raw-response display.
- 📊 **HTML Tools + Bracket Tools now wire Show Diff, recordFix,
  logAction, and a Show-thinking checkbox** — all the JSON Tools UX
  features that were missing.

### Fixed — Rust core hardening (rust-core/)

- 🐛 **SQL T-SQL `TOP N` clause was silently dropped during formatting.**
  The AST `Select.top` field wasn't read by the writer, so
  `SELECT TOP 10 * FROM t` formatted to `SELECT * FROM t`. Real bug,
  fixed.
- ⚡ **Bracket fixer no longer O(n²) on big inputs.** `result.insert(0, …)`
  per missing opener used to allocate + copy the whole string each
  call. Now we allocate once with the right capacity and prepend in
  O(n).
- 🔒 **Bracket string-state tracking now uses separate single/double
  quote state.** Previously a single `in_string` flag was shared, so
  apostrophes inside double-quoted strings (`"don't"`) and quoted
  literals adjacent to other quotes mis-toggled the flag and corrupted
  the bracket count.
- 🔒 **Bracket backslash-escape detection now counts consecutive
  backslashes** (odd = escape next, even = literal). Previously
  `"a\\"` was parsed as an unclosed string because the closing quote
  was treated as escaped.
- 🛡️ **All three formatters (JSON, HTML, Bracket, SQL) hard-cap input at
  50 MB** with a graceful safe-marker return — was unbounded before.
- 🛡️ **JSON regexes now compile once via `OnceLock`** with safe
  `.ok()` fallback (skip pass instead of panic if a regex ever fails
  to compile across the C ABI boundary).
- 🛡️ **JSON `pretty_print` no longer panics on serialization error** —
  falls back to manual pretty-print so the user always gets output.

### Tests

- 16 new Rust unit tests in `rust-core` covering: empty input, already-
  valid passthrough, missing close brace, trailing comma, unquoted key,
  deeply nested 1000-level structure, oversize-input safe marker,
  mixed quotes in string values, garbage no-crash, T-SQL TOP, T-SQL
  bracketed identifiers, PostgreSQL `::cast`, PostgreSQL LATERAL,
  multi-CTE WITH, idempotent SQL formatting, huge-bracket-run linear
  performance, escaped-quote string handling.
- All 19 existing C++ tests still pass.

---

## [0.1.47] — 2026-05-08

**Icon refresh.** User reported the Notepatra taskbar icon looked
visibly smaller than other apps' icons. The artwork was correct but
the leaf+circuit graphic only filled ~60% of the canvas, leaving lots
of dark padding around it — at small taskbar sizes (16×16, 24×24) the
leaf shrunk to a thumbnail and the icon read as "small black square".

### Changed

- 🎨 **All icons regenerated with the leaf scaled up ~22% within the
  rounded-square frame.** The leaf now fills ~85% of the canvas, so
  the icon reads larger at every taskbar / dock / file-manager size.
- All standard PNG sizes regenerated from the new 1024px master:
  16, 24, 32, 48, 64, 128, 256, 512, 1024 + the unsuffixed
  `notepatra.png` used as the Linux desktop icon.
- Windows `.ico` rebuilt with the 7 standard embedded sizes (16, 24,
  32, 48, 64, 128, 256).
- macOS `.icns` regenerated from the new master so the Dock + Finder
  + Mission Control icons all pick up the change.

---

## [0.1.46] — 2026-05-08

**Hotfix on top of v0.1.45.** Two issues: stacking didn't actually
work for the most-common Find All path, and the new ✕ close button
floated awkwardly on an empty SearchResultsPanel even before any
search had been run.

### Fixed

- 🔍 **`doFindAllCurrent` no longer calls `sr->clear()`**. v0.1.45 added
  the new `beginSession()` call but left a stale `sr->clear()` line
  immediately above it, so every Find All in Current wiped the panel's
  entire session history right before adding the new session. Symptom:
  stacking never visible, previous searches always gone. Removed.
- ❌ **Close ✕ on the SearchResultsPanel is now hidden until at least
  one session exists.** Pre-fix the ✕ floated at the right edge of an
  empty panel header even before any search had been run, looking like
  leftover UI noise. Now: panel starts with the ✕ hidden; the first
  `beginSession` call shows it; an explicit `clear()` hides it again.
- All three Find paths now have consistent behaviour: each search
  creates a new session at the top of the tree; previous sessions
  collapse; capped at 10 with oldest pruned.

---

## [0.1.45] — 2026-05-08

**Hotfix on top of v0.1.44.** v0.1.44 added a close button + stacked
search history to `ProjectSearch` (the tab opened with Ctrl+Shift+G), but
Find in Files (Ctrl+Shift+F) populates a different widget — the
bottom-docked `SearchResultsPanel` — which still had no close button and
still wiped its history on every search. The right-click comment menu
also lacked explicit Comment / Uncomment items separate from the toggles.
Both fixed.

### Fixed

- 🔍 **`SearchResultsPanel` (the bottom-docked Find-in-Files panel) now
  has the same red ✕ close button + Notepad++-style stacked history**
  that `ProjectSearch` got in v0.1.44. Each Find All / Find All in All
  Open Documents call becomes a new top-level **session** row
  (`🔎 Search "needle" — N hit(s) in M file(s) · HH:MM:SS`); previous
  sessions auto-collapse, capped at 10. Pressing the red ✕ hides the
  panel without losing the history. Was wired through the wrong widget
  in v0.1.44 — fixed.
- 💬 **Explicit Comment / Uncomment menu items, Notepad++-style.** The
  v0.1.44 right-click menu only had Toggle Line / Toggle Block; users
  who wanted a one-direction action ("just uncomment this") had to know
  the toggle would do the right thing. Now each kind has both a Toggle
  and explicit Comment-only / Uncomment-only items:
  - `Toggle Line Comment` (Ctrl+/, Ctrl+Q) — toggles state
  - `Toggle Block Comment` (Ctrl+Shift+Q) — toggles state
  - `Comment Line` (Ctrl+K) — always inserts the line marker
  - `Uncomment Line` (Ctrl+Shift+K) — always strips the line marker
  - `Comment Block` — always wraps in the block markers
  - `Uncomment Block` — always strips the block markers
  Ctrl+K / Ctrl+Shift+K match Notepad++'s defaults. All six items are
  in the right-click context menu AND the Edit → Comment/Uncomment
  submenu. Disabled greyed-out for languages without that kind of
  comment (Plain Text, Diff, etc.).

### Internals

- `src/searchresults.{h,cpp}` — header redesigned as a row with title +
  red ✕ close button. New `closeRequested` signal. New `beginSession`
  method opens a top-level session item; `setHeader` now updates the
  active session's label with totals; `addFileSection` /
  `addResultLine` add under the current session. Stacked sessions
  capped at 10; oldest pruned when over.
- `src/findreplace.cpp` — single-file Find All and multi-file Find All
  in All Open Documents both call `beginSession(needle)` first, then
  `setHeader` for the final label. The legacy `clear()` call is gone
  from the multi-file path.
- `src/mainwindow.cpp` — wires `SearchResultsPanel::closeRequested`
  → `setVisible(false)`; adds Comment Line / Uncomment Line / Comment
  Block / Uncomment Block actions to the Edit → Comment/Uncomment
  submenu with NPP-default shortcuts.
- `src/editor.{h,cpp}` — new `commentLine` / `uncommentLine` /
  `commentBlock` / `uncommentBlock` methods (one-direction, idempotent).
  Refactor: shared `commentSyntaxFor()` lookup. `uncommentLine` only
  strips when the comment marker is the FIRST non-whitespace token, so
  it doesn't mangle inline `--` in SQL expressions.
- Right-click context menu in the editor now has 6 comment-related
  items grouped after the standard Cut / Copy / Paste / Select All.

---

## [0.1.44] — 2026-05-07

**Project Search UX + language-aware comment toggling.** Three Notepad++-style
quality-of-life upgrades the editor was missing:

### Added — Project Search

- **Close button (red ✕) on the search panel header** — sits at the far right of
  the title row, theme-independent (Windows-canonical close-button red `#E81123`
  at rest, white-on-red on hover). Emits `ProjectSearch::closeRequested`; the
  host removes the panel's tab. Replaces the previous "no way to dismiss the
  panel" UX where users had to right-click the tab.
- **Stackable, collapsible search history** — pressing Search no longer wipes
  the prior results. Each search now becomes a top-level **session row** in the
  results tree with the query, flags, folder, hit count, and timestamp:
  `🔎  "pwd"  Aa W .* — 5 hits in 1 file · 12:34:07 · /path`. Files of that
  search are children; matches are grandchildren. Previous sessions auto-collapse
  so the new one stands out. Capped at **10 sessions**; oldest pruned.
- **Clear history button** in the action row wipes every stacked session in one
  click. Disabled when the tree is empty.

### Added — Editor

- **Right-click `Toggle Line Comment` and `Toggle Block Comment`** in the editor
  context menu, **enabled only when the language is recognized** (no more
  "comment everything as `#`" no matter the file type — the pre-v0.1.44
  behaviour silently mangled Markdown by prepending `#`, turning paragraphs
  into headings).
- **Language-aware comment syntax** via the new public static helper
  `Editor::commentSyntaxFor(lang) → {line, blockOpen, blockClose}` — single
  source of truth so the right-click menu, the menu-bar Comment/Uncomment
  submenu, and the keyboard shortcuts all agree.
  - **Line comment** support: Python · Bash · YAML · Ruby · Perl · PowerShell ·
    TCL · CMake · Makefile · Properties (`#`) · C/C++/C#/Java/JS/TS/D/Rust/Go/
    Swift/Kotlin/Verilog/POV (`//`) · SQL/VHDL/Lua (`--`) · ASM/NASM/MASM/IDL/
    Spice (`;`) · TeX/PostScript/Matlab/Octave (`%`) · Fortran (`!`) ·
    CoffeeScript (`#`) · Batch (`REM `).
  - **Block comment** support: C-family + CSS + Pascal (`/* */`) · HTML/XML/
    Markdown (`<!-- -->`) · PowerShell (`<# #>`) · Lua (`--[[ ]]`) ·
    Matlab/Octave (`%{ %}`) · CoffeeScript (`### ###`) · Ruby (`=begin =end`).
  - Plain text and unknown extensions return empty strings → menu items show
    "(no syntax for X)" and are disabled.
- **`Editor::toggleBlockComment()`** — wraps the selection (or current line) in
  the language's block markers; if already wrapped, strips them. Atomic round
  trip: wrap then unwrap returns the original text.
- **Notepad++ shortcuts**: `Ctrl+Q` for line comment (in addition to the
  pre-existing `Ctrl+/`), `Ctrl+Shift+Q` for block comment.

### Internals

- `src/editor.{h,cpp}` — new `CommentSyntax` struct + `commentSyntaxFor()`
  static helper, `toggleBlockComment()` method, `contextMenuEvent()` override
  that builds Cut/Copy/Paste/Select All + the two new comment toggles. Refactor
  removes the inline `name.contains("CPP")` ladder from `toggleComment()` —
  every language now flows through one canonical map.
- `src/projectsearch.{h,cpp}` — new `closeRequested` signal, `m_currentSession`
  pointer + `m_sessions` history list + `m_perSessionFiles` per-session
  file-row index. `startSearch()` no longer calls `m_results->clear()`.
- `src/mainwindow.cpp` — wires `ProjectSearch::closeRequested` → tab removal;
  binds `Ctrl+Q` (in addition to `Ctrl+/`) and `Ctrl+Shift+Q` to the menu-bar
  Comment/Uncomment submenu.
- `test_options_actually_work.cpp` — +25 assertions covering
  `commentSyntaxFor()` output for Python, JS, C++, SQL, Lua, HTML, Markdown,
  PowerShell, Bash, Batch, Plain Text, and an unknown language.

---

## [0.1.43] — 2026-04-30

**Data Analyst Mode** — the AI assistant gains a real data-analyst capability: query CSVs and saved database connections, generate inline charts, and read project-level instructions automatically. Fully native — Qt SQL + QtCharts, no Python sandbox, no external chart service. Toggle is mutually exclusive with Coding Mode so the panel stays focused.

### Added — Data Analyst Mode

- 📊 **`Data` toggle** in the AI panel header alongside Coding Mode. When on, the header band switches to an accent-orange "AI · 📊 DATA" so it's unmistakable which mode is active. State persists across launches via `Config::aiDataMode`.
- 🗂 **Database connection manager** (`Manage Connections…` button, visible only when Data Mode is on). Add / edit / test / delete connections; each record stores driver (QSQLITE / QPSQL / QMYSQL / QODBC), host, port, database, username, password, options. Saved at `~/.config/notepatra/db-connections.json`. Passwords are obscured at rest (XOR + base64) — **NOT real encryption**; documented honestly. Use OS keychain / `.pgpass` for production secrets.
- 🛠 **Two new agentic tools** wired into the agent loop:
  - `query_sql(connection_name, sql, max_rows?, confirm?)` — runs SQL against a saved connection. SELECT / WITH / EXPLAIN / PRAGMA / SHOW / DESCRIBE allowed by default; mutations (INSERT / UPDATE / DELETE / DDL) require `confirm:true` after explicit user approval. Caps results at 500 rows.
  - `csv_query(file_path, sql, max_rows?, max_load_rows?)` — loads a workspace CSV into in-memory SQLite (table name `csv`, column names match the header), runs the SQL, returns rows. The model can ask `SELECT category, SUM(revenue) FROM csv GROUP BY category` instead of scanning a 50 MB file as text.
- 📈 **Inline chart rendering** via QtCharts. The model emits a fenced `​```chart` block:
  ```
  {"type":"bar","title":"Revenue by quarter","x":"quarter","y":"revenue",
   "data":[{"quarter":"Q1","revenue":1200},{"quarter":"Q2","revenue":1850}]}
  ```
  Notepatra parses each one and embeds a real `QChartView` (interactive, theme-aware) under the assistant's prose. Supported types: `line`, `bar`, `pie`, `scatter`. Malformed JSON falls back to displaying the spec as a code block — nothing breaks.
- 🧠 **Smart CSV preview** — when a CSV is attached AND Data Mode is on, the preview the model sees is a structured digest: detected delimiter, header inference, per-column type (Integer / Real / Boolean / Date / DateTime / Text), null counts, min/max ranges, and N head + N tail rows. Capped at 4 KB. The full file stays accessible via `csv_query`.
- 📝 **`.notepatra/data-analyst.md` instruction file** — when present in the workspace, its contents are auto-prepended to the system prompt as a "Project data context" layer. Per-workspace, version-controllable, capped at 8 KB. Lets you tell the model "always join orders to customers on customer_id", "treat NULL in `amount` as 0", or share the schema in plain prose.
- ⚠️ **Model capability gating** — `AiTools::modelCapableOfDataAnalysis` allowlists frontier cloud models (Claude 4.x, GPT-4/5, Gemini 2.x, DeepSeek-V3) and local models ≥7B params from strong families (qwen2.5-coder, llama3.x, mistral-large). When Data Mode is on with a model below the bar, an inline orange banner suggests a few capable alternatives. Mode still works — banner is the heads-up.

### Added — Plumbing
- New `AiSystemPrompt::Intent::DataAnalyst` — separate from Chat / Explain / Transform / CodingStrict. Has its own system-prompt body (data-analyst persona, structured Findings → Method → Suggested follow-ups output, chart-spec emission rules) and its own tool-mode preamble that mentions `csv_query` / `query_sql` instead of file ops.
- `AiSystemPrompt::buildWithProjectContext` and `readDataAnalystInstructions` — composable layers for project-level data context.
- `src/csvanalyst.{h,cpp}` — schema detection (delimiter sniff, header probe, type inference) + preview generation + in-memory SQLite ingestion.
- `src/dbconnections.{h,cpp}` — Record struct, JSON persistence, `runQuery` (SELECT-only by default), driver-availability detection, the connection-manager dialog.
- `src/chartrender.{h,cpp}` — JSON-spec → `QChartView` widget; supports line / bar / pie / scatter with category-aware axes.

### Tests
- New `test_ai_dataanalyst` — ~50 assertions covering Intent classification (data flag wins over action; coding still wins over data), system prompt phrasing, `shouldAttachWorkspace=false` for DataAnalyst, project-context prepend + 8KB cap, instruction-file read, CSV schema (comma + tab, header detection, Integer / Real / Boolean / Date inference), CSV preview byte cap, `looksLikeCsv`, in-memory SQLite ingestion, password obfuscation round-trip, Record JSON round-trip, driver predicates, real SQLite SELECT + DELETE rejection, model capability gating (positive + negative), tool registry, chart spec parsing (bar / pie / malformed JSON).
- 18/18 ctest suites green on Linux baseline.

### Driver availability
SQLite ships with Qt by default — works out of the box on every Notepatra build. PostgreSQL / MySQL / SQL Server require the matching Qt SQL plugin to be installed:
- Debian / Ubuntu: `sudo apt install libqt5sql5-psql libqt5sql5-mysql libqt5sql5-odbc`
- macOS Homebrew: included in `brew install qt@5`
- Windows: install via `aqtinstall` `addons.qtcharts` and SQL plugins; documented in [docs/docs.html](https://notepatra.org/docs.html).
The Manage Connections… dialog reports which drivers are available on your system.

### Files changed
```
NEW:
  src/csvanalyst.{h,cpp}            — CSV schema + preview + SQLite ingest
  src/dbconnections.{h,cpp}         — connection model + dialog + runQuery
  src/chartrender.{h,cpp}           — chart spec → QChartView
  test_ai_dataanalyst.cpp           — pure-logic regression suite
  release_notes/v0.1.43.md          — full release notes

MODIFIED:
  src/ai_systemprompt.{h,cpp}       — Intent::DataAnalyst, buildWithProjectContext,
                                       readDataAnalystInstructions, dataMode flag
                                       on classifyIntent (default false to keep
                                       2-arg call sites compiling)
  src/ai_tools.{h,cpp}              — query_sql + csv_query tools, error kinds
                                       no_connection / non_select / open_failed /
                                       exec_failed; modelCapableOfDataAnalysis +
                                       suggestedModelsForDataAnalysis
  src/aipanel.{h,cpp}               — m_dataMode toggle + Manage Connections… btn
                                       + capability banner; smart CSV preview hook;
                                       inline chart rendering in aiAddAssistantCard
  src/config.h                       — aiDataMode field load/save
  CMakeLists.txt                    — Qt5 Sql + Charts; new sources; test target;
                                       VERSION 0.1.42 → 0.1.43
  .github/workflows/build.yml       — apt: libqt5charts5-dev libqt5sql5-sqlite
                                       Windows: install-qt-action modules: qtcharts
                                       Added test_ai_dataanalyst to all 4 target lists
  CHANGELOG.md                      — [0.1.43] entry
  README.md                         — v0.1.43 row in releases table
  docs/index.html                   — v0.1.43 LATEST card
  docs/docs.html                    — Data Analyst Mode section
```

---

## [0.1.42] — 2026-04-30

User-flagged fix bundle right after v0.1.41 — five small things that
add up to a noticeable polish pass.

### Changed
- ✂️ **Diff only is now the DEFAULT** in Compare. The most common reason to compare two files is "show me what changed", not "show me the whole files." Users who want full files (e.g. for context around a single edit) untick the prominent **Diff only** checkbox in the toolbar — now styled with a green border + bold text so it's immediately obvious you're in filtered mode.
- 🩹 **Edit-toggle auto-disables Diff only.** Clicking **Unlock Editing** in Compare automatically unticks Diff only and recompares in full-files view first, then unlocks the panes. Re-locking re-runs the diff with whatever full-text edits you made. Without this, editing while Diff only was active would have truncated the source files to just the visible (changed) rows.

### Fixed
- 🌙 **Dark-theme Compare toolbar checkboxes.** "Ignore spaces" / "Ignore case" / "Ignore empty lines" / "Diff only" labels were rendering in the default Qt palette colour (black) on the dark toolbar — invisible. Now uses `comparePalette().headerFg` (the same theme-aware foreground colour the editor headers use) and re-applies on theme switch.
- 📏 **Compare toolbar buttons no longer truncate.** Recompare / Unlock Editing / Lock Editing / Close all switched from `setFixedSize` to `setMinimumSize` so the buttons grow to fit their text on platforms where the system font is wider than the previous fixed widths assumed (Windows + Linux were truncating "Recompare" and "Unlock Editing"). Added 12 px padding for visual breathing room.
- 🖱 **AI Assistant dock manual-resize on Windows / macOS.** Removed the `setMaximumWidth(640)` cap on the AI dock host widget. The QSplitter parent still enforces a sane minimum on the editor pane so dragging too far is naturally bounded; users on wide screens can now widen the AI chat past 640 px (which presented as "manual resize doesn't work" on Windows because the splitter handle would refuse to widen the dock further).

### Added
- 🔤 **Broader modern monospace-font default chain** in `src/fonts.h`. The picker now walks: Geist Mono → Berkeley Mono → MonoLisa → Commit Mono → JetBrains Mono → Cascadia Code → IBM Plex Mono → Monaspace Neon → Fira Code → SF Mono → Menlo → Consolas → DejaVu Sans Mono → Liberation Mono → Noto Sans Mono → Cousine — uses the first one installed on the user's system. Override via `Config::fontFamily` in `~/.config/notepatra/config.json` (UI font picker arrives in v0.1.43).

### Tests
- `test_compare_widget` updated for the new Diff-only-default behaviour. Test 4 (changed-row markers) now ticks Diff only OFF first since it asserts on a specific editor line number in the full-files view. Test 7 reworded for the new default + verifies edit-toggle auto-disables Diff only correctly.
- 17/17 ctest suites green on Linux baseline.

### Files changed
```
MODIFIED:
  src/compare.cpp        — Diff only default ON + prominent styling;
                            auto-disable on edit unlock; theme-aware
                            checkbox label colours; buttons grow to fit
  src/compare.h          — (no change vs v0.1.41 — m_diffOnly already
                            declared)
  src/fonts.h            — broadened monospace font default chain
  src/mainwindow.cpp     — removed AI dock setMaximumWidth(640)
  test_compare_widget.cpp— Test 4 + Test 7 updated for new default
  CMakeLists.txt         — VERSION 0.1.41 → 0.1.42
  CHANGELOG.md           — [0.1.42] entry
  README.md              — v0.1.42 row in releases table
  docs/index.html        — v0.1.42 LATEST card; v0.1.41 demoted
```

---

## [0.1.42] — 2026-04-30

The "make every option actually work" release. Audit revealed the Preferences dialog's General / Editing / Margins / Tab Settings / Auto-Completion / New Document tabs were entirely **stub UI** — checkboxes / radios / spinboxes constructed with hardcoded values, never read from `Config`, never written back. There wasn't even an OK/Apply button. v0.1.42 fixes every dead control across the entire app.

### Fixed — every Preferences-dialog control now reads + writes Config (16 stubs)

**General tab**
- "Hide toolbar" → wired to new `Config::hideToolbar`; toggles the actual feature toolbar.
- "Double-click to close tab" → new `Config::doubleClickToCloseTab`.
- "Show close button on each tab" → wired to `Config::tabsClosable`; calls `m_tabs->setTabsClosable()`.
- "Show Welcome tab on startup" → was already in Config but no UI; now exposed.

**Editing tab**
- New **Font picker** (`QFontComboBox` + size spinbox) — finally answers the "where do I change the font?" question. Defaults to monospaced fonts; toggle "Show all fonts" to expand.
- "Anti-aliased (smooth) font rendering" → new `Config::smoothFont`; sets `QFont::PreferAntialias` vs `NoAntialias`.
- Caret width spinbox → `Config::caretWidth` (was hardcoded `setCaretWidth(2)`).
- "Highlight current line" → `Config::highlightCurrentLine` (was hardcoded `setCaretLineVisible(true)`).
- "Word wrap" → `Config::wordWrap` (was loaded but never applied).
- "Auto-indent" → `Config::autoIndent` (was loaded but never applied).
- "Show vertical line at column" + column spinbox → `Config::showEdge` + `Config::edgeColumn` (loaded but `EdgeNone` was hardcoded).

**Margins tab**
- Fold style combo → new `Config::foldStyle` (BoxedTree / CircleTree / Plain / Boxed / Circle / None). Was hardcoded `BoxedTreeFoldStyle`.
- "Display line numbers" → `Config::showLineNumbers` (loaded but never applied).
- "Display bookmark margin" → new `Config::showBookmarkMargin`.
- "Display indent guides" → `Config::showIndentGuides` (loaded but never applied).
- "Display document rulers" → existing `Config::showDocumentRulers`, now exposed in Preferences.
- "Show crosshair overlay" → existing `Config::showCrosshair`, now exposed.

**Tab Settings tab**
- Tab size spinbox → `Config::tabWidth` (was hardcoded `setTabWidth(4)`).
- "Replace tabs with spaces" / "Use tab character" radios — now in a `QButtonGroup`, init from `Config::useTabs`, save on OK.

**Auto-Completion tab**
- "Enable auto-completion" + threshold spinbox → both wired to `Config::autoComplete` / `Config::autoCompleteThreshold` (the dialog never read or wrote them; the values shown were defaults frozen at compile time).

**New Document tab**
- EOL radios (Windows CR LF / Unix LF / Macintosh CR) — now in a `QButtonGroup`, persist new `Config::defaultEol`. Fresh editors apply the chosen EOL via `Editor::applyConfig()`.

**OK / Apply / Cancel buttons** — replaced the previous lone "Close" button (which only saved AI tab fields). Apply pushes Config + emits `settingsApplied()`; OK does the same and closes; Cancel discards.

### Fixed — Encoding menu now actually re-decodes / converts (5 broken items)

- New `Editor::reloadWithEncoding(name)` — re-reads file bytes from disk and decodes through the right `QTextCodec` (UTF-8, UTF-8 BOM, UTF-16 LE / BE, Windows-1252, ISO-8859-1). Pre-v0.1.42 the menu only flipped a label; the bytes were never re-decoded so files with mojibake stayed mojibake.
- New `Editor::convertEncoding(name)` — keeps the in-memory text but changes the save-encoding label so the next `saveFile()` writes bytes in the new encoding (with BOM if requested).
- `Editor::saveFile()` rewritten to actually honour `m_encoding` via `QTextCodec::fromUnicode()` (UTF-16 LE / BE / UTF-8 BOM / Windows-1252 / ISO-8859-1 all produce correct bytes). Pre-v0.1.42 every save wrote UTF-8 regardless of the encoding label.
- Encoding menu split into two submenus:
  - **Reinterpret bytes as** — re-decodes from disk (with confirmation if buffer dirty).
  - **Convert to** — keeps text, changes save format.

### Fixed — View menu checkmarks now sync to active editor (4 broken items)

- "Show All Characters" / "Show Whitespace and TAB" / "Show End of Line" / "Show Indent Guide" / "Word Wrap" — checkmarks now reflect the **actual** state of each option on the active editor. On tab switch, `syncViewMenuToActiveEditor()` pulls real state from each editor and updates the checkmarks. Pre-v0.1.42, the QActions auto-toggled but stuck across tab switches and drifted from reality.
- Toggle now propagates to **all open tabs** (not just the active one).
- "Show Indent Guide" + "Word Wrap" persist to Config and reload at next launch.

### Fixed — EOL conversion menu updates status bar (1 broken item)

- New `Editor::setEolModeByName(name, convert)` — sets `QsciScintilla::EolMode` AND `Editor::m_eolName` AND emits `eolModeChanged` signal. Edit → EOL Conversion → Windows / Unix / Mac now flips the status-bar EOL pill (was stuck pre-v0.1.42).

### Fixed — Settings → Tab Settings + Zoom shortcuts persist (4 holes)

- Settings → Tab Settings → Use Spaces / Use Tabs / Tab Width: 2/4/8 — now writes Config and propagates to every open editor via `applyConfigEverywhere()`. Pre-v0.1.42 changes affected only the active tab and reset on next launch.
- Ctrl+= / Ctrl+- / Ctrl+0 — zoom shortcuts now write `Config::fontSize` and re-apply across tabs. Pre-v0.1.42 they used QScintilla's per-editor `zoomIn()` which didn't persist.

### Added — `Editor::applyConfig()` single source of truth

Replaces hardcoded values in `setupEditor()` with one method that reads every relevant `Config` field and applies it. Called by:
- `Editor` constructor (via `setupEditor`)
- `MainWindow::applyConfigEverywhere()` after Preferences OK/Apply
- Zoom shortcuts
- Tab Settings menu
- Indirectly on app startup so Config from previous session takes effect

This is the wire that pre-v0.1.42 was missing — every Config field had loaders/savers but no consumer.

### Added — broader monospace font default chain (`src/fonts.h`)

The font picker now walks: **Geist Mono → Berkeley Mono → MonoLisa → Commit Mono → JetBrains Mono → Cascadia Code → IBM Plex Mono → Monaspace Neon → Fira Code → SF Mono → Menlo → Consolas → DejaVu Sans Mono → Liberation Mono → Noto Sans Mono → Cousine**. Picks the first one installed.

### Added — Compare polish (continuation of v0.1.41)

- **Diff only is now the default** in Compare. Most users want "show me what changed", not "show me the whole files". Untick the prominent green-bordered checkbox to see full files.
- **Edit-toggle auto-disables Diff only.** Clicking Unlock Editing in Compare now first unticks Diff only and recompares in full-files view (so editing has the full context). Re-locking re-runs the diff.
- **Dark-theme Compare toolbar fixes.** Checkbox labels were rendering in default Qt black on the dark toolbar — invisible. Now use `comparePalette().headerFg` (theme-aware).
- **Compare toolbar buttons** (Recompare / Unlock Editing / Lock Editing / Close) switched from `setFixedSize` to `setMinimumSize` so the text isn't truncated on platforms where the system font is wider than the previous fixed widths.
- **AI Assistant dock — free manual resize.** Removed `setMaximumWidth(640)` cap. Users on Windows / macOS were hitting that limit when dragging the splitter — presented as "manual resize doesn't work."

### Tests — `test_options_actually_work` (NEW, 49 assertions)

This is the test suite that would have caught the v0.1.41 stub-Preferences regression and will catch any future re-stubbing. Programmatically:

- Sets each Config field, calls `Editor::applyConfig()`, asserts the editor's actual state changed (caret width, line-highlight, wrap mode, tab width, indentation, edge mode, auto-completion threshold, default EOL).
- Constructs a `PreferencesDialog`, finds every checkbox / radio by visible label, toggles it, clicks OK, asserts `Config::<field>` flipped.
- Verifies Cancel does NOT save.
- Verifies `setEncoding`, `convertEncoding`, `setEolModeByName`, `zoomInPersistent` / `zoomOutPersistent` / `zoomResetPersistent` all behave correctly.

**18/18 ctest suites green** on Linux (was 17 — `test_options_actually_work` is new). All previously existing tests untouched and still passing.

### Files changed
```
NEW:
  test_options_actually_work.cpp — 49-assertion integration test

MODIFIED:
  src/config.h           — 7 new Config fields (hideToolbar, tabsClosable,
                           doubleClickToCloseTab, smoothFont, foldStyle,
                           showBookmarkMargin, defaultEol) + load/save
  src/editor.h / .cpp    — applyConfig(), setEncoding(), reloadWithEncoding(),
                           convertEncoding(), setEolModeByName(),
                           zoomInPersistent / zoomOutPersistent /
                           zoomResetPersistent; saveFile() rewritten to
                           use QTextCodec for non-UTF-8 encodings
  src/preferences.h/.cpp — full rewrite; 25+ controls now wired to Config
                           with OK/Apply/Cancel and settingsApplied() signal
  src/mainwindow.h /.cpp — applyConfigEverywhere(), syncViewMenuToActiveEditor();
                           Encoding menu re-decodes via QTextCodec; View
                           menu QActions persist + propagate to all tabs;
                           EOL conversion updates status bar; Tab Settings
                           menu writes Config; zoom shortcuts persist;
                           Preferences dialog constructor connects
                           settingsApplied → applyConfigEverywhere
  src/compare.cpp / .h   — Diff only default ON + prominent styling;
                           edit-toggle auto-disables Diff only;
                           theme-aware checkbox label colours; buttons
                           grow to fit (fixed → minimum sizes)
  src/fonts.h            — broadened monospace font default chain
  CMakeLists.txt         — VERSION 0.1.41 → 0.1.42; new test target
  test_compare_widget.cpp— Test 4 + Test 7 reworked for Diff-only default
  CHANGELOG.md           — [0.1.42] entry (this)
```

---

## [0.1.41] — 2026-04-30

The "Diff only" toggle release. User asked for a one-click way to hide matching lines in Compare so only the differences remain visible — the existing full-files view still works exactly the same; the new toggle just adds a filtered view alongside it.

### Added
- ✂️ **Diff-only toggle** in the Compare toolbar. Tick **Diff only** and Compare hides every `RowEqual` line, leaving only Added / Deleted / Changed rows. Original line numbers are preserved in the gutter so you still see exactly where each diff lives in the source files. Default OFF — full-files view remains the v0.1.40 default behaviour.

### Tests
- `test_compare_widget` — Test 7 (NEW, 4 assertions): default-OFF, full-view row counts, diff-only collapses RowEqual but preserves diff count, untoggling restores full view.
- 17/17 ctest suites green on Linux baseline.

### Files changed
```
MODIFIED:
  src/compare.h          — added m_diffOnly QCheckBox member
  src/compare.cpp        — toolbar wiring + recompare() filter
  test_compare_widget.cpp— +Test 7 (4 assertions)
  CMakeLists.txt         — VERSION 0.1.40 → 0.1.41
  CHANGELOG.md           — [0.1.41] entry
```

---

## [0.1.40] — 2026-04-29

The "stop screwing up my JSON" release. User reported v0.1.39's AI Assistant chat would *add fields and restructure* when asked to fix broken JSON — the regex JSON fixer (Tools → JSON Tools → AI Fix) had strict minimal-change rules, but the chat-mode "fix my json" path didn't. v0.1.40 closes that gap and bundles five other agent-loop robustness fixes that came up while reproducing the bug.

### Added
- 🩹 **AI-chat fix-intent detection.** Type `fix my json` / `repair this html` / `the sql is broken` etc. into the AI dock and the system prompt automatically swaps to a strict minimal-change patcher (same rules as Tools → JSON Tools → AI Fix). Models stop "improving" the input by adding fields, reordering keys, or restructuring. Does NOT trigger on `explain my json` / `what is json` / `show me json files`. New module `src/ai_intent.{h,cpp}`; new test `test_ai_intent` (49 assertions).
- 🔘 **Three new quick-action buttons** in the AI dock — **Fix JSON**, **Fix HTML**, **Fix SQL** — route directly to the strict-patcher prompt for the current selection / file, no need to type the trigger phrase.
- 💡 **Tip line** under the quick-action grid pointing users at Tools → JSON Tools (or HTML / SQL) for repeated / large fixes — those still have side-by-side diff + regex-first repair + AI fallback.

### Changed
- 🛠 **`apply_diff` three-tier match.** The agent's `apply_diff` tool used to require byte-exact `old_lines` against the file; if the model echoed back read_file's `      N\t` line-number prefix (very common on small models) every hunk failed with `error_kind: conflict`. v0.1.40 falls back through three tiers:
  1. Strict equality (existing behaviour, now the fast path).
  2. Strip the `^\s*\d+\t` line-number prefix from `old_lines`, retry.
  3. `.trimmed()` comparison on each line, retry.

  On tier 2 or 3 the call still applies the edit but emits `result.warnings: ["..."]` so the agent self-corrects on the next read. True conflicts (genuinely different content) are still refused — verified with a regression test.
- 📖 **`read_file` `with_line_numbers` parameter.** New optional bool, default `true` (full v0.1.39 back-compat). Pass `false` to receive raw file content with no `      N\t` prefix — recommended when feeding lines into `apply_diff old_lines`.
- 🧠 **Tool-mode system prompt** updated to teach the model the new param and to NEVER copy the `      N\t` prefix into `apply_diff old_lines`.
- 🧨 **Tool-call JSON parse-error surfacing.** When the model emits malformed JSON in tool-call `arguments`, both the Ollama `/api/chat` path and the OpenAI-compat SSE path now surface a structured `error_kind: malformed_args` tool result back to the model — instead of silently passing empty args downstream (which used to surface as confusing errors like `hunks array is empty`). The model now gets `Tool-call arguments JSON failed to parse: <error>. Re-emit the call with valid JSON.` plus a 240-char raw-args preview, so it can self-correct.

### Tests
- **17/17 ctest suites green** on Linux baseline (was 16 on v0.1.39 — adds `test_ai_intent`). New + extended assertions:
  - `test_ai_intent` — 49 assertions: positive json/html/sql intents (case-insensitive, mixed phrasing), negatives (explain/describe/teach/show/list/find/grep), edge cases (multi-line, @file mention, generic "fix my code"), strict-prompt sanity.
  - `test_ai_tools` extended — apply_diff three-tier match (prefix-stripped, whitespace-trimmed, strict-still-clean, true-conflict-still-refused), read_file with/without line-number prefix, schema advertises `with_line_numbers`. 157 total assertions.

### Files changed
```
NEW:
  src/ai_intent.{h,cpp}         — fix-intent classifier + strict-patcher prompts
  test_ai_intent.cpp            — 49 assertions

MODIFIED:
  src/ai_tools.cpp              — apply_diff 3-tier match + warnings;
                                  read_file with_line_numbers param + schema
  src/ai_systemprompt.cpp       — toolModeLayer documents the prefix issue
  src/ollama.cpp                — surface QJsonParseError on tool-call args
                                  (Ollama + OpenAI-compat paths)
  src/aipanel.cpp               — fix-intent path (custom action) + 3 new
                                  quick-action buttons + tip line +
                                  malformed_args short-circuit handler
  test_ai_tools.cpp             — +20 new assertions for the v0.1.40 paths
  CMakeLists.txt                — version 0.1.39 → 0.1.40; ai_intent.cpp
                                  in main target; new test_ai_intent target
```

### Why two patch releases this close together
v0.1.39 closed the file-WRITE gap (write_file / search / apply_diff). v0.1.40 closes the file-FIX gap — same theme, surfaced once people started actually using the agent on broken files. The big v0.2 mega-release continues independently on `v0.2-megafeatures`; v0.1.40's six fixes will be cherry-picked onto that branch so v0.2 ships with them.

---

## [0.1.39] — 2026-04-27

Coding Mode goes from "agent that can read your code" to "agent that can build with you." User reported they expected `"create me a Python file"` to actually create the .py file — that gap is now closed. Plus: persistent chat history per workspace, a finally-visible red close button on the AI dock.

### Added
- 🤖 **`write_file` tool.** The agent can now create or overwrite text files (modes: `overwrite` default, `create` fails-if-exists, `append` adds-to-end). Auto-creates parent directories inside the workspace, refuses to write outside it. 5 MB cap per call. New `error_kind: exists` for `mode=create` collisions.
- 🤖 **`search` tool.** Pattern search across the workspace — literal substring or regex, optional glob filter (e.g. `*.py`), optional case-sensitivity, capped at 200 matches per call. Uses `QDirIterator` with the same heavy-dir filter as the file explorer (skips `.git`, `node_modules`, `target`, `dist`, `.venv`, etc.).
- 🤖 **`apply_diff` tool.** Surgical line-level edits to existing files. Each hunk has `old_start_line` + `old_lines` (expected current text) + `new_lines` (replacement). **Atomic**: validates ALL hunks against the live file first; if any drifted, the call returns `error_kind: conflict` and **nothing** is written. Hunks are applied in reverse order so earlier line numbers stay stable.
- 📂 **Auto-open / silent reload.** New files written by the agent open in a tab automatically. Files already open get reloaded silently (no "modified by another program" dialog — the user just told the AI to do it).
- 💾 **Persistent chat history per workspace.** Conversations now survive app restart. Stored at `~/.config/notepatra/chat-history/<sha1-of-workspace>.json`, debounced 2 s, capped at 1 MB per workspace (oldest messages roll off the front when full). Each workspace has its own history file; switching workspaces swaps in the right history. `Reset` deletes the on-disk file.
- 🎨 **Red close button on the AI dock** (theme-independent). Pre-v0.1.39, `SP_TitleBarCloseButton` over `pal.chromeBg` was tone-on-tone — invisible at rest on every theme. Now uses U+00D7 MULTIPLICATION SIGN (×) — present in every font on every desktop OS, no tofu risk — styled with the Windows-canonical close-button red `#E81123` at rest, red bg + white X on hover.

### Improved
- 🛡 **`resolveSafeWritePath` (defense in depth).** Lifted out of `resolveSafePath` for write-side tools whose target may not exist yet. The PARENT dir's lowest existing ancestor must canonicalize inside the workspace; only THEN do we `mkpath` new subdirs. Hardcoded deny-list still applies to the final candidate path so the agent can't create `~/.ssh/foo` even when the parent is in scope.
- 🧠 **System prompt now mentions all 5 tools.** The Coding-Mode preamble (`toolModeLayer()`) was listing only `read_file` + `list_dir`. Updated to spell out the read+write tool surface and when to prefer `write_file` vs `apply_diff`.
- 🧪 **Tests: 80 → 134.** New assertions for every `write_file` mode (overwrite/create/append), `search` (literal/regex/glob/case-sensitivity), `apply_diff` (single-hunk, multi-hunk, out-of-order hunks, conflict-aborts-atomically, deny-listed paths, line-beyond-EOF, missing file). Plus negative coverage: unknown tool name, unknown write mode, empty pattern, traversal attempts.
- 🧰 **`apply_diff` atomic write fixed.** The first implementation used `QFile::rename` for the temp→target swap, which silently fails when target exists on Linux. Now uses `std::rename` directly (POSIX atomic) on Linux/macOS; remove-then-rename on Windows.

### Files changed
```
src/ai_tools.{h,cpp}     — write_file, search, apply_diff + resolveSafeWritePath
src/aipanel.{h,cpp}      — fileWrittenByAgent signal; tool-card result summaries;
                           persistent chat history (load/save/clear); red close button
src/mainwindow.cpp       — connect fileWrittenByAgent → openFile/silent-reload
src/ai_systemprompt.cpp  — toolModeLayer mentions all 5 tools
test_ai_tools.cpp        — +54 new assertions (134 total)
CMakeLists.txt           — 0.1.38 → 0.1.39
release_notes/v0.1.39.md — full notes
README.md, docs/index.html — version refs + new release-card
```

### Notes
- 134 / 134 tool tests pass. All 16 regression suites green.
- Path safety unchanged in spirit, extended for writes: workspace anchor + canonicalize + hardcoded deny-list still hold.

---

## [0.1.38] — 2026-04-26

Two AI Assistant bugs reported by user — both root-caused and fixed.

### Fixed
- 🐛 **Coding Mode toggle crashed the app mid-stream.** Clicking the Coding Mode checkbox while a model was actively streaming a response caused a use-after-free crash. Root cause: the toggle handler calls `renderTranscript()` which calls `aiClearChat()` — that `deleteLater()`s every widget in `m_chatLayout` including `m_streamingCard` and its child `m_streamingStats` QLabel. But the 250 ms streaming-stats timer kept ticking on the dangling `m_streamingStats` pointer; the existing `if (!m_streamingStats) return` guard didn't catch a dangling-but-non-null pointer; the next `setText()` hit freed memory → crash. **Fix**: `renderTranscript()` now stops the timer + nullifies `m_streamingStats` (and resets `m_streamingTokenCount` / `m_streamingStartMs`) BEFORE calling `aiClearChat()`. Same protection in `endAssistantBubble`.
- 🐛 **Custom chat appended the entire open file to every prompt.** When the user typed a casual message in the AI chat box (Coding Mode OFF, no selection) — even something as small as "hi" — the AI got the **whole current file's contents** appended to the prompt. Root cause: `setWorkspaceContext()` set `m_context = selectedText.isEmpty() ? currentFileText : selectedText`, so when there was no selection `m_context` fell back to the whole file. The "custom" action's code at `aipanel.cpp:1565` then unconditionally appended `m_context` to the prompt. **Fix**: new `m_contextIsSelection` flag tracks whether `m_context` is a real user selection or a whole-file fallback. The "custom" action now only inlines `m_context` when `m_contextIsSelection == true`. Quick-action templates (Explain / Refactor / Write Tests / etc.) still inline because they need code to act on. Project-level questions about the file still benefit from the workspace-context block via `shouldAttachWorkspace`, or from Coding Mode's `read_file` tool for explicit file access.

### Notes
- 16 / 16 regression tests pass.
- Both fixes are minimal and surgical — 1 file changed for crash fix (`aipanel.cpp`), 2 files for the file-leak fix (`aipanel.h` + `aipanel.cpp`).

---

## [0.1.37] — 2026-04-26

Comprehensive lexer palette coverage. Every supported language's every QScintilla style now gets a recognisable colour on every theme. **1650 styles × 3 themes = 0 gaps.**

### Fixed
- 🎨 **359 lexer style gaps closed.** Pre-v0.1.37 audit (new `test_lexer_coverage`) found 359 styles across 28 lexers that fell through to default text colour on Light / Dark / Monokai. Examples: Markdown list items / block quotes / strikethrough / horizontal rules went unstyled; YAML "Identifier" (which is the KEY) painted as plain text; Properties / TOML sections + keys unstyled; Diff +/-/changed lines unstyled; CSS `@media` / `!important` / `#id` unstyled; Bash heredoc / `${var}` parameter expansion unstyled; Perl scalar / POD unstyled; HTML SGML / CDATA / fragment markers unstyled; C++ "Escape sequence" / "Task marker" (TODO) / "IDL UUID" unstyled.
- 🎨 **Monokai operator gap.** `npOperator` was set to `#F8F8F2` on Monokai, identical to the default text colour — operators rendered invisible. Now `#FD971F` (Monokai amber) so `+ - = ( ) { } ;` actually pop.

### Added
- 🎨 **~30 new style-kind matchers** in `npp_palette.cpp` chain (before the catch-all identifier fallback): escape sequences, task markers (TODO/FIXME), IDL UUIDs, here-documents, scalars, POD, labels, sections, key/value, references/anchors, document delimiters, list items, block quotes, strikeouts, horizontal rules, special chars, SGML/CDATA, HTML fragments, entities, CSS @-rules, diff +/- lines, position markers, stdout/stdin, quoted identifiers, SQL*Plus prompts, control-flow blocks (`while`/`if`/`foreach`/`for`/`MACRO`), parameter expansion (`${var}`), module names, media rules, JSON-LD IRIs, inline asm, `!important`, ID selectors, hash colours, external commands, Lua coroutines/IO subsets, code blocks, arrays.
- 🎨 **YAML brand override** — keys (style 2 "Identifier") now paint blue/JSON-blue, references/anchors get cyan, document delimiters bold. Previously YAML's keys painted as default text; now properly highlighted.
- 🎨 **Properties/TOML/INI/.env brand override** — sections, keys, and values get distinct brand colours (was generic blue/violet, fell through for TOML-specific kinds).
- 🎨 **Diff brand override** — keyword harmonised; the new "removed"/"added"/"changed"/"position" matchers handle the per-line colour.
- ✅ **`test_lexer_coverage` test (NEW).** Walks every QsciLexer subclass + all 6 Notepatra-local lexers (28 total), applies palette in Light / Dark / Monokai, and asserts no non-Default style falls through to default text. **1650 styles × 3 themes verified.** If anyone refactors the matcher chain or adds a lexer that introduces unthemed styles, CI fails before users see them.

### Refactored
- 🎨 **YAML's "Identifier" style** (which IS the key in YAML, not a generic identifier) special-cased in the identifier matcher to route to `npKeyword2`. Other languages (C/C++, Java, Python, Rust, Go, Swift, Kotlin, TypeScript) keep `npText` for "Identifier" per the v0.1.31 "all blue shades" fix.

### Notes
- 16 / 16 regression tests pass (was 15; +`test_lexer_coverage`).
- Total assertions across the suite now ≈ 1900 (1650 in coverage + 60 palette + 68 ai_tools + 31 projectsearch + ~90 others).
- No new dependencies, no schema changes.
- The lexer-coverage probe runs at every CI build now; any language that gains new styles in a future QScintilla update will be auto-flagged.

---

## [0.1.36] — 2026-04-26

Two small UX wins driven by user feedback: Project Search makes multi-word phrases obvious + safe, and Compare's "Ignore spaces" defaults to ON.

### Changed
- 🔍 **Project Search placeholder text** updated to make multi-word phrase support discoverable. Was *"Search for a string, word, or regex pattern…"* (sounds single-token). Now *"Search any text — words, phrases like \"import os\", or regex patterns…"*. Multi-word literals like `import os` (with space) already worked via `QString::indexOf` substring matching + the rust aho-corasick fast path; the change is purely about discoverability.
- 🔍 **Project Search query trim** — leading/trailing whitespace stripped at `startSearch()` so `" import os "` matches the same lines as `"import os"`. Internal whitespace preserved (multi-word phrase support intact). Trims for regex mode too — leading whitespace in regexes is rarely intentional and explicit `\s+` users are unaffected.
- 🔀 **Compare: "Ignore spaces" defaults to ON.** The most common compare-tab use case is *"did this code change?"* where reformatting / re-indentation shouldn't show up as diffs. Users who want byte-exact compares (whitespace-as-meaning, e.g. YAML / Python indentation diffs) just untick the checkbox.

### Added
- ✅ **Two new test cases in `test_projectsearch.cpp`** locking in the multi-word behaviour:
  - **Case 7** verifies `"import os"` returns exactly 5 matches (across two fixture files), excludes `"import sys"` and `"from os import path"` (where the words are present but not contiguous), and narrows the result set vs the single-word `"import"` baseline. Future refactors that accidentally tokenize the query on whitespace will fail CI.
  - **Case 8** verifies the trim path: `" import os "` (with surrounding whitespace) produces identical matches to the bare phrase. test_projectsearch went from 26 → 31 assertions.
- ✅ **`test_compare_widget`'s Test 1 updated** for the new default. First confirms `Ignore spaces` defaults to ON, then unchecks it (in byte-exact mode whitespace IS a diff), then re-checks to verify the diff disappears. Same coverage as before, inverted setup order.

### Notes
- 15 / 15 regression tests pass. All assertions updated to match the new defaults.
- No backend or wire-format changes; pure UI/UX improvement on top of v0.1.35.
- No new dependencies, no schema changes, no installer changes.

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
- 🛡 **Tool-call budget** — 25 hard cap per user turn (prevents runaway loops). When exhausted the model gets a structured error telling it to summarise and stop, preventing runaway loops.
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
- 🔎 **Project Search page-level scroll** — whole tab lives in a `QScrollArea`, match tree grows to fit content, page scrollbar takes over. One scroll, not two. Matches VS Code / Sublime.
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

AI Assistant becomes a proper side-dock · Project Search finally lives up to "Rust-powered" with a 10–50× speedup · terminal runs `claude` / `codex` / REPLs inline via PTY · CI back to green.

### Added
- 🤖 **AI Assistant lives in a right-side dock** (`Ctrl+Shift+A`) — persistent chat that survives tab switches. No more spawning a new editor tab per session; one conversation, always in the same place.
- 🤖 **Whole-workspace awareness** — every prompt carries the selection (or full file), every other open editor tab, the workspace root, AND a flat listing of every file under it. The AI can reason about files you haven't opened, modern AI-assistant style. Budget-capped so small 3B models don't overflow. Skips `.git` / `node_modules` / `target` / `dist` / `__pycache__`.
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
- 🤖 **AI Coding Mode toggle** — ON = system prompt becomes "return ONLY modified code, no prose, no fences, preserve indentation"; Replace Selection drops clean code straight into the editor (modern AI-assistant style).
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
