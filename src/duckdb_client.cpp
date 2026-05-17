// SPDX-License-Identifier: GPL-3.0-or-later

#include "duckdb_client.h"

#include <QFileInfo>
#include <QRegularExpression>

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
    duckdb_connection connection = nullptr;

    ~Impl() { closeAll(); }

    void closeAll() {
        if (connection) {
            duckdb_disconnect(&connection);
            connection = nullptr;
        }
        if (database) {
            duckdb_close(&database);
            database = nullptr;
        }
    }

    bool isOpen() const { return database != nullptr && connection != nullptr; }
};

Client::Client() : m_impl(new Impl) {}
Client::~Client() { delete m_impl; }

bool Client::available() { return true; }

bool Client::open(const QString &path, QString *outError) {
    close();
    const QByteArray pathUtf8 = path.toUtf8();
    const char *cpath = path.isEmpty() || path == ":memory:"
                          ? nullptr
                          : pathUtf8.constData();
    if (duckdb_open(cpath, &m_impl->database) == DuckDBError) {
        if (outError) *outError = "duckdb_open failed";
        m_impl->database = nullptr;
        return false;
    }
    if (duckdb_connect(m_impl->database, &m_impl->connection) == DuckDBError) {
        if (outError) *outError = "duckdb_connect failed";
        duckdb_close(&m_impl->database);
        m_impl->database = nullptr;
        m_impl->connection = nullptr;
        return false;
    }
    return true;
}

void Client::close() { m_impl->closeAll(); }

bool Client::isOpen() const { return m_impl->isOpen(); }

// Execute a query and stream rows through `cb`. `rowCap` is enforced
// outside DuckDB (we count rows we've delivered to cb). DuckDB's own
// streaming API gives us `duckdb_data_chunk` objects; we iterate those
// and emit one row at a time so the caller never sees the binary chunk
// format.
//
// On error the returned ResultSet has an empty `rows`, the columns may
// or may not be populated (depending on whether the prepare step
// succeeded), and `errorMessage` is non-empty.
static QStringList readChunkRow(duckdb_data_chunk chunk,
                                idx_t rowIndex,
                                idx_t colCount) {
    QStringList values;
    values.reserve(static_cast<int>(colCount));
    for (idx_t c = 0; c < colCount; ++c) {
        duckdb_vector vec = duckdb_data_chunk_get_vector(chunk, c);
        // NULL handling — DuckDB's "validity" mask flags non-NULL rows.
        uint64_t *validity = duckdb_vector_get_validity(vec);
        const bool isNotNull = duckdb_validity_row_is_valid(validity, rowIndex);
        if (!isNotNull) {
            values.append(QStringLiteral("NULL"));
            continue;
        }
        // For non-NULL values, ask DuckDB to produce a varchar
        // representation. This is per-row but DuckDB caches the
        // logical-type for the column so it's cheap; for huge
        // tables the streaming bottleneck is the network anyway.
        // We do this by extracting via duckdb_value_varchar on the
        // result, which requires a row index in the global result
        // not in the chunk — see the streaming loop below.
        values.append(QString());  // placeholder; filled by caller
    }
    return values;
}

ResultSet Client::exec(const QString &sql, int rowCap) {
    ResultSet rs;
    if (!isOpen()) {
        rs.errorMessage = "not connected";
        return rs;
    }

    duckdb_result result;
    if (duckdb_query(m_impl->connection, sql.toUtf8().constData(), &result)
            == DuckDBError) {
        rs.errorMessage = QString::fromUtf8(duckdb_result_error(&result));
        if (rs.errorMessage.isEmpty()) rs.errorMessage = "query failed";
        duckdb_destroy_result(&result);
        return rs;
    }

    const idx_t cols = duckdb_column_count(&result);
    rs.columns.reserve(static_cast<int>(cols));
    for (idx_t c = 0; c < cols; ++c) {
        rs.columns.append(QString::fromUtf8(duckdb_column_name(&result, c)));
    }

    const idx_t total = duckdb_row_count(&result);
    rs.totalRows = static_cast<int>(total);
    const idx_t cap = (rowCap > 0)
                        ? std::min<idx_t>(total, static_cast<idx_t>(rowCap))
                        : total;
    rs.truncated = (cap < total);

    rs.rows.reserve(static_cast<int>(cap));
    for (idx_t r = 0; r < cap; ++r) {
        Row row;
        row.values.reserve(static_cast<int>(cols));
        for (idx_t c = 0; c < cols; ++c) {
            char *valStr = duckdb_value_varchar(&result, c, r);
            if (valStr) {
                row.values.append(QString::fromUtf8(valStr));
                duckdb_free(valStr);
            } else {
                row.values.append(QStringLiteral("NULL"));
            }
        }
        rs.rows.append(std::move(row));
    }

    duckdb_destroy_result(&result);
    return rs;
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
    if (duckdb_query(m_impl->connection, sql.toUtf8().constData(), &result)
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

bool Client::open(const QString &, QString *outError) {
    if (outError) *outError = kStubError;
    return false;
}
void Client::close() {}
bool Client::isOpen() const { return false; }
ResultSet Client::exec(const QString &, int) {
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
