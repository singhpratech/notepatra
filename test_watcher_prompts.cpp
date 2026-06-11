// Watcher prompt branches (re-verify fix wave on win-open-ghost-fixes).
// The fileChanged path was rewritten (global one-at-a-time gate, deferred
// drain, post-modal re-resolution) with no test driving its branches:
//   §1 File Changed → Yes actually reloads, watcher stays armed after.
//   §2 File Deleted → Yes keeps the tab; No removes it.
//   §3 Two DIFFERENT paths changing while one prompt is open: the second
//      defers (never stacks) and still surfaces afterwards (never drops).
//   §4 closeTab's Save/Discard/Cancel modal is covered by the same gate:
//      deleting the watched file mid-prompt must NOT stack a File Deleted
//      box that deletes the editor closeTab holds (use-after-free repro).
// Skipped entirely under offscreen Windows (win-noter-segfault QMessageBox
// class) AND offscreen macOS (FSEvents watcher prompts wedge modal drives).
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
#include <QMessageBox>
#include <QPushButton>
#include <QTemporaryDir>
#include <QTimer>

#include <cstdio>

static int g_pass = 0, g_fail = 0;
#define EXPECT(label, cond) \
    do { if (cond) { ++g_pass; std::printf("  [PASS] %s\n", label); } \
         else      { ++g_fail; std::printf("  [FAIL] %s\n", label); } \
         fflush(stdout); } while (0)

// Windows: the offscreen QMessageBox class (win-noter-segfault). macOS:
// FSEvents-backed watcher prompts wedged a sibling test's modal loop for 2 h
// (v0.1.114 second tag run) — same skip, behaviour stays covered on Linux.
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

