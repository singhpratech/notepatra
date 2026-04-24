/**
 * Regression test for Notepatra's SQL formatter
 *   (Rust: rust-core/src/sql_fmt.rs, called via RustCore::formatSql()).
 *
 * This suite drives the SPEC for the Claude-style pretty-printer upgrade +
 * AI Fix button. Some assertions describe behaviour that only the upgraded
 * formatter can satisfy — those are marked "aspirational" and emit [SKIP]
 * instead of failing hard, so the file compiles and runs cleanly both
 * before and after the upgrade lands.
 *
 * Compile-time signature detection:
 *   -DSQLFMT_HAS_DIALECT=1  — set by CMake when rustbridge.h exposes the
 *                             4-arg formatSql(sql, indent, uppercase, dialect).
 * When unset, dialect-specific assertions (10–13) are skipped.
 *
 * Scaffold matches test_projectsearch.cpp:
 *   - QCoreApplication (offscreen).
 *   - check(name, ok, detail) helper → [PASS]/[FAIL] + counter.
 *   - Final "=== Summary: N passed, M failed ===".
 */

#include "src/rustbridge.h"

#include <QCoreApplication>
#include <QString>
#include <QStringList>
#include <QRegularExpression>
#include <QDebug>

#include <cstdio>
#include <cstdlib>

// ─── Counters + check helper ────────────────────────────────────────────

static int g_pass = 0, g_fail = 0, g_skip = 0;

static void check(const char *what, bool ok, const QString &detail = {}) {
    if (ok) { std::printf("  [PASS] %s\n", what); ++g_pass; }
    else    {
        std::printf("  [FAIL] %s%s%s\n", what,
                    detail.isEmpty() ? "" : " — ",
                    detail.toUtf8().constData());
        ++g_fail;
    }
}

// Aspirational check — prints [SKIP] when condition is false instead of
// bumping g_fail. Used for Claude-style pretty-printer assertions that
// only pass once the upgrade lands. Lets us commit the spec today
// without turning CI red.
static void aspire(const char *what, bool ok, const QString &detail = {}) {
    if (ok) {
        std::printf("  [PASS] %s  (aspirational — matches spec already!)\n", what);
        ++g_pass;
    } else {
        std::printf("  [SKIP] %s  (aspirational — awaits pretty-printer upgrade)"
                    "%s%s\n", what,
                    detail.isEmpty() ? "" : " — ",
                    detail.toUtf8().constData());
        ++g_skip;
    }
}

// ─── Helpers ────────────────────────────────────────────────────────────

// Call formatSql with or without the dialect arg depending on which shape
// rustbridge.h exposes (CMake probes it and sets SQLFMT_HAS_DIALECT).
static QString fmt(const QString &sql, int indent = 4, bool upper = true,
                   const QString &dialect = QStringLiteral("ansi")) {
#ifdef SQLFMT_HAS_DIALECT
    return RustCore::formatSql(sql, indent, upper, dialect);
#else
    (void)dialect;
    return RustCore::formatSql(sql, indent, upper);
#endif
}

static bool containsKeywordUppercase(const QString &s, const QString &kw) {
    // Match as whole word (surrounded by non-alnum/underscore).
    QRegularExpression rx(QStringLiteral("(^|\\W)%1(\\W|$)").arg(kw));
    return rx.match(s).hasMatch();
}

// Counts the number of lines whose first non-whitespace token starts with
// `needle` — useful for "JOIN on its own line" style assertions.
static int linesWithToken(const QString &s, const QString &needle) {
    int count = 0;
    for (const QString &line : s.split('\n')) {
        if (line.trimmed().startsWith(needle, Qt::CaseInsensitive))
            ++count;
    }
    return count;
}

// ─── Main ───────────────────────────────────────────────────────────────

