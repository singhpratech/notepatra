// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QMainWindow>
#include <QSplitter>
#include <QFileSystemWatcher>
#include <QDateTime>
#include <QMap>
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
#include <Qsci/qscimacro.h>

class Editor;
class QMenu;

class MainWindow : public QMainWindow {
    Q_OBJECT
public:
    MainWindow();
    Editor *currentEditor();
    void openFile(const QString &path);
    SearchResultsPanel *searchResults() { return m_searchResults; }
    QSplitter *vertSplitter() { return m_vertSplitter; }

    // Called by the single-instance bridge in main.cpp when a second
    // `notepatra path/to/file` invocation forwards its args into the
    // already-running process. Opens each file as a tab, jumps to
    // `gotoLine` in the first file if > 0, then raises + activates
    // the window so the user's double-click feels instant.
    void handleRemoteOpen(const QStringList &paths, int gotoLine,
                          const QByteArray &startupId = QByteArray());

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
    void updateTabTitle(int index);

    // Session persistence + crash recovery
    void saveSession();
    void restoreSession();
    void autoSaveRecovery();
    void checkCrashRecovery();
    void setupFileWatcher();
    void checkFileChanges();
    QString sessionFilePath();
    QString recoveryDir();

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
    // Push current workspace state (all open editor tabs, current file,
    // selection, workspace root) into an AIPanel so the model can reason
    // about cross-file questions like Cursor / Copilot.
    void populateAiContext(AIPanel *panel);
    RestClient *m_restClient = nullptr;
    GitPanel *m_gitPanel = nullptr;
    SqlFmtPanel *m_sqlFmtPanel = nullptr;
    SearchResultsPanel *m_searchResults = nullptr;
    QSplitter *m_vertSplitter = nullptr;

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
