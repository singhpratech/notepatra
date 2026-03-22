#ifndef GITGUTTER_H
#define GITGUTTER_H

#include <QString>
#include <QVector>
#include <QProcess>

struct GitLineStatus {
    int line;       // 1-based line number
    int status;     // 0=unchanged, 1=added, 2=modified, 3=deleted
};

class GitGutter {
public:
    static QVector<GitLineStatus> getChangedLines(const QString &filePath, const QString &currentText);
    static bool isGitRepo(const QString &filePath);
    static QString getGitBranch(const QString &filePath);
};

#endif
