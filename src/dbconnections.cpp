// SPDX-License-Identifier: GPL-3.0-or-later

#include "dbconnections.h"
#ifdef NOTEPATRA_HAVE_DUCKDB
#include "duckdb_client.h"
#endif

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QFileDialog>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSqlDatabase>
#include <QSqlError>
#include <QSqlQuery>
#include <QSqlRecord>
#include <QStandardPaths>
#include <QUuid>
#include <QListWidget>
#include <QLineEdit>
#include <QSpinBox>
#include <QComboBox>
#include <QPushButton>
#include <QLabel>
#include <QHBoxLayout>
#include <QVBoxLayout>
#include <QFormLayout>
#include <QGroupBox>
#include <QMessageBox>
#include <QDialogButtonBox>

namespace DbConnections {

QString configPath() {
    return QDir::homePath() + QStringLiteral("/.config/notepatra/db-connections.json");
}

// ── Password obfuscation ────────────────────────────────────────────────
// XOR-with-fixed-key + base64. NOT real encryption — purpose is to keep
// the plaintext password from sitting in a JSON file that anyone with
// `cat` can read. A determined attacker with the source can decode it
// trivially. Documented in CHANGELOG / release notes.
//
// Symmetric: obfuscatePassword("hello") -> "VW...="
//            obfuscatePassword("VW...=") -> "hello"
// We auto-detect direction: if the input is valid base64 AND decoding
// produces valid UTF-8 after XOR, treat it as obfuscated. Otherwise
// treat it as plaintext that needs to be obfuscated.

static QByteArray xorBytes(const QByteArray &in) {
    static const QByteArray kKey = QByteArrayLiteral(
        "notepatra-v1-pwd-obfuscation-key-not-encryption");
    QByteArray out;
    out.resize(in.size());
    for (int i = 0; i < in.size(); ++i) {
        out[i] = in[i] ^ kKey[i % kKey.size()];
    }
    return out;
}

static bool tryDeobfuscate(const QString &maybe, QString *out) {
    // Quick reject: base64 only contains A-Z a-z 0-9 + / = and is at least
    // length 4. (Empty string round-trips to empty regardless.)
    if (maybe.isEmpty()) { if (out) *out = QString(); return true; }
    if (maybe.length() < 4 || (maybe.length() % 4) != 0) return false;
    for (QChar c : maybe) {
        const ushort u = c.unicode();
        const bool ok = (u >= 'A' && u <= 'Z') || (u >= 'a' && u <= 'z') ||
                        (u >= '0' && u <= '9') || u == '+' || u == '/' || u == '=';
        if (!ok) return false;
    }
    QByteArray decoded = QByteArray::fromBase64(maybe.toUtf8(),
                                                QByteArray::AbortOnBase64DecodingErrors);
    if (decoded.isEmpty()) return false;
    QByteArray xored = xorBytes(decoded);
    // Validate UTF-8 — QString::fromUtf8 is lenient (replaces invalid bytes
    // with U+FFFD), so we round-trip and require byte-equality.
    QString candidate = QString::fromUtf8(xored);
    if (candidate.toUtf8() != xored) return false;
    if (out) *out = candidate;
    return true;
}

QString obfuscatePassword(const QString &plainOrObfuscated) {
    if (plainOrObfuscated.isEmpty()) return QString();
    QString deob;
    if (tryDeobfuscate(plainOrObfuscated, &deob)) {
        // Looked like obfuscated input; return the plaintext.
        return deob;
    }
    // Treat as plaintext; produce the obfuscated form.
    QByteArray xored = xorBytes(plainOrObfuscated.toUtf8());
    return QString::fromLatin1(xored.toBase64());
}

// ── Record ──────────────────────────────────────────────────────────────

QJsonObject Record::toJson() const {
    QJsonObject o;
    o["name"] = name;
    o["driver"] = driver;
    o["host"] = host;
    o["port"] = port;
    o["database"] = database;
    o["username"] = username;
    o["password"] = obfuscatePassword(password);  // obscure at rest
    o["options"] = options;
    return o;
}

Record Record::fromJson(const QJsonObject &o) {
    Record r;
    r.name = o.value("name").toString();
    r.driver = o.value("driver").toString("QSQLITE");
    r.host = o.value("host").toString();
    r.port = o.value("port").toInt(0);
    r.database = o.value("database").toString();
    r.username = o.value("username").toString();
    r.password = obfuscatePassword(o.value("password").toString());  // de-obscure
    r.options = o.value("options").toString();
    return r;
}

// ── Persistence ─────────────────────────────────────────────────────────

QVector<Record> loadAll() {
    QVector<Record> out;
    QFile f(configPath());
    if (!f.open(QIODevice::ReadOnly)) return out;
    const QJsonDocument doc = QJsonDocument::fromJson(f.readAll());
    if (!doc.isArray()) return out;
    for (const auto &v : doc.array()) {
        if (!v.isObject()) continue;
        out.append(Record::fromJson(v.toObject()));
    }
    return out;
}

bool saveAll(const QVector<Record> &records) {
    QString path = configPath();
    QDir().mkpath(QFileInfo(path).path());
    QJsonArray arr;
    for (const auto &r : records) arr.append(r.toJson());
    const QString tmp = path + QStringLiteral(".tmp");
    QFile f(tmp);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate)) return false;
    f.write(QJsonDocument(arr).toJson(QJsonDocument::Indented));
    f.close();
    QFile::remove(path);
    return QFile::rename(tmp, path);
}

bool findByName(const QString &name, Record *out) {
    if (!out) return false;
    const auto all = loadAll();
    for (const auto &r : all) {
        if (r.name == name) { *out = r; return true; }
    }
    return false;
}

