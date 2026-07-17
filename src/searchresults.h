// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef SEARCHRESULTS_H
#define SEARCHRESULTS_H

#include <QWidget>
#include <QTreeWidget>
#include <QLabel>
#include <QVector>
#include <QHash>

/**
 * Search Results panel — shows at bottom of editor like Notepad++.
 * Double-click any result line to jump to that exact match.
 *
 * v0.1.45 — header now has a red ✕ close button on the right that
 * hides the panel (closeRequested signal). Each search becomes a new
 * top-level "session" item in the tree (Notepad++-style stacked
 * history); previous sessions auto-collapse, capped at 10.
 */
class SearchResultsPanel : public QWidget {
    Q_OBJECT
public:
    explicit SearchResultsPanel(QWidget *parent = nullptr);

    // v0.1.45 — wipes ALL stacked sessions (kept for legacy callers
    // that wanted a hard reset; new callers should prefer beginSession).
    void clear();

    // v0.1.45 — start a new top-level session for the upcoming
    // addFileSection / addResultLine calls. Inserts at the top of the
    // tree, expands by default, collapses prior sessions, prunes the
    // oldest if more than 10 sessions are already stacked.
    void beginSession(const QString &searchTerm);

    // Updates the CURRENT session's label with the final hit / file
    // counts. If no session is active, opens one first (back-compat
    // for callers that still use the old setHeader-first pattern).
    void setHeader(const QString &searchTerm, int totalHits, int fileCount);
    // filePath is the REAL path (empty for unsaved tabs) stored on each
    // match row for double-click navigation; displayName, when given, is
    // what the file row shows (e.g. "Untitled" or a native-separator path).
    void addFileSection(const QString &filePath, int hitCount,
                        const QString &displayName = QString());
    void addResultLine(int lineNumber, const QString &lineContent, const QString &matchText);

signals:
    void resultDoubleClicked(const QString &filePath, int lineNumber);
    // v0.1.45 — emitted by the red ✕ button in the header row. The
    // host (MainWindow) catches this and hides the panel.
    void closeRequested();

private:
    // v0.1.46 — flush the in-memory session list to disk so search
    // history survives an app restart. Debounced via m_saveTimer.
    void persistHistory();
    void loadPersistedHistory();
    void scheduleSave();

    QTreeWidget *m_tree;
    QLabel *m_header;
    // v0.1.46 — kept for setVisible() toggling: hidden when the panel
    // has no sessions (avoids a stray ✕ floating on an empty panel).
    class QPushButton *m_closeBtn = nullptr;
    class QPushButton *m_clearBtn = nullptr;
    class QTimer *m_saveTimer = nullptr;
    QString m_historyPath;

    // v0.1.45 — stacked-session bookkeeping. The current session is
    // the one new addFileSection / addResultLine calls land under;
    // m_sessions retains every session row in chronological order so
    // we can prune the oldest when the cap is exceeded; the per-
    // session file map lets the same file path appear under multiple
    // sessions without colliding.
    QTreeWidgetItem *m_currentSession = nullptr;
    QVector<QTreeWidgetItem*> m_sessions;
    QTreeWidgetItem *m_currentFileItem = nullptr;
    QString m_currentFile;
};

#endif
