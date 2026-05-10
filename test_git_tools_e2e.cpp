// test_git_tools_e2e.cpp — end-to-end validation of Notepatra v0.1.58's
// agentic git tools. Spins up a temporary git repo, makes 3 known commits,
// then drives every git tool via AiTools::execute() and asserts on the
// JSON wire shape.
//
// Tools exercised:
//   git_status      — clean & dirty
//   git_diff        — working-tree, staged, path-scoped
//   git_log         — default count, max_count clamping, path scoping
//   git_branch_list — main + extra branch
//   git_show        — by full SHA, by HEAD, by HEAD~1
//
// Error paths exercised:
//   no_workspace      — empty workspace string
//   not_a_repo        — workspace is a non-git directory
//   io_error          — git_show with empty / flag-prefixed / metachar SHA
//   git_error         — git_show with nonexistent SHA
//   outside_workspace — path argument with ".."
//
// Plus a real-world integration sweep against the actual Notepatra repo
// (path passed via the NOTEPATRA_REPO_ROOT env var; skipped if unset) —
// read-only by design.

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QString>
#include <QTemporaryDir>
#include <QTextStream>
#include <cstdio>

#include "ai_tools.h"
#include "git_tools.h"

static int g_pass = 0;
static int g_fail = 0;

struct FailDetail { QString label; };
static QList<FailDetail> g_failDetails;

static void check(const char *label, bool ok) {
    if (ok) { ++g_pass; fprintf(stdout, "  ok   %s\n", label); }
    else    {
        ++g_fail;
        fprintf(stderr, "  FAIL %s\n", label);
        g_failDetails.append({QString::fromUtf8(label)});
    }
}

static void writeFile(const QString &path, const QByteArray &content) {
    QFile f(path);
    f.open(QFile::WriteOnly);
    f.write(content);
    f.close();
}

// Run `git <args>` in workspace, return exit code; abort if not 0 (test
// setup failure, distinct from a tool returning git_error).
static int runGitOrDie(const QString &workspace, const QStringList &args) {
    QProcess p;
    p.setWorkingDirectory(workspace);
    p.start("git", args);
    if (!p.waitForStarted(2000)) {
        fprintf(stderr, "FATAL: git did not start\n");
        return -1;
    }
    if (!p.waitForFinished(5000)) {
        fprintf(stderr, "FATAL: git timed out\n");
        p.kill();
        return -1;
    }
    int rc = p.exitCode();
    if (rc != 0) {
        fprintf(stderr, "git %s failed (exit %d): %s\n",
                qPrintable(args.join(' ')), rc,
                qPrintable(QString::fromUtf8(p.readAllStandardError())));
    }
    return rc;
}

// Run `git rev-parse <ref>` and return the resulting SHA (or empty on fail).
static QString gitRevParse(const QString &workspace, const QString &ref) {
    QProcess p;
    p.setWorkingDirectory(workspace);
    p.start("git", {"rev-parse", ref});
    p.waitForFinished(5000);
    if (p.exitCode() != 0) return QString();
    return QString::fromUtf8(p.readAllStandardOutput()).trimmed();
}

// Helper: invoke AiTools::execute, return JSON-parsed result body for
// easy field assertions. Caller still owns the raw ToolResult.
static QJsonObject parseToolBody(const AiTools::ToolResult &r) {
    return QJsonDocument::fromJson(r.content.toUtf8()).object();
}

// ── Test suites ──────────────────────────────────────────────────────────

static void testNoWorkspaceErrors() {
    fprintf(stdout, "[suite] no_workspace error path\n");
    const QStringList tools = {
        "git_status", "git_diff", "git_log", "git_branch_list", "git_show"
    };
    for (const QString &name : tools) {
        AiTools::ToolCall call;
        call.id = "nw_" + name;
        call.name = name;
        if (name == "git_show") {
            call.args = QJsonObject{{"commit", "HEAD"}};
        } else {
            call.args = QJsonObject{};
        }
        AiTools::ToolResult r = AiTools::execute(call, QString());
        check(qPrintable(name + ": empty ws → isError"), r.isError);
        check(qPrintable(name + ": empty ws → kind=no_workspace"),
              r.errorKind == "no_workspace");
        QJsonObject body = parseToolBody(r);
        check(qPrintable(name + ": empty ws → ok=false in JSON"),
              body.value("ok").toBool() == false);
        check(qPrintable(name + ": empty ws → error_kind in JSON body"),
              body.value("error_kind").toString() == "no_workspace");
    }
}

