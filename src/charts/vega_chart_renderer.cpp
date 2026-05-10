// ═══════════════════════════════════════════════════════════════════════
// v0.1.63 — VegaChartRenderer implementation.
//
// Two-code-path file: when built with `-DNOTEPATRA_WITH_WEBENGINE=ON` we
// pull in QWebEngineView and load the vega-embed shell from JSDelivr.
//
// v0.1.64 — Lite-mode default. The stub branch renders a proper
// "Charts Pack required" card with [Install charts pack] / [View JSON
// instead] buttons. Kept inside this same file (rather than splitting to
// `vega_chart_stub.cpp`) so the CMake gating stays simple — one
// translation unit, one #ifdef.
// ═══════════════════════════════════════════════════════════════════════

#include "charts/vega_chart_renderer.h"
#include "plugin_loader.h"

#include <QFrame>
#include <QHBoxLayout>
#include <QJsonDocument>
#include <QLabel>
#include <QLocale>
#include <QPushButton>
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
// with our GPLv3). v0.1.65 polish: bundle these as a Qt resource so charts
// work offline. The body { margin:0 } reset prevents the default WebEngine
// 8px margin from clipping the chart against the card border.
//
// renderSpec(specStr) is called from C++ via runJavaScript(). It JSON-
// parses the string, then vegaEmbed's the result into #chart. On success
// it sets window._notepatra_chartReady = true; on failure it sets
// window._notepatra_chartError to the message.
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
    // v0.1.65 — wire vega-embed.toImageURL() through here.
    return QString();
}

bool VegaChartRenderer::isLiteStub() const { return false; }

#else // NOTEPATRA_WITH_WEBENGINE

// ─────────────────────────────────────────────────────────────────────
// Lite-mode stub — "Charts Pack required" card.
//
// Composition:
//   ┌─────────────────────────────────────────────────────┐
//   │  📊 Chart rendering requires the Charts Pack        │
//   │  ───────────────────────────────────────────────    │
//   │  Renders Vega-Lite charts (bar / line / scatter /   │
//   │  area / composite) inline in the chat transcript.   │
//   │                                                     │
//   │  ≈ 95 MB · One-time download · Auto-installs on     │
//   │  first use                                          │
//   │                                                     │
//   │  [ Install charts pack ]   [ View JSON instead ]    │
//   └─────────────────────────────────────────────────────┘
//
// Theme-aware: pulls colours from QPalette so the card respects light /
// dark / custom themes without hard-coded greys. Buttons use a single
// stylesheet pair (primary blue + outline) so they're visible against
// both backgrounds.
// ─────────────────────────────────────────────────────────────────────

VegaChartRenderer::VegaChartRenderer(QWidget *parent)
    : QWidget(parent),
      m_chartId(synthChartId()) {
    buildLiteStubCard();
}

VegaChartRenderer::~VegaChartRenderer() = default;

