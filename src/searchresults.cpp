#include "searchresults.h"
#include "fonts.h"
#include "config.h"
#include <QApplication>
#include <QClipboard>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFont>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMenu>
#include <QPushButton>
#include <QStandardPaths>
#include <QTimer>
#include <QVBoxLayout>

namespace {
struct SRPalette {
    QString hdrBg, hdrFg, hdrBorder;
    QString treeBg, treeFg, selBg, selFg;
    QString fileTone, lineTone, sessionTone;
};
static bool srIsDark() {
    const QString &t = Config::instance().theme;
    return t.compare("Dark", Qt::CaseInsensitive) == 0 ||
           t.compare("Monokai", Qt::CaseInsensitive) == 0;
}
static SRPalette srPalette() {
    if (srIsDark()) {
        return {"#252526", "#D4D4D4", "#1E1E1E",
                "#1E1E1E", "#D4D4D4", "#094771", "#FFFFFF",
                "#4EC9B0", "#B8B5B1", "#DCB67A"};
    }
    return {"#F5F4EE", "#141413", "#E5E4DF",
            "#FFFFFF", "#141413", "#CC785C", "#FFFFFF",
            "#CC785C", "#54524E", "#A65D43"};
}
} // namespace

SearchResultsPanel::SearchResultsPanel(QWidget *parent) : QWidget(parent) {
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);
    const SRPalette p = srPalette();

    // ── v0.1.45 — header row (label stretched + red ✕ close) ─────────
    auto *headerHost = new QWidget;
    headerHost->setMinimumHeight(28);
    headerHost->setStyleSheet(QString(
        "background: %1; border-bottom: 1px solid %2;")
        .arg(p.hdrBg, p.hdrBorder));
    auto *headerRow = new QHBoxLayout(headerHost);
    headerRow->setContentsMargins(6, 0, 0, 0);
    headerRow->setSpacing(0);

    m_header = new QLabel("  Search Results");
    m_header->setStyleSheet(QString(
        "font-weight: bold; background: transparent; color: %1; padding: 4px 6px;")
        .arg(p.hdrFg));
    headerRow->addWidget(m_header, /*stretch*/ 1);

    auto *closeBtn = new QPushButton("×");
    QFont closeFont = closeBtn->font();
    closeFont.setPointSize(16);
    closeFont.setBold(true);
    closeBtn->setFont(closeFont);
    closeBtn->setFixedSize(34, 28);
    closeBtn->setCursor(Qt::PointingHandCursor);
    closeBtn->setFlat(true);
    closeBtn->setToolTip("Close Search Results");
    closeBtn->setStyleSheet(
        "QPushButton { background: transparent; border: none; "
        "color: #E81123; font-weight: 700; padding: 0; } "
        "QPushButton:hover { background: #E81123; color: white; } "
        "QPushButton:pressed { background: #C41019; color: white; }");
    // v0.1.46 — Reset/Clear button next to the close ✕. Wipes every
    // stacked session AND deletes the on-disk history file so the
    // user gets a clean slate (otherwise loadPersistedHistory would
    // bring everything back on the next launch).
    auto *clearBtn = new QPushButton("Clear");
    QFont clearFont = clearBtn->font();
    clearFont.setPointSize(10);
    clearBtn->setFont(clearFont);
    clearBtn->setFixedHeight(24);
    clearBtn->setCursor(Qt::PointingHandCursor);
    clearBtn->setFlat(true);
    clearBtn->setToolTip("Clear all stacked search sessions (and on-disk history)");
    clearBtn->setStyleSheet(QString(
        "QPushButton { background: transparent; border: 1px solid %1; "
        "color: %1; border-radius: 4px; padding: 2px 10px; margin-right: 6px; } "
        "QPushButton:hover { border-color: #E81123; color: #E81123; }")
        .arg(p.hdrFg));
    connect(clearBtn, &QPushButton::clicked, this, [this]() {
        clear();
    });
    clearBtn->setVisible(false);
    m_clearBtn = clearBtn;
    headerRow->addWidget(clearBtn);

    connect(closeBtn, &QPushButton::clicked, this, [this]() {
        emit closeRequested();
    });
    headerRow->addWidget(closeBtn);
    // v0.1.46 — only show the ✕ when the panel actually has search
    // sessions. An empty panel with a stray ✕ floating at the right
    // edge looked like leftover UI noise.
    closeBtn->setVisible(false);
    m_closeBtn = closeBtn;

    layout->addWidget(headerHost);

    m_tree = new QTreeWidget;
    m_tree->setHeaderHidden(true);
    m_tree->setRootIsDecorated(true);
    m_tree->setAnimated(false);
    m_tree->setIndentation(16);
    m_tree->setUniformRowHeights(true);

    QFont mono = notepatraCodeFont();
    m_tree->setFont(mono);

    m_tree->setStyleSheet(QString(
        "QTreeWidget { background: %1; color: %2; border: none; }"
        "QTreeWidget::item { padding: 2px 0; }"
        "QTreeWidget::item:selected { background: %3; color: %4; }")
        .arg(p.treeBg, p.treeFg, p.selBg, p.selFg));

    layout->addWidget(m_tree, 1);

    connect(m_tree, &QTreeWidget::itemDoubleClicked, this, [this](QTreeWidgetItem *item, int) {
        int line = item->data(0, Qt::UserRole).toInt();
        QString file = item->data(0, Qt::UserRole + 1).toString();
        if (line > 0) emit resultDoubleClicked(file, line);
    });

    // v0.1.46 — Notepad++-style right-click context menu on the
    // results tree. Available actions depend on what the user
    // clicked (a session, a file row, or a match line); shared
    // actions (Copy, Select All, Clear all sessions) always show.
    m_tree->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(m_tree, &QTreeWidget::customContextMenuRequested, this,
            [this](const QPoint &pos) {
        QTreeWidgetItem *item = m_tree->itemAt(pos);
        QMenu menu;
        if (item) {
            const int line = item->data(0, Qt::UserRole).toInt();
            const QString file = item->data(0, Qt::UserRole + 1).toString();
            const bool isMatch = (line > 0 && !file.isEmpty());

            if (isMatch) {
                menu.addAction(tr("&Open"), this, [this, file, line]() {
                    emit resultDoubleClicked(file, line);
                });
            }
            menu.addAction(tr("&Copy"), this, [item]() {
                QApplication::clipboard()->setText(item->text(0));
            });
            menu.addAction(tr("Copy &Path"), this, [file]() {
                if (!file.isEmpty())
                    QApplication::clipboard()->setText(file);
            })->setEnabled(!file.isEmpty());
            menu.addSeparator();
        }
        menu.addAction(tr("&Expand all"), this, [this]() {
            m_tree->expandAll();
        });
        menu.addAction(tr("Co&llapse all"), this, [this]() {
            m_tree->collapseAll();
            for (QTreeWidgetItem *s : m_sessions)
                if (s) s->setExpanded(false);
        });
        menu.addSeparator();
        menu.addAction(tr("Select &All"), this, [this]() {
            m_tree->selectAll();
        });
        menu.addSeparator();
        menu.addAction(tr("Clear &all sessions"), this, [this]() { clear(); });
        menu.exec(m_tree->viewport()->mapToGlobal(pos));
    });

    // v0.1.46 — persistent history. Debounced 1s save timer, fired
    // whenever a search adds/updates a session; load any existing
    // file from disk on construction so users see prior searches
    // when the app restarts.
    QString cfgDir = QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation);
    if (cfgDir.isEmpty()) {
        cfgDir = QStandardPaths::writableLocation(QStandardPaths::ConfigLocation);
    }
    if (!cfgDir.isEmpty()) {
        QDir().mkpath(cfgDir);
        m_historyPath = cfgDir + "/search-history.json";
    }
    m_saveTimer = new QTimer(this);
    m_saveTimer->setSingleShot(true);
    m_saveTimer->setInterval(1000);
    connect(m_saveTimer, &QTimer::timeout, this, &SearchResultsPanel::persistHistory);
    loadPersistedHistory();
}