QStringList availableDrivers() {
    QStringList ds = QSqlDatabase::drivers();
#ifdef NOTEPATRA_HAVE_DUCKDB
    if (DuckDb::Client::available() && !ds.contains("DUCKDB"))
        ds.append("DUCKDB");
#endif
    return ds;
}

bool driverNeedsNetwork(const QString &driver) {
    return driver != QStringLiteral("QSQLITE")
        && driver != QStringLiteral("DUCKDB");
}

// ── Open + query ────────────────────────────────────────────────────────

bool open(const Record &r, QString *outConnectionName, QString *outError) {
    if (!QSqlDatabase::drivers().contains(r.driver)) {
        if (outError) *outError = QStringLiteral(
            "Qt SQL driver '%1' is not available on this system. "
            "Install the appropriate Qt SQL plugin and restart Notepatra.")
                .arg(r.driver);
        return false;
    }
    const QString cname = QStringLiteral("notepatra-%1-%2")
        .arg(r.name, QUuid::createUuid().toString(QUuid::Id128));
    QSqlDatabase db = QSqlDatabase::addDatabase(r.driver, cname);
    if (r.driver == QStringLiteral("QSQLITE")) {
        db.setDatabaseName(r.database);
    } else if (r.driver == QStringLiteral("QODBC")) {
        // v0.1.70 — QODBC fix. Qt's QODBC driver treats setDatabaseName as
        // either a DSN name (single token) OR a full DSN-less ODBC
        // connection string. setHostName/setPort/setUserName/setPassword
        // alone do NOT compose into a valid connection string for QODBC —
        // they're only used by drivers like QPSQL/QMYSQL that speak the
        // wire protocol natively. And setConnectOptions for QODBC takes
        // Qt-specific options (SQL_ATTR_LOGIN_TIMEOUT etc.), NOT raw ODBC
        // keywords like DRIVER={...} — those have to be inside the
        // connection string itself.
        //
        // Symptom before this fix: error "[unixODBC][Driver Manager]Data
        // source name not found and no default driver specified" because
        // Qt passed just "master" (the database name) as the DSN to
        // SQLDriverConnect, and unixODBC couldn't find a DSN with that
        // name in odbcinst.ini.
        //
        // Fix: assemble the FULL connection string from all fields and
        // pass it via setDatabaseName. The Options field can supply the
        // DRIVER={...} (or other keywords); if it does, we splice the
        // host/port/database/uid/pwd around it. If Options is empty we
        // fall back to a minimal SERVER=host,port;DATABASE=db construct.
        QString conn = r.options.trimmed();
        // Ensure terminating semicolon so we can append more keywords.
        if (!conn.isEmpty() && !conn.endsWith(';')) conn += ';';
        // Server is host plus comma-port (the SQL Server / ODBC convention,
        // distinct from the colon-port that PostgreSQL etc. use).
        const QString server = (r.port > 0)
            ? QString("%1,%2").arg(r.host).arg(r.port)
            : r.host;
        if (!server.isEmpty())     conn += QString("SERVER=%1;").arg(server);
        if (!r.database.isEmpty()) conn += QString("DATABASE=%1;").arg(r.database);
        if (!r.username.isEmpty()) conn += QString("UID=%1;").arg(r.username);
        if (!r.password.isEmpty()) conn += QString("PWD=%1;").arg(r.password);
        db.setDatabaseName(conn);
    } else {
        db.setHostName(r.host);
        if (r.port > 0) db.setPort(r.port);
        db.setDatabaseName(r.database);
        db.setUserName(r.username);
        db.setPassword(r.password);
        if (!r.options.isEmpty()) db.setConnectOptions(r.options);
    }
    if (!db.open()) {
        if (outError) *outError = db.lastError().text();
        QSqlDatabase::removeDatabase(cname);
        return false;
    }
    if (outConnectionName) *outConnectionName = cname;
    return true;
}

static bool isSelectOnly(const QString &sql) {
    QString s = sql.trimmed();
    // Strip leading SQL line comments and block comments.
    while (s.startsWith("--")) {
        const int nl = s.indexOf('\n');
        s = (nl < 0) ? QString() : s.mid(nl + 1).trimmed();
    }
    while (s.startsWith("/*")) {
        const int end = s.indexOf("*/");
        s = (end < 0) ? QString() : s.mid(end + 2).trimmed();
    }
    if (s.isEmpty()) return false;
    // Allowed read prefixes: SELECT, WITH (CTE leading to a SELECT), EXPLAIN,
    // PRAGMA (SQLite), SHOW (Postgres/MySQL).
    static const QStringList kReadPrefixes = {
        "SELECT", "WITH", "EXPLAIN", "PRAGMA", "SHOW", "DESCRIBE", "DESC"
    };
    const QString upper = s.left(16).toUpper();
    for (const QString &p : kReadPrefixes) {
        if (upper.startsWith(p + " ") || upper == p) return true;
    }
    return false;
}