void VegaChartRenderer::buildLiteStubCard() {
    auto *outer = new QVBoxLayout(this);
    outer->setContentsMargins(0, 0, 0, 0);
    outer->setSpacing(0);

    auto *card = new QFrame(this);
    card->setObjectName("pluginRequiredCard");
    // Theme-aware: palette() reflects the active app palette. We pull
    // base() for the card background (matches the chat content surface)
    // and windowText() for the body copy.
    const QColor base   = palette().base().color();
    const QColor border = palette().mid().color();
    const QColor text   = palette().windowText().color();
    const QColor muted  = palette().placeholderText().color().isValid()
                              ? palette().placeholderText().color()
                              : palette().windowText().color();
    card->setStyleSheet(QString(
        "#pluginRequiredCard { background: %1; border: 1px dashed %2; "
        "border-radius: 8px; }")
        .arg(base.name(QColor::HexArgb), border.name()));
    auto *cardLay = new QVBoxLayout(card);
    cardLay->setContentsMargins(14, 12, 14, 12);
    cardLay->setSpacing(8);

    auto *title = new QLabel(QStringLiteral("📊 Chart rendering requires the Charts Pack"), card);
    title->setStyleSheet(QString("color: %1; font-size: 13px; font-weight: 600;")
                             .arg(text.name()));
    title->setWordWrap(true);
    cardLay->addWidget(title);

    auto *desc = new QLabel(
        NotepatraPlugins::packDescription(NotepatraPlugins::kChartsPack), card);
    desc->setWordWrap(true);
    desc->setStyleSheet(QString("color: %1; font-size: 11px;").arg(text.name()));
    cardLay->addWidget(desc);

    const qint64 bytes = NotepatraPlugins::approximateDownloadSize(NotepatraPlugins::kChartsPack);
    const QString sizeText = QLocale().formattedDataSize(bytes, 0, QLocale::DataSizeIecFormat);
    auto *meta = new QLabel(
        QStringLiteral("≈ %1 · One-time download · Auto-installs on first use")
            .arg(sizeText),
        card);
    meta->setWordWrap(true);
    meta->setStyleSheet(QString("color: %1; font-size: 10px; font-style: italic;")
                            .arg(muted.name()));
    cardLay->addWidget(meta);

    auto *btnRow = new QHBoxLayout;
    btnRow->setContentsMargins(0, 4, 0, 0);
    btnRow->setSpacing(8);

    auto *installBtn = new QPushButton(QStringLiteral("Install charts pack"), card);
    installBtn->setCursor(Qt::PointingHandCursor);
    installBtn->setStyleSheet(
        "QPushButton { background: #2563eb; color: white; border: none; "
        "padding: 6px 14px; border-radius: 4px; font-weight: 600; } "
        "QPushButton:hover { background: #1d4ed8; } "
        "QPushButton:pressed { background: #1e40af; }");
    connect(installBtn, &QPushButton::clicked, this,
            [this]() { emit installRequested(); });
    btnRow->addWidget(installBtn);

    m_viewJsonBtn = new QPushButton(QStringLiteral("View JSON instead"), card);
    m_viewJsonBtn->setCursor(Qt::PointingHandCursor);
    m_viewJsonBtn->setStyleSheet(QString(
        "QPushButton { background: transparent; color: %1; "
        "border: 1px solid %2; padding: 6px 14px; border-radius: 4px; } "
        "QPushButton:hover { border-color: %3; } "
        "QPushButton:disabled { color: %4; border-color: %4; }")
        .arg(text.name(), border.name(), text.name(), muted.name()));
    m_viewJsonBtn->setEnabled(false);  // enabled once setSpec() fires
    connect(m_viewJsonBtn, &QPushButton::clicked, this, [this]() {
        if (!m_stashedSpec.isEmpty()) {
            emit viewJsonRequested(m_stashedSpec);
            m_viewJsonBtn->setEnabled(false);  // one-shot
            m_viewJsonBtn->setText(QStringLiteral("JSON shown below"));
        }
    });
    btnRow->addWidget(m_viewJsonBtn);

    btnRow->addStretch();
    cardLay->addLayout(btnRow);

    outer->addWidget(card);
    setMinimumHeight(140);
}

void VegaChartRenderer::setSpec(const QJsonObject &vegaLiteSpec) {
    m_stashedSpec = vegaLiteSpec;
    if (m_viewJsonBtn && !vegaLiteSpec.isEmpty()) {
        m_viewJsonBtn->setEnabled(true);
    }
    // Intentionally NOT emitting renderError — the lite-mode card is
    // not an error state, it's a missing-feature state. AIPanel checks
    // isLiteStub() before rendering the muted "Chart error: ..." row.
}

QString VegaChartRenderer::exportPng(int /*scaleFactor*/) { return QString(); }

bool VegaChartRenderer::isLiteStub() const { return true; }

#endif // NOTEPATRA_WITH_WEBENGINE
