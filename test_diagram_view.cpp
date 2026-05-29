// SPDX-License-Identifier: GPL-3.0-or-later
//
// test_diagram_view — native-Qt diagram renderer + export (NO WebEngine).
// Builds in BOTH flavors. Validates layout, non-empty paint, hit-test, and
// PNG/SVG/PDF export through the public DiagramView API + DiagramRender module.

#include "diagram/diagram_render.h"
#include "diagram/diagram_view.h"
#include "diagram/npd_parser.h"

#include <QApplication>
#include <QImage>
#include <QJsonArray>
#include <QJsonObject>
#include <QPainter>
#include <QTemporaryDir>
#include <QFileInfo>
#include <QtTest>

static const char *kNpd =
    "diagram system\n"
    "title \"Test\"\n"
    "palette ocean\n"
    "node a (Start)\n"
    "node b {Valid?}\n"
    "node c [Process] :: \"hover detail\"\n"
    "node d ([Store])\n"
    "icon e :database \"DB\"\n"
    "a -> b\n"
    "b -> c : yes\n"
    "b -> d : no\n"
    "c -> e\n"
    "d <-> e : sync\n";

class TestDiagramView : public QObject {
    Q_OBJECT
private slots:
    void layout_has_geometry() {
        const Npd::Diagram d = Npd::parse(QString::fromUtf8(kNpd));
        QVERIFY(d.ok());
        QCOMPARE(d.nodes.size(), 5);
        const auto lay = DiagramRender::computeLayout(d);
        QVERIFY(lay.canvasW > 100);
        QVERIFY(lay.canvasH > 100);
        QCOMPARE(lay.nodeRects.size(), 5);
        for (const auto &n : d.nodes) {
            QVERIFY(lay.nodeRects.contains(n.id));
            QVERIFY(lay.nodeRects[n.id].width() > 0);
        }
        QVERIFY(lay.nodeRects["a"].center().y() < lay.nodeRects["b"].center().y());
        QVERIFY(lay.nodeRects["b"].center().y() < lay.nodeRects["c"].center().y());
    }

    void paint_is_nonempty() {
        const Npd::Diagram d = Npd::parse(QString::fromUtf8(kNpd));
        const auto lay = DiagramRender::computeLayout(d);
        const auto pal = DiagramRender::palette(d.palette);
        QImage img(int(lay.canvasW), int(lay.canvasH), QImage::Format_ARGB32);
        img.fill(Qt::transparent);
        QPainter p(&img);
        DiagramRender::paint(p, d, lay, pal);
        p.end();
        int painted = 0;
        for (int y = 0; y < img.height(); y += 3)
            for (int x = 0; x < img.width(); x += 3)
                if (qAlpha(img.pixel(x, y)) > 0) ++painted;
        QVERIFY(painted > 100);
    }

    void hittest_finds_node() {
        const Npd::Diagram d = Npd::parse(QString::fromUtf8(kNpd));
        const auto lay = DiagramRender::computeLayout(d);
        const QString id = DiagramRender::nodeAt(d, lay, lay.nodeRects["c"].center());
        QCOMPARE(id, QStringLiteral("c"));
        QVERIFY(DiagramRender::nodeAt(d, lay, QPointF(-50, -50)).isEmpty());
    }

    void export_all_formats() {
        QTemporaryDir tmp; QVERIFY(tmp.isValid());
        DiagramView view;
        view.resize(800, 600);
        view.setSource(QString::fromUtf8(kNpd));

        // first 4 bytes that prove the file is really that format
        auto magicOk = [](const QString &path, const QByteArray &magic) {
            QFile f(path); if (!f.open(QIODevice::ReadOnly)) return false;
            return f.read(magic.size()) == magic;
        };

        const QStringList supported = DiagramView::supportedExportFormats(); // PNG/JPEG/PDF[/WebP/SVG/HTML]
        qInfo() << "Supported on this build:" << supported.join(", ");

        struct Case { const char *fmt; QByteArray magic; };
        const QVector<Case> cases = {
            {"png",  QByteArray::fromHex("89504e47")},   // ‰PNG
            {"jpeg", QByteArray::fromHex("ffd8ff")},     // JPEG SOI
            {"pdf",  QByteArray("%PDF")},
            {"webp", QByteArray("RIFF")},                // RIFF....WEBP
            {"svg",  QByteArray("<?xml")},
            {"html", QByteArray("<!doc")},
        };
        for (const Case &c : cases) {
            const bool wantSupported =
                supported.contains(QString(c.fmt), Qt::CaseInsensitive);
            const QString path = tmp.filePath(QString("d.") + c.fmt);
            const bool ok = view.exportTo(c.fmt, path);
            if (wantSupported) {
                QVERIFY2(ok, c.fmt);
                QVERIFY2(QFileInfo(path).size() > 200, c.fmt);
                QVERIFY2(magicOk(path, c.magic), c.fmt);
                qInfo("  %-5s -> OK   %lld bytes  (valid)", c.fmt, QFileInfo(path).size());
            } else {
                QVERIFY2(!ok, c.fmt);   // must fail cleanly, not silently write garbage
                qInfo("  %-5s -> n/a  (not in this build; export refused)", c.fmt);
            }
        }
        QVERIFY(!view.exportTo("xyz", tmp.filePath("d.xyz")));  // unknown format
    }

