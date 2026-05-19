// SPDX-License-Identifier: GPL-3.0-or-later

#include "projectsearch.h"
#include "config.h"
#include "fonts.h"
#include "rustbridge.h"

#include <QMetaType>
#include <QtConcurrent/QtConcurrent>
#include <QMutex>
#include <QElapsedTimer>
#include <atomic>
#include <cstdio>
#include <functional>

static int s_paramsTypeId = qRegisterMetaType<ProjectSearchWorker::Params>("ProjectSearchWorker::Params");
static int s_matchTypeId  = qRegisterMetaType<ProjectSearchMatch>("ProjectSearchMatch");
static int s_matchVecId   = qRegisterMetaType<QVector<ProjectSearchMatch>>("QVector<ProjectSearchMatch>");

#include <QApplication>
#include <QCheckBox>
#include <QClipboard>
#include <QDateTime>
#include <QDir>
#include <QDirIterator>
#ifdef Q_OS_WIN
#include <windows.h>
#endif
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QLocale>
#include <QMenu>
#include <QProgressBar>
#include <QPushButton>
#include <QRegularExpression>
#include <QScrollArea>
#include <QScrollBar>
#include <QTextStream>
#include <QTimer>
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

// Directory names we never walk into — VCS metadata, dependency caches,
// build output, language virtual-envs, editor metadata. These alone can
// be 10× the source tree and always contain either binaries or generated
// code the user didn't write. Ripgrep skips the same set by default.
static bool isHeavyDir(const QString &name) {
    static const QStringList skip = {
        ".git", ".svn", ".hg", ".jj", ".bzr",
        "node_modules", "bower_components", "jspm_packages",
        "target", "build", "dist", "out", "bin", "obj",
        "__pycache__", ".venv", "venv", "env", ".env",
        ".cache", ".tox", ".mypy_cache", ".pytest_cache", ".ruff_cache",
        ".next", ".nuxt", ".turbo", ".angular", ".parcel-cache",
        ".gradle", ".idea", ".vscode", ".vs",
        "vendor", "Pods", "DerivedData",
        ".terraform", ".serverless",
        "coverage", ".nyc_output",
    };
    return skip.contains(name);
}

