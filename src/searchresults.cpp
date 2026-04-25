#include "searchresults.h"
#include "fonts.h"
#include "config.h"
#include <QVBoxLayout>
#include <QFont>
#include <QHeaderView>

namespace {
struct SRPalette {
    QString hdrBg, hdrFg, hdrBorder;
    QString treeBg, treeFg, selBg, selFg;
    QString fileTone, lineTone;
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
                "#4EC9B0", "#B8B5B1"};
    }
    return {"#F5F4EE", "#141413", "#E5E4DF",
            "#FFFFFF", "#141413", "#CC785C", "#FFFFFF",
            "#CC785C", "#54524E"};
}
} // namespace

SearchResultsPanel::SearchResultsPanel(QWidget *parent) : QWidget(parent) {
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);
    const SRPalette p = srPalette();

    m_header = new QLabel("  Search Results");
    m_header->setMinimumHeight(28);
    m_header->setStyleSheet(QString(
        "font-weight: bold; background: %1; color: %2; "
        "padding: 4px 6px; border-bottom: 1px solid %3;")
        .arg(p.hdrBg, p.hdrFg, p.hdrBorder));
    layout->addWidget(m_header);

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
    m_currentFileItem = nullptr;
    m_currentFile.clear();
}

void SearchResultsPanel::setHeader(const QString &searchTerm, int totalHits, int fileCount) {
    m_header->setText(QString("  Search \"%1\" (%2 hits in %3 file%4)")
                      .arg(searchTerm).arg(totalHits).arg(fileCount)
                      .arg(fileCount != 1 ? "s" : ""));
}

void SearchResultsPanel::addFileSection(const QString &filePath, int hitCount) {
    m_currentFile = filePath;
    m_currentFileItem = new QTreeWidgetItem(m_tree);
    m_currentFileItem->setText(0, QString("  %1 (%2 hits)").arg(filePath).arg(hitCount));
    m_currentFileItem->setExpanded(true);

    QFont bold = m_currentFileItem->font(0);
    bold.setBold(true);
    m_currentFileItem->setFont(0, bold);
    m_currentFileItem->setForeground(0, QColor(srPalette().fileTone));
}

void SearchResultsPanel::addResultLine(int lineNumber, const QString &lineContent, const QString &matchText) {
    Q_UNUSED(matchText);
    QTreeWidgetItem *parent = m_currentFileItem ? m_currentFileItem : nullptr;
    auto *item = parent ? new QTreeWidgetItem(parent) : new QTreeWidgetItem(m_tree);

    QString display = QString("  Line %1:\t%2").arg(lineNumber, 5).arg(lineContent.trimmed().left(200));
    item->setText(0, display);
    item->setData(0, Qt::UserRole, lineNumber);
    item->setData(0, Qt::UserRole + 1, m_currentFile);
    item->setForeground(0, QColor(srPalette().lineTone));
}
