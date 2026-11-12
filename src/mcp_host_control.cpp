// SPDX-License-Identifier: GPL-3.0-or-later
//
// v0.1.133 Phase 3 MCP host: git writes, Noter management, settings, data,
// REST and the small leftovers. The bridge (mcp_bridge.cpp) validates every
// argument and shows the approval card; these lambdas run the app's own code
// (GitHunkApply, MergeHelper, NotesPanel, DbConnections, DbSchema, RestClient)
// and never open a dialog that could block the bridge.

#include "mainwindow.h"
#include "ai_tools.h"
#include "config.h"
#include "csvanalyst.h"
#include "curl_convert.h"
#include "dbconnections.h"
#include "dbtree.h"
#include "editor.h"
#include "fileexplorer.h"
#include "git_hunk_apply.h"
#include "git_write.h"
#include "gitgutter.h"
#include "http_file.h"
#include "mcp_bridge.h"
#include "merge_helper.h"
#include "notes.h"
#include "notes_export.h"
#include "notes_storage.h"
#include "notes_todos.h"
#include "restclient.h"
#include "tabmanager.h"
#include "atomic_write.h"

#include <QAction>
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QFontDatabase>
#include <QFutureWatcher>
#include <QJsonArray>
#include <QPointer>
#include <QRegularExpression>
#include <QSqlDatabase>
#include <QSqlError>
#include <QSqlQuery>
#include <QSqlRecord>
#include <QTemporaryFile>
#include <QTimer>
#include <QtConcurrent/QtConcurrentRun>

#include <memory>

namespace {

constexpr int kNetGitTimeoutMs = 60 * 1000;
constexpr int kDbTimeoutMs = 20 * 1000;
constexpr int kSqlTimeoutMs = 60 * 1000;
constexpr int kRestBodyCap = 256 * 1024;
constexpr int kHunkLinesCap = 400;

QStringList strings(const QJsonValue &v) {
    QStringList out;
    for (const QJsonValue &x : v.toArray()) out << x.toString();
    return out;
}

// The whole hunk (header and every body line, uncapped), hashed: what the
// card showed must be exactly what is applied.
QString hunkFingerprint(const DiffHunk &h) {
    return QString::fromLatin1(
        QCryptographicHash::hash((h.hunkHeader + QLatin1Char('\n') +
                                  h.rawDiffLines.join(QLatin1Char('\n'))).toUtf8(),
                                 QCryptographicHash::Sha256).toHex());
}

// Every line of a conflict region, markers included, hashed.
QString regionFingerprint(const QString &text, const MergeHelper::ConflictRegion &r) {
    const QStringList lines = text.split(QLatin1Char('\n'));
    const QString region =
        lines.mid(r.startLine, r.endLine - r.startLine + 1).join(QLatin1Char('\n'));
    return QString::fromLatin1(
        QCryptographicHash::hash(QString::number(r.startLine).toUtf8() + '\n' + region.toUtf8(),
                                 QCryptographicHash::Sha256).toHex());
}

QJsonArray hunksJson(const QVector<DiffHunk> &hunks) {
    QJsonArray out;
    for (int i = 0; i < hunks.size() && i < 500; ++i) {
        const DiffHunk &h = hunks.at(i);
        QStringList lines = h.rawDiffLines.mid(0, kHunkLinesCap);
        if (h.rawDiffLines.size() > kHunkLinesCap)
            lines << QStringLiteral("[... %1 more lines ...]")
                         .arg(h.rawDiffLines.size() - kHunkLinesCap);
        out.append(QJsonObject{{QStringLiteral("index"), i},
                               {QStringLiteral("header"), h.hunkHeader},
                               {QStringLiteral("old_start"), h.oldStart},
                               {QStringLiteral("old_lines"), h.oldLen},
                               {QStringLiteral("new_start"), h.newStart},
                               {QStringLiteral("new_lines"), h.newLen},
                               {QStringLiteral("diff"), h.hunkHeader + QLatin1Char('\n') +
                                                            lines.join(QLatin1Char('\n'))},
                               {QStringLiteral("fingerprint"), hunkFingerprint(h)}});
    }
    return out;
}

QJsonArray regionsJson(const QString &text,
                       const QVector<MergeHelper::ConflictRegion> &regions) {
    QJsonArray out;
    for (int i = 0; i < regions.size(); ++i) {
        const auto &r = regions.at(i);
        out.append(QJsonObject{{QStringLiteral("index"), i},
                               {QStringLiteral("fingerprint"), regionFingerprint(text, r)},
                               {QStringLiteral("start_line"), r.startLine + 1},
                               {QStringLiteral("separator_line"), r.separatorLine + 1},
                               {QStringLiteral("end_line"), r.endLine + 1},
                               {QStringLiteral("ours_label"), r.oursLabel},
                               {QStringLiteral("theirs_label"), r.theirsLabel},
                               {QStringLiteral("ours"), r.ours},
                               {QStringLiteral("theirs"), r.theirs}});
    }
    return out;
}

QString readUtf8(const QString &path, QString *err) {
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) {
        if (err) *err = QStringLiteral("cannot read %1").arg(QDir::toNativeSeparators(path));
        return QString();
    }
    if (f.size() > 20 * 1024 * 1024) {
        if (err) *err = QStringLiteral("file too large");
        return QString();
    }
    return QString::fromUtf8(f.readAll());
}

// A driver error never carries the stored password or a key=value secret.
QString scrubDbError(QString text, const DbConnections::Record &r) {
    if (r.password.size() >= 2) text.replace(r.password, QStringLiteral("***"));
    static const QRegularExpression kv(
        QStringLiteral("((?:password|passwd|pwd|secret|token|key_id|secret_access_key)\\s*=\\s*)"
                       "('[^']*'|\"[^\"]*\"|[^;\\s]+)"),
        QRegularExpression::CaseInsensitiveOption);
    text.replace(kv, QStringLiteral("\\1***"));
    return text;
}

QJsonObject connectionJson(const DbConnections::Record &r) {
    return {{QStringLiteral("name"), r.name},
            {QStringLiteral("driver"), r.driver},
            {QStringLiteral("host"), r.host},
            {QStringLiteral("port"), r.port},
            {QStringLiteral("database"), r.database},
            {QStringLiteral("username"), r.username},
            {QStringLiteral("has_password"), !r.password.isEmpty()},
            {QStringLiteral("has_options"), !r.options.isEmpty()}};
}

// A file database (SQLite, DuckDB) is opened by test_connection and
// describe_table only when the file is already there: a test or a schema
// read never creates one. "" when fine.
QString missingDatabaseFile(const DbConnections::Record &r) {
    if (r.driver != QLatin1String("QSQLITE") && r.driver != QLatin1String("DUCKDB"))
        return QString();
    const QString db = r.database.trimmed();
    if (db == QLatin1String(":memory:")) return QString();
    if (db.isEmpty()) return QStringLiteral("the connection names no database file");
    if (!QFileInfo(db).isFile())
        return QStringLiteral("the database file does not exist: %1 (nothing was created)")
            .arg(QDir::toNativeSeparators(db));
    return QString();
}

const char *kNoPasswordNote =
    "No password, token or connection options are stored by MCP. If the database needs "
    "one, the user enters it in Manage Connections (open_connections_dialog opens it).";

