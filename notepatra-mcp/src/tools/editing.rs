// SPDX-License-Identifier: GPL-3.0-or-later
//! v0.1.133 tools: tabs and files, editing, view, and pure text tools.
//!
//! Every handler validates its arguments here (so a malformed call is a
//! -32602 before anything reaches the editor) and then forwards the SAME
//! argument object to the bridge verb of the same name through
//! [`EditorTransport::call_verb`]. The C++ bridge validates again, words the
//! approval card and runs the menu's own function.

use serde_json::{json, Map, Value};

use super::{reject_extras, to_outcome, CallOutcome, APPROVAL_NOTE};
use crate::transport::EditorTransport;

const TAB_ID_DESC: &str = "Stable tab id from list_open_tabs. PREFER THIS over tab_index: an \
     index is positional and silently re-points at a different document when another tab closes.";

/// Kinds of argument value the validator understands.
#[derive(Clone, Copy)]
pub(super) enum Kind {
    Str,
    Bool,
    /// Inclusive integer range.
    Int(i64, i64),
    Enum(&'static [&'static str]),
    /// Any integer (tab ids).
    Id,
    /// Non-negative integer (tab indexes).
    Index,
    Object,
    /// Non-empty array whose items are all in the list.
    EnumArray(&'static [&'static str]),
}

pub(super) struct Arg {
    pub key: &'static str,
    pub kind: Kind,
    pub required: bool,
}

const fn opt(key: &'static str, kind: Kind) -> Arg {
    Arg {
        key,
        kind,
        required: false,
    }
}

const fn req(key: &'static str, kind: Kind) -> Arg {
    Arg {
        key,
        kind,
        required: true,
    }
}

const TAB_SEL: [Arg; 2] = [opt("tab_id", Kind::Id), opt("tab_index", Kind::Index)];

fn bad(msg: String) -> CallOutcome {
    CallOutcome::InvalidParams(msg)
}

/// Rejects unknown keys, missing required keys and mistyped values.
pub(super) fn check(map: &Map<String, Value>, spec: &[Arg]) -> Result<(), CallOutcome> {
    let keys: Vec<&str> = spec.iter().map(|a| a.key).collect();
    reject_extras(map, &keys)?;
    for a in spec {
        let Some(v) = map.get(a.key) else {
            if a.required {
                return Err(bad(format!("{} is required", a.key)));
            }
            continue;
        };
        let ok = match a.kind {
            Kind::Str => v.is_string(),
            Kind::Bool => v.is_boolean(),
            Kind::Int(lo, hi) => v.as_i64().is_some_and(|n| n >= lo && n <= hi),
            Kind::Enum(list) => v.as_str().is_some_and(|s| list.contains(&s)),
            Kind::Id => v.as_i64().is_some(),
            Kind::Index => v.as_u64().is_some(),
            Kind::Object => v.is_object(),
            Kind::EnumArray(list) => v.as_array().is_some_and(|items| {
                !items.is_empty()
                    && items
                        .iter()
                        .all(|i| i.as_str().is_some_and(|s| list.contains(&s)))
            }),
        };
        if !ok {
            return Err(bad(match a.kind {
                Kind::Str => format!("{} must be a string", a.key),
                Kind::Bool => format!("{} must be a boolean", a.key),
                Kind::Int(lo, hi) => format!("{} must be an integer from {lo} to {hi}", a.key),
                Kind::Enum(list) => format!("{} must be one of {}", a.key, list.join("|")),
                Kind::Id => format!("{} must be an integer", a.key),
                Kind::Index => format!("{} must be a non-negative integer", a.key),
                Kind::Object => format!("{} must be an object", a.key),
                Kind::EnumArray(list) => {
                    format!("{} must be a non-empty array of {}", a.key, list.join("|"))
                }
            }));
        }
    }
    Ok(())
}

/// At most one of `keys` may be present; `need` = exactly one.
pub(super) fn one_of(
    map: &Map<String, Value>,
    keys: &[&str],
    need: bool,
) -> Result<(), CallOutcome> {
    let n = keys.iter().filter(|k| map.contains_key(**k)).count();
    if n > 1 {
        return Err(bad(format!("provide only one of {}", keys.join(", "))));
    }
    if need && n == 0 {
        return Err(bad(format!("provide one of {}", keys.join(", "))));
    }
    Ok(())
}

fn with(base: &[Arg], more: impl IntoIterator<Item = Arg>) -> Vec<Arg> {
    let mut v: Vec<Arg> = base
        .iter()
        .map(|a| Arg {
            key: a.key,
            kind: a.kind,
            required: a.required,
        })
        .collect();
    v.extend(more);
    v
}

