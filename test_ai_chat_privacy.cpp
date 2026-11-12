// test_ai_chat_privacy — v0.1.133 AI panel chat + privacy regressions.
//
// Drives a real AIPanel (offscreen) against a fake local Ollama server, so
// the assertions are on what actually leaves the machine and what lands in
// the transcript / on-disk history. No real Ollama or cloud is needed.
//
//   S1  A quick action with no model selected shows the notice WITHOUT
//       wiping the conversation, the saved history, or the typed input.
//   S2  Sending while a reply is still streaming keeps the partial reply
//       (finalised as-is, like Stop) before the new user turn.
//   S3  Cloud-free build: no cloud Settings button and the cloud settings
//       dialog cannot open (cloud build: both present, vacuity guard).
//   S4  Quick actions with Auto-attach off send only the selection, never
//       the rest of the file; Coding + Auto-attach on still sends the file.
//   S5  Data mode works from an attached or open CSV with no DB connection,
//       and the no-source notice does not tell anyone to install DuckDB.

#include "aipanel.h"
#include "config.h"

#include <QApplication>
#include <QDialog>
#include <QDir>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QFile>
#include <QHash>
#include <QHostAddress>
#include <QPlainTextEdit>
#include <QComboBox>
#include <QPushButton>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTemporaryDir>
#include <QTimer>
#include <QAbstractButton>

#include <cstdio>
#include <functional>

static int g_passed = 0;
static int g_failed = 0;

#define EXPECT(label, cond) \
    do { \
        if (cond) { ++g_passed; std::printf("  [PASS] %s\n", label); } \
        else { ++g_failed; std::printf("  [FAIL] %s\n", label); } \
    } while (0)

class AIPanelTestAccess {
public:
    using Msgs = QVector<AIPanel::ChatMessage>;
    static Msgs &active(AIPanel *p) { return p->activeMessages(); }
    static void send(AIPanel *p, const QString &action) { p->sendPrompt(action); }
    static QComboBox *combo(AIPanel *p) { return p->m_modelCombo; }
    static QPlainTextEdit *input(AIPanel *p) { return p->m_customInput; }
    static QString historyPath(AIPanel *p) { return p->m_chatHistoryPath; }
    static void saveNow(AIPanel *p) { p->saveChatHistoryNow(); }
    static bool inBubble(AIPanel *p) { return p->m_inAssistantBubble; }
    static QString streamed(AIPanel *p) { return p->m_currentAssistantText; }
    static void attach(AIPanel *p, const QString &path) {
        p->m_pendingFilePath = path;
        p->m_pendingFileKind = QStringLiteral("text");
    }
    static void openSettings(AIPanel *p) { p->openAiSettingsDialog(); }
    static bool setMode(AIPanel *p, int which) {
        QAbstractButton *b = which == 0 ? static_cast<QAbstractButton *>(p->m_chatMode)
                           : which == 1 ? static_cast<QAbstractButton *>(p->m_codingMode)
                                        : static_cast<QAbstractButton *>(p->m_dataMode);
        if (!b) return false;
        b->setChecked(true);
        return b->isChecked();
    }
};
using TA = AIPanelTestAccess;

// ── Minimal fake Ollama HTTP server ────────────────────────────────────
// Records every request body. /api/tags lists one model; chat/generate
// requests either answer immediately or "hold" (stream one token, keep the
// connection open) to simulate a reply in progress.
class FakeOllama : public QObject {
public:
    QTcpServer srv;
    QList<QByteArray> chatBodies;   // bodies of /api/generate + /api/chat
    bool holdNext = false;

    bool start() {
        connect(&srv, &QTcpServer::newConnection, this, [this]() {
            while (QTcpSocket *s = srv.nextPendingConnection()) {
                connect(s, &QTcpSocket::readyRead, this, [this, s]() { onData(s); });
                connect(s, &QTcpSocket::disconnected, this, [this, s]() {
                    m_bufs.remove(s);
                    s->deleteLater();
                });
            }
        });
        return srv.listen(QHostAddress::LocalHost, 0);
    }
    QString url() const { return QStringLiteral("http://127.0.0.1:%1").arg(srv.serverPort()); }
    bool anyBodyContains(const QByteArray &needle) const {
        for (const QByteArray &b : chatBodies)
            if (b.contains(needle)) return true;
        return false;
    }

private:
    QHash<QTcpSocket *, QByteArray> m_bufs;

