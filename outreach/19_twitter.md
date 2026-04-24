# Twitter / X posts

Pre-written tweets for launch. Each one is under the 280-char limit. Pick the order and pacing that feels right — don't fire all of them in one day, that looks like spam.

## Tweet 1 — the launch announcement (post on launch day)

```
Notepatra v0.1 is out.

A 5 MB native code editor for Linux, Mac, and Windows. C++ + Rust. No Electron. No telemetry. No cloud. Local AI via Ollama. GPL-3.

Took me a year to build. https://notepatra.org
```

## Tweet 2 — for the C++ crowd

```
Built Notepatra in C++17 + Qt5 + QScintilla, with a Rust core for everything that touches untrusted bytes (file I/O, search, diff, parsers).

The C++ layer is just UI glue. The most attack-prone code is in Rust by design.

5 MB binary. https://github.com/singhpratech/notepatra
```

## Tweet 3 — for the Rust crowd

```
Notepatra is a code editor where Rust is the LOAD-BEARING CORE, not just an embedded perf trick.

Every untrusted-input code path — file I/O via mmap2, search via aho-corasick, JSON/HTML/SQL parsers — is in Rust. The C++/Qt layer is intentionally minimal.

https://github.com/singhpratech/notepatra
```

## Tweet 4 — for the Linux crowd

```
Linux developers: I built you a native AI-era code editor.

Not Electron. Not a web app. A 5 MB native binary that opens instantly, runs your apt's Qt5, has 100+ file types, AI via local Ollama.

curl -fsSL https://notepatra.org/install.sh | sh
```

## Tweet 5 — for the AI crowd

```
Notepatra has local AI in the editor. Connects to Ollama on localhost.

No cloud. No API key. No tokens to your provider. Your code never leaves your machine.

There's a Fix-JSON button that uses regex first, AI when regex isn't enough.

https://notepatra.org
```

## Tweet 6 — for the supply-chain security crowd

```
Every Notepatra release ships with:

✓ SHA-256 checksums
✓ Cosign keyless signatures (Sigstore + Rekor transparency log)
✓ SLSA build provenance attestations

The install scripts auto-verify before extracting. Verify with cosign verify-blob.

https://github.com/singhpratech/notepatra/blob/main/SECURITY.md
```

## Tweet 7 — show the executable size

```
Notepatra binary sizes (real bytes from the actual v0.1 release):

🐧 Linux:    5.07 MB
🪟 Windows:  2.97 MB
🍎 macOS:    2.69 MB

VS Code: 318 MB.

Native binaries are still small if you don't ship a browser engine inside them.

https://notepatra.org
```

## Tweet 8 — quote tweet for engagement

When someone tweets about VS Code being slow:

```
This is why I built Notepatra — a 5 MB native C++/Rust code editor for Linux/Mac/Windows. No Electron. Opens instantly. Local AI via Ollama instead of cloud APIs. Free, GPL-3.

https://notepatra.org
```

When someone tweets "we need a Notepad++ for Linux":

```
@reply Notepatra. 5 MB native binary. Linux + Mac + Windows from one C++/Rust codebase. Local AI via Ollama. https://notepatra.org
```

## Tweet 9 — when v0.1.1 ships

```
Notepatra v0.1.1 is out.

Fixes:
✓ Markdown / SQL / JSON syntax highlighting on Windows
✓ Embedded icons on Mac and Windows
✓ JSON CRLF detection
✓ Linux launcher icons via hicolor theme

https://github.com/singhpratech/notepatra/releases/tag/v0.1.1
```

## Tweet 10 — when you hit a milestone

```
Notepatra hit 100 GitHub stars today. Thank you to everyone who tried it, opened an issue, or sent a PR.

The roadmap for v0.2: LSP support, ARM Linux, CodeBerg mirror, and the first round of community plugins.

https://github.com/singhpratech/notepatra
```

## What NOT to tweet

- ❌ Don't beg for retweets
- ❌ Don't tag celebrities who haven't engaged with you
- ❌ Don't use more than 2 hashtags per tweet (looks spammy)
- ❌ Don't use #AI as a hashtag — it's drowning in slop
- ❌ Don't reply-spam to popular tweets with your link
- ❌ Don't post "thread incoming 🧵" — write a single complete tweet
- ❌ Don't use the word "revolutionary"

## Hashtags worth using (max 2 per tweet)

- `#opensource`
- `#programming`
- `#linux`
- `#cpp`
- `#rust`
- `#qt`
- `#gpl`
