// SPDX-License-Identifier: GPL-3.0-or-later
//
// ═══════════════════════════════════════════════════════════════════════
// WIRE CONTRACT — SINGLE SOURCE OF TRUTH (C++ side).
//
// The Rust sidecar (notepatra-mcp) conforms to THIS list byte-exactly. Every
// request is one line of JSON: {"id":<int>,"verb":"<name>","args":{...}}.
// Every response is one line: {"id","ok":true,"result":{...}} on success or
// {"id","ok":false,"error":"<message>"} on failure. A greeting object
// {"notepatra_mcp":1,"app":"Notepatra","version":"..."} is always the FIRST
// line the peer receives. Tabs are addressed by 0-based index; the Welcome
// tab (when present) owns index 0.
//
// v0.1.126: every verb that names a tab ALSO accepts "tab_id" — the stable id
// published by list_open_tabs. An index is positional and re-points at a
// different document the moment another tab closes, so a caller that holds a
// selector across calls must hold the id. See McpBridge::resolveTab.
//
// READ tier — answered immediately.
//   list_open_tabs {}                       → {tabs:[{index,id,title,path,
//                                              modified,editable}]}
//                                              (editable: v0.1.121, id: v0.1.126)
//   read_tab    {tab_id|index|title, max_bytes?} → {title,path,id?,text,
//                                              truncated,total_chars}
//   get_selection {}                        → {text,tab_index}
//   get_status  {}                          → {tab_index,title,path,language,
//                                              encoding,cursor_line,cursor_col,
//                                              edition,version}
//   app_info    {}                          → {name,version,edition,platform}
//   list_recent_files {}                    → {files:[...]}
//   find_in_tab {query,tab_id?|index?|title?,regex?}
//                                           → {matches:[{line,text}],truncated}
//   search_project {query,max_results?,regex?} → {results:[{path,line,text}],
//                                              truncated,workspace_searched,
//                                              scope,notice?}
//                                              (zero matches is a SUCCESS with
//                                               an empty results list)
//   list_notes  {}                          → {notes:[{title,file,modified_iso}]}
//   read_note   {file}                      → {title,text}
//   compare_tabs{index_a,index_b}           → {opened}      (opens non-modal view)
//   format_text {kind("json"|"sql"|"html"),text} → {text}
//   list_reminders {}                       → {reminders:[{note_file,note_title,
//                                              due_iso,bucket}]}   (v0.1.119)
//   git_status {}                           → {output}            (v0.1.119)
//   git_diff   {path?}                      → {output}            (v0.1.119)
//   git_log    {limit?(=20,cap 100)}        → {output}            (v0.1.119)
//   git_show   {ref}                        → {output}            (v0.1.119)
//   git_branch {}                           → {output}            (v0.1.119)
//   validate_npd {tab_index|source}         → {valid,errors:[{line,message}]}
//                                                                  (v0.1.119)
//   run_sql    {sql,csv_path?}              → {columns:[...],rows:[[...]],
//                                              truncated,engine}   (v0.1.119)
//   list_languages {}                       → {languages:[...]}    (p0a)
//   get_capabilities {}                     → {edition,platform,version,
//                                              features:{duckdb,webengine,
//                                              noter}}              (p0a)
//                                             (the Rust sidecar augments the
//                                              tool result with tool_count
//                                              and tiers — derived there)
//   get_diagram_source {tab_index}          → {source}             (phase 1)
//   list_connections {}                     → {connections:[{name,driver,
//                                              database,read_only}]} (phase 2)
//   run_query  {connection_name,sql,max_rows?(=200,cap 200)}
//                                           → {columns,rows,truncated,engine} (phase 2)
//   list_tables{connection_name}            → {tables:[...]}         (phase 2)
//
// ACT tier — visible, non-destructive, NO approval card.
//   new_tab     {text?}                     → {tab_index}
//   goto_line   {line,tab_index?}           → {ok,tab_index,line,
//                                              requested_line,clamped}
//                                             `line` = where the cursor
//                                             LANDED (clamped to the
//                                             buffer); clamped:true when it
//                                             differs from requested_line.
//   select_range{start_line,start_col,end_line,end_col,tab_index?}
//                                           → {ok,tab_index}   (1-based line
//                                              +col; v0.1.121)
//   set_language{language,tab_index?}       → {ok,tab_index,language}
//                                             (language echoes the RESOLVED
//                                              canonical token since p0a)
//   open_note   {file}                      → {opened,title}       (v0.1.119)
//   create_diagram {source?,title?}         → {tab_index,valid,
//                                              errors:[{line,message}]}  (phase 1)
//   open_noter  {}                          → {opened}             (phase 1)
//   open_data_analyst {}                    → {opened}             (phase 2)
//   render_chart {spec,title?}              → {chart_id,rendered}   (phase 2,
//                                              Full/WebEngine only)
//
// WRITE tier — held behind the in-window human-approval card; a mutation
// runs ONLY on Approve (Deny / timeout / disconnect ⇒ nothing happened).
//   insert_text {text,tab_index?,line?,col?} → {tab_index}
//   replace_selection {text,tab_index?}      → {tab_index}
//   apply_edit  {find,replace,all?,tab_index?} → {count}
//   save_tab    {tab_index?,path?}           → {saved,tab_index,path}
//                                             (path = "Save As" to a NEW
//                                              absolute path; v0.1.121)
//   create_note {title,body}                 → {file,title}        (v0.1.119)
//   append_note {file,text}                  → {file}              (v0.1.119)
//   set_reminder{file,due_iso}               → {file,due_iso}      (v0.1.119)
//   export_diagram {tab_index,path,format("png"|"pdf")} → {path}   (v0.1.119)
//   set_diagram_source {tab_index,source}   → {ok,tab_index,valid,
//                                              errors:[{line,message}]}  (phase 1)
//   export_query_results {connection_name,sql,path,format("csv"|"json"),
//                         max_rows?(=10000,cap 100000)} → {ok,path,rows} (phase 2)
//   export_chart {spec,path,format("png"|"svg"|"html"|"spec"),scale?(=2,1..4)}
//                                           → {path}                 (phase 2,
//                                              Full/WebEngine only)
//
// v0.1.133 — tab selector everywhere: tab_id | tab_index (title for reads).
// Results that name a tab carry tab_index AND tab_id. find_in_tab and
// search_project take match_case?/whole_word? and report a 1-based column;
// apply_edit takes regex?/match_case?/whole_word?/scope("tab"|"all_open").
//   READ  check_brackets/fix_brackets/hash_text{algo}/encode_text{op}/
//         repair_json/markdown_convert{op}/mermaid_to_npd/list_symbols
//         {text | tab_id | tab_index}; diff_text{left,right:{text|tab}};
//         generate_password{mode,length,...}; format_text also takes
//         indent?/minify?(json)/keyword_case?(sql).
//   ACT   activate_tab, set_read_only{on}, bookmarks{action,line?},
//         fold{action}, set_preview{on}, compare_with_file{path},
//         set_view{word_wrap?,zoom?,show_whitespace?,show_eol?,
//         indent_guides?}, set_theme{theme}; new_tab{language?,title?};
//         create_diagram{template?}.
//   WRITE close_tab{discard_unsaved?} and reload_tab (card only when unsaved
//         changes would be lost), rename_file{new_name}, save_all,
//         undo/redo{steps?}, transform_lines{op,scope?},
//         convert_case{mode,scope?}, comment_lines{start_line,end_line,
//         kind?,action?}, line_edit{line,op}, set_eol{eol,convert?},
//         set_encoding{encoding,mode?,discard_unsaved?}.
//
// NOTE: write verbs take "tab_index"; read tab-arg verbs keep their existing
// keys ("index" for read_tab/find_in_tab). This asymmetry is deliberate and
// load-bearing — do not "unify" it. "tab_id" (v0.1.126) is spelled the same
// everywhere and takes precedence over either.
//
// Files matching PathDenylist::isSecretPath are refused by open_file,
// read_tab and find_in_tab, and skipped by both legs of search_project.
// ═══════════════════════════════════════════════════════════════════════
#include "mcp_bridge.h"
#include "build_flavor.h"
#include "config.h"
#include "diagram/npd_parser.h"
#include "notes_storage.h"
#include "path_denylist.h"
#include "singleinstance.h"
#include "theme_detect.h"

#include <QApplication>
#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QDirIterator>
#include <QEvent>
#include <QFile>
#include <QFileInfo>
#include <QFrame>
#include <QFutureWatcher>
#include <QHostAddress>
#include <QHBoxLayout>
#include <QJsonDocument>
#include <QJsonValue>
#include <QLabel>
#include <QLocalServer>
#include <QLocalSocket>
#include <QPointer>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QRegularExpression>
#include <QSaveFile>
#include <QSet>
#include <QTimer>
#include <QVBoxLayout>
#include <QWidget>
#include <QtConcurrent/QtConcurrentRun>

#include <climits>

#ifndef NOTEPATRA_VERSION
#define NOTEPATRA_VERSION "0.0.0-dev"
#endif

namespace {
// One request line must fit here; a client that streams more without a
// newline is broken and gets a parse error + queued disconnect.
constexpr int kMaxRequestLineBytes = 4 * 1024 * 1024;
// read_tab text cap (QString characters ≈ bytes for ASCII-dominant text).
constexpr int kReadTabCapChars = 5 * 1024 * 1024;
// search_project hard caps.
constexpr int kMaxScannedFiles = 2000;
constexpr int kMaxSearchResults = 200;
constexpr qint64 kMaxSearchFileBytes = 10 * 1024 * 1024;
constexpr int kMaxLineScanBytes = 64 * 1024;
// Result line text is a preview, not a transfer channel.
constexpr int kResultLinePreviewChars = 512;
// find_in_tab hard caps (v0.1.118).
constexpr int kMaxFindMatches = 500;
// read_note plaintext source cap (v0.1.118).
constexpr qint64 kMaxNoteBytes = 2 * 1024 * 1024;
// Approval-card preview / in-description elision caps (v0.1.118 write tier).
constexpr int kApprovalPreviewChars = 200;
constexpr int kApprovalDescChars = 60;
// v0.1.119 depth wave.
constexpr int kGitOutputCap = 256 * 1024;          // git_* output ceiling
constexpr int kMaxSqlRows = 200;                   // run_sql row cap
constexpr int kMaxSqlCells = 1024;                 // per-cell char cap
constexpr int kAppendNoteMaxChars = 1 * 1024 * 1024;
constexpr int kCreateNoteMaxChars = 1 * 1024 * 1024;
constexpr int kDiagramSourceMaxChars = 1 * 1024 * 1024; // create/set_diagram_source
// Phase 2 — data-analyst + charts.
constexpr int kExportRowsDefault = 10000;   // export_query_results default cap
constexpr int kExportRowsMax = 100000;      // export_query_results hard cap
constexpr int kChartScaleDefault = 2;       // export_chart png scale

QString elideForCard(const QString &s, int cap) {
    if (s.size() <= cap) return s;
    return s.left(cap) + QChar(0x2026);
}

// Every character the SQL gate (and PostgreSQL, SQL Server, ...) treats as
// the end of a line: a `--` comment stops at any of them.
bool isCardLineBreak(ushort u) {
    return u == 0x0A || u == 0x0D || u == 0x0B || u == 0x0C || u == 0x85 ||
           u == 0x2028 || u == 0x2029;
}

// The visible stand-in for a character that would otherwise be invisible.
QString cardEscape(ushort u) {
    switch (u) {
    case 0x09: return QStringLiteral("\\t");
    case 0x0A: return QStringLiteral("\\n");
    case 0x0B: return QStringLiteral("\\v");
    case 0x0C: return QStringLiteral("\\f");
    case 0x0D: return QStringLiteral("\\r");
    default: break;
    }
    if (u <= 0xFF) return QStringLiteral("\\x%1").arg(u, 2, 16, QLatin1Char('0'));
    return QStringLiteral("\\u%1").arg(u, 4, 16, QLatin1Char('0'));
}

// Make every control and format character VISIBLE before a string is shown
// on an approval card, so crafted text can't disguise what runs: a bare CR
// that would end a SQL comment shows as \r, an RLO that reverses a path's
// tail shows as ‮. Nothing is deleted (deleting a CR is what made
// "-- note\rOR 1=1" read as one commented-out line). Used for display text
// ONLY; the executed value is never passed through this.
QString sanitizeForCard(const QString &s) {
    QString out;
    out.reserve(s.size());
    for (const QChar ch : s) {
        const ushort u = ch.unicode();
        // C0 controls, DEL + C1 controls, the Unicode line/paragraph
        // separators, and every format character (bidi overrides and
        // isolates, zero-width characters, BOM, soft hyphen).
        if (u <= 0x1F || (u >= 0x7F && u <= 0x9F) || isCardLineBreak(u) ||
            ch.category() == QChar::Other_Format)
            out += cardEscape(u);
        else
            out.append(ch);
    }
    return out;
}

// Edition axis from build_flavor.h — same booleans the updater's asset
// picker trusts, so the bridge can never disagree with the binary.
const char *editionName() {
#if NOTEPATRA_BUILD_IS_FULL
    return "Full";
#else
    return "Lite";
#endif
}

const char *platformName() {
#if defined(Q_OS_WIN)
    return "windows";
#elif defined(Q_OS_MACOS)
    return "macos";
#elif defined(Q_OS_LINUX)
    return "linux";
#else
    return "other";
#endif
}

// v0.1.126 (NP-05): the credential deny-list guards EVERY door into file
// content, not just the workspace walk below.
//
// search_project's filesystem leg has consulted PathDenylist since v0.1.125,
// but open_file and read_tab did not — so an agent that asked for
// ~/.ssh/id_rsa by name got the tab opened, then got the whole key body back
// from read_tab with isError:false. Guarding the search leg alone only meant
// the model had to ask for the file directly instead of stumbling on it.
//
// Returns a refusal message, or "" when the path is fine. The message names
// the file but never quotes a byte of it.
QString credentialRefusal(const QString &path) {
    if (path.isEmpty()) return QString(); // untitled buffer — nothing on disk
    const QFileInfo fi(path);
    // Check the absolute form: a relative path would slip every segment-
    // anchored rule in the deny-list.
    const QString abs = fi.absoluteFilePath();
    // A symlink is judged by its target too (notes.txt -> ~/.ssh/id_rsa).
    const QString real = fi.canonicalFilePath();
    if (!PathDenylist::isSecretPath(abs)
        && (real.isEmpty() || !PathDenylist::isSecretPath(real)))
        return QString();
    return QStringLiteral(
               "refusing to read %1: it matches the credential deny-list "
               "(SSH/GPG/AWS keys, .env, keystores, Terraform state). Open it "
               "in the editor yourself if you need to see it.")
        .arg(QDir::toNativeSeparators(fi.fileName()));
}

// v0.1.133: the one line matcher find_in_tab and search_project share.
// Defaults keep what shipped: a literal is case-INsensitive, a regex is
// case-sensitive; match_case overrides either. Matched once per (capped)
// line, never a match(str, offset) loop over a buffer.
struct LineMatcher {
    QString query;
    bool regex = false;
    bool matchCase = false;
    bool wholeWord = false;
    QRegularExpression re;
    // 0-based column of the first match in `line`, -1 when none.
    int find(const QString &line) const {
        if (regex) {
            const QRegularExpressionMatch m = re.match(line);
            return m.hasMatch() ? m.capturedStart() : -1;
        }
        const Qt::CaseSensitivity cs =
            matchCase ? Qt::CaseSensitive : Qt::CaseInsensitive;
        const auto isWord = [](QChar c) {
            return c.isLetterOrNumber() || c == QLatin1Char('_');
        };
        for (int at = line.indexOf(query, 0, cs); at >= 0;
             at = line.indexOf(query, at + 1, cs)) {
            if (!wholeWord) return at;
            const int end = at + query.size();
            if ((at == 0 || !isWord(line.at(at - 1))) &&
                (end >= line.size() || !isWord(line.at(end))))
                return at;
        }
        return -1;
    }
};

// Builds the matcher from {query,regex?,match_case?,whole_word?}; returns
// "" on success or the error to send.
QString makeLineMatcher(const QJsonObject &args, LineMatcher *m) {
    m->query = args.value(QLatin1String("query")).toString();
    m->regex = args.value(QLatin1String("regex")).toBool(false);
    for (const char *k : {"match_case", "whole_word"})
        if (args.contains(QLatin1String(k)) &&
            !args.value(QLatin1String(k)).isBool())
            return QStringLiteral("%1 must be a boolean").arg(QLatin1String(k));
    m->matchCase = args.value(QLatin1String("match_case")).toBool(m->regex);
    m->wholeWord = args.value(QLatin1String("whole_word")).toBool(false);
    if (m->regex) {
        const QString pat = m->wholeWord
                                ? QStringLiteral("\\b(?:%1)\\b").arg(m->query)
                                : m->query;
        QRegularExpression::PatternOptions o =
            QRegularExpression::UseUnicodePropertiesOption;
        if (!m->matchCase) o |= QRegularExpression::CaseInsensitiveOption;
        m->re = QRegularExpression(pat, o);
        if (!m->re.isValid())
            return QStringLiteral("invalid regex: %1").arg(m->re.errorString());
        m->re.optimize();
    }
    return QString();
}

// v0.1.133 argument helpers: one wording for every verb.
QString optBool(const QJsonObject &a, const char *k, bool def, bool *out) {
    const QLatin1String key(k);
    *out = def;
    if (!a.contains(key)) return QString();
    if (!a.value(key).isBool())
        return QStringLiteral("%1 must be a boolean").arg(key);
    *out = a.value(key).toBool();
    return QString();
}

QString optInt(const QJsonObject &a, const char *k, int lo, int hi, int def,
               bool required, int *out) {
    const QLatin1String key(k);
    *out = def;
    if (!a.contains(key))
        return required ? QStringLiteral("missing %1").arg(key) : QString();
    const QJsonValue v = a.value(key);
    const double d = v.toDouble(-1e18);
    if (!v.isDouble() || d != double(qint64(d)) || d < lo || d > hi)
        return QStringLiteral("%1 must be an integer from %2 to %3")
            .arg(key).arg(lo).arg(hi);
    *out = int(d);
    return QString();
}

// def empty ⇒ required. Returns the canonical spelling from `allowed`.
QString optEnum(const QJsonObject &a, const char *k, const QStringList &allowed,
                const QString &def, QString *out) {
    const QLatin1String key(k);
    *out = def;
    if (!a.contains(key))
        return def.isEmpty()
                   ? QStringLiteral("missing %1 (%2)")
                         .arg(key, allowed.join(QLatin1Char('|')))
                   : QString();
    const QString v = a.value(key).toString();
    for (const QString &x : allowed)
        if (x.compare(v, Qt::CaseInsensitive) == 0) {
            *out = x;
            return QString();
        }
    return QStringLiteral("%1 must be one of %2")
        .arg(key, allowed.join(QLatin1Char('|')));
}

const QStringList &kLineOps() {
    static const QStringList l = {
        QStringLiteral("sort_asc"),      QStringLiteral("sort_desc"),
        QStringLiteral("sort_int_asc"),  QStringLiteral("sort_int_desc"),
        QStringLiteral("sort_len_asc"),  QStringLiteral("sort_len_desc"),
        QStringLiteral("dedupe"),        QStringLiteral("dedupe_consecutive"),
        QStringLiteral("remove_empty"),  QStringLiteral("remove_blank"),
        QStringLiteral("join"),          QStringLiteral("reverse"),
        QStringLiteral("trim_trailing"), QStringLiteral("trim_leading"),
        QStringLiteral("trim_both"),     QStringLiteral("tab_to_space"),
        QStringLiteral("space_to_tab")};
    return l;
}

// The Encoding menu's own list (Reinterpret bytes as / Convert to).
const QStringList &kEncodings() {
    static const QStringList l = {
        QStringLiteral("UTF-8"),         QStringLiteral("UTF-8 BOM"),
        QStringLiteral("UTF-16 LE"),     QStringLiteral("UTF-16 LE BOM"),
        QStringLiteral("UTF-16 BE"),     QStringLiteral("UTF-16 BE BOM"),
        QStringLiteral("UTF-32 LE BOM"), QStringLiteral("UTF-32 BE BOM"),
        QStringLiteral("ANSI (Windows-1252)"), QStringLiteral("ISO-8859-1")};
    return l;
}

constexpr int kComputeMaxChars = 5 * 1024 * 1024;

// v0.1.133 Phase 3 verb families (the sidecar's tool names, one each).
const QSet<QString> &kGitVerbs() {
    static const QSet<QString> s = {
        QStringLiteral("list_hunks"),        QStringLiteral("list_conflicts"),
        QStringLiteral("git_stage"),         QStringLiteral("git_unstage"),
        QStringLiteral("git_commit"),        QStringLiteral("git_branch_create"),
        QStringLiteral("git_checkout"),      QStringLiteral("git_stash"),
        QStringLiteral("git_discard"),       QStringLiteral("git_fetch"),
        QStringLiteral("git_pull"),          QStringLiteral("git_push"),
        QStringLiteral("git_hunk"),          QStringLiteral("resolve_conflict")};
    return s;
}
const QSet<QString> &kNoterVerbs() {
    static const QSet<QString> s = {
        QStringLiteral("search_notes"),      QStringLiteral("rename_note"),
        QStringLiteral("trash_note"),        QStringLiteral("restore_note"),
        QStringLiteral("delete_note_permanently"),
        QStringLiteral("delete_reminder"),   QStringLiteral("snooze_reminder"),
        QStringLiteral("dismiss_reminder"),  QStringLiteral("export_note")};
    return s;
}
const QSet<QString> &kSettingsVerbs() {
    static const QSet<QString> s = {QStringLiteral("get_preferences"),
                                    QStringLiteral("set_preference"),
                                    QStringLiteral("set_workspace_folder")};
    return s;
}
const QSet<QString> &kDataVerbs() {
    static const QSet<QString> s = {
        QStringLiteral("describe_table"),    QStringLiteral("test_connection"),
        QStringLiteral("execute_sql"),       QStringLiteral("add_connection"),
        QStringLiteral("update_connection"), QStringLiteral("delete_connection"),
        QStringLiteral("open_connections_dialog"),
        QStringLiteral("open_schema_browser")};
    return s;
}
const QSet<QString> &kRestVerbs() {
    static const QSet<QString> s = {
        QStringLiteral("parse_http_file"), QStringLiteral("curl_to_request"),
        QStringLiteral("request_to_curl"), QStringLiteral("rest_request")};
    return s;
}

// Phase 3 caps.
constexpr int kMaxPathsPerCall = 100;
constexpr int kCommitMessageMax = 10000;
constexpr int kHexDumpMax = 64 * 1024;
constexpr int kRestBodyMax = 10 * 1024 * 1024;

// A JSON array of 1..kMaxPathsPerCall non-empty strings.
QString pathsArg(const QJsonObject &a, QStringList *out) {
    const QJsonValue v = a.value(QLatin1String("paths"));
    if (!v.isArray() || v.toArray().isEmpty())
        return QStringLiteral("paths must be a non-empty array of file paths");
    const QJsonArray arr = v.toArray();
    if (arr.size() > kMaxPathsPerCall)
        return QStringLiteral("at most %1 paths per call").arg(kMaxPathsPerCall);
    for (const QJsonValue &p : arr) {
        if (!p.isString() || p.toString().isEmpty())
            return QStringLiteral("paths must be a non-empty array of file paths");
        if (!out->contains(p.toString())) out->append(p.toString());
    }
    return QString();
}

QString reqString(const QJsonObject &a, const char *k, QString *out) {
    const QLatin1String key(k);
    const QJsonValue v = a.value(key);
    if (!v.isString() || v.toString().trimmed().isEmpty())
        return QStringLiteral("missing %1").arg(key);
    *out = v.toString();
    return QString();
}

// sanitizeForCard per line. EVERY line terminator the SQL gate knows (CR,
// LF, CRLF, VT, FF, NEL, LS, PS) becomes a real line break on the card, so
// what looks like a new line is one, and what looks like one line is one.
QString sanitizeMultiline(const QString &s) {
    QStringList lines;
    QString cur;
    for (int i = 0; i < s.size(); ++i) {
        const ushort u = s.at(i).unicode();
        if (isCardLineBreak(u)) {
            lines << cur;
            cur.clear();
            if (u == 0x0D && i + 1 < s.size() && s.at(i + 1).unicode() == 0x0A) ++i; // CRLF
            continue;
        }
        cur.append(s.at(i));
    }
    lines << cur;
    for (QString &l : lines)
        l = sanitizeForCard(QString(l).replace(QLatin1Char('\t'), QLatin1String("    ")));
    return lines.join(QLatin1Char('\n'));
}

// The whole of a client-supplied text for a scrollable card preview. Small
// texts are shown entire; a huge one shows its size, its first and last
// lines and an explicit marker for what is left out (bracketed, on its own
// line, so it cannot pass for content).
constexpr int kFullPreviewMaxChars = 200 * 1024;
constexpr int kFullPreviewEdgeLines = 200;
// *note (when the text is cut) says so in words for the card's warning
// line, which the client cannot write into.
QString fullTextPreview(const QString &text, QString *note = nullptr) {
    const QString clean = sanitizeMultiline(text);
    if (clean.size() <= kFullPreviewMaxChars) return clean;
    const QStringList lines = clean.split(QLatin1Char('\n'));
    if (lines.size() <= 2 * kFullPreviewEdgeLines) {
        // Few, very long lines: cut by characters instead.
        const int edge = kFullPreviewMaxChars / 2;
        const int hidden = clean.size() - 2 * edge;
        if (note)
            *note = QStringLiteral("The text is %1 characters long; the preview leaves out %2 "
                                   "characters in the middle.").arg(clean.size()).arg(hidden);
        return clean.left(edge) +
               QStringLiteral("\n[... %1 characters not shown ...]\n").arg(hidden) +
               clean.right(edge);
    }
    const int hidden = lines.size() - 2 * kFullPreviewEdgeLines;
    if (note)
        *note = QStringLiteral("The text is %1 lines (%2 characters) long; the preview leaves "
                               "out %3 lines in the middle.")
                    .arg(lines.size()).arg(clean.size()).arg(hidden);
    return lines.mid(0, kFullPreviewEdgeLines).join(QLatin1Char('\n')) +
           QStringLiteral("\n[... %1 lines not shown ...]\n").arg(hidden) +
           lines.mid(lines.size() - kFullPreviewEdgeLines).join(QLatin1Char('\n'));
}

QString bulletList(const QStringList &items, int cap = 50) {
    QStringList lines;
    for (int i = 0; i < items.size() && i < cap; ++i)
        lines << QStringLiteral("- ") + sanitizeForCard(items.at(i));
    if (items.size() > cap)
        lines << QStringLiteral("... and %1 more").arg(items.size() - cap);
    return lines.join(QLatin1Char('\n'));
}

QStringList jsonStrings(const QJsonValue &v) {
    QStringList out;
    for (const QJsonValue &x : v.toArray()) out << x.toString();
    return out;
}

// Git ref-name rules (git check-ref-format --branch), checked before a card.
bool validBranchName(const QString &n) {
    static const QRegularExpression ok(QStringLiteral("^[A-Za-z0-9._/+-]+$"));
    if (n.isEmpty() || n.size() > 200 || !ok.match(n).hasMatch()) return false;
    if (n.startsWith(QLatin1Char('-')) || n.startsWith(QLatin1Char('/')) ||
        n.endsWith(QLatin1Char('/')) || n.endsWith(QLatin1Char('.')) ||
        n.endsWith(QLatin1String(".lock")) || n.contains(QLatin1String("..")) ||
        n.contains(QLatin1String("//")) || n.contains(QLatin1String("/.")) ||
        n.startsWith(QLatin1Char('.')) || n == QLatin1String("HEAD"))
        return false;
    return true;
}

struct SearchHit {
    QString path;
    int line;
    QString text;
    int column = 0; // 1-based, v0.1.133
};

QJsonArray hitsToJson(const QVector<SearchHit> &hits) {
    QJsonArray arr;
    for (const SearchHit &h : hits) {
        QJsonObject o;
        o[QStringLiteral("path")] = h.path;
        o[QStringLiteral("line")] = h.line;
        o[QStringLiteral("column")] = h.column;
        o[QStringLiteral("text")] = h.text.left(kResultLinePreviewChars);
        arr.append(o);
    }
    return arr;
}

// Filesystem leg of search_project. Runs on a QtConcurrent worker thread:
// touches only value copies, never the bridge, never a socket. When
// `useRegex` is set, `query` is a QRegularExpression pattern (already
// validated on the GUI thread before dispatch); it is compiled ONCE here and
// matched at most once per line — every line is already capped at
// kMaxLineScanBytes, so a megabyte one-liner never reaches the matcher.
QJsonObject scanWorkspace(const QString &root, const LineMatcher &matcher,
                          const QSet<QString> &skipPaths,
                          const QVector<SearchHit> &bufferHits,
                          int maxResults) {
    QVector<SearchHit> hits = bufferHits;
    bool truncated = false;
    int scanned = 0;
    const QString rootCanon = QDir(root).canonicalPath();
    const QString rootPrefix = rootCanon.endsWith(QLatin1Char('/')) ? rootCanon
                                                                    : rootCanon + QLatin1Char('/');
    QDirIterator it(root, QDir::Files | QDir::NoDotAndDotDot | QDir::Readable,
                    QDirIterator::Subdirectories);
    while (it.hasNext() && hits.size() < maxResults) {
        const QString path = it.next();
        if (++scanned > kMaxScannedFiles) {
            truncated = true;
            break;
        }
        const QFileInfo fi = it.fileInfo();
        if (fi.size() > kMaxSearchFileBytes) continue;
        // Skip VCS / dependency / build trees, the same set populateAiContext
        // already excludes. These hold thousands of files that burn the scan
        // budget for no user benefit, and .git in particular can echo the
        // contents of files the user never opened.
        {
            const QString rel = QDir(root).relativeFilePath(path);
            static const char *kSkipDirs[] = {".git/",   "node_modules/",
                                              "build/",  "target/",
                                              ".venv/",  "__pycache__/"};
            bool skip = false;
            for (const char *d : kSkipDirs) {
                const QString seg = QLatin1String(d);
                if (rel.startsWith(seg) || rel.contains(QLatin1Char('/') + seg)) {
                    skip = true;
                    break;
                }
            }
            if (skip) continue;
        }
        if (skipPaths.contains(fi.absoluteFilePath())) continue;
        // The AI file tools refuse to READ ~/.ssh/id_rsa, *.pem, .aws/
        // credentials and friends (AiTools::isHardDenied). search_project
        // walks the same tree with no such check, so a one-word query could
        // return the matching LINES out of exactly those files — the deny-list
        // was guarding the front door while this verb held the back one open.
        // Same list, same thread-safe pure function, applied per file.
        if (PathDenylist::isSecretPath(fi.absoluteFilePath())) continue;
        // Judge (and read) the symlink TARGET: it must stay in the root and off the list.
        const QString real = fi.canonicalFilePath();
        if (real.isEmpty() || rootCanon.isEmpty() || !real.startsWith(rootPrefix)
            || PathDenylist::isSecretPath(real))
            continue;
        QFile f(real);
        if (!f.open(QIODevice::ReadOnly)) continue;
        if (f.peek(8192).contains('\0')) continue; // binary-looking
        int lineNo = 0;
        bool skippingLongLine = false;
        while (!f.atEnd() && hits.size() < maxResults) {
            const QByteArray chunk = f.readLine(kMaxLineScanBytes + 1);
            if (chunk.isEmpty()) break;
            const bool lineComplete = chunk.endsWith('\n');
            if (skippingLongLine) {
                if (lineComplete) skippingLongLine = false;
                continue;
            }
            ++lineNo;
            if (!lineComplete && chunk.size() > kMaxLineScanBytes) {
                skippingLongLine = true; // over the per-line scan cap
                continue;
            }
            // Matched against the trimmed line, as it always was (a regex
            // anchor's meaning must not change); the column counts the
            // stripped indentation back in.
            const QString raw = QString::fromUtf8(chunk);
            const QString line = raw.trimmed();
            const int col = matcher.find(line);
            if (col >= 0) {
                int lead = 0;
                while (lead < raw.size() && raw.at(lead).isSpace()) ++lead;
                hits.append({fi.absoluteFilePath(), lineNo, line, lead + col + 1});
            }
        }
    }
    if (hits.size() >= maxResults) truncated = true;
    QJsonObject result;
    result[QStringLiteral("results")] = hitsToJson(hits);
    result[QStringLiteral("truncated")] = truncated;
    // Reaching here means the workspace really was walked. Every search_project
    // response carries these two fields so the caller never has to infer scope
    // from the result count.
    result[QStringLiteral("workspace_searched")] = true;
    result[QStringLiteral("scope")] = QStringLiteral("tabs_and_workspace");
    return result;
}

// Defensive re-cap of a {columns,rows,truncated,engine} result: clamp to
// kMaxSqlRows and truncate any string cell over kMaxSqlCells (shared by
// run_sql and the phase-2 run_query so both enforce the same wire ceiling).
void capSqlResultRows(QJsonObject &result) {
    QJsonArray rows = result.value(QLatin1String("rows")).toArray();
    bool truncated = result.value(QLatin1String("truncated")).toBool(false);
    if (rows.size() > kMaxSqlRows) {
        QJsonArray capped;
        for (int i = 0; i < kMaxSqlRows; ++i) capped.append(rows.at(i));
        rows = capped;
        truncated = true;
    }
    for (int ri = 0; ri < rows.size(); ++ri) {
        QJsonArray row = rows.at(ri).toArray();
        bool changed = false;
        for (int ci = 0; ci < row.size(); ++ci) {
            const QString cell = row.at(ci).toString();
            if (cell.size() > kMaxSqlCells) {
                row.replace(ci, cell.left(kMaxSqlCells) + QChar(0x2026)
                                    + QStringLiteral("[truncated]"));
                changed = true;
            }
        }
        if (changed) {
            rows.replace(ri, row);
            truncated = true;
        }
    }
    result[QStringLiteral("rows")] = rows;
    result[QStringLiteral("truncated")] = truncated;
}

// One CSV cell: quote when it carries a comma/quote/newline (RFC-4180).
QString csvCell(const QString &s) {
    if (s.contains(QLatin1Char('"')) || s.contains(QLatin1Char(',')) ||
        s.contains(QLatin1Char('\n')) || s.contains(QLatin1Char('\r'))) {
        QString q = s;
        q.replace(QLatin1String("\""), QLatin1String("\"\""));
        return QLatin1Char('"') + q + QLatin1Char('"');
    }
    return s;
}

// Serialize a {columns,rows} query result to CSV bytes (\n endings, UTF-8).
QByteArray sqlResultToCsv(const QJsonObject &r) {
    const QJsonArray cols = r.value(QLatin1String("columns")).toArray();
    const QJsonArray rows = r.value(QLatin1String("rows")).toArray();
    QString out;
    QStringList header;
    for (const QJsonValue &c : cols) header << csvCell(c.toString());
    out += header.join(QLatin1Char(',')) + QLatin1Char('\n');
    for (const QJsonValue &rv : rows) {
        const QJsonArray row = rv.toArray();
        QStringList cells;
        for (const QJsonValue &cell : row) cells << csvCell(cell.toString());
        out += cells.join(QLatin1Char(',')) + QLatin1Char('\n');
    }
    return out.toUtf8();
}

// Serialize a {columns,rows,engine} query result to pretty JSON bytes.
QByteArray sqlResultToJson(const QJsonObject &r) {
    QJsonObject o;
    o[QStringLiteral("columns")] = r.value(QLatin1String("columns")).toArray();
    o[QStringLiteral("rows")] = r.value(QLatin1String("rows")).toArray();
    o[QStringLiteral("engine")] = r.value(QLatin1String("engine")).toString();
    return QJsonDocument(o).toJson(QJsonDocument::Indented);
}
} // namespace

