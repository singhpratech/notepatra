#include "restclient.h"
#include "fonts.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
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

// ═══════════════════════════════════════════════════════════════════════
// Modern REST client — inspired by Postman / Thunder Client / Bruno.
//
// Layout (top to bottom):
//   ┌─ Request ──────────────────────────────────────────────────────┐
//   │ [GET ▾] [https://api.example.com/users     ] [  Send  ]       │
//   │ ┌── Headers | Body ──────────────────────────────────────────┐ │
//   │ │ Accept: application/json                                   │ │
//   │ │ Authorization: Bearer ...                                  │ │
//   │ └────────────────────────────────────────────────────────────┘ │
//   └─────────────────────────────────────────────────────────────────┘
//   ┌─ Response ─────────────────────────────────────────────────────┐
//   │ 200 OK · 43 ms · 1.2 KB                                       │
//   │ pretty-printed JSON or raw body                                │
//   └─────────────────────────────────────────────────────────────────┘
// ═══════════════════════════════════════════════════════════════════════

RestClient::RestClient(QWidget *parent) : QWidget(parent) {
    auto *root = new QVBoxLayout(this);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(0);

    const QString mono_family = notepatraCodeCssFamily();
    QFont mono = notepatraCodeFont();

    // ─── Header strip ──────────────────────────────────────────────
    auto *header = new QLabel("  🌐  REST Client");
    header->setFixedHeight(26);
    header->setStyleSheet(
        "font-weight: 600; background: #252526; color: #9CDCFE; "
        "padding: 4px 12px; border-bottom: 1px solid #1E1E1E; "
        "font-size: 12px; letter-spacing: 0.04em;");
    root->addWidget(header);

    // ─── Request bar: method dropdown + URL + Send ─────────────────
    auto *reqBarHost = new QWidget;
    reqBarHost->setStyleSheet("background: #1E1E1E;");
    auto *reqBar = new QHBoxLayout(reqBarHost);
    reqBar->setContentsMargins(12, 10, 12, 8);
    reqBar->setSpacing(8);

    m_methodCombo = new QComboBox;
    m_methodCombo->addItems({"GET", "POST", "PUT", "DELETE", "PATCH", "HEAD", "OPTIONS"});
    m_methodCombo->setCurrentText("GET");
    m_methodCombo->setFixedHeight(34);
    m_methodCombo->setFixedWidth(110);
    m_methodCombo->setStyleSheet(
        "QComboBox { background: #252526; color: #4EC9B0; "
        "border: 1px solid #3E3E3E; border-radius: 6px; "
        "padding: 4px 10px; font-weight: 700; font-size: 13px; }"
        "QComboBox:hover { border-color: #4EC9B0; }"
        "QComboBox::drop-down { border: none; width: 20px; }"
        "QComboBox QAbstractItemView { background: #252526; color: #D4D4D4; "
        "selection-background-color: #094771; border: 1px solid #3E3E3E; }");
    reqBar->addWidget(m_methodCombo);

    m_urlInput = new QLineEdit;
    m_urlInput->setPlaceholderText("https://api.example.com/users  (paste URL and hit Send)");
    m_urlInput->setFixedHeight(34);
    m_urlInput->setFont(mono);
    m_urlInput->setStyleSheet(
        "QLineEdit { background: #252526; color: #D4D4D4; "
        "border: 1px solid #3E3E3E; border-radius: 6px; "
        "padding: 6px 12px; font-size: 13px; "
        "selection-background-color: #264F78; }"
        "QLineEdit:focus { border-color: #4EC9B0; }");
    reqBar->addWidget(m_urlInput, 1);

    m_sendBtn = new QPushButton("Send");
    m_sendBtn->setFixedHeight(34);
    m_sendBtn->setFixedWidth(96);
    m_sendBtn->setCursor(Qt::PointingHandCursor);
    m_sendBtn->setStyleSheet(
        "QPushButton { background: #0E639C; color: #FFFFFF; "
        "border: none; border-radius: 6px; "
        "font-size: 13px; font-weight: 600; }"
        "QPushButton:hover { background: #1177BB; }"
        "QPushButton:pressed { background: #0B5182; }"
        "QPushButton:disabled { background: #3E3E3E; color: #888; }");
    reqBar->addWidget(m_sendBtn);
    root->addWidget(reqBarHost);

    // ─── Headers / Body tabs ───────────────────────────────────────
    m_reqTabs = new QTabWidget;
    m_reqTabs->setFixedHeight(170);
    m_reqTabs->setStyleSheet(
        "QTabWidget::pane { background: #1E1E1E; border: none; border-top: 1px solid #2D2D2D; }"
        "QTabBar { background: #1E1E1E; }"
        "QTabBar::tab { background: transparent; color: #888; "
        "padding: 6px 16px; border: none; font-size: 12px; margin-right: 2px; }"
        "QTabBar::tab:selected { color: #9CDCFE; border-bottom: 2px solid #0E639C; }"
        "QTabBar::tab:hover:!selected { color: #CCCCCC; }");

    m_headersInput = new QPlainTextEdit;
    m_headersInput->setFont(mono);
    m_headersInput->setPlaceholderText(
        "Accept: application/json\n"
        "Authorization: Bearer <token>\n"
        "Content-Type: application/json");
    m_headersInput->setStyleSheet(
        "QPlainTextEdit { background: #1E1E1E; color: #D4D4D4; border: none; "
        "padding: 10px; font-size: 12px; "
        "selection-background-color: #264F78; }");
    m_reqTabs->addTab(m_headersInput, "Headers");

    m_bodyInput = new QPlainTextEdit;
    m_bodyInput->setFont(mono);
    m_bodyInput->setPlaceholderText(
        "{\n"
        "  \"name\": \"Alice\",\n"
        "  \"role\": \"admin\"\n"
        "}\n\n"
        "For POST/PUT/PATCH requests. Leave empty for GET/DELETE.");
    m_bodyInput->setStyleSheet(m_headersInput->styleSheet());
    m_reqTabs->addTab(m_bodyInput, "Body");

    root->addWidget(m_reqTabs);

    // ─── Response status badge + body ──────────────────────────────
    m_statusBadge = new QLabel;
    m_statusBadge->setFixedHeight(28);
    m_statusBadge->setStyleSheet(
        "background: #252526; color: #808080; padding: 5px 12px; "
        "border-top: 1px solid #1E1E1E; border-bottom: 1px solid #1E1E1E; "
        "font-size: 11px; font-weight: 600; letter-spacing: 0.05em;");
    m_statusBadge->setText("  Response will appear here.");
    root->addWidget(m_statusBadge);

    m_output = new QTextEdit;
    m_output->setReadOnly(true);
    m_output->setFont(mono);
    m_output->setStyleSheet(
        "QTextEdit { background: #1E1E1E; color: #D4D4D4; "
        "border: none; padding: 12px; "
        "selection-background-color: #264F78; }"
        "QScrollBar:vertical { background: #1E1E1E; width: 10px; }"
        "QScrollBar::handle:vertical { background: #3E3E3E; "
        "border-radius: 5px; min-height: 40px; margin: 2px; }"
        "QScrollBar::handle:vertical:hover { background: #555; }"
        "QScrollBar::add-line:vertical, QScrollBar::sub-line:vertical { height: 0; }");
    m_output->setPlaceholderText(
        "Set a method + URL above, optionally fill Headers and Body tabs, "
        "then press Send. Pretty-printed JSON and full response headers "
        "will show here.");
    root->addWidget(m_output, 1);

    // ─── Bottom action row ─────────────────────────────────────────
    auto *btnRowHost = new QWidget;
    btnRowHost->setStyleSheet("background: #252526; border-top: 1px solid #1E1E1E;");
    auto *btnRow = new QHBoxLayout(btnRowHost);
    btnRow->setContentsMargins(8, 4, 8, 4);
    btnRow->addStretch();
    auto *copyBtn = new QPushButton("Copy Response");
    copyBtn->setFixedHeight(26);
    copyBtn->setStyleSheet(
        "QPushButton { background: transparent; color: #CCCCCC; "
        "border: 1px solid #3E3E3E; border-radius: 4px; padding: 4px 14px; "
        "font-size: 11px; }"
        "QPushButton:hover { background: #2D2D2D; border-color: #4EC9B0; color: #4EC9B0; }");
    connect(copyBtn, &QPushButton::clicked, this, [this]() {
        QApplication::clipboard()->setText(m_output->toPlainText());
    });
    btnRow->addWidget(copyBtn);
    root->addWidget(btnRowHost);

    // Enter in URL / click Send → fire request
    connect(m_sendBtn, &QPushButton::clicked, this, &RestClient::sendFromUi);
    connect(m_urlInput, &QLineEdit::returnPressed, this, &RestClient::sendFromUi);

    m_nam = new QNetworkAccessManager(this);
}

