// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef NOTEPATRA_SQL_NOTEBOOK_H
#define NOTEPATRA_SQL_NOTEBOOK_H

// Cell-by-cell runner for a .sql file split on "-- %%" lines. Every cell goes
// through DbConnections::runQuery on a worker thread, one query at a time.

#include "dbconnections.h"
#include "notebook_render.h"

#include <QElapsedTimer>
#include <QFutureWatcher>
#include <QHash>
#include <QStringList>
#include <QVector>
#include <QWidget>

#include <memory>

class QCheckBox;
class QComboBox;
class QFrame;
class QLabel;
class QTableWidget;
class QTemporaryDir;
class QTimer;
class QToolButton;
class QVBoxLayout;

class SqlNotebookWidget : public QWidget {
    Q_OBJECT
public:
    explicit SqlNotebookWidget(QWidget *parent = nullptr);
    ~SqlNotebookWidget() override;

    // `doc` (normally the Editor) owns the results; each document keeps its own.
    void setCells(const QVector<NotebookRender::SqlCell> &cells, QObject *doc = nullptr);
    void runCell(int index);
    void runAll();
    void runCellAtLine(int line);
    // Runs the cell under the cursor; in a file without "-- %%" lines, the statement under it.
    void runAtCursor(const QString &text, int line, int col);
    // Where Run-and-Advance moves the cursor next; false on the last cell / statement.
    bool nextRunTarget(const QString &text, int line, int col, int *outLine, int *outCol) const;
    // "Tip: add -- %% lines ..." under the toolbar, for files with no separators.
    void setSeparatorHintVisible(bool on);
    void stop();
    void applyColors(const NotebookRender::Colors &c);
    // Re-reads saved connections, keeping the current selection when it still exists.
    void refreshConnections();

    int cellCount() const { return m_cells.size(); }
    bool isBusy() const { return m_busy; }

    // Test hooks.
    QTableWidget *resultTableAt(int index);
    QString statusTextAt(int index);
    QComboBox *connectionCombo() const { return m_conn; }
    QCheckBox *allowWritesBox() const { return m_allowWrites; }
    void setTimeoutMs(int ms);

    static constexpr int kMaxRows = 500;
    static constexpr int kTimeoutMs = 120000;

signals:
    void busyChanged(bool busy);

private:
    struct CellResult {
        QString status;
        bool error = false;
        bool hasTable = false;
        QStringList columns;
        QVector<QVector<QString>> rows;
        QVector<QVector<bool>> nulls;
    };
    // One cell of one document; `id` survives edits so a result follows its cell.
    struct Slot {
        int id = 0;
        NotebookRender::SqlCell cell;
        bool hasResult = false;
        CellResult result;
        QString ranText;  // cell text when the result was produced (differs = stale)
    };
    struct Card {
        QFrame *frame = nullptr;
        QToolButton *run = nullptr;
        QLabel *status = nullptr;
        QTableWidget *table = nullptr;
    };

    // Carries results across an edit: common prefix/suffix by text, the changed middle by position.
    void mergeCells(QVector<Slot> &cellSlots, const QVector<NotebookRender::SqlCell> &cells);
    Slot *findSlot(QObject *doc, int id);
    int indexOfId(int id) const;
    void startRun(QObject *doc, int id, const QString &sql, const QString &statusPrefix = QString());
    void rebuildCards();
    void updateCard(int index);
    void updateAllCards();
    void setBusy(bool busy);
    void onQueryFinished(const DbConnections::QueryResult &qr);
    // Frees the UI now; a Qt-SQL worker cannot be interrupted, so its late result is dropped.
    void abandonRun(const QString &status);
    void storeRunResult(const CellResult &res);
    void onConnectionChanged();
    bool currentRecord(DbConnections::Record *out, QString *err);
    void runNextQueued();

    QComboBox *m_conn = nullptr;
    QCheckBox *m_allowWrites = nullptr;
    QToolButton *m_runAllBtn = nullptr;
    QToolButton *m_stopBtn = nullptr;
    QLabel *m_emptyLabel = nullptr;
    QLabel *m_hint = nullptr;
    QWidget *m_cardHost = nullptr;
    QVBoxLayout *m_cardLayout = nullptr;

    QVector<NotebookRender::SqlCell> m_cells;  // the shown document's cells
    QVector<Card> m_cards;
    QHash<QObject *, QVector<Slot>> m_docs;    // entry dropped when the document is destroyed
    QObject *m_doc = nullptr;
    int m_nextId = 1;
    QObject *m_runDoc = nullptr;
    int m_runId = 0;
    QString m_runText;
    QString m_runPrefix;
    QObject *m_queueDoc = nullptr;
    QVector<int> m_queue;
    bool m_busy = false;
    NotebookRender::Colors m_colors;

    QVector<DbConnections::Record> m_saved;
    std::shared_ptr<QTemporaryDir> m_scratchDir;
    std::shared_ptr<DbConnections::QueryInterruptToken> m_token;
    quint64 m_generation = 0;  // bumped per run and on Stop/timeout; stale results are dropped
    bool m_scratchWalDone = false;
    bool m_runInterruptible = false;  // DuckDB honours the token; Qt SQL drivers do not
    QTimer *m_timeout = nullptr;
    int m_timeoutMs = kTimeoutMs;
    QElapsedTimer m_elapsed;
};

#endif // NOTEPATRA_SQL_NOTEBOOK_H
