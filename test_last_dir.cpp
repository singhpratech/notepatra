/**
 * Regression test for the "remember the last Save/Open directory" behaviour
 * added after users reported the Save dialog always snapping back to the home
 * folder on Windows (and, it turned out, every platform). The user-visible
 * contract lives in the pure Config helpers exercised here — the dialogs just
 * feed lastDirOrHome() in and setLastDir()/noteLastDir() out:
 *
 *   setLastDir(path)     — extract + remember the directory of a file/dir path,
 *                          rejecting empty or no-longer-existing locations.
 *   lastDirOrHome()      — start directory: remembered dir if it still exists,
 *                          else the home folder (so a fresh install is unchanged).
 *
 * No widgets, no event loop, no config-file writes: setLastDir() and
 * lastDirOrHome() never touch disk, so this stays hermetic and cross-platform.
 */

#include "src/config.h"

#include <QCoreApplication>
#include <QDir>
#include <QTemporaryDir>
#include <QString>

#include <cstdio>

static int g_pass = 0, g_fail = 0;
static void check(const char *what, bool ok, const QString &detail = {}) {
    if (ok) { std::printf("  [PASS] %s\n", what); ++g_pass; }
    else {
        std::printf("  [FAIL] %s%s%s\n", what,
                    detail.isEmpty() ? "" : " — ",
                    detail.toUtf8().constData());
        ++g_fail;
    }
}

int main(int argc, char **argv) {
    QCoreApplication app(argc, argv);
    std::printf("=== last-directory memory tests ===\n\n");

    Config &c = Config::instance();
    const QString home = QDir::homePath();

    // ── fresh state falls back to home ──
    std::printf("— fallback ───────────────────────────────────────\n");
    {
        c.lastDir.clear();
        check("empty lastDir → home", c.lastDirOrHome() == home,
              c.lastDirOrHome());
    }

    // ── setLastDir extracts the folder from a file path ──
    std::printf("\n— extract from a file path ───────────────────────\n");
    {
        QTemporaryDir tmp;
        check("temp dir created", tmp.isValid());
        const QString dir = QDir(tmp.path()).absolutePath();
        const QString file = dir + "/notes.txt";
        // The file need not exist — only its PARENT directory must — because a
        // Save As targets a not-yet-written path inside an existing folder.
        c.lastDir.clear();
        c.setLastDir(file);
        check("file path → its parent directory remembered",
              c.lastDir == dir, c.lastDir);
        check("remembered dir is returned by lastDirOrHome",
              c.lastDirOrHome() == dir, c.lastDirOrHome());
    }

    // ── setLastDir accepts a directory path directly ──
    std::printf("\n— accept a directory path ────────────────────────\n");
    {
        QTemporaryDir tmp;
        const QString dir = QDir(tmp.path()).absolutePath();
        c.lastDir.clear();
        c.setLastDir(dir);
        check("directory path → itself remembered", c.lastDir == dir, c.lastDir);
    }

    // ── rejects junk so a dialog is never poisoned ──
    std::printf("\n— reject bad input ───────────────────────────────\n");
    {
        c.lastDir = home;
        c.setLastDir(QString());
        check("empty string is ignored (lastDir unchanged)", c.lastDir == home);

        c.setLastDir("");
        check("empty literal is ignored", c.lastDir == home);

        // A path whose parent directory does not exist (deleted folder /
        // unplugged removable drive) must NOT overwrite the good value.
        c.setLastDir("/no/such/folder/anywhere/file.txt");
        check("nonexistent parent is ignored (deleted-folder guard)",
              c.lastDir == home, c.lastDir);
    }

    // ── lastDirOrHome self-heals when the remembered dir disappears ──
    std::printf("\n— self-heal on vanished dir ──────────────────────\n");
    {
        QString goneDir;
        {
            QTemporaryDir tmp;
            goneDir = QDir(tmp.path()).absolutePath();
            c.lastDir.clear();
            c.setLastDir(goneDir);
            check("dir remembered while it exists", c.lastDir == goneDir);
        } // tmp destructed here → goneDir removed from disk
        check("vanished dir → falls back to home",
              c.lastDirOrHome() == home, c.lastDirOrHome());
        check("but the stale value is retained for a possible reappearance",
              c.lastDir == goneDir);
    }

    std::printf("\n=== Summary: %d passed, %d failed ===\n", g_pass, g_fail);
    return g_fail == 0 ? 0 : 1;
}
