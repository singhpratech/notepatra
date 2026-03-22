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

class Editor;

class MainWindow : public QMainWindow {
    Q_OBJECT
public:
    MainWindow();
    Editor *currentEditor();
    void openFile(const QString &path);
    SearchResultsPanel *searchResults() { return m_searchResults; }
    QSplitter *vertSplitter() { return m_vertSplitter; }

protected:
    void closeEvent(QCloseEvent *event) override;

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

    int m_newCount = 0;
    QString m_themeName = "Default (Light)";
    PluginManager m_pluginManager;
    QTimer *m_autoSaveTimer = nullptr;
    QFileSystemWatcher *m_fileWatcher = nullptr;
    QMap<QString, QDateTime> m_fileTimestamps;  // track last known modification time
    TerminalWidget *m_terminal = nullptr;
    MarkdownPreview *m_mdPreview = nullptr;
    AIPanel *m_aiPanel = nullptr;
    RestClient *m_restClient = nullptr;
    GitPanel *m_gitPanel = nullptr;
    SqlFmtPanel *m_sqlFmtPanel = nullptr;
    SearchResultsPanel *m_searchResults = nullptr;
    QSplitter *m_vertSplitter = nullptr;
};

#endif
