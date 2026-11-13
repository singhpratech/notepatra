// SPDX-License-Identifier: GPL-3.0-or-later
//
// test_ai_search_denylist — the agent's `search` tool must apply the same
// PathDenylist as read_file and must not follow a symlink out of the
// workspace; write_file must not write (or dry-run-read) through a symlink
// that leaves the workspace or lands on a deny-listed file.
// Regression: search walked the tree with no deny-list and read through
// symlinks, returning matching LINES from .env / *.key / files outside the
// workspace.

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSet>
#include <QString>
#include <QTemporaryDir>
#include <cstdio>

#include "ai_tools.h"

static int g_pass = 0, g_fail = 0, g_skip = 0;

static void check(const char *label, bool ok) {
    if (ok) { ++g_pass; std::printf("  [PASS] %s\n", label); }
    else    { ++g_fail; std::printf("  [FAIL] %s\n", label); }
}

static void put(const QString &path, const QByteArray &bytes) {
    QDir().mkpath(QFileInfo(path).absolutePath());
    QFile f(path);
    f.open(QIODevice::WriteOnly);
    f.write(bytes);
    f.close();
}

static QByteArray get(const QString &path) {
    QFile f(path);
    return f.open(QIODevice::ReadOnly) ? f.readAll() : QByteArray("<missing>");
}

// A real filesystem symlink (QFile::link makes a .lnk shortcut on Windows).
static bool makeSymlink(const QString &target, const QString &link) {
#ifdef Q_OS_WIN
    Q_UNUSED(target); Q_UNUSED(link);
    return false;
#else
    return QFile::link(target, link) && QFileInfo(link).isSymLink();
#endif
}

int main(int argc, char *argv[]) {
    QTemporaryDir home;
    qputenv("HOME", home.path().toUtf8());
    qputenv("XDG_CONFIG_HOME", home.path().toUtf8());
    qputenv("XDG_DATA_HOME", home.path().toUtf8());
#ifdef Q_OS_WIN
    qputenv("APPDATA", home.path().toUtf8());
    qputenv("USERPROFILE", home.path().toUtf8());
#endif
    QCoreApplication app(argc, argv);
    std::printf("=== test_ai_search_denylist ===\n");

    QTemporaryDir wsDir, outDir;
    const QString ws = QDir(wsDir.path()).canonicalPath();
    const QString out = QDir(outDir.path()).canonicalPath();

    put(ws + "/notes.txt", "needle in a normal file\n");
    put(ws + "/sub/ok.txt", "needle in a subdir\n");
    put(ws + "/.env", "needle=SECRET_DOTENV\n");
    put(ws + "/config/app.env", "needle=SECRET_APPENV\n");
    put(ws + "/server.key", "needle SECRET_KEYFILE\n");
    put(ws + "/secrets.json", "{\"needle\": \"SECRET_JSON\"}\n");
    put(out + "/outside.txt", "needle SECRET_OUTSIDE\n");
    put(out + "/victim.txt", "outside original\n");

    // Symlinks: out of the workspace (file + dir), onto a deny-listed file,
    // and a harmless in-workspace alias that must still be searchable.
    const bool linksOk =
        makeSymlink(out + "/outside.txt", ws + "/link.txt")
        && makeSymlink(out, ws + "/linkdir")
        && makeSymlink(ws + "/.env", ws + "/innocent.txt")
        && makeSymlink(ws + "/notes.txt", ws + "/alias.txt")
        && makeSymlink(out + "/victim.txt", ws + "/victim_link.txt");
#ifdef Q_OS_LINUX
    // Vacuity guard: on Linux the symlink cases must actually be exercised.
    check("symlinks were created (vacuity guard)", linksOk);
#endif

    AiTools::ToolCall c;
    c.id = "s";
    c.name = "search";
    c.args["pattern"] = "needle";
    c.args["max_matches"] = 200;
    const AiTools::ToolResult r = AiTools::execute(c, ws);
    check("search succeeds", !r.isError);
    const QJsonObject body =
        QJsonDocument::fromJson(r.content.toUtf8()).object().value("result").toObject();
    QSet<QString> paths;
    for (const QJsonValue &m : body.value("matches").toArray())
        paths.insert(m.toObject().value("path").toString());
    std::printf("  matched: %s\n", qPrintable(QStringList(paths.values()).join(", ")));

    // Vacuity guard: the search really ran over the workspace.
    check("search finds the normal file", paths.contains("notes.txt"));
    check("search finds the subdir file", paths.contains("sub/ok.txt"));
    check("no deny-listed or out-of-workspace content in results",
          !r.content.contains("SECRET_"));
    check(".env not searched", !paths.contains(".env"));
    check("*.env not searched", !paths.contains("config/app.env"));
    check("*.key not searched", !paths.contains("server.key"));
    check("secrets.json not searched", !paths.contains("secrets.json"));

    if (linksOk) {
        check("symlink to a file outside the workspace not followed",
              !paths.contains("link.txt"));
        check("symlinked dir outside the workspace not followed",
              !paths.contains("linkdir/outside.txt"));
        check("symlink onto a deny-listed file not followed", !paths.contains("innocent.txt"));
        check("in-workspace symlink to a normal file still searched",
              paths.contains("alias.txt"));

        // write_file must not escape through a symlinked leaf, nor read the
        // target into a dry-run "before".
        AiTools::ToolCall w;
        w.id = "w";
        w.name = "write_file";
        w.args["path"] = "victim_link.txt";
        w.args["content"] = "overwritten\n";
        w.args["mode"] = "overwrite";
        w.args["dry_run"] = true;
        const AiTools::ToolResult dry = AiTools::execute(w, ws);
        check("write_file dry run through outside symlink refused",
              dry.isError && dry.errorKind == "outside_workspace");
        check("write_file dry run does not leak the outside file",
              !dry.content.contains("outside original"));
        w.args.remove("dry_run");
        const AiTools::ToolResult real = AiTools::execute(w, ws);
        check("write_file through outside symlink refused", real.isError);
        check("outside file untouched", get(out + "/victim.txt") == "outside original\n");

        w.args["path"] = "innocent.txt";
        w.args["mode"] = "append";
        const AiTools::ToolResult den = AiTools::execute(w, ws);
        check("write_file through symlink onto .env refused as denied",
              den.isError && den.errorKind == "denied");
        check(".env untouched", get(ws + "/.env") == "needle=SECRET_DOTENV\n");
    } else {
        ++g_skip;
        std::printf("  [SKIP] symlink cases — symlinks unavailable on this platform\n");
    }

    std::printf("\n=== Summary: %d passed, %d failed, %d skipped ===\n", g_pass, g_fail, g_skip);
    return g_fail == 0 ? 0 : 1;
}