static void testNotARepo(const QString &nonRepoDir) {
    fprintf(stdout, "[suite] not_a_repo error path\n");
    AiTools::ToolCall call;
    call.id = "nr1";
    call.name = "git_status";
    call.args = QJsonObject{};
    AiTools::ToolResult r = AiTools::execute(call, nonRepoDir);
    check("git_status on non-repo: isError", r.isError);
    check("git_status on non-repo: kind=not_a_repo",
          r.errorKind == "not_a_repo");

    AiTools::ToolCall call2;
    call2.id = "nr2";
    call2.name = "git_log";
    call2.args = QJsonObject{};
    AiTools::ToolResult r2 = AiTools::execute(call2, nonRepoDir);
    check("git_log on non-repo: isError", r2.isError);
    check("git_log on non-repo: kind=not_a_repo",
          r2.errorKind == "not_a_repo");
}

// ── 1) Build the test repo ───────────────────────────────────────────────
//
// Three commits, on branch `main`:
//   commit 1: README.md ("# Test repo\n")
//   commit 2: src/foo.cpp + src/bar.cpp
//   commit 3: modify README.md
// Plus a second branch `feature/x` with one extra commit.
struct RepoFixture {
    QString ws;
    QString sha1;        // README.md initial
    QString sha2;        // src/{foo,bar}.cpp added
    QString sha3;        // README modified
    QString featureSha;  // commit on feature/x
};

static bool buildFixture(QTemporaryDir &tmp, RepoFixture &out) {
    out.ws = tmp.path();

    if (runGitOrDie(out.ws, {"init", "-q", "-b", "main"}) != 0) return false;
    runGitOrDie(out.ws, {"config", "user.email", "test@notepatra.local"});
    runGitOrDie(out.ws, {"config", "user.name",  "Notepatra Test"});
    runGitOrDie(out.ws, {"config", "commit.gpgsign", "false"});

    // Commit 1: README.md
    writeFile(out.ws + "/README.md", "# Test repo\n\nFor git tools.\n");
    runGitOrDie(out.ws, {"add", "README.md"});
    runGitOrDie(out.ws, {"commit", "-q", "-m", "initial: add README"});
    out.sha1 = gitRevParse(out.ws, "HEAD");

    // Commit 2: add src/{foo,bar}.cpp
    QDir(out.ws).mkpath("src");
    writeFile(out.ws + "/src/foo.cpp", "int foo() { return 1; }\n");
    writeFile(out.ws + "/src/bar.cpp", "int bar() { return 2; }\n");
    runGitOrDie(out.ws, {"add", "src/foo.cpp", "src/bar.cpp"});
    runGitOrDie(out.ws, {"commit", "-q", "-m", "feat: add src files"});
    out.sha2 = gitRevParse(out.ws, "HEAD");

    // Commit 3: modify README.md
    writeFile(out.ws + "/README.md",
              "# Test repo\n\nFor git tools.\n\nUpdated.\n");
    runGitOrDie(out.ws, {"add", "README.md"});
    runGitOrDie(out.ws, {"commit", "-q", "-m", "docs: update README"});
    out.sha3 = gitRevParse(out.ws, "HEAD");

    // Feature branch with one commit
    runGitOrDie(out.ws, {"checkout", "-q", "-b", "feature/x"});
    writeFile(out.ws + "/feature.txt", "feature work\n");
    runGitOrDie(out.ws, {"add", "feature.txt"});
    runGitOrDie(out.ws, {"commit", "-q", "-m", "feat: feature branch work"});
    out.featureSha = gitRevParse(out.ws, "HEAD");

    // Back to main for the rest of the tests
    runGitOrDie(out.ws, {"checkout", "-q", "main"});

    return !out.sha1.isEmpty() && !out.sha2.isEmpty() &&
           !out.sha3.isEmpty() && !out.featureSha.isEmpty();
}

