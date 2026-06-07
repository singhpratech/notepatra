// SPDX-License-Identifier: GPL-3.0-or-later

#include "notes_todos.h"

#include <QDateTime>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QRegularExpression>
#include <QSet>
#include <QSqlDatabase>
#include <QSqlError>
#include <QSqlQuery>
#include <QSqlRecord>
#include <QTextStream>
#include <QUuid>
#include <QVariant>

// ═══════════════════════════════════════════════════════════════════════
// NotesTodos — SQLite cache for action items extracted from meeting-note
// HTML files. See the header doc-comment for the threading + ownership
// contract.
// ═══════════════════════════════════════════════════════════════════════

namespace {

constexpr int kCurrentSchemaVersion = 1;

// All status string constants are tracked as freestanding symbols so a
// future refactor can switch them to an enum without grepping every
// SQL literal.
const char *const kStatusOpen      = "open";
const char *const kStatusDone      = "done";
const char *const kStatusCancelled = "cancelled";

const char *const kReminderNone      = "none";
const char *const kReminderScheduled = "scheduled";
// v0.1.98 — source_block_id sentinel for a note-level reminder row (a
// reminder bound to a whole note file, not an HTML action block).
const char *const kNoteReminderBlock = "__note_reminder__";
const char *const kReminderFired     = "fired";
const char *const kReminderSnoozed   = "snoozed";
const char *const kReminderDismissed = "dismissed";

// Conservative whitelist of action-status values we accept from the
// HTML. Anything else falls back to "open" so a bad attribute doesn't
// silently hide the row from the UI.
QString normaliseStatus(const QString &raw) {
    const QString s = raw.trimmed().toLower();
    if (s == QLatin1String(kStatusDone))      return QLatin1String(kStatusDone);
    if (s == QLatin1String(kStatusCancelled)) return QLatin1String(kStatusCancelled);
    return QLatin1String(kStatusOpen);
}

} // namespace

// ─── construction ──────────────────────────────────────────────────────

NotesTodos::NotesTodos(const QString &dbPath, QObject *parent)
    : QObject(parent), m_dbPath(dbPath) {
    // Unique per-instance connection name so multiple NotesTodos can
    // coexist (e.g. tests in the same process). Connection lifetime is
    // bound to the QObject — closed + removed in the destructor.
    m_connName = QStringLiteral("notepatra-notes-%1")
        .arg(QUuid::createUuid().toString(QUuid::Id128));
}

NotesTodos::~NotesTodos() {
    if (m_db.isValid()) {
        if (m_db.isOpen()) m_db.close();
        m_db = QSqlDatabase();  // drop ref before removeDatabase
        QSqlDatabase::removeDatabase(m_connName);
    }
}

// ─── open + schema ─────────────────────────────────────────────────────

bool NotesTodos::open(QString *errorOut) {
    if (m_db.isValid() && m_db.isOpen()) {
        return true;
    }
    // Ensure the parent directory exists so passing a fresh path Just
    // Works. SQLite would otherwise fail with "unable to open database
    // file" and the user has to read the error log to diagnose.
    QFileInfo fi(m_dbPath);
    if (!fi.absoluteDir().exists()) {
        if (!QDir().mkpath(fi.absoluteDir().absolutePath())) {
            if (errorOut) *errorOut = QStringLiteral("cannot create %1")
                .arg(fi.absoluteDir().absolutePath());
            return false;
        }
    }

    m_db = QSqlDatabase::contains(m_connName)
        ? QSqlDatabase::database(m_connName, /*open=*/false)
        : QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), m_connName);
    m_db.setDatabaseName(m_dbPath);
    if (!m_db.open()) {
        if (errorOut) *errorOut = m_db.lastError().text();
        return false;
    }

    // WAL = concurrent readers don't block the writer. NORMAL sync =
    // fsync on checkpoint only, not on every commit. Loses at most the
    // last few writes on a hard crash; acceptable for a rebuildable
    // cache.
    {
        QSqlQuery q(m_db);
        q.exec(QStringLiteral("PRAGMA journal_mode=WAL"));
        q.exec(QStringLiteral("PRAGMA synchronous=NORMAL"));
        q.exec(QStringLiteral("PRAGMA foreign_keys=ON"));
    }

    if (!createSchema(errorOut)) return false;
    const int existing = currentSchemaVersion();
    if (existing < kCurrentSchemaVersion) {
        if (!migrateSchema(existing, errorOut)) return false;
    }
    return true;
}

bool NotesTodos::ensureOpen() const {
    // const because the read-side queries call this. SQLite reopen is a
    // last-ditch self-heal; callers that need detailed errors should
    // have already called open() at startup.
    if (m_db.isValid() && m_db.isOpen()) return true;
    return false;
}

