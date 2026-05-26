// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef NOTEPATRA_DIAGRAM_VIEW_H
#define NOTEPATRA_DIAGRAM_VIEW_H

// ═══════════════════════════════════════════════════════════════════════
// DiagramView — the C++ host widget for Notepatra's .npd diagram preview.
//
// .npd text is the source of truth (see project memory
// "diagramming-tool-direction"); Npd::parse() + Npd::toGraphJson() turn it
// into the JSON graph the JS render layer consumes. This widget owns the
// rendering surface and the export pipeline.
//
// Two-flavor build, mirroring src/charts/vega_chart_renderer.{h,cpp}:
//
//   • NOTEPATRA_WITH_WEBENGINE defined (Full build) — hosts a real
//     QWebEngineView loading qrc:///diagram/diagram.html. setSource()
//     pushes the graph JSON to window.npdRender(json); exportTo() drives
//     the page's async export hooks (window._npd_export_png/webp/jpeg/svg)
//     plus QWebEnginePage::printToPdf for PDF and page->toHtml for HTML.
//
//   • NOTEPATRA_WITH_WEBENGINE undefined (Lite build) — a lightweight stub
//     with the SAME public API. setSource() paints a QLabel parse summary
//     ("<n> nodes, <m> edges — preview needs the Full build"); exportTo()
//     returns false and emits renderError. Keeps the bare binary linking
//     with zero WebEngine headers / link deps.
//
// The public API below is identical in both #ifdef arms — callers compile
// against one header and never branch on the flavor.
// ═══════════════════════════════════════════════════════════════════════

#include <QString>
#include <QWidget>

QT_BEGIN_NAMESPACE
class QVBoxLayout;
class QWebEngineView;
class QLabel;
QT_END_NAMESPACE

class DiagramView : public QWidget {
    Q_OBJECT
public:
    explicit DiagramView(QWidget *parent = nullptr);
    ~DiagramView() override;

    // Parse .npd text → JSON graph → render.
    //   Full build: QJsonDocument(Npd::toGraphJson(Npd::parse(npdText)))
    //               serialized Compact and pushed via
    //               runJavaScript("npdRender(<json>)"). Queued until the
    //               page's loadFinished fires if not yet loaded.
    //   Lite build: shows a "Diagram preview needs the Full build" label
    //               with the node/edge count from the parse.
    void setSource(const QString &npdText);

    // Export the currently-rendered diagram to `path`.
    //   format ∈ {"png","webp","jpeg","svg","pdf","html"} (case-insensitive).
    // Returns true on success.
    //   Full build: raster (png/webp/jpeg) + svg use the async window-slot
    //               export hooks, bridged to a synchronous result via a
    //               local QEventLoop + hard timeout; pdf uses
    //               QWebEnginePage::printToPdf; html writes a standalone
    //               copy of the page with the graph JSON embedded.
    //   Lite build: always returns false and emits renderError.
    bool exportTo(const QString &format, const QString &path);

signals:
    // Emitted on parse-with-errors, engine bring-up failure, export
    // failure, or export-before-load. The host surfaces the message.
    void renderError(const QString &msg);

private:
#ifdef NOTEPATRA_WITH_WEBENGINE
    QWebEngineView *m_view = nullptr;
    bool m_pageReady = false;
    QString m_pendingNpd;   // queued source pushed once the page loads
    bool m_hasPending = false;

    // Build the graph-JSON string literal and call npdRender on the page.
    void pushSource(const QString &npdText);
    // Async raster/svg export bridged to a blocking bool result.
    bool exportImageBlocking(const QString &jsFmt, double scale,
                             bool svg, const QString &path);
    bool exportPdfBlocking(const QString &path);
    bool exportHtmlBlocking(const QString &path);
#else
    QLabel *m_label = nullptr;   // parse-summary placeholder
#endif
};

#endif // NOTEPATRA_DIAGRAM_VIEW_H
