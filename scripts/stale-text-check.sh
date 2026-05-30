#!/usr/bin/env bash
# stale-text-check.sh — wired stale-text audit for every release.
#
# Why this exists:
#   v0.1.59 shipped with the App About dialog still claiming "100+ file
#   types · 48 language lexers · Local AI via Ollama/llama.cpp/
#   OpenAI-compatible" — frozen since v0.1.31. The version number itself
#   auto-flows from CMakeLists.txt via NOTEPATRA_VERSION, but feature
#   counts (lexers, file extensions, AI backends) are hardcoded text in
#   many surfaces and do NOT auto-update. Every release has a chance to
#   leak stale numbers into the App About body, README intro, website
#   stat cards, or the GitHub repo description that shows in search
#   snippets.
#
#   This script is the wired check: canonical counts live at the top,
#   every surface is grep-asserted to contain them, and a mismatch
#   FAILS the release. Update the constants below + every surface they
#   reference in the same commit.
#
# Usage:
#   bash scripts/stale-text-check.sh
#   VERSION=0.1.60 bash scripts/stale-text-check.sh
#
# Exits non-zero if anything is stale.

set -euo pipefail

cd "$(dirname "$0")/.."

# ── CANONICAL VALUES — bump these when reality changes ─────────────────
# When a new lexer / file extension / backend is added, update the
# constant here AND every surface that mentions it. The script will
# fail loudly if any surface drifts from these values.
LEXER_COUNT=82
FILE_EXT_COUNT=238
BACKEND_COUNT=6
BACKEND_LIST="Ollama / llama.cpp / OpenRouter / Ollama Cloud / OpenAI / Azure OpenAI"
# BARE_BIN_MB removed v0.1.86 — verify-download-sizes.sh now downloads the
# actual artifact and asserts byte count + stripped-vs-not, which is strictly
# more rigorous than a string-compare against a manually-edited constant.

cmake_version="$(grep -oE 'project\(Notepatra VERSION [0-9]+\.[0-9]+\.[0-9]+' CMakeLists.txt | grep -oE '[0-9]+\.[0-9]+\.[0-9]+')"
VERSION="${VERSION:-$cmake_version}"

FAIL=0
PASS=0

assert_contains() {
    local label="$1" file="$2" pattern="$3"
    if grep -qF -- "$pattern" "$file" 2>/dev/null; then
        printf "  ✓ %s\n" "$label"
        PASS=$((PASS + 1))
    else
        printf "  ✗ %s\n      file: %s\n      expected to contain: %s\n" \
               "$label" "$file" "$pattern"
        FAIL=$((FAIL + 1))
    fi
}

assert_contains_regex() {
    local label="$1" file="$2" pattern="$3"
    if grep -qE -- "$pattern" "$file" 2>/dev/null; then
        printf "  ✓ %s\n" "$label"
        PASS=$((PASS + 1))
    else
        printf "  ✗ %s\n      file: %s\n      expected to match regex: %s\n" \
               "$label" "$file" "$pattern"
        FAIL=$((FAIL + 1))
    fi
}

echo "=== Stale-text audit (v$VERSION) ==="
echo "  canonical: $LEXER_COUNT lexers · $FILE_EXT_COUNT file extensions · $BACKEND_COUNT backends"
echo

echo "── App About dialog body (src/mainwindow.cpp) ──"
assert_contains "About body: file extension count" \
    src/mainwindow.cpp "$FILE_EXT_COUNT file extensions"
assert_contains "About body: lexer count" \
    src/mainwindow.cpp "$LEXER_COUNT language lexers"
assert_contains "About body: backend count" \
    src/mainwindow.cpp "$BACKEND_COUNT AI backends"
assert_contains "About body: full backend list" \
    src/mainwindow.cpp "$BACKEND_LIST"

echo
echo "── lexer count derived from source (src/lexerutils.cpp) ──"
# Derive the real distinct-lexer count straight from the code so the canonical
# LEXER_COUNT (and therefore every docs surface above) can never silently drift
# from what the binary actually registers. Each lexer maps to one return-case.
derived_lexer_count=$(grep -cE 'return new Qsci|return new Lexer|new Lexer[A-Z]' src/lexerutils.cpp 2>/dev/null)
if [[ "$derived_lexer_count" == "$LEXER_COUNT" ]]; then
    printf "  ✓ src/lexerutils.cpp registers %s lexers (matches LEXER_COUNT)\n" "$derived_lexer_count"
    PASS=$((PASS + 1))
