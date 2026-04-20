#include "gitpanel.h"

#include <QApplication>
#include <QColor>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QFont>
#include <QHeaderView>
#include <QHBoxLayout>
#include <QInputDialog>
#include <QLineEdit>
#include <QMap>
#include <QMenu>
#include <QMessageBox>
#include <QPushButton>
#include <QShortcut>
#include <QRegularExpression>
#include <QStyle>
#include <QTreeWidgetItem>
#include <QVBoxLayout>

namespace {

enum GitTreeDataRole {
    KindRole = Qt::UserRole + 1,
    PathRole,
    MetaRole
};

struct StatusEntry {
    QString displayPath;
    QString actualPath;
    QString state;
    QString detail;
    QColor color;
};

void styleActionButton(QPushButton *button, const QString &accent) {
    button->setFixedHeight(25);
    button->setStyleSheet(QString(
        "QPushButton { background: #2B2B2B; color: #DCDCDC; border: 1px solid #3A3A3A; "
        "border-radius: 6px; padding: 0 10px; }"
        "QPushButton:hover:enabled { background: #323232; border: 1px solid %1; }"
        "QPushButton:pressed:enabled { background: #242424; }"
        "QPushButton:disabled { color: #666; border: 1px solid #303030; background: #242424; }")
        .arg(accent));
}

QTreeWidgetItem *makeSectionItem(QTreeWidget *tree, const QString &name, const QString &detail, const QColor &accent) {
    auto *item = new QTreeWidgetItem(tree);
    item->setText(0, name);
    item->setText(2, detail);
    item->setData(0, KindRole, GitPanel::SectionItem);
    item->setFirstColumnSpanned(false);

    QFont font = item->font(0);
    font.setBold(true);
    item->setFont(0, font);
    item->setFont(1, font);
    item->setForeground(0, accent);
    item->setForeground(2, accent.lighter(120));
    item->setExpanded(true);
    item->setFlags(item->flags() & ~Qt::ItemIsSelectable);
    return item;
}

QTreeWidgetItem *makeChildSection(QTreeWidgetItem *parent, const QString &name, int count, const QColor &accent) {
    auto *item = new QTreeWidgetItem(parent);
    item->setText(0, name);
    item->setText(1, count == 0 ? "clean" : QString::number(count));
    item->setData(0, KindRole, GitPanel::SectionItem);

    QFont font = item->font(0);
    font.setBold(true);
    item->setFont(0, font);
    item->setForeground(0, accent);
    item->setForeground(1, count == 0 ? QColor("#6C6C6C") : accent.lighter(120));
    item->setExpanded(true);
    item->setFlags(item->flags() & ~Qt::ItemIsSelectable);
    return item;
}

void addFileEntry(QTreeWidgetItem *parent, const StatusEntry &entry) {
    auto *item = new QTreeWidgetItem(parent);
    item->setText(0, entry.displayPath);
    item->setText(1, entry.state);
    item->setText(2, entry.detail);
    item->setData(0, KindRole, GitPanel::FileItem);
    item->setData(0, PathRole, entry.actualPath);
    item->setForeground(0, entry.color);
    item->setForeground(1, entry.color);
    item->setForeground(2, QColor("#A5A5A5"));
}

QColor colorForGitCode(QChar code, bool staged) {
    switch (code.toLatin1()) {
    case 'A': return QColor("#76D275");
    case 'M': return staged ? QColor("#F2C14E") : QColor("#FFD98A");
    case 'D': return QColor("#F48771");
    case 'R': return QColor("#7EC8FF");
    case 'C': return QColor("#7EC8FF");
    case 'U': return QColor("#FF8FB1");
    case 'T': return QColor("#C792EA");
    default: return QColor("#B0BEC5");
    }
}

QString normalizedPathFromStatus(const QString &pathInfo) {
    const int arrowIndex = pathInfo.indexOf(" -> ");
    if (arrowIndex >= 0)
        return pathInfo.mid(arrowIndex + 4);
    return pathInfo;
}

}  // namespace