// Manual recursive walk that prunes heavy directories up-front, instead
// of letting QDirIterator recurse into every node_modules/ tree only to
// have us filter 50 000 files out later. Writes absolute paths of
// filtered source files into `out`. Invokes `progress` every time `out`
// crosses a multiple of `progressEvery` so the UI can show live counts
// during a long walk — critical when the user points this at a home
// directory or a monorepo with millions of files.
static void walkSourceTree(const QString &root, const QStringList &globs,
                           QStringList &out, std::atomic<bool> &cancel,
                           int progressEvery,
                           const std::function<void(int)> &progress) {
    QDir d(root);
    const QFileInfoList entries = d.entryInfoList(
        QDir::Files | QDir::Dirs | QDir::NoDotAndDotDot | QDir::NoSymLinks);
    for (const QFileInfo &fi : entries) {
        if (cancel.load()) return;
        if (fi.isDir()) {
            if (isHeavyDir(fi.fileName())) continue;
            walkSourceTree(fi.absoluteFilePath(), globs, out, cancel,
                           progressEvery, progress);
            // Emit once per visited subdirectory too — gives the user a
            // live count even when individual directories are small.
            if (progress) progress(out.size());
        } else if (fi.isFile()) {
            if (!matchesAnyGlob(fi.fileName(), globs)) continue;
            out.append(fi.absoluteFilePath());
            // Fire on every file during the early ramp so the counter
            // visibly ticks from 0 upward, then drop to progressEvery
            // so we don't flood the UI thread for huge trees.
            const int n = out.size();
            if (progress && (n <= 20 || n % progressEvery == 0))
                progress(n);
        }
    }
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
    QElapsedTimer timer;
    timer.start();

    if (p.query.isEmpty()) {
        emit finishedSearch(0, 0, timer.elapsed(), 0);
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

    // Walk the tree, pruning heavy dirs (.git / node_modules / target / …)
    // up-front instead of visiting them just to filter later. Emit
    // walkProgress every 500 files so a slow walk (eg $HOME with millions
    // of files) shows activity instead of appearing frozen on "Scanning…".
    QStringList queue;
    queue.reserve(4096);
    auto walkCb = [this](int n) { emit walkProgress(n); };
    // Fire walkProgress every 50 files so small folder trees still see
    // live updates (the previous 500 was too coarse — a 200-file tree
    // got zero walk updates, UI looked frozen).
    walkSourceTree(p.folder, globs, queue, m_cancel, /*progressEvery*/ 50, walkCb);
    if (m_cancel.load()) { emit finishedSearch(0, 0, timer.elapsed(), 0); return; }
    const int totalFiles = queue.size();
    emit filesCounted(totalFiles);
    // Emit an immediate 0-progress with the real total so the progress bar
    // leaves its indeterminate state the moment the walk completes — even
    // if no file finishes scanning for another few ms.
    emit progress(0, totalFiles, 0, timer.elapsed(), 0);

    // Shared counters — threads bump these via atomic ops so the progress
    // signal stays coherent when several workers finish at the same time.
    std::atomic<int> filesDoneAtomic{0};
    std::atomic<int> totalMatchesAtomic{0};
    std::atomic<qint64> totalLinesAtomic{0};

    auto searchOne = [&, this](const QString &path) {
        if (m_cancel.load()) return;
        QFileInfo fi(path);

        // Emit the current path BEFORE any read — so when the scan
        // appears frozen (Windows OneDrive placeholder materializing,
        // network drive stall, AV lock, 2 GB log with no newlines…),
        // the user can see EXACTLY which file is stuck instead of
        // staring at a stalled progress bar.
        emit fileStarted(path);

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

        // Helper: bump filesDone atomically and emit a throttled progress
        // update. Called on every early-return path so the UI always sees
        // progress even when files are skipped. Fires every 8 files (was
        // every 32) so small repos show updates mid-scan instead of
        // appearing frozen until the very end.
        auto tick = [&]() {
            const int fd = ++filesDoneAtomic;
            // Tick every 4 files (was every 8). Faster progress-bar motion
            // on small trees + earlier detection if the scan has wedged.
            if ((fd & 0x03) == 0 || fd == totalFiles)
                emit progress(fd, totalFiles, totalMatchesAtomic.load(),
                              timer.elapsed(), totalLinesAtomic.load());
        };

        // Skip files above the sanity cap (2 GB default — effectively no cap
        // for source trees; still guards against accidentally iterating
        // massive archive files).
        if (fi.size() > p.maxFileSizeBytes) { tick(); return; }

#ifdef Q_OS_WIN
        // Skip OneDrive / cloud-storage placeholder files that would trigger
        // a giant network download the moment we try to open them. These
        // are the #1 cause of the "Project Search froze at 12 %" bug on
        // Windows — the walker enumerates thousands of *.py / *.md files
        // that are actually 0-byte stubs, each open() pulls GBs over WAN.
        //   FILE_ATTRIBUTE_RECALL_ON_OPEN         = 0x00040000
        //   FILE_ATTRIBUTE_RECALL_ON_DATA_ACCESS  = 0x00400000
        //   FILE_ATTRIBUTE_OFFLINE                = 0x00001000
        {
            DWORD attr = GetFileAttributesW(
                reinterpret_cast<const wchar_t*>(path.utf16()));
            if (attr != INVALID_FILE_ATTRIBUTES &&
                (attr & (0x00040000 | 0x00400000 | 0x00001000)) != 0) {
                tick();
                return;
            }
        }
#endif

        // Per-file elapsed watchdog — if any single file takes longer than
        // 30 seconds to open + read + scan (antivirus holding it, network
        // drive timing out, a 2 GB log with no newlines, whatever) we bail
        // on it and move on. Prevents one pathological file from wedging
        // the whole scan at N % forever.
        QElapsedTimer perFileTimer;
        perFileTimer.start();
        constexpr qint64 kPerFileCapMs = 30000;

        // Skip binary files so we don't waste time (and memory) scanning
        // images / compiled objects / archives.
        if (p.skipBinary && looksBinary(path)) { tick(); return; }

        QFile f(path);
        if (!f.open(QIODevice::ReadOnly | QIODevice::Text)) { tick(); return; }

        // Collect all matches for this file, emit in ONE queued event.
        // Per-match emit through QueuedConnection was costing ~1 µs of UI
        // thread time per match; on a query that hits 10 000 times that's
        // 10 ms of UI-thread pressure that shows up as jank. One emit per
        // file is 100×–1000× cheaper.
        QVector<ProjectSearchMatch> fileHits;
        const int maxMatchesPerFile = 500;

        // ── Rust-fast path: plain-text (non-regex, non-word) literal search
        //    on files that fit in memory. aho-corasick via RustCore::findAll
        //    is 5–50× faster than our line-by-line C++ fallback for typical
        //    source. Large files fall through to the streaming path below.
        constexpr qint64 kRustFastCap = 8 * 1024 * 1024;  // 8 MB
        if (!useRegex && fi.size() <= kRustFastCap) {
            const QByteArray raw = f.readAll();
            f.close();

            // Pre-build line-start table over the raw UTF-8 bytes. Done
            // UNCONDITIONALLY so files with zero matches still contribute
            // to the lines-scanned counter — the user wants to see
            // "N lines scanned" reflect actual scan volume, not hit volume.
            // upper_bound over this table then answers "which line is
            // byte N on?" in O(log lines) per match when we do find hits.
            QVector<int> lineStarts;
            lineStarts.reserve(int(raw.size() / 40) + 8);
            lineStarts.append(0);
            for (int i = 0; i < raw.size(); ++i) {
                if (raw[i] == '\n') lineStarts.append(i + 1);
            }
            // lineStarts.size() == total line count (the initial 0 entry
            // covers line 1; every '\n' pushes the start of the next).
            totalLinesAtomic += qint64(lineStarts.size());

            // Hand the bytes to Rust aho-corasick for literal search.
            const QString body = QString::fromUtf8(raw);
            const QVector<size_t> posBytes =
                RustCore::findAll(body, p.query, /*isRegex*/ false,
                                  p.caseSensitive, /*wholeWord*/ false);
            if (!posBytes.isEmpty()) {
                const int queryBytes = p.query.toUtf8().size();
                int lastLineNum = -1;
                QString cachedLineContent;
                int cachedLineStart = 0;
                for (size_t bytePos : posBytes) {
                    if (m_cancel.load()) break;
                    auto it = std::upper_bound(lineStarts.begin(), lineStarts.end(),
                                               static_cast<int>(bytePos));
                    int lineNum = int(it - lineStarts.begin());
                    int lineStart = lineStarts[lineNum - 1];
                    // Cache the decoded line text across consecutive matches
                    // on the SAME line — a query that hits 10× on one line
                    // pays one decode, not ten.
                    if (lineNum != lastLineNum) {
                        int lineEnd = (lineNum < lineStarts.size())
                                      ? lineStarts[lineNum] - 1 : raw.size();
                        cachedLineContent = QString::fromUtf8(
                            raw.mid(lineStart, lineEnd - lineStart));
                        cachedLineStart = lineStart;
                        lastLineNum = lineNum;
                    }
                    int col = QString::fromUtf8(
                        raw.mid(cachedLineStart, bytePos - cachedLineStart)).size();
                    ProjectSearchMatch pm;
                    pm.filePath    = path;
                    pm.lineNumber  = lineNum;
                    pm.lineContent = cachedLineContent;
                    pm.matchStart  = col;
                    pm.matchLength = QString::fromUtf8(raw.mid(bytePos, queryBytes)).size();
                    fileHits.append(std::move(pm));
                    ++totalMatchesAtomic;
                    if (fileHits.size() >= maxMatchesPerFile) break;
                }
            }
            if (!fileHits.isEmpty()) emit matchesFound(fileHits);
            tick();
            return;
        }

        // Streaming path — regex searches + very large files. Reads one line
        // at a time so a 2 GB log doesn't blow RAM.
        QTextStream ts(&f);
        ts.setCodec("UTF-8");
        int lineNum = 0;
        while (!ts.atEnd()) {
            if (m_cancel.load()) break;
            // Per-file watchdog — bail on a file that's eating too much
            // wall-clock so the scan keeps progressing. Checked every line
            // so a 2 GB log with occasional newlines can exit promptly.
            if (perFileTimer.elapsed() > kPerFileCapMs) break;
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
                    fileHits.append(std::move(pm));
                    ++totalMatchesAtomic;
                    if (fileHits.size() >= maxMatchesPerFile) break;
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
                    fileHits.append(std::move(pm));
                    ++totalMatchesAtomic;
                    if (fileHits.size() >= maxMatchesPerFile) break;
                    from = idx + literal.length();
                }
            }
            if (fileHits.size() >= maxMatchesPerFile) break;
        }
        // In the streaming path, lineNum is the total lines we read
        // (regardless of match count). Feed into the running total.
        totalLinesAtomic += qint64(lineNum);
        if (!fileHits.isEmpty()) emit matchesFound(fileHits);
        tick();
    };

    // Run the per-file searcher in parallel across the Qt thread pool —
    // blockingMap waits for all workers before returning, which keeps our
    // finishedSearch emission sequential with the last match. Thread pool
    // default size = QThread::idealThreadCount() (all CPU cores), so on a
    // 4-core laptop the walk is ~3–4× faster than the serial version.
    QtConcurrent::blockingMap(queue, searchOne);

    emit finishedSearch(totalMatchesAtomic.load(), totalFiles,
                        timer.elapsed(), totalLinesAtomic.load());
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

    // 10 Hz UI-side refresher so the elapsed-ms display ticks visibly
    // between worker progress events. Stopped in onFinished() — once the
    // bar reaches 100 %, the final status line is authoritative.
    m_liveTimer = new QTimer(this);
    m_liveTimer->setInterval(100);
    connect(m_liveTimer, &QTimer::timeout, this, &ProjectSearch::refreshLiveStatus);

    connect(m_worker, &ProjectSearchWorker::matchesFound,
            this, &ProjectSearch::onMatches, Qt::QueuedConnection);
    connect(m_worker, &ProjectSearchWorker::fileNameMatch,
            this, &ProjectSearch::onFileNameMatch, Qt::QueuedConnection);
    connect(m_worker, &ProjectSearchWorker::progress,
            this, &ProjectSearch::onProgress, Qt::QueuedConnection);
    connect(m_worker, &ProjectSearchWorker::finishedSearch,
            this, &ProjectSearch::onFinished, Qt::QueuedConnection);
    // Live walk progress — walk phase maps to 0 % … 25 % of the bar so the
    // user sees it crawling up even before we know the total file count.
    // ~100 files discovered per 1 %, capped at 25 %.
    connect(m_worker, &ProjectSearchWorker::walkProgress,
            this, [this](int n) {
        if (m_phase == Phase::Idle) return;   // cancel already fired; ignore
        m_phase = Phase::Walking;
        m_lastWalkDiscovered = n;
        m_progressBar->setRange(0, 100);
        m_progressBar->setValue(0);
        refreshLiveStatus();
    }, Qt::QueuedConnection);
    connect(m_worker, &ProjectSearchWorker::filesCounted,
            this, [this](int n) {
        if (m_phase == Phase::Idle) return;
        // Walk complete — NOW we know the total. Bar starts at 0 % and
        // fills honestly as files are scanned.
        m_phase = Phase::Scanning;
        m_lastFilesTotal = n;
        m_progressBar->setRange(0, 100);
        m_progressBar->setValue(0);
        refreshLiveStatus();
    }, Qt::QueuedConnection);
    // Diagnostic signal — the worker emits the path of every file BEFORE it
    // reads it. If the scan freezes (Windows OneDrive/network drive/AV lock,
    // huge file with no newlines, whatever), the status label shows exactly
    // which file the stuck worker is holding. Throttled by m_liveTimer's
    // 10 Hz tick so we don't flood the UI with hundreds of paths per second
    // across parallel threads — we just capture the latest.
    connect(m_worker, &ProjectSearchWorker::fileStarted,
            this, [this](const QString &path) {
        m_lastFileInFlight = path;
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

    // ── Page-level scroll area ──────────────────────────────────────
    // The WHOLE Project Search tab scrolls vertically when results
    // grow beyond the visible area. The match tree reports its own
    // ideal height (one row per item, expanded) so it spills to the
    // bottom of the page and the page-scrollbar takes over. This is
    // the behaviour modern code editors (VS Code, Cursor, Sublime)
    // use for their search views — one scroll, not two.
    auto *outer = new QVBoxLayout(this);
    outer->setContentsMargins(0, 0, 0, 0);
    outer->setSpacing(0);

    m_scrollArea = new QScrollArea(this);
    m_scrollArea->setWidgetResizable(true);
    m_scrollArea->setFrameShape(QFrame::NoFrame);
    m_scrollArea->setHorizontalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    m_scrollArea->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);

    m_content = new QWidget;
    auto *root = new QVBoxLayout(m_content);
    root->setContentsMargins(20, 18, 20, 18);
    root->setSpacing(14);

    m_scrollArea->setWidget(m_content);
    outer->addWidget(m_scrollArea);

    // ── Title row + close button ─────────────────────────────────────
    // v0.1.44 — header is a row with the title stretched and a red ✕
    // close button at the far right that emits closeRequested(). The
    // colour is theme-independent (Windows-canonical close-button red
    // #E81123, white-on-red on hover) so it stays visible on every
    // theme without consulting the palette.
    auto *titleRow = new QHBoxLayout;
    titleRow->setContentsMargins(0, 0, 0, 0);
    titleRow->setSpacing(8);

    m_title = new QLabel("🔍 Project Search");
    QFont tf = notepatraUiFont();
    tf.setPointSize(18);
    tf.setWeight(QFont::DemiBold);
    m_title->setFont(tf);
    titleRow->addWidget(m_title, /*stretch*/ 1);

    auto *closeBtn = new QPushButton("×");
    QFont closeFont = closeBtn->font();
    closeFont.setPointSize(18);
    closeFont.setBold(true);
    closeBtn->setFont(closeFont);
    closeBtn->setFixedSize(36, 28);
    closeBtn->setCursor(Qt::PointingHandCursor);
    closeBtn->setFlat(true);
    closeBtn->setToolTip("Close Project Search");
    closeBtn->setStyleSheet(
        "QPushButton { background: transparent; border: none; "
        "color: #E81123; font-weight: 700; padding: 0; } "
        "QPushButton:hover { background: #E81123; color: white; } "
        "QPushButton:pressed { background: #C41019; color: white; }");
    connect(closeBtn, &QPushButton::clicked, this, [this]() {
        emit closeRequested();
    });
    titleRow->addWidget(closeBtn);

    root->addLayout(titleRow);

    // Compact hint — the old three-line paragraph was eating vertical
    // space from the results view. One short line is plenty; full doc
    // lives in Help → Feature and Tool Guide.
    m_hint = new QLabel("Stream search across file names + contents. Double-click a match to jump to that line.");
    m_hint->setWordWrap(true);
    root->addWidget(m_hint);

    // ── Query input (big, prominent) ─────────────────────────────────
    m_queryInput = new QLineEdit;
    m_queryInput->setPlaceholderText("Search any text — words, phrases like \"import os\", or regex patterns…");
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
    m_folderLabel = new QLabel("Folder:");
    folderRow->addWidget(m_folderLabel);

    // v0.1.91 — native separators so Windows users see C:\Users\… not
    // C:/Users/… in the folder field. Qt's QDir / QFileInfo accept both
    // styles on Windows, so the worker doesn't need any matching change.
    m_folderInput = new QLineEdit(QDir::toNativeSeparators(QDir::homePath()));
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
        if (!d.isEmpty()) m_folderInput->setText(QDir::toNativeSeparators(d));
    });

    // ── Options row ──────────────────────────────────────────────────
    auto *optRow = new QHBoxLayout;
    optRow->setSpacing(16);

    m_globLabel = new QLabel("Files:");
    optRow->addWidget(m_globLabel);

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

    // v0.1.51 — Cancel + Clear history button labels in a strong orange
    // (#E67E22) that's visible on Light, Dark, and Monokai themes. The
    // previous styling used `p.textPrimary` (dark grey on Light, light
    // grey on Dark) for the enabled state and `#AAA` for disabled, which
    // made the labels nearly invisible on Light when disabled. Orange
    // gives high contrast on every theme background.
    const QString orangeFg     = QStringLiteral("#E67E22");
    const QString orangeHover  = QStringLiteral("#FFA94D");
    const QString orangeMuted  = QStringLiteral("#C97B3F");

    m_cancelBtn = new QPushButton("Cancel");
    m_cancelBtn->setCursor(Qt::PointingHandCursor);
    m_cancelBtn->setEnabled(false);
    m_cancelBtn->setStyleSheet(QString(
        "QPushButton { background: transparent; color: %1; font-weight: 600; "
        "border: 1px solid %2; border-radius: 8px; padding: 10px 24px; }"
        "QPushButton:hover { border-color: %3; color: %3; }"
        "QPushButton:disabled { color: %4; border-color: %4; }"
    ).arg(orangeFg, p.inputBorder, orangeHover, orangeMuted));

    // v0.1.44 — Clear history wipes every stacked session in the
    // results tree. Disabled until at least one session exists.
    m_clearHistoryBtn = new QPushButton("Clear history");
    m_clearHistoryBtn->setCursor(Qt::PointingHandCursor);
    m_clearHistoryBtn->setEnabled(false);
    m_clearHistoryBtn->setToolTip("Remove every stacked search session");
    m_clearHistoryBtn->setStyleSheet(QString(
        "QPushButton { background: transparent; color: %1; font-weight: 600; "
        "border: 1px solid %2; border-radius: 8px; padding: 10px 16px; }"
        "QPushButton:hover { border-color: %3; color: %3; }"
        "QPushButton:disabled { color: %4; border-color: %4; }"
    ).arg(orangeFg, p.inputBorder, orangeHover, orangeMuted));
    connect(m_clearHistoryBtn, &QPushButton::clicked, this, [this]() {
        m_results->clear();
        m_sessions.clear();
        m_perSessionFiles.clear();
        m_currentSession = nullptr;
        m_clearHistoryBtn->setEnabled(false);
        if (m_resizeTree) m_resizeTree();
    });

    actionRow->addWidget(m_searchBtn);
    actionRow->addWidget(m_cancelBtn);
    actionRow->addWidget(m_clearHistoryBtn);

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
    {
        QFont treeFont = notepatraCodeFont();
        // v0.1.48 — same Linux-only emoji fallback as SearchResultsPanel:
        // 🔍 / 🔎 in session headers were tofu-boxes on most Linux distros
        // because the default monospace font has no emoji glyphs.
#ifdef Q_OS_LINUX
        QStringList families = treeFont.families();
        if (families.isEmpty()) families << treeFont.family();
        for (const QString &e : QStringList{
                "Noto Color Emoji", "Twemoji Mozilla", "Symbola", "Joypixels"}) {
            if (!families.contains(e, Qt::CaseInsensitive)) families << e;
        }
        treeFont.setFamilies(families);
#endif
        m_results->setFont(treeFont);
    }
    m_results->setUniformRowHeights(true);
    m_results->setAlternatingRowColors(false);
    // NO internal scrollbars — the tree grows with its content and the
    // outer page scroll handles overflow. This is the "one scroll, not
    // two" UX the user asked for.
    m_results->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_results->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_results->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Minimum);
    m_results->setStyleSheet(QString(
        "QTreeWidget { background: %1; color: %2; border: 1px solid %3; "
        "border-radius: 10px; padding: 6px; } "
        "QTreeWidget::item { padding: 4px 6px; border: none; } "
        "QTreeWidget::item:selected { background: %4; color: #FFFFFF; }"
    ).arg(p.cardBg, p.textPrimary, p.inputBorder, p.accent));
    m_results->header()->setSectionResizeMode(QHeaderView::ResizeToContents);
    root->addWidget(m_results);   // no stretch — size-to-content

    // Whenever rows are added/removed/expanded/collapsed, recompute the
    // tree's preferred height so it grows to fit every visible row and
    // the outer QScrollArea takes over from there. Without this Qt
    // gives the tree its default ~200 px and everything below clips.
    auto resizeTree = [this]() {
        const int rowH = qMax(20, m_results->sizeHintForRow(0));
        int visible = 0;
        for (int i = 0; i < m_results->topLevelItemCount(); ++i) {
            visible++;   // parent row
            QTreeWidgetItem *top = m_results->topLevelItem(i);
            if (top->isExpanded()) visible += top->childCount();
        }
        if (visible < 1) visible = 1;
        // Add padding for the border/margins we applied via stylesheet.
        const int h = rowH * visible + 16;
        m_results->setFixedHeight(h);
    };
    m_resizeTree = resizeTree;
    connect(m_results, &QTreeWidget::itemExpanded,  this, [this]() { m_resizeTree(); });
    connect(m_results, &QTreeWidget::itemCollapsed, this, [this]() { m_resizeTree(); });
    // Initial compact height so the widget has a reasonable starting look
    m_results->setFixedHeight(240);

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

    // Right-click context menu — "Copy location" and "Copy match line".
    // Standard ripgrep-style `path:line:col` string goes on the clipboard
    // so the user can paste it into a terminal, IDE jump, grep output
    // etc. The match's line text is also copyable on its own.
    m_results->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(m_results, &QWidget::customContextMenuRequested, this,
            [this](const QPoint &pos) {
        QTreeWidgetItem *item = m_results->itemAt(pos);
        if (!item) return;
        QString path = item->data(0, Qt::UserRole).toString();
        if (path.isEmpty()) return;
        int line = item->data(0, Qt::UserRole + 1).toInt();
        int col  = item->data(0, Qt::UserRole + 2).toInt();

        QMenu menu(m_results);
        auto *actLoc = menu.addAction("Copy location (path:line:col)");
        auto *actPath = menu.addAction("Copy full path");
        QAction *actLine = nullptr;
        if (col > 0) {
            // Only child items (actual matches) have a real line:col.
            actLine = menu.addAction("Copy match line text");
        }
        QAction *picked = menu.exec(m_results->viewport()->mapToGlobal(pos));
        if (!picked) return;
        QClipboard *cb = QApplication::clipboard();
        if (picked == actLoc) {
            const QString nativePath = QDir::toNativeSeparators(path);
            cb->setText(col > 0
                ? QString("%1:%2:%3").arg(nativePath).arg(line).arg(col)
                : nativePath);
        } else if (picked == actPath) {
            cb->setText(QDir::toNativeSeparators(path));
        } else if (picked == actLine) {
            cb->setText(item->text(0).trimmed());
        }
    });

    // All visible stylesheets (bg, inputs, buttons, tree, scrollbars) are
    // centralised in applyPalette() so the runtime theme-change slot
    // (onThemeChanged) can re-render without rebuilding the UI tree.
    applyPalette();
}

