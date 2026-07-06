// SPDX-License-Identifier: GPL-3.0-or-later
//
// READ-ONLY SQL CLASSIFIER GATE — regression test.
//
// Asserts the USER-VISIBLE security contract for the `query_sql` / `csv_query`
// agentic tools (Data Analyst mode): in the default no-approval path the model
// may run reads, but a confused / prompt-injected model CANNOT smuggle a
// mutation through it. Enforcement keys on DbConnections::isReadOnlyQuery()
// (see dbconnections.cpp: `if (!allowMutation && !isReadOnlyQuery(sql)) reject`).
//
// Before the classifier landed, the gate was a leading-keyword prefix check
// (sql.left(N)), so all of these executed with the connection's full
// privileges on Postgres / SQL Server / SQLite WITHOUT a human gate:
//   * WITH t AS (DELETE FROM logs RETURNING *) SELECT * FROM t   (CTE-embedded DML)
//   * EXPLAIN ANALYZE DELETE FROM ...                            (ANALYZE runs it)
//   * SELECT * INTO newtbl FROM src                              (creates a table)
//   * SELECT 1; DELETE FROM logs                                 (statement stacking)
//   * WITH t AS (UPDATE ... RETURNING *) SELECT ...              (CTE-wrapped UPDATE)
//
// This links the REAL shipped classifier (DbConnections::classifySql /
// isReadOnlyQuery / isMutation from dbconnections.cpp), not a copy, so it
// catches any future drift. The classifier BIASES TO REJECTION (fail-safe):
// where the real behaviour over-rejects a benign read, or under-labels a
// mutation, we assert the REAL behaviour and flag the discrepancy as a PRODUCT
// RISK in a comment rather than weakening the security assertion.
//
// Scaffold matches test_terminal_ansi.cpp: QCoreApplication + check() +
// "=== Summary: N passed, M failed ===", nonzero exit on failure. Pure
// Qt-Core string logic — offscreen-safe, no widgets, no modal ever created.

#include "dbconnections.h"

#include <QCoreApplication>
#include <QElapsedTimer>
#include <QString>
#include <cstdio>
#include <cstdlib>

using DbConnections::SqlVerdict;
using DbConnections::classifySql;
using DbConnections::isReadOnlyQuery;
using DbConnections::isMutation;

static int g_pass = 0, g_fail = 0;

static void check(const char *what, bool ok) {
    if (ok) { std::printf("  [PASS] %s\n", what); ++g_pass; }
    else    { std::printf("  [FAIL] %s\n", what); ++g_fail; }
}

// isReadOnlyQuery() is the sole no-approval-path gate. wantAllow==true means
// "safe to run without a human"; ==false means "must be rejected / gated".
static void expectRO(const char *what, const QString &sql, bool wantAllow) {
    const bool got = isReadOnlyQuery(sql);
    const bool ok = (got == wantAllow);
    if (ok) { std::printf("  [PASS] %s\n", what); ++g_pass; }
    else {
        std::printf("  [FAIL] %s  (want=%s got=%s)\n", what,
                    wantAllow ? "ALLOW" : "REJECT",
                    got ? "ALLOW" : "REJECT");
        ++g_fail;
    }
}

// isMutation() is the verdict a human-approval gate keys on.
static void expectMut(const char *what, const QString &sql, bool wantMutation) {
    const bool got = isMutation(sql);
    const bool ok = (got == wantMutation);
    if (ok) { std::printf("  [PASS] %s\n", what); ++g_pass; }
    else {
        std::printf("  [FAIL] %s  (want mutation=%s got=%s)\n", what,
                    wantMutation ? "true" : "false", got ? "true" : "false");
        ++g_fail;
    }
}

