// Stress test — run many edit-save cycles across different lines to expose
// any cumulative state corruption in change-history tracking.

#include "mainwindow.h"
#include "editor.h"
#include "tabmanager.h"

#include <QAction>
#include <QApplication>
#include <QFile>
#include <QTemporaryDir>
#include <Qsci/qsciscintilla.h>
#include <Qsci/qsciscintillabase.h>

#include <cstdio>

static int g_pass = 0, g_fail = 0;
#define EXPECT(label, cond) \
    do { if (cond) { ++g_pass; std::printf("  [%s] %s\n", cond?"PASS":"FAIL", label); \
                     if (cond) ++g_pass; else ++g_fail; } } while (0)

static unsigned markersAt(Editor *e, int line) {
    return (unsigned)e->SendScintilla(QsciScintillaBase::SCI_MARKERGET,
                                       (unsigned long)line);
}

static QAction *findSaveAction(QWidget *root) {
    for (QAction *a : root->findChildren<QAction*>()) {
        if (a->shortcut() == QKeySequence("Ctrl+S")) return a;
    }
    return nullptr;
}

int main(int argc, char *argv[]) {
    QTemporaryDir cfg;
    qputenv("XDG_CONFIG_HOME", cfg.path().toUtf8());
    qputenv("XDG_DATA_HOME",   cfg.path().toUtf8());
    qputenv("QT_QPA_PLATFORM", "offscreen");

    QApplication app(argc, argv);

    QTemporaryDir wd;
    const QString path = wd.path() + "/stress.txt";
    {
        QFile f(path);
        f.open(QIODevice::WriteOnly);
        for (int i = 0; i < 20; ++i) {
            f.write(QString("line %1\n").arg(i).toUtf8());
        }
        f.close();
    }

    MainWindow mw;
    mw.show();
    QApplication::processEvents();
    mw.openFile(path);
    QApplication::processEvents();

    auto *tabs = mw.findChild<TabManager *>();
    Editor *e = tabs ? tabs->currentEditor() : nullptr;
    if (!e) { std::printf("no editor\n"); return 1; }
    QAction *save = findSaveAction(&mw);
    if (!save) { std::printf("no save action\n"); return 1; }

    std::printf("=== stress test: 10 edit-save cycles on different lines ===\n");

    // Cycles 0..9: edit line N, save, expect line N to be green.
    int total_failures = 0;
    for (int cycle = 0; cycle < 10; ++cycle) {
        int target_line = cycle * 2;  // 0, 2, 4, 6, ... 18
        e->setCursorPosition(target_line, 0);
        e->insert("X");
        QApplication::processEvents();

        unsigned pre = markersAt(e, target_line);
        bool orange_ok = (pre & (1u<<23)) != 0;

        save->trigger();
        for (int i = 0; i < 5; ++i) QApplication::processEvents();

        unsigned post = markersAt(e, target_line);
        bool green_ok = (post & (1u<<22)) != 0;
        bool no_orange = (post & (1u<<23)) == 0;

        std::printf("  cycle %2d line %2d: pre=0x%-8x post=0x%-8x  orange_pre=%d green_post=%d no_orange_post=%d %s\n",
                    cycle, target_line, pre, post, orange_ok, green_ok, no_orange,
                    (orange_ok && green_ok && no_orange) ? "OK" : "*** FAIL ***");
        if (!(orange_ok && green_ok && no_orange)) ++total_failures;
    }

    std::printf("\n=== verify ALL prior edits stayed green ===\n");
    for (int cycle = 0; cycle < 10; ++cycle) {
        int line = cycle * 2;
        unsigned m = markersAt(e, line);
        bool ok = (m & (1u<<22)) != 0;
        std::printf("  line %2d: 0x%-8x  green=%d %s\n", line, m, ok,
                    ok ? "OK" : "*** REGRESSED ***");
        if (!ok) ++total_failures;
    }

    std::printf("\nTotal failures: %d\n", total_failures);
    return total_failures == 0 ? 0 : 1;
}
