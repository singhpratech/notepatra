// test_openai_param_retry — OpenAI-compatible requests used to always send
// `max_tokens` and `temperature`. Some models reject both (they want
// `max_completion_tokens` and only the default temperature) and answer
// HTTP 400 naming the parameter, so chat never worked with them.
//
// The fix reads the 400 body (no model-name matching): it re-sends once per
// named parameter and remembers the fix per backend + URL + model for the
// session. A fake local server plays the strict provider; offline-safe.

#include <QCoreApplication>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QHash>
#include <QHostAddress>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTemporaryDir>
#include <QTimer>
#include <cstdio>

#include "config.h"
#include "ollama.h"

static int g_pass = 0, g_fail = 0;
#define EXPECT(label, cond) \
    do { if (cond) { ++g_pass; std::printf("  [PASS] %s\n", label); } \
         else      { ++g_fail; std::printf("  [FAIL] %s\n", label); } } while (0)

// Strict fake provider. Policy per model (taken from the request body):
//   "strict-model": rejects max_tokens and any temperature (OpenAI style)
//   "picky-model":  rejects max_tokens, but its error text says so only in
//                   the message (no `param` field)
//   "broken-model": always 400 for an unrelated reason
//   "stubborn":     rejects max_tokens even after the switch (loop guard)
//   anything else:  accepts the request
class StrictProvider : public QObject {
public:
    QTcpServer srv;
    QList<QJsonObject> bodies;

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

private:
    QHash<QTcpSocket *, QByteArray> m_bufs;

    static void send(QTcpSocket *s, int status, const QByteArray &type, const QByteArray &body) {
        s->write("HTTP/1.1 " + QByteArray::number(status) + (status == 200 ? " OK" : " Bad Request")
                 + "\r\nContent-Type: " + type + "\r\nContent-Length: "
                 + QByteArray::number(body.size()) + "\r\nConnection: close\r\n\r\n" + body);
        s->flush();
        s->disconnectFromHost();
    }
    static QByteArray err(const char *param, const char *code, const QByteArray &msg) {
        QJsonObject e{{"message", QString::fromUtf8(msg)}, {"type", "invalid_request_error"},
                      {"code", code}};
        if (param) e.insert("param", param);
        // Pretty-printed with a blank line, like some proxies emit: must not be
        // eaten by the SSE frame splitter.
        return "{\n\n  \"error\": " + QJsonDocument(e).toJson(QJsonDocument::Compact) + "\n}\n";
    }

    void onData(QTcpSocket *s) {
        QByteArray &b = m_bufs[s];
        b += s->readAll();
        const int he = b.indexOf("\r\n\r\n");
        if (he < 0) return;
        int cl = 0;
        for (const QByteArray &line : b.left(he).split('\n')) {
            const QByteArray l = line.trimmed();
            if (l.toLower().startsWith("content-length:")) cl = l.mid(15).trimmed().toInt();
        }
        if (b.size() < he + 4 + cl) return;
        const QJsonObject body = QJsonDocument::fromJson(b.mid(he + 4, cl)).object();
        b.clear();
        bodies << body;
        const QString model = body.value("model").toString();

        if (model == "broken-model") {
            send(s, 400, "application/json",
                 err("messages", "invalid_value", "Invalid value for 'messages'."));
            return;
        }
        if (model == "stubborn" && (body.contains("max_tokens") || body.contains("max_completion_tokens"))) {
            send(s, 400, "application/json",
                 err("max_tokens", "unsupported_parameter",
                     "Unsupported parameter: 'max_tokens' is not supported with this model."));
            return;
        }
        if ((model == "strict-model" || model == "picky-model") && body.contains("max_tokens")) {
            send(s, 400, "application/json",
                 model == "strict-model"
                     ? err("max_tokens", "unsupported_parameter",
                           "Unsupported parameter: 'max_tokens' is not supported with this "
                           "model. Use 'max_completion_tokens' instead.")
                     : err(nullptr, "", "max_tokens is not supported by this model"));
            return;
        }
        if (model == "strict-model" && body.contains("temperature")) {
            send(s, 400, "application/json",
                 err("temperature", "unsupported_value",
                     "Unsupported value: 'temperature' does not support 0.3 with this "
                     "model. Only the default (1) value is supported."));
            return;
        }
        send(s, 200, "text/event-stream",
             "data: {\"choices\":[{\"delta\":{\"content\":\"hello\"}}]}\n\n"
             "data: {\"choices\":[{\"delta\":{},\"finish_reason\":\"stop\"}]}\n\n"
             "data: [DONE]\n\n");
    }
};

struct Outcome {
    int errors = 0, finishes = 0;
    QString text, lastError;
};

