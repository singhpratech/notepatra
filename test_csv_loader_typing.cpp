// SPDX-License-Identifier: GPL-3.0-or-later
//
// CSV loader typing — v0.1.133 regression test for the Data Analyst csv_query path.
//
// The loader was line-based and string-typed, so (1) quoted fields holding a
// delimiter or a line break were split into wrong rows, (2) "$1,234.50",
// "1,234", "(1,234.50)" and "12.5%" were stored as TEXT and SUM/AVG were wrong,
// (3) "4/15/2026" dates were stored verbatim so ORDER BY sorted them as text,
// (4) a header with an empty or numeric cell was loaded as a data row, and
// (5) the preview handed to the model named a column "Unit Price" while the
// table column is Unit_Price, so SQL written from the preview failed.
// Every assertion runs real SQL over the table loadIntoSqlite builds.

#include "csvanalyst.h"

#include <QCoreApplication>
#include <QElapsedTimer>
#include <QFile>
#include <QSqlDatabase>
#include <QSqlError>
#include <QSqlQuery>
#include <QSqlRecord>
#include <QStringList>
#include <QTemporaryDir>
#include <QVariant>

#include <cmath>
#include <cstdio>

static int g_pass = 0, g_fail = 0;

static void check(const char *what, bool ok, const QString &detail = QString()) {
    if (ok) { std::printf("  [PASS] %s\n", what); ++g_pass; }
    else {
        std::printf("  [FAIL] %s  %s\n", what, detail.toUtf8().constData());
        ++g_fail;
    }
}

static QString writeFile(const QTemporaryDir &dir, const QString &name, const QByteArray &body) {
    const QString path = dir.filePath(name);
    QFile f(path);
    if (!f.open(QIODevice::WriteOnly)) return QString();
    f.write(body);
    return path;
}

// Loads `path`, runs each SQL and returns the first row (columns joined by '|').
// A failed load / query yields "ERROR: ...".
static QStringList run(const QString &path, const QStringList &sqls) {
    QStringList out;
    QString cname, err;
    bool truncated = false;
    if (!CsvAnalyst::loadIntoSqlite(path, 250000, &cname, &truncated, &err)) {
        for (int i = 0; i < sqls.size(); ++i) out << QStringLiteral("ERROR: load: ") + err;
        return out;
    }
    {
        QSqlDatabase db = QSqlDatabase::database(cname);
        for (const QString &sql : sqls) {
            QSqlQuery q(db);
            if (!q.exec(sql)) { out << QStringLiteral("ERROR: ") + q.lastError().text(); continue; }
            if (!q.next()) { out << QStringLiteral("(no rows)"); continue; }
            QStringList cols;
            for (int i = 0; i < q.record().count(); ++i) cols << q.value(i).toString();
            out << cols.join(QLatin1Char('|'));
        }
    }
    QSqlDatabase::removeDatabase(cname);
    return out;
}

static QString one(const QString &path, const QString &sql) { return run(path, {sql}).value(0); }

static bool near(const QString &got, double want) {
    bool ok = false;
    const double d = got.toDouble(&ok);
    return ok && std::fabs(d - want) < 1e-9;
}