void ProjectSearch::applyPalette() {
    const auto p = psearchPalette();

    setStyleSheet(QString("ProjectSearch { background: %1; }").arg(p.bg));

    if (m_scrollArea) {
        m_scrollArea->setStyleSheet(QString(
            "QScrollArea { background: %1; border: none; } "
            "QScrollBar:vertical { background: %1; width: 12px; margin: 0; } "
            "QScrollBar::handle:vertical { background: %2; border-radius: 5px; min-height: 30px; } "
            "QScrollBar::handle:vertical:hover { background: %3; } "
            "QScrollBar::add-line:vertical, QScrollBar::sub-line:vertical { height: 0; } "
            "QScrollBar:horizontal { background: %1; height: 12px; margin: 0; } "
            "QScrollBar::handle:horizontal { background: %2; border-radius: 5px; min-width: 30px; } "
            "QScrollBar::handle:horizontal:hover { background: %3; } "
            "QScrollBar::add-line:horizontal, QScrollBar::sub-line:horizontal { width: 0; }"
        ).arg(p.bg, p.inputBorder, p.accent));
    }
    if (m_content) {
        m_content->setStyleSheet(QString("background: %1;").arg(p.bg));
    }
    if (m_title) {
        m_title->setStyleSheet(QString("color: %1;").arg(p.textPrimary));
    }
    if (m_hint) {
        m_hint->setStyleSheet(QString("color: %1; font-size: 11px;").arg(p.textSecondary));
    }
    if (m_queryInput) {
        m_queryInput->setStyleSheet(QString(
            "QLineEdit { background: %1; color: %2; border: 2px solid %3; "
            "border-radius: 10px; padding: 12px 16px; }"
            "QLineEdit:focus { border-color: %4; }"
        ).arg(p.inputBg, p.textPrimary, p.inputBorder, p.accent));
    }
    if (m_folderLabel) {
        m_folderLabel->setStyleSheet(QString(
            "color: %1; font-size: 12px;").arg(p.textSecondary));
    }
    if (m_folderInput) {
        m_folderInput->setStyleSheet(QString(
            "QLineEdit { background: %1; color: %2; border: 1px solid %3; "
            "border-radius: 6px; padding: 6px 10px; }"
        ).arg(p.inputBg, p.textPrimary, p.inputBorder));
    }
    if (m_browseBtn) {
        m_browseBtn->setStyleSheet(QString(
            "QPushButton { background: transparent; color: %1; "
            "border: 1px solid %2; border-radius: 6px; padding: 6px 14px; }"
            "QPushButton:hover { border-color: %3; color: %3; }"
        ).arg(p.textPrimary, p.inputBorder, p.accent));
    }
    if (m_globLabel) {
        m_globLabel->setStyleSheet(QString(
            "color: %1; font-size: 12px;").arg(p.textSecondary));
    }
    if (m_globInput) {
        m_globInput->setStyleSheet(QString(
            "QLineEdit { background: %1; color: %2; border: 1px solid %3; "
            "border-radius: 6px; padding: 6px 10px; }"
        ).arg(p.inputBg, p.textPrimary, p.inputBorder));
    }
    for (QCheckBox *cb : {m_caseChk, m_wordChk, m_regexChk, m_namesChk, m_binaryChk}) {
        if (!cb) continue;
        cb->setStyleSheet(QString(
            "color: %1; font-size: 12px;").arg(p.textSecondary));
    }
    if (m_searchBtn) {
        m_searchBtn->setStyleSheet(QString(
            "QPushButton { background: %1; color: #FFFFFF; border: none; "
            "border-radius: 8px; padding: 10px 28px; font-weight: 600; }"
            "QPushButton:hover { background: #B86A4E; }"
            "QPushButton:disabled { background: #B8B5B1; color: #6C6C6C; }"
        ).arg(p.accent));
    }
    if (m_cancelBtn) {
        // v0.1.51 — orange label visible on every theme.
        m_cancelBtn->setStyleSheet(QString(
            "QPushButton { background: transparent; color: #E67E22; font-weight: 600; "
            "border: 1px solid %1; border-radius: 8px; padding: 10px 24px; }"
            "QPushButton:hover { border-color: #FFA94D; color: #FFA94D; }"
            "QPushButton:disabled { color: #C97B3F; border-color: #C97B3F; }"
        ).arg(p.inputBorder));
    }
    if (m_clearHistoryBtn) {
        m_clearHistoryBtn->setStyleSheet(QString(
            "QPushButton { background: transparent; color: #E67E22; font-weight: 600; "
            "border: 1px solid %1; border-radius: 8px; padding: 10px 16px; }"
            "QPushButton:hover { border-color: #FFA94D; color: #FFA94D; }"
            "QPushButton:disabled { color: #C97B3F; border-color: #C97B3F; }"
        ).arg(p.inputBorder));
    }
    if (m_progressBar) {
        m_progressBar->setStyleSheet(QString(
            "QProgressBar { background: %1; border: none; border-radius: 3px; }"
            "QProgressBar::chunk { background: %2; border-radius: 3px; }"
        ).arg(p.inputBorder, p.accent));
    }
    if (m_statusLabel) {
        m_statusLabel->setStyleSheet(QString(
            "color: %1; font-size: 12px;").arg(p.textMuted));
    }
    if (m_results) {
        m_results->setStyleSheet(QString(
            "QTreeWidget { background: %1; color: %2; border: 1px solid %3; "
            "border-radius: 10px; padding: 6px; } "
            "QTreeWidget::item { padding: 4px 6px; border: none; } "
            "QTreeWidget::item:selected { background: %4; color: #FFFFFF; }"
        ).arg(p.cardBg, p.textPrimary, p.inputBorder, p.accent));
    }
}

