# Notepatra regression log

Every regression we've caught and protected against, indexed by the
version where it landed.  The intent: future-me / future contributors
can run `ctest` and know **exactly** what each test guards against.

If any of these tests start failing, the corresponding regression is
back.  Don't `xfail` them — fix the underlying bug.

Maintained alongside the test suite — when adding a new regression
test, append a row here in the same commit.  The release checklist
(`scripts/release-check.sh`) does **not** auto-link to this file, so
the discipline is on us to keep it current.

---

## v0.1.76 — Expanded chart-type catalogue

### Every type in the dispatcher renders

- **Test**: `test_chart_types.cpp`
- **Guards against**:
  - A new chart type being added to `looksLikeChartSpec()` (the gatekeeper) but missing its dispatcher branch in `renderFromObject()`, or vice versa.
  - A render helper silently returning `nullptr` for a valid spec (legacy types like `line` / `bar` / `pie` / `scatter` would otherwise drift unnoticed).
  - A future Qt5 / Qt6 transition dropping a `QtCharts` series we depend on (`QAreaSeries`, `QHorizontalBarSeries`, `QStackedBarSeries`, `QHorizontalStackedBarSeries`, `QBoxPlotSeries`).
- **Coverage**: 26 sub-checks. (a) Every legacy type (line / bar / pie / scatter) still renders a non-null `QWidget`. (b) Every new v0.1.76 type (area, horizontal-bar, stacked-bar, stacked-horizontal-bar, grouped-bar, donut, histogram, boxplot) renders a non-null `QWidget` from a realistic spec. (c) `looksLikeChartSpec()` accepts every type in the catalogue. (d) An unsupported type returns `nullptr` + a descriptive error string (not a crash).
- **When adding a new chart type**: append the type to `looksLikeChartSpec()`'s `kTypes` list, add a `renderXxx()` helper, wire it in `renderFromObject()`, and add a test case here. The test fails if any of these steps is skipped.

---

## v0.1.75 — Runtime font-pack downloader

### Manifest validity + scan-and-load

- **Test**: `test_fontpack.cpp`
- **Guards against**:
  - A font entry losing its required fields (family / fileName / url) → install dialog would render blank rows or write a 0-byte file.
  - A duplicate `fileName` in the manifest → the second entry overwrites the first on disk, breaking the dedupe key.
  - A non-HTTPS URL sneaking in → an attacker on the network could swap in arbitrary bytes mid-stream and Notepatra would happily register the resulting "font" with `QFontDatabase`.
  - `loadInstalledFonts()` failing to register an on-disk TTF → the user installs a font but it never appears as a usable family.
- **Coverage**: 23 sub-checks across (a) manifest sanity (size bounds, no dup filenames, all-HTTPS URLs, every entry has license + origin, all four categories present, six industry-standard families by name), (b) path helpers (`fontsDir()`, `localPath()` composition), (c) a real-world load test that copies `/usr/share/fonts/truetype/ubuntu/UbuntuMono-RI.ttf` into `fontsDir()` and asserts `loadInstalledFonts()` returns ≥1.
- **When adding a new font to the manifest**: just append the row in `src/fontpack.cpp`; the test will fail if you accidentally collide a filename or skip a required field.

---

## v0.1.74 — `notepatra-local-ai` MSI upgrade

### Diagnostic — Component GUIDs no longer shared across flavors

- **Test**: none ✗ — requires a Windows VM with both regular and local-ai MSIs pre-installed to validate. Diagnosed by `strings -e s notepatra-*-0.1.7?.msi` showing identical `A2F3B641` / `C3D4E5F6` / `D4E5F6A7` hardcoded Component GUIDs across all flavors; fix switches them to `Guid="*"` so WiX auto-generates a deterministic GUID per flavor (different `INSTALLFOLDER` → different KeyPath → different GUID).
- **Validation**: post-release manual install/upgrade on Windows by anyone hitting the bug. If the upgrade dialog drops into Repair/Remove again on a v0.1.74→v0.1.75 local-ai upgrade, the fix isn't sufficient and we'd look at KeyPath / Feature membership differences next.

---

## v0.1.73 — AI dock blank-on-reopen

### S16: hide-show cycle restores dock width

- **Test**: `test_ai_fullscreen_exit.cpp` scenario 16
- **Guards against**: After `setAiDockVisible(false)` then
  `setAiDockVisible(true)`, the AI dock's splitter slot was at 0 px
  → user saw a blank, zero-width dock.
- **Root cause** (fixed in v0.1.73): `rebalanceAiDockSplit()` bailed
  unconditionally on `m_aiDockSizedOnce` so the 60/40 split was never
  re-applied after a full hide → show cycle.
- **Assertion**: the dock's `parentWidget()->width()` is `> 50 px`
  after the round-trip.

### S17: red ✕ close → toolbar re-open works

- **Test**: `test_ai_fullscreen_exit.cpp` scenario 17
- **Guards against**: Closing the AI dock via the red ✕ button in the
  panel header and then re-opening via the toolbar AI button left the
  dock at 0 px wide AND `Config::aiDockVisible` out of sync (stayed
  `true` while the dock was visually hidden).
- **Root cause** (fixed in v0.1.73): the ✕ button's click handler
  called `parentWidget()->setVisible(false)` directly, bypassing
  `setAiDockVisible(false)`'s Config persistence + splitter
  bookkeeping.  Fixed via a new `closeDockRequested` signal that
  MainWindow hooks into the canonical hide handler.
