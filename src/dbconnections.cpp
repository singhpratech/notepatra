// SPDX-License-Identifier: GPL-3.0-or-later

#include "dbconnections.h"
#include "config.h"
#ifdef NOTEPATRA_HAVE_DUCKDB
#include "duckdb_client.h"
#endif

#include <QDebug>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QFileDialog>
#include <QRegularExpression>
#include <QSet>
#include <QVector>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSqlDatabase>
#include <QSqlError>
#include <QSqlQuery>
#include <QSqlRecord>
#include <QStandardPaths>
#include <QUuid>
#include <QListWidget>
#include <QLineEdit>
#include <QSpinBox>
#include <QComboBox>
#include <QPushButton>
#include <QLabel>
#include <QHBoxLayout>
#include <QVBoxLayout>
#include <QFormLayout>
#include <QGroupBox>
#include <QMessageBox>
#include <QDialogButtonBox>

namespace DbConnections {

QString configPath() {
    // v0.1.96 — platform-conventional config dir.
    return Config::appConfigDir() + QStringLiteral("/db-connections.json");
}

// ── Password obfuscation ────────────────────────────────────────────────
// XOR-with-fixed-key + base64. NOT real encryption — purpose is to keep
// the plaintext password from sitting in a JSON file that anyone with
// `cat` can read. A determined attacker with the source can decode it
// trivially. Documented in CHANGELOG / release notes.
//
// Symmetric: obfuscatePassword("hello") -> "VW...="
//            obfuscatePassword("VW...=") -> "hello"
// We auto-detect direction: if the input is valid base64 AND decoding
// produces valid UTF-8 after XOR, treat it as obfuscated. Otherwise
// treat it as plaintext that needs to be obfuscated.

static QByteArray xorBytes(const QByteArray &in) {
    static const QByteArray kKey = QByteArrayLiteral(
        "notepatra-v1-pwd-obfuscation-key-not-encryption");
    QByteArray out;
    out.resize(in.size());
    for (int i = 0; i < in.size(); ++i) {
        out[i] = in[i] ^ kKey[i % kKey.size()];
    }
    return out;
}

static bool tryDeobfuscate(const QString &maybe, QString *out) {
    // Quick reject: base64 only contains A-Z a-z 0-9 + / = and is at least
    // length 4. (Empty string round-trips to empty regardless.)
    if (maybe.isEmpty()) { if (out) *out = QString(); return true; }
    if (maybe.length() < 4 || (maybe.length() % 4) != 0) return false;
    for (QChar c : maybe) {
        const ushort u = c.unicode();
        const bool ok = (u >= 'A' && u <= 'Z') || (u >= 'a' && u <= 'z') ||
                        (u >= '0' && u <= '9') || u == '+' || u == '/' || u == '=';
        if (!ok) return false;
    }
    QByteArray decoded = QByteArray::fromBase64(maybe.toUtf8(),
                                                QByteArray::AbortOnBase64DecodingErrors);
    if (decoded.isEmpty()) return false;
    QByteArray xored = xorBytes(decoded);
    // Validate UTF-8 — QString::fromUtf8 is lenient (replaces invalid bytes
    // with U+FFFD), so we round-trip and require byte-equality.
    QString candidate = QString::fromUtf8(xored);
    if (candidate.toUtf8() != xored) return false;
    if (out) *out = candidate;
    return true;
}

QString obfuscatePassword(const QString &plainOrObfuscated) {
    if (plainOrObfuscated.isEmpty()) return QString();
    QString deob;
    if (tryDeobfuscate(plainOrObfuscated, &deob)) {
        // Looked like obfuscated input; return the plaintext.
        return deob;
    }
    // Treat as plaintext; produce the obfuscated form.
    QByteArray xored = xorBytes(plainOrObfuscated.toUtf8());
    return QString::fromLatin1(xored.toBase64());
}

// ── Record ──────────────────────────────────────────────────────────────

QJsonObject Record::toJson() const {
    QJsonObject o;
    o["name"] = name;
    o["driver"] = driver;
    o["host"] = host;
    o["port"] = port;
    o["database"] = database;
    o["username"] = username;
    o["password"] = obfuscatePassword(password);  // obscure at rest
    o["options"] = options;
    return o;
}

Record Record::fromJson(const QJsonObject &o) {
    Record r;
    r.name = o.value("name").toString();
    r.driver = o.value("driver").toString("QSQLITE");
    r.host = o.value("host").toString();
    r.port = o.value("port").toInt(0);
    r.database = o.value("database").toString();
    r.username = o.value("username").toString();
    r.password = obfuscatePassword(o.value("password").toString());  // de-obscure
    r.options = o.value("options").toString();
    return r;
}

// ── Persistence ─────────────────────────────────────────────────────────

QVector<Record> loadAll() {
    QVector<Record> out;
    QFile f(configPath());
    if (!f.open(QIODevice::ReadOnly)) return out;
    const QJsonDocument doc = QJsonDocument::fromJson(f.readAll());
    if (!doc.isArray()) return out;
    for (const auto &v : doc.array()) {
        if (!v.isObject()) continue;
        out.append(Record::fromJson(v.toObject()));
    }
    return out;
}

bool saveAll(const QVector<Record> &records) {
    QString path = configPath();
    QDir().mkpath(QFileInfo(path).path());
    QJsonArray arr;
    for (const auto &r : records) arr.append(r.toJson());
    const QString tmp = path + QStringLiteral(".tmp");
    QFile f(tmp);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate)) return false;
    f.write(QJsonDocument(arr).toJson(QJsonDocument::Indented));
    f.close();
    QFile::remove(path);
    return QFile::rename(tmp, path);
}

bool findByName(const QString &name, Record *out) {
    if (!out) return false;
    const auto all = loadAll();
    for (const auto &r : all) {
        if (r.name == name) { *out = r; return true; }
    }
    return false;
}

QStringList availableDrivers() {
    QStringList ds = QSqlDatabase::drivers();
#ifdef NOTEPATRA_HAVE_DUCKDB
    if (DuckDb::Client::available() && !ds.contains("DUCKDB"))
        ds.append("DUCKDB");
#endif
    return ds;
}

bool driverNeedsNetwork(const QString &driver) {
    return driver != QStringLiteral("QSQLITE")
        && driver != QStringLiteral("DUCKDB");
}

// ── Open + query ────────────────────────────────────────────────────────

// Apply a session-level read-only guard after connecting. Only PostgreSQL
// and MySQL/MariaDB expose a session read-only knob; SQLite and SQL Server
// (QODBC) do not, so on those the SQL classifier is the sole enforcement
// layer (see the gate in runQuery). Returns false + a reason string if the
// SET fails; the caller keeps the connection open (a plain SELECT is still
// fine) but surfaces the warning.
static bool applySessionReadOnly(QSqlDatabase &db, const QString &driver,
                                 QString *outReason) {
    QString stmt;
    if (driver == QStringLiteral("QPSQL")) {
        stmt = QStringLiteral("SET default_transaction_read_only = on");
    } else if (driver == QStringLiteral("QMYSQL")) {
        // Sets the default for subsequent transactions; with autocommit on,
        // each statement is its own transaction and inherits this.
        stmt = QStringLiteral("SET SESSION TRANSACTION READ ONLY");
    } else {
        // QSQLITE / QODBC (SQL Server) / anything else: no session read-only.
        // The single-statement classifier is the enforcement layer there.
        return true;
    }
    QSqlQuery q(db);
    if (!q.exec(stmt)) {
        const QString reason = QStringLiteral(
            "Could not enforce a read-only session on %1: %2. "
            "Falling back to statement-level checks only.")
                .arg(driver, q.lastError().text());
        qWarning().noquote() << "[dbconnections]" << reason;
        if (outReason) *outReason = reason;
        return false;
    }
    return true;
}

