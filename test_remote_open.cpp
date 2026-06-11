// D2 (win-open-ghost) — remote-open contract.
// PART A: attachRemoteOpenClient (src/remoteopen.cpp) must accumulate a
// forwarded-open payload via readyRead (never block), survive split writes,
// cap payload size, and qWarn on every drop path. The replaced code blocked
// the GUI thread 500 ms per client and readAll'd ONCE — a split payload was
// silently truncated and the open silently dropped.
// PART B: handleRemoteOpen must raise/activate synchronously and defer the
// file loads one event-loop turn, so a forwarded double-click paints feedback
// before any heavy load runs.
// B8's not-found open queues a startup notice whose flush shows a QMessageBox
// — the offscreen-Windows QMessageBox class (win-noter-segfault) — so that
// section is winOffscreenModalUnsafe()-guarded.

#include "mainwindow.h"
#include "editor.h"
#include "tabmanager.h"
#include "config.h"
#include "remoteopen.h"

#include <QApplication>
#include <QCoreApplication>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLocalServer>
#include <QLocalSocket>
#include <QPointer>
#include <QTemporaryDir>

#include <cstdio>

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

static void pumpFor(int ms) {
    QElapsedTimer t;
    t.start();
    while (t.elapsed() < ms)
        QApplication::processEvents(QEventLoop::AllEvents, 10);
}

static Editor *tabForName(TabManager *tabs, const QString &name) {
    for (int i = 0; i < tabs->count(); ++i) {
        Editor *e = tabs->editorAt(i);
        if (e && QFileInfo(e->filePath()).fileName() == name) return e;
    }
    return nullptr;
}

// A5 — captured qWarning stream (drop paths must not be silent).
static QStringList g_warnings;
static void warningCatcher(QtMsgType type, const QMessageLogContext &,
                           const QString &msg) {
    if (type == QtWarningMsg) g_warnings.append(msg);
}

struct Captured {
    bool fired = false;
    int count = 0;
    QStringList paths;
    int line = -2;
    QByteArray sid;
};

static RemoteOpenHandler capture(Captured *c) {
    return [c](const QStringList &paths, int gotoLine, const QByteArray &sid) {
        c->fired = true;
        ++c->count;
        c->paths = paths;
        c->line = gotoLine;
        c->sid = sid;
    };
}

static QLocalSocket *acceptOne(QLocalServer &srv) {
    QElapsedTimer t;
    t.start();
    while (t.elapsed() < 2000 && !srv.hasPendingConnections())
        QApplication::processEvents(QEventLoop::AllEvents, 10);
    return srv.nextPendingConnection();
}