QStringList listTables(const Record &r, bool *outOk) {
    if (outOk) *outOk = false;
    QStringList tables;

    // Per-driver introspection query. Each one filters to user tables only —
    // system catalogues (sys.* on MSSQL, pg_catalog on Postgres, mysql/perf
    // schemas on MySQL, sqlite_* on SQLite) are excluded so the model isn't
    // overwhelmed with hundreds of irrelevant names.
    QString sql;
    if (r.driver == QStringLiteral("QSQLITE")) {
        sql = QStringLiteral(
            "SELECT name FROM sqlite_master WHERE type='table' "
            "AND name NOT LIKE 'sqlite_%' ORDER BY name");
    } else if (r.driver == QStringLiteral("QPSQL")) {
        sql = QStringLiteral(
            "SELECT table_name FROM information_schema.tables "
            "WHERE table_schema NOT IN ('pg_catalog','information_schema') "
            "ORDER BY table_name");
    } else if (r.driver == QStringLiteral("QMYSQL")) {
        sql = QStringLiteral(
            "SELECT table_name FROM information_schema.tables "
            "WHERE table_schema NOT IN ('mysql','information_schema',"
            "'performance_schema','sys') AND table_schema = DATABASE() "
            "ORDER BY table_name");
    } else if (r.driver == QStringLiteral("QODBC")) {
        // SQL Server-flavoured. sys.tables only lists user tables in the
        // current DB (which the connection string's DATABASE= selects).
        sql = QStringLiteral(
            "SELECT name FROM sys.tables ORDER BY name");
    } else {
        return tables;  // DuckDB handled separately via runQuery if needed.
    }

    QueryResult q = runQuery(r, sql, 500, /*allowMutation=*/false);
    if (!q.ok) return tables;
    for (const auto &row : q.rows) {
        if (!row.isEmpty()) tables.append(row.first());
    }
    if (outOk) *outOk = true;
    return tables;
}

QueryResult runQuery(const Record &r,
                     const QString &sql,
                     int maxRows,
                     bool allowMutation) {
    QueryResult out;
    if (!allowMutation && !isSelectOnly(sql)) {
        out.error = QStringLiteral(
            "Only SELECT / WITH / EXPLAIN / PRAGMA / SHOW / DESCRIBE are "
            "allowed by default. To run a mutation, set confirm:true after "
            "user approval.");
        out.errorKind = QStringLiteral("non_select");
        return out;
    }

    // v0.1.55 — DuckDB driver path. Routes through the native libduckdb
    // wrapper instead of QSqlDatabase. Lets users connect to:
    //   * .duckdb files (driver=DUCKDB, database=/path/file.duckdb)
    //   * in-memory DBs  (driver=DUCKDB, database=:memory:)
    //   * CSV files       (driver=DUCKDB, database=/path/file.csv)
    //   * Parquet         (driver=DUCKDB, database=/path/file.parquet)
    //   * JSON            (driver=DUCKDB, database=/path/file.json)
    //   * S3              (driver=DUCKDB, database=s3://bucket/key,
    //                      options=region;access_key_id;secret;session_token)
    if (r.driver.compare(QStringLiteral("DUCKDB"), Qt::CaseInsensitive) == 0) {
#ifndef NOTEPATRA_HAVE_DUCKDB
        out.error = "DuckDB support not compiled in (rebuild with NOTEPATRA_USE_DUCKDB=ON)";
        out.errorKind = QStringLiteral("no_connection");
        return out;
#else
        if (!DuckDb::Client::available()) {
            out.error = "DuckDB support not compiled in (rebuild with NOTEPATRA_USE_DUCKDB=ON)";
            out.errorKind = QStringLiteral("no_connection");
            return out;
        }
        DuckDb::Client client;
        QString openErr;
        // DuckDB file vs file-as-data-source: distinguish by extension.
        const QString dbField = r.database.trimmed();
        QString openPath = ":memory:";
        QString registerCsv, registerParquet, registerJson;
        bool isS3 = dbField.startsWith("s3://", Qt::CaseInsensitive);
        if (dbField.isEmpty() || dbField == ":memory:") {
            openPath = ":memory:";
        } else if (dbField.endsWith(".duckdb", Qt::CaseInsensitive)
                   || dbField.endsWith(".db", Qt::CaseInsensitive)) {
            openPath = dbField;
        } else if (dbField.endsWith(".csv", Qt::CaseInsensitive)) {
            registerCsv = dbField;
        } else if (dbField.endsWith(".parquet", Qt::CaseInsensitive)) {
            registerParquet = dbField;
        } else if (dbField.endsWith(".json", Qt::CaseInsensitive)
                   || dbField.endsWith(".ndjson", Qt::CaseInsensitive)) {
            registerJson = dbField;
        }  // else: leave as :memory: and let user reference via SQL

        if (!client.open(openPath, &openErr)) {
            out.error = openErr;
            out.errorKind = QStringLiteral("open_failed");
            return out;
        }
        if (!registerCsv.isEmpty()
            && !client.registerCsv(registerCsv, "data", &openErr)) {
            out.error = openErr;
            out.errorKind = QStringLiteral("open_failed");
            return out;
        }
        if (!registerParquet.isEmpty()
            && !client.registerParquet(registerParquet, "data", &openErr)) {
            out.error = openErr;
            out.errorKind = QStringLiteral("open_failed");
            return out;
        }
        if (!registerJson.isEmpty()
            && !client.registerJson(registerJson, "data", &openErr)) {
            out.error = openErr;
            out.errorKind = QStringLiteral("open_failed");
            return out;
        }
        if (isS3) {
            // r.options encodes the S3 creds: region;access_key_id;secret;session_token
            const QStringList parts = r.options.split(';');
            const QString region = parts.value(0);
            const QString akid   = parts.value(1);
            const QString secret = parts.value(2);
            const QString sess   = parts.value(3);
            if (!client.configureS3(region, akid, secret, sess, &openErr)) {
                out.error = openErr;
                out.errorKind = QStringLiteral("open_failed");
                return out;
            }
        }
        const int cap = qMax(1, qMin(maxRows, 50000));
        DuckDb::ResultSet rs = client.exec(sql, cap);
        if (!rs.errorMessage.isEmpty()) {
            out.error = rs.errorMessage;
            out.errorKind = QStringLiteral("exec_failed");
            return out;
        }
        out.columns = rs.columns;
        out.rowsReturned = rs.rows.size();
        out.truncated = rs.truncated;
        out.rows.reserve(rs.rows.size());
        for (const DuckDb::Row &row : rs.rows) {
            QVector<QString> r2;
            r2.reserve(row.values.size());
            for (const QString &v : row.values) r2.append(v);
            out.rows.append(r2);
        }
        out.ok = true;
        return out;
#endif
    }

    QString cname, openErr;
    if (!open(r, &cname, &openErr)) {
        out.error = openErr;
        out.errorKind = QStringLiteral("open_failed");
        return out;
    }

    {
        QSqlDatabase db = QSqlDatabase::database(cname);
        QSqlQuery q(db);
        if (!q.exec(sql)) {
            out.error = q.lastError().text();
            out.errorKind = QStringLiteral("exec_failed");
        } else {
            const QSqlRecord rec = q.record();
            const int cols = rec.count();
            for (int i = 0; i < cols; ++i) out.columns.append(rec.fieldName(i));
            const int cap = qMax(1, qMin(maxRows, 50000));
            while (q.next()) {
                if (out.rowsReturned >= cap) {
                    out.truncated = true;
                    break;
                }
                QVector<QString> row;
                row.reserve(cols);
                for (int i = 0; i < cols; ++i) {
                    QVariant v = q.value(i);
                    row.append(v.isNull() ? QString() : v.toString());
                }
                out.rows.append(row);
                ++out.rowsReturned;
            }
            out.ok = (out.errorKind.isEmpty());
        }
    }
    QSqlDatabase::removeDatabase(cname);
    return out;
}

} // namespace DbConnections

