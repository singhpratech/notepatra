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
// READ tier — answered immediately.
//   list_open_tabs {}                       → {tabs:[{index,title,path,modified}]}
//   read_tab    {index|title}               → {title,path,text,truncated?}
//   get_selection {}                        → {text,tab_index}
//   get_status  {}                          → {tab_index,title,path,language,
//                                              encoding,cursor_line,cursor_col,
//                                              edition,version}
//   app_info    {}                          → {name,version,edition,platform}
//   list_recent_files {}                    → {files:[...]}
//   find_in_tab {query,index?,regex?}       → {matches:[{line,text}],truncated}
//   search_project {query,max_results?,regex?} → {results:[{path,line,text}],
//                                                 truncated}
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
//
// ACT tier — visible, non-destructive, NO approval card.
//   new_tab     {text?}                     → {tab_index}
//   goto_line   {line,tab_index?}           → {ok,tab_index,line}
//   set_language{language,tab_index?}       → {ok,tab_index,language}
//   open_note   {file}                      → {opened,title}       (v0.1.119)
//
// WRITE tier — held behind the in-window human-approval card; a mutation
// runs ONLY on Approve (Deny / timeout / disconnect ⇒ nothing happened).
//   insert_text {text,tab_index?,line?,col?} → {tab_index}
//   replace_selection {text,tab_index?}      → {tab_index}
//   apply_edit  {find,replace,all?,tab_index?} → {count}
//   save_tab    {tab_index?}                 → {saved,tab_index}
//   create_note {title,body}                 → {file,title}        (v0.1.119)
//   append_note {file,text}                  → {file}              (v0.1.119)
//   set_reminder{file,due_iso}               → {file,due_iso}      (v0.1.119)
//   export_diagram {tab_index,path,format("png"|"pdf")} → {path}   (v0.1.119)
//
// NOTE: write verbs take "tab_index"; read tab-arg verbs keep their existing
// keys ("index" for read_tab/find_in_tab). This asymmetry is deliberate and
// load-bearing — do not "unify" it.
// ═══════════════════════════════════════════════════════════════════════
#include "mcp_bridge.h"
#include "build_flavor.h"
#include "diagram/npd_parser.h"
#include "notes_storage.h"
#include "singleinstance.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QDirIterator>
#include <QEvent>
#include <QFile>
#include <QFileInfo>
#include <QFrame>
#include <QFutureWatcher>
#include <QHBoxLayout>
#include <QJsonDocument>
#include <QJsonValue>
#include <QLabel>
#include <QLocalServer>
#include <QLocalSocket>
#include <QPointer>
#include <QPushButton>
#include <QRegularExpression>
#include <QSet>
#include <QTimer>
#include <QVBoxLayout>
#include <QWidget>
#include <QtConcurrent/QtConcurrentRun>

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

QString elideForCard(const QString &s, int cap) {
    if (s.size() <= cap) return s;
    return s.left(cap) + QChar(0x2026);
}