void SearchResultsPanel::scheduleSave() {
    if (m_saveTimer) m_saveTimer->start();
}

void SearchResultsPanel::persistHistory() {
    if (m_historyPath.isEmpty()) return;
    QJsonArray sessionsArr;
    for (QTreeWidgetItem *session : m_sessions) {
        if (!session) continue;
        QJsonObject sessionObj;
        sessionObj["label"] = session->text(0);
        QJsonArray filesArr;
        for (int i = 0; i < session->childCount(); i++) {
            QTreeWidgetItem *fileItem = session->child(i);
            if (!fileItem) continue;
            QJsonObject fileObj;
            fileObj["label"] = fileItem->text(0);
            QJsonArray matchesArr;
            for (int j = 0; j < fileItem->childCount(); j++) {
                QTreeWidgetItem *matchItem = fileItem->child(j);
                if (!matchItem) continue;
                QJsonObject m;
                m["label"] = matchItem->text(0);
                m["line"] = matchItem->data(0, Qt::UserRole).toInt();
                m["file"] = matchItem->data(0, Qt::UserRole + 1).toString();
                matchesArr.append(m);
            }
            fileObj["matches"] = matchesArr;
            filesArr.append(fileObj);
        }
        sessionObj["files"] = filesArr;
        sessionsArr.append(sessionObj);
    }
    QJsonObject root;
    root["version"] = 1;
    root["sessions"] = sessionsArr;
    QJsonDocument doc(root);
    QByteArray bytes = doc.toJson(QJsonDocument::Compact);
    // Cap on-disk size at 5 MB; if exceeded, drop the oldest sessions
    // until we fit. (m_sessions is in chronological order; oldest
    // first.) This avoids unbounded growth from massive search runs.
    constexpr int kMaxBytes = 5 * 1024 * 1024;
    while (bytes.size() > kMaxBytes && !sessionsArr.isEmpty()) {
        sessionsArr.removeFirst();
        root["sessions"] = sessionsArr;
        bytes = QJsonDocument(root).toJson(QJsonDocument::Compact);
    }
    QFile f(m_historyPath);
    if (f.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        f.write(bytes);
        f.close();
    }
}

