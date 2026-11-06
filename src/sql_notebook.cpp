// SPDX-License-Identifier: GPL-3.0-or-later

#include "sql_notebook.h"

#include "fonts.h"

#include <QApplication>
#include <QCheckBox>
#include <QClipboard>
#include <QComboBox>
#include <QFrame>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QIcon>
#include <QKeyEvent>
#include <QMap>
#include <QPainter>
#include <QPixmap>
#include <QLabel>
#include <QScrollArea>
#include <QScrollBar>
#include <QSet>
#include <QStyle>
#include <QTableWidget>
#include <QSqlDatabase>
#include <QSqlQuery>
#include <QTemporaryDir>
#include <QThreadPool>
#include <QTimer>
#include <QToolButton>
#include <QVBoxLayout>
#include <QtConcurrent>

#include <algorithm>

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

// Never destroyed: ~QThreadPool (like the global pool at app exit) would wait for a
// stopped Qt-SQL query that is still running.
QThreadPool *notebookPool() {
    static QThreadPool *pool = [] {
        auto *p = new QThreadPool;
        p->setMaxThreadCount(64);
        return p;
    }();
    return pool;
}

// WAL lets later cells write while a stopped query still holds a read transaction.
void enableWal(const QString &file) {
    const QString name = QStringLiteral("notepatra-notebook-wal");
    {
        QSqlDatabase db = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), name);
        db.setDatabaseName(file);
        if (db.open()) QSqlQuery(db).exec(QStringLiteral("PRAGMA journal_mode=WAL"));
        db.close();
    }
    QSqlDatabase::removeDatabase(name);
}

// Ctrl+C copies the selected cells as TSV: no header row, NULL as an empty field,
// fields holding a tab, newline or quote are double-quoted (spreadsheet paste).
class ResultTable : public QTableWidget {
public:
    static constexpr int kNullRole = Qt::UserRole + 1;

protected:
    bool event(QEvent *e) override {
        // Claim Ctrl+C before the window-wide Edit > Copy shortcut takes it.
        if (e->type() == QEvent::ShortcutOverride && static_cast<QKeyEvent *>(e) == QKeySequence::Copy) {
            e->accept();
            return true;
        }
        return QTableWidget::event(e);
    }
    void keyPressEvent(QKeyEvent *e) override {
        if (e == QKeySequence::Copy) {
            copySelection();
            e->accept();
            return;
        }
        QTableWidget::keyPressEvent(e);
    }

private:
    void copySelection() {
        const QModelIndexList sel = selectionModel() ? selectionModel()->selectedIndexes() : QModelIndexList();
        if (sel.isEmpty()) return;
        QMap<int, QMap<int, QString>> grid;
        QSet<int> colSet;
        for (const QModelIndex &ix : sel) {
            const bool isNull = ix.data(kNullRole).toBool();
            grid[ix.row()][ix.column()] = isNull ? QString() : ix.data(Qt::DisplayRole).toString();
            colSet.insert(ix.column());
        }
        QList<int> cols = colSet.values();
        std::sort(cols.begin(), cols.end());
        QStringList lines;
        for (auto r = grid.cbegin(); r != grid.cend(); ++r) {
            QStringList fields;
            for (int c : cols) {
                QString v = r.value().value(c);
                if (v.contains(QLatin1Char('\t')) || v.contains(QLatin1Char('\n')) ||
                    v.contains(QLatin1Char('\r')) || v.contains(QLatin1Char('"')))
                    v = QLatin1Char('"') + v.replace(QLatin1Char('"'), QLatin1String("\"\"")) + QLatin1Char('"');
                fields << v;
            }
            lines << fields.join(QLatin1Char('\t'));
        }
        QApplication::clipboard()->setText(lines.join(QLatin1Char('\n')));
    }
};

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
    m_hint = new QLabel(tr("Tip: add -- %% lines to split this file into cells"));
    m_hint->setObjectName(QStringLiteral("sqlNotebookHint"));
    m_hint->setWordWrap(true);
    m_hint->setToolTip(tr("Without -- %% lines, Ctrl+Return runs the statement under the cursor."));
    m_hint->hide();
    root->addWidget(m_hint);

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

    m_timeout = new QTimer(this);
    m_timeout->setSingleShot(true);
    m_timeout->setInterval(m_timeoutMs);
    connect(m_timeout, &QTimer::timeout, this, [this]() {
        abandonRun(m_timeoutMs % 1000 == 0 ? tr("Timed out after %1 s.").arg(m_timeoutMs / 1000)
                                            : tr("Timed out after %1 ms.").arg(m_timeoutMs));
    });

    connect(m_conn, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
            [this](int) { onConnectionChanged(); });
    connect(m_runAllBtn, &QToolButton::clicked, this, &SqlNotebookWidget::runAll);
    connect(m_stopBtn, &QToolButton::clicked, this, &SqlNotebookWidget::stop);
}

