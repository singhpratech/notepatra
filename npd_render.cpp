// SPDX-License-Identifier: GPL-3.0-or-later
//
// npd_render — offscreen CLI: render a .npd diagram to an image.
//   npd_render <in.npd> <out.{png,webp,jpeg,svg,pdf,html}>
//
// Reuses DiagramView's render + export pipeline headlessly (Full/WebEngine
// build). Handy for docs, CI visual checks, and previewing a diagram without
// launching the GUI. Same offscreen-Chromium setup as test_diagram_view.

#include "diagram/diagram_view.h"

#include <QApplication>
#include <QFile>
#include <QFileInfo>
#include <QTest>

#include <cstdio>

int main(int argc, char **argv) {
    qputenv("QT_QPA_PLATFORM", "offscreen");
    qputenv("QTWEBENGINE_DISABLE_SANDBOX", "1");
    qputenv("QTWEBENGINE_CHROMIUM_FLAGS",
            "--disable-gpu --no-sandbox --disable-software-rasterizer --single-process");
    QCoreApplication::setAttribute(Qt::AA_ShareOpenGLContexts);
    QApplication app(argc, argv);

    if (argc < 3) {
        std::fprintf(stderr, "usage: npd_render <in.npd> <out.{png,webp,jpeg,svg,pdf,html}>\n");
        return 2;
    }
    QFile in(argv[1]);
    if (!in.open(QIODevice::ReadOnly)) {
        std::fprintf(stderr, "npd_render: cannot read %s\n", argv[1]);
        return 2;
    }
    const QString npd = QString::fromUtf8(in.readAll());
    const QString out = QString::fromUtf8(argv[2]);
    const QString fmt = QFileInfo(out).suffix().toLower();

    DiagramView view;
    view.resize(1180, 820);
    view.show();
    view.setSource(npd);

    bool ok = false;
    for (int i = 0; i < 140 && !ok; ++i) {
        QTest::qWait(150);
        ok = view.exportTo(fmt, out);
    }
    std::fprintf(ok ? stdout : stderr, "%s: %s\n",
                 ok ? "rendered" : "FAILED", out.toUtf8().constData());
    return ok ? 0 : 1;
}