void SearchResultsPanel::loadPersistedHistory() {
    if (m_historyPath.isEmpty()) return;
    QFile f(m_historyPath);
    if (!f.exists() || !f.open(QIODevice::ReadOnly)) return;
    const QByteArray bytes = f.readAll();
    f.close();
    if (bytes.isEmpty()) return;
    QJsonParseError err;
    QJsonDocument doc = QJsonDocument::fromJson(bytes, &err);
    if (err.error != QJsonParseError::NoError || !doc.isObject()) return;
    const QJsonArray sessionsArr = doc.object().value("sessions").toArray();
    if (sessionsArr.isEmpty()) return;

    const SRPalette p = srPalette();
    // Sessions are stored in chronological order (oldest first); we
    // insert each one BELOW the previously inserted one to preserve
    // the on-disk order in the tree (oldest at the bottom, newest
    // at the top — same convention beginSession uses live).
    for (int i = sessionsArr.size() - 1; i >= 0; i--) {
        const QJsonObject sObj = sessionsArr[i].toObject();
        auto *session = new QTreeWidgetItem;
        m_tree->addTopLevelItem(session);
        session->setText(0, sObj.value("label").toString());
        QFont sf = m_tree->font();
        sf.setBold(true);
        session->setFont(0, sf);
        session->setForeground(0, QColor(p.sessionTone));
        session->setExpanded(false);

        const QJsonArray filesArr = sObj.value("files").toArray();
        for (const QJsonValue &fv : filesArr) {
            const QJsonObject fObj = fv.toObject();
            auto *fileItem = new QTreeWidgetItem(session);
            fileItem->setText(0, fObj.value("label").toString());
            QFont bold = fileItem->font(0);
            bold.setBold(true);
            fileItem->setFont(0, bold);
            fileItem->setForeground(0, QColor(p.fileTone));
            fileItem->setExpanded(true);

            const QJsonArray matchesArr = fObj.value("matches").toArray();
            for (const QJsonValue &mv : matchesArr) {
                const QJsonObject mObj = mv.toObject();
                auto *matchItem = new QTreeWidgetItem(fileItem);
                matchItem->setText(0, mObj.value("label").toString());
                matchItem->setData(0, Qt::UserRole, mObj.value("line").toInt());
                matchItem->setData(0, Qt::UserRole + 1, mObj.value("file").toString());
                matchItem->setForeground(0, QColor(p.lineTone));
            }
        }

        m_sessions.prepend(session);
    }

    // Make the close + clear buttons visible since we have content.
    if (m_closeBtn) m_closeBtn->setVisible(true);
    if (m_clearBtn) m_clearBtn->setVisible(true);

    // Show the panel since there's history to look at — user can
    // dismiss it via ✕ if they don't want it.
    setVisible(true);
}

