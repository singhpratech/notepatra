// SPDX-License-Identifier: GPL-3.0-or-later

// ═══════════════════════════════════════════════════════════════════════
// DiagramView implementation.
//
// Two-code-path file, gated on NOTEPATRA_WITH_WEBENGINE — the same one-
// translation-unit / one-#ifdef shape as src/charts/vega_chart_renderer.cpp.
// When built with -DNOTEPATRA_WITH_WEBENGINE=ON we host a QWebEngineView
// loading qrc:///diagram/diagram.html and drive its window.npdRender /
// window._npd_export_* hooks. Otherwise we compile a lightweight QLabel
// stub with the identical public API so the lite binary still links with
// no WebEngine header or link dependency.
//
// The render layer (resources/diagram/diagram.html + its JS) is built in
// parallel; this file assumes it exists at qrc:///diagram/diagram.html and
// exposes:
//   • window.npdRender(json)         — render a graph from a JSON string
//   • window.npdFit()                — fit the diagram to the viewport
//   • window._npd_export_png(slot, scale)
//   • window._npd_export_webp(slot, scale)
//   • window._npd_export_jpeg(slot, scale)   — set window['_npd_result_'+slot]
//                                              to a data: URL when done
//   • window._npd_export_svg(slot)           — set window['_npd_result_'+slot]
//                                              to raw / encoded SVG text
// ═══════════════════════════════════════════════════════════════════════

#include "npd_parser.h"
#include "diagram_view.h"

#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QVBoxLayout>

#ifdef NOTEPATRA_WITH_WEBENGINE
#include <functional>
#include <memory>

#include <QByteArray>
#include <QDateTime>
#include <QDir>
#include <QEventLoop>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QMarginsF>
#include <QPageLayout>
#include <QPageSize>
#include <QRandomGenerator>
#include <QTimer>
#include <QUrl>
#include <QVariant>
#include <QWebEnginePage>
#include <QWebEngineView>
#endif

namespace {

#ifdef NOTEPATRA_WITH_WEBENGINE

// Encode an arbitrary compact-JSON byte string as a valid JS string
// literal. Qt5's QJsonDocument has no QJsonValue ctor (only QJsonObject /
// QJsonArray), so we wrap the string in a 1-element array, serialize, and
// slice off the surrounding [ ] — this escapes every quote / backslash /
// control char the graph JSON can contain. Identical trick to
// VegaChartRenderer::setSpec(); see the long comment there for the
// v0.1.63 CI-only crash this guards against.
QString jsStringLiteral(const QByteArray &raw) {
    QJsonArray wrap;
    wrap.append(QString::fromUtf8(raw));
    const QByteArray wrapped = QJsonDocument(wrap).toJson(QJsonDocument::Compact);
    // wrapped looks like: ["...escaped JSON..."] — drop the [ and ].
    return QString::fromUtf8(wrapped.mid(1, wrapped.size() - 2));
}

// Per-export window slot id — collision-free across concurrent exports.
QString uniqueSlot() {
    return QStringLiteral("s") + QString::number(QDateTime::currentMSecsSinceEpoch())
           + QStringLiteral("_")
           + QString::number(QRandomGenerator::global()->generate());
}

bool writeBytes(const QString &path, const QByteArray &bytes) {
    QDir().mkpath(QFileInfo(path).absolutePath());
    QFile f(path);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate)) return false;
    const qint64 n = f.write(bytes);
    f.close();
    return n == bytes.size();
}

#endif // NOTEPATRA_WITH_WEBENGINE

} // namespace

// ─────────────────────────────────────────────────────────────────────
// WebEngine path (Full build)
// ─────────────────────────────────────────────────────────────────────

#ifdef NOTEPATRA_WITH_WEBENGINE

DiagramView::DiagramView(QWidget *parent) : QWidget(parent) {
    auto *lay = new QVBoxLayout(this);
    lay->setContentsMargins(0, 0, 0, 0);
    lay->setSpacing(0);

    m_view = new QWebEngineView(this);
    // Transparent background so the host card / panel theme shows through;
    // the diagram.html body cooperates with a transparent background.
    m_view->page()->setBackgroundColor(Qt::transparent);
    m_view->setMinimumHeight(280);
    lay->addWidget(m_view);

    // Once the render layer has loaded, replay any source queued by an
    // early setSource() call.
    connect(m_view, &QWebEngineView::loadFinished, this, [this](bool ok) {
        m_pageReady = ok;
        if (!ok) {
            emit renderError(QStringLiteral("WebEngine page failed to load"));
            return;
        }
        if (m_hasPending) {
            const QString queued = m_pendingNpd;
            m_pendingNpd.clear();
            m_hasPending = false;
            pushSource(queued);
        }
    });

    // qrc:///diagram/diagram.html is the render layer (built in parallel).
    m_view->load(QUrl(QStringLiteral("qrc:///diagram/diagram.html")));
}

DiagramView::~DiagramView() = default;

