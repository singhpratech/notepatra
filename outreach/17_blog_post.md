# Blog post — long-form launch article

This is the **evergreen content** that compounds for years. It ranks in Google for long-tail queries forever. Cross-post to dev.to, Hashnode, Medium, and your own blog with canonical link to your blog.

**Title options** (pick one — the title is 60% of the SEO ranking):

1. `I built a 5 MB native code editor in C++ and Rust because Electron is too heavy` (the angle most people will click)
2. `Building Notepatra: a Notepad++ alternative for Linux, Mac, and Windows in 2026`
3. `Why I built a code editor where Rust handles all the dangerous parsing` (the technical angle)
4. `12 CI iterations, 3 failed hypotheses, and one missing #define: my Windows build saga`

I'd lead with #1 for maximum reach, #4 for r/programming-style audiences.

---

## Article body (~2000 words, 7-minute read)

```markdown
# I built a 5 MB native code editor in C++ and Rust because Electron is too heavy

Last year I got tired of waiting 3 seconds for VS Code to open a 50 KB JSON file on my laptop. Last week I shipped Notepatra v0.1 — a native cross-platform code editor that's the same size as your favorite font.

This is the story of how, why, and what I learned along the way. If you're a developer who's ever felt that text editors got too big, this is for you. If you build cross-platform Qt apps and want to skip my mistakes, the second half is the technical postmortem.

## The problem

I'm a developer who lives on Linux. Every text editor I had access to felt wrong:

- **Vim/Neovim**: fast, native, terminal-only. I have nothing against modal editing, but I bounce between 20 file types a day and the language ecosystem of plugins is fragmented across init.lua / init.vim / lazy.nvim / packer.
- **VS Code**: works on every OS but it's a Chromium browser pretending to be a text editor. ~300 MB on disk. ~500 MB RAM to display a small file. 3-second cold start.
- **Sublime Text**: nice but proprietary, $99, no AI integration.
- **Notepad++**: the editor I actually liked when I had a Windows machine years ago. Don Ho has been clear it will never be ported to Linux. So I lived with Wine hacks and grumbled.
- **Kate / Gedit**: KDE/GNOME native, fine-but-limited. No AI, no plugin ecosystem.

None of these are bad editors. They're each great at one thing. But none of them is what I actually wanted: **a small, fast, cross-platform editor that runs natively on every OS, has a real plugin ecosystem, and integrates AI without sending my code to a cloud provider**.

So I built one.

## Notepatra in 30 seconds

Notepatra is a native cross-platform code editor.

- **3-5 MB native binary** on every platform (Linux 5.07 MB, Windows 2.97 MB, macOS Apple Silicon 2.69 MB — different compilers strip differently)
- **C++17 + Qt5 + QScintilla** for the UI and editing (same Scintilla engine that Notepad++ uses)
- **Rust core** for everything that touches untrusted bytes — file I/O, search, diff, JSON/HTML/SQL parsers
- **Local AI via Ollama** — no cloud, no API keys, no telemetry
- **100+ file types**, 44 language lexers
- **Cross-platform from day one**: Linux x64, macOS Apple Silicon, Windows x64
- **GPL-3.0**, free forever

There's a [website](https://notepatra.org) and the [source is on GitHub](https://github.com/singhpratech/notepatra). The rest of this post is the design decisions and the technical war stories.

## Why C++ and Rust

The internet has a habit of treating C++ vs Rust as a religious war. I picked both for boring engineering reasons.

**C++ for the UI and editing layer** because Qt and QScintilla are C++. There's no point fighting that. Every cross-platform GUI library is either C++ (Qt, wxWidgets, FLTK), browser-engine-based (Electron, Tauri), or platform-specific (AppKit, Win32). Of those, Qt5 + QScintilla is the only stack that gives you a Notepad++-like editing experience without writing it from scratch. So C++17 + Qt5 + QScintilla it is.

**Rust for the core** because every CVE class in text editors comes from C/C++ parsing untrusted bytes. When you load a file you don't control, when you search for a regex, when you compute a diff between two huge buffers, when you parse half-broken JSON — those are the places buffer overflows live. Move them to Rust and you eliminate that entire bug class by construction.

The split looks like this:

```
┌──────────────────────────────────────────────┐
│            C++ Layer (Qt5 + QScintilla)       │
│   UI · Menus · Tabs · Dialogs · Editor       │
│   Terminal · AI Panel · Compare · Plugins     │
├──────────────────────────────────────────────┤
│                C FFI boundary                 │
├──────────────────────────────────────────────┤
│            Rust Core Library                  │
│   File I/O (mmap2) · Search (Aho-Corasick)   │
│   Diff (Myers) · JSON/HTML/SQL Formatters    │
│   Bracket Fixer · Hash · Base64 · Encoding   │
└──────────────────────────────────────────────┘
```

Plain C ABI between the two. No exceptions cross the boundary. No std types either — only primitive types and raw pointers. The C++ side does QString ↔ UTF-8 conversion in a thin wrapper layer. The Rust side never sees a Qt type.

This boundary is intentional. If you make the Rust side aware of Qt or the C++ side aware of Rust's memory model, you're going to get confused at 2 AM when something segfaults. Keep them separate, communicate through bytes, free what you allocate.

## The Rust crates I'm using

For the curious, here's `Cargo.toml` for the rust-core crate:

```toml
[dependencies]
memmap2 = "0.9"          # 2 GB file support via mmap
encoding_rs = "0.8"      # CRLF + BOM + encoding detection
encoding_rs_io = "0.1"
aho-corasick = "1.1"     # fast literal search
regex = "1.10"
similar = "2.6"          # Myers diff
serde_json = { version = "1", features = ["preserve_order"] }
sqlformat = "0.3"
md-5 = "0.10"
sha1 = "0.10"
sha2 = "0.10"
base64 = "0.22"
urlencoding = "2.1"
rayon = "1.10"           # parallel search across files
libc = "0.2"
```

The whole crate compiles to a `staticlib` (~5 MB with `lto = true`). It links into the Qt5 exe via the C ABI exported in `notepad_core.h`. No async runtime, no Tokio — this is a code editor, not a server.

## Cross-platform without compromising

Same source tree, three OSes, one binary per platform:

- **Linux**: `apt install qtbase5-dev libqscintilla2-qt5-dev`, then `cmake .. && make`. Builds in 30 seconds. The .tar.gz is 1.8 MB because Qt is system-installed.
- **macOS Apple Silicon**: `brew install qt@5`, build QScintilla 2.14.1 from Riverbank source against Qt5 (because Homebrew's qscintilla2 links Qt6), then `cmake && make`. The .dmg is 22 MB because we bundle Qt frameworks via `macdeployqt`.
- **Windows**: `install-qt-action` for Qt5 5.15.2, `ilammy/msvc-dev-cmd` for the MSVC toolchain, build QScintilla from source via qmake/nmake, build the main project with the `Visual Studio 17 2022` generator, and bundle Qt + QScintilla DLLs via `windeployqt`. The .zip is 48 MB because Windows is special.

Linux took 1 day. Mac took 3 days. **Windows took 3 weeks** of debugging windeployqt edge cases. Which brings us to the saga.

## The Windows saga: 12 CI iterations of pain

Here's an honest list of every wrong hypothesis I had while debugging the Windows build, in order, so you can skip them when you're packaging your own Qt5+QScintilla+MSVC app.

**Iteration 1.** "Why is `cmake --build` failing silently?" → because the workflow used `shell: cmd` and `cmd` doesn't propagate exit codes through pipes. **Fix**: use `pwsh` and check `$LASTEXITCODE` after every step.

**Iterations 2-5.** "Why is the linker reporting `unresolved external symbol QsciScintilla::staticMetaObject`?" → I tried 4 different things — wrong CMake include path, wrong custom_command output, missing Windows system libs, mixing CRTs. **Actual fix (iteration 5)**: I never defined `QSCINTILLA_DLL` in my consumer's compile flags. Without that define, `<Qsci/qsciglobal.h>` declares the QsciScintilla symbols *without* `__declspec(dllimport)`, so MSVC's linker looks for them in my own .obj files instead of the import library. One line in CMakeLists.txt fixed it: `target_compile_definitions(notepatra PRIVATE QSCINTILLA_DLL)` on Windows.

**Iteration 6.** "Why are .md / .sql / .json files not getting syntax highlighting on Windows but they work on Linux/Mac?" → Because I switched to a third-party CMake wrapper for QScintilla on Windows (`farleyrunkel/QScintilla`), and that wrapper was missing the Lexilla bindings. The lexer C++ classes existed but they were no-op stubs at runtime. **Fix**: switch back to Riverbank's official source via qmake/nmake (same as Linux/Mac), and make the build verify that all required lexer headers are present at the install path.

**Iteration 7.** "Why does the lexer test fail on macOS?" → My hand-rolled `clang++` command for the test had the wrong include paths for Homebrew's qt@5 framework layout. **Fix**: extend `CMakeLists.txt` with a `BUILD_LEXER_TEST` option that adds the test as a CMake target using the same Qt5/QScintilla discovery the main exe uses. Single source of truth, no platform-specific compiler invocations.

**Iteration 8.** "Why is `notepatra.exe --version` exiting non-zero on Windows?" → I assumed it was missing MSVC runtime DLLs and tried statically linking the C++ runtime via `/MT`. **Wrong**: Qt5 from `install-qt-action` is built with `/MD` (dynamic CRT). Linking my exe with `/MT` against `/MD` Qt produces CRT-heap mismatches that crash at startup in different ways. **Fix**: revert to `/MD` everywhere.

**Iteration 9 onwards** is where I realized I had been guessing. I launched two parallel investigation agents — one to find a successful Qt5+QScintilla+MSVC project on GitHub Actions, the other to deep-dive into `windeployqt`'s actual behavior. Both came back with the same root cause:

> **`windeployqt` does NOT recursively scan third-party DLLs.** It only inspects the direct imports of the target exe. Notepatra.exe pulls in QtWidgets *through* `qscintilla2_qt5.dll` (not directly), so windeployqt never sees QtWidgets as a dependency and **skips deploying `platforms\qwindows.dll`** (the Qt platform abstraction plugin). Without that plugin, every Qt exe crashes before main() runs.

The fix is the same approach `imgbrd-grabber` (the only known-good public Qt5+QScintilla+MSVC project on GitHub Actions) uses: **run `windeployqt` twice** — once on the exe, once on `qscintilla2_qt5.dll`. The second pass forces Qt5Widgets + Qt5PrintSupport + `platforms\qwindows.dll` discovery.

That fix is what's in v0.1.1. If you're reading this and you're also packaging a Qt5 + QScintilla + MSVC app, save yourself 3 weeks: **`windeployqt yourapp.exe && windeployqt yourthirdparty.dll`**.

## The AI integration

The AI angle is the only part of Notepatra that's genuinely new vs Notepad++. Notepad++ predates ChatGPT by a decade. In 2026, every developer workflow eventually involves "fix this broken JSON" or "explain this regex" or "rewrite this bash". The web answer is to copy-paste into ChatGPT. Notepatra brings that loop in-editor via local Ollama.

**Two patterns**:

1. **Hybrid Fix** in the JSON / HTML / Bracket panels. There's a "Fix + Format" button that uses Rust regex/parsers (instant, no AI), and a separate "AI Fix" button that sends the broken text to local Ollama (takes 1-3 seconds, handles cases regex can't).

2. **AI Assistant tab** with Explain / Find Bugs / Refactor / Write Tests / Add Comments / Generate Docs / Optimize / Translate (Python ↔ JavaScript only — be honest about the scope).

Everything is local. The Notepatra binary itself never makes outbound network connections except to localhost Ollama (and to git when you click push/pull). The whole point of "local AI" is that your code never leaves your machine. You can verify it with `strace` on Linux or Process Monitor on Windows.

## Verifiable releases

The thing I'm most quietly proud of about Notepatra isn't the editor itself — it's the release pipeline. Every binary ships with three independent integrity checks:

1. **SHA-256 checksums** — the install scripts verify before extracting and refuse to install on mismatch
2. **Cosign signatures (Sigstore)** — keyless OIDC signing tied to my GitHub Actions workflow, certs recorded in the public Rekor transparency log, no long-lived signing key for me to leak
3. **SLSA build provenance attestations** — cryptographically links each binary to the git commit + workflow file + runner environment that built it

Anyone can verify:

```sh
# 1. SHA-256
sha256sum -c SHA256SUMS

