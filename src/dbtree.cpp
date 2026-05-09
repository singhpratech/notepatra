#include "dbtree.h"

#include "dbconnections.h"
#ifdef NOTEPATRA_HAVE_DUCKDB
#include "duckdb_client.h"
#endif

#include <QApplication>
#include <QClipboard>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QJsonArray>
#include <QJsonObject>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QPushButton>
#include <QSqlDatabase>
#include <QSqlError>
#include <QSqlQuery>
#include <QSqlRecord>
#include <QTreeWidget>
#include <QTreeWidgetItem>
#include <QVBoxLayout>

namespace {

// Tag a tree item so the right-click handler knows what to do with it.
enum NodeKind {
    NodeConnection = 1,
    NodeSchema     = 2,
    NodeTable      = 3,
    NodeColumn     = 4,
    NodeLoading    = 99,  // placeholder while introspection is in flight
};

// Returns information_schema for QSqlDatabase-backed drivers. Keyed at
// (schema, table_name) — DuckDB's introspection sits separately.
struct TableEntry {
    QString schema;
    QString name;
    QString type;          // BASE TABLE / VIEW / etc.
    QVector<QPair<QString, QString>> columns;  // (name, type)
};

QVector<TableEntry> introspectViaQSql(const DbConnections::Record &r,
                                      QString *outError) {
    QVector<TableEntry> out;
    QString cname, openErr;
    if (!DbConnections::open(r, &cname, &openErr)) {
        if (outError) *outError = openErr;
        return out;
    }
    QSqlDatabase db = QSqlDatabase::database(cname);
    QSqlQuery q(db);

    // SQLite has its own catalog; everything else uses INFORMATION_SCHEMA.
    if (r.driver == "QSQLITE") {
        if (!q.exec("SELECT name, sql, type FROM sqlite_master "
                    "WHERE type IN ('table','view') ORDER BY name")) {
            if (outError) *outError = q.lastError().text();
            QSqlDatabase::removeDatabase(cname);
            return out;
        }
        while (q.next()) {
            TableEntry e;
            e.schema = "main";
            e.name   = q.value(0).toString();
            e.type   = q.value(2).toString().toUpper() == "VIEW" ? "VIEW" : "BASE TABLE";
            out.append(e);
        }
        // Pull columns for each table via PRAGMA table_info.
        for (TableEntry &e : out) {
            QSqlQuery qc(db);
            qc.exec(QString("PRAGMA table_info('%1')").arg(e.name.replace('\'', "''")));
            while (qc.next()) {
                e.columns.append(qMakePair(qc.value(1).toString(),
                                           qc.value(2).toString()));
            }
        }
    } else {
        // Standard INFORMATION_SCHEMA path — works for PostgreSQL, MySQL,
        // SQL Server, MariaDB, and most ODBC-bridged DBs.
        if (!q.exec("SELECT table_schema, table_name, table_type "
                    "FROM information_schema.tables "
                    "WHERE table_schema NOT IN "
                    "('information_schema','pg_catalog','sys','mysql','performance_schema') "
                    "ORDER BY table_schema, table_name")) {
            if (outError) *outError = q.lastError().text();
            QSqlDatabase::removeDatabase(cname);
            return out;
        }
        while (q.next()) {
            TableEntry e;
            e.schema = q.value(0).toString();
            e.name   = q.value(1).toString();
            e.type   = q.value(2).toString();
            out.append(e);
        }
        for (TableEntry &e : out) {
            QSqlQuery qc(db);
            qc.prepare(
                "SELECT column_name, data_type FROM information_schema.columns "
                "WHERE table_schema = ? AND table_name = ? "
                "ORDER BY ordinal_position");
            qc.addBindValue(e.schema);
            qc.addBindValue(e.name);
            if (qc.exec()) {
                while (qc.next()) {
                    e.columns.append(qMakePair(qc.value(0).toString(),
                                               qc.value(1).toString()));
                }
            }
        }
    }
    QSqlDatabase::removeDatabase(cname);
    return out;
}

#ifdef NOTEPATRA_HAVE_DUCKDB
QVector<TableEntry> introspectViaDuckDb(const DbConnections::Record &r,
                                        QString *outError) {
    QVector<TableEntry> out;
    DuckDb::Client c;
    QString openErr;
    const QString dbField = r.database.trimmed();
    QString openPath = ":memory:";
    QString registerCsv, registerParquet, registerJson;
    if (dbField.isEmpty() || dbField == ":memory:") openPath = ":memory:";
    else if (dbField.endsWith(".duckdb", Qt::CaseInsensitive)
          || dbField.endsWith(".db", Qt::CaseInsensitive))      openPath = dbField;
    else if (dbField.endsWith(".csv",     Qt::CaseInsensitive)) registerCsv     = dbField;
    else if (dbField.endsWith(".parquet", Qt::CaseInsensitive)) registerParquet = dbField;
    else if (dbField.endsWith(".json",    Qt::CaseInsensitive)
          || dbField.endsWith(".ndjson",  Qt::CaseInsensitive)) registerJson    = dbField;
    if (!c.open(openPath, &openErr)) {
        if (outError) *outError = openErr;
        return out;
    }
    if (!registerCsv.isEmpty())     c.registerCsv(registerCsv, "data", &openErr);
    if (!registerParquet.isEmpty()) c.registerParquet(registerParquet, "data", &openErr);
    if (!registerJson.isEmpty())    c.registerJson(registerJson, "data", &openErr);

    QString tablesErr;
    auto tables = c.listTables(&tablesErr);
    if (!tablesErr.isEmpty() && outError) *outError = tablesErr;
    for (const auto &t : tables) {
        TableEntry e;
        e.schema = t.schema;
        e.name   = t.name;
        e.type   = t.kind;
        QString colErr;
        const QString qualified = t.schema.isEmpty()
                                    ? t.name
                                    : (t.schema + "." + t.name);
        auto cols = c.describeTable(qualified, &colErr);
        for (const auto &col : cols) {
            e.columns.append(qMakePair(col.name, col.dataType));
        }
        out.append(e);
    }
    return out;
}
#endif

QVector<TableEntry> introspect(const DbConnections::Record &r, QString *outError) {
    if (r.driver.compare("DUCKDB", Qt::CaseInsensitive) == 0) {
#ifdef NOTEPATRA_HAVE_DUCKDB
        return introspectViaDuckDb(r, outError);
#else
        if (outError) *outError = "DuckDB support not compiled in";
        return {};
#endif
    }
    return introspectViaQSql(r, outError);
}

}  // namespace

