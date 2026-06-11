// D7 Part A — parseCliArgs contract (src/cliargs.cpp).
//
// main.cpp used to decode argv with QString::fromUtf8(argv[i]); on Windows
// the CRT hands a WIN32-subsystem app CP_ACP bytes, so every non-ASCII path
// was mangled, QFileInfo(arg).isFile() failed, and the file silently never
// opened (the ghost-open). The fix routes QCoreApplication::arguments()
// (rebuilt from GetCommandLineW on Windows — true UTF-16) into the extracted
// parseCliArgs(). This test pins the parse contract on that post-arguments()
// QStringList; the GetCommandLineW rebuild itself is Qt's documented 5.15
// behavior and is not re-tested here.
//
// NOTE: this source file is UTF-8; non-ASCII names use u8"" literals so the
// bytes handed to QString::fromUtf8 are unambiguous regardless of the
// compiler's source/execution charset (MSVC without /utf-8 is the hazard).

#include "cliargs.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QString>
#include <QStringList>
#include <QTemporaryDir>

#include <cstdio>

static int g_pass = 0, g_fail = 0;
#define EXPECT(label, cond) \
    do { if (cond) { ++g_pass; std::printf("  [PASS] %s\n", label); } \
         else      { ++g_fail; std::printf("  [FAIL] %s\n", label); } } while (0)

static bool makeFile(const QString &path, const QByteArray &content) {
    QFile f(path);
    if (!f.open(QIODevice::WriteOnly)) return false;
    f.write(content);
    f.close();
    return true;
}

