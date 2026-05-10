#!/usr/bin/env bash
# release-check.sh — preflight before `git tag -a vX.Y.Z`.
#
# Catches every release-day friction we've hit so far:
#   • CMakeLists VERSION matches the tag you're about to cut
#   • release_notes/v<version>.md exists
#   • CHANGELOG.md has a [<version>] entry
#   • README.md latest-version row mentions the tag
#   • docs/index.html stat card / hero / sticky CTA mention the tag
#   • All 22+ test executables build (notepatra_all_tests target)
#   • ctest passes
#   • git working tree is clean
#   • No vendored binary (vendor/, build/, etc.) is staged
#
# Usage:
#   bash scripts/release-check.sh        # checks the version in CMakeLists.txt
#   VERSION=0.1.58 bash scripts/release-check.sh   # explicit override
#
# Exits non-zero if anything is wrong, with a one-line description per failure.

set -euo pipefail

cd "$(dirname "$0")/.."
ROOT="$(pwd)"
FAILED=()
PASSED=0

cmake_version="$(grep -oE 'project\(Notepatra VERSION [0-9]+\.[0-9]+\.[0-9]+' CMakeLists.txt | grep -oE '[0-9]+\.[0-9]+\.[0-9]+')"
VERSION="${VERSION:-$cmake_version}"
TAG="v$VERSION"

check() {
    local label="$1"; local cond="$2"
    if eval "$cond"; then
        printf "  ✓ %s\n" "$label"
        PASSED=$((PASSED + 1))
    else
        printf "  ✗ %s\n" "$label"
        FAILED+=("$label")
    fi
}

echo "=== Release preflight: $TAG ==="
echo

echo "── version sync ──"
check "CMakeLists.txt VERSION = $VERSION" \
    "[[ '$cmake_version' == '$VERSION' ]]"
check "release_notes/$TAG.md exists" \
    "[[ -f 'release_notes/$TAG.md' ]]"
check "CHANGELOG.md has [$VERSION] entry" \
    "grep -q '^## \[$VERSION\]' CHANGELOG.md"
check "README.md mentions $TAG (releases table)" \
    "grep -q '$TAG' README.md"
check "docs/index.html mentions $TAG (release card)" \
    "grep -q '$TAG' docs/index.html"
check "docs/docs.html mentions $TAG" \
    "grep -q '$TAG' docs/docs.html"

echo
echo "── stale-text audit (feature counts) ──"
if bash scripts/stale-text-check.sh >/tmp/stale-text-check.log 2>&1; then
    check "stale-text-check.sh: every surface matches canonical counts" "true"
else
    check "stale-text-check.sh: every surface matches canonical counts" "false"
    sed -n 's/^/    /p' /tmp/stale-text-check.log | tail -40
fi

echo
echo "── working tree ──"
check "git working tree clean (no uncommitted changes)" \
    "[[ -z \$(git status --porcelain) ]]"
check "vendor/ not tracked" \
    "! git ls-files --error-unmatch vendor/ >/dev/null 2>&1"
check "build/ not tracked" \
    "! git ls-files --error-unmatch build/ >/dev/null 2>&1"

echo
echo "── tests ──"
if [[ ! -d build ]]; then
    echo "  (configuring build/ for the first time …)"
    mkdir -p build
    (cd build && cmake .. -DBUILD_TESTING=ON >/dev/null)
fi

(cd build && cmake --build . --target notepatra_all_tests -j "$(nproc 2>/dev/null || sysctl -n hw.ncpu 2>/dev/null || echo 4)" >/tmp/release-build.log 2>&1) \
    && check "notepatra_all_tests builds (every test_* executable)" "true" \
    || { check "notepatra_all_tests builds" "false"; tail -20 /tmp/release-build.log | sed 's/^/    /'; }

(cd build && QT_QPA_PLATFORM=offscreen ctest --output-on-failure >/tmp/release-ctest.log 2>&1) \
    && check "ctest: every registered test passes" "true" \
    || { check "ctest passes" "false"; tail -20 /tmp/release-ctest.log | sed 's/^/    /'; }

echo
echo "── tag ──"
if git rev-parse "$TAG" >/dev/null 2>&1; then
    echo "  ⚠ tag $TAG already exists locally"
    if git ls-remote --tags origin "$TAG" 2>/dev/null | grep -q "$TAG"; then
        echo "    and on origin — you are about to FORCE-MOVE a published tag"
    fi
else
    echo "  ✓ tag $TAG not yet created — clean cut"
fi

echo
if (( ${#FAILED[@]} == 0 )); then
    echo "=== ALL CHECKS PASSED ($PASSED checks) ==="
    echo "Ready to cut: git tag -a $TAG -m \"<title>\" && git push origin main $TAG"
    exit 0
else
    echo "=== ${#FAILED[@]} CHECK(S) FAILED — fix before tagging ==="
    for f in "${FAILED[@]}"; do echo "  ✗ $f"; done
    exit 1
fi
