#ifndef NOTEPATRA_VEGA_CHART_RENDERER_H
#define NOTEPATRA_VEGA_CHART_RENDERER_H

// ═══════════════════════════════════════════════════════════════════════
// v0.1.63 — Vega-Lite chart renderer (Data Analyst scaffold).
//
// The AI emits a Vega-Lite JSON spec via the `generate_chart` tool. This
// widget embeds a QWebEngineView, loads a tiny HTML+JS shell that imports
// vega + vega-lite + vega-embed from the public CDN, and renders the spec
// inline in the AI chat transcript. v0.1.64 polish: bundle the JS as a
// Qt resource so charts render offline; this scaffold accepts the CDN
// dependency to ship the foundation cheaply.
//
// Library choice — Vega-Lite via QtWebEngine. Reason: AI emission
// ergonomics. Vega-Lite specs are short, flat, schema-validatable;
// LLMs emit them cleanly compared to QtCharts' imperative axis/series
// API. The runtime cost (≈70 MB QtWebEngine link surface) is one-time
// per binary and we revisit the on-demand plugin architecture in
// v0.1.64 if install-size complaints surface.
//
// Build gating — `NOTEPATRA_WITH_WEBENGINE` (default ON). When OFF the
// .cpp compiles a stub implementation that renders a single QLabel
// pointing the user at the rebuild flag. Lets distros without
// Qt5WebEngineWidgets installed still produce a working notepatra
// binary minus the chart surface.
// ═══════════════════════════════════════════════════════════════════════

#include <QJsonObject>
#include <QString>
#include <QWidget>

QT_BEGIN_NAMESPACE
class QVBoxLayout;
class QWebEngineView;
class QLabel;
QT_END_NAMESPACE

class VegaChartRenderer : public QWidget {
    Q_OBJECT
public:
    explicit VegaChartRenderer(QWidget *parent = nullptr);
    ~VegaChartRenderer() override;

    // Feed a Vega-Lite spec object. The widget will (re)load the JS shell
    // and call vegaEmbed('#chart', spec). Emits renderReady() on success,
    // renderError(message) on failure. Safe to call repeatedly — each call
    // replaces the previous chart.
    void setSpec(const QJsonObject &vegaLiteSpec);

    // Stable identifier — synthesized at construction time. Round-trips
    // through the generate_chart tool result so MainWindow / future
    // "explain this chart" modal can correlate.
    QString chartId() const { return m_chartId; }

    // v0.1.64 hook — export the rendered chart as a base64 PNG. The
    // scaffold returns "" (export wiring lands with the pptx writer).
    QString exportPng(int scaleFactor = 2);

signals:
    // Fires once the chart has painted. UI layers use this to size the
    // surrounding card or fade out a loading shimmer.
    void renderReady();

    // Fires on JSON-parse / vega-runtime / engine-bring-up failures. The
    // chat surface listens and surfaces the message as a muted error row
    // under the chart card.
    void renderError(const QString &message);

private:
    QString m_chartId;
#ifdef NOTEPATRA_WITH_WEBENGINE
    QWebEngineView *m_view = nullptr;
    QJsonObject m_pendingSpec;
    bool m_pageReady = false;
    void emitWhenReady();
#else
    QLabel *m_stubLabel = nullptr;
#endif
};

#endif // NOTEPATRA_VEGA_CHART_RENDERER_H