// ─── DbConnectionsDialog ───────────────────────────────────────────────────

DbConnectionsDialog::DbConnectionsDialog(QWidget *parent)
    : QDialog(parent) {
    setWindowTitle(tr("Database Connections"));
    resize(720, 460);

    m_records = DbConnections::loadAll();
    m_originalCount = m_records.size();  // v0.1.70 — remembered for the delete-confirm gate

    auto *root = new QHBoxLayout(this);

    // ─── Left: list + new/delete ─
    auto *leftCol = new QVBoxLayout();
    m_list = new QListWidget;
    m_list->setMinimumWidth(220);
    leftCol->addWidget(m_list);
    auto *leftBtns = new QHBoxLayout();
    m_newBtn = new QPushButton(tr("+ New"));
    m_deleteBtn = new QPushButton(tr("Delete"));
    leftBtns->addWidget(m_newBtn);
    leftBtns->addWidget(m_deleteBtn);
    leftCol->addLayout(leftBtns);
    root->addLayout(leftCol);

    // ─── Right: form ─
    auto *form = new QFormLayout();

    // v0.1.66 — Preset dropdown above all the fields. Picking a preset
    // populates driver / port / placeholder options with sane templates,
    // so users don't have to know that SQL Server lives behind QODBC
    // with a "DRIVER={...}" connection string, or that PostgreSQL's
    // default port is 5432. They can still edit anything afterwards.
    m_preset = new QComboBox;
    m_preset->addItems({
        tr("(custom — pick a driver below)"),
        tr("SQL Server (localhost, ODBC)"),
        tr("SQL Server (Notepatra local Docker, port 14330)"),
        tr("SQL Server Express (named instance, ODBC)"),
        tr("Azure SQL Database (ODBC)"),
        tr("PostgreSQL (localhost)"),
        tr("MySQL / MariaDB (localhost)"),
        tr("SQLite (file on disk)"),
        tr("DuckDB (file or :memory:)"),
    });

    m_name = new QLineEdit;
    m_driver = new QComboBox;
    m_driver->addItems({"QSQLITE", "QPSQL", "QMYSQL", "QODBC", "DUCKDB"});
    m_host = new QLineEdit;
    m_port = new QSpinBox;
    m_port->setRange(0, 65535);
    m_port->setValue(0);
    m_port->setSpecialValueText(tr("default"));
    auto *dbRow = new QHBoxLayout;
    m_database = new QLineEdit;
    m_browseDb = new QPushButton(tr("Browse..."));
    dbRow->addWidget(m_database);
    dbRow->addWidget(m_browseDb);
    m_username = new QLineEdit;
    m_password = new QLineEdit;
    m_password->setEchoMode(QLineEdit::Password);
    m_options = new QLineEdit;

    form->addRow(tr("Preset:"), m_preset);
    form->addRow(tr("Name:"), m_name);
    form->addRow(tr("Driver:"), m_driver);
    form->addRow(tr("Host:"), m_host);
    form->addRow(tr("Port:"), m_port);
    auto *dbWrap = new QWidget;
    dbWrap->setLayout(dbRow);
    form->addRow(tr("Database:"), dbWrap);
    form->addRow(tr("Username:"), m_username);
    form->addRow(tr("Password:"), m_password);
    form->addRow(tr("Options:"), m_options);

    // v0.1.66 — driver availability + install hint, refreshed on every
    // driver change. Lives just under the form so the user sees actionable
    // guidance ("Install msodbcsql18 — see…") instead of generic "plugin
    // not available" text.
    m_driverHint = new QLabel;
    m_driverHint->setWordWrap(true);
    m_driverHint->setOpenExternalLinks(true);
    m_driverHint->setTextFormat(Qt::RichText);
    m_driverHint->setStyleSheet("color: #888; font-size: 11px;");
    form->addRow(QString(), m_driverHint);

    auto *rightCol = new QVBoxLayout();
    rightCol->addLayout(form);

    auto *rightBtns = new QHBoxLayout();
    m_saveBtn = new QPushButton(tr("Save Changes"));
    m_testBtn = new QPushButton(tr("Test"));
    rightBtns->addWidget(m_saveBtn);
    rightBtns->addWidget(m_testBtn);
    rightBtns->addStretch();
    rightCol->addLayout(rightBtns);

    m_status = new QLabel;
    m_status->setWordWrap(true);
    m_status->setStyleSheet("color: #888; font-style: italic;");
    rightCol->addWidget(m_status);

    auto *box = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    rightCol->addWidget(box);

    root->addLayout(rightCol, 1);

    // v0.1.66 — high-level availability summary stays on the status row
    // below the action buttons; per-driver install detail lives on the
    // form's driverHint label (filled by refreshDriverHint()).
    const QStringList have = DbConnections::availableDrivers();
    QStringList missing;
    for (const QString &d : {"QSQLITE", "QPSQL", "QMYSQL", "QODBC", "DUCKDB"}) {
        if (!have.contains(d)) missing.append(d);
    }
    if (!missing.isEmpty()) {
        m_status->setText(tr("Drivers not installed on this system: %1.")
                              .arg(missing.join(", ")));
    } else {
        m_status->setText(tr("All drivers available."));
    }

    refreshList();

    connect(m_list, &QListWidget::currentRowChanged,
            this, &DbConnectionsDialog::onSelectionChanged);
    connect(m_newBtn, &QPushButton::clicked, this, &DbConnectionsDialog::onNew);
    connect(m_deleteBtn, &QPushButton::clicked, this, &DbConnectionsDialog::onDelete);
    connect(m_saveBtn, &QPushButton::clicked, this, &DbConnectionsDialog::onSave);
    connect(m_testBtn, &QPushButton::clicked, this, &DbConnectionsDialog::onTest);
    connect(box, &QDialogButtonBox::accepted, this, &DbConnectionsDialog::onAccept);
    connect(box, &QDialogButtonBox::rejected, this, &QDialog::reject);
    connect(m_driver, &QComboBox::currentTextChanged,
            this, &DbConnectionsDialog::onDriverChanged);
    connect(m_preset, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &DbConnectionsDialog::onPresetChanged);
    connect(m_browseDb, &QPushButton::clicked, this, [this]() {
        // Filter set varies per driver: SQLite shows .db files; DuckDB
        // accepts a wider mix (its own files + CSV/Parquet/JSON sources).
        const QString drv = m_driver->currentText();
        QString filter;
        if (drv == QStringLiteral("DUCKDB")) {
            filter = tr("All sources (*.duckdb *.db *.csv *.parquet *.json);;"
                       "DuckDB files (*.duckdb *.db);;"
                       "CSV (*.csv);;"
                       "Parquet (*.parquet);;"
                       "JSON (*.json *.ndjson);;"
                       "All files (*)");
        } else {
            filter = tr("SQLite databases (*.db *.sqlite *.sqlite3);;All files (*)");
        }
        const QString f = QFileDialog::getOpenFileName(
            this, tr("Select database file"), QString(), filter);
        if (!f.isEmpty()) m_database->setText(f);
    });
}

