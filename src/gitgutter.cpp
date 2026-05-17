// SPDX-License-Identifier: GPL-3.0-or-later

#include "gitgutter.h"
#include <QFileInfo>
#include <QDir>
#include <QTemporaryFile>
#include <QTextStream>
#include <QRegularExpression>

bool GitGutter::isGitRepo(const QString &filePath) {
    QProcess proc;
    proc.setWorkingDirectory(QFileInfo(filePath).path());
    proc.start("git", {"rev-parse", "--is-inside-work-tree"});
    proc.waitForFinished(3000);
    return proc.exitCode() == 0 && proc.readAllStandardOutput().trimmed() == "true";
}

QString GitGutter::getGitBranch(const QString &filePath) {
    QProcess proc;
    proc.setWorkingDirectory(QFileInfo(filePath).path());
    proc.start("git", {"branch", "--show-current"});
    proc.waitForFinished(3000);
    return proc.exitCode() == 0 ? QString::fromUtf8(proc.readAllStandardOutput()).trimmed() : "";
}

QVector<GitLineStatus> GitGutter::getChangedLines(const QString &filePath, const QString &currentText) {
    QVector<GitLineStatus> result;

    if (filePath.isEmpty() || !isGitRepo(filePath))
        return result;

    // Get the git version of the file
    QProcess proc;
    proc.setWorkingDirectory(QFileInfo(filePath).path());
    QString relPath = QDir(QFileInfo(filePath).path()).relativeFilePath(filePath);

    // Get HEAD version
    proc.start("git", {"show", "HEAD:" + relPath});
    proc.waitForFinished(5000);

    if (proc.exitCode() != 0) {
        // New file (not in git yet) — all lines are "added"
        int lineCount = currentText.count('\n') + 1;
        for (int i = 1; i <= lineCount; i++)
            result.append({i, 1});
        return result;
    }

    QString gitText = QString::fromUtf8(proc.readAllStandardOutput());

    // Write current text to temp file for diff
    QTemporaryFile tmpFile;
    tmpFile.setAutoRemove(true);
    if (!tmpFile.open()) return result;
    tmpFile.write(currentText.toUtf8());
    tmpFile.flush();

    // Run diff
    QProcess diffProc;
    diffProc.setWorkingDirectory(QFileInfo(filePath).path());
    diffProc.start("diff", {"-u", "--no-index", "-", tmpFile.fileName()});
    diffProc.write(gitText.toUtf8());
    diffProc.closeWriteChannel();
    diffProc.waitForFinished(5000);

    QString diffOutput = QString::fromUtf8(diffProc.readAllStandardOutput());

    // Parse unified diff output
    QRegularExpression hunkHeader("@@ -(\\d+)(?:,(\\d+))? \\+(\\d+)(?:,(\\d+))? @@");
    QStringList diffLines = diffOutput.split('\n');

    int currentLine = 0;

    for (const QString &line : diffLines) {
        auto match = hunkHeader.match(line);
        if (match.hasMatch()) {
            currentLine = match.captured(3).toInt();
            continue;
        }

        if (line.startsWith("---") || line.startsWith("+++") || line.startsWith("\\"))
            continue;

        if (line.startsWith("+")) {
            result.append({currentLine, 1}); // Added (green)
            currentLine++;
        } else if (line.startsWith("-")) {
            // Deleted line — mark at current position (red)
            result.append({currentLine, 3});
        } else if (line.startsWith(" ")) {
            currentLine++;
        }
    }

    return result;
}

