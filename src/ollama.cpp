// SPDX-License-Identifier: GPL-3.0-or-later

#include "ollama.h"
#include "config.h"
#include "ai_interaction_log.h"
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QNetworkRequest>
#include <QPointer>
#include <QUrl>
#include <QEventLoop>
#include <QTimer>
#include <QDateTime>

// v0.1.72 — cloud-free build flavor. The macros below are no-ops in the
// regular build; under NOTEPATRA_NO_CLOUD they interpose at every QNAM
// fire site to refuse non-private destinations, even if the rest of the
// stack (UI / Config / setBaseUrl override) somehow leaked a public URL
// through.  Three layers of defense: UI hides cloud presets, setBaseUrl
// refuses public assignments, these macros are the final firewall.
//
// _GATE  — drop-in for void member functions: emits error() + returns.
// _OK    — predicate variant for probes / non-void callers; caller
//          decides how to bail (typically return false / return empty).
#ifdef NOTEPATRA_NO_CLOUD
#include "network_policy.h"
#define NOTEPATRA_NO_CLOUD_GATE(URL) \
    do { \
        if (!::NotepatraNetworkPolicy::isPrivateNetworkHost((URL))) { \
            qWarning() << "[notepatra:no-cloud] refused" << (URL).host(); \
            emit error(QStringLiteral( \
                "Cloud-free build refused %1 — only localhost / " \
                "private-network LLM endpoints are allowed in this " \
                "flavor (notepatra-local-ai).").arg((URL).host())); \
            return; \
        } \
    } while (0)
#define NOTEPATRA_NO_CLOUD_OK(URL) \
    (::NotepatraNetworkPolicy::isPrivateNetworkHost((URL)))
#else
#define NOTEPATRA_NO_CLOUD_GATE(URL) do { (void)(URL); } while (0)
#define NOTEPATRA_NO_CLOUD_OK(URL)   (true)
#endif

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

// v0.1.55 — Azure OpenAI URL + auth helpers. Azure routes through
// `<resource>.openai.azure.com/openai/deployments/<deployment>` instead
// of a single global API endpoint, requires `?api-version=` on every
// call, and uses an `api-key` header instead of `Authorization: Bearer`.
// These helpers normalise that for the three call sites (listModels,
// chat tools, chat no-tools) so the per-site code stays readable.
namespace {
bool isAzureBackend() {
    return Config::instance().aiBackend.compare("Azure OpenAI", Qt::CaseInsensitive) == 0;
}

QString azureApiVersion() {
    const QString v = Config::instance().aiAzureApiVersion.trimmed();
    return v.isEmpty() ? QStringLiteral("2024-10-21") : v;
}

// Build the right URL for an Azure call. `op` is one of:
//   "models"           → list deployments
//   "chat/completions" → chat endpoint (uses the configured deployment)
QUrl azureUrl(const QString &op) {
    const auto &cfg = Config::instance();
    const QString resource = cfg.aiAzureResource.trimmed();
    const QString version  = azureApiVersion();
    if (op == QLatin1String("models")) {
        return QUrl(QString("https://%1.openai.azure.com/openai/deployments?api-version=%2")
                        .arg(resource, version));
    }
    const QString deployment = cfg.aiAzureDeployment.trimmed();
    return QUrl(QString("https://%1.openai.azure.com/openai/deployments/%2/%3?api-version=%4")
                    .arg(resource, deployment, op, version));
}

// Set the right auth header for whichever cloud we're talking to. Azure
// uses `api-key` (lowercase, hyphenated), every other OpenAI-compat
// server uses `Authorization: Bearer`.
void setOpenAiAuth(QNetworkRequest &req, const QString &apiKey) {
    if (apiKey.isEmpty()) return;
    if (isAzureBackend()) {
        req.setRawHeader("api-key", apiKey.toUtf8());
    } else {
        req.setRawHeader("Authorization", ("Bearer " + apiKey).toUtf8());
    }
}
}  // anonymous namespace