    static void respond(QTcpSocket *s, const QByteArray &body) {
        s->write("HTTP/1.1 200 OK\r\nContent-Type: application/json\r\nContent-Length: "
                 + QByteArray::number(body.size()) + "\r\nConnection: close\r\n\r\n" + body);
        s->flush();
        s->disconnectFromHost();
    }

    void onData(QTcpSocket *s) {
        QByteArray &b = m_bufs[s];
        b += s->readAll();
        const int he = b.indexOf("\r\n\r\n");
        if (he < 0) return;
        const QByteArray head = b.left(he);
        int cl = 0;
        for (const QByteArray &line : head.split('\n')) {
            const QByteArray l = line.trimmed();
            if (l.toLower().startsWith("content-length:")) cl = l.mid(15).trimmed().toInt();
        }
        if (b.size() < he + 4 + cl) return;
        const QByteArray body = b.mid(he + 4, cl);
        const QByteArray path = head.left(head.indexOf("\r\n")).split(' ').value(1);
        b.clear();
        if (path.startsWith("/api/tags")) {
            respond(s, R"({"models":[{"name":"test-model","model":"test-model"}]})");
        } else if (path.startsWith("/api/generate") || path.startsWith("/api/chat")) {
            chatBodies << body;
            if (holdNext) {
                holdNext = false;
                const QByteArray line =
                    R"({"response":"PARTIAL_GAMMA","message":{"role":"assistant","content":"PARTIAL_GAMMA"},"done":false})" "\n";
                s->write("HTTP/1.1 200 OK\r\nContent-Type: application/x-ndjson\r\n"
                         "Transfer-Encoding: chunked\r\n\r\n"
                         + QByteArray::number(line.size(), 16) + "\r\n" + line + "\r\n");
                s->flush();
            } else {
                respond(s, R"({"response":"ok","message":{"role":"assistant","content":"ok"},"done":true})" "\n");
            }
        } else {
            respond(s, "{}");
        }
    }
};

static void pumpUntil(const std::function<bool()> &cond, int capMs) {
    QElapsedTimer t;
    t.start();
    while (!cond() && t.elapsed() < capMs) {
        QEventLoop l;
        QTimer::singleShot(20, &l, &QEventLoop::quit);
        l.exec();
    }
}
static void pump(int ms) { pumpUntil([] { return false; }, ms); }

static bool modelReady(AIPanel *p) {
    QComboBox *c = TA::combo(p);
    return c && c->isEnabled() && c->currentText() == QLatin1String("test-model");
}

static int countRole(AIPanel *p, AIPanel::ChatMessage::Role r, const QString &needle = QString()) {
    int n = 0;
    for (const auto &m : TA::active(p))
        if (m.role == r && (needle.isEmpty() || m.text.contains(needle))) ++n;
    return n;
}

static QByteArray readFile(const QString &path) {
    QFile f(path);
    return f.open(QIODevice::ReadOnly) ? f.readAll() : QByteArray();
}

// Waits for every in-flight reply to settle so sections don't bleed.
static void settle(AIPanel *p) {
    pumpUntil([p] { return !TA::inBubble(p); }, 5000);
    pump(100);
}

