// SPDX-License-Identifier: GPL-3.0-or-later
//
// NotesTodos — SQLite cache + reminder data layer for the Notes feature.
//
// HTML files on disk are AUTHORITATIVE. This SQLite database is a
// rebuildable cache: any time the cache is lost / corrupted /
// schema-migrated, rebuildFromHtmlFiles() walks the notes root and
// repopulates it. The cache exists for fast read-side queries (Today,
// This Week, Overdue, by Owner) and to drive the reminder polling loop.
//
// Threading: one connection per QObject instance, created in open()
// and tied to the thread that called open(). All mutations are expected
// from a single thread (the GUI thread, in NotesPanel). If a worker
// thread ever needs access, open a second NotesTodos there with its own
// connection name.
//
// ISO 8601 strings (UTC, suffixed "Z") are used for all timestamps so
// that text sort = chronological sort and so the SQLite file is human
// inspectable.
//
// v0.1.94+ — initial schema = version 1.

#ifndef NOTES_TODOS_H
#define NOTES_TODOS_H

#include <QObject>
#include <QString>
#include <QStringList>
#include <QVector>
#include <QDateTime>
#include <QMetaType>
#include <QSqlDatabase>

class QSqlQuery;

struct TodoRow {
    QString id;             // UUID v4
    QString sourceFile;     // absolute path to .html note
    QString sourceBlockId;  // data-id within the file
    QString text;
    QString owner;          // "@bob", "@you", etc. or empty
    QDateTime dueAt;        // invalid = no due date
    QString status;         // "open" | "done" | "cancelled"
    QDateTime reminderAt;
    QString reminderStatus; // "none" | "scheduled" | "fired" | "snoozed" | "dismissed"
    QDateTime snoozeUntil;
    QString recurrence;     // RRULE-style string, optional
    QDateTime createdAt;
    QDateTime completedAt;
    QString meetingTitle;   // denormalized for display
    bool    sourceFileMissing = false;
};

// Allow TodoRow / QVector<TodoRow> to travel through QVariant + queued
// signals (the reminder engine emits both as signal payloads).
Q_DECLARE_METATYPE(TodoRow)
Q_DECLARE_METATYPE(QVector<TodoRow>)

class NotesTodos : public QObject {
    Q_OBJECT
public:
    explicit NotesTodos(const QString &dbPath, QObject *parent = nullptr);
    ~NotesTodos() override;

    // Open or create the SQLite database at dbPath; run migrations.
    // Returns false on any SQLite failure; errorOut (if non-null) gets
    // the QSqlError text. Idempotent — calling twice is harmless.
    bool open(QString *errorOut = nullptr);

    // Re-scan the given .html file's action blocks and update SQLite rows.
    //
    // Action blocks are recognised by:
    //   <div class="b b-act" data-id="UUID"
    //        data-owner="@bob" data-due="2026-05-23T17:00"
    //        data-status="open">…body text…</div>
    //
    // Operation:
    //   1) Parse meeting title from <h1 class="meet-title"> in the same
    //      file (falls back to <title>...</title> if absent).
    //   2) Walk every block-action div in the file.
    //   3) Upsert each into the todos table (keyed on id = data-id).
    //   4) Mark rows whose data-id no longer appears in the file as
    //      source_file_missing=1 (status preserved; UI flags them).
    void reindexNote(const QString &absolutePath, const QString &html);

    // Remove all rows for a deleted note file.
    void purgeNote(const QString &absolutePath);

    // Read-side queries — used by the Todos slide-over UI.
    QVector<TodoRow> dueGroupOverdue(const QDateTime &now) const;  // dueAt < now AND status="open"
    QVector<TodoRow> dueGroupToday(const QDateTime &now) const;    // today (00:00 - 23:59) AND open
    QVector<TodoRow> dueGroupWeek(const QDateTime &now) const;     // next 7 days
    QVector<TodoRow> dueGroupSomeday(const QDateTime &now) const;  // no dueAt or > 30d out
    QVector<TodoRow> dueGroupDone(int limit) const;                // last N done items
    // v0.1.95+ — Trash group. Returns rows where status='trashed',
    // ordered by created_at DESC. Limit caps the panel render cost.
    QVector<TodoRow> dueGroupTrashed(int limit) const;

