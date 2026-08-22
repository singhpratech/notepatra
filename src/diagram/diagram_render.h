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
//
// House style (v0.1.120): flat engineer's diagrams — no gradients, no drop
// shadows. Card fill, a 1.5 px border, 8 px corners, one accent hue kept for
// meaning. `direction LR` lays the graph out transposed. Groups, notes and a
// legend are geometry too, so they live in Layout and are painted here.

#pragma once

#include "npd_parser.h"

#include <QColor>
#include <QHash>
#include <QPointF>
#include <QRectF>
#include <QString>
#include <QVector>

class QPainter;

namespace DiagramRender {

struct Palette { QColor bg, card, card2, border, text, sub, edge, accent, title; };

// "default" | "ocean" | "forest" | "clay" | "mono" (dark) and the LIGHT
// "paper" | "slate" (falls back to default). "auto" resolves to the dark
// default here — use the overload below where the host theme is known.
Palette palette(const QString &name);

// Same, but resolves the pure-parser token "auto" against the host widget's
// theme: paper when the host is light, default when it is dark. Every other
// name renders exactly as the single-argument overload.
Palette palette(const QString &name, bool hostIsDark);

struct Layout {
    QHash<QString, QRectF> nodeRects;   // node id -> rect in scene coordinates
    QHash<QString, int>    layer;       // node id -> layer index (for edge routing)
    QVector<QRectF>        groupRects;  // parallel to Diagram::groups (null ⇒ skip)
    QVector<QRectF>        noteRects;   // parallel to Diagram::notes  (null ⇒ skip)
    QRectF                 legendRect;  // empty unless the diagram has legend rows
    bool  lr = false;                   // true ⇒ laid out left-to-right
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