static void testNoModelKeepsHistory(AIPanel *p, const QString &ws) {
    std::printf("S1: quick action with no model keeps the conversation\n");
    p->setWorkspaceContext(QStringLiteral("int x = 1;"), ws + "/a.cpp", "C++",
                           QStringLiteral("int x = 1;"), {}, ws, {});
    TA::active(p).clear();
    AIPanel::ChatMessage u;  u.role = AIPanel::ChatMessage::User;      u.text = "EARLIER_QUESTION";
    AIPanel::ChatMessage a;  a.role = AIPanel::ChatMessage::Assistant; a.text = "EARLIER_ANSWER";
    TA::active(p) << u << a;
    TA::saveNow(p);
    const QString hist = TA::historyPath(p);
    EXPECT("history file path is set (vacuity guard)", !hist.isEmpty());
    EXPECT("history saved before the action", readFile(hist).contains("EARLIER_ANSWER"));

    QComboBox *c = TA::combo(p);
    c->clear();
    c->addItem(QStringLiteral("(no models installed)"));
    c->setEnabled(false);
    TA::input(p)->setPlainText(QStringLiteral("TYPED_DRAFT"));

    TA::send(p, QStringLiteral("explain"));
    pump(50);

    EXPECT("no-model notice shown", countRole(p, AIPanel::ChatMessage::Error, "No Ollama model") == 1);
    EXPECT("earlier user turn kept in transcript", countRole(p, AIPanel::ChatMessage::User, "EARLIER_QUESTION") == 1);
    EXPECT("earlier reply kept in transcript", countRole(p, AIPanel::ChatMessage::Assistant, "EARLIER_ANSWER") == 1);
    TA::saveNow(p);
    EXPECT("saved history on disk still has the earlier reply", readFile(hist).contains("EARLIER_ANSWER"));
    EXPECT("typed input not wiped", TA::input(p)->toPlainText() == QLatin1String("TYPED_DRAFT"));
    TA::input(p)->clear();
}

static void testSendWhileStreamingKeepsPartial(AIPanel *p, FakeOllama &srv) {
    std::printf("S2: sending mid-reply keeps the partial reply\n");
    TA::active(p).clear();
    srv.holdNext = true;
    TA::input(p)->setPlainText(QStringLiteral("FIRST_QUESTION"));
    TA::send(p, QStringLiteral("custom"));
    pumpUntil([p] { return TA::streamed(p).contains("PARTIAL_GAMMA"); }, 8000);
    EXPECT("reply is streaming (vacuity guard)",
           TA::inBubble(p) && TA::streamed(p).contains("PARTIAL_GAMMA"));

    TA::input(p)->setPlainText(QStringLiteral("SECOND_QUESTION"));
    TA::send(p, QStringLiteral("custom"));

    const auto &msgs = TA::active(p);
    int iFirst = -1, iPartial = -1, iSecond = -1;
    for (int i = 0; i < msgs.size(); ++i) {
        if (msgs[i].role == AIPanel::ChatMessage::User && msgs[i].text.contains("FIRST_QUESTION")) iFirst = i;
        if (msgs[i].role == AIPanel::ChatMessage::Assistant && msgs[i].text.contains("PARTIAL_GAMMA")) iPartial = i;
        if (msgs[i].role == AIPanel::ChatMessage::User && msgs[i].text.contains("SECOND_QUESTION")) iSecond = i;
    }
    EXPECT("partial reply kept in transcript", iPartial >= 0);
    EXPECT("order: first question, partial reply, second question",
           iFirst >= 0 && iFirst < iPartial && iPartial < iSecond);
    EXPECT("partial reply saved to history on disk",
           readFile(TA::historyPath(p)).contains("PARTIAL_GAMMA"));
    settle(p);
    EXPECT("second turn completed", countRole(p, AIPanel::ChatMessage::Assistant, "ok") >= 1);
}

