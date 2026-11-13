// SPDX-License-Identifier: GPL-3.0-or-later
//
// test_mcp_search_symlink — the MCP bridge judges a symlink by its TARGET.
// Regression: search_project's workspace walk and the credential refusal used
// by open_file / read_tab checked only the path as written, so a workspace
// file symlinked to a deny-listed file (or to a file outside the workspace)
// had its lines returned. Symlink cases run on Linux/macOS only.

#include <QtTest>
#include <QApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLocalSocket>
#include <QTemporaryDir>

#include "mcp_bridge.h"

class TestMcpSearchSymlink : public QObject {
    Q_OBJECT

    struct Tab { QString path; QString text; };
    QVector<Tab> m_tabs;
    QString m_root;
    McpBridge *m_bridge = nullptr;
    QString m_name;

    McpEditorHost makeHost() {
        McpEditorHost h;
        h.tabCount = [this] { return m_tabs.size(); };
        h.tabTitle = [this](int i) { return QFileInfo(m_tabs.value(i).path).fileName(); };
        h.tabPath = [this](int i) { return m_tabs.value(i).path; };
        h.tabModified = [](int) { return false; };
        h.tabText = [this](int i) { return m_tabs.value(i).text; };
        h.openFile = [this](const QString &p) -> int {
            QFile f(p);
            if (!f.open(QIODevice::ReadOnly)) return -1;
            m_tabs.append({QFileInfo(p).absoluteFilePath(), QString::fromUtf8(f.readAll())});
            return m_tabs.size() - 1;
        };
        h.selection = [](int *idx) { if (idx) *idx = -1; return QString(); };
        h.workspaceRoot = [this] { return m_root; };
        return h;
    }

    static void put(const QString &path, const QByteArray &bytes) {
        QFile f(path);
        QVERIFY(f.open(QIODevice::WriteOnly));
        f.write(bytes);
    }

    QJsonObject call(QLocalSocket &s, int id, const QString &verb, const QJsonObject &args) {
        QJsonObject req{{"id", id}, {"verb", verb}, {"args", args}};
        s.write(QJsonDocument(req).toJson(QJsonDocument::Compact) + '\n');
        s.flush();
        QElapsedTimer t;
        t.start();
        while (!s.canReadLine() && t.elapsed() < 10000) QTest::qWait(10);
        return QJsonDocument::fromJson(s.readLine().trimmed()).object();
    }

    bool connectClient(QLocalSocket &s) {
        s.connectToServer(m_name);
        QElapsedTimer t;
        t.start();
        while (s.state() != QLocalSocket::ConnectedState && t.elapsed() < 10000)
            QTest::qWait(10);
        if (s.state() != QLocalSocket::ConnectedState) return false;
        while (!s.canReadLine() && t.elapsed() < 10000) QTest::qWait(10);
        s.readLine(); // greeting
        return true;
    }

private slots:
    void init() {
        m_tabs.clear();
        m_name = QStringLiteral("notepatra-mcp-symlink-%1").arg(QCoreApplication::applicationPid());
        m_bridge = new McpBridge(makeHost(), this, m_name);
        QVERIFY(m_bridge->isListening());
    }
    void cleanup() {
        delete m_bridge;
        m_bridge = nullptr;
    }

    void symlink_targets_are_judged_not_link_names() {
#ifdef Q_OS_WIN
        QSKIP("symlink cases need POSIX symlinks");
#else
        QTemporaryDir ws, outside;
        QVERIFY(ws.isValid() && outside.isValid());
        m_root = QDir(ws.path()).canonicalPath();
        const QString out = QDir(outside.path()).canonicalPath();
        put(m_root + "/readme.txt", "plain NEEDLE-SYM here\n");
        put(m_root + "/.env", "API_KEY=NEEDLE-SYM-ENV\n");
        put(out + "/outside.txt", "NEEDLE-SYM-OUTSIDE\n");
        QDir().mkpath(out + "/.ssh");
        put(out + "/.ssh/id_ed25519", "NEEDLE-SYM-KEY\n");
        QVERIFY(QFile::link(m_root + "/.env", m_root + "/innocent.txt"));
        QVERIFY(QFile::link(out + "/outside.txt", m_root + "/escape.txt"));
        QVERIFY(QFile::link(out + "/.ssh/id_ed25519", m_root + "/key.txt"));
        QVERIFY(QFile::link(m_root + "/readme.txt", m_root + "/alias.txt"));
        // Vacuity guard: the links exist and resolve.
        for (const char *n : {"innocent.txt", "escape.txt", "key.txt", "alias.txt"}) {
            const QFileInfo fi(m_root + "/" + n);
            QVERIFY2(fi.isSymLink() && fi.exists(), n);
        }

        QLocalSocket s;
        QVERIFY(connectClient(s));
        const QJsonObject r = call(s, 1, "search_project", {{"query", "NEEDLE-SYM"}})
                                  .value("result").toObject();
        QVERIFY2(r.value("workspace_searched").toBool(), "guard: workspace leg ran");
        QStringList hits;
        for (const QJsonValue &v : r.value("results").toArray())
            hits << QFileInfo(v.toObject().value("path").toString()).fileName()
                        + ":" + v.toObject().value("text").toString();
        const QString all = hits.join('\n');
        QVERIFY2(all.contains("readme.txt:"), qPrintable(all));
        QVERIFY2(all.contains("alias.txt:"), "an in-root, non-secret symlink is still searched");
        QVERIFY2(!all.contains("NEEDLE-SYM-ENV"), qPrintable(all));
        QVERIFY2(!all.contains("NEEDLE-SYM-OUTSIDE"), qPrintable(all));
        QVERIFY2(!all.contains("NEEDLE-SYM-KEY"), qPrintable(all));

        // open_file refuses a link whose target is deny-listed.
        for (const char *n : {"innocent.txt", "key.txt"}) {
            const QJsonObject o = call(s, 2, "open_file", {{"path", m_root + "/" + n}});
            QVERIFY2(!o.value("ok").toBool(), n);
            QVERIFY2(o.value("error").toString().contains("deny-list"), n);
        }
        QCOMPARE(m_tabs.size(), 0);
        // Vacuity: an ordinary link opens.
        QVERIFY(call(s, 3, "open_file", {{"path", m_root + "/alias.txt"}}).value("ok").toBool());

        // read_tab refuses a tab whose path is a link to a secret.
        m_tabs.append({m_root + "/innocent.txt", "API_KEY=NEEDLE-SYM-ENV\n"});
        const QJsonObject rt = call(s, 4, "read_tab", {{"index", m_tabs.size() - 1}});
        QVERIFY(!rt.value("ok").toBool());
        QVERIFY(!QJsonDocument(rt).toJson().contains("NEEDLE-SYM-ENV"));
#endif
    }
};

int main(int argc, char *argv[]) {
    QTemporaryDir home;
    qputenv("HOME", home.path().toUtf8());
    qputenv("XDG_CONFIG_HOME", (home.path() + "/.config").toUtf8());
    qputenv("APPDATA", (home.path() + "/AppData").toUtf8());
    qputenv("USERPROFILE", home.path().toUtf8());
    QApplication app(argc, argv);
    TestMcpSearchSymlink tc;
    return QTest::qExec(&tc, argc, argv);
}
#include "test_mcp_search_symlink.moc"
