# LinkedIn launch post

LinkedIn rewards longer-form posts with personal stakes. The "build in public" angle works here. Don't try to sound corporate — sound like a person.

## Post body

```
A year ago I started building a code editor. Today I'm shipping v0.1.

It's called Notepatra (link in comments). It's a native C++/Rust code editor for Linux, macOS, and Windows. ~5 MB binary. Free forever, GPL-3.0. No telemetry. No cloud. Local AI integration via Ollama.

I built it because every time I needed a quick text editor on Linux I had to choose between three bad options:

1. Vim — fast but cryptic for non-modal users
2. VS Code — powerful but 300+ MB and slow on big files
3. Wine + Notepad++ — the editor I actually wanted, but running through a compatibility layer

None of those felt right. Notepad++ is the editor I missed most from Windows, but Don Ho has been clear it will never be ported to Linux. So I built what I wished existed: a native, lightweight, plugin-friendly editor that runs the same on every OS, with the modern bits Notepad++ predates — Rust safety in the core, local AI via Ollama, signed releases with cosign + SLSA build provenance.

Things I learned along the way:

→ The C++/Rust split changes how you think about safety. Every CVE class in text editors comes from C/C++ parsing untrusted bytes. Move that to Rust and you eliminate buffer-overflow bugs by construction.

→ Cross-platform Qt is harder than people admit. Linux was 1 day. Mac was 3 days. Windows was 3 weeks of debugging windeployqt edge cases.

→ Sigstore + SLSA + GitHub Actions OIDC have made supply chain security genuinely free for solo open source maintainers. Five years ago the same setup required money for code signing certs and a paid build farm.

→ Local AI changes the editor UX. The ChatGPT-copy-paste loop disappears when "Fix this JSON" is a button in the same panel as your file.

What's next:
- LSP support (v0.2)
- Linux ARM64 builds (v0.1.1)
- More plugins
- A wishlist of features the community will ask for that I haven't thought of

If you've ever felt like text editors got too big, please give it a try. https://notepatra.org

GPL-3.0, source on GitHub. Single maintainer, no telemetry, no business tier.

#opensource #softwaredevelopment #programming
```

## First-comment tip

LinkedIn buries posts that contain external links in the body. **Put the link in the first comment** instead, like this:

```
🔗 Notepatra: https://notepatra.org
🔗 Source on GitHub: https://github.com/singhpratech/notepatra
🔗 Security model: https://github.com/singhpratech/notepatra/blob/main/SECURITY.md

Happy to answer any questions about the design decisions, the C++/Rust split, the Windows packaging story, or the AI integration. AMA.
```

## Best time to post on LinkedIn

**Tuesday or Wednesday, 8–10 AM your local time.**

- Mondays: people are catching up on email, low engagement
- Friday afternoon: people are checked out
- Weekends: dead zone for B2B / dev content
- 8–10 AM: peak commuting / "morning coffee" scrolling window

## Engagement tactics

1. **Reply to every comment in the first 4 hours.** LinkedIn's algorithm boosts posts with active discussion.
2. **Don't argue with critics.** Concede legit points, ignore trolls. LinkedIn audiences respect graciousness.
3. **Tag people who would care** — but only if you actually know them or have interacted before. Tagging celebrities who don't know you is a downvote magnet.
4. **Don't repost the same content** — write a follow-up post a week later highlighting one specific thing (e.g. "What I learned about Windows DLL hell building Notepatra").
5. **Share user feedback** when you get it — repost your favorite issue/PR/email as a follow-up week 2 post.

## What NOT to do

- ❌ "Excited to announce 🚀🎉" — LinkedIn cliché, scroll past
- ❌ Personal sob stories that aren't relevant — feels manipulative
- ❌ Hashtag spam (>3 hashtags = looks like an ad)
- ❌ "Like and share if you agree" — LinkedIn detects engagement bait
- ❌ AI-generated post content — LinkedIn users can smell ChatGPT output a mile away
