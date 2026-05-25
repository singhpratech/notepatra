// SPDX-License-Identifier: GPL-3.0-or-later
//
// test_notes_todos — unit coverage for NotesTodos + NotesReminderEngine.
//
// Headless, no QApplication, no real notes-root — every test creates a
// fresh QTemporaryDir, builds a NotesTodos against it, exercises the
// surface area, and asserts the SQLite + on-disk side effects.

#include "notes_todos.h"
#include "notes_reminder.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QtTest/QSignalSpy>
#include <QSqlDatabase>
#include <QSqlQuery>
#include <QTemporaryDir>
#include <QTextStream>

#include <cstdio>

// ─── tiny test harness (mirrors test_findreplace_carry_forward.cpp) ──
static int g_pass = 0, g_fail = 0;
#define EXPECT(label, cond)                                                  \
    do {                                                                     \
        if (cond) { ++g_pass; std::printf("  [PASS] %s\n", label); }         \
        else      { ++g_fail; std::printf("  [FAIL] %s\n", label); }         \
    } while (0)
#define EXPECT_EQ(label, lhs, rhs)                                           \
    do {                                                                     \
        const auto _l = (lhs); const auto _r = (rhs);                        \
        if (_l == _r) { ++g_pass; std::printf("  [PASS] %s\n", label); }     \
        else { ++g_fail; std::printf("  [FAIL] %s — expected %s, got %s\n",  \
            label, QVariant(_r).toString().toUtf8().constData(),             \
            QVariant(_l).toString().toUtf8().constData()); }                 \
    } while (0)

// ─── fixtures ─────────────────────────────────────────────────────────

// Build an HTML string with N action blocks, given (id, owner, due,
// status, text) tuples. Caller provides each tuple as a 5-string list.
static QString buildHtml(const QString &meetingTitle,
                         const QList<QStringList> &actions) {
    QString out =
        "<!doctype html>\n<html><head><meta charset=\"utf-8\">"
        "<title>" + meetingTitle + "</title></head><body>\n"
        "<h1 class=\"meet-title\">" + meetingTitle + "</h1>\n";
    for (const QStringList &a : actions) {
        const QString id     = a.value(0);
        const QString owner  = a.value(1);
        const QString due    = a.value(2);
        const QString status = a.value(3);
        const QString text   = a.value(4);
        QString attrs;
        attrs += " data-id=\"" + id + "\"";
        if (!owner.isEmpty())
            attrs += " data-owner=\"" + owner + "\"";
        if (!due.isEmpty())
            attrs += " data-due=\"" + due + "\"";
        attrs += " data-status=\"" + status + "\"";
        out += "<div class=\"b b-act\"" + attrs + ">" + text + "</div>\n";
    }
    out += "</body></html>\n";
    return out;
}

// ─── 1) schema migration on fresh DB ─────────────────────────────────

static void test_schema_fresh(const QString &workDir) {
    std::printf("\n[schema_fresh]\n");
    const QString dbPath = workDir + "/fresh.db";
    NotesTodos n(dbPath);
    QString err;
    const bool opened = n.open(&err);
    EXPECT("open() succeeds on fresh path", opened);
    if (!opened) { std::printf("  err: %s\n", err.toUtf8().constData()); return; }
    EXPECT("db file exists on disk", QFileInfo(dbPath).exists());

    // Reach into the connection to verify schema_version == 1.
    QSqlDatabase db = QSqlDatabase::database(n.connectionName());
    QSqlQuery q(db);
    bool ranV = q.exec("SELECT version FROM schema_version");
    EXPECT("schema_version table queryable", ranV && q.next());
    if (ranV && q.isValid()) EXPECT_EQ("schema version == 1", q.value(0).toInt(), 1);

    bool ranT = q.exec("SELECT name FROM sqlite_master WHERE type='table' AND name='todos'");
    EXPECT("todos table exists", ranT && q.next());

    bool ranI = q.exec("SELECT name FROM sqlite_master WHERE type='index' AND name='ix_todos_due_status'");
    EXPECT("ix_todos_due_status exists", ranI && q.next());
}

// ─── 2) no-op migration on already-v1 DB ──────────────────────────────

