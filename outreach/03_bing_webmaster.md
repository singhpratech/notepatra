# Bing Webmaster Tools — submit notepatra.org

Bing indexing is faster than Google, and Bing's index also feeds DuckDuckGo, Ecosia, and Yahoo. So submitting to Bing covers ~4 search engines at once.

## Time required

5 minutes.

## Step 1 — Sign in

https://www.bing.com/webmasters/

You can sign in with:
- A Microsoft account (Outlook/Hotmail/Live)
- A **Google account** (Bing has Google SSO)
- A Facebook account

## Step 2 — Add site

- If you already verified your site in Google Search Console, Bing offers an **"Import your sites from Google Search Console"** button. Click it. Bing will read the verified properties from your Google account and let you import them in one click — no additional verification needed.
- If you DIDN'T do Google first, click **Add a site manually**:
  - Enter: `https://notepatra.org`
  - Click **Add**

## Step 3 — Verify ownership (only if you didn't import from Google)

Bing offers three methods:

**Option A — XML file (default)**
- Bing gives you a file like `BingSiteAuth.xml` to upload to the root of your site
- Tell Claude: "Bing verification XML content is: <paste>"
- Claude saves it as `docs/BingSiteAuth.xml`, pushes, you click Verify

**Option B — Meta tag**
- Bing gives you a meta tag like:
  ```html
  <meta name="msvalidate.01" content="ABCDEF1234567890">
  ```
- Tell Claude: "Bing verification token is: <paste>"
- The placeholder is already in `docs/index.html` — Claude swaps it in
- You click Verify

**Option C — DNS CNAME**
- Bing gives you a CNAME record to add at your DNS provider (GoDaddy)
- Permanent verification, doesn't depend on the website host

## Step 4 — Submit the sitemap

- In the left sidebar, click **Sitemaps**
- Click **Submit sitemap**
- Enter: `https://notepatra.org/sitemap.xml`
- Click **Submit**

## Step 5 — Confirm IndexNow is also wired up

We already have a GitHub Actions workflow at `.github/workflows/indexnow.yml` that pings the IndexNow API on every push to `docs/`. This is the **fastest way to get Bing to recrawl** — Bing usually picks up changes within hours of the IndexNow ping.

You can verify the IndexNow key file is reachable:
```
curl https://notepatra.org/4e0be99a5f38f170e2721278d7f518f2.txt
```
Should return: `4e0be99a5f38f170e2721278d7f518f2`

## Step 6 — wait

Bing usually indexes new sites within 24–72 hours of sitemap submission, much faster than Google. After indexing, the site appears in:
- Bing search (https://www.bing.com)
- DuckDuckGo (uses Bing index for results)
- Ecosia (uses Bing index, plants trees)
- Yahoo Search (uses Bing index)

## Bonus: GitHub-aware Bing features

Bing Webmaster Tools includes a **"Site Explorer"** feature that shows you which pages are indexed and which aren't. Useful for checking if all the section anchor URLs (`#features`, `#download`, `#security`, etc.) got picked up.

It also includes a **"Keyword Research"** tool that shows you what people are searching for. Useful for tuning the page content.
