#ifndef OLLAMA_H
#define OLLAMA_H

#include <QObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
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
    void generate(const QString &prompt, const QString &systemPrompt = "",
                  bool enableThinking = false,
                  const QStringList &imagesBase64 = QStringList());
    void cancel();
    bool isAvailable();
    void listModels();   // async — emits modelsListed or modelsError

signals:
    void tokenReceived(const QString &token);
    void finished(const QString &fullResponse);
    void error(const QString &message);
    void modelsListed(const QStringList &models);
    void modelsError(const QString &reason);

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

    QNetworkAccessManager *m_nam;
    QNetworkReply *m_reply = nullptr;
    Backend m_backend = Ollama;
    QString m_baseUrl = "http://localhost:11434";
    QString m_model = "qwen2.5-coder:3b";
    QString m_fullResponse;
    QByteArray m_sseBuffer;  // for OpenAI SSE — frames span packets
    bool m_done = false;
};

#endif