static void test_schema_idempotent(const QString &workDir) {
    std::printf("\n[schema_idempotent]\n");
    const QString dbPath = workDir + "/idem.db";
    {
        NotesTodos n(dbPath);
        EXPECT("first open()", n.open(nullptr));
    }
    {
        NotesTodos n(dbPath);
        EXPECT("second open() on same path", n.open(nullptr));
        QSqlDatabase db = QSqlDatabase::database(n.connectionName());
        QSqlQuery q(db);
        q.exec("SELECT version FROM schema_version");
        bool hasRow = q.next();
        EXPECT("schema_version still has exactly one row", hasRow);
        // Confirm no duplicate insertion.
        int count = 0;
        QSqlQuery q2(db);
        if (q2.exec("SELECT COUNT(*) FROM schema_version") && q2.next()) {
            count = q2.value(0).toInt();
        }
        EXPECT_EQ("schema_version row count == 1", count, 1);
    }
}

// ─── 3) reindex + orphan flag ────────────────────────────────────────

static void test_reindex_orphan(const QString &workDir) {
    std::printf("\n[reindex_orphan]\n");
    NotesTodos n(workDir + "/reindex.db");
    EXPECT("open()", n.open(nullptr));

    const QString notePath = workDir + "/Meeting-Apr-23.html";
    const QString future = QDateTime::currentDateTime().addDays(2)
        .toString("yyyy-MM-ddTHH:mm");
    QList<QStringList> v1 = {
        {"id-aaa", "@alice", future, "open",   "review PR #42"},
        {"id-bbb", "@bob",   "",     "open",   "draft architecture doc"},
        {"id-ccc", "",       future, "done",   "ship v0.1.93"},
    };
    n.reindexNote(notePath, buildHtml("Q2 Sync", v1));

    QSqlDatabase db = QSqlDatabase::database(n.connectionName());
    QSqlQuery q(db);
    q.prepare("SELECT COUNT(*) FROM todos WHERE source_file = ?");
    q.addBindValue(notePath);
    q.exec(); q.next();
    EXPECT_EQ("three rows after first reindex", q.value(0).toInt(), 3);

    TodoRow aaa = n.find("id-aaa");
    EXPECT_EQ("id-aaa text round-trips", aaa.text, QStringLiteral("review PR #42"));
    EXPECT_EQ("id-aaa owner round-trips", aaa.owner, QStringLiteral("@alice"));
    EXPECT_EQ("id-aaa meeting_title set", aaa.meetingTitle, QStringLiteral("Q2 Sync"));
    EXPECT("id-aaa due_at valid", aaa.dueAt.isValid());

    TodoRow ccc = n.find("id-ccc");
    EXPECT_EQ("id-ccc status = done", ccc.status, QStringLiteral("done"));

    // Second reindex — id-bbb removed, id-ccc retained, id-ddd added.
    QList<QStringList> v2 = {
        {"id-aaa", "@alice", future, "open",   "review PR #42 (updated)"},
        {"id-ccc", "",       future, "done",   "ship v0.1.93"},
        {"id-ddd", "@dan",   "",     "open",   "schedule retro"},
    };
    n.reindexNote(notePath, buildHtml("Q2 Sync", v2));

    EXPECT_EQ("id-aaa text updated on reindex",
              n.find("id-aaa").text, QStringLiteral("review PR #42 (updated)"));
    EXPECT_EQ("id-ddd inserted", n.find("id-ddd").id, QStringLiteral("id-ddd"));
    EXPECT("id-bbb flagged source_file_missing",
           n.find("id-bbb").sourceFileMissing);
    EXPECT_EQ("id-bbb status preserved",
              n.find("id-bbb").status, QStringLiteral("open"));
    EXPECT("id-aaa source_file_missing CLEARED on second reindex",
           !n.find("id-aaa").sourceFileMissing);
}

// ─── 4) due-group classification across time boundaries ──────────────

