# Product Hunt launch

Product Hunt resets every day at midnight Pacific Time. The top-of-day product gets the most prominent front-page placement and a meaningful traffic spike (the exact number varies a lot — anywhere from a few hundred for low-ranking days to several thousand for top finishers). This is your **one shot** — you can only launch a product on PH once.

## Where

https://www.producthunt.com/posts/new

## Best day to launch

**Tuesday 12:01 AM Pacific Time.** Why:
- Mondays: people are catching up on email, low engagement
- Tue-Thu: peak engagement
- Fri/Sat/Sun: PH ranks weekend launches separately, fewer reviewers
- 12:01 AM Pacific: PH days run midnight-to-midnight Pacific, so launching at 12:01 gives you a full 24 hours
- Don't launch the same week as a major Apple/Google event — they steal attention

## Pre-launch (do these the day BEFORE)

- [ ] Create your Product Hunt account if you don't have one (use Twitter/X to sign in for credibility)
- [ ] Add a profile picture and a short bio
- [ ] Follow the Product Hunt @ProductHunt account
- [ ] Tell 5–10 friends to upvote when you launch (NOT a vote ring — these should be people who actually find it useful)
- [ ] Have your screenshots, video, and gallery images ready
- [ ] Test the website on a slow connection

## The submission form

### Tagline (60 char max — this is the headline)

```
The first code editor built for the AI era — 5 MB native
```

(Alternative tagline if the above feels too clickbaity:
`Notepad++ alternative for Linux, Mac, Windows — with local AI`)

### Description

```
Notepatra is a native C++/Rust code editor inspired by Notepad++ but rebuilt for the AI era.

3-5 MB native executable on every platform. Single codebase runs on Linux, macOS Apple Silicon, and Windows. Uses QScintilla (the same Scintilla editing engine as Notepad++) and adds local AI integration via Ollama — the AI runs entirely on your machine, nothing leaves your computer.

→ 100+ file types with 44 language lexers (Python, Rust, Go, C++, SQL, JSON, Markdown, etc.)
→ AI-powered JSON / HTML / SQL / Bracket fixers (regex first, AI when regex isn't enough)
→ Memory-mapped 2 GB file support via Rust core
→ Side-by-side file comparison with Myers diff
→ Built-in Git integration with gutter markers
→ Macro recording, session persistence, crash recovery
→ Plugin system (.so / .dylib / .dll)
→ Zero telemetry, zero analytics, zero phone-home
→ Free forever, GPL-3.0

Verifiable releases: every binary ships with SHA-256 checksums, cosign signatures (Sigstore), and SLSA build provenance. The install scripts auto-verify before extracting.

I built this because every developer — on every OS — deserves a fast, free, smart text editor that doesn't eat 500 MB of RAM.
```

### Topics (pick 3-4 — PH limits you)

```
Developer Tools
Productivity
Open Source
Artificial Intelligence
```

### First Comment (post this AS the maker, immediately after launch)

```
Hi Hunters! I'm Prateek, the maker of Notepatra. AMA.

A few things I want to call out upfront:

🛠 What this is: a native cross-platform code editor inspired by Notepad++. The C++ layer is just Qt UI glue (~3-5 MB). Everything that touches untrusted bytes — file I/O, search, diff, JSON/HTML/SQL parsers — is in Rust for memory safety. Cross-compiles to Linux, macOS Apple Silicon, and Windows from one source tree.

🤖 Why "AI era": every Notepad++ workflow today eventually involves "fix this broken JSON" or "explain this regex" or "rewrite this bash". The web answer is to copy-paste into ChatGPT. Notepatra brings that loop in-editor via local Ollama — your code never leaves your machine. There's a regex-first / AI-fallback fixer for JSON, HTML, and brackets; an AI Assistant tab with Explain / Refactor / Find Bugs / Write Tests / etc.; and a Compare/Diff plugin.

🔒 Why "free forever, GPL-3.0": QScintilla (the editing engine) is GPL-3, so the combined work is too. There's no business tier, no paid version, no telemetry, no analytics. The whole project is one person doing it because the gap is real.

✅ Things it has: 100+ file types, 44 language lexers, 2 GB memory-mapped files, macro recording, session persistence, crash recovery, plugin system, built-in terminal, REST client, hex editor, Git integration with gutter markers, signed releases (SHA-256 + cosign + SLSA), 105/105 tests passing.

❌ Things it doesn't have yet: LSP support, ARM Linux build, Apple notarization, Windows Authenticode signing. All on the roadmap.

Site: https://notepatra.org
Source: https://github.com/singhpratech/notepatra
Security model: https://github.com/singhpratech/notepatra/blob/main/SECURITY.md

Happy to answer anything — design decisions, why Rust, why Qt5 not Qt6, the QScintilla plugin saga, the windeployqt rabbit hole on Windows, anything.
```

## Gallery images (you need 4–5)

PH shows your gallery at the top of the listing. These convert hard:

1. **Hero shot** — the editor with syntax-highlighted code (use a Rust file or a gnarly JSON for max contrast)
2. **AI panel screenshot** — show the AI fixing a broken JSON
3. **Compare/Diff plugin** — side-by-side with green/red markers
4. **Three-platform screenshot** — Linux, Mac, Windows side by side (or use the website hero)
5. **The numbers slide** — "5 MB" "100+ file types" "0 telemetry" "GPL-3.0" overlaid on the icon

## Maker tips for Product Hunt day

1. **Reply to every comment** in the first 4 hours
2. **Don't link out** in your first comment — people stay on the listing longer if you don't immediately push them away. Push the GitHub/site link in a follow-up reply.
3. **Have the demo loaded in another tab** — when someone says "show me", paste a screenshot link instantly
4. **Ask questions back** — "what file types do you work in most?" — engagement compounds
5. **Don't argue with critics** — concede the legit points, ignore the trolls
6. **Watch the realtime ranking** — refresh the leaderboard at 2pm Pacific to see how you're doing
7. **At 8pm Pacific**, post a wrap-up comment thanking everyone and saying what you learned. People are checking PH at the end of their workday.

## What "winning" looks like — ballparks, not promises

The exact numbers depend heavily on the day and competing launches. Rough patterns I've seen people report:

- **Top 5 of the day** — meaningful traffic spike, noticeable star bump, often a mention in PH's daily email
- **Top 10** — smaller but real spike, some new stars
- **Below top 20** — modest impact

The variance is huge. Indie tools with a clear story and an active builder presence in the comments tend to do better than tools with bigger marketing but no real maker engagement. Notepatra's story (one person, GPL forever, local AI not cloud, no telemetry) hits the indie-friendly buttons.

## The day after

- Post a thank-you tweet/X tagging @ProductHunt
- Update the website with "as featured on Product Hunt" (only if you actually rank top 10)
- Reply to any final comments
- Add the PH badge to the README (PH provides one)

## What NOT to do

- ❌ Don't ask people to upvote
- ❌ Don't pay for upvotes (PH detects this and bans accounts)
- ❌ Don't launch on a holiday or weekend
- ❌ Don't launch the same product twice
- ❌ Don't fake the maker — your actual face should be on the listing
- ❌ Don't link to a "preorder" page — PH wants a real, usable product
- ❌ Don't title it "I built X" — third-person works better
