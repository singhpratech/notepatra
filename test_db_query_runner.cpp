// SPDX-License-Identifier: GPL-3.0-or-later
//
// DB query cancel regression test — the SHIPPING interrupt path.
//
// F1 (v0.1.115): the AI DB tool (query_sql via AIPanel::runDbToolGuarded) used
// to ABANDON a slow query on timeout/Stop — the DuckDB query kept running to
// completion in a detached worker (a wasted core; repeated timeouts saturated
// the pool) and nothing ever called duckdb_interrupt. The (now-deleted) async
// DbQueryRunner that a prior test exercised was wired into NOTHING in the app.
//
// The fix threads a DbConnections::QueryInterruptToken through the REAL shipping
// query path — DbConnections::runQuery(..., token) — which binds the live
// DuckDb::Client for the duration of exec() so a controller thread can call
// token->requestInterrupt() to genuinely abort the query (duckdb_interrupt). THIS
// test targets that exact mechanism (runQuery + token), not dead code.
//
// Assertions:
//   0. SQL classifier port — isReadOnlyQuery / isMutation / classifySql.
//   3. Row cap — a query over 1e8 rows returns EXACTLY maxRows (LIMIT rowCap+1
//      short-circuits before materialization; no OOM). Via runQuery.
//   4. Read-only at the engine — an on-disk DuckDB opened readOnly=true REFUSES
//      a CREATE TABLE; the :memory: scratch stays writable; runQuery's default
//      (allowMutation=false) path refuses a mutation.
//   1. MID-FLIGHT INTERRUPT — a token->requestInterrupt() fired while a
//      1e12-predicate cross join is running aborts it. A QElapsedTimer CEILING
//      proves it returned near the interrupt window, NOT after the full query.
//   2. EARLY CANCEL — a token flagged BEFORE the query starts is honored (exec()
//      no longer erases a pending interrupt at its top — the F1 sub-bug). The
//      heavy query returns near-instantly instead of running for minutes.
//
// SAFETY: this test can never hang. The heavy interrupt cases run runQuery on a
// std::thread; if a BROKEN interrupt never returns, the thread is DETACHED (never
// joined — joining would block on the un-interruptible query) and the process
// hard-exits via _exit, so a red-state failure is a FAST clean FAIL, not a wedge.
// (The shipping caller uses QtConcurrent; the interrupt mechanism under test —
// runQuery + QueryInterruptToken — is identical.)

#include "dbconnections.h"
#include "duckdb_client.h"

#include <QCoreApplication>
#include <QElapsedTimer>
#include <QString>
#include <QStringList>
#include <QTemporaryDir>

#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <thread>

static int g_pass = 0, g_fail = 0;
static void check(const char *label, bool ok) {
    if (ok) { ++g_pass; std::printf("  [PASS] %s\n", label); }
    else    { ++g_fail; std::printf("  [FAIL] %s\n", label); }
    std::fflush(stdout);
}

// A :memory: DuckDB record (writable scratch — the CSV-ingestion path).
static DbConnections::Record memRecord() {
    DbConnections::Record r;
    r.name = QStringLiteral("scratch");
    r.driver = QStringLiteral("DUCKDB");
    r.database = QStringLiteral(":memory:");
    return r;
}

// Long-running, interruptible, NON-collapsible query: a filtered cross join of
// two 1e6-element generate_series (1e12 predicate evaluations). The optimizer
// cannot rewrite it into a cardinality product, and a single count(*) row means
// LIMIT rowCap+1 can never short-circuit it — so the ONLY thing that can end it
// early is requestInterrupt() (duckdb_interrupt). Uninterrupted it runs for
// minutes; a working interrupt ends it in well under a second.
static QString heavyQuery() {
    return QStringLiteral(
        "SELECT count(*) FROM generate_series(1, 1000000) AS a(x), "
        "generate_series(1, 1000000) AS b(y) WHERE (a.x * b.y) % 7 = 3");
}

// Print the summary and hard-exit (skips static destructors so a detached,
// still-churning worker thread from a BROKEN interrupt can never wedge shutdown).
[[noreturn]] static void hardFailExit() {
    std::printf("\n=== Summary: %d passed, %d failed ===\n", g_pass, g_fail);
    std::fflush(stdout);
    std::_Exit(1);   // skip static dtors so a detached churning worker can't wedge exit
}

// Result of driving one heavy query to completion (or to a hard failsafe).
struct InterruptOutcome {
    bool completed = false;     // false => still churning at failsafe (broken)
    DbConnections::QueryResult result;
    qint64 elapsedMs = -1;
};

