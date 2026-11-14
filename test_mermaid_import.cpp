// SPDX-License-Identifier: GPL-3.0-or-later
//
// Round-trip tests for the Mermaid → .npd importer: translate Mermaid, then
// parse the result with the real Npd::parse and assert the node/edge model is
// what we expect. Pure (Qt Core only) — no GUI, no WebEngine.

#include "src/diagram/mermaid_import.h"
#include "src/diagram/npd_parser.h"

#include <QCoreApplication>
#include <QVector>
#include <cstdio>

using namespace Npd;

static int g_pass = 0, g_fail = 0;
static void check(const char *what, bool ok, const QString &detail = {}) {
    if (ok) { std::printf("  [PASS] %s\n", what); ++g_pass; }
    else    { std::printf("  [FAIL] %s%s%s\n", what, detail.isEmpty() ? "" : " — ",
                          detail.toUtf8().constData()); ++g_fail; }
}

static Node nodeById(const Diagram &d, const QString &id) {
    for (const Node &n : d.nodes) if (n.id == id) return n;
    return Node{};
}

// Every import in this file goes through imp(), which keeps each non-empty
// result so the end of main() can hold the header's promise against all of
// them: an import always validates with zero errors.
struct Imported { QString mermaid, npd; };
static QVector<Imported> g_imports;
static QString imp(const QString &mermaid, QString *error = nullptr) {
    const QString npd = mermaidToNpd(mermaid, error);
    if (!npd.isEmpty()) g_imports.append({mermaid, npd});
    return npd;
}