OllamaClient::Backend OllamaClient::backendFromString(const QString &s) {
    if (s.compare("llama.cpp", Qt::CaseInsensitive) == 0 ||
        s.compare("llamacpp", Qt::CaseInsensitive) == 0)
        return LlamaCpp;
    if (s.compare("OpenAI", Qt::CaseInsensitive) == 0 ||
        s.compare("OpenRouter", Qt::CaseInsensitive) == 0 ||
        s.compare("Ollama Cloud", Qt::CaseInsensitive) == 0 ||
        s.compare("OllamaCloud", Qt::CaseInsensitive) == 0 ||
        s.compare("Azure OpenAI", Qt::CaseInsensitive) == 0 ||
        s.compare("AzureOpenAI", Qt::CaseInsensitive) == 0 ||
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

// v0.1.98 — OpenAI-compat base normaliser. The AI panel stores cloud base
// URLs WITH a "/v1" suffix (https://openrouter.ai/api/v1, https://api.openai.com/v1,
// https://ollama.com/v1), but every OpenAI endpoint we build appends
// "/v1/<path>". Without stripping the existing "/v1" we'd request
// ".../api/v1/v1/models" → 404, surfaced as "OpenRouter API unreachable"
// (user-reported 2026-05-24). Strip a trailing "/v1" + slashes so the result
// is always single-/v1, whether or not the stored base already had it.
static QString openAiV1Base(QString base) {
    while (base.endsWith(QLatin1Char('/'))) base.chop(1);
    if (base.endsWith(QStringLiteral("/v1"))) base.chop(3);
    while (base.endsWith(QLatin1Char('/'))) base.chop(1);
    return base;
}

// ─── httpErrorMessage ──────────────────────────────────────────────────
// Turn an HTTP failure (status + raw body) into one human-readable line for
// the UI. A streaming chat request that fails auth (bad/expired key → 401
// "User not found"), runs out of credit (402) or gets rate-limited (429)
// returns a plain JSON error body, NOT an SSE "data:" frame — so the stream
// parser never sees it. We surface it from onFinishedOpenAI instead.
// (user-reported 2026-05-24: a bad OpenRouter key made Extract spin ~60 s
// with no feedback because neither finished() nor error() ever fired.)
static QString httpErrorMessage(int status, const QByteArray &body,
                                const QString &fallback) {
    QString providerMsg;
    QJsonParseError perr{};
    QJsonDocument doc = QJsonDocument::fromJson(body.trimmed(), &perr);
    if (perr.error == QJsonParseError::NoError && doc.isObject()) {
        const QJsonValue ev = doc.object().value("error");
        if (ev.isObject())      providerMsg = ev.toObject().value("message").toString();
        else if (ev.isString()) providerMsg = ev.toString();
        if (providerMsg.isEmpty())
            providerMsg = doc.object().value("message").toString();
    }

    QString head;
    switch (status) {
        case 401:
        case 403:
            head = QStringLiteral("Authentication failed (HTTP %1) — your API key is "
                                  "invalid, expired, or lacks access to this model. "
                                  "Open the AI panel ⚙ and paste a valid key.").arg(status);
            break;
        case 402:
            head = QStringLiteral("Payment required (HTTP 402) — your account is out of "
                                  "credits. Top up at your provider, or pick a free model.");
            break;
        case 404:
            head = QStringLiteral("Not found (HTTP 404) — check the model name and backend "
                                  "URL in the AI panel.");
            break;
        case 429:
            head = QStringLiteral("Rate limited (HTTP 429) — too many requests; wait a "
                                  "moment and try again.");
            break;
        default:
            if (status >= 500)
                head = QStringLiteral("Provider server error (HTTP %1) — the AI service "
                                      "failed; try again shortly.").arg(status);
            else if (status > 0)
                head = QStringLiteral("Request failed (HTTP %1).").arg(status);
            else
                head = fallback.isEmpty() ? QStringLiteral("Request failed.") : fallback;
            break;
    }
    if (!providerMsg.isEmpty())
        head += QStringLiteral("  [%1]").arg(providerMsg.trimmed());
    return head;
}

// ─── isAvailable ───────────────────────────────────────────────────────
// Synchronous probe, 3 s timeout. Endpoint varies by backend.
bool OllamaClient::isAvailable() {
    if (!m_nam) return false;  // hardening: guard NAM (defensive even though ctor always sets it)
    QUrl url = (m_backend == Ollama)
                   ? QUrl(m_baseUrl + "/api/tags")
                   : QUrl(openAiV1Base(m_baseUrl) + "/v1/models");
    if (!url.isValid()) return false;  // hardening: malformed base URL guard
    if (!NOTEPATRA_NO_CLOUD_OK(url)) return false;  // cloud-free probe refusal — silent (sync caller)
    QNetworkRequest req(url);
    req.setTransferTimeout(3000);  // hardening: explicit transfer timeout (matches sync 3 s budget)
    auto *reply = m_nam->get(req);
    if (!reply) return false;  // hardening: NAM may return null on extreme OOM

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
    if (!m_nam) {  // hardening: guard NAM
        emit modelsError(QStringLiteral("Network manager unavailable"));
        return;
    }
    QUrl url;
    if (m_backend == Ollama) {
        url = QUrl(m_baseUrl + "/api/tags");
    } else if (isAzureBackend()) {
        url = azureUrl("models");
    } else {
        url = QUrl(openAiV1Base(m_baseUrl) + "/v1/models");
    }
    if (!url.isValid()) {  // hardening: bad base-url short-circuits with explicit error
        emit modelsError(QStringLiteral("Invalid backend URL: ") + m_baseUrl);
        return;
    }
    if (!NOTEPATRA_NO_CLOUD_OK(url)) {  // v0.1.72 cloud-free refusal
        emit modelsError(QStringLiteral("Cloud-free build refused %1 — pick a "
                                        "local backend (Ollama / llama.cpp).")
                             .arg(url.host()));
        return;
    }
    QNetworkRequest req(url);
    req.setTransferTimeout(5000);  // hardening: 5 s probe budget for /api/tags + /v1/models
    // v0.1.55 — Send the saved API key on /v1/models too. OpenRouter's public
    // catalog endpoint accepts unauth GETs, but OpenAI's does not — and we
    // also rely on this call to surface 401/403 when validating a freshly
    // pasted key from the AI panel's "Save" button.
    if (m_backend != Ollama) {
        const QString apiKey = Config::instance().aiKeyForBackend(Config::instance().aiBackend);
        setOpenAiAuth(req, apiKey);
    }
    auto *reply = m_nam->get(req);
    if (!reply) {  // hardening: NAM->get can theoretically return null
        emit modelsError(QStringLiteral("Failed to start network request"));
        return;
    }

    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        if (reply->error() != QNetworkReply::NoError) {
            QString msg = reply->errorString();
            if (msg.contains("refused", Qt::CaseInsensitive) ||
                msg.contains("unreachable", Qt::CaseInsensitive) ||
                msg.contains("timed out", Qt::CaseInsensitive) ||  // hardening: timeout hint
                msg.contains("timeout", Qt::CaseInsensitive)) {
                if (m_backend == Ollama)
                    msg = "Ollama not running. Start it: ollama serve";
                else if (m_backend == LlamaCpp)
                    msg = "llama-server not running on " + m_baseUrl +
                          ". Start it: llama-server -m <model.gguf> --port 8080";
                else
                    msg = "No local-AI server reachable at " + m_baseUrl;
            }
            emit modelsError(msg);
            reply->deleteLater();  // hardening: ensure deleteLater on every error path
            return;
        }

        const QByteArray rawBody = reply->readAll();
        QJsonParseError perr{};  // hardening: capture parse error explicitly
        QJsonDocument doc = QJsonDocument::fromJson(rawBody, &perr);
        if (perr.error != QJsonParseError::NoError) {  // hardening: surface malformed-JSON to UI
            emit modelsError(QStringLiteral("Backend returned malformed JSON: ") + perr.errorString());
            reply->deleteLater();
            return;
        }
        QStringList models;
        if (doc.isObject()) {
            const QJsonObject root = doc.object();
            if (m_backend == Ollama) {
                const QJsonValue mv = root.value("models");
                if (mv.isArray()) {  // hardening: verify "models" really is array
                    for (const QJsonValue &v : mv.toArray()) {
                        if (!v.isObject()) continue;  // hardening: tolerate non-object entries
                        QString name = v.toObject().value("name").toString();
                        if (!name.isEmpty()) models << name;
                    }
                }
            } else {
                // OpenAI-compat /v1/models format: {"data": [{"id": "..."}, ...]}
                const QJsonValue dv = root.value("data");
                const QJsonArray dataArr = dv.isArray() ? dv.toArray() : QJsonArray();  // hardening: type-check
                for (const QJsonValue &v : dataArr) {
                    if (!v.isObject()) continue;  // hardening: skip malformed entries
                    QString id = v.toObject().value("id").toString();
                    if (!id.isEmpty()) models << id;
                }
                // llama-server --models-path sometimes returns just one
                // entry (the loaded GGUF's basename). Expose it even if
                // the list would otherwise be empty.
                if (models.isEmpty() && root.contains("object"))
                    models << "(default-loaded-model)";

                // v0.1.54 — also emit the raw array so aipanel can show
                // pricing / provider grouping for OpenRouter et al.
                if (!dataArr.isEmpty()) emit modelsListedRich(dataArr);
            }
        }
        models.sort(Qt::CaseInsensitive);
        emit modelsListed(models);
        reply->deleteLater();
    });
}

