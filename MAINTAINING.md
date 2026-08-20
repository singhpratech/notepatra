# MAINTAINING.md — Notepatra Maintainer Handbook

Written 2026-07-05 @ v0.1.114 (main @ fe35cf7). This is the institutional-knowledge document for any maintainer (human or AI) picking up this repo cold. `AGENTS.md` covers build/style basics; this file covers everything that will actually bite you. Read both before your first change.

---

## 1. What this is

**Notepatra** — a native Qt5/QScintilla C++17 text editor for Linux/Windows/macOS (Notepad++-class), with a Rust static-library core (`rust-core/`, FFI: file I/O, search, Myers diff, formatting) and a local-AI assistant (Ollama-first; Chat/Compose/Agent coding modes + a Data-analyst mode on DuckDB + Vega charts + a diagram DSL + Noter notes system).

- **Flavors:** Lite (default, small) and Full (DuckDB, optionally WebEngine). SSOT for the name: `src/build_flavor.h` (`NOTEPATRA_FLAVOR_NAME`). Detect "Full" via `NOTEPATRA_HAVE_DUCKDB` (|| `WITH_WEBENGINE`) — **never** WebEngine alone (macOS Full has DuckDB but no WebEngine). Keep `setApplicationName("Notepatra")` untouched (config paths depend on it); flavor goes on `setApplicationDisplayName`.
- **Standing product rule — bare binary stays small.** Heavy features (WebEngine, Poppler, embedding models, LSP servers) ship as optional plugins or runtime downloads, never bundled into Lite. DuckDB-extension pattern is the model. A stub fallback is fine if the upgrade prompt is one click.
- **Binary is intentionally NOT stripped** (decided 2026-05-16): DWARF kept so user crash reports symbolicate. Don't "optimize" this away.
- Website = `docs/` (GitHub Pages). Installers/packaging = `installers/`, `scripts/`.

## 2. Layout

| Path | What |
|---|---|
| `src/` | Qt app, paired `.h`/`.cpp`. Big ones: `mainwindow.cpp`, `editor.cpp`, `aipanel.cpp` (~7.7k lines), `ai_tools.cpp` |
| `src/ai_*` , `edit_plan.*`, `agent_repeat_guard.h` | AI assistant: context SSOT, tools, system prompts, edit-plan review UI, write-approval gate |
| `csvanalyst.*`, `duckdb_client.*`, `dbconnections.*`, `dbtree.*`, `chart_*`, `src/charts/` | Data-analyst stack |
| `src/diagram/` | Diagram DSL (NPD) |
| `rust-core/` | Rust 2021 static lib via FFI (diff via `similar` crate, search, file I/O) |
| `test_*.cpp` (repo root) | Regression tests — must be registered in `CMakeLists.txt` to count (see §4 phantom-test warning) |
| `eval/` | Model eval harnesses (coding-model + data-analyst). **Git-excluded via `.git/info/exclude`** — exists on one machine only |
| `docs/` | Website + install scripts (`install.sh`, `install.ps1`) |
| `scripts/` | Release gates (see §5) |
| `planning/` | `next_phase_plan.md` incl. the 20-rule anti-digression playbook |

## 3. Build & test

```bash
./build.sh                 # normal local build
./build.sh --tests         # full regression suite
# manual: cargo build --release in rust-core/, then cmake with -DBUILD_TESTING=ON,
# target notepatra_all_tests, then QT_QPA_PLATFORM=offscreen ctest --output-on-failure
```

- `notepatra_all_tests` is the meta-target that builds every test binary — use it; building tests one-by-one is how releases broke pre-v0.1.57.
- ctest has a **global 600s TIMEOUT net** — `enable_testing()` alone gives NO default timeout; a hung test = a CI job stuck for hours. Keep the net when adding tests; a "stuck 3 hours" CI job means a hung test, and the orphan-kill log line names it.
- Local Rust toolchain lags CI: run `rustup update` before pushing `rust-core/` changes — CI clippy runs latest stable with `-D warnings` and new lints will red the build.

