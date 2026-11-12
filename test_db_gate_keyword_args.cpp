// SPDX-License-Identifier: GPL-3.0-or-later
//
// READ-ONLY SQL GATE — keyword-named function arguments (v0.1.133).
//
// The gate treated the first word after ANY '(' as a statement verb, so
// harmless reads were refused: SELECT COUNT(comment), MIN(start),
// REPLACE(REPLACE(...)), CAST(start AS DATE), and DuckDB's PIVOT / UNPIVOT
// statements. This table asserts both halves of the contract through the real
// DbConnections::classifySql: every ACCEPT row is a read the model may run with
// no approval, and every REJECT row (writes, stacking, parenthesised DML,
// data-modifying CTEs, EXPLAIN ANALYZE, REPLACE INTO, COPY ... TO, hidden ';'
// after any line terminator) must still be refused.

#include "dbconnections.h"

#include <QCoreApplication>
#include <QString>
#include <QTemporaryDir>
#include <cstdio>

using DbConnections::SqlVerdict;
using DbConnections::classifySql;

static int g_pass = 0, g_fail = 0;

// Strict = the QODBC / SQL Server batch verdict; RF = the MCP path (no file reads)
enum Mode { Std, Strict, RF };

struct Row {
    const char *dialect;
    Mode mode;
    const char *sql;
};

static SqlVerdict verdict(const Row &r) {
    return classifySql(QString::fromUtf8(r.sql), r.mode == RF, r.mode == Strict);
}

static void report(bool ok, const Row &r, const char *want, const SqlVerdict &v) {
    QString shown = QString::fromUtf8(r.sql);
    shown.replace(QLatin1Char('\r'), QStringLiteral("\\r")).replace(QLatin1Char('\n'), QStringLiteral("\\n"));
    if (ok) {
        ++g_pass;
        std::printf("  [PASS] %s %-8s %s\n", want, r.dialect, shown.toUtf8().constData());
    } else {
        ++g_fail;
        std::printf("  [FAIL] %s %-8s %s  (readOnly=%d single=%d mutation=%d reason=%s)\n", want,
                    r.dialect, shown.toUtf8().constData(), v.readOnly, v.singleStatement,
                    v.mutation, v.reason.toUtf8().constData());
    }
}