/// Validate, then forward the same arguments to the bridge verb `verb`.
fn forward(
    transport: &mut dyn EditorTransport,
    verb: &str,
    args: &Map<String, Value>,
    spec: &[Arg],
    tab: TabRule,
) -> CallOutcome {
    if let Err(e) = check(args, spec) {
        return e;
    }
    let sel = match tab {
        TabRule::Optional => one_of(args, &["tab_id", "tab_index"], false),
        TabRule::Required => one_of(args, &["tab_id", "tab_index"], true),
        TabRule::TextOrTab => one_of(args, &["text", "tab_id", "tab_index"], true),
        TabRule::None => Ok(()),
    };
    if let Err(e) = sel {
        return e;
    }
    to_outcome(transport.call_verb(verb, Value::Object(args.clone())))
}

#[derive(Clone, Copy)]
enum TabRule {
    None,
    Optional,
    Required,
    TextOrTab,
}

// ── schemas ─────────────────────────────────────────────────────────────

fn tab_props(default_active: bool) -> Map<String, Value> {
    let mut m = Map::new();
    m.insert(
        "tab_id".into(),
        json!({ "type": "integer", "description": TAB_ID_DESC }),
    );
    m.insert(
        "tab_index".into(),
        json!({ "type": "integer", "minimum": 0, "description": if default_active {
            "Zero-based tab index (default: the active tab)"
        } else {
            "Zero-based tab index"
        } }),
    );
    m
}

fn text_or_tab_props() -> Map<String, Value> {
    let mut m = tab_props(false);
    m.insert(
        "text".into(),
        json!({ "type": "string", "description": "The text to work on. Provide this OR a tab (tab_id / tab_index), not both; a tab's buffer is read the way read_tab reads it (credential files are refused)." }),
    );
    m
}

fn tool(name: &str, description: String, props: Map<String, Value>, required: &[&str]) -> Value {
    json!({
        "name": name,
        "description": description,
        "inputSchema": {
            "type": "object",
            "properties": Value::Object(props),
            "required": required,
            "additionalProperties": false
        }
    })
}

fn add(mut m: Map<String, Value>, key: &str, schema: Value) -> Map<String, Value> {
    m.insert(key.into(), schema);
    m
}

pub(super) const LINE_OPS: &[&str] = &[
    "sort_asc",
    "sort_desc",
    "sort_int_asc",
    "sort_int_desc",
    "sort_len_asc",
    "sort_len_desc",
    "dedupe",
    "dedupe_consecutive",
    "remove_empty",
    "remove_blank",
    "join",
    "reverse",
    "trim_trailing",
    "trim_leading",
    "trim_both",
    "tab_to_space",
    "space_to_tab",
];
const SCOPES: &[&str] = &["selection", "document"];
const CASE_MODES: &[&str] = &["upper", "lower", "proper", "sentence", "invert"];
const COMMENT_KINDS: &[&str] = &["line", "block"];
const COMMENT_ACTIONS: &[&str] = &["toggle", "comment", "uncomment"];
const LINE_EDIT_OPS: &[&str] = &["duplicate", "delete", "move_up", "move_down"];
const EOLS: &[&str] = &["crlf", "lf", "cr"];
pub(super) const ENCODINGS: &[&str] = &[
    "UTF-8",
    "UTF-8 BOM",
    "UTF-16 LE",
    "UTF-16 LE BOM",
    "UTF-16 BE",
    "UTF-16 BE BOM",
    "UTF-32 LE BOM",
    "UTF-32 BE BOM",
    "ANSI (Windows-1252)",
    "ISO-8859-1",
];
const ENC_MODES: &[&str] = &["convert", "reinterpret"];
const BOOKMARK_ACTIONS: &[&str] = &["list", "toggle", "clear"];
const FOLD_ACTIONS: &[&str] = &["fold_all", "unfold_all"];
const THEMES: &[&str] = &["System", "Light", "Dark", "Monokai"];
const HASH_ALGOS: &[&str] = &["md5", "sha1", "sha256", "sha512"];
const ENCODE_OPS: &[&str] = &["base64_encode", "base64_decode", "url_encode", "url_decode"];
const MD_OPS: &[&str] = &[
    "table",
    "list",
    "code_block",
    "bold",
    "italic",
    "link",
    "heading",
    "html_to_markdown",
];
const PW_MODES: &[&str] = &["password", "passphrase"];
const PW_CLASSES: &[&str] = &["lower", "upper", "digits", "symbols"];