## 4. Testing discipline (hard-won rules)

1. **Full suite before every release, no exceptions.** Build ALL targets + full ctest + `stale-text-check.sh` + `release-check.sh` + offscreen smoke — not just the test for what you changed. Reason about blast radius first. (User hard rule.)
2. **Unit tests are not UI verification.** For ANY signal/slot/state-machine/dock/menu change, run the actual binary or write an integration test that drives the changed path. Never tell the user "reopen and try X" — QA is the maintainer's job. (Burned v0.1.67/v0.1.68.)
3. **Red-state verify regression tests:** revert just the fix and require a FAST clean FAIL (a hang is not a red). Give tests answering unexpected message boxes a clicker helper; set a ctest TIMEOUT.
4. **Phantom-test warning (found 2026-07-05):** a `test_*.cpp` on disk proves nothing. `test_db_readonly_gate.cpp` sat at repo root, git-excluded, in no CMake target, calling a function that doesn't exist — while its comments described the bypasses it "fixed". Before trusting any test: `ctest -N | grep <name>`, confirm the function it calls exists, then red-state verify. Also `cat .git/info/exclude` when auditing — locally hidden files are a review blind spot.
5. **Offscreen platform traps:**
   - **Windows offscreen: ANY QMessageBox (even non-modal `show()`) SIGSEGVs.** Guard test sections that *trigger* app notice boxes; `setvbuf` unbuffered stdout so you can see where it died.
   - **macOS offscreen: file-watcher prompts wedge modal drives** — skip watcher-modal sections there.
   - Gate every `exec()` in a window with the modal-gate pattern (`qScopeGuard`); clear the gate AFTER post-modal mutations; re-resolve indices after nested event loops.
6. **AI-backend tests must be offline-tolerant:** CI has no Ollama/cloud. Assert the UI contract (accept the "(no models)" placeholder), never backend results. (Cost v0.1.97 its first build.)
7. **Adversarially re-verify FIXES, not just original code.** A fix can introduce a worse bug (v0.1.111: a render guard briefly killed the entire Agent write-gate). For async/UI-teardown fixes, trace the full end-to-end path and enumerate every caller of the touched hot-path function. Run a fan-out adversarial review over any parser/grammar/DSL change before shipping — unit tests + eyeballing miss regressions (proved on v0.1.104 NPD).
8. **Perf fixes get a `QElapsedTimer` ceiling test** — correctness tests pass both the broken and fixed versions.

## 5. Release process

Flow (the `np-release` skill orchestrates this): bump `CMakeLists.txt` VERSION **and `notepatra-mcp/Cargo.toml`** → `release_notes/vX.md` → append `CHANGELOG.md` → `scripts/release-check.sh` → PII sweep → commit → tag → push → watch CI → verify GitHub Release assets + Pages → **`cd notepatra-mcp && cargo publish`**.

Gates in `scripts/` (release-check.sh calls most): `stale-text-check.sh` (canonical counts, capability claims, installer filenames), `verify-download-sizes.sh` (published bytes vs docs claims), `test-install-selection.sh` (lite/full artifact pick), `post-release-verify.sh`, `bump_version.sh`, `smoke-multiprocess.sh`.

**Meta-rule: memory must be a gate, not a post-it.** Any "remember to check X every release" where X is mechanical (grep/count/hash/version match) gets promoted into `release-check.sh`/CI immediately. Several past releases shipped stale claims while the reminder existed only as a note.

