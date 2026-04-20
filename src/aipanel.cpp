#include "aipanel.h"
#include "ai_context.h"
#include "fonts.h"
#include "config.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QFont>
#include <QScrollBar>
#include <QMenu>
#include <QApplication>
#include <QClipboard>
#include <QTimer>
#include <QFileDialog>
#include <QFileInfo>
#include <QFile>
#include <QDir>
#include <QDesktopServices>
#include <QMouseEvent>
#include <QProcess>
#include <QImage>
#include <QBuffer>
#include <QByteArray>
#include <QDateTime>
#include <QPainter>
#include <QPainterPath>
#include <QRegularExpression>
#include <QStandardPaths>
#include <QTextDocument>
#include <QUrl>

namespace {

// ═══════════════════════════════════════════════════════════════════════
// Theme-aware palette for the AI Assistant panel — reads Config::theme
// at construction so Light mode looks Light (Clay palette) and Dark/
// Monokai get the familiar VS-Code-ish dark greys. Before this, the
// AI panel was hardcoded dark — looked out-of-place on Light theme.
// ═══════════════════════════════════════════════════════════════════════

struct AiPalette {
    // Chrome
    QString bg, chromeBg, headerFg;
    QString inputBg, inputBorder, inputText, inputFocus;
    QString btnBg, btnBorder, btnHover;
    QString recBtnBg, recBtnBorder, recBtnHover;
    // Bubbles
    QString chatBg, chatFg;
    QString userBg, userFg, userBorder, userLabel;
    QString assistBg, assistFg, assistBorder, assistAccent;
    QString errBg, errFg, errBorder;
    // Code blocks inside answers
    QString codeBg, codeFg, codeInline, codeInlineFg;
    // Accent
    QString accent, muted, linkFg;
};

static bool aiIsDark() {
    const QString &t = Config::instance().theme;
    if (t.compare("System", Qt::CaseInsensitive) == 0) return false; // treat System-unknown as Light
    return t.compare("Dark", Qt::CaseInsensitive) == 0 ||
           t.compare("Monokai", Qt::CaseInsensitive) == 0;
}

static AiPalette aiPalette() {
    AiPalette p;
    if (aiIsDark()) {
        p.bg           = "#1E1E1E";
        p.chromeBg     = "#2D2D2D";
        p.headerFg     = "#4EC9B0";
        p.inputBg      = "#2D2D2D";
        p.inputBorder  = "#444";
        p.inputText    = "#E8E8E8";
        p.inputFocus   = "#4EC9B0";
        p.btnBg        = "#2D2D2D";
        p.btnBorder    = "#444";
        p.btnHover     = "#3D3D3D";
        p.recBtnBg     = "#8B2C2C";
        p.recBtnBorder = "#A03333";
        p.recBtnHover  = "#A03333";
        p.chatBg       = "#1E1E1E";
        p.chatFg       = "#D4D4D4";
        p.userBg       = "#0E639C";
        p.userFg       = "#FFFFFF";
        p.userBorder   = "#1177BB";
        p.userLabel    = "#B0D8E8";
        p.assistBg     = "#252526";
        p.assistFg     = "#D4D4D4";
        p.assistBorder = "#333";
        p.assistAccent = "#4EC9B0";
        p.errBg        = "#3A1F22";
        p.errFg        = "#FFD7D7";
        p.errBorder    = "#7C3232";
        p.codeBg       = "#111315";
        p.codeFg       = "#F3F7FA";
        p.codeInline   = "#1A1D20";
        p.codeInlineFg = "#F7D774";
        p.accent       = "#4EC9B0";
        p.muted        = "#888";
        p.linkFg       = "#7EC8FF";
    } else {
        // Light / Clay palette matching the rest of Notepatra on Light.
        p.bg           = "#FAF9F5";
        p.chromeBg     = "#F5F4EE";
        p.headerFg     = "#141413";
        p.inputBg      = "#FFFFFF";
        p.inputBorder  = "#D4D1C4";
        p.inputText    = "#141413";
        p.inputFocus   = "#CC785C";
        p.btnBg        = "#FFFFFF";
        p.btnBorder    = "#D4D1C4";
        p.btnHover     = "#F5F4EE";
        p.recBtnBg     = "#D84B3E";
        p.recBtnBorder = "#B8362A";
        p.recBtnHover  = "#E56A5E";
        p.chatBg       = "#FAF9F5";
        p.chatFg       = "#141413";
        p.userBg       = "#CC785C";
        p.userFg       = "#FFFFFF";
        p.userBorder   = "#B86A4E";
        p.userLabel    = "#FCE4D8";
        p.assistBg     = "#FFFFFF";
        p.assistFg     = "#141413";
        p.assistBorder = "#E5E4DF";
        p.assistAccent = "#CC785C";
        p.errBg        = "#FBCBCB";
        p.errFg        = "#7D1D1D";
        p.errBorder    = "#E89B9B";
        p.codeBg       = "#F5F4EE";
        p.codeFg       = "#141413";
        p.codeInline   = "#EDEBE2";
        p.codeInlineFg = "#9A6A20";
        p.accent       = "#CC785C";
        p.muted        = "#8E8C88";
        p.linkFg       = "#0B5BAF";
    }
    return p;
}

QString aiIconButtonStyle(const QColor &accent, bool active = false) {
    const AiPalette p = aiPalette();
    if (active) {
        return QString(
            "QPushButton { background: %1; border: 1px solid %2; border-radius: 18px; }"
            "QPushButton:hover:enabled { background: %3; border: 1px solid %2; }"
            "QPushButton:pressed:enabled { background: %2; border: 1px solid %2; }"
            "QPushButton:disabled { background: %1; border: 1px solid %2; }")
            .arg(p.recBtnBg, p.recBtnBorder, p.recBtnHover);
    }
    return QString(
        "QPushButton { background: %1; border: 1px solid %2; border-radius: 18px; }"
        "QPushButton:hover:enabled { background: %3; border: 1px solid %4; }"
        "QPushButton:pressed:enabled { background: %1; border: 1px solid %4; }"
        "QPushButton:disabled { background: %1; border: 1px solid %2; }")
        .arg(p.btnBg, p.btnBorder, p.btnHover, accent.name());
}

QIcon makePaperclipIcon(const QColor &stroke) {
    QPixmap pixmap(24, 24);
    pixmap.fill(Qt::transparent);

    QPainter painter(&pixmap);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.setPen(QPen(stroke, 2.1, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
    painter.setBrush(Qt::NoBrush);

    QPainterPath path;
    path.moveTo(14.5, 4.5);
    path.cubicTo(18.7, 4.5, 21.0, 7.2, 21.0, 11.0);
    path.lineTo(21.0, 15.8);
    path.cubicTo(21.0, 20.0, 17.8, 23.0, 13.6, 23.0);
    path.cubicTo(9.5, 23.0, 6.3, 20.0, 6.3, 15.8);
    path.lineTo(6.3, 8.8);
    path.cubicTo(6.3, 5.8, 8.7, 3.6, 11.7, 3.6);
    path.cubicTo(14.7, 3.6, 17.1, 5.9, 17.1, 8.8);
    path.lineTo(17.1, 15.0);
    path.cubicTo(17.1, 17.0, 15.5, 18.6, 13.5, 18.6);
    path.cubicTo(11.5, 18.6, 9.9, 17.0, 9.9, 15.0);
    path.lineTo(9.9, 9.5);
    painter.drawPath(path);

    return QIcon(pixmap);
}

QIcon makeMicrophoneIcon(const QColor &stroke) {
    QPixmap pixmap(24, 24);
    pixmap.fill(Qt::transparent);

    QPainter painter(&pixmap);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.setPen(QPen(stroke, 2.0, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
    painter.setBrush(Qt::NoBrush);

    painter.drawRoundedRect(QRectF(8.0, 3.5, 8.0, 10.5), 4.0, 4.0);

    QPainterPath arm;
    arm.moveTo(5.8, 11.0);
    arm.cubicTo(5.8, 15.7, 8.6, 18.6, 12.0, 18.6);
    arm.cubicTo(15.4, 18.6, 18.2, 15.7, 18.2, 11.0);
    painter.drawPath(arm);

    painter.drawLine(QPointF(12.0, 18.6), QPointF(12.0, 21.2));
    painter.drawLine(QPointF(8.5, 21.2), QPointF(15.5, 21.2));

    return QIcon(pixmap);
}

QIcon makeStopIcon(const QColor &fill) {
    QPixmap pixmap(24, 24);
    pixmap.fill(Qt::transparent);

    QPainter painter(&pixmap);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.setPen(Qt::NoPen);
    painter.setBrush(fill);
    painter.drawRoundedRect(QRectF(6.0, 6.0, 12.0, 12.0), 3.0, 3.0);

    return QIcon(pixmap);
}

QString plainTextHtml(const QString &text) {
    return text.toHtmlEscaped().replace("\n", "<br>");
}

QString markdownBodyHtml(const QString &text) {
#if QT_VERSION >= QT_VERSION_CHECK(5, 14, 0)
    QTextDocument doc;
    doc.setMarkdown(text);
    QString html = doc.toHtml();
    QRegularExpression bodyRe(
        "<body[^>]*>(.*)</body>",
        QRegularExpression::CaseInsensitiveOption | QRegularExpression::DotMatchesEverythingOption);
    QRegularExpressionMatch match = bodyRe.match(html);
    if (match.hasMatch())
        return match.captured(1);
#endif
    return plainTextHtml(text);
}

QString messageTranscriptHtml(const QVector<AIPanel::ChatMessage> &messages,
                              bool codingMode = false) {
    const AiPalette pal = aiPalette();
    // Coding Mode = monospace body + visible "CODING MODE" badge. We
    // intentionally DON'T change the background — it must stay aligned
    // with the app theme (Light = warm paper, Dark = near-black). A
    // hand-picked bg always looked out-of-place in one theme or the other.
    const QString bodyFamily = codingMode
        ? notepatraCodeCssFamily()
        : notepatraUiCssFamily();
    const QString modeBadge = codingMode
        ? QStringLiteral(
            "<div style='background:rgba(78,201,176,0.12);color:#4EC9B0;"
            "font-size:10px;font-weight:bold;letter-spacing:1.5px;"
            "padding:4px 10px;border-radius:12px;display:inline-block;"
            "margin-bottom:10px;font-family:%1;'>⌘ CODING MODE · code-only replies</div>")
              .arg(notepatraCodeCssFamily())
        : QString();
    QString html = QString(
"<html>\n"
"<head>\n"
"<style>\n"
"body { font-family: %21; line-height: 1.5; color: %3; background: %4; margin: 0; padding: 12px; }\n"
".assistant-content { font-family: %21; }\n"
"table { width: 100%%; border-collapse: collapse; margin: 10px 0; }\n"
".bubble { display: inline-block; max-width: 100%%; text-align: left; border-radius: 16px; padding: 12px 14px; }\n"
".bubble-user { background: %5; color: %6; border: 1px solid %7; }\n"
".bubble-assistant { background: %8; color: %9; border-left: 3px solid %10; border-top: 1px solid %11; border-right: 1px solid %11; border-bottom: 1px solid %11; min-width: 280px; }\n"
".bubble-error { background: %12; color: %13; border-left: 3px solid %14; border-top: 1px solid %14; border-right: 1px solid %14; border-bottom: 1px solid %14; }\n"
".user-label { font-size: 9px; color: %15; font-weight: bold; letter-spacing: 1px; margin-bottom: 6px; }\n"
".assistant-head { margin-bottom: 8px; }\n"
".assistant-model { color: %10; font-size: 10px; font-weight: bold; }\n"
".copy-btn { background: %16; color: white; font-size: 10px; font-weight: 600; padding: 2px 10px; border-radius: 10px; text-decoration: none; letter-spacing: 0.5px; }\n"
".copy-btn:hover { text-decoration: none; }\n"
".message-plain { white-space: pre-wrap; }\n"
".assistant-content { color: %9; font-size: 12px; }\n"
".assistant-content p { margin: 0 0 10px 0; }\n"
".assistant-content ul, .assistant-content ol { margin: 6px 0 10px 20px; padding-left: 12px; }\n"
".assistant-content li { margin: 3px 0; }\n"
".assistant-content h1, .assistant-content h2, .assistant-content h3, .assistant-content h4 { color: %9; margin: 12px 0 8px 0; }\n"
".assistant-content a { color: %16; }\n"
".assistant-content pre { background: %17; color: %18; border: 1px solid %11; border-radius: 10px; padding: 12px; overflow-x: auto; white-space: pre-wrap; }\n"
".assistant-content code { background: %19; color: %20; border-radius: 4px; padding: 1px 4px; font-family: %2; }\n"
".assistant-content pre code { background: transparent; padding: 0; color: inherit; }\n"
"</style>\n"
"</head>\n"
"<body>\n")
        .arg(notepatraUiCssFamily(), notepatraCodeCssFamily(),
             pal.chatFg, pal.chatBg,
             pal.userBg, pal.userFg, pal.userBorder,
             pal.assistBg, pal.assistFg, pal.assistAccent, pal.assistBorder,
             pal.errBg, pal.errFg, pal.errBorder,
             pal.userLabel, pal.linkFg,
             pal.codeBg, pal.codeFg,
             pal.codeInline, pal.codeInlineFg,
             bodyFamily);

    html += modeBadge;

    for (int i = 0; i < messages.size(); ++i) {
        const AIPanel::ChatMessage &message = messages.at(i);
        if (message.role == AIPanel::ChatMessage::User) {
            html += QString(
                "<table cellpadding='0' cellspacing='0'><tr><td width='25%%'></td><td width='75%%' align='right'>"
                "<div class='bubble bubble-user'>"
                "<div class='user-label'>You</div>"
                "<div class='message-plain'>%1</div>"
                "</div></td></tr></table>")
                .arg(plainTextHtml(message.text));
            continue;
        }

        if (message.role == AIPanel::ChatMessage::Error) {
            html += QString(
                "<table cellpadding='0' cellspacing='0'><tr><td width='75%%' align='left'>"
                "<div class='bubble bubble-error'>"
                "<div class='assistant-model'>ERROR</div>"
                "<div class='message-plain'>%1</div>"
                "</div></td><td></td></tr></table>")
                .arg(plainTextHtml(message.text));
            continue;
        }

        html += QString(
            "<table cellpadding='0' cellspacing='0'><tr><td width='75%%' align='left'>"
            "<div class='bubble bubble-assistant'>"
            "<table width='100%%' cellpadding='0' cellspacing='0' class='assistant-head'>"
            "<tr><td><span class='assistant-model'>%1</span></td>"
            "<td align='right'><a class='copy-btn' href='copy://message/%2'>⧉ Copy</a></td></tr>"
            "</table>"
            "<div class='assistant-content'>%3</div>"
            "</div></td><td></td></tr></table>")
            .arg(message.model.toHtmlEscaped(),
                 QString::number(i),
                 markdownBodyHtml(message.text));
    }

    html += QStringLiteral("</body></html>");
    return html;
}

}  // namespace

AIPanel::AIPanel(QWidget *parent) : QWidget(parent) {
    // Make the panel comfortably wide so chat bubbles render properly.
    // Like a real chat app — narrow chat looks cramped.
    setMinimumWidth(420);

    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(2);

    const AiPalette pal = aiPalette();

    // ─── TOP STRIP: Header + model selector + status ────────────────────
    auto *header = new QLabel("  AI Assistant");
    header->setFixedHeight(24);
    header->setStyleSheet(QString(
        "font-weight: bold; background: %1; color: %2; padding: 4px 8px;")
        .arg(pal.chromeBg, pal.headerFg));
    layout->addWidget(header);

    auto *modelRow = new QHBoxLayout;
    modelRow->setContentsMargins(8, 4, 8, 2);
    modelRow->setSpacing(6);

    // ─── Backend picker — quick-switch Ollama / llama.cpp / OpenRouter
    // / custom OpenAI-compat without digging into Preferences. Selecting
    // an option auto-fills the corresponding default URL into Config and
    // refreshes the model list. Gear ⚙ opens full AI prefs for URL +
    // API key editing.
    // Header labels ("Backend:", "Model:") were removed for a Cursor-style
    // cleaner look — the dropdowns self-describe and the tooltips carry any
    // extra hint. The combos speak for themselves on screen.
    auto *backendCombo = new QComboBox;
    backendCombo->addItem("Ollama",           "Ollama");
    backendCombo->addItem("llama.cpp (GGUF)", "llama.cpp");
    backendCombo->addItem("OpenRouter (cloud)", "OpenRouter");
    backendCombo->addItem("LM Studio",        "LMStudio");
    backendCombo->addItem("Jan",              "Jan");
    backendCombo->addItem("OpenAI",           "OpenAI");
    backendCombo->addItem("Custom",           "Custom");
    backendCombo->setFixedWidth(170);

    // Initialise from Config. blockSignals() prevents the
    // currentIndexChanged handler below from firing DURING construction
    // (before m_ollama exists) — which was silently eating the Ollama
    // default and breaking auto-detect on fresh launches.
    backendCombo->blockSignals(true);
    {
        const QString be = Config::instance().aiBackend;
        if (be.compare("llama.cpp", Qt::CaseInsensitive) == 0) backendCombo->setCurrentIndex(1);
        else if (Config::instance().aiBaseUrl.contains("openrouter")) backendCombo->setCurrentIndex(2);
        else if (Config::instance().aiBaseUrl.contains(":1234"))      backendCombo->setCurrentIndex(3); // LM Studio
        else if (Config::instance().aiBaseUrl.contains(":1337"))      backendCombo->setCurrentIndex(4); // Jan
        else if (Config::instance().aiBaseUrl.contains("openai.com")) backendCombo->setCurrentIndex(5);
        else if (be.startsWith("OpenAI", Qt::CaseInsensitive))        backendCombo->setCurrentIndex(6); // custom
        else backendCombo->setCurrentIndex(0);  // Ollama
    }
    backendCombo->blockSignals(false);
    connect(backendCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, [this, backendCombo](int) {
        const QString key = backendCombo->currentData().toString();
        auto &cfg = Config::instance();
        if (key == "Ollama")          { cfg.aiBackend = "Ollama";        cfg.aiBaseUrl.clear(); }
        else if (key == "llama.cpp")  { cfg.aiBackend = "llama.cpp";     cfg.aiBaseUrl.clear(); }
        else if (key == "OpenRouter") { cfg.aiBackend = "OpenAI-compat"; cfg.aiBaseUrl = "https://openrouter.ai/api/v1"; }
        else if (key == "LMStudio")   { cfg.aiBackend = "OpenAI-compat"; cfg.aiBaseUrl = "http://localhost:1234/v1"; }
        else if (key == "Jan")        { cfg.aiBackend = "OpenAI-compat"; cfg.aiBaseUrl = "http://localhost:1337/v1"; }
        else if (key == "OpenAI")     { cfg.aiBackend = "OpenAI-compat"; cfg.aiBaseUrl = "https://api.openai.com/v1"; }
        else                          { cfg.aiBackend = "OpenAI-compat"; /* leave URL as-is */ }
        cfg.save();
        // Reconfigure the client and refresh the model list
        if (m_ollama) {
            m_ollama->setBackend(OllamaClient::backendFromString(cfg.aiBackend));
            if (!cfg.aiBaseUrl.isEmpty()) m_ollama->setBaseUrl(cfg.aiBaseUrl);
            refreshModels();
        }
    });
    modelRow->addWidget(backendCombo);

    // ─── API-key inline prompt (shown only when a cloud backend is
    // selected AND Config::aiApiKey is empty). Lets users paste their
    // OpenRouter / OpenAI key right in the AI panel without digging
    // into Settings → Preferences → AI. Saves on Enter.
    auto *apiKeyHost = new QWidget;
    apiKeyHost->setVisible(false);
    auto *apiKeyRow = new QHBoxLayout(apiKeyHost);
    apiKeyRow->setContentsMargins(8, 2, 8, 4);
    apiKeyRow->setSpacing(6);
    auto *apiKeyLabel = new QLabel("API key:");
    apiKeyLabel->setStyleSheet(QString("color: %1; font-size: 11px;").arg(pal.muted));
    apiKeyRow->addWidget(apiKeyLabel);
    auto *apiKeyInput = new QLineEdit;
    apiKeyInput->setEchoMode(QLineEdit::Password);
    apiKeyInput->setPlaceholderText("Paste your API key (sk-or-v1-... for OpenRouter). Stored locally only.");
    apiKeyInput->setStyleSheet(QString(
        "QLineEdit { background: %1; color: %2; border: 1px solid %3; "
        "border-radius: 4px; padding: 4px 8px; font-size: 12px; }"
        "QLineEdit:focus { border: 1px solid %4; }")
        .arg(pal.inputBg, pal.inputText, pal.inputBorder, pal.inputFocus));
    apiKeyRow->addWidget(apiKeyInput, 1);
    auto *apiKeySaveBtn = new QPushButton("Save");
    apiKeySaveBtn->setFixedHeight(26);
    apiKeySaveBtn->setFixedWidth(60);
    apiKeySaveBtn->setStyleSheet(QString(
        "QPushButton { background: %1; color: white; border: none; "
        "border-radius: 4px; font-size: 11px; font-weight: 600; }"
        "QPushButton:hover { background: %2; }")
        .arg(pal.accent, pal.userBorder));
    apiKeyRow->addWidget(apiKeySaveBtn);

    auto showKeyRow = [apiKeyHost, apiKeyInput, backendCombo]() {
        const QString key = backendCombo->currentData().toString();
        const bool needsKey = (key == "OpenRouter" || key == "OpenAI" || key == "Custom");
        const QString existing = Config::instance().aiApiKey;
        if (needsKey && existing.isEmpty()) {
            apiKeyInput->clear();
            apiKeyInput->setPlaceholderText(key == "OpenRouter"
                ? "Paste your OpenRouter key (sk-or-v1-...) — get one at openrouter.ai/keys"
                : key == "OpenAI"
                    ? "Paste your OpenAI key (sk-...) — get one at platform.openai.com"
                    : "Paste API key (leave empty if the server doesn't require one)");
            apiKeyHost->setVisible(true);
        } else if (needsKey && !existing.isEmpty()) {
            apiKeyInput->setText(existing);
            apiKeyHost->setVisible(true);  // keep visible so users can update
        } else {
            apiKeyHost->setVisible(false);
        }
    };
    auto saveKey = [apiKeyInput]() {
        auto &cfg = Config::instance();
        cfg.aiApiKey = apiKeyInput->text().trimmed();
        cfg.save();
    };
    connect(apiKeySaveBtn, &QPushButton::clicked, this, saveKey);
    connect(apiKeyInput, &QLineEdit::returnPressed, this, saveKey);
    connect(backendCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, [showKeyRow](int) { showKeyRow(); });
    // Call once on construction to reflect current backend state
    showKeyRow();

    m_modelCombo = new QComboBox;
    m_modelCombo->setEditable(true);
    m_modelCombo->addItem("(detecting…)");
    m_modelCombo->setEnabled(false);
    modelRow->addWidget(m_modelCombo, 1);
    m_refreshBtn = new QPushButton("↻");
    m_refreshBtn->setFixedWidth(28);
    m_refreshBtn->setToolTip("Refresh model list from the selected backend");
    modelRow->addWidget(m_refreshBtn);

    // Gear ⚙ — jumps to Preferences → AI for URL / API-key editing
    auto *gearBtn = new QPushButton("⚙");
    gearBtn->setFixedWidth(28);
    gearBtn->setToolTip("AI settings — edit base URL, API key, advanced options");
    connect(gearBtn, &QPushButton::clicked, this, []() {
        // Defer open to the top-level window — it owns the menu action.
        // We can't include preferences.h here (circular), so just emit
        // a QShortcut-style broadcast via QApplication::sendEvent.
        // Simplest: users can also open Settings → Preferences.
        // TODO: wire a proper signal if needed.
    });
    gearBtn->setVisible(false);  // hidden until we wire up the jump cleanly
    modelRow->addWidget(gearBtn);
    // ─── Coding Mode toggle (Cursor / Copilot-Agent style) ─────────────
    // ON  = sharper system prompt ("return ONLY the modified code, no
    //       prose, no markdown fences") + the post-response [Apply] button
    //       turns into the primary call-to-action and drops selected text
    //       straight into the editor. Great for "refactor this function"
    //       / "rewrite this type" / "translate to Rust" tasks.
    // OFF = classic chat panel with explanations in prose form.
    // Short label keeps the header from overflowing on narrow docks.
    m_codingMode = new QCheckBox("Coding");
    m_codingMode->setChecked(false);
    m_codingMode->setStyleSheet(QString(
        "QCheckBox { font-size: 11px; color: %1; margin-left: 4px; font-weight: 600; }"
        "QCheckBox:checked { color: %2; }")
        .arg(pal.muted, pal.accent));
    m_codingMode->setToolTip(
        "Coding Mode: AI returns code only (no prose). Replaces selection "
        "directly. Auto-arranges the window as a 3-column Cursor-style "
        "layout: [FileExplorer | Editor | AI Chat]. Off = chat explanations.");
    modelRow->addWidget(m_codingMode);

    // Visible mode change when Coding Mode toggles — placeholder text,
    // CODING badge, "Show thinking" hides, "Apply Code" button becomes
    // the primary call to action. Also emits codingModeRequested() so
    // MainWindow can flip the 3-column layout (file tree + editor + AI
    // dock on right).
    connect(m_codingMode, &QCheckBox::toggled, this, [this](bool checked) {
        if (m_customInput) {
            m_customInput->setPlaceholderText(checked
                ? "Coding Mode · e.g. Refactor this function / Add types / Translate to TypeScript"
                : "Type a message and press Enter to send…");
        }
        // Collapse the panel to a Cursor/Copilot-style minimal chat when
        // Coding Mode is on: just the model header, chat transcript, and
        // input bar. Everything else (8-button quick-action grid, the
        // Insert/Replace/Copy row, the "Show thinking" toggle) hides so
        // the panel stops feeling crowded for the common coding flow.
        if (m_thinkingCheck)      m_thinkingCheck->setVisible(!checked);
        if (m_quickActionsWrap)   m_quickActionsWrap->setVisible(!checked);
        if (m_resultActionsWrap)  m_resultActionsWrap->setVisible(!checked);
        // Re-render the transcript with the new Coding-Mode CSS — flips
        // the chat to monospace + darker surface + "CODING MODE" badge.
        // Crucially, m_messages is NOT touched: switching modes preserves
        // the conversation so users don't lose context mid-thought.
        renderTranscript();
        emit codingModeRequested(checked);
    });

    m_thinkingCheck = new QCheckBox("Think");
    m_thinkingCheck->setChecked(false);
    m_thinkingCheck->setStyleSheet(QString(
        "font-size: 11px; color: %1; margin-left: 8px;").arg(pal.muted));
    m_thinkingCheck->setToolTip("Show the model's reasoning blocks (Qwen3, DeepSeek-R1). "
                                "Off = faster, cleaner answers. On = see how the model thinks.");
    modelRow->addWidget(m_thinkingCheck);
    m_clearBtn = new QPushButton("Reset");
    m_clearBtn->setObjectName("aiResetSessionButton");
    m_clearBtn->setFixedWidth(56);
    m_clearBtn->setStyleSheet("font-size: 11px;");
    m_clearBtn->setToolTip("Reset the AI Assistant session");
    modelRow->addWidget(m_clearBtn);
    layout->addLayout(modelRow);
    layout->addWidget(apiKeyHost);

    m_statusLabel = new QLabel("");
    m_statusLabel->setStyleSheet(QString(
        "color: %1; padding: 0 8px; font-size: 11px;").arg(pal.muted));
    m_statusLabel->setFixedHeight(14);
    layout->addWidget(m_statusLabel);

    // ─── MIDDLE: chat output (TAKES ALL VERTICAL SPACE) ────────────────
    // This is the conversation area. Bubbles flow from top to bottom.
    // Input is at the bottom of the panel like every real chat app.
    m_output = new QTextBrowser;
    m_output->setReadOnly(true);
    m_output->setAcceptRichText(true);
    m_output->setOpenLinks(false);
    m_output->setOpenExternalLinks(false);
    QFont mono = notepatraCodeFont();
    m_output->setFont(mono);
    m_output->setStyleSheet(QString(
        "QTextBrowser { background: %1; color: %2; border: none; padding: 12px; }")
        .arg(pal.chatBg, pal.chatFg));
    m_output->setPlaceholderText(
        "Type a message below and press Enter.\n"
        "\n"
        "Tip: tick Coding for a minimal chat layout.\n"
        "Click ▸ Quick actions below to reveal the one-click prompts.");
    layout->addWidget(m_output, 1);  // stretch=1 → takes all spare space

    // ─── BOTTOM STRIP: quick actions + input + send (like a real chat) ──
    // A thin chevron row always shows; clicking it reveals / hides the
    // 8-button quick-actions grid + the Insert/Replace/Copy row. Default
    // is hidden so the panel looks like a clean chat out of the box —
    // power users click to pin them open.
    auto *actionsToggleRow = new QHBoxLayout;
    actionsToggleRow->setContentsMargins(8, 2, 8, 0);
    actionsToggleRow->setSpacing(0);
    auto *actionsToggle = new QPushButton("▸ Quick actions");
    actionsToggle->setCheckable(true);
    actionsToggle->setCursor(Qt::PointingHandCursor);
    actionsToggle->setStyleSheet(QString(
        "QPushButton { background: transparent; color: %1; border: none; "
        "padding: 2px 4px; font-size: 11px; text-align: left; }"
        "QPushButton:hover { color: %2; }"
        "QPushButton:checked { color: %2; }").arg(pal.muted, pal.accent));
    actionsToggle->setToolTip("Show/hide the 8 one-click prompt buttons (Explain · Find Bugs · Refactor · …) "
                              "and the Insert / Replace / Copy apply row.");
    actionsToggleRow->addWidget(actionsToggle);
    actionsToggleRow->addStretch();
    layout->addLayout(actionsToggleRow);

    m_quickActionsWrap = new QWidget;
    m_quickActionsWrap->setVisible(false);  // default hidden — chevron reveals
    auto *quickWrapV = new QVBoxLayout(m_quickActionsWrap);
    quickWrapV->setContentsMargins(0, 0, 0, 0);
    quickWrapV->setSpacing(0);

    auto *actionsRow1 = new QHBoxLayout;
    actionsRow1->setContentsMargins(8, 4, 8, 0);
    actionsRow1->setSpacing(4);
    auto *explainBtn = new QPushButton("Explain");
    auto *fixBugsBtn = new QPushButton("Find Bugs");
    auto *refactorBtn = new QPushButton("Refactor");
    auto *testsBtn = new QPushButton("Write Tests");
    for (auto *b : {explainBtn, fixBugsBtn, refactorBtn, testsBtn}) {
        b->setFixedHeight(24);
        b->setStyleSheet("font-size: 11px; padding: 0 8px;");
        actionsRow1->addWidget(b);
    }
    quickWrapV->addLayout(actionsRow1);

    auto *actionsRow2 = new QHBoxLayout;
    actionsRow2->setContentsMargins(8, 0, 8, 4);
    actionsRow2->setSpacing(4);
    auto *commentBtn = new QPushButton("Add Comments");
    auto *docBtn = new QPushButton("Generate Docs");
    auto *optimizeBtn = new QPushButton("Optimize");
    auto *translateBtn = new QPushButton("Translate");
    for (auto *b : {commentBtn, docBtn, optimizeBtn, translateBtn}) {
        b->setFixedHeight(24);
        b->setStyleSheet("font-size: 11px; padding: 0 8px;");
        actionsRow2->addWidget(b);
    }
    quickWrapV->addLayout(actionsRow2);
    layout->addWidget(m_quickActionsWrap);

    // Insert/Replace/Copy mini row — also hidden by default, revealed
    // along with the quick-action grid by the same chevron toggle above.
    m_resultActionsWrap = new QWidget;
    m_resultActionsWrap->setVisible(false);
    auto *resultRow = new QHBoxLayout(m_resultActionsWrap);
    resultRow->setContentsMargins(8, 0, 8, 2);
    resultRow->setSpacing(4);
    auto *insertBtn = new QPushButton("Insert at Cursor");
    auto *replaceBtn = new QPushButton("Replace Selection");
    auto *copyBtn = new QPushButton("Copy");
    for (auto *b : {insertBtn, replaceBtn, copyBtn}) {
        b->setFixedHeight(22);
        b->setStyleSheet("font-size: 10px; color: #888; padding: 0 8px;");
        resultRow->addWidget(b);
    }
    resultRow->addStretch();
    layout->addWidget(m_resultActionsWrap);

    // Wire the chevron: ▸ → ▾ + reveal both wraps; reverse on uncheck.
    // Coding Mode's own handler still force-hides them; this toggle is
    // the user-controlled reveal when Coding Mode is off.
    connect(actionsToggle, &QPushButton::toggled, this, [this, actionsToggle](bool open) {
        actionsToggle->setText(open ? "▾ Quick actions" : "▸ Quick actions");
        // Never unhide these when Coding Mode is on — Coding's whole point
        // is the minimal chat view. The chevron becomes a no-op visual
        // cue in that case.
        const bool coding = m_codingMode && m_codingMode->isChecked();
        if (m_quickActionsWrap)  m_quickActionsWrap->setVisible(open && !coding);
        if (m_resultActionsWrap) m_resultActionsWrap->setVisible(open && !coding);
    });

    // ─── ATTACHMENT CHIP (shown above input when a file is attached) ────
    m_attachmentChip = new QLabel("");
    m_attachmentChip->setStyleSheet(QString(
        "background: %1; color: %2; border-radius: 10px; "
        "padding: 4px 10px; margin: 0 8px; font-size: 11px;")
        .arg(pal.chromeBg, pal.accent));
    m_attachmentChip->setFixedHeight(0);  // hidden until something attached
    m_attachmentChip->setVisible(false);
    layout->addWidget(m_attachmentChip);

    // ─── INPUT BAR AT THE BOTTOM (like every real chat / SMS app) ───────
    auto *customRow = new QHBoxLayout;
    customRow->setContentsMargins(8, 6, 8, 8);
    customRow->setSpacing(6);

    // Attach button — accepts any file (images, PDF, DOCX, PPTX, text, code…)
    m_attachBtn = new QPushButton;
    m_attachBtn->setFixedSize(36, 36);
    m_attachBtn->setIcon(makePaperclipIcon(QColor("#D6DDD9")));
    m_attachBtn->setIconSize(QSize(20, 20));
    m_attachBtn->setText("");
    m_attachBtn->setStyleSheet(aiIconButtonStyle(QColor("#4EC9B0")));
    m_attachBtn->setToolTip("Attach image, PDF, DOCX, PPTX, code, or any text file as context");
    m_attachBtn->setAccessibleName("Attach file");
    customRow->addWidget(m_attachBtn);

    m_voiceBtn = new QPushButton;
    m_voiceBtn->setFixedSize(36, 36);
    m_voiceBtn->setAccessibleName("Speech to text");
    updateVoiceButtonVisual(false);
    customRow->addWidget(m_voiceBtn);

    m_customInput = new QLineEdit;
    m_customInput->setPlaceholderText("Type a message and press Enter to send...");
    m_customInput->setStyleSheet(QString(
        "QLineEdit { background: %1; color: %2; border: 1px solid %3; "
        "border-radius: 18px; padding: 8px 16px; font-size: 13px; }"
        "QLineEdit:focus { border: 1px solid %4; }")
        .arg(pal.inputBg, pal.inputText, pal.inputBorder, pal.inputFocus));
    m_customInput->setFixedHeight(36);
    customRow->addWidget(m_customInput, 1);

    auto *sendBtn = new QPushButton("Send");
    sendBtn->setFixedSize(72, 36);
    sendBtn->setStyleSheet(QString(
        "QPushButton { background: %1; color: white; border: none; "
        "border-radius: 18px; font-weight: bold; font-size: 13px; }"
        "QPushButton:hover { background: %2; }"
        "QPushButton:pressed { background: %3; }")
        .arg(pal.userBg, pal.userBorder,
             aiIsDark() ? "#0A4F7C" : "#A55B40"));
    customRow->addWidget(sendBtn);

    m_stopBtn = new QPushButton("Stop");
    m_stopBtn->setFixedSize(56, 36);
    m_stopBtn->setEnabled(false);
    m_stopBtn->setStyleSheet(QString(
        "QPushButton { background: %1; color: %2; border: 1px solid %3; border-radius: 18px; font-size: 12px; }"
        "QPushButton:enabled { background: %4; color: white; border: 1px solid %5; }"
        "QPushButton:hover:enabled { background: %5; }")
        .arg(pal.btnBg, pal.muted, pal.btnBorder,
             pal.recBtnBg, pal.recBtnBorder));
    customRow->addWidget(m_stopBtn);
    layout->addLayout(customRow);

    connect(m_output, &QTextBrowser::anchorClicked, this, &AIPanel::handleChatLink);

    // Wire attach button
    connect(m_attachBtn, &QPushButton::clicked, this, &AIPanel::attachFile);
    connect(m_voiceBtn, &QPushButton::clicked, this, &AIPanel::toggleSpeechToText);

    // Ollama client
    m_ollama = new OllamaClient(this);

    connect(m_ollama, &OllamaClient::tokenReceived, this, [this](const QString &token) {
        streamIntoAssistantBubble(token);
    });
    connect(m_ollama, &OllamaClient::finished, this, [this](const QString &response) {
        m_lastResponse = response;
        m_stopBtn->setEnabled(false);
        endAssistantBubble();
    });
    connect(m_ollama, &OllamaClient::error, this, [this](const QString &msg) {
        endAssistantBubble();
        appendErrorBubble(msg);
        m_stopBtn->setEnabled(false);
    });
    connect(m_clearBtn, &QPushButton::clicked, this, &AIPanel::clearChat);

    // Dynamic model detection
    connect(m_ollama, &OllamaClient::modelsListed, this, [this](const QStringList &models) {
        QString prev = m_modelCombo->currentText();
        m_modelCombo->clear();
        if (models.isEmpty()) {
            m_modelCombo->addItem("(no models installed)");
            m_modelCombo->setEnabled(false);
            setStatus("Ollama running but no models. Pull a small one: "
                      "ollama pull qwen2.5-coder:3b", true);
        } else {
            m_modelCombo->addItems(models);
            m_modelCombo->setEnabled(true);
            // Auto-pick the most CPU-friendly small model on first run.
            // Priority: qwen2.5-coder:3b > qwen2.5:3b > gemma2:2b > llama3.2:3b
            // > gemma3:4b > qwen2.5:7b > anything else. This matters on CPU-
            // only / 16 GB RAM laptops where a 7B+ model will swap to disk.
            auto pickPreferred = [&models]() -> int {
                const QStringList priority = {
                    "qwen2.5-coder:3b", "qwen2.5-coder:1.5b",
                    "qwen2.5:3b", "qwen2.5:1.5b",
                    "gemma2:2b", "gemma3:4b", "gemma3:2b",
                    "llama3.2:3b", "llama3.2:1b",
                    "phi3.5:3.8b", "phi3:mini",
                    "qwen2.5-coder:7b", "qwen2.5:7b",
                    "codellama:7b", "mistral:7b"
                };
                for (const QString &p : priority) {
                    int idx = models.indexOf(p);
                    if (idx >= 0) return idx;
                }
                return 0;
            };
            int idx = m_modelCombo->findText(prev);
            if (idx < 0) idx = pickPreferred();
            m_modelCombo->setCurrentIndex(idx);
            setStatus(QString("Ollama: %1 model%2 detected · using %3")
                      .arg(models.size())
                      .arg(models.size() == 1 ? "" : "s")
                      .arg(m_modelCombo->currentText()), false);
        }
    });
    connect(m_ollama, &OllamaClient::modelsError, this, [this](const QString &reason) {
        m_modelCombo->clear();
        m_modelCombo->addItem("(Ollama offline)");
        m_modelCombo->setEnabled(false);
        setStatus(reason, true);
    });
    connect(m_refreshBtn, &QPushButton::clicked, this, &AIPanel::refreshModels);

    // Kick off initial detection
    QTimer::singleShot(100, this, &AIPanel::refreshModels);

    // Connect buttons
    connect(explainBtn, &QPushButton::clicked, this, [this]() { sendPrompt("explain"); });
    connect(fixBugsBtn, &QPushButton::clicked, this, [this]() { sendPrompt("bugs"); });
    connect(refactorBtn, &QPushButton::clicked, this, [this]() { sendPrompt("refactor"); });
    connect(testsBtn, &QPushButton::clicked, this, [this]() { sendPrompt("tests"); });
    connect(commentBtn, &QPushButton::clicked, this, [this]() { sendPrompt("comment"); });
    connect(docBtn, &QPushButton::clicked, this, [this]() { sendPrompt("docs"); });
    connect(optimizeBtn, &QPushButton::clicked, this, [this]() { sendPrompt("optimize"); });
    connect(translateBtn, &QPushButton::clicked, this, [this]() { sendPrompt("translate"); });
    connect(sendBtn, &QPushButton::clicked, this, [this]() {
        if (!m_customInput->text().isEmpty()) sendPrompt("custom");
    });
    connect(m_customInput, &QLineEdit::returnPressed, sendBtn, &QPushButton::click);
    connect(m_stopBtn, &QPushButton::clicked, m_ollama, &OllamaClient::cancel);
    connect(insertBtn, &QPushButton::clicked, this, [this]() {
        if (!m_lastResponse.isEmpty()) emit insertText(m_lastResponse);
    });
    connect(replaceBtn, &QPushButton::clicked, this, [this]() {
        if (!m_lastResponse.isEmpty()) emit replaceSelection(m_lastResponse);
    });
    connect(copyBtn, &QPushButton::clicked, this, [this]() {
        if (!m_lastResponse.isEmpty()) QApplication::clipboard()->setText(m_lastResponse);
    });
}

void AIPanel::setContext(const QString &selectedText, const QString &filePath, const QString &language) {
    // Legacy 3-arg path — keep working for callers that haven't moved to
    // setWorkspaceContext yet. We treat the passed selected text as both
    // the selection context AND the "current file text" so older call
    // sites still produce the same prompt as before.
    m_context = selectedText;
    m_language = language;
    m_currentFilePath = filePath;
    m_currentFileText = selectedText;
    m_openTabs.clear();
    m_workspaceRoot.clear();
}

void AIPanel::setWorkspaceContext(const QString &selectedText,
                                  const QString &currentFilePath,
                                  const QString &language,
                                  const QString &currentFileText,
                                  const QVector<OpenTabInfo> &openTabs,
                                  const QString &workspaceRoot,
                                  const QStringList &workspaceFilePaths) {
    // m_context is what the quick-action templates wrap in triple backticks
    // (Explain / Refactor / …). Prefer the live selection; otherwise fall
    // back to the whole current file so those actions still have something
    // to chew on.
    m_context = selectedText.isEmpty() ? currentFileText : selectedText;
    m_language = language;
    m_currentFilePath = currentFilePath;
    m_currentFileText = currentFileText;
    m_openTabs = openTabs;
    m_workspaceRoot = workspaceRoot;
    m_workspaceFilePaths = workspaceFilePaths;
}

void AIPanel::refreshModels() {
    setStatus("Detecting Ollama models...", false);
    m_ollama->listModels();
}

void AIPanel::setStatus(const QString &text, bool isError) {
    m_statusLabel->setText(text);
    m_statusLabel->setStyleSheet(isError
        ? "color: #F48771; padding: 0 6px;"
        : "color: #4EC9B0; padding: 0 6px;");
}

static QString findFirstExecutable(const QStringList &candidates) {
    for (const QString &candidate : candidates) {
        const QString path = QStandardPaths::findExecutable(candidate);
        if (!path.isEmpty()) return path;
    }
    return QString();
}

// Read a file and return its content suitable for embedding in a chat
// prompt. For images, returns the base64 of the image (with imageOut set).
// For text-like files, returns plain text. For PDF/DOCX/PPTX/XLSX, tries
// to extract text using system tools (pdftotext, unzip+grep). Returns
// empty string + sets reasonOut on failure.
static QString extractFileContent(const QString &path, const QString &kind,
                                  QString &imageBase64Out, QString &reasonOut) {
    imageBase64Out.clear();
    reasonOut.clear();
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) {
        reasonOut = "could not open file";
        return QString();
    }

    if (kind == "image") {
        // Load as QImage, re-encode as PNG (predictable format), base64
        QImage img;
        if (!img.loadFromData(f.readAll())) {
            reasonOut = "image format not recognised by Qt";
            return QString();
        }
        // Downscale very large images so we don't blow Ollama's context
        if (img.width() > 1280 || img.height() > 1280) {
            img = img.scaled(1280, 1280, Qt::KeepAspectRatio, Qt::SmoothTransformation);
        }
        QByteArray bytes;
        QBuffer buf(&bytes);
        buf.open(QIODevice::WriteOnly);
        img.save(&buf, "PNG");
        imageBase64Out = QString::fromLatin1(bytes.toBase64());
        return QString();   // image goes via images field, not the prompt
    }

    if (kind == "text") {
        QByteArray data = f.readAll();
        // Cap at 100 KB to avoid blowing the context window
        if (data.size() > 100 * 1024) {
            reasonOut = QString("file is %1 KB — truncated to 100 KB").arg(data.size() / 1024);
            data = data.left(100 * 1024);
        }
        return QString::fromUtf8(data);
    }

    if (kind == "pdf") {
        f.close();
        // pdftotext is part of poppler-utils, usually preinstalled on Linux
        QProcess p;
        p.start("pdftotext", {"-layout", path, "-"});
        if (!p.waitForStarted(1500)) {
            reasonOut = "pdftotext not installed (apt install poppler-utils)";
            return QString();
        }
        p.waitForFinished(15000);
        QByteArray text = p.readAllStandardOutput();
        if (text.isEmpty()) {
            reasonOut = "PDF text extraction returned empty (scanned PDF?)";
            return QString();
        }
        if (text.size() > 100 * 1024) text = text.left(100 * 1024);
        return QString::fromUtf8(text);
    }

    if (kind == "docx" || kind == "pptx" || kind == "xlsx") {
        f.close();
        // Office files are zip archives — extract the main XML and strip tags
        QString innerPath;
        if (kind == "docx") innerPath = "word/document.xml";
        else if (kind == "pptx") innerPath = "ppt/slides/slide1.xml";  // simple: first slide only
        else innerPath = "xl/sharedStrings.xml";
        QProcess p;
        p.start("unzip", {"-p", path, innerPath});
        if (!p.waitForStarted(1500)) {
            reasonOut = "unzip not installed (apt install unzip)";
            return QString();
        }
        p.waitForFinished(15000);
        QByteArray xml = p.readAllStandardOutput();
        if (xml.isEmpty()) {
            reasonOut = QString("could not extract %1 from %2").arg(innerPath, path);
            return QString();
        }
        // Strip XML tags crudely — good enough for prompt context
        QString text = QString::fromUtf8(xml);
        text.replace(QRegularExpression("<[^>]+>"), " ");
        text.replace(QRegularExpression("\\s+"), " ");
        text = text.trimmed();
        if (text.size() > 100 * 1024) text = text.left(100 * 1024);
        return text;
    }

    reasonOut = "unsupported file kind: " + kind;
    return QString();
}

bool AIPanel::eventFilter(QObject *obj, QEvent *evt) {
    if (obj == m_attachmentChip && evt->type() == QEvent::MouseButtonPress) {
        // Click on the chip → clear the attachment
        m_pendingFilePath.clear();
        m_pendingFileKind.clear();
        m_attachmentChip->setText("");
        m_attachmentChip->setVisible(false);
        m_attachmentChip->setFixedHeight(0);
        setStatus("Attachment removed", false);
        return true;
    }
    return QWidget::eventFilter(obj, evt);
}

void AIPanel::toggleSpeechToText() {
    if (m_transcribeProcess) {
        setStatus("Speech-to-text is still transcribing the last recording", true);
        return;
    }

    if (m_recordProcess) {
        setStatus("Stopping microphone capture…", false);
        m_recordProcess->terminate();
        return;
    }

    const QString recorder = findFirstExecutable({"arecord"});
    if (recorder.isEmpty()) {
        setStatus("STT unavailable: arecord not installed on this system", true);
        return;
    }

    const QString whisper = findFirstExecutable({"whisper"});
    if (whisper.isEmpty()) {
        setStatus("STT unavailable: install local whisper CLI first", true);
        return;
    }

    const QString tempDir = QStandardPaths::writableLocation(QStandardPaths::TempLocation);
    if (tempDir.isEmpty()) {
        setStatus("STT unavailable: no writable temp directory", true);
        return;
    }

    m_recordedAudioPath = tempDir + "/notepatra-stt-" +
        QDateTime::currentDateTimeUtc().toString("yyyyMMdd-hhmmsszzz") + ".wav";

    auto *process = new QProcess(this);
    m_recordProcess = process;
    connect(process, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
            this, [this, process](int exitCode, QProcess::ExitStatus) {
                handleRecordFinished(exitCode, process);
            });

    process->start(recorder, {"-q", "-f", "S16_LE", "-r", "16000", "-c", "1", m_recordedAudioPath});
    if (!process->waitForStarted(1500)) {
        setStatus("STT unavailable: could not start microphone recorder", true);
        process->deleteLater();
        m_recordProcess = nullptr;
        return;
    }

    updateVoiceButtonVisual(true);
    setStatus("Recording… click Stop to transcribe with local whisper", false);
}

void AIPanel::handleRecordFinished(int exitCode, QProcess *process) {
    Q_UNUSED(exitCode);

    if (process != m_recordProcess) {
        if (process) process->deleteLater();
        return;
    }

    m_recordProcess = nullptr;
    process->deleteLater();

    updateVoiceButtonVisual(false);

    QFileInfo info(m_recordedAudioPath);
    if (!info.exists() || info.size() < 512) {
        setStatus("STT recording failed or was too short", true);
        return;
    }

    startTranscription(m_recordedAudioPath);
}

void AIPanel::startTranscription(const QString &audioPath) {
    const QString whisper = findFirstExecutable({"whisper"});
    if (whisper.isEmpty()) {
        setStatus("STT unavailable: install local whisper CLI first", true);
        return;
    }

    const QFileInfo info(audioPath);
    const QString outDir = info.absolutePath();

    auto *process = new QProcess(this);
    m_transcribeProcess = process;
    m_voiceBtn->setEnabled(false);
    setStatus("Transcribing speech locally…", false);

    connect(process, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
            this, [this, process, audioPath](int exitCode, QProcess::ExitStatus) {
                handleTranscriptionFinished(exitCode, process, audioPath);
            });

    process->start(whisper, {
        audioPath,
        "--model", "base",
        "--task", "transcribe",
        "--fp16", "False",
        "--output_format", "txt",
        "--output_dir", outDir
    });

    if (!process->waitForStarted(1500)) {
        setStatus("STT unavailable: could not start whisper CLI", true);
        m_voiceBtn->setEnabled(true);
        process->deleteLater();
        m_transcribeProcess = nullptr;
    }
}

void AIPanel::handleTranscriptionFinished(int exitCode, QProcess *process, const QString &audioPath) {
    if (process != m_transcribeProcess) {
        if (process) process->deleteLater();
        return;
    }

    m_transcribeProcess = nullptr;
    m_voiceBtn->setEnabled(true);
    process->deleteLater();

    const QFileInfo info(audioPath);
    const QString transcriptPath = info.absolutePath() + "/" + info.completeBaseName() + ".txt";
    QFile transcriptFile(transcriptPath);
    if (exitCode != 0 || !transcriptFile.open(QIODevice::ReadOnly | QIODevice::Text)) {
        setStatus("STT transcription failed — whisper CLI did not return a transcript", true);
        return;
    }

    QString transcript = QString::fromUtf8(transcriptFile.readAll()).trimmed();
    if (transcript.isEmpty()) {
        setStatus("STT transcription returned empty text", true);
        return;
    }

    if (!m_customInput->text().trimmed().isEmpty())
        m_customInput->setText(m_customInput->text().trimmed() + " " + transcript);
    else
        m_customInput->setText(transcript);

    m_customInput->setFocus();
    setStatus("✓ Speech transcribed into the AI prompt box", false);
}

// Thin forwarder — the real implementation now lives in ai_context.cpp so
// it's testable without pulling in QtWidgets. Keeps the AIPanel::-scoped
// API stable for any existing caller that might reach in.
QString AIPanel::buildWorkspaceContextBlock(const QString &currentFilePath,
                                            const QString &currentFileText,
                                            const QVector<AIPanel::OpenTabInfo> &openTabs,
                                            const QString &workspaceRoot) {
    QVector<AiContext::OpenTabInfo> tabs;
    tabs.reserve(openTabs.size());
    for (const auto &t : openTabs) {
        AiContext::OpenTabInfo n;
        n.filePath = t.filePath;
        n.displayName = t.displayName;
        n.language = t.language;
        n.text = t.text;
        n.isCurrent = t.isCurrent;
        tabs.append(n);
    }
    return AiContext::buildWorkspaceContextBlock(
        currentFilePath, currentFileText, tabs, workspaceRoot);
}

void AIPanel::sendPrompt(const QString &action) {
    // Pull the freshest workspace state right before we send. The provider
    // lets MainWindow push the current editor text + open-tab list without
    // us having to subscribe to every edit signal.
    if (m_contextProvider) m_contextProvider(this);

    QString model = m_modelCombo->currentText();
    if (model.startsWith("(") || !m_modelCombo->isEnabled()) {
        clearChat();
        setStatus("No Ollama model selected", true);
        appendErrorBubble(
            "No Ollama model selected.\n\n"
            "1. Install Ollama: https://ollama.com\n"
            "2. Start it: ollama serve\n"
            "3. Pull a model. On CPU-only / 16 GB RAM laptops, small\n"
            "   models run fast and don't swap. Pick one:\n\n"
            "     ollama pull qwen2.5-coder:3b   (~2 GB, best for code)\n"
            "     ollama pull qwen2.5:3b         (~2 GB, general)\n"
            "     ollama pull gemma2:2b          (~1.6 GB, smallest)\n"
            "     ollama pull gemma3:4b          (~3 GB, newer)\n"
            "     ollama pull llama3.2:3b        (~2 GB, Meta)\n\n"
            "4. Click the refresh button above");
        return;
    }
    m_ollama->setModel(model);

    // Coding Mode swaps in a sharper system prompt that forces code-only
    // output. This makes the [Apply] button after the response do exactly
    // what users expect: replace the selection with clean, compilable code
    // rather than chat-style explanations wrapped in markdown.
    QString systemPrompt;
    if (m_codingMode && m_codingMode->isChecked()) {
        systemPrompt =
            "You are a code-editor agent. The user is working in " + m_language + ". "
            "Return ONLY the modified source code. No explanations, no prose, "
            "no preambles like 'Here is the code'. Do NOT wrap the output in "
            "markdown code fences (```). Preserve the original indentation "
            "style (tabs vs spaces). Output must be directly pasteable into "
            "the file — nothing else.";
    } else {
        systemPrompt = "You are a code assistant. Be concise. Output only code when asked to modify code. "
                       "The user is working in " + m_language + ".";
    }

    // Build the prompt + the user-visible prompt label (just the action name
    // + the code snippet for context — no need to dump the verbose template
    // text into the user bubble)
    QString prompt;
    QString userBubbleText;
    if (action == "explain") {
        prompt = "Explain this code clearly and concisely:\n\n```\n" + m_context + "\n```";
        userBubbleText = "Explain this code:\n\n" + m_context;
    } else if (action == "bugs") {
        prompt = "Find bugs and potential issues in this code. List each bug with a fix:\n\n```\n" + m_context + "\n```";
        userBubbleText = "Find bugs in:\n\n" + m_context;
    } else if (action == "refactor") {
        prompt = "Refactor this code to be cleaner and more readable. Output only the refactored code:\n\n```\n" + m_context + "\n```";
        userBubbleText = "Refactor:\n\n" + m_context;
    } else if (action == "tests") {
        prompt = "Write unit tests for this code:\n\n```\n" + m_context + "\n```";
        userBubbleText = "Write unit tests for:\n\n" + m_context;
    } else if (action == "comment") {
        prompt = "Add clear comments to this code. Output the code with comments:\n\n```\n" + m_context + "\n```";
        userBubbleText = "Add comments to:\n\n" + m_context;
    } else if (action == "docs") {
        prompt = "Generate documentation (docstrings/JSDoc/etc) for this code. Output the code with docs:\n\n```\n" + m_context + "\n```";
        userBubbleText = "Generate docs for:\n\n" + m_context;
    } else if (action == "optimize") {
        prompt = "Optimize this code for performance. Explain what you changed and output the optimized code:\n\n```\n" + m_context + "\n```";
        userBubbleText = "Optimize:\n\n" + m_context;
    } else if (action == "translate") {
        prompt = "Translate this code to Python (if not already Python) or to JavaScript (if already Python). Output only the translated code:\n\n```\n" + m_context + "\n```";
        userBubbleText = "Translate Python ↔ JavaScript:\n\n" + m_context;
    } else if (action == "custom") {
        prompt = m_customInput->text() + "\n\n```\n" + m_context + "\n```";
        userBubbleText = m_customInput->text() +
            (m_context.isEmpty() ? "" : "\n\n" + m_context);
        m_customInput->clear();
    }

    // ─── Resolve attached file (if any) → image base64 OR appended text ──
    QStringList imagesBase64;
    if (!m_pendingFilePath.isEmpty()) {
        QString imageB64;
        QString reason;
        QString fileText = extractFileContent(m_pendingFilePath, m_pendingFileKind, imageB64, reason);
        if (!imageB64.isEmpty()) {
            // Vision-model image attachment
            imagesBase64 << imageB64;
            QFileInfo fi(m_pendingFilePath);
            userBubbleText = QString("[🖼 %1]\n%2").arg(fi.fileName()).arg(userBubbleText);
        } else if (!fileText.isEmpty()) {
            // Text-extracted attachment — embed in the prompt as context
            QFileInfo fi(m_pendingFilePath);
            QString header = QString("\n\n--- Attached file: %1 ---\n").arg(fi.fileName());
            prompt = prompt + header + fileText + "\n--- end file ---\n";
            userBubbleText = QString("[📄 %1]\n%2").arg(fi.fileName()).arg(userBubbleText);
        } else if (!reason.isEmpty()) {
            setStatus("✗ attachment error: " + reason, true);
        }

        // Clear the attachment after sending so the next message is fresh
        m_pendingFilePath.clear();
        m_pendingFileKind.clear();
        m_attachmentChip->setVisible(false);
        m_attachmentChip->setFixedHeight(0);
    }

    // Prepend the workspace-awareness block so the model sees the current
    // file + open tabs + workspace root before the actual question. We only
    // add it when there's meaningful context — plain chat without any file
    // open still produces a clean, header-free prompt.
    // Convert our OpenTabInfo to the namespace-level one, then assemble
    // the workspace block — now with a full "@codebase" file-tree listing
    // when the workspace root is known, so the model can reference files
    // the user hasn't opened yet.
    QVector<AiContext::OpenTabInfo> acTabs;
    acTabs.reserve(m_openTabs.size());
    for (const auto &t : m_openTabs) {
        AiContext::OpenTabInfo n;
        n.filePath = t.filePath;
        n.displayName = t.displayName;
        n.language = t.language;
        n.text = t.text;
        n.isCurrent = t.isCurrent;
        acTabs.append(n);
    }
    QString workspaceBlock = AiContext::buildWorkspaceContextBlockWithTree(
        m_currentFilePath, m_currentFileText, acTabs,
        m_workspaceRoot, m_workspaceFilePaths);
    if (!workspaceBlock.isEmpty()) {
        prompt = workspaceBlock + "\n---\n\n" + prompt;
    }

    // Opt-in debug sink for end-to-end verification of the context pipeline.
    // Enable with NOTEPATRA_AI_DEBUG=1 to dump the exact prompt going to the
    // model. Never left on by default — writes nothing when the env is unset.
    if (qEnvironmentVariableIntValue("NOTEPATRA_AI_DEBUG") == 1) {
        QFile f("/tmp/notepatra-ai-prompt.log");
        if (f.open(QIODevice::WriteOnly | QIODevice::Append)) {
            f.write(("=== " + QDateTime::currentDateTime().toString(Qt::ISODate)
                     + " ===\n").toUtf8());
            f.write("[SYSTEM]\n"); f.write(systemPrompt.toUtf8()); f.write("\n");
            f.write("[USER]\n");   f.write(prompt.toUtf8());       f.write("\n\n");
            f.close();
        }
    }

    appendUserBubble(userBubbleText);
    beginAssistantBubble();
    m_stopBtn->setEnabled(true);
    m_ollama->generate(prompt, systemPrompt, m_thinkingCheck->isChecked(), imagesBase64);
}

// ───── Chat-bubble rendering ──────────────────────────────────────────

void AIPanel::clearChat() {
    m_ollama->cancel();
    m_stopBtn->setEnabled(false);

    if (m_recordProcess) {
        QProcess *recordProcess = m_recordProcess;
        m_recordProcess = nullptr;
        disconnect(recordProcess, nullptr, this, nullptr);
        recordProcess->terminate();
        if (!recordProcess->waitForFinished(300))
            recordProcess->kill();
        recordProcess->deleteLater();
    }

    if (m_transcribeProcess) {
        QProcess *transcribeProcess = m_transcribeProcess;
        m_transcribeProcess = nullptr;
        disconnect(transcribeProcess, nullptr, this, nullptr);
        transcribeProcess->terminate();
        if (!transcribeProcess->waitForFinished(300))
            transcribeProcess->kill();
        transcribeProcess->deleteLater();
    }

    m_voiceBtn->setEnabled(true);
    updateVoiceButtonVisual(false);

    m_output->clear();
    m_currentAssistantText.clear();
    m_inAssistantBubble = false;
    m_lastResponse.clear();
    m_customInput->clear();
    m_pendingFilePath.clear();
    m_pendingFileKind.clear();
    m_attachmentChip->setText("");
    m_attachmentChip->setVisible(false);
    m_attachmentChip->setFixedHeight(0);

    if (!m_recordedAudioPath.isEmpty()) {
        QFileInfo audioInfo(m_recordedAudioPath);
        QFile::remove(m_recordedAudioPath);
        QFile::remove(audioInfo.absolutePath() + "/" + audioInfo.completeBaseName() + ".txt");
        m_recordedAudioPath.clear();
    }

    m_messages.clear();
    setStatus("AI Assistant session reset", false);
}

void AIPanel::updateVoiceButtonVisual(bool recording) {
    if (!m_voiceBtn) return;

    if (recording) {
        m_voiceBtn->setIcon(makeStopIcon(Qt::white));
        m_voiceBtn->setIconSize(QSize(18, 18));
        m_voiceBtn->setToolTip("Stop recording and transcribe the captured speech locally");
        m_voiceBtn->setStyleSheet(aiIconButtonStyle(QColor("#C54A4A"), true));
    } else {
        m_voiceBtn->setIcon(makeMicrophoneIcon(QColor("#F2C14E")));
        m_voiceBtn->setIconSize(QSize(18, 18));
        m_voiceBtn->setToolTip("Record speech and transcribe it locally with whisper CLI");
        m_voiceBtn->setStyleSheet(aiIconButtonStyle(QColor("#F2C14E")));
    }
    m_voiceBtn->setText("");
}

void AIPanel::renderTranscript() {
    const bool coding = m_codingMode && m_codingMode->isChecked();
    m_output->setHtml(messageTranscriptHtml(m_messages, coding));
    m_output->moveCursor(QTextCursor::End);
    m_output->ensureCursorVisible();
    m_output->verticalScrollBar()->setValue(m_output->verticalScrollBar()->maximum());
}

void AIPanel::appendErrorBubble(const QString &text) {
    ChatMessage message;
    message.role = ChatMessage::Error;
    message.text = text;
    m_messages.push_back(message);
    renderTranscript();
}

void AIPanel::handleChatLink(const QUrl &url) {
    if (url.scheme() == "copy") {
        const QString path = url.path();
        bool ok = false;
        const int index = path.section('/', -1).toInt(&ok);
        if (ok && index >= 0 && index < m_messages.size()) {
            QApplication::clipboard()->setText(m_messages.at(index).text);
            setStatus("Assistant reply copied", false);
        }
        return;
    }

    QDesktopServices::openUrl(url);
}

void AIPanel::appendUserBubble(const QString &text) {
    ChatMessage message;
    message.role = ChatMessage::User;
    message.text = text;
    m_messages.push_back(message);
    renderTranscript();
}

void AIPanel::beginAssistantBubble() {
    // Left-aligned gray bubble for assistant responses. Streamed tokens get
    // appended via streamIntoAssistantBubble(). The bubble closes in
    // endAssistantBubble().
    m_currentAssistantText.clear();
    m_inAssistantBubble = true;

    QString header = QString(
        "<table width='100%' cellpadding='0' cellspacing='0' style='margin:8px 0;'>"
        "<tr><td width='65%%' align='left'>"
        "<div style='background:#2D2D2D;color:#D4D4D4;padding:10px 14px;"
        "border-radius:12px;display:inline-block;text-align:left;"
        "max-width:100%%;font-family:%2;font-size:12px;"
        "border-left:3px solid #4EC9B0;'>"
        "<div style='font-size:10px;color:#4EC9B0;font-weight:bold;margin-bottom:4px;'>%1</div>"
        "<span style='white-space:pre-wrap;'>"
    ).arg(m_ollama->model().toHtmlEscaped(), notepatraCodeCssFamily());

    m_output->moveCursor(QTextCursor::End);
    m_output->insertHtml(header);
    m_output->moveCursor(QTextCursor::End);
}

void AIPanel::streamIntoAssistantBubble(const QString &token) {
    if (!m_inAssistantBubble) beginAssistantBubble();
    m_currentAssistantText += token;
    m_output->moveCursor(QTextCursor::End);
    // insertPlainText preserves whitespace and avoids HTML escaping
    // headaches mid-stream. The bubble's parent <span> already has
    // white-space:pre-wrap so newlines render correctly.
    m_output->insertPlainText(token);
    m_output->verticalScrollBar()->setValue(m_output->verticalScrollBar()->maximum());
}

void AIPanel::endAssistantBubble() {
    if (!m_inAssistantBubble) return;
    m_inAssistantBubble = false;
    if (!m_currentAssistantText.isEmpty()) {
        ChatMessage message;
        message.role = ChatMessage::Assistant;
        message.text = m_currentAssistantText;
        message.model = m_ollama->model();
        m_messages.push_back(message);
    }
    renderTranscript();
}

// ─── File attachment ──────────────────────────────────────────────────
//
// Open a file picker, accept ANY file (image, PDF, DOCX, PPTX, code, text).
// Stash the path + a kind hint. The actual content extraction happens at
// send time so the user can attach a file, type a message, then hit Send.
void AIPanel::attachFile() {
    QString path = QFileDialog::getOpenFileName(this,
        "Attach file as context",
        QDir::homePath(),
        "All files (*);;"
        "Images (*.png *.jpg *.jpeg *.webp *.gif *.bmp);;"
        "Documents (*.pdf *.docx *.pptx *.xlsx *.odt);;"
        "Text (*.txt *.md *.json *.yaml *.toml *.csv *.xml *.html *.css);;"
        "Code (*.py *.js *.ts *.cpp *.h *.c *.rs *.go *.java *.sql *.sh)");
    if (path.isEmpty()) return;

    QFileInfo fi(path);
    QString ext = fi.suffix().toLower();
    QString kind;
    static const QStringList imageExts = {"png", "jpg", "jpeg", "webp", "gif", "bmp"};
    if (imageExts.contains(ext)) {
        kind = "image";
    } else if (ext == "pdf") {
        kind = "pdf";
    } else if (ext == "docx" || ext == "doc") {
        kind = "docx";
    } else if (ext == "pptx" || ext == "ppt") {
        kind = "pptx";
    } else if (ext == "xlsx" || ext == "xls") {
        kind = "xlsx";
    } else {
        kind = "text";
    }

    m_pendingFilePath = path;
    m_pendingFileKind = kind;

    // Show a chip above the input bar with the file name + kind + a remove [×]
    QString name = fi.fileName();
    qint64 sizeKb = fi.size() / 1024;
    QString icon = "📄";
    if (kind == "image") icon = "🖼";
    else if (kind == "pdf") icon = "📕";
    else if (kind == "docx") icon = "📘";
    else if (kind == "pptx") icon = "📙";
    else if (kind == "xlsx") icon = "📗";

    m_attachmentChip->setText(QString("%1 %2 (%3 KB) — will be included as context. Click chip to remove.")
                              .arg(icon).arg(name).arg(sizeKb));
    m_attachmentChip->setVisible(true);
    m_attachmentChip->setFixedHeight(24);
    m_attachmentChip->installEventFilter(this);  // catch click → clear

    setStatus(QString("✓ Attached: %1 (%2)").arg(name).arg(kind), false);
}