const FORMAT_JSON_ARGS: [Arg; 3] = [
    req("text", Kind::Str),
    opt("indent", Kind::Int(0, 8)),
    opt("minify", Kind::Bool),
];
const FORMAT_SQL_ARGS: [Arg; 3] = [
    req("text", Kind::Str),
    opt("indent", Kind::Int(1, 8)),
    opt("keyword_case", Kind::Enum(&["upper", "lower"])),
];
const FORMAT_HTML_ARGS: [Arg; 2] = [req("text", Kind::Str), opt("indent", Kind::Int(0, 8))];

/// format_json / format_sql / format_html arguments (v0.1.133 options).
pub(super) fn format_spec(kind: &str) -> &'static [Arg] {
    if kind == "json" {
        &FORMAT_JSON_ARGS
    } else if kind == "sql" {
        &FORMAT_SQL_ARGS
    } else {
        &FORMAT_HTML_ARGS
    }
}

/// The v0.1.133 tool definitions, appended to the existing list.
pub(super) fn definitions() -> Vec<Value> {
    let s = |d: &str| json!({ "type": "string", "description": d });
    let b = |d: &str| json!({ "type": "boolean", "description": d });
    let e = |list: &[&str], d: &str| json!({ "type": "string", "enum": list, "description": d });
    let line = |d: &str| json!({ "type": "integer", "minimum": 1, "description": d });
    vec![
        // ── tabs and files ──
        tool(
            "activate_tab",
            "Bring one open tab to the front (make it the active tab), selected by tab_id (preferred) or tab_index. Changes only which tab is shown. Returns {active, tab_index, tab_id}.".into(),
            tab_props(false),
            &[],
        ),
        tool(
            "close_tab",
            format!(
                "Close one open tab, selected by tab_id (preferred) or tab_index. A tab with no \
                 unsaved changes closes at once, with no card. A tab WITH unsaved changes is refused \
                 unless discard_unsaved is true, and then the user must approve a card that says \
                 the changes will be lost. Returns {{closed, tab_index, tab_id}}. {APPROVAL_NOTE}"
            ),
            add(tab_props(false), "discard_unsaved", b("Close even though the tab has unsaved changes, which are then lost (default false). Requires the user's approval.")),
            &[],
        ),
        tool(
            "reload_tab",
            format!(
                "Reload a tab's file from disk (File > Reload from Disk), default the active tab. \
                 With no unsaved changes this happens at once, with no card; with unsaved changes \
                 the user must approve a card saying they will be lost. Returns {{reloaded, \
                 tab_index, tab_id}}. {APPROVAL_NOTE}"
            ),
            tab_props(true),
            &[],
        ),
        tool(
            "rename_file",
            format!(
                "Rename the file behind a tab (default the active tab) inside the same folder. \
                 new_name is a bare file name: no path separators; an existing file is never \
                 overwritten; protected names (shell start-up files, git hooks, credential stores) \
                 are refused. Unsaved changes are saved to the old name first. Returns {{path, \
                 tab_index, tab_id}}. {APPROVAL_NOTE}"
            ),
            add(tab_props(true), "new_name", s("The new file name, e.g. \"notes-2026.md\" (same folder, no separators)")),
            &["new_name"],
        ),
        tool(
            "save_all",
            format!(
                "Save every open tab that has unsaved changes and a file on disk, behind ONE \
                 approval card that lists the files. Untitled tabs are skipped and reported (use \
                 save_tab with a path for those); protected destinations are skipped too. Returns \
                 {{saved:[paths], failed:[{{path,error}}], skipped_untitled:[titles], \
                 skipped_protected:[paths]}}. {APPROVAL_NOTE}"
            ),
            Map::new(),
            &[],
        ),
        tool(
            "set_read_only",
            "Turn a tab's read-only mode on or off (default the active tab). Only the editing lock changes; nothing is written. Returns {read_only, tab_index, tab_id}.".into(),
            add(tab_props(true), "on", b("true = read-only, false = editable")),
            &["on"],
        ),
        // ── editing ──
        tool(
            "undo",
            format!(
                "Undo the last edit(s) in a tab (Edit > Undo), default the active tab. Returns \
                 {{steps}} actually undone. {APPROVAL_NOTE}"
            ),
            add(tab_props(true), "steps", json!({ "type": "integer", "minimum": 1, "maximum": 100, "description": "How many undo steps (default 1, max 100)" })),
            &[],
        ),
        tool(
            "redo",
            format!(
                "Redo previously undone edit(s) in a tab (Edit > Redo), default the active tab. \
                 Returns {{steps}} actually redone. {APPROVAL_NOTE}"
            ),
            add(tab_props(true), "steps", json!({ "type": "integer", "minimum": 1, "maximum": 100, "description": "How many redo steps (default 1, max 100)" })),
            &[],
        ),
        tool(
            "transform_lines",
            format!(
                "Run one Edit > Line Operations / Blank Operations transform on a tab (default the \
                 active tab): sorts (lexicographic, as integers, by length), removing duplicate / \
                 empty / blank lines, join, reverse, trimming, and tab/space conversion. One undo \
                 step; the tab becomes modified. Returns {{changed, tab_index, tab_id}}. \
                 {APPROVAL_NOTE}"
            ),
            add(
                add(tab_props(true), "op", e(LINE_OPS, "The transform: sort_asc, sort_desc, sort_int_asc, sort_int_desc, sort_len_asc, sort_len_desc, dedupe, dedupe_consecutive, remove_empty (empty lines), remove_blank (whitespace-only too), join (with one space), reverse, trim_trailing, trim_leading, trim_both, tab_to_space, space_to_tab")),
                "scope",
                e(SCOPES, "document (default) or selection (the current selection only)"),
            ),
            &["op"],
        ),
        tool(
            "convert_case",
            format!(
                "Convert letter case (Edit > Convert Case to) in a tab, default the active tab: \
                 UPPER, lower, Proper, Sentence or iNVERT. One undo step. Returns {{changed, \
                 tab_index, tab_id}}. {APPROVAL_NOTE}"
            ),
            add(
                add(tab_props(true), "mode", e(CASE_MODES, "upper, lower, proper, sentence or invert")),
                "scope",
                e(SCOPES, "selection (default, as the menu) or document"),
            ),
            &["mode"],
        ),
        tool(
            "comment_lines",
            format!(
                "Comment, uncomment or toggle comments on a 1-based line range of a tab (default \
                 the active tab) with the tab language's own syntax (Edit > Comment/Uncomment). \
                 Fails when the language has no such comment syntax. Returns {{changed, \
                 tab_index, tab_id}}. {APPROVAL_NOTE}"
            ),
            add(
                add(
                    add(
                        add(tab_props(true), "start_line", line("First line (1-based)")),
                        "end_line",
                        line("Last line (1-based, >= start_line)"),
                    ),
                    "kind",
                    e(COMMENT_KINDS, "line (default) or block"),
                ),
                "action",
                e(COMMENT_ACTIONS, "toggle (default), comment or uncomment"),
            ),
            &["start_line", "end_line"],
        ),
        tool(
            "line_edit",
            format!(
                "Duplicate, delete, or move up/down one 1-based line of a tab (default the active \
                 tab), as Edit > Line Operations does at the cursor. One undo step. Returns \
                 {{line_count, tab_index, tab_id}}. {APPROVAL_NOTE}"
            ),
            add(
                add(tab_props(true), "line", line("The line (1-based)")),
                "op",
                e(LINE_EDIT_OPS, "duplicate, delete, move_up or move_down"),
            ),
            &["line", "op"],
        ),
        tool(
            "set_eol",
            format!(
                "Set a tab's line-ending style (Edit > EOL Conversion), default the active tab. \
                 With convert (default true) every existing line ending is rewritten; with false \
                 only new lines use it. Returns {{eol, tab_index, tab_id}}. {APPROVAL_NOTE}"
            ),
            add(
                add(tab_props(true), "eol", e(EOLS, "crlf (Windows), lf (Unix) or cr (classic Mac)")),
                "convert",
                b("Rewrite the existing line endings too (default true)"),
            ),
            &["eol"],
        ),
        tool(
            "set_encoding",
            format!(
                "Change a tab's encoding (the Encoding menu), default the active tab. mode convert \
                 (default) keeps the text and saves in the new encoding from now on (the tab \
                 becomes modified); mode reinterpret RELOADS the file from disk decoded as that \
                 encoding, which is refused on a tab with unsaved changes unless \
                 discard_unsaved is true (the card then says they will be lost). Returns \
                 {{encoding, tab_index, tab_id}}. {APPROVAL_NOTE}"
            ),
            add(
                add(
                    add(tab_props(true), "encoding", e(ENCODINGS, "The encoding, exactly as the Encoding menu names it")),
                    "mode",
                    e(ENC_MODES, "convert (default) or reinterpret"),
                ),
                "discard_unsaved",
                b("reinterpret only: reload even though the tab has unsaved changes (default false)"),
            ),
            &["encoding"],
        ),
        // ── view ──
        tool(
            "bookmarks",
            "List, toggle or clear the bookmarks of a tab (Search > Bookmarks), default the active tab. toggle needs a 1-based line. Returns {lines:[bookmarked 1-based lines], tab_index, tab_id} (toggle adds {line, bookmarked}).".into(),
            add(
                add(tab_props(true), "action", e(BOOKMARK_ACTIONS, "list, toggle or clear")),
                "line",
                line("Line to toggle (1-based; required for toggle)"),
            ),
            &["action"],
        ),
        tool(
            "fold",
            "Fold or unfold every foldable region of a tab (View > Fold All), default the active tab. Returns {action, tab_index, tab_id}.".into(),
            add(tab_props(true), "action", e(FOLD_ACTIONS, "fold_all or unfold_all")),
            &["action"],
        ),
        tool(
            "set_view",
            "Change editor view settings for every tab, exactly as the View menu does (persisted): word_wrap, zoom (editor font size in points, 6-48), show_whitespace, show_eol, indent_guides. Give at least one. Returns the values now in effect.".into(),
            {
                let mut m = Map::new();
                m.insert("word_wrap".into(), b("Wrap long lines"));
                m.insert("zoom".into(), json!({ "type": "integer", "minimum": 6, "maximum": 48, "description": "Editor font size in points (default size is 11)" }));
                m.insert("show_whitespace".into(), b("Show spaces and tabs"));
                m.insert("show_eol".into(), b("Show line-ending markers"));
                m.insert("indent_guides".into(), b("Show indent guides"));
                m
            },
            &[],
        ),
        tool(
            "set_theme",
            "Switch the application theme (Settings > Theme): System (follow the OS), Light, Dark or Monokai. Persisted. Returns {theme}.".into(),
            add(Map::new(), "theme", e(THEMES, "System, Light, Dark or Monokai")),
            &["theme"],
        ),
        tool(
            "set_preview",
            "Show or hide the Preview pane (View > Preview Pane) for a tab, default the active tab (the tab is brought to the front). Returns {on, tab_index, tab_id}.".into(),
            add(tab_props(true), "on", b("true = show the Preview pane, false = hide it")),
            &["on"],
        ),
        tool(
            "compare_with_file",
            "Open the side-by-side Compare view between a tab (default the active tab) and a file on disk (Tools > Compare with File). The file must be an existing absolute path that is not on the credential deny-list. Returns {opened, tab_index, tab_id}.".into(),
            add(tab_props(true), "path", s("Absolute path of the file to compare against")),
            &["path"],
        ),
        // ── pure tools (nothing in the editor changes) ──
        tool(
            "check_brackets",
            "Check (), [] and {} balance and begin/end-style keyword pairs in text or a tab, as Bracket Tools > Check does. Returns {report} (line/column of each problem). Read-only.".into(),
            text_or_tab_props(),
            &[],
        ),
        tool(
            "fix_brackets",
            "Auto-fix unbalanced brackets in text or a tab, as Bracket Tools > Auto-Fix does, and return the result WITHOUT changing the tab: {fixed, changed, report} where report lists what the checker found. Write it back yourself (e.g. apply_edit / insert_text) if wanted.".into(),
            text_or_tab_props(),
            &[],
        ),
        tool(
            "hash_text",
            "Hash text or a tab's content (UTF-8 bytes) like Utilities > Hash. Returns {algo, hash} as lowercase hex.".into(),
            add(text_or_tab_props(), "algo", e(HASH_ALGOS, "md5, sha1, sha256 (default) or sha512")),
            &[],
        ),
        tool(
            "encode_text",
            "Base64- or URL-encode/decode text or a tab's content, like Utilities > Base64 / URL. Returns {text}. Does not change the tab.".into(),
            add(text_or_tab_props(), "op", e(ENCODE_OPS, "base64_encode, base64_decode, url_encode or url_decode")),
            &["op"],
        ),
        tool(
            "diff_text",
            "Line diff between two texts, each given as {text} or a tab ({tab_id} / {tab_index}), using the Compare view's engine. Returns {identical, added, removed, hunks:[{left_line, right_line, lines:[{op:'+'|'-', text}]}], truncated} with 1-based line numbers (0 = the side has no line there).".into(),
            {
                let side = json!({ "type": "object", "description": "{\"text\": \"...\"} or {\"tab_id\": N} or {\"tab_index\": N}", "properties": { "text": { "type": "string" }, "tab_id": { "type": "integer" }, "tab_index": { "type": "integer", "minimum": 0 } }, "additionalProperties": false });
                let mut m = Map::new();
                m.insert("left".into(), side.clone());
                m.insert("right".into(), side);
                m
            },
            &["left", "right"],
        ),
        tool(
            "repair_json",
            "EXPLICIT opt-in JSON repair (JSON Tools > Fix): closes brackets, strips trailing commas, quotes bare keys. Returns {fixed, report}. The repaired text may contain structure the input never had; format_json never repairs and fails on invalid JSON instead.".into(),
            text_or_tab_props(),
            &[],
        ),
        tool(
            "markdown_convert",
            "Run a Tools > Markdown Converter transform on text or a tab's content and return the result as {text} (the tab is not changed): table (CSV/TSV to a Markdown table), list, code_block, bold, italic, link, heading, html_to_markdown (strip tags).".into(),
            add(text_or_tab_props(), "op", e(MD_OPS, "table, list, code_block, bold, italic, link, heading or html_to_markdown")),
            &["op"],
        ),
        tool(
            "mermaid_to_npd",
            "Translate Mermaid diagram text (or a tab holding it) to Notepatra Diagram (.npd) source with the Diagram tab's own importer. Returns {source}; pass it to create_diagram or validate_npd.".into(),
            text_or_tab_props(),
            &[],
        ),
        tool(
            "list_symbols",
            "List the classes and functions in a tab, as the Function List panel does (regex-based per language). Returns {language, symbols:[{name, kind:'class'|'function', line (1-based)}]}. Selected by tab_id / tab_index (default: give text and language instead).".into(),
            add(text_or_tab_props(), "language", s("Language for text input, e.g. \"Python\" (default: the tab's language)")),
            &[],
        ),
        tool(
            "generate_password",
            "Generate random passwords or passphrases with the Password Generator's engine (OS random source). The result is returned to this MCP client and therefore to the model provider behind it: do not use it for a secret the user must keep from that provider. Returns {passwords:[...]}.".into(),
            {
                let mut m = Map::new();
                m.insert("mode".into(), e(PW_MODES, "password (default) or passphrase"));
                m.insert("length".into(), json!({ "type": "integer", "minimum": 4, "maximum": 256, "description": "password: characters (default 20)" }));
                m.insert("classes".into(), json!({ "type": "array", "items": { "type": "string", "enum": PW_CLASSES }, "description": "password: character classes (default all four: lower, upper, digits, symbols)" }));
                m.insert("exclude_lookalikes".into(), b("password: drop 0 O 1 l I (default false)"));
                m.insert("require_each".into(), b("password: at least one of every class (default true)"));
                m.insert("words".into(), json!({ "type": "integer", "minimum": 3, "maximum": 24, "description": "passphrase: number of words (default 6)" }));
                m.insert("separator".into(), s("passphrase: one separator character (default '-')"));
                m.insert("capitalise".into(), b("passphrase: capitalise each word (default false)"));
                m.insert("append_digits".into(), b("passphrase: append two digits (default false)"));
                m.insert("count".into(), json!({ "type": "integer", "minimum": 1, "maximum": 100, "description": "How many to generate (default 1)" }));
                m
            },
            &[],
        ),
    ]
}

