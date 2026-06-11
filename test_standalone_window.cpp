// D6 (single-instance hardening) — standalone fallback window contract.
//
// When a secondary connects to a primary that never ACKs (hung GUI thread),
// main() opens a real window with MainWindow(true): it must show ONLY the
// requested files, never restore the saved session, never write session.json
// (10 s autosave or closeEvent), never create/trip the session.json.restoring
// marker machinery, and never destroy the primary's crash evidence
// (.crash_flag / recovery_*). --new shares the same constructor path.
//
// Proven here: with a seeded session.json S (one file-backed alpha.txt tab +
// one modified untitled tab carrying "SECRET-UNSAVED") and seeded recovery
// artifacts, a MainWindow(true) restores NOTHING, leaves session.json
// byte-identical to S through startup, openFile() and close(), creates no
// .restoring / .failed-* siblings, preserves the recovery dir — and still
// opens explicitly requested files normally.
//
// The default-path control (restore DOES run with the default ctor) is
// covered by test_restore_session_unreadable — no second MainWindow needed.
// No modal can appear: the interrupted-restore notice only shows after the
// window is first shown, and this test never calls show().

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
#include <QMessageBox>
#include <QTemporaryDir>
#include <QTimer>

#include <cstdio>

static int g_pass = 0, g_fail = 0;
#define EXPECT(label, cond) \
    do { if (cond) { ++g_pass; std::printf("  [PASS] %s\n", label); } \
         else      { ++g_fail; std::printf("  [FAIL] %s\n", label); } \
         fflush(stdout); } while (0)

// Locate the tab whose filePath() ends with the given file name.
static Editor *tabForName(TabManager *tabs, const QString &name) {
    for (int i = 0; i < tabs->count(); ++i) {
        Editor *e = tabs->editorAt(i);
        if (e && QFileInfo(e->filePath()).fileName() == name) return e;
    }
    return nullptr;
}

// True if ANY tab's buffer contains the needle (catches the unsaved-content
// overlay landing in an untitled tab, where no path is associated).
static bool anyTabContains(TabManager *tabs, const QString &needle) {
    for (int i = 0; i < tabs->count(); ++i) {
        Editor *e = tabs->editorAt(i);
        if (e && e->text().contains(needle)) return true;
    }
    return false;
}

static QByteArray readAllBytes(const QString &path) {
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) return QByteArray();
    return f.readAll();
}

// QMessageBox modal drives SIGSEGV in Qt's offscreen QPA on Windows
// (win-noter-segfault) — skip modal-driven sections there, never constructify.
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

// Click a SEQUENCE of buttons, one per successive visible QMessageBox —
// drives multi-prompt flows (closeEvent loops over several modified tabs).
static void scheduleModalClickSeq(QList<QMessageBox::StandardButton> seq,
                                  int triesLeft = 120) {
    if (seq.isEmpty()) return;
    QTimer::singleShot(50, [seq, triesLeft]() mutable {
        for (QWidget *tw : QApplication::topLevelWidgets()) {
            if (auto *mb = qobject_cast<QMessageBox *>(tw)) {
                if (mb->isVisible()) {
                    if (auto *b = mb->button(seq.first())) {
                        seq.removeFirst();
                        b->click();
                        if (!seq.isEmpty()) scheduleModalClickSeq(seq, 120);
                        return;
                    }
                }
            }
        }
        if (triesLeft > 0) scheduleModalClickSeq(seq, triesLeft - 1);
    });
}

static bool writeAllBytes(const QString &path, const QByteArray &bytes) {
    QFile f(path);
    if (!f.open(QIODevice::WriteOnly)) return false;
    f.write(bytes);
    return true;
}