    // For the reminder engine.
    //   remindersReadyAt(t): reminder_at <= t AND reminder_status="scheduled"
    //                        AND status="open" (i.e. not done/cancelled).
    //   remindersMissedSince(lastTick): same condition, but for catch-up
    //                        after the app was closed across the trigger
    //                        time. Returns rows whose reminder_at is
    //                        BETWEEN lastTick AND now.
    QVector<TodoRow> remindersReadyAt(const QDateTime &t) const;
    QVector<TodoRow> remindersMissedSince(const QDateTime &lastTick) const;

    // Find by id. Returns a default-constructed TodoRow (empty id) on miss.
    TodoRow find(const QString &id) const;

    // Mutations — write to SQLite. Callers MUST also update the source
    // HTML file (NotesPanel is the integrator). These return false on
    // SQLite error.
    bool markDone(const QString &id, const QDateTime &when);
    bool markOpen(const QString &id);
    bool setDue(const QString &id, const QDateTime &dueAt);
    bool setReminder(const QString &id, const QDateTime &remindAt);
    bool snooze(const QString &id, const QDateTime &until);
    bool dismissReminder(const QString &id);

    // v0.1.94 — update a todo's text in place. Used by the editable Todos
    // panel (double-click row → QLineEdit → Enter commits). Rejects empty
    // text. The integrator (NotesPanel) is also responsible for replacing
    // the matching line in the editor body when the todo originated from
    // a meeting note, so the next reindexNote() doesn't clobber the edit.
    bool setText(const QString &id, const QString &text);

    // v0.1.95+ — drop a row from the SQLite cache. For QUICK todos
    // (Inbox/quick-todos.html, no source meeting beyond Inbox itself)
    // this fully removes them. For meeting-sourced todos a subsequent
    // reindexNote() of the source file will re-create the row — the
    // caller must therefore also rewrite / delete the source line in
    // the meeting HTML to make the deletion stick.
    bool deleteRow(const QString &id);

    // v0.1.95+ — soft-delete: status='trashed'. The row stays in the
    // SQLite cache; queries other than dueGroupTrashed filter it out.
    // Restore via restoreRow(id) → status='open'. The user-facing
    // "Delete" menu item calls trashRow first; the "Delete permanently"
    // item in the Trash group calls deleteRow.
    bool trashRow(const QString &id);
    bool restoreRow(const QString &id);

    // Helper for the reminder engine: mark a reminder as fired so we
    // don't re-fire it on the next 60s tick. Distinct from
    // dismissReminder (which is a user-driven "stop bugging me about
    // this one") even though both result in a non-"scheduled" state.
    bool markReminderFired(const QString &id);

    // Full rebuild from HTML files on disk — recovery path if the
    // SQLite file gets corrupted or schema drifts.
    bool rebuildFromHtmlFiles(const QString &notesRoot, QString *errorOut = nullptr);

    // Add a standalone "quick todo" that isn't from any meeting.
    // Writes to a special <notesRoot>/Inbox/quick-todos.html AND
    // creates the SQLite row. Returns the new row's id, or empty on
    // failure.
    QString addQuickTodo(const QString &text, const QString &owner,
                         const QDateTime &dueAt);

    // Debug / introspection.
    QString connectionName() const { return m_connName; }
    QString databasePath() const { return m_dbPath; }

signals:
    void todoChanged(const QString &id);

private:
    bool ensureOpen() const;
    bool createSchema(QString *errorOut);
    bool migrateSchema(int fromVersion, QString *errorOut);
    int  currentSchemaVersion() const;

    // Parse action blocks out of an HTML string. Hand-rolled walker —
    // regex matches the opening div tag, body is taken up to the
    // closest "</div>" allowing for nested span/code tags.
    struct ParsedAction {
        QString id;
        QString owner;
        QString due;       // raw "2026-05-23T17:00" string
        QString status;    // raw "open" / "done" / "cancelled"
        QString text;      // body text, HTML-stripped
    };
    static QVector<ParsedAction> parseActionBlocks(const QString &html);
    static QString               parseMeetingTitle(const QString &html);
    static QString               stripHtml(const QString &fragment);
    static QDateTime             parseIsoFlex(const QString &s);
    static QString               toIso(const QDateTime &dt);
    static QDateTime             fromIso(const QString &s);

    // Reads a single row from a primed query (cursor on the matching
    // record). Lets find() and the group queries share column parsing.
    static TodoRow rowFromQuery(QSqlQuery &q);

    QSqlDatabase m_db;
    QString m_dbPath;
    QString m_connName;
};

#endif // NOTES_TODOS_H
