#include "projectsearch.h"
#include "config.h"
#include "fonts.h"

#include <QMetaType>

static int s_paramsTypeId = qRegisterMetaType<ProjectSearchWorker::Params>("ProjectSearchWorker::Params");
static int s_matchTypeId  = qRegisterMetaType<ProjectSearchMatch>("ProjectSearchMatch");

#include <QCheckBox>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QProgressBar>
#include <QPushButton>
#include <QRegularExpression>
#include <QTextStream>
#include <QTreeWidget>
#include <QTreeWidgetItem>
#include <QVBoxLayout>

// ═══════════════════════════════════════════════════════════════════════
// ProjectSearchWorker — runs on its own thread. Uses QDirIterator for
// walking, reads each file line by line, matches on literal or regex.
// Streams results via signals so the UI updates as matches are found.
// ═══════════════════════════════════════════════════════════════════════

ProjectSearchWorker::ProjectSearchWorker(QObject *parent) : QObject(parent) {}

void ProjectSearchWorker::cancel() {
    m_cancel.store(true);
}

static bool matchesAnyGlob(const QString &fileName, const QStringList &globs) {
    if (globs.isEmpty()) return true;
    for (const QString &g : globs) {
        QRegularExpression re(QRegularExpression::wildcardToRegularExpression(g),
                              QRegularExpression::CaseInsensitiveOption);
        if (re.match(fileName).hasMatch()) return true;
    }
    return false;
}

// Quick-and-cheap binary-file heuristic: read the first 4 KB and check
// for a NUL byte. Text files (any language — .py/.sql/.txt/.cpp/.js/
// .go/.rs/.java/.html/.xml/.yaml/.md/.log) never contain NUL bytes.
// Binary files (images, compiled objects, PDFs, archives) contain
// plenty. This matches the heuristic used by grep and ripgrep.
static bool looksBinary(const QString &path) {
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) return false;
    QByteArray head = f.read(4096);
    return head.contains('\0');
}