// ── handlers ────────────────────────────────────────────────────────────

pub(super) fn activate_tab(t: &mut dyn EditorTransport, a: &Map<String, Value>) -> CallOutcome {
    forward(t, "activate_tab", a, &TAB_SEL, TabRule::Required)
}

pub(super) fn close_tab(t: &mut dyn EditorTransport, a: &Map<String, Value>) -> CallOutcome {
    let spec = with(&TAB_SEL, [opt("discard_unsaved", Kind::Bool)]);
    forward(t, "close_tab", a, &spec, TabRule::Required)
}

pub(super) fn reload_tab(t: &mut dyn EditorTransport, a: &Map<String, Value>) -> CallOutcome {
    forward(t, "reload_tab", a, &TAB_SEL, TabRule::Optional)
}

pub(super) fn rename_file(t: &mut dyn EditorTransport, a: &Map<String, Value>) -> CallOutcome {
    let spec = with(&TAB_SEL, [req("new_name", Kind::Str)]);
    if let Some(n) = a.get("new_name").and_then(Value::as_str) {
        if n.contains('/') || n.contains('\\') || n.trim().is_empty() || n == "." || n == ".." {
            return bad("new_name must be a plain file name (no path separators)".into());
        }
    }
    forward(t, "rename_file", a, &spec, TabRule::Optional)
}