static void test_due_groups(const QString &workDir) {
    std::printf("\n[due_groups]\n");
    NotesTodos n(workDir + "/groups.db");
    EXPECT("open()", n.open(nullptr));

    const QDateTime now = QDateTime::currentDateTime();
    auto isoLocal = [](const QDateTime &dt) {
        return dt.toString("yyyy-MM-ddTHH:mm");
    };

    // We deliberately build rows directly (no HTML round-trip) so we
    // can pin exact due times. id-overdue / id-today / id-week /
    // id-faraway / id-undated cover every group boundary.
    QList<QStringList> rows = {
        {"id-overdue", "@x", isoLocal(now.addSecs(-60)),    "open", "past due"},
        // Use a small +60s offset rather than +1h so this test is not
        // flaky near midnight local time. +1h could roll past midnight
        // when run after ~23:00, throwing id-today into the Week group.
        {"id-today",   "@x", isoLocal(now.addSecs(60)),     "open", "in a minute"},
        {"id-week",    "@x", isoLocal(now.addDays(5)),      "open", "in five days"},
        {"id-faraway", "@x", isoLocal(now.addDays(40)),     "open", "in 40 days"},
        {"id-undated", "@x", "",                            "open", "no due"},
        {"id-done",    "@x", isoLocal(now.addSecs(-30)),    "done", "done already"},
    };
    n.reindexNote(workDir + "/groups.html",
                  buildHtml("Boundary Test", rows));

    auto hasId = [](const QVector<TodoRow> &v, const QString &id) {
        for (const TodoRow &r : v) if (r.id == id) return true;
        return false;
    };

    const QDateTime nowUtc = QDateTime::currentDateTimeUtc();
    const QVector<TodoRow> overdue = n.dueGroupOverdue(nowUtc);
    EXPECT("Overdue contains id-overdue", hasId(overdue, "id-overdue"));
    EXPECT("Overdue does NOT contain id-done (done items excluded)",
           !hasId(overdue, "id-done"));
    EXPECT("Overdue does NOT contain id-today", !hasId(overdue, "id-today"));

    const QVector<TodoRow> today = n.dueGroupToday(nowUtc);
    EXPECT("Today contains id-today", hasId(today, "id-today"));
    EXPECT("Today does NOT contain id-week", !hasId(today, "id-week"));

    const QVector<TodoRow> week = n.dueGroupWeek(nowUtc);
    EXPECT("Week contains id-week", hasId(week, "id-week"));
    EXPECT("Week does NOT contain id-today (today is its own group)",
           !hasId(week, "id-today"));
    EXPECT("Week does NOT contain id-faraway",
           !hasId(week, "id-faraway"));

    const QVector<TodoRow> someday = n.dueGroupSomeday(nowUtc);
    EXPECT("Someday contains id-undated", hasId(someday, "id-undated"));
    EXPECT("Someday contains id-faraway (>30 days)",
           hasId(someday, "id-faraway"));
    EXPECT("Someday does NOT contain id-week", !hasId(someday, "id-week"));

    const QVector<TodoRow> done = n.dueGroupDone(10);
    EXPECT("Done contains id-done", hasId(done, "id-done"));
    EXPECT("Done does NOT contain id-overdue", !hasId(done, "id-overdue"));
}

// ─── 5) markDone + snooze ────────────────────────────────────────────

static void test_mark_done_and_snooze(const QString &workDir) {
    std::printf("\n[mark_done_and_snooze]\n");
    NotesTodos n(workDir + "/markdone.db");
    EXPECT("open()", n.open(nullptr));

    QList<QStringList> rows = {
        {"id-zzz", "@x", "", "open", "do the thing"},
    };
    n.reindexNote(workDir + "/m.html", buildHtml("Plain", rows));

    const QDateTime when = QDateTime::currentDateTimeUtc();
    EXPECT("markDone returns true", n.markDone("id-zzz", when));
    TodoRow r = n.find("id-zzz");
    EXPECT_EQ("status = done", r.status, QStringLiteral("done"));
    EXPECT("completed_at populated", r.completedAt.isValid());

    EXPECT("markOpen returns true", n.markOpen("id-zzz"));
    r = n.find("id-zzz");
    EXPECT_EQ("status back to open", r.status, QStringLiteral("open"));
    EXPECT("completed_at cleared", !r.completedAt.isValid());

    // snooze: store at 10s precision (ISODate is second-precision). We
    // round to the second on both sides of the comparison to avoid
    // sub-second drift.
    QDateTime until = QDateTime::currentDateTimeUtc().addSecs(3600);
    until = until.addMSecs(-until.time().msec());
    EXPECT("snooze returns true", n.snooze("id-zzz", until));
    r = n.find("id-zzz");
    EXPECT("snooze_until populated", r.snoozeUntil.isValid());
    EXPECT_EQ("reminder_status = scheduled",
              r.reminderStatus, QStringLiteral("scheduled"));
    EXPECT("reminder_at populated", r.reminderAt.isValid());
    EXPECT_EQ("snooze_until == reminder_at",
              r.reminderAt.toSecsSinceEpoch(), r.snoozeUntil.toSecsSinceEpoch());
}

