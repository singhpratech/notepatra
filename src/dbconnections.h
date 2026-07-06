// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef NOTEPATRA_DBCONNECTIONS_H
#define NOTEPATRA_DBCONNECTIONS_H

// ═══════════════════════════════════════════════════════════════════════
// v0.1.43 — Saved database connections used by the Data Analyst AI
// assistant. Records are stored at ~/.config/notepatra/db-connections.json
// with passwords lightly obscured (XOR over a fixed key — NOT real
// encryption; documented in release notes). Real secrets should still
// live in OS keychain / .pgpass / instance-role IAM, not here.
//
// The `query_sql` agentic tool resolves a connection by `name`, opens it
// via QSqlDatabase, runs the SQL, and returns the result. The dialog
// (DbConnectionsDialog) lets users CRUD records and test reachability.
// ═══════════════════════════════════════════════════════════════════════

#include <QString>
#include <QStringList>
#include <QVector>
#include <QJsonObject>
#include <QDialog>
#include <QMetaType>
#include <QMutex>
#include <atomic>
#include <functional>

class QListWidget;

namespace DuckDb { class Client; }
class QLineEdit;
class QSpinBox;
class QComboBox;
class QPushButton;
class QLabel;

namespace DbConnections {

struct Record {
    QString name;       // user-chosen unique id, used by query_sql tool
    QString driver;     // "QSQLITE" | "QPSQL" | "QMYSQL" | "QODBC"
    QString host;       // ignored for SQLite
    int     port = 0;   // 0 means "use driver default"
    QString database;   // SQLite: path to .db file. Others: database name.
    QString username;   // ignored for SQLite
    QString password;   // stored obscured at rest — see obfuscatePassword
    QString options;    // optional connection-options string

