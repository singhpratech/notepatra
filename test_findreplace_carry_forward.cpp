// v0.1.92 — assert Find→Replace tab switch carries the search string
// forward into the Replace tab's "Find what" field. Reverse direction
// works too (carry from Replace to Find when Find is empty). Also asserts
// that the dialog-level bottom status bar (Notepad++ style) fires with
// the right message for Find Next / Count / Replace All.
#include "findreplace.h"
#include "mainwindow.h"
#include "editor.h"
#include "tabmanager.h"

#include <QApplication>
#include <QComboBox>
#include <QFile>
#include <QLabel>
#include <QPushButton>
#include <QTabWidget>
#include <QTemporaryDir>

#include <cstdio>

static int g_pass = 0, g_fail = 0;
#define EXPECT(label, cond) \
    do { if (cond) { ++g_pass; std::printf("  [PASS] %s\n", label); } \
         else      { ++g_fail; std::printf("  [FAIL] %s\n", label); } } while (0)

int main(int argc, char *argv[]) {
    QTemporaryDir cfg;
    qputenv("XDG_CONFIG_HOME", cfg.path().toUtf8());
    qputenv("XDG_DATA_HOME",   cfg.path().toUtf8());
    qputenv("QT_QPA_PLATFORM", "offscreen");

    QApplication app(argc, argv);

    MainWindow mw;
    mw.show();
    QApplication::processEvents();

    FindReplaceDialog dlg(&mw);
    dlg.showFind();
    QApplication::processEvents();

    // Identify the two combo boxes by walking children of each tab.
    auto *tabs = dlg.findChild<QTabWidget *>();
    EXPECT("FindReplace dialog has QTabWidget", tabs != nullptr);
    if (!tabs) return 1;

    QWidget *findTab = tabs->widget(0);
    QWidget *replTab = tabs->widget(1);
    QComboBox *findInput = findTab->findChildren<QComboBox*>().value(0);
    QComboBox *replFindInput = replTab->findChildren<QComboBox*>().value(0);
    EXPECT("Find tab combo exists", findInput != nullptr);
    EXPECT("Replace tab Find combo exists", replFindInput != nullptr);
    if (!findInput || !replFindInput) return 1;

    // ─ scenario 1: user types in Find, switches to Replace ─
    findInput->setCurrentText("hello");
    QApplication::processEvents();
    EXPECT("Find input has 'hello'", findInput->currentText() == "hello");
    EXPECT("Replace input still empty", replFindInput->currentText().isEmpty());

    tabs->setCurrentIndex(1);  // → Replace
    QApplication::processEvents();
    EXPECT("scenario 1: Replace input carries 'hello' forward",
           replFindInput->currentText() == "hello");

    // ─ scenario 2: don't clobber existing replace value ─
    tabs->setCurrentIndex(0);
    QApplication::processEvents();
    findInput->setCurrentText("foo");
    replFindInput->setCurrentText("bar");
    tabs->setCurrentIndex(1);
    QApplication::processEvents();
    EXPECT("scenario 2: existing replace 'bar' NOT clobbered by 'foo'",
           replFindInput->currentText() == "bar");

    // ─ scenario 3: reverse direction — switch back to Find when empty ─
    tabs->setCurrentIndex(0);
    QApplication::processEvents();
    // Clear Find input
    findInput->setCurrentText("");
    replFindInput->setCurrentText("xyz");
    tabs->setCurrentIndex(1);  // arrive at Replace
    QApplication::processEvents();
    tabs->setCurrentIndex(0);  // back to Find
    QApplication::processEvents();
    EXPECT("scenario 3: empty Find input gets 'xyz' from Replace",
           findInput->currentText() == "xyz");

    // ─ scenario 4: dialog-level bottom status bar fires for Replace All ─
    QTemporaryDir wd;
    const QString path = wd.path() + "/sample.txt";
    {
        QFile f(path);
        f.open(QIODevice::WriteOnly);
        f.write("alpha bcd bravo\nbcd charlie\nbcd delta\n");
        f.close();
    }
    mw.openFile(path);
    QApplication::processEvents();

    // Find the dialog status label by walking the dialog hierarchy looking
    // for the italic QLabel parented to the QFrame at the bottom.
    QLabel *statusLabel = nullptr;
    for (QLabel *l : dlg.findChildren<QLabel*>()) {
        if (l->font().italic()) { statusLabel = l; break; }
    }
    EXPECT("scenario 4: dialog has italic status label", statusLabel != nullptr);
    if (!statusLabel) return 1;

    // Drive Find Next from Find tab — status should reflect "Found" or "Not found".
    tabs->setCurrentIndex(0);
    findInput->setCurrentText("bcd");
    QPushButton *findNextBtn = nullptr;
    for (QPushButton *b : findTab->findChildren<QPushButton*>()) {
        const QString plain = QString(b->text()).remove('&');
        if (plain == "Find Next") { findNextBtn = b; break; }
    }
    EXPECT("scenario 4: Find Next button exists", findNextBtn != nullptr);
    if (findNextBtn) {
        // Position cursor explicitly so the find starts from the top — the
        // test's earlier scenarios may have moved the cursor around.
        auto *tabs4 = mw.findChild<TabManager*>();
        if (tabs4 && tabs4->currentEditor()) {
            tabs4->currentEditor()->setCursorPosition(0, 0);
            QApplication::processEvents();
        }
        findNextBtn->click();
        QApplication::processEvents();
        EXPECT("scenario 4: status reflects Found",
               statusLabel->text().contains("Found"));
    }

    // Drive Replace All from Replace tab — status should say "X occurrence(s) replaced".
    tabs->setCurrentIndex(1);
    QApplication::processEvents();
    // Find Replace input + Replace All button on Replace tab.
    QComboBox *replInput = replTab->findChildren<QComboBox*>().value(1);  // 2nd combo
    QPushButton *replAllBtn = nullptr;
    for (QPushButton *b : replTab->findChildren<QPushButton*>()) {
        const QString plain = QString(b->text()).remove('&');
        if (plain == "Replace All") { replAllBtn = b; break; }
    }
    EXPECT("scenario 4: Replace tab Replace input exists", replInput != nullptr);
    EXPECT("scenario 4: Replace All button exists", replAllBtn != nullptr);
    if (replInput && replAllBtn) {
        replFindInput->setCurrentText("bcd");
        replInput->setCurrentText("XXX");
        replAllBtn->click();
        QApplication::processEvents();
        EXPECT("scenario 4: status bar shows replacement count",
               statusLabel->text().contains("Replace All") &&
               statusLabel->text().contains("3"));
    }

    // ─ scenario 5: Find Next cycle — STOP at end, wrap on 2nd press ─
    // (User-requested IDE behavior, v0.1.92.)
    {
        const QString cyclePath = wd.path() + "/cycle.txt";
        {
            QFile f(cyclePath);
            f.open(QIODevice::WriteOnly);
            f.write("zzz bcd zzz\nzzz bcd zzz\nzzz bcd zzz\n");
            f.close();
        }
        mw.openFile(cyclePath);
        QApplication::processEvents();
        auto *tabs2 = mw.findChild<TabManager*>();
        Editor *cycEd = tabs2 ? tabs2->currentEditor() : nullptr;
        EXPECT("scenario 5: cycle editor opened", cycEd != nullptr);
        if (cycEd) {
            // Move cursor to start so Find Next picks match #1 first.
            cycEd->setCursorPosition(0, 0);
            QApplication::processEvents();
            tabs->setCurrentIndex(0);  // Find tab
            findInput->setCurrentText("bcd");
            QApplication::processEvents();

            // 3 matches → 3 clicks should each say "Found".
            int line = -1, idx = -1;
            for (int i = 0; i < 3; ++i) {
                findNextBtn->click();
                QApplication::processEvents();
                cycEd->getCursorPosition(&line, &idx);
            }
            EXPECT("scenario 5: 3rd Find Next still 'Found'",
                   statusLabel->text().contains("Found"));
            int afterThird = line;

            // 4th click: should STOP — status says "Reached end".
            findNextBtn->click();
            QApplication::processEvents();
            EXPECT("scenario 5: 4th Find Next shows 'Reached end' (no wrap yet)",
                   statusLabel->text().contains("Reached end"));
            int line4, idx4; cycEd->getCursorPosition(&line4, &idx4);
            EXPECT("scenario 5: cursor stays put when cycle stops at end",
                   line4 == afterThird);

            // 5th click: actually wraps; status says "wrapped".
            findNextBtn->click();
            QApplication::processEvents();
            EXPECT("scenario 5: 5th Find Next wraps with 'Search wrapped'",
                   statusLabel->text().contains("wrapped") ||
                   statusLabel->text().contains("Search wrapped"));
            cycEd->getCursorPosition(&line, &idx);
            EXPECT("scenario 5: cursor jumped back to first match (line 0)",
                   line == 0);
        }
    }

    std::printf("\n%d passed, %d failed\n", g_pass, g_fail);
    return g_fail == 0 ? 0 : 1;
}
