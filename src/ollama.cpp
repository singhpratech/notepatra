#include "ollama.h"
#include "config.h"
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkRequest>
#include <QUrl>
#include <QEventLoop>
#include <QTimer>

// ═══════════════════════════════════════════════════════════════════════
// Local-AI client.
//
// Supports three backends:
//   1. Ollama       — localhost:11434, /api/tags + /api/generate (streams
//                     one JSON object per newline)
//   2. llama.cpp    — llama-server at localhost:8080, /v1/models +
//                     /v1/chat/completions (streams SSE; "data: {json}\n\n")
//   3. OpenAICompat — same OpenAI-compatible endpoints as llama.cpp, but
//                     at a user-configured URL (LM Studio, Jan,
//                     text-generation-webui, vLLM, or even OpenAI itself
//                     if the user puts an API key there).
//
// The class name stays OllamaClient for source-compat with every call
// site that already references it. Behaviour is still Ollama by default
// unless the user switches backend in Settings → AI Backend.
// ═══════════════════════════════════════════════════════════════════════

OllamaClient::Backend OllamaClient::backendFromString(const QString &s) {
    if (s.compare("llama.cpp", Qt::CaseInsensitive) == 0 ||
        s.compare("llamacpp", Qt::CaseInsensitive) == 0)
        return LlamaCpp;
    if (s.compare("OpenAI", Qt::CaseInsensitive) == 0 ||
        s.compare("OpenAICompat", Qt::CaseInsensitive) == 0 ||
        s.compare("OpenAI-compat", Qt::CaseInsensitive) == 0 ||
        s.compare("custom", Qt::CaseInsensitive) == 0)
        return OpenAICompat;
    return Ollama;
}

QString OllamaClient::backendToString(Backend b) {
    switch (b) {
    case LlamaCpp:     return "llama.cpp";
    case OpenAICompat: return "OpenAI-compat";
    case Ollama:
    default:           return "Ollama";
    }
}

OllamaClient::OllamaClient(QObject *parent) : QObject(parent) {
    m_nam = new QNetworkAccessManager(this);

    // Pick up backend + URL from user config. Defaults are the Ollama
    // localhost endpoint so existing installations keep working with
    // zero configuration.
    const auto &cfg = Config::instance();
    m_backend = backendFromString(cfg.aiBackend);
    m_baseUrl = cfg.aiBaseUrl.isEmpty()
        ? (m_backend == LlamaCpp     ? "http://localhost:8080"
          : m_backend == OpenAICompat ? "http://localhost:8080"
          :                             "http://localhost:11434")
        : cfg.aiBaseUrl;
}

// ─── isAvailable ───────────────────────────────────────────────────────
// Synchronous probe, 3 s timeout. Endpoint varies by backend.
bool OllamaClient::isAvailable() {
    const QString probePath = (m_backend == Ollama) ? "/api/tags" : "/v1/models";
    QUrl url(m_baseUrl + probePath);
    QNetworkRequest req(url);
    auto *reply = m_nam->get(req);

    QEventLoop loop;
    QTimer timer;
    timer.setSingleShot(true);
    QObject::connect(&timer, &QTimer::timeout, &loop, &QEventLoop::quit);
    QObject::connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
    timer.start(3000);
    loop.exec();

    bool ok = reply->isFinished() && reply->error() == QNetworkReply::NoError;
    reply->deleteLater();
    return ok;
}

// ─── listModels ────────────────────────────────────────────────────────
// Ollama: GET /api/tags → {"models": [{"name": "..."}, ...]}
// OpenAI-compat: GET /v1/models → {"data": [{"id": "..."}, ...]}
void OllamaClient::listModels() {
    const QString path = (m_backend == Ollama) ? "/api/tags" : "/v1/models";
    QUrl url(m_baseUrl + path);
    QNetworkRequest req(url);
    auto *reply = m_nam->get(req);

    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        if (reply->error() != QNetworkReply::NoError) {
            QString msg = reply->errorString();
            if (msg.contains("refused", Qt::CaseInsensitive) ||
                msg.contains("unreachable", Qt::CaseInsensitive)) {
                if (m_backend == Ollama)
                    msg = "Ollama not running. Start it: ollama serve";
                else if (m_backend == LlamaCpp)
                    msg = "llama-server not running on " + m_baseUrl +
                          ". Start it: llama-server -m <model.gguf> --port 8080";
                else
                    msg = "No local-AI server reachable at " + m_baseUrl;
            }
            emit modelsError(msg);
            reply->deleteLater();
            return;
        }

        QJsonDocument doc = QJsonDocument::fromJson(reply->readAll());
        QStringList models;
        if (doc.isObject()) {
            const QJsonObject root = doc.object();
            if (m_backend == Ollama) {
                for (const QJsonValue &v : root.value("models").toArray()) {
                    QString name = v.toObject().value("name").toString();
                    if (!name.isEmpty()) models << name;
                }
            } else {
                // OpenAI-compat /v1/models format: {"data": [{"id": "..."}, ...]}
                for (const QJsonValue &v : root.value("data").toArray()) {
                    QString id = v.toObject().value("id").toString();
                    if (!id.isEmpty()) models << id;
                }
                // llama-server --models-path sometimes returns just one
                // entry (the loaded GGUF's basename). Expose it even if
                // the list would otherwise be empty.
                if (models.isEmpty() && root.contains("object"))
                    models << "(default-loaded-model)";
            }
        }
        models.sort(Qt::CaseInsensitive);
        emit modelsListed(models);
        reply->deleteLater();
    });
}

