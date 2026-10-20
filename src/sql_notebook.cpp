// SPDX-License-Identifier: GPL-3.0-or-later

#include "sql_notebook.h"

#include "fonts.h"

#include <QCheckBox>
#include <QComboBox>
#include <QFrame>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QIcon>
#include <QPainter>
#include <QPixmap>
#include <QLabel>
#include <QScrollArea>
#include <QScrollBar>
#include <QSet>
#include <QStyle>
#include <QTableWidget>
#include <QTemporaryDir>
#include <QTimer>
#include <QToolButton>
#include <QVBoxLayout>
#include <QtConcurrent>

namespace {

constexpr int kScratchSqlite = -1;
constexpr int kScratchDuckDb = -2;
constexpr int kMaxCodeLines = 14;

QString codePreview(const QString &text) {
    const QStringList lines = text.split(QLatin1Char('\n'));
    if (lines.size() <= kMaxCodeLines) return text;
    return lines.mid(0, kMaxCodeLines).join(QLatin1Char('\n')) +
           QStringLiteral("\n... (%1 more lines)").arg(lines.size() - kMaxCodeLines);
}

// Standard style icon recoloured to `color` so it stays visible on dark themes.
QIcon tintedStandardIcon(QStyle *style, QStyle::StandardPixmap which, const QColor &color) {
    QPixmap pm = style->standardIcon(which).pixmap(16, 16);
    if (pm.isNull() || !color.isValid()) return style->standardIcon(which);
    QPainter p(&pm);
    p.setCompositionMode(QPainter::CompositionMode_SourceIn);
    p.fillRect(pm.rect(), color);
    p.end();
    return QIcon(pm);
}

} // namespace

SqlNotebookWidget::SqlNotebookWidget(QWidget *parent) : QWidget(parent) {
    setObjectName(QStringLiteral("sqlNotebook"));
    auto *root = new QVBoxLayout(this);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(0);

    auto *bar = new QWidget;
    bar->setObjectName(QStringLiteral("sqlNotebookBar"));
    auto *barLayout = new QHBoxLayout(bar);
    barLayout->setContentsMargins(6, 4, 6, 4);
    barLayout->setSpacing(6);
    m_conn = new QComboBox;
    m_conn->setToolTip(tr("Connection the cells run against"));
    m_conn->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
    m_conn->setMinimumContentsLength(14);
    m_allowWrites = new QCheckBox(tr("Allow writes"));
    m_allowWrites->setToolTip(tr("Let cells run statements that change data. Always on for the "
                                 "temporary scratch databases."));
    m_runAllBtn = new QToolButton;
    m_runAllBtn->setIcon(style()->standardIcon(QStyle::SP_MediaPlay));
    m_runAllBtn->setText(tr("Run All"));
    m_runAllBtn->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    m_runAllBtn->setAutoRaise(true);
    m_stopBtn = new QToolButton;
    m_stopBtn->setIcon(style()->standardIcon(QStyle::SP_MediaStop));
    m_stopBtn->setText(tr("Stop"));
    m_stopBtn->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    m_stopBtn->setAutoRaise(true);
    m_stopBtn->setEnabled(false);
    barLayout->addWidget(m_conn, 1);
    barLayout->addWidget(m_allowWrites);
    barLayout->addWidget(m_runAllBtn);
    barLayout->addWidget(m_stopBtn);
    root->addWidget(bar);

    auto *scroll = new QScrollArea;
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    m_cardHost = new QWidget;
    m_cardHost->setObjectName(QStringLiteral("sqlNotebookCells"));
    m_cardHost->setAttribute(Qt::WA_StyledBackground, true);
    m_cardLayout = new QVBoxLayout(m_cardHost);
    m_cardLayout->setContentsMargins(8, 8, 8, 8);
    m_cardLayout->setSpacing(8);
    m_emptyLabel = new QLabel(tr("No SQL cells yet. Split the file into cells with lines "
                                 "containing -- %%"));
    m_emptyLabel->setObjectName(QStringLiteral("sqlCellStatus"));
    m_emptyLabel->setWordWrap(true);
    m_cardLayout->addWidget(m_emptyLabel);
    m_cardLayout->addStretch(1);
    scroll->setWidget(m_cardHost);
    root->addWidget(scroll, 1);

    m_watcher = new QFutureWatcher<DbConnections::QueryResult>(this);
    connect(m_watcher, &QFutureWatcher<DbConnections::QueryResult>::finished, this,
            &SqlNotebookWidget::onQueryFinished);
    m_timeout = new QTimer(this);
    m_timeout->setSingleShot(true);
    m_timeout->setInterval(kTimeoutMs);
    connect(m_timeout, &QTimer::timeout, this, [this]() {
        if (!m_busy || !m_token) return;
        m_token->requestInterrupt();
        m_results[m_runningKey].status = tr("Timed out after %1 s, stopping...").arg(kTimeoutMs / 1000);
        updateAllCards();
    });

    connect(m_conn, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
            [this](int) { onConnectionChanged(); });
    connect(m_runAllBtn, &QToolButton::clicked, this, &SqlNotebookWidget::runAll);
    connect(m_stopBtn, &QToolButton::clicked, this, &SqlNotebookWidget::stop);
}

