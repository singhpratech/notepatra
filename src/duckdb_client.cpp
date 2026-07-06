// SPDX-License-Identifier: GPL-3.0-or-later

#include "duckdb_client.h"

#include <QFileInfo>
#include <QRegularExpression>

#include <atomic>

#ifdef NOTEPATRA_HAVE_DUCKDB
#include <duckdb.h>
#endif

namespace DuckDb {

#ifdef NOTEPATRA_HAVE_DUCKDB

// Internal pimpl — RAII handles for the DuckDB C API. Each member is
// destroyed (in reverse construction order) by close() / ~Impl(), which
// makes leaks structurally impossible: any path out of this object —
// normal return, exception, panic — runs the destructor.
struct Client::Impl {
    duckdb_database database     = nullptr;
    // F1 sub-bug — the connection handle is READ by requestInterrupt()
    // (controller thread, for duckdb_interrupt) while it is WRITTEN by open() /
    // close() on the worker thread. std::atomic makes that cross-thread pointer
    // access well-defined (lock-free on every real platform).
    std::atomic<duckdb_connection> connection{nullptr};
    // Set from requestInterrupt() (possibly on another thread); read by the
    // pending-task loop in exec(). std::atomic so the cross-thread read/write
    // is well-defined.
    std::atomic<bool> interruptRequested{false};

    ~Impl() { closeAll(); }

    void closeAll() {
        duckdb_connection c = connection.load();
        if (c) {
            duckdb_disconnect(&c);
            connection.store(nullptr);
        }
        if (database) {
            duckdb_close(&database);
            database = nullptr;
        }
    }

