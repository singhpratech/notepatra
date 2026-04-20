#ifndef GITPANEL_H
#define GITPANEL_H

#include <QWidget>
#include <QTreeWidget>
#include <QLabel>
#include <QProcess>
#include <QPushButton>
#include <QPlainTextEdit>

class GitPanel : public QWidget {
    Q_OBJECT
public:
    explicit GitPanel(QWidget *parent = nullptr);
    void refresh(const QString &filePath);

    enum ItemKind {
        SectionItem = 0,
        FileItem,
        BranchItem,
        RemoteItem,
        StashItem
    };

signals:
    void fileClicked(const QString &path);
    void repositoryOpened(const QString &path);

private:
    QLabel *m_branchLabel;
    QLabel *m_statusLabel;
    QLabel *m_remoteLabel;
    QTreeWidget *m_tree;
    QString m_repoRoot;
    QString m_currentBranch;

    // VS Code-style SOURCE CONTROL header — branch pill + commit message
    // input + big commit button right at the top. See the comment in
    // the .cpp constructor for the full intended UX.
    QLabel *m_sourceControlHeader = nullptr;
    QPlainTextEdit *m_commitMessage = nullptr;
    QPushButton *m_commitVsCodeBtn = nullptr;

    QPushButton *m_cloneBtn;
    QPushButton *m_openRepoBtn;
    QPushButton *m_initBtn;
    QPushButton *m_remoteBtn;
    QPushButton *m_refreshBtn;
    QPushButton *m_fetchBtn;
    QPushButton *m_pullBtn;
    QPushButton *m_pushBtn;
    QPushButton *m_commitBtn;
    QPushButton *m_stageBtn;
    QPushButton *m_unstageBtn;
    QPushButton *m_discardBtn;
    QPushButton *m_checkoutBtn;
    QPushButton *m_branchBtn;
    QPushButton *m_mergeBtn;
    QPushButton *m_stashBtn;
    QPushButton *m_popStashBtn;

    bool runGitSync(
        const QStringList &args,
        QString *stdoutText = nullptr,
        QString *stderrText = nullptr,
        int timeoutMs = 5000,
        const QString &workingDirectory = QString()
    );

    bool ensureRepo(const QString &actionName = QString()) const;
    void setRepoRoot(const QString &repoRoot, bool announce = false);
    void refreshTree();
    void updateActionState();

    void cloneRepository();
    void openRepository();
    void initRepository();
    void connectRemote();
    void fetchRepository();
    void pullRepository();
    void pushRepository();
    void commitChanges();
    void stageSelection();
    void unstageSelection();
    void discardSelection();
    void checkoutBranch();
    void createBranch();
    void mergeBranch();
    void stashChanges();
    void popStash();
    void openSelectedFile();
    void showTreeContextMenu(const QPoint &pos);

    QString selectedRelativePath() const;
    QString selectedBranchName() const;
    QString selectedStashName() const;
    QString statusTextForCode(QChar code, bool staged) const;
    QString inferCloneDirectoryName(const QString &remoteUrl) const;
    QString selectBranchFromPrompt(const QString &title, bool excludeCurrent = false) const;
    void showGitError(const QString &title, const QString &details) const;
};

#endif
