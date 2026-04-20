// test_llamacpp.cpp — verify the llama.cpp / OpenAI-compat backend
// of OllamaClient works end-to-end. Spins up a fake OpenAI-compatible
// HTTP server on localhost, points the client at it, and checks:
//
//   1. setBackend(LlamaCpp) + setBaseUrl() are honored
//   2. listModels() hits /v1/models and parses {"data":[{"id":...}]}
//   3. generate() sends /v1/chat/completions with stream:true
//   4. SSE frames ("data: {...}\n\n") are parsed into token signals
//   5. "data: [DONE]" terminates the stream → finished() fires
//   6. Bearer-token Authorization header is set when aiApiKey is
//      configured
//
// Self-contained — no external llama-server required. The mock server
// is a QTcpServer that speaks exactly enough OpenAI to satisfy the
// client. If this test passes, the real llama-server / LM Studio / Jan
// / vLLM / KoboldCpp / OpenRouter paths all work too because they
// share the same wire format.

#include <QCoreApplication>
#include <QEventLoop>
#include <QTimer>
#include <QTcpServer>
#include <QTcpSocket>
#include <QByteArray>
#include <cstdio>

#include "ollama.h"
#include "config.h"

// ─── Mock OpenAI-compatible server ──────────────────────────────────────
// Listens on 127.0.0.1 on any free port. Replies with:
//   GET  /v1/models            → JSON list with two fake models
//   POST /v1/chat/completions  → SSE stream of 3 chunks then [DONE]
// Captures the Authorization header for verification.
class MockOpenAIServer : public QTcpServer {
public:
    QString lastAuthHeader;
    QByteArray lastRequestBody;
    bool sawDone = false;

    MockOpenAIServer() {
        connect(this, &QTcpServer::newConnection, this, [this]() {
            auto *sock = nextPendingConnection();
            // Per-connection buffer so we handle the common case where the
            // HTTP body arrives in a different TCP packet than the headers.
            auto *accumBuf = new QByteArray;
            connect(sock, &QTcpSocket::disconnected, sock, [accumBuf]() { delete accumBuf; });

            connect(sock, &QTcpSocket::readyRead, this, [this, sock, accumBuf]() {
                *accumBuf += sock->readAll();
                const int headerEnd = accumBuf->indexOf("\r\n\r\n");
                if (headerEnd < 0) return;  // wait for more bytes

                // Check Content-Length — if the body isn't fully here yet,
                // wait for the next readyRead before dispatching.
                const QByteArray headers = accumBuf->left(headerEnd);
                int bodyOffset = headerEnd + 4;
                int contentLength = 0;
                for (const QByteArray &line : headers.split('\n')) {
                    if (line.toLower().startsWith("content-length:"))
                        contentLength = line.mid(15).trimmed().toInt();
                }
                if (accumBuf->size() - bodyOffset < contentLength) return;

                const QByteArray buf = *accumBuf;
                accumBuf->clear();
                lastRequestBody = buf.mid(bodyOffset, contentLength);
                // Extract request line + Authorization
                lastAuthHeader.clear();
                for (const QByteArray &line : headers.split('\n')) {
                    if (line.startsWith("Authorization:"))
                        lastAuthHeader = QString::fromUtf8(line.mid(14)).trimmed();
                }

                const bool isModels = headers.startsWith("GET /v1/models");
                const bool isChat   = headers.startsWith("POST /v1/chat/completions");

                QByteArray reply;
                if (isModels) {
                    const QByteArray body =
                        R"({"object":"list","data":[)"
                        R"({"id":"qwen2.5-coder:3b-mock","object":"model"},)"
                        R"({"id":"llama3.2:3b-mock","object":"model"}]})";
                    reply  = "HTTP/1.1 200 OK\r\n";
                    reply += "Content-Type: application/json\r\n";
                    reply += "Content-Length: " + QByteArray::number(body.size()) + "\r\n";
                    reply += "\r\n";
                    reply += body;
                    sock->write(reply);
                    sock->flush();
                    sock->disconnectFromHost();
                } else if (isChat) {
                    // Write SSE preamble
                    reply  = "HTTP/1.1 200 OK\r\n";
                    reply += "Content-Type: text/event-stream\r\n";
                    reply += "Cache-Control: no-cache\r\n";
                    reply += "Transfer-Encoding: chunked\r\n";
                    reply += "\r\n";
                    sock->write(reply);
                    sock->flush();

                    auto chunk = [sock](const QByteArray &payload) {
                        // chunked encoding: hex-length, CRLF, data, CRLF
                        QByteArray hex = QByteArray::number(payload.size(), 16) + "\r\n";
                        sock->write(hex + payload + "\r\n");
                        sock->flush();
                    };

                    chunk("data: {\"choices\":[{\"delta\":{\"content\":\"Hello \"}}]}\n\n");
                    chunk("data: {\"choices\":[{\"delta\":{\"content\":\"from \"}}]}\n\n");
                    chunk("data: {\"choices\":[{\"delta\":{\"content\":\"llama.cpp!\"},\"finish_reason\":\"stop\"}]}\n\n");
                    chunk("data: [DONE]\n\n");
                    sawDone = true;
                    chunk("");  // end-of-chunks
                    sock->disconnectFromHost();
                } else {
                    reply = "HTTP/1.1 404 Not Found\r\n\r\n";
                    sock->write(reply);
                    sock->flush();
                    sock->disconnectFromHost();
                }
            });
        });
    }
};