// ── 2) git_status ────────────────────────────────────────────────────────
static void testGitStatus(const RepoFixture &fx) {
    fprintf(stdout, "[suite] git_status\n");

    // Clean repo
    {
        AiTools::ToolCall call;
        call.id = "gs_clean";
        call.name = "git_status";
        AiTools::ToolResult r = AiTools::execute(call, fx.ws);
        check("git_status clean: not error", !r.isError);
        QJsonObject body = parseToolBody(r);
        check("git_status clean: ok=true", body.value("ok").toBool());
        QJsonObject result = body.value("result").toObject();
        check("git_status clean: branch == main",
              result.value("branch").toString() == "main");
        check("git_status clean: clean=true",
              result.value("clean").toBool());
        check("git_status clean: staged is empty array",
              result.value("staged").toArray().isEmpty());
        check("git_status clean: modified is empty array",
              result.value("modified").toArray().isEmpty());
        check("git_status clean: untracked is empty array",
              result.value("untracked").toArray().isEmpty());
        check("git_status clean: ahead == 0",
              result.value("ahead").toInt() == 0);
        check("git_status clean: behind == 0",
              result.value("behind").toInt() == 0);
    }

    // Dirty: modify foo.cpp + create new file
    writeFile(fx.ws + "/src/foo.cpp", "int foo() { return 42; } // modified\n");
    writeFile(fx.ws + "/scratch.txt", "untracked\n");
    {
        AiTools::ToolCall call;
        call.id = "gs_dirty";
        call.name = "git_status";
        AiTools::ToolResult r = AiTools::execute(call, fx.ws);
        QJsonObject body = parseToolBody(r);
        QJsonObject result = body.value("result").toObject();
        check("git_status dirty: not clean",
              result.value("clean").toBool() == false);
        QJsonArray modified = result.value("modified").toArray();
        bool sawFoo = false;
        for (const auto &v : modified) if (v.toString() == "src/foo.cpp") sawFoo = true;
        check("git_status dirty: src/foo.cpp in modified[]", sawFoo);
        QJsonArray untracked = result.value("untracked").toArray();
        bool sawScratch = false;
        for (const auto &v : untracked) if (v.toString() == "scratch.txt") sawScratch = true;
        check("git_status dirty: scratch.txt in untracked[]", sawScratch);
    }

    // Stage scratch.txt → moves to staged[]
    runGitOrDie(fx.ws, {"add", "scratch.txt"});
    {
        AiTools::ToolCall call;
        call.id = "gs_staged";
        call.name = "git_status";
        AiTools::ToolResult r = AiTools::execute(call, fx.ws);
        QJsonObject body = parseToolBody(r);
        QJsonObject result = body.value("result").toObject();
        QJsonArray staged = result.value("staged").toArray();
        bool sawScratch = false;
        for (const auto &v : staged) if (v.toString() == "scratch.txt") sawScratch = true;
        check("git_status staged: scratch.txt in staged[]", sawScratch);
    }

    // Reset for downstream tests
    runGitOrDie(fx.ws, {"checkout", "-q", "--", "src/foo.cpp"});
    runGitOrDie(fx.ws, {"reset", "-q", "HEAD", "scratch.txt"});
    QFile::remove(fx.ws + "/scratch.txt");
}

