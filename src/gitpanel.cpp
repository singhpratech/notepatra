#include "gitpanel.h"
#include "theme_detect.h"

#include <QAction>
#include <QApplication>
#include <QByteArray>
#include <QClipboard>
#include <QColor>
#include <QDir>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QFont>
#include <QFrame>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QInputDialog>
#include <QLineEdit>
#include <QMenu>
#include <QMessageBox>
#include <QPoint>
#include <QRegularExpression>
#include <QShortcut>
#include <QStandardPaths>
#include <QStringList>
#include <QTextStream>
#include <QToolTip>
#include <QTreeWidgetItem>
#include <QVBoxLayout>

// ─────────────────────────────────────────────────────────────────────────────
// Helpers (file-local)
//
// All of the visible styling sits on top of `npPalette()` so Light and Dark
// themes both render correctly. Anything that used to have hex colors
// hardcoded is now interpolated out of the palette struct.
// ─────────────────────────────────────────────────────────────────────────────
namespace {

enum TreeDataRole {
    KindRole = Qt::UserRole + 1,
    PathRole,
    StagedRole
};

enum TreeItemKind { KindSection = 1, KindFile };

// Small helper that produces the rounded "chip" / secondary-button style used
// across the new panel. It's theme-aware via npPalette().
QString chipButtonStyle() {
    const NpPalette pal = npPalette();
    return QString(
        "QPushButton { background: %1; color: %2; border: 1px solid %3; "
        "border-radius: 4px; padding: 3px 10px; font-size: 12px; }"
        "QPushButton:hover:enabled { background: %4; border-color: %5; }"
        "QPushButton:pressed:enabled { background: %6; }"
        "QPushButton:disabled { color: %7; background: %6; }")
        .arg(pal.btnBg, pal.btnFg, pal.btnBorder,
             pal.btnHover, pal.accent,
             pal.bg, pal.textMuted);
}

// Tiny 18×18 [+]/[−] stage/unstage icon-buttons, inserted into the tree
// via setItemWidget so they sit flush with each file row.
QString inlineActionButtonStyle() {
    const NpPalette pal = npPalette();
    return QString(
        "QPushButton { background: transparent; color: %1; border: 1px solid transparent; "
        "border-radius: 3px; padding: 0; font-size: 12px; font-weight: 700; }"
        "QPushButton:hover { background: %2; border-color: %3; color: %4; }")
        .arg(pal.textMuted, pal.btnHover, pal.btnBorder, pal.text);
}

// Bold "Section header" style for the two tree top-levels.
void styleSectionItem(QTreeWidgetItem *item, const QColor &color) {
    QFont f = item->font(0);
    f.setBold(true);
    f.setCapitalization(QFont::AllUppercase);
    f.setPointSize(qMax(8, f.pointSize() - 1));
    item->setFont(0, f);
    item->setFirstColumnSpanned(true);
    item->setForeground(0, color);
    item->setFlags(item->flags() & ~Qt::ItemIsSelectable);
    item->setData(0, KindRole, KindSection);
    item->setExpanded(true);
}

} // namespace