Per-release non-gated work that still needs doing:
- **Version sweep:** grep `docs/` + `README.md` for stale `v0.1.X` strings (JSON-LD `softwareVersion`, "as of v0.1.X", aspirational claims). Case trap: "As of" ≠ "as of".
- **Factual audit:** download sizes, test counts, architecture claims, cross-page disagreement, example model names. Spawn a checker agent; fix in one commit.
- **Website policy:** `docs/index.html` keeps FULL details only for the latest release card; older releases collapse to one-line link rows.
- **Don't churn-ship:** one-click-bypassable friction is not a same-day patch trigger. Same-day patches are for hard crashes, broken features, security regressions, wrong artifacts only.

CI traps:
- **Pin `build-windows` to `windows-2025`.** `windows-latest`→VS2026 dropped `stdext::make_checked_array_iterator`; every Qt 5.15.2 QList TU fails and the release job silently *skips* → zero artifacts. After any tag push confirm the release job = success, not skipped.
- **macOS verifies the .app bundle binary** (`Notepatra.app/Contents/MacOS/Notepatra`), not `./notepatra`; rewrite the MAIN binary's libduckdb ref to `@rpath` before final codesign.
- **GitHub push protection** rejects anything key-shaped (`sk-…`, `ghp_…`) even all-zero fakes — build key-shaped test literals from concatenated fragments. Recovery: `reset --soft`, fix, re-commit, move tag.
- `Closes #N` in a direct push does NOT close issues — only PR merges do; follow up with `gh issue close N`.
- Install scripts **hard-fail** verification when the SHA/sig source is unreachable (Tier 1). Every new security check gets classified into exactly one tier first: hard-fail / soft-warn / skip-when-tool-missing.
- **crates.io is a distribution surface and nobody was watching it.** `notepatra-mcp` 0.1.118/0.1.119 were published by hand on 2026-07-18, then the channel rotted for eight releases — `cargo install notepatra-mcp` served 0.1.119, the one release whose Windows named-pipe transport deadlocked on every verb and had never completed a tool call. `release-check.sh` now hard-fails when crates.io's `max_version` is not the previous tag (soft-warn when the registry is unreachable). Publishing is still manual and happens AFTER the tag; the gate catches the omission on the next release, not the current one.

**User-pause rule:** "do not CI until I say" / "stop shipping" / "wait" = no push, no tag, no commit-to-main. Work locally, batch fixes, resume only on an explicit "ship it". Status questions do not unpause.

**Before ANY commit/push/release/deploy: PII + credential sweep.** Treat user-pasted content as containing PII by default.

## 6. Code-level gotchas (each of these shipped a bug once)

| Gotcha | Rule |
|---|---|
| Scintilla colors are **BGR** | Every `SendScintilla(SCI_*FORE/BACK,…)` takes a COLORREF: `(blue<<16)\|(green<<8)\|red` — never `QColor::rgb()` or raw `0xRRGGBB` |
| `QFileInfo::baseName()` splits on the FIRST dot | `notepatra-0.1.101.msi` → "notepatra-0". For "name (n).ext" dedup, split on the LAST dot; special-case `.tar.gz` |
| `QRegularExpression::match(str, offset)` in a loop is **O(n²)** | It re-validates the whole subject per call; use `QString::indexOf` for linear scans (125s → 1.4s on a 2MB input) |
| No emoji codepoints as UI icons | They tofu without a color-emoji font (Linux worst). Use `style()->standardIcon(QStyle::SP_*)` |
| `abort()` inside a socket's own `readyRead` = SIGSEGV | Defer via `QueuedConnection`; `QPointer` for deleteLater'd objects polled in tests |
| IPC: wait for peer proof-of-life BEFORE sending payload when a fallback exists | Flushed bytes survive the peer's `abort()`; success = `bytesToWrite()==0` drained |
| Single-line multi-MB file = whole-doc lexer range per keystroke | Cap regex slices (512KB); perf fixtures need newlines |
| "Nth thing" labels (untitled tabs, window titles) | Never derive from current index — scan visible labels for max-N and preserve assigned labels |
| Session-passive modes (`saveSession` no-op) | Must still prompt per modified tab on close + skip owner-evidence cleanup; grep sibling-flag sites |
| Palette/theme edits | Verify Light AND Dark AND Monokai; never change one ternary arm; eyeball a `samples/` file in each |
| Keyword lists | Pull from primary sources (postgresql.org, learn.microsoft.com, dev.mysql.com, cppreference) — N++/Scintilla lists lag. SQL dialect priority: T-SQL → PostgreSQL → MySQL → rest |
| No hard-coded model-capability allowlists | Never gate features on "is this model good enough" substring lists; trust the user's model choice |
| Qt6 migration (deferred, assessed) | Doable as macOS-only Qt6 via versionless CMake. THE trap: `editor.cpp` legacy codecs (Windows-1252 etc.) — `QStringConverter` can't decode them → MUST link `Qt6::Core5Compat`, never rewrite (silent ANSI corruption). Only `editor.cpp` + `chartrender.cpp:51` need `QT_VERSION` guards |

