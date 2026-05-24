// SPDX-License-Identifier: GPL-3.0-or-later

#include "ai_interaction_log.h"
#include "config.h"

#include <QDir>
#include <QFileInfo>
#include <QMutex>
#include <QMutexLocker>
#include <QSqlDatabase>
#include <QSqlError>
#include <QSqlQuery>
#include <QStandardPaths>
#include <QUuid>
#include <QRegularExpression>

namespace AiInteractionLog {

namespace {

// Single shared connection name. SQLite + QSqlDatabase is fine on the GUI
// thread for our write rate (~1 row per LLM turn). WAL mode lets readers
// (the dialog) coexist with writers without blocking each other.
constexpr const char *kConnName = "notepatra_ai_log";

QString g_sessionId;
QMutex  g_mutex;
bool    g_initialised = false;
bool    g_initFailed = false;

QString dbPathInternal() {
    // v0.1.96 — platform-conventional config dir. Avoids the Windows
    // %APPDATA%-vs-~/.config/notepatra inconsistency that left users
    // looking for the SQLite log in the wrong place.
    const QString dir = Config::appConfigDir() + QStringLiteral("/ai-logs");
    QDir().mkpath(dir);
    return dir + "/interactions.db";
}

// Strip obvious secrets before persisting. Conservative — false positives
// just blank out content, which is fine; false negatives would let a
// credential leak into the log file. Mirrors the pre-existing v0.1.55
// chat scrubber patterns.
QString scrub(const QString &in) {
    if (in.isEmpty()) return in;
    QString s = in;
    static const QRegularExpression kBearer(
        QStringLiteral("(?i)bearer\\s+[A-Za-z0-9._\\-]{12,}"));
    static const QRegularExpression kOpenAi(
        QStringLiteral("sk-[A-Za-z0-9_\\-]{20,}"));
    static const QRegularExpression kAnthropic(
        QStringLiteral("sk-ant-[A-Za-z0-9_\\-]{20,}"));
    static const QRegularExpression kGhPat(
        QStringLiteral("(ghp|ghs|gho|ghu|ghr)_[A-Za-z0-9]{30,}"));
    static const QRegularExpression kAwsAk(
        QStringLiteral("(?<![A-Z0-9])AKIA[0-9A-Z]{16}(?![A-Z0-9])"));
    static const QRegularExpression kGoogleApi(
        QStringLiteral("(?<![A-Za-z0-9])AIza[0-9A-Za-z\\-_]{30,}(?![A-Za-z0-9])"));
    static const QRegularExpression kPrivKey(
        QStringLiteral("-----BEGIN [A-Z ]*PRIVATE KEY-----[\\s\\S]*?"
                       "-----END [A-Z ]*PRIVATE KEY-----"));
    s.replace(kBearer,    QStringLiteral("[redacted-bearer]"));
    s.replace(kOpenAi,    QStringLiteral("[redacted-openai-key]"));
    s.replace(kAnthropic, QStringLiteral("[redacted-anthropic-key]"));
    s.replace(kGhPat,     QStringLiteral("[redacted-gh-token]"));
    s.replace(kAwsAk,     QStringLiteral("[redacted-aws-key]"));
    s.replace(kGoogleApi, QStringLiteral("[redacted-google-key]"));
    s.replace(kPrivKey,   QStringLiteral("[redacted-private-key-block]"));
    return s;
}

QString roleToStr(Role r) {
    switch (r) {
        case Role::User:        return "user";
        case Role::System:      return "system";
        case Role::Assistant:   return "assistant";
        case Role::ToolCall:    return "tool_call";
        case Role::ToolResult:  return "tool_result";
    }
    return "user";
}

Role roleFromStr(const QString &s) {
    if (s == "system")        return Role::System;
    if (s == "assistant")     return Role::Assistant;
    if (s == "tool_call")     return Role::ToolCall;
    if (s == "tool_result")   return Role::ToolResult;
    return Role::User;
}

QSqlDatabase openDb() {
    if (g_initFailed) return QSqlDatabase();
    QSqlDatabase db = QSqlDatabase::contains(kConnName)
        ? QSqlDatabase::database(kConnName, /*open=*/false)
        : QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), kConnName);
    if (!db.isOpen()) {
        db.setDatabaseName(dbPathInternal());
        if (!db.open()) {
            qWarning("[AiInteractionLog] open failed: %s",
                qPrintable(db.lastError().text()));
            g_initFailed = true;
            return QSqlDatabase();
        }
    }
    if (!g_initialised) {
        QSqlQuery q(db);
        q.exec(QStringLiteral("PRAGMA journal_mode=WAL"));
        q.exec(QStringLiteral("PRAGMA synchronous=NORMAL"));
        q.exec(QStringLiteral(
            "CREATE TABLE IF NOT EXISTS interactions ("
            " id INTEGER PRIMARY KEY AUTOINCREMENT,"
            " ts INTEGER NOT NULL,"
            " session_id TEXT NOT NULL,"
            " backend TEXT NOT NULL,"
            " model TEXT,"
            " mode TEXT,"
            " role TEXT NOT NULL,"
            " content TEXT,"
            " tool_name TEXT,"
            " tool_args TEXT,"
            " tool_result TEXT,"
            " prompt_tokens INTEGER,"
            " eval_tokens INTEGER,"
            " elapsed_ms INTEGER,"
            " error TEXT)"));
        q.exec(QStringLiteral(
            "CREATE INDEX IF NOT EXISTS idx_ts ON interactions(ts)"));
        q.exec(QStringLiteral(
            "CREATE INDEX IF NOT EXISTS idx_session ON interactions(session_id)"));
        g_initialised = true;
    }
    return db;
}

} // namespace