void DiagramView::setSource(const QString &npdText) {
    if (!m_pageReady) {
        // Stash — the loadFinished slot replays it.
        m_pendingNpd = npdText;
        m_hasPending = true;
        return;
    }
    pushSource(npdText);
}

void DiagramView::pushSource(const QString &npdText) {
    if (!m_view) {
        emit renderError(QStringLiteral("Web view not initialised"));
        return;
    }
    const Npd::Diagram d = Npd::parse(npdText);
    // Surface parse problems but still render whatever the parser salvaged.
    if (!d.ok()) {
        emit renderError(d.errors.join(QStringLiteral("; ")));
    }

    const QByteArray graphJson =
        QJsonDocument(Npd::toGraphJson(d)).toJson(QJsonDocument::Compact);
    const QString js =
        QStringLiteral("npdRender(%1)").arg(jsStringLiteral(graphJson));
    m_view->page()->runJavaScript(js);
}

// Bridge an async window-slot export to a synchronous bool. We kick the
// page's _npd_export_<fmt>() hook, then poll window['_npd_result_'+slot]
// via runJavaScript on a QTimer, spinning a local QEventLoop until the
// slot resolves or the hard timeout fires. This is the same blocking-
// over-async shape notes_export.cpp uses for printToPdf, so DiagramView's
// public exportTo() can stay synchronous as specified.
bool DiagramView::exportImageBlocking(const QString &jsFmt, double scale,
                                      bool svg, const QString &path) {
    if (!m_view || !m_pageReady) {
        emit renderError(QStringLiteral("Cannot export before the diagram has loaded"));
        return false;
    }

    const QString slot = uniqueSlot();
    const QString kick =
        svg ? QStringLiteral("window._npd_export_%1('%2')").arg(jsFmt, slot)
            : QStringLiteral("window._npd_export_%1('%2', %3)")
                  .arg(jsFmt, slot).arg(scale);

    QEventLoop loop;
    QByteArray result;
    bool done = false;
    bool failed = false;

    // Hard timeout so a never-resolving slot can't freeze the UI thread.
    QTimer timeout;
    timeout.setSingleShot(true);
    connect(&timeout, &QTimer::timeout, &loop, [&loop, &failed]() {
        failed = true;
        loop.quit();
    });

    auto *page = m_view->page();

    // Recursive poll. std::function lets the lambda reference itself.
    auto poll = std::make_shared<std::function<void()>>();
    *poll = [&]() {
        if (done || failed) return;
        page->runJavaScript(
            QStringLiteral("window['_npd_result_%1'] || ''").arg(slot),
            [&, poll](const QVariant &v) {
                if (done || failed) return;
                const QString s = v.toString();
                if (s.isEmpty()) {
                    QTimer::singleShot(100, page, [poll]() { (*poll)(); });
                    return;
                }
                if (s.startsWith(QStringLiteral("__err__")) ||
                    s == QStringLiteral("__nover__")) {
                    failed = true;
                    loop.quit();
                    return;
                }
                if (svg) {
                    // SVG slot may be a "data:image/svg+xml,<percent>" URL
                    // or raw markup. Unwrap the data-URL prefix, then
                    // percent-decode if it isn't already raw XML.
                    QString xml = s;
                    const int comma = xml.indexOf(QLatin1Char(','));
                    if (xml.startsWith(QStringLiteral("data:")) && comma > 0)
                        xml = xml.mid(comma + 1);
                    if (!xml.trimmed().startsWith(QLatin1Char('<')))
                        result = QByteArray::fromPercentEncoding(xml.toUtf8());
                    else
                        result = xml.toUtf8();
                } else {
                    // Raster slot is a "data:image/...;base64,<b64>" URL.
                    const int comma = s.indexOf(QLatin1Char(','));
                    const QString b64 = (comma > 0) ? s.mid(comma + 1) : s;
                    result = QByteArray::fromBase64(b64.toLatin1());
                }
                done = true;
                // Free the slot so we don't leak strings into JS scope.
                page->runJavaScript(
                    QStringLiteral("delete window['_npd_result_%1']").arg(slot));
                loop.quit();
            });
    };

    page->runJavaScript(kick, [poll](const QVariant &) { (*poll)(); });

    timeout.start(30000);  // 30 s, matching notes_export.cpp's PDF ceiling
    loop.exec();

    if (failed || !done || result.isEmpty()) {
        emit renderError(QStringLiteral("Diagram export (%1) produced no data").arg(jsFmt));
        return false;
    }
    if (!writeBytes(path, result)) {
        emit renderError(QStringLiteral("Could not write %1").arg(path));
        return false;
    }
    return true;
}

