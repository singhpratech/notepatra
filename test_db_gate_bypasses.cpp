// SPDX-License-Identifier: GPL-3.0-or-later
//
// READ-ONLY SQL GATE — confirmed bypasses from the v0.1.133 adversarial review.
//
// Each REJECT row went through DbConnections::classifySql as read-only before
// the fix (several then ran on DuckDB): nested block comments, EXPLAIN option
// spellings that execute, Postgres U&"..." names, missing side-effect
// functions and locking reads, SQL Server batch statements with no ';', and
// DuckDB file scans through quoted / dotted table names. Every REJECT row also
// asserts the reason names the rule, and write rows assert the mutation flag
// that routes them to the approval card. ACCEPT rows pin the reads each new
// rule must leave alone.

#include "dbconnections.h"

#include <QCoreApplication>
#include <QString>
#include <QTemporaryDir>
#include <cstdio>

using DbConnections::SqlVerdict;
using DbConnections::classifySql;
using DbConnections::classifyApprovedWrite;

static int g_pass = 0, g_fail = 0;

// Std = desktop gate; RF = AI / MCP path (no file reads); Strict = QODBC batch
// verdict; StrictRF = the AI path on a QODBC connection.
enum Mode { Std, RF, Strict, StrictRF };

static SqlVerdict verdict(Mode m, const char *sql) {
    return classifySql(QString::fromUtf8(sql), m == RF || m == StrictRF,
                       m == Strict || m == StrictRF);
}

static const char *modeName(Mode m) {
    switch (m) {
    case Std: return "std";
    case RF: return "rf";
    case Strict: return "strict";
    case StrictRF: return "strictrf";
    }
    return "?";
}

static QString shown(const char *sql) {
    QString s = QString::fromUtf8(sql);
    s.replace(QLatin1Char('\r'), QStringLiteral("\\r")).replace(QLatin1Char('\n'), QStringLiteral("\\n"));
    s.replace(QChar(0x0B), QStringLiteral("\\v")).replace(QChar(0x0C), QStringLiteral("\\f"));
    s.replace(QChar(0x85), QStringLiteral("\\u0085")).replace(QChar(0x2028), QStringLiteral("\\u2028"));
    s.replace(QChar(0x2029), QStringLiteral("\\u2029"));
    return s;
}

struct Reject {
    const char *item;
    Mode mode;
    const char *sql;
    const char *reason;  // substring the rejection reason must contain
    bool mutation;       // must also be flagged for the approval card
};

struct Accept {
    const char *item;
    Mode mode;
    const char *sql;
};

static const char *const kNested = "nested comments";
static const char *const kWraps = "wraps a statement that is not read-only";
static const char *const kUnicode = "Unicode-escaped";
static const char *const kLock = "row-locking clause";
static const char *const kTsql = "T-SQL";
static const char *const kFileStr = "quoted string used as a table name";
static const char *const kFilePath = "looks like a file path";
static const char *const kFileDot = "data-file extension";