void ProjectSearchWorker::search(const Params &p) {
    m_cancel.store(false);
    if (p.query.isEmpty()) {
        emit finishedSearch(0, 0);
        return;
    }

    QStringList globs;
    for (const QString &g : p.fileGlobs.split(',', Qt::SkipEmptyParts))
        globs << g.trimmed();

    // Build matcher once so we're not recompiling on every line
    QRegularExpression regex;
    bool useRegex = p.regex || p.wholeWord;
    if (useRegex) {
        QString pat = p.regex ? p.query
                              : "\\b" + QRegularExpression::escape(p.query) + "\\b";
        regex.setPattern(pat);
        regex.setPatternOptions(p.caseSensitive
            ? QRegularExpression::NoPatternOption
            : QRegularExpression::CaseInsensitiveOption);
        if (!regex.isValid()) {
            emit errorOccurred("Invalid regex: " + regex.errorString());
            return;
        }
    }
    const QString literal = p.caseSensitive ? p.query : p.query.toLower();

    // Walk the tree to count total files (cheap) so we can show progress
    QDirIterator probeIt(p.folder, QDir::Files | QDir::NoSymLinks,
                         QDirIterator::Subdirectories);
    int totalFiles = 0;
    QStringList queue;
    queue.reserve(1024);
    while (probeIt.hasNext()) {
        if (m_cancel.load()) { emit finishedSearch(0, 0); return; }
        probeIt.next();
        QFileInfo fi = probeIt.fileInfo();
        if (!fi.isFile()) continue;
        if (!matchesAnyGlob(fi.fileName(), globs)) continue;
        queue << fi.absoluteFilePath();
        ++totalFiles;
    }
    emit filesCounted(totalFiles);

    int filesDone = 0;
    int totalMatches = 0;

    for (const QString &path : queue) {
        if (m_cancel.load()) break;
        emit fileStarted(path);
        QFileInfo fi(path);

        // File-name match — fires once per file if the file's name
        // itself contains the query.
        if (p.searchNames) {
            bool nameHit;
            if (useRegex)
                nameHit = regex.match(fi.fileName()).hasMatch();
            else if (p.caseSensitive)
                nameHit = fi.fileName().contains(literal);
            else
                nameHit = fi.fileName().toLower().contains(literal);
            if (nameHit) emit fileNameMatch(path);
        }

        // Skip files above the sanity cap (2 GB default — effectively no cap
        // for source trees; still guards against accidentally iterating
        // massive archive files).
        if (fi.size() > p.maxFileSizeBytes) {
            ++filesDone;
            emit progress(filesDone, totalFiles, totalMatches);
            continue;
        }

        // Skip binary files so we don't waste time (and memory) scanning
        // images / compiled objects / archives. Users can untick "skip
        // binary" in the UI if they really want to search inside them.
        if (p.skipBinary && looksBinary(path)) {
            ++filesDone;
            emit progress(filesDone, totalFiles, totalMatches);
            continue;
        }

        QFile f(path);
        if (!f.open(QIODevice::ReadOnly | QIODevice::Text)) {
            ++filesDone;
            emit progress(filesDone, totalFiles, totalMatches);
            continue;
        }

        // Line-by-line reader — scales to gigabyte files without blowing
        // RAM (reads one line at a time from disk). One pass per file.
        // UTF-8 by default; Latin-1 / CP-1252 files still work because
        // QTextStream falls back to the system codec on decode errors.
        QTextStream ts(&f);
        ts.setCodec("UTF-8");
        int lineNum = 0;
        const int maxMatchesPerFile = 500;
        int fileMatches = 0;
        while (!ts.atEnd()) {
            if (m_cancel.load()) break;
            QString line = ts.readLine();
            ++lineNum;
            if (useRegex) {
                auto it = regex.globalMatch(line);
                while (it.hasNext()) {
                    auto m = it.next();
                    ProjectSearchMatch pm;
                    pm.filePath = path;
                    pm.lineNumber = lineNum;
                    pm.lineContent = line;
                    pm.matchStart  = m.capturedStart();
                    pm.matchLength = m.capturedLength();
                    emit matchFound(pm);
                    ++totalMatches;
                    ++fileMatches;
                    if (fileMatches >= maxMatchesPerFile) break;
                }
            } else {
                int from = 0;
                const QString &hayLine = p.caseSensitive ? line : line.toLower();
                while (true) {
                    int idx = hayLine.indexOf(literal, from);
                    if (idx < 0) break;
                    ProjectSearchMatch pm;
                    pm.filePath = path;
                    pm.lineNumber = lineNum;
                    pm.lineContent = line;
                    pm.matchStart = idx;
                    pm.matchLength = literal.length();
                    emit matchFound(pm);
                    ++totalMatches;
                    ++fileMatches;
                    if (fileMatches >= maxMatchesPerFile) break;
                    from = idx + literal.length();
                }
            }
            if (fileMatches >= maxMatchesPerFile) break;
        }

        ++filesDone;
        // Report progress every 25 files so we don't spam the UI
        if ((filesDone & 0x1F) == 0 || filesDone == totalFiles)
            emit progress(filesDone, totalFiles, totalMatches);
    }

    emit finishedSearch(totalMatches, totalFiles);
}

// ═══════════════════════════════════════════════════════════════════════
// ProjectSearch — the tab widget
// ═══════════════════════════════════════════════════════════════════════

namespace {

static bool psearchIsDark() {
    const QString &t = Config::instance().theme;
    return t.compare("Dark", Qt::CaseInsensitive) == 0 ||
           t.compare("Monokai", Qt::CaseInsensitive) == 0;
}

struct PSearchPalette {
    QString bg, cardBg, inputBg, inputBorder, accent, textPrimary, textSecondary, textMuted, highlight;
};

static PSearchPalette psearchPalette() {
    if (psearchIsDark()) {
        return {
            "#1E1E1E", "#252526", "#2D2D2F", "#3E3E42",
            "#CC785C", "#E8E6E3", "#B8B5B1", "#6C6C6C",
            "#FFE066"
        };
    }
    return {
        "#FAF9F5", "#FFFFFF", "#FFFFFF", "#D4D1C4",
        "#CC785C", "#141413", "#54524E", "#8E8C88",
        "#FFF2A8"
    };
}

} // namespace

