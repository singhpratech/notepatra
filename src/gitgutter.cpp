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
