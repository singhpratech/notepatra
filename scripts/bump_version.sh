#!/usr/bin/env bash
# bump_version.sh — single source of truth for version bumps.
#
# Updates every file in the repo that hard-codes a version string so a
# release never goes out with stale README / website / hero badge / etc.
# Run this once before tagging:
#
#   ./scripts/bump_version.sh 0.1.5
#   git diff
#   git add -A && git commit -m "bump to v0.1.5"
#   git tag -a v0.1.5 -m "v0.1.5" && git push origin main v0.1.5
#
# Files touched:
#   docs/index.html         — JSON-LD softwareVersion, hero badge, "Download v…" label
#   README.md               — "Latest release: v…" link, version table top row
#   CHANGELOG.md            — adds new ## [X.Y.Z] — date stub at the top
#   release_notes/<v>.md    — creates blank stub if missing
#
# This script is conservative: it never overwrites release notes that already
# exist, and it never reorders the version history table — it only PREPENDS
# the new row.

set -euo pipefail

if [ -z "${1:-}" ]; then
    echo "Usage: $0 <new-version>    (e.g. 0.1.5)" >&2
    exit 2
fi

NEW="$1"
TAG="v$NEW"
TODAY=$(date -u +%Y-%m-%d)
REPO_ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$REPO_ROOT"

# Discover the current "latest" tag from CHANGELOG so we know what we're replacing
OLD=$(grep -m1 -oE '^## \[[0-9]+\.[0-9]+\.[0-9]+\]' CHANGELOG.md | tr -d '[]' | awk '{print $2}')
if [ -z "$OLD" ]; then
    echo "::error:: could not detect current version from CHANGELOG.md" >&2
    exit 1
fi

if [ "$OLD" = "$NEW" ]; then
    echo "Already at $NEW — nothing to do."
    exit 0
fi

echo "Bumping $OLD → $NEW (date: $TODAY)"

# ─── CMakeLists.txt — single source of truth for the C++ binary version ─
# main.cpp picks this up via NOTEPATRA_VERSION compile-time define, so
# --version / --help / app.applicationVersion() all auto-update.
sed -i "s|^project(Notepatra VERSION [0-9]\+\.[0-9]\+\.[0-9]\+|project(Notepatra VERSION $NEW|" CMakeLists.txt

# ─── docs/index.html ─────────────────────────────────────────────────
sed -i "s|\"softwareVersion\": \"$OLD\"|\"softwareVersion\": \"$NEW\"|" docs/index.html
sed -i "s|hero-badge\">v$OLD|hero-badge\">v$NEW|"                       docs/index.html
sed -i "s|Download v$OLD|Download v$NEW|"                                docs/index.html

# ─── README.md ───────────────────────────────────────────────────────
sed -i "s|Latest release: v$OLD|Latest release: v$NEW|"                  README.md

# Prepend a new row to the version-history table in README so the table
# never goes stale. Match the SPECIFIC version-history table by looking for
# the `| Version | Date | Highlights |` header row first, THEN insert after
# the next `|---|---|---|` separator. Without this guard the script would
# insert into the keyboard shortcuts table instead (also has 3 columns).
if ! grep -q "tag/v$NEW" README.md; then
    awk -v new="$NEW" -v today="$TODAY" '
        /^\| Version \| Date \| Highlights \|/ { in_version_table=1 }
        /^\|---\|---\|---\|/ && in_version_table && !inserted {
            print
            print "| [**v" new "**](https://github.com/singhpratech/notepatra/releases/tag/v" new ") | " today " | TODO: short description of this release. |"
            inserted=1
            in_version_table=0
            next
        }
        { print }
    ' README.md > README.md.tmp && mv README.md.tmp README.md
fi

# ─── CHANGELOG.md — prepend a new section header (don't touch existing) ─
if ! grep -q "^## \[$NEW\]" CHANGELOG.md; then
    awk -v new="$NEW" -v today="$TODAY" '
        /^---$/ && !inserted {
            print
            print ""
            print "## [" new "] — " today
            print ""
            print "### Added"
            print "- TODO"
            print ""
            print "### Fixed"
            print "- TODO"
            print ""
            print "### Verifying this release"
            print "Same as previous — SHA-256, cosign, SLSA. See SECURITY.md."
            print ""
            inserted=1
            next
        }
        { print }
    ' CHANGELOG.md > CHANGELOG.md.tmp && mv CHANGELOG.md.tmp CHANGELOG.md
fi

# ─── release_notes/<tag>.md — create blank stub if absent ─────────────
NOTES="release_notes/$TAG.md"
if [ ! -f "$NOTES" ]; then
    cat > "$NOTES" <<EOF
# Notepatra $TAG

TODO: short summary of what's in this release.

## Fixed

TODO

## Added

TODO

## Verifying this release

Same as previous releases — SHA-256 in \`SHA256SUMS\`, cosign keyless signatures, SLSA build provenance. See \`SECURITY.md\`.

## Downloads

| Platform | Asset |
|---|---|
| Linux x64 | \`notepatra-linux-x64.tar.gz\` |
| macOS Apple Silicon | \`notepatra-macos-arm64.dmg\` |
| Windows x64 (installer) | \`notepatra-setup-$NEW.exe\` |
| Windows x64 (portable) | \`notepatra-windows-x64.zip\` |
EOF
fi

echo
echo "✓ Bumped to $NEW. Files changed:"
git status -s docs/index.html README.md CHANGELOG.md "$NOTES" 2>/dev/null || true
echo
echo "Next steps:"
echo "  1. Edit CHANGELOG.md and $NOTES with the actual changes"
echo "  2. git add -A && git commit -m \"bump to $TAG\""
echo "  3. git tag -a $TAG -m \"$TAG\""
echo "  4. git push origin main $TAG"
