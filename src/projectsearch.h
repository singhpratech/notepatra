// SPDX-License-Identifier: GPL-3.0-or-later

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
#include <functional>

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
        // v0.1.93 — "Match all words" mode. Splits `query` on whitespace into
        // tokens; a file matches when EVERY token appears somewhere in it
        // (any order, any distance, any line). Implements GitHub-code-search
        // semantics for users who don't want to learn regex. Mutually
        // ignored when `regex` is true (regex pattern wins).
        bool    allWords = false;
        bool    skipBinary = true;   // skip files that look binary (fast check)
        qint64  maxFileSizeBytes = 2LL * 1024 * 1024 * 1024;  // 2 GB — effectively no cap
    };

public slots:
    void search(const Params &p);
    void cancel();
    // v0.1.93 — B-with-tweaks checkpoint replies. UI calls one of these
    // from the modal dialog when pauseAtCheckpoint fires:
    //   • resumePastCheckpoint(): unpause, never ask again this search
    //   • stopButShowResults(): cancel scan, keep the partial results
    void resumePastCheckpoint();
    void stopButShowResults();

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
    // v0.1.93 — B-with-tweaks soft/hard threshold signals. softWarningHit
    // fires ONCE per search when totalMatches first crosses 10k — UI paints
    // the progress bar amber + adds a count to status, no interruption.
    // pauseAtCheckpoint fires ONCE at 50k matches — worker is paused, UI
    // shows a modal asking Continue / Show me these / Cancel.
    void softWarningHit(int totalMatches);
    void pauseAtCheckpoint(int totalMatches, int filesWithMatches,
                           int filesDone, int filesTotal);
    void errorOccurred(const QString &msg);

private:
    std::atomic<bool> m_cancel{false};
    // v0.1.93 — B-with-tweaks state. m_paused gates all worker threads at
    // file boundaries during checkpoint; m_softWarned / m_checkpointFired
    // ensure each signal fires once per search; m_skipCheckpoints stays
    // true after the user clicks Continue so we don't ask again.
    std::atomic<bool> m_paused{false};
    std::atomic<bool> m_softWarned{false};
    std::atomic<bool> m_checkpointFired{false};
    std::atomic<bool> m_skipCheckpoints{false};
};

class QScrollArea;

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

public slots:
    // Re-apply every psearchPalette()-dependent stylesheet in the widget
    // tree (panel bg, title, inputs, checkboxes, search/cancel buttons,
    // progress bar, status label, results tree, scroll-area scrollbars)
    // when MainWindow emits themeChanged().
    void onThemeChanged();

signals:
    void openFileAtLine(const QString &filePath, int lineNumber);
    // Emitted when the user double-clicks a result — column is 1-based
    // and lets the host scroll the editor to the exact match character.
    void openFileAtLineCol(const QString &filePath, int lineNumber, int column);
    // v0.1.44 — fired by the red × in the title row. The host (MainWindow)
    // catches this and removes the search panel's tab.
    void closeRequested();