    void icon_aliases_render() {
        QImage img(64, 64, QImage::Format_ARGB32); img.fill(Qt::transparent);
        QPainter p(&img);
        DiagramRender::drawIcon(p, "postgres", QRectF(8,8,48,48), QColor("#7cc4ff")); // -> database
        DiagramRender::drawIcon(p, "k8s", QRectF(8,8,48,48), QColor("#7cc4ff"));      // -> kubernetes
        p.end();
        int painted = 0;
        for (int y = 0; y < 64; ++y) for (int x = 0; x < 64; ++x) if (qAlpha(img.pixel(x,y))>0) ++painted;
        QVERIFY(painted > 20);
    }

    // v0.1.104 SECURITY regression (#5): an untrusted .npd title containing
    // "</title><script>…" must NOT inject a live tag into exported HTML/SVG —
    // it has to be HTML-escaped at the write site.
    void html_export_escapes_title_xss() {
        if (!DiagramView::supportedExportFormats().contains(QStringLiteral("HTML"),
                                                            Qt::CaseInsensitive))
            QSKIP("HTML export not supported on this build");

        QTemporaryDir tmp; QVERIFY(tmp.isValid());
        const QString payload = "x</title><script>alert(1)</script>";
        const QString npd =
            "diagram flow\n"
            "title \"" + payload + "\"\n"
            "node a (Start)\n"
            "a -> a\n";
        DiagramView view;
        view.resize(800, 600);
        view.setSource(npd);

        const QString path = tmp.filePath(QStringLiteral("xss.html"));
        QVERIFY(view.exportTo(QStringLiteral("html"), path));
        QFile f(path); QVERIFY(f.open(QIODevice::ReadOnly));
        const QString html = QString::fromUtf8(f.readAll());
        QVERIFY2(!html.contains(QStringLiteral("<script>")),
                 "live <script> tag leaked into exported HTML");
        QVERIFY2(!html.contains(QStringLiteral("</title><script")),
                 "title was not escaped — </title> break-out present");
        QVERIFY2(html.contains(QStringLiteral("&lt;script&gt;")),
                 "escaped payload not found in exported HTML");

        // Same vector via standalone SVG export: QSvgGenerator::setTitle() does
        // NOT escape, so the SVG <title> must be escaped at the call site.
        if (DiagramView::supportedExportFormats().contains(QStringLiteral("SVG"),
                                                           Qt::CaseInsensitive)) {
            const QString spath = tmp.filePath(QStringLiteral("xss.svg"));
            QVERIFY(view.exportTo(QStringLiteral("svg"), spath));
            QFile sf(spath); QVERIFY(sf.open(QIODevice::ReadOnly));
            const QString svg = QString::fromUtf8(sf.readAll());
            QVERIFY2(!svg.contains(QStringLiteral("<script>")),
                     "live <script> tag leaked into exported SVG");
            QVERIFY2(!svg.contains(QStringLiteral("</title><script")),
                     "SVG <title> not escaped — </title> break-out present");
        }
    }

