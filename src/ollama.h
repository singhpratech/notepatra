#ifndef OLLAMA_H
#define OLLAMA_H

#include <QObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QString>

class OllamaClient : public QObject {
    Q_OBJECT
public:
    explicit OllamaClient(QObject *parent = nullptr);

    void setBaseUrl(const QString &url) { m_baseUrl = url; }
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
    QNetworkAccessManager *m_nam;
    QNetworkReply *m_reply = nullptr;
    QString m_baseUrl = "http://localhost:11434";
    QString m_model = "qwen3.5:9b";
    QString m_fullResponse;
    bool m_done = false;
};

#endif
