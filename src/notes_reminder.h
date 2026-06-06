// SPDX-License-Identifier: GPL-3.0-or-later
//
// NotesReminderEngine — 60-second polling loop that emits reminderDue
// when an action item's reminder_at has elapsed. The engine itself is
// QtCore-only; the host UI (MainWindow's app-lifetime reminder service,
// or a self-owning NotesPanel in tests) wires reminderDue / missedBatch
// to QSystemTrayIcon::showMessage. Keeping the engine UI-free means we
// can unit-test it under offscreen-Qt without ever creating a real tray
// icon.

#ifndef NOTES_REMINDER_H
#define NOTES_REMINDER_H

#include <QObject>
#include <QPointer>
#include <QTimer>
#include <QDateTime>

#include "notes_todos.h"

class NotesReminderEngine : public QObject {
    Q_OBJECT
public:
    explicit NotesReminderEngine(NotesTodos *todos, QObject *parent = nullptr);

    void start();        // begin 60s poll loop (idempotent)
    void stop();

    // Manual tick — exposed for tests AND for catchUpMissed().
    void tick();

    // On launch, batch-fire EVERY overdue scheduled reminder
    // (remindersReadyAt(now)) as one missedBatch signal, then persist the
    // tick. Rows are marked fired before emit so the next tick() can't
    // re-fire them. Deliberately unbounded — no time lower-bound — so a
    // crash before the meta write / clock skew / pre-upgrade backlog can
    // never leak rows back into the per-row tick() path.
    void catchUpMissed();

    // Test seam: lets the test set a known last-tick time and assert via
    // lastTickAt(). NOTE: catch-up no longer reads this window for row
    // selection (it queries remindersReadyAt(now) unbounded); the seam
    // still drives lastTickAt() assertions and away-window metadata.
    void setLastTickAt(const QDateTime &t) { m_lastTickAt = t; }
    QDateTime lastTickAt() const { return m_lastTickAt; }

    // Test seam: lets the test override the default 60s poll interval
    // so a "fires after one tick" assertion doesn't actually wait 60s.
    void setIntervalMs(int ms);

signals:
    // Wired by NotesPanel to QSystemTrayIcon::showMessage(...). The
    // engine does NOT call into QtWidgets itself — keeps it testable.
    void reminderDue(const TodoRow &todo);
    void missedBatch(const QVector<TodoRow> &todos);

private:
    QPointer<NotesTodos> m_todos;
    QTimer    m_pollTimer;
    QDateTime m_lastTickAt;
};

#endif // NOTES_REMINDER_H
