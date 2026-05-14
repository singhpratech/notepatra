#include <QApplication>
#include <QGuiApplication>
#include <QFileInfo>
#include <QMessageBox>
#include <QLocalServer>
#include <QLocalSocket>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QCryptographicHash>
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
#include "editor.h"
#include "config.h"
#include "fonts.h"
#include "fontpack.h"

// NOTEPATRA_VERSION is injected at compile time from CMakeLists.txt's
// project(Notepatra VERSION X.Y.Z ...) so a single bump in CMake propagates
// to --version, --help, app version, .desktop file, etc. without needing
// to sed across multiple source files.
#ifndef NOTEPATRA_VERSION
#define NOTEPATRA_VERSION "0.0.0-dev"
#endif

// Global crash handler — saves recovery data before dying
static void crashHandler(int sig) {
    // Try to save recovery info
    fprintf(stderr, "Notepatra: caught signal %d, attempting recovery save...\n", sig);
    // Write crash flag
    QString dir = QDir::homePath() + "/.config/notepatra/recovery";
    QDir().mkpath(dir);
    QFile flag(dir + "/.crash_flag");
    if (flag.open(QIODevice::WriteOnly)) flag.write("crashed");
    // Re-raise to get default behavior (core dump etc)
    signal(sig, SIG_DFL);
    raise(sig);
}

// Per-user local-IPC name. Hashing the home path keeps the socket distinct
// across user accounts on the same box (and across WSL / native Windows
// sessions, where %USERNAME% can collide). Keep it short — Windows caps
// named-pipe names at 256 chars but older Qt on Linux uses
// /tmp/<name> and some filesystems are fussy.
static QString singleInstanceServerName() {
    const QByteArray salt = QDir::homePath().toUtf8();
    const QByteArray h = QCryptographicHash::hash(salt, QCryptographicHash::Sha1).toHex();
    return QStringLiteral("notepatra-") + QString::fromLatin1(h.left(16));
}

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
#ifdef NOTEPATRA_NO_CLOUD
            printf("Notepatra v%s (cloud-free / local-ai)\n", NOTEPATRA_VERSION);
            printf("Native C++/Rust code editor — local & private-network LLM endpoints only\n");
