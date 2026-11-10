#!/usr/bin/env bash
# test-install-selection.sh — offline regression lock for docs/install.sh.
#
# It runs the REAL docs/install.sh (no mirrored copy of its logic) under `sh`,
# the way `curl ... | sh` users run it, inside a throwaway $HOME, with stub
# `curl`, `uname`, `sudo`, `hdiutil`, `ldd` and desktop-cache tools first on
# PATH. The curl stub serves fixture release JSON, SHA256SUMS and assets from
# a temp dir, so nothing touches the network or the real home directory.
#
# Bug classes it guards:
#  - v0.1.111 macOS report: the download URL and the expected SHA-256 were
#    picked by two INDEPENDENT substring matches over differently-ordered
#    lists. "notepatra-macos-arm64" is a prefix of "notepatra-macos-arm64-full",
#    so the URL match (API order: full first) took the FULL asset while the
#    SHA match (SHA256SUMS order: lite first) took the LITE hash: a false
#    "tampered" error on a genuine release. The fixtures reproduce that order
#    and every case asserts the downloaded file verified against its own hash.
#  - Linux Full: the Full tarball holds notepatra + libduckdb.so, and the
#    binary finds the library via RUNPATH $ORIGIN. Installing only the binary
#    gave an install that could not start. Full cases assert both files land
#    together in ~/.local/opt/notepatra with ~/.local/bin/notepatra linked to it.
#  - Failure paths must exit non-zero with a true message (Intel Mac, GitHub
#    API rate limit, missing asset, unreachable SHA256SUMS, missing libraries).
#
# INSTALL_SH=/path/to/install.sh overrides the script under test (used to
# prove the test goes red against older behaviour).
set -u
cd "$(dirname "$0")/.."
INSTALL_SH="${INSTALL_SH:-$PWD/docs/install.sh}"
case "$INSTALL_SH" in /*) ;; *) INSTALL_SH="$PWD/$INSTALL_SH" ;; esac

pass=0; fail=0
ok()   { echo "  PASS  $1"; pass=$((pass+1)); }
bad()  { echo "  FAIL  $1"; fail=$((fail+1)); }

WORK=$(mktemp -d)
trap 'rm -rf "$WORK"' EXIT
FIX="$WORK/fixtures"; STUB="$WORK/stub"
mkdir -p "$FIX/assets" "$STUB"
TAG="v9.9.9"
DLBASE="https://github.com/singhpratech/notepatra/releases/download/$TAG"

# ── Fixture assets ──────────────────────────────────────────────────────────
# Linux tarballs carry a fake `notepatra` that names its own asset; the Full
# ones also carry libduckdb.so and a NEEDS_DUCKDB marker the ldd stub reads.
for base in notepatra-linux-x64 notepatra-linux-arm64; do
  for fl in "" "-full"; do
    a="${base}${fl}.tar.gz"; d="$WORK/build/$a"; mkdir -p "$d"
    { echo '#!/bin/sh'; echo "echo fixture $a"; [ -n "$fl" ] && echo '# NEEDS_DUCKDB'; } > "$d/notepatra"
    chmod 755 "$d/notepatra"
    [ -n "$fl" ] && echo "fake duckdb for $a" > "$d/libduckdb.so"
    tar czf "$FIX/assets/$a" -C "$d" .
  done
done
for a in notepatra-macos-arm64.dmg notepatra-macos-arm64-full.dmg; do
  echo "fake dmg $a" > "$FIX/assets/$a"
done

# SHA256SUMS: lite listed BEFORE full (real ordering), real hashes.
( cd "$FIX/assets" && for a in notepatra-linux-x64.tar.gz notepatra-linux-x64-full.tar.gz \
    notepatra-linux-arm64.tar.gz notepatra-linux-arm64-full.tar.gz \
    notepatra-macos-arm64.dmg notepatra-macos-arm64-full.dmg; do sha256sum "$a"; done ) > "$FIX/SHA256SUMS"

# Release JSON, one field per line like api.github.com. Every "-full" asset is
# listed BEFORE its lite sibling and .pem/.sig sidecars are interleaved.
{
  echo '{'
  echo "  \"tag_name\": \"$TAG\","
  echo '  "assets": ['
  for base in notepatra-linux-x64.tar.gz notepatra-linux-arm64.tar.gz notepatra-macos-arm64.dmg; do
    stem="${base%%.*}"; ext="${base#*.}"
    for nm in "${stem}-full.${ext}" "${stem}-full.${ext}.pem" "${stem}-full.${ext}.sig" \
              "${base}" "${base}.pem" "${base}.sig"; do
      echo '    {'
      echo "      \"name\": \"$nm\","
      echo "      \"browser_download_url\": \"$DLBASE/$nm\""
      echo '    },'
    done
  done
  echo '    {"name": "end"}'
  echo '  ]'
  echo '}'
} > "$FIX/release.json"

# ── Stubs ───────────────────────────────────────────────────────────────────
# curl: serve fixtures by URL; log each URL. API_MODE=ok|ratelimit|down,
# SUMS_MODE=ok|missing.
cat > "$STUB/curl" <<'EOF'
#!/bin/sh
out=""; url=""
while [ $# -gt 0 ]; do
  case "$1" in
    -o) out="$2"; shift ;;
    --proto|-H|-X|-d|-w) shift ;;
    http*) url="$1" ;;
  esac
  shift
done
echo "$url" >> "$FIX/curl.log"
emit() { if [ -n "$out" ]; then cat "$1" > "$out"; else cat "$1"; fi; }
case "$url" in
  https://api.github.com/*/releases/latest)
    case "${API_MODE:-ok}" in
      ok) emit "$FIX/release.json" ;;
      ratelimit) printf '{\n  "message": "API rate limit exceeded for 203.0.113.7.",\n  "documentation_url": "https://docs.github.com/rest"\n}\n' ;;
      *) exit 6 ;;
    esac ;;
  */SHA256SUMS)
    [ "${SUMS_MODE:-ok}" = ok ] || exit 22
    emit "$FIX/SHA256SUMS" ;;
  *)
    f="$FIX/assets/${url##*/}"
    [ -f "$f" ] || exit 22
    emit "$f" ;;