QString McpBridge::defaultServerName() {
    return SingleInstance::serverName() + QStringLiteral("-mcp");
}

McpBridge::McpBridge(McpEditorHost host, QObject *parent,
                     const QString &serverName,
                     const QString &endpointDirForTests)
    : QObject(parent), m_host(std::move(host)),
      m_serverName(serverName.isEmpty() ? defaultServerName() : serverName) {
    m_server = new QLocalServer(this);
    m_server->setSocketOptions(QLocalServer::UserAccessOption);
    if (!m_server->listen(m_serverName)) {
        // Bind failed: stale socket from a crash — or a LIVE peer that owns
        // the name. removeServer() on a live peer's socket would orphan it
        // (same law as the single-instance bind in main.cpp), so probe first
        // and remove only when nothing answers.
        QLocalSocket probe;
        probe.connectToServer(m_serverName);
        const bool alive = probe.waitForConnected(300);
        probe.abort();
        if (!alive) {
            QLocalServer::removeServer(m_serverName);
            m_server->listen(m_serverName);
        }
    }
    if (m_server->isListening()) {
        connect(m_server, &QLocalServer::newConnection,
                this, &McpBridge::onNewConnection);
        // Publish the endpoint only once we know what we actually bound —
        // after the stale-probe/relisten dance above, never before.
        //
        // A test-named bridge with no directory override publishes NOTHING:
        // several existing tests construct bridges with custom names and must
        // not stomp the real user's mcp-endpoint.json (which a live editor may
        // own while the suite runs).
        if (serverName.isEmpty())
            publishEndpoint(endpointDirForTests.isEmpty()
                                ? Config::appConfigDir()
                                : endpointDirForTests);
        else if (!endpointDirForTests.isEmpty())
            publishEndpoint(endpointDirForTests);
    } else {
        qWarning("Notepatra MCP bridge: bind failed on %s: %s",
                 qPrintable(m_serverName),
                 qPrintable(m_server->errorString()));
    }
    m_approvalTimer = new QTimer(this);
    m_approvalTimer->setSingleShot(true);
    connect(m_approvalTimer, &QTimer::timeout, this, [this]() {
        resolveActiveApproval(false, QStringLiteral("approval timed out"));
    });
    m_countdownTimer = new QTimer(this);
    m_countdownTimer->setInterval(1000);
    connect(m_countdownTimer, &QTimer::timeout, this,
            &McpBridge::updateCountdown);
    // v0.1.133 (D6): a quit that never destroys us (the SIGTERM path, a
    // leaked window) must still take the endpoint file down with the loop.
    if (QCoreApplication *app = QCoreApplication::instance())
        connect(app, &QCoreApplication::aboutToQuit, this,
                [this]() { unpublishEndpoint(); });
}

McpBridge::~McpBridge() {
    // The card is a child of the HOST widget, not of the bridge — take it
    // down with us so no orphan Approve button outlives its plumbing.
    dismissActiveCard();
    unpublishEndpoint();
}

QString McpBridge::fullServerName() const {
    return m_server ? m_server->fullServerName() : QString();
}

// Write <dir>/mcp-endpoint.json: the sidecar's ONLY reliable way to learn where
// we bound. Guessing $TMPDIR/<name> is wrong on macOS, where Qt resolves a bare
// server name under NSTemporaryDirectory() (/private/var/folders/…/T/), so the
// sidecar reported "Notepatra is not running" against a perfectly live editor.
//
// Failure here is never fatal — the sidecar still falls back to its guess, and
// on Linux that guess is correct — so we warn and carry on.
void McpBridge::publishEndpoint(const QString &dir) {
    if (dir.isEmpty() || !m_server) return;
    const QString path = dir + QStringLiteral("/mcp-endpoint.json");

    QJsonObject o;
    o[QStringLiteral("notepatra_mcp_endpoint")] = 1;
#ifdef Q_OS_WIN
    o[QStringLiteral("kind")] = QStringLiteral("named_pipe");
#else
    o[QStringLiteral("kind")] = QStringLiteral("unix_socket");
#endif
    // Verbatim: an absolute socket path on Unix, \\.\pipe\<name> on Windows.
    o[QStringLiteral("value")] = m_server->fullServerName();
    // name/pid/version are DIAGNOSTICS only — the reader must never gate on
    // pid liveness; the connect attempt is the liveness test.
    o[QStringLiteral("name")] = m_serverName;
    o[QStringLiteral("pid")] =
        static_cast<double>(QCoreApplication::applicationPid());
    o[QStringLiteral("version")] = QStringLiteral(NOTEPATRA_VERSION);

    // Atomic: a reader must never observe a half-written object. Compact + a
    // trailing newline, UTF-8, no BOM.
    QSaveFile f(path);
    if (!f.open(QIODevice::WriteOnly) ||
        f.write(QJsonDocument(o).toJson(QJsonDocument::Compact) + '\n') < 0 ||
        !f.commit()) {
        qWarning("Notepatra MCP bridge: could not publish endpoint file %s",
                 qPrintable(path));
        return;
    }
    // 0600 — the socket path is a capability. A no-op on Windows, where the
    // per-user %APPDATA% ACL already covers it (same posture as the sidecar's
    // token files).
    QFile::setPermissions(path,
                          QFileDevice::ReadOwner | QFileDevice::WriteOwner);
    m_endpointFile = path;
}

// Remove only what THIS instance wrote. A crash leaves a stale file by design:
// the reader tolerates it because a failed connect falls through to the guess.
void McpBridge::unpublishEndpoint() {
    if (m_endpointFile.isEmpty()) return;
    QFile::remove(m_endpointFile);
    m_endpointFile.clear();
}

void McpBridge::setApprovalTimeoutMs(int ms) {
    m_approvalTimeoutMs = qMax(1, ms);
    if (m_approvalTimer && m_approvalTimer->isActive()) {
        m_approvalTimer->start(m_approvalTimeoutMs);
        m_approvalDeadlineMs =
            QDateTime::currentMSecsSinceEpoch() + m_approvalTimeoutMs;
        updateCountdown();
    }
}

// v0.1.133 (D12). A card is not a deny-list: a human approving forty edits
// approves the forty-first, and a write into authorized_keys, a shell rc file
// or a git hook runs code later without anyone opening it. The check runs on
// every spelling of the destination: the absolute path, the leaf under its
// canonical parent (symlinked directory), and a leaf symlink's target.
QString McpBridge::writeDestinationRefusal(const QString &path) {
    if (path.isEmpty()) return QString();
    const QFileInfo fi(QDir::fromNativeSeparators(path));
    QStringList forms;
    forms << fi.absoluteFilePath();
    const QString parentCanon =
        QFileInfo(fi.absolutePath()).canonicalFilePath();
    if (!parentCanon.isEmpty())
        forms << parentCanon + QLatin1Char('/') + fi.fileName();
    if (fi.isSymLink())
        forms << QFileInfo(fi.symLinkTarget()).absoluteFilePath();
    if (fi.exists()) {
        const QString leafCanon = fi.canonicalFilePath();
        if (!leafCanon.isEmpty()) forms << leafCanon;
    }
    for (const QString &f : forms) {
        if (!credentialRefusal(f).isEmpty() || PathDenylist::isWriteDangerous(f))
            return QStringLiteral(
                       "refusing to write %1: it is a protected destination "
                       "(credential store, shell start-up file, autostart "
                       "entry, scheduled job, git hook or git config). Edit "
                       "it yourself if you really mean to.")
                .arg(QDir::toNativeSeparators(fi.absoluteFilePath()));
    }
    return QString();
}

QString McpBridge::tabWriteRefusal(int idx) const {
    return writeDestinationRefusal(m_host.tabPath ? m_host.tabPath(idx)
                                                  : QString());
}

bool McpBridge::isListening() const {
    return m_server && m_server->isListening();
}

void McpBridge::onNewConnection() {
    while (QLocalSocket *client = m_server->nextPendingConnection()) {
        client->setParent(this);
        // Attach handlers BEFORE greeting so nothing already buffered is lost.
        connect(client, &QLocalSocket::readyRead, this,
                [this, client]() { onReadyRead(client); });
        connect(client, &QLocalSocket::disconnected, this, [this, client]() {
            m_buffers.remove(client);
            // A vanished peer can never approve anything: dismiss its card,
            // drop its queued writes, execute NOTHING.
            dropClientApprovals(client);
            client->deleteLater();
        });
        // Greeting first — proof-of-life before any request handling.
        QJsonObject greet;
        greet[QStringLiteral("notepatra_mcp")] = 1;
        greet[QStringLiteral("app")] = QStringLiteral("Notepatra");
        greet[QStringLiteral("version")] = QStringLiteral(NOTEPATRA_VERSION);
        sendObject(client, greet);
        if (client->bytesAvailable() > 0) onReadyRead(client);
    }
}

void McpBridge::onReadyRead(QLocalSocket *client) {
    m_buffers[client] += client->readAll();
    // Re-find the buffer every iteration: a failed flush inside handleLine can
    // emit disconnected synchronously, which removes the entry — never hold a
    // reference into m_buffers across a handler call.
    QPointer<QLocalSocket> guard(client);
    while (guard) {
        const auto it = m_buffers.find(client);
        if (it == m_buffers.end()) return; // peer went away mid-loop
        const int nl = it->indexOf('\n');
        if (nl < 0) break;
        const QByteArray line = it->left(nl);
        it->remove(0, nl + 1);
        handleLine(client, line.trimmed());
    }
    if (!guard) return;
    const auto it = m_buffers.find(client);
    if (it != m_buffers.end() && it->size() > kMaxRequestLineBytes) {
        it->clear();
        sendError(client, -1, QStringLiteral("parse error"));
        // Never abort()/disconnect synchronously inside readyRead — queue it.
        QMetaObject::invokeMethod(
            client, [client]() { client->disconnectFromServer(); },
            Qt::QueuedConnection);
    }
}

void McpBridge::handleLine(QLocalSocket *client, const QByteArray &line) {
    if (line.isEmpty()) return;
    QJsonParseError perr{};
    const QJsonDocument doc = QJsonDocument::fromJson(line, &perr);
    if (perr.error != QJsonParseError::NoError || !doc.isObject()) {
        sendError(client, -1, QStringLiteral("parse error"));
        return;
    }
    const QJsonObject req = doc.object();
    const int id = req.value(QLatin1String("id")).toInt(-1);
    const QString verb = req.value(QLatin1String("verb")).toString();
    const QJsonObject args = req.value(QLatin1String("args")).toObject();

    if (verb == QLatin1String("open_file"))
        verbOpenFile(client, id, args);
    else if (verb == QLatin1String("list_open_tabs"))
        verbListOpenTabs(client, id);
    else if (verb == QLatin1String("read_tab"))
        verbReadTab(client, id, args);
    else if (verb == QLatin1String("get_selection"))
        verbGetSelection(client, id);
    else if (verb == QLatin1String("search_project"))
        verbSearchProject(client, id, args);
    else if (verb == QLatin1String("get_status"))
        verbGetStatus(client, id);
    else if (verb == QLatin1String("app_info"))
        verbAppInfo(client, id);
    else if (verb == QLatin1String("list_recent_files"))
        verbListRecentFiles(client, id);
    else if (verb == QLatin1String("find_in_tab"))
        verbFindInTab(client, id, args);
    else if (verb == QLatin1String("new_tab"))
        verbNewTab(client, id, args);
    else if (verb == QLatin1String("goto_line"))
        verbGotoLine(client, id, args);
    else if (verb == QLatin1String("select_range"))
        verbSelectRange(client, id, args);
    else if (verb == QLatin1String("set_language"))
        verbSetLanguage(client, id, args);
    else if (verb == QLatin1String("compare_tabs"))
        verbCompareTabs(client, id, args);
    else if (verb == QLatin1String("format_text"))
        verbFormatText(client, id, args);
    else if (verb == QLatin1String("list_notes"))
        verbListNotes(client, id, args);
    else if (verb == QLatin1String("read_note"))
        verbReadNote(client, id, args);
    else if (verb == QLatin1String("insert_text"))
        verbInsertText(client, id, args);
    else if (verb == QLatin1String("replace_selection"))
        verbReplaceSelection(client, id, args);
    else if (verb == QLatin1String("apply_edit"))
        verbApplyEdit(client, id, args);
    else if (verb == QLatin1String("save_tab"))
        verbSaveTab(client, id, args);
    // ── v0.1.119 depth wave ──
    else if (verb == QLatin1String("list_reminders"))
        verbListReminders(client, id);
    else if (verb == QLatin1String("git_status"))
        verbGit(client, id, args, QStringLiteral("status"));
    else if (verb == QLatin1String("git_diff"))
        verbGit(client, id, args, QStringLiteral("diff"));
    else if (verb == QLatin1String("git_log"))
        verbGit(client, id, args, QStringLiteral("log"));
    else if (verb == QLatin1String("git_show"))
        verbGit(client, id, args, QStringLiteral("show"));
    else if (verb == QLatin1String("git_branch"))
        verbGit(client, id, args, QStringLiteral("branch"));
    else if (verb == QLatin1String("validate_npd"))
        verbValidateNpd(client, id, args);
    else if (verb == QLatin1String("run_sql"))
        verbRunSql(client, id, args);
    else if (verb == QLatin1String("list_languages"))
        verbListLanguages(client, id);
    else if (verb == QLatin1String("get_capabilities"))
        verbGetCapabilities(client, id);
    else if (verb == QLatin1String("open_note"))
        verbOpenNote(client, id, args);
    else if (verb == QLatin1String("create_note"))
        verbCreateNote(client, id, args);
    else if (verb == QLatin1String("append_note"))
        verbAppendNote(client, id, args);
    else if (verb == QLatin1String("set_reminder"))
        verbSetReminder(client, id, args);
    else if (verb == QLatin1String("export_diagram"))
        verbExportDiagram(client, id, args);
    else if (verb == QLatin1String("create_diagram"))
        verbCreateDiagram(client, id, args);
    else if (verb == QLatin1String("get_diagram_source"))
        verbGetDiagramSource(client, id, args);
    else if (verb == QLatin1String("set_diagram_source"))
        verbSetDiagramSource(client, id, args);
    else if (verb == QLatin1String("open_noter"))
        verbOpenNoter(client, id);
    else if (verb == QLatin1String("list_connections"))
        verbListConnections(client, id);
    else if (verb == QLatin1String("run_query"))
        verbRunQuery(client, id, args);
    else if (verb == QLatin1String("list_tables"))
        verbListTables(client, id, args);
    else if (verb == QLatin1String("open_data_analyst"))
        verbOpenDataAnalyst(client, id);
    else if (verb == QLatin1String("render_chart"))
        verbRenderChart(client, id, args);
    else if (verb == QLatin1String("export_query_results"))
        verbExportQueryResults(client, id, args);
    else if (verb == QLatin1String("export_chart"))
        verbExportChart(client, id, args);
    // ── v0.1.133 ──
    else if (verb == QLatin1String("activate_tab") ||
             verb == QLatin1String("set_read_only") ||
             verb == QLatin1String("bookmarks") ||
             verb == QLatin1String("fold") ||
             verb == QLatin1String("set_preview") ||
             verb == QLatin1String("compare_with_file"))
        verbTabAction(client, id, args, verb);
    else if (verb == QLatin1String("close_tab"))
        verbCloseTab(client, id, args);
    else if (verb == QLatin1String("save_all"))
        verbSaveAll(client, id, args);
    else if (verb == QLatin1String("undo") || verb == QLatin1String("redo") ||
             verb == QLatin1String("transform_lines") ||
             verb == QLatin1String("convert_case") ||
             verb == QLatin1String("comment_lines") ||
             verb == QLatin1String("line_edit") ||
             verb == QLatin1String("set_eol") ||
             verb == QLatin1String("set_encoding") ||
             verb == QLatin1String("reload_tab") ||
             verb == QLatin1String("rename_file"))
        verbTabEdit(client, id, args, verb);
    else if (verb == QLatin1String("set_view") ||
             verb == QLatin1String("set_theme"))
        verbAppAction(client, id, args, verb);
    else if (verb == QLatin1String("check_brackets") ||
             verb == QLatin1String("fix_brackets") ||
             verb == QLatin1String("hash_text") ||
             verb == QLatin1String("encode_text") ||
             verb == QLatin1String("repair_json") ||
             verb == QLatin1String("markdown_convert") ||
             verb == QLatin1String("mermaid_to_npd") ||
             verb == QLatin1String("list_symbols") ||
             verb == QLatin1String("generate_password") ||
             verb == QLatin1String("diff_text"))
        verbCompute(client, id, args, verb);
    // ── v0.1.133 Phase 3 ──
    else if (kGitVerbs().contains(verb))
        verbGitWrite(client, id, args, verb);
    else if (kNoterVerbs().contains(verb))
        verbNoter(client, id, args, verb);
    else if (kSettingsVerbs().contains(verb))
        verbSettings(client, id, args, verb);
    else if (kDataVerbs().contains(verb))
        verbData(client, id, args, verb);
    else if (kRestVerbs().contains(verb))
        verbRest(client, id, args, verb);
    else if (verb == QLatin1String("hex_dump") ||
             verb == QLatin1String("clear_recent_files"))
        verbMisc(client, id, args, verb);
    else
        sendError(client, id,
                  QStringLiteral("unknown verb: %1").arg(verb));
}

void McpBridge::sendObject(QLocalSocket *client, const QJsonObject &obj) {
    if (!client || client->state() != QLocalSocket::ConnectedState) return;
    client->write(QJsonDocument(obj).toJson(QJsonDocument::Compact) + '\n');
    client->flush();
}

void McpBridge::sendResult(QLocalSocket *client, int id,
                           const QJsonObject &result) {
    QJsonObject o;
    o[QStringLiteral("id")] = id;
    o[QStringLiteral("ok")] = true;
    o[QStringLiteral("result")] = result;
    sendObject(client, o);
}

void McpBridge::sendError(QLocalSocket *client, int id,
                          const QString &message) {
    QJsonObject o;
    o[QStringLiteral("id")] = id;
    o[QStringLiteral("ok")] = false;
    o[QStringLiteral("error")] = message;
    sendObject(client, o);
}

void McpBridge::verbOpenFile(QLocalSocket *client, int id,
                             const QJsonObject &args) {
    const QString path = args.value(QLatin1String("path")).toString();
    if (path.isEmpty()) {
        sendError(client, id, QStringLiteral("missing path"));
        return;
    }
    if (!QFileInfo(path).isFile()) {
        sendError(client, id,
                  QStringLiteral("not found or not a file: %1")
                      .arg(QDir::toNativeSeparators(path)));
        return;
    }
    // NP-05: refuse BEFORE the tab exists. Opening it and refusing to read it
    // would still put the file on screen and in the session's tab list.
    if (const QString refusal = credentialRefusal(path); !refusal.isEmpty()) {
        sendError(client, id, refusal);
        return;
    }
    const int idx = m_host.openFile ? m_host.openFile(path) : -1;
    if (idx < 0) {
        sendError(client, id,
                  QStringLiteral("could not open: %1")
                      .arg(QDir::toNativeSeparators(path)));
        return;
    }
    QJsonObject result;
    result[QStringLiteral("opened")] = true;
    result[QStringLiteral("tab_index")] = idx;
    if (const qint64 tid = tabIdOf(idx); tid >= 0)
        result[QStringLiteral("tab_id")] = tid;
    sendResult(client, id, result);
}

void McpBridge::verbListOpenTabs(QLocalSocket *client, int id) {
    QJsonArray tabs;
    const int n = m_host.tabCount ? m_host.tabCount() : 0;
    for (int i = 0; i < n; ++i) {
        QJsonObject t;
        t[QStringLiteral("index")] = i;
        t[QStringLiteral("title")] = m_host.tabTitle ? m_host.tabTitle(i)
                                                     : QString();
        t[QStringLiteral("path")] = m_host.tabPath ? m_host.tabPath(i)
                                                   : QString();
        t[QStringLiteral("modified")] = m_host.tabModified
                                            ? m_host.tabModified(i) : false;
        // v0.1.121 (issue #1): mark tabs that are not editable text buffers
        // (Welcome page, Diagram canvas, Noter panel …) so an agent knows not
        // to read_tab / insert_text / save_tab against them.
        t[QStringLiteral("editable")] = tabIsEditable(i);
        // v0.1.126 (NP-13): the selector to hold on to. `index` above is only
        // valid until the next tab closes.
        if (const qint64 tid = tabIdOf(i); tid >= 0)
            t[QStringLiteral("id")] = tid;
        tabs.append(t);
    }
    QJsonObject result;
    result[QStringLiteral("tabs")] = tabs;
    sendResult(client, id, result);
}

void McpBridge::verbReadTab(QLocalSocket *client, int id,
                            const QJsonObject &args) {
    QString err;
    const int idx = resolveTab(args, "index", /*allowCurrent=*/false, &err);
    if (idx < 0) {
        sendError(client, id, err);
        return;
    }
    if (!tabIsEditable(idx)) { // v0.1.121: Welcome/diagram/… have no text buffer
        sendError(client, id, nonEditableReason(idx));
        return;
    }
    const QString path = m_host.tabPath ? m_host.tabPath(idx) : QString();
    QString text;
    QString refusal;
    if (!tabTextFor(idx, &text, &refusal)) {
        sendError(client, id, refusal);
        return;
    }
    QJsonObject result;
    result[QStringLiteral("title")] = m_host.tabTitle ? m_host.tabTitle(idx)
                                                      : QString();
    result[QStringLiteral("path")] = path;
    if (const qint64 tid = tabIdOf(idx); tid >= 0)
        result[QStringLiteral("id")] = tid;

    // v0.1.126 (NP-10): the caller may ask for less.
    //
    // The 5 MB ceiling always existed, but nothing under it was negotiable:
    // a 2.7 MB buffer came back whole and could fill an assistant's entire
    // context in one call, with no `truncated` field to even say it was
    // complete. `truncated` is now always present — absent-means-false is a
    // fact the caller had to infer, and every other bulk verb states it.
    int cap = kReadTabCapChars;
    if (args.contains(QLatin1String("max_bytes"))) {
        const QJsonValue v = args.value(QLatin1String("max_bytes"));
        if (!v.isDouble()) {
            sendError(client, id,
                      QStringLiteral("max_bytes must be an integer"));
            return;
        }
        const int want = v.toInt(0);
        if (want < 1) {
            sendError(client, id,
                      QStringLiteral("max_bytes must be >= 1"));
            return;
        }
        cap = qMin(want, kReadTabCapChars);
    }
    const int fullSize = text.size();
    const bool truncated = fullSize > cap;
    if (truncated) text.truncate(cap);
    result[QStringLiteral("truncated")] = truncated;
    result[QStringLiteral("total_chars")] = fullSize;
    result[QStringLiteral("text")] = text;
    sendResult(client, id, result);
}

void McpBridge::verbGetSelection(QLocalSocket *client, int id) {
    // NP-14: this used to be exempt on the reasoning that a selection is
    // human-made, so reading it back implies consent. select_range breaks
    // that: the CLIENT chooses the range, so an agent could select a private
    // key and read it out in chunks — five guarded doors and one open window.
    int tabIndex = -1;
    QString text;
    QString refusal;
    if (!selectionText(&text, &tabIndex, &refusal)) {
        sendError(client, id, refusal);
        return;
    }
    QJsonObject result;
    result[QStringLiteral("text")] = text;
    result[QStringLiteral("tab_index")] = tabIndex;
    sendResult(client, id, result);
}

void McpBridge::verbSearchProject(QLocalSocket *client, int id,
                                  const QJsonObject &args) {
    const QString query = args.value(QLatin1String("query")).toString();
    if (query.isEmpty()) {
        sendError(client, id, QStringLiteral("missing query"));
        return;
    }
    // Optional regex / match_case / whole_word. Validated on the GUI thread
    // BEFORE any worker is dispatched; the worker gets a compiled copy.
    LineMatcher matcher;
    if (const QString merr = makeLineMatcher(args, &matcher); !merr.isEmpty()) {
        sendError(client, id, merr);
        return;
    }
    int maxResults = args.value(QLatin1String("max_results"))
                         .toInt(kMaxSearchResults);
    maxResults = qBound(1, maxResults, kMaxSearchResults);

    // Leg 1 (synchronous): currently-open tab buffers.
    QVector<SearchHit> hits;
    QSet<QString> openPaths;
    const int n = m_host.tabCount ? m_host.tabCount() : 0;
    for (int i = 0; i < n && hits.size() < maxResults; ++i) {
        const QString path = m_host.tabPath ? m_host.tabPath(i) : QString();
        if (!path.isEmpty()) openPaths.insert(QFileInfo(path).absoluteFilePath());
        // NP-05: the workspace leg has skipped credential files since
        // v0.1.125, but a credential file that happens to be OPEN was read
        // straight out of the tab buffer by this leg — the deny-list guarded
        // the disk and left the buffer wide open. Skipping (not erroring) is
        // right here: one unreadable file must not fail the whole search.
        QString text;
        if (!tabTextFor(i, &text, nullptr)) continue;
        if (text.isEmpty()) continue;
        const QString hitPath = path; // "" for untitled tabs
        int lineNo = 1;
        int start = 0;
        while (start <= text.size() && hits.size() < maxResults) {
            int end = text.indexOf(QLatin1Char('\n'), start);
            if (end < 0) end = text.size();
            const int len = end - start;
            if (len <= kMaxLineScanBytes) {
                const QString line = text.mid(start, len);
                const int col = matcher.find(line);
                if (col >= 0)
                    hits.append({hitPath, lineNo, line.trimmed(), col + 1});
            }
            start = end + 1;
            ++lineNo;
            if (end == text.size()) break;
        }
    }

    const QString root = m_host.workspaceRoot ? m_host.workspaceRoot()
                                              : QString();
    // Whether the workspace leg ran is a property of the WORKSPACE, never of
    // whether the query happened to match an open tab.
    //
    // Gating the "no workspace" signal on `hits.isEmpty()` made the contract
    // flip on match luck: miss the open tabs and you got a clear error, hit one
    // and you got `{results:[...]}` shaped exactly like a completed project
    // search — so a caller could not tell a searched project from an unsearched
    // one. The `!isDir` case (folder deleted or renamed under us) was worse
    // still: empty results, `truncated:false`, no error at all.
    //
    // Every response now states which legs actually ran, and the caller can
    // trust that field instead of inferring from result count.
    const bool haveWorkspace = !root.isEmpty() && QFileInfo(root).isDir();
    if (!haveWorkspace || hits.size() >= maxResults) {
        QJsonObject result;
        result[QStringLiteral("results")] = hitsToJson(hits);
        result[QStringLiteral("truncated")] = hits.size() >= maxResults;
        // False here means "open tabs only" — the workspace was never walked.
        result[QStringLiteral("workspace_searched")] = haveWorkspace;
        result[QStringLiteral("scope")] =
            haveWorkspace ? QStringLiteral("tabs_and_workspace")
                          : QStringLiteral("open_tabs_only");
        // v0.1.126 (NP-08): "no workspace open" is a note about COVERAGE, not
        // an error, and it must not depend on whether the query matched.
        //
        // v0.1.125 still failed the call outright when the open tabs happened
        // to yield nothing — so "your string is not in these files" was
        // reported as "you have no folder open". Those are different facts,
        // and an assistant told the second one pushes the user to fix a
        // problem that does not exist. Zero results is a successful search.
        if (!haveWorkspace) {
            result[QStringLiteral("notice")] =
                root.isEmpty()
                    ? QStringLiteral("No workspace folder is open, so only the "
                                     "open tabs were searched. Open a folder "
                                     "to search a project.")
                    : QStringLiteral("The workspace folder no longer exists, "
                                     "so only the open tabs were searched. "
                                     "Re-open the folder.");
        }
        sendResult(client, id, result);
        return;
    }

    // Leg 2 (QtConcurrent): workspace files. The worker touches only value
    // copies; the response is written back on the GUI thread via the
    // bridge-parented watcher, which dies with the bridge — so a late future
    // can never touch a dead socket or a dead bridge.
    QPointer<QLocalSocket> guard(client);
    auto *watcher = new QFutureWatcher<QJsonObject>(this);
    connect(watcher, &QFutureWatcher<QJsonObject>::finished, this,
            [this, watcher, guard, id]() {
                watcher->deleteLater();
                if (guard) sendResult(guard, id, watcher->result());
            });
    watcher->setFuture(QtConcurrent::run(
        [root, matcher, openPaths, hits, maxResults]() {
            return scanWorkspace(root, matcher, openPaths, hits, maxResults);
        }));
}

