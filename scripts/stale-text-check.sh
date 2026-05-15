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
LEXER_COUNT=92
FILE_EXT_COUNT=226
BACKEND_COUNT=6
BACKEND_LIST="Ollama / llama.cpp / OpenRouter / Ollama Cloud / OpenAI / Azure OpenAI"
BARE_BIN_MB="~9 MB"

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
    all_count=$(grep -oE "$any_re" "$file" 2>/dev/null | wc -l)
    current_count=$(grep -oE "$current_re" "$file" 2>/dev/null | wc -l)
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
    docs/index.html '92 language lexers as of v__V__:'
check_phrase "index.html body lead 'As of v…: 226 file types · 92'" \
    docs/index.html 'As of v__V__: 226 file types · 92'
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
