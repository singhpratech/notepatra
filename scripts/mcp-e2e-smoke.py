#!/usr/bin/env python3
"""End-to-end MCP smoke test: the REAL editor driven by the REAL sidecar.

Every other MCP test proves one half. The C++ suite drives a fake sidecar; the
Rust suite drives a fake bridge. Neither proves the two halves actually meet on
the platform they are running on — and that is exactly where v0.1.120's two
worst bugs lived:

  * macOS: the sidecar computed the socket path as $TMPDIR/<name> while Qt binds
    under NSTemporaryDirectory(), so it could never find a running editor. Both
    unit suites passed the whole time.
  * Windows: the named-pipe transport deadlocked on the first verb AFTER the
    greeting. A smoke test that only connects would still have passed.

So this drives a genuine round trip, per platform, in CI. It is deliberately
small and dependency-free (stdlib only, no pytest) so it can run identically on
Linux, macOS and Windows runners.

Usage:  python3 scripts/mcp-e2e-smoke.py <editor-binary> <sidecar-binary>
Exit 0 = the halves meet and the approval gate holds. Non-zero = they do not.
"""
import json
import os
import shutil
import subprocess
import sys
import tempfile
import threading
import time

TIMEOUT = 30
# Hard ceiling for the whole run. A healthy pass takes a few seconds.
WATCHDOG_SECONDS = 300


def fail(msg):
    print(f"FAIL: {msg}")
    sys.exit(1)


def arm_watchdog():
    """Turn a HANG into a loud failure.

    This is the whole point of the test on Windows: the v0.1.120 deadlock froze
    the sidecar on the first verb after the greeting. A bare readline() waiting
    on a deadlocked peer blocks FOREVER, so without this the smoke test would
    hang the job for hours and report nothing — strictly worse than failing,
    because a hang reads as "still running". os._exit so a blocked reader thread
    cannot keep the process alive.
    """
    def blow_up():
        sys.stderr.write(
            f"\nFAIL: e2e smoke exceeded {WATCHDOG_SECONDS}s — the sidecar or "
            f"editor is HUNG (this is what a transport deadlock looks like).\n")
        sys.stderr.flush()
        os._exit(124)
    t = threading.Timer(WATCHDOG_SECONDS, blow_up)
    t.daemon = True
    t.start()
    return t


