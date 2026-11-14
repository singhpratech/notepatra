// SPDX-License-Identifier: GPL-3.0-or-later
//
// .npd validation: unclosed brackets and quotes.
//
// Reported from v0.1.134: validate_npd answered valid:true for
// "diagram flow\n(a) -> [b" — the parser took "[b" as a node id. The fix only
// ADDS "line N, column M: unclosed ..." errors; it never changes the parsed
// model, so every diagram renders exactly as before. This table pins both
// halves: each invalid case reports the right line, column and message, and
// each valid case (brackets inside quoted labels, escapes, comments, edge
// labels, nested shapes, CRLF, trailing whitespace, Unicode, the three
// built-in templates, the shipped sample, the docs example and Mermaid-import
// output) still parses clean. Pure Qt Core.

#include "src/diagram/mermaid_import.h"
#include "src/diagram/npd_parser.h"
#include "src/diagram/npd_templates.h"

#include <QCoreApplication>
#include <QElapsedTimer>
#include <QFile>
#include <QString>

#include <cstdio>

static int g_pass = 0, g_fail = 0;
static void check(const QString &what, bool ok, const QString &detail = {}) {
    if (ok) {
        std::printf("  [PASS] %s\n", what.toUtf8().constData());
        ++g_pass;
    } else {
        std::printf("  [FAIL] %s%s%s\n", what.toUtf8().constData(),
                    detail.isEmpty() ? "" : " — ", detail.toUtf8().constData());
        ++g_fail;
    }
}

using namespace Npd;

struct ValidCase {
    const char *name;
    QString src;
};

struct InvalidCase {
    const char *name;
    QString src;
    int line;
    int column;
    const char *message;   // prefix of the message after "line N, column M: "
};

static QString showcaseSample() {
    QFile f(QStringLiteral(NOTEPATRA_SOURCE_DIR "/samples/diagram_showcase.npd"));
    if (!f.open(QIODevice::ReadOnly)) return QString();
    return QString::fromUtf8(f.readAll());
}

static const char *kDocsExample =
    "diagram system\n"
    "title \"Checkout path\"\n"
    "direction LR\n"
    "palette auto\n"
    "\n"
    "icon user  :user  \"Browser\"\n"
    "icon cdn   :cloud \"CDN\"\n"
    "node api   [API gateway] #cc785c\n"
    "node pay   [Payments]\n"
    "node queue ([Events])\n"
    "\n"
    "group \"Edge tier\" : user cdn\n"
    "note api \"Rate-limited to 100 rps\"\n"
    "\n"
    "user -> cdn : assets\n"
    "cdn  -> api : /checkout\n"
    "api  -> pay : charge\n"
    "api  -.-> queue : audit event\n"
    "\n"
    "legend dashed \"async\"\n"
    "legend #cc785c \"hot path\"\n"
    "textbox \"Dashed hops are fire-and-forget.\"\n";

