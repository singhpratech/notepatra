# Repository Guidelines

> **Maintainers: read `MAINTAINING.md` first.** This file covers build/style basics only; `MAINTAINING.md` is the full handbook — release gates, CI traps, testing discipline, code-level gotchas, current fix queues, and the working agreement with the repo owner.

## Project Structure & Module Organization
`src/` contains the Qt5/QScintilla desktop app in C++17, with paired headers and sources such as `mainwindow.h` and `mainwindow.cpp`. `rust-core/` holds the Rust 2021 static library that handles file I/O, search, diff, formatting, and other backend logic exposed through FFI. Top-level `test_*.cpp` files are focused smoke or regression tests. `resources/` stores icons and platform assets, `docs/` contains the website/install artifacts, and `installers/` plus `scripts/` cover packaging and release helpers.

## Build, Test, and Development Commands
Use the repo script for normal local builds:

```bash
./build.sh
```

Run the full regression suite with:

```bash
./build.sh --tests
```

For manual builds:

```bash
cd rust-core && cargo build --release
mkdir -p build && cd build
cmake .. -DCMAKE_BUILD_TYPE=Release
cmake --build . --parallel "$(nproc)"
```

Run the registered regression tests manually with:

```bash
cd build
cmake .. -DBUILD_TESTING=ON
cmake --build . --target notepatra_all_tests
QT_QPA_PLATFORM=offscreen ctest --output-on-failure
```

## Coding Style & Naming Conventions
Match the existing style: 4-space indentation, braces on the same line, and short comments only where behavior is non-obvious. Use `PascalCase` for Qt types/classes, `camelCase` for C++ methods and locals, and `snake_case` for Rust modules and functions. Keep `.h`/`.cpp` pairs aligned, prefer small focused helpers, and preserve platform-specific build logic in `CMakeLists.txt`. Respect `.editorconfig`, use `.clang-format` for C++ when reflowing code, and keep Rust `cargo fmt` clean.

## Testing Guidelines
Add or update a focused `test_*.cpp` file when changing editor behavior, formatting, palette handling, Ollama integration, or lexer wiring. Prefer regression-style tests named after the feature under test, for example `test_palette.cpp` or `test_aifix.cpp`. Register new binaries in CMake so `ctest` runs them in CI.

## Commit & Pull Request Guidelines
Recent history favors short, scoped subjects such as `docs: update README version-history table`, `Windows smoke test: ...`, or `v0.1.8: ...`. Keep commits single-purpose and write subjects in that style. PRs should include a concise summary, affected platforms, commands run, and screenshots for UI changes. Link related issues and call out updates to installers, docs, or release notes.

## Security & Configuration Tips
Follow `SECURITY.md` for private vulnerability reporting. Do not commit secrets or machine-specific paths. AI features assume a local Ollama instance; document any new network-facing behavior explicitly in the PR.