void DbConnectionsDialog::refreshList(int selectIndex) {
    m_list->clear();
    for (const auto &r : m_records) {
        QString label = r.name.isEmpty() ? tr("(unnamed)") : r.name;
        label += QStringLiteral("    [%1]").arg(r.driver);
        m_list->addItem(label);
    }
    if (selectIndex < 0 && !m_records.isEmpty()) selectIndex = 0;
    if (selectIndex >= 0 && selectIndex < m_records.size()) {
        m_list->setCurrentRow(selectIndex);
    } else {
        // No selection — clear form
        formFromRecord(DbConnections::Record{});
    }
}

void DbConnectionsDialog::onSelectionChanged() {
    int row = m_list->currentRow();
    if (row < 0 || row >= m_records.size()) return;
    formFromRecord(m_records[row]);
}

void DbConnectionsDialog::onNew() {
    DbConnections::Record r;
    r.name = QStringLiteral("New connection %1").arg(m_records.size() + 1);
    r.driver = QStringLiteral("QSQLITE");
    m_records.append(r);
    refreshList(m_records.size() - 1);
}

void DbConnectionsDialog::onDelete() {
    int row = m_list->currentRow();
    if (row < 0 || row >= m_records.size()) return;
    if (QMessageBox::question(this, tr("Delete connection?"),
            tr("Delete '%1'? This cannot be undone.").arg(m_records[row].name))
        != QMessageBox::Yes) return;
    m_records.remove(row);
    refreshList(qMin(row, m_records.size() - 1));
}

void DbConnectionsDialog::onSave() {
    int row = m_list->currentRow();
    if (row < 0 || row >= m_records.size()) return;
    m_records[row] = formToRecord();
    refreshList(row);
    m_status->setText(tr("Saved changes (not yet written to disk — click OK to commit)."));
}

void DbConnectionsDialog::onTest() {
    DbConnections::Record r = formToRecord();
    QString cname, err;
    if (DbConnections::open(r, &cname, &err)) {
        QSqlDatabase::removeDatabase(cname);
        m_status->setText(tr("✓ Connected successfully."));
        m_status->setStyleSheet("color: #2c8c2c; font-weight: 600;");
    } else {
        m_status->setText(tr("✗ %1").arg(err));
        m_status->setStyleSheet("color: #c0392b; font-weight: 600;");
    }
}

