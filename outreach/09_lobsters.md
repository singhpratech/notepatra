# Lobste.rs post

Lobsters is a smaller, friendlier, more technical version of Hacker News. ~10x smaller traffic but the audience is software engineers who actually read posts. **You need an invite to post** — getting one takes ~2 weeks if you don't already have one (ask in #lobsters on freenode/libera, or in the Lobsters chat).

## Where

https://lobste.rs/stories/new

## Title

```
Notepatra – a 5 MB native Notepad++ alternative for Linux/Mac/Windows in C++ and Rust
```

## URL

```
https://notepatra.org
```

## Tags (Lobsters has a fixed tag set — pick from these)

- `programming`
- `release`
- `c++`
- `rust`

## Description (used as the comment if Lobsters generates one from the URL)

Lobsters auto-fetches an excerpt from the URL. If you need to add your own comment after submitting, post this:

```
Hi Lobsters,

I built Notepatra because there's no native Notepad++ on Linux/Mac. Wine hacks suck, Electron editors are 300 MB and slow. I wanted something that opens instantly, runs natively, and has the same lightweight feel as Notepad++ has on Windows.

Architecture:
- C++17 + Qt5 + QScintilla for the UI/editing engine — same Scintilla that powers Notepad++
- Rust core via C FFI for everything that touches untrusted bytes (file I/O via mmap2, search via aho-corasick, diff via similar/Myers, JSON/HTML/SQL parsers, base64, hashing). The C++ layer is intentionally minimal so the largest attack surface is in memory-safe Rust.
- ~3-5 MB native executable on each platform

The AI integration is local-only via Ollama on localhost. No cloud, no API keys, no telemetry. There's a regex-first / AI-fallback fixer for JSON / HTML / brackets, plus an AI Assistant tab with Explain / Refactor / Find Bugs / etc.

Verifiable releases: SHA-256 + cosign keyless signatures (Sigstore + Rekor) + SLSA build provenance attestations. The install scripts auto-verify before extracting.

GPL-3.0, free forever, single maintainer.

Things I'm specifically interested in feedback on:
- The C++/Rust split — where does the boundary feel awkward?
- The plugin C ABI — too minimal? Too restrictive?
- The Windows packaging story (the windeployqt + qscintilla2_qt5.dll dance was painful)

https://github.com/singhpratech/notepatra
```

## Lobsters etiquette

- **Lobsters voters are picky.** Posts that look like marketing get downvoted to oblivion.
- **Be technical.** Talk about the design decisions, not the marketing benefits.
- **Engage seriously.** Lobsters comments are higher-effort than HN — write back at the same level.
- **Don't crosspost.** If you're posting on HN the same week, don't mention it.
- **Don't repost.** If your post drops out of the top page, you can't repost.

## What "winning" looks like

- Top page (15+ upvotes) = ~500-2000 visits
- Front page (50+ upvotes) = ~5000-10000 visits and a rare slow-burn organic backlink

Lobsters traffic is small but the conversion rate (visit → star → install) is high. The audience is exactly the right one for a niche tool like Notepatra.
