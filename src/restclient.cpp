// SPDX-License-Identifier: GPL-3.0-or-later

#include "restclient.h"
#include "config.h"
#include "curl_convert.h"
#include "fonts.h"
#include "rest_history.h"
#include "shortcut_hints.h"
#include "theme_detect.h"

#include <QApplication>
#include <QClipboard>
#include <QDateTime>
#include <QDir>
#include <QDialog>
#include <QDialogButtonBox>
#include <QFileDialog>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QJsonArray>
#include <QJsonDocument>
#include <QLocale>
#include <QMimeDatabase>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QRegularExpression>
#include <QSaveFile>
#include <QSplitter>
#include <QSslError>
#include <QStackedWidget>
#include <QStyle>
#include <QTabBar>
#include <QTextCodec>
#include <QTreeWidget>
#include <QUrl>
#include <QVBoxLayout>

// Layout (top to bottom):
//   REST Client                                   Environment: [staging v]
//   [POST v] [{{baseUrl}}/users                    ] [Send] [Cancel]
//   [Headers] [Body]                       [Import cURL] [Copy as cURL]
//   201 Created · 43 ms · 1.2 KB                       (status badge)
//   note line (redirects, binary, truncation, import notes, refusals)
//   [Body] [Headers]                   [Open in tab] [Save…] [Copy]
//   response text
//   History (double-click loads a request; Send runs it)
//
// Every colour comes from npPalette() in applyPalette(), re-run on theme change.

namespace {

QString sizeText(qint64 bytes) {
    if (bytes < 1024) return QStringLiteral("%1 B").arg(bytes);
    if (bytes < 1024 * 1024) return QStringLiteral("%1 KB").arg(bytes / 1024.0, 0, 'f', 1);
    return QStringLiteral("%1 MB").arg(bytes / 1024.0 / 1024.0, 0, 'f', 2);
}

QString mainType(const QString &contentType) {
    return contentType.section(QLatin1Char(';'), 0, 0).trimmed().toLower();
}

QString decodeBody(const QByteArray &bytes, const QString &contentType) {
    static const QRegularExpression cs(QStringLiteral("charset\\s*=\\s*\"?([A-Za-z0-9._:-]+)"),
                                       QRegularExpression::CaseInsensitiveOption);
    const QRegularExpressionMatch m = cs.match(contentType);
    if (m.hasMatch()) {
        if (QTextCodec *codec = QTextCodec::codecForName(m.captured(1).toLatin1()))
            return codec->toUnicode(bytes);
    }
    return QString::fromUtf8(bytes);
}

const QStringList &standardMethods() {
    static const QStringList m = {"GET", "POST", "PUT", "DELETE", "PATCH", "HEAD", "OPTIONS"};
    return m;
}

// QTabBar sizes each tab with its widget font, while a font-size in a
// QTabBar::tab rule reaches only the painter. The label size therefore goes
// on the bar itself, so one font both measures and paints. (macOS: the 10pt
// app font is 10px at 72 dpi, and 12px labels were clipped on both sides.)
void setTabLabelFont(QTabBar *bar) {
    QFont f = bar->font();
    f.setPixelSize(12);
    bar->setFont(f);
}

}  // namespace

