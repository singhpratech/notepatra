// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef OLLAMA_H
#define OLLAMA_H

#include <QHash>
#include <QJsonArray>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QObject>
#include <QString>

class OllamaClient : public QObject {
    Q_OBJECT
public:
    // Which local-AI backend to talk to. The class is still called
    // OllamaClient for source-compatibility with everything that
    // references it, but it can now drive llama.cpp's `llama-server`
    // and any OpenAI-compatible local endpoint (LM Studio, Jan,
    // text-generation-webui, etc.) via the OpenAICompat branch.
    enum Backend {
        Ollama = 0,       // http://localhost:11434 — /api/tags + /api/generate
        LlamaCpp = 1,     // http://localhost:8080  — /v1/models + /v1/chat/completions
        OpenAICompat = 2  // any URL — same OpenAI endpoints as LlamaCpp
    };
    static Backend backendFromString(const QString &s);
    static QString  backendToString(Backend b);

    explicit OllamaClient(QObject *parent = nullptr);

    void setBackend(Backend b) { m_backend = b; }
    Backend backend() const { return m_backend; }
    void setBaseUrl(const QString &url) { m_baseUrl = url; }
    QString baseUrl() const { return m_baseUrl; }
    void setModel(const QString &model) { m_model = model; }
    QString model() const { return m_model; }
    // v0.1.71 — let the panel tag each request with the active mode
    // ("chat" / "coding" / "data") so the AI interaction log can filter
    // by mode without round-tripping that state out-of-band. Setting is
    // cheap and harmless; defaults to "".
    void setMode(const QString &mode) { m_mode = mode; }
    QString mode() const { return m_mode; }

    // think=false disables Qwen3-style thinking blocks (the model will not
    // emit <think>...</think> reasoning before its answer). Default is false
    // because thinking blocks break the JSON Tools / Format flow which
    // expects clean parseable output. Set true for the AI Assistant chat
    // panel where the user might want to see reasoning.
    //
    // images is an optional list of base64-encoded image data (no data URI
    // prefix, just the raw base64). Pass to vision models like llava,
    // llama3.2-vision, qwen2-vl, moondream, etc. Models that don't support
    // images ignore the field.
    // v0.1.70 — priorMessages threads multi-turn conversation history
    // into the API payload. Pre-v0.1.70 every send() built a fresh
    // [system, user] array and the model never saw prior turns — turn N
    // landed as if it were turn 1. AIPanel now builds the history from
    // activeMessages() (skipping Error roles + the just-appended current
    // user message) and passes it here. Empty array = single-turn (old
    // behaviour, fine for first send in a session).
    void generate(const QString &prompt, const QString &systemPrompt = "",
                  bool enableThinking = false,
                  const QStringList &imagesBase64 = QStringList(),
                  const QJsonArray &tools = QJsonArray(),
                  const QJsonArray &priorMessages = QJsonArray());
    // Continue an in-progress agent conversation by sending one or more
    // tool-result messages back to the model and starting a fresh stream.
    // toolResults is a JSON array of { id, name, content } objects (one
    // per executed tool call). systemPrompt + history are reused from the
    // last generate() call so the model retains the conversation. Used
    // by AIPanel's agent loop after handleToolCall executes a tool.
    // v0.1.112 — systemNote (optional): a one-shot role:system message
    // appended AFTER this batch's tool results. The agent loop uses it to
    // inject the perseveration-breaker nudge ("the identical call failed
    // twice; change strategy") exactly once, without touching the sticky
    // system prompt.
    void continueWithToolResults(const QJsonArray &toolResults,
                                 const QString &systemPrompt = "",
                                 const QJsonArray &tools = QJsonArray(),
                                 const QString &systemNote = QString());
    void cancel();
    bool isAvailable();
    void listModels();   // async — emits modelsListed or modelsError
    // v0.1.55 — async probe of Ollama's /api/show, which returns the
    // model's capabilities array (["completion", "tools", "thinking",
    // "vision", …]). Emits modelCapabilitiesLoaded on success or
    // modelCapabilitiesError on failure. Only meaningful when m_backend
    // == Ollama; OpenAI-compat servers don't expose this metadata so
    // AIPanel falls back to the substring allowlist for them.
    void showModel(const QString &name);

signals:
    void tokenReceived(const QString &token);
    void finished(const QString &fullResponse);
    // v0.1.115 — first-class mid-stream truncation. Emitted IN ADDITION to
    // finished() (which still fires, unchanged, so every existing caller keeps
    // working) whenever a response ended WITHOUT a clean completion marker:
    //   * the transport dropped mid-stream — no Ollama `done:true` frame and
    //     no OpenAI SSE `[DONE]`/finish_reason ever arrived, yet partial text
    //     had already accumulated; or
    //   * the backend reported finish_reason "length" (hit max_tokens / the
    //     context window) or "content_filter".
    // `partial` is the text received so far (the SAME value the paired
    // finished() carries). `reason` is a stable kebab token the UI can switch
    // on — one of:
    //   "network-drop"   — transport-level failure (reset / timeout); the
    //                       Qt error string follows after ": " for context.
    //   "backend-abort"  — server closed the stream cleanly but never sent a
    //                       completion marker (model unloaded / OOM / killed).
    //   "context-limit"  — finish_reason == "length" (output/context exhausted).
    //   "content-filter" — finish_reason == "content_filter".
    // The token is everything before the first ": "; any trailing detail is
    // human-readable only. The UI should render "response cut off — Retry" for
    // this instead of treating the paired finished() as a completed answer.
    void finishedTruncated(const QString &partial, const QString &reason);
    void error(const QString &message);
    void modelsListed(const QStringList &models);
    // v0.1.54 — rich variant carrying the raw /v1/models response for
    // backends that publish metadata (OpenRouter pricing, context length,
    // provider breakdown). Emitted alongside modelsListed for OpenAI-compat
    // backends; aipanel uses the richer data to render grouped + priced
    // dropdown entries instead of the flat alphabetical list.
    void modelsListedRich(const QJsonArray &dataArray);
    void modelsError(const QString &reason);
    // v0.1.55 — outcome of /api/show probe. `caps` is the lowercased
    // capabilities list as Ollama reports it ("tools", "thinking",
    // "vision", "completion", "embedding"). Authoritative — replaces
    // the hardcoded substring allowlist for local models.
    void modelCapabilitiesLoaded(const QString &model, const QStringList &caps);
    void modelCapabilitiesError(const QString &model, const QString &reason);

