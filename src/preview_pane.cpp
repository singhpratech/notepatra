// SPDX-License-Identifier: GPL-3.0-or-later

#include "preview_pane.h"

#include "config.h"
#include "editor.h"
#include "sql_notebook.h"
#include "themes.h"

#include <QDesktopServices>
#include <QFileInfo>
#include <QImage>
#include <QLabel>
#include <QScrollBar>
#include <QStackedWidget>
#include <QTextBrowser>
#include <QTimer>
#include <QUrl>
#include <QVBoxLayout>

#ifdef NOTEPATRA_WITH_WEBENGINE
#include <QWebEnginePage>
#include <QWebEngineSettings>
#include <QWebEngineView>
#endif

namespace {

#ifdef NOTEPATRA_WITH_WEBENGINE
// The pane shows ONE document: our own setHtml loads. A previewed file cannot
// navigate the pane (meta refresh, forms, iframes); http(s)/mailto links open externally.
class PreviewWebPage : public QWebEnginePage {
public:
    using QWebEnginePage::QWebEnginePage;
protected:
    bool acceptNavigationRequest(const QUrl &url, NavigationType type, bool isMainFrame) override {
        if (type == NavigationTypeTyped && isMainFrame) return true;
        if (type == NavigationTypeLinkClicked) {
            const QString s = url.scheme().toLower();
            if (s == QLatin1String("http") || s == QLatin1String("https") || s == QLatin1String("mailto"))
                QDesktopServices::openUrl(url);
        }
        return false;
    }
};
#endif

QString blend(const QColor &from, const QColor &to, qreal k) {
    return QColor(qRound(from.red() + (to.red() - from.red()) * k),
                  qRound(from.green() + (to.green() - from.green()) * k),
                  qRound(from.blue() + (to.blue() - from.blue()) * k))
        .name();
}

const QString kPlaceholderText = QStringLiteral(
    "Preview shows Markdown, HTML, Jupyter notebooks (.ipynb) and SQL notebooks "
    "(cells separated by -- %%).");

// QWebEngineView::setHtml goes through a data: URL capped at 2 MB.
constexpr int kMaxWebHtmlChars = 1500 * 1024;

} // namespace

PreviewPane::PreviewPane(QWidget *parent) : QWidget(parent) {
    setObjectName(QStringLiteral("previewPane"));
    setMinimumWidth(160);
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);

    m_header = new QLabel(tr("Preview"));
    m_header->setObjectName(QStringLiteral("previewPaneHeader"));
    m_header->setMinimumHeight(24);
    layout->addWidget(m_header);

    m_stack = new QStackedWidget;
    m_placeholder = new QLabel(kPlaceholderText);
    m_placeholder->setObjectName(QStringLiteral("previewPanePlaceholder"));
    m_placeholder->setWordWrap(true);
    m_placeholder->setAlignment(Qt::AlignCenter);
    m_placeholder->setMargin(16);
    m_stack->addWidget(m_placeholder);

    m_browser = new QTextBrowser;
    m_browser->setObjectName(QStringLiteral("previewPaneBrowser"));
    m_browser->setOpenLinks(false);
    m_browser->setOpenExternalLinks(true);
    m_browser->setFrameShape(QFrame::NoFrame);
    connect(m_browser, &QTextBrowser::anchorClicked, this, [this](const QUrl &url) {
        if (url.scheme().isEmpty() && url.path().isEmpty() && url.hasFragment()) {
            m_browser->scrollToAnchor(url.fragment());
            return;
        }
        const QString scheme = url.scheme().toLower();
        if (scheme == QLatin1String("http") || scheme == QLatin1String("https") ||
            scheme == QLatin1String("mailto"))
            QDesktopServices::openUrl(url);
    });
    m_stack->addWidget(m_browser);

    m_sql = new SqlNotebookWidget;
    m_stack->addWidget(m_sql);
    layout->addWidget(m_stack, 1);

    m_debounce = new QTimer(this);
    m_debounce->setSingleShot(true);
    m_debounce->setInterval(kDebounceMs);
    connect(m_debounce, &QTimer::timeout, this, &PreviewPane::refreshNow);
}