void DbConnectionsDialog::onAccept() {
    // If there's an active editing row whose changes haven't been "saved",
    // grab them so the user doesn't lose work.
    int row = m_list->currentRow();
    if (row >= 0 && row < m_records.size()) {
        m_records[row] = formToRecord();
    }

    // v0.1.70 — confirm destructive saves. If the user has deleted
    // connections (current count < original) — including the "delete
    // everything" case — show an explicit warning. This was the source
    // of silent data loss: open dialog with N connections, click Delete
    // a few times to clean up, click OK without realising the empty
    // list overwrites the file. Cancel keeps the dialog open so the
    // user can recover (add the connection back, or click Cancel to
    // abort entirely).
    if (m_records.size() < m_originalCount) {
        const int deleted = m_originalCount - m_records.size();
        const QString detail = m_records.isEmpty()
            ? tr("You are about to delete <b>all %1</b> saved database "
                 "connection%2. The saved-connections file will be wiped.")
                  .arg(m_originalCount)
                  .arg(m_originalCount == 1 ? "" : "s")
            : tr("You are about to remove <b>%1</b> saved database "
                 "connection%2 (was %3, now %4). The change writes "
                 "immediately to disk.")
                  .arg(deleted)
                  .arg(deleted == 1 ? "" : "s")
                  .arg(m_originalCount)
                  .arg(m_records.size());
        const auto reply = QMessageBox::warning(this,
            tr("Confirm deletion"),
            detail + "<br><br>" +
                tr("<b>This cannot be undone.</b> Continue?"),
            QMessageBox::Yes | QMessageBox::Cancel,
            QMessageBox::Cancel);
        if (reply != QMessageBox::Yes) {
            // Don't save, don't close. User can hit Cancel again to
            // abort entirely or undo their deletes manually.
            return;
        }
    }

    if (DbConnections::saveAll(m_records)) {
        accept();
    } else {
        QMessageBox::critical(this, tr("Save failed"),
            tr("Could not write %1").arg(DbConnections::configPath()));
    }
}

// v0.1.66 — per-driver default port. Returns 0 ("default") for SQLite /
// DuckDB which don't open TCP sockets. Used by onDriverChanged to populate
// the port field whenever the user picks a driver and the port is still
// at its zero default — saves the user from looking up that QODBC + SQL
// Server wants 1433, QPSQL wants 5432, QMYSQL wants 3306.
static int defaultPortForDriver(const QString &drv) {
    if (drv == QLatin1String("QODBC"))  return 1433; // SQL Server — most common QODBC use
    if (drv == QLatin1String("QPSQL"))  return 5432;
    if (drv == QLatin1String("QMYSQL")) return 3306;
    return 0; // QSQLITE / DUCKDB — file-based
}

void DbConnectionsDialog::onDriverChanged(const QString &drv) {
    setNetworkFieldsEnabled(DbConnections::driverNeedsNetwork(drv));
    if (drv == QStringLiteral("QSQLITE")) {
        m_browseDb->setVisible(true);
        m_database->setPlaceholderText(tr("/path/to/your.db"));
        m_options->setPlaceholderText(
            tr("Optional: QSQLITE_OPEN_URI=1, QSQLITE_BUSY_TIMEOUT=5000, etc."));
        m_host->setPlaceholderText(QString());
        m_username->setPlaceholderText(QString());
    } else if (drv == QStringLiteral("DUCKDB")) {
        // v0.1.55 — DuckDB path uses the Database field as a multi-mode
        // source: DuckDB file, in-memory, CSV, Parquet, JSON, or S3.
        m_browseDb->setVisible(true);
        m_database->setPlaceholderText(tr(
            ":memory:    or    /path/to.duckdb    or    /path/to.csv    "
            "or    /path/to.parquet    or    s3://bucket/key"));
        m_options->setPlaceholderText(tr(
            "S3 only: region;access_key_id;secret;session_token  "
            "(leave empty for non-S3)"));
        m_host->setPlaceholderText(QString());
        m_username->setPlaceholderText(QString());
    } else if (drv == QStringLiteral("QODBC")) {
        // SQL Server is the dominant QODBC use case. Surface the canonical
        // connection-string template + a hint about Windows Auth so users
        // don't have to memorise the `DRIVER={...}` incantation.
        m_browseDb->setVisible(false);
        m_database->setPlaceholderText(tr("e.g. master, NotepatraTest, tempdb"));
        m_host->setPlaceholderText(tr("127.0.0.1   (or SERVER\\INSTANCE for named instances)"));
        m_username->setPlaceholderText(tr("sa   (leave empty for Windows Auth)"));
        m_options->setPlaceholderText(tr(
            "DRIVER={ODBC Driver 18 for SQL Server};Encrypt=no   "
            "(add Trusted_Connection=yes for Windows Auth)"));
    } else if (drv == QStringLiteral("QPSQL")) {
        m_browseDb->setVisible(false);
        m_database->setPlaceholderText(tr("e.g. postgres, myapp_dev"));
        m_host->setPlaceholderText(tr("127.0.0.1"));
        m_username->setPlaceholderText(tr("postgres"));
        m_options->setPlaceholderText(tr(
            "Optional: sslmode=disable;connect_timeout=10  "
            "(use sslmode=require for production / managed PG)"));
    } else if (drv == QStringLiteral("QMYSQL")) {
        m_browseDb->setVisible(false);
        m_database->setPlaceholderText(tr("e.g. mysql, myapp_dev"));
        m_host->setPlaceholderText(tr("127.0.0.1"));
        m_username->setPlaceholderText(tr("root"));
        m_options->setPlaceholderText(tr(
            "Optional: SSL_CA=/path/to/ca.pem;MYSQL_OPT_CONNECT_TIMEOUT=10"));
    } else {
        m_browseDb->setVisible(false);
        m_database->setPlaceholderText(QString());
        m_options->setPlaceholderText(QString());
        m_host->setPlaceholderText(QString());
        m_username->setPlaceholderText(QString());
    }

    // Auto-fill the port ONLY if the user hasn't customised it (still at
    // the zero "default" value). Don't clobber an explicit non-zero port.
    const int dport = defaultPortForDriver(drv);
    if (m_port->isEnabled() && m_port->value() == 0 && dport > 0) {
        m_port->setValue(dport);
    }

    refreshDriverHint();
}

