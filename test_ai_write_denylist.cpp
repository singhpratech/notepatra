// SPDX-License-Identifier: GPL-3.0-or-later
//
// test_ai_write_denylist — one write rule for every AI writer (v0.1.133 review).
//
//   R3  write_file / apply_diff applied only the secret-READ list, so the agent
//       could write .git/config (core.fsmonitor / core.hooksPath run on the
//       app's next `git status`), .git/hooks/*, or a shell rc file reached
//       through a workspace symlink. The deny-list itself missed .git/config,
//       .git/info/, submodule / worktree config and the XDG git config.
//   R4  the app's own settings (config.json holds cloud API keys,
//       db-connections.json saved passwords) were on neither list.
//   (The temp-file-through-a-symlink case lives in test_agent_edit_encoding.)
//
// Refusals are checked on the dry-run preview too: that preview is what the
// Agent-mode approval card is built from, so a refusal there means no card.

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QStandardPaths>
#include <QString>
#include <QTemporaryDir>
#include <cstdio>

#include "ai_tools.h"
#include "config.h"
#include "path_denylist.h"

static int g_pass = 0, g_fail = 0;

static void check(const QString &label, bool ok) {
    if (ok) { ++g_pass; std::printf("  [PASS] %s\n", qPrintable(label)); }
    else    { ++g_fail; std::printf("  [FAIL] %s\n", qPrintable(label)); }
}

static QString g_ws;

static void put(const QString &abs, const QByteArray &bytes) {
    QDir().mkpath(QFileInfo(abs).absolutePath());
    QFile f(abs);
    f.open(QIODevice::WriteOnly);
    f.write(bytes);
    f.close();
}

static QByteArray get(const QString &abs) {
    QFile f(abs);
    if (!f.open(QIODevice::ReadOnly)) return QByteArray("<missing>");
    return f.readAll();
}

static AiTools::ToolResult writeFile(const QString &ws, const QString &path,
                                     const QString &content, bool dryRun) {
    AiTools::ToolCall c;
    c.id = "t";
    c.name = "write_file";
    c.args["path"] = path;
    c.args["content"] = content;
    c.args["mode"] = "overwrite";
    if (dryRun) c.args["dry_run"] = true;
    return AiTools::execute(c, ws);
}

static AiTools::ToolResult applyDiff(const QString &ws, const QString &path, int line,
                                     const QString &oldLines, const QString &newLines,
                                     bool dryRun) {
    QJsonObject h;
    h["old_start_line"] = line;
    h["old_lines"] = oldLines;
    h["new_lines"] = newLines;
    AiTools::ToolCall c;
    c.id = "t";
    c.name = "apply_diff";
    c.args["path"] = path;
    c.args["hunks"] = QJsonArray{h};
    if (dryRun) c.args["dry_run"] = true;
    return AiTools::execute(c, ws);
}

static AiTools::ToolResult readFile(const QString &ws, const QString &path) {
    AiTools::ToolCall c;
    c.id = "t";
    c.name = "read_file";
    c.args["path"] = path;
    return AiTools::execute(c, ws);
}

static bool denied(const AiTools::ToolResult &r) {
    return r.isError && r.errorKind == QLatin1String("denied");
}

// write_file refused in preview and for real, file bytes unchanged / absent.
static void expectWriteRefused(const QString &ws, const QString &rel, const QString &label) {
    const QString abs = ws + "/" + rel;
    const bool existed = QFileInfo::exists(abs);
    const QByteArray before = get(abs);
    check("write_file preview refused: " + label, denied(writeFile(ws, rel, "x\n", true)));
    check("write_file refused: " + label, denied(writeFile(ws, rel, "x\n", false)));
    check("  ... and the file is untouched: " + label,
          existed ? get(abs) == before : !QFileInfo::exists(abs));
}

static void expectDiffRefused(const QString &ws, const QString &rel, const QString &firstLine,
                              const QString &label) {
    const QByteArray before = get(ws + "/" + rel);
    check("apply_diff preview refused: " + label,
          denied(applyDiff(ws, rel, 1, firstLine, "pwned\n", true)));
    check("apply_diff refused: " + label,
          denied(applyDiff(ws, rel, 1, firstLine, "pwned\n", false)));
    check("  ... and the file is untouched: " + label, get(ws + "/" + rel) == before);
}