// ─── 6) remindersReadyAt filtering ───────────────────────────────────

static void test_reminders_ready(const QString &workDir) {
    std::printf("\n[reminders_ready]\n");
    NotesTodos n(workDir + "/ready.db");
    EXPECT("open()", n.open(nullptr));

    QList<QStringList> rows = {
        {"id-a", "@x", "", "open",   "scheduled past"},
        {"id-b", "@x", "", "open",   "scheduled future"},
        {"id-c", "@x", "", "done",   "scheduled but DONE"},
        {"id-d", "@x", "", "open",   "no reminder set"},
        {"id-e", "@x", "", "open",   "already fired"},
    };
    n.reindexNote(workDir + "/rd.html", buildHtml("Rd", rows));

    const QDateTime now = QDateTime::currentDateTimeUtc();
    n.setReminder("id-a", now.addSecs(-300));   // past — should fire
    n.setReminder("id-b", now.addSecs( 300));   // future — should not
    n.setReminder("id-c", now.addSecs(-300));   // past BUT done — excluded
    // id-d: no setReminder
    n.setReminder("id-e", now.addSecs(-300));
    n.markReminderFired("id-e");                // status flips to 'fired'

    const QVector<TodoRow> ready = n.remindersReadyAt(now);
    auto hasId = [](const QVector<TodoRow> &v, const QString &id) {
        for (const TodoRow &r : v) if (r.id == id) return true;
        return false;
    };
    EXPECT_EQ("exactly one ready", ready.size(), 1);
    EXPECT("id-a is ready", hasId(ready, "id-a"));
    EXPECT("id-b NOT ready (future)", !hasId(ready, "id-b"));
    EXPECT("id-c NOT ready (done)", !hasId(ready, "id-c"));
    EXPECT("id-d NOT ready (no reminder)", !hasId(ready, "id-d"));
    EXPECT("id-e NOT ready (already fired)", !hasId(ready, "id-e"));
}

// ─── 7) reminder engine tick + catchUpMissed ────────────────────────

static void test_reminder_engine(const QString &workDir) {
    std::printf("\n[reminder_engine]\n");
    NotesTodos n(workDir + "/engine.db");
    EXPECT("open()", n.open(nullptr));
    QList<QStringList> rows = {
        {"id-ping", "@x", "", "open", "ping me"},
    };
    n.reindexNote(workDir + "/e.html", buildHtml("E", rows));
    n.setReminder("id-ping",
                  QDateTime::currentDateTimeUtc().addSecs(-60));

    NotesReminderEngine engine(&n);
    QSignalSpy spy(&engine, &NotesReminderEngine::reminderDue);
    engine.tick();
    EXPECT_EQ("reminderDue emitted exactly once on first tick",
              spy.count(), 1);
    EXPECT_EQ("reminderDue arg is the id-ping row",
              spy.first().first().value<TodoRow>().id,
              QStringLiteral("id-ping"));

    // Second tick — must NOT re-emit, because tick() should have
    // flipped reminder_status to 'fired'.
    engine.tick();
    EXPECT_EQ("second tick does not re-emit", spy.count(), 1);
    EXPECT_EQ("reminder_status flipped to fired",
              n.find("id-ping").reminderStatus, QStringLiteral("fired"));

    // catchUpMissed — set up a NEW row, set lastTickAt into the past,
    // expect missedBatch to fire once.
    QList<QStringList> rows2 = {
        {"id-late", "@x", "", "open", "late ping"},
    };
    n.reindexNote(workDir + "/e2.html", buildHtml("E2", rows2));
    n.setReminder("id-late",
                  QDateTime::currentDateTimeUtc().addSecs(-30));

    QSignalSpy batch(&engine, &NotesReminderEngine::missedBatch);
    engine.setLastTickAt(
        QDateTime::currentDateTimeUtc().addSecs(-3600));
    engine.catchUpMissed();
    EXPECT_EQ("missedBatch fired once", batch.count(), 1);
    if (batch.count() >= 1) {
        const QVector<TodoRow> v = batch.first().first().value<QVector<TodoRow>>();
        EXPECT_EQ("batch contains exactly one item", v.size(), 1);
        if (!v.isEmpty()) {
            EXPECT_EQ("batch item is id-late", v.first().id,
                      QStringLiteral("id-late"));
        }
    }
}

