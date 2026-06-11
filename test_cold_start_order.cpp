// D1 (win-open-ghost) — cold-start ordering contract. The window must be
// constructible + visible BEFORE session restore / CLI opens run; the deferred
// startup slot (runStartupNow) then restores, opens CLI files, drains queued
// remote opens, and clears the marker. A kill during the deferred opens must
// not blame (rename aside) a healthy session.json — the marker is stage-aware.

#include "mainwindow.h"
#include "editor.h"
#include "tabmanager.h"
#include "config.h"
#include "statusbar.h"

#include <QApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QTemporaryDir>

#include <cstdio>

static int g_pass = 0, g_fail = 0;
#define EXPECT(label, cond) \
    do { if (cond) { ++g_pass; std::printf("  [PASS] %s\n", label); } \
         else      { ++g_fail; std::printf("  [FAIL] %s\n", label); } } while (0)

static Editor *tabForName(TabManager *tabs, const QString &name) {
    for (int i = 0; i < tabs->count(); ++i) {
        Editor *e = tabs->editorAt(i);
        if (e && QFileInfo(e->filePath()).fileName() == name) return e;
    }
    return nullptr;
}

static int tabIndexForName(TabManager *tabs, const QString &name) {
    for (int i = 0; i < tabs->count(); ++i) {
        Editor *e = tabs->editorAt(i);
        if (e && QFileInfo(e->filePath()).fileName() == name) return i;
    }
    return -1;
}

static bool anyLabelContains(QWidget *root, const QString &needle) {
    const auto labels = root->findChildren<QLabel *>();
    for (QLabel *l : labels)
        if (l->text().contains(needle)) return true;
    return false;
}

static QString g_cfgDir;

static QString sessionPath()  { return g_cfgDir + "/session.json"; }
static QString markerPath()   { return g_cfgDir + "/session.json.restoring"; }

static void seedSession(const QString &filePath) {
    QJsonObject t;
    t["path"] = filePath;
    t["tabName"] = QFileInfo(filePath).fileName();
    t["line"] = 0;
    t["col"] = 0;
    t["active"] = true;
    t["modified"] = false;
    QJsonArray tabs;
    tabs.append(t);
    QJsonObject session;
    session["tabs"] = tabs;
    session["windowW"] = 1000;
    session["windowH"] = 700;
    session["maximized"] = false;
    QFile sf(sessionPath());
    sf.open(QIODevice::WriteOnly);
    sf.write(QJsonDocument(session).toJson());
}

static void cleanSessionArtifacts() {
    QFile::remove(sessionPath());
    QFile::remove(markerPath());
    QDir d(g_cfgDir);
    for (const QString &f : d.entryList({"session.json.failed-*"}, QDir::Files))
        QFile::remove(g_cfgDir + "/" + f);
}

static int failedAsideCount() {
    return QDir(g_cfgDir).entryList({"session.json.failed-*"}, QDir::Files).size();
}