#else
            printf("Notepatra v%s\n", NOTEPATRA_VERSION);
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

    // Install crash handlers
    signal(SIGSEGV, crashHandler);
    signal(SIGABRT, crashHandler);
    signal(SIGFPE, crashHandler);

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
    app.setApplicationName("Notepatra");
    app.setOrganizationName("Notepatra");
    app.setApplicationVersion(NOTEPATRA_VERSION);

    // v0.1.75 — register every user-installed runtime font BEFORE we
    // pick the default UI / code family. notepatraDefaultUiFamily() and
    // notepatraDefaultCodeFamily() walk QFontDatabase().families() in
    // priority order, so any TTF / OTF the user has downloaded into the
    // font-pack directory becomes immediately available, no restart.
    NotepatraFontPack::loadInstalledFonts();

    app.setFont(notepatraUiFont());

    // Parse remaining args
    int gotoLine = -1;
    QString themeOverride;
    bool newWindow = false;
    QStringList filesToOpen;

    for (int i = 1; i < argc; i++) {
        QString arg = QString::fromUtf8(argv[i]);
        if (arg == "--line" && i + 1 < argc) {
            gotoLine = QString::fromUtf8(argv[++i]).toInt();
        } else if (arg == "--theme" && i + 1 < argc) {
            themeOverride = QString::fromUtf8(argv[++i]);
        } else if (arg == "-n" || arg == "--new") {
            newWindow = true;
        } else if (!arg.startsWith("-")) {
            if (QFileInfo(arg).isFile())
                filesToOpen.append(QFileInfo(arg).absoluteFilePath());
        }
    }

    // ─── Single-instance bridge ───
    // If another Notepatra is already running for this user and the caller
    // didn't pass --new, forward our args into it and exit. This is the
    // fix for "Windows double-click spawns a fresh clone per file" — the
    // shell verb invokes notepatra.exe with the file path, we hand it to
    // the running instance, and the user sees a new tab instead of a new
    // window.
    const QString serverName = singleInstanceServerName();
    if (!newWindow) {
        QLocalSocket probe;
        probe.connectToServer(serverName);
        if (probe.waitForConnected(300)) {
            QJsonObject payload;
            QJsonArray arr;
            for (const QString &p : filesToOpen) arr.append(p);
            payload.insert("files", arr);
            payload.insert("gotoLine", gotoLine);
            // Forward DESKTOP_STARTUP_ID (captured at main() entry before
            // Qt could unset it) so the running instance can set
            // _NET_STARTUP_ID on its window — that's what Cinnamon's panel
            // matches to stop the spinner, and what Muffin uses to treat
            // the activate request as user-initiated.
#ifdef Q_OS_LINUX
            if (!capturedStartupId.isEmpty()) {
                payload.insert("startupId",
                               QString::fromUtf8(capturedStartupId));
            }
#endif
            const QByteArray body = QJsonDocument(payload).toJson(QJsonDocument::Compact);
            probe.write(body);
            probe.flush();
            probe.waitForBytesWritten(500);
            probe.disconnectFromServer();
#ifdef Q_OS_WIN
            // Windows blocks SetForegroundWindow() in the running instance
            // unless a process with foreground rights grants permission.
            // Explorer hands those rights to *this* (newly spawned) process,
            // not to the running one — so we surrender them to anyone
            // (ASFW_ANY = -1) before exiting. Without this, the running
            // instance opens the file but only flashes its taskbar button.
            ::AllowSetForegroundWindow(ASFW_ANY);
#endif
#ifdef Q_OS_LINUX
            // Cancel the desktop-environment launch-feedback spinner so the
            // user isn't stuck staring at a busy cursor after we forwarded
            // the file path. No-op when DESKTOP_STARTUP_ID isn't set (CLI
            // invocations, terminal launches, Wayland-only sessions).
            // Use captured value since Qt already cleared the env var.
            sendStartupNotifyComplete(
                capturedStartupId.isEmpty() ? nullptr
                                            : capturedStartupId.constData());
#endif
            return 0;
        }
    }

    // Apply theme override before window creation
    if (!themeOverride.isEmpty()) {
        Config::instance().theme = themeOverride;
    }

    MainWindow window;

    // Open files from command line
    for (const auto &path : filesToOpen)
        window.openFile(path);

    // Go to line if specified
    if (gotoLine > 0) {
        if (auto *e = window.currentEditor())
            e->gotoLine(gotoLine);
    }

    // Start the local server that future invocations will connect to.
    // removeServer() clears a stale socket file from a previous crash —
    // without it, listen() fails with AddressInUseError on Linux.
    QLocalServer *server = nullptr;
    if (!newWindow) {
        QLocalServer::removeServer(serverName);
        server = new QLocalServer(&window);
        // SocketOption::UserAccessOption keeps the socket readable only by
        // the current user on platforms that honour it.
        server->setSocketOptions(QLocalServer::UserAccessOption);
        if (server->listen(serverName)) {
            QObject::connect(server, &QLocalServer::newConnection, &window, [server, &window]() {
                while (QLocalSocket *client = server->nextPendingConnection()) {
                    QObject::connect(client, &QLocalSocket::disconnected,
                                     client, &QLocalSocket::deleteLater);
                    // Wait briefly for the peer to send its JSON — the
                    // caller in the if(waitForConnected) branch above is
                    // synchronous so this is usually available on first
                    // read.
                    client->waitForReadyRead(500);
                    const QByteArray body = client->readAll();
                    const QJsonDocument doc = QJsonDocument::fromJson(body);
                    if (doc.isObject()) {
                        const QJsonObject o = doc.object();
                        QStringList paths;
                        for (const QJsonValue &v : o.value("files").toArray())
                            paths.append(v.toString());
                        const int line = o.value("gotoLine").toInt(-1);
                        const QByteArray sid =
                            o.value("startupId").toString().toUtf8();
                        window.handleRemoteOpen(paths, line, sid);
                    }
                    client->disconnectFromServer();
                }
            });
        }
    }

    window.show();
    return app.exec();
}
