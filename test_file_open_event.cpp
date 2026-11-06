// macOS Finder double-click / Open With / Dock drop deliver QEvent::FileOpen
// to qApp. FileOpenFilter must open the file in MainWindow through the
// remote-open path, and queue events that arrive before the window exists.

#include "mainwindow.h"
#include "editor.h"
#include "tabmanager.h"
#include "file_open_filter.h"

#include <QApplication>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QFileOpenEvent>
#include <QTemporaryDir>
#include <QUrl>

#include <cstdio>

static int g_pass = 0, g_fail = 0;
#define EXPECT(label, cond) \
    do { if (cond) { ++g_pass; std::printf("  [PASS] %s\n", label); } \
         else      { ++g_fail; std::printf("  [FAIL] %s\n", label); } } while (0)

static int tabsForPath(TabManager *tabs, const QString &path) {
    const QString abs = QFileInfo(path).absoluteFilePath();
    int n = 0;
    for (int i = 0; i < tabs->count(); ++i) {
        Editor *e = tabs->editorAt(i);
        if (e && QFileInfo(e->filePath()).absoluteFilePath() == abs) ++n;
    }
    return n;
}

static bool waitForTab(TabManager *tabs, const QString &path, int ms = 3000) {
    QElapsedTimer t;
    t.start();
    while (t.elapsed() < ms) {
        QApplication::processEvents(QEventLoop::AllEvents, 20);
        if (tabsForPath(tabs, path) > 0) return true;
    }
    return false;
}

static void writeFile(const QString &p) {
    QFile f(p);
    f.open(QIODevice::WriteOnly);
    f.write("hello from finder\n");
}

int main(int argc, char *argv[]) {
    QTemporaryDir cfg;
    qputenv("XDG_CONFIG_HOME", cfg.path().toUtf8());
    qputenv("XDG_DATA_HOME",   cfg.path().toUtf8());
    qputenv("HOME", cfg.path().toUtf8());
#ifdef Q_OS_WIN
    qputenv("APPDATA",     cfg.path().toUtf8());
    qputenv("USERPROFILE", cfg.path().toUtf8());
#endif
    qputenv("QT_QPA_PLATFORM", "offscreen");

    QApplication app(argc, argv);
    std::printf("=== test_file_open_event ===\n\n");

    QTemporaryDir wd;
    const QString early = wd.path() + "/early.txt";
    const QString earlyUrl = wd.path() + "/early_url.txt";
    const QString live = wd.path() + "/live.txt";
    for (const QString &p : {early, earlyUrl, live}) writeFile(p);

    FileOpenFilter filter;
    app.installEventFilter(&filter);

    std::printf("Phase A — events before MainWindow are queued\n");
    {
        QFileOpenEvent ev(early);
        const bool consumed = QCoreApplication::sendEvent(&app, &ev);
        EXPECT("FileOpen(path) consumed by the filter", consumed);
    }
    QCoreApplication::postEvent(&app, new QFileOpenEvent(QUrl::fromLocalFile(earlyUrl)));
    QApplication::processEvents();
    {
        QFileOpenEvent web(QUrl(QStringLiteral("https://notepatra.org/")));
        const bool consumed = QCoreApplication::sendEvent(&app, &web);
        EXPECT("non-file URL is not consumed", !consumed);
    }
    EXPECT("two local paths queued (path + file URL)",
           filter.pendingPaths() == QStringList({early, earlyUrl}));

    MainWindow mw;
    auto *tabs = mw.findChild<TabManager *>();
    EXPECT("TabManager found", tabs != nullptr);
    if (!tabs) { std::printf("\n%d passed, %d failed\n", g_pass, g_fail); return 1; }
    filter.setHandler([&mw](const QStringList &paths) { mw.handleRemoteOpen(paths, -1); });
    EXPECT("queue drained into the handler", filter.pendingPaths().isEmpty());
    mw.show();
    QApplication::processEvents();
    EXPECT("queued open parked until startup restore", tabsForPath(tabs, early) == 0);
    mw.runStartupNow();
    EXPECT("queued path opens after startup", waitForTab(tabs, early));
    EXPECT("queued file URL opens after startup", waitForTab(tabs, earlyUrl));

    std::printf("Phase B — event with the window live\n");
    QCoreApplication::postEvent(&app, new QFileOpenEvent(live));
    EXPECT("live FileOpen opens a tab", waitForTab(tabs, live));
    const int before = tabs->count();
    {
        QFileOpenEvent again(early);
        QCoreApplication::sendEvent(&app, &again);
    }
    QElapsedTimer t;
    t.start();
    while (t.elapsed() < 300) QApplication::processEvents(QEventLoop::AllEvents, 20);
    Editor *cur = tabs->editorAt(tabs->currentIndex());
    EXPECT("re-open of an open file does not duplicate the tab",
           tabsForPath(tabs, early) == 1 && tabs->count() == before);
    EXPECT("re-open activates the existing tab",
           cur && QFileInfo(cur->filePath()).absoluteFilePath()
                      == QFileInfo(early).absoluteFilePath());

    app.removeEventFilter(&filter);
    std::printf("\n%d passed, %d failed\n", g_pass, g_fail);
    return g_fail == 0 ? 0 : 1;
}