    // v0.1.104 FEATURE: optional inline per-node colour. A #hex token is always
    // taken; a colour NAME is taken only after a shape/quote boundary, so a bare
    // word in an unbracketed label is left alone. Label/hover/shape survive.
    void inline_color_parses() {
        const char *src =
            "diagram flow\n"
            "node a (Start) green\n"
            "node b [Process] #1565c0 :: \"detail\"\n"
            "node c {Valid?} orange\n"
            "node d [Plain label]\n"
            "node e Plain red\n"                         // unbracketed → 'red' is NOT a colour
            "icon f :database \"Users\" #6a1b9a :: \"store\"\n"
            "a -> b\n";
        const Npd::Diagram d = Npd::parse(QString::fromUtf8(src));
        QVERIFY(d.ok());
        auto byId = [&](const QString &id) -> Npd::Node {
            for (const auto &n : d.nodes) if (n.id == id) return n; return {}; };

        const Npd::Node a = byId("a");
        QCOMPARE(a.color, QStringLiteral("green"));
        QCOMPARE(a.label, QStringLiteral("Start"));
        QCOMPARE(a.shape, Npd::Shape::Pill);

        const Npd::Node b = byId("b");
        QCOMPARE(b.color, QStringLiteral("#1565c0"));
        QCOMPARE(b.label, QStringLiteral("Process"));
        QCOMPARE(b.hover, QStringLiteral("detail"));
        QCOMPARE(b.shape, Npd::Shape::Box);

        QCOMPARE(byId("c").color, QStringLiteral("orange"));
        QCOMPARE(byId("c").shape, Npd::Shape::Decision);

        // bracketed plain label, no trailing colour
        QVERIFY(byId("d").color.isEmpty());
        QCOMPARE(byId("d").label, QStringLiteral("Plain label"));

        // conservative rule: 'red' has no shape/quote boundary in front of it
        QVERIFY2(byId("e").color.isEmpty(), "bare word in unbracketed label wrongly taken as colour");
        QCOMPARE(byId("e").label, QStringLiteral("Plain red"));

        const Npd::Node f = byId("f");
        QCOMPARE(f.color, QStringLiteral("#6a1b9a"));
        QCOMPARE(f.label, QStringLiteral("Users"));
        QCOMPARE(f.icon,  QStringLiteral("database"));
        QCOMPARE(f.hover, QStringLiteral("store"));

        // colour rides through to the JSON contract the renderer/export consume
        const QJsonObject g = Npd::toGraphJson(d);
        const QJsonArray nodes = g.value(QStringLiteral("nodes")).toArray();
        bool sawColouredA = false;
        for (const auto &v : nodes) {
            const QJsonObject o = v.toObject();
            if (o.value(QStringLiteral("id")).toString() == QLatin1String("a"))
                sawColouredA = (o.value(QStringLiteral("color")).toString() == QLatin1String("green"));
        }
        QVERIFY2(sawColouredA, "toGraphJson dropped the node colour");
    }

    // A coloured node must actually paint different pixels than the same node
    // left to the palette — and the fill must be in the requested colour family.
    void colored_node_changes_pixels() {
        const QString bare    = QStringLiteral("diagram flow\nnode a [Step]\n");
        const QString colored = QStringLiteral("diagram flow\nnode a [Step] #d81b60\n"); // magenta
        const Npd::Diagram db = Npd::parse(bare), dc = Npd::parse(colored);
        const auto lay = DiagramRender::computeLayout(db);  // identical geometry
        const auto pal = DiagramRender::palette(db.palette);

        auto render = [&](const Npd::Diagram &d) {
            QImage img(int(lay.canvasW), int(lay.canvasH), QImage::Format_ARGB32);
            img.fill(Qt::transparent);
            QPainter p(&img); DiagramRender::paint(p, d, lay, pal); p.end();
            return img;
        };
        const QImage ib = render(db), ic = render(dc);
        // Sample the FILL above the vertically-centred label (the label glyph is
        // white auto-contrast, so the centre pixel would read as text, not fill).
        const QRectF ra = lay.nodeRects["a"];
        const QPoint fill(qRound(ra.center().x()), qRound(ra.top() + ra.height() * 0.20));

        QVERIFY2(ib.pixel(fill) != ic.pixel(fill), "coloured node did not change the fill");
        const QRgb px = ic.pixel(fill);
        // #d81b60 is red-dominant magenta; gradient keeps red the clear max channel
        QVERIFY2(qRed(px) > 120, "coloured fill is not in the requested (reddish) family");
        QVERIFY2(qRed(px) > qGreen(px) + 30 && qRed(px) >= qBlue(px),
                 "coloured fill hue does not match #d81b60");
    }

