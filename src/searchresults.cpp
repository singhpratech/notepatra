#include "searchresults.h"
#include "fonts.h"
#include <QVBoxLayout>
#include <QFont>
#include <QHeaderView>

SearchResultsPanel::SearchResultsPanel(QWidget *parent) : QWidget(parent) {
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);

    m_header = new QLabel("  Search Results");
    m_header->setFixedHeight(22);
    m_header->setStyleSheet("font-weight: bold; background: #E8E8E8; color: #333; padding: 2px 6px; border-bottom: 1px solid #CCC;");
    layout->addWidget(m_header);

    m_tree = new QTreeWidget;
    m_tree->setHeaderHidden(true);
    m_tree->setRootIsDecorated(true);
    m_tree->setAnimated(false);
    m_tree->setIndentation(16);
    m_tree->setUniformRowHeights(true);

    QFont mono = notepatraCodeFont();
    m_tree->setFont(mono);

    m_tree->setStyleSheet(
        "QTreeWidget { background: #FFFFFF; border: none; }"
        "QTreeWidget::item { padding: 2px 0; }"
        "QTreeWidget::item:selected { background: #CCE8FF; color: #000; }"
    );

    layout->addWidget(m_tree, 1);

    connect(m_tree, &QTreeWidget::itemDoubleClicked, this, [this](QTreeWidgetItem *item, int) {
        // Get line number from item data
        int line = item->data(0, Qt::UserRole).toInt();
        QString file = item->data(0, Qt::UserRole + 1).toString();
        if (line > 0) {
            emit resultDoubleClicked(file, line);
        }
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
    m_currentFileItem->setForeground(0, QColor("#0066CC"));
}

void SearchResultsPanel::addResultLine(int lineNumber, const QString &lineContent, const QString &matchText) {
    QTreeWidgetItem *parent = m_currentFileItem ? m_currentFileItem : nullptr;
    auto *item = parent ? new QTreeWidgetItem(parent) : new QTreeWidgetItem(m_tree);

    // Format: "  Line 42:   the actual line content with match highlighted"
    QString display = QString("  Line %1:\t%2").arg(lineNumber, 5).arg(lineContent.trimmed().left(200));
    item->setText(0, display);
    item->setData(0, Qt::UserRole, lineNumber);
    item->setData(0, Qt::UserRole + 1, m_currentFile);

    // Color the line number part
    item->setForeground(0, QColor("#333333"));
}
