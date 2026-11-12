# notepatra-mcp

A [Model Context Protocol](https://modelcontextprotocol.io) stdio server for the [Notepatra](https://notepatra.org) editor. Lets Claude Desktop, Claude Code, OpenAI Codex CLI, the OpenAI Agents SDK, and any spec-compliant MCP client see what you have open, search your workspace, read Noter notes — and, only with your explicit per-action approval inside the editor, edit and save.

- **119 tools in three tiers** (every write shows an Approve / Deny card inside Notepatra; 120 s auto-deny, no headless bypass):

| Tier | Tools |
|---|---|
| **Read** (46) | tabs and editor: `list_open_tabs`, `read_tab`, `get_selection`, `get_status`, `app_info`, `list_recent_files`, `list_languages`, `get_capabilities`, `get_preferences`, `list_symbols`; search: `find_in_tab`, `search_project`; Noter: `list_notes`, `read_note`, `list_reminders`, `search_notes`; Git: `git_status`, `git_diff`, `git_log`, `git_show`, `git_branch`, `list_hunks`, `list_conflicts`; diagrams: `validate_npd`, `get_diagram_source`, `mermaid_to_npd`; SQL and saved connections: `run_sql`, `list_connections`, `run_query`, `list_tables`, `describe_table`; pure text tools: `format_json`, `format_sql`, `format_html`, `check_brackets`, `fix_brackets`, `repair_json`, `hash_text`, `encode_text`, `diff_text`, `markdown_convert`, `generate_password`, `hex_dump`; REST helpers: `parse_http_file`, `curl_to_request`, `request_to_curl` |
| **Act** (22) | tabs and navigation: `open_file`, `new_tab`, `activate_tab`, `goto_line`, `select_range`, `set_language`, `set_read_only`, `bookmarks`, `fold`; view: `set_view`, `set_theme`, `set_preview`, `compare_tabs`, `compare_with_file`; Noter and diagrams: `open_note`, `open_noter`, `create_diagram`; data: `open_data_analyst`, `render_chart`, `open_connections_dialog`, `open_schema_browser`, `test_connection` |
| **Write** (51) | editing: `insert_text`, `replace_selection`, `apply_edit`, `undo`, `redo`, `transform_lines`, `convert_case`, `comment_lines`, `line_edit`, `set_eol`, `set_encoding`; tabs and files: `save_tab`, `save_all`, `close_tab`, `reload_tab`, `rename_file`, `set_workspace_folder`, `set_preference`, `clear_recent_files`; Noter: `create_note`, `append_note`, `set_reminder`, `rename_note`, `trash_note`, `restore_note`, `delete_note_permanently`, `delete_reminder`, `snooze_reminder`, `dismiss_reminder`, `export_note`; diagrams and exports: `set_diagram_source`, `export_diagram`, `export_query_results`, `export_chart`; Git: `git_stage`, `git_unstage`, `git_commit`, `git_branch_create`, `git_checkout`, `git_stash`, `git_discard`, `git_hunk`, `resolve_conflict`, `git_fetch`, `git_pull`, `git_push`; data and REST: `execute_sql`, `add_connection`, `update_connection`, `delete_connection`, `rest_request` |

- **Local by default**: stdio to the client and a per-user local socket to the editor; the server opens no network connections of its own. Tools that use a saved database connection talk to that database with its stored credentials: `run_query`, `list_tables`, `describe_table` and `test_connection` without a card, `export_query_results` and `execute_sql` after approval. `rest_request`, `git_fetch`, `git_pull` and `git_push` run only after approval, and over the opt-in remote gateway need the `write_external` scope, which is never a default.
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