SqlNotebookWidget::~SqlNotebookWidget() {
    // The worker keeps its own token/temp-dir references; just ask it to stop.
    if (m_busy && m_token) m_token->requestInterrupt();
}

void SqlNotebookWidget::refreshConnections() {
    const QVariant prevData = m_conn->currentData();
    const QString prevName = m_conn->currentText();
    m_saved = DbConnections::loadAll();

    const QSignalBlocker block(m_conn);
    m_conn->clear();
    m_conn->addItem(tr("Scratch (SQLite, temporary)"), kScratchSqlite);
#ifdef NOTEPATRA_HAVE_DUCKDB
    m_conn->addItem(tr("Scratch (DuckDB, temporary)"), kScratchDuckDb);
#endif
    for (int i = 0; i < m_saved.size(); ++i)
        m_conn->addItem(m_saved.at(i).name, i);

    int sel = 0;
    if (prevData.isValid()) {
        const int code = prevData.toInt();
        if (code < 0) {
            const int idx = m_conn->findData(code);
            if (idx >= 0) sel = idx;
        } else {
            const int idx = m_conn->findText(prevName);
            if (idx >= 0) sel = idx;
        }
    }
    const bool selectionKept = prevData.isValid() && m_conn->itemText(sel) == prevName;
    m_conn->setCurrentIndex(sel);
    if (!selectionKept) onConnectionChanged();
}

void SqlNotebookWidget::onConnectionChanged() {
    const bool scratch = m_conn->currentData().toInt() < 0;
    m_allowWrites->setChecked(scratch);
    m_allowWrites->setEnabled(!scratch);
}

bool SqlNotebookWidget::currentRecord(DbConnections::Record *out, QString *err) {
    if (m_conn->count() == 0) refreshConnections();
    const int code = m_conn->currentData().toInt();
    if (code >= 0) {
        if (code >= m_saved.size()) {
            *err = tr("The selected connection no longer exists.");
            return false;
        }
        *out = m_saved.at(code);
        return true;
    }
    if (!m_scratchDir) m_scratchDir = std::make_shared<QTemporaryDir>();
    if (!m_scratchDir->isValid()) {
        *err = tr("Could not create a temporary folder for the scratch database.");
        return false;
    }
    DbConnections::Record r;
    if (code == kScratchDuckDb) {
        r.name = QStringLiteral("Scratch (DuckDB)");
        r.driver = QStringLiteral("DUCKDB");
        r.database = m_scratchDir->filePath(QStringLiteral("notebook.duckdb"));
    } else {
        r.name = QStringLiteral("Scratch (SQLite)");
        r.driver = QStringLiteral("QSQLITE");
        // A file, not :memory:, because runQuery opens a fresh connection per call.
        r.database = m_scratchDir->filePath(QStringLiteral("notebook.sqlite"));
    }
    *out = r;
    return true;
}

