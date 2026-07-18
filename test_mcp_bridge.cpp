// SPDX-License-Identifier: GPL-3.0-or-later
//
// MCP bridge protocol test. Offscreen-safe, Windows-safe (no POSIX
// headers), no QMessageBox paths. Drives a real QLocalServer round-trip
// against a fake editor host. Widget-hosting (QTEST_MAIN, not guiless):
// the write tier's human-approval card is a real QFrame this test finds
// and clicks programmatically.
//
// Server and client live in the SAME event loop, so every wait is a
// QTest::qWait pump loop with a hard timeout — never a blocking
// waitFor* on the client (that would starve the server side).

#include <QtTest>
#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QFrame>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QLocalSocket>
#include <QPushButton>
#include <QRegularExpression>
#include <QSqlDatabase>
#include <QSqlQuery>
#include <QTemporaryDir>
#include <QWidget>

#include "dbconnections.h"
#include "mcp_bridge.h"

namespace {
constexpr int kWaitMs = 10000;

struct FakeTab {
    QString title;
    QString path;
    QString text;
    bool modified = false;
    QString language = QStringLiteral("Plain Text");
    bool isDiagram = false;   // v0.1.119 — .npd diagram tab
};
} // namespace

class TestMcpBridge : public QObject {
    Q_OBJECT

private:
    QVector<FakeTab> m_fakeTabs;
    QString m_selection;
    int m_currentIndex = 0;
    QString m_root;
    McpBridge *m_bridge = nullptr;
    QString m_name;
    int m_seq = 0;
    // v0.1.118 expansive-wave fake state.
    int m_cursorLine = 1;
    int m_cursorCol = 1;
    QStringList m_recent;
    QString m_notesRoot;
    int m_compareCount = 0;
    int m_lastGotoLine = -1;
    // v0.1.118 write-tier fake state.
    QWidget *m_hostWindow = nullptr;
    QVector<int> m_savedTabs;
    // v0.1.119 depth-wave fake state.
    QJsonArray m_reminders;         // raw reminders the host would return
    bool m_gitFail = false;         // runGit returns an error
    bool m_gitHuge = false;         // runGit returns > 256 KB
    QString m_openedNote;           // last open_note target
    bool m_noterOpened = false;     // open_noter fired
    QString m_lastCreatedTitle;
    QString m_lastCreatedBody;
    QString m_lastCreatedPath;
    QString m_appendedTo;
    QString m_appendedText;
    QString m_reminderFile;
    QDateTime m_reminderDue;
    QString m_exportedPath;
    QString m_exportedFormat;
    int m_sqlRows = 3;              // rows the fake runSql returns for a SELECT
    int m_sqlCellLen = 0;          // when >0, the "name" cell is this many chars
    // ── Phase 2 fake state ──
    QJsonArray m_connections;       // sanitized saved-connection list
    QHash<QString, DbConnections::Record> m_namedRecords; // real records for run_query/list_tables
    bool m_dataAnalystOpened = false;
    bool m_hostHasWebEngine = false; // gated is the default posture
    QJsonObject m_renderedSpec;
    QString m_renderedTitle;
    QString m_exportedChartPath;
    QString m_exportedChartFormat;
    int m_exportedChartScale = 0;

    McpEditorHost makeHost() {
        McpEditorHost h;
        h.tabCount = [this] { return m_fakeTabs.size(); };
        h.tabTitle = [this](int i) { return m_fakeTabs.value(i).title; };
        h.tabPath = [this](int i) { return m_fakeTabs.value(i).path; };
        h.tabModified = [this](int i) { return m_fakeTabs.value(i).modified; };
        h.tabText = [this](int i) { return m_fakeTabs.value(i).text; };
        h.openFile = [this](const QString &p) -> int {
            QFile f(p);
            if (!f.open(QIODevice::ReadOnly)) return -1;
            m_fakeTabs.append({QFileInfo(p).fileName(),
                               QFileInfo(p).absoluteFilePath(),
                               QString::fromUtf8(f.readAll()), false});
            return m_fakeTabs.size() - 1;
        };
        h.selection = [this](int *tabIndex) {
            if (tabIndex) *tabIndex = m_currentIndex;
            return m_selection;
        };
        h.workspaceRoot = [this] { return m_root; };
        // ── v0.1.118 expansive wave ──
        h.currentTabIndex = [this] { return m_currentIndex; };
        h.tabLanguage = [this](int i) { return m_fakeTabs.value(i).language; };
        h.tabEncoding = [this](int i) {
            return i >= 0 && i < m_fakeTabs.size() ? QStringLiteral("UTF-8")
                                                   : QString();
        };
        h.cursorPosition = [this](int *l, int *c) {
            if (l) *l = m_cursorLine;
            if (c) *c = m_cursorCol;
        };
        h.recentFiles = [this] { return m_recent; };
        h.newTab = [this](const QString &text) {
            m_fakeTabs.append({QStringLiteral("new %1")
                                   .arg(m_fakeTabs.size() + 1),
                               QString(), text, !text.isEmpty()});
            m_currentIndex = m_fakeTabs.size() - 1;
            return m_currentIndex;
        };
        h.gotoLine = [this](int idx, int line) {
            if (idx < 0 || idx >= m_fakeTabs.size() || line < 1) return false;
            m_currentIndex = idx;
            m_lastGotoLine = line;
            return true;
        };
        h.setLanguage = [this](int idx, const QString &lang) {
            static const QStringList known = {
                QStringLiteral("Plain Text"), QStringLiteral("Python"),
                QStringLiteral("JSON"), QStringLiteral("C++"),
                QStringLiteral("SQL")};
            if (idx < 0 || idx >= m_fakeTabs.size() || !known.contains(lang))
                return false;
            m_fakeTabs[idx].language = lang;
            return true;
        };
        h.compareTabs = [this](int a, int b) {
            if (a < 0 || b < 0 || a >= m_fakeTabs.size() ||
                b >= m_fakeTabs.size() || a == b)
                return false;
            ++m_compareCount;
            return true;
        };
        h.formatText = [](const QString &kind, const QString &text,
                          QString *err) -> QString {
            if (kind == QLatin1String("json")) {
                if (!text.trimmed().startsWith(QLatin1Char('{'))) {
                    if (err) *err = QStringLiteral("invalid json: expected object");
                    return QString();
                }
                return QStringLiteral("FMT-JSON:") + text;
            }
            return kind.toUpper() + QLatin1Char(':') + text;
        };
        h.notesRoot = [this] { return m_notesRoot; };
        // ── Phase 0A ──
        h.knownLanguages = [] {
            return QStringList{QStringLiteral("Plain Text"),
                               QStringLiteral("Python"), QStringLiteral("JSON"),
                               QStringLiteral("C++"), QStringLiteral("SQL")};
        };
        h.resolveLanguage = [](const QString &in) -> QString {
            static const QStringList known = {
                QStringLiteral("Plain Text"), QStringLiteral("Python"),
                QStringLiteral("JSON"), QStringLiteral("C++"),
                QStringLiteral("SQL")};
            for (const QString &k : known)
                if (k.compare(in, Qt::CaseInsensitive) == 0) return k;
            if (in.compare(QStringLiteral("py"), Qt::CaseInsensitive) == 0)
                return QStringLiteral("Python");
            return QString();
        };
        // ── v0.1.118 write tier ──
        h.approvalParent = [this]() -> QWidget * { return m_hostWindow; };
        h.hasSelection = [this](int) { return !m_selection.isEmpty(); };
        h.insertText = [this](int idx, int line, int col,
                              const QString &text) {
            if (idx < 0 || idx >= m_fakeTabs.size()) return false;
            QString &buf = m_fakeTabs[idx].text;
            int pos = buf.size(); // fake cursor: end of buffer
            if (line >= 1) {
                int cur = 1, off = 0;
                while (cur < line) {
                    const int nl = buf.indexOf(QLatin1Char('\n'), off);
                    if (nl < 0) return false; // line out of range
                    off = nl + 1;
                    ++cur;
                }
                int lineEnd = buf.indexOf(QLatin1Char('\n'), off);
                if (lineEnd < 0) lineEnd = buf.size();
                pos = qMin(off + (col - 1), lineEnd);
            }
            buf.insert(pos, text);
            m_fakeTabs[idx].modified = true;
            return true;
        };
        h.replaceSelection = [this](int idx, const QString &text) {
            if (idx < 0 || idx >= m_fakeTabs.size() || m_selection.isEmpty())
                return false;
            QString &buf = m_fakeTabs[idx].text;
            const int at = buf.indexOf(m_selection);
            if (at < 0) return false;
            buf.replace(at, m_selection.size(), text);
            m_selection.clear();
            return true;
        };
        h.applyEdit = [this](int idx, const QString &find,
                             const QString &repl, bool all) {
            if (idx < 0 || idx >= m_fakeTabs.size()) return -1;
            QString &buf = m_fakeTabs[idx].text;
            int count = 0, from = 0;
            while (true) {
                const int at = buf.indexOf(find, from);
                if (at < 0) break;
                buf.replace(at, find.size(), repl);
                ++count;
                from = at + repl.size();
                if (!all) break;
            }
            return count;
        };
        h.saveTab = [this](int idx) {
            if (idx < 0 || idx >= m_fakeTabs.size() ||
                m_fakeTabs[idx].path.isEmpty())
                return false;
            m_savedTabs.append(idx);
            m_fakeTabs[idx].modified = false;
            return true;
        };
        // ── v0.1.119 depth wave ──
        h.reminders = [this] { return m_reminders; };
        h.runGit = [this](const QString &sub, const QJsonObject &args,
                          QString *err) -> QString {
            if (m_gitFail) {
                if (err) *err = QStringLiteral("not_a_repo: not a git checkout");
                return QString();
            }
            if (m_gitHuge)
                return QString(300 * 1024, QLatin1Char('x'));
            // Echo the fixed subcommand + the args the bridge forwarded so a
            // test can prove path/limit/ref reach the git layer verbatim.
            return QStringLiteral("git-%1|path=%2|limit=%3|ref=%4")
                .arg(sub,
                     args.value(QLatin1String("path")).toString(),
                     QString::number(
                         args.value(QLatin1String("limit")).toInt(-1)),
                     args.value(QLatin1String("ref")).toString());
        };
        h.diagramSource = [this](int i, QString *out) -> bool {
            if (i < 0 || i >= m_fakeTabs.size() || !m_fakeTabs[i].isDiagram)
                return false;
            if (out) *out = m_fakeTabs[i].text;
            return true;
        };
        h.runSql = [this](const QString &sql, const QString &csvPath,
                          QString *err) -> QJsonObject {
            // Faithful-enough stand-in for DbConnections::classifySql: reject
            // any statement carrying a DML/DDL verb (this is what catches
            // UPDATE / DELETE and the WITH ... DELETE trap), accept the rest.
            static const QRegularExpression dml(
                QStringLiteral("\\b(insert|update|delete|drop|create|alter|"
                               "replace|truncate|attach|copy)\\b"),
                QRegularExpression::CaseInsensitiveOption);
            if (dml.match(sql).hasMatch()) {
                if (err)
                    *err = QStringLiteral(
                        "query rejected: statement is not read-only");
                return QJsonObject();
            }
            QJsonArray cols;
            cols.append(QStringLiteral("id"));
            cols.append(QStringLiteral("name"));
            QJsonArray rows;
            for (int i = 0; i < m_sqlRows; ++i) {
                QJsonArray r;
                r.append(QString::number(i));
                r.append(m_sqlCellLen > 0
                             ? QString(m_sqlCellLen, QLatin1Char('X'))
                             : QStringLiteral("row%1").arg(i));
                rows.append(r);
            }
            QJsonObject out;
            out[QStringLiteral("columns")] = cols;
            out[QStringLiteral("rows")] = rows;
            out[QStringLiteral("truncated")] = false;
            out[QStringLiteral("engine")] =
                csvPath.isEmpty() ? QStringLiteral("sqlite")
                                  : QStringLiteral("duckdb");
            return out;
        };
        h.openNote = [this](const QString &absFile, QString *) -> QString {
            m_openedNote = absFile;
            return QFileInfo(absFile).completeBaseName();
        };
        h.createNote = [this](const QString &title, const QString &body,
                              QString *) -> QString {
            m_lastCreatedTitle = title;
            m_lastCreatedBody = body;
            m_lastCreatedPath = m_notesRoot +
                QStringLiteral("/Inbox/created-%1.html").arg(title);
            return m_lastCreatedPath;
        };
        h.appendNote = [this](const QString &absFile, const QString &text,
                              QString *) -> bool {
            m_appendedTo = absFile;
            m_appendedText = text;
            return true;
        };
        h.setReminder = [this](const QString &absFile, const QDateTime &due,
                               QString *) -> bool {
            m_reminderFile = absFile;
            m_reminderDue = due;
            return true;
        };
        h.exportDiagram = [this](int, const QString &path,
                                 const QString &format, QString *) -> bool {
            m_exportedPath = path;
            m_exportedFormat = format;
            return true;
        };
        // ── Phase 1 ──
        h.createDiagram = [this](const QString &src, const QString &title) {
            m_fakeTabs.append({title.isEmpty() ? QStringLiteral("Diagram")
                                               : title,
                               QString(), src, false,
                               QStringLiteral("Plain Text"), true});
            m_currentIndex = m_fakeTabs.size() - 1;
            return m_currentIndex;
        };
        h.setDiagramSource = [this](int i, const QString &src) {
            if (i < 0 || i >= m_fakeTabs.size() || !m_fakeTabs[i].isDiagram)
                return false;
            m_fakeTabs[i].text = src;
            return true;
        };
        h.openNoter = [this]() {
            m_noterOpened = true;
            return true;
        };
        // ── Phase 2 ──
        h.listConnections = [this] { return m_connections; };
        h.classifySqlReadOnly = [](const QString &sql, QString *reason) {
            const auto v = DbConnections::classifySql(sql, /*restrict=*/true);
            if (v.singleStatement && v.readOnly) return true;
            if (reason) *reason = v.reason;
            return false;
        };
        h.runNamedQuery = [this](const QString &name, const QString &sql,
                                 int maxRows, QString *err) -> QJsonObject {
            const auto v = DbConnections::classifySql(sql, /*restrict=*/true);
            if (!(v.singleStatement && v.readOnly)) {
                if (err)
                    *err = v.reason.isEmpty()
                               ? QStringLiteral("query is not read-only (SELECT only)")
                               : QStringLiteral("query rejected: %1").arg(v.reason);
                return QJsonObject();
            }
            if (!m_namedRecords.contains(name)) {
                if (err) *err = QStringLiteral("no connection named: %1").arg(name);
                return QJsonObject();
            }
            const DbConnections::Record rec = m_namedRecords.value(name);
            const DbConnections::QueryResult qr =
                DbConnections::runQuery(rec, sql, maxRows, false, nullptr);
            if (!qr.ok) {
                if (err)
                    *err = qr.error.isEmpty() ? QStringLiteral("query failed")
                                              : qr.error;
                return QJsonObject();
            }
            QJsonArray cols;
            for (const QString &c : qr.columns) cols.append(c);
            QJsonArray rows;
            for (const QVector<QString> &row : qr.rows) {
                QJsonArray jr;
                for (const QString &cell : row) jr.append(cell);
                rows.append(jr);
            }
            QJsonObject out;
            out[QStringLiteral("columns")] = cols;
            out[QStringLiteral("rows")] = rows;
            out[QStringLiteral("truncated")] = qr.truncated;
            out[QStringLiteral("engine")] =
                rec.driver == QLatin1String("QSQLITE") ? QStringLiteral("sqlite")
                                                       : QStringLiteral("odbc");
            return out;
        };
        h.listTables = [this](const QString &name, QString *err) -> QJsonArray {
            if (!m_namedRecords.contains(name)) {
                if (err) *err = QStringLiteral("no connection named: %1").arg(name);
                return QJsonArray();
            }
            bool ok = true;
            const QStringList tables =
                DbConnections::listTables(m_namedRecords.value(name), &ok);
            if (!ok) {
                if (err) *err = QStringLiteral("could not connect to: %1").arg(name);
                return QJsonArray();
            }
            QJsonArray out;
            for (const QString &t : tables) out.append(t);
            return out;
        };
        h.openDataAnalyst = [this] {
            m_dataAnalystOpened = true;
            return true;
        };
        // Charts fields are set ONLY when the fake reports WebEngine — the
        // unset-field path IS the friendly gate the bridge answers pre-card.
        if (m_hostHasWebEngine) {
            h.renderChart = [this](const QJsonObject &spec, const QString &title,
                                   QString *) -> QJsonObject {
                m_renderedSpec = spec;
                m_renderedTitle = title;
                return QJsonObject{{QStringLiteral("chart_id"),
                                    QStringLiteral("test-chart-1")},
                                   {QStringLiteral("rendered"), true}};
            };
            h.exportChart = [this](const QJsonObject &, const QString &path,
                                   const QString &format, int scale,
                                   QString *err) -> bool {
                m_exportedChartPath = path;
                m_exportedChartFormat = format;
                m_exportedChartScale = scale;
                QFile f(path);
                if (!f.open(QIODevice::WriteOnly)) {
                    if (err) *err = QStringLiteral("could not write");
                    return false;
                }
                f.write("FAKECHART");
                f.close();
                return true;
            };
        }
        return h;
    }

