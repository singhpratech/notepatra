// test_rest_client — the REST Client panel and the .http editor wiring,
// driven offscreen against fake HTTP servers on 127.0.0.1 (never the real
// network).
//
//   S1  Undefined {{variable}} blocks the send: no connection reaches the server.
//   S2  .http request + environment: variables expanded on the wire, response
//       body pretty-printed, Headers view shows status line + headers,
//       history stores the request UNEXPANDED in an owner-only file.
//   S3  Literal secrets are sent but never written to history (<redacted>).
//   S4  Re-load from history fills the panel and does not send; a <redacted>
//       value blocks the send; a history entry with variables re-sends.
//   S5  Cancel mid-response; Send/Cancel enable states; timeout.
//   S6  Binary response note; Save writes the raw bytes; large-body cap.
//   S7  A redirect to another host is not followed (Authorization stays home).
//   S8  Import cURL / Copy as cURL keep {{variables}} unexpanded.
//   S9  MainWindow: .http tab gets markers in margin 4 + Send Request
//       (Ctrl+Alt+R) enabled; it sends via the REST tab; other tabs disabled;
//       context menu entry; Open in tab makes a JSON editor tab.

#include "editor.h"
#include "http_editor_support.h"
#include "mainwindow.h"
#include "rest_history.h"
#include "restclient.h"
#include "tabmanager.h"

#include <QAction>
#include <QApplication>
#include <QComboBox>
#include <QDir>
#include <QFile>
#include <QHash>
#include <QHostAddress>
#include <QLabel>
#include <QMenu>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSignalSpy>
#include <QTabBar>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTemporaryDir>
#include <QTest>
#include <QTimer>
#include <QTreeWidget>

#include <cstdio>

static int g_pass = 0, g_fail = 0;
#define EXPECT(label, cond) \
    do { if (cond) { ++g_pass; std::printf("  [PASS] %s\n", label); } \
         else { ++g_fail; std::printf("  [FAIL] %s\n", label); } } while (0)

class RestClientTestAccess {
public:
    static QComboBox *method(RestClient *r) { return r->m_methodCombo; }
    static QLineEdit *url(RestClient *r) { return r->m_urlInput; }
    static QPlainTextEdit *headers(RestClient *r) { return r->m_headersInput; }
    static QPlainTextEdit *body(RestClient *r) { return r->m_bodyInput; }
    static QString badge(RestClient *r) { return r->m_statusBadge->text(); }
    static QString note(RestClient *r) { return r->m_note->isVisibleTo(r) ? r->m_note->text() : QString(); }
    static QTabBar *respTabs(RestClient *r) { return r->m_respTabs; }
    static QString respBody(RestClient *r) { return r->m_respBody->toPlainText(); }
    static QString respHeaders(RestClient *r) { return r->m_respHeaders->toPlainText(); }
    static QTreeWidget *history(RestClient *r) { return r->m_history; }
    static QPushButton *sendBtn(RestClient *r) { return r->m_sendBtn; }
    static QPushButton *cancelBtn(RestClient *r) { return r->m_cancelBtn; }
    static QPushButton *openTabBtn(RestClient *r) { return r->m_openTabBtn; }
    static QPushButton *saveBtn(RestClient *r) { return r->m_saveBtn; }
    static QComboBox *env(RestClient *r) { return r->m_envCombo; }
    static void loadHistory(RestClient *r, int row) { r->loadHistoryEntry(row); }
    static bool saveTo(RestClient *r, const QString &p) { return r->saveResponseTo(p, nullptr); }
    static void setTimeout(RestClient *r, int ms) { r->m_timeoutMs = ms; }
    static QString historyPath(RestClient *r) { return r->historyPath(); }
};
using TA = RestClientTestAccess;

// ── Fake HTTP server ───────────────────────────────────────────────────
class FakeHttp : public QObject {
public:
    QTcpServer srv;
    int connections = 0;
    QList<QByteArray> heads;  // request line + headers of every request
    QList<QByteArray> bodies;
    int redirectPort = 0;

    bool start() {
        connect(&srv, &QTcpServer::newConnection, this, [this]() {
            while (QTcpSocket *s = srv.nextPendingConnection()) {
                ++connections;
                connect(s, &QTcpSocket::readyRead, this, [this, s]() { onData(s); });
                connect(s, &QTcpSocket::disconnected, s, &QObject::deleteLater);
            }
        });
        return srv.listen(QHostAddress::LocalHost, 0);
    }
    QString base() const { return QStringLiteral("http://127.0.0.1:%1").arg(srv.serverPort()); }
    bool anyHeadContains(const QByteArray &n) const {
        for (const QByteArray &h : heads) if (h.contains(n)) return true;
        return false;
    }

private:
    QHash<QTcpSocket *, QByteArray> m_buf;

