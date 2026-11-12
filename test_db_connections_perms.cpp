// SPDX-License-Identifier: GPL-3.0-or-later
//
// db-connections.json holds hosts, user names and XOR-obfuscated passwords.
// It used to be written with a plain QFile and the process umask (0644 on most
// Linux setups: readable by every local user). It is now written through
// QSaveFile with owner-only permissions on Unix, and loadAll() tightens a
// looser file left by an older build. Windows ACLs are left alone, so the
// permission checks are Unix-only; the round trip runs everywhere.

#include "dbconnections.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSqlDatabase>
#include <QStringList>
#include <QTemporaryDir>
#include <cstdio>

#ifndef Q_OS_WIN
#include <sys/stat.h>
#endif

static int g_pass = 0, g_fail = 0;

static void check(const char *what, bool ok) {
    std::printf("  [%s] %s\n", ok ? "PASS" : "FAIL", what);
    ok ? ++g_pass : ++g_fail;
}

#ifndef Q_OS_WIN
static const QFileDevice::Permissions kLoose =
    QFileDevice::ReadGroup | QFileDevice::WriteGroup | QFileDevice::ExeGroup |
    QFileDevice::ReadOther | QFileDevice::WriteOther | QFileDevice::ExeOther;

static bool ownerOnly(const QString &path) {
    const QFileDevice::Permissions p = QFile::permissions(path);
    return !(p & kLoose) && (p & QFileDevice::ReadOwner) && (p & QFileDevice::WriteOwner);
}
#endif

