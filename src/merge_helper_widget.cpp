// MergeHelperWidget — see merge_helper_widget.h for the design.

#include "merge_helper_widget.h"

#include <QFileInfo>
#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QScrollArea>
#include <QVBoxLayout>

#include <Qsci/qsciscintilla.h>
#include <Qsci/qsciscintillabase.h>

namespace {

QString rowStyle() {
    return QStringLiteral(
        "QFrame#mergeRow {"
        "  background: rgba(255, 215, 0, 0.10);"
        "  border: 1px solid rgba(255, 165, 0, 0.55);"
        "  border-radius: 4px;"
        "  padding: 4px 8px;"
        "}");
}

QString actionBtnStyle(const QString &accent) {
    return QString(
        "QPushButton {"
        "  padding: 3px 10px;"
        "  border-radius: 4px;"
        "  background: %1;"
        "  color: white;"
        "  font-weight: 600;"
        "}"
        "QPushButton:hover { background: %2; }")
        .arg(accent, QColor(accent).darker(115).name());
}

} // namespace

MergeHelperWidget::MergeHelperWidget(QWidget *parent) : QWidget(parent) {
    setObjectName("mergeHelperWidget");

    auto *outer = new QVBoxLayout(this);
    outer->setContentsMargins(8, 8, 8, 8);
    outer->setSpacing(6);

    m_header = new QLabel(this);
    m_header->setStyleSheet(
        "font-weight: 700; font-size: 13px; color: #B8860B; padding: 2px 4px;");
    outer->addWidget(m_header);

    auto *scroll = new QScrollArea(this);
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);

    m_rowsHost = new QWidget(scroll);
    m_rowsLayout = new QVBoxLayout(m_rowsHost);
    m_rowsLayout->setContentsMargins(0, 0, 0, 0);
    m_rowsLayout->setSpacing(4);
    m_rowsLayout->addStretch();
    scroll->setWidget(m_rowsHost);

    outer->addWidget(scroll, 1);

    setStyleSheet(
        "QWidget#mergeHelperWidget { background: rgba(0,0,0,0.02); "
        "border-top: 1px solid rgba(0,0,0,0.10); }");
}

void MergeHelperWidget::attach(QsciScintilla *editor, const QString &filePath) {
    m_editor = editor;
    m_filePath = filePath;
    rescan();
}

void MergeHelperWidget::clearRowsUi() {
    if (!m_rowsLayout) return;
    while (m_rowsLayout->count() > 0) {
        QLayoutItem *item = m_rowsLayout->takeAt(0);
        if (!item) continue;
        if (QWidget *w = item->widget()) w->deleteLater();
        delete item;
    }
    m_rowsLayout->addStretch();
}

void MergeHelperWidget::buildRowsUi() {
    clearRowsUi();
    if (!m_rowsLayout) return;

    for (int i = 0; i < m_regions.size(); ++i) {
        const MergeHelper::ConflictRegion &r = m_regions[i];

        auto *row = new QFrame(m_rowsHost);
        row->setObjectName("mergeRow");
        row->setStyleSheet(rowStyle());

        auto *rowLayout = new QHBoxLayout(row);
        rowLayout->setContentsMargins(6, 4, 6, 4);
        rowLayout->setSpacing(6);

        const QString oursLbl = r.oursLabel.isEmpty() ? QStringLiteral("ours")
                                                      : r.oursLabel;
        const QString theirsLbl = r.theirsLabel.isEmpty() ? QStringLiteral("theirs")
                                                          : r.theirsLabel;
        auto *label = new QLabel(QString("Conflict %1/%2 — %3 ↔ %4 (line %5)")
                                     .arg(i + 1)
                                     .arg(m_regions.size())
                                     .arg(oursLbl, theirsLbl)
                                     .arg(r.startLine + 1));
        label->setStyleSheet("font-weight: 600;");
        rowLayout->addWidget(label, 1);

        auto *takeOurs = new QPushButton("Take ours");
        takeOurs->setCursor(Qt::PointingHandCursor);
        takeOurs->setStyleSheet(actionBtnStyle("#2E7D32"));
        connect(takeOurs, &QPushButton::clicked, this,
                [this, i]() {
                    if (i < m_regions.size())
                        resolveRegion(i, m_regions[i].ours);
                });
        rowLayout->addWidget(takeOurs);

        auto *takeTheirs = new QPushButton("Take theirs");
        takeTheirs->setCursor(Qt::PointingHandCursor);
        takeTheirs->setStyleSheet(actionBtnStyle("#1565C0"));
        connect(takeTheirs, &QPushButton::clicked, this,
                [this, i]() {
                    if (i < m_regions.size())
                        resolveRegion(i, m_regions[i].theirs);
                });
        rowLayout->addWidget(takeTheirs);

        auto *takeBoth = new QPushButton("Take both");
        takeBoth->setCursor(Qt::PointingHandCursor);
        takeBoth->setStyleSheet(actionBtnStyle("#6A1B9A"));
        connect(takeBoth, &QPushButton::clicked, this,
                [this, i]() {
                    if (i < m_regions.size()) {
                        const auto &reg = m_regions[i];
                        QString combined = reg.ours;
                        if (!combined.isEmpty() && !combined.endsWith('\n'))
                            combined += '\n';
                        combined += reg.theirs;
                        resolveRegion(i, combined);
                    }
                });
        rowLayout->addWidget(takeBoth);

        auto *jumpBtn = new QPushButton("Jump →");
        jumpBtn->setCursor(Qt::PointingHandCursor);
        jumpBtn->setStyleSheet(actionBtnStyle("#546E7A"));
        connect(jumpBtn, &QPushButton::clicked, this,
                [this, i]() { jumpToRegion(i); });
        rowLayout->addWidget(jumpBtn);

        // Insert before the trailing stretch.
        m_rowsLayout->insertWidget(m_rowsLayout->count() - 1, row);
    }
}