// ─── 8) rebuildFromHtmlFiles ─────────────────────────────────────────

static void test_rebuild_from_html(const QString &workDir) {
    std::printf("\n[rebuild_from_html]\n");
    const QString notesRoot = workDir + "/rebuild_root";
    QDir().mkpath(notesRoot);
    QDir().mkpath(notesRoot + "/sub");

    // Two HTML files with action blocks.
    {
        QFile f(notesRoot + "/standup.html");
        f.open(QIODevice::WriteOnly);
        QTextStream t(&f);
        t << buildHtml("Standup", {{"id-x", "@you", "", "open", "do X"}});
    }
    {
        QFile f(notesRoot + "/sub/planning.html");
        f.open(QIODevice::WriteOnly);
        QTextStream t(&f);
        t << buildHtml("Planning", {{"id-y", "@bob", "", "open", "plan Y"}});
    }

    NotesTodos n(workDir + "/rebuild.db");
    EXPECT("open()", n.open(nullptr));
    QString err;
    EXPECT("rebuildFromHtmlFiles succeeds",
           n.rebuildFromHtmlFiles(notesRoot, &err));
    if (!err.isEmpty()) std::printf("  err: %s\n", err.toUtf8().constData());
    EXPECT_EQ("id-x picked up", n.find("id-x").id, QStringLiteral("id-x"));
    EXPECT_EQ("id-y picked up (nested dir)",
              n.find("id-y").id, QStringLiteral("id-y"));

    // Now wipe everything and re-rebuild — should still pick up both.
    QSqlDatabase db = QSqlDatabase::database(n.connectionName());
    QSqlQuery wipe(db);
    wipe.exec("DELETE FROM todos");
    EXPECT("post-wipe, find returns empty",
           n.find("id-x").id.isEmpty());
    EXPECT("rebuild after wipe", n.rebuildFromHtmlFiles(notesRoot, &err));
    EXPECT_EQ("id-x re-picked", n.find("id-x").id, QStringLiteral("id-x"));
}

// ─── 9) addQuickTodo ─────────────────────────────────────────────────

static void test_add_quick_todo(const QString &workDir) {
    std::printf("\n[add_quick_todo]\n");
    const QString notesRoot = workDir + "/inbox_root";
    QDir().mkpath(notesRoot);
    // DB lives DIRECTLY in the notes root so addQuickTodo's
    // "<notesRoot>/Inbox/quick-todos.html" inference picks the right
    // location. (NotesPanel may put the DB under .notepatra/ — see the
    // header's note on convention.)
    NotesTodos n(notesRoot + "/todos.db");
    EXPECT("open()", n.open(nullptr));

    QDateTime due = QDateTime::currentDateTime().addDays(2);
    due = due.addMSecs(-due.time().msec());  // round to second precision
    const QString id = n.addQuickTodo("buy groceries", "@you", due);
    EXPECT("addQuickTodo returns non-empty id", !id.isEmpty());

    const QString inboxFile = notesRoot + "/Inbox/quick-todos.html";
    EXPECT("Inbox/quick-todos.html exists", QFileInfo(inboxFile).exists());
    {
        QFile f(inboxFile);
        f.open(QIODevice::ReadOnly | QIODevice::Text);
        const QString body = QString::fromUtf8(f.readAll());
        EXPECT("file contains the new data-id",
               body.contains("data-id=\"" + id + "\""));
        EXPECT("file contains the todo text",
               body.contains("buy groceries"));
    }

    TodoRow r = n.find(id);
    EXPECT_EQ("row text matches", r.text, QStringLiteral("buy groceries"));
    EXPECT_EQ("row owner matches", r.owner, QStringLiteral("@you"));
    EXPECT("row due_at populated", r.dueAt.isValid());
    EXPECT_EQ("row status open", r.status, QStringLiteral("open"));
    EXPECT_EQ("row meeting_title = Inbox",
              r.meetingTitle, QStringLiteral("Inbox"));

    // Add a second quick todo — file should APPEND, not overwrite.
    const QString id2 = n.addQuickTodo("call dentist", "", QDateTime());
    EXPECT("second addQuickTodo returns non-empty id", !id2.isEmpty());
    {
        QFile f(inboxFile);
        f.open(QIODevice::ReadOnly | QIODevice::Text);
        const QString body = QString::fromUtf8(f.readAll());
        EXPECT("first todo still present after append",
               body.contains("buy groceries"));
        EXPECT("second todo appended", body.contains("call dentist"));
    }
}