bool open(const Record &r, QString *outConnectionName, QString *outError,
          bool readOnly, QString *outReadOnlyWarning) {
    if (!QSqlDatabase::drivers().contains(r.driver)) {
        if (outError) *outError = QStringLiteral(
            "Qt SQL driver '%1' is not available on this system. "
            "Install the appropriate Qt SQL plugin and restart Notepatra.")
                .arg(r.driver);
        return false;
    }
    const QString cname = QStringLiteral("notepatra-%1-%2")
        .arg(r.name, QUuid::createUuid().toString(QUuid::Id128));
    QSqlDatabase db = QSqlDatabase::addDatabase(r.driver, cname);
    if (r.driver == QStringLiteral("QSQLITE")) {
        db.setDatabaseName(r.database);
    } else if (r.driver == QStringLiteral("QODBC")) {
        // v0.1.70 — QODBC fix. Qt's QODBC driver treats setDatabaseName as
        // either a DSN name (single token) OR a full DSN-less ODBC
        // connection string. setHostName/setPort/setUserName/setPassword
        // alone do NOT compose into a valid connection string for QODBC —
        // they're only used by drivers like QPSQL/QMYSQL that speak the
        // wire protocol natively. And setConnectOptions for QODBC takes
        // Qt-specific options (SQL_ATTR_LOGIN_TIMEOUT etc.), NOT raw ODBC
        // keywords like DRIVER={...} — those have to be inside the
        // connection string itself.
        //
        // Symptom before this fix: error "[unixODBC][Driver Manager]Data
        // source name not found and no default driver specified" because
        // Qt passed just "master" (the database name) as the DSN to
        // SQLDriverConnect, and unixODBC couldn't find a DSN with that
        // name in odbcinst.ini.
        //
        // Fix: assemble the FULL connection string from all fields and
        // pass it via setDatabaseName. The Options field can supply the
        // DRIVER={...} (or other keywords); if it does, we splice the
        // host/port/database/uid/pwd around it. If Options is empty we
        // fall back to a minimal SERVER=host,port;DATABASE=db construct.
        QString conn = r.options.trimmed();
        // Ensure terminating semicolon so we can append more keywords.
        if (!conn.isEmpty() && !conn.endsWith(';')) conn += ';';
        // Server is host plus comma-port (the SQL Server / ODBC convention,
        // distinct from the colon-port that PostgreSQL etc. use).
        const QString server = (r.port > 0)
            ? QString("%1,%2").arg(r.host).arg(r.port)
            : r.host;
        if (!server.isEmpty())     conn += QString("SERVER=%1;").arg(server);
        if (!r.database.isEmpty()) conn += QString("DATABASE=%1;").arg(r.database);
        if (!r.username.isEmpty()) conn += QString("UID=%1;").arg(r.username);
        if (!r.password.isEmpty()) conn += QString("PWD=%1;").arg(r.password);
        db.setDatabaseName(conn);
    } else {
        db.setHostName(r.host);
        if (r.port > 0) db.setPort(r.port);
        db.setDatabaseName(r.database);
        db.setUserName(r.username);
        db.setPassword(r.password);
        if (!r.options.isEmpty()) db.setConnectOptions(r.options);
    }
    if (!db.open()) {
        if (outError) *outError = db.lastError().text();
        QSqlDatabase::removeDatabase(cname);
        return false;
    }
    if (readOnly) {
        // Best-effort defense-in-depth: a failed SET is warned about, not
        // fatal — the classifier still gates every non-mutation query.
        applySessionReadOnly(db, r.driver, outReadOnlyWarning);
    }
    if (outConnectionName) *outConnectionName = cname;
    return true;
}

// ── SQL classifier ───────────────────────────────────────────────────────
// A proper tokenizing classifier that replaces the old leading-keyword
// prefix allowlist. The prefix check let through every confirmed bypass:
//   WITH t AS (DELETE FROM x RETURNING *) SELECT * FROM t
//   EXPLAIN ANALYZE DELETE ...          (ANALYZE executes the statement)
//   SELECT * INTO backup FROM x         (creates a table)
//   SELECT 1; DROP TABLE x              (statement stacking)
//   PRAGMA journal_mode=WAL             (writable on SQLite)
// The classifier strips comments + string/identifier literals, tokenizes,
// enforces a single statement, and scans every token (so DML embedded in a
// CTE or wrapped in EXPLAIN is caught).

// Replace comments with a space and string / quoted-identifier literals with
// neutral placeholders, so keywords appearing inside them can never be
// mistaken for SQL verbs. Structural characters (';', parens) are preserved.
static QString stripSqlNoise(const QString &sql) {
    QString out;
    out.reserve(sql.size());
    const int n = sql.size();
    int i = 0;
    while (i < n) {
        const QChar c = sql[i];
        if (c == QLatin1Char('-') && i + 1 < n && sql[i + 1] == QLatin1Char('-')) {
            while (i < n && sql[i] != QLatin1Char('\n')) ++i;
            out += QLatin1Char(' ');
            continue;
        }
        if (c == QLatin1Char('/') && i + 1 < n && sql[i + 1] == QLatin1Char('*')) {
            i += 2;
            while (i + 1 < n && !(sql[i] == QLatin1Char('*') && sql[i + 1] == QLatin1Char('/'))) ++i;
            i = (i + 1 < n) ? i + 2 : n;
            out += QLatin1Char(' ');
            continue;
        }
        if (c == QLatin1Char('\'')) {              // single-quoted string
            ++i;
            while (i < n) {
                if (sql[i] == QLatin1Char('\'')) {
                    if (i + 1 < n && sql[i + 1] == QLatin1Char('\'')) { i += 2; continue; }
                    ++i;
                    break;
                }
                ++i;
            }
            out += QLatin1Char(' ');               // no word chars → drops out
            continue;
        }
        if (c == QLatin1Char('"')) {               // double-quoted identifier
            ++i;
            while (i < n) {
                if (sql[i] == QLatin1Char('"')) {
                    if (i + 1 < n && sql[i + 1] == QLatin1Char('"')) { i += 2; continue; }
                    ++i;
                    break;
                }
                ++i;
            }
            out += QStringLiteral(" qid ");         // neutral placeholder token
            continue;
        }
        if (c == QLatin1Char('`')) {               // backtick identifier (MySQL)
            ++i;
            while (i < n && sql[i] != QLatin1Char('`')) ++i;
            if (i < n) ++i;
            out += QStringLiteral(" qid ");
            continue;
        }
        out += c;
        ++i;
    }
    return out;
}

static QStringList splitStatements(const QString &stripped) {
    QStringList out;
    for (const QString &piece : stripped.split(QLatin1Char(';'))) {
        const QString t = piece.trimmed();
        if (!t.isEmpty()) out.append(t);
    }
    return out;
}

static QStringList wordTokens(const QString &s) {
    QStringList toks;
    QString cur;
    for (const QChar c : s) {
        if (c.isLetterOrNumber() || c == QLatin1Char('_')) {
            cur += c;
        } else if (!cur.isEmpty()) {
            toks.append(cur.toUpper());
            cur.clear();
        }
    }
    if (!cur.isEmpty()) toks.append(cur.toUpper());
    return toks;
}

// A structural token carries the positional context the flat token list throws
// away. It lets the classifier tell a leading VERB from an identifier that
// merely collides with a keyword (SELECT comment FROM posts) and a called
// function from a bare column reference (writefile(...) vs a column "writefile").
struct StructToken {
    QString text;        // uppercased word
    bool verbSlot;       // VERB position: statement start, immediately after
                         // '(' (a CTE/subquery body), right after
                         // EXPLAIN / ANALYZE, or the OUTER-query leading token
                         // of a leading-CTE (WITH …) statement.
    bool callsFunction;  // the next significant char is '(' — a function call
};

// Tokenize an already-noise-stripped statement, tracking structural context.
static QVector<StructToken> structTokens(const QString &stmt) {
    QVector<StructToken> out;
    const int n = stmt.size();
    int i = 0;
    bool atStart = true;         // no word emitted yet
    bool afterOpenParen = false; // last significant char was '('
    QString prevWord;            // immediately preceding word (cleared when any
                                 // non-word significant char intervenes)
    // ── Paren-depth + CTE tracking (F5b) ──────────────────────────────────
    // A leading-CTE statement hides its OUTER verb after the WITH-list's
    // closing ')': in `WITH c AS (SELECT 1) DELETE FROM t` the DELETE sits in
    // no other verb slot, so without this it classifies read-only and executes
    // with NO approval card. We open a verb slot for the first significant
    // token at depth 0 that follows a CTE body's ')' and is not a ',' (which
    // would begin another CTE).
    int parenDepth = 0;
    bool inWith = false;            // statement begins with WITH
    bool topLevelBodyIsCte = false; // the current depth-1 group is a CTE body —
                                    // its '(' opened right after AS / [NOT]
                                    // MATERIALIZED at depth 0, NOT the optional
                                    // name(collist) parens (whose '(' follows
                                    // the CTE NAME, not AS)
    bool pendingOuterStart = false; // a CTE body just closed; the next non-','
                                    // word is the outer query's leading token
    while (i < n) {
        const QChar c = stmt[i];
        if (c.isSpace()) { ++i; continue; }
        if (c.isLetterOrNumber() || c == QLatin1Char('_')) {
            int j = i;
            while (j < n && (stmt[j].isLetterOrNumber() || stmt[j] == QLatin1Char('_'))) ++j;
            const QString up = stmt.mid(i, j - i).toUpper();
            const bool verb = atStart || afterOpenParen
                           || prevWord == QStringLiteral("EXPLAIN")
                           || prevWord == QStringLiteral("ANALYZE")
                           || pendingOuterStart;
            int k = j;
            while (k < n && stmt[k].isSpace()) ++k;
            const bool call = (k < n && stmt[k] == QLatin1Char('('));
            out.append({up, verb, call});
            if (atStart && up == QStringLiteral("WITH")) inWith = true;
            pendingOuterStart = false;  // consumed by the outer-query token
            prevWord = up;
            afterOpenParen = false;
            atStart = false;
            i = j;
            continue;
        }
        // Structural (non-word) character. Track paren depth so a CTE body's
        // top-level ')' is distinguishable from a nested one, and open the
        // outer-query verb slot right after it.
        if (c == QLatin1Char('(')) {
            // The CTE body is the paren group after AS (or [NOT] MATERIALIZED)
            // at depth 0 — the optional name(collist) parens open after the CTE
            // NAME instead, so they must NOT be treated as the body.
            if (inWith && parenDepth == 0
                && (prevWord == QStringLiteral("AS")
                    || prevWord == QStringLiteral("MATERIALIZED"))) {
                topLevelBodyIsCte = true;
            }
            ++parenDepth;
            afterOpenParen = true;
            pendingOuterStart = false;  // a parenthesized outer query gets its
                                        // verb slot from afterOpenParen instead
            prevWord.clear();
            ++i;
            continue;
        }
        if (c == QLatin1Char(')')) {
            if (parenDepth > 0) --parenDepth;
            if (parenDepth == 0 && topLevelBodyIsCte) {
                pendingOuterStart = true;   // outer query begins after this ')'
                topLevelBodyIsCte = false;
            }
            afterOpenParen = false;
            prevWord.clear();
            ++i;
            continue;
        }
        if (c == QLatin1Char(',') && parenDepth == 0) {
            pendingOuterStart = false;      // a depth-0 ',' begins another CTE
            afterOpenParen = false;
            prevWord.clear();
            ++i;
            continue;
        }
        // Any other structural char breaks EXPLAIN/ANALYZE adjacency.
        afterOpenParen = false;
        prevWord.clear();
        ++i;
    }
    return out;
}

