// D8 — session-autosave guard + .restoring clone-race detection.
//
// PART A (autosave cost): the old saveSession() extracted EVERY unsaved
// buffer (text() → QString → JSON escape) and rewrote session.json on every
// 5 s tick, even when nothing changed — an idle app with an 8 MB modified
// buffer paid ~16 MB of copies + a full disk write per tick. New contract:
// saveSession() skips ALL work when no editor's sessionTextDirty flag is set
// AND the metadata fingerprint (paths/tabNames/cursor/active/modified/
// geometry) is unchanged AND session.json exists; real writes go through
// session.json.tmp + rename (atomic). The sentinel trick below is a
// deterministic no-rewrite detector immune to mtime granularity: we replace
// session.json with literal junk, run 20 ticks, and the junk must survive.
//
// PART B (clone race): the .restoring marker now carries "<pid> <ms>". A
// marker owned by a LIVE foreign pid younger than 10 minutes means another
// instance is mid-restore → skip restore entirely, leave session.json AND
// the marker untouched, create no .failed-* aside. Dead pid / legacy
// single-token marker / live-but-older-than-10-min → the existing stale
// path (session.json renamed aside to session.json.failed-<ms>, marker
// removed, restore skipped).
//
// No modal is ever DRIVEN here (test_notes_panels.cpp:431 pattern — the
// winOffscreenModalUnsafe() guard is NOT needed): the stale path queues a
// NON-modal notice that only displays after the window is shown, these
// windows are never shown, and each one is deleted promptly after its
// asserts, so no dialog can ever appear. Fully offline.

#include "mainwindow.h"
#include "editor.h"
#include "tabmanager.h"
#include "config.h"

#include <QApplication>
#include <QDateTime>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QMessageBox>
#include <QProcess>
#include <QTemporaryDir>
#include <QTimer>

#include <cstdio>
#include <string>

static int g_pass = 0, g_fail = 0;
#define EXPECT(label, cond) \
    do { if (cond) { ++g_pass; std::printf("  [PASS] %s\n", label); } \
         else      { ++g_fail; std::printf("  [FAIL] %s\n", label); } \
         std::fflush(stdout); } while (0)

static bool writeFile(const QString &path, const QByteArray &bytes) {
    QFile f(path);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate)) return false;
    const bool ok = f.write(bytes) == bytes.size();
    f.close();
    return ok;
}

static QByteArray readFile(const QString &path) {
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) return QByteArray();
    return f.readAll();
}

// session.json.failed-<ms> siblings in the config dir.
static QStringList failedSiblings(const QString &cfgDir) {
    return QDir(cfgDir).entryList(
        QStringList() << QStringLiteral("session.json.failed-*"), QDir::Files);
}

static bool anyTabContains(TabManager *tabs, const QString &token) {
    for (int i = 0; i < tabs->count(); ++i) {
        Editor *e = tabs->editorAt(i);
        if (e && e->text().contains(token)) return true;
    }
    return false;
}

// QMessageBox modal drives SIGSEGV in Qt's offscreen QPA on Windows
// (win-noter-segfault) — skip modal-driven sections there.
static bool winOffscreenModalUnsafe() {
#if defined(Q_OS_WIN)
    return QGuiApplication::platformName()
               .compare(QLatin1String("offscreen"), Qt::CaseInsensitive) == 0;
#else
    return false;
#endif
}

// Click `which` on the first visible QMessageBox; re-arms until one shows.
static void scheduleModalClick(QMessageBox::StandardButton which,
                               int triesLeft = 60) {
    QTimer::singleShot(50, [which, triesLeft]() {
        for (QWidget *tw : QApplication::topLevelWidgets()) {
            if (auto *mb = qobject_cast<QMessageBox *>(tw)) {
                if (mb->isVisible()) {
                    if (auto *b = mb->button(which)) { b->click(); return; }
                }
            }
        }
        if (triesLeft > 0) scheduleModalClick(which, triesLeft - 1);
    });
}