void RestClient::sendFromUi() {
    QString method = m_methodCombo->currentText();
    QString url    = m_urlInput->text().trimmed();
    if (url.isEmpty()) {
        m_statusBadge->setStyleSheet(m_statusBadge->styleSheet() +
            "color: #F44747;");
        m_statusBadge->setText("  ⚠ Enter a URL first.");
        m_urlInput->setFocus();
        return;
    }
    if (!url.startsWith("http://") && !url.startsWith("https://")) {
        url = "https://" + url;
        m_urlInput->setText(url);
    }

    // Build the parseAndSend input the same way executeRequest does
    QStringList lines;
    lines << QString("%1 %2").arg(method, url);
    const QString headers = m_headersInput->toPlainText().trimmed();
    if (!headers.isEmpty()) {
        for (const QString &h : headers.split('\n', Qt::SkipEmptyParts)) {
            if (!h.trimmed().isEmpty()) lines << h;
        }
    }
    const QString body = m_bodyInput->toPlainText();
    if (!body.trimmed().isEmpty()) {
        lines << "";  // blank line separates headers from body
        lines << body;
    }
    parseAndSend(lines.join('\n'));
}

void RestClient::executeRequest(const QString &httpText) {
    // Back-compat: selected .http block from the editor. Splits by ###
    // then executes the first non-empty block. Also pre-fills the UI
    // from the block so the user sees what's being sent.
    QStringList blocks = httpText.split("###");
    if (blocks.isEmpty()) return;
    for (const QString &block : blocks) {
        QString trimmed = block.trimmed();
        if (trimmed.isEmpty()) continue;

        // Pre-fill the builder from the first line
        QStringList lines = trimmed.split('\n');
        if (!lines.isEmpty()) {
            QString first = lines.first().trimmed();
            QStringList parts = first.split(' ', Qt::SkipEmptyParts);
            if (parts.size() >= 2) {
                m_methodCombo->setCurrentText(parts[0].toUpper());
                m_urlInput->setText(parts[1]);
            }
        }
        parseAndSend(trimmed);
        break;
    }
}

