// SPDX-License-Identifier: GPL-3.0-or-later
//
// Diagram tabs on every quit path, and a stable MCP tab_id across theme
// changes. Real MainWindow, real MCP bridge socket, isolated config.
//
// Bug 1 (reported on v0.1.134): four modified Diagram tabs made through MCP
// create_diagram were gone after a SIGTERM + relaunch, while the unsaved
// editor tabs came back. saveSession() serialised Editor tabs only, so a
// Diagram tab never reached session.json on ANY path: the autosave tick,
// the SIGTERM quit (Qt 5's quit() sends no close event, so the last tick
// is what survives) and a normal close. A session-less window (--new,
// live-owner restore skip) prompted only for Editor tabs, and closing a
// modified Diagram tab with its X never asked either.
//
//   A. autosave tick, then the window is destroyed with no close event (what
//      a SIGTERM quit leaves behind): relaunch restores the diagrams with
//      their source, title and modified state.
//   B. normal close (closeEvent) then relaunch: still restored.
//   C. a pristine, file-backed diagram is re-read from disk on restore.
//   D. a session-less window prompts for a modified Diagram tab on close
//      (Cancel keeps it, Discard closes); skipped on offscreen Windows,
//      where any QMessageBox segfaults.
//
// Bug 2: the Welcome tab's MCP tab_id changed (9 -> 17) across set_theme
// calls. applyThemeToAll() rebuilds the Welcome widget, and the id lives on
// the widget, so the rebuilt one was stamped with a fresh id.
//
//   E. list_open_tabs id of the Welcome tab is the same before and after
//      set_theme Dark / Light, and a tab_id selector still resolves it.

#include "mainwindow.h"
#include "config.h"
#include "editor.h"
#include "mcp_bridge.h"
#include "tabmanager.h"
#include "diagram/diagram_editor.h"

#include <QAction>
#include <QApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLocalSocket>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QPointer>
#include <QPushButton>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QThread>
#include <QTimer>

#include <cstdio>

static int g_pass = 0, g_fail = 0;
#define EXPECT(label, cond) \
    do { if (cond) { ++g_pass; std::printf("  [PASS] %s\n", label); } \
         else      { ++g_fail; std::printf("  [FAIL] %s\n", label); } \
         std::fflush(stdout); } while (0)

namespace {

void pump(int ms) {
    QElapsedTimer t;
    t.start();
    while (t.elapsed() < ms) {
        QApplication::processEvents(QEventLoop::AllEvents, 10);
        QThread::msleep(2);
    }
}

QByteArray readFile(const QString &path) {
    QFile f(path);
    return f.open(QIODevice::ReadOnly) ? f.readAll() : QByteArray();
}

bool writeFile(const QString &path, const QByteArray &bytes) {
    QDir().mkpath(QFileInfo(path).absolutePath());
    QFile f(path);
    return f.open(QIODevice::WriteOnly | QIODevice::Truncate) && f.write(bytes) == bytes.size();
}

// ── MCP socket client (same wire as test_mcp_host_control) ──
struct Client {
    QLocalSocket sock;
    int id = 0;

