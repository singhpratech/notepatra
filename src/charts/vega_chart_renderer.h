#ifndef NOTEPATRA_VEGA_CHART_RENDERER_H
#define NOTEPATRA_VEGA_CHART_RENDERER_H

// ═══════════════════════════════════════════════════════════════════════
// v0.1.63 — Vega-Lite chart renderer (Data Analyst scaffold).
//
// The AI emits a Vega-Lite JSON spec via the `generate_chart` tool. This
// widget embeds a QWebEngineView, loads a tiny HTML+JS shell that imports
// vega + vega-lite + vega-embed from the public CDN, and renders the spec
// inline in the AI chat transcript.
//
// v0.1.64 — Lite mode by default. The bare-binary build flips
// NOTEPATRA_WITH_WEBENGINE OFF, in which case setSpec() does NOT render
// a chart; instead the widget paints a "Charts Pack required" card with
// two actions: [Install charts pack] (currently links to manual-install
// docs, auto-installer ships v0.1.65) and [View JSON instead] (emits
// viewJsonRequested(spec) so the host can swap in a code block). This
// keeps the bare binary ~9 MB while giving users a clean upgrade path.
//
// Library choice — Vega-Lite via QtWebEngine. Reason: AI emission
// ergonomics. Vega-Lite specs are short, flat, schema-validatable;
// LLMs emit them cleanly compared to QtCharts' imperative axis/series
// API.
// ═══════════════════════════════════════════════════════════════════════

#include <QJsonObject>
#include <QString>
#include <QWidget>

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
#ifdef NOTEPATRA_WITH_WEBENGINE
    QWebEngineView *m_view = nullptr;
    QJsonObject m_pendingSpec;
    bool m_pageReady = false;
    void emitWhenReady();
#else
    // Lite-mode stub state. The card is built once; setSpec() just
    // stashes the spec for [View JSON instead].
    QJsonObject m_stashedSpec;
    QPushButton *m_viewJsonBtn = nullptr;
    void buildLiteStubCard();
#endif
};

#endif // NOTEPATRA_VEGA_CHART_RENDERER_H