// Side-effecting / pass-through FUNCTION names (F2). These carry no forbidden
// VERB token, and a wrapper like OPENQUERY(srv,'DELETE ...') hides its DML in a
// string literal that stripSqlNoise blanks — so a bare-name scan is the only
// way to catch them. Matched only when called (name immediately followed by
// '('); as a plain column reference the same word is an innocent read.
static const QSet<QString> &sideEffectFunctions() {
    static const QSet<QString> k = {
        "OPENQUERY", "OPENROWSET", "OPENDATASOURCE",
        "LOAD_EXTENSION", "WRITEFILE", "READFILE", "EDIT", "FTS3_TOKENIZER",
        "SETVAL", "NEXTVAL", "DBLINK_EXEC", "DBLINK",
        "PG_READ_FILE", "PG_READ_BINARY_FILE", "LO_IMPORT", "LO_EXPORT",
        "XP_CMDSHELL",
        // Postgres admin / signalling functions that mutate server state or
        // kill sessions — call-form only (a column named e.g. "query" stays a
        // read).
        "PG_TERMINATE_BACKEND", "PG_CANCEL_BACKEND", "PG_STAT_RESET",
        "PG_LOGICAL_EMIT_MESSAGE", "PG_RELOAD_CONF",
        // DuckDB pass-through table functions: query('…') / query_table('…')
        // execute an arbitrary nested statement (a DML smuggling vector).
        "QUERY", "QUERY_TABLE"
    };
    return k;
}

// DuckDB filesystem-READING table functions (Full edition). These are
// read-only by SQL semantics, so the read-only classifier passes them — but
// on the MCP run_sql path that makes the sandbox an arbitrary-file-read
// primitive (SELECT * FROM read_text('/home/user/.ssh/id_rsa')). Rejected
// ONLY when restrictFilesystem is true (the MCP path), matched call-form only
// (name immediately followed by '(') so a column/string named the same stays
// safe. Legit ingestion (Client::registerCsv) builds its view in TRUSTED code
// that never calls classifySql, so this denylist never touches ingestion.
static const QSet<QString> &fileReadingFunctions() {
    static const QSet<QString> k = {
        "READ_CSV", "READ_CSV_AUTO", "READ_TEXT", "READ_TEXT_AUTO",
        "READ_BLOB", "READ_PARQUET", "READ_JSON", "READ_JSON_AUTO",
        "READ_JSON_OBJECTS", "READ_NDJSON", "GLOB", "SNIFF_CSV",
        "PARQUET_METADATA", "PARQUET_SCHEMA", "PARQUET_FILE_METADATA",
        "PARQUET_KV_METADATA"
    };
    return k;
}

// Data-modifying / side-effecting keywords. If any appears as a token in a
// (comment/literal-stripped) statement it is not read-only — this is what
// catches DML embedded in a CTE or behind EXPLAIN.
static const QSet<QString> &forbiddenKeywords() {
    static const QSet<QString> k = {
        "INSERT", "UPDATE", "DELETE", "MERGE", "UPSERT", "TRUNCATE",
        "CREATE", "DROP", "ALTER", "RENAME", "REPLACE", "RETURNING",
        "GRANT", "REVOKE", "COMMENT",
        "ATTACH", "DETACH", "COPY", "EXPORT", "IMPORT",
        "INSTALL", "LOAD", "SET", "RESET", "CALL",
        "VACUUM", "CHECKPOINT", "REINDEX", "CLUSTER", "ANALYZE",
        "BEGIN", "START", "COMMIT", "ROLLBACK", "SAVEPOINT",
        "LOCK", "UNLOCK", "PREPARE", "DEALLOCATE", "EXECUTE", "USE"
    };
    return k;
}

// Statement-leading verbs that introduce a read-only query.
static const QSet<QString> &readBaseKeywords() {
    static const QSet<QString> k = {
        "SELECT", "WITH", "EXPLAIN", "SHOW", "DESCRIBE", "DESC",
        "PRAGMA", "TABLE", "VALUES", "FROM", "SUMMARIZE"
    };
    return k;
}

// Verbs whose statement is unambiguously a mutation (drives the isMutation
// verdict used by the human-approval gate).
static const QSet<QString> &mutationBaseKeywords() {
    static const QSet<QString> k = {
        "INSERT", "UPDATE", "DELETE", "MERGE", "UPSERT", "TRUNCATE",
        "CREATE", "DROP", "ALTER", "RENAME", "REPLACE",
        "GRANT", "REVOKE", "COMMENT",
        "ATTACH", "DETACH", "COPY", "EXPORT", "IMPORT",
        "INSTALL", "LOAD", "SET", "RESET", "CALL",
        "VACUUM", "CHECKPOINT", "REINDEX", "CLUSTER"
    };
    return k;
}

// PRAGMA allow-set, split so the parens-assignment bypass (F4) can't slip a
// write past. (a) Introspection pragmas legitimately take a paren argument that
// names an object — the argument is a read, e.g. PRAGMA table_info(users).
static const QSet<QString> &introspectionPragmas() {
    static const QSet<QString> k = {
        "TABLE_INFO", "TABLE_LIST", "INDEX_LIST", "INDEX_INFO", "INDEX_XINFO",
        "FOREIGN_KEY_LIST", "STORAGE_INFO", "DATABASE_LIST", "COLLATION_LIST",
        "FUNCTION_LIST", "MODULE_LIST", "PRAGMA_LIST", "COMPILE_OPTIONS",
        // DuckDB read-only introspection pragmas (may take an optional arg).
        "DATABASE_SIZE", "SHOW_TABLES", "SHOW_TABLES_EXPANDED", "VERSION",
        "COLLATIONS", "SETTINGS", "PLATFORM", "DATABASE_PAGE_COUNT"
    };
    return k;
}

// (b) Bare-value settable pragmas. In pure-read form (no argument) they report
// current state and are allowed. Any argument at all — PRAGMA x = v (assignment
// form) OR PRAGMA x(v) / PRAGMA x v (the SQLite parens/space form that carries
// no '=') — is a persistent write and is rejected + tagged as a mutation so it
// routes to the human approval gate.
static const QSet<QString> &settablePragmas() {
    static const QSet<QString> k = {
        "USER_VERSION", "SCHEMA_VERSION", "APPLICATION_ID",
        "JOURNAL_MODE", "SYNCHRONOUS"
    };
    return k;
}

