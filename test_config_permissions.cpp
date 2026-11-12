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
#include <QTemporaryDir>

#include <cstdio>

static int g_pass = 0, g_fail = 0;
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
#else
    std::printf("  (permission-mode checks are Unix-only; Windows ACLs untouched)\n");
#endif

    std::printf("\n=== %d passed · %d failed ===\n", g_pass, g_fail);
    return g_fail == 0 ? 0 : 1;
}
