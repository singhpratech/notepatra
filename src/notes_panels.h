// SPDX-License-Identifier: GPL-3.0-or-later
//
// Noter slide-over panels — Notebooks, Todos, Reminders.
//
// Three sibling QFrame subclasses, each ~360 px wide, designed to slide
// over the canvas from the left edge of the Noter tab. They are NOT
// dockable widgets and they do NOT share state — each one is a
// self-contained side panel with its own header / search / scroll body.
//
// Data plumbing strategy:
//   Panels are pure VIEW widgets. They never touch the filesystem-
//   storage or SQLite-todos layer directly. The integrator (NotesPanel
//   in src/notes.cpp) hands them seed data via the public setters
//   (setRoot, setTodos, setReminders). When the underlying store
//   changes, the integrator re-pushes the data — panels do not poll.
//
// This indirection lets the panels compile + unit-test against in-
// memory fixtures with no notes_storage / notes_todos dependency.

#ifndef NOTES_PANELS_H
#define NOTES_PANELS_H

#include <QDateTime>
#include <QFrame>
#include <QList>
#include <QPair>
#include <QString>

class QFileSystemModel;
class QLabel;
class QLineEdit;
class QTreeView;
class QVBoxLayout;
class QScrollArea;
class QWidget;
class QSortFilterProxyModel;
class QFrame;

// ─── data contracts ─────────────────────────────────────────────────
// POD shapes the panels read; integrator fills these from notes_todos.
// Kept dependency-free so the panels build + test in isolation.

struct NoterTodoRow {
    QString todoId;        // unique id; opaque to the panel
    QString text;          // user-visible title
    QString owner;         // "@alice" or "" if unowned
    QString meeting;       // source meeting / note title (no path)
    QString sourceFile;    // absolute path to the .html note
    QString blockId;       // anchor inside the note (for jump-to)
    QDateTime due;         // QDateTime() if no due date
    bool    done = false;
    // v0.1.95+ — soft-deleted rows. When trashed=true, the row was
    // explicitly moved to the Trash group via right-click → Delete.
    // Context-menu actions on these rows offer Restore or Delete
    // permanently instead of the normal Set due / Set reminder set.
    bool    trashed = false;
};

struct NoterReminderRow {
    QString reminderId;
    QString text;
    QString sourceFile;
    QString blockId;
    QDateTime when;        // when the reminder fired / is due
    // State strings the panel renders as colored dots:
    //   "fired"     — decision-teal
    //   "snoozed"   — action-amber
    //   "pending"   — question-indigo
    //   "missed"    — risk-rose
    //   "dismissed" — neutral
    QString state;
};

// ═══════════════════════════════════════════════════════════════════════
// NoterNotebooksPanel  (Ctrl+,)
// ═══════════════════════════════════════════════════════════════════════
class NoterNotebooksPanel : public QFrame {
    Q_OBJECT
public:
    explicit NoterNotebooksPanel(QWidget *parent = nullptr);
    ~NoterNotebooksPanel() override;

    // Root directory shown at the top of the tree. Typically
    // ~/Documents/Notepatra/Noter/ — but tests pass a QTemporaryDir.
    void setRoot(const QString &absoluteRootDir);
    QString root() const;

    // Focus the search input — wired to the panel-open keyboard shortcut.
    void focusSearch();

    // Test hooks.
    QTreeView *treeForTesting() const { return m_tree; }
    QLineEdit *searchForTesting() const { return m_search; }

signals:
    void closeRequested();
    void noteOpenRequested(const QString &absolutePath);

private:
    void buildUi();
    void rebuildModel();
    void onSearchTextChanged(const QString &text);
    void onActivated(const QModelIndex &index);

    QString m_root;

    QLabel    *m_title = nullptr;
    QLabel    *m_count = nullptr;
    QLineEdit *m_search = nullptr;
    QTreeView *m_tree = nullptr;
    QFileSystemModel *m_fs = nullptr;
    QSortFilterProxyModel *m_proxy = nullptr;
};

// ═══════════════════════════════════════════════════════════════════════
// NoterTodosPanel  (Ctrl+Alt+T)
// ═══════════════════════════════════════════════════════════════════════
class NoterTodosPanel : public QFrame {
    Q_OBJECT
public:
    explicit NoterTodosPanel(QWidget *parent = nullptr);
    ~NoterTodosPanel() override;

    // Integrator pushes the groups via this single setter. overdue /
    // today / week / someday are NOT-done rows; done is the collapsed-
    // by-default "Done" section; trashed is the soft-delete bucket
    // shown only when non-empty.
    void setTodos(const QList<NoterTodoRow> &overdue,
                  const QList<NoterTodoRow> &today,
                  const QList<NoterTodoRow> &week,
                  const QList<NoterTodoRow> &someday,
                  const QList<NoterTodoRow> &done,
                  const QList<NoterTodoRow> &trashed = QList<NoterTodoRow>());