pub(super) fn save_all(t: &mut dyn EditorTransport, a: &Map<String, Value>) -> CallOutcome {
    forward(t, "save_all", a, &[], TabRule::None)
}

pub(super) fn set_read_only(t: &mut dyn EditorTransport, a: &Map<String, Value>) -> CallOutcome {
    let spec = with(&TAB_SEL, [req("on", Kind::Bool)]);
    forward(t, "set_read_only", a, &spec, TabRule::Optional)
}

pub(super) fn undo(t: &mut dyn EditorTransport, a: &Map<String, Value>) -> CallOutcome {
    let spec = with(&TAB_SEL, [opt("steps", Kind::Int(1, 100))]);
    forward(t, "undo", a, &spec, TabRule::Optional)
}

pub(super) fn redo(t: &mut dyn EditorTransport, a: &Map<String, Value>) -> CallOutcome {
    let spec = with(&TAB_SEL, [opt("steps", Kind::Int(1, 100))]);
    forward(t, "redo", a, &spec, TabRule::Optional)
}

pub(super) fn transform_lines(t: &mut dyn EditorTransport, a: &Map<String, Value>) -> CallOutcome {
    let spec = with(
        &TAB_SEL,
        [
            req("op", Kind::Enum(LINE_OPS)),
            opt("scope", Kind::Enum(SCOPES)),
        ],
    );
    forward(t, "transform_lines", a, &spec, TabRule::Optional)
}