// Runs fn on a worker thread; done() gets its result on the GUI thread, or
// a timeout error once timeoutMs passes (the worker is then left to finish).
void runGuarded(QObject *ctx, int timeoutMs, std::function<QJsonObject(QString *)> fn,
                McpEditorHost::Done done, const QString &timeoutMessage) {
    struct Out {
        QJsonObject r;
        QString e;
    };
    auto settled = std::make_shared<bool>(false);
    auto *w = new QFutureWatcher<Out>(ctx);
    auto *t = new QTimer(w);
    t->setSingleShot(true);
    QObject::connect(t, &QTimer::timeout, ctx, [settled, done, timeoutMessage]() {
        if (*settled) return;
        *settled = true;
        done(QJsonObject(), timeoutMessage);
    });
    QObject::connect(w, &QFutureWatcher<Out>::finished, ctx, [w, settled, done]() {
        const Out o = w->result();
        w->deleteLater();
        if (*settled) return;
        *settled = true;
        done(o.r, o.e);
    });
    w->setFuture(QtConcurrent::run([fn]() {
        Out o;
        o.r = fn(&o.e);
        return o;
    }));
    t->start(timeoutMs);
}

} // namespace

void MainWindow::installMcpControlHost(McpEditorHost &host) {
    // ── Git (workspace repository only) ────────────────────────────────
    auto refreshGit = [this]() {
        for (int i = 0; i < m_tabs->count(); ++i)
            if (Editor *ed = m_tabs->editorAt(i)) ed->updateGitGutter();
    };
    host.gitOp = [this, refreshGit](const QString &op, const QJsonObject &a,
                                    QString *err) -> QJsonObject {
        const QString root = GitWrite::repoRoot(workspaceFolder(), err);
        if (root.isEmpty()) return QJsonObject();
        if (op == QLatin1String("context")) return GitWrite::context(root, err);
        // Every write runs in the repository the card named, or not at all: a
        // set_workspace_folder approved while this card waited moves the
        // workspace, never the write.
        const bool readOp = op == QLatin1String("list_hunks") ||
                            op == QLatin1String("list_conflicts");
        if (!readOp && a.value(QLatin1String("expect_root")).toString() != root) {
            *err = QStringLiteral("the workspace changed while the card was waiting; nothing "
                                  "was changed");
            return QJsonObject();
        }
        // Client paths (git_stage / git_unstage / git_discard) are literal.
        const bool clientPaths = a.contains(QLatin1String("paths"));
        auto git = [&](const QStringList &args, const QString &what, int ms = 15000) {
            const GitWrite::Result r = GitWrite::run(root, args, ms, clientPaths);
            if (!r.ok) *err = r.message(what);
            return r;
        };
        const QStringList paths = strings(a.value(QLatin1String("paths")));
        const QString rel = a.value(QLatin1String("path")).toString();
        const QString abs = root + QLatin1Char('/') + rel;
        QJsonObject out;
        if (op == QLatin1String("git_stage")) {
            if (!git(QStringList{QStringLiteral("add"), QStringLiteral("--")} + paths,
                     QStringLiteral("git add")).ok)
                return out;
            out[QStringLiteral("staged")] = QJsonArray::fromStringList(paths);
        } else if (op == QLatin1String("git_unstage")) {
            const bool hasHead = GitWrite::run(root, {QStringLiteral("rev-parse"),
                                                      QStringLiteral("--verify"),
                                                      QStringLiteral("-q"),
                                                      QStringLiteral("HEAD")}).ok;
            const QStringList args =
                hasHead ? QStringList{QStringLiteral("reset"), QStringLiteral("-q"),
                                      QStringLiteral("HEAD"), QStringLiteral("--")}
                        : QStringList{QStringLiteral("rm"), QStringLiteral("--cached"),
                                      QStringLiteral("-q"), QStringLiteral("--")};
            if (!git(args + paths, QStringLiteral("git reset")).ok) return out;
            out[QStringLiteral("unstaged")] = QJsonArray::fromStringList(paths);
        } else if (op == QLatin1String("git_discard")) {
            if (!git(QStringList{QStringLiteral("checkout"), QStringLiteral("--")} + paths,
                     QStringLiteral("git checkout --")).ok)
                return out;
            out[QStringLiteral("discarded")] = QJsonArray::fromStringList(paths);
        } else if (op == QLatin1String("git_branch_create")) {
            const QString name = a.value(QLatin1String("name")).toString();
            if (!git({QStringLiteral("check-ref-format"), QStringLiteral("--branch"), name},
                     QStringLiteral("branch name check")).ok)
                return out;
            const bool sw = a.value(QLatin1String("switch")).toBool();
            if (!git(sw ? QStringList{QStringLiteral("checkout"), QStringLiteral("-b"), name}
                        : QStringList{QStringLiteral("branch"), name},
                     QStringLiteral("git branch")).ok)
                return out;
            out[QStringLiteral("branch")] = name;
            out[QStringLiteral("switched")] = sw;
        } else if (op == QLatin1String("git_checkout")) {
            const QString name = a.value(QLatin1String("name")).toString();
            if (!git({QStringLiteral("show-ref"), QStringLiteral("--verify"), QStringLiteral("-q"),
                      QStringLiteral("refs/heads/") + name},
                     QStringLiteral("branch lookup")).ok) {
                *err = QStringLiteral("no local branch named '%1'").arg(name);
                return out;
            }
            // No --force: git refuses when local changes would be overwritten.
            if (!git({QStringLiteral("checkout"), name, QStringLiteral("--")},
                     QStringLiteral("git checkout"), 60000).ok)
                return out;
            out[QStringLiteral("branch")] = name;
        } else if (op == QLatin1String("git_stash")) {
            const QString action = a.value(QLatin1String("action")).toString();
            QStringList args{QStringLiteral("stash"), action};
            if (action == QLatin1String("push")) {
                args << QStringLiteral("-u");
                if (a.contains(QLatin1String("message")))
                    args << QStringLiteral("-m") << a.value(QLatin1String("message")).toString();
            }
            const GitWrite::Result r = git(args, QStringLiteral("git stash ") + action, 60000);
            if (!r.ok) return out;
            out[QStringLiteral("action")] = action;
            out[QStringLiteral("output")] = (r.out + r.err).trimmed().left(4000);
        } else if (op == QLatin1String("list_hunks")) {
            out[QStringLiteral("path")] = rel;
            out[QStringLiteral("hunks")] = hunksJson(GitGutter::hunksForFile(abs));
            return out;
        } else if (op == QLatin1String("git_hunk")) {
            const QVector<DiffHunk> hunks = GitGutter::hunksForFile(abs);
            const int i = a.value(QLatin1String("hunk_index")).toInt();
            // The full hunk text, not just its header: same header, other
            // lines must not apply.
            if (i < 0 || i >= hunks.size() ||
                hunks.at(i).hunkHeader != a.value(QLatin1String("expect_header")).toString() ||
                hunkFingerprint(hunks.at(i)) != a.value(QLatin1String("expect_hunk")).toString()) {
                *err = QStringLiteral("the hunk changed after the approval card was shown; "
                                      "nothing was applied");
                return out;
            }
            const bool stage = a.value(QLatin1String("action")).toString() == QLatin1String("stage");
            const GitHunkApply::Result r = GitHunkApply::applyHunk(
                abs, root, hunks.at(i),
                stage ? GitHunkApply::Mode::Stage : GitHunkApply::Mode::Revert);
            if (!r.ok) {
                *err = r.message.isEmpty() ? QStringLiteral("could not apply the hunk (%1)")
                                                 .arg(r.errorKind)
                                           : r.message;
                return out;
            }
            out[QStringLiteral("path")] = rel;
            out[QStringLiteral("hunk_index")] = i;
            out[QStringLiteral("action")] = a.value(QLatin1String("action"));
        } else if (op == QLatin1String("list_conflicts")) {
            if (rel.isEmpty()) {
                const QJsonObject ctx = GitWrite::context(root, err);
                QJsonArray files;
                for (const QString &f : strings(ctx.value(QLatin1String("conflicts")))) {
                    QString e;
                    const QString text = readUtf8(root + QLatin1Char('/') + f, &e);
                    files.append(QJsonObject{{QStringLiteral("path"), f},
                                             {QStringLiteral("regions"),
                                              MergeHelper::scanConflicts(text).size()}});
                }
                out[QStringLiteral("files")] = files;
                return out;
            }
            const int t = tabIndexForPath(abs);
            Editor *ed = t >= 0 ? m_tabs->editorAt(t) : nullptr;
            const QString text = ed ? ed->text() : readUtf8(abs, err);
            if (!err->isEmpty()) return out;
            out[QStringLiteral("path")] = rel;
            out[QStringLiteral("regions")] = regionsJson(text, MergeHelper::scanConflicts(text));
            return out;
        } else if (op == QLatin1String("resolve_conflict")) {
            int t = tabIndexForPath(abs);
            if (t >= 0 && m_tabs->editorAt(t) && m_tabs->editorAt(t)->isModified()) {
                *err = QStringLiteral("%1 has unsaved changes in the editor; save or discard "
                                      "them first, then resolve").arg(rel);
                return out;
            }
            if (t < 0) {
                openFile(abs);   // loads with the file's own encoding
                t = tabIndexForPath(abs);
            }
            Editor *ed = t >= 0 ? m_tabs->editorAt(t) : nullptr;
            if (!ed) {
                *err = QStringLiteral("could not open %1").arg(rel);
                return out;
            }
            const QString text = ed->text();
            const QVector<MergeHelper::ConflictRegion> regions = MergeHelper::scanConflicts(text);
            const int i = a.value(QLatin1String("region")).toInt();
            // Position AND every line of the region, markers included.
            if (i < 0 || i >= regions.size() ||
                regions.at(i).startLine + 1 != a.value(QLatin1String("expect_start")).toInt() ||
                regionFingerprint(text, regions.at(i)) !=
                    a.value(QLatin1String("expect_region")).toString()) {
                *err = QStringLiteral("the conflict regions changed after the approval card "
                                      "was shown; nothing was changed");
                return out;
            }
            const QString take = a.value(QLatin1String("take")).toString();
            QString repl;
            if (take != QLatin1String("theirs")) repl += regions.at(i).ours;
            if (take != QLatin1String("ours")) repl += regions.at(i).theirs;
            const QString next = MergeHelper::applyResolution(text, regions, i, repl);
            // One undo step, as the Merge Helper's own edit is.
            const int first = ed->firstVisibleLine();
            ed->beginUndoAction();
            ed->setText(next);
            ed->endUndoAction();
            ed->setFirstVisibleLine(first);
            // Editor::saveFile keeps the file's encoding, BOM and EOL; atomic.
            if (!ed->saveFile()) {
                *err = QStringLiteral("resolved in the editor but could not save %1").arg(rel);
                return out;
            }
            updateTabTitle(t);
            if (m_fileWatcher) m_fileTimestamps[ed->filePath()] = QFileInfo(ed->filePath()).lastModified();
            out[QStringLiteral("path")] = rel;
            out[QStringLiteral("remaining")] = MergeHelper::scanConflicts(next).size();
        } else {
            *err = QStringLiteral("unknown git op: %1").arg(op);
            return out;
        }
        refreshGit();
        return out;
    };
    host.gitAsync = [this, refreshGit](const QString &op, const QJsonObject &a,
                                       McpEditorHost::Done done) {
        QString err;
        const QString root = GitWrite::repoRoot(workspaceFolder(), &err);
        const QJsonObject ctx = root.isEmpty() ? QJsonObject() : GitWrite::context(root, &err);
        if (!err.isEmpty()) {
            done(QJsonObject(), err);
            return;
        }
        if (a.value(QLatin1String("expect_root")).toString() != root) {
            done(QJsonObject(), QStringLiteral("the workspace changed while the card was "
                                               "waiting; nothing was changed"));
            return;
        }
        if (op == QLatin1String("git_commit")) {
            // The files on the card are the files committed, or nothing is.
            QStringList now = strings(ctx.value(QLatin1String("staged")));
            QStringList want = strings(a.value(QLatin1String("expect_staged")));
            now.sort();
            want.sort();
            if (now != want) {
                done(QJsonObject(), QStringLiteral("the staged files changed after the approval "
                                                   "card was shown; nothing was committed"));
                return;
            }
            const QString branch = ctx.value(QLatin1String("branch")).toString();
            // Hooks run normally; their failure text comes back as the error.
            GitWrite::runAsync(this, root,
                               {QStringLiteral("commit"), QStringLiteral("-m"),
                                a.value(QLatin1String("message")).toString()},
                               120000, [done, root, branch, want, refreshGit](const GitWrite::Result &r) {
                                   if (!r.ok) {
                                       done(QJsonObject(), r.message(QStringLiteral("git commit")));
                                       return;
                                   }
                                   const GitWrite::Result h = GitWrite::run(
                                       root, {QStringLiteral("rev-parse"), QStringLiteral("--short"),
                                              QStringLiteral("HEAD")});
                                   refreshGit();
                                   done(QJsonObject{{QStringLiteral("commit"), h.out.trimmed()},
                                                    {QStringLiteral("branch"), branch},
                                                    {QStringLiteral("files"),
                                                     QJsonArray::fromStringList(want)}},
                                        QString());
                               });
            return;
        }
        // The card named a branch and its upstream; both must still hold.
        const QString branch = ctx.value(QLatin1String("branch")).toString();
        const QString remote = ctx.value(QLatin1String("upstream_remote")).toString();
        const QString merge = ctx.value(QLatin1String("upstream_merge")).toString();
        if (branch != a.value(QLatin1String("branch")).toString() ||
            ctx.value(QLatin1String("upstream")).toString() !=
                a.value(QLatin1String("upstream")).toString() ||
            remote.isEmpty() || remote.startsWith(QLatin1Char('-')) ||
            !merge.startsWith(QLatin1String("refs/heads/"))) {
            done(QJsonObject(), QStringLiteral("the branch or its upstream changed after the "
                                               "approval card was shown; nothing was sent"));
            return;
        }
        QStringList args;
        QString what;
        if (op == QLatin1String("git_fetch")) {
            args = QStringList{QStringLiteral("fetch"), QStringLiteral("--"), remote};
            what = QStringLiteral("git fetch");
        } else if (op == QLatin1String("git_pull")) {
            args = QStringList{QStringLiteral("pull"), QStringLiteral("--ff-only"), QStringLiteral("--no-rebase"),
                    QStringLiteral("--"), remote, merge};
            what = QStringLiteral("git pull --ff-only");
        } else {
            // Current branch to its upstream branch only, never forced.
            args = QStringList{QStringLiteral("push"), QStringLiteral("--"), remote,
                    QStringLiteral("HEAD:") + merge};
            what = QStringLiteral("git push");
        }
        GitWrite::runAsync(this, root, args, kNetGitTimeoutMs,
                           [done, what, op, branch, refreshGit](const GitWrite::Result &r) {
                               if (!r.ok) {
                                   done(QJsonObject(), r.timedOut
                                                           ? QStringLiteral("%1 timed out after 60 s").arg(what)
                                                           : r.message(what));
                                   return;
                               }
                               refreshGit();
                               done(QJsonObject{{QStringLiteral("ok"), true},
                                                {QStringLiteral("branch"), branch},
                                                {QStringLiteral("output"),
                                                 (r.out + r.err).trimmed().left(8000)}},
                                    QString());
                           });
    };

    // ── Noter ───────────────────────────────────────────────────────────
    host.noterOp = [this](const QString &op, const QJsonObject &a, QString *err) -> QJsonObject {
        const QString root = NotesPanel::defaultNotesFolder();
        NotesPanel *np = findNoterPanel();
        const QString file = a.value(QLatin1String("file")).toString();
        QJsonObject out;
        auto refresh = [this]() {
            if (NotesPanel *p = findNoterPanel()) p->refreshFromDisk();
        };
        if (op == QLatin1String("search_notes")) {
            // No Noter folder yet: nothing to find, and a read never creates it.
            if (!QFileInfo(root).isDir()) {
                out[QStringLiteral("results")] = QJsonArray();
                out[QStringLiteral("truncated")] = false;
                return out;
            }
            NotesStorage storage(root);
            const QString q = a.value(QLatin1String("query")).toString().toLower();
            const QStringList terms = q.split(QRegularExpression(QStringLiteral("\\s+")),
                                              Qt::SkipEmptyParts);
            const int max = a.value(QLatin1String("max_results")).toInt(20);
            const QString trash = QDir(root).absoluteFilePath(QStringLiteral("Trash")) + QLatin1Char('/');
            QJsonArray hits;
            bool truncated = false;
            for (const QString &path : storage.listAllNotes()) {
                const QFileInfo fi(path);
                if (fi.absoluteFilePath().startsWith(trash) || fi.size() > 2 * 1024 * 1024) continue;
                // An open note's unsaved text is what the user sees: save it first.
                QString rerr;
                const QString text = NotesStorage::plainTextForSearch(storage.readNote(path, &rerr));
                const QString title = storage.displayTitleForFile(path);
                const QString hay = (title + QLatin1Char('\n') + text).toLower();
                if (terms.isEmpty() || !NotesPanel::matchesAllSearchTerms(hay, terms)) continue;
                if (hits.size() >= max) {
                    truncated = true;
                    break;
                }
                const int at = qMax(0, text.toLower().indexOf(terms.first()));
                QString snip = text.mid(qMax(0, at - 80), 200).simplified();
                hits.append(QJsonObject{{QStringLiteral("file"), QDir::toNativeSeparators(path)},
                                        {QStringLiteral("title"), title},
                                        {QStringLiteral("snippet"), snip},
                                        {QStringLiteral("modified_iso"),
                                         fi.lastModified().toString(Qt::ISODate)}});
            }
            out[QStringLiteral("results")] = hits;
            out[QStringLiteral("truncated")] = truncated;
            return out;
        }
        if (op == QLatin1String("export_formats")) {
            QJsonArray f{QStringLiteral("markdown")};
#ifdef NOTEPATRA_WITH_WEBENGINE
            f.append(QStringLiteral("pdf"));
#endif
            out[QStringLiteral("formats")] = f;
            return out;
        }
        if (op.contains(QLatin1String("reminder"))) {
            // A lookup (even for a bogus id on a card that is then denied)
            // must not create the Noter DB or mark Noter as in use: someone
            // who never used Noter has no reminders.
            if (!m_noterTodos && Config::instance().noterInUse != 1) {
                *err = QStringLiteral("no reminder with id %1 (list_reminders shows the ids)")
                           .arg(a.value(QLatin1String("id")).toString());
                return out;
            }
            ensureNoterReminderService();
            if (!m_noterTodos) {
                *err = QStringLiteral("reminder store unavailable");
                return out;
            }
            const QString id = a.value(QLatin1String("id")).toString();
            const TodoRow row = m_noterTodos->find(id);
            if (row.id.isEmpty() || !row.reminderAt.isValid()) {
                *err = QStringLiteral("no reminder with id %1 (list_reminders shows the ids)").arg(id);
                return out;
            }
            out[QStringLiteral("id")] = id;
            out[QStringLiteral("note_file")] = QDir::toNativeSeparators(row.sourceFile);
            out[QStringLiteral("note_title")] = row.meetingTitle.isEmpty()
                                                    ? QFileInfo(row.sourceFile).fileName()
                                                    : row.meetingTitle;
            out[QStringLiteral("due_iso")] = row.reminderAt.toUTC().toString(Qt::ISODate);
            if (op == QLatin1String("reminder_info")) return out;
            bool ok = false;
            if (op == QLatin1String("delete_reminder")) {
                ok = m_noterTodos->deleteRow(id);
                out[QStringLiteral("deleted")] = ok;
            } else if (op == QLatin1String("dismiss_reminder")) {
                ok = m_noterTodos->dismissReminder(id);
                out[QStringLiteral("dismissed")] = ok;
            } else {
                const QDateTime until = QDateTime::currentDateTime().addSecs(
                    qint64(a.value(QLatin1String("minutes")).toInt(10)) * 60);
                ok = m_noterTodos->snooze(id, until);
                out[QStringLiteral("snoozed_until_iso")] = until.toUTC().toString(Qt::ISODate);
            }
            if (!ok) *err = QStringLiteral("could not update the reminder");
            refresh();
            return out;
        }
        QString e;
        if (op == QLatin1String("rename_note")) {
            const QString title = a.value(QLatin1String("new_title")).toString();
            QString newAbs;
            if (np) {
                newAbs = np->renameNote(file, title, &e);
            } else {
                ensureNoterReminderService();
                NotesStorage storage(root);
                newAbs = NotesPanel::renameNoteFiles(storage, m_noterTodos, file, title, true, &e);
            }
            if (newAbs.isEmpty()) {
                *err = e.isEmpty() ? QStringLiteral("could not rename the note") : e;
                return out;
            }
            out[QStringLiteral("file")] = QDir::toNativeSeparators(newAbs);
            out[QStringLiteral("title")] = title.trimmed().left(200);
            if (!e.isEmpty()) out[QStringLiteral("warning")] = e;
            return out;
        }
        if (op == QLatin1String("trash_note")) {
            const QString t = np ? np->trashNote(file, &e) : NotesPanel::trashNoteFiles(file, root);
            if (t.isEmpty()) {
                *err = e.isEmpty() ? QStringLiteral("could not move the note to Trash") : e;
                return out;
            }
            out[QStringLiteral("file")] = QDir::toNativeSeparators(t);
            out[QStringLiteral("trashed")] = true;
            return out;
        }
        if (op == QLatin1String("restore_note")) {
            const QString r = np ? np->restoreNote(file, &e) : NotesPanel::restoreNoteFiles(file, root);
            if (r.isEmpty()) {
                *err = e.isEmpty() ? QStringLiteral("could not restore the note") : e;
                return out;
            }
            out[QStringLiteral("file")] = QDir::toNativeSeparators(r);
            return out;
        }
        if (op == QLatin1String("delete_note_permanently")) {
            const bool ok = np ? np->deleteNotePermanently(file, &e) : NotesPanel::deleteNoteFiles(file);
            if (!ok) {
                *err = e.isEmpty() ? QStringLiteral("could not delete the note") : e;
                return out;
            }
            out[QStringLiteral("deleted")] = true;
            return out;
        }
        if (op == QLatin1String("export_note")) {
            // Export what the user sees: an open note's unsaved edits first.
            if (np && np->currentNotePath() == file) np->saveCurrentNote();
            const QString path = a.value(QLatin1String("path")).toString();
            bool ok = false;
            if (a.value(QLatin1String("format")).toString() == QLatin1String("pdf")) {
                // Render beside the target, then replace it in one step.
                QTemporaryFile tmp(path + QStringLiteral(".npexport-XXXXXX"));
                if (tmp.open()) {
                    const QString tmpPath = tmp.fileName();
                    tmp.close();
                    tmp.setAutoRemove(true);
                    QFile::remove(tmpPath); // the exporter creates it afresh
                    ok = NoterExport::exportPdf(file, tmpPath, &e) &&
                         NpAtomic::replaceFile(tmpPath, path, &e);
                } else {
                    e = QStringLiteral("could not create a temporary file next to %1")
                            .arg(QDir::toNativeSeparators(path));
                }
            } else {
                ok = NoterExport::exportMarkdown(file, path, &e);
            }
            if (!ok) {
                *err = e.isEmpty() ? QStringLiteral("export failed") : e;
                return out;
            }
            out[QStringLiteral("path")] = QDir::toNativeSeparators(path);
            return out;
        }
        *err = QStringLiteral("unknown Noter op: %1").arg(op);
        return out;
    };

    // ── Settings ────────────────────────────────────────────────────────
    host.settingsOp = [this](const QString &op, const QJsonObject &a, QString *err) -> QJsonObject {
        Config &c = Config::instance();
        QJsonObject out;
        if (op == QLatin1String("get_preferences")) {
            QJsonObject p;
            p[QStringLiteral("font_size")] = c.fontSize;
            p[QStringLiteral("font_family")] = c.fontFamily;
            p[QStringLiteral("tab_width")] = c.tabWidth;
            p[QStringLiteral("use_spaces")] = !c.useTabs;
            p[QStringLiteral("auto_indent")] = c.autoIndent;
            p[QStringLiteral("word_wrap")] = c.wordWrap;
            p[QStringLiteral("show_line_numbers")] = c.showLineNumbers;
            p[QStringLiteral("show_indent_guides")] = c.showIndentGuides;
            p[QStringLiteral("highlight_current_line")] = c.highlightCurrentLine;
            p[QStringLiteral("show_edge")] = c.showEdge;
            p[QStringLiteral("edge_column")] = c.edgeColumn;
            p[QStringLiteral("caret_width")] = c.caretWidth;
            p[QStringLiteral("smooth_font")] = c.smoothFont;
            p[QStringLiteral("fold_style")] = c.foldStyle;
            p[QStringLiteral("show_bookmark_margin")] = c.showBookmarkMargin;
            p[QStringLiteral("show_document_rulers")] = c.showDocumentRulers;
            p[QStringLiteral("show_crosshair")] = c.showCrosshair;
            p[QStringLiteral("auto_complete")] = c.autoComplete;
            p[QStringLiteral("auto_complete_threshold")] = c.autoCompleteThreshold;
            p[QStringLiteral("default_eol")] = c.defaultEol;
            p[QStringLiteral("autosave_interval_sec")] = c.autoSaveIntervalSec;
            p[QStringLiteral("theme")] = c.theme;
            p[QStringLiteral("hide_toolbar")] = c.hideToolbar;
            p[QStringLiteral("tabs_closable")] = c.tabsClosable;
            p[QStringLiteral("double_click_to_close_tab")] = c.doubleClickToCloseTab;
            p[QStringLiteral("show_welcome_on_startup")] = c.showWelcomeOnStartup;
            out[QStringLiteral("preferences")] = p;
            return out;
        }
        if (op == QLatin1String("check_font")) {
            const QString fam = a.value(QLatin1String("value")).toString();
            bool found = false;
            for (const QString &f : QFontDatabase().families())
                if (f.compare(fam, Qt::CaseInsensitive) == 0) found = true;
            if (!found) *err = QStringLiteral("font family not installed: %1").arg(fam);
            return out;
        }
        if (op == QLatin1String("set_preference")) {
            const QString k = a.value(QLatin1String("key")).toString();
            const QJsonValue v = a.value(QLatin1String("value"));
            // Fail closed: only the keys the bridge's allow-list names.
            bool known = false;
            for (const QJsonValue &s : McpBridge::preferenceSchema())
                if (s.toObject().value(QLatin1String("key")).toString() == k) known = true;
            if (!known) {
                *err = QStringLiteral("unknown or protected preference key");
                return out;
            }
            if (k == QLatin1String("theme")) {
                // The Settings > Theme action, exactly as set_theme uses it.
                QAction *act = findChild<QAction *>(QStringLiteral("theme") + v.toString());
                if (!act) {
                    *err = QStringLiteral("no theme action for %1").arg(v.toString());
                    return out;
                }
                act->trigger();
            } else {
                if (k == QLatin1String("font_size")) c.fontSize = v.toInt();
                else if (k == QLatin1String("font_family")) c.fontFamily = v.toString();
                else if (k == QLatin1String("tab_width")) c.tabWidth = v.toInt();
                else if (k == QLatin1String("use_spaces")) c.useTabs = !v.toBool();
                else if (k == QLatin1String("auto_indent")) c.autoIndent = v.toBool();
                else if (k == QLatin1String("word_wrap")) c.wordWrap = v.toBool();
                else if (k == QLatin1String("show_line_numbers")) c.showLineNumbers = v.toBool();
                else if (k == QLatin1String("show_indent_guides")) c.showIndentGuides = v.toBool();
                else if (k == QLatin1String("highlight_current_line")) c.highlightCurrentLine = v.toBool();
                else if (k == QLatin1String("show_edge")) c.showEdge = v.toBool();
                else if (k == QLatin1String("edge_column")) c.edgeColumn = v.toInt();
                else if (k == QLatin1String("caret_width")) c.caretWidth = v.toInt();
                else if (k == QLatin1String("smooth_font")) c.smoothFont = v.toBool();
                else if (k == QLatin1String("fold_style")) c.foldStyle = v.toString();
                else if (k == QLatin1String("show_bookmark_margin")) c.showBookmarkMargin = v.toBool();
                else if (k == QLatin1String("show_document_rulers")) c.showDocumentRulers = v.toBool();
                else if (k == QLatin1String("show_crosshair")) c.showCrosshair = v.toBool();
                else if (k == QLatin1String("auto_complete")) c.autoComplete = v.toBool();
                else if (k == QLatin1String("auto_complete_threshold")) c.autoCompleteThreshold = v.toInt();
                else if (k == QLatin1String("default_eol")) c.defaultEol = v.toString();
                else if (k == QLatin1String("autosave_interval_sec")) {
                    c.autoSaveIntervalSec = v.toInt();
                    if (m_autoSaveTimer) m_autoSaveTimer->setInterval(qBound(1, v.toInt(), 300) * 1000);
                }
                else if (k == QLatin1String("hide_toolbar")) c.hideToolbar = v.toBool();
                else if (k == QLatin1String("tabs_closable")) c.tabsClosable = v.toBool();
                else if (k == QLatin1String("double_click_to_close_tab")) c.doubleClickToCloseTab = v.toBool();
                else if (k == QLatin1String("show_welcome_on_startup")) c.showWelcomeOnStartup = v.toBool();
                // What Preferences > OK does: save, then re-apply everywhere.
                c.save();
                applyConfigEverywhere();
            }
            out[QStringLiteral("key")] = k;
            out[QStringLiteral("value")] = v;
            out[QStringLiteral("changed")] = true;
            return out;
        }
        if (op == QLatin1String("set_workspace_folder")) {
            const QString p = a.value(QLatin1String("path")).toString();
            // Re-checked at Approve: the folder may have moved meanwhile.
            QString canon;
            if (const QString r = McpBridge::workspaceFolderRefusal(p, &canon); !r.isEmpty()) {
                *err = r;
                return out;
            }
            // File > Open Folder as Workspace, without its dialog.
            Config::instance().noteLastDir(canon);
            m_explorer->setRoot(canon);
            out[QStringLiteral("workspace")] = QDir::toNativeSeparators(workspaceFolder());
            return out;
        }
        *err = QStringLiteral("unknown settings op: %1").arg(op);
        return out;
    };

    // ── Data ────────────────────────────────────────────────────────────
    host.dataOp = [this](const QString &op, const QJsonObject &a, QString *err) -> QJsonObject {
        QJsonObject out;
        const QString name = a.value(QLatin1String("connection_name")).toString();
        DbConnections::Record rec;
        const bool found = !name.isEmpty() && DbConnections::findByName(name, &rec);
        if (op == QLatin1String("connection_info")) {
            if (!found) {
                *err = QStringLiteral("no connection named: %1").arg(name);
                return out;
            }
            return connectionJson(rec);
        }
        if (op == QLatin1String("classify_sql")) {
            const DbConnections::SqlVerdict v = DbConnections::classifyApprovedWrite(
                a.value(QLatin1String("sql")).toString(), rec.driver);
            out[QStringLiteral("single_statement")] = v.singleStatement;
            out[QStringLiteral("statement_count")] = v.statementCount;
            out[QStringLiteral("mutation")] = v.mutation;
            out[QStringLiteral("read_only")] = v.readOnly;
            out[QStringLiteral("hard_fail")] = v.hardFail;
            out[QStringLiteral("reason")] = v.hardFail ? v.hardReason : v.reason;
            return out;
        }
        if (op == QLatin1String("describe_table")) {
            if (!found) {
                *err = QStringLiteral("no connection named: %1").arg(name);
                return out;
            }
#ifndef NOTEPATRA_HAVE_DUCKDB
            if (rec.driver == QLatin1String("DUCKDB")) {
                *err = QStringLiteral("this connection requires the Full edition (DuckDB)");
                return out;
            }
#endif
            if (const QString missing = missingDatabaseFile(rec); !missing.isEmpty()) {
                *err = missing;
                return out;
            }
            QString e;
            const QVector<DbSchema::Table> tables = DbSchema::introspect(rec, &e);
            if (tables.isEmpty() && !e.isEmpty()) {
                *err = QStringLiteral("could not read the schema: %1").arg(scrubDbError(e, rec));
                return out;
            }
            // Matched against the catalog's own names; the argument never
            // reaches SQL text.
            const QString want = a.value(QLatin1String("table")).toString();
            QVector<DbSchema::Table> hits;
            for (const DbSchema::Table &t : tables)
                if (t.name == want || t.schema + QLatin1Char('.') + t.name == want) hits << t;
            if (hits.isEmpty())
                for (const DbSchema::Table &t : tables)
                    if (t.name.compare(want, Qt::CaseInsensitive) == 0) hits << t;
            if (hits.isEmpty()) {
                *err = QStringLiteral("no table named %1 on %2 (list_tables shows them)").arg(want, name);
                return out;
            }
            if (hits.size() > 1) {
                *err = QStringLiteral("%1 exists in more than one schema; give schema.table").arg(want);
                return out;
            }
            const DbSchema::Table &t = hits.first();
            QJsonArray cols;
            QJsonArray pk;
            for (const DbSchema::Column &c : t.columns) {
                QJsonObject o{{QStringLiteral("name"), c.name}, {QStringLiteral("type"), c.type}};
                if (t.keysKnown) o[QStringLiteral("primary_key")] = c.primaryKey;
                if (c.primaryKey) pk.append(c.name);
                cols.append(o);
            }
            out[QStringLiteral("connection_name")] = name;
            out[QStringLiteral("schema")] = t.schema;
            out[QStringLiteral("table")] = t.name;
            out[QStringLiteral("type")] = t.type;
            out[QStringLiteral("columns")] = cols;
            out[QStringLiteral("keys_known")] = t.keysKnown;
            if (t.keysKnown) out[QStringLiteral("primary_key")] = pk;
            return out;
        }
        if (op == QLatin1String("add_connection") || op == QLatin1String("update_connection")) {
            QVector<DbConnections::Record> all = DbConnections::loadAll();
            const QString orig = a.value(QLatin1String("original_name")).toString();
            int at = -1;
            for (int i = 0; i < all.size(); ++i)
                if (all.at(i).name == orig) at = i;
            const bool add = op == QLatin1String("add_connection");
            if (add == (at >= 0)) {
                *err = add ? QStringLiteral("a connection named '%1' already exists").arg(orig)
                           : QStringLiteral("no connection named: %1").arg(orig);
                return out;
            }
            DbConnections::Record r = add ? DbConnections::Record() : all.at(at);
            r.name = a.value(QLatin1String("name")).toString();
            r.driver = a.value(QLatin1String("driver")).toString();
            r.host = a.value(QLatin1String("host")).toString();
            r.port = a.value(QLatin1String("port")).toInt();
            r.database = a.value(QLatin1String("database")).toString();
            r.username = a.value(QLatin1String("username")).toString();
            if (add || a.value(QLatin1String("clear_secret")).toBool()) {
                r.password.clear();
                r.options.clear();
            }
            if (add) all.append(r);
            else all[at] = r;
            if (!DbConnections::saveAll(all)) {
                *err = QStringLiteral("could not save the connection list");
                return out;
            }
            out = connectionJson(r);
            out[QStringLiteral("saved")] = true;
            out[QStringLiteral("note")] = QLatin1String(kNoPasswordNote);
            return out;
        }
        if (op == QLatin1String("delete_connection")) {
            QVector<DbConnections::Record> all = DbConnections::loadAll();
            const QString n = a.value(QLatin1String("name")).toString();
            int removed = 0;
            for (int i = all.size() - 1; i >= 0; --i)
                if (all.at(i).name == n) {
                    all.remove(i);
                    ++removed;
                }
            if (!removed || !DbConnections::saveAll(all)) {
                *err = removed ? QStringLiteral("could not save the connection list")
                               : QStringLiteral("no connection named: %1").arg(n);
                return out;
            }
            out[QStringLiteral("deleted")] = n;
            return out;
        }
        if (op == QLatin1String("open_connections_dialog") ||
            op == QLatin1String("open_schema_browser")) {
            // Non-modal: the bridge and the approval card stay usable.
            QDialog *d = op == QLatin1String("open_connections_dialog")
                             ? static_cast<QDialog *>(new DbConnectionsDialog(this))
                             : static_cast<QDialog *>(new DbTreeDialog(this));
            d->setAttribute(Qt::WA_DeleteOnClose);
            d->setModal(false);
            d->show();
            d->raise();
            out[QStringLiteral("opened")] = true;
            return out;
        }
        *err = QStringLiteral("unknown data op: %1").arg(op);
        return out;
    };
    host.dataAsync = [this](const QString &op, const QJsonObject &a, McpEditorHost::Done done) {
        const QString name = a.value(QLatin1String("connection_name")).toString();
        DbConnections::Record rec;
        if (!DbConnections::findByName(name, &rec)) {
            done(QJsonObject(), QStringLiteral("no connection named: %1").arg(name));
            return;
        }
        if (rec.driver == QLatin1String("DUCKDB")) {
#ifdef NOTEPATRA_HAVE_DUCKDB
            rec.sandboxFilesystem = true; // engine-level file-read lockdown
#else
            done(QJsonObject(), QStringLiteral("this connection requires the Full edition (DuckDB)"));
            return;
#endif
        }
        if (op == QLatin1String("test_connection")) {
            if (const QString missing = missingDatabaseFile(rec); !missing.isEmpty()) {
                done(QJsonObject{{QStringLiteral("connection_name"), name},
                                 {QStringLiteral("ok"), false},
                                 {QStringLiteral("error"), missing}},
                     QString());
                return;
            }
            runGuarded(this, kDbTimeoutMs, [rec, name](QString *) {
                QString cname, e;
                const bool ok = DbConnections::open(rec, &cname, &e);
                if (ok) QSqlDatabase::removeDatabase(cname);
                QJsonObject r{{QStringLiteral("connection_name"), name},
                              {QStringLiteral("ok"), ok}};
                if (!ok) r[QStringLiteral("error")] = scrubDbError(e, rec);
                return r;
            }, [done, name](const QJsonObject &r, const QString &e) {
                if (e.isEmpty()) done(r, e);
                else done(QJsonObject{{QStringLiteral("connection_name"), name},
                                      {QStringLiteral("ok"), false},
                                      {QStringLiteral("error"), e}},
                          QString());
            }, QStringLiteral("timed out after %1 s").arg(kDbTimeoutMs / 1000));
            return;
        }
        if (op == QLatin1String("execute_sql")) {
            const QString sql = a.value(QLatin1String("sql")).toString();
            // Defense in depth: the gate again, at the moment of running.
            const DbConnections::SqlVerdict v = DbConnections::classifyApprovedWrite(sql, rec.driver);
            if (!v.singleStatement || v.hardFail || !v.mutation) {
                done(QJsonObject(), QStringLiteral("refused by the SQL gate: %1")
                                        .arg(v.hardFail ? v.hardReason : v.reason));
                return;
            }
            runGuarded(this, kSqlTimeoutMs, [rec, sql, name](QString *e) {
                const DbConnections::QueryResult qr =
                    DbConnections::runQuery(rec, sql, 200, /*allowMutation=*/true, nullptr);
                if (!qr.ok) {
                    *e = scrubDbError(qr.error.isEmpty() ? QStringLiteral("query failed") : qr.error, rec);
                    return QJsonObject();
                }
                QJsonArray cols, rows;
                for (const QString &c : qr.columns) cols.append(c);
                for (const auto &row : qr.rows) {
                    QJsonArray jr;
                    for (const QString &cell : row) jr.append(cell);
                    rows.append(jr);
                }
                QJsonObject r{{QStringLiteral("ok"), true},
                              {QStringLiteral("connection_name"), name},
                              {QStringLiteral("executed"), 1}};
                if (!qr.columns.isEmpty()) {
                    r[QStringLiteral("columns")] = cols;
                    r[QStringLiteral("rows")] = rows;
                    r[QStringLiteral("truncated")] = qr.truncated;
                }
                if (!qr.warning.isEmpty()) r[QStringLiteral("warning")] = scrubDbError(qr.warning, rec);
                return r;
            }, done, QStringLiteral("execute_sql timed out after %1 s (the statement may still "
                                    "complete on the server)").arg(kSqlTimeoutMs / 1000));
            return;
        }
        done(QJsonObject(), QStringLiteral("unknown data op: %1").arg(op));
    };

    // ── REST ────────────────────────────────────────────────────────────
    auto headersText = [](const QJsonArray &h) {
        QStringList lines;
        for (const QJsonValue &v : h)
            lines << v.toObject().value(QLatin1String("name")).toString() + QStringLiteral(": ") +
                         v.toObject().value(QLatin1String("value")).toString();
        return lines.join(QLatin1Char('\n'));
    };
    host.restOp = [headersText](const QString &op, const QJsonObject &a, QString *err) -> QJsonObject {
        QJsonObject out;
        const QString text = a.value(QLatin1String("text")).toString();
        if (op == QLatin1String("parse_http_file")) {
            const HttpFile::Document doc = HttpFile::parse(text);
            QJsonArray reqs;
            for (int i = 0; i < doc.requests.size(); ++i) {
                const HttpFile::Request &r = doc.requests.at(i);
                QJsonArray names;
                for (const auto &h : r.headers) names.append(h.first);
                QJsonObject o{{QStringLiteral("index"), i},
                              {QStringLiteral("name"), r.name},
                              {QStringLiteral("method"), r.method},
                              {QStringLiteral("url"), r.url},
                              {QStringLiteral("line"), r.requestLine + 1},
                              {QStringLiteral("start_line"), r.startLine + 1},
                              {QStringLiteral("end_line"), r.endLine + 1},
                              {QStringLiteral("header_names"), names},
                              {QStringLiteral("body_chars"), r.body.size()},
                              {QStringLiteral("variables"),
                               QJsonArray::fromStringList(HttpFile::referencedVariables(
                                   r.url + QLatin1Char('\n') + HttpFile::headersToText(r.headers) +
                                   QLatin1Char('\n') + r.body))}};
                if (!r.bodyFromFile.isEmpty()) o[QStringLiteral("body_from_file")] = r.bodyFromFile;
                reqs.append(o);
            }
            QJsonArray vars;
            for (const auto &v : doc.variables) vars.append(v.first);
            out[QStringLiteral("requests")] = reqs;
            out[QStringLiteral("file_variables")] = vars;
            return out;
        }
        if (op == QLatin1String("curl_to_request")) {
            const CurlConvert::ImportResult r = CurlConvert::fromCurl(text);
            if (!r.ok) {
                *err = r.error.isEmpty() ? QStringLiteral("not a curl command") : r.error;
                return out;
            }
            QJsonArray headers;
            for (const auto &h : r.request.headers)
                headers.append(QJsonObject{{QStringLiteral("name"), h.first},
                                           {QStringLiteral("value"), h.second}});
            out[QStringLiteral("method")] = r.request.method;
            out[QStringLiteral("url")] = r.request.url;
            out[QStringLiteral("headers")] = headers;
            out[QStringLiteral("body")] = r.request.body;
            out[QStringLiteral("notes")] = QJsonArray::fromStringList(r.notes);
            return out;
        }
        const QJsonArray hdrs = a.value(QLatin1String("headers")).toArray();
        if (op == QLatin1String("request_to_curl")) {
            CurlConvert::Request r;
            r.method = a.value(QLatin1String("method")).toString();
            r.url = a.value(QLatin1String("url")).toString();
            for (const QJsonValue &v : hdrs)
                r.headers.append({v.toObject().value(QLatin1String("name")).toString(),
                                  v.toObject().value(QLatin1String("value")).toString()});
            r.body = a.value(QLatin1String("body")).toString();
            out[QStringLiteral("text")] = CurlConvert::toCurl(r);
            return out;
        }
        if (op == QLatin1String("resolve_request")) {
            const RestClient::Resolved r = RestClient::resolve(
                text, a.value(QLatin1String("line")).toInt(),
                a.value(QLatin1String("file_path")).toString(),
                a.value(QLatin1String("method")).toString(), a.value(QLatin1String("url")).toString(),
                headersText(hdrs), a.value(QLatin1String("body")).toString());
            if (!r.error.isEmpty()) {
                *err = QStringLiteral("not sent: %1").arg(r.error);
                return out;
            }
            QJsonArray headers;
            for (const auto &h : r.headers)
                headers.append(QJsonObject{{QStringLiteral("name"), h.first},
                                           {QStringLiteral("value"), h.second}});
            out[QStringLiteral("method")] = r.method;
            out[QStringLiteral("url")] = r.url;
            out[QStringLiteral("headers")] = headers;
            out[QStringLiteral("body")] = r.body;
            out[QStringLiteral("name")] = r.name;
            return out;
        }
        *err = QStringLiteral("unknown REST op: %1").arg(op);
        return out;
    };
    host.restAsync = [this, headersText](const QString &, const QJsonObject &a,
                                         McpEditorHost::Done done) {
        // The REST Client tab the menu opens (reused when one is open).
        RestClient *rest = qobject_cast<RestClient *>(m_tabs->currentWidget());
        for (int i = m_tabs->count() - 1; !rest && i >= 0; --i)
            rest = qobject_cast<RestClient *>(m_tabs->widget(i));
        if (!rest && m_newRestTab) rest = m_newRestTab();
        if (!rest) {
            done(QJsonObject(), QStringLiteral("could not open the REST Client"));
            return;
        }
        m_tabs->setCurrentWidget(rest);
        auto conn = std::make_shared<QMetaObject::Connection>();
        auto gone = std::make_shared<QMetaObject::Connection>();
        QPointer<RestClient> guard(rest);
        *conn = connect(rest, &RestClient::requestFinished, this, [guard, conn, gone, done]() {
            QObject::disconnect(*conn);
            QObject::disconnect(*gone);
            if (!guard) {
                done(QJsonObject(), QStringLiteral("the REST Client closed"));
                return;
            }
            done(guard->lastResponseJson(kRestBodyCap), QString());
        });
        // Closing the REST tab mid-request aborts it without requestFinished.
        *gone = connect(rest, &QObject::destroyed, this, [conn, gone, done]() {
            QObject::disconnect(*conn);
            QObject::disconnect(*gone);
            done(QJsonObject(), QStringLiteral("the REST Client tab was closed before the "
                                               "response arrived"));
        });
        QString e;
        if (!rest->sendForMcp(a.value(QLatin1String("text")).toString(),
                              a.value(QLatin1String("line")).toInt(),
                              a.value(QLatin1String("file_path")).toString(),
                              a.value(QLatin1String("method")).toString(),
                              a.value(QLatin1String("url")).toString(),
                              headersText(a.value(QLatin1String("headers")).toArray()),
                              a.value(QLatin1String("body")).toString(), &e)) {
            QObject::disconnect(*conn);
            QObject::disconnect(*gone);
            done(QJsonObject(), QStringLiteral("not sent: %1").arg(e));
        }
    };

    // ── Leftovers ───────────────────────────────────────────────────────
    host.miscOp = [this](const QString &op, const QJsonObject &a, QString *err) -> QJsonObject {
        QJsonObject out;
        if (op == QLatin1String("clear_recent_files")) {
            // File > Recent Files > Clear Recent Files.
            const int n = Config::instance().recentFiles.size();
            Config::instance().recentFiles.clear();
            Config::instance().save();
            updateRecentMenu();
            out[QStringLiteral("cleared")] = n;
            return out;
        }
        if (op == QLatin1String("hex_bytes")) {
            const qint64 offset = a.value(QLatin1String("offset")).toInt();
            const int length = a.value(QLatin1String("length")).toInt();
            if (a.contains(QLatin1String("text"))) {
                const QByteArray all = a.value(QLatin1String("text")).toString().toUtf8();
                out[QStringLiteral("base64")] = QString::fromLatin1(all.mid(int(offset), length).toBase64());
                out[QStringLiteral("total_size")] = double(all.size());
                out[QStringLiteral("source")] = QStringLiteral("tab text (UTF-8)");
                return out;
            }
            QString path = a.value(QLatin1String("path")).toString();
            if (a.value(QLatin1String("confine")).toBool()) {
                // Same sandbox as run_sql's csv_path: inside the workspace,
                // symlink escapes and the credential deny-list refused.
                QString canon;
                if (!AiTools::resolveSafePath(path, workspaceFolder(), &canon, nullptr) ||
                    !QFileInfo(canon).isFile()) {
                    *err = QStringLiteral("path is not an allowed file (it must be an existing "
                                          "file inside the workspace folder)");
                    return out;
                }
                path = canon;
            }
            QFile f(path);
            if (!f.open(QIODevice::ReadOnly) || !QFileInfo(path).isFile()) {
                *err = QStringLiteral("cannot read %1").arg(QDir::toNativeSeparators(path));
                return out;
            }
            out[QStringLiteral("total_size")] = double(f.size());
            if (offset > 0 && !f.seek(offset)) {
                *err = QStringLiteral("offset is past the end of the file");
                return out;
            }
            out[QStringLiteral("base64")] = QString::fromLatin1(f.read(length).toBase64());
            out[QStringLiteral("source")] = QDir::toNativeSeparators(path);
            return out;
        }
        *err = QStringLiteral("unknown op: %1").arg(op);
        return out;
    };
}

