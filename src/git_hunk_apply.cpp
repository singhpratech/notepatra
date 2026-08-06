// SPDX-License-Identifier: GPL-3.0-or-later

#include "git_hunk_apply.h"

#include "path_denylist.h"

#include <QDir>
#include <QFileInfo>
#include <QProcess>
#include <QStringList>

namespace GitHunkApply {

namespace {

// Hard cap. A 5000-line hunk is a rename or a regenerated build artifact;
// neither belongs in a per-hunk stage. The AI agent and the popup both
// should refuse and tell the user to use the GitPanel's "stage whole
// file" path.
constexpr int kMaxPatchBodyLines = 5000;

// v0.1.62 — inline path-safety check rather than depending on
// AiTools::resolveSafePath (which pulls QSql / dbconnections via ai_tools.cpp).
// Mirrors the read-shape contract: file must exist AND be inside repoRoot
// (after canonicalising both sides to defeat ../ traversal + symlinks).
// Returns true and writes canonical absolute path on success; false on
// any anchor / existence / symlink-escape failure with errKind set.
bool inlineResolveReadPath(const QString &absFilePath,
                           const QString &repoRoot,
                           QString *outCanonical,
                           QString *outErrorKind) {
    if (absFilePath.isEmpty() || repoRoot.isEmpty()) {
        if (outErrorKind) *outErrorKind = "io_error";
        return false;
    }
    QFileInfo fi(absFilePath);
    if (!fi.exists()) {
        if (outErrorKind) *outErrorKind = "io_error";
        return false;
    }
    QString fileCanonical = fi.canonicalFilePath();
    QString rootCanonical = QFileInfo(repoRoot).canonicalFilePath();
    if (fileCanonical.isEmpty() || rootCanonical.isEmpty()) {
        if (outErrorKind) *outErrorKind = "io_error";
        return false;
    }
    QString rootPrefix = rootCanonical;
    if (!rootPrefix.endsWith('/')) rootPrefix += '/';
    if (fileCanonical != rootCanonical && !fileCanonical.startsWith(rootPrefix)) {
        if (outErrorKind) *outErrorKind = "outside_workspace";
        return false;
    }
    // Hardcoded deny-list — never stage hunks against credentials / keys.
    // Shared with ai_tools.cpp and search_project via PathDenylist; this used
    // to be a hand-maintained MIRROR of the ai_tools list and had already
    // drifted from it in both directions.
    if (PathDenylist::isSecretPath(fileCanonical)) {
        if (outErrorKind) *outErrorKind = "denied";
        return false;
    }
    if (outCanonical) *outCanonical = fileCanonical;
    return true;
}

// Normalise a single line to LF — strip any trailing \r before we splice
// it into the patch. We do NOT touch internal carriage returns because
// those would actually be content the user typed (and `git apply` deals
// fine with embedded \r mid-line).
inline QString lfNormalise(const QString &line) {
    if (line.endsWith('\r')) return line.left(line.size() - 1);
    return line;
}

} // namespace

QString repoRootForFile(const QString &absFilePath) {
    if (absFilePath.isEmpty()) return QString();
    QFileInfo fi(absFilePath);
    if (!fi.exists()) return QString();
    QProcess proc;
    proc.setWorkingDirectory(fi.path());
    proc.start("git", {"rev-parse", "--show-toplevel"});
    if (!proc.waitForFinished(3000)) {
        proc.kill();
        proc.waitForFinished(500);
        return QString();
    }
    if (proc.exitCode() != 0) return QString();
    QString out = QString::fromUtf8(proc.readAllStandardOutput()).trimmed();
    // Canonicalise so it matches what resolveSafePath() will see.
    QFileInfo rfi(out);
    QString canonical = rfi.canonicalFilePath();
    return canonical.isEmpty() ? out : canonical;
}

QString synthesizePatch(const QString &absFilePath,
                        const QString &repoRoot,
                        const QVector<DiffHunk> &hunks,
                        QString *outErrorKind) {
    auto setErr = [&](const char *k) {
        if (outErrorKind) *outErrorKind = QString::fromLatin1(k);
    };

    if (hunks.isEmpty()) { setErr("io_error"); return QString(); }

    // Workspace-anchor + deny-list check (inline above) — see comment on
    // inlineResolveReadPath for why we don't lean on AiTools::resolveSafePath
    // here (it would pull in ai_tools.cpp, which drags QSql / dbconnections).
    QString canonical;
    QString errKind;
    if (!inlineResolveReadPath(absFilePath, repoRoot, &canonical, &errKind)) {
        setErr(errKind.isEmpty() ? "outside_workspace" : errKind.toUtf8().constData());
        return QString();
    }

    const QString relPath = QDir(repoRoot).relativeFilePath(canonical);

    // Count body lines for the cap. Sum across all hunks plus 4 header
    // lines (`diff --git`, `--- a/...`, `+++ b/...`, `@@ ... @@`) per
    // hunk, but we only cap the BODY because that's the part that can
    // actually be unbounded.
    int totalBody = 0;
    for (const DiffHunk &h : hunks) totalBody += h.rawDiffLines.size();
    if (totalBody > kMaxPatchBodyLines) { setErr("too_large"); return QString(); }

    QStringList out;
    // Outer file header — present once per file in a unified diff.
    // Note: `git apply` doesn't require the `index <sha>..<sha>` line;
    // omitting it sidesteps having to look up the blob SHA via
    // `git ls-files -s` / `git hash-object`. It DOES require the
    // `--- a/<path>` / `+++ b/<path>` markers.
    out << QString("diff --git a/%1 b/%1").arg(relPath);
    out << QString("--- a/%1").arg(relPath);
    out << QString("+++ b/%1").arg(relPath);

    for (const DiffHunk &h : hunks) {
        // Re-emit the hunk header verbatim (it already begins with @@).
        // Synthesizing it from oldStart/oldLen/newStart/newLen would also
        // work, but keeping the original means any "@@ ... @@ funcname"
        // trailer survives — `git apply` doesn't care, but it makes
        // "Copy Diff" output more useful.
        if (!h.hunkHeader.isEmpty()) {
            out << lfNormalise(h.hunkHeader);
        } else {
            out << QString("@@ -%1,%2 +%3,%4 @@")
                       .arg(h.oldStart).arg(h.oldLen)
                       .arg(h.newStart).arg(h.newLen);
        }
        for (const QString &raw : h.rawDiffLines) {
            out << lfNormalise(raw);
        }
    }

    // Join with LF and end with a trailing LF — `git apply` accepts
    // either but trailing-LF is the canonical form.
    return out.join('\n') + '\n';
}

QString synthesizePatch(const QString &absFilePath,
                        const QString &repoRoot,
                        const DiffHunk &hunk,
                        QString *outErrorKind) {
    QVector<DiffHunk> v;
    v.append(hunk);
    return synthesizePatch(absFilePath, repoRoot, v, outErrorKind);
}

Result applyHunk(const QString &absFilePath,
                 const QString &repoRoot,
                 const DiffHunk &hunk,
                 Mode mode) {
    Result r;

    if (repoRoot.trimmed().isEmpty()) {
        r.errorKind = "no_workspace";
        r.message = "No git repository workspace is open.";
        return r;
    }

    // Verify repoRoot is actually a git repo.
    {
        QProcess preflight;
        preflight.setWorkingDirectory(repoRoot);
        preflight.start("git", {"rev-parse", "--git-dir"});
        if (!preflight.waitForFinished(3000)) {
            preflight.kill();
            r.errorKind = "timeout";
            r.message = "git rev-parse timed out.";
            return r;
        }
        if (preflight.exitCode() != 0) {
            r.errorKind = "not_a_repo";
            r.message = "Workspace is not a git repository.";
            return r;
        }
    }

    QString errKind;
    QString patch = synthesizePatch(absFilePath, repoRoot, hunk, &errKind);
    if (patch.isEmpty()) {
        r.errorKind = errKind.isEmpty() ? "io_error" : errKind;
        if (r.errorKind == "too_large") {
            r.message = QString("Hunk exceeds %1-line cap. Use the Git "
                                "panel to stage the whole file instead.")
                            .arg(kMaxPatchBodyLines);
        } else if (r.errorKind == "denied") {
            r.message = "Refusing to stage hunks for a credential-pattern path.";
        } else if (r.errorKind == "outside_workspace") {
            r.message = "File path resolves outside the workspace.";
        } else {
            r.message = "Could not synthesize patch.";
        }
        r.patch = patch;
        return r;
    }
    r.patch = patch;

    // Build argv. --whitespace=nowarn keeps `git apply` from rejecting on
    // trailing-whitespace differences. -c core.autocrlf=false ensures git
    // doesn't re-massage line endings between our LF-normalised patch and
    // the index. --unidiff-zero would let us drop context lines but we
    // explicitly emit 3-context patches so we don't need it.
    QStringList argv;
    argv << "-c" << "core.autocrlf=false"
         << "-c" << "core.safecrlf=false"
         << "apply"
         << "--whitespace=nowarn";

    switch (mode) {
    case Mode::Stage:
        argv << "--cached";
        break;
    case Mode::Revert:
        argv << "--reverse";
        break;
    }
    argv << "-";  // read patch from stdin

    QProcess proc;
    proc.setWorkingDirectory(repoRoot);
    proc.start("git", argv);
    if (!proc.waitForStarted(3000)) {
        r.errorKind = "git_error";
        r.message = "git executable could not be started.";
        return r;
    }
    proc.write(patch.toUtf8());
    proc.closeWriteChannel();
    if (!proc.waitForFinished(5000)) {
        proc.kill();
        proc.waitForFinished(500);
        r.errorKind = "timeout";
        r.message = "git apply timed out after 5 seconds.";
        return r;
    }

    if (proc.exitStatus() != QProcess::NormalExit || proc.exitCode() != 0) {
        r.errorKind = "git_error";
        QString stderrTxt = QString::fromUtf8(proc.readAllStandardError()).trimmed();
        QString stdoutTxt = QString::fromUtf8(proc.readAllStandardOutput()).trimmed();
        r.message = stderrTxt.isEmpty()
                        ? (stdoutTxt.isEmpty()
                               ? QString("git apply failed (exit %1).")
                                     .arg(proc.exitCode())
                               : stdoutTxt)
                        : stderrTxt;
        return r;
    }

    r.ok = true;
    return r;
}

} // namespace GitHunkApply