RestClient::RestClient(QWidget *parent) : QWidget(parent) {
    auto *root = new QVBoxLayout(this);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(0);

    QFont mono = notepatraCodeFont();

    // ─── Header: title + environment picker ───────────────────────────
    m_headerHost = new QWidget;
    auto *head = new QHBoxLayout(m_headerHost);
    head->setContentsMargins(12, 2, 12, 2);
    head->setSpacing(6);
    m_header = new QLabel(tr("REST Client"));
    head->addWidget(m_header);
    head->addStretch();
    m_envLabel = new QLabel(tr("Environment:"));
    head->addWidget(m_envLabel);
    m_envCombo = new QComboBox;
    m_envCombo->setMinimumWidth(150);
    m_envCombo->addItem(tr("No environment"), QString());
    m_envCombo->setToolTip(tr("Environments come from http-client.env.json, and "
                              "http-client.private.env.json for secrets, next to the .http file."));
    head->addWidget(m_envCombo);
    m_headerHost->setFixedHeight(32);
    root->addWidget(m_headerHost);

    // ─── Request bar: method + URL + Send + Cancel ────────────────────
    m_reqBarHost = new QWidget;
    auto *reqBar = new QHBoxLayout(m_reqBarHost);
    reqBar->setContentsMargins(12, 10, 12, 8);
    reqBar->setSpacing(8);

    m_methodCombo = new QComboBox;
    m_methodCombo->addItems(standardMethods());
    m_methodCombo->setCurrentText(QStringLiteral("GET"));
    m_methodCombo->setFixedHeight(34);
    m_methodCombo->setFixedWidth(110);
    reqBar->addWidget(m_methodCombo);

    m_urlInput = new QLineEdit;
    m_urlInput->setPlaceholderText(tr("https://api.example.com/users  or  {{baseUrl}}/users"));
    m_urlInput->setFixedHeight(34);
    m_urlInput->setFont(mono);
    reqBar->addWidget(m_urlInput, 1);

    m_sendBtn = new QPushButton(tr("Send"));
    m_sendBtn->setFixedHeight(34);
    m_sendBtn->setMinimumWidth(84);
    m_sendBtn->setCursor(Qt::PointingHandCursor);
    reqBar->addWidget(m_sendBtn);

    m_cancelBtn = new QPushButton(tr("Cancel"));
    m_cancelBtn->setFixedHeight(34);
    m_cancelBtn->setMinimumWidth(84);
    m_cancelBtn->setEnabled(false);
    m_cancelBtn->setToolTip(tr("Stop the request that is running"));
    reqBar->addWidget(m_cancelBtn);
    root->addWidget(m_reqBarHost);

    // ─── Headers / Body tabs with the cURL buttons in the corner ──────
    m_reqTabs = new QTabWidget;
    m_reqTabs->setFixedHeight(170);
    setTabLabelFont(m_reqTabs->tabBar());

    m_headersInput = new QPlainTextEdit;
    m_headersInput->setFont(mono);
    m_headersInput->setPlaceholderText(QStringLiteral("Accept: application/json\n"
                                                      "Authorization: Bearer {{token}}\n"
                                                      "Content-Type: application/json"));
    m_reqTabs->addTab(m_headersInput, tr("Headers"));

    m_bodyInput = new QPlainTextEdit;
    m_bodyInput->setFont(mono);
    m_bodyInput->setPlaceholderText(tr("{\n  \"name\": \"Asha\",\n  \"role\": \"admin\"\n}\n\n"
                                       "For POST/PUT/PATCH requests. {{variables}} work here too."));
    m_reqTabs->addTab(m_bodyInput, tr("Body"));

    auto *corner = new QWidget;
    auto *cornerLay = new QHBoxLayout(corner);
    cornerLay->setContentsMargins(0, 2, 12, 2);
    cornerLay->setSpacing(6);
    m_importBtn = new QPushButton(tr("Import cURL"));
    m_importBtn->setToolTip(tr("Fill the request from a pasted curl command"));
    m_copyCurlBtn = new QPushButton(tr("Copy as cURL"));
    m_copyCurlBtn->setToolTip(tr("Copy this request as a curl command. {{variables}} stay "
                                 "unexpanded so tokens are not put on the clipboard."));
    cornerLay->addWidget(m_importBtn);
    cornerLay->addWidget(m_copyCurlBtn);
    m_reqTabs->setCornerWidget(corner, Qt::TopRightCorner);
    root->addWidget(m_reqTabs);

    // ─── Status badge + note ──────────────────────────────────────────
    m_statusBadge = new QLabel;
    m_statusBadge->setFixedHeight(28);
    m_statusBadge->setText(tr("Response will appear here."));
    root->addWidget(m_statusBadge);

    m_note = new QLabel;
    m_note->setWordWrap(true);
    m_note->setTextInteractionFlags(Qt::TextSelectableByMouse);
    m_note->hide();
    root->addWidget(m_note);

    // ─── Response bar: Body | Headers + actions ───────────────────────
    m_respBarHost = new QWidget;
    auto *respBar = new QHBoxLayout(m_respBarHost);
    respBar->setContentsMargins(4, 0, 12, 0);
    respBar->setSpacing(6);
    m_respTabs = new QTabBar;
    m_respTabs->addTab(tr("Body"));
    m_respTabs->addTab(tr("Headers"));
    m_respTabs->setDrawBase(false);
    m_respTabs->setExpanding(false);
    setTabLabelFont(m_respTabs);
    respBar->addWidget(m_respTabs);
    respBar->addStretch();
    m_openTabBtn = new QPushButton(tr("Open in tab"));
    m_openTabBtn->setToolTip(tr("Open the whole response body in a new editor tab"));
    m_saveBtn = new QPushButton(tr("Save…"));
    m_saveBtn->setToolTip(tr("Save the raw response bytes to a file"));
    m_copyBtn = new QPushButton(tr("Copy"));
    m_copyBtn->setToolTip(tr("Copy the text shown below"));
    // Open in tab stays text-only, like Copy: SP_FileIcon is the platform's
    // generic-document picture (a white page on macOS), which vanished on
    // the light bar when the button was disabled.
    m_saveBtn->setIcon(style()->standardIcon(QStyle::SP_DialogSaveButton));
    for (QPushButton *b : {m_openTabBtn, m_saveBtn, m_copyBtn}) {
        b->setEnabled(false);
        respBar->addWidget(b);
    }
    root->addWidget(m_respBarHost);

    // ─── Response view above, history below ──────────────────────────
    m_split = new QSplitter(Qt::Vertical);
    m_split->setChildrenCollapsible(false);

    m_respStack = new QStackedWidget;
    m_respBody = new QPlainTextEdit;
    m_respBody->setReadOnly(true);
    m_respBody->setFont(mono);
    m_respBody->setPlaceholderText(tr("Set a method and URL, or press Send Request (%1) "
                                      "in a .http file. The response body shows here; JSON is "
                                      "pretty-printed.")
                                       .arg(Shortcuts::nativeText(Shortcuts::sendHttpRequest())));
    m_respHeaders = new QPlainTextEdit;
    m_respHeaders->setReadOnly(true);
    m_respHeaders->setFont(mono);
    m_respHeaders->setPlaceholderText(tr("Status line and response headers."));
    m_respStack->addWidget(m_respBody);
    m_respStack->addWidget(m_respHeaders);
    m_split->addWidget(m_respStack);

    m_historyHost = new QWidget;
    auto *hist = new QVBoxLayout(m_historyHost);
    hist->setContentsMargins(0, 0, 0, 0);
    hist->setSpacing(0);
    auto *histHead = new QWidget;
    auto *histHeadLay = new QHBoxLayout(histHead);
    histHeadLay->setContentsMargins(12, 4, 12, 4);
    histHeadLay->setSpacing(8);
    m_historyTitle = new QLabel(tr("History"));
    m_historyHint = new QLabel(tr("Double-click to load a request, then press Send. Secret header "
                                  "values are saved as <redacted>; fill them in before re-sending."));
    m_historyHint->setTextFormat(Qt::PlainText);
    m_historyHint->setWordWrap(true);
    m_clearHistoryBtn = new QPushButton(tr("Clear history"));
    histHeadLay->addWidget(m_historyTitle);
    histHeadLay->addWidget(m_historyHint, 1);
    histHeadLay->addWidget(m_clearHistoryBtn);
    hist->addWidget(histHead);
    m_history = new QTreeWidget;
    m_history->setRootIsDecorated(false);
    m_history->setUniformRowHeights(true);
    m_history->setSelectionMode(QAbstractItemView::SingleSelection);
    m_history->setHeaderLabels({tr("Time"), tr("Method"), tr("URL"), tr("Status"), tr("ms")});
    m_history->header()->setStretchLastSection(false);
    m_history->header()->setSectionResizeMode(2, QHeaderView::Stretch);
    m_history->setColumnWidth(0, 80);
    m_history->setColumnWidth(1, 70);
    m_history->setColumnWidth(3, 90);
    m_history->setColumnWidth(4, 60);
    m_history->setMinimumHeight(96);
    hist->addWidget(m_history, 1);
    m_split->addWidget(m_historyHost);
    m_split->setStretchFactor(0, 3);
    m_split->setStretchFactor(1, 1);
    m_split->setSizes({320, 150});
    root->addWidget(m_split, 1);

    applyPalette();

    m_nam = new QNetworkAccessManager(this);
    m_timeout.setSingleShot(true);
    connect(&m_timeout, &QTimer::timeout, this, [this]() {
        if (!m_reply) return;
        m_timedOut = true;
        QMetaObject::invokeMethod(m_reply.data(), "abort", Qt::QueuedConnection);
    });

    connect(m_sendBtn, &QPushButton::clicked, this, &RestClient::send);
    connect(m_urlInput, &QLineEdit::returnPressed, this, &RestClient::send);
    // A hand-edited URL is no longer the named .http request it was loaded from.
    connect(m_urlInput, &QLineEdit::textEdited, this, [this]() { m_ctx.requestName.clear(); });
    connect(m_cancelBtn, &QPushButton::clicked, this, &RestClient::cancelRequest);
    connect(m_importBtn, &QPushButton::clicked, this, &RestClient::openImportDialog);
    connect(m_copyCurlBtn, &QPushButton::clicked, this, &RestClient::copyAsCurl);
    connect(m_respTabs, &QTabBar::currentChanged, this, &RestClient::showResponseView);
    connect(m_openTabBtn, &QPushButton::clicked, this, &RestClient::openResponseInTab);
    connect(m_saveBtn, &QPushButton::clicked, this, &RestClient::saveResponse);
    connect(m_copyBtn, &QPushButton::clicked, this, &RestClient::copyResponse);
    connect(m_clearHistoryBtn, &QPushButton::clicked, this, &RestClient::clearHistory);
    connect(m_history, &QTreeWidget::itemActivated, this, [this](QTreeWidgetItem *it, int) {
        loadHistoryEntry(m_history->indexOfTopLevelItem(it));
    });
    connect(m_envCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
            &RestClient::onEnvironmentChosen);

    reloadHistory();
}

RestClient::~RestClient() {
    if (m_reply) {
        m_reply->disconnect(this);
        m_reply->abort();
    }
}

QString RestClient::historyPath() const {
    if (!m_historyPathOverride.isEmpty()) return m_historyPathOverride;
    return Config::appConfigDir() + QStringLiteral("/rest-client.json");
}

// ─── Theme ──────────────────────────────────────────────────────────────

