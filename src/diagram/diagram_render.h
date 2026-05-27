// Notepatra Diagram — native-Qt renderer (NO WebEngine).
//
// Renders an Npd::Diagram with Qt's own 2D engine (QPainter), so the diagram
// canvas works in the DEFAULT (Lite) binary on EVERY platform — Linux,
// Windows, macOS Apple Silicon — with zero WebEngine dependency. Replaces the
// old QWebEngineView/dagre.js path (which had no arm64 macOS build).
//
// Pure Qt Widgets (QPainter / QFont / QPainterPath) — no QWebEngine, no qrc.
// Shared by DiagramView (the GUI widget), the npd_render CLI, and tests.
//
// Layout: longest-path layering + barycenter crossing-reduction + centred
// rows. Edges: border-anchored cubic beziers that fan out and bow around
// intervening rows (no overlap). 50+ painter-drawn icons + 5 shapes + palettes.

#pragma once

#include "npd_parser.h"

#include <QColor>
#include <QHash>
#include <QPointF>
#include <QRectF>
#include <QString>

class QPainter;

namespace DiagramRender {

struct Palette { QColor bg, card, card2, border, text, sub, edge, accent, title; };

// "default" | "ocean" | "forest" | "clay" | "mono" (falls back to default).
Palette palette(const QString &name);

struct Layout {
    QHash<QString, QRectF> nodeRects;   // node id -> rect in scene coordinates
    QHash<QString, int>    layer;       // node id -> layer index (for edge routing)
    qreal canvasW = 0;
    qreal canvasH = 0;
};

// Compute node geometry (layered + barycenter, centred rows, title/caption fit).
Layout computeLayout(const Npd::Diagram &d);

// Paint the whole diagram into `painter` (caller fills the background first if
// it wants; this paints title, edges, nodes, caption). Antialiasing is enabled
// internally. `painter` coordinates are scene coordinates (0,0 .. canvasW,H).
void paint(QPainter &painter, const Npd::Diagram &d, const Layout &lay, const Palette &pal);

// Hit-test: id of the node whose rect contains scene-point `p`, else empty.
QString nodeAt(const Npd::Diagram &d, const Layout &lay, QPointF p);

// Draw one named icon (with alias resolution) into `box`. Exposed for reuse/tests.
void drawIcon(QPainter &painter, const QString &name, const QRectF &box, const QColor &color);

} // namespace DiagramRender
