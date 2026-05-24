// SPDX-License-Identifier: GPL-3.0-or-later
//
// Unit tests for the three Noter slide-over panels.
//
//   1. NoterTodosPanel — seed with 4 lists of NoterTodoRow, assert each
//      group's row count + signal emission.
//   2. NoterNotebooksPanel — point at a QTemporaryDir with 3 subfolders
//      + 5 *.html files; assert the tree model exposes the right rows.
//
// Headless: relies on QT_QPA_PLATFORM=offscreen (set in CTest props).

#include "notes_panels.h"

#include <QApplication>
#include <QCheckBox>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileSystemModel>
#include <QLabel>
#include <QLineEdit>
#include <QMouseEvent>
#include <QSet>
#include <QSignalSpy>
#include <QStackedWidget>
#include <QTemporaryDir>
#include <QTimer>
#include <QTreeView>

#include <cstdio>
#include <cstdlib>

static int g_pass = 0, g_fail = 0;
#define EXPECT(label, cond) \
    do { if (cond) { ++g_pass; std::printf("  [PASS] %s\n", label); } \
         else      { ++g_fail; std::printf("  [FAIL] %s\n", label); } } while (0)
#define EXPECT_STR_EQ(label, got, want) \
    do { const QString _g = (got); const QString _w = (want); \
         if (_g == _w) { ++g_pass; std::printf("  [PASS] %s\n", label); } \
         else { ++g_fail; std::printf("  [FAIL] %s — got %s, want %s\n", \
                          label, qPrintable(_g), qPrintable(_w)); } } while (0)
#define REQUIRE(cond, msg) do { \
        if (!(cond)) { std::fprintf(stderr, "FATAL: %s\n", msg); \
                       std::exit(2); } } while (0)

// ─── helper — build a NoterTodoRow with sane defaults ────────────────
static NoterTodoRow mkTodo(const QString &id, const QString &text,
                           const QString &owner = QString(),
                           const QString &meeting = QString(),
                           bool done = false,
                           const QDateTime &due = QDateTime()) {
    NoterTodoRow r;
    r.todoId = id;
    r.text = text;
    r.owner = owner;
    r.meeting = meeting;
    r.sourceFile = QStringLiteral("/tmp/fake/%1.html").arg(id);
    r.blockId = QStringLiteral("anchor-") + id;
    r.due = due;
    r.done = done;
    return r;
}