// Classify one already-stripped statement. Sets *mutation / *readOnly and, on
// rejection, *why.
static void classifyOneStatement(const QString &stmt, bool *mutation,
                                 bool *readOnly, QString *why,
                                 bool restrictFilesystem = false) {
    const QStringList toks = wordTokens(stmt);
    const QVector<StructToken> stoks = structTokens(stmt);
    *mutation = false;
    *readOnly = false;
    if (toks.isEmpty()) { if (why) *why = QStringLiteral("empty statement"); return; }

    const QString base = toks.first();
    const bool hasReturning = toks.contains(QStringLiteral("RETURNING"));
    const bool hasInto = toks.contains(QStringLiteral("INTO"));
    // SELECT ... INTO creates a table; distinguish from INSERT/IMPORT INTO.
    const bool selectInto = hasInto && base != QStringLiteral("INSERT")
                          && base != QStringLiteral("IMPORT")
                          && base != QStringLiteral("REPLACE");

    *mutation = mutationBaseKeywords().contains(base) || hasReturning || selectInto;

    // F2 — side-effecting / pass-through FUNCTION names and the T-SQL
    // NEXT VALUE FOR sequence-consumption keyword sequence. A bare function name
    // carries no forbidden verb, and OPENQUERY(srv,'DELETE ...') hides its DML
    // inside a string literal that stripSqlNoise blanks. Detected here (even
    // under a SELECT base) so they route to the human approval gate: not
    // read-only AND flagged as a mutation.
    for (int idx = 0; idx < stoks.size(); ++idx) {
        const StructToken &tk = stoks[idx];
        if (tk.callsFunction && sideEffectFunctions().contains(tk.text)) {
            *mutation = true;
            if (why) *why = QStringLiteral(
                "call to the side-effecting function '%1' requires approval")
                .arg(tk.text);
            return;
        }
        // MCP run_sql path only: DuckDB filesystem-reading functions are a
        // read-only SQL construct the classifier would otherwise pass, but
        // they leak arbitrary files. Reject call-form use; a column/string
        // literal of the same name (callsFunction==false) stays safe.
        if (restrictFilesystem && tk.callsFunction
            && fileReadingFunctions().contains(tk.text)) {
            if (why) *why = QStringLiteral(
                "filesystem-reading function '%1' is not allowed").arg(tk.text);
            return;
        }
        if (tk.text == QStringLiteral("NEXT") && idx + 2 < stoks.size()
            && stoks[idx + 1].text == QStringLiteral("VALUE")
            && stoks[idx + 2].text == QStringLiteral("FOR")) {
            *mutation = true;
            if (why) *why = QStringLiteral(
                "NEXT VALUE FOR advances a sequence (a state change)");
            return;
        }
    }

    if (!readBaseKeywords().contains(base)) {
        if (why) *why = QStringLiteral("'%1' is not a read-only statement").arg(base);
        return;
    }

    if (base == QStringLiteral("EXPLAIN")) {
        // EXPLAIN ANALYZE actually RUNS the wrapped statement.
        if (toks.contains(QStringLiteral("ANALYZE"))) {
            if (why) *why = QStringLiteral("EXPLAIN ANALYZE executes the statement");
            return;
        }
    }

    if (base == QStringLiteral("PRAGMA")) {
        const QString pname = toks.value(1);
        // (a) Introspection pragma: a paren argument names an object and is a
        // read. Assignment form is still nonsense on these → reject.
        if (introspectionPragmas().contains(pname)) {
            if (stmt.contains(QLatin1Char('='))) {
                if (why) *why = QStringLiteral("assignment-form PRAGMA is not allowed");
                return;
            }
            *readOnly = true;
            return;
        }
        // (b) Settable pragma: allowed ONLY as a pure read (no argument at all).
        // ANY argument — '=' assignment, '(' parens form, or a bare value token
        // after the name (PRAGMA user_version 5) — is a persistent write. This
        // closes the PRAGMA user_version(9999) parens-assignment bypass (F4).
        if (settablePragmas().contains(pname)) {
            const bool hasParen = stmt.contains(QLatin1Char('('));
            const bool hasEq    = stmt.contains(QLatin1Char('='));
            const bool hasValueToken = toks.size() > 2;  // token after the name
            if (hasParen || hasEq || hasValueToken) {
                *mutation = true;
                if (why) *why = QStringLiteral(
                    "settable PRAGMA %1 with an argument is a write").arg(pname);
                return;
            }
            *readOnly = true;
            return;
        }
        if (why) *why = QStringLiteral(
            "PRAGMA %1 is not in the read-only allowlist").arg(pname);
        return;
    }

    // F5 — a DML/DDL/side-effect keyword disqualifies a read ONLY in VERB
    // position: the statement start, a CTE/subquery body right after '(', or
    // immediately after EXPLAIN/ANALYZE. As a select-list item, alias, or a
    // name after FROM/JOIN/WHERE it is an innocent identifier that happens to
    // collide with a keyword (SELECT comment FROM posts). Scan verb slots only.
    for (const StructToken &tk : stoks) {
        if (tk.verbSlot && forbiddenKeywords().contains(tk.text)) {
            // A mutation verb in a verb slot — notably the OUTER verb of a
            // leading-CTE statement (WITH c AS (SELECT 1) DELETE FROM t) — is a
            // genuine write. Surface it to the human-approval gate (isMutation),
            // not just the read gate, so it never runs silently.
            if (mutationBaseKeywords().contains(tk.text)) *mutation = true;
            if (why) *why = QStringLiteral(
                "statement contains the write/side-effect keyword '%1' "
                "in verb position").arg(tk.text);
            return;
        }
    }
    if (selectInto) {
        if (why) *why = QStringLiteral("SELECT ... INTO creates a table");
        return;
    }
    if (hasReturning) {
        if (why) *why = QStringLiteral("RETURNING implies a data modification");
        return;
    }
    *readOnly = true;
}

SqlVerdict classifySql(const QString &sql, bool restrictFilesystem) {
    SqlVerdict v;
    const QStringList stmts = splitStatements(stripSqlNoise(sql));
    if (stmts.isEmpty()) {
        v.reason = QStringLiteral("empty query");
        return v;
    }
    v.singleStatement = (stmts.size() == 1);

    bool anyMutation = false;
    bool allReadOnly = true;
    QString firstReason;
    for (const QString &st : stmts) {
        bool mut = false, ro = false;
        QString why;
        classifyOneStatement(st, &mut, &ro, &why, restrictFilesystem);
        if (mut) anyMutation = true;
        if (!ro) {
            allReadOnly = false;
            if (firstReason.isEmpty()) firstReason = why;
        }
    }

    v.mutation = anyMutation;
    v.readOnly = v.singleStatement && allReadOnly;
    if (!v.readOnly) {
        v.reason = !v.singleStatement
            ? QStringLiteral("multiple statements are not allowed "
                             "(possible statement stacking)")
            : (firstReason.isEmpty()
                   ? QStringLiteral("not a read-only query")
                   : firstReason);
    }
    return v;
}

bool isReadOnlyQuery(const QString &sql) {
    const SqlVerdict v = classifySql(sql);
    return v.singleStatement && v.readOnly;
}

bool isMutation(const QString &sql) {
    return classifySql(sql).mutation;
}

QStringList listTables(const Record &r, bool *outOk) {
    if (outOk) *outOk = false;
    QStringList tables;

    // Per-driver introspection query. Each one filters to user tables only —
    // system catalogues (sys.* on MSSQL, pg_catalog on Postgres, mysql/perf
    // schemas on MySQL, sqlite_* on SQLite) are excluded so the model isn't
    // overwhelmed with hundreds of irrelevant names.
    QString sql;
    if (r.driver == QStringLiteral("QSQLITE")) {
        sql = QStringLiteral(
            "SELECT name FROM sqlite_master WHERE type='table' "
            "AND name NOT LIKE 'sqlite_%' ORDER BY name");
    } else if (r.driver == QStringLiteral("QPSQL")) {
        sql = QStringLiteral(
            "SELECT table_name FROM information_schema.tables "
            "WHERE table_schema NOT IN ('pg_catalog','information_schema') "
            "ORDER BY table_name");
    } else if (r.driver == QStringLiteral("QMYSQL")) {
        sql = QStringLiteral(
            "SELECT table_name FROM information_schema.tables "
            "WHERE table_schema NOT IN ('mysql','information_schema',"
            "'performance_schema','sys') AND table_schema = DATABASE() "
            "ORDER BY table_name");
    } else if (r.driver == QStringLiteral("QODBC")) {
        // SQL Server-flavoured. sys.tables only lists user tables in the
        // current DB (which the connection string's DATABASE= selects).
        sql = QStringLiteral(
            "SELECT name FROM sys.tables ORDER BY name");
    } else {
        return tables;  // DuckDB handled separately via runQuery if needed.
    }

    QueryResult q = runQuery(r, sql, 500, /*allowMutation=*/false);
    if (!q.ok) return tables;
    for (const auto &row : q.rows) {
        if (!row.isEmpty()) tables.append(row.first());
    }
    if (outOk) *outOk = true;
    return tables;
}

