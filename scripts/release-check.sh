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
#   • notepatra-mcp/Cargo.toml version matches the tag
#   • crates.io already carries the PREVIOUS release (the sidecar publish
#     is manual and silently rotted for 8 releases once already)
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
echo "── install-script artifact selection (lite/full prefix-collision lock) ──"
if bash scripts/test-install-selection.sh >/tmp/install-sel-check.log 2>&1; then
    check "test-install-selection.sh: install.sh download-name == hash-name for every OS/arch/flavor" "true"
else
    check "test-install-selection.sh: install.sh download-name == hash-name for every OS/arch/flavor" "false"
    sed 's/^/    /' /tmp/install-sel-check.log | tail -30 || true
fi

echo
echo "── download-size byte cross-check (vs GitHub release artifacts) ──"
if bash scripts/verify-download-sizes.sh >/tmp/dl-size-check.log 2>&1; then
    check "verify-download-sizes.sh: docs match actual artifact bytes (±0.15 MB)" "true"
    sed 's/^/    /' /tmp/dl-size-check.log | head -12 || true
else
    check "verify-download-sizes.sh: docs match actual artifact bytes (±0.15 MB)" "false"
    sed 's/^/    /' /tmp/dl-size-check.log || true
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
echo "── full-flavor build sanity (WebEngine code path) ──"
# v0.1.65 addition: catch the v0.1.63 silent-failure class of bugs. The
# WebEngine code path in src/charts/vega_chart_renderer.cpp lives behind
# #ifdef NOTEPATRA_WITH_WEBENGINE; it never compiles on dev machines that
# don't have libqt5webengine5-dev installed. v0.1.63's QJsonDocument(QJsonValue)
# bug shipped because no local pre-tag check exercised that path — CI failed
# silently and the GitHub Release never published.
#
# Probe: does pkg-config / cmake's find_package see Qt5WebEngineWidgets?
# If yes, run a throwaway configure + build in build-full/ with
# -DNOTEPATRA_WITH_WEBENGINE=ON. Any compile error fails the gate hard.
# If no, emit a loud warning — CI runners HAVE WebEngine, so a missing
# local install means we're shipping blind.
WEBENGINE_PROBE_LOG=/tmp/release-webengine-probe.log
# Probe by what actually MATTERS — the CMake package + the header — not by a
# dpkg package NAME. The old check looked for "libqt5webengine5-dev", which is
# not the Debian package that ships these headers (it is "qtwebengine5-dev"),
# so it reported "NOT installed" on a machine that had WebEngine all along and
# silently skipped the very build it exists to verify. A checker bug reads
# exactly like the defect it is supposed to catch.
if [ -f /usr/lib/"$(uname -m)"-linux-gnu/cmake/Qt5WebEngineWidgets/Qt5WebEngineWidgetsConfig.cmake ] \
   || dpkg -l qtwebengine5-dev libqt5webenginewidgets5 2>/dev/null | grep -q '^ii' \
   || (echo 'find_package(Qt5 REQUIRED COMPONENTS WebEngineWidgets)' | cmake -DCMAKE_BUILD_TYPE=Release -P /dev/stdin >/dev/null 2>&1); then
    rm -rf build-full && mkdir -p build-full
    if (cd build-full && cmake .. -DCMAKE_BUILD_TYPE=Release -DNOTEPATRA_WITH_WEBENGINE=ON >"$WEBENGINE_PROBE_LOG" 2>&1 \
        && cmake --build . --target notepatra -j "$(nproc 2>/dev/null || echo 4)" >>"$WEBENGINE_PROBE_LOG" 2>&1); then
        check "full-flavor build (WebEngine path) compiles" "true"
    else
        check "full-flavor build (WebEngine path) compiles" "false"
        tail -25 "$WEBENGINE_PROBE_LOG" | sed 's/^/    /'
    fi
