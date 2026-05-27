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
};

QTEST_MAIN(TestDiagramView)
#include "test_diagram_view.moc"