else
    printf "  ✗ src/lexerutils.cpp registers %s lexers but LEXER_COUNT=%s\n      bump LEXER_COUNT and every docs surface to match the code\n" \
           "$derived_lexer_count" "$LEXER_COUNT"
    FAIL=$((FAIL + 1))
fi

echo
echo "── file-extension count derived from source (src/lexerutils.cpp extMap) ──"
# Mirror of the lexer derive-gate above for file extensions. Count the DISTINCT
# keys in the detectLanguageFromPath extMap so FILE_EXT_COUNT (and every docs
# surface that quotes it) can never silently drift from what the binary maps.
# The block is delimited from `extMap = {` to its first closing `    };` so the
# count excludes the separate nameMap below it (counting the whole file would
# over-count by the nameMap pairs). Pairs may repeat a key (e.g. several exts
# pointing at one lexer); `sort -u` collapses to distinct keys.
derived_ext_count=$(awk '/static const QHash<QString, QString> extMap = \{/{f=1} f{print} f&&/^    \};/{exit}' \
        src/lexerutils.cpp 2>/dev/null \
    | grep -oE '\{ *"[^"]+" *,' | grep -oE '"[^"]+"' | sort -u | wc -l)
if [[ "$derived_ext_count" == "$FILE_EXT_COUNT" ]]; then
    printf "  ✓ src/lexerutils.cpp extMap has %s distinct extensions (matches FILE_EXT_COUNT)\n" "$derived_ext_count"
    PASS=$((PASS + 1))
else
    printf "  ✗ src/lexerutils.cpp extMap has %s distinct extensions but FILE_EXT_COUNT=%s\n      bump FILE_EXT_COUNT and every docs surface to match the code\n" \
           "$derived_ext_count" "$FILE_EXT_COUNT"
    FAIL=$((FAIL + 1))
fi

echo
echo "── README.md ──"
assert_contains "README intro: file extension count (bold)" \
    README.md "**$FILE_EXT_COUNT file extensions**"
assert_contains "README intro: lexer count (bold)" \
    README.md "**$LEXER_COUNT language lexers**"
assert_contains "README intro: backend count" \
    README.md "**$BACKEND_COUNT entries**"
assert_contains "README: latest version row in releases table" \
    README.md "v$VERSION"

echo
echo "── docs/index.html ──"
assert_contains_regex "index.html: stat card $FILE_EXT_COUNT file types" \
    docs/index.html ">$FILE_EXT_COUNT</span></div><div class=\"stat-label\">File Types"
assert_contains_regex "index.html: stat card $LEXER_COUNT lexers" \
    docs/index.html ">$LEXER_COUNT</span></div><div class=\"stat-label\">Language Lexers"
assert_contains "index.html: meta description has lexer count" \
    docs/index.html "$LEXER_COUNT language lexers"
assert_contains "index.html: meta description has file type count" \
    docs/index.html "$FILE_EXT_COUNT file types"
assert_contains "index.html: latest version-card mentions $VERSION" \
    docs/index.html "v$VERSION"

echo
echo "── docs/docs.html ──"
assert_contains "docs.html: ships N language lexers line" \
    docs/docs.html "$LEXER_COUNT language lexers"
assert_contains "docs.html: $FILE_EXT_COUNT file extensions" \
    docs/docs.html "covering $FILE_EXT_COUNT file extensions"
assert_contains "docs.html: latest release line says v$VERSION" \
    docs/docs.html "Latest release is v$VERSION"

echo
echo "── stale version-ref sweep (user-facing phrases must point at v$VERSION) ──"
# This section catches the v0.1.82 → v0.1.83 drift: when a release bumps the
# version, every user-facing phrase listed below must contain the NEW version.
# A stale reference (e.g. hero badge stuck on the previous release) fails the
# release.
#
# Out of scope: CHANGELOG entries, release_notes/* archives, past version-card
# descriptions on the website. Those legitimately reference older versions.
#
# Template syntax: __V__ is a placeholder substituted twice — once with the
# current version's literal (dot-escaped for grep -E), once with the
# any-version regex `0\.1\.[0-9]+`. Match counts are compared per phrase.
v_lit_dot_escaped="${VERSION//./\\.}"