// Run runQuery(sql) on a worker thread and interrupt it via the shipping token.
//   preFlag=true  → the token is flagged BEFORE the query starts (early cancel).
//   preFlag=false → the token is flagged `interruptAfterMs` into the run.
// Returns once the query finishes OR a hard failsafe elapses; on failsafe the
// worker is DETACHED (never joined) so the test fast-fails instead of hanging.
static InterruptOutcome runInterruptible(const DbConnections::Record &rec,
                                         const QString &sql, int cap,
                                         bool preFlag, int interruptAfterMs,
                                         int failsafeMs) {
    InterruptOutcome oc;
    auto token  = std::make_shared<DbConnections::QueryInterruptToken>();
    auto done   = std::make_shared<std::atomic<bool>>(false);
    auto result = std::make_shared<DbConnections::QueryResult>();
    auto elapsed= std::make_shared<qint64>(-1);
    auto timer  = std::make_shared<QElapsedTimer>();
    timer->start();

    if (preFlag) token->requestInterrupt();   // early cancel BEFORE the query runs

    std::thread th([token, done, result, elapsed, timer, rec, sql, cap]() {
        DbConnections::QueryResult r =
            DbConnections::runQuery(rec, sql, cap, /*allowMutation=*/false, token.get());
        *result  = r;
        *elapsed = timer->elapsed();
        done->store(true);
    });

    if (!preFlag && interruptAfterMs >= 0) {
        // Give the worker time to open + bind + start exec(), then interrupt.
        std::this_thread::sleep_for(std::chrono::milliseconds(interruptAfterMs));
        token->requestInterrupt();
    }

    while (!done->load() && timer->elapsed() < failsafeMs)
        std::this_thread::sleep_for(std::chrono::milliseconds(10));

    if (done->load()) {
        th.join();
        oc.completed = true;
        oc.result    = *result;
        oc.elapsedMs = *elapsed;
    } else {
        th.detach();                  // broken interrupt still churning — abandon
        oc.elapsedMs = timer->elapsed();
    }
    return oc;
}