DbTreeDialog::DbTreeDialog(QWidget *parent) : QDialog(parent) {
    setWindowTitle(tr("Database Browser"));
    setMinimumSize(600, 480);
    resize(720, 600);

    auto *root = new QVBoxLayout(this);
    root->setContentsMargins(12, 12, 12, 12);
    root->setSpacing(8);

    auto *intro = new QLabel(tr(
        "Browse your saved database connections. Click a connection to load "
        "its tables (lazy — only fetched on click). Right-click a table for "
        "schema export, sampling, or quick queries."));
    intro->setWordWrap(true);
    intro->setStyleSheet("font-size: 11px; color: #666;");
    root->addWidget(intro);

    auto *topRow = new QHBoxLayout;
    topRow->setSpacing(6);

    auto *connLabel = new QLabel(tr("Connection:"));
    m_connCombo = new QComboBox;
    m_connCombo->setMinimumWidth(220);
    auto records = DbConnections::loadAll();
    for (const auto &r : records) {
        m_connCombo->addItem(QString("%1  [%2]").arg(r.name, r.driver), r.name);
    }
    if (records.isEmpty()) {
        m_connCombo->addItem(tr("(no connections saved)"));
        m_connCombo->setEnabled(false);
    }
    topRow->addWidget(connLabel);
    topRow->addWidget(m_connCombo, 1);

    m_refreshBtn = new QPushButton(tr("↻ Refresh"));
    m_refreshBtn->setToolTip(tr("Re-introspect the selected connection"));
    topRow->addWidget(m_refreshBtn);

    root->addLayout(topRow);

    auto *filterRow = new QHBoxLayout;
    filterRow->setSpacing(6);
    filterRow->addWidget(new QLabel(tr("Filter:")));
    m_filter = new QLineEdit;
    m_filter->setPlaceholderText(tr("type to filter tables / columns"));
    filterRow->addWidget(m_filter, 1);
    root->addLayout(filterRow);

    m_tree = new QTreeWidget;
    m_tree->setHeaderLabels({tr("Object"), tr("Type"), tr("Detail")});
    m_tree->setColumnWidth(0, 320);
    m_tree->setContextMenuPolicy(Qt::CustomContextMenu);
    m_tree->header()->setStretchLastSection(true);
    root->addWidget(m_tree, 1);

    m_status = new QLabel;
    m_status->setStyleSheet("font-size: 11px; color: #666;");
    root->addWidget(m_status);

    auto *box = new QDialogButtonBox(QDialogButtonBox::Close);
    connect(box, &QDialogButtonBox::rejected, this, &QDialog::accept);
    connect(box, &QDialogButtonBox::accepted, this, &QDialog::accept);
    root->addWidget(box);

    connect(m_connCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, [this](int) { onConnectionChanged(); });
    connect(m_refreshBtn, &QPushButton::clicked, this, &DbTreeDialog::onRefresh);
    connect(m_tree, &QTreeWidget::itemClicked, this, &DbTreeDialog::onItemClicked);
    connect(m_tree, &QTreeWidget::customContextMenuRequested,
            this, &DbTreeDialog::onContextMenu);
    connect(m_filter, &QLineEdit::textChanged, this, [this](const QString &) {
        // Cheap filter: walk the tree, hide non-matching items.
        const QString q = m_filter->text().trimmed().toLower();
        for (int i = 0; i < m_tree->topLevelItemCount(); ++i) {
            QTreeWidgetItem *conn = m_tree->topLevelItem(i);
            bool anyConnMatch = q.isEmpty();
            for (int s = 0; s < conn->childCount(); ++s) {
                QTreeWidgetItem *schema = conn->child(s);
                bool anySchMatch = q.isEmpty();
                for (int t = 0; t < schema->childCount(); ++t) {
                    QTreeWidgetItem *table = schema->child(t);
                    const bool match = q.isEmpty()
                        || table->text(0).toLower().contains(q)
                        || table->text(2).toLower().contains(q);
                    table->setHidden(!match);
                    if (match) { anySchMatch = true; anyConnMatch = true; }
                }
                schema->setHidden(!anySchMatch);
            }
            conn->setHidden(!anyConnMatch);
        }
    });

    rebuildTree();
}