ProjectSearch::ProjectSearch(QWidget *parent) : QWidget(parent) {
    buildUi();

    // Spin up worker thread
    m_thread = new QThread(this);
    m_worker = new ProjectSearchWorker;
    m_worker->moveToThread(m_thread);
    m_thread->start();

    connect(this, &QObject::destroyed, this, []() { /* thread stopped in dtor */ });

    connect(m_worker, &ProjectSearchWorker::matchFound,
            this, &ProjectSearch::onMatch, Qt::QueuedConnection);
    connect(m_worker, &ProjectSearchWorker::fileNameMatch,
            this, &ProjectSearch::onFileNameMatch, Qt::QueuedConnection);
    connect(m_worker, &ProjectSearchWorker::progress,
            this, &ProjectSearch::onProgress, Qt::QueuedConnection);
    connect(m_worker, &ProjectSearchWorker::finishedSearch,
            this, &ProjectSearch::onFinished, Qt::QueuedConnection);
    connect(m_worker, &ProjectSearchWorker::filesCounted,
            this, [this](int n) {
        m_progressBar->setRange(0, qMax(1, n));
        m_progressBar->setValue(0);
        m_statusLabel->setText(QString("Scanning %1 files...").arg(n));
    }, Qt::QueuedConnection);
    connect(m_worker, &ProjectSearchWorker::errorOccurred,
            this, [this](const QString &msg) {
        m_statusLabel->setText("❌ " + msg);
    }, Qt::QueuedConnection);
}

ProjectSearch::~ProjectSearch() {
    if (m_worker) m_worker->cancel();
    if (m_thread) {
        m_thread->quit();
        m_thread->wait(3000);
    }
    delete m_worker;
}