static const Reject kReject[] = {
    // ── 1. nested block comments ──────────────────────────────────────────
    {"1", Std,      "SELECT 1 /* /* */ -- */ ; DELETE FROM users", kNested, true},
    {"1", RF,       "SELECT 1 /* /* */ -- */ ; DELETE FROM users", kNested, true},
    {"1", Strict,   "SELECT 1 /* /* */ -- */ ; DELETE FROM users", kNested, true},
    {"1", Std,      "SELECT 1 /* /* */ ' */ ; DELETE FROM users", kNested, true},
    {"1", Std,      "SELECT 1 /* /* */ \" */ ; DELETE FROM users", kNested, true},
    {"1", Std,      "SELECT 1 /* /* */ ` */ ; DELETE FROM users", kNested, true},
    {"1", Strict,   "SELECT name FROM sys.tables /* /* */ -- */ DROP TABLE dbo.users", kNested, true},
    {"1", StrictRF, "SELECT name FROM sys.tables /* /* */ -- */ DROP TABLE dbo.users", kNested, true},
    {"1", Strict,   "SELECT 1 /*/* */ -- */ EXEC xp_cmdshell 'whoami'", kNested, true},
    {"1", Std,      "PIVOT t ON a USING sum(b) /* /* */ -- */ ; DELETE FROM t", kNested, true},
    {"1", RF,       "PIVOT t ON a USING sum(b) /* /* */ -- */ ; DELETE FROM t", kNested, true},
    {"1", Std,      "SELECT 1 /* /* /* */ */ -- */ ; DELETE FROM t", kNested, true},
    {"1", Std,      "/* /* */ SELECT 1 */ DELETE FROM t", kNested, true},
    {"1", Std,      "SELECT 1 /* /*\n*/ -- */\n; DELETE FROM t", kNested, true},
    {"1", Std,      "SELECT 1 /* /*\r*/ -- */\r; DELETE FROM t", kNested, true},
    {"1", Std,      "SELECT 1 /* /*\r\n*/ -- */\r\n; DELETE FROM t", kNested, true},
    {"1", Std,      "SELECT 1 /* /*\xE2\x80\xA8*/ -- */\xE2\x80\xA8; DELETE FROM t", kNested, true},
    {"1", Std,      "SELECT 1 /*! /* */ ; DELETE FROM t */", kNested, true},
    {"1", Std,      "SELECT 1 /*/*/ -- */ ; DELETE FROM t", kNested, false},  // overlapping opener
    {"1", Std,      "SELECT 1 /*/**/*/ ; DELETE FROM t", kNested, true},
    {"1", Std,      "SELECT 1 /* x /* */", kNested, false},

    // ── 2. EXPLAIN / DESCRIBE wrapping a statement that is not read-only ──
    {"2", Std, "EXPLAIN ANALYSE DELETE FROM users", kWraps, true},
    {"2", Std, "EXPLAIN (ANALYSE) DELETE FROM users", kWraps, true},
    {"2", Std, "EXPLAIN (\"analyze\") DELETE FROM users", kWraps, true},
    {"2", Std, "EXPLAIN ANALYSE DROP TABLE users", kWraps, true},
    {"2", Std, "EXPLAIN ANALYSE COPY (SELECT 'x') TO '/file'", kWraps, true},
    {"2", RF,  "EXPLAIN ANALYSE COPY (SELECT 'x') TO '/file'", kWraps, true},
    {"2", Std, "EXPLAIN (ANALYZE, BUFFERS, FORMAT JSON) UPDATE t SET a = 1", kWraps, true},
    {"2", Std, "EXPLAIN (FORMAT JSON, ANALYZE) DELETE FROM t1", kWraps, true},
    {"2", Std, "EXPLAIN (analyze true) INSERT INTO t1 VALUES (5)", kWraps, true},
    {"2", Std, "EXPLAIN (ANALYZE 1, VERBOSE on) DELETE FROM t", kWraps, true},
    {"2", Std, "EXPLAIN DELETE FROM t", kWraps, true},
    {"2", Std, "EXPLAIN VERBOSE DELETE FROM t", kWraps, true},
    {"2", Std, "EXPLAIN QUERY PLAN DELETE FROM t", kWraps, true},
    {"2", Std, "EXPLAIN FORMAT=JSON DELETE FROM t", kWraps, true},
    {"2", Std, "EXPLAIN FORMAT = TREE UPDATE t SET a = 1", kWraps, true},
    {"2", Std, "EXPLAIN ANALYZE FORMAT=TREE UPDATE t1 JOIN t2 ON t1.id = t2.id SET t1.a = 1", kWraps, true},
    {"2", Std, "EXPLAIN EXTENDED DELETE FROM t", kWraps, true},
    {"2", Std, "DESCRIBE ANALYZE DELETE t1 FROM t1 JOIN t2 ON t1.id = t2.id", kWraps, true},
    {"2", Std, "DESC FORMAT=JSON DELETE FROM t", kWraps, true},
    {"2", Std, "DESCRIBE DELETE FROM t", kWraps, true},
    {"2", Std, "EXPLAIN ANALYZE WITH c AS (DELETE FROM t RETURNING *) SELECT * FROM c", kWraps, true},
    {"2", Std, "EXPLAIN ANALYZE WITH c AS (SELECT 1) DELETE FROM t", kWraps, true},
    {"2", Std, "EXPLAIN ANALYZE SELECT * INTO newt FROM t", kWraps, true},
    {"2", Std, "EXPLAIN ANALYZE CREATE TABLE x AS SELECT 1", kWraps, true},
    {"2", Std, "EXPLAIN ANALYSE MERGE INTO t USING s ON t.id = s.id WHEN MATCHED THEN DELETE", kWraps, true},
    {"2", Std, "EXPLAIN ANALYZE VERBOSE INSERT INTO t VALUES (1)", kWraps, true},
    {"2", Std, "EXPLAIN /* c */ ANALYSE /* c */ DELETE FROM t", kWraps, true},
    {"2", Std, "EXPLAIN\nANALYSE\r\nDELETE FROM t", kWraps, true},
    {"2", Std, "EXPLAIN\x0B" "ANALYSE\x0C" "DELETE FROM t", kWraps, true},
    {"2", Std, "explain analyse delete from t", kWraps, true},
    {"2", Std, "(EXPLAIN ANALYSE DELETE FROM t)", kWraps, true},
    {"2", Std, "EXPLAIN (ANALYZE) (DELETE FROM t RETURNING *)", kWraps, true},
    {"2", Std, "EXPLAIN (ANALYZE (DELETE FROM t)) SELECT 1", kWraps, false},
    {"2", Std, "EXPLAIN \"analyze\" DELETE FROM t", kWraps, false},
    {"2", Std, "EXPLAIN EXPLAIN ANALYSE DELETE FROM t", kWraps, true},
    {"2", Std, "EXPLAIN ANALYZE EXECUTE stmt", kWraps, false},
    {"2", Std, "EXPLAIN (ANALYZE) DECLARE c CURSOR FOR SELECT 1", kWraps, false},
    {"2", Std, "EXPLAIN FORMAT=JSON INTO @v SELECT 1", kWraps, false},
    {"2", Std, "EXPLAIN FOR CONNECTION 5", kWraps, false},
    {"2", Std, "EXPLAIN ANALYZE SELECT * FROM t FOR UPDATE", kLock, true},
    {"2", Std, "EXPLAIN ANALYZE SELECT pg_sleep(10)", "side-effecting function 'PG_SLEEP'", true},

    // ── 3. Postgres U&"..." / U&'...' and quoted deny-listed names ────────
    {"3", Std, "SELECT U&\"pg\\005fread\\005ffile\"('/etc/passwd')", kUnicode, false},
    {"3", RF,  "SELECT U&\"pg\\005fread\\005ffile\"('/etc/passwd')", kUnicode, false},
    {"3", Std, "SELECT U&\"pg_terminate_backen\\0064\"(pid) FROM pg_stat_activity", kUnicode, false},
    {"3", Std, "SELECT * FROM U&\"dblink\\005fexec\"('host=x', 'DELETE FROM t')", kUnicode, false},
    {"3", Std, "SELECT U&\"nextva\\006c\"('s')", kUnicode, false},
    {"3", Std, "SELECT u&\"x\" FROM t", kUnicode, false},
    {"3", Std, "SELECT 1,U&\"x\" FROM t", kUnicode, false},
    {"3", Std, "SELECT (U&\"pg\\005fsleep\")(1)", kUnicode, false},
    {"3", Std, "SELECT U&'\\0041' UESCAPE '!'", kUnicode, false},
    {"3", Std, "SELECT U&'d!0061t!+000061' UESCAPE '!'", kUnicode, false},
    {"3", Std, "SELECT * FROM t WHERE a = U&'x'", kUnicode, false},
    {"3", RF,  "SELECT * FROM U&'/etc/passwd'", kUnicode, false},
    {"3", Std, "SELECT \"pg_read_file\"('/etc/passwd')", "side-effecting function 'PG_READ_FILE'", true},
    {"3", Std, "SELECT \"PG_READ_FILE\"('/etc/passwd')", "side-effecting function 'PG_READ_FILE'", true},
    {"3", Std, "SELECT \"Pg_Sleep\"(5)", "side-effecting function 'PG_SLEEP'", true},
    {"3", Std, "SELECT `sleep`(5)", "side-effecting function 'SLEEP'", true},
    {"3", Std, "SELECT [xp_cmdshell]('dir')", "side-effecting function 'XP_CMDSHELL'", true},
    {"3", Std, "SELECT \"dblink_exec\"('x', 'DELETE FROM t')", "side-effecting function 'DBLINK_EXEC'", true},
    {"3", Strict, "EXEC [xp_cmdshell] 'dir'", "'EXEC'", true},

    // ── 4. side-effect / session functions and locking reads ─────────────
    {"4", Std, "SELECT dblink_connect('c', 'host=x')", "'DBLINK_CONNECT'", true},
    {"4", Std, "SELECT dblink_send_query('c', 'DELETE FROM t')", "'DBLINK_SEND_QUERY'", true},
    {"4", Std, "SELECT * FROM dblink_open('c', 'select 1')", "'DBLINK_OPEN'", true},
    {"4", Std, "SELECT lo_unlink(1)", "'LO_UNLINK'", true},
    {"4", Std, "SELECT lo_from_bytea(0, 'x')", "'LO_FROM_BYTEA'", true},
    {"4", Std, "SELECT lo_put(1, 0, 'x')", "'LO_PUT'", true},
    {"4", Std, "SELECT lo_create(0)", "'LO_CREATE'", true},
    {"4", Std, "SELECT lo_creat(-1)", "'LO_CREAT'", true},
    {"4", Std, "SELECT lowrite(0, 'x')", "'LOWRITE'", true},
    {"4", Std, "SELECT lo_import('/etc/passwd')", "'LO_IMPORT'", true},
    {"4", Std, "SELECT lo_export(1, '/tmp/x')", "'LO_EXPORT'", true},
    {"4", Std, "SELECT pg_file_write('x', 'y', false)", "'PG_FILE_WRITE'", true},
    {"4", Std, "SELECT pg_file_unlink('x')", "'PG_FILE_UNLINK'", true},
    {"4", Std, "SELECT pg_file_rename('x', 'y')", "'PG_FILE_RENAME'", true},
    {"4", Std, "SELECT pg_read_binary_file('/etc/passwd')", "'PG_READ_BINARY_FILE'", true},
    {"4", Std, "SELECT pg_drop_replication_slot('s')", "'PG_DROP_REPLICATION_SLOT'", true},
    {"4", Std, "SELECT pg_create_physical_replication_slot('s')", "'PG_CREATE_PHYSICAL_REPLICATION_SLOT'", true},
    {"4", Std, "SELECT * FROM pg_create_logical_replication_slot('s', 'p')", "'PG_CREATE_LOGICAL_REPLICATION_SLOT'", true},
    {"4", Std, "SELECT pg_switch_wal()", "'PG_SWITCH_WAL'", true},
    {"4", Std, "SELECT pg_create_restore_point('x')", "'PG_CREATE_RESTORE_POINT'", true},
    {"4", Std, "SELECT pg_reload_conf()", "'PG_RELOAD_CONF'", true},
    {"4", Std, "SELECT pg_rotate_logfile()", "'PG_ROTATE_LOGFILE'", true},
    {"4", Std, "SELECT pg_cancel_backend(1)", "'PG_CANCEL_BACKEND'", true},
    {"4", Std, "SELECT pg_terminate_backend(1)", "'PG_TERMINATE_BACKEND'", true},
    {"4", Std, "SELECT set_config('work_mem', '1GB', false)", "'SET_CONFIG'", true},
    {"4", Std, "SELECT setval('s', 1)", "'SETVAL'", true},
    {"4", Std, "SELECT nextval('s')", "'NEXTVAL'", true},
    {"4", Std, "SELECT pg_advisory_lock(1)", "'PG_ADVISORY_LOCK'", true},
    {"4", Std, "SELECT pg_advisory_lock_shared(1)", "'PG_ADVISORY_LOCK_SHARED'", true},
    {"4", Std, "SELECT pg_advisory_xact_lock(1)", "'PG_ADVISORY_XACT_LOCK'", true},
    {"4", Std, "SELECT pg_advisory_xact_lock_shared(1)", "'PG_ADVISORY_XACT_LOCK_SHARED'", true},
    {"4", Std, "SELECT pg_try_advisory_lock(1)", "'PG_TRY_ADVISORY_LOCK'", true},
    {"4", Std, "SELECT pg_sleep(10)", "'PG_SLEEP'", true},
    {"4", Std, "SELECT pg_sleep_for('5 minutes')", "'PG_SLEEP_FOR'", true},
    {"4", Std, "SELECT pg_sleep_until('tomorrow')", "'PG_SLEEP_UNTIL'", true},
    {"4", Std, "SELECT pg_notify('c', 'x')", "'PG_NOTIFY'", true},
    {"4", Std, "SELECT pg_logical_emit_message(true, 'p', 'x')", "'PG_LOGICAL_EMIT_MESSAGE'", true},
    {"4", Std, "SELECT query_to_xml('DELETE FROM t RETURNING *', true, false, '')", "'QUERY_TO_XML'", true},
    {"4", Std, "SELECT query_to_xml_and_xmlschema('select 1', true, false, '')", "'QUERY_TO_XML_AND_XMLSCHEMA'", true},
    {"4", Std, "SELECT cursor_to_xml('c', 10, true, false, '')", "'CURSOR_TO_XML'", true},
    {"4", Std, "SELECT SLEEP(10)", "'SLEEP'", true},
    {"4", Std, "SELECT BENCHMARK(100000000, MD5('x'))", "'BENCHMARK'", true},
    {"4", Std, "SELECT GET_LOCK('x', 10)", "'GET_LOCK'", true},
    {"4", Std, "SELECT RELEASE_LOCK('x')", "'RELEASE_LOCK'", true},
    {"4", Std, "SELECT * FROM t WHERE id = 1 AND sleep (5)", "'SLEEP'", true},
    {"4", Std, "SELECT pg_sleep/* c */(5)", "'PG_SLEEP'", true},
    {"4", RF,  "SELECT LOAD_FILE('/etc/passwd')", "filesystem-reading function 'LOAD_FILE'", false},
    {"4", RF,  "SELECT pg_ls_dir('.')", "filesystem-reading function 'PG_LS_DIR'", false},
    {"4", RF,  "SELECT pg_stat_file('postgresql.conf')", "filesystem-reading function 'PG_STAT_FILE'", false},
    {"4", Std, "SELECT * FROM t FOR UPDATE", kLock, true},
    {"4", Std, "SELECT * FROM t FOR SHARE", kLock, true},
    {"4", Std, "SELECT * FROM t FOR NO KEY UPDATE", kLock, true},
    {"4", Std, "SELECT * FROM t FOR KEY SHARE", kLock, true},
    {"4", Std, "SELECT * FROM t FOR UPDATE SKIP LOCKED", kLock, true},
    {"4", Std, "SELECT * FROM t FOR UPDATE OF t NOWAIT", kLock, true},
    {"4", Std, "SELECT * FROM t LIMIT 1 FOR UPDATE", kLock, true},
    {"4", Std, "SELECT * FROM t LOCK IN SHARE MODE", kLock, true},
    {"4", Std, "SELECT * FROM (SELECT * FROM t FOR UPDATE) s", kLock, true},
    {"4", Std, "WITH c AS (SELECT * FROM t FOR UPDATE) SELECT * FROM c", kLock, true},
    {"4", Std, "SELECT * FROM t\nFOR\r\nUPDATE", kLock, true},
    {"4", Std, "SELECT * FROM t FOR /* c */ UPDATE", kLock, true},
    {"4", Std, "select * from t for update", kLock, true},
    {"4", Std, "SELECT * FROM t WITH (UPDLOCK)", kLock, true},
    {"4", Std, "SELECT * FROM t WITH (XLOCK, ROWLOCK)", kLock, true},
    {"4", Std, "SELECT * FROM t WITH (TABLOCKX)", kLock, true},
    {"4", Std, "SELECT * FROM t WITH (HOLDLOCK)", kLock, true},
    {"4", Strict, "SELECT 1 WAITFOR DELAY '00:00:10'", "T-SQL statement keyword 'WAITFOR'", true},
    {"4", Std, "WAITFOR DELAY '00:00:10'", "'WAITFOR' is not a read-only statement", false},

    // ── 5. SQL Server batches without ';' (strict) ───────────────────────
    {"5", Strict,   "SELECT 1 RECEIVE TOP(1) * FROM dbo.q", "'RECEIVE'", true},
    {"5", StrictRF, "SELECT 1 RECEIVE TOP(1) * FROM dbo.q", "'RECEIVE'", true},
    {"5", Strict,   "SELECT 1 END CONVERSATION @h WITH CLEANUP", "'END CONVERSATION'", true},
    {"5", Strict,   "SELECT 1 SEND ON CONVERSATION @h (N'x')", "'SEND ON'", true},
    {"5", Strict,   "SELECT 1 MOVE CONVERSATION @h TO @g", "'MOVE CONVERSATION'", true},
    {"5", Strict,   "SELECT 1 GET CONVERSATION GROUP @g FROM dbo.q", "'GET CONVERSATION'", true},
    {"5", Strict,   "SELECT 1 WHILE 1=1 SELECT 2", "'WHILE'", true},
    {"5", Strict,   "SELECT 1 xp_cmdshell 'dir'", "'XP_CMDSHELL'", true},
    {"5", Strict,   "SELECT 1 master..xp_cmdshell 'dir'", "'XP_CMDSHELL'", true},
    {"5", Strict,   "SELECT 1 EXEC master..xp_cmdshell 'dir'", kTsql, true},
    {"5", Strict,   "SELECT 1 sp_executesql N'DROP TABLE t'", "'SP_EXECUTESQL'", true},
    {"5", Strict,   "SELECT 1 DECLARE @x INT", "'DECLARE'", true},
    {"5", Strict,   "SELECT 1 OPEN SYMMETRIC KEY k DECRYPTION BY PASSWORD = 'p'", "'OPEN'", true},
    {"5", Strict,   "SELECT 1 CLOSE MASTER KEY", "'CLOSE'", true},
    {"5", Strict,   "SELECT 1 DEALLOCATE c", "'DEALLOCATE'", true},
    {"5", Strict,   "SELECT 1 BEGIN DIALOG @h FROM SERVICE a TO SERVICE 'b'", "'BEGIN'", true},
    {"5", Strict,   "SELECT 1 KILL 55", "'KILL'", true},
    {"5", Strict,   "SELECT 1 SHUTDOWN", "'SHUTDOWN'", true},
    {"5", Strict,   "SELECT 1 DBCC FREEPROCCACHE", "'DBCC'", true},
    {"5", Strict,   "SELECT 1 BACKUP DATABASE d TO DISK = 'x'", "'BACKUP'", true},
    {"5", Strict,   "SELECT 1 RESTORE DATABASE d FROM DISK = 'x'", "'RESTORE'", true},
    {"5", Strict,   "SELECT 1 RECONFIGURE", "'RECONFIGURE'", true},
    {"5", Strict,   "SELECT 1 GRANT SELECT ON t TO u", "'GRANT'", true},
    {"5", Strict,   "SELECT 1 REVOKE SELECT ON t FROM u", "'REVOKE'", true},
    {"5", Strict,   "SELECT 1 DENY SELECT ON t TO u", "'DENY'", true},
    {"5", Strict,   "SELECT 1 USE master", "'USE'", true},
    {"5", Strict,   "SELECT 1 SET NOCOUNT ON", "'SET'", true},
    {"5", Strict,   "SELECT 1 RAISERROR('x', 10, 1) WITH LOG", "'RAISERROR'", true},
    {"5", Strict,   "SELECT 1 GOTO l", "'GOTO'", true},
    {"5", Strict,   "SELECT 1\rEND CONVERSATION @h", "'END CONVERSATION'", true},
    {"5", Strict,   "SELECT 1\xC2\x85" "END CONVERSATION @h", "'END CONVERSATION'", true},
    {"5", Strict,   "SELECT 1 /* x */ END /* y */ CONVERSATION @h", "'END CONVERSATION'", true},
    {"5", Strict,   "SELECT 1 -- x\nEND CONVERSATION @h", "'END CONVERSATION'", true},
    {"5", Strict,   "SELECT 1 end conversation @h", "'END CONVERSATION'", true},
    {"5", Strict,   "SELECT CASE WHEN a = 1 THEN 1 END FROM t END CONVERSATION @h", "'END CONVERSATION'", true},
    {"5", Strict,   "SELECT [x] RECEIVE * FROM q", "'RECEIVE'", true},
    {"5", Strict,   "SELECT 1 [master]..[xp_cmdshell] 'dir'", "'XP_CMDSHELL'", true},

    // ── 6. DuckDB file scans through table names (restrictFilesystem) ────
    {"6", RF, "PIVOT \"/abs/data.csv\" ON a USING sum(b)", kFilePath, false},
    {"6", RF, "UNPIVOT \"/abs/data.csv\" ON a, b INTO NAME k VALUE v", kFilePath, false},
    {"6", RF, "SELECT * FROM \"/abs/data.csv\"", kFilePath, false},
    {"6", RF, "SELECT * FROM \"data.parquet\"", kFilePath, false},
    {"6", RF, "SELECT * FROM \"data.CSV\"", kFilePath, false},
    {"6", RF, "SELECT * FROM \"x.csv.gz\"", kFilePath, false},
    {"6", RF, "SELECT * FROM \"x\"\"y.csv\"", kFilePath, false},
    {"6", RF, "SELECT * FROM \"s3://bucket/x.parquet\"", kFilePath, false},
    {"6", RF, "SELECT * FROM \"*.csv\"", kFilePath, false},
    {"6", RF, "SELECT * FROM \"data_*\"", kFilePath, false},
    {"6", RF, "SELECT * FROM \"data.csv\" AS d", kFilePath, false},
    {"6", RF, "SELECT * FROM t1 NATURAL JOIN \"x.json\"", kFilePath, false},
    {"6", RF, "SELECT * FROM t JOIN `x.csv` USING (id)", kFilePath, false},
    {"6", RF, "SELECT * FROM [C:\\data\\x.csv]", kFilePath, false},
    {"6", RF, "SELECT * FROM (\"data.csv\" CROSS JOIN t1)", kFilePath, false},
    {"6", RF, "SELECT * FROM t WHERE id IN (SELECT id FROM \"x.csv\")", kFilePath, false},
    {"6", RF, "WITH c AS (SELECT * FROM \"x.csv\") SELECT * FROM c", kFilePath, false},
    {"6", RF, "SELECT * FROM /* c */ \"x.csv\"", kFilePath, false},
    {"6", RF, "SELECT * FROM\r\n\"x.csv\"", kFilePath, false},
    {"6", RF, "SUMMARIZE \"/abs/data.csv\"", kFilePath, false},
    {"6", RF, "DESCRIBE \"/abs/data.csv\"", kFilePath, false},
    {"6", RF, "SHOW \"/abs/data.csv\"", kFilePath, false},
    {"6", RF, "TABLE \"x.csv\"", kFilePath, false},
    {"6", RF, "DESCRIBE SELECT * FROM \"x.csv\"", kFilePath, false},
    {"6", RF, "FROM \"x.csv\"", kFilePath, false},
    {"6", StrictRF, "SELECT * FROM [x.csv]", kFilePath, false},
    {"6", RF, "DESCRIBE TABLE '/abs/data.csv'", kFileStr, false},
    {"6", RF, "SUMMARIZE TABLE '/abs/data.csv'", kFileStr, false},
    {"6", RF, "SHOW TABLE '/abs/data.csv'", kFileStr, false},
    {"6", RF, "TABLE '/abs/data.csv'", kFileStr, false},
    {"6", RF, "DESC '/abs/data.csv'", kFileStr, false},
    {"6", RF, "SELECT * FROM ('data.csv' CROSS JOIN t1)", kFileStr, false},
    {"6", RF, "SELECT * FROM ONLY 'data.csv'", kFileStr, false},
    {"6", RF, "describe table\n'/abs/data.csv'", kFileStr, false},
    {"6", RF, "SELECT * FROM data.csv", kFileDot, false},
    {"6", RF, "SELECT * FROM data.CSV", kFileDot, false},
    {"6", RF, "SELECT * FROM main.data.csv", kFileDot, false},
    {"6", RF, "SELECT * FROM \"data\".\"csv\"", kFileDot, false},
    {"6", RF, "SELECT * FROM t1, data.parquet", kFileDot, false},
    {"6", RF, "SELECT * FROM data . json", kFileDot, false},
    {"6", RF, "TABLE data.csv", kFileDot, false},
    {"6", RF, "DESCRIBE TABLE data.csv", kFileDot, false},
    // adversarial pass: VALUES rows in FROM no longer end the table list
    {"6", RF, "SELECT count(*) n FROM VALUES (1) v, data.csv", kFileDot, false},
    {"6", RF, "SELECT count(*) n FROM VALUES (1) v, 'data.csv'", kFileStr, false},
    {"6", RF, "SELECT count(*) n FROM VALUES (1), (2) AS v(x), \"data.csv\"", kFilePath, false},
    {"6", RF, "SELECT * FROM t1 POSITIONAL JOIN data.csv", kFileDot, false},
    {"6", RF, "SELECT * FROM (SHOW data.csv)", kFileDot, false},
    {"6", RF, "EXPLAIN SHOW data.csv", kFileDot, false},
    {"6", RF, "SELECT * FROM data/**/.csv", kFileDot, false},
    {"6", RF, "SELECT * FROM main.\"data.csv\"", kFilePath, false},

    // ── 7. a soft finding (lock clause, NEXT VALUE FOR) no longer ends the
    // scan: the hard-stop token after it decides the reason ─────────────
    {"7", Strict,   "SELECT * FROM t WITH (HOLDLOCK) EXEC xp_cmdshell 'calc'", kTsql, true},
    {"7", StrictRF, "SELECT * FROM t WITH (HOLDLOCK) EXEC xp_cmdshell 'calc'", kTsql, true},
    {"7", Strict,   "SELECT * FROM t WITH (UPDLOCK) DROP TABLE users", kTsql, true},
    {"7", Strict,   "SELECT NEXT VALUE FOR s EXEC sp_executesql N'DROP TABLE t'", kTsql, true},
    {"7", Strict,   "SELECT * FROM t WITH (XLOCK) WHERE a = 1 SHUTDOWN", kTsql, true},
    {"7", Std,      "SELECT * FROM t WHERE id IN (SELECT id FROM u FOR UPDATE) "
                    "AND dblink_exec('c', 'DROP TABLE x') IS NULL", "side-effecting function", true},
    {"7", Std,      "SELECT * FROM t FOR SHARE OF t NOWAIT, pg_sleep(10)", "side-effecting function", true},
    {"7", Std,      "SELECT NEXT VALUE FOR s, lo_import('/etc/passwd')", "side-effecting function", true},
    {"7", RF,       "SELECT * FROM t WHERE id IN (SELECT id FROM u FOR UPDATE) "
                    "AND pg_ls_dir('/') IS NULL", "filesystem-reading function", true},
    {"7", RF,       "SELECT * FROM t LOCK IN SHARE MODE UNION SELECT load_file('/etc/passwd')",
                    "filesystem-reading function", true},

    // ── 8. a keyword after an expression '(' followed by more words is still
    // a statement body (only (word) / (word op ...) is an operand) ───────
    {"8", Std, "SELECT (DELETE FROM t RETURNING 1)", "in verb position", true},
    {"8", Std, "SELECT * FROM t WHERE (UPDATE t SET a = 1 RETURNING 1) IS NULL", "in verb position", true},
    {"8", Std, "SELECT * FROM t WHERE a = 1 AND (DELETE * FROM t) IS NULL", "in verb position", true},
    {"8", Std, "SELECT * FROM (DELETE FROM t RETURNING *) d", "in verb position", true},
    {"8", Std, "SELECT * FROM t WHERE EXISTS (DELETE FROM t RETURNING 1)", "in verb position", true},
    {"8", Std, "SELECT a, (INSERT INTO t VALUES (1) RETURNING 1) FROM u", "in verb position", true},
    {"8", Std, "SELECT * FROM t WHERE x = (COPY t TO '/tmp/x')", "in verb position", true},
};