check_phrase() {
    local label="$1" file="$2" template="$3"
    local current_re="${template//__V__/$v_lit_dot_escaped}"
    local any_re="${template//__V__/0\\.1\\.[0-9]+}"
    if [[ ! -f "$file" ]]; then
        printf "  ⚠ %s — file missing: %s\n" "$label" "$file"
        return 0
    fi
    local all_count current_count
    # `|| true` inside each group keeps a no-match grep (exit 1) from tripping
    # `set -o pipefail` + `set -e` and aborting the whole script. Without it a
    # STALE version ref (current_re finds nothing → grep exits 1) crashed the
    # run instead of reaching the ✗ report branch below — so this sweep never
    # actually reported drift; it only ever passed because release-check bumps
    # the version first. The miss is the same class as the v0.1.107 honesty gap.
    all_count=$( { grep -oE "$any_re" "$file" 2>/dev/null || true; } | wc -l)
    current_count=$( { grep -oE "$current_re" "$file" 2>/dev/null || true; } | wc -l)
    if (( all_count == 0 )); then
        printf "  ⚠ %s — phrase template not found in %s\n      template: %s\n" \
               "$label" "$file" "$template"
        return 0
    fi
    if (( current_count == all_count )); then
        printf "  ✓ %s (%d/%d at v%s)\n" "$label" "$current_count" "$all_count" "$VERSION"
        PASS=$((PASS + 1))
    else
        local stales
        stales=$(grep -oE "$any_re" "$file" 2>/dev/null | grep -vE "^${current_re}$" | sort -u | head -5 | tr '\n' ' ')
        printf "  ✗ %s — %d stale ref(s) in %s\n      stale: %s\n      expected all to match: %s (with v%s)\n" \
               "$label" "$((all_count - current_count))" "$file" "$stales" "$template" "$VERSION"
        FAIL=$((FAIL + 1))
    fi
}

# IMPORTANT: patterns must be anchored to the live user-facing position
# (HTML attribute / element / specific colon-terminated phrase), NOT to
# generic prose snippets like "as of v…" or "v… · Now Available" that
# legitimately appear inside <code>...</code> quotes in past release-card
# descriptions or CHANGELOG entries. The anchors below avoid those.

# docs/index.html — hero, FAQ JSON-LD, download CTAs, page-body prose
check_phrase "index.html JSON-LD softwareVersion" \
    docs/index.html '"softwareVersion": "__V__"'
check_phrase "index.html sticky-CTA aria-label" \
    docs/index.html 'aria-label="Download Notepatra v__V__"'
check_phrase "index.html sticky-CTA visible text" \
    docs/index.html 'Download v__V__ ↓'
check_phrase "index.html hero-badge div" \
    docs/index.html 'hero-badge">v__V__ · Now Available'
check_phrase "index.html download section label" \
    docs/index.html 'section-label">Download v__V__</'
check_phrase "index.html 'Get Notepatra v…' download button" \
    docs/index.html 'Get Notepatra v__V__ →'
check_phrase "index.html 'Get Notepatra Local AI v…' download button" \
    docs/index.html 'Get Notepatra Local AI v__V__ →'
check_phrase "index.html 'Latest v… download sizes:' (FAQ + body, colon-anchored)" \
    docs/index.html 'Latest v__V__ download sizes:'
check_phrase "index.html JSON-LD 'as of v…:' (FAQ)" \
    docs/index.html '82 language lexers as of v__V__:'
check_phrase "index.html body lead 'As of v…: 238 file types · 82'" \
    docs/index.html 'As of v__V__: 238 file types · 82'
check_phrase "index.html lexer paragraph 'as of v… —'" \
    docs/index.html 'language lexers</strong> as of v__V__ —'

# docs/docs.html — tag header + latest-release statement
check_phrase "docs.html tag header" \
    docs/docs.html 'class="tag">v__V__ docs<'
check_phrase "docs.html 'Latest release is v…'" \
    docs/docs.html 'Latest release is v__V__'

# README.md
check_phrase "README 'v… downloads:'" \
    README.md 'v__V__ downloads:'
check_phrase "README 'Latest v… download sizes:' (colon-anchored)" \
    README.md 'Latest v__V__ download sizes:'
check_phrase "README 'Latest release: v…'" \
    README.md 'Latest release: v__V__'

echo
echo "── repo metadata ──"
if [[ -f release_notes/v$VERSION.md ]]; then
    printf "  ✓ release_notes/v%s.md exists\n" "$VERSION"
    PASS=$((PASS + 1))
