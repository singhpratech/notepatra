// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QMainWindow>
#include <QPointer>
#include <QSplitter>
#include <QFileSystemWatcher>
#include <QDateTime>
#include <QHash>
#include <QMap>
#include <QSet>
#include <QVector>
#include "tabmanager.h"
#include "statusbar.h"
#include "fileexplorer.h"
#include <QToolButton>
#include "functionlist.h"
#include "findreplace.h"
#include "plugin.h"
#include "terminal.h"
#include "markdownpreview.h"
#include "aipanel.h"
#include "restclient.h"
#include "gitpanel.h"
#include "sqlfmtpanel.h"
#include "searchresults.h"
#include "welcome.h"
#include "projectsearch.h"
#include "themes.h"
#include "config.h"
// QVector<TodoRow> member (m_noterUndelivered) needs the complete type.
// notes_todos.h is QtCore/Sql-only; Qt5::Sql is already linked app-wide.
#include "notes_todos.h"
#include <Qsci/qscimacro.h>

class Editor;
class QMenu;
class NotesPanel;
class NotesReminderEngine;

class MainWindow : public QMainWindow {
    Q_OBJECT
public:
    // standaloneNoSession (--new / hung-primary fallback windows): never
    // reads or writes session/recovery state — can't clobber the primary's
    // session.json or destroy its crash evidence.
    explicit MainWindow(bool standaloneNoSession = false);
    Editor *currentEditor();

    // ── Workspace roots ──────────────────────────────────────────────────
    //
    // These are four DIFFERENT questions and a single answer cannot serve them.
    // Collapsing them is what produced the original privacy bug, and collapsing
    // them a second time (folder-else-current-file) reproduced it: with one file
    // open in $HOME, "the current file's directory" IS $HOME, so a search that
    // trusted it walked the whole profile again.
    //
    // Rule of thumb: anything that SCOPES A SEARCH OR A SANDBOX must use
    // workspaceFolder() and must treat "" as a refusal to act — never as a cue
    // to fall back to something broader.

    /// The folder the user explicitly opened. "" when they never opened one.
    /// The only safe root for scoping a filesystem walk or a sandbox.
    QString workspaceFolder() const;

    /// Directory of the FIRST file-backed tab, or "". Answers "does the user
    /// have anything concrete open?" without caring which tab has focus — a
    /// Terminal or Welcome tab being current must not mean "nothing is open".
    QString firstOpenFileDir() const;

    /// Folder to pre-fill a file dialog with. May fall back to the current
    /// file's directory: the user SEES and can change this value, so a broad
    /// default is a convenience here rather than a silent scope. Never use it
    /// to bound a search.
    QString suggestedDialogFolder() const;

    /// The AI's project root, STICKY for the session.
    ///
    /// Latches the first non-empty root and keeps it until the user explicitly
    /// opens a folder. It cannot be a function of the focused tab: AIPanel
    /// treats any root change as a project switch — cancelling pending write
    /// approvals and re-keying chat history by sha1(root) — so a per-tab root
    /// made Ctrl+Tab silently swap the user's conversation, even between two
    /// files in one repository.
    QString aiWorkspaceRoot();
    void openFile(const QString &path);
    SearchResultsPanel *searchResults() { return m_searchResults; }
    QSplitter *vertSplitter() { return m_vertSplitter; }

    // Called by the single-instance bridge in main.cpp when a second
    // `notepatra path/to/file` invocation forwards its args into the
    // already-running process. Raises + activates immediately (so the
    // user's double-click feels instant), then opens each file from a
    // queued slot one event-loop turn later — a paint lands between the
    // raise and the load. Jumps to `gotoLine` in the last file if > 0.
    void handleRemoteOpen(const QStringList &paths, int gotoLine,
                          const QByteArray &startupId = QByteArray());

    // D1 cold-start ordering — CLI args are applied by the deferred startup
    // slot after the window is shown. Call before the event loop starts.
    void setStartupActions(const QStringList &files, int gotoLine);
    // Session restore + CLI/queued remote opens, run synchronously once.
    // Idempotent: fired by a 0 ms timer from the ctor; tests call it directly.
    void runStartupNow();

    // Public so the session-autosave contract test can drive ticks directly.
    void saveSession();