GitPanel::GitPanel(QWidget *parent) : QWidget(parent) {
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);

    // ─── VS Code-style SOURCE CONTROL header ──────────────────────────
    // Matches the iconic top-of-sidebar block: uppercased section label
    // + branch pill. Users who come from VS Code immediately know where
    // they are.
    m_sourceControlHeader = new QLabel("  SOURCE CONTROL");
    m_sourceControlHeader->setFixedHeight(24);
    m_sourceControlHeader->setStyleSheet(
        "background: #252526; color: #CCCCCC; padding: 4px 10px; "
        "font-size: 11px; font-weight: 600; letter-spacing: 0.08em;");
    layout->addWidget(m_sourceControlHeader);

    m_branchLabel = new QLabel("  No repository open");
    m_branchLabel->setFixedHeight(28);
    m_branchLabel->setStyleSheet(
        "background: #1E1E1E; color: #4EC9B0; padding: 4px 10px; "
        "font-size: 12px; font-weight: 600;");
    layout->addWidget(m_branchLabel);

    // ─── Commit-message box — the star of the VS Code SCM UX ──────────
    // A 3-line plain-text input styled like the editor itself, followed
    // by a prominent Commit button. Ctrl+Enter commits. This is the
    // single biggest reason VS Code's Git panel feels ergonomic: the
    // user flow is Stage → Type Message → Commit all in one place,
    // without a modal dialog blocking the screen.
    auto *commitWrap = new QWidget;
    auto *commitLay = new QVBoxLayout(commitWrap);
    commitLay->setContentsMargins(10, 8, 10, 10);
    commitLay->setSpacing(6);

    m_commitMessage = new QPlainTextEdit;
    m_commitMessage->setPlaceholderText("Message (Ctrl+Enter to commit)");
    m_commitMessage->setFixedHeight(72);
    m_commitMessage->setStyleSheet(
        "QPlainTextEdit { background: #1E1E1E; color: #D4D4D4; "
        "border: 1px solid #3C3C3C; border-radius: 4px; padding: 6px 8px; "
        "font-size: 12px; }"
        "QPlainTextEdit:focus { border-color: #4EC9B0; }");
    commitLay->addWidget(m_commitMessage);

    m_commitVsCodeBtn = new QPushButton("✓  Commit");
    m_commitVsCodeBtn->setFixedHeight(30);
    m_commitVsCodeBtn->setCursor(Qt::PointingHandCursor);
    m_commitVsCodeBtn->setStyleSheet(
        "QPushButton { background: #16825D; color: #FFFFFF; border: none; "
        "border-radius: 4px; font-weight: 600; font-size: 12px; }"
        "QPushButton:hover { background: #1B9868; }"
        "QPushButton:disabled { background: #333; color: #666; }");
    m_commitVsCodeBtn->setEnabled(false);
    commitLay->addWidget(m_commitVsCodeBtn);

    layout->addWidget(commitWrap);

    // Wire the VS Code commit button to the existing commitChanges logic
    // but pre-fill the dialog with our message so users never have to
    // re-type it. (The existing commitChanges() uses QInputDialog — we
    // short-circuit that here by reading directly from m_commitMessage.)
    connect(m_commitVsCodeBtn, &QPushButton::clicked, this, [this]() {
        const QString msg = m_commitMessage->toPlainText().trimmed();
        if (msg.isEmpty()) return;
        QString stdoutText, stderrText;
        bool ok = runGitSync({"commit", "-m", msg}, &stdoutText, &stderrText);
        if (!ok) {
            showGitError("Commit failed", stderrText.isEmpty() ? stdoutText : stderrText);
            return;
        }
        m_commitMessage->clear();
        refreshTree();
        updateActionState();
    });

    // Ctrl+Enter inside the commit message triggers commit — matches
    // VS Code's Git SCM keybinding.
    {
        auto *sc = new QShortcut(QKeySequence("Ctrl+Return"), m_commitMessage);
        sc->setContext(Qt::WidgetShortcut);
        connect(sc, &QShortcut::activated, m_commitVsCodeBtn, &QPushButton::click);
    }
    connect(m_commitMessage, &QPlainTextEdit::textChanged, this, [this]() {
        const bool hasText = !m_commitMessage->toPlainText().trimmed().isEmpty();
        const bool hasRepo = !m_repoRoot.isEmpty();
        m_commitVsCodeBtn->setEnabled(hasText && hasRepo);
    });

    m_remoteLabel = new QLabel("  Open, clone, or initialize a repository to start.");
    m_remoteLabel->setFixedHeight(22);
    m_remoteLabel->setStyleSheet(
        "background: #202326; color: #7EC8FF; padding: 2px 8px; font-size: 11px;");
    layout->addWidget(m_remoteLabel);

    m_statusLabel = new QLabel("  Working tree is idle");
    m_statusLabel->setFixedHeight(22);
    m_statusLabel->setStyleSheet(
        "background: #252526; color: #9A9A9A; padding: 2px 8px; font-size: 11px;");
    layout->addWidget(m_statusLabel);

    auto *repoRow = new QHBoxLayout;
    repoRow->setContentsMargins(6, 6, 6, 3);
    repoRow->setSpacing(6);
    m_cloneBtn = new QPushButton("Clone...");
    m_openRepoBtn = new QPushButton("Open Repo...");
    m_initBtn = new QPushButton("Init");
    m_remoteBtn = new QPushButton("Connect Remote...");
    m_refreshBtn = new QPushButton("Refresh");
    for (auto *button : {m_cloneBtn, m_openRepoBtn, m_initBtn, m_remoteBtn, m_refreshBtn}) {
        styleActionButton(button, "#4EC9B0");
        repoRow->addWidget(button);
    }
    repoRow->addStretch(1);
    layout->addLayout(repoRow);

    auto *syncRow = new QHBoxLayout;
    syncRow->setContentsMargins(6, 0, 6, 3);
    syncRow->setSpacing(6);
    m_fetchBtn = new QPushButton("Fetch");
    m_pullBtn = new QPushButton("Pull");
    m_pushBtn = new QPushButton("Push");
    m_commitBtn = new QPushButton("Commit...");
    for (auto *button : {m_fetchBtn, m_pullBtn, m_pushBtn, m_commitBtn}) {
        styleActionButton(button, "#7EC8FF");
        syncRow->addWidget(button);
    }
    syncRow->addStretch(1);
    layout->addLayout(syncRow);

    auto *worktreeRow = new QHBoxLayout;
    worktreeRow->setContentsMargins(6, 0, 6, 6);
    worktreeRow->setSpacing(6);
    m_stageBtn = new QPushButton("Stage");
    m_unstageBtn = new QPushButton("Unstage");
    m_discardBtn = new QPushButton("Discard");
    m_checkoutBtn = new QPushButton("Checkout...");
    m_branchBtn = new QPushButton("New Branch...");
    m_mergeBtn = new QPushButton("Merge...");
    m_stashBtn = new QPushButton("Stash");
    m_popStashBtn = new QPushButton("Pop Stash");
    for (auto *button : {m_stageBtn, m_unstageBtn, m_discardBtn, m_checkoutBtn,
                         m_branchBtn, m_mergeBtn, m_stashBtn, m_popStashBtn}) {
        styleActionButton(button, "#F2C14E");
        worktreeRow->addWidget(button);
    }
    worktreeRow->addStretch(1);
    layout->addLayout(worktreeRow);

    m_tree = new QTreeWidget;
    m_tree->setHeaderLabels({"Git Tree", "State", "Details"});
    m_tree->setRootIsDecorated(true);
    m_tree->setAlternatingRowColors(true);
    m_tree->setUniformRowHeights(true);
    m_tree->setSelectionMode(QAbstractItemView::SingleSelection);
    m_tree->header()->setStretchLastSection(true);
    m_tree->header()->resizeSection(0, 260);
    m_tree->header()->resizeSection(1, 120);
    m_tree->setContextMenuPolicy(Qt::CustomContextMenu);
    m_tree->setStyleSheet(
        "QTreeWidget { background: #17191C; color: #D8D8D8; border: none; "
        "alternate-background-color: #1C1F22; outline: 0; }"
        "QTreeWidget::item { padding: 3px 4px; }"
        "QTreeWidget::item:selected { background: #203246; color: #F3F7FA; }"
        "QHeaderView::section { background: #202326; color: #AFC3D6; border: none; padding: 5px; }");
    layout->addWidget(m_tree, 1);

    connect(m_cloneBtn, &QPushButton::clicked, this, &GitPanel::cloneRepository);
    connect(m_openRepoBtn, &QPushButton::clicked, this, &GitPanel::openRepository);
    connect(m_initBtn, &QPushButton::clicked, this, &GitPanel::initRepository);
    connect(m_remoteBtn, &QPushButton::clicked, this, &GitPanel::connectRemote);
    connect(m_refreshBtn, &QPushButton::clicked, this, [this]() {
        if (!m_repoRoot.isEmpty()) refreshTree();
    });
    connect(m_fetchBtn, &QPushButton::clicked, this, &GitPanel::fetchRepository);
    connect(m_pullBtn, &QPushButton::clicked, this, &GitPanel::pullRepository);
    connect(m_pushBtn, &QPushButton::clicked, this, &GitPanel::pushRepository);
    connect(m_commitBtn, &QPushButton::clicked, this, &GitPanel::commitChanges);
    connect(m_stageBtn, &QPushButton::clicked, this, &GitPanel::stageSelection);
    connect(m_unstageBtn, &QPushButton::clicked, this, &GitPanel::unstageSelection);
    connect(m_discardBtn, &QPushButton::clicked, this, &GitPanel::discardSelection);
    connect(m_checkoutBtn, &QPushButton::clicked, this, &GitPanel::checkoutBranch);
    connect(m_branchBtn, &QPushButton::clicked, this, &GitPanel::createBranch);
    connect(m_mergeBtn, &QPushButton::clicked, this, &GitPanel::mergeBranch);
    connect(m_stashBtn, &QPushButton::clicked, this, &GitPanel::stashChanges);
    connect(m_popStashBtn, &QPushButton::clicked, this, &GitPanel::popStash);
    connect(m_tree, &QTreeWidget::customContextMenuRequested, this, &GitPanel::showTreeContextMenu);
    connect(m_tree, &QTreeWidget::currentItemChanged, this, [this](QTreeWidgetItem *, QTreeWidgetItem *) {
        updateActionState();
    });
    connect(m_tree, &QTreeWidget::itemDoubleClicked, this, [this](QTreeWidgetItem *item, int) {
        if (!item) return;
        switch (item->data(0, KindRole).toInt()) {
        case FileItem:
            openSelectedFile();
            break;
        case BranchItem:
            checkoutBranch();
            break;
        case StashItem:
            popStash();
            break;
        default:
            break;
        }
    });

    updateActionState();
}