// v0.1.66 — preset applies a template + flips the driver. The "(custom)"
// entry (idx 0) is a no-op so users can return to free-form editing.
void DbConnectionsDialog::onPresetChanged(int idx) {
    if (idx <= 0) return; // (custom) — leave the form alone

    // Block signal recursion: setting m_driver below fires onDriverChanged,
    // which in turn calls refreshDriverHint(). We want that — but we don't
    // want onPresetChanged to fire again from a programmatic m_preset edit.
    QSignalBlocker bp(m_preset);

    switch (idx) {
    case 1: // SQL Server (localhost, ODBC) — generic 1433
        m_driver->setCurrentText("QODBC");
        m_host->setText("127.0.0.1");
        m_port->setValue(1433);
        m_database->setText("master");
        m_username->setText("sa");
        m_password->setText("");  // user types their own SA password
        m_options->setText("DRIVER={ODBC Driver 18 for SQL Server};Encrypt=no");
        break;
    case 2: // SQL Server — Notepatra local Docker harness on port 14330
        // Matches the bundled docker/sql-server-local.yml topology: port
        // 14330 (not 1433) so this harness coexists with any other local
        // SQL Server already on the default port. Pre-fills everything
        // EXCEPT the password — embedding a secret in a shipping preset
        // is a security smell, and v0.1.70 explicitly avoids it. The
        // password lives in docker/sql-server-local.yml's
        // MSSQL_SA_PASSWORD line; the user copies it once after they
        // spin up the harness. Pre-reqs: libqt5sql5-odbc +
        // msodbcsql18 on the host (see the in-dialog driver hint).
        m_driver->setCurrentText("QODBC");
        m_host->setText("127.0.0.1");
        m_port->setValue(14330);
        m_database->setText("NotepatraTest");
        m_username->setText("sa");
        m_password->setText("");
        m_password->setPlaceholderText(
            tr("paste from MSSQL_SA_PASSWORD in docker/sql-server-local.yml"));
        m_options->setText("DRIVER={ODBC Driver 18 for SQL Server};Encrypt=no");
        break;
    case 3: // SQL Server Express (named instance, ODBC)
        m_driver->setCurrentText("QODBC");
        m_host->setText("localhost\\SQLEXPRESS");
        m_port->setValue(0);  // Named instances use Browser service, not 1433
        m_database->setText("master");
        m_username->setText("");
        m_options->setText("DRIVER={ODBC Driver 18 for SQL Server};Trusted_Connection=yes;Encrypt=no");
        break;
    case 4: // Azure SQL Database (ODBC)
        m_driver->setCurrentText("QODBC");
        m_host->setText("yourserver.database.windows.net");
        m_port->setValue(1433);
        m_database->setText("");
        m_username->setText("admin@yourserver");
        m_options->setText(
            "DRIVER={ODBC Driver 18 for SQL Server};Encrypt=yes;TrustServerCertificate=no;Connection Timeout=30");
        break;
    case 5: // PostgreSQL (localhost)
        m_driver->setCurrentText("QPSQL");
        m_host->setText("127.0.0.1");
        m_port->setValue(5432);
        m_database->setText("postgres");
        m_username->setText("postgres");
        m_options->setText("connect_timeout=10");
        break;
    case 6: // MySQL / MariaDB (localhost)
        m_driver->setCurrentText("QMYSQL");
        m_host->setText("127.0.0.1");
        m_port->setValue(3306);
        m_database->setText("mysql");
        m_username->setText("root");
        m_options->setText("MYSQL_OPT_CONNECT_TIMEOUT=10");
        break;
    case 7: // SQLite (file on disk)
        m_driver->setCurrentText("QSQLITE");
        m_host->setText("");
        m_port->setValue(0);
        m_database->setText("");
        m_database->setFocus();  // user picks the path next
        m_username->setText("");
        m_options->setText("");
        break;
    case 8: // DuckDB (file or :memory:)
        m_driver->setCurrentText("DUCKDB");
        m_host->setText("");
        m_port->setValue(0);
        m_database->setText(":memory:");
        m_username->setText("");
        m_options->setText("");
        break;
    default:
        break;
    }

    refreshDriverHint();
}

