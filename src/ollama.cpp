#include "ollama.h"
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkRequest>
#include <QUrl>

OllamaClient::OllamaClient(QObject *parent) : QObject(parent) {
    m_nam = new QNetworkAccessManager(this);
}

bool OllamaClient::isAvailable() {
    QUrl url(m_baseUrl + "/api/tags");
    QNetworkRequest req(url);
    auto *reply = m_nam->get(req);
    reply->waitForReadyRead(2000);
    bool ok = reply->error() == QNetworkReply::NoError;
    reply->deleteLater();
    return ok;
}

void OllamaClient::generate(const QString &prompt, const QString &systemPrompt) {
    cancel();
    m_fullResponse.clear();
    m_done = false;

    QJsonObject body;
    body["model"] = m_model;
    body["prompt"] = prompt;
    body["stream"] = true;
    if (!systemPrompt.isEmpty())
        body["system"] = systemPrompt;

    QJsonObject options;
    options["temperature"] = 0.3;
    options["num_predict"] = 4096;
    body["options"] = options;

    QUrl url(m_baseUrl + "/api/generate");
    QNetworkRequest req(url);
    req.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");

    m_reply = m_nam->post(req, QJsonDocument(body).toJson());

    connect(m_reply, &QNetworkReply::readyRead, this, &OllamaClient::onReadyRead);
    connect(m_reply, &QNetworkReply::finished, this, &OllamaClient::onFinished);
    // Use errorOccurred on newer Qt, error signal on older
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
}

void OllamaClient::onReadyRead() {
    if (!m_reply) return;

    // Read all available data
    QByteArray data = m_reply->readAll();

    // Split by newlines — each line is a JSON object
    for (const QByteArray &line : data.split('\n')) {
        if (line.trimmed().isEmpty()) continue;

        QJsonDocument doc = QJsonDocument::fromJson(line);
        if (doc.isNull()) continue;

        QJsonObject obj = doc.object();

        // Check for error
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

void OllamaClient::onFinished() {
    if (!m_reply) return;

    // Read any remaining data
    QByteArray remaining = m_reply->readAll();
    for (const QByteArray &line : remaining.split('\n')) {
        if (line.trimmed().isEmpty()) continue;
        QJsonDocument doc = QJsonDocument::fromJson(line);
        if (doc.isNull()) continue;
        QJsonObject obj = doc.object();
        QString token = obj["response"].toString();
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

void OllamaClient::onError(QNetworkReply::NetworkError) {
    QString msg;
    if (m_reply)
        msg = m_reply->errorString();
    else
        msg = "Connection failed";

    if (msg.contains("Connection refused"))
        msg = "Ollama not running. Start it with: ollama serve";

    emit error(msg);
}