    void focusSearch();

    // Test hooks — read-only inspection of the rendered counts.
    int rowCountOverdue() const { return m_overdueCount; }
    int rowCountToday()   const { return m_todayCount; }
    int rowCountWeek()    const { return m_weekCount; }
    int rowCountSomeday() const { return m_somedayCount; }
    int rowCountDone()    const { return m_doneCount; }
    QLineEdit *searchForTesting() const { return m_search; }

signals:
    void closeRequested();
    void todoActivated(const QString &sourceFile, const QString &blockId);
    void todoMarkDone(const QString &todoId);
    // v0.1.94 — emitted when the user double-clicks a row's title and
    // commits a new value. Integrator (NotesPanel) is responsible for
    // calling NotesTodos::setText() AND replacing the matching line in
    // the source note's editor body so the SQLite + HTML stay in sync.
    void todoTextEdited(const QString &todoId, const QString &newText);
    // v0.1.95 — "+ Add" button in header → user typed text in QInputDialog
    // and accepted. Integrator calls NotesTodos::addQuickTodo + refreshes.
    void addTodoRequested(const QString &text);
    // v0.1.95+ — extended add path: user picked text + optional due +
    // optional reminder via the AddTodoDialog. Either QDateTime may be
    // invalid (= "not set"). Integrator calls addQuickTodo + setDue +
    // setReminder accordingly.
    void addTodoWithDateRequested(const QString &text,
                                  const QDateTime &due,
                                  const QDateTime &remind);
    // Right-click row actions.
    void setDueRequested(const QString &todoId, const QDateTime &due);
    void setReminderRequested(const QString &todoId, const QDateTime &remind);
    void markDoneRequested(const QString &todoId);
    // v0.1.95+ "Delete" on a non-trashed row moves it to Trash; the
    // separate "Delete permanently" in the Trash group calls deleteRow.
    void trashRequested(const QString &todoId);
    void restoreRequested(const QString &todoId);
    void deleteRequested(const QString &todoId);   // permanent — Trash group only

private:
    void buildUi();
    void clearGroups();
    void appendGroup(const QString &title, const QString &accentColor,
                     const QList<NoterTodoRow> &rows, bool collapsedDefault);
    void onSearchTextChanged(const QString &text);

    QLabel    *m_title = nullptr;
    QLabel    *m_count = nullptr;
    QLineEdit *m_search = nullptr;
    QScrollArea *m_scroll = nullptr;
    QWidget   *m_body = nullptr;
    QVBoxLayout *m_bodyLayout = nullptr;

    int m_overdueCount = 0;
    int m_todayCount = 0;
    int m_weekCount = 0;
    int m_somedayCount = 0;
    int m_doneCount = 0;

    // Parallel data list — search-filter walks this instead of using
    // findChildren so we don't need a Q_OBJECT on the row widget.
    // Pair is (row-frame, data); we toggle visibility on the frame.
    QList<QPair<QFrame *, NoterTodoRow>> m_visibleRows;
};

// ═══════════════════════════════════════════════════════════════════════
// NoterRemindersPanel  (Ctrl+Alt+R)
// ═══════════════════════════════════════════════════════════════════════
class NoterRemindersPanel : public QFrame {
    Q_OBJECT
public:
    explicit NoterRemindersPanel(QWidget *parent = nullptr);
    ~NoterRemindersPanel() override;

    // Integrator pushes the full reminder log. Panel buckets rows into
    // Today / Yesterday / Earlier this week / Older internally.
    void setReminders(const QList<NoterReminderRow> &rows);

    void focusSearch();

    // Test hooks.
    int rowCountToday() const     { return m_todayCount; }
    int rowCountYesterday() const { return m_yesterdayCount; }
    int rowCountWeek() const      { return m_weekCount; }
    int rowCountOlder() const     { return m_olderCount; }

signals:
    void closeRequested();
    void reminderActivated(const QString &sourceFile, const QString &blockId);

private:
    void buildUi();
    void clearGroups();
    void appendGroup(const QString &title,
                     const QList<NoterReminderRow> &rows);
    void onSearchTextChanged(const QString &text);

    QLabel    *m_title = nullptr;
    QLabel    *m_count = nullptr;
    QLineEdit *m_search = nullptr;
    QScrollArea *m_scroll = nullptr;
    QWidget   *m_body = nullptr;
    QVBoxLayout *m_bodyLayout = nullptr;

    int m_todayCount = 0;
    int m_yesterdayCount = 0;
    int m_weekCount = 0;
    int m_olderCount = 0;

    QList<QPair<QFrame *, NoterReminderRow>> m_visibleRows;
};

#endif // NOTES_PANELS_H
