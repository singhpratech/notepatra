# Reddit posts — one per subreddit

**One post per sub.** Cross-posting identical content to multiple subs is detected and shadow-banned. Each post below is **tailored to the audience** of that specific sub.

**Posting strategy:**
- Space them ~3 days apart so each gets fresh attention
- Reply to every comment in the first 2 hours
- Don't ask for upvotes anywhere
- Use the title verbatim — they're calibrated for each sub
- All posts use https://notepatra.org as the URL

---

## r/linux

**Title:**
```
Notepatra — a 5 MB native code editor for Linux that brings the Notepad++ feel to Linux/Mac/Windows
```

**Body:**
```markdown
I'm Prateek. I've spent years on Linux watching Windows users open Notepad++ and fix things in seconds — broken JSON, messy SQL, tangled HTML — while I was stuck with Wine hacks or Electron editors that take 3 seconds to open a 50 KB file.

Don Ho has been clear that Notepad++ will never be ported to Linux. So I built **Notepatra** — what Notepad++ would look like if it had been built today, in C++ and Rust, with cross-platform support and local AI as a first-class feature.

**For Linux specifically:**
- 5 MB native binary, 1.8 MB compressed download
- Uses your distro's Qt5 (apt install qtbase5-dev libqscintilla2-qt5-dev)
- Native GTK launcher integration via .desktop file
- Hicolor theme icons at all sizes (16/32/48/64/128/256)
- One-line install: `curl -fsSL https://notepatra.org/install.sh | sh`
- Verifies SHA-256 before extracting (refuses to install on mismatch)

**Architecture:**
- C++17 + Qt5 + QScintilla for the UI/editing engine — same Scintilla that powers Notepad++
- Rust core via C FFI for everything that touches untrusted bytes (file I/O, search, diff, JSON/HTML/SQL parsers). The C++ layer is intentionally minimal.

**The AI angle:** local Ollama only, no cloud, no API keys, no telemetry. Connects to localhost. There's a regex-first / AI-fallback fixer for JSON / HTML / brackets, plus an AI Assistant tab.

**100+ file types, 44 language lexers**, macro recording, session persistence, crash recovery, plugin system (.so files), built-in terminal, REST client, hex editor, Git integration with gutter markers.

GPL-3.0, free forever, no business tier.

https://notepatra.org
https://github.com/singhpratech/notepatra

Happy to answer any Linux-specific packaging questions. Currently working on Linux ARM64 builds for v0.1.1.
```

**Best time:** Monday 9 AM ET (r/linux peaks Monday morning).

---

## r/programming

**Title:**
```
I built a 5 MB native Notepad++ alternative for Linux/Mac/Windows in C++ and Rust
```

**Body:**
```markdown
A few months ago I got fed up with Electron editors eating 500 MB to display a 50 KB text file, and with the fact that Notepad++ — the editor I actually liked when I was on Windows — has never been ported to Linux or Mac.

So I built **Notepatra**: native C++17 + Rust, ~3-5 MB executable on every platform, cross-compiles to Linux, macOS Apple Silicon, and Windows from one source tree.

**The technical interesting bits:**

1. **C++/Rust split.** The C++ layer is intentionally just Qt5 + QScintilla glue. Everything that touches untrusted bytes — file I/O via mmap2, search via aho-corasick, diff via similar/Myers, JSON/HTML/SQL parsers via serde_json + custom code, hashing, base64 — is in Rust. C FFI between the two via `staticlib`. The reasoning: every text editor CVE class lives in C/C++ parsing untrusted bytes, so move that to Rust and you eliminate buffer overflows by construction.

2. **2 GB file support via mmap2.** Open a 2 GB log file instantly without allocating 2 GB of RAM. Files >50 MB auto-disable syntax highlighting + auto-complete to keep things snappy.

3. **Local AI via Ollama.** Two-button approach in JSON/HTML/Bracket fixers: a Rust-powered "Fix + Format" pass that handles common cases instantly (missing braces, trailing commas, single quotes, unquoted keys, missing closers) and a separate "AI Fix" button for the harder cases the regex pass can't repair. Plus an AI Assistant tab with Explain, Find Bugs, Refactor, Write Tests, Add Comments, Generate Docs, Optimize, and Translate (Python ↔ JavaScript only). All local. No cloud. No API keys.