bool NotesTodos::createSchema(QString *errorOut) {
    QSqlQuery q(m_db);
    if (!q.exec(QStringLiteral(
            "CREATE TABLE IF NOT EXISTS schema_version ("
            " version INTEGER PRIMARY KEY)"))) {
        if (errorOut) *errorOut = q.lastError().text();
        return false;
    }
    // Seed v1 row on first run. The IGNORE makes subsequent runs harmless.
    if (!q.exec(QStringLiteral(
            "INSERT OR IGNORE INTO schema_version VALUES (1)"))) {
        if (errorOut) *errorOut = q.lastError().text();
        return false;
    }
    if (!q.exec(QStringLiteral(
            "CREATE TABLE IF NOT EXISTS todos ("
            " id              TEXT PRIMARY KEY,"
            " source_file     TEXT NOT NULL,"
            " source_block_id TEXT NOT NULL,"
            " text            TEXT NOT NULL,"
            " owner           TEXT,"
            " due_at          TEXT,"
            " status          TEXT NOT NULL DEFAULT 'open',"
            " reminder_at     TEXT,"
            " reminder_status TEXT NOT NULL DEFAULT 'none',"
            " snooze_until    TEXT,"
            " recurrence      TEXT,"
            " meeting_title   TEXT,"
            " created_at      TEXT NOT NULL,"
            " completed_at    TEXT,"
            " source_file_missing INTEGER NOT NULL DEFAULT 0)"))) {
        if (errorOut) *errorOut = q.lastError().text();
        return false;
    }
    if (!q.exec(QStringLiteral(
            "CREATE INDEX IF NOT EXISTS ix_todos_due_status"
            " ON todos(due_at, status)"))) {
        if (errorOut) *errorOut = q.lastError().text();
        return false;
    }
    if (!q.exec(QStringLiteral(
            "CREATE INDEX IF NOT EXISTS ix_todos_reminder"
            " ON todos(reminder_at, reminder_status, status)"))) {
        if (errorOut) *errorOut = q.lastError().text();
        return false;
    }
    if (!q.exec(QStringLiteral(
            "CREATE INDEX IF NOT EXISTS ix_todos_source_file"
            " ON todos(source_file)"))) {
        if (errorOut) *errorOut = q.lastError().text();
        return false;
    }
    if (!q.exec(QStringLiteral(
            "CREATE INDEX IF NOT EXISTS ix_todos_owner"
            " ON todos(owner)"))) {
        if (errorOut) *errorOut = q.lastError().text();
        return false;
    }
    // App-lifetime reminder service bookkeeping (key/value). Runs on
    // every open() so existing v1 DBs self-upgrade — no schema-version
    // bump needed (IF NOT EXISTS, like every sibling statement).
    if (!q.exec(QStringLiteral(
            "CREATE TABLE IF NOT EXISTS meta ("
            " key   TEXT PRIMARY KEY,"
            " value TEXT)"))) {
        if (errorOut) *errorOut = q.lastError().text();
        return false;
    }
    return true;
}

bool NotesTodos::migrateSchema(int fromVersion, QString *errorOut) {
    // No v2 yet. This switch-on-version pattern is the seam future
    // migrations will hook into. Each case must be idempotent so a
    // crash mid-migration leaves the DB in a recoverable state.
    int v = fromVersion;
    while (v < kCurrentSchemaVersion) {
        switch (v) {
        // case 1:
        //     // v1 → v2 migration here.
        //     v = 2;
        //     break;
        default:
            // Nothing to do for an unknown / future version. Don't
            // touch existing data; the read-side queries will simply
            // see columns they understand.
            v = kCurrentSchemaVersion;
            break;
        }
    }
    QSqlQuery q(m_db);
    q.prepare(QStringLiteral(
        "UPDATE schema_version SET version = ?"));
    q.addBindValue(kCurrentSchemaVersion);
    if (!q.exec()) {
        if (errorOut) *errorOut = q.lastError().text();
        return false;
    }
    return true;
}

int NotesTodos::currentSchemaVersion() const {
    QSqlQuery q(m_db);
    if (!q.exec(QStringLiteral(
            "SELECT version FROM schema_version LIMIT 1"))) {
        return 0;
    }
    if (!q.next()) return 0;
    return q.value(0).toInt();
}

// ─── ISO-8601 plumbing ─────────────────────────────────────────────────

QString NotesTodos::toIso(const QDateTime &dt) {
    if (!dt.isValid()) return QString();
    // Always store as UTC so cross-timezone sync works and text sort =
    // chronological sort. The "Z" suffix is explicit; without it Qt's
    // Qt::ISODate format leaves the timezone implicit.
    return dt.toUTC().toString(Qt::ISODate);
}

QDateTime NotesTodos::fromIso(const QString &s) {
    if (s.isEmpty()) return QDateTime();
    QDateTime dt = QDateTime::fromString(s, Qt::ISODate);
    if (dt.isValid()) {
        dt.setTimeSpec(Qt::UTC);
        return dt;
    }
    return parseIsoFlex(s);
}

QDateTime NotesTodos::parseIsoFlex(const QString &s) {
    // The HTML attribute payload may omit seconds ("2026-05-23T17:00"),
    // include a Z, or include +HH:MM. Try the common shapes before
    // giving up.
    static const char *formats[] = {
        "yyyy-MM-ddTHH:mm:ss",
        "yyyy-MM-ddTHH:mm",
        "yyyy-MM-dd HH:mm:ss",
        "yyyy-MM-dd HH:mm",
        "yyyy-MM-dd",
    };
    for (const char *fmt : formats) {
        QDateTime dt = QDateTime::fromString(s, QString::fromLatin1(fmt));
        if (dt.isValid()) {
            // The user typed local-time strings in the HTML — convert
            // to UTC for storage.
            dt.setTimeSpec(Qt::LocalTime);
            return dt.toUTC();
        }
    }
    // Last try: Qt's Qt::ISODateWithMs (Qt5 supports it).
    QDateTime dt = QDateTime::fromString(s, Qt::ISODateWithMs);
    if (dt.isValid()) {
        if (dt.timeSpec() == Qt::LocalTime) dt.setTimeSpec(Qt::UTC);
        return dt.toUTC();
    }
    return QDateTime();
}

// ─── HTML parsing ──────────────────────────────────────────────────────

QString NotesTodos::stripHtml(const QString &fragment) {
    // Strip tags, collapse whitespace. Good enough for the body of an
    // action block — we're not trying to round-trip rich text, just
    // produce a readable string for the Todos panel.
    static const QRegularExpression kTag(QStringLiteral("<[^>]*>"));
    static const QRegularExpression kWs(QStringLiteral("\\s+"));
    QString out = fragment;
    out.replace(kTag, QStringLiteral(" "));
    // Common HTML entities. Anything more exotic stays raw; the slide-
    // over UI does its own escaping.
    out.replace(QLatin1String("&nbsp;"), QLatin1String(" "));
    out.replace(QLatin1String("&amp;"),  QLatin1String("&"));
    out.replace(QLatin1String("&lt;"),   QLatin1String("<"));
    out.replace(QLatin1String("&gt;"),   QLatin1String(">"));
    out.replace(QLatin1String("&quot;"), QLatin1String("\""));
    out.replace(QLatin1String("&#39;"),  QLatin1String("'"));
    out.replace(kWs, QStringLiteral(" "));
    return out.trimmed();
}