void RestClient::applyPalette() {
    const NpPalette pal = npPalette();

    const QString menuQss = QString(
        "QMenu { background: %1; color: %2; border: 1px solid %3; }"
        "QMenu::item:selected { background: %4; color: %5; }"
        "QMenu::item:disabled { color: %6; }")
        .arg(pal.inputBg, pal.inputFg, pal.border, pal.selectionBg, pal.selectionFg, pal.textMuted);
    const QString comboQss = QString(
        "QComboBox { background: %1; color: %2; border: 1px solid %3; border-radius: 6px; "
        "padding: 3px 10px; font-size: 12px; }"
        "QComboBox:hover { border-color: %4; }"
        "QComboBox:disabled { color: %7; }"
        // No ::drop-down rule: styling it hands arrow painting to QSS, which
        // then draws nothing; leaving it out keeps the platform arrow.
        "QComboBox QAbstractItemView { background: %1; color: %5; "
        "selection-background-color: %6; border: 1px solid %3; }")
        .arg(pal.inputBg, pal.text, pal.inputBorder, pal.inputFocus, pal.inputFg,
             pal.selectionBg, pal.textMuted);
    const QString secondaryBtn = QString(
        "QPushButton { background: %1; color: %2; border: 1px solid %3; border-radius: 4px; "
        "padding: 3px 12px; font-size: 11px; }"
        "QPushButton:hover { background: %4; border-color: %5; color: %5; }"
        "QPushButton:disabled { color: %6; border-color: %3; background: transparent; }")
        .arg(pal.btnBg, pal.btnFg, pal.btnBorder, pal.btnHover, pal.accent, pal.textMuted);
    const QString textQss = QString(
        "QPlainTextEdit { background: %1; color: %2; border: none; padding: 8px; "
        "font-size: 12px; selection-background-color: %3; selection-color: %4; }"
        "QScrollBar:vertical, QScrollBar:horizontal { background: %1; width: 10px; height: 10px; }"
        "QScrollBar::handle:vertical, QScrollBar::handle:horizontal { background: %5; "
        "border-radius: 5px; min-height: 30px; min-width: 30px; margin: 2px; }"
        "QScrollBar::add-line, QScrollBar::sub-line { height: 0; width: 0; }")
        .arg(pal.bg, pal.text, pal.selectionBg, pal.selectionFg, pal.border) + menuQss;
    const QString tabBarQss = QString(
        "QTabBar { background: %1; }"
        // No font-size here: setTabLabelFont() sizes the label on the bar.
        "QTabBar::tab { background: transparent; color: %2; padding: 6px 16px; border: none; "
        "margin-right: 2px; }"
        "QTabBar::tab:selected { color: %3; border-bottom: 2px solid %3; }"
        "QTabBar::tab:hover:!selected { color: %4; }")
        .arg(pal.chromeBg, pal.textMuted, pal.accent, pal.text);

    m_headerHost->setStyleSheet(QString("background: %1; border-bottom: 1px solid %2;")
                                    .arg(pal.chromeBg, pal.border));
    m_header->setStyleSheet(QString("font-weight: 600; color: %1; font-size: 12px; "
                                    "letter-spacing: 0.04em; border: none;").arg(pal.accent));
    m_envLabel->setStyleSheet(QString("color: %1; font-size: 11px; border: none;").arg(pal.textMuted));
    m_envCombo->setStyleSheet(comboQss);

    m_reqBarHost->setStyleSheet(QString("background: %1;").arg(pal.bg));
    m_methodCombo->setStyleSheet(QString(
        "QComboBox { background: %1; color: %2; border: 1px solid %3; border-radius: 6px; "
        "padding: 4px 10px; font-weight: 700; font-size: 13px; }"
        "QComboBox:hover { border-color: %4; }"
        "QComboBox QAbstractItemView { background: %1; color: %5; "
        "selection-background-color: %6; border: 1px solid %3; }")
        .arg(pal.inputBg, pal.accent, pal.inputBorder, pal.inputFocus, pal.inputFg, pal.selectionBg));
    m_urlInput->setStyleSheet(QString(
        "QLineEdit { background: %1; color: %2; border: 1px solid %3; border-radius: 6px; "
        "padding: 6px 12px; font-size: 13px; selection-background-color: %4; selection-color: %5; }"
        "QLineEdit:focus { border-color: %6; }")
        .arg(pal.inputBg, pal.inputFg, pal.inputBorder, pal.selectionBg, pal.selectionFg,
             pal.inputFocus) + menuQss);
    m_sendBtn->setStyleSheet(QString(
        "QPushButton { background: %1; color: %2; border: none; border-radius: 6px; "
        "font-size: 13px; font-weight: 600; padding: 0 14px; }"
        "QPushButton:hover { background: %3; }"
        "QPushButton:pressed { background: %1; }"
        "QPushButton:disabled { background: %4; color: %5; }")
        .arg(pal.accent, pal.selectionFg, pal.inputFocus, pal.btnBorder, pal.textMuted));
    m_cancelBtn->setStyleSheet(QString(
        "QPushButton { background: %1; color: %2; border: 1px solid %3; border-radius: 6px; "
        "font-size: 13px; padding: 0 14px; }"
        "QPushButton:hover { border-color: %4; color: %4; }"
        "QPushButton:disabled { color: %5; border-color: %3; background: transparent; }")
        .arg(pal.btnBg, pal.btnFg, pal.btnBorder, pal.errorFg, pal.textMuted));

    m_reqTabs->setStyleSheet(QString(
        "QTabWidget::pane { background: %1; border: none; border-top: 1px solid %2; }"
        "QTabWidget::tab-bar { left: 4px; }"
        "QTabBar { background: %1; }"
        "QTabBar::tab { background: transparent; color: %3; padding: 6px 16px; border: none; "
        "margin-right: 2px; }"
        "QTabBar::tab:selected { color: %4; border-bottom: 2px solid %4; }"
        "QTabBar::tab:hover:!selected { color: %5; }")
        .arg(pal.bg, pal.border, pal.textMuted, pal.accent, pal.text));
    if (QWidget *corner = m_reqTabs->cornerWidget())
        corner->setStyleSheet(QString("background: %1;").arg(pal.bg));
    m_headersInput->setStyleSheet(textQss);
    m_bodyInput->setStyleSheet(textQss);
    for (QPushButton *b : {m_importBtn, m_copyCurlBtn, m_openTabBtn, m_saveBtn, m_copyBtn,
                           m_clearHistoryBtn})
        b->setStyleSheet(secondaryBtn);

    setBadge(m_statusBadge->text(), m_badgeRole);
    setNote(m_note->text(), m_noteWarning);

    m_respBarHost->setStyleSheet(QString("background: %1; border-bottom: 1px solid %2;")
                                     .arg(pal.chromeBg, pal.border));
    m_respTabs->setStyleSheet(tabBarQss);
    m_respBody->setStyleSheet(textQss);
    m_respHeaders->setStyleSheet(textQss);
    m_split->setStyleSheet(QString("QSplitter::handle { background: %1; height: 1px; }").arg(pal.border));

    m_historyHost->setStyleSheet(QString("background: %1;").arg(pal.chromeBg));
    m_historyTitle->setStyleSheet(QString("color: %1; font-weight: 600; font-size: 11px; "
                                          "letter-spacing: 0.04em;").arg(pal.text));
    m_historyHint->setStyleSheet(QString("color: %1; font-size: 11px;").arg(pal.textMuted));
    m_history->setStyleSheet(QString(
        "QTreeWidget { background: %1; color: %2; border: none; border-top: 1px solid %3; "
        "font-size: 11px; alternate-background-color: %1; }"
        "QTreeWidget::item { padding: 2px 0; }"
        "QTreeWidget::item:hover { background: %4; }"
        "QTreeWidget::item:selected { background: %5; color: %6; }"
        "QHeaderView::section { background: %7; color: %8; border: none; "
        "border-bottom: 1px solid %3; padding: 3px 6px; font-size: 11px; }"
        "QScrollBar:vertical { background: %1; width: 10px; }"
        "QScrollBar::handle:vertical { background: %3; border-radius: 5px; min-height: 30px; margin: 2px; }"
        "QScrollBar::add-line, QScrollBar::sub-line { height: 0; width: 0; }")
        .arg(pal.bg, pal.text, pal.border, pal.btnHover, pal.selectionBg, pal.selectionFg,
             pal.chromeBg, pal.textMuted) + menuQss);
    QPalette vp = m_history->palette();
    vp.setColor(QPalette::Base, QColor(pal.bg));
    vp.setColor(QPalette::Text, QColor(pal.text));
    m_history->setPalette(vp);
}

