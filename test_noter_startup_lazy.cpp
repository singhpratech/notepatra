// v0.1.133 — Noter must not touch ~/Documents at startup unless the app's own
// config says Noter is in use.
//
// Bug: MainWindow's 1.5 s startup timer probed ~/Documents/Notepatra/Noter
// and opened the reminder DB there. On macOS that probe raises the Documents
// permission prompt and freezes the GUI thread (and the MCP bridge) until the
// user answers, even for people who never opened Noter. The decision now
// comes from Config::noterInUse; only profiles older than the flag probe once.
//
// Phases (each constructs a fresh MainWindow and spins past the timer):
//   1. fresh profile, no Noter data      -> no DB opened, no Noter dir created
//   2. fresh profile, Noter DB on disk   -> still no DB opened (pre-fix: opened)
//   3. noterInUse=1, DB with overdue row -> service running, catch-up ran
//   4. noterInUse=-1 (upgrade), DB       -> service running, flag recorded 1
//   5. noterInUse=-1 (upgrade), no data  -> flag recorded 0, nothing created
// Phases 2-5 need the Documents location inside the temp HOME; where Qt
// ignores the HOME override (Windows, some macOS setups) they are skipped.

#include "mainwindow.h"
#include "config.h"
#include "notes.h"
#include "notes_todos.h"
#include "notes_reminder.h"

#include <QApplication>
#include <QDateTime>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QThread>

#include <cstdio>
#include <functional>

static int g_pass = 0, g_fail = 0;
#define EXPECT(label, cond) \
    do { if (cond) { ++g_pass; std::printf("  [PASS] %s\n", label); } \
         else      { ++g_fail; std::printf("  [FAIL] %s\n", label); } \
         fflush(stdout); } while (0)

// Spin the event loop; stays under the 3 s update-check timer.
static void spin(int ms, const std::function<bool()> &done = {}) {
    QElapsedTimer t;
    t.start();
    while (t.elapsed() < ms) {
        QApplication::processEvents(QEventLoop::AllEvents, 20);
        if (done && done()) return;
        QThread::msleep(10);
    }
}

struct Started {
    bool todos = false;
    bool engine = false;
};

static Started runStartup(int waitMs, bool stopWhenStarted) {
    auto *w = new MainWindow;
    w->show();
    spin(waitMs, [&] {
        return stopWhenStarted && w->findChild<NotesReminderEngine *>() != nullptr;
    });
    Started s;
    s.todos = w->findChild<NotesTodos *>() != nullptr;
    s.engine = w->findChild<NotesReminderEngine *>() != nullptr;
    delete w;
    QApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
    QApplication::processEvents();
    return s;
}

static int pendingOverdue(const QString &dbPath) {
    NotesTodos t(dbPath);
    if (!t.open()) return -1;
    return t.remindersReadyAt(QDateTime::currentDateTime()).size();
}

static bool seedOverdueReminder(const QString &dbPath, const QString &noteFile) {
    NotesTodos t(dbPath);
    if (!t.open()) return false;
    return !t.setNoteReminder(noteFile, QStringLiteral("Seeded"),
                              QDateTime::currentDateTime().addSecs(-3600))
                .isEmpty();
}