void ProjectSearch::onThemeChanged() {
    applyPalette();
    update();
}

QString ProjectSearch::currentStatusText() const {
    return m_statusLabel ? m_statusLabel->text() : QString();
}
int ProjectSearch::currentProgressValue() const {
    return m_progressBar ? m_progressBar->value() : 0;
}

void ProjectSearch::setFolder(const QString &folder) {
    if (!folder.isEmpty()) m_folderInput->setText(QDir::toNativeSeparators(folder));
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

    // v0.1.44 — DON'T clear the tree any more. Each search becomes a
    // new top-level "session" row stacked at the top, with prior
    // searches collapsed underneath. Capped at 10 sessions; the oldest
    // is pruned when the cap is exceeded.
    const QString queryText = m_queryInput->text().trimmed();
    const QString folderText = m_folderInput->text();
    const QString flagsLabel = QString("%1%2%3")
        .arg(m_caseChk->isChecked() ? "Aa "  : "")
        .arg(m_wordChk->isChecked() ? "W "   : "")
        .arg(m_regexChk->isChecked() ? ".*"  : "");
    const QString stamp = QDateTime::currentDateTime().toString("HH:mm:ss");
    const auto pal = psearchPalette();

    // Collapse every prior session so the new one stands out without
    // hiding the history below it.
    for (QTreeWidgetItem *prior : m_sessions) {
        if (prior) prior->setExpanded(false);
    }

    m_currentSession = new QTreeWidgetItem;
    // Insert at the TOP of the tree so the most recent session is
    // visible without scrolling.
    m_results->insertTopLevelItem(0, m_currentSession);
    m_currentSession->setText(0, QString("🔎  \"%1\"  %2 — searching… · %3 · %4")
                                     .arg(queryText, flagsLabel, folderText, stamp));
    QFont sf = m_results->font();
    sf.setBold(true);
    sf.setPointSize(sf.pointSize() + 1);
    m_currentSession->setFont(0, sf);
    m_currentSession->setForeground(0, QBrush(QColor(pal.accent)));
    m_currentSession->setExpanded(true);
    m_sessions.append(m_currentSession);
    m_perSessionFiles.insert(m_currentSession, {});

    // Cap history at 10. Oldest sessions evicted from the top of the
    // tree (and from m_sessions / m_perSessionFiles).
    constexpr int kMaxSessions = 10;
    while (m_sessions.size() > kMaxSessions) {
        QTreeWidgetItem *oldest = m_sessions.takeFirst();
        m_perSessionFiles.remove(oldest);
        delete m_results->takeTopLevelItem(m_results->indexOfTopLevelItem(oldest));
    }

    if (m_resizeTree) m_resizeTree();
    if (m_clearHistoryBtn) m_clearHistoryBtn->setEnabled(true);
    m_matchesSoFar = 0;
    m_filesWithMatches = 0;
    m_searchBtn->setEnabled(false);
    m_cancelBtn->setEnabled(true);

    // Reset live counters + start wall-clock + 10 Hz UI refresher.
    m_phase = Phase::Walking;
    m_lastFilesDone = m_lastFilesTotal = m_lastMatches = 0;
    m_lastLines = 0;
    m_lastWalkDiscovered = 0;
    m_wallTimer.start();
    if (m_liveTimer) m_liveTimer->start();

    m_statusLabel->setText("Walking folder tree — 0 files discovered (0%)…");
    // Determinate bar that grows 0 → 100% across the entire search.
    // The user explicitly does NOT want the bouncing "indeterminate"
    // animation during the walk — they want to see percentage filling.
    //
    // We map the two phases onto [0, 100]:
    //   • walk (unknown total ahead of time) → 0 % to 25 %, ticked by
    //     walkProgress. Each ~100 files discovered adds 1 %, capped.
    //   • scan (known total after filesCounted) → 25 % to 100 %, mapped
    //     from filesDone / filesTotal.
    // Result: bar rises smoothly left-to-right for the whole operation;
    // never bounces, never sits at zero for long.
    m_progressBar->setRange(0, 100);
    m_progressBar->setValue(0);

    ProjectSearchWorker::Params p;
    p.folder = m_folderInput->text();
    // v0.1.36 — trim leading/trailing whitespace from the query so a
    // user who accidentally types " import os " gets the same matches
    // as "import os". Internal whitespace is preserved (multi-word
    // phrase support). For regex mode the trim is also safe: leading
    // whitespace in a regex is rarely intentional and `\s+` users can
    // express it explicitly.
    p.query  = m_queryInput->text().trimmed();
    p.fileGlobs = m_globInput->text();
    p.searchNames = m_namesChk->isChecked();
    p.caseSensitive = m_caseChk->isChecked();
    p.wholeWord = m_wordChk->isChecked();
    p.regex = m_regexChk->isChecked();
    p.skipBinary = !m_binaryChk->isChecked();

    // Queue the search onto the worker thread via a lambda — bypasses
    // Qt's string-based method lookup which can't match ProjectSearchWorker::
    // Params vs the slot's unqualified Params argument (this was silently
    // failing, leaving the worker never called and the UI sitting forever
    // at "0 files discovered").
    ProjectSearchWorker *w = m_worker;
    QMetaObject::invokeMethod(m_worker, [w, p]() { w->search(p); },
                              Qt::QueuedConnection);
}