QString NotesTodos::parseMeetingTitle(const QString &html) {
    // Title-identity SSOT — the notepatra-title head meta wins when
    // present (it is rewritten on every save/rename, while the body H1
    // can go stale). Small local regex + decode on purpose: this file
    // must not grow a NotesStorage dependency (its test target links
    // neither). Decode order: &amp; LAST so "&amp;lt;" → literal "&lt;".
    static const QRegularExpression kTitleMeta(
        QStringLiteral("<meta\\s+name=\"notepatra-title\"\\s+content=(['\"])(.*?)\\1"),
        QRegularExpression::CaseInsensitiveOption
            | QRegularExpression::DotMatchesEverythingOption);
    QRegularExpressionMatch m = kTitleMeta.match(html);
    if (m.hasMatch()) {
        QString t = m.captured(2);
        t.replace(QLatin1String("&lt;"),   QLatin1String("<"));
        t.replace(QLatin1String("&gt;"),   QLatin1String(">"));
        t.replace(QLatin1String("&quot;"), QLatin1String("\""));
        t.replace(QLatin1String("&#39;"),  QLatin1String("'"));
        t.replace(QLatin1String("&amp;"),  QLatin1String("&"));
        t = t.simplified();
        if (!t.isEmpty()) return t;
    }
    static const QRegularExpression kMeet(
        QStringLiteral("<h1[^>]*class=\"[^\"]*\\bmeet-title\\b[^\"]*\"[^>]*>(.*?)</h1>"),
        QRegularExpression::CaseInsensitiveOption
            | QRegularExpression::DotMatchesEverythingOption);
    m = kMeet.match(html);
    if (m.hasMatch()) return stripHtml(m.captured(1));
    // Fallback: <title>...</title>.
    static const QRegularExpression kTitle(
        QStringLiteral("<title[^>]*>(.*?)</title>"),
        QRegularExpression::CaseInsensitiveOption
            | QRegularExpression::DotMatchesEverythingOption);
    m = kTitle.match(html);
    if (m.hasMatch()) return stripHtml(m.captured(1));
    return QString();
}

QVector<NotesTodos::ParsedAction>
NotesTodos::parseActionBlocks(const QString &html) {
    QVector<ParsedAction> out;
    // We match opening div tags whose class CONTAINS "b-act" (allowing
    // other classes alongside, in any order). Attribute order is not
    // guaranteed so each attribute is fetched independently from the
    // captured tag string.
    static const QRegularExpression kOpen(
        QStringLiteral("<div\\b([^>]*\\bclass\\s*=\\s*\"[^\"]*\\bb-act\\b[^\"]*\"[^>]*)>"),
        QRegularExpression::CaseInsensitiveOption);
    static const QRegularExpression kAttr(
        QStringLiteral("\\b([\\w-]+)\\s*=\\s*\"([^\"]*)\""));

    QRegularExpressionMatchIterator it = kOpen.globalMatch(html);
    while (it.hasNext()) {
        QRegularExpressionMatch m = it.next();
        const QString attrs = m.captured(1);
        const int bodyStart = m.capturedEnd();

        // Body — find the matching </div>. Action blocks are not
        // expected to contain nested <div>s (the spec says paragraph
        // / span content); even if they do, the first </div> still
        // closes the action block under the simple "no nested divs"
        // rule the writer enforces.
        const int bodyEnd = html.indexOf(QStringLiteral("</div>"),
                                         bodyStart, Qt::CaseInsensitive);
        if (bodyEnd < 0) continue;  // malformed — skip

        ParsedAction pa;
        QRegularExpressionMatchIterator ai = kAttr.globalMatch(attrs);
        while (ai.hasNext()) {
            QRegularExpressionMatch am = ai.next();
            const QString k = am.captured(1).toLower();
            const QString v = am.captured(2);
            if      (k == QLatin1String("data-id"))     pa.id    = v;
            else if (k == QLatin1String("data-owner"))  pa.owner = v;
            else if (k == QLatin1String("data-due"))    pa.due   = v;
            else if (k == QLatin1String("data-status")) pa.status = v;
        }
        if (pa.id.isEmpty()) continue;  // no key, can't track it
        pa.text = stripHtml(html.mid(bodyStart, bodyEnd - bodyStart));
        out.append(pa);
    }
    return out;
}

// ─── reindex / purge ──────────────────────────────────────────────────

