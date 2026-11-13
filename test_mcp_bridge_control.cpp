// SPDX-License-Identifier: GPL-3.0-or-later
//
// v0.1.133 Phase 3 bridge verbs (git writes, Noter, settings, data, REST)
// against a FAKE host: argument checks, deny-list refusals before any card,
// card wording, Deny, async completion. The real-app behaviour is in
// test_mcp_host_control.cpp. Offscreen, no POSIX headers, no QMessageBox.

#include "dbconnections.h"
#include "mcp_bridge.h"

#include <QApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFrame>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QLocalSocket>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QThread>
#include <QTimer>
#include <QWidget>

#include <cstdio>
#include <functional>

static int g_pass = 0, g_fail = 0;
#define EXPECT(label, cond) \
    do { if (cond) { ++g_pass; std::printf("  [PASS] %s\n", label); } \
         else      { ++g_fail; std::printf("  [FAIL] %s\n", label); } \
         std::fflush(stdout); } while (0)

namespace {
QWidget *g_win = nullptr;
QLocalSocket *g_sock = nullptr;
int g_id = 0;
QStringList g_ops;          // every host op the bridge ran, "area:op"
QJsonObject g_lastArgs;     // args of the last write op
QJsonObject g_gitCtx;       // what gitOp("context") answers
QString g_cardDesc, g_cardWarn, g_cardPreview;

void pump(int ms) {
    QElapsedTimer t;
    t.start();
    while (t.elapsed() < ms) {
        QApplication::processEvents(QEventLoop::AllEvents, 10);
        QThread::msleep(2);
    }
}

QJsonObject readObj(int timeoutMs = 5000) {
    QElapsedTimer t;
    t.start();
    while (!g_sock->canReadLine() && t.elapsed() < timeoutMs) pump(5);
    if (!g_sock->canReadLine()) return QJsonObject{{"timeout", true}};
    return QJsonDocument::fromJson(g_sock->readLine().trimmed()).object();
}

void send(const QString &verb, const QJsonObject &args) {
    g_sock->write(QJsonDocument(QJsonObject{{"id", ++g_id}, {"verb", verb}, {"args", args}})
                      .toJson(QJsonDocument::Compact) + '\n');
    g_sock->flush();
}

QFrame *liveCard(int timeoutMs) {
    QElapsedTimer t;
    t.start();
    do {
        for (QFrame *c : g_win->findChildren<QFrame *>(QStringLiteral("mcpApprovalCard")))
            if (c->isVisible()) return c;
        pump(10);
    } while (t.elapsed() < timeoutMs);
    return nullptr;
}

// Immediate answer, and no card was raised for it.
QJsonObject call(const QString &verb, const QJsonObject &args = {}) {
    send(verb, args);
    const QJsonObject r = readObj();
    if (liveCard(50)) return QJsonObject{{"unexpected_card", true}};
    return r;
}

// Tab state the fake host reports (R7 / R18).
bool g_modified = false;
bool g_reports = true;
QByteArray g_rev = "r0";

QJsonObject callCard(const QString &verb, const QJsonObject &args, bool approve,
                     const std::function<void()> &whileWaiting = {}) {
    send(verb, args);
    QFrame *card = liveCard(3000);
    if (!card) return QJsonObject{{"no_card", true}, {"early", readObj(200)}};
    if (whileWaiting) whileWaiting();
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
    const QJsonObject r = readObj();
    pump(30);
    return r;
}

bool ok(const QJsonObject &r) { return r.value(QLatin1String("ok")).toBool(); }
QString errOf(const QJsonObject &r) { return r.value(QLatin1String("error")).toString(); }
QJsonObject res(const QJsonObject &r) { return r.value(QLatin1String("result")).toObject(); }
bool ran(const QString &op) { return g_ops.contains(op); }

bool writeFile(const QString &path, const QByteArray &bytes) {
    QDir().mkpath(QFileInfo(path).absolutePath());
    QFile f(path);
    return f.open(QIODevice::WriteOnly | QIODevice::Truncate) && f.write(bytes) == bytes.size();
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
    qputenv("QT_QPA_PLATFORM", "offscreen");
    QStandardPaths::setTestModeEnabled(true);
    QApplication app(argc, argv);
    std::printf("=== test_mcp_bridge_control ===\n");

    QTemporaryDir work;
    const QString repo = QDir(work.path()).absoluteFilePath(QStringLiteral("repo"));
    const QString notes = QDir(work.path()).absoluteFilePath(QStringLiteral("Noter"));
    QDir().mkpath(repo);
    writeFile(notes + "/Inbox/n1.html", "<html><body><h1>One</h1></body></html>");
    writeFile(notes + "/Trash/.trashed-1-n2.html", "<html><body><h1>Two</h1></body></html>");
    QDir().mkpath(home.path() + "/.ssh");

    g_win = new QWidget;
    g_win->resize(800, 500);
    g_win->show();

    g_gitCtx = QJsonObject{{"repo_root", repo}, {"branch", "main"},
                           {"upstream", "origin/main"}, {"upstream_remote", "origin"},
                           {"staged", QJsonArray{"a.txt"}}, {"modified", QJsonArray{"b.txt", "d.txt"}},
                           {"untracked", QJsonArray{"c.txt", ".env"}}, {"conflicts", QJsonArray{"m.txt"}},
                           {"branches", QJsonArray{"main", "dev"}}, {"stash_count", 1}};

    McpEditorHost h;
    h.tabCount = [] { return 1; };
    h.tabTitle = [](int) { return QStringLiteral("api.http"); };
    h.tabPath = [](int) { return QString(); };
    h.tabText = [](int) { return QStringLiteral("GET http://127.0.0.1/a\n"); };
    h.tabModified = [](int) { return g_modified; };
    h.tabReportsModified = [](int) { return g_reports; };
    h.tabRevision = [](int) { return g_rev; };
    h.approvalParent = [] { return g_win; };
    h.notesRoot = [notes] { return notes; };
    h.workspaceRoot = [repo] { return repo; };
    h.recentFiles = [] { return QStringList{"/a", "/b"}; };
    auto record = [](const QString &area, const QString &op, const QJsonObject &a) {
        g_ops << area + QLatin1Char(':') + op;
        g_lastArgs = a;
    };
    h.gitOp = [record](const QString &op, const QJsonObject &a, QString *err) -> QJsonObject {
        if (op == QLatin1String("context")) return g_gitCtx;
        if (op == QLatin1String("list_hunks"))
            return {{"hunks", QJsonArray{QJsonObject{{"index", 0}, {"header", "@@ -1 +1 @@"},
                                                     {"diff", "@@ -1 +1 @@\n-old\n+new"},
                                                     {"fingerprint", "fp-hunk"}}}}};
        if (op == QLatin1String("list_conflicts") && a.contains("path"))
            return {{"regions", QJsonArray{QJsonObject{{"start_line", 2}, {"end_line", 6},
                                                       {"ours", "x\n"}, {"theirs", "y\n"},
                                                       {"fingerprint", "fp-region"}}}}};
        record("git", op, a);
        Q_UNUSED(err);
        return {{"done", op}};
    };
    h.gitAsync = [record](const QString &op, const QJsonObject &a, McpEditorHost::Done done) {
        record("git", op, a);
        QTimer::singleShot(20, [done]() { done({{"ok", true}}, QString()); });
    };
    h.noterOp = [record](const QString &op, const QJsonObject &a, QString *err) -> QJsonObject {
        if (op == QLatin1String("export_formats")) return {{"formats", QJsonArray{"markdown"}}};
        if (op == QLatin1String("reminder_info")) {
            if (a.value("id").toString() != QLatin1String("r1")) *err = "no reminder with id";
            return {{"note_title", "One"}, {"due_iso", "2030-01-01T10:00:00Z"}};
        }
        record("noter", op, a);
        return {{"done", op}};
    };
    h.settingsOp = [record](const QString &op, const QJsonObject &a, QString *err) -> QJsonObject {
        if (op == QLatin1String("get_preferences"))
            return {{"preferences", QJsonObject{{"tab_width", 4}, {"font_size", 11},
                                                {"aiApiKey", "LEAKED"}, {"aiBaseUrl", "http://x"}}}};
        if (op == QLatin1String("check_font")) {
            if (a.value("value").toString() != QLatin1String("Mono")) *err = "font family not installed";
            return {};
        }
        record("settings", op, a);
        return {{"done", op}};
    };
    h.dataOp = [record](const QString &op, const QJsonObject &a, QString *err) -> QJsonObject {
        if (op == QLatin1String("connection_info")) {
            if (a.value("connection_name").toString() != QLatin1String("db")) {
                *err = "no connection named";
                return {};
            }
            return {{"name", "db"}, {"driver", "QPSQL"}, {"host", "db.internal"}, {"port", 5432},
                    {"database", "app"}, {"username", "me"}, {"has_password", true},
                    {"has_options", false}};
        }
        if (op == QLatin1String("classify_sql")) {
            // The REAL gate, so the hard-fail list is tested end to end.
            const auto v = DbConnections::classifyApprovedWrite(a.value("sql").toString(), "QPSQL");
            return {{"single_statement", v.singleStatement}, {"statement_count", v.statementCount},
                    {"mutation", v.mutation}, {"read_only", v.readOnly},
                    {"hard_fail", v.hardFail}, {"reason", v.hardFail ? v.hardReason : v.reason}};
        }
        record("data", op, a);
        return {{"done", op}};
    };
    h.dataAsync = [record](const QString &op, const QJsonObject &a, McpEditorHost::Done done) {
        record("data", op, a);
        done({{"ok", true}}, QString());
    };
    h.restOp = [record](const QString &op, const QJsonObject &a, QString *) -> QJsonObject {
        if (op == QLatin1String("resolve_request")) {
            if (a.contains("text"))
                return {{"method", "GET"}, {"url", "http://127.0.0.1/a"}, {"headers", QJsonArray()}};
            return {{"method", a.value("method")}, {"url", a.value("url")},
                    {"headers", a.value("headers")}, {"body", a.value("body")}};
        }
        record("rest", op, a);
        return {{"done", op}};
    };
    h.restAsync = [record](const QString &op, const QJsonObject &a, McpEditorHost::Done done) {
        record("rest", op, a);
        done({{"status", 200}}, QString());
    };
    h.tabAction = [record](int, const QString &op, const QJsonObject &a, QString *) -> QJsonObject {
        record("tab", op, a);
        return {};
    };
    h.editTab = [record](int, const QString &op, const QJsonObject &a, QString *) -> QJsonObject {
        record("tabedit", op, a);
        return {};
    };
    h.insertText = [record](int, int, int, const QString &t) {
        record("tab", "insert_text", {{"text", t}});
        return true;
    };
    h.miscOp = [record](const QString &op, const QJsonObject &a, QString *) -> QJsonObject {
        if (op == QLatin1String("hex_bytes"))
            return {{"base64", QString::fromLatin1(QByteArray("Hello, world!\n").toBase64())},
                    {"total_size", 14}, {"source", "x"}};
        record("misc", op, a);
        return {{"cleared", 2}};
    };

    const QString name = QStringLiteral("np-mcp-ctl-%1").arg(QCoreApplication::applicationPid());
    McpBridge bridge(h, nullptr, name, work.path());
    QLocalSocket sock;
    g_sock = &sock;
    sock.connectToServer(bridge.fullServerName());
    QElapsedTimer t;
    t.start();
    while (sock.state() != QLocalSocket::ConnectedState && t.elapsed() < 5000) pump(10);
    EXPECT("connected", sock.state() == QLocalSocket::ConnectedState);
    if (sock.state() != QLocalSocket::ConnectedState) return 1;
    readObj(); // greeting

    // ── git: refusals before any card ──
    QJsonObject r = call("git_stage", {{"paths", QJsonArray{".env"}}});
    EXPECT("staging .env is refused (deny-list), no card",
           !ok(r) && errOf(r).contains("credential deny-list") && !ran("git:git_stage"));
    r = call("git_stage", {{"paths", QJsonArray{"-A"}}});
    EXPECT("a path starting with '-' is refused", !ok(r) && errOf(r).contains("'-'"));
    r = call("git_stage", {{"paths", QJsonArray{"./-A"}}});
    EXPECT("./-A is refused too (option injection after cleaning)", !ok(r) && errOf(r).contains("'-'"));
    r = call("git_stage", {{"paths", QJsonArray{"../outside.txt"}}});
    EXPECT("a path escaping the repo is refused", !ok(r) && errOf(r).contains("outside"));
    r = call("git_stage", {{"paths", QJsonArray{".git/config"}}});
    EXPECT("a path inside .git is refused", !ok(r));
    r = call("git_stage", {{"paths", QJsonArray{"zzz.txt"}}});
    EXPECT("staging an unchanged file is refused", !ok(r) && errOf(r).contains("nothing to stage"));
    r = call("git_discard", {{"paths", QJsonArray{"c.txt"}}});
    EXPECT("discarding an untracked file is refused (no git clean)",
           !ok(r) && errOf(r).contains("never deletes untracked"));
    r = call("git_push", {{"force", true}});
    EXPECT("git_push takes no arguments (no --force, no refspec)", !ok(r));
    r = call("git_checkout", {{"name", "nope"}});
    EXPECT("checkout of an unknown branch is refused", !ok(r) && errOf(r).contains("no local branch"));
    r = call("git_branch_create", {{"name", "-D"}});
    EXPECT("a branch name starting with '-' is refused", !ok(r) && errOf(r).contains("invalid branch"));
    r = call("git_hunk", {{"path", "b.txt"}, {"hunk_index", 3}, {"action", "stage"}});
    EXPECT("an out-of-range hunk is refused", !ok(r) && errOf(r).contains("does not exist"));
    r = call("resolve_conflict", {{"path", "m.txt"}, {"region", 2}, {"take", "ours"}});
    EXPECT("an out-of-range conflict region is refused", !ok(r));
    r = call("list_hunks", {{"path", ".env"}});
    EXPECT("list_hunks on a secret path is refused", !ok(r) && errOf(r).contains("deny-list"));

    // ── git: cards ──
    r = callCard("git_stage", {{"paths", QJsonArray{"b.txt", repo + "/d.txt"}}}, false);
    EXPECT("git_stage Deny: denied, host not called",
           errOf(r) == "denied by user" && !ran("git:git_stage"));
    EXPECT("git_stage card names the repo and the count",
           g_cardDesc.contains("git add in") && g_cardDesc.contains("stage 2 file(s)") &&
               g_cardPreview.contains("- b.txt") && g_cardPreview.contains("- d.txt"));
    r = callCard("git_stage", {{"paths", QJsonArray{"b.txt"}}}, true);
    EXPECT("git_stage Approve runs the host with repo-relative paths",
           ok(r) && ran("git:git_stage") && g_lastArgs.value("paths").toArray() == QJsonArray{"b.txt"});
    EXPECT("R6: a git write carries the repository the card named",
           g_lastArgs.value("expect_root").toString() == repo);
    r = callCard("git_commit", {{"message", "fix: one\n\nbody"}}, false);
    EXPECT("git_commit card lists the staged files and the message",
           g_cardDesc.contains("git commit on branch 'main'") && g_cardPreview.contains("fix: one") &&
               g_cardPreview.contains("body") && g_cardPreview.contains("- a.txt"));
    g_gitCtx["staged"] = QJsonArray{"a.txt", "secrets/.env"};
    r = callCard("git_commit", {{"message", "x"}}, false);
    EXPECT("a staged secret is called out on the commit card", g_cardWarn.contains("secrets/.env"));
    g_gitCtx["staged"] = QJsonArray();
    r = call("git_commit", {{"message", "x"}});
    EXPECT("git_commit with nothing staged is refused (no commit -a)",
           !ok(r) && errOf(r).contains("nothing is staged") && !ran("git:git_commit"));
    g_gitCtx["staged"] = QJsonArray{"a.txt"};
    r = callCard("git_discard", {{"paths", QJsonArray{"b.txt"}}}, false);
    EXPECT("git_discard card carries a data-loss warning",
           g_cardWarn.contains("Data loss") && g_cardWarn.contains("cannot be recovered"));
    r = callCard("git_hunk", {{"path", "b.txt"}, {"hunk_index", 0}, {"action", "revert"}}, false);
    EXPECT("git_hunk revert shows the hunk and warns", g_cardWarn.contains("Data loss") &&
                                                           g_cardPreview.contains("+new"));
    r = callCard("resolve_conflict", {{"path", "m.txt"}, {"region", 0}, {"take", "both"}}, true);
    EXPECT("resolve_conflict card shows the replacement, host gets the expected start line",
           ok(r) && g_cardPreview.contains("x\ny") && g_lastArgs.value("expect_start").toInt() == 2);
    EXPECT("R15: resolve_conflict pins the whole region and the repository",
           g_lastArgs.value("expect_region").toString() == "fp-region" &&
               g_lastArgs.value("expect_root").toString() == repo);
    r = callCard("git_hunk", {{"path", "b.txt"}, {"hunk_index", 0}, {"action", "stage"}}, true);
    EXPECT("R15: git_hunk pins the whole hunk text, not only its header",
           ok(r) && g_lastArgs.value("expect_hunk").toString() == "fp-hunk" &&
               g_lastArgs.value("expect_header").toString() == "@@ -1 +1 @@" &&
               g_lastArgs.value("expect_root").toString() == repo);
    r = callCard("git_push", {}, true);
    EXPECT("git_push card says never forced; async completion answers",
           ok(r) && g_cardDesc.contains("never forced") && ran("git:git_push") &&
               g_lastArgs.value("expect_root").toString() == repo);
    g_gitCtx["upstream"] = QString();
    r = call("git_pull");
    EXPECT("git_pull without an upstream is refused", !ok(r) && errOf(r).contains("no upstream"));
    g_gitCtx["upstream"] = QStringLiteral("origin/main");
    r = callCard("git_stash", {{"action", "pop"}}, false);
    EXPECT("git_stash pop card", g_cardDesc.contains("stash@{0}"));

    // ── Noter ──
    r = call("trash_note", {{"file", notes + "/Trash/.trashed-1-n2.html"}});
    EXPECT("trash_note on a trashed note is refused", !ok(r) && errOf(r).contains("Trash"));
    r = call("delete_note_permanently", {{"file", notes + "/Inbox/n1.html"}});
    EXPECT("delete_note_permanently outside Trash is refused (as in the UI)", !ok(r));
    r = callCard("delete_note_permanently", {{"file", notes + "/Trash/.trashed-1-n2.html"}}, false);
    EXPECT("delete_note_permanently card says it cannot be undone",
           g_cardWarn.contains("cannot be undone") && !ran("noter:delete_note_permanently"));
    r = call("read_note", {{"file", work.path() + "/elsewhere.html"}});
    EXPECT("a note outside the Noter folder is refused", !ok(r));
    r = call("export_note", {{"file", "Inbox/n1.html"}, {"format", "pdf"}, {"path", work.path() + "/n.pdf"}});
    EXPECT("export_note pdf where unsupported says so", !ok(r) && errOf(r).contains("not available"));
    r = call("export_note", {{"file", "Inbox/n1.html"}, {"format", "markdown"}, {"path", home.path() + "/.bashrc"}});
    EXPECT("export_note to a shell rc file is refused before a card",
           !ok(r) && errOf(r).contains("protected destination"));
    r = callCard("export_note", {{"file", "Inbox/n1.html"}, {"format", "markdown"},
                                 {"path", work.path() + "/n.md"}}, true);
    EXPECT("export_note card names the note and destination",
           ok(r) && g_cardDesc.contains("export note '") && g_cardDesc.contains("as Markdown to"));
    r = call("snooze_reminder", {{"id", "zz"}});
    EXPECT("snooze of an unknown reminder is refused", !ok(r));
    r = callCard("snooze_reminder", {{"id", "r1"}, {"minutes", 15}}, true);
    EXPECT("snooze card", ok(r) && g_cardDesc.contains("15 minute(s)"));
    r = callCard("rename_note", {{"file", "Inbox/n1.html"}, {"new_title", "Uno"}}, false);
    EXPECT("rename_note card", g_cardDesc.contains("to 'Uno'"));

    // ── settings ──
    r = call("get_preferences");
    EXPECT("get_preferences never passes a non-allow-listed key, even from the host",
           ok(r) && res(r).value("preferences").toObject().contains("tab_width") &&
               !res(r).value("preferences").toObject().contains("aiApiKey") &&
               !QJsonDocument(r).toJson().contains("LEAKED"));
    r = call("set_preference", {{"key", "aiApiKey"}, {"value", "sk"}});
    EXPECT("set_preference aiApiKey is refused and names nothing sensitive",
           !ok(r) && errOf(r).contains("unknown or protected") && !errOf(r).contains("aiBaseUrl") &&
               !ran("settings:set_preference"));
    r = call("set_preference", {{"key", "tab_width"}, {"value", 99}});
    EXPECT("set_preference range check", !ok(r) && errOf(r).contains("1 to 16"));
    r = call("set_preference", {{"key", "font_family"}, {"value", "NoSuchFont"}});
    EXPECT("set_preference font_family must be installed", !ok(r));
    r = callCard("set_preference", {{"key", "tab_width"}, {"value", 2}}, true);
    EXPECT("set_preference card shows old -> new", ok(r) && g_cardDesc.contains("tab_width: 4 -> 2"));
    r = call("set_preference", {{"key", "tab_width"}, {"value", 4}});
    EXPECT("an unchanged preference needs no card", ok(r) && !res(r).value("changed").toBool(true));
    // The home the product refuses is QDir::homePath(). On Windows that is the
    // account's profile folder whatever HOME/USERPROFILE say (Qt asks the
    // process token first), so the temp `home` above is not it there.
    r = call("set_workspace_folder", {{"path", QDir::homePath()}});
    EXPECT("set_workspace_folder refuses the home folder", !ok(r) && errOf(r).contains("home folder"));
    r = call("set_workspace_folder", {{"path", QDir::rootPath()}});
    EXPECT("set_workspace_folder refuses the filesystem root", !ok(r) && errOf(r).contains("root"));
    r = call("set_workspace_folder", {{"path", home.path() + "/.ssh"}});
    EXPECT("set_workspace_folder refuses ~/.ssh",
           !ok(r) && errOf(r).contains("refusing") && errOf(r).contains(".ssh"));
    r = call("set_workspace_folder", {{"path", QFileInfo(QDir::homePath()).absolutePath()}});
    EXPECT("set_workspace_folder refuses a folder containing home",
           !ok(r) && errOf(r).contains("contains your home folder"));
    // On Windows `work` lies in ~/AppData/Local/Temp, so this is also the
    // live check that a temp project is not refused as ~/AppData.
    r = callCard("set_workspace_folder", {{"path", repo + "/../repo"}}, true);
    EXPECT("set_workspace_folder canonicalises and states the folder",
           ok(r) && g_cardDesc.contains(QDir::toNativeSeparators(QFileInfo(repo).canonicalFilePath())) &&
               g_lastArgs.value("path").toString() == QFileInfo(repo).canonicalFilePath());
#ifndef Q_OS_WIN
    // The Windows shape on this OS: the temp folder inside ~/AppData.
    {
        const QByteArray oldTmp = qgetenv("TMPDIR");
        const QString appData = QDir::homePath() + "/AppData";
        const QString winTmp = appData + "/Local/Temp";
        QDir().mkpath(winTmp + "/proj/.ssh");
        QDir().mkpath(appData + "/Roaming/app");
        qputenv("TMPDIR", winTmp.toUtf8());
        const bool tmpIsWinShape = QFileInfo(QDir::tempPath()).canonicalFilePath() ==
                                   QFileInfo(winTmp).canonicalFilePath();
        QString canon;
        const QString projR = McpBridge::workspaceFolderRefusal(winTmp + "/proj", &canon);
        const QString tmpR = McpBridge::workspaceFolderRefusal(winTmp);
        const QString roamR = McpBridge::workspaceFolderRefusal(appData + "/Roaming/app");
        const QString appR = McpBridge::workspaceFolderRefusal(appData);
        const QString sshR = McpBridge::workspaceFolderRefusal(winTmp + "/proj/.ssh");
        if (oldTmp.isEmpty()) qunsetenv("TMPDIR");
        else qputenv("TMPDIR", oldTmp);
        EXPECT("fixture: the temp folder is ~/AppData/Local/Temp", tmpIsWinShape);
        EXPECT("a project in a temp folder under ~/AppData is accepted",
               projR.isEmpty() && canon == QFileInfo(winTmp + "/proj").canonicalFilePath());
        EXPECT("...the temp folder itself is still refused as ~/AppData",
               tmpR.contains("~/AppData"));
        EXPECT("...AppData's other folders and AppData itself are still refused",
               roamR.contains("~/AppData") && appR.contains("~/AppData"));
        EXPECT("...a credential folder inside the temp project is still refused",
               sshR.contains("deny-list"));
    }
#endif

    // ── data ──
    r = call("execute_sql", {{"connection_name", "db"}, {"sql", "INSERT INTO t VALUES (1); DROP TABLE u"}});
    EXPECT("execute_sql refuses stacked statements", !ok(r) && errOf(r).contains("exactly one statement"));
    r = call("execute_sql", {{"connection_name", "db"}, {"sql", "INSERT INTO t /* a /* b */ */ VALUES (1)"}});
    EXPECT("execute_sql nested comment is a hard refusal", !ok(r) && errOf(r).contains("cannot override"));
    r = call("execute_sql", {{"connection_name", "db"}, {"sql", "INSERT INTO t SELECT pg_sleep(9)"}});
    EXPECT("execute_sql side-effect function is a hard refusal", !ok(r) && errOf(r).contains("cannot override"));
    r = call("execute_sql", {{"connection_name", "db"}, {"sql", "COPY t TO PROGRAM 'rm -rf /'"}});
    EXPECT("execute_sql COPY TO PROGRAM is a hard refusal", !ok(r) && errOf(r).contains("cannot override"));
    r = call("execute_sql", {{"connection_name", "db"}, {"sql", "INSERT INTO t SELECT * FROM 'x.csv'"}});
    EXPECT("execute_sql file-as-table is a hard refusal", !ok(r) && errOf(r).contains("cannot override"));
    r = call("execute_sql", {{"connection_name", "db"}, {"sql", "CREATE FUNCTION f() RETURNS int AS 'x' LANGUAGE sql"}});
    EXPECT("execute_sql CREATE FUNCTION is a hard refusal", !ok(r));
    r = call("execute_sql", {{"connection_name", "db"}, {"sql", "SELECT 1"}});
    EXPECT("execute_sql refuses a read-only query (use run_query)", !ok(r) && errOf(r).contains("run_query"));
    r = call("execute_sql", {{"connection_name", "nope"}, {"sql", "DELETE FROM t"}});
    EXPECT("execute_sql on an unknown connection", !ok(r));
    const QString longSql = "UPDATE t SET note = '" + QString(400, QChar('x')) + "' WHERE id = 1";
    r = callCard("execute_sql", {{"connection_name", "db"}, {"sql", longSql}}, true);
    EXPECT("execute_sql card: connection, driver, host, 1 statement, the WHOLE SQL",
           ok(r) && g_cardDesc.contains("connection 'db'") && g_cardDesc.contains("QPSQL") &&
               g_cardDesc.contains("db.internal:5432") && g_cardDesc.contains("1 statement") &&
               g_cardPreview.contains("WHERE id = 1") && g_cardWarn.contains("cannot undo") &&
               ran("data:execute_sql"));
    r = call("add_connection", {{"name", "x"}, {"driver", "QPSQL"}, {"database", "d"}, {"password", "p"}});
    EXPECT("add_connection refuses a password field", !ok(r) && errOf(r).contains("never taken"));
    r = call("add_connection", {{"name", "x"}, {"driver", "QPSQL"}, {"database", "d"}, {"options", "s3"}});
    EXPECT("add_connection refuses an options field", !ok(r));
    r = call("add_connection", {{"name", "x"}, {"driver", "QSQLITE"}, {"database", home.path() + "/.ssh/id_rsa"}});
    EXPECT("add_connection refuses a credential file as a database", !ok(r));
    r = callCard("update_connection", {{"name", "db"}, {"host", "evil.example"}}, false);
    EXPECT("moving a connection with a stored password to another host clears it (card says so)",
           g_cardWarn.contains("cleared") && g_cardPreview.contains("host: db.internal -> evil.example"));
    r = callCard("delete_connection", {{"name", "db"}}, false);
    EXPECT("delete_connection card names it", g_cardDesc.contains("'db'") && g_cardDesc.contains("not touched"));
    r = call("test_connection", {{"connection_name", "db"}});
    EXPECT("test_connection answers through the async path", ok(r) && ran("data:test_connection"));

    // ── R2: line breaks and control characters on the card ──
    r = callCard("execute_sql", {{"connection_name", "db"},
                                 {"sql", "UPDATE t SET a=1 WHERE id=5 -- note\rOR 1=1"}}, false);
    EXPECT("R2: a bare CR ends the comment on the card too (two lines, nothing hidden)",
           g_cardPreview.contains("-- note\nOR 1=1") && !g_cardPreview.contains("noteOR"));
    r = callCard("execute_sql", {{"connection_name", "db"},
                                 {"sql", QStringLiteral("UPDATE t SET a=1 -- x") + QChar(0x2028) +
                                             "WHERE 1=1" + QChar(0x0B) + "AND b=2" + QChar(0x0C) +
                                             "AND c=3" + QChar(0x85) + "AND d=4" + QChar(0x2029) + "AND e=5"}},
                 false);
    EXPECT("R2: LS, VT, FF, NEL and PS are line breaks on the card",
           g_cardPreview.contains("-- x\nWHERE 1=1\nAND b=2\nAND c=3\nAND d=4\nAND e=5"));
    r = callCard("git_commit", {{"message", QStringLiteral("fix: a\x1b[2Kb") + QChar(0x202E) + "cba"}}, false);
    EXPECT("R2: ESC and an RLO show as visible escapes, nothing deleted",
           g_cardPreview.contains("fix: a\\x1b[2Kb\\u202ecba"));

    // ── R17: the whole inserted text is reviewable ──
    {
        QString longText;
        for (int i = 0; i < 40; ++i) longText += QStringLiteral("line %1 of the inserted text\n").arg(i);
        longText += "LAST-LINE-MARKER";
        r = callCard("insert_text", {{"tab_index", 0}, {"text", longText}}, false);
        EXPECT("R17: insert_text card shows the whole text in the scrollable box",
               g_cardPreview.contains("line 0 of") && g_cardPreview.contains("LAST-LINE-MARKER") &&
                   !g_cardPreview.contains(QChar(0x2026)));
        QString huge;
        for (int i = 0; i < 20000; ++i) huge += QStringLiteral("row %1 xxxxxxxxxxxxxxxx\n").arg(i);
        r = callCard("insert_text", {{"tab_index", 0}, {"text", huge}}, false);
        EXPECT("R17: a huge text shows first and last lines and says how much is left out",
               g_cardPreview.contains("row 0 ") && g_cardPreview.contains("row 19999 ") &&
                   g_cardPreview.contains("lines not shown") && g_cardWarn.contains("leaves out"));
    }

    // ── R7 / R18: close_tab ──
    g_reports = false;
    r = callCard("close_tab", {{"tab_index", 0}}, false);
    EXPECT("R7: a tab type that cannot report unsaved work gets a card (never closed silently)",
           errOf(r) == "denied by user" && !ran("tab:close_tab") && !ran("tabedit:close_tab") &&
               g_cardDesc.contains("cannot report"));
    g_reports = true;
    g_modified = true;
    r = call("close_tab", {{"tab_index", 0}});
    EXPECT("R7: a modified tab is refused without discard_unsaved", !ok(r) && errOf(r).contains("unsaved"));
    r = callCard("close_tab", {{"tab_index", 0}, {"discard_unsaved", true}}, true,
                 []() { g_rev = "r1"; });
    EXPECT("R18: typing while the discard card waits refuses the close",
           !ok(r) && errOf(r).contains("edited while the approval card was waiting") &&
               !ran("tabedit:close_tab"));
    r = callCard("close_tab", {{"tab_index", 0}, {"discard_unsaved", true}}, true);
    EXPECT("R18: an unchanged tab closes on Approve", ok(r) && ran("tabedit:close_tab"));
    g_modified = false;

    // ── REST ──
    r = call("rest_request", {{"method", "GET"}, {"url", "file:///etc/passwd"}});
    EXPECT("rest_request refuses file://", !ok(r) && errOf(r).contains("only http"));
    r = call("rest_request", {{"method", "GET"}, {"url", "ftp://example.com/x"}});
    EXPECT("rest_request refuses ftp://", !ok(r));
    r = call("rest_request", {{"method", "GET"}, {"url", "http://x/"}, {"headers", QJsonObject{{"Bad Name", "v"}}}});
    EXPECT("rest_request refuses an invalid header name", !ok(r));
    r = callCard("rest_request", {{"method", "POST"}, {"url", "http://127.0.0.1:9/api/items?token=SECRETQ"},
                                  {"headers", QJsonObject{{"Authorization", "Bearer SECRETTOKEN"}}},
                                  {"body", "{\"a\":1}"}}, true);
    EXPECT("rest_request card: method, host, path, header names, body size; no secrets",
           ok(r) && g_cardDesc.contains("send HTTP POST to http://127.0.0.1:9/api/items") &&
               g_cardPreview.contains("loopback") && g_cardPreview.contains("Authorization") &&
               g_cardPreview.contains("7 bytes") && !g_cardPreview.contains("SECRETTOKEN") &&
               !g_cardDesc.contains("SECRETQ") && ran("rest:rest_request"));
    r = callCard("rest_request", {{"tab_index", 0}, {"line", 1}}, false);
    EXPECT("rest_request from a .http tab resolves before the card", g_cardDesc.contains("http://127.0.0.1/a"));
    r = callCard("rest_request", {{"method", "GET"}, {"url", "http://127.0.0.1:9/z"}}, true);
    EXPECT("R5: the host is handed the request resolved for the card, to send as is",
           ok(r) && g_lastArgs.value("resolved").toObject().value("url").toString() == "http://127.0.0.1:9/z" &&
               g_lastArgs.value("resolved").toObject().value("method").toString() == "GET" &&
               g_cardPreview.contains("not resolved again") && g_cardPreview.contains("request fields"));

    // ── leftovers ──
    r = call("hex_dump", {{"tab_index", 0}, {"length", 70000}});
    EXPECT("hex_dump caps the length", !ok(r));
    r = call("hex_dump", {{"path", home.path() + "/.ssh/id_rsa"}});
    EXPECT("hex_dump refuses a credential file", !ok(r) && errOf(r).contains("deny-list"));
    r = call("hex_dump", {{"path", repo + "/x.bin"}});
    EXPECT("hex_dump formats 16 bytes per line, xxd style",
           ok(r) && res(r).value("hex").toString().startsWith(
                        "00000000  48 65 6c 6c 6f 2c 20 77  6f 72 6c 64 21 0a"));
    r = callCard("clear_recent_files", {}, true);
    EXPECT("clear_recent_files card counts entries", ok(r) && g_cardDesc.contains("2 entries"));

    delete g_win;
    std::printf("\n%d passed, %d failed\n", g_pass, g_fail);
    return g_fail == 0 ? 0 : 1;
}
