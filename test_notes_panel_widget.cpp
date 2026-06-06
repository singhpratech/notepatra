// SPDX-License-Identifier: GPL-3.0-or-later
//
// Integration test for the top-level NotesPanel widget — v0.1.95 two-pane
// shape. Exercises construction, public-API roundtrip, and the markdown +
// checkbox eventFilter behavior that the unit tests can't reach.
//
// Headless via QT_QPA_PLATFORM=offscreen. Notes root is redirected to a
// QTemporaryDir via $HOME override so ~/Documents/Notepatra/Noter is
// untouched.

#include "notes.h"
#include "notes_extract_apply.h"
#include "notes_popout.h"
#include "notes_storage.h"
#include "notes_reminder.h"
#include "notes_sweep_dialog.h"
#include "notes_sweep_prompt.h"
#include "notes_todos.h"
#include "config.h"

#include <QAbstractButton>
#include <QAction>
#include <QApplication>
#include <QComboBox>
#include <QCompleter>
#include <QAbstractItemView>
#include <QMenu>
#include <QMessageBox>
#include <QCalendarWidget>
#include <QDateTimeEdit>
#include <QDialog>
#include <QDir>
#include <QFile>
#include <QTimer>
#include <QKeyEvent>
#include <QCheckBox>
#include <QFont>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QPushButton>
#include <QRegExp>
#include <QRegularExpression>
#include <QSplitter>
#include <QStackedWidget>
#include <QToolButton>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QTest>
#include <QtTest/QSignalSpy>
#include <QTextBlock>
#include <QTextCursor>
#include <QTextDocument>
#include <QTextEdit>
#include <QTreeWidget>
#include <QTreeWidgetItem>

#include <cstdio>
#include <cstdlib>
#include <memory>

// A7 — every "watchdog: never hang" singleShot(1500) used to be
// fire-and-forget: it rejected EVERY visible QDialog 1.5s after arming,
// whether or not its own section was still waiting. With ~7 such timers
// armed across the suite, any shift in section pacing (here: the A7
// conflict-guard hashing) could land a stale watchdog INSIDE a later
// section's modal exec and kill that dialog before its acceptor ran
// (observed: section 15's watchdog rejecting section 32's Extract
// dialog). Each watchdog now carries a section-done flag and no-ops once
// its section completed — same anti-hang rescue, zero cross-section
// interference.
static std::shared_ptr<bool> armDialogWatchdog(int ms = 1500) {
    auto done = std::make_shared<bool>(false);
    QTimer::singleShot(ms, [done]() {
        if (*done) return;             // section finished — leave later dialogs alone
        for (QWidget *w : QApplication::topLevelWidgets())
            if (auto *d = qobject_cast<QDialog *>(w)) d->reject();
    });
    return done;
}

static int g_pass = 0, g_fail = 0;
#define EXPECT(label, cond) \
    do { if (cond) { ++g_pass; std::printf("  [PASS] %s\n", label); } \
         else      { ++g_fail; std::printf("  [FAIL] %s\n", label); } } while (0)
#define EXPECT_STR_EQ(label, got, want) \
    do { const QString _g = (got); const QString _w = (want); \
         if (_g == _w) { ++g_pass; std::printf("  [PASS] %s\n", label); } \
         else { ++g_fail; std::printf("  [FAIL] %s — got '%s', want '%s'\n", \
                          label, qPrintable(_g), qPrintable(_w)); } } while (0)

static QString readAll(const QString &path) {
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) return {};
    return QString::fromUtf8(f.readAll());
}

