// SPDX-License-Identifier: GPL-3.0-or-later
//
// test_git_tools_secrets — git_show / git_diff must not print the contents of
// deny-listed files (.env, *.key, id_rsa, ...), and must not run repo-configured
// textconv / external diff commands.
// Regression: git_show only rejected a few shell characters, so
// `HEAD:.env` printed a committed secret; git_diff passed a relative path with
// no deny-list check and whole-tree diffs included deny-listed files.

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QStandardPaths>
#include <QString>
#include <QStringList>
#include <QTemporaryDir>
#include <cstdio>

#include "ai_tools.h"

static int g_pass = 0, g_fail = 0;
static QString g_ws;

static void check(const QString &label, bool ok) {
    if (ok) { ++g_pass; std::printf("  [PASS] %s\n", qPrintable(label)); }
    else    { ++g_fail; std::printf("  [FAIL] %s\n", qPrintable(label)); }
}

static void put(const QString &rel, const QByteArray &bytes) {
    const QString p = g_ws + "/" + rel;
    QDir().mkpath(QFileInfo(p).absolutePath());
    QFile f(p);
    f.open(QIODevice::WriteOnly);
    f.write(bytes);
    f.close();
}

static QString git(const QStringList &args, bool *ok = nullptr) {
    QProcess p;
    p.setWorkingDirectory(g_ws);
    p.start("git", args);
    const bool done = p.waitForStarted(5000) && p.waitForFinished(15000);
    const bool good = done && p.exitStatus() == QProcess::NormalExit && p.exitCode() == 0;
    if (ok) *ok = good;
    if (!good)
        std::printf("  git %s failed: %s\n", qPrintable(args.join(' ')),
                    p.readAllStandardError().constData());
    return QString::fromUtf8(p.readAllStandardOutput()).trimmed();
}

static AiTools::ToolResult run(const QString &tool, const QJsonObject &args) {
    AiTools::ToolCall c;
    c.id = "t";
    c.name = tool;
    c.args = args;
    return AiTools::execute(c, g_ws);
}

static QString textOf(const AiTools::ToolResult &r) {
    return QJsonDocument::fromJson(r.content.toUtf8()).object()
        .value("result").toObject().value("text").toString();
}

// Every secret fixture carries this token; no tool output may contain it.
static const char kLeak[] = "LEAKTOKEN";

static void expectShowRefused(const QString &spec) {
    const AiTools::ToolResult r = run("git_show", QJsonObject{{"commit", spec}});
    check("git_show refuses " + spec, r.isError && !r.content.contains(kLeak));
}