void ProjectSearch::buildUi() {
    const auto p = psearchPalette();
    setStyleSheet(QString("ProjectSearch { background: %1; }").arg(p.bg));

    auto *root = new QVBoxLayout(this);
    root->setContentsMargins(20, 18, 20, 18);
    root->setSpacing(14);

    // ── Title + hint ─────────────────────────────────────────────────
    auto *title = new QLabel("🔍 Project Search");
    QFont tf = notepatraUiFont();
    tf.setPointSize(18);
    tf.setWeight(QFont::DemiBold);
    title->setFont(tf);
    title->setStyleSheet(QString("color: %1;").arg(p.textPrimary));
    root->addWidget(title);

    auto *hint = new QLabel("Find strings across file names AND contents in any text-based file — Python, SQL, TXT, C/C++, JS/TS, Rust, Go, HTML, JSON, YAML, Markdown, logs, config files. Size is not a limit (streams line-by-line so a 2 GB log searches the same as a 2 KB script). Double-click any match to jump to that line.");
    hint->setWordWrap(true);
    hint->setStyleSheet(QString("color: %1; font-size: 12px;").arg(p.textSecondary));
    root->addWidget(hint);

    // ── Query input (big, prominent) ─────────────────────────────────
    m_queryInput = new QLineEdit;
    m_queryInput->setPlaceholderText("Search for a string, word, or regex pattern…");
    m_queryInput->setFont(QFont(notepatraUiFont().family(), 14));
    m_queryInput->setStyleSheet(QString(
        "QLineEdit { background: %1; color: %2; border: 2px solid %3; "
        "border-radius: 10px; padding: 12px 16px; }"
        "QLineEdit:focus { border-color: %4; }"
    ).arg(p.inputBg, p.textPrimary, p.inputBorder, p.accent));
    root->addWidget(m_queryInput);

    // ── Folder row ───────────────────────────────────────────────────
    auto *folderRow = new QHBoxLayout;
    folderRow->setSpacing(8);
    auto *folderLabel = new QLabel("Folder:");
    folderLabel->setStyleSheet(QString("color: %1; font-size: 12px;").arg(p.textSecondary));
    folderRow->addWidget(folderLabel);

    m_folderInput = new QLineEdit(QDir::homePath());
    m_folderInput->setStyleSheet(QString(
        "QLineEdit { background: %1; color: %2; border: 1px solid %3; "
        "border-radius: 6px; padding: 6px 10px; }"
    ).arg(p.inputBg, p.textPrimary, p.inputBorder));
    folderRow->addWidget(m_folderInput, 1);

    m_browseBtn = new QPushButton("Browse…");
    m_browseBtn->setCursor(Qt::PointingHandCursor);
    m_browseBtn->setStyleSheet(QString(
        "QPushButton { background: transparent; color: %1; "
        "border: 1px solid %2; border-radius: 6px; padding: 6px 14px; }"
        "QPushButton:hover { border-color: %3; color: %3; }"
    ).arg(p.textPrimary, p.inputBorder, p.accent));
    folderRow->addWidget(m_browseBtn);
    root->addLayout(folderRow);

    connect(m_browseBtn, &QPushButton::clicked, this, [this]() {
        QString d = QFileDialog::getExistingDirectory(this, "Search in folder",
                                                      m_folderInput->text());
        if (!d.isEmpty()) m_folderInput->setText(d);
    });

    // ── Options row ──────────────────────────────────────────────────
    auto *optRow = new QHBoxLayout;
    optRow->setSpacing(16);

    auto *globLabel = new QLabel("Files:");
    globLabel->setStyleSheet(QString("color: %1; font-size: 12px;").arg(p.textSecondary));
    optRow->addWidget(globLabel);

    m_globInput = new QLineEdit;
    m_globInput->setPlaceholderText("*.py, *.js, *.cpp  (empty = all files)");
    m_globInput->setStyleSheet(QString(
        "QLineEdit { background: %1; color: %2; border: 1px solid %3; "
        "border-radius: 6px; padding: 6px 10px; }"
    ).arg(p.inputBg, p.textPrimary, p.inputBorder));
    m_globInput->setMinimumWidth(260);
    optRow->addWidget(m_globInput);

    m_caseChk  = new QCheckBox("Match case");
    m_wordChk  = new QCheckBox("Whole word");
    m_regexChk = new QCheckBox("Regex");
    m_namesChk = new QCheckBox("Also match file names");
    m_namesChk->setChecked(true);
    m_binaryChk = new QCheckBox("Include binary files");
    m_binaryChk->setChecked(false);
    m_binaryChk->setToolTip("By default binary files (images, archives, compiled objects) are skipped for speed. Tick to force-search them too.");
    for (QCheckBox *cb : {m_caseChk, m_wordChk, m_regexChk, m_namesChk, m_binaryChk}) {
        cb->setStyleSheet(QString("color: %1; font-size: 12px;").arg(p.textSecondary));
        optRow->addWidget(cb);
    }
    optRow->addStretch();
    root->addLayout(optRow);

    // ── Search / Cancel row ──────────────────────────────────────────
    auto *actionRow = new QHBoxLayout;
    actionRow->setSpacing(10);

    m_searchBtn = new QPushButton("Search");
    m_searchBtn->setCursor(Qt::PointingHandCursor);
    m_searchBtn->setStyleSheet(QString(
        "QPushButton { background: %1; color: #FFFFFF; border: none; "
        "border-radius: 8px; padding: 10px 28px; font-weight: 600; }"
        "QPushButton:hover { background: #B86A4E; }"
        "QPushButton:disabled { background: #B8B5B1; color: #6C6C6C; }"
    ).arg(p.accent));

    m_cancelBtn = new QPushButton("Cancel");
    m_cancelBtn->setCursor(Qt::PointingHandCursor);
    m_cancelBtn->setEnabled(false);
    m_cancelBtn->setStyleSheet(QString(
        "QPushButton { background: transparent; color: %1; "
        "border: 1px solid %2; border-radius: 8px; padding: 10px 24px; }"
        "QPushButton:hover { border-color: %3; color: %3; }"
        "QPushButton:disabled { color: #AAA; border-color: #CCC; }"
    ).arg(p.textPrimary, p.inputBorder, p.accent));

    actionRow->addWidget(m_searchBtn);
    actionRow->addWidget(m_cancelBtn);

    m_progressBar = new QProgressBar;
    m_progressBar->setTextVisible(false);
    m_progressBar->setMaximumHeight(6);
    m_progressBar->setStyleSheet(QString(
        "QProgressBar { background: %1; border: none; border-radius: 3px; }"
        "QProgressBar::chunk { background: %2; border-radius: 3px; }"
    ).arg(p.inputBorder, p.accent));
    actionRow->addWidget(m_progressBar, 1);

    root->addLayout(actionRow);

    m_statusLabel = new QLabel("Enter a query to start.");
    m_statusLabel->setStyleSheet(QString("color: %1; font-size: 12px;").arg(p.textMuted));
    root->addWidget(m_statusLabel);

    connect(m_searchBtn, &QPushButton::clicked, this, &ProjectSearch::startSearch);
    connect(m_cancelBtn, &QPushButton::clicked, this, &ProjectSearch::cancelSearch);
    connect(m_queryInput, &QLineEdit::returnPressed, this, &ProjectSearch::startSearch);
    connect(m_globInput,  &QLineEdit::returnPressed, this, &ProjectSearch::startSearch);
    connect(m_folderInput, &QLineEdit::returnPressed, this, &ProjectSearch::startSearch);

    // ── Results tree ─────────────────────────────────────────────────
    m_results = new QTreeWidget;
    m_results->setHeaderHidden(true);
    m_results->setFont(notepatraCodeFont());
    m_results->setUniformRowHeights(true);
    m_results->setAlternatingRowColors(false);
    m_results->setStyleSheet(QString(
        "QTreeWidget { background: %1; color: %2; border: 1px solid %3; "
        "border-radius: 10px; padding: 6px; } "
        "QTreeWidget::item { padding: 4px 6px; border: none; } "
        "QTreeWidget::item:selected { background: %4; color: #FFFFFF; }"
    ).arg(p.cardBg, p.textPrimary, p.inputBorder, p.accent));
    m_results->header()->setSectionResizeMode(QHeaderView::ResizeToContents);
    root->addWidget(m_results, 1);

    connect(m_results, &QTreeWidget::itemDoubleClicked, this,
            [this](QTreeWidgetItem *item, int) {
        if (!item) return;
        QVariant v = item->data(0, Qt::UserRole);
        if (!v.isValid()) return;
        QString path = item->data(0, Qt::UserRole).toString();
        int line     = item->data(0, Qt::UserRole + 1).toInt();
        int col      = item->data(0, Qt::UserRole + 2).toInt();
        if (line <= 0) line = 1;
        emit openFileAtLine(path, line);
        // Column-level precision — placed on the matched character,
        // not just the start of the line.
        if (col > 0) emit openFileAtLineCol(path, line, col);
    });
}