// ── 3) git_diff ──────────────────────────────────────────────────────────
static void testGitDiff(const RepoFixture &fx) {
    fprintf(stdout, "[suite] git_diff\n");

    // Working-tree diff with no changes → empty
    {
        AiTools::ToolCall call;
        call.id = "gd_clean";
        call.name = "git_diff";
        AiTools::ToolResult r = AiTools::execute(call, fx.ws);
        check("git_diff clean: not error", !r.isError);
        QJsonObject body = parseToolBody(r);
        QJsonObject result = body.value("result").toObject();
        check("git_diff clean: text is empty",
              result.value("text").toString().isEmpty());
        check("git_diff clean: truncated=false",
              result.value("truncated").toBool() == false);
        check("git_diff clean: staged=false",
              result.value("staged").toBool() == false);
    }

    // Modify foo.cpp, expect non-empty diff with file path
    writeFile(fx.ws + "/src/foo.cpp",
              "int foo() { return 42; } // changed\n");
    {
        AiTools::ToolCall call;
        call.id = "gd_dirty";
        call.name = "git_diff";
        AiTools::ToolResult r = AiTools::execute(call, fx.ws);
        QJsonObject body = parseToolBody(r);
        QJsonObject result = body.value("result").toObject();
        QString text = result.value("text").toString();
        check("git_diff: contains src/foo.cpp",
              text.contains("src/foo.cpp"));
        check("git_diff: contains diff marker '+'",
              text.contains("+int foo() { return 42; } // changed"));
        check("git_diff: text non-empty",
              !text.isEmpty());
    }

    // Path-scoped diff with bar.cpp (which is unchanged) → empty even
    // though foo.cpp is dirty
    {
        AiTools::ToolCall call;
        call.id = "gd_path";
        call.name = "git_diff";
        call.args = QJsonObject{{"path", "src/bar.cpp"}};
        AiTools::ToolResult r = AiTools::execute(call, fx.ws);
        QJsonObject body = parseToolBody(r);
        QJsonObject result = body.value("result").toObject();
        check("git_diff path=bar.cpp: text empty (bar unchanged)",
              result.value("text").toString().isEmpty());
        check("git_diff path=bar.cpp: path echoed in result",
              result.value("path").toString() == "src/bar.cpp");
    }

    // Path traversal attempt
    {
        AiTools::ToolCall call;
        call.id = "gd_trav";
        call.name = "git_diff";
        call.args = QJsonObject{{"path", "../../../etc/passwd"}};
        AiTools::ToolResult r = AiTools::execute(call, fx.ws);
        check("git_diff path=../../etc/passwd: isError", r.isError);
        check("git_diff path=../../etc/passwd: kind=outside_workspace",
              r.errorKind == "outside_workspace");
    }

    // Stage foo.cpp, then `staged: true` should show it
    runGitOrDie(fx.ws, {"add", "src/foo.cpp"});
    {
        AiTools::ToolCall call;
        call.id = "gd_staged";
        call.name = "git_diff";
        call.args = QJsonObject{{"staged", true}};
        AiTools::ToolResult r = AiTools::execute(call, fx.ws);
        QJsonObject body = parseToolBody(r);
        QJsonObject result = body.value("result").toObject();
        check("git_diff staged: staged=true echoed",
              result.value("staged").toBool());
        check("git_diff staged: text references foo.cpp",
              result.value("text").toString().contains("foo.cpp"));
    }

    // Reset for downstream tests
    runGitOrDie(fx.ws, {"reset", "-q", "--hard", "HEAD"});
}

