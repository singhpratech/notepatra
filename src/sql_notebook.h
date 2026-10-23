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

    void setCells(const QVector<NotebookRender::SqlCell> &cells);
    void runCell(int index);
    void runAll();
    void runCellAtLine(int line);
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
    };
    struct Card {
        QFrame *frame = nullptr;
        QToolButton *run = nullptr;
        QLabel *status = nullptr;
        QTableWidget *table = nullptr;
    };

    static QString keyFor(const NotebookRender::SqlCell &c) { return c.text.trimmed(); }
    void rebuildCards();
    void updateCard(int index);
    void updateAllCards();
    void setBusy(bool busy);
    void onQueryFinished();
    void onConnectionChanged();
    bool currentRecord(DbConnections::Record *out, QString *err);
    void runNextQueued();

    QComboBox *m_conn = nullptr;
    QCheckBox *m_allowWrites = nullptr;
    QToolButton *m_runAllBtn = nullptr;
    QToolButton *m_stopBtn = nullptr;
    QLabel *m_emptyLabel = nullptr;
    QWidget *m_cardHost = nullptr;
    QVBoxLayout *m_cardLayout = nullptr;

    QVector<NotebookRender::SqlCell> m_cells;
    QVector<Card> m_cards;
    QHash<QString, CellResult> m_results;
    QStringList m_queue;
    QString m_runningKey;
    bool m_busy = false;
    NotebookRender::Colors m_colors;

    QVector<DbConnections::Record> m_saved;
    std::shared_ptr<QTemporaryDir> m_scratchDir;
    std::shared_ptr<DbConnections::QueryInterruptToken> m_token;
    QFutureWatcher<DbConnections::QueryResult> *m_watcher = nullptr;
    QTimer *m_timeout = nullptr;
    QElapsedTimer m_elapsed;
};

#endif // NOTEPATRA_SQL_NOTEBOOK_H
