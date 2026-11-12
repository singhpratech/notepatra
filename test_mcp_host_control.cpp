// SPDX-License-Identifier: GPL-3.0-or-later
//
// v0.1.133 Phase 3 MCP tools against the REAL MainWindow host: a socket
// client talks to the window's bridge and clicks the real approval card.
// Git runs against a temp repository with a repo-local identity (global and
// system git config disabled) and a local bare repository as the remote;
// Noter, preferences and connections live under an isolated HOME; SQL is
// SQLite in a temp folder; REST talks to a QTcpServer on 127.0.0.1. Never
// the real network, a real database server or a real remote.

#include "mainwindow.h"
#include "config.h"
#include "editor.h"
#include "mcp_bridge.h"
#include "notes.h"
#include "tabmanager.h"

#include <QApplication>
#include <QDialog>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QFrame>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QLocalSocket>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QProcess>
#include <QPushButton>
#include <QStandardPaths>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTemporaryDir>
#include <QThread>
#include <QTimer>

#include "diagram/diagram_editor.h"
#include "dbconnections.h"
#include "rest_history.h"

#include <QTextEdit>

#include <cstdio>
#include <functional>

static int g_pass = 0, g_fail = 0;
#define EXPECT(label, cond) \
    do { if (cond) { ++g_pass; std::printf("  [PASS] %s\n", label); } \
         else      { ++g_fail; std::printf("  [FAIL] %s\n", label); } \
         std::fflush(stdout); } while (0)

namespace {
MainWindow *g_mw = nullptr;
QLocalSocket *g_sock = nullptr;
int g_id = 0;
QString g_cardDesc, g_cardWarn, g_cardPreview;

void pump(int ms) {
    QElapsedTimer t;
    t.start();
    while (t.elapsed() < ms) {
        QApplication::processEvents(QEventLoop::AllEvents, 10);
        QThread::msleep(2);
    }
}

QJsonObject readObj(int timeoutMs = 20000) {
    QElapsedTimer t;
    t.start();
    while (!g_sock->canReadLine() && t.elapsed() < timeoutMs) pump(5);
    if (!g_sock->canReadLine()) return QJsonObject{{"timeout", true}};
    const QByteArray line = g_sock->readLine().trimmed();
    if (qEnvironmentVariableIsSet("NP_MCP_TEST_VERBOSE"))
        std::printf("      <- %s\n", line.left(300).constData());
    return QJsonDocument::fromJson(line).object();
}

void send(const QString &verb, const QJsonObject &args) {
    g_sock->write(QJsonDocument(QJsonObject{{"id", ++g_id}, {"verb", verb}, {"args", args}})
                      .toJson(QJsonDocument::Compact) + '\n');
    g_sock->flush();
}

QJsonObject call(const QString &verb, const QJsonObject &args = {}) {
    send(verb, args);
    return readObj();
}

QFrame *liveCard(int timeoutMs = 5000) {
    QElapsedTimer t;
    t.start();
    while (t.elapsed() < timeoutMs) {
        for (QFrame *c : g_mw->findChildren<QFrame *>(QStringLiteral("mcpApprovalCard")))
            if (c->isVisible()) return c;
        pump(10);
    }
    return nullptr;
}

// whileWaiting runs after the card is up and before the click: what a user
// (or another tool call) does while the card waits.
QJsonObject callCard(const QString &verb, const QJsonObject &args, bool approve,
                     const std::function<void()> &whileWaiting = {}) {
    send(verb, args);
    QFrame *card = liveCard();
    if (!card) return QJsonObject{{"no_card", true}, {"early", readObj(300)}};
    if (whileWaiting) {
        whileWaiting();
        pump(50);
    }
    auto text = [card](const char *name) {
        if (auto *l = card->findChild<QLabel *>(QLatin1String(name))) return l->text();
        if (auto *p = card->findChild<QPlainTextEdit *>(QLatin1String(name))) return p->toPlainText();
        return QString();
    };
    g_cardDesc = text("mcpApprovalDesc");
    g_cardWarn = text("mcpApprovalWarning");
    g_cardPreview = text("mcpApprovalPreview") + text("mcpApprovalPreviewFull");
    card->findChild<QPushButton *>(approve ? QStringLiteral("mcpApproveBtn")
                                           : QStringLiteral("mcpDenyBtn"))->click();
    const QJsonObject r = readObj(70000);
    pump(50);
    return r;
}

bool ok(const QJsonObject &r) { return r.value(QLatin1String("ok")).toBool(); }
QString errOf(const QJsonObject &r) { return r.value(QLatin1String("error")).toString(); }
QJsonObject res(const QJsonObject &r) { return r.value(QLatin1String("result")).toObject(); }

bool writeFile(const QString &path, const QByteArray &bytes) {
    QDir().mkpath(QFileInfo(path).absolutePath());
    QFile f(path);
    return f.open(QIODevice::WriteOnly | QIODevice::Truncate) && f.write(bytes) == bytes.size();
}
QByteArray readFile(const QString &path) {
    QFile f(path);
    return f.open(QIODevice::ReadOnly) ? f.readAll() : QByteArray();
}

QString g_repo;
QString git(const QStringList &args, bool *okOut = nullptr, const QString &dir = QString()) {
    QProcess p;
    p.setWorkingDirectory(dir.isEmpty() ? g_repo : dir);
    p.start(QStringLiteral("git"), args);
    p.waitForFinished(20000);
    if (okOut) *okOut = p.exitStatus() == QProcess::NormalExit && p.exitCode() == 0;
    return QString::fromUtf8(p.readAllStandardOutput() + p.readAllStandardError()).trimmed();
}

Editor *editorById(qint64 tid) {
    auto *tm = g_mw->findChild<TabManager *>();
    for (int i = 0; i < tm->count(); ++i)
        if (tm->widget(i)->property("npMcpTabId").toLongLong() == tid) return tm->editorAt(i);
    return nullptr;
}
qint64 openFile(const QString &path) {
    return qint64(res(call("open_file", {{"path", path}})).value("tab_id").toDouble(-1));
}
} // namespace