void ProjectSearch::cancelSearch() {
    // 1. Flag the worker (atomic bool). Every loop in walkSourceTree and
    //    searchOne re-reads this flag and bails out the moment it's true.
    if (m_worker) m_worker->cancel();
    // 2. Freeze the UI counters — no more live ticks while we wait for the
    //    worker to unwind. finishedSearch will fire shortly and give us the
    //    final numbers.
    m_phase = Phase::Idle;
    if (m_liveTimer && m_liveTimer->isActive()) m_liveTimer->stop();
    // 3. Immediate user feedback — no wait.
    m_statusLabel->setText("Cancelling… (stopping worker threads)");
    m_cancelBtn->setEnabled(false);
    // Search button stays disabled until finishedSearch confirms the
    // worker has fully stopped — prevents kicking off a second search on
    // top of one that's still unwinding.
}

// v0.1.44 — per-session file-row factory. Files are children of the
// session item now, not top-level rows. Same display rules as before
// (full path, bold, accent colour, expanded).
static QTreeWidgetItem *fileParent(QTreeWidgetItem *session,
                                   QHash<QString, QTreeWidgetItem*> &index,
                                   const QString &path, const QString &accent) {
    if (!session) return nullptr;
    auto it = index.find(path);
    if (it != index.end()) return it.value();
    auto *root = new QTreeWidgetItem(session);
    root->setText(0, QString("  %1").arg(QDir::toNativeSeparators(path)));
    root->setToolTip(0, QDir::toNativeSeparators(path));
    root->setData(0, Qt::UserRole, path);
    root->setData(0, Qt::UserRole + 1, 1);
    QFont f = session->treeWidget() ? session->treeWidget()->font() : QFont();
    f.setBold(true);
    root->setFont(0, f);
    root->setForeground(0, QBrush(QColor(accent)));
    root->setExpanded(true);
    index.insert(path, root);
    return root;
}