void GitPanel::refresh(const QString &filePath) {
    const QString probePath = QFileInfo(filePath).isDir() ? filePath : QFileInfo(filePath).path();
    if (probePath.isEmpty()) {
        setRepoRoot(QString(), false);
        m_tree->clear();
        m_branchLabel->setText("  Git Workspace");
        m_remoteLabel->setText("  Open, clone, or initialize a repository to start.");
        m_statusLabel->setText("  Open a file or repository folder to inspect Git state.");
        updateActionState();
        return;
    }

    QString rootOut;
    QString rootErr;
    if (!runGitSync({"rev-parse", "--show-toplevel"}, &rootOut, &rootErr, 3000, probePath)) {
        setRepoRoot(QString(), false);
        m_tree->clear();
        m_branchLabel->setText("  Git: (not a repository)");
        m_remoteLabel->setText("  Clone a repository, open one, or initialize Git here.");
        m_statusLabel->setText("  Open a file inside a Git repository to manage it.");
        updateActionState();
        return;
    }

    setRepoRoot(rootOut.trimmed(), false);
    refreshTree();
}

bool GitPanel::runGitSync(const QStringList &args, QString *stdoutText, QString *stderrText,
                          int timeoutMs, const QString &workingDirectory) {
    QProcess proc;
    const QString runDir = workingDirectory.isEmpty() ? m_repoRoot : workingDirectory;
    if (!runDir.isEmpty())
        proc.setWorkingDirectory(runDir);
    proc.start("git", args);
    if (!proc.waitForFinished(timeoutMs)) {
        proc.kill();
        proc.waitForFinished();
        if (stdoutText) *stdoutText = QString::fromUtf8(proc.readAllStandardOutput());
        if (stderrText) *stderrText = "git command timed out";
        return false;
    }

    if (stdoutText) *stdoutText = QString::fromUtf8(proc.readAllStandardOutput());
    if (stderrText) *stderrText = QString::fromUtf8(proc.readAllStandardError());
    return proc.exitStatus() == QProcess::NormalExit && proc.exitCode() == 0;
}