static void testCloudSettingsGate(AIPanel *p) {
    std::printf("S3: cloud Settings button vs cloud-free build\n");
    int settingsButtons = 0;
    for (QPushButton *b : p->findChildren<QPushButton *>())
        if (b->text().contains(QLatin1String("Settings"))) ++settingsButtons;

    bool dialogSeen = false;
    auto closeModal = [&dialogSeen]() {
        if (QWidget *w = QApplication::activeModalWidget()) {
            dialogSeen = true;
            if (auto *d = qobject_cast<QDialog *>(w)) d->reject();
            else w->close();
        }
    };
#ifdef NOTEPATRA_NO_CLOUD
    EXPECT("cloud-free: no cloud Settings button", settingsButtons == 0);
    QTimer guard;
    QObject::connect(&guard, &QTimer::timeout, closeModal);
    guard.start(50);
    TA::openSettings(p);
    guard.stop();
    EXPECT("cloud-free: cloud settings dialog does not open", !dialogSeen);
#else
    EXPECT("cloud build: Settings button present (probe works)", settingsButtons == 1);
#ifndef Q_OS_WIN
    QTimer guard;
    QObject::connect(&guard, &QTimer::timeout, closeModal);
    guard.start(50);
    TA::openSettings(p);
    guard.stop();
    EXPECT("cloud build: settings dialog opens (probe works)", dialogSeen);
#endif
#endif
}

static void testQuickActionPrivacy(AIPanel *p, FakeOllama &srv, const QString &ws) {
    std::printf("S4: quick actions honour Auto-attach\n");
    const QString file = ws + "/secret.py";
    const QString fileText = QStringLiteral("FILE_SECRET_ALPHA = 1\nSELECTED_BETA = 2\n");
    Config::instance().aiShareOpenFile = false;
    EXPECT("chat mode", TA::setMode(p, 0));

    // No selection, Auto-attach off: nothing from the file may be sent.
    p->setWorkspaceContext(QString(), file, "Python", fileText, {}, ws, {});
    const int before = srv.chatBodies.size();
    const int errsBefore = countRole(p, AIPanel::ChatMessage::Error);
    TA::send(p, QStringLiteral("explain"));
    pump(300);
    settle(p);
    EXPECT("no selection + Auto-attach off: file text never sent",
           !srv.anyBodyContains("FILE_SECRET_ALPHA"));
    EXPECT("no selection + Auto-attach off: no request at all", srv.chatBodies.size() == before);
    EXPECT("no selection + Auto-attach off: user told to select code",
           countRole(p, AIPanel::ChatMessage::Error) == errsBefore + 1);
    EXPECT("file text not shown as a user turn",
           countRole(p, AIPanel::ChatMessage::User, "FILE_SECRET_ALPHA") == 0);

    // Selection, Auto-attach off: only the selection goes out.
    p->setWorkspaceContext(QStringLiteral("SELECTED_BETA = 2"), file, "Python", fileText, {}, ws, {});
    TA::send(p, QStringLiteral("explain"));
    pumpUntil([&] { return srv.chatBodies.size() > before; }, 5000);
    settle(p);
    EXPECT("selection is sent", srv.chatBodies.size() > before
                                && srv.chatBodies.last().contains("SELECTED_BETA"));
    EXPECT("rest of the file is not sent", !srv.anyBodyContains("FILE_SECRET_ALPHA"));

    // Coding mode + Auto-attach on: whole-file fallback still works.
    Config::instance().aiShareOpenFile = true;
    EXPECT("coding mode", TA::setMode(p, 1));
    p->setWorkspaceContext(QString(), file, "Python", fileText, {}, ws, {});
    const int before2 = srv.chatBodies.size();
    TA::send(p, QStringLiteral("explain"));
    pumpUntil([&] { return srv.chatBodies.size() > before2; }, 5000);
    settle(p);
    EXPECT("Coding + Auto-attach on: file is sent",
           srv.chatBodies.size() > before2 && srv.chatBodies.last().contains("FILE_SECRET_ALPHA"));
    Config::instance().aiShareOpenFile = false;
    TA::setMode(p, 0);
}

