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
    void handleRemoteOpen(const QStringList &paths, int gotoLine);

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
    QWidget *m_minimapContainer;

    void updateRecentMenu();
    void applyThemeToAll(const Theme &theme);
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
    void toggleAiDock();
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