int main(int argc, char *argv[]) {
    QTemporaryDir home;
    const QString h = QDir(home.path()).canonicalPath();
    qputenv("HOME", h.toUtf8());
    qputenv("XDG_CONFIG_HOME", (h + "/.config").toUtf8());
    qputenv("XDG_DATA_HOME", (h + "/.local/share").toUtf8());
    qputenv("APPDATA", h.toUtf8());
    qputenv("USERPROFILE", h.toUtf8());
    QCoreApplication app(argc, argv);
    QCoreApplication::setOrganizationName(QStringLiteral("Notepatra"));
    QCoreApplication::setApplicationName(QStringLiteral("Notepatra"));
    std::printf("=== test_ai_write_denylist ===\n");

    using PathDenylist::isSecretPath;
    using PathDenylist::isWriteDangerous;

    // ── R3: deny-list rows ───────────────────────────────────────────
    std::printf("-- git and start-up files are write-dangerous --\n");
    const QStringList dangerous = {
        "/r/.git/config", "/r/.git/config.worktree", "/r/.git/info/attributes",
        "/r/.git/info/exclude", "/r/.git/modules/sub/config",
        "/r/.git/modules/sub/hooks/post-checkout", "/r/.git/worktrees/w/config",
        "/r/.git/hooks/pre-commit", "/r/sub/.git", "C:\\r\\.GIT\\Config",
        "/r/.Git/HOOKS/pre-push", h + "/.config/git/config", h + "/.config/git/attributes",
        h + "/.config/git/ignore", "/etc/gitconfig", h + "/.gitconfig", h + "/.bashrc",
        h + "/.bashrc.d/x.sh", h + "/.config/environment.d/x.conf",
        h + "/.local/share/applications/firefox.desktop",
    };
    for (const QString &p : dangerous) check("write-dangerous: " + p, isWriteDangerous(p));
    const QStringList ordinary = {
        "/r/src/main.py", "/r/.gitignore", "/r/.gitattributes", "/r/.gitmodules",
        "/r/my.git/x", "/r/docs/git/config.md", "/r/.github/workflows/ci.yml",
        h + "/.config/other/settings.json", h + "/projects/app/config",
    };
    for (const QString &p : ordinary)
        check("not write-dangerous: " + p, !isWriteDangerous(p) && !isSecretPath(p));
    {
        const QByteArray saved = qgetenv("XDG_CONFIG_HOME");
        qputenv("XDG_CONFIG_HOME", (h + "/xdg").toUtf8());
        check("write-dangerous: $XDG_CONFIG_HOME/git/config (XDG not ~/.config)",
              isWriteDangerous(h + "/xdg/git/config"));
        qputenv("XDG_CONFIG_HOME", saved);
    }

    // ── R4: the app's own settings are secret and write-dangerous ────
    std::printf("-- the app's settings directory --\n");
    const QString cfgDir = Config::appConfigDir();  // creates it, as the app does
    check("vacuity: the config dir is inside the isolated HOME", cfgDir.startsWith(h));
    for (const QString &leaf : {QStringLiteral("config.json"), QStringLiteral("db-connections.json"),
                                QStringLiteral("rest-client.json"),
                                QStringLiteral("mcp-endpoint.json"),
                                QStringLiteral("recovery/x.txt")}) {
        const QString p = cfgDir + "/" + leaf;
        check("secret: <config dir>/" + leaf, isSecretPath(p));
        check("write-dangerous: <config dir>/" + leaf, isWriteDangerous(p));
    }
    check("secret: <config dir> itself", isSecretPath(cfgDir));
    check("secret: upper-cased spelling", isSecretPath(cfgDir.toUpper() + "/CONFIG.JSON"));
    check("secret: native separators",
          isSecretPath(QString(cfgDir + "/config.json").replace('/', '\\')));
    check("secret: legacy ~/.config/notepatra copy",
          isSecretPath(Config::legacyLinuxConfigDir() + "/config.json"));
    check("secret: chat history (AppConfigLocation)",
          isSecretPath(QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation) +
                       "/chat-history/x.json"));
    check("write-dangerous: plugins folder",
          isWriteDangerous(QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) +
                           "/plugins/evil.so"));
    check("not secret: a sibling directory sharing the prefix",
          !isSecretPath(cfgDir + "-backup/config.json"));
#ifndef Q_OS_WIN
    {
        // ~/.config as a symlink (dotfile managers): the canonical spelling a
        // caller passes after resolving must match too.
        QDir().mkpath(h + "/dotfiles/cfg/notepatra");
        const bool linked =
            QFile::link(h + "/dotfiles/cfg", h + "/cfglink") && QFileInfo(h + "/cfglink").isSymLink();
        check("vacuity: XDG_CONFIG_HOME symlink created", linked);
        const QByteArray saved = qgetenv("XDG_CONFIG_HOME");
        qputenv("XDG_CONFIG_HOME", (h + "/cfglink").toUtf8());
        check("secret: canonical path behind a symlinked XDG_CONFIG_HOME",
              isSecretPath(h + "/dotfiles/cfg/notepatra/config.json"));
        check("secret: spelled path through the symlink",
              isSecretPath(h + "/cfglink/notepatra/config.json"));
        qputenv("XDG_CONFIG_HOME", saved);
    }
