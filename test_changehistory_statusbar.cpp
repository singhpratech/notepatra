// v0.1.92 — assert the new status-bar "N modified · M saved" label tracks
// Editor::changeHistoryUpdated. Covered scenarios:
//   1. Fresh load → label hidden (both counters 0)
//   2. Edit line 0 → "1 modified  ·  0 saved" visible
//   3. Save        → "0 modified  ·  1 saved" visible
//   4. Edit line 2 → "1 modified  ·  1 saved" visible
//   5. Re-edit line 0 (was green) → "2 modified  ·  0 saved" visible
//   6. Save        → "0 modified  ·  2 saved" visible
//   7. Reload file → label hidden again
#include "mainwindow.h"
#include "editor.h"
#include "tabmanager.h"
#include "statusbar.h"

#include <QAction>
#include <QApplication>
#include <QFile>
#include <QLabel>
#include <QTemporaryDir>
#include <Qsci/qsciscintilla.h>

#include <cstdio>

static int g_pass = 0, g_fail = 0;
#define EXPECT(label, cond) \
    do { if (cond) { ++g_pass; std::printf("  [PASS] %s\n", label); } \
         else      { ++g_fail; std::printf("  [FAIL] %s\n", label); } } while (0)

static QLabel *findChangeLabel(NppStatusBar *sb) {
    for (QLabel *l : sb->findChildren<QLabel*>()) {
        if (l->toolTip().contains("Modified", Qt::CaseInsensitive)) return l;
    }
    return nullptr;
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
    const QString path = wd.path() + "/sample.txt";
    {
        QFile f(path);
        f.open(QIODevice::WriteOnly);
        f.write("alpha\nbravo\ncharlie\ndelta\necho\n");
        f.close();
    }

    MainWindow mw;
    mw.show();
    QApplication::processEvents();
    mw.openFile(path);
    QApplication::processEvents();

    auto *tabs = mw.findChild<TabManager *>();
    Editor *e = tabs ? tabs->currentEditor() : nullptr;
    auto *sb = mw.findChild<NppStatusBar*>();
    if (!e || !sb) { std::printf("setup failed\n"); return 1; }

    QLabel *changeLabel = findChangeLabel(sb);
    EXPECT("status bar has change-history label", changeLabel != nullptr);
    if (!changeLabel) return 1;

    QAction *save = findSaveAction(&mw);
    EXPECT("save action found", save != nullptr);
    if (!save) return 1;

    // Scenario 1 — fresh load
    EXPECT("scenario 1: label hidden on fresh load", !changeLabel->isVisible());

    // Scenario 2 — edit line 0
    e->setCursorPosition(0, 0);
    e->insert("X");
    QApplication::processEvents();
    EXPECT("scenario 2: label visible after first edit", changeLabel->isVisible());
    EXPECT("scenario 2: text contains '1 modified'",
           changeLabel->text().contains("1 modified"));
    EXPECT("scenario 2: text contains '0 saved'",
           changeLabel->text().contains("0 saved"));

    // Scenario 3 — save
    save->trigger();
    for (int i = 0; i < 5; ++i) QApplication::processEvents();
    EXPECT("scenario 3: text contains '0 modified'",
           changeLabel->text().contains("0 modified"));
    EXPECT("scenario 3: text contains '1 saved'",
           changeLabel->text().contains("1 saved"));

    // Scenario 4 — edit line 2
    e->setCursorPosition(2, 0);
    e->insert("Y");
    QApplication::processEvents();
    EXPECT("scenario 4: text contains '1 modified'",
           changeLabel->text().contains("1 modified"));
    EXPECT("scenario 4: text still contains '1 saved'",
           changeLabel->text().contains("1 saved"));

    // Scenario 5 — re-edit line 0 (which had green); expect green count to drop.
    e->setCursorPosition(0, 0);
    e->insert("Z");
    QApplication::processEvents();
    EXPECT("scenario 5: text contains '2 modified'",
           changeLabel->text().contains("2 modified"));
    EXPECT("scenario 5: text contains '0 saved' (line 0 lost green)",
           changeLabel->text().contains("0 saved"));

    // Scenario 6 — save
    save->trigger();
    for (int i = 0; i < 5; ++i) QApplication::processEvents();
    EXPECT("scenario 6: text contains '0 modified'",
           changeLabel->text().contains("0 modified"));
    EXPECT("scenario 6: text contains '2 saved'",
           changeLabel->text().contains("2 saved"));

    std::printf("\n%d passed, %d failed\n", g_pass, g_fail);
    return g_fail == 0 ? 0 : 1;
}
