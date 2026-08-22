// SPDX-License-Identifier: GPL-3.0-or-later
//
// Round-trip tests for the Mermaid → .npd importer: translate Mermaid, then
// parse the result with the real Npd::parse and assert the node/edge model is
// what we expect. Pure (Qt Core only) — no GUI, no WebEngine.

#include "src/diagram/mermaid_import.h"
#include "src/diagram/npd_parser.h"

#include <QCoreApplication>
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

int main(int argc, char **argv) {
    QCoreApplication app(argc, argv);
    std::printf("=== Mermaid → .npd importer tests ===\n\n");

    check("empty input → empty output", mermaidToNpd("").isEmpty());
    check("whitespace input → empty output", mermaidToNpd("   \n\n").isEmpty());

    // ── shapes + inline node defs in edges ──
    {
        Diagram d = parse(mermaidToNpd(
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
        Diagram d = parse(mermaidToNpd("graph LR\nx(Round) --> y([Stadium])\nz((Circle)) --> y"));
        check("() → pill", nodeById(d, "x").shape == Shape::Pill);
        check("([]) → pill", nodeById(d, "y").shape == Shape::Pill);
        check("(()) → pill", nodeById(d, "z").shape == Shape::Pill);
    }

    // ── edge labels: pipe + dash forms ──
    {
        Diagram d = parse(mermaidToNpd("graph TD\nA -->|yes| B\nA -- no --> C"));
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
        Diagram d = parse(mermaidToNpd("graph LR\nA <--> B"));
        check("bidirectional edge", d.edges.size() == 1 && d.edges.at(0).bidirectional);
    }

    // ── arrow variants all become a directed edge ──
    {
        Diagram d = parse(mermaidToNpd("graph TD\nA --- B\nB ==> C\nC -.-> D"));
        check("--- / ==> / -.-> all become edges", d.edges.size() == 3,
              QString::number(d.edges.size()));
    }

    // ── unsupported constructs are dropped, output still parses ──
    {
        Diagram d = parse(mermaidToNpd(
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
        Diagram d = parse(mermaidToNpd("graph TD\nA -.-> B\nB --> C\nC -.->|later| D"));
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
        const QString npd = mermaidToNpd(
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
        Diagram t = parse(mermaidToNpd("flowchart TD\nsubgraph s1 [Nice Name]\nA --> B\nend\n"));
        check("subgraph id [Title] uses the title", t.groups.value(0).label == "Nice Name",
              t.groups.isEmpty() ? "(none)" : t.groups.at(0).label);

        // a quote inside a subgraph title must not break the quoted group label
        Diagram q = parse(mermaidToNpd("flowchart TD\nsubgraph My \"Tier\"\nA --> B\nend\n"));
        check("quoted subgraph title still parses", q.ok(), q.errors.join("; "));
        check("quoted subgraph title kept its words",
              q.groups.value(0).label.contains("Tier"),
              q.groups.isEmpty() ? "(none)" : q.groups.at(0).label);

        // a node is claimed by ONE group only, so the .npd stays valid
        Diagram once = parse(mermaidToNpd(
            "flowchart TD\nsubgraph One\nA --> B\nend\nsubgraph Two\nA --> C\nend\n"));
        check("shared node stays in a single group", once.ok(), once.errors.join("; "));
    }

    // ── v0.1.120: graph LR carries the direction over ──
    {
        Diagram lr = parse(mermaidToNpd("graph LR\nA --> B\n"));
        check("graph LR → direction LR", lr.direction == "LR", lr.direction);
        check("graph TD → direction TB", parse(mermaidToNpd("graph TD\nA --> B\n")).direction == "TB");
        check("flowchart RL → direction LR", parse(mermaidToNpd("flowchart RL\nA --> B\n")).direction == "LR");
        check("no direction token → default TB", parse(mermaidToNpd("graph\nA --> B\n")).direction == "TB");
    }

    // ── a realistic flow ──
    {
        const QString npd = mermaidToNpd(
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

    std::printf("\n=== Summary: %d passed, %d failed ===\n", g_pass, g_fail);
    return g_fail == 0 ? 0 : 1;
}
