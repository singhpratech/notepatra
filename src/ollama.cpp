#include "ollama.h"
#include "config.h"
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkRequest>
#include <QUrl>
#include <QEventLoop>
#include <QTimer>
#include <QDateTime>

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
                            const QStringList &imagesBase64,
                            const QJsonArray &tools) {
    cancel();
    m_fullResponse.clear();
    m_sseBuffer.clear();
    m_done = false;
    m_promptTokens = -1;
    m_evalTokens = -1;
    m_startMs = QDateTime::currentMSecsSinceEpoch();
    m_pendingToolCalls.clear();
    m_messages = QJsonArray();
    m_lastSystemPrompt = systemPrompt;
    m_lastTools = tools;
    m_toolCallSeq = 0;

    const bool hasTools = !tools.isEmpty();

    if (m_backend == Ollama) {
        // ─── Ollama: /api/generate (legacy, no tools) or /api/chat (tools) ──
        if (hasTools) {
            // Tool-calling requires /api/chat (the messages-array endpoint).
            // Build a minimal 2-message conversation: system + user. Future
            // tool-result rounds get appended via continueWithToolResults().
            if (!systemPrompt.isEmpty()) {
                QJsonObject sys;
                sys["role"] = "system";
                sys["content"] = enableThinking ? systemPrompt
                                                : systemPrompt + "\n/no_think";
                m_messages.append(sys);
            }
            QJsonObject user;
            user["role"] = "user";
            user["content"] = prompt;
            if (!imagesBase64.isEmpty()) {
                QJsonArray imgs;
                for (const QString &b64 : imagesBase64) imgs.append(b64);
                user["images"] = imgs;
            }
            m_messages.append(user);

            QJsonObject body;
            body["model"] = m_model;
            body["messages"] = m_messages;
            body["stream"] = true;
            body["tools"] = tools;
            body["think"] = enableThinking;

            QJsonObject options;
            // v0.1.35 — pin temperature low for tool-bearing requests.
            // Per Ollama / multi-editor research: high temperature
            // produces malformed JSON in tool arguments even on
            // tool-trained models. 0.1 is the documented sweet spot.
            options["temperature"]    = 0.1;
            options["num_predict"]    = 2048;
            options["num_ctx"]        = 8192;  // bigger ctx for tool round-trips
            options["repeat_penalty"] = 1.05;
            body["options"] = options;
            body["keep_alive"] = "5m";

            QUrl url(m_baseUrl + "/api/chat");
            QNetworkRequest req(url);
            req.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
            m_reply = m_nam->post(req, QJsonDocument(body).toJson());

            connect(m_reply, &QNetworkReply::readyRead, this, &OllamaClient::onReadyReadOllama);
            connect(m_reply, &QNetworkReply::finished,  this, &OllamaClient::onFinishedOllama);
        } else {
        // ─── Ollama native /api/generate (legacy completions endpoint) ─
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
        }  // end !hasTools (Ollama branch)
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

        // Persist messages for the agent loop's continueWithToolResults.
        m_messages = messages;

        QJsonObject body;
        body["model"] = m_model;
        body["messages"] = messages;
        body["stream"] = true;
        body["temperature"] = hasTools ? 0.1 : 0.3;
        body["max_tokens"]  = 2048;
        if (hasTools) {
            body["tools"] = tools;
            body["tool_choice"] = "auto";
        }

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

// ═══════════════════════════════════════════════════════════════════════
// continueWithToolResults — agent-loop continuation
//
// AIPanel calls this after executing the tool calls from a previous
// stream. We append the assistant's tool-call message + each tool
// result to m_messages, then re-POST to the chat endpoint with the
// FULL conversation history so the model can pick up where it left
// off. Tools array is forwarded so the model can call further tools.
// ═══════════════════════════════════════════════════════════════════════
void OllamaClient::continueWithToolResults(const QJsonArray &toolResults,
                                           const QString &systemPrompt,
                                           const QJsonArray &tools) {
    cancel();
    m_fullResponse.clear();
    m_sseBuffer.clear();
    m_done = false;
    m_promptTokens = -1;
    m_evalTokens = -1;
    m_startMs = QDateTime::currentMSecsSinceEpoch();
    m_pendingToolCalls.clear();

    if (!systemPrompt.isEmpty()) m_lastSystemPrompt = systemPrompt;
    if (!tools.isEmpty())        m_lastTools = tools;

    // Reconstruct the assistant's tool-call turn (we synthesized IDs and
    // emitted the calls earlier; now bake them into the conversation
    // history so the model sees its own tool-calling output).
    QJsonObject assistantTurn;
    assistantTurn["role"] = "assistant";
    assistantTurn["content"] = m_fullResponse;  // any text emitted alongside
    QJsonArray reconCalls;
    for (const QJsonValue &rv : toolResults) {
        QJsonObject r = rv.toObject();
        QJsonObject c;
        c["id"] = r.value("id").toString();
        QJsonObject fn;
        fn["name"] = r.value("name").toString();
        // We don't have the original args here — best-effort empty obj.
        // The real model output already had them; this is just a
        // history-shaped reconstruction so the model has the right
        // turn structure on the next round.
        fn["arguments"] = r.value("args").toObject();
        c["type"] = "function";
        c["function"] = fn;
        reconCalls.append(c);
    }
    if (!reconCalls.isEmpty()) assistantTurn["tool_calls"] = reconCalls;
    m_messages.append(assistantTurn);

    // Append each tool result as role:tool. Wire shape differs per
    // backend: Ollama uses `tool_name`, OpenAI-compat uses `tool_call_id`.
    for (const QJsonValue &rv : toolResults) {
        QJsonObject r = rv.toObject();
        QJsonObject msg;
        msg["role"] = "tool";
        msg["content"] = r.value("content").toString();
        if (m_backend == Ollama) {
            msg["tool_name"] = r.value("name").toString();
        } else {
            msg["tool_call_id"] = r.value("id").toString();
            msg["name"] = r.value("name").toString();
        }
        m_messages.append(msg);
    }

    // Re-send with full history.
    if (m_backend == Ollama) {
        QJsonObject body;
        body["model"] = m_model;
        body["messages"] = m_messages;
        body["stream"] = true;
        body["tools"] = m_lastTools;
        QJsonObject options;
        options["temperature"] = 0.1;
        options["num_predict"] = 2048;
        options["num_ctx"]     = 8192;
        body["options"] = options;
        body["keep_alive"] = "5m";

        QUrl url(m_baseUrl + "/api/chat");
        QNetworkRequest req(url);
        req.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
        m_reply = m_nam->post(req, QJsonDocument(body).toJson());
        connect(m_reply, &QNetworkReply::readyRead, this, &OllamaClient::onReadyReadOllama);
        connect(m_reply, &QNetworkReply::finished,  this, &OllamaClient::onFinishedOllama);
    } else {
        QJsonObject body;
        body["model"] = m_model;
        body["messages"] = m_messages;
        body["stream"] = true;
        body["temperature"] = 0.1;
        body["max_tokens"]  = 2048;
        body["tools"] = m_lastTools;
        body["tool_choice"] = "auto";

        QUrl url(m_baseUrl + "/v1/chat/completions");
        QNetworkRequest req(url);
        req.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
        const QString apiKey = Config::instance().aiApiKey;
        if (!apiKey.isEmpty())
            req.setRawHeader("Authorization", ("Bearer " + apiKey).toUtf8());
        m_reply = m_nam->post(req, QJsonDocument(body).toJson());
        connect(m_reply, &QNetworkReply::readyRead, this, &OllamaClient::onReadyReadOpenAI);
        connect(m_reply, &QNetworkReply::finished,  this, &OllamaClient::onFinishedOpenAI);
    }
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
//
// Two response shapes share this parser:
//   - /api/generate frames have `response` as the streamed token string
//     and `done:true` on the final frame with eval_count + prompt_eval_count.
//   - /api/chat (used when tools are enabled) frames have `message.content`
//     for streamed text tokens and `message.tool_calls` for tool calls.
//     Tool calls arrive ATOMICALLY — entire array in one chunk per the
//     v0.1.35 wire-format research; no partial accumulator needed for
//     Ollama (unlike OpenAI streaming).
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

        // /api/generate path — `response` field
        QString token = obj["response"].toString();
        // /api/chat path — `message.content` + `message.tool_calls`
        if (obj.contains("message")) {
            QJsonObject msg = obj.value("message").toObject();
            QString content = msg.value("content").toString();
            if (!content.isEmpty()) {
                token = content;
            }
            if (msg.contains("tool_calls")) {
                QJsonArray calls = msg.value("tool_calls").toArray();
                for (const QJsonValue &cv : calls) {
                    QJsonObject c = cv.toObject();
                    QJsonObject fn = c.value("function").toObject();
                    QString name = fn.value("name").toString();
                    // Ollama's `arguments` is already a parsed JSON object
                    // (NOT a stringified JSON like OpenAI canonical). Be
                    // defensive: accept both shapes.
                    QJsonObject args;
                    QJsonValue av = fn.value("arguments");
                    if (av.isObject()) {
                        args = av.toObject();
                    } else if (av.isString()) {
                        args = QJsonDocument::fromJson(av.toString().toUtf8()).object();
                    }
                    // Synthesize a client-side ID since Ollama doesn't
                    // supply one. AIPanel will round-trip it back on
                    // continueWithToolResults().
                    QString id = QString("call_n%1").arg(++m_toolCallSeq);
                    emit toolCallReceived(id, name, args);
                }
            }
        }
        if (!token.isEmpty()) {
            m_fullResponse += token;
            emit tokenReceived(token);
        }
        if (obj["done"].toBool() && !m_done) {
            m_done = true;
            // Ollama's done frame includes optional stats fields. Capture
            // them so the UI can render "1234 tokens · 2.3s" per response.
            if (obj.contains("eval_count"))
                m_evalTokens = obj["eval_count"].toInt();
            if (obj.contains("prompt_eval_count"))
                m_promptTokens = obj["prompt_eval_count"].toInt();
            const qint64 elapsed = QDateTime::currentMSecsSinceEpoch() - m_startMs;
            emit finished(m_fullResponse);
            emit responseStats(m_promptTokens, m_evalTokens, elapsed);
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
                    const qint64 elapsed = QDateTime::currentMSecsSinceEpoch() - m_startMs;
                    emit finished(m_fullResponse);
                    emit responseStats(m_promptTokens, m_evalTokens, elapsed);
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
            // OpenAI-compat servers emit a usage object on the final
            // chunk (or sometimes a separate non-choice frame). Capture
            // it before we look at choices.
            if (obj.contains("usage")) {
                QJsonObject u = obj.value("usage").toObject();
                if (u.contains("prompt_tokens"))      m_promptTokens = u.value("prompt_tokens").toInt();
                if (u.contains("completion_tokens"))  m_evalTokens   = u.value("completion_tokens").toInt();
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

            // ─── OpenAI-compat tool_calls streaming ─────────────────────
            //
            // Per the wire-format research: the first delta for each
            // tool_call carries id + type + function.name; subsequent
            // deltas carry only function.arguments fragments keyed by
            // `index`. Accumulate fragments per-index until we see
            // finish_reason: "tool_calls", then parse + emit.
            //
            // OpenRouter / OpenAI / Anthropic-proxy / vLLM / llama.cpp
            // (with --jinja) all use this canonical pattern.
            if (delta.contains("tool_calls")) {
                QJsonArray calls = delta.value("tool_calls").toArray();
                for (const QJsonValue &cv : calls) {
                    QJsonObject c = cv.toObject();
                    int idx = c.value("index").toInt(0);
                    PendingToolCall &p = m_pendingToolCalls[idx];
                    if (c.contains("id") && !c.value("id").toString().isEmpty()) {
                        p.id = c.value("id").toString();
                    }
                    QJsonObject fn = c.value("function").toObject();
                    if (fn.contains("name") && !fn.value("name").toString().isEmpty()) {
                        p.name = fn.value("name").toString();
                    }
                    if (fn.contains("arguments")) {
                        QJsonValue av = fn.value("arguments");
                        if (av.isString()) {
                            p.argsBuffer += av.toString();
                        } else if (av.isObject()) {
                            // llama.cpp Autoparser sends args as object
                            // — concatenate already-stringified form.
                            p.argsBuffer = QString::fromUtf8(
                                QJsonDocument(av.toObject()).toJson(QJsonDocument::Compact));
                        }
                    }
                }
            }

            const QString finishReason = choice.value("finish_reason").toString();
            // When finish_reason == "tool_calls", the assistant is done
            // emitting calls for this turn — flush all accumulated calls
            // and emit toolCallReceived for each. The agent loop will
            // execute them and call continueWithToolResults to keep
            // going. Don't fire `finished` yet — the conversation isn't
            // over until the model returns plain content with stop reason.
            if (finishReason == "tool_calls" && !m_pendingToolCalls.isEmpty()) {
                QList<int> indices = m_pendingToolCalls.keys();
                std::sort(indices.begin(), indices.end());
                for (int idx : indices) {
                    const PendingToolCall &p = m_pendingToolCalls.value(idx);
                    QString id = p.id.isEmpty()
                        ? QString("call_o%1").arg(++m_toolCallSeq)
                        : p.id;
                    QJsonObject args = QJsonDocument::fromJson(
                        p.argsBuffer.toUtf8()).object();
                    emit toolCallReceived(id, p.name, args);
                }
                m_pendingToolCalls.clear();
                // The connection stays open — the next /v1/chat/completions
                // response (kicked off by AIPanel via continueWithToolResults)
                // is a separate request.
                if (!m_done) {
                    m_done = true;
                    const qint64 elapsed = QDateTime::currentMSecsSinceEpoch() - m_startMs;
                    emit finished(m_fullResponse);
                    emit responseStats(m_promptTokens, m_evalTokens, elapsed);
                }
                continue;
            }

            if (!finishReason.isEmpty() && !m_done) {
                m_done = true;
                const qint64 elapsed = QDateTime::currentMSecsSinceEpoch() - m_startMs;
                emit finished(m_fullResponse);
                emit responseStats(m_promptTokens, m_evalTokens, elapsed);
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