int main(int argc, char *argv[]) {
    QTemporaryDir cfg;
    qputenv("XDG_CONFIG_HOME", cfg.path().toUtf8());
    qputenv("XDG_DATA_HOME",   cfg.path().toUtf8());
#ifdef Q_OS_MAC
    qputenv("HOME", cfg.path().toUtf8());
#endif
#ifdef Q_OS_WIN
    qputenv("APPDATA",     cfg.path().toUtf8());
    qputenv("USERPROFILE", cfg.path().toUtf8());
#endif
    qputenv("QT_QPA_PLATFORM", "offscreen");

    QApplication app(argc, argv);

    std::printf("=== test_cold_start_order ===\n\n");

    g_cfgDir = Config::appConfigDir();

    QTemporaryDir wd;
    const QString sessTxt = wd.path() + "/sess.txt";
    const QString cliTxt  = wd.path() + "/cli.txt";
    const QString cli2Txt = wd.path() + "/cli2.txt";
    const QString remTxt  = wd.path() + "/remote.txt";
    for (const QString &p : {sessTxt, cliTxt, cli2Txt, remTxt}) {
        QFile f(p);
        f.open(QIODevice::WriteOnly);
        f.write("line one\nline two\nline three\nline four\n");
    }

    // ── Phase A — ordering + CLI current-tab contract ──
    std::printf("Phase A — restore deferred until runStartupNow\n");
    seedSession(sessTxt);
    {
        MainWindow mw;
        auto *tm = mw.findChild<TabManager *>();
        EXPECT("TabManager found", tm != nullptr);
        if (!tm) { std::printf("\n%d passed, %d failed\n", g_pass, g_fail); return 1; }

        EXPECT("ctor did NOT restore the session", tabForName(tm, "sess.txt") == nullptr);
        mw.show();
        EXPECT("window visible before restore", mw.isVisible());
        EXPECT("still no restored tab after show()", tabForName(tm, "sess.txt") == nullptr);
        EXPECT("statusbar shows 'Restoring session'", anyLabelContains(&mw, "Restoring session"));

        mw.setStartupActions({cliTxt}, 3);
        mw.runStartupNow();

        EXPECT("session tab restored", tabForName(tm, "sess.txt") != nullptr);
        Editor *cli = tabForName(tm, "cli.txt");
        EXPECT("CLI file opened", cli != nullptr);
        EXPECT("CLI file is the CURRENT tab",
               tm->currentEditor() && tm->currentEditor() == cli);
        if (cli) {
            int line = -1, col = -1;
            cli->getCursorPosition(&line, &col);
            EXPECT("gotoLine(3) landed on 0-based line 2", line == 2);
        }
        EXPECT("'Restoring session' message cleared",
               !anyLabelContains(&mw, "Restoring session"));
        EXPECT("marker absent after startup", !QFileInfo::exists(markerPath()));

        const int count = tm->count();
        mw.runStartupNow();
        EXPECT("runStartupNow is idempotent", tm->count() == count);
    }
    cleanSessionArtifacts();

    // ── Phase A2 — multi-file CLI: --line targets the FIRST file ──
    // With one file the first-file re-resolution is vacuously green —
    // openFile leaves the LAST opened tab active, so two files is the
    // smallest case that exercises the branch.
    std::printf("\nPhase A2 — multi-file CLI, --line hits the first file\n");
    {
        MainWindow mw;
        auto *tm = mw.findChild<TabManager *>();
        mw.show();
        mw.setStartupActions({cliTxt, cli2Txt}, 3);
        mw.runStartupNow();

        Editor *first = tabForName(tm, "cli.txt");
        EXPECT("A2: first CLI file opened", first != nullptr);
        EXPECT("A2: second CLI file opened",
               tabForName(tm, "cli2.txt") != nullptr);
        EXPECT("A2: FIRST file is the CURRENT tab",
               tm->currentEditor() && tm->currentEditor() == first);
        if (first) {
            int line = -1, col = -1;
            first->getCursorPosition(&line, &col);
            EXPECT("A2: gotoLine(3) landed in the FIRST file (0-based 2)",
                   line == 2);
        }
    }
    cleanSessionArtifacts();

    // ── Phase B — remote opens queue behind the deferred restore ──
    std::printf("\nPhase B — remote opens queued until startup completes\n");
    seedSession(sessTxt);
    {
        MainWindow mw;
        auto *tm = mw.findChild<TabManager *>();
        mw.show();
        mw.handleRemoteOpen({remTxt}, -1);
        EXPECT("remote open queued (not opened pre-startup)",
               tabForName(tm, "remote.txt") == nullptr);
        EXPECT("window still visible", mw.isVisible());

        mw.setStartupActions({cli2Txt}, -1);
        mw.runStartupNow();

        // D2 — queued remote opens flush one event-loop turn after startup
        // so a paint lands between the restored session and the loads.
        EXPECT("remote open still deferred at runStartupNow return",
               tabForName(tm, "remote.txt") == nullptr);
        QElapsedTimer pump;
        pump.start();
        while (pump.elapsed() < 200 && tabForName(tm, "remote.txt") == nullptr)
            QApplication::processEvents(QEventLoop::AllEvents, 10);

        const int iSess = tabIndexForName(tm, "sess.txt");
        const int iCli  = tabIndexForName(tm, "cli2.txt");
        const int iRem  = tabIndexForName(tm, "remote.txt");
        EXPECT("session tab present", iSess >= 0);
        EXPECT("CLI tab present", iCli >= 0);
        EXPECT("remote tab present", iRem >= 0);
        EXPECT("order: session < cli < remote", iSess < iCli && iCli < iRem);
        EXPECT("remote file is the CURRENT tab",
               tm->currentEditor() && tm->currentEditor() == tabForName(tm, "remote.txt"));
        EXPECT("marker absent after startup", !QFileInfo::exists(markerPath()));
    }
    cleanSessionArtifacts();

    // ── Phase C — close before startup must not clobber session.json ──
    std::printf("\nPhase C — early close keeps session.json intact\n");
    seedSession(sessTxt);
    QByteArray snapshot;
    {
        QFile sf(sessionPath());
        sf.open(QIODevice::ReadOnly);
        snapshot = sf.readAll();
    }
    {
        MainWindow mw;
        mw.close();  // fires closeEvent -> saveSession, which must early-return
    }
    {
        QFile sf(sessionPath());
        sf.open(QIODevice::ReadOnly);
        EXPECT("session.json unchanged by pre-startup close",
               sf.readAll() == snapshot);
    }
    cleanSessionArtifacts();

    // ── Phase D — "cliopen" marker preserves the session ──
    std::printf("\nPhase D — cliopen-stage marker does not blame the session\n");
    seedSession(sessTxt);
    {
        QFile m(markerPath());
        m.open(QIODevice::WriteOnly);
        m.write("cliopen");
    }
    {
        MainWindow mw;
        auto *tm = mw.findChild<TabManager *>();
        mw.runStartupNow();
        EXPECT("session restored despite cliopen marker",
               tabForName(tm, "sess.txt") != nullptr);
        EXPECT("session.json still exists", QFileInfo::exists(sessionPath()));
        EXPECT("no session.json.failed-* created", failedAsideCount() == 0);
        EXPECT("marker removed", !QFileInfo::exists(markerPath()));
    }
    cleanSessionArtifacts();

    // ── Phase E — legacy/restore-stage marker still bails out ──
    std::printf("\nPhase E — restore-stage marker moves session aside\n");
    seedSession(sessTxt);
    {
        QFile m(markerPath());
        m.open(QIODevice::WriteOnly);
        m.write("1748293");  // legacy timestamp form
    }
    {
        MainWindow mw;
        auto *tm = mw.findChild<TabManager *>();
        mw.runStartupNow();
        EXPECT("session NOT restored after interrupted-restore marker",
               tabForName(tm, "sess.txt") == nullptr);
        EXPECT("session.json moved aside", !QFileInfo::exists(sessionPath()));
        EXPECT("exactly one session.json.failed-* exists", failedAsideCount() == 1);
        // No event pumping here: the deferred 800 ms dialog timer never fires
        // and dies with the window at scope end.
    }
    cleanSessionArtifacts();

    // ── Phase F — perf ceiling: ctor+show stay fast with a huge session ──
    std::printf("\nPhase F — construct+show ceiling with a 100k-line session\n");
    {
        QString big;
        big.reserve(2 * 1000 * 1000 + 200000);
        for (int i = 0; i < 100000; ++i)
            big += QStringLiteral("select col_%1 from t;\n").arg(i);
        QJsonObject t;
        t["path"] = sessTxt;
        t["tabName"] = "sess.txt";
        t["line"] = 0;
        t["col"] = 0;
        t["active"] = true;
        t["modified"] = true;
        t["unsavedContent"] = big;
        QJsonArray tabs;
        tabs.append(t);
        QJsonObject session;
        session["tabs"] = tabs;
        QFile sf(sessionPath());
        sf.open(QIODevice::WriteOnly);
        sf.write(QJsonDocument(session).toJson());
    }
    {
        QElapsedTimer timer;
        timer.start();
        MainWindow mw;
        mw.show();
        const qint64 elapsed = timer.elapsed();
        std::printf("  construct+show took %lld ms\n",
                    static_cast<long long>(elapsed));
        EXPECT("construct+show under 3000 ms (restore deferred)", elapsed < 3000);
        // Destroyed without running startup — pending 0 ms timer dies with it.
    }
    cleanSessionArtifacts();

    std::printf("\n%d passed, %d failed\n", g_pass, g_fail);
    return g_fail == 0 ? 0 : 1;
}