4. **Plugin system.** Drop a .so/.dylib/.dll in `~/.config/notepatra/plugins/`. Plugin C ABI is two functions: `notepatra_plugin_name()` and `notepatra_plugin_run(text, len)`.

5. **Verifiable releases.** Every binary ships with SHA-256 checksums + cosign keyless signatures (Sigstore + Rekor transparency log) + SLSA build provenance attestations. The install scripts auto-verify before extracting and refuse to install on mismatch.

**Compared to:**
- VS Code: ~14× smaller. Notepatra is 1.8 MB on Linux, VS Code is 300+ MB.
- Sublime Text: similar size on Mac/Windows, but free, GPL-3.0, no $99 license.
- Vim: not modal, has a GUI, runs everywhere Vim does.
- Notepad++: cross-platform, has AI, has Rust core. Same QScintilla engine.

GPL-3.0. Single maintainer. No telemetry. No cloud.

Site: https://notepatra.org
Source: https://github.com/singhpratech/notepatra
SECURITY.md: https://github.com/singhpratech/notepatra/blob/main/SECURITY.md

Happy to talk about any of the design decisions — C++/Rust boundary, why Qt5 not Qt6, the QScintilla `__declspec(dllimport)` saga that took 12 CI iterations to find, the windeployqt rabbit hole on Windows. AMA.
```

**Best time:** Tuesday 8 AM ET.

---

## r/cpp

**Title:**
```
A native code editor in C++17 + Qt5 + Rust core (Notepad++ alternative for Linux/Mac/Windows)
```

**Body:**
```markdown
**Project:** Notepatra — https://github.com/singhpratech/notepatra

I'm a long-time C++ developer who got tired of explaining why I use Electron editors on Linux. So I built one — native C++17, 5 MB, cross-platform.

**The C++ technical bits this sub will care about:**