int main(int argc, char **argv) {
    QCoreApplication app(argc, argv);
    std::printf("=== Mermaid → .npd importer tests ===\n\n");

    check("empty input → empty output", imp("").isEmpty());
    check("whitespace input → empty output", imp("   \n\n").isEmpty());

    // ── shapes + inline node defs in edges ──
    {
        Diagram d = parse(imp(
            "flowchart TD\n"
            "A[Start] --> B[Process]\n"
            "B --> C{OK?}\n"
            "C --> D[(Users DB)]\n"));
        check("flowchart parses clean", d.ok(), d.errors.join("; "));
        check("4 nodes", d.nodes.size() == 4, QString::number(d.nodes.size()));
        check("3 edges", d.edges.size() == 3, QString::number(d.edges.size()));
        check("[] → box", nodeById(d, "A").shape == Shape::Box);
        check("box label", nodeById(d, "A").label == "Start");
        check("{} → decision", nodeById(d, "C").shape == Shape::Decision);
        check("[(...)] → database", nodeById(d, "D").shape == Shape::Database);
        check("database label", nodeById(d, "D").label == "Users DB");
    }

    // ── round shapes → pill ──
    {
        Diagram d = parse(imp("graph LR\nx(Round) --> y([Stadium])\nz((Circle)) --> y"));
        check("() → pill", nodeById(d, "x").shape == Shape::Pill);
        check("([]) → pill", nodeById(d, "y").shape == Shape::Pill);
        check("(()) → pill", nodeById(d, "z").shape == Shape::Pill);
    }

    // ── edge labels: pipe + dash forms ──
    {
        Diagram d = parse(imp("graph TD\nA -->|yes| B\nA -- no --> C"));
        check("pipe-label edge parses", d.ok(), d.errors.join("; "));
        bool yes = false, no = false;
        for (const Edge &e : d.edges) {
            if (e.from == "A" && e.to == "B" && e.label == "yes") yes = true;
            if (e.from == "A" && e.to == "C" && e.label == "no") no = true;
        }
        check("pipe label |yes| captured", yes);
        check("dash label '-- no -->' captured", no);
    }

    // ── bidirectional ──
    {
        Diagram d = parse(imp("graph LR\nA <--> B"));
        check("bidirectional edge", d.edges.size() == 1 && d.edges.at(0).bidirectional);
    }

    // ── arrow variants all become a directed edge ──
    {
        Diagram d = parse(imp("graph TD\nA --- B\nB ==> C\nC -.-> D"));
        check("--- / ==> / -.-> all become edges", d.edges.size() == 3,
              QString::number(d.edges.size()));
    }

    // ── unsupported constructs are dropped, output still parses ──
    {
        Diagram d = parse(imp(
            "flowchart TD\n"
            "%% a comment\n"
            "subgraph cluster\n"
            "A[In cluster] --> B\n"
            "end\n"
            "classDef big fill:#f00\n"
            "class A big\n"
            "click A \"http://x\"\n"));
        check("subgraph/classDef/click dropped → still clean", d.ok(), d.errors.join("; "));
        check("edge inside subgraph survived", d.edges.size() == 1);
        check("node A declared as box", nodeById(d, "A").shape == Shape::Box);
    }

    // ── v0.1.120: dotted arrows become dashed .npd edges ──
    {
        Diagram d = parse(imp("graph TD\nA -.-> B\nB --> C\nC -.->|later| D"));
        check("dotted import parses clean", d.ok(), d.errors.join("; "));
        check("3 edges from the dotted mix", d.edges.size() == 3, QString::number(d.edges.size()));
        bool ab = false, bc = false, cd = false;
        for (const Edge &e : d.edges) {
            if (e.from == "A" && e.to == "B") ab = e.dashed;
            if (e.from == "B" && e.to == "C") bc = !e.dashed;
            if (e.from == "C" && e.to == "D") cd = e.dashed && e.label == "later";
        }
        check("-.-> → dashed edge", ab);
        check("--> stays solid", bc);
        check("-.->|label| → dashed + labelled", cd);
    }

    // ── v0.1.120: subgraph → group ──
    {
        const QString npd = imp(
            "flowchart TD\n"
            "subgraph Edge tier\n"
            "A[CDN] --> B[Gateway]\n"
            "end\n"
            "subgraph Data\n"
            "C[(DB)]\n"
            "end\n"
            "B --> C\n");
        Diagram d = parse(npd);
        check("subgraph import parses clean", d.ok(), d.errors.join("; "));
        check("two groups", d.groups.size() == 2, QString::number(d.groups.size()));
        check("first group label", d.groups.value(0).label == "Edge tier",
              d.groups.isEmpty() ? "(none)" : d.groups.at(0).label);
        check("first group members", d.groups.value(0).members == QStringList({"A", "B"}));
        check("second group members", d.groups.value(1).members == QStringList({"C"}));
        check("edges across subgraphs survive", d.edges.size() == 2, QString::number(d.edges.size()));

        // `subgraph id [Title]` — the bracketed title wins over the id
        Diagram t = parse(imp("flowchart TD\nsubgraph s1 [Nice Name]\nA --> B\nend\n"));
        check("subgraph id [Title] uses the title", t.groups.value(0).label == "Nice Name",
              t.groups.isEmpty() ? "(none)" : t.groups.at(0).label);

        // a quote inside a subgraph title must not break the quoted group label
        Diagram q = parse(imp("flowchart TD\nsubgraph My \"Tier\"\nA --> B\nend\n"));
        check("quoted subgraph title still parses", q.ok(), q.errors.join("; "));
        check("quoted subgraph title kept its words",
              q.groups.value(0).label.contains("Tier"),
              q.groups.isEmpty() ? "(none)" : q.groups.at(0).label);

        // a node is claimed by ONE group only, so the .npd stays valid
        Diagram once = parse(imp(
            "flowchart TD\nsubgraph One\nA --> B\nend\nsubgraph Two\nA --> C\nend\n"));
        check("shared node stays in a single group", once.ok(), once.errors.join("; "));
    }

    // ── v0.1.120: graph LR carries the direction over ──
    {
        Diagram lr = parse(imp("graph LR\nA --> B\n"));
        check("graph LR → direction LR", lr.direction == "LR", lr.direction);
        check("graph TD → direction TB", parse(imp("graph TD\nA --> B\n")).direction == "TB");
        check("flowchart RL → direction LR", parse(imp("flowchart RL\nA --> B\n")).direction == "LR");
        check("no direction token → default TB", parse(imp("graph\nA --> B\n")).direction == "TB");
    }

    // ── a realistic flow ──
    {
        const QString npd = imp(
            "flowchart TD\n"
            "  Start([Begin]) --> Auth{Logged in?}\n"
            "  Auth -->|yes| Dash[Dashboard]\n"
            "  Auth -->|no| Login[Login page]\n"
            "  Login --> Auth\n");
        Diagram d = parse(npd);
        check("realistic flow clean", d.ok(), d.errors.join("; "));
        check("realistic flow 4 nodes", d.nodes.size() == 4, QString::number(d.nodes.size()));
        check("realistic flow 4 edges", d.edges.size() == 4, QString::number(d.edges.size()));
        check("Auth is a decision", nodeById(d, "Auth").shape == Shape::Decision);
    }

    // ── unsupported types and empty results are errors, never an empty diagram ──
    // Reported from v0.1.134: a sequenceDiagram came back as success with
    // "# (Mermaid import found no nodes or edges)\ndiagram flow\n".
    {
        const QString supported = mermaidSupportedTypes();
        check("supported-types sentence names flowchart and graph",
              supported.startsWith("Supported: ") && supported.contains("\"flowchart\"") &&
                  supported.contains("\"graph\""), supported);

        QString err = "stale";
        QString out = imp("sequenceDiagram\n  Alice->>Bob: Hi\n  Bob-->>Alice: Hello\n", &err);
        check("sequenceDiagram → no source", out.isEmpty(), out);
        check("sequenceDiagram → error names the type and what is supported",
              err == "Mermaid \"sequenceDiagram\" diagrams cannot be imported. " + supported, err);

        // Every other Mermaid type, after front matter / directives / comments.
        const char *types[] = {"classDiagram", "stateDiagram-v2", "erDiagram", "gantt",
                               "pie title Pets", "journey", "gitGraph:", "mindmap",
                               "timeline", "C4Context", "quadrantChart", "sankey-beta",
                               "xychart-beta", "requirementDiagram", "block-beta",
                               "futureThingDiagram"};
        for (const char *t : types) {
            err.clear();
            const QString src = QStringLiteral("---\ntitle: x\n---\n%%{init: {}}%%\n%% c\n%1\n  A --> B\n")
                                    .arg(QLatin1String(t));
            out = imp(src, &err);
            const QString kw = QString::fromLatin1(t).section(' ', 0, 0).remove(':');
            check(qPrintable(QStringLiteral("%1 → error naming it").arg(kw)),
                  out.isEmpty() && err.startsWith("Mermaid \"" + kw + "\" diagrams cannot be imported."),
                  err);
        }

        err.clear();
        out = imp("flowchart TD\n%% nothing here\nclassDef x fill:#f00\n", &err);
        check("flowchart with no nodes → error, no source",
              out.isEmpty() && err == "the Mermaid flowchart has no nodes or edges to import", err);
        err.clear();
        out = imp("hello world\n", &err);
        check("text that is not Mermaid → error listing what is supported",
              out.isEmpty() && err == "found no Mermaid flowchart nodes or edges to import. " + supported,
              err);
        err.clear();
        out = imp("   \n", &err);
        check("blank input → error", out.isEmpty() && !err.isEmpty(), err);

        // Supported input still imports, and clears a stale error.
        err = "stale";
        out = imp("flowchart LR\nA --> B\n", &err);
        check("flowchart imports with no error", !out.isEmpty() && err.isEmpty(), err);
        check("headerless edges still import", !imp("A --> B\n", &err).isEmpty() && err.isEmpty());
        check("a node id that is also a type word is not a header",
              !imp("graph TD\npie --> gantt\n", &err).isEmpty() && err.isEmpty(), err);
    }

    // ── labels and ids the .npd grammar cannot carry verbatim ──
    // Reported on the v0.1.135 branch: `A[std::vector] --> B` imported as a
    // node line whose '::' started the hover text, so the result reported an
    // unclosed '['. Before that check existed it silently drew "[std".
    {
        const QChar prop(0x2237);
        Diagram d = parse(imp("flowchart LR\nA[std::vector] --> B\n"));
        check("A[std::vector] imports clean", d.ok(), d.errors.join("; "));
        check("...as a box labelled std∷vector, no hover",
              nodeById(d, "A").shape == Shape::Box &&
                  nodeById(d, "A").label == QStringLiteral("std") + prop + QStringLiteral("vector") &&
                  nodeById(d, "A").hover.isEmpty(),
              nodeById(d, "A").label + " / " + nodeById(d, "A").hover);
        d = parse(imp("graph TD\nA[\"IPv6 ::1\"]\nA --> B\n"));
        check("A[\"IPv6 ::1\"] imports clean with its label", d.ok() &&
                  nodeById(d, "A").label == QStringLiteral("IPv6 ") + prop + QStringLiteral("1"),
              d.errors.join("; ") + " / " + nodeById(d, "A").label);
        d = parse(imp("graph TD\nA([a::b:::c]) --> D[(x::y)]\nE{{p::q}}\n"));
        check("'::' in pill, database, decision labels (and ':::') imports clean",
              d.ok() && nodeById(d, "A").label == QStringLiteral("a") + prop + QStringLiteral("b") +
                                                     prop + QStringLiteral(":c") &&
                  nodeById(d, "D").shape == Shape::Database,
              d.errors.join("; ") + " / " + nodeById(d, "A").label);
        d = parse(imp("graph TD\nA -->|std::move| B\n"));
        check("'::' in an edge label is kept as is", d.ok() && d.edges.value(0).label == "std::move",
              d.errors.join("; "));
        d = parse(imp("graph TD\nA -->|\"half quoted| B\nB -->|:colon| C\n"));
        check("edge labels starting with '\"' or ':' import clean and keep their text",
              d.ok() && d.edges.size() == 2 && d.edges.at(0).label == "\"half quoted" &&
                  d.edges.at(1).label == ":colon",
              d.errors.join("; "));
        d = parse(imp("graph LR\ntitle --> diagram\nnote -.-> legend\ngroup <--> icon\n"));
        check("ids that are .npd keywords still become edges",
              d.ok() && d.edges.size() == 3 && d.edges.at(0).from == "title" &&
                  d.edges.at(0).to == "diagram" && d.edges.at(1).dashed &&
                  d.edges.at(2).bidirectional && d.title.isEmpty(),
              d.errors.join("; "));
        d = parse(imp("flowchart TD\nsubgraph S\nC --> [x]\nA --> B\nend\n"));
        check("an edge with an id-less end does not leave a group member with no node",
              d.ok() && d.groups.value(0).members == QStringList({"A", "B"}), d.errors.join("; "));
    }

    // ── header detection: only real Mermaid type words refuse ──
    // A headerless flowchart whose first line was a bare word ending in
    // "Diagram" or "-beta" was refused as an unsupported type.
    {
        QString err;
        for (const char *first : {"Diagram", "diagram", "Journey", "Gantt", "Pie", "MyDiagram",
                                  "Beta-beta", "x_Diagram", "my-thing-beta"}) {
            err = "stale";
            const QString src = QStringLiteral("%1\nA --> B\n").arg(QLatin1String(first));
            const QString out = imp(src, &err);
            check(qPrintable(QStringLiteral("headerless, first line \"%1\" → imports")
                                 .arg(QLatin1String(first))),
                  !out.isEmpty() && err.isEmpty() && parse(out).edges.size() == 1, err);
        }
        for (const char *t : {"sequenceDiagram", "classDiagram", "stateDiagram", "erDiagram",
                              "requirementDiagram", "newThingDiagram", "radar-beta", "foo-beta",
                              "gantt", "pie"}) {
            err.clear();
            const QString out = imp(QStringLiteral("%1\nA --> B\n").arg(QLatin1String(t)), &err);
            check(qPrintable(QStringLiteral("\"%1\" header → still refused").arg(QLatin1String(t))),
                  out.isEmpty() && err.startsWith(QStringLiteral("Mermaid \"%1\"").arg(QLatin1String(t))),
                  err);
        }
    }

    // ── the header's promise, over every import above ──
    std::printf("\n— every import validates (%d) ─────────────────\n", int(g_imports.size()));
    {
        int bad = 0;
        for (const Imported &im : g_imports) {
            const Diagram d = parse(im.npd);
            if (d.ok()) continue;
            ++bad;
            std::printf("  [FAIL] import does not validate: %s\n    errors: %s\n",
                        im.mermaid.left(80).toUtf8().constData(),
                        d.errors.join(QStringLiteral(" | ")).toUtf8().constData());
        }
        check(qPrintable(QStringLiteral("all %1 import outputs parse with zero errors")
                             .arg(g_imports.size())),
              bad == 0 && g_imports.size() > 30, QString::number(bad) + " failed");
    }

    std::printf("\n=== Summary: %d passed, %d failed ===\n", g_pass, g_fail);
    return g_fail == 0 ? 0 : 1;
}