private:
    void buildUi();
    void applyPalette();
    void startSearch();
    void cancelSearch();
    void onMatches(const QVector<ProjectSearchMatch> &matches);
    void onFileNameMatch(const QString &filePath);
    void onProgress(int done, int total, int matches,
                    qint64 elapsedMs, qint64 linesScanned);
    void onFinished(int totalMatches, int totalFiles,
                    qint64 elapsedMs, qint64 linesScanned);
    // v0.1.93 — B-with-tweaks UI handlers. onSoftWarning paints the amber
    // progress bar and shows a count. onPauseAtCheckpoint opens the modal
    // dialog asking what to do at the 50k threshold.
    void onSoftWarning(int totalMatches);
    void onPauseAtCheckpoint(int totalMatches, int filesWithMatches,
                             int filesDone, int filesTotal);

    // Theme-aware chrome retained for applyPalette() — these widgets have
    // stylesheets that interpolate from psearchPalette() and need to
    // re-render when the user flips Config::theme at runtime.
    QScrollArea *m_scrollArea = nullptr;
    QWidget    *m_content = nullptr;
    QLabel     *m_title = nullptr;
    QLabel     *m_hint = nullptr;
    QLabel     *m_folderLabel = nullptr;
    QLabel     *m_globLabel = nullptr;

    QLineEdit  *m_queryInput;
    QLineEdit  *m_folderInput;
    QPushButton *m_browseBtn;
    QLineEdit  *m_globInput;
    QCheckBox  *m_caseChk, *m_wordChk, *m_regexChk, *m_allWordsChk, *m_namesChk, *m_binaryChk;
    QPushButton *m_searchBtn, *m_cancelBtn;
    QLabel     *m_statusLabel;
    QProgressBar *m_progressBar;
    QTreeWidget *m_results;

    QThread    *m_thread = nullptr;
    ProjectSearchWorker *m_worker = nullptr;

    // v0.1.44 — search results are now stacked as collapsible "session"
    // top-level items (one per query) with files nested as children and
    // matches as grandchildren. The previous flat-tree single-search model
    // had two problems users hit: pressing Search wiped the prior results,
    // and there was no way to flip back and forth between two queries.
    //
    //  m_currentSession   — the session row populated by the in-flight
    //                       search; null between searches.
    //  m_sessions         — every session row in chronological order; we
    //                       cap at 10 and prune the oldest.
    //  m_perSessionFiles  — per-session file-path → file-row map (the
    //                       same file path can appear in multiple
    //                       sessions, so the file-lookup index is keyed
    //                       by session item).
    QTreeWidgetItem *m_currentSession = nullptr;
    QVector<QTreeWidgetItem*> m_sessions;
    QHash<QTreeWidgetItem*, QHash<QString, QTreeWidgetItem*>> m_perSessionFiles;
    QPushButton *m_clearHistoryBtn = nullptr;
    int m_matchesSoFar = 0;
    int m_filesWithMatches = 0;

    // v0.1.93 — B-with-tweaks UI state. m_softWarnActive is true once the
    // 10k amber bar is shown so we don't keep restyling. m_stoppedAtCheckpoint
    // is true if the user clicked "Show me these" — onFinished uses it to
    // pick the partial-results status text instead of the green ✓ line.
    bool m_softWarnActive = false;
    bool m_stoppedAtCheckpoint = false;

    // v0.1.93 — relevance state. When Match all words is ON the UI computes:
    //   • Per-FILE score in onMatches (UserRole + 3 on the file row). Phrase
    //     match = +1,000,000 boost, every match = +1. onFinished re-orders
    //     session children by this score descending.
    //   • Per-LINE relevance % in each match row's render: how many of the
    //     query tokens appear on that line. Line containing the literal
    //     phrase = 100 %; line with all tokens but not adjacent = 100 %;
    //     line with 1 of 2 tokens = 50 %; etc. Prepended to the rendered
    //     row text so users can scan relevance at a glance.
    QString     m_currentQuery;          // full query string (for phrase test)
    QStringList m_currentTokens;         // pre-split tokens (whitespace)
    bool        m_currentSearchAllWords = false;
    bool        m_currentCaseSensitive = false;

    // Called every time the match tree gains/loses rows — resizes
    // the tree to fit all visible rows so the outer page scroll
    // (one-scroll UX) can take over.
    std::function<void()> m_resizeTree;

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
    // Last path any worker thread started scanning — used by refreshLiveStatus
    // to append "· current: <path>" when the scan appears to have wedged
    // (same tick count for 2 s). Gives the user actionable diagnostic info
    // instead of a frozen progress bar.
    QString m_lastFileInFlight;
    int    m_stalledTicks = 0;  // counts UI ticks where (done,total) didn't change
    void refreshLiveStatus();
};

#endif // PROJECTSEARCH_H