pub(super) fn convert_case(t: &mut dyn EditorTransport, a: &Map<String, Value>) -> CallOutcome {
    let spec = with(
        &TAB_SEL,
        [
            req("mode", Kind::Enum(CASE_MODES)),
            opt("scope", Kind::Enum(SCOPES)),
        ],
    );
    forward(t, "convert_case", a, &spec, TabRule::Optional)
}

const MAX_LINE: i64 = i32::MAX as i64;

pub(super) fn comment_lines(t: &mut dyn EditorTransport, a: &Map<String, Value>) -> CallOutcome {
    let spec = with(
        &TAB_SEL,
        [
            req("start_line", Kind::Int(1, MAX_LINE)),
            req("end_line", Kind::Int(1, MAX_LINE)),
            opt("kind", Kind::Enum(COMMENT_KINDS)),
            opt("action", Kind::Enum(COMMENT_ACTIONS)),
        ],
    );
    if let Err(e) = check(a, &spec) {
        return e;
    }
    let s = a.get("start_line").and_then(Value::as_i64).unwrap_or(0);
    let e = a.get("end_line").and_then(Value::as_i64).unwrap_or(0);
    if e < s {
        return bad("end_line must be >= start_line".into());
    }
    forward(t, "comment_lines", a, &spec, TabRule::Optional)
}

