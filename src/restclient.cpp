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
    // Grab the theme palette ONCE — interpolated into every stylesheet
    // below. Keeps this constructor cheap (one Config lookup, no repeat
    // npPalette() calls inside each setStyleSheet).
    const NpPalette pal = npPalette();

    auto *root = new QVBoxLayout(this);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(0);

    const QString mono_family = notepatraCodeCssFamily();
    QFont mono = notepatraCodeFont();

    // ─── Header strip ──────────────────────────────────────────────
    auto *header = new QLabel("  🌐  REST Client");
    header->setFixedHeight(26);
    header->setStyleSheet(QString(
        "font-weight: 600; background: %1; color: %2; "
        "padding: 4px 12px; border-bottom: 1px solid %3; "
        "font-size: 12px; letter-spacing: 0.04em;")
        .arg(pal.chromeBg, pal.accent, pal.bg));
    root->addWidget(header);

    // ─── Request bar: method dropdown + URL + Send ─────────────────
    auto *reqBarHost = new QWidget;
    reqBarHost->setStyleSheet(QString("background: %1;").arg(pal.bg));
    auto *reqBar = new QHBoxLayout(reqBarHost);
    reqBar->setContentsMargins(12, 10, 12, 8);
    reqBar->setSpacing(8);

    m_methodCombo = new QComboBox;
    m_methodCombo->addItems({"GET", "POST", "PUT", "DELETE", "PATCH", "HEAD", "OPTIONS"});
    m_methodCombo->setCurrentText("GET");
    m_methodCombo->setFixedHeight(34);
    m_methodCombo->setFixedWidth(110);
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
    reqBar->addWidget(m_methodCombo);

    m_urlInput = new QLineEdit;
    m_urlInput->setPlaceholderText("https://api.example.com/users  (paste URL and hit Send)");
    m_urlInput->setFixedHeight(34);
    m_urlInput->setFont(mono);
    m_urlInput->setStyleSheet(QString(
        "QLineEdit { background: %1; color: %2; "
        "border: 1px solid %3; border-radius: 6px; "
        "padding: 6px 12px; font-size: 13px; "
        "selection-background-color: %4; selection-color: %5; }"
        "QLineEdit:focus { border-color: %6; }")
        .arg(pal.inputBg, pal.inputFg, pal.inputBorder,
             pal.selectionBg, pal.selectionFg, pal.inputFocus));
    reqBar->addWidget(m_urlInput, 1);

    m_sendBtn = new QPushButton("Send");
    m_sendBtn->setFixedHeight(34);
    m_sendBtn->setFixedWidth(96);
    m_sendBtn->setCursor(Qt::PointingHandCursor);
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
    reqBar->addWidget(m_sendBtn);
    root->addWidget(reqBarHost);

    // ─── Headers / Body tabs ───────────────────────────────────────
    m_reqTabs = new QTabWidget;
    m_reqTabs->setFixedHeight(170);
    m_reqTabs->setStyleSheet(QString(
        "QTabWidget::pane { background: %1; border: none; border-top: 1px solid %2; }"
        "QTabBar { background: %1; }"
        "QTabBar::tab { background: transparent; color: %3; "
        "padding: 6px 16px; border: none; font-size: 12px; margin-right: 2px; }"
        "QTabBar::tab:selected { color: %4; border-bottom: 2px solid %5; }"
        "QTabBar::tab:hover:!selected { color: %6; }")
        .arg(pal.bg, pal.cardBg, pal.textMuted,
             pal.accent, pal.accent, pal.text));

    m_headersInput = new QPlainTextEdit;
    m_headersInput->setFont(mono);
    m_headersInput->setPlaceholderText(
        "Accept: application/json\n"
        "Authorization: Bearer <token>\n"
        "Content-Type: application/json");
    m_headersInput->setStyleSheet(QString(
        "QPlainTextEdit { background: %1; color: %2; border: none; "
        "padding: 10px; font-size: 12px; "
        "selection-background-color: %3; selection-color: %4; }")
        .arg(pal.bg, pal.text, pal.selectionBg, pal.selectionFg));
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
    m_statusBadge->setStyleSheet(QString(
        "background: %1; color: %2; padding: 5px 12px; "
        "border-top: 1px solid %3; border-bottom: 1px solid %3; "
        "font-size: 11px; font-weight: 600; letter-spacing: 0.05em;")
        .arg(pal.chromeBg, pal.textMuted, pal.bg));
    m_statusBadge->setText("  Response will appear here.");
    root->addWidget(m_statusBadge);

    m_output = new QTextEdit;
    m_output->setReadOnly(true);
    m_output->setFont(mono);
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
    m_output->setPlaceholderText(
        "Set a method + URL above, optionally fill Headers and Body tabs, "
        "then press Send. Pretty-printed JSON and full response headers "
        "will show here.");
    root->addWidget(m_output, 1);

    // ─── Bottom action row ─────────────────────────────────────────
    auto *btnRowHost = new QWidget;
    btnRowHost->setStyleSheet(QString("background: %1; border-top: 1px solid %2;")
        .arg(pal.chromeBg, pal.bg));
    auto *btnRow = new QHBoxLayout(btnRowHost);
    btnRow->setContentsMargins(8, 4, 8, 4);
    btnRow->addStretch();
    auto *copyBtn = new QPushButton("Copy Response");
    copyBtn->setFixedHeight(26);
    copyBtn->setStyleSheet(QString(
        "QPushButton { background: transparent; color: %1; "
        "border: 1px solid %2; border-radius: 4px; padding: 4px 14px; "
        "font-size: 11px; }"
        "QPushButton:hover { background: %3; border-color: %4; color: %4; }")
        .arg(pal.btnFg, pal.btnBorder, pal.btnHover, pal.accent));
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

    // Capture the palette by value into the finished lambda so the
    // response-time restyle doesn't need another npPalette() call.
    connect(reply, &QNetworkReply::finished, this, [this, reply, timer, pal]() {
        qint64 elapsed = timer.elapsed();
        int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        QString reason = reply->attribute(QNetworkRequest::HttpReasonPhraseAttribute).toString();
        QByteArray responseBody = reply->readAll();
        qint64 bytes = responseBody.size();

        // Semantic status colors — 2xx success, 3xx info, 4xx warning,
        // 5xx error. Pulls from the palette cached in the request slot
        // so Light/Dark themes each get theme-appropriate shades.
        QString statusColor = (status >= 200 && status < 300) ? pal.successFg
                            : (status >= 300 && status < 400) ? pal.accent
                            : (status >= 400 && status < 500) ? pal.warningFg
                            : (status >= 500)                 ? pal.errorFg
                            :                                   pal.textMuted;

        m_statusBadge->setStyleSheet(QString(
            "background: %1; color: %2; padding: 5px 12px; "
            "border-top: 1px solid %3; border-bottom: 1px solid %3; "
            "font-size: 11px; font-weight: 600;")
            .arg(pal.chromeBg, statusColor, pal.bg));
        const QString sizeDisp = bytes < 1024 ? QString("%1 B").arg(bytes)
                               : bytes < 1024 * 1024 ? QString("%1 KB").arg(bytes / 1024.0, 0, 'f', 1)
                               :                       QString("%1 MB").arg(bytes / 1024.0 / 1024.0, 0, 'f', 2);
        m_statusBadge->setText(QString("  %1 %2  ·  %3 ms  ·  %4")
                                   .arg(status).arg(reason).arg(elapsed).arg(sizeDisp));

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