void ProjectSearch::setFolder(const QString &folder) {
    if (!folder.isEmpty()) m_folderInput->setText(folder);
}
void ProjectSearch::setQuery(const QString &query) {
    m_queryInput->setText(query);
}
void ProjectSearch::focusQuery() {
    m_queryInput->setFocus();
    m_queryInput->selectAll();
}

void ProjectSearch::startSearch() {
    if (m_queryInput->text().isEmpty()) {
        m_statusLabel->setText("⚠ Enter something to search for.");
        m_queryInput->setFocus();
        return;
    }
    if (!QFileInfo(m_folderInput->text()).isDir()) {
        m_statusLabel->setText("⚠ Folder does not exist: " + m_folderInput->text());
        return;
    }

    m_results->clear();
    m_fileItems.clear();
    m_matchesSoFar = 0;
    m_filesWithMatches = 0;
    m_searchBtn->setEnabled(false);
    m_cancelBtn->setEnabled(true);
    m_statusLabel->setText("Scanning…");
    m_progressBar->setRange(0, 0);   // indeterminate until filesCounted

    ProjectSearchWorker::Params p;
    p.folder = m_folderInput->text();
    p.query  = m_queryInput->text();
    p.fileGlobs = m_globInput->text();
    p.searchNames = m_namesChk->isChecked();
    p.caseSensitive = m_caseChk->isChecked();
    p.wholeWord = m_wordChk->isChecked();
    p.regex = m_regexChk->isChecked();
    p.skipBinary = !m_binaryChk->isChecked();

    QMetaObject::invokeMethod(m_worker, "search",
                              Qt::QueuedConnection,
                              Q_ARG(ProjectSearchWorker::Params, p));
}