#ifdef NOTEPATRA_HAVE_DUCKDB
// v0.1.55 — DuckDB driver path. Routes through the native libduckdb wrapper
// instead of QSqlDatabase. Lets users connect to:
//   * .duckdb files (driver=DUCKDB, database=/path/file.duckdb)
//   * in-memory DBs  (driver=DUCKDB, database=:memory:)
//   * CSV files       (driver=DUCKDB, database=/path/file.csv)
//   * Parquet         (driver=DUCKDB, database=/path/file.parquet)
//   * JSON            (driver=DUCKDB, database=/path/file.json)
//   * S3              (driver=DUCKDB, database=s3://bucket/key,
//                      options=region;access_key_id;secret;session_token)
bool openDuckDbForRecord(const Record &r, bool allowMutation,
                         DuckDb::Client &client,
                         QString *outErr, QString *outErrKind) {
    auto fail = [&](const QString &e, const QString &kind) {
        if (outErr) *outErr = e;
        if (outErrKind) *outErrKind = kind;
        return false;
    };
    if (!DuckDb::Client::available()) {
        return fail(QStringLiteral(
            "DuckDB support not compiled in (rebuild with NOTEPATRA_USE_DUCKDB=ON)"),
            QStringLiteral("no_connection"));
    }
    const QString dbField = r.database.trimmed();
    QString openPath = QStringLiteral(":memory:");
    QString registerCsv, registerParquet, registerJson;
    const bool isS3 = dbField.startsWith("s3://", Qt::CaseInsensitive);
    // Only an actual on-disk DuckDB *database file* is opened READ_ONLY (and
    // only when not mutating). The :memory: scratch DB used to hold CSV /
    // Parquet / JSON views must stay writable for ingestion.
    bool openReadOnly = false;
    if (dbField.isEmpty() || dbField == QStringLiteral(":memory:")) {
        openPath = QStringLiteral(":memory:");
    } else if (dbField.endsWith(".duckdb", Qt::CaseInsensitive)
               || dbField.endsWith(".db", Qt::CaseInsensitive)) {
        openPath = dbField;
        openReadOnly = !allowMutation;
    } else if (dbField.endsWith(".csv", Qt::CaseInsensitive)) {
        registerCsv = dbField;
    } else if (dbField.endsWith(".parquet", Qt::CaseInsensitive)) {
        registerParquet = dbField;
    } else if (dbField.endsWith(".json", Qt::CaseInsensitive)
               || dbField.endsWith(".ndjson", Qt::CaseInsensitive)) {
        registerJson = dbField;
    }  // else: leave as :memory: and let user reference via SQL

    // v0.1.119 — MCP filesystem sandbox. When set, ingest by MATERIALIZING the
    // source into a real in-memory table (read the file exactly once) so that,
    // after the engine-level lockdown below, the untrusted query can still
    // `SELECT * FROM data` but can no longer reach the disk. A VIEW would
    // re-scan the file on query and fail once external access is off.
    const bool sandbox = r.sandboxFilesystem;

    QString openErr;
    if (!client.open(openPath, &openErr, openReadOnly))
        return fail(openErr, QStringLiteral("open_failed"));
    if (!registerCsv.isEmpty()
        && !client.registerCsv(registerCsv, "data", &openErr, sandbox))
        return fail(openErr, QStringLiteral("open_failed"));
    if (!registerParquet.isEmpty()
        && !client.registerParquet(registerParquet, "data", &openErr, sandbox))
        return fail(openErr, QStringLiteral("open_failed"));
    if (!registerJson.isEmpty()
        && !client.registerJson(registerJson, "data", &openErr, sandbox))
        return fail(openErr, QStringLiteral("open_failed"));
    if (isS3) {
        // r.options encodes: region;access_key_id;secret;session_token
        const QStringList parts = r.options.split(QLatin1Char(';'));
        if (!client.configureS3(parts.value(0), parts.value(1),
                                parts.value(2), parts.value(3), &openErr))
            return fail(openErr, QStringLiteral("open_failed"));
    }
    // Lock down the engine LAST — after every legitimate ingestion has read its
    // file. This is the authoritative control that closes the MCP run_sql
    // arbitrary-file-read hole (replacement scan + quoted read_text/read_csv_auto
    // /read_blob/glob), independent of the SQL string classifier.
    if (sandbox && !client.disableExternalAccess(&openErr))
        return fail(openErr, QStringLiteral("open_failed"));
    return true;
}
#endif  // NOTEPATRA_HAVE_DUCKDB

// ── QueryInterruptToken ──────────────────────────────────────────────────
// The mutex serializes bind/unbind (worker thread) against requestInterrupt
// (controller thread), so requestInterrupt can never invoke a callback closed
// over a Client that runQuery has already torn down: unbind() clears m_interrupt
// under the same lock before the stack Client is destroyed. duckdb_interrupt
// itself is documented thread-safe, so invoking the callback (which calls it on
// a live bound connection) from another thread is OK.
void QueryInterruptToken::requestInterrupt() {
    m_flag.store(true);
    QMutexLocker lk(&m_mx);
    if (m_interrupt) m_interrupt();
}
void QueryInterruptToken::bind(std::function<void()> interruptFn) {
    QMutexLocker lk(&m_mx);
    m_interrupt = std::move(interruptFn);
}
void QueryInterruptToken::unbind() {
    QMutexLocker lk(&m_mx);
    m_interrupt = nullptr;
}

QueryResult runQuery(const Record &r,
                     const QString &sql,
                     int maxRows,
                     bool allowMutation,
                     QueryInterruptToken *token) {
    QueryResult out;
    // Authoritative read-only gate: the tokenizing classifier (single
    // statement, no CTE-embedded DML, no EXPLAIN-wrapped DML, no SELECT
    // INTO, no stacking, restricted PRAGMA). allowMutation=true bypasses it
    // only after the ai_tools human-approval gate has cleared the write.
    if (!allowMutation && !isReadOnlyQuery(sql)) {
        const SqlVerdict v = classifySql(sql);
        out.error = QStringLiteral(
            "Only read-only, single-statement queries are allowed by default "
            "(%1). To run a mutation, it must go through the approval gate "
            "(confirm:true after user approval).").arg(v.reason);
        out.errorKind = QStringLiteral("non_select");
        return out;
    }

    if (r.driver.compare(QStringLiteral("DUCKDB"), Qt::CaseInsensitive) == 0) {
#ifndef NOTEPATRA_HAVE_DUCKDB
        out.error = "DuckDB support not compiled in (rebuild with NOTEPATRA_USE_DUCKDB=ON)";
        out.errorKind = QStringLiteral("no_connection");
        return out;
#else
        DuckDb::Client client;
        QString derr, dkind;
        if (!openDuckDbForRecord(r, allowMutation, client, &derr, &dkind)) {
            out.error = derr;
            out.errorKind = dkind;
            return out;
        }
        const int cap = qMax(1, qMin(maxRows, 50000));
        // F1 — make this exec() interruptible from the controller thread. Bind
        // the live client to the token right before exec and unbind right after
        // (no early return in between), so a Stop/timeout on another thread
        // reaches duckdb_interrupt. Honor an interrupt that was requested before
        // exec even started (early cancel) — exec() no longer clears a pending
        // interrupt at its top.
        if (token) {
            token->bind([&client]() { client.requestInterrupt(); });
            if (token->interruptRequested()) client.requestInterrupt();
        }
        DuckDb::ResultSet rs = client.exec(sql, cap);
        if (token) token->unbind();
        if (!rs.errorMessage.isEmpty()) {
            out.error = rs.errorMessage;
            out.errorKind = rs.cancelled ? QStringLiteral("cancelled")
                                         : QStringLiteral("exec_failed");
            return out;
        }
        out.columns = rs.columns;
        out.rowsReturned = rs.rows.size();
        out.truncated = rs.truncated;
        out.rows.reserve(rs.rows.size());
        for (const DuckDb::Row &row : rs.rows) {
            QVector<QString> r2;
            r2.reserve(row.values.size());
            for (const QString &v : row.values) r2.append(v);
            out.rows.append(r2);
        }
        out.ok = true;
        return out;
#endif
    }

    QString cname, openErr, roWarn;
    // Read-only session guard on the server when this isn't an approved
    // mutation. On SQL Server / SQLite (no session read-only) the classifier
    // above is the only layer — documented on applySessionReadOnly.
    if (!open(r, &cname, &openErr, /*readOnly=*/!allowMutation, &roWarn)) {
        out.error = openErr;
        out.errorKind = QStringLiteral("open_failed");
        return out;
    }
    if (!roWarn.isEmpty()) out.warning = roWarn;

    {
        QSqlDatabase db = QSqlDatabase::database(cname);
        QSqlQuery q(db);
        if (!q.exec(sql)) {
            out.error = q.lastError().text();
            out.errorKind = QStringLiteral("exec_failed");
        } else {
            const QSqlRecord rec = q.record();
            const int cols = rec.count();
            for (int i = 0; i < cols; ++i) out.columns.append(rec.fieldName(i));
            const int cap = qMax(1, qMin(maxRows, 50000));
            while (q.next()) {
                if (out.rowsReturned >= cap) {
                    out.truncated = true;
                    break;
                }
                QVector<QString> row;
                row.reserve(cols);
                for (int i = 0; i < cols; ++i) {
                    QVariant v = q.value(i);
                    row.append(v.isNull() ? QString() : v.toString());
                }
                out.rows.append(row);
                ++out.rowsReturned;
            }
            out.ok = (out.errorKind.isEmpty());
        }
    }
    QSqlDatabase::removeDatabase(cname);
    return out;
}

} // namespace DbConnections

