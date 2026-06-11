// SPDX-License-Identifier: GPL-3.0-or-later

#include <QApplication>
#include <QGuiApplication>
#include <QFileInfo>
#include <QMessageBox>
#include <QLocalServer>
#include <QLocalSocket>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QElapsedTimer>
#include <QFile>
#include <QStatusBar>
#include <QStringList>
#include <QThread>
#include <QTimer>
#include <cstdio>
#include <csignal>
#include <exception>
#ifdef Q_OS_WIN
// NOMINMAX: windows.h defines min/max as preprocessor macros that collide
// with std::min/std::max calls elsewhere in this translation unit (and in
// indirect Qt includes). Define before windows.h is pulled in.
#  define NOMINMAX
#  include <windows.h>
#endif
#ifdef Q_OS_LINUX
#  include <xcb/xcb.h>
#  include <cstring>
#  include <cstdlib>
#  include <string>
#endif
#include "mainwindow.h"
#include "remoteopen.h"
#include "singleinstance.h"
#include "cliargs.h"
#include "crashflag.h"
#include "editor.h"
#include "config.h"
#include "fonts.h"
#include "fontpack.h"
#include "build_flavor.h"

// NOTEPATRA_VERSION is injected at compile time from CMakeLists.txt's
// project(Notepatra VERSION X.Y.Z ...) so a single bump in CMake propagates
// to --version, --help, app version, .desktop file, etc. without needing
// to sed across multiple source files.
#ifndef NOTEPATRA_VERSION
#define NOTEPATRA_VERSION "0.0.0-dev"
#endif

// Global crash handler. Async-signal-safe: precomputed path (crashFlagInit),
// raw kernel write, then default action. The old handler called
// Config::appConfigDir()/QDir::mkpath/QFile inside the signal context —
// any of those can deadlock in malloc and turn a crash into a silent hang.
static void crashHandler(int sig) {
    crashFlagWrite();
    signal(sig, SIG_DFL);
    raise(sig);
}

#ifdef Q_OS_WIN
static LONG WINAPI crashExceptionFilter(EXCEPTION_POINTERS *) {
    crashFlagWrite();
    return EXCEPTION_CONTINUE_SEARCH;  // keep WER dump / debugger behavior
}
#endif

#ifdef Q_OS_LINUX
// When Nemo / Files / gtk-launch fires our .desktop entry, it stamps
// DESKTOP_STARTUP_ID in the env and the WM starts a launch-feedback spinner
// waiting for a window with that ID to map. In the single-instance case we
// forward the file path to the already-running Notepatra and exit without
// ever creating a window, so the spinner ticks until the WM's 15 s timeout.
// Send a freedesktop startup-notify "remove" ClientMessage on the root
// window to cancel the spinner explicitly. See
// https://specifications.freedesktop.org/startup-notification-spec/
static void sendStartupNotifyComplete(const char *startupId) {
    if (!startupId || !*startupId) return;
    xcb_connection_t *conn = xcb_connect(nullptr, nullptr);
    if (!conn || xcb_connection_has_error(conn)) {
        if (conn) xcb_disconnect(conn);
        return;
    }
    auto intern = [&](const char *name) -> xcb_atom_t {
        xcb_intern_atom_cookie_t c = xcb_intern_atom(conn, 0, std::strlen(name), name);
        xcb_intern_atom_reply_t *r = xcb_intern_atom_reply(conn, c, nullptr);
        xcb_atom_t a = r ? r->atom : XCB_ATOM_NONE;
        free(r);
        return a;
    };
    const xcb_atom_t aBegin = intern("_NET_STARTUP_INFO_BEGIN");
    const xcb_atom_t aInfo  = intern("_NET_STARTUP_INFO");
    const xcb_screen_t *screen = xcb_setup_roots_iterator(xcb_get_setup(conn)).data;
    if (aBegin == XCB_ATOM_NONE || aInfo == XCB_ATOM_NONE || !screen) {
        xcb_disconnect(conn);
        return;
    }
    xcb_window_t src = xcb_generate_id(conn);
    xcb_create_window(conn, 0, src, screen->root, -100, -100, 1, 1, 0,
                      XCB_WINDOW_CLASS_INPUT_ONLY, screen->root_visual, 0, nullptr);
    // Per freedesktop startup-notification spec, string values in
    // messages must be quoted with double-quotes, with `"` and `\` in
    // the value escaped as `\"` and `\\`. libstartup-notification does
    // this; an unquoted `ID=<id>` is silently ignored by Cinnamon's
    // launch-feedback tracker (and the spinner ticks until the WM's
    // 15 s timeout). Build the message the spec-compliant way.
    std::string msg = "remove: ID=\"";
    for (const char *p = startupId; *p; ++p) {
        if (*p == '"' || *p == '\\') msg.push_back('\\');
        msg.push_back(*p);
    }
    msg.push_back('"');
    msg.push_back('\0');
    size_t offset = 0;
    bool first = true;
    while (offset < msg.size()) {
        xcb_client_message_event_t ev{};
        ev.response_type = XCB_CLIENT_MESSAGE;
        ev.format = 8;
        ev.window = src;
        ev.type = first ? aBegin : aInfo;
        const size_t take = std::min<size_t>(20, msg.size() - offset);
        std::memcpy(ev.data.data8, msg.data() + offset, take);
        xcb_send_event(conn, false, screen->root,
                       XCB_EVENT_MASK_PROPERTY_CHANGE,
                       reinterpret_cast<const char *>(&ev));
        offset += take;
        first = false;
    }
    xcb_destroy_window(conn, src);
    xcb_flush(conn);
    // Round-trip fence: ensure the X server has actually processed our
    // SendEvent + DestroyWindow before we tear down the connection.
    // Without this, the events can be dropped on disconnect and the
    // spinner ticks until timeout. (Same bug class as the focus path.)
    xcb_get_input_focus_reply_t *fr =
        xcb_get_input_focus_reply(conn, xcb_get_input_focus(conn), nullptr);
    free(fr);
    xcb_disconnect(conn);
}
#endif // Q_OS_LINUX