void DbTreeDialog::rebuildTree() {
    m_tree->clear();
    auto records = DbConnections::loadAll();
    if (records.isEmpty()) {
        m_status->setText(tr("No DB connections saved. Add one in Tools → Database Connections."));
        return;
    }
    m_status->setText(tr("Click a connection to load its schema."));
    for (const auto &r : records) {
        auto *item = new QTreeWidgetItem(m_tree);
        item->setText(0, r.name);
        item->setText(1, r.driver);
        item->setText(2, r.database);
        item->setData(0, Qt::UserRole, NodeConnection);
        item->setData(0, Qt::UserRole + 1, r.name);

        // Single placeholder so the user gets the disclosure triangle and
        // can click to trigger lazy loading.
        auto *placeholder = new QTreeWidgetItem(item);
        placeholder->setText(0, tr("(click to load)"));
        placeholder->setData(0, Qt::UserRole, NodeLoading);
        placeholder->setForeground(0, Qt::gray);
    }
}

void DbTreeDialog::onConnectionChanged() {
    // No-op for now — refresh is on demand. The combo is a quick-filter
    // for users with many connections; the tree shows them all up top.
    const QString name = m_connCombo->currentData().toString();
    if (name.isEmpty()) return;
    for (int i = 0; i < m_tree->topLevelItemCount(); ++i) {
        QTreeWidgetItem *it = m_tree->topLevelItem(i);
        if (it->data(0, Qt::UserRole + 1).toString() == name) {
            m_tree->setCurrentItem(it);
            it->setExpanded(true);
            introspectAndExpand(it);
            break;
        }
    }
}

void DbTreeDialog::onRefresh() {
    QTreeWidgetItem *it = m_tree->currentItem();
    while (it && it->data(0, Qt::UserRole).toInt() != NodeConnection) {
        it = it->parent();
    }
    if (!it) {
        rebuildTree();
        return;
    }
    const QString name = it->data(0, Qt::UserRole + 1).toString();
    m_introspected.remove(name);
    // Drop existing children.
    while (it->childCount() > 0) delete it->takeChild(0);
    auto *placeholder = new QTreeWidgetItem(it);
    placeholder->setText(0, tr("(click to load)"));
    placeholder->setData(0, Qt::UserRole, NodeLoading);
    placeholder->setForeground(0, Qt::gray);
    introspectAndExpand(it);
}

void DbTreeDialog::onItemClicked(QTreeWidgetItem *item, int) {
    if (!item) return;
    const int kind = item->data(0, Qt::UserRole).toInt();
    if (kind == NodeConnection) introspectAndExpand(item);
    if (kind == NodeLoading && item->parent()) introspectAndExpand(item->parent());
}

