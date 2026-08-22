/**
 * Deep test for the Notepatra Diagram (.npd) parser — the pure source-of-truth
 * engine the diagram canvas projects from. No WebEngine, no widgets, no event
 * loop: just text → model → JSON. Every assertion maps to a grammar guarantee
 * the live preview / AI generation / export all rely on.
 */

#include "src/diagram/npd_parser.h"

#include <QCoreApplication>
#include <QJsonArray>
#include <QJsonObject>
#include <QString>

#include <cstdio>

static int g_pass = 0, g_fail = 0;
static void check(const char *what, bool ok, const QString &detail = {}) {
    if (ok) { std::printf("  [PASS] %s\n", what); ++g_pass; }
    else    {
        std::printf("  [FAIL] %s%s%s\n", what,
                    detail.isEmpty() ? "" : " — ",
                    detail.toUtf8().constData());
        ++g_fail;
    }
}

using namespace Npd;

// Find a node by id (or return a sentinel with empty id).
static Node nodeById(const Diagram &d, const QString &id) {
    for (const Node &n : d.nodes) if (n.id == id) return n;
    return Node{};
}

int main(int argc, char **argv) {
    QCoreApplication app(argc, argv);
    std::printf("=== .npd parser deep tests ===\n\n");

    // ── empty / whitespace / comments ──
    std::printf("— skeleton ───────────────────────────────────────\n");
    {
        Diagram d = parse("");
        check("empty input is a clean parse", d.ok());
        check("empty input → 0 nodes", d.nodes.isEmpty());
        check("default type is flow", d.type == "flow");
        // v0.1.120: with no `palette` line the token is "auto" — the RAW string,
        // because resolving it needs the host theme, which this pure layer never
        // sees. The renderer turns it into paper (light host) or default (dark).
        check("default palette token is auto", d.palette == "auto");
        check("default direction is TB", d.direction == "TB");

        Diagram c = parse("# just a comment\n\n   \n# another\n");
        check("comments + blank lines → empty + clean", c.ok() && c.nodes.isEmpty());
    }

    // ── header directives ──
    std::printf("\n— directives ─────────────────────────────────────\n");
    {
        check("diagram er", parse("diagram er").type == "er");
        check("diagram system", parse("diagram system").type == "system");
        Diagram bad = parse("diagram bogus");
        check("unknown diagram type → error", !bad.ok());
        check("unknown diagram type → stays flow", bad.type == "flow");

        check("title (quoted)", parse("title \"User Login\"").title == "User Login");
        check("title (unquoted)", parse("title Bare Title").title == "Bare Title");
        check("palette", parse("palette ocean").palette == "ocean");
        check("palette auto is stored raw", parse("palette auto").palette == "auto");
        check("palette paper is stored raw", parse("palette paper").palette == "paper");

        Diagram tb = parse("textbox \"A caption.\"\ntextbox \"Two.\"");
        check("textbox accumulates", tb.textboxes.size() == 2 && tb.textboxes.at(0) == "A caption.");
    }

    // ── node shapes ──
    std::printf("\n— node shapes ────────────────────────────────────\n");
    {
        Diagram d = parse(
            "node a (Start)\n"
            "node b [Process]\n"
            "node c {Valid?}\n"
            "node d ([Users])\n");
        check("4 declared nodes", d.nodes.size() == 4, QString::number(d.nodes.size()));
        check("() → pill",      nodeById(d, "a").shape == Shape::Pill);
        check("[] → box",       nodeById(d, "b").shape == Shape::Box);
        check("{} → decision",  nodeById(d, "c").shape == Shape::Decision);
        check("([]) → database",nodeById(d, "d").shape == Shape::Database);
        check("pill label",     nodeById(d, "a").label == "Start");
        check("database label", nodeById(d, "d").label == "Users");
    }

    // ── hover detail (the label-overflow → tooltip feature) ──
    std::printf("\n— hover detail (::) ──────────────────────────────\n");
    {
        Diagram d = parse("node ok [Dashboard] :: \"Loads the saved session + tabs\"");
        const Node n = nodeById(d, "ok");
        check("short label stays in shape", n.label == "Dashboard");
        check("full detail captured for hover", n.hover == "Loads the saved session + tabs");
    }

    // ── icon nodes ──
    std::printf("\n— icon nodes ─────────────────────────────────────\n");
    {
        Diagram d = parse("icon db :database \"Users\" :: \"Postgres, primary\"");
        const Node n = nodeById(d, "db");
        check("icon → Shape::Icon", n.shape == Shape::Icon);
        check("icon name parsed", n.icon == "database");
        check("icon label parsed", n.label == "Users");
        check("icon hover parsed", n.hover == "Postgres, primary");

        check("icon without ':name' → error", !parse("icon x \"NoColon\"").ok());
    }

    // ── edges ──
    std::printf("\n— edges ──────────────────────────────────────────\n");
    {
        Diagram d = parse(
            "node a [A]\n"
            "node b [B]\n"
            "a -> b : yes\n"
            "a -> b\n"
            "a <-> b : sync\n");
        check("3 edges", d.edges.size() == 3, QString::number(d.edges.size()));
        check("edge label captured", d.edges.at(0).label == "yes");
        check("labelless edge has empty label", d.edges.at(1).label.isEmpty());
        check("plain -> is not bidirectional", !d.edges.at(1).bidirectional);
        check("<-> is bidirectional", d.edges.at(2).bidirectional);
        check("<-> label captured", d.edges.at(2).label == "sync");
        check("edge direction from→to", d.edges.at(0).from == "a" && d.edges.at(0).to == "b");

        check("edge missing endpoint → error", !parse("a ->").ok());
    }

    // ── leniency: undeclared endpoints auto-create as boxes ──
    std::printf("\n— auto-created endpoints ─────────────────────────\n");
    {
        Diagram d = parse("start -> finish");
        check("bare edge → clean parse", d.ok());
        check("both endpoints auto-created", d.nodes.size() == 2);
        check("auto node labelled by id", nodeById(d, "start").label == "start");
        check("auto node is a box", nodeById(d, "finish").shape == Shape::Box);
        check("declaring a node later upgrades its shape",
              nodeById(parse("a -> b\nnode b {Q?}"), "b").shape == Shape::Decision);
    }

    // ── error reporting ──
    std::printf("\n— errors ─────────────────────────────────────────\n");
    {
        Diagram d = parse("node a [A]\nthis is garbage\nnode b [B]");
        check("garbage line is reported", !d.ok());
        check("error carries the line number", d.errors.at(0).startsWith("line 2:"),
              d.errors.isEmpty() ? "(no errors)" : d.errors.at(0));
        check("valid lines still parse around the error", d.nodes.size() == 2);
    }

    // ── JSON contract (what render.js consumes) ──
    std::printf("\n— toGraphJson contract ───────────────────────────\n");
    {
        Diagram d = parse(
            "diagram flow\n"
            "title \"Login\"\n"
            "palette clay\n"
            "node a (Start)\n"
            "node b [Dashboard] :: \"detail\"\n"
            "a -> b : go\n"
            "textbox \"note\"\n");
        const QJsonObject j = toGraphJson(d);
        check("json type",    j.value("type").toString() == "flow");
        check("json title",   j.value("title").toString() == "Login");
        check("json palette", j.value("palette").toString() == "clay");
        const QJsonArray jn = j.value("nodes").toArray();
        check("json has 2 nodes", jn.size() == 2, QString::number(jn.size()));
        check("json node shape", jn.at(0).toObject().value("shape").toString() == "pill");
        check("json node hover only when present",
              jn.at(1).toObject().contains("hover") && !jn.at(0).toObject().contains("hover"));
        const QJsonArray je = j.value("edges").toArray();
        check("json has 1 edge", je.size() == 1);
        check("json edge label", je.at(0).toObject().value("label").toString() == "go");
        check("json textboxes", j.value("textboxes").toArray().size() == 1);
    }

    // ── direction (v0.1.120) ──
    std::printf("\n— direction ──────────────────────────────────────\n");
    {
        check("direction LR", parse("direction LR").direction == "LR");
        check("direction lr is case-insensitive", parse("direction lr").direction == "LR");
        check("direction TB", parse("direction TB").direction == "TB");
        check("direction TD is a TB alias", parse("direction TD").direction == "TB");
        check("layout is an alias for direction", parse("layout LR").direction == "LR");
        Diagram bad = parse("direction sideways");
        check("unknown direction → error", !bad.ok());
        check("unknown direction → stays TB", bad.direction == "TB");
        // a `direction` line must never be mistaken for an edge/garbage
        check("direction line does not create nodes", parse("direction LR").nodes.isEmpty());
    }

    // ── dashed edges (v0.1.120) ──
    std::printf("\n— dashed edges ───────────────────────────────────\n");
    {
        Diagram d = parse("a -.-> b\nc <.-> e\nf -> g\n");
        check("dashed chain is clean", d.ok(), d.errors.join("; "));
        check("3 edges", d.edges.size() == 3, QString::number(d.edges.size()));
        check("-.-> is dashed", d.edges.at(0).dashed && !d.edges.at(0).bidirectional);
        check("-.-> endpoints", d.edges.at(0).from == "a" && d.edges.at(0).to == "b");
        check("<.-> is dashed + bidirectional", d.edges.at(1).dashed && d.edges.at(1).bidirectional);
        check("<.-> endpoints", d.edges.at(1).from == "c" && d.edges.at(1).to == "e");
        check("plain -> stays solid", !d.edges.at(2).dashed);
        check("dashed hop makes no stray node ids",
              !nodeById(d, "a").id.isEmpty() && d.nodes.size() == 6, QString::number(d.nodes.size()));

        Diagram lab = parse("a -.-> b : async\n");
        check("dashed edge keeps its label", lab.edges.size() == 1 && lab.edges.at(0).label == "async");
        check("dashed edge with a label is still dashed", lab.edges.at(0).dashed);

        Diagram mix = parse("a -> b -.-> c\n");
        check("mixed chain: solid then dashed",
              mix.edges.size() == 2 && !mix.edges.at(0).dashed && mix.edges.at(1).dashed);

        const QJsonObject j = toGraphJson(mix);
        const QJsonArray je = j.value("edges").toArray();
        check("json marks the dashed hop only",
              !je.at(0).toObject().contains("dashed") && je.at(1).toObject().value("dashed").toBool());
    }

    // ── groups (v0.1.120) ──
    std::printf("\n— groups ─────────────────────────────────────────\n");
    {
        Diagram d = parse(
            "node a [A]\nnode b [B]\nnode c [C]\n"
            "a -> b -> c\n"
            "group \"Edge tier\" : a b\n");
        check("group parses clean", d.ok(), d.errors.join("; "));
        check("one group", d.groups.size() == 1, QString::number(d.groups.size()));
        check("group label", d.groups.at(0).label == "Edge tier");
        check("group members", d.groups.at(0).members == QStringList({"a","b"}));

        // forward reference: nodes may be declared AFTER the group line
        Diagram fwd = parse("group \"Later\" : a b\nnode a [A]\nnode b [B]\n");
        check("group before the node declarations is clean", fwd.ok(), fwd.errors.join("; "));

        // unknown member id → line error
        Diagram unk = parse("node a [A]\ngroup \"G\" : a nope\n");
        check("unknown group member → error", !unk.ok());
        check("unknown group member error carries the line + id",
              !unk.errors.isEmpty() && unk.errors.at(0).startsWith("line 2:")
                  && unk.errors.at(0).contains("nope"),
              unk.errors.isEmpty() ? "(no errors)" : unk.errors.at(0));

        // a node may be in at most one group
        Diagram dbl = parse("node a [A]\nnode b [B]\n"
                            "group \"One\" : a\ngroup \"Two\" : a b\n");
        check("double group membership → error", !dbl.ok());
        check("double membership error names the first group",
              !dbl.errors.isEmpty() && dbl.errors.at(0).startsWith("line 4:")
                  && dbl.errors.at(0).contains("One"),
              dbl.errors.isEmpty() ? "(no errors)" : dbl.errors.at(0));
        check("both groups are still modelled", dbl.groups.size() == 2);

        // malformed group lines
        check("group with no ':' → error", !parse("node a [A]\ngroup \"G\" a\n").ok());
        check("group with no members → error", !parse("node a [A]\ngroup \"G\" :\n").ok());
        check("unquoted group label works",
              parse("node a [A]\ngroup Tier one : a\n").groups.value(0).label == "Tier one");

        // a group line whose label contains an arrow is still a group
        Diagram arrow = parse("node a [A]\ngroup \"A -> B\" : a\n");
        check("group label may contain an arrow", arrow.ok() && arrow.edges.isEmpty()
              && arrow.groups.value(0).label == "A -> B", arrow.errors.join("; "));

        const QJsonObject j = toGraphJson(d);
        const QJsonArray jg = j.value("groups").toArray();
        check("json carries the group", jg.size() == 1
              && jg.at(0).toObject().value("label").toString() == "Edge tier"
              && jg.at(0).toObject().value("members").toArray().size() == 2);
    }

    // ── notes (v0.1.120) ──
    std::printf("\n— notes ──────────────────────────────────────────\n");
    {
        Diagram d = parse("node a [A]\nnote a \"Retries 3x with backoff\"\n");
        check("note parses clean", d.ok(), d.errors.join("; "));
        check("one note", d.notes.size() == 1);
        check("note target", d.notes.at(0).target == "a");
        check("note text", d.notes.at(0).text == "Retries 3x with backoff");
        check("note does not create a node", d.nodes.size() == 1);

        check("note with an unknown target → error", !parse("note ghost \"x\"").ok());
        check("note with no text → error", !parse("node a [A]\nnote a\n").ok());
        Diagram bad = parse("node a [A]\nnote nope \"x\"\n");
        check("unknown note target error carries the line",
              !bad.ok() && bad.errors.at(0).startsWith("line 2:"),
              bad.errors.isEmpty() ? "(no errors)" : bad.errors.at(0));

        const QJsonObject j = toGraphJson(d);
        check("json carries the note", j.value("notes").toArray().size() == 1);
    }

    // ── legend (v0.1.120) ──
    std::printf("\n— legend ─────────────────────────────────────────\n");
    {
        Diagram d = parse("node a [A]\nlegend dashed \"async\"\nlegend #cc785c \"hot path\"\nlegend green \"healthy\"\n");
        check("legend parses clean", d.ok(), d.errors.join("; "));
        check("three legend rows", d.legend.size() == 3, QString::number(d.legend.size()));
        check("dashed swatch", d.legend.at(0).swatch == "dashed" && d.legend.at(0).text == "async");
        check("hex swatch", d.legend.at(1).swatch == "#cc785c" && d.legend.at(1).text == "hot path");
        check("named swatch", d.legend.at(2).swatch == "green");
        check("legend creates no nodes", d.nodes.size() == 1);

        check("legend with a junk swatch → error", !parse("legend wibble \"x\"").ok());
        check("legend with no text → error", !parse("legend dashed").ok());
        check("json carries the legend", toGraphJson(d).value("legend").toArray().size() == 3);
    }

    // ── shapeName mapping ──
    std::printf("\n— shapeName ──────────────────────────────────────\n");
    {
        check("shapeName box",      shapeName(Shape::Box) == "box");
        check("shapeName pill",     shapeName(Shape::Pill) == "pill");
        check("shapeName decision", shapeName(Shape::Decision) == "decision");
        check("shapeName database", shapeName(Shape::Database) == "database");
        check("shapeName icon",     shapeName(Shape::Icon) == "icon");
    }

    // ── a realistic flowchart end-to-end ──
    std::printf("\n— realistic flow ─────────────────────────────────\n");
    {
        Diagram d = parse(
            "diagram flow\n"
            "title \"User Login\"\n"
            "palette clay\n"
            "node start (Start)\n"
            "node check {Valid creds?}\n"
            "node dash [Dashboard] :: \"Loads saved session + tabs\"\n"
            "icon db :database \"Users\"\n"
            "start -> check\n"
            "check -> dash : yes\n"
            "check -> start : no\n"
            "dash -> db\n"
            "textbox \"Happy path + one error branch.\"\n");
        check("realistic flow is clean", d.ok(), d.errors.join("; "));
        check("realistic flow node count", d.nodes.size() == 4, QString::number(d.nodes.size()));
        check("realistic flow edge count", d.edges.size() == 4, QString::number(d.edges.size()));
        check("realistic flow title", d.title == "User Login");
    }

    std::printf("\n=== Summary: %d passed, %d failed ===\n", g_pass, g_fail);
    return g_fail == 0 ? 0 : 1;
}