SqlNotebookWidget::~SqlNotebookWidget() {
    // The worker keeps its own token/temp-dir references; ask it to stop and give an
    // interruptible (DuckDB) query a bounded moment to unwind.
    if (m_busy && m_token) m_token->requestInterrupt();
    notebookPool()->waitForDone(2000);
}

void SqlNotebookWidget::setTimeoutMs(int ms) {
    m_timeoutMs = qMax(1, ms);
    m_timeout->setInterval(m_timeoutMs);
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
        if (!m_scratchWalDone) {
            m_scratchWalDone = true;
            enableWal(r.database);
        }
    }
    *out = r;
    return true;
}

void SqlNotebookWidget::setCells(const QVector<NotebookRender::SqlCell> &cells, QObject *doc) {
    if (m_conn->count() == 0) refreshConnections();
    const bool switched = doc != m_doc;
    if (doc && !m_docs.contains(doc)) {
        connect(doc, &QObject::destroyed, this, [this, doc]() {
            m_docs.remove(doc);
            if (m_doc == doc) m_doc = nullptr;
            if (m_queueDoc == doc) m_queue.clear();
        });
    }
    m_doc = doc;
    QVector<Slot> &docSlots = m_docs[doc];
    bool sameShape = !switched && cells.size() == m_cells.size();
    for (int i = 0; sameShape && i < cells.size(); ++i)
        sameShape = cells.at(i).text == m_cells.at(i).text &&
                    cells.at(i).markdown == m_cells.at(i).markdown;
    mergeCells(docSlots, cells);
    m_cells = cells;
    if (!sameShape) rebuildCards();
}

void SqlNotebookWidget::mergeCells(QVector<Slot> &cellSlots, const QVector<NotebookRender::SqlCell> &cells) {
    const QVector<Slot> old = cellSlots;
    const int n = old.size(), m = cells.size();
    auto same = [](const NotebookRender::SqlCell &a, const NotebookRender::SqlCell &b) {
        return a.text == b.text && a.markdown == b.markdown;
    };
    int pre = 0;
    while (pre < n && pre < m && same(old.at(pre).cell, cells.at(pre))) ++pre;
    int suf = 0;
    while (suf < n - pre && suf < m - pre && same(old.at(n - 1 - suf).cell, cells.at(m - 1 - suf))) ++suf;

    QVector<Slot> out(m);
    QVector<bool> used(n, false);
    for (int i = 0; i < pre; ++i) { out[i] = old.at(i); used[i] = true; }
    for (int k = 0; k < suf; ++k) { out[m - 1 - k] = old.at(n - 1 - k); used[n - 1 - k] = true; }
    const int oldMid = n - pre - suf, newMid = m - pre - suf;
    for (int j = 0; j < newMid; ++j) {
        const int ni = pre + j;
        int match = -1;
        if (oldMid == newMid) {
            // Same number of cells: the j-th changed cell is the j-th edited one.
            if (old.at(pre + j).cell.markdown == cells.at(ni).markdown) match = pre + j;
        } else {
            for (int oi = pre; oi < n - suf && match < 0; ++oi)
                if (!used[oi] && same(old.at(oi).cell, cells.at(ni))) match = oi;
        }
        if (match >= 0) {
            out[ni] = old.at(match);
            used[match] = true;
        } else {
            out[ni].id = m_nextId++;
        }
    }
    for (int i = 0; i < m; ++i) out[i].cell = cells.at(i);
    cellSlots = out;
}

SqlNotebookWidget::Slot *SqlNotebookWidget::findSlot(QObject *doc, int id) {
    auto it = m_docs.find(doc);
    if (it == m_docs.end()) return nullptr;
    for (Slot &s : it.value())
        if (s.id == id) return &s;
    return nullptr;
}