static void testTodosPanel() {
    std::printf("[test_notes_panels] NoterTodosPanel\n");

    NoterTodosPanel panel;
    panel.setTodos(
        {   // overdue — 2
            mkTodo("o1", "Renew SSL cert", "@alice", "Infra sync", false,
                   QDateTime::currentDateTime().addDays(-3)),
            mkTodo("o2", "Reply to vendor", "@bob",  "Vendor call"),
        },
        {   // today — 3
            mkTodo("t1", "Ship release notes"),
            mkTodo("t2", "Verify CI green", "@carol"),
            mkTodo("t3", "Sync with PM", "@dave"),
        },
        {   // week — 1
            mkTodo("w1", "Draft v0.2 plan"),
        },
        {   // someday — 4
            mkTodo("s1", "Refactor lexer"),
            mkTodo("s2", "Eval new font"),
            mkTodo("s3", "Try wayland"),
            mkTodo("s4", "Better icons"),
        },
        {   // done — 2
            mkTodo("d1", "Tag v0.1.93", QString(), QString(), true),
            mkTodo("d2", "Update README", QString(), QString(), true),
        }
    );

    EXPECT("overdue count = 2", panel.rowCountOverdue() == 2);
    EXPECT("today count = 3",   panel.rowCountToday()   == 3);
    EXPECT("week count = 1",    panel.rowCountWeek()    == 1);
    EXPECT("someday count = 4", panel.rowCountSomeday() == 4);
    EXPECT("done count = 2",    panel.rowCountDone()    == 2);

    // The total chip in the header counts active rows only (not done).
    // We can't easily read the chip text here without reaching into
    // children, but the counts above are the contract.

    // ── Signal: ticking a checkbox emits todoMarkDone(id) ───────────
    QSignalSpy spy(&panel, &NoterTodosPanel::todoMarkDone);
    // Find any todo row checkbox and toggle it on.
    QList<QCheckBox*> checks = panel.findChildren<QCheckBox*>("todoCheck");
    EXPECT("has at least one todoCheck", !checks.isEmpty());
    if (!checks.isEmpty()) {
        // First checkbox in render order corresponds to overdue/o1.
        checks.first()->setChecked(true);
        QApplication::processEvents();
        EXPECT("todoMarkDone fired once",
               spy.count() == 1);
        if (spy.count() == 1) {
            const QString id = spy.first().at(0).toString();
            // The very first checkbox should be in the Overdue group,
            // which is o1.
            EXPECT("todoMarkDone(\"o1\")", id == QStringLiteral("o1"));
        }
    }

    // ── Search filter ───────────────────────────────────────────────
    panel.searchForTesting()->setText("ssl");
    QApplication::processEvents();
    // After filtering, only the "Renew SSL cert" row should remain
    // visible. The internal counts don't change (they reflect data,
    // not visible rows), but we can verify visibility manually.
    // Use !isHidden() because the panel itself isn't shown in the
    // offscreen test — isVisible() walks ancestry and would return
    // false for everyone.
    int visibleRows = 0;
    for (QWidget *w : panel.findChildren<QWidget *>(QStringLiteral("todoRow")))
        if (!w->isHidden()) ++visibleRows;
    EXPECT("search 'ssl' leaves exactly 1 visible row",
           visibleRows == 1);

    panel.searchForTesting()->clear();
    QApplication::processEvents();

    // ── v0.1.94: inline-editable title ──────────────────────────────
    // Double-click on a row → QLineEdit appears → commit emits
    // todoTextEdited(id, newText). Esc cancels without emit.
    QSignalSpy editSpy(&panel, &NoterTodosPanel::todoTextEdited);

    // Grab the first todoRow + its stack + line edit.
    QList<QWidget*> rows = panel.findChildren<QWidget*>(QStringLiteral("todoRow"));
    EXPECT("has at least one todoRow for edit test", !rows.isEmpty());
    if (!rows.isEmpty()) {
        QWidget *row0 = rows.first();
        auto *stack = row0->findChild<QStackedWidget*>(
            QStringLiteral("todoTitleStack"));
        auto *titleLabel = row0->findChild<QLabel*>(QStringLiteral("todoTitle"));
        auto *titleEdit  = row0->findChild<QLineEdit*>(
            QStringLiteral("todoTitleEdit"));
        EXPECT("row has titleStack", stack != nullptr);
        EXPECT("row has titleLabel", titleLabel != nullptr);
        EXPECT("row has titleEdit",  titleEdit != nullptr);

        if (stack && titleEdit) {
            EXPECT("title starts on label page (index 0)",
                   stack->currentIndex() == 0);

            // Simulate the double-click handler via direct event.
            QMouseEvent dbl(QEvent::MouseButtonDblClick,
                            QPointF(5, 5), QPointF(5, 5), QPointF(5, 5),
                            Qt::LeftButton, Qt::LeftButton,
                            Qt::NoModifier);
            QApplication::sendEvent(row0, &dbl);
            QApplication::processEvents();
            EXPECT("after double-click swaps to edit page (index 1)",
                   stack->currentIndex() == 1);

            // Type a new value + commit via editingFinished.
            titleEdit->setText(QStringLiteral("renewed cert by Friday"));
            // editingFinished fires on focus-out OR Return. emit directly
            // for deterministic test.
            emit titleEdit->editingFinished();
            QApplication::processEvents();

            EXPECT("after commit swaps back to label (index 0)",
                   stack->currentIndex() == 0);
            EXPECT("todoTextEdited fired exactly once",
                   editSpy.count() == 1);
            if (editSpy.count() == 1) {
                EXPECT_STR_EQ("first arg is the row id",
                              editSpy.first().at(0).toString(),
                              QStringLiteral("o1"));
                EXPECT_STR_EQ("second arg is the new text",
                              editSpy.first().at(1).toString(),
                              QStringLiteral("renewed cert by Friday"));
            }
            EXPECT_STR_EQ("label text is updated post-commit",
                          titleLabel->text(),
                          QStringLiteral("renewed cert by Friday"));

            // Empty / whitespace-only commit must NOT emit.
            editSpy.clear();
            QApplication::sendEvent(row0, &dbl);
            QApplication::processEvents();
            titleEdit->setText(QStringLiteral("   "));
            emit titleEdit->editingFinished();
            QApplication::processEvents();
            EXPECT("empty commit suppressed", editSpy.count() == 0);
            EXPECT_STR_EQ("label text unchanged after empty commit",
                          titleLabel->text(),
                          QStringLiteral("renewed cert by Friday"));

            // Unchanged value must NOT emit (no diff).
            editSpy.clear();
            QApplication::sendEvent(row0, &dbl);
            QApplication::processEvents();
            // titleEdit was just shown with the SAME text as label —
            // commit without changing it.
            emit titleEdit->editingFinished();
            QApplication::processEvents();
            EXPECT("no-op commit suppressed", editSpy.count() == 0);
        }
    }
}