// ── 1a: RFC 4180 records ──────────────────────────────────────────────────
static void testRfc4180(const QTemporaryDir &dir) {
    std::printf("== RFC 4180: quotes, doubled quotes, line breaks, CRLF, BOM ==\n");
    const QString p = writeFile(dir, "rfc.csv",
        "\xEF\xBB\xBFid,name,notes,amount\r\n"
        "1,\"Smith, John\",\"He said \"\"hi\"\"\",10\r\n"
        "2,\"Doe, Jane\",\"line one\r\nline two, still notes\",20\r\n"
        "3,Plain,\"a;b|c\",30\r\n");
    const QStringList r = run(p, {
        "SELECT COUNT(*) FROM csv",
        "SELECT name FROM csv WHERE id = 1",
        "SELECT notes FROM csv WHERE id = 1",
        "SELECT notes FROM csv WHERE id = 2",
        "SELECT SUM(amount), SUM(id) FROM csv",
        "SELECT typeof(amount) FROM csv WHERE id = 3",
    });
    check("3 records (quoted comma / line break do not split rows)", r.value(0) == "3", r.value(0));
    check("quoted delimiter kept inside the field", r.value(1) == "Smith, John", r.value(1));
    check("doubled quotes unescaped", r.value(2) == "He said \"hi\"", r.value(2));
    check("embedded CRLF line break kept as one field",
          r.value(3) == "line one\nline two, still notes", r.value(3));
    check("SUM(amount) = 60, SUM(id) = 6 (BOM did not rename the first column)",
          r.value(4) == "60|6", r.value(4));
    check("amount typed integer", r.value(5) == "integer", r.value(5));

    const QString inch = writeFile(dir, "inch.csv",
        "item,size,price\nTV,5\" screen,100\nRadio,small,20\nLamp,9\" shade,5\n");
    check("unquoted inch mark does not swallow later rows",
          one(inch, "SELECT COUNT(*), SUM(price) FROM csv") == "3|125",
          one(inch, "SELECT COUNT(*), SUM(price) FROM csv"));
}

// ── 1b: currency / grouped / accounting / percent numbers ──────────────────
static void testNumbers(const QTemporaryDir &dir) {
    std::printf("== numbers: $1,234.50  1,234  (1,234.50)  12.5%% ==\n");
    const QString p = writeFile(dir, "money.csv",
        "item,price,qty,growth,eu,mixed\n"
        "Widget,\"$1,234.50\",\"1,200\",12.5%,\"1.234,50\",$5\n"
        "Gadget,\"$2,469.25\",34,-2.5%,\"2,5\",\xE2\x82\xAC" "5\n"
        "Refund,($100.00),\"1,000\",0%,\"7,25\",$6\n"
        "Gizmo,$100,5,5%,\"3,75\",$7\n");
    const QStringList r = run(p, {
        "SELECT SUM(price) FROM csv",
        "SELECT typeof(price) FROM csv WHERE item = 'Widget'",
        "SELECT SUM(qty), typeof(qty) FROM csv WHERE item = 'Widget'",
        "SELECT SUM(qty) FROM csv",
        "SELECT AVG(growth), MAX(growth), typeof(growth) FROM csv",
        "SELECT item FROM csv ORDER BY price DESC",
        "SELECT price FROM csv WHERE item = 'Refund'",
        "SELECT typeof(eu) FROM csv WHERE item = 'Widget'",
        "SELECT typeof(mixed) FROM csv WHERE item = 'Widget'",
    });
    check("SUM over the currency column = 3703.75", near(r.value(0), 3703.75), r.value(0));
    check("currency stored as REAL", r.value(1) == "real", r.value(1));
    check("\"1,200\" stored as integer 1200", r.value(2) == "1200|integer", r.value(2));
    check("SUM(qty) = 2239", r.value(3) == "2239", r.value(3));
    check("percent stored as points: AVG 3.75, MAX 12.5, real",
          r.value(4) == "3.75|12.5|real", r.value(4));
    check("ORDER BY price DESC sorts numerically (Gadget first)", r.value(5) == "Gadget", r.value(5));
    check("accounting (100.00) stored as -100", near(r.value(6), -100.0), r.value(6));
    check("decimal-comma \"1.234,50\" stays TEXT (no locale guess)", r.value(7) == "text", r.value(7));
    check("mixed $ / EUR column stays TEXT", r.value(8) == "text", r.value(8));

    const CsvAnalyst::CsvSchema s = CsvAnalyst::detectSchema(p);
    check("schema: price REAL, qty INTEGER, growth REAL percent",
          s.columns.size() == 6 && s.columns[1].type == CsvAnalyst::ColumnType::Real
              && s.columns[2].type == CsvAnalyst::ColumnType::Integer
              && s.columns[3].type == CsvAnalyst::ColumnType::Real && s.columns[3].percent);
}