// ─── showModel — capabilities probe for local Ollama models ───────────
// POSTs {"name": "<model>"} to /api/show. The response object includes:
//   - "capabilities": ["completion", "tools", "thinking", "vision", ...]
//   - "modelfile", "parameters", "template", "details", "model_info"
// We only care about capabilities here; everything else is ignored.
//
// This is the load-bearing replacement for the hardcoded allowlist in
// AiTools::modelLikelySupportsTools — Ollama's own answer to "does
// this model support function calling?" beats any substring guess.
void OllamaClient::showModel(const QString &name) {
    if (m_backend != Ollama) {
        // /api/show is Ollama-native; OpenAI-compat servers don't have it.
        emit modelCapabilitiesError(name, "not an Ollama backend");
        return;
    }
    if (name.isEmpty()) {
        emit modelCapabilitiesError(name, "empty model name");
        return;
    }
    if (!m_nam) {  // hardening: guard NAM
        emit modelCapabilitiesError(name, "Network manager unavailable");
        return;
    }
    QUrl url(m_baseUrl + "/api/show");
    if (!url.isValid()) {  // hardening: bad base-url
        emit modelCapabilitiesError(name, "Invalid backend URL: " + m_baseUrl);
        return;
    }
    if (!NOTEPATRA_NO_CLOUD_OK(url)) {  // v0.1.72 cloud-free refusal
        emit modelCapabilitiesError(name, QStringLiteral("Cloud-free build refused %1").arg(url.host()));
        return;
    }
    QNetworkRequest req(url);
    req.setTransferTimeout(5000);  // hardening: 5 s budget for capabilities probe
    req.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
    QJsonObject body;
    body["name"] = name;
    auto *reply = m_nam->post(req, QJsonDocument(body).toJson());
    if (!reply) {  // hardening: NAM->post can theoretically return null
        emit modelCapabilitiesError(name, "Failed to start /api/show request");
        return;
    }

    connect(reply, &QNetworkReply::finished, this, [this, reply, name]() {
        if (reply->error() != QNetworkReply::NoError) {
            emit modelCapabilitiesError(name, reply->errorString());
            reply->deleteLater();
            return;
        }
        const QByteArray rawBody = reply->readAll();
        QJsonParseError perr{};  // hardening: explicit parse-error capture
        const QJsonDocument doc = QJsonDocument::fromJson(rawBody, &perr);
        reply->deleteLater();
        if (perr.error != QJsonParseError::NoError || !doc.isObject()) {
            emit modelCapabilitiesError(name, "non-JSON response");
            return;
        }
        QStringList caps;
        const QJsonValue cv = doc.object().value("capabilities");
        if (cv.isArray()) {  // hardening: verify capabilities really is array
            for (const QJsonValue &v : cv.toArray()) {
                const QString s = v.toString().trimmed().toLower();
                if (!s.isEmpty()) caps << s;
            }
        }
        emit modelCapabilitiesLoaded(name, caps);
    });
}

