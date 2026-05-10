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