static Outcome run(StrictProvider &p, const QString &model) {
    OllamaClient client;
    client.setBackend(OllamaClient::OpenAICompat);
    client.setBaseUrl(p.url());
    client.setModel(model);
    Outcome o;
    QEventLoop loop;
    QObject::connect(&client, &OllamaClient::error, [&](const QString &m) {
        ++o.errors; o.lastError = m; loop.quit();
    });
    QObject::connect(&client, &OllamaClient::finished, [&](const QString &t) {
        ++o.finishes; o.text = t; loop.quit();
    });
    QTimer::singleShot(10000, &loop, &QEventLoop::quit);
    client.generate("hi");
    loop.exec();
    QEventLoop settle;
    QTimer::singleShot(200, &settle, &QEventLoop::quit);
    settle.exec();
    return o;
}

int main(int argc, char **argv) {
    QTemporaryDir home;
    if (!home.isValid()) return 2;
    qputenv("HOME", home.path().toUtf8());
    qputenv("XDG_CONFIG_HOME", (home.path() + "/.config").toUtf8());
#ifdef Q_OS_WIN
    qputenv("APPDATA", (home.path() + "/AppData/Roaming").toUtf8());
    qputenv("USERPROFILE", home.path().toUtf8());
#endif
    QCoreApplication app(argc, argv);
    std::setvbuf(stdout, nullptr, _IONBF, 0);
    Config::instance().aiBackend = QStringLiteral("llama.cpp");  // no cloud key / reasoning field
    Config::instance().aiInteractionLogging = false;

    StrictProvider p;
    if (!p.start()) return 2;
    std::printf("=== OpenAI-compat rejected-parameter retry ===\n");

    std::printf(" Section 1: strict model rejecting max_tokens and temperature\n");
    {
        const Outcome o = run(p, "strict-model");
        EXPECT("reply delivered", o.finishes == 1 && o.text == "hello");
        EXPECT("no error surfaced", o.errors == 0);
        EXPECT("three requests (original + one per rejected param)", p.bodies.size() == 3);
        const QJsonObject last = p.bodies.value(p.bodies.size() - 1);
        EXPECT("final request uses max_completion_tokens = 2048",
               last.value("max_completion_tokens").toInt() == 2048);
        EXPECT("final request drops max_tokens", !last.contains("max_tokens"));
        EXPECT("final request drops temperature", !last.contains("temperature"));
        EXPECT("conversation kept on re-send", last.value("messages").toArray().size()
                                                   == p.bodies.first().value("messages").toArray().size());
    }

    std::printf(" Section 2: fix remembered for the session (same backend+model)\n");
    {
        p.bodies.clear();
        const Outcome o = run(p, "strict-model");
        EXPECT("reply delivered", o.finishes == 1 && o.errors == 0);
        EXPECT("single request, already fixed", p.bodies.size() == 1
               && !p.bodies.first().contains("max_tokens")
               && !p.bodies.first().contains("temperature"));
    }

    std::printf(" Section 3: other models keep the default parameters\n");
    {
        p.bodies.clear();
        const Outcome o = run(p, "plain-model");
        EXPECT("reply delivered", o.finishes == 1 && o.errors == 0);
        EXPECT("plain model sends max_tokens + temperature", p.bodies.size() == 1
               && p.bodies.first().contains("max_tokens")
               && p.bodies.first().contains("temperature"));
    }

    std::printf(" Section 4: error that names the param only in its message\n");
    {
        p.bodies.clear();
        const Outcome o = run(p, "picky-model");
        EXPECT("reply delivered", o.finishes == 1 && o.errors == 0);
        EXPECT("two requests, temperature kept", p.bodies.size() == 2
               && p.bodies.last().contains("max_completion_tokens")
               && p.bodies.last().contains("temperature"));
    }

    std::printf(" Section 5: unrelated 400 is not retried\n");
    {
        p.bodies.clear();
        const Outcome o = run(p, "broken-model");
        EXPECT("exactly one error", o.errors == 1 && o.finishes == 0);
        EXPECT("single request", p.bodies.size() == 1);
        EXPECT("provider message surfaced", o.lastError.contains("Invalid value"));
    }

    std::printf(" Section 6: a param rejected again is not retried forever\n");
    {
        p.bodies.clear();
        const Outcome o = run(p, "stubborn");
        EXPECT("exactly one error", o.errors == 1 && o.finishes == 0);
        EXPECT("at most one re-send", p.bodies.size() == 2);
    }

    std::printf("\n=== %d passed · %d failed ===\n", g_pass, g_fail);
    return g_fail == 0 ? 0 : 1;
}
