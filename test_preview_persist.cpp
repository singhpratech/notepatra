// SPDX-License-Identifier: GPL-3.0-or-later
//
// test_preview_persist — the Preview pane's open/closed state and splitter
// sizes survive a relaunch: MainWindow #1 opens the pane and sizes it, closes;
// Config is re-read from disk; MainWindow #2 restores both after it is shown.

#include "mainwindow.h"
#include "config.h"
#include "editor.h"
#include "preview_pane.h"

#include <QAction>
#include <QApplication>
#include <QSplitter>
#include <QTemporaryDir>
#include <QTest>

#include <cstdio>

static int g_passed = 0;
static int g_failed = 0;

#define EXPECT_TRUE(label, cond) \
    do { \
        if (cond) { ++g_passed; std::printf("  [PASS] %s\n", label); } \
        else { ++g_failed; std::printf("  [FAIL] %s\n", label); } \
        std::fflush(stdout); \
    } while (0)

struct Window {
    MainWindow *mw = nullptr;
    PreviewPane *pane = nullptr;
    QAction *act = nullptr;
    QSplitter *split = nullptr;
};

static Window launch() {
    Window w;
    w.mw = new MainWindow;
    w.mw->resize(1400, 900);
    w.mw->show();
    w.mw->runStartupNow();
    QTest::qWait(150);
    w.pane = w.mw->findChild<PreviewPane *>();
    w.act = w.mw->findChild<QAction *>(QStringLiteral("actPreviewPane"));
    w.split = w.pane ? qobject_cast<QSplitter *>(w.pane->parentWidget()) : nullptr;
    return w;
}

static void quit(Window &w) {
    w.mw->close();
    delete w.mw;
    w.mw = nullptr;
    QApplication::processEvents();
    // Prove the next window reads the state from disk, not from memory.
    Config &cfg = Config::instance();
    cfg.previewPaneVisible = false;
    cfg.previewPaneSizes.clear();
    cfg.load();
}

static double share(QSplitter *s) {
    const QList<int> sz = s ? s->sizes() : QList<int>();
    if (sz.size() != 2 || sz.at(0) + sz.at(1) <= 0) return -1;
    return double(sz.at(1)) / double(sz.at(0) + sz.at(1));
}

int main(int argc, char *argv[]) {
    setvbuf(stdout, nullptr, _IONBF, 0);
    qputenv("QT_QPA_PLATFORM", "offscreen");
    QTemporaryDir cfgDir;
    qputenv("XDG_CONFIG_HOME", cfgDir.path().toUtf8());
    qputenv("XDG_DATA_HOME", cfgDir.path().toUtf8());
    qputenv("HOME", cfgDir.path().toUtf8());
    QApplication app(argc, argv);
    std::printf("=== test_preview_persist ===\n");

    Window w1 = launch();
    if (!w1.pane || !w1.act || !w1.split) {
        std::printf("  [FAIL] pane / action / splitter not found\n");
        return 1;
    }
    EXPECT_TRUE("1: fresh config -> pane hidden", !w1.pane->isVisible());
    w1.act->trigger();
    QTest::qWait(50);
    const int total = w1.split->width();
    w1.split->setSizes({total * 70 / 100, total - total * 70 / 100});
    QTest::qWait(50);
    const double wanted = share(w1.split);
    std::printf("     #1 sizes share=%.3f\n", wanted);
    EXPECT_TRUE("1: pane open at ~30%", w1.pane->isVisible() && wanted > 0.25 && wanted < 0.35);
    quit(w1);
    EXPECT_TRUE("1: state written to disk", Config::instance().previewPaneVisible &&
                                                Config::instance().previewPaneSizes.size() == 2);

    Window w2 = launch();
    const double got = share(w2.split);
    std::printf("     #2 sizes share=%.3f\n", got);
    EXPECT_TRUE("2: pane restored visible, action checked", w2.pane && w2.pane->isVisible() && w2.act->isChecked());
    EXPECT_TRUE("2: splitter sizes restored within 5%", got > 0 && qAbs(got - wanted) <= 0.05);
    w2.act->trigger();
    QTest::qWait(30);
    quit(w2);

    Window w3 = launch();
    EXPECT_TRUE("3: closed pane stays closed", w3.pane && !w3.pane->isVisible() && !w3.act->isChecked());
    quit(w3);

    std::printf("\n=== %d passed, %d failed ===\n", g_passed, g_failed);
    return g_failed == 0 ? 0 : 1;
}