// ─── generate ──────────────────────────────────────────────────────────
void OllamaClient::generate(const QString &prompt, const QString &systemPrompt,
                            bool enableThinking,
                            const QStringList &imagesBase64) {
    cancel();
    m_fullResponse.clear();
    m_sseBuffer.clear();
    m_done = false;

    if (m_backend == Ollama) {
        // ─── Ollama native /api/generate ───────────────────────────────
        QJsonObject body;
        body["model"] = m_model;
        body["prompt"] = prompt;
        body["stream"] = true;
        // Qwen3 / DeepSeek-R1 thinking-model toggle
        body["think"] = enableThinking;
        if (!systemPrompt.isEmpty()) {
            body["system"] = enableThinking ? systemPrompt
                                            : systemPrompt + "\n/no_think";
        } else if (!enableThinking) {
            body["system"] = "/no_think";
        }

        if (!imagesBase64.isEmpty()) {
            QJsonArray imgs;
            for (const QString &b64 : imagesBase64) imgs.append(b64);
            body["images"] = imgs;
        }

        QJsonObject options;
        options["temperature"]    = 0.3;
        options["num_predict"]    = 2048;
        options["num_ctx"]        = 4096;
        options["repeat_penalty"] = 1.1;
        body["options"] = options;
        body["keep_alive"] = "5m";

        QUrl url(m_baseUrl + "/api/generate");
        QNetworkRequest req(url);
        req.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
        m_reply = m_nam->post(req, QJsonDocument(body).toJson());

        connect(m_reply, &QNetworkReply::readyRead, this, &OllamaClient::onReadyReadOllama);
        connect(m_reply, &QNetworkReply::finished,  this, &OllamaClient::onFinishedOllama);
    } else {
        // ─── OpenAI-compatible /v1/chat/completions ────────────────────
        // Works for llama.cpp's llama-server, LM Studio, Jan, vLLM, etc.
        // No thinking-block field (OpenAI spec doesn't define one);
        // thinking suppression still goes through the system prompt.
        QJsonArray messages;
        if (!systemPrompt.isEmpty()) {
            QJsonObject sys;
            sys["role"] = "system";
            sys["content"] = enableThinking ? systemPrompt
                                            : systemPrompt + "\n/no_think";
            messages.append(sys);
        } else if (!enableThinking) {
            QJsonObject sys;
            sys["role"] = "system";
            sys["content"] = "/no_think";
            messages.append(sys);
        }

        QJsonObject user;
        user["role"] = "user";
        // Vision attachments use the OpenAI "content is array" shape:
        //   [{type:"text", text:"..."}, {type:"image_url", image_url:{url:"data:...;base64,..."}}]
        if (imagesBase64.isEmpty()) {
            user["content"] = prompt;
        } else {
            QJsonArray parts;
            QJsonObject textPart;
            textPart["type"] = "text";
            textPart["text"] = prompt;
            parts.append(textPart);
            for (const QString &b64 : imagesBase64) {
                QJsonObject img;
                img["type"] = "image_url";
                QJsonObject ref;
                ref["url"] = "data:image/jpeg;base64," + b64;
                img["image_url"] = ref;
                parts.append(img);
            }
            user["content"] = parts;
        }
        messages.append(user);

        QJsonObject body;
        body["model"] = m_model;
        body["messages"] = messages;
        body["stream"] = true;
        body["temperature"] = 0.3;
        body["max_tokens"]  = 2048;

        QUrl url(m_baseUrl + "/v1/chat/completions");
        QNetworkRequest req(url);
        req.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
        // llama-server ignores Authorization; LM Studio / OpenAI expect
        // a Bearer token. Pass-through from Config::aiApiKey if set.
        const QString apiKey = Config::instance().aiApiKey;
        if (!apiKey.isEmpty())
            req.setRawHeader("Authorization", ("Bearer " + apiKey).toUtf8());
        m_reply = m_nam->post(req, QJsonDocument(body).toJson());

        connect(m_reply, &QNetworkReply::readyRead, this, &OllamaClient::onReadyReadOpenAI);
        connect(m_reply, &QNetworkReply::finished,  this, &OllamaClient::onFinishedOpenAI);
    }

    connect(m_reply, &QNetworkReply::finished, this, [this]() {
        if (m_reply && m_reply->error() != QNetworkReply::NoError && !m_done) {
            emit error(m_reply->errorString());
        }
    });
}

void OllamaClient::cancel() {
    if (m_reply) {
        disconnect(m_reply, nullptr, this, nullptr);
        m_reply->abort();
        m_reply->deleteLater();
        m_reply = nullptr;
    }
    m_sseBuffer.clear();
}

