// v0.1.91 — change-history through the REAL MainWindow/TabManager path.
// Reproduces what the user sees in the live UI: open a file via MainWindow's
// openFile(), edit through the active editor, save through MainWindow's
// save action, and assert the orange→green transition still happens.

#include "mainwindow.h"
#include "editor.h"
#include "tabmanager.h"

#include <QApplication>
#include <QAction>
#include <QFile>
#include <QTemporaryDir>
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
    const QString path = wd.path() + "/medium-draft.md";
    {
        QFile f(path);
        f.open(QIODevice::WriteOnly);
        f.write("# Heading\n\nparagraph one\n\nparagraph two\n");
        f.close();
    }

    std::printf("=== test_change_history_mainwindow ===\n\n");

    MainWindow mw;
    mw.show();
    QApplication::processEvents();

    // Open the file through MainWindow's public openFile (same code path
    // as command-line "notepatra path" and File→Open).
    mw.openFile(path);
    QApplication::processEvents();

    auto *tabs = mw.findChild<TabManager *>();
    EXPECT("TabManager found", tabs != nullptr);
    Editor *e = tabs->currentEditor();
    EXPECT("currentEditor non-null", e != nullptr);
    if (!e) return 1;
    EXPECT("editor.filePath matches opened path", e->filePath() == path);

    // Post-load: no change-history markers anywhere.
    bool clean = true;
    for (int ln = 0; ln < e->lines(); ++ln) {
        unsigned m = markersAt(e, ln);
        if ((m & ((1u<<22)|(1u<<23))) != 0) clean = false;
    }
    EXPECT("post-load: no change-history markers", clean);

    // Edit line 2 (the "paragraph one" line) — append " hello".
    e->setCursorPosition(2, 0);
    e->insert("X");
    QApplication::processEvents();

    EXPECT("after edit: line 2 has orange (marker 23)",
           (markersAt(e, 2) & (1u<<23)) != 0);

    // Trigger save via the menu QAction so we go through MainWindow::saveFile()
    // (the actual user code path), not Editor::saveFile() directly.
    QAction *saveAct = nullptr;
    for (QAction *a : mw.findChildren<QAction*>()) {
        if (a->shortcut() == QKeySequence("Ctrl+S")) { saveAct = a; break; }
    }
    EXPECT("Ctrl+S action found", saveAct != nullptr);
    if (saveAct) {
        saveAct->trigger();
        QApplication::processEvents();
    }

    const unsigned m2_after_save = markersAt(e, 2);
    std::printf("  [debug] line-2 marker mask after save = 0x%x "
                "(orange bit 0x800000, green bit 0x400000)\n", m2_after_save);
    EXPECT("after save: line 2 has GREEN (marker 22)",
           (m2_after_save & (1u<<22)) != 0);
    EXPECT("after save: line 2 no longer has orange",
           (m2_after_save & (1u<<23)) == 0);

    // ── User-reported scenario v0.1.91 ──
    // "works on one line, orange to green works, but on another line
    //  after the save it changes to orange doesn't turn to green"
    // Sequence: edit line 4 (different line), save, expect green there too.
    std::printf("\n--- cross-line scenario ---\n");
    e->setCursorPosition(4, 0);
    e->insert("Q");
    QApplication::processEvents();
    const unsigned m4_after_edit = markersAt(e, 4);
    std::printf("  [debug] line-4 mask after edit = 0x%x\n", m4_after_edit);
    EXPECT("line 4 orange after cross-line edit",
           (m4_after_edit & (1u<<23)) != 0);
    if (saveAct) {
        saveAct->trigger();
        QApplication::processEvents();
    }
    const unsigned m4_after_save = markersAt(e, 4);
    std::printf("  [debug] line-4 mask after save = 0x%x\n", m4_after_save);
    EXPECT("line 4 GREEN after save (second save, different line)",
           (m4_after_save & (1u<<22)) != 0);
    EXPECT("line 4 no longer orange after that save",
           (m4_after_save & (1u<<23)) == 0);
    EXPECT("line 2 still GREEN (not regressed)",
           (markersAt(e, 2) & (1u<<22)) != 0);

    std::printf("\n%d passed, %d failed\n", g_pass, g_fail);
    return g_fail == 0 ? 0 : 1;
}
