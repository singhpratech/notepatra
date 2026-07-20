#!/usr/bin/env python3
"""Assemble notepatra-mcp.mcpb — an MCP Bundle (zip) Claude Desktop installs one-click.

Layout inside the zip:
    manifest.json
    server/linux/run.sh                       arch-picking launcher (see below)
    server/<platform>/notepatra-mcp[.exe]     platform ∈ {linux-x64, linux-arm64, darwin, win32-x64}

Why 'darwin' and not 'darwin-arm64': MCPB platform_overrides key on process.platform
only — there is no arch selector — so one darwin entry must serve both Macs. CI lipos
the arm64+x86_64 slices into a single universal binary and passes it as `--binary
darwin=<path>`; it lands at server/darwin/notepatra-mcp whatever the source basename.
Linux has the same problem with the opposite fix: two separate ELF binaries stay
packed and server/linux/run.sh picks between them at exec time via uname -m.
Intel-macOS coverage therefore exists ONLY as the universal binary's x86_64 slice —
no native Intel build is produced or tested here.

Local smoke run (linux binary as stand-in, no --require-all):
    python3 notepatra-mcp/mcpb/build-mcpb.py \
      --manifest notepatra-mcp/mcpb/manifest.json \
      --cargo-toml notepatra-mcp/Cargo.toml \
      --binary linux-x64=notepatra-mcp/target/release/notepatra-mcp \
      --out /tmp/.../notepatra-mcp.mcpb
CI passes all four platforms plus --require-all and --expect-version.
"""
import argparse, json, os, re, sys, zipfile

# 'darwin' REPLACES the old 'darwin-arm64' key on purpose: a stale CI invocation
# passing --binary darwin-arm64=... now dies on the unknown-platform fail() below
# rather than quietly producing a bundle no Intel Mac can run.
ALL_PLATFORMS = ("linux-x64", "linux-arm64", "darwin", "win32-x64")

def fail(msg):
    print(f"ERROR: {msg}", file=sys.stderr); sys.exit(1)

def main():
    p = argparse.ArgumentParser()
    p.add_argument("--manifest", required=True)
    p.add_argument("--cargo-toml", required=True)
    p.add_argument("--expect-version")
    p.add_argument("--require-all", action="store_true")
    p.add_argument("--binary", action="append", default=[], metavar="PLATFORM=PATH")
    p.add_argument("--out", required=True)
    a = p.parse_args()

    manifest = json.load(open(a.manifest))          # dies loudly on invalid JSON
    for key in ("manifest_version", "name", "version", "description", "author", "server"):
        if key not in manifest: fail(f"manifest.json missing required field '{key}'")
    mver = manifest["version"]

    cargo = open(a.cargo_toml).read()
    m = re.search(r'^version\s*=\s*"([0-9]+\.[0-9]+\.[0-9]+)"', cargo, re.M)
    if not m: fail("could not parse version from Cargo.toml")
    if m.group(1) != mver: fail(f"version skew: manifest {mver} != Cargo.toml {m.group(1)}")
    if a.expect_version and a.expect_version != mver:
        fail(f"version skew: manifest {mver} != release tag {a.expect_version}")

    binaries = {}
    for spec in a.binary:
        plat, _, path = spec.partition("=")
        if plat not in ALL_PLATFORMS: fail(f"unknown platform '{plat}' (expected one of {ALL_PLATFORMS})")
        binaries[plat] = path
    if not binaries: fail("no --binary given")
    if a.require_all:
        missing = [pl for pl in ALL_PLATFORMS if pl not in binaries]
        if missing: fail(f"--require-all set but platforms missing: {missing}")

    # The launcher ships in EVERY bundle, even partial local ones — the manifest's
    # linux command points at it unconditionally, so a bundle without it is broken.
    launcher = os.path.join(os.path.dirname(a.manifest), "run-linux.sh")
    if not os.path.isfile(launcher): fail(f"launcher not found: {launcher}")
    launcher_bytes = open(launcher, "rb").read()
    # A CRLF anywhere means the checkout mangled line endings; the kernel would then
    # look for an interpreter literally named "/bin/sh\r" and exec would fail.
    if b"\r" in launcher_bytes: fail(f"{launcher} has CRLF line endings (must be LF)")

    with zipfile.ZipFile(a.out, "w", zipfile.ZIP_DEFLATED) as z:
        z.write(a.manifest, "manifest.json")
        linfo = zipfile.ZipInfo("server/linux/run.sh")
        linfo.external_attr = 0o755 << 16             # must stay executable to be exec'd
        linfo.compress_type = zipfile.ZIP_DEFLATED
        z.writestr(linfo, launcher_bytes)
        for plat, path in sorted(binaries.items()):
            exe = "notepatra-mcp.exe" if plat == "win32-x64" else "notepatra-mcp"
            # Renames on the way in: the darwin universal may arrive as e.g.
            # notepatra-mcp-universal and still lands at server/darwin/notepatra-mcp.
            info = zipfile.ZipInfo(f"server/{plat}/{exe}")
            info.external_attr = 0o755 << 16          # keep the unix exec bit
            info.compress_type = zipfile.ZIP_DEFLATED
            with open(path, "rb") as f: z.writestr(info, f.read())

    with zipfile.ZipFile(a.out) as z:                 # self-check: reopen + reparse
        bad = z.testzip()
        if bad: fail(f"corrupt entry in bundle: {bad}")
        packed = json.loads(z.read("manifest.json"))
        names = z.namelist()
    # Pin manifest↔builder agreement forever: the paths the host will exec must be
    # paths this builder actually wrote. Drift here is invisible until install time.
    if "server/linux/run.sh" not in names: fail("bundle is missing server/linux/run.sh")
    cfg = packed.get("server", {}).get("mcp_config", {})
    cmd = cfg.get("command", "")
    if not cmd.endswith("server/linux/run.sh"):
        fail(f"manifest command '{cmd}' does not point at server/linux/run.sh")
    dcmd = cfg.get("platform_overrides", {}).get("darwin", {}).get("command", "")
    if not dcmd.endswith("server/darwin/notepatra-mcp"):
        fail(f"manifest darwin override '{dcmd}' does not point at server/darwin/notepatra-mcp")
    if a.require_all:
        for plat in ALL_PLATFORMS:
            if not any(n.startswith(f"server/{plat}/") for n in names):
                fail(f"--require-all set but bundle has no server/{plat}/ entry")
    print(f"OK: {a.out} — version {mver}, {len(binaries)} platform binaries")
    for n in names: print(f"   {n}")

if __name__ == "__main__":
    main()
