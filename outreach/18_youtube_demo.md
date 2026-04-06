# YouTube demo video — script

A 90-second screencast embedded on the homepage. YouTube videos rank in Google AND drive direct sub-conversions on the channel. The demo is the most-watched piece of content on most editor websites.

## Tools

- Recording: OBS Studio (free, every platform), or QuickTime on Mac
- Editing: DaVinci Resolve (free) or just trim in OBS
- Music: don't use any (it dates the video)
- Cursor highlight: Use OBS's "Cursor Effects" or `key-mon` on Linux to make clicks visible

## Length: 90 seconds

People bail at the 30-second mark on technical demo videos. Get to "this is what it does" in the first 5 seconds, show the AI feature in the first 30 seconds, end with the install command.

## The script

```
[00:00–00:05] OPEN COLD with the editor already running, a Python file open, syntax highlighted.
   No logo. No intro music. No "hey everyone." Just the editor.
   VOICEOVER: "This is Notepatra. A 5 megabyte native code editor for Linux, Mac, and Windows."

[00:05–00:15] FILE BROWSER: open a JSON file with broken syntax (missing brace, single quotes, trailing comma)
   VOICEOVER: "100+ file types, syntax highlighting on every one."
   Click around 3 different files to show the lexers working: .py, .js, .md, .sql, .yaml.

[00:15–00:30] OPEN THE BROKEN JSON FILE.
   VOICEOVER: "When something is broken — say, a JSON file with missing braces, trailing commas, single quotes —"
   CLICK the JSON Tools tab. Click "Fix + Format".
   The file fixes itself in front of you.
   VOICEOVER: "— the Rust core fixes it instantly. No AI. Just regex."

[00:30–00:45] OPEN A NEW BROKEN JSON FILE that the regex fix CAN'T repair (something gnarly).
   Click "Fix + Format" — it does its best but leaves it partial.
   VOICEOVER: "When the regex isn't enough —"
   CLICK "AI Fix". Wait 2 seconds. The file completes properly.
   VOICEOVER: "— Notepatra hands it to local Ollama. Your code never leaves your machine."

[00:45–01:00] OPEN THE COMPARE/DIFF PLUGIN. Pick two files.
   Show the side-by-side editor with green/red markers and Prev/Next navigation.
   VOICEOVER: "Side-by-side compare with Myers diff. Built in. No plugin needed."

[01:00–01:15] OPEN THE AI ASSISTANT TAB (Ctrl+Shift+A).
   Select some code in the editor. Click "Explain". Watch the response stream in.
   Then click "Find Bugs". Watch.
   VOICEOVER: "AI Assistant: Explain, Find Bugs, Refactor, Write Tests, Generate Docs, Optimize. Local Ollama. Zero cloud."

[01:15–01:30] CUT TO TERMINAL.
   Type the install command, hit Enter, watch it install.
   VOICEOVER: "One command. Linux, Mac, Windows. Free forever, GPL-3."
   Show: `curl -fsSL https://notepatra.org/install.sh | sh`

[01:30] FREEZE FRAME on the install command. Overlay text appears:
   "notepatra.org"
   "github.com/singhpratech/notepatra"

   No outro music. No "like and subscribe." End on the URL.
```

## Production tips

1. **Record at 1920×1080 or 2560×1440.** Anything smaller and the code is unreadable on mobile.
2. **Use a large font (16-18pt)** in the editor during recording. Default is too small for video.
3. **Pause after every action** for ~1 second so viewers can see what happened.
4. **Don't rush.** A slow, clear demo is better than a fast confusing one.
5. **Record the voiceover separately** in audacity or your DAW, then layer it over the screen capture in editing. Trying to talk and click at the same time produces awkward dead air.
6. **One take is fine.** Don't overproduce. The audience is technical — they care about content, not polish.
7. **No music.** Background music dates videos faster than anything else.
8. **No animated intros.** Get straight to the editor.

## YouTube upload settings

- **Title**: `Notepatra — a 5 MB native Notepad++ alternative for Linux, Mac, Windows`
- **Description** (the YouTube SEO killer):

```
Notepatra is a native cross-platform code editor inspired by Notepad++ but rebuilt for the AI era. ~3-5 MB native binary on every platform. C++17 + Rust under the hood. Free forever, GPL-3.0.

