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

#include <QApplication>
#include <QDir>
#include <QFile>
#include <QKeyEvent>
#include <QLineEdit>
#include <QListWidget>
#include <QPushButton>
#include <QSplitter>
#include <QStackedWidget>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QTest>
#include <QTextCursor>
#include <QTextEdit>

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
    EXPECT("has meeting QListWidget",
           panel.findChild<QListWidget *>() != nullptr);
    QList<QPushButton *> buttons = panel.findChildren<QPushButton *>();
    EXPECT("has at least 3 QPushButtons (new / todos / extract)",
           buttons.size() >= 3);

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
        // Press F4 again to toggle back
        QTest::keyClick(editor, Qt::Key_F4);
        QApplication::processEvents();
        const QString tail2 = editor->toPlainText().section('\n', -1, -1);
        EXPECT("F4 again turned ✓ back to ☐",
               tail2.startsWith(QStringLiteral("☐ review PR")));
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

    // ── Summary ───────────────────────────────────────────────────
    std::printf("\n──────────────────────────\n");
    std::printf("PASS: %d   FAIL: %d\n", g_pass, g_fail);
    return g_fail == 0 ? 0 : 1;
}