esac
EOF
cat > "$STUB/uname" <<'EOF'
#!/bin/sh
case "$1" in -s) echo "$FAKE_OS" ;; -m) echo "$FAKE_ARCH" ;; *) echo "$FAKE_OS" ;; esac
EOF
# ldd: emulate the loader's $ORIGIN lookup for the fixture binaries — a Full
# binary resolves libduckdb.so only from the directory of the path given.
cat > "$STUB/ldd" <<'EOF'
#!/bin/sh
[ -n "${FAKE_LDD_MISSING:-}" ] && echo "	$FAKE_LDD_MISSING => not found"
if grep -q NEEDS_DUCKDB "$1" 2>/dev/null; then
  d=$(dirname "$1")
  if [ -f "$d/libduckdb.so" ]; then echo "	libduckdb.so => $d/libduckdb.so (0x0)"
  else echo "	libduckdb.so => not found"; fi
fi
exit 0
EOF
for t in sudo hdiutil; do printf '#!/bin/sh\nexit 1\n' > "$STUB/$t"; done
for t in gtk-update-icon-cache update-desktop-database cosign; do printf '#!/bin/sh\nexit 0\n' > "$STUB/$t"; done
# cosign stub is never reached for verification: the curl stub 404s .sig/.pem,
# so install.sh takes its documented "SHA-only" path on any machine.
chmod +x "$STUB"/*

# run_install NAME OS ARCH FULL [VAR=VALUE...]  -> $WORK/NAME/{home,out,rc}
# Reuses $WORK/NAME/home if it already exists (upgrade/transition cases).
run_install() {
  local name="$1" os="$2" arch="$3" full="$4"; shift 4
  local dir="$WORK/$name"; mkdir -p "$dir/home"
  : > "$FIX/curl.log"
  ( cd "$dir" && env "$@" HOME="$dir/home" PATH="$STUB:/usr/bin:/bin" FIX="$FIX" \
      FAKE_OS="$os" FAKE_ARCH="$arch" NOTEPATRA_FULL="$full" \
      sh "$INSTALL_SH" ) > "$dir/out" 2>&1 < /dev/null
  echo $? > "$dir/rc"
  cp "$FIX/curl.log" "$dir/curl.log"
}
rc_of()   { cat "$WORK/$1/rc"; }
out_has() { grep -qF -- "$2" "$WORK/$1/out"; }
# Basename of the asset the script downloaded (not the API, sums or icons).
downloaded() { grep -F "$DLBASE/" "$WORK/$1/curl.log" | grep -vE '/SHA256SUMS$|\.(sig|pem)$' | head -1 | sed 's|.*/||'; }
show_tail() { sed 's/^/        | /' "$WORK/$1/out" | tail -8; }