// ── 4) git_log ───────────────────────────────────────────────────────────
static void testGitLog(const RepoFixture &fx) {
    fprintf(stdout, "[suite] git_log\n");

    // Default count → at least 3 commits
    {
        AiTools::ToolCall call;
        call.id = "gl_default";
        call.name = "git_log";
        AiTools::ToolResult r = AiTools::execute(call, fx.ws);
        check("git_log default: not error", !r.isError);
        QJsonObject body = parseToolBody(r);
        QJsonObject result = body.value("result").toObject();
        QJsonArray commits = result.value("commits").toArray();
        check("git_log default: count >= 3", commits.size() >= 3);
        check("git_log default: max_count == 20",
              result.value("max_count").toInt() == 20);

        // First commit (newest) should be sha3 / docs: update README
        if (!commits.isEmpty()) {
            QJsonObject head = commits[0].toObject();
            check("git_log default: HEAD hash == sha3",
                  head.value("hash").toString() == fx.sha3);
            check("git_log default: HEAD subject == 'docs: update README'",
                  head.value("subject").toString() == "docs: update README");
            check("git_log default: HEAD has author",
                  !head.value("author").toString().isEmpty());
            check("git_log default: HEAD has date (ISO)",
                  head.value("date").toString().contains("T"));
        }

        // Find sha1 in the commit list
        bool sawSha1 = false;
        for (const auto &v : commits) {
            if (v.toObject().value("hash").toString() == fx.sha1) {
                sawSha1 = true;
                break;
            }
        }
        check("git_log default: contains sha1", sawSha1);
    }

    // max_count=2 → only 2 commits, both newest
    {
        AiTools::ToolCall call;
        call.id = "gl_n2";
        call.name = "git_log";
        call.args = QJsonObject{{"max_count", 2}};
        AiTools::ToolResult r = AiTools::execute(call, fx.ws);
        QJsonObject body = parseToolBody(r);
        QJsonObject result = body.value("result").toObject();
        QJsonArray commits = result.value("commits").toArray();
        check("git_log n=2: count == 2", commits.size() == 2);
        check("git_log n=2: max_count == 2",
              result.value("max_count").toInt() == 2);
    }

    // max_count clamping: > 100 should clamp to 100 (we don't have 100
    // commits, so just verify max_count echoed correctly)
    {
        AiTools::ToolCall call;
        call.id = "gl_huge";
        call.name = "git_log";
        call.args = QJsonObject{{"max_count", 5000}};
        AiTools::ToolResult r = AiTools::execute(call, fx.ws);
        QJsonObject body = parseToolBody(r);
        QJsonObject result = body.value("result").toObject();
        check("git_log n=5000: clamped to 100",
              result.value("max_count").toInt() == 100);
    }

    // Path-scoped: only commits touching README.md → 2 (sha1 + sha3)
    {
        AiTools::ToolCall call;
        call.id = "gl_path";
        call.name = "git_log";
        call.args = QJsonObject{{"path", "README.md"}};
        AiTools::ToolResult r = AiTools::execute(call, fx.ws);
        QJsonObject body = parseToolBody(r);
        QJsonObject result = body.value("result").toObject();
        QJsonArray commits = result.value("commits").toArray();
        check("git_log path=README.md: count == 2",
              commits.size() == 2);
        check("git_log path=README.md: path echoed",
              result.value("path").toString() == "README.md");
    }

    // Path-scoped: src/foo.cpp → only 1 commit (sha2)
    {
        AiTools::ToolCall call;
        call.id = "gl_path_foo";
        call.name = "git_log";
        call.args = QJsonObject{{"path", "src/foo.cpp"}};
        AiTools::ToolResult r = AiTools::execute(call, fx.ws);
        QJsonObject body = parseToolBody(r);
        QJsonObject result = body.value("result").toObject();
        QJsonArray commits = result.value("commits").toArray();
        check("git_log path=src/foo.cpp: count == 1",
              commits.size() == 1);
        if (!commits.isEmpty()) {
            check("git_log path=src/foo.cpp: hash == sha2",
                  commits[0].toObject().value("hash").toString() == fx.sha2);
        }
    }

    // Path traversal in log
    {
        AiTools::ToolCall call;
        call.id = "gl_trav";
        call.name = "git_log";
        call.args = QJsonObject{{"path", "../escape"}};
        AiTools::ToolResult r = AiTools::execute(call, fx.ws);
        check("git_log path='../escape': isError", r.isError);
        check("git_log path='../escape': kind=outside_workspace",
              r.errorKind == "outside_workspace");
    }
}

// ── 5) git_branch_list ──────────────────────────────────────────────────
static void testGitBranchList(const RepoFixture &fx) {
    fprintf(stdout, "[suite] git_branch_list\n");

    AiTools::ToolCall call;
    call.id = "gb1";
    call.name = "git_branch_list";
    AiTools::ToolResult r = AiTools::execute(call, fx.ws);
    check("git_branch_list: not error", !r.isError);
    QJsonObject body = parseToolBody(r);
    QJsonObject result = body.value("result").toObject();

    check("git_branch_list: current == main",
          result.value("current").toString() == "main");
    check("git_branch_list: count == 2",
          result.value("count").toInt() == 2);

    QJsonArray branches = result.value("branches").toArray();
    bool sawMain = false, sawFeature = false;
    for (const auto &v : branches) {
        QJsonObject b = v.toObject();
        if (b.value("name").toString() == "main") {
            sawMain = true;
            check("git_branch_list: main is current",
                  b.value("current").toBool() == true);
        } else if (b.value("name").toString() == "feature/x") {
            sawFeature = true;
            check("git_branch_list: feature/x not current",
                  b.value("current").toBool() == false);
        }
    }
    check("git_branch_list: main present", sawMain);
    check("git_branch_list: feature/x present", sawFeature);
}