void RestClient::onThemeChanged() {
    applyPalette();
    update();
}

void RestClient::setBadge(const QString &text, const QString &role) {
    const NpPalette pal = npPalette();
    m_badgeRole = role;
    const QString color = role == QLatin1String("success") ? pal.successFg
                        : role == QLatin1String("accent")  ? pal.accent
                        : role == QLatin1String("warning") ? pal.warningFg
                        : role == QLatin1String("error")   ? pal.errorFg
                                                           : pal.textMuted;
    m_statusBadge->setStyleSheet(QString(
        "background: %1; color: %2; padding: 5px 12px; border-top: 1px solid %3; "
        "font-size: 11px; font-weight: 600; letter-spacing: 0.05em;")
        .arg(pal.chromeBg, color, pal.border));
    m_statusBadge->setText(text);
}

void RestClient::setNote(const QString &text, bool warning) {
    const NpPalette pal = npPalette();
    m_noteWarning = warning;
    m_note->setStyleSheet(QString("background: %1; color: %2; padding: 4px 12px 6px 12px; "
                                  "font-size: 11px;")
                              .arg(pal.chromeBg, warning ? pal.warningFg : pal.textMuted));
    m_note->setText(text);
    m_note->setVisible(!text.isEmpty());
}

void RestClient::refuse(const QString &badge, const QString &detail) {
    m_lastRefusal = detail.isEmpty() ? badge : badge + QStringLiteral(": ") + detail;
    setBadge(badge, QStringLiteral("error"));
    setNote(detail, true);
}

// ─── Filling the request ────────────────────────────────────────────────

void RestClient::fillFromRequest(const QString &method, const QString &url,
                                 const QString &headers, const QString &body) {
    const QString m = method.trimmed().toUpper();
    if (m_methodCombo->findText(m) < 0) m_methodCombo->addItem(m);
    m_methodCombo->setCurrentText(m);
    m_urlInput->setText(url);
    m_headersInput->setPlainText(headers);
    m_bodyInput->setPlainText(body);
    m_reqTabs->setCurrentIndex(body.trimmed().isEmpty() ? 0 : 1);
}

void RestClient::setContext(const Context &ctx) {
    const bool dirChanged = ctx.dir != m_ctx.dir;
    m_ctx = ctx;
    if (dirChanged) reloadEnvironments();
}

void RestClient::reloadEnvironments() {
    m_envs = HttpFile::loadEnvironments(m_ctx.dir);
    const QString remembered = m_ctx.dir.isEmpty()
                                   ? QString()
                                   : RestHistory::environmentFor(historyPath(), m_ctx.dir);
    QString want = m_envCombo->currentData().toString();
    if (want.isEmpty() || !m_envs.names.contains(want)) want = remembered;

    m_fillingEnvCombo = true;
    m_envCombo->clear();
    m_envCombo->addItem(tr("No environment"), QString());
    for (const QString &n : m_envs.names) m_envCombo->addItem(n, n);
    const int idx = m_envCombo->findData(want);
    m_envCombo->setCurrentIndex(idx > 0 ? idx : 0);
    m_fillingEnvCombo = false;

    QString tip = tr("Environments come from http-client.env.json, and "
                     "http-client.private.env.json for secrets, next to the .http file.");
    if (!m_envs.error.isEmpty()) tip += QStringLiteral("\n") + m_envs.error;
    m_envCombo->setToolTip(tip);
    if (!m_envs.error.isEmpty()) setNote(tr("Environment file problem: %1").arg(m_envs.error), true);
}

void RestClient::onEnvironmentChosen(int) {
    if (m_fillingEnvCombo || m_ctx.dir.isEmpty()) return;
    RestHistory::setEnvironmentFor(historyPath(), m_ctx.dir, m_envCombo->currentData().toString());
}

void RestClient::executeRequest(const QString &httpText, const QString &filePath) {
    if (isBusy()) {
        setNote(tr("A request is still running. Cancel it first."), true);
        return;
    }
    const HttpFile::Document doc = HttpFile::parse(httpText);
    if (doc.requests.isEmpty()) {
        refuse(tr("Not sent"), tr("The selection has no request line (METHOD URL)."));
        return;
    }
    const HttpFile::Request &r = doc.requests.first();
    Context ctx;
    ctx.filePath = filePath;
    ctx.dir = filePath.isEmpty() ? QString() : QFileInfo(filePath).absolutePath();
    ctx.fileVariables = doc.variables;
    ctx.requestName = r.name;
    setContext(ctx);
    fillFromRequest(r.method, r.url, HttpFile::headersToText(r.headers), r.body);
    send();
}

bool RestClient::sendHttpRequest(const QString &fileText, int line, const QString &filePath) {
    if (isBusy()) {
        setNote(tr("A request is still running. Cancel it first."), true);
        return false;
    }
    const HttpFile::Document doc = HttpFile::parse(fileText);
    const int idx = doc.requestAtLine(line);
    if (idx < 0) {
        refuse(tr("Not sent"), tr("There is no request at line %1. Put the caret inside a "
                                  "request block (between ### lines).").arg(line + 1));
        return false;
    }
    const HttpFile::Request &r = doc.requests[idx];
    Context ctx;
    ctx.filePath = filePath;
    ctx.dir = filePath.isEmpty() ? QString() : QFileInfo(filePath).absolutePath();
    ctx.fileVariables = doc.variables;
    ctx.requestName = r.name;
    setContext(ctx);
    fillFromRequest(r.method, r.url, HttpFile::headersToText(r.headers), r.body);
    send();
    return true;
}

// ─── MCP (rest_request) ─────────────────────────────────────────────────