// Strip Unicode bidirectional / format controls and C0/C1 control codepoints
// from a string before it is SHOWN on an approval card, so a crafted path
// can't visually disguise its real destination (e.g. an embedded RLO that
// reverses the tail). Used for display text ONLY — the executed path is the
// exact validated absolute path and is never passed through this.
QString sanitizeForCard(const QString &s) {
    QString out;
    out.reserve(s.size());
    for (const QChar ch : s) {
        const ushort u = ch.unicode();
        // C0 controls (0x00–0x1F) and DEL + C1 controls (0x7F–0x9F).
        if (u <= 0x1F || (u >= 0x7F && u <= 0x9F)) continue;
        // Bidi/format controls: LRM/RLM (200E/200F), LRE/RLE/PDF/LRO/RLO
        // (202A–202E), isolates LRI/RLI/FSI/PDI (2066–2069).
        if (u == 0x200E || u == 0x200F
            || (u >= 0x202A && u <= 0x202E)
            || (u >= 0x2066 && u <= 0x2069)) continue;
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

struct SearchHit {
    QString path;
    int line;
    QString text;
};

QJsonArray hitsToJson(const QVector<SearchHit> &hits) {
    QJsonArray arr;
    for (const SearchHit &h : hits) {
        QJsonObject o;
        o[QStringLiteral("path")] = h.path;
        o[QStringLiteral("line")] = h.line;
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
QJsonObject scanWorkspace(const QString &root, const QString &query,
                          bool useRegex, const QSet<QString> &skipPaths,
                          const QVector<SearchHit> &bufferHits,
                          int maxResults) {
    QVector<SearchHit> hits = bufferHits;
    bool truncated = false;
    int scanned = 0;
    const QRegularExpression re(useRegex ? query : QString());
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
        if (skipPaths.contains(fi.absoluteFilePath())) continue;
        QFile f(path);
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
            const QString line = QString::fromUtf8(chunk).trimmed();
            const bool matched = useRegex ? re.match(line).hasMatch()
                                          : line.contains(query,
                                                          Qt::CaseInsensitive);
            if (matched)
                hits.append({fi.absoluteFilePath(), lineNo, line});
        }
    }
    if (hits.size() >= maxResults) truncated = true;
    QJsonObject result;
    result[QStringLiteral("results")] = hitsToJson(hits);
    result[QStringLiteral("truncated")] = truncated;
    return result;
}
} // namespace

QString McpBridge::defaultServerName() {
    return SingleInstance::serverName() + QStringLiteral("-mcp");
}

McpBridge::McpBridge(McpEditorHost host, QObject *parent,
                     const QString &serverName)
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
}

McpBridge::~McpBridge() {
    // The card is a child of the HOST widget, not of the bridge — take it
    // down with us so no orphan Approve button outlives its plumbing.
    dismissActiveCard();
}

void McpBridge::setApprovalTimeoutMs(int ms) {
    m_approvalTimeoutMs = qMax(1, ms);
    if (m_approvalTimer && m_approvalTimer->isActive())
        m_approvalTimer->start(m_approvalTimeoutMs);
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
    else if (verb == QLatin1String("set_language"))
        verbSetLanguage(client, id, args);
    else if (verb == QLatin1String("compare_tabs"))
        verbCompareTabs(client, id, args);
    else if (verb == QLatin1String("format_text"))
        verbFormatText(client, id, args);
    else if (verb == QLatin1String("list_notes"))
        verbListNotes(client, id);
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
        tabs.append(t);
    }
    QJsonObject result;
    result[QStringLiteral("tabs")] = tabs;
    sendResult(client, id, result);
}

void McpBridge::verbReadTab(QLocalSocket *client, int id,
                            const QJsonObject &args) {
    const int n = m_host.tabCount ? m_host.tabCount() : 0;
    int idx = -1;
    if (args.contains(QLatin1String("index"))) {
        idx = args.value(QLatin1String("index")).toInt(-1);
    } else if (args.contains(QLatin1String("title"))) {
        const QString title = args.value(QLatin1String("title")).toString();
        for (int i = 0; i < n; ++i) {
            if (m_host.tabTitle && m_host.tabTitle(i) == title) {
                idx = i;
                break;
            }
        }
        if (idx < 0) {
            sendError(client, id,
                      QStringLiteral("no tab titled: %1").arg(title));
            return;
        }
    } else {
        sendError(client, id, QStringLiteral("missing index or title"));
        return;
    }
    if (idx < 0 || idx >= n) {
        sendError(client, id,
                  QStringLiteral("tab index out of range: %1").arg(idx));
        return;
    }
    QString text = m_host.tabText ? m_host.tabText(idx) : QString();
    QJsonObject result;
    result[QStringLiteral("title")] = m_host.tabTitle ? m_host.tabTitle(idx)
                                                      : QString();
    result[QStringLiteral("path")] = m_host.tabPath ? m_host.tabPath(idx)
                                                    : QString();
    if (text.size() > kReadTabCapChars) {
        text.truncate(kReadTabCapChars);
        result[QStringLiteral("truncated")] = true;
    }
    result[QStringLiteral("text")] = text;
    sendResult(client, id, result);
}