void NotesTodos::reindexNote(const QString &absolutePath, const QString &html) {
    if (!ensureOpen()) return;

    const QString meetingTitle = parseMeetingTitle(html);
    const QVector<ParsedAction> actions = parseActionBlocks(html);
    const QString nowIso = toIso(QDateTime::currentDateTimeUtc());

    m_db.transaction();

    // 1) Collect existing ids for this file. We use the set to compute
    //    the orphan diff after we've upserted everything in the
    //    incoming HTML.
    QSet<QString> existingIds;
    {
        QSqlQuery q(m_db);
        // v0.1.98 — exclude ALL synthetic reminder rows (block ids that start
        // with "__": the note-level "__note_reminder__" sentinel AND the
        // per-action "__reminder__:{uuid}" rows from Extract). They have no
        // HTML action block, so the orphan sweep below must never flag them
        // source_file_missing / delete them (which would otherwise happen on
        // every save of a note that carries one or more reminders). HTML
        // action data-ids are bare UUIDs, never "__"-prefixed.
        q.prepare(QStringLiteral(
            "SELECT id FROM todos WHERE source_file = ?"
            " AND source_block_id NOT LIKE '\\_\\_%' ESCAPE '\\'"));
        q.addBindValue(absolutePath);
        if (q.exec()) {
            while (q.next()) existingIds.insert(q.value(0).toString());
        }
    }

    // 2) Upsert each parsed block.
    for (const ParsedAction &pa : actions) {
        // Two-phase upsert: try UPDATE first (preserves reminder_* /
        // snooze_until / completed_at / created_at on re-indexed rows),
        // INSERT if no row was affected.
        {
            QSqlQuery q(m_db);
            q.prepare(QStringLiteral(
                "UPDATE todos SET"
                " source_file = ?, source_block_id = ?, text = ?,"
                " owner = ?, due_at = ?, status = ?,"
                " meeting_title = ?, source_file_missing = 0"
                " WHERE id = ?"));
            q.addBindValue(absolutePath);
            q.addBindValue(pa.id);
            q.addBindValue(pa.text);
            q.addBindValue(pa.owner.isEmpty() ? QVariant() : QVariant(pa.owner));
            const QDateTime due = parseIsoFlex(pa.due);
            q.addBindValue(due.isValid() ? QVariant(toIso(due)) : QVariant());
            q.addBindValue(normaliseStatus(pa.status));
            q.addBindValue(meetingTitle.isEmpty()
                ? QVariant() : QVariant(meetingTitle));
            q.addBindValue(pa.id);
            q.exec();
            if (q.numRowsAffected() > 0) {
                existingIds.remove(pa.id);
                emit todoChanged(pa.id);
                continue;
            }
        }
        // INSERT path.
        QSqlQuery q(m_db);
        q.prepare(QStringLiteral(
            "INSERT INTO todos("
            " id, source_file, source_block_id, text, owner, due_at,"
            " status, reminder_status, meeting_title, created_at,"
            " source_file_missing)"
            " VALUES(?,?,?,?,?,?,?,?,?,?,0)"));
        q.addBindValue(pa.id);
        q.addBindValue(absolutePath);
        q.addBindValue(pa.id);  // source_block_id mirrors id today
        q.addBindValue(pa.text);
        q.addBindValue(pa.owner.isEmpty() ? QVariant() : QVariant(pa.owner));
        const QDateTime due = parseIsoFlex(pa.due);
        q.addBindValue(due.isValid() ? QVariant(toIso(due)) : QVariant());
        q.addBindValue(normaliseStatus(pa.status));
        q.addBindValue(QLatin1String(kReminderNone));
        q.addBindValue(meetingTitle.isEmpty()
            ? QVariant() : QVariant(meetingTitle));
        q.addBindValue(nowIso);
        if (q.exec()) emit todoChanged(pa.id);
        existingIds.remove(pa.id);
    }

    // 3) Whatever ids remain in existingIds are no longer in the file
    //    — flag them. We preserve the row (status / reminder / etc.)
    //    so the UI can still show "you marked this done last week,
    //    then someone deleted the block from the note".
    if (!existingIds.isEmpty()) {
        QSqlQuery q(m_db);
        q.prepare(QStringLiteral(
            "UPDATE todos SET source_file_missing = 1 WHERE id = ?"));
        for (const QString &orphanId : existingIds) {
            q.addBindValue(orphanId);
            q.exec();
            emit todoChanged(orphanId);
        }
    }

    m_db.commit();
}

void NotesTodos::purgeNote(const QString &absolutePath) {
    if (!ensureOpen()) return;
    QSqlQuery q(m_db);
    q.prepare(QStringLiteral(
        "DELETE FROM todos WHERE source_file = ?"));
    q.addBindValue(absolutePath);
    q.exec();
}

void NotesTodos::repathNote(const QString &oldPath, const QString &newPath) {
    if (!ensureOpen() || oldPath == newPath || oldPath.isEmpty()) return;
    QSqlQuery q(m_db);
    q.prepare(QStringLiteral(
        "UPDATE todos SET source_file = ? WHERE source_file = ?"));
    q.addBindValue(newPath);
    q.addBindValue(oldPath);
    q.exec();
}

// ─── read-side queries ─────────────────────────────────────────────────

TodoRow NotesTodos::rowFromQuery(QSqlQuery &q) {
    TodoRow r;
    // Pull by column NAME via QSqlRecord so the column order in the
    // SELECT clause doesn't have to match a magic index list. Slightly
    // slower but vastly less bug-prone when adding columns.
    QSqlRecord rec = q.record();
    auto v = [&](const char *name) {
        const int idx = rec.indexOf(QString::fromLatin1(name));
        return idx < 0 ? QVariant() : q.value(idx);
    };
    r.id                = v("id").toString();
    r.sourceFile        = v("source_file").toString();
    r.sourceBlockId     = v("source_block_id").toString();
    r.text              = v("text").toString();
    r.owner             = v("owner").toString();
    r.dueAt             = fromIso(v("due_at").toString());
    r.status            = v("status").toString();
    r.reminderAt        = fromIso(v("reminder_at").toString());
    r.reminderStatus    = v("reminder_status").toString();
    r.snoozeUntil       = fromIso(v("snooze_until").toString());
    r.recurrence        = v("recurrence").toString();
    r.meetingTitle      = v("meeting_title").toString();
    r.createdAt         = fromIso(v("created_at").toString());
    r.completedAt       = fromIso(v("completed_at").toString());
    r.sourceFileMissing = v("source_file_missing").toInt() != 0;
    return r;
}

static const char *kSelectAll =
    "SELECT id, source_file, source_block_id, text, owner, due_at,"
    " status, reminder_at, reminder_status, snooze_until, recurrence,"
    " meeting_title, created_at, completed_at, source_file_missing"
    " FROM todos";

QVector<TodoRow> NotesTodos::dueGroupOverdue(const QDateTime &now) const {
    QVector<TodoRow> out;
    if (!ensureOpen()) return out;
    QSqlQuery q(m_db);
    q.prepare(QString::fromLatin1(kSelectAll)
        + QStringLiteral(" WHERE due_at IS NOT NULL AND due_at < ?"
                          " AND status = 'open'"
                          " ORDER BY due_at ASC"));
    q.addBindValue(toIso(now));
    if (!q.exec()) return out;
    while (q.next()) out.append(rowFromQuery(q));
    return out;
}