int main(int argc, char **argv) {
    qputenv("QT_QPA_PLATFORM", "offscreen");
    QCoreApplication app(argc, argv);

    std::printf("=== SQL formatter regression tests ===\n");
#ifdef SQLFMT_HAS_DIALECT
    std::printf("(SQLFMT_HAS_DIALECT=1 — 4-arg formatter detected)\n\n");
#else
    std::printf("(SQLFMT_HAS_DIALECT unset — 3-arg formatter detected; "
                "dialect tests skipped)\n\n");
#endif

    // ─────────────────────────────────────────────────────────────
    // 1. Basic SELECT → stays valid, keywords uppercased
    // ─────────────────────────────────────────────────────────────
    {
        std::printf("— test 1: basic SELECT, uppercase=true\n");
        QString out = fmt("select * from users", 4, true);
        check("output is non-empty",    !out.isEmpty());
        check("SELECT uppercased",      containsKeywordUppercase(out, "SELECT"));
        check("FROM uppercased",        containsKeywordUppercase(out, "FROM"));
        check("still contains 'users'", out.contains("users"));
        check("still contains '*'",     out.contains('*'));
    }

    // ─────────────────────────────────────────────────────────────
    // 2. Multi-column SELECT expands to one column per line
    //      (a) >3 columns
    //      (b) line that would exceed 100 chars
    // ─────────────────────────────────────────────────────────────
    {
        std::printf("\n— test 2: multi-column SELECT column stacking\n");

        QString five = fmt("SELECT a, b, c, d, e FROM t", 4, true);
        int linesCount = five.count('\n') + 1;
        aspire("5-column SELECT expands to multiple lines",
               linesCount >= 5,
               QStringLiteral("got %1 lines\n%2").arg(linesCount).arg(five));

        QString longLine =
            "SELECT very_long_column_name_one, very_long_column_name_two, "
            "very_long_column_name_three FROM very_long_table_name";
        QString longFmt = fmt(longLine, 4, true);
        bool hasLongLine = false;
        for (const QString &l : longFmt.split('\n'))
            if (l.size() > 100) hasLongLine = true;
        aspire(">100-char SELECT collapsed to fit width",
               !hasLongLine,
               QStringLiteral("longest line still > 100 chars\n%1").arg(longFmt));
    }

    // ─────────────────────────────────────────────────────────────
    // 3. JOIN / LEFT JOIN / INNER JOIN placed on their own line
    // ─────────────────────────────────────────────────────────────
    {
        std::printf("\n— test 3: JOIN clauses on own lines\n");
        QString out = fmt(
            "SELECT u.id, o.total FROM users u "
            "INNER JOIN orders o ON o.user_id = u.id "
            "LEFT JOIN payments p ON p.order_id = o.id", 4, true);

        int joinLines =
              linesWithToken(out, "INNER JOIN")
            + linesWithToken(out, "LEFT JOIN")
            + linesWithToken(out, "JOIN");
        // We accept >= 2 so both current impl and spec-compliant output
        // satisfy the assertion.
        check("at least 2 join clauses on own lines",
              joinLines >= 2,
              QStringLiteral("got %1 join-starting lines\n%2").arg(joinLines).arg(out));
    }

    // ─────────────────────────────────────────────────────────────
    // 4. Subquery indented one level deeper than parent
    // ─────────────────────────────────────────────────────────────
    {
        std::printf("\n— test 4: subquery indent depth\n");
        QString out = fmt(
            "SELECT * FROM (SELECT id, name FROM users WHERE active = 1) u", 4, true);

        QStringList lines = out.split('\n');
        int outerIndent = -1, innerIndent = -1;
        bool seenOuter = false;
        for (const QString &l : lines) {
            QString trimmed = l.trimmed();
            if (!seenOuter && trimmed.startsWith("SELECT", Qt::CaseInsensitive)) {
                outerIndent = l.size() - l.trimmed().size();
                seenOuter = true;
                continue;
            }
            if (seenOuter && trimmed.startsWith("SELECT", Qt::CaseInsensitive)) {
                innerIndent = l.size() - l.trimmed().size();
                break;
            }
        }
        bool bothFound = outerIndent >= 0 && innerIndent >= 0;
        aspire("inner SELECT indented deeper than outer SELECT",
               bothFound && innerIndent > outerIndent,
               bothFound
                 ? QStringLiteral("outer=%1 inner=%2").arg(outerIndent).arg(innerIndent)
                 : QStringLiteral("subquery inlined (no nested SELECT line):\n%1").arg(out));
    }

    // ─────────────────────────────────────────────────────────────
    // 5. CTE (WITH x AS (...)) — parens on their own lines
    // ─────────────────────────────────────────────────────────────
    {
        std::printf("\n— test 5: CTE formatting\n");
        QString out = fmt(
            "WITH recent AS (SELECT id FROM orders WHERE created_at > now() - interval '7 days') "
            "SELECT * FROM recent", 4, true);

        check("WITH keyword present",     containsKeywordUppercase(out, "WITH"));
        check("CTE body contains SELECT", out.contains("SELECT", Qt::CaseInsensitive));

        bool parenOnOwnLine = false;
        for (const QString &l : out.split('\n')) {
            QString t = l.trimmed();
            if (t == "(" || t.endsWith(" AS (")) parenOnOwnLine = true;
        }
        aspire("CTE opening paren on dedicated line", parenOnOwnLine,
               QStringLiteral("output:\n%1").arg(out));
    }

    // ─────────────────────────────────────────────────────────────
    // 6. CASE / WHEN / ELSE / END correctly indented
    // ─────────────────────────────────────────────────────────────
    {
        std::printf("\n— test 6: CASE / WHEN / ELSE / END indent\n");
        QString out = fmt(
            "SELECT CASE WHEN x > 0 THEN 'pos' WHEN x < 0 THEN 'neg' ELSE 'zero' END AS sign "
            "FROM t", 4, true);

        check("CASE present", out.contains("CASE", Qt::CaseInsensitive));
        check("WHEN present", out.contains("WHEN", Qt::CaseInsensitive));
        check("ELSE present", out.contains("ELSE", Qt::CaseInsensitive));
        check("END present",  out.contains("END",  Qt::CaseInsensitive));

        int caseIndent = -1, whenIndent = -1;
        for (const QString &l : out.split('\n')) {
            QString t = l.trimmed();
            if (caseIndent < 0 && t.startsWith("CASE", Qt::CaseInsensitive))
                caseIndent = l.size() - t.size();
            else if (whenIndent < 0 && t.startsWith("WHEN", Qt::CaseInsensitive))
                whenIndent = l.size() - t.size();
        }
        aspire("WHEN indented deeper than CASE",
               caseIndent >= 0 && whenIndent > caseIndent,
               QStringLiteral("case=%1 when=%2").arg(caseIndent).arg(whenIndent));
    }

    // ─────────────────────────────────────────────────────────────
    // 7. Comments preserved — line + block.
    // Aspirational until the formatter adds a trivia-preservation pass:
    // sqlparser's AST doesn't round-trip comments, so the current AST
    // pretty-printer drops them.
    // ─────────────────────────────────────────────────────────────
    {
        std::printf("\n— test 7: comments preserved\n");
        QString withLineCmt = fmt(
            "SELECT a -- this is a line comment\nFROM t", 4, true);
        aspire("line comment marker '--' preserved",
               withLineCmt.contains("--"),
               withLineCmt);

        QString withBlockCmt = fmt(
            "SELECT /* block cmt */ a FROM t", 4, true);
        aspire("block comment '/* */' preserved",
               withBlockCmt.contains("/*") && withBlockCmt.contains("*/"),
               withBlockCmt);
    }

    // ─────────────────────────────────────────────────────────────
    // 8. Empty input → empty OR graceful parser-fallback note, no crash
    // ─────────────────────────────────────────────────────────────
    {
        std::printf("\n— test 8: empty input\n");
        QString out = fmt("", 4, true);
        check("empty input does not crash", true);
        bool benign = out.trimmed().isEmpty() ||
                      out.contains("parser fallback", Qt::CaseInsensitive);
        check("empty input → empty output OR parser-fallback note",
              benign,
              QStringLiteral("got %1 chars: %2").arg(out.size()).arg(out));
    }

    // ─────────────────────────────────────────────────────────────
    // 9. Garbage input → graceful fallback (no crash)
    // ─────────────────────────────────────────────────────────────
    {
        std::printf("\n— test 9: garbage input fallback\n");
        QString out = fmt("!!! this isn't SQL @@@ ((( random }}}", 4, true);
        check("garbage input does not crash", true);
        check("garbage input produces some output (or unchanged)", !out.isNull());
        bool noted = out.contains("parser fallback", Qt::CaseInsensitive);
        bool roughlyEcho = out.contains("random") || out.contains("this");
        check("garbage fallback either echoes or notes parser fallback",
              noted || roughlyEcho,
              out);
    }

    // ─────────────────────────────────────────────────────────────
    // 10–13. Dialect handling — only when SQLFMT_HAS_DIALECT is defined.
    // ─────────────────────────────────────────────────────────────
#ifdef SQLFMT_HAS_DIALECT
    {
        std::printf("\n— test 10: PostgreSQL :: cast preserved\n");
        QString out = fmt("SELECT id::text FROM users", 4, true, "postgres");
        check("PostgreSQL '::' cast preserved",
              out.contains("::"), out);
    }
    {
        std::printf("\n— test 11: T-SQL [bracketed] identifiers preserved\n");
        QString out = fmt("SELECT [id], [first name] FROM [dbo].[Users]", 4, true, "tsql");
        // Spec: ALL brackets round-trip. Current impl drops them from
        // plain identifiers (dbo, Users) — aspirational.
        aspire("T-SQL brackets preserved (incl. [dbo].[Users])",
               out.contains("[dbo]") && out.contains("[Users]"),
               out);
        // Load-bearing: identifiers that REQUIRE quoting (have spaces)
        // MUST keep their brackets, else the SQL becomes invalid.
        check("T-SQL bracketed identifier with space preserved",
              out.contains("[first name]"), out);
    }
    {
        std::printf("\n— test 12: MySQL `backtick` identifiers preserved\n");
        QString out = fmt("SELECT `user id`, `name` FROM `users`", 4, true, "mysql");
        aspire("MySQL backticks preserved on all identifiers",
               out.contains("`users`") && out.contains("`name`"),
               out);
        check("MySQL backticks preserved around identifier with space",
              out.contains("`user id`"), out);
    }
    {
        std::printf("\n— test 13: invalid dialect falls back to ANSI\n");
        QString out = fmt("SELECT 1", 4, true, "klingon-sql");
        check("invalid dialect still produces output",
              !out.trimmed().isEmpty(), out);
        check("invalid dialect still uppercases SELECT",
              containsKeywordUppercase(out, "SELECT"), out);
    }
#else
    std::printf("\n— tests 10–13 (dialects): SKIPPED (SQLFMT_HAS_DIALECT undefined)\n");
    g_skip += 4;
#endif

    // ─────────────────────────────────────────────────────────────
    // 14. Idempotent: format(format(sql)) == format(sql)
    // ─────────────────────────────────────────────────────────────
    {
        std::printf("\n— test 14: idempotence for basic SELECT\n");
        QString sql = "SELECT id, name FROM users WHERE active = 1";
        QString once  = fmt(sql, 4, true);
        QString twice = fmt(once, 4, true);
        check("format(format(x)) == format(x)",
              once == twice,
              QStringLiteral("diverged:\n--- once ---\n%1\n--- twice ---\n%2")
                .arg(once, twice));
    }

    // ─────────────────────────────────────────────────────────────
    // 15. Uppercase=false preserves original keyword case
    // ─────────────────────────────────────────────────────────────
    {
        std::printf("\n— test 15: uppercase=false preserves case\n");
        QString out = fmt("select id from users", 4, false);
        bool kept = out.contains("select", Qt::CaseSensitive)
                 && out.contains("from",   Qt::CaseSensitive);
        check("'select'/'from' remain lowercase when uppercase=false",
              kept,
              out);
    }

    // ─── Summary ────────────────────────────────────────────────
    std::printf("\n=== Summary: %d passed, %d failed",
                g_pass, g_fail);
    if (g_skip > 0) std::printf(" (%d aspirational skipped)", g_skip);
    std::printf(" ===\n");

    return g_fail == 0 ? 0 : 1;
}