else
    printf "  ✗ release_notes/v%s.md missing\n" "$VERSION"
    FAIL=$((FAIL + 1))
fi

if head -30 CHANGELOG.md | grep -qE "^## \[$VERSION\]"; then
    printf "  ✓ CHANGELOG.md top entry is [%s]\n" "$VERSION"
    PASS=$((PASS + 1))
else
    printf "  ✗ CHANGELOG.md top entry is NOT [%s]\n" "$VERSION"
    FAIL=$((FAIL + 1))
fi

echo
echo "── GitHub repo description (gh) ──"
if command -v gh >/dev/null 2>&1 && gh auth status >/dev/null 2>&1; then
    desc="$(gh repo view singhpratech/notepatra --json description --jq .description 2>/dev/null || true)"
    if [[ -z "$desc" ]]; then
        echo "  ⚠ skipping — could not fetch repo description"
    else
        if echo "$desc" | grep -qF "$FILE_EXT_COUNT file extensions"; then
            printf "  ✓ repo description mentions %s file extensions\n" "$FILE_EXT_COUNT"
            PASS=$((PASS + 1))
        else
            printf "  ✗ repo description has stale file-extension count\n      current: %s\n      expected to contain: %s file extensions\n" \
                   "$desc" "$FILE_EXT_COUNT"
            FAIL=$((FAIL + 1))
        fi
        if echo "$desc" | grep -qF "$LEXER_COUNT language lexers"; then
            printf "  ✓ repo description mentions %s language lexers\n" "$LEXER_COUNT"
            PASS=$((PASS + 1))
        else
            printf "  ✗ repo description has stale lexer count\n      expected to contain: %s language lexers\n" \
                   "$LEXER_COUNT"
            FAIL=$((FAIL + 1))
        fi
        if echo "$desc" | grep -qF "$BACKEND_COUNT AI backends"; then
            printf "  ✓ repo description mentions %s AI backends\n" "$BACKEND_COUNT"
            PASS=$((PASS + 1))
        else
            printf "  ✗ repo description has stale backend count\n      expected to contain: %s AI backends\n" \
                   "$BACKEND_COUNT"
            FAIL=$((FAIL + 1))
        fi
    fi
else
    echo "  ⚠ skipping — gh CLI not authenticated (run 'gh auth login')"
fi

echo
echo "── installer-filename pinning (README admin/fleet install commands) ──"
# Bug class caught in v0.1.86 follow-up: README:281-297 had install commands
# like `notepatra-0.1.78.msi` that didn't get bumped during the version sweep
# because they're inside table cells / code spans, not in the patterns the
# np-sweep-versions skill mentally scans. Anyone copy-pasting the silent-
# install command got a 404.
#
# Scope: only check LIVE install commands. Skip historical release-table rows
# (lines containing `releases/tag/v0.1.X` are release-row narrative, not
# install commands meant for the current user). Skip lines inside the
# CHANGELOG-style release history that document old versions.
stale_installers=$(grep -vE 'releases/tag/v0\.1\.' README.md \
    | grep -hoE 'notepatra(-local-ai)?[-_][^"`]*?0\.1\.[0-9]+(_amd64|_arm64|-1\.x86_64|-1\.aarch64|-x86_64|-aarch64)?\.(msi|deb|rpm|AppImage|tar\.gz|zip|dmg|exe)' \
    | grep -oE "0\.1\.[0-9]+" \
    | sort -u \
    | grep -v "^$VERSION$" || true)
if [[ -z "$stale_installers" ]]; then
    printf "  ✓ every versioned installer filename in README install commands matches $VERSION\n"
    PASS=$((PASS + 1))
else
    echo "  ✗ stale installer filename versions found in README.md install commands:"
    for v in $stale_installers; do
        echo "      $v — bump to $VERSION"
        grep -vE 'releases/tag/v0\.1\.' README.md | grep -nE "notepatra(-local-ai)?[-_][^\"\`]*?$v" | head -2 | sed 's/^/        /'
    done
    FAIL=$((FAIL + 1))
fi

