#ifndef SEARCHRESULTS_H
#define SEARCHRESULTS_H

#include <QWidget>
#include <QTreeWidget>
#include <QLabel>

/**
 * Search Results panel — shows at bottom of editor like Notepad++.
 * Double-click any result line to jump to that exact match.
 */
class SearchResultsPanel : public QWidget {
    Q_OBJECT
public:
    explicit SearchResultsPanel(QWidget *parent = nullptr);

    void clear();
    void setHeader(const QString &searchTerm, int totalHits, int fileCount);
    void addFileSection(const QString &filePath, int hitCount);
    void addResultLine(int lineNumber, const QString &lineContent, const QString &matchText);

signals:
    void resultDoubleClicked(const QString &filePath, int lineNumber);

private:
    QTreeWidget *m_tree;
    QLabel *m_header;
    QString m_currentFile;
    QTreeWidgetItem *m_currentFileItem = nullptr;
};

#endif
