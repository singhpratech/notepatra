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

    // Hint about driver availability
    const QStringList have = DbConnections::availableDrivers();
    QStringList missing;
    for (const QString &d : {"QSQLITE", "QPSQL", "QMYSQL", "QODBC", "DUCKDB"}) {
        if (!have.contains(d)) missing.append(d);
    }
    if (!missing.isEmpty()) {
        m_status->setText(tr("Drivers not installed on this system: %1. "
                             "Install the matching Qt SQL plugin to use them.")
                              .arg(missing.join(", ")));
    } else {
        m_status->setText(tr("All four drivers are available on this system."));
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
    if (DbConnections::saveAll(m_records)) {
        accept();
    } else {
        QMessageBox::critical(this, tr("Save failed"),
            tr("Could not write %1").arg(DbConnections::configPath()));
    }
}

void DbConnectionsDialog::onDriverChanged(const QString &drv) {
    setNetworkFieldsEnabled(DbConnections::driverNeedsNetwork(drv));
    if (drv == QStringLiteral("QSQLITE")) {
        m_browseDb->setVisible(true);
        m_database->setPlaceholderText(tr("/path/to/your.db"));
    } else if (drv == QStringLiteral("DUCKDB")) {
        // v0.1.55 — DuckDB path uses the Database field as a multi-mode
        // source: DuckDB file, in-memory, CSV, Parquet, JSON, or S3.
        // Hint via placeholder + show Browse for file paths.
        m_browseDb->setVisible(true);
        m_database->setPlaceholderText(tr(
            ":memory:    or    /path/to.duckdb    or    /path/to.csv    "
            "or    /path/to.parquet    or    s3://bucket/key"));
        // Options field repurposed for S3 creds when database starts with s3://
        m_options->setPlaceholderText(tr(
            "S3 only: region;access_key_id;secret;session_token  "
            "(leave empty for non-S3)"));
    } else {
        m_browseDb->setVisible(false);
        m_database->setPlaceholderText(QString());
        m_options->setPlaceholderText(QString());
    }
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
