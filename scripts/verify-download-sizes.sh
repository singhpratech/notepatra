#!/usr/bin/env bash
# verify-download-sizes.sh — cross-check website/README download-size claims
# against ACTUAL bytes in the published GitHub release artifacts.
#
# Catches the v0.1.85 bug class: docs/index.html / docs/docs.html / README.md
# narrative copy carried 3.6 / 28.7 / 42.5 MB while the actual artifacts were
# 3.45 / 26.86 / 40.56 MB. stale-text-check.sh and np-sweep-versions bump
# VERSION STRINGS but never reconcile BYTE COUNTS.
#
# Strategy: gather all "X.YZ MB" claims from the three docs into one set.
# For each release artifact, find the closest claim. The closest claim must
# be within ±0.15 MB of the actual artifact size (covers nearest-0.1 rounding).
#
# This approach is order-insensitive and works even when several variants
# (e.g. all three Windows installers) share a line in the docs.

set -euo pipefail

cd "$(dirname "$0")/.."

cmake_version="$(grep -oE 'project\(Notepatra VERSION [0-9]+\.[0-9]+\.[0-9]+' CMakeLists.txt | grep -oE '[0-9]+\.[0-9]+\.[0-9]+')"
VERSION="${VERSION:-$cmake_version}"
TAG="v$VERSION"
TOL="0.15"

if ! command -v gh >/dev/null 2>&1; then
    echo "  ⚠ gh CLI not available — skipping download-size cross-check"
    exit 0
fi

if ! gh release view "$TAG" --json assets >/dev/null 2>&1; then
    echo "  ⓘ release $TAG not yet published — skipping — the release job re-runs this after upload and opens an issue on drift"
    exit 0
fi

echo "── download-size cross-check ($TAG) ──"

# Map: artifact filename → size in MB
declare -A ACTUAL_MB
while IFS=$'\t' read -r name size; do
    [[ -z "$name" ]] && continue
    [[ "$name" =~ \.(pem|sig)$ ]] && continue
    [[ "$name" == "SHA256SUMS" ]] && continue
    ACTUAL_MB["$name"]=$(awk "BEGIN{printf \"%.2f\", $size/1048576}")
done < <(gh release view "$TAG" --json assets --jq '.assets[] | "\(.name)\t\(.size)"')

# Collect every distinct "X.YZ MB" claim across the three pages
mapfile -t SITE_CLAIMS < <(
    grep -hoE "[0-9]+\.[0-9]+ MB" docs/index.html docs/docs.html README.md 2>/dev/null \
        | grep -oE "[0-9]+\.[0-9]+" | sort -u
)

# For diagnostics, also gather integer-only "X MB" claims (those are usually
# round-number marketing like "9 MB", "30 MB" — we don't byte-compare these)
mapfile -t SITE_CLAIMS_INT < <(
    grep -hoE "[0-9]+ MB" docs/index.html docs/docs.html README.md 2>/dev/null \
        | grep -oE "[0-9]+" | sort -un
)

check_artifact() {
    local artifact_glob="$1"; local label="$2"
    local matched=""
    for n in "${!ACTUAL_MB[@]}"; do
        # shellcheck disable=SC2053
        if [[ "$n" == $artifact_glob ]]; then matched="$n"; break; fi
    done
    if [[ -z "$matched" ]]; then
        echo "    ⓘ $label: no artifact matched glob '$artifact_glob' — skipping"
        return 0
    fi
    local actual="${ACTUAL_MB[$matched]}"
    # Find closest decimal claim
    local closest=""
    local closest_diff="999"
    for c in "${SITE_CLAIMS[@]}"; do
        local d
        d=$(awk -v c="$c" -v a="$actual" 'BEGIN{d=c-a; if(d<0)d=-d; printf "%.2f", d}')
        if awk -v d="$d" -v cd="$closest_diff" 'BEGIN{exit !(d<cd)}'; then
            closest_diff="$d"; closest="$c"
        fi
    done
    if [[ -z "$closest" ]]; then
        echo "    ⚠ $label: no decimal MB claim anywhere on the three pages (?)"
        return 1
    fi
    # Within tolerance?
    if awk -v d="$closest_diff" -v t="$TOL" 'BEGIN{exit !(d<=t)}'; then
        echo "    ✓ $label ($matched = $actual MB, site closest = $closest MB, drift $closest_diff)"
        return 0
    else
        echo "    ✗ $label ($matched = $actual MB, site closest = $closest MB, drift $closest_diff > $TOL)"
        return 1
    fi
}