// Shared stale-path outcome (cases B2/B3/B4): session.json renamed aside to
// exactly one session.json.failed-* preserving the unsaved content, marker
// removed, nothing restored. Seeds state, constructs a MainWindow, drives
// the deferred startup, asserts, deletes the window IMMEDIATELY (never
// shown, never pumped — the queued non-modal stale notice dies with it).
static void runStaleMarkerCase(const char *caseName, const QString &cfgDir,
                               const QByteArray &markerBytes,
                               const QByteArray &seedJson) {
    const QString sessionPath = cfgDir + QStringLiteral("/session.json");
    const QString markerPath  = sessionPath + QStringLiteral(".restoring");

    // Clean slate, then seed session.json + marker BEFORE construction.
    QFile::remove(sessionPath);
    QFile::remove(markerPath);
    for (const QString &f : failedSiblings(cfgDir))
        QFile::remove(cfgDir + QLatin1Char('/') + f);
    writeFile(sessionPath, seedJson);
    writeFile(markerPath, markerBytes);

    MainWindow *mw = new MainWindow();
    mw->runStartupNow();
    QApplication::processEvents();

    const std::string base(caseName);
    const std::string lGone = base + ": session.json renamed aside (gone)";
    EXPECT(lGone.c_str(), !QFileInfo::exists(sessionPath));

    const QStringList failed = failedSiblings(cfgDir);
    const std::string lOne = base + ": exactly one session.json.failed-* aside";
    EXPECT(lOne.c_str(), failed.size() == 1);

    bool preserved = false;
    if (failed.size() == 1)
        preserved = readFile(cfgDir + QLatin1Char('/') + failed.first())
                        .contains("D8_RACE_TOKEN");
    const std::string lTok = base + ": aside preserves the unsaved content";
    EXPECT(lTok.c_str(), preserved);

    const std::string lMark = base + ": marker removed";
    EXPECT(lMark.c_str(), !QFileInfo::exists(markerPath));

    auto *tm = mw->findChild<TabManager *>();
    const std::string lTab = base + ": restore skipped (no tab holds token)";
    EXPECT(lTab.c_str(), tm != nullptr && !anyTabContains(tm, "D8_RACE_TOKEN"));

    delete mw;  // never close() — closeEvent would re-fire saveSession

    for (const QString &f : failedSiblings(cfgDir))
        QFile::remove(cfgDir + QLatin1Char('/') + f);
}

