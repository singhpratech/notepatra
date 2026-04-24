#ifndef MARKDOWNPREVIEW_H
#define MARKDOWNPREVIEW_H

#include <QWidget>
#include <QLabel>
#include <QTextBrowser>

class MarkdownPreview : public QWidget {
    Q_OBJECT
public:
    explicit MarkdownPreview(QWidget *parent = nullptr);
    void updatePreview(const QString &markdown);

public slots:
    // Re-apply the theme-aware stylesheet for the header + preview canvas
    // when MainWindow emits themeChanged(). Also re-renders the last
    // markdown so the inline HTML style block (colors baked per-theme)
    // flips to the new palette.
    void onThemeChanged();

private:
    void applyPalette();
    QLabel *m_header = nullptr;
    QTextBrowser *m_browser;
    QString m_lastMarkdown;
    QString markdownToHtml(const QString &md);
};

#endif
