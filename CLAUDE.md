# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

**Read `AGENTS.md` (build/style basics) and `MAINTAINING.md` (the full handbook: release gates, CI traps, testing discipline, code-level gotchas, working agreement) before changing anything.** This file adds what those two leave implicit.

## Commands

```bash
./build.sh                      # Rust core + Lite app → build/notepatra
./build.sh --tests              # same, plus -DBUILD_TESTING=ON and offscreen ctest
```

Manual / partial flows (all from the repo root):

```bash
# Rust core only (the C++ link needs rust-core/target/release/libnotepad_core.a)
cd rust-core && cargo build --release && cargo test --release

# Rust lint — CI runs this with -D warnings on latest stable; `rustup update` first
cd rust-core && cargo fmt --check && cargo clippy --all-targets --all-features -- -D warnings
cd notepatra-mcp && cargo fmt --check && cargo clippy --all-targets --all-features -- -D warnings
cd notepatra-mcp && cargo test --release && cargo test --release --features remote

# C++ app + tests (use -DBUILD_TESTING=ON; NOTEPATRA_BUILD_TESTS is derived, never pass it)
cd build && cmake .. -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=ON
cmake --build . --target notepatra_all_tests --parallel "$(nproc)"   # builds EVERY test_* binary
QT_QPA_PLATFORM=offscreen ctest --output-on-failure

# One test
QT_QPA_PLATFORM=offscreen ctest -R test_palette --output-on-failure
QT_QPA_PLATFORM=offscreen ./test_palette            # or run the binary directly from build/
ctest -N | grep test_name                            # confirm a test is actually registered

# Full flavor (DuckDB + WebEngine where available) — CI builds it in build-full/, not build/
mkdir -p build-full && cd build-full && cmake .. -DCMAKE_BUILD_TYPE=Release -DNOTEPATRA_FULL=ON

# A tree is only Lite if its cache says so — check before trusting a "Lite" test run
grep -E '^NOTEPATRA_(WITH_WEBENGINE|USE_DUCKDB|FULL)' build/CMakeCache.txt
# True Lite tree (CI's Linux/Windows default): build-lite/
cmake .. -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=ON -DNOTEPATRA_WITH_WEBENGINE=OFF -DNOTEPATRA_USE_DUCKDB=OFF

# Release gates (release-check.sh calls most of the others)
bash scripts/release-check.sh
bash scripts/stale-text-check.sh
```

Lint config: `.clang-format` (LLVM base, 4-space, 100 cols, attached braces, `SortIncludes: Never`), `.editorconfig`. There is no C++ lint job in CI; Rust fmt/clippy/test run in `.github/workflows/quality.yml`.

## Architecture

Two languages, one C FFI boundary, one optional sidecar process.

**C++/Qt5 app (`src/`)**. `main.cpp` installs async-signal-safe crash handlers, reads the crash flag, binds the single-instance local server *before* constructing `MainWindow`, then restores the session. `MainWindow` (`mainwindow.cpp`, ~7.5k lines) owns menus, docks, and the `TabManager` (a `QTabWidget` of `Editor` pages). `Editor` subclasses `QsciScintilla` and is the only place editor config is applied (`applyConfig`); the Compare, formatter, and SQL-formatter panes host their *own* `QsciScintilla` views that bypass it, so any editor-wide setting needs an `applyXTo(QsciScintilla*)` helper. `CMakeLists.txt` auto-globs `src/*.cpp` (CONFIGURE_DEPENDS), so a new source file joins the app automatically, but **test targets list their sources explicitly** and will fail to link until you add the file there too.

**Rust core (`rust-core/`, crate `notepad_core`, static lib)**. Memory-mapped file I/O with encoding/BOM detection, Aho-Corasick/regex search, Myers diff (`similar`), JSON/HTML/SQL formatters, bracket fixer, hashing, SSH keygen. All entry points are `#[no_mangle] extern "C"` in `lib.rs`, declared in `rust-core/include/notepad_core.h`, and wrapped once on the C++ side by `src/rustbridge.{h,cpp}` (namespace `RustCore`, RAII around every returned buffer). Add new Rust functionality through that chain: Rust module → `lib.rs` export → header → `rustbridge`. Search results are byte ranges into the *original* text, never a lowercased copy.

**AI assistant (`src/ai_*`, `aipanel.cpp` ~8.3k lines, `edit_plan.*`, `agent_repeat_guard.h`)**. `ai_context` is the single source of truth for workspace context, `ai_tools` defines the tool catalogue and executes calls, `ai_systemprompt` carries the mode prompts (Chat / Compose / Agent / Data analyst), `edit_plan` is the review UI, and the write-approval gate lives in `aipanel.cpp`. Backends: `ollama.cpp`, `restclient.cpp` (OpenAI-compatible cloud), `network_policy.cpp` (enforces `-DNOTEPATRA_NO_CLOUD`). Fixes to model behaviour go in tool/loop code, not in prompts, and must not be tuned to one model.

