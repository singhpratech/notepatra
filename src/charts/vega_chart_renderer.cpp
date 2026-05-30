// SPDX-License-Identifier: GPL-3.0-or-later

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

#include <QDateTime>
#include <QFrame>
#include <QHBoxLayout>
#include <QRandomGenerator>
#include <QJsonArray>
#include <QJsonDocument>
#include <QLabel>
#include <QLocale>
#include <QPushButton>
#include <QTimer>
#include <QUuid>
#include <QVBoxLayout>

#ifdef NOTEPATRA_WITH_WEBENGINE
#include <QFile>
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

// SECURITY — escape a serialized JSON blob for safe embedding inside an
// HTML <script> block (the HTML-export sink). A model-emitted spec can
// carry a datum/field/title containing "</script><script>alert(1)</script>";
// concatenated raw into the exported .html it would close the script tag and
// execute on open via file://. The in-app render path is NOT affected — there
// the spec is passed as a JS string argument, not concatenated into markup.
//
// We replace the three HTML-significant bytes ('<', '>', '&') plus the line/
// paragraph separators U+2028 (0xE2 0x80 0xA8) and U+2029 (0xE2 0x80 0xA9)
// with their \uXXXX JSON escapes. These remain valid JSON (JSON.parse decodes
// \uXXXX back to the original characters) but no longer contain a literal
// "</script" token or a raw separator that could break the embedding. Note
// Qt5's QJsonDocument::toJson() emits non-ASCII as RAW UTF-8 bytes (a U+2028
// stays as the bytes E2 80 A8, NOT  ), so the 0xE2 branch below is
// LOAD-BEARING — do not remove it. A literal '<' / '>' / '&' can only appear
// inside a string value, which is exactly where the injection lives.
QByteArray escapeJsonForHtmlScript(const QByteArray &json) {
    QByteArray out;
    out.reserve(json.size());
    for (int i = 0; i < json.size(); ++i) {
        const unsigned char c = static_cast<unsigned char>(json.at(i));
        switch (c) {
        case '<': out += "\\u003c"; break;
        case '>': out += "\\u003e"; break;
        case '&': out += "\\u0026"; break;
        case 0xE2:
            // U+2028 / U+2029 are 3-byte UTF-8 sequences (E2 80 A8 / E2 80 A9).
            if (i + 2 < json.size()
                && static_cast<unsigned char>(json.at(i + 1)) == 0x80
                && (static_cast<unsigned char>(json.at(i + 2)) == 0xA8
                    || static_cast<unsigned char>(json.at(i + 2)) == 0xA9)) {
                out += (static_cast<unsigned char>(json.at(i + 2)) == 0xA8)
                           ? "\\u2028"
                           : "\\u2029";
                i += 2;
            } else {
                out += static_cast<char>(c);
            }
            break;
        default:
            out += static_cast<char>(c);
            break;
        }
    }
    return out;
}

#ifdef NOTEPATRA_WITH_WEBENGINE
// v0.1.90 — load vega/vega-lite/vega-embed from a bundled Qt resource
// instead of JSDelivr. Charts now render offline / on air-gapped boxes
// / during CDN outages. The qrc URL is served via WebEngine's
// qrc-scheme handler (built in since Qt 5.6).
//
// renderSpec(specStr) is called from C++ via runJavaScript(). It JSON-
// parses the string, then vegaEmbed's the result into #chart. On success
// it stashes the resulting view object on window so the export API can
// invoke .toImageURL() on it; on failure sets _notepatra_chartError.
//
// v0.1.90 — `{actions: {reset: true, source: false, export: false,
// editor: true}}`: a tiny Vega-embed overlay menu sits in the corner
// with "Reset view" + "Open in Vega Editor" — useful debug shortcuts
// for analysts; PNG/SVG export is driven by the C++ side via toImageURL
// to keep the file-dialog flow native.
const char *kVegaShellHtml = R"HTML(<!DOCTYPE html>
<html>
<head>
<meta charset="utf-8">
<script src="qrc:///vega/vega.min.js"></script>
<script src="qrc:///vega/vega-lite.min.js"></script>
<script src="qrc:///vega/vega-embed.min.js"></script>
<style>
  html, body { margin: 0; padding: 0; background: transparent; }
  body { padding: 6px; font-family: -apple-system, "Segoe UI", sans-serif; }
  #chart { width: 100%; }
  /* Tooltip styling — matches the Notepatra dark chat-panel theme.    */
  /* When light theme is on, the spec.config override wins.            */
  #vg-tooltip-element.vg-tooltip {
    background: rgba(36, 36, 38, 240) !important;
    color: #F0F0F0 !important;
    border: 1px solid #4EC9B0 !important;
    border-radius: 6px !important;
    padding: 6px 10px !important;
    font-size: 11px !important;
    box-shadow: 0 4px 12px rgba(0,0,0,0.35);
  }