# 2. Sigstore
cosign verify-blob \
  --certificate-identity-regexp '^https://github.com/singhpratech/notepatra/' \
  --certificate-oidc-issuer 'https://token.actions.githubusercontent.com' \
  --certificate notepatra-linux-x64.tar.gz.pem \
  --signature  notepatra-linux-x64.tar.gz.sig \
  notepatra-linux-x64.tar.gz

# 3. SLSA
gh attestation verify notepatra-linux-x64.tar.gz --owner singhpratech
```

Five years ago this whole setup would have required a paid code-signing cert and a build farm. Today, sigstore + GitHub Actions OIDC + SLSA make it free for solo open-source maintainers. If you ship binaries from a public GitHub repo and you don't have these checks yet, **you should** — full instructions in [SECURITY.md](https://github.com/singhpratech/notepatra/blob/main/SECURITY.md).

## What's not in v0.1

I want to be honest about the gaps:

- **No LSP support yet** (planned for v0.2). Notepatra has lexer-based syntax highlighting only. If you need real semantic intelligence (autocomplete by symbol, jump-to-definition, refactor across files), you're going to want VS Code or a JetBrains IDE.
- **No Linux ARM64 build yet** (planned for v0.1.1)
- **No Apple notarization** ($99/year Apple Developer fee I haven't paid yet — Gatekeeper warns on first launch, right-click → Open works)
- **No Authenticode code signing on Windows** (~$300/year EV cert — SmartScreen warns for the first ~20 downloads)
- **No telemetry** (not coming, ever)

The roadmap is on the GitHub README.

## What's next

I'm going to keep building this. Things on the immediate horizon:

- v0.1.1 — bug fix release (Windows lexer regression, icon embedding, JSON CRLF detection)
- v0.1.2 — Linux ARM64 builds, the first round of community plugins, Apple notarization
- v0.2.0 — LSP support
- v0.3.0 — More AI integrations (whatever the community asks for)
- v1.0 — When I feel like the foundation is solid enough that breaking-change releases stop

If you found this useful, give it a star on GitHub. If you find a bug, file an issue. If you want to contribute, the codebase is small enough to read in an afternoon.

If you've ever felt like text editors got too big, please give it a try. https://notepatra.org

— Prateek Singh
```

## Cross-posting strategy

1. **Publish on your own blog first** (or notepatra.org/blog if you set it up) with `<link rel="canonical">`
2. **Cross-post to dev.to** with `canonical_url: https://your-blog/post` in the front matter (preserves SEO credit to your blog)
3. **Cross-post to Hashnode** with the same canonical
4. **Cross-post to Medium** as a backup audience (Medium fights canonical tags but you can still embed a "originally posted at..." link at the top)

Each platform has a different audience. The same article can pull traffic from all 4 for years.

## SEO targets for this post

The article should rank in Google for queries like:
- "5 mb code editor"
- "native code editor for linux"
- "notepad++ alternative linux"
- "rust c++ code editor"
- "qt5 qscintilla msvc windows" (the technical postmortem)
- "windeployqt qscintilla platforms qwindows.dll" (the very specific bug)

The Windows saga section is gold for SEO because the **exact error message** + the fix is something people will Google for years.