bool GitPanel::ensureRepo(const QString &actionName) const {
    if (!m_repoRoot.isEmpty())
        return true;

    const QString prefix = actionName.isEmpty() ? "This action" : actionName;
    QMessageBox::information(const_cast<GitPanel *>(this), "Git",
                             prefix + " requires an opened Git repository.");
    return false;
}

void GitPanel::setRepoRoot(const QString &repoRoot, bool announce) {
    const bool changed = m_repoRoot != repoRoot;
    m_repoRoot = repoRoot;
    if (m_repoRoot.isEmpty())
        m_currentBranch.clear();
    updateActionState();
    if (announce && changed && !m_repoRoot.isEmpty())
        emit repositoryOpened(m_repoRoot);
}

void GitPanel::refreshTree() {
    m_tree->clear();

    if (!ensureRepo())
        return;

    QString statusOut;
    QString statusErr;
    if (!runGitSync({"status", "--porcelain=1", "--branch"}, &statusOut, &statusErr, 6000)) {
        showGitError("Git Status", statusErr.trimmed().isEmpty() ? "Could not read repository status." : statusErr.trimmed());
        return;
    }

    QStringList statusLines = statusOut.split('\n', Qt::SkipEmptyParts);
    QString branchSummary = "detached";
    if (!statusLines.isEmpty() && statusLines.first().startsWith("## ")) {
        branchSummary = statusLines.takeFirst().mid(3).trimmed();
        const QString branchToken = branchSummary.section("...", 0, 0).section(' ', 0, 0).trimmed();
        m_currentBranch = branchToken == "HEAD" ? QStringLiteral("(detached HEAD)") : branchToken;
    } else {
        m_currentBranch.clear();
    }

    QVector<StatusEntry> stagedEntries;
    QVector<StatusEntry> unstagedEntries;
    QVector<StatusEntry> untrackedEntries;
    QVector<StatusEntry> conflictEntries;

    for (const QString &line : statusLines) {
        if (line.size() < 3)
            continue;

        const QString xy = line.left(2);
        const QString pathInfo = line.mid(3);
        const QString actualPath = normalizedPathFromStatus(pathInfo);
        const QChar x = xy.at(0);
        const QChar y = xy.at(1);

        if (xy == "??") {
            untrackedEntries.push_back({pathInfo, actualPath, "Untracked", "new file", QColor("#76D275")});
            continue;
        }

        if (x == 'U' || y == 'U' || xy == "AA" || xy == "DD") {
            conflictEntries.push_back({pathInfo, actualPath, "Conflict", QString("state %1").arg(xy), QColor("#FF8FB1")});
            continue;
        }

        if (x != ' ' && x != '?') {
            stagedEntries.push_back({pathInfo, actualPath, statusTextForCode(x, true),
                                     QString("index %1").arg(x), colorForGitCode(x, true)});
        }
        if (y != ' ') {
            unstagedEntries.push_back({pathInfo, actualPath, statusTextForCode(y, false),
                                       QString("worktree %1").arg(y), colorForGitCode(y, false)});
        }
    }

    QString remoteOut;
    runGitSync({"remote", "-v"}, &remoteOut, nullptr, 4000);
    QMap<QString, QString> remoteFetchUrls;
    QMap<QString, QString> remotePushUrls;
    const QStringList remoteLines = remoteOut.split('\n', Qt::SkipEmptyParts);
    for (const QString &line : remoteLines) {
        const QStringList parts = line.simplified().split(' ');
        if (parts.size() < 3)
            continue;
        const QString remoteName = parts.at(0);
        const QString url = parts.at(1);
        if (parts.at(2).contains("fetch"))
            remoteFetchUrls[remoteName] = url;
        else if (parts.at(2).contains("push"))
            remotePushUrls[remoteName] = url;
    }

    QString branchesOut;
    runGitSync({"branch", "-vv", "--no-color"}, &branchesOut, nullptr, 4000);
    const QStringList branchLines = branchesOut.split('\n', Qt::SkipEmptyParts);

    QString stashOut;
    runGitSync({"stash", "list"}, &stashOut, nullptr, 4000);
    const QStringList stashLines = stashOut.split('\n', Qt::SkipEmptyParts);

    const QString repoName = QFileInfo(m_repoRoot).fileName();
    m_branchLabel->setText(QString("  Git: %1  [%2]").arg(branchSummary, repoName));

    if (remoteFetchUrls.isEmpty()) {
        m_remoteLabel->setText("  Remote: none configured");
    } else {
        const QString remoteName = remoteFetchUrls.firstKey();
        const QString fetchUrl = remoteFetchUrls.value(remoteName);
        const QString pushUrl = remotePushUrls.value(remoteName, fetchUrl);
        m_remoteLabel->setText(QString("  Remote %1  fetch: %2  push: %3")
                               .arg(remoteName, fetchUrl, pushUrl));
    }

    m_statusLabel->setText(QString("  %1 staged  %2 unstaged  %3 untracked  %4 conflicts  %5 stashes")
                           .arg(stagedEntries.size())
                           .arg(unstagedEntries.size())
                           .arg(untrackedEntries.size())
                           .arg(conflictEntries.size())
                           .arg(stashLines.size()));

    auto *workingRoot = makeSectionItem(m_tree, "Working Tree",
                                        QDir::toNativeSeparators(m_repoRoot), QColor("#7EC8FF"));
    auto *stagedRoot = makeChildSection(workingRoot, "Staged", stagedEntries.size(), QColor("#76D275"));
    auto *unstagedRoot = makeChildSection(workingRoot, "Unstaged", unstagedEntries.size(), QColor("#F2C14E"));
    auto *untrackedRoot = makeChildSection(workingRoot, "Untracked", untrackedEntries.size(), QColor("#4EC9B0"));
    auto *conflictRoot = makeChildSection(workingRoot, "Conflicts", conflictEntries.size(), QColor("#FF8FB1"));

    for (const StatusEntry &entry : stagedEntries) addFileEntry(stagedRoot, entry);
    for (const StatusEntry &entry : unstagedEntries) addFileEntry(unstagedRoot, entry);
    for (const StatusEntry &entry : untrackedEntries) addFileEntry(untrackedRoot, entry);
    for (const StatusEntry &entry : conflictEntries) addFileEntry(conflictRoot, entry);

    auto *branchRoot = makeSectionItem(m_tree, "Branches",
                                       QString("%1 local").arg(branchLines.size()), QColor("#F2C14E"));
    for (const QString &line : branchLines) {
        const bool current = line.startsWith('*');
        const QString trimmed = line.mid(current ? 1 : 0).trimmed();
        if (trimmed.isEmpty())
            continue;

        const QString branchName = trimmed.section(' ', 0, 0);
        QString details = trimmed.mid(branchName.size()).trimmed();
        auto *item = new QTreeWidgetItem(branchRoot);
        item->setText(0, branchName);
        item->setText(1, current ? "current" : "local");
        item->setText(2, details);
        item->setData(0, KindRole, BranchItem);
        item->setData(0, MetaRole, branchName);
        item->setForeground(0, current ? QColor("#7EC8FF") : QColor("#D6D6D6"));
        if (current) {
            QFont font = item->font(0);
            font.setBold(true);
            item->setFont(0, font);
            item->setFont(1, font);
        }
    }

    auto *remoteRoot = makeSectionItem(m_tree, "Remotes",
                                       QString("%1 configured").arg(remoteFetchUrls.size()), QColor("#4EC9B0"));
    for (auto it = remoteFetchUrls.constBegin(); it != remoteFetchUrls.constEnd(); ++it) {
        auto *item = new QTreeWidgetItem(remoteRoot);
        item->setText(0, it.key());
        item->setText(1, "fetch / push");
        item->setText(2, QString("%1 | %2").arg(it.value(), remotePushUrls.value(it.key(), it.value())));
        item->setData(0, KindRole, RemoteItem);
        item->setData(0, MetaRole, it.key());
        item->setForeground(0, QColor("#AEE3FF"));
        item->setForeground(1, QColor("#7FDBB6"));
    }

    auto *stashRoot = makeSectionItem(m_tree, "Stashes",
                                      QString("%1 entries").arg(stashLines.size()), QColor("#C792EA"));
    for (const QString &line : stashLines) {
        const QString stashName = line.section(':', 0, 0).trimmed();
        const QString detail = line.section(':', 2).trimmed();
        auto *item = new QTreeWidgetItem(stashRoot);
        item->setText(0, stashName);
        item->setText(1, "stash");
        item->setText(2, detail);
        item->setData(0, KindRole, StashItem);
        item->setData(0, MetaRole, stashName);
        item->setForeground(0, QColor("#D5B6FF"));
        item->setForeground(1, QColor("#C792EA"));
    }

    m_tree->expandAll();
    updateActionState();
}