    // v0.1.104 FEATURE: chained arrows. `a -> b -> c` expands to a→b and b→c —
    // it must NOT create one node literally named "b -> c" (the old bug). Each
    // hop keeps its own direction; a trailing ": label" is verbatim (so a label
    // may contain "->") and decorates the LAST hop.
    void chain_arrows_parse() {
        const char *src =
            "diagram flow\n"
            "a -> b -> c -> d\n"               // 3-hop directed chain
            "m -> n <-> o\n"                   // mixed: m→n directed, n↔o bidirectional
            "p -> q -> r : done\n"             // trailing label decorates the LAST hop
            "req -> resp : maps A -> B\n";     // ': label' is verbatim — the arrow in
                                               // the label is NOT a hop to a node "B"
        const Npd::Diagram dg = Npd::parse(QString::fromUtf8(src));
        QVERIFY2(dg.ok(), qPrintable(dg.errors.join("; ")));

        // no node id ever contains an arrow — the old code made "b -> c" boxes
        for (const auto &n : dg.nodes)
            QVERIFY2(!n.id.contains(QStringLiteral("->")),
                     qPrintable(QStringLiteral("stray arrow-node: ") + n.id));

        auto hasEdge = [&](const QString &f, const QString &t, bool bidir, const QString &lbl) {
            for (const auto &e : dg.edges)
                if (e.from == f && e.to == t && e.bidirectional == bidir && e.label == lbl)
                    return true;
            return false;
        };
        // 3-hop chain → three directed edges
        QVERIFY(hasEdge("a", "b", false, QString()));
        QVERIFY(hasEdge("b", "c", false, QString()));
        QVERIFY(hasEdge("c", "d", false, QString()));
        // mixed directionality within one line
        QVERIFY(hasEdge("m", "n", false, QString()));
        QVERIFY(hasEdge("n", "o", true,  QString()));
        // trailing label rides the final hop only (p→q stays unlabelled)
        QVERIFY(hasEdge("p", "q", false, QString()));
        QVERIFY(hasEdge("q", "r", false, QStringLiteral("done")));
        // a ': label' is verbatim: "maps A -> B" is ONE label, not a hop to "B"
        QVERIFY(hasEdge("req", "resp", false, QStringLiteral("maps A -> B")));

        // every node across the whole chain is auto-created
        QStringList ids; for (const auto &n : dg.nodes) ids << n.id;
        const QStringList want = {"a","b","c","d","m","n","o","p","q","r","req","resp"};
        for (const QString &id : want)
            QVERIFY2(ids.contains(id), qPrintable(QStringLiteral("missing node: ") + id));
        // the label text never leaks into a node id
        QVERIFY2(!ids.contains(QStringLiteral("B")), "arrow-in-label wrongly created a node");

        // single-segment edges are untouched (regression guard)
        const Npd::Diagram one = Npd::parse(QStringLiteral("a -> b : yes\n"));
        QCOMPARE(one.edges.size(), 1);
        QCOMPARE(one.edges.first().label, QStringLiteral("yes"));
        QVERIFY(!one.edges.first().bidirectional);
    }