</style>
</head>
<body>
<div id="chart"></div>
<script>
  window._notepatra_chartReady = false;
  window._notepatra_chartError = null;
  window._notepatra_view = null;
  window.renderSpec = function(specStr) {
    window._notepatra_chartReady = false;
    window._notepatra_chartError = null;
    window._notepatra_view = null;
    try {
      const spec = JSON.parse(specStr);
      if (typeof vegaEmbed === "undefined") {
        window._notepatra_chartError = "vega-embed failed to load";
        return;
      }
      vegaEmbed('#chart', spec, {
        actions: { reset: true, source: false, export: false, editor: true },
        renderer: 'canvas',
        tooltip: { theme: 'dark' }
      }).then(function(res) {
        window._notepatra_view = res.view;
        window._notepatra_chartReady = true;
      }).catch(function(e) {
        window._notepatra_chartError = String(e);
      });
    } catch (e) {
      window._notepatra_chartError = "JSON parse: " + String(e);
    }
  };
  // Returns a promise-resolving image URL via toImageURL().
  window.exportImage = function(fmt, scale, slot) {
    if (!window._notepatra_view) {
      window['_notepatra_export_' + slot] = '__nover__';
      return;
    }
    window._notepatra_view.toImageURL(fmt, scale || 1).then(function(url) {
      window['_notepatra_export_' + slot] = url;
    }).catch(function(e) {
      window['_notepatra_export_' + slot] = '__err__:' + String(e);
    });
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

    // The base URL governs how `qrc:///vega/...` resolves. Setting it
    // to "qrc:///" rather than "https://localhost/" makes the script
    // tags load from the bundled resource — JSDelivr is no longer in
    // the loop.
    m_view->setHtml(QString::fromUtf8(kVegaShellHtml),
                    QUrl(QStringLiteral("qrc:///")));
}

VegaChartRenderer::~VegaChartRenderer() = default;

void VegaChartRenderer::setSpec(const QJsonObject &vegaLiteSpec) {
    m_lastSpec = vegaLiteSpec;
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
    //
    // Qt5's QJsonDocument has no QJsonValue constructor (only QJsonObject /
    // QJsonArray). Wrap the spec string in a 1-element array, serialize,
    // and slice off the surrounding [ ] — that yields a valid JS string
    // literal regardless of what control characters / quotes / backslashes
    // the Vega-Lite spec contains. This bug crashed the v0.1.63 Linux CI
    // build silently (the buggy line existed in v0.1.63 too but only the
    // WebEngine code path compiles it, and local dev machines without
    // libqt5webengine5-dev installed never tripped it).
    QJsonArray wrap;
    wrap.append(QString::fromUtf8(specJson));
    const QByteArray wrapped = QJsonDocument(wrap).toJson(QJsonDocument::Compact);
    // wrapped looks like: ["...escaped JSON..."]
    // slice off [ and ] to get the bare JS string literal.
    const QString jsArg = QString::fromUtf8(wrapped.mid(1, wrapped.size() - 2));
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
    // Legacy sync API — deprecated in v0.1.90. Use exportPngAsync().
    return QString();
}

bool VegaChartRenderer::isLiteStub() const { return false; }

QJsonObject VegaChartRenderer::currentSpec() const { return m_lastSpec; }

// v0.1.90 — async PNG / SVG export. vega-embed's toImageURL() returns a
// JS Promise; we poll a per-call window slot until it resolves.
namespace {
QString uniqueSlot() {
    return QStringLiteral("s") + QString::number(QDateTime::currentMSecsSinceEpoch())
           + QStringLiteral("_") + QString::number(QRandomGenerator::global()->generate());
}
}