asset_name() { # OS ARCH FULL -> expected asset
  local fl=""; [ "$3" = 1 ] && fl="-full"
  case "$1/$2" in
    linux/x86_64) echo "notepatra-linux-x64$fl.tar.gz" ;;
    linux/aarch64) echo "notepatra-linux-arm64$fl.tar.gz" ;;
    darwin/arm64) echo "notepatra-macos-arm64$fl.dmg" ;;
  esac
}

# Full layout check for a home dir: opt dir holds both files, bin is a
# symlink whose REAL directory has libduckdb.so beside the binary.
full_layout_ok() {
  local h="$1" asset="$2" real
  [ -L "$h/.local/bin/notepatra" ] || return 1
  real=$(readlink -f "$h/.local/bin/notepatra") || return 1
  [ "$real" = "$h/.local/opt/notepatra/notepatra" ] || return 1
  [ -f "$h/.local/opt/notepatra/libduckdb.so" ] || return 1
  grep -qF "fixture $asset" "$real"
}
lite_layout_ok() {
  local h="$1" asset="$2"
  [ -f "$h/.local/bin/notepatra" ] && [ ! -L "$h/.local/bin/notepatra" ] || return 1
  [ ! -e "$h/.local/opt/notepatra" ] || return 1
  grep -qF "fixture $asset" "$h/.local/bin/notepatra"
}

