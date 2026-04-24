#ifndef PROJECTSEARCH_H
#define PROJECTSEARCH_H

#include <QWidget>
#include <QString>
#include <QStringList>
#include <QVector>
#include <QThread>
#include <QMutex>
#include <QHash>
#include <QElapsedTimer>
#include <atomic>

class QTimer;

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
        bool    skipBinary = true;   // skip files that look binary (fast check)
        qint64  maxFileSizeBytes = 2LL * 1024 * 1024 * 1024;  // 2 GB — effectively no cap
    };

public slots:
    void search(const Params &p);
    void cancel();

signals:
    // Fired periodically during the filesystem walk phase so the UI shows
    // activity before file-searching even starts. Without this the UI
    // freezes on "Scanning..." for the entire walk of large trees.
    void walkProgress(int filesDiscoveredSoFar);
    void filesCounted(int totalFiles);
    void fileStarted(const QString &filePath);
    // Batched per-file match delivery. One queued event per file (not per
    // match) — huge win when a file has thousands of matches, because every
    // queued emit costs ~a microsecond of UI-thread event processing.
    void matchesFound(const QVector<ProjectSearchMatch> &matches);
    void fileNameMatch(const QString &filePath);
    void progress(int filesDone, int filesTotal, int matches,
                  qint64 elapsedMs, qint64 linesScanned);
    void finishedSearch(int totalMatches, int totalFiles,
                        qint64 elapsedMs, qint64 linesScanned);
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

    // Test hooks — let headless tests inspect the live UI state without
    // needing a visible window. Also lets an external demo driver kick
    // off a search programmatically (bypasses xdotool focus races).
    QString currentStatusText() const;
    int     currentProgressValue() const;
    void    triggerSearchForTesting() { startSearch(); }

signals:
    void openFileAtLine(const QString &filePath, int lineNumber);
    // Emitted when the user double-clicks a result — column is 1-based
    // and lets the host scroll the editor to the exact match character.
    void openFileAtLineCol(const QString &filePath, int lineNumber, int column);

private:
    void buildUi();
    void startSearch();
    void cancelSearch();
    void onMatches(const QVector<ProjectSearchMatch> &matches);
    void onFileNameMatch(const QString &filePath);
    void onProgress(int done, int total, int matches,
                    qint64 elapsedMs, qint64 linesScanned);
    void onFinished(int totalMatches, int totalFiles,
                    qint64 elapsedMs, qint64 linesScanned);

    QLineEdit  *m_queryInput;
    QLineEdit  *m_folderInput;
    QPushButton *m_browseBtn;
    QLineEdit  *m_globInput;
    QCheckBox  *m_caseChk, *m_wordChk, *m_regexChk, *m_namesChk, *m_binaryChk;
    QPushButton *m_searchBtn, *m_cancelBtn;
    QLabel     *m_statusLabel;
    QProgressBar *m_progressBar;
    QTreeWidget *m_results;

    QThread    *m_thread = nullptr;
    ProjectSearchWorker *m_worker = nullptr;

    QHash<QString, QTreeWidgetItem*> m_fileItems;  // file path → tree parent
    int m_matchesSoFar = 0;
    int m_filesWithMatches = 0;

    // ── Live status refresher ────────────────────────────────────────
    // Worker ticks every 8 files; between ticks, the elapsed-ms display
    // would otherwise freeze. A 10 Hz QTimer in the UI reads the wall
    // clock + last-known counters and repaints the status line so
    // elapsed visibly scrolls up, even when no new match just arrived.
    // Timer stops when finishedSearch fires (search hit 100 %).
    QTimer        *m_liveTimer = nullptr;
    QElapsedTimer  m_wallTimer;
    enum class Phase { Idle, Walking, Scanning };
    Phase m_phase = Phase::Idle;
    int    m_lastFilesDone = 0;
    int    m_lastFilesTotal = 0;
    int    m_lastMatches = 0;
    qint64 m_lastLines = 0;
    int    m_lastWalkDiscovered = 0;
    void refreshLiveStatus();
};

#endif // PROJECTSEARCH_H