// ─────────────────────────────────────────────────────────────────────────────
// ctor
// ─────────────────────────────────────────────────────────────────────────────
GitPanel::GitPanel(QWidget *parent) : QWidget(parent) {
    auto *root = new QVBoxLayout(this);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(0);

    // ── 1. SOURCE CONTROL header band ───────────────────────────────────────
    // 24 px clipped on Windows where bold-font descenders pushed the text
    // into the next row. minimumHeight(28) lets the label grow naturally.
    m_sourceControlHeader = new QLabel("  SOURCE CONTROL");
    m_sourceControlHeader->setMinimumHeight(28);
    root->addWidget(m_sourceControlHeader);

    // ── git-missing banner (initially hidden) ───────────────────────────────
    // Rendered inline just under the header so it's the first thing users
    // see if git isn't on PATH. Keeps the rest of the panel disabled but
    // visible — nothing worse than a blank widget with no explanation.
    m_gitMissingBanner = new QLabel("  git not found — install git to enable SCM");
    m_gitMissingBanner->setFixedHeight(28);
    m_gitMissingBanner->setVisible(false);
    root->addWidget(m_gitMissingBanner);

    // ── 2. Branch chip row (36 px) ──────────────────────────────────────────
    m_branchChipRow = new QWidget;
    m_branchChipRow->setFixedHeight(36);
    auto *chipLay = new QHBoxLayout(m_branchChipRow);
    chipLay->setContentsMargins(10, 4, 10, 4);
    chipLay->setSpacing(6);

    m_branchChipLabel = new QLabel("⎇  no repo");
    chipLay->addWidget(m_branchChipLabel);

    m_fetchBtn = new QPushButton("Fetch");
    m_fetchBtn->setCursor(Qt::PointingHandCursor);
    m_fetchBtn->setFixedHeight(26);
    chipLay->addWidget(m_fetchBtn);

    m_branchMenuBtn = new QPushButton("Branch ▾");
    m_branchMenuBtn->setCursor(Qt::PointingHandCursor);
    m_branchMenuBtn->setFixedHeight(26);
    chipLay->addWidget(m_branchMenuBtn);

    chipLay->addStretch();
    root->addWidget(m_branchChipRow);

    // ── 3. Commit box ───────────────────────────────────────────────────────
    auto *commitWrap = new QWidget;
    auto *commitLay = new QVBoxLayout(commitWrap);
    commitLay->setContentsMargins(10, 4, 10, 8);
    commitLay->setSpacing(6);

    m_commitMessage = new QPlainTextEdit;
    m_commitMessage->setPlaceholderText("Message (Ctrl+Enter to commit)");
    m_commitMessage->setFixedHeight(72);
    commitLay->addWidget(m_commitMessage);

    auto *commitBtnRow = new QHBoxLayout;
    commitBtnRow->setContentsMargins(0, 0, 0, 0);
    commitBtnRow->setSpacing(8);

    m_commitBtn = new QPushButton("✓  Commit");
    m_commitBtn->setCursor(Qt::PointingHandCursor);
    m_commitBtn->setFixedHeight(28);
    m_commitBtn->setEnabled(false);
    commitBtnRow->addWidget(m_commitBtn);

    m_commitHint = new QLabel("0 files staged");
    commitBtnRow->addWidget(m_commitHint);
    commitBtnRow->addStretch();
    commitLay->addLayout(commitBtnRow);

    root->addWidget(commitWrap);

    // ── 4. Changes tree ─────────────────────────────────────────────────────
    m_tree = new QTreeWidget;
    m_tree->setHeaderHidden(true);
    m_tree->setRootIsDecorated(false);
    m_tree->setUniformRowHeights(false);
    m_tree->setSelectionMode(QAbstractItemView::SingleSelection);
    m_tree->setAlternatingRowColors(false);
    m_tree->setIndentation(8);
    m_tree->setMouseTracking(true);
    m_tree->setColumnCount(2);
    m_tree->header()->setSectionResizeMode(0, QHeaderView::Stretch);
    m_tree->header()->setSectionResizeMode(1, QHeaderView::Fixed);
    m_tree->header()->resizeSection(1, 28);
    m_tree->setContextMenuPolicy(Qt::CustomContextMenu);
    root->addWidget(m_tree, 1);

    // ── 5. Sync row ─────────────────────────────────────────────────────────
    m_syncRow = new QWidget;
    m_syncRow->setFixedHeight(36);
    auto *syncLay = new QHBoxLayout(m_syncRow);
    syncLay->setContentsMargins(10, 4, 10, 4);
    syncLay->setSpacing(6);

    m_pullBtn = new QPushButton("↓ Pull");
    m_pullBtn->setFixedHeight(26);
    m_pullBtn->setCursor(Qt::PointingHandCursor);
    m_pullBtn->setEnabled(false);
    syncLay->addWidget(m_pullBtn);

    m_pushBtn = new QPushButton("↑ Push");
    m_pushBtn->setFixedHeight(26);
    m_pushBtn->setCursor(Qt::PointingHandCursor);
    m_pushBtn->setEnabled(false);
    syncLay->addWidget(m_pushBtn);

    syncLay->addStretch();

    m_stashMenuBtn = new QPushButton("Stash ▾");
    m_stashMenuBtn->setFixedHeight(26);
    m_stashMenuBtn->setCursor(Qt::PointingHandCursor);
    syncLay->addWidget(m_stashMenuBtn);

    root->addWidget(m_syncRow);

    // ── 6. History (collapsible, collapsed by default) ──────────────────────
    m_historyToggle = new QPushButton("▸  HISTORY");
    m_historyToggle->setFixedHeight(26);
    m_historyToggle->setCursor(Qt::PointingHandCursor);
    root->addWidget(m_historyToggle);

    m_historyBody = new QWidget;
    m_historyBody->setVisible(false);
    auto *historyLay = new QVBoxLayout(m_historyBody);
    historyLay->setContentsMargins(0, 0, 0, 0);
    historyLay->setSpacing(0);

    m_historyTree = new QTreeWidget;
    m_historyTree->setHeaderHidden(true);
    m_historyTree->setRootIsDecorated(false);
    m_historyTree->setUniformRowHeights(true);
    m_historyTree->setMinimumHeight(140);
    m_historyTree->setSelectionMode(QAbstractItemView::SingleSelection);
    historyLay->addWidget(m_historyTree);

    m_historyMore = new QPushButton("Load 20 more");
    m_historyMore->setFixedHeight(24);
    m_historyMore->setCursor(Qt::PointingHandCursor);
    historyLay->addWidget(m_historyMore);

    root->addWidget(m_historyBody);

    // Apply the theme-aware stylesheets in one place — keeps the
    // constructor focused on layout and makes onThemeChanged() trivial.
    applyPalette();

    // ── File-system watcher → debounced refresh ─────────────────────────────
    // We watch both the repo root (covers file additions / deletions inside
    // the workdir) and the .git/index (covers index mutations, e.g. when the
    // user stages files from a terminal). Debounced by 250 ms so a flurry of
    // writes during a large `git add` doesn't cause the panel to thrash.
    m_watcher = new QFileSystemWatcher(this);
    m_debounce = new QTimer(this);
    m_debounce->setSingleShot(true);
    m_debounce->setInterval(250);
    connect(m_debounce, &QTimer::timeout, this, [this]() {
        if (!m_repoRoot.isEmpty()) refreshStatus();
    });
    connect(m_watcher, &QFileSystemWatcher::directoryChanged, this,
            [this](const QString &) { scheduleDebouncedRefresh(); });
    connect(m_watcher, &QFileSystemWatcher::fileChanged, this,
            [this](const QString &) { scheduleDebouncedRefresh(); });

    // ── Signal wiring ──────────────────────────────────────────────────────
    // Ctrl+Enter inside commit message triggers commit (matches VS Code).
    {
        auto *sc = new QShortcut(QKeySequence("Ctrl+Return"), m_commitMessage);
        sc->setContext(Qt::WidgetShortcut);
        connect(sc, &QShortcut::activated, this, &GitPanel::onCommitClicked);
    }
    connect(m_commitMessage, &QPlainTextEdit::textChanged, this, [this]() {
        updateCommitHint();
    });
    connect(m_commitBtn,    &QPushButton::clicked, this, &GitPanel::onCommitClicked);
    connect(m_fetchBtn,     &QPushButton::clicked, this, &GitPanel::onFetchClicked);
    connect(m_pullBtn,      &QPushButton::clicked, this, &GitPanel::onPullClicked);
    connect(m_pushBtn,      &QPushButton::clicked, this, &GitPanel::onPushClicked);
    connect(m_branchMenuBtn,&QPushButton::clicked, this, &GitPanel::onBranchMenuClicked);
    connect(m_stashMenuBtn, &QPushButton::clicked, this, &GitPanel::onStashMenuClicked);
    connect(m_historyToggle,&QPushButton::clicked, this, &GitPanel::toggleHistory);
    connect(m_historyMore,  &QPushButton::clicked, this, &GitPanel::loadMoreHistory);

    connect(m_tree, &QTreeWidget::customContextMenuRequested,
            this, &GitPanel::showFileContextMenu);
    connect(m_tree, &QTreeWidget::itemDoubleClicked, this,
            [this](QTreeWidgetItem *item, int) {
        if (!item) return;
        if (item->data(0, KindRole).toInt() != KindFile) return;
        const QString path = item->data(0, PathRole).toString();
        if (!path.isEmpty()) openDiffForPath(path);
    });

    connect(m_historyTree, &QTreeWidget::itemDoubleClicked, this,
            [this](QTreeWidgetItem *item, int) {
        if (!item) return;
        // Double-click on a commit row copies the short hash — poor-man's
        // "show diff" until we wire up a commit viewer. Good enough for now.
        const QString shortHash = item->data(0, PathRole).toString();
        if (!shortHash.isEmpty())
            QApplication::clipboard()->setText(shortHash);
    });

    detectGitAvailable();
    updateCommitHint();
}