int main(int argc, char *argv[]) {
    QCoreApplication app(argc, argv);
    setvbuf(stdout, nullptr, _IONBF, 0);

    QTemporaryDir home;
    qputenv("HOME", home.path().toUtf8());
    qputenv("XDG_CONFIG_HOME", (home.path() + "/.config").toUtf8());
    qputenv("APPDATA", (home.path() + "/AppData").toUtf8());
    qputenv("USERPROFILE", home.path().toUtf8());
    // Never read the developer's global/system git config.
    qputenv("GIT_CONFIG_GLOBAL", (home.path() + "/no-gitconfig").toUtf8());
    qputenv("GIT_CONFIG_NOSYSTEM", "1");

    if (QStandardPaths::findExecutable("git").isEmpty()) {
        std::printf("  [SKIP] git not found on PATH\n");
        return 0;
    }

    QTemporaryDir repo;
    g_ws = QDir(repo.path()).canonicalPath();
    bool ok = false;
    git({"init", "-q"}, &ok);
    if (!ok) { std::printf("FATAL: git init failed\n"); return 1; }
    git({"config", "user.email", "test@example.invalid"});
    git({"config", "user.name", "Notepatra Test"});
    git({"config", "commit.gpgsign", "false"});
    git({"config", "core.autocrlf", "false"});

    const QByteArray keyBody = "line one\nLEAKTOKEN key 2\nLEAKTOKEN key 3\nLEAKTOKEN key 4\n";
    put("README.md", "visible readme\n");
    put("app.txt", "visible app v1\n");
    put(".env", "API_KEY=LEAKTOKEN-env-1\n");
    put("config/server.key", keyBody);
    put("deploy/id_rsa", "LEAKTOKEN rsa\n");
    put(QString::fromUtf8("\xC3\xA9t\xC3\xA9.env"), "LEAKTOKEN quoted-name\n");
    put("my secrets.env", "LEAKTOKEN spaced-name\n");
    // data.txt is an ordinary name (readable anyway) until it is renamed to a
    // secret name below, so it carries its own token.
    put("data.txt", "MOVEDTOKEN 1\nMOVEDTOKEN 2\nMOVEDTOKEN 3\n");
    git({"add", "-A"});
    git({"commit", "-q", "-m", "c1 add files"}, &ok);
    if (!ok) { std::printf("FATAL: commit failed\n"); return 1; }

    put(".env", "API_KEY=LEAKTOKEN-env-2\n");
    put("app.txt", "visible app v2\n");
    put("config/server.key", keyBody + "LEAKTOKEN key 5\n");
    git({"add", "-A"});
    git({"commit", "-q", "-m", "c2 modify"});

    // Renames in both directions, each with an edit so the hunk shows context.
    git({"mv", "config/server.key", "config/notes.txt"});
    put("config/notes.txt", keyBody + "LEAKTOKEN key 6\n");
    git({"mv", "data.txt", "prod.pem"});
    put("prod.pem", "MOVEDTOKEN 1\nMOVEDTOKEN 2\nMOVEDTOKEN 3\nLEAKTOKEN new\n");
    git({"add", "-A"});
    git({"commit", "-q", "-m", "c3 renames"});

    std::printf("=== git_show: <rev>:<path> object specs ===\n");
    for (const QString &spec : {QStringLiteral("HEAD:.env"), QStringLiteral("HEAD~2:config/server.key"),
                                QStringLiteral(":0:.env"), QStringLiteral(":.env"),
                                QStringLiteral("HEAD:./.env"), QStringLiteral("HEAD:config/../.env"),
                                QStringLiteral("HEAD~1:deploy/id_rsa"), QStringLiteral("HEAD:prod.pem"),
                                QString::fromUtf8("HEAD:\xC3\xA9t\xC3\xA9.env"),
                                QStringLiteral("HEAD -- .env"), QStringLiteral("--output=x"),
                                QStringLiteral("--ext-diff"), QStringLiteral("-c")})
        expectShowRefused(spec);

    // A bare blob id, and an annotated tag pointing at the blob, carry no path.
    const QString blob = git({"rev-parse", "HEAD:.env"});
    check("fixture: blob id resolved", blob.size() >= 40);
    expectShowRefused(blob);
    git({"tag", "-a", "-m", "blob tag", "blobtag", blob});
    expectShowRefused("blobtag");

    {   // Vacuity: ordinary files still show.
        const AiTools::ToolResult r = run("git_show", QJsonObject{{"commit", "HEAD:app.txt"}});
        check("git_show HEAD:app.txt still works", !r.isError && textOf(r).contains("visible app v2"));
    }

    std::printf("=== git_show: commit diffs redact deny-listed files ===\n");
    for (const QString &rev : {QStringLiteral("HEAD~2"), QStringLiteral("HEAD~1"), QStringLiteral("HEAD")}) {
        const AiTools::ToolResult r = run("git_show", QJsonObject{{"commit", rev}});
        const QString t = textOf(r);
        check("git_show " + rev + ": ok", !r.isError);
        check("git_show " + rev + ": no secret content", !r.content.contains(kLeak));
        check("git_show " + rev + ": placeholder present",
              t.contains("[redacted: secret-pattern path]"));
    }
    check("git_show HEAD~2: ordinary file diff still shown",
          textOf(run("git_show", QJsonObject{{"commit", "HEAD~2"}})).contains("+visible app v1"));
    check("git_show HEAD~1: ordinary file diff still shown",
          textOf(run("git_show", QJsonObject{{"commit", "HEAD~1"}})).contains("+visible app v2"));
    {   // Renames: old name secret (server.key -> notes.txt) and new name secret.
        const QString t = textOf(run("git_show", QJsonObject{{"commit", "HEAD"}}));
        check("git_show HEAD: rename to a secret name redacted", !t.contains("MOVEDTOKEN"));
        check("git_show HEAD: two renamed blocks redacted",
              t.count("[redacted: secret-pattern path]") == 2);
    }

    std::printf("=== git_diff: explicit secret paths refused ===\n");
    put(".env", "API_KEY=LEAKTOKEN-env-3\n");
    put("app.txt", "visible app v3\n");
    put("deploy/id_rsa", "LEAKTOKEN rsa 2\n");
    for (const QString &p : {QStringLiteral(".env"), QStringLiteral("./.env"),
                             QStringLiteral("deploy/id_rsa"), QStringLiteral("prod.pem")}) {
        const AiTools::ToolResult r = run("git_diff", QJsonObject{{"path", p}});
        check("git_diff refuses path " + p, r.isError && r.errorKind == "denied"
                                                && !r.content.contains(kLeak));
    }
    {
        const AiTools::ToolResult r = run("git_diff", QJsonObject{{"path", "app.txt"}});
        check("git_diff app.txt still works", !r.isError && textOf(r).contains("+visible app v3"));
    }

    std::printf("=== git_diff: whole-tree diffs redact deny-listed files ===\n");
    {
        const AiTools::ToolResult r = run("git_diff", QJsonObject{});
        check("git_diff (worktree): ok", !r.isError);
        check("git_diff (worktree): no secret content", !r.content.contains(kLeak));
        check("git_diff (worktree): placeholder present",
              textOf(r).contains("[redacted: secret-pattern path]"));
        check("git_diff (worktree): ordinary change shown", textOf(r).contains("+visible app v3"));
    }
    git({"add", "-A"});
    {
        const AiTools::ToolResult r = run("git_diff", QJsonObject{{"staged", true}});
        check("git_diff (staged): no secret content", !r.isError && !r.content.contains(kLeak));
        check("git_diff (staged): ordinary change shown", textOf(r).contains("+visible app v3"));
    }
    git({"commit", "-q", "-m", "c4"});

    std::printf("=== git_show: merge commit (combined diff) ===\n");
    {
        const QString base = git({"rev-parse", "--abbrev-ref", "HEAD"});
        git({"checkout", "-q", "-b", "side"});
        put(".env", "API_KEY=LEAKTOKEN-side\n");
        put("app.txt", "side app\n");
        git({"commit", "-q", "-am", "side"});
        git({"checkout", "-q", base});
        put(".env", "API_KEY=LEAKTOKEN-main\n");
        put("app.txt", "main app\n");
        git({"commit", "-q", "-am", "main"});
        git({"merge", "-q", "side"});   // conflicts on purpose
        put(".env", "API_KEY=LEAKTOKEN-merged\n");
        put("app.txt", "merged app\n");
        git({"add", "-A"});
        git({"commit", "-q", "-m", "merge"}, &ok);
        const AiTools::ToolResult r = run("git_show", QJsonObject{{"commit", "HEAD"}});
        const QString t = textOf(r);
        check("merge fixture committed", ok);
        check("git_show merge: combined diff shown (vacuity)",
              t.contains("diff --cc") && t.contains("merged app"));
        check("git_show merge: no secret content", !r.isError && !r.content.contains(kLeak));
    }

    std::printf("=== textconv / external diff commands never run ===\n");
    put(".gitattributes", "*.txt diff=npconv\n");
    git({"config", "diff.npconv.textconv", "touch textconv-ran; cat"});
    git({"config", "diff.external", "touch extdiff-ran; true"});
    git({"add", ".gitattributes"});
    git({"commit", "-q", "-m", "c5 attrs"});
    put("app.txt", "visible app v4\n");
    run("git_diff", QJsonObject{});
    git({"add", "app.txt"});
    run("git_diff", QJsonObject{{"staged", true}});
    git({"commit", "-q", "-m", "c6"});
    run("git_show", QJsonObject{{"commit", "HEAD"}});
    check("textconv command not run", !QFile::exists(g_ws + "/textconv-ran"));
    check("external diff command not run", !QFile::exists(g_ws + "/extdiff-ran"));

    std::printf("\n=== Summary: %d passed, %d failed ===\n", g_pass, g_fail);
    return g_fail == 0 ? 0 : 1;
}