int main(int argc, char **argv) {
    QTemporaryDir home;
    qputenv("HOME", home.path().toUtf8());
    qputenv("XDG_CONFIG_HOME", home.path().toUtf8());
    qputenv("APPDATA", home.path().toUtf8());
    qputenv("USERPROFILE", home.path().toUtf8());
    QCoreApplication app(argc, argv);

    // Reads that must pass with no approval.
    const Row accept[] = {
        {"any",    Std,    "SELECT REPLACE(name,'a','b') FROM t"},
        {"any",    Std,    "SELECT REPLACE(REPLACE(name,'a','b'),'c','d') FROM t"},
        {"any",    Std,    "SELECT COUNT(comment) FROM posts"},
        {"any",    Std,    "SELECT MIN(start), MAX(start) FROM sessions"},
        {"any",    Std,    "SELECT MAX(update_time) FROM t"},
        {"pg",     Std,    "SELECT MAX(update) FROM t"},
        {"sqlite", Std,    "SELECT COALESCE(set, 0) FROM matches"},
        {"sqlite", Std,    "SELECT IFNULL(set, 0) FROM matches"},
        {"any",    Std,    "SELECT SUM(load) FROM servers"},
        {"mysql",  Std,    "SELECT LEFT(copy, 3) FROM docs"},
        {"any",    Std,    "SELECT CAST(start AS DATE) FROM sessions"},
        {"pg",     Std,    "SELECT string_agg(comment, ', ' ORDER BY id) FROM posts"},
        {"pg",     Std,    "SELECT array_agg(comment ORDER BY id) FROM posts"},
        {"any",    Std,    "SELECT COUNT(*) FROM t WHERE COALESCE(comment IS NULL, 0) = 1"},
        {"mysql",  Std,    "SELECT CONCAT(INSERT(name, 1, 2, 'x')) FROM t"},
        {"mysql",  Std,    "SELECT ROUND(TRUNCATE(price, 2), 1) FROM t"},
        {"pg",     Std,    "SELECT ABS(update - 1), LENGTH(comment) FROM t"},
        {"pg",     Std,    "SELECT date_trunc('month', start), f(start::date) FROM t"},
        {"duckdb", Std,    "PIVOT sales ON year USING sum(amount)"},
        {"duckdb", Std,    "PIVOT_WIDER sales ON year USING sum(amount)"},
        {"duckdb", Std,    "UNPIVOT monthly ON jan, feb INTO NAME month VALUE amount"},
        {"tsql",   Std,    "SELECT * FROM sales PIVOT (SUM(amount) FOR yr IN ([2023], [2024])) p"},
        {"any",    Std,    "SELECT * FROM (SELECT REPLACE(a,'x','y') AS a FROM t) s"},
        {"any",    Std,    "WITH x AS (SELECT COUNT(comment) AS n FROM posts) SELECT * FROM x"},
        {"any",    Std,    "WITH x(comment) AS (SELECT 1) SELECT * FROM x"},
        {"any",    Std,    "SELECT * FROM t WHERE id IN (SELECT id FROM u)"},
        {"any",    Std,    "(SELECT 1)"},
        {"any",    Std,    "SELECT 'DELETE FROM t; DROP TABLE x' AS s"},
        {"any",    Std,    "SELECT 1 -- DROP TABLE t; DELETE\r\n"},
        {"any",    Std,    "SELECT /* ; DELETE */ REPLACE(a, ';', ',') FROM t"},
        {"pg",     Std,    "SELECT $$DROP TABLE t$$ AS s, MIN(start) FROM t"},
        {"tsql",   Std,    "SELECT TOP 10 REPLACE([name], 'a', 'b') FROM [dbo].[t]"},
        {"tsql",   Strict, "SELECT TOP 10 REPLACE([name], 'a', 'b') FROM [dbo].[t]"},
        {"tsql",   Strict, "SELECT MIN(start), COUNT(comment) FROM t"},
        {"tsql",   Strict, "SELECT REPLACE(REPLACE(name,'a','b'),'c','d') FROM t"},
        {"any",    Std,    "SELECT REPLACE(name,'a','b')\rFROM t"},
        {"any",    Std,    "SELECT\xE2\x80\xA8MIN(start)\xE2\x80\xA8" "FROM t"},
        {"any",    Std,    "SELECT count(*) FILTER (WHERE start > 1) FROM t"},
        {"any",    Std,    "SELECT EXTRACT(YEAR FROM start) FROM t"},
        {"any",    Std,    "SELECT * FROM t WHERE x = REPLACE(y, 'a', 'b');"},
        {"duckdb", RF,     "PIVOT sales ON year USING sum(amount)"},
        {"any",    RF,     "SELECT COUNT(comment), REPLACE(name, 'a', 'b') FROM posts"},
    };

    // Statements that must still be refused (and, where stated, flagged as a mutation).
    struct RejectRow { Row r; bool mutation; };
    const RejectRow reject[] = {
        {{"any",    Std,    "(DELETE FROM t)"}, true},
        {{"any",    Std,    "(UPDATE t SET a = 1)"}, true},
        {{"pg",     Std,    "WITH x AS (DELETE FROM t RETURNING *) SELECT * FROM x"}, true},
        {{"pg",     Std,    "WITH x AS (UPDATE t SET a = REPLACE(a,'x','y') RETURNING *) SELECT * FROM x"}, true},
        {{"pg",     Std,    "WITH c AS (MERGE INTO t USING s ON t.id = s.id WHEN MATCHED THEN DELETE RETURNING *) SELECT * FROM c"}, true},
        {{"pg",     Std,    "WITH c AS (SELECT COUNT(comment) FROM t) DELETE FROM t"}, true},
        {{"any",    Std,    "SELECT * FROM (INSERT INTO t VALUES (1) RETURNING *) s"}, true},
        {{"any",    Std,    "SELECT * FROM t WHERE id IN (DELETE FROM t RETURNING id)"}, true},
        {{"any",    Std,    "SELECT * FROM t WHERE EXISTS (UPDATE t SET a = 1 RETURNING a)"}, true},
        {{"any",    Std,    "SELECT * FROM (REPLACE INTO t VALUES (1)) s"}, true},
        {{"any",    Std,    "SELECT f(DELETE FROM t)"}, true},
        {{"any",    Std,    "SELECT f(REPLACE INTO t VALUES (1))"}, true},
        {{"pg",     Std,    "SELECT f(COPY (SELECT 1) TO 'x')"}, true},
        {{"duckdb", Std,    "SELECT f(LOAD 'httpfs')"}, true},
        {{"pg",     Std,    "SELECT f(ANALYZE (VERBOSE) t)"}, false},
        {{"pg",     Std,    "EXPLAIN ANALYZE DELETE FROM t"}, false},
        {{"pg",     Std,    "EXPLAIN (ANALYZE) DELETE FROM t"}, false},
        {{"mysql",  Std,    "REPLACE INTO t VALUES (1)"}, true},
        {{"mysql",  Std,    "REPLACE t (a) VALUES (1)"}, true},
        {{"sqlite", Std,    "REPLACE INTO t SELECT REPLACE(a,'x','y') FROM s"}, true},
        {{"pg",     Std,    "COPY (SELECT 1) TO 'out.csv'"}, true},
        {{"duckdb", Std,    "COPY (SELECT MIN(start) FROM t) TO 'out.parquet'"}, true},
        {{"any",    Std,    "SELECT * INTO backup FROM t"}, true},
        {{"duckdb", Std,    "UNPIVOT t ON a, b INTO newtbl"}, true},
        {{"duckdb", Std,    "PIVOT (DELETE FROM t RETURNING *) ON a USING sum(b)"}, true},
        {{"any",    Std,    "SELECT 1; DROP TABLE t"}, true},
        {{"any",    Std,    "SELECT REPLACE(a,'x','y') FROM t; DELETE FROM t"}, true},
        {{"any",    Std,    "SELECT COUNT(comment) FROM t; COMMENT ON TABLE t IS 'x'"}, true},
        {{"any",    Std,    "SELECT 1 /* ; */ ; TRUNCATE t"}, true},
        {{"any",    Std,    "SELECT 'it''s' ; DROP TABLE t"}, true},
        {{"mysql",  Std,    "SELECT 'x\\'; DROP TABLE t; -- '"}, true},
        {{"pg",     Std,    "SELECT $$x$$; DROP TABLE t"}, true},
        {{"any",    Std,    "SELECT 1 -- c\r; DROP TABLE t"}, true},
        {{"any",    Std,    "SELECT 1 -- c\n; DROP TABLE t"}, true},
        {{"any",    Std,    "SELECT 1 -- c\r\n; DROP TABLE t"}, true},
        {{"any",    Std,    "SELECT 1 -- c\x0B; DROP TABLE t"}, true},
        {{"any",    Std,    "SELECT 1 -- c\x0C; DROP TABLE t"}, true},
        {{"any",    Std,    "SELECT 1 -- c\xC2\x85; DROP TABLE t"}, true},
        {{"any",    Std,    "SELECT 1 -- c\xE2\x80\xA8; DROP TABLE t"}, true},
        {{"any",    Std,    "SELECT 1 -- c\xE2\x80\xA9; DROP TABLE t"}, true},
        {{"mysql",  Std,    "SELECT 1 # c\r; DELETE FROM t"}, true},
        {{"tsql",   Strict, "SELECT MIN(start) FROM t DELETE FROM t"}, true},
        {{"tsql",   Strict, "SELECT REPLACE(a,'x','y') FROM t EXEC('DROP TABLE t')"}, true},
        {{"tsql",   Strict, "SELECT 1 -- c\rDROP TABLE t"}, true},
        {{"tsql",   Std,    "INSERT INTO t SELECT * FROM (DELETE FROM s OUTPUT deleted.*) d"}, true},
        {{"any",    Std,    "DELETE FROM t WHERE REPLACE(a,'x','y') = 'z'"}, true},
        {{"any",    Std,    "UPDATE t SET a = REPLACE(a,'x','y')"}, true},
        {{"pg",     Std,    "CALL p()"}, true},
        {{"pg",     Std,    "SET search_path = evil"}, true},
        {{"duckdb", Std,    "ATTACH 'x.db' AS x"}, true},
        {{"sqlite", Std,    "VACUUM"}, true},
        {{"duckdb", RF,     "PIVOT 'data.csv' ON year USING sum(amount)"}, false},
        {{"duckdb", RF,     "UNPIVOT '/etc/passwd' ON a INTO NAME k VALUE v"}, false},
        {{"duckdb", RF,     "SUMMARIZE 'data.parquet'"}, false},
    };

    std::printf("== ACCEPT: reads with keyword-named functions / columns ==\n");
    int nAccept = 0;
    for (const Row &r : accept) {
        const SqlVerdict v = verdict(r);
        report(v.singleStatement && v.readOnly && !v.mutation, r, "accept", v);
        ++nAccept;
    }
    std::printf("== REJECT: writes, stacking and parenthesised DML ==\n");
    int nReject = 0;
    for (const RejectRow &rr : reject) {
        const SqlVerdict v = verdict(rr.r);
        report(!(v.singleStatement && v.readOnly) && (!rr.mutation || v.mutation),
               rr.r, rr.mutation ? "reject+mutation" : "reject", v);
        ++nReject;
    }
    if (nAccept < 25 || nReject < 25) {
        ++g_fail;
        std::printf("  [FAIL] table too small (%d accept, %d reject)\n", nAccept, nReject);
    }

    std::printf("\n=== Summary: %d passed, %d failed ===\n", g_pass, g_fail);
    return g_fail == 0 ? 0 : 1;
}