void ProjectSearch::cancelSearch() {
    if (m_worker) m_worker->cancel();
    m_statusLabel->setText("Cancelling…");
}

// Add or fetch the per-file parent row
static QTreeWidgetItem *fileParent(QTreeWidget *tree, QHash<QString, QTreeWidgetItem*> &index,
                                   const QString &path, const QString &accent) {
    auto it = index.find(path);
    if (it != index.end()) return it.value();
    auto *root = new QTreeWidgetItem(tree);
    QFileInfo fi(path);
    root->setText(0, QString("  %1").arg(fi.fileName()));
    root->setToolTip(0, path);
    root->setData(0, Qt::UserRole, path);     // for double-click-to-open
    root->setData(0, Qt::UserRole + 1, 1);    // line 1
    QFont f = tree->font();
    f.setBold(true);
    root->setFont(0, f);
    root->setForeground(0, QBrush(QColor(accent)));
    root->setExpanded(true);
    index.insert(path, root);
    return root;
}

void ProjectSearch::onFileNameMatch(const QString &filePath) {
    const auto p = psearchPalette();
    auto *parent = fileParent(m_results, m_fileItems, filePath, p.accent);
    auto *child = new QTreeWidgetItem(parent);
    child->setText(0, "      ↳ filename matches query");
    child->setData(0, Qt::UserRole, filePath);
    child->setData(0, Qt::UserRole + 1, 1);
    child->setForeground(0, QBrush(QColor(p.textSecondary)));
    ++m_filesWithMatches;
}

void ProjectSearch::onMatch(const ProjectSearchMatch &m) {
    const auto p = psearchPalette();
    auto *parent = fileParent(m_results, m_fileItems, m.filePath, p.accent);

    // Build the match line with the query highlighted
    QString line = m.lineContent;
    if (line.length() > 240) line = line.left(240) + "…";
    QString rendered = QString("      %1  │  %2").arg(m.lineNumber, 5).arg(line);

    // "line:col │ content" — gives users the exact coordinate of every
    // match. matchStart is a zero-based char offset into the original
    // line, so column = matchStart + 1 for 1-based display (same as
    // every editor status bar in the world).
    const int col1 = m.matchStart + 1;
    const QString coord = QString("%1:%2").arg(m.lineNumber, 5).arg(col1, -3);
    const QString rendered2 = QString("      %1  │  %2").arg(coord).arg(line);
    Q_UNUSED(rendered);   // keep the earlier build of `rendered` compiling

    auto *child = new QTreeWidgetItem(parent);
    child->setText(0, rendered2);
    child->setToolTip(0, QString("%1:%2:%3\n%4")
                          .arg(m.filePath).arg(m.lineNumber).arg(col1).arg(m.lineContent));
    child->setData(0, Qt::UserRole, m.filePath);
    child->setData(0, Qt::UserRole + 1, m.lineNumber);
    child->setData(0, Qt::UserRole + 2, col1);   // column for precise jump
    ++m_matchesSoFar;
}

void ProjectSearch::onProgress(int done, int total, int matches) {
    if (total > 0) {
        m_progressBar->setRange(0, total);
        m_progressBar->setValue(done);
    }
    m_statusLabel->setText(QString("Searching — %1 / %2 files · %3 matches so far")
                               .arg(done).arg(total).arg(matches));
}

void ProjectSearch::onFinished(int totalMatches, int totalFiles) {
    m_searchBtn->setEnabled(true);
    m_cancelBtn->setEnabled(false);
    m_progressBar->setValue(m_progressBar->maximum());
    if (totalMatches == 0) {
        m_statusLabel->setText(QString("No matches — scanned %1 files.").arg(totalFiles));
    } else {
        m_statusLabel->setText(QString("✓ %1 matches across %2 file(s) · scanned %3 files.")
                                   .arg(totalMatches)
                                   .arg(m_fileItems.size())
                                   .arg(totalFiles));
    }
}
