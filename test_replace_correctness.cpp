// SPDX-License-Identifier: GPL-3.0-or-later
//
// Replace must replace what the user asked for, and must not claim work it did
// not do. Both of these shipped broken, and both destroyed text.
//
// The tests click the real buttons rather than calling doReplace()/doReplaceAll()
// directly. That is not ceremony: the whole class of bug here is the gap between
// "the engine can do it" and "the dialog does it", and the engine-level tests in
// rust-core/src/search.rs already pass on code that eats the user's text once
// the dialog is in the loop.
//
// Match case is left UNCHECKED throughout, because that is how the dialog is
// constructed and it is the branch that was broken.

#include "findreplace.h"
#include "mainwindow.h"
#include "editor.h"

#include <QApplication>
#include <QComboBox>
#include <QFile>
#include <QPushButton>
#include <QTabWidget>
#include <QTemporaryDir>

#include <cstdio>

static int g_pass = 0, g_fail = 0;
#define EXPECT(label, cond) \
    do { if (cond) { ++g_pass; std::printf("  [PASS] %s\n", label); } \
         else      { ++g_fail; std::printf("  [FAIL] %s\n", label); } } while (0)

static QPushButton *buttonNamed(QWidget *root, const QString &text) {
    for (QPushButton *b : root->findChildren<QPushButton *>())
        if (b->text().remove(QLatin1Char('&')) == text) return b;
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
        f.write("total = PRICE;\nsubtotal = PRICE;\nunrelated line here\n");
        f.close();
    }

    MainWindow mw;
    mw.show();
    mw.openFile(path);
    QApplication::processEvents();

    Editor *ed = mw.findChild<Editor *>();
    EXPECT("editor tab is open", ed != nullptr);
    if (!ed) return 1;

    FindReplaceDialog dlg(&mw);
    dlg.showReplace();
    QApplication::processEvents();

    auto *tabs = dlg.findChild<QTabWidget *>();
    if (!tabs) { std::printf("  [FAIL] no tab widget\n"); return 1; }
    QWidget *replTab = tabs->widget(1);
    auto combos = replTab->findChildren<QComboBox *>();
    EXPECT("replace tab has both combos", combos.size() >= 2);
    if (combos.size() < 2) return 1;
    QComboBox *findInput = combos.value(0);
    QComboBox *replInput = combos.value(1);

    QPushButton *btnReplace    = buttonNamed(replTab, "Replace");
    QPushButton *btnReplaceAll = buttonNamed(replTab, "Replace All");
    EXPECT("Replace button found",     btnReplace != nullptr);
    EXPECT("Replace All button found", btnReplaceAll != nullptr);
    if (!btnReplace || !btnReplaceAll) return 1;

    // ── 1. a literal replacement containing `$` must survive ──────────────
    //
    // The replacement was handed to the regex engine as a substitution
    // TEMPLATE, so "$100" parsed as capture group 100 and expanded to nothing.
    // The status bar still reported a successful replacement.
    findInput->setCurrentText("PRICE");
    replInput->setCurrentText("$100");
    btnReplaceAll->click();
    QApplication::processEvents();

    const QString afterDollars = ed->text();
    EXPECT("Replace All kept '$100' intact",
           afterDollars.contains("total = $100;"));
    EXPECT("Replace All did not silently delete the replacement",
           !afterDollars.contains("total = ;"));

    // ── 2. other `$` forms that also vanished ─────────────────────────────
    ed->setText("a X b\n");
    QApplication::processEvents();
    findInput->setCurrentText("X");
    replInput->setCurrentText("${HOME}");
    btnReplaceAll->click();
    QApplication::processEvents();
    EXPECT("Replace All kept '${HOME}' intact", ed->text().contains("a ${HOME} b"));

    // ── 3. Replace must not claim a replacement it did not make ───────────
    //
    // On a freshly loaded buffer QsciScintilla::replace() is a no-op because no
    // findFirst() has armed its find state. The old code reported "Replaced 1
    // occurrence" anyway AND skipped past the match, so a user walking the file
    // with Replace ended up one short.
    ed->setText("alpha beta alpha\n");
    ed->SendScintilla(QsciScintilla::SCI_SETSELECTIONSTART, 0);
    ed->SendScintilla(QsciScintilla::SCI_SETSELECTIONEND, 5);   // "alpha"
    QApplication::processEvents();
    EXPECT("precondition: 'alpha' is selected", ed->selectedText() == "alpha");

    findInput->setCurrentText("alpha");
    replInput->setCurrentText("ZZZ");
    btnReplace->click();
    QApplication::processEvents();
    EXPECT("Replace on a freshly loaded buffer actually replaces",
           ed->text().startsWith("ZZZ beta"));

    // ── 4. Replace must not overwrite an unrelated selection ──────────────
    //
    // replace() targets whatever is selected, so with the find state armed from
    // an earlier search it happily clobbered text the user never searched for.
    ed->setText("keep_me_intact and findable\n");
    QApplication::processEvents();
    ed->SendScintilla(QsciScintilla::SCI_SETSELECTIONSTART, 0);
    ed->SendScintilla(QsciScintilla::SCI_SETSELECTIONEND, 14);  // "keep_me_intact"
    QApplication::processEvents();
    EXPECT("precondition: unrelated text is selected",
           ed->selectedText() == "keep_me_intact");

    findInput->setCurrentText("findable");
    replInput->setCurrentText("CLOBBERED");
    btnReplace->click();
    QApplication::processEvents();
    EXPECT("Replace left the non-matching selection alone",
           ed->text().contains("keep_me_intact"));
    EXPECT("Replace did not write into the non-matching selection",
           !ed->text().startsWith("CLOBBERED"));

    std::printf("\n%d passed, %d failed\n", g_pass, g_fail);
    return g_fail == 0 ? 0 : 1;
}