PreviewPane::Mode PreviewPane::modeFor(const Editor *ed) {
    if (!ed) return Mode::None;
    const QString path = ed->filePath();
    const QString suffix = QFileInfo(path).suffix().toLower();
    if (suffix == QLatin1String("ipynb")) return Mode::Notebook;
    const QString lang = ed->language();
    if (lang == QLatin1String("Markdown")) return Mode::Markdown;
    if (lang == QLatin1String("SQL")) return Mode::SqlNotebook;
    if (lang == QLatin1String("HTML") &&
        (path.isEmpty() || suffix == QLatin1String("html") || suffix == QLatin1String("htm") ||
         suffix == QLatin1String("xhtml")))
        return Mode::Html;
    return Mode::None;
}

QString PreviewPane::headerText() const {
    return m_header ? m_header->text() : QString();
}

void PreviewPane::setSource(Editor *ed) {
    if (m_textConn) disconnect(m_textConn);
    m_editor = ed;
    if (ed)
        m_textConn = connect(ed, &QsciScintilla::textChanged, this, &PreviewPane::scheduleRefresh);
    m_debounce->stop();
    m_dirty = true;
    m_sourceChanged = true;
    if (isVisible()) {
        refreshNow();
        return;
    }
    const Mode previous = m_mode;
    m_mode = modeFor(ed);
    if (previous != m_mode) emit modeChanged();
}

void PreviewPane::scheduleRefresh() {
    m_dirty = true;
    if (isVisible()) m_debounce->start();
}

void PreviewPane::showEvent(QShowEvent *event) {
    QWidget::showEvent(event);
    if (m_chromeDirty) applyChrome();
    if (m_dirty) QTimer::singleShot(0, this, &PreviewPane::refreshNow);
}

void PreviewPane::onThemeChanged() {
    m_chromeDirty = true;
    m_dirty = true;
    if (isVisible()) refreshNow();
}

void PreviewPane::applyChrome() {
    m_chromeDirty = false;
    const Theme t = resolveTheme(Config::instance().theme);
    const bool dark = t.editorBg.lightness() < 128;
    NotebookRender::Colors &c = m_colors;
    c.bg = t.editorBg.name();
    c.fg = t.editorFg.name();
    c.dimFg = blend(t.editorBg, t.editorFg, 0.6);
    c.codeBg = blend(t.editorBg, t.editorFg, 0.10);
    c.border = blend(t.editorBg, t.editorFg, 0.25);
    c.accent = dark ? QStringLiteral("#4EC9B0") : QStringLiteral("#0B7A75");
    c.linkFg = dark ? QStringLiteral("#7EC8FF") : QStringLiteral("#1A5FB4");

    m_header->setStyleSheet(QStringLiteral(
        "QLabel { font-weight: 600; padding: 3px 8px; background: %1; color: %2; "
        "border-bottom: 1px solid %3; }")
        .arg(t.tabBg.name(), t.tabFg.name(), t.tabBorder.name()));
    m_placeholder->setStyleSheet(
        QStringLiteral("QLabel { background: %1; color: %2; }").arg(c.bg, c.dimFg));
    m_browser->setStyleSheet(
        QStringLiteral("QTextBrowser { background: %1; color: %2; border: none; }").arg(c.bg, c.fg));
    m_browser->document()->setDefaultStyleSheet(QStringLiteral(
        "body { color: %1; }"
        "a { color: %2; }"
        "pre, code { background-color: %3; font-family: monospace; }"
        "table { border-color: %4; }"
        "th { background-color: %3; }"
        "td, th { border-color: %4; padding: 4px; }"
        "blockquote { color: %5; }")
        .arg(c.fg, c.linkFg, c.codeBg, c.border, c.dimFg));
    m_sql->applyColors(c);
}

void PreviewPane::showPlaceholder(const QString &text) {
    m_placeholder->setText(text);
    m_stack->setCurrentWidget(m_placeholder);
}

