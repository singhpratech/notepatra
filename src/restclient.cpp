// SPDX-License-Identifier: GPL-3.0-or-later

#include "restclient.h"
#include "fonts.h"
#include "theme_detect.h"
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
#include <QTimer>
#include <QSslError>
#include <QTextCursor>

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
//
// All colors come from npPalette() so the panel honors Light/Dark theme
// without hardcoding dark-mode swatches (which used to leave light-mode
// users staring at a black block with invisible text). The constructor
// grabs the palette ONCE and feeds it into every stylesheet. The
// request slots (sendFromUi / parseAndSend) look it up once per user
// action when re-styling the status badge — still cheap.
// ═══════════════════════════════════════════════════════════════════════

RestClient::RestClient(QWidget *parent) : QWidget(parent) {
    auto *root = new QVBoxLayout(this);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(0);

    const QString mono_family = notepatraCodeCssFamily();
    QFont mono = notepatraCodeFont();

    // ─── Header strip ──────────────────────────────────────────────
    m_header = new QLabel("  🌐  REST Client");
    m_header->setFixedHeight(26);
    root->addWidget(m_header);

    // ─── Request bar: method dropdown + URL + Send ─────────────────
    m_reqBarHost = new QWidget;
    auto *reqBar = new QHBoxLayout(m_reqBarHost);
    reqBar->setContentsMargins(12, 10, 12, 8);
    reqBar->setSpacing(8);

    m_methodCombo = new QComboBox;
    m_methodCombo->addItems({"GET", "POST", "PUT", "DELETE", "PATCH", "HEAD", "OPTIONS"});
    m_methodCombo->setCurrentText("GET");
    m_methodCombo->setFixedHeight(34);
    m_methodCombo->setFixedWidth(110);
    reqBar->addWidget(m_methodCombo);

    m_urlInput = new QLineEdit;
    m_urlInput->setPlaceholderText("https://api.example.com/users  (paste URL and hit Send)");
    m_urlInput->setFixedHeight(34);
    m_urlInput->setFont(mono);
    reqBar->addWidget(m_urlInput, 1);

    m_sendBtn = new QPushButton("Send");
    m_sendBtn->setFixedHeight(34);
    m_sendBtn->setFixedWidth(96);
    m_sendBtn->setCursor(Qt::PointingHandCursor);
    reqBar->addWidget(m_sendBtn);
    root->addWidget(m_reqBarHost);

    // ─── Headers / Body tabs ───────────────────────────────────────
    m_reqTabs = new QTabWidget;
    m_reqTabs->setFixedHeight(170);

    m_headersInput = new QPlainTextEdit;
    m_headersInput->setFont(mono);
    m_headersInput->setPlaceholderText(
        "Accept: application/json\n"
        "Authorization: Bearer <token>\n"
        "Content-Type: application/json");
    m_reqTabs->addTab(m_headersInput, "Headers");

    m_bodyInput = new QPlainTextEdit;
    m_bodyInput->setFont(mono);
    m_bodyInput->setPlaceholderText(
        "{\n"
        "  \"name\": \"Alice\",\n"
        "  \"role\": \"admin\"\n"
        "}\n\n"
        "For POST/PUT/PATCH requests. Leave empty for GET/DELETE.");
    m_reqTabs->addTab(m_bodyInput, "Body");

    root->addWidget(m_reqTabs);

    // ─── Response status badge + body ──────────────────────────────
    m_statusBadge = new QLabel;
    m_statusBadge->setFixedHeight(28);
    m_statusBadge->setText("  Response will appear here.");
    root->addWidget(m_statusBadge);

    m_output = new QTextEdit;
    m_output->setReadOnly(true);
    m_output->setFont(mono);
    m_output->setPlaceholderText(
        "Set a method + URL above, optionally fill Headers and Body tabs, "
        "then press Send. Pretty-printed JSON and full response headers "
        "will show here.");
    root->addWidget(m_output, 1);

    // ─── Bottom action row ─────────────────────────────────────────
    m_btnRowHost = new QWidget;
    auto *btnRow = new QHBoxLayout(m_btnRowHost);
    btnRow->setContentsMargins(8, 4, 8, 4);
    btnRow->addStretch();
    m_copyBtn = new QPushButton("Copy Response");
    m_copyBtn->setFixedHeight(26);
    connect(m_copyBtn, &QPushButton::clicked, this, [this]() {
        QApplication::clipboard()->setText(m_output->toPlainText());
    });
    btnRow->addWidget(m_copyBtn);
    root->addWidget(m_btnRowHost);

    applyPalette();

    // Enter in URL / click Send → fire request
    connect(m_sendBtn, &QPushButton::clicked, this, &RestClient::sendFromUi);
    connect(m_urlInput, &QLineEdit::returnPressed, this, &RestClient::sendFromUi);

    m_nam = new QNetworkAccessManager(this);
}

