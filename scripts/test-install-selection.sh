#!/usr/bin/env bash
# test-install-selection.sh — offline regression lock for docs/install.sh's
# artifact selection. No network: uses embedded fixtures.
#
# Bug class it guards (v0.1.111 macОS report): install.sh chose the download
# URL and the expected SHA-256 with two INDEPENDENT substring matches + `head
# -1`, over differently-ordered lists. Because "notepatra-macos-arm64" is a
# prefix of "notepatra-macos-arm64-full", the URL match (GitHub API order:
# full first) picked the FULL dmg while the SHA match (SHA256SUMS order: lite
# first) picked the LITE hash — a false "tampered" error on a genuine release.
#
# This test reproduces that exact ordering and asserts the CURRENT install.sh
# matchers resolve download-name == hash-name == one real asset, for every
# OS/arch/flavor. It also asserts the fixture genuinely reproduces the old bug
# (so the regression stays real). Keep the two matcher lines below in sync with
# docs/install.sh.
set -u
pass=0; fail=0
ok()   { echo "  PASS  $1"; pass=$((pass+1)); }
bad()  { echo "  FAIL  $1"; fail=$((fail+1)); }

WORK=$(mktemp -d)
trap 'rm -rf "$WORK"' EXIT
REL="$WORK/rel.json"
SUMS="$WORK/SHA256SUMS"
TAG="v9.9.9"
DLBASE="https://github.com/singhpratech/notepatra/releases/download/$TAG"

# Fixture: release-assets JSON. Order mirrors the real bug trigger —
# every "-full" browser_download_url is listed BEFORE its lite sibling, and
# .pem/.sig sidecars are interleaved (they must never be selected).
{
  for base in notepatra-linux-x64 notepatra-linux-arm64; do
    for nm in "${base}-full.tar.gz" "${base}-full.tar.gz.pem" "${base}-full.tar.gz.sig" \
              "${base}.tar.gz" "${base}.tar.gz.pem" "${base}.tar.gz.sig"; do
      echo "    {\"name\": \"$nm\", \"browser_download_url\": \"$DLBASE/$nm\"},"
    done
  done
  for nm in notepatra-macos-arm64-full.dmg notepatra-macos-arm64-full.dmg.pem notepatra-macos-arm64-full.dmg.sig \
            notepatra-macos-arm64.dmg notepatra-macos-arm64.dmg.pem notepatra-macos-arm64.dmg.sig; do
    echo "    {\"name\": \"$nm\", \"browser_download_url\": \"$DLBASE/$nm\"},"
  done
} > "$REL"

# Fixture: SHA256SUMS. Lite listed BEFORE full (real ordering). Distinct
# deterministic 64-hex hashes so a wrong pick is detectable.
hashfor() { printf '%064d' "$1"; }   # "...0001", "...0002", distinct & 64 chars
{
  echo "$(hashfor 1)  notepatra-linux-x64.tar.gz"
  echo "$(hashfor 2)  notepatra-linux-x64-full.tar.gz"
  echo "$(hashfor 3)  notepatra-linux-arm64.tar.gz"
  echo "$(hashfor 4)  notepatra-linux-arm64-full.tar.gz"
  echo "$(hashfor 5)  notepatra-macos-arm64.dmg"
  echo "$(hashfor 6)  notepatra-macos-arm64-full.dmg"
} > "$SUMS"

# ── Selection under test — MUST mirror docs/install.sh ──────────────────────
sel_url()  { grep -F "/$1\"" "$REL" | head -1 | cut -d '"' -f4; }                       # download URL for ASSET
sel_hash() { awk -v f="$1" '{ n=$2; sub(/^\*/,"",n); if (n==f) print $1 }' "$SUMS"; }   # expected hash for ASSET
asset_of() { # OS ARCH FULL -> ASSET
  local FLAVOR="" BASE="" EXT=""
  [ "$3" = "1" ] && FLAVOR="-full"
  case "$1" in
    linux) EXT="tar.gz"; [ "$2" = x86_64 ] && BASE="notepatra-linux-x64" || BASE="notepatra-linux-arm64" ;;
    darwin) EXT="dmg"; BASE="notepatra-macos-arm64" ;;
  esac
  echo "${BASE}${FLAVOR}.${EXT}"
}

echo "=== install.sh selection: download-name == hash-name == one real asset ==="
for spec in "linux x86_64 0" "linux x86_64 1" "linux arm64 0" "linux arm64 1" "darwin arm64 0" "darwin arm64 1"; do
  set -- $spec
  ASSET=$(asset_of "$1" "$2" "$3")
  URL=$(sel_url "$ASSET"); H=$(sel_hash "$ASSET")
  back=$(awk -v h="$H" '{ n=$2; sub(/^\*/,"",n); if ($1==h) print n }' "$SUMS")
  if [ "$(basename "$URL")" = "$ASSET" ] && [ -n "$H" ] && [ "$back" = "$ASSET" ]; then
    ok "$1/$2 full=$3 -> $ASSET"
  else
    bad "$1/$2 full=$3 -> dl=$(basename "$URL") hash->$back (want $ASSET)"
  fi
done

echo ""
echo "=== fixture honesty: the OLD naive matcher MUST desync on darwin/arm64 ==="
# Old logic: substring grep + head -1, independently on each list.
old_url=$(grep "browser_download_url.*notepatra-macos-arm64" "$REL" | head -1 | cut -d '"' -f4)
old_hash=$(grep "notepatra-macos-arm64" "$SUMS" | awk '{print $1}' | head -1)
old_hash_name=$(awk -v h="$old_hash" '{print ($1==h)?$2:""}' "$SUMS" | grep . | head -1)
if [ "$(basename "$old_url")" != "$old_hash_name" ]; then
  ok "old matcher desyncs (dl=$(basename "$old_url") vs hash=$old_hash_name) — fixture reproduces the bug"
else
  bad "fixture no longer reproduces the bug (old matcher agreed) — regression value lost"
fi

echo ""
echo "=================================================="
echo "  PASS=$pass  FAIL=$fail"
if [ "$fail" = "0" ]; then echo "  ✅ install-selection regression lock GREEN"; else echo "  ❌ FAIL"; fi
echo "=================================================="
exit "$fail"