RestClient::Resolved RestClient::resolve(const QString &fileText, int line,
                                         const QString &filePath, const QString &method,
                                         const QString &url, const QString &headersText,
                                         const QString &body, const QString &historyPath) {
    Resolved out;
    QString rawMethod = method, rawUrl = url.trimmed(), rawHeaders = headersText,
            rawBody = body.trimmed();
    QList<QPair<QString, QString>> fileVars;
    if (!fileText.isEmpty()) {
        const HttpFile::Document doc = HttpFile::parse(fileText);
        const int idx = doc.requestAtLine(line);
        if (idx < 0) {
            out.error = tr("There is no request at line %1 (put the line inside a request "
                           "block between ### lines)").arg(line + 1);
            return out;
        }
        const HttpFile::Request &r = doc.requests[idx];
        rawMethod = r.method;
        rawUrl = r.url.trimmed();
        rawHeaders = HttpFile::headersToText(r.headers);
        rawBody = r.body.trimmed();
        fileVars = doc.variables;
        out.name = r.name;
    }
    out.method = rawMethod.trimmed().toUpper();
    if (rawUrl.isEmpty()) {
        out.error = tr("The request has no URL");
        return out;
    }
    if (RestHistory::containsRedacted(rawUrl) || RestHistory::containsRedacted(rawHeaders) ||
        RestHistory::bodyBlocksSend(rawBody)) {
        out.error = tr("The URL, headers or body still contain <redacted> values (or a body "
                       "history did not keep)");
        return out;
    }
    if (rawBody.startsWith(QLatin1String("< "))) {
        out.error = tr("A body read from a file is not supported");
        return out;
    }
    // The environment Send would pick: the one remembered for this folder.
    const QString dir = filePath.isEmpty() ? QString() : QFileInfo(filePath).absolutePath();
    HttpFile::Environments envs;
    QString envName;
    if (!dir.isEmpty()) {
        envs = HttpFile::loadEnvironments(dir);
        const QString hp = historyPath.isEmpty()
                               ? Config::appConfigDir() + QStringLiteral("/rest-client.json")
                               : historyPath;
        envName = RestHistory::environmentFor(hp, dir);
        if (!envs.names.contains(envName)) envName.clear();
    }
    out.rawUrl = rawUrl;
    out.rawHeaders = rawHeaders.trimmed();
    out.rawBody = rawBody;
    out.filePath = filePath;
    out.environment = envName;
    const HttpFile::Resolver resolver(envs.variablesFor(envName), fileVars);
    QStringList undefined;
    auto take = [&](const HttpFile::ResolveResult &r) {
        for (const QString &n : r.undefined + r.cycles)
            if (!undefined.contains(n)) undefined << n;
        return r.text;
    };
    QString resolvedUrl = take(resolver.resolve(rawUrl));
    for (const auto &h : HttpFile::headersFromText(rawHeaders))
        out.headers.append({take(resolver.resolve(h.first)), take(resolver.resolve(h.second))});
    out.body = take(resolver.resolve(rawBody));
    if (!undefined.isEmpty()) {
        QStringList names;
        for (const QString &n : undefined) names << QStringLiteral("{{%1}}").arg(n);
        out.error = tr("Undefined variable(s) %1: define them with @name = value in the .http "
                       "file or in http-client.env.json next to it")
                        .arg(names.join(QStringLiteral(", ")));
        return out;
    }
    if (!resolvedUrl.contains(QLatin1String("://"))) resolvedUrl.prepend(QStringLiteral("https://"));
    out.url = resolvedUrl;
    return out;
}

bool RestClient::sendResolvedForMcp(const Resolved &r, QString *err) {
    if (isBusy()) {
        if (err) *err = tr("the REST client is already sending a request");
        return false;
    }
    if (!r.error.isEmpty()) {
        if (err) *err = r.error;
        return false;
    }
    m_lastRefusal.clear();
    // The approved request, byte for byte: no second resolve, so a changed
    // environment choice or env file cannot redirect it to another host.
    if (!startRequest(r.method.trimmed().toUpper(), r.url, r.headers, r.body, r.rawUrl,
                      r.rawHeaders, r.rawBody, r.name, r.filePath)) {
        if (err)
            *err = m_lastRefusal.isEmpty() ? tr("the REST client did not send the request")
                                           : m_lastRefusal;
        return false;
    }
    return true;
}

QJsonObject RestClient::lastResponseJson(int bodyCap) const {
    QJsonObject o;
    o[QStringLiteral("status")] = m_respStatus;
    o[QStringLiteral("time_ms")] = double(m_respMs);
    o[QStringLiteral("url")] = m_respUrl;
    if (m_respStatus == 0) {
        o[QStringLiteral("error")] = m_respOutcome.isEmpty() ? tr("No response") : m_respOutcome;
    } else {
        o[QStringLiteral("reason")] = m_respReason;
        QJsonArray headers;
        for (int i = 1; i < m_respHeaderLines.size(); ++i) headers.append(m_respHeaderLines.at(i));
        o[QStringLiteral("headers")] = headers;
        o[QStringLiteral("content_type")] = m_respContentType;
        o[QStringLiteral("size")] = double(m_respBytes.size());
        if (isBinaryResponse(m_respContentType, m_respBytes)) {
            o[QStringLiteral("binary")] = true;
            o[QStringLiteral("body")] =
                tr("(binary response, %1 bytes; not returned)").arg(m_respBytes.size());
            o[QStringLiteral("truncated")] = false;
        } else {
            o[QStringLiteral("binary")] = false;
            const bool cut = m_respBytes.size() > bodyCap;
            o[QStringLiteral("body")] = QString::fromUtf8(cut ? m_respBytes.left(bodyCap)
                                                               : m_respBytes);
            o[QStringLiteral("truncated")] = cut;
        }
    }
    if (!m_respNotes.isEmpty())
        o[QStringLiteral("notes")] = QJsonArray::fromStringList(m_respNotes);
    return o;
}

// ─── Sending ────────────────────────────────────────────────────────────

void RestClient::send() {
    if (isBusy()) return;

    const QString method = m_methodCombo->currentText().trimmed().toUpper();
    QString rawUrl = m_urlInput->text().trimmed();
    const QString rawHeaders = m_headersInput->toPlainText();
    const QString rawBody = m_bodyInput->toPlainText().trimmed();

    if (rawUrl.isEmpty()) {
        refuse(tr("Enter a URL first."), QString());
        m_urlInput->setFocus();
        return;
    }
    if (RestHistory::containsRedacted(rawUrl) || RestHistory::containsRedacted(rawHeaders) ||
        RestHistory::bodyBlocksSend(rawBody)) {
        refuse(tr("Not sent: fill in the <redacted> values"),
               tr("History never stores secrets. Replace each <redacted> value in the URL, "
                  "headers or body (or use a {{variable}}), or paste back a body history did "
                  "not keep, and press Send again."));
        return;
    }
    if (rawBody.startsWith(QLatin1String("< "))) {
        refuse(tr("Not sent: body from a file"),
               tr("A body read from a file (%1) is not supported. Paste the body into the Body "
                  "tab instead.").arg(rawBody.section(QLatin1Char('\n'), 0, 0)));
        return;
    }

    // Environments are re-read on every send so edits to the env files apply.
    if (!m_ctx.dir.isEmpty()) reloadEnvironments();
    const HttpFile::Resolver resolver(m_envs.variablesFor(m_envCombo->currentData().toString()),
                                      m_ctx.fileVariables);
    QStringList undefined, cycles;
    auto take = [&](const HttpFile::ResolveResult &r) {
        for (const QString &n : r.undefined) if (!undefined.contains(n)) undefined << n;
        for (const QString &n : r.cycles) if (!cycles.contains(n)) cycles << n;
        return r.text;
    };
    QString url = take(resolver.resolve(rawUrl));
    HttpFile::HeaderList headers;
    for (const auto &h : HttpFile::headersFromText(rawHeaders))
        headers.append({take(resolver.resolve(h.first)), take(resolver.resolve(h.second))});
    const QString body = take(resolver.resolve(rawBody));

    if (!undefined.isEmpty() || !cycles.isEmpty()) {
        QStringList names;
        for (const QString &n : undefined) names << QStringLiteral("{{%1}}").arg(n);
        for (const QString &n : cycles) names << QStringLiteral("{{%1}}").arg(n);
        const QString badge = names.size() == 1
                                  ? tr("Not sent: undefined variable %1").arg(names.first())
                                  : tr("Not sent: undefined variables %1").arg(names.join(QStringLiteral(", ")));
        QString detail;
        if (!cycles.isEmpty())
            detail = tr("%1 refers back to itself. ").arg(cycles.join(QStringLiteral(", ")));
        detail += tr("Define variables with @name = value in the .http file, or in "
                     "http-client.env.json / http-client.private.env.json next to it and pick "
                     "that environment above.");
        refuse(badge, detail);
        return;
    }

    if (!url.contains(QLatin1String("://")) && !HttpFile::hasVariables(rawUrl))
        m_urlInput->setText(QStringLiteral("https://") + url);
    startRequest(method, url, headers, body, rawUrl, rawHeaders.trimmed(), rawBody,
                 m_ctx.requestName, m_ctx.filePath);
}