void McpBridge::verbGetSelection(QLocalSocket *client, int id) {
    int tabIndex = -1;
    const QString text = m_host.selection ? m_host.selection(&tabIndex)
                                          : QString();
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
    // Optional regex mode. Validate on the GUI thread BEFORE any worker is
    // dispatched — the worker re-compiles the same pattern, so an invalid one
    // must never reach it.
    const bool useRegex = args.value(QLatin1String("regex")).toBool(false);
    QRegularExpression re;
    if (useRegex) {
        re.setPattern(query);
        if (!re.isValid()) {
            sendError(client, id,
                      QStringLiteral("invalid regex: %1").arg(re.errorString()));
            return;
        }
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
        const QString text = m_host.tabText ? m_host.tabText(i) : QString();
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
                const bool matched =
                    useRegex ? re.match(line).hasMatch()
                             : line.contains(query, Qt::CaseInsensitive);
                if (matched)
                    hits.append({hitPath, lineNo, line.trimmed()});
            }
            start = end + 1;
            ++lineNo;
            if (end == text.size()) break;
        }
    }

    const QString root = m_host.workspaceRoot ? m_host.workspaceRoot()
                                              : QString();
    if (root.isEmpty() || !QFileInfo(root).isDir() ||
        hits.size() >= maxResults) {
        QJsonObject result;
        result[QStringLiteral("results")] = hitsToJson(hits);
        result[QStringLiteral("truncated")] = hits.size() >= maxResults;
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
        [root, query, useRegex, openPaths, hits, maxResults]() {
            return scanWorkspace(root, query, useRegex, openPaths, hits,
                                 maxResults);
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
    // Optional regex mode. Compile ONCE, up front — an invalid pattern is a
    // fail-fast error, never a per-line surprise.
    const bool useRegex = args.value(QLatin1String("regex")).toBool(false);
    QRegularExpression re;
    if (useRegex) {
        re.setPattern(query);
        if (!re.isValid()) {
            sendError(client, id,
                      QStringLiteral("invalid regex: %1").arg(re.errorString()));
            return;
        }
    }
    const int n = m_host.tabCount ? m_host.tabCount() : 0;
    int idx = args.contains(QLatin1String("index"))
                  ? args.value(QLatin1String("index")).toInt(-1)
                  : (m_host.currentTabIndex ? m_host.currentTabIndex() : -1);
    if (idx < 0 || idx >= n) {
        sendError(client, id,
                  QStringLiteral("tab index out of range: %1").arg(idx));
        return;
    }
    const QString text = m_host.tabText ? m_host.tabText(idx) : QString();
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
            const bool lineMatch =
                useRegex ? re.match(line).hasMatch()
                         : line.contains(query, Qt::CaseInsensitive);
            if (lineMatch) {
                if (matches.size() >= kMaxFindMatches) {
                    truncated = true;
                    break;
                }
                QJsonObject m;
                m[QStringLiteral("line")] = lineNo;
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
    const int idx = m_host.newTab(text);
    if (idx < 0) {
        sendError(client, id, QStringLiteral("could not create tab"));
        return;
    }
    QJsonObject result;
    result[QStringLiteral("tab_index")] = idx;
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
    const int n = m_host.tabCount ? m_host.tabCount() : 0;
    const int idx = args.contains(QLatin1String("tab_index"))
                        ? args.value(QLatin1String("tab_index")).toInt(-1)
                        : (m_host.currentTabIndex ? m_host.currentTabIndex()
                                                  : -1);
    if (idx < 0 || idx >= n) {
        sendError(client, id,
                  QStringLiteral("tab index out of range: %1").arg(idx));
        return;
    }
    if (!m_host.gotoLine(idx, line)) {
        sendError(client, id, QStringLiteral("could not move cursor"));
        return;
    }
    QJsonObject result;
    result[QStringLiteral("ok")] = true;
    result[QStringLiteral("tab_index")] = idx;
    result[QStringLiteral("line")] = line;
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
    const int n = m_host.tabCount ? m_host.tabCount() : 0;
    const int idx = args.contains(QLatin1String("tab_index"))
                        ? args.value(QLatin1String("tab_index")).toInt(-1)
                        : (m_host.currentTabIndex ? m_host.currentTabIndex()
                                                  : -1);
    if (idx < 0 || idx >= n) {
        sendError(client, id,
                  QStringLiteral("tab index out of range: %1").arg(idx));
        return;
    }
    if (!m_host.setLanguage(idx, lang)) {
        sendError(client, id,
                  QStringLiteral("unknown language: %1").arg(lang));
        return;
    }
    QJsonObject result;
    result[QStringLiteral("ok")] = true;
    result[QStringLiteral("tab_index")] = idx;
    result[QStringLiteral("language")] = lang;
    sendResult(client, id, result);
}

void McpBridge::verbCompareTabs(QLocalSocket *client, int id,
                                const QJsonObject &args) {
    if (!m_host.compareTabs) {
        sendError(client, id,
                  QStringLiteral("compare_tabs not supported by host"));
        return;
    }
    const int n = m_host.tabCount ? m_host.tabCount() : 0;
    const int a = args.value(QLatin1String("index_a")).toInt(-1);
    const int b = args.value(QLatin1String("index_b")).toInt(-1);
    if (a < 0 || a >= n || b < 0 || b >= n) {
        sendError(client, id,
                  QStringLiteral("tab index out of range: %1, %2").arg(a).arg(b));
        return;
    }
    if (a == b) {
        sendError(client, id,
                  QStringLiteral("cannot compare a tab with itself"));
        return;
    }
    if (!m_host.compareTabs(a, b)) {
        sendError(client, id, QStringLiteral("could not open compare view"));
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
void McpBridge::verbListNotes(QLocalSocket *client, int id) {
    QJsonArray notes;
    const QString root = m_host.notesRoot ? m_host.notesRoot() : QString();
    if (!root.isEmpty() && QFileInfo(root).isDir()) {
        NotesStorage storage(root);
        const QString trashPrefix =
            QDir(root).absoluteFilePath(QStringLiteral("Trash")) +
            QLatin1Char('/');
        const QStringList all = storage.listAllNotes(); // mtime desc
        for (const QString &path : all) {
            const QFileInfo fi(path);
            // Exclude the Trash archive and .trashed- strays.
            if (fi.absoluteFilePath().startsWith(trashPrefix)) continue;
            if (fi.fileName().startsWith(QLatin1String(".trashed-"))) continue;
            QJsonObject o;
            o[QStringLiteral("title")] = storage.displayTitleForFile(path);
            o[QStringLiteral("file")] =
                QDir::toNativeSeparators(fi.absoluteFilePath());
            o[QStringLiteral("modified_iso")] =
                fi.lastModified().toString(Qt::ISODate);
            notes.append(o);
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
    const QString rootCanon = QFileInfo(root).canonicalFilePath();
    const QFileInfo fi(fileArg);
    const QString fileCanon = fi.canonicalFilePath();
    if (rootCanon.isEmpty() || fileCanon.isEmpty() || !fi.isFile() ||
        !fileCanon.endsWith(QLatin1String(".html"), Qt::CaseInsensitive) ||
        !fileCanon.startsWith(rootCanon + QLatin1Char('/'))) {
        if (err)
            *err = QStringLiteral("not a note file: %1")
                       .arg(QDir::toNativeSeparators(fileArg));
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
    const int n = m_host.tabCount ? m_host.tabCount() : 0;
    const int idx = args.contains(QLatin1String("tab_index"))
                        ? args.value(QLatin1String("tab_index")).toInt(-1)
                        : (m_host.currentTabIndex ? m_host.currentTabIndex()
                                                  : -1);
    if (idx < 0 || idx >= n) {
        if (err)
            *err = QStringLiteral("tab index out of range: %1").arg(idx);
        return -1;
    }
    return idx;
}

QString McpBridge::tabLabel(int idx) const {
    const QString t = m_host.tabTitle ? m_host.tabTitle(idx) : QString();
    return t.isEmpty() ? QStringLiteral("tab %1").arg(idx) : t;
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
    int line = -1, col = -1;
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
    enqueueApproval(client, id, desc,
                    elideForCard(text, kApprovalPreviewChars),
                    [this, idx, line, col, text](QString *execErr) {
        if (!m_host.insertText(idx, line, col, text)) {
            *execErr = QStringLiteral("could not insert");
            return QJsonObject();
        }
        QJsonObject r;
        r[QStringLiteral("tab_index")] = idx;
        return r;
    });
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
    // Fail fast: never ask the human to approve a no-op.
    if (m_host.hasSelection && !m_host.hasSelection(idx)) {
        sendError(client, id, QStringLiteral("no selection"));
        return;
    }
    const QString desc =
        QStringLiteral("replace the selection in '%1' with %2 chars")
            .arg(tabLabel(idx)).arg(text.size());
    enqueueApproval(client, id, desc,
                    text.isEmpty() ? QStringLiteral("(delete the selection)")
                                   : elideForCard(text, kApprovalPreviewChars),
                    [this, idx, text](QString *execErr) {
        // Re-checked at execute time: the selection may have vanished while
        // the card was waiting for a click.
        if (!m_host.replaceSelection(idx, text)) {
            *execErr = QStringLiteral("no selection");
            return QJsonObject();
        }
        QJsonObject r;
        r[QStringLiteral("tab_index")] = idx;
        return r;
    });
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
    QString err;
    const int idx = resolveWriteTab(args, &err);
    if (idx < 0) {
        sendError(client, id, err);
        return;
    }
    // Literal, case-sensitive pre-check: a no-match request fails fast
    // without burning a human decision.
    const QString text = m_host.tabText ? m_host.tabText(idx) : QString();
    if (!text.contains(find)) {
        sendError(client, id, QStringLiteral("no match"));
        return;
    }
    const QString desc =
        QStringLiteral("replace %1 '%2' with '%3' in '%4'")
            .arg(all ? QStringLiteral("every") : QStringLiteral("the first"),
                 elideForCard(find, kApprovalDescChars),
                 elideForCard(replace, kApprovalDescChars), tabLabel(idx));
    const QString preview =
        elideForCard(find, kApprovalPreviewChars / 2) +
        QStringLiteral("\n-> ") +
        elideForCard(replace, kApprovalPreviewChars / 2);
    enqueueApproval(client, id, desc, preview,
                    [this, idx, find, replace, all](QString *execErr) {
        const int count = m_host.applyEdit(idx, find, replace, all);
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
    });
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
    const QString path = m_host.tabPath ? m_host.tabPath(idx) : QString();
    if (path.isEmpty()) {
        // Untitled tab: this verb NEVER opens a Save As dialog.
        sendError(client, id, QStringLiteral("needs Save As"));
        return;
    }
    const QString desc =
        QStringLiteral("save '%1' to disk").arg(tabLabel(idx));
    enqueueApproval(client, id, desc, QDir::toNativeSeparators(path),
                    [this, idx](QString *execErr) {
        const QString nowPath = m_host.tabPath ? m_host.tabPath(idx)
                                               : QString();
        if (nowPath.isEmpty()) { // became untitled while pending
            *execErr = QStringLiteral("needs Save As");
            return QJsonObject();
        }
        if (!m_host.saveTab(idx)) {
            *execErr = QStringLiteral("could not save: %1")
                           .arg(QDir::toNativeSeparators(nowPath));
            return QJsonObject();
        }
        QJsonObject r;
        r[QStringLiteral("saved")] = true;
        r[QStringLiteral("tab_index")] = idx;
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
void McpBridge::verbValidateNpd(QLocalSocket *client, int id,
                                const QJsonObject &args) {
    QString source;
    if (args.contains(QLatin1String("source"))) {
        source = args.value(QLatin1String("source")).toString();
    } else if (args.contains(QLatin1String("tab_index"))) {
        const int n = m_host.tabCount ? m_host.tabCount() : 0;
        const int idx = args.value(QLatin1String("tab_index")).toInt(-1);
        if (idx < 0 || idx >= n) {
            sendError(client, id,
                      QStringLiteral("tab index out of range: %1").arg(idx));
            return;
        }
        if (!m_host.diagramSource || !m_host.diagramSource(idx, &source)) {
            sendError(client, id,
                      QStringLiteral("tab %1 is not a diagram (.npd) tab")
                          .arg(idx));
            return;
        }
    } else {
        sendError(client, id, QStringLiteral("missing source or tab_index"));
        return;
    }
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
    QJsonObject result;
    result[QStringLiteral("valid")] = d.ok();
    result[QStringLiteral("errors")] = errors;
    sendResult(client, id, result);
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
    QJsonArray rows = r.value(QLatin1String("rows")).toArray();
    bool truncated = r.value(QLatin1String("truncated")).toBool(false);
    if (rows.size() > kMaxSqlRows) { // defensive re-cap in the bridge
        QJsonArray capped;
        for (int i = 0; i < kMaxSqlRows; ++i) capped.append(rows.at(i));
        rows = capped;
        truncated = true;
    }
    // Per-cell char cap (defense in depth): a single cell must not be able to
    // exfiltrate a whole file (e.g. read_text over a huge file that slipped a
    // gate). Truncate each string cell to kMaxSqlCells and mark it.
    for (int ri = 0; ri < rows.size(); ++ri) {
        QJsonArray row = rows.at(ri).toArray();
        bool changed = false;
        for (int ci = 0; ci < row.size(); ++ci) {
            const QString cell = row.at(ci).toString();
            if (cell.size() > kMaxSqlCells) {
                row.replace(ci, cell.left(kMaxSqlCells)
                                    + QChar(0x2026)
                                    + QStringLiteral("[truncated]"));
                changed = true;
            }
        }
        if (changed) {
            rows.replace(ri, row);
            truncated = true;
        }
    }
    QJsonObject result;
    result[QStringLiteral("columns")] = r.value(QLatin1String("columns")).toArray();
    result[QStringLiteral("rows")] = rows;
    result[QStringLiteral("truncated")] = truncated;
    result[QStringLiteral("engine")] = r.value(QLatin1String("engine")).toString();
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
    enqueueApproval(client, id, desc,
                    elideForCard(body, kApprovalPreviewChars),
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
    enqueueApproval(client, id, desc,
                    elideForCard(text, kApprovalPreviewChars),
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
    if (!args.contains(QLatin1String("tab_index"))) {
        sendError(client, id, QStringLiteral("missing tab_index"));
        return;
    }
    const int n = m_host.tabCount ? m_host.tabCount() : 0;
    const int idx = args.value(QLatin1String("tab_index")).toInt(-1);
    if (idx < 0 || idx >= n) {
        sendError(client, id,
                  QStringLiteral("tab index out of range: %1").arg(idx));
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
    if (format != QLatin1String("png") && format != QLatin1String("pdf")) {
        sendError(client, id,
                  QStringLiteral("unsupported format: %1 (png|pdf)").arg(format));
        return;
    }
    const QString path =
        QDir::fromNativeSeparators(args.value(QLatin1String("path")).toString());
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
        QStringLiteral("export '%1' to %2")
            .arg(elideForCard(tabLabel(idx), kApprovalDescChars), shownDest);
    enqueueApproval(client, id, desc, shownDest,
                    [this, idx, path, format](QString *execErr) {
        QString e;
        if (!m_host.exportDiagram(idx, path, format, &e)) {
            *execErr = e.isEmpty() ? QStringLiteral("export failed") : e;
            return QJsonObject();
        }
        QJsonObject r;
        r[QStringLiteral("path")] = QDir::toNativeSeparators(path);
        return r;
    });
}

void McpBridge::enqueueApproval(QLocalSocket *client, int id,
                                const QString &description,
                                const QString &preview,
                                std::function<QJsonObject(QString *)> execute) {
    PendingWrite pw;
    pw.client = client;
    pw.id = id;
    pw.description = description;
    pw.preview = preview;
    pw.execute = std::move(execute);
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
        showApprovalCard(parent, front);
        m_approvalTimer->start(m_approvalTimeoutMs);
        return;
    }
}

// Non-modal banner overlaid on the host widget, styled after the AI panel's
// write-approval card (accent left border, muted preview, Approve/Deny).
// Palette-derived colors so Light, Dark, and Monokai all render sanely.
void McpBridge::showApprovalCard(QWidget *parent, const PendingWrite &pw) {
    dismissActiveCard();
    m_cardParent = parent;
    parent->installEventFilter(this);

    const QPalette pal = parent->palette();
    const QColor baseC = pal.color(QPalette::Base);
    const QColor textC = pal.color(QPalette::Text);
    const QColor accentC = pal.color(QPalette::Highlight);
    const QColor borderC = pal.color(QPalette::Mid);
    const QColor mutedC((baseC.red() + textC.red()) / 2,
                        (baseC.green() + textC.green()) / 2,
                        (baseC.blue() + textC.blue()) / 2);

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
    hdr->setText(QStringLiteral("AI tool (MCP) requests: %1")
                     .arg(pw.description));
    hdr->setStyleSheet(QStringLiteral(
        "color:%1; background:transparent; font-weight:600;")
        .arg(textC.name()));
    lay->addWidget(hdr);

    if (!pw.preview.isEmpty()) {
        auto *prev = new QLabel(card);
        prev->setObjectName(QStringLiteral("mcpApprovalPreview"));
        prev->setTextFormat(Qt::PlainText);
        prev->setWordWrap(true);
        prev->setText(pw.preview);
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
        .arg(accentC.name(), pal.color(QPalette::HighlightedText).name()));
    denyBtn->setStyleSheet(QStringLiteral(
        "QPushButton{background:%1;color:%2;border:1px solid %3;"
        "border-radius:6px;padding:4px 10px;}")
        .arg(pal.color(QPalette::Button).name(),
             pal.color(QPalette::ButtonText).name(), borderC.name()));
    btnRow->addWidget(approveBtn);
    btnRow->addStretch(1);
    btnRow->addWidget(denyBtn);
    lay->addLayout(btnRow);

    connect(approveBtn, &QPushButton::clicked, this,
            [this]() { resolveActiveApproval(true, QString()); });
    connect(denyBtn, &QPushButton::clicked, this, [this]() {
        resolveActiveApproval(false, QStringLiteral("denied by user"));
    });

    m_card = card;
    positionCard();
    card->show();
    card->raise();
}

void McpBridge::resolveActiveApproval(bool approved,
                                      const QString &denyMessage) {
    if (!m_approvalActive || m_approvalQueue.isEmpty()) return;
    m_approvalTimer->stop();
    const PendingWrite pw = m_approvalQueue.takeFirst();
    m_approvalActive = false;
    dismissActiveCard();
    if (pw.client && pw.client->state() == QLocalSocket::ConnectedState) {
        if (approved) {
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