// ── 6) git_show ──────────────────────────────────────────────────────────
static void testGitShow(const RepoFixture &fx) {
    fprintf(stdout, "[suite] git_show\n");

    // By full SHA
    {
        AiTools::ToolCall call;
        call.id = "gsh_sha";
        call.name = "git_show";
        call.args = QJsonObject{{"commit", fx.sha2}};
        AiTools::ToolResult r = AiTools::execute(call, fx.ws);
        check("git_show full-sha: not error", !r.isError);
        QJsonObject body = parseToolBody(r);
        QJsonObject result = body.value("result").toObject();
        QString text = result.value("text").toString();
        check("git_show full-sha: text contains sha prefix",
              text.contains(fx.sha2.left(7)));
        check("git_show full-sha: text contains commit message",
              text.contains("feat: add src files"));
        check("git_show full-sha: text references foo.cpp",
              text.contains("src/foo.cpp"));
        check("git_show full-sha: text references bar.cpp",
              text.contains("src/bar.cpp"));
        check("git_show full-sha: commit echoed",
              result.value("commit").toString() == fx.sha2);
        check("git_show full-sha: truncated=false (small)",
              result.value("truncated").toBool() == false);
    }

    // By short SHA (7 chars)
    {
        AiTools::ToolCall call;
        call.id = "gsh_short";
        call.name = "git_show";
        call.args = QJsonObject{{"commit", fx.sha1.left(7)}};
        AiTools::ToolResult r = AiTools::execute(call, fx.ws);
        check("git_show short-sha: not error", !r.isError);
        QJsonObject body = parseToolBody(r);
        QJsonObject result = body.value("result").toObject();
        check("git_show short-sha: text references README.md",
              result.value("text").toString().contains("README.md"));
    }

    // By relative ref HEAD
    {
        AiTools::ToolCall call;
        call.id = "gsh_head";
        call.name = "git_show";
        call.args = QJsonObject{{"commit", "HEAD"}};
        AiTools::ToolResult r = AiTools::execute(call, fx.ws);
        check("git_show HEAD: not error", !r.isError);
        QJsonObject body = parseToolBody(r);
        QJsonObject result = body.value("result").toObject();
        check("git_show HEAD: text contains 'docs: update README'",
              result.value("text").toString().contains("docs: update README"));
    }

    // By HEAD~1
    {
        AiTools::ToolCall call;
        call.id = "gsh_h1";
        call.name = "git_show";
        call.args = QJsonObject{{"commit", "HEAD~1"}};
        AiTools::ToolResult r = AiTools::execute(call, fx.ws);
        check("git_show HEAD~1: not error", !r.isError);
        QJsonObject body = parseToolBody(r);
        QJsonObject result = body.value("result").toObject();
        check("git_show HEAD~1: text contains 'feat: add src files'",
              result.value("text").toString().contains("feat: add src files"));
    }

    // By branch name
    {
        AiTools::ToolCall call;
        call.id = "gsh_branch";
        call.name = "git_show";
        call.args = QJsonObject{{"commit", "feature/x"}};
        AiTools::ToolResult r = AiTools::execute(call, fx.ws);
        check("git_show feature/x: not error", !r.isError);
        QJsonObject body = parseToolBody(r);
        QJsonObject result = body.value("result").toObject();
        check("git_show feature/x: contains feature.txt",
              result.value("text").toString().contains("feature.txt"));
    }

    // ── Error paths ──

    // Empty commit
    {
        AiTools::ToolCall call;
        call.id = "gsh_empty";
        call.name = "git_show";
        call.args = QJsonObject{{"commit", ""}};
        AiTools::ToolResult r = AiTools::execute(call, fx.ws);
        check("git_show empty: isError", r.isError);
        check("git_show empty: kind=io_error",
              r.errorKind == "io_error");
    }

    // Missing commit field
    {
        AiTools::ToolCall call;
        call.id = "gsh_missing";
        call.name = "git_show";
        call.args = QJsonObject{};
        AiTools::ToolResult r = AiTools::execute(call, fx.ws);
        check("git_show missing arg: isError", r.isError);
    }

    // Flag-prefixed (e.g. --upload-pack injection attempt)
    {
        AiTools::ToolCall call;
        call.id = "gsh_flag";
        call.name = "git_show";
        call.args = QJsonObject{{"commit", "--upload-pack=evil"}};
        AiTools::ToolResult r = AiTools::execute(call, fx.ws);
        check("git_show '--flag': isError", r.isError);
        check("git_show '--flag': kind=io_error",
              r.errorKind == "io_error");
    }

    // Shell metacharacter injection attempts
    const QList<QString> meta = {
        "abc;rm -rf /", "abc&evil", "abc|cat", "abc`whoami`", "abc$X", "abc def"
    };
    for (const QString &m : meta) {
        AiTools::ToolCall call;
        call.id = "gsh_meta";
        call.name = "git_show";
        call.args = QJsonObject{{"commit", m}};
        AiTools::ToolResult r = AiTools::execute(call, fx.ws);
        check(qPrintable(QString("git_show meta '%1': isError").arg(m)),
              r.isError);
        check(qPrintable(QString("git_show meta '%1': kind=io_error").arg(m)),
              r.errorKind == "io_error");
    }

    // Nonexistent SHA — passes our regex/metachar gate but git rejects.
    // git_show returns git_error in that case (stderr surfaced).
    {
        AiTools::ToolCall call;
        call.id = "gsh_nope";
        call.name = "git_show";
        call.args = QJsonObject{{"commit", "deadbeefcafebabe"}};
        AiTools::ToolResult r = AiTools::execute(call, fx.ws);
        check("git_show nonexistent SHA: isError", r.isError);
        check("git_show nonexistent SHA: kind=git_error",
              r.errorKind == "git_error");
    }
}

