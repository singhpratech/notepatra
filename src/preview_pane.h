// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef NOTEPATRA_PREVIEW_PANE_H
#define NOTEPATRA_PREVIEW_PANE_H

// Right-hand pane beside the editor tabs that renders the current tab:
// Markdown, HTML, Jupyter .ipynb (read-only) or a "-- %%" SQL notebook.

#include "notebook_render.h"

#include <QPointer>
#include <QWidget>

class Editor;
class QLabel;
class QStackedWidget;
class QTextBrowser;
class QTimer;
class SqlNotebookWidget;
#ifdef NOTEPATRA_WITH_WEBENGINE
class QWebEngineView;
#endif

class PreviewPane : public QWidget {
    Q_OBJECT
public:
    enum class Mode { None, Markdown, Html, Notebook, SqlNotebook };

    explicit PreviewPane(QWidget *parent = nullptr);

    // nullptr (or a non-previewable tab) shows the placeholder.
    void setSource(Editor *ed);
    Editor *source() const { return m_editor; }
    void refreshNow();

    static Mode modeFor(const Editor *ed);
    static bool supports(const Editor *ed) { return modeFor(ed) != Mode::None; }

    Mode mode() const { return m_mode; }
    SqlNotebookWidget *sqlNotebook() { return m_sql; }
    QTextBrowser *browser() { return m_browser; }
    QLabel *placeholder() { return m_placeholder; }
    QString headerText() const;

    static constexpr int kDebounceMs = 250;
    static constexpr int kMaxPreviewChars = 8 * 1024 * 1024;

public slots:
    void onThemeChanged();

signals:
    void modeChanged();

protected:
    void showEvent(QShowEvent *event) override;

private:
    void scheduleRefresh();
    // Resolves the theme once (System = an OS probe) and caches it in m_colors.
    void applyChrome();
    void showPlaceholder(const QString &text);
    void setBrowserHtml(const QString &html, const QString &baseDir);

    QPointer<Editor> m_editor;
    QMetaObject::Connection m_textConn;
    QTimer *m_debounce = nullptr;
    QLabel *m_header = nullptr;
    QStackedWidget *m_stack = nullptr;
    QLabel *m_placeholder = nullptr;
    QTextBrowser *m_browser = nullptr;
#ifdef NOTEPATRA_WITH_WEBENGINE
    QWebEngineView *m_web = nullptr;
#endif
    SqlNotebookWidget *m_sql = nullptr;
    Mode m_mode = Mode::None;
    bool m_dirty = true;
    bool m_chromeDirty = true;
    NotebookRender::Colors m_colors;
    bool m_sourceChanged = true;
};

#endif // NOTEPATRA_PREVIEW_PANE_H