// ── v0.1.118 expansive wave ──────────────────────────────────────────

void McpBridge::verbGetStatus(QLocalSocket *client, int id) {
    const int n = m_host.tabCount ? m_host.tabCount() : 0;
    int idx = m_host.currentTabIndex ? m_host.currentTabIndex() : -1;
    if (idx < 0 || idx >= n) idx = -1;
    QJsonObject result;
    result[QStringLiteral("tab_index")] = idx;
    result[QStringLiteral("title")] =
        (idx >= 0 && m_host.tabTitle) ? m_host.tabTitle(idx) : QString();
    result[QStringLiteral("path")] =
        (idx >= 0 && m_host.tabPath) ? m_host.tabPath(idx) : QString();
    result[QStringLiteral("language")] =
        (idx >= 0 && m_host.tabLanguage) ? m_host.tabLanguage(idx) : QString();
    result[QStringLiteral("encoding")] =
        (idx >= 0 && m_host.tabEncoding) ? m_host.tabEncoding(idx) : QString();
    int line = 0, col = 0; // 0 = unknown; real positions are 1-based
    if (idx >= 0 && m_host.cursorPosition) m_host.cursorPosition(&line, &col);
    result[QStringLiteral("cursor_line")] = line;
    result[QStringLiteral("cursor_col")] = col;
    // v0.1.133 (D9 + additive status). Absent fields mean "no active tab".
    if (idx >= 0) {
        if (const qint64 tid = tabIdOf(idx); tid >= 0)
            result[QStringLiteral("tab_id")] = tid;
        result[QStringLiteral("modified")] =
            m_host.tabModified ? m_host.tabModified(idx) : false;
        result[QStringLiteral("editable")] = tabIsEditable(idx);
        if (m_host.tabState) {
            const QJsonObject st = m_host.tabState(idx);
            for (auto it = st.begin(); it != st.end(); ++it)
                result[it.key()] = it.value();
        }
    }
    result[QStringLiteral("workspace_root")] =
        QDir::toNativeSeparators(m_host.workspaceRoot ? m_host.workspaceRoot()
                                                      : QString());
    result[QStringLiteral("edition")] = QLatin1String(editionName());
    result[QStringLiteral("version")] = QStringLiteral(NOTEPATRA_VERSION);
    sendResult(client, id, result);
}

void McpBridge::verbAppInfo(QLocalSocket *client, int id) {
    QJsonObject result;
    result[QStringLiteral("name")] = QStringLiteral("Notepatra");
    result[QStringLiteral("version")] = QStringLiteral(NOTEPATRA_VERSION);
    result[QStringLiteral("edition")] = QLatin1String(editionName());
    result[QStringLiteral("platform")] = QLatin1String(platformName());
    sendResult(client, id, result);
}

void McpBridge::verbListRecentFiles(QLocalSocket *client, int id) {
    QJsonArray files;
    if (m_host.recentFiles) {
        const QStringList list = m_host.recentFiles();
        for (const QString &f : list) files.append(f);
    }
    QJsonObject result;
    result[QStringLiteral("files")] = files;
    sendResult(client, id, result);
}

void McpBridge::verbFindInTab(QLocalSocket *client, int id,
                              const QJsonObject &args) {
    const QString query = args.value(QLatin1String("query")).toString();
    if (query.isEmpty()) {
        sendError(client, id, QStringLiteral("missing query"));
        return;
    }
    // Optional regex / match_case / whole_word. Compiled ONCE, up front.
    LineMatcher matcher;
    if (const QString merr = makeLineMatcher(args, &matcher); !merr.isEmpty()) {
        sendError(client, id, merr);
        return;
    }
    // NP-11: same selector set as read_tab — tab_id, index, or title.
    QString err;
    const int idx = resolveTab(args, "index", /*allowCurrent=*/true, &err);
    if (idx < 0) {
        sendError(client, id, err);
        return;
    }
    // NP-05: a match list is a content channel — it quotes the matching
    // lines. Refusing read_tab on a key while letting find_in_tab return the
    // line the key material sits on would just be a slower leak.
    QString text;
    QString denied;
    if (!tabTextFor(idx, &text, &denied)) {
        sendError(client, id, denied);
        return;
    }
    QJsonArray matches;
    bool truncated = false;
    int lineNo = 1;
    int start = 0;
    while (start <= text.size()) {
        int end = text.indexOf(QLatin1Char('\n'), start);
        if (end < 0) end = text.size();
        const int len = end - start;
        // Same per-line scan cap as search_project: a megabyte one-liner
        // is not a searchable "line". The regex matcher sees at most one
        // capped line per call — never a whole-buffer offset loop.
        if (len <= kMaxLineScanBytes) {
            const QString line = text.mid(start, len);
            const int col = matcher.find(line);
            if (col >= 0) {
                if (matches.size() >= kMaxFindMatches) {
                    truncated = true;
                    break;
                }
                QJsonObject m;
                m[QStringLiteral("line")] = lineNo;
                m[QStringLiteral("column")] = col + 1;
                m[QStringLiteral("text")] =
                    line.trimmed().left(kResultLinePreviewChars);
                matches.append(m);
            }
        }
        start = end + 1;
        ++lineNo;
        if (end == text.size()) break;
    }
    QJsonObject result;
    result[QStringLiteral("matches")] = matches;
    result[QStringLiteral("truncated")] = truncated;
    sendResult(client, id, result);
}

void McpBridge::verbNewTab(QLocalSocket *client, int id,
                           const QJsonObject &args) {
    if (!m_host.newTab) {
        sendError(client, id, QStringLiteral("new_tab not supported by host"));
        return;
    }
    const QString text = args.value(QLatin1String("text")).toString();
    // v0.1.133: language? and title?, validated BEFORE the tab exists.
    QString lang = args.value(QLatin1String("language")).toString();
    if (!lang.isEmpty()) {
        if (!m_host.setLanguage) {
            sendError(client, id, QStringLiteral("language not supported by host"));
            return;
        }
        if (m_host.resolveLanguage) {
            const QString resolved = m_host.resolveLanguage(lang);
            if (resolved.isEmpty()) {
                sendError(client, id,
                          QStringLiteral("unknown language: %1 (see list_languages)")
                              .arg(lang));
                return;
            }
            lang = resolved;
        }
    }
    const QString title = args.value(QLatin1String("title")).toString();
    if (!title.isEmpty() && !m_host.tabAction) {
        sendError(client, id, QStringLiteral("title not supported by host"));
        return;
    }
    const int idx = m_host.newTab(text);
    if (idx < 0) {
        sendError(client, id, QStringLiteral("could not create tab"));
        return;
    }
    QJsonObject result;
    if (!lang.isEmpty()) {
        m_host.setLanguage(idx, lang);
        result[QStringLiteral("language")] = lang;
    }
    if (!title.isEmpty()) {
        QString terr;
        QJsonObject t;
        t[QStringLiteral("title")] = title;
        m_host.tabAction(idx, QStringLiteral("set_title"), t, &terr);
        result[QStringLiteral("title")] = tabLabel(idx);
    }
    result[QStringLiteral("tab_index")] = idx;
    if (const qint64 tid = tabIdOf(idx); tid >= 0)
        result[QStringLiteral("tab_id")] = tid;
    sendResult(client, id, result);
}

void McpBridge::verbGotoLine(QLocalSocket *client, int id,
                             const QJsonObject &args) {
    if (!m_host.gotoLine) {
        sendError(client, id,
                  QStringLiteral("goto_line not supported by host"));
        return;
    }
    const int line = args.value(QLatin1String("line")).toInt(0);
    if (line < 1) {
        sendError(client, id,
                  QStringLiteral("line must be >= 1"));
        return;
    }
    // v0.1.133 (D3): the common resolver. This used to read tab_index only,
    // so a tab_id call silently moved the cursor in the FOCUSED tab.
    QString err;
    const int idx = resolveWriteTab(args, &err);
    if (idx < 0) {
        sendError(client, id, err);
        return;
    }
    const int landed = m_host.gotoLine(idx, line);
    if (landed < 1) {
        sendError(client, id, QStringLiteral("could not move cursor"));
        return;
    }
    QJsonObject result;
    result[QStringLiteral("ok")] = true;
    result[QStringLiteral("tab_index")] = idx;
    // The line ACTUALLY landed on, which is not always the one requested: a
    // line past end-of-file clamps to the last line. Echoing the request here
    // was a lie that callers acted on — insert_text defaults to the cursor, so
    // "ok, line 99999" meant a write at the top of the file.
    result[QStringLiteral("line")] = landed;
    result[QStringLiteral("requested_line")] = line;
    result[QStringLiteral("clamped")] = (landed != line);
    sendResult(client, id, result);
}

// v0.1.121 (issue #5): move the selection to an explicit 1-based range. ACT
// tier — it changes only what is selected, so there is NO approval card. The
// host clamps columns and re-focuses the editor.
void McpBridge::verbSelectRange(QLocalSocket *client, int id,
                                const QJsonObject &args) {
    if (!m_host.selectRange) {
        sendError(client, id,
                  QStringLiteral("select_range not supported by host"));
        return;
    }
    QString err;
    const int idx = resolveWriteTab(args, &err); // resolves tab_id/tab_index/current
    if (idx < 0) {
        sendError(client, id, err);
        return;
    }
    if (!tabIsEditable(idx)) {
        sendError(client, id, nonEditableReason(idx));
        return;
    }
    // NP-14: refuse to STAGE the read, not just to return it. get_selection
    // is guarded now, but leaving select_range open would still let a client
    // move the user's cursor and highlight a private key inside their own
    // editor window — and any future verb that reports selection state would
    // inherit a leak that was already set up for it.
    if (const QString denied =
            credentialRefusal(m_host.tabPath ? m_host.tabPath(idx) : QString());
        !denied.isEmpty()) {
        sendError(client, id, denied);
        return;
    }
    const int startLine = args.value(QLatin1String("start_line")).toInt(0);
    const int startCol = args.value(QLatin1String("start_col")).toInt(0);
    const int endLine = args.value(QLatin1String("end_line")).toInt(0);
    const int endCol = args.value(QLatin1String("end_col")).toInt(0);
    if (startLine < 1 || startCol < 1 || endLine < 1 || endCol < 1) {
        sendError(client, id,
                  QStringLiteral("start_line, start_col, end_line and end_col "
                                 "must all be >= 1"));
        return;
    }
    if (!m_host.selectRange(idx, startLine, startCol, endLine, endCol)) {
        sendError(client, id,
                  QStringLiteral("could not select range in tab %1 (line out "
                                 "of range?)")
                      .arg(idx));
        return;
    }
    QJsonObject result;
    result[QStringLiteral("ok")] = true;
    result[QStringLiteral("tab_index")] = idx;
    sendResult(client, id, result);
}

void McpBridge::verbSetLanguage(QLocalSocket *client, int id,
                                const QJsonObject &args) {
    if (!m_host.setLanguage) {
        sendError(client, id,
                  QStringLiteral("set_language not supported by host"));
        return;
    }
    const QString lang = args.value(QLatin1String("language")).toString();
    if (lang.isEmpty()) {
        sendError(client, id, QStringLiteral("missing language"));
        return;
    }
    // v0.1.133 (D3): same resolver as every other tab verb — tab_id used to
    // be ignored here, so the language changed on the focused tab instead.
    QString err;
    const int idx = resolveWriteTab(args, &err);
    if (idx < 0) {
        sendError(client, id, err);
        return;
    }
    // Resolve to the canonical menu token; a null resolver (test hosts)
    // falls back to the raw token so old fakes keep working.
    QString resolved = lang;
    if (m_host.resolveLanguage) {
        resolved = m_host.resolveLanguage(lang);
        if (resolved.isEmpty()) {
            sendError(client, id,
                      QStringLiteral("unknown language: %1").arg(lang));
            return;
        }
    }
    if (!m_host.setLanguage(idx, resolved)) {
        sendError(client, id,
                  QStringLiteral("unknown language: %1").arg(resolved));
        return;
    }
    QJsonObject result;
    result[QStringLiteral("ok")] = true;
    result[QStringLiteral("tab_index")] = idx;
    result[QStringLiteral("language")] = resolved; // honest: what was set
    sendResult(client, id, result);
}

void McpBridge::verbCompareTabs(QLocalSocket *client, int id,
                                const QJsonObject &args) {
    if (!m_host.compareTabs) {
        sendError(client, id,
                  QStringLiteral("compare_tabs not supported by host"));
        return;
    }
    // v0.1.133 (D3): each side is tab_id_X or index_X, resolved by the
    // common resolver, so a stale id is an error and never a fallback.
    const auto side = [this, &args](const char *idKey, const char *idxKey,
                                    QString *err) -> int {
        QJsonObject sel;
        if (args.contains(QLatin1String(idKey)))
            sel[QStringLiteral("tab_id")] = args.value(QLatin1String(idKey));
        else if (args.contains(QLatin1String(idxKey)))
            sel[QStringLiteral("index")] = args.value(QLatin1String(idxKey));
        else {
            *err = QStringLiteral("missing %1 or %2")
                       .arg(QLatin1String(idKey), QLatin1String(idxKey));
            return -1;
        }
        return resolveTab(sel, "index", /*allowCurrent=*/false, err);
    };
    QString errA, errB;
    const int a = side("tab_id_a", "index_a", &errA);
    if (a < 0) {
        sendError(client, id, errA);
        return;
    }
    const int b = side("tab_id_b", "index_b", &errB);
    if (b < 0) {
        sendError(client, id, errB);
        return;
    }
    if (a == b) {
        sendError(client, id,
                  QStringLiteral("cannot compare a tab with itself"));
        return;
    }
    // v0.1.121 (issue #6): both sides must be editable text tabs — the
    // Compare view has nothing to diff for a Welcome/diagram tab.
    if (!tabIsEditable(a)) {
        sendError(client, id,
                  QStringLiteral("compare_tabs requires two editable text "
                                 "tabs; tab %1 is not editable")
                      .arg(a));
        return;
    }
    if (!tabIsEditable(b)) {
        sendError(client, id,
                  QStringLiteral("compare_tabs requires two editable text "
                                 "tabs; tab %1 is not editable")
                      .arg(b));
        return;
    }
    if (!m_host.compareTabs(a, b)) {
        sendError(client, id,
                  QStringLiteral("could not open compare view for tabs %1 "
                                 "and %2")
                      .arg(a)
                      .arg(b));
        return;
    }
    QJsonObject result;
    result[QStringLiteral("opened")] = true;
    sendResult(client, id, result);
}

void McpBridge::verbFormatText(QLocalSocket *client, int id,
                               const QJsonObject &args) {
    if (!m_host.formatText) {
        sendError(client, id,
                  QStringLiteral("format_text not supported by host"));
        return;
    }
    const QString kind = args.value(QLatin1String("kind")).toString();
    if (kind != QLatin1String("json") && kind != QLatin1String("sql") &&
        kind != QLatin1String("html")) {
        sendError(client, id,
                  QStringLiteral("unknown kind: %1 (json|sql|html)").arg(kind));
        return;
    }
    const QString text = args.value(QLatin1String("text")).toString();
    if (text.isEmpty()) {
        sendError(client, id, QStringLiteral("missing text"));
        return;
    }
    // v0.1.133: formatter options, only those the formatter really has.
    if (args.contains(QLatin1String("indent")) ||
        args.contains(QLatin1String("minify")) ||
        args.contains(QLatin1String("keyword_case"))) {
        QJsonObject a;
        a[QStringLiteral("kind")] = kind;
        a[QStringLiteral("text")] = text;
        QString e;
        int indent = 0;
        const bool json = kind == QLatin1String("json");
        const bool sql = kind == QLatin1String("sql");
        const bool html = kind == QLatin1String("html");
        if (args.contains(QLatin1String("minify")) && !json && !html)
            e = QStringLiteral("minify is only supported for json and html");
        else if (args.contains(QLatin1String("keyword_case")) && !sql)
            e = QStringLiteral("keyword_case is only supported for sql");
        if (e.isEmpty())
            e = optInt(args, "indent", sql ? 1 : 0, 8, json ? 4 : (sql ? 4 : 2),
                       false, &indent);
        a[QStringLiteral("indent")] = indent;
        if (e.isEmpty() && (json || html)) {
            bool minify = false;
            e = optBool(args, "minify", false, &minify);
            a[QStringLiteral("minify")] = minify;
        }
        if (e.isEmpty() && sql) {
            QString kc;
            e = optEnum(args, "keyword_case",
                        {QStringLiteral("upper"), QStringLiteral("lower")},
                        QStringLiteral("upper"), &kc);
            a[QStringLiteral("keyword_case")] = kc;
        }
        if (e.isEmpty() && !m_host.computeText)
            e = QStringLiteral("formatter options not supported by host");
        if (!e.isEmpty()) {
            sendError(client, id, e);
            return;
        }
        const QJsonObject r =
            m_host.computeText(QStringLiteral("format"), a, &e);
        if (!e.isEmpty()) {
            sendError(client, id, e);
            return;
        }
        sendResult(client, id, r);
        return;
    }
    QString errorMsg;
    const QString out = m_host.formatText(kind, text, &errorMsg);
    if (out.isEmpty()) {
        sendError(client, id,
                  errorMsg.isEmpty()
                      ? QStringLiteral("formatter returned empty output")
                      : errorMsg);
        return;
    }
    QJsonObject result;
    result[QStringLiteral("text")] = out;
    sendResult(client, id, result);
}

// Noter access — pure notes_storage/file reads with the SAME path + title
// conventions as the Noter panel (no Noter UI is ever constructed here).
void McpBridge::verbListNotes(QLocalSocket *client, int id, const QJsonObject &args) {
    QJsonArray notes;
    bool withTrash = false;
    if (const QString e = optBool(args, "include_trash", false, &withTrash); !e.isEmpty()) {
        sendError(client, id, e);
        return;
    }
    const QString root = m_host.notesRoot ? m_host.notesRoot() : QString();
    // A denied root lists as empty; report it instead of an empty success.
    const QString blocked = NotesStorage::accessBlockedMessage(root);
    if (!blocked.isEmpty()) {
        sendError(client, id, blocked);
        return;
    }
    if (!root.isEmpty() && QFileInfo(root).isDir()) {
        NotesStorage storage(root);
        const QString trashPrefix =
            QDir(root).absoluteFilePath(QStringLiteral("Trash")) +
            QLatin1Char('/');
        const QStringList all = storage.listAllNotes(); // mtime desc
        for (const QString &path : all) {
            const QFileInfo fi(path);
            // Exclude the Trash archive and .trashed- strays (unless asked).
            const bool trashed = fi.absoluteFilePath().startsWith(trashPrefix) ||
                                 fi.fileName().startsWith(QLatin1String(".trashed-"));
            if (trashed && !withTrash) continue;
            QJsonObject o;
            if (withTrash) o[QStringLiteral("trashed")] = trashed;
            o[QStringLiteral("title")] = storage.displayTitleForFile(path);
            o[QStringLiteral("file")] =
                QDir::toNativeSeparators(fi.absoluteFilePath());
            o[QStringLiteral("modified_iso")] =
                fi.lastModified().toString(Qt::ISODate);
            notes.append(o);
        }
        // Trashed notes are dot-files (".trashed-<ts>-name.html"), which
        // listAllNotes never sees; list them from the Trash folder itself.
        if (withTrash) {
            const QDir trash(QDir(root).absoluteFilePath(QStringLiteral("Trash")));
            const QFileInfoList list = trash.entryInfoList(
                {QStringLiteral(".trashed-*.html")}, QDir::Files | QDir::Hidden, QDir::Time);
            for (const QFileInfo &fi : list) {
                QJsonObject o;
                o[QStringLiteral("trashed")] = true;
                o[QStringLiteral("title")] = storage.displayTitleForFile(fi.absoluteFilePath());
                o[QStringLiteral("file")] = QDir::toNativeSeparators(fi.absoluteFilePath());
                o[QStringLiteral("modified_iso")] = fi.lastModified().toString(Qt::ISODate);
                notes.append(o);
            }
        }
    }
    QJsonObject result;
    result[QStringLiteral("notes")] = notes;
    sendResult(client, id, result);
}

// Containment law lives here, shared by read_note and every v0.1.119 note
// verb: only an EXISTING .html file whose canonical path sits inside the
// Noter root resolves — ../ escapes and out-of-root absolutes canonicalize
// away and are rejected.
QString McpBridge::resolveNotePath(const QJsonObject &args,
                                   QString *err) const {
    const QString fileArg = QDir::fromNativeSeparators(
        args.value(QLatin1String("file")).toString());
    if (fileArg.isEmpty()) {
        if (err) *err = QStringLiteral("missing file");
        return QString();
    }
    const QString root = m_host.notesRoot ? m_host.notesRoot() : QString();
    if (root.isEmpty()) {
        if (err) *err = QStringLiteral("no Noter storage configured");
        return QString();
    }
    const QString blocked = NotesStorage::accessBlockedMessage(root);
    if (!blocked.isEmpty()) {
        if (err) *err = blocked;
        return QString();
    }
    const QString rootCanon = QFileInfo(root).canonicalFilePath();
    // v0.1.133 (D7b): a relative path is relative to the Noter ROOT, never
    // to the app's working directory. Canonicalized and confined below.
    const QFileInfo fi(QDir::isAbsolutePath(fileArg)
                           ? fileArg
                           : QDir(root).absoluteFilePath(fileArg));
    const QString fileCanon = fi.canonicalFilePath();
    if (rootCanon.isEmpty() || fileCanon.isEmpty() || !fi.isFile() ||
        !fileCanon.endsWith(QLatin1String(".html"), Qt::CaseInsensitive) ||
        !fileCanon.startsWith(rootCanon + QLatin1Char('/'))) {
        if (err)
            *err = QStringLiteral(
                       "not a note file: %1 (expected an existing .html note "
                       "inside the Noter folder %2, given as the absolute "
                       "path list_notes returns or relative to that folder)")
                       .arg(QDir::toNativeSeparators(fileArg),
                            QDir::toNativeSeparators(root));
        return QString();
    }
    return fileCanon;
}

void McpBridge::verbReadNote(QLocalSocket *client, int id,
                             const QJsonObject &args) {
    QString err;
    const QString fileCanon = resolveNotePath(args, &err);
    if (fileCanon.isEmpty()) {
        sendError(client, id, err);
        return;
    }
    const QString root = m_host.notesRoot ? m_host.notesRoot() : QString();
    const QFileInfo fi(fileCanon);
    if (fi.size() > kMaxNoteBytes) {
        sendError(client, id,
                  QStringLiteral("note too large (%1 bytes, cap %2)")
                      .arg(fi.size()).arg(kMaxNoteBytes));
        return;
    }
    NotesStorage storage(root);
    QString readErr;
    const QString html = storage.readNote(fileCanon, &readErr);
    if (html.isEmpty()) {
        sendError(client, id,
                  readErr.isEmpty() ? QStringLiteral("could not read note")
                                    : readErr);
        return;
    }
    QJsonObject result;
    result[QStringLiteral("title")] = storage.displayTitleForFile(fileCanon);
    // Same HTML→plaintext conversion as Noter's search prewarm.
    result[QStringLiteral("text")] = NotesStorage::plainTextForSearch(html);
    sendResult(client, id, result);
}

// ── v0.1.118 WRITE tier — human-approval-gated buffer mutations ─────────
//
// Contract: a write verb NEVER executes when it arrives. Validation errors
// answer immediately; anything that would mutate is parked in a FIFO queue
// and shown to the human as ONE non-modal in-window card at a time. Only a
// click on Approve runs the mutation (on the GUI thread); Deny, the
// timeout, or a peer disconnect all resolve to "nothing happened".

int McpBridge::resolveWriteTab(const QJsonObject &args, QString *err) const {
    return resolveTab(args, "tab_index", /*allowCurrent=*/true, err);
}

qint64 McpBridge::tabIdOf(int idx) const {
    return m_host.tabId ? m_host.tabId(idx) : -1;
}

// See the header for why this exists rather than a sixth per-verb check.
bool McpBridge::tabTextFor(int idx, QString *out, QString *refusal) const {
    const QString path = m_host.tabPath ? m_host.tabPath(idx) : QString();
    const QString deny = credentialRefusal(path);
    if (!deny.isEmpty()) {
        if (refusal) *refusal = deny;
        return false;
    }
    if (out) *out = m_host.tabText ? m_host.tabText(idx) : QString();
    return true;
}

bool McpBridge::selectionText(QString *out, int *tabIndex,
                              QString *refusal) const {
    int idx = -1;
    const QString text = m_host.selection ? m_host.selection(&idx) : QString();
    if (tabIndex) *tabIndex = idx;
    // A selection the host cannot attribute to a tab (idx < 0) has no path to
    // check. That is the ONE case this cannot judge, so it is returned — the
    // alternative is refusing every unattributed selection, which would break
    // get_selection on hosts that do not track the owning tab at all.
    const int n = m_host.tabCount ? m_host.tabCount() : 0;
    if (idx >= 0 && idx < n) {
        const QString path = m_host.tabPath ? m_host.tabPath(idx) : QString();
        const QString deny = credentialRefusal(path);
        if (!deny.isEmpty()) {
            if (refusal) *refusal = deny;
            return false;
        }
    }
    if (out) *out = text;
    return true;
}

// See the header for the selector precedence. Every verb that names a tab
// goes through here, so a selector accepted by one is accepted by all.
int McpBridge::resolveTab(const QJsonObject &args, const char *indexKey,
                          bool allowCurrent, QString *err) const {
    const int n = m_host.tabCount ? m_host.tabCount() : 0;
    const auto fail = [err](const QString &msg) {
        if (err) *err = msg;
        return -1;
    };

    // 1. Stable id — checked first, because a caller that took the trouble to
    //    hold one is explicitly asking not to be resolved positionally.
    if (args.contains(QLatin1String("tab_id"))) {
        const QJsonValue v = args.value(QLatin1String("tab_id"));
        if (!v.isDouble())
            return fail(QStringLiteral("tab_id must be an integer"));
        const qint64 want = static_cast<qint64>(v.toDouble());
        for (int i = 0; i < n; ++i)
            if (tabIdOf(i) == want) return i;
        // The id is gone, not merely out of range: say so plainly rather than
        // reporting a bad index the caller never supplied.
        return fail(QStringLiteral(
                        "no open tab has id %1 — it was closed. Call "
                        "list_open_tabs again for current ids.")
                        .arg(want));
    }

    // 2. Positional index, under whichever key this verb family shipped with.
    const QLatin1String key(indexKey);
    if (args.contains(key)) {
        const QJsonValue v = args.value(key);
        if (!v.isDouble())
            return fail(QStringLiteral("%1 must be an integer").arg(key));
        const int idx = v.toInt(-1);
        if (idx < 0 || idx >= n)
            return fail(QStringLiteral("tab index out of range: %1").arg(idx));
        return idx;
    }

    // 3. Title.
    if (args.contains(QLatin1String("title"))) {
        const QString title = args.value(QLatin1String("title")).toString();
        for (int i = 0; i < n; ++i)
            if (m_host.tabTitle && m_host.tabTitle(i) == title) return i;
        return fail(QStringLiteral("no tab titled: %1").arg(title));
    }

    if (allowCurrent) {
        const int idx = m_host.currentTabIndex ? m_host.currentTabIndex() : -1;
        if (idx < 0 || idx >= n)
            return fail(QStringLiteral("tab index out of range: %1").arg(idx));
        return idx;
    }
    return fail(QStringLiteral("missing tab_id, %1 or title").arg(key));
}

QString McpBridge::tabLabel(int idx) const {
    const QString t = m_host.tabTitle ? m_host.tabTitle(idx) : QString();
    return t.isEmpty() ? QStringLiteral("tab %1").arg(idx) : t;
}

// A host that cannot classify tabs (older editor / test fake with no
// tabEditable lambda) is trusted to have only editable text tabs, so every
// existing wire contract keeps working unchanged. (v0.1.121, issue #1)
bool McpBridge::tabIsEditable(int idx) const {
    return !m_host.tabEditable || m_host.tabEditable(idx);
}

// v0.1.121 (issue #6): a specific, consistent reason string. The Welcome tab
// (title "Welcome", the app's index-0 page) is named outright; any other
// non-editable tab reports its title so the agent knows what it hit.
QString McpBridge::nonEditableReason(int idx) const {
    const QString title = m_host.tabTitle ? m_host.tabTitle(idx) : QString();
    if (title == QLatin1String("Welcome"))
        return QStringLiteral(
                   "tab %1 is the Welcome tab and is not an editable buffer")
            .arg(idx);
    return QStringLiteral(
               "tab %1 is not an editable text buffer (it is \"%2\")")
        .arg(idx)
        .arg(title);
}

void McpBridge::verbInsertText(QLocalSocket *client, int id,
                               const QJsonObject &args) {
    if (!m_host.insertText) {
        sendError(client, id,
                  QStringLiteral("insert_text not supported by host"));
        return;
    }
    const QString text = args.value(QLatin1String("text")).toString();
    if (text.isEmpty()) {
        sendError(client, id, QStringLiteral("missing text"));
        return;
    }
    QString err;
    const int idx = resolveWriteTab(args, &err);
    if (idx < 0) {
        sendError(client, id, err);
        return;
    }
    if (!tabIsEditable(idx)) { // v0.1.121: never a card for a non-editable tab
        sendError(client, id, nonEditableReason(idx));
        return;
    }
    if (const QString deny = tabWriteRefusal(idx); !deny.isEmpty()) {
        sendError(client, id, deny); // D12: never a card for a protected file
        return;
    }
    int line = -1, col = -1;
    // `col` alone is meaningless and used to be silently DISCARDED: the insert
    // then went to the cursor, so an assistant asking for column 1 of wherever
    // it thought it was landed somewhere else entirely — behind an approval
    // card whose text said "at the cursor" and was therefore truthful but
    // unread. Reject it and say which argument is missing.
    if (args.contains(QLatin1String("col")) &&
        !args.contains(QLatin1String("line"))) {
        sendError(client, id,
                  QStringLiteral("col requires line — pass both, or neither "
                                 "to insert at the cursor"));
        return;
    }
    if (args.contains(QLatin1String("line"))) {
        line = args.value(QLatin1String("line")).toInt(0);
        col = args.contains(QLatin1String("col"))
                  ? args.value(QLatin1String("col")).toInt(0)
                  : 1;
        if (line < 1 || col < 1) {
            sendError(client, id,
                      QStringLiteral("line and col must be >= 1"));
            return;
        }
    }
    const QString desc = line >= 1
        ? QStringLiteral("insert %1 chars into '%2' at line %3, col %4")
              .arg(text.size()).arg(tabLabel(idx)).arg(line).arg(col)
        : QStringLiteral("insert %1 chars into '%2' at the cursor")
              .arg(text.size()).arg(tabLabel(idx));
    // R17: the whole inserted text, scrollable; never cut to a teaser.
    CardOptions o;
    o.fullPreview = true;
    const QString preview = text.isEmpty() ? QStringLiteral("(empty text)")
                                           : fullTextPreview(text, &o.warning);
    enqueueTabApproval(client, id, idx, desc, preview,
                       [this, line, col, text](int live, QString *execErr) {
        if (!m_host.insertText(live, line, col, text)) {
            // v0.1.121 (issue #6): name the tab and the likely cause.
            *execErr = QStringLiteral(
                           "could not insert into tab %1 (line/col out of "
                           "range?)")
                           .arg(live);
            return QJsonObject();
        }
        return QJsonObject();
    }, o);
}