void SqlNotebookWidget::setCells(const QVector<NotebookRender::SqlCell> &cells) {
    if (m_conn->count() == 0) refreshConnections();
    bool sameShape = cells.size() == m_cells.size();
    for (int i = 0; sameShape && i < cells.size(); ++i)
        sameShape = cells.at(i).text == m_cells.at(i).text &&
                    cells.at(i).markdown == m_cells.at(i).markdown;
    m_cells = cells;
    if (sameShape) return;

    QSet<QString> live;
    for (const auto &c : m_cells) live.insert(keyFor(c));
    if (m_busy) live.insert(m_runningKey);
    for (auto it = m_results.begin(); it != m_results.end();) {
        if (!live.contains(it.key())) it = m_results.erase(it);
        else ++it;
    }
    rebuildCards();
}

void SqlNotebookWidget::rebuildCards() {
    for (const Card &card : qAsConst(m_cards)) {
        m_cardLayout->removeWidget(card.frame);
        card.frame->hide();
        card.frame->deleteLater();
    }
    m_cards.clear();
    m_emptyLabel->setVisible(m_cells.isEmpty());

    const QFont mono = notepatraCodeFont();
    const QIcon playIcon = m_colors.fg.isEmpty()
        ? style()->standardIcon(QStyle::SP_MediaPlay)
        : tintedStandardIcon(style(), QStyle::SP_MediaPlay, QColor(m_colors.fg));
    for (int i = 0; i < m_cells.size(); ++i) {
        const NotebookRender::SqlCell &cell = m_cells.at(i);
        Card card;
        card.frame = new QFrame;
        card.frame->setObjectName(QStringLiteral("sqlNotebookCell"));
        auto *v = new QVBoxLayout(card.frame);
        v->setContentsMargins(8, 6, 8, 8);
        v->setSpacing(4);

        auto *head = new QHBoxLayout;
        auto *title = new QLabel(cell.markdown ? tr("[%1] Notes").arg(i + 1)
                                               : tr("[%1] SQL").arg(i + 1));
        title->setObjectName(QStringLiteral("sqlCellTitle"));
        head->addWidget(title);
        head->addStretch(1);
        if (!cell.markdown) {
            card.run = new QToolButton;
            card.run->setIcon(playIcon);
            card.run->setText(tr("Run"));
            card.run->setToolTip(tr("Run this cell (Ctrl+Return in the editor)"));
            card.run->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
            card.run->setAutoRaise(true);
            const QString key = keyFor(cell);
            connect(card.run, &QToolButton::clicked, this, [this, key]() {
                for (int k = 0; k < m_cells.size(); ++k)
                    if (keyFor(m_cells.at(k)) == key) { runCell(k); return; }
            });
            head->addWidget(card.run);
        }
        v->addLayout(head);

        auto *body = new QLabel;
        body->setWordWrap(true);
        body->setTextInteractionFlags(Qt::TextSelectableByMouse);
        if (cell.markdown) {
            body->setObjectName(QStringLiteral("sqlCellNotes"));
            body->setTextFormat(Qt::RichText);
            body->setText(NotebookRender::htmlBodyContent(NotebookRender::markdownToHtml(cell.text, &m_colors)));
        } else {
            body->setObjectName(QStringLiteral("sqlCellCode"));
            body->setTextFormat(Qt::PlainText);
            body->setFont(mono);
            body->setText(codePreview(cell.text));
        }
        v->addWidget(body);

        if (!cell.markdown) {
            card.status = new QLabel;
            card.status->setObjectName(QStringLiteral("sqlCellStatus"));
            card.status->setWordWrap(true);
            card.status->setTextInteractionFlags(Qt::TextSelectableByMouse);
            v->addWidget(card.status);
            card.table = new QTableWidget;
            card.table->setEditTriggers(QAbstractItemView::NoEditTriggers);
            card.table->verticalHeader()->setDefaultSectionSize(22);
            card.table->hide();
            v->addWidget(card.table);
        }
        m_cardLayout->insertWidget(m_cardLayout->count() - 1, card.frame);
        m_cards.append(card);
    }
    updateAllCards();
}

