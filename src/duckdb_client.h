// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef DUCKDB_CLIENT_H
#define DUCKDB_CLIENT_H

#include <QString>
#include <QStringList>
#include <QJsonArray>
#include <QJsonObject>
#include <QVector>
#include <functional>

// v0.1.55 — DuckDB RAII wrapper. Replaces the QSqlDatabase abstraction for
// every connection type that DuckDB handles natively (CSV / Parquet /
// JSON / S3 / DuckDB files). Built around the C API in duckdb.h so we
// don't take a hard ABI dependency on the C++ headers.
//
// The wrapper enforces RAII: every `duckdb_database`, `duckdb_connection`,
// and `duckdb_result` handle is owned by exactly one wrapper class whose
// destructor invokes the matching `duckdb_close` / `duckdb_disconnect` /
// `duckdb_destroy_result`. There are no bare pointers in the public API,
// no manual cleanup required by callers — leaks are structurally
// impossible from outside this class.
//
// All methods are no-ops (returning a clear error) when Notepatra was
// built without DuckDB support. Callers can check available() first or
// rely on the error path.

namespace DuckDb {

// One row of a query result, as a list of column→string-value pairs.
// String-only on purpose: the Data Analyst chat path renders these as
// markdown tables anyway, and string round-tripping via DuckDB's
// `duckdb_value_varchar` handles every type uniformly (timestamps, JSON
// columns, decimals, UUIDs) without us reimplementing per-type formatters.
struct Row {
    QStringList values;
};

struct ResultSet {
    QStringList columns;        // ordered column names
    QVector<Row> rows;          // ordered rows
    int totalRows = 0;          // total observed (may exceed rows.size()
                                // if a row cap was applied)
    bool truncated = false;     // true if more rows existed but we stopped
    bool cancelled = false;     // true iff exec aborted via requestInterrupt()
    QString errorMessage;       // non-empty iff the query failed
};

// Streaming row-by-row callback. Return false to stop iteration early
// (e.g. row cap reached).
using RowCallback = std::function<bool(const QStringList &columns,
                                       const QStringList &values)>;

class Client {
public:
    Client();
    ~Client();

    Client(const Client &) = delete;
    Client &operator=(const Client &) = delete;

    // Was Notepatra built with DuckDB support? Returns false if compiled
    // without NOTEPATRA_HAVE_DUCKDB; in that case every other method
    // returns an error result.
    static bool available();

    // Open a database. `path` is one of:
    //   ""           in-memory database
    //   ":memory:"   in-memory database (alias)
    //   "/path/file.duckdb"  local DuckDB file
    // The connection stays open until close() or destruction.
    //
    // `readOnly` opens an EXISTING on-disk database file with
    // access_mode=READ_ONLY (via duckdb_open_ext) so the engine itself
    // refuses every write — the strongest enforcement layer for the
    // Data-analyst read-only contract. It has no effect on in-memory /
    // ":memory:" databases: those are the writable scratch space used to
    // ingest CSV/Parquet/JSON views for analysis, and must stay writable.
    bool open(const QString &path, QString *outError = nullptr,
              bool readOnly = false);

    // Close any open connection / database. Safe to call repeatedly.
    void close();

    bool isOpen() const;

    // Request cancellation of an in-flight exec(). Thread-safe: designed to
    // be called from a DIFFERENT thread than the one blocked inside exec()
    // (duckdb_interrupt is documented thread-safe). A no-op if nothing is
    // running. The interrupted exec() returns a ResultSet with cancelled=true.
    void requestInterrupt();

    // Execute SQL and return up to `rowCap` rows. Use 0 for unbounded.
    //
    // Row-cap-BEFORE-materialization: for a plain query (SELECT / WITH /
    // TABLE / VALUES / FROM) the statement is wrapped in a subquery with a
    // top-level `LIMIT rowCap+1`, so DuckDB short-circuits the pipeline and
    // a cross-join can never materialize the full product into memory before
    // truncation. Execution is driven through the pending-task API so it can
    // be aborted mid-flight via requestInterrupt() (used by the async worker
    // for cancel + timeout). Non-query statements (DESCRIBE / PRAGMA / DDL /
    // introspection) run through the direct path with a post-fetch cap; those
    // are bounded by construction.
    ResultSet exec(const QString &sql, int rowCap = 10000);

