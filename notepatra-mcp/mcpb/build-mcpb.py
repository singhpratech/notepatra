#!/usr/bin/env python3
"""Assemble notepatra-mcp.mcpb — an MCP Bundle (zip) Claude Desktop installs one-click.

Layout inside the zip:
    manifest.json
    server/<platform>/notepatra-mcp[.exe]     platform ∈ {linux-x64, linux-arm64, darwin-arm64, win32-x64}

Local smoke run (linux binary as stand-in, no --require-all):
    python3 notepatra-mcp/mcpb/build-mcpb.py \
      --manifest notepatra-mcp/mcpb/manifest.json \
      --cargo-toml notepatra-mcp/Cargo.toml \
      --binary linux-x64=notepatra-mcp/target/release/notepatra-mcp \
      --out /tmp/.../notepatra-mcp.mcpb
CI passes all four platforms plus --require-all and --expect-version.
"""
import argparse, json, re, sys, zipfile

ALL_PLATFORMS = ("linux-x64", "linux-arm64", "darwin-arm64", "win32-x64")

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

    with zipfile.ZipFile(a.out, "w", zipfile.ZIP_DEFLATED) as z:
        z.write(a.manifest, "manifest.json")
        for plat, path in sorted(binaries.items()):
            exe = "notepatra-mcp.exe" if plat == "win32-x64" else "notepatra-mcp"
            info = zipfile.ZipInfo(f"server/{plat}/{exe}")
            info.external_attr = 0o755 << 16          # keep the unix exec bit
            info.compress_type = zipfile.ZIP_DEFLATED
            with open(path, "rb") as f: z.writestr(info, f.read())

    with zipfile.ZipFile(a.out) as z:                 # self-check: reopen + reparse
        bad = z.testzip()
        if bad: fail(f"corrupt entry in bundle: {bad}")
        json.loads(z.read("manifest.json"))
        names = z.namelist()
    print(f"OK: {a.out} — version {mver}, {len(binaries)} platform binaries")
    for n in names: print(f"   {n}")

if __name__ == "__main__":
    main()
