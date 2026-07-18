# notepatra-mcp

A [Model Context Protocol](https://modelcontextprotocol.io) stdio server for the [Notepatra](https://notepatra.org) editor. Lets Claude Desktop, Claude Code, OpenAI Codex CLI, the OpenAI Agents SDK, and any spec-compliant MCP client see what you have open, search your workspace, read Noter notes — and, only with your explicit per-action approval inside the editor, edit and save.

- **41 tools in three tiers**: read (tabs, selection, search, notes, reminders, Git status/diff/log/show/branch, npd validation, read-only SQL, language list, capabilities, diagram source read), act (open, compare, format, navigate, open notes, create diagram, open Noter panel), write (insert/replace/edit/save, create/append notes, set reminders, set diagram source, export diagrams — every write shows an Approve / Deny card inside Notepatra; 120 s auto-deny, no headless bypass).
- **Local only**: stdio + a per-user local socket. Nothing leaves your machine.
- **Resources and prompts**: open tabs as `notepatra://tab/N`, notes as `notepatra://note/<name>`, plus ready-made review/explain/summarize prompts.

## Quick start

```sh
cargo install notepatra-mcp        # or grab a signed binary from the Notepatra release page

# Claude Code
claude mcp add notepatra -- notepatra-mcp --socket
```

`--socket` connects to a running Notepatra (v0.1.118+). Without it, the server runs against a built-in mock editor for testing client integrations.

Linux, macOS, and Windows (named-pipe transport, v0.1.119+). Full docs, per-client setup snippets, and the security model: **[notepatra.org/mcp.html](https://notepatra.org/mcp.html)**.

## Links

- Docs: [notepatra.org/mcp.html](https://notepatra.org/mcp.html)
- Source: [github.com/singhpratech/notepatra](https://github.com/singhpratech/notepatra)
- mcp-name: io.github.singhpratech/notepatra-mcp

License: GPL-3.0-or-later. Part of the [Notepatra](https://github.com/singhpratech/notepatra) project.