    // v0.1.35 — Emitted when the backend's stream contains a tool_calls
    // frame (Ollama atomic NDJSON / OpenAI accumulated SSE). `id` is
    // synthesized client-side for Ollama (which doesn't supply one) and
    // backend-supplied for OpenAI-compat. AIPanel listens for this,
    // executes the tool against the workspace, and replies via
    // continueWithToolResults().
    void toolCallReceived(const QString &id, const QString &name,
                          const QJsonObject &args);

    // Emitted right after finished() when the backend reports stats. Both
    // Ollama and OpenAI-compatible endpoints provide these in their final
    // chunk:
    //   - Ollama:        eval_count + prompt_eval_count + total_duration (ns)
    //   - OpenAI-compat: usage.prompt_tokens + usage.completion_tokens
    //                    (no timing -- we measure wall-clock via QElapsedTimer)
    // promptTokens or evalTokens may be -1 if the backend didn't report them.
    // elapsedMs is wall-clock from generate() to finished(), always populated.
    void responseStats(int promptTokens, int evalTokens, qint64 elapsedMs);

private slots:
    void onReadyRead();
    void onFinished();
    void onError(QNetworkReply::NetworkError err);

private:
    // Dispatch helpers — each backend has its own wire format for
    // streaming token output and model listing.
    void onReadyReadOllama();
    void onReadyReadOpenAI();
    void onFinishedOllama();
    void onFinishedOpenAI();
    // Single source of the per-backend "how do I start the server" wording
    // ("Ollama not running. Start it with: ollama serve", …). onError and
    // BOTH onFinished* transport-error short-circuits route through it, so
    // the friendly hint surfaces no matter which path catches the failure.
    QString friendlyTransportMessage(const QString &raw) const;

    QNetworkAccessManager *m_nam;
    QNetworkReply *m_reply = nullptr;
    Backend m_backend = Ollama;
    QString m_baseUrl = "http://localhost:11434";
    QString m_model = "qwen2.5-coder:3b";
    QString m_mode;             // v0.1.71 — "chat" / "coding" / "data" tag for the interaction log
    QString m_fullResponse;
    QByteArray m_sseBuffer;  // for OpenAI SSE — frames span packets
    bool m_done = false;

    // v0.1.35 — Conversation history for the agent loop. Each entry is
    // a chat message ({role, content, tool_calls?, tool_call_id?}). The
    // history is appended to on every generate() / continueWithToolResults
    // and resent so multi-turn tool conversations work. Cleared at the
    // start of a fresh generate() call (no tool round-trip in flight).
    QJsonArray m_messages;
    QString m_lastSystemPrompt;
    QJsonArray m_lastTools;
    int m_toolCallSeq = 0;  // incrementing counter for synthetic Ollama tool_call IDs

    // Per-call accumulator for OpenAI-compat streamed tool_call argument
    // fragments. Keyed by the `index` field of each tool_call delta —
    // each fragment appends to the entry until finish_reason: tool_calls.
    struct PendingToolCall {
        QString id;
        QString name;
        QString argsBuffer;
    };
    QHash<int, PendingToolCall> m_pendingToolCalls;

    // Wall-clock timer + token counts captured from the streaming
    // response. -1 means "not reported by this backend / not yet known".
    qint64 m_startMs = 0;
    int m_promptTokens = -1;
    int m_evalTokens = -1;
};

#endif
