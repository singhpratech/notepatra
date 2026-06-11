// D5 (win-open-ghost) — pre-show + background modal hygiene. Contract: no
// QMessageBox may exec() before the main window has ever been shown (pre-fix,
// an oversized/unreadable file in the session or CLI args blocked the GUI
// thread forever behind an INVISIBLE modal — the "ghost process" symptom),
// and the fileChanged watcher must coalesce/debounce/defer its prompts.
// Failure trigger is the cross-platform large-file gate (fileMemoryLimitMb=1
// + a 2 MB file) — no POSIX permission injection, runs on all 3 platforms.
// Modal DRIVES are winOffscreenModalUnsafe()-guarded (win-noter-segfault);
// the watcher-prompt sections (§4/§5) are additionally skipped under
// offscreen macOS, where FSEvents-backed prompts wedged the modal loop.
// Fully offline.

#include "mainwindow.h"
#include "editor.h"
#include "tabmanager.h"
#include "config.h"

#include <QApplication>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMessageBox>
#include <QPushButton>
#include <QTemporaryDir>
#include <QTimer>

#include <cstdio>
#include <cstdlib>

static int g_pass = 0, g_fail = 0;
#define EXPECT(label, cond) \
    do { if (cond) { ++g_pass; std::printf("  [PASS] %s\n", label); } \
         else      { ++g_fail; std::printf("  [FAIL] %s\n", label); } } while (0)

static bool winOffscreenModalUnsafe() {
#if defined(Q_OS_WIN)
    return QGuiApplication::platformName()
               .compare(QLatin1String("offscreen"), Qt::CaseInsensitive) == 0;
#else
    return false;
#endif
}

// FSEvents/kqueue-backed watcher prompts wedge modal drives under offscreen
// macOS (v0.1.114 second tag run hung 2 h in §4/§5); Windows shares the
// offscreen QMessageBox class. Watcher behaviour stays covered on Linux.
static bool offscreenWatcherModalUnsafe() {
#if defined(Q_OS_WIN) || defined(Q_OS_MAC)
    return QGuiApplication::platformName()
               .compare(QLatin1String("offscreen"), Qt::CaseInsensitive) == 0;
#else
    return false;
#endif
}

static void pumpFor(int ms) {
    QElapsedTimer t;
    t.start();
    while (t.elapsed() < ms)
        QApplication::processEvents(QEventLoop::AllEvents, 10);
}

static QList<QMessageBox *> visibleMessageBoxes() {
    QList<QMessageBox *> out;
    for (QWidget *w : QApplication::topLevelWidgets())
        if (auto *mb = qobject_cast<QMessageBox *>(w))
            if (mb->isVisible()) out.append(mb);
    return out;
}

static Editor *tabForName(TabManager *tabs, const QString &name) {
    for (int i = 0; i < tabs->count(); ++i) {
        Editor *e = tabs->editorAt(i);
        if (e && QFileInfo(e->filePath()).fileName() == name) return e;
    }
    return nullptr;
}