## 7. Current state & fix queues (as of 2026-07-05)

Recent formal grades (fleet-reviewed, source-verified — full reports in `eval/GRADE_REVIEW_2026-07-05.md`, plus the 05-31 docs in `eval/`):

| Area | Grade | Top blockers |
|---|---|---|
| Coding assistant | **B** | Write-gate wire bypasses (Compose dry-run prompt-only; coding-Chat/Data get unfiltered tools via `aipanel.cpp:3817`); `writeFileAtomic` not atomic (`aipanel.cpp:7400` — can destroy both copies; correct pattern at `ai_tools.cpp:1755`); zero tests on gate/rollback paths |
| Data analyst (app) | **C-** | Read-only "gate" is a bypassable prefix allowlist (`dbconnections.cpp:247`); DuckDB not opened READ_ONLY (`duckdb_client.cpp:51`); mutations approved by model-supplied `confirm:true` (`ai_tools.cpp:1800`); sync GUI-thread queries, no timeout/cancel |
| Data analyst (eval stack) | **B+** | 26b frozen SQL GT 98.4%, 12b arm 92.7%; 5 of 8 May-31 grader P0s still unapplied; eval/ not version-controlled |
| Noter | **C-** (audit 2026-06-06) | Capture core solid; reminders/search/export/Extract-offline broken; quick-win list exists, nothing fixed yet. Separate A-grade queue: issue #563, branch `noter-agrade` (12 findings: 3 CRIT + 9 HIGH, local only) |

Other open local work: `win-open-ghost-fixes` branch (Windows .sql double-click ghost + 16MB hang — 13 confirmed findings, GUI-thread hang behind invisible window, fix plan ranked, no code yet); apply_diff small-model mitigations (gemma4:12b clusters: escalation ladder, escape lint, loop breaker); deferred "eval-improvement release" (grader P0s).

**Priority order if you pick up cold:** (1) Data-app safety cluster — real read-only enforcement + route `query_sql`/`csv_query` through the existing v0.1.111 approval gate + make the phantom test real; (2) `writeFileAtomic` fix + gate/rollback regression tests; (3) coding write-gate bypass closure; (4) then feature rocks (gated `run_command`, retrieval).

## 8. Working agreement with the owner

- **Model division (until 2026-07-07):** Fable 5 = brain — decides what to do/not do, synthesizes after completion, does all grading. Opus 4.8 = hands — implementation agents. After that date, default inheritance unless renewed.
- Workflow-first for substantive tasks; parallel agent fan-out for independent heavy subtasks (standing preference). Prompt fixes must generalize — no per-model/per-dataset overfitting; fixes go in tool/loop code, prompts carry only the contract.
- No regressions: a fix must never break a working feature. Comparisons are presented in tables. Save learnings immediately after every non-trivial fix.
- Walk the anti-digression playbook (`planning/next_phase_plan.md`, summarized in `.claude/skills/np-anti-digress/`) before any non-trivial task; announce triggers that fire.