int main(int argc, char **argv) {
    QCoreApplication app(argc, argv);
    std::printf("=== .npd validation: unclosed brackets and quotes ===\n\n");

    const QString sample = showcaseSample();
    check("samples/diagram_showcase.npd is readable", !sample.isEmpty());

    const QString mermaidFlow = mermaidToNpd(QStringLiteral(
        "flowchart TD\n"
        "  Start([Begin]) --> Auth{Logged in?}\n"
        "  Auth -->|yes| Dash[Dashboard]\n"
        "  Auth -->|no| Login[Login page]\n"
        "  Login --> Auth\n"));
    const QString mermaidShapes = mermaidToNpd(QStringLiteral(
        "graph LR\n"
        "  subgraph Data [Data tier]\n"
        "    D[(Orders DB)]\n"
        "  end\n"
        "  A[[Sub routine]] --> H{{Hexagon}}\n"
        "  H -.-> C((Circle))\n"
        "  C <--> P[/Para/]\n"
        "  P -- \"say (hi\" --> D\n"
        "  X>Flag] ==> D\n"
        "  Q[\"f(x) [draft\"] --> D\n"));
    check("mermaid flow import produced source", !mermaidFlow.isEmpty());
    check("mermaid shapes import produced source", !mermaidShapes.isEmpty());

    // ── valid: each must parse with NO errors ──
    const QVector<ValidCase> valid = {
        {"box", "node a [Box]"},
        {"pill", "node a (Pill)"},
        {"decision", "node a {Decision}"},
        {"database", "node a ([DB])"},
        {"parens inside a box label", "node a [f(x) = y]"},
        {"unbalanced '(' inside a closed box label", "node a [Tuple (a, b]"},
        {"brackets inside a quoted hover", "node a [Label] #1565c0 :: \"hover (detail [x\""},
        {"colour name + quoted hover", "node a (Start) green :: \"Signed-in [web]\""},
        {"brackets inside a quoted title", "title \"A [draft (v2\""},
        {"brackets inside a quoted textbox", "textbox \"Use ( and [ and { freely\""},
        {"escaped quotes inside a title", "title \"say \\\"hi\\\"\""},
        {"backslashes inside a textbox", "textbox \"C:\\\\path\\\\\""},
        {"comment lines with open brackets",
         "# comment with [ unclosed ( and \" quote\nnode a [A]\n  # indented {"},
        {"edge label with open brackets", "a -> b : label with ( and ["},
        {"quoted edge label with a paren", "a -> b : \"quoted (label\""},
        {"chain with label", "a -> b -> c : chain"},
        {"balanced brackets in endpoint ids", "list[0] -> fn() -> map{k}"},
        {"dashed and bidirectional edges", "a -.-> b\na <.-> c\na <-> d"},
        {"CRLF line endings", "node a [A]\r\nnode b (B)\r\na -> b : x\r\n"},
        {"trailing whitespace and indentation", "node a [A]   \t\n  a -> b  \t\n\t\n"},
        {"Unicode ids and labels", QString::fromUtf8(
             "node \xC3\xBC [\xC3\x9Cn\xC3\xAF" "c\xC3\xB6" "d\xC3\xA9 \xF0\x9F\x98\x80 label]\n"
             "\xC3\xBC -> \xE7\xB5\x82\xE7\x82\xB9")},
        {"icon with brackets in quoted label and hover",
         "icon db :database \"Users (primary)\" #6a1b9a :: \"Postgres [main]\""},
        {"group label with brackets", "group \"Edge [tier]\" : a b\nnode a [A]\nnode b [B]"},
        {"note text with parens", "note a \"Retry (3x)\"\nnode a [A]"},
        {"legend text with parens", "legend dashed \"async (best effort)\""},
        {"arrow inside a node label", "node x [Maps A -> B]"},
        {"closing bracket inside a box label", "node a [a ] b]"},
        {"pill whose label starts with a tag", "node a ([a] b)"},
        {"bare node", "node a"},
        {"empty box", "node a []"},
        {"colons in ids", "http://x -> ns:b"},
        {"empty quoted title", "title \"\""},
        {"text after a closed box", "node a [A] # trailing (comment)"},
        {"balanced junk after an edge", "a -> b # see (notes)"},
        {"nested shapes in a label", "node a [outer (inner {deep})]"},
        {"flow template", QString::fromUtf8(NpdTemplates::kFlowTemplate)},
        {"ER template", QString::fromUtf8(NpdTemplates::kErTemplate)},
        {"system template", QString::fromUtf8(NpdTemplates::kSystemTemplate)},
        {"samples/diagram_showcase.npd", sample},
        {"docs complete example", QString::fromUtf8(kDocsExample)},
        {"Mermaid import: realistic flow", mermaidFlow},
        {"Mermaid import: every shape, subgraph, labels", mermaidShapes},
    };
    std::printf("— valid (%d) ─────────────────────────────────────\n", int(valid.size()));
    for (const ValidCase &c : valid) {
        const Diagram d = parse(c.src);
        check(QStringLiteral("valid: %1").arg(QLatin1String(c.name)), d.ok() && !c.src.isEmpty(),
              d.errors.join(QStringLiteral(" | ")));
    }

    // ── invalid: the FIRST error must name the line, column and problem ──
    const QVector<InvalidCase> invalid = {
        {"reported: (a) -> [b", "diagram flow\n(a) -> [b", 2, 8, "unclosed '[': add the matching ']'"},
        {"box shape", "node b [Label", 1, 8, "unclosed '[': add the matching ']'"},
        {"pill shape", "node b (Label", 1, 8, "unclosed '(': add the matching ')'"},
        {"decision shape", "node b {Label", 1, 8, "unclosed '{': add the matching '}'"},
        {"database, no closers", "node b ([Label", 1, 8, "unclosed '([': a database shape ends with '])'"},
        {"database closed as a pill", "node b ([Label)", 1, 8, "unclosed '(['"},
        {"database closed as a box", "node b ([Label]", 1, 8, "unclosed '(['"},
        {"box + hex colour", "node b [Label #fff", 1, 8, "unclosed '['"},
        {"box + colour name", "node b [Label green", 1, 8, "unclosed '['"},
        {"box + hover", "node b [Label :: \"hover\"", 1, 8, "unclosed '['"},
        {"'::' inside a box label", "node b [Foo::Bar]", 1, 8,
         "unclosed '[': add the matching ']' (the '::' in it starts the hover text)"},
        {"hover quote", "node b [L] :: \"unterminated", 1, 15, "unterminated quote"},
        {"quoted node spec", "node b \"Label", 1, 8, "unterminated quote"},
        {"title quote", "title \"Unclosed", 1, 7, "unterminated quote: add the closing '\"'"},
        {"textbox quote", "textbox \"Unclosed caption", 1, 9, "unterminated quote"},
        {"note quote", "node a [A]\nnote a \"text", 2, 8, "unterminated quote"},
        {"legend quote", "legend dashed \"async", 1, 15, "unterminated quote"},
        {"icon label quote", "icon db :database \"Users", 1, 19, "unterminated quote"},
        {"icon hover quote", "icon db :database \"Users\" :: \"hover", 1, 30, "unterminated quote"},
        {"edge label quote", "a -> b : \"label", 1, 10, "unterminated quote"},
        {"edge target brace", "a -> {b", 1, 6, "unclosed '{'"},
        {"edge source paren", "(a -> b", 1, 1, "unclosed '('"},
        {"last hop of a chain", "a -> b -> [c", 1, 11, "unclosed '['"},
        {"quoted endpoint", "\"a b -> c", 1, 1, "unterminated quote"},
        {"indented, trailing whitespace", "  node b [Label  \t", 1, 10, "unclosed '['"},
        {"CRLF", "node a [A]\r\nnode b [B\r\n", 2, 8, "unclosed '['"},
        {"Unicode before the bracket (columns count characters)",
         QString::fromUtf8("node \xF0\x9F\x98\x80 [\xC3\x9Cn\xC3\xAF"), 1, 8, "unclosed '['"},
        {"Mermaid-style id on an edge", "a[Label -> b", 1, 2, "unclosed '['"},
        {"Mermaid-style id on a node line", "node a[Label", 1, 7, "unclosed '['"},
        {"after comments and blank lines", "# ok\n\nnode a [A]\n\n  b -> [c", 5, 8, "unclosed '['"},
        {"dashed edge", "a -.-> (b", 1, 8, "unclosed '('"},
        {"bidirectional edge with label", "[a <-> b : sync", 1, 1, "unclosed '['"},
    };
    std::printf("\n— invalid (%d) ───────────────────────────────────\n", int(invalid.size()));
    for (const InvalidCase &c : invalid) {
        const Diagram d = parse(c.src);
        const QString prefix = QStringLiteral("line %1, column %2: ").arg(c.line).arg(c.column);
        const QString first = d.errors.isEmpty() ? QString() : d.errors.first();
        check(QStringLiteral("invalid: %1").arg(QLatin1String(c.name)),
              !d.ok() && first.startsWith(prefix + QLatin1String(c.message)),
              QStringLiteral("want \"%1%2…\", got [%3]")
                  .arg(prefix, QLatin1String(c.message), d.errors.join(QStringLiteral(" | "))));
    }

    // ── the check only ADDS errors: what renders is unchanged ──
    std::printf("\n— model unchanged ───────────────────────────────\n");
    {
        const Diagram d = parse(QStringLiteral("diagram flow\n(a) -> [b"));
        check("(a) -> [b still draws two nodes and one edge",
              d.nodes.size() == 2 && d.edges.size() == 1 &&
                  d.nodes.at(0).id == QLatin1String("(a)") &&
                  d.nodes.at(1).id == QLatin1String("[b"));
        const Diagram n = parse(QStringLiteral("node b [Label"));
        check("node b [Label still draws a box labelled \"[Label\"",
              n.nodes.size() == 1 && n.nodes.at(0).shape == Shape::Box &&
                  n.nodes.at(0).label == QLatin1String("[Label"));
        const Diagram t = parse(QStringLiteral("title \"Unclosed"));
        check("an unterminated title keeps its text", t.title == QLatin1String("\"Unclosed"));
        const Diagram two = parse(QStringLiteral("(a -> [b"));
        check("each open endpoint on a line is reported",
              two.errors.size() == 2 &&
                  two.errors.at(0).startsWith(QLatin1String("line 1, column 1: unclosed '('")) &&
                  two.errors.at(1).startsWith(QLatin1String("line 1, column 7: unclosed '['")),
              two.errors.join(QStringLiteral(" | ")));
        const Diagram g = parse(QStringLiteral("group \"Edge : a\nnode a [A]"));
        check("the existing group-quote error keeps its line-only form",
              g.errors.size() == 1 &&
                  g.errors.first() == QLatin1String("line 1: group label is missing a closing quote"),
              g.errors.join(QStringLiteral(" | ")));
    }

    // ── columns stay right when one line has several errors ──
    // The column walk resumes from the previous error on the line instead of
    // restarting at column 1; it must agree with a from-scratch count,
    // including across surrogate pairs and when a later check points back.
    std::printf("\n— columns, several errors per line ──────────────\n");
    {
        const Diagram d = parse(QString::fromUtf8("(\xF0\x9F\x98\x80 -> [\xF0\x9F\x98\x80 -> {b"));
        check("three open endpoints after emoji: columns 1, 7, 13",
              d.errors.size() == 3 &&
                  d.errors.at(0).startsWith(QLatin1String("line 1, column 1: unclosed '('")) &&
                  d.errors.at(1).startsWith(QLatin1String("line 1, column 7: unclosed '['")) &&
                  d.errors.at(2).startsWith(QLatin1String("line 1, column 13: unclosed '{'")),
              d.errors.join(QStringLiteral(" | ")));
        const Diagram back = parse(QStringLiteral("icon db :database \"Users :: \"hover"));
        check("icon: hover error, then the label error to its left",
              back.errors.size() == 2 &&
                  back.errors.at(0).startsWith(QLatin1String("line 1, column 29: unterminated")) &&
                  back.errors.at(1).startsWith(QLatin1String("line 1, column 19: unterminated")),
              back.errors.join(QStringLiteral(" | ")));
    }

    // ── error cap ──
    // Reported on the v0.1.135 branch: every open endpoint added a message,
    // so one pasted line could build tens of thousands of them.
    std::printf("\n— error cap ─────────────────────────────────────\n");
    {
        auto lines = [](int n) {
            QString s;
            for (int i = 0; i < n; ++i) s += QStringLiteral("bogus%1\n").arg(i);
            return s;
        };
        const Diagram exact = parse(lines(kMaxErrors));
        check("exactly kMaxErrors errors: all listed, no summary entry",
              exact.errors.size() == kMaxErrors &&
                  exact.errors.last().startsWith(QLatin1String("line 100: unrecognized")),
              exact.errors.isEmpty() ? QString() : exact.errors.last());
        const Diagram one = parse(lines(kMaxErrors + 1));
        check("one over: kMaxErrors listed + \"…and 1 more error\"",
              one.errors.size() == kMaxErrors + 1 &&
                  one.errors.last() == QStringLiteral("…and 1 more error (only the first 100 are listed)"),
              one.errors.isEmpty() ? QString() : one.errors.last());
        const Diagram many = parse(lines(500));
        check("500 bad lines: 100 listed + \"…and 400 more errors\", still invalid",
              !many.ok() && many.errors.size() == kMaxErrors + 1 &&
                  many.errors.at(kMaxErrors - 1).startsWith(QLatin1String("line 100: ")) &&
                  many.errors.last() == QStringLiteral("…and 400 more errors (only the first 100 are listed)"),
              many.errors.isEmpty() ? QString() : many.errors.last());
        const Diagram ref = parse(lines(500) + QStringLiteral("group \"G\" : ghost\n"));
        check("cross-reference errors after the cap are counted too",
              ref.errors.size() == kMaxErrors + 1 &&
                  ref.errors.last().startsWith(QStringLiteral("…and 401 more errors")),
              ref.errors.isEmpty() ? QString() : ref.errors.last());
    }

    // ── perf: one long line full of open endpoints ──
    // Each error used to re-count columns from the start of the line: a
    // 209 KB line of "[n0 -> [n1 -> ..." took 2.5 s and 1 MB took 71 s (the
    // parser before the column check: 32 ms / ~200 ms), on the GUI thread per
    // keystroke and from MCP validate_npd. The bound is loose for slow CI.
    std::printf("\n— perf: 1 MB adversarial line ───────────────────\n");
    {
        const int kBytes = 1 << 20;
        const struct { const char *name; const char *open; } shapes[] = {
            {"\"[n -> [n -> ...\"", "["}, {"'\"n -> \"n -> ...'", "\""}};
        for (const auto &sh : shapes) {
            QString src;
            src.reserve(kBytes + 64);
            src += QStringLiteral("diagram flow\n");
            for (int i = 0; src.size() < kBytes; ++i)
                src += QLatin1String(sh.open) + QStringLiteral("n%1 -> ").arg(i);
            src += QStringLiteral("end\n");
            QElapsedTimer t;
            t.start();
            const Diagram d = parse(src);
            const qint64 ms = t.elapsed();
            std::printf("  [INFO] %s: %lld ms, %d errors kept\n", sh.name, (long long)ms,
                        int(d.errors.size()));
            check(qPrintable(QStringLiteral("1 MB %1 parses in under 3000 ms")
                                 .arg(QLatin1String(sh.name))),
                  ms < 3000, QStringLiteral("%1 ms").arg(ms));
            check(qPrintable(QStringLiteral("1 MB %1 is capped and still invalid")
                                 .arg(QLatin1String(sh.name))),
                  !d.ok() && d.errors.size() == kMaxErrors + 1 &&
                      d.errors.first().startsWith(QLatin1String("line 2, column 1: ")) &&
                      d.errors.last().startsWith(QStringLiteral("…and ")),
                  d.errors.isEmpty() ? QString() : d.errors.first());
        }
    }

    std::printf("\n=== Summary: %d passed, %d failed ===\n", g_pass, g_fail);
    return g_fail == 0 ? 0 : 1;
}