void RestClient::parseAndSend(const QString &block) {
    QStringList lines = block.split('\n');
    if (lines.isEmpty()) return;

    QString firstLine = lines[0].trimmed();
    while (firstLine.startsWith("#") || firstLine.startsWith("//")) {
        lines.removeFirst();
        if (lines.isEmpty()) return;
        firstLine = lines[0].trimmed();
    }

    QStringList parts = firstLine.split(' ');
    if (parts.size() < 2) {
        m_output->setText("Error: first line must be METHOD URL "
                          "(e.g. GET https://api.example.com)");
        return;
    }
    QString method = parts[0].toUpper();
    QString url = parts[1];

    QMap<QString, QString> headers;
    QString body;
    bool inBody = false;
    for (int i = 1; i < lines.size(); i++) {
        QString line = lines[i];
        if (line.trimmed().isEmpty() && !inBody) { inBody = true; continue; }
        if (inBody) { body += line + "\n"; }
        else {
            int colonIdx = line.indexOf(':');
            if (colonIdx > 0)
                headers[line.left(colonIdx).trimmed()] = line.mid(colonIdx + 1).trimmed();
        }
    }

    QUrl qurl(url);
    QNetworkRequest req(qurl);
    for (auto it = headers.begin(); it != headers.end(); ++it)
        req.setRawHeader(it.key().toUtf8(), it.value().toUtf8());

    m_output->clear();
    m_output->append(QString("<span style='color:#569CD6; font-weight:600;'>%1</span> "
                             "<span style='color:#4EC9B0;'>%2</span>")
                         .arg(method, url));
    m_output->append("<span style='color:#808080;'>Sending…</span>");

    m_sendBtn->setEnabled(false);
    m_statusBadge->setText("  ⏳ Sending…");
    m_statusBadge->setStyleSheet(
        "background: #252526; color: #DCDCAA; padding: 5px 12px; "
        "border-top: 1px solid #1E1E1E; border-bottom: 1px solid #1E1E1E; "
        "font-size: 11px; font-weight: 600;");

    QElapsedTimer timer;
    timer.start();

    QNetworkReply *reply = nullptr;
    QByteArray bodyBytes = body.trimmed().toUtf8();
    if (method == "GET") reply = m_nam->get(req);
    else if (method == "POST") reply = m_nam->post(req, bodyBytes);
    else if (method == "PUT") reply = m_nam->put(req, bodyBytes);
    else if (method == "DELETE") reply = m_nam->deleteResource(req);
    else if (method == "HEAD") reply = m_nam->head(req);
    else if (method == "PATCH" || method == "OPTIONS") {
        auto *buf = new QBuffer;
        buf->setData(bodyBytes);
        buf->open(QIODevice::ReadOnly);
        reply = m_nam->sendCustomRequest(req, method.toUtf8(), buf);
        buf->setParent(reply);
    } else {
        m_output->append("Unsupported method: " + method);
        m_sendBtn->setEnabled(true);
        return;
    }

    connect(reply, &QNetworkReply::finished, this, [this, reply, timer]() {
        qint64 elapsed = timer.elapsed();
        int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        QString reason = reply->attribute(QNetworkRequest::HttpReasonPhraseAttribute).toString();
        QByteArray responseBody = reply->readAll();
        qint64 bytes = responseBody.size();

        QString statusColor = (status >= 200 && status < 300) ? "#4EC9B0"
                            : (status >= 300 && status < 400) ? "#9CDCFE"
                            : (status >= 400 && status < 500) ? "#DCDCAA"
                            : (status >= 500)                 ? "#F44747"
                            :                                   "#808080";

        m_statusBadge->setStyleSheet(QString(
            "background: #252526; color: %1; padding: 5px 12px; "
            "border-top: 1px solid #1E1E1E; border-bottom: 1px solid #1E1E1E; "
            "font-size: 11px; font-weight: 600;").arg(statusColor));
        const QString sizeDisp = bytes < 1024 ? QString("%1 B").arg(bytes)
                               : bytes < 1024 * 1024 ? QString("%1 KB").arg(bytes / 1024.0, 0, 'f', 1)
                               :                       QString("%1 MB").arg(bytes / 1024.0 / 1024.0, 0, 'f', 2);
        m_statusBadge->setText(QString("  %1 %2  ·  %3 ms  ·  %4")
                                   .arg(status).arg(reason).arg(elapsed).arg(sizeDisp));

        m_output->append(QString("\n<span style='color:#808080;'>── Response Headers ──</span>"));
        for (const auto &pair : reply->rawHeaderPairs()) {
            m_output->append(QString("<span style='color:#9CDCFE;'>%1</span>: %2")
                             .arg(QString::fromUtf8(pair.first).toHtmlEscaped(),
                                  QString::fromUtf8(pair.second).toHtmlEscaped()));
        }

        m_output->append(QString("\n<span style='color:#808080;'>── Response Body ──</span>\n"));
        QJsonDocument doc = QJsonDocument::fromJson(responseBody);
        if (!doc.isNull()) {
            m_output->append(doc.toJson(QJsonDocument::Indented));
        } else {
            m_output->append(QString::fromUtf8(responseBody));
        }

        m_sendBtn->setEnabled(true);
        reply->deleteLater();
    });
}