int main(int argc, char *argv[]) {
    QCoreApplication app(argc, argv);

    std::printf("=== test_cli_args ===\n\n");
    fflush(stdout);

    // ── Workspace: plain, non-ASCII, CJK files; a subdirectory; a ghost ──
    QTemporaryDir wd;
    EXPECT("temp workspace created", wd.isValid());

    const QString plain = wd.path() + QStringLiteral("/plain.sql");
    // QFile uses wide APIs internally on Windows, so creating these succeeds
    // even when the ANSI codepage can't represent the characters.
    const QString nonAscii =
        wd.path() + QStringLiteral("/") +
        QString::fromUtf8(u8"héllo wörld – тест.sql");
    const QString cjk =
        wd.path() + QStringLiteral("/") +
        QString::fromUtf8(u8"日本語ノート.sql");
    const QString dirPath = wd.path() + QStringLiteral("/adir");
    const QString missing = wd.path() + QStringLiteral("/gone.sql");

    EXPECT("created plain.sql", makeFile(plain, "select 1;\n"));
    EXPECT("created non-ASCII file", makeFile(nonAscii, "select 2;\n"));
    EXPECT("created CJK file", makeFile(cjk, "select 3;\n"));
    EXPECT("created subdirectory adir", QDir().mkpath(dirPath));
    EXPECT("gone.sql really absent", !QFileInfo::exists(missing));

    // ── Non-ASCII path resolves into .files as absoluteFilePath ──
    {
        const CliArgs r = parseCliArgs({QStringLiteral("prog"), nonAscii});
        EXPECT("non-ASCII path lands in files",
               r.files == QStringList{QFileInfo(nonAscii).absoluteFilePath()});
        EXPECT("non-ASCII path: notFound empty", r.notFound.isEmpty());
    }

    // ── CJK path resolves the same way ──
    {
        const CliArgs r = parseCliArgs({QStringLiteral("prog"), cjk});
        EXPECT("CJK path lands in files",
               r.files == QStringList{QFileInfo(cjk).absoluteFilePath()});
        EXPECT("CJK path: notFound empty", r.notFound.isEmpty());
    }

    // ── --line 42 with a file ──
    {
        const CliArgs r = parseCliArgs({QStringLiteral("prog"),
                                        QStringLiteral("--line"),
                                        QStringLiteral("42"), plain});
        EXPECT("--line 42 parsed", r.gotoLine == 42);
        EXPECT("--line consumes its value; file still resolved",
               r.files == QStringList{QFileInfo(plain).absoluteFilePath()});
        EXPECT("--line run: notFound empty", r.notFound.isEmpty());
    }

    // ── --theme Dark ──
    {
        const CliArgs r = parseCliArgs({QStringLiteral("prog"),
                                        QStringLiteral("--theme"),
                                        QStringLiteral("Dark")});
        EXPECT("--theme Dark parsed", r.theme == QStringLiteral("Dark"));
        EXPECT("--theme value not misread as a file",
               r.files.isEmpty() && r.notFound.isEmpty());
    }

    // ── -n and --new ──
    {
        const CliArgs r = parseCliArgs({QStringLiteral("prog"),
                                        QStringLiteral("-n")});
        EXPECT("-n sets newWindow", r.newWindow);
    }
    {
        const CliArgs r = parseCliArgs({QStringLiteral("prog"),
                                        QStringLiteral("--new")});
        EXPECT("--new sets newWindow", r.newWindow);
    }

    // ── Missing path → notFound (no longer silent — caller surfaces it) ──
    {
        const CliArgs r = parseCliArgs({QStringLiteral("prog"), missing});
        EXPECT("missing path goes to notFound",
               r.notFound == QStringList{missing});
        EXPECT("missing path: files empty", r.files.isEmpty());
    }

    // ── Directory → notFound (directories are not openable files) ──
    {
        const CliArgs r = parseCliArgs({QStringLiteral("prog"), dirPath});
        EXPECT("directory goes to notFound", r.notFound.contains(dirPath));
        EXPECT("directory: files empty", r.files.isEmpty());
    }

    // ── Unknown "-" flags stay silently ignored (also covers -psn_*) ──
    {
        const CliArgs r = parseCliArgs({QStringLiteral("prog"),
                                        QStringLiteral("--unknown-flag")});
        EXPECT("--unknown-flag: files empty", r.files.isEmpty());
        EXPECT("--unknown-flag: notFound empty", r.notFound.isEmpty());
        EXPECT("--unknown-flag: gotoLine default", r.gotoLine == -1);
        EXPECT("--unknown-flag: theme default", r.theme.isEmpty());
        EXPECT("--unknown-flag: newWindow default", !r.newWindow);
    }

    // ── Trailing --line without a value → gotoLine stays -1, no crash ──
    {
        const CliArgs r = parseCliArgs({QStringLiteral("prog"),
                                        QStringLiteral("--line")});
        EXPECT("trailing --line leaves gotoLine at -1", r.gotoLine == -1);
        EXPECT("trailing --line: nothing else set",
               r.files.isEmpty() && r.notFound.isEmpty());
    }

    // ── `--line file.py` (forgotten N) must NOT eat the file ──
    {
        const CliArgs r = parseCliArgs({QStringLiteral("prog"),
                                        QStringLiteral("--line"), plain});
        EXPECT("--line <non-numeric>: gotoLine stays -1", r.gotoLine == -1);
        EXPECT("--line <non-numeric>: the file is still opened",
               r.files == QStringList{QFileInfo(plain).absoluteFilePath()});
    }

    // ── --line boundary values: 0 and negatives parse + consume; jump
    //    suppression happens at the consumer's `> 0` guard ──
    {
        const CliArgs r = parseCliArgs({QStringLiteral("prog"),
                                        QStringLiteral("--line"),
                                        QStringLiteral("0"), plain});
        EXPECT("--line 0: parsed as 0", r.gotoLine == 0);
        EXPECT("--line 0: file still opened",
               r.files == QStringList{QFileInfo(plain).absoluteFilePath()});
        EXPECT("--line 0: notFound empty", r.notFound.isEmpty());
    }
    {
        const CliArgs r = parseCliArgs({QStringLiteral("prog"),
                                        QStringLiteral("--line"),
                                        QStringLiteral("-5"), plain});
        EXPECT("--line -5: parsed as -5", r.gotoLine == -5);
        EXPECT("--line -5: file still opened",
               r.files == QStringList{QFileInfo(plain).absoluteFilePath()});
        EXPECT("--line -5: notFound empty", r.notFound.isEmpty());
    }

    // ── Relative NOT-FOUND arg absolutizes against OUR cwd — forwarded
    //    payloads must never be re-resolved against the primary's cwd ──
    {
        EXPECT("chdir into workspace (notFound case)",
               QDir::setCurrent(wd.path()));
        const CliArgs r = parseCliArgs({QStringLiteral("prog"),
                                        QStringLiteral("ghost-rel.sql")});
        EXPECT("relative missing arg lands in notFound (one entry)",
               r.notFound.size() == 1);
        if (r.notFound.size() == 1) {
            EXPECT("relative missing arg became absolute",
                   QFileInfo(r.notFound.first()).isAbsolute());
            // Compare against currentPath() (not wd.path()) — setCurrent
            // can land on a symlink-resolved variant of the temp dir.
            EXPECT("relative missing arg resolved against secondary cwd",
                   r.notFound.first() ==
                       QDir::currentPath() + QStringLiteral("/ghost-rel.sql"));
        }
    }

    // ── Relative path resolves to an absolute path ──
    {
        EXPECT("chdir into workspace", QDir::setCurrent(wd.path()));
        const CliArgs r = parseCliArgs({QStringLiteral("prog"),
                                        QStringLiteral("plain.sql")});
        EXPECT("relative path resolved (one file)", r.files.size() == 1);
        if (r.files.size() == 1) {
            EXPECT("relative path became absolute",
                   QFileInfo(r.files.first()).isAbsolute());
            EXPECT("relative path ends in plain.sql",
                   r.files.first().endsWith(QStringLiteral("plain.sql")));
        }
        EXPECT("relative path: notFound empty", r.notFound.isEmpty());
    }

    std::printf("\n%d passed, %d failed\n", g_pass, g_fail);
    fflush(stdout);
    return g_fail;
}
