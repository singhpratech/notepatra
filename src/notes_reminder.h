// SPDX-License-Identifier: GPL-3.0-or-later
//
// NotesReminderEngine — 60-second polling loop that emits reminderDue
// when an action item's reminder_at has elapsed. The engine itself is
// QtCore-only; the host UI (NotesPanel) wires reminderDue / missedBatch
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

    // On launch, fire a single batched "N missed reminders since
    // <lastTick>" notification for any reminders that triggered while
    // the laptop was closed.
    void catchUpMissed();

    // Test seam: lets the test set a known last-tick time before
    // calling catchUpMissed(), instead of waiting through real wall
    // clock between start() and a missed-reminder window.
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
