#include <QApplication>
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
#include "mainwindow.h"
#include "editor.h"
#include "config.h"
#include "fonts.h"

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

int main(int argc, char *argv[]) {
    // Handle --version and --help before creating QApplication
    for (int i = 1; i < argc; i++) {
        QString arg = QString::fromUtf8(argv[i]);
        if (arg == "--version" || arg == "-v") {
            printf("Notepatra v%s\n", NOTEPATRA_VERSION);
            printf("Native C++/Rust code editor\n");
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

    // Install crash handlers
    signal(SIGSEGV, crashHandler);
    signal(SIGABRT, crashHandler);
    signal(SIGFPE, crashHandler);

    QApplication app(argc, argv);
    app.setApplicationName("Notepatra");
    app.setOrganizationName("Notepatra");
    app.setApplicationVersion(NOTEPATRA_VERSION);
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
            const QByteArray body = QJsonDocument(payload).toJson(QJsonDocument::Compact);
            probe.write(body);
            probe.flush();
            probe.waitForBytesWritten(500);
            probe.disconnectFromServer();
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
                        window.handleRemoteOpen(paths, line);
                    }
                    client->disconnectFromServer();
                }
            });
        }
    }

    window.show();
    return app.exec();
}