void SqlNotebookWidget::updateCard(int index) {
    if (index < 0 || index >= m_cards.size() || index >= m_cells.size()) return;
    const Card &card = m_cards.at(index);
    if (!card.status) return;
    const QString key = keyFor(m_cells.at(index));
    const bool running = m_busy && key == m_runningKey;
    if (card.run) card.run->setEnabled(!m_busy);
    const auto it = m_results.constFind(key);
    if (it == m_results.constEnd()) {
        card.status->setText(running ? tr("Running...") : QString());
        card.status->setVisible(running);
        card.table->hide();
        return;
    }
    const CellResult &res = it.value();
    card.status->setText(res.status);
    card.status->setVisible(!res.status.isEmpty());
    card.status->setStyleSheet(res.error ? QStringLiteral("color:#d9534f;") : QString());
    if (!res.hasTable || running) {
        card.table->hide();
        return;
    }
    QTableWidget *t = card.table;
    t->clear();
    t->setColumnCount(res.columns.size());
    t->setRowCount(res.rows.size());
    t->setHorizontalHeaderLabels(res.columns);
    for (int r = 0; r < res.rows.size(); ++r) {
        const QVector<QString> &row = res.rows.at(r);
        for (int c = 0; c < row.size() && c < res.columns.size(); ++c)
            t->setItem(r, c, new QTableWidgetItem(row.at(c)));
    }
    t->resizeColumnsToContents();
    const int rowsH = t->verticalHeader()->length() + t->horizontalHeader()->height() +
                      2 * t->frameWidth() + (t->horizontalScrollBar()->sizeHint().height());
    t->setFixedHeight(qBound(90, rowsH, 260));
    t->show();
}

void SqlNotebookWidget::updateAllCards() {
    for (int i = 0; i < m_cards.size(); ++i) updateCard(i);
}

void SqlNotebookWidget::setBusy(bool busy) {
    if (m_busy == busy) return;
    m_busy = busy;
    m_stopBtn->setEnabled(busy);
    m_runAllBtn->setEnabled(!busy);
    m_conn->setEnabled(!busy);
    emit busyChanged(busy);
}

void SqlNotebookWidget::runCell(int index) {
    if (m_busy || index < 0 || index >= m_cells.size()) return;
    const NotebookRender::SqlCell cell = m_cells.at(index);
    if (cell.markdown) return;
    const QString key = keyFor(cell);

    DbConnections::Record rec;
    QString err;
    if (!currentRecord(&rec, &err)) {
        CellResult res;
        res.status = tr("Error: %1").arg(err);
        res.error = true;
        m_results.insert(key, res);
        m_queue.clear();
        updateCard(index);
        return;
    }
    const bool allow = m_allowWrites->isChecked();
    m_results.remove(key);
    m_runningKey = key;
    m_token = std::make_shared<DbConnections::QueryInterruptToken>();
    auto token = m_token;
    auto keepDir = m_scratchDir;  // the temp DB outlives this widget until the worker returns
    const QString sql = cell.text;
    setBusy(true);
    updateAllCards();
    m_elapsed.start();
    m_timeout->start();
    m_watcher->setFuture(QtConcurrent::run([rec, sql, allow, token, keepDir]() {
        return DbConnections::runQuery(rec, sql, kMaxRows, allow, token.get());
    }));
}