→ Website:    https://notepatra.org
→ GitHub:     https://github.com/singhpratech/notepatra
→ Install:    curl -fsSL https://notepatra.org/install.sh | sh
              (Windows PowerShell: irm https://notepatra.org/install.ps1 | iex)
→ Security:   https://github.com/singhpratech/notepatra/blob/main/SECURITY.md

What's in the demo:
00:00 Cold open — the editor in action
00:05 Syntax highlighting for 100+ file types
00:15 JSON regex fixer (Rust core, instant)
00:30 AI Fix for broken JSON (local Ollama)
00:45 Side-by-side Compare with Myers diff
01:00 AI Assistant — Explain / Find Bugs / Refactor / Tests / Docs
01:15 One-command install

Key features:
✓ 100+ file types with 44 language lexers
✓ Memory-mapped 2 GB file support via Rust
✓ AI-powered JSON / HTML / Bracket / SQL fixers (regex first, AI fallback)
✓ Local AI via Ollama — zero cloud, zero telemetry, zero API keys
✓ Built-in Git integration with gutter markers
✓ Built-in terminal, REST client, hex editor, markdown converter
✓ Plugin system (.so / .dylib / .dll)
✓ Macro recording, session persistence, crash recovery
✓ Verifiable releases — SHA-256 + cosign + SLSA build provenance

Notepatra is licensed under GPL-3.0. Free forever. No telemetry. No cloud. No paid tier.

#opensource #programming #linux #cpp #rust
```

- **Tags** (YouTube allows 500 chars):
  ```
  notepatra, code editor, notepad++, notepad++ alternative, linux code editor, mac code editor, windows code editor, c++, rust, qt5, qscintilla, ai code editor, local ai, ollama, cross platform editor, lightweight code editor, native code editor, gpl code editor, free code editor, open source editor, notepad, text editor, programming, syntax highlighting, plugin system, git integration
  ```

- **Thumbnail**: Use the Notepatra icon as the background, big readable text overlay: "5 MB" + "Notepad++ for Linux/Mac/Win". Make it readable at 16:9 mobile thumbnail size. No clickbait faces.

- **Category**: Science & Technology
- **Language**: English
- **Caption**: Auto-generate then proofread
- **Cards/end screens**: Add a card to the GitHub link at 0:30 and another to notepatra.org at 1:15

## Embedding on the homepage

After the video is uploaded, embed it on notepatra.org in a `<section>` between the AI section and the Plugins section. Use the YouTube `<iframe>` tag with `loading="lazy"` so it doesn't slow page load.

```html
<section id="demo">
    <div class="section-label">90-second demo</div>
    <div class="section-title">See it in action</div>
    <div style="position: relative; padding-bottom: 56.25%; height: 0; max-width: 800px; margin: 0 auto;">
        <iframe src="https://www.youtube-nocookie.com/embed/YOUR_VIDEO_ID"
                title="Notepatra demo"
                frameborder="0"
                allowfullscreen
                loading="lazy"
                style="position: absolute; top: 0; left: 0; width: 100%; height: 100%; border-radius: 12px;"></iframe>
    </div>
</section>
```

Use `youtube-nocookie.com` instead of `youtube.com` so YouTube doesn't drop tracking cookies on visitors who never click play. This matches the "no telemetry" promise the website makes.

## SEO impact

Embedded YouTube videos rank in Google's video search AND increase the **time-on-page** signal that Google uses to rank the page itself. A 90-second video pushes average time-on-page from ~30 seconds to 60+ seconds, which is a meaningful ranking signal.

Also: people share videos. They almost never share static landing pages. A good demo video gets shared in DMs ("hey check this out") which drives the kind of organic word-of-mouth that no amount of SEO can buy.