// ─────────────────────────────────────────────────────────────────────────────
// public: refresh
// ─────────────────────────────────────────────────────────────────────────────
void GitPanel::refresh(const QString &filePath) {
    if (!m_gitAvailable) {
        detectGitAvailable();
        if (!m_gitAvailable) return;
    }

    const QString probe = QFileInfo(filePath).isDir() ? filePath : QFileInfo(filePath).path();
    if (probe.isEmpty()) {
        setRepoRoot(QString(), false);
        rebuildChangesTree();
        updateBranchChip();
        updateSyncRow();
        updateCommitHint();
        return;
    }

    // rev-parse is cheap and we already pay for a subprocess — use sync here.
    QString rootOut, rootErr;
    if (!runGitSync({"rev-parse", "--show-toplevel"}, &rootOut, &rootErr, 3000, probe)) {
        setRepoRoot(QString(), false);
        rebuildChangesTree();
        updateBranchChip();
        updateSyncRow();
        updateCommitHint();
        return;
    }

    setRepoRoot(rootOut.trimmed(), true);
    refreshStatus();
}

// ─────────────────────────────────────────────────────────────────────────────
// Async / sync process runners
// ─────────────────────────────────────────────────────────────────────────────
QProcess *GitPanel::runGitAsync(const QStringList &args,
                                std::function<void(const QByteArray &, const QByteArray &, bool)> onDone,
                                const QString &cwd) {
    auto *proc = new QProcess(this);
    const QString runDir = cwd.isEmpty() ? m_repoRoot : cwd;
    if (!runDir.isEmpty()) proc->setWorkingDirectory(runDir);

    // Capture both streams; v2 porcelain output can be large-ish.
    connect(proc, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
            this, [proc, onDone](int exitCode, QProcess::ExitStatus status) {
        const QByteArray out = proc->readAllStandardOutput();
        const QByteArray err = proc->readAllStandardError();
        const bool ok = (status == QProcess::NormalExit && exitCode == 0);
        if (onDone) onDone(out, err, ok);
        proc->deleteLater();
    });
    connect(proc, &QProcess::errorOccurred, this, [proc, onDone](QProcess::ProcessError) {
        // Fire onDone with a failure signal so the UI clears spinners etc.
        if (onDone) onDone(QByteArray(), proc->readAllStandardError(), false);
        proc->deleteLater();
    });

    proc->start("git", args);
    return proc;
}