// ─── v0.1.62 — hunk accessor for the gutter-popup staging flow ──────────
//
// Implementation note: we shell out to `git diff -U0 --no-color -- <relpath>`
// (default context is 3 lines, but we want exactly what git would apply,
// so we let git decide rather than synthesizing the diff via /usr/bin/diff
// the way getChangedLines() does for the speculative dirty-buffer view).
// `--no-color` ensures we get raw ANSI-free bytes regardless of the user's
// global git config; without it the @@ header parsing breaks on terminals
// where someone set color.ui=always.
//
// Why DEFAULT context (3 lines) not -U0:
//   `git apply --cached` is strict — it requires context lines to anchor
//   the hunk in the index version of the file. -U0 patches frequently
//   reject on otherwise-clean working copies. The default 3-line context
//   is what `git add -p` (the gold-standard per-hunk stager) also uses.
QVector<DiffHunk> GitGutter::hunksForFile(const QString &absPath) {
    QVector<DiffHunk> hunks;
    if (absPath.isEmpty() || !isGitRepo(absPath)) return hunks;

    QFileInfo fi(absPath);
    QString dirPath = fi.path();
    QString relPath = QDir(dirPath).relativeFilePath(absPath);

    QProcess proc;
    proc.setWorkingDirectory(dirPath);
    // -c core.autocrlf=false → don't let git massage line endings between
    //                          what we see and what we'd later try to apply.
    // -c color.ui=never      → defense-in-depth over --no-color.
    proc.start("git", {"-c", "core.autocrlf=false",
                       "-c", "color.ui=never",
                       "diff", "--no-color", "--", relPath});
    proc.waitForFinished(5000);
    if (proc.exitCode() != 0 && proc.exitCode() != 1) {
        // git diff returns 1 if there are differences, 0 if not.
        // Anything else is an actual error (e.g. ambiguous ref) — bail.
        return hunks;
    }

    QString diffOutput = QString::fromUtf8(proc.readAllStandardOutput());
    if (diffOutput.isEmpty()) return hunks;

    QRegularExpression hunkHeader("^@@ -(\\d+)(?:,(\\d+))? \\+(\\d+)(?:,(\\d+))? @@.*$");
    const QStringList diffLines = diffOutput.split('\n');

    DiffHunk current;
    bool inHunk = false;
    bool sawAnyBodyLine = false;

    auto flush = [&]() {
        if (inHunk && sawAnyBodyLine) {
            hunks.append(current);
        }
        current = DiffHunk{};
        inHunk = false;
        sawAnyBodyLine = false;
    };

    for (const QString &line : diffLines) {
        // Skip "diff --git", "index ...", "--- a/...", "+++ b/..." headers.
        // We only care about hunk headers + body lines.
        if (line.startsWith("diff --git") ||
            line.startsWith("index ") ||
            line.startsWith("--- ") ||
            line.startsWith("+++ ") ||
            line.startsWith("new file mode") ||
            line.startsWith("deleted file mode") ||
            line.startsWith("similarity index") ||
            line.startsWith("rename from") ||
            line.startsWith("rename to") ||
            line.startsWith("Binary files")) {
            flush();
            continue;
        }

        auto m = hunkHeader.match(line);
        if (m.hasMatch()) {
            flush();
            current.oldStart = m.captured(1).toInt();
            current.oldLen   = m.captured(2).isEmpty() ? 1 : m.captured(2).toInt();
            current.newStart = m.captured(3).toInt();
            current.newLen   = m.captured(4).isEmpty() ? 1 : m.captured(4).toInt();
            current.hunkHeader = line;
            current.rawDiffLines.clear();
            inHunk = true;
            sawAnyBodyLine = false;
            continue;
        }

        if (!inHunk) continue;

        // Body lines must start with one of ' ', '+', '-', '\'.
        // The '\' line is "\ No newline at end of file" — keep it: git
        // apply needs it verbatim or the patch is rejected.
        if (line.isEmpty()) {
            // Trailing empty line of the diff output — could be the file
            // ending. Treat as a context space line ONLY if we're still
            // in the middle of a hunk that hasn't reached newLen. Safer:
            // close out the hunk here.
            flush();
            continue;
        }
        QChar c0 = line[0];
        if (c0 == ' ' || c0 == '+' || c0 == '-' || c0 == '\\') {
            current.rawDiffLines.append(line);
            sawAnyBodyLine = true;
            continue;
        }
        // Unknown prefix — diff ended.
        flush();
    }
    flush();

    return hunks;
}

int GitGutter::hunkIndexForLine(const QVector<DiffHunk> &hunks, int oneBasedLine) {
    for (int i = 0; i < hunks.size(); ++i) {
        const DiffHunk &h = hunks[i];
        // Pure-addition hunks have newLen > 0 and newStart pointing at the
        // first added line. Pure-deletion hunks have newLen == 0 and
        // newStart pointing at the line BEFORE which the deletion occurs;
        // we accept a click on newStart OR newStart+1 so the user can hit
        // either side of the deletion bar.
        if (h.newLen == 0) {
            if (oneBasedLine == h.newStart || oneBasedLine == h.newStart + 1) {
                return i;
            }
            continue;
        }
        const int hunkEnd = h.newStart + h.newLen - 1;
        if (oneBasedLine >= h.newStart && oneBasedLine <= hunkEnd) {
            return i;
        }
    }
    return -1;
}