void RestClient::applyPalette() {
    // Grab the palette ONCE — interpolated into every stylesheet below and
    // cached into m_pal* so sendFromUi()/parseAndSend() can recolour the
    // status badge without another Config lookup per request.
    const NpPalette pal = npPalette();
    m_palBg        = pal.bg;
    m_palChromeBg  = pal.chromeBg;
    m_palText      = pal.text;
    m_palTextMuted = pal.textMuted;
    m_palAccent    = pal.accent;
    m_palBorder    = pal.border;
    m_palSuccessFg = pal.successFg;
    m_palWarningFg = pal.warningFg;
    m_palErrorFg   = pal.errorFg;

    if (m_header) {
        m_header->setStyleSheet(QString(
            "font-weight: 600; background: %1; color: %2; "
            "padding: 4px 12px; border-bottom: 1px solid %3; "
            "font-size: 12px; letter-spacing: 0.04em;")
            .arg(pal.chromeBg, pal.accent, pal.bg));
    }
    if (m_reqBarHost) {
        m_reqBarHost->setStyleSheet(QString("background: %1;").arg(pal.bg));
    }
    if (m_methodCombo) {
        m_methodCombo->setStyleSheet(QString(
            "QComboBox { background: %1; color: %2; "
            "border: 1px solid %3; border-radius: 6px; "
            "padding: 4px 10px; font-weight: 700; font-size: 13px; }"
            "QComboBox:hover { border-color: %4; }"
            "QComboBox::drop-down { border: none; width: 20px; }"
            "QComboBox QAbstractItemView { background: %1; color: %5; "
            "selection-background-color: %6; border: 1px solid %3; }")
            .arg(pal.inputBg, pal.accent, pal.inputBorder,
                 pal.inputFocus, pal.inputFg, pal.selectionBg));
    }
    if (m_urlInput) {
        m_urlInput->setStyleSheet(QString(
            "QLineEdit { background: %1; color: %2; "
            "border: 1px solid %3; border-radius: 6px; "
            "padding: 6px 12px; font-size: 13px; "
            "selection-background-color: %4; selection-color: %5; }"
            "QLineEdit:focus { border-color: %6; }")
            .arg(pal.inputBg, pal.inputFg, pal.inputBorder,
                 pal.selectionBg, pal.selectionFg, pal.inputFocus));
    }
    if (m_sendBtn) {
        // Primary action — uses accent as background for strong call-to-action.
        m_sendBtn->setStyleSheet(QString(
            "QPushButton { background: %1; color: %2; "
            "border: none; border-radius: 6px; "
            "font-size: 13px; font-weight: 600; }"
            "QPushButton:hover { background: %3; }"
            "QPushButton:pressed { background: %1; }"
            "QPushButton:disabled { background: %4; color: %5; }")
            .arg(pal.accent, pal.selectionFg, pal.inputFocus,
                 pal.btnBorder, pal.textMuted));
    }
    if (m_reqTabs) {
        m_reqTabs->setStyleSheet(QString(
            "QTabWidget::pane { background: %1; border: none; border-top: 1px solid %2; }"
            "QTabBar { background: %1; }"
            "QTabBar::tab { background: transparent; color: %3; "
            "padding: 6px 16px; border: none; font-size: 12px; margin-right: 2px; }"
            "QTabBar::tab:selected { color: %4; border-bottom: 2px solid %5; }"
            "QTabBar::tab:hover:!selected { color: %6; }")
            .arg(pal.bg, pal.cardBg, pal.textMuted,
                 pal.accent, pal.accent, pal.text));
    }
    if (m_headersInput) {
        m_headersInput->setStyleSheet(QString(
            "QPlainTextEdit { background: %1; color: %2; border: none; "
            "padding: 10px; font-size: 12px; "
            "selection-background-color: %3; selection-color: %4; }")
            .arg(pal.bg, pal.text, pal.selectionBg, pal.selectionFg));
    }
    if (m_bodyInput && m_headersInput) {
        m_bodyInput->setStyleSheet(m_headersInput->styleSheet());
    }
    if (m_statusBadge) {
        m_statusBadge->setStyleSheet(QString(
            "background: %1; color: %2; padding: 5px 12px; "
            "border-top: 1px solid %3; border-bottom: 1px solid %3; "
            "font-size: 11px; font-weight: 600; letter-spacing: 0.05em;")
            .arg(pal.chromeBg, pal.textMuted, pal.bg));
    }
    if (m_output) {
        m_output->setStyleSheet(QString(
            "QTextEdit { background: %1; color: %2; "
            "border: none; padding: 12px; "
            "selection-background-color: %3; selection-color: %4; }"
            "QScrollBar:vertical { background: %1; width: 10px; }"
            "QScrollBar::handle:vertical { background: %5; "
            "border-radius: 5px; min-height: 40px; margin: 2px; }"
            "QScrollBar::handle:vertical:hover { background: %6; }"
            "QScrollBar::add-line:vertical, QScrollBar::sub-line:vertical { height: 0; }")
            .arg(pal.bg, pal.text, pal.selectionBg, pal.selectionFg,
                 pal.border, pal.textMuted));
    }
    if (m_btnRowHost) {
        m_btnRowHost->setStyleSheet(QString("background: %1; border-top: 1px solid %2;")
            .arg(pal.chromeBg, pal.bg));
    }
    if (m_copyBtn) {
        m_copyBtn->setStyleSheet(QString(
            "QPushButton { background: transparent; color: %1; "
            "border: 1px solid %2; border-radius: 4px; padding: 4px 14px; "
            "font-size: 11px; }"
            "QPushButton:hover { background: %3; border-color: %4; color: %4; }")
            .arg(pal.btnFg, pal.btnBorder, pal.btnHover, pal.accent));
    }
}