void SqlNotebookWidget::onQueryFinished() {
    m_timeout->stop();
    const DbConnections::QueryResult qr = m_watcher->result();
    const qint64 ms = m_elapsed.elapsed();
    CellResult res;
    if (qr.ok) {
        res.hasTable = !qr.columns.isEmpty();
        res.columns = qr.columns;
        res.rows = qr.rows;
        if (res.hasTable) {
            res.status = tr("%n row(s) in %1 ms", nullptr, qr.rowsReturned).arg(ms);
            if (qr.truncated)
                res.status += tr(" (truncated: showing the first %1 rows)").arg(kMaxRows);
        } else {
            res.status = tr("Done in %1 ms").arg(ms);
        }
        if (!qr.warning.isEmpty()) res.status += QStringLiteral("\n") + qr.warning;
    } else {
        res.error = true;
        if (qr.errorKind == QLatin1String("cancelled"))
            res.status = tr("Stopped.");
        else if (qr.errorKind == QLatin1String("non_select"))
            res.status = tr("Error: read-only connection. This statement changes data or is not a "
                            "single statement; tick \"Allow writes\" to run it.");
        else
            res.status = tr("Error: %1").arg(qr.error.isEmpty() ? qr.errorKind : qr.error);
    }
    m_results.insert(m_runningKey, res);
    const bool continueQueue = qr.ok && !(m_token && m_token->interruptRequested());
    m_token.reset();
    setBusy(false);
    updateAllCards();
    if (continueQueue) runNextQueued();
    else m_queue.clear();
}

void SqlNotebookWidget::runAll() {
    if (m_busy) return;
    m_queue.clear();
    for (const auto &c : qAsConst(m_cells))
        if (!c.markdown) m_queue << keyFor(c);
    runNextQueued();
}

void SqlNotebookWidget::runNextQueued() {
    while (!m_queue.isEmpty() && !m_busy) {
        const QString key = m_queue.takeFirst();
        for (int k = 0; k < m_cells.size(); ++k) {
            if (keyFor(m_cells.at(k)) == key && !m_cells.at(k).markdown) {
                runCell(k);
                break;
            }
        }
    }
}

void SqlNotebookWidget::runCellAtLine(int line) {
    runCell(NotebookRender::cellIndexForLine(m_cells, line));
}

void SqlNotebookWidget::stop() {
    m_queue.clear();
    if (!m_busy || !m_token) return;
    m_token->requestInterrupt();
    m_results[m_runningKey].status = tr("Stopping...");
    updateAllCards();
}

QTableWidget *SqlNotebookWidget::resultTableAt(int index) {
    if (index < 0 || index >= m_cards.size()) return nullptr;
    return m_cards.at(index).table;
}

QString SqlNotebookWidget::statusTextAt(int index) {
    if (index < 0 || index >= m_cards.size() || !m_cards.at(index).status) return QString();
    return m_cards.at(index).status->text();
}

void SqlNotebookWidget::applyColors(const NotebookRender::Colors &c) {
    m_colors = c;
    setStyleSheet(QStringLiteral(
        "QWidget#sqlNotebookCells { background: %1; }"
        "QFrame#sqlNotebookCell { background: %1; border: 1px solid %5; border-radius: 4px; }"
        "QFrame#sqlNotebookCell QLabel { color: %2; border: none; }"
        "QLabel#sqlCellTitle { color: %6; font-weight: 600; }"
        "QLabel#sqlCellCode { background: %4; padding: 6px; }"
        "QLabel#sqlCellStatus { color: %3; }"
        "QFrame#sqlNotebookCell QTableWidget { background: %1; color: %2; gridline-color: %5; }"
        "QFrame#sqlNotebookCell QHeaderView { background: %4; }"
        "QFrame#sqlNotebookCell QHeaderView::section { background: %4; color: %2; "
        "border: 1px solid %5; padding: 2px 4px; }"
        "QFrame#sqlNotebookCell QTableCornerButton::section { background: %4; border: 1px solid %5; }"
        "QWidget#sqlNotebookBar QToolButton, QFrame#sqlNotebookCell QToolButton { color: %2; }"
        "QWidget#sqlNotebookBar QToolButton:disabled, QFrame#sqlNotebookCell QToolButton:disabled "
        "{ color: %3; }")
        .arg(c.bg, c.fg, c.dimFg, c.codeBg, c.border, c.accent));
    const QColor fg(c.fg);
    m_runAllBtn->setIcon(tintedStandardIcon(style(), QStyle::SP_MediaPlay, fg));
    m_stopBtn->setIcon(tintedStandardIcon(style(), QStyle::SP_MediaStop, fg));
    rebuildCards();
}