static void testNotebooksPanel() {
    std::printf("[test_notes_panels] NoterNotebooksPanel\n");

    QTemporaryDir tmp;
    REQUIRE(tmp.isValid(),
            tmp.errorString().toUtf8().constData());

    // Build:
    //   tmp/
    //     a/  → 2 .html
    //     b/  → 2 .html
    //     c/  → 1 .html
    //     ignored.txt
    QDir root(tmp.path());
    const QStringList subs{"a", "b", "c"};
    for (const QString &s : subs) root.mkpath(s);

    auto touch = [&](const QString &rel) {
        QFile f(root.filePath(rel));
        f.open(QIODevice::WriteOnly);
        f.write("<html><body>hi</body></html>");
        f.close();
    };
    touch("a/n1.html");
    touch("a/n2.html");
    touch("b/n3.html");
    touch("b/n4.html");
    touch("c/n5.html");
    touch("ignored.txt");

    NoterNotebooksPanel panel;
    panel.setRoot(tmp.path());
    QApplication::processEvents();

    QFileSystemModel *fs = panel.findChild<QFileSystemModel*>();
    EXPECT("has a QFileSystemModel", fs != nullptr);
    if (!fs) return;

    // QFileSystemModel populates async + lazily — children of a
    // directory aren't fetched until something asks for them. We call
    // expandAll() on the tree to force the proxy to pull every
    // subdirectory's contents, then spin the event loop until the
    // model has reported directoryLoaded for everything we expect.
    QStringList expectedDirs{
        tmp.path(),
        root.filePath("a"),
        root.filePath("b"),
        root.filePath("c"),
    };
    QSet<QString> loadedDirs;
    QObject::connect(fs, &QFileSystemModel::directoryLoaded,
                     [&](const QString &p) { loadedDirs.insert(p); });

    QTreeView *tree = panel.treeForTesting();
    tree->expandAll();
    for (int i = 0; i < 50; ++i) {
        QEventLoop loop;
        QTimer::singleShot(100, &loop, &QEventLoop::quit);
        loop.exec();
        tree->expandAll();  // re-expand as new rows appear
        bool allLoaded = true;
        for (const QString &d : expectedDirs)
            if (!loadedDirs.contains(d)) { allLoaded = false; break; }
        if (allLoaded) break;
    }

    EXPECT("tree exists", tree != nullptr);
    if (tree) {
        QAbstractItemModel *m = tree->model();
        const QModelIndex rootIdx = tree->rootIndex();
        EXPECT("root index valid", rootIdx.isValid());

        // Walk the tree counting *.html displayed names.
        std::function<int(const QModelIndex &)> count =
            [&](const QModelIndex &p) -> int {
                int n = 0;
                m->fetchMore(p);
                const int rows = m->rowCount(p);
                for (int r = 0; r < rows; ++r) {
                    const QModelIndex c = m->index(r, 0, p);
                    if (!c.isValid()) continue;
                    const QString name =
                        c.data(Qt::DisplayRole).toString();
                    if (name.endsWith(QLatin1String(".html"),
                                      Qt::CaseInsensitive)) ++n;
                    n += count(c);
                }
                return n;
            };
        const int html = count(rootIdx);
        EXPECT("5 .html notes under root", html == 5);

        std::function<int(const QModelIndex &)> txtCount =
            [&](const QModelIndex &p) -> int {
                int n = 0;
                m->fetchMore(p);
                const int rows = m->rowCount(p);
                for (int r = 0; r < rows; ++r) {
                    const QModelIndex c = m->index(r, 0, p);
                    if (!c.isValid()) continue;
                    const QString name =
                        c.data(Qt::DisplayRole).toString();
                    if (name.endsWith(QLatin1String(".txt"),
                                      Qt::CaseInsensitive)) ++n;
                    n += txtCount(c);
                }
                return n;
            };
        EXPECT("ignored.txt not visible", txtCount(rootIdx) == 0);
    }
}

static void testRemindersPanel() {
    std::printf("[test_notes_panels] NoterRemindersPanel\n");

    NoterRemindersPanel panel;
    QList<NoterReminderRow> rows;
    auto mk = [&](const QString &id, const QString &text,
                  const QString &state, const QDateTime &when) {
        NoterReminderRow r;
        r.reminderId = id;
        r.text = text;
        r.sourceFile = QStringLiteral("/tmp/x.html");
        r.blockId = id;
        r.state = state;
        r.when = when;
        return r;
    };
    const QDateTime now = QDateTime::currentDateTime();
    rows << mk("r1", "Today fired",      "fired",   now);
    rows << mk("r2", "Today snoozed",    "snoozed", now);
    rows << mk("r3", "Yesterday missed", "missed",  now.addDays(-1));
    rows << mk("r4", "3 days ago",       "fired",   now.addDays(-3));
    rows << mk("r5", "Last month",       "dismissed", now.addDays(-40));

    panel.setReminders(rows);
    EXPECT("today = 2",     panel.rowCountToday()     == 2);
    EXPECT("yesterday = 1", panel.rowCountYesterday() == 1);
    EXPECT("week = 1",      panel.rowCountWeek()      == 1);
    EXPECT("older = 1",     panel.rowCountOlder()     == 1);
}

int main(int argc, char *argv[]) {
    qputenv("QT_QPA_PLATFORM", "offscreen");
    QApplication app(argc, argv);

    testTodosPanel();
    testNotebooksPanel();
    testRemindersPanel();

    std::printf("\n[test_notes_panels] %d passed, %d failed\n",
                g_pass, g_fail);
    return g_fail == 0 ? 0 : 1;
}
