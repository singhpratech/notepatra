# Fallback plan: ship v0.1.1 with Linux + Mac binaries only

This is the **escape hatch** if Windows CI cannot be made green by morning.

## When to use this

Only if Claude has tried at least 4 distinct fixes and Windows is still failing AND there's a clear root-cause writeup in this folder explaining what's left to try.

## What gets shipped

- ✅ `notepatra-linux-x64.tar.gz` (1.8 MB) — verified working, 21/21 lexers passing
- ✅ `notepatra-macos-arm64.dmg` (22 MB) — verified working with embedded icon
- ❌ `notepatra-windows-x64.zip` — NOT shipped, marked "build from source" on the website

## What changes in the workflow

1. Make `build-windows` use `continue-on-error: true` so it doesn't block release
2. Drop `build-windows` from the `release` job's `needs:` list
3. Skip the Windows artifact in the release upload step

## What changes on the website

1. The download grid:
   - Linux x64 → ✅ active button
   - macOS Apple Silicon → ✅ active button
   - **Windows x64** → 🟡 "Build from source — v0.1.2 coming"
2. The orange "known issues in v0.1.0" warning box stays in the download section, updated to:
   ```
   v0.1.1 ships with Linux x64 + macOS Apple Silicon binaries.
   Windows users: build from source today; pre-built Windows binary
   coming in v0.1.2 once the windeployqt + QScintilla packaging is sorted.
   ```

## What changes in the README

Add a "Windows status" section near the top:

```markdown
## Windows status (v0.1.1)

Pre-built Windows binaries are temporarily not shipped while we work
through a `windeployqt` + QScintilla packaging issue. Windows users
have two options:

1. **Build from source** — instructions in the [Install](#install)
   section. ~10 minutes start to finish on Visual Studio 2022.
2. **Wait for v0.1.2** — see [issue #1](https://github.com/singhpratech/notepatra/issues/1)
   for the current debugging status. Subscribe to the issue to get
   notified when v0.1.2 ships.
```

## What I tell users on Show HN / Reddit / etc

Be honest:

> Windows users: v0.1.1 ships with Linux + Mac pre-built binaries. The
> Windows binary is temporarily build-from-source while I work through
> a windeployqt packaging issue (Qt's deployment tool doesn't recursively
> scan the QScintilla DLL, so platforms\qwindows.dll never gets bundled,
> so the exe crashes before main() runs). Tracking in issue #1.
> v0.1.2 with the Windows binary is coming once that's solved.

## How to apply this fallback (one commit)

```sh
# Edit .github/workflows/build.yml:
# 1. Add continue-on-error: true to build-windows
# 2. Remove build-windows from release needs:
# 3. Remove the Windows artifact from the release files: list

# Edit docs/index.html:
# 1. Update the warning banner text
# 2. Change the Windows download card to "build from source"

# Then:
git add .github/workflows/build.yml docs/index.html
git commit -m "Ship v0.1.1 with Linux + Mac only; Windows build still in progress"
git push origin main
git tag -a v0.1.1 -m "v0.1.1 — Linux + Mac binaries; Windows still in progress (issue #1)"
git push origin v0.1.1
```

## Why this is OK

- Linux + Mac users get the lexer and icon fixes IMMEDIATELY
- Windows users with v0.1.0 are no worse off than they are right now
  (the v0.1.0 Windows binary already has the bugs, but it does run)
- The `windeployqt` issue is well-documented in commit history and
  in issue #1 with the actual error log
- Anyone who wants to help can read the issue and submit a PR
- Better than blocking the entire v0.1.1 release indefinitely on
  one platform's packaging quirk

## When Windows IS fixed later

Tag a v0.1.2 with all 3 platforms. Run the IndexNow ping. Update the
Versions section on the website. Done.

The fallback is not a defeat — it's the right call if Windows turns
out to need something I can't do tonight (e.g. a runner config change
that needs a UI action from the user).