static void pollForExport(QWebEnginePage *page, const QString &slot,
                          int attempts,
                          VegaChartRenderer::ExportCallback cb) {
    if (attempts <= 0) {
        cb(QByteArray());
        return;
    }
    page->runJavaScript(
        QStringLiteral("window['_notepatra_export_%1'] || ''").arg(slot),
        [page, slot, attempts, cb](const QVariant &v) {
            const QString s = v.toString();
            if (s.isEmpty()) {
                QTimer::singleShot(120, page, [page, slot, attempts, cb]() {
                    pollForExport(page, slot, attempts - 1, cb);
                });
                return;
            }
            if (s == QStringLiteral("__nover__") || s.startsWith(QStringLiteral("__err__"))) {
                cb(QByteArray());
                return;
            }
            // Strip "data:image/...;base64,"
            const int comma = s.indexOf(',');
            const QString b64 = (comma > 0) ? s.mid(comma + 1) : s;
            cb(QByteArray::fromBase64(b64.toLatin1()));
            // Clean up the slot to avoid leaking strings in the JS scope.
            page->runJavaScript(QStringLiteral("delete window['_notepatra_export_%1'];").arg(slot));
        });
}

void VegaChartRenderer::exportPngAsync(int scaleFactor, ExportCallback cb) {
    if (!m_view || !m_pageReady) { cb(QByteArray()); return; }
    const QString slot = uniqueSlot();
    const QString js = QStringLiteral("window.exportImage('png', %1, '%2');")
                           .arg(qBound(1, scaleFactor, 6)).arg(slot);
    m_view->page()->runJavaScript(js, [page = m_view->page(), slot, cb](const QVariant &) {
        pollForExport(page, slot, 80, cb);
    });
}

void VegaChartRenderer::exportSvgAsync(ExportCallback cb) {
    if (!m_view || !m_pageReady) { cb(QByteArray()); return; }
    const QString slot = uniqueSlot();
    // Vega's toImageURL("svg") returns a "data:image/svg+xml;..." URL;
    // we decode and unwrap to get raw <svg>...</svg> XML.
    const QString js = QStringLiteral("window.exportImage('svg', 1, '%1');").arg(slot);
    m_view->page()->runJavaScript(js, [page = m_view->page(), slot, cb](const QVariant &) {
        pollForExport(page, slot, 80, [cb](const QByteArray &raw) {
            // Vega's SVG result is URL-encoded; decode percent-escapes first.
            if (raw.isEmpty()) { cb(raw); return; }
            cb(QByteArray::fromPercentEncoding(raw));
        });
    });
}

void VegaChartRenderer::exportHtmlAsync(ExportCallback cb) {
    // Self-contained HTML: embeds vega@5 / vega-lite@5 / vega-embed@6
    // from JSDelivr (works when the user re-opens the saved file in
    // a browser; the saved file is portable). The original page used
    // the qrc:// bundle — that path won't resolve in a file:// context,
    // so the export uses the public CDN for the exported file.
    // SECURITY — escape the serialized spec before embedding it in the
    // <script> block, else a spec value containing "</script><script>…"
    // breaks out and executes on file:// open (stored XSS). escapeJsonForHtmlScript
    // keeps the output valid JSON. See its definition for the threat model.
    const QByteArray specJson = escapeJsonForHtmlScript(
        QJsonDocument(m_lastSpec).toJson(QJsonDocument::Indented));
    QByteArray html;
    html += "<!DOCTYPE html>\n<html><head>\n";
    html += "<meta charset=\"utf-8\">\n";
    html += "<title>Notepatra chart</title>\n";
    html += "<script src=\"https://cdn.jsdelivr.net/npm/vega@5\"></script>\n";
    html += "<script src=\"https://cdn.jsdelivr.net/npm/vega-lite@5\"></script>\n";
    html += "<script src=\"https://cdn.jsdelivr.net/npm/vega-embed@6\"></script>\n";
    html += "<style>body{margin:24px;font-family:system-ui,sans-serif;background:#fafafa;}";
    html += "#chart{max-width:1100px;margin:0 auto;}h1{font-size:18px;font-weight:600;}</style>\n";
    html += "</head><body>\n";
    html += "<div id=\"chart\"></div>\n";
    html += "<script>\nconst spec = ";
    html += specJson;
    html += ";\nvegaEmbed('#chart', spec, {actions: true, renderer: 'canvas'});\n</script>\n";
    html += "</body></html>\n";
    cb(html);
}

void VegaChartRenderer::exportSpecAsync(ExportCallback cb) {
    cb(QJsonDocument(m_lastSpec).toJson(QJsonDocument::Indented));
}

