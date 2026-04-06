# Awesome lists — PR templates

`awesome-*` lists are curated GitHub repos that link to projects in a category. They get tens of thousands of stars and rank well in Google for "awesome cpp", "awesome rust", etc. **One PR to a popular awesome list = a permanent backlink from a 30k+ star repo.**

## Strategy

1. Pick the lists below
2. Fork the repo
3. Edit the README in the right section
4. Open a PR with the template below
5. Wait 2–14 days for the maintainer to merge
6. Once merged, the listing is permanent

Each PR follows the same format. **Do NOT spam multiple PRs to the same list.** One project, one entry per list.

---

## 1. sindresorhus/awesome (the meta-list)

Repo: https://github.com/sindresorhus/awesome

Notepatra doesn't qualify here directly — this is a list of awesome lists. Skip.

## 2. fffaraz/awesome-cpp

Repo: https://github.com/fffaraz/awesome-cpp
Section: **Editors and Notepad** (or **Tools** if Editors doesn't exist anymore)

### Entry to add (one line)

```markdown
* [Notepatra](https://github.com/singhpratech/notepatra) - The first code editor built for the AI era. Native C++/Rust binary (~3-5 MB), 100+ file types, AI-powered formatters via local Ollama. Cross-platform (Linux, macOS, Windows). [GPL-3.0]
```

### PR template

**Title:** `Add Notepatra (Notepad++ alternative built in C++/Rust with local AI)`

**Body:**
```markdown
Adding Notepatra under "Editors and Notepad".

**What it is:** A native C++17/Rust code editor inspired by Notepad++ but rebuilt for the AI era. Single codebase runs on Linux, macOS Apple Silicon, and Windows. Tiny 3-5 MB executable.

**Why it belongs in awesome-cpp:**
- Pure C++17 + Qt5 + QScintilla for the UI/editing layer
- Rust core via C FFI for memory-safe file I/O, search, diff, parsers
- CMake build system, single source tree
- 105/105 tests passing
- Open source under GPL-3.0
- Active development (released v0.1.0 in 2026-04)

**Repo:** https://github.com/singhpratech/notepatra
**Site:** https://notepatra.org
**License:** GPL-3.0

I've added the entry alphabetically in the "Editors and Notepad" section. Happy to adjust placement or wording if there's a preferred convention I missed.
```

## 3. rust-unofficial/awesome-rust

Repo: https://github.com/rust-unofficial/awesome-rust
Section: **Applications written in Rust → Text editors** (or **Embedded Rust** if there's no editor section, since the Rust part is embedded in a larger app)

### Entry

```markdown
* [Notepatra](https://github.com/singhpratech/notepatra) — A C++/Rust code editor where the Rust core handles all untrusted-input processing (file I/O via mmap2, search via aho-corasick, diff via similar/Myers, JSON/HTML/SQL parsers, base64, hashing). C++ layer is just Qt UI glue. Cross-platform. [GPL-3.0]
```

### PR template

**Title:** `Add Notepatra (C++/Rust code editor — Rust core for memory-safe untrusted input handling)`

**Body:**
```markdown
Adding Notepatra under the "Applications written in Rust" section.

**What it is:** A native cross-platform code editor (Notepad++ alternative for Linux/Mac/Windows) where the C++ layer is intentionally minimal (Qt UI glue) and **Rust is the core** for all untrusted-input handling. The reasoning: every CVE class in text editors comes from C/C++ parsing untrusted bytes — search, diff, JSON, HTML, file I/O. Move that to Rust and you eliminate the buffer-overflow class of bugs by construction.

**Rust crates used in the core:**
- `memmap2` — memory-mapped file I/O for 2 GB file support
- `aho-corasick` — fast literal search
- `regex` — regex engine
- `similar` — Myers diff
- `serde_json` (with preserve_order) — JSON
- `sqlformat` — SQL formatting
- `md-5`, `sha1`, `sha2`, `base64`, `urlencoding`
- `encoding_rs` for character encoding detection

**FFI:** plain C ABI between C++ and Rust via `cargo build --release` producing a `staticlib` (.a / .lib).

**Repo:** https://github.com/singhpratech/notepatra
**Site:** https://notepatra.org
**License:** GPL-3.0
```

## 4. luong-komorebi/Awesome-Linux-Software

Repo: https://github.com/luong-komorebi/Awesome-Linux-Software
Section: **Programming → Editors** or **Programming → Text Editors**

### Entry

```markdown
| [Notepatra](https://notepatra.org) | A 5 MB native C++/Rust code editor with local AI integration. 100+ file types, 44 lexers, JSON/HTML/SQL fixers via local Ollama. Free forever, GPL-3.0. |
```

(awesome-linux-software uses table format. Match the column structure of the existing rows.)

### PR template

**Title:** `Add Notepatra under Programming → Editors`

**Body:**
```markdown
Adding Notepatra to Programming → Editors. It's a native (non-Electron) cross-platform code editor that runs natively on Linux at 1.8 MB compressed (5 MB extracted binary).

- Cross-platform: Linux, macOS Apple Silicon, Windows
- Native C++17 + Rust, no Electron
- Local AI via Ollama (no cloud)
- 100+ file types, 44 language lexers
- Free, GPL-3.0
- https://notepatra.org
- https://github.com/singhpratech/notepatra
```

## 5. iCHAIT/awesome-macOS

Repo: https://github.com/iCHAIT/awesome-macOS
Section: **Development → Code editors**

### Entry

```markdown
* [Notepatra](https://notepatra.org) — Native C++/Rust code editor inspired by Notepad++. Apple Silicon native (M1-M4). Local AI via Ollama. ~3 MB binary, 22 MB .dmg. Free, GPL-3.0.
```

## 6. Awesomo / Awesome Mac (Awesome-mac/awesome-mac)

Repo: https://github.com/jaywcjlove/awesome-mac
Section: **Coding → Text Editors**

### Entry

```markdown
* [Notepatra](https://notepatra.org) - Cross-platform native code editor. Apple Silicon. AI via local Ollama. 3 MB binary, GPL-3.0.
```

## 7. mahmoud/awesome-python-applications (skip — Python only)

Notepatra is C++/Rust, doesn't qualify.

## 8. agarrharr/awesome-cli-apps (skip — Notepatra is GUI)

Doesn't qualify.

## 9. webpro/awesome-dotfiles (skip — not a dotfile)

## 10. h4cc/awesome-elixir (skip — not Elixir)

---

## Recommended order

Submit PRs in this priority order (one PR per repo, wait for merge before doing the next):

1. **awesome-cpp** — biggest C++ list (~60k stars). Highest impact.
2. **awesome-rust** — biggest Rust list (~50k stars). Second highest impact, and the Rust angle is genuine.
3. **Awesome-Linux-Software** — Linux-specific audience.
4. **awesome-macOS** + **awesome-mac** — Mac audience (2 PRs).
5. Skip the rest unless you find a relevant editor-specific list.

## What NOT to do

- ❌ **Don't submit a PR before v0.1.1 is released.** Maintainers check that the project actually works.
- ❌ **Don't open multiple PRs to the same list.** That's spam.
- ❌ **Don't submit your own project to a list you maintain.** Conflict of interest.
- ❌ **Don't include marketing language.** "Revolutionary AI-powered next-gen" gets rejected.
- ❌ **Don't ask for the PR to be merged.** Be patient. Some lists merge in days, others in weeks.
- ❌ **Don't open a PR with a typo.** Maintainers reject sloppy submissions.

## Estimated SEO impact

Each accepted PR is worth roughly:
- 1 permanent backlink from a 30–60k-star repo
- ~50–500 visits/month from organic GitHub discovery
- 1 ranking signal in Google for the topic

Three accepted PRs = ~150–1500 visits/month + 3 high-DA backlinks. This compounds for years.