pub(super) fn line_edit(t: &mut dyn EditorTransport, a: &Map<String, Value>) -> CallOutcome {
    let spec = with(
        &TAB_SEL,
        [
            req("line", Kind::Int(1, MAX_LINE)),
            req("op", Kind::Enum(LINE_EDIT_OPS)),
        ],
    );
    forward(t, "line_edit", a, &spec, TabRule::Optional)
}

pub(super) fn set_eol(t: &mut dyn EditorTransport, a: &Map<String, Value>) -> CallOutcome {
    let spec = with(
        &TAB_SEL,
        [req("eol", Kind::Enum(EOLS)), opt("convert", Kind::Bool)],
    );
    forward(t, "set_eol", a, &spec, TabRule::Optional)
}

pub(super) fn set_encoding(t: &mut dyn EditorTransport, a: &Map<String, Value>) -> CallOutcome {
    let spec = with(
        &TAB_SEL,
        [
            req("encoding", Kind::Enum(ENCODINGS)),
            opt("mode", Kind::Enum(ENC_MODES)),
            opt("discard_unsaved", Kind::Bool),
        ],
    );
    forward(t, "set_encoding", a, &spec, TabRule::Optional)
}

pub(super) fn bookmarks(t: &mut dyn EditorTransport, a: &Map<String, Value>) -> CallOutcome {
    let spec = with(
        &TAB_SEL,
        [
            req("action", Kind::Enum(BOOKMARK_ACTIONS)),
            opt("line", Kind::Int(1, MAX_LINE)),
        ],
    );
    if a.get("action").and_then(Value::as_str) == Some("toggle") && !a.contains_key("line") {
        return bad("toggle needs line".into());
    }
    forward(t, "bookmarks", a, &spec, TabRule::Optional)
}

pub(super) fn fold(t: &mut dyn EditorTransport, a: &Map<String, Value>) -> CallOutcome {
    let spec = with(&TAB_SEL, [req("action", Kind::Enum(FOLD_ACTIONS))]);
    forward(t, "fold", a, &spec, TabRule::Optional)
}

pub(super) fn set_view(t: &mut dyn EditorTransport, a: &Map<String, Value>) -> CallOutcome {
    let spec = [
        opt("word_wrap", Kind::Bool),
        opt("zoom", Kind::Int(6, 48)),
        opt("show_whitespace", Kind::Bool),
        opt("show_eol", Kind::Bool),
        opt("indent_guides", Kind::Bool),
    ];
    if a.is_empty() {
        return bad(
            "set_view needs at least one of word_wrap, zoom, show_whitespace, show_eol, \
             indent_guides"
                .into(),
        );
    }
    forward(t, "set_view", a, &spec, TabRule::None)
}

