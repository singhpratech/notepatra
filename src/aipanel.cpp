#include "aipanel.h"
#include "ai_context.h"
#include "ai_intent.h"
#include "ai_systemprompt.h"
#include "ai_tools.h"
#include "chartrender.h"
#include "csvanalyst.h"
#include "dbconnections.h"
#include "fonts.h"
#include "config.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QFont>
#include <QScrollBar>
#include <QMenu>
#include <QApplication>
#include <QStyle>
#include <QClipboard>
#include <QTimer>
#include <QFileDialog>
#include <QFileInfo>
#include <QFile>
#include <QDir>
#include <QDesktopServices>
#include <QMouseEvent>
#include <QKeyEvent>
#include <QTextCursor>
#include <QTextDocument>
#include <QFrame>
#include <QButtonGroup>
#include <QStandardItemModel>
#include <QStandardItem>
#include <QAbstractButton>
#include <QScrollArea>
#include <QProcess>
#include <QImage>
#include <QBuffer>
#include <QByteArray>
#include <QDateTime>
#include <QPainter>
#include <QPainterPath>
#include <QRegularExpression>
#include <QCryptographicHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
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
        // Assistant card background is DELIBERATELY lifted ~15 %% above the
        // chat canvas (#1E1E1E) so the response visibly reads as a card,
        // not a transparent panel blending with the page. Text on top
        // stays at #E8E8E8 for high contrast (≈ 13:1).
        p.assistBg     = "#2E2E31";
        p.assistFg     = "#E8E8E8";
        p.assistBorder = "#3F3F42";
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

QString markdownBodyHtml(const QString &text, int messageIndex = -1) {
    QString body;
#if QT_VERSION >= QT_VERSION_CHECK(5, 14, 0)
    QTextDocument doc;
    doc.setMarkdown(text);
    QString html = doc.toHtml();
    QRegularExpression bodyRe(
        "<body[^>]*>(.*)</body>",
        QRegularExpression::CaseInsensitiveOption | QRegularExpression::DotMatchesEverythingOption);
    QRegularExpressionMatch m = bodyRe.match(html);
    body = m.hasMatch() ? m.captured(1) : plainTextHtml(text);
#else
    body = plainTextHtml(text);
#endif
    if (messageIndex < 0) return body;

    // Inject a ChatGPT-style "⧉ Copy" button at the top of every <pre>
    // block so the user can grab a specific snippet without selecting it
    // by hand. The URL scheme encodes the (message, block) pair; the
    // click handler re-parses the source markdown to find the exact
    // fenced block so the copied text is the raw source, not the
    // syntax-highlighted HTML.
    QRegularExpression preRe("<pre[^>]*>",
                             QRegularExpression::CaseInsensitiveOption);
    QRegularExpressionMatchIterator it = preRe.globalMatch(body);
    QString out;
    out.reserve(body.size() + 256);
    int cursor = 0, blockIdx = 0;
    while (it.hasNext()) {
        const auto match = it.next();
        out += body.mid(cursor, match.capturedStart() - cursor);
        out += match.captured(0);
        out += QString(
            "<div style='float:right;'>"
            "<a href='copy-code://message/%1/block/%2' "
            "style='color:#4EC9B0;font-size:10px;font-weight:600;"
            "text-decoration:none;padding:2px 8px;border-radius:8px;"
            "background:rgba(78,201,176,0.15);'>⧉ Copy code</a></div>")
            .arg(messageIndex).arg(blockIdx);
        cursor = match.capturedEnd();
        ++blockIdx;
    }
    out += body.mid(cursor);
    return out;
}

// Extract the Nth ``` fenced block (0-indexed) from a raw markdown
// message. Returns empty string if the block doesn't exist. Used by
// handleChatLink to copy the exact source of a clicked code block.
static QString extractFencedBlock(const QString &markdown, int index) {
    int pos = 0, found = 0;
    while (pos < markdown.size()) {
        const int open = markdown.indexOf("```", pos);
        if (open < 0) return {};
        // skip the optional language tag + newline
        int bodyStart = markdown.indexOf('\n', open + 3);
        if (bodyStart < 0) return {};
        ++bodyStart;
        const int close = markdown.indexOf("```", bodyStart);
        if (close < 0) return {};
        if (found == index) {
            return markdown.mid(bodyStart, close - bodyStart);
        }
        ++found;
        pos = close + 3;
    }
    return {};
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
"body { font-family: %21; line-height: 1.55; color: %3; background: %4; margin: 0; padding: 14px 16px; }\n"
".assistant-content { font-family: %21; }\n"
"/* Each message is its own tight table. No vertical margins that\n"
"   compound; we space with empty rows instead. */\n"
"table.msg { width: 100%%; border-collapse: collapse; margin: 0 0 14px 0; }\n"
"/* USER — right-aligned pill, accent fill, white text. Max 88 %% so long\n"
"   paragraphs wrap. Asymmetric corner radius gives the speech-bubble\n"
"   'point' towards the user's side. */\n"
".bubble-user { display: inline-block; max-width: 88%%; text-align: left; border-radius: 18px 18px 4px 18px; padding: 10px 16px; background: %5; color: %6; font-size: 13px; font-weight: 500; }\n"
"/* ASSISTANT — distinct card background (different from chat bg) so the\n"
"   response stands out visually. Thin 3 px accent left stripe for brand\n"
"   anchor. Generous padding for breathing room. Border subtle enough\n"
"   that it reads as a card, not a box. */\n"
".assistant-wrap { background: %8; color: %9; border-left: 3px solid %10; border-top: 1px solid %11; border-right: 1px solid %11; border-bottom: 1px solid %11; border-radius: 4px 12px 12px 4px; padding: 14px 16px; }\n"
".assistant-head { margin: 0 0 10px 0; padding-bottom: 6px; border-bottom: 1px solid %11; }\n"
".assistant-model { color: %10; font-size: 10px; font-weight: 700; letter-spacing: 1px; text-transform: uppercase; }\n"
".copy-btn { color: %16; font-size: 10px; font-weight: 600; padding: 2px 10px; border-radius: 10px; text-decoration: none; letter-spacing: 0.4px; background: transparent; border: 1px solid %11; }\n"
".copy-btn:hover { background: %16; color: #ffffff; }\n"
"/* ERROR — red accent stripe + tinted background */\n"
".error-wrap { background: %12; color: %13; border-left: 3px solid %14; border-top: 1px solid %14; border-right: 1px solid %14; border-bottom: 1px solid %14; border-radius: 4px 8px 8px 4px; padding: 10px 14px; }\n"
".error-label { color: %14; font-size: 10px; font-weight: bold; letter-spacing: 1px; margin-bottom: 4px; }\n"
".message-plain { white-space: pre-wrap; }\n"
".assistant-content { color: %9; font-size: 13px; }\n"
".assistant-content p { margin: 0 0 10px 0; }\n"
".assistant-content ul, .assistant-content ol { margin: 6px 0 10px 22px; padding-left: 4px; }\n"
".assistant-content li { margin: 3px 0; }\n"
".assistant-content h1, .assistant-content h2, .assistant-content h3, .assistant-content h4 { color: %9; margin: 14px 0 8px 0; font-weight: 600; }\n"
".assistant-content a { color: %16; }\n"
".assistant-content pre { background: %17; color: %18; border: 1px solid %11; border-radius: 8px; padding: 12px; overflow-x: auto; white-space: pre-wrap; font-size: 12px; margin: 8px 0; }\n"
".assistant-content code { background: %19; color: %20; border-radius: 4px; padding: 1px 5px; font-family: %2; font-size: 12px; }\n"
".assistant-content pre code { background: transparent; padding: 0; color: inherit; font-size: 12px; }\n"
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
            // User turn — right-aligned card with accent fill. Background
            // applied on the <td> via bgcolor for Qt rich-text reliability
            // (same reason as the assistant card below — nested-div bg in
            // Qt's CSS subset can drop).
            html += QString(
                "<table class='msg' cellpadding='0' cellspacing='0' style='margin-bottom:14px;'>"
                "<tr><td align='right'>"
                "<table cellpadding='0' cellspacing='0'><tr>"
                "<td bgcolor='%2' style='padding:10px 16px; color:%3; font-size:13px; font-weight:500;'>"
                "<div class='message-plain'>%1</div>"
                "</td></tr></table>"
                "</td></tr></table>")
                .arg(plainTextHtml(message.text), pal.userBg, pal.userFg);
            continue;
        }

        if (message.role == AIPanel::ChatMessage::Error) {
            html += QString(
                "<table class='msg' cellpadding='0' cellspacing='0' style='margin-bottom:14px;'>"
                "<tr><td bgcolor='%2' style='padding:12px 14px; border-left:4px solid %3; color:%4;'>"
                "<div style='color:%3; font-size:10px; font-weight:bold; letter-spacing:1px; margin-bottom:4px;'>ERROR</div>"
                "<div class='message-plain'>%1</div>"
                "</td></tr></table>")
                .arg(plainTextHtml(message.text), pal.errBg, pal.errBorder, pal.errFg);
            continue;
        }

        // Assistant turn — card background applied DIRECTLY on the <td>.
        // Qt's QTextBrowser rich-text engine renders <td> backgrounds
        // reliably; nested-div backgrounds can silently drop in Qt's CSS
        // subset, which is why the response read as "all black" before.
        //
        // The header row carries: model name (left), token+timing stats
        // (middle, only if the backend reported them), Copy link (right).
        QString statsHtml;
        if (message.elapsedMs >= 0) {
            const double secs = message.elapsedMs / 1000.0;
            QString tokensPart;
            if (message.evalTokens > 0) {
                // Tokens-per-second is the metric users care about
                // (decoder throughput on their hardware). Only show it
                // for runs > 200 ms so we don't divide by tiny numbers.
                if (message.elapsedMs > 200) {
                    const double tps = message.evalTokens * 1000.0 / message.elapsedMs;
                    tokensPart = QString("%1 tok · %2 tok/s · ")
                                     .arg(message.evalTokens)
                                     .arg(QString::number(tps, 'f', 1));
                } else {
                    tokensPart = QString("%1 tok · ").arg(message.evalTokens);
                }
            }
            statsHtml = QString(
                "<span style='color:%1; font-size:10px; font-weight:500; "
                "letter-spacing:0.3px; opacity:0.75;'>%2%3 s</span>")
                .arg(pal.linkFg, tokensPart, QString::number(secs, 'f', 1));
        }
        html += QString(
            "<table class='msg' cellpadding='0' cellspacing='0' style='margin-bottom:14px;'>"
            "<tr><td bgcolor='%4' style='padding:16px 18px; border-left:4px solid %5; border-top:1px solid %6; border-right:1px solid %6; border-bottom:1px solid %6; color:%7;'>"
            "<table width='100%%' cellpadding='0' cellspacing='0'>"
            "<tr>"
            "<td><span style='color:%5; font-size:10px; font-weight:700; letter-spacing:1px; text-transform:uppercase;'>%1</span></td>"
            "<td align='center' style='padding:0 8px;'>%9</td>"
            "<td align='right'><a href='copy://message/%2' style='color:%8; font-size:10px; text-decoration:none; font-weight:600;'>⧉ copy</a></td>"
            "</tr>"
            "</table>"
            "<div class='assistant-content' style='margin-top:10px; padding-top:8px; border-top:1px solid %6; color:%7;'>%3</div>"
            "</td></tr></table>")
            .arg(message.model.toHtmlEscaped(),
                 QString::number(i),
                 markdownBodyHtml(message.text, i),
                 pal.assistBg, pal.assistAccent, pal.assistBorder, pal.assistFg, pal.linkFg,
                 statsHtml);
    }

    html += QStringLiteral("</body></html>");
    return html;
}

}  // namespace

// Forward declarations for the bubble-factory helpers — their definitions
// live further down near renderTranscript(). Declared here so earlier
// methods (clearChat, etc.) can call them.
static void aiClearChat(QVBoxLayout *layout);
static void aiAddUserBubble(QVBoxLayout *target, const QString &text,
                            const AiPalette &pal);
static QFrame *aiAddAssistantCard(QVBoxLayout *target,
                                  const AIPanel::ChatMessage &msg,
                                  int messageIndex,
                                  const AiPalette &pal,
                                  std::function<void(int)> copyCb,
                                  QTextBrowser **outBody = nullptr);
static void aiAddErrorCard(QVBoxLayout *target, const QString &text,
                           const AiPalette &pal);