    // Post-show non-modal notice queue (D5) — also used by main() for the
    // crash-recovery / standalone-fallback startup notices.
    void queueStartupNotice(const QString &msg);

    // Bounded close-all sweep. The old `while (count() > 0) closeTab(0)`
    // looped forever against closeTab's newFile-on-zero backfill, and
    // Cancel on a modified tab re-prompted forever. Cancel aborts the sweep.
    void closeAllTabs();

    // v0.1.70 — AI dock visibility public API. setAiDockVisible() is the
    // single source of truth for whether the AI dock is on screen.
    // showAiDockForInvocation() is the auto-open helper called by AI
    // feature entry points (Ctrl+I, Composer triggers, etc.) so the user
    // always sees AI output regardless of dock state. Config::aiDockVisible
    // is written synchronously on every toggle so the layout survives
    // quit/relaunch.
    void setAiDockVisible(bool show);
    bool isAiDockVisible() const;
    void showAiDockForInvocation();

signals:
    // Emitted after applyThemeToAll() has updated Config::theme +
    // the Scintilla editors + statusbar + Welcome + main-window stylesheet.
    // Theme-aware panels (AIPanel, ProjectSearch, RestClient, GitPanel,
    // Compare, MarkdownPreview, HexEditor, Terminal) listen for this so
    // they can re-render against the new palette without an app restart.
    void themeChanged();

protected:
    void closeEvent(QCloseEvent *event) override;
    void showEvent(QShowEvent *event) override;
    void changeEvent(QEvent *event) override;
    void dragEnterEvent(QDragEnterEvent *event) override;
    void dropEvent(QDropEvent *event) override;

private:
    void buildMenus();
    void buildToolbar();
    void setupShortcuts();
    void applyTheme(const QString &name);

    Editor *newFile();
    void saveFile();
    void saveFileAs();
    void closeTab(int index);

    void updateTitle();
    void updateStatusBar();
    void updateDocStats();
    void updateTabTitle(int index);

    // Session persistence + crash recovery
    void restoreSession();
    void setupFileWatcher();
    void checkFileChanges();
    QString sessionFilePath();
    QString recoveryDir();

    // D5 — modal hygiene. Pre-show load failures queue here and flush after
    // first show as ONE combined non-modal dialog; the file watcher defers,
    // coalesces, and debounces its prompts so they can never stack or fire
    // against a never-shown window.
    void flushStartupNotices();
    void onWatchedFileChanged(const QString &path);
    QStringList m_startupNotices;
    bool m_startupNoticeFlushScheduled = false;
    bool m_everShown = false;
    QSet<QString> m_fileChangePromptOpen;
    QHash<QString, qint64> m_fileChangePromptClosedMs;
    QSet<QString> m_fileChangeRecheckPending;
    QSet<QString> m_deferredFileChangePaths;
    // One watcher prompt at a time across ALL paths — a second fileChanged
    // delivered into the first prompt's nested event loop stacked another
    // modal and mutated the tab list under the first one's saved index.
    bool m_anyFileChangePromptOpen = false;
    void drainDeferredFileChanges();
    int tabIndexForPath(const QString &path) const;

    TabManager *m_tabs;
    NppStatusBar *m_statusBar;
    FileExplorer *m_explorer;
    FunctionList *m_funcList;
    FindReplaceDialog *m_findDialog = nullptr;
    QSplitter *m_splitter;
    // v0.1.70 — internal horizontal splitter inside m_aiDockHost holding
    // [m_explorer | m_aiDockPanel]. Lets the file tree live inside the
    // AI dock surface rather than as a separate leftmost sidebar.
    QSplitter *m_aiDockInternalSplit = nullptr;
    // v0.1.70 — VS Code-style activity strip: thin vertical 32px bar on
    // the left edge of m_aiDockHost. Currently holds a single 📁 toggle
    // button for the file tree; designed for future icons (search-in-AI-
    // context, git context, etc.). The toggle button is a member so the
    // codingModeRequested signal handler can keep it in sync with the
    // actual FileExplorer visibility state.
    QToolButton *m_explorerToggleBtn = nullptr;
    QWidget *m_minimapContainer;