// ─── onReadyRead dispatcher — kept for source-compat ───────────────────
void OllamaClient::onReadyRead() {
    if (m_backend == Ollama) onReadyReadOllama();
    else                     onReadyReadOpenAI();
}

// ─── Ollama wire format: one JSON object per newline ───────────────────
void OllamaClient::onReadyReadOllama() {
    if (!m_reply) return;
    QByteArray data = m_reply->readAll();
    for (const QByteArray &line : data.split('\n')) {
        if (line.trimmed().isEmpty()) continue;
        QJsonDocument doc = QJsonDocument::fromJson(line);
        if (doc.isNull()) continue;
        QJsonObject obj = doc.object();
        if (obj.contains("error")) {
            emit error(obj["error"].toString());
            return;
        }
        QString token = obj["response"].toString();
        if (!token.isEmpty()) {
            m_fullResponse += token;
            emit tokenReceived(token);
        }
        if (obj["done"].toBool() && !m_done) {
            m_done = true;
            emit finished(m_fullResponse);
        }
    }
}

// ─── OpenAI-compat wire format: SSE ("data: {json}\n\n") ──────────────
// A single network read can contain a partial frame, so we buffer until
// we see "\n\n" between frames. Each frame starts with "data: " and
// ends with "\n\n"; the terminal frame is the literal string "data: [DONE]".
void OllamaClient::onReadyReadOpenAI() {
    if (!m_reply) return;
    m_sseBuffer += m_reply->readAll();

    while (true) {
        int nn = m_sseBuffer.indexOf("\n\n");
        if (nn < 0) break;
        QByteArray frame = m_sseBuffer.left(nn);
        m_sseBuffer.remove(0, nn + 2);

        // A frame may have multiple lines; iterate each "data: " prefix
        for (const QByteArray &line : frame.split('\n')) {
            QByteArray trimmed = line.trimmed();
            if (!trimmed.startsWith("data:")) continue;
            QByteArray payload = trimmed.mid(5).trimmed();
            if (payload == "[DONE]") {
                if (!m_done) {
                    m_done = true;
                    emit finished(m_fullResponse);
                }
                continue;
            }
            QJsonDocument doc = QJsonDocument::fromJson(payload);
            if (!doc.isObject()) continue;
            QJsonObject obj = doc.object();
            if (obj.contains("error")) {
                QJsonValue err = obj.value("error");
                QString msg = err.isObject() ? err.toObject().value("message").toString()
                                              : err.toString();
                if (!msg.isEmpty()) emit error(msg);
                return;
            }
            // choices[0].delta.content — the streaming chunk token
            QJsonArray choices = obj.value("choices").toArray();
            if (choices.isEmpty()) continue;
            QJsonObject choice = choices.first().toObject();
            QJsonObject delta  = choice.value("delta").toObject();
            QString tok = delta.value("content").toString();
            if (!tok.isEmpty()) {
                m_fullResponse += tok;
                emit tokenReceived(tok);
            }
            if (choice.value("finish_reason").toString().length() > 0 && !m_done) {
                m_done = true;
                emit finished(m_fullResponse);
            }
        }
    }
}

void OllamaClient::onFinished() {
    if (m_backend == Ollama) onFinishedOllama();
    else                     onFinishedOpenAI();
}

void OllamaClient::onFinishedOllama() {
    if (!m_reply) return;
    QByteArray remaining = m_reply->readAll();
    for (const QByteArray &line : remaining.split('\n')) {
        if (line.trimmed().isEmpty()) continue;
        QJsonDocument doc = QJsonDocument::fromJson(line);
        if (doc.isNull()) continue;
        QString token = doc.object().value("response").toString();
        if (!token.isEmpty()) {
            m_fullResponse += token;
            emit tokenReceived(token);
        }
    }
    if (!m_done && !m_fullResponse.isEmpty()) {
        m_done = true;
        emit finished(m_fullResponse);
    }
    m_reply->deleteLater();
    m_reply = nullptr;
}

void OllamaClient::onFinishedOpenAI() {
    if (!m_reply) return;
    // Flush any trailing SSE data
    m_sseBuffer += m_reply->readAll();
    if (!m_sseBuffer.isEmpty()) {
        m_sseBuffer += "\n\n";  // force a final frame boundary
        onReadyReadOpenAI();
    }
    if (!m_done && !m_fullResponse.isEmpty()) {
        m_done = true;
        emit finished(m_fullResponse);
    }
    m_reply->deleteLater();
    m_reply = nullptr;
}

void OllamaClient::onError(QNetworkReply::NetworkError) {
    QString msg = m_reply ? m_reply->errorString() : QString("Connection failed");
    if (msg.contains("Connection refused")) {
        if (m_backend == Ollama)
            msg = "Ollama not running. Start it with: ollama serve";
        else if (m_backend == LlamaCpp)
            msg = "llama-server not running. Start it: llama-server -m <model.gguf> --port 8080";
        else
            msg = "No local-AI server reachable at " + m_baseUrl;
    }
    emit error(msg);
}
