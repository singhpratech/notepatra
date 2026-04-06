# Status when you wake up

This file is the single source of truth Claude maintains overnight. **Read this first.**

Last updated: 2026-04-06 (latest push: `b024c5d`)

---

## TL;DR (so far — Claude is still iterating)

**Website:** ✅ live, no false claims, "Translate" honestly labeled "Py↔JS", warranty disclaimer up, security headers in place, sitemap submitted-ready.

**Outreach content:** ✅ 22 pre-written files in `outreach/` covering every launch channel (Show HN, PH, Reddit, Twitter, LinkedIn, blog, YouTube script, awesome-list PRs, Search Console steps, alternativeto, slant, Bing, IndexNow). All ready to paste.

**Linux v0.1.1 binary:** ✅ green in CI (lexer test 21/21, all editor.cpp fixes verified)

**macOS Apple Silicon v0.1.1 binary:** ✅ green in CI (lexer test via CMake target verified working — 3 green runs in a row)

**Windows v0.1.1 binary:** 🔄 **STILL ITERATING.** This is the only blocker.
- 12+ CI iterations so far. Each new fix has surfaced a new failure.
- Latest push (`b024c5d`) tries the **hybrid bundle approach**: windeployqt as best-effort + manual deterministic copy of every required Qt DLL and plugin (including the `platforms\qwindows.dll` that windeployqt has been silently skipping).
- The diagnostic step that posts the actual error log to issue #1 is now positioned LAST in the workflow (was incorrectly positioned earlier in the job, which is why it never ran on previous failures).
- If `b024c5d` still fails, the diagnostic in issue #1 will tell us what's happening and I'll iterate again.
- If `b024c5d` succeeds, I tag `v0.1.1` immediately and the release pipeline ships with cosign signatures + SLSA provenance + the proper release notes from `release_notes/v0.1.1.md`.

---

## What's been pushed (in order)

| Commit | What |
|---|---|
| Earlier session | Lexer fixes, icon files, Windows QScintilla switched to Riverbank source |
| `b30c3b4` | Security hardening (SHA-256, cosign, SLSA, SECURITY.md, dependabot, codeql, CSP) |
| `5b6aba6` | CMake target lexer test for all 3 platforms + per-version release notes |
| `301a00c` | First Windows fix attempt (incorrectly used `/MT` static CRT) + IndexNow ping + stats workflow |
| `11ce819` | Stats workflow → Contents API (works around signed-commit requirement) |
| `6f87008` | Reverted bad `/MT`, added diagnostic step (later found to be at wrong position) |
| `4b9751c` | Dual-windeployqt fix |
| `0b38463` | Known Issues warning banner on download section |
| `20de2c7` | Audit fixes: `.nojekyll`, Kate row, Qt5::Core link |
| `c22136f` | Website honesty pass: Translate→Py↔JS, Generate Docs added, issue-poster re-enabled |
| `6af0992` | Outreach cleanup: removed unverified numerical claims |
| `cdcc078` | Issue-poster moved to LAST step so it actually runs on bundle failure |
| `c87ecba` | Outreach files: blog post, YouTube script, Twitter, LinkedIn, fallback plan |
| **`b024c5d`** | **Hybrid bundle: windeployqt + manual deterministic DLL copy + critical files assertion** ← latest |

---

## What you need to do when you wake up

**Best case (Windows is green and v0.1.1 is shipping):**
1. Visit https://notepatra.org — verify the warning banner is gone
2. Visit https://github.com/singhpratech/notepatra/releases — verify v0.1.1 is up
3. Try downloading the Windows binary, run it, confirm icons work and Markdown/SQL/JSON files highlight
4. Start working through `outreach/00_LAUNCH_CHECKLIST.md` for marketing

**Likely case (Windows still failing, I have a diagnostic):**
1. Read `outreach/STATUS_WHEN_USER_WAKES.md` (this file) for what I learned overnight
2. Read https://github.com/singhpratech/notepatra/issues/1 — has the latest CI failure diagnostic
3. Decide whether to:
   - Apply one more iteration with the info from the diagnostic
   - OR ship `v0.1.1` with Linux + Mac only per `outreach/21_FALLBACK_v0.1.1_linux_mac_only.md`

