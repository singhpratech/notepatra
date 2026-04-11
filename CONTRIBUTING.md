# Contributing to Notepatra

## Before You Start
Notepatra is a native Qt5/QScintilla desktop app in `src/` with a Rust static library in `rust-core/`. Keep changes scoped. Do not mix product work, marketing copy, release notes, and refactors in one PR unless they are directly tied to the same fix.

## Local Setup
Build the app with:

```bash
./build.sh
```

Build and run the regression tests with:

```bash
./build.sh --tests
```

Manual flow:

```bash
cd rust-core && cargo build --release
mkdir -p build && cd build
cmake .. -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=ON
cmake --build . --parallel "$(nproc)"
QT_QPA_PLATFORM=offscreen ctest --output-on-failure
```

Rust-only checks:

```bash
cd rust-core
cargo fmt
cargo clippy --all-targets --all-features -- -D warnings
```

## Style
- C++ uses 4-space indentation, same-line braces, and nearby-file formatting conventions.
- Rust should stay `cargo fmt` clean.
- Use focused comments only where intent is not obvious.
- Prefer small targeted helpers over broad rewrites.
- Preserve platform-specific logic in `CMakeLists.txt` and CI when changing build behavior.

## Tests
If you touch lexer wiring, palette behavior, formatter panels, or Ollama integration, update or add a `test_*.cpp` regression binary and make sure it is registered through CMake/CTest. `test_ollama` and `test_aifix` skip cleanly when Ollama is offline; that behavior should remain intact for CI.

## Pull Requests
PRs should include:
- a short problem statement and the chosen fix
- commands you ran locally
- affected platforms (`Linux`, `macOS`, `Windows`)
- screenshots or GIFs for UI-visible changes
- doc or release-note updates when user-facing behavior changes

Keep commit subjects short and scoped, matching the existing history, for example `docs: ...`, `Windows: ...`, or `git panel: ...`.