void McpBridge::verbReplaceSelection(QLocalSocket *client, int id,
                                     const QJsonObject &args) {
    if (!m_host.replaceSelection) {
        sendError(client, id,
                  QStringLiteral("replace_selection not supported by host"));
        return;
    }
    if (!args.contains(QLatin1String("text"))) {
        sendError(client, id, QStringLiteral("missing text"));
        return;
    }
    const QString text = args.value(QLatin1String("text")).toString();
    QString err;
    const int idx = resolveWriteTab(args, &err);
    if (idx < 0) {
        sendError(client, id, err);
        return;
    }
    if (!tabIsEditable(idx)) { // v0.1.121
        sendError(client, id, nonEditableReason(idx));
        return;
    }
    if (const QString deny = tabWriteRefusal(idx); !deny.isEmpty()) {
        sendError(client, id, deny);
        return;
    }
    // Fail fast: never ask the human to approve a no-op.
    if (m_host.hasSelection && !m_host.hasSelection(idx)) {
        sendError(client, id, QStringLiteral("no selection"));
        return;
    }
    const QString desc =
        QStringLiteral("replace the selection in '%1' with %2 chars")
            .arg(tabLabel(idx)).arg(text.size());
    CardOptions o;
    o.fullPreview = true;
    enqueueTabApproval(client, id, idx, desc,
                       text.isEmpty()
                           ? QStringLiteral("(delete the selection)")
                           : fullTextPreview(text, &o.warning),
                       [this, text](int live, QString *execErr) {
        // Re-checked at execute time: the selection may have vanished while
        // the card was waiting for a click.
        if (!m_host.replaceSelection(live, text)) {
            *execErr = QStringLiteral("no selection");
            return QJsonObject();
        }
        return QJsonObject();
    }, o);
}

void McpBridge::verbApplyEdit(QLocalSocket *client, int id,
                              const QJsonObject &args) {
    if (!m_host.applyEdit) {
        sendError(client, id,
                  QStringLiteral("apply_edit not supported by host"));
        return;
    }
    const QString find = args.value(QLatin1String("find")).toString();
    if (find.isEmpty()) {
        sendError(client, id, QStringLiteral("missing find"));
        return;
    }
    if (!args.contains(QLatin1String("replace"))) {
        sendError(client, id, QStringLiteral("missing replace"));
        return;
    }
    const QString replace = args.value(QLatin1String("replace")).toString();
    const bool all = args.value(QLatin1String("all")).toBool(false);
    // v0.1.133: regex? match_case? whole_word? scope? — any non-default goes
    // through the Replace dialog's engine (RustCore count + replace) on the
    // host; the defaults keep the literal, case-sensitive path below.
    {
        bool regex = false, cs = true, ww = false;
        QString scope, e;
        if ((e = optBool(args, "regex", false, &regex)).isEmpty() &&
            (e = optBool(args, "match_case", true, &cs)).isEmpty() &&
            (e = optBool(args, "whole_word", false, &ww)).isEmpty())
            e = optEnum(args, "scope", {QStringLiteral("tab"), QStringLiteral("all_open")},
                        QStringLiteral("tab"), &scope);
        if (!e.isEmpty()) {
            sendError(client, id, e);
            return;
        }
        if (regex || !cs || ww || scope == QLatin1String("all_open")) {
            QJsonObject o;
            o[QStringLiteral("find")] = find;
            o[QStringLiteral("replace")] = replace;
            o[QStringLiteral("all")] = all;
            o[QStringLiteral("regex")] = regex;
            o[QStringLiteral("match_case")] = cs;
            o[QStringLiteral("whole_word")] = ww;
            applyEditEx(client, id, args, o, scope == QLatin1String("all_open"));
            return;
        }
    }
    QString err;
    const int idx = resolveWriteTab(args, &err);
    if (idx < 0) {
        sendError(client, id, err);
        return;
    }
    // Literal, case-sensitive pre-check: a no-match request fails fast
    // without burning a human decision.
    //
    // NP-14: this reads the buffer too, and it is an ORACLE — "no match" vs a
    // card naming the match tells a caller what the file contains, one probe
    // at a time, without ever returning the text. Editing a credential file
    // is also the thing the deny-list exists to prevent.
    QString text;
    QString denied;
    if (!tabTextFor(idx, &text, &denied)) {
        sendError(client, id, denied);
        return;
    }
    if (!text.contains(find)) {
        sendError(client, id, QStringLiteral("no match"));
        return;
    }
    if (const QString deny = tabWriteRefusal(idx); !deny.isEmpty()) {
        sendError(client, id, deny);
        return;
    }
    // D4: what the card describes, pinned. The first match's offset (or the
    // total count for all=true) must be unchanged when the human approves.
    const auto fingerprint = [find, all](const QString &t) {
        if (!all) return t.indexOf(find);
        int n = 0;
        for (int at = t.indexOf(find); at >= 0;
             at = t.indexOf(find, at + find.size()))
            ++n;
        return n;
    };
    const int pinned = fingerprint(text);
    const QString desc =
        QStringLiteral("replace %1 '%2' with '%3' in '%4'")
            .arg(all ? QStringLiteral("every") : QStringLiteral("the first"),
                 elideForCard(find, kApprovalDescChars),
                 elideForCard(replace, kApprovalDescChars), tabLabel(idx));
    CardOptions o;
    o.fullPreview = true;
    QString findNote, replNote;
    const QString preview = QStringLiteral("Find:\n") + fullTextPreview(find, &findNote) +
                            QStringLiteral("\n\nReplace with:\n") +
                            (replace.isEmpty() ? QStringLiteral("(nothing: the match is deleted)")
                                               : fullTextPreview(replace, &replNote));
    o.warning = (findNote + QLatin1Char(' ') + replNote).trimmed();
    enqueueTabApproval(client, id, idx, desc, preview,
                       [this, find, replace, all, pinned,
                        fingerprint](int live, QString *execErr) {
        QString now;
        if (!tabTextFor(live, &now, execErr)) return QJsonObject();
        if (!now.contains(find)) {
            *execErr = QStringLiteral("no match");
            return QJsonObject();
        }
        if (fingerprint(now) != pinned) {
            *execErr = QStringLiteral(
                "the document changed while the approval card was waiting, "
                "so the match the card described may not be the one that "
                "would be replaced. Nothing was changed; call apply_edit "
                "again.");
            return QJsonObject();
        }
        const int count = m_host.applyEdit(live, find, replace, all);
        if (count < 0) {
            *execErr = QStringLiteral("could not apply edit");
            return QJsonObject();
        }
        if (count == 0) { // buffer changed while the card was pending
            *execErr = QStringLiteral("no match");
            return QJsonObject();
        }
        QJsonObject r;
        r[QStringLiteral("count")] = count;
        return r;
    }, o);
}

void McpBridge::verbSaveTab(QLocalSocket *client, int id,
                            const QJsonObject &args) {
    if (!m_host.saveTab) {
        sendError(client, id,
                  QStringLiteral("save_tab not supported by host"));
        return;
    }
    QString err;
    const int idx = resolveWriteTab(args, &err);
    if (idx < 0) {
        sendError(client, id, err);
        return;
    }
    if (!tabIsEditable(idx)) { // v0.1.121
        sendError(client, id, nonEditableReason(idx));
        return;
    }
    const QString currentPath = m_host.tabPath ? m_host.tabPath(idx)
                                               : QString();
    // v0.1.121 (issue #4): an optional "path" turns this into a Save As. The
    // path is VALIDATED here, before any card is shown, so the human always
    // sees the exact destination the write would hit.
    const QString explicitPath = args.value(QLatin1String("path")).toString();
    const bool saveAs = !explicitPath.isEmpty();
    QString destPath;
    if (saveAs) {
        if (!m_host.saveTabAs) {
            sendError(client, id,
                      QStringLiteral(
                          "save_tab with a path is not supported by host"));
            return;
        }
        if (!QDir::isAbsolutePath(explicitPath)) {
            sendError(client, id,
                      QStringLiteral("save_tab path must be absolute: %1")
                          .arg(QDir::toNativeSeparators(explicitPath)));
            return;
        }
        const QFileInfo fi(explicitPath);
        // The parent folder must already exist — save_tab writes ONE file, it
        // never creates directories.
        if (!QDir(fi.absolutePath()).exists()) {
            sendError(client, id,
                      QStringLiteral(
                          "save_tab path's folder does not exist: %1")
                          .arg(QDir::toNativeSeparators(fi.absolutePath())));
            return;
        }
        destPath = fi.absoluteFilePath();
    } else if (!currentPath.isEmpty()) {
        destPath = currentPath;
    } else {
        // Untitled tab and no path: this verb NEVER opens a Save As dialog.
        sendError(client, id,
                  QStringLiteral("save_tab needs a path: this tab has never "
                                 "been saved (pass \"path\")"));
        return;
    }
    // D12: the destination, and (for Save As) the tab's own file too, since
    // a credential buffer must not be copied anywhere either.
    for (const QString &p : {destPath, currentPath}) {
        if (const QString deny = writeDestinationRefusal(p); !deny.isEmpty()) {
            sendError(client, id, deny);
            return;
        }
    }
    // Card shows the destination; a pre-existing file is flagged OVERWRITE.
    const bool overwrite = QFileInfo::exists(destPath);
    const QString shownDest =
        sanitizeForCard(QDir::toNativeSeparators(destPath));
    const QString desc =
        QStringLiteral("%1save '%2' to %3")
            .arg(overwrite ? QStringLiteral("OVERWRITE — ") : QString(),
                 tabLabel(idx), shownDest);
    enqueueTabApproval(client, id, idx, desc, shownDest,
                       [this, saveAs, destPath](int live, QString *execErr) {
        bool ok;
        if (saveAs) {
            ok = m_host.saveTabAs(live, destPath);
        } else {
            // Re-check: the tab may have been retargeted while the card
            // waited (Save As, rename); never save it somewhere unshown.
            const QString nowPath = m_host.tabPath ? m_host.tabPath(live)
                                                   : QString();
            if (nowPath.isEmpty()) {
                *execErr = QStringLiteral("save_tab needs a path: this tab has "
                                          "never been saved (pass \"path\")");
                return QJsonObject();
            }
            if (QFileInfo(nowPath).absoluteFilePath() !=
                QFileInfo(destPath).absoluteFilePath()) {
                *execErr = QStringLiteral(
                               "the tab now points at %1, not the file the "
                               "approval card named; nothing was saved")
                               .arg(QDir::toNativeSeparators(nowPath));
                return QJsonObject();
            }
            ok = m_host.saveTab(live);
        }
        if (!ok) {
            *execErr = QStringLiteral("could not save: %1")
                           .arg(QDir::toNativeSeparators(destPath));
            return QJsonObject();
        }
        QJsonObject r;
        r[QStringLiteral("saved")] = true;
        r[QStringLiteral("path")] = QDir::toNativeSeparators(destPath);
        return r;
    });
}

// ── v0.1.119 depth wave — READ tier ────────────────────────────────────

void McpBridge::verbListReminders(QLocalSocket *client, int id) {
    QJsonArray out;
    if (m_host.reminders) {
        const QJsonArray raw = m_host.reminders();
        // Same bucketing the Noter "Reminders" sidebar uses
        // (NotesPanel::populateRemindersRoot): overdue / today / within a
        // week / later, computed against LOCAL time.
        const QDateTime now = QDateTime::currentDateTime();
        const QDate today = now.date();
        for (const QJsonValue &v : raw) {
            const QJsonObject r = v.toObject();
            const QString dueIso =
                r.value(QLatin1String("due_iso")).toString();
            const QDateTime due =
                QDateTime::fromString(dueIso, Qt::ISODate).toLocalTime();
            QString bucket;
            if (!due.isValid()) bucket = QStringLiteral("Later");
            else if (due < now) bucket = QStringLiteral("Overdue");
            else if (due.date() == today) bucket = QStringLiteral("Today");
            else if (due.date() <= today.addDays(7))
                bucket = QStringLiteral("This week");
            else bucket = QStringLiteral("Later");
            QJsonObject o;
            if (r.contains(QLatin1String("id")))
                o[QStringLiteral("id")] = r.value(QLatin1String("id")).toString();
            o[QStringLiteral("note_file")] =
                r.value(QLatin1String("note_file")).toString();
            o[QStringLiteral("note_title")] =
                r.value(QLatin1String("note_title")).toString();
            o[QStringLiteral("due_iso")] = dueIso;
            o[QStringLiteral("bucket")] = bucket;
            out.append(o);
        }
    }
    QJsonObject result;
    result[QStringLiteral("reminders")] = out;
    sendResult(client, id, result);
}

// Read-only git. `sub` is one of the bridge's five fixed tokens — the client
// can never smuggle another subcommand, only the optional path/limit/ref
// arg. The heavy QProcess lifting is the host's (reusing git_tools.cpp).
void McpBridge::verbGit(QLocalSocket *client, int id, const QJsonObject &args,
                        const QString &sub) {
    if (!m_host.runGit) {
        sendError(client, id, QStringLiteral("git not supported by host"));
        return;
    }
    QString err;
    QString out = m_host.runGit(sub, args, &err);
    if (!err.isEmpty()) {
        sendError(client, id, err);
        return;
    }
    bool truncated = false;
    if (out.size() > kGitOutputCap) {
        out.truncate(kGitOutputCap);
        out += QStringLiteral("\n[... truncated at 256 KB ...]");
        truncated = true;
    }
    QJsonObject result;
    result[QStringLiteral("output")] = out;
    if (truncated) result[QStringLiteral("truncated")] = true;
    sendResult(client, id, result);
}

// Parse-only .npd validation. NEVER touches a tab/canvas — Npd::parse is a
// pure QtCore function the bridge links directly.
// Npd::parse + "line N: msg" split, shared by validate_npd / create_diagram /
// set_diagram_source.
static QJsonObject npdValidationJson(const QString &source) {
    const Npd::Diagram d = Npd::parse(source);
    static const QRegularExpression rx(QStringLiteral("^line (\\d+): (.*)$"));
    QJsonArray errors;
    for (const QString &e : d.errors) {
        int line = 0;
        QString message = e;
        const QRegularExpressionMatch m = rx.match(e);
        if (m.hasMatch()) {
            line = m.captured(1).toInt();
            message = m.captured(2);
        }
        QJsonObject o;
        o[QStringLiteral("line")] = line;
        o[QStringLiteral("message")] = message;
        errors.append(o);
    }
    QJsonObject r;
    r[QStringLiteral("valid")] = d.ok();
    r[QStringLiteral("errors")] = errors;
    return r;
}

void McpBridge::verbValidateNpd(QLocalSocket *client, int id,
                                const QJsonObject &args) {
    QString source;
    if (args.contains(QLatin1String("source"))) {
        source = args.value(QLatin1String("source")).toString();
    } else if (args.contains(QLatin1String("tab_index")) ||
               args.contains(QLatin1String("tab_id"))) {
        // v0.1.133 (D2): tab_id is accepted, through the common resolver.
        QString err;
        const int idx = resolveTab(args, "tab_index", false, &err);
        if (idx < 0) {
            sendError(client, id, err);
            return;
        }
        if (!m_host.diagramSource || !m_host.diagramSource(idx, &source)) {
            sendError(client, id,
                      QStringLiteral("tab %1 is not a diagram (.npd) tab")
                          .arg(idx));
            return;
        }
    } else {
        sendError(client, id,
                  QStringLiteral("missing source, tab_id or tab_index"));
        return;
    }
    sendResult(client, id, npdValidationJson(source));
}

// SELECT-only query. The host lambda routes SQL through the real
// DbConnections::classifySql gate and runs it with allowMutation=false, so a
// non-read-only query never executes; the bridge surfaces the rejection and
// re-caps the row count.
void McpBridge::verbRunSql(QLocalSocket *client, int id,
                           const QJsonObject &args) {
    if (!m_host.runSql) {
        sendError(client, id, QStringLiteral("run_sql not supported by host"));
        return;
    }
    const QString sql = args.value(QLatin1String("sql")).toString();
    if (sql.trimmed().isEmpty()) {
        sendError(client, id, QStringLiteral("missing sql"));
        return;
    }
    const QString csvPath =
        args.value(QLatin1String("csv_path")).toString();
    QString err;
    const QJsonObject r = m_host.runSql(sql, csvPath, &err);
    if (!err.isEmpty()) {
        sendError(client, id, err);
        return;
    }
    QJsonObject result;
    result[QStringLiteral("columns")] = r.value(QLatin1String("columns")).toArray();
    result[QStringLiteral("rows")] = r.value(QLatin1String("rows")).toArray();
    result[QStringLiteral("truncated")] = r.value(QLatin1String("truncated")).toBool(false);
    result[QStringLiteral("engine")] = r.value(QLatin1String("engine")).toString();
    // Defensive re-cap (row + per-cell): a single cell must not exfiltrate a
    // whole file (e.g. read_text over a huge file that slipped a gate).
    capSqlResultRows(result);
    sendResult(client, id, result);
}

// ── Phase 0A read tier ─────────────────────────────────────────────────

void McpBridge::verbListLanguages(QLocalSocket *client, int id) {
    QJsonArray langs;
    if (m_host.knownLanguages) {
        const QStringList list = m_host.knownLanguages();
        for (const QString &l : list) langs.append(l);
    }
    QJsonObject result;
    result[QStringLiteral("languages")] = langs;
    sendResult(client, id, result);
}

// Edition/platform/version/features from THIS binary's build state; the
// Rust sidecar adds tool_count and tiers (it owns the tool surface).
void McpBridge::verbGetCapabilities(QLocalSocket *client, int id) {
    QJsonObject features;
#ifdef NOTEPATRA_HAVE_DUCKDB
    features[QStringLiteral("duckdb")] = true;
#else
    features[QStringLiteral("duckdb")] = false;
#endif
#ifdef NOTEPATRA_WITH_WEBENGINE
    features[QStringLiteral("webengine")] = true;
#else
    features[QStringLiteral("webengine")] = false;
#endif
    features[QStringLiteral("noter")] = static_cast<bool>(m_host.notesRoot);
    QJsonObject result;
    result[QStringLiteral("edition")] = QLatin1String(editionName());
    result[QStringLiteral("platform")] = QLatin1String(platformName());
    result[QStringLiteral("version")] = QStringLiteral(NOTEPATRA_VERSION);
    result[QStringLiteral("features")] = features;
    // v0.1.133: what export_diagram accepts in THIS build, asked at runtime.
    result[QStringLiteral("diagram_export_formats")] =
        QJsonArray::fromStringList(
            m_host.diagramExportFormats
                ? m_host.diagramExportFormats()
                : QStringList{QStringLiteral("png"), QStringLiteral("pdf")});
    sendResult(client, id, result);
}

// ── v0.1.119 depth wave — ACT tier (visible, no card) ──────────────────

void McpBridge::verbOpenNote(QLocalSocket *client, int id,
                             const QJsonObject &args) {
    QString err;
    const QString fileCanon = resolveNotePath(args, &err);
    if (fileCanon.isEmpty()) { // includes missing-file / escape rejection
        sendError(client, id, err);
        return;
    }
    if (!m_host.openNote) {
        sendError(client, id, QStringLiteral("open_note not supported by host"));
        return;
    }
    QString openErr;
    const QString title = m_host.openNote(fileCanon, &openErr);
    if (!openErr.isEmpty()) {
        sendError(client, id, openErr);
        return;
    }
    QJsonObject result;
    result[QStringLiteral("opened")] = true;
    result[QStringLiteral("title")] = title;
    sendResult(client, id, result);
}

// ── v0.1.119 depth wave — WRITE tier (human-approval-gated) ─────────────

// Title label for a Noter card: the containment check already proved the
// path is inside the root, so a NotesStorage title read is safe.
static QString noteTitleForCard(const QString &root, const QString &fileCanon) {
    if (!root.isEmpty()) {
        NotesStorage storage(root);
        const QString t = storage.displayTitleForFile(fileCanon);
        if (!t.isEmpty()) return t;
    }
    return QFileInfo(fileCanon).fileName();
}

void McpBridge::verbCreateNote(QLocalSocket *client, int id,
                               const QJsonObject &args) {
    if (!m_host.createNote) {
        sendError(client, id,
                  QStringLiteral("create_note not supported by host"));
        return;
    }
    const QString title = args.value(QLatin1String("title")).toString();
    if (title.trimmed().isEmpty()) {
        sendError(client, id, QStringLiteral("missing title"));
        return;
    }
    const QString body = args.value(QLatin1String("body")).toString();
    if (body.size() > kCreateNoteMaxChars) {
        sendError(client, id, QStringLiteral("body too large"));
        return;
    }
    const QString desc = QStringLiteral("create note '%1' (%2 chars)")
                             .arg(elideForCard(title, kApprovalDescChars))
                             .arg(body.size());
    CardOptions o;
    o.fullPreview = true;
    const QString preview = body.isEmpty() ? QStringLiteral("(empty body)")
                                           : fullTextPreview(body, &o.warning);
    enqueueApprovalEx(client, id, desc, preview, o,
                    [this, title, body](QString *execErr) {
        QString e;
        const QString path = m_host.createNote(title, body, &e);
        if (path.isEmpty()) {
            *execErr = e.isEmpty() ? QStringLiteral("could not create note") : e;
            return QJsonObject();
        }
        QJsonObject r;
        r[QStringLiteral("file")] = QDir::toNativeSeparators(path);
        r[QStringLiteral("title")] = title;
        return r;
    });
}

void McpBridge::verbAppendNote(QLocalSocket *client, int id,
                               const QJsonObject &args) {
    if (!m_host.appendNote) {
        sendError(client, id,
                  QStringLiteral("append_note not supported by host"));
        return;
    }
    const QString text = args.value(QLatin1String("text")).toString();
    if (text.isEmpty()) {
        sendError(client, id, QStringLiteral("missing text"));
        return;
    }
    if (text.size() > kAppendNoteMaxChars) {
        sendError(client, id, QStringLiteral("text too large"));
        return;
    }
    QString err;
    const QString fileCanon = resolveNotePath(args, &err);
    if (fileCanon.isEmpty()) {
        sendError(client, id, err);
        return;
    }
    const QString root = m_host.notesRoot ? m_host.notesRoot() : QString();
    const QString title = noteTitleForCard(root, fileCanon);
    const QString desc = QStringLiteral("append %1 chars to '%2'")
                             .arg(text.size())
                             .arg(elideForCard(title, kApprovalDescChars));
    CardOptions o;
    o.fullPreview = true;
    const QString preview = fullTextPreview(text, &o.warning);
    enqueueApprovalEx(client, id, desc, preview, o,
                    [this, fileCanon, text](QString *execErr) {
        QString e;
        if (!m_host.appendNote(fileCanon, text, &e)) {
            *execErr = e.isEmpty() ? QStringLiteral("could not append") : e;
            return QJsonObject();
        }
        QJsonObject r;
        r[QStringLiteral("file")] = QDir::toNativeSeparators(fileCanon);
        return r;
    });
}

void McpBridge::verbSetReminder(QLocalSocket *client, int id,
                                const QJsonObject &args) {
    if (!m_host.setReminder) {
        sendError(client, id,
                  QStringLiteral("set_reminder not supported by host"));
        return;
    }
    const QString dueIso = args.value(QLatin1String("due_iso")).toString();
    if (dueIso.isEmpty()) {
        sendError(client, id, QStringLiteral("missing due_iso"));
        return;
    }
    const QDateTime due = QDateTime::fromString(dueIso, Qt::ISODate);
    if (!due.isValid()) {
        sendError(client, id,
                  QStringLiteral("invalid due_iso (expected ISO-8601): %1")
                      .arg(dueIso));
        return;
    }
    if (due <= QDateTime::currentDateTime()) {
        sendError(client, id, QStringLiteral("due_iso must be in the future"));
        return;
    }
    QString err;
    const QString fileCanon = resolveNotePath(args, &err);
    if (fileCanon.isEmpty()) {
        sendError(client, id, err);
        return;
    }
    const QString root = m_host.notesRoot ? m_host.notesRoot() : QString();
    const QString title = noteTitleForCard(root, fileCanon);
    const QString human =
        due.toLocalTime().toString(QStringLiteral("yyyy-MM-dd HH:mm"));
    const QString desc = QStringLiteral("set reminder on '%1' for %2")
                             .arg(elideForCard(title, kApprovalDescChars), human);
    enqueueApproval(client, id, desc, QString(),
                    [this, fileCanon, due, dueIso](QString *execErr) {
        QString e;
        if (!m_host.setReminder(fileCanon, due, &e)) {
            *execErr = e.isEmpty() ? QStringLiteral("could not set reminder") : e;
            return QJsonObject();
        }
        QJsonObject r;
        r[QStringLiteral("file")] = QDir::toNativeSeparators(fileCanon);
        r[QStringLiteral("due_iso")] = dueIso;
        return r;
    });
}

void McpBridge::verbExportDiagram(QLocalSocket *client, int id,
                                  const QJsonObject &args) {
    if (!m_host.exportDiagram) {
        sendError(client, id,
                  QStringLiteral("export_diagram not supported by host"));
        return;
    }
    // v0.1.133 (D2): tab_id OR tab_index, through the common resolver.
    QString selErr;
    const int idx = resolveTab(args, "tab_index", false, &selErr);
    if (idx < 0) {
        sendError(client, id, selErr);
        return;
    }
    // Fail fast when the tab isn't a diagram — never queue a doomed approval.
    QString probe;
    if (!m_host.diagramSource || !m_host.diagramSource(idx, &probe)) {
        sendError(client, id,
                  QStringLiteral("tab %1 is not a diagram (.npd) tab").arg(idx));
        return;
    }
    QString format = args.value(QLatin1String("format")).toString().toLower();
    if (format.isEmpty()) format = QStringLiteral("png");
    if (format == QLatin1String("jpg")) format = QStringLiteral("jpeg");
    // The accepted set is what THIS build's DiagramView can write, asked at
    // runtime; a host that cannot say keeps the historical png|pdf.
    QStringList formats = m_host.diagramExportFormats
                              ? m_host.diagramExportFormats()
                              : QStringList{QStringLiteral("png"),
                                            QStringLiteral("pdf")};
    if (!formats.contains(format)) {
        sendError(client, id,
                  QStringLiteral("unsupported format: %1 (this build "
                                 "supports: %2)")
                      .arg(format, formats.join(QLatin1Char('|'))));
        return;
    }
    const QString path =
        QDir::cleanPath(
            QDir::fromNativeSeparators(args.value(QLatin1String("path")).toString()));
    if (path.isEmpty()) {
        sendError(client, id, QStringLiteral("missing path"));
        return;
    }
    const QFileInfo fi(path);
    if (!fi.isAbsolute()) {
        sendError(client, id, QStringLiteral("path must be absolute"));
        return;
    }
    if (!QFileInfo(fi.absolutePath()).isDir()) {
        sendError(client, id,
                  QStringLiteral("parent directory does not exist: %1")
                      .arg(QDir::toNativeSeparators(fi.absolutePath())));
        return;
    }
    if (const QString deny = writeDestinationRefusal(path); !deny.isEmpty()) {
        sendError(client, id, deny);
        return;
    }
    // Card DISPLAY text only: sanitize bidi/control codepoints so a crafted
    // path can't visually disguise its destination, and flag an overwrite of
    // an existing file. The EXECUTED path below stays the exact validated
    // absolute path — only the human-shown string is transformed.
    const bool overwrites = QFileInfo(path).isFile();
    const QString shownPath = sanitizeForCard(QDir::toNativeSeparators(path));
    const QString shownDest = overwrites
        ? QStringLiteral("OVERWRITE existing file: ") + shownPath
        : shownPath;
    const QString desc =
        QStringLiteral("export '%1' as %2 to %3")
            .arg(elideForCard(tabLabel(idx), kApprovalDescChars),
                 format.toUpper(), shownDest);
    enqueueTabApproval(client, id, idx, desc, shownDest,
                       [this, path, format](int live, QString *execErr) {
        QString e;
        if (!m_host.exportDiagram(live, path, format, &e)) {
            *execErr = e.isEmpty()
                           ? QStringLiteral("export to %1 failed")
                                 .arg(QDir::toNativeSeparators(path))
                           : e;
            return QJsonObject();
        }
        QJsonObject r;
        r[QStringLiteral("path")] = QDir::toNativeSeparators(path);
        return r;
    });
}

// ── Phase 1 — diagram control + Noter panel ─────────────────────────────

// ACT: creates the tab even when the source is invalid .npd (the canvas
// shows its own parse state); valid/errors ride along so the agent knows.
void McpBridge::verbCreateDiagram(QLocalSocket *client, int id,
                                  const QJsonObject &args) {
    if (!m_host.createDiagram) {
        sendError(client, id,
                  QStringLiteral("create_diagram not supported by host"));
        return;
    }
    QString source = args.value(QLatin1String("source")).toString();
    if (source.size() > kDiagramSourceMaxChars) {
        sendError(client, id, QStringLiteral("source too large"));
        return;
    }
    // v0.1.133: template? — the Diagram tab's own New ▾ starter.
    if (args.contains(QLatin1String("template"))) {
        QString tpl;
        if (const QString e = optEnum(args, "template",
                                      {QStringLiteral("flow"), QStringLiteral("er"),
                                       QStringLiteral("system")},
                                      QString(), &tpl);
            !e.isEmpty()) {
            sendError(client, id, e);
            return;
        }
        if (!source.isEmpty()) {
            sendError(client, id,
                      QStringLiteral("provide source or template, not both"));
            return;
        }
        QString terr;
        QJsonObject ta;
        ta[QStringLiteral("template")] = tpl;
        source = m_host.computeText
                     ? m_host.computeText(QStringLiteral("diagram_template"), ta,
                                          &terr)
                           .value(QLatin1String("source")).toString()
                     : QString();
        if (source.isEmpty()) {
            sendError(client, id, QStringLiteral("template not supported by host"));
            return;
        }
    }
    const QString title = args.value(QLatin1String("title")).toString();
    const int idx = m_host.createDiagram(source, title);
    if (idx < 0) {
        sendError(client, id, QStringLiteral("could not create diagram tab"));
        return;
    }
    // Validate the tab's REAL content (covers the empty-source case too).
    QString actual = source;
    if (m_host.diagramSource) m_host.diagramSource(idx, &actual);
    QJsonObject result = npdValidationJson(actual);
    result[QStringLiteral("tab_index")] = idx;
    if (const qint64 tid = tabIdOf(idx); tid >= 0)
        result[QStringLiteral("tab_id")] = tid;
    sendResult(client, id, result);
}

void McpBridge::verbGetDiagramSource(QLocalSocket *client, int id,
                                     const QJsonObject &args) {
    // v0.1.133 (D2): tab_id OR tab_index, through the common resolver.
    QString selErr;
    const int idx = resolveTab(args, "tab_index", false, &selErr);
    if (idx < 0) {
        sendError(client, id, selErr);
        return;
    }
    QString src;
    if (!m_host.diagramSource || !m_host.diagramSource(idx, &src)) {
        sendError(client, id,
                  QStringLiteral("tab %1 is not a diagram (.npd) tab").arg(idx));
        return;
    }
    QJsonObject result;
    result[QStringLiteral("source")] = src;
    sendResult(client, id, result);
}