    // Register a real QSQLITE connection record backed by a temp sqlite file
    // with a table `t(id INTEGER, name TEXT)` pre-filled with `rows` rows.
    QString makeSqliteFixture(QTemporaryDir &dir, const QString &connName,
                              int rows) {
        const QString dbPath = dir.path() + QStringLiteral("/") + connName +
                               QStringLiteral(".db");
        {
            QSqlDatabase db =
                QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"),
                                          QStringLiteral("fixture-") + connName);
            db.setDatabaseName(dbPath);
            if (db.open()) {
                QSqlQuery q(db);
                q.exec(QStringLiteral("CREATE TABLE t(id INTEGER, name TEXT)"));
                for (int i = 0; i < rows; ++i)
                    q.exec(QStringLiteral("INSERT INTO t VALUES(%1, 'name%1')")
                               .arg(i));
                db.close();
            }
        }
        QSqlDatabase::removeDatabase(QStringLiteral("fixture-") + connName);
        DbConnections::Record rec;
        rec.name = connName;
        rec.driver = QStringLiteral("QSQLITE");
        rec.database = dbPath;
        m_namedRecords.insert(connName, rec);
        return dbPath;
    }

    // Rebuild the bridge with a host that reports WebEngine (charts enabled).
    void useWebEngineHost() {
        delete m_bridge;
        m_hostHasWebEngine = true;
        m_bridge = new McpBridge(makeHost(), this, m_name);
        QVERIFY(m_bridge->isListening());
    }

    // Write a minimal Noter-convention note (notepatra-title meta wins the
    // title resolution). Returns the absolute path.
    QString writeNote(const QString &absPath, const QString &title,
                      const QString &bodyText) {
        QDir().mkpath(QFileInfo(absPath).absolutePath());
        QFile f(absPath);
        if (!f.open(QIODevice::WriteOnly)) return QString();
        const QString html =
            QStringLiteral("<html><head><meta name=\"notepatra-title\" "
                           "content=\"%1\"></head><body><h1>%1</h1>"
                           "<p>%2</p></body></html>")
                .arg(title, bodyText);
        f.write(html.toUtf8());
        f.close();
        return QFileInfo(absPath).absoluteFilePath();
    }

    bool connectClient(QLocalSocket &s) {
        s.connectToServer(m_name);
        QElapsedTimer t;
        t.start();
        while (s.state() != QLocalSocket::ConnectedState &&
               t.elapsed() < kWaitMs)
            QTest::qWait(10);
        return s.state() == QLocalSocket::ConnectedState;
    }

    QByteArray readLine(QLocalSocket &s, int timeoutMs = kWaitMs) {
        QElapsedTimer t;
        t.start();
        while (!s.canReadLine() && t.elapsed() < timeoutMs)
            QTest::qWait(10);
        return s.canReadLine() ? s.readLine().trimmed() : QByteArray();
    }

    QJsonObject readObj(QLocalSocket &s, int timeoutMs = kWaitMs) {
        const QByteArray line = readLine(s, timeoutMs);
        return QJsonDocument::fromJson(line).object();
    }

    // Reads and validates the greeting line. Returns it for extra checks.
    QJsonObject readGreeting(QLocalSocket &s) { return readObj(s); }

    void sendLine(QLocalSocket &s, const QByteArray &line) {
        s.write(line + '\n');
        s.flush();
    }

    QJsonObject call(QLocalSocket &s, int id, const QString &verb,
                     const QJsonObject &args = QJsonObject()) {
        QJsonObject req;
        req[QStringLiteral("id")] = id;
        req[QStringLiteral("verb")] = verb;
        req[QStringLiteral("args")] = args;
        sendLine(s, QJsonDocument(req).toJson(QJsonDocument::Compact));
        return readObj(s);
    }

    // ── v0.1.118 write-tier helpers ──

    // Fire-and-forget request: write verbs don't answer until the human
    // decides, so the caller reads the response separately.
    void sendRequest(QLocalSocket &s, int id, const QString &verb,
                     const QJsonObject &args = QJsonObject()) {
        QJsonObject req;
        req[QStringLiteral("id")] = id;
        req[QStringLiteral("verb")] = verb;
        req[QStringLiteral("args")] = args;
        sendLine(s, QJsonDocument(req).toJson(QJsonDocument::Compact));
    }

    QFrame *findCard() const {
        return m_hostWindow
                   ? m_hostWindow->findChild<QFrame *>(
                         QStringLiteral("mcpApprovalCard"))
                   : nullptr;
    }

    QFrame *waitForCard(int timeoutMs = kWaitMs) {
        QElapsedTimer t;
        t.start();
        while (t.elapsed() < timeoutMs) {
            if (QFrame *c = findCard()) return c;
            QTest::qWait(10);
        }
        return nullptr;
    }

    // Waits for a card whose description label contains `needle` — needed
    // when one card replaces another and a plain findChild could still see
    // the deleteLater'd predecessor.
    QFrame *waitForCardContaining(const QString &needle,
                                  int timeoutMs = kWaitMs) {
        QElapsedTimer t;
        t.start();
        while (t.elapsed() < timeoutMs) {
            const auto cards = m_hostWindow->findChildren<QFrame *>(
                QStringLiteral("mcpApprovalCard"));
            for (QFrame *c : cards) {
                auto *d = c->findChild<QLabel *>(
                    QStringLiteral("mcpApprovalDesc"));
                if (d && d->text().contains(needle)) return c;
            }
            QTest::qWait(10);
        }
        return nullptr;
    }

    bool waitForCardGone(int timeoutMs = kWaitMs) {
        QElapsedTimer t;
        t.start();
        while (t.elapsed() < timeoutMs) {
            const auto cards = m_hostWindow->findChildren<QFrame *>(
                QStringLiteral("mcpApprovalCard"));
            bool anyVisible = false;
            for (QFrame *c : cards)
                if (c->isVisible()) anyVisible = true;
            if (!anyVisible) return true;
            QTest::qWait(10);
        }
        return false;
    }

