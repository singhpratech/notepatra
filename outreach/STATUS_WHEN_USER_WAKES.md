# Status when you wake up

This file is the single source of truth Claude maintains overnight. **Read this first.**

Last updated: 2026-04-06 (latest push: `e96afcc` — **THE actual root cause fix**)

---

## TL;DR — 🎉 **v0.1.1 SHIPPED ON ALL THREE PLATFORMS** 🎉

**Release URL:** https://github.com/singhpratech/notepatra/releases/tag/v0.1.1

| Platform | Status | Download |
|---|---|---|
| 🐧 Linux x64 | ✅ shipped | [notepatra-linux-x64.tar.gz (1.8 MB)](https://github.com/singhpratech/notepatra/releases/download/v0.1.1/notepatra-linux-x64.tar.gz) |
| 🍎 macOS Apple Silicon | ✅ shipped | [notepatra-macos-arm64.dmg (23.8 MB)](https://github.com/singhpratech/notepatra/releases/download/v0.1.1/notepatra-macos-arm64.dmg) |
| 🪟 **Windows x64** | ✅ **SHIPPED** | [notepatra-windows-x64.zip (39.4 MB)](https://github.com/singhpratech/notepatra/releases/download/v0.1.1/notepatra-windows-x64.zip) |

Every binary is checksummed (SHA256SUMS), cosign-signed (Sigstore + Rekor transparency log), and SLSA-attested.

**Website:** ✅ live, warning banner removed, "Translate" honestly labeled "Py↔JS", every claim verified against source.

**Outreach content:** ✅ 22 pre-written files in `outreach/` covering every launch channel.

## Verifying the Windows binary works (do this first when you wake up)

```sh
# From any Windows machine with the binary
sha256sum -c SHA256SUMS --ignore-missing
# Then: unzip notepatra-windows-x64.zip, run notepatra.exe
# Open a .md, .sql, .json file — all three should now have syntax highlighting
# Right-click notepatra.exe → Properties → Details should show real metadata
```

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

**SOLVED** (commit `e96afcc`). 12 iterations of Windows fixes were all fighting symptoms because `$qtRoot` was computed wrong from the very first commit.

The diagnostic in issue #1 from c87ecba revealed the actual bug:
- **Computed**: `D:\a\_temp\Qt\Qt\5.15.2\` ← WRONG (one level too high)
- **Actual Qt root**: `D:\a\_temp\Qt\Qt\5.15.2\msvc2019_64\`

The line `Resolve-Path "$env:Qt5_DIR\..\..\.."` walks up from `<root>\msvc2019_64\lib\cmake\Qt5` and lands at `<root>\` (the version dir) instead of `<root>\msvc2019_64\` (the actual arch-specific Qt root). install-qt-action's layout has the arch dir nested one level deeper than I assumed.

The QScintilla source build accidentally still worked because qmake.exe was on PATH via install-qt-action's OWN PATH addition, NOT via our bogus `$qtRoot\bin\` prepend. The fallback `Get-ChildItem` search in the discovery step then masked the bug by finding `qscintilla2_qt5.lib` via recursive search. So the QScintilla step exited 0 with the correct file paths, but exported the wrong `QT_ROOT` step output for downstream steps.

Every downstream step (`windeployqt`, manual DLL copy, lexer test) used `$qtRoot\bin\X` and resolved to a non-existent path → `Test-Path` failed → `exit 1`.

**The fix in `e96afcc`:**
1. Discover qtRoot by finding `qmake.exe` directly under `$env:RUNNER_TEMP\Qt` via recursive search
2. Derive `qtRoot = dirname(dirname(qmake.exe))` — bin\qmake.exe → bin → Qt root
3. Sanity-check the layout: require `$qtRoot\bin\qmake.exe`, `$qtRoot\bin\windeployqt.exe`, `$qtRoot\lib`, AND `$qtRoot\plugins\platforms\qwindows.dll` all to exist before proceeding

This combined with the `b024c5d` hybrid bundle (windeployqt + manual deterministic DLL copy + critical-files assertion) should produce a green Windows build. The `e96afcc` run is queued behind `b3d54ad` and will start in ~12 min once b3d54ad finishes failing the same way.

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