bool RestClient::startRequest(const QString &method, QString url,
                              const HttpFile::HeaderList &headers, const QString &body,
                              const QString &histUrl, const QString &histHeaders,
                              const QString &histBody, const QString &name,
                              const QString &source) {
    if (!url.contains(QLatin1String("://"))) url.prepend(QStringLiteral("https://"));
    const QUrl qurl(url, QUrl::TolerantMode);
    const QString scheme = qurl.scheme().toLower();
    if (scheme != QLatin1String("http") && scheme != QLatin1String("https")) {
        refuse(tr("Not sent: unsupported URL"), tr("Only http:// and https:// URLs can be sent."));
        return false;
    }
    if (!qurl.isValid() || qurl.host().isEmpty()) {
        refuse(tr("Not sent: invalid URL"), url);
        return false;
    }

    QNetworkRequest req(qurl);
    // Never follow redirects: the 3xx and its Location are shown instead, so an
    // Authorization header can never be replayed to another host.
    req.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::ManualRedirectPolicy);
    for (const auto &h : headers) req.setRawHeader(h.first.toUtf8(), h.second.toUtf8());

    const QByteArray bodyBytes = body.toUtf8();
    QNetworkReply *reply = nullptr;
    if (method == QLatin1String("GET") && bodyBytes.isEmpty()) reply = m_nam->get(req);
    else if (method == QLatin1String("HEAD") && bodyBytes.isEmpty()) reply = m_nam->head(req);
    else if (method == QLatin1String("POST")) reply = m_nam->post(req, bodyBytes);
    else if (method == QLatin1String("PUT")) reply = m_nam->put(req, bodyBytes);
    else if (method == QLatin1String("DELETE") && bodyBytes.isEmpty()) reply = m_nam->deleteResource(req);
    else reply = m_nam->sendCustomRequest(req, method.toUtf8(), bodyBytes);

    m_reply = reply;
    m_timedOut = false;
    m_tooLarge = false;
    m_sslErrors.clear();
    m_sentMethod = method;
    m_sentUrl = histUrl;
    m_sentHeaders = histHeaders;
    m_sentBody = histBody;
    m_sentName = name;
    m_sentSource = source;
    m_respUrl = url;
    m_elapsed.start();
    m_timeout.start(m_timeoutMs);

    m_sendBtn->setEnabled(false);
    m_cancelBtn->setEnabled(true);
    setBadge(tr("Sending…"), QStringLiteral("warning"));
    setNote(QString());
    m_respBody->setPlainText(tr("%1 %2\nSending…").arg(method, url));
    m_respHeaders->clear();
    m_haveResponse = false;
    updateResponseActions();

    connect(reply, &QNetworkReply::downloadProgress, this, [this, reply](qint64 received, qint64) {
        if (received > kMaxResponse && reply == m_reply && !m_tooLarge) {
            m_tooLarge = true;
            // Queued: never abort a reply from inside its own signal handler.
            QMetaObject::invokeMethod(reply, "abort", Qt::QueuedConnection);
        }
    });
    connect(reply, &QNetworkReply::sslErrors, this, [this](const QList<QSslError> &errs) {
        // Shown, never ignored.
        for (const QSslError &e : errs) m_sslErrors << e.errorString();
    });
    connect(reply, &QNetworkReply::finished, this, [this, reply]() { onReplyFinished(reply); });
    return true;
}

void RestClient::cancelRequest() {
    if (!m_reply) return;
    QMetaObject::invokeMethod(m_reply.data(), "abort", Qt::QueuedConnection);
}

void RestClient::onReplyFinished(QNetworkReply *reply) {
    reply->deleteLater();
    if (reply != m_reply) return;
    m_timeout.stop();
    m_reply = nullptr;
    const qint64 ms = m_elapsed.elapsed();

    const QNetworkReply::NetworkError err = reply->error();
    const bool aborted = err == QNetworkReply::OperationCanceledError;
    // Headers may already have arrived when a request is cancelled; it still
    // counts as no response (status 0), and the aborted body is not read.
    const int httpStatus = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    const int status = aborted ? 0 : httpStatus;
    const QString reason = reply->attribute(QNetworkRequest::HttpReasonPhraseAttribute).toString();
    m_respBytes = aborted ? QByteArray() : reply->readAll();
    m_respContentType = QString::fromUtf8(reply->rawHeader("Content-Type"));

    QString outcome;
    if (aborted) {
        outcome = m_timedOut ? tr("Timed out after %1 s").arg(m_timeoutMs / 1000.0)
                : m_tooLarge ? tr("Stopped: response larger than %1").arg(sizeText(kMaxResponse))
                             : tr("Cancelled");
    } else if (status == 0 && err != QNetworkReply::NoError) {
        outcome = reply->errorString();
    }

    QStringList headerLines;
    if (httpStatus > 0) {
        const bool h2 = reply->attribute(QNetworkRequest::Http2WasUsedAttribute).toBool();
        headerLines << QStringLiteral("%1 %2 %3")
                           .arg(h2 ? QStringLiteral("HTTP/2") : QStringLiteral("HTTP/1.1"))
                           .arg(httpStatus)
                           .arg(reason);
        for (const auto &p : reply->rawHeaderPairs())
            headerLines << QString::fromUtf8(p.first) + QStringLiteral(": ") +
                               QString::fromUtf8(p.second);
    }
    m_respHeaders->setPlainText(headerLines.join(QLatin1Char('\n')));
    m_haveResponse = status > 0;

    const qint64 bytes = m_respBytes.size();
    m_respStatus = status;
    m_respReason = reason;
    m_respMs = ms;
    m_respOutcome = outcome;
    m_respHeaderLines = headerLines;
    QStringList notes;
    if (status > 0) {
        const QString role = status < 300 ? QStringLiteral("success")
                           : status < 400 ? QStringLiteral("accent")
                           : status < 500 ? QStringLiteral("warning")
                                          : QStringLiteral("error");
        setBadge(QStringLiteral("%1 %2  ·  %3 ms  ·  %4").arg(status).arg(reason).arg(ms)
                     .arg(sizeText(bytes)), role);
        const QByteArray location = reply->rawHeader("Location");
        if (status >= 300 && status < 400 && !location.isEmpty())
            notes << tr("Redirect not followed. Location: %1").arg(QString::fromUtf8(location));
    } else {
        setBadge(QStringLiteral("%1  ·  %2 ms").arg(outcome.isEmpty() ? tr("Error") : outcome).arg(ms),
                 err == QNetworkReply::OperationCanceledError ? QStringLiteral("warning")
                                                              : QStringLiteral("error"));
        if (err != QNetworkReply::OperationCanceledError) {
            const QString hint =
                err == QNetworkReply::HostNotFoundError ? tr("DNS lookup failed: check the URL or your network.")
              : err == QNetworkReply::ConnectionRefusedError ? tr("Connection refused: nothing is listening on that port.")
              : err == QNetworkReply::SslHandshakeFailedError ? tr("TLS handshake failed: invalid, expired or self-signed certificate. Certificate errors are never ignored.")
              : QString();
            if (!hint.isEmpty()) notes << hint;
        }
    }
    if (!m_sslErrors.isEmpty()) notes << tr("TLS: %1").arg(m_sslErrors.join(QStringLiteral("; ")));
    renderResponse();
    if (status == 0) {
        m_respBody->setPlainText(outcome.isEmpty() ? tr("No response.") : outcome);
    } else if (m_respBinary) {
        notes << tr("Binary response, %1 bytes%2. Use Save… to keep it.")
                     .arg(QLocale().toString(bytes))
                     .arg(m_respContentType.isEmpty() ? QString()
                                                      : QStringLiteral(" (%1)").arg(mainType(m_respContentType)));
    } else if (bytes > kRenderCap) {
        notes << tr("Showing the first %1 of %2. Open in tab or Save… gets the whole response.")
                     .arg(sizeText(kRenderCap), sizeText(bytes));
    }
    setNote(notes.join(QLatin1Char('\n')), !notes.isEmpty() && status == 0);
    m_respNotes = notes;

    m_sendBtn->setEnabled(true);
    m_cancelBtn->setEnabled(false);
    updateResponseActions();

    RestHistory::Entry e;
    e.time = QDateTime::currentDateTime();
    e.method = m_sentMethod;
    e.url = m_sentUrl;
    e.headers = m_sentHeaders;
    e.body = m_sentBody;
    e.status = status;
    e.outcome = status > 0 ? QString() : (outcome.isEmpty() ? tr("Error") : outcome);
    e.ms = ms;
    e.size = bytes;
    e.name = m_sentName;
    e.sourceFile = m_sentSource;
    RestHistory::append(historyPath(), e);
    reloadHistory();

    emit requestFinished();
}

