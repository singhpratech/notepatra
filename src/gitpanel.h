#ifndef GITPANEL_H
#define GITPANEL_H

#include <QWidget>
#include <QTreeWidget>
#include <QLabel>
#include <QProcess>
#include <QPushButton>
#include <QPlainTextEdit>
#include <QTimer>
#include <QFileSystemWatcher>
#include <QStringList>
#include <QVector>
#include <functional>

// ─────────────────────────────────────────────────────────────────────────────
// GitPanel — VS Code-style Source Control view, rewritten for async + clarity.
//
// The previous panel crammed branches / remotes / stashes / files into one
// giant tree and ran every git call synchronously on the UI thread. This
// rewrite trims the chrome to the five areas users actually use:
//
//   SOURCE CONTROL  (24 px label band)
//   ⎇ main · ↑3 ↓0 · Fetch · Branch ▾                         (36 px chip row)
//   ┌──── commit message (QPlainTextEdit) ────┐
//   │  Ctrl+Enter to commit                   │
//   └─ [✓ Commit]  N files staged ────────────┘
//   STAGED CHANGES (n)
//     M · src/foo.cpp                                 [−]
//   CHANGES (n)
//     M · src/bar.cpp                                 [+]
//   ↓ Pull (2)   ↑ Push (3)
//   HISTORY ▾ (collapsible, last 20 commits, "Load 20 more")
//   Stash ▾
//
// All git invocations go through an internal async runner that queues a
// single QProcess at a time per command-kind so we never stampede the
// working copy. The status refresh is debounced via a 250 ms timer
// triggered by QFileSystemWatcher on the repo root and .git/index.
// ─────────────────────────────────────────────────────────────────────────────

class QTreeWidgetItem;

class GitPanel : public QWidget {
    Q_OBJECT
public:
    explicit GitPanel(QWidget *parent = nullptr);

    // Entry point used by MainWindow: pass any path inside (or equal to) a
    // repository. The panel resolves the repo root and pulls everything else
    // on its own.
    void refresh(const QString &filePath);

public slots:
    // Re-apply every palette-dependent stylesheet — header bar, branch
    // chip, commit-message box, commit button, tree, history — when
    // MainWindow emits themeChanged(). Also re-renders the changes tree
    // so row foreground colors (green/red/amber baked per-row) pick up
    // the new palette.
    void onThemeChanged();

signals:
    // Kept for backward compat with MainWindow wiring. `fileClicked` is
    // emitted from the legacy context-menu path and from anywhere the user
    // asks to jump to a file without diff context.
    void fileClicked(const QString &path);
    void repositoryOpened(const QString &path);

    // New: emitted when user Ctrl/double-clicks a plain file row. Callers
    // should open the file in the editor tab stack.
    void openFileInTab(const QString &path);

    // New: emitted when user double-clicks a changed file row. Caller is
    // expected to open a CompareWidget tab showing `left` (HEAD version) vs
    // `right` (working copy). `title` is what the tab label should read.
    void openDiffInTab(const QString &title, const QString &leftText, const QString &rightText);

private:
    // ─── Parsed git data types ──────────────────────────────────────────────
    struct GitFileEntry {
        QString path;           // worktree path (destination of rename if any)
        char    indexStatus = ' ';
        char    workStatus  = ' ';
        bool    isRename    = false;
        QString originalPath;   // non-empty when isRename
    };

    struct AheadBehind {
        int ahead  = 0;
        int behind = 0;
        QString upstream;       // e.g. "origin/main" or "" when unset
    };

    struct CommitEntry {
        QString hash;
        QString shortHash;
        QString subject;
        QString author;
        QString relDate;
    };

    // ─── Widgets (top-to-bottom, matches the drawing in the comment) ────────
    QLabel          *m_sourceControlHeader = nullptr;
    QWidget         *m_branchChipRow       = nullptr;
    QLabel          *m_branchChipLabel     = nullptr;  // "⎇ main · ↑3 ↓0"
    QPushButton     *m_fetchBtn            = nullptr;
    QPushButton     *m_branchMenuBtn       = nullptr;

    QLabel          *m_gitMissingBanner    = nullptr;  // "git not found — install …"

    QPlainTextEdit  *m_commitMessage       = nullptr;
    QPushButton     *m_commitBtn           = nullptr;
    QLabel          *m_commitHint          = nullptr;  // "N files staged"

    QTreeWidget     *m_tree                = nullptr;
    QTreeWidgetItem *m_stagedSection       = nullptr;
    QTreeWidgetItem *m_changesSection      = nullptr;