// ─── 10) setText — v0.1.94 inline-editable Todos rows ────────────────

static void test_set_text(const QString &workDir) {
    std::printf("\n[set_text]\n");
    const QString notesRoot = workDir + "/setText_root";
    QDir().mkpath(notesRoot);
    NotesTodos n(notesRoot + "/todos.db");
    EXPECT("open()", n.open(nullptr));

    const QString id = n.addQuickTodo("original text", "@me", QDateTime());
    EXPECT("addQuickTodo returns non-empty id", !id.isEmpty());

    QSignalSpy spy(&n, &NotesTodos::todoChanged);

    // Happy path — update the text.
    EXPECT("setText happy path returns true",
           n.setText(id, QStringLiteral("updated text")));
    EXPECT("todoChanged emitted once", spy.count() == 1);
    EXPECT_EQ("first arg is the id",
              spy.value(0).value(0).toString(), id);

    TodoRow r = n.find(id);
    EXPECT_EQ("row text reflects update", r.text, QStringLiteral("updated text"));
    // Other fields untouched.
    EXPECT_EQ("owner preserved", r.owner, QStringLiteral("@me"));
    EXPECT_EQ("status preserved", r.status, QStringLiteral("open"));

    // Whitespace trim.
    spy.clear();
    EXPECT("setText trims whitespace",
           n.setText(id, QStringLiteral("   spaced   ")));
    EXPECT_EQ("text stored trimmed",
              n.find(id).text, QStringLiteral("spaced"));

    // Reject empty / whitespace-only.
    spy.clear();
    EXPECT("setText empty rejected", !n.setText(id, QString()));
    EXPECT("setText whitespace-only rejected",
           !n.setText(id, QStringLiteral("   \t  \n  ")));
    EXPECT("no spurious todoChanged from rejections", spy.count() == 0);
    EXPECT_EQ("text unchanged after rejected calls",
              n.find(id).text, QStringLiteral("spaced"));

    // Reject unknown id.
    spy.clear();
    EXPECT("setText unknown id returns false",
           !n.setText(QStringLiteral("does-not-exist"), QStringLiteral("x")));
    EXPECT("no todoChanged for unknown id", spy.count() == 0);

    // Reject empty id.
    EXPECT("setText empty id returns false",
           !n.setText(QString(), QStringLiteral("x")));
}

// ─── main ────────────────────────────────────────────────────────────