// Hard-fail rows: the verdict an approval card can never override. Before the
// fix, a lock clause or NEXT VALUE FOR returned before these were examined, so
// the approval path offered a card for the first 9 (v0.1.133 review).
struct Hard {
    const char *driver;  // classifyApprovedWrite's driver
    const char *sql;
    const char *reason;  // substring of hardReason
};
static const Hard kHard[] = {
    {"QODBC", "SELECT * FROM t WITH (HOLDLOCK) EXEC xp_cmdshell 'calc'", kTsql},
    {"QODBC", "UPDATE t WITH (TABLOCK) SET a=1 DROP TABLE users", kTsql},
    {"QODBC", "UPDATE t WITH (UPDLOCK) SET a=1 EXEC xp_cmdshell 'calc'", kTsql},
    {"QODBC", "UPDATE t SET a = NEXT VALUE FOR s EXEC xp_cmdshell 'x'", kTsql},
    {"QODBC", "SELECT * FROM t WITH (TABLOCKX) EXEC master..xp_cmdshell 'x'", kTsql},
    {"QPSQL", "UPDATE t SET a=1 WHERE id IN (SELECT id FROM u FOR UPDATE) "
              "AND dblink_exec('c','DROP TABLE x') IS NULL", "DBLINK_EXEC"},
    {"QPSQL", "UPDATE t SET a=1 WHERE id IN (SELECT id FROM u FOR UPDATE) "
              "RETURNING pg_read_file('/etc/passwd')", "PG_READ_FILE"},
    {"QPSQL", "UPDATE t SET a = (SELECT b FROM u FOR NO KEY UPDATE), c = pg_ls_dir('/')",
              "PG_LS_DIR"},
    {"QMYSQL", "UPDATE t SET a=1 WHERE id IN (SELECT id FROM u LOCK IN SHARE MODE) "
               "AND SLEEP(5) = 0", "SLEEP"},
    // lock clause + each remaining hard family (these were already hard; pinned)
    {"QPSQL", "UPDATE t SET a=1 WHERE id IN (SELECT id FROM u FOR UPDATE) /* /* */ */",
              "nested"},
    {"QPSQL", "UPDATE t SET a=1 WHERE id IN (SELECT id FROM u FOR UPDATE) "
              "AND U&\"\\0070g_read_file\"('/x') IS NULL", "Unicode-escaped"},
    {"DUCKDB", "INSERT INTO t SELECT * FROM 'x.csv' FOR UPDATE", kFileStr},
    {"QPSQL", "UPDATE t SET a = dblink_exec('c','x') WHERE id IN (SELECT id FROM u FOR UPDATE)",
              "DBLINK_EXEC"},
    // forms the approval scan read only under standard lexing / only as the
    // statement's first word
    {"QMYSQL", "/*!LOAD DATA LOCAL INFILE '/etc/passwd' INTO TABLE t*/", "LOAD"},
    {"QMYSQL", "SELECT * FROM t /*!INTO OUTFILE '/tmp/x'*/", "OUTFILE"},
    {"QMYSQL", "UPDATE t SET a = 1 /*!50000 , b = (SELECT 1 INTO DUMPFILE '/tmp/x')*/", "DUMPFILE"},
    {"QPSQL", "EXPLAIN ANALYZE EXECUTE p", "EXECUTE"},
    {"DUCKDB", "EXPLAIN ANALYZE ATTACH 'x.db' AS x", "ATTACH"},
};

