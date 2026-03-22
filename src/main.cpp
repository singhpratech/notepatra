#include <QApplication>
#include <QFileInfo>
#include <QMessageBox>
#include <cstdio>
#include <csignal>
#include <exception>
#include "mainwindow.h"
#include "editor.h"
#include "config.h"

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

int main(int argc, char *argv[]) {
    // Handle --version and --help before creating QApplication
    for (int i = 1; i < argc; i++) {
        QString arg = QString::fromUtf8(argv[i]);
        if (arg == "--version" || arg == "-v") {
            printf("Notepatra v0.1.0\n");
            printf("Native C++/Rust code editor\n");
            printf("https://github.com/singhpratech/notepatra\n");
            return 0;
        }
        if (arg == "--help" || arg == "-h") {
            printf("Notepatra v0.1.0 — Native code editor for Linux\n\n");
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
    app.setApplicationVersion("0.1.0");

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

    window.show();
    return app.exec();
}