QVector<TodoRow> NotesTodos::dueGroupToday(const QDateTime &now) const {
    QVector<TodoRow> out;
    if (!ensureOpen()) return out;
    // "Today" boundaries are in LOCAL time (that's what the user means
    // when they say "today"). Convert local-midnight → UTC for the SQL
    // string comparison.
    const QDateTime local = now.toLocalTime();
    QDateTime startLocal(local.date(), QTime(0, 0, 0), Qt::LocalTime);
    QDateTime endLocal(local.date(), QTime(23, 59, 59, 999), Qt::LocalTime);
    QSqlQuery q(m_db);
    q.prepare(QString::fromLatin1(kSelectAll)
        + QStringLiteral(" WHERE due_at IS NOT NULL"
                          " AND due_at >= ? AND due_at <= ?"
                          " AND status = 'open'"
                          " ORDER BY due_at ASC"));
    q.addBindValue(toIso(startLocal.toUTC()));
    q.addBindValue(toIso(endLocal.toUTC()));
    if (!q.exec()) return out;
    while (q.next()) out.append(rowFromQuery(q));
    return out;
}

QVector<TodoRow> NotesTodos::dueGroupWeek(const QDateTime &now) const {
    QVector<TodoRow> out;
    if (!ensureOpen()) return out;
    // "Next 7 days" starts AFTER today's end so the Today group and
    // Week group don't double-count tomorrow-evening reminders.
    const QDateTime local = now.toLocalTime();
    QDateTime startLocal(local.date().addDays(1), QTime(0, 0, 0),
                         Qt::LocalTime);
    QDateTime endLocal(local.date().addDays(7), QTime(23, 59, 59, 999),
                       Qt::LocalTime);
    QSqlQuery q(m_db);
    q.prepare(QString::fromLatin1(kSelectAll)
        + QStringLiteral(" WHERE due_at IS NOT NULL"
                          " AND due_at >= ? AND due_at <= ?"
                          " AND status = 'open'"
                          " ORDER BY due_at ASC"));
    q.addBindValue(toIso(startLocal.toUTC()));
    q.addBindValue(toIso(endLocal.toUTC()));
    if (!q.exec()) return out;
    while (q.next()) out.append(rowFromQuery(q));
    return out;
}

QVector<TodoRow> NotesTodos::dueGroupSomeday(const QDateTime &now) const {
    QVector<TodoRow> out;
    if (!ensureOpen()) return out;
    // Either no due date, or due date is more than 30 days out from now.
    const QDateTime cutoff = now.toUTC().addDays(30);
    QSqlQuery q(m_db);
    q.prepare(QString::fromLatin1(kSelectAll)
        + QStringLiteral(" WHERE status = 'open'"
                          " AND (due_at IS NULL OR due_at > ?)"
                          " ORDER BY created_at DESC"));
    q.addBindValue(toIso(cutoff));
    if (!q.exec()) return out;
    while (q.next()) out.append(rowFromQuery(q));
    return out;
}

QVector<TodoRow> NotesTodos::dueGroupDone(int limit) const {
    QVector<TodoRow> out;
    if (!ensureOpen()) return out;
    if (limit <= 0) limit = 50;
    QSqlQuery q(m_db);
    q.prepare(QString::fromLatin1(kSelectAll)
        + QStringLiteral(" WHERE status = 'done'"
                          " ORDER BY completed_at DESC LIMIT ?"));
    q.addBindValue(limit);
    if (!q.exec()) return out;
    while (q.next()) out.append(rowFromQuery(q));
    return out;
}

QVector<TodoRow> NotesTodos::dueGroupTrashed(int limit) const {
    QVector<TodoRow> out;
    if (!ensureOpen()) return out;
    if (limit <= 0) limit = 100;
    QSqlQuery q(m_db);
    q.prepare(QString::fromLatin1(kSelectAll)
        + QStringLiteral(" WHERE status = 'trashed'"
                          " ORDER BY created_at DESC LIMIT ?"));
    q.addBindValue(limit);
    if (!q.exec()) return out;
    while (q.next()) out.append(rowFromQuery(q));
    return out;
}

QVector<TodoRow>
NotesTodos::remindersReadyAt(const QDateTime &t) const {
    QVector<TodoRow> out;
    if (!ensureOpen()) return out;
    QSqlQuery q(m_db);
    q.prepare(QString::fromLatin1(kSelectAll)
        + QStringLiteral(" WHERE reminder_at IS NOT NULL"
                          " AND reminder_at <= ?"
                          " AND reminder_status = 'scheduled'"
                          " AND status = 'open'"
                          " ORDER BY reminder_at ASC"));
    q.addBindValue(toIso(t));
    if (!q.exec()) return out;
    while (q.next()) out.append(rowFromQuery(q));
    return out;
}

QVector<TodoRow>
NotesTodos::remindersMissedSince(const QDateTime &lastTick) const {
    QVector<TodoRow> out;
    if (!ensureOpen()) return out;
    const QDateTime now = QDateTime::currentDateTimeUtc();
    QSqlQuery q(m_db);
    q.prepare(QString::fromLatin1(kSelectAll)
        + QStringLiteral(" WHERE reminder_at IS NOT NULL"
                          " AND reminder_at >= ? AND reminder_at <= ?"
                          " AND reminder_status = 'scheduled'"
                          " AND status = 'open'"
                          " ORDER BY reminder_at ASC"));
    q.addBindValue(toIso(lastTick));
    q.addBindValue(toIso(now));
    if (!q.exec()) return out;
    while (q.next()) out.append(rowFromQuery(q));
    return out;
}

TodoRow NotesTodos::find(const QString &id) const {
    TodoRow r;
    if (!ensureOpen()) return r;
    QSqlQuery q(m_db);
    q.prepare(QString::fromLatin1(kSelectAll)
        + QStringLiteral(" WHERE id = ? LIMIT 1"));
    q.addBindValue(id);
    if (!q.exec()) return r;
    if (q.next()) r = rowFromQuery(q);
    return r;
}

// ─── mutations ─────────────────────────────────────────────────────────

bool NotesTodos::markDone(const QString &id, const QDateTime &when) {
    if (!ensureOpen()) return false;
    QSqlQuery q(m_db);
    q.prepare(QStringLiteral(
        "UPDATE todos SET status = 'done', completed_at = ?,"
        " reminder_status = CASE reminder_status"
        "   WHEN 'scheduled' THEN 'dismissed'"
        "   ELSE reminder_status END"
        " WHERE id = ?"));
    q.addBindValue(toIso(when.isValid() ? when : QDateTime::currentDateTimeUtc()));
    q.addBindValue(id);
    if (!q.exec()) return false;
    emit todoChanged(id);
    return true;
}

