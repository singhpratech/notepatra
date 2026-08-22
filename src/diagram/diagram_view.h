// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef NOTEPATRA_DIAGRAM_VIEW_H
#define NOTEPATRA_DIAGRAM_VIEW_H

// ═══════════════════════════════════════════════════════════════════════
// DiagramView — native-Qt host widget for Notepatra's .npd diagram preview.
//
// .npd text is the source of truth (project memory "diagramming-tool-direction");
// Npd::parse() builds the model and DiagramRender (diagram_render.*) paints it
// with Qt's own 2D engine — NO WebEngine. This means the diagram renders in the
// DEFAULT (Lite) binary on EVERY platform, including macOS Apple Silicon, with
// no 95 MB Chromium and no Full/Lite split. (Replaced the QWebEngineView/dagre
// path in v0.1.103, which had no arm64 macOS build.)
//
// Interaction: scroll to zoom (toward cursor), drag to pan, double-click to fit.
// Hovering a node with `:: "detail"` shows the detail as a tooltip.
// Export: PNG / WebP / JPEG (QImage), SVG (QSvgGenerator), PDF (QPdfWriter),
// HTML (inline SVG). All native — works in every build.
// ═══════════════════════════════════════════════════════════════════════

#include "diagram_render.h"
#include "npd_parser.h"

#include <QString>
#include <QStringList>
#include <QPointF>
#include <QWidget>

class DiagramView : public QWidget {
    Q_OBJECT
public:
    explicit DiagramView(QWidget *parent = nullptr);
    ~DiagramView() override;

    // Parse .npd text → model → layout → repaint. Emits renderError on a
    // parse-with-errors (still renders what it can).
    void setSource(const QString &npdText);

    // Export the rendered diagram to `path`.
    //   format ∈ {"png","webp","jpeg","svg","pdf","html"} (case-insensitive).
    // Returns true on success; emits renderError + returns false otherwise.
    bool exportTo(const QString &format, const QString &path);

    // Formats this build can actually produce (PNG/JPEG/PDF always; WebP only
    // if the qwebp image plugin is present; SVG/HTML only if Qt Svg is linked).
    // The editor builds its Export menu from this so it never offers a format
    // that would fail at write time.
    static QStringList supportedExportFormats();

signals:
    void renderError(const QString &msg);

protected:
    void paintEvent(QPaintEvent *) override;
    void wheelEvent(QWheelEvent *) override;
    void mousePressEvent(QMouseEvent *) override;
    void mouseMoveEvent(QMouseEvent *) override;
    void mouseReleaseEvent(QMouseEvent *) override;
    void mouseDoubleClickEvent(QMouseEvent *) override;

private:
    // True when the host widget's window colour is dark — decides what the
    // `palette auto` token resolves to (paper when light, default when dark).
    bool hostIsDark() const;
    void fitToView();
    QPointF widgetToScene(const QPointF &w) const;

    Npd::Diagram          m_diag;
    DiagramRender::Layout m_lay;
    DiagramRender::Palette m_pal;
    bool   m_have = false;

    qreal   m_zoom = 1.0;
    QPointF m_pan;           // widget-px translation of scene origin
    bool    m_dragging = false;
    QPoint  m_lastPos;
};

#endif // NOTEPATRA_DIAGRAM_VIEW_H