QJsonObject MainWindow::mcpQueryCsvSqlite(const QString &csv, const QString &sql, QString *err) {
    QString cname, loadErr;
    bool loadTruncated = false;
    if (!CsvAnalyst::loadIntoSqlite(csv, 250000, &cname, &loadTruncated, &loadErr)) {
        if (err) *err = QStringLiteral("CSV load failed: %1").arg(loadErr);
        return QJsonObject();
    }
    QJsonObject out;
    {
        QSqlDatabase db = QSqlDatabase::database(cname);
        QSqlQuery setup(db);
        // Same table name as the Full (DuckDB) path, then lock the copy.
        setup.exec(QStringLiteral("CREATE VIEW data AS SELECT * FROM csv"));
        setup.exec(QStringLiteral("PRAGMA query_only = ON"));
        QSqlQuery q(db);
        if (!q.exec(sql)) {
            if (err) *err = QStringLiteral("SQL error: %1").arg(q.lastError().text());
        } else {
            const QSqlRecord rec = q.record();
            QJsonArray cols, rows;
            for (int i = 0; i < rec.count(); ++i) cols.append(rec.fieldName(i));
            bool truncated = false;
            while (q.next()) {
                if (rows.size() >= 200) {
                    truncated = true;
                    break;
                }
                QJsonArray row;
                for (int i = 0; i < rec.count(); ++i)
                    row.append(q.value(i).isNull() ? QString() : q.value(i).toString());
                rows.append(row);
            }
            out[QStringLiteral("columns")] = cols;
            out[QStringLiteral("rows")] = rows;
            out[QStringLiteral("truncated")] = truncated || loadTruncated;
            out[QStringLiteral("engine")] = QStringLiteral("sqlite");
        }
    }
    QSqlDatabase::removeDatabase(cname);
    return out;
}