bool NotesTodos::markOpen(const QString &id) {
    if (!ensureOpen()) return false;
    QSqlQuery q(m_db);
    q.prepare(QStringLiteral(
        "UPDATE todos SET status = 'open', completed_at = NULL"
        " WHERE id = ?"));
    q.addBindValue(id);
    if (!q.exec()) return false;
    emit todoChanged(id);
    return true;
}

bool NotesTodos::setDue(const QString &id, const QDateTime &dueAt) {
    if (!ensureOpen()) return false;
    QSqlQuery q(m_db);
    q.prepare(QStringLiteral(
        "UPDATE todos SET due_at = ? WHERE id = ?"));
    q.addBindValue(dueAt.isValid() ? QVariant(toIso(dueAt)) : QVariant());
    q.addBindValue(id);
    if (!q.exec()) return false;
    emit todoChanged(id);
    return true;
}

bool NotesTodos::trashRow(const QString &id) {
    if (id.isEmpty() || !ensureOpen()) return false;
    QSqlQuery q(m_db);
    q.prepare(QStringLiteral(
        "UPDATE todos SET status = 'trashed',"
        " reminder_status = CASE reminder_status"
        "   WHEN 'scheduled' THEN 'dismissed'"
        "   ELSE reminder_status END"
        " WHERE id = ?"));
    q.addBindValue(id);
    if (!q.exec()) return false;
    if (q.numRowsAffected() <= 0) return false;
    emit todoChanged(id);
    return true;
}

bool NotesTodos::restoreRow(const QString &id) {
    if (id.isEmpty() || !ensureOpen()) return false;
    QSqlQuery q(m_db);
    q.prepare(QStringLiteral(
        "UPDATE todos SET status = 'open' WHERE id = ?"));
    q.addBindValue(id);
    if (!q.exec()) return false;
    if (q.numRowsAffected() <= 0) return false;
    emit todoChanged(id);
    return true;
}

bool NotesTodos::deleteRow(const QString &id) {
    if (id.isEmpty() || !ensureOpen()) return false;
    QSqlQuery q(m_db);
    q.prepare(QStringLiteral("DELETE FROM todos WHERE id = ?"));
    q.addBindValue(id);
    if (!q.exec()) return false;
    if (q.numRowsAffected() <= 0) return false;
    emit todoChanged(id);
    return true;
}

bool NotesTodos::setText(const QString &id, const QString &text) {
    // v0.1.94 — back-end of inline-editable Todos rows. Trim whitespace,
    // reject empty (a row with no text is a deletion, not an edit). Caller
    // (NotesPanel) handles the editor-body replacement so the next
    // reindexNote() doesn't clobber the edit.
    if (id.isEmpty()) return false;
    if (!ensureOpen()) return false;
    const QString trimmed = text.trimmed();
    if (trimmed.isEmpty()) return false;
    QSqlQuery q(m_db);
    q.prepare(QStringLiteral(
        "UPDATE todos SET text = ? WHERE id = ?"));
    q.addBindValue(trimmed);
    q.addBindValue(id);
    if (!q.exec()) return false;
    if (q.numRowsAffected() <= 0) return false;
    emit todoChanged(id);
    return true;
}

bool NotesTodos::setReminder(const QString &id, const QDateTime &remindAt) {
    if (!ensureOpen()) return false;
    QSqlQuery q(m_db);
    q.prepare(QStringLiteral(
        "UPDATE todos SET reminder_at = ?, reminder_status = ?"
        " WHERE id = ?"));
    if (remindAt.isValid()) {
        q.addBindValue(toIso(remindAt));
        q.addBindValue(QLatin1String(kReminderScheduled));
    } else {
        q.addBindValue(QVariant());
        q.addBindValue(QLatin1String(kReminderNone));
    }
    q.addBindValue(id);
    if (!q.exec()) return false;
    emit todoChanged(id);
    return true;
}

QString NotesTodos::setNoteReminder(const QString &sourceFile,
                                    const QString &title,
                                    const QDateTime &remindAt) {
    if (!ensureOpen() || sourceFile.isEmpty()) return QString();

    // Is there already a note-reminder row for this file?
    QString id;
    {
        QSqlQuery q(m_db);
        q.prepare(QStringLiteral(
            "SELECT id FROM todos WHERE source_file = ?"
            " AND source_block_id = ? LIMIT 1"));
        q.addBindValue(sourceFile);
        q.addBindValue(QLatin1String(kNoteReminderBlock));
        if (q.exec() && q.next()) id = q.value(0).toString();
    }

    // Clear → drop the row (a note with no reminder needs no row).
    if (!remindAt.isValid()) {
        if (!id.isEmpty()) {
            QSqlQuery q(m_db);
            q.prepare(QStringLiteral("DELETE FROM todos WHERE id = ?"));
            q.addBindValue(id);
            q.exec();
            emit todoChanged(id);
        }
        return id;
    }

    const QString iso = toIso(remindAt);
    const QString safeTitle = title.isEmpty() ? sourceFile : title;
    if (id.isEmpty()) {
        id = QUuid::createUuid().toString(QUuid::WithoutBraces).toLower();
        QSqlQuery q(m_db);
        q.prepare(QStringLiteral(
            "INSERT INTO todos("
            " id, source_file, source_block_id, text, status,"
            " reminder_at, reminder_status, meeting_title, created_at,"
            " source_file_missing)"
            " VALUES(?,?,?,?,'open',?,'scheduled',?,?,0)"));
        q.addBindValue(id);
        q.addBindValue(sourceFile);
        q.addBindValue(QLatin1String(kNoteReminderBlock));
        q.addBindValue(safeTitle);
        q.addBindValue(iso);
        q.addBindValue(safeTitle);
        q.addBindValue(toIso(QDateTime::currentDateTimeUtc()));
        if (!q.exec()) return QString();
    } else {
        QSqlQuery q(m_db);
        q.prepare(QStringLiteral(
            "UPDATE todos SET text = ?, meeting_title = ?, reminder_at = ?,"
            " reminder_status = 'scheduled', status = 'open',"
            " source_file_missing = 0 WHERE id = ?"));
        q.addBindValue(safeTitle);
        q.addBindValue(safeTitle);
        q.addBindValue(iso);
        q.addBindValue(id);
        if (!q.exec()) return QString();
    }
    emit todoChanged(id);
    return id;
}