int main(int argc, char *argv[]) {
    // Handle --version and --help before creating QApplication
    for (int i = 1; i < argc; i++) {
        QString arg = QString::fromUtf8(argv[i]);
        if (arg == "--version" || arg == "-v") {
            // NOTEPATRA_FLAVOR_NAME encodes Lite/Full + the Local AI variant
            // (see build_flavor.h), so the four build flavors self-identify.
            printf("%s v%s\n", NOTEPATRA_FLAVOR_NAME, NOTEPATRA_VERSION);
#ifdef NOTEPATRA_NO_CLOUD
            printf("Native C++/Rust code editor — local & private-network LLM endpoints only\n");
#else
            printf("Native C++/Rust code editor\n");
#endif
            printf("https://github.com/singhpratech/notepatra\n");
            return 0;
        }
        if (arg == "--help" || arg == "-h") {
            printf("Notepatra v%s — Native code editor for Linux\n\n", NOTEPATRA_VERSION);
            printf("Usage: notepatra [options] [file1] [file2] ...\n\n");
            printf("Options:\n");
            printf("  -h, --help       Show this help\n");
            printf("  -v, --version    Show version\n");
            printf("  -n, --new        Open new window (don't restore session)\n");
            printf("  --line N         Go to line N in the first file\n");
            printf("  --theme NAME     Use theme: Light, Dark, Monokai\n\n");
            printf("Examples:\n");
            printf("  notepatra                       Open with last session\n");
            printf("  notepatra file.py               Open file\n");
            printf("  notepatra --line 42 file.py     Open file at line 42\n");
            printf("  notepatra --theme Dark           Start in dark mode\n");
            printf("  notepatra *.json                Open multiple files\n\n");
            printf("Envisioned by Prateek Singh. Built by Claude.\n");
            return 0;
        }
    }

    // Capture DESKTOP_STARTUP_ID BEFORE constructing QApplication. Qt's
    // xcb platform plugin unsets the env var inside QApplication's
    // constructor so child processes don't inherit it — but our
    // single-instance bridge needs it to forward to the running instance
    // for proper _NET_STARTUP_ID handoff. Snapshot once, use later.
#ifdef Q_OS_LINUX
    QByteArray capturedStartupId;
    if (const char *sid = std::getenv("DESKTOP_STARTUP_ID"); sid && *sid) {
        capturedStartupId = QByteArray(sid);
    }
#endif

    // Install crash handlers. All are no-ops until crashFlagInit runs
    // (post-QApplication), so installing this early is safe.
    signal(SIGSEGV, crashHandler);
    signal(SIGABRT, crashHandler);
    signal(SIGFPE, crashHandler);
    std::set_terminate([]() {
        crashFlagWrite();
        std::abort();   // routes through the SIGABRT handler; flag write is idempotent
    });
#ifdef Q_OS_WIN
    SetUnhandledExceptionFilter(crashExceptionFilter);
#endif

    // ───────────────────────────────────────────────────────────────────
    // v0.1.50 — HiDPI / fractional-zoom support, especially for Windows.
    //
    // BACKGROUND. Qt 5.15 defaults its scale-factor rounding policy to
    // `Round`, so a Windows machine set to 125 % rounds DOWN to 100 %,
    // 150 % rounds UP to 200 %, and 175 % rounds UP to 200 % too. The
    // first case makes everything tiny; the latter two make every button
    // / icon / font 33 % wider than its layout was sized for, which is
    // the exact cause of the user-reported "Bracket Tools / HTML Tools
    // button labels are cut on Windows" bug at 150 % display zoom.
    //
    // FIX. Three Qt application attributes, set BEFORE QApplication is
    // constructed (Qt requires this — they're inspected during static
    // platform-plugin init):
    //
    //   1. `Qt::AA_EnableHighDpiScaling` — opt the app into Qt's logical-
    //      pixel coordinate system. setFixedHeight(26) means "26 logical
    //      px" instead of "26 device px"; Qt translates to device px
    //      based on the monitor's DPI.
    //   2. `Qt::HighDpiScaleFactorRoundingPolicy::PassThrough` — use the
    //      OS-reported scale factor exactly (1.25, 1.5, 1.75, …) instead
    //      of rounding to the nearest integer. This is what makes 150 %
    //      actually behave as 1.5 ×.
    //   3. `Qt::AA_UseHighDpiPixmaps` — opt into @2x bitmap variants for
    //      QIcon / QPixmap so toolbar icons and the leaf-circuit logo
    //      stay crisp on Retina / 200 % displays.
    //
    // Linux / macOS already behave correctly in most cases (Wayland +
    // GNOME Mutter scale at the compositor level; macOS handles Retina
    // transparently), so these flags are mainly for Windows but the
    // PassThrough policy is also a quality-of-life win on Linux distros
    // that report fractional fontconfig DPI.
    // ───────────────────────────────────────────────────────────────────
#if QT_VERSION >= QT_VERSION_CHECK(5, 14, 0)
    QGuiApplication::setHighDpiScaleFactorRoundingPolicy(
        Qt::HighDpiScaleFactorRoundingPolicy::PassThrough);
#endif
    QCoreApplication::setAttribute(Qt::AA_EnableHighDpiScaling, true);
    QCoreApplication::setAttribute(Qt::AA_UseHighDpiPixmaps, true);

    QApplication app(argc, argv);
    app.setApplicationName("Notepatra");          // bare name — drives QSettings / config paths, keep stable
    app.setApplicationDisplayName(NOTEPATRA_FLAVOR_NAME);  // flavor-aware name shown in window/taskbar
    app.setOrganizationName("Notepatra");
    app.setApplicationVersion(NOTEPATRA_VERSION);

    // Precompute the crash-flag path: Config::appConfigDir allocates, and
    // signal handlers must not. crashedLastRun is captured BEFORE MainWindow
    // construction so it is immune to ctor reordering by other code.
    const QString crashFlagPath =
        Config::appConfigDir() + QStringLiteral("/recovery/.crash_flag");
    crashFlagInit(crashFlagPath);
    const bool crashedLastRun = QFile::exists(crashFlagPath);

    // v0.1.75 — register every user-installed runtime font BEFORE we
    // pick the default UI / code family. notepatraDefaultUiFamily() and
    // notepatraDefaultCodeFamily() walk QFontDatabase().families() in
    // priority order, so any TTF / OTF the user has downloaded into the
    // font-pack directory becomes immediately available, no restart.
    NotepatraFontPack::loadInstalledFonts();

    app.setFont(notepatraUiFont());

    // Parse remaining args. Windows argv[] bytes are CP_ACP (qtmain hands
    // main() the CRT's ANSI argv), so fromUtf8(argv[i]) mangled every
    // non-ASCII path and the isFile() check silently dropped it — the
    // "double-click does nothing" ghost. QCoreApplication::arguments()
    // rebuilds the list from GetCommandLineW (true UTF-16) on Windows and
    // is fromLocal8Bit elsewhere — strictly better on all three platforms.
    const CliArgs cli = parseCliArgs(QCoreApplication::arguments());
    const int gotoLine = cli.gotoLine;
    const QString themeOverride = cli.theme;
    const bool newWindow = cli.newWindow;
    const QStringList filesToOpen = cli.files;
    for (const QString &p : cli.notFound)
        qWarning("Notepatra: skipping argument that is not an existing file: %s",
                 qPrintable(p));

    // ─── Single-instance bridge (D6) ───
    // If another Notepatra is already running for this user and the caller
    // didn't pass --new, forward our args into it and exit — but only after
    // the primary ACKs. The old fire-and-forget write meant a hung primary
    // silently swallowed up to 8 opens (Windows pre-accepts pipe instances),
    // and the 9th spawned a duplicate full instance that clone-raced the
    // session. Now: no ACK in 3 s → primary exists but is stuck → open a
    // standalone window with ONLY the requested files. Visible, can't
    // black-hole opens, never binds the pipe, never touches session.json.
    const QString serverName = SingleInstance::serverName();
    bool standaloneFallback = false;  // primary exists but is unreachable/hung
    if (!newWindow) {
        QJsonObject payload;
        QJsonArray arr;
        for (const QString &p : filesToOpen) arr.append(p);
        // Not-found args ride along too: openFile() surfaces a statusbar
        // notice for them in the primary. Without this the stderr warning
        // above was the ONLY feedback — invisible to GUI launches.
        for (const QString &p : cli.notFound) arr.append(p);
        payload.insert("files", arr);
        payload.insert("gotoLine", gotoLine);
        // Forward DESKTOP_STARTUP_ID (captured at main() entry before Qt
        // could unset it) so the running instance can set _NET_STARTUP_ID
        // on its window — Cinnamon matches it to stop the spinner.
#ifdef Q_OS_LINUX
        if (!capturedStartupId.isEmpty())
            payload.insert("startupId", QString::fromUtf8(capturedStartupId));
#endif
        const QByteArray body = QJsonDocument(payload).toJson(QJsonDocument::Compact);
#ifdef Q_OS_WIN
        // Grant foreground rights BEFORE the ACK wait — the primary may
        // raise its window while we are still alive waiting for the ACK.
        // (Explorer hands foreground rights to *this* process, not the
        // running one; ASFW_ANY surrenders them. The grant persists after
        // we exit.)
        ::AllowSetForegroundWindow(ASFW_ANY);
#endif
        QElapsedTimer probeClock;
        probeClock.start();
        const SingleInstance::ForwardResult fwd =
            SingleInstance::forwardToPrimary(serverName, body);
        if (fwd == SingleInstance::ForwardResult::Acked) {
#ifdef Q_OS_LINUX
            // Cancel the desktop launch-feedback spinner — we exit without
            // ever mapping a window.
            sendStartupNotifyComplete(
                capturedStartupId.isEmpty() ? nullptr
                                            : capturedStartupId.constData());
#endif
            return 0;
        }
        if (fwd == SingleInstance::ForwardResult::NoAck) {
            // Connected but never acknowledged — a primary exists and is
            // stuck. Open our files standalone; never bind its pipe or
            // touch its session.
            standaloneFallback = true;
        }
#ifdef Q_OS_WIN
        else {  // NoServer: pipe unreachable — the kernel mutex is ground truth
            bool primaryExists = false;
            void *mutexHandle =
                SingleInstance::acquireSingletonMutex(serverName, &primaryExists);
            if (primaryExists) {
                if (mutexHandle) ::CloseHandle(static_cast<HANDLE>(mutexHandle));
                // Primary alive but its pipe didn't answer (instances
                // exhausted or mid-startup). Re-probe with patience, but
                // only if the first probe failed FAST (cold-start mutex
                // race): connectToServer fails INSTANTLY when the pipe
                // doesn't exist yet, so a single immediate retry lost the
                // race every time.
                if (probeClock.elapsed() < 2000) {
                    QElapsedTimer reprobe;
                    reprobe.start();
                    auto re = SingleInstance::ForwardResult::NoServer;
                    while (reprobe.elapsed() < 3000) {
                        re = SingleInstance::forwardToPrimary(serverName, body,
                                                              500, 0, 3000);
                        // Delivered, or up-but-stuck — either way stop.
                        if (re != SingleInstance::ForwardResult::NoServer) break;
                        QThread::msleep(100);
                    }
                    if (re == SingleInstance::ForwardResult::Acked) return 0;
                }
                standaloneFallback = true;
            }
            // else: we created the mutex — we are the primary. The handle is
            // deliberately held for process lifetime; the kernel frees it on
            // any exit, including a crash. Qt's QLocalServer on Windows lacks
            // FILE_FLAG_FIRST_PIPE_INSTANCE, so without the mutex a second
            // listen() would silently succeed and split future opens.
        }
#endif
        // Linux/macOS NoServer: fall through to listen() exactly as before —
        // Unix-domain bind gives the mutual exclusion Windows pipes lack.
    }

    // Apply theme override before window creation
    if (!themeOverride.isEmpty()) {
        Config::instance().theme = themeOverride;
    }

    // v0.1.94 — bind the named-pipe/socket BEFORE constructing MainWindow.
    // MainWindow's constructor restores the session (potentially many tabs,
    // many seconds on a large session); without an early bind, any double-
    // click during that window failed every probe and accumulated PIDs.
    QLocalServer *server = nullptr;
    if (!newWindow && !standaloneFallback) {
        server = new QLocalServer();
        server->setSocketOptions(QLocalServer::UserAccessOption);
        if (!server->listen(serverName)) {
            // Stale socket from a crashed previous instance — clean up and
            // retry once. Only on listen-failure do we call removeServer,
            // so a still-alive previous instance is never orphaned.
            QLocalServer::removeServer(serverName);
            if (!server->listen(serverName)) {
                fprintf(stderr,
                        "Notepatra: single-instance server bind failed: %s\n",
                        qPrintable(server->errorString()));
                delete server;
                server = nullptr;
            }
        }
    }

    // --new and the hung-primary fallback never read/write session state.
    MainWindow window(newWindow || standaloneFallback);
    if (server) server->setParent(&window);

    // D1 — CLI files + --line are applied by the deferred startup slot,
    // after the window is visible.
    window.setStartupActions(filesToOpen, gotoLine);

    window.show();

    if (server) {
        auto drainConnections = [server, &window]() {
            while (QLocalSocket *client = server->nextPendingConnection()) {
                // D6 — ACK on accept: reaching this code proves the event
                // loop is pumping, which is exactly what the secondary's
                // 3 s ACK wait disambiguates (a hung primary never accepts).
                // Write fails harmlessly if the peer already hung up.
                SingleInstance::ackClient(client);
                // D2 — readyRead-driven, size-capped accumulation; never
                // blocks the GUI thread (old code waited 500 ms per client).
                attachRemoteOpenClient(client,
                    [&window](const QStringList &paths, int gotoLine,
                              const QByteArray &startupId) {
                        window.handleRemoteOpen(paths, gotoLine, startupId);
                    });
            }
        };
        QObject::connect(server, &QLocalServer::newConnection, &window, drainConnections);
        // Drain any connections that arrived between listen() and the
        // newConnection slot wire-up above. Qt buffers pending sockets
        // until we ask, so nothing is lost — but the signal won't re-fire
        // for them on its own.
        drainConnections();
    }

    // D7 — post-show startup notices on the existing statusbar surface.
    // No modal, no new UI; queued so they run after the first paint.
    QStringList startupNotices;
    const bool standaloneMode = newWindow || standaloneFallback;
    if (standaloneFallback)
        startupNotices << QObject::tr(
            "Another Notepatra is running but not responding — opened a "
            "temporary window; your saved session is untouched.");
    // In standalone mode the crash flag belongs to the primary's next normal
    // launch — don't surface or clear it here.
    if (crashedLastRun && !standaloneMode)
        startupNotices << QObject::tr("Notepatra closed unexpectedly last time.");
    if (!cli.notFound.isEmpty())
        startupNotices << QObject::tr("Could not open: %1 (not found or not a file)")
                              .arg(cli.notFound.join(QStringLiteral(", ")));
    if (!startupNotices.isEmpty()) {
        const QString msg = startupNotices.join(QStringLiteral("  |  "));
        const bool clearFlag = crashedLastRun && !standaloneMode;
        QTimer::singleShot(0, &window, [&window, msg, crashFlagPath, clearFlag]() {
            window.statusBar()->showMessage(msg, 15000);
            // Surfaced, THEN cleared — if we crash before the event loop
            // runs this, the flag survives to the next launch.
            if (clearFlag) QFile::remove(crashFlagPath);
        });
    }

    return app.exec();
}