// ── 7) Real-repo integration ─────────────────────────────────────────────
//
// Read-only sweep against the actual Notepatra repo. Verifies the tools
// return sensible data on a realistic codebase. Uses no setup, no teardown.
static void testRealRepo() {
    fprintf(stdout, "[suite] real-repo integration sweep\n");
    QString repo = QString::fromLocal8Bit(qgetenv("NOTEPATRA_REPO_ROOT"));
    if (repo.isEmpty()) {
        fprintf(stdout, "  (skipping — NOTEPATRA_REPO_ROOT unset)\n");
        return;
    }
    QFileInfo fi(repo + "/.git");
    if (!fi.exists()) {
        fprintf(stdout, "  (skipping — repo not present at %s)\n",
                qPrintable(repo));
        return;
    }

    // git_log → expect a recent commit
    {
        AiTools::ToolCall call;
        call.id = "rl";
        call.name = "git_log";
        call.args = QJsonObject{{"max_count", 50}};
        AiTools::ToolResult r = AiTools::execute(call, repo);
        check("real git_log: not error", !r.isError);
        QJsonObject body = parseToolBody(r);
        QJsonObject result = body.value("result").toObject();
        QJsonArray commits = result.value("commits").toArray();
        check("real git_log: count > 0", commits.size() > 0);

        bool sawV58 = false;
        for (const auto &v : commits) {
            QString subj = v.toObject().value("subject").toString();
            if (subj.contains("v0.1.58") || subj.contains("0.1.58")) {
                sawV58 = true;
                break;
            }
        }
        check("real git_log: contains v0.1.58 commit", sawV58);
    }

    // git_branch_list → expect main
    {
        AiTools::ToolCall call;
        call.id = "rb";
        call.name = "git_branch_list";
        AiTools::ToolResult r = AiTools::execute(call, repo);
        check("real git_branch_list: not error", !r.isError);
        QJsonObject body = parseToolBody(r);
        QJsonObject result = body.value("result").toObject();
        QJsonArray branches = result.value("branches").toArray();
        bool sawMain = false;
        for (const auto &v : branches) {
            if (v.toObject().value("name").toString() == "main") {
                sawMain = true;
                break;
            }
        }
        check("real git_branch_list: contains 'main'", sawMain);
    }

    // git_status → report what's there (we don't enforce clean since
    // there might be untracked files).
    {
        AiTools::ToolCall call;
        call.id = "rs";
        call.name = "git_status";
        AiTools::ToolResult r = AiTools::execute(call, repo);
        check("real git_status: not error", !r.isError);
        QJsonObject body = parseToolBody(r);
        QJsonObject result = body.value("result").toObject();
        check("real git_status: branch present (non-empty)",
              !result.value("branch").toString().isEmpty());
        QJsonArray staged    = result.value("staged").toArray();
        QJsonArray modified  = result.value("modified").toArray();
        QJsonArray untracked = result.value("untracked").toArray();
        fprintf(stdout, "  (real-repo state: staged=%d modified=%d untracked=%d)\n",
                staged.size(), modified.size(), untracked.size());
    }

    // git_show HEAD → succeeds, contains commit info
    {
        AiTools::ToolCall call;
        call.id = "rsh";
        call.name = "git_show";
        call.args = QJsonObject{{"commit", "HEAD"}};
        AiTools::ToolResult r = AiTools::execute(call, repo);
        check("real git_show HEAD: not error", !r.isError);
        QJsonObject body = parseToolBody(r);
        QJsonObject result = body.value("result").toObject();
        check("real git_show HEAD: text non-empty",
              !result.value("text").toString().isEmpty());
    }
}

