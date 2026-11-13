// test_config_permissions — config.json holds API keys in plain text, so it
// must be owner-only (0600) on Unix: written that way by Config::save(), and
// tightened on load/save when an older build left it group/world-readable.
// The save must also stay atomic (QSaveFile: no partial file, no leftovers).
// Windows ACLs are deliberately left alone, so the mode checks are Unix-only.

#include "src/config.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QStringList>
#include <QTemporaryDir>

#include <cstdio>

static int g_pass = 0, g_fail = 0;
static QStringList g_warnings;
static void captureWarnings(QtMsgType type, const QMessageLogContext &, const QString &msg) {
    if (type == QtWarningMsg) g_warnings << msg;
}
static void check(const char *what, bool ok) {
    std::printf("  [%s] %s\n", ok ? "PASS" : "FAIL", what);
    ok ? ++g_pass : ++g_fail;
}

#ifndef Q_OS_WIN
static const QFileDevice::Permissions kLoose =
    QFileDevice::ReadGroup | QFileDevice::WriteGroup | QFileDevice::ExeGroup |
    QFileDevice::ReadOther | QFileDevice::WriteOther | QFileDevice::ExeOther;
static bool ownerOnly(const QString &p) {
    const QFileDevice::Permissions perms = QFile::permissions(p);
    return !(perms & kLoose) && (perms & QFileDevice::ReadOwner)
           && (perms & QFileDevice::WriteOwner);
}
static void makeWorldReadable(const QString &p) {
    QFile::setPermissions(p, QFileDevice::ReadOwner | QFileDevice::WriteOwner |
                                 QFileDevice::ReadGroup | QFileDevice::ReadOther);
}
#endif

int main(int argc, char **argv) {
    QTemporaryDir home;
    if (!home.isValid()) return 2;
    qputenv("HOME", home.path().toUtf8());
    qputenv("XDG_CONFIG_HOME", (home.path() + "/.config").toUtf8());
#ifdef Q_OS_WIN
    qputenv("APPDATA", (home.path() + "/AppData/Roaming").toUtf8());
    qputenv("USERPROFILE", home.path().toUtf8());
#endif
    QCoreApplication app(argc, argv);
    qInstallMessageHandler(captureWarnings);
    std::printf("=== config.json permissions ===\n");

    Config &c = Config::instance();
    const QString path = Config::configPath();
    check("config path is inside the isolated home",
          QDir::cleanPath(path).startsWith(QDir::cleanPath(home.path())));

    // Built from fragments so no key-shaped literal sits in the source.
    const QString fakeKey = QStringLiteral("sk") + QStringLiteral("-test-") + QStringLiteral("0000");
    c.aiOpenAIKey = fakeKey;
    c.save();
    check("config written", QFile::exists(path));
    {
        QFile f(path);
        f.open(QIODevice::ReadOnly);
        const QJsonObject o = QJsonDocument::fromJson(f.readAll()).object();
        check("saved JSON round-trips the key", o.value("aiOpenAIKey").toString() == fakeKey);
    }
    const QStringList leftovers =
        QDir(QFileInfo(path).path()).entryList({"config.json*"}, QDir::Files);
    check("no temp file left beside config.json", leftovers == QStringList{"config.json"});

#ifndef Q_OS_WIN
    check("fresh save is owner-only (0600)", ownerOnly(path));

    makeWorldReadable(path);
    check("setup: file made world-readable (vacuity guard)", !ownerOnly(path));
    c.save();
    check("save over a world-readable file tightens it to 0600", ownerOnly(path));

    makeWorldReadable(path);
    c.load();
    check("load tightens a world-readable file to 0600", ownerOnly(path));
    check("load still reads the key", c.aiOpenAIKey == fakeKey);

    // ── A save that cannot land is reported and leaves the file intact ──
    // QSaveFile::commit()'s result used to be ignored: with a read-only
    // config directory (or config.json symlinked into one) settings silently
    // stopped persisting.
    std::printf("=== failed save ===\n");
    const QString dir = QFileInfo(path).path();
    auto readAll = [](const QString &p) {
        QFile f(p);
        return f.open(QIODevice::ReadOnly) ? f.readAll() : QByteArray("<missing>");
    };
    auto dirWritable = [](const QString &d) {
        QFile probe(d + "/.probe");
        const bool ok = probe.open(QIODevice::WriteOnly);
        if (ok) { probe.close(); probe.remove(); }
        return ok;
    };
    {
        c.save();
        const QByteArray before = readAll(path);
        QFile::setPermissions(dir, QFileDevice::ReadOwner | QFileDevice::ExeOwner);
        if (dirWritable(dir)) {
            std::printf("  (skipped: running with privileges that ignore directory modes)\n");
        } else {
            g_warnings.clear();
            c.theme = QStringLiteral("Monokai");
            c.save();
            check("read-only directory: save emits a warning",
                  g_warnings.join('\n').contains("settings not saved"));
            check("read-only directory: config.json byte-identical", readAll(path) == before);
            check("read-only directory: still owner-only", ownerOnly(path));
        }
        QFile::setPermissions(dir, QFileDevice::ReadOwner | QFileDevice::WriteOwner
                                       | QFileDevice::ExeOwner);
    }
    {
        // config.json -> <home>/ro/real.json inside a read-only directory.
        const QString ro = home.path() + "/ro";
        QDir().mkpath(ro);
        {
            QFile real(ro + "/real.json");
            real.open(QIODevice::WriteOnly);
            real.write("{\"theme\": \"Light\"}\n");
        }
        QFile::remove(path);
        const bool linked = QFile::link(ro + "/real.json", path) && QFileInfo(path).isSymLink();
        check("symlink setup: config.json is a symlink (vacuity guard)", linked);
        QFile::setPermissions(ro, QFileDevice::ReadOwner | QFileDevice::ExeOwner);
        if (dirWritable(ro)) {
            std::printf("  (skipped: running with privileges that ignore directory modes)\n");
        } else {
            g_warnings.clear();
            c.theme = QStringLiteral("Dark");
            c.save();
            check("symlink into a read-only directory: save emits a warning",
                  g_warnings.join('\n').contains("settings not saved"));
            check("symlink into a read-only directory: target unchanged",
                  readAll(ro + "/real.json") == "{\"theme\": \"Light\"}\n");
        }
        QFile::setPermissions(ro, QFileDevice::ReadOwner | QFileDevice::WriteOwner
                                      | QFileDevice::ExeOwner);
    }
#else
    std::printf("  (permission-mode checks are Unix-only; Windows ACLs untouched)\n");
#endif

    std::printf("\n=== %d passed · %d failed ===\n", g_pass, g_fail);
    return g_fail == 0 ? 0 : 1;
}