void GitPanel::updateActionState() {
    const bool hasRepo = !m_repoRoot.isEmpty();
    const auto *item = m_tree->currentItem();
    const ItemKind kind = item ? static_cast<ItemKind>(item->data(0, KindRole).toInt()) : SectionItem;
    const bool fileSelected = kind == FileItem;
    const bool branchSelected = kind == BranchItem;
    const bool stashSelected = kind == StashItem;

    // VS Code-style commit button: enabled iff we have both a message
    // and a repo. Re-evaluated here so opening a repo picks up any
    // already-typed message.
    if (m_commitVsCodeBtn && m_commitMessage) {
        const bool hasText = !m_commitMessage->toPlainText().trimmed().isEmpty();
        m_commitVsCodeBtn->setEnabled(hasRepo && hasText);
    }
    if (m_branchLabel && hasRepo && !m_currentBranch.isEmpty()) {
        m_branchLabel->setText("  ⎇  " + m_currentBranch);
    }

    m_remoteBtn->setEnabled(hasRepo);
    m_refreshBtn->setEnabled(hasRepo);
    m_fetchBtn->setEnabled(hasRepo);
    m_pullBtn->setEnabled(hasRepo);
    m_pushBtn->setEnabled(hasRepo);
    m_commitBtn->setEnabled(hasRepo);
    m_stageBtn->setEnabled(hasRepo);
    m_unstageBtn->setEnabled(hasRepo);
    m_discardBtn->setEnabled(fileSelected);
    m_checkoutBtn->setEnabled(hasRepo);
    m_branchBtn->setEnabled(hasRepo);
    m_mergeBtn->setEnabled(hasRepo && (branchSelected || !m_currentBranch.isEmpty()));
    m_stashBtn->setEnabled(hasRepo);
    m_popStashBtn->setEnabled(hasRepo && (stashSelected || true));
}