    // Same as exec but streams each row to `cb` instead of accumulating.
    // Returns column names (always populated) plus an error string (empty
    // on success). The callback can return false to stop early.
    QStringList execStreaming(const QString &sql,
                              const RowCallback &cb,
                              QString *outError = nullptr);

    // Schema introspection — returns the table tree for the currently-
    // attached database (and any ATTACHed peers). Each entry: schema,
    // table name, row-count estimate (-1 if unavailable).
    struct TableInfo {
        QString schema;
        QString name;
        QString kind;            // "BASE TABLE" / "VIEW" / etc.
        qint64  rowEstimate = -1;
    };
    QVector<TableInfo> listTables(QString *outError = nullptr);

    struct ColumnInfo {
        QString name;
        QString dataType;        // DuckDB-reported type
        bool nullable = true;
    };
    QVector<ColumnInfo> describeTable(const QString &qualifiedName,
                                      QString *outError = nullptr);

    // Extension installation/loading. DuckDB ships extensions out-of-band;
    // httpfs (for S3), postgres_scanner, mysql_scanner, sqlite_scanner all
    // live in the extension subsystem. Installing pulls from the official
    // repository the first time; subsequent calls are no-ops.
    bool installAndLoad(const QString &extensionName, QString *outError = nullptr);

    // S3 credential setup. Sets the connection's session-level keys so
    // subsequent SELECTs against `s3://...` URIs authenticate correctly.
    // Pass empty `accessKeyId` to clear (use anonymous public buckets).
    bool configureS3(const QString &region,
                     const QString &accessKeyId,
                     const QString &secretAccessKey,
                     const QString &sessionToken = QString(),
                     QString *outError = nullptr);

    // Convenience: load a CSV file as a virtual table named `tableName`
    // (default: derived from filename) using DuckDB's auto-detect reader.
    // Subsequent queries can SELECT from `tableName`.
    //
    // `materialize` (default false = existing desktop behaviour) controls the
    // ingestion shape. false → a CREATE VIEW that RE-SCANS the file on every
    // query (cheap, lazy). true → a CREATE TABLE AS that reads the file EXACTLY
    // ONCE into an in-memory table. The materializing form exists for the MCP
    // filesystem sandbox: it lets trusted code read the source while external
    // access is still enabled, then call disableExternalAccess() so the
    // untrusted query can still `SELECT * FROM data` (a real table) but can no
    // longer touch the disk. A view would re-scan on query and break under the
    // lockdown, so the sandbox MUST use materialize=true.
    bool registerCsv(const QString &csvPath,
                     const QString &tableName = QString(),
                     QString *outError = nullptr,
                     bool materialize = false);
    bool registerParquet(const QString &parquetPath,
                         const QString &tableName = QString(),
                         QString *outError = nullptr,
                         bool materialize = false);
    bool registerJson(const QString &jsonPath,
                      const QString &tableName = QString(),
                      QString *outError = nullptr,
                      bool materialize = false);

    // Engine-level filesystem lockdown for the MCP run_sql sandbox. Issues
    // `SET enable_external_access=false`, which DuckDB documents as a one-way
    // switch (it cannot be re-enabled on the same connection) — exactly the
    // property we want. After this call the engine itself REFUSES every
    // local-file / http / S3 access: the replacement scan (SELECT * FROM
    // '/path'), read_text/read_csv_auto/read_blob/read_parquet/read_json/glob,
    // COPY, ATTACH — all fail with "Permission Error: … disabled through
    // configuration". Must be called AFTER any legitimate ingestion (the file
    // is read once during registerCsv(materialize=true)); anything read after
    // is blocked. Empirically verified against vendor/duckdb libduckdb.so
    // (v1.1.3): blocks replacement scan + quoted read_text + read_csv_auto +
    // read_blob + glob while `SELECT * FROM data` (a materialized table) still
    // returns rows.
    bool disableExternalAccess(QString *outError = nullptr);

private:
    // Internal exec paths — see duckdb_client.cpp. execCapped wraps a plain
    // query in a LIMIT subquery and drives it interruptibly; execRaw is the
    // direct duckdb_query fallback for non-wrappable / unbounded statements.
    ResultSet execCapped(const QString &querySql, int rowCap);
    ResultSet execRaw(const QString &sql, int rowCap);

    struct Impl;
    Impl *m_impl;   // owned; pimpl so duckdb.h doesn't bleed into the header
};

}  // namespace DuckDb

#endif