**Worst case (Windows fundamentally blocked, I exhausted reasonable fixes):**
1. Read this file's "What's still broken" section below for the root cause
2. Apply the fallback in `outreach/21_FALLBACK_v0.1.1_linux_mac_only.md` — ships v0.1.1 with Linux + Mac
3. Open an issue tracking the Windows fix for v0.1.2

---

## What's still broken

(Will be updated as iterations progress.)

The Windows pre-main crash. Three independent investigations agree on the root cause:

**windeployqt does not recursively scan third-party DLLs.** Notepatra.exe pulls in Qt5Widgets through qscintilla2_qt5.dll, not directly. When windeployqt scans the exe's import table, it doesn't see Qt5Widgets as a dependency, so it skips deploying `platforms\qwindows.dll` (the Qt platform plugin every Qt app needs to start).

The `b024c5d` commit's hybrid approach should solve this by manually copying `platforms\qwindows.dll` regardless of what windeployqt does. If it doesn't, the issue is something else and the diagnostic in issue #1 will reveal what.

---

## Where everything lives

- **GitHub Actions runs**: https://github.com/singhpratech/notepatra/actions
- **Issue #1 (auto-updated with last Windows failure log)**: https://github.com/singhpratech/notepatra/issues/1
- **Latest release**: https://github.com/singhpratech/notepatra/releases/latest
- **Live website**: https://notepatra.org
- **Stats endpoint**: https://notepatra.org/stats.json

---

## Outreach files ready to fire

All in the `outreach/` directory. Ordered by priority:

| File | What |
|---|---|
| `00_LAUNCH_CHECKLIST.md` | full ordered launch plan with 5 phases |
| `01_github_repo_metadata.md` | repo description + 20 topics, paste-ready |
| `02_google_search_console.md` | sitemap submission steps |
| `03_bing_webmaster.md` | Bing/DuckDuckGo/Yahoo coverage |
| `04_alternativeto_listing.md` | pre-filled listing form (highest-impact) |
| `05_slant_listing.md` | slant.co listings |
| `06_awesome_lists.md` | PR templates for awesome-cpp, awesome-rust, etc. |
| `07_show_hn.md` | Show HN body + posting strategy + first comment |
| `08_product_hunt.md` | Product Hunt launch with maker tips |
| `09_lobsters.md` | Lobste.rs post |
| `10_reddit_posts.md` | 7 sub-specific Reddit posts (linux, programming, cpp, rust, coding, macapps, opensource) |
| `17_blog_post.md` | long-form blog article (~2000 words) for evergreen SEO |
| `18_youtube_demo.md` | 90-sec demo video script + YouTube SEO |
| `19_twitter.md` | 10 pre-written launch tweets |
| `20_linkedin.md` | LinkedIn launch post |
| `21_FALLBACK_v0.1.1_linux_mac_only.md` | escape hatch if Windows can't be fixed |
| `22_WINDOWS_BUNDLE_MANUAL_FALLBACK.md` | manual DLL copy bundle (now folded into b024c5d) |

---

## Quick command reference

```sh
# Local repo state
git -C /home/papapratlinux/Documents/notepad-linux-native log --oneline -5

# Latest CI status
curl -sL "https://api.github.com/repos/singhpratech/notepatra/actions/runs?per_page=4" | \
  python3 -c "
import json,sys
d=json.load(sys.stdin)
for r in d.get('workflow_runs',[])[:4]:
    print(f\"{r['name'][:25]:25} sha={r['head_sha'][:7]} {r['status']:12} {r['conclusion']}\")"

# Latest failure log
curl -sL https://api.github.com/repos/singhpratech/notepatra/issues/1 | \
  python3 -c "import json,sys; d=json.load(sys.stdin); print(d['body'][:5000])"

# Tag v0.1.1 (only when Windows is green)
git -C /home/papapratlinux/Documents/notepad-linux-native tag -a v0.1.1 \
  -m "v0.1.1 — lexer + icon fixes; release notes in release_notes/v0.1.1.md"
git -C /home/papapratlinux/Documents/notepad-linux-native push origin v0.1.1
```

---

## Honest grade: B+ on overall progress

**A**: SEO content, security hardening, site UX, release pipeline architecture, lexer test coverage
**B+**: Mac fix (took 2 iterations to get right via CMake target)
**C+**: Windows fix (still grinding through iterations)

The Windows fight has been a learning experience worth documenting. The 17_blog_post.md "12 CI iterations" section captures the saga as evergreen SEO content — turns the pain into something useful for the next developer who hits the same wall.