static void writeBytes(const QString &path, const QByteArray &bytes) {
    QFile f(path);
    f.open(QIODevice::WriteOnly);
    f.write(bytes);
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

    // Unbuffered stdout: section headers survive a hard crash or a watchdog
    // kill, so ctest --output-on-failure pinpoints the failing section (the
    // v0.1.114 second tag run hung here for 2 h with zero output).
    setvbuf(stdout, nullptr, _IONBF, 0);

    QApplication app(argc, argv);

    // Hard watchdog: every section is elapsed-bounded, so the only way past
    // ~240 s is a wedged nested modal loop. Exit red fast instead of hanging
    // CI until the 6-hour job kill.
    QTimer::singleShot(240000, []() {
        std::printf("[WATCHDOG] test exceeded 240 s — aborting\n");
        std::_Exit(3);
    });

    std::printf("=== test_preshow_modal_hygiene ===\n\n");

    // 1 MB memory limit (direct member write skips the JSON-load clamp), so a
    // 2 MB file trips the large-file gate without any multi-GB fixture.
    Config::instance().fileMemoryLimitMb = 1;

    QTemporaryDir wd;
    const QString big1 = wd.path() + "/oversized_one.txt";
    const QString big2 = wd.path() + "/oversized_two.txt";
    {
        QByteArray two(2 * 1024 * 1024, 'a');
        writeBytes(big1, two);
        writeBytes(big2, two);
    }

    const QString sessionPath = Config::appConfigDir() + "/session.json";

    // ── §1 — pre-show open of an oversized file: no modal, no hang ──
    std::printf("Section 1 — pre-show oversized open declines, no modal\n");
    {
        QElapsedTimer t;
        t.start();
        MainWindow mw;  // never shown
        mw.openFile(big1);
        const qint64 elapsed = t.elapsed();
        std::printf("  pre-show openFile(oversized) took %lld ms\n",
                    static_cast<long long>(elapsed));
        EXPECT("no invisible-modal hang (under 5000 ms)", elapsed < 5000);
        EXPECT("no QMessageBox pre-show", visibleMessageBoxes().isEmpty());
        auto *tm = mw.findChild<TabManager *>();
        EXPECT("oversized file was NOT opened",
               tm && tabForName(tm, "oversized_one.txt") == nullptr);

        if (winOffscreenModalUnsafe()) {
            std::printf("  [SKIP] flush-after-show drive — offscreen-Windows "
                        "QMessageBox class (win-noter-segfault)\n");
        } else {
            mw.show();
            pumpFor(150);
            const auto boxes = visibleMessageBoxes();
            EXPECT("exactly ONE combined notice after show",
                   boxes.size() == 1);
            if (boxes.size() == 1) {
                EXPECT("notice is non-modal", !boxes[0]->isModal());
                EXPECT("notice names the file",
                       boxes[0]->text().contains("oversized_one.txt"));
                boxes[0]->close();
                pumpFor(50);
            }
        }
    }

    // §1b — combined surface: two failures, one dialog
    std::printf("\nSection 1b — two pre-show failures combine into one notice\n");
    if (winOffscreenModalUnsafe()) {
        std::printf("  [SKIP] combined-notice drive (offscreen-Windows)\n");
    } else {
        MainWindow mw;
        mw.openFile(big1);
        mw.openFile(big2);
        EXPECT("still no QMessageBox pre-show", visibleMessageBoxes().isEmpty());
        mw.show();
        pumpFor(150);
        const auto boxes = visibleMessageBoxes();
        EXPECT("exactly ONE combined notice", boxes.size() == 1);
        if (boxes.size() == 1) {
            EXPECT("notice names BOTH files",
                   boxes[0]->text().contains("oversized_one.txt") &&
                   boxes[0]->text().contains("oversized_two.txt"));
            boxes[0]->close();
            pumpFor(50);
        }
    }

    // ── §2 — session restore takes the conservative branch ──
    std::printf("\nSection 2 — session restore declines oversized, keeps buffers\n");
    {
        auto makeTab = [](const QString &path, bool modified,
                          const QString &unsaved) {
            QJsonObject t;
            t["path"] = path;
            t["tabName"] = QFileInfo(path).fileName();
            t["line"] = 0;
            t["col"] = 0;
            t["active"] = false;
            t["modified"] = modified;
            if (modified) t["unsavedContent"] = unsaved;
            return t;
        };
        QJsonArray tabs;
        tabs.append(makeTab(big1, false, QString()));
        tabs.append(makeTab(big2, true, "KEEP ME"));
        QJsonObject session;
        session["tabs"] = tabs;
        writeBytes(sessionPath, QJsonDocument(session).toJson());

        QElapsedTimer t;
        t.start();
        MainWindow mw;  // never shown
        mw.runStartupNow();
        const qint64 elapsed = t.elapsed();
        std::printf("  pre-show restore took %lld ms\n",
                    static_cast<long long>(elapsed));
        EXPECT("restore completed without modal hang (under 10000 ms)",
               elapsed < 10000);
        EXPECT("no QMessageBox pre-show", visibleMessageBoxes().isEmpty());
        auto *tm = mw.findChild<TabManager *>();
        EXPECT("no tab bound to the oversized pristine file",
               tm && tabForName(tm, "oversized_one.txt") == nullptr);
        bool keptBuffer = false;
        for (int i = 0; tm && i < tm->count(); ++i) {
            Editor *e = tm->editorAt(i);
            if (e && e->text() == "KEEP ME") keptBuffer = true;
        }
        EXPECT("modified tab's unsaved buffer preserved (no data loss)",
               keptBuffer);
    }
    QFile::remove(sessionPath);

    // ── §3 — interrupted-restore marker routes through the notice queue ──
    std::printf("\nSection 3 — interrupted-restore notice is queued, not exec'd\n");
    {
        QJsonArray tabs;
        QJsonObject t0;
        t0["path"] = big1;
        t0["tabName"] = "oversized_one.txt";
        t0["modified"] = false;
        tabs.append(t0);
        QJsonObject session;
        session["tabs"] = tabs;
        writeBytes(sessionPath, QJsonDocument(session).toJson());
        writeBytes(sessionPath + ".restoring", "1748293");  // legacy stage

        MainWindow mw;  // never shown
        mw.runStartupNow();
        pumpFor(1200);  // would cover the old singleShot(800)+exec()
        EXPECT("no QMessageBox while window never shown",
               visibleMessageBoxes().isEmpty());
        const QStringList asides = QDir(Config::appConfigDir())
            .entryList({"session.json.failed-*"}, QDir::Files);
        EXPECT("session.json moved aside", asides.size() == 1);

        if (winOffscreenModalUnsafe()) {
            std::printf("  [SKIP] post-show flush drive (offscreen-Windows)\n");
        } else {
            mw.show();
            pumpFor(150);
            const auto boxes = visibleMessageBoxes();
            EXPECT("notice flushed after show", boxes.size() == 1);
            if (boxes.size() == 1) {
                EXPECT("notice mentions the session",
                       boxes[0]->text().contains("session"));
                boxes[0]->close();
                pumpFor(50);
            }
        }
        for (const QString &a : asides)
            QFile::remove(Config::appConfigDir() + "/" + a);
    }
    QFile::remove(sessionPath);

    // Restore a sane limit so watcher reloads in §4/§5 don't trip the gate.
    Config::instance().fileMemoryLimitMb = 2048;

    // ── §4 — watcher coalesce + debounce ──
    std::printf("\nSection 4 — watcher prompts coalesce and debounce\n");
    int promptCount = 0;
    QTimer clicker;
    clicker.setInterval(100);
    QObject::connect(&clicker, &QTimer::timeout, [&promptCount]() {
        for (QWidget *w : QApplication::topLevelWidgets()) {
            auto *mb = qobject_cast<QMessageBox *>(w);
            if (!mb || !mb->isVisible()) continue;
            if (mb->windowTitle() == QLatin1String("File Changed"))
                ++promptCount;
            else
                std::printf("  [CLICKER] unexpected box: \"%s\"\n",
                            qPrintable(mb->windowTitle()));
            // Answer EVERY box — an unanswered unexpected modal wedges the
            // pump loop forever (red-state lesson: clickers answer
            // unexpected boxes).
            if (auto *no = mb->button(QMessageBox::No)) no->click();
            else mb->close();
        }
    });

    if (offscreenWatcherModalUnsafe()) {
        std::printf("  [SKIP] §4 watcher modal drives (offscreen Windows/macOS)\n");
    } else {
        const QString watched = wd.path() + "/watched.txt";
        writeBytes(watched, "original content\n");
        MainWindow mw;
        mw.show();
        pumpFor(100);
        mw.openFile(watched);
        pumpFor(100);
        auto *tm = mw.findChild<TabManager *>();
        Editor *e = tabForName(tm, "watched.txt");
        EXPECT("watched file open", e != nullptr);

        clicker.start();
        QElapsedTimer burst;
        burst.start();
        int writes = 0;
        while (writes < 5) {
            if (burst.elapsed() >= (writes + 1) * 80) {
                writeBytes(watched,
                           QString("rewrite %1\n").arg(writes).toUtf8());
                ++writes;
            }
            QApplication::processEvents(QEventLoop::AllEvents, 10);
        }
        pumpFor(1200);
        std::printf("  prompts during 5-write burst: %d\n", promptCount);
        EXPECT("burst of 5 external writes produced 1-2 prompts (not 5)",
               promptCount >= 1 && promptCount <= 2);
        EXPECT("'No' honored — buffer not reloaded",
               e && e->text() == "original content\n");

        pumpFor(1700);  // clear the debounce window
        const int before = promptCount;
        writeBytes(watched, "final rewrite\n");
        pumpFor(1200);
        EXPECT("watcher still armed after debounce (+1 prompt)",
               promptCount == before + 1);
        clicker.stop();
    }

    // ── §5 — never-shown / minimized deferral ──
    std::printf("\nSection 5 — deferral until show / un-minimize\n");
    if (offscreenWatcherModalUnsafe()) {
        std::printf("  [SKIP] §5 watcher modal drives (offscreen Windows/macOS)\n");
    } else {
        const QString watched2 = wd.path() + "/watched2.txt";
        writeBytes(watched2, "v1\n");
        MainWindow mw2;  // never shown
        mw2.openFile(watched2);
        pumpFor(100);
        promptCount = 0;
        clicker.start();
        writeBytes(watched2, "v2\n");
        pumpFor(500);
        EXPECT("no prompt while window never shown", promptCount == 0);

        mw2.show();
        pumpFor(600);
        EXPECT("exactly one prompt drained by first show", promptCount == 1);

        mw2.showMinimized();
        pumpFor(100);
        writeBytes(watched2, "v3\n");
        pumpFor(500);
        EXPECT("no prompt while minimized", promptCount == 1);

        pumpFor(1200);  // let the §4-style debounce window expire pre-restore
        mw2.showNormal();
        pumpFor(600);
        EXPECT("prompt drained by un-minimize", promptCount == 2);
        clicker.stop();
    }

    std::printf("\n%d passed, %d failed\n", g_pass, g_fail);
    return g_fail == 0 ? 0 : 1;
}