echo
echo "── stale-backend mention sweep (current-tense docs must NOT list removed UI dropdown entries) ──"
# History: in v0.1.54, src/aipanel.cpp:644 removed LM Studio / Jan / Custom from
# the AI panel backend dropdown. Subsequent releases also did NOT add vLLM /
# KoboldCpp / llamafile / text-generation-webui as first-class dropdown entries.
# Current-tense docs that name those programs as Notepatra backends are stale
# and have to be fixed BEFORE release. Historical CHANGELOG / release-table
# rows in README.md are NOT a concern — those describe what past releases
# shipped and stay as-is.
#
# Files scanned: docs/index.html, docs/docs.html, docs/enterprise.html, and
# README.md sections OUTSIDE the historical release-table block (line 580–650
# is the historical changelog area; we skip it).
# Narrow regex — only the third-party server-program names that were removed
# from the AI panel dropdown. We do NOT regex on "Anthropic.*direct" /
# "Claude.*direct" because those produce false positives on legitimate
# nearby uses ("OpenAI direct" is correct; "no direct Anthropic" is the
# fix wording itself). The Anthropic-direct bug class is rare enough to
# leave to the pre-launch agent audit.
stale_backend_pat='LM Studio|vLLM|KoboldCpp|llamafile|text-generation-webui'
# 'Jan' word-boundary version is too noisy ('Jan-Mar' SQL example etc.).
# Run a SEPARATE check that requires Jan in a backend-list context (preceded
# or followed by another backend program name).
jan_context_pat='(Ollama|llama\.cpp|LM Studio|vLLM|KoboldCpp)[^<>]{0,80}\bJan\b|\bJan\b[^<>]{0,80}(Ollama|llama\.cpp|LM Studio|vLLM|KoboldCpp)'

# README historical-changelog block (release-history table rows). Skip every
# release-table row (any line linking releases/tag/v0.1.X) — those describe
# past releases factually and stay as-is. Pattern-based (not a hardcoded line
# range) so adding a new release row can't silently un-skip an older one — the
# v0.1.35 row drifted past a hardcoded 650 bound when the v0.1.102 row landed.
readme_filtered=$(grep -nvE 'releases/tag/v0\.1\.' README.md)

stale_hits=$(
    grep -nE "$stale_backend_pat" docs/index.html docs/docs.html docs/enterprise.html 2>/dev/null || true
    grep -nE "$jan_context_pat" docs/index.html docs/docs.html docs/enterprise.html 2>/dev/null || true
    echo "$readme_filtered" | grep -E "$stale_backend_pat" | sed 's|^|README.md:|' || true
    echo "$readme_filtered" | grep -E "$jan_context_pat" | sed 's|^|README.md:|' || true
)
stale_hits=$(echo "$stale_hits" | grep -v '^$' || true)

if [ -z "$stale_hits" ]; then
    echo "  ✓ no stale third-party-backend names listed as Notepatra backends in current-tense docs"
    PASS=$((PASS + 1))
else
    echo "  ✗ stale third-party-backend names listed as Notepatra backends:"
    echo "$stale_hits" | sed 's/^/      /'
    echo "    Notepatra ships 6 dropdown entries (Ollama, llama.cpp, OpenRouter,"
    echo "    Ollama Cloud, OpenAI, Azure OpenAI). LM Studio / Jan / vLLM /"
    echo "    KoboldCpp / llamafile / text-generation-webui are user-configured"
    echo "    passthroughs via the llama.cpp entry's custom URL, NOT first-class"
    echo "    backends. Anthropic / Claude are reached only via OpenRouter; no"
    echo "    direct entry. Strip these references from current-tense docs."
    FAIL=$((FAIL + 1))
fi

echo
echo "── point-in-time qualifier sweep on legal-defensibility privacy claims ──"
# Per docs/enterprise.html legal-hardening pass: every "no telemetry / no
# data collection / no network calls" claim that's published on the
# public website needs an "as of this release" / "current release" /
# point-in-time qualifier near it, or it creates FTC §5 / CCPA / GDPR
# strict-liability exposure if a future release ever ships any of that.
# Acceptable: "current release does not include telemetry".
# Risky: "Notepatra will never have telemetry."
# This gate runs only against docs/enterprise.html (the legal-defensibility
# page); other pages are marketing copy and can speak more freely.
risky_perpetual_claims=$(
    grep -nE 'Notepatra will never|will never (have|add|include) telemetry|never collects' docs/enterprise.html 2>/dev/null || true
)
if [ -z "$risky_perpetual_claims" ]; then
    echo "  ✓ enterprise.html — no perpetual privacy claims (all qualified with 'current release')"
    PASS=$((PASS + 1))