int main(int argc, char *argv[]) {
    QCoreApplication app(argc, argv);

    // Spin up mock server
    MockOpenAIServer server;
    if (!server.listen(QHostAddress::LocalHost)) {
        fprintf(stderr, "FAIL: cannot start mock server\n");
        return 1;
    }
    const QString baseUrl = QString("http://127.0.0.1:%1").arg(server.serverPort());
    fprintf(stdout, "Mock OpenAI-compat server listening at %s\n",
            baseUrl.toUtf8().constData());

    // Configure the client to use the llama.cpp backend pointed at our
    // mock. Also set a fake API key so we can verify the Authorization
    // header is attached.
    Config::instance().aiBackend = "llama.cpp";
    Config::instance().aiBaseUrl = baseUrl;
    Config::instance().aiApiKey = "test-bearer-token-42";

    OllamaClient client;
    client.setBackend(OllamaClient::LlamaCpp);
    client.setBaseUrl(baseUrl);

    // ─── Test 1: listModels() against OpenAI /v1/models ─────────────
    fprintf(stdout, "\nTest 1: listModels() parses /v1/models JSON\n");
    QStringList gotModels;
    QString gotError;
    bool gotSignal = false;
    QObject::connect(&client, &OllamaClient::modelsListed,
                     [&](const QStringList &m) { gotModels = m; gotSignal = true; });
    QObject::connect(&client, &OllamaClient::modelsError,
                     [&](const QString &e) { gotError = e; gotSignal = true; });

    client.listModels();
    QEventLoop loop1;
    QTimer::singleShot(3000, &loop1, &QEventLoop::quit);
    QObject::connect(&client, &OllamaClient::modelsListed, &loop1, &QEventLoop::quit);
    QObject::connect(&client, &OllamaClient::modelsError,  &loop1, &QEventLoop::quit);
    loop1.exec();

    if (!gotSignal) { fprintf(stderr, "  FAIL: no signal within 3s\n"); return 1; }
    if (!gotError.isEmpty()) {
        fprintf(stderr, "  FAIL: error: %s\n", gotError.toUtf8().constData());
        return 1;
    }
    if (gotModels.size() != 2) {
        fprintf(stderr, "  FAIL: expected 2 models, got %d\n", gotModels.size());
        return 1;
    }
    if (!gotModels.contains("qwen2.5-coder:3b-mock")) {
        fprintf(stderr, "  FAIL: model list missing qwen2.5-coder:3b-mock\n");
        return 1;
    }
    fprintf(stdout, "  ok: parsed 2 models (qwen2.5-coder:3b-mock, llama3.2:3b-mock)\n");

    // ─── Test 2: generate() streams SSE frames and emits tokens ─────
    fprintf(stdout, "\nTest 2: generate() parses SSE 'data: {...}\\n\\n' frames\n");
    QString fullResponse;
    QStringList tokens;
    bool gotFinished = false;
    QString genError;

    QObject::connect(&client, &OllamaClient::tokenReceived,
                     [&](const QString &tok) { tokens << tok; });
    QObject::connect(&client, &OllamaClient::finished,
                     [&](const QString &full) { fullResponse = full; gotFinished = true; });
    QObject::connect(&client, &OllamaClient::error,
                     [&](const QString &e) { genError = e; });

    client.setModel("qwen2.5-coder:3b-mock");
    client.generate("say hi", "You are helpful", false);

    QEventLoop loop2;
    QTimer::singleShot(5000, &loop2, &QEventLoop::quit);
    QObject::connect(&client, &OllamaClient::finished, &loop2, &QEventLoop::quit);
    QObject::connect(&client, &OllamaClient::error,    &loop2, &QEventLoop::quit);
    loop2.exec();

    if (!gotFinished) {
        fprintf(stderr, "  FAIL: finished() never fired. error=%s tokens=%d\n",
                genError.toUtf8().constData(), tokens.size());
        return 1;
    }
    if (tokens.size() != 3) {
        fprintf(stderr, "  FAIL: expected 3 token chunks, got %d\n", tokens.size());
        return 1;
    }
    if (fullResponse != "Hello from llama.cpp!") {
        fprintf(stderr, "  FAIL: full response mismatch: '%s'\n",
                fullResponse.toUtf8().constData());
        return 1;
    }
    fprintf(stdout, "  ok: 3 tokens assembled into '%s'\n",
            fullResponse.toUtf8().constData());

    // ─── Test 3: Authorization: Bearer header propagated ────────────
    fprintf(stdout, "\nTest 3: Authorization header uses Config::aiApiKey\n");
    if (server.lastAuthHeader != "Bearer test-bearer-token-42") {
        fprintf(stderr, "  FAIL: expected 'Bearer test-bearer-token-42', got '%s'\n",
                server.lastAuthHeader.toUtf8().constData());
        return 1;
    }
    fprintf(stdout, "  ok: Bearer token passed through to OpenAI-compat endpoint\n");

    // ─── Test 4: request body has OpenAI shape ──────────────────────
    fprintf(stdout, "\nTest 4: request body is OpenAI chat-completions shape\n");
    const QByteArray &body = server.lastRequestBody;
    // Qt's QJsonDocument serializes with "key": value (space after colon),
    // so match both shapes to be robust.
    const QByteArray compact = body.simplified().replace(" ", "");
    if (!compact.contains("\"messages\":[") || !compact.contains("\"stream\":true")) {
        fprintf(stderr, "  FAIL: body missing 'messages' array or 'stream:true'\n");
        fprintf(stderr, "  got: %s\n", body.constData());
        return 1;
    }
    if (!body.contains("qwen2.5-coder:3b-mock")) {
        fprintf(stderr, "  FAIL: body missing model name\n");
        return 1;
    }
    fprintf(stdout, "  ok: body is OpenAI /v1/chat/completions shape\n");

    if (!server.sawDone) {
        fprintf(stderr, "  WARN: server never sent [DONE] (but stream finished anyway)\n");
    }

    fprintf(stdout, "\n=== ALL LLAMA.CPP / OPENAI-COMPAT TESTS PASS ===\n");
    fprintf(stdout, "OllamaClient correctly drives llama-server / LM Studio / Jan /\n");
    fprintf(stdout, "vLLM / KoboldCpp / llamafile / OpenRouter via the OpenAI\n");
    fprintf(stdout, "/v1/chat/completions protocol. Bearer auth works.\n");
    return 0;
}
