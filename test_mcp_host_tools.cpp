// SPDX-License-Identifier: GPL-3.0-or-later
//
// v0.1.133 MCP tools against the REAL MainWindow host: a socket client talks
// to the bridge the window serves, clicks the real approval card, and checks
// the editor buffers. This is where "the same function the menu runs" and
// "one undo step" are proved, not in the fake-host bridge test. Offscreen,
// isolated HOME/XDG (+APPDATA/USERPROFILE on Windows); never a modal.

#include "mainwindow.h"
#include "editor.h"
#include "tabmanager.h"
#include "config.h"
#include "mcp_bridge.h"

#include <QAction>
#include <QApplication>
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
#include <QMenu>
#include <QPushButton>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QThread>

#include <cstdio>

static int g_pass = 0, g_fail = 0;
#define EXPECT(label, cond) \
    do { if (cond) { ++g_pass; std::printf("  [PASS] %s\n", label); } \
         else      { ++g_fail; std::printf("  [FAIL] %s\n", label); } \
         std::fflush(stdout); } while (0)

namespace {
MainWindow *g_mw = nullptr;
QLocalSocket *g_sock = nullptr;
int g_id = 0;

void pump(int ms) {
    QElapsedTimer t;
    t.start();
    while (t.elapsed() < ms) {
        QApplication::processEvents(QEventLoop::AllEvents, 10);
        QThread::msleep(2);
    }
}

QJsonObject readObj(int timeoutMs = 8000) {
    QElapsedTimer t;
    t.start();
    while (!g_sock->canReadLine() && t.elapsed() < timeoutMs) pump(5);
    if (!g_sock->canReadLine()) return QJsonObject();
    return QJsonDocument::fromJson(g_sock->readLine().trimmed()).object();
}

void send(const QString &verb, const QJsonObject &args) {
    QJsonObject req{{"id", ++g_id}, {"verb", verb}, {"args", args}};
    g_sock->write(QJsonDocument(req).toJson(QJsonDocument::Compact) + '\n');
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

QString lastCardText;

// Write verb: wait for the card, click Approve or Deny, return the response.
QJsonObject callCard(const QString &verb, const QJsonObject &args, bool approve) {
    send(verb, args);
    QFrame *card = liveCard();
    if (!card) return QJsonObject{{"no_card", true}};
    if (auto *d = card->findChild<QLabel *>(QStringLiteral("mcpApprovalDesc")))
        lastCardText = d->text();
    card->findChild<QPushButton *>(approve ? QStringLiteral("mcpApproveBtn")
                                           : QStringLiteral("mcpDenyBtn"))
        ->click();
    const QJsonObject r = readObj();
    pump(50);
    return r;
}

bool ok(const QJsonObject &r) { return r.value(QLatin1String("ok")).toBool(); }
QJsonObject res(const QJsonObject &r) { return r.value(QLatin1String("result")).toObject(); }
QString errOf(const QJsonObject &r) { return r.value(QLatin1String("error")).toString(); }

bool writeFile(const QString &path, const QByteArray &bytes) {
    QFile f(path);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate)) return false;
    return f.write(bytes) == bytes.size();
}

Editor *editorById(qint64 tid) {
    auto *tm = g_mw->findChild<TabManager *>();
    for (int i = 0; i < tm->count(); ++i)
        if (tm->widget(i)->property("npMcpTabId").toLongLong() == tid)
            return tm->editorAt(i);
    return nullptr;
}

qint64 openFile(const QString &path) {
    return qint64(res(call(QStringLiteral("open_file"), {{"path", path}}))
                      .value(QLatin1String("tab_id")).toDouble(-1));
}
} // namespace