    // v0.1.48 — VS Code-style inline diff. Tree on top, diff view below
    // sharing a QSplitter so the user can resize. Hidden until the user
    // selects a file row; collapses back when the diff is closed.
    class QSplitter   *m_treeDiffSplitter = nullptr;
    QWidget           *m_diffWrap         = nullptr;
    QLabel            *m_diffHeader       = nullptr;
    QPushButton       *m_diffCloseBtn     = nullptr;
    class QPlainTextEdit *m_diffView      = nullptr;
    QString            m_diffPath;        // path currently shown
    bool               m_diffStaged       = false;

    QPushButton     *m_pullBtn             = nullptr;
    QPushButton     *m_pushBtn             = nullptr;
    QWidget         *m_syncRow             = nullptr;  // 36 px bottom chrome strip

    // History is a pair: a bold clickable header that toggles expansion,
    // and the container holding the commit rows.
    QPushButton     *m_historyToggle       = nullptr;
    QWidget         *m_historyBody         = nullptr;
    QTreeWidget     *m_historyTree         = nullptr;
    QPushButton     *m_historyMore         = nullptr;
    bool             m_historyExpanded     = false;
    int              m_historyLoaded       = 0;

    QPushButton     *m_stashMenuBtn        = nullptr;

    // ─── State ──────────────────────────────────────────────────────────────
    QString m_repoRoot;
    QString m_currentBranch;
    AheadBehind m_aheadBehind;
    QVector<GitFileEntry> m_staged;
    QVector<GitFileEntry> m_unstaged;

    bool m_gitAvailable = true;
    bool m_refreshing   = false;

    QFileSystemWatcher *m_watcher = nullptr;
    QTimer *m_debounce = nullptr;

    // ─── Helpers (async git) ────────────────────────────────────────────────
    // Kicks `git <args>` in `cwd` (or repo root). `onDone` is invoked with
    // (stdout, stderr, ok). The QProcess is parented to `this` so the panel
    // cleans up its children at shutdown. Callers get back the QProcess* so
    // they can keep a handle if they need to cancel, but most won't.
    QProcess *runGitAsync(const QStringList &args,
                          std::function<void(const QByteArray &, const QByteArray &, bool)> onDone,
                          const QString &cwd = QString());

    // Convenience sync runner — kept for small commands (rev-parse, add,
    // reset) where async complication isn't worth the benefit. Same
    // semantics as the legacy helper.
    bool runGitSync(const QStringList &args,
                    QString *stdoutText = nullptr,
                    QString *stderrText = nullptr,
                    int timeoutMs = 5000,
                    const QString &cwd = QString());

    bool detectGitAvailable();

    // ─── Data loaders (parse porcelain v2 + friends) ────────────────────────
    void refreshStatus();
    void parsePorcelainV2(const QByteArray &out);
    void loadHistory(int limit, bool append);

    // ─── UI rendering ───────────────────────────────────────────────────────
    void rebuildChangesTree();
    void addFileRow(QTreeWidgetItem *parent, const GitFileEntry &entry, bool staged);
    void updateBranchChip();
    void updateSyncRow();
    void updateCommitHint();

    // ─── Actions ────────────────────────────────────────────────────────────
    void onCommitClicked();
    void onFetchClicked();
    void onPullClicked();
    void onPushClicked();
    void onBranchMenuClicked();
    void onStashMenuClicked();
    void toggleHistory();
    void loadMoreHistory();

    // File-row actions (triggered from inline [+]/[−] buttons + double-click)
    void stagePath(const QString &path);
    void unstagePath(const QString &path);
    void discardPath(const QString &path);
    void openFilePath(const QString &path);
    void openDiffForPath(const QString &path);
    // v0.1.48 — inline diff: show / hide / refresh.
    void showInlineDiffForPath(const QString &path, bool staged);
    void hideInlineDiff();
    void renderDiffText(const QString &raw);

    // ─── Context menus ──────────────────────────────────────────────────────
    void showFileContextMenu(const QPoint &pos);

    // ─── Misc ───────────────────────────────────────────────────────────────
    void scheduleDebouncedRefresh();
    void setRepoRoot(const QString &root, bool announce);
    void showGitError(const QString &title, const QString &details);
    QString relativeToRepo(const QString &path) const;

    // Applies every palette-dependent stylesheet in one place. Called
    // from the constructor and from onThemeChanged().
    void applyPalette();
};

#endif  // GITPANEL_H
