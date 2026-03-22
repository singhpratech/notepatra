#ifndef MARKDOWNPREVIEW_H
#define MARKDOWNPREVIEW_H

#include <QWidget>
#include <QTextBrowser>

class MarkdownPreview : public QWidget {
    Q_OBJECT
public:
    explicit MarkdownPreview(QWidget *parent = nullptr);
    void updatePreview(const QString &markdown);

private:
    QTextBrowser *m_browser;
    QString markdownToHtml(const QString &md);
};

#endif