void SearchResultsPanel::clear() {
    m_tree->clear();
    m_currentSession = nullptr;
    m_sessions.clear();
    m_currentFileItem = nullptr;
    m_currentFile.clear();
    if (m_closeBtn) m_closeBtn->setVisible(false);
    if (m_clearBtn) m_clearBtn->setVisible(false);
    // v0.1.46 — Reset wipes the on-disk file too so the next launch
    // doesn't restore the just-cleared sessions.
    if (!m_historyPath.isEmpty()) QFile::remove(m_historyPath);
    if (m_saveTimer) m_saveTimer->stop();
}

void SearchResultsPanel::beginSession(const QString &searchTerm) {
    const SRPalette p = srPalette();

    // Collapse prior sessions so the new one stands out without
    // hiding the history below it.
    for (QTreeWidgetItem *prior : m_sessions) {
        if (prior) prior->setExpanded(false);
    }

    m_currentSession = new QTreeWidgetItem;
    m_tree->insertTopLevelItem(0, m_currentSession);
    const QString stamp = QDateTime::currentDateTime().toString("HH:mm:ss");
    m_currentSession->setText(0, QString("🔎  Search \"%1\" — searching… · %2")
                                     .arg(searchTerm, stamp));
    QFont sf = m_tree->font();
    sf.setBold(true);
    m_currentSession->setFont(0, sf);
    m_currentSession->setForeground(0, QColor(p.sessionTone));
    m_currentSession->setExpanded(true);

    m_sessions.append(m_currentSession);
    if (m_closeBtn) m_closeBtn->setVisible(true);
    if (m_clearBtn) m_clearBtn->setVisible(true);

    // Cap at 10 sessions; oldest pruned from the bottom of the tree.
    constexpr int kMaxSessions = 10;
    while (m_sessions.size() > kMaxSessions) {
        QTreeWidgetItem *oldest = m_sessions.takeFirst();
        delete m_tree->takeTopLevelItem(m_tree->indexOfTopLevelItem(oldest));
    }

    // Reset per-session file pointer so the next addFileSection
    // creates a fresh child under this session.
    m_currentFileItem = nullptr;
    m_currentFile.clear();
    scheduleSave();
}

void SearchResultsPanel::setHeader(const QString &searchTerm, int totalHits, int fileCount) {
    // Back-compat: callers that called setHeader without beginSession
    // first get a fresh session opened automatically.
    if (!m_currentSession) {
        beginSession(searchTerm);
    }
    const QString stamp = QDateTime::currentDateTime().toString("HH:mm:ss");
    const QString hits = totalHits == 0
        ? QString("no hits")
        : QString("%1 hit%2 in %3 file%4")
              .arg(totalHits).arg(totalHits == 1 ? "" : "s")
              .arg(fileCount).arg(fileCount == 1 ? "" : "s");
    m_currentSession->setText(0, QString("🔎  Search \"%1\" — %2 · %3")
                                     .arg(searchTerm, hits, stamp));
    scheduleSave();
}

void SearchResultsPanel::addFileSection(const QString &filePath, int hitCount) {
    // Lazy session — Find-All-in-current-doc skips beginSession and
    // calls setHeader after the file/result loop, so without a session
    // open here there's nowhere for the file row to land.
    if (!m_currentSession) {
        beginSession(QString());
    }
    m_currentFile = filePath;
    m_currentFileItem = new QTreeWidgetItem(m_currentSession);
    m_currentFileItem->setText(0, QString("  %1 (%2 hit%3)")
                                  .arg(filePath).arg(hitCount)
                                  .arg(hitCount == 1 ? "" : "s"));
    m_currentFileItem->setExpanded(true);

    QFont bold = m_currentFileItem->font(0);
    bold.setBold(true);
    m_currentFileItem->setFont(0, bold);
    m_currentFileItem->setForeground(0, QColor(srPalette().fileTone));
}

void SearchResultsPanel::addResultLine(int lineNumber, const QString &lineContent, const QString &matchText) {
    Q_UNUSED(matchText);
    QTreeWidgetItem *parent = m_currentFileItem ? m_currentFileItem
                            : (m_currentSession ? m_currentSession : nullptr);
    auto *item = parent ? new QTreeWidgetItem(parent) : new QTreeWidgetItem(m_tree);

    QString display = QString("  Line %1:\t%2").arg(lineNumber, 5).arg(lineContent.trimmed().left(200));
    item->setText(0, display);
    item->setData(0, Qt::UserRole, lineNumber);
    item->setData(0, Qt::UserRole + 1, m_currentFile);
    item->setForeground(0, QColor(srPalette().lineTone));
}