void McpBridge::verbSetDiagramSource(QLocalSocket *client, int id,
                                     const QJsonObject &args) {
    if (!m_host.setDiagramSource) {
        sendError(client, id,
                  QStringLiteral("set_diagram_source not supported by host"));
        return;
    }
    if (!args.contains(QLatin1String("source"))) {
        sendError(client, id, QStringLiteral("missing source"));
        return;
    }
    const QString source = args.value(QLatin1String("source")).toString();
    if (source.size() > kDiagramSourceMaxChars) {
        sendError(client, id, QStringLiteral("source too large"));
        return;
    }
    // v0.1.133 (D2): tab_id OR tab_index, through the common resolver.
    QString selErr;
    const int idx = resolveTab(args, "tab_index", false, &selErr);
    if (idx < 0) {
        sendError(client, id, selErr);
        return;
    }
    // Fail fast when the tab isn't a diagram — never queue a doomed approval.
    QString probe;
    if (!m_host.diagramSource || !m_host.diagramSource(idx, &probe)) {
        sendError(client, id,
                  QStringLiteral("tab %1 is not a diagram (.npd) tab").arg(idx));
        return;
    }
    const QString desc =
        QStringLiteral("REPLACE the diagram source of '%1' with %2 chars "
                       "(the canvas re-renders)")
            .arg(elideForCard(tabLabel(idx), kApprovalDescChars))
            .arg(source.size());
    CardOptions o;
    o.fullPreview = true;
    const QString preview = source.isEmpty() ? QStringLiteral("(empty source)")
                                             : fullTextPreview(source, &o.warning);
    enqueueTabApproval(client, id, idx, desc, preview,
                       [this, source](int live, QString *execErr) {
        // Re-checked at execute time: the tab may have changed while pending.
        if (!m_host.setDiagramSource(live, source)) {
            *execErr = QStringLiteral("tab %1 is not a diagram (.npd) tab")
                           .arg(live);
            return QJsonObject();
        }
        QJsonObject r = npdValidationJson(source);
        r[QStringLiteral("ok")] = true;
        return r;
    }, o);
}

void McpBridge::verbOpenNoter(QLocalSocket *client, int id) {
    if (!m_host.openNoter) {
        sendError(client, id,
                  QStringLiteral("open_noter not supported by host"));
        return;
    }
    if (!m_host.openNoter()) {
        sendError(client, id, QStringLiteral("could not open Noter"));
        return;
    }
    QJsonObject result;
    result[QStringLiteral("opened")] = true;
    sendResult(client, id, result);
}

// ── Phase 2 — Data-analyst + Charts ─────────────────────────────────────

void McpBridge::verbListConnections(QLocalSocket *client, int id) {
    QJsonArray out = m_host.listConnections ? m_host.listConnections()
                                            : QJsonArray();
    QJsonObject result;
    result[QStringLiteral("connections")] = out;
    sendResult(client, id, result);
}

// READ: SELECT-only query on a SAVED named connection (what run_sql cannot
// reach). The host lambda re-classifies + runs with allowMutation=false; the
// bridge re-caps rows/cells with the shared helper.
void McpBridge::verbRunQuery(QLocalSocket *client, int id,
                             const QJsonObject &args) {
    if (!m_host.runNamedQuery) {
        sendError(client, id,
                  QStringLiteral("run_query not supported by host"));
        return;
    }
    const QString name =
        args.value(QLatin1String("connection_name")).toString();
    if (name.trimmed().isEmpty()) {
        sendError(client, id, QStringLiteral("missing connection_name"));
        return;
    }
    const QString sql = args.value(QLatin1String("sql")).toString();
    if (sql.trimmed().isEmpty()) {
        sendError(client, id, QStringLiteral("missing sql"));
        return;
    }
    const int maxRows = qBound(
        1, args.value(QLatin1String("max_rows")).toInt(kMaxSqlRows),
        kMaxSqlRows);
    QString err;
    const QJsonObject r = m_host.runNamedQuery(name, sql, maxRows, &err);
    if (!err.isEmpty()) {
        sendError(client, id, err);
        return;
    }
    QJsonObject result;
    result[QStringLiteral("columns")] =
        r.value(QLatin1String("columns")).toArray();
    result[QStringLiteral("rows")] = r.value(QLatin1String("rows")).toArray();
    result[QStringLiteral("truncated")] =
        r.value(QLatin1String("truncated")).toBool(false);
    result[QStringLiteral("engine")] =
        r.value(QLatin1String("engine")).toString();
    capSqlResultRows(result);
    sendResult(client, id, result);
}

void McpBridge::verbListTables(QLocalSocket *client, int id,
                               const QJsonObject &args) {
    if (!m_host.listTables) {
        sendError(client, id,
                  QStringLiteral("list_tables not supported by host"));
        return;
    }
    const QString name =
        args.value(QLatin1String("connection_name")).toString();
    if (name.trimmed().isEmpty()) {
        sendError(client, id, QStringLiteral("missing connection_name"));
        return;
    }
    QString err;
    const QJsonArray tables = m_host.listTables(name, &err);
    if (!err.isEmpty()) {
        sendError(client, id, err);
        return;
    }
    QJsonObject result;
    result[QStringLiteral("tables")] = tables;
    sendResult(client, id, result);
}

void McpBridge::verbOpenDataAnalyst(QLocalSocket *client, int id) {
    if (!m_host.openDataAnalyst) {
        sendError(client, id,
                  QStringLiteral("open_data_analyst not supported by host"));
        return;
    }
    if (!m_host.openDataAnalyst()) {
        sendError(client, id, QStringLiteral("could not open Data Analyst"));
        return;
    }
    QJsonObject result;
    result[QStringLiteral("opened")] = true;
    sendResult(client, id, result);
}

// ACT (no card): insert an inline chart card into the Data transcript. The
// unset-field message IS the friendly Full/WebEngine gate (Lite leaves the
// field unset), so gating is answered immediately with no card.
void McpBridge::verbRenderChart(QLocalSocket *client, int id,
                                const QJsonObject &args) {
    if (!m_host.renderChart) {
        sendError(client, id,
                  QStringLiteral("charts require the Full edition (WebEngine)"));
        return;
    }
    const QJsonValue specV = args.value(QLatin1String("spec"));
    if (!specV.isObject() || specV.toObject().isEmpty()) {
        sendError(client, id, QStringLiteral("missing spec (object)"));
        return;
    }
    const QString title = args.value(QLatin1String("title")).toString();
    QString err;
    const QJsonObject r = m_host.renderChart(specV.toObject(), title, &err);
    if (!err.isEmpty()) {
        sendError(client, id, err);
        return;
    }
    QJsonObject result;
    result[QStringLiteral("chart_id")] = r.value(QLatin1String("chart_id"));
    result[QStringLiteral("rendered")] = r.value(QLatin1String("rendered"));
    sendResult(client, id, result);
}

// WRITE (card): run the read-only query, serialize to CSV/JSON, write to the
// requested path. Containment is the human approval card (which names the
// cleaned absolute destination) plus absolute-path + existing-parent checks —
// same posture as export_diagram. Fail fast on every doomed case BEFORE a card.
void McpBridge::verbExportQueryResults(QLocalSocket *client, int id,
                                       const QJsonObject &args) {
    if (!m_host.runNamedQuery) {
        sendError(client, id,
                  QStringLiteral("export_query_results not supported by host"));
        return;
    }
    const QString name =
        args.value(QLatin1String("connection_name")).toString();
    if (name.trimmed().isEmpty()) {
        sendError(client, id, QStringLiteral("missing connection_name"));
        return;
    }
    const QString sql = args.value(QLatin1String("sql")).toString();
    if (sql.trimmed().isEmpty()) {
        sendError(client, id, QStringLiteral("missing sql"));
        return;
    }
    QString format = args.value(QLatin1String("format")).toString().toLower();
    if (format.isEmpty()) format = QStringLiteral("csv");
    if (format != QLatin1String("csv") && format != QLatin1String("json")) {
        sendError(client, id,
                  QStringLiteral("unsupported format: %1 (csv|json)").arg(format));
        return;
    }
    const int maxRows = qBound(
        1, args.value(QLatin1String("max_rows")).toInt(kExportRowsDefault),
        kExportRowsMax);
    const QString path =
        QDir::cleanPath(
            QDir::fromNativeSeparators(args.value(QLatin1String("path")).toString()));
    if (path.isEmpty()) {
        sendError(client, id, QStringLiteral("missing path"));
        return;
    }
    const QFileInfo fi(path);
    if (!fi.isAbsolute()) {
        sendError(client, id, QStringLiteral("path must be absolute"));
        return;
    }
    if (!QFileInfo(fi.absolutePath()).isDir()) {
        sendError(client, id,
                  QStringLiteral("parent directory does not exist: %1")
                      .arg(QDir::toNativeSeparators(fi.absolutePath())));
        return;
    }
    // Fail fast: a mutation SQL or an unknown connection never queues a card.
    if (m_host.classifySqlReadOnly) {
        QString reason;
        if (!m_host.classifySqlReadOnly(sql, &reason)) {
            sendError(client, id, QStringLiteral("query rejected: %1").arg(reason));
            return;
        }
    }
    if (m_host.listConnections) {
        const QJsonArray conns = m_host.listConnections();
        bool found = false;
        for (const QJsonValue &c : conns) {
            if (c.toObject().value(QLatin1String("name")).toString() == name) {
                found = true;
                break;
            }
        }
        if (!found) {
            sendError(client, id,
                      QStringLiteral("no connection named: %1").arg(name));
            return;
        }
    }
    if (const QString deny = writeDestinationRefusal(path); !deny.isEmpty()) {
        sendError(client, id, deny);
        return;
    }
    const bool overwrites = QFileInfo(path).isFile();
    const QString shownPath = sanitizeForCard(QDir::toNativeSeparators(path));
    const QString shownDest = overwrites
        ? QStringLiteral("OVERWRITE existing file: ") + shownPath
        : shownPath;
    const QString desc =
        QStringLiteral("export query results from '%1' to %2")
            .arg(elideForCard(name, kApprovalDescChars), shownDest);
    CardOptions o;
    o.fullPreview = true;
    const QString preview = fullTextPreview(sql, &o.warning);
    enqueueApprovalEx(client, id, desc, preview, o,
                    [this, name, sql, maxRows, path, format](QString *execErr) {
        QString e;
        const QJsonObject r = m_host.runNamedQuery(name, sql, maxRows, &e);
        if (!e.isEmpty()) {
            *execErr = e;
            return QJsonObject();
        }
        const QByteArray bytes = (format == QLatin1String("csv"))
                                     ? sqlResultToCsv(r)
                                     : sqlResultToJson(r);
        // D13: write-temp-then-replace, never a truncate in place.
        QSaveFile f(path);
        if (!f.open(QIODevice::WriteOnly) || f.write(bytes) != bytes.size() ||
            !f.commit()) {
            *execErr = QStringLiteral("could not write: %1 (%2)")
                           .arg(QDir::toNativeSeparators(path),
                                f.errorString());
            return QJsonObject();
        }
        QJsonObject out;
        out[QStringLiteral("ok")] = true;
        out[QStringLiteral("path")] = QDir::toNativeSeparators(path);
        out[QStringLiteral("rows")] = r.value(QLatin1String("rows")).toArray().size();
        return out;
    });
}

// WRITE (card): off-screen render + async export → file. Full/WebEngine only;
// the unset-field message IS the friendly gate, so Lite fails fast pre-card.
void McpBridge::verbExportChart(QLocalSocket *client, int id,
                                const QJsonObject &args) {
    if (!m_host.exportChart) {
        sendError(client, id,
                  QStringLiteral("charts require the Full edition (WebEngine)"));
        return;
    }
    const QJsonValue specV = args.value(QLatin1String("spec"));
    if (!specV.isObject() || specV.toObject().isEmpty()) {
        sendError(client, id, QStringLiteral("missing spec (object)"));
        return;
    }
    QString format = args.value(QLatin1String("format")).toString().toLower();
    if (format.isEmpty()) format = QStringLiteral("png");
    if (format != QLatin1String("png") && format != QLatin1String("svg") &&
        format != QLatin1String("html") && format != QLatin1String("spec")) {
        sendError(client, id,
                  QStringLiteral("unsupported format: %1 (png|svg|html|spec)")
                      .arg(format));
        return;
    }
    const int scale = qBound(
        1, args.value(QLatin1String("scale")).toInt(kChartScaleDefault), 4);
    const QString path =
        QDir::cleanPath(
            QDir::fromNativeSeparators(args.value(QLatin1String("path")).toString()));
    if (path.isEmpty()) {
        sendError(client, id, QStringLiteral("missing path"));
        return;
    }
    const QFileInfo fi(path);
    if (!fi.isAbsolute()) {
        sendError(client, id, QStringLiteral("path must be absolute"));
        return;
    }
    if (!QFileInfo(fi.absolutePath()).isDir()) {
        sendError(client, id,
                  QStringLiteral("parent directory does not exist: %1")
                      .arg(QDir::toNativeSeparators(fi.absolutePath())));
        return;
    }
    if (const QString deny = writeDestinationRefusal(path); !deny.isEmpty()) {
        sendError(client, id, deny);
        return;
    }
    const bool overwrites = QFileInfo(path).isFile();
    const QString shownPath = sanitizeForCard(QDir::toNativeSeparators(path));
    const QString shownDest = overwrites
        ? QStringLiteral("OVERWRITE existing file: ") + shownPath
        : shownPath;
    const QString desc = QStringLiteral("export a chart as %1 to %2")
                             .arg(format.toUpper(), shownDest);
    const QJsonObject spec = specV.toObject();
    enqueueApproval(client, id, desc, shownDest,
                    [this, spec, path, format, scale](QString *execErr) {
        QString e;
        if (!m_host.exportChart(spec, path, format, scale, &e)) {
            *execErr = e.isEmpty() ? QStringLiteral("chart export failed") : e;
            return QJsonObject();
        }
        QJsonObject r;
        r[QStringLiteral("path")] = QDir::toNativeSeparators(path);
        return r;
    });
}

// ── v0.1.133 — editing, tabs, view and pure tools ──────────────────────
//
// The bridge validates and words the card; the host runs the SAME function
// the menu runs. A WRITE verb never executes before Approve, and the card
// names the tool, the tab, the operation and its scope.


bool McpBridge::textOrTab(const QJsonObject &args, const QString &prefix,
                          QString *text, int *tabIdx, QString *err) const {
    const QString tk = prefix + QStringLiteral("text");
    const QString idk = prefix + QStringLiteral("tab_id");
    const QString ixk = prefix + QStringLiteral("tab_index");
    if (tabIdx) *tabIdx = -1;
    const bool hasTab = args.contains(idk) || args.contains(ixk);
    if (args.contains(tk) && hasTab) {
        *err = QStringLiteral("provide %1 or a tab (%2 / %3), not both")
                   .arg(tk, idk, ixk);
        return false;
    }
    if (args.contains(tk)) {
        if (!args.value(tk).isString()) {
            *err = QStringLiteral("%1 must be a string").arg(tk);
            return false;
        }
        *text = args.value(tk).toString();
    } else if (hasTab) {
        QJsonObject sel;
        if (args.contains(idk)) sel[QStringLiteral("tab_id")] = args.value(idk);
        else sel[QStringLiteral("tab_index")] = args.value(ixk);
        const int idx = resolveTab(sel, "tab_index", false, err);
        if (idx < 0) return false;
        if (!tabIsEditable(idx)) {
            *err = nonEditableReason(idx);
            return false;
        }
        // Same door read_tab uses: a credential buffer never leaves.
        if (!tabTextFor(idx, text, err)) return false;
        if (tabIdx) *tabIdx = idx;
    } else {
        *err = QStringLiteral("provide %1 or a tab (%2 / %3)").arg(tk, idk, ixk);
        return false;
    }
    if (text->size() > kComputeMaxChars) {
        *err = QStringLiteral("text too large (%1 chars, cap %2)")
                   .arg(text->size())
                   .arg(kComputeMaxChars);
        return false;
    }
    return true;
}

// ACT: view / navigation on one tab. No card: nothing in the document changes.
void McpBridge::verbTabAction(QLocalSocket *client, int id,
                              const QJsonObject &args, const QString &verb) {
    if (!m_host.tabAction) {
        sendError(client, id, QStringLiteral("%1 not supported by host").arg(verb));
        return;
    }
    QString err;
    // activate_tab must name its tab; the rest default to the focused one.
    const int idx = verb == QLatin1String("activate_tab")
                        ? resolveTab(args, "tab_index", false, &err)
                        : resolveWriteTab(args, &err);
    if (idx < 0) {
        sendError(client, id, err);
        return;
    }
    const bool needsEditor = verb != QLatin1String("activate_tab") &&
                             verb != QLatin1String("set_preview");
    if (needsEditor && !tabIsEditable(idx)) {
        sendError(client, id, nonEditableReason(idx));
        return;
    }
    QJsonObject a = args;
    bool b = false;
    int n = 0;
    QString e;
    if (verb == QLatin1String("set_read_only") ||
        verb == QLatin1String("set_preview")) {
        if (!args.contains(QLatin1String("on"))) e = QStringLiteral("missing on");
        else e = optBool(args, "on", false, &b);
    } else if (verb == QLatin1String("bookmarks")) {
        QString act;
        e = optEnum(args, "action",
                    {QStringLiteral("list"), QStringLiteral("toggle"),
                     QStringLiteral("clear")},
                    QString(), &act);
        if (e.isEmpty()) {
            a[QStringLiteral("action")] = act;
            e = optInt(args, "line", 1, INT_MAX, 0,
                       act == QLatin1String("toggle"), &n);
        }
    } else if (verb == QLatin1String("fold")) {
        QString act;
        e = optEnum(args, "action",
                    {QStringLiteral("fold_all"), QStringLiteral("unfold_all")},
                    QString(), &act);
        a[QStringLiteral("action")] = act;
    } else if (verb == QLatin1String("compare_with_file")) {
        const QString path = QDir::cleanPath(QDir::fromNativeSeparators(
            args.value(QLatin1String("path")).toString()));
        const QFileInfo fi(path);
        if (path.isEmpty()) e = QStringLiteral("missing path");
        else if (!fi.isAbsolute()) e = QStringLiteral("path must be absolute");
        else if (!fi.isFile())
            e = QStringLiteral("not found or not a file: %1")
                    .arg(QDir::toNativeSeparators(path));
        else if (fi.size() > kMaxSearchFileBytes)
            e = QStringLiteral("file too large to compare (%1 bytes)").arg(fi.size());
        else e = credentialRefusal(path); // canonical target judged too
        // The tab side goes on screen next to it: same rule as read_tab.
        if (e.isEmpty()) {
            QString ignored;
            if (!tabTextFor(idx, &ignored, &e)) {}
        }
        a[QStringLiteral("path")] = fi.canonicalFilePath();
    }
    if (!e.isEmpty()) {
        sendError(client, id, e);
        return;
    }
    QJsonObject r = m_host.tabAction(idx, verb, a, &err);
    if (!err.isEmpty()) {
        sendError(client, id, err);
        return;
    }
    r[QStringLiteral("tab_index")] = idx;
    if (const qint64 tid = tabIdOf(idx); tid >= 0)
        r[QStringLiteral("tab_id")] = tid;
    sendResult(client, id, r);
}

// Closing an unmodified tab loses nothing: ACT, no card. A tab with unsaved
// changes is refused unless discard_unsaved is set, and then a card says so.
void McpBridge::verbCloseTab(QLocalSocket *client, int id,
                             const QJsonObject &args) {
    if (!m_host.tabAction || !m_host.editTab) {
        sendError(client, id, QStringLiteral("close_tab not supported by host"));
        return;
    }
    QString err;
    const int idx = resolveTab(args, "tab_index", false, &err);
    if (idx < 0) {
        sendError(client, id, err);
        return;
    }
    bool discard = false;
    if (const QString e = optBool(args, "discard_unsaved", false, &discard);
        !e.isEmpty()) {
        sendError(client, id, e);
        return;
    }
    const bool modified = m_host.tabModified && m_host.tabModified(idx);
    const qint64 tid = tabIdOf(idx);
    if (!modified) {
        QJsonObject r = m_host.tabAction(idx, QStringLiteral("close_tab"), args, &err);
        if (!err.isEmpty()) {
            sendError(client, id, err);
            return;
        }
        r[QStringLiteral("closed")] = true;
        r[QStringLiteral("tab_index")] = idx;
        if (tid >= 0) r[QStringLiteral("tab_id")] = tid;
        sendResult(client, id, r);
        return;
    }
    if (!discard) {
        sendError(client, id,
                  QStringLiteral("tab '%1' has unsaved changes. Save it first "
                                 "(save_tab), or pass discard_unsaved:true to "
                                 "ask the user to approve losing them.")
                      .arg(tabLabel(idx)));
        return;
    }
    const QString desc =
        QStringLiteral("close_tab: close '%1' and DISCARD its unsaved changes "
                       "(they will be lost)")
            .arg(elideForCard(tabLabel(idx), kApprovalDescChars));
    enqueueTabApproval(client, id, idx, desc, QString(),
                       [this, args](int live, QString *execErr) {
        QJsonObject r = m_host.editTab(live, QStringLiteral("close_tab"), args,
                                       execErr);
        r[QStringLiteral("closed")] = execErr->isEmpty();
        return r;
    });
}

// One card listing every file that would be written.
void McpBridge::verbSaveAll(QLocalSocket *client, int id,
                            const QJsonObject &args) {
    if (!m_host.saveTab) {
        sendError(client, id, QStringLiteral("save_all not supported by host"));
        return;
    }
    if (!args.isEmpty()) {
        sendError(client, id, QStringLiteral("save_all takes no arguments"));
        return;
    }
    struct Target { qint64 tid; int idx; QString title; QString path; };
    QVector<Target> targets;
    QJsonArray untitled, refused;
    const int n = m_host.tabCount ? m_host.tabCount() : 0;
    for (int i = 0; i < n; ++i) {
        if (!tabIsEditable(i) || !m_host.tabModified || !m_host.tabModified(i))
            continue;
        const QString path = m_host.tabPath ? m_host.tabPath(i) : QString();
        if (path.isEmpty()) {
            untitled.append(tabLabel(i));
            continue;
        }
        if (!writeDestinationRefusal(path).isEmpty()) {
            refused.append(QDir::toNativeSeparators(path));
            continue;
        }
        targets.append({tabIdOf(i), i, tabLabel(i), path});
    }
    QJsonObject base;
    base[QStringLiteral("skipped_untitled")] = untitled;
    base[QStringLiteral("skipped_protected")] = refused;
    if (targets.isEmpty()) {
        base[QStringLiteral("saved")] = QJsonArray();
        base[QStringLiteral("failed")] = QJsonArray();
        sendResult(client, id, base);
        return;
    }
    QStringList shown;
    for (const Target &t : targets)
        shown << sanitizeForCard(QDir::toNativeSeparators(t.path));
    QString desc = QStringLiteral("save_all: save %1 modified file(s) to disk")
                       .arg(targets.size());
    if (!untitled.isEmpty())
        desc += QStringLiteral(" (%1 untitled tab(s) skipped)").arg(untitled.size());
    enqueueApproval(client, id, desc,
                    elideForCard(shown.join(QLatin1Char('\n')), 1200),
                    [this, targets, base](QString *) {
        QJsonArray saved, failed;
        for (const Target &t : targets) {
            QString why;
            const int live = liveTabIndex(t.tid, t.idx, t.title, &why);
            const QString now = live >= 0 && m_host.tabPath ? m_host.tabPath(live)
                                                            : QString();
            if (live >= 0 && QFileInfo(now).absoluteFilePath() !=
                                 QFileInfo(t.path).absoluteFilePath())
                why = QStringLiteral("the tab now points at another file");
            if (why.isEmpty() && !m_host.saveTab(live))
                why = QStringLiteral("could not save");
            if (why.isEmpty()) {
                saved.append(QDir::toNativeSeparators(t.path));
            } else {
                QJsonObject f;
                f[QStringLiteral("path")] = QDir::toNativeSeparators(t.path);
                f[QStringLiteral("error")] = why;
                failed.append(f);
            }
        }
        QJsonObject r = base;
        r[QStringLiteral("saved")] = saved;
        r[QStringLiteral("failed")] = failed;
        return r;
    });
}

QString McpBridge::describeTabEdit(const QString &verb, const QJsonObject &args,
                                   int idx, QString *what,
                                   QString *preview) const {
    const QString label = elideForCard(tabLabel(idx), kApprovalDescChars);
    const QString path = m_host.tabPath ? m_host.tabPath(idx) : QString();
    const bool modified = m_host.tabModified && m_host.tabModified(idx);
    const bool hasSel = !m_host.hasSelection || m_host.hasSelection(idx);
    QString e;
    if (verb == QLatin1String("undo") || verb == QLatin1String("redo")) {
        int steps = 1;
        if (!(e = optInt(args, "steps", 1, 100, 1, false, &steps)).isEmpty()) return e;
        *what = QStringLiteral("%1 in '%2': %1 the last %3 edit(s)")
                    .arg(verb, label).arg(steps);
    } else if (verb == QLatin1String("transform_lines")) {
        QString op, scope;
        if (!(e = optEnum(args, "op", kLineOps(), QString(), &op)).isEmpty()) return e;
        if (!(e = optEnum(args, "scope",
                          {QStringLiteral("selection"), QStringLiteral("document")},
                          QStringLiteral("document"), &scope)).isEmpty())
            return e;
        if (scope == QLatin1String("selection") && !hasSel) return QStringLiteral("no selection");
        *what = QStringLiteral("transform_lines in '%1': %2 on the %3")
                    .arg(label, op,
                         scope == QLatin1String("selection")
                             ? QStringLiteral("selection")
                             : QStringLiteral("whole document"));
    } else if (verb == QLatin1String("convert_case")) {
        QString mode, scope;
        if (!(e = optEnum(args, "mode",
                          {QStringLiteral("upper"), QStringLiteral("lower"),
                           QStringLiteral("proper"), QStringLiteral("sentence"),
                           QStringLiteral("invert")},
                          QString(), &mode)).isEmpty())
            return e;
        if (!(e = optEnum(args, "scope",
                          {QStringLiteral("selection"), QStringLiteral("document")},
                          QStringLiteral("selection"), &scope)).isEmpty())
            return e;
        if (scope == QLatin1String("selection") && !hasSel) return QStringLiteral("no selection");
        *what = QStringLiteral("convert_case in '%1': %2 case on the %3")
                    .arg(label, mode,
                         scope == QLatin1String("selection")
                             ? QStringLiteral("selection")
                             : QStringLiteral("whole document"));
    } else if (verb == QLatin1String("comment_lines")) {
        int a = 0, b = 0;
        QString kind, action;
        if (!(e = optInt(args, "start_line", 1, INT_MAX, 0, true, &a)).isEmpty()) return e;
        if (!(e = optInt(args, "end_line", 1, INT_MAX, 0, true, &b)).isEmpty()) return e;
        if (b < a) return QStringLiteral("end_line must be >= start_line");
        if (!(e = optEnum(args, "kind", {QStringLiteral("line"), QStringLiteral("block")},
                          QStringLiteral("line"), &kind)).isEmpty())
            return e;
        if (!(e = optEnum(args, "action",
                          {QStringLiteral("toggle"), QStringLiteral("comment"),
                           QStringLiteral("uncomment")},
                          QStringLiteral("toggle"), &action)).isEmpty())
            return e;
        *what = QStringLiteral("comment_lines in '%1': %2 %3 comments on lines %4-%5")
                    .arg(label, action, kind).arg(a).arg(b);
    } else if (verb == QLatin1String("line_edit")) {
        int line = 0;
        QString op;
        if (!(e = optInt(args, "line", 1, INT_MAX, 0, true, &line)).isEmpty()) return e;
        if (!(e = optEnum(args, "op",
                          {QStringLiteral("duplicate"), QStringLiteral("delete"),
                           QStringLiteral("move_up"), QStringLiteral("move_down")},
                          QString(), &op)).isEmpty())
            return e;
        *what = QStringLiteral("line_edit in '%1': %2 line %3").arg(label, op).arg(line);
    } else if (verb == QLatin1String("set_eol")) {
        QString eol;
        bool convert = true;
        if (!(e = optEnum(args, "eol",
                          {QStringLiteral("crlf"), QStringLiteral("lf"), QStringLiteral("cr")},
                          QString(), &eol)).isEmpty())
            return e;
        if (!(e = optBool(args, "convert", true, &convert)).isEmpty()) return e;
        *what = QStringLiteral("set_eol in '%1': line endings to %2, %3")
                    .arg(label, eol.toUpper(),
                         convert ? QStringLiteral("converting every existing line ending")
                                 : QStringLiteral("for new lines only"));
    } else if (verb == QLatin1String("set_encoding")) {
        QString enc, mode;
        bool discard = false;
        if (!(e = optEnum(args, "encoding", kEncodings(), QString(), &enc)).isEmpty()) return e;
        if (!(e = optEnum(args, "mode",
                          {QStringLiteral("convert"), QStringLiteral("reinterpret")},
                          QStringLiteral("convert"), &mode)).isEmpty())
            return e;
        if (!(e = optBool(args, "discard_unsaved", false, &discard)).isEmpty()) return e;
        if (mode == QLatin1String("convert")) {
            *what = QStringLiteral("set_encoding in '%1': save as %2 from now on "
                                   "(the text is unchanged; the tab becomes modified)")
                        .arg(label, enc);
        } else {
            if (path.isEmpty())
                return QStringLiteral("reinterpret needs a file on disk; this tab "
                                      "has never been saved");
            if (modified && !discard)
                return QStringLiteral("tab '%1' has unsaved changes and reinterpret "
                                      "reloads it from disk. Pass "
                                      "discard_unsaved:true to ask the user to "
                                      "approve losing them.")
                    .arg(tabLabel(idx));
            *what = QStringLiteral("set_encoding in '%1': RELOAD from disk decoded "
                                   "as %2%3")
                        .arg(label, enc,
                             modified ? QStringLiteral(" (UNSAVED CHANGES WILL BE LOST)")
                                      : QString());
        }
    } else if (verb == QLatin1String("reload_tab")) {
        if (path.isEmpty()) return QStringLiteral("tab '%1' has no file on disk").arg(tabLabel(idx));
        *what = QStringLiteral("reload_tab: reload '%1' from disk (UNSAVED "
                               "CHANGES WILL BE LOST)").arg(label);
    } else if (verb == QLatin1String("rename_file")) {
        const QString nn = args.value(QLatin1String("new_name")).toString();
        if (path.isEmpty())
            return QStringLiteral("tab '%1' has no file on disk to rename").arg(tabLabel(idx));
        if (nn.trimmed().isEmpty()) return QStringLiteral("missing new_name");
        if (nn.contains(QLatin1Char('/')) || nn.contains(QLatin1Char('\\')) ||
            nn == QLatin1String(".") || nn == QLatin1String(".."))
            return QStringLiteral("new_name must be a plain file name in the same "
                                  "folder (no path separators)");
        const QFileInfo oldFi(path);
        const QString dest = oldFi.dir().filePath(nn);
        if (nn != oldFi.fileName() &&
            nn.compare(oldFi.fileName(), Qt::CaseInsensitive) != 0 &&
            QFileInfo::exists(dest))
            return QStringLiteral("a file named '%1' already exists in that folder").arg(nn);
        if (const QString deny = writeDestinationRefusal(dest); !deny.isEmpty())
            return deny;
        *what = QStringLiteral("rename_file: rename %1 to '%2' in the same folder%3")
                    .arg(sanitizeForCard(QDir::toNativeSeparators(path)),
                         sanitizeForCard(nn),
                         modified ? QStringLiteral(" (its unsaved changes are saved first)")
                                  : QString());
    } else {
        return QStringLiteral("unknown verb: %1").arg(verb);
    }
    Q_UNUSED(preview);
    return QString();
}

