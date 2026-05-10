#!/usr/bin/env bash
# post-release-verify.sh — confirm a GitHub Release actually published
# after `git push origin v<version>` and CI ran.
#
# Catches the v0.1.63 / v0.1.64 silent-failure class: tag is pushed,
# CI fails for a reason we missed, the release step is skipped, and we
# never notice that no Release ever showed up on the Releases page.
#
# Usage:
#   bash scripts/post-release-verify.sh               # auto-detects version
#   VERSION=0.1.65 bash scripts/post-release-verify.sh
#   bash scripts/post-release-verify.sh --timeout 1800   # 30 min max
#
# Polls every 30 seconds. Reports which artifacts have landed and which
# are missing. Exits non-zero if any expected artifact never appears
# before the timeout — so you find out the release failed within 30 min
# of tagging, not the next day when a user reports a 404.

set -euo pipefail

cd "$(dirname "$0")/.."

cmake_version="$(grep -oE 'project\(Notepatra VERSION [0-9]+\.[0-9]+\.[0-9]+' CMakeLists.txt | grep -oE '[0-9]+\.[0-9]+\.[0-9]+')"
VERSION="${VERSION:-$cmake_version}"
TAG="v$VERSION"

TIMEOUT_S=1800   # 30 min — conservative for slow Windows runs
POLL_INTERVAL_S=30

while [[ $# -gt 0 ]]; do
    case "$1" in
        --timeout) TIMEOUT_S="$2"; shift 2 ;;
        --poll)    POLL_INTERVAL_S="$2"; shift 2 ;;
        *)         echo "unknown flag: $1" >&2; exit 2 ;;
    esac
done

# Expected artifacts. The lite tarballs always ship; macOS / Windows
# bundles ship; the -full flavor artifacts ship from v0.1.65 onward
# (Linux only for now — macOS/Win full ship in v0.1.66+).
REQUIRED_ARTIFACTS=(
    "notepatra-linux-x64.tar.gz"
    "notepatra-linux-arm64.tar.gz"
    "notepatra-macos-arm64.dmg"
    "notepatra-windows-x64.zip"
    "SHA256SUMS"
)

# Optional (warn-only if missing — these are v0.1.65+ surfaces).
OPTIONAL_ARTIFACTS=(
    "notepatra-linux-x64-full.tar.gz"
    "notepatra-linux-arm64-full.tar.gz"
    "notepatra-setup-${VERSION}.exe"
    "notepatra-${VERSION}.msi"
)

echo "=== post-release-verify: $TAG ==="
echo "    timeout: ${TIMEOUT_S}s · poll: every ${POLL_INTERVAL_S}s"
echo

START=$(date +%s)
LAST_SEEN=""

while true; do
    NOW=$(date +%s)
    ELAPSED=$((NOW - START))

    # gh release view returns non-zero if the release doesn't exist yet
    # (CI hasn't completed). Tolerate that until timeout.
    if release_json=$(gh release view "$TAG" --json assets,tagName,publishedAt 2>/dev/null); then
        ASSETS=$(echo "$release_json" | jq -r '.assets[].name' | sort)

        MISSING_REQUIRED=()
        for art in "${REQUIRED_ARTIFACTS[@]}"; do
            if ! echo "$ASSETS" | grep -qx "$art"; then
                MISSING_REQUIRED+=("$art")
            fi
        done

        MISSING_OPTIONAL=()
        for art in "${OPTIONAL_ARTIFACTS[@]}"; do
            if ! echo "$ASSETS" | grep -qFx "$art"; then
                MISSING_OPTIONAL+=("$art")
            fi
        done

        # Status snapshot. Only re-print when state changes so the
        # output isn't a wall of identical polls.
        FP="found=${#ASSETS} missing=${#MISSING_REQUIRED[@]}"
        if [[ "$FP" != "$LAST_SEEN" ]]; then
            echo "[+${ELAPSED}s] Release exists. $(echo "$ASSETS" | wc -l) assets so far."
            LAST_SEEN="$FP"
        fi

        if [[ ${#MISSING_REQUIRED[@]} -eq 0 ]]; then
            echo
            echo "✓ All required artifacts present:"
            for art in "${REQUIRED_ARTIFACTS[@]}"; do
                printf "    %s\n" "$art"
            done
            if [[ ${#MISSING_OPTIONAL[@]} -gt 0 ]]; then
                echo
                echo "ℹ Optional artifacts still missing (v0.1.65+ surfaces or platform-specific):"
                for art in "${MISSING_OPTIONAL[@]}"; do
                    printf "    %s\n" "$art"
                done
            fi
            echo
            echo "=== $TAG is LIVE — ${ELAPSED}s after polling started ==="
            exit 0
        fi
    else
        if [[ "$LAST_SEEN" != "no-release-yet" ]]; then
            echo "[+${ELAPSED}s] Release for $TAG does not exist yet — CI still running?"
            LAST_SEEN="no-release-yet"
        fi
    fi

    if (( ELAPSED >= TIMEOUT_S )); then
        echo
        echo "✗ TIMEOUT after ${TIMEOUT_S}s — release did NOT publish completely."
        if [[ -n "${MISSING_REQUIRED+x}" && ${#MISSING_REQUIRED[@]} -gt 0 ]]; then
            echo
            echo "Missing required artifacts:"
            for art in "${MISSING_REQUIRED[@]}"; do
                printf "    %s\n" "$art"
            done
        fi
        echo
        echo "Check CI status with:  gh run list --limit 6"
        echo "Inspect failed jobs:   gh run view <id> --log-failed | head -50"
        exit 1
    fi

    sleep "$POLL_INTERVAL_S"
done