void PreviewPane::setBrowserHtml(const QString &html, const QString &baseDir) {
    QScrollBar *sb = m_browser->verticalScrollBar();
    const int scroll = sb ? sb->value() : 0;
    if (!baseDir.isEmpty()) {
        m_browser->document()->setBaseUrl(QUrl::fromLocalFile(baseDir + QLatin1Char('/')));
        m_browser->setSearchPaths({baseDir});  // relative <img src> resolve against the file's folder
    }
    m_browser->document()->setHtml(html);
    if (sb) sb->setValue(scroll);
    m_stack->setCurrentWidget(m_browser);
}

void PreviewPane::refreshNow() {
    m_debounce->stop();
    m_dirty = false;
    if (m_chromeDirty) applyChrome();
    Editor *ed = m_editor.data();
    const Mode previous = m_mode;
    m_mode = modeFor(ed);

    const QString path = ed ? ed->filePath() : QString();
    const QString name = path.isEmpty() ? tr("untitled") : QFileInfo(path).fileName();
    const QString dir = path.isEmpty() ? QString() : QFileInfo(path).absolutePath();
    switch (m_mode) {
    case Mode::Markdown: m_header->setText(tr("Markdown preview — %1").arg(name)); break;
    case Mode::Html: m_header->setText(tr("HTML preview — %1").arg(name)); break;
    case Mode::Notebook: m_header->setText(tr("Notebook — %1").arg(name)); break;
    case Mode::SqlNotebook: m_header->setText(tr("SQL notebook — %1").arg(name)); break;
    case Mode::None: m_header->setText(tr("Preview")); break;
    }

    if (m_mode == Mode::None) {
        showPlaceholder(kPlaceholderText);
    } else if (ed->length() > kMaxPreviewChars) {
        showPlaceholder(tr("This file is too large to preview."));
    } else {
        const QString text = ed->text();
        const NotebookRender::Colors &c = m_colors;
        if (m_mode == Mode::Markdown) {
            setBrowserHtml(NotebookRender::markdownToHtml(text, &c), dir);
        } else if (m_mode == Mode::Html) {
#ifdef NOTEPATRA_WITH_WEBENGINE
            if (text.size() <= kMaxWebHtmlChars) {
                if (!m_web) {
                    m_web = new QWebEngineView;
                    m_web->setPage(new PreviewWebPage(m_web));
                    // Static preview of an arbitrary file: no scripts, no network; local images/CSS beside the file still load.
                    QWebEngineSettings *ws = m_web->settings();
                    ws->setAttribute(QWebEngineSettings::JavascriptEnabled, false);
                    ws->setAttribute(QWebEngineSettings::LocalContentCanAccessRemoteUrls, false);
                    ws->setAttribute(QWebEngineSettings::LocalContentCanAccessFileUrls, true);
                    ws->setAttribute(QWebEngineSettings::PluginsEnabled, false);
                    ws->setAttribute(QWebEngineSettings::AutoLoadImages, true);
                    m_stack->addWidget(m_web);
                }
                m_web->setHtml(text, path.isEmpty() ? QUrl() : QUrl::fromLocalFile(path));
                m_stack->setCurrentWidget(m_web);
            } else {
                setBrowserHtml(text, dir);
            }
#else
            setBrowserHtml(text, dir);
#endif
        } else if (m_mode == Mode::Notebook) {
            QVector<NotebookRender::NotebookImage> images;
            QString err;
            const QString html = NotebookRender::ipynbToHtml(text.toUtf8(), c, &images, &err);
            if (!err.isEmpty()) {
                setBrowserHtml(QStringLiteral("<p>%1</p>")
                                   .arg(NotebookRender::escapeHtml(tr("Cannot render this notebook: %1").arg(err))),
                               dir);
            } else {
                QTextDocument *doc = m_browser->document();
                for (const auto &img : images)
                    doc->addResource(QTextDocument::ImageResource, QUrl(img.name),
                                     QImage::fromData(img.pngBytes, "PNG"));
                setBrowserHtml(html, dir);
            }
        } else if (m_mode == Mode::SqlNotebook) {
            if (m_sourceChanged || previous != Mode::SqlNotebook) m_sql->refreshConnections();
            m_sql->setCells(NotebookRender::splitSqlCells(text));
            m_stack->setCurrentWidget(m_sql);
        }
    }
    m_sourceChanged = false;
    if (previous != m_mode) emit modeChanged();
}