void McpBridge::verbTabEdit(QLocalSocket *client, int id,
                            const QJsonObject &args, const QString &verb) {
    if (!m_host.editTab) {
        sendError(client, id, QStringLiteral("%1 not supported by host").arg(verb));
        return;
    }
    QString err;
    const int idx = resolveWriteTab(args, &err);
    if (idx < 0) {
        sendError(client, id, err);
        return;
    }
    if (!tabIsEditable(idx)) {
        sendError(client, id, nonEditableReason(idx));
        return;
    }
    if (const QString deny = tabWriteRefusal(idx); !deny.isEmpty()) {
        sendError(client, id, deny);
        return;
    }
    QString what, preview;
    if (const QString e = describeTabEdit(verb, args, idx, &what, &preview);
        !e.isEmpty()) {
        sendError(client, id, e);
        return;
    }
    // Reloading a tab with nothing unsaved loses nothing: no card.
    if (verb == QLatin1String("reload_tab") &&
        !(m_host.tabModified && m_host.tabModified(idx))) {
        QJsonObject r = m_host.editTab(idx, verb, args, &err);
        if (!err.isEmpty()) {
            sendError(client, id, err);
            return;
        }
        r[QStringLiteral("tab_index")] = idx;
        if (const qint64 tid = tabIdOf(idx); tid >= 0)
            r[QStringLiteral("tab_id")] = tid;
        sendResult(client, id, r);
        return;
    }
    enqueueTabApproval(client, id, idx, what, preview,
                       [this, verb, args](int live, QString *execErr) {
        return m_host.editTab(live, verb, args, execErr);
    });
}

// ACT: app-wide view state, through the same QActions the View menu uses.
void McpBridge::verbAppAction(QLocalSocket *client, int id,
                              const QJsonObject &args, const QString &verb) {
    if (!m_host.appAction) {
        sendError(client, id, QStringLiteral("%1 not supported by host").arg(verb));
        return;
    }
    QJsonObject a;
    QString e;
    if (verb == QLatin1String("set_theme")) {
        QString theme;
        e = optEnum(args, "theme",
                    {QStringLiteral("System"), QStringLiteral("Light"),
                     QStringLiteral("Dark"), QStringLiteral("Monokai")},
                    QString(), &theme);
        a[QStringLiteral("theme")] = theme;
    } else {
        static const char *kBools[] = {"word_wrap", "show_whitespace", "show_eol",
                                       "indent_guides"};
        for (const char *k : kBools) {
            if (!args.contains(QLatin1String(k))) continue;
            bool b = false;
            if (!(e = optBool(args, k, false, &b)).isEmpty()) break;
            a[QLatin1String(k)] = b;
        }
        if (e.isEmpty() && args.contains(QLatin1String("zoom"))) {
            int z = 0;
            e = optInt(args, "zoom", 6, 48, 0, true, &z);
            a[QStringLiteral("zoom")] = z;
        }
        if (e.isEmpty() && a.isEmpty())
            e = QStringLiteral("set_view needs at least one of word_wrap, zoom, "
                               "show_whitespace, show_eol, indent_guides");
    }
    if (!e.isEmpty()) {
        sendError(client, id, e);
        return;
    }
    QString err;
    const QJsonObject r = m_host.appAction(verb, a, &err);
    if (!err.isEmpty()) {
        sendError(client, id, err);
        return;
    }
    sendResult(client, id, r);
}

// READ: pure computation. Tab text comes through textOrTab (deny-list), the
// result goes back to the client and nothing in the editor changes.
void McpBridge::verbCompute(QLocalSocket *client, int id,
                            const QJsonObject &args, const QString &verb) {
    if (!m_host.computeText) {
        sendError(client, id, QStringLiteral("%1 not supported by host").arg(verb));
        return;
    }
    QJsonObject a;
    QString e;
    QString text;
    int tab = -1;
    if (verb == QLatin1String("generate_password")) {
        QString mode;
        int length = 0, words = 0, count = 0;
        bool b = false;
        if ((e = optEnum(args, "mode",
                         {QStringLiteral("password"), QStringLiteral("passphrase")},
                         QStringLiteral("password"), &mode)).isEmpty() &&
            (e = optInt(args, "length", 4, 256, 20, false, &length)).isEmpty() &&
            (e = optInt(args, "words", 3, 24, 6, false, &words)).isEmpty() &&
            (e = optInt(args, "count", 1, 100, 1, false, &count)).isEmpty()) {
            a[QStringLiteral("mode")] = mode;
            a[QStringLiteral("length")] = length;
            a[QStringLiteral("words")] = words;
            a[QStringLiteral("count")] = count;
            for (const char *k : {"exclude_lookalikes", "require_each",
                                  "capitalise", "append_digits"}) {
                const bool def = QLatin1String(k) == QLatin1String("require_each");
                if (!(e = optBool(args, k, def, &b)).isEmpty()) break;
                a[QLatin1String(k)] = b;
            }
        }
        if (e.isEmpty() && args.contains(QLatin1String("separator"))) {
            const QString sep = args.value(QLatin1String("separator")).toString();
            if (!args.value(QLatin1String("separator")).isString() || sep.size() != 1)
                e = QStringLiteral("separator must be a single character");
            a[QStringLiteral("separator")] = sep;
        }
        if (e.isEmpty() && args.contains(QLatin1String("classes"))) {
            const QJsonValue cv = args.value(QLatin1String("classes"));
            static const QStringList known = {QStringLiteral("lower"),
                                              QStringLiteral("upper"),
                                              QStringLiteral("digits"),
                                              QStringLiteral("symbols")};
            if (!cv.isArray() || cv.toArray().isEmpty()) {
                e = QStringLiteral("classes must be a non-empty array of lower, "
                                   "upper, digits, symbols");
            } else {
                for (const QJsonValue &c : cv.toArray())
                    if (!known.contains(c.toString()))
                        e = QStringLiteral("unknown class: %1 (lower|upper|digits|"
                                           "symbols)")
                                .arg(c.toString());
                a[QStringLiteral("classes")] = cv;
            }
        }
    } else if (verb == QLatin1String("diff_text")) {
        for (const char *side : {"left", "right"}) {
            const QJsonValue v = args.value(QLatin1String(side));
            if (!v.isObject()) {
                e = QStringLiteral("%1 must be an object: {text} or {tab_id} or "
                                   "{tab_index}")
                        .arg(QLatin1String(side));
                break;
            }
            QString t;
            if (!textOrTab(v.toObject(), QString(), &t, nullptr, &e)) {
                e = QStringLiteral("%1: %2").arg(QLatin1String(side), e);
                break;
            }
            a[QLatin1String(side)] = t;
        }
    } else {
        if (textOrTab(args, QString(), &text, &tab, &e)) {
            a[QStringLiteral("text")] = text;
            if (verb == QLatin1String("hash_text")) {
                QString algo;
                e = optEnum(args, "algo",
                            {QStringLiteral("md5"), QStringLiteral("sha1"),
                             QStringLiteral("sha256"), QStringLiteral("sha512")},
                            QStringLiteral("sha256"), &algo);
                a[QStringLiteral("algo")] = algo;
            } else if (verb == QLatin1String("encode_text")) {
                QString op;
                e = optEnum(args, "op",
                            {QStringLiteral("base64_encode"),
                             QStringLiteral("base64_decode"),
                             QStringLiteral("url_encode"),
                             QStringLiteral("url_decode")},
                            QString(), &op);
                a[QStringLiteral("op")] = op;
            } else if (verb == QLatin1String("markdown_convert")) {
                QString op;
                e = optEnum(args, "op",
                            {QStringLiteral("table"), QStringLiteral("list"),
                             QStringLiteral("code_block"), QStringLiteral("bold"),
                             QStringLiteral("italic"), QStringLiteral("link"),
                             QStringLiteral("heading"),
                             QStringLiteral("html_to_markdown")},
                            QString(), &op);
                a[QStringLiteral("op")] = op;
            } else if (verb == QLatin1String("list_symbols")) {
                QString lang = args.value(QLatin1String("language")).toString();
                if (lang.isEmpty() && tab >= 0 && m_host.tabLanguage)
                    lang = m_host.tabLanguage(tab);
                a[QStringLiteral("language")] = lang;
            }
        }
    }
    if (!e.isEmpty()) {
        sendError(client, id, e);
        return;
    }
    QString err;
    QJsonObject r = m_host.computeText(verb, a, &err);
    if (!err.isEmpty()) {
        sendError(client, id, err);
        return;
    }
    if (tab >= 0) {
        r[QStringLiteral("tab_index")] = tab;
        if (const qint64 tid = tabIdOf(tab); tid >= 0)
            r[QStringLiteral("tab_id")] = tid;
    }
    sendResult(client, id, r);
}

void McpBridge::applyEditEx(QLocalSocket *client, int id,
                            const QJsonObject &args, QJsonObject opts,
                            bool allOpen) {
    if (!m_host.editTab) {
        sendError(client, id,
                  QStringLiteral("apply_edit options not supported by host"));
        return;
    }
    QVector<int> candidates;
    if (allOpen) {
        for (const char *k : {"tab_id", "tab_index", "title"})
            if (args.contains(QLatin1String(k))) {
                sendError(client, id,
                          QStringLiteral("scope all_open edits every open tab; "
                                         "do not name one"));
                return;
            }
        const int n = m_host.tabCount ? m_host.tabCount() : 0;
        for (int i = 0; i < n; ++i) {
            QString ignored;
            // Credential and write-protected buffers are skipped, not probed.
            if (tabIsEditable(i) && tabWriteRefusal(i).isEmpty() &&
                tabTextFor(i, &ignored, nullptr))
                candidates << i;
        }
    } else {
        QString err;
        const int idx = resolveWriteTab(args, &err);
        if (idx < 0) {
            sendError(client, id, err);
            return;
        }
        if (!tabIsEditable(idx)) {
            sendError(client, id, nonEditableReason(idx));
            return;
        }
        QString ignored;
        if (!tabTextFor(idx, &ignored, &err)) {
            sendError(client, id, err);
            return;
        }
        if (const QString deny = tabWriteRefusal(idx); !deny.isEmpty()) {
            sendError(client, id, deny);
            return;
        }
        candidates << idx;
    }
    struct Target { qint64 tid; int idx; QString title; int count; };
    QVector<Target> targets;
    int total = 0;
    QJsonObject dry = opts;
    dry[QStringLiteral("dry_run")] = true;
    for (int i : candidates) {
        QString err;
        const int c = m_host.editTab(i, QStringLiteral("apply_edit"), dry, &err)
                          .value(QLatin1String("count")).toInt(-1);
        if (!err.isEmpty()) {
            sendError(client, id, err); // e.g. an invalid pattern
            return;
        }
        if (c > 0) {
            targets.append({tabIdOf(i), i, tabLabel(i), c});
            total += c;
        }
    }
    if (targets.isEmpty()) {
        sendError(client, id, QStringLiteral("no match"));
        return;
    }
    const bool all = opts.value(QLatin1String("all")).toBool();
    QStringList flags;
    flags << (opts.value(QLatin1String("regex")).toBool() ? QStringLiteral("regex")
                                                         : QStringLiteral("literal"));
    flags << (opts.value(QLatin1String("match_case")).toBool()
                  ? QStringLiteral("case-sensitive")
                  : QStringLiteral("case-insensitive"));
    if (opts.value(QLatin1String("whole_word")).toBool())
        flags << QStringLiteral("whole word");
    const QString what =
        QStringLiteral("replace %1 %2 match of '%3' with '%4'")
            .arg(all ? QStringLiteral("every") : QStringLiteral("the first"),
                 flags.join(QStringLiteral(", ")),
                 elideForCard(opts.value(QLatin1String("find")).toString(),
                              kApprovalDescChars),
                 elideForCard(opts.value(QLatin1String("replace")).toString(),
                              kApprovalDescChars));
    QString desc;
    QStringList lines;
    if (allOpen) {
        desc = QStringLiteral("apply_edit in %1 open tab(s): %2 (%3 match(es) "
                              "in total)")
                   .arg(targets.size()).arg(what).arg(total);
        for (const Target &t : targets)
            lines << QStringLiteral("%1: %2").arg(sanitizeForCard(t.title)).arg(t.count);
    } else {
        desc = QStringLiteral("apply_edit in '%1': %2 (%3 match(es))")
                   .arg(elideForCard(targets.first().title, kApprovalDescChars),
                        what)
                   .arg(targets.first().count);
    }
    // R17: the whole pattern and replacement, scrollable (the line above
    // only names them).
    CardOptions o;
    o.fullPreview = true;
    QString findNote, replNote;
    const QString repl = opts.value(QLatin1String("replace")).toString();
    QString preview =
        QStringLiteral("Find:\n") +
        fullTextPreview(opts.value(QLatin1String("find")).toString(), &findNote) +
        QStringLiteral("\n\nReplace with:\n") +
        (repl.isEmpty() ? QStringLiteral("(nothing: each match is deleted)")
                        : fullTextPreview(repl, &replNote));
    o.warning = (findNote + QLatin1Char(' ') + replNote).trimmed();
    if (!lines.isEmpty())
        preview += QStringLiteral("\n\nMatches per tab:\n") +
                   elideForCard(lines.join(QLatin1Char('\n')), 4000);
    enqueueApprovalEx(client, id, desc, preview, o,
                    [this, targets, opts, dry, allOpen](QString *execErr) {
        // Every target must still exist and still hold exactly the matches
        // the card counted, or nothing is changed at all.
        QVector<int> live;
        for (const Target &t : targets) {
            const int li = liveTabIndex(t.tid, t.idx, t.title, execErr);
            if (li < 0) return QJsonObject();
            if (const QString deny = tabWriteRefusal(li); !deny.isEmpty()) {
                *execErr = deny;
                return QJsonObject();
            }
            const int c = m_host.editTab(li, QStringLiteral("apply_edit"), dry, execErr)
                              .value(QLatin1String("count")).toInt(-1);
            if (!execErr->isEmpty()) return QJsonObject();
            if (c != t.count) {
                *execErr = QStringLiteral(
                               "'%1' changed while the approval card was waiting "
                               "(%2 matches, the card said %3). Nothing was "
                               "changed; call apply_edit again.")
                               .arg(t.title).arg(c).arg(t.count);
                return QJsonObject();
            }
            live << li;
        }
        QJsonArray tabs;
        int done = 0;
        for (int k = 0; k < targets.size(); ++k) {
            QString e;
            const int c = m_host.editTab(live[k], QStringLiteral("apply_edit"), opts, &e)
                              .value(QLatin1String("count")).toInt(-1);
            QJsonObject o;
            o[QStringLiteral("tab_index")] = live[k];
            if (targets[k].tid >= 0) o[QStringLiteral("tab_id")] = targets[k].tid;
            o[QStringLiteral("title")] = targets[k].title;
            if (!e.isEmpty()) o[QStringLiteral("error")] = e;
            else {
                o[QStringLiteral("count")] = c;
                done += c;
            }
            tabs.append(o);
        }
        QJsonObject r;
        r[QStringLiteral("count")] = done;
        if (allOpen) r[QStringLiteral("tabs")] = tabs;
        else {
            r[QStringLiteral("tab_index")] = live.first();
            if (targets.first().tid >= 0)
                r[QStringLiteral("tab_id")] = targets.first().tid;
            if (tabs.first().toObject().contains(QLatin1String("error")))
                *execErr = tabs.first().toObject().value(QLatin1String("error")).toString();
        }
        return r;
    });
}

int McpBridge::liveTabIndex(qint64 tid, int idx, const QString &title,
                            QString *err) const {
    const int n = m_host.tabCount ? m_host.tabCount() : 0;
    if (tid >= 0) {
        for (int i = 0; i < n; ++i)
            if (tabIdOf(i) == tid) return i;
    } else if (idx >= 0 && idx < n && tabLabel(idx) == title) {
        return idx; // host without ids: same position AND same title only
    }
    if (err)
        *err = QStringLiteral(
                   "the tab '%1' named on the approval card was closed while "
                   "the card was waiting; nothing was changed")
                   .arg(title);
    return -1;
}

void McpBridge::enqueueTabApproval(QLocalSocket *client, int id, int idx,
                                   const QString &description,
                                   const QString &preview,
                                   std::function<QJsonObject(int, QString *)> run) {
    enqueueTabApproval(client, id, idx, description, preview, std::move(run), CardOptions());
}

void McpBridge::enqueueTabApproval(QLocalSocket *client, int id, int idx,
                                   const QString &description,
                                   const QString &preview,
                                   std::function<QJsonObject(int, QString *)> run,
                                   const CardOptions &options) {
    const qint64 tid = tabIdOf(idx);
    const QString title = tabLabel(idx);
    enqueueApprovalEx(client, id, description, preview, options,
                    [this, tid, idx, title, run](QString *execErr) {
        const int live = liveTabIndex(tid, idx, title, execErr);
        if (live < 0) return QJsonObject();
        // The file behind the tab can change during the wait too (Save As).
        if (const QString deny = tabWriteRefusal(live); !deny.isEmpty()) {
            *execErr = deny;
            return QJsonObject();
        }
        QJsonObject r = run(live, execErr);
        if (!execErr->isEmpty()) return QJsonObject();
        r[QStringLiteral("tab_index")] = live;
        if (tid >= 0) r[QStringLiteral("tab_id")] = tid;
        return r;
    });
}

void McpBridge::enqueueApproval(QLocalSocket *client, int id,
                                const QString &description,
                                const QString &preview,
                                std::function<QJsonObject(QString *)> execute) {
    enqueueApprovalEx(client, id, description, preview, CardOptions(),
                      std::move(execute));
}

void McpBridge::enqueueApprovalEx(QLocalSocket *client, int id,
                                  const QString &description,
                                  const QString &preview,
                                  const CardOptions &options,
                                  std::function<QJsonObject(QString *)> execute,
                                  AsyncExec executeAsync) {
    PendingWrite pw;
    pw.client = client;
    pw.id = id;
    pw.description = description;
    pw.preview = preview;
    pw.options = options;
    pw.execute = std::move(execute);
    pw.executeAsync = std::move(executeAsync);
    m_approvalQueue.append(pw);
    showNextApproval();
}

void McpBridge::showNextApproval() {
    if (m_approvalActive) return; // one card at a time, FIFO
    while (!m_approvalQueue.isEmpty()) {
        const PendingWrite &front = m_approvalQueue.constFirst();
        if (!front.client ||
            front.client->state() != QLocalSocket::ConnectedState) {
            m_approvalQueue.removeFirst(); // nobody left to answer
            continue;
        }
        QWidget *parent =
            m_host.approvalParent ? m_host.approvalParent() : nullptr;
        if (!parent) {
            // No surface to ask a human on: refuse. A write NEVER runs
            // unapproved — there is no headless fallback by design.
            const PendingWrite pw = m_approvalQueue.takeFirst();
            sendError(pw.client, pw.id,
                      QStringLiteral("approval unavailable"));
            continue;
        }
        m_approvalActive = true;
        m_approvalDeadlineMs =
            QDateTime::currentMSecsSinceEpoch() + m_approvalTimeoutMs;
        showApprovalCard(parent, front);
        m_approvalTimer->start(m_approvalTimeoutMs);
        m_countdownTimer->start();
        // D16: flash the taskbar entry / bounce the dock icon so a human in
        // another window notices. alert() never raises or takes focus.
        if (QWidget *win = parent->window())
            QApplication::alert(win, 0);
        return;
    }
}

// Non-modal banner overlaid on the host widget, styled after the AI panel's
// write-approval card (accent left border, muted preview, Approve/Deny).
// Theme colours from npPalette() so Light, Dark and Monokai all read well.
void McpBridge::showApprovalCard(QWidget *parent, const PendingWrite &pw) {
    dismissActiveCard();
    m_cardParent = parent;
    parent->installEventFilter(this);

    // v0.1.133: colours from the app's theme SSOT (npPalette). The widget
    // palette is not what Dark/Monokai repaint, so it left black header text
    // on a dark card.
    const NpPalette np = npPalette();
    const QColor baseC(np.cardBg);
    const QColor textC(np.text);
    const QColor accentC(np.accent);
    const QColor borderC(np.border);
    const QColor mutedC(np.textMuted);

    auto *card = new QFrame(parent);
    card->setObjectName(QStringLiteral("mcpApprovalCard"));
    card->setFocusPolicy(Qt::NoFocus);
    card->setStyleSheet(QStringLiteral(
        "#mcpApprovalCard { background:%1; border:1px solid %2; "
        "border-left:3px solid %3; border-radius:8px; }")
        .arg(baseC.name(), borderC.name(), accentC.name()));
    auto *lay = new QVBoxLayout(card);
    lay->setContentsMargins(10, 8, 10, 8);
    lay->setSpacing(6);

    auto *hdr = new QLabel(card);
    hdr->setObjectName(QStringLiteral("mcpApprovalDesc"));
    hdr->setTextFormat(Qt::PlainText);
    hdr->setWordWrap(true);
    // Defense in depth: every string on the card goes through the same
    // line-break + control-character rendering (idempotent on text the verb
    // already sanitised).
    hdr->setText(QStringLiteral("AI tool (MCP) requests: %1")
                     .arg(sanitizeMultiline(pw.description)));
    hdr->setStyleSheet(QStringLiteral(
        "color:%1; background:transparent; font-weight:600;")
        .arg(textC.name()));
    lay->addWidget(hdr);

    if (!pw.options.warning.isEmpty()) {
        auto *warn = new QLabel(card);
        warn->setObjectName(QStringLiteral("mcpApprovalWarning"));
        warn->setTextFormat(Qt::PlainText);
        warn->setWordWrap(true);
        warn->setText(sanitizeMultiline(pw.options.warning));
        warn->setStyleSheet(QStringLiteral(
            "color:%1; background:transparent; font-weight:600;")
            .arg(QColor(np.errorFg).name()));
        lay->addWidget(warn);
    }

    if (!pw.preview.isEmpty() && pw.options.fullPreview) {
        // Whole text, scrollable: nothing that could hide a second statement is cut.
        auto *full = new QPlainTextEdit(card);
        full->setObjectName(QStringLiteral("mcpApprovalPreviewFull"));
        full->setReadOnly(true);
        full->setFocusPolicy(Qt::NoFocus);
        full->setPlainText(sanitizeMultiline(pw.preview));
        full->setLineWrapMode(QPlainTextEdit::WidgetWidth);
        const int lines = qBound(2, int(pw.preview.count(QLatin1Char('\n'))) + 1, 10);
        full->setFixedHeight(full->fontMetrics().lineSpacing() * lines + 14);
        full->setStyleSheet(QStringLiteral(
            "QPlainTextEdit{color:%1; background:%2; border:1px solid %3; "
            "border-radius:4px; font-family:'JetBrains Mono','Cascadia Code',"
            "'Consolas',monospace; font-size:11px;}")
            .arg(textC.name(), QColor(np.inputBg).name(), borderC.name()));
        lay->addWidget(full);
    } else if (!pw.preview.isEmpty()) {
        auto *prev = new QLabel(card);
        prev->setObjectName(QStringLiteral("mcpApprovalPreview"));
        prev->setTextFormat(Qt::PlainText);
        prev->setWordWrap(true);
        prev->setText(sanitizeMultiline(pw.preview));
        prev->setStyleSheet(QStringLiteral(
            "color:%1; background:transparent; font-family:'JetBrains Mono',"
            "'Cascadia Code','Consolas',monospace; font-size:11px;")
            .arg(mutedC.name()));
        lay->addWidget(prev);
    }

    auto *btnRow = new QHBoxLayout;
    btnRow->setSpacing(6);
    auto *approveBtn = new QPushButton(tr("Approve"), card);
    approveBtn->setObjectName(QStringLiteral("mcpApproveBtn"));
    auto *denyBtn = new QPushButton(tr("Deny"), card);
    denyBtn->setObjectName(QStringLiteral("mcpDenyBtn"));
    // The card must never steal keyboard focus from the editor: mouse-only.
    approveBtn->setFocusPolicy(Qt::NoFocus);
    denyBtn->setFocusPolicy(Qt::NoFocus);
    approveBtn->setStyleSheet(QStringLiteral(
        "QPushButton{background:%1;color:%2;border:1px solid %1;"
        "border-radius:6px;padding:4px 10px;font-weight:600;}")
        .arg(accentC.name(), QColor(np.selectionFg).name()));
    denyBtn->setStyleSheet(QStringLiteral(
        "QPushButton{background:%1;color:%2;border:1px solid %3;"
        "border-radius:6px;padding:4px 10px;}")
        .arg(QColor(np.btnBg).name(), QColor(np.btnFg).name(),
             QColor(np.btnBorder).name()));
    btnRow->addWidget(approveBtn);
    btnRow->addStretch(1);
    // D16: the time left before the card auto-denies, refreshed each second.
    auto *countdown = new QLabel(card);
    countdown->setObjectName(QStringLiteral("mcpApprovalCountdown"));
    countdown->setTextFormat(Qt::PlainText);
    countdown->setStyleSheet(QStringLiteral(
        "color:%1; background:transparent; font-size:11px;")
        .arg(mutedC.name()));
    btnRow->addWidget(countdown);
    m_cardCountdown = countdown;
    btnRow->addSpacing(8);
    btnRow->addWidget(denyBtn);
    lay->addLayout(btnRow);

    connect(approveBtn, &QPushButton::clicked, this,
            [this]() { resolveActiveApproval(true, QString()); });
    connect(denyBtn, &QPushButton::clicked, this, [this]() {
        resolveActiveApproval(false, QStringLiteral("denied by user"));
    });

    m_card = card;
    updateCountdown();
    positionCard();
    card->show();
    card->raise();
}

void McpBridge::updateCountdown() {
    if (!m_cardCountdown) return;
    const qint64 leftMs =
        qMax<qint64>(0, m_approvalDeadlineMs -
                            QDateTime::currentMSecsSinceEpoch());
    const qint64 secs = (leftMs + 999) / 1000;
    m_cardCountdown->setText(
        tr("Auto-deny in %1:%2")
            .arg(secs / 60)
            .arg(secs % 60, 2, 10, QLatin1Char('0')));
}

void McpBridge::resolveActiveApproval(bool approved,
                                      const QString &denyMessage) {
    if (!m_approvalActive || m_approvalQueue.isEmpty()) return;
    m_approvalTimer->stop();
    const PendingWrite pw = m_approvalQueue.takeFirst();
    m_approvalActive = false;
    dismissActiveCard();
    if (pw.client && pw.client->state() == QLocalSocket::ConnectedState) {
        if (approved && pw.executeAsync) {
            // Answer when the work finishes; the next card need not wait.
            QPointer<QLocalSocket> c = pw.client;
            QPointer<McpBridge> self(this);
            const int rid = pw.id;
            pw.executeAsync([self, c, rid](const QJsonObject &r, const QString &e) {
                if (!self || !c || c->state() != QLocalSocket::ConnectedState) return;
                if (e.isEmpty()) self->sendResult(c, rid, r);
                else self->sendError(c, rid, e);
            });
        } else if (approved) {
            QString err;
            const QJsonObject result =
                pw.execute ? pw.execute(&err) : QJsonObject();
            if (err.isEmpty()) sendResult(pw.client, pw.id, result);
            else sendError(pw.client, pw.id, err);
        } else {
            sendError(pw.client, pw.id, denyMessage);
        }
    }
    showNextApproval();
}

void McpBridge::dismissActiveCard() {
    if (m_countdownTimer) m_countdownTimer->stop();
    m_cardCountdown.clear();
    if (m_cardParent) m_cardParent->removeEventFilter(this);
    m_cardParent.clear();
    if (m_card) {
        m_card->hide();
        // deleteLater, never delete: we may be inside the card's own
        // button-clicked signal right now.
        m_card->deleteLater();
        m_card.clear();
    }
}

void McpBridge::dropClientApprovals(QLocalSocket *client) {
    bool droppedActive = false;
    for (int i = m_approvalQueue.size() - 1; i >= 0; --i) {
        QLocalSocket *c = m_approvalQueue.at(i).client.data();
        if (c == client || !c) {
            if (i == 0 && m_approvalActive) droppedActive = true;
            m_approvalQueue.removeAt(i);
        }
    }
    if (droppedActive) {
        m_approvalTimer->stop();
        m_approvalActive = false;
        dismissActiveCard();
        showNextApproval();
    }
}

void McpBridge::positionCard() {
    if (!m_card || !m_cardParent) return;
    const int margin = 12;
    const int w = qMax(280, m_cardParent->width() - 2 * margin);
    m_card->setFixedWidth(w);
    m_card->adjustSize();
    m_card->move(qMax(0, (m_cardParent->width() - w) / 2), 8);
    m_card->raise();
}

bool McpBridge::eventFilter(QObject *watched, QEvent *event) {
    if (watched == m_cardParent && event->type() == QEvent::Resize)
        positionCard();
    return QObject::eventFilter(watched, event);
}

// ═══════════════════════════════════════════════════════════════════════
// v0.1.133 Phase 3: git writes, Noter management, settings, data, REST.
// Every verb validates here, asks the host for the facts its card needs,
// and runs the app's own code only after Approve (writes).
// ═══════════════════════════════════════════════════════════════════════

void McpBridge::sendOp(QLocalSocket *client, int id, const QJsonObject &r,
                       const QString &err) {
    if (!err.isEmpty()) sendError(client, id, err);
    else sendResult(client, id, r);
}

QString McpBridge::gitRelPath(const QString &repoRoot, const QString &path,
                              QString *err) {
    const QString p = QDir::fromNativeSeparators(path);
    auto fail = [&](const QString &m) {
        if (err) *err = m;
        return QString();
    };
    if (p.isEmpty() || p.contains(QChar(0)))
        return fail(QStringLiteral("empty path"));
    if (p.startsWith(QLatin1Char('-')))
        return fail(QStringLiteral("refusing path '%1': a path may not start with '-'")
                        .arg(sanitizeForCard(p)));
    const QString root = QDir::cleanPath(QDir::fromNativeSeparators(repoRoot));
    QString rel;
    if (QDir::isAbsolutePath(p)) {
        const QString cp = QDir::cleanPath(p);
        if (cp.startsWith(root + QLatin1Char('/'))) {
            rel = cp.mid(root.size() + 1);
        } else {
            // A symlinked spelling of the repo (macOS /var vs /private/var).
            const QString rootCanon = QFileInfo(root).canonicalFilePath();
            const QString parentCanon =
                QFileInfo(QFileInfo(cp).absolutePath()).canonicalFilePath();
            const QString full = parentCanon + QLatin1Char('/') + QFileInfo(cp).fileName();
            if (rootCanon.isEmpty() || parentCanon.isEmpty() ||
                !full.startsWith(rootCanon + QLatin1Char('/')))
                return fail(QStringLiteral("path is outside the workspace repository: %1")
                                .arg(QDir::toNativeSeparators(p)));
            rel = full.mid(rootCanon.size() + 1);
        }
    } else {
        rel = QDir::cleanPath(p);
    }
    if (rel.isEmpty() || rel == QLatin1String(".") || rel == QLatin1String("..") ||
        rel.startsWith(QLatin1String("../")))
        return fail(QStringLiteral("path is outside the workspace repository: %1")
                        .arg(QDir::toNativeSeparators(p)));
    if (rel.startsWith(QLatin1Char('-')))
        return fail(QStringLiteral("refusing path '%1': a path may not start with '-'")
                        .arg(sanitizeForCard(rel)));
    if (rel == QLatin1String(".git") || rel.startsWith(QLatin1String(".git/")))
        return fail(QStringLiteral("refusing a path inside .git"));
    const QString abs = root + QLatin1Char('/') + rel;
    // Same deny-list as git_diff's redaction (isSecretRepoPath).
    if (PathDenylist::isSecretPath(QLatin1Char('/') + rel) || PathDenylist::isSecretPath(abs))
        return fail(QStringLiteral(
                        "refusing %1: it matches the credential deny-list (SSH/GPG/AWS "
                        "keys, .env, keystores, Terraform state)")
                        .arg(sanitizeForCard(rel)));
    // A symlink inside the repo must not lead out of it or onto a secret.
    const QFileInfo fi(abs);
    if (fi.exists() || fi.isSymLink()) {
        const QString real = fi.canonicalFilePath();
        const QString rootCanon = QFileInfo(root).canonicalFilePath();
        if (!real.isEmpty() &&
            ((!rootCanon.isEmpty() && real != rootCanon &&
              !real.startsWith(rootCanon + QLatin1Char('/'))) ||
             PathDenylist::isSecretPath(real)))
            return fail(QStringLiteral("refusing %1: it leads outside the repository or "
                                       "onto a credential file")
                            .arg(sanitizeForCard(rel)));
    }
    return rel;
}