// ─── Response view ──────────────────────────────────────────────────────

bool RestClient::isBinaryResponse(const QString &contentType, const QByteArray &body) {
    if (body.left(8192).contains('\0')) return true;
    const QString t = mainType(contentType);
    if (t.isEmpty() || t.startsWith(QLatin1String("text/"))) return false;
    static const char *const textish[] = {"json", "xml", "javascript", "ecmascript", "html",
                                          "x-www-form-urlencoded", "yaml", "csv", "graphql",
                                          "sql", "markdown", "toml", "x-sh", "event-stream",
                                          "ndjson", "problem", "x-ndjson"};
    for (const char *k : textish)
        if (t.contains(QLatin1String(k))) return false;
    return true;
}

QString RestClient::languageForContentType(const QString &contentType) {
    const QString t = mainType(contentType);
    if (t.contains(QLatin1String("json"))) return QStringLiteral("JSON");
    if (t.contains(QLatin1String("html"))) return QStringLiteral("HTML");
    if (t.contains(QLatin1String("xml"))) return QStringLiteral("XML");
    return QStringLiteral("Plain Text");
}

QString RestClient::suggestedFileName(const QString &url, const QString &contentType) {
    QString name = QUrl(url).fileName();
    name.replace(QRegularExpression(QStringLiteral("[^A-Za-z0-9._-]")), QStringLiteral("_"));
    if (name.isEmpty() || name.startsWith(QLatin1Char('.'))) name = QStringLiteral("response");
    const QString t = mainType(contentType);
    if (!name.contains(QLatin1Char('.')) && !t.isEmpty()) {
        const QMimeType mt = QMimeDatabase().mimeTypeForName(t);
        const QString suffix = mt.isValid() ? mt.preferredSuffix() : QString();
        if (!suffix.isEmpty()) name += QLatin1Char('.') + suffix;
    }
    return name;
}

void RestClient::renderResponse() {
    m_respBinary = isBinaryResponse(m_respContentType, m_respBytes);
    // The "Set a method and URL" hint is for before the first response; an
    // empty pane after one says why it is empty.
    m_respBody->setPlaceholderText(m_respBinary
                                       ? tr("Binary response: not shown as text. Use Save… to keep it.")
                                       : tr("The response has no body."));
    if (m_respBinary) {
        m_respBody->setPlainText(QString());
        return;
    }
    const bool capped = m_respBytes.size() > kRenderCap;
    QString text;
    if (!capped) {
        const QJsonDocument doc = QJsonDocument::fromJson(m_respBytes);
        text = doc.isNull() ? decodeBody(m_respBytes, m_respContentType)
                            : QString::fromUtf8(doc.toJson(QJsonDocument::Indented));
    } else {
        text = decodeBody(m_respBytes.left(kRenderCap), m_respContentType);
        if (text.endsWith(QChar::ReplacementCharacter)) text.chop(1);  // cut mid-character
    }
    m_respBody->setPlainText(text);
}

void RestClient::showResponseView(int which) {
    m_respStack->setCurrentIndex(which == 1 ? 1 : 0);
    updateResponseActions();
}

void RestClient::updateResponseActions() {
    const bool body = m_respStack->currentIndex() == 0;
    m_openTabBtn->setEnabled(m_haveResponse && !m_respBinary && !m_respBytes.isEmpty());
    m_saveBtn->setEnabled(m_haveResponse);
    m_copyBtn->setEnabled(m_haveResponse && (!body || !m_respBinary));
}

void RestClient::copyResponse() {
    QPlainTextEdit *view = m_respStack->currentIndex() == 0 ? m_respBody : m_respHeaders;
    QApplication::clipboard()->setText(view->toPlainText());
}

void RestClient::openResponseInTab() {
    if (!m_haveResponse || m_respBinary) return;
    QString text;
    const QJsonDocument doc = m_respBytes.size() <= 64 * 1024 * 1024
                                  ? QJsonDocument::fromJson(m_respBytes) : QJsonDocument();
    text = doc.isNull() ? decodeBody(m_respBytes, m_respContentType)
                        : QString::fromUtf8(doc.toJson(QJsonDocument::Indented));
    // Same threshold as Editor::loadFile: big buffers open without a lexer.
    const QString lang = m_respBytes.size() > 50 * 1024 * 1024
                             ? QStringLiteral("Plain Text")
                             : languageForContentType(m_respContentType);
    emit openInTabRequested(text, lang);
}

bool RestClient::saveResponseTo(const QString &path, QString *error) {
    QSaveFile f(path);
    if (!f.open(QIODevice::WriteOnly)) {
        if (error) *error = f.errorString();
        return false;
    }
    if (f.write(m_respBytes) != m_respBytes.size() || !f.commit()) {
        if (error) *error = f.errorString();
        return false;
    }
    return true;
}