AIPanel::AIPanel(QWidget *parent) : QWidget(parent) {
    // Make the panel comfortably wide so chat bubbles render properly.
    // Like a real chat app — narrow chat looks cramped.
    setMinimumWidth(420);

    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(2);

    const AiPalette pal = aiPalette();

    // ─── TOP STRIP: Header + model selector + status ────────────────────
    // Single-letter brand mark in a compact header — "AI Assistant" was
    // routinely truncated in the 320 px-wide right dock. Full product
    // name lives on the welcome screen + the dock's content speaks for
    // itself. Height bumped so the underline for Coding Mode doesn't
    // clip the ascenders.
    // ── Header row: label on the left, close-dock (✕) on the right ──
    // Close hides the dock but PRESERVES the chat session (m_messages is
    // not cleared). Reset (in the model row below) still clears the
    // session. This mirrors how every real chat app works — closing
    // the window != starting over.
    auto *headerRow = new QWidget;
    headerRow->setFixedHeight(28);
    headerRow->setStyleSheet(QString("background: %1;").arg(pal.chromeBg));
    auto *headerLay = new QHBoxLayout(headerRow);
    headerLay->setContentsMargins(0, 0, 0, 0);
    headerLay->setSpacing(0);
    m_headerLabel = new QLabel("  AI");
    m_headerLabel->setStyleSheet(QString(
        "font-weight: 600; background: %1; color: %2; "
        "padding: 6px 10px; letter-spacing: 1px; font-size: 11px;")
        .arg(pal.chromeBg, pal.headerFg));
    headerLay->addWidget(m_headerLabel, 1);

    // Close button — use the OS-native close glyph via QStyle so we never
    // depend on a Unicode codepoint that might mojibake on Windows fonts.
    // v0.1.39 — theme-independent red close button. The previous
    // platform-icon variant (SP_TitleBarCloseButton over pal.chromeBg)
    // was tone-on-tone against the panel header on both Light and Dark
    // themes, so the X was effectively invisible at rest. Using the
    // U+00D7 MULTIPLICATION SIGN (×) — present in every font shipped on
    // every desktop OS, no tofu risk — and the Windows-canonical close-
    // button red (#E81123) at rest, red background + white X on hover.
    // Theme-independent on purpose: should be prominent on every chrome.
    auto *closeBtn = new QPushButton(QString::fromUtf8("\xC3\x97"));  // U+00D7 MULTIPLICATION SIGN
    {
        QFont closeFont = closeBtn->font();
        closeFont.setPointSize(closeFont.pointSize() > 0 ? closeFont.pointSize() + 6 : 18);
        closeFont.setBold(true);
        closeBtn->setFont(closeFont);
    }
    closeBtn->setFixedSize(36, 28);
    closeBtn->setCursor(Qt::PointingHandCursor);
    closeBtn->setFlat(true);
    closeBtn->setToolTip("Close the AI dock (session stays — press Reset to clear chat)");
    closeBtn->setStyleSheet(
        "QPushButton { background: transparent; border: none; "
        "color: #E81123; font-weight: 700; padding: 0 0 2px 0; } "
        "QPushButton:hover { background: #E81123; color: white; } "
        "QPushButton:pressed { background: #C41019; color: white; }");
    connect(closeBtn, &QPushButton::clicked, this, [this]() {
        // Hide by walking up to the enclosing dock host (m_aiDockHost
        // in MainWindow is our grandparent via QVBoxLayout → QWidget).
        QWidget *host = parentWidget();
        if (host) host->setVisible(false);
        // Do NOT clear m_messages — the chat is preserved for when the
        // user reopens via Ctrl+Shift+A or the menu.
    });
    headerLay->addWidget(closeBtn, 0, Qt::AlignRight);
    layout->addWidget(headerRow);

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
    // v0.1.54 — backend dropdown trimmed to 4 entries. "Custom" was removed
    // earlier (no URL field in panel chrome). LM Studio + Jan are removed
    // here because they're just GUI wrappers around llama.cpp's HTTP
    // server — Ollama covers the easy local case, llama.cpp covers the
    // power-user case, and a curated catalog of 12 GGUF models is in the
    // model dropdown so users don't need a separate "GUI catalog" app.
    // Users who DO run LM Studio / Jan can still reach them by picking
    // "llama.cpp" + setting the port via Settings → Preferences → AI.
    auto *backendCombo = new QComboBox;
    backendCombo->addItem("Ollama",             "Ollama");
    backendCombo->addItem("llama.cpp (GGUF)",   "llama.cpp");
    backendCombo->addItem("OpenRouter (cloud)", "OpenRouter");
    backendCombo->addItem("OpenAI",             "OpenAI");
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
        else if (Config::instance().aiBaseUrl.contains("openai.com")) backendCombo->setCurrentIndex(3);
        // Stale LM Studio / Jan URLs from older configs fall through to
        // Ollama default — user just picks llama.cpp manually if needed.
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
        else if (key == "OpenAI")     { cfg.aiBackend = "OpenAI-compat"; cfg.aiBaseUrl = "https://api.openai.com/v1"; }
        else                          { cfg.aiBackend = "Ollama";        cfg.aiBaseUrl.clear(); }
        cfg.save();
        // Reconfigure the client and refresh the model list.
        // v0.1.54 — ALWAYS reset the OllamaClient's base URL on every
        // backend switch. Pre-fix the call was only made when
        // cfg.aiBaseUrl was non-empty, so switching cloud → Ollama left
        // m_baseUrl pointing at openrouter.ai (or wherever the previous
        // cloud backend lived) and the /api/tags probe failed silently.
        if (m_ollama) {
            m_ollama->setBackend(OllamaClient::backendFromString(cfg.aiBackend));
            QString resolved = cfg.aiBaseUrl;
            if (resolved.isEmpty()) {
                // Fall back to the backend's documented default endpoint.
                if      (cfg.aiBackend == "Ollama")    resolved = "http://localhost:11434";
                else if (cfg.aiBackend == "llama.cpp") resolved = "http://localhost:8080";
                else                                    resolved = "http://localhost:8080";
            }
            m_ollama->setBaseUrl(resolved);
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
        const bool needsKey = (key == "OpenRouter" || key == "OpenAI");
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

    // Reset session — moved here in v0.1.48 from the bottom of the row so
    // the top-bar shape is just [backend | model ↻ ⚙ Reset]. Mode toggles
    // (Chat/Coding/Data + Think) live on a dedicated second row below.
    m_clearBtn = new QPushButton("Reset");
    m_clearBtn->setObjectName("aiResetSessionButton");
    m_clearBtn->setFixedWidth(56);
    m_clearBtn->setStyleSheet("font-size: 11px;");
    m_clearBtn->setToolTip("Reset the AI Assistant session");
    modelRow->addWidget(m_clearBtn);
    layout->addLayout(modelRow);
    layout->addWidget(apiKeyHost);

    // ─── Mode row: 3-way segmented selector + Think checkbox ─────────────
    // v0.1.48 — Coding/Data are no longer two independent checkboxes that
    // had to ping-pong each other off (the previous wiring also had a
    // dead-code bug: connect(m_dataMode,…) ran before m_dataMode was
    // constructed, so toggling Data did nothing through that path). They
    // are now a 3-way mode group:
    //
    //   [ Chat ][ Coding ][ Data ]   ☐ Think
    //
    //   Chat   = general chat assistant (default; conversational prompt,
    //            quick actions visible, no agentic tools)
    //   Coding = full agentic coding agent (read/write files, search,
    //            apply diffs, run shell commands; code-only output;
    //            [Apply] button replaces selection)
    //   Data   = data analyst (query_sql / csv_query / chart_spec, prompt
    //            steered toward CSV + DB analysis, Manage Connections
    //            button + model-capability banner)
    //
    // Mutual exclusion is enforced by QButtonGroup::setExclusive(true).
    auto *modeRow = new QHBoxLayout;
    modeRow->setContentsMargins(8, 0, 8, 4);
    modeRow->setSpacing(0);

    auto *modeFrame = new QFrame;
    modeFrame->setStyleSheet(QString(
        "QFrame { background: %1; border: 1px solid %2; border-radius: 6px; }")
        .arg(pal.chromeBg, pal.btnBorder));
    auto *modeLay = new QHBoxLayout(modeFrame);
    modeLay->setContentsMargins(2, 2, 2, 2);
    modeLay->setSpacing(2);

    auto makeModeBtn = [&pal](const QString &label,
                              const QString &accentColor,
                              const QString &tip) {
        auto *btn = new QPushButton(label);
        btn->setCheckable(true);
        btn->setCursor(Qt::PointingHandCursor);
        btn->setFixedHeight(22);
        btn->setMinimumWidth(54);
        btn->setToolTip(tip);
        btn->setStyleSheet(QString(
            "QPushButton { background: transparent; border: none; "
            "color: %1; font-size: 11px; font-weight: 600; padding: 2px 10px; "
            "border-radius: 4px; } "
            "QPushButton:hover { background: %2; color: %3; } "
            "QPushButton:checked { background: %4; color: white; }")
            .arg(pal.muted, pal.btnHover, pal.inputText, accentColor));
        return btn;
    };

    auto *chatBtn   = makeModeBtn("Chat",   pal.accent,
        "General chat assistant — explain code, brainstorm, ask anything. "
        "Default mode. No agentic file edits, no DB tools.");
    auto *codingBtn = makeModeBtn("Coding", QStringLiteral("#4EC9B0"),
        "Coding Agent — full agentic mode: read/write files, search workspace, "
        "apply diffs, run shell commands. Returns code, applies changes.");
    auto *dataBtn   = makeModeBtn("Data",   QStringLiteral("#FF9F43"),
        "Data Analyst — query CSVs and saved DB connections, generate charts "
        "inline. Best with capable models (banner appears if a smaller model is loaded).");

    auto *modeGroup = new QButtonGroup(this);
    modeGroup->setExclusive(true);
    modeGroup->addButton(chatBtn);
    modeGroup->addButton(codingBtn);
    modeGroup->addButton(dataBtn);

    modeLay->addWidget(chatBtn);
    modeLay->addWidget(codingBtn);
    modeLay->addWidget(dataBtn);

    m_chatMode   = chatBtn;
    m_codingMode = codingBtn;
    m_dataMode   = dataBtn;

    modeRow->addWidget(modeFrame);
    modeRow->addStretch();

    m_thinkingCheck = new QCheckBox("Think");
    m_thinkingCheck->setChecked(false);
    m_thinkingCheck->setStyleSheet(QString(
        "font-size: 11px; color: %1; margin-left: 8px;").arg(pal.muted));
    m_thinkingCheck->setToolTip("Show the model's reasoning blocks (Qwen3, DeepSeek-R1). "
                                "Off = faster, cleaner answers. On = see how the model thinks.");
    modeRow->addWidget(m_thinkingCheck);
    layout->addLayout(modeRow);

    // ─── Mode toggled — single applier wired to all three buttons. ───────
    // Only fires when a button transitions to checked (the "off" toggle of
    // the previously-active button is ignored — applyMode reads the
    // current state of all three pointers, so handling the on-edge alone
    // is sufficient). All widget access is null-guarded because applyMode
    // is also called once at the end of the constructor before some
    // later-constructed widgets exist (m_customInput etc.).
    auto applyMode = [this]() {
        const bool coding = m_codingMode && m_codingMode->isChecked();
        const bool data   = m_dataMode   && m_dataMode->isChecked();

        Config::instance().aiDataMode = data;
        Config::instance().save();

        if (m_customInput) {
            m_customInput->setPlaceholderText(
                coding ? "Coding Mode · e.g. Refactor this function / Add types / Translate to TypeScript"
              : data   ? "Data Mode · e.g. summarize this CSV / show top 10 customers / chart revenue by month"
                       : "Type a message and press Enter to send…");
        }

        if (m_thinkingCheck) {
            m_thinkingCheck->setEnabled(!coding);
            m_thinkingCheck->setToolTip(coding
                ? "Disabled while Coding Mode is on — Coding Mode forces code-only output, "
                  "so reasoning blocks would interfere with the [Apply] button paste. "
                  "Switch to Chat or Data to use thinking mode."
                : "Show the model's reasoning blocks (Qwen3, DeepSeek-R1). "
                  "Off = faster, cleaner answers. On = see how the model thinks.");
        }

        if (m_quickActionsWrap)  m_quickActionsWrap->setVisible(!coding);
        if (m_resultActionsWrap) m_resultActionsWrap->setVisible(!coding);

        // v0.1.53 — show the welcome card whenever Data mode is on AND
        // the chat is empty AND the user hasn't dismissed it. Always
        // shown regardless of banner state.
        const bool showWelcome =
            data && m_messages.isEmpty() && !Config::instance().aiHideDataWelcome;
        if (showWelcome) {
            renderDataWelcomeCard();
        } else {
            removeDataWelcomeCard();
        }
        // v0.1.54 — when the welcome card is up, it carries its OWN
        // "Manage Connections…" button right next to the connection
        // count. The standalone external button on the data row is
        // duplicate chrome in that case — hide it. When the welcome
        // card is hidden (chat in progress, or user clicked Hide), the
        // external button is the only way to reach the dialog, so we
        // show it.
        if (m_manageConnsBtn) m_manageConnsBtn->setVisible(data && !showWelcome);

        if (m_dataCapBanner) {
            if (!data) {
                m_dataCapBanner->setVisible(false);
            } else {
                const QString modelName = m_modelCombo ? m_modelCombo->currentText() : QString();
                const bool capable = AiTools::modelCapableOfDataAnalysis(modelName);
                if (capable) {
                    m_dataCapBanner->setVisible(false);
                } else {
                    // v0.1.53 — single-line tight banner. The welcome card
                    // below carries the rich version with examples + chips;
                    // this banner just flags the model issue at a glance.
                    // v0.1.54 — family names instead of version pins. Specific
                    // model versions (Claude Sonnet 4.5, GPT-5, Qwen 2.5)
                    // get retired/renamed every few months; Notepatra would
                    // need an app update to refresh them. Family names
                    // (Claude / GPT / Gemini / Qwen-Coder) are stable.
                    m_dataCapBanner->setText(tr(
                        "⚠ %1 is too small — try a strong local code model "
                        "(<code>ollama pull qwen2.5-coder:14b</code> or any "
                        "Qwen-Coder / DeepSeek-Coder / Llama 7B+) or a "
                        "frontier cloud model (Claude / GPT / Gemini).")
                            .arg(modelName.isEmpty() ? tr("(no model selected)") : modelName));
                    m_dataCapBanner->setTextFormat(Qt::RichText);
                    m_dataCapBanner->setWordWrap(true);
                    m_dataCapBanner->setMaximumHeight(32);
                    m_dataCapBanner->setVisible(true);
                }
            }
        }

        if (m_headerLabel) {
            const AiPalette p = aiPalette();
            const QString text = coding ? "  AI  ·  ⌘ CODING"
                              : data   ? "  AI  ·  📊 DATA"
                                       : "  AI";
            const QString fg   = coding ? QStringLiteral("#4EC9B0")
                              : data   ? QStringLiteral("#FF9F43")
                                       : p.headerFg;
            const QString rule = coding ? QStringLiteral("#4EC9B0")
                              : data   ? QStringLiteral("#FF9F43")
                                       : QStringLiteral("transparent");
            m_headerLabel->setText(text);
            m_headerLabel->setStyleSheet(QString(
                "font-weight: 600; background: %1; color: %2; "
                "padding: 6px 10px; letter-spacing: 1px; font-size: 11px;"
                "border-bottom: 2px solid %3;")
                .arg(p.chromeBg, fg, rule));
        }

        if (m_chatLayout) renderTranscript();
        emit codingModeRequested(coding);
    };

    connect(m_chatMode,   &QAbstractButton::toggled, this, [applyMode](bool on) { if (on) applyMode(); });
    connect(m_codingMode, &QAbstractButton::toggled, this, [applyMode](bool on) { if (on) applyMode(); });
    connect(m_dataMode,   &QAbstractButton::toggled, this, [applyMode](bool on) { if (on) applyMode(); });

    // Refresh the data-mode capability banner when the user picks a
    // different model. v0.1.53: includes the local recommendation
    // (`qwen2.5-coder:14b`) — pre-fix the suggestion list was cloud-only,
    // which made local-Ollama users think the only fix was paying for a
    // cloud API.
    if (m_modelCombo) {
        connect(m_modelCombo, &QComboBox::currentTextChanged,
                this, [this](const QString &modelName) {
            if (!m_dataMode || !m_dataMode->isChecked() || !m_dataCapBanner) return;
            const bool capable = AiTools::modelCapableOfDataAnalysis(modelName);
            if (capable) {
                m_dataCapBanner->setVisible(false);
            } else {
                m_dataCapBanner->setText(tr(
                    "⚠ %1 is too small for reliable multi-table SQL and chart specs.\n"
                    "<b>Local (free)</b>: <code>ollama pull qwen2.5-coder:14b</code> "
                    "(or any Qwen-Coder / DeepSeek-Coder / Llama 7B+)\n"
                    "<b>Cloud</b>: any frontier Claude · GPT · Gemini · DeepSeek")
                        .arg(modelName.isEmpty() ? tr("(no model selected)") : modelName));
                m_dataCapBanner->setTextFormat(Qt::RichText);
                m_dataCapBanner->setVisible(true);
            }
            // v0.1.53 — refresh the welcome card to show the new model + capability.
            if (m_dataWelcomeFrame && m_messages.isEmpty()) {
                removeDataWelcomeCard();
                renderDataWelcomeCard();
            }
        });
    }

    // Initial mode is set at the END of the constructor, once all
    // referenced widgets (m_customInput, m_quickActionsWrap, m_chatLayout,
    // etc.) are constructed. See the trailing applyMode() call below.

    // ─── Data Analyst row (v0.1.43, hidden until Data Mode is on) ────────
    // Manage Connections... button + capability banner. The banner is only
    // visible when (a) Data Mode is on AND (b) the active model is below
    // the recommended bar for SQL/chart work — see modelCapableOfDataAnalysis.
    {
        auto *dataRow = new QHBoxLayout();
        m_manageConnsBtn = new QPushButton(tr("Manage Connections…"));
        m_manageConnsBtn->setStyleSheet(
            "QPushButton { font-size: 11px; padding: 4px 10px; "
            "background: #FF9F43; color: white; border: none; border-radius: 4px; }"
            "QPushButton:hover { background: #FFA726; }");
        m_manageConnsBtn->setToolTip(tr(
            "Add, edit, and test database connections for the query_sql tool. "
            "SQLite ships built-in; install Qt SQL plugins for "
            "Postgres/MySQL/SQL Server."));
        dataRow->addWidget(m_manageConnsBtn);
        dataRow->addStretch();
        m_dataCapBanner = new QLabel;
        m_dataCapBanner->setWordWrap(true);
        // v0.1.54 — theme-aware banner colours. Pre-fix the banner was
        // hard-coded brown (#553B19) + cream (#FFD49A) which looked OK on
        // Dark theme but read as muddy/illegible on Light. Use a soft
        // amber palette that works on both, with extra contrast on the
        // text. Light theme: cream-bg + dark-amber-text (#7A4A0E).
        // Dark theme: muted-orange-bg + cream-text. Reactive via aiPalette().
        const bool dark = aiIsDark();
        const QString bg = dark ? QStringLiteral("#3F2E16")
                                : QStringLiteral("#FFF1D6");
        const QString fg = dark ? QStringLiteral("#FFD49A")
                                : QStringLiteral("#7A4A0E");
        const QString border = dark ? QStringLiteral("#7A4A0E")
                                    : QStringLiteral("#E5A661");
        m_dataCapBanner->setStyleSheet(QString(
            "QLabel { background: %1; color: %2; border: 1px solid %3; "
            "padding: 6px 10px; border-radius: 6px; font-size: 12px; }")
            .arg(bg, fg, border));
        dataRow->addWidget(m_dataCapBanner, 1);
        layout->addLayout(dataRow);
        m_manageConnsBtn->setVisible(false);
        m_dataCapBanner->setVisible(false);

        // Manage Connections… opens the connection-CRUD dialog. Wired here
        // (not earlier next to the mode-row connects) because m_manageConnsBtn
        // is created in this block — connecting before construction was a
        // dead-code bug in v0.1.43-v0.1.47.
        connect(m_manageConnsBtn, &QPushButton::clicked, this, [this]() {
            DbConnectionsDialog dlg(this);
            dlg.exec();
        });
    }

    m_statusLabel = new QLabel("");
    m_statusLabel->setStyleSheet(QString(
        "color: %1; padding: 2px 8px; font-size: 11px;").arg(pal.muted));
    // wordWrap so long error messages like "Error transferring
    // https://api.openai.com/..." wrap instead of clipping at the right
    // panel edge. minimumHeight gives the label room for the typical
    // 1-line case, but it can grow to 2-3 lines for long URLs.
    m_statusLabel->setWordWrap(true);
    m_statusLabel->setMinimumHeight(18);
    m_statusLabel->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::MinimumExpanding);
    layout->addWidget(m_statusLabel);

    // ─── MIDDLE: chat area — REAL Qt widgets, not HTML ─────────────────
    // Each turn is its own QFrame (see addUserBubble / addAssistantCard /
    // addErrorCard below). Widget-level stylesheets render reliably; HTML
    // + CSS through QTextBrowser kept silently dropping backgrounds on
    // nested divs, which is why responses read as "all dark" before.
    m_chatArea = new QScrollArea;
    m_chatArea->setWidgetResizable(true);
    m_chatArea->setFrameShape(QFrame::NoFrame);
    m_chatArea->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_chatArea->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    m_chatArea->setStyleSheet(QString(
        "QScrollArea { background: %1; border: none; } "
        "QScrollBar:vertical { background: %1; width: 10px; margin: 0; } "
        "QScrollBar::handle:vertical { background: %2; border-radius: 4px; min-height: 24px; } "
        "QScrollBar::handle:vertical:hover { background: %3; } "
        "QScrollBar::add-line:vertical, QScrollBar::sub-line:vertical { height: 0; }")
        .arg(pal.chatBg, pal.assistBorder, pal.accent));

    m_chatContent = new QWidget;
    m_chatContent->setStyleSheet(QString("background: %1;").arg(pal.chatBg));
    m_chatLayout = new QVBoxLayout(m_chatContent);
    m_chatLayout->setContentsMargins(12, 12, 12, 12);
    m_chatLayout->setSpacing(0);
    m_chatLayout->addStretch(1);   // bubbles get inserted BEFORE this stretch
    m_chatArea->setWidget(m_chatContent);
    layout->addWidget(m_chatArea, 1);

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
        // v0.1.49 — guarantee min width fits the label + padding so Windows
        // (Segoe UI ~20 % wider than DejaVu) doesn't truncate "Write Tests"
        // / "Find Bugs" / "Add Comments" with "...".
        b->setMinimumWidth(b->fontMetrics().horizontalAdvance(b->text()) + 22);
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
        b->setMinimumWidth(b->fontMetrics().horizontalAdvance(b->text()) + 22);
        actionsRow2->addWidget(b);
    }
    quickWrapV->addLayout(actionsRow2);

    // v0.1.40 — third row of strict-prompt format-fix buttons. These route
    // to the same "minimal-change patcher" prompt that Tools → JSON Tools →
    // AI Fix uses (mainwindow.cpp:2209), so small models stop "improving"
    // the user's payload by adding fields, restructuring, hallucinating.
    auto *actionsRow3 = new QHBoxLayout;
    actionsRow3->setContentsMargins(8, 0, 8, 4);
    actionsRow3->setSpacing(4);
    auto *fixJsonBtn = new QPushButton("Fix JSON");
    auto *fixHtmlBtn = new QPushButton("Fix HTML");
    auto *fixSqlBtn  = new QPushButton("Fix SQL");
    for (auto *b : {fixJsonBtn, fixHtmlBtn, fixSqlBtn}) {
        b->setFixedHeight(24);
        b->setStyleSheet("font-size: 11px; padding: 0 8px;");
        b->setMinimumWidth(b->fontMetrics().horizontalAdvance(b->text()) + 22);
        b->setToolTip("Strict minimal-change fix for the selection (or current file). "
                      "Won't add fields, won't reformat, won't restructure.");
        actionsRow3->addWidget(b);
    }
    actionsRow3->addStretch();
    quickWrapV->addLayout(actionsRow3);

    // v0.1.40 — tip line under the quick-action rows pointing users at the
    // dedicated panel for serious format-fixing work. Uses the existing
    // text-dim palette colour so it's visible without competing with the
    // buttons. Wraps to two lines on narrow docks.
    auto *fixTip = new QLabel(
        "💡 For larger or repeated fixes, open Tools → JSON Tools "
        "(or HTML / SQL) — dedicated panel with side-by-side diff, "
        "regex-first repair, AI fallback.");
    fixTip->setWordWrap(true);
    fixTip->setStyleSheet(QString(
        "color: %1; font-size: 10px; padding: 2px 10px 4px;")
        .arg(pal.muted));
    quickWrapV->addWidget(fixTip);
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
        b->setMinimumWidth(b->fontMetrics().horizontalAdvance(b->text()) + 22);
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

    // Multi-line, Cursor-style input — grows with content up to 6 lines,
    // scrolls after that. Enter sends, Shift+Enter inserts a newline
    // (standard chat-app semantics; handled in eventFilter below).
    m_customInput = new QPlainTextEdit;
    m_customInput->setPlaceholderText("Ask anything about your code, or paste a snippet…");
    m_customInput->setStyleSheet(QString(
        "QPlainTextEdit { background: %1; color: %2; border: 1px solid %3; "
        "border-radius: 12px; padding: 8px 14px; font-size: 13px; }"
        "QPlainTextEdit:focus { border: 1px solid %4; }")
        .arg(pal.inputBg, pal.inputText, pal.inputBorder, pal.inputFocus));
    m_customInput->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    m_customInput->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_customInput->setTabChangesFocus(true);
    m_customInput->setMinimumHeight(40);
    m_customInput->setMaximumHeight(140);
    // Suppress Qt's default scroll-area corner widget. Without this, the
    // Windows native style paints a small grey dot in the bottom-right
    // intersection where a horizontal scrollbar would sit (even though
    // we've set HorizontalScrollBarPolicy to AlwaysOff). Users reported
    // "what's that circle in the input box?" -- this kills it.
    m_customInput->setCornerWidget(nullptr);
    // Also disable the size-grip behaviour some Windows themes inherit
    // from the widget being inside a frame / dock.
    if (auto *vb = m_customInput->verticalScrollBar()) {
        vb->setStyleSheet("QScrollBar::add-line:vertical, "
                          "QScrollBar::sub-line:vertical { height: 0; }");
    }
    // Auto-grow: resize to fit content up to maximumHeight.
    auto resizeInput = [this]() {
        const int lines = qMax(1, m_customInput->document()->blockCount());
        const int lineH = m_customInput->fontMetrics().lineSpacing();
        const int h = qBound(40, lineH * lines + 18, 140);
        m_customInput->setFixedHeight(h);
    };
    connect(m_customInput->document(), &QTextDocument::contentsChanged, this, resizeInput);
    // Enter = send, Shift+Enter = newline. Registered via the existing
    // eventFilter() hook below.
    m_customInput->installEventFilter(this);
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

    // Per-message anchorClicked connects are installed in aiAddAssistantCard
    // now (each assistant card has its own QTextBrowser body). The global
    // connect to m_output is obsolete — m_output is no longer used.

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
        // v0.1.35 — agent loop: if any tool calls landed during this
        // stream, flush them back to the model BEFORE finalising the
        // bubble. flushPendingToolResults handles the round-trip and
        // re-opens the assistant bubble for the next response chunk.
        if (m_toolsActiveThisTurn && !m_pendingToolResults.isEmpty()) {
            flushPendingToolResults();
            return;
        }
        m_toolsActiveThisTurn = false;
        m_toolCallsThisTurn = 0;
        m_toolCallsTotal = 0;
        m_stopBtn->setEnabled(false);
        endAssistantBubble();
    });
    // v0.1.35 — Tool-call from the model. Execute against the workspace,
    // queue the result for the agent loop, and render an inline 🔧 card.
    connect(m_ollama, &OllamaClient::toolCallReceived, this,
            &AIPanel::handleToolCall);
    // Per-response stats: tokens + elapsed time. Wired to attach onto the
    // last Assistant message (the one we just finalised in endAssistantBubble)
    // and trigger a re-render so the bubble shows "1234 tokens · 2.3s".
    connect(m_ollama, &OllamaClient::responseStats, this,
            [this](int promptTokens, int evalTokens, qint64 elapsedMs) {
        for (int i = m_messages.size() - 1; i >= 0; --i) {
            if (m_messages[i].role == ChatMessage::Assistant) {
                m_messages[i].promptTokens = promptTokens;
                m_messages[i].evalTokens   = evalTokens;
                m_messages[i].elapsedMs    = elapsedMs;
                break;
            }
        }
        scheduleChatSave();
        renderTranscript();
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

        // v0.1.53 — when the user picks "llama.cpp (GGUF)" in the backend
        // dropdown, llama-server's /v1/models endpoint only returns the
        // single model that's currently loaded (or nothing if no server
        // is running). That's not a "list" the user can pick from. So we
        // augment the dropdown with a curated catalog of well-known GGUF
        // models — they pick one, then download + run llama-server with
        // that model. Each item's userData carries the HuggingFace direct-
        // download URL so a future "Download + start server" wizard can
        // pull the file. For now, the names + tooltips guide the user.
        const QString backendNow = Config::instance().aiBackend;
        const bool isLlamaCpp = backendNow.compare("llama.cpp", Qt::CaseInsensitive) == 0;

        if (isLlamaCpp) {
            // Group: actually-loaded model first (if any), then catalog.
            if (!models.isEmpty()) {
                m_modelCombo->addItem(QString("● Loaded: %1").arg(models.first()));
                m_modelCombo->setItemData(0, models.first(), Qt::UserRole);
                m_modelCombo->insertSeparator(m_modelCombo->count());
            }
            struct GgufEntry {
                const char *label;     // dropdown text
                const char *params;    // "1.5B" etc — for display
                const char *family;    // group separator
                const char *url;       // HuggingFace GGUF URL (Q4_K_M)
                const char *useCase;   // tooltip
            };
            static const GgufEntry CATALOG[] = {
                {"Qwen2.5-Coder 1.5B (Q4_K_M)", "1.5B", "Qwen — code",
                    "https://huggingface.co/Qwen/Qwen2.5-Coder-1.5B-Instruct-GGUF/resolve/main/qwen2.5-coder-1.5b-instruct-q4_k_m.gguf",
                    "Tiny code model — fast on CPU. ~1 GB. Great default."},
                {"Qwen2.5-Coder 7B (Q4_K_M)", "7B", "Qwen — code",
                    "https://huggingface.co/Qwen/Qwen2.5-Coder-7B-Instruct-GGUF/resolve/main/qwen2.5-coder-7b-instruct-q4_k_m.gguf",
                    "Best small-model coding choice. ~4.7 GB."},
                {"Qwen2.5-Coder 14B (Q4_K_M)", "14B", "Qwen — code",
                    "https://huggingface.co/Qwen/Qwen2.5-Coder-14B-Instruct-GGUF/resolve/main/qwen2.5-coder-14b-instruct-q4_k_m.gguf",
                    "Excellent code model — needs 16 GB+ RAM. ~8.4 GB."},
                {"Qwen2.5 7B (Q4_K_M)", "7B", "Qwen — chat",
                    "https://huggingface.co/Qwen/Qwen2.5-7B-Instruct-GGUF/resolve/main/qwen2.5-7b-instruct-q4_k_m.gguf",
                    "General-purpose chat model from Alibaba."},
                {"Llama 3.2 3B (Q4_K_M)", "3B", "Meta — small",
                    "https://huggingface.co/bartowski/Llama-3.2-3B-Instruct-GGUF/resolve/main/Llama-3.2-3B-Instruct-Q4_K_M.gguf",
                    "Small, fast Meta model. ~2 GB. Good for short chats."},
                {"Llama 3.1 8B (Q4_K_M)", "8B", "Meta — chat",
                    "https://huggingface.co/bartowski/Meta-Llama-3.1-8B-Instruct-GGUF/resolve/main/Meta-Llama-3.1-8B-Instruct-Q4_K_M.gguf",
                    "Capable mid-size Meta model. ~4.9 GB."},
                {"Phi-4 14B (Q4_K_M)", "14B", "Microsoft",
                    "https://huggingface.co/bartowski/phi-4-GGUF/resolve/main/phi-4-Q4_K_M.gguf",
                    "Microsoft's smart compact model. ~9 GB."},
                {"Gemma 2 2B (Q4_K_M)", "2B", "Google",
                    "https://huggingface.co/bartowski/gemma-2-2b-it-GGUF/resolve/main/gemma-2-2b-it-Q4_K_M.gguf",
                    "Google's smallest Gemma — great on weak hardware. ~1.6 GB."},
                {"Gemma 2 9B (Q4_K_M)", "9B", "Google",
                    "https://huggingface.co/bartowski/gemma-2-9b-it-GGUF/resolve/main/gemma-2-9b-it-Q4_K_M.gguf",
                    "Google's mid-size Gemma. ~5.8 GB."},
                {"Mistral 7B Instruct v0.3 (Q4_K_M)", "7B", "Mistral",
                    "https://huggingface.co/bartowski/Mistral-7B-Instruct-v0.3-GGUF/resolve/main/Mistral-7B-Instruct-v0.3-Q4_K_M.gguf",
                    "Mistral's classic 7B chat model. ~4.4 GB."},
                {"DeepSeek-Coder-V2-Lite (Q4_K_M)", "16B (MoE)", "DeepSeek",
                    "https://huggingface.co/bartowski/DeepSeek-Coder-V2-Lite-Instruct-GGUF/resolve/main/DeepSeek-Coder-V2-Lite-Instruct-Q4_K_M.gguf",
                    "MoE coding model — only 2.4B active params. ~10 GB."},
                {"StarCoder2 3B (Q4_K_M)", "3B", "BigCode — code",
                    "https://huggingface.co/bartowski/starcoder2-3b-GGUF/resolve/main/starcoder2-3b-Q4_K_M.gguf",
                    "Compact code completion model. ~2 GB."},
            };
            QString lastFamily;
            for (const auto &e : CATALOG) {
                const QString fam = QString::fromUtf8(e.family);
                if (fam != lastFamily) {
                    if (!lastFamily.isEmpty()) m_modelCombo->insertSeparator(m_modelCombo->count());
                    lastFamily = fam;
                }
                const QString label = QString::fromUtf8(e.label);
                m_modelCombo->addItem(label);
                const int idx = m_modelCombo->count() - 1;
                m_modelCombo->setItemData(idx, label, Qt::UserRole);
                m_modelCombo->setItemData(idx, QString::fromUtf8(e.url), Qt::UserRole + 1);
                m_modelCombo->setItemData(idx,
                    QString("%1 · %2 · %3 GB / RAM\n%4\n\nGGUF URL: %5")
                        .arg(QString::fromUtf8(e.label),
                             QString::fromUtf8(e.params),
                             QString::fromUtf8(e.params).startsWith("1") ? "1-2" :
                             QString::fromUtf8(e.params).startsWith("2") ? "2-3" :
                             QString::fromUtf8(e.params).startsWith("3") ? "3-4" :
                             QString::fromUtf8(e.params).startsWith("7") ? "5-8" : "8+",
                             QString::fromUtf8(e.useCase),
                             QString::fromUtf8(e.url)),
                    Qt::ToolTipRole);
            }
            m_modelCombo->setEnabled(true);
            int restoreIdx = m_modelCombo->findText(prev);
            if (restoreIdx >= 0) m_modelCombo->setCurrentIndex(restoreIdx);
            const QString status = models.isEmpty()
                ? QString("llama.cpp catalog · %1 GGUF models · pick one and run "
                          "`llama-server -m <file>` then click ↻").arg(sizeof(CATALOG)/sizeof(CATALOG[0]))
                : QString("llama.cpp · loaded %1 · plus %2 catalog options below")
                      .arg(models.first()).arg(sizeof(CATALOG)/sizeof(CATALOG[0]));
            setStatus(status, false);
            return;
        }

        // v0.1.53 — curated catalogs for cloud + local OpenAI-compat backends.
        // Prepended ABOVE whatever the live /v1/models returned, so the user
        // sees the recommended picks without losing access to everything else
        // the server reports. Each entry's tooltip explains cost / capability
        // tradeoff so users don't have to memorise model slugs.
        const QString baseUrl = Config::instance().aiBaseUrl;
        const bool isOpenRouter = baseUrl.contains("openrouter", Qt::CaseInsensitive);
        const bool isOpenAI     = baseUrl.contains("openai.com", Qt::CaseInsensitive);

        struct CuratedModel { const char *slug; const char *label; const char *tip; };

        // OpenRouter — top picks across providers; slugs are OpenRouter format.
        // Pricing accurate as of 2026-Q2; user should still cross-check on
        // openrouter.ai/models for current rates.
        static const CuratedModel OPENROUTER_CATALOG[] = {
            {"anthropic/claude-sonnet-4.5",   "Claude Sonnet 4.5  (recommended)",
             "Best balance of speed and reasoning. ~$3 / $15 per M tokens."},
            {"anthropic/claude-opus-4.5",     "Claude Opus 4.5  (smartest)",
             "Anthropic's flagship — slowest but the highest-quality output. ~$15 / $75 per M."},
            {"anthropic/claude-haiku-4.5",    "Claude Haiku 4.5  (fast)",
             "Fastest Claude — great for short tasks. ~$1 / $5 per M."},
            {"openai/gpt-5",                  "GPT-5",
             "OpenAI's flagship reasoning model."},
            {"openai/gpt-5-mini",             "GPT-5 mini  (cheap)",
             "Smaller and cheaper GPT-5 variant."},
            {"openai/gpt-4o",                 "GPT-4o",
             "Multimodal GPT-4 class. ~$2.5 / $10 per M."},
            {"openai/o1-mini",                "o1-mini  (reasoning, cheap)",
             "Reasoning-tuned cheap variant. ~$3 / $12 per M."},
            {"google/gemini-2.5-pro",         "Gemini 2.5 Pro",
             "Google's flagship — large 2 M-token context."},
            {"google/gemini-2.5-flash",       "Gemini 2.5 Flash  (cheap)",
             "Fast cheap Google model."},
            {"deepseek/deepseek-r1",          "DeepSeek R1  (reasoning)",
             "Strong reasoning model — cheap relative to o1."},
            {"meta-llama/llama-3.3-70b-instruct", "Llama 3.3 70B",
             "Meta's open-weights flagship via OpenRouter."},
            {"qwen/qwen-2.5-coder-32b-instruct",  "Qwen2.5-Coder 32B  (code)",
             "Top-tier open-source coding model."},
            {"mistralai/mistral-large",       "Mistral Large",
             "Mistral's flagship — strong on multilingual."},
        };

        // OpenAI direct — current-gen models. Costs in USD per 1M tokens (in/out).
        static const CuratedModel OPENAI_CATALOG[] = {
            {"gpt-5",       "GPT-5  (recommended)",      "OpenAI's flagship."},
            {"gpt-5-mini",  "GPT-5 mini  (cheap)",       "Smaller, cheaper GPT-5."},
            {"gpt-4o",      "GPT-4o",                    "Multimodal GPT-4 class."},
            {"gpt-4o-mini", "GPT-4o mini  (cheapest)",   "Tiny GPT-4o."},
            {"o1",          "o1  (reasoning)",           "Premier reasoning model."},
            {"o1-mini",     "o1-mini  (reasoning, cheap)", "Cheap reasoning model."},
        };

        const CuratedModel *catalog = nullptr;
        size_t catalog_n = 0;
        QString catalog_label;
        if (isOpenRouter) { catalog = OPENROUTER_CATALOG; catalog_n = sizeof(OPENROUTER_CATALOG)/sizeof(OPENROUTER_CATALOG[0]); catalog_label = "OpenRouter recommended"; }
        else if (isOpenAI) { catalog = OPENAI_CATALOG;     catalog_n = sizeof(OPENAI_CATALOG)/sizeof(OPENAI_CATALOG[0]);    catalog_label = "OpenAI"; }

        if (catalog) {
            // Header
            m_modelCombo->addItem(QString("── %1 ──").arg(catalog_label));
            qobject_cast<QStandardItemModel *>(m_modelCombo->model())
                ->item(m_modelCombo->count() - 1)->setEnabled(false);
            for (size_t i = 0; i < catalog_n; ++i) {
                m_modelCombo->addItem(QString::fromUtf8(catalog[i].label));
                const int idx = m_modelCombo->count() - 1;
                m_modelCombo->setItemData(idx, QString::fromUtf8(catalog[i].slug), Qt::UserRole);
                m_modelCombo->setItemData(idx, QString::fromUtf8(catalog[i].tip), Qt::ToolTipRole);
            }
            if (!models.isEmpty()) {
                m_modelCombo->insertSeparator(m_modelCombo->count());
                m_modelCombo->addItem("── all available ──");
                qobject_cast<QStandardItemModel *>(m_modelCombo->model())
                    ->item(m_modelCombo->count() - 1)->setEnabled(false);
                m_modelCombo->addItems(models);
            }
            m_modelCombo->setEnabled(true);
            // Default to first curated entry (skip the disabled header).
            if (m_modelCombo->count() > 1) m_modelCombo->setCurrentIndex(1);
            setStatus(QString("%1 — %2 curated picks · %3 total")
                          .arg(catalog_label).arg(catalog_n).arg(models.size() + (int)catalog_n), false);
            return;
        }

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
    // v0.1.54 — dispatch the offline message by backend AND keep the
    // curated catalog usable for backends where it makes sense even
    // without a live probe (llama.cpp catalog, OpenRouter / OpenAI
    // curated picks). Pre-fix this handler unconditionally cleared and
    // disabled the combo, killing the catalog logic added in v0.1.53.
    connect(m_ollama, &OllamaClient::modelsError, this, [this](const QString &reason) {
        Q_UNUSED(reason);
        const QString backend = Config::instance().aiBackend;
        const QString baseUrl = Config::instance().aiBaseUrl;
        const bool isLlamaCpp   = backend.compare("llama.cpp", Qt::CaseInsensitive) == 0;
        const bool isOpenRouter = baseUrl.contains("openrouter", Qt::CaseInsensitive);
        const bool isOpenAI     = baseUrl.contains("openai.com", Qt::CaseInsensitive);

        if (isLlamaCpp || isOpenRouter || isOpenAI) {
            // For these backends, the model list is *largely* a static
            // catalog — we don't need the live /v1/models response to
            // populate it. Re-emit a synthetic empty modelsListed so the
            // catalog-rendering branch in the success handler runs.
            emit m_ollama->modelsListed(QStringList());
            setStatus(isLlamaCpp
                  ? tr("llama.cpp not running — pick a model below and run "
                       "`llama-server -m <file>`")
                  : isOpenRouter
                      ? tr("OpenRouter — paste your API key to enable "
                           "billing-aware live model list")
                      : tr("OpenAI — paste your API key to enable live model list"),
                false);
            return;
        }

        // Default = Ollama offline. (LM Studio / Jan paths were removed in
        // v0.1.54 since those backends are no longer in the dropdown.)
        m_modelCombo->clear();
        m_modelCombo->addItem(QStringLiteral("(Ollama offline)"));
        m_modelCombo->setEnabled(false);
        setStatus(tr("Run `ollama serve` (or start the Ollama app); then "
                     "click ↻ to refresh"), true);
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
    // v0.1.40 strict-prompt format-fix actions.
    connect(fixJsonBtn, &QPushButton::clicked, this, [this]() { sendPrompt("fix-json"); });
    connect(fixHtmlBtn, &QPushButton::clicked, this, [this]() { sendPrompt("fix-html"); });
    connect(fixSqlBtn,  &QPushButton::clicked, this, [this]() { sendPrompt("fix-sql"); });
    connect(sendBtn, &QPushButton::clicked, this, [this]() {
        if (!m_customInput->toPlainText().trimmed().isEmpty()) sendPrompt("custom");
    });
    // Enter-to-send wiring lives in eventFilter() — the event filter
    // intercepts Return (no modifier) on the QPlainTextEdit and fires
    // this click. Shift+Return falls through and inserts a newline.
    connect(m_stopBtn, &QPushButton::clicked, m_ollama, &OllamaClient::cancel);
    // OllamaClient::cancel() disconnects + aborts the reply silently — no
    // finished/error signal fires after that. So the streaming card would
    // stay active with the live-stats timer still ticking. Hook the stop
    // button to also end the bubble cleanly + stop the timer.
    connect(m_stopBtn, &QPushButton::clicked, this, [this]() {
        if (m_inAssistantBubble) {
            m_stopBtn->setEnabled(false);
            endAssistantBubble();
        }
    });
    connect(insertBtn, &QPushButton::clicked, this, [this]() {
        if (!m_lastResponse.isEmpty()) emit insertText(m_lastResponse);
    });
    connect(replaceBtn, &QPushButton::clicked, this, [this]() {
        if (!m_lastResponse.isEmpty()) emit replaceSelection(m_lastResponse);
    });
    connect(copyBtn, &QPushButton::clicked, this, [this]() {
        if (!m_lastResponse.isEmpty()) QApplication::clipboard()->setText(m_lastResponse);
    });

    // ─── Initial mode (v0.1.48) ──────────────────────────────────────────
    // All UI is now constructed (m_customInput, m_quickActionsWrap,
    // m_chatLayout, etc.). Picking the initial mode here fires the
    // QButtonGroup::toggled handler, which calls the applyMode lambda that
    // syncs every dependent widget. Persisted aiDataMode wins; otherwise
    // Chat is the default.
    if (Config::instance().aiDataMode && m_dataMode) {
        m_dataMode->setChecked(true);
    } else if (m_chatMode) {
        m_chatMode->setChecked(true);
    }
}

void AIPanel::setContext(const QString &selectedText, const QString &filePath, const QString &language) {
    // Legacy 3-arg path — keep working for callers that haven't moved to
    // setWorkspaceContext yet. We treat the passed selected text as both
    // the selection context AND the "current file text" so older call
    // sites still produce the same prompt as before.
    m_context = selectedText;
    m_contextIsSelection = !selectedText.isEmpty();
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
    //
    // v0.1.38: track whether m_context is a real selection or a whole-file
    // fallback. The "custom" chat action uses m_contextIsSelection to
    // decide whether to inline m_context into the prompt — pre-v0.1.38 it
    // ALWAYS appended m_context, which meant a casual "hi" got the entire
    // current file dumped after it. Quick-action templates (Explain etc.)
    // still always inline m_context because they need code context to make
    // sense.
    m_contextIsSelection = !selectedText.isEmpty();
    m_context = m_contextIsSelection ? selectedText : currentFileText;
    m_language = language;
    m_currentFilePath = currentFilePath;
    m_currentFileText = currentFileText;
    m_openTabs = openTabs;
    const QString prevWorkspace = m_workspaceRoot;
    m_workspaceRoot = workspaceRoot;
    m_workspaceFilePaths = workspaceFilePaths;

    // v0.1.39 — when the workspace root changes, swap to the per-workspace
    // chat history file. (Re-)compute the on-disk path; if a saved file
    // exists, load it and re-render the transcript.
    if (workspaceRoot != prevWorkspace) {
        updateChatHistoryPath();
        if (!m_chatHistoryPath.isEmpty() && QFileInfo::exists(m_chatHistoryPath)) {
            loadChatHistory();
            renderTranscript();
        }
    }
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
    // Cursor / Copilot / ChatGPT semantics for the multi-line input:
    //   Enter          → send
    //   Shift+Enter    → insert a newline (default QPlainTextEdit behaviour)
    //   Ctrl+Enter     → send (alias, for people used to Slack / Linear)
    if (obj == m_customInput && evt->type() == QEvent::KeyPress) {
        auto *ke = static_cast<QKeyEvent*>(evt);
        const int key = ke->key();
        const Qt::KeyboardModifiers mods = ke->modifiers();
        const bool isEnter = (key == Qt::Key_Return || key == Qt::Key_Enter);
        if (isEnter && !(mods & Qt::ShiftModifier)) {
            if (!m_customInput->toPlainText().trimmed().isEmpty())
                sendPrompt("custom");
            return true;   // swallow the keypress; don't insert a newline
        }
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

    const QString cur = m_customInput->toPlainText().trimmed();
    if (!cur.isEmpty())
        m_customInput->setPlainText(cur + " " + transcript);
    else
        m_customInput->setPlainText(transcript);

    m_customInput->setFocus();
    // Caret to end so the user keeps typing after the inserted transcript.
    QTextCursor c = m_customInput->textCursor();
    c.movePosition(QTextCursor::End);
    m_customInput->setTextCursor(c);
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

    // Build the system prompt via the layered builder in ai_systemprompt.cpp.
    // The builder composes: identity + anti-tool-call + mode-specific +
    // language hint. Coding Mode -> CodingStrict intent which preserves the
    // legacy code-only prompt verbatim. The anti-tool-call layer is what
    // suppresses the {"command":...,"output":...} drift that tool-calling
    // models (Qwen3, Qwen3.5, Hermes-3, Llama 3.1+) produce by default
    // when they see anything that looks like an agent frame.
    const bool codingMode = (m_codingMode && m_codingMode->isChecked());
    const bool dataMode   = (m_dataMode   && m_dataMode->isChecked());
    const AiSystemPrompt::Intent intent =
        AiSystemPrompt::classifyIntent(action, codingMode, dataMode);
    // Predict whether tools will fire so the system prompt swaps in the
    // tool-mode preamble (instead of the anti-tool-call layer that
    // would otherwise contradict the tools we're about to attach).
    // v0.1.43 — Data Mode also uses tools (csv_query, query_sql); the
    // tool decision is the same model-allowlist test.
    const bool toolModeActive = (codingMode || dataMode);
    const bool willUseTools = toolModeActive && !m_workspaceRoot.isEmpty()
        && ((m_ollama->backend() != OllamaClient::Ollama)
            || AiTools::modelLikelySupportsTools(m_ollama->model()));
    QString systemPrompt;
    if (intent == AiSystemPrompt::Intent::DataAnalyst) {
        // Pull .notepatra/data-analyst.md (if present) so the model gets
        // project-specific instructions automatically.
        const QString projectCtx =
            AiSystemPrompt::readDataAnalystInstructions(m_workspaceRoot);
        systemPrompt = AiSystemPrompt::buildWithProjectContext(
            intent, m_language, willUseTools, projectCtx);
    } else {
        systemPrompt = AiSystemPrompt::build(intent, m_language, willUseTools);
    }

    // v0.1.40 — JSON / HTML / SQL "fix my X" intent detection. When the
    // user types "fix my json" (etc.) in the chat input, swap in the
    // strict-patcher system prompt so small models stop "improving" the
    // payload (adding fields, restructuring, hallucinating). Only fires
    // for the "custom" action (i.e. user-typed prompts, not quick
    // actions); doesn't run when Coding-Mode tools are active because
    // the agent loop handles file edits itself.
    if (action == "custom" && !willUseTools) {
        const QString chatText = m_customInput->toPlainText();
        AiIntent::FixKind fixKind = AiIntent::detectFixIntent(chatText);
        if (fixKind != AiIntent::FixKind::None) {
            systemPrompt = AiIntent::strictFixSystemPrompt(fixKind);
        }
    } else if (action == "fix-json") {
        systemPrompt = AiIntent::strictFixSystemPrompt(AiIntent::FixKind::Json);
    } else if (action == "fix-html") {
        systemPrompt = AiIntent::strictFixSystemPrompt(AiIntent::FixKind::Html);
    } else if (action == "fix-sql") {
        systemPrompt = AiIntent::strictFixSystemPrompt(AiIntent::FixKind::Sql);
    }

    // Build the prompt + the user-visible prompt label (just the action name
    // + the code snippet for context — no need to dump the verbose template
    // text into the user bubble). customUserText captures just the raw text
    // the user typed for the "custom" action; the workspace-gate heuristic
    // reads it without the appended code context.
    QString prompt;
    QString userBubbleText;
    QString customUserText;
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
    } else if (action == "fix-json" || action == "fix-html" || action == "fix-sql") {
        // v0.1.40 strict format fix. The system prompt is overridden a
        // few lines below to AiIntent::strictFixSystemPrompt so the
        // model gets the same minimal-change rules as Tools → JSON Tools.
        QString format = (action == "fix-json") ? "JSON"
                       : (action == "fix-html") ? "HTML" : "SQL";
        prompt = "Fix ONLY the broken parts of this " + format
               + ". Make MINIMAL changes. PRESERVE the original line order, "
                 "key/element order, and formatting. Do NOT reorder, do NOT "
                 "reformat, do NOT add new content. Return ONLY the corrected "
                 + format + ".\n\nBROKEN " + format + ":\n" + m_context;
        userBubbleText = "Fix " + format + " (minimal change):\n\n" + m_context;
    } else if (action == "custom") {
        const QString userText = m_customInput->toPlainText();
        customUserText = userText;
        // v0.1.38 — only inline m_context if it's a REAL user selection.
        // Pre-v0.1.38 every casual chat appended the entire open file
        // (because m_context falls back to the full file when there's no
        // selection). Now: selection → inline as fenced code; no selection
        // → just the user's text. Workspace-level questions about the
        // file still benefit from the workspace-context block via the
        // shouldAttachWorkspace gate; or use Coding Mode's read_file tool
        // for explicit file access.
        if (m_contextIsSelection && !m_context.isEmpty()) {
            prompt = userText + "\n\n```\n" + m_context + "\n```";
            userBubbleText = userText + "\n\n" + m_context;
        } else {
            prompt = userText;
            userBubbleText = userText;
        }
        m_customInput->clear();
    }

    // ─── Resolve attached file (if any) → image base64 OR appended text ──
    QStringList imagesBase64;
    if (!m_pendingFilePath.isEmpty()) {
        QString imageB64;
        QString reason;
        // v0.1.43 — when Data Mode is on AND the attachment is a CSV, swap
        // the raw-text dump for a compact schema-aware preview so a 50 MB
        // CSV doesn't blow the model's context window. The model can still
        // query the full file via the csv_query tool.
        QString fileText;
        if (dataMode && CsvAnalyst::looksLikeCsv(m_pendingFilePath)) {
            fileText = CsvAnalyst::buildPreviewText(m_pendingFilePath);
        } else {
            fileText = extractFileContent(m_pendingFilePath, m_pendingFileKind, imageB64, reason);
        }
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
            const QString icon = (dataMode && CsvAnalyst::looksLikeCsv(m_pendingFilePath))
                                  ? QStringLiteral("📊") : QStringLiteral("📄");
            userBubbleText = QString("[%1 %2]\n%3").arg(icon, fi.fileName(), userBubbleText);
        } else if (!reason.isEmpty()) {
            setStatus("✗ attachment error: " + reason, true);
        }

        // Clear the attachment after sending so the next message is fresh
        m_pendingFilePath.clear();
        m_pendingFileKind.clear();
        m_attachmentChip->setVisible(false);
        m_attachmentChip->setFixedHeight(0);
    }

    // Prepend the workspace-awareness block IF this scenario actually
    // benefits from it. The gating function says no for:
    //   * Coding Mode (code-only output, workspace just bloats prompt)
    //   * Explain / Transform with a non-empty selection (selection IS context)
    //   * Chat with a casual greeting ("hi", "thanks") -- the load-bearing
    //     fix for the qwen3.5:0.8b / 2b "{"command":...}" hallucination
    //
    // It still attaches workspace context for project-level questions
    // like "show me my files" and code-shaped chat ("how do I import X?").
    const bool attachWorkspace =
        AiSystemPrompt::shouldAttachWorkspace(intent, m_context, customUserText);
    if (attachWorkspace) {
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

    // v0.1.35 — agent-loop activation. Tools fire when:
    //   1. Coding Mode is on (the user has explicitly opted into agent
    //      behaviour — read_file in non-coding chat would be jarring), AND
    //   2. EITHER the active model is in the Ollama tool-allowlist
    //      (qwen3, llama3.1+, hermes3, mistral-nemo, granite3, gpt-oss,
    //      etc.) OR the backend is OpenAI-compat (in which case we
    //      always send tools — OpenRouter / OpenAI / Anthropic / vLLM /
    //      LM Studio handle support detection server-side and ignore
    //      the field for non-tool models).
    //   3. The user actually has a workspace root open (no point
    //      offering file tools without a workspace).
    QJsonArray toolsForRequest;
    m_pendingToolResults = QJsonArray();
    m_toolCallsThisTurn = 0;
    m_toolCallsTotal = 0;
    m_toolsActiveThisTurn = false;
    if (toolModeActive && !m_workspaceRoot.isEmpty()) {
        const bool likelyOk =
            (m_ollama->backend() != OllamaClient::Ollama)
            || AiTools::modelLikelySupportsTools(m_ollama->model());
        if (likelyOk) {
            toolsForRequest = AiTools::availableTools();
            m_toolsActiveThisTurn = true;
            m_lastSystemPromptForTools = systemPrompt;
            m_lastToolsArray = toolsForRequest;
        }
    }

    m_ollama->generate(prompt, systemPrompt, m_thinkingCheck->isChecked(),
                       imagesBase64, toolsForRequest);
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

    if (m_chatLayout) aiClearChat(m_chatLayout);
    m_dataWelcomeFrame = nullptr;  // wiped by aiClearChat above; clear our pointer to avoid use-after-free
    m_currentAssistantText.clear();
    m_inAssistantBubble = false;
    m_streamingCard = nullptr;
    m_streamingBody = nullptr;
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
    // v0.1.39 — also delete the on-disk persisted history for this
    // workspace. Reset means start fresh on next launch too.
    if (!m_chatHistoryPath.isEmpty() && QFileInfo::exists(m_chatHistoryPath))
        QFile::remove(m_chatHistoryPath);
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

// ───────────────────────────────────────────────────────────────────────
// Widget-based chat rendering — bulletproof bubbles
// ───────────────────────────────────────────────────────────────────────
//
// Each message is a standalone QFrame styled via widget QSS (which Qt
// renders fully and reliably), not HTML-CSS in a QTextBrowser (which
// silently drops nested backgrounds). The rendering cost is comparable
// for our message volumes; the visual certainty is the whole point.

static void aiClearChat(QVBoxLayout *layout) {
    if (!layout) return;
    // Remove all items except the trailing stretch (last item).
    while (layout->count() > 1) {
        QLayoutItem *it = layout->takeAt(0);
        if (QWidget *w = it->widget()) w->deleteLater();
        delete it;
    }
}

// User bubble — right-aligned, accent fill, rounded pill. Uses an outer
// QHBoxLayout with stretch-on-left to push the bubble to the right edge.
static void aiAddUserBubble(QVBoxLayout *target, const QString &text,
                            const AiPalette &pal) {
    auto *row = new QWidget;
    row->setStyleSheet("background: transparent;");
    auto *rowLay = new QHBoxLayout(row);
    rowLay->setContentsMargins(0, 0, 0, 10);
    rowLay->addStretch(1);

    auto *pill = new QFrame;
    pill->setObjectName("userPill");
    pill->setStyleSheet(QString(
        "#userPill { background: %1; border-radius: 14px; }")
        .arg(pal.userBg));
    auto *pillLay = new QVBoxLayout(pill);
    pillLay->setContentsMargins(14, 10, 14, 10);
    pillLay->setSpacing(0);
    auto *msg = new QLabel(text);
    msg->setWordWrap(true);
    msg->setTextInteractionFlags(Qt::TextSelectableByMouse);
    msg->setStyleSheet(QString(
        "color: %1; font-size: 13px; font-weight: 500; background: transparent;")
        .arg(pal.userFg));
    msg->setMaximumWidth(480);
    pillLay->addWidget(msg);

    rowLay->addWidget(pill, 0, Qt::AlignRight);

    target->insertWidget(target->count() - 1, row);
}

// Assistant card — distinct background, accent left stripe, model label,
// copy button, QTextBrowser body with markdown rendering.
static QFrame *aiAddAssistantCard(QVBoxLayout *target,
                                  const AIPanel::ChatMessage &msg,
                                  int messageIndex,
                                  const AiPalette &pal,
                                  std::function<void(int)> copyCb,
                                  QTextBrowser **outBody) {
    auto *card = new QFrame;
    card->setObjectName("assistantCard");
    card->setStyleSheet(QString(
        "#assistantCard { "
        "  background: %1; "
        "  border: 1px solid %2; "
        "  border-left: 4px solid %3; "
        "  border-radius: 8px; "
        "} "
        "QLabel { background: transparent; }")
        .arg(pal.assistBg, pal.assistBorder, pal.assistAccent));

    auto *outer = new QVBoxLayout(card);
    outer->setContentsMargins(14, 12, 14, 12);
    outer->setSpacing(8);

    auto *headerRow = new QHBoxLayout;
    headerRow->setContentsMargins(0, 0, 0, 0);
    auto *modelLbl = new QLabel(msg.model.isEmpty() ? QStringLiteral("AI") : msg.model.toUpper());
    modelLbl->setStyleSheet(QString(
        "color: %1; font-size: 10px; font-weight: 700; letter-spacing: 1.2px;")
        .arg(pal.assistAccent));
    headerRow->addWidget(modelLbl);
    headerRow->addStretch(1);
    auto *copyBtn = new QPushButton("⧉ copy");
    copyBtn->setCursor(Qt::PointingHandCursor);
    copyBtn->setFlat(true);
    copyBtn->setStyleSheet(QString(
        "QPushButton { "
        "  color: %1; font-size: 10px; font-weight: 600; "
        "  padding: 3px 10px; border: 1px solid %2; border-radius: 10px; "
        "  background: transparent; "
        "} "
        "QPushButton:hover { background: %1; color: white; border: 1px solid %1; }")
        .arg(pal.accent, pal.assistBorder));
    QObject::connect(copyBtn, &QPushButton::clicked, card, [copyCb, messageIndex]() {
        copyCb(messageIndex);
    });
    headerRow->addWidget(copyBtn);
    outer->addLayout(headerRow);

    auto *divider = new QFrame;
    divider->setFrameShape(QFrame::HLine);
    divider->setStyleSheet(QString(
        "background: %1; border: none; max-height: 1px;")
        .arg(pal.assistBorder));
    outer->addWidget(divider);

    // Per-response stats line — same "⏱ N tok · X tok/s · Y s" format
    // used by the live streaming label so the bubble keeps the same
    // visual after the stream ends (instead of the stats vanishing).
    // Hidden when no stats reported yet (placeholder card during stream
    // start, before the first token).
    if (msg.elapsedMs >= 0 || msg.evalTokens > 0) {
        const qint64 ms = msg.elapsedMs >= 0 ? msg.elapsedMs : 0;
        const double secs = ms / 1000.0;
        QString text;
        if (msg.evalTokens > 0 && ms > 200) {
            const double tps = msg.evalTokens * 1000.0 / ms;
            text = QString("⏱ %1 tok · %2 tok/s · %3 s")
                       .arg(msg.evalTokens)
                       .arg(QString::number(tps, 'f', 1))
                       .arg(QString::number(secs, 'f', 1));
        } else if (msg.evalTokens > 0) {
            text = QString("⏱ %1 tok · %2 s")
                       .arg(msg.evalTokens)
                       .arg(QString::number(secs, 'f', 1));
        } else {
            text = QString("⏱ %1 s").arg(QString::number(secs, 'f', 1));
        }
        auto *statsLbl = new QLabel(text);
        statsLbl->setStyleSheet(QString(
            "color: %1; font-size: 10px; font-weight: 500; "
            "letter-spacing: 0.3px; padding: 0; opacity: 0.8;")
            .arg(pal.linkFg));
        outer->addWidget(statsLbl);
    }

    auto *body = new QTextBrowser;
    body->setReadOnly(true);
    body->setOpenLinks(false);
    body->setFrameShape(QFrame::NoFrame);
    body->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    body->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    body->setStyleSheet(QString(
        "QTextBrowser { background: transparent; color: %1; border: none; padding: 0; }")
        .arg(pal.assistFg));
    // Render the message — markdown for assistant text.
    const QString bodyHtml = markdownBodyHtml(msg.text, messageIndex);
    body->setHtml(bodyHtml);
    // Size the body to fit its content so the card grows with the text
    // and the outer QScrollArea handles overflow.
    QObject::connect(body->document(), &QTextDocument::contentsChanged, card,
        [body]() {
            body->document()->setTextWidth(body->viewport()->width());
            const int h = int(body->document()->size().height()) + 8;
            body->setFixedHeight(qMax(30, h));
        });
    body->document()->setTextWidth(400);
    body->setFixedHeight(qMax(30, int(body->document()->size().height()) + 8));
    outer->addWidget(body);

    // ── v0.1.43 — render any ```chart fenced JSON specs as real QCharts ──
    // The model is instructed (via the DataAnalyst system prompt) to emit
    // a fenced ```chart {...} block when a visualization helps. We parse
    // each one and append a QChartView widget under the prose body so the
    // user sees a real interactive chart alongside the explanation.
    {
        static const QRegularExpression kChartFence(
            QStringLiteral("```chart\\s*\\n([\\s\\S]*?)```"));
        QRegularExpressionMatchIterator it = kChartFence.globalMatch(msg.text);
        while (it.hasNext()) {
            const QRegularExpressionMatch m = it.next();
            const QString json = m.captured(1).trimmed();
            QString chartErr;
            QWidget *chart = ChartRender::renderFromSpec(json, card, &chartErr);
            if (chart) {
                outer->addWidget(chart);
            } else if (!chartErr.isEmpty()) {
                auto *errLbl = new QLabel(QStringLiteral("⚠ Chart spec error: ") + chartErr);
                errLbl->setStyleSheet("color: #c0392b; font-size: 10px; font-style: italic;");
                errLbl->setWordWrap(true);
                outer->addWidget(errLbl);
            }
        }
    }

    // Bottom margin between cards
    auto *row = new QWidget;
    row->setStyleSheet("background: transparent;");
    auto *rowLay = new QVBoxLayout(row);
    rowLay->setContentsMargins(0, 0, 0, 12);
    rowLay->setSpacing(0);
    rowLay->addWidget(card);

    target->insertWidget(target->count() - 1, row);
    if (outBody) *outBody = body;
    return card;
}

static void aiAddErrorCard(QVBoxLayout *target, const QString &text,
                           const AiPalette &pal) {
    auto *card = new QFrame;
    card->setObjectName("errorCard");
    card->setStyleSheet(QString(
        "#errorCard { "
        "  background: %1; color: %2; "
        "  border: 1px solid %3; border-left: 4px solid %3; "
        "  border-radius: 8px; "
        "} "
        "QLabel { background: transparent; color: %2; }")
        .arg(pal.errBg, pal.errFg, pal.errBorder));
    auto *lay = new QVBoxLayout(card);
    lay->setContentsMargins(14, 12, 14, 12);
    lay->setSpacing(4);
    auto *lbl = new QLabel("ERROR");
    lbl->setStyleSheet(QString(
        "color: %1; font-size: 10px; font-weight: 700; letter-spacing: 1.2px;")
        .arg(pal.errBorder));
    lay->addWidget(lbl);
    auto *body = new QLabel(text);
    body->setWordWrap(true);
    body->setTextInteractionFlags(Qt::TextSelectableByMouse);
    lay->addWidget(body);

    auto *row = new QWidget;
    row->setStyleSheet("background: transparent;");
    auto *rowLay = new QVBoxLayout(row);
    rowLay->setContentsMargins(0, 0, 0, 12);
    rowLay->setSpacing(0);
    rowLay->addWidget(card);
    target->insertWidget(target->count() - 1, row);
}

void AIPanel::renderTranscript() {
    if (!m_chatLayout) return;
    // v0.1.38 crash fix: stop the live streaming-stats timer + nullify the
    // QLabel pointer BEFORE we delete the streaming card. aiClearChat
    // deleteLater()s every widget in m_chatLayout including m_streamingCard
    // and its child m_streamingStats. Without this stop+nullify, the
    // 250ms timer kept firing on the dangling QLabel pointer (the
    // existing `if (!m_streamingStats) return` check doesn't catch a
    // dangling-but-non-null pointer) → use-after-free crash on
    // setText(). Reproduces by clicking Coding Mode mid-stream.
    if (m_streamingStatsTimer) m_streamingStatsTimer->stop();
    m_streamingStats = nullptr;
    m_streamingTokenCount = 0;
    m_streamingStartMs = 0;
    aiClearChat(m_chatLayout);
    m_dataWelcomeFrame = nullptr;  // pointed at a now-deleteLater'd widget
    m_streamingCard = nullptr;
    m_streamingBody = nullptr;

    const AiPalette pal = aiPalette();
    auto copyCb = [this](int index) {
        if (index >= 0 && index < m_messages.size()) {
            QApplication::clipboard()->setText(m_messages.at(index).text);
            setStatus("✓ Response copied to clipboard", false);
        }
    };
    for (int i = 0; i < m_messages.size(); ++i) {
        const ChatMessage &m = m_messages.at(i);
        if (m.role == ChatMessage::User) {
            aiAddUserBubble(m_chatLayout, m.text, pal);
        } else if (m.role == ChatMessage::Error) {
            aiAddErrorCard(m_chatLayout, m.text, pal);
        } else {
            QTextBrowser *body = nullptr;
            aiAddAssistantCard(m_chatLayout, m, i, pal, copyCb, &body);
            if (body) {
                connect(body, &QTextBrowser::anchorClicked,
                        this, &AIPanel::handleChatLink);
            }
        }
    }
    // v0.1.53 — re-render the Data Analyst welcome card if we're in Data
    // mode with an empty chat and the user hasn't dismissed it. Covers the
    // "Reset" path (m_messages cleared → renderTranscript) and the initial
    // first-mode-toggle path.
    if (m_messages.isEmpty()
        && m_dataMode && m_dataMode->isChecked()
        && !Config::instance().aiHideDataWelcome) {
        renderDataWelcomeCard();
    }

    // Scroll to bottom after layout settles
    QTimer::singleShot(0, m_chatArea, [this]() {
        if (m_chatArea) {
            m_chatArea->verticalScrollBar()->setValue(
                m_chatArea->verticalScrollBar()->maximum());
        }
    });
}

void AIPanel::appendErrorBubble(const QString &text) {
    ChatMessage message;
    message.role = ChatMessage::Error;
    message.text = text;
    m_messages.push_back(message);
    scheduleChatSave();
    renderTranscript();
}

// ─────────────────────────────────────────────────────────────────────────────
// Data Analyst welcome card (v0.1.53)
//
// When the user toggles into Data mode for the first time on a fresh chat,
// pop a rich card at the top of the chat area that explains what Data mode
// does, shows the current model's capability check, lists saved DB
// connections, and offers three clickable example prompts to seed the input.
//
// The card is removed automatically once any chat content exists or the
// user clicks "Hide". The "Hide" choice is sticky via Config::aiHideDataWelcome.
// ─────────────────────────────────────────────────────────────────────────────
void AIPanel::renderDataWelcomeCard() {
    if (!m_chatLayout) return;
    if (m_dataWelcomeFrame) return;  // already shown

    const AiPalette pal = aiPalette();
    const QString modelName = m_modelCombo ? m_modelCombo->currentText() : QString();
    const bool capable = AiTools::modelCapableOfDataAnalysis(modelName);
    const int connCount = DbConnections::loadAll().size();

    auto *card = new QFrame;
    card->setFrameShape(QFrame::StyledPanel);
    card->setStyleSheet(QString(
        "QFrame { background: %1; border: 1px solid #FF9F43; border-radius: 10px; }"
        "QLabel { background: transparent; color: %2; }")
        .arg(pal.assistBg, pal.chatFg));

    auto *cardLay = new QVBoxLayout(card);
    cardLay->setContentsMargins(16, 14, 16, 14);
    cardLay->setSpacing(10);

    // Title row
    auto *titleRow = new QHBoxLayout;
    titleRow->setSpacing(8);
    auto *title = new QLabel(QStringLiteral("📊  Data Analyst Mode"));
    {
        QFont f = title->font();
        f.setPointSize(f.pointSize() + 2);
        f.setBold(true);
        title->setFont(f);
        title->setStyleSheet("color: #FF9F43;");
    }
    titleRow->addWidget(title, 1);
    auto *hideBtn = new QPushButton(QStringLiteral("Hide"));
    hideBtn->setFlat(true);
    hideBtn->setCursor(Qt::PointingHandCursor);
    hideBtn->setToolTip("Don't show this card again — you can re-enable it from Preferences");
    hideBtn->setStyleSheet(QString(
        "QPushButton { color: %1; background: transparent; border: none; "
        "padding: 2px 8px; font-size: 11px; }"
        "QPushButton:hover { color: %2; }")
        .arg(pal.muted, pal.accent));
    connect(hideBtn, &QPushButton::clicked, this, [this]() {
        Config::instance().aiHideDataWelcome = true;
        Config::instance().save();
        removeDataWelcomeCard();
    });
    titleRow->addWidget(hideBtn);
    cardLay->addLayout(titleRow);

    // One-paragraph explainer
    auto *blurb = new QLabel(QStringLiteral(
        "Ask questions about CSVs and saved database connections. The AI "
        "can run real SQL, summarise tables, and render charts inline."));
    blurb->setWordWrap(true);
    blurb->setStyleSheet(QString("color: %1; font-size: 12px;").arg(pal.chatFg));
    cardLay->addWidget(blurb);

    // Example prompt chips — clicking fills the input + focuses it.
    auto *chips = new QVBoxLayout;
    chips->setSpacing(6);
    static const char *EXAMPLES[] = {
        "Top 10 customers by revenue last quarter",
        "Plot monthly signups for 2024 as a line chart",
        "Schema of users table — find duplicate emails",
    };
    for (const char *prompt : EXAMPLES) {
        auto *chip = new QPushButton(QString::fromUtf8("›  ") + QString::fromUtf8(prompt));
        chip->setCursor(Qt::PointingHandCursor);
        chip->setStyleSheet(QString(
            "QPushButton { background: %1; color: %2; border: 1px solid %3; "
            "border-radius: 6px; padding: 6px 10px; text-align: left; font-size: 12px; }"
            "QPushButton:hover { background: %4; border-color: #FF9F43; color: #FF9F43; }")
            .arg(pal.codeBg, pal.chatFg, pal.assistBorder, pal.btnHover));
        const QString text = QString::fromUtf8(prompt);
        connect(chip, &QPushButton::clicked, this, [this, text]() {
            if (m_customInput) {
                m_customInput->setPlainText(text);
                m_customInput->setFocus();
                QTextCursor c = m_customInput->textCursor();
                c.movePosition(QTextCursor::End);
                m_customInput->setTextCursor(c);
            }
        });
        chips->addWidget(chip);
    }
    cardLay->addLayout(chips);

    // Status row — connection count + model capability.
    auto *status = new QVBoxLayout;
    status->setSpacing(4);

    auto *connRow = new QHBoxLayout;
    connRow->setSpacing(6);
    auto *connLabel = new QLabel(connCount == 0
        ? QStringLiteral("🔌  <b>No connections saved.</b>")
        : QString::fromUtf8("🔌  <b>%1 connection%2 saved.</b>")
              .arg(connCount).arg(connCount == 1 ? "" : "s"));
    connLabel->setTextFormat(Qt::RichText);
    connLabel->setStyleSheet(QString("color: %1; font-size: 11px;").arg(pal.muted));
    connRow->addWidget(connLabel);
    connRow->addStretch();
    auto *manageBtn = new QPushButton(QStringLiteral("Manage Connections…"));
    manageBtn->setCursor(Qt::PointingHandCursor);
    manageBtn->setStyleSheet(QString(
        "QPushButton { background: #FF9F43; color: white; border: none; "
        "border-radius: 4px; padding: 4px 10px; font-size: 11px; font-weight: 600; }"
        "QPushButton:hover { background: #FFA94D; }"));
    connect(manageBtn, &QPushButton::clicked, this, [this]() {
        DbConnectionsDialog dlg(this);
        dlg.exec();
        // Refresh the card so the connection count updates.
        if (m_dataWelcomeFrame) {
            removeDataWelcomeCard();
            renderDataWelcomeCard();
        }
    });
    connRow->addWidget(manageBtn);
    status->addLayout(connRow);

    auto *modelLabel = new QLabel(capable
        ? QString::fromUtf8("🤖  <b>%1</b> &nbsp;·&nbsp; <span style='color:#3FB950'>capable for Data mode ✓</span>")
              .arg(modelName.isEmpty() ? QStringLiteral("(no model selected)") : modelName)
        : QString::fromUtf8("🤖  <b>%1</b> &nbsp;·&nbsp; <span style='color:#FF9F43'>too small for multi-table SQL ⚠</span>")
              .arg(modelName.isEmpty() ? QStringLiteral("(no model selected)") : modelName));
    modelLabel->setTextFormat(Qt::RichText);
    modelLabel->setStyleSheet(QString("color: %1; font-size: 11px;").arg(pal.muted));
    status->addWidget(modelLabel);

    if (!capable) {
        auto *fix = new QLabel(QStringLiteral(
            "&nbsp;&nbsp;&nbsp;<i>Pull a stronger local model:</i> "
            "<code>ollama pull qwen2.5-coder:14b</code> &nbsp;(≈9 GB) "
            "<br>&nbsp;&nbsp;&nbsp;<i>Or use a cloud model:</i> "
            "Claude Sonnet 4.5 · GPT-5 · Gemini 2.5 Pro · DeepSeek-V3"));
        fix->setTextFormat(Qt::RichText);
        fix->setWordWrap(true);
        fix->setStyleSheet(QString("color: %1; font-size: 11px;").arg(pal.muted));
        status->addWidget(fix);
    }

    cardLay->addLayout(status);

    // Insert at the top of m_chatLayout (above the trailing stretch).
    // m_chatLayout last item is the stretch (added in ctor at line ~979).
    m_chatLayout->insertWidget(0, card);
    m_dataWelcomeFrame = card;
}

void AIPanel::removeDataWelcomeCard() {
    if (!m_dataWelcomeFrame) return;
    m_dataWelcomeFrame->setParent(nullptr);
    m_dataWelcomeFrame->deleteLater();
    m_dataWelcomeFrame = nullptr;
}

void AIPanel::handleChatLink(const QUrl &url) {
    // copy://message/N  — copy the entire Nth message verbatim.
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

    // copy-code://message/N/block/B  — copy just the raw source of the Bth
    // fenced code block in the Nth message. Gives users the ChatGPT /
    // Cursor-style per-snippet copy they expect.
    if (url.scheme() == "copy-code") {
        const QString path = url.path();  // /N/block/B  (leading / from URL)
        const QStringList parts = path.split('/', Qt::SkipEmptyParts);
        if (parts.size() >= 3 && parts[1] == "block") {
            bool okMsg = false, okBlk = false;
            const int msgIdx = parts[0].toInt(&okMsg);
            const int blkIdx = parts[2].toInt(&okBlk);
            if (okMsg && okBlk && msgIdx >= 0 && msgIdx < m_messages.size()) {
                const QString block = extractFencedBlock(m_messages.at(msgIdx).text, blkIdx);
                if (!block.isEmpty()) {
                    QApplication::clipboard()->setText(block);
                    setStatus("Code block copied", false);
                    return;
                }
            }
        }
        setStatus("Could not copy that code block", true);
        return;
    }

    QDesktopServices::openUrl(url);
}

void AIPanel::appendUserBubble(const QString &text) {
    ChatMessage message;
    message.role = ChatMessage::User;
    message.text = text;
    m_messages.push_back(message);
    scheduleChatSave();
    renderTranscript();
}

void AIPanel::beginAssistantBubble() {
    // Live assistant card. During streaming we show plain text in the
    // body for speed (no incremental markdown reparsing). On end we
    // replace with the full markdown-rendered card.
    m_currentAssistantText.clear();
    m_inAssistantBubble = true;
    if (!m_chatLayout) return;
    const AiPalette pal = aiPalette();

    ChatMessage placeholder;
    placeholder.role = ChatMessage::Assistant;
    placeholder.model = m_ollama ? m_ollama->model() : QStringLiteral("AI");
    placeholder.text.clear();

    auto copyCb = [this](int) {
        if (!m_currentAssistantText.isEmpty()) {
            QApplication::clipboard()->setText(m_currentAssistantText);
        }
    };
    // Render the card with an empty body; we'll stream into it.
    m_streamingCard = aiAddAssistantCard(m_chatLayout, placeholder,
                                         m_messages.size(), pal,
                                         copyCb, &m_streamingBody);
    if (m_streamingBody) {
        m_streamingBody->setPlainText("");
        connect(m_streamingBody, &QTextBrowser::anchorClicked,
                this, &AIPanel::handleChatLink, Qt::UniqueConnection);
    }

    // ─── Live streaming-stats label ──────────────────────────────────────
    // Sits between the card frame and the body, showing token count +
    // tokens/sec + elapsed wall-clock time, refreshed every 250 ms during
    // generation. Disappears when the stream ends (responseStats then bakes
    // the FINAL counts into the bubble header via renderTranscript).
    if (m_streamingCard) {
        m_streamingStats = new QLabel(m_streamingCard);
        m_streamingStats->setStyleSheet(QString(
            "color: %1; font-size: 10px; font-weight: 500; "
            "letter-spacing: 0.3px; padding: 2px 18px; opacity: 0.8;")
            .arg(pal.linkFg));
        m_streamingStats->setText(QStringLiteral("⏱ generating…"));
        if (auto *cardLayout = qobject_cast<QVBoxLayout*>(m_streamingCard->layout())) {
            // Insert as the second item — right after the card header so
            // the stats sit between header and body.
            cardLayout->insertWidget(1, m_streamingStats);
        }
    }
    m_streamingTokenCount = 0;
    m_streamingStartMs = QDateTime::currentMSecsSinceEpoch();
    if (!m_streamingStatsTimer) {
        m_streamingStatsTimer = new QTimer(this);
        m_streamingStatsTimer->setInterval(250); // 4 Hz refresh — feels
                                                  // live but doesn't burn CPU
        connect(m_streamingStatsTimer, &QTimer::timeout, this, [this]() {
            if (!m_streamingStats) return;
            const qint64 elapsedMs = QDateTime::currentMSecsSinceEpoch()
                                     - m_streamingStartMs;
            const double secs = elapsedMs / 1000.0;
            QString text;
            if (m_streamingTokenCount > 0 && elapsedMs > 200) {
                const double tps = m_streamingTokenCount * 1000.0 / elapsedMs;
                text = QString("⏱ %1 tok · %2 tok/s · %3 s")
                           .arg(m_streamingTokenCount)
                           .arg(QString::number(tps, 'f', 1))
                           .arg(QString::number(secs, 'f', 1));
            } else if (m_streamingTokenCount > 0) {
                text = QString("⏱ %1 tok · %2 s")
                           .arg(m_streamingTokenCount)
                           .arg(QString::number(secs, 'f', 1));
            } else {
                text = QString("⏱ generating… %1 s").arg(QString::number(secs, 'f', 1));
            }
            m_streamingStats->setText(text);
        });
    }
    m_streamingStatsTimer->start();
    if (m_chatArea) {
        QTimer::singleShot(0, m_chatArea, [this]() {
            m_chatArea->verticalScrollBar()->setValue(
                m_chatArea->verticalScrollBar()->maximum());
        });
    }
}

void AIPanel::streamIntoAssistantBubble(const QString &token) {
    if (!m_inAssistantBubble) beginAssistantBubble();

    // Count this token for the live streaming-stats display. Ollama emits
    // one streamed chunk per token (ish) so this is roughly the eval-token
    // count by the time the stream ends. The final canonical count comes
    // from responseStats() and overwrites this estimate.
    ++m_streamingTokenCount;
    m_currentAssistantText += token;
    if (m_streamingBody) {
        // Append plain text — fast, preserves whitespace, no HTML escape
        // headaches mid-stream. Markdown rendering happens on end.
        QTextCursor c = m_streamingBody->textCursor();
        c.movePosition(QTextCursor::End);
        c.insertText(token);
        m_streamingBody->setTextCursor(c);
        // Nudge height + scroll so new tokens stay visible.
        m_streamingBody->document()->setTextWidth(m_streamingBody->viewport()->width());
        const int h = int(m_streamingBody->document()->size().height()) + 8;
        m_streamingBody->setFixedHeight(qMax(30, h));
    }
    if (m_chatArea) {
        m_chatArea->verticalScrollBar()->setValue(
            m_chatArea->verticalScrollBar()->maximum());
    }
}

void AIPanel::endAssistantBubble() {
    if (!m_inAssistantBubble) return;
    m_inAssistantBubble = false;

    // Stop the live streaming-stats timer + drop the label. The final
    // counts get persisted onto the ChatMessage below so the post-
    // completion bubble keeps showing "⏱ N tok · X tok/s · Y s" via
    // aiAddAssistantCard. responseStats may later overwrite with the
    // canonical Ollama / OpenAI-compat token counts.
    const int    finalTokens   = m_streamingTokenCount;
    const qint64 finalElapsedMs = m_streamingStartMs > 0
        ? (QDateTime::currentMSecsSinceEpoch() - m_streamingStartMs)
        : -1;
    if (m_streamingStatsTimer) m_streamingStatsTimer->stop();
    if (m_streamingStats) {
        m_streamingStats->deleteLater();
        m_streamingStats = nullptr;
    }
    m_streamingTokenCount = 0;
    m_streamingStartMs = 0;

    m_streamingCard = nullptr;
    m_streamingBody = nullptr;
    if (!m_currentAssistantText.isEmpty()) {
        ChatMessage message;
        message.role = ChatMessage::Assistant;
        message.text = m_currentAssistantText;
        message.model = m_ollama ? m_ollama->model() : QStringLiteral("AI");
        // Seed stats from the live streaming counters so the bubble keeps
        // showing the same "⏱ N tok · X tok/s · Y s" line after the stream
        // ends. responseStats will replace these with canonical counts.
        if (finalTokens > 0)       message.evalTokens = finalTokens;
        if (finalElapsedMs >= 0)   message.elapsedMs  = finalElapsedMs;
        m_messages.push_back(message);
        scheduleChatSave();
    }
    // Full re-render with markdown now that we have the complete text.
    renderTranscript();
}

// ═══════════════════════════════════════════════════════════════════════
// v0.1.35 — Agent-loop tool-call handling
//
// When Coding Mode is on AND the active model is in the tool-allowlist,
// sendPrompt attaches `tools: [read_file, list_dir]` to the request.
// The model can call them; OllamaClient parses the tool_calls frames
// out of the response stream and emits toolCallReceived. We execute
// the call against m_workspaceRoot via AiTools::execute, queue the
// result, and render a 🔧 card inline. When the stream finishes (with
// finish_reason=tool_calls), we call continueWithToolResults to feed
// the results back and continue the conversation.
// ═══════════════════════════════════════════════════════════════════════

namespace {
// Render a transient inline tool-call card. Goes into m_chatLayout
// just before the streaming card so the user sees "🔧 read_file …"
// while the model decides what to do next. Style is muted compared to
// real chat bubbles — the tool call is process metadata, not content.
QFrame *aiAddToolCallCard(QVBoxLayout *target,
                          const QString &toolName,
                          const QString &argsSummary,
                          const QString &resultSummary,
                          bool isError,
                          const AiPalette &pal) {
    auto *card = new QFrame;
    card->setObjectName("toolCallCard");
    const QString accent = isError ? pal.errBorder : pal.accent;
    card->setStyleSheet(QString(
        "#toolCallCard { "
        "  background: %1; "
        "  border: 1px solid %2; "
        "  border-left: 3px solid %3; "
        "  border-radius: 6px; "
        "} "
        "QLabel { background: transparent; color: %4; font-size: 11px; }")
        .arg(pal.assistBg, pal.assistBorder, accent, pal.muted));
    auto *outer = new QHBoxLayout(card);
    outer->setContentsMargins(10, 6, 10, 6);
    outer->setSpacing(8);
    auto *icon = new QLabel(isError ? "✗" : "🔧");
    icon->setStyleSheet(QString("color: %1; font-weight: 600;").arg(accent));
    outer->addWidget(icon);
    auto *body = new QLabel(QString("<b>%1</b> %2 %3 <span style='color:%4;'>%5</span>")
        .arg(toolName.toHtmlEscaped(),
             argsSummary.toHtmlEscaped(),
             resultSummary.isEmpty() ? "" : "→",
             pal.muted,
             resultSummary.toHtmlEscaped()));
    body->setTextFormat(Qt::RichText);
    body->setWordWrap(true);
    body->setTextInteractionFlags(Qt::TextSelectableByMouse);
    outer->addWidget(body, 1);

    auto *row = new QWidget;
    row->setStyleSheet("background: transparent;");
    auto *rowLay = new QVBoxLayout(row);
    rowLay->setContentsMargins(0, 0, 0, 6);
    rowLay->setSpacing(0);
    rowLay->addWidget(card);
    target->insertWidget(target->count() - 1, row);
    return card;
}
} // anon

void AIPanel::handleToolCall(const QString &id, const QString &name,
                             const QJsonObject &args) {
    // Hard cap: 25 tool calls per user turn — match Cursor / Aider's
    // budget. Soft cap (10 consecutive) is handled implicitly by
    // emitting a system-reminder string into the next round.
    constexpr int kHardCap = 25;
    if (m_toolCallsTotal >= kHardCap) {
        AiTools::ToolResult r;
        r.id = id;
        r.name = name;
        r.isError = true;
        r.errorKind = "io_error";
        r.content = QString("{\"ok\":false,\"error_kind\":\"io_error\",\"message\":"
                            "\"Tool-call budget exhausted (%1 calls this turn). "
                            "Stop and summarise what you've found.\"}").arg(kHardCap);
        QJsonObject payload;
        payload["id"] = r.id;
        payload["name"] = r.name;
        payload["args"] = args;
        payload["content"] = r.content;
        m_pendingToolResults.append(payload);
        const AiPalette pal = aiPalette();
        aiAddToolCallCard(m_chatLayout, name, "(budget exhausted)", "skipped", true, pal);
        return;
    }
    ++m_toolCallsThisTurn;
    ++m_toolCallsTotal;

    // v0.1.40: detect the malformed-args marker that ollama.cpp stuffs
    // into args when the model emits invalid JSON in tool-call arguments.
    // Surface it as a structured tool result so the model gets a clear
    // signal to retry with valid JSON, instead of silently executing
    // with empty args (which used to surface as confusing downstream
    // errors like "hunks array is empty").
    if (args.contains("_notepatra_parse_error")) {
        const QString perr = args.value("_notepatra_parse_error").toString();
        const QString rawArgs = args.value("_notepatra_raw_args").toString();
        QString rawPreview = rawArgs;
        if (rawPreview.size() > 240) rawPreview = rawPreview.left(240) + "…";
        // Escape for JSON content embedding.
        QString escapedPreview = rawPreview;
        escapedPreview.replace('\\', "\\\\").replace('"', "\\\"")
                      .replace('\n', "\\n").replace('\t', "\\t");
        QString escapedErr = perr;
        escapedErr.replace('\\', "\\\\").replace('"', "\\\"");
        AiTools::ToolResult r;
        r.id = id;
        r.name = name;
        r.isError = true;
        r.errorKind = "malformed_args";
        r.content = QString(
            "{\"ok\":false,\"error_kind\":\"malformed_args\",\"message\":"
            "\"Tool-call arguments JSON failed to parse: %1. "
            "Re-emit the call with valid JSON. Make sure quotes inside "
            "string values are escaped (\\\\\\\") and that nested objects "
            "are well-formed. Raw args preview: %2\"}")
            .arg(escapedErr, escapedPreview);
        QJsonObject payload;
        payload["id"] = r.id;
        payload["name"] = r.name;
        payload["args"] = args;
        payload["content"] = r.content;
        m_pendingToolResults.append(payload);
        const AiPalette palErr = aiPalette();
        aiAddToolCallCard(m_chatLayout, name, "(malformed args)", "✗ malformed_args", true, palErr);
        return;
    }

    // Build a short user-facing summary of the args. Keeps the card
    // readable without dumping full JSON. search uses 'pattern' as its
    // primary arg (path is optional + workspace-root by default), so
    // surface that instead of '(...)'.
    QString argsSummary;
    if (name == "search") {
        const QString pattern = args.value("pattern").toString();
        argsSummary = "(\"" + pattern + "\")";
    } else if (args.contains("path")) {
        argsSummary = "(" + args.value("path").toString() + ")";
    } else {
        argsSummary = "(...)";
    }

    // Execute. AiTools::execute never throws — failures come back as
    // structured ToolResult with isError=true.
    AiTools::ToolCall call;
    call.id = id;
    call.name = name;
    call.args = args;
    AiTools::ToolResult result = AiTools::execute(call, m_workspaceRoot);

    // Build a one-line result summary for the card UI.
    QString resultSummary;
    if (result.isError) {
        resultSummary = "✗ " + result.errorKind;
    } else if (name == "read_file") {
        // Pull lines/truncated from the JSON content for a tight summary.
        QJsonDocument jd = QJsonDocument::fromJson(result.content.toUtf8());
        QJsonObject body = jd.object().value("result").toObject();
        const int n = body.value("lines_emitted").toInt();
        const bool tr = body.value("truncated").toBool();
        resultSummary = QString("%1 lines%2").arg(n).arg(tr ? " (truncated)" : "");
    } else if (name == "list_dir") {
        QJsonDocument jd = QJsonDocument::fromJson(result.content.toUtf8());
        QJsonObject body = jd.object().value("result").toObject();
        const int n = body.value("entries").toArray().size();
        resultSummary = QString("%1 entries").arg(n);
    } else if (name == "write_file") {
        QJsonDocument jd = QJsonDocument::fromJson(result.content.toUtf8());
        QJsonObject body = jd.object().value("result").toObject();
        const int bytes = body.value("bytes_written").toInt();
        const QString mode = body.value("mode").toString();
        const bool created = body.value("created").toBool();
        resultSummary = QString("%1 %2 (%3 bytes)")
                            .arg(created ? "created" : "wrote", mode).arg(bytes);
        // Auto-open / reload the file in the editor.
        const QString absPath = body.value("abs_path").toString();
        if (!absPath.isEmpty()) emit fileWrittenByAgent(absPath, created);
    } else if (name == "search") {
        QJsonDocument jd = QJsonDocument::fromJson(result.content.toUtf8());
        QJsonObject body = jd.object().value("result").toObject();
        const int total = body.value("total_matches").toInt();
        const int files = body.value("files_scanned").toInt();
        const bool tr = body.value("truncated").toBool();
        resultSummary = QString("%1 matches in %2 files%3")
                            .arg(total).arg(files).arg(tr ? " (truncated)" : "");
    } else if (name == "apply_diff") {
        QJsonDocument jd = QJsonDocument::fromJson(result.content.toUtf8());
        QJsonObject body = jd.object().value("result").toObject();
        const int hunks = body.value("hunks_applied").toInt();
        resultSummary = QString("%1 hunk%2 applied").arg(hunks).arg(hunks == 1 ? "" : "s");
        // Reload the editor for the modified file.
        const QString absPath = body.value("abs_path").toString();
        if (!absPath.isEmpty()) emit fileWrittenByAgent(absPath, false);
    }

    const AiPalette pal = aiPalette();
    aiAddToolCallCard(m_chatLayout, name, argsSummary, resultSummary,
                      result.isError, pal);

    // Queue for flush when the stream finishes.
    QJsonObject payload;
    payload["id"] = id;
    payload["name"] = name;
    payload["args"] = args;
    payload["content"] = result.content;
    m_pendingToolResults.append(payload);
}

void AIPanel::flushPendingToolResults() {
    if (m_pendingToolResults.isEmpty()) return;

    const QJsonArray batch = m_pendingToolResults;
    m_pendingToolResults = QJsonArray();

    // Continue the conversation: feed the tool results back, the model
    // will keep streaming text or call more tools.
    m_ollama->continueWithToolResults(batch, m_lastSystemPromptForTools,
                                      m_lastToolsArray);
    // The streaming-bubble flow continues into the same body — token
    // streaming will resume in the existing card.
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

void AIPanel::onThemeChanged() {
    // The bubble HTML (user / assistant / error cards) embeds per-theme
    // colours inline; renderTranscript() rebuilds every bubble by
    // calling aiPalette() fresh, which is the cheapest way to swap
    // themes on a live chat. Do that first so the chat view flips,
    // then restyle the persistent chrome widgets (header label + chat
    // area + inputs that live for the lifetime of the widget).
    const AiPalette pal = aiPalette();

    if (m_headerLabel) {
        m_headerLabel->setStyleSheet(QString(
            "font-weight: 600; background: %1; color: %2; "
            "padding: 6px 10px; letter-spacing: 1px; font-size: 11px;")
            .arg(pal.chromeBg, pal.headerFg));
    }

    if (m_chatArea) {
        // Match the original ctor stylesheet for the chat scroll area so
        // the background + scroll-bar handles pick up the new theme.
        m_chatArea->setStyleSheet(QString(
            "QScrollArea { background: %1; border: none; } "
            "QScrollBar:vertical { background: %1; width: 10px; } "
            "QScrollBar::handle:vertical { background: %2; border-radius: 5px; "
            "min-height: 30px; margin: 2px; } "
            "QScrollBar::handle:vertical:hover { background: %3; } "
            "QScrollBar::add-line:vertical, QScrollBar::sub-line:vertical { height: 0; }")
            .arg(pal.chatBg, pal.inputBorder, pal.muted));
    }
    if (m_chatContent) {
        m_chatContent->setStyleSheet(QString("background: %1;").arg(pal.chatBg));
    }
    if (m_customInput) {
        m_customInput->setStyleSheet(QString(
            "QPlainTextEdit { background: %1; color: %2; border: 1px solid %3; "
            "border-radius: 6px; padding: 8px 10px; font-size: 13px; "
            "selection-background-color: %4; selection-color: #FFFFFF; } "
            "QPlainTextEdit:focus { border-color: %5; }")
            .arg(pal.inputBg, pal.inputText, pal.inputBorder,
                 pal.accent, pal.inputFocus));
    }

    // Re-render the chat — this is the heavy lifter. aiPalette() is
    // consulted once at the top of renderTranscript() and all bubble /
    // error / assistant cards are rebuilt with the new colours.
    renderTranscript();
    update();
}

// ═══════════════════════════════════════════════════════════════════════
// v0.1.39 — persistent chat history (per-workspace JSON).
//
// Path:    ~/.config/notepatra/chat-history/<sha1-of-workspace>.json
// Schema:  { "version": 1, "workspace": "<absPath>",
//            "messages": [{role, text, model?, promptTokens?, evalTokens?, elapsedMs?}, ...] }
// Limit:   1 MB on disk per workspace; older messages roll off the front
//          if the next save would exceed.
//
// Only User / Assistant / Error roles are persisted — transient tool-
// call cards (rendered inline by handleToolCall) aren't part of
// m_messages and aren't saved. Saves are debounced 2s so the disk
// doesn't see a write per token.
// ═══════════════════════════════════════════════════════════════════════

void AIPanel::updateChatHistoryPath() {
    if (m_workspaceRoot.isEmpty()) {
        m_chatHistoryPath.clear();
        return;
    }
    const QString configDir =
        QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation);
    if (configDir.isEmpty()) {
        m_chatHistoryPath.clear();
        return;
    }
    const QString historyDir = configDir + "/chat-history";
    QDir().mkpath(historyDir);
    const QByteArray hash = QCryptographicHash::hash(
        m_workspaceRoot.toUtf8(), QCryptographicHash::Sha1).toHex();
    m_chatHistoryPath = historyDir + "/" + QString::fromLatin1(hash) + ".json";
}

void AIPanel::scheduleChatSave() {
    if (m_chatHistoryPath.isEmpty()) return;  // no workspace open → no persistence
    if (!m_chatSaveTimer) {
        m_chatSaveTimer = new QTimer(this);
        m_chatSaveTimer->setSingleShot(true);
        m_chatSaveTimer->setInterval(2000);
        connect(m_chatSaveTimer, &QTimer::timeout, this, &AIPanel::saveChatHistory);
    }
    m_chatSaveTimer->start();
}

void AIPanel::saveChatHistory() {
    if (m_chatHistoryPath.isEmpty()) return;
    QJsonArray arr;
    for (const ChatMessage &m : m_messages) {
        QJsonObject o;
        const char *roleStr = "user";
        if (m.role == ChatMessage::Assistant) roleStr = "assistant";
        else if (m.role == ChatMessage::Error) roleStr = "error";
        o["role"] = QString::fromLatin1(roleStr);
        o["text"] = m.text;
        if (!m.model.isEmpty())   o["model"]        = m.model;
        if (m.promptTokens >= 0)  o["promptTokens"] = m.promptTokens;
        if (m.evalTokens >= 0)    o["evalTokens"]   = m.evalTokens;
        if (m.elapsedMs >= 0)     o["elapsedMs"]    = m.elapsedMs;
        arr.append(o);
    }
    QJsonObject root;
    root["version"]   = 1;
    root["workspace"] = m_workspaceRoot;
    root["messages"]  = arr;

    QByteArray bytes = QJsonDocument(root).toJson(QJsonDocument::Compact);

    // Roll-off oldest messages until under the 1 MB cap.
    constexpr int kMaxBytes = 1 * 1024 * 1024;
    while (bytes.size() > kMaxBytes && !arr.isEmpty()) {
        arr.removeFirst();
        root["messages"] = arr;
        bytes = QJsonDocument(root).toJson(QJsonDocument::Compact);
    }

    // Atomic write: .tmp + rename. Don't risk a half-written history
    // file if the app gets killed mid-save.
    const QString tmp = m_chatHistoryPath + ".tmp";
    QFile f(tmp);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate)) return;
    f.write(bytes);
    f.close();
    QFile::remove(m_chatHistoryPath);  // Windows rename can't overwrite
    QFile::rename(tmp, m_chatHistoryPath);
}

void AIPanel::loadChatHistory() {
    if (m_chatHistoryPath.isEmpty()) return;
    QFile f(m_chatHistoryPath);
    if (!f.open(QIODevice::ReadOnly)) return;
    const QByteArray bytes = f.readAll();
    f.close();

    QJsonParseError err{};
    const QJsonDocument doc = QJsonDocument::fromJson(bytes, &err);
    if (err.error != QJsonParseError::NoError || !doc.isObject()) return;
    const QJsonArray arr = doc.object().value("messages").toArray();

    m_messages.clear();
    m_messages.reserve(arr.size());
    for (const QJsonValue &v : arr) {
        if (!v.isObject()) continue;
        const QJsonObject o = v.toObject();
        ChatMessage m;
        const QString role = o.value("role").toString();
        if      (role == "assistant") m.role = ChatMessage::Assistant;
        else if (role == "error")     m.role = ChatMessage::Error;
        else                          m.role = ChatMessage::User;
        m.text         = o.value("text").toString();
        m.model        = o.value("model").toString();
        m.promptTokens = o.value("promptTokens").toInt(-1);
        m.evalTokens   = o.value("evalTokens").toInt(-1);
        m.elapsedMs    = static_cast<qint64>(o.value("elapsedMs").toDouble(-1));
        m_messages.push_back(m);
    }
}