bool isEnabled() {
    return Config::instance().aiInteractionLogging;
}

void setEnabled(bool on) {
    Config::instance().aiInteractionLogging = on;
    Config::instance().save();
}

QString sessionId() {
    QMutexLocker lock(&g_mutex);
    if (g_sessionId.isEmpty()) {
        g_sessionId = QUuid::createUuid().toString(QUuid::WithoutBraces).left(12);
    }
    return g_sessionId;
}

QString databasePath() { return dbPathInternal(); }

void record(const Event &e_in) {
    if (!isEnabled()) return;
    QMutexLocker lock(&g_mutex);
    QSqlDatabase db = openDb();
    if (!db.isValid() || !db.isOpen()) return;

    Event e = e_in;
    if (e.ts == 0) e.ts = QDateTime::currentSecsSinceEpoch();
    if (e.sessionId.isEmpty()) {
        if (g_sessionId.isEmpty()) {
            g_sessionId = QUuid::createUuid().toString(QUuid::WithoutBraces).left(12);
        }
        e.sessionId = g_sessionId;
    }
    e.content    = scrub(e.content);
    e.toolArgs   = scrub(e.toolArgs);
    e.toolResult = scrub(e.toolResult);
    e.error      = scrub(e.error);
    e.toolName   = scrub(e.toolName);
    e.model      = scrub(e.model);

    QSqlQuery q(db);
    q.prepare(QStringLiteral(
        "INSERT INTO interactions("
        " ts, session_id, backend, model, mode, role, content,"
        " tool_name, tool_args, tool_result,"
        " prompt_tokens, eval_tokens, elapsed_ms, error)"
        " VALUES(?,?,?,?,?,?,?,?,?,?,?,?,?,?)"));
    q.addBindValue(e.ts);
    q.addBindValue(e.sessionId);
    q.addBindValue(e.backend);
    q.addBindValue(e.model);
    q.addBindValue(e.mode);
    q.addBindValue(roleToStr(e.role));
    q.addBindValue(e.content);
    q.addBindValue(e.toolName);
    q.addBindValue(e.toolArgs);
    q.addBindValue(e.toolResult);
    q.addBindValue(e.promptTokens);
    q.addBindValue(e.evalTokens);
    q.addBindValue(e.elapsedMs);
    q.addBindValue(e.error);
    if (!q.exec()) {
        qWarning("[AiInteractionLog] insert failed: %s",
            qPrintable(q.lastError().text()));
    }
}

void recordUser(const QString &backend, const QString &model,
                const QString &mode, const QString &content) {
    Event e;
    e.backend = backend; e.model = model; e.mode = mode;
    e.role = Role::User; e.content = content;
    record(e);
}

void recordAssistant(const QString &backend, const QString &model,
                     const QString &mode, const QString &content,
                     int promptTokens, int evalTokens, int elapsedMs) {
    Event e;
    e.backend = backend; e.model = model; e.mode = mode;
    e.role = Role::Assistant; e.content = content;
    e.promptTokens = promptTokens; e.evalTokens = evalTokens;
    e.elapsedMs = elapsedMs;
    record(e);
}