int main(int argc, char *argv[]) {
    QTemporaryDir cfg;
    qputenv("HOME", cfg.path().toUtf8());
    qputenv("XDG_CONFIG_HOME", cfg.path().toUtf8());
    qputenv("XDG_DATA_HOME", cfg.path().toUtf8());
#ifdef Q_OS_WIN
    qputenv("APPDATA", cfg.path().toUtf8());
    qputenv("USERPROFILE", cfg.path().toUtf8());
#endif
    qputenv("QT_QPA_PLATFORM", "offscreen");
    QStandardPaths::setTestModeEnabled(true);
    QApplication app(argc, argv);
    std::printf("=== test_mcp_host_tools ===\n");

    QDir().mkpath(Config::appConfigDir());
    writeFile(Config::appConfigDir() + QStringLiteral("/config.json"),
              "{\"showWelcomeOnStartup\": false, \"autoSaveIntervalSec\": 300}\n");
    QTemporaryDir work;
    const QString d = work.path();

    g_mw = new MainWindow();
    g_mw->resize(900, 600);
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
    EXPECT("greeting", readObj().value(QLatin1String("notepatra_mcp")).toInt() == 1);

    // ── transform_lines: the Line Operations function, one undo step ──
    writeFile(d + "/lines.txt", "b\na\nc\n");
    const qint64 lt = openFile(d + "/lines.txt");
    Editor *le = editorById(lt);
    EXPECT("open_file returned the tab_id of an editor", le != nullptr);
    if (!le) return 1;
    QJsonObject r = callCard("transform_lines", {{"tab_id", lt}, {"op", "sort_asc"}}, false);
    EXPECT("transform_lines Deny leaves the buffer", le->text() == "b\na\nc\n" && !le->isModified());
    EXPECT("card names tool, tab, op and scope",
           lastCardText.contains("transform_lines in 'lines.txt': sort_asc on the whole document"));
    r = callCard("transform_lines", {{"tab_id", lt}, {"op", "sort_asc"}}, true);
    // RustCore::sortLines drops the final newline; the menu does the same.
    EXPECT("transform_lines Approve sorts", ok(r) && le->text() == "a\nb\nc");
    EXPECT("the tab is modified, like the menu leaves it", le->isModified());
    r = callCard("undo", {{"tab_id", lt}}, true);
    EXPECT("ONE undo step restores the original", ok(r) && le->text() == "b\na\nc\n");
    r = callCard("redo", {{"tab_id", lt}}, true);
    EXPECT("redo re-applies it", ok(r) && le->text() == "a\nb\nc");
    r = callCard("transform_lines", {{"tab_id", lt}, {"op", "reverse"}}, true);
    EXPECT("reverse (same RustCore call as the menu)", le->text() == "c\nb\na");
    // The menu path on the same buffer gives the same result.
    {
        QAction *menuSortAsc = nullptr;
        for (QAction *a : g_mw->findChildren<QAction *>())
            if (a->text() == QStringLiteral("Sort Lexicographically &Ascending")) menuSortAsc = a;
        g_mw->findChild<TabManager *>()->setCurrentWidget(le);
        pump(20);
        if (menuSortAsc) menuSortAsc->trigger();
        EXPECT("Edit > Line Operations > Sort Asc still works and matches MCP",
               menuSortAsc && le->text() == "a\nb\nc");
    }

    // ── convert_case on the selection (the menu's default scope) ──
    le->setSelection(0, 0, 0, 1);
    r = callCard("convert_case", {{"tab_id", lt}, {"mode", "upper"}}, true);
    EXPECT("convert_case upper on the selection", ok(r) && le->text().startsWith("A\nb"));
    le->selectAll(false);
    r = call("convert_case", {{"tab_id", lt}, {"mode", "upper"}, {"scope", "selection"}});
    EXPECT("convert_case with nothing selected is refused before a card",
           !ok(r) && r.value("error").toString() == "no selection");

    // ── apply_edit: a literal replacement never expands $1 ──
    writeFile(d + "/price.txt", "price X and x\n");
    const qint64 pt = openFile(d + "/price.txt");
    Editor *pe = editorById(pt);
    r = callCard("apply_edit", {{"tab_id", pt}, {"find", "X"}, {"replace", "$1.00"}}, true);
    EXPECT("default literal path: $1 stays $1", ok(r) && pe->text() == "price $1.00 and x\n");
    r = callCard("apply_edit", {{"tab_id", pt}, {"find", "x"}, {"replace", "$1"},
                                {"match_case", false}, {"whole_word", true}, {"all", true}}, true);
    EXPECT("whole-word literal through the Replace engine: $1 stays $1",
           ok(r) && pe->text() == "price $1.00 and $1\n");
    r = callCard("apply_edit", {{"tab_id", pt}, {"find", "(\\d)\\.00"}, {"replace", "$1.50"},
                                {"regex", true}}, true);
    EXPECT("regex mode expands $1", ok(r) && pe->text() == "price $1.50 and $1\n");
    // all_open: one card, every tab and its count.
    r = callCard("apply_edit", {{"find", "and"}, {"replace", "&"}, {"all", true},
                                {"scope", "all_open"}, {"match_case", false}}, false);
    EXPECT("all_open card lists the scope", lastCardText.contains("open tab(s)"));
    EXPECT("all_open Deny changed nothing", pe->text() == "price $1.50 and $1\n");

    // ── comment_lines / line_edit / set_eol on a Python file ──
    writeFile(d + "/a.py", "x = 1\ny = 2\n");
    const qint64 py = openFile(d + "/a.py");
    Editor *pye = editorById(py);
    r = callCard("comment_lines", {{"tab_id", py}, {"start_line", 1}, {"end_line", 2},
                                   {"action", "comment"}}, true);
    EXPECT("comment_lines uses the language's line comment",
           ok(r) && pye->text() == "# x = 1\n# y = 2\n");
    r = callCard("line_edit", {{"tab_id", py}, {"line", 1}, {"op", "duplicate"}}, true);
    EXPECT("line_edit duplicate", ok(r) && pye->lines() >= 4 && pye->text().startsWith("# x = 1\n# x = 1\n"));
    r = callCard("set_eol", {{"tab_id", py}, {"eol", "crlf"}}, true);
    EXPECT("set_eol crlf converts", ok(r) && pye->eolModeName() == "Windows (CR LF)" &&
                                        pye->text().contains("\r\n"));
    r = call("comment_lines", {{"tab_id", lt}, {"start_line", 1}, {"end_line", 1}});
    // lines.txt is Plain Text: no comment syntax, refused (card or not, nothing changes).
    if (QFrame *c = liveCard(300)) {
        c->findChild<QPushButton *>(QStringLiteral("mcpApproveBtn"))->click();
        r = readObj();
    }
    EXPECT("comment_lines on a language without comments fails", !ok(r));

    // ── pure tools through the real engines ──
    r = call("hash_text", {{"text", "abc"}, {"algo", "sha256"}});
    EXPECT("hash_text sha256('abc')",
           res(r).value("hash").toString() ==
               "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
    r = call("encode_text", {{"text", "a b"}, {"op", "url_encode"}});
    EXPECT("encode_text url", ok(r) && res(r).value("text").toString().contains("%20"));
    r = call("diff_text", {{"left", QJsonObject{{"text", "a\nb\n"}}},
                           {"right", QJsonObject{{"text", "a\nc\n"}}}});
    EXPECT("diff_text returns a hunk", ok(r) && res(r).value("hunks").toArray().size() == 1);
    r = call("list_symbols", {{"text", "class A:\n    def f(self):\n        pass\n"},
                              {"language", "Python"}});
    EXPECT("list_symbols via the Function List parser",
           res(r).value("symbols").toArray().size() == 2);
    r = call("markdown_convert", {{"text", "a,b\n1,2"}, {"op", "table"}});
    EXPECT("markdown_convert table",
           res(r).value("text").toString() == "| a | b |\n| --- | --- |\n| 1 | 2 |\n");
    r = call("check_brackets", {{"text", "f(x"}});
    EXPECT("check_brackets reports", ok(r) && !res(r).value("report").toString().isEmpty());
    r = call("fix_brackets", {{"tab_id", lt}});
    EXPECT("fix_brackets on a tab does not change it", ok(r) && le->text().startsWith("A\n"));
    r = call("fix_brackets", {{"text", "f(x\ng(y"}});
    EXPECT("R21b: fix_brackets reports what the FIXER inserted, with line and column",
           ok(r) && res(r).value("changed").toBool() &&
               res(r).value("report").toString().startsWith("Fixed") &&
               res(r).value("report").toString().contains("inserted ')'") &&
               res(r).value("report").toString().contains("line "));
    r = call("generate_password", {{"length", 16}, {"count", 2}});
    const QJsonArray pw = res(r).value("passwords").toArray();
    EXPECT("generate_password", pw.size() == 2 && pw.at(0).toString().size() == 16);
    r = call("format_text", {{"kind", "json"}, {"text", "{\"a\":1}"}, {"indent", 2}});
    EXPECT("format_json indent 2", res(r).value("text").toString().contains("\n  \"a\""));
    r = call("format_text", {{"kind", "json"}, {"text", "[1,2"}, {"minify", true}});
    EXPECT("format_json still refuses invalid JSON with options", !ok(r));
    EXPECT("W1: the parse error is the formatter's own words, unprefixed",
           errOf(r).startsWith("not valid JSON: line 1") && !errOf(r).contains("invalid JSON"));
    {
        // Valid, 24 KB, but its pretty output (16-space indent, 12000 deep)
        // would be gigabytes: a size refusal, never "invalid", never success.
        const QString deep = QString(12000, QLatin1Char('[')) + QString(12000, QLatin1Char(']'));
        r = call("format_text", {{"kind", "json"}, {"text", deep}, {"indent", 16}});
        EXPECT("W1: a valid document too large to pretty-print is refused as a size limit",
               !ok(r) && !errOf(r).isEmpty() && !errOf(r).contains("invalid") &&
                   !errOf(r).startsWith("not valid"));
        r = call("format_text", {{"kind", "json"}, {"text", deep}});
        EXPECT("W1: ...also on the default (format_json) path",
               !ok(r) && !errOf(r).isEmpty() && !errOf(r).contains("invalid"));
    }
    r = call("repair_json", {{"text", "{'a': 1,}"}});
    EXPECT("repair_json is the explicit repair",
           ok(r) && res(r).value("fixed").toString().contains("\"a\"") &&
               !res(r).value("fixed").toString().contains(",}") &&
               res(r).value("repaired").toBool());
    r = call("repair_json", {{"text", "[1,2"}});
    EXPECT("repair_json declines a truncated document",
           ok(r) && res(r).value("fixed").toString() == QLatin1String("[1,2") &&
               !res(r).value("report").toString().isEmpty() &&
               !res(r).value("repaired").toBool(true));
    r = call("format_text", {{"kind", "html"}, {"text", "<p>a</p>\n<p>b</p>\n"}, {"minify", true}});
    EXPECT("format_html minify through the safe minifier",
           ok(r) && res(r).value("text").toString().trimmed() == QLatin1String("<p>a</p><p>b</p>"));
    r = call("format_text", {{"kind", "html"}, {"text", "<p>a</p>\n"}});
    EXPECT("format_html formats", ok(r));
    r = call("format_text", {{"kind", "html"}, {"text", "<div>\n  <p><b>x</p>\n</div>"}, {"indent", 2}});
    EXPECT("format_html decline is an error with the reason",
           !ok(r) && r.value("error").toString().contains("declined") &&
               r.value("error").toString().contains("line 2"));
    r = call("format_text", {{"kind", "html"}, {"text", "<div>\n  <p><b>x</p>\n</div>"}});
    EXPECT("format_html (no options) decline is an error too", !ok(r));

    // ── Markdown menu items: same function, same output ──
    {
        writeFile(d + "/m.md", "word\n");
        const qint64 mt = openFile(d + "/m.md");
        Editor *me = editorById(mt);
        g_mw->findChild<TabManager *>()->setCurrentWidget(me);
        me->setSelection(0, 0, 0, 4);
        QAction *bold = nullptr;
        for (QAction *a : g_mw->findChildren<QAction *>())
            if (a->text() == QString::fromUtf8("Selection → Bold")) bold = a;
        if (bold) bold->trigger();
        EXPECT("Tools > Markdown Converter > Bold still works", me->text() == "**word**\n");
        r = call("markdown_convert", {{"text", "word"}, {"op", "bold"}});
        EXPECT("markdown_convert bold equals the menu", res(r).value("text").toString() == "**word**");
    }

    // ── tabs: rename, close (no card when clean), read-only, theme ──
    r = callCard("rename_file", {{"tab_id", py}, {"new_name", "b.py"}}, true);
    EXPECT("rename_file renames on disk", ok(r) && QFileInfo::exists(d + "/b.py") &&
                                              !QFileInfo::exists(d + "/a.py"));
    r = call("rename_file", {{"tab_id", py}, {"new_name", ".bashrc"}});
    EXPECT("rename_file to a protected name is refused before a card", !ok(r));
    r = call("set_read_only", {{"tab_id", lt}, {"on", true}});
    EXPECT("set_read_only", ok(r) && le->isReadOnly());
    call("set_read_only", {{"tab_id", lt}, {"on", false}});
    r = call("set_theme", {{"theme", "Dark"}});
    EXPECT("set_theme Dark via the menu action", ok(r) && Config::instance().theme == "Dark");
    r = call("bookmarks", {{"tab_id", lt}, {"action", "toggle"}, {"line", 2}});
    EXPECT("bookmarks toggle", ok(r) && res(r).value("lines").toArray().contains(2));
    r = call("get_status");
    EXPECT("get_status has the v0.1.133 fields", res(r).contains("tab_id") &&
                                                       res(r).contains("eol") &&
                                                       res(r).contains("line_count"));
    r = call("find_in_tab", {{"tab_id", pt}, {"query", "AND"}, {"match_case", false}});
    EXPECT("find_in_tab column", res(r).value("matches").toArray().at(0).toObject()
                                         .value("column").toInt() == 13);
    writeFile(d + "/clean.txt", "c\n");
    const qint64 ct = openFile(d + "/clean.txt");
    r = call("close_tab", {{"tab_id", ct}});
    EXPECT("close_tab on a clean tab: no card, closed", ok(r) && !editorById(ct));
    r = call("close_tab", {{"tab_id", pt}});
    EXPECT("close_tab on a modified tab is refused", !ok(r) && editorById(pt));
    r = callCard("close_tab", {{"tab_id", pt}, {"discard_unsaved", true}}, true);
    EXPECT("close_tab discard after approval", ok(r) && !editorById(pt));
    r = call("activate_tab", {{"tab_id", ct}});
    EXPECT("a closed tab's id is an error", !ok(r));

    delete g_mw;
    std::printf("\n%d passed, %d failed\n", g_pass, g_fail);
    return g_fail == 0 ? 0 : 1;
}