def main():
    arm_watchdog()
    if len(sys.argv) != 3:
        fail("usage: mcp-e2e-smoke.py <editor-binary> <sidecar-binary>")
    editor, sidecar = sys.argv[1], sys.argv[2]
    for p in (editor, sidecar):
        if not os.path.exists(p):
            fail(f"binary not found: {p}")

    home = tempfile.mkdtemp(prefix="np-e2e-")
    env = dict(os.environ)
    # Isolate config/state so the runner's real profile is untouched and the
    # editor is guaranteed to be its own primary instance.
    env["HOME"] = home
    env["USERPROFILE"] = home           # Windows equivalent of HOME
    env["APPDATA"] = os.path.join(home, "AppData", "Roaming")
    env["XDG_CONFIG_HOME"] = os.path.join(home, ".config")
    env["QT_QPA_PLATFORM"] = "offscreen"
    for var in ("WAYLAND_DISPLAY", "DISPLAY"):
        env.pop(var, None)

    doc = os.path.join(home, "smoke.py")
    with open(doc, "w", encoding="utf-8") as f:
        f.write("def greet(name):\n    return name\n")

    ed = subprocess.Popen([editor, doc], env=env,
                          stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    proc = None
    try:
        # 1. The editor must PUBLISH its bound endpoint. This is the macOS fix:
        #    the sidecar reads the real path instead of guessing $TMPDIR.
        if sys.platform == "win32":
            cfg = os.path.join(env["APPDATA"], "Notepatra")
        elif sys.platform == "darwin":
            cfg = os.path.join(home, "Library", "Application Support", "Notepatra")
        else:
            cfg = os.path.join(env["XDG_CONFIG_HOME"], "notepatra")
        endpoint = os.path.join(cfg, "mcp-endpoint.json")

        deadline = time.time() + TIMEOUT
        while time.time() < deadline and not os.path.exists(endpoint):
            if ed.poll() is not None:
                fail(f"editor exited early (rc={ed.returncode}) without publishing")
            time.sleep(0.25)
        if not os.path.exists(endpoint):
            fail(f"editor never published {endpoint}")
        rec = json.load(open(endpoint, encoding="utf-8"))
        want_kind = "named_pipe" if sys.platform == "win32" else "unix_socket"
        if rec.get("kind") != want_kind:
            fail(f"endpoint kind is {rec.get('kind')!r}, expected {want_kind!r}")
        print(f"  ok  endpoint published: {rec['kind']} pid={rec['pid']}")

        # 2. The sidecar must REACH that editor and complete a round trip. A
        #    greeting alone is not enough — the Windows deadlock passed that.
        proc = subprocess.Popen([sidecar, "--socket"], env=env, text=True,
                                bufsize=1, stdin=subprocess.PIPE,
                                stdout=subprocess.PIPE, stderr=subprocess.DEVNULL)
        seq = [0]

        def call(method, params=None):
            seq[0] += 1
            proc.stdin.write(json.dumps({"jsonrpc": "2.0", "id": seq[0],
                                         "method": method,
                                         "params": params or {}}) + "\n")
            proc.stdin.flush()
            line = proc.stdout.readline()
            if not line:
                fail(f"sidecar closed the stream during {method}")
            return json.loads(line)

        def tool(name, args=None):
            r = call("tools/call", {"name": name, "arguments": args or {}})
            res = r.get("result", {})
            body = res.get("content", [{}])[0].get("text", "")
            return res.get("isError", True), body

        call("initialize", {"protocolVersion": "2025-06-18", "capabilities": {},
                            "clientInfo": {"name": "e2e-smoke", "version": "1"}})
        tools = call("tools/list").get("result", {}).get("tools", [])
        print(f"  ok  tools/list -> {len(tools)} tools")

        # A READ verb: proves a client-initiated request completes. This is the
        # exact step the Windows deadlock blocked.
        err, body = tool("app_info")
        if err or "Notepatra" not in body:
            fail(f"app_info did not round-trip: {body[:200]}")
        print("  ok  app_info round-tripped (client-initiated request completes)")

        err, body = tool("list_open_tabs")
        if err or "smoke.py" not in body:
            fail(f"list_open_tabs did not see the open file: {body[:200]}")
        print("  ok  list_open_tabs sees the opened document")

        # 3. The approval gate must HOLD. Nothing can click Approve in offscreen,
        #    so a write MUST be refused and the file MUST be untouched. A bypass
        #    would be the single worst regression this project could ship.
        before = open(doc, encoding="utf-8").read()
        seq[0] += 1
        proc.stdin.write(json.dumps({
            "jsonrpc": "2.0", "id": seq[0], "method": "tools/call",
            "params": {"name": "insert_text",
                       "arguments": {"tab_index": 1,
                                     "text": "# INJECTED\n"}}}) + "\n")
        proc.stdin.flush()
        # Deliberately do NOT read the response. The card auto-denies after 120 s,
        # and blocking for that on four CI jobs buys nothing: the property under
        # test is that the write does not land WITHOUT a click, which is already
        # decided the moment the request is accepted. Sample the file instead.
        time.sleep(5)
        after = open(doc, encoding="utf-8").read()
        if "INJECTED" in after or after != before:
            fail("APPROVAL GATE BYPASSED — a write applied with no human click")
        print("  ok  approval gate held (headless write did not land)")

        print(f"\nMCP E2E SMOKE: PASS on {sys.platform}")
        return 0
    finally:
        if proc is not None:
            proc.kill()
        ed.terminate()
        try:
            ed.wait(timeout=10)
        except Exception:
            ed.kill()
        shutil.rmtree(home, ignore_errors=True)


if __name__ == "__main__":
    sys.exit(main())
