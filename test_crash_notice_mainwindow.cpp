// D7 Part B — .crash_flag ownership moved to main(): MainWindow startup must
// NOT wipe the flag, must still clean legacy recovery_*.txt residue, and must
// no longer inject "[recovered]" tabs (checkCrashRecovery is deleted).
//
// Old bug (the pinning target): runStartupNow()'s else-branch — taken
// whenever the restored session had > 1 tab — silently did
// QFile::remove(recoveryDir() + "/.crash_flag") BEFORE main() could surface
// the "closed unexpectedly last time" notice, and checkCrashRecovery()'s
// early-return removed it on <= 1 tab launches too. Every crash was therefore
// invisible. After the fix the flag is written by the async-signal-safe
// handlers, read+surfaced+cleared by main() post-show, and MainWindow only
// cleans the legacy recovery_*.txt files (nothing writes those since
// pre-v0.1.96; autoSaveRecovery is gone).
//
// This test seeds a crashed-last-run config dir with a TWO-tab session —
// two tabs specifically exercises the old else-branch wipe — and proves:
//   1. .crash_flag SURVIVES MainWindow construction + startup (pre-fix FAILS),
//   2. legacy recovery_0.txt + .meta residue is cleaned (the kept cleanup),
//   3. both session tabs are restored (startup rework didn't break restore),
//   4. no "[recovered]" tab appears (the dead modal/injection path is gone).

#include "mainwindow.h"
#include "editor.h"
#include "tabmanager.h"
#include "config.h"

#include <QApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>

#include <cstdio>

static int g_pass = 0, g_fail = 0;
#define EXPECT(label, cond) \
    do { if (cond) { ++g_pass; std::printf("  [PASS] %s\n", label); } \
         else      { ++g_fail; std::printf("  [FAIL] %s\n", label); } } while (0)

static bool makeFile(const QString &path, const QByteArray &content) {
    QFile f(path);
    if (!f.open(QIODevice::WriteOnly)) return false;
    f.write(content);
    f.close();
    return true;
}

// Locate the restored tab whose filePath() ends with the given file name.
static Editor *tabForName(TabManager *tabs, const QString &name) {
    for (int i = 0; i < tabs->count(); ++i) {
        Editor *e = tabs->editorAt(i);
        if (e && QFileInfo(e->filePath()).fileName() == name) return e;
    }
    return nullptr;
}