#endif

    // ai_tools read: a workspace that contains the settings (the user opened
    // their home folder) still cannot read or write them.
    {
        put(cfgDir + "/config.json", "{\"aiApiKey\": \"marker-key-12345\"}\n");
        const QString rel = QDir(h).relativeFilePath(cfgDir + "/config.json");
        const auto r = readFile(h, rel);
        check("read_file refused: " + rel, denied(r) && !r.content.contains("marker-key-12345"));
        AiTools::ToolCall s;
        s.id = "t";
        s.name = "search";
        s.args["pattern"] = "marker-key-12345";
        s.args["path"] = ".config";
        put(h + "/.config/other/notes.txt", "marker-key-12345 in an ordinary file\n");
        const auto sr = AiTools::execute(s, h);
        check("vacuity: search finds the marker in an ordinary file",
              sr.content.contains("notes.txt"));
        check("search does not return the settings file",
              !sr.content.contains("config.json") && !sr.content.contains("aiApiKey"));
        expectWriteRefused(h, rel, rel);
    }

    // ── R3: agent writes ─────────────────────────────────────────────
    std::printf("-- agent writes --\n");
    QTemporaryDir wsDir;
    g_ws = QDir(wsDir.path()).canonicalPath();
    put(g_ws + "/.git/config", "[core]\n\trepositoryformatversion = 0\n");
    put(g_ws + "/.git/hooks/README", "hooks\n");
    put(g_ws + "/.git/info/exclude", "# exclude\n");
    put(g_ws + "/src/ok.txt", "one\ntwo\n");

    expectWriteRefused(g_ws, ".git/hooks/pre-commit", ".git/hooks/pre-commit");
    expectWriteRefused(g_ws, ".git/config", ".git/config");
    expectWriteRefused(g_ws, ".git/info/attributes", ".git/info/attributes");
    expectWriteRefused(g_ws, "sub/.git", "sub/.git (a gitdir redirect file)");
    expectWriteRefused(g_ws, "fresh/.git/config", "fresh/.git/config");
    check("  ... and no fresh/.git directory was created on the way",
          !QFileInfo::exists(g_ws + "/fresh/.git"));
    expectDiffRefused(g_ws, ".git/config", "[core]\n", ".git/config");
    expectDiffRefused(g_ws, ".git/info/exclude", "# exclude\n", ".git/info/exclude");

#ifndef Q_OS_WIN
    {
        // A shell rc file and a git config reached through workspace symlinks.
        put(g_ws + "/.bashrc", "export A=1\n");
        const bool l1 = QFile::link(g_ws + "/.bashrc", g_ws + "/notes.sh")
                        && QFileInfo(g_ws + "/notes.sh").isSymLink();
        const bool l2 = QFile::link(g_ws + "/.git/config", g_ws + "/settings.ini")
                        && QFileInfo(g_ws + "/settings.ini").isSymLink();
        QDir().mkpath(g_ws + "/.config/fish");
        const bool l3 = QFile::link(g_ws + "/.config/fish", g_ws + "/fishconf")
                        && QFileInfo(g_ws + "/fishconf").isSymLink();
        check("vacuity: workspace symlinks created", l1 && l2 && l3);
        expectWriteRefused(g_ws, ".bashrc", ".bashrc in a home-folder workspace");
        expectWriteRefused(g_ws, "notes.sh", "notes.sh -> .bashrc");
        expectDiffRefused(g_ws, "notes.sh", "export A=1\n", "notes.sh -> .bashrc");
        expectWriteRefused(g_ws, "settings.ini", "settings.ini -> .git/config");
        expectDiffRefused(g_ws, "settings.ini", "[core]\n", "settings.ini -> .git/config");
        expectWriteRefused(g_ws, "fishconf/config.fish", "fishconf/ -> .config/fish/");
    }
#endif

    // Ordinary files still write.
    {
        const auto w = writeFile(g_ws, "src/new.txt", "hello\n", false);
        check("write_file still writes an ordinary file", !w.isError &&
              get(g_ws + "/src/new.txt") == "hello\n");
        const auto d = applyDiff(g_ws, "src/ok.txt", 2, "two\n", "TWO\n", false);
        check("apply_diff still edits an ordinary file", !d.isError &&
              get(g_ws + "/src/ok.txt") == "one\nTWO\n");
        check("write_file preview of an ordinary file is offered",
              !writeFile(g_ws, "src/ok.txt", "x\n", true).isError);
    }

    std::printf("\n=== Summary: %d passed, %d failed ===\n", g_pass, g_fail);
    return g_fail == 0 ? 0 : 1;
}
