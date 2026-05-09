#include "src/duckdb_client.h"

#include <QFile>
#include <QTemporaryFile>
#include <QString>
#include <cstdio>
#include <cstdlib>

#define EXPECT(cond) \
    do { if (!(cond)) { \
        std::fprintf(stderr, "FAIL: %s line %d: %s\n", __FILE__, __LINE__, #cond); \
        std::exit(1); \
    } } while (0)

int main() {
    if (!DuckDb::Client::available()) {
        std::printf("test_duckdb: SKIP — built without DuckDB support\n");
        return 0;
    }

    // ── In-memory open + simple query ───────────────────────────────────
    {
        DuckDb::Client c;
        QString err;
        EXPECT(c.open(":memory:", &err));
        EXPECT(c.isOpen());

        DuckDb::ResultSet rs = c.exec("SELECT 1 AS a, 'hello' AS b");
        EXPECT(rs.errorMessage.isEmpty());
        EXPECT(rs.columns.size() == 2);
        EXPECT(rs.columns[0] == "a");
        EXPECT(rs.columns[1] == "b");
        EXPECT(rs.rows.size() == 1);
        EXPECT(rs.rows[0].values[0] == "1");
        EXPECT(rs.rows[0].values[1] == "hello");
    }

    // ── CSV register + select ───────────────────────────────────────────
    {
        QTemporaryFile csv;
        EXPECT(csv.open());
        const char *csvBody =
            "name,age,email\n"
            "alice,30,alice@example.com\n"
            "bob,25,bob@example.com\n"
            "carol,40,carol@example.com\n";
        csv.write(csvBody);
        csv.close();

        DuckDb::Client c;
        EXPECT(c.open(":memory:"));
        QString err;
        EXPECT(c.registerCsv(csv.fileName(), "people", &err));
        DuckDb::ResultSet rs = c.exec("SELECT name FROM people ORDER BY age DESC");
        EXPECT(rs.errorMessage.isEmpty());
        EXPECT(rs.rows.size() == 3);
        EXPECT(rs.rows[0].values[0] == "carol");
        EXPECT(rs.rows[1].values[0] == "alice");
        EXPECT(rs.rows[2].values[0] == "bob");
    }

    // ── Schema introspection ────────────────────────────────────────────
    {
        DuckDb::Client c;
        EXPECT(c.open(":memory:"));
        c.exec("CREATE TABLE customers (id INTEGER PRIMARY KEY, name VARCHAR, signed_up TIMESTAMP)");
        c.exec("CREATE TABLE orders (id INTEGER, customer_id INTEGER, total DECIMAL(10,2))");

        QString err;
        auto tables = c.listTables(&err);
        EXPECT(err.isEmpty());
        bool sawCustomers = false, sawOrders = false;
        for (const auto &t : tables) {
            if (t.name == "customers") sawCustomers = true;
            if (t.name == "orders")    sawOrders    = true;
        }
        EXPECT(sawCustomers);
        EXPECT(sawOrders);

        auto cols = c.describeTable("customers", &err);
        EXPECT(err.isEmpty());
        EXPECT(cols.size() == 3);
        EXPECT(cols[0].name == "id");
        EXPECT(cols[1].name == "name");
        EXPECT(cols[2].name == "signed_up");
    }

    // ── Streaming row-by-row ────────────────────────────────────────────
    {
        DuckDb::Client c;
        EXPECT(c.open(":memory:"));
        c.exec("CREATE TABLE numbers AS SELECT * FROM generate_series(1, 100) AS s(n)");

        int rowsSeen = 0;
        QString err;
        auto cols = c.execStreaming("SELECT n FROM numbers ORDER BY n",
            [&rowsSeen](const QStringList &, const QStringList &values) {
                if (values.size() != 1) return false;
                bool ok = false;
                int n = values[0].toInt(&ok);
                if (!ok || n != rowsSeen + 1) return false;
                ++rowsSeen;
                return true;  // keep going
            }, &err);
        EXPECT(err.isEmpty());
        EXPECT(cols.size() == 1);
        EXPECT(rowsSeen == 100);
    }

    // ── Streaming with early-exit ──────────────────────────────────────
    {
        DuckDb::Client c;
        EXPECT(c.open(":memory:"));
        int rowsSeen = 0;
        c.execStreaming("SELECT n FROM generate_series(1, 1000) AS s(n)",
            [&rowsSeen](const QStringList &, const QStringList &) {
                ++rowsSeen;
                return rowsSeen < 5;  // stop after 5 rows
            });
        EXPECT(rowsSeen == 5);
    }

    // ── Query error path — ensure errorMessage is populated, not crashed
    {
        DuckDb::Client c;
        EXPECT(c.open(":memory:"));
        DuckDb::ResultSet rs = c.exec("SELECT * FROM table_that_doesnt_exist");
        EXPECT(!rs.errorMessage.isEmpty());
    }

    // ── Open / close / re-open cycle (RAII, no leak) ────────────────────
    {
        DuckDb::Client c;
        for (int i = 0; i < 5; ++i) {
            EXPECT(c.open(":memory:"));
            EXPECT(c.isOpen());
            c.close();
            EXPECT(!c.isOpen());
        }
    }

    std::printf("test_duckdb: all assertions passed\n");
    return 0;
}