**Data analyst stack** (`csvanalyst`, `duckdb_client`, `dbconnections`, `dbtree`, `chart_*`, `src/charts/`). Works on CSV + in-memory SQLite in Lite; DuckDB is Full-only and loaded from `vendor/duckdb`. Chart specs are translated to Vega-Lite by `chart_spec_to_vega` (a pure function, no Qt widgets) and rendered by WebEngine when built in, with JS assets bundled via `.qrc`.

**Diagram DSL (`src/diagram/`)**: `npd_parser` → `diagram_render` → `diagram_view`, plus `mermaid_import`. Any parser change gets an adversarial fan-out review before shipping.

**Noter** (`src/notes*`): note storage, reminders, templates, sweep/extract dialogs, export (WebEngine-gated).

**MCP sidecar (`notepatra-mcp/`)**. A standalone Rust crate (own `[workspace]`, never joins the app build) that speaks MCP over stdio and talks to the running editor through a per-user local socket / Windows named pipe. The app side is `src/mcp_bridge.{h,cpp}`, which queries the editor through the `McpEditorHost` struct of `std::function` fields so tests can fake the host. The wire contract is two-layered: the tool argument is `tab_index`/`id`, the bridge verb uses `index`. Every write surfaces an Approve/Deny card in-window. Its version in `notepatra-mcp/Cargo.toml` must match the app's `CMakeLists.txt` VERSION, and `cargo publish` is manual after the tag.

**Build flavors**. Lite is the default and must stay small (~13 MB). `-DNOTEPATRA_FULL=ON` turns on DuckDB and WebEngine; `-DNOTEPATRA_WITH_WEBENGINE=ON` alone also enables DuckDB. Detect Full in code with `NOTEPATRA_HAVE_DUCKDB` (or `|| NOTEPATRA_WITH_WEBENGINE`), never WebEngine alone, because macOS Full has DuckDB but no WebEngine. The flavor name SSOT is `src/build_flavor.h`. `-DNOTEPATRA_NO_CLOUD=ON` produces the cloud-free variant.

**Plugins** (`src/plugin.{h,cpp}`, `plugin_loader`): shared libraries exporting `notepatra_plugin_*` C functions, loaded from the per-user config dir.

## Tests

- Regression tests are top-level `test_*.cpp` files, each an `add_executable` + `add_test` in `CMakeLists.txt`. A file on disk that is not registered proves nothing; `ctest -N` is the truth. Integration tests reuse `${SOURCES}` with `src/main.cpp` filtered out and construct a real `MainWindow`.
- `notepatra_all_tests` auto-aggregates every `test_*` target, so a new registration needs no CI change.
- Every test gets a TIMEOUT (a global 600 s net exists; set an explicit one for anything that can wedge).
- Tests run under `QT_QPA_PLATFORM=offscreen`; on Windows any `QMessageBox` segfaults there, so guard sections that trigger notice boxes, and tests that touch QStandardPaths must isolate `$HOME`.
- AI-backend tests must pass with no Ollama/cloud reachable (assert the UI contract, accept "(no models)").
- Counts (lexers, extensions, backends, MCP tools) are canonical constants in `scripts/stale-text-check.sh`; bump them there and in every surface in the same commit, and prefer deriving counts from source.

## Repo-specific skills

`.claude/skills/` ships project skills: `np-anti-digress` (run before any non-trivial task), `np-release`, `np-sweep-versions`, `np-pii-sweep` (before every commit/push), `np-verify-gui` (unit pass is not UI verification), `np-bare-or-full`, `np-icon-check`, `np-encoding-probe`.

## Non-obvious rules that have each shipped a bug

Full list with rationale in `MAINTAINING.md` §6. The ones most likely to bite a new change:

- Scintilla colours are BGR COLORREFs, not `QColor::rgb()`.
- `QFile::rename` will not overwrite an existing target. Never remove-then-rename (a failed rename loses both copies); use `QSaveFile`. Four such sites still exist on main (`dbconnections.cpp`, `notes_storage.cpp`, `ai_tools.cpp`, `aipanel.cpp` chat history); the proper fix lives on the unmerged `noter-home-row` branch.
- No emoji codepoints as UI icons; use `QStyle::standardIcon`.
- `QRegularExpression::match(str, offset)` in a loop is quadratic; use `indexOf`.
- Never call `abort()` inside a socket's own `readyRead`.
- Palette/theme edits must be verified in Light, Dark, and Monokai.
- Binary is deliberately not stripped (DWARF kept for crash symbolication).
- Commit subjects are short and scoped (`docs: ...`, `v0.1.X: ...`, `git panel: ...`). `Closes #N` only closes on PR merge.