int main(int argc, char *argv[]) {
    QTemporaryDir home;
    qputenv("HOME", home.path().toUtf8());
    qputenv("XDG_CONFIG_HOME", (home.path() + "/.config").toUtf8());
    qputenv("XDG_DATA_HOME", (home.path() + "/.local/share").toUtf8());
#ifdef Q_OS_WIN
    qputenv("APPDATA", home.path().toUtf8());
    qputenv("USERPROFILE", home.path().toUtf8());
#endif
    // Git sees only the repo-local config this test writes.
    qputenv("GIT_CONFIG_GLOBAL", (home.path() + "/no-such-gitconfig").toUtf8());
    qputenv("GIT_CONFIG_NOSYSTEM", "1");
    qputenv("QT_QPA_PLATFORM", "offscreen");
    QStandardPaths::setTestModeEnabled(true);
    QApplication app(argc, argv);
    std::printf("=== test_mcp_host_control ===\n");

    QDir().mkpath(Config::appConfigDir());
    writeFile(Config::appConfigDir() + "/config.json",
              "{\"showWelcomeOnStartup\": false, \"autoSaveIntervalSec\": 300}\n");
    QTemporaryDir work;
    g_repo = QFileInfo(work.path()).canonicalFilePath() + "/repo";
    const QString bare = QFileInfo(work.path()).canonicalFilePath() + "/remote.git";
    QDir().mkpath(g_repo);

    g_mw = new MainWindow();
    g_mw->resize(1000, 700);
    g_mw->show();
    g_mw->runStartupNow();
    pump(100);

    QLocalSocket sock;
    g_sock = &sock;
    sock.connectToServer(McpBridge::defaultServerName());
    QElapsedTimer t;
    t.start();
    while (sock.state() != QLocalSocket::ConnectedState && t.elapsed() < 5000) pump(10);
    EXPECT("connected to the window's MCP bridge", sock.state() == QLocalSocket::ConnectedState);
    if (sock.state() != QLocalSocket::ConnectedState) return 1;
    readObj();

    QJsonObject r;
    // ── R13: a reminder lookup never starts Noter for someone who never used it ──
    pump(1700); // past the 1.5 s startup probe
    EXPECT("fixture: Noter not in use, no reminder DB",
           Config::instance().noterInUse != 1 && !QFileInfo::exists(NotesPanel::todosDbPath()));
    r = call("delete_reminder", {{"id", "bogus-id"}});
    EXPECT("R13: delete_reminder with a bogus id is refused as unknown",
           !ok(r) && errOf(r).contains("no reminder"));
    r = call("snooze_reminder", {{"id", "bogus-id"}, {"minutes", 5}});
    EXPECT("R13: ...and creates no Noter DB and does not mark Noter in use",
           !ok(r) && !QFileInfo::exists(NotesPanel::todosDbPath()) &&
               Config::instance().noterInUse != 1);
    r = call("search_notes", {{"query", "anything"}});
    EXPECT("R13: search_notes before Noter exists finds nothing and creates no folder",
           ok(r) && res(r).value("results").toArray().isEmpty() &&
               !QFileInfo::exists(NotesPanel::defaultNotesFolder()));
    EXPECT("R14: a negative first-launch probe is not saved as 'Noter unused'",
           Config::instance().noterInUse == -1 &&
               !readFile(Config::appConfigDir() + "/config.json").contains("\"noterInUse\": 0"));

    // ── workspace folder ──
    // QDir::homePath(), not `home`: on Windows Qt takes the profile folder from
    // the process token, so the env vars above do not move it.
    r = call("set_workspace_folder", {{"path", QDir::homePath()}});
    EXPECT("set_workspace_folder refuses $HOME before a card", !ok(r) && errOf(r).contains("home folder"));
    r = callCard("set_workspace_folder", {{"path", g_repo}}, true);
    EXPECT("set_workspace_folder approved sets the Explorer workspace",
           ok(r) && QFileInfo(res(r).value("workspace").toString()).canonicalFilePath() == g_repo);

    // ── git ──
    bool haveGit = false;
    git({"--version"}, &haveGit);
    if (!haveGit) {
        std::printf("  [SKIP] git not available\n");
    } else {
        git({"init", "-q"});
        git({"checkout", "-q", "-b", "main"});
        git({"config", "user.name", "Test User"});
        git({"config", "user.email", "test@example.invalid"});
        git({"config", "commit.gpgsign", "false"});
        writeFile(g_repo + "/a.txt", "one\ntwo\nthree\n");
        writeFile(g_repo + "/c.txt", "base\n");
        git({"add", "."});
        git({"commit", "-q", "-m", "init"});
        git({"init", "-q", "--bare", bare}, nullptr, work.path());
        git({"remote", "add", "origin", bare});
        bool pushed = false;
        git({"push", "-q", "-u", "origin", "main"}, &pushed);
        EXPECT("fixture: bare remote with upstream", pushed);

        writeFile(g_repo + "/a.txt", "one\nTWO\nthree\n");
        writeFile(g_repo + "/b.txt", "new\n");
        writeFile(g_repo + "/.env", "TOKEN=x\n");
        r = call("git_stage", {{"paths", QJsonArray{".env"}}});
        EXPECT("git_stage .env refused before a card", !ok(r) && errOf(r).contains("deny-list"));
        r = call("git_stage", {{"paths", QJsonArray{"--all"}}});
        EXPECT("git_stage --all refused (option injection)", !ok(r));
        r = callCard("git_stage", {{"paths", QJsonArray{"a.txt", "b.txt"}}}, false);
        EXPECT("git_stage Deny stages nothing", errOf(r) == "denied by user" &&
                                                    git({"diff", "--cached", "--name-only"}).isEmpty());
        r = callCard("git_stage", {{"paths", QJsonArray{"a.txt", "b.txt"}}}, true);
        EXPECT("git_stage Approve stages exactly those files",
               ok(r) && git({"diff", "--cached", "--name-only"}) == "a.txt\nb.txt");
        r = call("git_diff", {{"staged", true}});
        EXPECT("git_diff staged:true shows the staged change", ok(r) &&
               res(r).value("output").toString().contains("+TWO"));
        r = callCard("git_commit", {{"message", "mcp: two files"}}, true);
        EXPECT("git_commit card lists files; commit made",
               ok(r) && g_cardPreview.contains("- a.txt") && g_cardPreview.contains("mcp: two files") &&
                   git({"log", "-1", "--format=%s"}) == "mcp: two files");
        r = call("git_commit", {{"message", "again"}});
        EXPECT("git_commit with nothing staged refused (no commit -a)",
               !ok(r) && errOf(r).contains("nothing is staged"));
        r = callCard("git_push", {}, true);
        EXPECT("git_push to the local bare remote",
               ok(r) && git({"rev-parse", "HEAD"}) == git({"rev-parse", "main"}, nullptr, bare));
        r = callCard("git_fetch", {}, true);
        EXPECT("git_fetch", ok(r));
        r = callCard("git_pull", {}, true);
        EXPECT("git_pull --ff-only (up to date)", ok(r));

        // discard
        writeFile(g_repo + "/a.txt", "changed\n");
        r = callCard("git_discard", {{"paths", QJsonArray{"a.txt"}}}, false);
        EXPECT("git_discard card warns; Deny keeps the change",
               g_cardWarn.contains("Data loss") && readFile(g_repo + "/a.txt") == "changed\n");
        r = callCard("git_discard", {{"paths", QJsonArray{"a.txt"}}}, true);
        EXPECT("git_discard Approve restores the committed text",
               ok(r) && readFile(g_repo + "/a.txt") == "one\nTWO\nthree\n");
        r = call("git_discard", {{"paths", QJsonArray{".env"}}});
        EXPECT("git_discard never touches an untracked secret", !ok(r) && QFileInfo::exists(g_repo + "/.env"));

        // R6: a workspace switch approved while a git card waits moves the
        // workspace, never the write.
        {
            const QString repo2 = QFileInfo(work.path()).canonicalFilePath() + "/repo2";
            QDir().mkpath(repo2);
            git({"init", "-q"}, nullptr, repo2);
            git({"config", "user.name", "Test User"}, nullptr, repo2);
            git({"config", "user.email", "test@example.invalid"}, nullptr, repo2);
            git({"config", "commit.gpgsign", "false"}, nullptr, repo2);
            writeFile(repo2 + "/a.txt", "repo2 committed\n");
            git({"add", "."}, nullptr, repo2);
            git({"commit", "-q", "-m", "init"}, nullptr, repo2);
            writeFile(repo2 + "/a.txt", "repo2 local work\n");
            writeFile(g_repo + "/a.txt", "repo1 local work\n");
            send("set_workspace_folder", {{"path", repo2}});
            QFrame *wsCard = liveCard();
            send("git_discard", {{"paths", QJsonArray{"a.txt"}}}); // card text built now, for repo 1
            pump(100);
            if (wsCard) wsCard->findChild<QPushButton *>("mcpApproveBtn")->click();
            const QJsonObject ws = readObj();
            pump(100);
            QFrame *gitCard = liveCard();
            const QString gitDesc = gitCard ? gitCard->findChild<QLabel *>("mcpApprovalDesc")->text() : QString();
            if (gitCard) gitCard->findChild<QPushButton *>("mcpApproveBtn")->click();
            r = readObj();
            pump(50);
            EXPECT("R6: the discard card named repo 1; the workspace moved to repo 2 meanwhile",
                   ok(ws) && gitDesc.contains(QDir::toNativeSeparators(g_repo)));
            EXPECT("R6: the approved discard is refused, nothing discarded in either repository",
                   !ok(r) && errOf(r).contains("workspace changed while the card was waiting") &&
                       readFile(repo2 + "/a.txt") == "repo2 local work\n" &&
                       readFile(g_repo + "/a.txt") == "repo1 local work\n");
            r = callCard("set_workspace_folder", {{"path", g_repo}}, true);
            git({"checkout", "--", "a.txt"});
        }

        // R16: client paths are literal, never globs.
        writeFile(g_repo + "/a[1].txt", "bracket\n");
        writeFile(g_repo + "/a1.txt", "plain\n");
        r = callCard("git_stage", {{"paths", QJsonArray{"a[1].txt"}}}, true);
        EXPECT("R16: staging a[1].txt stages only that file, not a1.txt",
               ok(r) && git({"diff", "--cached", "--name-only"}) == "a[1].txt");
        git({"reset", "-q"});
        QFile::remove(g_repo + "/a[1].txt");
        QFile::remove(g_repo + "/a1.txt");

        // hunks
        writeFile(g_repo + "/a.txt", "one\nTWO\nthree\nfour\n");
        r = call("list_hunks", {{"path", "a.txt"}});
        const QJsonArray hunks = res(r).value("hunks").toArray();
        EXPECT("list_hunks finds the change", ok(r) && hunks.size() == 1 &&
                                                  hunks.at(0).toObject().value("diff").toString().contains("+four"));
        r = callCard("git_hunk", {{"path", "a.txt"}, {"hunk_index", 0}, {"action", "stage"}}, true);
        EXPECT("git_hunk stage puts the hunk in the index",
               ok(r) && git({"diff", "--cached"}).contains("+four"));
        git({"reset", "-q", "HEAD", "--", "a.txt"});
        // R15: same header, different lines while the card waits.
        writeFile(g_repo + "/a.txt", "one\nTWO\nthree\nfour\n");
        r = callCard("git_hunk", {{"path", "a.txt"}, {"hunk_index", 0}, {"action", "stage"}}, true,
                     []() { writeFile(g_repo + "/a.txt", "one\nTWO\nthree\nFIVE\n"); });
        EXPECT("R15: a hunk whose lines changed under the same header is refused",
               !ok(r) && errOf(r).contains("hunk changed") && git({"diff", "--cached"}).isEmpty());
        r = call("git_unstage", {{"paths", QJsonArray{"a.txt"}}});
        EXPECT("git_unstage of a file that is not staged is refused", !ok(r));
        git({"checkout", "--", "a.txt"});

        // stash
        writeFile(g_repo + "/a.txt", "stashed\n");
        r = callCard("git_stash", {{"action", "push"}, {"message", "wip"}}, true);
        EXPECT("git_stash push", ok(r) && readFile(g_repo + "/a.txt") == "one\nTWO\nthree\n");
        r = callCard("git_stash", {{"action", "pop"}}, true);
        EXPECT("git_stash pop", ok(r) && readFile(g_repo + "/a.txt") == "stashed\n");

        // branches and checkout
        r = call("git_checkout", {{"name", "nope"}});
        EXPECT("git_checkout of a missing branch refused", !ok(r));
        git({"checkout", "--", "a.txt"});
        git({"stash", "drop", "-q"});
        r = callCard("git_branch_create", {{"name", "topic"}, {"switch", true}}, true);
        EXPECT("git_branch_create + switch", ok(r) && git({"branch", "--show-current"}) == "topic");
        writeFile(g_repo + "/a.txt", "topic version\n");
        git({"commit", "-q", "-am", "topic edit"});
        r = callCard("git_checkout", {{"name", "main"}}, true);
        EXPECT("git_checkout main", ok(r) && git({"branch", "--show-current"}) == "main");
        writeFile(g_repo + "/a.txt", "local edit\n");
        r = callCard("git_checkout", {{"name", "topic"}}, true);
        EXPECT("git_checkout refuses to overwrite local changes (git's own message)",
               !ok(r) && errOf(r).contains("overwritten") && readFile(g_repo + "/a.txt") == "local edit\n");
        git({"checkout", "--", "a.txt"});
#ifndef Q_OS_WIN
        {
            // An open tab with UNSAVED edits whose file changes on checkout:
            // the watcher asks, and "keep my version" keeps every edit.
            const qint64 tid = openFile(g_repo + "/a.txt");
            Editor *ed = editorById(tid);
            if (ed) ed->setText("unsaved edit\n");
            int prompts = 0;
            QTimer keep;
            QObject::connect(&keep, &QTimer::timeout, [&prompts]() {
                for (QWidget *w : QApplication::topLevelWidgets())
                    if (auto *mb = qobject_cast<QMessageBox *>(w); mb && mb->isVisible()) {
                        ++prompts;
                        mb->done(QMessageBox::No);
                    }
            });
            keep.start(50);
            r = callCard("git_checkout", {{"name", "topic"}}, true);
            pump(2500);
            keep.stop();
            EXPECT("checkout with an open, unsaved tab: the file-watcher path keeps the edits",
                   ok(r) && ed && ed->text() == "unsaved edit\n" && ed->isModified() &&
                       readFile(g_repo + "/a.txt") == "topic version\n");
            std::printf("      (watcher prompts answered: %d)\n", prompts);
            if (ed) {
                ed->setModified(false);
                call("close_tab", {{"tab_id", tid}});
            }
            git({"checkout", "-q", "main"});
        }
#endif
        // conflicts
        git({"checkout", "-q", "-b", "side"});
        writeFile(g_repo + "/c.txt", "theirs\n");
        git({"commit", "-q", "-am", "side"});
        git({"checkout", "-q", "main"});
        writeFile(g_repo + "/c.txt", "ours\n");
        git({"commit", "-q", "-am", "main side"});
        git({"merge", "-q", "side"});
        r = call("list_conflicts");
        EXPECT("list_conflicts lists the conflicted file",
               ok(r) && res(r).value("files").toArray().at(0).toObject().value("path").toString() == "c.txt");
        r = call("list_conflicts", {{"path", "c.txt"}});
        EXPECT("list_conflicts path gives the region", ok(r) && res(r).value("regions").toArray().size() == 1);
        // R15: the same region, other text, while the card waits.
        const QByteArray conflicted = readFile(g_repo + "/c.txt");
        r = callCard("resolve_conflict", {{"path", "c.txt"}, {"region", 0}, {"take", "theirs"}}, true,
                     [&]() {
                         QByteArray changed = conflicted;
                         changed.replace("theirs\n", "edited theirs\n");
                         writeFile(g_repo + "/c.txt", changed);
                     });
        EXPECT("R15: a conflict region whose text changed (same start line) is refused",
               !ok(r) && errOf(r).contains("conflict regions changed") &&
                   readFile(g_repo + "/c.txt").contains("edited theirs"));
        // The refused attempt opened c.txt in a tab: close it (unmodified, no
        // card) before the file is put back, so no file-watcher prompt fires.
        {
            auto *tm = g_mw->findChild<TabManager *>();
            for (int i = tm->count() - 1; i >= 0; --i)
                if (Editor *ced = tm->editorAt(i))
                    if (QFileInfo(ced->filePath()).fileName() == "c.txt")
                        call("close_tab", {{"tab_index", i}});
        }
        writeFile(g_repo + "/c.txt", conflicted);
        r = callCard("resolve_conflict", {{"path", "c.txt"}, {"region", 0}, {"take", "theirs"}}, true);
        EXPECT("resolve_conflict theirs writes the file, markers gone",
               ok(r) && readFile(g_repo + "/c.txt") == "theirs\n" && res(r).value("remaining").toInt() == 0);
        git({"add", "c.txt"});
        git({"commit", "-q", "-m", "merge"});
    }

    // ── Noter ──
    r = callCard("create_note", {{"title", "Planning sync"}, {"body", "budget review with finance"}}, true);
    const QString noteFile = res(r).value("file").toString();
    EXPECT("fixture: a note", ok(r) && QFileInfo::exists(noteFile));
    r = call("search_notes", {{"query", "FINANCE budget"}});
    EXPECT("search_notes matches body terms", ok(r) && res(r).value("results").toArray().size() == 1);
    r = call("search_notes", {{"query", "nothing-like-this"}});
    EXPECT("search_notes zero hits is a success", ok(r) && res(r).value("results").toArray().isEmpty());
    call("open_note", {{"file", noteFile}});
    pump(100);
    r = callCard("rename_note", {{"file", noteFile}, {"new_title", "Budget planning"}}, true);
    const QString renamed = res(r).value("file").toString();
    NotesPanel *np = g_mw->findChild<NotesPanel *>();
    EXPECT("rename_note renames the file and the open note follows it",
           ok(r) && QFileInfo::exists(renamed) && !QFileInfo::exists(noteFile) && np &&
               QFileInfo(np->currentNotePath()).canonicalFilePath() == QFileInfo(renamed).canonicalFilePath());
    if (np) np->saveCurrentNote(); // the 5 s autosave's own call
    pump(50);
    EXPECT("an autosave after the rename never recreates the old path", !QFileInfo::exists(noteFile));
    // R23: typing not yet autosaved goes to Trash with the note.
    if (np)
        if (auto *te = np->findChild<QTextEdit *>("noterEditor")) {
            QTextCursor c = te->textCursor();
            c.movePosition(QTextCursor::End);
            te->setTextCursor(c);
            te->insertPlainText(" UNSAVED-TAIL-23");
        }
    r = callCard("trash_note", {{"file", renamed}}, true);
    const QString trashed = res(r).value("file").toString();
    EXPECT("trash_note moves it to Trash and closes it in the editor",
           ok(r) && QDir::fromNativeSeparators(trashed).contains("/Trash/.trashed-") && !QFileInfo::exists(renamed) && np &&
               np->currentNotePath().isEmpty());
    if (np) np->saveCurrentNote();
    EXPECT("an autosave after trash writes nothing back", !QFileInfo::exists(renamed));
    r = call("list_notes", {{"include_trash", true}});
    bool listed = false;
    for (const QJsonValue &n : res(r).value("notes").toArray())
        if (n.toObject().value("trashed").toBool()) listed = true;
    EXPECT("list_notes include_trash lists the trashed note", ok(r) && listed);
    r = callCard("restore_note", {{"file", trashed}}, true);
    const QString restored = res(r).value("file").toString();
    EXPECT("restore_note brings it back to the Inbox", ok(r) && QFileInfo::exists(restored));
    EXPECT("R23: the open note's unsaved typing was saved before it went to Trash",
           readFile(restored).contains("UNSAVED-TAIL-23"));
    r = call("delete_note_permanently", {{"file", restored}});
    EXPECT("delete_note_permanently refuses a note outside Trash", !ok(r));
    r = callCard("trash_note", {{"file", restored}}, true);
    const QString trashed2 = res(r).value("file").toString();
    r = callCard("delete_note_permanently", {{"file", trashed2}}, true);
    EXPECT("delete_note_permanently from Trash (card says cannot be undone)",
           ok(r) && g_cardWarn.contains("cannot be undone") && !QFileInfo::exists(trashed2));
    r = callCard("create_note", {{"title", "Exported"}, {"body", "export me"}}, true);
    const QString exportSrc = res(r).value("file").toString();
    r = callCard("export_note", {{"file", exportSrc}, {"format", "markdown"},
                                 {"path", work.path() + "/note.md"}}, true);
    EXPECT("export_note markdown", ok(r) && readFile(work.path() + "/note.md").contains("export me"));
#ifndef NOTEPATRA_WITH_WEBENGINE
    r = call("export_note", {{"file", exportSrc}, {"format", "pdf"}, {"path", work.path() + "/n.pdf"}});
    EXPECT("export_note pdf is honestly unavailable in Lite", !ok(r) && errOf(r).contains("not available"));
#endif
    const QString due = QDateTime::currentDateTimeUtc().addDays(2).toString(Qt::ISODate);
    callCard("set_reminder", {{"file", exportSrc}, {"due_iso", due}}, true);
    r = call("list_reminders");
    const QString rid = res(r).value("reminders").toArray().at(0).toObject().value("id").toString();
    EXPECT("list_reminders carries an id", ok(r) && !rid.isEmpty());
    r = callCard("snooze_reminder", {{"id", rid}, {"minutes", 30}}, true);
    EXPECT("snooze_reminder", ok(r) && !res(r).value("snoozed_until_iso").toString().isEmpty());
    r = callCard("dismiss_reminder", {{"id", rid}}, true);
    EXPECT("dismiss_reminder", ok(r) && res(r).value("dismissed").toBool());
    r = callCard("delete_reminder", {{"id", rid}}, true);
    EXPECT("delete_reminder", ok(r));
    r = call("delete_reminder", {{"id", rid}});
    EXPECT("a deleted reminder id is unknown", !ok(r));

    // ── preferences ──
    r = call("get_preferences");
    EXPECT("get_preferences has tab_width and no AI keys",
           ok(r) && res(r).value("preferences").toObject().contains("tab_width") &&
               !QJsonDocument(r).toJson().contains("aiApiKey") &&
               !QJsonDocument(r).toJson().contains("aiBaseUrl"));
    r = call("set_preference", {{"key", "aiApiKey"}, {"value", "x"}});
    EXPECT("set_preference aiApiKey refused", !ok(r) && Config::instance().aiApiKey != "x");
    writeFile(work.path() + "/tabs.py", "x = 1\n");
    Editor *pe = editorById(openFile(work.path() + "/tabs.py"));
    r = callCard("set_preference", {{"key", "tab_width"}, {"value", 2}}, true);
    EXPECT("set_preference tab_width applies to open editors at once",
           ok(r) && Config::instance().tabWidth == 2 && pe && pe->tabWidth() == 2);
    r = callCard("set_preference", {{"key", "theme"}, {"value", "Dark"}}, true);
    EXPECT("set_preference theme through the Theme menu action", ok(r) && Config::instance().theme == "Dark");

    // ── data ──
    const QString dbPath = work.path() + "/scratch.db";
    r = callCard("add_connection", {{"name", "scratch"}, {"driver", "QSQLITE"}, {"database", dbPath}}, true);
    EXPECT("add_connection (no password stored; response says where to enter it)",
           ok(r) && res(r).value("note").toString().contains("Manage Connections") &&
               !res(r).contains("password"));
    r = callCard("execute_sql", {{"connection_name", "scratch"},
                                 {"sql", "CREATE TABLE t (id INTEGER PRIMARY KEY, name TEXT)"}}, true);
    EXPECT("execute_sql CREATE TABLE", ok(r));
    r = callCard("execute_sql", {{"connection_name", "scratch"},
                                 {"sql", "INSERT INTO t (name) VALUES ('alpha')"}}, true);
    EXPECT("execute_sql INSERT approved", ok(r) && g_cardPreview.contains("INSERT INTO t"));
    r = call("run_query", {{"connection_name", "scratch"}, {"sql", "SELECT name FROM t"}});
    EXPECT("the insert reads back", ok(r) && res(r).value("rows").toArray().at(0).toArray().at(0).toString() == "alpha");
    r = call("execute_sql", {{"connection_name", "scratch"}, {"sql", "DELETE FROM t; DROP TABLE t"}});
    EXPECT("execute_sql stacked statements refused", !ok(r) && errOf(r).contains("exactly one"));
    r = call("execute_sql", {{"connection_name", "scratch"}, {"sql", "ATTACH DATABASE '/tmp/x.db' AS x"}});
    EXPECT("execute_sql ATTACH refused even though it is a mutation", !ok(r) && errOf(r).contains("cannot override"));
    r = call("describe_table", {{"connection_name", "scratch"}, {"table", "t"}});
    EXPECT("describe_table columns and primary key",
           ok(r) && res(r).value("columns").toArray().size() == 2 &&
               res(r).value("primary_key").toArray() == QJsonArray{"id"});
    r = call("describe_table", {{"connection_name", "scratch"}, {"table", "t'; DROP TABLE t; --"}});
    EXPECT("describe_table never puts the argument into SQL", !ok(r));
    r = call("test_connection", {{"connection_name", "scratch"}});
    EXPECT("test_connection ok", ok(r) && res(r).value("ok").toBool());
    {
        const QString missingDb = work.path() + "/never-created.db";
        callCard("add_connection", {{"name", "ghost"}, {"driver", "QSQLITE"}, {"database", missingDb}}, true);
        r = call("test_connection", {{"connection_name", "ghost"}});
        EXPECT("R12: test_connection on a missing SQLite file reports it and creates nothing",
               ok(r) && !res(r).value("ok").toBool(true) &&
                   res(r).value("error").toString().contains("does not exist") &&
                   !QFileInfo::exists(missingDb));
        r = call("describe_table", {{"connection_name", "ghost"}, {"table", "t"}});
        EXPECT("R12: describe_table on a missing SQLite file creates nothing",
               !ok(r) && !QFileInfo::exists(missingDb));
        callCard("delete_connection", {{"name", "ghost"}}, true);
    }
    r = callCard("update_connection", {{"name", "scratch"}, {"new_name", "scratch2"}}, true);
    EXPECT("update_connection rename", ok(r) && res(r).value("name").toString() == "scratch2");
    {
        // W3: the stored password follows the server it was saved for. The
        // card is built while the record points at one host; the user moves
        // it to another in Manage Connections before Approve; the approved
        // update puts the first host back, so the password saved for the
        // second must not travel with it.
        auto seed = [](const QString &host, const QString &pw) {
            QVector<DbConnections::Record> all = DbConnections::loadAll();
            DbConnections::Record rec;
            rec.name = "remote";
            rec.driver = "QPSQL";
            rec.host = host;
            rec.port = 5432;
            rec.database = "app";
            rec.username = "me";
            rec.password = pw;
            for (int i = all.size() - 1; i >= 0; --i)
                if (all.at(i).name == rec.name) all.remove(i);
            all.append(rec);
            return DbConnections::saveAll(all);
        };
        auto stored = []() {
            DbConnections::Record rec;
            DbConnections::findByName("remote", &rec);
            return rec;
        };
        EXPECT("fixture: a server connection with a saved password", seed("db.internal", "pw-one"));
        r = callCard("update_connection", {{"name", "remote"}, {"database", "app2"}}, true);
        EXPECT("W3: a database-only change keeps the saved password",
               ok(r) && stored().database == "app2" && stored().password == "pw-one");
        r = callCard("update_connection", {{"name", "remote"}, {"database", "app3"}}, true,
                     [&]() { seed("other.internal", "pw-for-other"); });
        EXPECT("W3: a host change found at Approve clears the password, whatever the card said",
               ok(r) && stored().host == "db.internal" && stored().password.isEmpty());
        seed("db.internal", "pw-one");
        r = callCard("update_connection", {{"name", "remote"}, {"port", 6543}}, true);
        EXPECT("W3: a port change clears the saved password", ok(r) && stored().password.isEmpty());
        callCard("delete_connection", {{"name", "remote"}}, true);
    }
    r = call("list_connections");
    EXPECT("no tool returns a password or options",
           !QJsonDocument(r).toJson().contains("password") && !QJsonDocument(r).toJson().contains("options"));
    r = call("open_connections_dialog");
    QDialog *dlg = nullptr;
    for (QWidget *w : QApplication::topLevelWidgets())
        if (w->inherits("DbConnectionsDialog") && w->isVisible()) dlg = qobject_cast<QDialog *>(w);
    EXPECT("open_connections_dialog opens non-modally", ok(r) && dlg && !dlg->isModal());
    if (dlg) dlg->close();
    r = callCard("delete_connection", {{"name", "scratch2"}}, true);
    EXPECT("delete_connection", ok(r) && g_cardDesc.contains("scratch2"));
    // run_sql over a workspace CSV in Lite (DuckDB in Full).
    writeFile(g_repo + "/data.csv", "name,qty\nx,1\ny,2\n");
    r = call("run_sql", {{"sql", "SELECT count(*) FROM data"}, {"csv_path", g_repo + "/data.csv"}});
    EXPECT("run_sql csv_path works in this build", ok(r) && res(r).value("rows").toArray().at(0).toArray().at(0).toString() == "2");
    r = call("run_sql", {{"sql", "SELECT 1"}, {"csv_path", home.path() + "/outside.csv"}});
    EXPECT("run_sql csv_path outside the workspace refused", !ok(r));

    // ── REST against 127.0.0.1 ──
    QTcpServer server;
    server.listen(QHostAddress::LocalHost, 0);
    QByteArray seen;
    QObject::connect(&server, &QTcpServer::newConnection, [&server, &seen]() {
        QTcpSocket *s = server.nextPendingConnection();
        QObject::connect(s, &QTcpSocket::readyRead, s, [s, &seen]() {
            seen += s->readAll();
            if (!seen.contains("\r\n\r\n")) return;
            s->write("HTTP/1.1 200 OK\r\nContent-Type: application/json\r\nContent-Length: 11\r\n"
                     "Connection: close\r\n\r\n{\"ok\":true}");
            s->disconnectFromHost();
        });
    });
    const QString base = QStringLiteral("http://127.0.0.1:%1").arg(server.serverPort());
    r = call("rest_request", {{"method", "GET"}, {"url", "file:///etc/passwd"}});
    EXPECT("rest_request file:// refused", !ok(r));
    r = callCard("rest_request", {{"method", "GET"}, {"url", base + "/health"},
                                  {"headers", QJsonObject{{"Authorization", "Bearer s3cr3t-value"}}}}, true);
    EXPECT("rest_request to a loopback server: 200 and the body",
           ok(r) && res(r).value("status").toInt() == 200 &&
               res(r).value("body").toString() == "{\"ok\":true}" && seen.contains("GET /health") &&
               !g_cardPreview.contains("s3cr3t"));
    EXPECT("the request is in the REST history, secret redacted",
           readFile(Config::appConfigDir() + "/rest-client.json").contains("/health") &&
               !readFile(Config::appConfigDir() + "/rest-client.json").contains("s3cr3t-value"));
    writeFile(work.path() + "/api.http", ("@host = " + base + "\n\n### Ping\nGET {{host}}/ping\n").toUtf8());
    const qint64 ht = openFile(work.path() + "/api.http");
    r = call("parse_http_file", {{"tab_id", ht}});
    EXPECT("parse_http_file keeps variables unexpanded",
           ok(r) && res(r).value("requests").toArray().at(0).toObject().value("url").toString() == "{{host}}/ping");
    seen.clear();
    r = callCard("rest_request", {{"tab_id", ht}, {"line", 4}}, true);
    EXPECT("rest_request from a .http tab resolves {{host}} (card shows the resolved host)",
           ok(r) && g_cardDesc.contains("127.0.0.1") && seen.contains("GET /ping"));
    // R5: the environment changes while the card waits; the request that
    // reaches the server is the one the card described.
    {
        const QString envDir = work.path() + "/envapi";
        QDir().mkpath(envDir);
        writeFile(envDir + "/env.http", "### Who\nGET {{host}}/who\nX-Token: {{tok}}\n");
        writeFile(envDir + "/http-client.env.json",
                  ("{\"dev\": {\"host\": \"" + base + "\", \"tok\": \"dev-token-1\"}}").toUtf8());
        RestHistory::setEnvironmentFor(Config::appConfigDir() + "/rest-client.json",
                                       QFileInfo(envDir + "/env.http").absolutePath(), "dev");
        const qint64 et = openFile(envDir + "/env.http");
        seen.clear();
        r = callCard("rest_request", {{"tab_id", et}, {"line", 2}}, true, [&]() {
            writeFile(envDir + "/http-client.env.json",
                      "{\"dev\": {\"host\": \"http://127.0.0.1:1\", \"tok\": \"prod-token-2\"}}");
        });
        EXPECT("R5: the card's host and token are what is sent, not the env file's new values",
               ok(r) && g_cardDesc.contains("127.0.0.1:" + QString::number(server.serverPort())) &&
                   seen.contains("GET /who") && seen.contains("dev-token-1") &&
                   !seen.contains("prod-token-2"));
    }
    r = call("curl_to_request", {{"text", "curl -X POST " + base + "/a -H 'X-A: 1' -d 'b'"}});
    EXPECT("curl_to_request", ok(r) && res(r).value("method").toString() == "POST");
    r = call("request_to_curl", {{"method", "GET"}, {"url", base + "/b"}});
    EXPECT("request_to_curl", ok(r) && res(r).value("text").toString().startsWith("curl"));

    // ── R7 / R18: close_tab and reload_tab ──
    {
        auto *tm = g_mw->findChild<TabManager *>();
        r = call("create_diagram", {{"source", "flow\n  a -> b\n"}});
        const qint64 dt = qint64(res(r).value("tab_id").toDouble(-1));
        DiagramEditor *de = nullptr;
        for (int i = 0; i < tm->count(); ++i)
            if (tm->widget(i)->property("npMcpTabId").toLongLong() == dt)
                de = qobject_cast<DiagramEditor *>(tm->widget(i));
        if (de)
            if (auto *pe = de->findChild<QPlainTextEdit *>()) pe->appendPlainText("  b -> c");
        r = call("close_tab", {{"tab_id", dt}});
        EXPECT("R7: a Diagram tab with unsaved .npd edits is not closed without a card",
               de && de->isModified() && !ok(r) && errOf(r).contains("unsaved"));
        r = callCard("close_tab", {{"tab_id", dt}, {"discard_unsaved", true}}, true);
        EXPECT("R7: with discard_unsaved and Approve the Diagram tab closes", ok(r));
        int restTab = -1;
        for (int i = 0; i < tm->count(); ++i)
            if (tm->widget(i)->inherits("RestClient")) restTab = i;
        if (restTab >= 0) {
            r = callCard("close_tab", {{"tab_index", restTab}}, false);
            EXPECT("R7: a REST Client tab (cannot report unsaved work) is closed only via a card",
                   errOf(r) == "denied by user" && tm->widget(restTab)->inherits("RestClient"));
        }
        writeFile(work.path() + "/reload.txt", "on disk\n");
        const qint64 rt = openFile(work.path() + "/reload.txt");
        Editor *red = editorById(rt);
        if (red) red->setText("first edit\n");
        r = callCard("reload_tab", {{"tab_id", rt}}, true, [red]() {
            if (red) red->append("typed while the card waited\n");
        });
        EXPECT("R18: reload_tab refuses when the tab was edited while its card waited",
               !ok(r) && red && red->text().contains("typed while the card waited"));
        r = callCard("reload_tab", {{"tab_id", rt}}, true);
        EXPECT("R18: an unchanged tab reloads on Approve", ok(r) && red && red->text() == "on disk\n");
    }

    // ── W2: the app's own settings are never readable through MCP ──
    {
        // The user opens a settings file in a tab; nothing of it leaves
        // through MCP. config.json is rewritten by the app itself (recent
        // files), so its file-watcher prompt is answered here; that prompt
        // is a QMessageBox, which offscreen Windows cannot show: db-
        // connections.json (not rewritten meanwhile) covers every platform.
        auto check = [&](const QString &file, const char *marker) {
            r = call("open_file", {{"path", file}});
            EXPECT("W2: open_file on an app settings file is refused",
                   !ok(r) && errOf(r).contains("deny-list"));
            g_mw->openFile(file); // the USER opens it
            pump(100);
            auto *tm = g_mw->findChild<TabManager *>();
            int ci = -1;
            for (int i = 0; i < tm->count(); ++i)
                if (Editor *e = tm->editorAt(i))
                    if (QFileInfo(e->filePath()).fileName() == QFileInfo(file).fileName()) ci = i;
            EXPECT("fixture: the user has the settings file open in a tab", ci >= 0);
            if (ci < 0) return;
            r = call("read_tab", {{"index", ci}});
            EXPECT("W2: read_tab on the user's settings tab is refused",
                   !ok(r) && errOf(r).contains("deny-list") && !QJsonDocument(r).toJson().contains(marker));
            r = call("hash_text", {{"tab_index", ci}});
            EXPECT("W2: hash_text on that tab is refused", !ok(r));
            r = call("hex_dump", {{"tab_index", ci}});
            EXPECT("W2: hex_dump on that tab is refused", !ok(r));
            r = call("find_in_tab", {{"index", ci}, {"query", "a"}});
            EXPECT("W2: find_in_tab on that tab is refused", !ok(r));
            tm->setCurrentIndex(ci);
            tm->editorAt(ci)->selectAll();
            r = call("get_selection");
            EXPECT("W2: get_selection inside that tab is refused",
                   !ok(r) && !QJsonDocument(r).toJson().contains(marker));
            tm->editorAt(ci)->setModified(false);
            call("close_tab", {{"tab_index", ci}});
        };
        check(Config::appConfigDir() + "/db-connections.json", "QSQLITE");
#ifndef Q_OS_WIN
        QTimer keep;
        QObject::connect(&keep, &QTimer::timeout, []() {
            for (QWidget *w : QApplication::topLevelWidgets())
                if (auto *mb = qobject_cast<QMessageBox *>(w); mb && mb->isVisible())
                    mb->done(QMessageBox::No);
        });
        keep.start(50);
        check(Config::appConfigDir() + "/config.json", "showWelcome");
        keep.stop();
#endif
    }

    // ── leftovers ──
    writeFile(g_repo + "/bytes.bin", QByteArray("\x00\x01" "AB", 4));
    r = call("hex_dump", {{"path", g_repo + "/bytes.bin"}});
    EXPECT("hex_dump of a workspace file", ok(r) && res(r).value("hex").toString().startsWith("00000000  00 01 41 42"));
    writeFile(home.path() + "/outside.bin", "x");
    r = call("hex_dump", {{"path", home.path() + "/outside.bin"}});
    EXPECT("hex_dump outside the workspace refused", !ok(r));
    r = callCard("clear_recent_files", {}, true);
    EXPECT("clear_recent_files empties the list", ok(r) && Config::instance().recentFiles.isEmpty());

    delete g_mw;
    std::printf("\n%d passed, %d failed\n", g_pass, g_fail);
    return g_fail == 0 ? 0 : 1;
}