// ── 1c: US / day-first / two-digit-year dates ──────────────────────────────
static void testDates(const QTemporaryDir &dir) {
    std::printf("== dates: M/D/YYYY, M/D/YY, D/M/YYYY, ambiguous ==\n");
    const QString p = writeFile(dir, "dates.csv",
        "event,us_date,short_date,dmy,ambiguous\n"
        "Launch,4/15/2026,4/15/26,15/04/2026,01/02/2026\n"
        "Kickoff,12/1/2025,12/1/25,01/12/2025,03/04/2026\n"
        "Review,1/20/2026,1/20/26,20/01/2026,05/06/2026\n");
    const QStringList r = run(p, {
        "SELECT group_concat(event, ',') FROM (SELECT event FROM csv ORDER BY us_date)",
        "SELECT us_date, short_date, dmy FROM csv WHERE event = 'Launch'",
        "SELECT COUNT(*) FROM csv WHERE us_date >= '2026-01-01'",
        "SELECT strftime('%Y-%m', us_date) FROM csv WHERE event = 'Kickoff'",
        "SELECT group_concat(event, ',') FROM (SELECT event FROM csv ORDER BY dmy)",
        "SELECT ambiguous FROM csv WHERE event = 'Launch'",
    });
    check("ORDER BY the M/D/YYYY column sorts chronologically",
          r.value(0) == "Kickoff,Review,Launch", r.value(0));
    check("dates stored as ISO-8601 (M/D/YYYY, M/D/YY, D/M/YYYY)",
          r.value(1) == "2026-04-15|2026-04-15|2026-04-15", r.value(1));
    check("date comparison against an ISO literal works", r.value(2) == "2", r.value(2));
    check("SQLite strftime works on the stored date", r.value(3) == "2025-12", r.value(3));
    check("ORDER BY the D/M/YYYY column sorts chronologically",
          r.value(4) == "Kickoff,Review,Launch", r.value(4));
    check("ambiguous DD/MM vs MM/DD column kept verbatim", r.value(5) == "01/02/2026", r.value(5));

    const CsvAnalyst::CsvSchema s = CsvAnalyst::detectSchema(p);
    check("schema: us_date DATE, ambiguous TEXT",
          s.columns.size() == 5 && s.columns[1].type == CsvAnalyst::ColumnType::Date
              && s.columns[4].type == CsvAnalyst::ColumnType::Text);
}

// ── 1d: header detection ───────────────────────────────────────────────────
static void testHeader(const QTemporaryDir &dir) {
    std::printf("== header detection ==\n");
    const QString pandas = writeFile(dir, "pandas.csv", ",name,amount\n0,Alice,10\n1,Bob,20\n");
    check("pandas index header (empty first cell) is a header",
          CsvAnalyst::detectSchema(pandas).hasHeader);
    check("pandas export: COUNT = 2, SUM(amount) = 30",
          one(pandas, "SELECT COUNT(*), SUM(amount) FROM csv") == "2|30",
          one(pandas, "SELECT COUNT(*), SUM(amount) FROM csv"));

    const QString years = writeFile(dir, "years.csv", "id,product,2024\n1,Widget,10\n2,Gadget,20\n");
    const CsvAnalyst::CsvSchema ys = CsvAnalyst::detectSchema(years);
    check("header with a numeric cell (2024) is a header", ys.hasHeader);
    check("numeric header column queryable as col_2024",
          one(years, "SELECT SUM(col_2024) FROM csv") == "30",
          one(years, "SELECT SUM(col_2024) FROM csv"));

    const QString noHeader = writeFile(dir, "nohdr.csv", "Alice,30\nBob,25\nCleo,41\n");
    check("headerless text+number file stays headerless",
          !CsvAnalyst::detectSchema(noHeader).hasHeader);
    const QString numbers = writeFile(dir, "nums.csv", "1,2,3\n4,5,6\n");
    check("headerless numeric file stays headerless", !CsvAnalyst::detectSchema(numbers).hasHeader);
}

