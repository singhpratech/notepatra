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

    // Read outside workspace → error.
    // On Linux: /etc/passwd exists; canonicalize succeeds; workspace
    //   anchor check fails → "outside_workspace" (or "denied" via
    //   the hardcoded /etc/passwd entry in the deny-list).
    // On Windows: /etc/passwd doesn't exist; canonicalFilePath()
    //   returns empty → "not_found" before workspace check.
    // Both are correct refusals — test accepts all three.
    {
        AiTools::ToolCall call;
        call.id = "t-deny";
        call.name = "read_file";
        QJsonObject args;
        args["path"] = "/etc/passwd";
        call.args = args;
        AiTools::ToolResult r = AiTools::execute(call, ws);
        check("read_file('/etc/passwd'): isError=true", r.isError);
        check("  errorKind is a refusal (outside_workspace / denied / not_found)",
              r.errorKind == "outside_workspace"
              || r.errorKind == "denied"
              || r.errorKind == "not_found");
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

    // ── v0.1.39: write_file ───────────────────────────────────────────
    {
        AiTools::ToolCall call;
        call.name = "write_file";
        QJsonObject args;
        args["path"]    = "new/hello.py";
        args["content"] = "print('hi')\n";
        call.args = args;
        AiTools::ToolResult r = AiTools::execute(call, ws);
        check("write_file: creates new file (default overwrite)", !r.isError);
        QJsonObject result = QJsonDocument::fromJson(r.content.toUtf8())
                                .object().value("result").toObject();
        check("  result.created == true (new file)", result.value("created").toBool());
        check("  result.bytes_written == 12", result.value("bytes_written").toInt() == 12);
        check("  result.mode == overwrite", result.value("mode").toString() == "overwrite");
        check("  result.abs_path is absolute",
              QDir::isAbsolutePath(result.value("abs_path").toString()));
        // Verify the file actually exists with correct content.
        QFile fread(ws + "/new/hello.py");
        fread.open(QFile::ReadOnly);
        check("  file on disk has expected content",
              fread.readAll() == QByteArray("print('hi')\n"));
        fread.close();
    }

    // write_file mode=create with a new path → ok, created=true
    {
        AiTools::ToolCall call;
        call.name = "write_file";
        QJsonObject args;
        args["path"]    = "fresh.txt";
        args["content"] = "fresh\n";
        args["mode"]    = "create";
        call.args = args;
        AiTools::ToolResult r = AiTools::execute(call, ws);
        check("write_file mode=create on new path: ok", !r.isError);
    }

    // write_file mode=create on existing path → error_kind:exists
    {
        AiTools::ToolCall call;
        call.name = "write_file";
        QJsonObject args;
        args["path"]    = "fresh.txt";
        args["content"] = "different\n";
        args["mode"]    = "create";
        call.args = args;
        AiTools::ToolResult r = AiTools::execute(call, ws);
        check("write_file mode=create on existing: errors", r.isError);
        check("  errorKind == exists", r.errorKind == "exists");
    }

    // write_file mode=overwrite → succeeds, created=false
    {
        AiTools::ToolCall call;
        call.name = "write_file";
        QJsonObject args;
        args["path"]    = "fresh.txt";
        args["content"] = "v2\n";
        args["mode"]    = "overwrite";
        call.args = args;
        AiTools::ToolResult r = AiTools::execute(call, ws);
        check("write_file mode=overwrite: ok", !r.isError);
        QJsonObject result = QJsonDocument::fromJson(r.content.toUtf8())
                                .object().value("result").toObject();
        check("  result.created == false (was already there)",
              !result.value("created").toBool());
    }

    // write_file mode=append → adds to end
    {
        AiTools::ToolCall call;
        call.name = "write_file";
        QJsonObject args;
        args["path"]    = "fresh.txt";
        args["content"] = "tail\n";
        args["mode"]    = "append";
        call.args = args;
        AiTools::ToolResult r = AiTools::execute(call, ws);
        check("write_file mode=append: ok", !r.isError);
        QFile fread(ws + "/fresh.txt");
        fread.open(QFile::ReadOnly);
        check("  on-disk content is concatenation",
              fread.readAll() == QByteArray("v2\ntail\n"));
        fread.close();
    }

    // write_file traversal attempt → denied / outside_workspace
    {
        AiTools::ToolCall call;
        call.name = "write_file";
        QJsonObject args;
        args["path"]    = "../escape.txt";
        args["content"] = "x";
        call.args = args;
        AiTools::ToolResult r = AiTools::execute(call, ws);
        check("write_file('../escape.txt'): refused", r.isError);
        check("  errorKind is a refusal",
              r.errorKind == "outside_workspace"
              || r.errorKind == "denied"
              || r.errorKind == "not_found");
    }

    // write_file to deny-listed path inside workspace → denied
    // (Simulate a hardcoded-deny path: a .pem file inside the workspace.
    //  The deny-list catches *.pem regardless of where it is.)
    {
        AiTools::ToolCall call;
        call.name = "write_file";
        QJsonObject args;
        args["path"]    = "creds.pem";
        args["content"] = "-----BEGIN PRIVATE KEY-----\n";
        call.args = args;
        AiTools::ToolResult r = AiTools::execute(call, ws);
        check("write_file('creds.pem'): refused by deny-list", r.isError);
        check("  errorKind == denied", r.errorKind == "denied");
    }

    // ── v0.1.39: search ──────────────────────────────────────────────
    {
        // Seed some content to search against.
        writeFile(ws + "/src/util.rs", "fn util() { println!(\"hi\"); }\n");
        AiTools::ToolCall call;
        call.name = "search";
        QJsonObject args;
        args["pattern"] = "println";
        call.args = args;
        AiTools::ToolResult r = AiTools::execute(call, ws);
        check("search('println'): ok", !r.isError);
        QJsonObject result = QJsonDocument::fromJson(r.content.toUtf8())
                                .object().value("result").toObject();
        check("  total_matches >= 2 (main.rs + util.rs)",
              result.value("total_matches").toInt() >= 2);
        check("  matches array shape: each entry has path + line",
              result.value("matches").toArray().first().toObject().contains("path")
              && result.value("matches").toArray().first().toObject().contains("line"));
    }

    // search with glob filter
    {
        AiTools::ToolCall call;
        call.name = "search";
        QJsonObject args;
        args["pattern"] = "println";
        args["glob"]    = "*.txt";
        call.args = args;
        AiTools::ToolResult r = AiTools::execute(call, ws);
        check("search with glob='*.txt': no .rs matches", !r.isError);
        QJsonObject result = QJsonDocument::fromJson(r.content.toUtf8())
                                .object().value("result").toObject();
        check("  total_matches == 0 (only .txt scanned)",
              result.value("total_matches").toInt() == 0);
    }

    // search with regex
    {
        AiTools::ToolCall call;
        call.name = "search";
        QJsonObject args;
        args["pattern"] = "fn\\s+\\w+\\(\\)";
        args["regex"]   = true;
        call.args = args;
        AiTools::ToolResult r = AiTools::execute(call, ws);
        check("search regex: ok", !r.isError);
        QJsonObject result = QJsonDocument::fromJson(r.content.toUtf8())
                                .object().value("result").toObject();
        check("  matches >= 2 (main + util)",
              result.value("total_matches").toInt() >= 2);
    }

    // search with empty pattern → error
    {
        AiTools::ToolCall call;
        call.name = "search";
        QJsonObject args;
        args["pattern"] = "";
        call.args = args;
        AiTools::ToolResult r = AiTools::execute(call, ws);
        check("search(''): rejects empty pattern", r.isError);
    }

    // ── v0.1.39: apply_diff ──────────────────────────────────────────
    {
        // Set up: 5-line file we'll edit.
        writeFile(ws + "/diff_target.txt",
                  "line A\nline B\nline C\nline D\nline E\n");
        AiTools::ToolCall call;
        call.name = "apply_diff";
        QJsonObject args;
        args["path"] = "diff_target.txt";
        QJsonArray hunks;
        QJsonObject h1;
        h1["old_start_line"] = 2;
        h1["old_lines"]      = "line B\n";
        h1["new_lines"]      = "line B (edited)\n";
        hunks.append(h1);
        args["hunks"] = hunks;
        call.args = args;
        AiTools::ToolResult r = AiTools::execute(call, ws);
        check("apply_diff: single-hunk success", !r.isError);
        QFile fread(ws + "/diff_target.txt");
        fread.open(QFile::ReadOnly);
        const QByteArray after = fread.readAll();
        fread.close();
        check("  file content contains 'line B (edited)'",
              after.contains("line B (edited)"));
        check("  unchanged lines preserved",
              after.contains("line A\n") && after.contains("line E\n"));
    }

    // apply_diff with conflict (old_lines doesn't match) → file untouched
    {
        // First reset target.
        writeFile(ws + "/diff_target.txt",
                  "line A\nline B\nline C\nline D\nline E\n");
        AiTools::ToolCall call;
        call.name = "apply_diff";
        QJsonObject args;
        args["path"] = "diff_target.txt";
        QJsonArray hunks;
        QJsonObject h1;
        h1["old_start_line"] = 2;
        h1["old_lines"]      = "line WRONG\n";  // doesn't match!
        h1["new_lines"]      = "should never appear\n";
        hunks.append(h1);
        args["hunks"] = hunks;
        call.args = args;
        AiTools::ToolResult r = AiTools::execute(call, ws);
        check("apply_diff conflict: errors", r.isError);
        check("  errorKind == conflict", r.errorKind == "conflict");
        QFile fread(ws + "/diff_target.txt");
        fread.open(QFile::ReadOnly);
        const QByteArray after = fread.readAll();
        fread.close();
        check("  file is UNCHANGED on conflict (atomic)",
              !after.contains("should never appear")
              && after.contains("line B\n"));
    }

    // apply_diff multiple hunks (must be applied in REVERSE order so
    // earlier line numbers stay stable). Test: edit lines 2 and 4.
    {
        writeFile(ws + "/diff_target.txt",
                  "line A\nline B\nline C\nline D\nline E\n");
        AiTools::ToolCall call;
        call.name = "apply_diff";
        QJsonObject args;
        args["path"] = "diff_target.txt";
        QJsonArray hunks;
        QJsonObject h1;
        h1["old_start_line"] = 2;
        h1["old_lines"]      = "line B\n";
        h1["new_lines"]      = "BB\n";
        QJsonObject h2;
        h2["old_start_line"] = 4;
        h2["old_lines"]      = "line D\n";
        h2["new_lines"]      = "DD\n";
        hunks.append(h1);
        hunks.append(h2);
        args["hunks"] = hunks;
        call.args = args;
        AiTools::ToolResult r = AiTools::execute(call, ws);
        check("apply_diff multi-hunk: ok", !r.isError);
        QFile fread(ws + "/diff_target.txt");
        fread.open(QFile::ReadOnly);
        const QByteArray after = fread.readAll();
        fread.close();
        check("  result is 'line A\\nBB\\nline C\\nDD\\nline E\\n'",
              after == QByteArray("line A\nBB\nline C\nDD\nline E\n"));
    }

    // apply_diff on missing file → not_found
    {
        AiTools::ToolCall call;
        call.name = "apply_diff";
        QJsonObject args;
        args["path"] = "nonexistent.txt";
        QJsonArray hunks;
        QJsonObject h1;
        h1["old_start_line"] = 1;
        h1["old_lines"]      = "x\n";
        h1["new_lines"]      = "y\n";
        hunks.append(h1);
        args["hunks"] = hunks;
        call.args = args;
        AiTools::ToolResult r = AiTools::execute(call, ws);
        check("apply_diff on missing file: errors", r.isError);
    }

    // ── v0.1.39 deeper edge cases ────────────────────────────────────

    // write_file: empty content (zero-length file is legitimate)
    {
        AiTools::ToolCall call;
        call.name = "write_file";
        QJsonObject args;
        args["path"]    = "empty.txt";
        args["content"] = "";
        call.args = args;
        AiTools::ToolResult r = AiTools::execute(call, ws);
        check("write_file: empty content writes 0-byte file", !r.isError);
        QJsonObject res = QJsonDocument::fromJson(r.content.toUtf8())
                              .object().value("result").toObject();
        check("  bytes_written == 0", res.value("bytes_written").toInt() == 0);
    }

    // write_file: invalid mode → error (defensive — rejects unknown modes)
    {
        AiTools::ToolCall call;
        call.name = "write_file";
        QJsonObject args;
        args["path"]    = "weird.txt";
        args["content"] = "x";
        args["mode"]    = "totally-not-a-mode";
        call.args = args;
        AiTools::ToolResult r = AiTools::execute(call, ws);
        check("write_file: unknown mode rejected", r.isError);
    }

    // search case-sensitivity respected
    {
        writeFile(ws + "/case.txt", "Apple\napple\nAPPLE\n");
        AiTools::ToolCall call;
        call.name = "search";
        QJsonObject args;
        args["pattern"]        = "apple";
        args["case_sensitive"] = true;
        args["glob"]           = "case.txt";
        call.args = args;
        AiTools::ToolResult r = AiTools::execute(call, ws);
        check("search case_sensitive=true: ok", !r.isError);
        QJsonObject res = QJsonDocument::fromJson(r.content.toUtf8())
                              .object().value("result").toObject();
        check("  finds exactly 1 lowercase match",
              res.value("total_matches").toInt() == 1);
    }

    // search case_sensitive=false (default) finds all 3
    {
        AiTools::ToolCall call;
        call.name = "search";
        QJsonObject args;
        args["pattern"] = "apple";
        args["glob"]    = "case.txt";
        call.args = args;
        AiTools::ToolResult r = AiTools::execute(call, ws);
        QJsonObject res = QJsonDocument::fromJson(r.content.toUtf8())
                              .object().value("result").toObject();
        check("search case-insensitive (default): finds all 3 cases",
              res.value("total_matches").toInt() == 3);
    }

    // apply_diff with hunks supplied OUT OF ORDER — internal sort
    // should fix this and apply correctly.
    {
        writeFile(ws + "/diff_target.txt",
                  "line A\nline B\nline C\nline D\nline E\n");
        AiTools::ToolCall call;
        call.name = "apply_diff";
        QJsonObject args;
        args["path"] = "diff_target.txt";
        QJsonArray hunks;
        // Submit out of order: line 4 BEFORE line 2.
        QJsonObject h2; h2["old_start_line"] = 4;
        h2["old_lines"] = "line D\n"; h2["new_lines"] = "DD2\n";
        QJsonObject h1; h1["old_start_line"] = 2;
        h1["old_lines"] = "line B\n"; h1["new_lines"] = "BB2\n";
        hunks.append(h2);
        hunks.append(h1);
        args["hunks"] = hunks;
        call.args = args;
        AiTools::ToolResult r = AiTools::execute(call, ws);
        check("apply_diff: hunks supplied out-of-order still apply correctly", !r.isError);
        QFile fread(ws + "/diff_target.txt");
        fread.open(QFile::ReadOnly);
        const QByteArray after = fread.readAll();
        fread.close();
        check("  result is 'line A\\nBB2\\nline C\\nDD2\\nline E\\n'",
              after == QByteArray("line A\nBB2\nline C\nDD2\nline E\n"));
    }

    // apply_diff on a deny-listed path → refused (deny-list catches
    // even if the file exists in the workspace)
    {
        writeFile(ws + "/secret.pem", "----BEGIN PRIVATE KEY----\n");
        AiTools::ToolCall call;
        call.name = "apply_diff";
        QJsonObject args;
        args["path"] = "secret.pem";
        QJsonArray hunks;
        QJsonObject h1; h1["old_start_line"] = 1;
        h1["old_lines"] = "----BEGIN PRIVATE KEY----\n";
        h1["new_lines"] = "tampered\n";
        hunks.append(h1);
        args["hunks"] = hunks;
        call.args = args;
        AiTools::ToolResult r = AiTools::execute(call, ws);
        check("apply_diff('secret.pem'): refused by deny-list", r.isError);
        check("  errorKind == denied", r.errorKind == "denied");
    }

    // Unknown tool name routes through execute() → io_error refusal
    {
        AiTools::ToolCall call;
        call.name = "rm_rf";  // not a real tool
        call.args = QJsonObject{};
        AiTools::ToolResult r = AiTools::execute(call, ws);
        check("execute(unknown tool): refused", r.isError);
        check("  errorKind == io_error", r.errorKind == "io_error");
    }

    // apply_diff on a hunk that references lines beyond EOF → conflict
    {
        writeFile(ws + "/short.txt", "only one line\n");
        AiTools::ToolCall call;
        call.name = "apply_diff";
        QJsonObject args;
        args["path"] = "short.txt";
        QJsonArray hunks;
        QJsonObject h1; h1["old_start_line"] = 99;
        h1["old_lines"] = "missing\n"; h1["new_lines"] = "x\n";
        hunks.append(h1);
        args["hunks"] = hunks;
        call.args = args;
        AiTools::ToolResult r = AiTools::execute(call, ws);
        check("apply_diff: line 99 of 1-line file → conflict", r.isError);
        check("  errorKind == conflict", r.errorKind == "conflict");
        QFile fread(ws + "/short.txt");
        fread.open(QFile::ReadOnly);
        check("  file unchanged on out-of-bounds hunk",
              fread.readAll() == QByteArray("only one line\n"));
        fread.close();
    }

    // ── v0.1.40: apply_diff three-tier match — strict → strip-prefix → trimmed
    //
    // Tier 1 — model echoed read_file's "      N\t" prefix verbatim into
    // old_lines. The tool strips the prefix before matching, applies the
    // edit, and reports it in result.warnings.
    {
        writeFile(ws + "/diff_target.txt",
                  "line A\nline B\nline C\nline D\nline E\n");
        AiTools::ToolCall call;
        call.name = "apply_diff";
        QJsonObject args;
        args["path"] = "diff_target.txt";
        QJsonArray hunks;
        QJsonObject h1;
        h1["old_start_line"] = 2;
        // The leading "     2\t" mimics what read_file emits with the
        // default with_line_numbers=true. Without the v0.1.40 fallback this
        // would conflict against the file's clean "line B".
        h1["old_lines"] = "     2\tline B\n";
        h1["new_lines"] = "line B (edited)\n";
        hunks.append(h1);
        args["hunks"] = hunks;
        call.args = args;
        AiTools::ToolResult r = AiTools::execute(call, ws);
        check("apply_diff tier-1: prefix-stripped match applies",
              !r.isError);
        // Verify the warning surfaced.
        QJsonDocument jd = QJsonDocument::fromJson(r.content.toUtf8());
        QJsonObject body = jd.object().value("result").toObject();
        const QJsonArray warnings = body.value("warnings").toArray();
        check("  result.warnings is non-empty",
              warnings.size() == 1);
        if (warnings.size() == 1) {
            const QString w = warnings.at(0).toString();
            check("  warning mentions stripping line-number prefix",
                  w.contains("read_file line-number prefix"));
        }
        QFile fread(ws + "/diff_target.txt");
        fread.open(QFile::ReadOnly);
        const QByteArray after = fread.readAll();
        fread.close();
        check("  file actually edited",
              after.contains("line B (edited)"));
    }

    // Tier 2 — old_lines has whitespace-only differences vs the file
    // (trailing space, slightly different leading indent). Tool retries
    // with .trimmed() and applies, with a separate warning that asks the
    // model to verify new_lines indentation.
    {
        writeFile(ws + "/diff_target.txt",
                  "line A\n    indented B\nline C\n");
        AiTools::ToolCall call;
        call.name = "apply_diff";
        QJsonObject args;
        args["path"] = "diff_target.txt";
        QJsonArray hunks;
        QJsonObject h1;
        h1["old_start_line"] = 2;
        h1["old_lines"] = "indented B\n";  // missing 4-space indent
        h1["new_lines"] = "    indented B (edited)\n";
        hunks.append(h1);
        args["hunks"] = hunks;
        call.args = args;
        AiTools::ToolResult r = AiTools::execute(call, ws);
        check("apply_diff tier-2: whitespace-trimmed match applies",
              !r.isError);
        QJsonDocument jd = QJsonDocument::fromJson(r.content.toUtf8());
        QJsonObject body = jd.object().value("result").toObject();
        const QJsonArray warnings = body.value("warnings").toArray();
        check("  result.warnings has whitespace-normalisation entry",
              warnings.size() == 1);
        if (warnings.size() == 1) {
            const QString w = warnings.at(0).toString();
            check("  warning mentions whitespace normalization",
                  w.contains("whitespace"));
        }
    }

    // Tier 0 (strict) still wins when old_lines matches byte-exactly —
    // verify NO warnings appear so we don't spam the model on clean calls.
    {
        writeFile(ws + "/diff_target.txt",
                  "line A\nline B\nline C\n");
        AiTools::ToolCall call;
        call.name = "apply_diff";
        QJsonObject args;
        args["path"] = "diff_target.txt";
        QJsonArray hunks;
        QJsonObject h1;
        h1["old_start_line"] = 2;
        h1["old_lines"] = "line B\n";  // exact match
        h1["new_lines"] = "line B (clean)\n";
        hunks.append(h1);
        args["hunks"] = hunks;
        call.args = args;
        AiTools::ToolResult r = AiTools::execute(call, ws);
        check("apply_diff strict tier: clean match applies",
              !r.isError);
        QJsonDocument jd = QJsonDocument::fromJson(r.content.toUtf8());
        QJsonObject body = jd.object().value("result").toObject();
        check("  no warnings on clean strict match",
              !body.contains("warnings"));
    }

    // True conflict (content actually differs even after trim) → still
    // refuses, file untouched. Guards against the tier-fallback being
    // too permissive.
    {
        writeFile(ws + "/diff_target.txt",
                  "line A\nline B\nline C\n");
        AiTools::ToolCall call;
        call.name = "apply_diff";
        QJsonObject args;
        args["path"] = "diff_target.txt";
        QJsonArray hunks;
        QJsonObject h1;
        h1["old_start_line"] = 2;
        // Genuinely different content — even after strip+trim, "totally
        // different" is not "line B".
        h1["old_lines"] = "totally different content\n";
        h1["new_lines"] = "should not appear\n";
        hunks.append(h1);
        args["hunks"] = hunks;
        call.args = args;
        AiTools::ToolResult r = AiTools::execute(call, ws);
        check("apply_diff: true conflict still refused (no false-positive tier match)",
              r.isError);
        check("  errorKind == conflict", r.errorKind == "conflict");
        QFile fread(ws + "/diff_target.txt");
        fread.open(QFile::ReadOnly);
        check("  file unchanged on real conflict",
              fread.readAll() == QByteArray("line A\nline B\nline C\n"));
        fread.close();
    }

    // ── v0.1.40: read_file with_line_numbers parameter ────────────────
    //
    // Default (no arg): line-number prefix included, back-compat with
    // v0.1.39 callers that depend on the cat-n style output.
    {
        writeFile(ws + "/with_lines.txt", "alpha\nbeta\ngamma\n");
        AiTools::ToolCall call;
        call.name = "read_file";
        QJsonObject args; args["path"] = "with_lines.txt";
        call.args = args;
        AiTools::ToolResult r = AiTools::execute(call, ws);
        check("read_file (default): success", !r.isError);
        QJsonDocument jd = QJsonDocument::fromJson(r.content.toUtf8());
        QString content = jd.object().value("result").toObject()
                            .value("content").toString();
        check("  default content has line-number prefix on first line",
              content.startsWith("     1\talpha"));
        check("  default content has line-number prefix on second line",
              content.contains("\n     2\tbeta"));
    }

    // with_line_numbers=true (explicit) → identical to default.
    {
        AiTools::ToolCall call;
        call.name = "read_file";
        QJsonObject args;
        args["path"] = "with_lines.txt";
        args["with_line_numbers"] = true;
        call.args = args;
        AiTools::ToolResult r = AiTools::execute(call, ws);
        check("read_file with_line_numbers=true: success", !r.isError);
        QJsonDocument jd = QJsonDocument::fromJson(r.content.toUtf8());
        QString content = jd.object().value("result").toObject()
                            .value("content").toString();
        check("  with_line_numbers=true matches default behaviour",
              content.startsWith("     1\talpha"));
    }

    // with_line_numbers=false → raw content, no prefix. This is the path
    // the model should use before feeding lines into apply_diff old_lines.
    {
        AiTools::ToolCall call;
        call.name = "read_file";
        QJsonObject args;
        args["path"] = "with_lines.txt";
        args["with_line_numbers"] = false;
        call.args = args;
        AiTools::ToolResult r = AiTools::execute(call, ws);
        check("read_file with_line_numbers=false: success", !r.isError);
        QJsonDocument jd = QJsonDocument::fromJson(r.content.toUtf8());
        QString content = jd.object().value("result").toObject()
                            .value("content").toString();
        check("  raw content (no prefix on first line)",
              content.startsWith("alpha"));
        check("  raw content equals exactly the file bytes",
              content == "alpha\nbeta\ngamma\n");
    }

    // read_file schema in availableTools() advertises with_line_numbers
    {
        QJsonArray tools = AiTools::availableTools();
        QJsonObject readSchema;
        for (const QJsonValue &tv : tools) {
            QJsonObject t = tv.toObject();
            if (t.value("function").toObject().value("name").toString()
                  == "read_file") {
                readSchema = t.value("function").toObject();
                break;
            }
        }
        check("read_file schema: located",
              !readSchema.isEmpty());
        QJsonObject props = readSchema.value("parameters").toObject()
                              .value("properties").toObject();
        check("read_file schema: properties contains with_line_numbers",
              props.contains("with_line_numbers"));
        check("read_file schema: with_line_numbers is boolean",
              props.value("with_line_numbers").toObject()
                   .value("type").toString() == "boolean");
    }

    // Tool registry — verify availableTools() returns valid JSONSchema
    {
        QJsonArray tools = AiTools::availableTools();
        check("availableTools() has 7 entries (read/list/write/search/apply_diff + csv_query/query_sql)",
              tools.size() == 7);
        QStringList names;
        for (const QJsonValue &tv : tools) {
            QJsonObject t = tv.toObject();
            check("  each tool has type=function",
                  t.value("type").toString() == "function");
            QJsonObject fn = t.value("function").toObject();
            const QString name = fn.value("name").toString();
            names << name;
            check("  each tool has function.name", !name.isEmpty());
            check("  each tool has function.description",
                  !fn.value("description").toString().isEmpty());
            check("  each tool has function.parameters object",
                  fn.value("parameters").isObject());
        }
        check("  registry has read_file",  names.contains("read_file"));
        check("  registry has list_dir",   names.contains("list_dir"));
        check("  registry has write_file", names.contains("write_file"));
        check("  registry has search",     names.contains("search"));
        check("  registry has apply_diff", names.contains("apply_diff"));
        check("  registry has csv_query",  names.contains("csv_query"));
        check("  registry has query_sql",  names.contains("query_sql"));
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

    // ── v0.1.56: dry_run on write_file / apply_diff (Composer mode) ──
    //
    // The dry_run path runs every safety check the real path runs, but
    // returns a {dry_run:true, proposed:{path,before,after,mode}} body
    // instead of touching the filesystem. Used by the Composer UI to
    // present a diff preview the user clicks Apply on.

    // write_file dry_run on a NEW path → ok, file NOT created on disk
    {
        AiTools::ToolCall call;
        call.name = "write_file";
        QJsonObject args;
        args["path"]    = "composer_proposal.txt";
        args["content"] = "hello composer\n";
        args["dry_run"] = true;
        call.args = args;
        AiTools::ToolResult r = AiTools::execute(call, ws);
        check("dry_run write_file: ok=true", !r.isError);
        QJsonObject result = QJsonDocument::fromJson(r.content.toUtf8())
                                .object().value("result").toObject();
        check("  result.dry_run == true", result.value("dry_run").toBool());
        QJsonObject proposed = result.value("proposed").toObject();
        check("  result.proposed has path/before/after/mode",
              proposed.contains("path") && proposed.contains("before")
              && proposed.contains("after") && proposed.contains("mode"));
        check("  proposed.after contains the new content",
              proposed.value("after").toString() == "hello composer\n");
        check("  proposed.before is empty (target didn't exist)",
              proposed.value("before").toString().isEmpty());
        // The file MUST NOT exist on disk after a dry run.
        check("  file on disk was NOT created",
              !QFileInfo(ws + "/composer_proposal.txt").exists());
    }

    // write_file dry_run on an EXISTING path captures the before/after
    {
        writeFile(ws + "/exists_for_dry.txt", "old version\n");
        AiTools::ToolCall call;
        call.name = "write_file";
        QJsonObject args;
        args["path"]    = "exists_for_dry.txt";
        args["content"] = "new version\n";
        args["dry_run"] = true;
        call.args = args;
        AiTools::ToolResult r = AiTools::execute(call, ws);
        check("dry_run write_file on existing: ok=true", !r.isError);
        QJsonObject result = QJsonDocument::fromJson(r.content.toUtf8())
                                .object().value("result").toObject();
        QJsonObject proposed = result.value("proposed").toObject();
        check("  proposed.before == old content",
              proposed.value("before").toString() == "old version\n");
        check("  proposed.after  == new content",
              proposed.value("after").toString() == "new version\n");
        QFile fread(ws + "/exists_for_dry.txt");
        fread.open(QFile::ReadOnly);
        check("  file on disk is UNCHANGED (still 'old version')",
              fread.readAll() == QByteArray("old version\n"));
        fread.close();
    }

    // write_file dry_run with mode=append composes after = before + content
    {
        writeFile(ws + "/append_for_dry.txt", "head\n");
        AiTools::ToolCall call;
        call.name = "write_file";
        QJsonObject args;
        args["path"]    = "append_for_dry.txt";
        args["content"] = "tail\n";
        args["mode"]    = "append";
        args["dry_run"] = true;
        call.args = args;
        AiTools::ToolResult r = AiTools::execute(call, ws);
        check("dry_run write_file mode=append: ok", !r.isError);
        QJsonObject result = QJsonDocument::fromJson(r.content.toUtf8())
                                .object().value("result").toObject();
        QJsonObject proposed = result.value("proposed").toObject();
        check("  proposed.after == before + content",
              proposed.value("after").toString() == "head\ntail\n");
        QFile fread(ws + "/append_for_dry.txt");
        fread.open(QFile::ReadOnly);
        check("  file on disk untouched after append dry_run",
              fread.readAll() == QByteArray("head\n"));
        fread.close();
    }

    // write_file dry_run with denied path → error_kind:denied, file
    // NOT created. Verifies safety checks run on the dry-run path.
    {
        AiTools::ToolCall call;
        call.name = "write_file";
        QJsonObject args;
        args["path"]    = "secret_dry.pem";
        args["content"] = "----BEGIN PRIVATE KEY----\n";
        args["dry_run"] = true;
        call.args = args;
        AiTools::ToolResult r = AiTools::execute(call, ws);
        check("dry_run write_file('secret_dry.pem'): refused", r.isError);
        check("  errorKind == denied", r.errorKind == "denied");
        check("  file on disk was NOT created",
              !QFileInfo(ws + "/secret_dry.pem").exists());
    }

    // write_file dry_run with traversal → refused, file NOT created.
    {
        AiTools::ToolCall call;
        call.name = "write_file";
        QJsonObject args;
        args["path"]    = "../escape_dry.txt";
        args["content"] = "x";
        args["dry_run"] = true;
        call.args = args;
        AiTools::ToolResult r = AiTools::execute(call, ws);
        check("dry_run write_file('../escape_dry.txt'): refused", r.isError);
        check("  errorKind is a refusal",
              r.errorKind == "outside_workspace"
              || r.errorKind == "denied"
              || r.errorKind == "not_found");
    }

    // write_file dry_run with mode=create on EXISTING file → exists
    // (same as the non-dry path). File untouched.
    {
        writeFile(ws + "/exists_create_dry.txt", "v1\n");
        AiTools::ToolCall call;
        call.name = "write_file";
        QJsonObject args;
        args["path"]    = "exists_create_dry.txt";
        args["content"] = "v2\n";
        args["mode"]    = "create";
        args["dry_run"] = true;
        call.args = args;
        AiTools::ToolResult r = AiTools::execute(call, ws);
        check("dry_run write_file mode=create on existing: errors", r.isError);
        check("  errorKind == exists", r.errorKind == "exists");
        QFile fread(ws + "/exists_create_dry.txt");
        fread.open(QFile::ReadOnly);
        check("  on-disk content unchanged",
              fread.readAll() == QByteArray("v1\n"));
        fread.close();
    }

    // apply_diff dry_run on a valid hunk → proposal, file unchanged
    {
        writeFile(ws + "/dry_diff.txt",
                  "line A\nline B\nline C\n");
        AiTools::ToolCall call;
        call.name = "apply_diff";
        QJsonObject args;
        args["path"]    = "dry_diff.txt";
        args["dry_run"] = true;
        QJsonArray hunks;
        QJsonObject h1;
        h1["old_start_line"] = 2;
        h1["old_lines"]      = "line B\n";
        h1["new_lines"]      = "line B (proposed)\n";
        hunks.append(h1);
        args["hunks"] = hunks;
        call.args = args;
        AiTools::ToolResult r = AiTools::execute(call, ws);
        check("dry_run apply_diff: ok=true", !r.isError);
        QJsonObject result = QJsonDocument::fromJson(r.content.toUtf8())
                                .object().value("result").toObject();
        check("  result.dry_run == true", result.value("dry_run").toBool());
        QJsonObject proposed = result.value("proposed").toObject();
        check("  proposed.before == original file",
              proposed.value("before").toString() == "line A\nline B\nline C\n");
        check("  proposed.after  reflects the hunk",
              proposed.value("after").toString() == "line A\nline B (proposed)\nline C\n");
        check("  proposed.mode == apply_diff",
              proposed.value("mode").toString() == "apply_diff");
        QFile fread(ws + "/dry_diff.txt");
        fread.open(QFile::ReadOnly);
        check("  file on disk UNCHANGED after dry_run",
              fread.readAll() == QByteArray("line A\nline B\nline C\n"));
        fread.close();
    }

    // apply_diff dry_run with conflict → error_kind:conflict, no proposal
    {
        writeFile(ws + "/dry_conflict.txt",
                  "line A\nline B\nline C\n");
        AiTools::ToolCall call;
        call.name = "apply_diff";
        QJsonObject args;
        args["path"]    = "dry_conflict.txt";
        args["dry_run"] = true;
        QJsonArray hunks;
        QJsonObject h1;
        h1["old_start_line"] = 2;
        h1["old_lines"]      = "WRONG content\n";  // doesn't match
        h1["new_lines"]      = "should never appear\n";
        hunks.append(h1);
        args["hunks"] = hunks;
        call.args = args;
        AiTools::ToolResult r = AiTools::execute(call, ws);
        check("dry_run apply_diff conflict: errors", r.isError);
        check("  errorKind == conflict", r.errorKind == "conflict");
        QFile fread(ws + "/dry_conflict.txt");
        fread.open(QFile::ReadOnly);
        check("  file unchanged on dry_run conflict",
              fread.readAll() == QByteArray("line A\nline B\nline C\n"));
        fread.close();
    }

    // apply_diff dry_run on deny-listed path → denied, no proposal
    {
        writeFile(ws + "/secret_diff.pem", "----BEGIN PRIVATE KEY----\n");
        AiTools::ToolCall call;
        call.name = "apply_diff";
        QJsonObject args;
        args["path"]    = "secret_diff.pem";
        args["dry_run"] = true;
        QJsonArray hunks;
        QJsonObject h1; h1["old_start_line"] = 1;
        h1["old_lines"] = "----BEGIN PRIVATE KEY----\n";
        h1["new_lines"] = "tampered\n";
        hunks.append(h1);
        args["hunks"] = hunks;
        call.args = args;
        AiTools::ToolResult r = AiTools::execute(call, ws);
        check("dry_run apply_diff on deny-listed path: refused", r.isError);
        check("  errorKind == denied", r.errorKind == "denied");
    }

    // Default (no dry_run arg) STILL writes — backwards-compat guard.
    {
        AiTools::ToolCall call;
        call.name = "write_file";
        QJsonObject args;
        args["path"]    = "default_writes.txt";
        args["content"] = "real write\n";
        // No dry_run field at all.
        call.args = args;
        AiTools::ToolResult r = AiTools::execute(call, ws);
        check("default write_file (no dry_run arg): writes for real", !r.isError);
        check("  file exists on disk",
              QFileInfo(ws + "/default_writes.txt").exists());
    }

    // dry_run:false explicitly → still writes (proves the flag default
    // path is unbroken).
    {
        AiTools::ToolCall call;
        call.name = "write_file";
        QJsonObject args;
        args["path"]    = "explicit_false.txt";
        args["content"] = "yes write\n";
        args["dry_run"] = false;
        call.args = args;
        AiTools::ToolResult r = AiTools::execute(call, ws);
        check("write_file dry_run=false: writes for real", !r.isError);
        check("  file exists on disk",
              QFileInfo(ws + "/explicit_false.txt").exists());
    }

    // Schemas advertise dry_run on write_file + apply_diff
    {
        QJsonArray tools = AiTools::availableTools();
        QJsonObject writeProps, applyProps;
        for (const QJsonValue &tv : tools) {
            QJsonObject t = tv.toObject();
            QJsonObject fn = t.value("function").toObject();
            const QString name = fn.value("name").toString();
            if (name == "write_file") {
                writeProps = fn.value("parameters").toObject()
                               .value("properties").toObject();
            } else if (name == "apply_diff") {
                applyProps = fn.value("parameters").toObject()
                               .value("properties").toObject();
            }
        }
        check("write_file schema declares dry_run",
              writeProps.contains("dry_run"));
        check("  write_file dry_run is boolean",
              writeProps.value("dry_run").toObject()
                        .value("type").toString() == "boolean");
        check("apply_diff schema declares dry_run",
              applyProps.contains("dry_run"));
        check("  apply_diff dry_run is boolean",
              applyProps.value("dry_run").toObject()
                        .value("type").toString() == "boolean");
    }

    fprintf(stdout, "\n=== test_ai_tools: %d passed, %d failed ===\n", g_pass, g_fail);
    return g_fail == 0 ? 0 : 1;
}
