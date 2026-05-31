// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef NOTEPATRA_VEGA_CHART_RENDERER_H
#define NOTEPATRA_VEGA_CHART_RENDERER_H

// ═══════════════════════════════════════════════════════════════════════
// v0.1.63 — Vega-Lite chart renderer (Data Analyst scaffold).
//
// The AI emits a Vega-Lite JSON spec via the `generate_chart` tool. This
// widget embeds a QWebEngineView, loads a tiny HTML+JS shell that imports
// vega + vega-lite + vega-embed bundled via qrc (qrc:///vega/, no CDN),
// and renders the spec inline in the AI chat transcript.
//
// v0.1.64 — Lite mode by default. The bare-binary build flips
// NOTEPATRA_WITH_WEBENGINE OFF, in which case setSpec() does NOT render
// a chart; instead the widget paints an upgrade card with two actions:
// [Install charts pack] (opens the Releases page so the user can swap in
// the Full binary; no in-app auto-installer ships yet) and [View JSON
// instead] (emits viewJsonRequested(spec) so the host can swap in a code
// block). This keeps the bare binary ~11.5 MB while giving users a clean
// upgrade path. Inline Vega is a Linux/Windows Full feature; macOS Full is
// DuckDB-only (no Apple-Silicon Qt5 WebEngine) — the native ```chart
// renderer (QtCharts) still works on every platform and in both flavors.
//
// Library choice — Vega-Lite via QtWebEngine. Reason: AI emission
// ergonomics. Vega-Lite specs are short, flat, schema-validatable;
// LLMs emit them cleanly compared to QtCharts' imperative axis/series
// API.
// ═══════════════════════════════════════════════════════════════════════

#include <QJsonObject>
#include <QString>
#include <QWidget>
#include <functional>

QT_BEGIN_NAMESPACE
class QVBoxLayout;
class QWebEngineView;
class QLabel;
class QPushButton;
QT_END_NAMESPACE

class VegaChartRenderer : public QWidget {
    Q_OBJECT
public:
    explicit VegaChartRenderer(QWidget *parent = nullptr);
    ~VegaChartRenderer() override;

    // Feed a Vega-Lite spec object. With WebEngine ON, the widget (re)loads
    // the JS shell and calls vegaEmbed('#chart', spec). With WebEngine OFF
    // (lite-mode build) the spec is stashed for [View JSON instead] but
    // not rendered.
    void setSpec(const QJsonObject &vegaLiteSpec);

    // Stable identifier — synthesized at construction time. Round-trips
    // through the generate_chart tool result so MainWindow / future
    // "explain this chart" modal can correlate.
    QString chartId() const { return m_chartId; }

    // v0.1.64 hook — export the rendered chart as a base64 PNG. The
    // scaffold returns "" (export wiring lands with the pptx writer).
    QString exportPng(int scaleFactor = 2);

    // True iff this is the lite-mode stub variant (no WebEngine).
    // The host (AIPanel) uses this to decide whether to forgo the
    // surrounding chartCard title row.
    bool isLiteStub() const;

    // v0.1.90 — async exports. Each completion callback fires on the
    // main thread once vega-embed has produced the data; cb receives an
    // empty QByteArray on failure (renderError is emitted with detail).
    //  • PNG: rasterized at `scaleFactor` (1=1x, 2=2x, 4=4x).
    //  • SVG: vector. Tooltips work when opened in a browser (Vega
    //    embeds <title> elements + listens for hover).
    //  • HTML: self-contained doc, full Vega interactivity (hover,
    //    zoom, pan, brush). Vega libraries served from local CDN.
    //  • Spec JSON: pretty-printed Vega-Lite v5 spec — paste into
    //    vega-editor.netlify.app to fork the chart.
    using ExportCallback = std::function<void(const QByteArray &)>;
    void exportPngAsync(int scaleFactor, ExportCallback cb);
    void exportSvgAsync(ExportCallback cb);
    void exportHtmlAsync(ExportCallback cb);
    void exportSpecAsync(ExportCallback cb);

    // The Vega-Lite spec last fed to setSpec(). Used by the HTML export
    // path and as a fallback for spec download.
    QJsonObject currentSpec() const;

signals:
    // Fires once the chart has painted. UI layers use this to size the
    // surrounding card or fade out a loading shimmer.
    void renderReady();

    // Fires on JSON-parse / vega-runtime / engine-bring-up failures. The
    // chat surface listens and surfaces the message as a muted error row
    // under the chart card.
    void renderError(const QString &message);

    // v0.1.64 — emitted when the user clicks [Install charts pack] on the
    // lite-mode "Charts Pack required" card. AIPanel listens and opens
    // the install instructions dialog. Never emitted in the WebEngine
    // build path.
    void installRequested();

    // v0.1.64 — emitted when the user clicks [View JSON instead] on the
    // lite-mode card. The argument is the stashed spec. AIPanel listens
    // and appends a fenced-JSON code block under the card.
    void viewJsonRequested(const QJsonObject &spec);

private:
    QString m_chartId;
    QJsonObject m_lastSpec;  // both paths track this for currentSpec()
#ifdef NOTEPATRA_WITH_WEBENGINE
    QWebEngineView *m_view = nullptr;
    QJsonObject m_pendingSpec;
    bool m_pageReady = false;
    void emitWhenReady();
#else
    // Lite-mode stub state. The card is built once; setSpec() just
    // stashes the spec for [View JSON instead].
    QPushButton *m_viewJsonBtn = nullptr;
    void buildLiteStubCard();
#endif
};

#endif // NOTEPATRA_VEGA_CHART_RENDERER_H