void ProjectSearch::onFileNameMatch(const QString &filePath) {
    if (!m_currentSession) return;
    const auto p = psearchPalette();
    auto &fileMap = m_perSessionFiles[m_currentSession];
    auto *parent = fileParent(m_currentSession, fileMap, filePath, p.accent);
    auto *child = new QTreeWidgetItem(parent);
    child->setText(0, "      ↳ filename matches query");
    child->setData(0, Qt::UserRole, filePath);
    child->setData(0, Qt::UserRole + 1, 1);
    child->setForeground(0, QBrush(QColor(p.textSecondary)));
    ++m_filesWithMatches;
    if (m_resizeTree) m_resizeTree();
}

void ProjectSearch::onMatches(const QVector<ProjectSearchMatch> &matches) {
    if (matches.isEmpty()) return;
    if (!m_currentSession) return;
    const auto p = psearchPalette();
    auto &fileMap = m_perSessionFiles[m_currentSession];
    auto *parent = fileParent(m_currentSession, fileMap, matches.first().filePath, p.accent);
    for (const ProjectSearchMatch &m : matches) {
        QString line = m.lineContent;
        if (line.length() > 240) line = line.left(240) + "…";
        const int col1 = m.matchStart + 1;
        const QString coord = QString("%1:%2").arg(m.lineNumber, 5).arg(col1, -3);
        const QString rendered = QString("      %1  │  %2").arg(coord, line);
        auto *child = new QTreeWidgetItem(parent);
        child->setText(0, rendered);
        child->setToolTip(0, QString("%1:%2:%3\n%4")
                              .arg(QDir::toNativeSeparators(m.filePath))
                              .arg(m.lineNumber).arg(col1).arg(m.lineContent));
        child->setData(0, Qt::UserRole, m.filePath);
        child->setData(0, Qt::UserRole + 1, m.lineNumber);
        child->setData(0, Qt::UserRole + 2, col1);
        ++m_matchesSoFar;
    }
    if (m_resizeTree) m_resizeTree();
}