// v0.1.66 — fills m_driverHint with driver-specific install / usage
// guidance. Two cases:
//   (1) the Qt SQL plugin for the selected driver is missing → emit a
//       per-OS install command that actually works (apt / brew / etc.).
//   (2) the plugin is present → emit a short usage note (e.g. "Use
//       127.0.0.1\\SQLEXPRESS for SQL Server Express named instances").
void DbConnectionsDialog::refreshDriverHint() {
    if (!m_driverHint) return;

    const QString drv = m_driver->currentText();
    const QStringList have = DbConnections::availableDrivers();
    const bool installed = have.contains(drv);

    if (!installed) {
        // Driver plugin missing — show actionable install commands.
        QString cmd;
        if (drv == QLatin1String("QODBC")) {
            cmd =
                "<b>SQL Server / ODBC driver is missing.</b><br>"
                "<b>Linux (Debian/Ubuntu):</b> install Qt's ODBC plugin <i>and</i> "
                "Microsoft's ODBC driver:<br>"
                "<code>sudo apt-get install libqt5sql5-odbc unixodbc-dev</code><br>"
                "<code>curl https://packages.microsoft.com/keys/microsoft.asc | sudo gpg --dearmor -o /usr/share/keyrings/microsoft-prod.gpg</code><br>"
                "<code>sudo ACCEPT_EULA=Y apt-get install msodbcsql18</code><br>"
                "<b>macOS:</b> <code>brew tap microsoft/mssql-release &amp;&amp; HOMEBREW_ACCEPT_EULA=Y brew install msodbcsql18 unixodbc</code><br>"
                "<b>Windows:</b> already bundled with Qt — restart Notepatra.";
        } else if (drv == QLatin1String("QPSQL")) {
            cmd =
                "<b>PostgreSQL driver is missing.</b><br>"
                "<b>Linux:</b> <code>sudo apt-get install libqt5sql5-psql</code><br>"
                "<b>macOS:</b> already bundled with <code>brew install qt@5</code><br>"
                "<b>Windows:</b> already bundled with Qt — restart Notepatra.";
        } else if (drv == QLatin1String("QMYSQL")) {
            cmd =
                "<b>MySQL driver is missing.</b><br>"
                "<b>Linux:</b> <code>sudo apt-get install libqt5sql5-mysql</code><br>"
                "<b>macOS:</b> already bundled with <code>brew install qt@5</code><br>"
                "<b>Windows:</b> already bundled with Qt — restart Notepatra.";
        } else if (drv == QLatin1String("DUCKDB")) {
            cmd = "<b>DuckDB is not bundled in this build.</b> Build from source with "
                  "<code>-DNOTEPATRA_WITH_DUCKDB=ON</code>, or download the full-flavor "
                  "release (v0.1.66+).";
        } else {
            cmd = tr("<b>%1 plugin is not available on this system.</b>").arg(drv);
        }
        m_driverHint->setText(cmd);
        m_driverHint->setStyleSheet("color: #b8860b; font-size: 11px;");
        return;
    }

    // Driver present — show usage hint (helps users avoid first-try blunders).
    QString hint;
    if (drv == QLatin1String("QODBC")) {
        hint = tr(
            "<b>Tip:</b> for a local SQL Server, run "
            "<code>bash scripts/sql-server-local-setup.sh</code> "
            "to spin up a Docker container + seed a sample DB. "
            "Named instances (<code>SERVER\\SQLEXPRESS</code>) use the SQL Browser "
            "service — leave Port at <i>default</i>. "
            "Windows Auth: clear Username + add <code>Trusted_Connection=yes</code> in Options.");
    } else if (drv == QLatin1String("QPSQL")) {
        hint = tr(
            "<b>Tip:</b> for managed Postgres (RDS, Cloud SQL, Neon), add "
            "<code>sslmode=require</code> in Options. For Unix-socket connections, "
            "leave Host empty and put the socket dir in Options as "
            "<code>host=/var/run/postgresql</code>.");
    } else if (drv == QLatin1String("QMYSQL")) {
        hint = tr(
            "<b>Tip:</b> for TLS-required servers (RDS, PlanetScale), add "
            "<code>SSL_CA=/path/to/ca.pem</code> in Options. "
            "For Unix sockets, set Host to an empty string and add "
            "<code>UNIX_SOCKET=/var/run/mysqld/mysqld.sock</code> in Options.");
    } else if (drv == QLatin1String("QSQLITE")) {
        hint = tr(
            "<b>Tip:</b> SQLite is file-based — no host / port / credentials. "
            "Use <b>Browse…</b> to pick the .db file. The file is created if it "
            "doesn't exist on first connect.");
    } else if (drv == QLatin1String("DUCKDB")) {
        hint = tr(
            "<b>Tip:</b> the Database field accepts <code>:memory:</code>, a .duckdb file, "
            "or a path to CSV / Parquet / JSON / S3 — DuckDB reads them all directly. "
            "For S3 sources, fill Options with "
            "<code>region;access_key_id;secret;session_token</code>.");
    }
    m_driverHint->setText(hint);
    m_driverHint->setStyleSheet("color: #2c8c2c; font-size: 11px;");
}

void DbConnectionsDialog::setNetworkFieldsEnabled(bool on) {
    m_host->setEnabled(on);
    m_port->setEnabled(on);
    m_username->setEnabled(on);
    m_password->setEnabled(on);
    m_options->setEnabled(on);
}

void DbConnectionsDialog::formFromRecord(const DbConnections::Record &r) {
    m_name->setText(r.name);
    int driverIdx = m_driver->findText(r.driver);
    m_driver->setCurrentIndex(driverIdx >= 0 ? driverIdx : 0);
    m_host->setText(r.host);
    m_port->setValue(r.port);
    m_database->setText(r.database);
    m_username->setText(r.username);
    m_password->setText(r.password);
    m_options->setText(r.options);
    setNetworkFieldsEnabled(DbConnections::driverNeedsNetwork(m_driver->currentText()));
    m_browseDb->setVisible(m_driver->currentText() == "QSQLITE");
}

DbConnections::Record DbConnectionsDialog::formToRecord() const {
    DbConnections::Record r;
    r.name = m_name->text().trimmed();
    r.driver = m_driver->currentText();
    r.host = m_host->text().trimmed();
    r.port = m_port->value();
    r.database = m_database->text().trimmed();
    r.username = m_username->text();
    r.password = m_password->text();
    r.options = m_options->text();
    return r;
}