#else // NOTEPATRA_WITH_WEBENGINE

// ─────────────────────────────────────────────────────────────────────
// Lite-mode stub — "Charts Pack required" card.
//
// Composition:
//   ┌─────────────────────────────────────────────────────┐
//   │  Inline Vega-Lite charts need the Full build        │
//   │  ───────────────────────────────────────────────    │
//   │  Renders Vega-Lite charts (bar / line / scatter /   │
//   │  area / composite) inline in the chat transcript.   │
//   │                                                     │
//   │  Linux / Windows Full build — swap your Lite        │
//   │  binary for Full to render inline charts            │
//   │  (macOS Full is DuckDB-only: use View JSON)         │
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

    auto *title = new QLabel(
        QStringLiteral("Inline Vega-Lite charts need the Full build (Linux / Windows)"), card);
    title->setStyleSheet(QString("color: %1; font-size: 13px; font-weight: 600;")
                             .arg(text.name()));
    title->setWordWrap(true);
    cardLay->addWidget(title);

    auto *desc = new QLabel(
        NotepatraPlugins::packDescription(NotepatraPlugins::kChartsPack), card);
    desc->setWordWrap(true);
    desc->setStyleSheet(QString("color: %1; font-size: 11px;").arg(text.name()));
    cardLay->addWidget(desc);

#if defined(Q_OS_MACOS)
    auto *meta = new QLabel(
        QStringLiteral("Inline Vega-Lite charts aren't available on macOS "
                       "(no Apple-Silicon Qt5 WebEngine). Use View JSON, or a "
                       "native ```chart block — those render on every platform."),
        card);
#else
    auto *meta = new QLabel(
        QStringLiteral("Available in the Full build (Linux & Windows) — swap "
                       "your Lite binary for the Full download to render charts."),
        card);
#endif
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
        if (!m_lastSpec.isEmpty()) {
            emit viewJsonRequested(m_lastSpec);
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
    m_lastSpec = vegaLiteSpec;
    if (m_viewJsonBtn && !vegaLiteSpec.isEmpty()) {
        m_viewJsonBtn->setEnabled(true);
    }
    // Intentionally NOT emitting renderError — the lite-mode card is
    // not an error state, it's a missing-feature state. AIPanel checks
    // isLiteStub() before rendering the muted "Chart error: ..." row.
}

QString VegaChartRenderer::exportPng(int /*scaleFactor*/) { return QString(); }

bool VegaChartRenderer::isLiteStub() const { return true; }

QJsonObject VegaChartRenderer::currentSpec() const { return m_lastSpec; }

// Lite-mode stubs — the chart never rendered, so PNG/SVG are unavailable.
// Spec / HTML still work (they're driven by the stashed spec).
void VegaChartRenderer::exportPngAsync(int /*scaleFactor*/, ExportCallback cb) {
    cb(QByteArray());
}
void VegaChartRenderer::exportSvgAsync(ExportCallback cb) {
    cb(QByteArray());
}
void VegaChartRenderer::exportHtmlAsync(ExportCallback cb) {
    // SECURITY — same XSS escaping as the WebEngine export path above: a spec
    // value containing "</script><script>…" must not break out of the embed
    // <script> block when the exported .html is opened via file://.
    const QByteArray specJson = escapeJsonForHtmlScript(
        QJsonDocument(m_lastSpec).toJson(QJsonDocument::Indented));
    QByteArray html;
    html += "<!DOCTYPE html>\n<html><head>\n";
    html += "<meta charset=\"utf-8\"><title>Notepatra chart</title>\n";
    html += "<script src=\"https://cdn.jsdelivr.net/npm/vega@5\"></script>\n";
    html += "<script src=\"https://cdn.jsdelivr.net/npm/vega-lite@5\"></script>\n";
    html += "<script src=\"https://cdn.jsdelivr.net/npm/vega-embed@6\"></script>\n";
    html += "</head><body><div id=\"chart\"></div>\n";
    html += "<script>vegaEmbed('#chart', ";
    html += specJson;
    html += ", {actions: true});</script></body></html>\n";
    cb(html);
}
void VegaChartRenderer::exportSpecAsync(ExportCallback cb) {
    cb(QJsonDocument(m_lastSpec).toJson(QJsonDocument::Indented));
}

#endif // NOTEPATRA_WITH_WEBENGINE
