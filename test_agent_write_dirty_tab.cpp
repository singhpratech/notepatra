// SPDX-License-Identifier: GPL-3.0-or-later
//
// test_agent_write_dirty_tab — when the AI agent writes a file that is open
// in a tab with UNSAVED edits, the tab must keep the user's buffer and the
// existing "File Changed" prompt must be offered instead of a silent reload.
// Regression: MainWindow's fileWrittenByAgent handler called loadFile()
// unconditionally and threw the user's unsaved edits away.
//
// Drives the real MainWindow + AIPanel signal. The prompt section runs only
// on Linux: QMessageBox SIGSEGVs under offscreen Windows and watcher prompts
// wedge offscreen macOS. Elsewhere the window is never shown, so the watcher
// defers and no box is created.

#include "aipanel.h"
#include "editor.h"
#include "mainwindow.h"
#include "tabmanager.h"

#include <QAbstractButton>
#include <QApplication>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QMessageBox>
#include <QPointer>
#include <QStringList>
#include <QTemporaryDir>
#include <QTimer>

#include <cstdio>

static int g_pass = 0, g_fail = 0;

static void check(const char *label, bool ok) {
    if (ok) { ++g_pass; std::printf("  [PASS] %s\n", label); }
    else    { ++g_fail; std::printf("  [FAIL] %s\n", label); }
}

static void put(const QString &path, const QByteArray &bytes) {
    QFile f(path);
    f.open(QIODevice::WriteOnly | QIODevice::Truncate);
    f.write(bytes);
    f.close();
}

static void pump(int ms) {
    QElapsedTimer t;
    t.start();
    while (t.elapsed() < ms) QApplication::processEvents(QEventLoop::AllEvents, 20);
}

static Editor *editorFor(TabManager *tabs, const QString &path) {
    for (int i = 0; i < tabs->count(); ++i)
        if (Editor *e = tabs->editorAt(i))
            if (e->filePath() == path) return e;
    return nullptr;
}

int main(int argc, char *argv[]) {
    std::setvbuf(stdout, nullptr, _IONBF, 0);
    QTemporaryDir cfg;
    qputenv("HOME", cfg.path().toUtf8());
    qputenv("XDG_CONFIG_HOME", cfg.path().toUtf8());
    qputenv("XDG_DATA_HOME", cfg.path().toUtf8());
#ifdef Q_OS_WIN
    qputenv("APPDATA", cfg.path().toUtf8());
    qputenv("USERPROFILE", cfg.path().toUtf8());
#endif
    qputenv("QT_QPA_PLATFORM", "offscreen");
    QApplication app(argc, argv);
    std::printf("=== test_agent_write_dirty_tab ===\n");

    QTemporaryDir wd;
    const QString dirtyPath = QFileInfo(wd.path() + "/dirty.txt").absoluteFilePath();
    const QString cleanPath = QFileInfo(wd.path() + "/clean.txt").absoluteFilePath();
    put(dirtyPath, "original dirty\n");
    put(cleanPath, "original clean\n");

    MainWindow mw;
    AIPanel *panel = mw.findChild<AIPanel *>();
    TabManager *tabs = mw.findChild<TabManager *>();
    check("AIPanel + TabManager exist", panel && tabs);
    if (!panel || !tabs) return 1;

    mw.openFile(dirtyPath);
    mw.openFile(cleanPath);
    QApplication::processEvents();
    Editor *dirty = editorFor(tabs, dirtyPath);
    Editor *clean = editorFor(tabs, cleanPath);
    check("both files open", dirty && clean);
    if (!dirty || !clean) return 1;

    // ── dirty tab: agent write must not discard unsaved edits ────────
    dirty->setCursorPosition(0, 0);
    dirty->insert(QStringLiteral("USER EDIT "));
    check("dirty tab is modified before the agent write", dirty->isModified());
    put(dirtyPath, "agent content\n");
    emit panel->fileWrittenByAgent(dirtyPath, false);
    pump(300);
    check("dirty tab keeps the user's unsaved text", dirty->text().contains("USER EDIT"));
    check("dirty tab not replaced by the agent's content",
          !dirty->text().contains("agent content"));
    check("dirty tab still marked modified", dirty->isModified());

    // ── clean tab: agent write still reloads silently ────────────────
    check("clean tab unmodified", !clean->isModified());
    put(cleanPath, "agent rewrote clean\n");
    emit panel->fileWrittenByAgent(cleanPath, false);
    pump(300);
    check("clean tab reloaded with the agent's content",
          clean->text() == QStringLiteral("agent rewrote clean\n"));

#ifdef Q_OS_LINUX
    // ── shown window: the existing "File Changed" prompt is offered ──
    QStringList boxes;
    QTimer clicker;
    QObject::connect(&clicker, &QTimer::timeout, [&boxes]() {
        for (QWidget *w : QApplication::topLevelWidgets()) {
            auto *box = qobject_cast<QMessageBox *>(w);
            if (!box || !box->isVisible()) continue;
            boxes << box->text();
            if (QAbstractButton *no = box->button(QMessageBox::No)) no->click();  // keep mine
            else box->reject();
        }
    });
    clicker.start(50);
    mw.show();
    pump(500);

    const QString path3 = QFileInfo(wd.path() + "/shown.txt").absoluteFilePath();
    put(path3, "original shown\n");
    mw.openFile(path3);
    pump(200);
    Editor *shown = editorFor(tabs, path3);
    check("shown file open", shown != nullptr);
    if (shown) {
        shown->setCursorPosition(0, 0);
        shown->insert(QStringLiteral("MINE "));
        boxes.clear();
        put(path3, "agent shown content\n");
        emit panel->fileWrittenByAgent(path3, false);
        QElapsedTimer t;
        t.start();
        while (t.elapsed() < 3000 && boxes.filter("shown.txt").isEmpty())
            QApplication::processEvents(QEventLoop::AllEvents, 20);
        pump(300);
        check("\"File Changed\" prompt offered for the dirty tab",
              !boxes.filter("shown.txt").filter("modified by another program").isEmpty());
        check("choosing Keep leaves the user's buffer", shown->text().contains("MINE"));
        check("buffer still modified after Keep", shown->isModified());
    }
    clicker.stop();
#endif

    std::printf("\n=== Summary: %d passed, %d failed ===\n", g_pass, g_fail);
    return g_fail == 0 ? 0 : 1;
}