static bool writeBytes(const QString &path, const QByteArray &bytes) {
    QFile f(path);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate)) return false;
    f.write(bytes);
    return true;
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

    std::printf("=== test_watcher_prompts ===\n\n");
    fflush(stdout);

    if (offscreenWatcherModalUnsafe()) {
        std::printf("  [SKIP] entire test drives watcher modals "
                    "(offscreen Windows/macOS)\n");
        std::printf("\n%d passed, %d failed\n", g_pass, g_fail);
        return 0;
    }

    QTemporaryDir wd;

    // ── §1 — File Changed → Yes reloads; watcher stays armed after ──
    std::printf("Section 1 — Yes reloads from disk\n");
    {
        const QString watched = wd.path() + "/reload.txt";
        writeBytes(watched, "v1 original\n");
        MainWindow mw;
        mw.show();
        pumpFor(100);  // deferred startup completes
        mw.openFile(watched);
        pumpFor(100);
        auto *tm = mw.findChild<TabManager *>();
        Editor *e = tabForName(tm, "reload.txt");
        EXPECT("watched file open", e != nullptr);

        int yesClicks = 0, noClicks = 0;
        QTimer clicker;
        clicker.setInterval(50);
        QObject::connect(&clicker, &QTimer::timeout,
                         [&yesClicks, &noClicks]() {
            for (QMessageBox *mb : visibleMessageBoxes()) {
                if (mb->windowTitle() != QLatin1String("File Changed"))
                    continue;
                // First prompt: Yes (reload). Second prompt: No (keep).
                if (yesClicks == 0) {
                    if (auto *b = mb->button(QMessageBox::Yes)) {
                        ++yesClicks;
                        b->click();
                    }
                } else if (auto *b = mb->button(QMessageBox::No)) {
                    ++noClicks;
                    b->click();
                }
            }
        });
        clicker.start();

        writeBytes(watched, "v2 rewritten\n");
        pumpFor(1500);
        EXPECT("Yes prompt fired once", yesClicks == 1);
        EXPECT("Yes reloaded the buffer from disk",
               e && e->text() == "v2 rewritten\n");
        EXPECT("reloaded buffer is not modified", e && !e->isModified());

        // Watcher must still be armed for the next external change.
        pumpFor(1700);  // clear the per-path debounce window
        writeBytes(watched, "v3 again\n");
        pumpFor(1500);
        EXPECT("watcher re-armed — second prompt fired", noClicks >= 1);
        EXPECT("No keeps the v2 buffer", e && e->text() == "v2 rewritten\n");
        clicker.stop();
    }

    // ── §2 — File Deleted → Yes keeps the tab; No removes it ──
    std::printf("\nSection 2 — File Deleted branches\n");
    {
        const QString keepF = wd.path() + "/keepme.txt";
        const QString dropF = wd.path() + "/dropme.txt";
        writeBytes(keepF, "keep content\n");
        writeBytes(dropF, "drop content\n");
        MainWindow mw;
        mw.show();
        pumpFor(100);
        mw.openFile(keepF);
        mw.openFile(dropF);
        pumpFor(100);
        auto *tm = mw.findChild<TabManager *>();
        EXPECT("both files open", tabForName(tm, "keepme.txt") &&
                                  tabForName(tm, "dropme.txt"));

        int delPrompts = 0;
        QTimer clicker;
        clicker.setInterval(50);
        QObject::connect(&clicker, &QTimer::timeout, [&delPrompts]() {
            for (QMessageBox *mb : visibleMessageBoxes()) {
                if (mb->windowTitle() != QLatin1String("File Deleted"))
                    continue;
                ++delPrompts;
                // First deletion: Yes (keep). Second: No (remove tab).
                const auto which =
                    delPrompts == 1 ? QMessageBox::Yes : QMessageBox::No;
                if (auto *b = mb->button(which)) b->click();
            }
        });
        clicker.start();

        QFile::remove(keepF);
        pumpFor(1500);
        EXPECT("File Deleted prompt fired for keepme", delPrompts == 1);
        EXPECT("Yes keeps the tab",
               tabForName(tm, "keepme.txt") != nullptr);
        EXPECT("Yes keeps the buffer content",
               tabForName(tm, "keepme.txt") &&
               tabForName(tm, "keepme.txt")->text() == "keep content\n");

        QFile::remove(dropF);
        pumpFor(1500);
        EXPECT("File Deleted prompt fired for dropme", delPrompts == 2);
        EXPECT("No removes the tab",
               tabForName(tm, "dropme.txt") == nullptr);
        EXPECT("the kept tab is untouched by the second prompt",
               tabForName(tm, "keepme.txt") != nullptr);
        clicker.stop();
    }

    // ── §3 — two paths, one prompt at a time, none dropped ──
    std::printf("\nSection 3 — cross-path deferral, no stacking, no drops\n");
    {
        const QString fA = wd.path() + "/two_a.txt";
        const QString fB = wd.path() + "/two_b.txt";
        writeBytes(fA, "A v1\n");
        writeBytes(fB, "B v1\n");
        MainWindow mw;
        mw.show();
        pumpFor(100);
        mw.openFile(fA);
        mw.openFile(fB);
        pumpFor(100);
        auto *tm = mw.findChild<TabManager *>();
        EXPECT("both files open", tabForName(tm, "two_a.txt") &&
                                  tabForName(tm, "two_b.txt"));

        int prompts = 0, maxVisible = 0;
        QSet<QString> promptedFor;
        QElapsedTimer holdOpen;
        QTimer clicker;
        clicker.setInterval(50);
        QObject::connect(&clicker, &QTimer::timeout,
                         [&prompts, &maxVisible, &promptedFor, &holdOpen]() {
            const auto boxes = visibleMessageBoxes();
            maxVisible = qMax(maxVisible, int(boxes.size()));
            for (QMessageBox *mb : boxes) {
                if (mb->windowTitle() != QLatin1String("File Changed"))
                    continue;
                if (!holdOpen.isValid()) holdOpen.start();
                // Hold the first prompt open ~400 ms so the OTHER path's
                // change arrives while a prompt is up — the stacking window.
                if (prompts == 0 && holdOpen.elapsed() < 400) return;
                ++prompts;
                promptedFor.insert(mb->text());
                holdOpen.invalidate();
                if (auto *b = mb->button(QMessageBox::No)) b->click();
            }
        });
        clicker.start();

        // Both change in the same event-loop turn.
        writeBytes(fA, "A v2\n");
        writeBytes(fB, "B v2\n");
        pumpFor(3000);
        EXPECT("both paths eventually prompted (deferred one not dropped)",
               prompts >= 2);
        EXPECT("prompts named both files (not the same path twice)",
               promptedFor.size() >= 2);
        EXPECT("never more than ONE box visible at a time", maxVisible == 1);
        clicker.stop();
    }

    // ── §4 — closeTab modal vs watcher: the use-after-free repro ──
    std::printf("\nSection 4 — file deleted while closeTab prompts\n");
    {
        const QString gateF = wd.path() + "/gate.txt";
        writeBytes(gateF, "gate content\n");
        MainWindow mw;
        mw.show();
        pumpFor(100);
        mw.openFile(gateF);
        pumpFor(100);
        auto *tm = mw.findChild<TabManager *>();
        Editor *e = tabForName(tm, "gate.txt");
        EXPECT("gate file open", e != nullptr);
        if (e) e->insertAt(QStringLiteral("UNSAVED\n"), 0, 0);
        EXPECT("gate tab modified", e && e->isModified());

        bool deleted = false, clicked = false;
        int maxVisible = 0, deletedBoxes = 0;
        QElapsedTimer sinceDelete;
        QTimer clicker;
        clicker.setInterval(50);
        QObject::connect(&clicker, &QTimer::timeout,
                         [&]() {
            const auto boxes = visibleMessageBoxes();
            maxVisible = qMax(maxVisible, int(boxes.size()));
            for (QMessageBox *mb : boxes) {
                if (mb->windowTitle() == QLatin1String("File Deleted")) {
                    // Must never appear. If the gate regresses, answer it
                    // (No deletes the editor closeTab holds) so the broken
                    // build fails FAST as a crash, not a suite hang.
                    ++deletedBoxes;
                    if (auto *b = mb->button(QMessageBox::No)) b->click();
                }
            }
            if (boxes.size() == 1 &&
                boxes[0]->windowTitle() == QLatin1String("Save")) {
                if (!deleted) {
                    // Delete the watched file WHILE the close prompt is up —
                    // the fileChanged lands in this modal's nested loop.
                    QFile::remove(gateF);
                    deleted = true;
                    sinceDelete.start();
                    return;
                }
                // Give the watcher ~600 ms inside the modal to (wrongly)
                // stack its own prompt, then answer.
                if (sinceDelete.elapsed() > 600 && !clicked) {
                    clicked = true;
                    if (auto *b = boxes[0]->button(QMessageBox::Discard))
                        b->click();
                }
            }
        });
        clicker.start();

        mw.closeAllTabs();   // prompts for the modified gate tab
        pumpFor(1500);       // deferred File-Deleted re-dispatch runs here
        clicker.stop();

        EXPECT("close prompt was answered (file deletion happened mid-prompt)",
               deleted && clicked);
        EXPECT("watcher prompt never stacked on the close prompt",
               maxVisible == 1);
        EXPECT("no File Deleted box fired during OR after (tab already gone)",
               deletedBoxes == 0);
        EXPECT("discarded tab is gone", tabForName(tm, "gate.txt") == nullptr);
        EXPECT("window survived (no use-after-free)", tm->count() >= 1);
    }

    std::printf("\n%d passed, %d failed\n", g_pass, g_fail);
    fflush(stdout);
    return g_fail;
}