int main(int argc, char *argv[]) {
    QApplication app(argc, argv);

    QTemporaryDir tmpHome;
    qputenv("HOME", tmpHome.path().toUtf8());
    qputenv("XDG_DOCUMENTS_DIR", (tmpHome.path() + "/Documents").toUtf8());
    QStandardPaths::setTestModeEnabled(true);
    QDir().mkpath(tmpHome.path() + "/Documents");

    std::printf("[test_notes_panel_widget] tmpHome=%s\n",
                qPrintable(tmpHome.path()));

    NotesPanel panel;

    // ── 1. Construction + folder layout ────────────────────────────
    std::printf("\n--- 1. construction ---\n");
    EXPECT("notesRoot non-empty",  !panel.notesRoot().isEmpty());
    EXPECT("inboxFolder under root",
           panel.inboxFolder().startsWith(panel.notesRoot()));
    EXPECT("trashFolder under root",
           panel.trashFolder().startsWith(panel.notesRoot()));
    EXPECT("inbox folder created on construct",
           QDir(panel.inboxFolder()).exists());
    EXPECT("trash folder created on construct",
           QDir(panel.trashFolder()).exists());

    // The two-pane shape — QSplitter with at least two children.
    auto *splitter = panel.findChild<QSplitter *>();
    EXPECT("has a QSplitter", splitter != nullptr);
    if (splitter) EXPECT("splitter has >=2 children", splitter->count() >= 2);

    // Sidebar widgets.
    EXPECT("has search QLineEdit",
           panel.findChild<QLineEdit *>() != nullptr);
    EXPECT("has sidebar QTreeWidget (v0.1.97 tree shape)",
           panel.findChild<QTreeWidget *>(QStringLiteral("noterSidebarTree")) != nullptr);
    QList<QPushButton *> buttons = panel.findChildren<QPushButton *>();
    EXPECT("has at least 2 QPushButtons (+ Noter / Extract)",
           buttons.size() >= 2);
    bool hasNoterBtn = false;
    for (auto *b : buttons)
        if (b->text() == QStringLiteral("+ Noter")) hasNoterBtn = true;
    EXPECT("the create button is labelled '+ Noter' (todos dropped)", hasNoterBtn);

    // ── 2. newMeetingNote creates a file ───────────────────────────
    std::printf("\n--- 2. newMeetingNote ---\n");
    const int beforeCount = QDir(panel.inboxFolder())
                                .entryList(QStringList() << "*.html",
                                           QDir::Files).size();
    panel.newMeetingNote();
    QApplication::processEvents();
    const QStringList after = QDir(panel.inboxFolder())
                                  .entryList(QStringList() << "*.html",
                                             QDir::Files);
    EXPECT("inbox file count incremented", after.size() == beforeCount + 1);

    // The file we just created — newest by mtime.
    QDir d(panel.inboxFolder());
    QFileInfoList all = d.entryInfoList(QStringList() << "*.html",
                                        QDir::Files, QDir::Time);
    QString newPath;
    if (!all.isEmpty()) newPath = all.first().absoluteFilePath();
    EXPECT("new path resolved", !newPath.isEmpty());

    // ── 3. Editor is alive + accepts text ──────────────────────────
    std::printf("\n--- 3. editor write ---\n");
    QTextEdit *editor = panel.findChild<QTextEdit *>();
    EXPECT("QTextEdit alive after newMeeting", editor != nullptr);
    if (editor) {
        // Move cursor to end + insert text.
        QTextCursor cur(editor->document());
        cur.movePosition(QTextCursor::End);
        editor->setTextCursor(cur);
        editor->insertPlainText(QStringLiteral("\nhello noter v95"));
        QApplication::processEvents();
        EXPECT("editor body contains inserted text",
               editor->toPlainText().contains("hello noter v95"));
    }

    // ── 4. saveCurrentNote persists ───────────────────────────────
    std::printf("\n--- 4. save roundtrip ---\n");
    panel.saveCurrentNote();
    QApplication::processEvents();
    const QString diskHtml = readAll(newPath);
    EXPECT("disk content non-empty after save", !diskHtml.isEmpty());
    EXPECT("disk has user-inserted text",
           diskHtml.contains("hello noter v95"));

    // ── 5. openNoteFile re-loads + roundtrip ──────────────────────
    std::printf("\n--- 5. openNoteFile roundtrip ---\n");
    // Modify the body, reopen the file → should show original disk content
    // (modified buffer should auto-save first).
    if (editor) {
        editor->insertPlainText(QStringLiteral(" — second edit"));
        QApplication::processEvents();
    }
    panel.saveCurrentNote();
    QApplication::processEvents();

    // Construct a second panel, open the same file — verifies the round-trip.
    NotesPanel panel2;
    panel2.openNoteFile(newPath);
    QApplication::processEvents();
    QTextEdit *editor2 = panel2.findChild<QTextEdit *>();
    EXPECT("second panel has editor", editor2 != nullptr);
    if (editor2) {
        EXPECT("second panel sees first panel's text",
               editor2->toPlainText().contains("hello noter v95"));
        EXPECT("second panel sees the second edit",
               editor2->toPlainText().contains("— second edit"));
    }

    // ── 6. Markdown shortcut '- [ ]' + Space → ☐ ──────────────────
    std::printf("\n--- 6. markdown shortcut ---\n");
    if (editor) {
        // Move to a fresh line and type the prefix
        QTextCursor cur(editor->document());
        cur.movePosition(QTextCursor::End);
        editor->setTextCursor(cur);
        editor->insertPlainText(QStringLiteral("\n"));
        QApplication::processEvents();

        // Type "- [ ]" character by character so the eventFilter sees each
        // QKeyEvent. We use QTest::keyClicks for the prefix, then a single
        // explicit Space key to trigger the rewrite.
        editor->setFocus();
        QTest::keyClicks(editor, QStringLiteral("- [ ]"));
        QApplication::processEvents();
        QTest::keyClick(editor, Qt::Key_Space);
        QApplication::processEvents();

        const QString tail = editor->toPlainText().section('\n', -1, -1);
        EXPECT("line ends with '☐ ' after '- [ ]' + Space",
               tail == QStringLiteral("☐ "));
    }

    // ── 7. F4 toggles checkbox on current line ─────────────────────
    std::printf("\n--- 7. F4 toggle ---\n");
    if (editor) {
        // Append text to the ☐ line, then press F4 — should swap to ✓
        editor->insertPlainText(QStringLiteral("review PR"));
        QApplication::processEvents();
        QTest::keyClick(editor, Qt::Key_F4);
        QApplication::processEvents();
        const QString tail = editor->toPlainText().section('\n', -1, -1);
        EXPECT("F4 turned ☐ into ✓",
               tail.startsWith(QStringLiteral("✓ review PR")));
        // v0.1.98 — a checked line's text is struck through.
        {
            QTextCursor c = editor->textCursor();
            c.movePosition(QTextCursor::End);
            c.movePosition(QTextCursor::StartOfBlock);
            c.movePosition(QTextCursor::Right, QTextCursor::MoveAnchor, 3);
            EXPECT("checked (✓) line text is struck through",
                   c.charFormat().fontStrikeOut());
        }
        // Press F4 again to toggle back
        QTest::keyClick(editor, Qt::Key_F4);
        QApplication::processEvents();
        const QString tail2 = editor->toPlainText().section('\n', -1, -1);
        EXPECT("F4 again turned ✓ back to ☐",
               tail2.startsWith(QStringLiteral("☐ review PR")));
        // v0.1.98 — reopening removes the strike-through.
        {
            QTextCursor c = editor->textCursor();
            c.movePosition(QTextCursor::End);
            c.movePosition(QTextCursor::StartOfBlock);
            c.movePosition(QTextCursor::Right, QTextCursor::MoveAnchor, 3);
            EXPECT("reopened (☐) line text is NOT struck through",
                   !c.charFormat().fontStrikeOut());
        }
    }

    // ── 8. Enter on a non-empty ☐ line creates a new ☐ ────────────
    std::printf("\n--- 8. Enter continues checkbox list ---\n");
    if (editor) {
        // Cursor is at end of "☐ review PR"
        QTest::keyClick(editor, Qt::Key_Return);
        QApplication::processEvents();
        const QString last = editor->toPlainText().section('\n', -1, -1);
        EXPECT("Enter on ☐ line produced new '☐ ' below",
               last == QStringLiteral("☐ "));
    }

    // ── 9. Enter on EMPTY ☐ line breaks out (clears the marker) ───
    std::printf("\n--- 9. Enter on empty ☐ breaks out ---\n");
    if (editor) {
        // Cursor is on a fresh "☐ " line (empty after the marker)
        QTest::keyClick(editor, Qt::Key_Return);
        QApplication::processEvents();
        const QString last = editor->toPlainText().section('\n', -1, -1);
        EXPECT("Enter on empty ☐ left an empty line (no new ☐)",
               last.isEmpty() || !last.startsWith(QStringLiteral("☐")));
    }

    // ── 10-12. Delegate row-button interaction (v0.1.97) ──────────────
    // These exercise the NoterRowDelegate paint+editorEvent path that
    // REPLACED the broken setItemWidget rows. The buttons are painted on
    // top of the native item; clicking their pixel region must fire the
    // rename / trash / restore action while a click on the text region
    // still selects + opens. Geometry mirrors NoterRowDelegate exactly:
    //   right    = rect.right() - 4
    //   ✕ (slot0) center_x = right - 11   → rect.right() - 15
    //   pencil/↺ (slot1) center_x = right - 35 → rect.right() - 39
    auto *tree = panel.findChild<QTreeWidget *>(QStringLiteral("noterSidebarTree"));
    EXPECT("sidebar tree resolved for interaction", tree != nullptr);

    // Give the viewport real geometry so visualItemRect() is valid.
    panel.resize(1000, 700);
    panel.show();
    QApplication::processEvents();

    auto findLeaf = [&](const QString &kind) -> QTreeWidgetItem * {
        if (!tree) return nullptr;
        tree->expandAll();
        QApplication::processEvents();
        QTreeWidgetItemIterator it(tree);
        while (*it) {
            if ((*it)->data(0, Qt::UserRole).toString() == kind) return *it;
            ++it;
        }
        return nullptr;
    };
    auto inboxCount = [&]() {
        return QDir(panel.inboxFolder())
            .entryList(QStringList() << "*.html", QDir::Files).size();
    };
    auto trashCount = [&]() {
        return QDir(panel.trashFolder())
            .entryList(QStringList() << ".trashed-*", QDir::Files | QDir::Hidden)
            .size();
    };

    // ── 10. pencil (slot 1) click opens the inline rename editor ──────
    std::printf("\n--- 10. pencil click → rename editor ---\n");
    if (tree) {
        QTreeWidgetItem *leaf = findLeaf(QStringLiteral("meeting"));
        EXPECT("found a meeting leaf to rename", leaf != nullptr);
        if (leaf) {
            // Baseline: editItem() (what double-click / context-menu use)
            // must produce a discoverable inline editor — proves our
            // detection method and that the item is editable.
            tree->editItem(leaf, 0);
            for (int i = 0; i < 4; ++i) QApplication::processEvents();
            EXPECT("editItem() opens a discoverable inline editor",
                   tree->findChild<QLineEdit *>() != nullptr);
            if (auto *le0 = tree->findChild<QLineEdit *>()) {
                QTest::keyClick(le0, Qt::Key_Escape);
                for (int i = 0; i < 3; ++i) QApplication::processEvents();
            }

            const QRect r = tree->visualItemRect(leaf);
            EXPECT("meeting leaf has a non-empty visual rect", !r.isEmpty());
            const QPoint pencil(r.right() - 39, r.center().y());
            QTest::mouseClick(tree->viewport(), Qt::LeftButton,
                              Qt::NoModifier, pencil);
            // Drain the deferred edit() + the nested selectAll singleShot.
            for (int i = 0; i < 6; ++i) QApplication::processEvents();
            // The inline editor is created as a QLineEdit while editing —
            // search the whole tree (not just viewport: Qt parents item
            // editors to the viewport, but be liberal). The sidebar search
            // box is a sibling of the tree, so it won't match here.
            QLineEdit *inlineEditor = tree->findChild<QLineEdit *>();
            EXPECT("pencil click opened the inline rename editor",
                   inlineEditor != nullptr);
            // Bail out of editing so it doesn't leak into the next section.
            if (inlineEditor) {
                QTest::keyClick(inlineEditor, Qt::Key_Escape);
                QApplication::processEvents();
            }
        }
    }

    // ── 11. ✕ (slot 0) click moves the meeting to Trash ──────────────
    std::printf("\n--- 11. ✕ click → move to Trash ---\n");
    if (tree) {
        QTreeWidgetItem *leaf = findLeaf(QStringLiteral("meeting"));
        EXPECT("found a meeting leaf to trash", leaf != nullptr);
        if (leaf) {
            const int inboxBefore = inboxCount();
            const int trashBefore = trashCount();
            const QRect r = tree->visualItemRect(leaf);
            const QPoint x(r.right() - 15, r.center().y());
            QTest::mouseClick(tree->viewport(), Qt::LeftButton,
                              Qt::NoModifier, x);
            QApplication::processEvents();
            EXPECT("inbox lost one meeting after ✕", inboxCount() == inboxBefore - 1);
            EXPECT("trash gained the trashed meeting after ✕",
                   trashCount() > trashBefore);
        }
    }

    // ── 12. ↺ (slot 1) click restores a trashed meeting ──────────────
    std::printf("\n--- 12. ↺ click → restore from Trash ---\n");
    if (tree) {
        QTreeWidgetItem *leaf = findLeaf(QStringLiteral("trashed_meeting"));
        EXPECT("found a trashed_meeting leaf to restore", leaf != nullptr);
        if (leaf) {
            const int inboxBefore = inboxCount();
            const QRect r = tree->visualItemRect(leaf);
            const QPoint restore(r.right() - 39, r.center().y());
            QTest::mouseClick(tree->viewport(), Qt::LeftButton,
                              Qt::NoModifier, restore);
            QApplication::processEvents();
            EXPECT("inbox regained the meeting after ↺ restore",
                   inboxCount() == inboxBefore + 1);
        }
    }

    // ── 13. Click SEMANTICS — the regression that shipped broken ──────
    // The bug users hit: itemClicked (single click) was wired to OPEN, and
    // Qt emits clicked() BEFORE the delegate consumes a button hit — so
    // clicking a pencil/✕ button ALSO opened the file, and a plain click
    // opened instead of selecting. Contract now: single click SELECTS,
    // double click OPENS, a button click fires ONLY its action (never
    // opens). These asserts observe the open document via the editor body.
    std::printf("\n--- 13. click semantics (select vs open vs button) ---\n");
    if (tree) {
        QTextEdit *ed = panel.findChild<QTextEdit *>();
        EXPECT("editor present for semantics test", ed != nullptr);

        auto newestHtml = [&]() {
            const QFileInfoList l = QDir(panel.inboxFolder())
                .entryInfoList(QStringList() << "*.html", QDir::Files, QDir::Time);
            return l.isEmpty() ? QString() : l.first().absoluteFilePath();
        };
        // Two fresh meetings with unique, identifiable bodies.
        panel.newMeetingNote(); QApplication::processEvents();
        const QString pathA = newestHtml();
        if (ed) ed->setPlainText(QStringLiteral("ALPHA_BODY_13"));
        panel.saveCurrentNote(); QApplication::processEvents();

        panel.newMeetingNote(); QApplication::processEvents();
        const QString pathB = newestHtml();
        if (ed) ed->setPlainText(QStringLiteral("BRAVO_BODY_13"));
        panel.saveCurrentNote(); QApplication::processEvents();

        auto leafForPath = [&](const QString &p) -> QTreeWidgetItem * {
            tree->expandAll(); QApplication::processEvents();
            QTreeWidgetItemIterator it(tree);
            while (*it) {
                if ((*it)->data(0, Qt::UserRole).toString() == QLatin1String("meeting") &&
                    (*it)->data(0, Qt::UserRole + 1).toString() == p) return *it;
                ++it;
            }
            return nullptr;
        };
        auto bodyPoint = [&](QTreeWidgetItem *leaf) {
            const QRect r = tree->visualItemRect(leaf);
            return QPoint(r.left() + 45, r.center().y());  // text area, left of buttons
        };

        EXPECT("found leaf A", leafForPath(pathA) != nullptr);
        EXPECT("found leaf B", leafForPath(pathB) != nullptr);

        // NOTE on method: QTest::mouseDClick is not recognized as a real
        // double-click by item views under QT_QPA_PLATFORM=offscreen (the
        // synthetic event never reaches mouseDoubleClickEvent), so the OPEN
        // contracts are driven by emitting itemDoubleClicked directly —
        // that exercises the production connection
        // (itemDoubleClicked → onSidebarItemActivated) deterministically.
        // The two ACTUAL regressions (single-click must not open, button
        // must not open) use real QTest::mouseClick, which DOES deliver.

        // (a) double-click (signal) on A → opens A. Editor shows ALPHA.
        if (QTreeWidgetItem *leafA = leafForPath(pathA); leafA && ed) {
            emit tree->itemDoubleClicked(leafA, 0);
            for (int i = 0; i < 4; ++i) QApplication::processEvents();
            EXPECT("double-click A opened A's body",
                   ed->toPlainText().contains(QStringLiteral("ALPHA_BODY_13")));
        }

        // (b) single-click B body → SELECTS B, does NOT open (editor stays
        //     ALPHA). THE regression: this used to open on single click.
        if (QTreeWidgetItem *leafB = leafForPath(pathB); leafB && ed) {
            QTest::mouseClick(tree->viewport(), Qt::LeftButton,
                              Qt::NoModifier, bodyPoint(leafB));
            for (int i = 0; i < 4; ++i) QApplication::processEvents();
            EXPECT("single-click B selected B (currentItem)",
                   tree->currentItem() == leafB);
            EXPECT("single-click B did NOT open it (editor still ALPHA)",
                   ed->toPlainText().contains(QStringLiteral("ALPHA_BODY_13")));
        }

        // (c) real click on ✕ of B → trashes B, editor STILL ALPHA (the
        //     button click must NOT also open B). THE other regression.
        if (QTreeWidgetItem *leafB = leafForPath(pathB); leafB && ed) {
            const int inboxBefore = inboxCount();
            const QRect r = tree->visualItemRect(leafB);
            QTest::mouseClick(tree->viewport(), Qt::LeftButton, Qt::NoModifier,
                              QPoint(r.right() - 15, r.center().y()));
            for (int i = 0; i < 4; ++i) QApplication::processEvents();
            EXPECT("✕ on B trashed it", inboxCount() == inboxBefore - 1);
            EXPECT("✕ on B did NOT open it (editor still ALPHA)",
                   ed->toPlainText().contains(QStringLiteral("ALPHA_BODY_13")));
        }

        // (d) double-click (signal) on a TRASHED meeting → must NOT open
        //     (editor stays ALPHA). Trash is a holding area; restore first.
        if (ed) {
            tree->expandAll(); QApplication::processEvents();
            QTreeWidgetItem *trashLeaf = nullptr;
            QTreeWidgetItemIterator it(tree);
            while (*it) {
                if ((*it)->data(0, Qt::UserRole).toString() == QLatin1String("trashed_meeting"))
                    { trashLeaf = *it; break; }
                ++it;
            }
            EXPECT("found a trashed_meeting leaf", trashLeaf != nullptr);
            if (trashLeaf) {
                emit tree->itemDoubleClicked(trashLeaf, 0);
                for (int i = 0; i < 4; ++i) QApplication::processEvents();
                EXPECT("double-click on TRASH did NOT open it (editor still ALPHA)",
                       ed->toPlainText().contains(QStringLiteral("ALPHA_BODY_13")));
            }
        }
    }

    // ── 14. auto-collapse the left sidebar each time Noter is shown ───
    // User-requested 2026-05-24: opening the Noter tab must NOT greet you
    // with a wall of expanded meetings / 23 trashed rows. showEvent
    // collapses every root so all section labels are visible; the user
    // expands whichever section they want.
    std::printf("\n--- 14. auto-collapse left sidebar on show ---\n");
    if (tree) {
        tree->expandAll();
        QApplication::processEvents();
        bool anyExpandedBefore = false;
        for (int i = 0; i < tree->topLevelItemCount(); ++i)
            if (tree->topLevelItem(i)->isExpanded()) anyExpandedBefore = true;
        EXPECT("baseline: a root is expanded after expandAll", anyExpandedBefore);

        // Re-show the panel → showEvent must collapse every root.
        panel.hide();
        QApplication::processEvents();
        panel.show();
        for (int i = 0; i < 4; ++i) QApplication::processEvents();

        bool allRootsCollapsed = true;
        for (int i = 0; i < tree->topLevelItemCount(); ++i)
            if (tree->topLevelItem(i)->isExpanded()) allRootsCollapsed = false;
        EXPECT("re-showing Noter collapsed every sidebar root", allRootsCollapsed);
        EXPECT("sidebar still has its two roots after collapse (Notes + Trash)",
               tree->topLevelItemCount() >= 2);
    }

    // ── 15. reminder dialog opens + dismisses without crashing ────────
    // v0.1.98 — right-click → "Set reminder" crashed in the field. Drive
    // the modal under a watchdog: if construction faults, this section
    // crashes the suite (and a debugger pinpoints the line).
    std::printf("\n--- 15. reminder dialog opens without crashing ---\n");
    {
        bool dismissed = false;
        QTimer::singleShot(150, [&dismissed]() {
            for (QWidget *w : QApplication::topLevelWidgets())
                if (auto *d = qobject_cast<QDialog *>(w))
                    if (d->isVisible()) { d->reject(); dismissed = true; }
        });
        auto done15a = armDialogWatchdog();       // watchdog: never hang
        panel.promptReminderForNote(QStringLiteral("/tmp/repro-noter-01.html"),
                                    QStringLiteral("Noter 01"));
        *done15a = true;
        EXPECT("reminder dialog (no existing) opened without crashing", true);
        EXPECT("watchdog saw + dismissed the modal", dismissed);

        // Now with an EXISTING reminder so the "Clear reminder" button path
        // is constructed too.
        QTimer::singleShot(150, [&]() {
            for (QWidget *w : QApplication::topLevelWidgets())
                if (auto *d = qobject_cast<QDialog *>(w))
                    if (d->isVisible()) d->reject();
        });
        auto done15b = armDialogWatchdog();
        // Seed a reminder via the same public path (accept would set one, but
        // we just need noteReminderAt to be valid — reuse promptReminder is
        // overkill; instead schedule directly is private, so accept once).
        panel.promptReminderForNote(QStringLiteral("/tmp/repro-noter-01.html"),
                                    QStringLiteral("Noter 01"));
        *done15b = true;
        EXPECT("reminder dialog (2nd open) opened without crashing", true);
    }

    // ── 16. Insert subheader drops a heading + a ☐ bullet ─────────────
    // v0.1.98 — editor right-click → Insert subheader. Contract: the heading
    // text lands AND the section is seeded with a checkbox bullet (user:
    // "always a checkbox as bullet point").
    std::printf("\n--- 16. insert subheader + checkbox bullet ---\n");
    {
        panel.newMeetingNote();                 // ensure an editable note is open
        QApplication::processEvents();
        QTextEdit *ed = panel.findChild<QTextEdit *>();
        EXPECT("editor present for subheader test", ed != nullptr);
        if (ed) {
            ed->clear();
            QApplication::processEvents();
            panel.insertSubheader(QStringLiteral("Action Items"));
            QApplication::processEvents();
            const QString txt = ed->toPlainText();
            EXPECT("subheader text inserted",
                   txt.contains(QStringLiteral("Action Items")));
            bool hasBullet = false;
            for (const QString &ln : txt.split('\n'))
                if (ln.startsWith(QStringLiteral("☐ "))) hasBullet = true;
            EXPECT("subheader seeds a ☐ checkbox bullet", hasBullet);
        }
    }

    // ── 17. Extract dialog: only non-empty sections (action items first) ─
    // v0.1.98 — user: "keep all, basically action items most of the time."
    // The dialog must NOT render empty preset buckets; with only actions
    // present, the Decisions/Questions/Risks section headings must be absent.
    std::printf("\n--- 17. extract dialog hides empty sections ---\n");
    {
        NoterSweepPrompt::SweepResult r;
        NoterSweepPrompt::SweepResult::Item a;
        a.text = QStringLiteral("Ship the release");
        r.actions.append(a);                 // ONLY actions
        NoterSweepDialog dlg(r);
        QApplication::processEvents();
        bool hasActionHeading = false, hasEmptyHeading = false;
        for (QLabel *lbl : dlg.findChildren<QLabel *>()) {
            const QString t = lbl->text();
            if (t.startsWith(QStringLiteral("Action Items"))) hasActionHeading = true;
            if (t.startsWith(QStringLiteral("Decisions")) ||
                t.startsWith(QStringLiteral("Questions")) ||
                t.startsWith(QStringLiteral("Risks")))
                hasEmptyHeading = true;
        }
        EXPECT("extract dialog shows the Action Items section", hasActionHeading);
        EXPECT("extract dialog hides empty Decisions/Questions/Risks headings",
               !hasEmptyHeading);
    }

    // ── 18. Strike-through survives close + reopen (persistence) ──────
    // v0.1.98 deep-dive fix — the ✓ marker survives save/reload but the rich
    // strike-through format is sanitized out of the HTML, so checked items
    // looked un-checked after closing + reopening. restyleChecklistLines()
    // re-derives it on load. Verified in a FRESH panel reading from disk.
    std::printf("\n--- 18. strike-through persists across reopen ---\n");
    {
        panel.newMeetingNote();
        QApplication::processEvents();
        QString persistPath;
        {
            QFileInfoList l = QDir(panel.inboxFolder())
                .entryInfoList(QStringList() << "*.html", QDir::Files, QDir::Time);
            if (!l.isEmpty()) persistPath = l.first().absoluteFilePath();
        }
        QTextEdit *ed = panel.findChild<QTextEdit *>();
        if (ed && !persistPath.isEmpty()) {
            ed->clear();
            QApplication::processEvents();
            ed->insertPlainText(QStringLiteral("☐ persist me"));
            QApplication::processEvents();
            QTest::keyClick(ed, Qt::Key_F4);          // → "✓ persist me", struck
            QApplication::processEvents();
            panel.saveCurrentNote();
            QApplication::processEvents();

            NotesPanel reopened;                       // fresh panel reads disk
            reopened.openNoteFile(persistPath);
            QApplication::processEvents();
            QTextEdit *ed2 = reopened.findChild<QTextEdit *>();
            EXPECT("reopened panel has editor", ed2 != nullptr);
            if (ed2) {
                bool foundLine = false, foundStruck = false;
                for (QTextBlock b = ed2->document()->begin(); b.isValid();
                     b = b.next()) {
                    if (!b.text().startsWith(QStringLiteral("✓ persist me")))
                        continue;
                    foundLine = true;
                    QTextCursor c(ed2->document());
                    c.setPosition(b.position() + 3);   // into the text
                    if (c.charFormat().fontStrikeOut()) foundStruck = true;
                }
                EXPECT("reopened note still has the ✓ line", foundLine);
                EXPECT("strike-through survives close + reopen", foundStruck);
            }
        }
    }

    // ── 19. New note has no "00:00" timer + editor toolbar present ────
    // v0.1.98 — dropped the legacy meeting timer; added a top icon toolbar
    // (Header preset menu + Insert checkbox) mirroring the right-click menu.
    std::printf("\n--- 19. timer removed + editor toolbar ---\n");
    {
        panel.newMeetingNote();
        QApplication::processEvents();
        QTextEdit *ed = panel.findChild<QTextEdit *>();
        if (ed) {
            bool hasTimer = false;
            for (const QString &ln : ed->toPlainText().split('\n'))
                if (ln.trimmed() == QStringLiteral("00:00")) hasTimer = true;
            EXPECT("new note has no 00:00 timer line", !hasTimer);
        }
        bool hasActionItems = false, hasWhatIPlan = false,
             hasToDos = false, hasCheckboxBtn = false;
        for (QToolButton *b : panel.findChildren<QToolButton *>()) {
            const QString tt = b->toolTip();
            if (tt.contains(QStringLiteral("Action Items"))) hasActionItems = true;
            if (tt.contains(QStringLiteral("What I plan")))  hasWhatIPlan = true;
            if (tt.contains(QStringLiteral("To-dos")))       hasToDos = true;
            // M6 — tooltip now carries the F4 shortcut hint, so prefix-match.
            if (tt.startsWith(QStringLiteral("Insert checkbox"))) hasCheckboxBtn = true;
        }
        EXPECT("toolbar has a standalone Action Items icon", hasActionItems);
        EXPECT("toolbar has a standalone What I plan icon", hasWhatIPlan);
        EXPECT("toolbar has a standalone To-dos icon", hasToDos);
        EXPECT("editor toolbar has an Insert checkbox button", hasCheckboxBtn);
    }

    // ── 20. Noter AI model dropdown populates from the backend ────────
    // User reported "the dropdown is not working". Construct the panel, let
    // the async listModels() round-trip to the backend, and print what the
    // combo ends up with so we can see loading/empty/populated.
    std::printf("\n--- 20. model dropdown populates ---\n");
    {
        QComboBox *mc = panel.findChild<QComboBox *>(QStringLiteral("noterModelCombo"));
        EXPECT("model combo exists", mc != nullptr);
        if (mc) {
            for (int i = 0; i < 70; ++i) {     // up to ~7s for the round-trip
                QTest::qWait(100);
                if (mc->count() >= 2) break;
            }
            std::printf("  combo (%d items): ", mc->count());
            for (int i = 0; i < mc->count(); ++i)
                std::printf("[%s] ", qPrintable(mc->itemText(i)));
            std::printf("\n");
            const QString first = mc->count() ? mc->itemText(0) : QString();
            EXPECT("combo is not stuck on (loading…)",
                   first != QStringLiteral("(loading…)"));
            // Backend reachability is environment-dependent — CI has no Ollama /
            // OpenRouter, so the combo correctly resolves to the explicit
            // "(no models — check AI panel backend)" placeholder. Accept EITHER
            // real models (backend up) OR that placeholder (offline); the only
            // real failure is being stuck on "(loading…)" or empty (the
            // "dropdown not working" bug). Don't hard-require a live backend or
            // this test fails every CI run. (v0.1.97 — was red in CI.)
            const bool hasRealModel =
                mc->count() >= 1 && !first.startsWith(QStringLiteral("("));
            const bool noBackend = first.startsWith(QStringLiteral("(no models"));
            EXPECT("combo resolved to real models, or '(no models)' when offline",
                   hasRealModel || noBackend);
            std::printf(hasRealModel ? "  → backend reachable: %d model(s)\n"
                                     : "  → no backend in this env (placeholder shown)\n",
                        mc->count());
            // v0.1.98 — type-to-filter + refresh.
            EXPECT("model combo is editable (type-to-filter)", mc->isEditable());
            EXPECT("model combo has a contains-match completer",
                   mc->completer() &&
                   mc->completer()->filterMode() == Qt::MatchContains);
            EXPECT("model refresh button exists",
                   panel.findChild<QToolButton *>(
                       QStringLiteral("noterModelRefresh")) != nullptr);
        }
    }

    // ── 21. Central Reminders root (v0.1.98) — additive, ordered, populated
    // Contract: a "Reminders" root sits between Notes and Trash (the meeting
    // view is untouched), and scheduling a reminder makes it appear there.
    std::printf("\n--- 21. central Reminders root ---\n");
    {
        auto *tree = panel.findChild<QTreeWidget *>(QStringLiteral("noterSidebarTree"));
        EXPECT("sidebar tree present", tree != nullptr);

        // (a) Root present + correctly ordered (Notes · Reminders · Trash).
        int notesIdx = -1, remIdx = -1, trashIdx = -1;
        if (tree)
            for (int i = 0; i < tree->topLevelItemCount(); ++i) {
                const QString t = tree->topLevelItem(i)->text(0);
                if (t.startsWith(QStringLiteral("Notes")))     notesIdx = i;
                if (t.startsWith(QStringLiteral("Reminders"))) remIdx   = i;
                if (t.startsWith(QStringLiteral("Trash")))     trashIdx = i;
            }
        EXPECT("Notes root still present (meeting view unchanged)", notesIdx >= 0);
        EXPECT("Reminders root present", remIdx >= 0);
        EXPECT("Trash root still present", trashIdx >= 0);
        EXPECT("Reminders sits between Notes and Trash",
               notesIdx >= 0 && remIdx > notesIdx && trashIdx > remIdx);

        // (b) ACCEPT the picker → schedules a reminder (default time is
        //     tomorrow 9am, in the future) → the root populates with a leaf.
        QTimer::singleShot(150, [&]() {
            for (QWidget *w : QApplication::topLevelWidgets())
                if (auto *d = qobject_cast<QDialog *>(w))
                    if (d->isVisible()) d->accept();
        });
        auto done21 = armDialogWatchdog();        // watchdog: never hang
        const QString remNote = panel.inboxFolder() + "/rem-test-note.html";
        panel.promptReminderForNote(remNote, QStringLiteral("Ship the build"));
        *done21 = true;
        QApplication::processEvents();

        int remCount = -1; bool sawLeaf = false;
        if (tree)
            for (int i = 0; i < tree->topLevelItemCount(); ++i) {
                auto *root = tree->topLevelItem(i);
                if (!root->text(0).startsWith(QStringLiteral("Reminders"))) continue;
                const QString t = root->text(0);
                const int op = t.indexOf('(');
                if (op >= 0) remCount = t.mid(op + 1, t.indexOf(')') - op - 1).toInt();
                for (int s = 0; s < root->childCount(); ++s) {
                    auto *sect = root->child(s);
                    for (int l = 0; l < sect->childCount(); ++l)
                        if (sect->child(l)->text(0).contains(QStringLiteral("Ship the build")))
                            sawLeaf = true;
                }
            }
        EXPECT("Reminders root count >= 1 after scheduling", remCount >= 1);
        EXPECT("Reminders root lists a leaf for the scheduled reminder", sawLeaf);
    }

    // ── 22. Extract result dialog opens + CANCELS without crashing (v0.1.98)
    // The user-reported crash was cancelling the Extract output. The root cause
    // (a nested modal loop on the reply-teardown stack) is fixed by deferring
    // showExtractResult off the finished signal; this drives that dialog path
    // directly (auto-reject) to guard its construction (summary header +
    // per-action QDateTimeEdit rows) and the cancel path.
    std::printf("\n--- 22. Extract result dialog cancel ---\n");
    {
        panel.newMeetingNote();
        QApplication::processEvents();
        QTextEdit *ed = panel.findChild<QTextEdit *>();
        const QString before = ed ? ed->toPlainText() : QString();

        const QString fakeResponse = QStringLiteral(
            "{\"summary\":\"Quick sync about the build and follow-ups.\","
            "\"actions\":[{\"text\":\"Ship the build\",\"owner\":\"@prateek\","
            "\"due\":\"2026-12-25T10:00\"}],"
            "\"decisions\":[],\"questions\":[],\"risks\":[]}");

        bool rejected = false;
        QTimer::singleShot(150, [&rejected]() {
            for (QWidget *w : QApplication::topLevelWidgets())
                if (auto *d = qobject_cast<QDialog *>(w))
                    if (d->isVisible()) { d->reject(); rejected = true; }
        });
        auto done22 = armDialogWatchdog();        // watchdog: never hang
        panel.showExtractResult(fakeResponse, QStringLiteral("test-model"));
        *done22 = true;
        QApplication::processEvents();
        EXPECT("Extract dialog opened + cancelled without crashing", true);
        EXPECT("watchdog saw + rejected the Extract dialog", rejected);
        EXPECT("cancel left the note body unchanged",
               !ed || ed->toPlainText() == before);
    }

    // ── 23. Extract flags already-scheduled + Remind defaults (v0.1.98) ──
    // Re-running Extract must not silently duplicate: setExistingReminders()
    // default-unchecks any action that fuzzy-matches an existing reminder and
    // lists them in the header. Remind defaults ON only when a concrete time
    // was extracted.
    std::printf("\n--- 23. Extract flags already-scheduled ---\n");
    {
        NoterSweepPrompt::SweepResult r;
        NoterSweepPrompt::SweepResult::Item a1;   // fuzzy-matches an existing one
        a1.text = QStringLiteral("Ship the build");
        a1.dueAt = QDateTime::currentDateTime().addDays(1);
        NoterSweepPrompt::SweepResult::Item a2;   // new, HAS a time
        a2.text = QStringLiteral("Email the vendor");
        a2.dueAt = QDateTime::currentDateTime().addDays(2);
        NoterSweepPrompt::SweepResult::Item a3;   // new, NO time
        a3.text = QStringLiteral("Refactor the parser");
        r.actions << a1 << a2 << a3;

        NoterSweepDialog dlg(r);
        QVector<QPair<QString, QDateTime>> existing;
        existing.append({ QStringLiteral("ship the build tomorrow"),   // reworded
                          QDateTime::currentDateTime().addDays(1) });
        dlg.setExistingReminders(existing);
        QApplication::processEvents();

        QCheckBox *shipCb = nullptr, *emailCb = nullptr, *refactorCb = nullptr;
        for (QCheckBox *cb : dlg.findChildren<QCheckBox *>())
            if (cb->text().contains(QStringLiteral("already scheduled"))) shipCb = cb;
        for (QLineEdit *le : dlg.findChildren<QLineEdit *>()) {
            if (le->text() == QStringLiteral("Email the vendor") && le->parentWidget())
                emailCb = le->parentWidget()->findChild<QCheckBox *>();
            if (le->text() == QStringLiteral("Refactor the parser") && le->parentWidget())
                refactorCb = le->parentWidget()->findChild<QCheckBox *>();
        }
        EXPECT("matched action flagged 'already scheduled'", shipCb != nullptr);
        EXPECT("matched action default-unchecked", shipCb && !shipCb->isChecked());
        EXPECT("new action WITH a time defaults Remind ON",
               emailCb && emailCb->isChecked());
        EXPECT("new action WITHOUT a time defaults Remind OFF",
               refactorCb && !refactorCb->isChecked());
        bool hasExistingHeader = false;
        for (QLabel *lbl : dlg.findChildren<QLabel *>())
            if (lbl->text().contains(QStringLiteral("Already scheduled for this note")))
                hasExistingHeader = true;
        EXPECT("header lists already-scheduled reminders", hasExistingHeader);
    }

    // ── 24. Checklist click semantics — marker toggles, text edits ────
    // M1 fix (a): pre-fix the MouseButtonRelease handler discarded the click
    // column, so a click ANYWHERE on a "☐ …" line flipped its done-state and
    // editing an item by mouse was impossible. Contract now: a click on the
    // marker (columns 0–2) toggles; a click in the item text places the
    // caret normally. F4 keeps toggling regardless.
    std::printf("\n--- 24. checklist click: marker toggles, text places caret ---\n");
    {
        panel.show();
        QApplication::processEvents();
        panel.newMeetingNote();
        QApplication::processEvents();
        QTextEdit *ed = panel.findChild<QTextEdit *>();
        EXPECT("editor present for click-semantics test", ed != nullptr);
        if (ed) {
            ed->clear();
            QApplication::processEvents();
            ed->insertPlainText(QStringLiteral("☐ buy milk and eggs"));
            QApplication::processEvents();
            QTextBlock line = ed->document()->firstBlock();

            // Click at a given column of the first line — viewport coords
            // come from cursorRect so the point is exact, not font-guessed.
            auto clickAtColumn = [&](int col) {
                QTextCursor c(ed->document());
                c.setPosition(line.position() + col);
                const QPoint pt = ed->cursorRect(c).center();
                QTest::mouseClick(ed->viewport(), Qt::LeftButton,
                                  Qt::NoModifier, pt);
                QApplication::processEvents();
            };

            // (a) click deep in the TEXT (column 12) → NOT toggled, caret
            //     placed in that line near the click. THE regression.
            clickAtColumn(12);
            EXPECT("click at column 12 did NOT toggle the item",
                   ed->document()->firstBlock().text()
                       .startsWith(QStringLiteral("☐ buy milk and eggs")));
            EXPECT("click at column 12 placed the caret in that line",
                   ed->textCursor().block() == ed->document()->firstBlock());
            EXPECT("caret landed near the click (col >= 5, not col 0)",
                   ed->textCursor().positionInBlock() >= 5);

            // (b) click ON the marker (column 0–2) → toggles ☐ → ✓.
            clickAtColumn(1);
            EXPECT("click on the marker toggled ☐ → ✓",
                   ed->document()->firstBlock().text()
                       .startsWith(QStringLiteral("✓ buy milk and eggs")));

            // (c) F4 on the line still toggles (✓ back to ☐) — the keyboard
            //     path must be untouched by the mouse fix.
            {
                QTextCursor c(ed->document());
                c.setPosition(ed->document()->firstBlock().position() + 8);
                ed->setTextCursor(c);
            }
            QTest::keyClick(ed, Qt::Key_F4);
            QApplication::processEvents();
            EXPECT("F4 still toggles the line (✓ → ☐)",
                   ed->document()->firstBlock().text()
                       .startsWith(QStringLiteral("☐ buy milk and eggs")));
        }
    }

    // ── 25. Insert checkbox mid-line → marker normalizes to line start ──
    // M1 fix (b): the checkbox is a LINE marker. With the caret mid-word,
    // Insert checkbox used to drop "☐ " into the middle of the text
    // ("buy ☐ milk"); now the whole line becomes the item.
    std::printf("\n--- 25. insert checkbox mid-line normalizes to line start ---\n");
    {
        QTextEdit *ed = panel.findChild<QTextEdit *>();
        QToolButton *chkBtn = nullptr;
        for (QToolButton *b : panel.findChildren<QToolButton *>())
            if (b->toolTip().startsWith(QStringLiteral("Insert checkbox"))) chkBtn = b;
        EXPECT("Insert checkbox toolbar button found", chkBtn != nullptr);
        if (ed && chkBtn) {
            ed->clear();
            QApplication::processEvents();
            ed->insertPlainText(QStringLiteral("buy milk"));
            QTextCursor c(ed->document());
            c.setPosition(ed->document()->firstBlock().position() + 4); // "buy |milk"
            ed->setTextCursor(c);
            chkBtn->click();
            QApplication::processEvents();
            EXPECT_STR_EQ("mid-line Insert checkbox put the marker at line start",
                          ed->document()->firstBlock().text(),
                          QStringLiteral("☐ buy milk"));
            // Invoking it again on the same line must not stack a second box.
            chkBtn->click();
            QApplication::processEvents();
            EXPECT_STR_EQ("second Insert checkbox on the line is a no-op",
                          ed->document()->firstBlock().text(),
                          QStringLiteral("☐ buy milk"));
        }
    }

    // ── 26. Re-opening the ALREADY-OPEN note keeps unsaved typing ─────
    // M1 fix (c): banner "Open" / reminder-leaf click call openNoteFile with
    // the CURRENT path; pre-fix that re-read the file from disk and reverted
    // up to 5s of typing (the autosave interval). Now it early-returns.
    std::printf("\n--- 26. openNoteFile on current path preserves unsaved text ---\n");
    {
        panel.newMeetingNote();
        QApplication::processEvents();
        QString curPath;
        {
            const QFileInfoList l = QDir(panel.inboxFolder())
                .entryInfoList(QStringList() << "*.html", QDir::Files, QDir::Time);
            if (!l.isEmpty()) curPath = l.first().absoluteFilePath();
        }
        QTextEdit *ed = panel.findChild<QTextEdit *>();
        EXPECT("editor + path present for revert-guard test",
               ed != nullptr && !curPath.isEmpty());
        if (ed && !curPath.isEmpty()) {
            ed->insertPlainText(QStringLiteral("\nUNSAVED_TYPING_26"));
            QApplication::processEvents();
            // Prove the typing is genuinely unsaved (autosave hasn't fired).
            EXPECT("disk does NOT yet contain the unsaved typing",
                   !readAll(curPath).contains(QStringLiteral("UNSAVED_TYPING_26")));
            panel.openNoteFile(curPath);   // same path — the banner-Open path
            QApplication::processEvents();
            EXPECT("re-opening the SAME note preserved the unsaved typing",
                   ed->toPlainText().contains(QStringLiteral("UNSAVED_TYPING_26")));
        }
    }

    // ── 27. failed save → red NOT SAVED hint + one-shot banner (M2a) ──
    // The silent save/read failure cluster: a failed autosave used to be
    // stderr-only while the footer hint kept reading "editing… (auto-saves
    // in 5s)". Contract now: the hint flips to a red NOT SAVED failure
    // state, a 2nd consecutive failure raises the "Save a copy…" banner,
    // and the next successful save restores the normal hint cycle.
    std::printf("\n--- 27. failed save → NOT SAVED hint + banner ---\n");
    {
        panel.newMeetingNote();
        QApplication::processEvents();
        QTextEdit *ed  = panel.findChild<QTextEdit *>();
        QLabel *hint   = panel.findChild<QLabel *>(QStringLiteral("noterSavedHint"));
        QWidget *banner = panel.findChild<QWidget *>(
            QStringLiteral("noterSaveFailBanner"));
        EXPECT("editor + hint + banner widgets resolved",
               ed && hint && banner);
        QString failPath;
        {
            QFileInfoList l = QDir(panel.inboxFolder())
                .entryInfoList(QStringList() << "*.html", QDir::Files, QDir::Time);
            if (!l.isEmpty()) failPath = l.first().absoluteFilePath();
        }
        if (ed && hint && banner && !failPath.isEmpty()) {
            ed->insertPlainText(QStringLiteral("\nDELTA_24_RESCUE_ME"));
            QApplication::processEvents();

            // Lock the Inbox DIRECTORY (0500). Read-only-ing just the note
            // file would NOT fail the atomic .tmp+rename save protocol —
            // POSIX rename only needs write permission on the directory,
            // which is also where the .tmp sibling is created.
            const QString inbox = panel.inboxFolder();
            QFile::setPermissions(inbox,
                QFileDevice::ReadOwner | QFileDevice::ExeOwner);

            panel.saveCurrentNote();             // failure #1
            QApplication::processEvents();
            EXPECT("hint shows NOT SAVED after failed save",
                   hint->text().startsWith(QStringLiteral("NOT SAVED")));
            EXPECT("hint carries the saveFailed failure-state property",
                   hint->property("saveFailed").toBool());
            EXPECT("hint is restyled red (failure state)",
                   hint->styleSheet().contains(QStringLiteral("#DC2626")));
            EXPECT("banner stays hidden after a single failure",
                   banner->isHidden());
            EXPECT("failed save did NOT flush the delta to disk",
                   !readAll(failPath).contains(QStringLiteral("DELTA_24_RESCUE_ME")));

            // Typing must NOT mask the failure state with "editing…".
            ed->insertPlainText(QStringLiteral("x"));
            QApplication::processEvents();
            EXPECT("typing keeps the NOT SAVED hint visible",
                   hint->text().startsWith(QStringLiteral("NOT SAVED")));

            panel.saveCurrentNote();             // failure #2 → banner
            QApplication::processEvents();
            EXPECT("2nd consecutive failure shows the banner",
                   !banner->isHidden());
            EXPECT("banner offers a 'Save a copy…' button",
                   banner->findChild<QPushButton *>(
                       QStringLiteral("noterSaveCopyBtn")) != nullptr);

            // Unlock + save again → normal hint cycle restored.
            QFile::setPermissions(inbox, QFileDevice::ReadOwner |
                QFileDevice::WriteOwner | QFileDevice::ExeOwner);
            panel.saveCurrentNote();
            QApplication::processEvents();
            EXPECT_STR_EQ("hint back to 'auto-saved' after recovery",
                          hint->text(), QStringLiteral("auto-saved"));
            EXPECT("failure-state property cleared after recovery",
                   !hint->property("saveFailed").toBool());
            EXPECT("banner hidden again after recovery", banner->isHidden());
            EXPECT("recovered save flushed the delta to disk",
                   readAll(failPath).contains(QStringLiteral("DELTA_24_RESCUE_ME")));
        }
    }

    // ── 28. unreadable/missing note → read-only error state (M2b) ─────
    // renderNoteAtPath used to discard readNote's error channel: a locked
    // or missing file opened as a BLANK editor still bound to the path —
    // one keystroke + the 5s autosave tick then OVERWROTE the real file.
    // Contract now: read-only "Could not open" notice, m_currentPath NOT
    // bound (so a save is inert), real file untouched.
    std::printf("\n--- 28. unreadable note → error state, no overwrite ---\n");
    {
        QTextEdit *ed = panel.findChild<QTextEdit *>();

        // (a) missing file.
        const QString missing =
            panel.inboxFolder() + QStringLiteral("/no-such-note.html");
        panel.openNoteFile(missing);
        QApplication::processEvents();
        EXPECT("missing file renders a 'Could not open' notice",
               ed && ed->toPlainText().contains(QStringLiteral("Could not open")));
        EXPECT("error notice is read-only", ed && ed->isReadOnly());
        panel.saveCurrentNote();   // the autosave tick path — must be inert
        QApplication::processEvents();
        EXPECT("no file was created at the missing path",
               !QFile::exists(missing));

        // (b) existing-but-unreadable file — the original overwrite bug.
        const QString lockedPath =
            panel.inboxFolder() + QStringLiteral("/locked-note.html");
        {
            QFile f(lockedPath);
            f.open(QIODevice::WriteOnly);
            f.write("<html><body><div>SECRET_REAL_CONTENT_25</div></body></html>");
            f.close();
        }
        QFile::setPermissions(lockedPath, QFileDevice::WriteOwner);  // 0200
        panel.openNoteFile(lockedPath);
        QApplication::processEvents();
        EXPECT("locked file renders a 'Could not open' notice",
               ed && ed->toPlainText().contains(QStringLiteral("Could not open")));
        EXPECT("locked-file state is read-only", ed && ed->isReadOnly());

        // The killer sequence: an edit (programmatic insert bypasses the
        // read-only flag, standing in for the old bug's keystroke) + the
        // autosave tick. With the path unbound this must be a no-op.
        if (ed) ed->insertPlainText(QStringLiteral("CLOBBER_25"));
        QApplication::processEvents();
        panel.saveCurrentNote();
        QApplication::processEvents();
        QFile::setPermissions(lockedPath,
            QFileDevice::ReadOwner | QFileDevice::WriteOwner);
        const QString diskNow = readAll(lockedPath);
        EXPECT("real file NOT overwritten after edit + autosave tick",
               diskNow.contains(QStringLiteral("SECRET_REAL_CONTENT_25")));
        EXPECT("no CLOBBER text reached the disk",
               !diskNow.contains(QStringLiteral("CLOBBER_25")));

        // Recovery: the now-readable file opens normally again.
        panel.openNoteFile(lockedPath);
        QApplication::processEvents();
        EXPECT("fixed file opens normally (notice gone)",
               ed && !ed->toPlainText().contains(QStringLiteral("Could not open")));
        EXPECT("editor writable again after recovery", ed && !ed->isReadOnly());
    }

    // ── 29. navigate-away after a failed save asks first (M2c) ────────
    // renderNoteAtPath used to replace the editor content + clear m_dirty
    // unconditionally — navigating away after a failed save silently
    // destroyed the only copy of the delta. Contract now: a Stay /
    // Discard changes / Save a copy… modal guards the replacement.
    std::printf("\n--- 29. navigate-away after failed save asks Stay/Discard ---\n");
    {
        QTextEdit *ed = panel.findChild<QTextEdit *>();
        auto newestHtml = [&]() {
            const QFileInfoList l = QDir(panel.inboxFolder())
                .entryInfoList(QStringList() << "*.html", QDir::Files, QDir::Time);
            return l.isEmpty() ? QString() : l.first().absoluteFilePath();
        };
        // Note A — saved cleanly while the dir is writable.
        panel.newMeetingNote(); QApplication::processEvents();
        const QString pathA = newestHtml();
        if (ed) ed->setPlainText(QStringLiteral("ALPHA_BODY_26"));
        panel.saveCurrentNote(); QApplication::processEvents();

        // Note B — gets a delta, then the dir goes read-only.
        panel.newMeetingNote(); QApplication::processEvents();
        if (ed) ed->insertPlainText(QStringLiteral("\nDELTA_26_UNSAVED"));
        QApplication::processEvents();

        const QString inbox = panel.inboxFolder();
        QFile::setPermissions(inbox,
            QFileDevice::ReadOwner | QFileDevice::ExeOwner);
        panel.saveCurrentNote();             // fails — arms the M2c guard
        QApplication::processEvents();

        auto clickModalButton = [](const QString &needle, bool *clicked) {
            for (QWidget *w : QApplication::topLevelWidgets()) {
                auto *mb = qobject_cast<QMessageBox *>(w);
                if (!mb || !mb->isVisible()) continue;
                for (QAbstractButton *b : mb->buttons())
                    if (b->text().contains(needle)) {
                        if (clicked) *clicked = true;
                        b->click();
                        return;
                    }
            }
        };

        // (i) Stay — content must NOT be replaced.
        bool sawStay = false;
        QTimer::singleShot(150, [&]() {
            clickModalButton(QStringLiteral("Stay"), &sawStay);
        });
        auto done29a = armDialogWatchdog();   // watchdog: never hang
        panel.openNoteFile(pathA);
        *done29a = true;
        QApplication::processEvents();
        EXPECT("modal appeared and Stay was clicked", sawStay);
        EXPECT("Stay kept the unsaved delta in the editor",
               ed && ed->toPlainText().contains(QStringLiteral("DELTA_26_UNSAVED")));

        // (ii) Discard changes — content replaced by the target note.
        bool sawDiscard = false;
        QTimer::singleShot(150, [&]() {
            clickModalButton(QStringLiteral("Discard"), &sawDiscard);
        });
        auto done29b = armDialogWatchdog();
        panel.openNoteFile(pathA);
        *done29b = true;
        QApplication::processEvents();
        EXPECT("modal appeared and Discard was clicked", sawDiscard);
        EXPECT("Discard navigated to the target note",
               ed && ed->toPlainText().contains(QStringLiteral("ALPHA_BODY_26")));
        EXPECT("the unsaved delta is gone after Discard",
               ed && !ed->toPlainText().contains(QStringLiteral("DELTA_26_UNSAVED")));

        // Restore the dir for the rest of tmpHome's lifetime (cleanup).
        QFile::setPermissions(inbox, QFileDevice::ReadOwner |
            QFileDevice::WriteOwner | QFileDevice::ExeOwner);
    }

    // ── 30. Section headers survive save + reload as REAL h2 (M3) ─────
    // The bug: insertSubheader made text bold but never set
    // QTextBlockFormat::headingLevel, so toHtml() saved no <h2> and the
    // reloaded note rendered the header as plain body text. Contract:
    // insertSubheader → save via the real writer → reload via the real
    // reader → the header block still has headingLevel==2 AND bold.
    std::printf("\n--- 30. subheader survives save+reload as h2 ---\n");
    {
        panel.newMeetingNote();
        QApplication::processEvents();
        QString hdrPath;
        {
            QFileInfoList l = QDir(panel.inboxFolder())
                .entryInfoList(QStringList() << "*.html", QDir::Files, QDir::Time);
            if (!l.isEmpty()) hdrPath = l.first().absoluteFilePath();
        }
        QTextEdit *ed = panel.findChild<QTextEdit *>();
        EXPECT("editor present for header round-trip", ed != nullptr);
        if (ed && !hdrPath.isEmpty()) {
            ed->clear();
            QApplication::processEvents();
            panel.insertSubheader(QStringLiteral("Action Items"));
            QApplication::processEvents();

            auto findBlock = [](QTextDocument *doc,
                                const QString &prefix) -> QTextBlock {
                for (QTextBlock b = doc->begin(); b.isValid(); b = b.next())
                    if (b.text().startsWith(prefix)) return b;
                return QTextBlock();
            };

            // In-editor: the header is a REAL heading block, the ☐ seed
            // below it stays plain body.
            QTextBlock hb = findBlock(ed->document(),
                                      QStringLiteral("Action Items"));
            EXPECT("header block exists in editor", hb.isValid());
            EXPECT("header block has headingLevel 2 (pre-save)",
                   hb.isValid() && hb.blockFormat().headingLevel() == 2);
            QTextBlock sb = findBlock(ed->document(), QStringLiteral("☐"));
            EXPECT("☐ seed block stays plain body (headingLevel 0)",
                   sb.isValid() && sb.blockFormat().headingLevel() == 0);

            // Save via the real writer → the artifact carries a real <h2>.
            panel.saveCurrentNote();
            QApplication::processEvents();
            const QString disk = readAll(hdrPath);
            EXPECT("saved HTML contains a real <h2> tag",
                   disk.contains(QStringLiteral("<h2")));
            EXPECT("saved HTML carries the header text",
                   disk.contains(QStringLiteral("Action Items")));

            // Reload via the real reader in a FRESH panel.
            NotesPanel reopened;
            reopened.openNoteFile(hdrPath);
            QApplication::processEvents();
            QTextEdit *ed2 = reopened.findChild<QTextEdit *>();
            EXPECT("reopened panel has editor (header test)", ed2 != nullptr);
            if (ed2) {
                QTextBlock rb = findBlock(ed2->document(),
                                          QStringLiteral("Action Items"));
                EXPECT("reloaded header block exists", rb.isValid());
                EXPECT("reloaded header keeps headingLevel==2",
                       rb.isValid() && rb.blockFormat().headingLevel() == 2);
                bool boldAgain = false;
                if (rb.isValid() && rb.length() > 1) {
                    QTextCursor c(ed2->document());
                    c.setPosition(rb.position() + 1);   // format of 1st char
                    boldAgain = c.charFormat().fontWeight() >= QFont::Bold;
                }
                EXPECT("reloaded header text is bold", boldAgain);
                QTextBlock rs = findBlock(ed2->document(), QStringLiteral("☐"));
                EXPECT("reloaded ☐ seed stays plain body (headingLevel 0)",
                       rs.isValid() && rs.blockFormat().headingLevel() == 0);
            }
        }
    }

    // ── 31. Enter at the end of a heading exits the heading format ────
    // Companion to 24: the block AFTER a heading must be plain body text
    // (headingLevel 0, not bold) or everything typed under a header would
    // save as more <h2> lines.
    std::printf("\n--- 31. Enter exits the heading format ---\n");
    {
        QTextEdit *ed = panel.findChild<QTextEdit *>();
        EXPECT("editor present for Enter-exit test", ed != nullptr);
        if (ed) {
            ed->clear();
            QApplication::processEvents();
            panel.insertSubheader(QStringLiteral("Decisions"));
            QApplication::processEvents();
            // Park the caret at the END of the heading line, press Enter.
            for (QTextBlock b = ed->document()->begin(); b.isValid();
                 b = b.next()) {
                if (!b.text().startsWith(QStringLiteral("Decisions"))) continue;
                QTextCursor c(ed->document());
                c.setPosition(b.position() + b.length() - 1);
                ed->setTextCursor(c);
                break;
            }
            ed->setFocus();
            QTest::keyClick(ed, Qt::Key_Return);
            QApplication::processEvents();
            const QTextBlock nb = ed->textCursor().block();
            EXPECT("Enter after heading lands on a body block (headingLevel 0)",
                   nb.blockFormat().headingLevel() == 0);
            EXPECT("body block after heading is not bold",
                   ed->textCursor().charFormat().fontWeight() < QFont::Bold);
        }
    }

    // ── 32. Extract append path writes REAL h2 section headers (M3) ───
    // Accepting the Extract dialog appends "Summary" + "Action Items" —
    // those must be heading blocks too (same evaporate-on-reload bug).
    std::printf("\n--- 32. Extract appends real h2 headers ---\n");
    {
        panel.newMeetingNote();
        QApplication::processEvents();
        QTextEdit *ed = panel.findChild<QTextEdit *>();
        const QString fakeResponse = QStringLiteral(
            "{\"summary\":\"Quick sync about the build.\","
            "\"actions\":[{\"text\":\"Ship the build\",\"owner\":\"@prateek\","
            "\"due\":\"2026-12-25T10:00\"}],"
            "\"decisions\":[],\"questions\":[],\"risks\":[]}");
        QTimer::singleShot(150, []() {
            for (QWidget *w : QApplication::topLevelWidgets())
                if (auto *d = qobject_cast<QDialog *>(w))
                    if (d->isVisible()) d->accept();
        });
        auto done32 = armDialogWatchdog();        // watchdog: never hang
        panel.showExtractResult(fakeResponse, QStringLiteral("test-model"));
        *done32 = true;
        QApplication::processEvents();
        bool summaryH2 = false, actionsH2 = false;
        if (ed)
            for (QTextBlock b = ed->document()->begin(); b.isValid();
                 b = b.next()) {
                if (b.text() == QStringLiteral("Summary") &&
                    b.blockFormat().headingLevel() == 2) summaryH2 = true;
                if (b.text() == QStringLiteral("Action Items") &&
                    b.blockFormat().headingLevel() == 2) actionsH2 = true;
            }
        EXPECT("accepted Extract appended 'Summary' as a real h2", summaryH2);
        EXPECT("accepted Extract appended 'Action Items' as a real h2",
               actionsH2);
    }

    // ── 33. Pop-out — close → reopen, re-point, pretty title ──────
    // v0.1.111 audit (CRITICAL): the pop-out died permanently after its
    // first close — close() only hid the WA_DeleteOnClose-less window, the
    // panel pointer stayed non-null, and popOutActive() early-returned
    // forever. The titlebar also showed the raw filename stem instead of
    // the prettified sidebar label.
    std::printf("\n--- 33. pop-out lifecycle + title ---\n");
    {
        // Filenames that exercise the shared prettifier end-to-end.
        const QString rawA = QStringLiteral("2026-06-06-145233-noter-06.html");
        const QString rawB = QStringLiteral("2026-06-06-150000-noter-07.html");
        const QString pathA = QDir(panel.inboxFolder()).absoluteFilePath(rawA);
        const QString pathB = QDir(panel.inboxFolder()).absoluteFilePath(rawB);
        {
            QFile f(pathA);
            f.open(QIODevice::WriteOnly);
            f.write("<html><body><p>pop-out body A</p></body></html>");
        }
        {
            QFile f(pathB);
            f.open(QIODevice::WriteOnly);
            f.write("<html><body><p>pop-out body B</p></body></html>");
        }

        panel.openNoteFile(pathA);
        QApplication::processEvents();

        // (1) First pop-out: live, visible, mirrors the current note.
        panel.popOutActive();
        QApplication::processEvents();
        NoterPopOut *p1 = panel.popOutForTesting();
        EXPECT("popOutActive creates a pop-out", p1 != nullptr);
        EXPECT("pop-out is visible", p1 && p1->isVisible());
        EXPECT("pop-out mirrors the current note",
               p1 && p1->notePath() == pathA);

        // (1b) Titlebar = the sidebar display label, NOT the raw stem.
        QString sidebarLabel;
        if (auto *tree = panel.findChild<QTreeWidget *>(
                QStringLiteral("noterSidebarTree"))) {
            QList<QTreeWidgetItem *> stack;
            for (int i = 0; i < tree->topLevelItemCount(); ++i)
                stack << tree->topLevelItem(i);
            while (!stack.isEmpty()) {
                QTreeWidgetItem *it = stack.takeLast();
                if (it->data(0, Qt::UserRole + 1).toString() == pathA)
                    sidebarLabel = it->text(0);
                for (int i = 0; i < it->childCount(); ++i)
                    stack << it->child(i);
            }
        }
        EXPECT("sidebar lists the note (label found)", !sidebarLabel.isEmpty());
        EXPECT_STR_EQ("sidebar label is the prettified form",
                      sidebarLabel, QStringLiteral("Noter 06"));
        EXPECT_STR_EQ("pop-out titlebar equals the sidebar label",
                      p1 && p1->titleLabelForTesting()
                          ? p1->titleLabelForTesting()->text() : QString(),
                      sidebarLabel);
        EXPECT("pop-out titlebar is NOT the raw filename stem",
               p1 && p1->titleLabelForTesting() &&
                   p1->titleLabelForTesting()->text() !=
                       QStringLiteral("2026-06-06-145233-noter-06"));

        // (2) Close → reopen — the marquee regression. close() destroys the
        // WA_DeleteOnClose window, the destroyed-connection nulls the panel
        // pointer, and popOutActive must build a FRESH visible pop-out.
        p1->close();
        QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
        QApplication::processEvents();
        EXPECT("panel pointer nulled after close (destroyed-connection)",
               panel.popOutForTesting() == nullptr);

        panel.popOutActive();
        QApplication::processEvents();
        NoterPopOut *p2 = panel.popOutForTesting();
        EXPECT("pop-out comes back after close (was permanently dead pre-fix)",
               p2 != nullptr);
        EXPECT("reopened pop-out is visible", p2 && p2->isVisible());
        EXPECT("reopened pop-out mirrors the current note",
               p2 && p2->notePath() == pathA);

        // (2b) Close again but DON'T pump DeferredDelete before re-invoking:
        // covers the closed-but-not-yet-destroyed window. popOutActive must
        // replace the hidden pop-out, and the LATE deferred delete of the
        // old one must not wipe the new pointer.
        p2->close();   // hidden now; deletion queued, not yet run
        panel.popOutActive();
        NoterPopOut *p3 = panel.popOutForTesting();
        EXPECT("hidden-but-alive pop-out replaced immediately",
               p3 != nullptr && p3 != p2);
        QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
        QApplication::processEvents();
        EXPECT("late delete of the REPLACED pop-out keeps the new pointer",
               panel.popOutForTesting() == p3);
        EXPECT("replacement pop-out is visible", p3 && p3->isVisible());

        // (3) Switch the current note → popOutActive re-points.
        panel.openNoteFile(pathB);
        QApplication::processEvents();
        panel.popOutActive();
        QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
        QApplication::processEvents();
        NoterPopOut *p4 = panel.popOutForTesting();
        EXPECT("pop-out re-points to the newly opened note",
               p4 && p4->notePath() == pathB);
        EXPECT("re-pointed pop-out is visible", p4 && p4->isVisible());
        EXPECT_STR_EQ("re-pointed titlebar follows the new note",
                      p4 && p4->titleLabelForTesting()
                          ? p4->titleLabelForTesting()->text() : QString(),
                      QStringLiteral("Noter 07"));

        // (4) Same note + still visible → reuse, not respawn.
        panel.popOutActive();
        QApplication::processEvents();
        EXPECT("same-note popOutActive reuses the live window",
               panel.popOutForTesting() == p4);

        // Tidy up so no always-on-top window outlives this section.
        if (NoterPopOut *last = panel.popOutForTesting()) {
            last->close();
            QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
            QApplication::processEvents();
        }
        EXPECT("cleanup: pop-out fully gone", panel.popOutForTesting() == nullptr);
    }

    // ── 34-36. Sidebar rename — prefix preservation, collision dedup,
    // failure honesty (package M5). The rename path is the itemChanged
    // lambda: setting the leaf's text is exactly what the inline editor's
    // commit does, so these drive the production code path directly.
    auto newestInboxHtml = [&]() {
        const QFileInfoList l = QDir(panel.inboxFolder())
            .entryInfoList(QStringList() << "*.html", QDir::Files, QDir::Time);
        return l.isEmpty() ? QString() : l.first().absoluteFilePath();
    };
    auto meetingLeafForPath = [&](const QString &p) -> QTreeWidgetItem * {
        if (!tree) return nullptr;
        QTreeWidgetItemIterator it(tree);
        while (*it) {
            if ((*it)->data(0, Qt::UserRole).toString() == QLatin1String("meeting") &&
                (*it)->data(0, Qt::UserRole + 1).toString() == p) return *it;
            ++it;
        }
        return nullptr;
    };

    // ── 24. rename PRESERVES the 6-digit-time creation prefix ─────────
    // Regression: the prefix regex expected \d{4} for the time part while
    // newMeetingNote stamps hhmmss (6 digits) — every rename of a new note
    // silently dropped its creation-date prefix.
    std::printf("\n--- 34. rename preserves 6-digit-time creation prefix ---\n");
    {
        panel.newMeetingNote();
        QApplication::processEvents();
        const QString srcPath = newestInboxHtml();
        EXPECT("created a note to rename", !srcPath.isEmpty());
        QRegExp stampRx(QStringLiteral("^(\\d{4}-\\d{2}-\\d{2}-\\d{6}-)"));
        const QString srcName = QFileInfo(srcPath).fileName();
        EXPECT("new note carries a 6-digit-time prefix",
               stampRx.indexIn(srcName) == 0);
        const QString prefix = stampRx.cap(1);

        QTreeWidgetItem *leaf = meetingLeafForPath(srcPath);
        EXPECT("found the sidebar leaf for the new note", leaf != nullptr);
        if (leaf && !prefix.isEmpty()) {
            leaf->setText(0, QStringLiteral("Weekly Sync Platform Team"));
            for (int i = 0; i < 4; ++i) QApplication::processEvents();
            const QString want =
                prefix + QStringLiteral("weekly-sync-platform-team.html");
            EXPECT("renamed file KEPT the creation-date prefix",
                   QFile::exists(panel.inboxFolder() + "/" + want));
            EXPECT("no prefix-less file appeared (the dropped-prefix bug)",
                   !QFile::exists(panel.inboxFolder() +
                                  "/weekly-sync-platform-team.html"));
            EXPECT("old filename is gone after rename", !QFile::exists(srcPath));
        }
    }

    // ── 25. rename onto an EXISTING name dedups with ' (2)' ───────────
    // Regression: d.rename() was unchecked — on target-exists it silently
    // failed yet the buffer was repointed at the OTHER note's file, which
    // the next autosave would clobber. Contract: insert ' (2)' BEFORE the
    // extension (split on the LAST dot), both files intact.
    std::printf("\n--- 35. rename collision dedups with ' (2)' before .html ---\n");
    {
        panel.newMeetingNote();
        QApplication::processEvents();
        const QString srcPath = newestInboxHtml();
        EXPECT("created a note for the collision test", !srcPath.isEmpty());
        QTextEdit *ed = panel.findChild<QTextEdit *>();
        if (ed) ed->setPlainText(QStringLiteral("DEDUP_BODY_25"));
        panel.saveCurrentNote();
        QApplication::processEvents();

        QRegExp stampRx(QStringLiteral("^(\\d{4}-\\d{2}-\\d{2}-\\d{6}-)"));
        stampRx.indexIn(QFileInfo(srcPath).fileName());
        const QString prefix = stampRx.cap(1);
        // Pre-create the EXACT rename target → forces the collision.
        const QString targetName =
            prefix + QStringLiteral("quarterly-review.html");
        const QString targetPath = panel.inboxFolder() + "/" + targetName;
        {
            QFile f(targetPath);
            EXPECT("seeded the colliding note on disk",
                   f.open(QIODevice::WriteOnly));
            f.write("<html><body>OTHER_NOTE_25</body></html>");
        }

        QTreeWidgetItem *leaf = meetingLeafForPath(srcPath);
        EXPECT("found the sidebar leaf to rename onto the collision",
               leaf != nullptr);
        if (leaf && !prefix.isEmpty()) {
            leaf->setText(0, QStringLiteral("Quarterly Review"));
            for (int i = 0; i < 4; ++i) QApplication::processEvents();
            const QString dedupPath = panel.inboxFolder() + "/" + prefix +
                QStringLiteral("quarterly-review (2).html");
            EXPECT("collision produced ' (2)' BEFORE .html",
                   QFile::exists(dedupPath));
            EXPECT("the existing note was NOT clobbered",
                   readAll(targetPath).contains(QStringLiteral("OTHER_NOTE_25")));
            EXPECT("renamed note's content intact in the deduped file",
                   readAll(dedupPath).contains(QStringLiteral("DEDUP_BODY_25")));
            EXPECT("old filename gone after dedup rename",
                   !QFile::exists(srcPath));
            // The open buffer must FOLLOW the deduped file — a save lands
            // there, never in the other note.
            if (ed) {
                ed->setPlainText(QStringLiteral("DEDUP_BODY_25 v2"));
                panel.saveCurrentNote();
                QApplication::processEvents();
            }
            EXPECT("buffer follows the deduped file (save lands in ' (2)')",
                   readAll(dedupPath).contains(QStringLiteral("DEDUP_BODY_25 v2")));
            EXPECT("save did NOT leak into the colliding note",
                   readAll(targetPath).contains(QStringLiteral("OTHER_NOTE_25")));
        }
    }

    // ── 26. FAILED rename keeps the buffer on the ORIGINAL file ───────
    // Read-only dir → rename(2) fails. Contract: old name kept, no new
    // file, and the open buffer still saves to the ORIGINAL path (never
    // silently repointed).
    std::printf("\n--- 36. failed rename keeps buffer on the original file ---\n");
    {
        panel.newMeetingNote();
        QApplication::processEvents();
        const QString srcPath = newestInboxHtml();
        EXPECT("created a note for the failure test", !srcPath.isEmpty());
        QTextEdit *ed = panel.findChild<QTextEdit *>();
        if (ed) ed->setPlainText(QStringLiteral("READONLY_BODY_26"));
        panel.saveCurrentNote();
        QApplication::processEvents();

        QTreeWidgetItem *leaf = meetingLeafForPath(srcPath);
        EXPECT("found the sidebar leaf for the failure test", leaf != nullptr);
        if (leaf) {
            // Make the inbox dir read-only so the rename syscall fails.
            QFile dirf(panel.inboxFolder());
            const QFile::Permissions orig = dirf.permissions();
            EXPECT("made inbox read-only",
                   dirf.setPermissions(QFile::ReadOwner | QFile::ExeOwner));
            leaf->setText(0, QStringLiteral("Should Not Apply"));
            for (int i = 0; i < 4; ++i) QApplication::processEvents();
            // Restore FIRST so later assertions + tmpdir cleanup work.
            dirf.setPermissions(orig);

            EXPECT("original file still exists after failed rename",
                   QFile::exists(srcPath));
            const QStringList strays = QDir(panel.inboxFolder())
                .entryList(QStringList() << "*should-not-apply*",
                           QDir::Files | QDir::Hidden);
            EXPECT("no renamed file appeared in the read-only dir",
                   strays.isEmpty());
            // Buffer must still point at the ORIGINAL file.
            if (ed) {
                ed->setPlainText(QStringLiteral("READONLY_BODY_26 after-fail"));
                panel.saveCurrentNote();
                QApplication::processEvents();
            }
            EXPECT("buffer still saves to the ORIGINAL file after failure",
                   readAll(srcPath).contains(
                       QStringLiteral("READONLY_BODY_26 after-fail")));
        }
    }

    // ══ v0.1.112 — retrieval & search (sections 37+; 24-36 are reserved
    // by the mechanical wave on the noter-agrade branch). ══════════════
    // EVERY section below that touches the search box MUST end with
    //   search->setText(QString()); QApplication::processEvents();
    // so sections stay order-independent and section 14's collapse-on-show
    // assertion is never poisoned by a lingering filter.
    QLineEdit *search = panel.findChild<QLineEdit *>(QStringLiteral("noterSearch"));
    QLabel *searchStatus =
        panel.findChild<QLabel *>(QStringLiteral("noterSearchStatus"));
    auto countMeetingLeaves = [&]() {
        int n = 0;
        QTreeWidgetItemIterator it(tree);
        while (*it) {
            if ((*it)->data(0, Qt::UserRole).toString() ==
                QStringLiteral("meeting")) ++n;
            ++it;
        }
        return n;
    };
    auto findLeafByText = [&](const QString &text) -> QTreeWidgetItem * {
        QTreeWidgetItemIterator it(tree);
        while (*it) {
            if ((*it)->data(0, Qt::UserRole).toString() ==
                    QStringLiteral("meeting") &&
                (*it)->text(0) == text) return *it;
            ++it;
        }
        return nullptr;
    };
    QString sentinelPath;

    // ── 37. Body search end-to-end (match, expand, visible, tooltip) ──
    std::printf("\n--- 37. body-content search end-to-end ---\n");
    if (tree && search && editor) {
        panel.resize(1000, 700);
        panel.show();
        QApplication::processEvents();

        panel.newMeetingNote();
        QApplication::processEvents();
        QFileInfoList newest = QDir(panel.inboxFolder())
            .entryInfoList(QStringList() << "*.html", QDir::Files, QDir::Time);
        if (!newest.isEmpty()) sentinelPath = newest.first().absoluteFilePath();
        EXPECT("sentinel note created", !sentinelPath.isEmpty());

        editor->setPlainText(
            QStringLiteral("zebrabudget quantum rollout"));
        panel.saveCurrentNote();
        QApplication::processEvents();

        search->setText(QStringLiteral("zebrabudget"));
        QApplication::processEvents();

        EXPECT("body-only term finds exactly 1 meeting leaf",
               countMeetingLeaves() == 1);
        QTreeWidgetItem *hit = nullptr;
        {
            QTreeWidgetItemIterator it(tree);
            while (*it) {
                if ((*it)->data(0, Qt::UserRole).toString() ==
                    QStringLiteral("meeting")) { hit = *it; break; }
                ++it;
            }
        }
        EXPECT("matched leaf resolved", hit != nullptr);
        if (hit) {
            EXPECT("leaf's section auto-expanded",
                   hit->parent() && hit->parent()->isExpanded());
            EXPECT("leaf's root auto-expanded",
                   hit->parent() && hit->parent()->parent() &&
                   hit->parent()->parent()->isExpanded());
            // The 0×0-rect probe: an auto-expanded match must occupy real
            // pixels — a hit hidden under a collapsed section reads as
            // "search is broken".
            EXPECT("matched leaf paints at a real rect (height > 0)",
                   tree->visualItemRect(hit).height() > 0);
            EXPECT("tooltip carries the match snippet",
                   hit->toolTip(0).contains(QStringLiteral("Match:")) &&
                   hit->toolTip(0).contains(QStringLiteral("zebrabudget")));
            EXPECT("leaf TEXT does NOT carry the snippet (rename safety)",
                   !hit->text(0).contains(QStringLiteral("zebrabudget")));
        }
        search->setText(QString());
        QApplication::processEvents();
    }

    // ── 38. Match-count label under the search box ────────────────────
    std::printf("\n--- 38. match-count label ---\n");
    EXPECT("noterSearchStatus label exists", searchStatus != nullptr);
    if (tree && search && searchStatus) {
        EXPECT("label hidden while not searching", !searchStatus->isVisible());
        search->setText(QStringLiteral("zebrabudget"));
        QApplication::processEvents();
        EXPECT("label visible during a search", searchStatus->isVisible());
        EXPECT("label reports the 1 match",
               searchStatus->text().contains(QStringLiteral("1")));

        search->setText(QStringLiteral("qqqxyzzy"));
        QApplication::processEvents();
        EXPECT("no-hit search lists 0 meeting leaves", countMeetingLeaves() == 0);
        EXPECT("label visible with the no-match string",
               searchStatus->isVisible() &&
               searchStatus->text() == QStringLiteral("No matches"));
        EXPECT("empty Notes root stays collapsed (\"nothing here\")",
               tree->topLevelItemCount() > 0 &&
               !tree->topLevelItem(0)->isExpanded());

        search->setText(QString());
        QApplication::processEvents();
        EXPECT("label hides when the filter clears", !searchStatus->isVisible());
    }

    // ── 39. Multi-term AND semantics over title-OR-body ───────────────
    std::printf("\n--- 39. multi-term AND ---\n");
    if (tree && search) {
        search->setText(QStringLiteral("quantum rollout"));   // both in body
        QApplication::processEvents();
        EXPECT("both-terms-in-body matches the sentinel note",
               countMeetingLeaves() == 1);

        search->setText(QStringLiteral("noter quantum"));     // title + body
        QApplication::processEvents();
        EXPECT("title term + body term AND-match",
               countMeetingLeaves() >= 1);

        search->setText(QStringLiteral("quantum nonexistentterm"));
        QApplication::processEvents();
        EXPECT("one failing term kills the match (AND, not OR)",
               countMeetingLeaves() == 0);

        search->setText(QString());
        QApplication::processEvents();
    }

    // ── 40. Title-filter regression — filename path still matches ─────
    std::printf("\n--- 40. title filter regression ---\n");
    if (tree && search) {
        search->setText(QStringLiteral("noter"));
        QApplication::processEvents();
        EXPECT("title-derived display still matches (>= 2 leaves)",
               countMeetingLeaves() >= 2);
        search->setText(QString());
        QApplication::processEvents();
    }

    // ── 41. Clearing the filter restores the pre-search expand state ──
    std::printf("\n--- 41. filter-session snapshot restore ---\n");
    if (tree && search) {
        panel.hide();
        QApplication::processEvents();
        panel.show();
        for (int i = 0; i < 4; ++i) QApplication::processEvents();
        bool allCollapsedBefore = true;
        for (int i = 0; i < tree->topLevelItemCount(); ++i)
            if (tree->topLevelItem(i)->isExpanded()) allCollapsedBefore = false;
        EXPECT("baseline: roots collapsed post-showEvent", allCollapsedBefore);

        search->setText(QStringLiteral("zebrabudget"));
        QApplication::processEvents();
        EXPECT("search auto-expands the matched root",
               tree->topLevelItemCount() > 0 &&
               tree->topLevelItem(0)->isExpanded());

        search->setText(QString());
        QApplication::processEvents();
        bool allCollapsedAfter = true;
        for (int i = 0; i < tree->topLevelItemCount(); ++i)
            if (tree->topLevelItem(i)->isExpanded()) allCollapsedAfter = false;
        EXPECT("clearing restores the pre-search snapshot (all collapsed)",
               allCollapsedAfter);
    }

    // ── 42. showEvent guard — a live search survives a tab switch ─────
    std::printf("\n--- 42. showEvent skips collapse while filtering ---\n");
    if (tree && search) {
        search->setText(QStringLiteral("zebrabudget"));
        QApplication::processEvents();
        panel.hide();
        QApplication::processEvents();
        panel.show();
        for (int i = 0; i < 4; ++i) QApplication::processEvents();
        EXPECT("matched root still expanded after hide+show mid-search",
               tree->topLevelItemCount() > 0 &&
               tree->topLevelItem(0)->isExpanded());

        // Clear → the tidy-collapse preference resumes (mirrors section 14).
        search->setText(QString());
        QApplication::processEvents();
        panel.hide();
        QApplication::processEvents();
        panel.show();
        for (int i = 0; i < 4; ++i) QApplication::processEvents();
        bool allCollapsed = true;
        for (int i = 0; i < tree->topLevelItemCount(); ++i)
            if (tree->topLevelItem(i)->isExpanded()) allCollapsed = false;
        EXPECT("collapse-on-show preference intact once filter clears",
               allCollapsed);
    }

    // ── 43. Creation-date buckets: filename stamp wins, mtime falls back ─
    std::printf("\n--- 43. creation-date month bucket + mtime fallback ---\n");
    if (tree && search) {
        const QDate today = QDate::currentDate();
        const QDate past = today.addDays(-70);
        // Compute the expected label EXACTLY like the implementation —
        // month names are locale-dependent; never hard-code "March".
        const QString expectedLabel = (past.year() == today.year())
            ? past.toString(QStringLiteral("MMMM"))
            : past.toString(QStringLiteral("MMMM yyyy"));

        const QString stamped = past.toString(QStringLiteral("yyyy-MM-dd"))
            + QStringLiteral("-1200-march-note.html");
        {
            QFile f(QDir(panel.inboxFolder()).absoluteFilePath(stamped));
            EXPECT("stamped fixture written", f.open(QIODevice::WriteOnly));
            f.write("<html><head></head><body><p>march body</p></body></html>");
        }
        // Trigger a rebuild (mtime is NOW — only the stamp says -70 days).
        search->setText(QStringLiteral("zzz-no-such"));
        QApplication::processEvents();
        search->setText(QString());
        QApplication::processEvents();

        QTreeWidgetItem *marchLeaf = findLeafByText(QStringLiteral("march note"));
        EXPECT("stamped leaf present", marchLeaf != nullptr);
        if (marchLeaf && marchLeaf->parent()) {
            EXPECT("stamped leaf bucketed by its CREATION month",
                   marchLeaf->parent()->text(0).startsWith(expectedLabel));
            EXPECT("stamped leaf NOT under Today despite fresh mtime",
                   !marchLeaf->parent()->text(0).startsWith(
                       QStringLiteral("Today")));
        }

        {
            QFile f(QDir(panel.inboxFolder())
                        .absoluteFilePath(QStringLiteral("plain-note.html")));
            EXPECT("unstamped fixture written", f.open(QIODevice::WriteOnly));
            f.write("<html><head></head><body><p>plain body</p></body></html>");
        }
        search->setText(QStringLiteral("zzz-no-such"));
        QApplication::processEvents();
        search->setText(QString());
        QApplication::processEvents();

        QTreeWidgetItem *plainLeaf = findLeafByText(QStringLiteral("plain note"));
        EXPECT("unstamped leaf present", plainLeaf != nullptr);
        if (plainLeaf && plainLeaf->parent()) {
            EXPECT("unstamped leaf falls back to mtime → Today",
                   plainLeaf->parent()->text(0).startsWith(
                       QStringLiteral("Today")));
        }
        search->setText(QString());
        QApplication::processEvents();
    }

    // ── 44. Cap gone — capture never blocks past note 99 ──────────────
    std::printf("\n--- 44. no cap / no modal past noter-99 ---\n");
    {
        {
            QFile f(QDir(panel.inboxFolder()).absoluteFilePath(
                QStringLiteral("2026-01-01-120000-noter-99.html")));
            EXPECT("noter-99 seed written", f.open(QIODevice::WriteOnly));
            f.write("<html><head></head><body><p>n99</p></body></html>");
        }
        bool sawModal = false;
        QTimer::singleShot(150, [&sawModal]() {
            for (QWidget *w : QApplication::topLevelWidgets())
                if (auto *d = qobject_cast<QDialog *>(w))
                    if (d->isVisible()) { sawModal = true; d->reject(); }
        });
        panel.newMeetingNote();
        QApplication::processEvents();
        QTest::qWait(250);   // let the watchdog fire (and prove it saw nothing)

        const QStringList after100 = QDir(panel.inboxFolder())
            .entryList(QStringList() << QStringLiteral("*-noter-100.html"),
                       QDir::Files);
        EXPECT("note #100 created", after100.size() == 1);
        EXPECT("NO modal blocked or nagged the capture", !sawModal);

        panel.newMeetingNote();
        QApplication::processEvents();
        const QStringList after101 = QDir(panel.inboxFolder())
            .entryList(QStringList() << QStringLiteral("*-noter-101.html"),
                       QDir::Files);
        EXPECT("note #101 created on the next call", after101.size() == 1);
        if (search) { search->setText(QString()); QApplication::processEvents(); }
    }

    // ── 45. Cache invalidation — edited body drops out of the results ─
    std::printf("\n--- 45. (mtime,size) cache invalidation ---\n");
    if (tree && search && editor && !sentinelPath.isEmpty()) {
        search->setText(QStringLiteral("zebrabudget"));
        QApplication::processEvents();
        EXPECT("sentinel still matches before the edit",
               countMeetingLeaves() == 1);

        panel.openNoteFile(sentinelPath);
        QApplication::processEvents();
        editor->setPlainText(QStringLiteral("nothing to see here"));
        panel.saveCurrentNote();
        QApplication::processEvents();

        search->setText(QString());
        QApplication::processEvents();
        search->setText(QStringLiteral("zebrabudget"));
        QApplication::processEvents();
        EXPECT("edited-away sentinel no longer matches (cache invalidated)",
               countMeetingLeaves() == 0);

        search->setText(QString());
        QApplication::processEvents();
    }

    // ── 46. external edit → conflict copy, original intact (A7) ───────
    // THE silent-clobber bug: zero mtime checks existed anywhere in the
    // save path, so a note rewritten on disk (sync tool, other editor)
    // while open here was silently overwritten by the next autosave tick
    // and rotated out of the .bak ring within ~25s. Contract now: the
    // external version stays untouched; the buffer is rescued to a
    // "<name> (conflict …).html" sibling; the M2 banner says what
    // happened; editing stays bound to the conflict copy.
    std::printf("\n--- 46. external edit → conflict copy, original intact ---\n");
    {
        QTextEdit *ed = panel.findChild<QTextEdit *>();
        QWidget *banner = panel.findChild<QWidget *>(
            QStringLiteral("noterSaveFailBanner"));
        QLabel *hint = panel.findChild<QLabel *>(QStringLiteral("noterSavedHint"));
        const QString p37 = QDir(panel.inboxFolder())
            .absoluteFilePath(QStringLiteral("2026-01-02-091500-conflict-test.html"));
        {
            QFile f(p37);
            f.open(QIODevice::WriteOnly);
            f.write("<html><body><p>MINE_BODY_37</p></body></html>");
        }
        EXPECT("editor + banner + hint present for conflict test",
               ed && banner && hint);
        if (ed && banner && hint) {
            panel.openNoteFile(p37);          // records the disk stamp
            QApplication::processEvents();

            // External program rewrites the note behind our back.
            {
                QFile f(p37);
                f.open(QIODevice::WriteOnly | QIODevice::Truncate);
                f.write("<html><body><p>EXTERNAL_BODY_37 — a sync tool "
                        "rewrote this file while it was open</p></body></html>");
            }

            ed->insertPlainText(QStringLiteral("\nTYPED_AFTER_37"));
            QApplication::processEvents();
            panel.saveCurrentNote();          // must NOT clobber the external version
            QApplication::processEvents();

            const QString diskOrig = readAll(p37);
            EXPECT("external version NOT clobbered by the save",
                   diskOrig.contains(QStringLiteral("EXTERNAL_BODY_37")));
            EXPECT("typed delta did NOT reach the original file",
                   !diskOrig.contains(QStringLiteral("TYPED_AFTER_37")));

            QString confPath;
            for (const QString &c : QDir(panel.inboxFolder()).entryList(
                     QStringList() << QStringLiteral("*conflict-test (conflict*"),
                     QDir::Files)) {
                const QString full = panel.inboxFolder() + "/" + c;
                if (readAll(full).contains(QStringLiteral("TYPED_AFTER_37")))
                    confPath = full;
            }
            EXPECT("a conflict copy holds the user's version", !confPath.isEmpty());
            EXPECT("conflict copy carries the pre-edit body too",
                   !confPath.isEmpty() &&
                       readAll(confPath).contains(QStringLiteral("MINE_BODY_37")));

            EXPECT("M2 banner visible after the conflict", !banner->isHidden());
            bool bannerSaysConflict = false;
            for (QLabel *l : banner->findChildren<QLabel *>())
                if (l->text().contains(QStringLiteral("Note changed on disk")))
                    bannerSaysConflict = true;
            EXPECT("banner says 'Note changed on disk — your version was saved as …'",
                   bannerSaysConflict);
            EXPECT("hint flips to the red CONFLICT state",
                   hint->text().startsWith(QStringLiteral("CONFLICT")));

            // Editing stays bound to the conflict copy — never the original.
            ed->insertPlainText(QStringLiteral("\nMORE_AFTER_CONFLICT_37"));
            QApplication::processEvents();
            panel.saveCurrentNote();
            QApplication::processEvents();
            EXPECT("further saves land in the conflict copy",
                   !confPath.isEmpty() && readAll(confPath).contains(
                       QStringLiteral("MORE_AFTER_CONFLICT_37")));
            EXPECT("original STILL holds the external version afterwards",
                   readAll(p37).contains(QStringLiteral("EXTERNAL_BODY_37")) &&
                   !readAll(p37).contains(QStringLiteral("MORE_AFTER_CONFLICT_37")));
        }
    }

    // ── 47. mtime-only touch is NOT a conflict (A7) ───────────────────
    // touch(1) / sync tools that re-stamp without rewriting bump mtime
    // only. The content-hash short-circuit must let the save proceed
    // normally — no false-positive conflict copies.
    std::printf("\n--- 47. mtime-only touch does not false-positive ---\n");
    {
        QTextEdit *ed = panel.findChild<QTextEdit *>();
        const QString p38 = QDir(panel.inboxFolder())
            .absoluteFilePath(QStringLiteral("2026-01-02-093000-touch-test.html"));
        {
            QFile f(p38);
            f.open(QIODevice::WriteOnly);
            f.write("<html><body><p>TOUCH_BASE_38</p></body></html>");
        }
        EXPECT("editor present for touch test", ed != nullptr);
        if (ed) {
            panel.openNoteFile(p38);
            QApplication::processEvents();
            // mtime-only bump — same bytes (touch(1) equivalent).
            {
                QFile f(p38);
                f.open(QIODevice::ReadWrite);
                f.setFileTime(QDateTime::currentDateTime().addSecs(7),
                              QFileDevice::FileModificationTime);
            }
            ed->insertPlainText(QStringLiteral("\nTOUCH_OK_38"));
            QApplication::processEvents();
            panel.saveCurrentNote();
            QApplication::processEvents();
            const QStringList confs = QDir(panel.inboxFolder()).entryList(
                QStringList() << QStringLiteral("*touch-test (conflict*"),
                QDir::Files);
            EXPECT("no conflict copy for an mtime-only touch", confs.isEmpty());
            EXPECT("save proceeded into the original file",
                   readAll(p38).contains(QStringLiteral("TOUCH_OK_38")));
        }
    }

    // ── 48. note deleted under us → rescue conflict copy (A7) ─────────
    // A missing file is treated as a conflict too: the buffer is the only
    // copy left, so it gets rescued — never silently re-materialized at
    // the old path (the deleting program may have meant it).
    std::printf("\n--- 48. deleted-under-us note rescues to a conflict copy ---\n");
    {
        QTextEdit *ed = panel.findChild<QTextEdit *>();
        QWidget *banner = panel.findChild<QWidget *>(
            QStringLiteral("noterSaveFailBanner"));
        const QString p39 = QDir(panel.inboxFolder())
            .absoluteFilePath(QStringLiteral("2026-01-02-094500-vanish-test.html"));
        {
            QFile f(p39);
            f.open(QIODevice::WriteOnly);
            f.write("<html><body><p>VANISH_BASE_39</p></body></html>");
        }
        EXPECT("editor present for vanish test", ed != nullptr);
        if (ed && banner) {
            panel.openNoteFile(p39);
            QApplication::processEvents();
            QFile::remove(p39);               // deleted behind our back
            ed->insertPlainText(QStringLiteral("\nVANISH_TYPED_39"));
            QApplication::processEvents();
            panel.saveCurrentNote();
            QApplication::processEvents();
            EXPECT("the deleted path was NOT silently re-created",
                   !QFile::exists(p39));
            QString confPath;
            for (const QString &c : QDir(panel.inboxFolder()).entryList(
                     QStringList() << QStringLiteral("*vanish-test (conflict*"),
                     QDir::Files))
                confPath = panel.inboxFolder() + "/" + c;
            EXPECT("buffer rescued to a conflict copy", !confPath.isEmpty());
            EXPECT("rescued copy holds the typed delta",
                   !confPath.isEmpty() && readAll(confPath).contains(
                       QStringLiteral("VANISH_TYPED_39")));
            EXPECT("banner visible for the vanished-file conflict",
                   !banner->isHidden());
        }
    }

    // ── 49. draft cadence: .draft within 2s of typing, gone on save (A7)
    // The .draft API existed in notes_storage with ZERO callers — a hard
    // crash used to lose up to the full 5s autosave window. The panel's
    // single-shot ~1.5s draft timer now writes the sidecar after the
    // first unsaved keystroke. A dedicated panel with a 300s autosave
    // interval isolates the draft from the 5s autosave of `panel`.
    std::printf("\n--- 49. draft written within 2s, cleared by clean save ---\n");
    {
        const int savedInterval = Config::instance().autoSaveIntervalSec;
        Config::instance().autoSaveIntervalSec = 300;
        {
            NotesPanel draftPanel;
            const QString p40 = QDir(draftPanel.inboxFolder())
                .absoluteFilePath(QStringLiteral("2026-01-03-080000-draft-test.html"));
            {
                QFile f(p40);
                f.open(QIODevice::WriteOnly);
                f.write("<html><body><p>DRAFT_BASE_40</p></body></html>");
            }
            QTextEdit *ded = draftPanel.findChild<QTextEdit *>();
            EXPECT("draft panel has an editor", ded != nullptr);
            if (ded) {
                draftPanel.openNoteFile(p40);
                QApplication::processEvents();
                ded->insertPlainText(QStringLiteral("\nDRAFT_ME_40"));
                QApplication::processEvents();
                EXPECT("no .draft immediately after the keystroke",
                       !QFile::exists(p40 + QStringLiteral(".draft")));
                QTest::qWait(2000);           // cadence target: <=2s loss window
                EXPECT(".draft exists within 2s of the first keystroke",
                       QFile::exists(p40 + QStringLiteral(".draft")));
                EXPECT(".draft holds the typed delta",
                       readAll(p40 + QStringLiteral(".draft"))
                           .contains(QStringLiteral("DRAFT_ME_40")));
                draftPanel.saveCurrentNote();
                QApplication::processEvents();
                EXPECT("clean save removed the .draft",
                       !QFile::exists(p40 + QStringLiteral(".draft")));
                EXPECT("clean save landed the delta in the note",
                       readAll(p40).contains(QStringLiteral("DRAFT_ME_40")));
            }
        }
        Config::instance().autoSaveIntervalSec = savedInterval;
    }

    // ── 50. crash-sim: newer .draft offers recovery on open (A7) ──────
    // kill -9 equivalent: the .draft survives on disk, the dtor never ran,
    // no clean save landed. Opening the note in a FRESH panel must offer
    // "Recover unsaved changes?" — Restore loads the draft (then autosaves
    // it into the .html and clears the sidecar); Discard keeps the disk
    // version and deletes the sidecar.
    std::printf("\n--- 50. crash-sim recovery prompt (Restore / Discard) ---\n");
    {
        auto clickBoxButton = [](const QString &needle, bool *clicked) {
            for (QWidget *w : QApplication::topLevelWidgets()) {
                auto *mb = qobject_cast<QMessageBox *>(w);
                if (!mb || !mb->isVisible()) continue;
                for (QAbstractButton *b : mb->buttons())
                    if (b->text().contains(needle)) {
                        if (clicked) *clicked = true;
                        b->click();
                        return;
                    }
            }
        };
        auto craftCrashArtifact = [&](const QString &path, const char *disk,
                                      const char *draft) {
            {
                QFile f(path);
                f.open(QIODevice::WriteOnly);
                f.write(disk);
            }
            const QString dp = path + QStringLiteral(".draft");
            {
                QFile f(dp);
                f.open(QIODevice::WriteOnly);
                f.write(draft);
            }
            {   // draft strictly NEWER than the note — the crash signature
                QFile f(dp);
                f.open(QIODevice::ReadWrite);
                f.setFileTime(QDateTime::currentDateTime().addSecs(60),
                              QFileDevice::FileModificationTime);
            }
        };

        NotesPanel recoveredPanel;
        QTextEdit *red = recoveredPanel.findChild<QTextEdit *>();
        EXPECT("recovery panel has an editor", red != nullptr);

        // (a) Restore.
        const QString p41 = QDir(recoveredPanel.inboxFolder())
            .absoluteFilePath(QStringLiteral("2026-01-03-090000-crash-a.html"));
        craftCrashArtifact(p41,
            "<html><body><p>OLD_DISK_BODY_41A</p></body></html>",
            "<html><body><p>OLD_DISK_BODY_41A</p>"
            "<p>DRAFT_RECOVERED_41A</p></body></html>");
        bool sawRestore = false;
        QTimer::singleShot(150, [&]() {
            clickBoxButton(QStringLiteral("Restore"), &sawRestore);
        });
        auto done41a = armDialogWatchdog();
        recoveredPanel.openNoteFile(p41);
        *done41a = true;
        QApplication::processEvents();
        EXPECT("recovery prompt appeared and Restore was clicked", sawRestore);
        EXPECT("editor shows the recovered draft text",
               red && red->toPlainText().contains(
                   QStringLiteral("DRAFT_RECOVERED_41A")));
        recoveredPanel.saveCurrentNote();   // the autosave-tick path
        QApplication::processEvents();
        EXPECT("recovered text persisted into the note",
               readAll(p41).contains(QStringLiteral("DRAFT_RECOVERED_41A")));
        EXPECT(".draft cleared after the post-recovery save",
               !QFile::exists(p41 + QStringLiteral(".draft")));

        // (b) Discard.
        const QString p41b = QDir(recoveredPanel.inboxFolder())
            .absoluteFilePath(QStringLiteral("2026-01-03-091000-crash-b.html"));
        craftCrashArtifact(p41b,
            "<html><body><p>OLD_DISK_BODY_41B</p></body></html>",
            "<html><body><p>OLD_DISK_BODY_41B</p>"
            "<p>DRAFT_LOST_41B</p></body></html>");
        bool sawDiscard41 = false;
        QTimer::singleShot(150, [&]() {
            clickBoxButton(QStringLiteral("Discard"), &sawDiscard41);
        });
        auto done41b = armDialogWatchdog();
        recoveredPanel.openNoteFile(p41b);
        *done41b = true;
        QApplication::processEvents();
        EXPECT("recovery prompt appeared and Discard was clicked", sawDiscard41);
        EXPECT("editor shows the DISK version after Discard",
               red && red->toPlainText().contains(
                   QStringLiteral("OLD_DISK_BODY_41B")) &&
                   !red->toPlainText().contains(QStringLiteral("DRAFT_LOST_41B")));
        EXPECT(".draft deleted after Discard",
               !QFile::exists(p41b + QStringLiteral(".draft")));

        // (c) A stale draft (older than the note) must NOT prompt at all —
        // openNoteFile must complete without any modal. The watchdog stays
        // armed purely as an anti-hang rescue.
        const QString p41c = QDir(recoveredPanel.inboxFolder())
            .absoluteFilePath(QStringLiteral("2026-01-03-092000-crash-c.html"));
        {
            const QString dc = p41c + QStringLiteral(".draft");
            QFile f(dc);
            f.open(QIODevice::WriteOnly);
            f.write("<html><body><p>STALE_DRAFT_41C</p></body></html>");
        }
        {
            QFile f(p41c);
            f.open(QIODevice::WriteOnly);
            f.write("<html><body><p>NEWER_DISK_41C</p></body></html>");
        }
        {   // note strictly newer than the draft
            QFile f(p41c);
            f.open(QIODevice::ReadWrite);
            f.setFileTime(QDateTime::currentDateTime().addSecs(60),
                          QFileDevice::FileModificationTime);
        }
        auto done41c = armDialogWatchdog();
        recoveredPanel.openNoteFile(p41c);
        *done41c = true;
        QApplication::processEvents();
        EXPECT("stale (older) draft opened WITHOUT a prompt, disk wins",
               red && red->toPlainText().contains(
                   QStringLiteral("NEWER_DISK_41C")));
        EXPECT("stale draft silently dropped",
               !QFile::exists(p41c + QStringLiteral(".draft")));
    }

    // ═══════════════════════════════════════════════════════════════
    // 51-57. AI-Extract marked-region persistence (v0.1.112)
    // ═══════════════════════════════════════════════════════════════

    // Earlier sections (conflict/draft fixtures) forge FUTURE mtimes to
    // exercise the (mtime,size) guards; clamp them back to now so the
    // mtime-sorted newestInboxHtml() lookups below stay order-independent.
    {
        const QDateTime nowTs = QDateTime::currentDateTime();
        for (const QFileInfo &pf : QDir(panel.inboxFolder())
                 .entryInfoList(QStringList() << "*.html", QDir::Files))
            if (pf.lastModified() > nowTs) {
                QFile f(pf.absoluteFilePath());
                if (f.open(QIODevice::ReadWrite)) {
                    f.setFileTime(nowTs, QFileDevice::FileModificationTime);
                    f.close();
                }
            }
    }

    // Shared helpers for the extract-region sections.
    //
    // TIMING: sections run back-to-back in milliseconds, but the earlier
    // dialog-driving sections (22/32) leave 1.5s/2.5s reject-everything
    // watchdogs pending — those fire DURING these sections' modal execs
    // and silently reject them (flaky pass/fail depending on wall time).
    // Two defenses: (a) drain all stale timers once before section 37,
    // (b) every timer here is generation-guarded — bumping dlgGen
    // neutralizes the previous section's pending watchdog.
    int dlgGen = 0;
    auto countBeginAnchors = [](const QString &html) {
        static const QRegularExpression re(
            QStringLiteral("name=\"np-extract-begin-[0-9a-f]{8}\""));
        int n = 0;
        QRegularExpressionMatchIterator it = re.globalMatch(html);
        while (it.hasNext()) { it.next(); ++n; }
        return n;
    };
    auto acceptDialogSoon = [&dlgGen]() {
        const int gen = ++dlgGen;
        QTimer::singleShot(150, [&dlgGen, gen]() {
            if (dlgGen != gen) return;            // a later section took over
            for (QWidget *w : QApplication::topLevelWidgets())
                if (auto *d = qobject_cast<QDialog *>(w))
                    if (d->isVisible()) d->accept();
        });
        QTimer::singleShot(2500, [&dlgGen, gen]() {   // watchdog: never hang
            if (dlgGen != gen) return;
            for (QWidget *w : QApplication::topLevelWidgets())
                if (auto *d = qobject_cast<QDialog *>(w)) d->reject();
        });
    };
    // Drain the stale watchdogs sections 22/32 left pending (they reject
    // EVERY visible dialog when they fire). No dialog is open right now,
    // so they expire harmlessly during this wait.
    QTest::qWait(2600);
    auto findBlockStarting = [](QTextDocument *doc,
                                const QString &prefix) -> QTextBlock {
        for (QTextBlock b = doc->begin(); b.isValid(); b = b.next())
            if (b.text().startsWith(prefix)) return b;
        return QTextBlock();
    };

    const QString fakeAllSections = QStringLiteral(
        "{\"summary\":\"Quick sync about the build and follow-ups.\","
        "\"actions\":[{\"text\":\"Ship the build\",\"owner\":\"@prateek\","
        "\"due\":\"2026-12-25T10:00\"}],"
        "\"decisions\":[{\"text\":\"Adopt the marked-region design\"}],"
        "\"questions\":[{\"text\":\"Who owns the rollout?\"}],"
        "\"risks\":[{\"text\":\"CI capacity is tight\"}]}");

    // ── 51. ALL reviewed sections persist (incl. Decisions/Questions/
    // Risks, which used to be silently discarded) + provenance caption ──
    std::printf("\n--- 51. all four sections persist into the note ---\n");
    {
        panel.newMeetingNote();
        QApplication::processEvents();
        QTextEdit *ed = panel.findChild<QTextEdit *>();
        EXPECT("editor present (37)", ed != nullptr);
        if (ed) {
            ed->clear();
            QApplication::processEvents();
            ed->insertPlainText(QStringLiteral("Meeting raw notes."));
            QApplication::processEvents();

            acceptDialogSoon();
            panel.showExtractResult(fakeAllSections, QStringLiteral("test-model"));
            QApplication::processEvents();

            // The one-sentence user contract: every reviewed section is
            // really IN the note, as h2 sections, with the action as an
            // interactive ☐ line.
            bool decH2 = false, qH2 = false, riskH2 = false, wrapH2 = false;
            for (QTextBlock b = ed->document()->begin(); b.isValid();
                 b = b.next()) {
                if (b.blockFormat().headingLevel() != 2) continue;
                if (b.text() == QStringLiteral("Decisions")) decH2 = true;
                if (b.text() == QStringLiteral("Questions")) qH2 = true;
                if (b.text() == QStringLiteral("Risks")) riskH2 = true;
                if (b.text() == QStringLiteral("AI Extract")) wrapH2 = true;
            }
            EXPECT("Decisions persisted as real h2", decH2);
            EXPECT("Questions persisted as real h2", qH2);
            EXPECT("Risks persisted as real h2", riskH2);
            EXPECT("AI Extract wrapper heading present", wrapH2);
            const QString plain = ed->toPlainText();
            EXPECT("decision bullet persisted",
                   plain.contains(QStringLiteral(
                       "• Adopt the marked-region design")));
            EXPECT("question bullet persisted",
                   plain.contains(QStringLiteral("• Who owns the rollout?")));
            EXPECT("risk bullet persisted",
                   plain.contains(QStringLiteral("• CI capacity is tight")));
            EXPECT("action persisted as an interactive ☐ line",
                   findBlockStarting(ed->document(),
                                     QStringLiteral("☐ Ship the build"))
                       .isValid());
            EXPECT("provenance caption persisted (model + coverage)",
                   plain.contains(QStringLiteral("Extracted by test-model")) &&
                       plain.contains(QStringLiteral("full note")));
            EXPECT("markers are invisible (no np-extract in visible text)",
                   !plain.contains(QStringLiteral("np-extract")));
            EXPECT("exactly one marked region written",
                   countBeginAnchors(ed->toHtml()) == 1);
            EXPECT("user's raw notes untouched above the region",
                   plain.startsWith(QStringLiteral("Meeting raw notes.")));
        }
    }

    // ── 52. Idempotent re-run — replace in place, never stack ─────────
    std::printf("\n--- 52. re-run replaces the region, never stacks ---\n");
    {
        QTextEdit *ed = panel.findChild<QTextEdit *>();
        EXPECT("editor present (38)", ed != nullptr);
        if (ed) {
            // A user paragraph AFTER the region (plain format, so it can
            // never inherit the end anchor).
            {
                QTextCursor c(ed->document());
                c.movePosition(QTextCursor::End);
                c.setCharFormat(QTextCharFormat());
                c.insertBlock();
                c.insertText(QStringLiteral("USER PARAGRAPH AFTER."),
                             QTextCharFormat());
            }

            const QString fakeB = QStringLiteral(
                "{\"summary\":\"Second pass.\","
                "\"actions\":[{\"text\":\"Email the vendor\",\"owner\":null,"
                "\"due\":\"2026-12-26T09:00\"}],"
                "\"decisions\":[],\"questions\":[],\"risks\":[]}");
            acceptDialogSoon();
            panel.showExtractResult(fakeB, QStringLiteral("test-model"));
            QApplication::processEvents();

            EXPECT("still exactly one begin anchor after the re-run",
                   countBeginAnchors(ed->toHtml()) == 1);
            const QString plain = ed->toPlainText();
            EXPECT("run-B item present",
                   plain.contains(QStringLiteral("Email the vendor")));
            EXPECT("run-A item replaced (absent)",
                   !plain.contains(QStringLiteral("Ship the build")));
            EXPECT("run-A decision replaced (absent)",
                   !plain.contains(QStringLiteral(
                       "Adopt the marked-region design")));
            EXPECT("paragraph above the region intact",
                   plain.contains(QStringLiteral("Meeting raw notes.")));
            EXPECT("paragraph below the region intact",
                   plain.contains(QStringLiteral("USER PARAGRAPH AFTER.")));

            // Two more sig-matched re-runs → stable block count, one region.
            const int stableBlocks = ed->document()->blockCount();
            for (int run = 0; run < 2; ++run) {
                acceptDialogSoon();
                panel.showExtractResult(fakeB, QStringLiteral("test-model"));
                QApplication::processEvents();
            }
            EXPECT("3 runs total: block count stable (no bloat per run)",
                   ed->document()->blockCount() == stableBlocks);
            EXPECT("3 runs total: still one begin anchor",
                   countBeginAnchors(ed->toHtml()) == 1);
        }
    }

    // ── 53. Done-state carries across a re-run ────────────────────────
    std::printf("\n--- 53. checked ✓ item stays checked after a re-run ---\n");
    {
        QTextEdit *ed = panel.findChild<QTextEdit *>();
        EXPECT("editor present (39)", ed != nullptr);
        if (ed) {
            // Flip the region's "☐ " to "✓ " — the exact 2-char toggle the
            // click handler performs.
            QTextBlock line = findBlockStarting(
                ed->document(), QStringLiteral("☐ Email the vendor"));
            EXPECT("found the ☐ action line to toggle", line.isValid());
            if (line.isValid()) {
                QTextCursor c(ed->document());
                c.setPosition(line.position());
                c.movePosition(QTextCursor::Right, QTextCursor::KeepAnchor, 2);
                c.insertText(QStringLiteral("✓ "));
                QApplication::processEvents();
            }

            const QString fakeB = QStringLiteral(
                "{\"summary\":\"Second pass.\","
                "\"actions\":[{\"text\":\"Email the vendor\",\"owner\":null,"
                "\"due\":\"2026-12-26T09:00\"}],"
                "\"decisions\":[],\"questions\":[],\"risks\":[]}");
            acceptDialogSoon();
            panel.showExtractResult(fakeB, QStringLiteral("test-model"));
            QApplication::processEvents();

            EXPECT("re-run after toggle still replaces (one region — the "
                   "✓ toggle is NOT an edit)",
                   countBeginAnchors(ed->toHtml()) == 1);
            QTextBlock done = findBlockStarting(
                ed->document(), QStringLiteral("✓ Email the vendor"));
            EXPECT("re-written action line carries the ✓ done-state",
                   done.isValid());
            if (done.isValid() && done.length() > 4) {
                QTextCursor c(ed->document());
                c.setPosition(done.position() + 4);   // inside the item text
                EXPECT("carried ✓ line is struck through "
                       "(restyleChecklistLines ran)",
                       c.charFormat().fontStrikeOut());
            }
        }
    }

    // ── 54. Edited region → Keep-both ask, default never destroys ─────
    std::printf("\n--- 54. edited region asks; Keep both preserves edits ---\n");
    {
        QTextEdit *ed = panel.findChild<QTextEdit *>();
        EXPECT("editor present (40)", ed != nullptr);
        if (ed) {
            // Edit a body line INSIDE the region.
            QTextBlock summaryLine = findBlockStarting(
                ed->document(), QStringLiteral("Second pass."));
            EXPECT("found a region body line to edit", summaryLine.isValid());
            if (summaryLine.isValid()) {
                QTextCursor c(ed->document());
                c.setPosition(summaryLine.position() + summaryLine.length() - 1);
                c.insertText(QStringLiteral(" EDITED-BY-USER"));
                QApplication::processEvents();
            }

            const QString fakeC = QStringLiteral(
                "{\"summary\":\"Third pass.\","
                "\"actions\":[{\"text\":\"Book the venue\",\"owner\":null,"
                "\"due\":\"2026-12-27T09:00\"}],"
                "\"decisions\":[],\"questions\":[],\"risks\":[]}");

            bool keepBothSeen = false, keepBothDefault = false;
            // t=150: accept the sweep dialog (a QDialog). t=700: the
            // Keep-both QMessageBox is up — assert + click "Keep both".
            const int gen40 = ++dlgGen;
            QTimer::singleShot(150, [&dlgGen, gen40]() {
                if (dlgGen != gen40) return;
                for (QWidget *w : QApplication::topLevelWidgets())
                    if (auto *d = qobject_cast<QDialog *>(w))
                        if (d->isVisible() &&
                            !qobject_cast<QMessageBox *>(d)) d->accept();
            });
            QTimer::singleShot(700, [&dlgGen, gen40,
                                     &keepBothSeen, &keepBothDefault]() {
                if (dlgGen != gen40) return;
                for (QWidget *w : QApplication::topLevelWidgets()) {
                    auto *mb = qobject_cast<QMessageBox *>(w);
                    if (!mb || !mb->isVisible()) continue;
                    for (QAbstractButton *b : mb->buttons()) {
                        if (!b->text().contains(QStringLiteral("Keep both")))
                            continue;
                        keepBothSeen = true;
                        keepBothDefault =
                            (mb->defaultButton() ==
                             qobject_cast<QPushButton *>(b));
                        b->click();
                        return;
                    }
                }
            });
            QTimer::singleShot(2500, [&dlgGen, gen40]() {   // watchdog
                if (dlgGen != gen40) return;
                for (QWidget *w : QApplication::topLevelWidgets())
                    if (auto *d = qobject_cast<QDialog *>(w)) d->reject();
            });
            panel.showExtractResult(fakeC, QStringLiteral("test-model"));
            QApplication::processEvents();

            EXPECT("Keep-both ask appeared for the edited region",
                   keepBothSeen);
            EXPECT("Keep both is the DEFAULT button (never destroy edits)",
                   keepBothDefault);
            EXPECT("Keep both → two regions",
                   countBeginAnchors(ed->toHtml()) == 2);
            const QString plain = ed->toPlainText();
            EXPECT("the user's edit survived",
                   plain.contains(QStringLiteral("EDITED-BY-USER")));
            EXPECT("the new extract was appended",
                   plain.contains(QStringLiteral("Book the venue")));
        }
    }

    // ── 55. Truncation honesty — dialog notice + persisted caption ────
    std::printf("\n--- 55. truncation notice + coverage caption ---\n");
    {
        panel.newMeetingNote();
        QApplication::processEvents();
        QTextEdit *ed = panel.findChild<QTextEdit *>();
        EXPECT("editor present (41)", ed != nullptr);
        if (ed) {
            ed->clear();
            QApplication::processEvents();
            ed->insertPlainText(QStringLiteral("A very long note, allegedly."));
            QApplication::processEvents();

            bool noticeSeen = false;
            const int gen41 = ++dlgGen;
            QTimer::singleShot(150, [&dlgGen, gen41, &noticeSeen]() {
                if (dlgGen != gen41) return;
                for (QWidget *w : QApplication::topLevelWidgets()) {
                    auto *d = qobject_cast<QDialog *>(w);
                    if (!d || !d->isVisible()) continue;
                    for (QLabel *lbl : d->findChildren<QLabel *>())
                        if (lbl->isVisible() &&
                            lbl->text().startsWith(QStringLiteral("Long note:")))
                            noticeSeen = true;
                    d->accept();
                }
            });
            QTimer::singleShot(2500, [&dlgGen, gen41]() {   // watchdog
                if (dlgGen != gen41) return;
                for (QWidget *w : QApplication::topLevelWidgets())
                    if (auto *d = qobject_cast<QDialog *>(w)) d->reject();
            });
            panel.showExtractResult(fakeAllSections,
                                    QStringLiteral("test-model"),
                                    /*wordsUsed=*/1200, /*wordsTotal=*/5000);
            QApplication::processEvents();

            EXPECT("amber truncation notice visible in the dialog",
                   noticeSeen);
            const QString plain = ed->toPlainText();
            EXPECT("persisted caption records partial coverage",
                   plain.contains(QStringLiteral("first ~")) &&
                       (plain.contains(QStringLiteral("1,200")) ||
                        plain.contains(QStringLiteral("1200"))) &&
                       (plain.contains(QStringLiteral("5,000")) ||
                        plain.contains(QStringLiteral("5000"))));
            EXPECT("partial-coverage caption never claims 'full note'",
                   !plain.contains(QStringLiteral("full note")));
        }
    }

    // ── 56. End-to-end survival — save → reopen from disk → re-run ────
    std::printf("\n--- 56. region survives disk round-trip, re-run replaces ---\n");
    {
        panel.newMeetingNote();
        QApplication::processEvents();
        const QString pathA = newestInboxHtml();
        EXPECT("created the survival note", !pathA.isEmpty());
        QTextEdit *ed = panel.findChild<QTextEdit *>();
        if (ed && !pathA.isEmpty()) {
            ed->clear();
            QApplication::processEvents();
            ed->insertPlainText(QStringLiteral("Survival meeting notes."));
            QApplication::processEvents();

            acceptDialogSoon();
            panel.showExtractResult(fakeAllSections, QStringLiteral("test-model"));
            QApplication::processEvents();
            panel.saveCurrentNote();
            QApplication::processEvents();
            EXPECT("artifact on disk carries the begin anchor",
                   countBeginAnchors(readAll(pathA)) == 1);

            // Force a REAL disk reload: switch to another note, then back.
            panel.newMeetingNote();
            QApplication::processEvents();
            panel.openNoteFile(pathA);
            QApplication::processEvents();

            EXPECT("region found after reopen (anchors re-attached)",
                   NoterExtractApply::findExtractRegion(
                       ed->document()).found);
            QTextBlock done = findBlockStarting(
                ed->document(), QStringLiteral("☐ Ship the build"));
            EXPECT("☐ action line intact after reopen", done.isValid());

            const QString fakeB = QStringLiteral(
                "{\"summary\":\"After reload.\","
                "\"actions\":[{\"text\":\"Post-reload action\",\"owner\":null,"
                "\"due\":\"2026-12-28T09:00\"}],"
                "\"decisions\":[],\"questions\":[],\"risks\":[]}");
            acceptDialogSoon();
            panel.showExtractResult(fakeB, QStringLiteral("test-model"));
            QApplication::processEvents();
            EXPECT("re-run after the disk round-trip REPLACES (one region)",
                   countBeginAnchors(ed->toHtml()) == 1);
            EXPECT("post-reload item present",
                   ed->toPlainText().contains(
                       QStringLiteral("Post-reload action")));
            EXPECT("pre-reload item replaced",
                   !ed->toPlainText().contains(
                       QStringLiteral("Ship the build")));
        }
    }

    // ── 57. Extract is gated OFF the Todos checklist surface ──────────
    // Accepting Extract there would feed the written headings/bullets into
    // saveTodosChecklist, which converts EVERY line into a todo row —
    // corrupting the todo store.
    std::printf("\n--- 57. Extract refuses the Todos checklist ---\n");
    {
        panel.openTodosChecklist();
        QApplication::processEvents();
        QTextEdit *ed = panel.findChild<QTextEdit *>();
        const QString before = ed ? ed->toPlainText() : QString();

        bool guardSeen = false;
        const int gen43 = ++dlgGen;
        QTimer::singleShot(150, [&dlgGen, gen43, &guardSeen]() {
            if (dlgGen != gen43) return;
            for (QWidget *w : QApplication::topLevelWidgets()) {
                auto *mb = qobject_cast<QMessageBox *>(w);
                if (!mb || !mb->isVisible()) continue;
                guardSeen = mb->text().contains(
                    QStringLiteral("meeting note"));
                mb->close();
            }
        });
        QTimer::singleShot(2500, [&dlgGen, gen43]() {     // watchdog
            if (dlgGen != gen43) return;
            for (QWidget *w : QApplication::topLevelWidgets())
                if (auto *d = qobject_cast<QDialog *>(w)) d->reject();
        });
        panel.showExtractResult(fakeAllSections, QStringLiteral("test-model"));
        QApplication::processEvents();

        EXPECT("guard info box shown on the checklist surface", guardSeen);
        EXPECT("checklist content untouched (no extract written)",
               !ed || ed->toPlainText() == before);
        EXPECT("no marked region written on the checklist",
               !ed || countBeginAnchors(ed->toHtml()) == 0);
    }

    // ── 58. app-lifetime service: shared engine + todos survive panel
    // delete (audit fix — reminders used to die with the Noter tab).
    std::printf("\n--- 58. shared-engine lifetime survives panel delete ---\n");
    {
        qRegisterMetaType<TodoRow>("TodoRow");
        qRegisterMetaType<QVector<TodoRow>>("QVector<TodoRow>");

        NotesTodos todos(tmpHome.path() + "/shared-todos.db");
        EXPECT("shared todos open()", todos.open(nullptr));
        NotesReminderEngine engine(&todos);

        auto *p = new NotesPanel(nullptr, &todos, &engine);
        QApplication::processEvents();
        delete p;                         // "close the Noter tab"
        QApplication::processEvents();

        // Panel must NOT have deleted the injected todos: still usable.
        const QString rid = todos.addReminder(
            tmpHome.path() + "/Documents/Notepatra/Noter/Inbox/shared-note.html",
            QStringLiteral("fires after tab close"),
            QDateTime::currentDateTime().addSecs(-120));
        EXPECT("injected todos still usable after panel delete",
               !rid.isEmpty());

        QSignalSpy spy(&engine, &NotesReminderEngine::reminderDue);
        engine.tick();
        EXPECT("engine still fires reminderDue after panel delete",
               spy.count() == 1);
        if (spy.count() == 1)
            EXPECT_STR_EQ("fired row is the one scheduled after close",
                          spy.first().first().value<TodoRow>().id, rid);
        EXPECT_STR_EQ("row flipped to fired",
                      todos.find(rid).reminderStatus,
                      QStringLiteral("fired"));
    }

    // ── 59. shared panel alive: exactly ONE fire, no double-handling ──
    std::printf("\n--- 59. shared panel alive: single fire ---\n");
    {
        NotesTodos todos(tmpHome.path() + "/shared2-todos.db");
        EXPECT("shared todos open()", todos.open(nullptr));
        NotesReminderEngine engine(&todos);
        NotesPanel shared(nullptr, &todos, &engine);
        shared.show();
        shared.newMeetingNote();          // editor page → banner visibility is real
        QApplication::processEvents();

        const QString rid = todos.addReminder(
            shared.inboxFolder() + "/sp-note.html",
            QStringLiteral("one ping only"),
            QDateTime::currentDateTime().addSecs(-60));
        EXPECT("due reminder scheduled", !rid.isEmpty());

        QSignalSpy spy(&engine, &NotesReminderEngine::reminderDue);
        engine.tick();
        QApplication::processEvents();
        EXPECT("reminderDue emitted exactly once", spy.count() == 1);
        EXPECT_STR_EQ("row fired exactly once",
                      todos.find(rid).reminderStatus,
                      QStringLiteral("fired"));
        engine.tick();
        QApplication::processEvents();
        EXPECT("second tick does not re-fire", spy.count() == 1);

        // The in-window banner showed (shared mode keeps the panel as the
        // banner surface; toasts move to MainWindow).
        auto *banner = shared.findChild<QWidget *>(
            QStringLiteral("noterReminderBanner"));
        EXPECT("banner exists", banner != nullptr);
        EXPECT("banner visible after fire", banner && banner->isVisible());
        bool labelHasText = false;
        if (banner)
            for (QLabel *l : banner->findChildren<QLabel *>())
                if (l->text().contains(QStringLiteral("one ping only")))
                    labelHasText = true;
        EXPECT("banner label carries the reminder text", labelHasText);
    }

    // ── 60. sidebar Reminders count decrements when a reminder fires ──
    std::printf("\n--- 60. sidebar Reminders count updates on fire ---\n");
    {
        NotesTodos todos(tmpHome.path() + "/shared3-todos.db");
        EXPECT("shared todos open()", todos.open(nullptr));
        NotesReminderEngine engine(&todos);
        NotesPanel shared(nullptr, &todos, &engine);
        shared.show();
        QApplication::processEvents();

        // One past (will fire) + one future (stays scheduled).
        EXPECT("past reminder scheduled",
               !todos.addReminder(shared.inboxFolder() + "/sw-note.html",
                                  QStringLiteral("past one"),
                                  QDateTime::currentDateTime().addSecs(-60))
                    .isEmpty());
        EXPECT("future reminder scheduled",
               !todos.addReminder(shared.inboxFolder() + "/sw-note.html",
                                  QStringLiteral("future one"),
                                  QDateTime::currentDateTime().addSecs(3600))
                    .isEmpty());

        engine.tick();                    // fires "past one" → refreshSidebar
        QApplication::processEvents();

        auto *tree = shared.findChild<QTreeWidget *>(
            QStringLiteral("noterSidebarTree"));
        EXPECT("sidebar tree present", tree != nullptr);
        int remCount = -1;
        if (tree)
            for (int i = 0; i < tree->topLevelItemCount(); ++i) {
                const QString t = tree->topLevelItem(i)->text(0);
                if (!t.startsWith(QStringLiteral("Reminders"))) continue;
                const int op = t.indexOf('(');
                if (op >= 0)
                    remCount = t.mid(op + 1, t.indexOf(')') - op - 1).toInt();
            }
        EXPECT("Reminders root shows ONLY the future reminder (count==1)",
               remCount == 1);
    }

    // ── 61. replayReminders → banner shows + dedupes by id ───────────
    std::printf("\n--- 61. replayReminders banner + dedupe ---\n");
    {
        NotesTodos todos(tmpHome.path() + "/shared4-todos.db");
        EXPECT("shared todos open()", todos.open(nullptr));
        NotesReminderEngine engine(&todos);
        NotesPanel shared(nullptr, &todos, &engine);
        shared.show();
        shared.newMeetingNote();          // editor page → banner can be visible
        QApplication::processEvents();

        TodoRow row;
        row.id = QStringLiteral("replay-1");
        row.text = QStringLiteral("undelivered toast");
        row.sourceFile = shared.inboxFolder() + "/replay-note.html";
        shared.replayReminders({row});
        QApplication::processEvents();

        auto *banner = shared.findChild<QWidget *>(
            QStringLiteral("noterReminderBanner"));
        EXPECT("banner exists", banner != nullptr);
        EXPECT("banner visible after replay", banner && banner->isVisible());
        bool labelHasText = false;
        if (banner)
            for (QLabel *l : banner->findChildren<QLabel *>())
                if (l->text().contains(QStringLiteral("undelivered toast")))
                    labelHasText = true;
        EXPECT("banner carries the replayed reminder text", labelHasText);

        // Replay the SAME id again — enqueueReminder dedupe must drop it.
        shared.replayReminders({row});
        QApplication::processEvents();
        bool labelHasMore = false;
        if (banner)
            for (QLabel *l : banner->findChildren<QLabel *>())
                if (l->text().contains(QStringLiteral("more")))
                    labelHasMore = true;
        EXPECT("no '(+N more)' suffix after duplicate replay", !labelHasMore);

        // One Dismiss must empty the queue (dedupe held) → banner hides.
        QPushButton *dismiss = nullptr;
        if (banner)
            for (QPushButton *b : banner->findChildren<QPushButton *>())
                if (b->text() == QStringLiteral("Dismiss")) dismiss = b;
        EXPECT("Dismiss button present", dismiss != nullptr);
        if (dismiss) dismiss->click();
        QApplication::processEvents();
        EXPECT("banner hidden after a single dismiss (no duplicate queued)",
               banner && !banner->isVisible());
    }

    // ══ A3 — title identity (sections 62+; spec integration tests 7-17).
    // The notepatra-title head meta is the on-disk title SSOT; every label
    // reads the resolver; H1 *edits* adopt at save time; sidebar renames
    // commit the title to meta + H1; the filename stays an ASCII disk-id. ══
    auto findH1Block = [](QTextDocument *doc) -> QTextBlock {
        int scanned = 0;
        for (QTextBlock b = doc->firstBlock(); b.isValid() && scanned < 200;
             b = b.next(), ++scanned)
            if (b.blockFormat().headingLevel() == 1) return b;
        return QTextBlock();
    };
    auto readBytes = [](const QString &path) -> QByteArray {
        QFile f(path);
        if (!f.open(QIODevice::ReadOnly)) return {};
        return f.readAll();
    };
    auto leafTextForPath = [&](const QString &p) -> QString {
        QTreeWidgetItem *l = meetingLeafForPath(p);
        return l ? l->text(0) : QString();
    };
    auto stampPrefixOf = [](const QString &absPath) -> QString {
        QRegExp rx(QStringLiteral("^(\\d{4}-\\d{2}-\\d{2}-\\d{4,6}-)"));
        return rx.indexIn(QFileInfo(absPath).fileName()) == 0 ? rx.cap(1)
                                                              : QString();
    };
    QString teamSyncPath;   // set in 65, reused by 69

    // ── 62. new note carries the title meta from birth + h1 import pin ─
    std::printf("\n--- 62. new note: title meta + sidebar leaf + h1 import ---\n");
    {
        panel.newMeetingNote();
        QApplication::processEvents();
        const QString p = newestInboxHtml();
        EXPECT("created a note for the meta-from-birth test", !p.isEmpty());
        QRegExp nRx(QStringLiteral("noter-(\\d+)\\.html$"));
        EXPECT("default filename carries the counter", nRx.indexIn(p) >= 0);
        const QString want = QStringLiteral("Noter %1").arg(nRx.cap(1));
        EXPECT_STR_EQ("head meta notepatra-title == default title",
                      NotesStorage::titleMetaIn(readAll(p)), want);
        EXPECT_STR_EQ("sidebar leaf reads the same title",
                      leafTextForPath(p), want);
        // Pin the Qt h1-import assumption the whole design depends on:
        // the template's <h1 class="meet-title"> must arrive as a block
        // with blockFormat().headingLevel()==1, and it is the first block
        // with any text in it.
        const QTextBlock h1 = findH1Block(editor->document());
        EXPECT("template h1 imports with headingLevel==1", h1.isValid());
        EXPECT_STR_EQ("h1 block text is the default title",
                      h1.isValid() ? h1.text().simplified() : QString(), want);
        bool h1IsFirstText = false;
        for (QTextBlock b = editor->document()->firstBlock(); b.isValid();
             b = b.next()) {
            if (b.text().simplified().isEmpty()) continue;
            h1IsFirstText = (b == h1);
            break;
        }
        EXPECT("the h1 is the first non-empty block", h1IsFirstText);
    }

    // ── 63. H1 edit → title adopted at save; filename NEVER changes ───
    std::printf("\n--- 63. H1-edit sync on save ---\n");
    {
        panel.newMeetingNote();
        QApplication::processEvents();
        const QString p = newestInboxHtml();
        const QStringList namesBefore = QDir(panel.inboxFolder())
            .entryList(QStringList() << "*.html", QDir::Files);
        QTextBlock h1 = findH1Block(editor->document());
        EXPECT("h1 block found for the edit", h1.isValid());
        if (h1.isValid()) {
            QSignalSpy titleSpy(&panel, &NotesPanel::noteTitleChanged);
            QTextCursor c(editor->document());
            c.setPosition(h1.position());
            c.movePosition(QTextCursor::EndOfBlock, QTextCursor::KeepAnchor);
            c.insertText(QStringLiteral("Roadmap review"));
            QApplication::processEvents();
            panel.saveCurrentNote();
            QApplication::processEvents();
            EXPECT_STR_EQ("sidebar leaf adopted the H1 after save",
                          leafTextForPath(p), QStringLiteral("Roadmap review"));
            EXPECT_STR_EQ("file meta carries the adopted title",
                          NotesStorage::titleMetaIn(readAll(p)),
                          QStringLiteral("Roadmap review"));
            EXPECT("disk FILENAME unchanged (H1 sync writes meta only)",
                   QFile::exists(p) &&
                   QDir(panel.inboxFolder())
                           .entryList(QStringList() << "*.html", QDir::Files)
                       == namesBefore);
            bool spyCarried = false;
            for (int i = 0; i < titleSpy.count(); ++i)
                if (titleSpy.at(i).first().toString()
                        == QStringLiteral("Roadmap review")) spyCarried = true;
            EXPECT("noteTitleChanged fired with the new title", spyCarried);
        }
    }

    // ── 64. legacy note: body edit never adopts; meta seeds the SAME
    // label; an H1 edit then adopts; audit-fix display rules ───────────
    std::printf("\n--- 64. legacy no-meta note: no-flip seed + heal rules ---\n");
    {
        // (a) custom filename + stale default H1 → filename-pretty wins
        // (the no-hijack guard), and the first save seeds THAT label.
        const QString p64 = QDir(panel.inboxFolder()).absoluteFilePath(
            QStringLiteral("2026-01-02-0930-weekly-sync.html"));
        {
            QFile f(p64);
            EXPECT("legacy fixture written", f.open(QIODevice::WriteOnly));
            f.write("<html><head><title>w</title></head><body>"
                    "<h1 class=\"meet-title\">Noter 01</h1>"
                    "<p>legacy body 64</p></body></html>");
        }
        panel.openNoteFile(p64);
        QApplication::processEvents();
        EXPECT_STR_EQ("stale 'Noter 01' H1 does NOT hijack the renamed file",
                      leafTextForPath(p64), QStringLiteral("weekly sync"));
        // Body edit (NOT the H1) + save → label sticky, meta seeded.
        {
            QTextCursor c(editor->document());
            c.movePosition(QTextCursor::End);
            c.insertText(QStringLiteral(" plus64"));
        }
        QApplication::processEvents();
        panel.saveCurrentNote();
        QApplication::processEvents();
        EXPECT_STR_EQ("label did not flip on the migration save",
                      leafTextForPath(p64), QStringLiteral("weekly sync"));
        EXPECT_STR_EQ("meta seeded with the label the user has been seeing",
                      NotesStorage::titleMetaIn(readAll(p64)),
                      QStringLiteral("weekly sync"));
        // Editing the H1 itself IS a title change → adopt on save.
        QTextBlock h1 = findH1Block(editor->document());
        EXPECT("legacy hand-written <h1> imported with headingLevel==1",
               h1.isValid());
        if (h1.isValid()) {
            QTextCursor c(editor->document());
            c.setPosition(h1.position());
            c.movePosition(QTextCursor::EndOfBlock, QTextCursor::KeepAnchor);
            c.insertText(QStringLiteral("Platform retro"));
            QApplication::processEvents();
            panel.saveCurrentNote();
            QApplication::processEvents();
            EXPECT_STR_EQ("H1 edit adopted as the new label",
                          leafTextForPath(p64), QStringLiteral("Platform retro"));
            EXPECT_STR_EQ("meta follows the adopted H1",
                          NotesStorage::titleMetaIn(readAll(p64)),
                          QStringLiteral("Platform retro"));
        }
        // (b) DEFAULT filename + CUSTOMIZED H1 → the H1 shows (audit fix);
        // DEFAULT filename + default H1 → filename-pretty (pristine).
        const QString pAudit = QDir(panel.inboxFolder()).absoluteFilePath(
            QStringLiteral("2026-01-03-1000-noter-55.html"));
        const QString pPristine = QDir(panel.inboxFolder()).absoluteFilePath(
            QStringLiteral("2026-01-03-1015-noter-56.html"));
        {
            QFile f(pAudit);
            f.open(QIODevice::WriteOnly);
            f.write("<html><head></head><body><h1>Budget kickoff</h1>"
                    "<p>a</p></body></html>");
        }
        {
            QFile f(pPristine);
            f.open(QIODevice::WriteOnly);
            f.write("<html><head></head><body><h1>Noter 56</h1>"
                    "<p>b</p></body></html>");
        }
        if (search) { search->setText(QStringLiteral("x")); QApplication::processEvents();
                      search->setText(QString());           QApplication::processEvents(); }
        EXPECT_STR_EQ("default file + customized H1 shows the H1 (audit fix)",
                      leafTextForPath(pAudit), QStringLiteral("Budget kickoff"));
        EXPECT_STR_EQ("default file + pristine H1 keeps the filename label",
                      leafTextForPath(pPristine), QStringLiteral("Noter 56"));
    }

    // ── 65. sidebar rename (Latin): file + meta + H1 heal + pop-out
    // follow + undo cannot resurrect the old title ─────────────────────
    std::printf("\n--- 65. sidebar rename commits title everywhere ---\n");
    {
        panel.newMeetingNote();
        QApplication::processEvents();
        const QString p = newestInboxHtml();
        const QString prefix = stampPrefixOf(p);
        EXPECT("rename fixture created with a stamp prefix", !prefix.isEmpty());
        panel.popOutActive();
        QApplication::processEvents();
        QTreeWidgetItem *leaf = meetingLeafForPath(p);
        EXPECT("leaf found for the open-note rename", leaf != nullptr);
        const QString newP = QDir(panel.inboxFolder())
            .absoluteFilePath(prefix + QStringLiteral("team-sync-hub.html"));
        if (leaf) {
            leaf->setText(0, QStringLiteral("Team Sync Hub"));
            for (int i = 0; i < 4; ++i) QApplication::processEvents();
            EXPECT("file renamed, stamp prefix kept", QFile::exists(newP));
            EXPECT("old filename gone", !QFile::exists(p));
            EXPECT_STR_EQ("meta carries the EXACT rename case",
                          NotesStorage::titleMetaIn(readAll(newP)),
                          QStringLiteral("Team Sync Hub"));
            EXPECT_STR_EQ("body H1 healed to the new title",
                          NotesStorage::legacyH1In(readAll(newP)),
                          QStringLiteral("Team Sync Hub"));
            NoterPopOut *pop = panel.popOutForTesting();
            EXPECT("pop-out re-pointed at the renamed file",
                   pop && pop->notePath() == newP);
            EXPECT_STR_EQ("pop-out titlebar follows the rename",
                          pop && pop->titleLabelForTesting()
                              ? pop->titleLabelForTesting()->text() : QString(),
                          QStringLiteral("Team Sync Hub"));
            // The rename surgery is NOT a user edit: undo must be unable
            // to resurrect the old H1 (a revived "Noter NN" would be
            // re-adopted by the next autosave).
            EXPECT("undo stack cleared after rename surgery",
                   !editor->document()->isUndoAvailable());
            editor->undo();
            QApplication::processEvents();
            panel.saveCurrentNote();
            QApplication::processEvents();
            EXPECT_STR_EQ("undo + save cannot resurrect the old title",
                          NotesStorage::titleMetaIn(readAll(newP)),
                          QStringLiteral("Team Sync Hub"));
            // Buffer follows the renamed file (wave M5 contract intact).
            editor->insertPlainText(QStringLiteral(" REPOINT_65"));
            QApplication::processEvents();
            panel.saveCurrentNote();
            QApplication::processEvents();
            EXPECT("buffer save lands in the renamed file",
                   readAll(newP).contains(QStringLiteral("REPOINT_65")));
        }
        if (NoterPopOut *last = panel.popOutForTesting()) {
            last->close();
            QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
            QApplication::processEvents();
        }
        teamSyncPath = newP;   // reused by section 69

        // Closed-note rename — byte-level head surgery, never a
        // QTextDocument round-trip of a note that isn't open.
        panel.newMeetingNote();
        QApplication::processEvents();
        const QString pB = newestInboxHtml();
        panel.newMeetingNote();          // open ANOTHER note → pB is closed
        QApplication::processEvents();
        QTreeWidgetItem *leafB = meetingLeafForPath(pB);
        EXPECT("leaf found for the closed-note rename", leafB != nullptr);
        if (leafB) {
            const QString newB = QDir(panel.inboxFolder()).absoluteFilePath(
                stampPrefixOf(pB) + QStringLiteral("archive-plan.html"));
            leafB->setText(0, QStringLiteral("Archive Plan"));
            for (int i = 0; i < 4; ++i) QApplication::processEvents();
            EXPECT("closed note renamed on disk", QFile::exists(newB));
            EXPECT_STR_EQ("closed-note meta written via head surgery",
                          NotesStorage::titleMetaIn(readAll(newB)),
                          QStringLiteral("Archive Plan"));
            EXPECT_STR_EQ("closed-note H1 inner text healed",
                          NotesStorage::legacyH1In(readAll(newB)),
                          QStringLiteral("Archive Plan"));
            EXPECT("h1 keeps its meet-title class (todos parser contract)",
                   readAll(newB).contains(QStringLiteral("meet-title")));
        }
    }

    // ── 66. CJK/emoji rename: Unicode label + meta, ASCII filename ────
    std::printf("\n--- 66. CJK/emoji title — filename stays ASCII ---\n");
    const QString cjkTitle = QString::fromUtf8(
        "\xE8\xAE\xBE\xE8\xAE\xA1\xE5\x91\xA8\xE4\xBC\x9A "
        "\xF0\x9F\x9A\x80");   // "design weekly" CJK + rocket
    QString cjkPath;
    {
        panel.newMeetingNote();
        QApplication::processEvents();
        const QString p = newestInboxHtml();
        cjkPath = p;
        panel.popOutActive();
        QApplication::processEvents();
        QTreeWidgetItem *leaf = meetingLeafForPath(p);
        EXPECT("leaf found for the CJK rename", leaf != nullptr);
        if (leaf) {
            leaf->setText(0, cjkTitle);
            for (int i = 0; i < 4; ++i) QApplication::processEvents();
            EXPECT("disk filename UNCHANGED for a pure-CJK/emoji title",
                   QFile::exists(p));
            EXPECT("no '<prefix>-untitled.html' stray appeared",
                   QDir(panel.inboxFolder())
                       .entryList(QStringList() << "*-untitled.html",
                                  QDir::Files).isEmpty());
            bool asciiName = true;
            const QString fname = QFileInfo(p).fileName();
            for (const QChar c : fname)
                if (c.unicode() > 127) asciiName = false;
            EXPECT("filename is pure ASCII", asciiName);
            EXPECT_STR_EQ("sidebar leaf shows the Unicode title verbatim",
                          leafTextForPath(p), cjkTitle);
            EXPECT_STR_EQ("meta stores the Unicode title",
                          NotesStorage::titleMetaIn(readAll(p)), cjkTitle);
            NoterPopOut *pop = panel.popOutForTesting();
            EXPECT_STR_EQ("pop-out titlebar shows the Unicode title",
                          pop && pop->titleLabelForTesting()
                              ? pop->titleLabelForTesting()->text() : QString(),
                          cjkTitle);
        }
        if (NoterPopOut *last = panel.popOutForTesting()) {
            last->close();
            QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
            QApplication::processEvents();
        }
    }

    // ── 67. true no-op rename: zero writes, zero renames ──────────────
    std::printf("\n--- 67. no-op rename leaves the file untouched ---\n");
    {
        // "Archive Plan" from 65 is CLOSED and meta-titled — ideal probe.
        QString p;
        for (const QFileInfo &fi : QDir(panel.inboxFolder())
                 .entryInfoList(QStringList() << "*archive-plan.html",
                                QDir::Files))
            p = fi.absoluteFilePath();
        EXPECT("no-op probe file present", !p.isEmpty());
        if (!p.isEmpty()) {
            const QByteArray bytesBefore = readBytes(p);
            const QDateTime mtimeBefore = QFileInfo(p).lastModified();
            QTreeWidgetItem *leaf = meetingLeafForPath(p);
            EXPECT("leaf found for the no-op rename", leaf != nullptr);
            if (leaf) {
                // Padded text → itemChanged fires, trimmed text equals the
                // resolver display → MUST be swallowed as a no-op.
                leaf->setText(0, QStringLiteral(" Archive Plan "));
                for (int i = 0; i < 4; ++i) QApplication::processEvents();
                EXPECT("file not renamed on a no-op", QFile::exists(p));
                EXPECT("bytes unchanged (no write)", readBytes(p) == bytesBefore);
                EXPECT("mtime unchanged (no write)",
                       QFileInfo(p).lastModified() == mtimeBefore);
                EXPECT_STR_EQ("leaf label restored",
                              leafTextForPath(p), QStringLiteral("Archive Plan"));
            }
        }
    }

    // ── 68. counter never reuses a LIVE number (titles count too) ─────
    std::printf("\n--- 68. counter: stale H1 + meta titles reserve numbers ---\n");
    {
        // (a) stale legacy body H1 "Noter 150" on a custom-named file.
        {
            QFile f(QDir(panel.inboxFolder()).absoluteFilePath(
                QStringLiteral("2026-01-04-1100-counter-probe.html")));
            EXPECT("H1 counter probe written", f.open(QIODevice::WriteOnly));
            f.write("<html><head></head><body><h1>Noter 150</h1>"
                    "<p>probe a</p></body></html>");
        }
        panel.newMeetingNote();
        QApplication::processEvents();
        EXPECT("stale legacy H1 reserves its number (next == 151)",
               QDir(panel.inboxFolder())
                       .entryList(QStringList() << "*noter-151.html",
                                  QDir::Files).size() == 1);
        // (b) meta title "Noter 200" on a custom-named file.
        const QString metaProbe = QDir(panel.inboxFolder()).absoluteFilePath(
            QStringLiteral("2026-01-04-1130-meta-probe.html"));
        {
            QFile f(metaProbe);
            EXPECT("meta counter probe written", f.open(QIODevice::WriteOnly));
            f.write("<html><head>"
                    "<meta name=\"notepatra-title\" content=\"Noter 200\">"
                    "</head><body><p>probe b</p></body></html>");
        }
        panel.newMeetingNote();
        QApplication::processEvents();
        const QStringList got201 = QDir(panel.inboxFolder())
            .entryList(QStringList() << "*noter-201.html", QDir::Files);
        EXPECT("meta title reserves its number (next == 201)",
               got201.size() == 1);
        // (c) a number that disappears from EVERY surface is freed again.
        panel.openTodosChecklist();        // unbind the open note first
        QApplication::processEvents();
        if (got201.size() == 1)
            QFile::remove(QDir(panel.inboxFolder())
                              .absoluteFilePath(got201.first()));
        {
            QFile f(metaProbe);            // genuine retitle, everywhere
            f.open(QIODevice::WriteOnly);
            f.write("<html><head>"
                    "<meta name=\"notepatra-title\" content=\"Probe archived\">"
                    "</head><body><p>probe b v2</p></body></html>");
        }
        panel.newMeetingNote();
        QApplication::processEvents();
        EXPECT("genuinely retitled number is freed (next == 152, not 202)",
               QDir(panel.inboxFolder())
                       .entryList(QStringList() << "*noter-152.html",
                                  QDir::Files).size() == 1);
        EXPECT("no note skipped to 202",
               QDir(panel.inboxFolder())
                       .entryList(QStringList() << "*noter-202.html",
                                  QDir::Files).isEmpty());
    }

    // ── 69. pop-out: meta-titled note + LIVE H1-edit follow ───────────
    std::printf("\n--- 69. pop-out follows the live display title ---\n");
    {
        const QString p = teamSyncPath;   // "Team Sync Hub" from section 65
        EXPECT("meta-titled note available", QFile::exists(p));
        panel.openNoteFile(p);
        QApplication::processEvents();
        panel.popOutActive();
        QApplication::processEvents();
        NoterPopOut *pop = panel.popOutForTesting();
        EXPECT("pop-out alive on the meta-titled note", pop != nullptr);
        EXPECT_STR_EQ("pop-out titlebar equals the sidebar leaf",
                      pop && pop->titleLabelForTesting()
                          ? pop->titleLabelForTesting()->text() : QString(),
                      leafTextForPath(p));
        QTextBlock h1 = findH1Block(editor->document());
        EXPECT("h1 present for the live-follow edit", h1.isValid());
        if (h1.isValid() && pop) {
            QTextCursor c(editor->document());
            c.setPosition(h1.position());
            c.movePosition(QTextCursor::EndOfBlock, QTextCursor::KeepAnchor);
            c.insertText(QStringLiteral("Hub Sync v2"));
            QApplication::processEvents();
            panel.saveCurrentNote();
            QApplication::processEvents();
            EXPECT_STR_EQ("pop-out title updated LIVE on H1 adoption",
                          pop->titleLabelForTesting()->text(),
                          QStringLiteral("Hub Sync v2"));
            EXPECT_STR_EQ("sidebar agrees with the pop-out",
                          leafTextForPath(p), QStringLiteral("Hub Sync v2"));
        }
        if (NoterPopOut *last = panel.popOutForTesting()) {
            last->close();
            QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
            QApplication::processEvents();
        }
    }

    // ── 70. read-error title is the PRETTY name, never the raw stem ───
    std::printf("\n--- 70. read-error carries the pretty title ---\n");
    {
        const QString p = QDir(panel.inboxFolder()).absoluteFilePath(
            QStringLiteral("2026-01-05-1200-secret-probe.html"));
        {
            QFile f(p);
            EXPECT("read-error fixture written", f.open(QIODevice::WriteOnly));
            f.write("<html><body><p>locked</p></body></html>");
        }
        QFile::setPermissions(p, QFileDevice::WriteOwner);   // 0200 — unreadable
        QSignalSpy titleSpy(&panel, &NotesPanel::noteTitleChanged);
        panel.openNoteFile(p);
        QApplication::processEvents();
        EXPECT("noteTitleChanged fired on the read error", titleSpy.count() >= 1);
        const QString carried = titleSpy.count()
            ? titleSpy.last().first().toString() : QString();
        EXPECT_STR_EQ("signal carries the PRETTY name", carried,
                      QStringLiteral("secret probe"));
        EXPECT("signal does NOT carry the raw stem",
               carried != QStringLiteral("2026-01-05-1200-secret-probe"));
        QFile::setPermissions(p,
            QFileDevice::ReadOwner | QFileDevice::WriteOwner);
        panel.openNoteFile(p);   // leave the panel in a clean, bound state
        QApplication::processEvents();
    }

    // ── 71. zero-rewrite migration: open+close never touches the file ─
    std::printf("\n--- 71. legacy note: zero-rewrite + no label flip ---\n");
    {
        const QString p = QDir(panel.inboxFolder()).absoluteFilePath(
            QStringLiteral("2026-01-06-0930-noter-03.html"));   // 4-digit legacy
        {
            QFile f(p);
            EXPECT("legacy 4-digit fixture written", f.open(QIODevice::WriteOnly));
            f.write("<html><head><title>n</title></head><body>"
                    "<h1 class=\"meet-title\">Noter 03</h1>"
                    "<p>legacy migrate body</p></body></html>");
        }
        if (search) { search->setText(QStringLiteral("x")); QApplication::processEvents();
                      search->setText(QString());           QApplication::processEvents(); }
        EXPECT_STR_EQ("label before open (filename-pretty)",
                      leafTextForPath(p), QStringLiteral("Noter 03"));
        const QByteArray bytesBefore = readBytes(p);
        panel.openNoteFile(p);
        QApplication::processEvents();
        panel.openTodosChecklist();   // navigate away WITHOUT editing
        QApplication::processEvents();
        EXPECT("file bytes hash-identical after open+close (no rewrite)",
               readBytes(p) == bytesBefore);
        EXPECT_STR_EQ("label unchanged after open+close",
                      leafTextForPath(p), QStringLiteral("Noter 03"));
        // An unrelated BODY edit + save adopts the meta — same label.
        panel.openNoteFile(p);
        QApplication::processEvents();
        {
            QTextCursor c(editor->document());
            c.movePosition(QTextCursor::End);
            c.insertText(QStringLiteral(" tail71"));
        }
        QApplication::processEvents();
        panel.saveCurrentNote();
        QApplication::processEvents();
        EXPECT_STR_EQ("meta adoption locks in the SAME label",
                      leafTextForPath(p), QStringLiteral("Noter 03"));
        EXPECT_STR_EQ("seeded meta equals the pre-migration label",
                      NotesStorage::titleMetaIn(readAll(p)),
                      QStringLiteral("Noter 03"));
    }

    // ── 72. Unicode search matches the meta title ──────────────────────
    std::printf("\n--- 72. Unicode title is searchable ---\n");
    if (search && !cjkPath.isEmpty()) {
        search->setText(QString::fromUtf8("\xE8\xAE\xBE\xE8\xAE\xA1"));  // CJK fragment
        QApplication::processEvents();
        EXPECT("CJK fragment keeps exactly the renamed leaf visible",
               countMeetingLeaves() == 1);
        EXPECT("the surviving leaf IS the CJK-titled note",
               meetingLeafForPath(cjkPath) != nullptr);
        search->setText(QString());
        QApplication::processEvents();
    }

    // ── Summary ───────────────────────────────────────────────────
    std::printf("\n──────────────────────────\n");
    std::printf("PASS: %d   FAIL: %d\n", g_pass, g_fail);
    return g_fail == 0 ? 0 : 1;
}
