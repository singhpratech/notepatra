#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
# Live end-to-end test: real editor (build118) + real MCP sidecar over the
# dedicated -mcp QLocalServer socket, under an isolated temp $HOME.
#
# Prints PASS/FAIL per step; exits non-zero on any failure.
set -u

SCRIPT_DIR=$(cd "$(dirname "$0")" && pwd)
REPO=$(dirname "$SCRIPT_DIR")
EDITOR_BIN=$REPO/build118/notepatra
MCP_BIN=$SCRIPT_DIR/target/release/notepatra-mcp

# Short, predictable temp root: Qt derives the socket dir from $TMPDIR, and
# unix socket paths are capped near 108 bytes.
export TMPDIR=/tmp

FAILED=0
pass() { echo "PASS: $1"; }
fail() { echo "FAIL: $1"; FAILED=1; }

step() { # step <name> <command...>
    local name=$1; shift
    if "$@" >/dev/null 2>&1; then pass "$name"; else fail "$name"; fi
}

[ -x "$EDITOR_BIN" ] || { echo "FAIL: editor binary missing at $EDITOR_BIN"; exit 1; }

echo "== build sidecar (release) =="
( cd "$SCRIPT_DIR" && cargo build --release --quiet ) || { echo "FAIL: cargo build --release"; exit 1; }
[ -x "$MCP_BIN" ] || { echo "FAIL: sidecar binary missing at $MCP_BIN"; exit 1; }
pass "cargo build --release"

# ── Temp HOME + seeded Noter note + document ────────────────────────────
TMP_HOME=$(mktemp -d /tmp/np-e2e-home.XXXXXX)
NOTER=$TMP_HOME/Documents/Notepatra/Noter
mkdir -p "$NOTER"
NOTE_FILE=$NOTER/e2e-note.html
# Mimics NotesTemplate::shellHtml enough for NotesStorage: UTF-8 HTML with
# the notepatra-title meta (the on-disk title SSOT) and a body.
cat > "$NOTE_FILE" <<'EOF'
<!DOCTYPE html>
<html lang="en">
<head>
<meta charset="UTF-8">
<meta name="notepatra-schema" content="1">
<meta name="notepatra-title" content="E2E Note">
<title>E2E Note</title>
</head>
<body contenteditable="true">
<h1>E2E Note</h1>
<p>hello from noter e2e</p>
</body>
</html>
EOF
DOC=$TMP_HOME/e2e-doc.txt
printf 'alpha line one\nbravo line two\n' > "$DOC"
pass "temp HOME seeded ($TMP_HOME)"

EDPID=""
cleanup() {
    if [ -n "$EDPID" ] && kill -0 "$EDPID" 2>/dev/null; then
        kill "$EDPID" 2>/dev/null
        for _ in $(seq 1 50); do kill -0 "$EDPID" 2>/dev/null || break; sleep 0.1; done
        kill -9 "$EDPID" 2>/dev/null
    fi
    rm -rf "$TMP_HOME"
}
trap cleanup EXIT

# ── Launch the editor under the temp HOME ───────────────────────────────
HOME=$TMP_HOME QT_QPA_PLATFORM=offscreen "$EDITOR_BIN" "$DOC" \
    > "$TMP_HOME/editor.log" 2>&1 &
EDPID=$!

# Socket name mirrors SingleInstance::serverName(): "notepatra-" + first 16
# hex of SHA-1($HOME) + "-mcp", materialized at $TMPDIR/<name>.
HASH=$(printf '%s' "$TMP_HOME" | sha1sum | cut -c1-16)
SOCK=$TMPDIR/notepatra-$HASH-mcp
FOUND=0
for _ in $(seq 1 150); do
    [ -S "$SOCK" ] && { FOUND=1; break; }
    kill -0 "$EDPID" 2>/dev/null || break
    sleep 0.1
done
if [ "$FOUND" = 1 ]; then
    pass "editor up, -mcp socket appeared ($SOCK)"
else
    fail "-mcp socket did not appear within 15s"
    echo "---- editor.log ----"; cat "$TMP_HOME/editor.log"; echo "--------------------"
    exit 1
fi

# Isolation check: the temp HOME hashes to its own single-instance name, so
# this editor must still be running as its own primary (it did NOT forward
# to any pre-existing session and exit).
step "editor is its own instance (temp HOME isolation)" kill -0 "$EDPID"