static void testDataModeCsv(AIPanel *p, FakeOllama &srv, const QString &ws) {
    std::printf("S5: Data mode works from a CSV with no DB connection\n");
    const QString csv = ws + "/sales.csv";
    {
        QFile f(csv);
        f.open(QIODevice::WriteOnly);
        f.write("region,revenue\nnorth,10\nsouth,20\n");
    }
    const QString notes = ws + "/notes.txt";
    {
        QFile f(notes);
        f.open(QIODevice::WriteOnly);
        f.write("hello\n");
    }
    EXPECT("data mode", TA::setMode(p, 2));

    // No CSV, no connection: refused, and the notice must not push DuckDB.
    p->setWorkspaceContext(QString(), notes, "Text", "hello\n", {}, ws, {});
    const int before = srv.chatBodies.size();
    TA::input(p)->setPlainText(QStringLiteral("total revenue?"));
    TA::send(p, QStringLiteral("custom"));
    pump(200);
    QString notice;
    for (const auto &m : TA::active(p))
        if (m.role == AIPanel::ChatMessage::Error) notice = m.text;
    EXPECT("no source: refused without a request", srv.chatBodies.size() == before);
    EXPECT("no source: notice shown (vacuity guard)", notice.contains(QLatin1String("Data Mode")));
    EXPECT("no source: notice does not tell users to add DuckDB",
           !notice.contains(QLatin1String("DuckDB")));
    EXPECT("no source: notice mentions CSV", notice.contains(QLatin1String("CSV")));
    TA::input(p)->clear();

    // Attached CSV, no connection: request goes out with the CSV preview.
    TA::attach(p, csv);
    TA::input(p)->setPlainText(QStringLiteral("ATTACHED_CSV_QUESTION"));
    TA::send(p, QStringLiteral("custom"));
    pumpUntil([&] { return srv.chatBodies.size() > before; }, 5000);
    settle(p);
    EXPECT("attached CSV: request sent", srv.chatBodies.size() > before
           && srv.chatBodies.last().contains("ATTACHED_CSV_QUESTION"));
    EXPECT("attached CSV: CSV preview included",
           srv.chatBodies.size() > before && srv.chatBodies.last().contains("region"));

    // Open CSV tab, no connection: request goes out naming the CSV.
    p->setWorkspaceContext(QString(), csv, "CSV", "region,revenue\n", {}, ws, {});
    const int before2 = srv.chatBodies.size();
    TA::input(p)->setPlainText(QStringLiteral("OPEN_CSV_QUESTION"));
    TA::send(p, QStringLiteral("custom"));
    pumpUntil([&] { return srv.chatBodies.size() > before2; }, 5000);
    settle(p);
    EXPECT("open CSV: request sent", srv.chatBodies.size() > before2
           && srv.chatBodies.last().contains("OPEN_CSV_QUESTION"));
    EXPECT("open CSV: csv_query source named",
           srv.chatBodies.size() > before2 && srv.chatBodies.last().contains("sales.csv"));
    TA::setMode(p, 0);
}

int main(int argc, char *argv[]) {
    // Isolate every config/history write from the real user profile.
    QTemporaryDir home;
    if (!home.isValid()) return 2;
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
    std::setvbuf(stdout, nullptr, _IONBF, 0);

    FakeOllama srv;
    if (!srv.start()) {
        std::printf("could not start fake server\n");
        return 2;
    }
    Config &cfg = Config::instance();
    cfg.aiDataMode = false;
    cfg.aiBackend = QStringLiteral("Ollama");
    cfg.aiBaseUrl = srv.url();
    cfg.aiShareOpenFile = false;

    QTemporaryDir ws;
    const QString wsPath = QDir(ws.path()).absolutePath();

    std::printf("=== AI panel chat + privacy (v0.1.133) ===\n");
    {
        AIPanel panel;
        pumpUntil([&] { return modelReady(&panel); }, 8000);
        EXPECT("fake server model listed (setup)", modelReady(&panel));

        testCloudSettingsGate(&panel);
        testNoModelKeepsHistory(&panel, wsPath);
        // Restore a selectable model for the remaining sections.
        TA::combo(&panel)->clear();
        TA::combo(&panel)->addItem(QStringLiteral("test-model"));
        TA::combo(&panel)->setEnabled(true);
        testSendWhileStreamingKeepsPartial(&panel, srv);
        testQuickActionPrivacy(&panel, srv, wsPath);
        testDataModeCsv(&panel, srv, wsPath);
        settle(&panel);
    }

    std::printf("\n=== %d passed · %d failed ===\n", g_passed, g_failed);
    return g_failed == 0 ? 0 : 1;
}
