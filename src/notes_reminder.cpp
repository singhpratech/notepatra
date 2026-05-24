// SPDX-License-Identifier: GPL-3.0-or-later

#include "notes_reminder.h"

#include <QDateTime>
#include <QVector>

// ═══════════════════════════════════════════════════════════════════════
// NotesReminderEngine — 60s poll loop. See header for the contract; this
// .cpp is the entire implementation.
//
// Design notes:
//   • Why poll, not event-driven? Reminders can be set hours / days in
//     the future. A QTimer per reminder would balloon to thousands on a
//     long-lived note collection, and we'd still need a launch-time
//     reconciliation pass for reminders that triggered while the app
//     was closed. Polling once a minute against an indexed SQLite
//     column is cheap and side-effect-free.
//   • Why 60s, not 1s? Reminder granularity is "minute" (set via the
//     UI's HH:mm picker). Polling faster wastes battery on laptops; a
//     full second of skew at fire-time is below user-perceptible.
//   • Why mark fired BEFORE emitting? Otherwise the emit -> slot ->
//     tray-icon path could re-enter the engine (e.g. via user clicking
//     "Show now" which calls back into NotesTodos) and we'd re-fire the
//     same reminder on the next tick before the slot updated the row.
// ═══════════════════════════════════════════════════════════════════════

NotesReminderEngine::NotesReminderEngine(NotesTodos *todos, QObject *parent)
    : QObject(parent), m_todos(todos) {
    m_pollTimer.setInterval(60 * 1000);
    m_pollTimer.setSingleShot(false);
    connect(&m_pollTimer, &QTimer::timeout, this, &NotesReminderEngine::tick);
    m_lastTickAt = QDateTime::currentDateTimeUtc();
}

void NotesReminderEngine::start() {
    // Idempotent — second call is a no-op. We deliberately do NOT
    // tick() here; callers that want immediate processing call
    // catchUpMissed() then start().
    if (m_pollTimer.isActive()) return;
    m_pollTimer.start();
}

void NotesReminderEngine::stop() {
    m_pollTimer.stop();
}

void NotesReminderEngine::setIntervalMs(int ms) {
    m_pollTimer.setInterval(ms);
}

void NotesReminderEngine::tick() {
    if (!m_todos) return;
    const QDateTime now = QDateTime::currentDateTimeUtc();
    const QVector<TodoRow> ready = m_todos->remindersReadyAt(now);
    for (const TodoRow &r : ready) {
        // Mark fired FIRST so we don't re-fire on next tick. Even if
        // the slot bound to reminderDue throws or blocks, the DB row
        // is already in the right state.
        m_todos->markReminderFired(r.id);
        emit reminderDue(r);
    }
    m_lastTickAt = now;
}

void NotesReminderEngine::catchUpMissed() {
    if (!m_todos) return;
    const QDateTime now = QDateTime::currentDateTimeUtc();

    // The window is "anything that should have fired between our last
    // known tick and now". If we've never run, lastTickAt is the
    // constructor-time stamp, which is "now-ish" — that's fine,
    // there's nothing to catch up on.
    QVector<TodoRow> missed = m_todos->remindersMissedSince(m_lastTickAt);
    if (missed.isEmpty()) {
        m_lastTickAt = now;
        return;
    }

    // Mark each as fired so they don't re-fire on the next normal tick,
    // then emit one batch signal. The UI shows ONE notification
    // ("3 reminders missed while you were away") so we don't bombard
    // the user with N popups on launch.
    for (const TodoRow &r : missed) {
        m_todos->markReminderFired(r.id);
    }
    emit missedBatch(missed);
    m_lastTickAt = now;
}
