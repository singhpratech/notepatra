# notepatra-mcp

A [Model Context Protocol](https://modelcontextprotocol.io) stdio server for the [Notepatra](https://notepatra.org) editor. Lets Claude Desktop, Claude Code, OpenAI Codex CLI, the OpenAI Agents SDK, and any spec-compliant MCP client see what you have open, search your workspace, read Noter notes — and, only with your explicit per-action approval inside the editor, edit and save.

- **49 tools in three tiers**: read (tabs, selection, search, notes, reminders, Git status/diff/log/show/branch, npd validation, read-only SQL, language list, capabilities, diagram source read, saved-connection list/query/tables), act (open, compare, format, navigate, open notes, create diagram, open Noter panel, open Data Analyst, render chart), write (insert/replace/edit/save, create/append notes, set reminders, set diagram source, export diagrams, export query results, export chart — every write shows an Approve / Deny card inside Notepatra; 120 s auto-deny, no headless bypass).
- **Local only**: stdio + a per-user local socket. Nothing leaves your machine.
- **Resources and prompts**: open tabs as `notepatra://tab/N`, notes as `notepatra://note/<name>`, plus ready-made review/explain/summarize prompts.

## Quick start

```sh
cargo install notepatra-mcp        # or grab a signed binary from the Notepatra release page

# Claude Code
claude mcp add notepatra -- notepatra-mcp --socket
```

`--socket` connects to a running Notepatra (v0.1.118+). Without it, the server runs against a built-in **mock** editor for testing client integrations — every result from it is labelled `[MOCK DATA]` with `_meta.mock`, so a config that has lost its `--socket` cannot quietly serve an assistant fabricated tabs.

Address tabs by the `id` from `list_open_tabs`, not by `tab_index`: an index is positional and re-points at a different document as soon as another tab closes. Both are accepted everywhere.

Files matching the credential deny-list (SSH/GPG/AWS key directories, `id_rsa`, `.env`, `*.pem`, `*.key`, `*.pfx`, `*.p12`, `*.jks`, Terraform state) are refused by every read path and cannot be reached through this server.

Linux, macOS, and Windows (named-pipe transport, v0.1.119+). Full docs, per-client setup snippets, and the security model: **[notepatra.org/mcp.html](https://notepatra.org/mcp.html)**.

## Links

- Docs: [notepatra.org/mcp.html](https://notepatra.org/mcp.html)
- Source: [github.com/singhpratech/notepatra](https://github.com/singhpratech/notepatra)
- mcp-name: io.github.singhpratech/notepatra-mcp

License: GPL-3.0-or-later. Part of the [Notepatra](https://github.com/singhpratech/notepatra) project.