int main(int argc, char *argv[]) {
    QTemporaryDir home;
    const QString cfgRoot = home.path() + QStringLiteral("/cfg");
    QDir().mkpath(cfgRoot);
    qputenv("HOME", home.path().toUtf8());
    qputenv("XDG_CONFIG_HOME", cfgRoot.toUtf8());
    qputenv("XDG_DATA_HOME", (home.path() + QStringLiteral("/data")).toUtf8());
#ifdef Q_OS_WIN
    qputenv("APPDATA", cfgRoot.toUtf8());
    qputenv("USERPROFILE", home.path().toUtf8());
#endif
    qputenv("QT_QPA_PLATFORM", "offscreen");

    QApplication app(argc, argv);
    std::printf("=== test_noter_startup_lazy ===\n\n");
    fflush(stdout);

    auto &cfg = Config::instance();   // first access: loads the empty profile
    const QString noterRoot = NotesPanel::defaultNotesFolder();
    const QString dbPath = NotesPanel::todosDbPath();
    const QString docs =
        QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation);
    const bool docsIsolated =
        QDir::cleanPath(docs).startsWith(QDir::cleanPath(home.path()) + QLatin1Char('/'));

    std::printf("-- 1. fresh profile, no Noter data\n");
    EXPECT("fresh profile starts with noterInUse == 0", cfg.noterInUse == 0);
    {
        const Started s = runStartup(2200, false);
        EXPECT("no NotesTodos created at startup", !s.todos);
        EXPECT("no reminder engine started at startup", !s.engine);
        EXPECT("noterInUse still 0", cfg.noterInUse == 0);
        if (docsIsolated)
            EXPECT("Noter directory NOT created", !QFileInfo::exists(noterRoot));
    }

    if (!docsIsolated) {
        std::printf("  [SKIP] phases 2-5: Documents (%s) is outside the temp HOME\n",
                    qPrintable(QDir::toNativeSeparators(docs)));
        std::printf("\n%d passed, %d failed\n", g_pass, g_fail);
        return g_fail;
    }

    std::printf("-- 2. fresh profile, Noter DB already on disk\n");
    const QString noteFile = noterRoot + QStringLiteral("/Inbox/seeded.html");
    QDir().mkpath(QFileInfo(noteFile).path());
    {
        QFile nf(noteFile);
        EXPECT("seeded note file", nf.open(QIODevice::WriteOnly) &&
                                       nf.write("<h1>Seeded</h1>\n") > 0);
    }
    EXPECT("seeded overdue reminder", seedOverdueReminder(dbPath, noteFile));
    EXPECT("one overdue reminder pending", pendingOverdue(dbPath) == 1);
    {
        const Started s = runStartup(2200, false);
        EXPECT("DB present but flag 0: no NotesTodos opened", !s.todos);
        EXPECT("DB present but flag 0: no reminder engine", !s.engine);
        EXPECT("overdue reminder untouched", pendingOverdue(dbPath) == 1);
    }

    std::printf("-- 3. noterInUse=1 with an existing DB\n");
    cfg.noterInUse = 1;
    {
        const Started s = runStartup(2600, true);
        EXPECT("reminder engine running after startup", s.engine);
        EXPECT("NotesTodos opened after startup", s.todos);
        EXPECT("catch-up ran (overdue reminder marked fired)",
               pendingOverdue(dbPath) == 0);
    }

    std::printf("-- 4. upgraded profile (-1) with an existing DB\n");
    cfg.noterInUse = -1;
    {
        const Started s = runStartup(2600, true);
        EXPECT("upgrade probe found the DB: engine running", s.engine);
        EXPECT("upgrade probe recorded noterInUse = 1", cfg.noterInUse == 1);
    }

    std::printf("-- 5. upgraded profile (-1) without Noter data\n");
    EXPECT("removed seeded Noter tree", QDir(noterRoot).removeRecursively());
    cfg.noterInUse = -1;
    {
        const Started s = runStartup(2200, false);
        EXPECT("no Noter data: no reminder engine", !s.engine);
        EXPECT("no Noter data: no NotesTodos", !s.todos);
        EXPECT("upgrade probe recorded noterInUse = 0", cfg.noterInUse == 0);
        EXPECT("Noter directory NOT created", !QFileInfo::exists(noterRoot));
    }

    std::printf("-- 6. persistence of the flag\n");
    {
        QFile f(Config::configPath());
        QJsonObject o;
        if (f.open(QIODevice::ReadOnly))
            o = QJsonDocument::fromJson(f.readAll()).object();
        f.close();
        EXPECT("probe result persisted to config.json",
               o.contains(QStringLiteral("noterInUse")) &&
                   o.value(QStringLiteral("noterInUse")).toInt() == 0);
        o.remove(QStringLiteral("noterInUse"));
        if (f.open(QIODevice::WriteOnly | QIODevice::Truncate))
            f.write(QJsonDocument(o).toJson());
        f.close();
        cfg.load();
        EXPECT("existing config without the key loads as -1 (probe once)",
               cfg.noterInUse == -1);
    }

    std::printf("\n%d passed, %d failed\n", g_pass, g_fail);
    fflush(stdout);
    return g_fail;
}