int main(int argc, char **argv) {
    std::setvbuf(stdout, nullptr, _IONBF, 0);   // unbuffered — offscreen-safe
    QCoreApplication app(argc, argv);

    if (!DuckDb::Client::available()) {
        std::printf("test_db_query_runner: SKIP — built without DuckDB support\n");
        return 0;
    }

    using DbConnections::isReadOnlyQuery;
    using DbConnections::isMutation;
    using DbConnections::classifySql;

    // ── 0. SQL classifier port ─────────────────────────────────────────────
    std::printf("== [0] SQL classifier: isReadOnlyQuery / isMutation / classifySql ==\n");
    check("isReadOnlyQuery(plain SELECT) == true",
          isReadOnlyQuery("SELECT id, name FROM users WHERE id > 10"));
    check("isReadOnlyQuery(DROP) == false",
          !isReadOnlyQuery("DROP TABLE users"));
    check("isReadOnlyQuery(CTE-embedded DELETE) == false",
          !isReadOnlyQuery("WITH t AS (DELETE FROM logs RETURNING *) SELECT * FROM t"));
    check("isReadOnlyQuery(stacked SELECT; DELETE) == false",
          !isReadOnlyQuery("SELECT 1; DELETE FROM logs"));
    check("isMutation(DROP) == true", isMutation("DROP TABLE users"));
    check("isMutation(plain SELECT) == false", !isMutation("SELECT 1"));
    {
        const DbConnections::SqlVerdict v = classifySql("SELECT 1; DROP TABLE t");
        check("classifySql(stacked).singleStatement == false", !v.singleStatement);
        check("classifySql(stacked).readOnly == false", !v.readOnly);
        check("classifySql(stacked).reason non-empty", !v.reason.isEmpty());
    }

    // ── 3. Row cap — capped without materializing the full input (via runQuery) ─
    std::printf("== [3] row cap: LIMIT rowCap+1 short-circuits 1e8 rows (no OOM) ==\n");
    {
        // 1e8 rows: if the cap didn't short-circuit BEFORE materialization this
        // would buffer ~800MB / hang. A working cap returns 5 rows instantly.
        DbConnections::QueryResult qr = DbConnections::runQuery(
            memRecord(),
            QStringLiteral("SELECT n FROM generate_series(1, 100000000) AS s(n)"),
            /*maxRows=*/5, /*allowMutation=*/false);
        check("row-cap query ok", qr.ok);
        check("row-cap capped to EXACTLY maxRows (5)", qr.rows.size() == 5);
        check("row-cap rowsReturned == 5", qr.rowsReturned == 5);
        check("row-cap truncated flag set", qr.truncated);
    }

    // ── 4. Read-only enforcement ───────────────────────────────────────────
    std::printf("== [4a] engine read-only: on-disk READ_ONLY refuses writes; :memory: writable ==\n");
    {
        QTemporaryDir dir;
        check("temp dir created", dir.isValid());
        const QString dbPath = dir.path() + QStringLiteral("/ro_probe.duckdb");

        // 1) Create the file writable and seed a table.
        {
            DuckDb::Client w;
            QString err;
            check("open on-disk writable", w.open(dbPath, &err, /*readOnly=*/false));
            DuckDb::ResultSet rc = w.exec("CREATE TABLE t (x INTEGER)");
            check("writable open: CREATE TABLE succeeds", rc.errorMessage.isEmpty());
            w.exec("INSERT INTO t VALUES (1), (2)");
        }
        // 2) Reopen READ_ONLY: a write MUST be refused by the engine itself.
        {
            DuckDb::Client ro;
            QString err;
            check("reopen on-disk READ_ONLY", ro.open(dbPath, &err, /*readOnly=*/true));
            DuckDb::ResultSet wr = ro.exec("CREATE TABLE t2 (y INTEGER)");
            check("READ_ONLY: CREATE TABLE refused by engine",
                  !wr.errorMessage.isEmpty());
            DuckDb::ResultSet rd = ro.exec("SELECT count(*) FROM t");
            check("READ_ONLY: SELECT still allowed",
                  rd.errorMessage.isEmpty() && rd.rows.size() == 1 &&
                  rd.rows[0].values.value(0) == "2");
        }
        // 3) In-memory scratch (CSV-ingestion path) MUST stay writable.
        {
            DuckDb::Client mem;
            check("open :memory:", mem.open(QStringLiteral(":memory:")));
            DuckDb::ResultSet wr = mem.exec("CREATE TABLE scratch (z INTEGER)");
            check(":memory: scratch stays writable (CREATE succeeds)",
                  wr.errorMessage.isEmpty());
        }
    }

    std::printf("== [4b] runQuery default path (allowMutation=false) refuses a mutation ==\n");
    {
        DbConnections::QueryResult qr = DbConnections::runQuery(
            memRecord(), QStringLiteral("CREATE TABLE hack (x INTEGER)"),
            /*maxRows=*/100, /*allowMutation=*/false);
        check("mutation on default path -> not ok", !qr.ok);
        check("mutation rejected with errorKind non_select",
              qr.errorKind == QStringLiteral("non_select"));
    }

    // ── 1. MID-FLIGHT INTERRUPT — the perf-bound that proves the token bites ─
    std::printf("== [1] INTERRUPT: token->requestInterrupt() aborts mid-flight ~interrupt, "
                "NOT after full query (perf ceiling) ==\n");
    {
        const int kInterruptAtMs = 300;   // fire once the worker is definitely live
        const int kCeilingMs     = 6000;  // perf bound: green ~0.3-1s; a broken
                                          // interrupt overshoots this (minutes)
        const int kFailsafeMs    = 25000; // test-level hang guard (< ctest 180)

        InterruptOutcome oc = runInterruptible(memRecord(), heavyQuery(),
                                               /*cap=*/1, /*preFlag=*/false,
                                               kInterruptAtMs, kFailsafeMs);
        // A broken interrupt never completes → detached → hard fast-fail here.
        check("interrupt returned (query did NOT run to completion)", oc.completed);
        if (!oc.completed) hardFailExit();
        // CRITICAL perf bound: proves the interrupt aborted the query near the
        // interrupt window instead of running the full 1e12-row product.
        check("interrupt returns within perf ceiling (duckdb_interrupt worked)",
              oc.elapsedMs >= 0 && oc.elapsedMs < kCeilingMs);
        check("interrupted query is NOT ok (aborted, not a clean result)",
              !oc.result.ok);
        check("interrupted query flagged cancelled",
              oc.result.errorKind == QStringLiteral("cancelled"));
        std::printf("      [info] mid-flight interrupt elapsed = %lld ms (ceiling %d)\n",
                    (long long)oc.elapsedMs, kCeilingMs);
    }

    // ── 2. EARLY CANCEL — a pre-exec interrupt is honored (exec() reset sub-bug) ─
    std::printf("== [2] EARLY CANCEL: token flagged BEFORE the query starts is honored "
                "(exec() no longer erases a pending interrupt) ==\n");
    {
        const int kCeilingMs  = 6000;
        const int kFailsafeMs = 25000;

        InterruptOutcome oc = runInterruptible(memRecord(), heavyQuery(),
                                               /*cap=*/1, /*preFlag=*/true,
                                               /*interruptAfterMs=*/-1, kFailsafeMs);
        check("early-cancel returned (pending interrupt was NOT erased by exec())",
              oc.completed);
        if (!oc.completed) hardFailExit();
        check("early-cancel returns within perf ceiling (honored before full run)",
              oc.elapsedMs >= 0 && oc.elapsedMs < kCeilingMs);
        check("early-cancel query is NOT ok", !oc.result.ok);
        std::printf("      [info] early-cancel elapsed = %lld ms (ceiling %d)\n",
                    (long long)oc.elapsedMs, kCeilingMs);
    }

    std::printf("\n=== Summary: %d passed, %d failed ===\n", g_pass, g_fail);
    return g_fail == 0 ? 0 : 1;
}
