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

    void generate(const QString &prompt, const QString &systemPrompt = "");
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