// Human-readable elapsed for the live status line.
//   < 1 s    → "N ms"           (fine-grained for fast scans)
//   < 60 s   → "N.NN s"         (short searches, 2 decimals for sub-10s)
//   < 60 min → "M min SS s"     (medium searches)
//   >= 60 m  → "H h MM min SS s" (huge monorepo scans)
// Format rolls up naturally as the timer ticks past each boundary.
static QString psearchFormatElapsed(qint64 ms) {
    if (ms < 1000) return QString("%1 ms").arg(ms);
    if (ms < 60'000) {
        double s = ms / 1000.0;
        return QString("%1 s").arg(s, 0, 'f', s < 10 ? 2 : 1);
    }
    const qint64 totalSec = ms / 1000;
    if (totalSec < 3600) {
        const qint64 minutes = totalSec / 60;
        const qint64 seconds = totalSec % 60;
        return QString("%1 min %2 s").arg(minutes).arg(seconds, 2, 10, QChar('0'));
    }
    const qint64 hours   = totalSec / 3600;
    const qint64 minutes = (totalSec % 3600) / 60;
    const qint64 seconds = totalSec % 60;
    return QString("%1 h %2 min %3 s")
               .arg(hours)
               .arg(minutes, 2, 10, QChar('0'))
               .arg(seconds, 2, 10, QChar('0'));
}