void DbTreeDialog::introspectAndExpand(QTreeWidgetItem *connItem) {
    if (!connItem) return;
    const QString connName = connItem->data(0, Qt::UserRole + 1).toString();
    if (m_introspected.value(connName, false)) {
        connItem->setExpanded(true);
        return;
    }
    DbConnections::Record rec;
    if (!DbConnections::findByName(connName, &rec)) {
        m_status->setText(tr("✗ Connection '%1' not found").arg(connName));
        return;
    }
    m_status->setText(tr("Loading schema for %1 …").arg(connName));
    QApplication::processEvents();

    QString err;
    auto tables = introspect(rec, &err);
    while (connItem->childCount() > 0) delete connItem->takeChild(0);
    if (!err.isEmpty()) {
        auto *errItem = new QTreeWidgetItem(connItem);
        errItem->setText(0, tr("✗ ") + err);
        errItem->setForeground(0, Qt::red);
        m_status->setText(tr("✗ %1: %2").arg(connName, err));
        connItem->setExpanded(true);
        return;
    }
    if (tables.isEmpty()) {
        auto *empty = new QTreeWidgetItem(connItem);
        empty->setText(0, tr("(no tables)"));
        empty->setForeground(0, Qt::gray);
        m_status->setText(tr("%1 connected — 0 tables").arg(connName));
        connItem->setExpanded(true);
        m_introspected[connName] = true;
        return;
    }
    // Group by schema.
    QMap<QString, QVector<TableEntry>> bySchema;
    for (const auto &t : tables) bySchema[t.schema].append(t);
    for (auto it = bySchema.constBegin(); it != bySchema.constEnd(); ++it) {
        auto *schemaItem = new QTreeWidgetItem(connItem);
        schemaItem->setText(0, it.key().isEmpty() ? "(default)" : it.key());
        schemaItem->setText(2, tr("%1 tables").arg(it.value().size()));
        schemaItem->setData(0, Qt::UserRole, NodeSchema);
        for (const auto &t : it.value()) {
            auto *tableItem = new QTreeWidgetItem(schemaItem);
            tableItem->setText(0, t.name);
            tableItem->setText(1, t.type);
            tableItem->setText(2, tr("%1 cols").arg(t.columns.size()));
            tableItem->setData(0, Qt::UserRole, NodeTable);
            tableItem->setData(0, Qt::UserRole + 1, connName);
            tableItem->setData(0, Qt::UserRole + 2,
                t.schema.isEmpty() ? t.name : (t.schema + "." + t.name));
            // Build the columns blob now (cheap; QTreeWidget keeps it).
            QStringList colsBlob;
            for (const auto &c : t.columns) {
                auto *colItem = new QTreeWidgetItem(tableItem);
                colItem->setText(0, c.first);
                colItem->setText(1, c.second);
                colItem->setData(0, Qt::UserRole, NodeColumn);
                colsBlob.append(QString("%1 %2").arg(c.first, c.second));
            }
            tableItem->setData(0, Qt::UserRole + 3, colsBlob.join(", "));
        }
    }
    connItem->setExpanded(true);
    m_introspected[connName] = true;
    m_status->setText(tr("%1 connected — %2 tables across %3 schemas")
                          .arg(connName).arg(tables.size()).arg(bySchema.size()));
}

void DbTreeDialog::onContextMenu(const QPoint &pos) {
    QTreeWidgetItem *item = m_tree->itemAt(pos);
    if (!item) return;
    const int kind = item->data(0, Qt::UserRole).toInt();
    if (kind != NodeTable) return;
    const QString conn = item->data(0, Qt::UserRole + 1).toString();
    const QString qualified = item->data(0, Qt::UserRole + 2).toString();
    const QString colsBlob = item->data(0, Qt::UserRole + 3).toString();

    QMenu menu(this);
    QAction *aSchema = menu.addAction(tr("Send schema to AI"));
    QAction *aSample = menu.addAction(tr("Sample 10 rows (clipboard)"));
    QAction *aSelect = menu.addAction(tr("Copy SELECT * (clipboard)"));
    QAction *chosen = menu.exec(m_tree->mapToGlobal(pos));
    if (!chosen) return;
    if (chosen == aSchema) {
        const QString blob = QString(
            "Database connection: %1\n"
            "Table: %2\n"
            "Columns: %3\n").arg(conn, qualified, colsBlob);
        emit schemaForAi(blob);
        m_status->setText(tr("✓ Schema for %1 pinned for next AI prompt").arg(qualified));
    } else if (chosen == aSample) {
        QApplication::clipboard()->setText(
            QString("SELECT * FROM %1 LIMIT 10").arg(qualified));
        m_status->setText(tr("✓ SELECT … LIMIT 10 copied to clipboard"));
    } else if (chosen == aSelect) {
        QApplication::clipboard()->setText(QString("SELECT * FROM %1").arg(qualified));
        m_status->setText(tr("✓ SELECT * copied to clipboard"));
    }
}