    bool connectTo() {
        sock.connectToServer(McpBridge::defaultServerName());
        QElapsedTimer t;
        t.start();
        while (sock.state() != QLocalSocket::ConnectedState && t.elapsed() < 5000) pump(10);
        if (sock.state() != QLocalSocket::ConnectedState) return false;
        readObj();   // greeting
        return true;
    }
    QJsonObject readObj(int timeoutMs = 20000) {
        QElapsedTimer t;
        t.start();
        while (!sock.canReadLine() && t.elapsed() < timeoutMs) pump(5);
        if (!sock.canReadLine()) return QJsonObject{{"timeout", true}};
        return QJsonDocument::fromJson(sock.readLine().trimmed()).object();
    }
    QJsonObject call(const QString &verb, const QJsonObject &args = {}) {
        sock.write(QJsonDocument(QJsonObject{{"id", ++id}, {"verb", verb}, {"args", args}})
                       .toJson(QJsonDocument::Compact) + '\n');
        sock.flush();
        return readObj();
    }
    void close() {
        sock.disconnectFromServer();
        pump(20);
    }
};

bool ok(const QJsonObject &r) { return r.value(QLatin1String("ok")).toBool(); }
QJsonObject res(const QJsonObject &r) { return r.value(QLatin1String("result")).toObject(); }

QList<DiagramEditor *> diagrams(TabManager *tm) {
    QList<DiagramEditor *> out;
    for (int i = 0; i < tm->count(); ++i)
        if (auto *de = qobject_cast<DiagramEditor *>(tm->widget(i))) out.append(de);
    return out;
}

DiagramEditor *diagramWithText(TabManager *tm, const QString &needle) {
    for (DiagramEditor *de : diagrams(tm))
        if (de->npdText().contains(needle)) return de;
    return nullptr;
}

QString tabTextOf(TabManager *tm, QWidget *w) {
    const int i = tm->indexOf(w);
    return i >= 0 ? tm->tabText(i) : QString();
}

bool anyEditorContains(TabManager *tm, const QString &needle) {
    for (int i = 0; i < tm->count(); ++i)
        if (Editor *e = tm->editorAt(i); e && e->text().contains(needle)) return true;
    return false;
}

// The live session window, constructed the way main() does it, minus show().
MainWindow *openWindow(bool standalone = false) {
    auto *w = new MainWindow(standalone);
    w->resize(1000, 700);
    w->runStartupNow();
    pump(50);
    return w;
}

QAction *diagramAction(MainWindow *w) {
    for (QAction *act : w->findChildren<QAction *>())
        if (act->text().startsWith(QStringLiteral("Diagram — Flow"))) return act;
    return nullptr;
}

bool winOffscreenModalUnsafe() {
#if defined(Q_OS_WIN)
    return QGuiApplication::platformName().compare(QLatin1String("offscreen"),
                                                   Qt::CaseInsensitive) == 0;
#else
    return false;
#endif
}

// Click `which` on the first visible QMessageBox; re-arms until one shows.
// *seen proves a prompt really appeared (no vacuous "nothing happened").
void scheduleModalClick(QMessageBox::StandardButton which, bool *seen, int triesLeft = 60) {
    QTimer::singleShot(50, [which, seen, triesLeft]() {
        for (QWidget *tw : QApplication::topLevelWidgets()) {
            if (auto *mb = qobject_cast<QMessageBox *>(tw); mb && mb->isVisible()) {
                if (auto *b = mb->button(which)) {
                    if (seen) *seen = true;
                    b->click();
                    return;
                }
            }
        }
        if (triesLeft > 0) scheduleModalClick(which, seen, triesLeft - 1);
    });
}

const char kSrcA[] = "diagram flow\ntitle \"Session A\"\nnode a (Start)\nnode b [SESSION-A-NODE]\na -> b\n";
const char kSrcB[] = "diagram er\nnode u ([Users])\nnode o ([SESSION-B-NODE])\nu -> o : places\n";

} // namespace