// ─── DbConnectionsDialog ───────────────────────────────────────────────────

DbConnectionsDialog::DbConnectionsDialog(QWidget *parent)
    : QDialog(parent) {
    setWindowTitle(tr("Database Connections"));
    resize(720, 460);

    m_records = DbConnections::loadAll();
    m_originalCount = m_records.size();  // v0.1.70 — remembered for the delete-confirm gate

    auto *root = new QHBoxLayout(this);

    // ─── Left: list + new/delete ─
    auto *leftCol = new QVBoxLayout();
    m_list = new QListWidget;
    m_list->setMinimumWidth(220);
    leftCol->addWidget(m_list);
    auto *leftBtns = new QHBoxLayout();
    m_newBtn = new QPushButton(tr("+ New"));
    m_deleteBtn = new QPushButton(tr("Delete"));
    leftBtns->addWidget(m_newBtn);
    leftBtns->addWidget(m_deleteBtn);
    leftCol->addLayout(leftBtns);
    root->addLayout(leftCol);

    // ─── Right: form ─
    auto *form = new QFormLayout();

    // v0.1.66 — Preset dropdown above all the fields. Picking a preset
    // populates driver / port / placeholder options with sane templates,
    // so users don't have to know that SQL Server lives behind QODBC
    // with a "DRIVER={...}" connection string, or that PostgreSQL's
    // default port is 5432. They can still edit anything afterwards.
    m_preset = new QComboBox;
    m_preset->addItems({
        tr("(custom — pick a driver below)"),
        tr("SQL Server (localhost, ODBC)"),
        tr("SQL Server (Notepatra local Docker, port 14330)"),
        tr("SQL Server Express (named instance, ODBC)"),
        tr("Azure SQL Database (ODBC)"),
        tr("PostgreSQL (localhost)"),
        tr("MySQL / MariaDB (localhost)"),
        tr("SQLite (file on disk)"),
        tr("DuckDB (file or :memory:)"),
    });

    m_name = new QLineEdit;
    m_driver = new QComboBox;
    m_driver->addItems({"QSQLITE", "QPSQL", "QMYSQL", "QODBC", "DUCKDB"});
    m_host = new QLineEdit;
    m_port = new QSpinBox;
    m_port->setRange(0, 65535);
    m_port->setValue(0);
    m_port->setSpecialValueText(tr("default"));
    auto *dbRow = new QHBoxLayout;
    m_database = new QLineEdit;
    m_browseDb = new QPushButton(tr("Browse..."));
    dbRow->addWidget(m_database);
    dbRow->addWidget(m_browseDb);
    m_username = new QLineEdit;
    m_password = new QLineEdit;
    m_password->setEchoMode(QLineEdit::Password);
    m_options = new QLineEdit;

    form->addRow(tr("Preset:"), m_preset);
    form->addRow(tr("Name:"), m_name);
    form->addRow(tr("Driver:"), m_driver);
    form->addRow(tr("Host:"), m_host);
    form->addRow(tr("Port:"), m_port);
    auto *dbWrap = new QWidget;
    dbWrap->setLayout(dbRow);
    form->addRow(tr("Database:"), dbWrap);
    form->addRow(tr("Username:"), m_username);
    form->addRow(tr("Password:"), m_password);
    form->addRow(tr("Options:"), m_options);

    // v0.1.66 — driver availability + install hint, refreshed on every
    // driver change. Lives just under the form so the user sees actionable
    // guidance ("Install msodbcsql18 — see…") instead of generic "plugin
    // not available" text.
    m_driverHint = new QLabel;
    m_driverHint->setWordWrap(true);
    m_driverHint->setOpenExternalLinks(true);
    m_driverHint->setTextFormat(Qt::RichText);
    m_driverHint->setStyleSheet("color: #888; font-size: 11px;");
    form->addRow(QString(), m_driverHint);

    auto *rightCol = new QVBoxLayout();
    rightCol->addLayout(form);

    auto *rightBtns = new QHBoxLayout();
    m_saveBtn = new QPushButton(tr("Save Changes"));
    m_testBtn = new QPushButton(tr("Test"));
    rightBtns->addWidget(m_saveBtn);
    rightBtns->addWidget(m_testBtn);
    rightBtns->addStretch();
    rightCol->addLayout(rightBtns);

    m_status = new QLabel;
    m_status->setWordWrap(true);
    m_status->setStyleSheet("color: #888; font-style: italic;");
    rightCol->addWidget(m_status);

    auto *box = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    rightCol->addWidget(box);

    root->addLayout(rightCol, 1);

    // v0.1.66 — high-level availability summary stays on the status row
    // below the action buttons; per-driver install detail lives on the
    // form's driverHint label (filled by refreshDriverHint()).
    const QStringList have = DbConnections::availableDrivers();
    QStringList missing;
    for (const QString &d : {"QSQLITE", "QPSQL", "QMYSQL", "QODBC", "DUCKDB"}) {
        if (!have.contains(d)) missing.append(d);
    }
    if (!missing.isEmpty()) {
        m_status->setText(tr("Drivers not installed on this system: %1.")
                              .arg(missing.join(", ")));
    } else {
        m_status->setText(tr("All drivers available."));
    }

    refreshList();

    connect(m_list, &QListWidget::currentRowChanged,
            this, &DbConnectionsDialog::onSelectionChanged);
    connect(m_newBtn, &QPushButton::clicked, this, &DbConnectionsDialog::onNew);
    connect(m_deleteBtn, &QPushButton::clicked, this, &DbConnectionsDialog::onDelete);
    connect(m_saveBtn, &QPushButton::clicked, this, &DbConnectionsDialog::onSave);
    connect(m_testBtn, &QPushButton::clicked, this, &DbConnectionsDialog::onTest);
    connect(box, &QDialogButtonBox::accepted, this, &DbConnectionsDialog::onAccept);
    connect(box, &QDialogButtonBox::rejected, this, &QDialog::reject);
    connect(m_driver, &QComboBox::currentTextChanged,
            this, &DbConnectionsDialog::onDriverChanged);
    connect(m_preset, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &DbConnectionsDialog::onPresetChanged);
    connect(m_browseDb, &QPushButton::clicked, this, [this]() {
        // Filter set varies per driver: SQLite shows .db files; DuckDB
        // accepts a wider mix (its own files + CSV/Parquet/JSON sources).
        const QString drv = m_driver->currentText();
        QString filter;
        if (drv == QStringLiteral("DUCKDB")) {
            filter = tr("All sources (*.duckdb *.db *.csv *.parquet *.json);;"
                       "DuckDB files (*.duckdb *.db);;"
                       "CSV (*.csv);;"
                       "Parquet (*.parquet);;"
                       "JSON (*.json *.ndjson);;"
                       "All files (*)");
        } else {
            filter = tr("SQLite databases (*.db *.sqlite *.sqlite3);;All files (*)");
        }
        const QString f = QFileDialog::getOpenFileName(
            this, tr("Select database file"), QString(), filter);
        if (!f.isEmpty()) m_database->setText(f);
    });
}

void DbConnectionsDialog::refreshList(int selectIndex) {
    m_list->clear();
    for (const auto &r : m_records) {
        QString label = r.name.isEmpty() ? tr("(unnamed)") : r.name;
        label += QStringLiteral("    [%1]").arg(r.driver);
        m_list->addItem(label);
    }
    if (selectIndex < 0 && !m_records.isEmpty()) selectIndex = 0;
    if (selectIndex >= 0 && selectIndex < m_records.size()) {
        m_list->setCurrentRow(selectIndex);
    } else {
        // No selection — clear form
        formFromRecord(DbConnections::Record{});
    }
}

void DbConnectionsDialog::onSelectionChanged() {
    int row = m_list->currentRow();
    if (row < 0 || row >= m_records.size()) return;
    formFromRecord(m_records[row]);
}

void DbConnectionsDialog::onNew() {
    DbConnections::Record r;
    r.name = QStringLiteral("New connection %1").arg(m_records.size() + 1);
    r.driver = QStringLiteral("QSQLITE");
    m_records.append(r);
    refreshList(m_records.size() - 1);
}

void DbConnectionsDialog::onDelete() {
    int row = m_list->currentRow();
    if (row < 0 || row >= m_records.size()) return;
    if (QMessageBox::question(this, tr("Delete connection?"),
            tr("Delete '%1'? This cannot be undone.").arg(m_records[row].name))
        != QMessageBox::Yes) return;
    m_records.remove(row);
    refreshList(qMin(row, m_records.size() - 1));
}

