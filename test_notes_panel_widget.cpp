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
#include "notes_sweep_dialog.h"
#include "notes_sweep_prompt.h"

#include <QAction>
#include <QApplication>
#include <QComboBox>
#include <QCompleter>
#include <QAbstractItemView>
#include <QMenu>
#include <QCalendarWidget>
#include <QDateTimeEdit>
#include <QDialog>
#include <QDir>
#include <QFile>
#include <QTimer>
#include <QKeyEvent>
#include <QCheckBox>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QPushButton>
#include <QSplitter>
#include <QStackedWidget>
#include <QToolButton>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QTest>
#include <QTextBlock>
#include <QTextCursor>
#include <QTextDocument>
#include <QTextEdit>
#include <QTreeWidget>
#include <QTreeWidgetItem>

#include <cstdio>
#include <cstdlib>

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
        QTimer::singleShot(1500, []() {           // watchdog: never hang
            for (QWidget *w : QApplication::topLevelWidgets())
                if (auto *d = qobject_cast<QDialog *>(w)) d->reject();
        });
        panel.promptReminderForNote(QStringLiteral("/tmp/repro-noter-01.html"),
                                    QStringLiteral("Noter 01"));
        EXPECT("reminder dialog (no existing) opened without crashing", true);
        EXPECT("watchdog saw + dismissed the modal", dismissed);

        // Now with an EXISTING reminder so the "Clear reminder" button path
        // is constructed too.
        QTimer::singleShot(150, [&]() {
            for (QWidget *w : QApplication::topLevelWidgets())
                if (auto *d = qobject_cast<QDialog *>(w))
                    if (d->isVisible()) d->reject();
        });
        QTimer::singleShot(1500, []() {
            for (QWidget *w : QApplication::topLevelWidgets())
                if (auto *d = qobject_cast<QDialog *>(w)) d->reject();
        });
        // Seed a reminder via the same public path (accept would set one, but
        // we just need noteReminderAt to be valid — reuse promptReminder is
        // overkill; instead schedule directly is private, so accept once).
        panel.promptReminderForNote(QStringLiteral("/tmp/repro-noter-01.html"),
                                    QStringLiteral("Noter 01"));
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
            if (tt == QStringLiteral("Insert checkbox"))     hasCheckboxBtn = true;
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
        QTimer::singleShot(1500, []() {           // watchdog: never hang
            for (QWidget *w : QApplication::topLevelWidgets())
                if (auto *d = qobject_cast<QDialog *>(w)) d->reject();
        });
        const QString remNote = panel.inboxFolder() + "/rem-test-note.html";
        panel.promptReminderForNote(remNote, QStringLiteral("Ship the build"));
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
        QTimer::singleShot(1500, []() {           // watchdog: never hang
            for (QWidget *w : QApplication::topLevelWidgets())
                if (auto *d = qobject_cast<QDialog *>(w)) d->reject();
        });
        panel.showExtractResult(fakeResponse, QStringLiteral("test-model"));
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
            if (b->toolTip() == QStringLiteral("Insert checkbox")) chkBtn = b;
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

    // ── Summary ───────────────────────────────────────────────────
    std::printf("\n──────────────────────────\n");
    std::printf("PASS: %d   FAIL: %d\n", g_pass, g_fail);
    return g_fail == 0 ? 0 : 1;
}