int SqlNotebookWidget::indexOfId(int id) const {
    const auto it = m_docs.constFind(m_doc);
    if (it == m_docs.constEnd()) return -1;
    for (int i = 0; i < it.value().size(); ++i)
        if (it.value().at(i).id == id) return i;
    return -1;
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
            card.run->setToolTip(tr("Run this cell (Ctrl+Return in the editor; Ctrl+Shift+Return runs and "
                                    "moves to the next cell)"));
            card.run->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
            card.run->setAutoRaise(true);
            const int id = m_docs.value(m_doc).value(i).id;
            connect(card.run, &QToolButton::clicked, this, [this, id]() { runCell(indexOfId(id)); });
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
            card.table = new ResultTable;
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
    const auto docIt = m_docs.constFind(m_doc);
    if (docIt == m_docs.constEnd() || index >= docIt.value().size()) return;
    const Slot &slot = docIt.value().at(index);
    const bool running = m_busy && m_runDoc == m_doc && m_runId == slot.id;
    if (card.run) card.run->setEnabled(!m_busy);
    if (!slot.hasResult || running) {
        card.status->setText(running ? tr("Running...") : QString());
        card.status->setVisible(running);
        card.status->setStyleSheet(QString());
        card.table->hide();
        return;
    }
    const CellResult &res = slot.result;
    const bool stale = slot.ranText != slot.cell.text;
    card.status->setText(stale ? tr("(stale: the cell changed since this run) %1").arg(res.status)
                               : res.status);
    card.status->setVisible(!card.status->text().isEmpty());
    card.status->setStyleSheet(res.error ? QStringLiteral("color:#d9534f;") : QString());
    if (!res.hasTable) {
        card.table->hide();
        return;
    }
    QTableWidget *t = card.table;
    t->clear();
    t->setColumnCount(res.columns.size());
    t->setRowCount(res.rows.size());
    t->setHorizontalHeaderLabels(res.columns);
    // NULL looks the same for every driver: a dimmed italic "NULL" in the theme's dim text colour.
    const QColor dim = m_colors.dimFg.isEmpty() ? palette().color(QPalette::Disabled, QPalette::Text)
                                                : QColor(m_colors.dimFg);
    QFont nullFont = t->font();
    nullFont.setItalic(true);
    for (int r = 0; r < res.rows.size(); ++r) {
        const QVector<QString> &row = res.rows.at(r);
        const QVector<bool> nullRow = res.nulls.value(r);
        for (int c = 0; c < row.size() && c < res.columns.size(); ++c) {
            if (nullRow.value(c)) {
                auto *item = new QTableWidgetItem(QStringLiteral("NULL"));
                item->setForeground(dim);
                item->setFont(nullFont);
                item->setData(ResultTable::kNullRole, true);
                t->setItem(r, c, item);
            } else {
                t->setItem(r, c, new QTableWidgetItem(row.at(c)));
            }
        }
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
    if (m_busy || index < 0 || index >= m_cells.size() || m_cells.at(index).markdown) return;
    const int id = m_docs.value(m_doc).value(index).id;
    startRun(m_doc, id, m_cells.at(index).text);
}

void SqlNotebookWidget::startRun(QObject *doc, int id, const QString &sql, const QString &statusPrefix) {
    Slot *slot = findSlot(doc, id);
    if (m_busy || !slot || slot->cell.markdown) return;
    DbConnections::Record rec;
    QString err;
    if (!currentRecord(&rec, &err)) {
        slot->hasResult = true;
        slot->result = CellResult();
        slot->result.status = tr("Error: %1").arg(err);
        slot->result.error = true;
        slot->ranText = slot->cell.text;
        m_queue.clear();
        if (doc == m_doc) updateAllCards();
        return;
    }
    const bool allow = m_allowWrites->isChecked();
    m_runInterruptible = rec.driver.compare(QLatin1String("DUCKDB"), Qt::CaseInsensitive) == 0;
    slot->hasResult = false;
    m_runDoc = doc;
    m_runId = id;
    m_runText = slot->cell.text;
    m_runPrefix = statusPrefix;
    m_token = std::make_shared<DbConnections::QueryInterruptToken>();
    auto token = m_token;
    auto keepDir = m_scratchDir;  // the temp DB outlives this widget until the worker returns
    setBusy(true);
    updateAllCards();
    m_elapsed.start();
    m_timeout->start();
    const quint64 gen = ++m_generation;
    auto *watcher = new QFutureWatcher<DbConnections::QueryResult>(this);
    connect(watcher, &QFutureWatcher<DbConnections::QueryResult>::finished, this, [this, watcher, gen]() {
        watcher->deleteLater();
        if (gen == m_generation && m_busy) onQueryFinished(watcher->result());
    });
    watcher->setFuture(QtConcurrent::run(notebookPool(), [rec, sql, allow, token, keepDir]() {
        return DbConnections::runQuery(rec, sql, kMaxRows, allow, token.get());
    }));
}

void SqlNotebookWidget::abandonRun(const QString &status) {
    m_queue.clear();
    if (!m_busy) return;
    if (m_token) m_token->requestInterrupt();  // real interrupt on DuckDB, a flag only on Qt SQL
    ++m_generation;
    m_timeout->stop();
    CellResult res;
    res.status = m_runInterruptible
        ? status
        : status + QLatin1Char(' ') +
              tr("The database may still finish the query in the background; its result is discarded.");
    res.error = true;
    storeRunResult(res);
    m_token.reset();
    setBusy(false);
    updateAllCards();
}

void SqlNotebookWidget::storeRunResult(const CellResult &res) {
    if (Slot *slot = findSlot(m_runDoc, m_runId)) {
        slot->hasResult = true;
        slot->result = res;
        slot->result.status.prepend(m_runPrefix);
        slot->ranText = m_runText;
    }
}

void SqlNotebookWidget::onQueryFinished(const DbConnections::QueryResult &qr) {
    m_timeout->stop();
    const qint64 ms = m_elapsed.elapsed();
    CellResult res;
    if (qr.ok) {
        res.hasTable = !qr.columns.isEmpty();
        res.columns = qr.columns;
        res.rows = qr.rows;
        res.nulls = qr.nulls;
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
    storeRunResult(res);
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
    m_queueDoc = m_doc;
    for (const Slot &slot : m_docs.value(m_doc))
        if (!slot.cell.markdown) m_queue << slot.id;
    runNextQueued();
}

void SqlNotebookWidget::runNextQueued() {
    while (!m_queue.isEmpty() && !m_busy) {
        const int id = m_queue.takeFirst();
        if (const Slot *slot = findSlot(m_queueDoc, id)) startRun(m_queueDoc, id, slot->cell.text);
    }
}

void SqlNotebookWidget::runCellAtLine(int line) {
    runCell(NotebookRender::cellIndexForLine(m_cells, line));
}

namespace {
int offsetAt(const QString &text, int line, int col) {
    int pos = 0;
    for (int l = 0; l < line; ++l) {
        const int nl = text.indexOf(QLatin1Char('\n'), pos);
        if (nl < 0) return text.size();
        pos = nl + 1;
    }
    int lineEnd = text.indexOf(QLatin1Char('\n'), pos);
    if (lineEnd < 0) lineEnd = text.size();
    return qMin(pos + qMax(0, col), lineEnd);
}

void lineColAt(const QString &text, int pos, int *line, int *col) {
    *line = text.left(pos).count(QLatin1Char('\n'));
    *col = pos - (text.lastIndexOf(QLatin1Char('\n'), pos - 1) + 1);
}
} // namespace

void SqlNotebookWidget::runAtCursor(const QString &text, int line, int col) {
    if (NotebookRender::hasSqlCellSeparators(text)) {
        runCellAtLine(line);
        return;
    }
    if (m_busy || m_cells.isEmpty() || m_cells.at(0).markdown) return;
    const QVector<NotebookRender::SqlStatementSpan> spans = NotebookRender::splitSqlStatements(text);
    const int at = NotebookRender::statementIndexAt(spans, offsetAt(text, line, col));
    if (at < 0) return;
    const auto &sp = spans.at(at);
    int stmtLine = 0, stmtCol = 0;
    lineColAt(text, sp.start, &stmtLine, &stmtCol);
    startRun(m_doc, m_docs.value(m_doc).value(0).id, text.mid(sp.start, sp.end - sp.start).trimmed(),
             tr("Statement at line %1: ").arg(stmtLine + 1));
}

bool SqlNotebookWidget::nextRunTarget(const QString &text, int line, int col, int *outLine, int *outCol) const {
    if (NotebookRender::hasSqlCellSeparators(text)) {
        const int at = NotebookRender::cellIndexForLine(m_cells, line);
        if (at < 0) return false;
        for (int i = at + 1; i < m_cells.size(); ++i) {
            if (m_cells.at(i).markdown) continue;
            *outLine = m_cells.at(i).bodyLine;
            *outCol = 0;
            return true;
        }
        return false;
    }
    const QVector<NotebookRender::SqlStatementSpan> spans = NotebookRender::splitSqlStatements(text);
    const int at = NotebookRender::statementIndexAt(spans, offsetAt(text, line, col));
    if (at < 0 || at + 1 >= spans.size()) return false;
    lineColAt(text, spans.at(at + 1).start, outLine, outCol);
    return true;
}

void SqlNotebookWidget::setSeparatorHintVisible(bool on) {
    m_hint->setVisible(on);
}

void SqlNotebookWidget::stop() {
    abandonRun(tr("Stopped."));
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
        "QLabel#sqlNotebookHint { background: %1; color: %3; padding: 2px 8px; }"
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