private slots:
    void init() {
        m_fakeTabs = {
            {QStringLiteral("notes.txt"), QStringLiteral("/fake/notes.txt"),
             QStringLiteral("hello world\nSecond Line with Needle\n"), false},
            {QStringLiteral("Untitled 1"), QString(),
             QStringLiteral("alpha\nbeta NEEDLE gamma\n"), true},
        };
        m_selection.clear();
        m_currentIndex = 0;
        m_root.clear();
        m_cursorLine = 1;
        m_cursorCol = 1;
        m_recent.clear();
        m_notesRoot.clear();
        m_compareCount = 0;
        m_lastGotoLine = -1;
        m_savedTabs.clear();
        m_reminders = QJsonArray();
        m_gitFail = false;
        m_gitHuge = false;
        m_openedNote.clear();
        m_noterOpened = false;
        m_lastCreatedTitle.clear();
        m_lastCreatedBody.clear();
        m_lastCreatedPath.clear();
        m_appendedTo.clear();
        m_appendedText.clear();
        m_reminderFile.clear();
        m_reminderDue = QDateTime();
        m_exportedPath.clear();
        m_exportedFormat.clear();
        m_sqlRows = 3;
        m_sqlCellLen = 0;
        m_connections = QJsonArray();
        m_namedRecords.clear();
        m_dataAnalystOpened = false;
        m_hostHasWebEngine = false;
        m_renderedSpec = QJsonObject();
        m_renderedTitle.clear();
        m_exportedChartPath.clear();
        m_exportedChartFormat.clear();
        m_exportedChartScale = 0;
        m_hostWindow = new QWidget;
        m_hostWindow->resize(640, 420);
        m_hostWindow->show();
        m_name = QStringLiteral("notepatra-mcp-selftest-%1-%2")
                     .arg(QCoreApplication::applicationPid())
                     .arg(++m_seq);
        m_bridge = new McpBridge(makeHost(), this, m_name);
        QVERIFY(m_bridge->isListening());
    }

    void cleanup() {
        delete m_bridge;
        m_bridge = nullptr;
        delete m_hostWindow;
        m_hostWindow = nullptr;
    }

    void greeting_arrives_first() {
        QLocalSocket s;
        QVERIFY(connectClient(s));
        // Fire a request immediately, BEFORE reading anything: the greeting
        // must still be the first line on the wire (proof-of-life law).
        sendLine(s, QByteArray(
            R"({"id":1,"verb":"list_open_tabs","args":{}})"));
        const QJsonObject greet = readObj(s);
        QCOMPARE(greet.value(QLatin1String("notepatra_mcp")).toInt(), 1);
        QCOMPARE(greet.value(QLatin1String("app")).toString(),
                 QStringLiteral("Notepatra"));
        QVERIFY(!greet.value(QLatin1String("version")).toString().isEmpty());
        const QJsonObject resp = readObj(s);
        QCOMPARE(resp.value(QLatin1String("id")).toInt(), 1);
        QVERIFY(resp.value(QLatin1String("ok")).toBool());
    }

    void list_open_tabs_shape() {
        QLocalSocket s;
        QVERIFY(connectClient(s));
        readGreeting(s);
        const QJsonObject resp = call(s, 2, QStringLiteral("list_open_tabs"));
        QVERIFY(resp.value(QLatin1String("ok")).toBool());
        const QJsonArray tabs = resp.value(QLatin1String("result"))
                                    .toObject()
                                    .value(QLatin1String("tabs"))
                                    .toArray();
        QCOMPARE(tabs.size(), 2);
        const QJsonObject t0 = tabs.at(0).toObject();
        QCOMPARE(t0.value(QLatin1String("index")).toInt(), 0);
        QCOMPARE(t0.value(QLatin1String("title")).toString(),
                 QStringLiteral("notes.txt"));
        QCOMPARE(t0.value(QLatin1String("path")).toString(),
                 QStringLiteral("/fake/notes.txt"));
        QCOMPARE(t0.value(QLatin1String("modified")).toBool(), false);
        const QJsonObject t1 = tabs.at(1).toObject();
        QCOMPARE(t1.value(QLatin1String("index")).toInt(), 1);
        QCOMPARE(t1.value(QLatin1String("path")).toString(), QString());
        QCOMPARE(t1.value(QLatin1String("modified")).toBool(), true);
    }

    void read_tab_by_index() {
        QLocalSocket s;
        QVERIFY(connectClient(s));
        readGreeting(s);
        QJsonObject args;
        args[QStringLiteral("index")] = 0;
        const QJsonObject resp = call(s, 3, QStringLiteral("read_tab"), args);
        QVERIFY(resp.value(QLatin1String("ok")).toBool());
        const QJsonObject r = resp.value(QLatin1String("result")).toObject();
        QCOMPARE(r.value(QLatin1String("title")).toString(),
                 QStringLiteral("notes.txt"));
        QCOMPARE(r.value(QLatin1String("path")).toString(),
                 QStringLiteral("/fake/notes.txt"));
        QCOMPARE(r.value(QLatin1String("text")).toString(),
                 m_fakeTabs[0].text);
        QVERIFY(!r.contains(QLatin1String("truncated")));
    }

    void read_tab_by_title() {
        QLocalSocket s;
        QVERIFY(connectClient(s));
        readGreeting(s);
        QJsonObject args;
        args[QStringLiteral("title")] = QStringLiteral("Untitled 1");
        const QJsonObject resp = call(s, 4, QStringLiteral("read_tab"), args);
        QVERIFY(resp.value(QLatin1String("ok")).toBool());
        const QJsonObject r = resp.value(QLatin1String("result")).toObject();
        QCOMPARE(r.value(QLatin1String("text")).toString(),
                 m_fakeTabs[1].text);
        QCOMPARE(r.value(QLatin1String("path")).toString(), QString());
    }

    void read_tab_not_found() {
        QLocalSocket s;
        QVERIFY(connectClient(s));
        readGreeting(s);
        QJsonObject byIndex;
        byIndex[QStringLiteral("index")] = 99;
        QJsonObject r1 = call(s, 5, QStringLiteral("read_tab"), byIndex);
        QCOMPARE(r1.value(QLatin1String("ok")).toBool(), false);
        QVERIFY(!r1.value(QLatin1String("error")).toString().isEmpty());
        QJsonObject byTitle;
        byTitle[QStringLiteral("title")] = QStringLiteral("no such tab");
        QJsonObject r2 = call(s, 6, QStringLiteral("read_tab"), byTitle);
        QCOMPARE(r2.value(QLatin1String("ok")).toBool(), false);
        QJsonObject r3 = call(s, 7, QStringLiteral("read_tab"));
        QCOMPARE(r3.value(QLatin1String("ok")).toBool(), false);
    }

    void read_tab_truncates_at_5mb() {
        const int cap = 5 * 1024 * 1024;
        m_fakeTabs[1].text = QString(cap + 4096, QLatin1Char('x'));
        QLocalSocket s;
        QVERIFY(connectClient(s));
        readGreeting(s);
        QJsonObject args;
        args[QStringLiteral("index")] = 1;
        const QJsonObject resp = call(s, 8, QStringLiteral("read_tab"), args);
        QVERIFY(resp.value(QLatin1String("ok")).toBool());
        const QJsonObject r = resp.value(QLatin1String("result")).toObject();
        QCOMPARE(r.value(QLatin1String("truncated")).toBool(), true);
        QCOMPARE(r.value(QLatin1String("text")).toString().size(), cap);
    }

    void open_file_roundtrip() {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString path = dir.path() + QStringLiteral("/opened.txt");
        {
            QFile f(path);
            QVERIFY(f.open(QIODevice::WriteOnly));
            f.write("hello from disk\n");
        }
        QLocalSocket s;
        QVERIFY(connectClient(s));
        readGreeting(s);
        QJsonObject args;
        args[QStringLiteral("path")] = path;
        const QJsonObject resp = call(s, 9, QStringLiteral("open_file"), args);
        QVERIFY(resp.value(QLatin1String("ok")).toBool());
        const QJsonObject r = resp.value(QLatin1String("result")).toObject();
        QCOMPARE(r.value(QLatin1String("opened")).toBool(), true);
        QCOMPARE(r.value(QLatin1String("tab_index")).toInt(), 2);
        // The new tab is visible to list_open_tabs and readable.
        QJsonObject readArgs;
        readArgs[QStringLiteral("index")] = 2;
        const QJsonObject rd = call(s, 10, QStringLiteral("read_tab"),
                                    readArgs);
        QVERIFY(rd.value(QLatin1String("ok")).toBool());
        QCOMPARE(rd.value(QLatin1String("result"))
                     .toObject()
                     .value(QLatin1String("text"))
                     .toString(),
                 QStringLiteral("hello from disk\n"));
    }

    void open_file_nonexistent_fails() {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        QLocalSocket s;
        QVERIFY(connectClient(s));
        readGreeting(s);
        QJsonObject args;
        args[QStringLiteral("path")] = dir.path() + QStringLiteral("/nope.txt");
        const QJsonObject resp = call(s, 11, QStringLiteral("open_file"),
                                      args);
        QCOMPARE(resp.value(QLatin1String("ok")).toBool(), false);
        QVERIFY(!resp.value(QLatin1String("error")).toString().isEmpty());
        // No tab was appended.
        QCOMPARE(m_fakeTabs.size(), 2);
    }

    void get_selection_roundtrip() {
        m_selection = QStringLiteral("picked text");
        m_currentIndex = 1;
        QLocalSocket s;
        QVERIFY(connectClient(s));
        readGreeting(s);
        QJsonObject resp = call(s, 12, QStringLiteral("get_selection"));
        QVERIFY(resp.value(QLatin1String("ok")).toBool());
        QJsonObject r = resp.value(QLatin1String("result")).toObject();
        QCOMPARE(r.value(QLatin1String("text")).toString(),
                 QStringLiteral("picked text"));
        QCOMPARE(r.value(QLatin1String("tab_index")).toInt(), 1);
        // Empty selection is ok:true with an empty string.
        m_selection.clear();
        resp = call(s, 13, QStringLiteral("get_selection"));
        QVERIFY(resp.value(QLatin1String("ok")).toBool());
        QCOMPARE(resp.value(QLatin1String("result"))
                     .toObject()
                     .value(QLatin1String("text"))
                     .toString(),
                 QString());
    }

    void unknown_verb_fails() {
        QLocalSocket s;
        QVERIFY(connectClient(s));
        readGreeting(s);
        QJsonObject resp = call(s, 14, QStringLiteral("delete_everything"));
        QCOMPARE(resp.value(QLatin1String("id")).toInt(), 14);
        QCOMPARE(resp.value(QLatin1String("ok")).toBool(), false);
        QVERIFY(resp.value(QLatin1String("error"))
                    .toString()
                    .contains(QLatin1String("unknown verb")));
    }

    void malformed_line_fails() {
        QLocalSocket s;
        QVERIFY(connectClient(s));
        readGreeting(s);
        sendLine(s, QByteArrayLiteral("this is not json"));
        QJsonObject resp = readObj(s);
        QCOMPARE(resp.value(QLatin1String("id")).toInt(), -1);
        QCOMPARE(resp.value(QLatin1String("ok")).toBool(), false);
        QCOMPARE(resp.value(QLatin1String("error")).toString(),
                 QStringLiteral("parse error"));
        // The connection survives a bad line: a valid request still works.
        QJsonObject after = call(s, 15, QStringLiteral("list_open_tabs"));
        QVERIFY(after.value(QLatin1String("ok")).toBool());
    }

    void search_buffers_case_insensitive() {
        QLocalSocket s;
        QVERIFY(connectClient(s));
        readGreeting(s);
        QJsonObject args;
        args[QStringLiteral("query")] = QStringLiteral("needle");
        const QJsonObject resp = call(s, 16, QStringLiteral("search_project"),
                                      args);
        QVERIFY(resp.value(QLatin1String("ok")).toBool());
        const QJsonObject r = resp.value(QLatin1String("result")).toObject();
        const QJsonArray results = r.value(QLatin1String("results")).toArray();
        QCOMPARE(results.size(), 2); // "Needle" in tab0 + "NEEDLE" in tab1
        QCOMPARE(results.at(0).toObject().value(QLatin1String("line")).toInt(),
                 2);
        QCOMPARE(r.value(QLatin1String("truncated")).toBool(), false);
    }

    void search_workspace_async() {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        {
            QFile f(dir.path() + QStringLiteral("/a.txt"));
            QVERIFY(f.open(QIODevice::WriteOnly));
            f.write("one\ntwo needle three\n");
        }
        {
            QFile f(dir.path() + QStringLiteral("/blob.bin"));
            QVERIFY(f.open(QIODevice::WriteOnly));
            f.write(QByteArray("\0\0needle\0\0", 10)); // binary-looking: skip
        }
        m_root = dir.path();
        QLocalSocket s;
        QVERIFY(connectClient(s));
        readGreeting(s);
        QJsonObject args;
        args[QStringLiteral("query")] = QStringLiteral("needle");
        const QJsonObject resp = call(s, 17, QStringLiteral("search_project"),
                                      args);
        QVERIFY(resp.value(QLatin1String("ok")).toBool());
        const QJsonArray results = resp.value(QLatin1String("result"))
                                       .toObject()
                                       .value(QLatin1String("results"))
                                       .toArray();
        // 2 buffer hits + 1 file hit; the binary file contributes nothing.
        QCOMPARE(results.size(), 3);
        bool sawFileHit = false;
        for (const QJsonValue &v : results) {
            const QJsonObject hit = v.toObject();
            const QString p = hit.value(QLatin1String("path")).toString();
            QVERIFY(!p.endsWith(QLatin1String(".bin")));
            if (p.endsWith(QLatin1String("a.txt"))) {
                sawFileHit = true;
                QCOMPARE(hit.value(QLatin1String("line")).toInt(), 2);
                QCOMPARE(hit.value(QLatin1String("text")).toString(),
                         QStringLiteral("two needle three"));
            }
        }
        QVERIFY(sawFileHit);
    }

    void search_respects_max_results() {
        QLocalSocket s;
        QVERIFY(connectClient(s));
        readGreeting(s);
        QJsonObject args;
        args[QStringLiteral("query")] = QStringLiteral("e"); // matches a lot
        args[QStringLiteral("max_results")] = 1;
        const QJsonObject resp = call(s, 18, QStringLiteral("search_project"),
                                      args);
        QVERIFY(resp.value(QLatin1String("ok")).toBool());
        const QJsonObject r = resp.value(QLatin1String("result")).toObject();
        QCOMPARE(r.value(QLatin1String("results")).toArray().size(), 1);
        QCOMPARE(r.value(QLatin1String("truncated")).toBool(), true);
    }

    // ── v0.1.118 expansive wave ──────────────────────────────────────

    void get_status_shape() {
        m_currentIndex = 0;
        m_cursorLine = 2;
        m_cursorCol = 5;
        QLocalSocket s;
        QVERIFY(connectClient(s));
        readGreeting(s);
        const QJsonObject resp = call(s, 20, QStringLiteral("get_status"));
        QVERIFY(resp.value(QLatin1String("ok")).toBool());
        const QJsonObject r = resp.value(QLatin1String("result")).toObject();
        QCOMPARE(r.value(QLatin1String("tab_index")).toInt(), 0);
        QCOMPARE(r.value(QLatin1String("title")).toString(),
                 QStringLiteral("notes.txt"));
        QCOMPARE(r.value(QLatin1String("path")).toString(),
                 QStringLiteral("/fake/notes.txt"));
        QCOMPARE(r.value(QLatin1String("language")).toString(),
                 QStringLiteral("Plain Text"));
        QCOMPARE(r.value(QLatin1String("encoding")).toString(),
                 QStringLiteral("UTF-8"));
        QCOMPARE(r.value(QLatin1String("cursor_line")).toInt(), 2);
        QCOMPARE(r.value(QLatin1String("cursor_col")).toInt(), 5);
        const QString edition = r.value(QLatin1String("edition")).toString();
        QVERIFY(edition == QLatin1String("Full") ||
                edition == QLatin1String("Lite"));
        QVERIFY(!r.value(QLatin1String("version")).toString().isEmpty());
        // No tabs at all → tab_index -1 with empty per-tab fields, still ok.
        m_fakeTabs.clear();
        const QJsonObject resp2 = call(s, 21, QStringLiteral("get_status"));
        QVERIFY(resp2.value(QLatin1String("ok")).toBool());
        const QJsonObject r2 = resp2.value(QLatin1String("result")).toObject();
        QCOMPARE(r2.value(QLatin1String("tab_index")).toInt(), -1);
        QCOMPARE(r2.value(QLatin1String("title")).toString(), QString());
        QCOMPARE(r2.value(QLatin1String("language")).toString(), QString());
    }

    void app_info_shape() {
        QLocalSocket s;
        QVERIFY(connectClient(s));
        readGreeting(s);
        const QJsonObject resp = call(s, 22, QStringLiteral("app_info"));
        QVERIFY(resp.value(QLatin1String("ok")).toBool());
        const QJsonObject r = resp.value(QLatin1String("result")).toObject();
        QCOMPARE(r.value(QLatin1String("name")).toString(),
                 QStringLiteral("Notepatra"));
        QVERIFY(!r.value(QLatin1String("version")).toString().isEmpty());
        const QString edition = r.value(QLatin1String("edition")).toString();
        QVERIFY(edition == QLatin1String("Full") ||
                edition == QLatin1String("Lite"));
        const QString platform = r.value(QLatin1String("platform")).toString();
        QVERIFY(platform == QLatin1String("linux") ||
                platform == QLatin1String("windows") ||
                platform == QLatin1String("macos") ||
                platform == QLatin1String("other"));
    }

    void list_recent_files_roundtrip() {
        m_recent = QStringList()
                   << QStringLiteral("/tmp/a.txt") << QStringLiteral("/tmp/b.md");
        QLocalSocket s;
        QVERIFY(connectClient(s));
        readGreeting(s);
        QJsonObject resp = call(s, 23, QStringLiteral("list_recent_files"));
        QVERIFY(resp.value(QLatin1String("ok")).toBool());
        QJsonArray files = resp.value(QLatin1String("result"))
                               .toObject()
                               .value(QLatin1String("files"))
                               .toArray();
        QCOMPARE(files.size(), 2);
        QCOMPARE(files.at(0).toString(), QStringLiteral("/tmp/a.txt"));
        QCOMPARE(files.at(1).toString(), QStringLiteral("/tmp/b.md"));
        // No recents → empty list, still ok (never an error).
        m_recent.clear();
        resp = call(s, 24, QStringLiteral("list_recent_files"));
        QVERIFY(resp.value(QLatin1String("ok")).toBool());
        QCOMPARE(resp.value(QLatin1String("result"))
                     .toObject()
                     .value(QLatin1String("files"))
                     .toArray()
                     .size(),
                 0);
    }

    void find_in_tab_matches() {
        QLocalSocket s;
        QVERIFY(connectClient(s));
        readGreeting(s);
        // Default tab = current (0).
        QJsonObject args;
        args[QStringLiteral("query")] = QStringLiteral("needle");
        QJsonObject resp = call(s, 25, QStringLiteral("find_in_tab"), args);
        QVERIFY(resp.value(QLatin1String("ok")).toBool());
        QJsonObject r = resp.value(QLatin1String("result")).toObject();
        QJsonArray matches = r.value(QLatin1String("matches")).toArray();
        QCOMPARE(matches.size(), 1);
        QCOMPARE(matches.at(0).toObject().value(QLatin1String("line")).toInt(),
                 2);
        QCOMPARE(matches.at(0).toObject().value(QLatin1String("text"))
                     .toString(),
                 QStringLiteral("Second Line with Needle"));
        QCOMPARE(r.value(QLatin1String("truncated")).toBool(), false);
        // Explicit index.
        args[QStringLiteral("index")] = 1;
        resp = call(s, 26, QStringLiteral("find_in_tab"), args);
        QVERIFY(resp.value(QLatin1String("ok")).toBool());
        matches = resp.value(QLatin1String("result"))
                      .toObject()
                      .value(QLatin1String("matches"))
                      .toArray();
        QCOMPARE(matches.size(), 1);
        QCOMPARE(matches.at(0).toObject().value(QLatin1String("text"))
                     .toString(),
                 QStringLiteral("beta NEEDLE gamma"));
        // Failures: missing query; index out of range.
        QJsonObject bad = call(s, 27, QStringLiteral("find_in_tab"));
        QCOMPARE(bad.value(QLatin1String("ok")).toBool(), false);
        QJsonObject oob;
        oob[QStringLiteral("query")] = QStringLiteral("x");
        oob[QStringLiteral("index")] = 99;
        bad = call(s, 28, QStringLiteral("find_in_tab"), oob);
        QCOMPARE(bad.value(QLatin1String("ok")).toBool(), false);
        QVERIFY(bad.value(QLatin1String("error"))
                    .toString()
                    .contains(QLatin1String("out of range")));
    }

    void find_in_tab_truncates_at_500() {
        QString big;
        for (int i = 0; i < 600; ++i)
            big += QStringLiteral("needle %1\n").arg(i);
        m_fakeTabs[0].text = big;
        QLocalSocket s;
        QVERIFY(connectClient(s));
        readGreeting(s);
        QJsonObject args;
        args[QStringLiteral("query")] = QStringLiteral("NEEDLE");
        args[QStringLiteral("index")] = 0;
        const QJsonObject resp = call(s, 29, QStringLiteral("find_in_tab"),
                                      args);
        QVERIFY(resp.value(QLatin1String("ok")).toBool());
        const QJsonObject r = resp.value(QLatin1String("result")).toObject();
        QCOMPARE(r.value(QLatin1String("matches")).toArray().size(), 500);
        QCOMPARE(r.value(QLatin1String("truncated")).toBool(), true);
    }

    void new_tab_creates_and_fills() {
        QLocalSocket s;
        QVERIFY(connectClient(s));
        readGreeting(s);
        QJsonObject args;
        args[QStringLiteral("text")] = QStringLiteral("hello world\n");
        QJsonObject resp = call(s, 30, QStringLiteral("new_tab"), args);
        QVERIFY(resp.value(QLatin1String("ok")).toBool());
        QCOMPARE(resp.value(QLatin1String("result"))
                     .toObject()
                     .value(QLatin1String("tab_index"))
                     .toInt(),
                 2);
        // The buffer content is readable back through read_tab.
        QJsonObject readArgs;
        readArgs[QStringLiteral("index")] = 2;
        const QJsonObject rd = call(s, 31, QStringLiteral("read_tab"),
                                    readArgs);
        QCOMPARE(rd.value(QLatin1String("result"))
                     .toObject()
                     .value(QLatin1String("text"))
                     .toString(),
                 QStringLiteral("hello world\n"));
        // No text → empty new tab.
        resp = call(s, 32, QStringLiteral("new_tab"));
        QVERIFY(resp.value(QLatin1String("ok")).toBool());
        QCOMPARE(resp.value(QLatin1String("result"))
                     .toObject()
                     .value(QLatin1String("tab_index"))
                     .toInt(),
                 3);
        QCOMPARE(m_fakeTabs.size(), 4);
        QCOMPARE(m_fakeTabs[3].text, QString());
    }

    void goto_line_switches_and_moves() {
        QLocalSocket s;
        QVERIFY(connectClient(s));
        readGreeting(s);
        QJsonObject args;
        args[QStringLiteral("line")] = 2;
        args[QStringLiteral("tab_index")] = 1;
        QJsonObject resp = call(s, 33, QStringLiteral("goto_line"), args);
        QVERIFY(resp.value(QLatin1String("ok")).toBool());
        QCOMPARE(resp.value(QLatin1String("result"))
                     .toObject()
                     .value(QLatin1String("ok"))
                     .toBool(),
                 true);
        QCOMPARE(m_currentIndex, 1);
        QCOMPARE(m_lastGotoLine, 2);
        // Default tab = current.
        QJsonObject cur;
        cur[QStringLiteral("line")] = 1;
        resp = call(s, 34, QStringLiteral("goto_line"), cur);
        QVERIFY(resp.value(QLatin1String("ok")).toBool());
        QCOMPARE(m_lastGotoLine, 1);
        // Failures: line 0; tab out of range.
        QJsonObject zero;
        zero[QStringLiteral("line")] = 0;
        QJsonObject bad = call(s, 35, QStringLiteral("goto_line"), zero);
        QCOMPARE(bad.value(QLatin1String("ok")).toBool(), false);
        QJsonObject oob;
        oob[QStringLiteral("line")] = 1;
        oob[QStringLiteral("tab_index")] = 99;
        bad = call(s, 36, QStringLiteral("goto_line"), oob);
        QCOMPARE(bad.value(QLatin1String("ok")).toBool(), false);
    }

    void set_language_routes_and_rejects() {
        QLocalSocket s;
        QVERIFY(connectClient(s));
        readGreeting(s);
        QJsonObject args;
        args[QStringLiteral("language")] = QStringLiteral("Python");
        QJsonObject resp = call(s, 37, QStringLiteral("set_language"), args);
        QVERIFY(resp.value(QLatin1String("ok")).toBool());
        QCOMPARE(resp.value(QLatin1String("result"))
                     .toObject()
                     .value(QLatin1String("language"))
                     .toString(),
                 QStringLiteral("Python"));
        QCOMPARE(m_fakeTabs[0].language, QStringLiteral("Python"));
        // Explicit tab index.
        args[QStringLiteral("tab_index")] = 1;
        args[QStringLiteral("language")] = QStringLiteral("SQL");
        resp = call(s, 38, QStringLiteral("set_language"), args);
        QVERIFY(resp.value(QLatin1String("ok")).toBool());
        QCOMPARE(m_fakeTabs[1].language, QStringLiteral("SQL"));
        // Unknown language → ok:false with the contract message.
        QJsonObject bad;
        bad[QStringLiteral("language")] = QStringLiteral("Klingon");
        resp = call(s, 39, QStringLiteral("set_language"), bad);
        QCOMPARE(resp.value(QLatin1String("ok")).toBool(), false);
        QVERIFY(resp.value(QLatin1String("error"))
                    .toString()
                    .contains(QLatin1String("unknown language")));
        QCOMPARE(m_fakeTabs[0].language, QStringLiteral("Python")); // untouched
    }

    void set_language_resolves_case_and_alias() {
        QLocalSocket s;
        QVERIFY(connectClient(s));
        readGreeting(s);
        // Lowercase resolves to the canonical token, echoed honestly.
        QJsonObject args;
        args[QStringLiteral("language")] = QStringLiteral("python");
        QJsonObject resp = call(s, 240, QStringLiteral("set_language"), args);
        QVERIFY(resp.value(QLatin1String("ok")).toBool());
        QCOMPARE(resp.value(QLatin1String("result")).toObject()
                     .value(QLatin1String("language")).toString(),
                 QStringLiteral("Python"));
        QCOMPARE(m_fakeTabs[0].language, QStringLiteral("Python"));
        // Alias path.
        args[QStringLiteral("language")] = QStringLiteral("py");
        resp = call(s, 241, QStringLiteral("set_language"), args);
        QVERIFY(resp.value(QLatin1String("ok")).toBool());
        QCOMPARE(resp.value(QLatin1String("result")).toObject()
                     .value(QLatin1String("language")).toString(),
                 QStringLiteral("Python"));
        // Unknown still fails fast with the contract message.
        args[QStringLiteral("language")] = QStringLiteral("klingon");
        resp = call(s, 242, QStringLiteral("set_language"), args);
        QCOMPARE(resp.value(QLatin1String("ok")).toBool(), false);
        QVERIFY(resp.value(QLatin1String("error")).toString()
                    .contains(QLatin1String("unknown language")));
    }

    void list_languages_shape() {
        QLocalSocket s;
        QVERIFY(connectClient(s));
        readGreeting(s);
        const QJsonObject resp = call(s, 243, QStringLiteral("list_languages"));
        QVERIFY(resp.value(QLatin1String("ok")).toBool());
        const QJsonArray langs = resp.value(QLatin1String("result")).toObject()
                                     .value(QLatin1String("languages")).toArray();
        QCOMPARE(langs.size(), 5);
        QCOMPARE(langs.at(0).toString(), QStringLiteral("Plain Text"));
        QCOMPARE(langs.at(1).toString(), QStringLiteral("Python"));
    }

    void get_capabilities_shape() {
        QLocalSocket s;
        QVERIFY(connectClient(s));
        readGreeting(s);
        const QJsonObject resp =
            call(s, 244, QStringLiteral("get_capabilities"));
        QVERIFY(resp.value(QLatin1String("ok")).toBool());
        const QJsonObject r = resp.value(QLatin1String("result")).toObject();
        const QString edition = r.value(QLatin1String("edition")).toString();
        QVERIFY(edition == QLatin1String("Full") ||
                edition == QLatin1String("Lite"));
        QVERIFY(!r.value(QLatin1String("version")).toString().isEmpty());
        const QJsonObject f = r.value(QLatin1String("features")).toObject();
        QVERIFY(f.value(QLatin1String("duckdb")).isBool());
        QVERIFY(f.value(QLatin1String("webengine")).isBool());
        QCOMPARE(f.value(QLatin1String("noter")).toBool(), true); // host has notesRoot
        // The bridge NEVER sends tool_count/tiers — the sidecar owns those.
        QVERIFY(!r.contains(QLatin1String("tool_count")));
        QVERIFY(!r.contains(QLatin1String("tiers")));
    }

    void compare_tabs_opens_view() {
        QLocalSocket s;
        QVERIFY(connectClient(s));
        readGreeting(s);
        QJsonObject args;
        args[QStringLiteral("index_a")] = 0;
        args[QStringLiteral("index_b")] = 1;
        QJsonObject resp = call(s, 40, QStringLiteral("compare_tabs"), args);
        QVERIFY(resp.value(QLatin1String("ok")).toBool());
        QCOMPARE(resp.value(QLatin1String("result"))
                     .toObject()
                     .value(QLatin1String("opened"))
                     .toBool(),
                 true);
        QCOMPARE(m_compareCount, 1);
        // Failures: out-of-range index; self-compare.
        args[QStringLiteral("index_b")] = 99;
        resp = call(s, 41, QStringLiteral("compare_tabs"), args);
        QCOMPARE(resp.value(QLatin1String("ok")).toBool(), false);
        args[QStringLiteral("index_b")] = 0;
        resp = call(s, 42, QStringLiteral("compare_tabs"), args);
        QCOMPARE(resp.value(QLatin1String("ok")).toBool(), false);
        QCOMPARE(m_compareCount, 1); // no extra dialog was opened
    }

    void format_text_pure_function() {
        const QString before0 = m_fakeTabs[0].text;
        QLocalSocket s;
        QVERIFY(connectClient(s));
        readGreeting(s);
        QJsonObject args;
        args[QStringLiteral("kind")] = QStringLiteral("json");
        args[QStringLiteral("text")] = QStringLiteral("{\"a\":1}");
        QJsonObject resp = call(s, 43, QStringLiteral("format_text"), args);
        QVERIFY(resp.value(QLatin1String("ok")).toBool());
        QCOMPARE(resp.value(QLatin1String("result"))
                     .toObject()
                     .value(QLatin1String("text"))
                     .toString(),
                 QStringLiteral("FMT-JSON:{\"a\":1}"));
        args[QStringLiteral("kind")] = QStringLiteral("sql");
        args[QStringLiteral("text")] = QStringLiteral("select 1");
        resp = call(s, 44, QStringLiteral("format_text"), args);
        QVERIFY(resp.value(QLatin1String("ok")).toBool());
        QCOMPARE(resp.value(QLatin1String("result"))
                     .toObject()
                     .value(QLatin1String("text"))
                     .toString(),
                 QStringLiteral("SQL:select 1"));
        // Invalid input → the formatter's error message.
        args[QStringLiteral("kind")] = QStringLiteral("json");
        args[QStringLiteral("text")] = QStringLiteral("not json");
        resp = call(s, 45, QStringLiteral("format_text"), args);
        QCOMPARE(resp.value(QLatin1String("ok")).toBool(), false);
        QVERIFY(resp.value(QLatin1String("error"))
                    .toString()
                    .contains(QLatin1String("invalid json")));
        // Unknown kind; missing text.
        QJsonObject badKind;
        badKind[QStringLiteral("kind")] = QStringLiteral("yaml");
        badKind[QStringLiteral("text")] = QStringLiteral("x");
        resp = call(s, 46, QStringLiteral("format_text"), badKind);
        QCOMPARE(resp.value(QLatin1String("ok")).toBool(), false);
        QVERIFY(resp.value(QLatin1String("error"))
                    .toString()
                    .contains(QLatin1String("unknown kind")));
        QJsonObject noText;
        noText[QStringLiteral("kind")] = QStringLiteral("sql");
        resp = call(s, 47, QStringLiteral("format_text"), noText);
        QCOMPARE(resp.value(QLatin1String("ok")).toBool(), false);
        // MUST NOT touch any buffer.
        QCOMPARE(m_fakeTabs[0].text, before0);
    }

    void list_notes_scans_root_excluding_trash() {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString root = dir.path();
        const QString notePath =
            writeNote(root + QStringLiteral("/Inbox/20260101-1200-team-sync.html"),
                      QStringLiteral("Team Sync"),
                      QStringLiteral("agenda items and decisions"));
        QVERIFY(!notePath.isEmpty());
        QVERIFY(!writeNote(root + QStringLiteral("/20260102-0900-noter-02.html"),
                           QStringLiteral("Second Note"),
                           QStringLiteral("more body")).isEmpty());
        // Trash + sidecar files must never appear.
        QVERIFY(!writeNote(root + QStringLiteral("/Trash/gone.html"),
                           QStringLiteral("Trashed"),
                           QStringLiteral("deleted")).isEmpty());
        {
            QFile f(root + QStringLiteral("/Inbox/x.html.bak1"));
            QVERIFY(f.open(QIODevice::WriteOnly));
            f.write("<html></html>");
        }
        m_notesRoot = root;
        QLocalSocket s;
        QVERIFY(connectClient(s));
        readGreeting(s);
        QJsonObject resp = call(s, 48, QStringLiteral("list_notes"));
        QVERIFY(resp.value(QLatin1String("ok")).toBool());
        const QJsonArray notes = resp.value(QLatin1String("result"))
                                     .toObject()
                                     .value(QLatin1String("notes"))
                                     .toArray();
        QCOMPARE(notes.size(), 2);
        bool sawTeamSync = false;
        for (const QJsonValue &v : notes) {
            const QJsonObject n = v.toObject();
            const QString file = n.value(QLatin1String("file")).toString();
            QVERIFY(!file.contains(QLatin1String("/Trash/")));
            QVERIFY(!n.value(QLatin1String("modified_iso")).toString()
                         .isEmpty());
            QVERIFY(!n.value(QLatin1String("title")).toString().isEmpty());
            if (n.value(QLatin1String("title")).toString() ==
                QLatin1String("Team Sync"))
                sawTeamSync = true;
        }
        QVERIFY(sawTeamSync);
        // No Noter root configured → empty list, not an error.
        m_notesRoot.clear();
        resp = call(s, 49, QStringLiteral("list_notes"));
        QVERIFY(resp.value(QLatin1String("ok")).toBool());
        QCOMPARE(resp.value(QLatin1String("result"))
                     .toObject()
                     .value(QLatin1String("notes"))
                     .toArray()
                     .size(),
                 0);
    }

    void read_note_plaintext_and_containment() {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString root = dir.path();
        const QString notePath =
            writeNote(root + QStringLiteral("/Inbox/20260101-1200-team-sync.html"),
                      QStringLiteral("Team Sync"),
                      QStringLiteral("agenda items and decisions"));
        QVERIFY(!notePath.isEmpty());
        m_notesRoot = root;
        QLocalSocket s;
        QVERIFY(connectClient(s));
        readGreeting(s);
        QJsonObject args;
        args[QStringLiteral("file")] = notePath;
        QJsonObject resp = call(s, 50, QStringLiteral("read_note"), args);
        QVERIFY(resp.value(QLatin1String("ok")).toBool());
        const QJsonObject r = resp.value(QLatin1String("result")).toObject();
        QCOMPARE(r.value(QLatin1String("title")).toString(),
                 QStringLiteral("Team Sync"));
        const QString text = r.value(QLatin1String("text")).toString();
        QVERIFY(text.contains(QLatin1String("agenda items and decisions")));
        QVERIFY(!text.contains(QLatin1Char('<'))); // tags stripped
        // Containment: a .html OUTSIDE the notes root is refused.
        QTemporaryDir outside;
        QVERIFY(outside.isValid());
        const QString stray = outside.path() + QStringLiteral("/evil.html");
        {
            QFile f(stray);
            QVERIFY(f.open(QIODevice::WriteOnly));
            f.write("<html><body>secret</body></html>");
        }
        QJsonObject bad;
        bad[QStringLiteral("file")] = stray;
        resp = call(s, 51, QStringLiteral("read_note"), bad);
        QCOMPARE(resp.value(QLatin1String("ok")).toBool(), false);
        QVERIFY(resp.value(QLatin1String("error"))
                    .toString()
                    .contains(QLatin1String("not a note file")));
        // Nonexistent file and missing arg also fail.
        bad[QStringLiteral("file")] = root + QStringLiteral("/Inbox/nope.html");
        resp = call(s, 52, QStringLiteral("read_note"), bad);
        QCOMPARE(resp.value(QLatin1String("ok")).toBool(), false);
        resp = call(s, 53, QStringLiteral("read_note"));
        QCOMPARE(resp.value(QLatin1String("ok")).toBool(), false);
    }

    void read_note_caps_at_2mb() {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString root = dir.path();
        const QString big = root + QStringLiteral("/Inbox/huge.html");
        QDir().mkpath(root + QStringLiteral("/Inbox"));
        {
            QFile f(big);
            QVERIFY(f.open(QIODevice::WriteOnly));
            f.write("<html><body>");
            const QByteArray filler(1024, 'x');
            for (int i = 0; i < 2 * 1024 + 64; ++i) f.write(filler); // > 2 MB
            f.write("</body></html>");
        }
        m_notesRoot = root;
        QLocalSocket s;
        QVERIFY(connectClient(s));
        readGreeting(s);
        QJsonObject args;
        args[QStringLiteral("file")] = big;
        const QJsonObject resp = call(s, 54, QStringLiteral("read_note"),
                                      args);
        QCOMPARE(resp.value(QLatin1String("ok")).toBool(), false);
        QVERIFY(resp.value(QLatin1String("error"))
                    .toString()
                    .contains(QLatin1String("too large")));
    }

    // ── v0.1.118 WRITE tier — human-approval-gated verbs ─────────────

    void insert_text_approve_applies_mutation() {
        QLocalSocket s;
        QVERIFY(connectClient(s));
        readGreeting(s);
        QJsonObject args;
        args[QStringLiteral("text")] = QStringLiteral("XYZ-");
        args[QStringLiteral("tab_index")] = 0;
        args[QStringLiteral("line")] = 1;
        args[QStringLiteral("col")] = 1;
        sendRequest(s, 60, QStringLiteral("insert_text"), args);
        QFrame *card = waitForCard();
        QVERIFY(card);
        auto *desc = card->findChild<QLabel *>(
            QStringLiteral("mcpApprovalDesc"));
        QVERIFY(desc);
        QVERIFY(desc->text().contains(
            QLatin1String("AI tool (MCP) requests")));
        QVERIFY(desc->text().contains(
            QLatin1String("insert 4 chars into 'notes.txt'")));
        auto *prev = card->findChild<QLabel *>(
            QStringLiteral("mcpApprovalPreview"));
        QVERIFY(prev);
        QVERIFY(prev->text().contains(QLatin1String("XYZ-")));
        // Held: no response, no mutation until the human decides.
        QTest::qWait(150);
        QVERIFY(!s.canReadLine());
        QVERIFY(!m_fakeTabs[0].text.startsWith(QLatin1String("XYZ-")));
        // The card is focus-neutral: it must never steal the editor's keys.
        QCOMPARE(card->focusPolicy(), Qt::NoFocus);
        auto *approve = card->findChild<QPushButton *>(
            QStringLiteral("mcpApproveBtn"));
        auto *deny = card->findChild<QPushButton *>(
            QStringLiteral("mcpDenyBtn"));
        QVERIFY(approve);
        QVERIFY(deny);
        QCOMPARE(approve->focusPolicy(), Qt::NoFocus);
        QCOMPARE(deny->focusPolicy(), Qt::NoFocus);
        approve->click();
        const QJsonObject resp = readObj(s);
        QCOMPARE(resp.value(QLatin1String("id")).toInt(), 60);
        QVERIFY(resp.value(QLatin1String("ok")).toBool());
        QCOMPARE(resp.value(QLatin1String("result"))
                     .toObject()
                     .value(QLatin1String("tab_index"))
                     .toInt(),
                 0);
        QVERIFY(m_fakeTabs[0].text.startsWith(
            QLatin1String("XYZ-hello world")));
        QVERIFY(waitForCardGone());
    }

    void insert_text_deny_leaves_buffer_untouched() {
        QLocalSocket s;
        QVERIFY(connectClient(s));
        readGreeting(s);
        const QString before = m_fakeTabs[0].text;
        QJsonObject args;
        args[QStringLiteral("text")] = QStringLiteral("nope");
        args[QStringLiteral("tab_index")] = 0;
        sendRequest(s, 61, QStringLiteral("insert_text"), args);
        QFrame *card = waitForCard();
        QVERIFY(card);
        auto *desc = card->findChild<QLabel *>(
            QStringLiteral("mcpApprovalDesc"));
        QVERIFY(desc);
        QVERIFY(desc->text().contains(QLatin1String("at the cursor")));
        auto *deny = card->findChild<QPushButton *>(
            QStringLiteral("mcpDenyBtn"));
        QVERIFY(deny);
        deny->click();
        const QJsonObject resp = readObj(s);
        QCOMPARE(resp.value(QLatin1String("id")).toInt(), 61);
        QCOMPARE(resp.value(QLatin1String("ok")).toBool(), false);
        QCOMPARE(resp.value(QLatin1String("error")).toString(),
                 QStringLiteral("denied by user"));
        QCOMPARE(m_fakeTabs[0].text, before);
        QVERIFY(waitForCardGone());
    }

    void insert_text_missing_text_fails_without_card() {
        QLocalSocket s;
        QVERIFY(connectClient(s));
        readGreeting(s);
        const QJsonObject resp = call(s, 62, QStringLiteral("insert_text"));
        QCOMPARE(resp.value(QLatin1String("ok")).toBool(), false);
        QVERIFY(!resp.value(QLatin1String("error")).toString().isEmpty());
        QTest::qWait(100);
        QVERIFY(!findCard());
    }

    void replace_selection_approve_and_no_selection() {
        m_selection = QStringLiteral("hello");
        QLocalSocket s;
        QVERIFY(connectClient(s));
        readGreeting(s);
        QJsonObject args;
        args[QStringLiteral("text")] = QStringLiteral("goodbye");
        args[QStringLiteral("tab_index")] = 0;
        sendRequest(s, 63, QStringLiteral("replace_selection"), args);
        QFrame *card = waitForCard();
        QVERIFY(card);
        auto *desc = card->findChild<QLabel *>(
            QStringLiteral("mcpApprovalDesc"));
        QVERIFY(desc);
        QVERIFY(desc->text().contains(
            QLatin1String("replace the selection in 'notes.txt'")));
        auto *approve = card->findChild<QPushButton *>(
            QStringLiteral("mcpApproveBtn"));
        QVERIFY(approve);
        approve->click();
        const QJsonObject resp = readObj(s);
        QCOMPARE(resp.value(QLatin1String("id")).toInt(), 63);
        QVERIFY(resp.value(QLatin1String("ok")).toBool());
        QVERIFY(m_fakeTabs[0].text.startsWith(
            QLatin1String("goodbye world")));
        QVERIFY(waitForCardGone());
        // No selection left → fails fast, and NO card ever appears.
        const QJsonObject bad = call(s, 64,
                                     QStringLiteral("replace_selection"),
                                     args);
        QCOMPARE(bad.value(QLatin1String("ok")).toBool(), false);
        QVERIFY(bad.value(QLatin1String("error"))
                    .toString()
                    .contains(QLatin1String("no selection")));
        QTest::qWait(100);
        QVERIFY(!findCard());
    }

    void apply_edit_first_all_and_no_match() {
        QLocalSocket s;
        QVERIFY(connectClient(s));
        readGreeting(s);
        // First occurrence only.
        QJsonObject args;
        args[QStringLiteral("find")] = QStringLiteral("l");
        args[QStringLiteral("replace")] = QStringLiteral("L");
        args[QStringLiteral("tab_index")] = 0;
        sendRequest(s, 65, QStringLiteral("apply_edit"), args);
        QFrame *card = waitForCardContaining(QStringLiteral("the first"));
        QVERIFY(card);
        auto *approve = card->findChild<QPushButton *>(
            QStringLiteral("mcpApproveBtn"));
        QVERIFY(approve);
        approve->click();
        QJsonObject resp = readObj(s);
        QCOMPARE(resp.value(QLatin1String("id")).toInt(), 65);
        QVERIFY(resp.value(QLatin1String("ok")).toBool());
        QCOMPARE(resp.value(QLatin1String("result"))
                     .toObject()
                     .value(QLatin1String("count"))
                     .toInt(),
                 1);
        QVERIFY(m_fakeTabs[0].text.startsWith(QLatin1String("heLlo")));
        // Every occurrence. Text is now
        // "heLlo world\nSecond Line with Needle\n" → three 'o's.
        QJsonObject allArgs;
        allArgs[QStringLiteral("find")] = QStringLiteral("o");
        allArgs[QStringLiteral("replace")] = QStringLiteral("0");
        allArgs[QStringLiteral("all")] = true;
        allArgs[QStringLiteral("tab_index")] = 0;
        sendRequest(s, 66, QStringLiteral("apply_edit"), allArgs);
        card = waitForCardContaining(QStringLiteral("every"));
        QVERIFY(card);
        approve = card->findChild<QPushButton *>(
            QStringLiteral("mcpApproveBtn"));
        QVERIFY(approve);
        approve->click();
        resp = readObj(s);
        QCOMPARE(resp.value(QLatin1String("id")).toInt(), 66);
        QVERIFY(resp.value(QLatin1String("ok")).toBool());
        QCOMPARE(resp.value(QLatin1String("result"))
                     .toObject()
                     .value(QLatin1String("count"))
                     .toInt(),
                 3);
        QVERIFY(!m_fakeTabs[0].text.contains(QLatin1Char('o')));
        QVERIFY(waitForCardGone());
        // No match → fails fast, no card, no human bothered.
        QJsonObject miss;
        miss[QStringLiteral("find")] = QStringLiteral("zzz-not-here");
        miss[QStringLiteral("replace")] = QStringLiteral("x");
        miss[QStringLiteral("tab_index")] = 0;
        const QJsonObject bad = call(s, 67, QStringLiteral("apply_edit"),
                                     miss);
        QCOMPARE(bad.value(QLatin1String("ok")).toBool(), false);
        QCOMPARE(bad.value(QLatin1String("error")).toString(),
                 QStringLiteral("no match"));
        QTest::qWait(100);
        QVERIFY(!findCard());
    }

    void save_tab_approve_and_untitled_refusal() {
        QLocalSocket s;
        QVERIFY(connectClient(s));
        readGreeting(s);
        QJsonObject args;
        args[QStringLiteral("tab_index")] = 0;
        sendRequest(s, 68, QStringLiteral("save_tab"), args);
        QFrame *card = waitForCard();
        QVERIFY(card);
        auto *desc = card->findChild<QLabel *>(
            QStringLiteral("mcpApprovalDesc"));
        QVERIFY(desc);
        QVERIFY(desc->text().contains(
            QLatin1String("save 'notes.txt' to disk")));
        auto *prev = card->findChild<QLabel *>(
            QStringLiteral("mcpApprovalPreview"));
        QVERIFY(prev);
        QCOMPARE(prev->text(),
                 QDir::toNativeSeparators(QStringLiteral("/fake/notes.txt")));
        auto *approve = card->findChild<QPushButton *>(
            QStringLiteral("mcpApproveBtn"));
        QVERIFY(approve);
        approve->click();
        const QJsonObject resp = readObj(s);
        QCOMPARE(resp.value(QLatin1String("id")).toInt(), 68);
        QVERIFY(resp.value(QLatin1String("ok")).toBool());
        QCOMPARE(resp.value(QLatin1String("result"))
                     .toObject()
                     .value(QLatin1String("saved"))
                     .toBool(),
                 true);
        QCOMPARE(m_savedTabs, QVector<int>{0});
        QVERIFY(waitForCardGone());
        // Untitled tab: refused up front — never a Save As dialog, no card.
        QJsonObject untitled;
        untitled[QStringLiteral("tab_index")] = 1;
        const QJsonObject bad = call(s, 69, QStringLiteral("save_tab"),
                                     untitled);
        QCOMPARE(bad.value(QLatin1String("ok")).toBool(), false);
        QCOMPARE(bad.value(QLatin1String("error")).toString(),
                 QStringLiteral("needs Save As"));
        QTest::qWait(100);
        QVERIFY(!findCard());
        QCOMPARE(m_savedTabs.size(), 1);
    }

    void approval_times_out_to_deny() {
        m_bridge->setApprovalTimeoutMs(200);
        QLocalSocket s;
        QVERIFY(connectClient(s));
        readGreeting(s);
        const QString before = m_fakeTabs[0].text;
        QJsonObject args;
        args[QStringLiteral("text")] = QStringLiteral("late");
        args[QStringLiteral("tab_index")] = 0;
        sendRequest(s, 70, QStringLiteral("insert_text"), args);
        QVERIFY(waitForCard());
        // Nobody clicks: the timeout auto-denies.
        const QJsonObject resp = readObj(s);
        QCOMPARE(resp.value(QLatin1String("id")).toInt(), 70);
        QCOMPARE(resp.value(QLatin1String("ok")).toBool(), false);
        QCOMPARE(resp.value(QLatin1String("error")).toString(),
                 QStringLiteral("approval timed out"));
        QCOMPARE(m_fakeTabs[0].text, before);
        QVERIFY(waitForCardGone());
    }

    void disconnect_while_pending_dismisses_card() {
        const QString before = m_fakeTabs[0].text;
        {
            QLocalSocket s;
            QVERIFY(connectClient(s));
            readGreeting(s);
            QJsonObject args;
            args[QStringLiteral("text")] = QStringLiteral("ghost");
            args[QStringLiteral("tab_index")] = 0;
            sendRequest(s, 71, QStringLiteral("insert_text"), args);
            QVERIFY(waitForCard());
            s.abort(); // peer vanishes while the card is on screen
        }
        QVERIFY(waitForCardGone());
        QCOMPARE(m_fakeTabs[0].text, before); // nothing executed
        // The bridge still serves a fresh client afterwards.
        QLocalSocket s2;
        QVERIFY(connectClient(s2));
        readGreeting(s2);
        const QJsonObject resp = call(s2, 72,
                                      QStringLiteral("list_open_tabs"));
        QVERIFY(resp.value(QLatin1String("ok")).toBool());
    }

    void approvals_queue_fifo_one_card_at_a_time() {
        QLocalSocket s;
        QVERIFY(connectClient(s));
        readGreeting(s);
        QJsonObject first;
        first[QStringLiteral("text")] = QStringLiteral("first");
        first[QStringLiteral("tab_index")] = 0;
        sendRequest(s, 73, QStringLiteral("insert_text"), first);
        QJsonObject second;
        second[QStringLiteral("text")] = QStringLiteral("second");
        second[QStringLiteral("tab_index")] = 0;
        sendRequest(s, 74, QStringLiteral("insert_text"), second);
        // FIFO: the first request's card shows first — and alone.
        QFrame *card = waitForCardContaining(QStringLiteral("5 chars"));
        QVERIFY(card);
        int visibleCards = 0;
        const auto cards = m_hostWindow->findChildren<QFrame *>(
            QStringLiteral("mcpApprovalCard"));
        for (QFrame *c : cards)
            if (c->isVisible()) ++visibleCards;
        QCOMPARE(visibleCards, 1);
        auto *approve = card->findChild<QPushButton *>(
            QStringLiteral("mcpApproveBtn"));
        QVERIFY(approve);
        approve->click();
        QJsonObject resp = readObj(s);
        QCOMPARE(resp.value(QLatin1String("id")).toInt(), 73);
        QVERIFY(resp.value(QLatin1String("ok")).toBool());
        // The second card follows automatically; deny it.
        card = waitForCardContaining(QStringLiteral("6 chars"));
        QVERIFY(card);
        auto *deny = card->findChild<QPushButton *>(
            QStringLiteral("mcpDenyBtn"));
        QVERIFY(deny);
        deny->click();
        resp = readObj(s);
        QCOMPARE(resp.value(QLatin1String("id")).toInt(), 74);
        QCOMPARE(resp.value(QLatin1String("ok")).toBool(), false);
        QCOMPARE(resp.value(QLatin1String("error")).toString(),
                 QStringLiteral("denied by user"));
        QVERIFY(m_fakeTabs[0].text.contains(QLatin1String("first")));
        QVERIFY(!m_fakeTabs[0].text.contains(QLatin1String("second")));
    }

    void read_verbs_bypass_approval_gate() {
        QLocalSocket s;
        QVERIFY(connectClient(s));
        readGreeting(s);
        QJsonObject args;
        args[QStringLiteral("text")] = QStringLiteral("hold");
        args[QStringLiteral("tab_index")] = 0;
        sendRequest(s, 75, QStringLiteral("insert_text"), args);
        QVERIFY(waitForCard());
        // A read verb on the SAME socket answers immediately while the
        // write sits behind the card.
        const QJsonObject read = call(s, 76,
                                      QStringLiteral("list_open_tabs"));
        QCOMPARE(read.value(QLatin1String("id")).toInt(), 76);
        QVERIFY(read.value(QLatin1String("ok")).toBool());
        QVERIFY(findCard()); // still pending
        auto *deny = findCard()->findChild<QPushButton *>(
            QStringLiteral("mcpDenyBtn"));
        QVERIFY(deny);
        deny->click();
        const QJsonObject resp = readObj(s);
        QCOMPARE(resp.value(QLatin1String("id")).toInt(), 75);
        QCOMPARE(resp.value(QLatin1String("ok")).toBool(), false);
    }

    // ══ v0.1.119 depth wave ═══════════════════════════════════════════

    void list_reminders_buckets_and_empty() {
        // No reminders → ok, empty array (documented, never an error).
        {
            QLocalSocket s;
            QVERIFY(connectClient(s));
            readGreeting(s);
            const QJsonObject resp =
                call(s, 100, QStringLiteral("list_reminders"));
            QVERIFY(resp.value(QLatin1String("ok")).toBool());
            QCOMPARE(resp.value(QLatin1String("result"))
                         .toObject()
                         .value(QLatin1String("reminders"))
                         .toArray()
                         .size(),
                     0);
        }
        // One reminder per bucket; the bridge computes the bucket itself.
        const QDateTime now = QDateTime::currentDateTime();
        auto add = [this](const QString &file, const QString &title,
                          const QDateTime &due) {
            QJsonObject o;
            o[QStringLiteral("note_file")] = file;
            o[QStringLiteral("note_title")] = title;
            o[QStringLiteral("due_iso")] = due.toUTC().toString(Qt::ISODate);
            m_reminders.append(o);
        };
        add(QStringLiteral("/n/a.html"), QStringLiteral("OverdueOne"),
            QDateTime(QDate(2000, 1, 1), QTime(9, 0)));
        add(QStringLiteral("/n/b.html"), QStringLiteral("TodayOne"),
            QDateTime(now.date(), QTime(23, 59, 59)));
        add(QStringLiteral("/n/c.html"), QStringLiteral("WeekOne"),
            now.addDays(3));
        add(QStringLiteral("/n/d.html"), QStringLiteral("LaterOne"),
            now.addDays(400));
        QLocalSocket s;
        QVERIFY(connectClient(s));
        readGreeting(s);
        const QJsonObject resp =
            call(s, 101, QStringLiteral("list_reminders"));
        QVERIFY(resp.value(QLatin1String("ok")).toBool());
        const QJsonArray rem = resp.value(QLatin1String("result"))
                                   .toObject()
                                   .value(QLatin1String("reminders"))
                                   .toArray();
        QCOMPARE(rem.size(), 4);
        auto bucketOf = [&](const QString &title) -> QString {
            for (const QJsonValue &v : rem) {
                const QJsonObject o = v.toObject();
                if (o.value(QLatin1String("note_title")).toString() == title)
                    return o.value(QLatin1String("bucket")).toString();
            }
            return QString();
        };
        for (const QJsonValue &v : rem) {
            const QJsonObject o = v.toObject();
            QVERIFY(!o.value(QLatin1String("note_file")).toString().isEmpty());
            QVERIFY(!o.value(QLatin1String("due_iso")).toString().isEmpty());
        }
        QCOMPARE(bucketOf(QStringLiteral("OverdueOne")),
                 QStringLiteral("Overdue"));
        QCOMPARE(bucketOf(QStringLiteral("TodayOne")),
                 QStringLiteral("Today"));
        QCOMPARE(bucketOf(QStringLiteral("WeekOne")),
                 QStringLiteral("This week"));
        QCOMPARE(bucketOf(QStringLiteral("LaterOne")),
                 QStringLiteral("Later"));
    }

    void git_verbs_route_args() {
        QLocalSocket s;
        QVERIFY(connectClient(s));
        readGreeting(s);
        auto out = [](const QJsonObject &r) {
            return r.value(QLatin1String("result"))
                .toObject()
                .value(QLatin1String("output"))
                .toString();
        };
        QJsonObject r = call(s, 110, QStringLiteral("git_status"));
        QVERIFY(r.value(QLatin1String("ok")).toBool());
        QVERIFY(out(r).contains(QLatin1String("git-status")));
        QJsonObject dargs;
        dargs[QStringLiteral("path")] = QStringLiteral("src/x.cpp");
        r = call(s, 111, QStringLiteral("git_diff"), dargs);
        QVERIFY(r.value(QLatin1String("ok")).toBool());
        QVERIFY(out(r).contains(QLatin1String("git-diff")));
        QVERIFY(out(r).contains(QLatin1String("path=src/x.cpp")));
        QJsonObject largs;
        largs[QStringLiteral("limit")] = 5;
        r = call(s, 112, QStringLiteral("git_log"), largs);
        QVERIFY(r.value(QLatin1String("ok")).toBool());
        QVERIFY(out(r).contains(QLatin1String("git-log")));
        QVERIFY(out(r).contains(QLatin1String("limit=5")));
        QJsonObject sargs;
        sargs[QStringLiteral("ref")] = QStringLiteral("HEAD~2");
        r = call(s, 113, QStringLiteral("git_show"), sargs);
        QVERIFY(r.value(QLatin1String("ok")).toBool());
        QVERIFY(out(r).contains(QLatin1String("ref=HEAD~2")));
        r = call(s, 114, QStringLiteral("git_branch"));
        QVERIFY(r.value(QLatin1String("ok")).toBool());
        QVERIFY(out(r).contains(QLatin1String("git-branch")));
    }

    void git_error_surfaces() {
        m_gitFail = true;
        QLocalSocket s;
        QVERIFY(connectClient(s));
        readGreeting(s);
        const QJsonObject r = call(s, 115, QStringLiteral("git_status"));
        QCOMPARE(r.value(QLatin1String("ok")).toBool(), false);
        QVERIFY(r.value(QLatin1String("error"))
                    .toString()
                    .contains(QLatin1String("not_a_repo")));
    }

    void git_output_caps_at_256kb() {
        m_gitHuge = true;
        QLocalSocket s;
        QVERIFY(connectClient(s));
        readGreeting(s);
        const QJsonObject r = call(s, 116, QStringLiteral("git_log"));
        QVERIFY(r.value(QLatin1String("ok")).toBool());
        const QJsonObject res = r.value(QLatin1String("result")).toObject();
        QCOMPARE(res.value(QLatin1String("truncated")).toBool(), true);
        const QString o = res.value(QLatin1String("output")).toString();
        QVERIFY(o.contains(QLatin1String("truncated at 256 KB")));
        QVERIFY(o.size() <= 256 * 1024 + 64);
    }

    void validate_npd_source_valid_and_invalid() {
        QLocalSocket s;
        QVERIFY(connectClient(s));
        readGreeting(s);
        QJsonObject good;
        good[QStringLiteral("source")] =
            QStringLiteral("node a (Start)\nnode b [Process]\n");
        QJsonObject r = call(s, 120, QStringLiteral("validate_npd"), good);
        QVERIFY(r.value(QLatin1String("ok")).toBool());
        QJsonObject res = r.value(QLatin1String("result")).toObject();
        QCOMPARE(res.value(QLatin1String("valid")).toBool(), true);
        QCOMPARE(res.value(QLatin1String("errors")).toArray().size(), 0);
        // An icon node without a ':name' is a parse error (real parser rule).
        QJsonObject bad;
        bad[QStringLiteral("source")] =
            QStringLiteral("node a (Start)\nicon x \"NoColon\"\n");
        r = call(s, 121, QStringLiteral("validate_npd"), bad);
        QVERIFY(r.value(QLatin1String("ok")).toBool()); // verb itself succeeds
        res = r.value(QLatin1String("result")).toObject();
        QCOMPARE(res.value(QLatin1String("valid")).toBool(), false);
        const QJsonArray errs = res.value(QLatin1String("errors")).toArray();
        QVERIFY(errs.size() >= 1);
        const QJsonObject e0 = errs.at(0).toObject();
        QVERIFY(e0.contains(QLatin1String("line")));
        QVERIFY(e0.value(QLatin1String("line")).toInt() >= 1);
        QVERIFY(!e0.value(QLatin1String("message")).toString().isEmpty());
    }

    void validate_npd_by_tab_and_bad_args() {
        m_fakeTabs.append({QStringLiteral("chart.npd"), QString(),
                           QStringLiteral("node a (Start)\n"), false,
                           QStringLiteral("Plain Text"), true});
        const int di = m_fakeTabs.size() - 1;
        QLocalSocket s;
        QVERIFY(connectClient(s));
        readGreeting(s);
        QJsonObject args;
        args[QStringLiteral("tab_index")] = di;
        QJsonObject r = call(s, 122, QStringLiteral("validate_npd"), args);
        QVERIFY(r.value(QLatin1String("ok")).toBool());
        QCOMPARE(r.value(QLatin1String("result"))
                     .toObject()
                     .value(QLatin1String("valid"))
                     .toBool(),
                 true);
        // Non-diagram tab → error.
        QJsonObject nd;
        nd[QStringLiteral("tab_index")] = 0;
        r = call(s, 123, QStringLiteral("validate_npd"), nd);
        QCOMPARE(r.value(QLatin1String("ok")).toBool(), false);
        QVERIFY(r.value(QLatin1String("error"))
                    .toString()
                    .contains(QLatin1String("not a diagram")));
        // Out of range.
        QJsonObject oob;
        oob[QStringLiteral("tab_index")] = 99;
        r = call(s, 124, QStringLiteral("validate_npd"), oob);
        QCOMPARE(r.value(QLatin1String("ok")).toBool(), false);
        // Neither source nor tab_index.
        r = call(s, 125, QStringLiteral("validate_npd"));
        QCOMPARE(r.value(QLatin1String("ok")).toBool(), false);
        QVERIFY(r.value(QLatin1String("error"))
                    .toString()
                    .contains(QLatin1String("missing source or tab_index")));
    }

    void run_sql_select_and_engine() {
        m_sqlRows = 3;
        QLocalSocket s;
        QVERIFY(connectClient(s));
        readGreeting(s);
        QJsonObject a;
        a[QStringLiteral("sql")] = QStringLiteral("SELECT id, name FROM t");
        QJsonObject r = call(s, 130, QStringLiteral("run_sql"), a);
        QVERIFY(r.value(QLatin1String("ok")).toBool());
        QJsonObject res = r.value(QLatin1String("result")).toObject();
        QCOMPARE(res.value(QLatin1String("columns")).toArray().size(), 2);
        QCOMPARE(res.value(QLatin1String("rows")).toArray().size(), 3);
        QCOMPARE(res.value(QLatin1String("truncated")).toBool(), false);
        QCOMPARE(res.value(QLatin1String("engine")).toString(),
                 QStringLiteral("sqlite"));
        // csv_path → duckdb engine reported.
        a[QStringLiteral("csv_path")] = QStringLiteral("/tmp/data.csv");
        r = call(s, 131, QStringLiteral("run_sql"), a);
        QVERIFY(r.value(QLatin1String("ok")).toBool());
        QCOMPARE(r.value(QLatin1String("result"))
                     .toObject()
                     .value(QLatin1String("engine"))
                     .toString(),
                 QStringLiteral("duckdb"));
    }

    void run_sql_rejects_non_readonly() {
        QLocalSocket s;
        QVERIFY(connectClient(s));
        readGreeting(s);
        const QStringList bad = {
            QStringLiteral("UPDATE t SET x=1"),
            QStringLiteral("DELETE FROM t"),
            QStringLiteral("WITH c AS (SELECT 1) DELETE FROM t"), // WITH-DML trap
            QStringLiteral("DROP TABLE t"),
        };
        int id = 140;
        for (const QString &q : bad) {
            QJsonObject a;
            a[QStringLiteral("sql")] = q;
            const QJsonObject r = call(s, id++, QStringLiteral("run_sql"), a);
            QCOMPARE(r.value(QLatin1String("ok")).toBool(), false);
            QVERIFY(!r.value(QLatin1String("error")).toString().isEmpty());
        }
        // Missing sql.
        const QJsonObject miss = call(s, id++, QStringLiteral("run_sql"));
        QCOMPARE(miss.value(QLatin1String("ok")).toBool(), false);
    }

    void run_sql_caps_at_200_rows() {
        m_sqlRows = 250;
        QLocalSocket s;
        QVERIFY(connectClient(s));
        readGreeting(s);
        QJsonObject a;
        a[QStringLiteral("sql")] = QStringLiteral("SELECT * FROM big");
        const QJsonObject r = call(s, 150, QStringLiteral("run_sql"), a);
        QVERIFY(r.value(QLatin1String("ok")).toBool());
        const QJsonObject res = r.value(QLatin1String("result")).toObject();
        QCOMPARE(res.value(QLatin1String("rows")).toArray().size(), 200);
        QCOMPARE(res.value(QLatin1String("truncated")).toBool(), true);
    }

    // Per-cell char cap: a single cell longer than kMaxSqlCells (1024) is
    // truncated with a marker, so one cell can't exfiltrate a whole file.
    void run_sql_caps_cell_length() {
        m_sqlRows = 1;
        m_sqlCellLen = 5000;   // > 1024
        QLocalSocket s;
        QVERIFY(connectClient(s));
        readGreeting(s);
        QJsonObject a;
        a[QStringLiteral("sql")] = QStringLiteral("SELECT * FROM big");
        const QJsonObject r = call(s, 151, QStringLiteral("run_sql"), a);
        QVERIFY(r.value(QLatin1String("ok")).toBool());
        const QJsonObject res = r.value(QLatin1String("result")).toObject();
        const QJsonArray rows = res.value(QLatin1String("rows")).toArray();
        QCOMPARE(rows.size(), 1);
        const QString cell = rows.at(0).toArray().at(1).toString();
        QVERIFY2(cell.size() < 5000, "cell was not truncated");
        QVERIFY2(cell.endsWith(QStringLiteral("[truncated]")),
                 "truncation marker missing");
        // 1024 kept chars + ellipsis + "[truncated]".
        QCOMPARE(cell.size(), 1024 + 1 + int(qstrlen("[truncated]")));
        QVERIFY(res.value(QLatin1String("truncated")).toBool());
    }

    void open_note_and_containment() {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        m_notesRoot = dir.path();
        const QString note =
            writeNote(dir.path() + QStringLiteral("/Inbox/a.html"),
                      QStringLiteral("Alpha"), QStringLiteral("body"));
        QVERIFY(!note.isEmpty());
        QLocalSocket s;
        QVERIFY(connectClient(s));
        readGreeting(s);
        QJsonObject a;
        a[QStringLiteral("file")] = note;
        QJsonObject r = call(s, 160, QStringLiteral("open_note"), a);
        QVERIFY(r.value(QLatin1String("ok")).toBool());
        QCOMPARE(r.value(QLatin1String("result"))
                     .toObject()
                     .value(QLatin1String("opened"))
                     .toBool(),
                 true);
        QCOMPARE(m_openedNote, QFileInfo(note).canonicalFilePath());
        // Escape outside root → rejected, host untouched.
        QTemporaryDir outside;
        QVERIFY(outside.isValid());
        const QString stray = outside.path() + QStringLiteral("/evil.html");
        {
            QFile f(stray);
            QVERIFY(f.open(QIODevice::WriteOnly));
            f.write("<html></html>");
        }
        m_openedNote.clear();
        QJsonObject bad;
        bad[QStringLiteral("file")] = stray;
        r = call(s, 161, QStringLiteral("open_note"), bad);
        QCOMPARE(r.value(QLatin1String("ok")).toBool(), false);
        QVERIFY(r.value(QLatin1String("error"))
                    .toString()
                    .contains(QLatin1String("not a note file")));
        QVERIFY(m_openedNote.isEmpty());
        // Missing file arg.
        r = call(s, 162, QStringLiteral("open_note"));
        QCOMPARE(r.value(QLatin1String("ok")).toBool(), false);
    }

    void create_note_approve_and_validation() {
        QLocalSocket s;
        QVERIFY(connectClient(s));
        readGreeting(s);
        QJsonObject a;
        a[QStringLiteral("title")] = QStringLiteral("Meeting X");
        a[QStringLiteral("body")] = QStringLiteral("line one\nline two");
        sendRequest(s, 170, QStringLiteral("create_note"), a);
        QFrame *card = waitForCard();
        QVERIFY(card);
        auto *desc =
            card->findChild<QLabel *>(QStringLiteral("mcpApprovalDesc"));
        QVERIFY(desc);
        QVERIFY(desc->text().contains(
            QLatin1String("create note 'Meeting X'")));
        QVERIFY(desc->text().contains(QLatin1String("chars)")));
        // Held: no response, no host mutation until Approve.
        QTest::qWait(120);
        QVERIFY(!s.canReadLine());
        QVERIFY(m_lastCreatedTitle.isEmpty());
        card->findChild<QPushButton *>(QStringLiteral("mcpApproveBtn"))->click();
        const QJsonObject r = readObj(s);
        QCOMPARE(r.value(QLatin1String("id")).toInt(), 170);
        QVERIFY(r.value(QLatin1String("ok")).toBool());
        const QJsonObject res = r.value(QLatin1String("result")).toObject();
        QCOMPARE(res.value(QLatin1String("title")).toString(),
                 QStringLiteral("Meeting X"));
        QVERIFY(!res.value(QLatin1String("file")).toString().isEmpty());
        QCOMPARE(m_lastCreatedTitle, QStringLiteral("Meeting X"));
        QCOMPARE(m_lastCreatedBody, QStringLiteral("line one\nline two"));
        QVERIFY(waitForCardGone());
        // Missing title → fails fast, NO card.
        QJsonObject noTitle;
        noTitle[QStringLiteral("body")] = QStringLiteral("x");
        const QJsonObject bad =
            call(s, 171, QStringLiteral("create_note"), noTitle);
        QCOMPARE(bad.value(QLatin1String("ok")).toBool(), false);
        QTest::qWait(80);
        QVERIFY(!findCard());
    }

    void append_note_approve_deny_escape() {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        m_notesRoot = dir.path();
        const QString note =
            writeNote(dir.path() + QStringLiteral("/Inbox/n.html"),
                      QStringLiteral("NoteA"), QStringLiteral("b"));
        QVERIFY(!note.isEmpty());
        QLocalSocket s;
        QVERIFY(connectClient(s));
        readGreeting(s);
        QJsonObject a;
        a[QStringLiteral("file")] = note;
        a[QStringLiteral("text")] = QStringLiteral("appended text"); // 13 chars
        sendRequest(s, 180, QStringLiteral("append_note"), a);
        QFrame *card = waitForCard();
        QVERIFY(card);
        auto *desc =
            card->findChild<QLabel *>(QStringLiteral("mcpApprovalDesc"));
        QVERIFY(desc);
        QVERIFY(desc->text().contains(
            QLatin1String("append 13 chars to 'NoteA'")));
        card->findChild<QPushButton *>(QStringLiteral("mcpApproveBtn"))->click();
        const QJsonObject r = readObj(s);
        QVERIFY(r.value(QLatin1String("ok")).toBool());
        QCOMPARE(m_appendedText, QStringLiteral("appended text"));
        QCOMPARE(m_appendedTo, QFileInfo(note).canonicalFilePath());
        QVERIFY(waitForCardGone());
        // Escape → rejected, NO card, host untouched.
        QTemporaryDir outside;
        QVERIFY(outside.isValid());
        const QString stray = outside.path() + QStringLiteral("/e.html");
        {
            QFile f(stray);
            QVERIFY(f.open(QIODevice::WriteOnly));
            f.write("<html></html>");
        }
        m_appendedTo.clear();
        QJsonObject esc;
        esc[QStringLiteral("file")] = stray;
        esc[QStringLiteral("text")] = QStringLiteral("x");
        QJsonObject bad = call(s, 181, QStringLiteral("append_note"), esc);
        QCOMPARE(bad.value(QLatin1String("ok")).toBool(), false);
        QVERIFY(bad.value(QLatin1String("error"))
                    .toString()
                    .contains(QLatin1String("not a note file")));
        QVERIFY(m_appendedTo.isEmpty());
        QTest::qWait(80);
        QVERIFY(!findCard());
        // Missing text → no card.
        QJsonObject noText;
        noText[QStringLiteral("file")] = note;
        bad = call(s, 182, QStringLiteral("append_note"), noText);
        QCOMPARE(bad.value(QLatin1String("ok")).toBool(), false);
    }

    void set_reminder_approve_and_validation() {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        m_notesRoot = dir.path();
        const QString note =
            writeNote(dir.path() + QStringLiteral("/Inbox/r.html"),
                      QStringLiteral("RemNote"), QStringLiteral("b"));
        QVERIFY(!note.isEmpty());
        QLocalSocket s;
        QVERIFY(connectClient(s));
        readGreeting(s);
        const QDateTime future = QDateTime::currentDateTime().addDays(2);
        QJsonObject a;
        a[QStringLiteral("file")] = note;
        a[QStringLiteral("due_iso")] = future.toString(Qt::ISODate);
        sendRequest(s, 190, QStringLiteral("set_reminder"), a);
        QFrame *card = waitForCard();
        QVERIFY(card);
        auto *desc =
            card->findChild<QLabel *>(QStringLiteral("mcpApprovalDesc"));
        QVERIFY(desc);
        QVERIFY(desc->text().contains(
            QLatin1String("set reminder on 'RemNote' for")));
        card->findChild<QPushButton *>(QStringLiteral("mcpApproveBtn"))->click();
        const QJsonObject r = readObj(s);
        QVERIFY(r.value(QLatin1String("ok")).toBool());
        QCOMPARE(m_reminderFile, QFileInfo(note).canonicalFilePath());
        QVERIFY(m_reminderDue.isValid());
        QVERIFY(waitForCardGone());
        // Past due → rejected, no card.
        QJsonObject past;
        past[QStringLiteral("file")] = note;
        past[QStringLiteral("due_iso")] =
            QDateTime::currentDateTime().addDays(-1).toString(Qt::ISODate);
        QJsonObject bad = call(s, 191, QStringLiteral("set_reminder"), past);
        QCOMPARE(bad.value(QLatin1String("ok")).toBool(), false);
        QVERIFY(bad.value(QLatin1String("error"))
                    .toString()
                    .contains(QLatin1String("future")));
        QTest::qWait(80);
        QVERIFY(!findCard());
        // Invalid due_iso.
        QJsonObject inv;
        inv[QStringLiteral("file")] = note;
        inv[QStringLiteral("due_iso")] = QStringLiteral("not-a-date");
        bad = call(s, 192, QStringLiteral("set_reminder"), inv);
        QCOMPARE(bad.value(QLatin1String("ok")).toBool(), false);
        QVERIFY(bad.value(QLatin1String("error"))
                    .toString()
                    .contains(QLatin1String("invalid due_iso")));
        // Missing due_iso.
        QJsonObject nod;
        nod[QStringLiteral("file")] = note;
        bad = call(s, 193, QStringLiteral("set_reminder"), nod);
        QCOMPARE(bad.value(QLatin1String("ok")).toBool(), false);
    }

    void export_diagram_approve_and_validation() {
        m_fakeTabs.append({QStringLiteral("d.npd"), QString(),
                           QStringLiteral("node a (Start)\n"), false,
                           QStringLiteral("Plain Text"), true});
        const int di = m_fakeTabs.size() - 1;
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString outPath = dir.path() + QStringLiteral("/out.png");
        QLocalSocket s;
        QVERIFY(connectClient(s));
        readGreeting(s);
        QJsonObject a;
        a[QStringLiteral("tab_index")] = di;
        a[QStringLiteral("path")] = outPath;
        a[QStringLiteral("format")] = QStringLiteral("png");
        sendRequest(s, 200, QStringLiteral("export_diagram"), a);
        QFrame *card = waitForCard();
        QVERIFY(card);
        auto *desc =
            card->findChild<QLabel *>(QStringLiteral("mcpApprovalDesc"));
        QVERIFY(desc);
        QVERIFY(desc->text().contains(QLatin1String("export 'd.npd' to")));
        card->findChild<QPushButton *>(QStringLiteral("mcpApproveBtn"))->click();
        const QJsonObject r = readObj(s);
        QVERIFY(r.value(QLatin1String("ok")).toBool());
        QCOMPARE(m_exportedFormat, QStringLiteral("png"));
        QCOMPARE(QDir::fromNativeSeparators(m_exportedPath), outPath);
        QVERIFY(waitForCardGone());
        // Non-diagram tab → error, no card.
        QJsonObject nd;
        nd[QStringLiteral("tab_index")] = 0;
        nd[QStringLiteral("path")] = outPath;
        nd[QStringLiteral("format")] = QStringLiteral("png");
        QJsonObject bad = call(s, 201, QStringLiteral("export_diagram"), nd);
        QCOMPARE(bad.value(QLatin1String("ok")).toBool(), false);
        QVERIFY(bad.value(QLatin1String("error"))
                    .toString()
                    .contains(QLatin1String("not a diagram")));
        // Relative path → error.
        QJsonObject rel;
        rel[QStringLiteral("tab_index")] = di;
        rel[QStringLiteral("path")] = QStringLiteral("out.png");
        rel[QStringLiteral("format")] = QStringLiteral("png");
        bad = call(s, 202, QStringLiteral("export_diagram"), rel);
        QCOMPARE(bad.value(QLatin1String("ok")).toBool(), false);
        QVERIFY(bad.value(QLatin1String("error"))
                    .toString()
                    .contains(QLatin1String("absolute")));
        // Missing parent directory → error.
        QJsonObject nopar;
        nopar[QStringLiteral("tab_index")] = di;
        nopar[QStringLiteral("path")] =
            dir.path() + QStringLiteral("/nope/out.png");
        nopar[QStringLiteral("format")] = QStringLiteral("png");
        bad = call(s, 203, QStringLiteral("export_diagram"), nopar);
        QCOMPARE(bad.value(QLatin1String("ok")).toBool(), false);
        QVERIFY(bad.value(QLatin1String("error"))
                    .toString()
                    .contains(QLatin1String("parent directory")));
        // Unsupported format → error.
        QJsonObject badfmt;
        badfmt[QStringLiteral("tab_index")] = di;
        badfmt[QStringLiteral("path")] = outPath;
        badfmt[QStringLiteral("format")] = QStringLiteral("gif");
        bad = call(s, 204, QStringLiteral("export_diagram"), badfmt);
        QCOMPARE(bad.value(QLatin1String("ok")).toBool(), false);
        QVERIFY(bad.value(QLatin1String("error"))
                    .toString()
                    .contains(QLatin1String("unsupported format")));
        QTest::qWait(80);
        QVERIFY(!findCard());
    }

    // Approval card flags an overwrite of an existing destination file
    // (display text only — the executed path is unchanged).
    void export_diagram_card_flags_overwrite() {
        m_fakeTabs.append({QStringLiteral("d.npd"), QString(),
                           QStringLiteral("node a (Start)\n"), false,
                           QStringLiteral("Plain Text"), true});
        const int di = m_fakeTabs.size() - 1;
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString outPath = dir.path() + QStringLiteral("/exists.png");
        {   // pre-create the destination so it's an OVERWRITE
            QFile f(outPath);
            QVERIFY(f.open(QIODevice::WriteOnly));
            f.write("stale");
        }
        QLocalSocket s;
        QVERIFY(connectClient(s));
        readGreeting(s);
        QJsonObject a;
        a[QStringLiteral("tab_index")] = di;
        a[QStringLiteral("path")] = outPath;
        a[QStringLiteral("format")] = QStringLiteral("png");
        sendRequest(s, 205, QStringLiteral("export_diagram"), a);
        QFrame *card = waitForCard();
        QVERIFY(card);
        auto *desc =
            card->findChild<QLabel *>(QStringLiteral("mcpApprovalDesc"));
        QVERIFY(desc);
        QVERIFY2(desc->text().contains(
                     QLatin1String("OVERWRITE existing file:")),
                 qPrintable(desc->text()));
        card->findChild<QPushButton *>(QStringLiteral("mcpApproveBtn"))->click();
        const QJsonObject r = readObj(s);
        QVERIFY(r.value(QLatin1String("ok")).toBool());
        // The EXECUTED path is the exact absolute path, not the card string.
        QCOMPARE(QDir::fromNativeSeparators(m_exportedPath), outPath);
        QVERIFY(waitForCardGone());
    }

    void create_diagram_creates_tab_and_reports_validity() {
        QLocalSocket s;
        QVERIFY(connectClient(s));
        readGreeting(s);
        const int before = m_fakeTabs.size();
        QJsonObject a;
        a[QStringLiteral("source")] = QStringLiteral("node a (Start)\n");
        a[QStringLiteral("title")] = QStringLiteral("flow.npd");
        QJsonObject r = call(s, 300, QStringLiteral("create_diagram"), a);
        QVERIFY(r.value(QLatin1String("ok")).toBool());
        QJsonObject res = r.value(QLatin1String("result")).toObject();
        QCOMPARE(res.value(QLatin1String("tab_index")).toInt(), before);
        QCOMPARE(res.value(QLatin1String("valid")).toBool(), true);
        QCOMPARE(res.value(QLatin1String("errors")).toArray().size(), 0);
        QVERIFY(m_fakeTabs.at(before).isDiagram);   // exportable by export_diagram
        QCOMPARE(m_fakeTabs.at(before).title, QStringLiteral("flow.npd"));
        QCOMPARE(m_currentIndex, before);           // focused
        // Invalid source STILL creates the tab, with errors reported.
        QJsonObject b;
        b[QStringLiteral("source")] =
            QStringLiteral("node a (Start)\nicon x \"NoColon\"\n");
        r = call(s, 301, QStringLiteral("create_diagram"), b);
        QVERIFY(r.value(QLatin1String("ok")).toBool());
        res = r.value(QLatin1String("result")).toObject();
        QCOMPARE(res.value(QLatin1String("tab_index")).toInt(), before + 1);
        QCOMPARE(res.value(QLatin1String("valid")).toBool(), false);
        QVERIFY(res.value(QLatin1String("errors")).toArray().size() >= 1);
        // No source: tab still created; valid/errors keys always present.
        r = call(s, 302, QStringLiteral("create_diagram"));
        QVERIFY(r.value(QLatin1String("ok")).toBool());
        res = r.value(QLatin1String("result")).toObject();
        QCOMPARE(res.value(QLatin1String("tab_index")).toInt(), before + 2);
        QVERIFY(res.contains(QLatin1String("valid")));
        QVERIFY(res.contains(QLatin1String("errors")));
    }

    void get_diagram_source_round_trip() {
        m_fakeTabs.append({QStringLiteral("d.npd"), QString(),
                           QStringLiteral("node a (Start)\n"), false,
                           QStringLiteral("Plain Text"), true});
        const int di = m_fakeTabs.size() - 1;
        QLocalSocket s;
        QVERIFY(connectClient(s));
        readGreeting(s);
        QJsonObject a;
        a[QStringLiteral("tab_index")] = di;
        QJsonObject r = call(s, 310, QStringLiteral("get_diagram_source"), a);
        QVERIFY(r.value(QLatin1String("ok")).toBool());
        QCOMPARE(r.value(QLatin1String("result")).toObject()
                     .value(QLatin1String("source")).toString(),
                 QStringLiteral("node a (Start)\n"));
        // Non-diagram tab → error.
        QJsonObject nd;
        nd[QStringLiteral("tab_index")] = 0;
        r = call(s, 311, QStringLiteral("get_diagram_source"), nd);
        QCOMPARE(r.value(QLatin1String("ok")).toBool(), false);
        QVERIFY(r.value(QLatin1String("error")).toString()
                    .contains(QLatin1String("not a diagram")));
        // Out of range and missing tab_index → errors.
        QJsonObject oob;
        oob[QStringLiteral("tab_index")] = 99;
        r = call(s, 312, QStringLiteral("get_diagram_source"), oob);
        QCOMPARE(r.value(QLatin1String("ok")).toBool(), false);
        r = call(s, 313, QStringLiteral("get_diagram_source"));
        QCOMPARE(r.value(QLatin1String("ok")).toBool(), false);
        QVERIFY(r.value(QLatin1String("error")).toString()
                    .contains(QLatin1String("missing tab_index")));
    }

    void set_diagram_source_approve_deny_and_validation() {
        m_fakeTabs.append({QStringLiteral("d.npd"), QString(),
                           QStringLiteral("node a (Start)\n"), false,
                           QStringLiteral("Plain Text"), true});
        const int di = m_fakeTabs.size() - 1;
        QLocalSocket s;
        QVERIFY(connectClient(s));
        readGreeting(s);
        // Approve → source replaced, result carries ok/valid.
        QJsonObject a;
        a[QStringLiteral("tab_index")] = di;
        a[QStringLiteral("source")] = QStringLiteral("node b [Process]\n");
        sendRequest(s, 320, QStringLiteral("set_diagram_source"), a);
        QFrame *card = waitForCard();
        QVERIFY(card);
        auto *desc =
            card->findChild<QLabel *>(QStringLiteral("mcpApprovalDesc"));
        QVERIFY(desc);
        QVERIFY(desc->text().contains(
            QLatin1String("REPLACE the diagram source of 'd.npd'")));
        // Held: no mutation before Approve.
        QCOMPARE(m_fakeTabs.at(di).text, QStringLiteral("node a (Start)\n"));
        card->findChild<QPushButton *>(QStringLiteral("mcpApproveBtn"))->click();
        QJsonObject r = readObj(s);
        QVERIFY(r.value(QLatin1String("ok")).toBool());
        QJsonObject res = r.value(QLatin1String("result")).toObject();
        QCOMPARE(res.value(QLatin1String("ok")).toBool(), true);
        QCOMPARE(res.value(QLatin1String("tab_index")).toInt(), di);
        QCOMPARE(res.value(QLatin1String("valid")).toBool(), true);
        QCOMPARE(m_fakeTabs.at(di).text, QStringLiteral("node b [Process]\n"));
        QVERIFY(waitForCardGone());
        // Deny → verbatim error, source unchanged.
        QJsonObject d;
        d[QStringLiteral("tab_index")] = di;
        d[QStringLiteral("source")] = QStringLiteral("node c (Other)\n");
        sendRequest(s, 321, QStringLiteral("set_diagram_source"), d);
        card = waitForCard();
        QVERIFY(card);
        card->findChild<QPushButton *>(QStringLiteral("mcpDenyBtn"))->click();
        r = readObj(s);
        QCOMPARE(r.value(QLatin1String("ok")).toBool(), false);
        QCOMPARE(r.value(QLatin1String("error")).toString(),
                 QStringLiteral("denied by user"));
        QCOMPARE(m_fakeTabs.at(di).text, QStringLiteral("node b [Process]\n"));
        QVERIFY(waitForCardGone());
        // Non-diagram tab → immediate error, NO card.
        QJsonObject nd;
        nd[QStringLiteral("tab_index")] = 0;
        nd[QStringLiteral("source")] = QStringLiteral("x");
        r = call(s, 322, QStringLiteral("set_diagram_source"), nd);
        QCOMPARE(r.value(QLatin1String("ok")).toBool(), false);
        QVERIFY(r.value(QLatin1String("error")).toString()
                    .contains(QLatin1String("not a diagram")));
        // Missing source → immediate error, NO card.
        QJsonObject ns;
        ns[QStringLiteral("tab_index")] = di;
        r = call(s, 323, QStringLiteral("set_diagram_source"), ns);
        QCOMPARE(r.value(QLatin1String("ok")).toBool(), false);
        QTest::qWait(80);
        QVERIFY(!findCard());
    }

    void open_noter_reveals_panel() {
        QLocalSocket s;
        QVERIFY(connectClient(s));
        readGreeting(s);
        const QJsonObject r = call(s, 330, QStringLiteral("open_noter"));
        QVERIFY(r.value(QLatin1String("ok")).toBool());
        QCOMPARE(r.value(QLatin1String("result")).toObject()
                     .value(QLatin1String("opened")).toBool(), true);
        QVERIFY(m_noterOpened);
    }

    void find_in_tab_regex_flag() {
        // tab 0 text: "hello world\nSecond Line with Needle\n".
        QLocalSocket s;
        QVERIFY(connectClient(s));
        readGreeting(s);
        QJsonObject a;
        a[QStringLiteral("query")] = QStringLiteral("S.*Needle");
        a[QStringLiteral("index")] = 0;
        a[QStringLiteral("regex")] = true;
        QJsonObject r = call(s, 210, QStringLiteral("find_in_tab"), a);
        QVERIFY(r.value(QLatin1String("ok")).toBool());
        QCOMPARE(r.value(QLatin1String("result"))
                     .toObject()
                     .value(QLatin1String("matches"))
                     .toArray()
                     .size(),
                 1);
        // The SAME text as a literal (regex off) matches nothing.
        QJsonObject lit;
        lit[QStringLiteral("query")] = QStringLiteral("S.*Needle");
        lit[QStringLiteral("index")] = 0;
        r = call(s, 211, QStringLiteral("find_in_tab"), lit);
        QVERIFY(r.value(QLatin1String("ok")).toBool());
        QCOMPARE(r.value(QLatin1String("result"))
                     .toObject()
                     .value(QLatin1String("matches"))
                     .toArray()
                     .size(),
                 0);
        // Invalid pattern → fail-fast error.
        QJsonObject bad;
        bad[QStringLiteral("query")] = QStringLiteral("(unterminated");
        bad[QStringLiteral("index")] = 0;
        bad[QStringLiteral("regex")] = true;
        r = call(s, 212, QStringLiteral("find_in_tab"), bad);
        QCOMPARE(r.value(QLatin1String("ok")).toBool(), false);
        QVERIFY(r.value(QLatin1String("error"))
                    .toString()
                    .contains(QLatin1String("invalid regex")));
    }

    void search_project_regex_flag() {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        {
            QFile f(dir.path() + QStringLiteral("/a.txt"));
            QVERIFY(f.open(QIODevice::WriteOnly));
            f.write("alpha\nfoo123bar\n");
        }
        m_root = dir.path();
        m_fakeTabs.clear(); // only the file leg should match
        QLocalSocket s;
        QVERIFY(connectClient(s));
        readGreeting(s);
        QJsonObject a;
        a[QStringLiteral("query")] = QStringLiteral("foo[0-9]+bar");
        a[QStringLiteral("regex")] = true;
        QJsonObject r = call(s, 220, QStringLiteral("search_project"), a);
        QVERIFY(r.value(QLatin1String("ok")).toBool());
        const QJsonArray results = r.value(QLatin1String("result"))
                                       .toObject()
                                       .value(QLatin1String("results"))
                                       .toArray();
        bool saw = false;
        for (const QJsonValue &v : results)
            if (v.toObject()
                    .value(QLatin1String("text"))
                    .toString()
                    .contains(QLatin1String("foo123bar")))
                saw = true;
        QVERIFY(saw);
        // Invalid pattern rejected BEFORE any worker is dispatched.
        QJsonObject bad;
        bad[QStringLiteral("query")] = QStringLiteral("[unterminated");
        bad[QStringLiteral("regex")] = true;
        r = call(s, 221, QStringLiteral("search_project"), bad);
        QCOMPARE(r.value(QLatin1String("ok")).toBool(), false);
        QVERIFY(r.value(QLatin1String("error"))
                    .toString()
                    .contains(QLatin1String("invalid regex")));
    }

    void new_verbs_report_not_supported_without_host() {
        // A bridge with a bare host refuses each host-backed verb with a
        // clear error — and the WRITE verbs never show a card.
        McpEditorHost bare;
        bare.tabCount = [] { return 0; };
        const QString name = QStringLiteral("notepatra-mcp-bare-%1-%2")
                                 .arg(QCoreApplication::applicationPid())
                                 .arg(++m_seq);
        McpBridge bridge(bare, this, name);
        QVERIFY(bridge.isListening());
        QLocalSocket s;
        s.connectToServer(name);
        QElapsedTimer t;
        t.start();
        while (s.state() != QLocalSocket::ConnectedState &&
               t.elapsed() < kWaitMs)
            QTest::qWait(10);
        QVERIFY(s.state() == QLocalSocket::ConnectedState);
        readGreeting(s);
        const QStringList verbs = {
            QStringLiteral("git_status"),   QStringLiteral("run_sql"),
            QStringLiteral("open_note"),    QStringLiteral("create_note"),
            QStringLiteral("append_note"),  QStringLiteral("set_reminder"),
            QStringLiteral("export_diagram"), QStringLiteral("create_diagram"),
            QStringLiteral("set_diagram_source"), QStringLiteral("open_noter"),
            // phase 2 — every host-backed verb refuses on a bare host; the
            // chart verbs answer with the friendly Full/WebEngine gate.
            QStringLiteral("run_query"), QStringLiteral("list_tables"),
            QStringLiteral("open_data_analyst"),
            QStringLiteral("export_query_results"),
            QStringLiteral("render_chart"), QStringLiteral("export_chart")};
        int id = 230;
        for (const QString &v : verbs) {
            QJsonObject args;
            args[QStringLiteral("sql")] = QStringLiteral("SELECT 1");
            args[QStringLiteral("title")] = QStringLiteral("t");
            const QJsonObject r = call(s, id++, v, args);
            QCOMPARE(r.value(QLatin1String("ok")).toBool(), false);
            QVERIFY(!r.value(QLatin1String("error")).toString().isEmpty());
        }
        // p0a verbs degrade to empty/false on a bare host, never error.
        QJsonObject r = call(s, id++, QStringLiteral("list_languages"));
        QVERIFY(r.value(QLatin1String("ok")).toBool());
        QCOMPARE(r.value(QLatin1String("result")).toObject()
                     .value(QLatin1String("languages")).toArray().size(), 0);
        r = call(s, id++, QStringLiteral("get_capabilities"));
        QVERIFY(r.value(QLatin1String("ok")).toBool());
        QCOMPARE(r.value(QLatin1String("result")).toObject()
                     .value(QLatin1String("features")).toObject()
                     .value(QLatin1String("noter")).toBool(), false);
    }

    // ── Phase 2 — Data-analyst + Charts ─────────────────────────────

    void list_connections_empty_and_sanitized() {
        QLocalSocket s;
        QVERIFY(connectClient(s));
        readGreeting(s);
        // Empty list when no connections are saved.
        QJsonObject r = call(s, 300, QStringLiteral("list_connections"));
        QVERIFY(r.value(QLatin1String("ok")).toBool());
        QCOMPARE(r.value(QLatin1String("result")).toObject()
                     .value(QLatin1String("connections")).toArray().size(), 0);
        // Populate one entry; keys are exactly name/driver/database/read_only.
        QJsonObject c;
        c[QStringLiteral("name")] = QStringLiteral("pg1");
        c[QStringLiteral("driver")] = QStringLiteral("QPSQL");
        c[QStringLiteral("database")] = QStringLiteral("analytics");
        c[QStringLiteral("read_only")] = true;
        m_connections.append(c);
        r = call(s, 301, QStringLiteral("list_connections"));
        const QJsonArray conns = r.value(QLatin1String("result")).toObject()
                                     .value(QLatin1String("connections")).toArray();
        QCOMPARE(conns.size(), 1);
        const QJsonObject o = conns.at(0).toObject();
        QCOMPARE(o.value(QLatin1String("name")).toString(), QStringLiteral("pg1"));
        QCOMPARE(o.value(QLatin1String("read_only")).toBool(), true);
        QVERIFY(!o.contains(QLatin1String("password")));
    }

    void run_query_rejects_mutations_without_a_card() {
        QLocalSocket s;
        QVERIFY(connectClient(s));
        readGreeting(s);
        QJsonObject a;
        a[QStringLiteral("connection_name")] = QStringLiteral("t1");
        a[QStringLiteral("sql")] = QStringLiteral("DELETE FROM t");
        const QJsonObject r = call(s, 310, QStringLiteral("run_query"), a);
        QCOMPARE(r.value(QLatin1String("ok")).toBool(), false);
        QVERIFY(!r.value(QLatin1String("error")).toString().isEmpty());
        // READ verb: never a card.
        QTest::qWait(80);
        QVERIFY(!findCard());
    }

    // Regression: run_query reaches saved MySQL/Postgres connections, so the
    // restrictFilesystem denylist must cover their file-read functions, not
    // just DuckDB's. Each is a read-only SELECT by SQL semantics but reads a
    // server-side file — must be rejected pre-card ("Read-only SQL ≠ file-safe").
    void run_query_rejects_filesystem_reads_across_engines() {
        QLocalSocket s;
        QVERIFY(connectClient(s));
        readGreeting(s);
        const QStringList banned = {
            QStringLiteral("SELECT LOAD_FILE('/etc/passwd')"),        // MySQL
            QStringLiteral("SELECT pg_read_server_files"),            // PG (col ok)
            QStringLiteral("SELECT pg_stat_file('/etc/passwd')"),     // Postgres
            QStringLiteral("SELECT pg_ls_dir('/')"),                  // Postgres
            QStringLiteral("SELECT * FROM read_text('/etc/passwd')"), // DuckDB
        };
        int idn = 340;
        for (const QString &sql : banned) {
            QJsonObject a;
            a[QStringLiteral("connection_name")] = QStringLiteral("t1");
            a[QStringLiteral("sql")] = sql;
            const QJsonObject r =
                call(s, idn++, QStringLiteral("run_query"), a);
            if (sql.contains(QStringLiteral("pg_read_server_files"))) {
                // Bare column reference (no call form) is a benign read; it must
                // NOT be rejected as a filesystem read. It fails later only for
                // the unknown fixture connection, never as a fs-read.
                const QString err =
                    r.value(QLatin1String("error")).toString();
                QVERIFY(!err.contains(QStringLiteral("filesystem")));
            } else {
                QCOMPARE(r.value(QLatin1String("ok")).toBool(), false);
                QVERIFY(!r.value(QLatin1String("error")).toString().isEmpty());
            }
        }
        // READ verb: never a card, even on rejection.
        QTest::qWait(80);
        QVERIFY(!findCard());
    }

    void run_query_select_via_real_sqlite() {
        if (!QSqlDatabase::isDriverAvailable(QStringLiteral("QSQLITE")))
            QSKIP("QSQLITE driver not present");
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        makeSqliteFixture(dir, QStringLiteral("t1"), 3);
        QLocalSocket s;
        QVERIFY(connectClient(s));
        readGreeting(s);
        QJsonObject a;
        a[QStringLiteral("connection_name")] = QStringLiteral("t1");
        a[QStringLiteral("sql")] = QStringLiteral("SELECT id, name FROM t");
        QJsonObject r = call(s, 320, QStringLiteral("run_query"), a);
        QVERIFY(r.value(QLatin1String("ok")).toBool());
        const QJsonObject res = r.value(QLatin1String("result")).toObject();
        QCOMPARE(res.value(QLatin1String("columns")).toArray().size(), 2);
        QCOMPARE(res.value(QLatin1String("columns")).toArray().at(0).toString(),
                 QStringLiteral("id"));
        QCOMPARE(res.value(QLatin1String("rows")).toArray().size(), 3);
        QCOMPARE(res.value(QLatin1String("engine")).toString(),
                 QStringLiteral("sqlite"));
        // 250 rows, no max_rows → exactly 200 rows + truncated.
        QTemporaryDir dir2;
        QVERIFY(dir2.isValid());
        makeSqliteFixture(dir2, QStringLiteral("t2"), 250);
        QJsonObject a2;
        a2[QStringLiteral("connection_name")] = QStringLiteral("t2");
        a2[QStringLiteral("sql")] = QStringLiteral("SELECT id, name FROM t");
        r = call(s, 321, QStringLiteral("run_query"), a2);
        const QJsonObject res2 = r.value(QLatin1String("result")).toObject();
        QCOMPARE(res2.value(QLatin1String("rows")).toArray().size(), 200);
        QCOMPARE(res2.value(QLatin1String("truncated")).toBool(), true);
    }

    void list_tables_roundtrip_and_unknown_connection() {
        if (!QSqlDatabase::isDriverAvailable(QStringLiteral("QSQLITE")))
            QSKIP("QSQLITE driver not present");
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        makeSqliteFixture(dir, QStringLiteral("t1"), 1);
        QLocalSocket s;
        QVERIFY(connectClient(s));
        readGreeting(s);
        QJsonObject a;
        a[QStringLiteral("connection_name")] = QStringLiteral("t1");
        QJsonObject r = call(s, 330, QStringLiteral("list_tables"), a);
        QVERIFY(r.value(QLatin1String("ok")).toBool());
        const QJsonArray tables = r.value(QLatin1String("result")).toObject()
                                      .value(QLatin1String("tables")).toArray();
        bool sawT = false;
        for (const QJsonValue &v : tables)
            if (v.toString() == QLatin1String("t")) sawT = true;
        QVERIFY(sawT);
        // Unknown connection → error.
        QJsonObject bad;
        bad[QStringLiteral("connection_name")] = QStringLiteral("nope");
        r = call(s, 331, QStringLiteral("list_tables"), bad);
        QCOMPARE(r.value(QLatin1String("ok")).toBool(), false);
        QVERIFY(r.value(QLatin1String("error")).toString()
                    .contains(QLatin1String("no connection named: nope")));
    }

    void open_data_analyst_flag_and_unset_host() {
        QLocalSocket s;
        QVERIFY(connectClient(s));
        readGreeting(s);
        QJsonObject r = call(s, 340, QStringLiteral("open_data_analyst"));
        QVERIFY(r.value(QLatin1String("ok")).toBool());
        QCOMPARE(r.value(QLatin1String("result")).toObject()
                     .value(QLatin1String("opened")).toBool(), true);
        QVERIFY(m_dataAnalystOpened);
        // Unset host field → clear error.
        McpEditorHost bare;
        bare.tabCount = [] { return 0; };
        const QString name = QStringLiteral("notepatra-mcp-oda-%1-%2")
                                 .arg(QCoreApplication::applicationPid())
                                 .arg(++m_seq);
        McpBridge bridge(bare, this, name);
        QVERIFY(bridge.isListening());
        QLocalSocket s2;
        s2.connectToServer(name);
        QElapsedTimer t;
        t.start();
        while (s2.state() != QLocalSocket::ConnectedState &&
               t.elapsed() < kWaitMs)
            QTest::qWait(10);
        QVERIFY(s2.state() == QLocalSocket::ConnectedState);
        readGreeting(s2);
        r = call(s2, 341, QStringLiteral("open_data_analyst"));
        QCOMPARE(r.value(QLatin1String("ok")).toBool(), false);
        QVERIFY(r.value(QLatin1String("error")).toString()
                    .contains(QLatin1String("not supported by host")));
    }

    void export_query_results_approve_writes_csv_deny_writes_nothing() {
        if (!QSqlDatabase::isDriverAvailable(QStringLiteral("QSQLITE")))
            QSKIP("QSQLITE driver not present");
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        makeSqliteFixture(dir, QStringLiteral("t1"), 3);
        QJsonObject conn;
        conn[QStringLiteral("name")] = QStringLiteral("t1");
        conn[QStringLiteral("driver")] = QStringLiteral("QSQLITE");
        conn[QStringLiteral("database")] = QStringLiteral("t1.db");
        conn[QStringLiteral("read_only")] = true;
        m_connections.append(conn);
        const QString csvPath = dir.path() + QStringLiteral("/out.csv");
        QLocalSocket s;
        QVERIFY(connectClient(s));
        readGreeting(s);
        // Approve → CSV written.
        QJsonObject a;
        a[QStringLiteral("connection_name")] = QStringLiteral("t1");
        a[QStringLiteral("sql")] = QStringLiteral("SELECT id, name FROM t");
        a[QStringLiteral("path")] = csvPath;
        a[QStringLiteral("format")] = QStringLiteral("csv");
        sendRequest(s, 350, QStringLiteral("export_query_results"), a);
        QFrame *card = waitForCard();
        QVERIFY(card);
        auto *desc = card->findChild<QLabel *>(QStringLiteral("mcpApprovalDesc"));
        QVERIFY(desc);
        QVERIFY(desc->text().contains(QLatin1String("t1")));
        QVERIFY(desc->text().contains(QDir::toNativeSeparators(csvPath)));
        card->findChild<QPushButton *>(QStringLiteral("mcpApproveBtn"))->click();
        QJsonObject r = readObj(s);
        QVERIFY(r.value(QLatin1String("ok")).toBool());
        QCOMPARE(r.value(QLatin1String("result")).toObject()
                     .value(QLatin1String("rows")).toInt(), 3);
        QVERIFY(QFileInfo::exists(csvPath));
        {
            QFile f(csvPath);
            QVERIFY(f.open(QIODevice::ReadOnly));
            const QByteArray first = f.readLine().trimmed();
            QCOMPARE(QString::fromUtf8(first), QStringLiteral("id,name"));
        }
        QVERIFY(waitForCardGone());
        // JSON variant.
        const QString jsonPath = dir.path() + QStringLiteral("/out.json");
        QJsonObject aj = a;
        aj[QStringLiteral("path")] = jsonPath;
        aj[QStringLiteral("format")] = QStringLiteral("json");
        sendRequest(s, 351, QStringLiteral("export_query_results"), aj);
        QVERIFY(waitForCard());
        findCard()->findChild<QPushButton *>(QStringLiteral("mcpApproveBtn"))->click();
        r = readObj(s);
        QVERIFY(r.value(QLatin1String("ok")).toBool());
        {
            QFile f(jsonPath);
            QVERIFY(f.open(QIODevice::ReadOnly));
            const QJsonObject o = QJsonDocument::fromJson(f.readAll()).object();
            QVERIFY(o.contains(QLatin1String("columns")));
        }
        QVERIFY(waitForCardGone());
        // Deny → no file.
        const QString denyPath = dir.path() + QStringLiteral("/deny.csv");
        QJsonObject ad = a;
        ad[QStringLiteral("path")] = denyPath;
        sendRequest(s, 352, QStringLiteral("export_query_results"), ad);
        QVERIFY(waitForCard());
        findCard()->findChild<QPushButton *>(QStringLiteral("mcpDenyBtn"))->click();
        r = readObj(s);
        QCOMPARE(r.value(QLatin1String("ok")).toBool(), false);
        QCOMPARE(r.value(QLatin1String("error")).toString(),
                 QStringLiteral("denied by user"));
        QVERIFY(!QFileInfo::exists(denyPath));
        QVERIFY(waitForCardGone());
        // Fail-fast: bad format → error, no card.
        QJsonObject bf = a;
        bf[QStringLiteral("format")] = QStringLiteral("xml");
        bf[QStringLiteral("path")] = dir.path() + QStringLiteral("/x.xml");
        r = call(s, 353, QStringLiteral("export_query_results"), bf);
        QCOMPARE(r.value(QLatin1String("ok")).toBool(), false);
        QVERIFY(r.value(QLatin1String("error")).toString()
                    .contains(QLatin1String("unsupported format")));
        // Relative path → error.
        QJsonObject rel = a;
        rel[QStringLiteral("path")] = QStringLiteral("out.csv");
        r = call(s, 354, QStringLiteral("export_query_results"), rel);
        QCOMPARE(r.value(QLatin1String("ok")).toBool(), false);
        QVERIFY(r.value(QLatin1String("error")).toString()
                    .contains(QLatin1String("absolute")));
        // Mutation SQL → rejected, no card.
        QJsonObject mut = a;
        mut[QStringLiteral("sql")] = QStringLiteral("DELETE FROM t");
        mut[QStringLiteral("path")] = dir.path() + QStringLiteral("/m.csv");
        r = call(s, 355, QStringLiteral("export_query_results"), mut);
        QCOMPARE(r.value(QLatin1String("ok")).toBool(), false);
        QVERIFY(r.value(QLatin1String("error")).toString()
                    .contains(QLatin1String("query rejected")));
        // Unknown connection → error, no card.
        QJsonObject unk = a;
        unk[QStringLiteral("connection_name")] = QStringLiteral("nope");
        unk[QStringLiteral("path")] = dir.path() + QStringLiteral("/u.csv");
        r = call(s, 356, QStringLiteral("export_query_results"), unk);
        QCOMPARE(r.value(QLatin1String("ok")).toBool(), false);
        QVERIFY(r.value(QLatin1String("error")).toString()
                    .contains(QLatin1String("no connection named")));
        QTest::qWait(80);
        QVERIFY(!findCard());
    }

    void chart_verbs_webengine_gated() {
        // Default host: renderChart/exportChart fields unset ⇒ the bridge
        // answers the friendly Full/WebEngine gate PRE-CARD, no card ever.
        QLocalSocket s;
        QVERIFY(connectClient(s));
        readGreeting(s);
        QJsonObject spec;
        spec[QStringLiteral("mark")] = QStringLiteral("bar");
        QJsonObject rc;
        rc[QStringLiteral("spec")] = spec;
        QJsonObject r = call(s, 360, QStringLiteral("render_chart"), rc);
        QCOMPARE(r.value(QLatin1String("ok")).toBool(), false);
        QCOMPARE(r.value(QLatin1String("error")).toString(),
                 QStringLiteral("charts require the Full edition (WebEngine)"));
        QJsonObject ec;
        ec[QStringLiteral("spec")] = spec;
        ec[QStringLiteral("path")] = QStringLiteral("/tmp/np-chart.png");
        ec[QStringLiteral("format")] = QStringLiteral("png");
        r = call(s, 361, QStringLiteral("export_chart"), ec);
        QCOMPARE(r.value(QLatin1String("ok")).toBool(), false);
        QCOMPARE(r.value(QLatin1String("error")).toString(),
                 QStringLiteral("charts require the Full edition (WebEngine)"));
        QTest::qWait(80);
        QVERIFY(!findCard());
    }

    void render_chart_happy_path_stubbed() {
        useWebEngineHost();
        QLocalSocket s;
        QVERIFY(connectClient(s));
        readGreeting(s);
        QJsonObject spec;
        spec[QStringLiteral("mark")] = QStringLiteral("bar");
        QJsonObject a;
        a[QStringLiteral("spec")] = spec;
        a[QStringLiteral("title")] = QStringLiteral("sales");
        const QJsonObject r = call(s, 370, QStringLiteral("render_chart"), a);
        QVERIFY(r.value(QLatin1String("ok")).toBool());
        const QJsonObject res = r.value(QLatin1String("result")).toObject();
        QCOMPARE(res.value(QLatin1String("chart_id")).toString(),
                 QStringLiteral("test-chart-1"));
        QCOMPARE(res.value(QLatin1String("rendered")).toBool(), true);
        QCOMPARE(m_renderedTitle, QStringLiteral("sales"));
        // ACT: no card.
        QTest::qWait(80);
        QVERIFY(!findCard());
    }

    void export_chart_approve_and_deny_stubbed() {
        useWebEngineHost();
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString pngPath = dir.path() + QStringLiteral("/chart.png");
        QLocalSocket s;
        QVERIFY(connectClient(s));
        readGreeting(s);
        QJsonObject spec;
        spec[QStringLiteral("mark")] = QStringLiteral("bar");
        QJsonObject a;
        a[QStringLiteral("spec")] = spec;
        a[QStringLiteral("path")] = pngPath;
        a[QStringLiteral("format")] = QStringLiteral("png");
        sendRequest(s, 380, QStringLiteral("export_chart"), a);
        QFrame *card = waitForCard();
        QVERIFY(card);
        auto *desc = card->findChild<QLabel *>(QStringLiteral("mcpApprovalDesc"));
        QVERIFY(desc);
        QVERIFY(desc->text().contains(QLatin1String("export a chart as PNG")));
        card->findChild<QPushButton *>(QStringLiteral("mcpApproveBtn"))->click();
        QJsonObject r = readObj(s);
        QVERIFY(r.value(QLatin1String("ok")).toBool());
        QCOMPARE(QDir::fromNativeSeparators(
                     r.value(QLatin1String("result")).toObject()
                         .value(QLatin1String("path")).toString()),
                 pngPath);
        QVERIFY(QFileInfo::exists(pngPath));
        {
            QFile f(pngPath);
            QVERIFY(f.open(QIODevice::ReadOnly));
            QCOMPARE(f.readAll(), QByteArray("FAKECHART"));
        }
        QCOMPARE(m_exportedChartScale, 2); // default scale
        QVERIFY(waitForCardGone());
        // scale passthrough.
        const QString png4 = dir.path() + QStringLiteral("/chart4.png");
        QJsonObject a4 = a;
        a4[QStringLiteral("path")] = png4;
        a4[QStringLiteral("scale")] = 4;
        sendRequest(s, 381, QStringLiteral("export_chart"), a4);
        QVERIFY(waitForCard());
        findCard()->findChild<QPushButton *>(QStringLiteral("mcpApproveBtn"))->click();
        r = readObj(s);
        QVERIFY(r.value(QLatin1String("ok")).toBool());
        QCOMPARE(m_exportedChartScale, 4);
        QVERIFY(waitForCardGone());
        // Deny → no file.
        const QString denyPng = dir.path() + QStringLiteral("/deny.png");
        QJsonObject ad = a;
        ad[QStringLiteral("path")] = denyPng;
        sendRequest(s, 382, QStringLiteral("export_chart"), ad);
        QVERIFY(waitForCard());
        findCard()->findChild<QPushButton *>(QStringLiteral("mcpDenyBtn"))->click();
        r = readObj(s);
        QCOMPARE(r.value(QLatin1String("ok")).toBool(), false);
        QVERIFY(!QFileInfo::exists(denyPng));
        QVERIFY(waitForCardGone());
    }
};

QTEST_MAIN(TestMcpBridge)
#include "test_mcp_bridge.moc"