QDateTime NotesTodos::noteReminderAt(const QString &sourceFile) const {
    if (!ensureOpen() || sourceFile.isEmpty()) return QDateTime();
    QSqlQuery q(m_db);
    q.prepare(QStringLiteral(
        "SELECT reminder_at FROM todos WHERE source_file = ?"
        " AND source_block_id = ? AND reminder_status = ? LIMIT 1"));
    q.addBindValue(sourceFile);
    q.addBindValue(QLatin1String(kNoteReminderBlock));
    q.addBindValue(QLatin1String(kReminderScheduled));
    if (q.exec() && q.next()) return fromIso(q.value(0).toString());
    return QDateTime();
}

QString NotesTodos::addReminder(const QString &sourceFile, const QString &title,
                                const QDateTime &remindAt) {
    if (!ensureOpen() || sourceFile.isEmpty() || !remindAt.isValid())
        return QString();
    const QString id = QUuid::createUuid().toString(QUuid::WithoutBraces).toLower();
    // "__"-prefixed so reindexNote's orphan sweep skips it; suffixed with the
    // row id so multiple action reminders can coexist on one note file.
    const QString blockId = QStringLiteral("__reminder__:") + id;
    const QString iso = toIso(remindAt);
    const QString safeTitle = title.isEmpty() ? sourceFile : title;
    QSqlQuery q(m_db);
    q.prepare(QStringLiteral(
        "INSERT INTO todos("
        " id, source_file, source_block_id, text, status,"
        " due_at, reminder_at, reminder_status, meeting_title, created_at,"
        " source_file_missing)"
        " VALUES(?,?,?,?,'open',?,?,'scheduled',?,?,0)"));
    q.addBindValue(id);
    q.addBindValue(sourceFile);
    q.addBindValue(blockId);
    q.addBindValue(safeTitle);
    q.addBindValue(iso);   // due_at
    q.addBindValue(iso);   // reminder_at
    q.addBindValue(safeTitle);
    q.addBindValue(toIso(QDateTime::currentDateTimeUtc()));
    if (!q.exec()) return QString();
    emit todoChanged(id);
    return id;
}

QVector<TodoRow> NotesTodos::allScheduledReminders() const {
    QVector<TodoRow> out;
    if (!ensureOpen()) return out;
    QSqlQuery q(m_db);
    // Includes overdue (reminder_at in the past) so a missed reminder still
    // shows in the central view until the user acts on it. Dismissed / fired /
    // snoozed rows are intentionally excluded — only "scheduled" is live.
    q.prepare(QString::fromLatin1(kSelectAll)
        + QStringLiteral(" WHERE reminder_status = 'scheduled'"
                          " AND reminder_at IS NOT NULL"
                          " AND status = 'open'"
                          " ORDER BY reminder_at ASC"));
    if (!q.exec()) return out;
    while (q.next()) out.append(rowFromQuery(q));
    return out;
}

bool NotesTodos::snooze(const QString &id, const QDateTime &until) {
    if (!ensureOpen()) return false;
    if (!until.isValid()) return false;
    QSqlQuery q(m_db);
    q.prepare(QStringLiteral(
        "UPDATE todos SET snooze_until = ?,"
        " reminder_at = ?, reminder_status = 'scheduled'"
        " WHERE id = ?"));
    const QString iso = toIso(until);
    q.addBindValue(iso);
    q.addBindValue(iso);
    q.addBindValue(id);
    if (!q.exec()) return false;
    emit todoChanged(id);
    return true;
}

bool NotesTodos::dismissReminder(const QString &id) {
    if (!ensureOpen()) return false;
    QSqlQuery q(m_db);
    q.prepare(QStringLiteral(
        "UPDATE todos SET reminder_status = 'dismissed' WHERE id = ?"));
    q.addBindValue(id);
    if (!q.exec()) return false;
    emit todoChanged(id);
    return true;
}

bool NotesTodos::markReminderFired(const QString &id) {
    if (!ensureOpen()) return false;
    QSqlQuery q(m_db);
    q.prepare(QStringLiteral(
        "UPDATE todos SET reminder_status = 'fired' WHERE id = ?"));
    q.addBindValue(id);
    if (!q.exec()) return false;
    emit todoChanged(id);
    return true;
}

QDateTime NotesTodos::lastReminderTickAt() const {
    if (!ensureOpen()) return QDateTime();
    QSqlQuery q(m_db);
    q.prepare(QStringLiteral("SELECT value FROM meta WHERE key='last_tick_at'"));
    if (!q.exec() || !q.next()) return QDateTime();
    return fromIso(q.value(0).toString());
}

bool NotesTodos::setLastReminderTickAt(const QDateTime &t) {
    if (!ensureOpen() || !t.isValid()) return false;
    QSqlQuery q(m_db);
    q.prepare(QStringLiteral(
        "INSERT OR REPLACE INTO meta(key,value) VALUES('last_tick_at',?)"));
    q.addBindValue(toIso(t.toUTC()));
    return q.exec();   // bookkeeping — deliberately NO todoChanged emit
}

// ─── rebuild from disk ─────────────────────────────────────────────────

