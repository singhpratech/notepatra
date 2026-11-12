// SPDX-License-Identifier: GPL-3.0-or-later
#include "git_write.h"
#include "git_tools.h"

#include <QDir>
#include <QFileInfo>
#include <QJsonArray>
#include <QPointer>
#include <QProcess>
#include <QProcessEnvironment>
#include <QTimer>

namespace GitWrite {
namespace {

QProcessEnvironment gitEnv() {
    QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
    // Never prompt: a missing credential is an error, not a hang.
    env.insert(QStringLiteral("GIT_TERMINAL_PROMPT"), QStringLiteral("0"));
    env.insert(QStringLiteral("GCM_INTERACTIVE"), QStringLiteral("never"));
    // Stable, parseable output whatever the user's locale.
    env.insert(QStringLiteral("LC_ALL"), QStringLiteral("C"));
    return env;
}

void prepare(QProcess &p, const QString &dir, bool literalPaths = false) {
    p.setWorkingDirectory(dir);
    QProcessEnvironment env = gitEnv();
    // Client paths are file names, never globs or :(magic): "a[1].txt" must
    // not also stage "a1.txt". Only for the commands that take client paths:
    // stash push -u relies on pathspec magic internally and breaks with it.
    if (literalPaths) env.insert(QStringLiteral("GIT_LITERAL_PATHSPECS"), QStringLiteral("1"));
    p.setProcessEnvironment(env);
    p.setProgram(QStringLiteral("git"));
}

} // namespace

QString Result::message(const QString &what) const {
    if (!started) return QStringLiteral("%1 failed: git could not be started").arg(what);
    if (timedOut) return QStringLiteral("%1 timed out").arg(what);
    const QString text = (err.trimmed().isEmpty() ? out : err).trimmed();
    return text.isEmpty() ? QStringLiteral("%1 failed (exit code %2)").arg(what).arg(exitCode)
                          : QStringLiteral("%1 failed: %2").arg(what, text.left(4000));
}

Result run(const QString &dir, const QStringList &args, int timeoutMs, bool literalPaths) {
    Result r;
    QProcess p;
    prepare(p, dir, literalPaths);
    p.setArguments(args);
    p.start();
    if (!p.waitForStarted(3000)) return r;
    r.started = true;
    p.closeWriteChannel();
    if (!p.waitForFinished(timeoutMs)) {
        p.kill();
        p.waitForFinished(1000);
        r.timedOut = true;
    }
    r.outBytes = p.readAllStandardOutput();
    r.out = QString::fromUtf8(r.outBytes.constData(), r.outBytes.size());
    r.err = QString::fromUtf8(p.readAllStandardError());
    r.exitCode = p.exitCode();
    r.ok = !r.timedOut && p.exitStatus() == QProcess::NormalExit && r.exitCode == 0;
    return r;
}

void runAsync(QObject *context, const QString &dir, const QStringList &args, int timeoutMs,
              std::function<void(const Result &)> done) {
    auto *p = new QProcess(context);
    prepare(*p, dir);
    p->setArguments(args);
    auto *timer = new QTimer(p);
    timer->setSingleShot(true);
    auto finished = std::make_shared<bool>(false);
    auto finish = [p, done, finished](Result r) {
        if (*finished) return;
        *finished = true;
        done(r);
        p->deleteLater();
    };
    QObject::connect(timer, &QTimer::timeout, p, [p, finish]() {
        Result r;
        r.started = true;
        r.timedOut = true;
        p->disconnect();
        p->kill();
        finish(r);
    });
    QObject::connect(p, &QProcess::errorOccurred, p, [p, finish](QProcess::ProcessError e) {
        if (e != QProcess::FailedToStart) return;
        Q_UNUSED(p);
        finish(Result());
    });
    QObject::connect(p, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished), p,
                     [p, finish](int code, QProcess::ExitStatus st) {
                         Result r;
                         r.started = true;
                         r.exitCode = code;
                         r.out = QString::fromUtf8(p->readAllStandardOutput());
                         r.err = QString::fromUtf8(p->readAllStandardError());
                         r.ok = st == QProcess::NormalExit && code == 0;
                         finish(r);
                     });
    p->start();
    p->closeWriteChannel();
    timer->start(timeoutMs);
}

QString repoRoot(const QString &dir, QString *err) {
    if (dir.isEmpty() || !QFileInfo(dir).isDir()) {
        if (err)
            *err = QStringLiteral("no workspace folder is open (set one with "
                                  "set_workspace_folder or File > Open Folder as Workspace)");
        return QString();
    }
    const Result r = run(dir, {QStringLiteral("rev-parse"), QStringLiteral("--show-toplevel")}, 5000);
    if (!r.ok) {
        if (err)
            *err = r.started ? QStringLiteral("the workspace folder is not inside a git repository")
                             : QStringLiteral("git executable not found on PATH");
        return QString();
    }
    const QString top = QDir::fromNativeSeparators(r.out.trimmed());
    const QString canon = QFileInfo(top).canonicalFilePath();
    return canon.isEmpty() ? top : canon;
}

QJsonObject context(const QString &root, QString *err) {
    const Result st = run(root, {QStringLiteral("status"), QStringLiteral("--porcelain=v2"),
                                 QStringLiteral("--branch"), QStringLiteral("-z"),
                                 QStringLiteral("--untracked-files=all")},
                          10000);
    if (!st.ok) {
        if (err) *err = st.message(QStringLiteral("git status"));
        return QJsonObject();
    }
    // Porcelain bytes: a QString round trip would stop at the first NUL.
    QJsonObject o = GitTools::parsePorcelainV2(st.outBytes);
    o[QStringLiteral("repo_root")] = root;
    const Result br = run(root, {QStringLiteral("for-each-ref"),
                                 QStringLiteral("--format=%(refname:short)"),
                                 QStringLiteral("refs/heads/")},
                          5000);
    o[QStringLiteral("branches")] =
        QJsonArray::fromStringList(br.out.split(QLatin1Char('\n'), Qt::SkipEmptyParts));
    const QString branch = o.value(QLatin1String("branch")).toString();
    if (!branch.isEmpty() && !branch.startsWith(QLatin1Char('('))) {
        const Result rem = run(root, {QStringLiteral("config"), QStringLiteral("--get"),
                                      QStringLiteral("branch.%1.remote").arg(branch)},
                               5000);
        const Result mer = run(root, {QStringLiteral("config"), QStringLiteral("--get"),
                                      QStringLiteral("branch.%1.merge").arg(branch)},
                               5000);
        o[QStringLiteral("upstream_remote")] = rem.ok ? rem.out.trimmed() : QString();
        o[QStringLiteral("upstream_merge")] = mer.ok ? mer.out.trimmed() : QString();
    }
    const Result sl = run(root, {QStringLiteral("stash"), QStringLiteral("list")}, 5000);
    o[QStringLiteral("stash_count")] = sl.out.split(QLatin1Char('\n'), Qt::SkipEmptyParts).size();
    return o;
}

} // namespace GitWrite
