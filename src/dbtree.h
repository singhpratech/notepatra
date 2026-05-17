// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef DBTREE_H
#define DBTREE_H

#include <QDialog>
#include <QHash>
#include <QString>
#include <QVector>

class QTreeWidget;
class QTreeWidgetItem;
class QPushButton;
class QLineEdit;
class QLabel;
class QComboBox;

// v0.1.55 — Database Tree dialog. Opens from the AI panel's Data mode and
// surfaces every saved DB connection in a tree:
//
//   ▾ prod-postgres            [QPSQL]
//      ▸ public                (8 tables)
//          customers           (12 cols)
//          orders              (18 cols)
//      ▸ analytics             (1 table)
//   ▾ ./local.duckdb           [DUCKDB]
//          customers           ▼
//              id              INTEGER NOT NULL
//              name            VARCHAR
//              signed_up       TIMESTAMP
//
// Schema is introspected lazily — clicking a connection runs the schema
// query (INFORMATION_SCHEMA for SQL drivers, listTables/describeTable
// for DuckDB) and caches the result for the dialog's lifetime.
//
// Right-click on a table:
//   * "Sample 10 rows"      — runs SELECT * LIMIT 10, shows result inline
//   * "Send schema to AI"   — emits schemaForAi(blob); AIPanel pins it in
//                             the next outgoing prompt
//   * "Copy SELECT *"       — clipboard
class DbTreeDialog : public QDialog {
    Q_OBJECT
public:
    explicit DbTreeDialog(QWidget *parent = nullptr);

signals:
    // The user picked "Send schema to AI" on a table. The blob includes
    // the connection name + table qualified name + columns + types so the
    // model can answer questions about it without further tool calls.
    void schemaForAi(const QString &blob);

private slots:
    void onConnectionChanged();
    void onItemClicked(QTreeWidgetItem *item, int col);
    void onRefresh();
    void onContextMenu(const QPoint &pos);

private:
    void rebuildTree();
    void introspectAndExpand(QTreeWidgetItem *connItem);

    QComboBox *m_connCombo = nullptr;
    QPushButton *m_refreshBtn = nullptr;
    QLineEdit *m_filter = nullptr;
    QLabel *m_status = nullptr;
    QTreeWidget *m_tree = nullptr;
    // Cache of which connection has had its schema introspected this session.
    // Key: connection name. Value: true if loaded.
    QHash<QString, bool> m_introspected;
};

#endif