int main(int argc, char **argv) {
    QTemporaryDir home;
    if (!home.isValid()) {
        std::printf("  [FAIL] temp HOME\n");
        return 1;
    }
    qputenv("HOME", home.path().toUtf8());
    qputenv("XDG_CONFIG_HOME", home.path().toUtf8());
    qputenv("APPDATA", home.path().toUtf8());
    qputenv("USERPROFILE", home.path().toUtf8());
#ifndef Q_OS_WIN
    ::umask(022);  // the common default that used to leave the file world-readable
#endif
    QCoreApplication app(argc, argv);

    const QString path = DbConnections::configPath();
    check("config path is inside the isolated HOME", path.startsWith(home.path()));

    DbConnections::Record r;
    r.name = QStringLiteral("perm-test");
    r.driver = QStringLiteral("QPSQL");
    r.host = QStringLiteral("db.example.invalid");
    r.port = 5432;
    r.database = QStringLiteral("app");
    r.username = QStringLiteral("reader");
    r.password = QStringLiteral("not-a-real-secret");

    std::printf("== save writes the file (owner-only on Unix) ==\n");
    check("saveAll succeeds", DbConnections::saveAll({r}));
    check("file exists", QFileInfo::exists(path));
#ifndef Q_OS_WIN
    check("new file is owner-only (0600)", ownerOnly(path));
#endif
    {
        const QStringList left = QDir(QFileInfo(path).path())
                                     .entryList(QDir::Files | QDir::Hidden | QDir::System);
        check("no temp file left beside it", left == QStringList{QFileInfo(path).fileName()});
    }

    std::printf("== load round-trips the record ==\n");
    {
        const QVector<DbConnections::Record> all = DbConnections::loadAll();
        check("one record loaded", all.size() == 1);
        check("password round-trips", !all.isEmpty() && all[0].password == r.password);
        check("host round-trips", !all.isEmpty() && all[0].host == r.host);
    }
    {
        QFile f(path);
        const bool opened = f.open(QIODevice::ReadOnly);
        const QByteArray raw = opened ? f.readAll() : QByteArray();
        check("password is not stored in plain text", opened && !raw.contains("not-a-real-secret"));
    }

#ifndef Q_OS_WIN
    std::printf("== an older, looser file is tightened ==\n");
    QFile::setPermissions(path, QFileDevice::ReadOwner | QFileDevice::WriteOwner |
                                    QFileDevice::ReadGroup | QFileDevice::ReadOther);
    check("precondition: file made world-readable", !ownerOnly(path));
    check("loadAll still reads it", DbConnections::loadAll().size() == 1);
    check("loadAll tightened it to owner-only", ownerOnly(path));

    QFile::setPermissions(path, QFileDevice::ReadOwner | QFileDevice::WriteOwner |
                                    QFileDevice::ReadGroup | QFileDevice::WriteGroup |
                                    QFileDevice::ReadOther);
    r.name = QStringLiteral("perm-test-2");
    check("saveAll over a loose file succeeds", DbConnections::saveAll({r}));
    check("saving over a loose file leaves it owner-only", ownerOnly(path));
    check("the new contents replaced the old",
          DbConnections::loadAll().value(0).name == QStringLiteral("perm-test-2"));

#else
    std::printf("  (permission checks skipped on Windows: ACLs are left alone)\n");
#endif

    // ── ODBC connection string: field values cannot add keywords ──────
    // database "x;Encrypt=no;TrustServerCertificate=yes" used to append two
    // keywords to the string open() hands SQLDriverConnect.
    std::printf("== QODBC connection string quoting ==\n");
    {
        DbConnections::Record o;
        o.driver = QStringLiteral("QODBC");
        o.options = QStringLiteral("DRIVER={ODBC Driver 18 for SQL Server};Encrypt=yes");
        o.host = QStringLiteral("sql.example.invalid");
        o.port = 1433;
        o.database = QStringLiteral("master");
        o.username = QStringLiteral("reader");
        o.password = QStringLiteral("pw");
        QString err;
        check("ordinary values are passed exactly as before",
              DbConnections::odbcConnectionString(o, &err)
                  == QStringLiteral("DRIVER={ODBC Driver 18 for SQL Server};Encrypt=yes;"
                                    "SERVER=sql.example.invalid,1433;DATABASE=master;"
                                    "UID=reader;PWD=pw;") && err.isEmpty());
        o.database = QStringLiteral("Sales Archive 2024");
        check("a database name with spaces is unchanged",
              DbConnections::odbcConnectionString(o).contains(
                  QStringLiteral(";DATABASE=Sales Archive 2024;")));

        o.database = QStringLiteral("x;Encrypt=no;TrustServerCertificate=yes");
        const QString inj = DbConnections::odbcConnectionString(o);
        check("database with ';' is one brace-quoted value",
              inj.contains(QStringLiteral(";DATABASE={x;Encrypt=no;TrustServerCertificate=yes};")));
        check("  ... and adds no TrustServerCertificate keyword of its own",
              !inj.contains(QStringLiteral(";TrustServerCertificate=yes;")));
        o.database = QStringLiteral("master");
        o.username = QStringLiteral("u};SERVER=evil.invalid;{");
        check("'}' is doubled inside the braces",
              DbConnections::odbcConnectionString(o).contains(
                  QStringLiteral(";UID={u}};SERVER=evil.invalid;{};")));
        o.username = QStringLiteral("reader");
        o.host = QStringLiteral("h;SERVER=evil.invalid");
        check("host with ';' is quoted together with its port",
              DbConnections::odbcConnectionString(o).contains(
                  QStringLiteral(";SERVER={h;SERVER=evil.invalid,1433};")));
        o.host = QStringLiteral("sql.example.invalid");
        o.password = QStringLiteral("p;w}d");
        check("a password with ';' and '}' now survives as one value",
              DbConnections::odbcConnectionString(o).endsWith(QStringLiteral(";PWD={p;w}}d};")));

        o.password = QStringLiteral("pw");
        o.database = QStringLiteral("master\nDRIVER=x");
        err.clear();
        check("a control character is refused",
              DbConnections::odbcConnectionString(o, &err).isEmpty()
                  && err.contains(QStringLiteral("database")));
        QString cname, openErr;
        const bool opened = DbConnections::open(o, &cname, &openErr);
        if (opened) QSqlDatabase::removeDatabase(cname);
        check("open() refuses it too (or the QODBC driver is absent)",
              !opened && (openErr.contains(QStringLiteral("control character"))
                          || openErr.contains(QStringLiteral("not available"))));
    }

    std::printf("\n=== Summary: %d passed, %d failed ===\n", g_pass, g_fail);
    return g_fail == 0 ? 0 : 1;
}
