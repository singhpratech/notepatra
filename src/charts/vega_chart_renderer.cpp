// ═══════════════════════════════════════════════════════════════════════
// v0.1.63 — VegaChartRenderer implementation.
//
// Two-code-path file: when built with `-DNOTEPATRA_WITH_WEBENGINE=ON` we
// pull in QWebEngineView and load the vega-embed shell from JSDelivr.
// Otherwise we fall back to a single QLabel that tells the user how to
// rebuild with WebEngine. Keeps the binary buildable on systems that
// don't have libqt5webengine5 installed (e.g. minimal CI containers,
// distros that ship a slimmed-down Qt5).
// ═══════════════════════════════════════════════════════════════════════

#include "charts/vega_chart_renderer.h"

#include <QJsonDocument>
#include <QLabel>
#include <QUuid>
#include <QVBoxLayout>

#ifdef NOTEPATRA_WITH_WEBENGINE
#include <QWebEngineView>
#include <QWebEnginePage>
#include <QWebEngineSettings>
#endif

namespace {

// Synthesize a short, stable chart id. The leading "chart-" makes it
// trivial to grep tool-call payloads in the chat transcript for
// debugging. We trim to 12 chars of the UUID hex so the id is short
// enough to land cleanly in a tool-result JSON body without bloating
// the model's context window.
QString synthChartId() {
    const QString uuid = QUuid::createUuid().toString(QUuid::Id128);
    return QStringLiteral("chart-") + uuid.left(12);
}

#ifdef NOTEPATRA_WITH_WEBENGINE
// Inline HTML+JS shell. Loads vega-embed from JSDelivr (BSD-3 — compatible
// with our GPLv3). v0.1.64 polish: bundle these as a Qt resource so charts
// work offline. The body { margin:0 } reset prevents the default WebEngine
// 8px margin from clipping the chart against the card border.
//
// renderSpec(specStr) is called from C++ via runJavaScript(). It JSON-
// parses the string, then vegaEmbed's the result into #chart. On success
// it sets window._notepatra_chartReady = true; on failure it sets
// window._notepatra_chartError to the message. The C++ side polls those
// flags via a follow-up runJavaScript() to drive the renderReady() /
// renderError() signals. Avoids the extra round-trip of wiring QWebChannel
// just to surface one bit of state.
const char *kVegaShellHtml = R"HTML(<!DOCTYPE html>
<html>
<head>
<meta charset="utf-8">
<script src="https://cdn.jsdelivr.net/npm/vega@5"></script>
<script src="https://cdn.jsdelivr.net/npm/vega-lite@5"></script>
<script src="https://cdn.jsdelivr.net/npm/vega-embed@6"></script>
<style>
  html, body { margin: 0; padding: 0; background: transparent; }
  body { padding: 8px; font-family: -apple-system, "Segoe UI", sans-serif; }
  #chart { width: 100%; }
</style>
</head>
<body>
<div id="chart"></div>
<script>
  window._notepatra_chartReady = false;
  window._notepatra_chartError = null;
  window.renderSpec = function(specStr) {
    window._notepatra_chartReady = false;
    window._notepatra_chartError = null;
    try {
      const spec = JSON.parse(specStr);
      if (typeof vegaEmbed === "undefined") {
        window._notepatra_chartError = "vega-embed failed to load (offline?)";
        return;
      }
      vegaEmbed('#chart', spec, {actions: false}).then(function() {
        window._notepatra_chartReady = true;
      }).catch(function(e) {
        window._notepatra_chartError = String(e);
      });
    } catch (e) {
      window._notepatra_chartError = "JSON parse: " + String(e);
    }
  };
</script>
</body>
</html>)HTML";
#endif // NOTEPATRA_WITH_WEBENGINE

} // namespace

// ─────────────────────────────────────────────────────────────────────
// WebEngine path
// ─────────────────────────────────────────────────────────────────────

#ifdef NOTEPATRA_WITH_WEBENGINE