void RestClient::saveResponse() {
    if (!m_haveResponse) return;
    const QString name = suggestedFileName(m_respUrl, m_respContentType);
    const QString start = QDir(Config::instance().lastDirOrHome()).filePath(name);
    const QString suffix = QFileInfo(name).suffix();
    const QString filter = suffix.isEmpty()
                               ? tr("All Files (*)")
                               : tr("%1 files (*.%2);;All Files (*)").arg(suffix.toUpper(), suffix);
    const QString path = QFileDialog::getSaveFileName(this, tr("Save Response"), start, filter);
    if (path.isEmpty()) return;
    QString err;
    if (saveResponseTo(path, &err)) {
        Config::instance().noteLastDir(path);
        setNote(tr("Saved %1 bytes to %2").arg(QLocale().toString(m_respBytes.size()),
                                               QDir::toNativeSeparators(path)));
    } else {
        setNote(tr("Could not save: %1").arg(err), true);
    }
}

// ─── cURL ───────────────────────────────────────────────────────────────

bool RestClient::importCurl(const QString &command) {
    const CurlConvert::ImportResult r = CurlConvert::fromCurl(command);
    if (!r.ok) {
        refuse(tr("Import failed"), r.error);
        return false;
    }
    fillFromRequest(r.request.method, r.request.url, HttpFile::headersToText(r.request.headers),
                    r.request.body);
    m_ctx.requestName.clear();  // no longer the .http request it came after
    setBadge(tr("Imported from cURL. Press Send to run it."), QStringLiteral("muted"));
    setNote(r.notes.join(QLatin1Char('\n')), !r.notes.isEmpty());
    return true;
}

QString RestClient::currentAsCurl() const {
    CurlConvert::Request r;
    r.method = m_methodCombo->currentText().trimmed().toUpper();
    r.url = m_urlInput->text().trimmed();
    r.headers = HttpFile::headersFromText(m_headersInput->toPlainText());
    r.body = m_bodyInput->toPlainText().trimmed();
    return CurlConvert::toCurl(r);
}

void RestClient::copyAsCurl() {
    const QString cmd = currentAsCurl();
    QApplication::clipboard()->setText(cmd);
    const bool vars = HttpFile::hasVariables(cmd);
    setNote(vars ? tr("Copied as cURL. {{variables}} are left unexpanded so tokens are not put "
                      "on the clipboard.")
                 : tr("Copied as cURL."));
}

void RestClient::openImportDialog() {
    const NpPalette pal = npPalette();
    auto *dlg = new QDialog(this);
    dlg->setAttribute(Qt::WA_DeleteOnClose);
    dlg->setWindowTitle(tr("Import cURL"));
    dlg->resize(640, 320);
    // A dialog is its own window: it inherits no stylesheet from the panel.
    dlg->setStyleSheet(QString(
        "QDialog { background: %1; }"
        "QLabel { color: %2; }"
        "QPlainTextEdit { background: %3; color: %4; border: 1px solid %5; border-radius: 4px; "
        "padding: 6px; selection-background-color: %6; selection-color: %7; }"
        "QPushButton { background: %8; color: %9; border: 1px solid %10; border-radius: 4px; "
        "padding: 4px 14px; min-width: 70px; }"
        "QPushButton:hover { border-color: %11; color: %11; }"
        "QMenu { background: %3; color: %4; border: 1px solid %5; }"
        "QMenu::item:selected { background: %6; color: %7; }")
        .arg(pal.bg, pal.textMuted, pal.inputBg, pal.inputFg, pal.inputBorder, pal.selectionBg,
             pal.selectionFg, pal.btnBg, pal.btnFg)
        .arg(pal.btnBorder, pal.accent));
    auto *lay = new QVBoxLayout(dlg);
    auto *hint = new QLabel(tr("Paste a curl command (bash or Windows cmd). Files (-d @file) are "
                               "never read, and -k does not turn certificate checks off."));
    hint->setWordWrap(true);
    lay->addWidget(hint);
    auto *edit = new QPlainTextEdit;
    edit->setFont(notepatraCodeFont());
    const QString clip = QApplication::clipboard()->text().trimmed();
    if (clip.startsWith(QLatin1String("curl"), Qt::CaseInsensitive)) edit->setPlainText(clip);
    lay->addWidget(edit, 1);
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    buttons->button(QDialogButtonBox::Ok)->setText(tr("Import"));
    lay->addWidget(buttons);
    connect(buttons, &QDialogButtonBox::accepted, dlg, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, dlg, &QDialog::reject);
    connect(dlg, &QDialog::accepted, this, [this, edit]() { importCurl(edit->toPlainText()); });
    dlg->open();  // window-modal, no nested event loop
    edit->setFocus();
}

// ─── History ────────────────────────────────────────────────────────────

void RestClient::reloadHistory() {
    m_history->clear();
    const QList<RestHistory::Entry> entries = RestHistory::load(historyPath());
    for (const RestHistory::Entry &e : entries) {
        auto *it = new QTreeWidgetItem(m_history);
        const QDateTime local = e.time.toLocalTime();
        it->setText(0, local.date() == QDate::currentDate() ? local.toString(QStringLiteral("HH:mm:ss"))
                                                            : local.toString(QStringLiteral("MM-dd HH:mm")));
        it->setText(1, e.method);
        const int scheme = e.url.indexOf(QLatin1String("://"));
        const int slash = scheme >= 0 ? e.url.indexOf(QLatin1Char('/'), scheme + 3) : -1;
        const QString path = e.url.startsWith(QLatin1String("{{")) || slash < 0 ? e.url : e.url.mid(slash);
        it->setText(2, e.name.isEmpty() ? path : QStringLiteral("%1  (%2)").arg(path, e.name));
        it->setToolTip(2, e.url);
        it->setText(3, e.status > 0 ? QString::number(e.status) : e.outcome);
        it->setToolTip(3, e.outcome);
        it->setText(4, QString::number(e.ms));
        it->setTextAlignment(4, Qt::AlignRight | Qt::AlignVCenter);
    }
    m_clearHistoryBtn->setEnabled(!entries.isEmpty());
}

void RestClient::loadHistoryEntry(int row) {
    const QList<RestHistory::Entry> entries = RestHistory::load(historyPath());
    if (row < 0 || row >= entries.size()) return;
    if (isBusy()) {
        setNote(tr("A request is still running. Cancel it first."), true);
        return;
    }
    const RestHistory::Entry &e = entries[row];
    Context ctx;
    ctx.requestName = e.name;
    if (!e.sourceFile.isEmpty() && QFileInfo(e.sourceFile).isFile()) {
        QFile f(e.sourceFile);
        if (f.size() <= 16 * 1024 * 1024 && f.open(QIODevice::ReadOnly)) {
            ctx.filePath = e.sourceFile;
            ctx.dir = QFileInfo(e.sourceFile).absolutePath();
            ctx.fileVariables = HttpFile::parse(QString::fromUtf8(f.readAll())).variables;
        }
    }
    setContext(ctx);
    fillFromRequest(e.method, e.url, e.headers, e.body);
    QStringList notes;
    notes << tr("Loaded from history. Press Send to run it again.");
    if (RestHistory::containsRedacted(e.url) || RestHistory::containsRedacted(e.headers) ||
        RestHistory::containsRedacted(e.body))
        notes << tr("Values marked <redacted> were not saved; fill them in before sending.");
    if (e.bodyOmitted)
        notes << tr("History did not keep this body (too large, or not text); paste it in "
                    "before sending.");
    setBadge(tr("Ready"), QStringLiteral("muted"));
    setNote(notes.join(QLatin1Char(' ')), notes.size() > 1);
}

void RestClient::clearHistory() {
    QString err;
    if (!RestHistory::clear(historyPath(), &err)) setNote(tr("Could not clear history: %1").arg(err), true);
    reloadHistory();
}