else
    echo "  ⚠ Qt5WebEngineWidgets NOT found locally — CANNOT verify the WebEngine"
    echo "    code path compiles. CI runners DO have WebEngine, so a regression in"
    echo "    src/charts/vega_chart_renderer.cpp (or anything else inside"
    echo "    #ifdef NOTEPATRA_WITH_WEBENGINE) will fail CI silently like v0.1.63 did."
    echo "    Install with:   sudo apt-get install qtwebengine5-dev libqt5webenginewidgets5"
    echo "    (note: the dev headers come from qtwebengine5-dev, NOT libqt5webengine5-dev)"
    # Don't fail the gate — devs without WebEngine should still be able to cut
    # releases, but the warning makes the risk visible.
fi

echo
echo "── rust quality + audit ──"
if [ -d rust-core ]; then
    # cargo clippy --all-features -D warnings  (matches the rust-quality CI gate)
    # Catches lints locally before CI does — see feedback_ci_clippy_newer_than_local.md
    if (cd rust-core && cargo clippy --all-targets --all-features -- -D warnings) >/dev/null 2>&1; then
        echo "  ✓ cargo clippy clean (rust-core)"
        PASSED=$((PASSED + 1))
    else
        echo "  ✗ cargo clippy: warnings present (re-run inside rust-core/ to see them)"
        FAILED+=("cargo clippy")
    fi
    if (cd rust-core && cargo fmt --check) >/dev/null 2>&1; then
        echo "  ✓ cargo fmt clean"
        PASSED=$((PASSED + 1))
    else
        echo "  ✗ cargo fmt: drift present (run \`cd rust-core && cargo fmt\`)"
        FAILED+=("cargo fmt")
    fi
    # cargo test — the rust-core unit suites (matches the rust-quality CI gate)
    if (cd rust-core && cargo test --release) >/dev/null 2>&1; then
        echo "  ✓ cargo test clean (rust-core unit suites)"
        PASSED=$((PASSED + 1))
    else
        echo "  ✗ cargo test: failures present (re-run inside rust-core/ to see them)"
        FAILED+=("cargo test")
    fi
    # cargo audit — flags CVEs in resolved transitive deps. We install on demand
    # so the script remains usable on machines without cargo-audit pre-installed.
    if ! command -v cargo-audit >/dev/null 2>&1; then
        echo "  ⚠ cargo-audit not installed; attempting one-off install..."
        cargo install --quiet --locked cargo-audit 2>/dev/null || true
    fi
    if command -v cargo-audit >/dev/null 2>&1; then
        if (cd rust-core && cargo audit --deny warnings) >/dev/null 2>&1; then
            echo "  ✓ cargo audit clean (no advisories in resolved deps)"
            PASSED=$((PASSED + 1))
        else
            echo "  ✗ cargo audit: advisories present (re-run inside rust-core/ to see them)"
            FAILED+=("cargo audit")
        fi
    else
        echo "  ⚠ cargo-audit unavailable — skipping CVE check"
    fi
fi