// ─── generate ──────────────────────────────────────────────────────────
void OllamaClient::generate(const QString &prompt, const QString &systemPrompt,
                            bool enableThinking,
                            const QStringList &imagesBase64,
                            const QJsonArray &tools,
                            const QJsonArray &priorMessages) {
    if (!m_nam) {  // hardening: guard NAM (defensive — ctor always sets it)
        emit error(QStringLiteral("Network manager unavailable"));
        return;
    }
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

    // v0.1.71 — AI interaction log. Record the outgoing user turn before
    // the request leaves. Recorder is a no-op when the user has opted out
    // (Config::aiInteractionLogging == false). Backend tag is one of
    // "ollama" / "ollama-cloud" / "llama.cpp" / "openrouter" / "openai" /
    // "azure-openai" — mirrors the backend dropdown labels. The model
    // field is whatever the active OllamaClient is configured with.
    {
        QString backendTag;
        switch (m_backend) {
            case Ollama:           backendTag = QStringLiteral("ollama"); break;
            case LlamaCpp:         backendTag = QStringLiteral("llama.cpp"); break;
            case OpenAICompat:
                if (m_baseUrl.contains("openai.azure.com", Qt::CaseInsensitive))     backendTag = QStringLiteral("azure-openai");
                else if (m_baseUrl.contains("api.openai.com", Qt::CaseInsensitive))  backendTag = QStringLiteral("openai");
                else if (m_baseUrl.contains("ollama.com", Qt::CaseInsensitive))      backendTag = QStringLiteral("ollama-cloud");
                else if (m_baseUrl.contains("openrouter.ai", Qt::CaseInsensitive))   backendTag = QStringLiteral("openrouter");
                else                                                                 backendTag = QStringLiteral("openai-compat");
                break;
        }
        AiInteractionLog::recordUser(backendTag, m_model, m_mode, prompt);
        if (!systemPrompt.isEmpty()) {
            AiInteractionLog::Event sys;
            sys.backend = backendTag;
            sys.model   = m_model;
            sys.mode    = m_mode;
            sys.role    = AiInteractionLog::Role::System;
            sys.content = systemPrompt;
            AiInteractionLog::record(sys);
        }
    }

    const bool hasTools = !tools.isEmpty();

    // v0.1.70 — switch to /api/chat for ANY request that carries multi-turn
    // history, even if no tools are requested. The legacy /api/generate
    // endpoint takes a single prompt + system and CANNOT replay prior
    // conversation turns, so plain-chat mode on Ollama used to "forget"
    // every previous message. Force /api/chat whenever priorMessages
    // is non-empty so the conversation actually flows.
    const bool useChatEndpoint = hasTools || !priorMessages.isEmpty();

    if (m_backend == Ollama) {
        // ─── Ollama: /api/generate (legacy, no tools, no history) or /api/chat ──
        if (useChatEndpoint) {
            // Build the full conversation: [system] + priorMessages + currentUser.
            // Tool-result rounds (if any) get appended later via
            // continueWithToolResults().
            if (!systemPrompt.isEmpty()) {
                QJsonObject sys;
                sys["role"] = "system";
                sys["content"] = enableThinking ? systemPrompt
                                                : systemPrompt + "\n/no_think";
                m_messages.append(sys);
            }
            // v0.1.70 — splice the prior turns in before the new user message.
            // AIPanel passes {role, content} objects; we copy them through
            // unchanged so the model sees the full thread.
            for (const QJsonValue &v : priorMessages) {
                if (v.isObject()) m_messages.append(v);
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
            // v0.1.70 — only include tools field when we actually have
            // tools. /api/chat allows tools=[] but some models choke on
            // it; omit when empty for safety.
            if (hasTools) body["tools"] = tools;
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
            if (!url.isValid()) {  // hardening: invalid base-url short-circuit
                emit error(QStringLiteral("Invalid backend URL: ") + m_baseUrl);
                return;
            }
            NOTEPATRA_NO_CLOUD_GATE(url);   // v0.1.72 cloud-free refusal
            QNetworkRequest req(url);
            req.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
            req.setTransferTimeout(120000);  // hardening: 120 s chat-stream connect/transfer ceiling
            m_reply = m_nam->post(req, QJsonDocument(body).toJson());
            if (!m_reply) {  // hardening: guard against null reply
                emit error(QStringLiteral("Failed to start chat request"));
                return;
            }

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
        if (!url.isValid()) {  // hardening: invalid base-url short-circuit
            emit error(QStringLiteral("Invalid backend URL: ") + m_baseUrl);
            return;
        }
        NOTEPATRA_NO_CLOUD_GATE(url);   // v0.1.72 cloud-free refusal
        QNetworkRequest req(url);
        req.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
        req.setTransferTimeout(120000);  // hardening: 120 s generate-stream connect/transfer ceiling
        m_reply = m_nam->post(req, QJsonDocument(body).toJson());
        if (!m_reply) {  // hardening: guard against null reply
            emit error(QStringLiteral("Failed to start generate request"));
            return;
        }

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

        // v0.1.70 — thread conversation history into OpenAI-compat requests
        // too. /v1/chat/completions natively supports a multi-message
        // array (any cloud provider that speaks OpenAI does), so we splice
        // the prior turns in before the current user message.
        for (const QJsonValue &v : priorMessages) {
            if (v.isObject()) messages.append(v);
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

        // v0.1.55 — OpenRouter unified reasoning parameter. OpenRouter
        // normalises every provider's reasoning knob (Anthropic extended-
        // thinking max_tokens, OpenAI reasoning_effort, Gemini
        // thinkingBudget) into one `reasoning` field. We only send it on
        // OpenRouter; for other backends the /no_think system-prompt
        // suffix above remains the universal toggle.
        const QString backend = Config::instance().aiBackend;
        if (backend.compare("OpenRouter", Qt::CaseInsensitive) == 0) {
            QJsonObject reasoning;
            if (enableThinking) {
                reasoning["effort"] = "medium";       // OpenAI o-series
                reasoning["max_tokens"] = 4000;       // Anthropic / Gemini
            } else {
                reasoning["exclude"] = true;          // suppress on all
            }
            body["reasoning"] = reasoning;
        }

        QUrl url = isAzureBackend()
                     ? azureUrl("chat/completions")
                     : QUrl(openAiV1Base(m_baseUrl) + "/v1/chat/completions");
        if (!url.isValid()) {  // hardening: invalid url short-circuit
            emit error(QStringLiteral("Invalid backend URL: ") + url.toString());
            return;
        }
        NOTEPATRA_NO_CLOUD_GATE(url);   // v0.1.72 cloud-free refusal
        QNetworkRequest req(url);
        req.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
        req.setTransferTimeout(120000);  // hardening: 120 s OpenAI-compat stream ceiling
        // llama-server ignores Authorization; LM Studio / OpenAI expect
        // Bearer; Azure expects api-key header. setOpenAiAuth normalises.
        setOpenAiAuth(req, Config::instance().aiKeyForBackend(Config::instance().aiBackend));
        m_reply = m_nam->post(req, QJsonDocument(body).toJson());
        if (!m_reply) {  // hardening: guard against null reply
            emit error(QStringLiteral("Failed to start OpenAI-compat request"));
            return;
        }

        connect(m_reply, &QNetworkReply::readyRead, this, &OllamaClient::onReadyReadOpenAI);
        connect(m_reply, &QNetworkReply::finished,  this, &OllamaClient::onFinishedOpenAI);
    }

    if (!m_reply) return;  // hardening: skip wiring if every branch failed to allocate
    // Last-resort backstop. The onFinished* handlers (connected first, so
    // they run first) now check m_reply->error() themselves, null m_reply
    // and set m_done before this lambda runs — so it only ever fires if a
    // future finished-handler forgets the error short-circuit. m_done is
    // set before emitting to keep the single-outcome contract.
    connect(m_reply, &QNetworkReply::finished, this, [this]() {
        if (m_reply && m_reply->error() != QNetworkReply::NoError && !m_done) {
            m_done = true;
            emit error(friendlyTransportMessage(m_reply->errorString()));
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
    if (!m_nam) {  // hardening: guard NAM
        emit error(QStringLiteral("Network manager unavailable"));
        return;
    }
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
        if (!url.isValid()) {  // hardening: invalid base-url short-circuit
            emit error(QStringLiteral("Invalid backend URL: ") + m_baseUrl);
            return;
        }
        NOTEPATRA_NO_CLOUD_GATE(url);   // v0.1.72 cloud-free refusal
        QNetworkRequest req(url);
        req.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
        req.setTransferTimeout(120000);  // hardening: 120 s ceiling on tool-result continuation
        m_reply = m_nam->post(req, QJsonDocument(body).toJson());
        if (!m_reply) {  // hardening: guard against null reply
            emit error(QStringLiteral("Failed to start tool-result continuation"));
            return;
        }
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

        QUrl url = isAzureBackend()
                     ? azureUrl("chat/completions")
                     : QUrl(openAiV1Base(m_baseUrl) + "/v1/chat/completions");
        if (!url.isValid()) {  // hardening: invalid url short-circuit
            emit error(QStringLiteral("Invalid backend URL: ") + url.toString());
            return;
        }
        NOTEPATRA_NO_CLOUD_GATE(url);   // v0.1.72 cloud-free refusal
        QNetworkRequest req(url);
        req.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
        req.setTransferTimeout(120000);  // hardening: 120 s ceiling on OpenAI-compat continuation
        setOpenAiAuth(req, Config::instance().aiKeyForBackend(Config::instance().aiBackend));
        m_reply = m_nam->post(req, QJsonDocument(body).toJson());
        if (!m_reply) {  // hardening: guard against null reply
            emit error(QStringLiteral("Failed to start OpenAI-compat continuation"));
            return;
        }
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
        QJsonParseError perr{};  // hardening: capture parse errors
        QJsonDocument doc = QJsonDocument::fromJson(line, &perr);
        if (perr.error != QJsonParseError::NoError || doc.isNull() || !doc.isObject()) continue;  // hardening: skip malformed/non-object frames
        QJsonObject obj = doc.object();
        if (obj.contains("error")) {
            // Single-outcome guard: mark the request terminal BEFORE emitting
            // so onFinishedOllama's error short-circuit (and its finished()/
            // stats emission) can't fire a second signal for this request.
            m_done = true;
            emit error(obj["error"].toString());
            return;
        }

        // /api/generate path — `response` field
        QString token = obj["response"].toString();
        // /api/chat path — `message.content` + `message.tool_calls`
        if (obj.contains("message")) {
            const QJsonValue mv = obj.value("message");
            if (!mv.isObject()) continue;  // hardening: tolerate non-object message field
            QJsonObject msg = mv.toObject();
            QString content = msg.value("content").toString();
            if (!content.isEmpty()) {
                token = content;
            }
            if (msg.contains("tool_calls")) {
                const QJsonValue tcv = msg.value("tool_calls");
                if (!tcv.isArray()) {  // hardening: tolerate non-array tool_calls
                    // skip — fall through to token emit below
                } else {
                QJsonArray calls = tcv.toArray();
                for (const QJsonValue &cv : calls) {
                    if (!cv.isObject()) continue;  // hardening: skip non-object call entries
                    QJsonObject c = cv.toObject();
                    const QJsonValue fnv = c.value("function");
                    QJsonObject fn = fnv.isObject() ? fnv.toObject() : QJsonObject();  // hardening: type-check function
                    QString name = fn.value("name").toString();
                    // Ollama's `arguments` is already a parsed JSON object
                    // (NOT a stringified JSON like OpenAI canonical). Be
                    // defensive: accept both shapes.
                    // v0.1.40: surface JSON parse failures via the
                    // `_notepatra_parse_error` marker so AIPanel can return
                    // a structured tool result to the model instead of
                    // silently passing empty args.
                    QJsonObject args;
                    QJsonValue av = fn.value("arguments");
                    if (av.isObject()) {
                        args = av.toObject();
                    } else if (av.isString()) {
                        const QByteArray rawArgs = av.toString().toUtf8();
                        QJsonParseError perr;
                        QJsonDocument d = QJsonDocument::fromJson(rawArgs, &perr);
                        if (perr.error != QJsonParseError::NoError && !rawArgs.trimmed().isEmpty()) {
                            args["_notepatra_parse_error"] = perr.errorString();
                            args["_notepatra_raw_args"] = QString::fromUtf8(rawArgs);
                        } else {
                            args = d.object();
                        }
                    }
                    // Synthesize a client-side ID since Ollama doesn't
                    // supply one. AIPanel will round-trip it back on
                    // continueWithToolResults().
                    QString id = QString("call_n%1").arg(++m_toolCallSeq);
                    emit toolCallReceived(id, name, args);
                }
                }  // hardening: close else (isArray) branch
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
            AiInteractionLog::recordAssistant(
                QStringLiteral("ollama"), m_model, m_mode, m_fullResponse,
                m_promptTokens, m_evalTokens, int(elapsed));
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
                    QString bt;
                    switch (m_backend) {
                        case LlamaCpp:     bt = "llama.cpp"; break;
                        case OpenAICompat:
                            if (m_baseUrl.contains("openai.azure.com", Qt::CaseInsensitive))      bt = "azure-openai";
                            else if (m_baseUrl.contains("api.openai.com", Qt::CaseInsensitive))   bt = "openai";
                            else if (m_baseUrl.contains("ollama.com", Qt::CaseInsensitive))       bt = "ollama-cloud";
                            else if (m_baseUrl.contains("openrouter.ai", Qt::CaseInsensitive))    bt = "openrouter";
                            else                                                                  bt = "openai-compat";
                            break;
                        default:           bt = "openai-compat"; break;
                    }
                    AiInteractionLog::recordAssistant(bt, m_model, m_mode,
                        m_fullResponse, m_promptTokens, m_evalTokens, int(elapsed));
                }
                continue;
            }
            QJsonParseError perr{};  // hardening: capture parse error
            QJsonDocument doc = QJsonDocument::fromJson(payload, &perr);
            if (perr.error != QJsonParseError::NoError || !doc.isObject()) continue;  // hardening: skip malformed/non-object
            QJsonObject obj = doc.object();
            if (obj.contains("error")) {
                QJsonValue err = obj.value("error");
                QString msg = err.isObject() ? err.toObject().value("message").toString()
                                              : err.toString();
                if (!msg.isEmpty()) {
                    // Single-outcome guard — same as the Ollama path: block
                    // onFinishedOpenAI from emitting a second error() or a
                    // phantom finished() for this already-failed request.
                    m_done = true;
                    emit error(msg);
                }
                return;
            }
            // OpenAI-compat servers emit a usage object on the final
            // chunk (or sometimes a separate non-choice frame). Capture
            // it before we look at choices.
            if (obj.contains("usage")) {
                const QJsonValue uv = obj.value("usage");
                if (uv.isObject()) {  // hardening: type-check usage
                    QJsonObject u = uv.toObject();
                    if (u.contains("prompt_tokens"))      m_promptTokens = u.value("prompt_tokens").toInt();
                    if (u.contains("completion_tokens"))  m_evalTokens   = u.value("completion_tokens").toInt();
                }
            }
            // choices[0].delta.content — the streaming chunk token
            const QJsonValue chv = obj.value("choices");
            if (!chv.isArray()) continue;  // hardening: skip frames without choices array
            QJsonArray choices = chv.toArray();
            if (choices.isEmpty()) continue;
            const QJsonValue cf = choices.first();
            if (!cf.isObject()) continue;  // hardening: skip malformed first choice
            QJsonObject choice = cf.toObject();
            const QJsonValue dv = choice.value("delta");
            QJsonObject delta  = dv.isObject() ? dv.toObject() : QJsonObject();  // hardening: type-check delta
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
                const QJsonValue tcv = delta.value("tool_calls");
                if (tcv.isArray()) {  // hardening: tolerate non-array tool_calls field
                QJsonArray calls = tcv.toArray();
                for (const QJsonValue &cv : calls) {
                    if (!cv.isObject()) continue;  // hardening: skip malformed call entries
                    QJsonObject c = cv.toObject();
                    int idx = c.value("index").toInt(0);
                    PendingToolCall &p = m_pendingToolCalls[idx];
                    if (c.contains("id") && !c.value("id").toString().isEmpty()) {
                        p.id = c.value("id").toString();
                    }
                    const QJsonValue fnv = c.value("function");
                    QJsonObject fn = fnv.isObject() ? fnv.toObject() : QJsonObject();  // hardening: type-check function
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
                }  // hardening: close isArray guard
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
                    // v0.1.40: same parse-error surfacing as the Ollama path.
                    const QByteArray rawArgs = p.argsBuffer.toUtf8();
                    QJsonParseError perr;
                    QJsonDocument d = QJsonDocument::fromJson(rawArgs, &perr);
                    QJsonObject args;
                    if (perr.error != QJsonParseError::NoError && !rawArgs.trimmed().isEmpty()) {
                        args["_notepatra_parse_error"] = perr.errorString();
                        args["_notepatra_raw_args"] = QString::fromUtf8(rawArgs);
                    } else {
                        args = d.object();
                    }
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
                    QString bt;
                    switch (m_backend) {
                        case LlamaCpp:     bt = "llama.cpp"; break;
                        case OpenAICompat:
                            if (m_baseUrl.contains("openai.azure.com", Qt::CaseInsensitive))      bt = "azure-openai";
                            else if (m_baseUrl.contains("api.openai.com", Qt::CaseInsensitive))   bt = "openai";
                            else if (m_baseUrl.contains("ollama.com", Qt::CaseInsensitive))       bt = "ollama-cloud";
                            else if (m_baseUrl.contains("openrouter.ai", Qt::CaseInsensitive))    bt = "openrouter";
                            else                                                                  bt = "openai-compat";
                            break;
                        default:           bt = "openai-compat"; break;
                    }
                    AiInteractionLog::recordAssistant(bt, m_model, m_mode,
                        m_fullResponse, m_promptTokens, m_evalTokens, int(elapsed));
                }
                continue;
            }

            if (!finishReason.isEmpty() && !m_done) {
                m_done = true;
                const qint64 elapsed = QDateTime::currentMSecsSinceEpoch() - m_startMs;
                emit finished(m_fullResponse);
                emit responseStats(m_promptTokens, m_evalTokens, elapsed);
                QString bt;
                switch (m_backend) {
                    case LlamaCpp:     bt = "llama.cpp"; break;
                    case OpenAICompat:
                        if (m_baseUrl.contains("openai.azure.com", Qt::CaseInsensitive))    bt = "azure-openai";
                        else if (m_baseUrl.contains("api.openai.com", Qt::CaseInsensitive)) bt = "openai";
                        else if (m_baseUrl.contains("ollama.com", Qt::CaseInsensitive))     bt = "ollama-cloud";
                        else if (m_baseUrl.contains("openrouter.ai", Qt::CaseInsensitive))  bt = "openrouter";
                        else                                                                bt = "openai-compat";
                        break;
                    default:           bt = "openai-compat"; break;
                }
                AiInteractionLog::recordAssistant(bt, m_model, m_mode,
                    m_fullResponse, m_promptTokens, m_evalTokens, int(elapsed));
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
    const int httpStatus =
        m_reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();

    // ── Transport / HTTP error short-circuit — mirrors the v0.1.98 fix in
    // onFinishedOpenAI. Ollama down → /api/generate (or /api/chat) gets
    // connection-refused: no NDJSON ever arrives, so the loop below parses
    // nothing, m_fullResponse stays empty and neither finished() nor error()
    // fires — and because we null m_reply at the bottom, the safety-net
    // lambda in generate() skips too. Net effect pre-fix: the caller hung
    // forever (Noter audit, CRITICAL). Check the reply's error FIRST and
    // surface the friendly per-backend message. Gated on
    // m_fullResponse.isEmpty() so a mid-stream drop still flushes whatever
    // partial text already arrived (same contract as the OpenAI path).
    // Cleanup precedes the emit so a caller that reacts to error() by
    // calling cancel() finds no live reply (the Extract crash class —
    // see notes.cpp).
    if (!m_done && m_fullResponse.isEmpty() &&
        (m_reply->error() != QNetworkReply::NoError || httpStatus >= 400)) {
        // Ollama HTTP failures carry an {"error":"..."} JSON body (e.g.
        // 404 "model not found"); transport failures (refused /
        // unreachable / timeout) carry nothing useful — map those to the
        // friendly "start the server" hint instead.
        const QString msg = (httpStatus >= 400)
            ? httpErrorMessage(httpStatus, remaining,
                               friendlyTransportMessage(m_reply->errorString()))
            : friendlyTransportMessage(m_reply->errorString());
        m_done = true;
        m_reply->deleteLater();
        m_reply = nullptr;
        emit error(msg);
        return;
    }

    for (const QByteArray &line : remaining.split('\n')) {
        if (line.trimmed().isEmpty()) continue;
        QJsonParseError perr{};  // hardening: capture parse error
        QJsonDocument doc = QJsonDocument::fromJson(line, &perr);
        if (perr.error != QJsonParseError::NoError || doc.isNull() || !doc.isObject()) continue;  // hardening: skip malformed/non-object lines
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
    if (m_reply) {  // hardening: re-check pointer (cancel may have nulled it)
        m_reply->deleteLater();
        m_reply = nullptr;
    }
}

void OllamaClient::onFinishedOpenAI() {
    if (!m_reply) return;
    // Capture trailing bytes + HTTP status once, before parsing/cleanup.
    m_sseBuffer += m_reply->readAll();
    const int httpStatus =
        m_reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();

    // ── HTTP / transport error short-circuit (v0.1.98) ──────────────────
    // When a chat stream fails before any token arrives — bad/expired key
    // (401 "User not found"), out of credit (402), rate limit (429), or a
    // transport timeout — the body is a plain JSON error, not an SSE "data:"
    // frame, so onReadyReadOpenAI() silently skips it. And since we null
    // m_reply below, the safety-net lambda in generate() short-circuits too.
    // Without this block neither finished() nor error() fires and the caller
    // spins to its own timeout. Gated on m_fullResponse.isEmpty() so a
    // mid-stream drop still flushes whatever partial text already arrived.
    if (!m_done && m_fullResponse.isEmpty() &&
        (m_reply->error() != QNetworkReply::NoError || httpStatus >= 400)) {
        const int sc = (httpStatus >= 400) ? httpStatus : 0;
        // Route the transport-error fallback (sc == 0 — refused / unreachable
        // / timeout, no HTTP status) through the friendly per-backend wording
        // so "llama-server not running. Start it: …" actually surfaces.
        const QString msg =
            httpErrorMessage(sc, m_sseBuffer,
                             friendlyTransportMessage(m_reply->errorString()));
        m_done = true;
        m_sseBuffer.clear();
        m_reply->deleteLater();
        m_reply = nullptr;
        emit error(msg);
        return;
    }

    // Flush any trailing SSE data (success path).
    if (!m_sseBuffer.isEmpty()) {
        m_sseBuffer += "\n\n";  // force a final frame boundary
        onReadyReadOpenAI();
    }
    if (!m_done && !m_fullResponse.isEmpty()) {
        m_done = true;
        emit finished(m_fullResponse);
    }
    if (m_reply) {  // hardening: re-check pointer (cancel may have nulled it during onReadyReadOpenAI)
        m_reply->deleteLater();
        m_reply = nullptr;
    }
}

// ─── friendlyTransportMessage ──────────────────────────────────────────
// Map a raw QNetworkReply::errorString() onto the per-backend "how do I
// start the server" hint. This is the SINGLE source of the friendly
// wording: onError and both onFinished* transport-error short-circuits
// route through it. Pre-fix, this wording lived only in onError — which
// was connected nowhere, so "Ollama not running" never reached the user;
// errors are routed through the finished() handlers instead (the same way
// the v0.1.98 OpenAI fix does), which Qt guarantees to fire after every
// errorOccurred and which also have the response body for rich HTTP
// messages.
QString OllamaClient::friendlyTransportMessage(const QString &raw) const {
    QString msg = raw.isEmpty() ? QStringLiteral("Connection failed") : raw;
    if (msg.contains(QLatin1String("Connection refused"), Qt::CaseInsensitive)) {
        if (m_backend == Ollama)
            msg = QStringLiteral("Ollama not running. Start it with: ollama serve");
        else if (m_backend == LlamaCpp)
            msg = QStringLiteral("llama-server not running. Start it: "
                                 "llama-server -m <model.gguf> --port 8080");
        else
            msg = QStringLiteral("No local-AI server reachable at ") + m_baseUrl;
    }
    return msg;
}

void OllamaClient::onError(QNetworkReply::NetworkError) {
    // Defensive: errors normally arrive via the onFinished* short-circuits
    // (single emission point, post-cleanup). If this slot ever gets wired to
    // QNetworkReply::errorOccurred, the m_done guard keeps the single-outcome
    // contract — never error() after finished() or a second error().
    if (m_done) return;
    m_done = true;
    emit error(friendlyTransportMessage(
        m_reply ? m_reply->errorString() : QString()));
}
