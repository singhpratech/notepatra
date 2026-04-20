#ifndef PROJECTSEARCH_H
#define PROJECTSEARCH_H

#include <QWidget>
#include <QString>
#include <QStringList>
#include <QVector>
#include <QThread>
#include <QMutex>
#include <QHash>
#include <atomic>

class QLineEdit;
class QPushButton;
class QCheckBox;
class QLabel;
class QTreeWidget;
class QTreeWidgetItem;
class QProgressBar;

/**
 * Project-wide file + content search.
 *
 * Opens as a tab. Users type a query, pick a folder (defaults to the
 * current tab's file's folder), optionally restrict by file-name glob,
 * and get a tree of matches. Double-click a match jumps to the line in
 * a new tab.
 *
 * Architecture:
 *   - SearchWorker runs on its own QThread, walks the folder tree with
 *     QDirIterator, reads files in chunks, and emits matches as it
 *     finds them. Cancellable at any time.
 *   - The main widget streams results into a QTreeWidget as they
 *     arrive — users see first hits within milliseconds of pressing
 *     Enter, even on huge trees.
 */
struct ProjectSearchMatch {
    QString filePath;
    int     lineNumber;
    QString lineContent;
    int     matchStart;   // char offset into lineContent
    int     matchLength;
};

class ProjectSearchWorker : public QObject {
    Q_OBJECT
public:
    explicit ProjectSearchWorker(QObject *parent = nullptr);

    struct Params {
        QString folder;
        QString query;
        QString fileGlobs;     // comma-separated: *.py,*.js
        bool    searchNames;   // also match the file NAME not just contents
        bool    caseSensitive;
        bool    wholeWord;
        bool    regex;
        qint64  maxFileSizeBytes = 16 * 1024 * 1024;  // 16 MB per file cap
    };

public slots:
    void search(const Params &p);
    void cancel();

signals:
    void filesCounted(int totalFiles);
    void fileStarted(const QString &filePath);
    void matchFound(const ProjectSearchMatch &m);
    void fileNameMatch(const QString &filePath);
    void progress(int filesDone, int filesTotal, int matches);
    void finishedSearch(int totalMatches, int totalFiles);
    void errorOccurred(const QString &msg);

private:
    std::atomic<bool> m_cancel{false};
};

class ProjectSearch : public QWidget {
    Q_OBJECT
public:
    explicit ProjectSearch(QWidget *parent = nullptr);
    ~ProjectSearch();

    // Prefill the folder input — used when the menu opens the panel with
    // "search in the folder of the current file".
    void setFolder(const QString &folder);
    void setQuery(const QString &query);
    void focusQuery();

signals:
    void openFileAtLine(const QString &filePath, int lineNumber);

private:
    void buildUi();
    void startSearch();
    void cancelSearch();
    void onMatch(const ProjectSearchMatch &m);
    void onFileNameMatch(const QString &filePath);
    void onProgress(int done, int total, int matches);
    void onFinished(int totalMatches, int totalFiles);

    QLineEdit  *m_queryInput;
    QLineEdit  *m_folderInput;
    QPushButton *m_browseBtn;
    QLineEdit  *m_globInput;
    QCheckBox  *m_caseChk, *m_wordChk, *m_regexChk, *m_namesChk;
    QPushButton *m_searchBtn, *m_cancelBtn;
    QLabel     *m_statusLabel;
    QProgressBar *m_progressBar;
    QTreeWidget *m_results;

    QThread    *m_thread = nullptr;
    ProjectSearchWorker *m_worker = nullptr;

    QHash<QString, QTreeWidgetItem*> m_fileItems;  // file path → tree parent
    int m_matchesSoFar = 0;
    int m_filesWithMatches = 0;
};

#endif // PROJECTSEARCH_H