    // v0.1.104 — adversarial-audit fixes. The label ':' is only the one written
    // AFTER the first arrow AND with a leading space, so colons inside node ids
    // (URLs, namespaces, typed ids) survive; a leading keyword always wins so a
    // node label may contain "->"; '::' on an edge yields a clean label.
    void edge_colon_keyword_robustness() {
        auto edgeOf = [](const Npd::Diagram &d) { return d.edges; };
        auto only = [&](const QString &src) -> Npd::Edge {
            const Npd::Diagram d = Npd::parse(src);
            Q_ASSERT(d.ok()); Q_ASSERT(d.edges.size() == 1);
            return d.edges.first();
        };

        // #1/#3 colon in the LEFT node id (namespace / URL) — was a regression
        {
            const Npd::Diagram d = Npd::parse(QStringLiteral("a:start -> b\n"));
            QVERIFY2(d.ok(), qPrintable(d.errors.join("; ")));
            QCOMPARE(d.edges.size(), 1);
            QCOMPARE(d.edges.first().from, QStringLiteral("a:start"));
            QCOMPARE(d.edges.first().to,   QStringLiteral("b"));
            QVERIFY(d.edges.first().label.isEmpty());
        }
        {
            const Npd::Diagram d = Npd::parse(QStringLiteral("http://x -> y\n"));
            QVERIFY2(d.ok(), qPrintable(d.errors.join("; ")));
            QCOMPARE(d.edges.size(), 1);
            QCOMPARE(d.edges.first().from, QStringLiteral("http://x"));
            QCOMPARE(d.edges.first().to,   QStringLiteral("y"));
        }

        // #7/#10 colon in a RIGHT / mid-chain node id is part of the id, not a label
        {
            const Npd::Diagram d = Npd::parse(QStringLiteral("a -> b:state -> c\n"));
            QVERIFY2(d.ok(), qPrintable(d.errors.join("; ")));
            QCOMPARE(d.edges.size(), 2);
            QCOMPARE(d.edges.at(0).from, QStringLiteral("a"));
            QCOMPARE(d.edges.at(0).to,   QStringLiteral("b:state"));
            QVERIFY(d.edges.at(0).label.isEmpty());
            QCOMPARE(d.edges.at(1).from, QStringLiteral("b:state"));
            QCOMPARE(d.edges.at(1).to,   QStringLiteral("c"));
        }

        // a real " : label" still works and rides the (single) hop
        {
            const Npd::Edge e = only(QStringLiteral("check -> dash : yes\n"));
            QCOMPARE(e.from, QStringLiteral("check"));
            QCOMPARE(e.to,   QStringLiteral("dash"));
            QCOMPARE(e.label, QStringLiteral("yes"));
        }
        // …and a label may itself contain "->"
        {
            const Npd::Edge e = only(QStringLiteral("req -> resp : maps A -> B\n"));
            QCOMPARE(e.to,    QStringLiteral("resp"));
            QCOMPARE(e.label, QStringLiteral("maps A -> B"));
        }

        // #4/#11 '::' on an edge line → clean label (no stray leading colon)
        {
            const Npd::Edge e = only(QStringLiteral("a -> b :: detail\n"));
            QCOMPARE(e.from, QStringLiteral("a"));
            QCOMPARE(e.to,   QStringLiteral("b"));
            QCOMPARE(e.label, QStringLiteral("detail"));
        }

        // #5 a NODE line whose label contains "->" is a node, not an edge
        {
            const Npd::Diagram d = Npd::parse(QStringLiteral("node x [Maps A -> B]\n"));
            QVERIFY2(d.ok(), qPrintable(d.errors.join("; ")));
            QCOMPARE(edgeOf(d).size(), 0);
            QCOMPARE(d.nodes.size(), 1);
            QCOMPARE(d.nodes.first().id,    QStringLiteral("x"));
            QCOMPARE(d.nodes.first().label, QStringLiteral("Maps A -> B"));
            QCOMPARE(d.nodes.first().shape, Npd::Shape::Box);
        }
        // …same for a textbox caption containing an arrow
        {
            const Npd::Diagram d = Npd::parse(QStringLiteral("textbox \"x -> y flow\"\n"));
            QVERIFY2(d.ok(), qPrintable(d.errors.join("; ")));
            QCOMPARE(edgeOf(d).size(), 0);
            QCOMPARE(d.textboxes.size(), 1);
            QCOMPARE(d.textboxes.first(), QStringLiteral("x -> y flow"));
        }
    }

    // Colour is not gated on diagram type or shape: it works on database
    // cylinders (ER) and icon nodes (system) exactly as on flow boxes.
    void color_on_er_and_system_nodes() {
        const Npd::Diagram er = Npd::parse(QStringLiteral(
            "diagram er\n"
            "node customers ([Customers]) #2e7d32 :: \"id, name\"\n"
            "node orders ([Orders]) #1565c0\n"
            "customers -> orders : places\n"));
        QVERIFY2(er.ok(), qPrintable(er.errors.join("; ")));
        auto eid=[&](const QString&id){ for(const auto&n:er.nodes) if(n.id==id) return n; return Npd::Node{}; };
        QCOMPARE(eid("customers").color, QStringLiteral("#2e7d32"));  // colour before :: hover
        QCOMPARE(eid("customers").shape, Npd::Shape::Database);
        QCOMPARE(eid("customers").hover, QStringLiteral("id, name"));
        QCOMPARE(eid("orders").color, QStringLiteral("#1565c0"));
        QCOMPARE(eid("orders").shape, Npd::Shape::Database);

        const Npd::Diagram sys = Npd::parse(QStringLiteral(
            "diagram system\n"
            "icon user :user \"Browser\" #00838f :: \"client\"\n"
            "icon db :database \"DB\" #6a1b9a\n"
            "user -> db\n"));
        QVERIFY2(sys.ok(), qPrintable(sys.errors.join("; ")));
        auto sid=[&](const QString&id){ for(const auto&n:sys.nodes) if(n.id==id) return n; return Npd::Node{}; };
        QCOMPARE(sid("user").color, QStringLiteral("#00838f"));
        QCOMPARE(sid("user").shape, Npd::Shape::Icon);
        QCOMPARE(sid("user").icon, QStringLiteral("user"));
        QCOMPARE(sid("user").hover, QStringLiteral("client"));
        QCOMPARE(sid("db").color, QStringLiteral("#6a1b9a"));
    }
};

QTEST_MAIN(TestDiagramView)
#include "test_diagram_view.moc"