// Writes the approval path must still offer a card for.
static const Hard kApprovable[] = {
    {"QPSQL", "UPDATE t SET a = 1 WHERE id IN (SELECT id FROM u FOR UPDATE)", ""},
    {"QPSQL", "DELETE FROM t WHERE id = 1", ""},
    {"QMYSQL", "UPDATE t SET a = 1 WHERE b = 'x' -- note", ""},
    {"QPSQL", "EXPLAIN ANALYZE UPDATE t SET a = 1", ""},
    {"QSQLITE", "INSERT INTO t (a) VALUES (1)", ""},
};

static const Accept kAccept[] = {
    // 1 — ordinary comments still pass
    {"1", Std,    "SELECT /* a comment */ 1 FROM t /* another */"},
    {"1", Std,    "SELECT 1 /* a * b / c */ FROM t"},
    {"1", Std,    "SELECT a / b /* ratio */ FROM t"},
    {"1", Std,    "SELECT 2*/* note */3"},
    {"1", Std,    "/**/SELECT 1"},
    {"1", Std,    "/***/SELECT 1"},
    {"1", Std,    "SELECT '/*' AS a, '*/' AS b FROM t"},
    {"1", RF,     "SELECT \"/*\" FROM t"},
    {"1", Std,    "SELECT 1 -- /* not an opener\nFROM t"},
    {"1", Strict, "SELECT name FROM sys.tables /* list */"},

    // 2 — explained reads
    {"2", Std, "EXPLAIN SELECT * FROM users"},
    {"2", Std, "EXPLAIN ANALYZE SELECT * FROM users"},
    {"2", RF,  "EXPLAIN ANALYZE SELECT * FROM users"},
    {"2", Std, "EXPLAIN ANALYSE SELECT 1"},
    {"2", Std, "EXPLAIN VERBOSE SELECT 1"},
    {"2", Std, "EXPLAIN (ANALYZE, BUFFERS, FORMAT JSON) SELECT * FROM t"},
    {"2", Std, "EXPLAIN (ANALYZE true, COSTS off) SELECT * FROM t"},
    {"2", Std, "EXPLAIN (\"analyze\") SELECT 1"},
    {"2", Std, "EXPLAIN QUERY PLAN SELECT * FROM t WHERE id = 1"},
    {"2", Std, "EXPLAIN FORMAT=JSON SELECT * FROM t"},
    {"2", Std, "EXPLAIN ANALYZE FORMAT=TREE SELECT * FROM t"},
    {"2", Std, "EXPLAIN (FORMAT JSON) WITH c AS (SELECT 1) SELECT * FROM c"},
    {"2", Std, "EXPLAIN (SELECT 1)"},
    {"2", Std, "EXPLAIN VALUES (1)"},
    {"2", Std, "EXPLAIN TABLE t"},
    {"2", Std, "EXPLAIN users"},
    {"2", Std, "DESCRIBE users"},
    {"2", Std, "DESC users"},
    {"2", Std, "DESCRIBE db.users"},
    {"2", Std, "DESC users name"},
    {"2", Std, "DESCRIBE users 'na%'"},
    {"2", Std, "DESCRIBE SELECT * FROM t"},
    {"2", Std, "DESCRIBE (SELECT 1)"},
    {"2", Std, "DESCRIBE TABLE users"},
    {"2", Std, "DESCRIBE"},
    {"2", Std, "SELECT * FROM t ORDER BY a DESC"},

    // 3 — '&' and quoted names that are not U& escapes
    {"3", Std, "SELECT flags & 4 FROM t"},
    {"3", Std, "SELECT a&b FROM t"},
    {"3", Std, "SELECT U & 1 FROM t"},
    {"3", Std, "SELECT 'U&\"x\"' AS s"},
    {"3", Std, "SELECT \"u\" FROM t"},
    {"3", Std, "SELECT \"pg_read_file\" FROM t"},
    {"3", Std, "SELECT [xp_cmdshell] FROM t"},

    // 4 — reads near the new deny-list entries
    {"4", Std,    "SELECT table_to_xml('t', true, false, '')"},
    {"4", Std,    "SELECT sleep FROM t"},
    {"4", Std,    "SELECT dblink FROM t"},
    {"4", Std,    "SELECT pg_sleep_note FROM t"},
    {"4", Std,    "SELECT currval('s')"},
    {"4", Std,    "SELECT current_setting('work_mem')"},
    {"4", Std,    "SELECT * FROM t WHERE share > 1"},
    {"4", Std,    "SELECT update_for, lock FROM t"},
    {"4", Std,    "SELECT * FROM sales PIVOT (SUM(amount) FOR share IN ([a], [b])) p"},
    {"4", Std,    "SELECT * FROM sales PIVOT (SUM(amount) FOR update IN ([a], [b])) p"},
    {"4", Std,    "SELECT * FROM t FOR READ ONLY"},
    {"4", Std,    "SELECT * FROM t FOR SYSTEM_TIME AS OF '2024-01-01'"},
    {"4", Strict, "SELECT * FROM t FOR JSON AUTO"},
    {"4", Strict, "SELECT * FROM t FOR XML PATH('')"},
    {"4", Strict, "SELECT * FROM t WITH (NOLOCK)"},
    {"4", Std,    "SELECT lock_timeout, share FROM t"},

    // 5 — strict batch reads that use the new words harmlessly
    {"5", Strict,   "SELECT CASE WHEN a = 1 THEN 'x' ELSE 'y' END FROM t"},
    {"5", Strict,   "SELECT CASE WHEN a = 1 THEN 1 END AS c, [get], [move] FROM t"},
    {"5", Strict,   "SELECT CASE WHEN a = 1 THEN 1 END conversation_count FROM t"},
    {"5", Strict,   "SELECT get, move, send FROM t"},
    {"5", Strict,   "SELECT [receive], [open], [close], [declare], [while] FROM t"},
    {"5", Strict,   "SELECT t.receive, t.declare, t.[end] FROM t"},
    {"5", Strict,   "SELECT x AS [end], y AS conversation FROM t"},
    {"5", Strict,   "SELECT 'END CONVERSATION' AS s, 'WHILE 1=1' AS w"},
    {"5", Strict,   "SELECT [conversation], [end] FROM t"},
    {"5", Strict,   "SELECT * FROM t WHERE note = 'xp_cmdshell'"},
    {"5", StrictRF, "SELECT TOP 10 * FROM dbo.t ORDER BY id"},

    // 6 — quoted / dotted names that are not files
    {"6", RF,       "SELECT * FROM \"order items\""},
    {"6", RF,       "SELECT * FROM \"my.schema\".\"t\""},
    {"6", RF,       "SELECT * FROM \"my.schema\".\"data\""},
    {"6", RF,       "SELECT * FROM \"Sales 2024\""},
    {"6", RF,       "SELECT * FROM \"t.v1\""},
    {"6", RF,       "SELECT * FROM main.t"},
    {"6", RF,       "SELECT \"a/b\" AS ratio FROM t"},
    {"6", RF,       "SELECT \"x.csv\" FROM t"},
    {"6", RF,       "SELECT * FROM t AS \"x.csv\""},
    {"6", RF,       "SELECT * FROM t ORDER BY a DESC, 'x'"},
    {"6", RF,       "SELECT show, 'x' FROM t"},
    {"6", RF,       "SELECT * FROM (SELECT 1) s"},
    {"6", RF,       "SELECT * FROM (VALUES ('a'), ('b')) v(x)"},
    {"6", RF,       "SELECT * FROM VALUES ('a'), ('b') v(x)"},
    {"6", RF,       "SELECT * FROM VALUES (1, 'x.csv') v(a, b), t2"},
    {"6", RF,       "SELECT * FROM t1 JOIN (SELECT * FROM t2) s ON s.id = t1.id"},
    {"6", RF,       "SELECT * FROM t1 JOIN t2 ON t2.k = 'x.csv'"},
    {"6", RF,       "DESCRIBE users"},
    {"6", RF,       "DESCRIBE TABLE users"},
    {"6", RF,       "SHOW TABLES"},
    {"6", RF,       "SHOW users"},
    {"6", RF,       "SUMMARIZE sales"},
    {"6", RF,       "TABLE users"},
    {"6", StrictRF, "SELECT * FROM [order details]"},
    {"6", Std,      "SELECT * FROM \"data.csv\""},  // desktop gate unchanged
    {"6", Std,      "SELECT * FROM data.csv"},

    // 8 — a keyword-named column in a parenthesised expression is an operand
    {"8", Std,    "SELECT (comment) FROM posts"},
    {"8", RF,     "SELECT (comment) FROM posts"},
    {"8", Std,    "SELECT * FROM posts WHERE (start > 1)"},
    {"8", Std,    "SELECT a, (comment) FROM posts"},
    {"8", Std,    "SELECT * FROM t WHERE a = 1 AND (update = 2 OR (lock <> 3))"},
    {"8", Std,    "SELECT CASE WHEN (start > 1) THEN (comment) ELSE (set) END FROM t"},
    {"8", Std,    "SELECT 2 * (start) - (reset) FROM t"},
    {"8", Std,    "SELECT (comment || 'x') FROM posts"},
    {"8", Std,    "SELECT a FROM t ORDER BY a, (start)"},
    {"8", Strict, "SELECT (comment) FROM posts WHERE (start > 1)"},
};