pub(super) fn set_theme(t: &mut dyn EditorTransport, a: &Map<String, Value>) -> CallOutcome {
    forward(
        t,
        "set_theme",
        a,
        &[req("theme", Kind::Enum(THEMES))],
        TabRule::None,
    )
}

pub(super) fn set_preview(t: &mut dyn EditorTransport, a: &Map<String, Value>) -> CallOutcome {
    let spec = with(&TAB_SEL, [req("on", Kind::Bool)]);
    forward(t, "set_preview", a, &spec, TabRule::Optional)
}

pub(super) fn compare_with_file(
    t: &mut dyn EditorTransport,
    a: &Map<String, Value>,
) -> CallOutcome {
    let spec = with(&TAB_SEL, [req("path", Kind::Str)]);
    forward(t, "compare_with_file", a, &spec, TabRule::Optional)
}

fn text_spec(more: impl IntoIterator<Item = Arg>) -> Vec<Arg> {
    with(
        &[
            opt("text", Kind::Str),
            opt("tab_id", Kind::Id),
            opt("tab_index", Kind::Index),
        ],
        more,
    )
}

pub(super) fn check_brackets(t: &mut dyn EditorTransport, a: &Map<String, Value>) -> CallOutcome {
    forward(t, "check_brackets", a, &text_spec([]), TabRule::TextOrTab)
}

pub(super) fn fix_brackets(t: &mut dyn EditorTransport, a: &Map<String, Value>) -> CallOutcome {
    forward(t, "fix_brackets", a, &text_spec([]), TabRule::TextOrTab)
}

pub(super) fn hash_text(t: &mut dyn EditorTransport, a: &Map<String, Value>) -> CallOutcome {
    let spec = text_spec([opt("algo", Kind::Enum(HASH_ALGOS))]);
    forward(t, "hash_text", a, &spec, TabRule::TextOrTab)
}

pub(super) fn encode_text(t: &mut dyn EditorTransport, a: &Map<String, Value>) -> CallOutcome {
    let spec = text_spec([req("op", Kind::Enum(ENCODE_OPS))]);
    forward(t, "encode_text", a, &spec, TabRule::TextOrTab)
}

pub(super) fn diff_text(t: &mut dyn EditorTransport, a: &Map<String, Value>) -> CallOutcome {
    let spec = [req("left", Kind::Object), req("right", Kind::Object)];
    if let Err(e) = check(a, &spec) {
        return e;
    }
    for side in ["left", "right"] {
        let Some(Value::Object(m)) = a.get(side) else {
            continue;
        };
        if let Err(CallOutcome::InvalidParams(msg)) = check(m, &text_spec([]))
            .and_then(|()| one_of(m, &["text", "tab_id", "tab_index"], true))
        {
            return bad(format!("{side}: {msg}"));
        }
    }
    forward(t, "diff_text", a, &spec, TabRule::None)
}

pub(super) fn repair_json(t: &mut dyn EditorTransport, a: &Map<String, Value>) -> CallOutcome {
    forward(t, "repair_json", a, &text_spec([]), TabRule::TextOrTab)
}

pub(super) fn markdown_convert(t: &mut dyn EditorTransport, a: &Map<String, Value>) -> CallOutcome {
    let spec = text_spec([req("op", Kind::Enum(MD_OPS))]);
    forward(t, "markdown_convert", a, &spec, TabRule::TextOrTab)
}

pub(super) fn mermaid_to_npd(t: &mut dyn EditorTransport, a: &Map<String, Value>) -> CallOutcome {
    forward(t, "mermaid_to_npd", a, &text_spec([]), TabRule::TextOrTab)
}

pub(super) fn list_symbols(t: &mut dyn EditorTransport, a: &Map<String, Value>) -> CallOutcome {
    let spec = text_spec([opt("language", Kind::Str)]);
    forward(t, "list_symbols", a, &spec, TabRule::TextOrTab)
}

pub(super) fn generate_password(
    t: &mut dyn EditorTransport,
    a: &Map<String, Value>,
) -> CallOutcome {
    let spec = [
        opt("mode", Kind::Enum(PW_MODES)),
        opt("length", Kind::Int(4, 256)),
        opt("classes", Kind::EnumArray(PW_CLASSES)),
        opt("exclude_lookalikes", Kind::Bool),
        opt("require_each", Kind::Bool),
        opt("words", Kind::Int(3, 24)),
        opt("separator", Kind::Str),
        opt("capitalise", Kind::Bool),
        opt("append_digits", Kind::Bool),
        opt("count", Kind::Int(1, 100)),
    ];
    if let Some(sep) = a.get("separator").and_then(Value::as_str) {
        if sep.chars().count() != 1 {
            return bad("separator must be a single character".into());
        }
    }
    forward(t, "generate_password", a, &spec, TabRule::None)
}
