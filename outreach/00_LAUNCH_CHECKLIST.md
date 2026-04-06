# Notepatra launch checklist

Order matters. Do them top-to-bottom on launch day. Each one takes 5–15 minutes.

## Phase 1 — foundation (do this BEFORE you tell anyone)

These are silent and just set you up so when traffic hits, the funnels work.

- [ ] **GitHub repo description** — paste from `01_github_repo_metadata.md`
- [ ] **GitHub repo topics** — paste from `01_github_repo_metadata.md`
- [ ] **Google Search Console** — verify ownership + submit sitemap (steps in `02_google_search_console.md`)
- [ ] **Bing Webmaster Tools** — verify ownership + submit sitemap (steps in `03_bing_webmaster.md`)
- [ ] **Tag v0.1.1** — once Windows CI is green, push the tag so the release workflow publishes binaries (waits on Claude)
- [ ] **Verify the website warning banner is gone** — once v0.1.1 ships, remove the orange "Known issues in v0.1.0" banner

## Phase 2 — directory listings (high-DA backlinks)

These build the SEO foundation. Each one is a permanent backlink.

- [ ] **alternativeto.net listing** — submit at https://alternativeto.net/software/new/, body text in `04_alternativeto_listing.md`. Historically the #1 inbound funnel for editors.
- [ ] **Slant.co listing** — https://www.slant.co/improve, follow `05_slant_listing.md`
- [ ] **Awesome lists PRs** — copy text from `06_awesome_lists.md`. One PR per list:
  - awesome-cpp
  - awesome-rust
  - awesome-linux-software
  - awesome-text-editors (if it exists)
  - awesome-macos
  - awesome-windows
- [ ] **OpenSourceFeed.com submission**
- [ ] **AlternativeFor.com submission**

## Phase 3 — content launch (the spike)

Pick ONE of these to lead with. Don't fire all of them on the same day — space them ~3 days apart so each gets fresh attention.

- [ ] **Show HN post** — `07_show_hn.md`. **Best day to post: Tuesday or Wednesday, 8–10 AM Pacific.** Watch r/news for breaking events that day — those steal HN front-page attention. Avoid Mondays (people catching up) and Fridays (low traffic).
- [ ] **Product Hunt launch** — `08_product_hunt.md`. Schedule 24h in advance via PH dashboard. **Best day: Tuesday 12:01 AM Pacific.** Launches reset at midnight Pacific, so you get a full day on the leaderboard. Fridays and weekends die fast.
- [ ] **Lobsters post** — `09_lobsters.md`. Similar audience to HN but smaller and friendlier. Post during US business hours.
- [ ] **r/linux** — `10_reddit_linux.md`. Post Monday 9 AM ET when the subreddit is busiest.
- [ ] **r/programming** — `11_reddit_programming.md`. Post Tuesday 8 AM ET.
- [ ] **r/cpp** — `12_reddit_cpp.md`. Smaller but more engaged audience.
- [ ] **r/rust** — `13_reddit_rust.md`. They love posts that highlight Rust safely interfacing with C++.
- [ ] **r/coding** — `14_reddit_coding.md`. General programming subreddit, big audience.
- [ ] **r/macapps** — `15_reddit_macapps.md`. Mac-specific, post when v0.1.1 has the icon working.
- [ ] **r/opensource** — `16_reddit_opensource.md`. Friendly audience.

## Phase 4 — long-tail content (the slow burn)

These don't spike traffic but build evergreen SEO that compounds for years.

- [ ] **Blog post on the website** — `17_blog_post.md`. Long-form write-up of the design decisions.
- [ ] **YouTube demo video** — script in `18_youtube_demo.md`. Embed on the homepage.
- [ ] **dev.to post** — same content as the blog post, cross-posted with canonical link.
- [ ] **Hashnode post** — same.
- [ ] **Medium post** — same. Each platform = a separate audience.

## Phase 5 — measure and iterate

- [ ] **Daily download stats** — `https://notepatra.org/stats.json` updates automatically every 06:00 UTC via the workflow
- [ ] **Star history graph** — embed `https://api.star-history.com/svg?repos=singhpratech/notepatra&type=Date` in the website
- [ ] **Twitter/X tweets** — pre-written in `19_twitter.md`
- [ ] **LinkedIn post** — `20_linkedin.md`

## Tips for not wasting your one shot

1. **Show HN gets one shot.** If your post flops, you cannot repost the same project for 6 months. Wait until everything is polished.
2. **Reddit communities ban duplicates.** Post the SAME content to different subs (with a tweaked title for each), but never twice to the same sub.
3. **Don't ask for upvotes.** Reddit detects vote manipulation and shadowbans accounts.
4. **Engage with comments.** The first 30 minutes after posting determines whether something goes viral. Be there to answer questions.
5. **Have your demo loaded** so when someone asks "show me", you can paste a screenshot or video link instantly.
6. **Don't oversell.** "I built X, here's why" beats "X is the best thing ever". Notepatra has known limitations — be honest about them.
7. **Reply to negative comments politely.** People who hate the project but engage are still driving traffic.