void MergeHelperWidget::applyAnnotations() {
    if (!m_editor) return;

    // Reset any existing annotations first so we don't accumulate stale
    // labels between rescans.
    clearAnnotations();

    // Style 200 = our annotation style. ANNOTATION_BOXED gives the
    // bordered "callout" look that reads as a meta UI affordance, not
    // editable content.
    m_editor->SendScintilla(QsciScintillaBase::SCI_ANNOTATIONSETVISIBLE,
                            (long)QsciScintillaBase::ANNOTATION_BOXED);
    m_editor->SendScintilla(QsciScintillaBase::SCI_STYLESETBACK, 200,
                            (long)0x00C8E6FA);  // pale gold (BGR)
    m_editor->SendScintilla(QsciScintillaBase::SCI_STYLESETFORE, 200,
                            (long)0x00204050);  // dark blue text

    for (int i = 0; i < m_regions.size(); ++i) {
        const auto &r = m_regions[i];
        const QString label =
            QString("▼ Conflict %1/%2  —  ours: %3   theirs: %4")
                .arg(i + 1)
                .arg(m_regions.size())
                .arg(r.oursLabel.isEmpty() ? QStringLiteral("HEAD") : r.oursLabel,
                     r.theirsLabel.isEmpty() ? QStringLiteral("incoming") : r.theirsLabel);

        const QByteArray bytes = label.toUtf8();
        m_editor->SendScintilla(QsciScintillaBase::SCI_ANNOTATIONSETTEXT,
                                r.startLine, bytes.constData());
        m_editor->SendScintilla(QsciScintillaBase::SCI_ANNOTATIONSETSTYLE,
                                r.startLine, 200);
    }
}

void MergeHelperWidget::clearAnnotations() {
    if (!m_editor) return;
    m_editor->SendScintilla(QsciScintillaBase::SCI_ANNOTATIONCLEARALL);
}

void MergeHelperWidget::rescan() {
    if (!m_editor) {
        m_regions.clear();
        clearRowsUi();
        if (m_header) m_header->setText("Merge helper — no editor attached.");
        return;
    }

    const QString buf = m_editor->text();
    m_regions = MergeHelper::scanConflicts(buf);

    const QString file =
        m_filePath.isEmpty() ? QStringLiteral("(unsaved buffer)")
                             : QFileInfo(m_filePath).fileName();

    if (m_regions.isEmpty()) {
        if (m_header)
            m_header->setText(QString("✓ %1 — no remaining conflicts").arg(file));
        clearRowsUi();
        clearAnnotations();
        emit allConflictsResolved();
        return;
    }

    if (m_header)
        m_header->setText(QString("Resolve conflicts in %1 — %2 remaining")
                              .arg(file)
                              .arg(m_regions.size()));

    buildRowsUi();
    applyAnnotations();
}

void MergeHelperWidget::resolveRegion(int regionIndex, const QString &replacement) {
    if (!m_editor) return;
    if (regionIndex < 0 || regionIndex >= m_regions.size()) return;

    const QString buf = m_editor->text();
    const QString next = MergeHelper::applyResolution(buf, m_regions, regionIndex,
                                                     replacement);
    if (next == buf) return;

    // Preserve scroll position so the editor doesn't jump back to the
    // top after each resolution.
    const int firstVisible = m_editor->firstVisibleLine();
    m_editor->setText(next);
    m_editor->setFirstVisibleLine(firstVisible);

    rescan();
}

void MergeHelperWidget::jumpToRegion(int regionIndex) {
    if (!m_editor) return;
    if (regionIndex < 0 || regionIndex >= m_regions.size()) return;
    const int line = m_regions[regionIndex].startLine;
    m_editor->setCursorPosition(line, 0);
    m_editor->setFirstVisibleLine(qMax(0, line - 2));
    m_editor->setFocus(Qt::OtherFocusReason);
}
