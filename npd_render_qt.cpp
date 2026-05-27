// SPDX-License-Identifier: GPL-3.0-or-later
//
// npd_render_qt — offscreen .npd → image/doc via the native-Qt renderer (NO
// WebEngine). Output format is taken from the out-file extension and routed
// through DiagramView::exportTo, so it exercises the exact app export path.
//   npd_render_qt <in.npd> <out.{png|jpeg|webp|svg|pdf|html}>

#include "diagram/npd_parser.h"
#include "diagram/diagram_view.h"

#include <QApplication>
#include <QFile>
#include <QFileInfo>
#include <cstdio>

int main(int argc, char **argv){
    QApplication app(argc,argv);
    if (argc<3){ std::fprintf(stderr,"usage: npd_render <in.npd> <out.{png|jpeg|webp|svg|pdf|html}>\n"); return 2; }
    QFile in(argv[1]); if(!in.open(QIODevice::ReadOnly)){ std::fprintf(stderr,"cannot read %s\n",argv[1]); return 2; }

    DiagramView view;
    view.resize(1000, 800);
    view.setSource(QString::fromUtf8(in.readAll()));

    const QString out = QString::fromUtf8(argv[2]);
    QString fmt = QFileInfo(out).suffix().toLower();
    if (fmt.isEmpty()) fmt = "png";
    if (!view.exportTo(fmt, out)) { std::fprintf(stderr,"export failed (%s)\n", fmt.toUtf8().constData()); return 1; }
    std::fprintf(stdout,"exported %s  (%lld bytes)\n", out.toUtf8().constData(), QFileInfo(out).size());
    return 0;
}