void McpBridge::verbGitWrite(QLocalSocket *client, int id, const QJsonObject &args,
                             const QString &verb) {
    if (!m_host.gitOp) {
        sendError(client, id, QStringLiteral("%1 not supported by host").arg(verb));
        return;
    }
    QString err;
    const QJsonObject ctx = m_host.gitOp(QStringLiteral("context"), QJsonObject(), &err);
    if (!err.isEmpty()) {
        sendError(client, id, err);
        return;
    }
    const QString root = ctx.value(QLatin1String("repo_root")).toString();
    const QString branch = ctx.value(QLatin1String("branch")).toString();
    const QString shownRepo = sanitizeForCard(QDir::toNativeSeparators(root));
    const QStringList staged = jsonStrings(ctx.value(QLatin1String("staged")));
    const QStringList modified = jsonStrings(ctx.value(QLatin1String("modified")));
    const QStringList untracked = jsonStrings(ctx.value(QLatin1String("untracked")));
    const QStringList conflicts = jsonStrings(ctx.value(QLatin1String("conflicts")));
    auto run = [this, verb](const QJsonObject &a) {
        return [this, verb, a](QString *e) { return m_host.gitOp(verb, a, e); };
    };

    // ── READ ──
    if (verb == QLatin1String("list_hunks") || verb == QLatin1String("list_conflicts")) {
        QJsonObject a;
        if (args.contains(QLatin1String("path")) || verb == QLatin1String("list_hunks")) {
            QString p;
            if (!(err = reqString(args, "path", &p)).isEmpty()) {
                sendError(client, id, err);
                return;
            }
            const QString rel = gitRelPath(root, p, &err);
            if (rel.isEmpty()) {
                sendError(client, id, err);
                return;
            }
            a[QStringLiteral("path")] = rel;
        }
        const QJsonObject r = m_host.gitOp(verb, a, &err);
        sendOp(client, id, r, err);
        return;
    }

    // ── WRITE: paths ──
    if (verb == QLatin1String("git_stage") || verb == QLatin1String("git_unstage") ||
        verb == QLatin1String("git_discard")) {
        QStringList in, rels;
        if (!(err = pathsArg(args, &in)).isEmpty()) {
            sendError(client, id, err);
            return;
        }
        for (const QString &p : in) {
            const QString rel = gitRelPath(root, p, &err);
            if (rel.isEmpty()) {
                sendError(client, id, err);
                return;
            }
            if (verb == QLatin1String("git_stage") && !modified.contains(rel) &&
                !untracked.contains(rel) && !conflicts.contains(rel)) {
                sendError(client, id, QStringLiteral("nothing to stage for %1 (no changes "
                                                     "in the working tree)").arg(rel));
                return;
            }
            if (verb == QLatin1String("git_unstage") && !staged.contains(rel)) {
                sendError(client, id, QStringLiteral("%1 is not staged").arg(rel));
                return;
            }
            if (verb == QLatin1String("git_discard")) {
                if (untracked.contains(rel)) {
                    sendError(client, id, QStringLiteral(
                        "%1 is untracked: git_discard never deletes untracked files")
                                              .arg(rel));
                    return;
                }
                if (!modified.contains(rel)) {
                    sendError(client, id, QStringLiteral(
                        "%1 has no uncommitted working-tree changes to discard").arg(rel));
                    return;
                }
            }
            if (!rels.contains(rel)) rels << rel;
        }
        QJsonObject a{{QStringLiteral("paths"), QJsonArray::fromStringList(rels)}};
        CardOptions o;
        o.fullPreview = true;
        QString desc;
        if (verb == QLatin1String("git_stage"))
            desc = QStringLiteral("git add in %1: stage %2 file(s)").arg(shownRepo).arg(rels.size());
        else if (verb == QLatin1String("git_unstage"))
            desc = QStringLiteral("git reset in %1: unstage %2 file(s) (the working copy is "
                                  "kept)").arg(shownRepo).arg(rels.size());
        else {
            desc = QStringLiteral("git checkout -- in %1: discard the uncommitted changes in "
                                  "%2 file(s)").arg(shownRepo).arg(rels.size());
            o.warning = QStringLiteral(
                "Data loss: the working-tree changes in these files are thrown away and "
                "cannot be recovered. Each file goes back to its staged (or last committed) "
                "content; staged changes are kept and untracked files are never touched.");
        }
        enqueueApprovalEx(client, id, desc, bulletList(rels), o, run(a));
        return;
    }

    if (verb == QLatin1String("git_commit")) {
        QString msg;
        if (!(err = reqString(args, "message", &msg)).isEmpty()) {
            sendError(client, id, err);
            return;
        }
        if (msg.size() > kCommitMessageMax) {
            sendError(client, id, QStringLiteral("message too long (max %1 characters)")
                                      .arg(kCommitMessageMax));
            return;
        }
        if (staged.isEmpty()) {
            sendError(client, id, QStringLiteral(
                "nothing is staged: stage files with git_stage first (git_commit never "
                "commits unstaged changes)"));
            return;
        }
        CardOptions o;
        o.fullPreview = true;
        QStringList secret;
        for (const QString &s : staged)
            if (PathDenylist::isSecretPath(QLatin1Char('/') + s)) secret << s;
        if (!secret.isEmpty())
            o.warning = QStringLiteral("Staged files on the credential deny-list: %1")
                            .arg(secret.join(QStringLiteral(", ")));
        const QString desc =
            QStringLiteral("git commit on branch '%1' in %2: %3 staged file(s); hooks run")
                .arg(sanitizeForCard(branch), shownRepo).arg(staged.size());
        const QString preview = QStringLiteral("Message:\n%1\n\nStaged files:\n%2")
                                    .arg(sanitizeMultiline(msg), bulletList(staged));
        const QJsonObject a{{QStringLiteral("message"), msg},
                            {QStringLiteral("expect_staged"), QJsonArray::fromStringList(staged)}};
        // Hooks can take a while: the commit answers asynchronously when it can.
        if (m_host.gitAsync)
            enqueueApprovalEx(client, id, desc, preview, o, nullptr,
                              [this, a](McpEditorHost::Done done) {
                                  m_host.gitAsync(QStringLiteral("git_commit"), a, done);
                              });
        else
            enqueueApprovalEx(client, id, desc, preview, o, run(a));
        return;
    }

    if (verb == QLatin1String("git_branch_create") || verb == QLatin1String("git_checkout")) {
        QString name;
        if (!(err = reqString(args, "name", &name)).isEmpty()) {
            sendError(client, id, err);
            return;
        }
        if (!validBranchName(name)) {
            sendError(client, id, QStringLiteral("invalid branch name: %1").arg(sanitizeForCard(name)));
            return;
        }
        const QStringList branches = jsonStrings(ctx.value(QLatin1String("branches")));
        bool sw = false;
        if (verb == QLatin1String("git_branch_create")) {
            if (!(err = optBool(args, "switch", false, &sw)).isEmpty()) {
                sendError(client, id, err);
                return;
            }
            if (branches.contains(name)) {
                sendError(client, id, QStringLiteral("branch '%1' already exists").arg(name));
                return;
            }
            const QString desc =
                QStringLiteral("create branch '%1' at the current commit of '%2' in %3%4")
                    .arg(name, sanitizeForCard(branch), shownRepo,
                         sw ? QStringLiteral(" and switch to it") : QString());
            enqueueApprovalEx(client, id, desc, QString(), CardOptions(),
                              run(QJsonObject{{QStringLiteral("name"), name},
                                              {QStringLiteral("switch"), sw}}));
            return;
        }
        if (!branches.contains(name)) {
            sendError(client, id, QStringLiteral("no local branch named '%1'").arg(name));
            return;
        }
        if (name == branch) {
            sendError(client, id, QStringLiteral("already on branch '%1'").arg(name));
            return;
        }
        const QString desc =
            QStringLiteral("switch branch in %1: '%2' -> '%3' (git refuses if it would "
                           "overwrite local changes; open files reload from disk)")
                .arg(shownRepo, sanitizeForCard(branch), name);
        enqueueApprovalEx(client, id, desc, QString(), CardOptions(),
                          run(QJsonObject{{QStringLiteral("name"), name}}));
        return;
    }

    if (verb == QLatin1String("git_stash")) {
        QString action;
        if (!(err = optEnum(args, "action",
                            {QStringLiteral("push"), QStringLiteral("pop"),
                             QStringLiteral("apply")},
                            QString(), &action)).isEmpty()) {
            sendError(client, id, err);
            return;
        }
        const QString msg = args.value(QLatin1String("message")).toString();
        if (args.contains(QLatin1String("message")) &&
            (action != QLatin1String("push") || !args.value(QLatin1String("message")).isString())) {
            sendError(client, id, QStringLiteral("message is only for action push"));
            return;
        }
        const int stashes = ctx.value(QLatin1String("stash_count")).toInt();
        QString desc;
        if (action == QLatin1String("push")) {
            const int n = staged.size() + modified.size() + untracked.size();
            if (n == 0) {
                sendError(client, id, QStringLiteral("nothing to stash: the working tree is clean"));
                return;
            }
            desc = QStringLiteral("git stash push -u in %1: set aside the changes in %2 "
                                  "file(s), untracked ones included%3")
                       .arg(shownRepo).arg(n)
                       .arg(msg.isEmpty() ? QString()
                                          : QStringLiteral(" ('%1')").arg(
                                                elideForCard(sanitizeForCard(msg), kApprovalDescChars)));
        } else {
            if (stashes == 0) {
                sendError(client, id, QStringLiteral("there is no stash to %1").arg(action));
                return;
            }
            desc = QStringLiteral("git stash %1 in %2: re-apply the latest stash "
                                  "(stash@{0})%3")
                       .arg(action, shownRepo,
                            action == QLatin1String("pop")
                                ? QStringLiteral(" and drop it if it applies cleanly")
                                : QStringLiteral(" and keep it"));
        }
        QJsonObject a{{QStringLiteral("action"), action}};
        if (!msg.isEmpty()) a[QStringLiteral("message")] = msg;
        CardOptions so;
        so.fullPreview = true;
        const QString preview =
            msg.isEmpty() ? QString() : QStringLiteral("Message:\n") + fullTextPreview(msg, &so.warning);
        enqueueApprovalEx(client, id, desc, preview, so, run(a));
        return;
    }

    if (verb == QLatin1String("git_fetch") || verb == QLatin1String("git_pull") ||
        verb == QLatin1String("git_push")) {
        if (!args.isEmpty()) {
            sendError(client, id, QStringLiteral("%1 takes no arguments: it always uses the "
                                                 "current branch and its upstream").arg(verb));
            return;
        }
        if (!m_host.gitAsync) {
            sendError(client, id, QStringLiteral("%1 not supported by host").arg(verb));
            return;
        }
        const QString upstream = ctx.value(QLatin1String("upstream")).toString();
        const QString remote = ctx.value(QLatin1String("upstream_remote")).toString();
        if (branch.isEmpty() || branch.startsWith(QLatin1Char('('))) {
            sendError(client, id, QStringLiteral("not on a branch (detached HEAD)"));
            return;
        }
        if (upstream.isEmpty() || remote.isEmpty()) {
            sendError(client, id, QStringLiteral(
                "branch '%1' has no upstream configured; set one in a terminal "
                "(git push -u) first").arg(branch));
            return;
        }
        QString desc;
        if (verb == QLatin1String("git_fetch"))
            desc = QStringLiteral("git fetch from remote '%1' in %2 (network)")
                       .arg(sanitizeForCard(remote), shownRepo);
        else if (verb == QLatin1String("git_pull"))
            desc = QStringLiteral("git pull --ff-only: fast-forward '%1' to '%2' in %3 "
                                  "(network; refused if the branches diverged)")
                       .arg(sanitizeForCard(branch), sanitizeForCard(upstream), shownRepo);
        else
            desc = QStringLiteral("git push: send '%1' to '%2' in %3 (network; never forced)")
                       .arg(sanitizeForCard(branch), sanitizeForCard(upstream), shownRepo);
        const QJsonObject a{{QStringLiteral("branch"), branch},
                            {QStringLiteral("upstream"), upstream}};
        enqueueApprovalEx(client, id, desc, QString(), CardOptions(), nullptr,
                          [this, verb, a](McpEditorHost::Done done) {
                              m_host.gitAsync(verb, a, done);
                          });
        return;
    }

    if (verb == QLatin1String("git_hunk")) {
        QString p, action;
        int idx = 0;
        if (!(err = reqString(args, "path", &p)).isEmpty() ||
            !(err = optInt(args, "hunk_index", 0, 100000, 0, true, &idx)).isEmpty() ||
            !(err = optEnum(args, "action",
                            {QStringLiteral("stage"), QStringLiteral("revert")}, QString(),
                            &action)).isEmpty()) {
            sendError(client, id, err);
            return;
        }
        const QString rel = gitRelPath(root, p, &err);
        if (rel.isEmpty()) {
            sendError(client, id, err);
            return;
        }
        const QJsonObject info =
            m_host.gitOp(QStringLiteral("list_hunks"), {{QStringLiteral("path"), rel}}, &err);
        if (!err.isEmpty()) {
            sendError(client, id, err);
            return;
        }
        const QJsonArray hunks = info.value(QLatin1String("hunks")).toArray();
        if (idx >= hunks.size()) {
            sendError(client, id, QStringLiteral("%1 has %2 hunk(s); hunk_index %3 does not "
                                                 "exist (list_hunks shows them)")
                                      .arg(rel).arg(hunks.size()).arg(idx));
            return;
        }
        const QJsonObject h = hunks.at(idx).toObject();
        CardOptions o;
        o.fullPreview = true;
        QString desc;
        if (action == QLatin1String("stage")) {
            desc = QStringLiteral("stage hunk %1 of %2 (%3) into the git index")
                       .arg(idx).arg(sanitizeForCard(rel),
                                     h.value(QLatin1String("header")).toString());
        } else {
            desc = QStringLiteral("revert hunk %1 of %2 (%3) in the working copy")
                       .arg(idx).arg(sanitizeForCard(rel),
                                     h.value(QLatin1String("header")).toString());
            o.warning = QStringLiteral("Data loss: the lines this hunk added or changed are "
                                       "removed from the file and cannot be recovered.");
        }
        enqueueApprovalEx(client, id, desc,
                          sanitizeMultiline(h.value(QLatin1String("diff")).toString()), o,
                          run(QJsonObject{{QStringLiteral("path"), rel},
                                          {QStringLiteral("hunk_index"), idx},
                                          {QStringLiteral("action"), action},
                                          {QStringLiteral("expect_header"),
                                           h.value(QLatin1String("header"))}}));
        return;
    }

    if (verb == QLatin1String("resolve_conflict")) {
        QString p, take;
        int region = 0;
        if (!(err = reqString(args, "path", &p)).isEmpty() ||
            !(err = optInt(args, "region", 0, 100000, 0, true, &region)).isEmpty() ||
            !(err = optEnum(args, "take",
                            {QStringLiteral("ours"), QStringLiteral("theirs"),
                             QStringLiteral("both")},
                            QString(), &take)).isEmpty()) {
            sendError(client, id, err);
            return;
        }
        const QString rel = gitRelPath(root, p, &err);
        if (rel.isEmpty()) {
            sendError(client, id, err);
            return;
        }
        if (const QString deny = writeDestinationRefusal(root + QLatin1Char('/') + rel);
            !deny.isEmpty()) {
            sendError(client, id, deny);
            return;
        }
        const QJsonObject info = m_host.gitOp(QStringLiteral("list_conflicts"),
                                              {{QStringLiteral("path"), rel}}, &err);
        if (!err.isEmpty()) {
            sendError(client, id, err);
            return;
        }
        const QJsonArray regions = info.value(QLatin1String("regions")).toArray();
        if (region >= regions.size()) {
            sendError(client, id, QStringLiteral("%1 has %2 conflict region(s); region %3 "
                                                 "does not exist (list_conflicts shows them)")
                                      .arg(rel).arg(regions.size()).arg(region));
            return;
        }
        const QJsonObject rg = regions.at(region).toObject();
        QString repl;
        if (take != QLatin1String("theirs")) repl += rg.value(QLatin1String("ours")).toString();
        if (take != QLatin1String("ours")) repl += rg.value(QLatin1String("theirs")).toString();
        CardOptions o;
        o.fullPreview = true;
        const QString desc =
            QStringLiteral("resolve conflict %1 in %2 (lines %3-%4) taking %5, then save the "
                           "file")
                .arg(region).arg(sanitizeForCard(rel))
                .arg(rg.value(QLatin1String("start_line")).toInt())
                .arg(rg.value(QLatin1String("end_line")).toInt())
                .arg(take == QLatin1String("both") ? QStringLiteral("both sides (ours, then theirs)")
                                                   : take);
        enqueueApprovalEx(client, id, desc,
                          repl.isEmpty() ? QStringLiteral("(the region becomes empty)")
                                         : sanitizeMultiline(repl),
                          o,
                          run(QJsonObject{{QStringLiteral("path"), rel},
                                          {QStringLiteral("region"), region},
                                          {QStringLiteral("take"), take},
                                          {QStringLiteral("expect_start"),
                                           rg.value(QLatin1String("start_line"))}}));
        return;
    }
    sendError(client, id, QStringLiteral("unknown verb: %1").arg(verb));
}

void McpBridge::verbNoter(QLocalSocket *client, int id, const QJsonObject &args,
                          const QString &verb) {
    if (!m_host.noterOp) {
        sendError(client, id, QStringLiteral("%1 not supported by host").arg(verb));
        return;
    }
    QString err;
    auto run = [this, verb](const QJsonObject &a) {
        return [this, verb, a](QString *e) { return m_host.noterOp(verb, a, e); };
    };

    if (verb == QLatin1String("search_notes")) {
        QString q;
        int max = 0;
        if (!(err = reqString(args, "query", &q)).isEmpty() ||
            !(err = optInt(args, "max_results", 1, 100, 20, false, &max)).isEmpty()) {
            sendError(client, id, err);
            return;
        }
        const QString root = m_host.notesRoot ? m_host.notesRoot() : QString();
        if (const QString blocked = NotesStorage::accessBlockedMessage(root); !blocked.isEmpty()) {
            sendError(client, id, blocked);
            return;
        }
        const QJsonObject r = m_host.noterOp(verb, {{QStringLiteral("query"), q},
                                                    {QStringLiteral("max_results"), max}},
                                             &err);
        sendOp(client, id, r, err);
        return;
    }

    // ── reminders (id from list_reminders) ──
    if (verb == QLatin1String("delete_reminder") || verb == QLatin1String("snooze_reminder") ||
        verb == QLatin1String("dismiss_reminder")) {
        QString rid;
        int minutes = 10;
        if (!(err = reqString(args, "id", &rid)).isEmpty() ||
            (verb == QLatin1String("snooze_reminder") &&
             !(err = optInt(args, "minutes", 1, 10080, 10, false, &minutes)).isEmpty())) {
            sendError(client, id, err);
            return;
        }
        if (verb != QLatin1String("snooze_reminder") && args.contains(QLatin1String("minutes"))) {
            sendError(client, id, QStringLiteral("minutes is only for snooze_reminder"));
            return;
        }
        const QJsonObject info =
            m_host.noterOp(QStringLiteral("reminder_info"), {{QStringLiteral("id"), rid}}, &err);
        if (!err.isEmpty()) {
            sendError(client, id, err);
            return;
        }
        const QString title =
            elideForCard(sanitizeForCard(info.value(QLatin1String("note_title")).toString()),
                         kApprovalDescChars);
        const QDateTime due = QDateTime::fromString(
            info.value(QLatin1String("due_iso")).toString(), Qt::ISODate).toLocalTime();
        const QString dueText = due.isValid()
            ? due.toString(QStringLiteral("yyyy-MM-dd HH:mm")) : QStringLiteral("unknown");
        QString desc;
        QJsonObject a{{QStringLiteral("id"), rid}};
        if (verb == QLatin1String("delete_reminder")) {
            desc = QStringLiteral("delete the reminder on note '%1' (due %2)").arg(title, dueText);
        } else if (verb == QLatin1String("dismiss_reminder")) {
            desc = QStringLiteral("dismiss the reminder on note '%1' (due %2); it will not "
                                  "fire again").arg(title, dueText);
        } else {
            const QDateTime until = QDateTime::currentDateTime().addSecs(qint64(minutes) * 60);
            desc = QStringLiteral("snooze the reminder on note '%1' for %2 minute(s), until %3")
                       .arg(title).arg(minutes)
                       .arg(until.toString(QStringLiteral("yyyy-MM-dd HH:mm")));
            a[QStringLiteral("minutes")] = minutes;
        }
        enqueueApprovalEx(client, id, desc, QString(), CardOptions(), run(a));
        return;
    }

    // ── notes (file from list_notes / search_notes) ──
    const QString fileCanon = resolveNotePath(args, &err);
    if (fileCanon.isEmpty()) {
        sendError(client, id, err);
        return;
    }
    const QString root = m_host.notesRoot ? m_host.notesRoot() : QString();
    const QString rootCanon = QFileInfo(root).canonicalFilePath();
    const bool inTrash =
        fileCanon.startsWith(rootCanon + QStringLiteral("/Trash/"));
    const QString title =
        elideForCard(sanitizeForCard(noteTitleForCard(root, fileCanon)), kApprovalDescChars);
    QJsonObject a{{QStringLiteral("file"), fileCanon}};
    const bool trashVerb = verb == QLatin1String("restore_note") ||
                           verb == QLatin1String("delete_note_permanently");
    if (trashVerb && !inTrash) {
        sendError(client, id, QStringLiteral("'%1' is not in the Noter Trash; %2 only works on "
                                             "trashed notes (trash_note first)").arg(title, verb));
        return;
    }
    if (!trashVerb && inTrash) {
        sendError(client, id, QStringLiteral("'%1' is in the Noter Trash; restore_note it "
                                             "first").arg(title));
        return;
    }

    if (verb == QLatin1String("rename_note")) {
        QString nt;
        if (!(err = reqString(args, "new_title", &nt)).isEmpty()) {
            sendError(client, id, err);
            return;
        }
        nt = nt.trimmed();
        if (nt.size() > 200) {
            sendError(client, id, QStringLiteral("new_title is too long (max 200 characters)"));
            return;
        }
        a[QStringLiteral("new_title")] = nt;
        enqueueApprovalEx(client, id,
                          QStringLiteral("rename note '%1' to '%2' (the file is renamed to "
                                         "match)").arg(title, sanitizeForCard(nt)),
                          QString(), CardOptions(), run(a));
        return;
    }
    if (verb == QLatin1String("trash_note")) {
        enqueueApprovalEx(client, id,
                          QStringLiteral("move note '%1' to the Noter Trash (restore_note "
                                         "brings it back)").arg(title),
                          QString(), CardOptions(), run(a));
        return;
    }
    if (verb == QLatin1String("restore_note")) {
        enqueueApprovalEx(client, id,
                          QStringLiteral("restore note '%1' from the Noter Trash to the "
                                         "Inbox").arg(title),
                          QString(), CardOptions(), run(a));
        return;
    }
    if (verb == QLatin1String("delete_note_permanently")) {
        CardOptions o;
        o.warning = QStringLiteral("This cannot be undone: the note and its backup copies "
                                   "are deleted from disk.");
        enqueueApprovalEx(client, id,
                          QStringLiteral("permanently delete note '%1' from the Noter "
                                         "Trash").arg(title),
                          QString(), o, run(a));
        return;
    }
    if (verb == QLatin1String("export_note")) {
        QString format;
        if (!(err = optEnum(args, "format",
                            {QStringLiteral("markdown"), QStringLiteral("pdf")}, QString(),
                            &format)).isEmpty()) {
            sendError(client, id, err);
            return;
        }
        const QJsonObject caps =
            m_host.noterOp(QStringLiteral("export_formats"), QJsonObject(), &err);
        const QStringList formats = jsonStrings(caps.value(QLatin1String("formats")));
        if (!formats.contains(format)) {
            sendError(client, id, QStringLiteral(
                "export format '%1' is not available in this build (available: %2)")
                                      .arg(format, formats.join(QStringLiteral(", "))));
            return;
        }
        const QString path = QDir::cleanPath(
            QDir::fromNativeSeparators(args.value(QLatin1String("path")).toString()));
        if (path.isEmpty() || path == QLatin1String(".")) {
            sendError(client, id, QStringLiteral("missing path"));
            return;
        }
        if (!QFileInfo(path).isAbsolute()) {
            sendError(client, id, QStringLiteral("path must be absolute"));
            return;
        }
        if (!QFileInfo(QFileInfo(path).absolutePath()).isDir()) {
            sendError(client, id, QStringLiteral("parent directory does not exist: %1")
                                      .arg(QDir::toNativeSeparators(QFileInfo(path).absolutePath())));
            return;
        }
        if (QFileInfo(path).isDir()) {
            sendError(client, id, QStringLiteral("path is a directory"));
            return;
        }
        if (const QString deny = writeDestinationRefusal(path); !deny.isEmpty()) {
            sendError(client, id, deny);
            return;
        }
        const QString shown = sanitizeForCard(QDir::toNativeSeparators(path));
        const QString dest = QFileInfo(path).isFile()
                                 ? QStringLiteral("OVERWRITE existing file: ") + shown
                                 : shown;
        a[QStringLiteral("format")] = format;
        a[QStringLiteral("path")] = path;
        enqueueApprovalEx(client, id,
                          QStringLiteral("export note '%1' as %2 to %3%4")
                              .arg(title,
                                   format == QLatin1String("pdf") ? QStringLiteral("PDF")
                                                                  : QStringLiteral("Markdown"),
                                   dest,
                                   format == QLatin1String("markdown")
                                       ? QStringLiteral(" (images go to a .assets folder "
                                                        "next to it)")
                                       : QString()),
                          QString(), CardOptions(), run(a));
        return;
    }
    sendError(client, id, QStringLiteral("unknown verb: %1").arg(verb));
}

// The ONE allow-list of preferences MCP may read or write. Editor and view
// settings only: nothing that holds a credential, a URL, a plugin path, an
// update or network setting, recent files or Noter state.
QJsonArray McpBridge::preferenceSchema() {
    auto b = [](const char *k, const char *d) {
        return QJsonObject{{QStringLiteral("key"), QLatin1String(k)},
                           {QStringLiteral("type"), QStringLiteral("boolean")},
                           {QStringLiteral("description"), QLatin1String(d)}};
    };
    auto i = [](const char *k, int lo, int hi, const char *d) {
        return QJsonObject{{QStringLiteral("key"), QLatin1String(k)},
                           {QStringLiteral("type"), QStringLiteral("integer")},
                           {QStringLiteral("min"), lo},
                           {QStringLiteral("max"), hi},
                           {QStringLiteral("description"), QLatin1String(d)}};
    };
    auto e = [](const char *k, const QStringList &v, const char *d) {
        return QJsonObject{{QStringLiteral("key"), QLatin1String(k)},
                           {QStringLiteral("type"), QStringLiteral("enum")},
                           {QStringLiteral("values"), QJsonArray::fromStringList(v)},
                           {QStringLiteral("description"), QLatin1String(d)}};
    };
    static const QJsonArray s = {
        i("font_size", 6, 48, "Editor font size in points"),
        QJsonObject{{QStringLiteral("key"), QStringLiteral("font_family")},
                    {QStringLiteral("type"), QStringLiteral("string")},
                    {QStringLiteral("description"),
                     QStringLiteral("Editor font family (must be installed)")}},
        i("tab_width", 1, 16, "Columns per tab stop"),
        b("use_spaces", "Insert spaces instead of a tab character"),
        b("auto_indent", "Keep the indentation of the previous line"),
        b("word_wrap", "Wrap long lines"),
        b("show_line_numbers", "Line-number margin"),
        b("show_indent_guides", "Indentation guides"),
        b("highlight_current_line", "Highlight the caret line"),
        b("show_edge", "Long-line edge marker"),
        i("edge_column", 20, 500, "Column of the long-line edge marker"),
        i("caret_width", 1, 3, "Caret width in pixels"),
        b("smooth_font", "Anti-aliased font rendering"),
        e("fold_style", {QStringLiteral("BoxedTree"), QStringLiteral("CircleTree"),
                         QStringLiteral("Plain"), QStringLiteral("Boxed"),
                         QStringLiteral("Circle"), QStringLiteral("None")},
          "Fold margin style"),
        b("show_bookmark_margin", "Bookmark margin"),
        b("show_document_rulers", "Document rulers"),
        b("show_crosshair", "Crosshair cursor guides"),
        b("auto_complete", "Word auto-completion"),
        i("auto_complete_threshold", 1, 10, "Characters typed before completion shows"),
        e("default_eol", {QStringLiteral("Unix"), QStringLiteral("Windows"),
                          QStringLiteral("Mac")},
          "Line ending for new documents"),
        i("autosave_interval_sec", 1, 300, "Session autosave interval in seconds"),
        e("theme", {QStringLiteral("System"), QStringLiteral("Light"),
                    QStringLiteral("Dark"), QStringLiteral("Monokai")},
          "Colour theme"),
        b("hide_toolbar", "Hide the toolbar"),
        b("tabs_closable", "Close button on each tab"),
        b("double_click_to_close_tab", "Double-click a tab to close it"),
        b("show_welcome_on_startup", "Show the Welcome tab when nothing is restored")};
    return s;
}

QString McpBridge::workspaceFolderRefusal(const QString &dir, QString *canon) {
    const QString in = QDir::fromNativeSeparators(dir);
    if (in.isEmpty()) return QStringLiteral("missing path");
    if (!QDir::isAbsolutePath(in)) return QStringLiteral("path must be absolute");
    const QFileInfo fi(in);
    if (!fi.isDir()) return QStringLiteral("not an existing folder: %1")
                                .arg(QDir::toNativeSeparators(in));
    const QString c = fi.canonicalFilePath();
    if (c.isEmpty()) return QStringLiteral("cannot resolve %1").arg(QDir::toNativeSeparators(in));
    if (canon) *canon = c;
    auto refuse = [&](const QString &why) {
        return QStringLiteral("refusing to make %1 the workspace folder: %2")
            .arg(QDir::toNativeSeparators(c), why);
    };
    if (QDir(c).isRoot()) return refuse(QStringLiteral("it is the filesystem root"));
    const QString home = QFileInfo(QDir::homePath()).canonicalFilePath();
    if (!home.isEmpty()) {
        if (c == home) return refuse(QStringLiteral("it is your home folder; pick a project folder"));
        if (home.startsWith(c + QLatin1Char('/')))
            return refuse(QStringLiteral("it contains your home folder"));
        static const char *kHomeDeny[] = {
            ".ssh", ".gnupg", ".config", ".aws", ".azure", ".kube", ".docker",
            ".password-store", ".local/share/keyrings", ".mozilla", ".thunderbird",
            "Library/Keychains", "Library/Application Support", "Library/Cookies",
            "AppData", ".npmrc", ".pki", ".cert"};
        for (const char *d : kHomeDeny) {
            const QString p = home + QLatin1Char('/') + QLatin1String(d);
            if (c == p || c.startsWith(p + QLatin1Char('/')))
                return refuse(QStringLiteral("~/%1 holds credentials or app settings")
                                  .arg(QLatin1String(d)));
        }
    }
    static const char *kSystem[] = {"/etc", "/proc", "/sys", "/dev", "/boot", "/root",
                                    "/var", "/private", "/usr", "/bin", "/sbin", "/lib",
                                    "/System", "/Library", "C:/Windows", "C:/ProgramData"};
    for (const char *d : kSystem) {
        const QString p = QLatin1String(d);
        if (c.compare(p, Qt::CaseInsensitive) == 0 ||
            c.startsWith(p + QLatin1Char('/'), Qt::CaseInsensitive)) {
            // A temp folder under /var or /private is a normal project place.
            const QString tmp = QFileInfo(QDir::tempPath()).canonicalFilePath();
            if (!tmp.isEmpty() && (c == tmp || c.startsWith(tmp + QLatin1Char('/')))) break;
            return refuse(QStringLiteral("it is a system folder"));
        }
    }
    if (PathDenylist::isSecretPath(c) || PathDenylist::isSecretPath(c + QStringLiteral("/x")))
        return refuse(QStringLiteral("it is on the credential deny-list"));
    return QString();
}