static QByteArray payloadFor(const QStringList &files, int gotoLine,
                             const QString &sid) {
    QJsonObject o;
    QJsonArray arr;
    for (const QString &f : files) arr.append(f);
    o["files"] = arr;
    o["gotoLine"] = gotoLine;
    o["startupId"] = sid;
    return QJsonDocument(o).toJson(QJsonDocument::Compact);
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

    // Unbuffered stdout: section headers printed before a hard crash are
    // captured by ctest --output-on-failure, pinpointing the failing section
    // on Windows (the v0.1.114 first run segfaulted with ZERO output).
    setvbuf(stdout, nullptr, _IONBF, 0);

    QApplication app(argc, argv);

    std::printf("=== test_remote_open ===\n\n");

    const QString srvName = "notepatra-test-ro-" +
        QString::number(QCoreApplication::applicationPid());
    QLocalServer::removeServer(srvName);
    QLocalServer srv;
    if (!srv.listen(srvName)) {
        std::printf("FATAL: cannot listen on %s\n", qPrintable(srvName));
        return 1;
    }

    // ── A1 — single-write payload parses once, fields intact ──
    std::printf("Part A1 — single-write payload\n");
    {
        Captured c;
        QLocalSocket cli;
        cli.connectToServer(srvName);
        cli.waitForConnected(2000);
        QLocalSocket *sc = acceptOne(srv);
        EXPECT("server accepted client", sc != nullptr);
        if (sc) {
            attachRemoteOpenClient(sc, capture(&c));
            cli.write(payloadFor({"/tmp/one.txt", "/tmp/two.txt"}, 7,
                                 "abc_TIME42"));
            cli.flush();
            pumpFor(150);
            EXPECT("handler fired exactly once", c.fired && c.count == 1);
            EXPECT("both paths in order",
                   c.paths == QStringList({"/tmp/one.txt", "/tmp/two.txt"}));
            EXPECT("gotoLine == 7", c.line == 7);
            EXPECT("startupId round-tripped", c.sid == "abc_TIME42");
        }
        cli.abort();
        pumpFor(50);
    }

    // ── A2 — split payload accumulates (THE truncation race) ──
    std::printf("\nPart A2 — split-write payload accumulates\n");
    {
        Captured c;
        QLocalSocket cli;
        cli.connectToServer(srvName);
        cli.waitForConnected(2000);
        QLocalSocket *sc = acceptOne(srv);
        EXPECT("server accepted client", sc != nullptr);
        if (sc) {
            attachRemoteOpenClient(sc, capture(&c));
            const QByteArray body =
                payloadFor({"/tmp/alpha.txt", "/tmp/beta.txt"}, 12, "s_TIME9");
            cli.write(body.left(body.size() / 2));
            cli.flush();
            pumpFor(80);
            EXPECT("half a payload does not fire the handler", !c.fired);
            QElapsedTimer t;
            t.start();
            cli.write(body.mid(body.size() / 2));
            cli.flush();
            while (t.elapsed() < 1000 && !c.fired)
                QApplication::processEvents(QEventLoop::AllEvents, 10);
            EXPECT("handler fired once with the COMPLETE list",
                   c.fired && c.count == 1 &&
                   c.paths == QStringList({"/tmp/alpha.txt", "/tmp/beta.txt"}));
            EXPECT("completion under 1000 ms of final write", t.elapsed() < 1000);
        }
        cli.abort();
        pumpFor(50);
    }

    // ── A3 — attach never blocks (replaced code waited 500 ms here) ──
    std::printf("\nPart A3 — non-blocking attach ceiling\n");
    {
        Captured c;
        QLocalSocket cli;
        cli.connectToServer(srvName);
        cli.waitForConnected(2000);
        QLocalSocket *sc = acceptOne(srv);
        EXPECT("server accepted client", sc != nullptr);
        if (sc) {
            QElapsedTimer t;
            t.start();
            attachRemoteOpenClient(sc, capture(&c));
            const qint64 elapsed = t.elapsed();
            std::printf("  attach took %lld ms\n",
                        static_cast<long long>(elapsed));
            EXPECT("attach on a silent client returns in < 100 ms",
                   elapsed < 100);
            EXPECT("handler not fired for silent client", !c.fired);
        }
        cli.abort();
        pumpFor(50);
    }

    // ── A4 — payload cap aborts the client, no crash ──
    std::printf("\nPart A4 — size cap drops oversized payload\n");
    {
        Captured c;
        QLocalSocket cli;
        cli.connectToServer(srvName);
        cli.waitForConnected(2000);
        QLocalSocket *sc = acceptOne(srv);
        EXPECT("server accepted client", sc != nullptr);
        if (sc) {
            // The drop path deleteLater()s the socket — track via QPointer.
            QPointer<QLocalSocket> scp(sc);
            attachRemoteOpenClient(sc, capture(&c));
            const QByteArray big(kRemoteOpenPayloadCap + 4096, 'x');
            cli.write(big);
            cli.flush();
            QElapsedTimer t;
            t.start();
            while (t.elapsed() < 3000 && scp &&
                   scp->state() != QLocalSocket::UnconnectedState)
                QApplication::processEvents(QEventLoop::AllEvents, 10);
            EXPECT("handler never fired for oversized payload", !c.fired);
            EXPECT("server side dropped the client",
                   !scp || scp->state() == QLocalSocket::UnconnectedState);
        }
        cli.abort();
        pumpFor(50);
    }

    // ── A5 — unparseable payload warns (no silent drop) ──
    std::printf("\nPart A5 — garbage payload qWarns\n");
    {
        Captured c;
        QLocalSocket cli;
        cli.connectToServer(srvName);
        cli.waitForConnected(2000);
        QLocalSocket *sc = acceptOne(srv);
        EXPECT("server accepted client", sc != nullptr);
        if (sc) {
            g_warnings.clear();
            QtMessageHandler prev = qInstallMessageHandler(warningCatcher);
            attachRemoteOpenClient(sc, capture(&c));
            cli.write("not json");
            cli.flush();
            pumpFor(50);
            cli.disconnectFromServer();
            pumpFor(200);
            qInstallMessageHandler(prev);
            EXPECT("handler never fired for garbage", !c.fired);
            bool warned = false;
            for (const QString &w : g_warnings)
                if (w.contains("remote-open")) warned = true;
            EXPECT("a qWarning names the remote-open drop", warned);
        }
        pumpFor(50);
    }

    // ── A6 — data raced ahead of the attach (startup-drain replay) ──
    std::printf("\nPart A6 — payload written before attach still delivers\n");
    {
        Captured c;
        QLocalSocket cli;
        cli.connectToServer(srvName);
        cli.waitForConnected(2000);
        cli.write(payloadFor({"/tmp/early.txt"}, -1, ""));
        cli.flush();
        pumpFor(100);  // bytes land BEFORE anyone attaches
        QLocalSocket *sc = acceptOne(srv);
        EXPECT("server accepted client", sc != nullptr);
        if (sc) {
            attachRemoteOpenClient(sc, capture(&c));
            QElapsedTimer t;
            t.start();
            while (t.elapsed() < 200 && !c.fired)
                QApplication::processEvents(QEventLoop::AllEvents, 10);
            EXPECT("handler fired for pre-attach payload",
                   c.fired && c.paths == QStringList({"/tmp/early.txt"}));
        }
        cli.abort();
        pumpFor(50);
    }

    srv.close();

    // ── PART B — MainWindow activation-before-load contract ──
    std::printf("\nPart B — handleRemoteOpen defers loads past activation\n");

    QTemporaryDir wd;
    const QString fA = wd.path() + "/a.txt";
    const QString fB = wd.path() + "/b.txt";
    const QString f1 = wd.path() + "/first.txt";
    const QString f2 = wd.path() + "/second.txt";
    for (const QString &p : {fA, f1, f2}) {
        QFile f(p);
        f.open(QIODevice::WriteOnly);
        f.write("hello remote\n");
    }
    {
        QFile f(fB);
        f.open(QIODevice::WriteOnly);
        for (int i = 1; i <= 10; ++i)
            f.write(QString("line %1\n").arg(i).toUtf8());
    }

    {
        MainWindow mw;
        auto *tm = mw.findChild<TabManager *>();
        EXPECT("TabManager found", tm != nullptr);
        if (!tm) { std::printf("\n%d passed, %d failed\n", g_pass, g_fail); return 1; }
        pumpFor(100);  // deferred startup (D1) completes; empty session = no-op

        // B1 — activation synchronous, load deferred
        const int before = tm->count();
        mw.handleRemoteOpen({fA}, -1);
        EXPECT("window visible immediately after handleRemoteOpen",
               mw.isVisible());
        EXPECT("load NOT yet performed at return", tm->count() == before);
        pumpFor(200);
        Editor *a = tabForName(tm, "a.txt");
        EXPECT("a.txt opened by the deferred flush", a != nullptr);
        EXPECT("a.txt is the current tab",
               tm->currentEditor() && tm->currentEditor() == a);

        // B2 — gotoLine applied after the deferred open (1-based)
        mw.handleRemoteOpen({fB}, 3);
        pumpFor(200);
        Editor *b = tabForName(tm, "b.txt");
        EXPECT("b.txt opened", b != nullptr);
        if (b) {
            int line = -1, col = -1;
            b->getCursorPosition(&line, &col);
            EXPECT("gotoLine(3) landed on 0-based line 2", line == 2);
        }

        // B3 — burst ordering, queue preserves arrival order
        mw.handleRemoteOpen({f1}, -1);
        mw.handleRemoteOpen({f2}, -1);
        pumpFor(200);
        EXPECT("first.txt opened", tabForName(tm, "first.txt") != nullptr);
        EXPECT("second.txt opened", tabForName(tm, "second.txt") != nullptr);
        EXPECT("burst: second request's file is current",
               tm->currentEditor() &&
               tm->currentEditor() == tabForName(tm, "second.txt"));

        // B4 — return-time perf ceiling with a ~4 MB file (old code ran
        // loadFile + 2x full-document word-count regex INSIDE this call)
        const QString big = wd.path() + "/big.txt";
        {
            QFile f(big);
            f.open(QIODevice::WriteOnly);
            QByteArray chunk;
            chunk.reserve(1 << 16);
            while (chunk.size() < (1 << 16))
                chunk += "lorem ipsum dolor sit amet consectetur ";
            for (int i = 0; i < 64; ++i) f.write(chunk);  // ~4 MB
        }
        QElapsedTimer t;
        t.start();
        mw.handleRemoteOpen({big}, -1);
        const qint64 elapsed = t.elapsed();
        std::printf("  handleRemoteOpen(4MB) returned in %lld ms\n",
                    static_cast<long long>(elapsed));
        EXPECT("handleRemoteOpen returns in < 250 ms for a 4 MB file",
               elapsed < 250);
        QElapsedTimer w;
        w.start();
        while (w.elapsed() < 3000 && tabForName(tm, "big.txt") == nullptr)
            QApplication::processEvents(QEventLoop::AllEvents, 10);
        EXPECT("4 MB file eventually opened (no lost open)",
               tabForName(tm, "big.txt") != nullptr);

        // B5 — empty forward (icon re-click / bare `notepatra`): raise only,
        // open nothing, no notice.
        const int b5Before = tm->count();
        mw.handleRemoteOpen({}, -1);
        pumpFor(200);
        EXPECT("empty forward: tab count unchanged", tm->count() == b5Before);
        EXPECT("empty forward: window still visible", mw.isVisible());

        // B6 — re-open of an ALREADY-OPEN file focuses the existing tab:
        // no duplicate, no silent disk reload clobbering unsaved edits.
        Editor *a2 = tabForName(tm, "a.txt");
        EXPECT("B6: a.txt still open", a2 != nullptr);
        if (a2) {
            a2->insertAt(QStringLiteral("REMOTE-UNSAVED-EDIT\n"), 0, 0);
            const int b6Before = tm->count();
            mw.handleRemoteOpen({fA}, -1);
            pumpFor(200);
            EXPECT("B6: no duplicate tab", tm->count() == b6Before);
            EXPECT("B6: existing tab focused",
                   tm->currentEditor() == tabForName(tm, "a.txt"));
            EXPECT("B6: unsaved edit survived the re-open",
                   tabForName(tm, "a.txt") &&
                   tabForName(tm, "a.txt")->text().contains(
                       QStringLiteral("REMOTE-UNSAVED-EDIT")));
        }

        // B7 — multi-file forward with gotoLine: --help promises the FIRST
        // file gets the jump; openFile leaves the LAST tab active.
        const QString m1 = wd.path() + "/multi1.txt";
        const QString m2 = wd.path() + "/multi2.txt";
        for (const QString &p : {m1, m2}) {
            QFile f(p);
            f.open(QIODevice::WriteOnly);
            for (int i = 1; i <= 10; ++i)
                f.write(QString("m line %1\n").arg(i).toUtf8());
        }
        mw.handleRemoteOpen({m1, m2}, 4);
        pumpFor(200);
        Editor *em1 = tabForName(tm, "multi1.txt");
        EXPECT("B7: multi1.txt opened", em1 != nullptr);
        EXPECT("B7: multi2.txt opened",
               tabForName(tm, "multi2.txt") != nullptr);
        EXPECT("B7: FIRST file is the current tab",
               tm->currentEditor() && tm->currentEditor() == em1);
        if (em1) {
            int line = -1, col = -1;
            em1->getCursorPosition(&line, &col);
            EXPECT("B7: gotoLine(4) landed in the FIRST file (0-based 3)",
                   line == 3);
        }

        // B8 — gotoLine for a first file that FAILS to open must not jump
        // an unrelated surviving tab. The not-found notice flush shows a
        // QMessageBox, unsafe under the offscreen-Windows QPA plugin.
        if (winOffscreenModalUnsafe()) {
            std::printf("  [SKIP] B8 not-found notice flush — offscreen-"
                        "Windows QMessageBox class (win-noter-segfault)\n");
        } else {
            Editor *cur = tm->currentEditor();
            int curLine = -1, curCol = -1;
            if (cur) cur->getCursorPosition(&curLine, &curCol);
            mw.handleRemoteOpen({wd.path() + "/does-not-exist.txt"}, 8);
            pumpFor(200);
            if (cur) {
                int nl = -1, nc = -1;
                cur->getCursorPosition(&nl, &nc);
                EXPECT("B8: failed first file leaves the surviving tab's cursor",
                       nl == curLine && nc == curCol);
            }
        }
    }

    std::printf("\n%d passed, %d failed\n", g_pass, g_fail);
    return g_fail == 0 ? 0 : 1;
}
