// v0.1.91 — integration test for the Notepad++-style change-history strip.
// Verifies the manual SCN_MODIFIED / SCN_SAVEPOINTREACHED algorithm in
// editor.cpp paints marker 23 (orange) on edited lines and flips it to
// marker 22 (green) after save.
//
// Why a separate test: the prior Python-PyQt5 simulation proved the
// algorithm under direct manipulation of the bundled Scintilla; this test
// proves the same algorithm under Notepatra's actual Editor class with
// loadFile() + saveFile() — the real entry points users hit via the
// menubar.

#include "editor.h"

#include <QApplication>
#include <QFile>
#include <QTemporaryDir>
#include <Qsci/qsciscintilla.h>
#include <Qsci/qsciscintillabase.h>
#include <QStandardPaths>

#include <cstdio>

static int g_pass = 0, g_fail = 0;

#define EXPECT(label, cond) \
    do { if (cond) { ++g_pass; std::printf("  [PASS] %s\n", label); } \
         else      { ++g_fail; std::printf("  [FAIL] %s\n", label); } } while (0)

static unsigned markersAt(Editor &e, int line) {
    return (unsigned)e.SendScintilla(QsciScintillaBase::SCI_MARKERGET,
                                      (unsigned long)line);
}

int main(int argc, char *argv[]) {
    // Isolate config so prior-session tab restore doesn't run.
    QTemporaryDir cfg;
    qputenv("XDG_CONFIG_HOME", cfg.path().toUtf8());
    qputenv("XDG_DATA_HOME",   cfg.path().toUtf8());
    qputenv("QT_QPA_PLATFORM", "offscreen");

    QApplication app(argc, argv);

    QTemporaryDir wd;
    const QString path = wd.path() + "/sample.txt";
    {
        QFile f(path);
        f.open(QIODevice::WriteOnly);
        f.write("aaa\nbbb\nccc\nddd\n");
        f.close();
    }

    std::printf("=== test_change_history ===\n\n");

    Editor e;
    EXPECT("loadFile returns true",       e.loadFile(path));

    // After load: no change-history markers on any line.
    bool postLoadClean = true;
    for (int i = 0; i < 4; ++i) {
        unsigned m = markersAt(e, i);
        if ((m & ((1u<<22)|(1u<<23))) != 0) postLoadClean = false;
    }
    EXPECT("no markers after fresh load", postLoadClean);

    // Edit line 1 — insert at start.
    e.setCursorPosition(1, 0);
    e.insert("X");

    const unsigned m1_after_edit = markersAt(e, 1);
    EXPECT("line 1 has orange (marker 23) after edit",
           (m1_after_edit & (1u<<23)) != 0);
    EXPECT("line 0 has no marker (untouched)",
           (markersAt(e, 0) & ((1u<<22)|(1u<<23))) == 0);

    // saveFile() to the same path → setModified(false) → SCN_SAVEPOINTREACHED.
    EXPECT("saveFile returns true", e.saveFile(path));

    const unsigned m1_after_save = markersAt(e, 1);
    EXPECT("line 1 has GREEN (marker 22) after save",
           (m1_after_save & (1u<<22)) != 0);
    EXPECT("line 1 no longer has orange after save",
           (m1_after_save & (1u<<23)) == 0);

    // Edit line 1 AGAIN — should flip green→orange. (Column 1 is safe;
    // line 1 contains "Xaaa" after the first insert.)
    e.setCursorPosition(1, 1);
    e.insert("Y");

    const unsigned m1_re_edit = markersAt(e, 1);
    EXPECT("line 1 back to orange after re-edit",
           (m1_re_edit & (1u<<23)) != 0);
    EXPECT("line 1 no longer green after re-edit",
           (m1_re_edit & (1u<<22)) == 0);

    // Save again — line 1 should turn green a second time.
    EXPECT("second saveFile returns true", e.saveFile(path));
    const unsigned m1_after_2nd_save = markersAt(e, 1);
    EXPECT("line 1 GREEN after 2nd save",
           (m1_after_2nd_save & (1u<<22)) != 0);

    // Now edit a DIFFERENT line (line 2) AFTER a save. This is the user-
    // reported v0.1.91 failure mode: orange shows up but a subsequent save
    // does not turn it green.
    e.setCursorPosition(2, 0);
    e.insert("Z");

    const unsigned m2_after_edit = markersAt(e, 2);
    EXPECT("line 2 orange after edit (cross-line edit)",
           (m2_after_edit & (1u<<23)) != 0);

    // Save — line 2 should now go orange→green.
    EXPECT("third saveFile returns true", e.saveFile(path));
    const unsigned m2_after_save = markersAt(e, 2);
    std::printf("  [debug] line-2 marker mask after 3rd save = 0x%x "
                "(expected green bit 0x400000 set, orange bit 0x800000 clear)\n",
                m2_after_save);
    EXPECT("line 2 GREEN after save following cross-line edit",
           (m2_after_save & (1u<<22)) != 0);
    EXPECT("line 2 no longer orange after that save",
           (m2_after_save & (1u<<23)) == 0);
    EXPECT("line 1 STILL green after line-2 save (not regressed)",
           (markersAt(e, 1) & (1u<<22)) != 0);

    std::printf("\n%d passed, %d failed\n", g_pass, g_fail);
    return g_fail == 0 ? 0 : 1;
}
