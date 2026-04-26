// test_ai_tools.cpp — verify agentic file-reading tools are wired safely
// against the workspace root. Catches regressions in the path-safety
// layer (workspace anchor, symlink resolve, hardcoded deny-list) and
// verifies tool execution returns properly-shaped JSON results.
//
// Three layers tested:
//   1. AiTools::isHardDenied — hardcoded credential-path filter
//   2. AiTools::resolveSafePath — workspace anchor + canonicalize
//   3. AiTools::execute — read_file / list_dir end-to-end
//
// v0.1.35 ship requirement.

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QString>
#include <QTemporaryDir>
#include <cstdio>

#include "ai_tools.h"

static int g_pass = 0;
static int g_fail = 0;

static void check(const char *label, bool ok) {
    if (ok) { ++g_pass; fprintf(stdout, "  ok   %s\n", label); }
    else    { ++g_fail; fprintf(stderr, "  FAIL %s\n", label); }
}

static void writeFile(const QString &path, const QByteArray &content) {
    QFile f(path);
    f.open(QFile::WriteOnly);
    f.write(content);
    f.close();
}

int main(int argc, char *argv[]) {
    QCoreApplication app(argc, argv);

    // ── Layer 1: isHardDenied — credential pattern matching ──────────
    {
        check("denies ~/.ssh/id_rsa",       AiTools::isHardDenied("/home/u/.ssh/id_rsa"));
        check("denies ~/.ssh/id_ed25519",   AiTools::isHardDenied("/home/u/.ssh/id_ed25519"));
        check("denies ~/.ssh/id_rsa.pub",   AiTools::isHardDenied("/home/u/.ssh/id_rsa.pub"));
        check("denies ~/.gnupg/pubring.kbx",AiTools::isHardDenied("/home/u/.gnupg/pubring.kbx"));
        check("denies ~/.aws/credentials",  AiTools::isHardDenied("/home/u/.aws/credentials"));
        check("denies ~/.netrc",            AiTools::isHardDenied("/home/u/.netrc"));
        check("denies /etc/passwd",         AiTools::isHardDenied("/etc/passwd"));
        check("denies /etc/shadow",         AiTools::isHardDenied("/etc/shadow"));
        check("denies *.pem",               AiTools::isHardDenied("/home/u/cert.pem"));
        check("denies *.key",               AiTools::isHardDenied("/home/u/server.key"));
        check("denies authorized_keys",     AiTools::isHardDenied("/home/u/.ssh/authorized_keys"));
        check("denies known_hosts",         AiTools::isHardDenied("/home/u/.ssh/known_hosts"));
        check("denies .npmrc",              AiTools::isHardDenied("/home/u/.npmrc"));
        check("denies docker config.json",  AiTools::isHardDenied("/home/u/.docker/config.json"));

        // Negatives — these should NOT be denied
        check("allows project README.md",   !AiTools::isHardDenied("/home/u/myproj/README.md"));
        check("allows project src/main.rs", !AiTools::isHardDenied("/home/u/myproj/src/main.rs"));
        check("allows project assh.txt",    !AiTools::isHardDenied("/home/u/myproj/assh.txt"));
        check("allows non-cred .pem-named-dir/file.txt",
              !AiTools::isHardDenied("/home/u/myproj/test.pem.txt"));
    }

    // ── Layer 2: resolveSafePath — workspace anchor ──────────────────
    QTemporaryDir tmp;
    if (!tmp.isValid()) {
        fprintf(stderr, "FAIL: couldn't create temp workspace\n");
        return 1;
    }
    const QString ws = tmp.path();
    QDir(ws).mkpath("src");
    writeFile(ws + "/README.md", "# Hello\nThis is a project.\n");
    writeFile(ws + "/src/main.rs", "fn main() { println!(\"hi\"); }\n");

    {
        QString out;
        QString err;
        check("resolves README.md inside workspace",
              AiTools::resolveSafePath("README.md", ws, &out, &err)
              && out.endsWith("README.md"));
        check("resolves src/main.rs inside workspace",
              AiTools::resolveSafePath("src/main.rs", ws, &out, &err)
              && out.endsWith("main.rs"));
        check("resolves \".\" to workspace root",
              AiTools::resolveSafePath(".", ws, &out, &err)
              && QDir(out).canonicalPath() == QDir(ws).canonicalPath());

        out.clear(); err.clear();
        check("REJECTS ../../etc/passwd traversal",
              !AiTools::resolveSafePath("../../etc/passwd", ws, &out, &err));
        check("  errorKind = outside_workspace or not_found",
              err == "outside_workspace" || err == "not_found");

        out.clear(); err.clear();
        check("REJECTS absolute path outside workspace",
              !AiTools::resolveSafePath("/etc/passwd", ws, &out, &err));

        out.clear(); err.clear();
        check("REJECTS empty path",
              !AiTools::resolveSafePath("", ws, &out, &err));

        out.clear(); err.clear();
        check("REJECTS missing file (not_found)",
              !AiTools::resolveSafePath("nonexistent.txt", ws, &out, &err)
              && err == "not_found");
    }

    // ── Layer 3: execute — read_file end-to-end ──────────────────────
    {
        AiTools::ToolCall call;
        call.id = "t1";
        call.name = "read_file";
        QJsonObject args;
        args["path"] = "README.md";
        call.args = args;

        AiTools::ToolResult r = AiTools::execute(call, ws);
        check("read_file: ok=true on README.md", !r.isError);
        QJsonDocument jd = QJsonDocument::fromJson(r.content.toUtf8());
        QJsonObject body = jd.object();
        check("  body.ok == true", body.value("ok").toBool());
        QJsonObject result = body.value("result").toObject();
        check("  result.path == README.md", result.value("path").toString() == "README.md");
        check("  result.content contains '# Hello'",
              result.value("content").toString().contains("# Hello"));
        check("  result.content has line numbers",
              result.value("content").toString().contains("\t# Hello"));
        check("  result.lines_emitted >= 2", result.value("lines_emitted").toInt() >= 2);
    }

    // Read with offset/limit pagination
    {
        // Write a 50-line file, ask for lines 10-15
        QString path = ws + "/log.txt";
        QString text;
        for (int i = 1; i <= 50; ++i)
            text += QString("line %1\n").arg(i);
        writeFile(path, text.toUtf8());

        AiTools::ToolCall call;
        call.name = "read_file";
        QJsonObject args;
        args["path"] = "log.txt";
        args["offset"] = 10;
        args["limit"] = 5;
        call.args = args;
        AiTools::ToolResult r = AiTools::execute(call, ws);
        check("read_file with offset+limit: ok=true", !r.isError);
        QJsonObject result = QJsonDocument::fromJson(r.content.toUtf8())
                                .object().value("result").toObject();
        check("  lines_emitted == 5", result.value("lines_emitted").toInt() == 5);
        check("  total_lines == 50", result.value("total_lines").toInt() == 50);
        check("  truncated == true", result.value("truncated").toBool());
        check("  content includes 'line 10'",
              result.value("content").toString().contains("line 10"));
        check("  content does NOT include 'line 9'",
              !result.value("content").toString().contains("line 9\n"));
    }

    // Read outside workspace → error
    {
        AiTools::ToolCall call;
        call.id = "t-deny";
        call.name = "read_file";
        QJsonObject args;
        args["path"] = "/etc/passwd";
        call.args = args;
        AiTools::ToolResult r = AiTools::execute(call, ws);
        check("read_file('/etc/passwd'): isError=true", r.isError);
        check("  errorKind == outside_workspace OR denied",
              r.errorKind == "outside_workspace" || r.errorKind == "denied");
    }

    // Binary file → error_kind: binary
    {
        QString path = ws + "/blob.bin";
        QByteArray bin;
        for (int i = 0; i < 1000; ++i) {
            bin.append(char(i % 256));
        }
        writeFile(path, bin);

        AiTools::ToolCall call;
        call.name = "read_file";
        QJsonObject args;
        args["path"] = "blob.bin";
        call.args = args;
        AiTools::ToolResult r = AiTools::execute(call, ws);
        check("read_file(binary): isError=true", r.isError);
        check("  errorKind == binary", r.errorKind == "binary");
    }

    // Layer 3: list_dir
    {
        AiTools::ToolCall call;
        call.id = "ld1";
        call.name = "list_dir";
        QJsonObject args;
        args["path"] = ".";
        call.args = args;
        AiTools::ToolResult r = AiTools::execute(call, ws);
        check("list_dir('.'): ok=true", !r.isError);
        QJsonObject result = QJsonDocument::fromJson(r.content.toUtf8())
                                .object().value("result").toObject();
        const QJsonArray entries = result.value("entries").toArray();
        // Expect README.md, src dir, log.txt, blob.bin = 4 entries
        check("  entries >= 4", entries.size() >= 4);

        // Check every entry has name + type
        bool allShaped = true;
        for (const QJsonValue &ev : entries) {
            QJsonObject e = ev.toObject();
            if (!e.contains("name") || !e.contains("type")) {
                allShaped = false;
                break;
            }
        }
        check("  all entries have name + type", allShaped);
    }

    // list_dir on a subdir
    {
        AiTools::ToolCall call;
        call.name = "list_dir";
        QJsonObject args;
        args["path"] = "src";
        call.args = args;
        AiTools::ToolResult r = AiTools::execute(call, ws);
        check("list_dir('src'): ok=true", !r.isError);
        QJsonObject result = QJsonDocument::fromJson(r.content.toUtf8())
                                .object().value("result").toObject();
        check("  src has 1 entry (main.rs)",
              result.value("entries").toArray().size() == 1);
    }

    // Filter junk dirs (.git, node_modules, etc.) from list_dir output
    {
        QDir(ws).mkpath(".git");
        QDir(ws).mkpath("node_modules");
        AiTools::ToolCall call;
        call.name = "list_dir";
        QJsonObject args;
        args["path"] = ".";
        call.args = args;
        AiTools::ToolResult r = AiTools::execute(call, ws);
        QJsonObject result = QJsonDocument::fromJson(r.content.toUtf8())
                                .object().value("result").toObject();
        const QJsonArray entries = result.value("entries").toArray();
        bool sawGit = false;
        bool sawNodeModules = false;
        for (const QJsonValue &ev : entries) {
            const QString n = ev.toObject().value("name").toString();
            if (n == ".git") sawGit = true;
            if (n == "node_modules") sawNodeModules = true;
        }
        check("list_dir filters .git",        !sawGit);
        check("list_dir filters node_modules", !sawNodeModules);
    }

    // Tool registry — verify availableTools() returns valid JSONSchema
    {
        QJsonArray tools = AiTools::availableTools();
        check("availableTools() has 2+ entries", tools.size() >= 2);
        for (const QJsonValue &tv : tools) {
            QJsonObject t = tv.toObject();
            check("  each tool has type=function",
                  t.value("type").toString() == "function");
            QJsonObject fn = t.value("function").toObject();
            check("  each tool has function.name",
                  !fn.value("name").toString().isEmpty());
            check("  each tool has function.description",
                  !fn.value("description").toString().isEmpty());
            check("  each tool has function.parameters object",
                  fn.value("parameters").isObject());
        }
    }

    // Model allowlist — happy paths and rejections
    {
        check("modelLikelySupportsTools(qwen3:7b)",
              AiTools::modelLikelySupportsTools("qwen3:7b"));
        check("modelLikelySupportsTools(llama3.1:8b)",
              AiTools::modelLikelySupportsTools("llama3.1:8b"));
        check("modelLikelySupportsTools(hermes3:8b)",
              AiTools::modelLikelySupportsTools("hermes3:8b"));
        check("modelLikelySupportsTools(gpt-4o)",
              AiTools::modelLikelySupportsTools("gpt-4o"));
        check("modelLikelySupportsTools(claude-3.5-sonnet)",
              AiTools::modelLikelySupportsTools("claude-3.5-sonnet"));
        check("modelLikelySupportsTools(openai/gpt-4o)",
              AiTools::modelLikelySupportsTools("openai/gpt-4o"));
        check("modelLikelySupportsTools(anthropic/claude-3.5-sonnet)",
              AiTools::modelLikelySupportsTools("anthropic/claude-3.5-sonnet"));

        check("REJECTS phi-3-mini",  !AiTools::modelLikelySupportsTools("phi-3-mini"));
        check("REJECTS gemma-2-2b",  !AiTools::modelLikelySupportsTools("gemma-2-2b"));
        check("REJECTS empty",       !AiTools::modelLikelySupportsTools(""));
    }

    fprintf(stdout, "\n=== test_ai_tools: %d passed, %d failed ===\n", g_pass, g_fail);
    return g_fail == 0 ? 0 : 1;
}