// ── 2: preview names == queryable names ────────────────────────────────────
static void testPreviewNames(const QTemporaryDir &dir) {
    std::printf("== preview names are the table's column names ==\n");
    const QString p = writeFile(dir, "names.csv",
        "Order ID,Unit Price,Qty (units),unit price,Customer Name\n"
        "1,2.50,4,9,Ann\n2,3.00,1,8,Bo\n");
    const QString preview = CsvAnalyst::buildPreviewText(p);
    QStringList names;
    bool inSchema = false;
    for (const QString &line : preview.split(QLatin1Char('\n'))) {
        if (line.startsWith(QStringLiteral("Schema:"))) { inSchema = true; continue; }
        if (!inSchema) continue;
        if (!line.startsWith(QStringLiteral("  "))) break;
        names << line.trimmed().section(QStringLiteral(" ("), 0, 0);
    }
    check("preview lists 5 columns", names.size() == 5, names.join(QLatin1Char(',')));
    const QString sql = QStringLiteral("SELECT %1 FROM csv").arg(names.join(QStringLiteral(", ")));
    const QString got = one(p, sql);
    check("the SQL built from previewed names executes", !got.startsWith(QStringLiteral("ERROR")),
          sql + QStringLiteral(" -> ") + got);
    check("preview names Unit_Price (the real column)", names.contains(QStringLiteral("Unit_Price")),
          names.join(QLatin1Char(',')));
    check("preview keeps the original header alongside",
          preview.contains(QStringLiteral("header \"Unit Price\"")));
    const QString sum = one(p, QStringLiteral("SELECT SUM(%1) FROM csv").arg(names.value(1)));
    check("SUM over the previewed price column = 5.5", near(sum, 5.5), sum);
}

// ── performance: linear on a large file ────────────────────────────────────
static void testPerformance(const QTemporaryDir &dir) {
    std::printf("== performance ==\n");
    QByteArray body = "id,customer,note,price,qty,growth,day\n";
    body.reserve(4 * 1024 * 1024);
    for (int i = 0; i < 50000; ++i) {
        body += QByteArray::number(i) + ",\"Cust, " + QByteArray::number(i % 97)
              + "\",\"note " + QByteArray::number(i) + "\nsecond line\",\"$1,"
              + QByteArray::number(100 + i % 900) + ".25\",\"1,000\","
              + QByteArray::number(i % 50) + ".5%," + QByteArray::number(1 + i % 12) + "/"
              + QByteArray::number(13 + i % 15) + "/2025\n";
    }
    // One quoted field spanning 20k lines (~1 MB) must not go quadratic either.
    body += "50000,big,\"" + QByteArray("x,y \"\" z\n").repeated(20000) + "\",\"$1.00\",\"1,000\",1%,1/13/2025\n";
    const QString p = writeFile(dir, "big.csv", body);
    QElapsedTimer t;
    t.start();
    const QStringList r = run(p, {"SELECT COUNT(*), SUM(qty) FROM csv",
                                  "SELECT typeof(price), typeof(day) FROM csv WHERE id = 7"});
    const qint64 ms = t.elapsed();
    std::printf("    (loaded %d bytes in %lld ms)\n", int(body.size()), static_cast<long long>(ms));
    check("50,001 records loaded with typed columns", r.value(0) == "50001|50001000", r.value(0));
    check("price real, day ISO text", r.value(1) == "real|text", r.value(1));
    check("~5 MB / 50k-row load under 30 s (linear)", ms < 30000, QString::number(ms));
}

int main(int argc, char **argv) {
    QTemporaryDir home;
    qputenv("HOME", home.path().toUtf8());
    qputenv("XDG_CONFIG_HOME", home.path().toUtf8());
    qputenv("APPDATA", home.path().toUtf8());
    qputenv("USERPROFILE", home.path().toUtf8());
    QCoreApplication app(argc, argv);

    QTemporaryDir dir;
    if (!dir.isValid() || !QSqlDatabase::drivers().contains(QStringLiteral("QSQLITE"))) {
        std::printf("  [FAIL] temp dir / QSQLITE driver unavailable\n");
        return 1;
    }
    testRfc4180(dir);
    testNumbers(dir);
    testDates(dir);
    testHeader(dir);
    testPreviewNames(dir);
    testPerformance(dir);

    std::printf("\n=== Summary: %d passed, %d failed ===\n", g_pass, g_fail);
    return g_fail == 0 ? 0 : 1;
}