    void updateRecentMenu();
    void applyThemeToAll(const Theme &theme);
    // v0.1.42 — pushes Config to every editor tab + chrome (toolbar
    // visibility, tabs-closable). Called after Preferences OK/Apply
    // and at startup so every option in the Preferences dialog
    // actually takes effect across the whole app.
    void applyConfigEverywhere();
    // v0.1.42 — refresh the View menu's checkable actions from the
    // active editor's actual state (so the checkmark mirrors reality
    // after a tab switch). Called by m_tabs::currentChanged.
    void syncViewMenuToActiveEditor();
    // Show / focus the Welcome tab. Opens a new one if not already open,
    // otherwise switches to the existing one. Returns the tab index.
    int showWelcomeTab();
    // Trigger a menu action by its action-name prefix — used by the
    // Welcome tab's feature cards so they can click straight into
    // AI Assistant / Terminal / Compare etc.
    void triggerMenuAction(const QString &actionId);
    // Shared picker used by both "Compare (inbuilt)" and "ComparePlus" menu
    // entries — pops a 2-step picker for LEFT/RIGHT (any open tab or any
    // file on disk), then opens a CompareWidget tab with the chosen pair.
    void openComparePicker(const QString &tabLabel);

    // Update check — hits GitHub Releases API and compares tag_name with
    // NOTEPATRA_VERSION. `silent=true` skips the "you're up to date" dialog
    // (used by the optional check-on-startup path so we don't nag users).
    void checkForUpdates(bool silent);

    int m_newCount = 0;
    QMenu *m_recentMenu = nullptr;
    PluginManager m_pluginManager;
    QTimer *m_autoSaveTimer = nullptr;
    QTimer *m_wordCountTimer = nullptr;
    // Fallback/--new window: never reads or writes session/recovery state.
    bool m_standaloneNoSession = false;
    // Compact metadata fingerprint of the last session.json actually written.
    QByteArray m_lastSessionMeta;
    // restoreSession() skipped because a LIVE instance owns the .restoring
    // marker — runStartupNow must not overwrite or remove that marker.
    bool m_restoreSkippedLiveOwner = false;
    bool m_startupDone = false;
    QStringList m_startupFiles;
    int m_startupGotoLine = -1;
    // The working directory the process was launched from, captured once at
    // construction. The MCP git verbs fall back to it as a last-resort repo
    // root so `git_status` works when an agent launches Notepatra from inside
    // a checkout with no folder open and only untitled tabs (issue #3).
    QString m_startupCwd;
    // D2 remote-open: raise/activate happens synchronously in
    // handleRemoteOpen; file loads queue here and run one event-loop turn
    // later so a paint lands between raise and load. While startup is
    // pending (!m_startupDone, D1) the flush stays parked; runStartupNow()
    // re-schedules it after the deferred session restore.
    struct RemoteOpenRequest { QStringList paths; int gotoLine; };
    QVector<RemoteOpenRequest> m_pendingRemoteOpens;
    bool m_remoteFlushQueued = false;
    // Re-entrancy guard: a forward arriving while openFile sits in a load
    // modal (large-file confirm) must not re-enter the drain loop — the
    // half-constructed first editor isn't in the tab bar yet, so the
    // already-open check would miss it and duplicate the tab.
    bool m_remoteFlushActive = false;
    void scheduleRemoteOpenFlush();
    void flushPendingRemoteOpens();
    QFileSystemWatcher *m_fileWatcher = nullptr;
    QMap<QString, QDateTime> m_fileTimestamps;  // track last known modification time
    TerminalWidget *m_terminal = nullptr;
    MarkdownPreview *m_mdPreview = nullptr;
    AIPanel *m_aiPanel = nullptr;
    // Cursor-style right dock — AI Assistant docked on the right so
    // users get the classic 3-column coding layout (file tree | editor
    // | AI chat). Toggleable from View menu / toolbar / Coding Mode.
    QWidget *m_aiDockHost = nullptr;
    AIPanel *m_aiDockPanel = nullptr;
    bool m_aiDockSizedOnce = false;  // v0.1.48 — split 50/50 on first show
    // v0.1.56 — saved state for the AI fullscreen toggle. When the user
    // expands the AI dock, we hide sibling widgets and squash the splitter
    // to give the dock 100 % of the width. These two members let the
    // restore branch reverse that exactly.
    QList<int> m_aiSavedSplitterSizes;
    QHash<QWidget*, bool> m_aiSavedSiblingVisibility;
    void toggleAiDock();
    // v0.1.67 — gracefully exits AI dock fullscreen if active so a newly-
    // opened tool tab is visible alongside the (now-shrunk) AI conversation.
    // Called from every tool action that adds a tab via m_tabs->addTab().
    // The AI session itself is preserved — AIPanel widget is never destroyed,
    // only visually resized back to its docked width.
    void exitAiFullscreenIfActive();
    // v0.1.70 — extracted from toggleAiDock so the constructor can reuse it
    // on first-launch when Config::aiDockVisible is true.
    void rebalanceAiDockSplit();
    // v0.1.68 — when a programmatic tab switch fires QTabWidget::currentChanged
    // we sometimes want to suppress the auto-exit-fullscreen side-effect.
    // Specifically: newFile() (Ctrl+N) and openFile() create a new tab AND
    // call setCurrentIndex(idx) to focus it. The v0.1.61 UX rule is that
    // a Ctrl+N issued while the AI dock is fullscreen should NOT collapse
    // the AI dock — the new tab becomes the focused tab but stays hidden
    // behind the still-fullscreen AI dock until the user exits manually.
    // Set this flag to true *immediately before* a programmatic setCurrentIndex
    // that should not collapse the dock; the currentChanged slot consumes
    // (and resets) the flag. User-initiated tab switches (Ctrl+Tab, click
    // in the tab bar) never set this flag, so they correctly exit the dock.
    bool m_skipAiAutoExitOnNextTabChange = false;
    // v0.1.70 — one-shot guard for the "Open Folder / Open File / Skip"
    // picker that fires on first Coding-mode entry without a workspace.
    // Resets on app restart; within one session, the picker shows once
    // per Coding entry and then stays out of the way.
    bool m_codingFolderPromptShown = false;
    // Sticky AI project root — see aiWorkspaceRoot(). Latched once, then only
    // replaced when the user explicitly opens a different folder.
    QString m_aiWorkspaceLatched;
    // Push current workspace state (all open editor tabs, current file,
    // selection, workspace root) into an AIPanel so the model can reason
    // about cross-file questions like Cursor / Copilot.
    void populateAiContext(AIPanel *panel);
    RestClient *m_restClient = nullptr;
    GitPanel *m_gitPanel = nullptr;
    SqlFmtPanel *m_sqlFmtPanel = nullptr;
    SearchResultsPanel *m_searchResults = nullptr;
    QSplitter *m_vertSplitter = nullptr;

