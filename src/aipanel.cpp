#include "aipanel.h"
#include "ai_context.h"
#include "ai_intent.h"
#include "ai_systemprompt.h"
#include "ai_tools.h"
#include "chartrender.h"
#include "charts/vega_chart_renderer.h"
#include "credscrub.h"
#include "csvanalyst.h"
#include "dbconnections.h"
#include "dbtree.h"
#include "edit_plan.h"
#include "fonts.h"
#include "config.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QtMath>
#include <QLabel>
#include <QFont>
#include <QScrollBar>
#include <QMenu>
#include <QCompleter>
#include <QAbstractItemView>
#include <QSortFilterProxyModel>
#include <QStringListModel>
#include <QApplication>
#include <QStyle>
#include <QClipboard>
#include <QTimer>
#include <QFileDialog>
#include <QFileInfo>
#include <QFile>
#include <QDir>
#include <QMessageBox>
#include <QDesktopServices>
#include <QMouseEvent>
#include <QKeyEvent>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QMimeData>
#include <QTextBlock>
#include <QTextCursor>
#include <QTextDocument>
#include <QFrame>
#include <QButtonGroup>
#include <QStandardItemModel>
#include <QStandardItem>
#include <QAbstractButton>
#include <QScrollArea>
#include <QTabWidget>
#include <QTabBar>
#include <QProcess>
#include <QImage>
#include <QBuffer>
#include <QByteArray>
#include <QDateTime>
#include <QPainter>
#include <QPainterPath>
#include <QRegularExpression>
#include <QSet>
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
            // v0.1.61 — copy anchor below the pill, matches the assistant
            // card's per-message ⧉ copy link. Routes through the same
            // copy://message/N handler in handleChatLink().
            html += QString(
                "<table class='msg' cellpadding='0' cellspacing='0' style='margin-bottom:14px;'>"
                "<tr><td align='right'>"
                "<table cellpadding='0' cellspacing='0'>"
                "<tr><td bgcolor='%2' style='padding:10px 16px; color:%3; font-size:13px; font-weight:500;'>"
                "<div class='message-plain'>%1</div>"
                "</td></tr>"
                "<tr><td align='right' style='padding:3px 4px 0 0;'>"
                "<a href='copy://message/%4' style='color:%5; font-size:10px; text-decoration:none; font-weight:600;'>⧉ copy</a>"
                "</td></tr>"
                "</table>"
                "</td></tr></table>")
                .arg(plainTextHtml(message.text), pal.userBg, pal.userFg,
                     QString::number(i), pal.linkFg);
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

    // v0.1.61 — drag-and-drop support: users can drop images / PDFs / DOCX
    // / PPTX directly onto the AI panel and they'll be attached as context.
    // dragEnterEvent / dropEvent below handle the heavy lifting; vision-only
    // kinds (images) are gated on modelSupportsVision() so we don't silently
    // drop the image when the active model can't read it.
    setAcceptDrops(true);

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
    // v0.1.56 — Expand-to-fullscreen button. Lets the user blow the AI dock
    // out to take the entire MainWindow client area for heavy data-analyst
    // and coding sessions where the chat is the primary surface. Works as
    // a toggle: first click hides every sibling widget in the splitter and
    // resizes the AI dock to fill it; second click restores the saved
    // splitter sizes + sibling visibility. Implemented as a signal here +
    // slot in MainWindow so this panel doesn't need to know about its
    // splitter parent. Keyboard shortcut F11 also works (registered in
    // MainWindow).
    m_aiExpandBtn = new QPushButton(QString::fromUtf8("\xE2\x9B\xB6")); // U+26F6 SQUARE FOUR CORNERS
    {
        QFont f = m_aiExpandBtn->font();
        f.setPointSize(f.pointSize() > 0 ? f.pointSize() + 4 : 14);
        f.setBold(true);
        m_aiExpandBtn->setFont(f);
    }
    m_aiExpandBtn->setFixedSize(36, 28);
    m_aiExpandBtn->setCursor(Qt::PointingHandCursor);
    m_aiExpandBtn->setFlat(true);
    m_aiExpandBtn->setCheckable(true);
    m_aiExpandBtn->setToolTip(tr("Expand the AI panel to take the full window. "
                             "Click again to restore. (F11)"));
    m_aiExpandBtn->setStyleSheet(
        "QPushButton { background: transparent; border: none; "
        "color: #888; padding: 0 0 2px 0; } "
        "QPushButton:hover { background: rgba(120,120,120,0.18); color: #444; } "
        "QPushButton:checked { background: rgba(204,120,92,0.18); color: #CC785C; }");
    connect(m_aiExpandBtn, &QPushButton::toggled, this, [this](bool on) {
        emit fullscreenToggled(on);
    });
    headerLay->addWidget(m_aiExpandBtn, 0, Qt::AlignRight);

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
    backendCombo->addItem("Ollama Cloud",       "Ollama Cloud");
    backendCombo->addItem("llama.cpp (GGUF)",   "llama.cpp");
    backendCombo->addItem("OpenRouter",         "OpenRouter");
    backendCombo->addItem("OpenAI",             "OpenAI");
    backendCombo->addItem("Azure OpenAI",       "Azure OpenAI");
    backendCombo->setItemData(0, "Local Ollama daemon · 11434", Qt::ToolTipRole);
    backendCombo->setItemData(1, "Hosted Ollama models · gpt-oss:120b · qwen3-coder:480b · deepseek-v3.1:671b", Qt::ToolTipRole);
    backendCombo->setItemData(2, "Self-hosted GGUF · llama-server on 8080", Qt::ToolTipRole);
    backendCombo->setItemData(3, "367 models routed across providers · paste OpenRouter key in Settings", Qt::ToolTipRole);
    backendCombo->setItemData(4, "OpenAI direct · GPT-4o / GPT-5 / o-series", Qt::ToolTipRole);
    backendCombo->setItemData(5, "Azure OpenAI · enterprise Azure resource + deployment", Qt::ToolTipRole);
    backendCombo->setFixedWidth(170);

    // Initialise from Config. blockSignals() prevents the
    // currentIndexChanged handler below from firing DURING construction
    // (before m_ollama exists) — which was silently eating the Ollama
    // default and breaking auto-detect on fresh launches.
    backendCombo->blockSignals(true);
    {
        const QString be = Config::instance().aiBackend;
        const QString url = Config::instance().aiBaseUrl;
        // Index map: 0=Ollama, 1=Ollama Cloud, 2=llama.cpp, 3=OpenRouter,
        // 4=OpenAI, 5=Azure OpenAI.
        if (be.compare("Ollama Cloud", Qt::CaseInsensitive) == 0
            || url.contains("ollama.com")) {
            backendCombo->setCurrentIndex(1);
        } else if (be.compare("llama.cpp", Qt::CaseInsensitive) == 0) {
            backendCombo->setCurrentIndex(2);
        } else if (be.compare("OpenRouter", Qt::CaseInsensitive) == 0
                   || url.contains("openrouter")) {
            backendCombo->setCurrentIndex(3);
        } else if (be.compare("OpenAI", Qt::CaseInsensitive) == 0
                   || url.contains("api.openai.com")) {
            backendCombo->setCurrentIndex(4);
        } else if (be.compare("Azure OpenAI", Qt::CaseInsensitive) == 0
                   || url.contains("openai.azure.com")) {
            backendCombo->setCurrentIndex(5);
        } else {
            // Stale LM Studio / Jan URLs from older configs fall through to
            // Ollama default — user just picks llama.cpp manually if needed.
            backendCombo->setCurrentIndex(0);
        }
    }
    backendCombo->blockSignals(false);
    connect(backendCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, [this, backendCombo](int) {
        const QString key = backendCombo->currentData().toString();
        auto &cfg = Config::instance();
        // v0.1.55 — preserve the per-provider distinction in cfg.aiBackend.
        // Pre-v0.1.55 we stored "OpenAI-compat" for both OpenRouter and
        // OpenAI, which made aiKeyForBackend() unable to tell them apart.
        // backendFromString() still maps "OpenRouter" / "OpenAI" → the
        // OpenAICompat enum so OllamaClient routing is unchanged.
        if (key == "Ollama")            { cfg.aiBackend = "Ollama";        cfg.aiBaseUrl.clear(); }
        else if (key == "Ollama Cloud") { cfg.aiBackend = "Ollama Cloud";  cfg.aiBaseUrl = "https://ollama.com/v1"; }
        else if (key == "llama.cpp")    { cfg.aiBackend = "llama.cpp";     cfg.aiBaseUrl.clear(); }
        else if (key == "OpenRouter")   { cfg.aiBackend = "OpenRouter";    cfg.aiBaseUrl = "https://openrouter.ai/api/v1"; }
        else if (key == "OpenAI")       { cfg.aiBackend = "OpenAI";        cfg.aiBaseUrl = "https://api.openai.com/v1"; }
        else if (key == "Azure OpenAI") {
            cfg.aiBackend = "Azure OpenAI";
            // Azure URL is reconstructed at request time from
            // aiAzureResource + aiAzureDeployment + aiAzureApiVersion;
            // aiBaseUrl carries the resource hostname for visibility.
            cfg.aiBaseUrl = cfg.aiAzureResource.isEmpty()
                ? QString("https://<resource>.openai.azure.com/")
                : QString("https://%1.openai.azure.com/").arg(cfg.aiAzureResource);
        }
        else                            { cfg.aiBackend = "Ollama";        cfg.aiBaseUrl.clear(); }
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

    // ─── Cloud-config banner (replaces the v0.1.54 inline API-key row) ──
    //
    // Pre-v0.1.55 the AI panel had an inline "API key:" QLineEdit + Save
    // button that appeared whenever a cloud backend was selected. That
    // worked for one provider at a time but a) cluttered the panel and
    // b) silently broke when users alternated between OpenRouter and
    // OpenAI because the single Config::aiApiKey field couldn't hold
    // both. Now: per-provider keys live in Config::aiOpenRouterKey /
    // aiOpenAIKey, and the AI panel just shows a one-line banner that
    // points users at the gear (⚙) icon when the active cloud backend
    // has no key saved yet.
    auto *cloudConfigBanner = new QLabel;
    cloudConfigBanner->setVisible(false);
    cloudConfigBanner->setWordWrap(true);
    cloudConfigBanner->setTextInteractionFlags(Qt::TextBrowserInteraction);
    cloudConfigBanner->setOpenExternalLinks(false);
    cloudConfigBanner->setContentsMargins(10, 6, 10, 6);
    auto refreshCloudConfigBanner = [cloudConfigBanner, backendCombo]() {
        const QString backend = backendCombo->currentData().toString();
        const auto &cfg = Config::instance();
        const QString key = cfg.aiKeyForBackend(backend);
        const bool isCloud = (backend == "OpenRouter" || backend == "OpenAI"
                              || backend == "Ollama Cloud"
                              || backend == "Azure OpenAI");
        if (!isCloud) {
            cloudConfigBanner->setVisible(false);
            return;
        }
        const AiPalette p = aiPalette();
        if (key.isEmpty()) {
            // Provider selected but no key — block the chat path and tell
            // the user exactly where to go.
            const QString warnBg = aiIsDark() ? "#3A2E1A" : "#FFF6DD";
            const QString warnFg = aiIsDark() ? "#F6D77A" : "#7A5A0E";
            const QString warnBorder = aiIsDark() ? "#7A5E2E" : "#E8C764";
            cloudConfigBanner->setStyleSheet(QString(
                "QLabel { background: %1; color: %2; border: 1px solid %3; "
                "border-radius: 4px; padding: 6px 10px; font-size: 12px; }")
                .arg(warnBg, warnFg, warnBorder));
            cloudConfigBanner->setText(QString(
                "⚠ No %1 API key saved. Click <b>⚙ Settings</b> next to the "
                "model dropdown to add one — both OpenRouter and OpenAI keys "
                "can be saved side by side.")
                .arg(backend));
        } else {
            // Key is present — show a friendly confirmation. Folded into
            // the same banner so users always see *some* signal about what
            // backend is active.
            const QString okBg     = aiIsDark() ? "#1F2D24" : "#EAF8EE";
            const QString okFg     = aiIsDark() ? "#9BD9A0" : "#1B5E20";
            const QString okBorder = aiIsDark() ? "#3F6B49" : "#B7D8B8";
            cloudConfigBanner->setStyleSheet(QString(
                "QLabel { background: %1; color: %2; border: 1px solid %3; "
                "border-radius: 4px; padding: 6px 10px; font-size: 12px; }")
                .arg(okBg, okFg, okBorder));
            // Show only the prefix + last 4 chars of the key, never the
            // middle. Even a quick screen capture won't expose enough to
            // make the secret usable.
            QString masked = key.length() > 12
                ? key.left(8) + "…" + key.right(4)
                : QString("•").repeated(key.length());
            cloudConfigBanner->setText(QString(
                "✓ %1 connected · key %2 · click <b>⚙</b> to change or remove")
                .arg(backend).arg(masked));
        }
        cloudConfigBanner->setVisible(true);
        Q_UNUSED(p);
    };
    connect(backendCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, [refreshCloudConfigBanner](int) { refreshCloudConfigBanner(); });

    m_modelCombo = new QComboBox;
    m_modelCombo->setEditable(true);
    m_modelCombo->addItem("(detecting…)");
    m_modelCombo->setEnabled(false);
    // v0.1.61 — watch dropdown enable/disable transitions so the chat input
    // auto-enables the instant a probe completes (no Reset required).
    // Previously updateInputAvailability() only fired on currentTextChanged,
    // which races setEnabled(true) — the dropdown text changed while still
    // disabled, so `ready` was computed as false; then setEnabled(true) ran
    // silently. Net effect: user had to wiggle the dropdown (or hit Reset)
    // to coax the input on. EnabledChange catches every code path.
    m_modelCombo->installEventFilter(this);
    // v0.1.55 — searchable dropdown. The QCompleter searches against
    // Qt::EditRole on each item (a fused blob of label + provider key +
    // nice name + synonyms like "claude haiku sonnet opus anthropic …"
    // installed by renderGroupedModelTree) while the popup renders
    // Qt::DisplayRole (the pretty visible label). The custom subclass
    // below overrides pathFromIndex so picking an item from the
    // completer's popup writes the pretty label back into the lineEdit
    // instead of the gibberish blob.
    if (auto *le = m_modelCombo->lineEdit()) {
        class SearchCompleter : public QCompleter {
        public:
            using QCompleter::QCompleter;
        protected:
            QString pathFromIndex(const QModelIndex &idx) const override {
                if (!idx.isValid()) return QString();
                // Pretty label for the lineEdit; never the search blob.
                return idx.data(Qt::DisplayRole).toString();
            }
        };
        auto *completer = new SearchCompleter(m_modelCombo->model(), m_modelCombo);
        completer->setCaseSensitivity(Qt::CaseInsensitive);
        completer->setFilterMode(Qt::MatchContains);
        completer->setCompletionMode(QCompleter::PopupCompletion);
        completer->setCompletionRole(Qt::EditRole);  // search the blob
        m_modelCombo->setCompleter(completer);
        le->setPlaceholderText("type to search (e.g. claude · xai · gpt · gemini)");
    }
    modelRow->addWidget(m_modelCombo, 1);
    m_refreshBtn = new QPushButton("↻");
    m_refreshBtn->setFixedWidth(28);
    m_refreshBtn->setToolTip("Refresh model list from the selected backend");
    modelRow->addWidget(m_refreshBtn);

    // Gear ⚙ — opens a guided AI Settings dialog. The dialog lets users
    // save BOTH OpenRouter and OpenAI keys side-by-side, test each one
    // against the provider's /v1/models endpoint without leaving the
    // dialog, and forget either key independently. Pre-v0.1.55 this
    // button was a small popup menu against a single shared aiApiKey;
    // now it owns the whole cloud-config story.
    auto *gearBtn = new QPushButton("⚙ Settings");
    gearBtn->setFixedHeight(26);
    gearBtn->setToolTip("AI Settings — manage OpenRouter / OpenAI keys");
    gearBtn->setStyleSheet(QString(
        "QPushButton { background: %1; color: %2; border: 1px solid %3; "
        "border-radius: 4px; font-size: 11px; padding: 0 8px; }"
        "QPushButton:hover { background: %4; }")
        .arg(pal.btnBg, pal.inputText, pal.btnBorder, pal.btnHover));
    connect(gearBtn, &QPushButton::clicked, this,
        [this, refreshCloudConfigBanner]() {
            openAiSettingsDialog();
            refreshCloudConfigBanner();
        });
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
    layout->addWidget(cloudConfigBanner);
    // Reflect current backend / saved-key state on construction.
    refreshCloudConfigBanner();

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

    // v0.1.55 — privacy / safety toggle. Default off so the AI cannot see
    // the file you're editing unless you explicitly grant access. Lock
    // glyph + colored state make the toggle's privacy posture obvious at
    // a glance (red lock = AI is blind, green key = AI can see). Tooltip
    // explains the surface so users know what it controls.
    m_shareFileCheck = new QCheckBox(QStringLiteral("🔒 Share file"));
    m_shareFileCheck->setChecked(Config::instance().aiShareOpenFile);
    m_shareFileCheck->setCursor(Qt::PointingHandCursor);
    auto applyShareCheckStyle = [this, pal]() {
        const bool on = m_shareFileCheck->isChecked();
        m_shareFileCheck->setStyleSheet(QString(
            "QCheckBox { font-size: 11px; font-weight: 600; "
            "color: %1; margin-left: 12px; padding: 2px 6px; "
            "border-radius: 4px; }")
            .arg(on ? (aiIsDark() ? "#9BD9A0" : "#1B5E20")    // green = AI can see
                    : (aiIsDark() ? "#F48771" : "#B92E1B"))); // red = AI blind
        m_shareFileCheck->setText(on ? QStringLiteral("🔓 Sharing file")
                                     : QStringLiteral("🔒 Share file"));
        m_shareFileCheck->setToolTip(on
            ? "ON — the AI can see your currently-open file (path, language, "
              "and content) and any other open editor tabs. Click to revoke."
            : "OFF — the AI is BLIND to your open file. It can't read your "
              "code, paths, or filenames unless you explicitly include them "
              "in your message. Click to grant access.");
    };
    applyShareCheckStyle();
    connect(m_shareFileCheck, &QCheckBox::toggled, this,
        [this, applyShareCheckStyle](bool on) {
            Config::instance().aiShareOpenFile = on;
            Config::instance().save();
            applyShareCheckStyle();
            setStatus(on
                ? "📄 File access granted — AI can now see your open editor tab"
                : "🔒 File access revoked — AI is blind to your open file",
                false);
        });
    modeRow->addWidget(m_shareFileCheck);

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

        // v0.1.61 — chrome visibility per mode:
        //   * Coding/Data: committed AI-first workflow — hide the ⛶ expand
        //     toggle. Only the ✕ close button shows. To exit fullscreen the
        //     user switches back to Chat mode (mode-button click).
        //   * Coding only: 🔒 Share file toggle is meaningful (controls
        //     whether the open editor file ships with the prompt). In Chat
        //     and Data modes there's no current-file concept worth sharing,
        //     so the toggle disappears.
        if (m_aiExpandBtn) {
            m_aiExpandBtn->setVisible(!(coding || data));
        }
        if (m_shareFileCheck) {
            m_shareFileCheck->setVisible(coding);
        }

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

        // v0.1.55 — collapse quick actions on every mode change. User
        // explicitly asked for "always collapses on chat". Re-using the
        // chevron toggle here drives both the visibility AND the chevron's
        // own ▸/▾ glyph update so the UI stays consistent. Users can still
        // click ▸ Quick actions to reveal them after the mode change.
        if (auto *qa = findChild<QPushButton*>("aiQuickActionsToggle")) {
            if (qa->isChecked()) qa->setChecked(false);
        }
        if (m_quickActionsWrap)  m_quickActionsWrap->setVisible(false);
        if (m_resultActionsWrap) m_resultActionsWrap->setVisible(false);

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
        // v0.1.57 — Coding-mode welcome card (parallel to the Data path).
        // Shown only when Coding is on AND the chat is empty AND the user
        // hasn't dismissed it. Removed whenever Coding is left.
        const bool showCodingWelcome =
            coding && m_messages.isEmpty() && !Config::instance().aiHideCodingWelcome;
        if (showCodingWelcome) {
            renderCodingWelcomeCard();
        } else {
            removeCodingWelcomeCard();
        }
        // v0.1.54 — when the welcome card is up, it carries its OWN
        // "Manage Connections…" button right next to the connection
        // count. The standalone external button on the data row is
        // duplicate chrome in that case — hide it. When the welcome
        // card is hidden (chat in progress, or user clicked Hide), the
        // external button is the only way to reach the dialog, so we
        // show it.
        if (m_manageConnsBtn) m_manageConnsBtn->setVisible(data && !showWelcome);
        if (m_browseSchemasBtn) m_browseSchemasBtn->setVisible(data && !showWelcome);

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

        // v0.1.61 (Item 8) — chat surface is now ONE conversation surface,
        // controlled by the bottom 3-segment toggle (Chat / Compose / Agent).
        // Default segment by intent: Coding → Agent, Data → Chat,
        // otherwise Chat. Switching ALSO refreshes the input placeholder
        // via chatModeSelectorChanged.
        if (m_chatSegBtn && m_composeSegBtn && m_agentSegBtn) {
            QAbstractButton *target =
                  coding ? m_agentSegBtn
                : data   ? m_chatSegBtn
                         : m_chatSegBtn;
            if (target && !target->isChecked()) {
                target->setChecked(true);
            }
        }

        if (m_chatLayout) renderTranscript();
        emit codingModeRequested(coding);
        emit codingModeChanged(coding);   // v0.1.61 — explorer-visibility gate
    };

    // v0.1.57 — Coding mode AUTO-EXPANDS the AI dock to fullscreen so the
    // user gets the dedicated coding surface (Composer, multi-file edits,
    // diff preview) at full size. Switching back to Chat / Data restores
    // the previous splitter layout. The user can still hit ⛶ to override
    // mid-session — if they do, that override stands until the next mode
    // change.
    //
    // hardening: mode-toggle mid-stream cancels the in-flight request first
    // so the response doesn't keep streaming into the now-rebuilt chat surface
    // (renderTranscript inside applyMode wipes m_streamingCard / m_streamingBody).
    auto applyModeWithCancel = [this, applyMode]() {
        if (m_inAssistantBubble && m_ollama) {
            m_ollama->cancel();   // hardening: kill in-flight stream on mode swap
            m_inAssistantBubble = false;
            if (m_stopBtn) m_stopBtn->setEnabled(false);
        }
        applyMode();
        decorateModelsByMode();
    };
    connect(m_chatMode,   &QAbstractButton::toggled, this,
            [this, applyModeWithCancel](bool on) {
                if (on) { applyModeWithCancel(); emit fullscreenToggled(false); }
            });
    connect(m_codingMode, &QAbstractButton::toggled, this,
            [this, applyModeWithCancel](bool on) {
                if (on) { applyModeWithCancel(); emit fullscreenToggled(true); }
            });
    // v0.1.61 — Data mode is AI-first and auto-fullscreens like Coding
    // mode. Pre-fix Data mode stayed in split layout, which conflicted
    // with the request to have a committed Data-mode workflow.
    connect(m_dataMode,   &QAbstractButton::toggled, this,
            [this, applyModeWithCancel](bool on) {
                if (on) { applyModeWithCancel(); emit fullscreenToggled(true); }
            });

    // Refresh the data-mode capability banner when the user picks a
    // different model. v0.1.53: includes the local recommendation
    // (`qwen2.5-coder:14b`) — pre-fix the suggestion list was cloud-only,
    // which made local-Ollama users think the only fix was paying for a
    // cloud API.
    // v0.1.55 — Ollama capability probe. Whenever the user changes the
    // model dropdown AND the active backend is local Ollama, ask
    // /api/show for that model's capabilities. We cache the result
    // (m_ollamaModelCaps[name]) and consult it in sendPrompt before
    // falling back to the substring allowlist. This is what lets new
    // tool-capable Ollama models (Gemma 3 Instruct with function calling,
    // Phi-4, Magistral, Granite-3.3, etc.) actually work in Coding Mode
    // without needing a Notepatra release to update the allowlist.
    if (m_modelCombo) {
        connect(m_modelCombo, &QComboBox::currentTextChanged,
                this, [this](const QString &) {
            if (!m_ollama || m_ollama->backend() != OllamaClient::Ollama) return;
            const int idx = m_modelCombo->currentIndex();
            const QString id = (idx >= 0)
                ? m_modelCombo->itemData(idx, Qt::UserRole).toString()
                : QString();
            const QString name = id.isEmpty() ? m_modelCombo->currentText().trimmed() : id;
            if (name.isEmpty() || name.startsWith('(')) return;
            if (m_ollamaModelCaps.contains(name)) return;  // cached
            m_ollama->showModel(name);
        });
        // The two connect(m_ollama, …) calls that pair with the probe above
        // are deferred until after m_ollama is constructed (search this
        // file for "modelCapabilitiesLoaded" later in the constructor).
    }

    if (m_modelCombo) {
        connect(m_modelCombo, &QComboBox::currentTextChanged,
                this, [this](const QString &modelName) {
            if (!m_dataMode || !m_dataMode->isChecked() || !m_dataCapBanner) return;
            const bool capable = AiTools::modelCapableOfDataAnalysis(modelName);
            if (capable) {
                m_dataCapBanner->setVisible(false);
            } else {
                m_dataCapBanner->setText(tr(
                    "⚠ %1 is too small for reliable multi-table SQL and chart specs.<br>"
                    "<b>Local (free)</b>: <code>ollama pull qwen2.5-coder:14b</code> "
                    "(or any Qwen-Coder / DeepSeek-Coder / Llama 7B+)<br>"
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

    // v0.1.59 — gate input on model readiness. Whenever the dropdown text
    // changes (initial population, refresh, user pick, "(detecting…)" →
    // real model, etc.), re-check whether the input + Send button should be
    // active. The dropdown's own enabled state already reflects backend
    // health (set false on offline/no-models/api-key-required, true once a
    // probe returns at least one model), so we mirror that with a small
    // textual sanity-check (entries wrapped in parens like "(Ollama
    // offline)" never count as a real model even if the combo is enabled).
    if (m_modelCombo) {
        connect(m_modelCombo, &QComboBox::currentTextChanged,
                this, [this](const QString &) { updateInputAvailability(); });
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

        // v0.1.55 — "Browse Schemas" button opens the DbTreeDialog so the
        // user can drill into connections → schemas → tables → columns
        // and pin a table's schema for the next AI prompt without making
        // the agent do an introspection round-trip.
        m_browseSchemasBtn = new QPushButton(tr("Browse Schemas…"));
        m_browseSchemasBtn->setStyleSheet(
            "QPushButton { font-size: 11px; padding: 4px 10px; "
            "background: #4A6FA5; color: white; border: none; border-radius: 4px; }"
            "QPushButton:hover { background: #5A7FB5; }");
        m_browseSchemasBtn->setToolTip(tr(
            "Lazy-load tables/columns from any saved DB connection. "
            "Right-click a table to pin its schema for the next AI question."));
        dataRow->addWidget(m_browseSchemasBtn);
        connect(m_browseSchemasBtn, &QPushButton::clicked, this, [this]() {
            DbTreeDialog dlg(this);
            connect(&dlg, &DbTreeDialog::schemaForAi, this,
                [this](const QString &blob) {
                    m_pendingSchemaPin = blob;
                });
            dlg.exec();
        });

        dataRow->addStretch();
        layout->addLayout(dataRow);

        // v0.1.56: banner moved to its OWN row below the button row so it
        // gets the full panel width to wrap into. Previously it shared a
        // QHBoxLayout with the buttons and any text past ~600 px clipped
        // off the right edge instead of wrapping to a second line.
        m_dataCapBanner = new QLabel;
        m_dataCapBanner->setWordWrap(true);
        m_dataCapBanner->setMinimumWidth(0);
        {
            QSizePolicy sp(QSizePolicy::Ignored, QSizePolicy::Minimum);
            sp.setHeightForWidth(true);
            m_dataCapBanner->setSizePolicy(sp);
        }
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
        layout->addWidget(m_dataCapBanner);
        m_manageConnsBtn->setVisible(false);
        if (m_browseSchemasBtn) m_browseSchemasBtn->setVisible(false);
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
    // v0.1.56: Ignored horizontal + heightForWidth(true) makes the label
    // recompute its required height every time the panel width changes.
    // Without heightForWidth the layout caches the original sizeHint and
    // the label still clips when the user drags the dock narrower.
    {
        QSizePolicy sp(QSizePolicy::Ignored, QSizePolicy::Minimum);
        sp.setHeightForWidth(true);
        m_statusLabel->setSizePolicy(sp);
    }
    m_statusLabel->setMinimumWidth(0);
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

    // v0.1.61 (Item 8) — Edit Plan list lives INLINE inside the chat
    // scroll content (parented to m_chatContent), positioned after the
    // stretch so it always sits at the bottom of the transcript when
    // it has rows. Hidden by default; addEdit() makes it visible.
    // Replaces the previous QTabWidget(Chat | Composer) split.
    m_editPlan = new EditPlanList(m_chatContent);
    m_editPlan->setVisible(false);
    m_chatLayout->addWidget(m_editPlan, 0);

    // Apply pipeline — when the user clicks Apply All / Apply Selected
    // inside EditPlanList, write each (absPath, afterText) to disk and
    // open / reload the file in the editor.
    connect(m_editPlan, &EditPlanList::applyRequested,
            this,        &AIPanel::applyComposerEdits);
    // v0.1.61 (Item 8) — auto-hide the inline panel when the last row
    // is removed (Reject All / per-row [x] / Apply pipeline housekeeping).
    connect(m_editPlan, &EditPlanList::editRemoved, this,
            [this](const QString &) {
                if (m_editPlan && m_editPlan->count() == 0) {
                    m_editPlan->setVisible(false);
                }
            });

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
    actionsToggle->setObjectName("aiQuickActionsToggle");
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

    // v0.1.55 — prominent quick-action buttons. Pre-v0.1.55 these were
    // 24 px tall flat text labels that read more like footer chrome than
    // primary affordances. Now: 32 px tall, accent border, hover lift,
    // sized to actually invite clicks. The user asked to make them
    // "proper prominent" once revealed.
    const QString quickActionStyle = QString(
        "QPushButton { background: %1; color: %2; border: 1px solid %3; "
        "border-radius: 5px; font-size: 12px; font-weight: 500; padding: 0 12px; }"
        "QPushButton:hover { background: %4; border-color: %5; color: %5; }"
        "QPushButton:pressed { background: %5; color: white; border-color: %5; }")
        .arg(pal.btnBg, pal.inputText, pal.btnBorder, pal.btnHover, pal.accent);

    auto *actionsRow1 = new QHBoxLayout;
    actionsRow1->setContentsMargins(8, 4, 8, 4);
    actionsRow1->setSpacing(6);
    auto *explainBtn = new QPushButton("Explain");
    auto *fixBugsBtn = new QPushButton("Find Bugs");
    auto *refactorBtn = new QPushButton("Refactor");
    auto *testsBtn = new QPushButton("Write Tests");
    for (auto *b : {explainBtn, fixBugsBtn, refactorBtn, testsBtn}) {
        b->setFixedHeight(32);
        b->setStyleSheet(quickActionStyle);
        b->setCursor(Qt::PointingHandCursor);
        // v0.1.49 — guarantee min width fits the label + padding so Windows
        // (Segoe UI ~20 % wider than DejaVu) doesn't truncate "Write Tests"
        // / "Find Bugs" / "Add Comments" with "...".
        b->setMinimumWidth(b->fontMetrics().horizontalAdvance(b->text()) + 28);
        actionsRow1->addWidget(b);
    }
    quickWrapV->addLayout(actionsRow1);

    auto *actionsRow2 = new QHBoxLayout;
    actionsRow2->setContentsMargins(8, 0, 8, 4);
    actionsRow2->setSpacing(6);
    auto *commentBtn = new QPushButton("Add Comments");
    auto *docBtn = new QPushButton("Generate Docs");
    auto *optimizeBtn = new QPushButton("Optimize");
    auto *translateBtn = new QPushButton("Translate");
    for (auto *b : {commentBtn, docBtn, optimizeBtn, translateBtn}) {
        b->setFixedHeight(32);
        b->setStyleSheet(quickActionStyle);
        b->setCursor(Qt::PointingHandCursor);
        b->setMinimumWidth(b->fontMetrics().horizontalAdvance(b->text()) + 28);
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
        b->setFixedHeight(32);
        b->setStyleSheet(quickActionStyle);
        b->setCursor(Qt::PointingHandCursor);
        b->setMinimumWidth(b->fontMetrics().horizontalAdvance(b->text()) + 28);
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
    // Auto-grow: resize to fit content (including visually-wrapped long lines)
    // up to maximumHeight. Uses the document's rendered height rather than
    // blockCount() so a single typed line that wraps still triggers a grow.
    // Empty document collapses back to the 40px single-line default — that's
    // the post-Send / post-clear() behaviour the user expects.
    auto resizeInput = [this]() {
        m_customInput->document()->setTextWidth(
            m_customInput->viewport()->width());
        const int docH = qCeil(m_customInput->document()->size().height());
        const int padding = 18; // matches the 8px top/bottom padding + frame
        const int h = qBound(40, docH + padding, 140);
        m_customInput->setFixedHeight(h);
    };
    connect(m_customInput->document(), &QTextDocument::contentsChanged, this, resizeInput);
    // Enter = send, Shift+Enter = newline. The eventFilter() hook below
    // intercepts both Return key handling AND a Resize event so the
    // wrap-driven height recalculates when the dock is resized.
    m_customInput->installEventFilter(this);
    customRow->addWidget(m_customInput, 1);

    // v0.1.56 — `@file` mention picker. When the caret is in a word that
    // starts with `@` (and the `@` is on a word boundary, i.e. the char
    // before it is not alphanumeric), pop the completer. Picking inserts
    // `@<relative-path> ` into the input, replacing the typed `@xxx`.
    // Empty workspace → completer stays empty, no popup, no crash.
    {
        m_filementionCompleter = new QCompleter(this);
        m_filementionCompleter->setModel(new QStringListModel(m_filementionCompleter));
        m_filementionCompleter->setWidget(m_customInput);
        m_filementionCompleter->setCompletionMode(QCompleter::PopupCompletion);
        m_filementionCompleter->setCaseSensitivity(Qt::CaseInsensitive);
        m_filementionCompleter->setFilterMode(Qt::MatchContains);
        if (auto *popup = m_filementionCompleter->popup()) {
            popup->setObjectName("aiFileMentionPopup");
        }

        // Returns the `@…` token at the caret (without the leading `@`),
        // or an empty QString when the caret is not inside one. A token
        // is recognised when:
        //   * the previous non-token char is start-of-line or non-alnum
        //     (so "foo@bar" isn't treated as a mention)
        //   * the run of chars after `@` so far contains no whitespace
        auto mentionTokenAtCursor = [this]() -> QString {
            QTextCursor cur = m_customInput->textCursor();
            if (cur.hasSelection()) return QString();
            const QString block = cur.block().text();
            const int pos = cur.positionInBlock();
            // Walk backwards from the caret to find the most recent `@`,
            // bailing if we hit whitespace or a path-illegal char first.
            int i = pos - 1;
            while (i >= 0) {
                const QChar c = block.at(i);
                if (c == QLatin1Char('@')) {
                    // Word-boundary check: char before `@` must be SOL or
                    // a non-alnum / non-`_` char. Otherwise this is
                    // something like "user@host" — not a mention.
                    if (i == 0) return block.mid(i + 1, pos - i - 1);
                    const QChar prev = block.at(i - 1);
                    if (!prev.isLetterOrNumber() && prev != QLatin1Char('_')) {
                        return block.mid(i + 1, pos - i - 1);
                    }
                    return QString();
                }
                if (c.isSpace()) return QString();
                --i;
            }
            return QString();
        };

        // Open / refresh the completer popup when a mention token is
        // active at the caret. Closes silently when no token, no
        // workspace files, or no matches.
        auto refreshMentionPopup = [this, mentionTokenAtCursor]() {
            if (!m_filementionCompleter) return;
            const QString token = mentionTokenAtCursor();
            QAbstractItemView *popup = m_filementionCompleter->popup();
            if (token.isNull() || m_workspaceFilePaths.isEmpty()) {
                if (popup) popup->hide();
                return;
            }
            // Re-seed the model lazily — the workspace file list can
            // change between turns (e.g. user opened a different folder).
            auto *m = qobject_cast<QStringListModel*>(m_filementionCompleter->model());
            if (m) m->setStringList(m_workspaceFilePaths);
            m_filementionCompleter->setCompletionPrefix(token);
            // No matches → don't show the popup.
            if (m_filementionCompleter->completionCount() == 0) {
                if (popup) popup->hide();
                return;
            }
            // Anchor the popup to the caret, then size it to the input.
            QRect r = m_customInput->cursorRect();
            r.setWidth(qMax(220, m_customInput->width() - 20));
            m_filementionCompleter->complete(r);
        };

        connect(m_customInput->document(), &QTextDocument::contentsChanged,
                this, refreshMentionPopup);

        // Picking a file → replace the `@token` typed so far with
        // `@<path> ` and close the popup. The replacement is scoped to
        // the run from the active `@` up to the current caret.
        connect(m_filementionCompleter,
                QOverload<const QString &>::of(&QCompleter::activated),
                this, [this](const QString &chosen) {
            if (chosen.isEmpty()) return;
            QTextCursor cur = m_customInput->textCursor();
            const QString block = cur.block().text();
            const int pos = cur.positionInBlock();
            const int blockStart = cur.block().position();
            // Find the nearest `@` walking backwards.
            int i = pos - 1;
            while (i >= 0) {
                const QChar c = block.at(i);
                if (c == QLatin1Char('@')) break;
                if (c.isSpace()) { i = -1; break; }
                --i;
            }
            if (i < 0) {
                // Defensive: completer fired but we no longer see the
                // `@` (e.g. user backspaced). Just insert the token.
                cur.insertText(QString("@%1 ").arg(chosen));
                m_customInput->setTextCursor(cur);
                return;
            }
            cur.setPosition(blockStart + i);
            cur.setPosition(blockStart + pos, QTextCursor::KeepAnchor);
            cur.insertText(QString("@%1 ").arg(chosen));
            m_customInput->setTextCursor(cur);
        });
    }

    m_sendBtn = new QPushButton("Send");
    m_sendBtn->setFixedSize(72, 36);
    m_sendBtn->setStyleSheet(QString(
        "QPushButton { background: %1; color: white; border: none; "
        "border-radius: 18px; font-weight: bold; font-size: 13px; }"
        "QPushButton:hover { background: %2; }"
        "QPushButton:pressed { background: %3; }"
        "QPushButton:disabled { background: %1; color: rgba(255,255,255,0.45); }")
        .arg(pal.userBg, pal.userBorder,
             aiIsDark() ? "#0A4F7C" : "#A55B40"));
    customRow->addWidget(m_sendBtn);

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

    // ─── v0.1.61 (Item 8) — Bottom 3-segment toggle ─────────────────────
    // Full-width row sitting BELOW the input bar. Replaces the previous
    // top-tabbed (Chat | Composer) split and brings the panel in line
    // with the iOS / Slack keyboard-accessory mental model: mode is a
    // property of the next message, not a destination. Works alongside
    // the existing Chat / Coding / Data intent selector at the top —
    // that one picks WHICH model gating + system-prompt layer applies;
    // this one picks how the CURRENT conversation should be presented
    // and which tool surface is active.
    //
    //   Chat    — plain conversation, no tools fire
    //   Compose — tools enabled but write_file / apply_diff are forced
    //             to dry_run → routed to the inline Edit Plan list
    //   Agent   — full autonomous loop (tools fire, edits hit disk)
    {
        auto *segWrap = new QHBoxLayout;
        segWrap->setContentsMargins(8, 0, 8, 8);
        segWrap->setSpacing(0);

        auto *segFrame = new QFrame;
        segFrame->setStyleSheet(QString(
            "QFrame { background: %1; border: 1px solid %2; "
            "border-radius: 8px; }")
            .arg(pal.chromeBg, pal.btnBorder));
        auto *segLay = new QHBoxLayout(segFrame);
        segLay->setContentsMargins(2, 2, 2, 2);
        segLay->setSpacing(2);

        // Three segments share the same QSS but differ in corner radius
        // (rounded outer corners on the leftmost / rightmost only) and
        // accent colour while checked. Pattern mirrors the existing
        // Chat/Coding/Data row near line 875.
        auto makeSegBtn = [&pal](const QString &label,
                                 const QString &accentColor,
                                 const QString &tip,
                                 const QString &cornerRule) {
            auto *btn = new QPushButton(label);
            btn->setCheckable(true);
            btn->setCursor(Qt::PointingHandCursor);
            btn->setFixedHeight(26);
            btn->setToolTip(tip);
            btn->setStyleSheet(QString(
                "QPushButton { background: transparent; border: none; "
                "color: %1; font-size: 11px; font-weight: 600; padding: 4px 10px; "
                "%5 } "
                "QPushButton:hover { background: %2; color: %3; } "
                "QPushButton:checked { background: %4; color: white; }")
                .arg(pal.muted, pal.btnHover, pal.inputText,
                     accentColor, cornerRule));
            return btn;
        };

        m_chatSegBtn = makeSegBtn("Chat", pal.accent,
            "Chat — plain conversation, no agentic tool calls. "
            "Picks how the CURRENT conversation behaves; the Chat / "
            "Coding / Data buttons above pick the AI INTENT.",
            "border-top-left-radius: 6px; border-bottom-left-radius: 6px;");
        m_composeSegBtn = makeSegBtn("Compose", QStringLiteral("#4EC9B0"),
            "Compose — tools enabled but write_file / apply_diff are "
            "forced to dry_run. Proposed edits land in the Edit Plan "
            "list (inline below the chat) for you to review + Apply.",
            "border-radius: 0;");
        m_agentSegBtn = makeSegBtn("Agent", QStringLiteral("#E07B5A"),
            "Agent — full autonomous loop. Tool calls fire and edits "
            "hit disk immediately. Use when you trust the model to "
            "drive the workspace end-to-end.",
            "border-top-right-radius: 6px; border-bottom-right-radius: 6px;");

        auto *segGroup = new QButtonGroup(this);
        segGroup->setExclusive(true);
        segGroup->addButton(m_chatSegBtn,    static_cast<int>(ChatModeSegment::Chat));
        segGroup->addButton(m_composeSegBtn, static_cast<int>(ChatModeSegment::Compose));
        segGroup->addButton(m_agentSegBtn,   static_cast<int>(ChatModeSegment::Agent));

        segLay->addWidget(m_chatSegBtn,    1);
        segLay->addWidget(m_composeSegBtn, 1);
        segLay->addWidget(m_agentSegBtn,   1);

        segWrap->addWidget(segFrame, 1);

        // Default state — applyMode below will override based on the
        // active intent (Coding → Agent, Data → Chat, otherwise Chat).
        m_chatSegBtn->setChecked(true);

#if QT_VERSION >= QT_VERSION_CHECK(5, 15, 0)
        connect(segGroup, &QButtonGroup::idToggled, this,
                [this](int id, bool on) {
                    if (on) chatModeSelectorChanged(id);
                });
#else
        connect(segGroup,
                QOverload<int, bool>::of(&QButtonGroup::buttonToggled),
                this,
                [this](int id, bool on) {
                    if (on) chatModeSelectorChanged(id);
                });
#endif

        layout->addLayout(segWrap);
    }

    // Per-message anchorClicked connects are installed in aiAddAssistantCard
    // now (each assistant card has its own QTextBrowser body). The global
    // connect to m_output is obsolete — m_output is no longer used.

    // Wire attach button
    connect(m_attachBtn, &QPushButton::clicked, this, &AIPanel::attachFile);
    connect(m_voiceBtn, &QPushButton::clicked, this, &AIPanel::toggleSpeechToText);

    // Ollama client
    m_ollama = new OllamaClient(this);

    // v0.1.55 — capability-cache wiring (paired with the probe trigger
    // earlier in this constructor). Now that m_ollama exists, the two
    // signal connections are safe.
    connect(m_ollama, &OllamaClient::modelCapabilitiesLoaded, this,
        [this](const QString &model, const QStringList &caps) {
            m_ollamaModelCaps.insert(model, caps);
        });
    connect(m_ollama, &OllamaClient::modelCapabilitiesError, this,
        [this](const QString &model, const QString &) {
            // Cache an empty list so we don't hammer /api/show on every
            // keystroke when the model genuinely lacks capabilities
            // metadata. The substring allowlist still applies.
            m_ollamaModelCaps.insert(model, QStringList());
        });

    connect(m_ollama, &OllamaClient::tokenReceived, this, [this](const QString &token) {
        if (!m_chatLayout) return;  // hardening: ignore tokens after panel teardown
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
        if (m_stopBtn) m_stopBtn->setEnabled(false);  // hardening: guard m_stopBtn (slot may fire after teardown)
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
        if (m_messages.isEmpty()) return;  // hardening: nothing to attach stats to
        for (int i = m_messages.size() - 1; i >= 0; --i) {
            if (m_messages[i].role == ChatMessage::Assistant) {
                m_messages[i].promptTokens = promptTokens;
                m_messages[i].evalTokens   = evalTokens;
                m_messages[i].elapsedMs    = elapsedMs;
                break;
            }
        }
        scheduleChatSave();
        if (m_chatLayout) renderTranscript();  // hardening: skip render if surface gone
    });
    connect(m_ollama, &OllamaClient::error, this, [this](const QString &msg) {
        endAssistantBubble();
        // hardening: surface even an empty error string with a generic notice
        // so a silent backend failure never becomes an invisible failure.
        appendErrorBubble(msg.isEmpty() ? QStringLiteral("AI request failed.") : msg);
        if (m_stopBtn) m_stopBtn->setEnabled(false);  // hardening: guard m_stopBtn
    });
    connect(m_clearBtn, &QPushButton::clicked, this, &AIPanel::clearChat);

    // Dynamic model detection
    connect(m_ollama, &OllamaClient::modelsListed, this, [this](const QStringList &models) {
        if (!m_modelCombo) return;  // hardening: guard against teardown
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
            // When models.isEmpty() llama-server isn't running OR no GGUF
            // is loaded — make the dropdown items clearly read as
            // "download these to use", not "pick an available model".
            const bool serverDown = models.isEmpty();
            const bool serverInstalledHere =
                serverDown && (!QStandardPaths::findExecutable("llama-server").isEmpty() ||
                               !QStandardPaths::findExecutable("llamafile").isEmpty());
            if (!serverDown) {
                m_modelCombo->addItem(QString("● Loaded: %1").arg(models.first()));
                m_modelCombo->setItemData(0, models.first(), Qt::UserRole);
                m_modelCombo->insertSeparator(m_modelCombo->count());
            } else {
                // Disabled header reflects whether llama-server is on PATH
                // ("installed but not running") vs absent entirely.
                const QString header = serverInstalledHere
                    ? QStringLiteral("— llama-server installed but not running · pick a GGUF below —")
                    : QStringLiteral("— llama.cpp not installed · pick one to download a GGUF —");
                m_modelCombo->addItem(header);
                if (auto *m = qobject_cast<QStandardItemModel*>(m_modelCombo->model())) {
                    if (auto *it = m->item(0)) {
                        it->setFlags(it->flags() & ~Qt::ItemIsSelectable & ~Qt::ItemIsEnabled);
                    }
                }
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
                // Prefix with "↓ " when llama-server isn't running so each
                // dropdown row visually reads "download me" instead of
                // looking like an installed-and-ready option.
                const QString rawLabel = QString::fromUtf8(e.label);
                const QString label = serverDown
                    ? QStringLiteral("↓ ") + rawLabel
                    : rawLabel;
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
            // v0.1.56 — distinguish "not installed" from "installed but not
            // running". `which llama-server` is the cheap test; if the
            // binary is on PATH we can give a more accurate hint.
            const bool serverInstalled =
                !QStandardPaths::findExecutable("llama-server").isEmpty() ||
                !QStandardPaths::findExecutable("llamafile").isEmpty();
            QString status;
            if (!models.isEmpty()) {
                status = QString("llama.cpp · loaded %1 · plus %2 catalog options below")
                            .arg(models.first()).arg(sizeof(CATALOG)/sizeof(CATALOG[0]));
            } else if (serverInstalled) {
                status = QString("llama-server is installed but not running. "
                                 "Pick a GGUF below and run `llama-server -m <file>`, "
                                 "then click ↻.");
            } else {
                status = QString("llama.cpp not installed. Install it (brew install "
                                 "llama.cpp · or build from github.com/ggml-org/llama.cpp), "
                                 "pick a GGUF below, and run `llama-server -m <file>`.");
            }
            setStatus(status, false);
            decorateModelsByMode();
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

        // v0.1.55 — for cloud OpenAI-compat backends we DON'T render any
        // hardcoded catalog any more. The flat `models` list arriving here
        // (just IDs) is too sparse for a useful tree — we wait for the
        // richer data via modelsListedRich and group it there. If the
        // probe failed entirely (modelsError fires), the disk cache loader
        // populates the dropdown from a previous successful fetch.
        if (isOpenRouter || isOpenAI) {
            if (models.isEmpty()) {
                m_modelCombo->addItem(isOpenRouter
                    ? "(loading OpenRouter catalog…)"
                    : "(loading OpenAI catalog…)");
                m_modelCombo->setEnabled(false);
            } else {
                m_modelCombo->addItems(models);
                m_modelCombo->setEnabled(true);
            }
            // Status will be set by the rich handler once the grouped tree
            // is rendered, OR by modelsError when the cache fallback kicks
            // in. Don't overwrite it here.
            decorateModelsByMode();
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
        decorateModelsByMode();
    });
    // v0.1.54 — dispatch the offline message by backend AND keep the
    // curated catalog usable for backends where it makes sense even
    // without a live probe (llama.cpp catalog, OpenRouter / OpenAI
    // curated picks). Pre-fix this handler unconditionally cleared and
    // disabled the combo, killing the catalog logic added in v0.1.53.
    // v0.1.55 — live tree-grouped renderer for OpenRouter / OpenAI. Fires
    // when the rich /v1/models response arrives. Groups by provider prefix
    // (anthropic / openai / google / meta-llama / mistralai / qwen / x-ai
    // / minimax / moonshotai / deepseek / nvidia / others), renders
    // section headers + per-model entries with price suffix, caches the
    // raw array to disk so subsequent offline launches show the last
    // known catalog. The user can still type a custom model id in the
    // editable combo (setEditable(true) above) — that bypasses the tree
    // entirely.
    connect(m_ollama, &OllamaClient::modelsListedRich, this,
            [this](const QJsonArray &dataArr) {
        const QString baseUrl = Config::instance().aiBaseUrl;
        const bool isOpenRouter = baseUrl.contains("openrouter", Qt::CaseInsensitive);
        const bool isOpenAI     = baseUrl.contains("openai.com", Qt::CaseInsensitive);
        if (!isOpenRouter && !isOpenAI) return;

        renderGroupedModelTree(dataArr);
        // Cache the raw response so a future offline launch falls back to
        // the last successful fetch.
        const QString cacheDir = QStandardPaths::writableLocation(
            QStandardPaths::ConfigLocation) + "/notepatra/cache";
        QDir().mkpath(cacheDir);
        const QString cacheFile = cacheDir + "/" +
            (isOpenRouter ? "openrouter-models.json" : "openai-models.json");
        QJsonObject cache;
        cache["fetched"] = QDateTime::currentSecsSinceEpoch();
        cache["data"] = dataArr;
        QFile f(cacheFile);
        if (f.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
            f.write(QJsonDocument(cache).toJson(QJsonDocument::Compact));
            f.close();
        }
    });

    connect(m_ollama, &OllamaClient::modelsError, this, [this](const QString &reason) {
        Q_UNUSED(reason);
        if (!m_modelCombo) return;  // hardening: skip if dropdown gone
        const QString backend = Config::instance().aiBackend;
        const QString baseUrl = Config::instance().aiBaseUrl;
        const bool isLlamaCpp   = backend.compare("llama.cpp", Qt::CaseInsensitive) == 0;
        const bool isOpenRouter = baseUrl.contains("openrouter", Qt::CaseInsensitive);
        const bool isOpenAI     = baseUrl.contains("openai.com", Qt::CaseInsensitive);

        if (isOpenRouter || isOpenAI) {
            // v0.1.55 — for cloud backends, fall back to the last cached
            // /v1/models response if any (per the user's "only list if
            // API succeeds" rule, the cache only exists when a previous
            // fetch was successful, so this is safe). No hardcoded list.
            const QString cacheDir = QStandardPaths::writableLocation(
                QStandardPaths::ConfigLocation) + "/notepatra/cache";
            const QString cacheFile = cacheDir + "/" +
                (isOpenRouter ? "openrouter-models.json" : "openai-models.json");
            QFile f(cacheFile);
            if (f.exists() && f.open(QIODevice::ReadOnly)) {
                QJsonDocument doc = QJsonDocument::fromJson(f.readAll());
                f.close();
                const qint64 fetched = doc.object().value("fetched").toVariant().toLongLong();
                const QJsonArray dataArr = doc.object().value("data").toArray();
                if (!dataArr.isEmpty()) {
                    renderGroupedModelTree(dataArr);
                    const qint64 ageH = (QDateTime::currentSecsSinceEpoch() - fetched) / 3600;
                    setStatus(QString("Showing cached %1 catalog (%2 models, %3 h old). "
                                      "Click ↻ to refresh from %4 API.")
                                  .arg(isOpenRouter ? "OpenRouter" : "OpenAI")
                                  .arg(dataArr.size()).arg(ageH)
                                  .arg(isOpenRouter ? "openrouter.ai" : "api.openai.com"),
                              false);
                    return;
                }
            }
            // No cache and no live data → empty editable combo. User can
            // still type a custom model id by hand.
            m_modelCombo->clear();
            m_modelCombo->setEditable(true);
            m_modelCombo->setEnabled(true);
            setStatus(isOpenRouter
                  ? tr("OpenRouter API unreachable — paste API key + click ↻, "
                       "or type a custom model id (e.g. anthropic/claude-3.5-sonnet)")
                  : tr("OpenAI API unreachable — paste API key + click ↻, "
                       "or type a custom model id (e.g. gpt-4o)"),
                true);
            return;
        }

        if (isLlamaCpp) {
            // llama.cpp catalog stays usable when server is offline (those
            // models are downloadable via tooltip URL, server-independent).
            emit m_ollama->modelsListed(QStringList());
            setStatus(tr("llama.cpp not running — pick a model below and run "
                         "`llama-server -m <file>`"),
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
    connect(m_sendBtn, &QPushButton::clicked, this, [this]() {
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
        // hardening: safe to invoke even if the reply already finished
        // (m_inAssistantBubble is false by then). Also guard m_stopBtn
        // pointer in case the slot fires during teardown.
        if (m_stopBtn) m_stopBtn->setEnabled(false);
        // hardening: also flush any half-built agent state so the next
        // turn doesn't carry stale tool-result fragments from the aborted run.
        m_pendingToolResults = QJsonArray();
        m_toolCallsThisTurn = 0;
        m_toolCallsTotal = 0;
        m_toolsActiveThisTurn = false;
        if (m_inAssistantBubble) {
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

    // v0.1.59 — initial state. The dropdown almost always says
    // "(detecting…)" at this point (Ollama probe is in flight, OpenAI/
    // OpenRouter catalog is loading, etc.), so the input should start
    // disabled. The currentTextChanged connect installed earlier will
    // re-fire and enable it once a real model name lands.
    updateInputAvailability();
}

bool AIPanel::isCodingMode() const {
    return m_codingMode && m_codingMode->isChecked();
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

    // Slice C — keep the Edit Plan's workspace root in sync so the per-row
    // path label renders relative to the project (instead of dumping a
    // full /home/.../project/src/foo.cpp on every row).
    if (m_editPlan) m_editPlan->setWorkspaceRoot(workspaceRoot);

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
    if (!m_ollama) {  // hardening: guard m_ollama (refreshModels can fire from QTimer::singleShot before ctor wires it up)
        setStatus(QStringLiteral("AI client not ready"), true);
        return;
    }
    setStatus("Detecting Ollama models...", false);
    m_ollama->listModels();
}

void AIPanel::setStatus(const QString &text, bool isError) {
    if (!m_statusLabel) return;  // hardening: guard m_statusLabel (status calls happen during teardown / early ctor)
    m_statusLabel->setText(text);
    // v0.1.54 — theme-aware status colours. The previous hard-coded
    // #4EC9B0 (mint/teal) and #F48771 (salmon) read fine on Dark but
    // washed out badly on Light theme — the user reported the
    // "llama.cpp not running" hint was barely visible.
    const bool dark = aiIsDark();
    const QString successFg = dark ? QStringLiteral("#4EC9B0")
                                   : QStringLiteral("#1B7A3E");
    const QString errorFg   = dark ? QStringLiteral("#F48771")
                                   : QStringLiteral("#B92E1B");
    m_statusLabel->setStyleSheet(QString(
        "color: %1; padding: 2px 8px; font-size: 12px; font-weight: 500;")
        .arg(isError ? errorFg : successFg));
}

// v0.1.56 — `@file` mention resolver. Scans `userText` (which is the
// outgoing prompt, NOT the visible bubble) for `@<path>` tokens at word
// boundaries, resolves each via AiTools::resolveSafePath against
// m_workspaceRoot, reads the file (cap 64 KB / file, 256 KB total
// across all mentions), and appends each as
//   \n\n[file: <relpath>]\n<content>\n[end file]
// to `userText`. The function is a no-op when:
//   * the workspace root is empty (no folder open), OR
//   * no `@<path>` tokens are found, OR
//   * resolveSafePath rejects every mention (e.g. user typed a path
//     outside the workspace, or the file disappeared between the time
//     the picker was shown and Send).
// Mentions that fail to resolve are silently dropped from the prompt
// payload but the bare `@<path>` token still ships in the visible bubble
// so the user can see what they tried to attach.
void AIPanel::resolveFileMentions(QString &userText) {
    if (m_workspaceRoot.isEmpty()) return;

    static const qint64 PER_FILE_CAP   = 64 * 1024;   // 64 KB / file
    static const qint64 TOTAL_CAP      = 256 * 1024;  // 256 KB across all

    // Match `@<path>` where the leading `@` is on a word boundary (start
    // of string OR preceded by whitespace / non-alnum). The path body
    // accepts characters that are typically legal in relative POSIX
    // paths: letters, digits, `_`, `-`, `.`, `/`. We deliberately stop
    // at whitespace and at most punctuation that is typically NOT in a
    // path (commas, parentheses, quotes), so trailing punctuation in
    // the user's sentence ("see @foo.py.") doesn't get sucked into the
    // token.
    static const QRegularExpression mentionRx(
        QStringLiteral("(?:^|[^A-Za-z0-9_])@([A-Za-z0-9_\\-./]+)"));

    QSet<QString> seen;          // dedupe — pasting "@foo.py @foo.py" reads once
    qint64 totalBytes = 0;

    auto it = mentionRx.globalMatch(userText);
    while (it.hasNext()) {
        const QRegularExpressionMatch m = it.next();
        const QString rel = m.captured(1);
        if (rel.isEmpty()) continue;
        if (seen.contains(rel)) continue;
        seen.insert(rel);

        // Workspace-anchor + symlink + deny-list checks.
        QString canonical;
        QString errorKind;
        if (!AiTools::resolveSafePath(rel, m_workspaceRoot, &canonical, &errorKind))
            continue;

        QFileInfo fi(canonical);
        if (!fi.isFile() || !fi.isReadable()) continue;

        QFile f(canonical);
        if (!f.open(QIODevice::ReadOnly)) continue;

        // Per-file cap. Reading PER_FILE_CAP+1 lets us know whether the
        // file was truncated, so we can flag it to the model.
        const QByteArray raw = f.read(PER_FILE_CAP + 1);
        f.close();

        QByteArray contentBytes = raw;
        bool truncated = false;
        if (contentBytes.size() > PER_FILE_CAP) {
            contentBytes.truncate(PER_FILE_CAP);
            truncated = true;
        }

        // Total-cap gate. If this file would push us over the global
        // budget, skip it entirely (better than silently truncating a
        // file the user explicitly mentioned to half its size).
        if (totalBytes + contentBytes.size() > TOTAL_CAP) continue;
        totalBytes += contentBytes.size();

        const QString content = QString::fromUtf8(contentBytes);
        userText += QStringLiteral("\n\n[file: %1]\n%2%3\n[end file]")
                        .arg(rel,
                             content,
                             truncated ? QStringLiteral("\n... (truncated)")
                                       : QString());
    }
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
    // v0.1.61 — model dropdown enable/disable transitions retrigger the
    // input gate. See the installEventFilter call where m_modelCombo is
    // created for the full rationale. Defer to next event-loop tick so any
    // currentText change that came along with the enable lands first.
    if (obj == m_modelCombo && evt->type() == QEvent::EnabledChange) {
        QTimer::singleShot(0, this, [this]() { updateInputAvailability(); });
        return false;  // don't swallow — let the widget process the event
    }
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
    // hardening: bail early if core widgets were never built or m_ollama is null.
    // sendPrompt can be reached via QButtonGroup activation that fires before
    // ctor fully wires every member, or after a teardown signal storm.
    if (!m_ollama || !m_customInput || !m_modelCombo) {
        setStatus(QStringLiteral("AI panel not ready — try again in a moment"), true);
        return;
    }
    // Pull the freshest workspace state right before we send. The provider
    // lets MainWindow push the current editor text + open-tab list without
    // us having to subscribe to every edit signal.
    if (m_contextProvider) m_contextProvider(this);

    // v0.1.55 — resolve the model id from the dropdown. For tree-grouped
    // dropdowns (OpenRouter), the visible text is a decorated label like
    // "    claude-opus-4-5  ·  $15/$75 per M" — we don't want to send THAT
    // verbatim to the API. The actual model id ("anthropic/claude-opus-4-5")
    // is stashed at Qt::UserRole on each item by renderGroupedModelTree.
    // Fall back to currentText for editable / Ollama / llama.cpp dropdowns
    // where the visible text IS the id.
    QString model;
    {
        const int idx = m_modelCombo->currentIndex();
        const QString userRoleId = (idx >= 0)
            ? m_modelCombo->itemData(idx, Qt::UserRole).toString()
            : QString();
        const QString typed = m_modelCombo->currentText().trimmed();
        // If the user typed a custom id (one that's not in the dropdown),
        // currentText differs from the visible label of the highlighted
        // item — prefer the typed text in that case so paste-a-custom-id
        // still works.
        const QString visible = (idx >= 0) ? m_modelCombo->itemText(idx) : QString();
        if (!userRoleId.isEmpty() && typed == visible.trimmed())
            model = userRoleId;
        else
            model = typed;
    }
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

    // v0.1.61 — pre-send capability + context guard. Refuse politely BEFORE
    // dispatching when the selected model can't do what's needed (no tool
    // calls) OR the prerequisite workspace/DB context hasn't been wired
    // (Coding without an open file/folder, Data without a configured
    // connection). Otherwise the model generates plausible prose that
    // fails silently, and the failure mode is invisible to a new user.
    // Skip for the plain Chat path — chat doesn't need workspace/DB.
    {
        const bool codingNow = (m_codingMode && m_codingMode->isChecked());
        const bool dataNow   = (m_dataMode   && m_dataMode->isChecked());

        // Context gate: Coding needs a workspace root. Data needs at least
        // one saved DB connection. Refuse with a one-click setup pointer.
        if (codingNow && m_workspaceRoot.isEmpty()) {
            appendErrorBubble(
                "Coding Mode needs an open folder so I can read/edit your "
                "files. Open one via File → Open Folder… (or drag a folder "
                "onto the window), then ask again. If you just want to chat "
                "about code in general (no file ops), switch to Chat mode.");
            return;
        }
        if (dataNow && DbConnections::loadAll().isEmpty()) {
            appendErrorBubble(
                "Data Mode needs at least one database connection (SQLite, "
                "PostgreSQL, MySQL, SQL Server, or DuckDB). Click "
                "Manage Connections… in the Data row above to add one, "
                "then ask again. If you just have a CSV / Parquet file, add "
                "it as a DuckDB connection pointing at the file path.");
            return;
        }

        if (codingNow && !currentModelSupportsTools()) {
            appendErrorBubble(QString(
                "The selected model '%1' doesn't support tool calls, "
                "which Coding Mode needs to read your files and apply edits. "
                "Switch to a tool-capable model:\n\n"
                "  • Local (Ollama):  qwen2.5-coder:7b · qwen2.5-coder:14b · "
                "llama3.1:8b · mistral-nemo\n"
                "  • Cloud (OpenRouter / direct):  claude-sonnet-4-5 · "
                "claude-opus-4-7 · gpt-5 · gpt-4o · gemini-2.5-flash\n\n"
                "Tip: tool-capable models render in green in the dropdown; "
                "the amber ones can't call tools. Or turn off Coding Mode if "
                "you just want to chat.").arg(model));
            return;
        }
        if (dataNow && !AiTools::modelCapableOfDataAnalysis(model)) {
            appendErrorBubble(QString(
                "The selected model '%1' isn't strong enough for the Data "
                "Analyst mode — that workflow needs both tool calls (to run "
                "SQL against your DB) AND structured reasoning (to plan "
                "queries + summarise results). Switch to:\n\n"
                "  • Local (Ollama):  qwen2.5-coder:14b (recommended) · "
                "qwen2.5-coder:7b · deepseek-coder:33b\n"
                "  • Cloud:  claude-sonnet-4-5 · claude-opus-4-7 · gpt-5 · "
                "gpt-4o · gemini-2.5-pro · deepseek-r1\n\n"
                "Tip: data-capable models render in green; amber ones won't "
                "be reliable for SQL + charts. Or turn off Data Mode if you "
                "just want to chat.").arg(model));
            return;
        }
    }

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
        && currentModelSupportsTools();
    // v0.1.61 (Item 8) — Compose mode flips on when the user has the
    // bottom segment toggle set to "Compose" while Coding intent is
    // active. The composerModeLayer in the system prompt instructs the
    // model to ALWAYS pass dry_run:true on write_file / apply_diff,
    // which AIPanel::handleToolCall then routes to the inline Edit Plan
    // list instead of touching disk directly.
    const bool composerActive = (intent == AiSystemPrompt::Intent::CodingStrict)
        && m_chatModeSegment == ChatModeSegment::Compose;
    QString systemPrompt;
    if (intent == AiSystemPrompt::Intent::DataAnalyst) {
        // Pull .notepatra/data-analyst.md (if present) so the model gets
        // project-specific instructions automatically.
        const QString projectCtx =
            AiSystemPrompt::readDataAnalystInstructions(m_workspaceRoot);
        systemPrompt = AiSystemPrompt::buildWithProjectContext(
            intent, m_language, willUseTools, projectCtx, composerActive);
    } else {
        systemPrompt = AiSystemPrompt::build(intent, m_language, willUseTools, composerActive);
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

    // v0.1.55 — consume any pending schema pin from the DB tree dialog.
    // The user clicked "Send schema to AI" on a table; we staged the
    // blob in m_pendingSchemaPin. Prepend it to the system prompt for
    // THIS turn only, then clear so it doesn't bleed into future turns.
    if (!m_pendingSchemaPin.isEmpty()) {
        systemPrompt += "\n\n--- Pinned table schema (from DB tree) ---\n"
                      + m_pendingSchemaPin;
        m_pendingSchemaPin.clear();
    }

    // v0.1.55 — file-access policy (defined here so all downstream
    // gates — system-prompt pin, quick-action fallback, workspace block —
    // share the same answer). Chat and Data modes are HARD-blocked from
    // file access; Coding mode is gated on the privacy toggle (default
    // off). Selection is implicit consent and stays unaffected.
    const bool codingModeOn    = (m_codingMode && m_codingMode->isChecked());
    const bool shareFileToggle = Config::instance().aiShareOpenFile;
    const bool aiCanSeeFile    = codingModeOn && shareFileToggle;

    // File awareness in the system prompt. Two cases:
    //   * AI can see the file (Coding Mode + Share File toggle ON): pin
    //     a short "Currently open: foo.py" line so phrases like "this
    //     function" disambiguate without burning tokens.
    //   * AI is blind (Chat / Data mode, OR Coding Mode with toggle OFF):
    //     pin a DIFFERENT line — tell the model the user has hidden the
    //     file, and that if asked it should guide them to flip the toggle
    //     and switch to Coding mode. This is what makes "can you see
    //     this file?" → "no, but I can if you check 🔒 Share file in the
    //     AI panel and switch to Coding mode" — exactly the safety UX.
    if (aiCanSeeFile && (!m_currentFilePath.isEmpty() || !m_currentFileText.isEmpty())) {
        QString openLine = "\n\nUser's currently-open editor tab: ";
        if (!m_currentFilePath.isEmpty()) {
            QFileInfo fi(m_currentFilePath);
            QString shown = m_currentFilePath;
            if (!m_workspaceRoot.isEmpty()
                && shown.startsWith(m_workspaceRoot)) {
                shown = shown.mid(m_workspaceRoot.size());
                if (shown.startsWith('/')) shown.remove(0, 1);
            } else {
                shown = fi.fileName();
            }
            openLine += shown;
        } else {
            openLine += "(unsaved buffer)";
        }
        if (!m_language.isEmpty()) openLine += " · " + m_language;
        const int lineCount = m_currentFileText.count('\n') + 1;
        const int byteSize  = m_currentFileText.toUtf8().size();
        if (byteSize > 0) {
            openLine += QString(" · %1 lines · %2 bytes")
                            .arg(lineCount).arg(byteSize);
        }
        if (m_contextIsSelection && !m_context.isEmpty()) {
            openLine += " · user has a selection highlighted";
        }
        openLine += ". When the user says \"this file\", \"this code\", or "
                    "asks about something without naming a file, they mean "
                    "this one. Use read_file (if available) or rely on any "
                    "workspace context already attached.";
        systemPrompt += openLine;
    } else {
        // Privacy mode active. The model knows nothing about the editor
        // contents — and crucially, must KNOW it knows nothing so it
        // doesn't hallucinate. Plus an explicit instruction: if the user
        // asks "can you see this file?" guide them to flip the toggle.
        QString blindLine =
            "\n\nFile-access policy: the user has NOT shared their open "
            "editor file with you. You have no visibility into the file's "
            "name, path, or content. ";
        if (codingModeOn) {
            blindLine +=
                "Coding mode IS active, but the safety toggle (🔒 Share "
                "file) is OFF — the user must enable it for you to see "
                "the file. ";
        } else {
            blindLine +=
                "The user is in Chat mode or Data mode; only Coding mode "
                "can access the open file, and only when the 🔒 Share "
                "file toggle is enabled. ";
        }
        blindLine +=
            "If the user asks something like \"can you see this file?\", "
            "\"explain this code\", \"what does this function do\", or any "
            "question that implies you should be reading their editor "
            "buffer, do NOT guess or pretend. Answer plainly: \"I can't "
            "see your open file right now — your privacy toggle is on. "
            "To grant me access, switch the AI panel to Coding mode and "
            "enable the 🔒 Share file toggle (it'll turn green and read "
            "🔓 Sharing file).\" Keep that guidance short and friendly.";
        systemPrompt += blindLine;
    }

    // Build the prompt + the user-visible prompt label (just the action name
    // + the code snippet for context — no need to dump the verbose template
    // text into the user bubble). customUserText captures just the raw text
    // the user typed for the "custom" action; the workspace-gate heuristic
    // reads it without the appended code context.
    // v0.1.55 — Credential leak prevention. Anything we forward to a remote
    // backend (cloud OR a llama.cpp / Ollama server, since "local" can still
    // be reached over LAN) must be scrubbed of credential-shaped strings
    // first. The 2026-05 incident: a user pasted their OpenRouter key into
    // an editor buffer, then asked the AI Assistant a casual question. The
    // selected-text inlining at action="custom" plus the workspace-context
    // block both forwarded the key to OpenRouter, where it landed in their
    // server-side logs. CredScrub::redact() rewrites the key as
    // "[REDACTED-OPENROUTER-KEY]" before it leaves the machine.
    //
    // We track an aggregate count across all four sources (selection,
    // workspace, attached file, user-typed) so the status bar can warn the
    // user that something was scrubbed — both reassurance ("we caught it")
    // and an action prompt ("you should rotate it anyway").
    int redactionCount = 0;
    auto scrubAndCount = [&redactionCount](const QString &in) -> QString {
        int n = 0;
        QString out = CredScrub::redact(in, &n);
        redactionCount += n;
        return out;
    };
    // Quick-action context fallback. Only fires when the AI is allowed
    // to see the file in the first place. The aiCanSeeFile / codingModeOn
    // / shareFileToggle locals are defined earlier in this function (the
    // unified file-access policy).
    QString contextForActions = m_context;
    if (contextForActions.isEmpty() && aiCanSeeFile && !m_currentFileText.isEmpty()) {
        contextForActions = m_currentFileText;
    }
    const QString safeContext = scrubAndCount(contextForActions);

    QString prompt;
    QString userBubbleText;
    QString customUserText;
    if (action == "explain") {
        prompt = "Explain this code clearly and concisely:\n\n```\n" + safeContext + "\n```";
        userBubbleText = "Explain this code:\n\n" + safeContext;
    } else if (action == "bugs") {
        prompt = "Find bugs and potential issues in this code. List each bug with a fix:\n\n```\n" + safeContext + "\n```";
        userBubbleText = "Find bugs in:\n\n" + safeContext;
    } else if (action == "refactor") {
        prompt = "Refactor this code to be cleaner and more readable. Output only the refactored code:\n\n```\n" + safeContext + "\n```";
        userBubbleText = "Refactor:\n\n" + safeContext;
    } else if (action == "tests") {
        prompt = "Write unit tests for this code:\n\n```\n" + safeContext + "\n```";
        userBubbleText = "Write unit tests for:\n\n" + safeContext;
    } else if (action == "comment") {
        prompt = "Add clear comments to this code. Output the code with comments:\n\n```\n" + safeContext + "\n```";
        userBubbleText = "Add comments to:\n\n" + safeContext;
    } else if (action == "docs") {
        prompt = "Generate documentation (docstrings/JSDoc/etc) for this code. Output the code with docs:\n\n```\n" + safeContext + "\n```";
        userBubbleText = "Generate docs for:\n\n" + safeContext;
    } else if (action == "optimize") {
        prompt = "Optimize this code for performance. Explain what you changed and output the optimized code:\n\n```\n" + safeContext + "\n```";
        userBubbleText = "Optimize:\n\n" + safeContext;
    } else if (action == "translate") {
        prompt = "Translate this code to Python (if not already Python) or to JavaScript (if already Python). Output only the translated code:\n\n```\n" + safeContext + "\n```";
        userBubbleText = "Translate Python ↔ JavaScript:\n\n" + safeContext;
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
                 + format + ".\n\nBROKEN " + format + ":\n" + safeContext;
        userBubbleText = "Fix " + format + " (minimal change):\n\n" + safeContext;
    } else if (action == "custom") {
        // Scrub the user-typed text too, in case they pasted a key directly
        // into the chat input.
        const QString userText = scrubAndCount(m_customInput->toPlainText());
        customUserText = userText;
        // v0.1.38 — only inline m_context if it's a REAL user selection.
        // Pre-v0.1.38 every casual chat appended the entire open file
        // (because m_context falls back to the full file when there's no
        // selection). Now: selection → inline as fenced code; no selection
        // → just the user's text. Workspace-level questions about the
        // file still benefit from the workspace-context block via the
        // shouldAttachWorkspace gate; or use Coding Mode's read_file tool
        // for explicit file access.
        if (m_contextIsSelection && !safeContext.isEmpty()) {
            prompt = userText + "\n\n```\n" + safeContext + "\n```";
            userBubbleText = userText + "\n\n" + safeContext;
        } else {
            prompt = userText;
            userBubbleText = userText;
        }
        // v0.1.56 — `@file` mentions. Scan the user's typed text for
        // `@<path>` tokens, resolve each via AiTools::resolveSafePath,
        // read the file (cap 64 KB / file, 256 KB total), and append
        // each as `[file: <relpath>]\n<content>\n[end file]` to the
        // outgoing prompt. The visible bubble keeps the bare `@<path>`
        // tokens — only the request body gets the inlined contents.
        resolveFileMentions(prompt);
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
            // Text-extracted attachment — embed in the prompt as context.
            // Scrub credentials before forwarding (a user could attach a
            // .env file with secrets without thinking about it).
            const QString safeFileText = scrubAndCount(fileText);
            QFileInfo fi(m_pendingFilePath);
            QString header = QString("\n\n--- Attached file: %1 ---\n").arg(fi.fileName());
            prompt = prompt + header + safeFileText + "\n--- end file ---\n";
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
    // v0.1.55 — workspace attachment is gated on the privacy toggle AND on
    // the mode being Coding. Without the gate, Chat-mode questions like
    // "what's a closure?" (when there's a `.py` open) would still attach
    // the file because hasOpenFileKeyword fires on "the file" buried in
    // a model's tone. Now: blind unless Coding+Share are both green.
    if (attachWorkspace && aiCanSeeFile) {
        QVector<AiContext::OpenTabInfo> acTabs;
        acTabs.reserve(m_openTabs.size());
        for (const auto &t : m_openTabs) {
            AiContext::OpenTabInfo n;
            n.filePath = t.filePath;
            n.displayName = t.displayName;
            n.language = t.language;
            // Scrub each tab's buffer — this is the broader leak vector.
            // Without this, ANY open tab containing a credential gets
            // forwarded to the model whenever workspace awareness fires.
            n.text = scrubAndCount(t.text);
            n.isCurrent = t.isCurrent;
            acTabs.append(n);
        }
        const QString safeCurrentFileText = scrubAndCount(m_currentFileText);
        QString workspaceBlock = AiContext::buildWorkspaceContextBlockWithTree(
            m_currentFilePath, safeCurrentFileText, acTabs,
            m_workspaceRoot, m_workspaceFilePaths);
        if (!workspaceBlock.isEmpty()) {
            prompt = workspaceBlock + "\n---\n\n" + prompt;
        }
    }

    // Surface the redaction in the status bar so the user knows we caught
    // something and can decide whether to rotate the secret. We do this
    // BEFORE rendering the user bubble so the warning is the first thing
    // they see; setStatus is overwritten by streaming-stats milliseconds
    // later, but it persists long enough for the warning to register.
    if (redactionCount > 0) {
        const QString plural = (redactionCount == 1) ? "credential" : "credentials";
        setStatus(QString("⚠ Redacted %1 %2 from outgoing prompt — rotate any real secrets")
                      .arg(redactionCount).arg(plural), true);
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
    if (m_stopBtn) m_stopBtn->setEnabled(true);  // hardening: guard m_stopBtn

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
        if (currentModelSupportsTools()) {
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
    if (m_ollama) m_ollama->cancel();  // hardening: guard m_ollama (clearChat can be invoked during teardown)
    if (m_stopBtn) m_stopBtn->setEnabled(false);  // hardening: guard m_stopBtn

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

    if (m_voiceBtn) m_voiceBtn->setEnabled(true);  // hardening: guard m_voiceBtn
    updateVoiceButtonVisual(false);

    if (m_chatLayout) aiClearChat(m_chatLayout);
    m_dataWelcomeFrame = nullptr;  // wiped by aiClearChat above; clear our pointer to avoid use-after-free
    m_codingWelcomeFrame = nullptr;  // ditto for the v0.1.57 Coding welcome card
    m_currentAssistantText.clear();
    m_inAssistantBubble = false;
    m_streamingCard = nullptr;
    m_streamingBody = nullptr;
    m_lastResponse.clear();
    if (m_customInput) m_customInput->clear();  // hardening: guard m_customInput
    m_pendingFilePath.clear();
    m_pendingFileKind.clear();
    if (m_attachmentChip) {  // hardening: guard m_attachmentChip
        m_attachmentChip->setText("");
        m_attachmentChip->setVisible(false);
        m_attachmentChip->setFixedHeight(0);
    }

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

    // v0.1.61 — wrap pill + copy button in a vertical stack so the copy
    // icon sits just under the pill, right-aligned. Users have been asking
    // to copy their own prompt back (re-edit, paste elsewhere) and the
    // assistant card has had a copy button forever — now both sides match.
    auto *stack = new QWidget;
    stack->setStyleSheet("background: transparent;");
    auto *stackLay = new QVBoxLayout(stack);
    stackLay->setContentsMargins(0, 0, 0, 0);
    stackLay->setSpacing(2);

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
    stackLay->addWidget(pill, 0, Qt::AlignRight);

    auto *copyBtn = new QPushButton("⧉ copy");
    copyBtn->setCursor(Qt::PointingHandCursor);
    copyBtn->setFlat(true);
    copyBtn->setToolTip("Copy this message to the clipboard");
    copyBtn->setStyleSheet(QString(
        "QPushButton { "
        "  color: %1; font-size: 10px; font-weight: 600; "
        "  padding: 2px 8px; border: none; "
        "  background: transparent; "
        "} "
        "QPushButton:hover { color: %2; text-decoration: underline; }")
        .arg(pal.muted, pal.accent));
    QObject::connect(copyBtn, &QPushButton::clicked, copyBtn, [text]() {
        QApplication::clipboard()->setText(text);
    });
    stackLay->addWidget(copyBtn, 0, Qt::AlignRight);

    rowLay->addWidget(stack, 0, Qt::AlignRight);

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
    m_codingWelcomeFrame = nullptr;  // ditto for the v0.1.57 Coding welcome card
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
    // v0.1.57 — same idea for the Coding welcome card.
    if (m_messages.isEmpty()
        && m_codingMode && m_codingMode->isChecked()
        && !Config::instance().aiHideCodingWelcome) {
        renderCodingWelcomeCard();
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
    // hardening: never push a totally empty error — the user must see SOMETHING.
    message.text = text.isEmpty() ? QStringLiteral("AI request failed.") : text;
    m_messages.push_back(message);
    scheduleChatSave();
    if (m_chatLayout) renderTranscript();  // hardening: skip render when chat surface not built yet
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
// ─────────────────────────────────────────────────────────────────────────────
// renderGroupedModelTree — v0.1.55 live cloud-catalog dropdown for
// OpenRouter / OpenAI. Takes the raw `/v1/models` array, groups by provider
// prefix (id format = "<provider>/<model>"), sorts groups by a curated
// importance order, then writes the dropdown as section headers + indented
// model entries with a "<provider>/<model>  ·  $in/$out" price suffix.
// ─────────────────────────────────────────────────────────────────────────────
bool AIPanel::currentModelSupportsTools() const {
    if (!m_ollama) return false;
    const QString model = m_ollama->model();
    if (model.isEmpty()) return false;

    // For non-Ollama backends, the server decides — always send tools.
    if (m_ollama->backend() != OllamaClient::Ollama) return true;

    // For Ollama: prefer the /api/show capabilities cache. "tools" in the
    // capabilities array is Ollama's own ground-truth answer to the
    // function-calling question. Falls back to the substring allowlist
    // when the cache hasn't been populated yet (first turn after model
    // selection, or /api/show failed).
    auto it = m_ollamaModelCaps.constFind(model);
    if (it != m_ollamaModelCaps.constEnd() && !it.value().isEmpty()) {
        return it.value().contains(QStringLiteral("tools"), Qt::CaseInsensitive);
    }
    return AiTools::modelLikelySupportsTools(model);
}

// ─────────────────────────────────────────────────────────────────────────────
// modelSupportsVision — does the currently-selected model accept image inputs?
//
// Two-path resolver (mirrors currentModelSupportsTools):
//   1. Ollama backend: trust the /api/show capabilities cache. If it exists
//      and is non-empty, "vision" in the capabilities array is the answer.
//      If the cache is EMPTY for this model, fall through to the prefix
//      allowlist below — but the allowlist only matches known vision tags
//      (qwen2.5vl, llava, gemma3, …) so an unknown / freshly-installed
//      Ollama model returns false. That's the safe default: it's better to
//      tell the user "switch model" than to silently strip the image and
//      let the model hallucinate a description of nothing.
//   2. Cloud / llama.cpp / OpenAI-compat: read the dropdown text, lowercase
//      it, strip any leading "<provider>/" segment (OpenRouter format), then
//      check a hardcoded May-2026 allowlist of multimodal model families.
//
// Returns false if there's no model selected or no client.
// ─────────────────────────────────────────────────────────────────────────────
bool AIPanel::modelSupportsVision() const {
    if (!m_ollama) return false;

    // Prefer the model the dropdown currently shows — that's what the user
    // sees and is the source of truth for "what model are we about to use."
    // Falls back to whatever m_ollama has cached (covers the brief window
    // before the dropdown's currentTextChanged fires after a refresh).
    QString rawName;
    if (m_modelCombo) rawName = m_modelCombo->currentText();
    if (rawName.isEmpty()) rawName = m_ollama->model();
    if (rawName.isEmpty()) return false;

    QString name = rawName.trimmed().toLower();
    if (name.isEmpty()) return false;

    // Path 1: Ollama — capabilities cache is ground truth when populated.
    if (m_ollama->backend() == OllamaClient::Ollama) {
        auto it = m_ollamaModelCaps.constFind(rawName);
        if (it == m_ollamaModelCaps.constEnd()) {
            // Try the lowercased key as well — m_ollamaModelCaps is keyed by
            // exact model id; combobox text and ollama-list text agree, so
            // this is mostly defensive.
            it = m_ollamaModelCaps.constFind(name);
        }
        if (it != m_ollamaModelCaps.constEnd() && !it.value().isEmpty()) {
            return it.value().contains(QStringLiteral("vision"), Qt::CaseInsensitive);
        }
        // Cache empty: fall through to the allowlist. We do NOT default-true
        // here — silent-drop is the worst trap on Ollama.
    }

    // Path 2: prefix allowlist (May 2026). Strip any "<provider>/" prefix
    // first so "anthropic/claude-sonnet-4-6" matches the same way as the
    // bare "claude-sonnet-4-6" name. Then check each family with a simple
    // startsWith / contains rule. Order matters only insofar as we want to
    // match the more-specific tags first — startsWith checks are mutually
    // exclusive on these prefixes, so the order is mostly cosmetic.
    QString stem = name;
    int slash = stem.indexOf('/');
    if (slash >= 0) stem = stem.mid(slash + 1);

    // Anthropic Claude — anything 3.5 and up is multimodal.
    static const QStringList kClaudePrefixes = {
        QStringLiteral("claude-3.5"),
        QStringLiteral("claude-3.7"),
        QStringLiteral("claude-4"),
        QStringLiteral("claude-opus-4"),
        QStringLiteral("claude-sonnet-4"),
        QStringLiteral("claude-haiku-4"),
    };
    // OpenAI — 4o family + 4.1 / 4.5 / 5 / o4 / chatgpt-4o.
    static const QStringList kOpenAiPrefixes = {
        QStringLiteral("gpt-4o"),
        QStringLiteral("gpt-4.1"),
        QStringLiteral("gpt-4.5"),
        QStringLiteral("gpt-5"),
        QStringLiteral("o4"),
        QStringLiteral("chatgpt-4o"),
    };
    // Google Gemini — 1.5 / 2.x / 3.x are all multimodal.
    static const QStringList kGeminiPrefixes = {
        QStringLiteral("gemini-1.5"),
        QStringLiteral("gemini-2"),
        QStringLiteral("gemini-3"),
    };
    // Ollama / open-source vision tags. Used both as a fallback when the
    // /api/show cache is empty AND when the user is on a non-Ollama
    // backend that happens to expose one of these models (e.g. a llama.cpp
    // server hosting a local vision model).
    static const QStringList kOllamaVisionPrefixes = {
        QStringLiteral("qwen2.5vl"),
        QStringLiteral("qwen2.5-vl"),
        QStringLiteral("llava"),
        QStringLiteral("gemma3"),
        QStringLiteral("llama3.2-vision"),
        QStringLiteral("minicpm-v"),
        QStringLiteral("moondream"),
        QStringLiteral("mistral-small3.1"),
    };

    auto matchesAny = [&stem](const QStringList &prefixes) {
        for (const QString &p : prefixes) {
            if (stem.startsWith(p)) return true;
        }
        return false;
    };

    if (matchesAny(kClaudePrefixes))        return true;
    if (matchesAny(kOpenAiPrefixes))        return true;
    if (matchesAny(kGeminiPrefixes))        return true;
    if (matchesAny(kOllamaVisionPrefixes))  return true;

    return false;
}

// ─────────────────────────────────────────────────────────────────────────────
// acceptAttachment — shared vision-capability gate.
//
// Both attachFile() (file picker) and dropEvent() (drag-drop) funnel through
// here so the refusal logic lives in exactly one place. Returns true to mean
// "go ahead — set m_pendingFilePath / m_pendingFileKind and show the chip."
// Returns false (after appending a helpful error bubble) when the kind is
// "image" and modelSupportsVision() is false. The bubble cites concrete
// model tags so the user's next action is obvious — switch the dropdown,
// or `ollama pull qwen2.5vl:7b`, etc.
// ─────────────────────────────────────────────────────────────────────────────
bool AIPanel::acceptAttachment(const QString &path, const QString &kind) {
    if (kind != QLatin1String("image")) {
        // Non-image kinds (pdf / docx / pptx / xlsx / text / code) flow
        // through extractFileContent at send time and don't require a
        // multimodal model. Always accept.
        return true;
    }

    if (modelSupportsVision()) return true;

    // Action-oriented refusal: name the file, name the current model, then
    // give the user a one-step path forward for both local and cloud.
    QString currentName;
    if (m_modelCombo) currentName = m_modelCombo->currentText().trimmed();
    if (currentName.isEmpty()) currentName = QStringLiteral("the selected model");

    QFileInfo fi(path);
    const QString fname = fi.fileName().isEmpty() ? path : fi.fileName();

    const QString message = tr(
        "🖼 Can't attach <b>%1</b> — <b>%2</b> doesn't support image input.<br>"
        "<br>"
        "Switch to a vision-capable model, then drop the image again:<br>"
        "&nbsp;&nbsp;• <b>Local (Ollama)</b>: <code>ollama pull qwen2.5vl:7b</code> "
        "or <code>ollama pull gemma3:4b</code><br>"
        "&nbsp;&nbsp;• <b>Cloud</b>: pick <code>claude-sonnet-4-6</code>, "
        "<code>gpt-5</code>, or <code>gemini-2.5-flash</code> from the model dropdown."
    ).arg(fname.toHtmlEscaped(), currentName.toHtmlEscaped());

    appendErrorBubble(message);

    // Defensive — make sure no stale pending attachment lingers from a
    // prior drop / pick. attachFile() and dropEvent() both clear these
    // before they call acceptAttachment, but belt-and-braces.
    m_pendingFilePath.clear();
    m_pendingFileKind.clear();
    if (m_attachmentChip) {
        m_attachmentChip->setText(QString());
        m_attachmentChip->setVisible(false);
        m_attachmentChip->setFixedHeight(0);
    }
    return false;
}

// ─────────────────────────────────────────────────────────────────────────────
// dragEnterEvent / dropEvent — accept file drops onto the AI panel.
//
// Drop semantics mirror attachFile(): the first dropped local file is staged
// as m_pendingFilePath / m_pendingFileKind, the attachment chip appears, and
// extractFileContent runs at send time. Image drops are gated by
// modelSupportsVision() via acceptAttachment(); other kinds (PDF / DOCX /
// PPTX) fall back to text-extraction unchanged. Drops with no local file
// are silently ignored — same behaviour as MainWindow's drop handler.
// ─────────────────────────────────────────────────────────────────────────────
void AIPanel::dragEnterEvent(QDragEnterEvent *event) {
    if (event->mimeData()->hasUrls()) event->acceptProposedAction();
}

void AIPanel::dropEvent(QDropEvent *event) {
    const QList<QUrl> urls = event->mimeData()->urls();
    if (urls.isEmpty()) return;

    // Only handle the first file — the AI panel attaches one file at a time.
    // Future improvement: stack multiple chips. For now, drop[0] wins.
    QString path;
    for (const QUrl &u : urls) {
        if (u.isLocalFile()) { path = u.toLocalFile(); break; }
    }
    if (path.isEmpty()) return;  // remote URLs / non-file drops aren't supported

    // Mirror attachFile()'s extension → kind detection so the chip / icon /
    // send-time extraction behaves identically regardless of how the file
    // landed in m_pendingFilePath.
    QFileInfo fi(path);
    const QString ext = fi.suffix().toLower();
    QString kind;
    static const QStringList imageExts = {"png", "jpg", "jpeg", "webp", "gif", "bmp"};
    if (imageExts.contains(ext))                       kind = QStringLiteral("image");
    else if (ext == QLatin1String("pdf"))              kind = QStringLiteral("pdf");
    else if (ext == QLatin1String("docx") || ext == QLatin1String("doc"))   kind = QStringLiteral("docx");
    else if (ext == QLatin1String("pptx") || ext == QLatin1String("ppt"))   kind = QStringLiteral("pptx");
    else if (ext == QLatin1String("xlsx") || ext == QLatin1String("xls"))   kind = QStringLiteral("xlsx");
    else                                               kind = QStringLiteral("text");

    // Vision-capability gate. If acceptAttachment refuses, it has already
    // appended a helpful error bubble — bail out without touching pending
    // state and accept the drop event so the desktop's drop animation
    // settles correctly.
    if (!acceptAttachment(path, kind)) {
        event->acceptProposedAction();
        return;
    }

    // Happy path — same as attachFile() body from m_pendingFilePath onward.
    m_pendingFilePath = path;
    m_pendingFileKind = kind;

    const QString name   = fi.fileName();
    const qint64  sizeKb = fi.size() / 1024;
    QString icon = QStringLiteral("📄");
    if      (kind == QLatin1String("image")) icon = QStringLiteral("🖼");
    else if (kind == QLatin1String("pdf"))   icon = QStringLiteral("📕");
    else if (kind == QLatin1String("docx"))  icon = QStringLiteral("📘");
    else if (kind == QLatin1String("pptx"))  icon = QStringLiteral("📙");
    else if (kind == QLatin1String("xlsx"))  icon = QStringLiteral("📗");

    if (m_attachmentChip) {
        m_attachmentChip->setText(QString("%1 %2 (%3 KB) — will be included as context. Click chip to remove.")
                                  .arg(icon).arg(name).arg(sizeKb));
        m_attachmentChip->setVisible(true);
        m_attachmentChip->setFixedHeight(24);
        m_attachmentChip->installEventFilter(this);
    }

    setStatus(QString("✓ Attached (drop): %1 (%2)").arg(name).arg(kind), false);
    event->acceptProposedAction();
}

// v0.1.56 — mode-aware visual cue on each dropdown row. Same model name
// rule as currentModelSupportsTools() / modelCapableOfDataAnalysis(): we
// don't second-guess the user, we just colour and tooltip the rows so
// "this won't really work for what you're doing" is obvious before they
// pick. Capable rows render in the panel's normal foreground; not-capable
// rows go amber (#FF9F43) and grow a tooltip that says why. Section
// headers / disabled placeholders are skipped (not selectable → no point
// decorating them). Cleared by passing through chat mode (any model OK).
void AIPanel::decorateModelsByMode() {
    if (!m_modelCombo) return;
    auto *m = qobject_cast<QStandardItemModel*>(m_modelCombo->model());
    if (!m) return;

    const bool dataMode   = m_dataMode   && m_dataMode->isChecked();
    const bool codingMode = m_codingMode && m_codingMode->isChecked();
    const bool chatMode   = !dataMode && !codingMode;

    const QColor amber("#FF9F43");
    const QBrush defaultBrush;  // default = palette text colour

    for (int i = 0; i < m_modelCombo->count(); ++i) {
        QStandardItem *item = m->item(i);
        if (!item) continue;
        // Separators and the disabled "— pick one to download —" header
        // are not selectable — leave them alone.
        if (!(item->flags() & Qt::ItemIsSelectable)) continue;

        // Resolve the underlying model name. UserRole holds it for the
        // llama.cpp catalog rows we set ourselves; for live /v1/models
        // and Ollama lists the visible label IS the model name.
        QString name = item->data(Qt::UserRole).toString();
        if (name.isEmpty()) name = item->text();
        // Strip any "↓ " download prefix we added for not-installed
        // llama.cpp catalog items, so capability lookup hits the real id.
        if (name.startsWith(QStringLiteral("↓ "))) name.remove(0, 2);

        if (chatMode) {
            // Wipe any earlier mode's decoration.
            item->setForeground(defaultBrush);
            item->setToolTip(QString());
            continue;
        }

        bool capable = false;
        QString reason;
        if (dataMode) {
            capable = AiTools::modelCapableOfDataAnalysis(name);
            reason = capable
                ? tr("✓ Capable for Data mode (multi-table SQL + chart specs)")
                : tr("⚠ Too small for reliable Data mode. "
                     "Need a frontier cloud model OR a local 7B+ from a "
                     "strong family (qwen2.5-coder, deepseek-coder, "
                     "llama3.x, mistral-large).");
        } else { // codingMode
            // Same logic as currentModelSupportsTools() but for an
            // arbitrary model name, not the active one.
            if (m_ollama && m_ollama->backend() != OllamaClient::Ollama) {
                // Cloud / llama.cpp: server decides, treat as capable.
                capable = true;
            } else {
                auto it = m_ollamaModelCaps.constFind(name);
                if (it != m_ollamaModelCaps.constEnd() && !it.value().isEmpty()) {
                    capable = it.value().contains(QStringLiteral("tools"), Qt::CaseInsensitive);
                } else {
                    capable = AiTools::modelLikelySupportsTools(name);
                }
            }
            reason = capable
                ? tr("✓ Supports function-calling tools — Coding mode will work")
                : tr("⚠ No tool support known for this model. Coding mode "
                     "needs function-calling. Try qwen2.5-coder, llama3.1+, "
                     "qwen3, hermes3, command-r, or any frontier cloud model.");
        }

        item->setForeground(capable ? defaultBrush : QBrush(amber));
        item->setToolTip(reason);
    }
}

void AIPanel::renderGroupedModelTree(const QJsonArray &dataArr) {
    if (!m_modelCombo) return;
    QSignalBlocker blockSignals(m_modelCombo);
    const QString prev = m_modelCombo->currentText();
    m_modelCombo->clear();

    // Group by provider prefix (the part before "/" in the id).
    QMap<QString, QVector<QJsonObject>> grouped;
    for (const QJsonValue &v : dataArr) {
        const QJsonObject obj = v.toObject();
        const QString id = obj.value("id").toString();
        if (id.isEmpty()) continue;
        const QString prov = id.contains('/') ? id.section('/', 0, 0) : "other";
        grouped[prov].append(obj);
    }

    // Curated provider order — flagships first, then mid-tier, then long
    // tail alphabetised. Anything not listed falls into "Other".
    static const QStringList CURATED_ORDER = {
        "anthropic",      // Claude
        "openai",         // GPT
        "google",         // Gemini
        "deepseek",       // R1, V3
        "meta-llama",     // Llama
        "mistralai",      // Mistral
        "qwen",           // Qwen / Qwen-Coder
        "x-ai",           // Grok
        "minimax",        // MiniMax
        "moonshotai",     // Kimi
        "z-ai",           // GLM
        "nvidia",         // Nemotron
        "amazon",         // Nova
        "cohere",         // Command R
        "perplexity",     // Sonar
        "nousresearch",   // Hermes
        "01-ai",          // Yi
        "baidu",          // ERNIE
    };

    auto formatPrice = [](const QJsonObject &obj) -> QString {
        const QJsonObject p = obj.value("pricing").toObject();
        const double inP  = p.value("prompt").toString().toDouble();
        const double outP = p.value("completion").toString().toDouble();
        if (inP == 0.0 && outP == 0.0) return QStringLiteral("free");
        // OpenRouter prices are per-token; convert to per-M-tokens.
        const double inM = inP * 1'000'000.0;
        const double outM = outP * 1'000'000.0;
        return QString("$%1/$%2 per M")
            .arg(inM, 0, 'g', 3).arg(outM, 0, 'g', 3);
    };

    auto pretty = [](const QString &id) -> QString {
        // Strip provider prefix; keep tag suffix (":free" / ":beta" / etc).
        return id.contains('/') ? id.section('/', 1) : id;
    };

    auto *qsim = qobject_cast<QStandardItemModel *>(m_modelCombo->model());
    auto addHeader = [this, qsim](const QString &text) {
        if (m_modelCombo->count() > 0) m_modelCombo->insertSeparator(m_modelCombo->count());
        m_modelCombo->addItem(text);
        if (qsim) qsim->item(m_modelCombo->count() - 1)->setEnabled(false);
    };

    // v0.1.55 — provider synonyms / aliases users actually type. Users
    // search "claude" or "anthropic" interchangeably, "xai" without a dash
    // for x-ai/grok, "gemini" for google. Each item gets these merged
    // into its Qt::UserRole+2 search blob so the QCompleter can match on
    // them.
    static const QMap<QString, QString> PROVIDER_SYNONYMS = {
        {"anthropic",    "claude haiku sonnet opus"},
        {"openai",       "gpt o1 o3 chatgpt"},
        {"google",       "gemini bard palm"},
        {"deepseek",     "deepseek r1 v3"},
        {"meta-llama",   "llama meta facebook"},
        {"mistralai",    "mistral codestral"},
        {"qwen",         "alibaba"},
        {"x-ai",         "xai grok elon"},
        {"minimax",      "abab"},
        {"moonshotai",   "kimi moonshot"},
        {"z-ai",         "glm zai zhipu"},
        {"nvidia",       "nemotron"},
        {"amazon",       "aws nova bedrock"},
        {"cohere",       "command"},
        {"perplexity",   "sonar pplx"},
        {"nousresearch", "hermes nous"},
        {"01-ai",        "yi zeroone"},
        {"baidu",        "ernie"},
    };

    auto addProviderGroup = [&](const QString &provKey,
                                const QString &headerText) {
        if (!grouped.contains(provKey)) return;
        const QVector<QJsonObject> models = grouped.take(provKey);
        addHeader(QString("── %1  (%2 models) ──")
                  .arg(headerText).arg(models.size()));
        const QString synonyms = PROVIDER_SYNONYMS.value(provKey);
        for (const QJsonObject &m : models) {
            const QString id = m.value("id").toString();
            const QString name = m.value("name").toString();
            const QString priceStr = formatPrice(m);
            const QString label = QString("    %1  ·  %2")
                                    .arg(pretty(id))
                                    .arg(priceStr);
            m_modelCombo->addItem(label);
            const int idx = m_modelCombo->count() - 1;
            m_modelCombo->setItemData(idx, id, Qt::UserRole);
            // v0.1.55 — search blob in Qt::EditRole. QCompleter reads
            // EditRole when matching the typed text, while the popup
            // renders DisplayRole. So typing "xai" / "claude" / "gpt"
            // matches the blob even though the visible label is just
            // "claude-opus-4-5 · $15/$75 per M". The blob fuses:
            //   * full id (so "anthropic/claude-…" matches)
            //   * model's friendly name ("GPT-5 Codex")
            //   * curated header ("Anthropic — Claude")
            //   * raw provider key ("x-ai")
            //   * synonyms ("xai", "grok", "elon" for x-ai;
            //     "claude haiku sonnet opus" for anthropic; etc.)
            //   * the visible label itself
            const QString blob = QStringList{
                label, id, name, headerText, provKey, synonyms
            }.join(' ').toLower();
            m_modelCombo->setItemData(idx, blob, Qt::EditRole);
            // Keep UserRole+2 around for any tooling that wants the raw
            // blob without round-tripping through EditRole.
            m_modelCombo->setItemData(idx, blob, Qt::UserRole + 2);
            // Tooltip = full id + name + description (truncated).
            const QString desc = m.value("description").toString().left(280);
            const qint64 ctx   = m.value("context_length").toVariant().toLongLong();
            m_modelCombo->setItemData(idx,
                QString("%1\n\n%2\n\nContext: %3 tokens · Pricing: %4")
                    .arg(name.isEmpty() ? id : name, desc)
                    .arg(ctx).arg(priceStr),
                Qt::ToolTipRole);
        }
    };

    static const QMap<QString, QString> NICE_NAMES = {
        {"anthropic",    "Anthropic — Claude"},
        {"openai",       "OpenAI — GPT / o1 / o3"},
        {"google",       "Google — Gemini"},
        {"deepseek",     "DeepSeek"},
        {"meta-llama",   "Meta — Llama"},
        {"mistralai",    "Mistral AI"},
        {"qwen",         "Alibaba — Qwen"},
        {"x-ai",         "xAI — Grok"},
        {"minimax",      "MiniMax"},
        {"moonshotai",   "Moonshot AI — Kimi"},
        {"z-ai",         "Z.ai — GLM"},
        {"nvidia",       "NVIDIA"},
        {"amazon",       "Amazon — Nova"},
        {"cohere",       "Cohere"},
        {"perplexity",   "Perplexity"},
        {"nousresearch", "NousResearch — Hermes"},
        {"01-ai",        "01.AI — Yi"},
        {"baidu",        "Baidu — ERNIE"},
    };

    for (const QString &provKey : CURATED_ORDER) {
        if (NICE_NAMES.contains(provKey))
            addProviderGroup(provKey, NICE_NAMES.value(provKey));
        else
            addProviderGroup(provKey, provKey);
    }
    // Any remaining providers — long tail, alphabetised.
    QStringList rest = grouped.keys();
    rest.sort(Qt::CaseInsensitive);
    for (const QString &k : rest) {
        addProviderGroup(k, k.toUpper().left(1) + k.mid(1));
    }

    m_modelCombo->setEnabled(true);
    int restoreIdx = m_modelCombo->findText(prev);
    if (restoreIdx >= 0) m_modelCombo->setCurrentIndex(restoreIdx);
    else if (m_modelCombo->count() > 1) m_modelCombo->setCurrentIndex(1);

    const QString backendName =
        Config::instance().aiBaseUrl.contains("openrouter", Qt::CaseInsensitive)
            ? "OpenRouter" : "OpenAI";
    setStatus(QString("%1 — %2 models from live API · grouped by provider · "
                      "type a custom id to override")
                  .arg(backendName).arg(dataArr.size()),
              false);
    decorateModelsByMode();
}

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
    connLabel->setWordWrap(true);
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
    modelLabel->setWordWrap(true);
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

    // v0.1.55 — surface the loaded analyst-context files. Counts every file
    // under .notepatra/data-analyst.md + .notepatra/data-analyst/* with one
    // of the supported extensions. Lets users SEE that their data dictionary
    // / business rules / KPIs are actually being attached to the prompt.
    {
        QStringList loadedFiles;
        if (!m_workspaceRoot.isEmpty()) {
            const QDir root(m_workspaceRoot);
            QFileInfo legacy(root.filePath(".notepatra/data-analyst.md"));
            if (legacy.exists() && legacy.isFile())
                loadedFiles << "data-analyst.md";
            const QString subdir = root.filePath(".notepatra/data-analyst");
            QDir sd(subdir);
            if (sd.exists()) {
                const QStringList exts = {"*.md", "*.sql", "*.yaml", "*.yml", "*.json", "*.txt"};
                for (const QString &fn : sd.entryList(exts, QDir::Files, QDir::Name))
                    loadedFiles << fn;
            }
        }
        if (loadedFiles.isEmpty()) {
            auto *ctxHint = new QLabel(QStringLiteral(
                "📚  <i>No analyst-context files yet. Drop a "
                "<code>.notepatra/data-analyst/business-rules.md</code> or "
                "<code>data-dictionary.md</code> into your workspace and the "
                "AI will use them as ground truth.</i>"));
            ctxHint->setTextFormat(Qt::RichText);
            ctxHint->setWordWrap(true);
            ctxHint->setStyleSheet(QString("color: %1; font-size: 11px;").arg(pal.muted));
            status->addWidget(ctxHint);
        } else {
            const QString joined = loadedFiles.join(" · ");
            auto *ctxLine = new QLabel(QString::fromUtf8(
                "📚  <b>%1 context file%2 loaded</b> &nbsp;·&nbsp; <span style='color:%3;'>%4</span>")
                .arg(loadedFiles.size())
                .arg(loadedFiles.size() == 1 ? "" : "s")
                .arg(pal.muted, joined.left(160) + (joined.size() > 160 ? "…" : "")));
            ctxLine->setTextFormat(Qt::RichText);
            ctxLine->setWordWrap(true);
            ctxLine->setStyleSheet(QString("color: %1; font-size: 11px;").arg(pal.muted));
            status->addWidget(ctxLine);
        }
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

// v0.1.57 — Coding-mode welcome card. Mirrors the Data welcome card pattern
// (renderDataWelcomeCard above) so users entering Coding mode for the first
// time see a quick tour of the new agentic features (Composer tab, @file
// mention, Ctrl+I inline edit, agentic git tools). Uses the same teal accent
// (#4EC9B0) as the Coding mode header. The git status pill is computed via
// QProcess with a 1s timeout so the card never blocks the UI; if git is
// missing, slow, or the workspace isn't a repo, the pill degrades gracefully.
void AIPanel::renderCodingWelcomeCard() {
    if (!m_chatLayout) return;
    if (m_codingWelcomeFrame) return;  // already shown

    const AiPalette pal = aiPalette();
    const QString modelName = m_modelCombo ? m_modelCombo->currentText() : QString();
    const bool toolsCapable = currentModelSupportsTools();

    auto *card = new QFrame;
    card->setFrameShape(QFrame::StyledPanel);
    card->setStyleSheet(QString(
        "QFrame { background: %1; border: 1px solid #4EC9B0; border-radius: 10px; }"
        "QLabel { background: transparent; color: %2; }")
        .arg(pal.assistBg, pal.chatFg));

    auto *cardLay = new QVBoxLayout(card);
    cardLay->setContentsMargins(16, 14, 16, 14);
    cardLay->setSpacing(10);

    // Title row + Hide button
    auto *titleRow = new QHBoxLayout;
    titleRow->setSpacing(8);
    auto *title = new QLabel(QStringLiteral("💻  Coding Mode"));
    {
        QFont f = title->font();
        f.setPointSize(f.pointSize() + 2);
        f.setBold(true);
        title->setFont(f);
        title->setStyleSheet("color: #4EC9B0;");
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
        Config::instance().aiHideCodingWelcome = true;
        Config::instance().save();
        removeCodingWelcomeCard();
    });
    titleRow->addWidget(hideBtn);
    cardLay->addLayout(titleRow);

    // One-sentence blurb
    auto *blurb = new QLabel(QStringLiteral(
        "Ask questions, propose multi-file edits, run git, all without "
        "leaving your editor."));
    blurb->setWordWrap(true);
    blurb->setStyleSheet(QString("color: %1; font-size: 12px;").arg(pal.chatFg));
    cardLay->addWidget(blurb);

    // Quick start row — three boxed example chips. The first fills the
    // input + focuses; the other two are informational (Ctrl+I and @file
    // are editor-side surfaces, not chat prompts).
    {
        auto *qsLabel = new QLabel(QStringLiteral("Quick start"));
        QFont qsF = qsLabel->font();
        qsF.setBold(true);
        qsF.setPointSize(qsF.pointSize() - 1);
        qsLabel->setFont(qsF);
        qsLabel->setStyleSheet(QString("color: %1;").arg(pal.muted));
        cardLay->addWidget(qsLabel);
    }
    auto *chips = new QVBoxLayout;
    chips->setSpacing(6);
    struct QuickStart {
        const char *label;
        const char *prompt;  // empty = informational (no autofill)
    };
    static const QuickStart QS[] = {
        { "Show what changed since yesterday",
          "Show what changed since yesterday" },
        { "Refactor selection — Ctrl+I in editor",
          "" },
        { "Mention a file with @path/to/file.py",
          "" },
    };
    for (const auto &qs : QS) {
        auto *chip = new QPushButton(QString::fromUtf8("›  ") + QString::fromUtf8(qs.label));
        chip->setCursor(Qt::PointingHandCursor);
        chip->setStyleSheet(QString(
            "QPushButton { background: %1; color: %2; border: 1px solid %3; "
            "border-radius: 6px; padding: 6px 10px; text-align: left; font-size: 12px; }"
            "QPushButton:hover { background: %4; border-color: #4EC9B0; color: #4EC9B0; }")
            .arg(pal.codeBg, pal.chatFg, pal.assistBorder, pal.btnHover));
        const QString prompt = QString::fromUtf8(qs.prompt);
        connect(chip, &QPushButton::clicked, this, [this, prompt]() {
            if (prompt.isEmpty() || !m_customInput) return;
            m_customInput->setPlainText(prompt);
            m_customInput->setFocus();
            QTextCursor c = m_customInput->textCursor();
            c.movePosition(QTextCursor::End);
            m_customInput->setTextCursor(c);
        });
        chips->addWidget(chip);
    }
    cardLay->addLayout(chips);

    // Status block — model tool-call capability + git status pill.
    auto *status = new QVBoxLayout;
    status->setSpacing(4);

    auto *modelLabel = new QLabel(toolsCapable
        ? QString::fromUtf8("🤖  <b>%1</b> &nbsp;·&nbsp; <span style='color:#3FB950'>tool calls supported ✓</span>")
              .arg(modelName.isEmpty() ? QStringLiteral("(no model selected)") : modelName)
        : QString::fromUtf8("🤖  <b>%1</b> &nbsp;·&nbsp; <span style='color:#FF9F43'>tool calls unverified ⚠</span>")
              .arg(modelName.isEmpty() ? QStringLiteral("(no model selected)") : modelName));
    modelLabel->setTextFormat(Qt::RichText);
    modelLabel->setStyleSheet(QString("color: %1; font-size: 11px;").arg(pal.muted));
    status->addWidget(modelLabel);

    if (!toolsCapable) {
        auto *toolsHint = new QLabel(QStringLiteral(
            "&nbsp;&nbsp;&nbsp;<i>For agentic git / file edits, use a tool-"
            "capable model: Claude Sonnet · GPT-4o / GPT-5 · Gemini 2.5 Pro · "
            "qwen2.5-coder · DeepSeek-Coder.</i>"));
        toolsHint->setTextFormat(Qt::RichText);
        toolsHint->setWordWrap(true);
        toolsHint->setStyleSheet(QString("color: %1; font-size: 11px;").arg(pal.muted));
        status->addWidget(toolsHint);
    }

    // Git status pill — uses QProcess with 1s timeout so render never blocks.
    // If the workspace is missing, not a git repo, or git times out, we fall
    // back to a friendly "not a repo / couldn't read state" line.
    auto *gitRow = new QHBoxLayout;
    gitRow->setSpacing(6);
    auto *gitLabel = new QLabel;
    gitLabel->setTextFormat(Qt::RichText);
    gitLabel->setStyleSheet(QString("color: %1; font-size: 11px;").arg(pal.muted));
    gitLabel->setWordWrap(true);

    QString branch;
    int modifiedCount = 0;
    int aheadCount = 0;
    int behindCount = 0;
    bool isRepo = false;
    bool gitOk = true;

    if (!m_workspaceRoot.isEmpty()) {
        // Check if it's a git repo via rev-parse.
        QProcess pBranch;
        pBranch.start("git", { "-C", m_workspaceRoot, "rev-parse",
                               "--abbrev-ref", "HEAD" });
        if (pBranch.waitForStarted(500) && pBranch.waitForFinished(1000)
            && pBranch.exitCode() == 0) {
            branch = QString::fromUtf8(pBranch.readAllStandardOutput()).trimmed();
            isRepo = !branch.isEmpty();
        } else if (pBranch.state() != QProcess::NotRunning) {
            pBranch.kill();
            pBranch.waitForFinished(200);
            gitOk = false;
        }

        if (isRepo) {
            QProcess pStatus;
            pStatus.start("git", { "-C", m_workspaceRoot, "status",
                                   "--porcelain=v2", "-b" });
            if (pStatus.waitForStarted(500) && pStatus.waitForFinished(1000)
                && pStatus.exitCode() == 0) {
                const QString out = QString::fromUtf8(pStatus.readAllStandardOutput());
                const QStringList lines = out.split('\n', Qt::SkipEmptyParts);
                for (const QString &ln : lines) {
                    if (ln.startsWith("# branch.ab ")) {
                        // Format: "# branch.ab +N -M"
                        const QStringList parts = ln.split(' ', Qt::SkipEmptyParts);
                        for (const QString &p : parts) {
                            if (p.startsWith('+')) aheadCount = p.mid(1).toInt();
                            else if (p.startsWith('-')) behindCount = p.mid(1).toInt();
                        }
                    } else if (!ln.startsWith('#')) {
                        // Any non-header line is a tracked-change entry.
                        ++modifiedCount;
                    }
                }
            } else if (pStatus.state() != QProcess::NotRunning) {
                pStatus.kill();
                pStatus.waitForFinished(200);
                gitOk = false;
            }
        }
    }

    if (!gitOk) {
        gitLabel->setText(QStringLiteral("🌿  <i>(couldn't read git state)</i>"));
        gitRow->addWidget(gitLabel, 1);
    } else if (isRepo) {
        QStringList parts;
        parts << QString("<b>%1</b>").arg(branch.toHtmlEscaped());
        if (modifiedCount > 0)
            parts << QString("%1 modified").arg(modifiedCount);
        if (aheadCount > 0)
            parts << QString("%1 ahead").arg(aheadCount);
        if (behindCount > 0)
            parts << QString("%1 behind").arg(behindCount);
        if (modifiedCount == 0 && aheadCount == 0 && behindCount == 0)
            parts << "clean";
        gitLabel->setText(QString::fromUtf8("🌿  ") + parts.join(" · "));
        gitRow->addWidget(gitLabel, 1);
    } else if (m_workspaceRoot.isEmpty()) {
        gitLabel->setText(QStringLiteral("🌿  <i>no workspace folder open</i>"));
        gitRow->addWidget(gitLabel, 1);
    } else {
        gitLabel->setText(QStringLiteral(
            "🌿  <i>not a git repo (init with: <code>git init</code>)</i>"));
        gitRow->addWidget(gitLabel, 1);

        auto *initBtn = new QPushButton(QStringLiteral("Initialize repo"));
        initBtn->setCursor(Qt::PointingHandCursor);
        initBtn->setStyleSheet(QString(
            "QPushButton { background: #4EC9B0; color: white; border: none; "
            "border-radius: 4px; padding: 4px 10px; font-size: 11px; font-weight: 600; }"
            "QPushButton:hover { background: #6FD9C2; }"));
        connect(initBtn, &QPushButton::clicked, this, [this]() {
            if (m_workspaceRoot.isEmpty()) return;
            const auto reply = QMessageBox::question(this,
                tr("Initialize git repository?"),
                tr("Run <code>git init</code> in:<br><b>%1</b><br><br>"
                   "This creates a <code>.git/</code> folder so the AI can "
                   "use git tools (log, diff, blame, status) on this "
                   "workspace.")
                    .arg(m_workspaceRoot.toHtmlEscaped()),
                QMessageBox::Ok | QMessageBox::Cancel,
                QMessageBox::Cancel);
            if (reply != QMessageBox::Ok) return;
            QProcess p;
            p.start("git", { "-C", m_workspaceRoot, "init" });
            if (!p.waitForStarted(1500)) {
                setStatus(tr("git not installed (apt install git)"), true);
                return;
            }
            p.waitForFinished(5000);
            if (p.exitCode() == 0) {
                setStatus(tr("✓ Initialized git repo in %1").arg(m_workspaceRoot), false);
            } else {
                const QString err = QString::fromUtf8(p.readAllStandardError()).trimmed();
                setStatus(tr("git init failed: %1").arg(err.isEmpty() ? "unknown error" : err), true);
            }
            // Refresh the card so the pill updates to the new branch state.
            if (m_codingWelcomeFrame) {
                removeCodingWelcomeCard();
                renderCodingWelcomeCard();
            }
        });
        gitRow->addWidget(initBtn);
    }
    status->addLayout(gitRow);

    cardLay->addLayout(status);

    // Composer tip — tiny one-liner pointing at the dedicated tab.
    auto *composerTip = new QLabel(QStringLiteral(
        "<i>Tip: switch to the <b>Composer</b> tab for multi-file edits with "
        "diff preview.</i>"));
    composerTip->setTextFormat(Qt::RichText);
    composerTip->setWordWrap(true);
    composerTip->setStyleSheet(QString("color: %1; font-size: 11px;").arg(pal.muted));
    cardLay->addWidget(composerTip);

    // Insert at the top of m_chatLayout (above the trailing stretch).
    m_chatLayout->insertWidget(0, card);
    m_codingWelcomeFrame = card;
}

void AIPanel::removeCodingWelcomeCard() {
    if (!m_codingWelcomeFrame) return;
    m_codingWelcomeFrame->setParent(nullptr);
    m_codingWelcomeFrame->deleteLater();
    m_codingWelcomeFrame = nullptr;
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
            // hardening: re-check m_chatArea inside the deferred lambda — the
            // panel could be destroyed between the singleShot post and fire.
            if (!m_chatArea) return;
            QScrollBar *sb = m_chatArea->verticalScrollBar();
            if (sb) sb->setValue(sb->maximum());
        });
    }
}

void AIPanel::streamIntoAssistantBubble(const QString &token) {
    if (!m_chatLayout) return;  // hardening: drop tokens if chat surface is gone
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
    if (!m_chatLayout) return;  // hardening: drop tool calls if chat surface is gone
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

    // hardening: shared JSON-parse helper. AiTools::execute always returns
    // valid compact JSON, but if a future change returns garbage (or the
    // content gets truncated/corrupted across some IPC layer) we don't
    // want to crash on .toObject() of a non-object QJsonValue.
    auto parseResultBody = [](const QString &content) -> QJsonObject {
        QJsonParseError perr{};
        const QJsonDocument jd = QJsonDocument::fromJson(content.toUtf8(), &perr);
        if (perr.error != QJsonParseError::NoError || !jd.isObject()) return QJsonObject();
        const QJsonValue rv = jd.object().value("result");
        return rv.isObject() ? rv.toObject() : QJsonObject();
    };

    // Build a one-line result summary for the card UI.
    QString resultSummary;
    if (result.isError) {
        resultSummary = "✗ " + result.errorKind;
    } else if (name == "read_file") {
        // Pull lines/truncated from the JSON content for a tight summary.
        const QJsonObject body = parseResultBody(result.content);  // hardening: shared safe-parse
        const int n = body.value("lines_emitted").toInt();
        const bool tr = body.value("truncated").toBool();
        resultSummary = QString("%1 lines%2").arg(n).arg(tr ? " (truncated)" : "");
    } else if (name == "list_dir") {
        const QJsonObject body = parseResultBody(result.content);  // hardening: shared safe-parse
        const int n = body.value("entries").toArray().size();
        resultSummary = QString("%1 entries").arg(n);
    } else if (name == "write_file") {
        const QJsonObject body = parseResultBody(result.content);  // hardening: shared safe-parse
        // Slice D — dry_run interception. Composer-mode write_file calls
        // come back with {dry_run:true, proposed:{path, before, after,
        // mode}} instead of touching disk. Route them to the Edit Plan
        // list so the user can review + click Apply per file.
        if (body.value("dry_run").toBool() && m_editPlan) {
            const QJsonObject proposed = body.value("proposed").toObject();
            const QString absPath = proposed.value("path").toString();
            const QString before  = proposed.value("before").toString();
            const QString after   = proposed.value("after").toString();
            if (!absPath.isEmpty()) {
                m_editPlan->addEdit(absPath, before, after);
                // v0.1.61: Edit Plan is now inline in the chat scroll;
                // show it so the user sees the queued edit at the
                // bottom of the transcript.
                m_editPlan->setVisible(true);
            }
            resultSummary = QString("queued for review (%1 chars)").arg(after.size());
        } else {
            const int bytes = body.value("bytes_written").toInt();
            const QString mode = body.value("mode").toString();
            const bool created = body.value("created").toBool();
            resultSummary = QString("%1 %2 (%3 bytes)")
                                .arg(created ? "created" : "wrote", mode).arg(bytes);
            // Auto-open / reload the file in the editor.
            const QString absPath = body.value("abs_path").toString();
            if (!absPath.isEmpty()) emit fileWrittenByAgent(absPath, created);
        }
    } else if (name == "search") {
        const QJsonObject body = parseResultBody(result.content);  // hardening: shared safe-parse
        const int total = body.value("total_matches").toInt();
        const int files = body.value("files_scanned").toInt();
        const bool tr = body.value("truncated").toBool();
        resultSummary = QString("%1 matches in %2 files%3")
                            .arg(total).arg(files).arg(tr ? " (truncated)" : "");
    } else if (name == "apply_diff") {
        const QJsonObject body = parseResultBody(result.content);  // hardening: shared safe-parse
        // Slice D — same dry_run interception as write_file. The
        // ai_tools layer composes proposed.before/after by reading the
        // file then materialising the post-hunk text in memory, so the
        // EditPlan side doesn't need to know about hunks.
        if (body.value("dry_run").toBool() && m_editPlan) {
            const QJsonObject proposed = body.value("proposed").toObject();
            const QString absPath = proposed.value("path").toString();
            const QString before  = proposed.value("before").toString();
            const QString after   = proposed.value("after").toString();
            if (!absPath.isEmpty()) {
                m_editPlan->addEdit(absPath, before, after);
                // v0.1.61: same inline-EditPlan reveal as write_file.
                m_editPlan->setVisible(true);
            }
            resultSummary = QString("queued for review (%1 hunk%2)")
                                .arg(args.value("hunks").toArray().size())
                                .arg(args.value("hunks").toArray().size() == 1 ? "" : "s");
        } else {
            const int hunks = body.value("hunks_applied").toInt();
            resultSummary = QString("%1 hunk%2 applied").arg(hunks).arg(hunks == 1 ? "" : "s");
            // Reload the editor for the modified file.
            const QString absPath = body.value("abs_path").toString();
            if (!absPath.isEmpty()) emit fileWrittenByAgent(absPath, false);
        }
    } else if (name == "generate_chart" && !result.isError) {
        // v0.1.63 — Data Analyst Mode chart inline. The executor echoed
        // the Vega-Lite spec back on the result body; instantiate a
        // VegaChartRenderer and slot it into the chat layout above the
        // bottom stretch, mirroring how aiAddToolCallCard / aiAddBubble
        // insert their widgets.
        const QJsonObject body = parseResultBody(result.content);
        const QJsonObject spec = body.value("spec").toObject();
        const QString chartId = body.value("chart_id").toString();
        const QString title   = body.value("title").toString();
        const AiPalette palChart = aiPalette();

        // Outer card frame: 1px border + 8px padding per the v0.1.63 spec.
        auto *card = new QFrame(m_chatContent);
        card->setObjectName("chartCard");
        card->setStyleSheet(QString(
            "#chartCard { background: %1; border: 1px solid %2; "
            "border-radius: 8px; padding: 0px; }"
            "QLabel#chartTitle { color: %3; font-size: 11px; "
            "font-weight: 600; letter-spacing: 0.4px; "
            "padding: 6px 10px 0px 10px; background: transparent; }")
            .arg(palChart.assistBg, palChart.assistBorder, palChart.muted));
        auto *cardLay = new QVBoxLayout(card);
        cardLay->setContentsMargins(8, 8, 8, 8);
        cardLay->setSpacing(4);

        if (!title.isEmpty()) {
            auto *titleLbl = new QLabel(title, card);
            titleLbl->setObjectName("chartTitle");
            titleLbl->setWordWrap(true);
            cardLay->addWidget(titleLbl);
        }

        auto *renderer = new VegaChartRenderer(card);
        cardLay->addWidget(renderer);

        // Error row sits hidden until the JS shell reports a parse /
        // runtime failure. Connecting the lambda by-value to `card`
        // means it survives the dispatch loop returning.
        auto *errLbl = new QLabel(card);
        errLbl->setStyleSheet(QString(
            "color: %1; font-size: 10px; font-style: italic;").arg(palChart.errBorder));
        errLbl->setWordWrap(true);
        errLbl->setVisible(false);
        cardLay->addWidget(errLbl);
        QObject::connect(renderer, &VegaChartRenderer::renderError, errLbl,
                         [errLbl](const QString &msg) {
                             errLbl->setText(QStringLiteral("⚠ Chart error: ") + msg);
                             errLbl->setVisible(true);
                         });

        // Wrap in a margin row, matching the bubble cadence.
        auto *row = new QWidget(m_chatContent);
        row->setStyleSheet(QStringLiteral("background: transparent;"));
        auto *rowLay = new QVBoxLayout(row);
        rowLay->setContentsMargins(0, 0, 0, 12);
        rowLay->setSpacing(0);
        rowLay->addWidget(card);
        m_chatLayout->insertWidget(m_chatLayout->count() - 1, row);

        // Feed the spec in. setSpec stashes it if the WebEngine page
        // isn't ready yet and replays on loadFinished.
        renderer->setSpec(spec);

        resultSummary = QStringLiteral("rendered (id=") + chartId + QStringLiteral(")");
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
    if (!m_ollama) {  // hardening: guard m_ollama (continueWithToolResults would deref a null nam)
        m_pendingToolResults = QJsonArray();
        appendErrorBubble(QStringLiteral("AI client unavailable — tool-call results dropped."));
        return;
    }

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

    // v0.1.61 — vision-capability gate. acceptAttachment refuses (and posts
    // a helpful refusal bubble) when an image is picked but the active model
    // can't read images. Same code path is hit by drag-and-drop in dropEvent.
    if (!acceptAttachment(path, kind)) return;

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

    if (m_attachmentChip) {  // hardening: guard m_attachmentChip
        m_attachmentChip->setText(QString("%1 %2 (%3 KB) — will be included as context. Click chip to remove.")
                                  .arg(icon).arg(name).arg(sizeKb));
        m_attachmentChip->setVisible(true);
        m_attachmentChip->setFixedHeight(24);
        m_attachmentChip->installEventFilter(this);  // catch click → clear
    }

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

// ─── Guided AI Settings dialog ──────────────────────────────────────────
//
// Two provider sections (OpenRouter, OpenAI), each with a numbered
// step-by-step layout:
//
//   1. Get your key →  hyperlink to provider's keys page
//   2. Paste it here  →  password-masked QLineEdit
//   3. Test / Save / Forget buttons + inline status label
//
// The dialog can save both keys at once — no need to switch backends to
// configure them, no shared aiApiKey field that gets clobbered.
//
// Keys are validated by hitting the provider's /v1/models endpoint with
// a fresh QNetworkAccessManager (we don't reuse m_ollama here because
// that would disrupt the panel's current backend state mid-test). If
// the request returns 200 the key is saved; on 401/403 we tell the user
// the key was rejected without persisting it.
#include <QDialog>
#include <QGroupBox>
#include <QFormLayout>
#include <QGridLayout>
#include <QDialogButtonBox>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>

namespace {

// One per-provider section inside the AI Settings dialog. Self-contained
// so the dialog body is just two of these stacked vertically.
class ProviderSection : public QGroupBox {
public:
    enum Provider { OpenRouter, OpenAI, OllamaCloud, AzureOpenAI };
    ProviderSection(Provider provider, QWidget *parent)
        : QGroupBox(parent), m_provider(provider) {
        QString title, keysUrl, prefix, hint;
        switch (provider) {
        case OpenRouter:
            title   = "OpenRouter  ·  recommended for variety";
            keysUrl = "https://openrouter.ai/keys";
            prefix  = "sk-or-v1-…";
            hint    = "367+ models — Claude, GPT, Gemini, DeepSeek, Llama, Qwen — one key";
            break;
        case OpenAI:
            title   = "OpenAI";
            keysUrl = "https://platform.openai.com/api-keys";
            prefix  = "sk-…";
            hint    = "Direct OpenAI access — GPT-4o, GPT-5, o-series reasoning";
            break;
        case OllamaCloud:
            title   = "Ollama Cloud  ·  hosted big models";
            keysUrl = "https://ollama.com/settings/keys";
            prefix  = "your Ollama Cloud key";
            hint    = "gpt-oss:120b · qwen3-coder:480b · deepseek-v3.1:671b — subscription based";
            break;
        case AzureOpenAI:
            title   = "Azure OpenAI  ·  enterprise";
            keysUrl = "https://portal.azure.com";
            prefix  = "your Azure OpenAI key";
            hint    = "Resource + deployment routing · uses api-key header (not Bearer)";
            break;
        }
        setTitle(title);
        setStyleSheet(
            "QGroupBox { font-weight: 600; font-size: 13px; "
            "  margin-top: 12px; padding-top: 14px; "
            "  border: 1px solid #D4D1C4; border-radius: 6px; }"
            "QGroupBox::title { subcontrol-origin: margin; "
            "  left: 12px; padding: 0 6px; }");

        auto *form = new QVBoxLayout(this);
        form->setContentsMargins(14, 18, 14, 14);
        form->setSpacing(10);

        // Single guidance line — replaces the previous numbered Step 1 / 2 / 3
        // chrome that ate vertical space and competed with the input itself.
        auto *hintLabel = new QLabel(QString("<span style=\"color:#444;\">%1</span>").arg(hint));
        hintLabel->setWordWrap(true);
        hintLabel->setStyleSheet("font-size: 11px;");
        form->addWidget(hintLabel);

        auto *guide = new QLabel(QString(
            "Get a key at "
            "<a href=\"%1\" style=\"color:#0B5BAF; text-decoration:none;\"><b>%1</b></a>"
            ", paste it below, then click <b>Test</b> or <b>Save</b>.")
            .arg(keysUrl));
        guide->setOpenExternalLinks(true);
        guide->setTextInteractionFlags(Qt::TextBrowserInteraction);
        guide->setWordWrap(true);
        guide->setStyleSheet("font-size: 11px; color: #666; font-weight: 400;");
        form->addWidget(guide);

        if (provider == OllamaCloud) {
            // Smart UX hint: many users will already have run `ollama signin`
            // and don't need the explicit Bearer key path. Make this obvious.
            auto *signinHint = new QLabel(
                "<i style=\"color:#777;\">Already ran "
                "<code style=\"background:#EFEDE3; padding:1px 4px; border-radius:2px;\">"
                "ollama signin</code>? Pick the <b>Ollama</b> backend instead — your local "
                "daemon proxies <code>:cloud</code> models automatically. This section is "
                "for using Ollama Cloud without the local CLI.</i>");
            signinHint->setWordWrap(true);
            signinHint->setStyleSheet("font-size: 11px; padding: 4px 0;");
            form->addWidget(signinHint);
        }

        if (provider == AzureOpenAI) {
            // Azure has three extra fields beyond the API key: resource
            // name (subdomain of openai.azure.com), deployment name (the
            // user's named deployment of e.g. gpt-4o), and API version.
            auto *grid = new QGridLayout;
            grid->setHorizontalSpacing(8);
            grid->setVerticalSpacing(6);

            const auto &cfg = Config::instance();

            auto *resLabel = new QLabel("Resource name:");
            m_azureResource = new QLineEdit;
            m_azureResource->setText(cfg.aiAzureResource);
            m_azureResource->setPlaceholderText("e.g. my-org-gpt");
            auto *resSuffix = new QLabel("<span style=\"color:#888;\">.openai.azure.com</span>");
            resSuffix->setTextFormat(Qt::RichText);
            grid->addWidget(resLabel, 0, 0);
            grid->addWidget(m_azureResource, 0, 1);
            grid->addWidget(resSuffix, 0, 2);

            auto *depLabel = new QLabel("Deployment:");
            m_azureDeployment = new QLineEdit;
            m_azureDeployment->setText(cfg.aiAzureDeployment);
            m_azureDeployment->setPlaceholderText("e.g. gpt-4o-prod");
            grid->addWidget(depLabel, 1, 0);
            grid->addWidget(m_azureDeployment, 1, 1, 1, 2);

            auto *verLabel = new QLabel("API version:");
            m_azureApiVersion = new QLineEdit;
            m_azureApiVersion->setText(cfg.aiAzureApiVersion.isEmpty()
                                        ? QStringLiteral("2024-10-21")
                                        : cfg.aiAzureApiVersion);
            grid->addWidget(verLabel, 2, 0);
            grid->addWidget(m_azureApiVersion, 2, 1);

            for (auto *e : {m_azureResource, m_azureDeployment, m_azureApiVersion}) {
                e->setMinimumHeight(28);
                e->setStyleSheet(
                    "QLineEdit { font-size: 12px; padding: 4px 8px; "
                    "  border: 1px solid #C7C4B7; border-radius: 4px; }"
                    "QLineEdit:focus { border: 1px solid #CC785C; }");
            }

            form->addLayout(grid);
        }

        // Input on its own row — so it gets the full dialog width and never
        // gets squeezed by the buttons next to it.
        m_input = new QLineEdit;
        m_input->setEchoMode(QLineEdit::Password);
        m_input->setPlaceholderText(QString("Paste your %1 key here").arg(prefix));
        m_input->setMinimumHeight(30);
        m_input->setStyleSheet(
            "QLineEdit { font-size: 12px; padding: 5px 10px; "
            "  border: 1px solid #C7C4B7; border-radius: 4px; }"
            "QLineEdit:focus { border: 1px solid #CC785C; }");
        form->addWidget(m_input);

        // Buttons + status on a second row, with the status label taking the
        // remaining space and elide-on-overflow behavior so long error
        // messages from the provider don't blow out the layout.
        auto *actionRow = new QHBoxLayout;
        actionRow->setSpacing(8);

        m_testBtn = new QPushButton("Test");
        m_testBtn->setToolTip("Send a /v1/models probe with this key without saving it");
        m_testBtn->setMinimumWidth(72);
        m_testBtn->setMinimumHeight(28);

        m_saveBtn = new QPushButton("Save");
        m_saveBtn->setToolTip("Validate, then save locally");
        m_saveBtn->setMinimumWidth(72);
        m_saveBtn->setMinimumHeight(28);
        m_saveBtn->setStyleSheet(
            "QPushButton { background: #CC785C; color: white; border: none; "
            "  border-radius: 4px; font-weight: 600; padding: 0 12px; }"
            "QPushButton:hover { background: #B86A4E; }"
            "QPushButton:disabled { background: #E0DDD2; color: #999; }");

        m_forgetBtn = new QPushButton("Forget");
        m_forgetBtn->setToolTip("Remove this key from your local config");
        m_forgetBtn->setMinimumWidth(72);
        m_forgetBtn->setMinimumHeight(28);

        actionRow->addWidget(m_testBtn);
        actionRow->addWidget(m_saveBtn);
        actionRow->addWidget(m_forgetBtn);

        m_status = new QLabel;
        m_status->setMinimumHeight(20);
        m_status->setWordWrap(false);
        m_status->setTextInteractionFlags(Qt::TextSelectableByMouse);
        m_status->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
        m_status->setText(currentKey().isEmpty()
            ? "<i style=\"color:#888;\">No key saved yet.</i>"
            : QString("<i style=\"color:#666;\">Saved key: <code>%1</code></i>").arg(maskedSavedKey()));
        m_status->setStyleSheet("font-size: 11px; padding-left: 8px;");
        actionRow->addWidget(m_status, 1);

        form->addLayout(actionRow);

        connect(m_input, &QLineEdit::textChanged, this, [this](const QString &t) {
            m_testBtn->setEnabled(!t.trimmed().isEmpty());
            m_saveBtn->setEnabled(!t.trimmed().isEmpty());
        });
        m_testBtn->setEnabled(false);
        m_saveBtn->setEnabled(false);
        m_forgetBtn->setEnabled(!currentKey().isEmpty());

        connect(m_testBtn,   &QPushButton::clicked, this, [this]() { runValidation(false); });
        connect(m_saveBtn,   &QPushButton::clicked, this, [this]() { runValidation(true); });
        connect(m_forgetBtn, &QPushButton::clicked, this, [this]() { forgetKey(); });
    }

    QString currentKey() const {
        const auto &cfg = Config::instance();
        switch (m_provider) {
        case OpenRouter:  return cfg.aiOpenRouterKey;
        case OpenAI:      return cfg.aiOpenAIKey;
        case OllamaCloud: return cfg.aiOllamaCloudKey;
        case AzureOpenAI: return cfg.aiAzureKey;
        }
        return QString();
    }

    QString maskedSavedKey() const {
        const QString k = currentKey();
        if (k.length() > 12) return k.left(8) + "…" + k.right(4);
        return QString("•").repeated(k.length());
    }

    void writeKey(const QString &k) {
        auto &cfg = Config::instance();
        switch (m_provider) {
        case OpenRouter:  cfg.aiOpenRouterKey  = k; break;
        case OpenAI:      cfg.aiOpenAIKey      = k; break;
        case OllamaCloud: cfg.aiOllamaCloudKey = k; break;
        case AzureOpenAI:
            cfg.aiAzureKey = k;
            // Persist the three extra Azure fields too — they only mean
            // anything in conjunction with the key.
            if (m_azureResource)    cfg.aiAzureResource   = m_azureResource->text().trimmed();
            if (m_azureDeployment)  cfg.aiAzureDeployment = m_azureDeployment->text().trimmed();
            if (m_azureApiVersion)  cfg.aiAzureApiVersion = m_azureApiVersion->text().trimmed();
            break;
        }
        cfg.save();
    }

    void forgetKey() {
        writeKey(QString());
        m_input->clear();
        m_forgetBtn->setEnabled(false);
        m_status->setText("<i>(no key saved)</i>");
        m_status->setStyleSheet("font-size: 11px; color: #666;");
    }

    // GET /v1/models with the candidate key. If `commitOnSuccess` is true,
    // a 200 response also persists the key to Config; otherwise it's
    // purely a connectivity / authentication probe (Test button).
    void runValidation(bool commitOnSuccess) {
        const QString candidate = m_input->text().trimmed();
        if (candidate.isEmpty()) return;

        m_testBtn->setEnabled(false);
        m_saveBtn->setEnabled(false);
        m_status->setStyleSheet("font-size: 11px; color: #666;");
        m_status->setText("<i>Validating against the provider…</i>");

        QString endpoint;
        bool azureAuth = false;
        switch (m_provider) {
        case OpenRouter:  endpoint = "https://openrouter.ai/api/v1/models"; break;
        case OpenAI:      endpoint = "https://api.openai.com/v1/models";    break;
        case OllamaCloud: endpoint = "https://ollama.com/v1/models";        break;
        case AzureOpenAI: {
            // Azure validation: list deployments under the configured
            // resource. Requires resource name + API version; deployment
            // name is not used here (it'd narrow to one model).
            const QString resource = m_azureResource
                ? m_azureResource->text().trimmed() : QString();
            const QString version  = m_azureApiVersion
                ? m_azureApiVersion->text().trimmed() : QStringLiteral("2024-10-21");
            if (resource.isEmpty()) {
                m_status->setStyleSheet("font-size: 11px; color: #B92E1B; font-weight: 600;");
                m_status->setText("✗ Resource name required");
                m_testBtn->setEnabled(true); m_saveBtn->setEnabled(true);
                return;
            }
            endpoint = QString("https://%1.openai.azure.com/openai/deployments?api-version=%2")
                          .arg(resource, version.isEmpty() ? "2024-10-21" : version);
            azureAuth = true;
            break;
        }
        }

        QNetworkRequest req((QUrl(endpoint)));
        if (azureAuth) {
            req.setRawHeader("api-key", candidate.toUtf8());
        } else {
            req.setRawHeader("Authorization", ("Bearer " + candidate).toUtf8());
        }
        req.setRawHeader("User-Agent", "Notepatra/0.1.55");

        // Lazy-create a NAM owned by this section so it lives as long as
        // the dialog. Replies are auto-cleaned via deleteLater().
        if (!m_nam) m_nam = new QNetworkAccessManager(this);
        QNetworkReply *reply = m_nam->get(req);

        connect(reply, &QNetworkReply::finished, this,
            [this, reply, candidate, commitOnSuccess]() {
                const int httpCode = reply->attribute(
                    QNetworkRequest::HttpStatusCodeAttribute).toInt();
                const QNetworkReply::NetworkError err = reply->error();
                const QByteArray body = reply->readAll();
                reply->deleteLater();

                m_testBtn->setEnabled(true);
                m_saveBtn->setEnabled(true);

                if (err == QNetworkReply::NoError && httpCode == 200) {
                    // Count returned models for an informative success line.
                    int modelCount = 0;
                    QJsonDocument doc = QJsonDocument::fromJson(body);
                    if (doc.isObject())
                        modelCount = doc.object().value("data").toArray().size();

                    if (commitOnSuccess) {
                        writeKey(candidate);
                        m_forgetBtn->setEnabled(true);
                        m_status->setStyleSheet(
                            "font-size: 11px; color: #1B7A3E; font-weight: 600;");
                        m_status->setText(QString(
                            "✓ Valid &amp; saved · %1 models reachable").arg(modelCount));
                    } else {
                        m_status->setStyleSheet(
                            "font-size: 11px; color: #1B7A3E; font-weight: 600;");
                        m_status->setText(QString(
                            "✓ Test passed · %1 models reachable · click Save to persist")
                            .arg(modelCount));
                    }
                } else {
                    QString reason;
                    if (httpCode == 401) reason = "Invalid key (HTTP 401)";
                    else if (httpCode == 403) reason = "Forbidden (HTTP 403)";
                    else if (httpCode == 429) reason = "Rate limited (HTTP 429)";
                    else if (httpCode > 0)    reason = QString("HTTP %1").arg(httpCode);
                    else                      reason = reply->errorString();
                    m_status->setStyleSheet(
                        "font-size: 11px; color: #B92E1B; font-weight: 600;");
                    m_status->setText(QString("✗ %1 — key not saved").arg(reason));
                }
            });
    }

private:
    Provider m_provider;
    QLineEdit *m_input;
    QPushButton *m_testBtn;
    QPushButton *m_saveBtn;
    QPushButton *m_forgetBtn;
    QLabel *m_status;
    QNetworkAccessManager *m_nam = nullptr;
    // Azure-only — three extra inputs above the API key row.
    QLineEdit *m_azureResource    = nullptr;
    QLineEdit *m_azureDeployment  = nullptr;
    QLineEdit *m_azureApiVersion  = nullptr;
};

}  // namespace

void AIPanel::openAiSettingsDialog() {
    QDialog dlg(this);
    dlg.setWindowTitle("AI Settings — Cloud Providers");
    dlg.setMinimumWidth(640);
    dlg.setMinimumHeight(460);
    dlg.resize(720, 500);

    auto *root = new QVBoxLayout(&dlg);
    root->setContentsMargins(20, 18, 20, 14);
    root->setSpacing(14);

    auto *intro = new QLabel(
        "<b>Configure cloud AI providers.</b><br>"
        "Both keys can be saved at once — Notepatra picks the right one "
        "based on which backend you select in the panel. Keys are stored "
        "locally only (in <code>~/.config/notepatra/notepatra.conf</code>) "
        "and never synced or transmitted except as the <code>Authorization: "
        "Bearer</code> header on requests to the provider you chose.");
    intro->setWordWrap(true);
    intro->setStyleSheet("font-size: 12px;");
    root->addWidget(intro);

    // Wrap sections in a scroll area — three providers + intro + footer is
    // taller than a typical first-time dialog screen, and we want every
    // setting reachable without resizing.
    auto *scroll = new QScrollArea(&dlg);
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    auto *scrollHost = new QWidget;
    auto *scrollBox = new QVBoxLayout(scrollHost);
    scrollBox->setContentsMargins(0, 0, 0, 0);
    scrollBox->setSpacing(12);
    auto *or_section = new ProviderSection(ProviderSection::OpenRouter, scrollHost);
    auto *oa_section = new ProviderSection(ProviderSection::OpenAI, scrollHost);
    auto *oc_section = new ProviderSection(ProviderSection::OllamaCloud, scrollHost);
    auto *az_section = new ProviderSection(ProviderSection::AzureOpenAI, scrollHost);
    scrollBox->addWidget(or_section);
    scrollBox->addWidget(oa_section);
    scrollBox->addWidget(oc_section);
    scrollBox->addWidget(az_section);
    scrollBox->addStretch(1);
    scroll->setWidget(scrollHost);
    root->addWidget(scroll, 1);

    auto *btnBox = new QDialogButtonBox(QDialogButtonBox::Close);
    connect(btnBox, &QDialogButtonBox::rejected, &dlg, &QDialog::accept);
    connect(btnBox, &QDialogButtonBox::accepted, &dlg, &QDialog::accept);
    root->addWidget(btnBox);

    dlg.exec();
}

// v0.1.61 (Item 8) — handler for the bottom 3-segment toggle.
// Stores the new value and refreshes the input placeholder hint so
// the user knows what the next message will do. Purely internal
// state — the host MainWindow doesn't get a signal.
void AIPanel::chatModeSelectorChanged(int segment) {
    switch (segment) {
        case static_cast<int>(ChatModeSegment::Chat):
            m_chatModeSegment = ChatModeSegment::Chat;
            break;
        case static_cast<int>(ChatModeSegment::Compose):
            m_chatModeSegment = ChatModeSegment::Compose;
            break;
        case static_cast<int>(ChatModeSegment::Agent):
            m_chatModeSegment = ChatModeSegment::Agent;
            break;
        default:
            return;
    }

    // Refresh the input placeholder so the affordance stays consistent
    // with the active intent + segment combo. The intent layer's
    // existing placeholder is a fine baseline for Chat; Compose / Agent
    // append a hint about the tool surface.
    if (m_customInput) {
        const bool coding = m_codingMode && m_codingMode->isChecked();
        const bool data   = m_dataMode   && m_dataMode->isChecked();
        QString base =
              coding ? QStringLiteral("Coding Mode · e.g. Refactor this function / Add types / Translate to TypeScript")
            : data   ? QStringLiteral("Data Mode · e.g. summarize this CSV / show top 10 customers / chart revenue by month")
                     : QStringLiteral("Type a message and press Enter to send…");
        switch (m_chatModeSegment) {
            case ChatModeSegment::Compose:
                base = QStringLiteral("Compose · proposed edits land in the Edit Plan for review");
                break;
            case ChatModeSegment::Agent:
                if (coding) base = QStringLiteral("Agent · autonomous loop · edits hit disk on the fly");
                break;
            case ChatModeSegment::Chat:
            default:
                break;
        }
        m_customInput->setPlaceholderText(base);
    }
}

// ─── Slice D — apply Edit Plan rows ─────────────────────────────────
//
// Called when EditPlanList::applyRequested fires. Each pair is the
// absolute file path + the post-edit text the user approved. We write
// each one with the same atomic .tmp + rename pattern AiTools uses for
// the non-dry path, then emit fileWrittenByAgent so the open editor
// picks up the change (or opens the file if it wasn't open). Failures
// surface as a single error bubble listing the bad paths — partial
// success still counts as success for the rows that wrote cleanly.
//
// The Edit Plan list keeps the failed rows so the user can retry; the
// successful rows are removed by EditPlanList itself when applyRequested
// completes (it removes whatever it sent us, then the host can re-add
// any failures via addEdit if needed). For now we just log failures.
void AIPanel::applyComposerEdits(const QList<QPair<QString, QString>> &edits) {
    if (edits.isEmpty()) return;

    QStringList failures;
    int wrote = 0;
    for (const auto &pair : edits) {
        const QString &absPath = pair.first;
        const QString &after   = pair.second;
        if (absPath.isEmpty()) {
            failures << "(empty path)";
            continue;
        }

        // Ensure parent directory exists. The AI tool layer already
        // canonicalised + workspace-anchored this path; we just need to
        // make the on-disk parent exist before we write.
        QFileInfo fi(absPath);
        QDir parent = fi.dir();
        if (!parent.exists() && !parent.mkpath(".")) {
            failures << absPath + " (parent dir creation failed)";
            continue;
        }

        // Atomic write — same pattern as ai_tools::executeWriteFile.
        const QString tmp = absPath + ".notepatra.tmp";
        QFile out(tmp);
        if (!out.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
            failures << absPath + " (open " + tmp + " failed: " + out.errorString() + ")";
            continue;
        }
        const QByteArray bytes = after.toUtf8();
        const qint64 written = out.write(bytes);
        out.close();
        if (written != bytes.size()) {
            QFile::remove(tmp);
            failures << absPath + " (short write: " + QString::number(written) + "/"
                                + QString::number(bytes.size()) + ")";
            continue;
        }
        // QFile::rename refuses to overwrite on some platforms; remove
        // first if the destination exists, then rename. This races with
        // an external editor saving simultaneously, but the same is true
        // of the non-dry path (and the user is reviewing the diff at
        // this point so concurrent external saves are unlikely).
        const bool wasNew = !QFile::exists(absPath);
        if (!wasNew) QFile::remove(absPath);
        if (!QFile::rename(tmp, absPath)) {
            QFile::remove(tmp);
            failures << absPath + " (rename failed)";
            continue;
        }

        ++wrote;
        emit fileWrittenByAgent(absPath, wasNew);
    }

    // Surface a summary bubble. Successful rows are silent (the file
    // tabs popping open / reloading is the user-visible signal); any
    // failures get a single grouped error message.
    if (!failures.isEmpty()) {
        QString msg = QString("Composer Apply: %1 wrote, %2 failed:")
                        .arg(wrote).arg(failures.size());
        for (const QString &f : failures) msg += "\n  • " + f;
        appendErrorBubble(msg);
    }
}

// v0.1.59 — Gate the chat input + Send button on whether a real model is
// currently selected. Behavior:
//   • Dropdown disabled OR text empty OR text wrapped in parens like
//     "(detecting…)" / "(Ollama offline)" / "(no models installed)" /
//     "(API key required)" → input + Send disabled, placeholder explains
//     why.
//   • Real model name selected → input + Send enabled, mode-appropriate
//     placeholder restored.
// Called from m_modelCombo currentTextChanged AND from any code path that
// flips the dropdown's enabled state (ollama probe complete, openai key
// changed, backend switch). Safe to call before m_customInput / m_sendBtn
// exist (early-construction path) — every member access is guarded.
void AIPanel::updateInputAvailability() {
    if (!m_customInput && !m_sendBtn) return;

    bool ready = false;
    if (m_modelCombo) {
        const QString text = m_modelCombo->currentText().trimmed();
        // Real model = dropdown is interactive AND the visible label
        // isn't a placeholder/error string. We use the parenthesis
        // convention because every error/placeholder entry in this file
        // wraps its text in `(...)` — see "(detecting…)", "(Ollama
        // offline)", "(no models installed)", "(API key required)",
        // "(no model)", etc.
        ready = m_modelCombo->isEnabled()
             && !text.isEmpty()
             && !(text.startsWith('(') && text.endsWith(')'));
    }

    if (m_customInput) {
        m_customInput->setEnabled(ready);
        if (!ready) {
            // v0.1.61 — placeholders now teach the user how to fix the
            // disabled state. The prior copy ("Select a model…") was
            // accurate but didn't help a first-time user who had no idea
            // where to find a model in the first place. Each state gives
            // a concrete next action with the exact command to run.
            QString why = QStringLiteral(
                "Please select a model to start chatting. "
                "Pick one from the dropdown above — Ollama runs locally on your machine, "
                "or click ⚙ to add an API key for OpenRouter / OpenAI / Azure / Ollama Cloud.");
            if (m_modelCombo) {
                const QString text = m_modelCombo->currentText().trimmed();
                if (text.contains("offline", Qt::CaseInsensitive))
                    why = QStringLiteral(
                        "Ollama is offline. Open a terminal and run `ollama serve`, "
                        "then click ↻ Refresh above. Alternatively, switch the backend "
                        "dropdown to llama.cpp / OpenRouter / OpenAI / Azure to use a "
                        "different runner.");
                else if (text.contains("no models", Qt::CaseInsensitive))
                    why = QStringLiteral(
                        "Ollama is running but has no models installed yet. Run "
                        "`ollama pull qwen2.5-coder:7b` for code work (~5 GB), or "
                        "`ollama pull llama3.2:3b` for a lightweight chat model "
                        "(~2 GB). Then click ↻ Refresh above.");
                else if (text.contains("api key", Qt::CaseInsensitive)
                      || text.contains("key required", Qt::CaseInsensitive))
                    why = QStringLiteral(
                        "This backend needs an API key. Click ⚙ Settings (top of the AI dock) "
                        "to paste your key for OpenRouter / OpenAI / Azure / Ollama Cloud, "
                        "save, and pick a model from the dropdown.");
                else if (text.contains("detecting", Qt::CaseInsensitive))
                    why = QStringLiteral(
                        "Detecting available models… "
                        "If this hangs, click ↻ Refresh or switch the backend dropdown.");
            }
            m_customInput->setPlaceholderText(why);
        }
        // When ready, leave the placeholder as whatever applyMode last
        // set (Chat / Coding / Data appropriate) — don't overwrite it.
    }

    if (m_sendBtn) {
        m_sendBtn->setEnabled(ready);
        m_sendBtn->setToolTip(ready
            ? QString()
            : QStringLiteral("Pick a model first — see the dropdown above"));
    }
}
