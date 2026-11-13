// v0.1.133 — Noter must say so when the OS blocks its folder.
//
// Bug: with macOS Documents access denied (TCC) the note files exist, but the
// directory listing comes back empty, so Noter showed an empty list and MCP
// list_notes answered {"notes": []} as a success. Now
// NotesStorage::accessBlockedMessage() opens the root and reports a
// permission failure, and the Noter empty page shows that text in place of
// the keyboard hint. Absent (Noter never used) and empty roots are not errors.
// The MCP side is covered in test_mcp_bridge (list_notes_reports_unreadable_root).

#include "notes.h"
#include "notes_storage.h"

#include <QApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QLabel>
#include <QStandardPaths>
#include <QTemporaryDir>

#include <cstdio>

static int g_pass = 0, g_fail = 0;
#define EXPECT(label, cond) \
    do { if (cond) { ++g_pass; std::printf("  [PASS] %s\n", label); } \
         else      { ++g_fail; std::printf("  [FAIL] %s\n", label); } \
         fflush(stdout); } while (0)

static QString hintText(NotesPanel *panel) {
    auto *hint = panel->findChild<QLabel *>(QStringLiteral("noterEmptyHint"));
    return hint ? hint->text() : QStringLiteral("<no hint label>");
}

int main(int argc, char *argv[]) {
    QTemporaryDir home;
    qputenv("HOME", home.path().toUtf8());
    qputenv("XDG_CONFIG_HOME", (home.path() + QStringLiteral("/cfg")).toUtf8());
    qputenv("XDG_DATA_HOME", (home.path() + QStringLiteral("/data")).toUtf8());
#ifdef Q_OS_WIN
    qputenv("APPDATA", (home.path() + QStringLiteral("/cfg")).toUtf8());
    qputenv("USERPROFILE", home.path().toUtf8());
#endif
    qputenv("QT_QPA_PLATFORM", "offscreen");
    QApplication app(argc, argv);
    std::printf("=== test_noter_access_notice ===\n\n");
    fflush(stdout);

    std::printf("-- accessBlockedMessage: absent and empty roots\n");
    QTemporaryDir scratch;
    const QString absent = scratch.path() + QStringLiteral("/Noter");
    EXPECT("absent root: no message", NotesStorage::accessBlockedMessage(absent).isEmpty());
    EXPECT("absent root: not created by the probe", !QFileInfo::exists(absent));
    EXPECT("empty root created", QDir().mkpath(absent));
    EXPECT("empty root: no message", NotesStorage::accessBlockedMessage(absent).isEmpty());
    EXPECT("empty string root: no message", NotesStorage::accessBlockedMessage(QString()).isEmpty());

    const QString docs =
        QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation);
    if (!QDir::cleanPath(docs).startsWith(QDir::cleanPath(home.path()) + QLatin1Char('/'))) {
        std::printf("  [SKIP] panel phases: Documents (%s) is outside the temp HOME\n",
                    qPrintable(QDir::toNativeSeparators(docs)));
        std::printf("\n%d passed, %d failed\n", g_pass, g_fail);
        return g_fail;
    }
    const QString root = NotesPanel::defaultNotesFolder();

    std::printf("-- panel on a readable root\n");
    {
        NotesPanel panel;
        panel.show();
        QApplication::processEvents();
        const QString t = hintText(&panel);
        EXPECT("readable root: keyboard hint shown", t.contains(QLatin1String("Ctrl+Alt+M")));
        EXPECT("readable root: no blocked notice",
               !t.contains(QLatin1String("blocked access")));
    }

#ifndef Q_OS_WIN
    std::printf("-- panel on a root the OS will not open (chmod 000)\n");
    const QString notePath = root + QStringLiteral("/Inbox/20260101-1200-locked.html");
    {
        QFile f(notePath);
        EXPECT("seeded a note", f.open(QIODevice::WriteOnly) &&
                                    f.write("<html><body><h1>Locked</h1></body></html>") > 0);
    }
    const QFileDevice::Permissions saved = QFile(root).permissions();
    EXPECT("chmod 000 on the Noter root", QFile(root).setPermissions(QFileDevice::Permissions()));
    const bool reallyLocked =
        QDir(root).entryList(QDir::AllEntries | QDir::NoDotAndDotDot).isEmpty() &&
        !QFile(notePath).open(QIODevice::ReadOnly);
    if (!reallyLocked) {
        std::printf("  [SKIP] root still readable after chmod 000 (running as root?)\n");
    } else {
        const QString msg = NotesStorage::accessBlockedMessage(root);
        EXPECT("probe reports the blocked root",
               msg.contains(QLatin1String("The system blocked access to the Noter folder")));
        EXPECT("message names the path", msg.contains(QDir::toNativeSeparators(root)));
#ifdef Q_OS_MAC
        EXPECT("macOS: settings hint",
               msg.contains(QLatin1String("Privacy & Security > Files and Folders")));
#endif
        std::printf("  message: %s\n", qPrintable(msg));
        {
            NotesPanel panel;
            panel.show();
            QApplication::processEvents();
            const QString t = hintText(&panel);
            EXPECT("blocked root: empty page shows the notice", t == msg);
            auto *hint = panel.findChild<QLabel *>(QStringLiteral("noterEmptyHint"));
            EXPECT("blocked root: notice label visible",
                   hint && hint->isVisibleTo(&panel));

            QFile(root).setPermissions(saved);
            panel.refreshFromDisk();
            EXPECT("access restored: keyboard hint back",
                   hintText(&panel).contains(QLatin1String("Ctrl+Alt+M")));
        }
    }
    QFile(root).setPermissions(saved);
#endif

    std::printf("\n%d passed, %d failed\n", g_pass, g_fail);
    fflush(stdout);
    return g_fail;
}
