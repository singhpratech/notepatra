#include "gitpanel.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QPushButton>
#include <QFont>
#include <QFileInfo>
#include <QDir>
#include <QTreeWidgetItem>
#include <QHeaderView>

GitPanel::GitPanel(QWidget *parent) : QWidget(parent) {
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);

    // Branch header
    m_branchLabel = new QLabel("  Git: (no repo)");
    m_branchLabel->setFixedHeight(24);
    m_branchLabel->setStyleSheet("font-weight: bold; background: #2D2D2D; color: #4EC9B0; padding: 2px 6px;");
    layout->addWidget(m_branchLabel);

    // Status summary
    m_statusLabel = new QLabel("  No changes");
    m_statusLabel->setFixedHeight(20);
    m_statusLabel->setStyleSheet("background: #252526; color: #808080; padding: 2px 6px; font-size: 11px;");
    layout->addWidget(m_statusLabel);

    // Buttons
    auto *btnRow = new QHBoxLayout;
    btnRow->setContentsMargins(4, 4, 4, 4);
    auto *refreshBtn = new QPushButton("Refresh");
    refreshBtn->setFixedHeight(24);
    auto *commitBtn = new QPushButton("Commit...");
    commitBtn->setFixedHeight(24);
    auto *pushBtn = new QPushButton("Push");
    pushBtn->setFixedHeight(24);
    auto *pullBtn = new QPushButton("Pull");
    pullBtn->setFixedHeight(24);
    btnRow->addWidget(refreshBtn);
    btnRow->addWidget(commitBtn);
    btnRow->addWidget(pushBtn);
    btnRow->addWidget(pullBtn);
    layout->addLayout(btnRow);

    // Changed files tree
    m_tree = new QTreeWidget;
    m_tree->setHeaderLabels({"Status", "File"});
    m_tree->setRootIsDecorated(false);
    m_tree->header()->setStretchLastSection(true);
    m_tree->header()->resizeSection(0, 60);
    m_tree->setStyleSheet("QTreeWidget { background: #1E1E1E; color: #D4D4D4; border: none; }"
                          "QHeaderView::section { background: #2D2D2D; color: #CCC; border: none; padding: 4px; }");
    layout->addWidget(m_tree, 1);

    connect(refreshBtn, &QPushButton::clicked, this, [this]() {
        if (!m_repoRoot.isEmpty()) refresh(m_repoRoot);
    });

    connect(commitBtn, &QPushButton::clicked, this, [this]() {
        if (m_repoRoot.isEmpty()) return;
        // Quick commit — stage all + commit with message from input dialog
        QProcess proc;
        proc.setWorkingDirectory(m_repoRoot);
        proc.start("git", {"add", "-A"});
        proc.waitForFinished(5000);

        // TODO: show input dialog for commit message
        // For now just stage
    });

    connect(pushBtn, &QPushButton::clicked, this, [this]() {
        if (m_repoRoot.isEmpty()) return;
        QProcess::startDetached("git", {"push"}, m_repoRoot);
    });

    connect(pullBtn, &QPushButton::clicked, this, [this]() {
        if (m_repoRoot.isEmpty()) return;
        QProcess::startDetached("git", {"pull"}, m_repoRoot);
    });

    connect(m_tree, &QTreeWidget::itemDoubleClicked, this, [this](QTreeWidgetItem *item, int) {
        QString file = item->text(1);
        if (!file.isEmpty() && !m_repoRoot.isEmpty()) {
            emit fileClicked(m_repoRoot + "/" + file);
        }
    });
}

void GitPanel::refresh(const QString &filePath) {
    m_tree->clear();

    // Find repo root
    QProcess proc;
    QString dir = QFileInfo(filePath).isDir() ? filePath : QFileInfo(filePath).path();
    proc.setWorkingDirectory(dir);
    proc.start("git", {"rev-parse", "--show-toplevel"});
    proc.waitForFinished(3000);
    if (proc.exitCode() != 0) {
        m_branchLabel->setText("  Git: (not a git repo)");
        m_statusLabel->setText("  Open a file inside a git repository");
        return;
    }
    m_repoRoot = QString::fromUtf8(proc.readAllStandardOutput()).trimmed();

    // Get branch
    proc.setWorkingDirectory(m_repoRoot);
    proc.start("git", {"branch", "--show-current"});
    proc.waitForFinished(3000);
    QString branch = QString::fromUtf8(proc.readAllStandardOutput()).trimmed();
    m_branchLabel->setText(QString("  Git: %1  [%2]").arg(branch, QDir(m_repoRoot).dirName()));

    // Get status
    proc.start("git", {"status", "--porcelain"});
    proc.waitForFinished(5000);
    QString statusOutput = QString::fromUtf8(proc.readAllStandardOutput());

    QStringList lines = statusOutput.split('\n', Qt::SkipEmptyParts);
    int added = 0, modified = 0, deleted = 0, untracked = 0;

    for (const QString &line : lines) {
        if (line.length() < 4) continue;
        QString status = line.left(2).trimmed();
        QString file = line.mid(3);

        auto *item = new QTreeWidgetItem(m_tree);
        item->setText(1, file);

        if (status == "??" || status == "A") {
            item->setText(0, "Added");
            item->setForeground(0, QColor("#4CAF50"));
            item->setForeground(1, QColor("#4CAF50"));
            added++;
        } else if (status == "M" || status == "MM") {
            item->setText(0, "Modified");
            item->setForeground(0, QColor("#FFC107"));
            item->setForeground(1, QColor("#FFC107"));
            modified++;
        } else if (status == "D") {
            item->setText(0, "Deleted");
            item->setForeground(0, QColor("#F44336"));
            item->setForeground(1, QColor("#F44336"));
            deleted++;
        } else if (status == "R") {
            item->setText(0, "Renamed");
            item->setForeground(0, QColor("#2196F3"));
            item->setForeground(1, QColor("#2196F3"));
        } else {
            item->setText(0, status);
            untracked++;
        }
    }

    m_statusLabel->setText(QString("  +%1 added  ~%2 modified  -%3 deleted  ?%4 untracked")
                           .arg(added).arg(modified).arg(deleted).arg(untracked));
}
