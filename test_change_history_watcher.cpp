// Reproduces the user-reported scenario: edit line, save, edit DIFFERENT line,
// save — with a real QFileSystemWatcher running on the same path so any
// race with the watcher's fileChanged signal would be observable.

#include "mainwindow.h"
#include "editor.h"
#include "tabmanager.h"

#include <QAction>
#include <QApplication>
#include <QFile>
#include <QFileSystemWatcher>
#include <QTemporaryDir>
#include <QTimer>
#include <Qsci/qsciscintilla.h>
#include <Qsci/qsciscintillabase.h>

#include <cstdio>

static int g_pass = 0, g_fail = 0;
#define EXPECT(label, cond) \
    do { if (cond) { ++g_pass; std::printf("  [PASS] %s\n", label); } \
         else      { ++g_fail; std::printf("  [FAIL] %s\n", label); } } while (0)

static unsigned markersAt(Editor *e, int line) {
    return (unsigned)e->SendScintilla(QsciScintillaBase::SCI_MARKERGET,
                                       (unsigned long)line);
}

int main(int argc, char *argv[]) {
    QTemporaryDir cfg;
    qputenv("XDG_CONFIG_HOME", cfg.path().toUtf8());
    qputenv("XDG_DATA_HOME",   cfg.path().toUtf8());
    qputenv("QT_QPA_PLATFORM", "offscreen");

    QApplication app(argc, argv);

    QTemporaryDir wd;
    const QString path = wd.path() + "/test.md";
    {
        QFile f(path);
        f.open(QIODevice::WriteOnly);
        f.write("alpha\nbravo\ncharlie\ndelta\necho\n");
        f.close();
    }

    std::printf("=== test_change_history_watcher ===\n\n");

    MainWindow mw;
    mw.show();
    QApplication::processEvents();
    mw.openFile(path);
    QApplication::processEvents();

    auto *tabs = mw.findChild<TabManager *>();
    Editor *e = tabs->currentEditor();
    if (!e) { std::printf("no editor\n"); return 1; }

    // Find Ctrl+S action
    QAction *saveAct = nullptr;
    for (QAction *a : mw.findChildren<QAction*>()) {
        if (a->shortcut() == QKeySequence("Ctrl+S")) { saveAct = a; break; }
    }
    if (!saveAct) { std::printf("no save action\n"); return 1; }

    // ── Cycle 1: edit line 1, save ──
    std::printf("\n--- cycle 1: edit line 1, save ---\n");
    e->setCursorPosition(1, 0);
    e->insert("X");
    saveAct->trigger();
    // Give the file watcher time to fire ANY queued events.
    for (int i = 0; i < 20; ++i) {
        QApplication::processEvents(QEventLoop::AllEvents, 50);
    }
    QApplication::processEvents();

    unsigned m1 = markersAt(e, 1);
    std::printf("  line 1 mask = 0x%x\n", m1);
    EXPECT("cycle 1: line 1 GREEN after save",
           (m1 & (1u<<22)) != 0);

    // ── Cycle 2: edit line 3 (different line), save ──
    std::printf("\n--- cycle 2: edit line 3, save ---\n");
    e->setCursorPosition(3, 0);
    e->insert("Y");
    QApplication::processEvents();
    unsigned m3_pre_save = markersAt(e, 3);
    std::printf("  line 3 mask AFTER EDIT, BEFORE SAVE = 0x%x\n", m3_pre_save);
    EXPECT("cycle 2: line 3 orange after edit",
           (m3_pre_save & (1u<<23)) != 0);

    saveAct->trigger();
    // Pump events MULTIPLE times to give watcher time to fire and even
    // potentially trigger a reload-dialog (which would block but we
    // wouldn't reach here if it did).
    for (int i = 0; i < 50; ++i) {
        QApplication::processEvents(QEventLoop::AllEvents, 20);
    }

    unsigned m3_post = markersAt(e, 3);
    unsigned m1_post = markersAt(e, 1);
    std::printf("  line 3 mask AFTER SAVE = 0x%x  (expect green 0x400000)\n", m3_post);
    std::printf("  line 1 mask AFTER cycle-2 save = 0x%x (expect green 0x400000)\n", m1_post);

    EXPECT("cycle 2: line 3 GREEN after save (user-reported failure)",
           (m3_post & (1u<<22)) != 0);
    EXPECT("cycle 2: line 3 no longer orange",
           (m3_post & (1u<<23)) == 0);
    EXPECT("cycle 2: line 1 STILL green (not regressed)",
           (m1_post & (1u<<22)) != 0);

    // ── Cycle 3: edit line 0, save ──
    std::printf("\n--- cycle 3: edit line 0, save ---\n");
    e->setCursorPosition(0, 0);
    e->insert("Z");
    QApplication::processEvents();
    saveAct->trigger();
    for (int i = 0; i < 50; ++i) {
        QApplication::processEvents(QEventLoop::AllEvents, 20);
    }
    unsigned m0_post = markersAt(e, 0);
    std::printf("  line 0 mask after cycle-3 save = 0x%x\n", m0_post);
    EXPECT("cycle 3: line 0 GREEN", (m0_post & (1u<<22)) != 0);

    std::printf("\n%d passed, %d failed\n", g_pass, g_fail);
    return g_fail == 0 ? 0 : 1;
}
