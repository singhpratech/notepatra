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
        check("default palette", d.palette == "default");

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
