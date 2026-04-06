# Status when you wake up

This file is the **single source of truth** Claude maintains overnight. Read this first.

Last updated: (Claude updates this every push)

---

## TL;DR

(Claude fills this in based on actual outcome)

- ☐ Windows v0.1.1 SHIPPED with all 3 platforms green
- ☐ Windows v0.1.1 SHIPPED with Linux + Mac only (Windows pending v0.1.2)
- ☐ Still iterating — read the diagnostics section below

---

## What worked overnight

(Claude fills this in)

---

## What's still broken

(Claude fills this in)

---

## What you need to do when you wake up

(Claude fills this in — the smallest possible list of UI clicks / decisions only you can make)

---

## Where the diagnostics live

- **GitHub Actions runs**: https://github.com/singhpratech/notepatra/actions
- **Issue #1 (auto-updated with last Windows failure log)**: https://github.com/singhpratech/notepatra/issues/1
- **Latest release**: https://github.com/singhpratech/notepatra/releases/latest
- **Live website**: https://notepatra.org
- **Stats endpoint**: https://notepatra.org/stats.json

---

## Outreach files ready to fire

All of these are pre-written in the `outreach/` directory and ready to paste:

- `00_LAUNCH_CHECKLIST.md` — full ordered launch plan
- `01_github_repo_metadata.md` — GitHub repo description + topics
- `02_google_search_console.md` — sitemap submission steps
- `03_bing_webmaster.md` — Bing/DuckDuckGo/Yahoo coverage
- `04_alternativeto_listing.md` — pre-filled listing
- `05_slant_listing.md` — slant.co
- `06_awesome_lists.md` — PR templates for awesome-cpp, awesome-rust, etc.
- `07_show_hn.md` — Show HN post body + posting strategy
- `08_product_hunt.md` — Product Hunt launch
- `09_lobsters.md` — Lobste.rs post
- `10_reddit_posts.md` — 7 sub-specific Reddit posts
- `17_blog_post.md` — long-form blog article (cross-post to dev.to/Hashnode/Medium)
- `18_youtube_demo.md` — 90-sec demo video script
- `19_twitter.md` — 10 pre-written tweets
- `20_linkedin.md` — LinkedIn launch post
- `21_FALLBACK_v0.1.1_linux_mac_only.md` — what to do if Windows can't be fixed

---

## Quick verify checklist

When you wake up, in order:

1. **Check this file's TL;DR section** — Claude has marked which scenario happened
2. **Visit https://notepatra.org** — make sure the site looks good
3. **Visit https://github.com/singhpratech/notepatra/releases** — confirm v0.1.1 is published (or not)
4. **Verify a download** — pick the platform you're on, run:
   ```sh
   curl -sL -O https://github.com/singhpratech/notepatra/releases/download/v0.1.1/SHA256SUMS
   curl -sL -O https://github.com/singhpratech/notepatra/releases/download/v0.1.1/notepatra-linux-x64.tar.gz
   sha256sum -c SHA256SUMS --ignore-missing
   ```
5. **Read issue #1** if Windows isn't fixed — it has the latest CI failure diagnostics

---

## Contact loop

If something is fundamentally broken (e.g. you can't access GitHub Settings, the binary that DID ship is corrupted, etc.) — that's the kind of issue I can't fix from here. Tell me what you see and I'll help you fix it as soon as you message.