- **Assertions**: dock hidden after ✕, `Config::aiDockVisible == false`
  after ✕, dock visible + width > 50 px after toolbar re-open.

---

## v0.1.72 — Network policy for cloud-free build

### 49-case network allowlist coverage

- **Test**: `test_network_policy.cpp`
- **Guards against**: cloud-free build (`-DNOTEPATRA_NO_CLOUD=ON`)
  accidentally permitting a public LLM endpoint, or accidentally
  refusing a legitimate local one.
- **What's covered**:
  - Loopback: `localhost`, `127.0.0.1`, `::1`, `[::1]`
  - RFC1918: `10.0.0.0/8`, `172.16.0.0/12`, `192.168.0.0/16`
  - CGNAT: `100.64.0.0/10` (Tailscale, corp VPN)
  - IPv6 ULA: `fc00::/7`
  - Link-local: `169.254.0.0/16`
  - DNS suffixes: `.local`, `.lan`, `.internal`, `.intranet`, `.corp`, `.home`
  - File / unix schemes: `file://`, `unix://`, `qrc://`, scheme-less
  - **Refused**: api.openai.com, api.anthropic.com, api.mistral.ai,
    generativelanguage.googleapis.com, openrouter.ai, api.groq.com,
    api.together.xyz, api.deepseek.com, api.cohere.com, api.x.ai,
    api.perplexity.ai, api.fireworks.ai, 8.8.8.8, 1.1.1.1,
    example.com.
  - Adversarial: `api.openai.com.evil.com`, `localhost.attacker.com`,
    `my-local` (no dot), `172.15.x.y` (outside RFC1918),
    `172.32.x.y` (outside RFC1918), `100.63.x.y` / `100.128.x.y`
    (outside CGNAT).
- **When adding a new cloud LLM provider**: add its API hostname to
  the "PUBLIC LLM endpoints — MUST be refused" block in
  `test_network_policy.cpp`.

---

## v0.1.71 — AI Interaction Log

### Credential scrubber covers all six free-text fields

- **Test**: `test_credscrub.cpp`
- **Guards against**: a credential-shaped string (Bearer token,
  OpenAI `sk-…`, Anthropic `sk-ant-…`, GitHub PAT, AWS access key,
  Google API key, PEM block) being written to the AI interaction log
  in clear text.  Originally only `content`, `toolArgs`, `toolResult`
  were scrubbed — the `error`, `toolName`, `model` fields could
  leak credentials embedded in HTTP error messages.  Caught during
  v0.1.71's pre-release smoke test.
- **Coverage**: every regex pattern + every recorded field.
- **When adding a new credential pattern**: add a test case here
  AND a regex in `src/credscrub.cpp`.

---

## v0.1.68–v0.1.70 — AI dock visibility chaos

### Three independent paths exit AI fullscreen

- **Tests**: `test_ai_fullscreen_exit.cpp` scenarios 1–15
- **Guards against**: the v0.1.67–v0.1.70 sprint's AI-dock-fullscreen
  mode getting stuck across:
  - tool-tab opens (Project Search, Terminal, REST, JSON Tools, …)
  - editor-tab switches (Ctrl+Tab, double-click search result, File→Open)
  - mode switches (Chat ↔ Coding ↔ Data)
  - Ctrl+N background tab creation
  - per-mode chat history isolation
  - sub-mode restoration on dock re-open (v0.1.70 deliberate
    "restore last sub-mode" rule — S14)
  - Ctrl+Q shortcut mapping to AI toggle (S15)

### Per-mode chat conversations stay isolated

- **Test**: `test_ai_chat_history.cpp`
- **Guards against**: shared `m_messages` vector across Chat / Coding
  / Data modes (the v0.1.67 fix replaced it with three vectors).

---

## v0.1.55 — DuckDB Data Mode

### DuckDB SQL parse round-trip + connection layer

- **Tests**: `test_duckdb.cpp`, `test_ai_dataanalyst.cpp`
- **Guards against**: DuckDB extension/connection regressions when
  the vendored libduckdb.so version is bumped.

---

## v0.1.43–v0.1.50 — Lexer + Format engines

### 92 language lexers + 226 file extensions

- **Tests**: `test_lexers.cpp`, `test_lexers_v0125.cpp`,
  `test_lexer_smoke.cpp`, `test_lexer_coverage.cpp`
- **Guards against**: a lexer accidentally being removed from the
  build, an extension routing to the wrong lexer, or a new lexer
  not being wired into the file-extension table.

### SQL formatter (5 dialects)

- **Test**: `test_sqlfmt.cpp`
- **Guards against**: sqlfmt's dialect dispatch (PostgreSQL / MySQL /
  T-SQL / SQLite / Snowflake) breaking on edge cases.

---

## How to add a new regression scenario

1. Add the test case to the appropriate `test_*.cpp` file (or create
   a new one — the CMake glob in the `notepatra_all_tests` meta-target
   auto-discovers any `test_*.cpp` at the repo root).
2. Confirm it FAILS on the broken code and PASSES on the fix.  Commit
   the fix and the test in the same commit so reviewers can verify
   the test actually exercises the bug.
3. Append an entry to this file under the current version section.
   Include: **Test** (file + scenario number), **Guards against**
   (the user-observable symptom), **Root cause**, **Assertions**.
4. CI runs the new test on every commit automatically — no build.yml
   changes needed if the file is at the repo root.

`scripts/release-check.sh` runs the full `ctest` suite as part of
preflight, so any of these tests failing blocks the next release tag.
