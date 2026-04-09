#include "ollama.h"
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkRequest>
#include <QUrl>
#include <QEventLoop>
#include <QTimer>

OllamaClient::OllamaClient(QObject *parent) : QObject(parent) {
    m_nam = new QNetworkAccessManager(this);
}

bool OllamaClient::isAvailable() {
    // Synchronous probe with hard timeout (reliable on Windows;
    // waitForReadyRead() alone is unreliable for localhost sockets there).
    QUrl url(m_baseUrl + "/api/tags");
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

void OllamaClient::listModels() {
    QUrl url(m_baseUrl + "/api/tags");
    QNetworkRequest req(url);
    auto *reply = m_nam->get(req);

    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        if (reply->error() != QNetworkReply::NoError) {
            QString msg = reply->errorString();
            if (msg.contains("refused", Qt::CaseInsensitive) ||
                msg.contains("unreachable", Qt::CaseInsensitive))
                msg = "Ollama not running. Start it: ollama serve";
            emit modelsError(msg);
            reply->deleteLater();
            return;
        }

        QJsonDocument doc = QJsonDocument::fromJson(reply->readAll());
        QStringList models;
        if (doc.isObject()) {
            QJsonArray arr = doc.object().value("models").toArray();
            for (const QJsonValue &v : arr) {
                QString name = v.toObject().value("name").toString();
                if (!name.isEmpty()) models << name;
            }
        }
        models.sort(Qt::CaseInsensitive);
        emit modelsListed(models);
        reply->deleteLater();
    });
}

void OllamaClient::generate(const QString &prompt, const QString &systemPrompt,
                            bool enableThinking,
                            const QStringList &imagesBase64) {
    cancel();
    m_fullResponse.clear();
    m_done = false;

    QJsonObject body;
    body["model"] = m_model;
    body["prompt"] = prompt;
    body["stream"] = true;
    // For Qwen3 / DeepSeek-R1 / other thinking models, "think": false makes
    // the model skip its <think>...</think> reasoning and go straight to the
    // answer. Older / non-thinking models ignore this field harmlessly.
    body["think"] = enableThinking;
    if (!systemPrompt.isEmpty()) {
        // If thinking is disabled, also tell the model in the system prompt
        // — some models honor /no_think instead of the API field
        if (!enableThinking)
            body["system"] = systemPrompt + "\n/no_think";
        else
            body["system"] = systemPrompt;
    } else if (!enableThinking) {
        body["system"] = "/no_think";
    }

    // Image attachments for vision-capable models (llava, llama3.2-vision,
    // qwen2-vl, moondream, granite-vision, etc.). Non-vision models silently
    // ignore the images field.
    if (!imagesBase64.isEmpty()) {
        QJsonArray imgs;
        for (const QString &b64 : imagesBase64) imgs.append(b64);
        body["images"] = imgs;
    }

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