int main(int argc, char *argv[]) {
    QTemporaryDir cfg;
    qputenv("XDG_CONFIG_HOME", cfg.path().toUtf8());
    qputenv("XDG_DATA_HOME",   cfg.path().toUtf8());
    // appConfigDir() honours XDG only on Linux. macOS reads
    // $HOME/Library/Application Support/Notepatra and Windows reads
    // %APPDATA%\Notepatra — both ignore XDG_*, so without these redirects the
    // seeded session.json/config.json would land somewhere the app never
    // looks. Redirect each platform's config root into the temp dir.
#ifdef Q_OS_MAC
    qputenv("HOME", cfg.path().toUtf8());
#endif
#ifdef Q_OS_WIN
    qputenv("APPDATA",     cfg.path().toUtf8());
    qputenv("USERPROFILE", cfg.path().toUtf8());
#endif
    qputenv("QT_QPA_PLATFORM", "offscreen");

    QApplication app(argc, argv);

    std::printf("=== test_session_autosave_guard ===\n\n");

    // Config seeded BEFORE the first Config::instance() use (MainWindow ctor):
    // no Welcome tab, and a 300 s autosave interval so the timer never fires
    // mid-test — every saveSession() below is an explicit call.
    const QString cfgDir = Config::appConfigDir();  // static; mkpaths the dir
    EXPECT("wrote config.json (welcome off, autosave 300 s)",
           writeFile(cfgDir + QStringLiteral("/config.json"),
                     "{\"showWelcomeOnStartup\": false, "
                     "\"autoSaveIntervalSec\": 300}\n"));

    const QString sessionPath = cfgDir + QStringLiteral("/session.json");
    const QString markerPath  = sessionPath + QStringLiteral(".restoring");

    // ════════ PART A — autosave change-detection + atomic write ════════
    std::printf("\nPart A — autosave change-detection + atomic write\n");

    MainWindow *mw = new MainWindow();
    mw->runStartupNow();  // no session.json yet → restore is a no-op
    QApplication::processEvents();

    auto *tm = mw->findChild<TabManager *>();
    EXPECT("TabManager found", tm != nullptr);
    Editor *ed = tm ? tm->editorAt(0) : nullptr;
    EXPECT("editorAt(0) is an Editor (welcome suppressed)", ed != nullptr);
    if (!ed) {
        std::printf("\n%d passed, %d failed\n", g_pass, g_fail);
        return g_fail;
    }

    // (1) ~8 MB buffer carrying a unique token. textChanged fires → the
    // editor's sessionTextDirty flag is set, so the first save serializes.
    const QString big = QStringLiteral("D8_BIG_TOKEN\n")
        + QStringLiteral("lorem ipsum dolor sit amet\n").repeated(310000);
    ed->setText(big);

    // (2) Baseline write works and carries the buffer.
    mw->saveSession();
    EXPECT("baseline: session.json written", QFileInfo::exists(sessionPath));
    EXPECT("baseline: serialized buffer contains the token",
           readFile(sessionPath).contains("D8_BIG_TOKEN"));

    // (3) Sentinel trick: replace session.json with junk. Nothing in memory
    // changed (flags cleared by the baseline write, fingerprint identical,
    // file exists), so subsequent saves must skip and leave the junk alone.
    EXPECT("sentinel A planted", writeFile(sessionPath, "SENTINEL_A"));

    // (4) PERF CEILING — the load-bearing pin. Broken code does 20 ×
    // (8 MB text() extraction + JSON escape + 8 MB+ disk write) — multiple
    // seconds. Fixed code does 20 × small-metadata fingerprint compares —
    // single-digit ms. Correctness asserts alone pass the broken code, so
    // this ceiling IS the contract (QElapsedTimer rule).
    {
        QElapsedTimer t;
        t.start();
        for (int i = 0; i < 20; ++i) mw->saveSession();
        const qint64 elapsed = t.elapsed();
        std::printf("  20 idle saveSession() ticks took %lld ms\n",
                    static_cast<long long>(elapsed));
        std::fflush(stdout);
        EXPECT("PERF: 20 unchanged ticks under 500 ms "
               "(broken path re-serializes 8 MB per tick)",
               elapsed < 500);
    }

    // (5) Zero rewrites while unchanged — the sentinel must be byte-intact.
    EXPECT("unchanged ticks never rewrote the file (sentinel intact)",
           readFile(sessionPath) == QByteArray("SENTINEL_A"));

    // (6) Data-safety net intact: one real edit must force a full rewrite.
    ed->insertAt(QStringLiteral("x"), 0, 0);  // fires textChanged → dirty
    mw->saveSession();
    {
        QJsonParseError perr;
        const QJsonDocument doc =
            QJsonDocument::fromJson(readFile(sessionPath), &perr);
        EXPECT("text edit forces a rewrite (valid JSON again)",
               perr.error == QJsonParseError::NoError && doc.isObject());
        const QJsonArray tabs = doc.object().value("tabs").toArray();
        const QString uc = tabs.at(0).toObject()
                               .value("unsavedContent").toString();
        EXPECT("tabs[0].unsavedContent starts with the inserted 'x'",
               uc.startsWith(QLatin1Char('x')));
        EXPECT("tabs[0].unsavedContent still carries the token",
               uc.contains(QStringLiteral("D8_BIG_TOKEN")));
    }

    // (7) Metadata catch: a cursor-only change (no text dirty flag) must
    // still force a rewrite via the fingerprint.
    EXPECT("sentinel B planted", writeFile(sessionPath, "SENTINEL_B"));
    ed->setCursorPosition(5, 0);
    mw->saveSession();
    {
        QJsonParseError perr;
        const QJsonDocument doc =
            QJsonDocument::fromJson(readFile(sessionPath), &perr);
        EXPECT("cursor-only change forces a rewrite (valid JSON)",
               perr.error == QJsonParseError::NoError && doc.isObject());
        const QJsonArray tabs = doc.object().value("tabs").toArray();
        EXPECT("tabs[0].line == 5 after the cursor move",
               tabs.at(0).toObject().value("line").toInt(-1) == 5);
    }

    // (8) Atomicity smoke: the .tmp staging file must never be left behind.
    EXPECT("no session.json.tmp orphan after the writes",
           !QFileInfo::exists(sessionPath + QStringLiteral(".tmp")));

    delete mw;  // never close() — closeEvent would re-fire saveSession
    mw = nullptr;
    QFile::remove(sessionPath);
    QFile::remove(markerPath);

    // ════════ PART B — .restoring marker: live-PID clone-race ════════
    std::printf("\nPart B — .restoring marker clone-race\n");

    // Common seed: one untitled modified tab carrying the race token.
    const QByteArray seedJson = QByteArrayLiteral(
        "{\"tabs\":[{\"path\":\"\",\"tabName\":\"race\",\"modified\":true,"
        "\"unsavedContent\":\"D8_RACE_TOKEN\",\"line\":0,\"col\":0,"
        "\"active\":true}],"
        "\"windowW\":900,\"windowH\":600,\"maximized\":false}");

    // ── B1 LIVE PID (concurrent clone) — restore skipped, NOTHING touched ──
    // The implementation deliberately ignores the test's OWN pid (PID-reuse
    // guard: ownerPid != applicationPid()), so applicationPid() would take
    // the wrong branch. Use a long-lived CHILD process as the live foreign
    // owner instead.
    {
        std::printf("\n  B1 — live foreign pid, fresh stamp\n");
        QProcess sleeper;
#ifdef Q_OS_WIN
        sleeper.start(QStringLiteral("ping"),
                      {QStringLiteral("-n"), QStringLiteral("30"),
                       QStringLiteral("127.0.0.1")});
#else
        sleeper.start(QStringLiteral("sleep"), {QStringLiteral("30")});
#endif
        EXPECT("B1: sleeper child started (live foreign pid)",
               sleeper.waitForStarted(5000));
        const qint64 livePid = sleeper.processId();
        EXPECT("B1: sleeper pid > 0", livePid > 0);

        const QByteArray liveMarker =
            QByteArray::number(livePid) + ' '
            + QByteArray::number(QDateTime::currentMSecsSinceEpoch());
        writeFile(sessionPath, seedJson);
        writeFile(markerPath, liveMarker);

        MainWindow *raceMw = new MainWindow();
        raceMw->runStartupNow();
        QApplication::processEvents();

        EXPECT("B1: session.json still exists",
               QFileInfo::exists(sessionPath));
        EXPECT("B1: session.json bytes unchanged",
               readFile(sessionPath) == seedJson);
        EXPECT("B1: no session.json.failed-* aside created",
               failedSiblings(cfgDir).isEmpty());
        EXPECT("B1: marker still exists (belongs to the live owner)",
               QFileInfo::exists(markerPath));
        EXPECT("B1: marker content unchanged",
               readFile(markerPath) == liveMarker);
        auto *tmB = raceMw->findChild<TabManager *>();
        EXPECT("B1: restore skipped — no tab holds the race token",
               tmB != nullptr && !anyTabContains(tmB, "D8_RACE_TOKEN"));

        // Write-gate: the skipping instance must stay session-PASSIVE, not
        // just restore-passive. Without the saveSession() gate the next
        // autosave tick (or this closeEvent) clobbered the very session the
        // live owner was still restoring.
        raceMw->close();   // closeEvent -> saveSession()
        QApplication::processEvents();
        EXPECT("B1: saveSession after live-skip did NOT clobber session.json",
               readFile(sessionPath) == seedJson);
        EXPECT("B1: marker still intact after close",
               readFile(markerPath) == liveMarker);

        // The skipping instance is session-passive for LIFE, so closeEvent
        // must prompt per modified buffer — the old prompt-less close
        // discarded them silently (no session write ever happens here).
        if (!winOffscreenModalUnsafe()) {
            Editor *ed0 = tmB ? tmB->editorAt(0) : nullptr;
            EXPECT("B1: untitled tab present for close-prompt test",
                   ed0 != nullptr);
            if (ed0) {
                ed0->insertAt(QStringLiteral("B1-UNSAVED-EDIT\n"), 0, 0);
                EXPECT("B1: tab modified after edit", ed0->isModified());

                scheduleModalClick(QMessageBox::Cancel);
                const bool closedA = raceMw->close();
                QApplication::processEvents();
                EXPECT("B1: Cancel keeps the skip window open", !closedA);
                EXPECT("B1: Cancel keeps the unsaved content",
                       anyTabContains(tmB, "B1-UNSAVED-EDIT"));

                scheduleModalClick(QMessageBox::Discard);
                const bool closedB = raceMw->close();
                QApplication::processEvents();
                EXPECT("B1: Discard lets the skip window close", closedB);
                EXPECT("B1: session.json STILL unchanged after prompted close",
                       readFile(sessionPath) == seedJson);
                EXPECT("B1: marker STILL intact after prompted close",
                       readFile(markerPath) == liveMarker);
            }
        } else {
            std::printf("  [SKIP] B1 close-prompt section "
                        "(offscreen Windows)\n");
        }

        delete raceMw;
        sleeper.kill();
        sleeper.waitForFinished(5000);
        QFile::remove(markerPath);
        QFile::remove(sessionPath);
    }

    // ── B2 DEAD PID → stale path ──
    {
        std::printf("\n  B2 — dead pid, fresh stamp\n");
        QProcess dead;
#ifdef Q_OS_WIN
        dead.start(QStringLiteral("cmd"),
                   {QStringLiteral("/c"), QStringLiteral("exit"),
                    QStringLiteral("0")});
#else
        dead.start(QStringLiteral("true"), QStringList());
#endif
        EXPECT("B2: dead-pid child started", dead.waitForStarted(5000));
        // Capture the pid BEFORE waitForFinished — QProcess clears its
        // cached pid once the child is reaped and returns 0 afterwards.
        const qint64 deadPid = dead.processId();
        EXPECT("B2: captured child pid > 0", deadPid > 0);
        EXPECT("B2: child exited (pid now dead)", dead.waitForFinished(5000));
        // PID recycling between the reap and the assert is a millisecond
        // window — accepted (spec risk 7).

        runStaleMarkerCase(
            "B2 dead pid", cfgDir,
            QByteArray::number(deadPid) + ' '
                + QByteArray::number(QDateTime::currentMSecsSinceEpoch()),
            seedJson);
    }

    // ── B3 LEGACY single-token marker "<ms>" → stale path (back compat) ──
    {
        std::printf("\n  B3 — legacy timestamp-only marker\n");
        runStaleMarkerCase(
            "B3 legacy marker", cfgDir,
            QByteArray::number(QDateTime::currentMSecsSinceEpoch()),
            seedJson);
    }

    // ── B4 LIVE-BUT-OLD — pins the 10-minute PID-reuse bound ──
    // A live owner whose stamp is older than kRestoringMarkerLiveWindowMs is
    // treated as PID reuse after a crash → stale path, exactly like B2.
    {
        std::printf("\n  B4 — live pid, stamp 11 minutes old\n");
        QProcess sleeper;
#ifdef Q_OS_WIN
        sleeper.start(QStringLiteral("ping"),
                      {QStringLiteral("-n"), QStringLiteral("30"),
                       QStringLiteral("127.0.0.1")});
#else
        sleeper.start(QStringLiteral("sleep"), {QStringLiteral("30")});
#endif
        EXPECT("B4: sleeper child started (live foreign pid)",
               sleeper.waitForStarted(5000));
        const qint64 livePid = sleeper.processId();
        EXPECT("B4: sleeper pid > 0", livePid > 0);

        const qint64 oldStamp = QDateTime::currentMSecsSinceEpoch()
                                - 11 * 60 * 1000;  // just past the 10-min bound
        runStaleMarkerCase(
            "B4 live-but-old", cfgDir,
            QByteArray::number(livePid) + ' ' + QByteArray::number(oldStamp),
            seedJson);

        sleeper.kill();
        sleeper.waitForFinished(5000);
    }

    std::printf("\n%d passed, %d failed\n", g_pass, g_fail);
    std::fflush(stdout);
    return g_fail;
}
