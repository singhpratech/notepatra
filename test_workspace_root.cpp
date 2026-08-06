// SPDX-License-Identifier: GPL-3.0-or-later
//
// FileExplorer draws a folder tree AND answers "what is the user working on".
// Those are different questions, and answering the second with the first caused
// a privacy bug: search_project over MCP treated the tree's display folder as a
// workspace. With no folder open that folder is $HOME, so a one-word search
// walked the user's entire profile and returned line-level content from files
// they had never opened in the editor — in the report that found this, that
// included the transcript of the session driving the tool.
//
// The same single default silently disarmed three other things that were all
// written to test `rootPath().isEmpty()` and therefore could never fire:
//   * the Coding Mode "open a folder" prompt,
//   * the AI CSV sandbox root (a SECURITY guard — it was rooted at $HOME
//     instead of being closed),
//   * the Find-in-Files default folder, whose own comment says walking $HOME
//     is "~always wrong".
//
// So the invariant under test is narrow and load-bearing: workspaceRoot() is
// empty until the user actually opens a folder, whatever the tree is showing.

#include "fileexplorer.h"

#include <QApplication>
#include <QComboBox>
#include <QLineEdit>
#include <QtTest/QtTest>
#include <QDir>
#include <QTemporaryDir>

#include <cstdio>

static int g_pass = 0, g_fail = 0;
#define EXPECT(label, cond)                                                    \
    do {                                                                       \
        if (cond) { ++g_pass; std::printf("  [PASS] %s\n", label); }           \
        else      { ++g_fail; std::printf("  [FAIL] %s\n", label); }           \
    } while (0)

int main(int argc, char *argv[]) {
    // Isolate config/data writes: QStandardPaths must never touch the real
    // profile from a test run.
    QTemporaryDir home;
    qputenv("XDG_CONFIG_HOME", home.path().toUtf8());
    qputenv("XDG_DATA_HOME", home.path().toUtf8());
    qputenv("QT_QPA_PLATFORM", "offscreen");

    QApplication app(argc, argv);

    QTemporaryDir workspace;
    if (!workspace.isValid()) {
        std::printf("  [FAIL] could not create a temp workspace\n");
        return 1;
    }

    // ── 1. Fresh explorer: showing something, scoping nothing ─────────────
    {
        FileExplorer ex;

        // Vacuity guard. If rootPath() were empty the workspaceRoot() assertion
        // below would pass for the wrong reason and this test would be worth
        // nothing — so prove the display root really is populated first.
        EXPECT("precondition: a fresh explorer HAS a display root",
               !ex.rootPath().isEmpty());
        EXPECT("precondition: that display root is the home directory",
               QDir(ex.rootPath()).absolutePath() ==
                   QDir(QDir::homePath()).absolutePath());

        // The actual regression guard.
        EXPECT("a fresh explorer scopes NO workspace",
               ex.workspaceRoot().isEmpty());
    }

    // ── 2. After the user opens a folder, it IS the workspace ─────────────
    {
        FileExplorer ex;
        ex.setRoot(workspace.path());

        EXPECT("opening a folder sets the workspace root",
               !ex.workspaceRoot().isEmpty());
        EXPECT("the workspace root is the folder that was opened",
               QDir(ex.workspaceRoot()).absolutePath() ==
                   QDir(workspace.path()).absolutePath());
        EXPECT("display root and workspace root now agree",
               QDir(ex.rootPath()).absolutePath() ==
                   QDir(ex.workspaceRoot()).absolutePath());
    }

    // ── 3. A rejected setRoot must not fabricate a workspace ──────────────
    //
    // setRoot() ignores anything that is not a directory. It must also leave
    // the workspace unset, rather than marking it explicit while the path
    // silently stays at $HOME — that would reintroduce the original bug
    // through a different door.
    {
        FileExplorer ex;
        ex.setRoot(workspace.path() + QStringLiteral("/does-not-exist"));
        EXPECT("a non-existent path does not become the workspace",
               ex.workspaceRoot().isEmpty());

        const QString file = workspace.path() + QStringLiteral("/a-file.txt");
        QFile f(file);
        if (f.open(QIODevice::WriteOnly)) { f.write("x"); f.close(); }
        ex.setRoot(file);
        EXPECT("a FILE does not become the workspace",
               ex.workspaceRoot().isEmpty());
    }

    // ── 4. Typing must NOT latch a workspace ──────────────────────────────
    //
    // The path box is an editable QComboBox. Latching on currentTextChanged
    // meant every KEYSTROKE committed a workspace: typing a path that passes
    // through "/" made "/" the user's explicit workspace, permanently, which
    // then anchors the AI file sandbox at the filesystem root and hands
    // search_project the whole disk. Only a committed choice may latch.
    {
        FileExplorer ex;
        auto *combo = ex.findChild<QComboBox *>();
        EXPECT("precondition: the path box is an editable combo",
               combo != nullptr && combo->isEditable());
        if (combo && combo->lineEdit()) {
            // Clear first: the box is pre-filled with the home path, so typing
            // straight in appends and yields a path that does not exist — which
            // would make the "does not latch" assertion pass for the wrong
            // reason and prove nothing.
            combo->lineEdit()->clear();
            combo->lineEdit()->setFocus();

            // Type a REAL directory one character at a time. Every intermediate
            // prefix that happens to be a directory (notably the leading "/")
            // used to latch immediately.
            QTest::keyClicks(combo->lineEdit(), workspace.path());
            EXPECT("precondition: the typed text really is an existing dir",
                   QFileInfo(combo->lineEdit()->text()).isDir());
            EXPECT("typing alone does not latch a workspace",
                   ex.workspaceRoot().isEmpty());

            // Committing with Enter is a deliberate act, so it may latch.
            QTest::keyClick(combo->lineEdit(), Qt::Key_Return);
            EXPECT("pressing Enter DOES commit the typed folder",
                   QDir(ex.workspaceRoot()).absolutePath() ==
                       QDir(workspace.path()).absolutePath());
        }
    }

    std::printf("\n%d passed, %d failed\n", g_pass, g_fail);
    return g_fail == 0 ? 0 : 1;
}