# ── Resolve the document's tab index (a fresh HOME opens a Welcome tab
#    at index 0, so the doc index is discovered, not assumed) ────────────
DOC_IDX=$(
    {
        echo '{"jsonrpc":"2.0","id":1,"method":"initialize","params":{"protocolVersion":"2025-06-18","capabilities":{},"clientInfo":{"name":"e2e-probe","version":"0"}}}'
        echo '{"jsonrpc":"2.0","method":"notifications/initialized"}'
        echo '{"jsonrpc":"2.0","id":2,"method":"tools/call","params":{"name":"list_open_tabs","arguments":{}}}'
    } | HOME=$TMP_HOME "$MCP_BIN" --socket 2>/dev/null \
      | DOC="$DOC" python3 -c '
import json, os, sys
doc = os.environ["DOC"]
for line in sys.stdin:
    m = json.loads(line)
    if m.get("id") == 2:
        tabs = json.loads(m["result"]["content"][0]["text"])
        for t in tabs:
            if t.get("path") == doc:
                print(t["index"]); break
'
)
if [ -n "$DOC_IDX" ]; then
    pass "document tab located (index $DOC_IDX)"
else
    fail "document tab not found in list_open_tabs"
    exit 1
fi

# ── One scripted MCP stdio session against the live editor ─────────────
RESP=$TMP_HOME/responses.jsonl
{
    echo '{"jsonrpc":"2.0","id":1,"method":"initialize","params":{"protocolVersion":"2025-06-18","capabilities":{},"clientInfo":{"name":"e2e","version":"0"}}}'
    echo '{"jsonrpc":"2.0","method":"notifications/initialized"}'
    echo '{"jsonrpc":"2.0","id":2,"method":"tools/list"}'
    echo '{"jsonrpc":"2.0","id":3,"method":"tools/call","params":{"name":"list_open_tabs","arguments":{}}}'
    printf '{"jsonrpc":"2.0","id":4,"method":"tools/call","params":{"name":"read_tab","arguments":{"tab_index":%s}}}\n' "$DOC_IDX"
    echo '{"jsonrpc":"2.0","id":5,"method":"tools/call","params":{"name":"get_status","arguments":{}}}'
    echo '{"jsonrpc":"2.0","id":6,"method":"tools/call","params":{"name":"list_notes","arguments":{}}}'
    printf '{"jsonrpc":"2.0","id":7,"method":"tools/call","params":{"name":"read_note","arguments":{"file":"%s"}}}\n' "$NOTE_FILE"
    echo '{"jsonrpc":"2.0","id":8,"method":"tools/call","params":{"name":"format_json","arguments":{"text":"{\"b\":1,\"a\":2}"}}}'
    echo '{"jsonrpc":"2.0","id":9,"method":"resources/list"}'
    printf '{"jsonrpc":"2.0","id":10,"method":"resources/read","params":{"uri":"notepatra://tab/%s"}}\n' "$DOC_IDX"
    echo '{"jsonrpc":"2.0","id":11,"method":"prompts/get","params":{"name":"explain-selection"}}'
    # v0.1.119 read verb against the live bridge: a fresh HOME has no reminders
    # set, so this must return a well-formed (empty) reminders array.
    echo '{"jsonrpc":"2.0","id":12,"method":"tools/call","params":{"name":"list_reminders","arguments":{}}}'
} | HOME=$TMP_HOME "$MCP_BIN" --socket > "$RESP" 2>"$TMP_HOME/mcp.log"

if [ -s "$RESP" ]; then
    pass "scripted stdio session produced responses"
else
    fail "sidecar produced no output"
    echo "---- mcp.log ----"; cat "$TMP_HOME/mcp.log"; echo "-----------------"
    exit 1
fi

# ── Per-step response validation (python3 stands in for jq) ─────────────
DOC="$DOC" NOTE_FILE="$NOTE_FILE" DOC_IDX="$DOC_IDX" python3 - "$RESP" <<'PYEOF'
import json, os, sys

doc = os.environ["DOC"]
note_file = os.environ["NOTE_FILE"]
doc_idx = int(os.environ["DOC_IDX"])
resp = {}
with open(sys.argv[1]) as f:
    for line in f:
        line = line.strip()
        if line:
            m = json.loads(line)
            resp[m.get("id")] = m

failed = False
def check(name, cond, detail=""):
    global failed
    if cond:
        print(f"PASS: {name}")
    else:
        print(f"FAIL: {name}" + (f" — {detail}" if detail else ""))
        failed = True

def result(i):
    return resp.get(i, {}).get("result")

def tool_text(i):
    r = result(i) or {}
    if r.get("isError") is not False:
        return None
    return r["content"][0]["text"]

# 1: initialize
r = result(1) or {}
check("initialize", r.get("protocolVersion") == "2025-06-18"
      and r.get("serverInfo", {}).get("name") == "notepatra-mcp",
      json.dumps(resp.get(1)))