VegaChartRenderer::VegaChartRenderer(QWidget *parent)
    : QWidget(parent),
      m_chartId(synthChartId()) {
    auto *lay = new QVBoxLayout(this);
    lay->setContentsMargins(0, 0, 0, 0);
    lay->setSpacing(0);

    m_view = new QWebEngineView(this);
    // Transparent background so the chart card's QSS shows through; the
    // shell's body { background: transparent } cooperates.
    m_view->page()->setBackgroundColor(Qt::transparent);
    // Sensible minimum so the chart isn't collapsed inside QScrollArea's
    // layout pass. Vega-Lite specs without explicit width auto-fit to
    // the container.
    m_view->setMinimumHeight(280);
    lay->addWidget(m_view);

    // Inject the shell HTML once. setSpec() then drives renderSpec() via
    // runJavaScript() — way cheaper than reloading the page per call.
    connect(m_view, &QWebEngineView::loadFinished, this, [this](bool ok) {
        m_pageReady = ok;
        if (!ok) {
            emit renderError(QStringLiteral("WebEngine page failed to load"));
            return;
        }
        if (!m_pendingSpec.isEmpty()) {
            setSpec(m_pendingSpec);
            m_pendingSpec = QJsonObject();
        }
    });

    m_view->setHtml(QString::fromUtf8(kVegaShellHtml),
                    QUrl(QStringLiteral("https://localhost/")));
}

VegaChartRenderer::~VegaChartRenderer() = default;

void VegaChartRenderer::setSpec(const QJsonObject &vegaLiteSpec) {
    if (!m_pageReady) {
        // Stash the spec — the loadFinished slot replays it.
        m_pendingSpec = vegaLiteSpec;
        return;
    }
    if (!m_view) {
        emit renderError(QStringLiteral("Web view not initialised"));
        return;
    }
    const QByteArray specJson = QJsonDocument(vegaLiteSpec).toJson(QJsonDocument::Compact);
    // JSON-encode the JSON string so it embeds safely as a JS string
    // literal — handles every escape Vega-Lite specs can contain.
    QString jsArg = QString::fromUtf8(
        QJsonDocument(QJsonValue(QString::fromUtf8(specJson))).toJson(QJsonDocument::Compact));
    const QString js = QStringLiteral("window.renderSpec(%1);").arg(jsArg);
    m_view->page()->runJavaScript(js, [this](const QVariant &) {
        // Schedule a one-shot poll for ready/error. vega-embed is async
        // (it pulls data + compiles the spec), so we can't read the
        // flags immediately. A short delay is empirically enough for
        // simple specs; complex specs continue to refine via timed polls
        // until v0.1.64 ships QWebChannel-based callbacks.
        QMetaObject::invokeMethod(this, [this]() { emitWhenReady(); },
                                  Qt::QueuedConnection);
    });
}

void VegaChartRenderer::emitWhenReady() {
    if (!m_view) return;
    m_view->page()->runJavaScript(
        QStringLiteral(
            "JSON.stringify({r: !!window._notepatra_chartReady, "
            "e: window._notepatra_chartError || null})"),
        [this](const QVariant &v) {
            const QJsonDocument jd = QJsonDocument::fromJson(v.toString().toUtf8());
            if (!jd.isObject()) return;
            const QJsonObject o = jd.object();
            const QString err = o.value("e").toString();
            if (!err.isEmpty()) {
                emit renderError(err);
                return;
            }
            if (o.value("r").toBool()) {
                emit renderReady();
            }
        });
}

QString VegaChartRenderer::exportPng(int /*scaleFactor*/) {
    // v0.1.64 — wire vega-embed.toImageURL() through here. For now the
    // pptx writer + "Save chart as PNG" menu both stub out.
    return QString();
}

#else // NOTEPATRA_WITH_WEBENGINE

// ─────────────────────────────────────────────────────────────────────
// Stub path — chart support compiled out.
// ─────────────────────────────────────────────────────────────────────

VegaChartRenderer::VegaChartRenderer(QWidget *parent)
    : QWidget(parent),
      m_chartId(synthChartId()) {
    auto *lay = new QVBoxLayout(this);
    lay->setContentsMargins(8, 8, 8, 8);
    lay->setSpacing(0);
    m_stubLabel = new QLabel(
        QStringLiteral("(Charts require QtWebEngine — rebuild with "
                       "-DNOTEPATRA_WITH_WEBENGINE=ON)"),
        this);
    m_stubLabel->setWordWrap(true);
    m_stubLabel->setStyleSheet(
        QStringLiteral("color: #c0392b; font-style: italic; font-size: 11px;"));
    lay->addWidget(m_stubLabel);
    setMinimumHeight(60);
}

VegaChartRenderer::~VegaChartRenderer() = default;

void VegaChartRenderer::setSpec(const QJsonObject & /*vegaLiteSpec*/) {
    // No-op in the stub build — emit error so the agent loop surfaces a
    // muted message under the placeholder.
    emit renderError(
        QStringLiteral("WebEngine support disabled at build time"));
}

QString VegaChartRenderer::exportPng(int /*scaleFactor*/) { return QString(); }

#endif // NOTEPATRA_WITH_WEBENGINE