void DbConnectionsDialog::onSave() {
    int row = m_list->currentRow();
    if (row < 0 || row >= m_records.size()) return;
    m_records[row] = formToRecord();
    refreshList(row);
    m_status->setText(tr("Saved changes (not yet written to disk — click OK to commit)."));
}

void DbConnectionsDialog::onTest() {
    DbConnections::Record r = formToRecord();
    QString cname, err;
    if (DbConnections::open(r, &cname, &err)) {
        QSqlDatabase::removeDatabase(cname);
        m_status->setText(tr("✓ Connected successfully."));
        m_status->setStyleSheet("color: #2c8c2c; font-weight: 600;");
    } else {
        m_status->setText(tr("✗ %1").arg(err));
        m_status->setStyleSheet("color: #c0392b; font-weight: 600;");
    }
}

void DbConnectionsDialog::onAccept() {
    // If there's an active editing row whose changes haven't been "saved",
    // grab them so the user doesn't lose work.
    int row = m_list->currentRow();
    if (row >= 0 && row < m_records.size()) {
        m_records[row] = formToRecord();
    }

    // v0.1.70 — confirm destructive saves. If the user has deleted
    // connections (current count < original) — including the "delete
    // everything" case — show an explicit warning. This was the source
    // of silent data loss: open dialog with N connections, click Delete
    // a few times to clean up, click OK without realising the empty
    // list overwrites the file. Cancel keeps the dialog open so the
    // user can recover (add the connection back, or click Cancel to
    // abort entirely).
    if (m_records.size() < m_originalCount) {
        const int deleted = m_originalCount - m_records.size();
        const QString detail = m_records.isEmpty()
            ? tr("You are about to delete <b>all %1</b> saved database "
                 "connection%2. The saved-connections file will be wiped.")
                  .arg(m_originalCount)
                  .arg(m_originalCount == 1 ? "" : "s")
            : tr("You are about to remove <b>%1</b> saved database "
                 "connection%2 (was %3, now %4). The change writes "
                 "immediately to disk.")
                  .arg(deleted)
                  .arg(deleted == 1 ? "" : "s")
                  .arg(m_originalCount)
                  .arg(m_records.size());
        const auto reply = QMessageBox::warning(this,
            tr("Confirm deletion"),
            detail + "<br><br>" +
                tr("<b>This cannot be undone.</b> Continue?"),
            QMessageBox::Yes | QMessageBox::Cancel,
            QMessageBox::Cancel);
        if (reply != QMessageBox::Yes) {
            // Don't save, don't close. User can hit Cancel again to
            // abort entirely or undo their deletes manually.
            return;
        }
    }

    if (DbConnections::saveAll(m_records)) {
        accept();
    } else {
        QMessageBox::critical(this, tr("Save failed"),
            tr("Could not write %1").arg(DbConnections::configPath()));
    }
}

// v0.1.66 — per-driver default port. Returns 0 ("default") for SQLite /
// DuckDB which don't open TCP sockets. Used by onDriverChanged to populate
// the port field whenever the user picks a driver and the port is still
// at its zero default — saves the user from looking up that QODBC + SQL
// Server wants 1433, QPSQL wants 5432, QMYSQL wants 3306.
static int defaultPortForDriver(const QString &drv) {
    if (drv == QLatin1String("QODBC"))  return 1433; // SQL Server — most common QODBC use
    if (drv == QLatin1String("QPSQL"))  return 5432;
    if (drv == QLatin1String("QMYSQL")) return 3306;
    return 0; // QSQLITE / DUCKDB — file-based
}

void DbConnectionsDialog::onDriverChanged(const QString &drv) {
    setNetworkFieldsEnabled(DbConnections::driverNeedsNetwork(drv));
    if (drv == QStringLiteral("QSQLITE")) {
        m_browseDb->setVisible(true);
        m_database->setPlaceholderText(tr("/path/to/your.db"));
        m_options->setPlaceholderText(
            tr("Optional: QSQLITE_OPEN_URI=1, QSQLITE_BUSY_TIMEOUT=5000, etc."));
        m_host->setPlaceholderText(QString());
        m_username->setPlaceholderText(QString());
    } else if (drv == QStringLiteral("DUCKDB")) {
        // v0.1.55 — DuckDB path uses the Database field as a multi-mode
        // source: DuckDB file, in-memory, CSV, Parquet, JSON, or S3.
        m_browseDb->setVisible(true);
        m_database->setPlaceholderText(tr(
            ":memory:    or    /path/to.duckdb    or    /path/to.csv    "
            "or    /path/to.parquet    or    s3://bucket/key"));
        m_options->setPlaceholderText(tr(
            "S3 only: region;access_key_id;secret;session_token  "
            "(leave empty for non-S3)"));
        m_host->setPlaceholderText(QString());
        m_username->setPlaceholderText(QString());
    } else if (drv == QStringLiteral("QODBC")) {
        // SQL Server is the dominant QODBC use case. Surface the canonical
        // connection-string template + a hint about Windows Auth so users
        // don't have to memorise the `DRIVER={...}` incantation.
        m_browseDb->setVisible(false);
        m_database->setPlaceholderText(tr("e.g. master, NotepatraTest, tempdb"));
        m_host->setPlaceholderText(tr("127.0.0.1   (or SERVER\\INSTANCE for named instances)"));
        m_username->setPlaceholderText(tr("sa   (leave empty for Windows Auth)"));
        m_options->setPlaceholderText(tr(
            "DRIVER={ODBC Driver 18 for SQL Server};Encrypt=no   "
            "(add Trusted_Connection=yes for Windows Auth)"));
    } else if (drv == QStringLiteral("QPSQL")) {
        m_browseDb->setVisible(false);
        m_database->setPlaceholderText(tr("e.g. postgres, myapp_dev"));
        m_host->setPlaceholderText(tr("127.0.0.1"));
        m_username->setPlaceholderText(tr("postgres"));
        m_options->setPlaceholderText(tr(
            "Optional: sslmode=disable;connect_timeout=10  "
            "(use sslmode=require for production / managed PG)"));
    } else if (drv == QStringLiteral("QMYSQL")) {
        m_browseDb->setVisible(false);
        m_database->setPlaceholderText(tr("e.g. mysql, myapp_dev"));
        m_host->setPlaceholderText(tr("127.0.0.1"));
        m_username->setPlaceholderText(tr("root"));
        m_options->setPlaceholderText(tr(
            "Optional: SSL_CA=/path/to/ca.pem;MYSQL_OPT_CONNECT_TIMEOUT=10"));
    } else {
        m_browseDb->setVisible(false);
        m_database->setPlaceholderText(QString());
        m_options->setPlaceholderText(QString());
        m_host->setPlaceholderText(QString());
        m_username->setPlaceholderText(QString());
    }

    // Auto-fill the port ONLY if the user hasn't customised it (still at
    // the zero "default" value). Don't clobber an explicit non-zero port.
    const int dport = defaultPortForDriver(drv);
    if (m_port->isEnabled() && m_port->value() == 0 && dport > 0) {
        m_port->setValue(dport);
    }

    refreshDriverHint();
}