bool DiagramView::exportPdfBlocking(const QString &path) {
    if (!m_view || !m_pageReady) {
        emit renderError(QStringLiteral("Cannot export before the diagram has loaded"));
        return false;
    }
    auto *page = m_view->page();
    // printToPdf doesn't mkpath on its own.
    QDir().mkpath(QFileInfo(path).absolutePath());

    QEventLoop loop;
    bool ok = false;

    const QPageLayout layout(QPageSize(QPageSize::A4), QPageLayout::Landscape,
                             QMarginsF(12, 12, 12, 12));  // mm; landscape suits diagrams

    QMetaObject::Connection conn = connect(
        page, &QWebEnginePage::pdfPrintingFinished, &loop,
        [&loop, &ok](const QString &, bool success) {
            ok = success;
            loop.quit();
        });

    QTimer timeout;
    timeout.setSingleShot(true);
    connect(&timeout, &QTimer::timeout, &loop, [&loop]() { loop.quit(); });
    timeout.start(30000);

    page->printToPdf(path, layout);
    loop.exec();
    disconnect(conn);

    if (!ok) {
        emit renderError(QStringLiteral("WebEngine printToPdf reported failure"));
        return false;
    }
    return true;
}

bool DiagramView::exportHtmlBlocking(const QString &path) {
    if (!m_view || !m_pageReady) {
        emit renderError(QStringLiteral("Cannot export before the diagram has loaded"));
        return false;
    }
    auto *page = m_view->page();

    QEventLoop loop;
    QString html;
    bool got = false;

    QTimer timeout;
    timeout.setSingleShot(true);
    connect(&timeout, &QTimer::timeout, &loop, [&loop]() { loop.quit(); });
    timeout.start(30000);

    // toHtml serializes the live DOM — the rendered diagram is captured as
    // a standalone snapshot of the current page state.
    page->toHtml([&loop, &html, &got](const QString &result) {
        html = result;
        got = true;
        loop.quit();
    });
    loop.exec();

    if (!got || html.isEmpty()) {
        emit renderError(QStringLiteral("Could not serialize diagram HTML"));
        return false;
    }
    if (!writeBytes(path, html.toUtf8())) {
        emit renderError(QStringLiteral("Could not write %1").arg(path));
        return false;
    }
    return true;
}

bool DiagramView::exportTo(const QString &format, const QString &path) {
    const QString fmt = format.trimmed().toLower();
    if (fmt == QStringLiteral("png"))
        return exportImageBlocking(QStringLiteral("png"), 2.0, false, path);
    if (fmt == QStringLiteral("webp"))
        return exportImageBlocking(QStringLiteral("webp"), 2.0, false, path);
    if (fmt == QStringLiteral("jpeg") || fmt == QStringLiteral("jpg"))
        return exportImageBlocking(QStringLiteral("jpeg"), 2.0, false, path);
    if (fmt == QStringLiteral("svg"))
        return exportImageBlocking(QStringLiteral("svg"), 1.0, true, path);
    if (fmt == QStringLiteral("pdf"))
        return exportPdfBlocking(path);
    if (fmt == QStringLiteral("html") || fmt == QStringLiteral("htm"))
        return exportHtmlBlocking(path);

    emit renderError(QStringLiteral("Unsupported export format: %1").arg(format));
    return false;
}

#else // NOTEPATRA_WITH_WEBENGINE

// ─────────────────────────────────────────────────────────────────────
// Lite-mode stub — same public API, no WebEngine. setSource() paints a
// parse summary; exportTo() is a no-op that reports the missing feature.
// ─────────────────────────────────────────────────────────────────────

namespace {
// "<n> nodes, <m> edges" — pluralized. Stub-only; the Full arm surfaces
// parse state through renderError instead.
QString parseSummary(const Npd::Diagram &d) {
    return QStringLiteral("%1 node%2, %3 edge%4")
        .arg(d.nodes.size())
        .arg(d.nodes.size() == 1 ? QString() : QStringLiteral("s"))
        .arg(d.edges.size())
        .arg(d.edges.size() == 1 ? QString() : QStringLiteral("s"));
}
} // namespace

DiagramView::DiagramView(QWidget *parent) : QWidget(parent) {
    auto *lay = new QVBoxLayout(this);
    lay->setContentsMargins(0, 0, 0, 0);
    lay->setSpacing(0);

    m_label = new QLabel(
        QStringLiteral("Diagram preview needs the Full build"), this);
    m_label->setAlignment(Qt::AlignCenter);
    m_label->setWordWrap(true);
    m_label->setMargin(16);
    lay->addWidget(m_label);
    setMinimumHeight(140);
}

DiagramView::~DiagramView() = default;

void DiagramView::setSource(const QString &npdText) {
    const Npd::Diagram d = Npd::parse(npdText);
    if (m_label) {
        m_label->setText(QStringLiteral("%1 — preview needs the Full build")
                             .arg(parseSummary(d)));
    }
    // Not an error state — a missing-feature state — so renderError is
    // intentionally NOT emitted here.
}

bool DiagramView::exportTo(const QString &format, const QString &path) {
    Q_UNUSED(format);
    Q_UNUSED(path);
    emit renderError(
        QStringLiteral("Diagram export needs the Full build"));
    return false;
}

#endif // NOTEPATRA_WITH_WEBENGINE
