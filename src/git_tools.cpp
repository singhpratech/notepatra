// SPDX-License-Identifier: GPL-3.0-or-later

#include "git_tools.h"

#include <QByteArray>
#include <QDir>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QString>
#include <QStringList>

#include <cctype>

namespace GitTools {

// ═══════════════════════════════════════════════════════════════════════
// Limits + constants
// ═══════════════════════════════════════════════════════════════════════
namespace Limits {
    // 32 KB cap on diff / show payloads. Larger diffs would burn the
    // model's context window and the agent loop's chat history. Past the
    // cap we truncate with a marker the model can recognise.
    constexpr int kDiffMaxBytes  = 32 * 1024;
    constexpr int kShowMaxBytes  = 32 * 1024;

    // 5-second timeout per git invocation. Matches the runGitSync default
    // in gitpanel.cpp on file-staging commands; reads of metadata typically
    // finish in well under 100 ms even on big repos.
    constexpr int kGitTimeoutMs  = 5000;

    // git_log defaults / clamps.
    constexpr int kLogDefaultMax = 20;
    constexpr int kLogHardCap    = 100;
}

// ═══════════════════════════════════════════════════════════════════════
// Result shaping helpers — duplicated from ai_tools.cpp's anonymous
// namespace so we don't need to expose them in the public header just
// for this module. Same wire shape, identical JSON keys.
// ═══════════════════════════════════════════════════════════════════════
namespace {

AiTools::ToolResult makeError(const AiTools::ToolCall &call,
                              const QString &errorKind,
                              const QString &humanMessage) {
    AiTools::ToolResult r;
    r.id = call.id;
    r.name = call.name;
    r.isError = true;
    r.errorKind = errorKind;
    QJsonObject body;
    body["ok"] = false;
    body["error_kind"] = errorKind;
    body["message"] = humanMessage;
    if (call.args.contains("path")) {
        body["path"] = call.args.value("path").toString();
    }
    r.content = QString::fromUtf8(QJsonDocument(body).toJson(QJsonDocument::Compact));
    return r;
}

AiTools::ToolResult makeSuccess(const AiTools::ToolCall &call,
                                const QJsonObject &result) {
    AiTools::ToolResult r;
    r.id = call.id;
    r.name = call.name;
    r.isError = false;
    QJsonObject body;
    body["ok"] = true;
    body["result"] = result;
    r.content = QString::fromUtf8(QJsonDocument(body).toJson(QJsonDocument::Compact));
    return r;
}

// ── Process runner ────────────────────────────────────────────────────
// Tiny, deliberate wrapper around QProcess. The `argv` list is built by
// the caller from a literal verb (status/diff/log/rev-parse/branch/show)
// so there is no path for the model to inject another verb — even a
// crafted `path` argument lands in argv as a single element git will
// treat as a path.
//
// Returns:
//   exitOk = true   → exit code 0 in the timeout window
//   exitOk = false  → either timed out, failed to start, or non-zero exit
// The caller distinguishes via `timedOut` and `started`.
struct GitRunResult {
    bool started   = false;
    bool timedOut  = false;
    bool exitOk    = false;
    int  exitCode  = -1;
    QByteArray stdoutBytes;
    QByteArray stderrBytes;
};

GitRunResult runGit(const QStringList &argv, const QString &workspaceRoot) {
    GitRunResult res;
    QProcess proc;
    proc.setWorkingDirectory(workspaceRoot);
    proc.start("git", argv);
    if (!proc.waitForStarted(2000)) {
        res.started = false;
        res.stderrBytes = "git executable could not be started";
        return res;
    }
    res.started = true;
    if (!proc.waitForFinished(Limits::kGitTimeoutMs)) {
        proc.kill();
        proc.waitForFinished(500);
        res.timedOut = true;
        res.stdoutBytes = proc.readAllStandardOutput();
        res.stderrBytes = proc.readAllStandardError();
        return res;
    }
    res.stdoutBytes = proc.readAllStandardOutput();
    res.stderrBytes = proc.readAllStandardError();
    res.exitCode = proc.exitCode();
    res.exitOk = (proc.exitStatus() == QProcess::NormalExit && res.exitCode == 0);
    return res;
}

// Validate workspace + verify it's a git repo. On failure returns a
// fully-formed ToolResult error; on success returns an empty result
// (caller should check `outOk`).
AiTools::ToolResult preflight(const AiTools::ToolCall &call,
                              const QString &workspaceRoot,
                              bool *outOk) {
    *outOk = false;
    if (workspaceRoot.trimmed().isEmpty()) {
        return makeError(call, "no_workspace",
                         "No workspace is open. Open a folder first.");
    }
    QFileInfo wsFi(workspaceRoot);
    if (!wsFi.exists() || !wsFi.isDir()) {
        return makeError(call, "no_workspace",
                         "Workspace path does not exist or is not a directory.");
    }
    GitRunResult rv = runGit({"rev-parse", "--git-dir"}, workspaceRoot);
    if (!rv.started) {
        return makeError(call, "git_error",
                         "git executable not found on PATH.");
    }
    if (rv.timedOut) {
        return makeError(call, "timeout",
                         "git rev-parse timed out after 5 seconds.");
    }
    if (!rv.exitOk) {
        return makeError(call, "not_a_repo",
                         "This workspace is not a git repository.");
    }
    *outOk = true;
    return AiTools::ToolResult{}; // unused on success
}

// Bytes → JSON string with truncation marker if oversized.
QJsonObject capDiffPayload(const QByteArray &raw, int capBytes) {
    QJsonObject out;
    if (raw.size() <= capBytes) {
        out["text"] = QString::fromUtf8(raw);
        out["truncated"] = false;
        out["size_bytes"] = raw.size();
        return out;
    }
    QByteArray clipped = raw.left(capBytes);
    QString text = QString::fromUtf8(clipped);
    text += QString("\n[... truncated — diff is %1 bytes; first %2 KB shown ...]")
                .arg(raw.size()).arg(capBytes / 1024);
    out["text"] = text;
    out["truncated"] = true;
    out["size_bytes"] = raw.size();
    return out;
}

// Diff flags for every patch-printing call: no colour (keeps headers parseable),
// fixed a/ b/ prefixes, and no external diff / textconv command from repo config.
const QStringList kSafeDiffFlags = {"--no-color", "--no-ext-diff", "--no-textconv",
                                    "--src-prefix=a/", "--dst-prefix=b/"};

// A repo-relative path is secret when the shared deny-list matches it.
bool isSecretRepoPath(const QString &rel) {
    if (rel.trimmed().isEmpty()) return false;
    return AiTools::isHardDenied(QLatin1Char('/') + rel)
        || AiTools::isHardDenied(QLatin1Char('/') + QDir::cleanPath(rel));
}

// Undo git's C-style quoting ("a/\303\251 x.env") of unusual path names.
QByteArray unquoteGitPath(const QByteArray &s) {
    if (s.size() < 2 || !s.startsWith('"')) return s;
    QByteArray out;
    for (int i = 1; i < s.size(); ++i) {
        char c = s[i];
        if (c == '"') break;
        if (c != '\\' || i + 1 >= s.size()) { out += c; continue; }
        c = s[++i];
        if (c >= '0' && c <= '7') {
            int v = 0, k = 0;
            for (; k < 3 && i < s.size() && s[i] >= '0' && s[i] <= '7'; ++k, ++i)
                v = v * 8 + (s[i] - '0');
            --i;
            out += char(v);
            continue;
        }
        switch (c) {
        case 'a': out += '\a'; break;
        case 'b': out += '\b'; break;
        case 'f': out += '\f'; break;
        case 'n': out += '\n'; break;
        case 'r': out += '\r'; break;
        case 't': out += '\t'; break;
        case 'v': out += '\v'; break;
        default:  out += c; break;
        }
    }
    return out;
}

bool isFileDiffHeader(const QByteArray &l) {
    return l.startsWith("diff --git ") || l.startsWith("diff --cc ")
        || l.startsWith("diff --combined ");
}

bool isCommitHeader(const QByteArray &l) {
    if (!l.startsWith("commit ") || l.size() < 14) return false;
    for (int i = 7; i < 14; ++i)
        if (!isxdigit(static_cast<unsigned char>(l[i]))) return false;
    return true;
}

// Every path a file block's header names: both sides of `diff --git`, every
// ambiguous split of an unquoted header, ---/+++ and rename/copy lines.
QStringList blockHeaderPaths(const QList<QByteArray> &lines, int begin, int end) {
    QList<QByteArray> raw;
    const QByteArray &first = lines[begin];
    const int sp = first.indexOf(' ', first.indexOf("--") + 2);
    const QByteArray rest = sp >= 0 ? first.mid(sp + 1) : QByteArray();
    raw << rest;
    for (int i = 0; i < rest.size(); ++i)
        if (rest[i] == ' ' && (rest.mid(i + 1).startsWith("b/")
                               || rest.mid(i + 1).startsWith("\"b/")))
            raw << rest.left(i) << rest.mid(i + 1);
    static const char *kPrefixes[] = {"--- ", "+++ ", "rename from ", "rename to ",
                                      "copy from ", "copy to "};
    for (int i = begin + 1; i < end; ++i) {
        const QByteArray &l = lines[i];
        if (l.startsWith("@@") || l.startsWith("Binary files")
            || l.startsWith("GIT binary patch")) break;
        for (const char *p : kPrefixes)
            if (l.startsWith(p)) {
                QByteArray v = l.mid(int(qstrlen(p)));
                if (v.endsWith('\t')) v.chop(1);
                raw << v;
            }
    }
    QStringList out;
    for (const QByteArray &r : raw) {
        out << QString::fromUtf8(r);
        if (r.startsWith('"')) out << QString::fromUtf8(unquoteGitPath(r));
    }
    return out;
}

// Replace each deny-listed file's patch with a one-line placeholder. Checks
// old AND new names, so a rename to or from a secret name is redacted too.
QByteArray redactSecretDiffs(const QByteArray &raw) {
    const QList<QByteArray> lines = raw.split('\n');
    QList<QByteArray> out;
    int i = 0;
    while (i < lines.size()) {
        if (!isFileDiffHeader(lines[i])) { out << lines[i++]; continue; }
        int j = i + 1;
        while (j < lines.size() && !isFileDiffHeader(lines[j]) && !isCommitHeader(lines[j]))
            ++j;
        bool secret = false;
        for (const QString &p : blockHeaderPaths(lines, i, j))
            if (isSecretRepoPath(p)) { secret = true; break; }
        if (secret) {
            out << lines[i] << QByteArray("[redacted: secret-pattern path]");
            if (j == lines.size() && lines.last().isEmpty()) out << QByteArray();
        } else {
            for (int k = i; k < j; ++k) out << lines[k];
        }
        i = j;
    }
    return out.join('\n');
}

} // anonymous namespace

// ═══════════════════════════════════════════════════════════════════════
// Parser — porcelain v2. The shape is documented at
// https://git-scm.com/docs/git-status#_porcelain_format_version_2
// We re-implement here (rather than reusing GitPanel's) because that
// parser writes into widget-state structures; we want plain JSON.
// ═══════════════════════════════════════════════════════════════════════
QJsonObject parsePorcelainV2(const QByteArray &out) {
    QJsonObject result;
    QJsonArray staged, modified, untracked, conflicts;

    QString branch;
    QString upstream;
    int ahead = 0;
    int behind = 0;

    QList<QByteArray> recs = out.split('\0');

    // Rename "2 " records carry the original path in the *next* NUL-
    // delimited fragment. Merge them so the loop below sees one record.
    QVector<QByteArray> merged;
    merged.reserve(recs.size());
    for (int i = 0; i < recs.size(); ++i) {
        const QByteArray &r = recs[i];
        if (r.isEmpty()) continue;
        if (r.startsWith("2 ") && i + 1 < recs.size()) {
            QByteArray combined = r;
            combined.append('\0');
            combined.append(recs[i + 1]);
            merged.append(combined);
            ++i;
        } else {
            merged.append(r);
        }
    }

    auto pushPath = [](QJsonArray &arr, const QString &path) {
        arr.push_back(path);
    };

    for (const QByteArray &r : merged) {
        if (r.isEmpty()) continue;
        const char c = r[0];
        switch (c) {
        case '#': {
            const QString line = QString::fromUtf8(r);
            if (line.startsWith("# branch.head ")) {
                const QString head = line.mid(QByteArray("# branch.head ").size()).trimmed();
                branch = (head == "(detached)") ? QStringLiteral("(detached HEAD)") : head;
            } else if (line.startsWith("# branch.upstream ")) {
                upstream = line.mid(QByteArray("# branch.upstream ").size()).trimmed();
            } else if (line.startsWith("# branch.ab ")) {
                const QString rest = line.mid(QByteArray("# branch.ab ").size()).trimmed();
                const QStringList parts = rest.split(' ', Qt::SkipEmptyParts);
                for (const QString &p : parts) {
                    if (p.startsWith('+')) ahead = p.mid(1).toInt();
                    else if (p.startsWith('-')) behind = p.mid(1).toInt();
                }
            }
            break;
        }
        case '1': {
            // "1 XY sub mH mI mW hH hI path" — XY is staged/work statuses.
            const int firstSpace = r.indexOf(' ');
            if (firstSpace < 0) break;
            const QByteArray xy = r.mid(firstSpace + 1, 2);
            int pathStart = 0;
            int spaces = 0;
            for (int i = 0; i < r.size(); ++i) {
                if (r[i] == ' ') { ++spaces; if (spaces == 8) { pathStart = i + 1; break; } }
            }
            if (pathStart <= 0 || pathStart >= r.size()) break;
            const QString path = QString::fromUtf8(r.mid(pathStart));
            const char idx = xy.size() > 0 ? xy[0] : ' ';
            const char wrk = xy.size() > 1 ? xy[1] : ' ';
            if (idx != '.' && idx != ' ') pushPath(staged, path);
            if (wrk != '.' && wrk != ' ') pushPath(modified, path);
            break;
        }
        case '2': {
            // Renames: head + NUL + origPath. We surface the new path.
            const int nulIdx = r.indexOf('\0');
            if (nulIdx < 0) break;
            const QByteArray head = r.left(nulIdx);
            const int firstSpace = head.indexOf(' ');
            if (firstSpace < 0) break;
            const QByteArray xy = head.mid(firstSpace + 1, 2);
            int pathStart = 0;
            int spaces = 0;
            for (int i = 0; i < head.size(); ++i) {
                if (head[i] == ' ') { ++spaces; if (spaces == 9) { pathStart = i + 1; break; } }
            }
            if (pathStart <= 0 || pathStart >= head.size()) break;
            const QString newPath = QString::fromUtf8(head.mid(pathStart));
            const char idx = xy.size() > 0 ? xy[0] : ' ';
            const char wrk = xy.size() > 1 ? xy[1] : ' ';
            if (idx != '.' && idx != ' ') pushPath(staged, newPath);
            if (wrk != '.' && wrk != ' ') pushPath(modified, newPath);
            break;
        }
        case 'u': {
            // Unmerged / conflict.
            int pathStart = 0;
            int spaces = 0;
            for (int i = 0; i < r.size(); ++i) {
                if (r[i] == ' ') { ++spaces; if (spaces == 10) { pathStart = i + 1; break; } }
            }
            if (pathStart <= 0 || pathStart >= r.size()) break;
            const QString path = QString::fromUtf8(r.mid(pathStart));
            pushPath(conflicts, path);
            break;
        }
        case '?': {
            const QString path = QString::fromUtf8(r.mid(2));
            pushPath(untracked, path);
            break;
        }
        default: break;
        }
    }

    result["branch"]    = branch;
    result["upstream"]  = upstream;
    result["ahead"]     = ahead;
    result["behind"]    = behind;
    result["staged"]    = staged;
    result["modified"]  = modified;
    result["untracked"] = untracked;
    result["conflicts"] = conflicts;
    result["clean"]     = staged.isEmpty() && modified.isEmpty()
                          && untracked.isEmpty() && conflicts.isEmpty();
    return result;
}

// Parse log records. Format we run is
// `--pretty=format:%H%x1f%an%x1f%aI%x1f%s%x1e` so each record is
// %H (full hash) %x1f %an %x1f %aI %x1f %s, terminated by %x1e.
QJsonArray parseLogRecords(const QByteArray &out) {
    QJsonArray arr;
    if (out.isEmpty()) return arr;

    // Records separated by 0x1e; fields by 0x1f.
    const QList<QByteArray> records = out.split('\x1e');
    for (const QByteArray &rec : records) {
        if (rec.trimmed().isEmpty()) continue;
        // git inserts a newline after the record-separator on every entry
        // except the last; trim leading whitespace per record to compensate.
        QByteArray trimmed = rec;
        while (!trimmed.isEmpty() && (trimmed[0] == '\n' || trimmed[0] == '\r')) {
            trimmed = trimmed.mid(1);
        }
        const QList<QByteArray> fields = trimmed.split('\x1f');
        if (fields.size() < 4) continue;
        QJsonObject entry;
        entry["hash"]    = QString::fromUtf8(fields[0]);
        entry["author"]  = QString::fromUtf8(fields[1]);
        entry["date"]    = QString::fromUtf8(fields[2]);
        entry["subject"] = QString::fromUtf8(fields[3]);
        arr.push_back(entry);
    }
    return arr;
}

// ═══════════════════════════════════════════════════════════════════════
// Tool implementations
// ═══════════════════════════════════════════════════════════════════════

AiTools::ToolResult executeGitStatus(const AiTools::ToolCall &call,
                                     const QString &workspaceRoot) {
    bool ok = false;
    AiTools::ToolResult pre = preflight(call, workspaceRoot, &ok);
    if (!ok) return pre;

    GitRunResult rv = runGit(
        {"status", "--porcelain=v2", "--branch", "-z"},
        workspaceRoot);
    if (rv.timedOut) {
        return makeError(call, "timeout",
                         "git status timed out after 5 seconds.");
    }
    if (!rv.exitOk) {
        return makeError(call, "git_error",
                         QString::fromUtf8(rv.stderrBytes).trimmed());
    }
    QJsonObject result = parsePorcelainV2(rv.stdoutBytes);
    return makeSuccess(call, result);
}

AiTools::ToolResult executeGitDiff(const AiTools::ToolCall &call,
                                   const QString &workspaceRoot) {
    bool ok = false;
    AiTools::ToolResult pre = preflight(call, workspaceRoot, &ok);
    if (!ok) return pre;

    const bool staged = call.args.value("staged").toBool(false);
    const QString pathArg = call.args.value("path").toString();

    QStringList argv;
    argv << "diff" << kSafeDiffFlags;
    if (staged) argv << "--cached";

    // If a path was supplied, validate it sits inside the workspace and
    // pass it to git as a relative path. We allow non-existent paths
    // (they may be deleted in the diff), so use a permissive validation
    // path here that still anchors at workspace root.
    if (!pathArg.trimmed().isEmpty()) {
        // Reject directly-traversal-y paths up-front. The full canonical
        // check requires the file to exist; for diffs of deleted files we
        // fall back to a syntactic guard.
        if (pathArg.contains("..")) {
            return makeError(call, "outside_workspace",
                             "Path contains '..' traversal — not allowed.");
        }
        if (QDir::isAbsolutePath(pathArg)) {
            QString canonical;
            QString errorKind;
            if (!AiTools::resolveSafePath(pathArg, workspaceRoot,
                                          &canonical, &errorKind)) {
                return makeError(call, errorKind.isEmpty() ? "outside_workspace"
                                                           : errorKind,
                                 "Path resolves outside the workspace.");
            }
            // Pass relative to working dir.
            argv << "--" << QDir(workspaceRoot).relativeFilePath(canonical);
        } else {
            const QString real = QFileInfo(QDir(workspaceRoot).absoluteFilePath(pathArg))
                                     .canonicalFilePath();
            if (isSecretRepoPath(pathArg) || (!real.isEmpty() && AiTools::isHardDenied(real))) {
                return makeError(call, "denied",
                                 "Access to this path is restricted (credential deny-list).");
            }
            argv << "--" << pathArg;
        }
    }

    GitRunResult rv = runGit(argv, workspaceRoot);
    if (rv.timedOut) {
        return makeError(call, "timeout",
                         "git diff timed out after 5 seconds.");
    }
    if (!rv.exitOk) {
        return makeError(call, "git_error",
                         QString::fromUtf8(rv.stderrBytes).trimmed());
    }

    QJsonObject result = capDiffPayload(redactSecretDiffs(rv.stdoutBytes), Limits::kDiffMaxBytes);
    result["staged"] = staged;
    if (!pathArg.trimmed().isEmpty()) result["path"] = pathArg;
    return makeSuccess(call, result);
}

AiTools::ToolResult executeGitLog(const AiTools::ToolCall &call,
                                  const QString &workspaceRoot) {
    bool ok = false;
    AiTools::ToolResult pre = preflight(call, workspaceRoot, &ok);
    if (!ok) return pre;

    int maxCount = call.args.value("max_count").toInt(Limits::kLogDefaultMax);
    if (maxCount < 1) maxCount = Limits::kLogDefaultMax;
    if (maxCount > Limits::kLogHardCap) maxCount = Limits::kLogHardCap;

    const QString pathArg = call.args.value("path").toString();

    QStringList argv;
    argv << "log"
         << QString("-n%1").arg(maxCount)
         << "--pretty=format:%H%x1f%an%x1f%aI%x1f%s%x1e";

    if (!pathArg.trimmed().isEmpty()) {
        if (pathArg.contains("..")) {
            return makeError(call, "outside_workspace",
                             "Path contains '..' traversal — not allowed.");
        }
        argv << "--";
        if (QDir::isAbsolutePath(pathArg)) {
            QString canonical;
            QString errorKind;
            if (!AiTools::resolveSafePath(pathArg, workspaceRoot,
                                          &canonical, &errorKind)) {
                return makeError(call, errorKind.isEmpty() ? "outside_workspace"
                                                           : errorKind,
                                 "Path resolves outside the workspace.");
            }
            argv << QDir(workspaceRoot).relativeFilePath(canonical);
        } else {
            argv << pathArg;
        }
    }

    GitRunResult rv = runGit(argv, workspaceRoot);
    if (rv.timedOut) {
        return makeError(call, "timeout",
                         "git log timed out after 5 seconds.");
    }
    if (!rv.exitOk) {
        return makeError(call, "git_error",
                         QString::fromUtf8(rv.stderrBytes).trimmed());
    }

    QJsonArray commits = parseLogRecords(rv.stdoutBytes);
    QJsonObject result;
    result["commits"]    = commits;
    result["count"]      = commits.size();
    result["max_count"]  = maxCount;
    if (!pathArg.trimmed().isEmpty()) result["path"] = pathArg;
    return makeSuccess(call, result);
}

AiTools::ToolResult executeGitBranchList(const AiTools::ToolCall &call,
                                         const QString &workspaceRoot) {
    bool ok = false;
    AiTools::ToolResult pre = preflight(call, workspaceRoot, &ok);
    if (!ok) return pre;

    // Format: <name>\x1f<upstream>\x1f<is_current>
    GitRunResult rv = runGit(
        {"branch", "--list",
         "--format=%(refname:short)%1f%(upstream:short)%1f%(HEAD)"},
        workspaceRoot);
    if (rv.timedOut) {
        return makeError(call, "timeout",
                         "git branch timed out after 5 seconds.");
    }
    if (!rv.exitOk) {
        return makeError(call, "git_error",
                         QString::fromUtf8(rv.stderrBytes).trimmed());
    }

    QJsonArray branches;
    QString currentBranch;
    const QList<QByteArray> lines = rv.stdoutBytes.split('\n');
    for (const QByteArray &line : lines) {
        if (line.trimmed().isEmpty()) continue;
        const QList<QByteArray> fields = line.split('\x1f');
        if (fields.isEmpty()) continue;
        QJsonObject entry;
        const QString name = QString::fromUtf8(fields.value(0)).trimmed();
        if (name.isEmpty()) continue;
        entry["name"]     = name;
        entry["upstream"] = QString::fromUtf8(fields.value(1)).trimmed();
        // "%(HEAD)" expands to "*" for the current branch, " " otherwise.
        const QString head = QString::fromUtf8(fields.value(2)).trimmed();
        const bool isCurrent = (head == "*");
        entry["current"] = isCurrent;
        if (isCurrent) currentBranch = name;
        branches.push_back(entry);
    }

    QJsonObject result;
    result["current"]  = currentBranch;
    result["branches"] = branches;
    result["count"]    = branches.size();
    return makeSuccess(call, result);
}

AiTools::ToolResult executeGitShow(const AiTools::ToolCall &call,
                                   const QString &workspaceRoot) {
    bool ok = false;
    AiTools::ToolResult pre = preflight(call, workspaceRoot, &ok);
    if (!ok) return pre;

    QString commit = call.args.value("commit").toString().trimmed();
    if (commit.isEmpty()) {
        return makeError(call, "io_error",
                         "git_show requires a non-empty 'commit' argument.");
    }
    // Defense-in-depth: refuse anything that looks like an option flag or
    // shell metacharacter so the model can't smuggle a `--upload-pack=...`
    // through. Real refs / shas / HEAD~N never start with '-' or contain
    // ; & | $ ` etc.
    if (commit.startsWith('-') ||
        commit.contains(';') || commit.contains('&') ||
        commit.contains('|') || commit.contains('`') ||
        commit.contains('$') || commit.contains(' ')) {
        return makeError(call, "io_error",
                         "Invalid commit reference.");
    }

    // Resolve to one object id first, so what git prints is what was checked.
    auto gitLine = [&](const QStringList &args, QString *outLine) -> AiTools::ToolResult {
        GitRunResult r = runGit(args, workspaceRoot);
        if (r.timedOut)
            return makeError(call, "timeout", "git show timed out after 5 seconds.");
        if (!r.exitOk) {
            const QString e = QString::fromUtf8(r.stderrBytes).trimmed();
            return makeError(call, "git_error",
                             e.isEmpty() ? QStringLiteral("Unknown revision: %1").arg(commit) : e);
        }
        *outLine = QString::fromUtf8(r.stdoutBytes).trimmed();
        return AiTools::ToolResult{};
    };
    QString oid, type;
    AiTools::ToolResult err = gitLine({"rev-parse", "--verify", "--quiet", commit}, &oid);
    if (err.isError) return err;
    err = gitLine({"cat-file", "-t", oid}, &type);
    if (err.isError) return err;
    if (type == QLatin1String("tag")) {
        QString peeled;
        err = gitLine({"rev-parse", "--verify", "--quiet", oid + "^{}"}, &peeled);
        if (err.isError) return err;
        err = gitLine({"cat-file", "-t", peeled}, &type);
        if (err.isError) return err;
    }
    if (type == QLatin1String("blob")) {
        // File contents only as <rev>:<path>, and never for a deny-listed path.
        if (!commit.contains(':')) {
            return makeError(call, "denied",
                             "git_show shows file contents only as <rev>:<path>.");
        }
        for (int i = commit.indexOf(':'); i >= 0; i = commit.indexOf(':', i + 1)) {
            if (isSecretRepoPath(commit.mid(i + 1))) {
                return makeError(call, "denied",
                                 "Access to this path is restricted (credential deny-list).");
            }
        }
    }

    GitRunResult rv = runGit(QStringList{"show"} << kSafeDiffFlags << oid << "--",
                             workspaceRoot);
    if (rv.timedOut) {
        return makeError(call, "timeout",
                         "git show timed out after 5 seconds.");
    }
    if (!rv.exitOk) {
        return makeError(call, "git_error",
                         QString::fromUtf8(rv.stderrBytes).trimmed());
    }

    QJsonObject result = capDiffPayload(redactSecretDiffs(rv.stdoutBytes), Limits::kShowMaxBytes);
    result["commit"] = commit;
    return makeSuccess(call, result);
}

} // namespace GitTools