    bool isOpen() const {
        return database != nullptr && connection.load() != nullptr;
    }
};

Client::Client() : m_impl(new Impl) {}
Client::~Client() { delete m_impl; }

bool Client::available() { return true; }

bool Client::open(const QString &path, QString *outError, bool readOnly) {
    close();
    const QByteArray pathUtf8 = path.toUtf8();
    const bool inMemory = path.isEmpty() || path == ":memory:";
    const char *cpath = inMemory ? nullptr : pathUtf8.constData();

    // read_only is meaningful only for an on-disk database file. An in-memory
    // database is the writable scratch space used to ingest CSV/Parquet/JSON
    // as views for analysis, so we never force it read-only even when the
    // caller asks — there is nothing persistent to protect and ingestion
    // needs the writes.
    if (readOnly && !inMemory) {
        duckdb_config config = nullptr;
        if (duckdb_create_config(&config) == DuckDBError) {
            if (outError) *outError = "duckdb_create_config failed";
            duckdb_destroy_config(&config);
            return false;
        }
        // access_mode=READ_ONLY makes the engine itself reject every write —
        // the authoritative enforcement layer, independent of the SQL
        // classifier. Opening a not-yet-existing file read-only fails, which
        // is the correct behaviour (we never want to create a user DB here).
        duckdb_set_config(config, "access_mode", "READ_ONLY");
        char *openErr = nullptr;
        const duckdb_state st =
            duckdb_open_ext(cpath, &m_impl->database, config, &openErr);
        duckdb_destroy_config(&config);
        if (st == DuckDBError) {
            if (outError)
                *outError = openErr ? QString::fromUtf8(openErr)
                                    : QStringLiteral("duckdb_open (read-only) failed");
            if (openErr) duckdb_free(openErr);
            m_impl->database = nullptr;
            return false;
        }
        if (openErr) duckdb_free(openErr);
    } else if (duckdb_open(cpath, &m_impl->database) == DuckDBError) {
        if (outError) *outError = "duckdb_open failed";
        m_impl->database = nullptr;
        return false;
    }

    duckdb_connection conn = nullptr;
    if (duckdb_connect(m_impl->database, &conn) == DuckDBError) {
        if (outError) *outError = "duckdb_connect failed";
        duckdb_close(&m_impl->database);
        m_impl->database = nullptr;
        m_impl->connection.store(nullptr);
        return false;
    }
    m_impl->connection.store(conn);
    return true;
}

void Client::close() { m_impl->closeAll(); }

bool Client::isOpen() const { return m_impl->isOpen(); }

void Client::requestInterrupt() {
    m_impl->interruptRequested.store(true);
    // duckdb_interrupt is safe to call from any thread; it flags the
    // connection's active query so the executor unwinds at its next check.
    duckdb_connection c = m_impl->connection.load();
    if (c) duckdb_interrupt(c);
}

// Copy a materialized duckdb_result into a ResultSet, capping at `rowCap`
// rows (0 = unbounded). Universal stringification via duckdb_value_varchar
// handles every type uniformly. When the result was produced by the LIMIT
// rowCap+1 wrap in execCapped(), `total` is rowCap+1 for a truncated query,
// which sets truncated=true without exposing the extra row.
static void fillResultSet(duckdb_result *result, ResultSet &rs, int rowCap) {
    const idx_t cols = duckdb_column_count(result);
    rs.columns.reserve(static_cast<int>(cols));
    for (idx_t c = 0; c < cols; ++c) {
        rs.columns.append(QString::fromUtf8(duckdb_column_name(result, c)));
    }
    const idx_t total = duckdb_row_count(result);
    const idx_t cap = (rowCap > 0)
                        ? std::min<idx_t>(total, static_cast<idx_t>(rowCap))
                        : total;
    rs.totalRows = static_cast<int>(cap);
    rs.truncated = (cap < total);
    rs.rows.reserve(static_cast<int>(cap));
    for (idx_t r = 0; r < cap; ++r) {
        Row row;
        row.values.reserve(static_cast<int>(cols));
        for (idx_t c = 0; c < cols; ++c) {
            char *valStr = duckdb_value_varchar(result, c, r);
            if (valStr) {
                row.values.append(QString::fromUtf8(valStr));
                duckdb_free(valStr);
            } else {
                row.values.append(QStringLiteral("NULL"));
            }
        }
        rs.rows.append(std::move(row));
    }
}

// Leading-keyword test for statements that can be safely wrapped in a
// `SELECT * FROM ( <sql> ) LIMIT n` subquery. Only plain queries qualify;
// DESCRIBE / PRAGMA / SHOW / DDL cannot appear inside a subquery.
static bool isWrappableQuery(const QString &trimmedSql) {
    static const QRegularExpression re(
        QStringLiteral("^\\s*(SELECT|WITH|TABLE|VALUES|FROM)\\b"),
        QRegularExpression::CaseInsensitiveOption);
    return re.match(trimmedSql).hasMatch();
}

// Direct path: single duckdb_query call + post-fetch cap. Used for
// non-wrappable / unbounded statements (introspection, DDL on the writable
// scratch DB, multi-statement internal helpers). Still abortable because
// requestInterrupt() calls duckdb_interrupt on the connection, which unwinds
// a running duckdb_query.
ResultSet Client::execRaw(const QString &sql, int rowCap) {
    ResultSet rs;
    duckdb_result result;
    if (duckdb_query(m_impl->connection.load(), sql.toUtf8().constData(), &result)
            == DuckDBError) {
        rs.cancelled = m_impl->interruptRequested.load();
        rs.errorMessage = QString::fromUtf8(duckdb_result_error(&result));
        if (rs.errorMessage.isEmpty())
            rs.errorMessage = rs.cancelled ? QStringLiteral("query cancelled")
                                           : QStringLiteral("query failed");
        duckdb_destroy_result(&result);
        return rs;
    }
    fillResultSet(&result, rs, rowCap);
    duckdb_destroy_result(&result);
    return rs;
}

// Capped path for plain queries. Wraps the query in a LIMIT rowCap+1
// subquery so DuckDB never materializes more than rowCap+1 rows (a
// top-level LIMIT short-circuits the pipeline — a cross-join can't OOM),
// and drives execution through the pending-task API so requestInterrupt()
// can abort it between tasks.
ResultSet Client::execCapped(const QString &querySql, int rowCap) {
    ResultSet rs;
    const QString effective =
        QStringLiteral("SELECT * FROM (\n%1\n) AS _np_capped LIMIT %2")
            .arg(querySql)
            .arg(static_cast<qulonglong>(rowCap) + 1);

    duckdb_prepared_statement prep = nullptr;
    if (duckdb_prepare(m_impl->connection.load(), effective.toUtf8().constData(),
                       &prep) == DuckDBError) {
        // Our wrappability heuristic can be wrong (e.g. a statement that
        // isn't valid inside a subquery); fall back to the direct capped
        // path rather than surfacing a spurious syntax error.
        duckdb_destroy_prepare(&prep);
        return execRaw(querySql, rowCap);
    }

    duckdb_pending_result pending = nullptr;
    if (duckdb_pending_prepared(prep, &pending) == DuckDBError) {
        rs.errorMessage = QString::fromUtf8(duckdb_pending_error(pending));
        if (rs.errorMessage.isEmpty()) rs.errorMessage = QStringLiteral("execution failed");
        duckdb_destroy_pending(&pending);
        duckdb_destroy_prepare(&prep);
        return rs;
    }

    for (;;) {
        const duckdb_pending_state st = duckdb_pending_execute_task(pending);
        if (st == DUCKDB_PENDING_RESULT_READY) break;
        if (st == DUCKDB_PENDING_ERROR) {
            rs.cancelled = m_impl->interruptRequested.load();
            rs.errorMessage = QString::fromUtf8(duckdb_pending_error(pending));
            if (rs.errorMessage.isEmpty())
                rs.errorMessage = rs.cancelled ? QStringLiteral("query cancelled")
                                               : QStringLiteral("query failed");
            duckdb_destroy_pending(&pending);
            duckdb_destroy_prepare(&prep);
            return rs;
        }
        if (m_impl->interruptRequested.load())
            duckdb_interrupt(m_impl->connection.load());  // unwind; next task → ERROR
    }

    duckdb_result result;
    const duckdb_state ok = duckdb_execute_pending(pending, &result);
    duckdb_destroy_pending(&pending);
    duckdb_destroy_prepare(&prep);
    if (ok == DuckDBError) {
        rs.cancelled = m_impl->interruptRequested.load();
        rs.errorMessage = QString::fromUtf8(duckdb_result_error(&result));
        if (rs.errorMessage.isEmpty())
            rs.errorMessage = rs.cancelled ? QStringLiteral("query cancelled")
                                           : QStringLiteral("query failed");
        duckdb_destroy_result(&result);
        return rs;
    }
    fillResultSet(&result, rs, rowCap);
    duckdb_destroy_result(&result);
    return rs;
}

ResultSet Client::exec(const QString &sql, int rowCap) {
    ResultSet rs;
    if (!isOpen()) {
        rs.errorMessage = "not connected";
        return rs;
    }
    // F1 sub-bug — do NOT unconditionally clear interruptRequested here: a
    // requestInterrupt() that arrived just before this exec (an "early cancel",
    // e.g. Stop pressed the instant the query dispatched) would be erased,
    // letting the query run to completion. Instead the flag is cleared at the
    // END of exec (RAII), so a stale flag from a prior cancelled exec on a
    // REUSED client is gone before we start, while an early cancel for THIS run
    // survives to be honored by the pending-task loop.
    struct FlagReset {
        std::atomic<bool> *f;
        ~FlagReset() { f->store(false); }
    } flagReset{&m_impl->interruptRequested};

    QString trimmed = sql.trimmed();
    while (trimmed.endsWith(QLatin1Char(';'))) {
        trimmed.chop(1);
        trimmed = trimmed.trimmed();
    }
    if (rowCap > 0 && isWrappableQuery(trimmed))
        return execCapped(trimmed, rowCap);
    return execRaw(sql, rowCap);
}

QStringList Client::execStreaming(const QString &sql,
                                  const RowCallback &cb,
                                  QString *outError) {
    QStringList columns;
    if (!isOpen()) {
        if (outError) *outError = "not connected";
        return columns;
    }

    duckdb_result result;
    if (duckdb_query(m_impl->connection.load(), sql.toUtf8().constData(), &result)
            == DuckDBError) {
        if (outError) *outError = QString::fromUtf8(duckdb_result_error(&result));
        duckdb_destroy_result(&result);
        return columns;
    }

    const idx_t cols = duckdb_column_count(&result);
    columns.reserve(static_cast<int>(cols));
    for (idx_t c = 0; c < cols; ++c) {
        columns.append(QString::fromUtf8(duckdb_column_name(&result, c)));
    }

    const idx_t total = duckdb_row_count(&result);
    for (idx_t r = 0; r < total; ++r) {
        QStringList values;
        values.reserve(static_cast<int>(cols));
        for (idx_t c = 0; c < cols; ++c) {
            char *valStr = duckdb_value_varchar(&result, c, r);
            if (valStr) {
                values.append(QString::fromUtf8(valStr));
                duckdb_free(valStr);
            } else {
                values.append(QStringLiteral("NULL"));
            }
        }
        if (!cb(columns, values)) break;  // caller asked us to stop
    }

    duckdb_destroy_result(&result);
    return columns;
}

QVector<Client::TableInfo> Client::listTables(QString *outError) {
    QVector<TableInfo> out;
    // information_schema.tables works across attached databases too.
    const QString sql =
        "SELECT table_schema, table_name, table_type "
        "FROM information_schema.tables "
        "WHERE table_schema NOT IN ('information_schema','pg_catalog') "
        "ORDER BY table_schema, table_name";
    ResultSet rs = exec(sql, 0);
    if (!rs.errorMessage.isEmpty()) {
        if (outError) *outError = rs.errorMessage;
        return out;
    }
    out.reserve(rs.rows.size());
    for (const Row &r : rs.rows) {
        if (r.values.size() < 3) continue;
        TableInfo t;
        t.schema = r.values[0];
        t.name   = r.values[1];
        t.kind   = r.values[2];
        out.append(t);
    }
    return out;
}

QVector<Client::ColumnInfo> Client::describeTable(const QString &qualifiedName,
                                                  QString *outError) {
    QVector<ColumnInfo> out;
    // DESCRIBE accepts a qualified name; the result has columns
    // (column_name, column_type, null, key, default, extra).
    const QString sql = "DESCRIBE " + qualifiedName;
    ResultSet rs = exec(sql, 0);
    if (!rs.errorMessage.isEmpty()) {
        if (outError) *outError = rs.errorMessage;
        return out;
    }
    out.reserve(rs.rows.size());
    for (const Row &r : rs.rows) {
        if (r.values.size() < 3) continue;
        ColumnInfo c;
        c.name     = r.values[0];
        c.dataType = r.values[1];
        c.nullable = r.values[2].compare(QStringLiteral("YES"), Qt::CaseInsensitive) == 0;
        out.append(c);
    }
    return out;
}

bool Client::installAndLoad(const QString &name, QString *outError) {
    // INSTALL is a no-op if already installed. LOAD must run on every
    // connection — extensions aren't sticky across reconnects.
    const QString safe = name.trimmed();
    if (safe.isEmpty() || safe.contains(QRegularExpression("[^A-Za-z0-9_-]"))) {
        if (outError) *outError = "invalid extension name";
        return false;
    }
    const QString sql = QString("INSTALL %1; LOAD %1;").arg(safe);
    ResultSet rs = exec(sql, 0);
    if (!rs.errorMessage.isEmpty()) {
        if (outError) *outError = rs.errorMessage;
        return false;
    }
    return true;
}

bool Client::configureS3(const QString &region,
                         const QString &accessKeyId,
                         const QString &secretAccessKey,
                         const QString &sessionToken,
                         QString *outError) {
    if (!installAndLoad("httpfs", outError)) return false;

    auto sqlEscape = [](const QString &s) -> QString {
        QString e = s;
        e.replace(QChar('\''), QStringLiteral("''"));
        return e;
    };

    QStringList stmts;
    if (!region.isEmpty())
        stmts << QString("SET s3_region='%1';").arg(sqlEscape(region));
    if (accessKeyId.isEmpty()) {
        // Anonymous access — clear any previously-set creds for this session.
        stmts << "SET s3_access_key_id='';"
              << "SET s3_secret_access_key='';"
              << "SET s3_session_token='';";
    } else {
        stmts << QString("SET s3_access_key_id='%1';").arg(sqlEscape(accessKeyId))
              << QString("SET s3_secret_access_key='%1';").arg(sqlEscape(secretAccessKey));
        if (!sessionToken.isEmpty())
            stmts << QString("SET s3_session_token='%1';").arg(sqlEscape(sessionToken));
    }
    for (const QString &s : stmts) {
        ResultSet rs = exec(s, 0);
        if (!rs.errorMessage.isEmpty()) {
            if (outError) *outError = rs.errorMessage;
            return false;
        }
    }
    return true;
}

static QString deriveTableName(const QString &filePath, const QString &explicitName) {
    if (!explicitName.isEmpty()) return explicitName;
    QFileInfo fi(filePath);
    QString base = fi.completeBaseName();  // strip extension
    // Sanitize for SQL identifier — replace anything non-alnum/underscore with _.
    base.replace(QRegularExpression("[^A-Za-z0-9_]"), "_");
    if (base.isEmpty() || base[0].isDigit()) base.prepend('_');
    return base;
}

bool Client::registerCsv(const QString &csvPath,
                         const QString &tableName,
                         QString *outError) {
    const QString name = deriveTableName(csvPath, tableName);
    QString p = csvPath;
    p.replace(QChar('\''), QStringLiteral("''"));
    const QString sql = QString(
        "CREATE OR REPLACE VIEW %1 AS SELECT * FROM read_csv_auto('%2');")
        .arg(name, p);
    ResultSet rs = exec(sql, 0);
    if (!rs.errorMessage.isEmpty()) {
        if (outError) *outError = rs.errorMessage;
        return false;
    }
    return true;
}

bool Client::registerParquet(const QString &parquetPath,
                             const QString &tableName,
                             QString *outError) {
    const QString name = deriveTableName(parquetPath, tableName);
    QString p = parquetPath;
    p.replace(QChar('\''), QStringLiteral("''"));
    const QString sql = QString(
        "CREATE OR REPLACE VIEW %1 AS SELECT * FROM read_parquet('%2');")
        .arg(name, p);
    ResultSet rs = exec(sql, 0);
    if (!rs.errorMessage.isEmpty()) {
        if (outError) *outError = rs.errorMessage;
        return false;
    }
    return true;
}

bool Client::registerJson(const QString &jsonPath,
                          const QString &tableName,
                          QString *outError) {
    const QString name = deriveTableName(jsonPath, tableName);
    QString p = jsonPath;
    p.replace(QChar('\''), QStringLiteral("''"));
    const QString sql = QString(
        "CREATE OR REPLACE VIEW %1 AS SELECT * FROM read_json_auto('%2');")
        .arg(name, p);
    ResultSet rs = exec(sql, 0);
    if (!rs.errorMessage.isEmpty()) {
        if (outError) *outError = rs.errorMessage;
        return false;
    }
    return true;
}

#else  // NOTEPATRA_HAVE_DUCKDB

// Stub implementation when Notepatra was built without DuckDB. Every
// method returns a clear error string explaining the missing feature so
// the user can see exactly what to do (rebuild with NOTEPATRA_USE_DUCKDB
// or install libduckdb).
struct Client::Impl { bool dummy = false; };

Client::Client() : m_impl(new Impl) {}
Client::~Client() { delete m_impl; }

bool Client::available() { return false; }

static const char *kStubError =
    "DuckDB support not compiled in (rebuild with NOTEPATRA_USE_DUCKDB=ON "
    "and a vendored libduckdb in vendor/duckdb/)";

bool Client::open(const QString &, QString *outError, bool) {
    if (outError) *outError = kStubError;
    return false;
}
void Client::close() {}
bool Client::isOpen() const { return false; }
void Client::requestInterrupt() {}
ResultSet Client::exec(const QString &, int) {
    ResultSet rs; rs.errorMessage = kStubError; return rs;
}
ResultSet Client::execCapped(const QString &, int) {
    ResultSet rs; rs.errorMessage = kStubError; return rs;
}
ResultSet Client::execRaw(const QString &, int) {
    ResultSet rs; rs.errorMessage = kStubError; return rs;
}
QStringList Client::execStreaming(const QString &, const RowCallback &, QString *outError) {
    if (outError) *outError = kStubError;
    return {};
}
QVector<Client::TableInfo> Client::listTables(QString *outError) {
    if (outError) *outError = kStubError;
    return {};
}
QVector<Client::ColumnInfo> Client::describeTable(const QString &, QString *outError) {
    if (outError) *outError = kStubError;
    return {};
}
bool Client::installAndLoad(const QString &, QString *outError) {
    if (outError) *outError = kStubError;
    return false;
}
bool Client::configureS3(const QString &, const QString &, const QString &,
                         const QString &, QString *outError) {
    if (outError) *outError = kStubError;
    return false;
}
bool Client::registerCsv(const QString &, const QString &, QString *outError) {
    if (outError) *outError = kStubError; return false;
}
bool Client::registerParquet(const QString &, const QString &, QString *outError) {
    if (outError) *outError = kStubError; return false;
}
bool Client::registerJson(const QString &, const QString &, QString *outError) {
    if (outError) *outError = kStubError; return false;
}

#endif

}  // namespace DuckDb