    static void reply(QTcpSocket *s, const QByteArray &status, const QByteArray &type,
                      const QByteArray &body, const QByteArray &extra = QByteArray()) {
        s->write("HTTP/1.1 " + status + "\r\nContent-Type: " + type + "\r\nContent-Length: " +
                 QByteArray::number(body.size()) + "\r\n" + extra + "Connection: close\r\n\r\n" + body);
        s->flush();
        s->disconnectFromHost();
    }

    void onData(QTcpSocket *s) {
        QByteArray &b = m_buf[s];
        b += s->readAll();
        const int he = b.indexOf("\r\n\r\n");
        if (he < 0) return;
        const QByteArray head = b.left(he);
        int cl = 0;
        for (const QByteArray &line : head.split('\n'))
            if (line.trimmed().toLower().startsWith("content-length:")) cl = line.trimmed().mid(15).trimmed().toInt();
        if (b.size() < he + 4 + cl) return;
        heads << head;
        bodies << b.mid(he + 4, cl);
        const QByteArray path = head.left(head.indexOf("\r\n")).split(' ').value(1);
        m_buf.remove(s);
        if (path.startsWith("/json")) {
            reply(s, "200 OK", "application/json", R"({"ok":true,"items":[1,2]})", "X-Test: yes\r\n");
        } else if (path.startsWith("/created")) {
            reply(s, "201 Created", "application/json", R"({"id":7})");
        } else if (path.startsWith("/slow")) {
            s->write("HTTP/1.1 200 OK\r\nContent-Type: text/plain\r\nContent-Length: 100000\r\n\r\npartial");
            s->flush();  // ...and never finish
        } else if (path.startsWith("/binary")) {
            QByteArray png("\x89PNG\r\n\x1a\n\0\0\0\rIHDR", 16);
            png += QByteArray(1000, '\x01');
            reply(s, "200 OK", "image/png", png);
        } else if (path.startsWith("/big")) {
            QByteArray big;
            big.reserve(3 * 1024 * 1024);
            while (big.size() < 3 * 1024 * 1024) big += "abcdefghij\n";
            reply(s, "200 OK", "text/plain; charset=utf-8", big);
        } else if (path.startsWith("/redirect")) {
            reply(s, "302 Found", "text/plain", "moved",
                  "Location: http://localhost:" + QByteArray::number(redirectPort) + "/landing\r\n");
        } else {
            reply(s, "404 Not Found", "text/plain", "nope");
        }
    }
};

static bool waitFinished(QSignalSpy &spy, int before, int ms = 10000) {
    return QTest::qWaitFor([&]() { return spy.count() > before; }, ms);
}

static void writeFile(const QString &path, const QByteArray &data) {
    QFile f(path);
    f.open(QIODevice::WriteOnly);
    f.write(data);
}

static QByteArray readFile(const QString &path) {
    QFile f(path);
    f.open(QIODevice::ReadOnly);
    return f.readAll();
}