void GitPanel::cloneRepository() {
    bool ok = false;
    const QString remoteUrl = QInputDialog::getText(
        this, "Clone Repository", "Remote URL:", QLineEdit::Normal, "", &ok).trimmed();
    if (!ok || remoteUrl.isEmpty())
        return;

    const QString parentDir = QFileDialog::getExistingDirectory(
        this, "Choose parent directory for clone", QDir::homePath());
    if (parentDir.isEmpty())
        return;

    QString cloneOut;
    QString cloneErr;
    if (!runGitSync({"clone", remoteUrl}, &cloneOut, &cloneErr, 120000, parentDir)) {
        showGitError("Git Clone", cloneErr.trimmed().isEmpty() ? cloneOut.trimmed() : cloneErr.trimmed());
        return;
    }

    const QString repoDirName = inferCloneDirectoryName(remoteUrl);
    const QString clonedRepo = QDir(parentDir).filePath(repoDirName);
    setRepoRoot(clonedRepo, true);
    refresh(clonedRepo);
}

void GitPanel::openRepository() {
    const QString dir = QFileDialog::getExistingDirectory(this, "Open Git Repository", QDir::homePath());
    if (dir.isEmpty())
        return;
    refresh(dir);
    if (!m_repoRoot.isEmpty())
        emit repositoryOpened(m_repoRoot);
}

void GitPanel::initRepository() {
    const QString dir = QFileDialog::getExistingDirectory(
        this, "Initialize Git Repository", m_repoRoot.isEmpty() ? QDir::homePath() : m_repoRoot);
    if (dir.isEmpty())
        return;

    QString initOut;
    QString initErr;
    if (!runGitSync({"init"}, &initOut, &initErr, 10000, dir)) {
        showGitError("Git Init", initErr.trimmed().isEmpty() ? initOut.trimmed() : initErr.trimmed());
        return;
    }

    setRepoRoot(dir, true);
    refresh(dir);
}

void GitPanel::connectRemote() {
    if (!ensureRepo("Connecting a remote"))
        return;

    bool ok = false;
    const QString remoteUrl = QInputDialog::getText(
        this, "Connect Remote", "Remote URL for origin:", QLineEdit::Normal, "", &ok).trimmed();
    if (!ok || remoteUrl.isEmpty())
        return;

    QString remotesOut;
    runGitSync({"remote"}, &remotesOut, nullptr, 3000);
    const bool hasOrigin = remotesOut.split('\n', Qt::SkipEmptyParts).contains("origin");

    QString out;
    QString err;
    const QStringList args = hasOrigin
        ? QStringList{"remote", "set-url", "origin", remoteUrl}
        : QStringList{"remote", "add", "origin", remoteUrl};
    if (!runGitSync(args, &out, &err, 10000)) {
        showGitError("Connect Remote", err.trimmed().isEmpty() ? out.trimmed() : err.trimmed());
        return;
    }

    refreshTree();
}

void GitPanel::fetchRepository() {
    if (!ensureRepo("Fetch"))
        return;
    QString out;
    QString err;
    if (!runGitSync({"fetch", "--all", "--prune"}, &out, &err, 120000)) {
        showGitError("Git Fetch", err.trimmed().isEmpty() ? out.trimmed() : err.trimmed());
        return;
    }
    m_statusLabel->setText("  Fetch complete");
    refreshTree();
}

void GitPanel::pullRepository() {
    if (!ensureRepo("Pull"))
        return;
    QString out;
    QString err;
    if (!runGitSync({"pull", "--stat"}, &out, &err, 120000)) {
        showGitError("Git Pull", err.trimmed().isEmpty() ? out.trimmed() : err.trimmed());
        return;
    }
    m_statusLabel->setText("  Pull complete");
    refreshTree();
}

void GitPanel::pushRepository() {
    if (!ensureRepo("Push"))
        return;
    QString out;
    QString err;
    if (!runGitSync({"push"}, &out, &err, 120000)) {
        showGitError("Git Push", err.trimmed().isEmpty() ? out.trimmed() : err.trimmed());
        return;
    }
    m_statusLabel->setText("  Push complete");
    refreshTree();
}