bool NotesTodos::rebuildFromHtmlFiles(const QString &notesRoot,
                                      QString *errorOut) {
    if (!ensureOpen()) {
        if (errorOut) *errorOut = QStringLiteral("database not open");
        return false;
    }
    if (!QFileInfo(notesRoot).isDir()) {
        if (errorOut) *errorOut = QStringLiteral("notes root not a directory: %1")
            .arg(notesRoot);
        return false;
    }

    m_db.transaction();
    {
        QSqlQuery q(m_db);
        // Wipe rows that came from .html files; quick todos in Inbox
        // are themselves .html files so they get re-picked-up by the
        // walk below.
        if (!q.exec(QStringLiteral("DELETE FROM todos"))) {
            if (errorOut) *errorOut = q.lastError().text();
            m_db.rollback();
            return false;
        }
    }
    m_db.commit();

    QDirIterator it(notesRoot,
                    QStringList() << QStringLiteral("*.html")
                                  << QStringLiteral("*.htm"),
                    QDir::Files | QDir::NoDotAndDotDot,
                    QDirIterator::Subdirectories);
    while (it.hasNext()) {
        const QString path = it.next();
        QFile f(path);
        if (!f.open(QIODevice::ReadOnly | QIODevice::Text)) continue;
        const QString html = QString::fromUtf8(f.readAll());
        reindexNote(path, html);
    }
    return true;
}

// ─── quick todo ────────────────────────────────────────────────────────

QString NotesTodos::addQuickTodo(const QString &text, const QString &owner,
                                 const QDateTime &dueAt) {
    if (!ensureOpen()) return QString();
    if (text.trimmed().isEmpty()) return QString();

    // The Inbox file holds standalone todos that aren't attached to any
    // meeting. We figure out the notes root from the SQLite path:
    //   <notesRoot>/.notepatra/todos.db   (typical layout)
    //   <notesRoot>/todos.db              (test layout)
    // — fall back to the parent dir of the parent dir, then the parent
    // dir itself. NotesPanel can override by setting notesRoot first
    // via reindex; for v1 we keep this convention-over-configuration.
    QFileInfo dbInfo(m_dbPath);
    QString notesRoot = dbInfo.absoluteDir().absolutePath();
    if (dbInfo.absoluteDir().dirName() == QLatin1String(".notepatra")) {
        notesRoot = dbInfo.absoluteDir().absolutePath();
        QDir up(notesRoot);
        up.cdUp();
        notesRoot = up.absolutePath();
    }

    const QString id = QUuid::createUuid()
        .toString(QUuid::WithoutBraces).toLower();
    const QDateTime now = QDateTime::currentDateTimeUtc();
    const QString meetingTitle = QStringLiteral("Inbox");

    // Build the block. Use Qt::ISODate for the data-due attribute so
    // the HTML stays parseable on re-index. Status defaults to "open".
    QString dueAttr;
    if (dueAt.isValid()) {
        dueAttr = QStringLiteral(" data-due=\"%1\"")
            .arg(dueAt.toUTC().toString(Qt::ISODate));
    }
    QString ownerAttr;
    if (!owner.isEmpty()) {
        // Minimal escaping for the attribute payload — the owner is a
        // short handle like "@bob"; we still defensively replace " in
        // case it ever isn't.
        QString safe = owner;
        safe.replace(QLatin1Char('"'), QLatin1String("&quot;"));
        ownerAttr = QStringLiteral(" data-owner=\"%1\"").arg(safe);
    }
    // Escape body text for HTML so a "<" or "&" in the todo text
    // doesn't break the file.
    QString safeText = text;
    safeText.replace(QLatin1Char('&'), QLatin1String("&amp;"));
    safeText.replace(QLatin1Char('<'), QLatin1String("&lt;"));
    safeText.replace(QLatin1Char('>'), QLatin1String("&gt;"));

    const QString block = QStringLiteral(
        "<div class=\"b b-act\" data-id=\"%1\" data-status=\"open\"%2%3>%4</div>\n")
        .arg(id, ownerAttr, dueAttr, safeText);

    // ─ Write to <notesRoot>/Inbox/quick-todos.html ─
    const QString inboxDir = notesRoot + QLatin1Char('/')
        + QLatin1String("Inbox");
    if (!QDir().mkpath(inboxDir)) return QString();
    const QString filePath = inboxDir + QLatin1Char('/')
        + QLatin1String("quick-todos.html");
    QFile f(filePath);
    if (!f.exists()) {
        if (!f.open(QIODevice::WriteOnly | QIODevice::Text)) return QString();
        QTextStream ts(&f);
        ts << "<!doctype html>\n"
              "<html><head><meta charset=\"utf-8\">"
              "<title>Inbox</title></head><body>\n"
              "<h1 class=\"meet-title\">Inbox</h1>\n"
           << block
           << "</body></html>\n";
        f.close();
    } else {
        // Append the new block just before </body> if the marker
        // exists; otherwise append at the end. Round-trip is
        // intentionally lossy on whitespace — we don't try to preserve
        // the user's hand-formatted layout.
        if (!f.open(QIODevice::ReadWrite | QIODevice::Text)) return QString();
        QString existing = QString::fromUtf8(f.readAll());
        f.resize(0);
        const int idx = existing.lastIndexOf(QStringLiteral("</body>"),
                                             -1, Qt::CaseInsensitive);
        if (idx >= 0) {
            existing.insert(idx, block);
        } else {
            existing += block;
        }
        QTextStream ts(&f);
        ts << existing;
        f.close();
    }

    // ─ Insert SQLite row directly (skip a full reindex round-trip) ─
    QSqlQuery q(m_db);
    q.prepare(QStringLiteral(
        "INSERT INTO todos("
        " id, source_file, source_block_id, text, owner, due_at,"
        " status, reminder_status, meeting_title, created_at,"
        " source_file_missing)"
        " VALUES(?,?,?,?,?,?,'open','none',?,?,0)"));
    q.addBindValue(id);
    q.addBindValue(filePath);
    q.addBindValue(id);
    q.addBindValue(text.trimmed());
    q.addBindValue(owner.isEmpty() ? QVariant() : QVariant(owner));
    q.addBindValue(dueAt.isValid() ? QVariant(toIso(dueAt)) : QVariant());
    q.addBindValue(meetingTitle);
    q.addBindValue(toIso(now));
    if (!q.exec()) return QString();

    emit todoChanged(id);
    return id;
}