else
    echo "  ✗ enterprise.html contains perpetual (un-qualified) privacy claims:"
    echo "$risky_perpetual_claims" | sed 's/^/      /'
    echo "    Replace with a point-in-time form: 'The current release does not"
    echo "    include X' instead of 'Notepatra will never include X'."
    FAIL=$((FAIL + 1))
fi

echo
echo "── content-honesty sweep: DuckDB-bundled + macOS inline-charts (v0.1.107) ──"
# Why this exists (the v0.1.107 miss): v0.1.107 changed shipped reality. The
# Full download now BUNDLES the DuckDB v1.1.3 engine on every platform (it
# used to be build-from-source only), and macOS Full is DuckDB-ONLY (Homebrew
# qt@5 dropped QtWebEngine; there is no Apple-Silicon Qt5 WebEngine), so inline
# Vega-Lite charts are a Linux/Windows-only feature. The docs + in-app strings
# were only half-updated and SHIPPED self-contradictory (docs.html said both
# "bundles DuckDB on every platform" and "not bundled in the prebuilt binaries"
# on the same page). stale-text-check only gated counts/versions, so nothing
# caught it — exactly the failure feedback_factual_audit_must_be_a_gate.md
# warned about since v0.1.80. This block FAILS the release if either false-claim
# class reappears in current-tense docs or in user-visible in-app strings.
#
# Matching strategy — LITERAL forbidden phrases (grep -F), NOT a regex that
# tries to span "DuckDB … not bundled" across HTML tags (that silently misses
# <td>/<code>-separated cells, which is why an earlier draft of this gate let
# docs.html:1463/1553 through). Each phrase below only ever appeared in the
# now-false framing. The generic "not bundled with Notepatra" (about third-
# party AI servers) is deliberately excluded — we require the "prebuilt"
# qualifier so legitimate uses don't false-positive.
honesty_fail=0
HONESTY_DOCS="docs/index.html docs/docs.html docs/enterprise.html README.md"

# (1) DuckDB-is-build-from-source / not-bundled — FALSE since v0.1.107.
duckdb_stale_phrases=(
    "optional build-from-source engine"
    "not bundled in the prebuilt"
    "not bundled in prebuilt"
    "Not in prebuilt downloads"
    "not included in prebuilt"
    "which enables DuckDB"
    "which turns DuckDB on"
    "Bundling it into the prebuilt"
)
# (2) macOS inline-charts overclaim — inline Vega is Linux/Windows-only.
# Each requires "Vega-Lite charts" + every/all-platform: the native fenced
# ```chart (QtCharts) renderer IS on every platform but is never called
# "Vega-Lite", so these stay specific to the false WebEngine claim.
macos_overclaim_phrases=(
    "inline Vega-Lite charts on every platform"
    "inline Vega-Lite charts on all platforms"
    "Vega-Lite charts on every platform"
    "Vega-Lite charts on all platforms"
)
for f in $HONESTY_DOCS; do
    # Exclude frozen release-history rows (any line linking releases/tag/v0.1.X).
    filtered=$(grep -nvE 'releases/tag/v0\.1\.' "$f" 2>/dev/null || true)
    for phrase in "${duckdb_stale_phrases[@]}" "${macos_overclaim_phrases[@]}"; do
        hit=$(printf '%s\n' "$filtered" | grep -F -- "$phrase" || true)
        if [ -n "$hit" ]; then
            echo "  ✗ $f: stale claim — \"$phrase\""
            printf '%s\n' "$hit" | sed "s|^|      $f:|"
            honesty_fail=1
        fi
    done
done

# (3) In-app (src/) strings that regressed in v0.1.107 — the sweep's first
# gate draft skipped src/ entirely, so these could regress freely. Scan all
# tracked C++ under src/ (recursive — vega_chart_renderer.cpp lives in
# src/charts/). Each phrase is one we just removed for being false.
src_files=$(git ls-files -- src/ 2>/dev/null | grep -E '\.(cpp|h)$' || true)
src_stale_phrases=(
    "install Charts Pack to enable"
    "Auto-installs on first use"
    "DuckDB is not bundled in this build"
    "NOTEPATRA_WITH_DUCKDB"
)
for phrase in "${src_stale_phrases[@]}"; do
    hit=$(printf '%s\n' "$src_files" | xargs -r grep -nF -- "$phrase" 2>/dev/null || true)
    if [ -n "$hit" ]; then
        echo "  ✗ src/: stale/false in-app string — \"$phrase\""
        printf '%s\n' "$hit" | sed 's|^|      |'
        honesty_fail=1
    fi
