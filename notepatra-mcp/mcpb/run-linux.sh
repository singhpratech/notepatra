#!/bin/sh
# Notepatra MCP launcher — the MCPB manifest cannot select by CPU arch,
# so this picks the right packed linux binary at runtime.
#
# Residuals, stated honestly:
#   * assumes the host execs a shebang script (true for the Node spawn-based
#     MCPB hosts; a host that exec()s the file with no shell would break);
#   * unknown archs deliberately fall back to linux-x64 — best effort beats a
#     hard failure on a machine that may well be running x86 emulation.
dir="$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)"
case "$(uname -m)" in
  aarch64|arm64) exec "$dir/../linux-arm64/notepatra-mcp" "$@" ;;
  *)             exec "$dir/../linux-x64/notepatra-mcp" "$@" ;;
esac