int main(int argc, char *argv[]) {
    QCoreApplication app(argc, argv);
    qRegisterMetaType<TodoRow>("TodoRow");
    qRegisterMetaType<QVector<TodoRow>>("QVector<TodoRow>");

    QTemporaryDir tmp;
    if (!tmp.isValid()) {
        std::fprintf(stderr, "could not create QTemporaryDir\n");
        return 1;
    }
    const QString root = tmp.path();

    test_schema_fresh(root);
    test_schema_idempotent(root);
    test_reindex_orphan(root);
    test_due_groups(root);
    test_mark_done_and_snooze(root);
    test_reminders_ready(root);
    test_reminder_engine(root);
    test_rebuild_from_html(root);
    test_add_quick_todo(root);
    test_set_text(root);

    // ─── 11) trash + restore + permanent delete (v0.1.95+) ───────
    std::printf("\n[trash_lifecycle]\n");
    {
        const QString notesRoot = root + "/trash_root";
        QDir().mkpath(notesRoot);
        NotesTodos n(notesRoot + "/todos.db");
        EXPECT("open()", n.open(nullptr));

        const QString a = n.addQuickTodo("apples", "@me", QDateTime());
        const QString b = n.addQuickTodo("bananas", "@me", QDateTime());
        const QString c = n.addQuickTodo("cherries", "@me", QDateTime());
        EXPECT("3 quick todos created", !a.isEmpty() && !b.isEmpty() && !c.isEmpty());

        QSignalSpy spy(&n, &NotesTodos::todoChanged);

        // Trash b. Should disappear from Someday, appear in Trashed.
        EXPECT("trashRow(b) returns true", n.trashRow(b));
        EXPECT("todoChanged emitted on trash", spy.count() == 1);
        EXPECT_EQ("b.status is 'trashed' after trashRow",
                  n.find(b).status, QStringLiteral("trashed"));

        const auto someday = n.dueGroupSomeday(QDateTime::currentDateTime());
        bool sawBInSomeday = false;
        for (const auto &r : someday) if (r.id == b) sawBInSomeday = true;
        EXPECT("b NOT in Someday after trash", !sawBInSomeday);

        const auto trashed = n.dueGroupTrashed(100);
        bool sawBInTrashed = false;
        for (const auto &r : trashed) if (r.id == b) sawBInTrashed = true;
        EXPECT("b IS in Trashed after trash", sawBInTrashed);
        EXPECT("Trashed group has 1 row", trashed.size() == 1);

        // Other live todos (a, c) untouched.
        const auto someday2 = n.dueGroupSomeday(QDateTime::currentDateTime());
        EXPECT("Someday still has 2 live rows (a, c)", someday2.size() == 2);

        // Restore b → back to open.
        spy.clear();
        EXPECT("restoreRow(b) returns true", n.restoreRow(b));
        EXPECT("todoChanged emitted on restore", spy.count() == 1);
        EXPECT_EQ("b.status is 'open' after restore",
                  n.find(b).status, QStringLiteral("open"));
        EXPECT("Trashed group empty after restore",
               n.dueGroupTrashed(100).isEmpty());
        EXPECT("Someday back to 3 rows", n.dueGroupSomeday(
                   QDateTime::currentDateTime()).size() == 3);

        // Now trash + permanently delete b.
        EXPECT("trashRow(b) again", n.trashRow(b));
        spy.clear();
        EXPECT("deleteRow(b) returns true", n.deleteRow(b));
        EXPECT("todoChanged emitted on delete", spy.count() == 1);
        EXPECT("b.id is empty after permanent delete (not found)",
               n.find(b).id.isEmpty());

        // Edge cases.
        EXPECT("trashRow on unknown id rejected", !n.trashRow("nope"));
        EXPECT("restoreRow on unknown id rejected", !n.restoreRow("nope"));
        EXPECT("trashRow on empty id rejected", !n.trashRow(QString()));
        EXPECT("restoreRow on empty id rejected", !n.restoreRow(QString()));
    }

    // ─── 12) note-level reminders (v0.1.98 — todos dropped, reminder
    //         binds to a note FILE via right-click → Set reminder) ─────
    std::printf("\n[note_reminder]\n");
    {
        const QString notesRoot = root + "/note_rem_root";
        QDir().mkpath(notesRoot);
        NotesTodos n(notesRoot + "/todos.db");
        EXPECT("open()", n.open(nullptr));

        const QString notePath =
            notesRoot + "/Inbox/2026-05-24-160000-noter-01.html";

        // Future reminder → stored + queryable.
        const QDateTime future = QDateTime::currentDateTimeUtc().addSecs(3600);
        const QString id = n.setNoteReminder(notePath, "Noter 01", future);
        EXPECT("setNoteReminder returns an id", !id.isEmpty());
        EXPECT("noteReminderAt returns the scheduled time",
               qAbs(n.noteReminderAt(notePath).toUTC().secsTo(future)) <= 1);

        // A reminder whose time has passed is returned by remindersReadyAt
        // — i.e. the engine WILL fire it. This is the user-visible contract.
        const QDateTime past = QDateTime::currentDateTimeUtc().addSecs(-60);
        n.setNoteReminder(notePath, "Noter 01", past);
        bool fires = false;
        for (const TodoRow &r :
             n.remindersReadyAt(QDateTime::currentDateTimeUtc()))
            if (r.sourceFile == notePath) fires = true;
        EXPECT("a due note reminder fires (remindersReadyAt returns it)", fires);

        // Reschedule must reuse the row, not create a second.
        const QDateTime future2 = QDateTime::currentDateTimeUtc().addSecs(7200);
        const QString id2 = n.setNoteReminder(notePath, "Noter 01", future2);
        EXPECT("reschedule reuses the same row", id2 == id);

        // reindexNote of the note (a freeform note has no action blocks)
        // must NOT clobber the note-reminder row.
        n.reindexNote(notePath, QStringLiteral(
            "<html><body><h1 class=\"meet-title\">Noter 01</h1></body></html>"));
        EXPECT("note reminder survives reindexNote of its note",
               n.noteReminderAt(notePath).isValid());

        // Clear → gone.
        n.setNoteReminder(notePath, "Noter 01", QDateTime());
        EXPECT("noteReminderAt invalid after clear",
               !n.noteReminderAt(notePath).isValid());
    }

    // ─── 13) per-action reminders (v0.1.98 — Extract schedules these;
    //         central Reminders root lists them via allScheduledReminders) ──
    std::printf("\n[action_reminders]\n");
    {
        const QString notesRoot = root + "/action_rem_root";
        QDir().mkpath(notesRoot);
        NotesTodos n(notesRoot + "/todos.db");
        EXPECT("open()", n.open(nullptr));

        const QString notePath = notesRoot + "/Inbox/2026-05-24-noter-02.html";
        const QDateTime t1 = QDateTime::currentDateTimeUtc().addSecs(3600);
        const QDateTime t2 = QDateTime::currentDateTimeUtc().addSecs(7200);

        // addReminder always INSERTs — multiple reminders coexist on ONE note
        // (unlike setNoteReminder which is one-per-file).
        const QString r1 = n.addReminder(notePath, "Ship build  @prateek", t1);
        const QString r2 = n.addReminder(notePath, "Email Priya", t2);
        EXPECT("addReminder #1 returns id", !r1.isEmpty());
        EXPECT("addReminder #2 returns id", !r2.isEmpty());
        EXPECT("the two reminders are distinct rows", r1 != r2);

        // allScheduledReminders lists BOTH, globally ordered by reminder_at ASC.
        QVector<TodoRow> all = n.allScheduledReminders();
        int onThisNote = 0, posR1 = -1, posR2 = -1;
        for (int i = 0; i < all.size(); ++i) {
            if (all[i].sourceFile == notePath) ++onThisNote;
            if (all[i].id == r1) posR1 = i;
            if (all[i].id == r2) posR2 = i;
        }
        EXPECT_EQ("allScheduledReminders lists both action reminders", onThisNote, 2);
        EXPECT("reminders ordered by time (earlier first)",
               posR1 >= 0 && posR2 >= 0 && posR1 < posR2);

        // CRITICAL: saving the note (reindexNote) must NOT delete or orphan the
        // action-reminder rows — that's the v0.1.98 guard widening. The note
        // also gains a REAL action block that must NOT appear in the reminders
        // list (it has no scheduled reminder).
        n.reindexNote(notePath, QStringLiteral(
            "<html><body><h1 class=\"meet-title\">Noter 02</h1>"
            "<div class=\"b b-act\" data-id=\"real-action-1\" data-status=\"open\">do a thing</div>"
            "</body></html>"));
        int afterReindex = 0;
        for (const TodoRow &r : n.allScheduledReminders())
            if (r.sourceFile == notePath) ++afterReindex;
        EXPECT_EQ("action reminders survive reindexNote", afterReindex, 2);
        EXPECT("action reminder #1 not flagged source_file_missing",
               !n.find(r1).sourceFileMissing);

        // setReminder changes the time; deleteRow removes it.
        const QDateTime t3 = QDateTime::currentDateTimeUtc().addSecs(10800);
        EXPECT("setReminder(r1, t3) returns true", n.setReminder(r1, t3));
        EXPECT("r1 reminder_at updated",
               qAbs(n.find(r1).reminderAt.toUTC().secsTo(t3)) <= 1);
        EXPECT_EQ("r1 still scheduled after change",
                  n.find(r1).reminderStatus, QStringLiteral("scheduled"));

        EXPECT("deleteRow(r2) returns true", n.deleteRow(r2));
        int afterDelete = 0;
        for (const TodoRow &r : n.allScheduledReminders())
            if (r.sourceFile == notePath) ++afterDelete;
        EXPECT_EQ("one reminder left after delete", afterDelete, 1);
    }

    std::printf("\n────────────────────────\n");
    std::printf("PASS: %d   FAIL: %d\n", g_pass, g_fail);
    return g_fail == 0 ? 0 : 1;
}