int main(int argc, char *argv[]) {
    QTemporaryDir cfg;
    qputenv("XDG_CONFIG_HOME", cfg.path().toUtf8());
    qputenv("XDG_DATA_HOME",   cfg.path().toUtf8());
    // appConfigDir() honours XDG only on Linux. macOS reads
    // $HOME/Library/Application Support/Notepatra and Windows reads
    // %APPDATA%\Notepatra — both ignore XDG_*, so without these redirects the
    // hand-seeded crash flag + session.json below land somewhere the app never
    // looks. Redirect each platform's config root into the temp dir so the
    // seeded state is the one that gets read.
#ifdef Q_OS_MAC
    qputenv("HOME", cfg.path().toUtf8());
#endif
#ifdef Q_OS_WIN
    qputenv("APPDATA",     cfg.path().toUtf8());
    qputenv("USERPROFILE", cfg.path().toUtf8());
#endif
    qputenv("QT_QPA_PLATFORM", "offscreen");

    QApplication app(argc, argv);

    std::printf("=== test_crash_notice_mainwindow ===\n\n");
    fflush(stdout);

    // ── Seed the config dir: crashed-last-run + legacy residue ──
    const QString cfgDir = Config::appConfigDir();   // also mkpaths it
    const QString recoveryDir = cfgDir + QStringLiteral("/recovery");
    EXPECT("recovery dir created", QDir().mkpath(recoveryDir));

    const QString flagPath = recoveryDir + QStringLiteral("/.crash_flag");
    const QString legacyTxt = recoveryDir + QStringLiteral("/recovery_0.txt");
    const QString legacyMeta = recoveryDir + QStringLiteral("/recovery_0.txt.meta");
    EXPECT("seeded .crash_flag", makeFile(flagPath, "crashed"));
    EXPECT("seeded legacy recovery_0.txt",
           makeFile(legacyTxt, "stale pre-v0.1.96 residue\n"));
    EXPECT("seeded legacy recovery_0.txt.meta",
           makeFile(legacyMeta, "{\"tabName\":\"stale\"}\n"));

    // ── Two real files + a TWO-tab pristine session (two tabs exercises the
    //    old else-branch at the historical mainwindow.cpp:1511-1518 that
    //    silently wiped the flag) ──
    QTemporaryDir wd;
    const QString p1 = wd.path() + QStringLiteral("/one.txt");
    const QString p2 = wd.path() + QStringLiteral("/two.txt");
    EXPECT("created one.txt", makeFile(p1, "ONE ON DISK\n"));
    EXPECT("created two.txt", makeFile(p2, "TWO ON DISK\n"));

    auto makeTab = [](const QString &path) {
        QJsonObject t;
        t["path"] = path;
        t["tabName"] = QFileInfo(path).fileName();
        t["line"] = 0;
        t["col"] = 0;
        t["active"] = false;
        t["modified"] = false;   // pristine — content comes from disk
        return t;
    };
    QJsonArray tabs;
    tabs.append(makeTab(p1));
    tabs.append(makeTab(p2));

    QJsonObject session;
    session["tabs"] = tabs;
    session["windowW"] = 1000;
    session["windowH"] = 700;
    session["maximized"] = false;
    {
        QFile sf(cfgDir + QStringLiteral("/session.json"));
        EXPECT("wrote session.json", sf.open(QIODevice::WriteOnly));
        sf.write(QJsonDocument(session).toJson());
        sf.close();
    }

    // ── MainWindow defers restore (D1) — run the startup slot explicitly ──
    MainWindow w;
    w.runStartupNow();   // idempotent
    // A few non-blocking pumps to let queued startup work settle. Do NOT pump
    // past ~500ms total — the restoreSession .restoring-marker dialog at
    // mainwindow.cpp:~4693 is a different cluster's surface.
    for (int i = 0; i < 5; ++i)
        QApplication::processEvents();

    // ── 1. THE pinning assert: pre-fix code wiped the flag right here ──
    EXPECT(".crash_flag SURVIVES MainWindow construction",
           QFile::exists(flagPath));

    // ── 2. Legacy recovery_*.txt residue cleaned (the kept cleanup) ──
    EXPECT("legacy recovery_0.txt cleaned", !QFile::exists(legacyTxt));
    EXPECT("legacy recovery_0.txt.meta cleaned", !QFile::exists(legacyMeta));

    // ── 3. Both session tabs restored ──
    auto *tm = w.findChild<TabManager *>();
    EXPECT("TabManager found", tm != nullptr);
    if (!tm) {
        std::printf("\n%d passed, %d failed\n", g_pass, g_fail);
        fflush(stdout);
        return g_fail;
    }
    EXPECT("both session tabs restored (count >= 2)", tm->count() >= 2);
    EXPECT("one.txt tab restored", tabForName(tm, "one.txt") != nullptr);
    EXPECT("two.txt tab restored", tabForName(tm, "two.txt") != nullptr);

    // ── 4. checkCrashRecovery's tab-injection path is gone ──
    bool sawRecovered = false;
    for (int i = 0; i < tm->count(); ++i) {
        if (tm->tabText(i).contains(QStringLiteral("[recovered]")))
            sawRecovered = true;
    }
    EXPECT("no [recovered] tab appeared", !sawRecovered);

    std::printf("\n%d passed, %d failed\n", g_pass, g_fail);
    fflush(stdout);
    return g_fail;
}