    QJsonObject toJson() const;
    static Record fromJson(const QJsonObject &o);
};

// CRUD over ~/.config/notepatra/db-connections.json. loadAll() returns an
// empty vector if the file doesn't exist or is unreadable. saveAll()
// creates the parent directory if needed and writes atomically (tmp +
// rename) so a crashed write can't corrupt the file.
QVector<Record> loadAll();
bool saveAll(const QVector<Record> &records);
QString configPath();

// Look up a single record by its `name` field. Returns false if not found
// (in which case *out is left unchanged).
bool findByName(const QString &name, Record *out);

// Attempt to QSqlDatabase::open() the record. On success returns true and
// the connection is open under a unique connection name (returned via
// *outConnectionName). Caller MUST QSqlDatabase::removeDatabase(*outName)
// when done. On failure, returns false and writes the driver error to
// *outError.
//
// `readOnly` (default true) asks for a session-level read-only guard on the
// server after connecting — PostgreSQL SET default_transaction_read_only,
// MySQL SET SESSION TRANSACTION READ ONLY. SQLite / SQL Server (QODBC) have
// no session read-only knob, so on those the single-statement SQL classifier
// (see classifySql) is the sole enforcement layer. A read-only setup that
// fails does NOT fail the connection (the query may still be a harmless
// SELECT); the reason is emitted via qWarning and written to
// *outReadOnlyWarning so the caller can surface it.
bool open(const Record &r, QString *outConnectionName, QString *outError,
          bool readOnly = true, QString *outReadOnlyWarning = nullptr);

// ── SQL read-only / mutation classifier ─────────────────────────────────
// Replaces the old leading-keyword prefix allowlist. Strips comments and
// string/identifier literals, tokenizes, and enforces a SINGLE statement
// (rejecting stacked "SELECT 1; DROP TABLE x"). It walks CTE bodies for
// embedded DML (WITH t AS (DELETE ... RETURNING *)), rejects EXPLAIN
// ANALYZE and EXPLAIN wrapping DML, rejects SELECT ... INTO, rejects
// INSTALL/LOAD/ATTACH/COPY/EXPORT/SET/CALL, and permits only a small
// read-only PRAGMA allowlist (rejecting assignment-form PRAGMA x=y).
struct SqlVerdict {
    bool singleStatement = false;  // exactly one statement (no stacking)
    bool readOnly = false;         // provably safe to run without approval
    bool mutation = false;         // authoritative "this changes data/state"
                                   // verdict for the human-approval gate
    QString reason;                // why it was rejected (empty if readOnly)
};
SqlVerdict classifySql(const QString &sql);

// Convenience wrappers over classifySql.
//   isReadOnlyQuery: safe to run in the default (no-approval) path.
//   isMutation:      the verdict a human-approval gate should key on
//                    (used by ai_tools.cpp's confirm=true flow).
bool isReadOnlyQuery(const QString &sql);
bool isMutation(const QString &sql);

#ifdef NOTEPATRA_HAVE_DUCKDB
// Open a DuckDb::Client for `r`: detects :memory: vs a DuckDB file vs a
// CSV/Parquet/JSON/S3 data source, opens attached *database files* READ_ONLY
// when !allowMutation (the in-memory scratch DB used to ingest data sources
// always stays writable), registers the source, and configures S3. Returns
// false and writes *outErr / *outErrKind on failure.
bool openDuckDbForRecord(const Record &r, bool allowMutation,
                         DuckDb::Client &client,
                         QString *outErr, QString *outErrKind);
#endif

// Convenience: open + run a single SELECT, return rows as JSON, close.
// `maxRows` caps the row count; `outTruncated` is set true if more rows
// were available. SQL must be a single SELECT (basic prefix check); other
// statements fail with errorKind="non_select" unless `allowMutation` is
// true (which the agent only sets after the user confirms).
struct QueryResult {
    bool ok = false;
    QStringList columns;
    QVector<QVector<QString>> rows;   // stringified per-cell for portability
    int rowsReturned = 0;
    bool truncated = false;
    QString error;        // driver error text on failure
    QString errorKind;    // "non_select" | "no_connection" | "open_failed" |
                          //  "exec_failed" | "syntax" | "cancelled" | "timeout"
    QString warning;      // non-fatal note (e.g. read-only session could not
                          // be enforced on the server — classifier only)
};

// v0.1.115 (F1) — thread-safe cancel token for a runQuery() call in flight.
// The controller thread holds one (typically via shared_ptr) and hands a raw
// pointer to runQuery on a worker thread; runQuery's DuckDB path binds an
// interrupt callback (closed over its live DuckDb::Client) for the duration of
// exec() and unbinds after. requestInterrupt() — safe to call from ANY thread —
// flags the token and, if a callback is currently bound, invokes it (which calls
// duckdb_interrupt on the live connection) so the running query unwinds at its
// next task boundary instead of running to completion. This is what makes the AI
// DB tool's Stop button / 30 s timeout GENUINELY interrupt a DuckDB query rather
// than merely abandoning the future (which left the query churning + could
// saturate the thread pool). QSql drivers can't be interrupted mid-exec, so for
// them nothing is bound and the token only records intent (best-effort).
//
// A std::function callback (rather than a DuckDb::Client*) keeps this type free
// of any dependency on the DuckDB translation unit, so it compiles + links in
// builds that don't include duckdb_client at all.
class QueryInterruptToken {
public:
    QueryInterruptToken() = default;
    QueryInterruptToken(const QueryInterruptToken &) = delete;
    QueryInterruptToken &operator=(const QueryInterruptToken &) = delete;

    // Flag the token and invoke any currently-bound interrupt callback. Thread-safe.
    void requestInterrupt();
    bool interruptRequested() const { return m_flag.load(); }