// ── 8) Results writer ────────────────────────────────────────────────────
//
// Writes a JSON results blob to tests/comprehensive/results/git_tools.json.
static void writeResultsJson(const QString &outPath, int total,
                             int passed, int failed) {
    QFile f(outPath);
    if (!f.open(QFile::WriteOnly | QFile::Truncate)) {
        fprintf(stderr, "warn: cannot write %s\n", qPrintable(outPath));
        return;
    }
    QJsonObject doc;
    doc["test"]      = "git_tools_e2e";
    doc["scenarios"] = total;
    doc["pass"]      = passed;
    doc["fail"]      = failed;

    QJsonArray failures;
    for (const FailDetail &d : g_failDetails) {
        failures.push_back(d.label);
    }
    QJsonObject details;
    details["failures"] = failures;
    details["suites"]   = QJsonArray{
        "no_workspace_errors",
        "not_a_repo",
        "git_status (clean/dirty/staged)",
        "git_diff (working/staged/path/traversal)",
        "git_log (default/n/clamp/path/traversal)",
        "git_branch_list (main + feature/x)",
        "git_show (sha/short/HEAD/HEAD~1/branch/error paths)",
        "real-repo integration sweep"
    };
    doc["details"] = details;

    f.write(QJsonDocument(doc).toJson(QJsonDocument::Indented));
    f.close();
}

int main(int argc, char *argv[]) {
    QCoreApplication app(argc, argv);

    // ── Setup: temp non-repo dir + temp repo dir ─────────────────────────
    QTemporaryDir tmpNonRepo;
    if (!tmpNonRepo.isValid()) {
        fprintf(stderr, "FATAL: cannot create temp non-repo dir\n");
        return 1;
    }
    QTemporaryDir tmpRepo;
    if (!tmpRepo.isValid()) {
        fprintf(stderr, "FATAL: cannot create temp repo dir\n");
        return 1;
    }

    RepoFixture fx;
    if (!buildFixture(tmpRepo, fx)) {
        fprintf(stderr, "FATAL: buildFixture failed\n");
        return 1;
    }
    fprintf(stdout, "Fixture built: ws=%s sha1=%s sha2=%s sha3=%s\n",
            qPrintable(fx.ws), qPrintable(fx.sha1.left(7)),
            qPrintable(fx.sha2.left(7)), qPrintable(fx.sha3.left(7)));

    testNoWorkspaceErrors();
    testNotARepo(tmpNonRepo.path());
    testGitStatus(fx);
    testGitDiff(fx);
    testGitLog(fx);
    testGitBranchList(fx);
    testGitShow(fx);
    testRealRepo();

    int total = g_pass + g_fail;
    fprintf(stdout, "\n=========================================\n");
    fprintf(stdout, "  total: %d   pass: %d   fail: %d\n",
            total, g_pass, g_fail);
    fprintf(stdout, "=========================================\n");

    // Write results.json. Path is overridable via NOTEPATRA_RESULTS_DIR
    // (the comprehensive harness sets this); otherwise default to a
    // relative path so checked-in source has no machine-specific paths.
    QString resultsDir = QString::fromLocal8Bit(qgetenv("NOTEPATRA_RESULTS_DIR"));
    if (resultsDir.isEmpty())
        resultsDir = QStringLiteral("tests/comprehensive/results");
    QString resultsPath = resultsDir + "/git_tools.json";
    QDir().mkpath(QFileInfo(resultsPath).absolutePath());
    writeResultsJson(resultsPath, total, g_pass, g_fail);

    return g_fail == 0 ? 0 : 1;
}