failed=0
for spec in \
    "notepatra-linux-x64.tar.gz|Linux x64 tar.gz" \
    "notepatra-linux-arm64.tar.gz|Linux ARM64 tar.gz" \
    "notepatra-macos-arm64.dmg|macOS DMG" \
    "notepatra-[0-9]*.msi|Windows MSI" \
    "notepatra-setup-*[0-9].exe|Windows NSIS .exe" \
    "notepatra-windows-x64.zip|Windows portable .zip"
do
    glob="${spec%%|*}"; label="${spec##*|}"
    check_artifact "$glob" "$label" || failed=$((failed + 1))
done

# Bare-binary claim verification — the "~12 MB bare (12.2 MB on Linux x64)"
# class went stale in v0.1.114 when two fix waves grew the binary past the
# "under 12 MB" wording. Measure the SHIPPED binary, not the local build.
echo
echo "── bare-binary claim (Linux x64 tarball) ──"
BARE_TMP=$(mktemp -d)
trap 'rm -rf "$BARE_TMP"' EXIT
if gh release download "v$VERSION" \
        --pattern "notepatra-linux-x64.tar.gz" --dir "$BARE_TMP" 2>/dev/null \
   && tar xzf "$BARE_TMP/notepatra-linux-x64.tar.gz" -C "$BARE_TMP" 2>/dev/null; then
    BARE_BIN=$(find "$BARE_TMP" -name notepatra -type f | head -1)
    if [[ -n "$BARE_BIN" ]]; then
        bare_mb=$(awk -v b="$(stat -c%s "$BARE_BIN")" 'BEGIN{printf "%.1f", b/1048576}')
        # The docs claim one decimal value for the Linux x64 bare binary.
        claim=$(grep -hoE "[0-9]+\.[0-9]+ MB on Linux x64" docs/index.html README.md \
                    | grep -oE "^[0-9]+\.[0-9]+" | sort -u | head -1)
        if [[ -z "$claim" ]]; then
            echo "    ⓘ no 'X.Y MB on Linux x64' bare claim found — skipping"
        elif awk -v c="$claim" -v a="$bare_mb" -v t="$TOL" \
                 'BEGIN{d=c-a; if(d<0)d=-d; exit !(d<=t)}'; then
            echo "    ✓ bare binary $bare_mb MB matches claim $claim MB"
        else
            echo "    ✗ bare binary $bare_mb MB vs claimed $claim MB (> $TOL MB drift)"
            failed=$((failed + 1))
        fi
    else
        echo "    ⓘ binary not found in tarball — skipping"
    fi
else
    echo "    ⓘ tarball download failed — skipping"
fi

# Stripped-binary claim verification — separate check, separate failure mode
echo
echo "── stripped-binary claim ──"
# Only trigger on binary-size context: "X MB stripped", "bare stripped",
# "stripped binary", or "stripped executable". Not the UI-strip / XML-tag-strip
# mentions which have a different meaning. And ignore forensic release-notes
# rows (lines that begin with the `| [**v0.1.X**](...)` table syntax).
if grep -hE '([0-9]+(\.[0-9]+)? MB stripped|bare stripped|stripped binary|stripped executable|stripped on (each|every) platform)' \
        docs/index.html docs/docs.html README.md 2>/dev/null \
        | grep -v '^| \[\*\*v0\.1\.' >/dev/null; then
    linux_tar=""
    for n in "${!ACTUAL_MB[@]}"; do
        if [[ "$n" == "notepatra-linux-x64.tar.gz" ]]; then linux_tar="$n"; break; fi
    done
    if [[ -n "$linux_tar" ]]; then
        tmpdir=$(mktemp -d)
        if gh release download "$TAG" -p "$linux_tar" -D "$tmpdir" >/dev/null 2>&1; then
            tar -xzf "$tmpdir/$linux_tar" -C "$tmpdir"
            bin="$tmpdir/notepatra"
            if [[ -f "$bin" ]]; then
                if file "$bin" | grep -q ", stripped$"; then
                    echo "    ✓ shipped Linux x64 binary IS stripped"
                else
                    echo "    ✗ shipped Linux x64 binary is NOT stripped — site says 'stripped'"
                    echo "      Fix: add 'strip --strip-all build/notepatra' to .github/workflows/build.yml"
                    echo "      (drops binary from ~9.7 MB → ~7 MB), OR remove 'stripped' from docs."
                    failed=$((failed + 1))
                fi
            fi
        fi
        rm -rf "$tmpdir"
    fi
else
    echo "    ⓘ docs don't claim 'stripped' anywhere — skipping verification"
fi

if (( failed > 0 )); then
    echo
    echo "  ✗ $failed size-claim mismatch(es) — bring docs in line with actual artifact bytes"
    exit 1
fi
exit 0