    // App-lifetime Noter reminder service (audit fix: reminders used to die
    // with the Noter tab). Lazily created — see ensureNoterReminderService().
    // OS-level scheduling (firing while the app is CLOSED) is out of scope;
    // reminders missed while closed arrive as ONE catch-up digest at launch.
    NotesTodos          *m_noterTodos          = nullptr;
    NotesReminderEngine *m_noterReminderEngine = nullptr;
    QAction             *m_noterAct            = nullptr;  // truthful checked-state
    QString m_noterToastNote;        // click-to-open target for the last toast/digest
    qint64  m_noterToastShownMs = 0; // messageClicked relevance window
    QVector<TodoRow> m_noterUndelivered;  // toast failed AND no panel existed
    void ensureNoterReminderService();
    NotesPanel *findNoterPanel(int *indexOut = nullptr) const;
    NotesPanel *ensureNoterTab();
    int newDiagramTab(const QString &source, const QString &title); // Features->Diagram + MCP create_diagram
    QAction *m_passwordAct = nullptr;  // truthful checked-state
    QPointer<Editor> m_lastEditor;     // last EDITOR tab; tool panels hand back to it
    int newPasswordTab();                                          // Tools->Password Generator
    void onNoterTrayMessageClicked();

    // Macro recording/playback
    QsciMacro *m_macro = nullptr;
    bool m_macroRecording = false;
    QString m_savedMacro;           // serialised macro for persistence across tab switches
    QAction *m_macroStartAct = nullptr;
    QAction *m_macroStopAct = nullptr;
    QAction *m_macroPlayAct = nullptr;
    QAction *m_macroRunMultiAct = nullptr;
    QAction *m_macroSaveAct = nullptr;
    QAction *m_macroLoadAct = nullptr;
    void macroUpdateActions();
    void macroEnsureObject();       // (re)create QsciMacro on current editor
};

#endif