int main(int argc, char **argv) {
    QCoreApplication app(argc, argv);
    // Unbuffered so a hard crash mid-run still shows how far we got (offscreen
    // test-runner convention).
    std::setvbuf(stdout, nullptr, _IONBF, 0);

    std::printf("== the 5 attack payloads MUST be REJECTED by the read gate ==\n");

    // (1) CTE-wrapped DELETE — DML hidden in the WITH body; RETURNING confirms
    //     it mutates. Rejected, and reported as a mutation.
    {
        const QString sql = "WITH t AS (DELETE FROM logs RETURNING *) SELECT * FROM t";
        expectRO ("CTE-wrapped DELETE rejected",       sql, false);
        expectMut("CTE-wrapped DELETE is a mutation",  sql, true);
    }

    // (2) EXPLAIN ANALYZE DELETE — ANALYZE actually EXECUTES the wrapped DELETE.
    //     Correctly REJECTED from the read path (isReadOnlyQuery==false). But
    //     PRODUCT RISK: classifySql early-returns on the ANALYZE check before
    //     the mutation scan, so isMutation()==false even though the statement
    //     DOES mutate. Safe today (the read gate keys on isReadOnlyQuery, which
    //     rejects it), but a human-approval card keyed on isMutation would
    //     mislabel it as read-only. Assert the REAL (imperfect) value + flag.
    {
        const QString sql = "EXPLAIN ANALYZE DELETE FROM users WHERE id = 1";
        expectRO ("EXPLAIN ANALYZE DELETE rejected",   sql, false);
        // RISK: under-labelled as non-mutation (see comment above).
        expectMut("EXPLAIN ANALYZE DELETE mutation-label (real=false, RISK)",
                  sql, false);
    }

    // (3) SELECT ... INTO — creates a new table on SQL Server / Postgres.
    {
        const QString sql = "SELECT * INTO newtbl FROM src";
        expectRO ("SELECT INTO rejected",              sql, false);
        expectMut("SELECT INTO is a mutation",         sql, true);
    }

    // (4) Stacked SELECT; DELETE — two statements; stacking is rejected outright.
    {
        const QString sql = "SELECT 1; DELETE FROM logs";
        expectRO ("stacked SELECT;DELETE rejected",    sql, false);
        expectMut("stacked SELECT;DELETE is a mutation", sql, true);
        check("stacked SELECT;DELETE => singleStatement==false",
              classifySql(sql).singleStatement == false);
    }

    // (5) CTE-wrapped UPDATE.
    {
        const QString sql =
            "WITH t AS (UPDATE accounts SET bal = 0 RETURNING *) SELECT * FROM t";
        expectRO ("CTE-wrapped UPDATE rejected",       sql, false);
        expectMut("CTE-wrapped UPDATE is a mutation",  sql, true);
    }

    std::printf("== more mutation / DDL / side-effect shapes MUST be REJECTED ==\n");

    expectRO ("CTE-wrapped INSERT rejected",
              "WITH t AS (INSERT INTO a VALUES (1) RETURNING *) SELECT * FROM t", false);
    expectMut("CTE-wrapped INSERT is a mutation",
              "WITH t AS (INSERT INTO a VALUES (1) RETURNING *) SELECT * FROM t", true);

    expectRO ("bare DROP rejected",       "DROP TABLE users", false);
    expectMut("bare DROP is a mutation",  "DROP TABLE users", true);

    expectRO ("bare TRUNCATE rejected",       "TRUNCATE users", false);
    expectMut("bare TRUNCATE is a mutation",  "TRUNCATE users", true);

    expectRO ("bare ALTER rejected",       "ALTER TABLE users ADD COLUMN x INT", false);
    expectMut("bare ALTER is a mutation",  "ALTER TABLE users ADD COLUMN x INT", true);

    // COPY / EXPORT / ATTACH / INSTALL / LOAD / SET / CALL — DuckDB-family
    // side-effect + local-file / network exfiltration verbs.
    expectRO ("COPY ... FROM file rejected",   "COPY t FROM '/etc/passwd'", false);
    expectMut("COPY ... FROM file is a mutation", "COPY t FROM '/etc/passwd'", true);

    expectRO ("COPY (SELECT..) TO file rejected",
              "COPY (SELECT * FROM secrets) TO '/tmp/out.csv'", false);
    expectMut("COPY (SELECT..) TO file is a mutation",
              "COPY (SELECT * FROM secrets) TO '/tmp/out.csv'", true);

    expectRO ("ATTACH database rejected", "ATTACH DATABASE 'evil.db' AS e", false);
    expectMut("ATTACH database is a mutation", "ATTACH DATABASE 'evil.db' AS e", true);

    expectRO ("INSTALL extension rejected", "INSTALL httpfs", false);
    expectMut("INSTALL extension is a mutation", "INSTALL httpfs", true);

    expectRO ("LOAD extension rejected", "LOAD httpfs", false);
    expectMut("LOAD extension is a mutation", "LOAD httpfs", true);

    expectRO ("SET assignment rejected", "SET s3_region = 'us-east-1'", false);
    expectMut("SET assignment is a mutation", "SET s3_region = 'us-east-1'", true);

    expectRO ("CALL procedure rejected", "CALL pragma_version()", false);
    expectMut("CALL procedure is a mutation", "CALL pragma_version()", true);

    // Stacking hidden behind a block comment: the comment is stripped, leaving
    // two statements => rejected as stacked.
    expectRO ("stacked hidden by block comment rejected",
              "SELECT 1 /* x */; DROP TABLE t", false);
    expectMut("stacked hidden by block comment is a mutation",
              "SELECT 1 /* x */; DROP TABLE t", true);

    // Assignment-form PRAGMA is a write on SQLite. Rejected from the read path
    // AND (F4 fix) now correctly tagged as a mutation, so it routes to the human
    // approval card instead of failing silently. This upgrades the old
    // documented RISK (isMutation was false) to the honest verdict.
    expectRO ("assignment PRAGMA rejected", "PRAGMA journal_mode = WAL", false);
    expectMut("assignment PRAGMA is a mutation (F4)",
              "PRAGMA journal_mode = WAL", true);

    // Empty / whitespace / comment-only queries are not runnable reads.
    expectRO ("empty query rejected", "", false);
    expectRO ("comment-only query rejected", "/* nothing here */", false);

    std::printf("== legitimate read statements MUST be ALLOWED ==\n");

    expectRO("plain SELECT allowed",
             "SELECT id, name FROM users WHERE id > 10", true);
    expectMut("plain SELECT is not a mutation",
              "SELECT id, name FROM users WHERE id > 10", false);
    expectRO("CTE leading to SELECT allowed",
             "WITH t AS (SELECT * FROM orders) SELECT count(*) FROM t", true);
    expectRO("EXPLAIN SELECT allowed", "EXPLAIN SELECT * FROM users", true);
    expectRO("PRAGMA table_info allowed", "PRAGMA table_info(users)", true);
    expectMut("PRAGMA table_info is not a mutation", "PRAGMA table_info(users)", false);
    expectRO("SHOW TABLES allowed", "SHOW TABLES", true);
    expectRO("DESCRIBE allowed", "DESCRIBE users", true);
    expectRO("SUMMARIZE allowed", "SUMMARIZE sales", true);
    expectRO("VALUES allowed", "VALUES (1), (2), (3)", true);
    expectRO("TABLE shorthand allowed", "TABLE users", true);
    expectRO("single trailing semicolon allowed", "SELECT 1;", true);
    expectRO("leading line comment then SELECT allowed",
             "-- daily count\nSELECT count(*) FROM t", true);
    expectRO("leading block comment then SELECT allowed",
             "/* hello */ SELECT * FROM t", true);

    // FAIL-SAFE OVER-REJECTION (documented product risk, NOT a bug we hide):
    // `PRAGMA foreign_keys` in query form is a harmless read, but it is not on
    // the read-only PRAGMA allowlist, so the fail-safe classifier REJECTS it.
    // Assert the REAL behaviour so the test stays truthful; flagged as a
    // usability risk (a legit read needs manual approval).
    expectRO("PRAGMA not-on-allowlist over-rejected (real=REJECT, RISK)",
             "PRAGMA foreign_keys", false);

    std::printf("== keywords inside literals / comments MUST NOT trip the gate ==\n");

    expectRO("DELETE inside a string literal is inert",
             "SELECT 'please DELETE this row' AS note FROM t", true);
    expectMut("DELETE-in-literal is not a mutation",
              "SELECT 'please DELETE this row' AS note FROM t", false);
    expectRO("DROP inside a double-quoted identifier is inert",
             "SELECT \"DROP TABLE\" FROM t", true);
    expectRO("stacking ';' inside a trailing line comment is inert",
             "SELECT 1 -- ; DELETE FROM logs\n", true);
    expectRO("keyword inside block comment is inert",
             "SELECT /* DROP TABLE t */ 1 FROM t", true);
    expectMut("keyword inside block comment is not a mutation",
              "SELECT /* DROP TABLE t */ 1 FROM t", false);

    // ═══════════════════════════════════════════════════════════════════════
    // F5 — position-aware verb detection. A DML/DDL/side-effect keyword is only
    // a threat in VERB position (statement start / CTE body after '(' / after
    // EXPLAIN|ANALYZE). Used as an UNQUOTED identifier (a column, alias, or
    // table name) it is an innocent read a Data-mode model naturally writes. The
    // old "any forbidden token anywhere" scan false-rejected every one of these.
    // ═══════════════════════════════════════════════════════════════════════
    std::printf("== F5: keyword-named identifiers in read position MUST be ALLOWED ==\n");

    expectRO ("SELECT comment FROM posts allowed", "SELECT comment FROM posts", true);
    expectMut("SELECT comment FROM posts is not a mutation",
              "SELECT comment FROM posts", false);
    expectRO ("SELECT load FROM servers allowed", "SELECT load FROM servers", true);
    expectRO ("SELECT set FROM matches allowed", "SELECT set FROM matches", true);
    expectRO ("SELECT start,end FROM sessions allowed",
              "SELECT start,end FROM sessions", true);
    expectRO ("SELECT copy FROM documents allowed", "SELECT copy FROM documents", true);
    expectRO ("SELECT call FROM logs allowed", "SELECT call FROM logs", true);
    expectRO ("SELECT lock FROM t allowed", "SELECT lock FROM t", true);
    expectRO ("SELECT * FROM comment allowed", "SELECT * FROM comment", true);

    // The verb-position rule MUST NOT weaken any existing catch: DML in a CTE
    // body, stacked DML, EXPLAIN ANALYZE DELETE, and SELECT ... INTO all still
    // sit in (or reduce to) verb position and stay rejected.
    expectRO ("F5 keeps CTE-embedded DELETE rejected",
              "WITH t AS (DELETE FROM logs RETURNING *) SELECT * FROM t", false);
    expectRO ("F5 keeps stacked SELECT;DELETE rejected",
              "SELECT 1; DELETE FROM logs", false);
    expectRO ("F5 keeps EXPLAIN ANALYZE DELETE rejected",
              "EXPLAIN ANALYZE DELETE FROM users WHERE id = 1", false);
    expectRO ("F5 keeps SELECT INTO rejected", "SELECT * INTO newtbl FROM src", false);

    // ═══════════════════════════════════════════════════════════════════════
    // F5b — the verb-position rewrite (F5) that stopped false-rejecting
    // keyword-named identifiers ALSO blinded the classifier to the OUTER DML
    // verb of a LEADING-CTE statement: structTokens opened a verb slot at
    // statement start, after '(', and after EXPLAIN|ANALYZE, but NOT for the
    // first token after the top-level ')' that closes the WITH/CTE list. So
    //   WITH c AS (SELECT 1) DELETE FROM t
    // classified read-only / non-mutation and ran with NO approval card
    // (QSQLITE + QODBC have no session read-only backstop — the classifier is
    // the ONLY layer). Fix: paren-depth tracking opens a verb slot for the
    // outer-query leading token of a CTE statement, so the outer DELETE /
    // UPDATE / DROP / TRUNCATE / MERGE / INSERT is caught, while a CTE that
    // leads to SELECT stays allowed.
    // ═══════════════════════════════════════════════════════════════════════
    std::printf("== F5b: WITH-CTE outer-DML MUST be REJECTED + gated ==\n");

    {
        struct { const char *name; const char *sql; } cases[] = {
            {"WITH-CTE DELETE",            "WITH c AS (SELECT 1) DELETE FROM t"},
            {"WITH-CTE UPDATE",            "WITH c AS (SELECT 1) UPDATE t SET x=1"},
            {"WITH-CTE UPDATE FROM (PG)",  "WITH c AS (SELECT 1) UPDATE t SET x=1 FROM c"},
            {"WITH-CTE DROP",              "WITH c AS (SELECT 1) DROP TABLE t"},
            {"WITH-CTE TRUNCATE",          "WITH c AS (SELECT 1) TRUNCATE t"},
            {"multi-CTE DELETE",           "WITH a AS (SELECT 1), b AS (SELECT 2) DELETE FROM t"},
            {"WITH RECURSIVE DELETE",      "WITH RECURSIVE c AS (SELECT 1) DELETE FROM t"},
            {"WITH-CTE MERGE (T-SQL)",
             "WITH c AS (SELECT 1) MERGE INTO tgt USING c ON tgt.id=c.id WHEN MATCHED THEN DELETE"},
            {"WITH-CTE INSERT",            "WITH c AS (SELECT 1) INSERT INTO t SELECT * FROM c"},
        };
        for (const auto &tc : cases) {
            const QString sql = QString::fromLatin1(tc.sql);
            char buf[160];
            std::snprintf(buf, sizeof(buf), "%s rejected", tc.name);
            expectRO(buf, sql, false);
            std::snprintf(buf, sizeof(buf), "%s is a mutation", tc.name);
            expectMut(buf, sql, true);
        }
    }

    // DML INSIDE the CTE paren is still caught (verb slot after '('), unchanged
    // by the outer-slot addition.
    expectRO ("F5b keeps DML-inside-CTE rejected",
              "WITH t AS (DELETE FROM logs RETURNING *) SELECT * FROM t", false);

    std::printf("== F5b: WITH-CTE leading to a read MUST be ALLOWED ==\n");

    expectRO ("WITH-CTE -> SELECT allowed",
              "WITH c AS (SELECT 1) SELECT * FROM c", true);
    expectMut("WITH-CTE -> SELECT is not a mutation",
              "WITH c AS (SELECT 1) SELECT * FROM c", false);
    expectRO ("WITH-CTE(count) -> SELECT allowed",
              "WITH c AS (SELECT * FROM orders) SELECT count(*) FROM c", true);
    expectRO ("multi-CTE -> SELECT JOIN allowed",
              "WITH a AS (SELECT 1),b AS (SELECT 2) SELECT * FROM a JOIN b", true);
    expectRO ("WITH RECURSIVE -> SELECT allowed",
              "WITH RECURSIVE c AS (SELECT 1 UNION ALL SELECT n+1 FROM c) SELECT * FROM c", true);
    expectRO ("WITH-CTE column-list form -> SELECT allowed",
              "WITH c(a,b) AS (SELECT 1,2) SELECT * FROM c", true);
    // Nested parens inside the CTE body must not confuse the outer-slot logic.
    expectRO ("WITH-CTE nested-paren body -> DELETE still rejected",
              "WITH c AS (SELECT (SELECT 1)) DELETE FROM t", false);
    expectMut("WITH-CTE nested-paren body DELETE is a mutation",
              "WITH c AS (SELECT (SELECT 1)) DELETE FROM t", true);

    // ═══════════════════════════════════════════════════════════════════════
    // F2 — side-effecting / pass-through FUNCTION names slip through the verb
    // scan (a bare function name is no forbidden verb) and OPENQUERY hides its
    // DML in a string literal that gets blanked. Deny-list them by name-then-'('
    // (+ the T-SQL NEXT VALUE FOR sequence keyword). All must be REJECTED from
    // the read path AND tagged as a mutation so they hit the approval gate.
    // ═══════════════════════════════════════════════════════════════════════
    std::printf("== F2: side-effecting function calls MUST be REJECTED + gated ==\n");

    expectRO ("SELECT load_extension() rejected",
              "SELECT load_extension('/tmp/evil.so')", false);
    expectMut("SELECT load_extension() is a mutation",
              "SELECT load_extension('/tmp/evil.so')", true);

    expectRO ("SELECT * FROM OPENQUERY(...) rejected",
              "SELECT * FROM OPENQUERY(srv,'DELETE FROM x')", false);
    expectMut("SELECT * FROM OPENQUERY(...) is a mutation",
              "SELECT * FROM OPENQUERY(srv,'DELETE FROM x')", true);

    expectRO ("SELECT writefile() rejected",
              "SELECT writefile('/tmp/x','data')", false);
    expectMut("SELECT writefile() is a mutation",
              "SELECT writefile('/tmp/x','data')", true);

    expectRO ("SELECT setval() rejected", "SELECT setval('s',1)", false);
    expectMut("SELECT setval() is a mutation", "SELECT setval('s',1)", true);

    expectRO ("SELECT NEXT VALUE FOR seq rejected",
              "SELECT NEXT VALUE FOR seq", false);
    expectMut("SELECT NEXT VALUE FOR seq is a mutation",
              "SELECT NEXT VALUE FOR seq", true);

    // A deny-listed word that is NOT a call (bare column reference) stays an
    // allowed read — the deny-list keys on name-immediately-followed-by-'('.
    expectRO ("bare 'nextval' column reference allowed",
              "SELECT nextval FROM sequences", true);

    // Genuine pass-throughs the re-attack found still slipping: PG admin /
    // signalling functions + DuckDB query()/query_table() nested-statement
    // table functions. Call-position keyed → reject + tagged as a mutation.
    expectRO ("pg_terminate_backend() rejected",
              "SELECT pg_terminate_backend(123)", false);
    expectMut("pg_terminate_backend() is a mutation",
              "SELECT pg_terminate_backend(123)", true);
    expectRO ("pg_cancel_backend() rejected",
              "SELECT pg_cancel_backend(123)", false);
    expectMut("pg_cancel_backend() is a mutation",
              "SELECT pg_cancel_backend(123)", true);
    expectRO ("pg_stat_reset() rejected", "SELECT pg_stat_reset()", false);
    expectMut("pg_stat_reset() is a mutation", "SELECT pg_stat_reset()", true);
    expectRO ("pg_logical_emit_message() rejected",
              "SELECT pg_logical_emit_message(true,'p','x')", false);
    expectMut("pg_logical_emit_message() is a mutation",
              "SELECT pg_logical_emit_message(true,'p','x')", true);
    expectRO ("pg_reload_conf() rejected", "SELECT pg_reload_conf()", false);
    expectMut("pg_reload_conf() is a mutation", "SELECT pg_reload_conf()", true);
    expectRO ("query('DELETE..') pass-through rejected",
              "SELECT * FROM query('DELETE FROM t')", false);
    expectMut("query('DELETE..') pass-through is a mutation",
              "SELECT * FROM query('DELETE FROM t')", true);
    expectRO ("query_table('t') pass-through rejected",
              "SELECT * FROM query_table('t')", false);
    expectMut("query_table('t') pass-through is a mutation",
              "SELECT * FROM query_table('t')", true);

    // …but a column literally named `query` (no call parens) stays a read.
    expectRO ("bare 'query' column reference allowed",
              "SELECT query FROM t", true);
    expectMut("bare 'query' column reference is not a mutation",
              "SELECT query FROM t", false);

    // ═══════════════════════════════════════════════════════════════════════
    // F4 — PRAGMA parens-assignment bypass. PRAGMA user_version(9999) has no
    // '=', so the old assignment-only reject missed it → a silent SQLite header
    // write. Settable pragmas are allowed ONLY as a pure read; any argument
    // (parens, space, or '=') is a write → rejected + tagged as a mutation.
    // ═══════════════════════════════════════════════════════════════════════
    std::printf("== F4: settable-PRAGMA writes (parens/space/=) MUST be REJECTED ==\n");

    expectRO ("PRAGMA user_version (pure read) allowed", "PRAGMA user_version", true);
    expectMut("PRAGMA user_version (pure read) is not a mutation",
              "PRAGMA user_version", false);

    expectRO ("PRAGMA user_version(5) parens-write rejected",
              "PRAGMA user_version(5)", false);
    expectMut("PRAGMA user_version(5) is a mutation", "PRAGMA user_version(5)", true);

    expectRO ("PRAGMA user_version = 5 assignment-write rejected",
              "PRAGMA user_version = 5", false);
    expectMut("PRAGMA user_version = 5 is a mutation", "PRAGMA user_version = 5", true);

    expectRO ("PRAGMA user_version (5) spaced-parens-write rejected",
              "PRAGMA user_version (5)", false);
    expectMut("PRAGMA user_version (5) is a mutation", "PRAGMA user_version (5)", true);

    // Introspection pragma with a paren arg stays an allowed read.
    expectRO ("PRAGMA table_info(users) still allowed", "PRAGMA table_info(users)", true);

    std::printf("== SqlVerdict surface (reason / singleStatement) ==\n");
    {
        const SqlVerdict good = classifySql("SELECT 1");
        check("clean SELECT => readOnly", good.readOnly);
        check("clean SELECT => singleStatement", good.singleStatement);
        check("clean SELECT => empty reason", good.reason.isEmpty());

        const SqlVerdict bad =
            classifySql("WITH t AS (DELETE FROM logs RETURNING *) SELECT * FROM t");
        check("rejected query => not readOnly", !bad.readOnly);
        check("rejected query => nonempty reason", !bad.reason.isEmpty());
    }

    // ── Perf ceiling: prove the classifier stays LINEAR and that a huge string
    //    literal packed with write keywords is both (a) inert and (b) not a
    //    catastrophic-backtracking sink. A future ReDoS-style rewrite of
    //    stripSqlNoise would blow this bound. QElapsedTimer per project rule
    //    (a correctness-only assertion passes both broken+fixed perf code).
    std::printf("== perf ceiling: large keyword-laden literal stays linear ==\n");
    {
        QString big = "SELECT '";
        big += QString("DELETE FROM x; DROP TABLE y; TRUNCATE z; ").repeated(20000);
        big += "' AS note FROM t";   // keywords are all inside one string literal
        QElapsedTimer timer;
        timer.start();
        const bool allowed = isReadOnlyQuery(big);
        const qint64 ms = timer.elapsed();
        check("~880KB literal of write-keywords is still an allowed read", allowed);
        check("~880KB classify under 2000ms ceiling (linear, no ReDoS)", ms < 2000);
        std::printf("    (classified %d chars in %lld ms)\n",
                    int(big.size()), static_cast<long long>(ms));
    }

    std::printf("\n=== Summary: %d passed, %d failed ===\n", g_pass, g_fail);
    return g_fail == 0 ? 0 : 1;
}
