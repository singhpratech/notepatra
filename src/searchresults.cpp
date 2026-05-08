#include "searchresults.h"
#include "fonts.h"
#include "config.h"
#include <QDateTime>
#include <QFont>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QPushButton>
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
    connect(closeBtn, &QPushButton::clicked, this, [this]() {
        emit closeRequested();
    });
    headerRow->addWidget(closeBtn);

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
}

void SearchResultsPanel::clear() {
    m_tree->clear();
    m_currentSession = nullptr;
    m_sessions.clear();
    m_currentFileItem = nullptr;
    m_currentFile.clear();
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