void RestClient::onThemeChanged() {
    applyPalette();
    update();
}

void RestClient::sendFromUi() {
    // One palette lookup per click — cheap, avoids repeating per stylesheet.
    const NpPalette pal = npPalette();

    QString method = m_methodCombo->currentText();
    QString url    = m_urlInput->text().trimmed();
    if (url.isEmpty()) {
        m_statusBadge->setStyleSheet(QString(
            "background: %1; color: %2; padding: 5px 12px; "
            "border-top: 1px solid %3; border-bottom: 1px solid %3; "
            "font-size: 11px; font-weight: 600;")
            .arg(pal.chromeBg, pal.errorFg, pal.bg));
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
    // One palette lookup per request — all the "Sending…" / response
    // styles below reuse it instead of calling npPalette() repeatedly.
    const NpPalette pal = npPalette();

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
    // Inline HTML colors for the response stream — use accent for the
    // verb and text for the URL so both themes stay readable.
    m_output->append(QString("<span style='color:%1; font-weight:600;'>%2</span> "
                             "<span style='color:%3;'>%4</span>")
                         .arg(pal.accent, method, pal.text, url));
    m_output->append(QString("<span style='color:%1;'>Sending…</span>").arg(pal.textMuted));

    m_sendBtn->setEnabled(false);
    m_statusBadge->setText("  ⏳ Sending…");
    m_statusBadge->setStyleSheet(QString(
        "background: %1; color: %2; padding: 5px 12px; "
        "border-top: 1px solid %3; border-bottom: 1px solid %3; "
        "font-size: 11px; font-weight: 600;")
        .arg(pal.chromeBg, pal.warningFg, pal.bg));

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

    // v0.1.48 — 30-second transfer timeout. Without this a hanging server
    // would keep the Send button disabled and the user stuck staring at
    // "Sending…". QNetworkRequest::setTransferTimeout was added in 5.15.
    if (reply) {
        reply->setReadBufferSize(0);  // unlimited; we cap response size in lambda below
    }
    QTimer *timeoutGuard = new QTimer(this);
    timeoutGuard->setSingleShot(true);
    timeoutGuard->setInterval(30 * 1000);
    connect(timeoutGuard, &QTimer::timeout, this, [reply]() {
        if (reply && reply->isRunning()) reply->abort();
    });
    timeoutGuard->start();

    // v0.1.48 — explicit error handler. Pre-v0.1.48 a DNS / SSL / refused
    // connection silently produced an empty response with status 0, so
    // users couldn't tell if the server returned 200 OK with empty body
    // or if the request never reached anyone. Now we emit a clear error
    // line into the response area BEFORE finished() fires.
    connect(reply, &QNetworkReply::errorOccurred, this,
            [this, reply, pal, timeoutGuard](QNetworkReply::NetworkError err) {
        if (err == QNetworkReply::NoError) return;
        timeoutGuard->stop();
        const QString hint = (err == QNetworkReply::OperationCanceledError)
            ? QStringLiteral("Request aborted (timeout after 30s, or you clicked Send again).")
            : (err == QNetworkReply::HostNotFoundError)
                ? QStringLiteral("DNS resolution failed — check the URL or your network.")
                : (err == QNetworkReply::ConnectionRefusedError)
                    ? QStringLiteral("Connection refused — server not listening on this port.")
                    : (err == QNetworkReply::SslHandshakeFailedError)
                        ? QStringLiteral("SSL handshake failed — invalid / expired / self-signed cert.")
                        : (err == QNetworkReply::TimeoutError)
                            ? QStringLiteral("Request timed out before the server responded.")
                            : QStringLiteral("Network error.");
        m_output->append(QString("<span style='color:%1; font-weight:600;'>✗ %2</span>")
                             .arg(pal.errorFg, reply->errorString().toHtmlEscaped()));
        m_output->append(QString("<span style='color:%1;'>%2</span>")
                             .arg(pal.textMuted, hint.toHtmlEscaped()));
    });

    // SSL errors — show but don't block (Qt blocks by default on cert
    // errors; we're a developer tool so we let the user see what went
    // wrong rather than auto-trusting). The error path above already
    // surfaces SslHandshakeFailedError; this lambda just adds detail.
    connect(reply, &QNetworkReply::sslErrors, this,
            [this, pal](const QList<QSslError> &errs) {
        for (const QSslError &e : errs) {
            m_output->append(QString("<span style='color:%1;'>SSL: %2</span>")
                             .arg(pal.warningFg, e.errorString().toHtmlEscaped()));
        }
    });

    // Capture the palette by value into the finished lambda so the
    // response-time restyle doesn't need another npPalette() call.
    connect(reply, &QNetworkReply::finished, this, [this, reply, timer, pal, timeoutGuard]() {
        timeoutGuard->stop();
        timeoutGuard->deleteLater();
        qint64 elapsed = timer.elapsed();
        int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        QString reason = reply->attribute(QNetworkRequest::HttpReasonPhraseAttribute).toString();
        QByteArray responseBody = reply->readAll();
        qint64 bytes = responseBody.size();

        // Semantic status colors — 2xx success, 3xx info, 4xx warning,
        // 5xx error. Pulls from the palette cached in the request slot
        // so Light/Dark themes each get theme-appropriate shades. Errored
        // requests have status==0; show that explicitly so the user
        // doesn't read it as an HTTP 0 response.
        QString statusColor;
        QString statusLabel;
        if (status == 0 && reply->error() != QNetworkReply::NoError) {
            statusColor = pal.errorFg;
            statusLabel = QStringLiteral("ERROR");
        } else {
            statusColor = (status >= 200 && status < 300) ? pal.successFg
                        : (status >= 300 && status < 400) ? pal.accent
                        : (status >= 400 && status < 500) ? pal.warningFg
                        : (status >= 500)                 ? pal.errorFg
                        :                                   pal.textMuted;
            statusLabel = QString::number(status) + " " + reason;
        }

        m_statusBadge->setStyleSheet(QString(
            "background: %1; color: %2; padding: 5px 12px; "
            "border-top: 1px solid %3; border-bottom: 1px solid %3; "
            "font-size: 11px; font-weight: 600;")
            .arg(pal.chromeBg, statusColor, pal.bg));
        const QString sizeDisp = bytes < 1024 ? QString("%1 B").arg(bytes)
                               : bytes < 1024 * 1024 ? QString("%1 KB").arg(bytes / 1024.0, 0, 'f', 1)
                               :                       QString("%1 MB").arg(bytes / 1024.0 / 1024.0, 0, 'f', 2);
        m_statusBadge->setText(QString("  %1  ·  %2 ms  ·  %3")
                                   .arg(statusLabel).arg(elapsed).arg(sizeDisp));

        m_output->append(QString("\n<span style='color:%1;'>── Response Headers ──</span>")
                            .arg(pal.textMuted));
        for (const auto &pair : reply->rawHeaderPairs()) {
            m_output->append(QString("<span style='color:%1;'>%2</span>: %3")
                             .arg(pal.accent,
                                  QString::fromUtf8(pair.first).toHtmlEscaped(),
                                  QString::fromUtf8(pair.second).toHtmlEscaped()));
        }

        m_output->append(QString("\n<span style='color:%1;'>── Response Body ──</span>\n")
                            .arg(pal.textMuted));
        // v0.1.48 — pretty-print JSON, otherwise HTML-escape so a server
        // returning <html>...<script>alert(1)</script>... doesn't get
        // rendered as actual markup in our QTextEdit. Pre-fix any text/*
        // body went through QTextEdit::append() which interprets HTML.
        QJsonDocument doc = QJsonDocument::fromJson(responseBody);
        if (!doc.isNull()) {
            // Plain-text insertion of pretty JSON via setPlainText would
            // wipe headers; use insertPlainText at end so headers remain.
            QTextCursor cur = m_output->textCursor();
            cur.movePosition(QTextCursor::End);
            cur.insertText(QString::fromUtf8(doc.toJson(QJsonDocument::Indented)));
            m_output->setTextCursor(cur);
        } else {
            // Non-JSON: HTML-escape and wrap in <pre> so whitespace is
            // preserved and any tags in the body are shown as text.
            const QString safe = QString::fromUtf8(responseBody).toHtmlEscaped();
            m_output->append(QString("<pre style='margin:0;'>%1</pre>").arg(safe));
        }

        m_sendBtn->setEnabled(true);
        reply->deleteLater();
    });
}