void GitPanel::commitChanges() {
    if (!ensureRepo("Commit"))
        return;

    QString statusOut;
    if (!runGitSync({"status", "--porcelain"}, &statusOut, nullptr, 5000)) {
        showGitError("Git Commit", "Could not read repository status.");
        return;
    }
    if (statusOut.trimmed().isEmpty()) {
        m_statusLabel->setText("  Nothing to commit");
        return;
    }

    const bool hasStaged = statusOut.contains(QRegularExpression("^[ MADRCUT]{1}[MADRCUT]", QRegularExpression::MultilineOption));
    if (!hasStaged) {
        const auto reply = QMessageBox::question(
            this, "Git Commit", "No staged changes found. Stage all current changes before committing?");
        if (reply != QMessageBox::Yes)
            return;
        QString stageErr;
        if (!runGitSync({"add", "-A"}, nullptr, &stageErr, 15000)) {
            showGitError("Git Commit", QString("Staging failed:\n%1").arg(stageErr.trimmed()));
            return;
        }
    }

    bool ok = false;
    const QString message = QInputDialog::getText(
        this, "Git Commit", "Commit message:", QLineEdit::Normal, "", &ok).trimmed();
    if (!ok || message.isEmpty())
        return;

    QString commitOut;
    QString commitErr;
    if (!runGitSync({"commit", "-m", message}, &commitOut, &commitErr, 30000)) {
        const QString details = commitErr.trimmed().isEmpty() ? commitOut.trimmed() : commitErr.trimmed();
        showGitError("Git Commit", details.isEmpty() ? "git commit returned a non-zero exit code." : details);
        return;
    }

    m_statusLabel->setText(QString("  Committed: %1").arg(message));
    refreshTree();
}

void GitPanel::stageSelection() {
    if (!ensureRepo("Stage"))
        return;

    QString err;
    QStringList args{"add"};
    const QString path = selectedRelativePath();
    if (path.isEmpty())
        args << "-A";
    else
        args << "--" << path;

    if (!runGitSync(args, nullptr, &err, 15000)) {
        showGitError("Git Stage", err.trimmed().isEmpty() ? "Could not stage selection." : err.trimmed());
        return;
    }
    refreshTree();
}

void GitPanel::unstageSelection() {
    if (!ensureRepo("Unstage"))
        return;

    QString err;
    QStringList args{"reset", "HEAD", "--"};
    const QString path = selectedRelativePath();
    args << (path.isEmpty() ? "." : path);

    if (!runGitSync(args, nullptr, &err, 15000)) {
        showGitError("Git Unstage", err.trimmed().isEmpty() ? "Could not unstage selection." : err.trimmed());
        return;
    }
    refreshTree();
}

void GitPanel::discardSelection() {
    if (!ensureRepo("Discard"))
        return;

    const QString path = selectedRelativePath();
    if (path.isEmpty()) {
        QMessageBox::information(this, "Discard", "Select a file in the Git tree first.");
        return;
    }

    const auto reply = QMessageBox::warning(
        this, "Discard Changes",
        QString("Discard local changes for:\n%1").arg(path),
        QMessageBox::Yes | QMessageBox::No,
        QMessageBox::No);
    if (reply != QMessageBox::Yes)
        return;

    QString statusOut;
    runGitSync({"status", "--porcelain", "--", path}, &statusOut, nullptr, 5000);
    const bool untracked = statusOut.startsWith("??");

    QString err;
    bool ok = false;
    if (untracked) {
        ok = runGitSync({"clean", "-f", "--", path}, nullptr, &err, 15000);
    } else {
        ok = runGitSync({"checkout", "--", path}, nullptr, &err, 15000);
    }

    if (!ok) {
        showGitError("Discard Changes", err.trimmed().isEmpty() ? "Could not discard selected file." : err.trimmed());
        return;
    }
    refreshTree();
}

void GitPanel::checkoutBranch() {
    if (!ensureRepo("Checkout"))
        return;

    QString branch = selectedBranchName();
    if (branch.isEmpty())
        branch = selectBranchFromPrompt("Checkout Branch", false);
    if (branch.isEmpty())
        return;

    QString out;
    QString err;
    if (!runGitSync({"checkout", branch}, &out, &err, 20000)) {
        showGitError("Git Checkout", err.trimmed().isEmpty() ? out.trimmed() : err.trimmed());
        return;
    }

    m_statusLabel->setText(QString("  Checked out %1").arg(branch));
    refreshTree();
}

void GitPanel::createBranch() {
    if (!ensureRepo("Create branch"))
        return;

    bool ok = false;
    const QString branch = QInputDialog::getText(
        this, "New Branch", "Branch name:", QLineEdit::Normal, "", &ok).trimmed();
    if (!ok || branch.isEmpty())
        return;

    QString out;
    QString err;
    if (!runGitSync({"checkout", "-b", branch}, &out, &err, 20000)) {
        showGitError("New Branch", err.trimmed().isEmpty() ? out.trimmed() : err.trimmed());
        return;
    }
    m_statusLabel->setText(QString("  Created and checked out %1").arg(branch));
    refreshTree();
}

void GitPanel::mergeBranch() {
    if (!ensureRepo("Merge"))
        return;

    QString branch = selectedBranchName();
    if (branch == m_currentBranch)
        branch.clear();
    if (branch.isEmpty())
        branch = selectBranchFromPrompt("Merge Branch Into Current", true);
    if (branch.isEmpty())
        return;

    const auto reply = QMessageBox::question(
        this, "Merge Branch",
        QString("Merge %1 into %2?").arg(branch, m_currentBranch));
    if (reply != QMessageBox::Yes)
        return;

    QString out;
    QString err;
    if (!runGitSync({"merge", branch}, &out, &err, 60000)) {
        showGitError("Git Merge", err.trimmed().isEmpty() ? out.trimmed() : err.trimmed());
        return;
    }
    m_statusLabel->setText(QString("  Merged %1").arg(branch));
    refreshTree();
}