bool GitPanel::runGitSync(const QStringList &args, QString *stdoutText, QString *stderrText,
                          int timeoutMs, const QString &cwd) {
    QProcess proc;
    const QString runDir = cwd.isEmpty() ? m_repoRoot : cwd;
    if (!runDir.isEmpty()) proc.setWorkingDirectory(runDir);
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

bool GitPanel::detectGitAvailable() {
    // We don't want to pay for a full `git --version` on every refresh, so
    // we cache into m_gitAvailable. Banner stays visible until git reappears.
    QProcess p;
    p.start("git", {"--version"});
    const bool started = p.waitForStarted(1000);
    if (!started) {
        m_gitAvailable = false;
        if (m_gitMissingBanner) m_gitMissingBanner->setVisible(true);
        return false;
    }
    p.waitForFinished(1000);
    m_gitAvailable = (p.exitStatus() == QProcess::NormalExit && p.exitCode() == 0);
    if (m_gitMissingBanner) m_gitMissingBanner->setVisible(!m_gitAvailable);
    return m_gitAvailable;
}

// ─────────────────────────────────────────────────────────────────────────────
// Repo root management
// ─────────────────────────────────────────────────────────────────────────────
void GitPanel::setRepoRoot(const QString &root, bool announce) {
    const bool changed = (m_repoRoot != root);
    m_repoRoot = root;

    // Re-point file watcher. We intentionally keep the watch list small
    // (just the repo root + .git/index) to avoid runaway inode usage on
    // large monorepos.
    if (m_watcher) {
        const QStringList oldDirs  = m_watcher->directories();
        const QStringList oldFiles = m_watcher->files();
        if (!oldDirs.isEmpty())  m_watcher->removePaths(oldDirs);
        if (!oldFiles.isEmpty()) m_watcher->removePaths(oldFiles);
        if (!m_repoRoot.isEmpty()) {
            m_watcher->addPath(m_repoRoot);
            const QString indexPath = QDir(m_repoRoot).filePath(".git/index");
            if (QFile::exists(indexPath)) m_watcher->addPath(indexPath);
        }
    }

    if (m_repoRoot.isEmpty()) {
        m_currentBranch.clear();
        m_aheadBehind = AheadBehind{};
        m_staged.clear();
        m_unstaged.clear();
        m_historyLoaded = 0;
    }

    if (announce && changed && !m_repoRoot.isEmpty())
        emit repositoryOpened(m_repoRoot);
}

// ─────────────────────────────────────────────────────────────────────────────
// Status loading — porcelain v2, NUL-delimited, async
// ─────────────────────────────────────────────────────────────────────────────
void GitPanel::refreshStatus() {
    if (m_repoRoot.isEmpty()) return;
    if (m_refreshing) return;    // single-in-flight; debounce covers coalescing
    m_refreshing = true;

    runGitAsync({"status", "--porcelain=v2", "--branch", "-z"},
        [this](const QByteArray &out, const QByteArray &err, bool ok) {
        m_refreshing = false;
        if (!ok) {
            showGitError("Git status", QString::fromUtf8(err).trimmed());
            return;
        }
        parsePorcelainV2(out);
        rebuildChangesTree();
        updateBranchChip();
        updateSyncRow();
        updateCommitHint();
    });
}

void GitPanel::parsePorcelainV2(const QByteArray &out) {
    // Reset
    m_staged.clear();
    m_unstaged.clear();
    m_currentBranch.clear();
    m_aheadBehind = AheadBehind{};

    // v2 records are NUL-delimited. Rename/Copy records have TWO paths
    // separated by NUL. We split once by NUL then paste the second path
    // back onto its parent record when we see the "2 " leading type code.
    QList<QByteArray> recs = out.split('\0');
    if (recs.isEmpty()) return;

    QVector<QByteArray> merged;
    merged.reserve(recs.size());
    for (int i = 0; i < recs.size(); ++i) {
        const QByteArray &r = recs[i];
        if (r.isEmpty()) continue;
        // "2 " record: next raw fragment is the original path.
        if (r.startsWith("2 ") && i + 1 < recs.size()) {
            QByteArray combined = r;
            combined.append('\0');
            combined.append(recs[i + 1]);
            merged.append(combined);
            ++i;
        } else {
            merged.append(r);
        }
    }

    for (const QByteArray &r : merged) {
        if (r.isEmpty()) continue;
        const char c = r[0];
        switch (c) {
        case '#': {
            // Header. Examples:
            //   # branch.oid <hash>
            //   # branch.head <name>
            //   # branch.upstream <upstream>
            //   # branch.ab +<a> -<b>
            const QString line = QString::fromUtf8(r);
            if (line.startsWith("# branch.head ")) {
                const QString head = line.mid(QByteArray("# branch.head ").size()).trimmed();
                m_currentBranch = (head == "(detached)") ? QStringLiteral("(detached HEAD)") : head;
            } else if (line.startsWith("# branch.upstream ")) {
                m_aheadBehind.upstream = line.mid(QByteArray("# branch.upstream ").size()).trimmed();
            } else if (line.startsWith("# branch.ab ")) {
                // Format: "# branch.ab +N -M"
                const QString rest = line.mid(QByteArray("# branch.ab ").size()).trimmed();
                const QStringList parts = rest.split(' ', Qt::SkipEmptyParts);
                for (const QString &p : parts) {
                    if (p.startsWith('+')) m_aheadBehind.ahead  = p.mid(1).toInt();
                    else if (p.startsWith('-')) m_aheadBehind.behind = p.mid(1).toInt();
                }
            }
            break;
        }
        case '1': {
            // "1 XY sub mH mI mW hH hI path"
            //  0 1  2   3  4  5  6  7  8
            const int firstSpace = r.indexOf(' ');
            if (firstSpace < 0) break;
            const QByteArray xy = r.mid(firstSpace + 1, 2);
            // Skip eight space-separated fields before the path.
            int pathStart = 0;
            int spaces = 0;
            for (int i = 0; i < r.size(); ++i) {
                if (r[i] == ' ') { ++spaces; if (spaces == 8) { pathStart = i + 1; break; } }
            }
            if (pathStart <= 0 || pathStart >= r.size()) break;
            const QString path = QString::fromUtf8(r.mid(pathStart));
            GitFileEntry entry;
            entry.path = path;
            entry.indexStatus = xy.size() > 0 ? xy[0] : ' ';
            entry.workStatus  = xy.size() > 1 ? xy[1] : ' ';
            if (entry.indexStatus != '.' && entry.indexStatus != ' ') m_staged.append(entry);
            if (entry.workStatus  != '.' && entry.workStatus  != ' ') m_unstaged.append(entry);
            break;
        }
        case '2': {
            // "2 XY sub mH mI mW hH hI Xscore <new>\0<old>"
            // The '\0' separator sits inside our combined record.
            const int nulIdx = r.indexOf('\0');
            if (nulIdx < 0) break;
            const QByteArray head = r.left(nulIdx);
            const QByteArray origPath = r.mid(nulIdx + 1);
            const int firstSpace = head.indexOf(' ');
            if (firstSpace < 0) break;
            const QByteArray xy = head.mid(firstSpace + 1, 2);
            int pathStart = 0;
            int spaces = 0;
            for (int i = 0; i < head.size(); ++i) {
                if (head[i] == ' ') { ++spaces; if (spaces == 9) { pathStart = i + 1; break; } }
            }
            if (pathStart <= 0 || pathStart >= head.size()) break;
            const QString newPath = QString::fromUtf8(head.mid(pathStart));
            GitFileEntry entry;
            entry.path = newPath;
            entry.indexStatus = xy.size() > 0 ? xy[0] : ' ';
            entry.workStatus  = xy.size() > 1 ? xy[1] : ' ';
            entry.isRename = true;
            entry.originalPath = QString::fromUtf8(origPath);
            if (entry.indexStatus != '.' && entry.indexStatus != ' ') m_staged.append(entry);
            if (entry.workStatus  != '.' && entry.workStatus  != ' ') m_unstaged.append(entry);
            break;
        }
        case 'u': {
            // Unmerged. Map to a conflict entry, shown under CHANGES.
            // Format: "u XY sub m1 m2 m3 mW h1 h2 h3 path"
            int pathStart = 0;
            int spaces = 0;
            for (int i = 0; i < r.size(); ++i) {
                if (r[i] == ' ') { ++spaces; if (spaces == 10) { pathStart = i + 1; break; } }
            }
            if (pathStart <= 0 || pathStart >= r.size()) break;
            const QString path = QString::fromUtf8(r.mid(pathStart));
            GitFileEntry entry;
            entry.path = path;
            entry.indexStatus = 'U';
            entry.workStatus  = 'U';
            m_unstaged.append(entry);
            break;
        }
        case '?': {
            // Untracked. "? path"
            const QString path = QString::fromUtf8(r.mid(2));
            GitFileEntry entry;
            entry.path = path;
            entry.indexStatus = '?';
            entry.workStatus  = '?';
            m_unstaged.append(entry);
            break;
        }
        case '!':
            // Ignored — we don't surface these.
            break;
        default:
            break;
        }
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// Tree rendering
// ─────────────────────────────────────────────────────────────────────────────
void GitPanel::rebuildChangesTree() {
    m_tree->clear();
    m_stagedSection = nullptr;
    m_changesSection = nullptr;

    const NpPalette pal = npPalette();

    m_stagedSection = new QTreeWidgetItem(m_tree);
    m_stagedSection->setText(0, QString("Staged Changes (%1)").arg(m_staged.size()));
    styleSectionItem(m_stagedSection, QColor(pal.successFg));
    for (const GitFileEntry &e : m_staged) addFileRow(m_stagedSection, e, true);

    m_changesSection = new QTreeWidgetItem(m_tree);
    m_changesSection->setText(0, QString("Changes (%1)").arg(m_unstaged.size()));
    styleSectionItem(m_changesSection, QColor(pal.warningFg));
    for (const GitFileEntry &e : m_unstaged) addFileRow(m_changesSection, e, false);

    // If neither section has any rows, show a "clean" placeholder so the
    // user gets feedback instead of a blank tree.
    if (m_staged.isEmpty() && m_unstaged.isEmpty() && !m_repoRoot.isEmpty()) {
        auto *empty = new QTreeWidgetItem(m_tree);
        empty->setText(0, "  Working tree clean");
        empty->setForeground(0, QColor(pal.textMuted));
        empty->setFlags(empty->flags() & ~Qt::ItemIsSelectable);
    }
}

void GitPanel::addFileRow(QTreeWidgetItem *parent, const GitFileEntry &entry, bool staged) {
    const NpPalette pal = npPalette();
    auto *item = new QTreeWidgetItem(parent);

    // Status letter sits before the path, e.g. "M · src/foo.cpp". Using a
    // single character keeps the first column from wrapping even on narrow
    // docks, and mirrors what VS Code / IntelliJ display.
    const char idx = entry.indexStatus;
    const char work = entry.workStatus;
    const char statusChar = staged ? idx
                                   : (work == '?' ? 'U' : work);

    QString display = QString("%1 · %2")
        .arg(QChar::fromLatin1(statusChar))
        .arg(entry.path);
    if (entry.isRename)
        display = QString("%1 · %2 → %3")
            .arg(QChar::fromLatin1(statusChar))
            .arg(entry.originalPath, entry.path);

    item->setText(0, display);
    item->setData(0, KindRole, KindFile);
    item->setData(0, PathRole, entry.path);
    item->setData(0, StagedRole, staged);
    item->setToolTip(0, entry.path);  // full path on hover (narrow panel fix)

    // Color by status code — aligns with the palette's accent/success/error
    // conventions so Light and Dark both read correctly.
    QColor c = QColor(pal.text);
    switch (statusChar) {
        case 'A': c = QColor(pal.successFg); break;
        case 'M': c = staged ? QColor(pal.warningFg) : QColor(pal.warningFg).lighter(115); break;
        case 'D': c = QColor(pal.errorFg); break;
        case 'R': c = QColor(pal.accent); break;
        case 'C': c = QColor(pal.accent); break;
        case 'U': c = QColor(pal.errorFg); break;
        case '?': c = QColor(pal.successFg); break;
        default:  c = QColor(pal.text); break;
    }
    item->setForeground(0, c);

    // Inline stage/unstage button — setItemWidget parents it into column 1.
    auto *btn = new QPushButton(staged ? "−" : "+");
    btn->setFixedSize(20, 20);
    btn->setCursor(Qt::PointingHandCursor);
    btn->setStyleSheet(inlineActionButtonStyle());
    btn->setToolTip(staged ? "Unstage" : "Stage");
    const QString path = entry.path;
    connect(btn, &QPushButton::clicked, this, [this, path, staged]() {
        if (staged) unstagePath(path);
        else        stagePath(path);
    });
    m_tree->setItemWidget(item, 1, btn);
}

// ─────────────────────────────────────────────────────────────────────────────
// Branch chip + sync row + commit hint
// ─────────────────────────────────────────────────────────────────────────────
void GitPanel::updateBranchChip() {
    const NpPalette pal = npPalette();
    if (m_repoRoot.isEmpty()) {
        m_branchChipLabel->setText("⎇  no repo");
        m_branchChipLabel->setStyleSheet(QString(
            "QLabel { color: %1; font-size: 12px; font-weight: 600; padding: 2px 8px; "
            "background: %2; border: 1px solid %3; border-radius: 4px; }")
            .arg(pal.textMuted, pal.chromeBg, pal.btnBorder));
        m_fetchBtn->setEnabled(false);
        m_branchMenuBtn->setEnabled(false);
        return;
    }

    QString chip = QString("⎇ %1").arg(m_currentBranch.isEmpty() ? "HEAD" : m_currentBranch);
    if (m_aheadBehind.ahead > 0 || m_aheadBehind.behind > 0 || !m_aheadBehind.upstream.isEmpty()) {
        chip += QString(" · ↑%1 ↓%2")
            .arg(m_aheadBehind.ahead)
            .arg(m_aheadBehind.behind);
    }
    m_branchChipLabel->setText(chip);
    m_branchChipLabel->setStyleSheet(QString(
        "QLabel { color: %1; font-size: 12px; font-weight: 600; padding: 2px 8px; "
        "background: %2; border: 1px solid %3; border-radius: 4px; }")
        .arg(pal.accent, pal.chromeBg, pal.btnBorder));

    m_fetchBtn->setEnabled(true);
    m_branchMenuBtn->setEnabled(true);
}

void GitPanel::updateSyncRow() {
    const bool hasRepo = !m_repoRoot.isEmpty();
    const int behind = m_aheadBehind.behind;
    const int ahead  = m_aheadBehind.ahead;

    m_pullBtn->setText(behind > 0 ? QString("↓ Pull (%1)").arg(behind) : "↓ Pull");
    m_pushBtn->setText(ahead  > 0 ? QString("↑ Push (%1)").arg(ahead)  : "↑ Push");

    // Buttons are enabled only when there's something to sync. That matches
    // the plan — disabled when count = 0.
    m_pullBtn->setEnabled(hasRepo && behind > 0);
    m_pushBtn->setEnabled(hasRepo && ahead  > 0);
    m_stashMenuBtn->setEnabled(hasRepo);
}

void GitPanel::updateCommitHint() {
    const bool hasRepo = !m_repoRoot.isEmpty();
    const int n = m_staged.size();
    if (m_commitHint) m_commitHint->setText(QString("%1 files staged").arg(n));

    const bool hasMsg = !m_commitMessage->toPlainText().trimmed().isEmpty();
    // VS Code behavior: the commit button is enabled when there's a message
    // and at least one staged change. If nothing is staged we auto-stage
    // tracked modifications via `commit -a` path in onCommitClicked.
    if (m_commitBtn) m_commitBtn->setEnabled(hasRepo && hasMsg);
}

// ─────────────────────────────────────────────────────────────────────────────
// Actions
// ─────────────────────────────────────────────────────────────────────────────
void GitPanel::onCommitClicked() {
    if (m_repoRoot.isEmpty()) return;
    const QString msg = m_commitMessage->toPlainText().trimmed();
    if (msg.isEmpty()) return;

    // If nothing is staged, fall back to `commit -am` so users who type
    // a message first and click Commit get the obvious behavior.
    const bool hasStaged = !m_staged.isEmpty();
    const QStringList args = hasStaged
        ? QStringList{"commit", "-m", msg}
        : QStringList{"commit", "-am", msg};

    runGitAsync(args, [this](const QByteArray &out, const QByteArray &err, bool ok) {
        if (!ok) {
            showGitError("Commit failed",
                         QString::fromUtf8(err.isEmpty() ? out : err).trimmed());
            return;
        }
        m_commitMessage->clear();
        refreshStatus();
        if (m_historyExpanded) loadHistory(20, false);
    });
}

void GitPanel::onFetchClicked() {
    if (m_repoRoot.isEmpty()) return;
    m_fetchBtn->setEnabled(false);
    runGitAsync({"fetch", "--all", "--prune"},
        [this](const QByteArray &out, const QByteArray &err, bool ok) {
        if (!ok) {
            showGitError("Fetch failed",
                         QString::fromUtf8(err.isEmpty() ? out : err).trimmed());
        }
        refreshStatus();
    });
}

void GitPanel::onPullClicked() {
    if (m_repoRoot.isEmpty()) return;
    m_pullBtn->setEnabled(false);
    runGitAsync({"pull", "--stat"},
        [this](const QByteArray &out, const QByteArray &err, bool ok) {
        if (!ok) {
            showGitError("Pull failed",
                         QString::fromUtf8(err.isEmpty() ? out : err).trimmed());
        }
        refreshStatus();
        if (m_historyExpanded) loadHistory(20, false);
    });
}

void GitPanel::onPushClicked() {
    if (m_repoRoot.isEmpty()) return;
    m_pushBtn->setEnabled(false);
    runGitAsync({"push"},
        [this](const QByteArray &out, const QByteArray &err, bool ok) {
        if (!ok) {
            showGitError("Push failed",
                         QString::fromUtf8(err.isEmpty() ? out : err).trimmed());
        }
        refreshStatus();
    });
}

void GitPanel::onBranchMenuClicked() {
    if (m_repoRoot.isEmpty()) return;
    QMenu menu(this);
    QString branchesOut;
    runGitSync({"branch", "--format=%(refname:short)"}, &branchesOut, nullptr, 4000);
    const QStringList branches = branchesOut.split('\n', Qt::SkipEmptyParts);

    auto *newAct = menu.addAction("New branch…");
    connect(newAct, &QAction::triggered, this, [this]() {
        bool ok = false;
        const QString name = QInputDialog::getText(this, "New branch",
            "Branch name:", QLineEdit::Normal, "", &ok).trimmed();
        if (!ok || name.isEmpty()) return;
        runGitAsync({"checkout", "-b", name},
            [this](const QByteArray &out, const QByteArray &err, bool okc) {
            if (!okc) showGitError("New branch",
                                   QString::fromUtf8(err.isEmpty() ? out : err).trimmed());
            refreshStatus();
        });
    });
    menu.addSeparator();
    for (const QString &b : branches) {
        const bool isCurrent = (b == m_currentBranch);
        auto *a = menu.addAction((isCurrent ? QString("✓ ") : QString("   ")) + b);
        if (isCurrent) a->setEnabled(false);
        connect(a, &QAction::triggered, this, [this, b]() {
            runGitAsync({"checkout", b},
                [this](const QByteArray &out, const QByteArray &err, bool ok) {
                if (!ok) showGitError("Checkout",
                                      QString::fromUtf8(err.isEmpty() ? out : err).trimmed());
                refreshStatus();
            });
        });
    }
    menu.exec(m_branchMenuBtn->mapToGlobal(QPoint(0, m_branchMenuBtn->height() + 2)));
}

void GitPanel::onStashMenuClicked() {
    if (m_repoRoot.isEmpty()) return;
    QMenu menu(this);

    auto *stashAct = menu.addAction("Stash");
    connect(stashAct, &QAction::triggered, this, [this]() {
        bool ok = false;
        const QString msg = QInputDialog::getText(this, "Stash",
            "Optional stash message:", QLineEdit::Normal, "", &ok).trimmed();
        if (!ok) return;
        QStringList args{"stash", "push", "-u"};
        if (!msg.isEmpty()) args << "-m" << msg;
        runGitAsync(args,
            [this](const QByteArray &out, const QByteArray &err, bool okp) {
            if (!okp) showGitError("Stash",
                                   QString::fromUtf8(err.isEmpty() ? out : err).trimmed());
            refreshStatus();
        });
    });

    auto *popAct = menu.addAction("Pop");
    connect(popAct, &QAction::triggered, this, [this]() {
        runGitAsync({"stash", "pop"},
            [this](const QByteArray &out, const QByteArray &err, bool ok) {
            if (!ok) showGitError("Stash pop",
                                  QString::fromUtf8(err.isEmpty() ? out : err).trimmed());
            refreshStatus();
        });
    });

    auto *applyAct = menu.addAction("Apply");
    connect(applyAct, &QAction::triggered, this, [this]() {
        runGitAsync({"stash", "apply"},
            [this](const QByteArray &out, const QByteArray &err, bool ok) {
            if (!ok) showGitError("Stash apply",
                                  QString::fromUtf8(err.isEmpty() ? out : err).trimmed());
            refreshStatus();
        });
    });

    menu.exec(m_stashMenuBtn->mapToGlobal(QPoint(0, m_stashMenuBtn->height() + 2)));
}

// ─────────────────────────────────────────────────────────────────────────────
// History
// ─────────────────────────────────────────────────────────────────────────────
void GitPanel::toggleHistory() {
    m_historyExpanded = !m_historyExpanded;
    m_historyBody->setVisible(m_historyExpanded);
    m_historyToggle->setText(m_historyExpanded ? "▾  HISTORY" : "▸  HISTORY");
    if (m_historyExpanded && m_historyLoaded == 0 && !m_repoRoot.isEmpty())
        loadHistory(20, false);
}

void GitPanel::loadMoreHistory() {
    loadHistory(20, true);
}

void GitPanel::loadHistory(int limit, bool append) {
    if (m_repoRoot.isEmpty()) return;
    // The log format encodes each field in a unit-separator-delimited line
    // so subjects containing spaces / tabs / pipes parse cleanly.
    const QString skip = QString::number(append ? m_historyLoaded : 0);
    const QString count = QString::number(limit);
    runGitAsync({
        "log",
        QString("--skip=%1").arg(skip),
        QString("--max-count=%1").arg(count),
        "--pretty=format:%H%x1F%h%x1F%s%x1F%an%x1F%ar"
    }, [this, append, limit](const QByteArray &out, const QByteArray &, bool ok) {
        if (!ok) return;
        if (!append) {
            m_historyTree->clear();
            m_historyLoaded = 0;
        }
        const QStringList lines = QString::fromUtf8(out).split('\n', Qt::SkipEmptyParts);
        for (const QString &line : lines) {
            const QStringList f = line.split(QChar(0x1F));
            if (f.size() < 5) continue;
            auto *item = new QTreeWidgetItem(m_historyTree);
            item->setText(0, QString("%1  %2  ·  %3")
                          .arg(f[1], f[2].left(60), f[4]));
            item->setToolTip(0, QString("%1\n%2  (%3, %4)").arg(f[2], f[0], f[3], f[4]));
            item->setData(0, PathRole, f[1]);  // short hash
        }
        m_historyLoaded += lines.size();
        m_historyMore->setVisible(lines.size() == limit);
    });
}

// ─────────────────────────────────────────────────────────────────────────────
// File-row actions
// ─────────────────────────────────────────────────────────────────────────────
void GitPanel::stagePath(const QString &path) {
    if (path.isEmpty()) return;
    runGitAsync({"add", "--", path},
        [this](const QByteArray &out, const QByteArray &err, bool ok) {
        if (!ok) showGitError("Stage",
                              QString::fromUtf8(err.isEmpty() ? out : err).trimmed());
        refreshStatus();
    });
}

void GitPanel::unstagePath(const QString &path) {
    if (path.isEmpty()) return;
    runGitAsync({"reset", "HEAD", "--", path},
        [this](const QByteArray &out, const QByteArray &err, bool ok) {
        if (!ok) showGitError("Unstage",
                              QString::fromUtf8(err.isEmpty() ? out : err).trimmed());
        refreshStatus();
    });
}

void GitPanel::discardPath(const QString &path) {
    if (path.isEmpty()) return;
    const auto reply = QMessageBox::warning(
        this, "Discard changes",
        QString("Discard local changes for:\n%1").arg(path),
        QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
    if (reply != QMessageBox::Yes) return;

    // Untracked files need `clean -f`; everything else uses `checkout --`.
    QString statusOut;
    runGitSync({"status", "--porcelain", "--", path}, &statusOut, nullptr, 5000);
    const bool untracked = statusOut.startsWith("??");
    const QStringList args = untracked
        ? QStringList{"clean", "-f", "--", path}
        : QStringList{"checkout", "--", path};

    runGitAsync(args, [this](const QByteArray &out, const QByteArray &err, bool ok) {
        if (!ok) showGitError("Discard",
                              QString::fromUtf8(err.isEmpty() ? out : err).trimmed());
        refreshStatus();
    });
}

void GitPanel::openFilePath(const QString &path) {
    if (m_repoRoot.isEmpty() || path.isEmpty()) return;
    const QString abs = QDir(m_repoRoot).filePath(path);
    if (QFileInfo::exists(abs)) {
        emit openFileInTab(abs);
        emit fileClicked(abs);  // keep legacy listeners happy
    }
}

void GitPanel::openDiffForPath(const QString &path) {
    if (m_repoRoot.isEmpty() || path.isEmpty()) return;
    const QString abs = QDir(m_repoRoot).filePath(path);

    // Left side = HEAD version (`git show HEAD:<path>`). Right side = current
    // working copy on disk. Using HEAD (not the index) matches what a user
    // typically wants to see in a "what's different from last commit" view.
    QString headOut, headErr;
    const bool ok = runGitSync({"show", QString("HEAD:%1").arg(path)}, &headOut, &headErr, 4000);
    QString leftText = ok ? headOut : QString();   // new files: empty left side

    QString rightText;
    QFile f(abs);
    if (f.open(QIODevice::ReadOnly | QIODevice::Text)) {
        rightText = QTextStream(&f).readAll();
    }

    const QString title = QString("Diff · %1").arg(QFileInfo(path).fileName());
    emit openDiffInTab(title, leftText, rightText);
}

// ─────────────────────────────────────────────────────────────────────────────
// Context menu
// ─────────────────────────────────────────────────────────────────────────────
void GitPanel::showFileContextMenu(const QPoint &pos) {
    QTreeWidgetItem *item = m_tree->itemAt(pos);
    if (!item || item->data(0, KindRole).toInt() != KindFile) return;
    const QString path = item->data(0, PathRole).toString();
    const bool staged = item->data(0, StagedRole).toBool();
    if (path.isEmpty()) return;

    QMenu menu(this);
    menu.addAction("Open file", this, [this, path]() { openFilePath(path); });
    menu.addAction("Open diff", this, [this, path]() { openDiffForPath(path); });
    menu.addSeparator();
    if (staged) {
        menu.addAction("Unstage", this, [this, path]() { unstagePath(path); });
    } else {
        menu.addAction("Stage", this, [this, path]() { stagePath(path); });
    }
    menu.addAction("Discard", this, [this, path]() { discardPath(path); });
    menu.exec(m_tree->viewport()->mapToGlobal(pos));
}

// ─────────────────────────────────────────────────────────────────────────────
// Misc
// ─────────────────────────────────────────────────────────────────────────────
void GitPanel::scheduleDebouncedRefresh() {
    if (!m_debounce) return;
    m_debounce->start();  // restarts the timer on every change
}

void GitPanel::showGitError(const QString &title, const QString &details) {
    QMessageBox::critical(this, title, details.isEmpty() ? "Git command failed." : details);
}

QString GitPanel::relativeToRepo(const QString &path) const {
    if (m_repoRoot.isEmpty()) return path;
    return QDir(m_repoRoot).relativeFilePath(path);
}

void GitPanel::applyPalette() {
    // Pull the theme-aware palette once and interpolate into every
    // stylesheet below. Called from the constructor AND from
    // onThemeChanged() so one change site keeps both paths consistent.
    const NpPalette pal = npPalette();

    if (m_sourceControlHeader) {
        m_sourceControlHeader->setStyleSheet(QString(
            "background: %1; color: %2; padding: 4px 10px; "
            "font-size: 11px; font-weight: 700; letter-spacing: 0.08em;")
            .arg(pal.chromeBg, pal.text));
    }
    if (m_gitMissingBanner) {
        m_gitMissingBanner->setStyleSheet(QString(
            "background: %1; color: %2; padding: 4px 10px; "
            "font-size: 12px; font-weight: 600;")
            .arg(pal.errorFg, QColor("#FFFFFF").name()));
    }
    if (m_branchChipRow) {
        m_branchChipRow->setStyleSheet(QString("background: %1;").arg(pal.bg));
    }
    if (m_branchChipLabel) {
        // Initial style; updateBranchChip() re-colours between accent
        // (repo open) and textMuted (no repo) on its own.
        m_branchChipLabel->setStyleSheet(QString(
            "QLabel { color: %1; font-size: 12px; font-weight: 600; padding: 2px 8px; "
            "background: %2; border: 1px solid %3; border-radius: 4px; }")
            .arg(pal.accent, pal.chromeBg, pal.btnBorder));
    }
    if (m_fetchBtn)      m_fetchBtn->setStyleSheet(chipButtonStyle());
    if (m_branchMenuBtn) m_branchMenuBtn->setStyleSheet(chipButtonStyle());
    if (m_pullBtn)       m_pullBtn->setStyleSheet(chipButtonStyle());
    if (m_pushBtn)       m_pushBtn->setStyleSheet(chipButtonStyle());
    if (m_stashMenuBtn)  m_stashMenuBtn->setStyleSheet(chipButtonStyle());
    if (m_historyMore)   m_historyMore->setStyleSheet(chipButtonStyle());

    if (m_commitMessage) {
        m_commitMessage->setStyleSheet(QString(
            "QPlainTextEdit { background: %1; color: %2; border: 1px solid %3; "
            "border-radius: 4px; padding: 6px 8px; font-size: 12px; }"
            "QPlainTextEdit:focus { border-color: %4; }")
            .arg(pal.inputBg, pal.inputFg, pal.inputBorder, pal.inputFocus));
    }
    if (m_commitBtn) {
        m_commitBtn->setStyleSheet(QString(
            "QPushButton { background: %1; color: #FFFFFF; border: none; "
            "border-radius: 4px; font-weight: 600; font-size: 12px; padding: 0 14px; }"
            "QPushButton:hover:enabled { background: %2; }"
            "QPushButton:disabled { background: %3; color: %4; }")
            .arg(pal.successFg, QColor(pal.successFg).lighter(115).name(),
                 pal.btnBg, pal.textMuted));
    }
    if (m_commitHint) {
        m_commitHint->setStyleSheet(QString(
            "color: %1; font-size: 11px;").arg(pal.textMuted));
    }
    if (m_tree) {
        m_tree->setStyleSheet(QString(
            "QTreeWidget { background: %1; color: %2; border: none; outline: 0; }"
            "QTreeWidget::item { padding: 3px 6px; }"
            "QTreeWidget::item:selected { background: %3; color: %4; }")
            .arg(pal.bg, pal.text, pal.selectionBg, pal.selectionFg));
    }
    if (m_syncRow) {
        m_syncRow->setStyleSheet(QString(
            "background: %1; border-top: 1px solid %2;")
            .arg(pal.bg, pal.border));
    }
    if (m_historyToggle) {
        m_historyToggle->setStyleSheet(QString(
            "QPushButton { background: %1; color: %2; border: none; "
            "text-align: left; padding: 4px 10px; font-size: 11px; font-weight: 700; "
            "letter-spacing: 0.08em; }"
            "QPushButton:hover { background: %3; }")
            .arg(pal.chromeBg, pal.text, pal.btnHover));
    }
    if (m_historyTree) {
        m_historyTree->setStyleSheet(QString(
            "QTreeWidget { background: %1; color: %2; border: none; outline: 0; }"
            "QTreeWidget::item { padding: 3px 10px; }"
            "QTreeWidget::item:selected { background: %3; color: %4; }")
            .arg(pal.bg, pal.text, pal.selectionBg, pal.selectionFg));
    }
}

void GitPanel::onThemeChanged() {
    applyPalette();
    // Branch chip and sync row labels are coloured from the palette on
    // every refresh, and the per-file tree rows have their colours baked
    // in when read. Rebuild so those pick up the new palette too.
    updateBranchChip();
    updateSyncRow();
    rebuildChangesTree();
    update();
}