int main(int argc, char **argv) {
    QTemporaryDir home;
    qputenv("HOME", home.path().toUtf8());
    qputenv("XDG_CONFIG_HOME", home.path().toUtf8());
    qputenv("APPDATA", home.path().toUtf8());
    qputenv("USERPROFILE", home.path().toUtf8());
    QCoreApplication app(argc, argv);

    std::printf("== REJECT: confirmed bypasses and their variants ==\n");
    int perItem[9] = {0};
    for (const Reject &r : kReject) {
        const SqlVerdict v = verdict(r.mode, r.sql);
        const bool rejected = !(v.singleStatement && v.readOnly);
        const bool reasonOk = v.reason.contains(QString::fromUtf8(r.reason));
        const bool mutOk = !r.mutation || v.mutation;
        const bool ok = rejected && reasonOk && mutOk;
        ++perItem[r.item[0] - '0'];
        if (ok) {
            ++g_pass;
            std::printf("  [PASS] %s %-8s %s\n", r.item, modeName(r.mode), shown(r.sql).toUtf8().constData());
        } else {
            ++g_fail;
            std::printf("  [FAIL] %s %-8s %s  (readOnly=%d single=%d mutation=%d want-mutation=%d "
                        "reason=\"%s\" want-reason~\"%s\")\n",
                        r.item, modeName(r.mode), shown(r.sql).toUtf8().constData(), v.readOnly,
                        v.singleStatement, v.mutation, r.mutation,
                        v.reason.toUtf8().constData(), r.reason);
        }
    }

    std::printf("== ACCEPT: reads each new rule must leave alone ==\n");
    for (const Accept &a : kAccept) {
        const SqlVerdict v = verdict(a.mode, a.sql);
        const bool ok = v.singleStatement && v.readOnly && !v.mutation;
        if (ok) {
            ++g_pass;
            std::printf("  [PASS] %s %-8s %s\n", a.item, modeName(a.mode), shown(a.sql).toUtf8().constData());
        } else {
            ++g_fail;
            std::printf("  [FAIL] %s %-8s %s  (readOnly=%d single=%d mutation=%d reason=\"%s\")\n",
                        a.item, modeName(a.mode), shown(a.sql).toUtf8().constData(), v.readOnly,
                        v.singleStatement, v.mutation, v.reason.toUtf8().constData());
        }
    }

    std::printf("== HARD: approval path refuses, whatever precedes the hard-stop token ==\n");
    for (const Hard &h : kHard) {
        const SqlVerdict v = classifyApprovedWrite(QString::fromUtf8(h.sql),
                                                   QString::fromUtf8(h.driver));
        const bool ok = v.hardFail && v.hardReason.contains(QString::fromUtf8(h.reason));
        ok ? ++g_pass : ++g_fail;
        std::printf("  [%s] %-7s %s  (hardFail=%d single=%d hardReason=\"%s\")\n",
                    ok ? "PASS" : "FAIL", h.driver, shown(h.sql).toUtf8().constData(), v.hardFail,
                    v.singleStatement, v.hardReason.toUtf8().constData());
    }
    // The strict read path reports the same hard verdict (execute_sql's card text
    // comes from it): a lock hint must not downgrade an EXEC to a soft refusal.
    for (const char *sql : {"SELECT * FROM t WITH (HOLDLOCK) EXEC xp_cmdshell 'calc'",
                            "UPDATE t SET a = NEXT VALUE FOR s EXEC xp_cmdshell 'x'"}) {
        const SqlVerdict v = classifySql(QString::fromUtf8(sql), true, true);
        const bool ok = v.hardFail && !v.readOnly;
        ok ? ++g_pass : ++g_fail;
        std::printf("  [%s] classifySql strict+rf hardFail: %s\n", ok ? "PASS" : "FAIL", sql);
    }
    std::printf("== APPROVABLE: ordinary writes still get a card ==\n");
    for (const Hard &h : kApprovable) {
        const SqlVerdict v = classifyApprovedWrite(QString::fromUtf8(h.sql),
                                                   QString::fromUtf8(h.driver));
        const bool ok = !v.hardFail && v.singleStatement && v.mutation;
        ok ? ++g_pass : ++g_fail;
        std::printf("  [%s] %-7s %s  (hardFail=%d mutation=%d reason=\"%s\")\n",
                    ok ? "PASS" : "FAIL", h.driver, shown(h.sql).toUtf8().constData(), v.hardFail,
                    v.mutation, (v.hardFail ? v.hardReason : v.reason).toUtf8().constData());
    }

    // EXPLAIN bodies are classified recursively: a long EXPLAIN EXPLAIN ... chain
    // must stay bounded (no stack blow-up) and be refused.
    {
        const QString deep = QStringLiteral("EXPLAIN ").repeated(50000) + QStringLiteral("SELECT 1");
        const SqlVerdict v = classifySql(deep, true, true);
        const bool ok = !v.readOnly && v.reason.contains(QStringLiteral("nested too deeply"));
        std::printf("  [%s] 50000 x EXPLAIN is refused without recursion blow-up\n", ok ? "PASS" : "FAIL");
        ok ? ++g_pass : ++g_fail;
        const SqlVerdict two = classifySql(QStringLiteral("EXPLAIN EXPLAIN SELECT 1"));
        const bool ok2 = two.readOnly;
        std::printf("  [%s] EXPLAIN EXPLAIN SELECT 1 still a read\n", ok2 ? "PASS" : "FAIL");
        ok2 ? ++g_pass : ++g_fail;
    }

    // Every item keeps a real reject table (guards against a truncated array).
    for (int item = 1; item <= 8; ++item) {
        if (perItem[item] < 5) {
            ++g_fail;
            std::printf("  [FAIL] item %d has only %d reject rows\n", item, perItem[item]);
        }
    }

    std::printf("\n=== Summary: %d passed, %d failed ===\n", g_pass, g_fail);
    return g_fail == 0 ? 0 : 1;
}