void GitPanel::stashChanges() {
    if (!ensureRepo("Stash"))
        return;

    bool ok = false;
    const QString message = QInputDialog::getText(
        this, "Stash Changes", "Optional stash message:", QLineEdit::Normal, "", &ok).trimmed();
    if (!ok)
        return;

    QStringList args{"stash", "push", "-u"};
    if (!message.isEmpty())
        args << "-m" << message;

    QString out;
    QString err;
    if (!runGitSync(args, &out, &err, 30000)) {
        showGitError("Git Stash", err.trimmed().isEmpty() ? out.trimmed() : err.trimmed());
        return;
    }
    m_statusLabel->setText("  Changes stashed");
    refreshTree();
}

void GitPanel::popStash() {
    if (!ensureRepo("Pop stash"))
        return;

    QString stashName = selectedStashName();
    if (stashName.isEmpty())
        stashName = "stash@{0}";

    QString out;
    QString err;
    if (!runGitSync({"stash", "pop", stashName}, &out, &err, 30000)) {
        showGitError("Pop Stash", err.trimmed().isEmpty() ? out.trimmed() : err.trimmed());
        return;
    }
    m_statusLabel->setText(QString("  Popped %1").arg(stashName));
    refreshTree();
}

void GitPanel::openSelectedFile() {
    if (!ensureRepo("Open file"))
        return;

    const QString relativePath = selectedRelativePath();
    if (relativePath.isEmpty())
        return;

    const QString absolutePath = QDir(m_repoRoot).filePath(relativePath);
    if (QFileInfo::exists(absolutePath))
        emit fileClicked(absolutePath);
}

void GitPanel::showTreeContextMenu(const QPoint &pos) {
    QTreeWidgetItem *item = m_tree->itemAt(pos);
    if (!item)
        return;

    QMenu menu(this);
    const ItemKind kind = static_cast<ItemKind>(item->data(0, KindRole).toInt());
    switch (kind) {
    case FileItem: {
        menu.addAction("Open File", this, &GitPanel::openSelectedFile);
        menu.addSeparator();
        menu.addAction("Stage", this, &GitPanel::stageSelection);
        menu.addAction("Unstage", this, &GitPanel::unstageSelection);
        menu.addAction("Discard", this, &GitPanel::discardSelection);
        break;
    }
    case BranchItem: {
        menu.addAction("Checkout Branch", this, &GitPanel::checkoutBranch);
        menu.addAction("Merge Into Current", this, &GitPanel::mergeBranch);
        break;
    }
    case RemoteItem: {
        menu.addAction("Fetch", this, &GitPanel::fetchRepository);
        menu.addAction("Pull", this, &GitPanel::pullRepository);
        menu.addAction("Push", this, &GitPanel::pushRepository);
        menu.addSeparator();
        menu.addAction("Connect Remote...", this, &GitPanel::connectRemote);
        break;
    }
    case StashItem: {
        menu.addAction("Pop Stash", this, &GitPanel::popStash);
        break;
    }
    default:
        menu.addAction("Refresh", this, [this]() { refreshTree(); });
        break;
    }

    menu.exec(m_tree->viewport()->mapToGlobal(pos));
}

QString GitPanel::selectedRelativePath() const {
    if (auto *item = m_tree->currentItem())
        return item->data(0, PathRole).toString();
    return QString();
}

QString GitPanel::selectedBranchName() const {
    if (auto *item = m_tree->currentItem();
        item && item->data(0, KindRole).toInt() == BranchItem) {
        return item->data(0, MetaRole).toString();
    }
    return QString();
}

QString GitPanel::selectedStashName() const {
    if (auto *item = m_tree->currentItem();
        item && item->data(0, KindRole).toInt() == StashItem) {
        return item->data(0, MetaRole).toString();
    }
    return QString();
}

QString GitPanel::statusTextForCode(QChar code, bool staged) const {
    switch (code.toLatin1()) {
    case 'A': return staged ? "Added" : "Added locally";
    case 'M': return staged ? "Modified" : "Modified";
    case 'D': return staged ? "Deleted" : "Deleted";
    case 'R': return staged ? "Renamed" : "Renamed";
    case 'C': return "Copied";
    case 'T': return "Type changed";
    case 'U': return "Conflict";
    default: return QString(code);
    }
}

QString GitPanel::inferCloneDirectoryName(const QString &remoteUrl) const {
    QString name = remoteUrl.trimmed();
    while (name.endsWith('/'))
        name.chop(1);
    const int slashIndex = qMax(name.lastIndexOf('/'), name.lastIndexOf(':'));
    if (slashIndex >= 0)
        name = name.mid(slashIndex + 1);
    if (name.endsWith(".git"))
        name.chop(4);
    return name.isEmpty() ? QStringLiteral("repo") : name;
}

QString GitPanel::selectBranchFromPrompt(const QString &title, bool excludeCurrent) const {
    QString branchesOut;
    if (!const_cast<GitPanel *>(this)->runGitSync({"branch", "--format=%(refname:short)"}, &branchesOut, nullptr, 4000))
        return QString();

    QStringList branches = branchesOut.split('\n', Qt::SkipEmptyParts);
    if (excludeCurrent)
        branches.removeAll(m_currentBranch);
    if (branches.isEmpty())
        return QString();

    bool ok = false;
    const QString choice = QInputDialog::getItem(
        const_cast<GitPanel *>(this), title, "Branch:", branches, 0, false, &ok);
    return ok ? choice.trimmed() : QString();
}

void GitPanel::showGitError(const QString &title, const QString &details) const {
    QMessageBox::critical(const_cast<GitPanel *>(this), title,
                          details.isEmpty() ? "Git command failed." : details);
}