done

# Emoji-codepoint icon — the 📊 bar-chart glyph as a UI icon violates the
# no-emoji rule (tofu on Linux without a colour-emoji font; use
# QStyle::standardIcon). Exclude comment lines (leading // or *) so the
# rule-documenting comment in aipanel.cpp doesn't false-positive — we only
# care about the glyph inside a live string literal / label.
emoji_hit=$(printf '%s\n' "$src_files" | xargs -r grep -nF -- "📊" 2>/dev/null \
    | grep -vE '^[^:]+:[0-9]+:[[:space:]]*(//|\*)' || true)
if [ -n "$emoji_hit" ]; then
    echo "  ✗ src/: 📊 emoji used as a UI icon (tofu on Linux; use QStyle::standardIcon)"
    printf '%s\n' "$emoji_hit" | sed 's|^|      |'
    honesty_fail=1
fi

if [ "$honesty_fail" -eq 0 ]; then
    echo "  ✓ no stale DuckDB-not-bundled / macOS-inline-charts claims in docs or in-app strings"
    PASS=$((PASS + 1))
else
    echo "    Ground truth (v0.1.107): the Full download BUNDLES DuckDB v1.1.3 on"
    echo "    EVERY platform; macOS Full is DuckDB-ONLY (no Apple-Silicon Qt5"
    echo "    WebEngine), so inline Vega-Lite charts are Linux/Windows-only. The"
    echo "    native fenced \`\`\`chart (QtCharts) renderer works on every platform."
    FAIL=$((FAIL + 1))
fi

# (4) Positive canary — the detailed docs page MUST always carry the macOS
# caveat. Per the v0.1.107 decision: don't surface it prominently on the main
# marketing site, but docs.html must keep users aware. If a future rewrite of
# the Lite-vs-Full section drops the caveat, fail loudly.
echo
echo "── macOS-DuckDB-only caveat present in detailed docs ──"
assert_contains "docs.html carries the macOS-Full-is-DuckDB-only caveat" \
    docs/docs.html "macOS Full is DuckDB-only"

echo
echo "── test-suite count (Regression Suites stat) ──"
# v0.1.97 — the "N Regression Suites" stat (docs/index.html) + the README
# test-suite count drifted twice (25→22→actual 47) because nothing gated them.
# Source of truth is the CTest-registered count (NOT `ls test_*.cpp` — some
# tests are conditionally registered, so the file glob over-counts). That needs
# a configured build/, so we derive it when build/ is present and skip
# otherwise. release-check.sh always builds notepatra_all_tests first, so this
# gate fires in the real release path. See feedback_release_factual_audit.md.
if command -v ctest >/dev/null 2>&1 && [ -d build ]; then
    suite_count="$(ctest --test-dir build -N 2>/dev/null | grep -cE '^[[:space:]]*Test[[:space:]]+#')"
    if [ "${suite_count:-0}" -gt 0 ]; then
        echo "  ⓘ ctest reports $suite_count registered test suites"
        assert_contains "index.html: Regression Suites stat = $suite_count" \
            docs/index.html "<span>$suite_count</span></div><div class=\"stat-label\">Regression Suites"
        assert_contains "README: test-suite count = $suite_count" \
            README.md "**$suite_count test suites**"
    else
        echo "  ⓘ ctest -N returned no tests (build not configured?) — skipping suite-count gate"
    fi
else
    echo "  ⓘ no build/ dir or ctest unavailable — skipping suite-count gate (release-check builds first)"
fi

echo
if (( FAIL == 0 )); then
    echo "=== ALL SURFACES MATCH ($PASS passed) ==="
    exit 0
else
    echo "=== STALE TEXT DETECTED ($FAIL failed, $PASS passed) ==="
    echo
    echo "To fix:"
    echo "  1. Decide whether the canonical value (top of this script) is the"
    echo "     real one, or the surface text is. Update the loser."
    echo "  2. If a count actually changed in this release, bump the constant"
    echo "     here AND every surface listed above in the same commit."
    echo "  3. For repo description drift: gh repo edit singhpratech/notepatra \\"
    echo "       --description '...new text matching the canonicals...'"
    exit 1
fi