void McpBridge::verbSettings(QLocalSocket *client, int id, const QJsonObject &args,
                             const QString &verb) {
    if (!m_host.settingsOp) {
        sendError(client, id, QStringLiteral("%1 not supported by host").arg(verb));
        return;
    }
    QString err;
    if (verb == QLatin1String("get_preferences")) {
        QJsonObject r = m_host.settingsOp(verb, QJsonObject(), &err);
        if (err.isEmpty()) {
            // Only allow-listed keys leave the bridge, whatever the host sent.
            const QJsonObject all = r.value(QLatin1String("preferences")).toObject();
            QJsonObject prefs;
            for (const QJsonValue &k : preferenceSchema()) {
                const QString key = k.toObject().value(QLatin1String("key")).toString();
                if (all.contains(key)) prefs[key] = all.value(key);
            }
            r = QJsonObject{{QStringLiteral("preferences"), prefs},
                            {QStringLiteral("keys"), preferenceSchema()}};
        }
        sendOp(client, id, r, err);
        return;
    }
    if (verb == QLatin1String("set_preference")) {
        const QString key = args.value(QLatin1String("key")).toString();
        QJsonObject spec;
        QStringList allowed;
        for (const QJsonValue &k : preferenceSchema()) {
            allowed << k.toObject().value(QLatin1String("key")).toString();
            if (allowed.last() == key) spec = k.toObject();
        }
        if (spec.isEmpty()) {
            sendError(client, id, QStringLiteral("unknown or protected preference key '%1'; "
                                                 "settable keys: %2")
                                      .arg(sanitizeForCard(key).left(64),
                                           allowed.join(QStringLiteral(", "))));
            return;
        }
        const QString type = spec.value(QLatin1String("type")).toString();
        const QJsonValue v = args.value(QLatin1String("value"));
        QJsonValue value;
        if (type == QLatin1String("boolean")) {
            if (!v.isBool()) err = QStringLiteral("%1 must be a boolean").arg(key);
            value = v;
        } else if (type == QLatin1String("integer")) {
            const int lo = spec.value(QLatin1String("min")).toInt();
            const int hi = spec.value(QLatin1String("max")).toInt();
            const double d = v.toDouble(-1e18);
            if (!v.isDouble() || d != double(qint64(d)) || d < lo || d > hi)
                err = QStringLiteral("%1 must be an integer from %2 to %3").arg(key).arg(lo).arg(hi);
            value = int(d);
        } else if (type == QLatin1String("enum")) {
            const QStringList vals = jsonStrings(spec.value(QLatin1String("values")));
            QString hit;
            for (const QString &x : vals)
                if (v.isString() && x.compare(v.toString(), Qt::CaseInsensitive) == 0) hit = x;
            if (hit.isEmpty())
                err = QStringLiteral("%1 must be one of %2").arg(key, vals.join(QLatin1Char('|')));
            value = hit;
        } else {
            if (!v.isString() || v.toString().trimmed().isEmpty() || v.toString().size() > 100)
                err = QStringLiteral("%1 must be a non-empty string").arg(key);
            value = v.toString().trimmed();
        }
        if (err.isEmpty() && key == QLatin1String("font_family")) {
            const QJsonObject chk = m_host.settingsOp(QStringLiteral("check_font"),
                                                      {{QStringLiteral("value"), value}}, &err);
            Q_UNUSED(chk);
        }
        if (!err.isEmpty()) {
            sendError(client, id, err);
            return;
        }
        const QJsonObject cur = m_host.settingsOp(QStringLiteral("get_preferences"),
                                                  QJsonObject(), &err)
                                    .value(QLatin1String("preferences")).toObject();
        auto show = [](const QJsonValue &x) {
            return x.isBool() ? QString::fromLatin1(x.toBool() ? "true" : "false")
                 : x.isDouble() ? QString::number(x.toInt())
                                : sanitizeForCard(x.toString());
        };
        if (cur.value(key) == value) {
            sendResult(client, id, {{QStringLiteral("key"), key},
                                    {QStringLiteral("value"), value},
                                    {QStringLiteral("changed"), false}});
            return;
        }
        const QString desc = QStringLiteral("change preference %1: %2 -> %3 (applied to every "
                                            "open editor and saved)")
                                 .arg(key, show(cur.value(key)), show(value));
        enqueueApprovalEx(client, id, desc, QString(), CardOptions(),
                          [this, key, value](QString *e) {
                              return m_host.settingsOp(
                                  QStringLiteral("set_preference"),
                                  {{QStringLiteral("key"), key}, {QStringLiteral("value"), value}},
                                  e);
                          });
        return;
    }
    if (verb == QLatin1String("set_workspace_folder")) {
        QString canon;
        if (const QString r = workspaceFolderRefusal(args.value(QLatin1String("path")).toString(),
                                                     &canon);
            !r.isEmpty()) {
            sendError(client, id, r);
            return;
        }
        const QString cur = m_host.workspaceRoot ? m_host.workspaceRoot() : QString();
        const QString desc =
            QStringLiteral("set the workspace folder to %1 (was: %2). Read tools such as "
                           "search_project and the git tools then see the files under it.")
                .arg(sanitizeForCard(QDir::toNativeSeparators(canon)),
                     cur.isEmpty() ? QStringLiteral("none")
                                   : sanitizeForCard(QDir::toNativeSeparators(cur)));
        enqueueApprovalEx(client, id, desc, QString(), CardOptions(), [this, canon](QString *e) {
            return m_host.settingsOp(QStringLiteral("set_workspace_folder"),
                                     {{QStringLiteral("path"), canon}}, e);
        });
        return;
    }
    sendError(client, id, QStringLiteral("unknown verb: %1").arg(verb));
}

void McpBridge::verbData(QLocalSocket *client, int id, const QJsonObject &args,
                         const QString &verb) {
    if (!m_host.dataOp) {
        sendError(client, id, QStringLiteral("%1 not supported by host").arg(verb));
        return;
    }
    QString err;
    if (verb == QLatin1String("open_connections_dialog") ||
        verb == QLatin1String("open_schema_browser")) {
        const QJsonObject r = m_host.dataOp(verb, QJsonObject(), &err);
        sendOp(client, id, r, err);
        return;
    }
    QString name;
    if (!(err = reqString(args, "connection_name", &name)).isEmpty() &&
        !(verb == QLatin1String("add_connection") || verb == QLatin1String("update_connection") ||
          verb == QLatin1String("delete_connection"))) {
        sendError(client, id, err);
        return;
    }
    // Connection tools name the connection "name" (the record's own field).
    if (verb == QLatin1String("add_connection") || verb == QLatin1String("update_connection") ||
        verb == QLatin1String("delete_connection")) {
        if (!(err = reqString(args, "name", &name)).isEmpty()) {
            sendError(client, id, err);
            return;
        }
    }
    const QJsonObject info = m_host.dataOp(QStringLiteral("connection_info"),
                                           {{QStringLiteral("connection_name"), name}}, &err);
    const bool exists = err.isEmpty();
    auto describe = [](const QJsonObject &c) {
        QString s = QStringLiteral("driver %1, database %2")
                        .arg(c.value(QLatin1String("driver")).toString(),
                             sanitizeForCard(c.value(QLatin1String("database")).toString()));
        const QString host = c.value(QLatin1String("host")).toString();
        if (!host.isEmpty()) {
            s += QStringLiteral(", host %1").arg(sanitizeForCard(host));
            if (c.value(QLatin1String("port")).toInt() > 0)
                s += QStringLiteral(":%1").arg(c.value(QLatin1String("port")).toInt());
        }
        return s;
    };

    if (verb == QLatin1String("describe_table") || verb == QLatin1String("test_connection")) {
        if (!exists) {
            sendError(client, id, err);
            return;
        }
        if (verb == QLatin1String("describe_table")) {
            QString table;
            if (!(err = reqString(args, "table", &table)).isEmpty()) {
                sendError(client, id, err);
                return;
            }
            if (table.size() > 256 || table != sanitizeForCard(table)) {
                sendError(client, id, QStringLiteral("invalid table name"));
                return;
            }
            const QJsonObject r = m_host.dataOp(verb, {{QStringLiteral("connection_name"), name},
                                                       {QStringLiteral("table"), table}},
                                                &err);
            sendOp(client, id, r, err);
            return;
        }
        if (!m_host.dataAsync) {
            sendError(client, id, QStringLiteral("%1 not supported by host").arg(verb));
            return;
        }
        QPointer<QLocalSocket> c(client);
        QPointer<McpBridge> self(this);
        m_host.dataAsync(verb, {{QStringLiteral("connection_name"), name}},
                         [self, c, id](const QJsonObject &r, const QString &e) {
                             if (self && c && c->state() == QLocalSocket::ConnectedState)
                                 self->sendOp(c, id, r, e);
                         });
        return;
    }

    if (verb == QLatin1String("execute_sql")) {
        if (!exists) {
            sendError(client, id, err);
            return;
        }
        if (!m_host.dataAsync) {
            sendError(client, id, QStringLiteral("execute_sql not supported by host"));
            return;
        }
        QString sql;
        if (!(err = reqString(args, "sql", &sql)).isEmpty()) {
            sendError(client, id, err);
            return;
        }
        // The app's own gate classifies; approval never overrides a hard refusal.
        const QJsonObject v = m_host.dataOp(QStringLiteral("classify_sql"),
                                            {{QStringLiteral("connection_name"), name},
                                             {QStringLiteral("sql"), sql}},
                                            &err);
        if (!err.isEmpty()) {
            sendError(client, id, err);
            return;
        }
        const int count = v.value(QLatin1String("statement_count")).toInt();
        if (!v.value(QLatin1String("single_statement")).toBool()) {
            sendError(client, id, QStringLiteral(
                "execute_sql runs exactly one statement; this text has %1 (stacked "
                "statements are refused)").arg(qMax(count, 2)));
            return;
        }
        if (v.value(QLatin1String("hard_fail")).toBool()) {
            sendError(client, id, QStringLiteral("refused by the SQL gate (approval cannot "
                                                 "override this): %1")
                                      .arg(v.value(QLatin1String("reason")).toString()));
            return;
        }
        if (!v.value(QLatin1String("mutation")).toBool()) {
            sendError(client, id, QStringLiteral(
                "this statement does not change data; run it with run_query instead "
                "(execute_sql is only for statements that modify the database)%1")
                                      .arg(v.value(QLatin1String("read_only")).toBool()
                                               ? QString()
                                               : QStringLiteral(": ") +
                                                     v.value(QLatin1String("reason")).toString()));
            return;
        }
        CardOptions o;
        o.fullPreview = true;
        o.warning = QStringLiteral("This changes data in the database. Notepatra cannot undo "
                                   "it.");
        const QString desc = QStringLiteral("run SQL on connection '%1' (%2): 1 statement")
                                 .arg(sanitizeForCard(name), describe(info));
        const QJsonObject a{{QStringLiteral("connection_name"), name},
                            {QStringLiteral("sql"), sql}};
        enqueueApprovalEx(client, id, desc, sanitizeMultiline(sql), o, nullptr,
                          [this, a](McpEditorHost::Done done) {
                              m_host.dataAsync(QStringLiteral("execute_sql"), a, done);
                          });
        return;
    }

    // ── connection CRUD: never a password, token or options string ──
    static const QStringList kDrivers = {QStringLiteral("QSQLITE"), QStringLiteral("QPSQL"),
                                         QStringLiteral("QMYSQL"), QStringLiteral("QODBC"),
                                         QStringLiteral("DUCKDB")};
    static const QStringList kFields = {QStringLiteral("name"),     QStringLiteral("new_name"),
                                        QStringLiteral("driver"),   QStringLiteral("host"),
                                        QStringLiteral("port"),     QStringLiteral("database"),
                                        QStringLiteral("username")};
    for (auto it = args.begin(); it != args.end(); ++it) {
        if (!kFields.contains(it.key())) {
            sendError(client, id, QStringLiteral(
                "unexpected field '%1': passwords, tokens and connection options are never "
                "taken over MCP; the user enters them in Manage Connections")
                                      .arg(sanitizeForCard(it.key()).left(40)));
            return;
        }
        if (it.key() != QLatin1String("port") && !it.value().isString()) {
            sendError(client, id, QStringLiteral("%1 must be a string").arg(it.key()));
            return;
        }
    }
    if (verb == QLatin1String("delete_connection")) {
        if (!exists) {
            sendError(client, id, err);
            return;
        }
        enqueueApprovalEx(client, id,
                          QStringLiteral("delete the saved database connection '%1' (%2). "
                                         "The database itself is not touched.")
                              .arg(sanitizeForCard(name), describe(info)),
                          QString(), CardOptions(), [this, name](QString *e) {
                              return m_host.dataOp(QStringLiteral("delete_connection"),
                                                   {{QStringLiteral("name"), name}}, e);
                          });
        return;
    }
    if (verb == QLatin1String("add_connection") && exists) {
        sendError(client, id, QStringLiteral("a connection named '%1' already exists").arg(name));
        return;
    }
    if (verb == QLatin1String("update_connection") && !exists) {
        sendError(client, id, err);
        return;
    }
    if (verb == QLatin1String("add_connection") && args.contains(QLatin1String("new_name"))) {
        sendError(client, id, QStringLiteral("new_name is only for update_connection"));
        return;
    }
    // The record as it will be saved.
    QJsonObject rec = exists ? info : QJsonObject();
    rec[QStringLiteral("name")] = name;
    for (const QString &k : {QStringLiteral("driver"), QStringLiteral("host"),
                             QStringLiteral("database"), QStringLiteral("username")})
        if (args.contains(k)) rec[k] = args.value(k).toString().trimmed();
    if (args.contains(QLatin1String("port"))) {
        int port = 0;
        if (!(err = optInt(args, "port", 0, 65535, 0, true, &port)).isEmpty()) {
            sendError(client, id, err);
            return;
        }
        rec[QStringLiteral("port")] = port;
    }
    if (args.contains(QLatin1String("new_name"))) {
        const QString nn = args.value(QLatin1String("new_name")).toString().trimmed();
        if (nn.isEmpty()) {
            sendError(client, id, QStringLiteral("new_name must not be empty"));
            return;
        }
        QString e2;
        m_host.dataOp(QStringLiteral("connection_info"), {{QStringLiteral("connection_name"), nn}},
                      &e2);
        if (e2.isEmpty() && nn != name) {
            sendError(client, id, QStringLiteral("a connection named '%1' already exists").arg(nn));
            return;
        }
        rec[QStringLiteral("name")] = nn;
    }
    const QString driver = rec.value(QLatin1String("driver")).toString().toUpper();
    rec[QStringLiteral("driver")] = driver;
    if (name.trimmed() != name || name.size() > 100 || name != sanitizeForCard(name) ||
        rec.value(QLatin1String("name")).toString().size() > 100) {
        sendError(client, id, QStringLiteral("name must be 1-100 printable characters without "
                                             "leading or trailing spaces"));
        return;
    }
    if (!kDrivers.contains(driver)) {
        sendError(client, id, QStringLiteral("driver must be one of %1")
                                  .arg(kDrivers.join(QLatin1Char('|'))));
        return;
    }
    const QString db = rec.value(QLatin1String("database")).toString();
    if (db.isEmpty()) {
        sendError(client, id, QStringLiteral("missing database"));
        return;
    }
    const bool fileDriver = driver == QLatin1String("QSQLITE") || driver == QLatin1String("DUCKDB");
    if (fileDriver && db != QLatin1String(":memory:")) {
        if (!QDir::isAbsolutePath(QDir::fromNativeSeparators(db))) {
            sendError(client, id, QStringLiteral("database must be an absolute file path (or "
                                                 ":memory:) for %1").arg(driver));
            return;
        }
        if (const QString deny = writeDestinationRefusal(db); !deny.isEmpty()) {
            sendError(client, id, deny);
            return;
        }
    }
    QStringList changes;
    for (const QString &k : {QStringLiteral("name"), QStringLiteral("driver"),
                             QStringLiteral("host"), QStringLiteral("port"),
                             QStringLiteral("database"), QStringLiteral("username")}) {
        const QJsonValue before = info.value(k), after = rec.value(k);
        if (exists && before == after) continue;
        if (!exists && (after.isUndefined() || after.toString() == QString() ||
                        (k == QLatin1String("port") && after.toInt() == 0)))
            continue;
        const QString a = after.isDouble() ? QString::number(after.toInt())
                                           : sanitizeForCard(after.toString());
        changes << (exists ? QStringLiteral("%1: %2 -> %3")
                                 .arg(k, before.isDouble() ? QString::number(before.toInt())
                                                           : sanitizeForCard(before.toString()),
                                      a)
                           : QStringLiteral("%1: %2").arg(k, a));
    }
    if (exists && changes.isEmpty()) {
        sendError(client, id, QStringLiteral("nothing to change"));
        return;
    }
    CardOptions o;
    const bool serverChanged =
        exists && (info.value(QLatin1String("driver")) != rec.value(QLatin1String("driver")) ||
                   info.value(QLatin1String("host")) != rec.value(QLatin1String("host")) ||
                   info.value(QLatin1String("port")) != rec.value(QLatin1String("port")) ||
                   info.value(QLatin1String("username")) != rec.value(QLatin1String("username")));
    const bool hadSecret = info.value(QLatin1String("has_password")).toBool() ||
                           info.value(QLatin1String("has_options")).toBool();
    if (serverChanged && hadSecret)
        o.warning = QStringLiteral("The saved password and connection options are cleared "
                                   "because the server or account changes; the user re-enters "
                                   "them in Manage Connections.");
    rec[QStringLiteral("clear_secret")] = serverChanged;
    rec[QStringLiteral("original_name")] = name;
    const QString desc =
        verb == QLatin1String("add_connection")
            ? QStringLiteral("add the database connection '%1' (no password is stored; the "
                             "user enters it in Manage Connections)").arg(sanitizeForCard(name))
            : QStringLiteral("update the database connection '%1'").arg(sanitizeForCard(name));
    enqueueApprovalEx(client, id, desc, changes.join(QLatin1Char('\n')), o,
                      [this, verb, rec](QString *e) { return m_host.dataOp(verb, rec, e); });
}

namespace {
// What the card says about a request's host, without a DNS lookup.
QString hostKind(const QString &host) {
    const QString h = host.toLower();
    if (h == QLatin1String("localhost") || h.endsWith(QLatin1String(".localhost")))
        return QStringLiteral("loopback: this machine");
    QHostAddress a;
    if (!a.setAddress(h)) return QStringLiteral("resolved by DNS when sent");
    if (a.isLoopback()) return QStringLiteral("loopback: this machine");
    if (a.isLinkLocal()) return QStringLiteral("link-local network address");
    const quint32 v4 = a.protocol() == QAbstractSocket::IPv4Protocol ? a.toIPv4Address() : 0;
    const bool priv4 = (v4 >> 24) == 10 || (v4 >> 20) == 0xAC1 || (v4 >> 16) == 0xC0A8 ||
                       (v4 >> 22) == 0x191; // 10/8, 172.16/12, 192.168/16, 100.64/10
    if (priv4 || a.isUniqueLocalUnicast() || a.isSiteLocal())
        return QStringLiteral("private network address");
    return QStringLiteral("public address");
}

bool validHeaderName(const QString &n) {
    static const QRegularExpression re(QStringLiteral("^[!#$%&'*+.^_`|~0-9A-Za-z-]+$"));
    return re.match(n).hasMatch();
}
} // namespace

void McpBridge::verbRest(QLocalSocket *client, int id, const QJsonObject &args,
                         const QString &verb) {
    if (!m_host.restOp) {
        sendError(client, id, QStringLiteral("%1 not supported by host").arg(verb));
        return;
    }
    QString err;
    static const QStringList kMethods = {
        QStringLiteral("GET"),    QStringLiteral("POST"),   QStringLiteral("PUT"),
        QStringLiteral("PATCH"),  QStringLiteral("DELETE"), QStringLiteral("HEAD"),
        QStringLiteral("OPTIONS")};
    // {name: value} → [{name,value}], validated.
    auto headersArg = [&](QJsonArray *out) -> QString {
        if (!args.contains(QLatin1String("headers"))) return QString();
        const QJsonValue hv = args.value(QLatin1String("headers"));
        if (!hv.isObject()) return QStringLiteral("headers must be an object of name: value strings");
        const QJsonObject ho = hv.toObject();
        for (auto it = ho.begin(); it != ho.end(); ++it) {
            if (!validHeaderName(it.key()))
                return QStringLiteral("invalid header name: %1").arg(sanitizeForCard(it.key()).left(60));
            if (!it.value().isString() || it.value().toString().contains(QLatin1Char('\n')) ||
                it.value().toString().contains(QLatin1Char('\r')))
                return QStringLiteral("header %1 must be a single-line string").arg(it.key());
            out->append(QJsonObject{{QStringLiteral("name"), it.key()},
                                    {QStringLiteral("value"), it.value().toString()}});
        }
        return QString();
    };

    if (verb == QLatin1String("parse_http_file") || verb == QLatin1String("curl_to_request")) {
        QString text;
        int tab = -1;
        if (verb == QLatin1String("curl_to_request")) {
            if (!(err = reqString(args, "text", &text)).isEmpty()) {
                sendError(client, id, err);
                return;
            }
        } else if (!textOrTab(args, QString(), &text, &tab, &err)) {
            sendError(client, id, err);
            return;
        }
        if (text.size() > kComputeMaxChars) {
            sendError(client, id, QStringLiteral("text too large"));
            return;
        }
        QJsonObject r = m_host.restOp(verb, {{QStringLiteral("text"), text}}, &err);
        if (err.isEmpty() && tab >= 0) {
            r[QStringLiteral("tab_index")] = tab;
            if (const qint64 tid = tabIdOf(tab); tid >= 0) r[QStringLiteral("tab_id")] = tid;
        }
        sendOp(client, id, r, err);
        return;
    }

    QJsonObject req;
    const bool fromTab = args.contains(QLatin1String("tab_id")) ||
                         args.contains(QLatin1String("tab_index"));
    if (verb == QLatin1String("request_to_curl") || !fromTab) {
        QString method, url;
        QJsonArray headers;
        if (!(err = optEnum(args, "method", kMethods, QString(), &method)).isEmpty() ||
            !(err = reqString(args, "url", &url)).isEmpty() ||
            !(err = headersArg(&headers)).isEmpty()) {
            sendError(client, id, err);
            return;
        }
        if (args.contains(QLatin1String("body")) && !args.value(QLatin1String("body")).isString()) {
            sendError(client, id, QStringLiteral("body must be a string"));
            return;
        }
        if (fromTab || args.contains(QLatin1String("line"))) {
            sendError(client, id, QStringLiteral("give either method+url (and headers/body) or "
                                                 "a .http tab and line, not both"));
            return;
        }
        req = {{QStringLiteral("method"), method}, {QStringLiteral("url"), url},
               {QStringLiteral("headers"), headers},
               {QStringLiteral("body"), args.value(QLatin1String("body")).toString()}};
        if (verb == QLatin1String("request_to_curl")) {
            sendOp(client, id, m_host.restOp(verb, req, &err), err);
            return;
        }
    } else {
        for (const char *k : {"method", "url", "headers", "body"})
            if (args.contains(QLatin1String(k))) {
                sendError(client, id, QStringLiteral("give either method+url (and "
                                                     "headers/body) or a .http tab and line, "
                                                     "not both"));
                return;
            }
        int line = 0;
        QString text;
        int tab = -1;
        if (!(err = optInt(args, "line", 1, INT_MAX, 1, true, &line)).isEmpty() ||
            !textOrTab(args, QString(), &text, &tab, &err)) {
            sendError(client, id, err);
            return;
        }
        req = {{QStringLiteral("text"), text}, {QStringLiteral("line"), line - 1},
               {QStringLiteral("file_path"), m_host.tabPath ? m_host.tabPath(tab) : QString()}};
    }
    if (req.value(QLatin1String("body")).toString().size() > kRestBodyMax) {
        sendError(client, id, QStringLiteral("body too large (max 10 MB)"));
        return;
    }
    // Variables resolved exactly as the REST panel resolves them at send time.
    const QJsonObject res = m_host.restOp(QStringLiteral("resolve_request"), req, &err);
    if (!err.isEmpty()) {
        sendError(client, id, err);
        return;
    }
    const QUrl url(res.value(QLatin1String("url")).toString(), QUrl::StrictMode);
    const QString scheme = url.scheme().toLower();
    if (scheme != QLatin1String("http") && scheme != QLatin1String("https")) {
        sendError(client, id, QStringLiteral("refusing to send: only http:// and https:// URLs "
                                             "are allowed (got '%1')")
                                  .arg(sanitizeForCard(scheme.isEmpty() ? url.toString() : scheme)
                                           .left(60)));
        return;
    }
    if (!url.isValid() || url.host().isEmpty()) {
        sendError(client, id, QStringLiteral("invalid URL: %1")
                                  .arg(sanitizeForCard(url.toString()).left(200)));
        return;
    }
    QStringList names;
    for (const QJsonValue &h : res.value(QLatin1String("headers")).toArray()) {
        const QString n = h.toObject().value(QLatin1String("name")).toString();
        const QString v = h.toObject().value(QLatin1String("value")).toString();
        if (!validHeaderName(n) || v.contains(QLatin1Char('\n')) || v.contains(QLatin1Char('\r'))) {
            sendError(client, id, QStringLiteral("invalid header after variable expansion: %1")
                                      .arg(sanitizeForCard(n).left(60)));
            return;
        }
        names << n;
    }
    const QString method = res.value(QLatin1String("method")).toString();
    const int bodyBytes = res.value(QLatin1String("body")).toString().toUtf8().size();
    const QString port = url.port() > 0 ? QStringLiteral(":%1").arg(url.port()) : QString();
    const QString desc =
        QStringLiteral("send HTTP %1 to %2://%3%4%5")
            .arg(method, scheme, sanitizeForCard(url.host()), port,
                 sanitizeForCard(url.path().isEmpty() ? QStringLiteral("/") : url.path()));
    QStringList preview;
    preview << QStringLiteral("Host: %1 (%2)").arg(sanitizeForCard(url.host()), hostKind(url.host()));
    if (url.hasQuery())
        preview << QStringLiteral("Query string: %1 characters (not shown)").arg(url.query().size());
    preview << QStringLiteral("Headers: %1").arg(names.isEmpty() ? QStringLiteral("none")
                                                                 : names.join(QStringLiteral(", ")));
    preview << QStringLiteral("Body: %1 bytes").arg(bodyBytes);
    preview << QStringLiteral("Redirects are not followed; 30 s timeout; recorded in the REST "
                              "history with secrets redacted.");
    enqueueApprovalEx(client, id, desc, preview.join(QLatin1Char('\n')), CardOptions(), nullptr,
                      [this, req](McpEditorHost::Done done) {
                          if (!m_host.restAsync) {
                              done(QJsonObject(), QStringLiteral("rest_request not supported by host"));
                              return;
                          }
                          m_host.restAsync(QStringLiteral("rest_request"), req, done);
                      });
}

void McpBridge::verbMisc(QLocalSocket *client, int id, const QJsonObject &args,
                         const QString &verb) {
    if (!m_host.miscOp) {
        sendError(client, id, QStringLiteral("%1 not supported by host").arg(verb));
        return;
    }
    QString err;
    if (verb == QLatin1String("clear_recent_files")) {
        if (!args.isEmpty()) {
            sendError(client, id, QStringLiteral("clear_recent_files takes no arguments"));
            return;
        }
        const int n = m_host.recentFiles ? m_host.recentFiles().size() : 0;
        if (n == 0) {
            sendResult(client, id, {{QStringLiteral("cleared"), 0}});
            return;
        }
        enqueueApprovalEx(client, id,
                          QStringLiteral("clear the recent files list (%1 entries; the files "
                                         "themselves are not touched)").arg(n),
                          QString(), CardOptions(), [this](QString *e) {
                              return m_host.miscOp(QStringLiteral("clear_recent_files"),
                                                   QJsonObject(), e);
                          });
        return;
    }
    // hex_dump: a tab's file (or its text when untitled), or a workspace path.
    int offset = 0, length = 0;
    if (!(err = optInt(args, "offset", 0, INT_MAX, 0, false, &offset)).isEmpty() ||
        !(err = optInt(args, "length", 1, kHexDumpMax, 4096, false, &length)).isEmpty()) {
        sendError(client, id, err);
        return;
    }
    const bool hasTab = args.contains(QLatin1String("tab_id")) ||
                        args.contains(QLatin1String("tab_index"));
    if (hasTab == args.contains(QLatin1String("path"))) {
        sendError(client, id, QStringLiteral("provide a tab (tab_id / tab_index) or path"));
        return;
    }
    QJsonObject a{{QStringLiteral("offset"), offset}, {QStringLiteral("length"), length}};
    if (hasTab) {
        const int idx = resolveTab(args, "tab_index", false, &err);
        if (idx < 0) {
            sendError(client, id, err);
            return;
        }
        const QString p = m_host.tabPath ? m_host.tabPath(idx) : QString();
        if (!p.isEmpty()) {
            if (const QString deny = credentialRefusal(p); !deny.isEmpty()) {
                sendError(client, id, deny);
                return;
            }
            a[QStringLiteral("path")] = p;
            a[QStringLiteral("confine")] = false; // the user opened this file
        } else {
            QString text;
            if (!tabTextFor(idx, &text, &err)) {
                sendError(client, id, err);
                return;
            }
            a[QStringLiteral("text")] = text;
        }
    } else {
        QString p;
        if (!(err = reqString(args, "path", &p)).isEmpty()) {
            sendError(client, id, err);
            return;
        }
        if (const QString deny = credentialRefusal(p); !deny.isEmpty()) {
            sendError(client, id, deny);
            return;
        }
        a[QStringLiteral("path")] = p;
        a[QStringLiteral("confine")] = true;
    }
    const QJsonObject raw = m_host.miscOp(QStringLiteral("hex_bytes"), a, &err);
    if (!err.isEmpty()) {
        sendError(client, id, err);
        return;
    }
    const QByteArray bytes =
        QByteArray::fromBase64(raw.value(QLatin1String("base64")).toString().toLatin1());
    QStringList lines;
    for (int i = 0; i < bytes.size(); i += 16) {
        QString hex, ascii;
        for (int j = 0; j < 16; ++j) {
            if (i + j < bytes.size()) {
                const uchar c = uchar(bytes.at(i + j));
                hex += QStringLiteral("%1 ").arg(c, 2, 16, QLatin1Char('0'));
                ascii += (c >= 0x20 && c < 0x7F) ? QChar(c) : QChar('.');
            } else {
                hex += QStringLiteral("   ");
            }
            if (j == 7) hex += QLatin1Char(' ');
        }
        lines << QStringLiteral("%1  %2 |%3|").arg(offset + i, 8, 16, QLatin1Char('0'))
                     .arg(hex, ascii);
    }
    QJsonObject r{{QStringLiteral("offset"), offset},
                  {QStringLiteral("length"), bytes.size()},
                  {QStringLiteral("total_size"), raw.value(QLatin1String("total_size"))},
                  {QStringLiteral("source"), raw.value(QLatin1String("source"))},
                  {QStringLiteral("hex"), lines.join(QLatin1Char('\n'))}};
    r[QStringLiteral("truncated")] =
        qint64(offset) + bytes.size() < qint64(raw.value(QLatin1String("total_size")).toDouble());
    sendResult(client, id, r);
}