// v0.1.66 — preset applies a template + flips the driver. The "(custom)"
// entry (idx 0) is a no-op so users can return to free-form editing.
void DbConnectionsDialog::onPresetChanged(int idx) {
    if (idx <= 0) return; // (custom) — leave the form alone

    // Block signal recursion: setting m_driver below fires onDriverChanged,
    // which in turn calls refreshDriverHint(). We want that — but we don't
    // want onPresetChanged to fire again from a programmatic m_preset edit.
    QSignalBlocker bp(m_preset);

    switch (idx) {
    case 1: // SQL Server (localhost, ODBC) — generic 1433
        m_driver->setCurrentText("QODBC");
        m_host->setText("127.0.0.1");
        m_port->setValue(1433);
        m_database->setText("master");
        m_username->setText("sa");
        m_password->setText("");  // user types their own SA password
        m_options->setText("DRIVER={ODBC Driver 18 for SQL Server};Encrypt=no");
        break;
    case 2: // SQL Server — Notepatra local Docker harness on port 14330
        // Matches the bundled docker/sql-server-local.yml topology: port
        // 14330 (not 1433) so this harness coexists with any other local
        // SQL Server already on the default port. Pre-fills everything
        // EXCEPT the password — embedding a secret in a shipping preset
        // is a security smell, and v0.1.70 explicitly avoids it. The
        // password lives in docker/sql-server-local.yml's
        // MSSQL_SA_PASSWORD line; the user copies it once after they
        // spin up the harness. Pre-reqs: libqt5sql5-odbc +
        // msodbcsql18 on the host (see the in-dialog driver hint).
        m_driver->setCurrentText("QODBC");
        m_host->setText("127.0.0.1");
        m_port->setValue(14330);
        m_database->setText("NotepatraTest");
        m_username->setText("sa");
        m_password->setText("");
        m_password->setPlaceholderText(
            tr("paste from MSSQL_SA_PASSWORD in docker/sql-server-local.yml"));
        m_options->setText("DRIVER={ODBC Driver 18 for SQL Server};Encrypt=no");
        break;
    case 3: // SQL Server Express (named instance, ODBC)
        m_driver->setCurrentText("QODBC");
        m_host->setText("localhost\\SQLEXPRESS");
        m_port->setValue(0);  // Named instances use Browser service, not 1433
        m_database->setText("master");
        m_username->setText("");
        m_options->setText("DRIVER={ODBC Driver 18 for SQL Server};Trusted_Connection=yes;Encrypt=no");
        break;
    case 4: // Azure SQL Database (ODBC)
        m_driver->setCurrentText("QODBC");
        m_host->setText("yourserver.database.windows.net");
        m_port->setValue(1433);
        m_database->setText("");
        m_username->setText("admin@yourserver");
        m_options->setText(
            "DRIVER={ODBC Driver 18 for SQL Server};Encrypt=yes;TrustServerCertificate=no;Connection Timeout=30");
        break;
    case 5: // PostgreSQL (localhost)
        m_driver->setCurrentText("QPSQL");
        m_host->setText("127.0.0.1");
        m_port->setValue(5432);
        m_database->setText("postgres");
        m_username->setText("postgres");
        m_options->setText("connect_timeout=10");
        break;
    case 6: // MySQL / MariaDB (localhost)
        m_driver->setCurrentText("QMYSQL");
        m_host->setText("127.0.0.1");
        m_port->setValue(3306);
        m_database->setText("mysql");
        m_username->setText("root");
        m_options->setText("MYSQL_OPT_CONNECT_TIMEOUT=10");
        break;
    case 7: // SQLite (file on disk)
        m_driver->setCurrentText("QSQLITE");
        m_host->setText("");
        m_port->setValue(0);
        m_database->setText("");
        m_database->setFocus();  // user picks the path next
        m_username->setText("");
        m_options->setText("");
        break;
    case 8: // DuckDB (file or :memory:)
        m_driver->setCurrentText("DUCKDB");
        m_host->setText("");
        m_port->setValue(0);
        m_database->setText(":memory:");
        m_username->setText("");
        m_options->setText("");
        break;
    default:
        break;
    }

    refreshDriverHint();
}

// v0.1.66 — fills m_driverHint with driver-specific install / usage
// guidance. Two cases:
//   (1) the Qt SQL plugin for the selected driver is missing → emit a
//       per-OS install command that actually works (apt / brew / etc.).
//   (2) the plugin is present → emit a short usage note (e.g. "Use
//       127.0.0.1\\SQLEXPRESS for SQL Server Express named instances").
void DbConnectionsDialog::refreshDriverHint() {
    if (!m_driverHint) return;

    const QString drv = m_driver->currentText();
    const QStringList have = DbConnections::availableDrivers();
    const bool installed = have.contains(drv);

    if (!installed) {
        // Driver plugin missing — show actionable install commands.
        QString cmd;
        if (drv == QLatin1String("QODBC")) {
            cmd =
                "<b>SQL Server / ODBC driver is missing.</b><br>"
                "<b>Linux (Debian/Ubuntu):</b> install Qt's ODBC plugin <i>and</i> "
                "Microsoft's ODBC driver:<br>"
                "<code>sudo apt-get install libqt5sql5-odbc unixodbc-dev</code><br>"
                "<code>curl https://packages.microsoft.com/keys/microsoft.asc | sudo gpg --dearmor -o /usr/share/keyrings/microsoft-prod.gpg</code><br>"
                "<code>sudo ACCEPT_EULA=Y apt-get install msodbcsql18</code><br>"
                "<b>macOS:</b> <code>brew tap microsoft/mssql-release &amp;&amp; HOMEBREW_ACCEPT_EULA=Y brew install msodbcsql18 unixodbc</code><br>"
                "<b>Windows:</b> already bundled with Qt — restart Notepatra.";
        } else if (drv == QLatin1String("QPSQL")) {
            cmd =
                "<b>PostgreSQL driver is missing.</b><br>"
                "<b>Linux:</b> <code>sudo apt-get install libqt5sql5-psql</code><br>"
                "<b>macOS:</b> already bundled with <code>brew install qt@5</code><br>"
                "<b>Windows:</b> already bundled with Qt — restart Notepatra.";
        } else if (drv == QLatin1String("QMYSQL")) {
            cmd =
                "<b>MySQL driver is missing.</b><br>"
                "<b>Linux:</b> <code>sudo apt-get install libqt5sql5-mysql</code><br>"
                "<b>macOS:</b> already bundled with <code>brew install qt@5</code><br>"
                "<b>Windows:</b> already bundled with Qt — restart Notepatra.";
        } else if (drv == QLatin1String("DUCKDB")) {
            cmd = "<b>DuckDB isn't in this Lite build.</b> Download the <b>Full</b> "
                  "release — Linux, macOS and Windows Full all bundle the DuckDB "
                  "v1.1.3 engine next to the binary. (Developers can also build the "
                  "Full flavor from source with <code>-DNOTEPATRA_FULL=ON</code>.)";
        } else {
            cmd = tr("<b>%1 plugin is not available on this system.</b>").arg(drv);
        }
        m_driverHint->setText(cmd);
        m_driverHint->setStyleSheet("color: #b8860b; font-size: 11px;");
        return;
    }

    // Driver present — show usage hint (helps users avoid first-try blunders).
    QString hint;
    if (drv == QLatin1String("QODBC")) {
        hint = tr(
            "<b>Tip:</b> for a local SQL Server, run "
            "<code>bash scripts/sql-server-local-setup.sh</code> "
            "to spin up a Docker container + seed a sample DB. "
            "Named instances (<code>SERVER\\SQLEXPRESS</code>) use the SQL Browser "
            "service — leave Port at <i>default</i>. "
            "Windows Auth: clear Username + add <code>Trusted_Connection=yes</code> in Options.");
    } else if (drv == QLatin1String("QPSQL")) {
        hint = tr(
            "<b>Tip:</b> for managed Postgres (RDS, Cloud SQL, Neon), add "
            "<code>sslmode=require</code> in Options. For Unix-socket connections, "
            "leave Host empty and put the socket dir in Options as "
            "<code>host=/var/run/postgresql</code>.");
    } else if (drv == QLatin1String("QMYSQL")) {
        hint = tr(
            "<b>Tip:</b> for TLS-required servers (RDS, PlanetScale), add "
            "<code>SSL_CA=/path/to/ca.pem</code> in Options. "
            "For Unix sockets, set Host to an empty string and add "
            "<code>UNIX_SOCKET=/var/run/mysqld/mysqld.sock</code> in Options.");
    } else if (drv == QLatin1String("QSQLITE")) {
        hint = tr(
            "<b>Tip:</b> SQLite is file-based — no host / port / credentials. "
            "Use <b>Browse…</b> to pick the .db file. The file is created if it "
            "doesn't exist on first connect.");
    } else if (drv == QLatin1String("DUCKDB")) {
        hint = tr(
            "<b>Tip:</b> the Database field accepts <code>:memory:</code>, a .duckdb file, "
            "or a path to CSV / Parquet / JSON / S3 — DuckDB reads them all directly. "
            "For S3 sources, fill Options with "
            "<code>region;access_key_id;secret;session_token</code>.");
    }
    m_driverHint->setText(hint);
    m_driverHint->setStyleSheet("color: #2c8c2c; font-size: 11px;");
}

void DbConnectionsDialog::setNetworkFieldsEnabled(bool on) {
    m_host->setEnabled(on);
    m_port->setEnabled(on);
    m_username->setEnabled(on);
    m_password->setEnabled(on);
    m_options->setEnabled(on);
}

void DbConnectionsDialog::formFromRecord(const DbConnections::Record &r) {
    m_name->setText(r.name);
    int driverIdx = m_driver->findText(r.driver);
    m_driver->setCurrentIndex(driverIdx >= 0 ? driverIdx : 0);
    m_host->setText(r.host);
    m_port->setValue(r.port);
    m_database->setText(r.database);
    m_username->setText(r.username);
    m_password->setText(r.password);
    m_options->setText(r.options);
    setNetworkFieldsEnabled(DbConnections::driverNeedsNetwork(m_driver->currentText()));
    m_browseDb->setVisible(m_driver->currentText() == "QSQLITE");
}

DbConnections::Record DbConnectionsDialog::formToRecord() const {
    DbConnections::Record r;
    r.name = m_name->text().trimmed();
    r.driver = m_driver->currentText();
    r.host = m_host->text().trimmed();
    r.port = m_port->value();
    r.database = m_database->text().trimmed();
    r.username = m_username->text();
    r.password = m_password->text();
    r.options = m_options->text();
    return r;
}