int main(int argc, char *argv[]) {
    std::setvbuf(stdout, nullptr, _IONBF, 0);
    QTemporaryDir home;
    qputenv("HOME", home.path().toUtf8());
    qputenv("XDG_CONFIG_HOME", (home.path() + "/.config").toUtf8());
    qputenv("XDG_DATA_HOME", (home.path() + "/.local/share").toUtf8());
#ifdef Q_OS_WIN
    qputenv("APPDATA", (home.path() + "/AppData/Roaming").toUtf8());
    qputenv("LOCALAPPDATA", (home.path() + "/AppData/Local").toUtf8());
    qputenv("USERPROFILE", home.path().toUtf8());
#endif
    qputenv("QT_QPA_PLATFORM", "offscreen");
    QApplication app(argc, argv);
    QTimer::singleShot(170000, []() {
        std::printf("WATCHDOG: test_rest_client wedged\n");
        std::fflush(stdout);
        std::_Exit(3);
    });
    std::printf("=== test_rest_client ===\n");

    FakeHttp server, other;
    if (!server.start() || !other.start()) {
        std::printf("FAIL - could not listen on 127.0.0.1\n");
        return 2;
    }
    server.redirectPort = other.srv.serverPort();

    QTemporaryDir work;
    const QString httpPath = work.filePath("api.http");
    const QByteArray httpText =
        "@path = /json\n"
        "### Get json\n"
        "GET {{base}}{{path}}?id={{$randomInt 1 2}}\n"
        "Authorization: Bearer {{token}}\n"
        "Accept: application/json\n"
        "\n"
        "### Create\n"
        "POST {{base}}/created\n"
        "Content-Type: application/json\n"
        "\n"
        "{\"name\": \"Asha\"}\n";
    writeFile(httpPath, httpText);
    writeFile(work.filePath("http-client.env.json"),
              QStringLiteral(R"({"local": {"base": "%1"}, "other": {"base": "http://127.0.0.1:1"}})")
                  .arg(server.base()).toUtf8());
    writeFile(work.filePath("http-client.private.env.json"), R"({"local": {"token": "tok-fake-123"}})");

    auto *rest = new RestClient;
    rest->resize(1000, 800);
    rest->show();
    QSignalSpy finished(rest, &RestClient::requestFinished);

    // ── S1 ──
    std::printf("-- S1: undefined variable blocks the send --\n");
    rest->sendHttpRequest(QString::fromUtf8(httpText), 2, httpPath);
    QTest::qWait(300);
    EXPECT("S1: badge names the undefined variable", TA::badge(rest).contains("{{base}}") &&
                                                        TA::badge(rest).contains("Not sent"));
    EXPECT("S1: {{token}} reported too", TA::badge(rest).contains("{{token}}"));
    EXPECT("S1: no connection reached the server", server.connections == 0);
    // A valid URL with only a header variable undefined: a broken refusal
    // would really send "Bearer {{missingToken}}" here.
    rest->sendHttpRequest(QStringLiteral("GET %1/json\nAuthorization: Bearer {{missingToken}}\n")
                              .arg(server.base()), 0, httpPath);
    (void)QTest::qWaitFor([&]() { return server.connections > 0; }, 1000);
    EXPECT("S1: undefined header variable refused", TA::badge(rest).contains("{{missingToken}}"));
    EXPECT("S1: ...and no connection reached the server", server.connections == 0 && !rest->isBusy());
    EXPECT("S1: nothing recorded in history", RestHistory::load(TA::historyPath(rest)).isEmpty());
    EXPECT("S1: environment combo lists the env file entries",
           TA::env(rest)->count() == 3 && TA::env(rest)->itemText(0) == "No environment" &&
               TA::env(rest)->findData("local") > 0 && TA::env(rest)->findData("other") > 0);

    // ── S2 ──
    std::printf("-- S2: send with environment --\n");
    TA::env(rest)->setCurrentIndex(TA::env(rest)->findData("local"));
    int before = finished.count();
    rest->sendHttpRequest(QString::fromUtf8(httpText), 3, httpPath);
    EXPECT("S2: Send disabled / Cancel enabled while running",
           !TA::sendBtn(rest)->isEnabled() && TA::cancelBtn(rest)->isEnabled());
    EXPECT("S2: finished", waitFinished(finished, before));
    EXPECT("S2: variables expanded on the wire",
           server.anyHeadContains("GET /json?id=1 HTTP/1.1") && server.anyHeadContains("Bearer tok-fake-123"));
    EXPECT("S2: badge shows status, ms and size", TA::badge(rest).startsWith("200 OK") &&
                                                      TA::badge(rest).contains(" ms") && TA::badge(rest).contains(" B"));
    EXPECT("S2: JSON pretty-printed", TA::respBody(rest).contains("\"ok\": true") &&
                                          TA::respBody(rest).contains("\n"));
    TA::respTabs(rest)->setCurrentIndex(1);
    EXPECT("S2: Headers view has the status line and headers",
           TA::respHeaders(rest).startsWith("HTTP/1.1 200 OK") && TA::respHeaders(rest).contains("X-Test: yes"));
    TA::respTabs(rest)->setCurrentIndex(0);
    EXPECT("S2: Send enabled again, Cancel disabled", TA::sendBtn(rest)->isEnabled() && !TA::cancelBtn(rest)->isEnabled());
    EXPECT("S2: URL field keeps the unexpanded text", TA::url(rest)->text() == "{{base}}{{path}}?id={{$randomInt 1 2}}");
    const QString histPath = TA::historyPath(rest);
    const QByteArray hist1 = readFile(histPath);
    EXPECT("S2: history stores the request unexpanded",
           hist1.contains("{{base}}{{path}}") && hist1.contains("Bearer {{token}}"));
    EXPECT("S2: no expanded token in history", !hist1.contains("tok-fake-123"));
#ifndef Q_OS_WIN
    EXPECT("S2: history file is 0600", QFile::permissions(histPath) ==
                                           (QFileDevice::ReadOwner | QFileDevice::WriteOwner |
                                            QFileDevice::ReadUser | QFileDevice::WriteUser));
#endif
    EXPECT("S2: environment remembered for the folder",
           RestHistory::environmentFor(histPath, QFileInfo(httpPath).absolutePath()) == "local");
    EXPECT("S2: history row shows method, path, status",
           TA::history(rest)->topLevelItemCount() == 1 && TA::history(rest)->topLevelItem(0)->text(1) == "GET" &&
               TA::history(rest)->topLevelItem(0)->text(3) == "200");

    // ── S3 ──
    std::printf("-- S3: literal secrets are redacted in history --\n");
    TA::method(rest)->setCurrentText("POST");
    TA::url(rest)->setText(server.base() + "/created?api_key=QUERYSECRET5");
    TA::headers(rest)->setPlainText("Authorization: Bearer LITERALSECRET9\nX-Api-Key: KEYLITERAL7\nContent-Type: application/json");
    TA::body(rest)->setPlainText("{\"a\": 1}");
    before = finished.count();
    rest->send();
    EXPECT("S3: finished with 201", waitFinished(finished, before) && TA::badge(rest).startsWith("201 Created"));
    EXPECT("S3: the real request carried the secret", server.anyHeadContains("Bearer LITERALSECRET9"));
    const QByteArray hist2 = readFile(histPath);
    EXPECT("S3: no literal secret on disk", !hist2.contains("LITERALSECRET9") && !hist2.contains("KEYLITERAL7") &&
                                                !hist2.contains("QUERYSECRET5"));
    EXPECT("S3: <redacted> stored instead", hist2.contains("Authorization: <redacted>") &&
                                                hist2.contains("X-Api-Key: <redacted>"));

    // ── S4 ──
    std::printf("-- S4: re-load from history --\n");
    const int conns = server.connections;
    TA::history(rest)->clearSelection();
    TA::loadHistory(rest, 0);
    QTest::qWait(300);
    EXPECT("S4: entry loaded into the panel", TA::method(rest)->currentText() == "POST" &&
                                                  TA::headers(rest)->toPlainText().contains("Authorization: <redacted>") &&
                                                  TA::body(rest)->toPlainText() == "{\"a\": 1}");
    EXPECT("S4: loading did not send", server.connections == conns);
    EXPECT("S4: note tells the user to fill in redacted values", TA::note(rest).contains("<redacted>"));
    rest->send();
    QTest::qWait(300);
    EXPECT("S4: a <redacted> value blocks the send", TA::badge(rest).contains("redacted") && server.connections == conns);
    TA::loadHistory(rest, 1);
    EXPECT("S4: entry with variables loaded", TA::url(rest)->text() == "{{base}}{{path}}?id={{$randomInt 1 2}}");
    before = finished.count();
    rest->send();
    EXPECT("S4: history entry re-sends with the file's variables + remembered env",
           waitFinished(finished, before) && TA::badge(rest).startsWith("200") && server.connections == conns + 1);

    // ── S5 ──
    std::printf("-- S5: cancel + timeout --\n");
    TA::method(rest)->setCurrentText("GET");
    TA::headers(rest)->clear();
    TA::body(rest)->clear();
    TA::url(rest)->setText(server.base() + "/slow");
    before = finished.count();
    const int heads = server.heads.size();
    rest->send();
    (void)QTest::qWaitFor([&]() { return server.heads.size() > heads; }, 5000);
    QTest::qWait(200);  // headers + partial body have arrived
    EXPECT("S5: still running mid-response", rest->isBusy() && TA::cancelBtn(rest)->isEnabled());
    TA::cancelBtn(rest)->click();
    EXPECT("S5: cancel finishes the request", waitFinished(finished, before, 5000));
    EXPECT("S5: badge says Cancelled", TA::badge(rest).startsWith("Cancelled"));
    EXPECT("S5: Send enabled after cancel", TA::sendBtn(rest)->isEnabled() && !TA::cancelBtn(rest)->isEnabled());
    TA::setTimeout(rest, 400);
    before = finished.count();
    rest->send();
    EXPECT("S5: timeout finishes the request", waitFinished(finished, before, 5000));
    EXPECT("S5: badge says Timed out", TA::badge(rest).startsWith("Timed out"));
    TA::setTimeout(rest, 30000);

    // ── S6 ──
    std::printf("-- S6: binary + large responses --\n");
    TA::url(rest)->setText(server.base() + "/binary");
    before = finished.count();
    rest->send();
    EXPECT("S6: binary finished", waitFinished(finished, before));
    EXPECT("S6: binary note instead of bytes", TA::note(rest).contains("Binary response, ") && TA::note(rest).contains(" bytes (image/png)") &&
                                                   TA::respBody(rest).isEmpty());
    EXPECT("S6: Save enabled, Open in tab disabled", TA::saveBtn(rest)->isEnabled() && !TA::openTabBtn(rest)->isEnabled());
    const QString saved = work.filePath("out.png");
    EXPECT("S6: Save writes the raw bytes", TA::saveTo(rest, saved) && readFile(saved).size() == 1016 &&
                                                readFile(saved).startsWith("\x89PNG"));
    EXPECT("S6: suggested name from URL + type", RestClient::suggestedFileName(server.base() + "/binary", "image/png") == "binary.png");
    TA::url(rest)->setText(server.base() + "/big");
    before = finished.count();
    rest->send();
    EXPECT("S6: large finished", waitFinished(finished, before));
    EXPECT("S6: render capped with a note", TA::note(rest).contains("Showing the first 2.00 MB of 3.00 MB") &&
                                                TA::respBody(rest).size() <= RestClient::kRenderCap);
    QSignalSpy openSpy(rest, &RestClient::openInTabRequested);
    TA::openTabBtn(rest)->click();
    EXPECT("S6: Open in tab gets the whole body", openSpy.count() == 1 &&
                                                      openSpy.at(0).at(0).toString().size() >= 3 * 1024 * 1024 &&
                                                      openSpy.at(0).at(1).toString() == "Plain Text");

    // ── S7 ──
    std::printf("-- S7: redirect to another host is not followed --\n");
    TA::url(rest)->setText(server.base() + "/redirect");
    TA::headers(rest)->setPlainText("Authorization: Bearer REDIRSECRET3");
    before = finished.count();
    rest->send();
    EXPECT("S7: finished", waitFinished(finished, before));
    QTest::qWait(300);
    EXPECT("S7: 302 shown", TA::badge(rest).startsWith("302"));
    EXPECT("S7: note shows the Location", TA::note(rest).contains("Redirect not followed. Location: http://localhost:"));
    std::printf("     (other host: %d connection(s), saw the Authorization value: %s)\n", other.connections,
                other.anyHeadContains("REDIRSECRET3") ? "yes" : "no");
    EXPECT("S7: the other host got no connection (Authorization not forwarded)",
           other.connections == 0 && !other.anyHeadContains("REDIRSECRET3"));

    // ── S8 ──
    std::printf("-- S8: cURL --\n");
    EXPECT("S8: import fills the panel",
           rest->importCurl("curl -X PUT 'https://x.test/a' -H 'Authorization: Bearer {{token}}' -d 'v=1' -k") &&
               TA::method(rest)->currentText() == "PUT" && TA::url(rest)->text() == "https://x.test/a" &&
               TA::body(rest)->toPlainText() == "v=1");
    EXPECT("S8: -k note shown", TA::note(rest).contains("TLS certificate checks stay on"));
    TA::url(rest)->setText(server.base() + "/json");
    TA::headers(rest)->clear();
    TA::body(rest)->clear();
    TA::method(rest)->setCurrentText("GET");
    before = finished.count();
    rest->send();
    EXPECT("S8: imported request is not filed under the last .http request's name",
           waitFinished(finished, before) && RestHistory::load(histPath).value(0).name.isEmpty());
    rest->importCurl("curl -X PUT 'https://x.test/a' -H 'Authorization: Bearer {{token}}' -d 'v=1'");
    EXPECT("S8: Copy as cURL keeps {{token}}", rest->currentAsCurl().contains("Bearer {{token}}"));
    EXPECT("S8: bad command reported", !rest->importCurl("wget x") && TA::badge(rest).contains("Import failed"));

    delete rest;

    // ── S9 ──
    std::printf("-- S9: MainWindow wiring --\n");
    {
        const int conns9 = server.connections;
        MainWindow mw;
        mw.resize(1300, 900);
        mw.show();
        mw.runStartupNow();
        QApplication::processEvents();
        QAction *sendAct = mw.findChild<QAction *>("actSendHttpRequest");
        EXPECT("S9: Send Request action exists with Ctrl+Alt+R",
               sendAct && sendAct->text() == "Send Request" && sendAct->shortcut() == QKeySequence("Ctrl+Alt+R"));
        const QString txtPath = work.filePath("notes.txt");
        writeFile(txtPath, "GET http://127.0.0.1:1/\n");
        mw.openFile(txtPath);
        QApplication::processEvents();
        EXPECT("S9: disabled on a non-.http tab", sendAct && !sendAct->isEnabled());
        EXPECT("S9: no send margin on a non-.http tab", mw.currentEditor() &&
                                                           mw.currentEditor()->marginWidth(HttpEditorSupport::kMargin) == 0);
        mw.openFile(httpPath);
        QApplication::processEvents();
        Editor *ed = mw.currentEditor();
        EXPECT("S9: enabled on a .http tab", sendAct && sendAct->isEnabled());
        const int mask = 1 << HttpEditorSupport::kMarker;
        EXPECT("S9: marker on each request line only",
               ed && (ed->markersAtLine(2) & mask) && (ed->markersAtLine(7) & mask) &&
                   !(ed->markersAtLine(0) & mask) && !(ed->markersAtLine(3) & mask));
        EXPECT("S9: send margin visible", ed && ed->marginWidth(HttpEditorSupport::kMargin) > 0);
        EXPECT("S9: bookmark margin mask untouched", ed && ed->marginMarkerMask(1) == 1);
        QMenu menu;
        menu.addAction("Undo");
        emit ed->contextMenuAboutToShow(&menu, QPoint(5, 5));
        QAction *ctx = menu.findChild<QAction *>("ctxSendHttpRequest");
        EXPECT("S9: context menu gets Send Request first",
               ctx && menu.actions().value(0) == ctx && ctx->text() == "Send Request");
        ed->setCursorPosition(8, 0);  // inside "### Create"
        RestHistory::setEnvironmentFor(histPath, QFileInfo(httpPath).absolutePath(), "local");
        sendAct->trigger();
        QApplication::processEvents();
        auto *tabs = mw.findChild<TabManager *>();
        RestClient *panel = tabs ? qobject_cast<RestClient *>(tabs->currentWidget()) : nullptr;
        EXPECT("S9: REST Client tab opened and current", panel != nullptr);
        if (panel) {
            QSignalSpy fin(panel, &RestClient::requestFinished);
            EXPECT("S9: request under the caret was sent", QTest::qWaitFor([&]() { return fin.count() > 0 || !panel->isBusy(); }, 10000) &&
                                                                server.connections == conns9 + 1 &&
                                                                server.anyHeadContains("POST /created"));
            QTest::qWait(100);
            const int tabsBefore = tabs->count();
            TA::openTabBtn(panel)->click();
            QApplication::processEvents();
            EXPECT("S9: Open in tab adds a JSON editor tab", tabs->count() == tabsBefore + 1 && mw.currentEditor() &&
                                                                 mw.currentEditor()->language() == "JSON" &&
                                                                 mw.currentEditor()->text().contains("\"id\": 7"));
            // A second send reuses the same REST tab.
            int idx = -1;
            for (int i = 0; i < tabs->count(); ++i) if (tabs->widget(i) == ed) idx = i;
            tabs->setCurrentIndex(idx);
            QApplication::processEvents();
            ed->setCursorPosition(2, 0);
            sendAct->trigger();
            QApplication::processEvents();
            int restTabs = 0;
            for (int i = 0; i < tabs->count(); ++i) restTabs += qobject_cast<RestClient *>(tabs->widget(i)) ? 1 : 0;
            EXPECT("S9: second send reuses the REST tab", restTabs == 1 && tabs->currentWidget() == panel);
            (void)QTest::qWaitFor([&]() { return !panel->isBusy(); }, 10000);
        }
        for (Editor *e : mw.findChildren<Editor *>()) e->setModified(false);
    }

    std::printf("=== %d passed, %d failed ===\n", g_pass, g_fail);
    return g_fail == 0 ? 0 : 1;
}
