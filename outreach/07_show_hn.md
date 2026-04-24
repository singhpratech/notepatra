# Show HN post

## Where

https://news.ycombinator.com/submit

## Title (use this exact wording — Show HN posts have a strict format)

```
Show HN: Notepatra – A native AI-era code editor (~4 MB) for Linux, Mac and Windows
```

(80 char max. This one is 78 chars. The "5 MB" hook is the strongest specific number you have. Don't say "AI-powered" in the title — HN is allergic to "AI" buzzwords; let people discover the AI angle by reading.)

## URL

```
https://notepatra.org
```

## Text (the body of the post — paste in the "text" field, leave URL empty if HN forces you to choose)

```
I'm Prateek Singh. I've spent years on Linux watching Windows users open Notepad++ and fix things in seconds — broken JSON, messy SQL, tangled HTML — while I was stuck with Wine hacks or 300 MB Electron editors that take 4 seconds to open a 50 KB file.

Every editor told me to pick two of {fast, powerful, native}. Vim is fast and native but cryptic. VS Code is powerful and cross-platform but heavy. Notepad++ is fast and powerful but Windows-only.

So I built Notepatra. Not a port. Not a wrapper. Not "Notepad++ for Linux". Something new — what Notepad++ would look like if Don Ho had built it in 2026, in C++ and Rust, with cross-platform support and local AI as a first-class feature.

Architecture:
- C++17 + Qt5 + QScintilla for the UI and editing engine (same Scintilla that powers Notepad++)
- A Rust core (mmap2 + aho-corasick + similar/Myers diff + serde_json) for everything that touches untrusted bytes — file I/O, search, diff, JSON/HTML/SQL parsing. Memory-safe by construction.
- C FFI between the two layers
- ~3-5 MB native executable on every platform (Linux 5.07 MB, Windows 2.97 MB, macOS Apple Silicon 2.69 MB — different compiler stripping)
- Linux download is 1.8 MB, Mac .dmg is 22 MB, Windows .zip is 48 MB (Mac and Windows bundle Qt for portability)

What it does:
- 100+ file types with 44 language lexers
- Tabbed editing, color tags, 3 themes, session persistence, crash recovery, macros, code folding, brace matching
- Find/Replace with 5 tabs (Find, Replace, Find in Files, Mark, Go to), regex, extended escapes, find-all-in-all-tabs
- 2 GB file support via memory-mapped I/O
- Built-in plugins: JSON/HTML/SQL/Bracket fixers (regex first, AI fallback), Compare/Diff (Myers algorithm, side-by-side Scintilla), Git integration with gutter markers, Terminal as a tab, REST client, Hex editor
- A plugin system for native shared libraries (.so / .dylib / .dll)

Local AI:
- Connects to Ollama on localhost only. Never the cloud. Never an API key.
- AI Assistant: Explain, Find Bugs, Refactor, Write Tests, Add Comments, Generate Docs, Optimize, Translate (Python ↔ JavaScript only — be honest about scope)
- Hybrid Fix in JSON/HTML/Bracket tools — Rust regex/parser handles common cases instantly, AI is a separate "AI Fix" button for the harder ones the regex pass can't repair
- Default model: qwen3.5:9b. Works with any Ollama model.

Verifying it's not malware:
- Every release ships with SHA-256 checksums, cosign keyless signatures (Sigstore + Rekor transparency log), and SLSA build provenance attestations
- The install scripts auto-verify SHA-256 before extracting and refuse to install on mismatch
- Zero telemetry, zero analytics, zero outbound network connections except localhost Ollama and git pull/push when you click them
- Full threat model in SECURITY.md

Things it doesn't have yet (honest list):
- Authenticode code signing on Windows (would cost ~$300/year for an EV cert)
- Apple notarization on macOS ($99/year Apple Developer)
- LSP support (planned for v0.2)
- Linux ARM64 builds (planned for v0.1.1)

License: GPL-3.0. Free forever. No paid tier. No "business" version.

Source: https://github.com/singhpratech/notepatra
Site: https://notepatra.org
SECURITY.md: https://github.com/singhpratech/notepatra/blob/main/SECURITY.md

I built this because every developer — on every OS — deserves a fast, free, smart text editor that doesn't eat 500 MB of RAM. AMA.
```

## Best time to post

**Tuesday or Wednesday, 8:00–10:00 AM Pacific Time.**

Why:
- Mornings: HN front page turns over fast, so posting early gives you the most uptime on the front page
- Tuesday/Wednesday: peak engagement; Mondays everyone's catching up, Fridays everyone's leaving
- Pacific time: HN traffic skews US west coast (Bay Area + Seattle + LA)
- Avoid 9–10 AM EST: that's when bay area hasn't woken up yet but east coast is glued to Slack, lower comment volume

## Strategy for the first 30 minutes

The first 30 minutes determine whether the post takes off. HN's front page is a ranked list — you need ~5 upvotes in the first 10 minutes to break out of `/newest` and into `/show`.

1. **Post.** Don't comment yet.
2. **Sit at your computer for the next hour.** This is non-negotiable.
3. **Within the first 5 minutes, post a top-level comment** answering the most likely question: "Why not just use Notepad++?". Use this:

   > Quick FAQ since I expect this comes up:
   > 
   > **Why not just use Notepad++?** Notepad++ is Windows only. The official position from Don Ho is that it will never be ported (he's said this multiple times). I love Notepad++. I built Notepatra for the Linux/Mac users who can't have it.
   >
   > **Why not just use VS Code?** I have it installed too. It uses 500 MB to show me a 50 KB file. The startup time is 3 seconds. I want a tool that's instant.
   >
   > **Why C++ + Rust?** C++ because Qt and QScintilla are C++ and that's zero friction for the UI. Rust for everything that touches untrusted bytes (file I/O, parsers, search) so the most attack-prone code paths are memory-safe by construction.
   >
   > **Why GPL?** QScintilla is GPL-3, so the combined work is too. If you don't like that the alternative is to pay Riverbank Computing for a commercial QScintilla license — Notepatra doesn't.

4. **Reply to every comment in the first hour.** Even ones you don't like. Especially ones you don't like, politely.
5. **Don't ask for upvotes anywhere.** HN detects vote rings and shadowbans accounts.
6. **Don't post the same project twice.** If this flops, you cannot repost it for 6 months. Pick your moment.

## What to expect — these are ballparks, not promises

The variance on Show HN is enormous and depends on luck (what else is on the front page that hour), the title, the first 30 minutes of comments, and whether one influential commenter shares it elsewhere. Some rough categories of outcome people report:

- **Best case** — post hits front page, gets thousands of visitors, hundreds of stars, dozens of PRs/issues over the following week
- **Median case** — post sticks around `/show` for a few hours, a few hundred visitors, modest star bump
- **Worst case** — post falls off `/newest` with single-digit upvotes, almost no traffic

The single best leading indicator is **comment velocity in the first 30 minutes**. If people are engaging with substantive questions, the post is likely to climb. If it sits without comments, it's likely to die. Don't read too much into the first 5 minutes — that's still in the "hidden" phase before HN moves new posts onto the active page.

## Don't do these things

- ❌ Don't title it "Show HN: I built this" — HN hates first-person titles
- ❌ Don't say "the best", "revolutionary", "AI-powered", "next-generation"
- ❌ Don't link to a Twitter/X thread instead of the project
- ❌ Don't use emojis in the title
- ❌ Don't capitalize words for emphasis
- ❌ Don't mention you posted on Reddit too
- ❌ Don't post during a major HN news cycle (election day, Apple keynote, big tech earnings) — those steal attention