// Group-separated integer for big line counts — "28340" → "28,340"
static QString psearchFormatCount(qint64 n) {
    QLocale l(QLocale::C);
    l.setNumberOptions(QLocale::DefaultNumberOptions);
    return QLocale(QLocale::English).toString(n);
}

void ProjectSearch::onProgress(int done, int total, int matches,
                               qint64 elapsedMs, qint64 linesScanned) {
    if (m_phase == Phase::Idle) return;   // cancelled; ignore tail events
    // Cache latest counters for the UI-side 10 Hz refresher.
    m_phase = Phase::Scanning;
    m_lastFilesDone = done;
    m_lastFilesTotal = total;
    m_lastMatches = matches;
    m_lastLines = linesScanned;
    Q_UNUSED(elapsedMs);
    // Honest 0→100 % — exactly scanned / total, no fake walk share.
    const int pct = total > 0 ? int((double(done) / double(total)) * 100.0) : 0;
    m_progressBar->setRange(0, 100);
    m_progressBar->setValue(pct);
    refreshLiveStatus();
}

void ProjectSearch::onFinished(int totalMatches, int totalFiles,
                               qint64 elapsedMs, qint64 linesScanned) {
    m_searchBtn->setEnabled(true);
    m_cancelBtn->setEnabled(false);
    // Stop the 10 Hz live refresher — bar has hit 100 %, time to freeze
    // the elapsed display at the authoritative worker-reported value.
    m_phase = Phase::Idle;
    if (m_liveTimer && m_liveTimer->isActive()) m_liveTimer->stop();
    m_progressBar->setRange(0, 100);
    m_progressBar->setValue(100);
    const QString elapsed = psearchFormatElapsed(elapsedMs);
    const QString lines   = psearchFormatCount(linesScanned);

    // v0.1.44 — files-with-matches now counts the CURRENT session's
    // file map, not a global flat hash, since each session has its own.
    const int filesWithMatches = m_currentSession
        ? m_perSessionFiles.value(m_currentSession).size()
        : 0;

    if (totalMatches == 0) {
        m_statusLabel->setText(QString("No matches — scanned %1 files, %2 lines in %3.")
                                   .arg(totalFiles).arg(lines).arg(elapsed));
    } else {
        m_statusLabel->setText(
            QString("✓ %1 matches across %2 file(s) · scanned %3 files, %4 lines in %5.")
                .arg(totalMatches)
                .arg(filesWithMatches)
                .arg(totalFiles)
                .arg(lines)
                .arg(elapsed));
    }

    // v0.1.44 — stamp the session header with the final result so the
    // collapsed history reads at a glance. Format mirrors Notepad++:
    //   🔎  "pwd"  — 5 hits in 1 file · 12:34:07 · /path
    if (m_currentSession) {
        // Pull the original query string + flags + folder out of the
        // existing label by re-deriving from the inputs (cheaper than
        // parsing the placeholder text).
        const QString queryText  = m_queryInput->text().trimmed();
        const QString folderText = m_folderInput->text();
        const QString flagsLabel = QString("%1%2%3")
            .arg(m_caseChk->isChecked() ? "Aa "  : "")
            .arg(m_wordChk->isChecked() ? "W "   : "")
            .arg(m_regexChk->isChecked() ? ".*"  : "");
        const QString hitsText = totalMatches == 0
            ? QString("no hits")
            : QString("%1 hits in %2 file%3")
                  .arg(totalMatches)
                  .arg(filesWithMatches)
                  .arg(filesWithMatches == 1 ? "" : "s");
        m_currentSession->setText(0,
            QString("🔎  \"%1\"  %2 — %3 · %4 · %5")
                .arg(queryText, flagsLabel, hitsText,
                     QDateTime::currentDateTime().toString("HH:mm:ss"),
                     folderText));
    }
    // Search done — drop the in-flight pointer; new searches make a
    // fresh session row at the top of the tree.
    m_currentSession = nullptr;
}

// Called by the 10 Hz QTimer between worker updates AND immediately
// whenever walkProgress / filesCounted / onProgress updates our cached
// counters. Shows LIVE elapsed time — it visibly ticks up while the
// scan runs, then freezes when onFinished stops the timer.
void ProjectSearch::refreshLiveStatus() {
    if (!m_wallTimer.isValid()) return;
    const qint64 elapsedMs = m_wallTimer.elapsed();
    const QString elapsed  = psearchFormatElapsed(elapsedMs);
    const QString lines    = psearchFormatCount(m_lastLines);

    if (m_phase == Phase::Walking) {
        // No percentage during walk — we don't know the total yet.
        // The number of files discovered ticks up instead.
        m_statusLabel->setText(
            QString("Walking folder tree — %1 files discovered · %2 elapsed")
                .arg(m_lastWalkDiscovered).arg(elapsed));
    } else if (m_phase == Phase::Scanning) {
        const int pct = m_lastFilesTotal > 0
            ? int((double(m_lastFilesDone) / double(m_lastFilesTotal)) * 100.0) : 0;
        QString base = QString("Searching — %1 / %2 files (%3%) · %4 lines · %5 matches · %6 elapsed")
                .arg(m_lastFilesDone).arg(m_lastFilesTotal).arg(pct)
                .arg(lines).arg(m_lastMatches).arg(elapsed);
        // Stall detection — the live timer ticks at 10 Hz. If the
        // (done, total) pair hasn't advanced for 20 consecutive ticks
        // (~2 seconds), append the last in-flight path so the user
        // can see which file has the scan wedged. Common on Windows:
        //   · OneDrive placeholder pulls a GB over the net
        //   · network drive (Z:\) that lost its connection
        //   · Defender / AV holds an exclusive lock
        //   · 2 GB log with no newlines (QTextStream reads it whole)
        static int s_lastDone = -1;
        if (m_lastFilesDone == s_lastDone) m_stalledTicks++;
        else { m_stalledTicks = 0; s_lastDone = m_lastFilesDone; }
        if (m_stalledTicks >= 20 && !m_lastFileInFlight.isEmpty()) {
            QString shortPath = m_lastFileInFlight;
            if (shortPath.length() > 80)
                shortPath = "…" + shortPath.right(79);
            base += QString("\n⏳ stalled on: %1").arg(shortPath);
        }
        m_statusLabel->setText(base);
    }
    // Idle = onFinished already wrote the final line — leave it alone.
}