int main(int argc, char *argv[]) {
    // Config isolation on all three platforms: Config::appConfigDir() reads
    // XDG_CONFIG_HOME on Linux, $HOME/Library/... on macOS (QDir::homePath()
    // follows HOME there) and %APPDATA% on Windows, where QDir::homePath()
    // follows USERPROFILE, not HOME. The MCP socket name is salted with
    // QDir::homePath(), so this also keeps the test off a real editor's bridge.
    QTemporaryDir home;
    qputenv("HOME", home.path().toUtf8());
    qputenv("XDG_CONFIG_HOME", (home.path() + "/.config").toUtf8());
    qputenv("XDG_DATA_HOME", (home.path() + "/.local/share").toUtf8());
#ifdef Q_OS_WIN
    qputenv("APPDATA", home.path().toUtf8());
    qputenv("USERPROFILE", home.path().toUtf8());
#endif
    qputenv("QT_QPA_PLATFORM", "offscreen");
    QStandardPaths::setTestModeEnabled(true);
    QApplication app(argc, argv);
    std::setvbuf(stdout, nullptr, _IONBF, 0);
    std::printf("=== test_diagram_session ===\n");

    const QString cfgDir = Config::appConfigDir();
    EXPECT("fixture: config dir is inside the temp HOME",
           QDir::cleanPath(cfgDir).startsWith(QDir::cleanPath(home.path()) + QLatin1Char('/')));
    // Long autosave interval: this test drives the ticks itself. No Welcome
    // on startup, so the restored tab set is exactly what the session holds;
    // noterInUse 0 keeps the startup probe off a shared Documents folder.
    writeFile(cfgDir + "/config.json",
              "{\"showWelcomeOnStartup\": false, \"autoSaveIntervalSec\": 300, "
              "\"noterInUse\": 0}\n");
    const QString sessionPath = cfgDir + "/session.json";

    // ── A. autosave tick + no close event (the SIGTERM shape) ──
    std::printf("\n-- A: autosave tick, then quit with no close event --\n");
    {
        MainWindow *w = openWindow();
        auto *tm = w->findChild<TabManager *>();
        Client c;
        EXPECT("A: connected to the window's MCP bridge", tm && c.connectTo());

        // A pristine diagram first (Features > Diagram: the starter template,
        // never edited): nothing to lose, so nothing to restore. The toggle
        // only creates when no Diagram tab exists yet, hence first.
        if (QAction *diagAct = diagramAction(w)) diagAct->trigger();
        EXPECT("A: Features > Diagram made a pristine diagram",
               diagrams(tm).size() == 1 && !diagrams(tm).first()->isModified());

        QJsonObject r = c.call("create_diagram", {{"source", QString::fromUtf8(kSrcA)},
                                                  {"title", "Auth flow"}});
        EXPECT("A: create_diagram with source + title", ok(r));
        r = c.call("create_diagram", {{"source", QString::fromUtf8(kSrcB)}});
        EXPECT("A: create_diagram with source only", ok(r));
        // An unsaved editor tab beside them — the control that already worked.
        if (Editor *e = tm->editorAt(0)) e->setText(QStringLiteral("EDITOR-UNSAVED-CONTROL\n"));

        DiagramEditor *a = diagramWithText(tm, "SESSION-A-NODE");
        DiagramEditor *b = diagramWithText(tm, "SESSION-B-NODE");
        EXPECT("A: both MCP diagrams are open and modified",
               a && b && a->isModified() && b->isModified());
        // Typing in the canvas source after creation, as a user would.
        if (b)
            if (auto *pe = b->findChild<QPlainTextEdit *>(QStringLiteral("diagramSource")))
                pe->appendPlainText(QStringLiteral("o -> u : TYPED-AFTER-CREATE"));

        w->saveSession();   // the autosave tick's body
        c.close();
        delete w;           // no closeEvent: Qt 5 quit() on SIGTERM sends none
        pump(20);

        const QJsonArray tabs =
            QJsonDocument::fromJson(readFile(sessionPath)).object().value("tabs").toArray();
        int diagEntries = 0;
        for (const QJsonValue &v : tabs)
            if (v.toObject().value("kind").toString() == QLatin1String("diagram")) ++diagEntries;
        EXPECT("A: session.json holds the two modified diagrams (not the pristine template)",
               diagEntries == 2);

        MainWindow *w2 = openWindow();
        auto *tm2 = w2->findChild<TabManager *>();
        DiagramEditor *ra = diagramWithText(tm2, "SESSION-A-NODE");
        DiagramEditor *rb = diagramWithText(tm2, "SESSION-B-NODE");
        EXPECT("A: relaunch restores the unsaved editor tab (control)",
               anyEditorContains(tm2, QStringLiteral("EDITOR-UNSAVED-CONTROL")));
        EXPECT("A: relaunch restores diagram A", ra != nullptr);
        EXPECT("A: relaunch restores diagram B with the text typed after creation",
               rb && rb->npdText().contains(QStringLiteral("TYPED-AFTER-CREATE")));
        EXPECT("A: restored diagrams are still marked modified",
               ra && rb && ra->isModified() && rb->isModified());
        EXPECT("A: restored diagrams are still unsaved (no path)",
               ra && rb && ra->filePath().isEmpty() && rb->filePath().isEmpty());
        EXPECT("A: diagram A keeps its tab title",
               ra && tabTextOf(tm2, ra) == QStringLiteral("Auth flow"));
        EXPECT("A: the pristine template diagram is not resurrected",
               diagrams(tm2).size() == 2);

        // ── B. normal close (closeEvent) then relaunch ──
        std::printf("\n-- B: normal close, then relaunch --\n");
        if (rb)
            if (auto *pe = rb->findChild<QPlainTextEdit *>(QStringLiteral("diagramSource")))
                pe->appendPlainText(QStringLiteral("u -> o : EDIT-BEFORE-CLOSE"));
        const bool closed = w2->close();
        EXPECT("B: a session window closes without prompting", closed);
        delete w2;
        pump(20);

        MainWindow *w3 = openWindow();
        auto *tm3 = w3->findChild<TabManager *>();
        EXPECT("B: both diagrams survive a normal close",
               diagramWithText(tm3, "SESSION-A-NODE") &&
                   diagramWithText(tm3, "EDIT-BEFORE-CLOSE"));
        w3->close();
        delete w3;
        pump(20);
    }

    // ── C. pristine file-backed diagram is re-read from disk ──
    std::printf("\n-- C: file-backed diagram restore --\n");
    {
        QTemporaryDir work;
        const QString npd = work.path() + "/saved.npd";
        writeFile(npd, "diagram flow\nnode x [ON-DISK-NOW]\n");
        QJsonObject t;
        t["kind"] = "diagram";
        t["path"] = npd;
        t["tabName"] = "saved.npd";
        t["line"] = 0;
        t["col"] = 0;
        t["active"] = true;
        t["modified"] = false;
        QJsonObject s;
        s["tabs"] = QJsonArray{t};
        writeFile(sessionPath, QJsonDocument(s).toJson());

        MainWindow *w = openWindow();
        auto *tm = w->findChild<TabManager *>();
        DiagramEditor *de = diagramWithText(tm, "ON-DISK-NOW");
        EXPECT("C: the .npd is reopened from disk as a Diagram tab", de != nullptr);
        EXPECT("C: ...unmodified and bound to its path",
               de && !de->isModified() &&
                   QFileInfo(de->filePath()) == QFileInfo(npd));
        EXPECT("C: ...and it is the active tab", de && tm->currentWidget() == de);
        w->close();
        delete w;
        pump(20);
    }

    // ── D. session-less window prompts for a modified Diagram tab ──
    std::printf("\n-- D: session-less window prompts on close --\n");
    if (!winOffscreenModalUnsafe()) {
        const QByteArray before = readFile(sessionPath);
        MainWindow *w = openWindow(/*standalone=*/true);
        auto *tm = w->findChild<TabManager *>();
        QAction *diagAct = diagramAction(w);
        EXPECT("D: Features > Diagram action found", diagAct != nullptr);
        if (diagAct) diagAct->trigger();
        DiagramEditor *de = diagrams(tm).value(0);
        if (de)
            if (auto *pe = de->findChild<QPlainTextEdit *>(QStringLiteral("diagramSource")))
                pe->appendPlainText(QStringLiteral("node z [STANDALONE-UNSAVED]"));
        EXPECT("D: the diagram is modified", de && de->isModified());

        bool seen = false;
        scheduleModalClick(QMessageBox::Cancel, &seen);
        const bool closedCancel = w->close();
        pump(20);
        EXPECT("D: closing prompted for the modified diagram", seen);
        EXPECT("D: Cancel keeps the window open", !closedCancel);
        EXPECT("D: Cancel keeps the diagram and its edit",
               diagramWithText(tm, "STANDALONE-UNSAVED") != nullptr);

        seen = false;
        scheduleModalClick(QMessageBox::Discard, &seen);
        const bool closedDiscard = w->close();
        pump(20);
        EXPECT("D: Discard lets the window close", seen && closedDiscard);
        EXPECT("D: the session-less window never wrote session.json",
               readFile(sessionPath) == before);
        delete w;
        pump(20);
    } else {
        std::printf("  [SKIP] D: modal-driven section (offscreen Windows)\n");
    }

    // ── E. Welcome tab id is stable across set_theme ──
    std::printf("\n-- E: Welcome tab_id across set_theme --\n");
    {
        QFile::remove(sessionPath);
        MainWindow *w = openWindow();
        auto *tm = w->findChild<TabManager *>();
        Client c;
        EXPECT("E: connected to the window's MCP bridge", c.connectTo());
        if (QAction *wa = w->findChild<QAction *>(QStringLiteral("actShowWelcomeTab")))
            wa->trigger();
        const auto ids = [&c]() {
            QMap<QString, qint64> out;
            for (const QJsonValue &v : res(c.call("list_open_tabs")).value("tabs").toArray()) {
                const QJsonObject t = v.toObject();
                out.insert(t.value("title").toString(), qint64(t.value("id").toDouble(-1)));
            }
            return out;
        };
        const QMap<QString, qint64> before = ids();
        const qint64 welcomeId = before.value(QStringLiteral("Welcome"), -1);
        EXPECT("E: the Welcome tab is listed with an id", welcomeId > 0);
        QPointer<QWidget> welcomeWidget;
        for (int i = 0; i < tm->count(); ++i)
            if (tm->tabText(i) == QStringLiteral("Welcome")) welcomeWidget = tm->widget(i);

        const QJsonObject d = c.call("set_theme", {{"theme", "Dark"}});
        const QJsonObject l = c.call("set_theme", {{"theme", "Light"}});
        EXPECT("E: set_theme Dark then Light succeed", ok(d) && ok(l));
        // Informational: the id contract holds whether the Welcome page is
        // rebuilt (today) or restyled in place.
        std::printf("  [INFO] Welcome widget %s by set_theme\n",
                    welcomeWidget ? "restyled in place" : "rebuilt");
        EXPECT("E: no tab appeared or vanished", tm->count() == before.size());

        const QMap<QString, qint64> after = ids();
        EXPECT("E: the Welcome tab keeps its tab_id across set_theme",
               after.value(QStringLiteral("Welcome"), -2) == welcomeId);
        EXPECT("E: every other tab keeps its tab_id too", [&]() {
            for (auto it = before.cbegin(); it != before.cend(); ++it)
                if (after.value(it.key(), -2) != it.value()) return false;
            return true;
        }());
        const QJsonObject act = c.call("activate_tab", {{"tab_id", welcomeId}});
        EXPECT("E: the old tab_id still selects the Welcome tab", ok(act));
        c.close();
        w->close();
        delete w;
        pump(20);
    }

    std::printf("\n%d passed, %d failed\n", g_pass, g_fail);
    return g_fail == 0 ? 0 : 1;
}