# 2: tools/list — all 48 tools present (22 from v0.1.118 + 13 from v0.1.119 + 2 from p0a + 4 from phase 1 + 7 from phase 2).
# tools/list is served by the Rust sidecar itself, so the count is independent
# of which verbs the live editor bridge implements.
tools = [t["name"] for t in (result(2) or {}).get("tools", [])]
check("tools/list (48 tools)", len(tools) == 48 and "read_note" in tools
      and "insert_text" in tools and "save_tab" in tools
      and "open_file" in tools and "list_reminders" in tools
      and "git_status" in tools and "export_diagram" in tools
      and "list_languages" in tools and "get_capabilities" in tools
      and "create_diagram" in tools and "open_noter" in tools
      and "list_connections" in tools and "run_query" in tools
      and "export_chart" in tools, str(tools))

# 3: list_open_tabs — our document is an open tab
t = tool_text(3)
tabs = json.loads(t) if t else []
check("tools/call list_open_tabs", any(tab.get("path") == doc for tab in tabs),
      t or json.dumps(resp.get(3)))

# 4: read_tab (document tab) — raw document text
t = tool_text(4)
check(f"tools/call read_tab (index {doc_idx})",
      t is not None and "alpha line one" in t and "bravo line two" in t,
      json.dumps(resp.get(4)))

# 5: get_status — exact bridge shape fields
t = tool_text(5)
st = json.loads(t) if t else {}
check("tools/call get_status",
      st.get("path") == doc and st.get("tab_index") == doc_idx
      and all(k in st for k in ("title", "language", "encoding",
                                "cursor_line", "cursor_col", "edition", "version")),
      t or json.dumps(resp.get(5)))

# 6: list_notes — seeded note listed with meta title + absolute .html path
t = tool_text(6)
notes = (json.loads(t) if t else {}).get("notes", [])
check("tools/call list_notes",
      any(n.get("title") == "E2E Note" and n.get("file") == note_file
          and "T" in n.get("modified_iso", "") for n in notes),
      t or json.dumps(resp.get(6)))

# 7: read_note — title from meta, text is the HTML stripped to plaintext
t = tool_text(7)
note = json.loads(t) if t else {}
check("tools/call read_note",
      note.get("title") == "E2E Note"
      and "hello from noter e2e" in note.get("text", ""),
      t or json.dumps(resp.get(7)))

# 8: format_json — round-trips through the editor's Rust formatter
t = tool_text(8)
try:
    fmt = json.loads(t) if t else None
except Exception:
    fmt = None
check("tools/call format_json", fmt == {"a": 2, "b": 1},
      t or json.dumps(resp.get(8)))

# 9: resources/list — tab resource + seeded note resource
res = (result(9) or {}).get("resources", [])
uris = [r.get("uri", "") for r in res]
check("resources/list",
      f"notepatra://tab/{doc_idx}" in uris
      and any(u == "notepatra://note/" + os.path.basename(note_file) for u in uris),
      str(uris))

# 10: resources/read (document tab)
contents = (result(10) or {}).get("contents", [])
check(f"resources/read (tab {doc_idx})",
      bool(contents) and contents[0].get("uri") == f"notepatra://tab/{doc_idx}"
      and "alpha line one" in contents[0].get("text", ""),
      json.dumps(resp.get(10)))

# 11: prompts/get explain-selection (no selection in offscreen editor —
# the prompt must still render with the empty selection embedded)
msgs = (result(11) or {}).get("messages", [])
check("prompts/get explain-selection",
      bool(msgs) and msgs[0]["role"] == "user"
      and "Explain the following text" in msgs[0]["content"]["text"],
      json.dumps(resp.get(11)))

# 12: tools/call list_reminders (v0.1.119) — live bridge returns a
# well-formed reminders array (empty for a fresh HOME with no reminders set)
t = tool_text(12)
rem = json.loads(t) if t else None
check("tools/call list_reminders (v0.1.119)",
      isinstance(rem, dict) and isinstance(rem.get("reminders"), list),
      t or json.dumps(resp.get(12)))

sys.exit(1 if failed else 0)
PYEOF
[ $? -eq 0 ] || FAILED=1

# ── Shutdown ────────────────────────────────────────────────────────────
kill "$EDPID" 2>/dev/null
for _ in $(seq 1 50); do kill -0 "$EDPID" 2>/dev/null || break; sleep 0.1; done
if kill -0 "$EDPID" 2>/dev/null; then
    kill -9 "$EDPID" 2>/dev/null
    fail "editor needed SIGKILL"
else
    pass "editor shut down cleanly"
fi
EDPID=""

if [ "$FAILED" = 0 ]; then
    echo "== E2E: ALL STEPS PASS =="
    exit 0
else
    echo "== E2E: FAILURES (see FAIL lines above) =="
    exit 1
fi