    // Called by runQuery on the worker thread around the interruptible exec().
    // bind() publishes the callback under the lock; unbind() clears it so a late
    // requestInterrupt() can never touch a destroyed Client (the mutex serializes
    // against an in-flight requestInterrupt()).
    void bind(std::function<void()> interruptFn);
    void unbind();

private:
    std::atomic<bool> m_flag{false};
    QMutex m_mx;                          // guards m_interrupt
    std::function<void()> m_interrupt;
};

// `token` (optional) makes a DuckDB query interruptible from another thread —
// pass a QueryInterruptToken and call token->requestInterrupt() to abort a slow
// query mid-flight (timeout / user Stop). nullptr keeps the fully-synchronous
// behaviour. The JSON/QueryResult shape is identical either way.
QueryResult runQuery(const Record &r,
                     const QString &sql,
                     int maxRows,
                     bool allowMutation,
                     QueryInterruptToken *token = nullptr);

// Open the record briefly and read back the list of user tables. Skips
// system catalogues (sys.*, INFORMATION_SCHEMA.*, sqlite_internal_*).
// Returns an empty list if the connection can't be opened. `outOk` is
// set false on failure (useful to distinguish "no tables" from "couldn't
// connect"). Used to drive the Data-mode attached-schema chip + system
// prompt injection so the model knows what's available before it picks
// a query.
QStringList listTables(const Record &r, bool *outOk = nullptr);

// Returns the list of QSqlDatabase driver IDs available at runtime
// (after Qt has loaded its plugins). Used by the dialog to warn when a
// user selects a driver whose plugin isn't installed.
QStringList availableDrivers();

// Symmetric XOR obfuscation. NOT real encryption — it stops casual
// config-leak readers, not a determined attacker. Documented in CHANGELOG.
QString obfuscatePassword(const QString &plainOrObfuscated);

// True if the given driver string requires a host/port (i.e. anything
// except SQLite). Used by the dialog to enable/disable network fields.
bool driverNeedsNetwork(const QString &driver);

} // namespace DbConnections

// Registered so QueryResult can cross a thread boundary via a queued
// signal/slot connection if a caller ever marshals it.
Q_DECLARE_METATYPE(DbConnections::QueryResult)

// ─── DbConnectionsDialog ──────────────────────────────────────────────────
// Modal dialog for CRUDing the connection list. Opens from the AI panel's
// "Manage Connections..." button when Data Analyst Mode is on.
class DbConnectionsDialog : public QDialog {
    Q_OBJECT
public:
    explicit DbConnectionsDialog(QWidget *parent = nullptr);

private slots:
    void onSelectionChanged();
    void onNew();
    void onDelete();
    void onSave();      // commits form state to the in-memory record
    void onTest();      // opens the current form's record without saving
    void onAccept();    // OK — persist all records to disk
    void onDriverChanged(const QString &drv);
    // v0.1.66 — preset dropdown handler. Fills driver / port / placeholder
    // options for the selected template (SQL Server / PostgreSQL / MySQL /
    // SQLite / DuckDB). User can still edit anything afterwards.
    void onPresetChanged(int idx);

private:
    void refreshList(int selectIndex = -1);
    void formFromRecord(const DbConnections::Record &r);
    DbConnections::Record formToRecord() const;
    void setNetworkFieldsEnabled(bool on);
    // v0.1.66 — paints the driver-availability + per-OS install-hint
    // message under the form whenever the driver selection changes.
    // Driver-missing case links to a concrete `apt-get` / `brew` /
    // `vcpkg` command rather than a generic "install the plugin" line.
    void refreshDriverHint();

    QVector<DbConnections::Record> m_records;
    // v0.1.70 — snapshot of the connection count at dialog construction.
    // onAccept compares the current m_records.size() against this; if the
    // user has deleted connections (count went down), a confirmation
    // dialog prompts before overwriting the saved file. Prevents the
    // "Delete + OK = silent wipe" data-loss bug.
    int m_originalCount = 0;

    QListWidget *m_list = nullptr;
    QComboBox   *m_preset = nullptr;
    QLineEdit   *m_name = nullptr;
    QComboBox   *m_driver = nullptr;
    QLineEdit   *m_host = nullptr;
    QSpinBox    *m_port = nullptr;
    QLineEdit   *m_database = nullptr;
    QPushButton *m_browseDb = nullptr;
    QLineEdit   *m_username = nullptr;
    QLineEdit   *m_password = nullptr;
    QLineEdit   *m_options = nullptr;
    QPushButton *m_newBtn = nullptr;
    QPushButton *m_deleteBtn = nullptr;
    QPushButton *m_saveBtn = nullptr;
    QPushButton *m_testBtn = nullptr;
    QLabel      *m_status = nullptr;
    QLabel      *m_driverHint = nullptr;
};

#endif // NOTEPATRA_DBCONNECTIONS_H