echo "=== real docs/install.sh: download-name == hash-name == installed asset ==="
echo "    (script under test: ${INSTALL_SH#$PWD/})"
for spec in "linux x86_64 0" "linux x86_64 1" "linux aarch64 0" "linux aarch64 1" "darwin arm64 0" "darwin arm64 1"; do
  set -- $spec
  name="sel-$1-$2-$3"; want=$(asset_name "$1" "$2" "$3")
  run_install "$name" "$1" "$2" "$3"
  got=$(downloaded "$name")
  if [ "$got" != "$want" ]; then
    bad "$1/$2 full=$3: downloaded '${got:-nothing}', want $want"; show_tail "$name"; continue
  fi
  if ! out_has "$name" "SHA-256 verified" || out_has "$name" "mismatch"; then
    bad "$1/$2 full=$3: $want did not verify against its own SHA256SUMS line"; show_tail "$name"; continue
  fi
  h="$WORK/$name/home"
  case "$1/$3" in
    linux/0)
      if [ "$(rc_of "$name")" = 0 ] && lite_layout_ok "$h" "$want"; then ok "$1/$2 lite -> $want installed as a plain file"
      else bad "$1/$2 lite: rc=$(rc_of "$name"), layout wrong"; show_tail "$name"; fi ;;
    linux/1)
      if [ "$(rc_of "$name")" = 0 ] && full_layout_ok "$h" "$want"; then ok "$1/$2 full -> notepatra + libduckdb.so together in ~/.local/opt/notepatra, bin symlinked"
      else bad "$1/$2 full: rc=$(rc_of "$name"); binary and libduckdb.so are not installed together"; show_tail "$name"; ls -laR "$h/.local" 2>/dev/null | sed 's/^/        | /' | head -20; fi ;;
    darwin/*)
      # hdiutil is stubbed to fail: selection + verification is what's under test.
      ok "$1/$2 full=$3 -> $want downloaded and verified" ;;
  esac
done

echo ""
echo "=== Linux flavor transitions (same \$HOME) ==="
run_install trans-fl linux x86_64 1
run_install trans-fl linux x86_64 0
h="$WORK/trans-fl/home"
if [ "$(rc_of trans-fl)" = 0 ] && lite_layout_ok "$h" notepatra-linux-x64.tar.gz; then
  ok "Full then Lite: symlink replaced by a real file, old opt dir removed"
else
  bad "Full then Lite: rc=$(rc_of trans-fl)"; show_tail trans-fl; ls -la "$h/.local/bin" "$h/.local/opt" 2>&1 | sed 's/^/        | /'
fi
run_install trans-lf linux x86_64 0
run_install trans-lf linux x86_64 1
h="$WORK/trans-lf/home"
if [ "$(rc_of trans-lf)" = 0 ] && full_layout_ok "$h" notepatra-linux-x64-full.tar.gz; then
  ok "Lite then Full: plain file replaced by the symlink into ~/.local/opt/notepatra"
else
  bad "Lite then Full: rc=$(rc_of trans-lf)"; show_tail trans-lf
fi
run_install trans-ff linux x86_64 1
echo stale > "$WORK/trans-ff/home/.local/opt/notepatra/stale-file"
run_install trans-ff linux x86_64 1
h="$WORK/trans-ff/home"
if [ "$(rc_of trans-ff)" = 0 ] && full_layout_ok "$h" notepatra-linux-x64-full.tar.gz \
   && [ ! -e "$h/.local/opt/notepatra/stale-file" ] && [ ! -e "$h/.local/opt/notepatra.new" ] && [ ! -e "$h/.local/opt/notepatra.old" ]; then
  ok "Full then Full: opt dir replaced wholesale, no .new/.old left behind"
else
  bad "Full then Full: rc=$(rc_of trans-ff)"; show_tail trans-ff
fi

echo ""
echo "=== failure paths exit non-zero and say what happened ==="
expect_fail() { # NAME MESSAGE LABEL
  if [ "$(rc_of "$1")" != 0 ] && out_has "$1" "$2"; then ok "$3"
  else bad "$3 (rc=$(rc_of "$1"), want non-zero and \"$2\")"; show_tail "$1"; fi
}
run_install f-intel darwin x86_64 0
expect_fail f-intel "Intel Macs" "Intel Mac: no pre-built asset -> exit 1"
run_install f-rate linux x86_64 0 API_MODE=ratelimit
expect_fail f-rate "did not return the latest release" "GitHub API rate limit -> exit 1"
out_has f-rate "API rate limit exceeded" && ok "rate limit: GitHub's message is shown" || bad "rate limit: GitHub's message not shown"
run_install f-down linux x86_64 0 API_MODE=down
expect_fail f-down "Could not reach the GitHub API" "GitHub API unreachable -> exit 1"
# asset missing: point the script at a name the fixture release does not have
sed 's/notepatra-linux-x64\.tar\.gz/notepatra-linux-x64-gone.tar.gz/' "$FIX/release.json" > "$FIX/release.gone.json"
cp "$FIX/release.json" "$FIX/release.keep.json"; cp "$FIX/release.gone.json" "$FIX/release.json"
run_install f-asset linux x86_64 0
cp "$FIX/release.keep.json" "$FIX/release.json"
expect_fail f-asset "has no asset named notepatra-linux-x64.tar.gz" "release without the asset -> exit 1"
run_install f-sums linux x86_64 0 SUMS_MODE=missing
expect_fail f-sums "Refusing to install an unverified binary" "SHA256SUMS unreachable -> refuse, exit 1"
[ ! -e "$WORK/f-sums/home/.local/bin/notepatra" ] && ok "SHA256SUMS unreachable: nothing installed" || bad "SHA256SUMS unreachable: a binary was installed anyway"
run_install f-ldd linux x86_64 0 FAKE_LDD_MISSING=libQt5Charts.so.5
expect_fail f-ldd "libQt5Charts.so.5" "missing shared library -> listed, exit 1"
out_has f-ldd "Installed to" && bad "missing shared library: success banner still printed" || ok "missing shared library: no success banner"

echo ""
echo "=== fixture honesty: the OLD naive matcher MUST desync on darwin/arm64 ==="
# Old logic: substring grep + head -1, independently on each list.
old_url=$(grep "browser_download_url.*notepatra-macos-arm64" "$FIX/release.json" | head -1 | cut -d '"' -f4)
old_hash=$(grep "notepatra-macos-arm64" "$FIX/SHA256SUMS" | awk '{print $1}' | head -1)
old_hash_name=$(awk -v h="$old_hash" '$1==h {print $2}' "$FIX/SHA256SUMS" | head -1)
if [ "$(basename "$old_url")" != "$old_hash_name" ]; then
  ok "old matcher desyncs (dl=$(basename "$old_url") vs hash=$old_hash_name) — fixture reproduces the bug"
else
  bad "fixture no longer reproduces the bug (old matcher agreed) — regression value lost"
fi

echo ""
echo "=================================================="
echo "  PASS=$pass  FAIL=$fail"
if [ "$fail" = "0" ]; then echo "  ✅ install.sh regression lock GREEN"; else echo "  ❌ FAIL"; fi
echo "=================================================="
exit "$fail"