- **Qt5 + QScintilla** for the UI and editing engine. Same Scintilla that powers Notepad++.
- **C++17 features used:** structured bindings (sparingly), `__has_include` for optional QScintilla lexer headers, `if constexpr`, fold expressions in a couple of places, `std::variant` for plugin return types.
- **CMake build system** with `find_package(Qt5)`, AUTOMOC, single source tree. CMakeLists is short and platform-aware (`if(WIN32) ... elseif(APPLE) ... else()`).
- **MSVC compatibility:** the headache. Took 12 CI iterations to find the actual root cause: `windeployqt` doesn't recursively scan third-party DLLs, so `qscintilla2_qt5.dll` pulling in QtWidgets was invisible to the deploy step, and `platforms\qwindows.dll` never got copied. Fix: run windeployqt twice, once on the exe and once on the QScintilla DLL. There's a writeup of the saga in the commit history if anyone wants to skim.
- **Windows-specific gotchas I hit:** the `QSCINTILLA_DLL` define MUST be set on the consumer side (without it, MSVC linker can't resolve `__declspec(dllimport)` for `QsciScintilla::staticMetaObject`). The `/MT` vs `/MD` CRT trap. The `windeployqt --compiler-runtime` flag's reliance on `VCINSTALLDIR`. All documented in the build.yml comments.

**Why mix Rust in:**
- C++ for the parts where Qt's API is the path of least resistance (UI, signal/slot, QString glue)
- Rust for the parts where memory safety matters most (file I/O, search, diff, parsers — i.e. anything that processes untrusted bytes)
- C ABI between them, no exceptions, no smart pointers crossing the boundary
- The Rust crates: `memmap2`, `aho-corasick`, `similar`, `serde_json` (preserve_order), `regex`, `encoding_rs`, `sqlformat`, the obvious `md-5/sha1/sha2/base64` set
- Result: the speed of C++ + the safety of Rust + a 5 MB binary

**License:** GPL-3.0 (because QScintilla is GPL-3, the combined work is too)

**Builds on:** Linux (apt qtbase5-dev), macOS Apple Silicon (Homebrew qt@5), Windows (Visual Studio 2022 + Qt 5.15.2 from install-qt-action + QScintilla 2.14.1 from Riverbank source)

**Smoke-test in CI:** every build on every platform now runs a `test_lexers` target that instantiates every QsciLexer and asserts each one produces non-empty styling. Catches stub-lexer regressions across platforms.

105/105 tests passing. Single maintainer. No telemetry. Free forever.

Happy to answer any C++ specific questions — the FFI design, the AUTOMOC + Q_OBJECT setup, the platform-specific link libs, anything.
```

**Best time:** Tuesday 11 AM ET (r/cpp is small but engaged).

---

## r/rust

**Title:**
```
Notepatra: a code editor where Rust is the core (file I/O, search, diff, parsers) and C++/Qt is just the UI glue
```

**Body:**
```markdown
**Project:** https://github.com/singhpratech/notepatra

I built a cross-platform code editor where the Rust crate is **the load-bearing core**, not just an "embedded for performance" afterthought. The C++ layer is intentionally minimal — just Qt5 + QScintilla wiring for the UI. Everything that touches untrusted bytes lives in Rust.

**Why this design:**

Every CVE class in text editors comes from C/C++ parsing untrusted input. File loading, search, diff, JSON, HTML, SQL — all classic places for buffer overflows, use-after-free, sign-confusion bugs. So the design rule was: **the more attack-prone the code path, the more it should be in Rust.**

**The Rust crate (rust-core/):**
```
[dependencies]
memmap2 = "0.9"          # 2 GB file support via mmap
encoding_rs = "0.8"      # CRLF + BOM + encoding detection
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
```

Compiled as `crate-type = ["staticlib"]` with `lto = true`, links into the Qt5 exe via plain C ABI. No exceptions cross the boundary. No std types either — only primitive types and raw pointers in the FFI.

**The C FFI surface (rust-core/include/notepad_core.h):**

Plain C structs, plain C functions, plain `void *`. The C++ wrapper in `src/rustbridge.cpp` does all the QString ↔ UTF-8 conversion. The Rust side never sees a Qt type, the C++ side never sees a Rust type. Each function returns owned memory that the C++ side has to `npc_free_string()` / `npc_free_text_result()` when done.

**Cool things this enables:**

- Open a 2 GB log file in 200ms (mmap2 doesn't allocate)
- Find/replace across 50 files in parallel (rayon)
- JSON fixer that handles missing braces, trailing commas, single quotes, unquoted keys, missing `{` after `[` — multi-pass with explicit stack-based brace closure (the v0.1.0 release notes have the gnarly details)
- Detection of CRLF/LF/CR + UTF-8/16/ANSI/ISO-8859-1 via encoding_rs

**Things I'd love feedback on:**

- The C ABI design — am I missing anything ergonomic? I'm passing a lot of `(const char *, size_t)` pairs and returning structs by value. Considered cbindgen but found it overkill for ~30 functions.
- `cargo audit` runs in CI on every build. Currently zero advisories. Anything else I should be running? `cargo deny`? `cargo geiger`?
- LTO + opt-level 3 + staticlib produces a 5 MB lib. Anything I should add to shrink further? `panic = "abort"`?
- Should I use `bindgen` for the reverse direction (Rust calling Qt) for any features? Currently I do all that in the C++ layer.

**Site:** https://notepatra.org
**Source:** https://github.com/singhpratech/notepatra

GPL-3.0, single maintainer, no telemetry. Cross-platform binaries with cosign signatures and SLSA provenance.
```

**Best time:** Wednesday 10 AM ET (r/rust is most active mid-week mornings).

---

## r/coding

**Title:**
```
Notepatra v0.1 — a 5 MB native code editor that's actually fast (Linux/Mac/Windows)
```

**Body:** (shorter, more general-purpose than r/programming)

```markdown
After getting tired of Electron editors taking 500 MB to display a 50 KB file, I built one in C++ and Rust. ~3-5 MB. Cross-platform. Notepad++ is the obvious inspiration (same Scintilla engine).

What's new vs Notepad++:
- Cross-platform from day 1 (Linux + Mac + Windows from one C++ codebase)
- Local AI via Ollama (no cloud, no API key) for JSON/HTML/Bracket fixers + Explain/Refactor/Test code generation
- Rust core for memory-safe file I/O, search, diff, parsers
- Per-release SHA-256 + cosign + SLSA build provenance for verifiable downloads

What's the same:
- Tabs, syntax highlighting for 100+ file types, find/replace with regex, macro recording, plugin system, session persistence, crash recovery
- Free, GPL-3.0, no telemetry

https://notepatra.org
```

---

## r/macapps

**Title:**
```
Notepatra — a 3 MB native Apple Silicon code editor (Notepad++ alternative for Mac)
```

**Body:**
```markdown
I built a native macOS code editor that's basically what Notepad++ would feel like if it ran on Mac. C++ + Rust under the hood, ~3 MB executable, ~22 MB .dmg with bundled Qt frameworks.

**Mac-specific:**
- Apple Silicon native (M1/M2/M3/M4)
- Notepatra.app bundle with proper .icns icon (so Finder shows the real icon, not the generic one)
- Drag-to-Applications install via .dmg
- Brew-built Qt5 frameworks bundled via macdeployqt
- Connects to Ollama via localhost for local AI (no cloud)
- Honors macOS Dark mode

**Caveat:** I haven't paid the $99/year for Apple Developer notarization yet, so Gatekeeper will warn on first launch. Right-click → Open the first time, then it remembers.

**General features:**
- 100+ file types with full syntax highlighting
- AI-powered JSON/HTML/SQL/Bracket fixers via local Ollama (no cloud)
- 2 GB file support via memory-mapped I/O
- Side-by-side compare/diff
- Built-in Git integration
- Macro recording
- Plugin system (.dylib)
- Free, GPL-3.0

https://notepatra.org

The Apple Silicon binary is ~2.7 MB extracted. The Linux binary is ~5 MB. Different compiler optimization (clang strips more aggressively than gcc).

Happy to answer Mac-specific questions.
```

**Best time:** Wednesday 2 PM ET.

---

## r/opensource

**Title:**
```
Notepatra — a single-maintainer GPL-3 code editor with verifiable releases (cosign + SLSA)
```

**Body:**
```markdown
Released v0.1 of Notepatra today — a native cross-platform code editor in C++17 + Rust. GPL-3.0, single maintainer, no telemetry, no enterprise tier.

The thing I'm most proud of about this release isn't the editor itself — it's the **release pipeline transparency**.

**Three independent integrity checks per release:**

1. **SHA-256 checksums** — every artifact has a `SHA256SUMS` file, the install scripts verify before extracting and refuse on mismatch
2. **Cosign signatures (Sigstore)** — keyless OIDC signing tied to the GitHub Actions workflow; certs recorded in the public Rekor transparency log; no long-lived signing key for me to leak
3. **SLSA build provenance attestations** — cryptographically links each binary to the git commit + workflow file + runner environment that built it

Anyone can verify a downloaded binary independently:
```sh
sha256sum -c SHA256SUMS
cosign verify-blob --certificate-identity-regexp '^https://github.com/singhpratech/notepatra/' \
  --certificate-oidc-issuer 'https://token.actions.githubusercontent.com' \
  --certificate notepatra-linux-x64.tar.gz.pem \
  --signature  notepatra-linux-x64.tar.gz.sig \
  notepatra-linux-x64.tar.gz
gh attestation verify notepatra-linux-x64.tar.gz --owner singhpratech
```

**Other open-source goodness:**
- Cargo audit runs in CI on every build (catches vulnerable Rust deps)
- CodeQL static analysis on the C++ source
- Dependabot weekly updates
- SECURITY.md with private vulnerability disclosure path
- RFC 9116 /.well-known/security.txt
- Threat model documented honestly (including what's NOT done — no Authenticode, no Apple notarization)

The editor itself: Notepad++ alternative for Linux/Mac/Windows, ~5 MB native binary, local AI via Ollama, 100+ file types, plugin system. Built because every developer deserves a fast, free, smart text editor that doesn't eat 500 MB of RAM.

https://notepatra.org
https://github.com/singhpratech/notepatra
```

**Best time:** Monday 11 AM ET.

---

## After posting

For each post:

1. **Stay at the keyboard for 2 hours.** Reddit decides whether to bury or boost a post in the first hour based on comment velocity.
2. **Reply to every comment.** Even hostile ones. Especially hostile ones, politely. Concede legit points, push back on misinformation.
3. **Don't edit your post** to add "EDIT:" notes — they look defensive. Just reply in the comments.
4. **If a thread takes off**, add "Recently asked in this thread: [question] — [answer]" as a top-level reply so latecomers see the highlights.
5. **The next morning**, post a wrap-up reply in your top comment: "Thanks everyone — biggest things I learned: [list]".

## What NOT to do

- ❌ Don't post the same content to multiple subs the same day
- ❌ Don't spam links to your other subs
- ❌ Don't ask people to upvote anywhere
- ❌ Don't pay for upvotes (ban risk)
- ❌ Don't argue with downvoters
- ❌ Don't post screenshots that look like marketing
- ❌ Don't include affiliate links
- ❌ Don't post during US holidays (Reddit goes quiet)
