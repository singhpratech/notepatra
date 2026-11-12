// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef NOTEPATRA_GIT_WRITE_H
#define NOTEPATRA_GIT_WRITE_H

// Headless git runner for the MCP git write tools (v0.1.133). The argv is
// always built by the caller from fixed verbs; paths follow "--". Every run
// has a timeout, a closed stdin and no terminal prompt (GIT_TERMINAL_PROMPT=0),
// so a credential request fails instead of hanging. No dialog, ever.

#include <QByteArray>
#include <QJsonObject>
#include <QObject>
#include <QString>
#include <QStringList>

#include <functional>

namespace GitWrite {

struct Result {
    bool started = false;
    bool timedOut = false;
    bool ok = false;
    int exitCode = -1;
    QString out;
    QString err;
    QByteArray outBytes; // raw stdout (porcelain -z output keeps its NULs)
    // git's own words for a failure (stderr, else stdout), or the timeout.
    QString message(const QString &what) const;
};

Result run(const QString &dir, const QStringList &args, int timeoutMs = 15000);
// Same, asynchronously: done() runs on the GUI thread, once.
void runAsync(QObject *context, const QString &dir, const QStringList &args, int timeoutMs,
              std::function<void(const Result &)> done);

// Top level of the repository containing dir; "" + *err when it is none.
QString repoRoot(const QString &dir, QString *err);

// {repo_root, branch, upstream, upstream_remote, upstream_merge, ahead, behind,
//  staged[], modified[], untracked[], conflicts[], branches[], stash_count}.
QJsonObject context(const QString &repoRoot, QString *err);

} // namespace GitWrite

#endif
