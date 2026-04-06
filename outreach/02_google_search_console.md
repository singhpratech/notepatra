# Google Search Console — submit notepatra.org

## Why

Google deprecated their `ping?sitemap=` endpoint in 2023. The ONLY way to get notepatra.org into Google's index now is via Search Console. Until you do this, the site will not appear in Google for `notepatra` or any other query.

## Time required

5–10 minutes total.

## Step 1 — Sign in

https://search.google.com/search-console

Use whatever Google account you want — you become the permanent owner of the property.

## Step 2 — Add property

- Click **Add property** (top-left dropdown)
- Choose **URL prefix** (NOT domain)
- Enter: `https://notepatra.org/`
- Click **Continue**

## Step 3 — Verify ownership (HTML tag method, easiest)

Search Console shows a list of verification methods. Pick **HTML tag** at the top.

It will show you a meta tag like:
```html
<meta name="google-site-verification" content="abc123XYZdef456GHI789jkl-real-token">
```

**Copy ONLY the `content="..."` value** — that's the token.

Then either:

**Option A — let Claude commit it for you (faster)**
- Tell Claude: "Google verification token is: <paste>"
- Claude commits to `docs/index.html`, pushes, GitHub Pages redeploys in ~30 sec
- Click **Verify** in Search Console
- Done

**Option B — edit the file yourself**
- Open https://github.com/singhpratech/notepatra/edit/main/docs/index.html
- Find the line: `<meta name="google-site-verification" content="YOUR_VERIFICATION_TOKEN_HERE">`
- Replace `YOUR_VERIFICATION_TOKEN_HERE` with your real token
- Commit the change directly to main
- Wait ~30 sec for Pages to redeploy
- Back in Search Console, click **Verify**

## Step 4 — Submit the sitemap

Once verification succeeds:

- In the left sidebar, click **Sitemaps**
- In the "Add a new sitemap" field, type: `sitemap.xml`
- Click **Submit**
- The status will show "Success" within seconds. The "Discovered URLs" count will populate over the next few hours.

## Step 5 — Request indexing for the homepage (speeds up first crawl)

- At the top of Search Console, paste this into the URL inspection bar:
  ```
  https://notepatra.org/
  ```
- Press Enter
- Wait for the inspection to complete (~10 sec)
- Click **Request indexing**
- This puts the homepage in Google's high-priority crawl queue

You can do this for up to ~10 URLs/day. Worth doing for:
- https://notepatra.org/ (the homepage)
- https://notepatra.org/#download (download section)

## Step 6 — Wait

Google's first-crawl latency for new domains is **1–7 days** typically, sometimes up to 14. The first thing to appear in search results will be `site:notepatra.org` (returns the homepage). Then `notepatra` as a query (returns the homepage). Then long-tail queries like `notepad++ alternative linux ai` (takes weeks, depends on backlinks).

## Verifying it worked

After 1–2 days, run:
```
site:notepatra.org
```
on Google.com. If you see the homepage in results, you're indexed. If you see "0 results", wait another day.

## Troubleshooting

- **"Verification failed"**: GitHub Pages might still be redeploying. Wait 1 minute and retry. Check the meta tag is actually live by curl-ing `https://notepatra.org/` and grepping for `google-site-verification`.
- **"Sitemap couldn't be fetched"**: Check that `https://notepatra.org/sitemap.xml` returns valid XML with HTTP 200. Should already work since we set it up.
- **"Indexed but not shown in search"**: Normal. Google shows new sites in search results AFTER they have some authority signals (backlinks, age, click-through). Submitting to alternativeto.net + getting a Show HN post + a few Reddit threads will speed this up.
