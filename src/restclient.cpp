#include "restclient.h"
#include <QVBoxLayout>
#include <QLabel>
#include <QFont>
#include <QNetworkRequest>
#include <QNetworkReply>
#include <QJsonDocument>
#include <QUrl>
#include <QElapsedTimer>
#include <QScrollBar>
#include <QBuffer>
#include <QPushButton>
#include <QApplication>
#include <QClipboard>

RestClient::RestClient(QWidget *parent) : QWidget(parent) {
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);

    auto *header = new QLabel("  HTTP Response");
    header->setFixedHeight(22);
    header->setStyleSheet("font-weight: bold; background: #2D2D2D; color: #569CD6; padding: 2px 6px;");
    layout->addWidget(header);

    m_output = new QTextEdit;
    m_output->setReadOnly(true);
    QFont mono("Consolas", 10);
    mono.setStyleHint(QFont::Monospace);
    m_output->setFont(mono);
    m_output->setStyleSheet("QTextEdit { background: #1E1E1E; color: #D4D4D4; border: none; padding: 8px; }");
    m_output->setPlaceholderText("Send an HTTP request to see the response here.\n\nUse .http files or select a request block and press Ctrl+Shift+R");
    layout->addWidget(m_output, 1);

    auto *btnRow = new QHBoxLayout;
    btnRow->setContentsMargins(4, 4, 4, 4);
    btnRow->addStretch();
    auto *copyBtn = new QPushButton("Copy Output");
    copyBtn->setFixedHeight(26);
    connect(copyBtn, &QPushButton::clicked, this, [this]() {
        QApplication::clipboard()->setText(m_output->toPlainText());
    });
    btnRow->addWidget(copyBtn);
    layout->addLayout(btnRow);

    m_nam = new QNetworkAccessManager(this);
}

void RestClient::executeRequest(const QString &httpText) {
    // Split by ### (multiple requests)
    QStringList blocks = httpText.split("###");
    if (blocks.isEmpty()) return;

    // Execute first non-empty block
    for (const QString &block : blocks) {
        QString trimmed = block.trimmed();
        if (!trimmed.isEmpty()) {
            parseAndSend(trimmed);
            break;
        }
    }
}

void RestClient::parseAndSend(const QString &block) {
    QStringList lines = block.split('\n');
    if (lines.isEmpty()) return;

    // Parse first line: METHOD URL
    QString firstLine = lines[0].trimmed();
    // Skip comment lines
    while (firstLine.startsWith("#") || firstLine.startsWith("//")) {
        lines.removeFirst();
        if (lines.isEmpty()) return;
        firstLine = lines[0].trimmed();
    }

    QStringList parts = firstLine.split(' ');
    if (parts.size() < 2) {
        m_output->setText("Error: First line must be METHOD URL (e.g., GET https://api.example.com)");
        return;
    }

    QString method = parts[0].toUpper();
    QString url = parts[1];

    // Parse headers and body
    QMap<QString, QString> headers;
    QString body;
    bool inBody = false;

    for (int i = 1; i < lines.size(); i++) {
        QString line = lines[i];
        if (line.trimmed().isEmpty() && !inBody) {
            inBody = true;
            continue;
        }
        if (inBody) {
            body += line + "\n";
        } else {
            int colonIdx = line.indexOf(':');
            if (colonIdx > 0) {
                headers[line.left(colonIdx).trimmed()] = line.mid(colonIdx + 1).trimmed();
            }
        }
    }

    // Build request
    QUrl qurl(url);
    QNetworkRequest req(qurl);
    for (auto it = headers.begin(); it != headers.end(); ++it) {
        req.setRawHeader(it.key().toUtf8(), it.value().toUtf8());
    }

    m_output->clear();
    m_output->append(QString("<span style='color:#569CD6;'>%1</span> <span style='color:#4EC9B0;'>%2</span>")
                     .arg(method, url));
    m_output->append("Sending...\n");

    QElapsedTimer timer;
    timer.start();

    QNetworkReply *reply = nullptr;
    QByteArray bodyBytes = body.trimmed().toUtf8();

    if (method == "GET") reply = m_nam->get(req);
    else if (method == "POST") reply = m_nam->post(req, bodyBytes);
    else if (method == "PUT") reply = m_nam->put(req, bodyBytes);
    else if (method == "DELETE") reply = m_nam->deleteResource(req);
    else if (method == "PATCH") {
        QBuffer *buf = new QBuffer;
        buf->setData(bodyBytes);
        buf->open(QIODevice::ReadOnly);
        reply = m_nam->sendCustomRequest(req, "PATCH", buf);
        buf->setParent(reply); // auto-delete with reply
    } else if (method == "HEAD") {
        reply = m_nam->head(req);
    } else {
        m_output->append("Unsupported method: " + method);
        return;
    }

    connect(reply, &QNetworkReply::finished, this, [this, reply, timer]() {
        qint64 elapsed = timer.elapsed();
        int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        QString reason = reply->attribute(QNetworkRequest::HttpReasonPhraseAttribute).toString();
        QByteArray responseBody = reply->readAll();

        // Status line
        QString statusColor = (status >= 200 && status < 300) ? "#4EC9B0" :
                              (status >= 400) ? "#F44747" : "#DCDCAA";
        m_output->append(QString("\n<span style='color:%1;'>HTTP %2 %3</span>  (%4 ms)")
                         .arg(statusColor).arg(status).arg(reason).arg(elapsed));

        // Response headers
        m_output->append("\n<span style='color:#808080;'>--- Response Headers ---</span>");
        for (const auto &pair : reply->rawHeaderPairs()) {
            m_output->append(QString("<span style='color:#9CDCFE;'>%1</span>: %2")
                             .arg(QString::fromUtf8(pair.first), QString::fromUtf8(pair.second)));
        }

        // Response body
        m_output->append("\n<span style='color:#808080;'>--- Response Body ---</span>\n");

        // Try to pretty-print JSON
        QJsonDocument doc = QJsonDocument::fromJson(responseBody);
        if (!doc.isNull()) {
            m_output->append(doc.toJson(QJsonDocument::Indented));
        } else {
            m_output->append(QString::fromUtf8(responseBody));
        }

        reply->deleteLater();
    });
}