# cargo test — the notepatra-mcp sidecar suites (protocol + socket bridge),
# mirroring the rust-core gate above. Runs only where the sidecar exists.
if [ -d notepatra-mcp ]; then
    if (cd notepatra-mcp && cargo test --release) >/dev/null 2>&1; then
        echo "  ✓ cargo test clean (notepatra-mcp sidecar suites)"
        PASSED=$((PASSED + 1))
    else
        echo "  ✗ cargo test: failures present (re-run inside notepatra-mcp/ to see them)"
        FAILED+=("cargo test (notepatra-mcp)")
    fi

    # Cross-compile CHECK of the test targets for Windows.
    #
    # tests/pipe_bridge.rs is `#![cfg(windows)]` — the named-pipe twin of
    # socket_bridge.rs — so it never compiles on Linux and never compiles in
    # `cargo test` here. v0.1.126 changed a transport signature, updated the
    # unix twin, and shipped a call site in the windows twin that could not
    # build: the Windows CI job failed at 8 minutes on a one-argument
    # read_tab. A local suite that is green because a file was never read is
    # not evidence about that file.
    #
    # `cargo check --tests` for the Windows target compiles it in ~1s and
    # reproduces the exact rustc error. Soft-skip when the target is absent
    # (`rustup target add x86_64-pc-windows-gnu`) — this is a gate for the
    # release machine, not a reason to block a contributor who lacks it.
    if rustup target list --installed 2>/dev/null | grep -q x86_64-pc-windows-gnu; then
        if (cd notepatra-mcp && cargo check --tests --target x86_64-pc-windows-gnu) \
             >/dev/null 2>&1; then
            echo "  ✓ cargo check --tests (windows target): cfg(windows) tests still compile"
            PASSED=$((PASSED + 1))
        else
            echo "  ✗ cargo check --tests --target x86_64-pc-windows-gnu: FAILS"
            echo "      cfg(windows)-only tests (tests/pipe_bridge.rs) do not compile."
            echo "      Re-run inside notepatra-mcp/ to see the error."
            FAILED+=("windows-target cargo check (notepatra-mcp)")
        fi
    else
        echo "  ⚠ x86_64-pc-windows-gnu target not installed — cfg(windows) tests unchecked"
        echo "      rustup target add x86_64-pc-windows-gnu"
    fi
fi

echo
echo "── crates.io ──"
# The sidecar is published to crates.io as `notepatra-mcp`. Nothing in CI does
# this, so it is a manual step — and a manual step that is not a gate is a step
# that stops happening. It did: 0.1.118 and 0.1.119 went up on 2026-07-18 and
# then the channel rotted for EIGHT releases, leaving `cargo install
# notepatra-mcp` serving 0.1.119 — the one release whose Windows named-pipe
# transport deadlocked on every verb and had never completed a tool call.
#
# The invariant that would have caught it on the very next release: by the time
# you cut vN, the PREVIOUS tag must already be on crates.io. Checking for the
# version being cut would be wrong — that one is published after the tag.
prev_tag="$(git describe --tags --abbrev=0 2>/dev/null || true)"
prev_ver="${prev_tag#v}"
crate_ver="$(grep -m1 -oE '^version = "[0-9]+\.[0-9]+\.[0-9]+"' notepatra-mcp/Cargo.toml \
             | grep -oE '[0-9]+\.[0-9]+\.[0-9]+' || true)"

check "notepatra-mcp/Cargo.toml version matches the tag being cut" \
      "[ \"$crate_ver\" = \"$VERSION\" ]"

if [ -z "$prev_ver" ] || [ "$prev_ver" = "$VERSION" ]; then
    echo "  ⚠ no earlier tag to compare against — crates.io currency unchecked"
elif published="$(curl -fsSL --max-time 15 -A notepatra-release-check \
                    https://crates.io/api/v1/crates/notepatra-mcp 2>/dev/null \
                  | grep -oE '"max_version":"[0-9]+\.[0-9]+\.[0-9]+"' \
                  | grep -oE '[0-9]+\.[0-9]+\.[0-9]+')" && [ -n "$published" ]; then
    # Hard-fail: a stale registry ships a known-broken sidecar to anyone
    # running `cargo install`, and nothing else in the pipeline notices.
    check "crates.io is current (published $published = previous tag $prev_ver)" \
          "[ \"$published\" = \"$prev_ver\" ]"
    if [ "$published" != "$prev_ver" ]; then
        echo "      crates.io serves $published but the last release was $prev_ver."
        echo "      cd notepatra-mcp && cargo publish   # then re-run this script"
    fi
else
    # Soft-warn: offline or crates.io down is not a reason to block a release.
    echo "  ⚠ could not reach crates.io — published sidecar version unverified"
fi
echo
echo "  → after tagging: cd notepatra-mcp && cargo publish   (publishes $VERSION)"

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