void recordToolCall(const QString &backend, const QString &model,
                    const QString &mode, const QString &toolName,
                    const QString &toolArgs) {
    Event e;
    e.backend = backend; e.model = model; e.mode = mode;
    e.role = Role::ToolCall; e.toolName = toolName; e.toolArgs = toolArgs;
    record(e);
}

void recordToolResult(const QString &backend, const QString &model,
                      const QString &mode, const QString &toolName,
                      const QString &toolResult) {
    Event e;
    e.backend = backend; e.model = model; e.mode = mode;
    e.role = Role::ToolResult; e.toolName = toolName; e.toolResult = toolResult;
    record(e);
}

void recordError(const QString &backend, const QString &model,
                 const QString &mode, const QString &error) {
    Event e;
    e.backend = backend; e.model = model; e.mode = mode;
    e.role = Role::Assistant; e.error = error;
    record(e);
}

QVector<Event> query(const Filter &f) {
    QVector<Event> out;
    if (!isEnabled()) return out;
    QMutexLocker lock(&g_mutex);
    QSqlDatabase db = openDb();
    if (!db.isValid() || !db.isOpen()) return out;

    QString sql = QStringLiteral(
        "SELECT id, ts, session_id, backend, model, mode, role, content,"
        " tool_name, tool_args, tool_result,"
        " prompt_tokens, eval_tokens, elapsed_ms, error"
        " FROM interactions WHERE 1=1");
    QVector<QVariant> binds;
    if (f.sinceTs > 0) { sql += " AND ts >= ?"; binds.append(f.sinceTs); }
    if (f.untilTs > 0) { sql += " AND ts <= ?"; binds.append(f.untilTs); }
    if (!f.backend.isEmpty()) { sql += " AND backend = ?"; binds.append(f.backend); }
    if (!f.model.isEmpty())   { sql += " AND model = ?";   binds.append(f.model);   }
    if (!f.mode.isEmpty())    { sql += " AND mode = ?";    binds.append(f.mode);    }
    sql += " ORDER BY id DESC LIMIT ?";
    binds.append(qBound(1, f.limit, 10000));

    QSqlQuery q(db);
    q.prepare(sql);
    for (const auto &v : binds) q.addBindValue(v);
    if (!q.exec()) {
        qWarning("[AiInteractionLog] query failed: %s",
            qPrintable(q.lastError().text()));
        return out;
    }
    while (q.next()) {
        Event e;
        e.id           = q.value(0).toLongLong();
        e.ts           = q.value(1).toLongLong();
        e.sessionId    = q.value(2).toString();
        e.backend      = q.value(3).toString();
        e.model        = q.value(4).toString();
        e.mode         = q.value(5).toString();
        e.role         = roleFromStr(q.value(6).toString());
        e.content      = q.value(7).toString();
        e.toolName     = q.value(8).toString();
        e.toolArgs     = q.value(9).toString();
        e.toolResult   = q.value(10).toString();
        e.promptTokens = q.value(11).toInt();
        e.evalTokens   = q.value(12).toInt();
        e.elapsedMs    = q.value(13).toInt();
        e.error        = q.value(14).toString();
        out.append(e);
    }
    return out;
}

void pruneOld() {
    if (!isEnabled()) return;
    QMutexLocker lock(&g_mutex);
    QSqlDatabase db = openDb();
    if (!db.isValid() || !db.isOpen()) return;

    const qint64 cutoff = QDateTime::currentSecsSinceEpoch() - 7 * 24 * 3600;
    QSqlQuery q(db);
    q.prepare(QStringLiteral("DELETE FROM interactions WHERE ts < ?"));
    q.addBindValue(cutoff);
    if (!q.exec()) {
        qWarning("[AiInteractionLog] prune failed: %s",
            qPrintable(q.lastError().text()));
        return;
    }

    // Size cap. If the db file exceeds 50 MB after pruning by date,
    // delete the oldest rows in batches of 500 until we drop below.
    const qint64 kSizeCap = 50 * 1024 * 1024;
    QFileInfo fi(dbPathInternal());
    while (fi.size() > kSizeCap) {
        QSqlQuery del(db);
        if (!del.exec(QStringLiteral(
            "DELETE FROM interactions WHERE id IN ("
            " SELECT id FROM interactions ORDER BY id ASC LIMIT 500)"))) {
            break;
        }
        if (del.numRowsAffected() == 0) break;
        QSqlQuery vac(db);
        vac.exec(QStringLiteral("VACUUM"));
        fi.refresh();
    }
}

} // namespace AiInteractionLog