int main(int argc, char *argv[]) {
    QTemporaryDir cfg;
    qputenv("XDG_CONFIG_HOME", cfg.path().toUtf8());
    qputenv("XDG_DATA_HOME",   cfg.path().toUtf8());
    // appConfigDir() honours XDG only on Linux. macOS reads
    // $HOME/Library/Application Support/Notepatra and Windows reads
    // %APPDATA%\Notepatra — both ignore XDG_*, so without these redirects the
    // hand-seeded session.json below lands somewhere the window never looks
    // and the "restore skipped" assertions would pass vacuously. Redirect each
    // platform's config root into the temp dir so the seeded session is the
    // one at stake.
#ifdef Q_OS_MAC
    qputenv("HOME", cfg.path().toUtf8());
#endif
#ifdef Q_OS_WIN
    qputenv("APPDATA",     cfg.path().toUtf8());
    qputenv("USERPROFILE", cfg.path().toUtf8());
#endif
    qputenv("QT_QPA_PLATFORM", "offscreen");

    QApplication app(argc, argv);

    std::printf("=== test_standalone_window ===\n\n");
    fflush(stdout);

    // ── Workspace files ──
    QTemporaryDir wd;
    const QString alphaPath = wd.path() + "/alpha.txt";
    const QString betaPath  = wd.path() + "/beta.txt";
    {
        QFile a(alphaPath); a.open(QIODevice::WriteOnly);
        a.write("ALPHA DISK CONTENT\n");
        QFile b(betaPath); b.open(QIODevice::WriteOnly);
        b.write("BETA DISK CONTENT\n");
    }

    // ── Seed session.json S: one file-backed tab + one modified untitled ──
    // This exact shape DOES restore both tabs under the default ctor (the
    // control proven by test_restore_session_unreadable), so "nothing
    // restored" below is a real assertion, not a vacuous one.
    const QString cfgDir = Config::appConfigDir();
    const QString sessionPath = cfgDir + "/session.json";
    {
        QJsonObject tFile;                     // pristine file-backed tab
        tFile["path"] = alphaPath;
        tFile["tabName"] = QStringLiteral("alpha.txt");
        tFile["line"] = 0;
        tFile["col"] = 0;
        tFile["active"] = true;
        tFile["modified"] = false;

        QJsonObject tUntitled;                 // modified untitled tab
        tUntitled["path"] = QString();
        tUntitled["tabName"] = QStringLiteral("new 1");
        tUntitled["line"] = 0;
        tUntitled["col"] = 0;
        tUntitled["active"] = false;
        tUntitled["modified"] = true;
        tUntitled["unsavedContent"] = QStringLiteral("SECRET-UNSAVED");

        QJsonArray tabs;
        tabs.append(tFile);
        tabs.append(tUntitled);

        QJsonObject session;
        session["tabs"] = tabs;
        session["windowW"] = 1000;
        session["windowH"] = 700;
        session["maximized"] = false;

        EXPECT("wrote session.json",
               writeAllBytes(sessionPath, QJsonDocument(session).toJson()));
    }
    // Byte-exact snapshot S taken from disk BEFORE constructing the window.
    const QByteArray S = readAllBytes(sessionPath);
    EXPECT("captured non-empty pre-construction snapshot of session.json",
           !S.isEmpty());

    // ── Seed crash evidence the standalone window must never destroy ──
    const QString recDir = cfgDir + "/recovery";
    QDir().mkpath(recDir);
    const QString crashFlag = recDir + "/.crash_flag";
    const QString recFile   = recDir + "/recovery_zzz.txt";
    EXPECT("seeded recovery/.crash_flag", writeAllBytes(crashFlag, "1"));
    EXPECT("seeded recovery/recovery_zzz.txt",
           writeAllBytes(recFile, "orphaned recovery payload\n"));

    // ── Standalone window: construct, run deferred startup, pump ──
    MainWindow w(true);
    w.runStartupNow();   // idempotent; the ctor's queued singleShot is harmless
    for (int i = 0; i < 5; ++i)
        QApplication::processEvents();

    auto *tm = w.findChild<TabManager *>();
    EXPECT("TabManager found", tm != nullptr);
    if (!tm) {
        std::printf("\n%d passed, %d failed\n", g_pass, g_fail);
        fflush(stdout);
        return g_fail;
    }

    // Restore must have been skipped entirely.
    EXPECT("no tab is path-associated with alpha.txt (restore skipped)",
           tabForName(tm, "alpha.txt") == nullptr);
    EXPECT("no tab carries the session's unsaved content SECRET-UNSAVED",
           !anyTabContains(tm, QStringLiteral("SECRET-UNSAVED")));

    // The primary's session and marker machinery must be untouched.
    EXPECT("session.json bytes identical to S after startup",
           readAllBytes(sessionPath) == S);
    EXPECT("session.json.restoring was never created",
           !QFileInfo::exists(sessionPath + ".restoring"));
    EXPECT("no session.json.failed-* sibling created",
           QDir(cfgDir).entryList({"session.json.failed-*"},
                                  QDir::Files).isEmpty());

    // Crash evidence preserved (the ctor-time recovery wipe must be gated).
    EXPECT(".crash_flag still exists after startup",
           QFileInfo::exists(crashFlag));
    EXPECT("recovery_zzz.txt still exists after startup",
           QFileInfo::exists(recFile));

    // ── Standalone still opens explicitly requested files normally ──
    w.openFile(betaPath);
    QApplication::processEvents();
    EXPECT("tab for beta.txt exists after openFile()",
           tabForName(tm, "beta.txt") != nullptr);

    // ── Standalone close PROMPTS for unsaved buffers (no session backing) ──
    // saveSession() no-ops in this mode, so the prompt-less close contract
    // would silently discard edits; closeEvent must route modified tabs
    // through Save / Discard / Cancel instead.
    if (!winOffscreenModalUnsafe()) {
        Editor *beta = tabForName(tm, "beta.txt");
        EXPECT("beta tab present for unsaved-close test", beta != nullptr);
        if (beta) {
            beta->insertAt(QStringLiteral("EDITED-BETA-UNSAVED\n"), 0, 0);
            EXPECT("beta tab is modified after edit", beta->isModified());

            scheduleModalClick(QMessageBox::Cancel);
            const bool closedA = w.close();
            QApplication::processEvents();
            EXPECT("Cancel keeps the window open (close() == false)", !closedA);
            EXPECT("Cancel keeps the unsaved tab alive",
                   tabForName(tm, "beta.txt") != nullptr);
            EXPECT("Cancel keeps the unsaved content",
                   anyTabContains(tm, QStringLiteral("EDITED-BETA-UNSAVED")));

            // Save — the most likely answer — must write the file, close
            // the tab, let the window close, and never touch session.json.
            scheduleModalClick(QMessageBox::Save);
            const bool closedB = w.close();
            QApplication::processEvents();
            EXPECT("Save lets the window close (close() == true)", closedB);
            EXPECT("Save removed the now-clean tab",
                   tabForName(tm, "beta.txt") == nullptr);
            EXPECT("Save wrote the edit to disk",
                   readAllBytes(betaPath)
                       .startsWith("EDITED-BETA-UNSAVED"));
            EXPECT("session.json bytes still identical to S after Save-close",
                   readAllBytes(sessionPath) == S);

            // Multi-tab close: prompts run highest-index-first. Discard the
            // second file, then Cancel the first — the sweep must stop,
            // keeping the window open and the cancelled tab intact.
            w.openFile(alphaPath);
            w.openFile(betaPath);
            QApplication::processEvents();
            Editor *ea = tabForName(tm, "alpha.txt");
            Editor *eb = tabForName(tm, "beta.txt");
            EXPECT("multi-tab close: both tabs open", ea && eb);
            if (ea && eb) {
                ea->insertAt(QStringLiteral("EDITED-ALPHA-KEEP\n"), 0, 0);
                eb->insertAt(QStringLiteral("EDITED-BETA-DROP\n"), 0, 0);
                scheduleModalClickSeq({QMessageBox::Discard,
                                       QMessageBox::Cancel});
                const bool closedC = w.close();
                QApplication::processEvents();
                EXPECT("Discard-then-Cancel keeps the window open", !closedC);
                EXPECT("the discarded (beta) tab is gone",
                       tabForName(tm, "beta.txt") == nullptr);
                EXPECT("the cancelled (alpha) tab survives with its edit",
                       anyTabContains(tm,
                                      QStringLiteral("EDITED-ALPHA-KEEP")));

                // Cleanup: discard the survivor so the final close below is
                // prompt-less again.
                scheduleModalClick(QMessageBox::Discard);
                const bool closedD = w.close();
                QApplication::processEvents();
                EXPECT("final Discard lets the window close", closedD);
            }
        }
    } else {
        std::printf("  [SKIP] modal-driven unsaved-close section "
                    "(offscreen Windows)\n");
    }

    // ── close() drives closeEvent: saveSession + recovery wipe must no-op ──
    w.close();
    QApplication::processEvents();
    EXPECT("session.json bytes STILL identical to S after close()",
           readAllBytes(sessionPath) == S);
    EXPECT(".crash_flag STILL exists after close()",
           QFileInfo::exists(crashFlag));
    EXPECT("recovery_zzz.txt STILL exists after close()",
           QFileInfo::exists(recFile));

    std::printf("\n%d passed, %d failed\n", g_pass, g_fail);
    fflush(stdout);
    return g_fail;
}
