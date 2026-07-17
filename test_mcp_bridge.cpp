// SPDX-License-Identifier: GPL-3.0-or-later
//
// MCP bridge protocol test. Offscreen-safe, guiless (no widgets, no
// QMessageBox paths), Windows-safe (no POSIX headers). Drives a real
// QLocalServer round-trip against a fake editor host.
//
// Server and client live in the SAME event loop, so every wait is a
// QTest::qWait pump loop with a hard timeout — never a blocking
// waitFor* on the client (that would starve the server side).

#include <QtTest>
#include <QCoreApplication>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLocalSocket>
#include <QTemporaryDir>

#include "mcp_bridge.h"

namespace {
constexpr int kWaitMs = 10000;

struct FakeTab {
    QString title;
    QString path;
    QString text;
    bool modified = false;
    QString language = QStringLiteral("Plain Text");
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
        return h;
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
        m_name = QStringLiteral("notepatra-mcp-selftest-%1-%2")
                     .arg(QCoreApplication::applicationPid())
                     .arg(++m_seq);
        m_bridge = new McpBridge(makeHost(), this, m_name);
        QVERIFY(m_bridge->isListening());
    }

    void cleanup() {
        delete m_bridge;
        m_bridge = nullptr;
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
};

QTEST_GUILESS_MAIN(TestMcpBridge)
#include "test_mcp_bridge.moc"
