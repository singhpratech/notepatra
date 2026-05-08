#include "mainwindow.h"
#include "editor.h"
#include "npp_palette.h"
#include "preferences.h"
#include "rustbridge.h"
#include "fonts.h"
#include "updater.h"
#include <QDir>
#include <QDirIterator>

#include <QCheckBox>
#include <QRegularExpression>
#include <QNetworkAccessManager>
#include <QSslSocket>
#include <QNetworkRequest>
#include <QNetworkReply>
#include <QJsonDocument>
#include <QJsonObject>
#include <QDesktopServices>
#include <QSettings>
#include <QTimer>
#include <QUrl>

// ─── Tolerant JSON pretty printer ─────────────────────────────────────
// Tokenizes ANY JSON-ish input (even broken JSON) and re-emits it with
// uniform indentation + one logical token group per line. Handles cases
// where the broken input is missing commas, has unquoted keys, single
// quotes, etc — the tokenizer recovers and the printer aligns it so a
// line-by-line diff against the FIXED version shows actual semantic
// changes (added quotes, fixed values) instead of just whitespace noise.
//
// Strategy: tokenize first (string / number / identifier / punctuation),
// then walk tokens and emit. When we see two consecutive value tokens
// with no separator, insert a comma+newline anyway so the diff aligns.
static QString tolerantPrettyJson(const QString &input, int indentSize = 4) {
    // ─── 1. Tokenize ───
    struct Token {
        enum Kind { Punct, String, Bare } kind;
        QString text;
        QChar punct;  // only valid for Punct
    };
    QList<Token> toks;

    int i = 0;
    while (i < input.length()) {
        QChar c = input[i];
        if (c.isSpace()) { ++i; continue; }

        if (c == '"' || c == '\'') {
            // String literal — read until matching quote, honoring escapes
            QChar quote = c;
            int j = i + 1;
            QString s; s += '"';
            while (j < input.length()) {
                QChar cc = input[j];
                if (cc == '\\' && j + 1 < input.length()) {
                    s += cc; s += input[j+1];
                    j += 2; continue;
                }
                if (cc == quote) { ++j; break; }
                s += cc; ++j;
            }
            s += '"';
            toks.append({Token::String, s, QChar()});
            i = j;
            continue;
        }

        if (c == '{' || c == '}' || c == '[' || c == ']' ||
            c == ',' || c == ':') {
            toks.append({Token::Punct, QString(c), c});
            ++i;
            continue;
        }

        // Bare token: identifier, number, or keyword (true/false/null/etc)
        // Read until whitespace or punctuation
        int j = i;
        while (j < input.length()) {
            QChar cc = input[j];
            if (cc.isSpace()) break;
            if (cc == '{' || cc == '}' || cc == '[' || cc == ']' ||
                cc == ',' || cc == ':' || cc == '"' || cc == '\'') break;
            ++j;
        }
        if (j > i) {
            toks.append({Token::Bare, input.mid(i, j - i), QChar()});
            i = j;
        } else {
            // Couldn't advance — single weird char, skip
            ++i;
        }
    }

    // ─── 2. Emit with indentation ───
    QString out;
    int level = 0;
    auto indent = [&](int n) {
        out += QString(qMax(0, n) * indentSize, QChar(' '));
    };
    auto isValue = [](const Token &t) {
        return t.kind == Token::String || t.kind == Token::Bare;
    };
    auto isCloser = [](const Token &t) {
        return t.kind == Token::Punct && (t.punct == '}' || t.punct == ']');
    };

    for (int k = 0; k < toks.size(); ++k) {
        const Token &t = toks[k];
        const Token *prev = (k > 0) ? &toks[k-1] : nullptr;

        // Insert a synthetic newline if previous token was a value or closer
        // and this token is ALSO a value or opener — i.e. missing comma case.
        bool needSyntheticBreak = false;
        if (prev) {
            bool prevWasEnd = isValue(*prev) || isCloser(*prev);
            bool thisIsStart = isValue(t) ||
                (t.kind == Token::Punct && (t.punct == '{' || t.punct == '['));
            if (prevWasEnd && thisIsStart) {
                needSyntheticBreak = true;
            }
        }

        if (needSyntheticBreak) {
            out += '\n';
            indent(level);
        }

        if (t.kind == Token::Punct) {
            QChar p = t.punct;
            if (p == '{' || p == '[') {
                out += p;
                ++level;
                out += '\n';
                indent(level);
            } else if (p == '}' || p == ']') {
                --level;
                while (out.endsWith(' ')) out.chop(1);
                if (!out.endsWith('\n')) out += '\n';
                indent(level);
                out += p;
            } else if (p == ',') {
                while (out.endsWith(' ')) out.chop(1);
                out += ',';
                out += '\n';
                indent(level);
            } else if (p == ':') {
                out += ": ";
            }
        } else if (t.kind == Token::String) {
            out += t.text;
        } else { // Bare — wrap in quotes if it looks like an unquoted key
            // (heuristic: next non-whitespace token is ':')
            bool isKey = (k + 1 < toks.size() &&
                          toks[k+1].kind == Token::Punct &&
                          toks[k+1].punct == ':');
            if (isKey) {
                out += '"';
                out += t.text;
                out += '"';
            } else {
                out += t.text;
            }
        }
    }
    return out;
}

#include <QApplication>
#include <QClipboard>
#include <QCloseEvent>
#include <QInputDialog>
#include <QProcess>
#include <QPrinter>
#include <QPrintDialog>
#include <QTextDocument>
#include <QTextStream>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QTimer>
#include "compare.h"
#include "hexeditor.h"
#include "gitgutter.h"
#include "fmtpanel.h"
#include "ollama.h"
#include "ollamastatus.h"
#include <QRegularExpression>
#include <QFileDialog>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QVBoxLayout>
#include <QMenuBar>
#include <QMessageBox>
#include <QScreen>
#include <QShortcut>
#include <QSplitter>
#include <QStatusBar>
#include <QTextBrowser>
#include <QToolBar>
#include <QToolButton>
#include <QDialog>
#include <QDialogButtonBox>
#include <QPainter>
#include <QPainterPath>
#include <QLinearGradient>
#include <QMimeData>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QDesktopServices>
#include <QUrl>
#include <Qsci/qsciscintilla.h>

static QAction *findActionByPrefix(QObject *root, const QString &prefix) {
    for (QAction *action : root->findChildren<QAction *>()) {
        if (action && action->text().startsWith(prefix)) return action;
    }
    return nullptr;
}

static void drawAiFeatureGlyph(QPainter &painter, const QRectF &rect) {
    painter.setPen(QPen(Qt::white, 1.8, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
    painter.setBrush(Qt::NoBrush);

    QRectF bubble(rect.left() + 4.5, rect.top() + 6.0, rect.width() - 10.0, rect.height() - 13.0);
    QPainterPath bubblePath;
    bubblePath.addRoundedRect(bubble, 6.0, 6.0);
    bubblePath.moveTo(bubble.left() + 10.0, bubble.bottom());
    bubblePath.lineTo(bubble.left() + 13.0, bubble.bottom() + 4.0);
    bubblePath.lineTo(bubble.left() + 17.0, bubble.bottom() - 0.2);
    painter.drawPath(bubblePath);

    painter.setPen(Qt::NoPen);
    painter.setBrush(QColor("#DDF5FF"));
    for (int i = 0; i < 3; ++i)
        painter.drawEllipse(QPointF(bubble.left() + 8.5 + i * 5.0, bubble.center().y()), 1.5, 1.5);

    painter.setBrush(QColor("#F6D26B"));
    QPainterPath spark;
    spark.moveTo(rect.right() - 8.0, rect.top() + 5.0);
    spark.lineTo(rect.right() - 6.4, rect.top() + 8.2);
    spark.lineTo(rect.right() - 3.3, rect.top() + 9.2);
    spark.lineTo(rect.right() - 6.1, rect.top() + 10.9);
    spark.lineTo(rect.right() - 6.8, rect.top() + 14.1);
    spark.lineTo(rect.right() - 8.9, rect.top() + 11.5);
    spark.lineTo(rect.right() - 12.1, rect.top() + 12.1);
    spark.lineTo(rect.right() - 10.4, rect.top() + 9.3);
    spark.lineTo(rect.right() - 11.9, rect.top() + 6.5);
    spark.lineTo(rect.right() - 8.8, rect.top() + 7.3);
    spark.closeSubpath();
    painter.drawPath(spark);
}

static void drawSearchFeatureGlyph(QPainter &painter, const QRectF &rect) {
    // Magnifying-glass icon for the Project Search toolbar button.
    // Circle lens in the upper-left, diagonal handle extending to the
    // bottom-right. White stroke on the feature's accent-coloured tile.
    painter.setPen(QPen(Qt::white, 2.0, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
    painter.setBrush(Qt::NoBrush);

    const qreal r = rect.width() * 0.28;
    const QPointF center(rect.left() + rect.width() * 0.40,
                         rect.top()  + rect.height() * 0.40);
    painter.drawEllipse(center, r, r);

    // Handle: 45° line from lens edge to rect corner
    const QPointF edge(center.x() + r * 0.707, center.y() + r * 0.707);
    const QPointF tip (rect.right() - 5.0,     rect.bottom() - 5.0);
    painter.drawLine(edge, tip);
}

static void drawTerminalFeatureGlyph(QPainter &painter, const QRectF &rect) {
    painter.setPen(QPen(Qt::white, 1.6, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
    painter.setBrush(Qt::NoBrush);

    QRectF screen(rect.left() + 4.0, rect.top() + 5.5, rect.width() - 8.0, rect.height() - 11.0);
    painter.drawRoundedRect(screen, 4.0, 4.0);
    painter.drawLine(QPointF(screen.left() + 4.0, screen.top() + 8.0),
                     QPointF(screen.left() + 8.0, screen.top() + 11.5));
    painter.drawLine(QPointF(screen.left() + 4.0, screen.top() + 15.0),
                     QPointF(screen.left() + 8.0, screen.top() + 11.5));
    painter.drawLine(QPointF(screen.left() + 11.5, screen.top() + 16.0),
                     QPointF(screen.left() + 18.5, screen.top() + 16.0));
}

static void drawCompareFeatureGlyph(QPainter &painter, const QRectF &rect) {
    QRectF leftDoc(rect.left() + 4.5, rect.top() + 5.5, rect.width() / 2.0 - 3.5, rect.height() - 11.0);
    QRectF rightDoc(rect.center().x() + 0.5, rect.top() + 5.5, rect.width() / 2.0 - 5.0, rect.height() - 11.0);

    painter.setPen(QPen(Qt::white, 1.3, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
    painter.setBrush(QColor(255, 255, 255, 28));
    painter.drawRoundedRect(leftDoc, 3.5, 3.5);
    painter.drawRoundedRect(rightDoc, 3.5, 3.5);

    painter.setPen(QPen(QColor("#F48771"), 1.6, Qt::SolidLine, Qt::RoundCap));
    painter.drawLine(QPointF(leftDoc.left() + 3.5, leftDoc.top() + 8.0),
                     QPointF(leftDoc.right() - 3.5, leftDoc.top() + 8.0));
    painter.drawLine(QPointF(leftDoc.left() + 3.5, leftDoc.top() + 13.0),
                     QPointF(leftDoc.right() - 7.0, leftDoc.top() + 13.0));

    painter.setPen(QPen(QColor("#7DDA99"), 1.6, Qt::SolidLine, Qt::RoundCap));
    painter.drawLine(QPointF(rightDoc.left() + 3.5, rightDoc.top() + 8.0),
                     QPointF(rightDoc.right() - 6.5, rightDoc.top() + 8.0));
    painter.drawLine(QPointF(rightDoc.left() + 3.5, rightDoc.top() + 16.0),
                     QPointF(rightDoc.right() - 3.5, rightDoc.top() + 16.0));

    painter.setPen(QPen(QColor("#F7D774"), 1.6));
    painter.drawLine(QPointF(rect.center().x(), rect.top() + 6.0),
                     QPointF(rect.center().x(), rect.bottom() - 6.0));
}

static void drawJsonFeatureGlyph(QPainter &painter, const QRectF &rect) {
    painter.setPen(QPen(Qt::white, 1.6, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
    painter.setBrush(Qt::NoBrush);
    painter.drawLine(QPointF(rect.left() + 10.0, rect.top() + 7.0), QPointF(rect.left() + 7.5, rect.top() + 10.0));
    painter.drawLine(QPointF(rect.left() + 7.5, rect.top() + 10.0), QPointF(rect.left() + 10.0, rect.top() + 13.0));
    painter.drawLine(QPointF(rect.left() + 10.0, rect.top() + 13.0), QPointF(rect.left() + 7.5, rect.top() + 16.0));
    painter.drawLine(QPointF(rect.left() + 7.5, rect.top() + 16.0), QPointF(rect.left() + 10.0, rect.top() + 19.0));

    painter.drawLine(QPointF(rect.right() - 10.0, rect.top() + 7.0), QPointF(rect.right() - 7.5, rect.top() + 10.0));
    painter.drawLine(QPointF(rect.right() - 7.5, rect.top() + 10.0), QPointF(rect.right() - 10.0, rect.top() + 13.0));
    painter.drawLine(QPointF(rect.right() - 10.0, rect.top() + 13.0), QPointF(rect.right() - 7.5, rect.top() + 16.0));
    painter.drawLine(QPointF(rect.right() - 7.5, rect.top() + 16.0), QPointF(rect.right() - 10.0, rect.top() + 19.0));

    painter.setPen(QPen(QColor("#D6ECFF"), 1.4, Qt::SolidLine, Qt::RoundCap));
    painter.drawLine(QPointF(rect.center().x() - 1.5, rect.top() + 8.5),
                     QPointF(rect.center().x() - 1.5, rect.bottom() - 8.0));
    painter.drawLine(QPointF(rect.center().x() - 1.5, rect.top() + 8.5),
                     QPointF(rect.center().x() + 6.0, rect.top() + 8.5));
    painter.drawLine(QPointF(rect.center().x() - 1.5, rect.center().y()),
                     QPointF(rect.center().x() + 7.0, rect.center().y()));
    painter.drawLine(QPointF(rect.center().x() - 1.5, rect.bottom() - 8.0),
                     QPointF(rect.center().x() + 5.0, rect.bottom() - 8.0));

    painter.setPen(Qt::NoPen);
    painter.setBrush(QColor("#AEE3FF"));
    painter.drawEllipse(QPointF(rect.center().x() - 1.5, rect.top() + 8.5), 1.9, 1.9);
    painter.setBrush(QColor("#7FDBB6"));
    painter.drawEllipse(QPointF(rect.center().x() + 7.5, rect.top() + 8.5), 1.9, 1.9);
    painter.setBrush(QColor("#F7D774"));
    painter.drawEllipse(QPointF(rect.center().x() + 8.5, rect.center().y()), 1.9, 1.9);
    painter.setBrush(QColor("#F7A08A"));
    painter.drawEllipse(QPointF(rect.center().x() + 5.5, rect.bottom() - 8.0), 1.9, 1.9);
}

static void drawHtmlFeatureGlyph(QPainter &painter, const QRectF &rect) {
    painter.setPen(QPen(Qt::white, 2.0, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
    painter.setBrush(Qt::NoBrush);
    painter.drawLine(QPointF(rect.left() + 9.0, rect.center().y()),
                     QPointF(rect.left() + 13.0, rect.top() + 9.0));
    painter.drawLine(QPointF(rect.left() + 9.0, rect.center().y()),
                     QPointF(rect.left() + 13.0, rect.bottom() - 9.0));
    painter.drawLine(QPointF(rect.right() - 9.0, rect.center().y()),
                     QPointF(rect.right() - 13.0, rect.top() + 9.0));
    painter.drawLine(QPointF(rect.right() - 9.0, rect.center().y()),
                     QPointF(rect.right() - 13.0, rect.bottom() - 9.0));
    painter.setPen(QPen(QColor("#FFD48A"), 1.8, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
    painter.drawLine(QPointF(rect.center().x() + 2.0, rect.top() + 8.5),
                     QPointF(rect.center().x() - 1.0, rect.bottom() - 8.5));
}

static void drawSqlFeatureGlyph(QPainter &painter, const QRectF &rect) {
    painter.setPen(QPen(Qt::white, 1.5, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
    painter.setBrush(QColor(255, 255, 255, 24));

    QRectF body(rect.left() + 7.0, rect.top() + 8.0, rect.width() - 14.0, rect.height() - 14.0);
    QRectF topOval(body.left(), body.top() - 2.5, body.width(), 7.0);
    QRectF midOval(body.left(), body.center().y() - 2.0, body.width(), 7.0);
    QRectF bottomOval(body.left(), body.bottom() - 4.0, body.width(), 7.0);

    painter.drawEllipse(topOval);
    painter.drawLine(QPointF(body.left(), topOval.center().y()),
                     QPointF(body.left(), bottomOval.center().y()));
    painter.drawLine(QPointF(body.right(), topOval.center().y()),
                     QPointF(body.right(), bottomOval.center().y()));
    painter.drawArc(midOval, 0, 180 * 16);
    painter.drawEllipse(bottomOval);
}

static void drawBracketsFeatureGlyph(QPainter &painter, const QRectF &rect) {
    painter.setPen(QPen(Qt::white, 1.9, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
    painter.setBrush(Qt::NoBrush);

    painter.drawLine(QPointF(rect.left() + 10.0, rect.top() + 7.5), QPointF(rect.left() + 6.5, rect.top() + 7.5));
    painter.drawLine(QPointF(rect.left() + 6.5, rect.top() + 7.5), QPointF(rect.left() + 6.5, rect.bottom() - 7.5));
    painter.drawLine(QPointF(rect.left() + 6.5, rect.bottom() - 7.5), QPointF(rect.left() + 10.0, rect.bottom() - 7.5));

    painter.drawLine(QPointF(rect.right() - 10.0, rect.top() + 7.5), QPointF(rect.right() - 6.5, rect.top() + 7.5));
    painter.drawLine(QPointF(rect.right() - 6.5, rect.top() + 7.5), QPointF(rect.right() - 6.5, rect.bottom() - 7.5));
    painter.drawLine(QPointF(rect.right() - 6.5, rect.bottom() - 7.5), QPointF(rect.right() - 10.0, rect.bottom() - 7.5));

    painter.setBrush(QColor("#F6D26B"));
    painter.setPen(Qt::NoPen);
    painter.drawRoundedRect(QRectF(rect.center().x() - 4.0, rect.center().y() - 4.0, 8.0, 8.0), 2.0, 2.0);
}

static void drawRestFeatureGlyph(QPainter &painter, const QRectF &rect) {
    painter.setPen(QPen(Qt::white, 1.7, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
    painter.setBrush(Qt::NoBrush);

    painter.drawLine(QPointF(rect.left() + 8.0, rect.top() + 10.0),
                     QPointF(rect.right() - 10.0, rect.top() + 10.0));
    painter.drawLine(QPointF(rect.right() - 13.5, rect.top() + 7.0),
                     QPointF(rect.right() - 9.5, rect.top() + 10.0));
    painter.drawLine(QPointF(rect.right() - 13.5, rect.top() + 13.0),
                     QPointF(rect.right() - 9.5, rect.top() + 10.0));

    painter.drawLine(QPointF(rect.right() - 8.0, rect.bottom() - 10.0),
                     QPointF(rect.left() + 10.0, rect.bottom() - 10.0));
    painter.drawLine(QPointF(rect.left() + 13.5, rect.bottom() - 13.0),
                     QPointF(rect.left() + 9.5, rect.bottom() - 10.0));
    painter.drawLine(QPointF(rect.left() + 13.5, rect.bottom() - 7.0),
                     QPointF(rect.left() + 9.5, rect.bottom() - 10.0));

    painter.setPen(Qt::NoPen);
    painter.setBrush(QColor("#D4F7FF"));
    painter.drawEllipse(QPointF(rect.left() + 7.0, rect.top() + 10.0), 2.1, 2.1);
    painter.drawEllipse(QPointF(rect.right() - 7.0, rect.bottom() - 10.0), 2.1, 2.1);
}

static void drawGitFeatureGlyph(QPainter &painter, const QRectF &rect) {
    painter.setPen(QPen(Qt::white, 1.7, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
    painter.setBrush(Qt::NoBrush);

    QPointF top(rect.left() + 10.0, rect.top() + 8.0);
    QPointF mid(rect.center().x(), rect.center().y());
    QPointF bottom(rect.left() + 10.0, rect.bottom() - 8.0);
    QPointF right(rect.right() - 9.0, rect.top() + 11.0);

    painter.drawLine(top, mid);
    painter.drawLine(mid, bottom);
    painter.drawLine(mid, right);

    painter.setPen(Qt::NoPen);
    painter.setBrush(QColor("#D6ECFF"));
    painter.drawEllipse(top, 2.2, 2.2);
    painter.drawEllipse(mid, 2.2, 2.2);
    painter.drawEllipse(bottom, 2.2, 2.2);
    painter.drawEllipse(right, 2.2, 2.2);
}

static QIcon makeFeatureIcon(const QColor &base, const QString &iconKind, const QString &glyph = QString()) {
    QPixmap pixmap(32, 32);
    pixmap.fill(Qt::transparent);

    QPainter painter(&pixmap);
    painter.setRenderHint(QPainter::Antialiasing);

    QRect rect = pixmap.rect().adjusted(1, 1, -1, -1);
    QLinearGradient gradient(rect.topLeft(), rect.bottomRight());
    gradient.setColorAt(0.0, base.lighter(125));
    gradient.setColorAt(1.0, base.darker(110));

    painter.setPen(QPen(base.darker(160), 1));
    painter.setBrush(gradient);
    painter.drawRoundedRect(rect, 8, 8);

    if (iconKind == "ai") {
        drawAiFeatureGlyph(painter, rect);
    } else if (iconKind == "search") {
        drawSearchFeatureGlyph(painter, rect);
    } else if (iconKind == "terminal") {
        drawTerminalFeatureGlyph(painter, rect);
    } else if (iconKind == "compare") {
        drawCompareFeatureGlyph(painter, rect);
    } else if (iconKind == "json") {
        drawJsonFeatureGlyph(painter, rect);
    } else if (iconKind == "html") {
        drawHtmlFeatureGlyph(painter, rect);
    } else if (iconKind == "sql") {
        drawSqlFeatureGlyph(painter, rect);
    } else if (iconKind == "brackets") {
        drawBracketsFeatureGlyph(painter, rect);
    } else if (iconKind == "rest") {
        drawRestFeatureGlyph(painter, rect);
    } else if (iconKind == "git") {
        drawGitFeatureGlyph(painter, rect);
    } else {
        QFont font = notepatraCodeFont(glyph.size() > 2 ? 8 : 10, QFont::Bold);
        painter.setFont(font);
        painter.setPen(Qt::white);
        painter.drawText(rect, Qt::AlignCenter, glyph);
    }

    return QIcon(pixmap);
}

static void addFeatureShortcut(QToolBar *toolbar, QAction *action,
                               const QColor &base, const QString &iconKind,
                               const QString &label, const QString &tooltip = QString()) {
    if (!toolbar || !action) return;

    auto *button = new QToolButton;
    button->setObjectName("featureShortcutButton");
    button->setToolButtonStyle(Qt::ToolButtonTextUnderIcon);
    button->setAutoRaise(true);
    button->setCursor(Qt::PointingHandCursor);
    button->setIcon(makeFeatureIcon(base, iconKind));
    button->setIconSize(QSize(32, 32));
    button->setText(label);
    button->setToolTip(tooltip.isEmpty() ? action->text() : tooltip);
    button->setMinimumSize(78, 60);

    QObject::connect(button, &QToolButton::clicked, action, &QAction::trigger);
    toolbar->addWidget(button);
}

static QString featureGuideHtml() {
    return QStringLiteral(R"HTML(
<html>
<head>
<style>
body { font-family: %1; line-height: 1.45; color: #d4d4d4; background: #1e1e1e; }
h1 { color: #4ec9b0; margin: 0 0 12px 0; font-size: 24px; }
h2 { color: #7ec8ff; margin: 18px 0 6px 0; font-size: 18px; }
h3 { color: #f2c14e; margin: 14px 0 4px 0; font-size: 15px; }
p, li { font-size: 13px; }
code, pre { font-family: %2; }
code { background: #252526; color: #f7d774; padding: 1px 4px; border-radius: 4px; }
ul { margin-top: 4px; }
</style>
</head>
<body>
<h1>Notepatra Help Guide</h1>
<p>Open this guide from the top <b>?</b> menu whenever you need to understand what a built-in tool does or where a feature lives. The icon row exposes the major workflows quickly; the menus expose the rest of the editor.</p>

<h2>Getting Started</h2>
<ul>
<li><b>Create and open files:</b> Use <code>Ctrl+N</code>, <code>Ctrl+O</code>, drag and drop, or the File menu.</li>
<li><b>Save and restore work:</b> Session restore and crash recovery are built in. Modified files are autosaved to recovery storage.</li>
<li><b>Workspace navigation:</b> Open a folder as a workspace and browse files from the sidebar with <code>Ctrl+Shift+E</code>.</li>
<li><b>Status bar:</b> The bottom bar shows line, column, language, encoding, EOL mode, line count, length, and word count.</li>
<li><b>Where features live:</b> use the icon row for the main built-in tools, the <b>Features</b> menu for editing helpers, the <b>Tools</b> menu for utilities, and the <b>Plugins</b> menu for formatter-style panels.</li>
</ul>

<h2>Core Editing</h2>
<ul>
<li><b>Syntax highlighting:</b> Use the Language menu or file extension detection. Notepatra supports a broad lexer set including Python, JavaScript, C/C++, Java, SQL, JSON, HTML, CSS, Bash, YAML, Markdown, Lua, and many more.</li>
<li><b>Code editing tools:</b> Undo/redo, multi-tab editing, brace matching, bookmarks, line duplication, line moving, comment toggling, whitespace cleanup, EOL conversion, and case conversion are built in.</li>
<li><b>Search:</b> Find, replace, regex search, find in files, go to line, bookmark navigation, and results panel navigation are integrated.</li>
<li><b>View controls:</b> Word wrap, fold/unfold, whitespace and EOL markers, indent guides, zoom, and full screen are available from the View menu.</li>
<li><b>Themes and fonts:</b> switch between Light, Dark, and Monokai from <b>Settings &gt; Theme</b>. The default editor fonts are tuned to be calmer and easier on the eyes across the app.</li>
</ul>

<h2>Built-In Tool Row</h2>
<h3>AI Assistant</h3>
<ul>
<li>Opens in a tab and works with local Ollama models.</li>
<li>Use it to explain code, find bugs, refactor, generate tests, add comments, translate, optimize, and draft docs.</li>
<li>The paperclip button attaches text files, images, PDFs, and Office documents as extra prompt context.</li>
<li>The microphone button supports speech-to-text when local recording and <code>whisper</code> CLI are available.</li>
<li>The <code>Reset</code> button clears the visible chat and the assistant session state.</li>
</ul>

<h3>Terminal</h3>
<ul>
<li>Opens a built-in terminal tab, typically rooted to the current file’s directory.</li>
<li>Useful for build commands, git, package managers, and quick shell work.</li>
</ul>

<h3>Compare</h3>
<ul>
<li>Open side-by-side file or tab comparisons from the Compare entry.</li>
<li>Navigation buttons move between differences, and <code>Recompare</code> recalculates the diff after changes.</li>
<li>Modified rows and character-level edits are highlighted separately so you can spot both changed lines and changed characters.</li>
<li>Use the lock toggle to unlock the panes, make fixes directly in compare mode, then press <code>Recompare</code>.</li>
<li>The overview bar gives you a quick visual map of where differences live across the file pair.</li>
</ul>

<h3>JSON / HTML / SQL / Brackets</h3>
<ul>
<li><b>JSON Tools:</b> format, minify, fix malformed JSON, compare before/after output, and optionally use local AI fixing.</li>
<li><b>HTML Tools:</b> format and clean up HTML-oriented content in a dedicated tool panel.</li>
<li><b>SQL Formatter:</b> uppercase/lowercase SQL formatting and before/after compare support.</li>
<li><b>Bracket Tools:</b> bracket matching and structural editing helpers for code cleanup.</li>
</ul>

<h3>REST and Git</h3>
<ul>
<li><b>REST Client:</b> run selected HTTP requests from editor content, especially <code>.http</code> style request blocks.</li>
<li><b>Git Integration:</b> inspect repository status, changed files, and work with repository context without leaving the editor.</li>
<li><b>Git gutter:</b> modified files show changed-line markers directly in the editor margin.</li>
</ul>

<h2>Tools, Utilities, and Panels</h2>
<ul>
<li><b>Markdown tools:</b> convert selections into lists, tables, headings, links, and code blocks.</li>
<li><b>Hex viewer:</b> open a saved file in hex view.</li>
<li><b>Hash tools:</b> MD5, SHA-1, SHA-256, and SHA-512 are available under <b>Tools</b>.</li>
<li><b>Base64 tools:</b> encode or decode selected text directly from the <b>Tools</b> menu.</li>
<li><b>Measurement tools:</b> <b>Tools &gt; Measurement</b> lets you turn document rulers and a crosshair pixel measure on or off when you need them.</li>
<li><b>Encoding tools:</b> UTF-8, BOM, UTF-16, legacy encodings, and EOL conversion are built into the menu bar workflow.</li>
<li><b>Macros:</b> record and replay editor actions from the <b>Macro</b> menu.</li>
<li><b>Plugins:</b> the Plugins menu also hosts the built-in formatter panels and any compatible user plugins you install.</li>
</ul>

<h2>Language and Lexer Behavior</h2>
<ul>
<li>Language detection follows file extensions where possible, but you can override it at any time from the <b>Language</b> menu.</li>
<li>If a file uses an uncommon extension, set the language manually once and work with the correct lexer, folding, and keyword highlighting immediately.</li>
<li>The same language and theme styling is intended to stay consistent across Linux, Windows, macOS, and Linux ARM builds.</li>
</ul>

<h2>Useful Shortcuts</h2>
<ul>
<li><code>Ctrl+N</code> new file, <code>Ctrl+O</code> open, <code>Ctrl+S</code> save, <code>Ctrl+W</code> close tab</li>
<li><code>Ctrl+F</code> find, <code>Ctrl+H</code> replace, <code>Ctrl+Shift+F</code> find in files, <code>Ctrl+G</code> go to line</li>
<li><code>Ctrl+Shift+A</code> AI Assistant, <code>Ctrl+`</code> Terminal, <code>Ctrl+Shift+R</code> REST Client, <code>Ctrl+Shift+E</code> Workspace</li>
<li><code>F11</code> full screen, <code>Ctrl+=</code> / <code>Ctrl+-</code> zoom, <code>Ctrl+0</code> reset zoom</li>
</ul>

<h2>Practical Tip</h2>
<p>If you are unsure where a feature lives, first check the top <b>?</b> menu for this guide and the shortcut list. Then use the icon row for the main workflows and the <b>Features</b>, <b>Tools</b>, and <b>Plugins</b> menus for the deeper editing and utility commands.</p>
</body>
</html>
)HTML").arg(notepatraUiCssFamily(), notepatraCodeCssFamily());
}

static void showRichHelpDialog(QWidget *parent, const QString &title, const QString &html) {
    auto *dialog = new QDialog(parent);
    dialog->setAttribute(Qt::WA_DeleteOnClose);
    dialog->setWindowTitle(title);
    dialog->resize(860, 720);

    auto *layout = new QVBoxLayout(dialog);
    layout->setContentsMargins(10, 10, 10, 10);
    layout->setSpacing(8);

    auto *browser = new QTextBrowser(dialog);
    browser->setOpenExternalLinks(true);
    browser->setHtml(html);
    browser->moveCursor(QTextCursor::Start);
    layout->addWidget(browser, 1);

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Close, dialog);
    QObject::connect(buttons, &QDialogButtonBox::rejected, dialog, &QDialog::close);
    QObject::connect(buttons, &QDialogButtonBox::accepted, dialog, &QDialog::close);
    layout->addWidget(buttons);

    dialog->show();
    dialog->raise();
    dialog->activateWindow();
}

MainWindow::MainWindow() {
    setWindowTitle("new 1 - Notepatra");
    setMinimumSize(640, 480);
    setAcceptDrops(true);  // Enable drag-and-drop

    // ── Central layout ──
    auto *central = new QWidget;
    auto *centralLayout = new QVBoxLayout(central);
    centralLayout->setContentsMargins(0, 0, 0, 0);
    centralLayout->setSpacing(0);

    // Vertical splitter: editor area (top) + search results (bottom)
    m_vertSplitter = new QSplitter(Qt::Vertical);
    centralLayout->addWidget(m_vertSplitter, 1);

    m_splitter = new QSplitter(Qt::Horizontal);
    m_vertSplitter->addWidget(m_splitter);

    // Search results panel — hidden until Find All is used
    m_searchResults = new SearchResultsPanel;
    m_searchResults->setVisible(false);
    m_searchResults->setMinimumHeight(80);
    m_vertSplitter->addWidget(m_searchResults);
    m_vertSplitter->setSizes({700, 0});

    // v0.1.45 — red ✕ in the panel header → hide it (history kept).
    connect(m_searchResults, &SearchResultsPanel::closeRequested, this, [this]() {
        m_searchResults->setVisible(false);
        m_vertSplitter->setSizes({700, 0});
    });

    // Double-click search result → jump to that line in editor
    connect(m_searchResults, &SearchResultsPanel::resultDoubleClicked, this, [this](const QString &file, int line) {
        // Find the tab with this file, or open it
        if (!file.isEmpty()) {
            bool found = false;
            for (int i = 0; i < m_tabs->count(); i++) {
                auto *ed = m_tabs->editorAt(i);
                if (ed && ed->filePath() == file) {
                    m_tabs->setCurrentIndex(i);
                    ed->gotoLine(line);
                    found = true;
                    break;
                }
            }
            if (!found) {
                openFile(file);
                if (auto *ed = m_tabs->currentEditor()) ed->gotoLine(line);
            }
        } else {
            if (auto *ed = m_tabs->currentEditor()) ed->gotoLine(line);
        }
        // After jumping, try to find and select the search term on that line
        if (auto *ed = m_tabs->currentEditor()) {
            // Get the search term from the find dialog
            if (m_findDialog && !m_findDialog->findInput()->currentText().isEmpty()) {
                QString term = m_findDialog->findInput()->currentText();
                int l, c; ed->getCursorPosition(&l, &c);
                ed->findFirst(term, false, false, false, false, true, l, 0);
            }
        }
    });

    // File explorer (hidden by default)
    m_explorer = new FileExplorer;
    m_explorer->setMinimumWidth(180);
    m_explorer->setMaximumWidth(400);
    m_explorer->setVisible(false);
    m_splitter->addWidget(m_explorer);

    connect(m_explorer, &FileExplorer::fileOpenRequested, this, &MainWindow::openFile);

    // Center: tabs
    m_tabs = new TabManager;
    m_splitter->addWidget(m_tabs);

    // Right: AI Assistant side-dock (hidden by default). Gives users the
    // Cursor / VS Code Copilot 3-column layout on demand:
    //    [ FileExplorer | EditorTabs | AIPanel (right dock) ]
    // Toggle via Tools → Dock AI Assistant on Right, or Ctrl+Alt+A.
    // The dock re-uses the same AIPanel widget class as the tab-based
    // AI Assistant — identical functionality, different surface.
    m_aiDockHost = new QWidget;
    m_aiDockHost->setMinimumWidth(320);
    // v0.1.42 — removed setMaximumWidth(640). The hard 640 cap stopped
    // users from dragging the QSplitter handle past that width on
    // Windows + macOS, where it presented as "manual resize doesn't
    // work" (the splitter would refuse to widen the dock further).
    // The QSplitter parent still enforces a sane minimum on the editor
    // pane, so removing the max here just lets users size the chat as
    // wide as their screen allows.
    auto *aiDockLayout = new QVBoxLayout(m_aiDockHost);
    aiDockLayout->setContentsMargins(0, 0, 0, 0);
    aiDockLayout->setSpacing(0);
    m_aiDockPanel = new AIPanel;
    aiDockLayout->addWidget(m_aiDockPanel);
    // Theme propagation — the panel re-renders its chat transcript and
    // chrome when the user flips Config::theme at runtime.
    connect(this, &MainWindow::themeChanged, m_aiDockPanel, &AIPanel::onThemeChanged);
    // Pull-based workspace awareness — the panel calls back into us right
    // before each Send so the model sees the freshest selection + tab list.
    m_aiDockPanel->setContextProvider([this](AIPanel *p) { populateAiContext(p); });
    connect(m_aiDockPanel, &AIPanel::insertText, this, [this](const QString &text) {
        if (auto *e = currentEditor()) e->insert(text);
    });
    connect(m_aiDockPanel, &AIPanel::replaceSelection, this, [this](const QString &text) {
        if (auto *e = currentEditor(); e && e->hasSelectedText()) e->replaceSelectedText(text);
    });
    // Coding Mode opens the file explorer (left). The AI chat this
    // checkbox lives in IS the coding chat — there's no separate tab
    // to open, the dock is already visible (that's where the toggle
    // is). Uncheck hides the explorer so the user gets a clean editor.
    connect(m_aiDockPanel, &AIPanel::codingModeRequested, this, [this](bool on) {
        if (m_explorer) m_explorer->setVisible(on);
    });

    // v0.1.39 — agentic write_file / apply_diff modified a file. If
    // the file is already open in a tab, reload the editor silently
    // (skip the "modified by another program" dialog — the user just
    // told the AI to do this). Otherwise, open it in a new tab.
    connect(m_aiDockPanel, &AIPanel::fileWrittenByAgent, this,
            [this](const QString &absPath, bool /*created*/) {
        if (absPath.isEmpty()) return;
        const QString canonical = QFileInfo(absPath).absoluteFilePath();
        for (int i = 0; i < m_tabs->count(); i++) {
            auto *ed = m_tabs->editorAt(i);
            if (ed && ed->filePath() == canonical) {
                // Already open — silent reload. Update the watcher's
                // timestamp first so its fileChanged dialog doesn't fire.
                if (m_fileWatcher && m_fileTimestamps.contains(canonical))
                    m_fileTimestamps[canonical] = QFileInfo(canonical).lastModified();
                ed->loadFile(canonical);
                if (m_fileWatcher) {
                    m_fileTimestamps[canonical] = QFileInfo(canonical).lastModified();
                    if (!m_fileWatcher->files().contains(canonical))
                        m_fileWatcher->addPath(canonical);
                }
                updateTabTitle(i);
                return;
            }
        }
        // Not open — open it in a new tab.
        openFile(canonical);
    });
    m_aiDockHost->setVisible(false);
    m_splitter->addWidget(m_aiDockHost);

    connect(m_tabs, &QTabWidget::tabCloseRequested, this, &MainWindow::closeTab);
    connect(m_tabs, &QTabWidget::currentChanged, this, [this](int) {
        if (m_macroRecording && m_macro) {
            m_macro->endRecording();
            m_savedMacro = m_macro->save();
            m_macroRecording = false;
            macroUpdateActions();
        }
        updateTitle();
        updateStatusBar();
        // v0.1.42 — refresh View menu checkmarks to mirror the new
        // active editor's actual state (whitespace / EOL / wrap /
        // indent guide). Pre-v0.1.42 these stuck on whatever the
        // previously-active tab happened to be in.
        syncViewMenuToActiveEditor();
    });
    connect(m_tabs, &TabManager::tabContextNew, this, [this]() { newFile(); });
    connect(m_tabs, &TabManager::tabContextClose, this, [this](int idx) { closeTab(idx); });
    connect(m_tabs, &TabManager::tabContextCloseAll, this, [this]() {
        while (m_tabs->count() > 0) closeTab(0);
    });
    connect(m_tabs, &TabManager::tabContextCloseOthers, this, [this](int keep) {
        for (int i = m_tabs->count() - 1; i >= 0; i--) if (i != keep) closeTab(i);
    });
    connect(m_tabs, &TabManager::tabContextCloseLeft, this, [this](int idx) {
        for (int i = idx - 1; i >= 0; i--) closeTab(i);
    });
    connect(m_tabs, &TabManager::tabContextCloseRight, this, [this](int idx) {
        for (int i = m_tabs->count() - 1; i > idx; i--) closeTab(i);
    });
    connect(m_tabs, &TabManager::tabContextSave, this, [this](int idx) {
        auto *ed = m_tabs->editorAt(idx);
        if (ed && !ed->filePath().isEmpty()) { ed->saveFile(); updateTabTitle(idx); }
        else saveFileAs();
    });
    connect(m_tabs, &TabManager::tabContextSaveAs, this, [this](int) { saveFileAs(); });
    connect(m_tabs, &TabManager::tabContextRename, this, [this](int idx) {
        auto *ed = m_tabs->editorAt(idx);
        if (!ed || ed->filePath().isEmpty()) return;
        bool ok;
        QString name = QInputDialog::getText(this, "Rename", "New filename:",
            QLineEdit::Normal, QFileInfo(ed->filePath()).fileName(), &ok);
        if (ok && !name.isEmpty()) {
            QString newPath = QFileInfo(ed->filePath()).dir().filePath(name);
            QFile::rename(ed->filePath(), newPath);
            ed->saveFile(newPath);
            m_tabs->setTabText(idx, name);
            m_tabs->setTabToolTip(idx, newPath);
            updateTitle();
        }
    });

    // Function list (hidden by default)
    m_funcList = new FunctionList;
    m_funcList->setMinimumWidth(180);
    m_funcList->setMaximumWidth(350);
    m_funcList->setVisible(false);
    m_splitter->addWidget(m_funcList);

    connect(m_funcList, &FunctionList::navigateRequested, this, [this](int line) {
        if (auto *e = currentEditor()) e->gotoLine(line);
    });

    // All features (Terminal, AI, Git, SQL Fmt, REST, Markdown) open as new tabs — no panels

    // Status bar
    m_statusBar = new NppStatusBar;
    centralLayout->addWidget(m_statusBar);

    setCentralWidget(central);

    // ── Menus, toolbar, shortcuts ──
    buildMenus();
    buildToolbar();
    setupShortcuts();

    // v0.1.42 — propagate Config to every editor + chrome at startup so
    // settings the user persisted in a previous session actually take
    // effect (font, tab width, word wrap, etc.). Pre-v0.1.42 these
    // were loaded into Config but never read by any code path.
    applyConfigEverywhere();

    // ── Status-bar click-through ──
    // Click the language / encoding / EOL indicators to open the
    // matching menu at cursor position — same UX as VS Code, Sublime,
    // Atom. findActionByPrefix-lookup resolves the existing menu
    // items so we don't have to duplicate the action logic here.
    connect(m_statusBar, &NppStatusBar::languageClicked, this, [this](const QPoint &g) {
        // Find the top-level "Language" menu via the menu bar
        for (QAction *act : menuBar()->actions()) {
            if (act->text().contains("Language", Qt::CaseInsensitive) && act->menu()) {
                act->menu()->popup(g);
                return;
            }
        }
    });
    connect(m_statusBar, &NppStatusBar::encodingClicked, this, [this](const QPoint &g) {
        for (QAction *act : menuBar()->actions()) {
            if (act->text().contains("Encoding", Qt::CaseInsensitive) && act->menu()) {
                act->menu()->popup(g);
                return;
            }
        }
    });
    connect(m_statusBar, &NppStatusBar::eolClicked, this, [this](const QPoint &g) {
        // EOL Conversion is a submenu inside Edit; look it up by name
        for (QAction *act : menuBar()->actions()) {
            if (act->text().contains("Edit", Qt::CaseInsensitive) && act->menu()) {
                for (QAction *sub : act->menu()->actions()) {
                    if (sub->text().contains("EOL", Qt::CaseInsensitive) && sub->menu()) {
                        sub->menu()->popup(g);
                        return;
                    }
                }
            }
        }
    });

    // Apply theme from saved config
    {
        // Apply the configured theme. "System" is resolved via
        // resolveTheme() to either Light or Dark based on the OS
        // preference, falling back to Light when nothing is detected.
        const QString themeName = Config::instance().theme;
        applyThemeToAll(resolveTheme(themeName));
    }

    // First-launch / no-file-to-restore welcome tab.
    //
    // On the first run, or any launch where the user has no session to
    // restore AND they haven't dismissed the Welcome tab via the
    // "Don't show again" checkbox, we open a Welcome page before the
    // blank editor. It's the user's first impression — without it, a
    // new user sees "new 1" and has no idea Notepatra has AI, Compare,
    // Git, JSON tools, etc.  With it, every feature is one click away.
    //
    // The Welcome tab is a real tab (closable, movable) and never
    // re-appears if it's still open from a previous session restore.
    if (Config::instance().showWelcomeOnStartup) {
        showWelcomeTab();
    } else {
        newFile();
    }

    // Restore window geometry from config
    {
        auto &cfg = Config::instance();
        if (cfg.maximized) {
            showMaximized();
        } else if (cfg.windowW > 100 && cfg.windowH > 100) {
            resize(cfg.windowW, cfg.windowH);
            if (cfg.windowX >= 0) move(cfg.windowX, cfg.windowY);
        } else {
            if (auto *screen = QApplication::primaryScreen()) {
                QRect avail = screen->availableGeometry();
                int w = avail.width() * 80 / 100;
                int h = avail.height() * 80 / 100;
                resize(w, h);
                move((avail.width() - w) / 2, (avail.height() - h) / 2);
            }
        }
    }

    // Check for crash recovery first
    checkCrashRecovery();

    // Restore previous session (open files from last time)
    restoreSession();

    // Optional check-on-startup — throttled to once per 24h via QSettings
    // so we don't hammer the GitHub API on every launch. User can disable
    // entirely by setting updates/checkOnStartup=false.
    QSettings settings("Notepatra", "Notepatra");
    bool checkOnStartup = settings.value("updates/checkOnStartup", true).toBool();
    if (checkOnStartup) {
        QDateTime last = settings.value("updates/lastCheck").toDateTime();
        if (!last.isValid() || last.secsTo(QDateTime::currentDateTime()) > 24 * 3600) {
            QTimer::singleShot(3000, this, [this]() {
                checkForUpdates(/*silent=*/true);
                QSettings s("Notepatra", "Notepatra");
                s.setValue("updates/lastCheck", QDateTime::currentDateTime());
            });
        }
    }

    // Auto-save session every 10 seconds + recovery every 30 seconds
    m_autoSaveTimer = new QTimer(this);
    connect(m_autoSaveTimer, &QTimer::timeout, this, [this]() {
        saveSession();
        autoSaveRecovery();
        checkFileChanges();
        // Persist window geometry + config
        auto &cfg = Config::instance();
        cfg.windowX = x(); cfg.windowY = y();
        cfg.windowW = width(); cfg.windowH = height();
        cfg.maximized = isMaximized();
        cfg.save();
    });
    m_autoSaveTimer->start(10000);  // every 10 seconds

    // File change watcher — detects external modifications
    setupFileWatcher();
}

Editor *MainWindow::currentEditor() {
    return m_tabs->currentEditor();
}

// ── File operations ──

Editor *MainWindow::newFile() {
    m_newCount++;
    auto *editor = new Editor(this);
    editor->applyTheme(Config::instance().theme);
    int idx = m_tabs->addTab(editor, QString("new %1").arg(m_newCount));
    m_tabs->setCurrentIndex(idx);

    connect(editor, &QsciScintilla::modificationChanged, this, [this, editor](bool) {
        updateTabTitle(m_tabs->indexOf(editor));
    });
    connect(editor, &Editor::cursorPositionUpdated, this, [this](int line, int col, int pos) {
        m_statusBar->updatePosition(line, col, pos);
    });
    connect(editor, &QsciScintilla::textChanged, this, [this]() {
        if (auto *e = currentEditor()) {
            m_statusBar->updateLines(e->lines());
            m_statusBar->updateLength(e->text().length());
            m_statusBar->updateWords(e->text().split(QRegularExpression("\\s+"), Qt::SkipEmptyParts).size());
        }
    });

    return editor;
}

void MainWindow::openFile(const QString &path) {
    if (path.isEmpty() || !QFileInfo(path).isFile()) return;

    // Check if already open
    for (int i = 0; i < m_tabs->count(); i++) {
        auto *ed = m_tabs->editorAt(i);
        if (ed && ed->filePath() == QFileInfo(path).absoluteFilePath()) {
            m_tabs->setCurrentIndex(i);
            return;
        }
    }

    auto *editor = new Editor(this);
    if (!editor->loadFile(path)) {
        delete editor;
        return;
    }
    editor->applyTheme(Config::instance().theme);

    int idx = m_tabs->addTab(editor, QFileInfo(path).fileName());
    m_tabs->setTabToolTip(idx, path);
    m_tabs->setCurrentIndex(idx);

    connect(editor, &QsciScintilla::modificationChanged, this, [this, editor](bool) {
        updateTabTitle(m_tabs->indexOf(editor));
    });
    connect(editor, &Editor::cursorPositionUpdated, this, [this](int line, int col, int pos) {
        m_statusBar->updatePosition(line, col, pos);
    });
    connect(editor, &QsciScintilla::textChanged, this, [this]() {
        if (auto *e = currentEditor()) {
            m_statusBar->updateLines(e->lines());
            m_statusBar->updateLength(e->text().length());
            m_statusBar->updateWords(e->text().split(QRegularExpression("\\s+"), Qt::SkipEmptyParts).size());
        }
    });

    updateTitle();
    updateStatusBar();

    // Watch this file for external changes
    QString absPath = QFileInfo(path).absoluteFilePath();
    if (m_fileWatcher) {
        m_fileWatcher->addPath(absPath);
        m_fileTimestamps[absPath] = QFileInfo(absPath).lastModified();
    }

    // Add to recent files
    Config::instance().addRecent(absPath);
    Config::instance().save();
    updateRecentMenu();
}

void MainWindow::handleRemoteOpen(const QStringList &paths, int gotoLine) {
    for (const QString &p : paths) openFile(p);
    if (gotoLine > 0) {
        if (auto *e = currentEditor()) e->gotoLine(gotoLine);
    }
    // Un-minimize if needed, then bring to front. On Windows this is the
    // only reliable way to steal focus from the shell that just launched us.
    if (isMinimized()) showNormal();
    else show();
    raise();
    activateWindow();
}

void MainWindow::saveFile() {
    auto *e = currentEditor();
    if (!e) return;
    if (!e->filePath().isEmpty()) {
        e->saveFile();
        updateTabTitle(m_tabs->currentIndex());
        // Auto-refresh git gutter on save
        e->updateGitGutter();
        // Update file timestamp so watcher doesn't trigger
        if (m_fileWatcher) {
            QString path = e->filePath();
            m_fileTimestamps[path] = QFileInfo(path).lastModified();
        }
    } else {
        saveFileAs();
    }
}

void MainWindow::saveFileAs() {
    auto *e = currentEditor();
    if (!e) return;
    QString path = QFileDialog::getSaveFileName(this, "Save As", QDir::homePath(), "All Files (*)");
    if (!path.isEmpty()) {
        e->saveFile(path);
        m_tabs->setTabText(m_tabs->currentIndex(), QFileInfo(path).fileName());
        m_tabs->setTabToolTip(m_tabs->currentIndex(), path);
        updateTitle();
    }
}

void MainWindow::closeTab(int index) {
    QWidget *widget = m_tabs->widget(index);
    if (!widget) return;

    // If it's an editor, check for unsaved changes
    auto *editor = qobject_cast<Editor *>(widget);
    if (editor && editor->isModified()) {
        QString name = m_tabs->tabText(index).remove(" *");
        auto result = QMessageBox::question(this, "Save",
            QString("Save changes to %1?").arg(name),
            QMessageBox::Save | QMessageBox::Discard | QMessageBox::Cancel);

        if (result == QMessageBox::Save) {
            if (!editor->filePath().isEmpty())
                editor->saveFile();
            else {
                QString path = QFileDialog::getSaveFileName(this, "Save File");
                if (!path.isEmpty()) editor->saveFile(path);
                else return;
            }
        } else if (result == QMessageBox::Cancel) {
            return;
        }
    }

    // Remove file from watcher if it's an editor
    if (editor && !editor->filePath().isEmpty() && m_fileWatcher) {
        m_fileWatcher->removePath(editor->filePath());
        m_fileTimestamps.remove(editor->filePath());
    }

    m_tabs->removeTab(index);
    delete widget;
    if (m_tabs->count() == 0) newFile();
}

// ── UI updates ──

void MainWindow::updateTitle() {
    auto *e = currentEditor();
    if (e && !e->filePath().isEmpty())
        setWindowTitle(e->filePath() + " - Notepatra");
    else if (e)
        setWindowTitle(m_tabs->tabText(m_tabs->currentIndex()) + " - Notepatra");
    else
        setWindowTitle("Notepatra");
}

void MainWindow::updateStatusBar() {
    auto *e = currentEditor();
    if (!e) return;
    int line, col;
    e->getCursorPosition(&line, &col);
    int pos = static_cast<int>(e->SendScintilla(QsciScintilla::SCI_GETCURRENTPOS));
    m_statusBar->updatePosition(line + 1, col + 1, pos);
    m_statusBar->updateLanguage(e->language());
    m_statusBar->updateEncoding(e->encoding());
    m_statusBar->updateEol(e->eolModeName());
    m_statusBar->updateLines(e->lines());
    m_statusBar->updateLength(e->text().length());
            m_statusBar->updateWords(e->text().split(QRegularExpression("\\s+"), Qt::SkipEmptyParts).size());
}

void MainWindow::updateTabTitle(int index) {
    if (index < 0) return;
    auto *e = m_tabs->editorAt(index);
    if (!e) return;
    QString name = e->filePath().isEmpty()
                       ? QString("new %1").arg(index + 1)
                       : QFileInfo(e->filePath()).fileName();
    if (e->isModified()) name += " *";
    m_tabs->setTabText(index, name);
    updateTitle();
}

// ── Menus ──

void MainWindow::buildMenus() {
    auto *mb = menuBar();
    auto E = [this]() -> Editor* { return currentEditor(); };
    auto FD = [this]() -> FindReplaceDialog* {
        if (!m_findDialog) m_findDialog = new FindReplaceDialog(this);
        return m_findDialog;
    };

    // ═══ FILE ═══
    auto *file = mb->addMenu("&File");
    file->addAction("&New", this, [this]() { newFile(); }, QKeySequence("Ctrl+N"));
    file->addAction("&Open...", this, [this]() {
        for (const auto &p : QFileDialog::getOpenFileNames(this, "Open", QDir::homePath(), "All Files (*)"))
            openFile(p);
    }, QKeySequence("Ctrl+O"));
    file->addAction("Open Folder as Workspace...", this, [this]() {
        QString p = QFileDialog::getExistingDirectory(this, "Open Folder", QDir::homePath());
        if (!p.isEmpty()) { m_explorer->setRoot(p); m_explorer->setVisible(true); }
    });
    file->addAction("Reload from Disk", this, [E]() {
        if (auto *e = E(); e && !e->filePath().isEmpty()) e->loadFile(e->filePath());
    });
    file->addSeparator();
    file->addAction("&Save", this, [this]() { saveFile(); }, QKeySequence("Ctrl+S"));
    file->addAction("Save &As...", this, [this]() { saveFileAs(); }, QKeySequence("Ctrl+Shift+S"));
    file->addAction("Save a Copy As...", this, [E]() {
        if (auto *e = E()) {
            QString p = QFileDialog::getSaveFileName(nullptr, "Save a Copy As", QDir::homePath());
            if (!p.isEmpty()) { QFile f(p); if (f.open(QIODevice::WriteOnly)) { f.write(e->text().toUtf8()); } }
        }
    });
    file->addAction("Save All", this, [this]() {
        for (int i = 0; i < m_tabs->count(); i++) {
            auto *ed = m_tabs->editorAt(i);
            if (ed && ed->isModified() && !ed->filePath().isEmpty()) { ed->saveFile(); updateTabTitle(i); }
        }
    }, QKeySequence("Ctrl+Alt+S"));
    file->addSeparator();
    file->addAction("Rename...", this, [this, E]() {
        auto *e = E(); if (!e || e->filePath().isEmpty()) return;
        bool ok; QString name = QInputDialog::getText(this, "Rename", "New name:", QLineEdit::Normal,
            QFileInfo(e->filePath()).fileName(), &ok);
        if (ok && !name.isEmpty()) {
            QString newPath = QFileInfo(e->filePath()).dir().filePath(name);
            QFile::rename(e->filePath(), newPath);
            e->saveFile(newPath);
            m_tabs->setTabText(m_tabs->currentIndex(), name);
            updateTitle();
        }
    });
    file->addSeparator();
    file->addAction("&Close", this, [this]() { closeTab(m_tabs->currentIndex()); }, QKeySequence("Ctrl+W"));
    file->addAction("Close All", this, [this]() { while (m_tabs->count() > 0) closeTab(0); });
    file->addAction("Close All BUT This", this, [this]() {
        int cur = m_tabs->currentIndex();
        for (int i = m_tabs->count()-1; i >= 0; i--) if (i != cur) closeTab(i);
    });
    file->addAction("Close All to the Left", this, [this]() {
        int cur = m_tabs->currentIndex();
        for (int i = cur-1; i >= 0; i--) closeTab(i);
    });
    file->addAction("Close All to the Right", this, [this]() {
        int cur = m_tabs->currentIndex();
        for (int i = m_tabs->count()-1; i > cur; i--) closeTab(i);
    });
    file->addSeparator();
    file->addAction("&Print...", this, [E]() {
        if (auto *e = E()) {
            QPrinter printer; QPrintDialog dlg(&printer);
            if (dlg.exec() == QPrintDialog::Accepted) {
                QTextDocument doc(e->text());
                doc.print(&printer);
            }
        }
    }, QKeySequence("Ctrl+P"));
    file->addSeparator();
    m_recentMenu = file->addMenu("Recent &Files");
    updateRecentMenu();
    file->addSeparator();
    file->addAction("E&xit", this, &QMainWindow::close, QKeySequence("Alt+F4"));

    // ═══ EDIT ═══
    auto *edit = mb->addMenu("&Edit");
    edit->addAction("&Undo", this, [E]() { if (auto *e = E()) e->undo(); }, QKeySequence("Ctrl+Z"));
    edit->addAction("&Redo", this, [E]() { if (auto *e = E()) e->redo(); }, QKeySequence("Ctrl+Y"));
    edit->addSeparator();
    edit->addAction("Cu&t", this, [E]() { if (auto *e = E()) e->cut(); }, QKeySequence("Ctrl+X"));
    edit->addAction("&Copy", this, [E]() { if (auto *e = E()) e->copy(); }, QKeySequence("Ctrl+C"));
    edit->addAction("&Paste", this, [E]() { if (auto *e = E()) e->paste(); }, QKeySequence("Ctrl+V"));
    edit->addAction("&Delete", this, [E]() { if (auto *e = E(); e && e->hasSelectedText()) e->removeSelectedText(); });
    edit->addAction("Select &All", this, [E]() { if (auto *e = E()) e->selectAll(); }, QKeySequence("Ctrl+A"));
    edit->addSeparator();

    auto *copyClip = edit->addMenu("Copy to Clipboard");
    copyClip->addAction("Copy Full Path", this, [E]() {
        if (auto *e = E(); e && !e->filePath().isEmpty()) QApplication::clipboard()->setText(e->filePath());
    });
    copyClip->addAction("Copy Filename", this, [E]() {
        if (auto *e = E(); e && !e->filePath().isEmpty()) QApplication::clipboard()->setText(QFileInfo(e->filePath()).fileName());
    });
    copyClip->addAction("Copy Directory", this, [E]() {
        if (auto *e = E(); e && !e->filePath().isEmpty()) QApplication::clipboard()->setText(QFileInfo(e->filePath()).path());
    });
    edit->addSeparator();

    // Case
    auto *caseMenu = edit->addMenu("Convert Case to");
    caseMenu->addAction("&UPPERCASE", this, [E]() {
        if (auto *e = E(); e && e->hasSelectedText()) e->replaceSelectedText(RustCore::convertCase(e->selectedText(), 0));
    }, QKeySequence("Ctrl+Shift+U"));
    caseMenu->addAction("&lowercase", this, [E]() {
        if (auto *e = E(); e && e->hasSelectedText()) e->replaceSelectedText(RustCore::convertCase(e->selectedText(), 1));
    }, QKeySequence("Ctrl+U"));
    caseMenu->addAction("&Proper Case", this, [E]() {
        if (auto *e = E(); e && e->hasSelectedText()) e->replaceSelectedText(RustCore::convertCase(e->selectedText(), 2));
    });
    caseMenu->addAction("&Sentence case", this, [E]() {
        if (auto *e = E(); e && e->hasSelectedText()) e->replaceSelectedText(RustCore::convertCase(e->selectedText(), 3));
    });
    caseMenu->addAction("&iNVERT cASE", this, [E]() {
        if (auto *e = E(); e && e->hasSelectedText()) e->replaceSelectedText(RustCore::convertCase(e->selectedText(), 4));
    });

    // Line operations
    auto *lineOps = edit->addMenu("Line Opera&tions");
    lineOps->addAction("&Duplicate Current Line", this, [E]() { if (auto *e = E()) e->duplicateLine(); }, QKeySequence("Ctrl+D"));
    lineOps->addAction("D&elete Current Line", this, [E]() { if (auto *e = E()) e->deleteLine(); }, QKeySequence("Ctrl+Shift+K"));
    lineOps->addAction("Move Line &Up", this, [E]() { if (auto *e = E()) e->moveLineUp(); }, QKeySequence("Ctrl+Shift+Up"));
    lineOps->addAction("Move Line &Down", this, [E]() { if (auto *e = E()) e->moveLineDown(); }, QKeySequence("Ctrl+Shift+Down"));
    lineOps->addSeparator();
    lineOps->addAction("Sort Lexicographically &Ascending", this, [E]() {
        if (auto *e = E()) { e->selectAll(); e->replaceSelectedText(RustCore::sortLines(e->text(), 0)); }
    });
    lineOps->addAction("Sort Lexicographically &Descending", this, [E]() {
        if (auto *e = E()) { e->selectAll(); e->replaceSelectedText(RustCore::sortLines(e->text(), 1)); }
    });
    lineOps->addAction("Sort as &Integers Ascending", this, [E]() {
        if (auto *e = E()) { e->selectAll(); e->replaceSelectedText(RustCore::sortLines(e->text(), 2)); }
    });
    lineOps->addAction("Sort as Integers Descending", this, [E]() {
        if (auto *e = E()) { e->selectAll(); e->replaceSelectedText(RustCore::sortLines(e->text(), 3)); }
    });
    lineOps->addAction("Sort by &Length Ascending", this, [E]() {
        if (auto *e = E()) { e->selectAll(); e->replaceSelectedText(RustCore::sortLines(e->text(), 4)); }
    });
    lineOps->addAction("Sort by Length Descending", this, [E]() {
        if (auto *e = E()) { e->selectAll(); e->replaceSelectedText(RustCore::sortLines(e->text(), 5)); }
    });
    lineOps->addSeparator();
    lineOps->addAction("&Remove Duplicate Lines", this, [E]() {
        if (auto *e = E()) { e->selectAll(); e->replaceSelectedText(RustCore::removeDuplicates(e->text(), 0)); }
    });
    lineOps->addAction("Remove Consecutive Duplicate Lines", this, [E]() {
        if (auto *e = E()) { e->selectAll(); e->replaceSelectedText(RustCore::removeDuplicates(e->text(), 1)); }
    });
    lineOps->addAction("Remove Empty Lines", this, [E]() {
        if (auto *e = E()) { e->selectAll(); e->replaceSelectedText(RustCore::removeEmptyLines(e->text(), 0)); }
    });
    lineOps->addAction("Remove Blank Lines", this, [E]() {
        if (auto *e = E()) { e->selectAll(); e->replaceSelectedText(RustCore::removeEmptyLines(e->text(), 1)); }
    });
    lineOps->addSeparator();
    lineOps->addAction("&Join Lines", this, [E]() {
        if (auto *e = E(); e && e->hasSelectedText()) e->replaceSelectedText(RustCore::joinLines(e->selectedText(), " "));
    });
    lineOps->addAction("&Reverse Line Order", this, [E]() {
        if (auto *e = E()) { e->selectAll(); e->replaceSelectedText(RustCore::reverseLines(e->text())); }
    });

    // Comment / Uncomment — v0.1.44 added Toggle Block Comment + Notepad++
    // shortcuts (Ctrl+Q / Ctrl+Shift+Q). Both menu items are always present;
    // they no-op silently when the active language has no syntax for that
    // kind of comment (right-click context menu greys them out instead).
    auto *commentMenu = edit->addMenu("Comment/Uncomment");
    auto *lineCommentAct = commentMenu->addAction("Toggle &Line Comment", this,
        [E]() { if (auto *e = E()) e->toggleComment(); });
    lineCommentAct->setShortcuts({QKeySequence("Ctrl+/"), QKeySequence("Ctrl+Q")});
    auto *blockCommentAct = commentMenu->addAction("Toggle &Block Comment", this,
        [E]() { if (auto *e = E()) e->toggleBlockComment(); });
    blockCommentAct->setShortcut(QKeySequence("Ctrl+Shift+Q"));
    // v0.1.45 — explicit Comment / Uncomment per kind, NPP-style.
    // Ctrl+K + Ctrl+Shift+K match Notepad++'s defaults.
    commentMenu->addSeparator();
    auto *commentLineAct = commentMenu->addAction("&Comment Line", this,
        [E]() { if (auto *e = E()) e->commentLine(); });
    commentLineAct->setShortcut(QKeySequence("Ctrl+K"));
    auto *uncommentLineAct = commentMenu->addAction("&Uncomment Line", this,
        [E]() { if (auto *e = E()) e->uncommentLine(); });
    uncommentLineAct->setShortcut(QKeySequence("Ctrl+Shift+K"));
    commentMenu->addAction("Co&mment Block", this,
        [E]() { if (auto *e = E()) e->commentBlock(); });
    commentMenu->addAction("Un&comment Block", this,
        [E]() { if (auto *e = E()) e->uncommentBlock(); });

    // Blank operations
    auto *blankMenu = edit->addMenu("Blank Operations");
    blankMenu->addAction("Trim &Trailing Space", this, [E]() {
        if (auto *e = E()) { e->selectAll(); e->replaceSelectedText(RustCore::trimLines(e->text(), 0)); }
    });
    blankMenu->addAction("Trim &Leading Space", this, [E]() {
        if (auto *e = E()) { e->selectAll(); e->replaceSelectedText(RustCore::trimLines(e->text(), 1)); }
    });
    blankMenu->addAction("Trim &Both", this, [E]() {
        if (auto *e = E()) { e->selectAll(); e->replaceSelectedText(RustCore::trimLines(e->text(), 2)); }
    });
    blankMenu->addSeparator();
    blankMenu->addAction("TAB to &Space", this, [E]() {
        if (auto *e = E()) { e->selectAll(); e->replaceSelectedText(RustCore::convertWhitespace(e->text(), e->tabWidth(), 0)); }
    });
    blankMenu->addAction("Space to TA&B", this, [E]() {
        if (auto *e = E()) { e->selectAll(); e->replaceSelectedText(RustCore::convertWhitespace(e->text(), e->tabWidth(), 1)); }
    });

    // EOL conversion
    // v0.1.42 — uses Editor::setEolModeByName so the status-bar EOL pill
    // refreshes via the eolModeChanged signal (was stuck on the original
    // EOL after conversion pre-v0.1.42 because the menu only updated
    // QsciScintilla but not Editor::m_eolName).
    auto *eolMenu = edit->addMenu("EOL Conversion");
    eolMenu->addAction("Windows (CR LF)", this, [this, E]() {
        if (auto *e = E()) { e->setEolModeByName("Windows (CR LF)", true); m_statusBar->updateEol("Windows (CR LF)"); }
    });
    eolMenu->addAction("Unix (LF)", this, [this, E]() {
        if (auto *e = E()) { e->setEolModeByName("Unix (LF)", true); m_statusBar->updateEol("Unix (LF)"); }
    });
    eolMenu->addAction("Macintosh (CR)", this, [this, E]() {
        if (auto *e = E()) { e->setEolModeByName("Macintosh (CR)", true); m_statusBar->updateEol("Macintosh (CR)"); }
    });

    // ── Insert Date / Time (Notepad++ parity) ──
    // Notepad++ has these under Edit → Insert. Users reach for them often
    // in logs/changelogs/readmes. Three formats cover the common cases.
    edit->addSeparator();
    auto *insertMenu = edit->addMenu("Insert");
    insertMenu->addAction("Date &Time (short)  — 2026-04-20 13:45", this, [E]() {
        if (auto *e = E()) e->insert(QDateTime::currentDateTime().toString("yyyy-MM-dd hh:mm"));
    }, QKeySequence("Ctrl+F5"));
    insertMenu->addAction("&Date (yyyy-MM-dd)", this, [E]() {
        if (auto *e = E()) e->insert(QDate::currentDate().toString("yyyy-MM-dd"));
    });
    insertMenu->addAction("Date (long)  — April 20, 2026", this, [E]() {
        if (auto *e = E()) e->insert(QDate::currentDate().toString("MMMM d, yyyy"));
    });
    insertMenu->addAction("T&ime (HH:mm:ss)", this, [E]() {
        if (auto *e = E()) e->insert(QTime::currentTime().toString("hh:mm:ss"));
    });
    insertMenu->addAction("ISO 8601 timestamp", this, [E]() {
        if (auto *e = E()) e->insert(QDateTime::currentDateTime().toString(Qt::ISODate));
    });

    // ═══ SEARCH ═══
    auto *search = mb->addMenu("&Search");
    search->addAction("&Find...", this, [FD]() { FD()->showFind(); }, QKeySequence("Ctrl+F"));
    search->addAction("Find in Files...", this, [FD]() { FD()->showFindInFiles(); }, QKeySequence("Ctrl+Shift+F"));
    search->addAction("Find &Next", this, [this, FD]() {
        if (m_findDialog && !m_findDialog->findInput()->currentText().isEmpty()) m_findDialog->findNext();
        else FD()->showFind();
    }, QKeySequence("F3"));
    search->addAction("Find &Previous", this, [this, FD]() {
        if (m_findDialog && !m_findDialog->findInput()->currentText().isEmpty()) m_findDialog->findPrevious();
        else FD()->showFind();
    }, QKeySequence("Shift+F3"));
    search->addSeparator();
    search->addAction("&Replace...", this, [FD]() { FD()->showReplace(); }, QKeySequence("Ctrl+H"));
    search->addSeparator();
    search->addAction("&Go to Line...", this, [FD]() { FD()->showGoto(); }, QKeySequence("Ctrl+G"));
    search->addAction("Go to Matching &Brace", this, [this, E]() {
        // Try our Editor first
        if (auto *e = E()) { e->goToMatchingBrace(); return; }
        // Try any focused QsciScintilla (formatter panels etc)
        auto *focused = QApplication::focusWidget();
        auto *sci = qobject_cast<QsciScintilla *>(focused);
        if (!sci) return;
        int pos = (int)sci->SendScintilla(QsciScintilla::SCI_GETCURRENTPOS);
        auto isBrace = [](int c) { return c=='('||c==')'||c=='['||c==']'||c=='{'||c=='}'||c=='<'||c=='>'; };
        int bp = -1;
        int ch = (int)sci->SendScintilla(QsciScintilla::SCI_GETCHARAT, (unsigned long)pos, (long)0);
        if (isBrace(ch)) bp = pos;
        else if (pos > 0) { ch = (int)sci->SendScintilla(QsciScintilla::SCI_GETCHARAT, (unsigned long)(pos-1), (long)0); if (isBrace(ch)) bp = pos-1; }
        if (bp < 0) return;
        int mp = (int)sci->SendScintilla(QsciScintilla::SCI_BRACEMATCH, (unsigned long)bp, (long)0);
        if (mp >= 0) {
            sci->SendScintilla(QsciScintilla::SCI_BRACEHIGHLIGHT, (unsigned long)bp, (long)mp);
            sci->SendScintilla(QsciScintilla::SCI_GOTOPOS, (unsigned long)mp);
        } else {
            sci->SendScintilla(QsciScintilla::SCI_BRACEBADLIGHT, (unsigned long)bp);
        }
    }, QKeySequence("Ctrl+B"));
    search->addSeparator();

    auto *bmMenu = search->addMenu("Bookmarks");
    bmMenu->addAction("Toggle Bookmark", this, [E]() {
        if (auto *e = E()) { int l, c; e->getCursorPosition(&l, &c);
            if (e->markersAtLine(l) & 1) e->markerDelete(l, 0); else e->markerAdd(l, 0); }
    }, QKeySequence("Ctrl+F2"));
    bmMenu->addAction("Next Bookmark", this, [E]() {
        if (auto *e = E()) { int l, c; e->getCursorPosition(&l, &c);
            int n = e->markerFindNext(l+1, 1); if (n < 0) n = e->markerFindNext(0, 1);
            if (n >= 0) e->gotoLine(n+1); }
    }, QKeySequence("F2"));
    bmMenu->addAction("Previous Bookmark", this, [E]() {
        if (auto *e = E()) { int l, c; e->getCursorPosition(&l, &c);
            int n = e->markerFindPrevious(l-1, 1); if (n < 0) n = e->markerFindPrevious(e->lines()-1, 1);
            if (n >= 0) e->gotoLine(n+1); }
    }, QKeySequence("Shift+F2"));
    bmMenu->addAction("Clear All Bookmarks", this, [E]() { if (auto *e = E()) e->markerDeleteAll(0); });

    // ═══ VIEW ═══
    auto *view = mb->addMenu("&View");
    view->addAction("Always on Top", this, [this]() {
        auto flags = windowFlags();
        setWindowFlags(flags ^ Qt::WindowStaysOnTopHint);
        show();
    })->setCheckable(true);
    view->addAction("Toggle Full-Screen Mode", this, [this]() {
        isFullScreen() ? showNormal() : showFullScreen();
    }, QKeySequence("F11"));
    view->addSeparator();

    // v0.1.42 — every View toggle now: (1) actually applies to the editor,
    // (2) reflects the editor's TRUE state via setChecked, (3) persists
    // its sticky-state config field, (4) propagates to all open tabs.
    // QActions are tagged with objectName so syncViewMenuToActiveEditor()
    // can refresh them on tab switch.
    auto *symMenu = view->addMenu("Show Symbol");
    auto *actShowAll = symMenu->addAction("Show All Characters");
    actShowAll->setObjectName("viewShowAllCharacters");
    actShowAll->setCheckable(true);
    QObject::connect(actShowAll, &QAction::toggled, this, [this](bool on) {
        for (int i = 0; i < m_tabs->count(); ++i) {
            if (auto *e = m_tabs->editorAt(i)) {
                e->setWhitespaceVisibility(on ? QsciScintilla::WsVisible
                                              : QsciScintilla::WsInvisible);
                e->setEolVisibility(on);
            }
        }
    });
    symMenu->addSeparator();

    auto *actShowWs = symMenu->addAction("Show Whitespace and TAB");
    actShowWs->setObjectName("viewShowWhitespace");
    actShowWs->setCheckable(true);
    QObject::connect(actShowWs, &QAction::toggled, this, [this](bool on) {
        for (int i = 0; i < m_tabs->count(); ++i) {
            if (auto *e = m_tabs->editorAt(i))
                e->setWhitespaceVisibility(on ? QsciScintilla::WsVisible
                                              : QsciScintilla::WsInvisible);
        }
    });

    auto *actShowEol = symMenu->addAction("Show End of Line");
    actShowEol->setObjectName("viewShowEol");
    actShowEol->setCheckable(true);
    QObject::connect(actShowEol, &QAction::toggled, this, [this](bool on) {
        for (int i = 0; i < m_tabs->count(); ++i) {
            if (auto *e = m_tabs->editorAt(i)) e->setEolVisibility(on);
        }
    });

    auto *actShowIndent = symMenu->addAction("Show Indent Guide");
    actShowIndent->setObjectName("viewShowIndentGuide");
    actShowIndent->setCheckable(true);
    actShowIndent->setChecked(Config::instance().showIndentGuides);
    QObject::connect(actShowIndent, &QAction::toggled, this, [this](bool on) {
        auto &cfg = Config::instance();
        cfg.showIndentGuides = on;
        cfg.save();
        for (int i = 0; i < m_tabs->count(); ++i) {
            if (auto *e = m_tabs->editorAt(i)) e->setIndentationGuides(on);
        }
    });

    auto *zoomMenu = view->addMenu("Zoom");
    // v0.1.42 — zoom now persists to Config::fontSize via Editor's helpers
    // and propagates to every open tab through applyConfig().
    zoomMenu->addAction("Zoom In", this, [this]() {
        Config::instance().fontSize = qBound(6, Config::instance().fontSize + 1, 48);
        Config::instance().save();
        applyConfigEverywhere();
    }, QKeySequence("Ctrl+="));
    zoomMenu->addAction("Zoom Out", this, [this]() {
        Config::instance().fontSize = qBound(6, Config::instance().fontSize - 1, 48);
        Config::instance().save();
        applyConfigEverywhere();
    }, QKeySequence("Ctrl+-"));
    zoomMenu->addAction("Restore Default Zoom", this, [this]() {
        Config::instance().fontSize = 11;
        Config::instance().save();
        applyConfigEverywhere();
    }, QKeySequence("Ctrl+0"));

    view->addSeparator();
    auto *actWordWrap = view->addAction("Word Wrap");
    actWordWrap->setObjectName("viewWordWrap");
    actWordWrap->setCheckable(true);
    actWordWrap->setChecked(Config::instance().wordWrap);
    QObject::connect(actWordWrap, &QAction::toggled, this, [this](bool on) {
        auto &cfg = Config::instance();
        cfg.wordWrap = on;
        cfg.save();
        for (int i = 0; i < m_tabs->count(); ++i) {
            if (auto *e = m_tabs->editorAt(i))
                e->setWrapMode(on ? QsciScintilla::WrapWord : QsciScintilla::WrapNone);
        }
    });
    view->addSeparator();

    auto *foldMenu = view->addMenu("Fold All");
    foldMenu->addAction("Fold All", this, [E]() { if (auto *e = E()) e->foldAll(); }, QKeySequence("Alt+0"));
    foldMenu->addAction("Unfold All", this, [E]() { if (auto *e = E()) e->clearFolds(); }, QKeySequence("Alt+Shift+0"));

    view->addSeparator();
    view->addAction("Summary...", this, [E, this]() {
        auto *e = E(); if (!e) return;
        QMessageBox::information(this, "Summary",
            QString("Path: %1\nLines: %2\nLength: %3\nLanguage: %4\nEncoding: %5\nEOL: %6")
            .arg(e->filePath().isEmpty() ? "(unsaved)" : e->filePath())
            .arg(e->lines()).arg(e->text().length())
            .arg(e->language()).arg(e->encoding()).arg(e->eolModeName()));
    });
    view->addSeparator();

    auto *explorerAct = view->addAction("Folder as Workspace");
    explorerAct->setCheckable(true);
    explorerAct->setShortcut(QKeySequence("Ctrl+Shift+E"));
    connect(explorerAct, &QAction::triggered, this, [this, explorerAct]() {
        m_explorer->setVisible(!m_explorer->isVisible());
        explorerAct->setChecked(m_explorer->isVisible());
    });

    auto *funcAct = view->addAction("Function List");
    funcAct->setCheckable(true);
    connect(funcAct, &QAction::triggered, this, [this, funcAct]() {
        m_funcList->setVisible(!m_funcList->isVisible());
        funcAct->setChecked(m_funcList->isVisible());
        if (m_funcList->isVisible()) if (auto *e = currentEditor()) m_funcList->updateSymbols(e->text(), e->language());
    });

    // ═══ TOOLS ═══
    // Primary power-tools menu. Ordered top-down by a marketing-first
    // hierarchy so the first thing users see is AI:
    //   1. ══ AI ══                (AI Assistant — the headline feature)
    //   2. ══ Search ══             (Project Search)
    //   3. ══ Workflow ══           (Terminal, Compare, Markdown)
    //   4. ══ Formatters ══         (JSON, HTML, SQL, Bracket)
    //   5. ══ Integrations ══       (REST Client, Git)
    // Section headers are rendered via disabled actions with unicode
    // horizontal lines so the groups visually separate.
    auto *feat = mb->addMenu("&Tools");

    auto sectionHeader = [feat](const QString &title) {
        auto *s = feat->addAction("── " + title + " ──────");
        s->setEnabled(false);
        QFont f = s->font(); f.setWeight(QFont::DemiBold); f.setPointSize(f.pointSize() - 1);
        s->setFont(f);
        return s;
    };

    sectionHeader("AI");

    // --- Dock AI on the right (Cursor-style 3-column layout) ---
    auto *aiDockAct = feat->addAction("Dock AI Assistant on Right    Ctrl+Alt+A");
    aiDockAct->setCheckable(true);
    aiDockAct->setShortcut(QKeySequence("Ctrl+Alt+A"));
    aiDockAct->setStatusTip("Toggle the AI Assistant as a right-side panel — "
                            "3-column layout with file tree on left, editor in middle.");
    connect(aiDockAct, &QAction::triggered, this, [this, aiDockAct]() {
        toggleAiDock();
        if (aiDockAct) aiDockAct->setChecked(m_aiDockHost && m_aiDockHost->isVisible());
    });

    // --- AI Assistant ---
    // AI Assistant — toggles the persistent right-side dock instead of
    // spawning a new editor tab. One chat, always in the same place, so
    // switching between files doesn't reset the conversation. Matches
    // the Cursor / VS Code layout the user asked for.
    auto *aiAct = feat->addAction("AI Assistant      Ctrl+Shift+A");
    aiAct->setCheckable(true);
    aiAct->setShortcut(QKeySequence("Ctrl+Shift+A"));
    aiAct->setStatusTip("Toggle the AI Assistant dock (right side). Persistent chat that sees all your open files + workspace. Configure backends in Settings → Preferences → AI.");
    connect(aiAct, &QAction::triggered, this, [this, aiAct]() {
        // toggleAiDock also seeds fresh workspace context into the dock,
        // so the user can send immediately after opening.
        toggleAiDock();
        aiAct->setChecked(m_aiDockHost && m_aiDockHost->isVisible());
    });

    feat->addSeparator();
    sectionHeader("Search");

    // --- Project Search — fast folder-wide file-name + content search ---
    auto *projectSearchAct = feat->addAction("Project Search      Ctrl+Shift+G");
    projectSearchAct->setShortcut(QKeySequence("Ctrl+Shift+G"));
    projectSearchAct->setStatusTip("Recursively search file names AND file contents across a folder tree, streamed to a clickable tree.");
    connect(projectSearchAct, &QAction::triggered, this, [this, E]() {
        auto *ps = new ProjectSearch;
        // Theme propagation — panel re-applies psearchPalette() styles
        // when the user flips themes.
        connect(this, &MainWindow::themeChanged, ps, &ProjectSearch::onThemeChanged);
        // Folder cascade: current file's folder → file-explorer workspace root
        // → $HOME as the last resort. Walking $HOME is ~always wrong (the
        // walker sees millions of files), so we prefer a narrower default
        // whenever one is available.
        QString defaultFolder;
        if (auto *e = E(); e && !e->filePath().isEmpty())
            defaultFolder = QFileInfo(e->filePath()).path();
        else if (m_explorer && !m_explorer->rootPath().isEmpty())
            defaultFolder = m_explorer->rootPath();
        if (!defaultFolder.isEmpty())
            ps->setFolder(defaultFolder);
        connect(ps, &ProjectSearch::openFileAtLine, this,
                [this](const QString &path, int line) {
            openFile(path);
            if (auto *ed = currentEditor()) ed->gotoLine(line);
        });
        connect(ps, &ProjectSearch::openFileAtLineCol, this,
                [this](const QString &path, int line, int col) {
            openFile(path);
            if (auto *ed = currentEditor()) {
                // setCursorPosition is 0-based; gotoLine is 1-based
                ed->setCursorPosition(line - 1, col - 1);
                ed->ensureLineVisible(line - 1);
            }
        });
        // v0.1.44 — close button inside the search panel header → drop the tab.
        connect(ps, &ProjectSearch::closeRequested, this, [this, ps]() {
            int i = m_tabs->indexOf(ps);
            if (i >= 0) m_tabs->removeTab(i);
            ps->deleteLater();
        });
        int idx = m_tabs->addTab(ps, "Project Search");
        m_tabs->setCurrentIndex(idx);
        ps->focusQuery();
    });

    feat->addSeparator();
    sectionHeader("Workflow");

    // --- Terminal ---
    auto *termAct = feat->addAction("Terminal                  Ctrl+`");
    termAct->setCheckable(true);
    termAct->setShortcut(QKeySequence("Ctrl+`"));
    termAct->setStatusTip("Opens a terminal in a new tab.");
    connect(termAct, &QAction::triggered, this, [this, E]() {
        auto *term = new TerminalWidget;
        // Theme propagation — re-colour chrome when user flips themes.
        connect(this, &MainWindow::themeChanged, term, &TerminalWidget::onThemeChanged);
        if (auto *e = E(); e && !e->filePath().isEmpty())
            term->setWorkingDirectory(QFileInfo(e->filePath()).path());
        int idx = m_tabs->addTab(term, "Terminal");
        m_tabs->setCurrentIndex(idx);
    });

    // --- Markdown Converter ---
    auto *mdMenu = feat->addMenu("Markdown Converter");
    mdMenu->addAction("Selection → Markdown Table", this, [E]() {
        auto *e = E(); if (!e || !e->hasSelectedText()) return;
        // Convert CSV/tab-separated to markdown table
        QStringList lines = e->selectedText().split("\n", Qt::SkipEmptyParts);
        if (lines.isEmpty()) return;
        QString result;
        for (int i = 0; i < lines.size(); i++) {
            QStringList cols = lines[i].split(QRegularExpression("[,\t]"));
            result += "| " + cols.join(" | ") + " |\n";
            if (i == 0) {
                result += "|";
                for (int j = 0; j < cols.size(); j++) result += " --- |";
                result += "\n";
            }
        }
        e->replaceSelectedText(result);
    });
    mdMenu->addAction("Selection → Markdown List", this, [E]() {
        auto *e = E(); if (!e || !e->hasSelectedText()) return;
        QStringList lines = e->selectedText().split("\n", Qt::SkipEmptyParts);
        QString result;
        for (const auto &l : lines) result += "- " + l.trimmed() + "\n";
        e->replaceSelectedText(result);
    });
    mdMenu->addAction("Selection → Markdown Code Block", this, [E]() {
        auto *e = E(); if (!e || !e->hasSelectedText()) return;
        e->replaceSelectedText("```\n" + e->selectedText() + "\n```");
    });
    mdMenu->addAction("Selection → Bold", this, [E]() {
        auto *e = E(); if (!e || !e->hasSelectedText()) return;
        e->replaceSelectedText("**" + e->selectedText() + "**");
    });
    mdMenu->addAction("Selection → Italic", this, [E]() {
        auto *e = E(); if (!e || !e->hasSelectedText()) return;
        e->replaceSelectedText("*" + e->selectedText() + "*");
    });
    mdMenu->addAction("Selection → Link", this, [E]() {
        auto *e = E(); if (!e || !e->hasSelectedText()) return;
        e->replaceSelectedText("[" + e->selectedText() + "](url)");
    });
    mdMenu->addAction("Selection → Heading", this, [E]() {
        auto *e = E(); if (!e || !e->hasSelectedText()) return;
        e->replaceSelectedText("## " + e->selectedText());
    });
    mdMenu->addAction("HTML → Markdown (strip tags)", this, [E]() {
        auto *e = E(); if (!e || !e->hasSelectedText()) return;
        QString text = e->selectedText();
        text.replace(QRegularExpression("<br\\s*/?>"), "\n");
        text.replace(QRegularExpression("<p>(.*?)</p>"), "\\1\n\n");
        text.replace(QRegularExpression("<strong>(.*?)</strong>"), "**\\1**");
        text.replace(QRegularExpression("<b>(.*?)</b>"), "**\\1**");
        text.replace(QRegularExpression("<em>(.*?)</em>"), "*\\1*");
        text.replace(QRegularExpression("<i>(.*?)</i>"), "*\\1*");
        text.replace(QRegularExpression("<h1>(.*?)</h1>"), "# \\1\n");
        text.replace(QRegularExpression("<h2>(.*?)</h2>"), "## \\1\n");
        text.replace(QRegularExpression("<h3>(.*?)</h3>"), "### \\1\n");
        text.replace(QRegularExpression("<a href=\"(.*?)\">(.*?)</a>"), "[\\2](\\1)");
        text.replace(QRegularExpression("<[^>]+>"), "");
        e->replaceSelectedText(text);
    });

    // --- REST Client ---
    auto *restAct = feat->addAction("REST Client (.http)       Ctrl+Shift+R");
    restAct->setCheckable(true);
    restAct->setShortcut(QKeySequence("Ctrl+Shift+R"));
    restAct->setStatusTip("Opens REST client in a new tab. Select HTTP request first.");
    connect(restAct, &QAction::triggered, this, [this, E]() {
        auto *rest = new RestClient;
        // Theme propagation — palette-driven stylesheets re-render.
        connect(this, &MainWindow::themeChanged, rest, &RestClient::onThemeChanged);
        if (E() && E()->hasSelectedText()) rest->executeRequest(E()->selectedText());
        int idx = m_tabs->addTab(rest, "REST Client");
        m_tabs->setCurrentIndex(idx);
    });

    // --- Hex Editor ---
    feat->addAction("Hex Editor — View Binary", this, [this, E]() {
        auto *e = E();
        if (e && !e->filePath().isEmpty()) {
            auto *dlg = new HexEditorDialog(e->filePath(), this);
            // Theme propagation — info-strip + canvas restyle on flip.
            connect(this, &MainWindow::themeChanged, dlg, &HexEditorDialog::onThemeChanged);
            dlg->show();
        } else {
            QMessageBox::information(this, "Hex Editor", "Save the file first to view in hex mode.");
        }
    });

    feat->addSeparator();

    // --- How features work ---
    feat->addAction("How AI Assistant Works...", this, [this]() {
        QMessageBox::information(this, "AI Assistant — How It Works",
            "AI ASSISTANT (Ollama Integration)\n\n"
            "Prerequisites:\n"
            "  1. Install Ollama:  curl -fsSL https://ollama.com/install.sh | sh\n"
            "  2. Pull a model:    ollama pull qwen3.5:9b\n"
            "  3. Start server:    ollama serve\n\n"
            "Usage:\n"
            "  1. Select code in the editor\n"
            "  2. Open AI panel:  Ctrl+Shift+A\n"
            "  3. Click an action:\n"
            "       Explain     — explains what the code does\n"
            "       Find Bugs   — spots issues and suggests fixes\n"
            "       Refactor    — rewrites code cleaner\n"
            "       Write Tests — generates unit tests\n"
            "       Add Comments — annotates the code\n"
            "       Generate Docs — adds docstrings/JSDoc\n"
            "       Optimize    — performance improvements\n"
            "       Translate   — converts to another language\n"
            "  4. Or type any custom prompt\n"
            "  5. Click 'Insert at Cursor' or 'Replace Selection'\n\n"
            "Models: qwen3.5:9b (default), llama3.2:3b, codellama:7b,\n"
            "        deepseek-coder:6.7b, mistral:7b, phi3:mini\n\n"
            "All processing is LOCAL. Nothing leaves your machine.");
    });

    feat->addAction("How REST Client Works...", this, [this]() {
        QMessageBox::information(this, "REST Client — How It Works",
            "REST CLIENT (.http files)\n\n"
            "Write HTTP requests in your editor:\n\n"
            "  GET https://api.github.com/users/octocat\n"
            "  Authorization: Bearer YOUR_TOKEN\n\n"
            "  ###\n\n"
            "  POST https://api.example.com/data\n"
            "  Content-Type: application/json\n\n"
            "  {\"name\": \"test\", \"value\": 42}\n\n"
            "Usage:\n"
            "  1. Select the request block\n"
            "  2. Ctrl+Shift+R to open REST panel\n"
            "  3. Response appears with headers + body\n"
            "  4. JSON is auto-pretty-printed\n\n"
            "Supports: GET, POST, PUT, DELETE, PATCH, HEAD\n"
            "### separates multiple requests in one file");
    });

    // ═══ ENCODING ═══
    // v0.1.42 — encoding menu now actually re-decodes file bytes when the
    // user picks a different codec, instead of just flipping a label.
    // For unmodified files: re-reads bytes from disk via QTextCodec.
    // For modified files: prompts before discarding edits, or just
    // updates the save-encoding label if the user cancels.
    auto *enc = mb->addMenu("E&ncoding");

    // Section 1 — "Reinterpret as ..." (re-decode from disk)
    auto *encReinterpretMenu = enc->addMenu("Reinterpret bytes as");
    const QStringList reinterpretEncs = {
        "UTF-8", "UTF-8 BOM", "UTF-16 LE", "UTF-16 BE",
        "ANSI (Windows-1252)", "ISO-8859-1"
    };
    for (const QString &encName : reinterpretEncs) {
        encReinterpretMenu->addAction(encName, this, [this, E, encName]() {
            auto *e = E(); if (!e) return;
            if (e->isModified()) {
                auto reply = QMessageBox::question(this, "Discard unsaved changes?",
                    QString("Reinterpreting bytes as %1 will reload the file from disk and "
                            "discard your unsaved edits.\n\nContinue?").arg(encName),
                    QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
                if (reply != QMessageBox::Yes) return;
            }
            if (!e->reloadWithEncoding(encName, /*force=*/true)) {
                QMessageBox::warning(this, "Encoding error",
                    QString("Could not reload the file as %1. The codec may not be available.")
                        .arg(encName));
                return;
            }
            m_statusBar->updateEncoding(encName);
        });
    }

    enc->addSeparator();

    // Section 2 — "Convert to ..." (keep current text, change save format)
    auto *encConvertMenu = enc->addMenu("Convert to");
    const QStringList convertEncs = {
        "UTF-8", "UTF-8 BOM", "UTF-16 LE", "UTF-16 BE",
        "ANSI (Windows-1252)", "ISO-8859-1"
    };
    for (const QString &encName : convertEncs) {
        encConvertMenu->addAction(encName, this, [this, E, encName]() {
            auto *e = E(); if (!e) return;
            e->convertEncoding(encName);
            m_statusBar->updateEncoding(encName);
        });
    }

    enc->addSeparator();
    enc->addAction("Convert to UTF-8 (legacy shortcut)", this, [this, E]() {
        auto *e = E(); if (!e) return;
        e->convertEncoding("UTF-8");
        m_statusBar->updateEncoding("UTF-8");
    });

    // ═══ LANGUAGE — 45 languages ═══
    auto *lang = mb->addMenu("&Language");
    lang->addAction("Normal Text", this, [this, E]() { if (auto *e = E()) { e->setLanguage("Plain Text"); m_statusBar->updateLanguage("Plain Text"); } });
    lang->addSeparator();

    // Common languages
    for (const auto &l : {"Python", "JavaScript", "C", "C++", "C#", "Java", "HTML", "CSS",
                          "JSON", "XML", "SQL", "Bash", "Ruby", "Perl", "Lua", "YAML", "Markdown"}) {
        lang->addAction(l, this, [this, E, l]() { if (auto *e = E()) { e->setLanguage(l); m_statusBar->updateLanguage(l); } });
    }
    lang->addSeparator();

    // SQL variants submenu
    auto *sqlMenu = lang->addMenu("SQL Variants");
    for (const auto &l : {"SQL"}) {
        sqlMenu->addAction("SQL (ANSI / Generic)", this, [this, E]() { if (auto *e = E()) { e->setLanguage("SQL"); m_statusBar->updateLanguage("SQL"); } });
        sqlMenu->addAction("T-SQL (SQL Server)", this, [this, E]() { if (auto *e = E()) { e->setLanguage("SQL"); m_statusBar->updateLanguage("T-SQL (SQL Server)"); } });
        sqlMenu->addAction("PL/SQL (Oracle)", this, [this, E]() { if (auto *e = E()) { e->setLanguage("SQL"); m_statusBar->updateLanguage("PL/SQL (Oracle)"); } });
        sqlMenu->addAction("MySQL", this, [this, E]() { if (auto *e = E()) { e->setLanguage("SQL"); m_statusBar->updateLanguage("MySQL"); } });
        sqlMenu->addAction("PostgreSQL", this, [this, E]() { if (auto *e = E()) { e->setLanguage("SQL"); m_statusBar->updateLanguage("PostgreSQL"); } });
        sqlMenu->addAction("SQLite", this, [this, E]() { if (auto *e = E()) { e->setLanguage("SQL"); m_statusBar->updateLanguage("SQLite"); } });
    }
    lang->addSeparator();

    // More languages submenu
    auto *moreLang = lang->addMenu("More Languages");
    for (const auto &l : {"ASM", "AVS", "Batch", "CMake", "CoffeeScript", "D", "Diff",
                          "Fortran", "Fortran77", "IDL", "IntelHex", "Makefile", "MASM",
                          "Matlab", "NASM", "Octave", "Pascal", "PO", "PostScript", "POV",
                          "Properties", "Spice", "SRecord", "TCL", "TeX", "Verilog", "VHDL"}) {
        moreLang->addAction(l, this, [this, E, l]() { if (auto *e = E()) { e->setLanguage(l); m_statusBar->updateLanguage(l); } });
    }

    // ═══ SETTINGS ═══
    auto *settings = mb->addMenu("&Settings");
    settings->addAction("&Preferences...", this, [this]() {
        PreferencesDialog dlg(this);
        // v0.1.42 — when the user clicks OK / Apply, propagate Config
        // to every editor + chrome (toolbar, tab bar, etc.). Pre-v0.1.42
        // the dialog wrote nothing to Config and nothing reapplied even
        // if it had — full theatre.
        QObject::connect(&dlg, &PreferencesDialog::settingsApplied,
                         this, &MainWindow::applyConfigEverywhere);
        dlg.exec();
    });
    settings->addSeparator();

    // Theme selector. "System" picks up macOS AppleInterfaceStyle /
    // Windows AppsUseLightTheme / GNOME color-scheme and renders Light
    // or Dark accordingly. It's the default for fresh installs.
    auto *themeMenu = settings->addMenu("&Theme");
    // Exclusive action group so exactly ONE theme shows a checkmark at
    // any time — the user can see at a glance which theme is active,
    // matching standard Qt / GNOME / macOS menu conventions.
    auto *themeGroup = new QActionGroup(this);
    themeGroup->setExclusive(true);

    auto addTheme = [&](const QString &label, const QString &configKey,
                        const std::function<void()> &apply) {
        auto *act = themeMenu->addAction(label);
        act->setCheckable(true);
        themeGroup->addAction(act);
        if (Config::instance().theme.compare(configKey, Qt::CaseInsensitive) == 0)
            act->setChecked(true);
        connect(act, &QAction::triggered, this, [configKey, apply]() {
            Config::instance().theme = configKey;
            Config::instance().save();
            apply();
        });
    };

    addTheme("System (follow OS)", "System", [this]() {
        applyThemeToAll(resolveTheme("System"));
    });
    themeMenu->addSeparator();
    addTheme("Light",   "Light",   [this]() { applyThemeToAll(lightTheme()); });
    addTheme("Dark",    "Dark",    [this]() { applyThemeToAll(darkTheme()); });
    addTheme("Monokai", "Monokai", [this]() { applyThemeToAll(monokaiTheme()); });
    settings->addSeparator();
    // v0.1.42 — persists via Config + propagates to every open editor.
    // Pre-v0.1.42, these only affected the active tab and reset on next
    // launch because Config::useTabs / Config::tabWidth weren't being
    // written.
    auto *tabMenu = settings->addMenu("Tab Settings");
    tabMenu->addAction("Use Spaces", this, [this]() {
        Config::instance().useTabs = false;
        Config::instance().save();
        applyConfigEverywhere();
    });
    tabMenu->addAction("Use Tabs", this, [this]() {
        Config::instance().useTabs = true;
        Config::instance().save();
        applyConfigEverywhere();
    });
    tabMenu->addSeparator();
    for (int s : {2, 4, 8})
        tabMenu->addAction(QString("Tab Width: %1").arg(s), this, [this, s]() {
            Config::instance().tabWidth = s;
            Config::instance().save();
            applyConfigEverywhere();
        });

    // ═══ TOOLS ═══
    // "Utilities" menu — Hash, Measurement, etc. Renamed from "Tools" to
    // "Utilities" so the primary AI/Terminal/Markdown tools live under
    // "Tools" above and this menu clearly hosts the smaller specialised
    // helpers. Users looking for "Tools" no longer have to guess which of
    // two nearly-identically-named menus to open.
    auto *tools = mb->addMenu("Util&ities");
    auto *measureMenu = tools->addMenu("Measurement");
    auto *rulersAct = measureMenu->addAction("Document Rulers", this, [this](bool checked) {
        Config::instance().showDocumentRulers = checked;
        Config::instance().save();
        for (int i = 0; i < m_tabs->count(); ++i) {
            if (auto *ed = m_tabs->editorAt(i)) ed->setDocumentRulersVisible(checked);
        }
    });
    rulersAct->setCheckable(true);
    rulersAct->setChecked(Config::instance().showDocumentRulers);

    auto *crosshairAct = measureMenu->addAction("Crosshair Pixel Measure", this, [this](bool checked) {
        Config::instance().showCrosshair = checked;
        Config::instance().save();
        for (int i = 0; i < m_tabs->count(); ++i) {
            if (auto *ed = m_tabs->editorAt(i)) ed->setCrosshairVisible(checked);
        }
    });
    crosshairAct->setCheckable(true);
    crosshairAct->setChecked(Config::instance().showCrosshair);

    tools->addSeparator();
    auto *hashMenu = tools->addMenu("Hash");
    struct HashEntry { const char *name; int algo; };
    for (const auto &h : {HashEntry{"MD5", 0}, HashEntry{"SHA-1", 1}, HashEntry{"SHA-256", 2}, HashEntry{"SHA-512", 3}}) {
        hashMenu->addAction(h.name, this, [this, E, algo = h.algo]() {
            if (auto *e = E()) {
                QByteArray d = e->hasSelectedText() ? e->selectedText().toUtf8() : e->text().toUtf8();
                QString h = RustCore::computeHash(d, algo);
                QMessageBox::information(this, "Hash", h);
                QApplication::clipboard()->setText(h);
            }
        });
    }
    tools->addSeparator();
    tools->addAction("Base64 Encode", this, [E]() {
        if (auto *e = E(); e && e->hasSelectedText()) e->replaceSelectedText(RustCore::base64Encode(e->selectedText().toUtf8()));
    });
    tools->addAction("Base64 Decode", this, [E]() {
        if (auto *e = E(); e && e->hasSelectedText()) e->replaceSelectedText(RustCore::base64Decode(e->selectedText().toUtf8()));
    });
    tools->addSeparator();
    tools->addAction("URL Encode", this, [E]() {
        if (auto *e = E(); e && e->hasSelectedText()) e->replaceSelectedText(RustCore::urlEncode(e->selectedText()));
    });
    tools->addAction("URL Decode", this, [E]() {
        if (auto *e = E(); e && e->hasSelectedText()) e->replaceSelectedText(RustCore::urlDecode(e->selectedText()));
    });
    tools->addSeparator();

    // SQL Formatter
    tools->addAction("Format SQL", this, [E]() {
        if (auto *e = E()) {
            QString input = e->hasSelectedText() ? e->selectedText() : e->text();
            QString formatted = RustCore::formatSql(input, 4, true);
            if (e->hasSelectedText()) e->replaceSelectedText(formatted);
            else { e->selectAll(); e->replaceSelectedText(formatted); }
        }
    });
    tools->addAction("Format SQL (lowercase)", this, [E]() {
        if (auto *e = E()) {
            QString input = e->hasSelectedText() ? e->selectedText() : e->text();
            QString formatted = RustCore::formatSql(input, 4, false);
            if (e->hasSelectedText()) e->replaceSelectedText(formatted);
            else { e->selectAll(); e->replaceSelectedText(formatted); }
        }
    });
    tools->addSeparator();

    // Compare
    tools->addAction("Compare with File...", this, [this, E]() {
        auto *e = E(); if (!e) return;
        QString path = QFileDialog::getOpenFileName(this, "Select file to compare", QDir::homePath());
        if (path.isEmpty()) return;
        QFile file(path);
        if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) return;
        QString rightText = QTextStream(&file).readAll();
        QString leftName = e->filePath().isEmpty() ? "Current" : QFileInfo(e->filePath()).fileName();
        auto *dlg = new CompareDialog(e->text(), leftName, rightText, QFileInfo(path).fileName(), this);
        dlg->setAttribute(Qt::WA_DeleteOnClose);
        dlg->show();
    });
    tools->addAction("Compare Two Open Tabs...", this, [this]() {
        if (m_tabs->count() < 2) {
            QMessageBox::information(this, "Compare", "Need at least 2 open tabs to compare.");
            return;
        }
        // Compare current tab with next tab
        int cur = m_tabs->currentIndex();
        int other = (cur + 1) % m_tabs->count();
        auto *left = m_tabs->editorAt(cur);
        auto *right = m_tabs->editorAt(other);
        if (!left || !right) return;
        QString leftName = left->filePath().isEmpty() ? m_tabs->tabText(cur) : QFileInfo(left->filePath()).fileName();
        QString rightName = right->filePath().isEmpty() ? m_tabs->tabText(other) : QFileInfo(right->filePath()).fileName();
        auto *dlg = new CompareDialog(left->text(), leftName, right->text(), rightName, this);
        dlg->setAttribute(Qt::WA_DeleteOnClose);
        dlg->show();
    });

    // ═══ PLUGINS ═══
    // The Plugins menu holds formatters (JSON / HTML / SQL / Bracket), the
    // Compare widget, the Git panel, the REST client — everything that is
    // shipped inbuilt AND ANY user plugins loaded from
    // ~/.config/notepatra/plugins. Naming clarified via "(inbuilt)" suffix
    // on the built-in entries so users can tell them apart from third-party
    // ones. Terminal and AI Assistant live in the "Tools" menu above.
    // ═══ BUILT-IN TOOLS — appended to the Tools menu ═══
    // The "Plu&gins (inbuilt)" items (SQL Formatter, Compare, Git, JSON,
    // HTML, Bracket Tools, REST Client) used to live under a separate
    // "Plugins" top-level menu, which confused users ("are these plugins
    // I need to install? Or built-in?"). They're now appended directly
    // to the Tools menu so the mental model is:
    //    Tools   = everything that ships with Notepatra
    //    Plugins = third-party extensions YOU add
    // See the user-plugin block at the end of this function for the
    // dedicated Plugins menu that hosts user-installable .so/.dll/.dylib
    // extensions and the "How to write a plugin" documentation entry.
    feat->addSeparator();
    sectionHeader("Formatters");
    QString pluginDir = QDir::homePath() + "/.config/notepatra/plugins";
    m_pluginManager.loadPlugins(pluginDir);
    QMenu *pluginsMenu = feat;  // "inbuilt plugin" actions append to Tools

    // SQL Formatter (inbuilt) — opens in a new tab
    pluginsMenu->addAction("SQL Formatter (inbuilt)", this, [this, E]() {
        auto *panel = new SqlFmtPanel;
        // Theme propagation — SQL Formatter chrome + lexer colours track the
        // current theme on runtime flip.
        connect(this, &MainWindow::themeChanged, panel, &SqlFmtPanel::onThemeChanged);
        if (E()) panel->setInput(E()->hasSelectedText() ? E()->selectedText() : E()->text());
        int idx = m_tabs->addTab(panel, "SQL Formatter");
        m_tabs->setCurrentIndex(idx);
    });

    // Compare (inbuilt) — built-in side-by-side compare inspired by Pavel
    // Nedev's ComparePlus for Notepad++. Credit remains in the tooltip and
    // source comments, but the user-facing feature name is now simply Compare.
    auto *compareAct = pluginsMenu->addAction("Compare (inbuilt)", this, [this]() {
        openComparePicker("Compare");
    });
    compareAct->setToolTip("Side-by-side file/tab compare. UX inspired by Pavel Nedev's "
                           "ComparePlus plugin for Notepad++ "
                           "(https://github.com/pnedev/comparePlus).");

    // Git Integration (inbuilt) — opens Git panel in a new tab
    pluginsMenu->addAction("Git Integration (inbuilt)", this, [this, E]() {
        auto *panel = new GitPanel;
        // Theme propagation — SCM chrome + changes tree rows restyle on flip.
        connect(this, &MainWindow::themeChanged, panel, &GitPanel::onThemeChanged);
        connect(panel, &GitPanel::fileClicked, this, &MainWindow::openFile);
        connect(panel, &GitPanel::repositoryOpened, this, [this](const QString &repoRoot) {
            if (!repoRoot.isEmpty()) {
                m_explorer->setRoot(repoRoot);
                m_explorer->setVisible(true);
            }
        });
        // New signals from the v2 GitPanel rewrite — `openFileInTab` opens a
        // plain-file tab, `openDiffInTab` opens a CompareWidget tab showing
        // HEAD-vs-working-copy (same pattern as the FormatterPanel diff path
        // a few dozen lines below).
        connect(panel, &GitPanel::openFileInTab, this, &MainWindow::openFile);
        connect(panel, &GitPanel::openDiffInTab, this,
                [this](const QString &title, const QString &leftText, const QString &rightText) {
            auto *cmp = new CompareWidget;
            // Theme propagation — diff markers re-render on theme flip.
            connect(this, &MainWindow::themeChanged, cmp, &CompareWidget::onThemeChanged);
            int idx = m_tabs->addTab(cmp, title);
            m_tabs->setCurrentIndex(idx);
            connect(cmp, &CompareWidget::closeRequested, this, [this, cmp]() {
                int i = m_tabs->indexOf(cmp);
                if (i >= 0) closeTab(i);
            });
            // compare() must run AFTER closeRequested is connected — if the
            // two sides are identical the widget emits closeRequested right
            // away from inside compare().
            cmp->compare(leftText, "HEAD", rightText, "Working copy");
        });
        if (auto *e = E(); e && !e->filePath().isEmpty()) {
            panel->refresh(e->filePath());
            e->updateGitGutter();
        }
        int idx = m_tabs->addTab(panel, "Git");
        m_tabs->setCurrentIndex(idx);
    });

    // JSON Tools (inbuilt) — opens as tab
    pluginsMenu->addAction("JSON Tools (inbuilt)", this, [this, E]() {
        auto *p = new FormatterPanel("JSON Tools", "JSON");
        connect(this, &MainWindow::themeChanged, p, &FormatterPanel::onThemeChanged);
        p->addButton("Format", [](const QString &s) { return RustCore::formatJson(s, 4); });
        p->addButton("Minify", [](const QString &s) { return RustCore::minifyJson(s); });
        p->addButton("Fix + Format", [](const QString &s) {
            QString report = RustCore::fixJsonReport(s);
            QString fixed = RustCore::fixJson(s);
            QString formatted = RustCore::formatJson(fixed, 4);
            return "/* ═══ FIX REPORT ═══\n" + report + "\n═══════════════════ */\n\n" + formatted;
        });

        // Ollama status + model selector + AI Fix button
        auto *ollamaBar = new OllamaStatus(p);
        auto *panelLayout = p->layout();
        if (panelLayout) panelLayout->addWidget(ollamaBar);

        // "Show thinking" toggle — default OFF because thinking models break
        // the format pipeline. User can enable it if they want to see the
        // model's reasoning (the AI Assistant chat panel is a better place).
        auto *thinkingCheck = new QCheckBox("Show thinking (slower, may break format)");
        thinkingCheck->setChecked(false);
        thinkingCheck->setStyleSheet("padding: 4px 8px; font-size: 11px;");
        if (panelLayout) panelLayout->addWidget(thinkingCheck);

        auto *aiBtn = new QPushButton("AI Fix (Ollama)");
        aiBtn->setFixedHeight(26);
        auto *btnLayout = p->findChild<QHBoxLayout *>();
        if (btnLayout) btnLayout->insertWidget(btnLayout->count() - 3, aiBtn);

        // The Show Diff button is built into FormatterPanel itself —
        // works for ANY action (Format / Minify / Fix+Format / AI Fix).
        // Just pass the raw before/after to CompareWidget. The Myers diff
        // (RustCore::computeDiff) aligns line by line and the visual
        // markers show only the lines that actually differ. No
        // pre-formatting — that was over-engineering and made things
        // worse by changing content the user didn't want changed.
        connect(p, &FormatterPanel::showDiffRequested, this,
                [this](const QString &before, const QString &after, const QString &title) {
            auto *cmp = new CompareWidget;
            // Theme propagation — diff markers re-render on theme flip.
            connect(this, &MainWindow::themeChanged, cmp, &CompareWidget::onThemeChanged);
            cmp->compare(before, "Before", after, "After");
            int idx = m_tabs->addTab(cmp, title);
            m_tabs->setCurrentIndex(idx);
            connect(cmp, &CompareWidget::closeRequested, this, [this, cmp]() {
                int i = m_tabs->indexOf(cmp);
                if (i >= 0) closeTab(i);
            });
        });

        // Captured snapshot of the AI Fix input — used for status messages
        // and to call recordFix() so the built-in Show Diff button works
        // for AI Fix too.
        struct AiFixState {
            QString originalInput;
            QString fixedOutput;
            bool hasResult = false;
        };
        auto *fixState = new AiFixState;

        auto *ollama = new OllamaClient(p);

        connect(aiBtn, &QPushButton::clicked, p, [p, ollama, ollamaBar, thinkingCheck, fixState]() {
            QString input = p->inputText();
            if (input.isEmpty()) {
                p->setStatus("Empty input — paste JSON into the panel below first", true);
                return;
            }
            // Capture the input BEFORE generate() — this is what the diff
            // will compare against.
            fixState->originalInput = input;
            fixState->hasResult = false;

            // Use OllamaClient's synchronous isAvailable() (3s timeout via
            // QEventLoop) instead of OllamaStatus's cached/racing one.
            if (!ollama->isAvailable()) {
                p->setStatus("Ollama not running — start it: ollama serve", true);
                p->setOutput("Ollama is not running.\n\n"
                             "Setup:\n"
                             "  1. Install:  curl -fsSL https://ollama.com/install.sh | sh\n"
                             "  2. Pull:     ollama pull qwen2.5:7b\n"
                             "  3. Start:    ollama serve\n"
                             "  4. Click AI Fix again");
                return;
            }

            QString model = ollamaBar->selectedModel();
            if (model.isEmpty() || model.startsWith("(")) {
                // Force a refresh — maybe the dropdown was populated before
                // Ollama came up. Re-query and use the first model.
                ollamaBar->checkStatus();
                model = ollamaBar->selectedModel();
                if (model.isEmpty() || model.startsWith("(")) {
                    p->setStatus("No models installed — run: ollama pull qwen2.5:7b", true);
                    return;
                }
            }

            bool wantThinking = thinkingCheck->isChecked();
            ollama->setModel(model);
            p->setStatus(QString("Asking %1 to fix the JSON%2...")
                         .arg(model)
                         .arg(wantThinking ? " (with reasoning)" : ""), false);
            p->setOutput("Asking " + model + "...\n");

            // ─── Strict minimal-change prompt ─────────────────────────
            // CRITICAL: tell the model to ONLY patch broken parts and
            // PRESERVE original line order, key order, formatting. Without
            // this, models reorder keys alphabetically and reformat the
            // whole document, which makes Show Diff useless because every
            // line looks different even if only one comma was missing.
            const QString rules =
                "RULES (follow ALL of these):\n"
                "1. PRESERVE the original line structure and key order EXACTLY. "
                "Do NOT reformat. Do NOT reorder keys. Do NOT change indentation "
                "unless the original had no indentation.\n"
                "2. Make MINIMAL changes — only patch the broken parts. If a key "
                "is in line 5 of the input, it must be in line 5 of the output.\n"
                "3. PRESERVE ALL DATA. Never delete keys, values, array elements, "
                "or nested structures.\n"
                "4. Fix ONLY broken syntax:\n"
                "   - Add missing braces { } and brackets [ ]\n"
                "   - Add missing commas between fields and array elements\n"
                "   - Remove trailing commas (JSON spec forbids them)\n"
                "   - Wrap unquoted object keys in double quotes\n"
                "   - Convert single-quoted strings to double-quoted\n"
                "   - Convert Python True/False/None to true/false/null\n"
                "   - Strip // and /* */ comments\n"
                "5. Output ONLY the corrected JSON. No prose, no markdown ``` "
                "fences, no comments, no <think> blocks, no preamble.\n"
                "6. If the input is already valid JSON, output it UNCHANGED.\n";

            QString systemPrompt =
                "You are a minimal-change JSON patcher. Your job is to take broken JSON "
                "and return the SAME JSON with ONLY the broken parts fixed. Preserve "
                "everything else exactly — line order, key order, indentation, whitespace.\n\n"
                + rules;

            // For Gemma and other models that ignore system prompts, repeat
            // the rules at the start of the user message.
            QString modelLower = model.toLower();
            bool isGemmaLike = modelLower.contains("gemma") ||
                               modelLower.contains("phi") ||
                               modelLower.contains("tiny");

            QString userPrompt;
            if (isGemmaLike) {
                userPrompt = rules + "\n"
                    "Fix this broken JSON now with MINIMAL changes. Return ONLY the corrected JSON.\n\n"
                    "BROKEN JSON:\n" + input;
            } else {
                userPrompt =
                    "Fix ONLY the broken parts of this JSON. Make MINIMAL changes. "
                    "PRESERVE the original line order, key order, and formatting. "
                    "Do NOT reorder keys. Do NOT reformat. Return ONLY the corrected JSON.\n\n"
                    "BROKEN JSON:\n" + input;
            }

            ollama->generate(userPrompt, systemPrompt, wantThinking);
        });

        auto *firstToken = new bool(true);
        connect(ollama, &OllamaClient::tokenReceived, p, [p, aiBtn, firstToken](const QString &token) {
            if (*firstToken) { p->setOutput(""); *firstToken = false; }
            p->appendOutput(token);
            aiBtn->setText("AI Fixing...");
        });
        connect(ollama, &OllamaClient::finished, p, [p, aiBtn, firstToken, fixState](const QString &response) {
            aiBtn->setText("AI Fix (Ollama)");
            *firstToken = true;

            QString cleaned = response.trimmed();

            // 1. Strip <think>...</think> blocks (defensive — even with
            //    think=false, some models still emit them).
            QRegularExpression thinkRe("<think>.*?</think>",
                                       QRegularExpression::DotMatchesEverythingOption);
            cleaned.remove(thinkRe);
            cleaned = cleaned.trimmed();

            // 2. Strip markdown ``` code blocks
            if (cleaned.startsWith("```")) {
                int f = cleaned.indexOf('\n');
                int l = cleaned.lastIndexOf("```");
                if (f > 0 && l > f) cleaned = cleaned.mid(f + 1, l - f - 1).trimmed();
            }

            // 3. Some models prefix with "Here is the fixed JSON:" or similar.
            //    Find the first { or [ and trim everything before it.
            int firstBrace = cleaned.indexOf('{');
            int firstBracket = cleaned.indexOf('[');
            int firstStruct = -1;
            if (firstBrace >= 0 && firstBracket >= 0)
                firstStruct = qMin(firstBrace, firstBracket);
            else if (firstBrace >= 0)
                firstStruct = firstBrace;
            else if (firstBracket >= 0)
                firstStruct = firstBracket;
            if (firstStruct > 0) cleaned = cleaned.mid(firstStruct);

            // 4. Try to format the result — if formatJson returns something
            //    sensible (>2 chars, more than just "{}"), use it. Otherwise
            //    show the raw cleaned text so the user can at least see what
            //    the model returned.
            QString formatted = RustCore::formatJson(cleaned, 4);
            QString result = formatted.length() > 2 ? formatted : cleaned;

            if (result.isEmpty()) {
                p->setOutput("(model returned empty response after stripping)\n\nRaw response:\n" + response);
                p->setStatus("✗ AI fix returned empty — try a different model or enable Show thinking", true);
            } else {
                p->setOutput(result);
                // Smart description of WHAT actually changed (commas, braces,
                // brackets, quotes etc.) — same helper Format / Minify use.
                QString desc = FormatterPanel::describeChanges(fixState->originalInput, result);
                int origChars = fixState->originalInput.length();
                int newChars = result.length();
                int newLines = result.count('\n') + 1;
                p->setStatus(QString("✓ AI fix complete — %1 chars, %2 lines (%3). Click 'Show Diff' to see changes.")
                             .arg(newChars).arg(newLines).arg(desc), false);
                // Log to session history with the change description
                p->logAction("AI Fix (Ollama)", origChars, newChars, desc);

                // Stash result + enable the FormatterPanel's built-in Show
                // Diff button via recordFix(). The panel emits
                // showDiffRequested() which we connected above to open a
                // CompareWidget tab — same code path as Format/Minify/Fix.
                fixState->fixedOutput = result;
                fixState->hasResult = true;
                p->recordFix(fixState->originalInput, result, "AI Fix (Ollama)");
            }
        });
        connect(ollama, &OllamaClient::error, p, [p, aiBtn, firstToken](const QString &msg) {
            aiBtn->setText("AI Fix (Ollama)");
            *firstToken = true;
            p->setStatus("✗ AI fix failed: " + msg, true);
            p->setOutput("Error: " + msg + "\n\nIs Ollama running? Try: ollama serve");
        });

        if (E()) p->setInput(E()->hasSelectedText() ? E()->selectedText() : E()->text());
        int idx = m_tabs->addTab(p, "JSON Tools");
        m_tabs->setCurrentIndex(idx);
    });

    // HTML Tools (inbuilt) — format, minify, fix, AI fix
    pluginsMenu->addAction("HTML Tools (inbuilt)", this, [this, E]() {
        auto *p = new FormatterPanel("HTML Tools", "HTML");
        connect(this, &MainWindow::themeChanged, p, &FormatterPanel::onThemeChanged);
        p->addButton("Format (2 spaces)", [](const QString &s) { return RustCore::formatHtml(s, 2); });
        p->addButton("Format (4 spaces)", [](const QString &s) { return RustCore::formatHtml(s, 4); });
        p->addButton("Minify", [](const QString &s) {
            // Strip newlines and extra spaces between tags
            QString result = s;
            result.replace(QRegularExpression("\\s*\\n\\s*"), "");
            result.replace(QRegularExpression(">\\s+<"), "><");
            return result;
        });
        p->addButton("Fix + Format", [](const QString &s) {
            // Fix common HTML issues then format
            QString fixed = s;
            // Close unclosed self-closing tags
            QRegularExpression re_img("<img([^>]*[^/])>", QRegularExpression::CaseInsensitiveOption);
            fixed.replace(re_img, "<img\\1 />");
            QRegularExpression re_br("<br\\s*>", QRegularExpression::CaseInsensitiveOption);
            fixed.replace(re_br, "<br />");
            QRegularExpression re_hr("<hr\\s*>", QRegularExpression::CaseInsensitiveOption);
            fixed.replace(re_hr, "<hr />");
            QRegularExpression re_input("<input([^>]*[^/])>", QRegularExpression::CaseInsensitiveOption);
            fixed.replace(re_input, "<input\\1 />");
            QRegularExpression re_meta("<meta([^>]*[^/])>", QRegularExpression::CaseInsensitiveOption);
            fixed.replace(re_meta, "<meta\\1 />");
            QRegularExpression re_link("<link([^>]*[^/])>", QRegularExpression::CaseInsensitiveOption);
            fixed.replace(re_link, "<link\\1 />");

            // Count open/close tags to detect missing closers
            QString report = "/* ═══ HTML FIX REPORT ═══\n";
            QRegularExpression re_open("<([a-zA-Z][a-zA-Z0-9]*)(?:\\s[^>]*)?>");
            QRegularExpression re_close("</([a-zA-Z][a-zA-Z0-9]*)>");
            QStringList voidTags = {"br","hr","img","input","meta","link","area","base","col","embed","source","track","wbr"};

            QMap<QString, int> openCount, closeCount;
            auto openMatches = re_open.globalMatch(fixed);
            while (openMatches.hasNext()) {
                auto m = openMatches.next();
                QString tag = m.captured(1).toLower();
                if (!voidTags.contains(tag)) openCount[tag]++;
            }
            auto closeMatches = re_close.globalMatch(fixed);
            while (closeMatches.hasNext()) {
                auto m = closeMatches.next();
                closeCount[m.captured(1).toLower()]++;
            }
            int issues = 0;
            for (auto it = openCount.begin(); it != openCount.end(); ++it) {
                int diff = it.value() - closeCount.value(it.key(), 0);
                if (diff > 0) {
                    report += QString("Missing %1 </%2> closing tag(s)\n").arg(diff).arg(it.key());
                    // Add missing closing tags at the end
                    for (int i = 0; i < diff; i++) fixed += "</" + it.key() + ">";
                    issues++;
                }
            }
            if (issues == 0) report += "No issues found.\n";
            report += "═══════════════════════ */\n\n";

            return report + RustCore::formatHtml(fixed, 2);
        });

        // Ollama status + AI Fix
        auto *ollamaBar = new OllamaStatus(p);
        auto *panelLayout = p->layout();
        if (panelLayout) panelLayout->addWidget(ollamaBar);

        auto *aiBtn = new QPushButton("AI Fix (Ollama)");
        aiBtn->setFixedHeight(26);
        auto *btnLayout = p->findChild<QHBoxLayout *>();
        if (btnLayout) btnLayout->insertWidget(btnLayout->count() - 2, aiBtn);

        auto *ollama = new OllamaClient(p);

        connect(aiBtn, &QPushButton::clicked, p, [p, ollama, ollamaBar]() {
            QString input = p->inputText();
            if (input.isEmpty()) return;
            if (!ollamaBar->isAvailable()) {
                p->setOutput("Ollama is not running.\n\nSetup:\n  1. curl -fsSL https://ollama.com/install.sh | sh\n  2. ollama pull qwen3.5:9b\n  3. ollama serve");
                return;
            }
            ollama->setModel(ollamaBar->selectedModel());
            p->setOutput("Asking " + ollamaBar->selectedModel() + "...");
            ollama->generate(
                "Fix this broken HTML. Return ONLY valid HTML. No markdown, no explanation.\n\n" + input,
                "You are an HTML repair tool. Return ONLY fixed HTML. Preserve ALL content.");
        });

        auto *firstToken = new bool(true);
        connect(ollama, &OllamaClient::tokenReceived, p, [p, aiBtn, firstToken](const QString &token) {
            if (*firstToken) { p->setOutput(""); *firstToken = false; }
            p->appendOutput(token);
            aiBtn->setText("AI Fixing...");
        });
        connect(ollama, &OllamaClient::finished, p, [p, aiBtn, firstToken](const QString &response) {
            aiBtn->setText("AI Fix (Ollama)");
            *firstToken = true;
            QString cleaned = response.trimmed();
            if (cleaned.startsWith("```")) {
                int f = cleaned.indexOf('\n'), l = cleaned.lastIndexOf("```");
                if (f > 0 && l > f) cleaned = cleaned.mid(f + 1, l - f - 1).trimmed();
            }
            p->setOutput(RustCore::formatHtml(cleaned, 2));
        });
        connect(ollama, &OllamaClient::error, p, [p, aiBtn, firstToken](const QString &msg) {
            aiBtn->setText("AI Fix (Ollama)");
            *firstToken = true;
            p->setOutput("Error: " + msg + "\n\nRun: ollama serve");
        });

        if (E()) p->setInput(E()->hasSelectedText() ? E()->selectedText() : E()->text());
        int idx = m_tabs->addTab(p, "HTML Tools");
        m_tabs->setCurrentIndex(idx);
    });

    // Bracket Tools (inbuilt) — check, fix, AI fix
    pluginsMenu->addAction("Bracket Tools (inbuilt)", this, [this, E]() {
        auto *p = new FormatterPanel("Bracket Tools", "JavaScript");
        connect(this, &MainWindow::themeChanged, p, &FormatterPanel::onThemeChanged);
        p->addButton("Check", [](const QString &s) { return RustCore::checkBrackets(s); });
        p->addButton("Auto-Fix", [](const QString &s) { return RustCore::fixBrackets(s); });

        // Ollama status + AI Fix
        auto *ollamaBar = new OllamaStatus(p);
        auto *panelLayout = p->layout();
        if (panelLayout) panelLayout->addWidget(ollamaBar);

        auto *aiBtn = new QPushButton("AI Fix (Ollama)");
        aiBtn->setFixedHeight(26);
        auto *btnLayout = p->findChild<QHBoxLayout *>();
        if (btnLayout) btnLayout->insertWidget(btnLayout->count() - 2, aiBtn);

        auto *ollama = new OllamaClient(p);

        connect(aiBtn, &QPushButton::clicked, p, [p, ollama, ollamaBar]() {
            QString input = p->inputText();
            if (input.isEmpty()) return;
            if (!ollamaBar->isAvailable()) {
                p->setOutput("Ollama not running.\n\nSetup:\n  1. curl -fsSL https://ollama.com/install.sh | sh\n  2. ollama pull qwen3.5:9b\n  3. ollama serve");
                return;
            }
            ollama->setModel(ollamaBar->selectedModel());
            p->setOutput("Asking " + ollamaBar->selectedModel() + " to fix brackets...");
            ollama->generate(
                "Fix ALL bracket issues in this code. Fix missing (), [], {}, matching begin/end, if/fi, do/done. "
                "Return ONLY the fixed code, nothing else. No explanation. No markdown.\n\n" + input,
                "You are a code bracket repair tool. Fix all mismatched and missing brackets, parentheses, braces. Preserve all code logic.");
        });

        auto *firstToken = new bool(true);
        connect(ollama, &OllamaClient::tokenReceived, p, [p, aiBtn, firstToken](const QString &token) {
            if (*firstToken) { p->setOutput(""); *firstToken = false; }
            p->appendOutput(token);
            aiBtn->setText("AI Fixing...");
        });
        connect(ollama, &OllamaClient::finished, p, [p, aiBtn, firstToken](const QString &response) {
            aiBtn->setText("AI Fix (Ollama)");
            *firstToken = true;
            QString cleaned = response.trimmed();
            if (cleaned.startsWith("```")) {
                int f = cleaned.indexOf('\n'), l = cleaned.lastIndexOf("```");
                if (f > 0 && l > f) cleaned = cleaned.mid(f + 1, l - f - 1).trimmed();
            }
            p->setOutput(cleaned);
        });
        connect(ollama, &OllamaClient::error, p, [p, aiBtn, firstToken](const QString &msg) {
            aiBtn->setText("AI Fix (Ollama)");
            *firstToken = true;
            p->setOutput("Error: " + msg + "\n\nRun: ollama serve");
        });

        if (E()) p->setInput(E()->hasSelectedText() ? E()->selectedText() : E()->text());
        int idx = m_tabs->addTab(p, "Bracket Tools");
        m_tabs->setCurrentIndex(idx);
    });

    // ═══ PLUGINS — user-installable extensions only ═══
    // Now a dedicated top-level menu containing ONLY user plugins loaded
    // from ~/.config/notepatra/plugins plus the helpers for installing /
    // writing them. The built-in plugin equivalents live under Tools
    // above (see the comment near pluginsMenu = feat).
    auto *userPluginsMenu = mb->addMenu("Pl&ugins");

    if (m_pluginManager.plugins().isEmpty()) {
        auto *emptyAct = userPluginsMenu->addAction("(no user plugins installed)");
        emptyAct->setEnabled(false);
    }

    // User plugins — just click to run
    for (int i = 0; i < m_pluginManager.plugins().size(); i++) {
        const auto &p = m_pluginManager.plugins()[i];
        userPluginsMenu->addAction(QString("Run: %1 v%2").arg(p.name, p.version), this, [this, E, i]() {
            if (auto *e = E()) {
                QString input = e->hasSelectedText() ? e->selectedText() : e->text();
                QString output = m_pluginManager.runPlugin(i, input);
                if (e->hasSelectedText()) e->replaceSelectedText(output);
                else { e->selectAll(); e->replaceSelectedText(output); }
            }
        });
    }

    userPluginsMenu->addSeparator();

    userPluginsMenu->addAction("Open Plugins Folder", this, [pluginDir]() {
        QDir().mkpath(pluginDir);
        QDesktopServices::openUrl(QUrl::fromLocalFile(pluginDir));
    });
    userPluginsMenu->addAction("How to Write a Plugin...", this, [this, pluginDir]() {
        QString compileHint;
#ifdef Q_OS_WIN
        compileHint = "Compile:  cl /LD myplugin.cpp /Fe:myplugin.dll";
#elif defined(Q_OS_MAC)
        compileHint = "Compile:  g++ -shared -fPIC -o myplugin.dylib myplugin.cpp";
#else
        compileHint = "Compile:  g++ -shared -fPIC -o myplugin.so myplugin.cpp";
#endif
        QMessageBox::information(this, "Write a Plugin",
            "Notepatra Plugin API\n\n"
            "Create myplugin.cpp:\n\n"
            "  extern \"C\" {\n"
            "    const char* notepatra_plugin_name() { return \"Name\"; }\n"
            "    char* notepatra_plugin_run(const char* text, int len) {\n"
            "      // transform text, return malloc'd result\n"
            "    }\n"
            "  }\n\n"
            + compileHint + "\n"
            "Drop in:  " + pluginDir + "/\n"
            "Restart Notepatra.");
    });

    // ═══ MACRO ═══
    auto *macro = mb->addMenu("&Macro");

    m_macroStartAct = macro->addAction("Start &Recording", this, [this, E]() {
        auto *ed = E();
        if (!ed) return;
        // Create a fresh QsciMacro attached to the current editor
        delete m_macro;
        m_macro = new QsciMacro(ed);
        m_savedMacro.clear();
        m_macroRecording = true;
        m_macro->startRecording();
        macroUpdateActions();
        statusBar()->showMessage("Macro recording started...", 3000);
    }, QKeySequence("Ctrl+Shift+M"));
    // Note: Ctrl+Shift+R is taken by REST Client (above). Macro recording
    // uses Ctrl+Shift+M (M for Macro) to avoid the ambiguous-shortcut
    // conflict that made REST unreachable on Windows.

    m_macroStopAct = macro->addAction("S&top Recording", this, [this]() {
        if (!m_macro || !m_macroRecording) return;
        m_macro->endRecording();
        m_savedMacro = m_macro->save();
        m_macroRecording = false;
        macroUpdateActions();
        statusBar()->showMessage("Macro recording stopped.", 3000);
    }, QKeySequence("Ctrl+Shift+T"));

    macro->addSeparator();

    m_macroPlayAct = macro->addAction("&Playback", this, [this, E]() {
        auto *ed = E();
        if (!ed) return;
        if (m_savedMacro.isEmpty() && (!m_macro || m_macro->save().isEmpty())) {
            statusBar()->showMessage("No macro recorded.", 3000);
            return;
        }
        // Re-attach macro to current editor (may have switched tabs)
        macroEnsureObject();
        m_macro->play();
        statusBar()->showMessage("Macro played.", 2000);
    }, QKeySequence("Ctrl+Shift+P"));

    m_macroRunMultiAct = macro->addAction("Run &Multiple Times...", this, [this, E]() {
        auto *ed = E();
        if (!ed) return;
        if (m_savedMacro.isEmpty() && (!m_macro || m_macro->save().isEmpty())) {
            statusBar()->showMessage("No macro recorded.", 3000);
            return;
        }
        bool ok;
        int times = QInputDialog::getInt(this, "Run Macro Multiple Times",
                        "Number of times to run:", 1, 1, 10000, 1, &ok);
        if (!ok) return;
        macroEnsureObject();
        for (int i = 0; i < times; i++)
            m_macro->play();
        statusBar()->showMessage(QString("Macro played %1 time(s).").arg(times), 3000);
    });

    macro->addSeparator();

    m_macroSaveAct = macro->addAction("&Save Current Macro...", this, [this]() {
        if (m_savedMacro.isEmpty() && (!m_macro || m_macro->save().isEmpty())) {
            statusBar()->showMessage("No macro to save.", 3000);
            return;
        }
        QString data = m_savedMacro.isEmpty() ? m_macro->save() : m_savedMacro;
        QString path = QFileDialog::getSaveFileName(this, "Save Macro", QDir::homePath(), "Macro Files (*.macro);;All Files (*)");
        if (path.isEmpty()) return;
        QFile f(path);
        if (f.open(QIODevice::WriteOnly | QIODevice::Text)) {
            QTextStream out(&f);
            out << data;
            f.close();
            statusBar()->showMessage("Macro saved to " + path, 3000);
        } else {
            QMessageBox::warning(this, "Save Macro", "Could not write to " + path);
        }
    });

    m_macroLoadAct = macro->addAction("&Load Macro...", this, [this, E]() {
        auto *ed = E();
        if (!ed) return;
        QString path = QFileDialog::getOpenFileName(this, "Load Macro", QDir::homePath(), "Macro Files (*.macro);;All Files (*)");
        if (path.isEmpty()) return;
        QFile f(path);
        if (!f.open(QIODevice::ReadOnly | QIODevice::Text)) {
            QMessageBox::warning(this, "Load Macro", "Could not read " + path);
            return;
        }
        QTextStream in(&f);
        QString data = in.readAll();
        f.close();
        delete m_macro;
        m_macro = new QsciMacro(ed);
        if (m_macro->load(data)) {
            m_savedMacro = data;
            macroUpdateActions();
            statusBar()->showMessage("Macro loaded from " + path, 3000);
        } else {
            QMessageBox::warning(this, "Load Macro", "Invalid macro file.");
            delete m_macro;
            m_macro = nullptr;
        }
    });

    // Initial state: stop/playback/run disabled until a macro exists
    m_macroStopAct->setEnabled(false);
    m_macroPlayAct->setEnabled(false);
    m_macroRunMultiAct->setEnabled(false);
    m_macroSaveAct->setEnabled(false);

    // ═══ RUN ═══
    auto *run = mb->addMenu("&Run");
    run->addAction("Run...", this, [this, E]() {
        bool ok; QString cmd = QInputDialog::getText(this, "Run", "Command:", QLineEdit::Normal, "", &ok);
        if (ok && !cmd.isEmpty()) {
            auto *e = E(); QString dir = (e && !e->filePath().isEmpty()) ? QFileInfo(e->filePath()).path() : QDir::homePath();
#ifdef Q_OS_WIN
            QProcess::startDetached("cmd.exe", {"/c", cmd}, dir);
#else
            QProcess::startDetached("sh", {"-c", cmd}, dir);
#endif
        }
    }, QKeySequence("F5"));
    run->addSeparator();
    run->addAction("Open Containing Folder", this, [E]() {
        if (auto *e = E(); e && !e->filePath().isEmpty())
            QDesktopServices::openUrl(QUrl::fromLocalFile(QFileInfo(e->filePath()).path()));
    });
    run->addAction("Open in Terminal", this, [E]() {
        if (auto *e = E(); e && !e->filePath().isEmpty()) {
            QString dir = QFileInfo(e->filePath()).path();
#ifdef Q_OS_WIN
            QProcess::startDetached("cmd.exe", {"/k", "cd /d " + dir}, dir);
#elif defined(Q_OS_MAC)
            QProcess::startDetached("open", {"-a", "Terminal", dir});
#else
            QProcess::startDetached("x-terminal-emulator", {}, dir);
#endif
        }
    });

    // ═══ WINDOW ═══
    auto *window = mb->addMenu("&Window");
    window->addAction("Windows...", this, [this]() {
        QStringList names;
        for (int i = 0; i < m_tabs->count(); i++) {
            auto *ed = m_tabs->editorAt(i);
            names << QString("%1. %2").arg(i+1).arg(ed && !ed->filePath().isEmpty() ? ed->filePath() : m_tabs->tabText(i));
        }
        QMessageBox::information(this, "Windows", names.join("\n"));
    });

    // ═══ HELP ═══
    // Notepad++ uses "?" as the Help menu label for historical reasons,
    // but "Help" is what every modern app uses and what users actually
    // look for. Renamed for discoverability.
    auto *help = mb->addMenu("&Help");

    auto *guideAct = help->addAction("Feature and Tool Guide...", this, [this]() {
        showRichHelpDialog(this, "Notepatra Help Guide", featureGuideHtml());
    });
    guideAct->setStatusTip("Open the built-in help guide to Notepatra's features and tools");

    // AI help entry — explains the three supported backends in one
    // place so new users don't wonder "do I need Ollama? What's llama.cpp?"
    help->addAction("How the AI Assistant works…", this, [this]() {
        QMessageBox msg(this);
        msg.setWindowTitle("How the AI Assistant works");
        msg.setTextFormat(Qt::RichText);
        msg.setText(
            "<h3>Notepatra AI is local-first</h3>"
            "<p>By default, your code <b>never leaves your machine</b>. No mandatory "
            "API key, no telemetry. The default backend is local (Ollama / llama.cpp).</p>"
            "<p style='color:#888; font-size:11px;'>You can <em>opt in</em> to cloud "
            "LLMs (OpenAI, OpenRouter, Anthropic-via-proxy, Gemini-via-OpenRouter, etc.) "
            "via the OpenAI-compatible backend — those requests leave your machine "
            "if you choose them.</p>"
            "<p>Pick a backend in <b>Settings → Preferences → AI</b>:</p>"
            "<ul>"
            "<li><b>Ollama</b> (easiest) — install from <a href='https://ollama.com'>ollama.com</a>, "
            "run <code>ollama serve</code>, pull a small model: "
            "<code>ollama pull qwen2.5-coder:3b</code>.</li>"
            "<li><b>llama.cpp</b> (most control) — grab any <code>.gguf</code> "
            "from <a href='https://huggingface.co'>huggingface.co</a> and run "
            "<code>llama-server -m model.gguf --port 8080</code>.</li>"
            "<li><b>OpenAI-compat</b> — works with LM Studio, Jan, vLLM, "
            "KoboldCpp, llamafile, text-generation-webui, OpenRouter, "
            "or OpenAI itself. Paste the base URL (and API key if needed).</li>"
            "</ul>"
            "<p>Once a backend is reachable, the AI panel model dropdown "
            "auto-populates and you can click <b>Explain / Find Bugs / "
            "Refactor / Write Tests</b> or type a custom prompt.</p>"
            "<p>For CPU-only / 16 GB laptops, Notepatra auto-picks the "
            "smallest installed model: qwen2.5-coder:3b → qwen2.5:3b → "
            "gemma2:2b → gemma3:4b → llama3.2:3b.</p>"
            "<p>Shortcut: <b>Ctrl+Shift+A</b></p>"
            "<p>Full docs: <a href='https://notepatra.org/docs.html#ai-overview'>"
            "notepatra.org/docs.html#ai-overview</a></p>"
        );
        msg.setStandardButtons(QMessageBox::Ok);
        msg.exec();
    })->setStatusTip("Explains Notepatra's three AI backends: Ollama, llama.cpp, OpenAI-compat.");

    help->addSeparator();

    help->addAction("Keyboard Shortcuts", this, [this]() {
        QMessageBox::information(this, "Keyboard Shortcuts",
            "FILE\n"
            "  Ctrl+N          New\n"
            "  Ctrl+O          Open\n"
            "  Ctrl+S          Save\n"
            "  Ctrl+Shift+S    Save As\n"
            "  Ctrl+W          Close\n"
            "  Ctrl+P          Print\n\n"
            "EDIT\n"
            "  Ctrl+Z          Undo\n"
            "  Ctrl+Y          Redo\n"
            "  Ctrl+D          Duplicate Line\n"
            "  Ctrl+Shift+K    Delete Line\n"
            "  Ctrl+Shift+Up   Move Line Up\n"
            "  Ctrl+Shift+Down Move Line Down\n"
            "  Ctrl+/          Toggle Comment\n"
            "  Ctrl+Shift+U    UPPERCASE\n"
            "  Ctrl+U          lowercase\n\n"
            "SEARCH\n"
            "  Ctrl+F          Find\n"
            "  Ctrl+H          Replace\n"
            "  Ctrl+Shift+F    Find in Files\n"
            "  F3              Find Next\n"
            "  Shift+F3        Find Previous\n"
            "  Ctrl+G          Go to Line\n"
            "  Ctrl+B          Go to Matching Brace\n"
            "  Ctrl+F2         Toggle Bookmark\n"
            "  F2              Next Bookmark\n\n"
            "VIEW\n"
            "  F11             Full Screen\n"
            "  Ctrl+=          Zoom In\n"
            "  Ctrl+-          Zoom Out\n"
            "  Ctrl+0          Zoom Reset\n"
            "  Alt+0           Fold All\n"
            "  Alt+Shift+0     Unfold All\n\n"
            "PANELS\n"
            "  Ctrl+`          Terminal\n"
            "  Ctrl+Shift+A    AI Assistant\n"
            "  Ctrl+Shift+R    REST Client\n"
            "  Ctrl+Shift+E    File Explorer\n"
            "  Ctrl+Shift+M    Record Macro (Macro menu)\n\n"
            "TABS\n"
            "  Ctrl+Tab        Next Tab\n"
            "  Ctrl+Shift+Tab  Previous Tab\n"
            "  Middle-click    Close Tab\n"
            "  Double-click    New Tab (on empty area)");
    });

    help->addSeparator();

    // Check for updates — hits GitHub Releases API. Notify-only, no
    // auto-install. See checkForUpdates() for the full flow.
    help->addAction("Check for Updates...", this, [this]() {
        checkForUpdates(/*silent=*/false);
    });

    help->addSeparator();

    // Quick links — one-click open in the user's browser. The repo URL
    // is the canonical source of truth; release page + issue tracker
    // are what users usually want access to from inside the app.
    help->addAction("Notepatra on GitHub", this, []() {
        QDesktopServices::openUrl(QUrl("https://github.com/singhpratech/notepatra"));
    });
    help->addAction("Latest Release...", this, []() {
        QDesktopServices::openUrl(QUrl("https://github.com/singhpratech/notepatra/releases/latest"));
    });
    help->addAction("Report an Issue...", this, []() {
        QDesktopServices::openUrl(QUrl("https://github.com/singhpratech/notepatra/issues/new"));
    });

    help->addSeparator();

    help->addAction("About Notepatra", this, [this]() {
        // NOTEPATRA_VERSION is injected at compile time from CMakeLists.txt's
        // project(Notepatra VERSION X.Y.Z) so the About dialog never goes
        // stale. Rich-text format so the website + GitHub links are
        // actually clickable — not just visible strings.
        QString version = QApplication::applicationVersion();
        if (version.isEmpty()) version = NOTEPATRA_VERSION;
        QMessageBox box(this);
        box.setWindowTitle("About Notepatra");
        box.setIconPixmap(windowIcon().pixmap(64, 64));
        box.setTextFormat(Qt::RichText);
        box.setTextInteractionFlags(Qt::TextBrowserInteraction);
        box.setText(QString(
            "<h2 style='margin:0 0 6px 0;'>Notepatra v%1</h2>"
            "<p style='color:#888; margin:0 0 14px 0;'>The first editor built for the AI era.</p>"
            "<p>A blazing-fast native code editor for Linux, macOS, and Windows.<br>"
            "Native C++ + Rust. No Electron.<br>"
            "100+ file types · 48 language lexers. Plugin system. 2 GB files.<br>"
            "Local AI via Ollama / llama.cpp / OpenAI-compatible. Zero telemetry.</p>"
            "<p>"
            "🌐 Website: <a href='https://notepatra.org'>notepatra.org</a><br>"
            "💻 Source: <a href='https://github.com/singhpratech/notepatra'>github.com/singhpratech/notepatra</a>"
            "</p>"
            "<p style='color:#888; font-size:11px; margin-top:14px;'>"
            "Envisioned by <a href='https://github.com/singhpratech'>Prateek Singh</a>. Built with Claude."
            "</p>")
            .arg(version));
        box.exec();
    });
}

void MainWindow::buildToolbar() {
    auto *featureTb = addToolBar("Built-in Tools");
    featureTb->setObjectName("featureShortcutBar");
    featureTb->setMovable(false);
    featureTb->setFloatable(false);
    featureTb->setIconSize(QSize(32, 32));
    featureTb->setToolButtonStyle(Qt::ToolButtonTextUnderIcon);

    // Project Search at slot 1 — most-used feature after AI. Clay-orange
    // accent matches its Welcome-tab card. Shortcut: Ctrl+Shift+G.
    addFeatureShortcut(featureTb, findActionByPrefix(this, "Project Search"),
                       QColor("#D47A1E"), "search", "Search",
                       "Recursively search file names + contents (Ctrl+Shift+G)");
    addFeatureShortcut(featureTb, findActionByPrefix(this, "AI Assistant"),
                       QColor("#0E639C"), "ai", "AI",
                       "Open AI Assistant in a new tab");
    addFeatureShortcut(featureTb, findActionByPrefix(this, "Terminal"),
                       QColor("#2D7D46"), "terminal", "Terminal",
                       "Open the built-in terminal");
    addFeatureShortcut(featureTb, findActionByPrefix(this, "Compare (inbuilt)"),
                       QColor("#C27A13"), "compare", "Compare",
                       "Open Compare");
    addFeatureShortcut(featureTb, findActionByPrefix(this, "JSON Tools"),
                       QColor("#1769AA"), "json", "JSON",
                       "Open JSON Tools");
    addFeatureShortcut(featureTb, findActionByPrefix(this, "HTML Tools"),
                       QColor("#C84F2B"), "html", "HTML",
                       "Open HTML Tools");
    addFeatureShortcut(featureTb, findActionByPrefix(this, "SQL Formatter"),
                       QColor("#6A4FBF"), "sql", "SQL",
                       "Open SQL Formatter");
    addFeatureShortcut(featureTb, findActionByPrefix(this, "Bracket Tools"),
                       QColor("#8A5A17"), "brackets", "Brackets",
                       "Open Bracket Tools");
    addFeatureShortcut(featureTb, findActionByPrefix(this, "REST Client"),
                       QColor("#00838F"), "rest", "REST",
                       "Open REST Client");
    addFeatureShortcut(featureTb, findActionByPrefix(this, "Git Integration"),
                       QColor("#B23A48"), "git", "Git",
                       "Open Git Integration");
}

void MainWindow::setupShortcuts() {
    new QShortcut(QKeySequence("Ctrl+Tab"), this, [this]() {
        int idx = (m_tabs->currentIndex() + 1) % qMax(m_tabs->count(), 1);
        m_tabs->setCurrentIndex(idx);
    });
    new QShortcut(QKeySequence("Ctrl+Shift+Tab"), this, [this]() {
        int idx = (m_tabs->currentIndex() - 1 + m_tabs->count()) % qMax(m_tabs->count(), 1);
        m_tabs->setCurrentIndex(idx);
    });
}

// ═══════════════════════════════════════
// Session persistence + crash recovery
// ═══════════════════════════════════════

QString MainWindow::sessionFilePath() {
    return QDir::homePath() + "/.config/notepatra/session.json";
}

QString MainWindow::recoveryDir() {
    return QDir::homePath() + "/.config/notepatra/recovery";
}

void MainWindow::saveSession() {
    QDir().mkpath(QFileInfo(sessionFilePath()).path());

    QJsonArray tabs;
    for (int i = 0; i < m_tabs->count(); i++) {
        auto *e = m_tabs->editorAt(i);
        if (!e || e->filePath().isEmpty()) continue;

        QJsonObject tab;
        tab["path"] = e->filePath();
        int line, col;
        e->getCursorPosition(&line, &col);
        tab["line"] = line;
        tab["col"] = col;
        tab["active"] = (i == m_tabs->currentIndex());
        tabs.append(tab);
    }

    QJsonObject session;
    session["tabs"] = tabs;
    session["windowX"] = x();
    session["windowY"] = y();
    session["windowW"] = width();
    session["windowH"] = height();
    session["maximized"] = isMaximized();

    QFile f(sessionFilePath());
    if (f.open(QIODevice::WriteOnly)) {
        f.write(QJsonDocument(session).toJson());
    }
}

void MainWindow::restoreSession() {
    QFile f(sessionFilePath());
    if (!f.open(QIODevice::ReadOnly)) return;

    QJsonDocument doc = QJsonDocument::fromJson(f.readAll());
    if (doc.isNull()) return;
    QJsonObject session = doc.object();

    // Restore window geometry
    if (session.contains("windowW")) {
        int sw = session["windowW"].toInt();
        int sh = session["windowH"].toInt();
        if (sw > 100 && sh > 100) {
            resize(sw, sh);
            move(session["windowX"].toInt(), session["windowY"].toInt());
        }
    }
    if (session["maximized"].toBool()) {
        showMaximized();
    }

    // Restore tabs
    QJsonArray tabs = session["tabs"].toArray();
    int activeIdx = 0;

    for (int i = 0; i < tabs.size(); i++) {
        QJsonObject tab = tabs[i].toObject();
        QString path = tab["path"].toString();
        if (path.isEmpty() || !QFileInfo(path).exists()) continue;

        openFile(path);

        // Restore cursor position
        auto *e = m_tabs->editorAt(m_tabs->count() - 1);
        if (e) {
            e->setCursorPosition(tab["line"].toInt(), tab["col"].toInt());
        }

        if (tab["active"].toBool()) activeIdx = m_tabs->count() - 1;
    }

    if (m_tabs->count() > 1) {
        // Remove the initial empty "new 1" tab if we restored files
        auto *first = m_tabs->editorAt(0);
        if (first && first->filePath().isEmpty() && !first->isModified()) {
            m_tabs->removeTab(0);
            if (activeIdx > 0) activeIdx--;
        }
    }

    m_tabs->setCurrentIndex(activeIdx);
}

void MainWindow::autoSaveRecovery() {
    QString dir = recoveryDir();
    QDir().mkpath(dir);

    // Write a crash flag
    QFile flag(dir + "/.crash_flag");
    if (flag.open(QIODevice::WriteOnly)) {
        flag.write("running");
        flag.close();
    }

    // Save unsaved/modified content to recovery files
    for (int i = 0; i < m_tabs->count(); i++) {
        auto *e = m_tabs->editorAt(i);
        if (!e || !e->isModified()) continue;

        QString recoveryPath = dir + QString("/recovery_%1.txt").arg(i);
        QFile rf(recoveryPath);
        if (rf.open(QIODevice::WriteOnly)) {
            rf.write(e->text().toUtf8());
        }

        // Save metadata
        QJsonObject meta;
        meta["originalPath"] = e->filePath();
        meta["tabName"] = m_tabs->tabText(i);
        meta["tabIndex"] = i;
        int line, col;
        e->getCursorPosition(&line, &col);
        meta["line"] = line;
        meta["col"] = col;

        QFile mf(recoveryPath + ".meta");
        if (mf.open(QIODevice::WriteOnly)) {
            mf.write(QJsonDocument(meta).toJson());
        }
    }
}

void MainWindow::checkCrashRecovery() {
    QString dir = recoveryDir();
    QFile flag(dir + "/.crash_flag");

    if (!flag.exists()) return;

    // Flag exists = last session didn't close cleanly
    QDir recovDir(dir);
    QStringList recoveryFiles = recovDir.entryList({"recovery_*.txt"}, QDir::Files);

    if (recoveryFiles.isEmpty()) {
        flag.remove();
        return;
    }

    auto result = QMessageBox::question(this, "Crash Recovery",
        QString("Notepatra detected an unclean shutdown.\n\n"
                "%1 unsaved file(s) found in recovery.\n\n"
                "Restore recovered files?").arg(recoveryFiles.size()),
        QMessageBox::Yes | QMessageBox::No);

    if (result == QMessageBox::Yes) {
        for (const QString &rf : recoveryFiles) {
            QString recovPath = dir + "/" + rf;
            QString metaPath = recovPath + ".meta";

            // Read content
            QFile contentFile(recovPath);
            if (!contentFile.open(QIODevice::ReadOnly)) continue;
            QString content = QString::fromUtf8(contentFile.readAll());

            // Read metadata
            QString tabName = "Recovered";
            QString originalPath;
            int line = 0, col = 0;

            QFile metaFile(metaPath);
            if (metaFile.open(QIODevice::ReadOnly)) {
                QJsonObject meta = QJsonDocument::fromJson(metaFile.readAll()).object();
                tabName = meta["tabName"].toString("Recovered");
                originalPath = meta["originalPath"].toString();
                line = meta["line"].toInt();
                col = meta["col"].toInt();
            }

            // Create a new tab with recovered content
            auto *editor = newFile();
            editor->setText(content);
            editor->setCursorPosition(line, col);

            int idx = m_tabs->indexOf(editor);
            m_tabs->setTabText(idx, tabName + " [recovered]");
            if (!originalPath.isEmpty()) {
                m_tabs->setTabToolTip(idx, "Recovered from: " + originalPath);
            }
        }
    }

    // Clean up recovery files
    for (const QString &rf : recovDir.entryList({"recovery_*"}, QDir::Files)) {
        QFile::remove(dir + "/" + rf);
    }
    flag.remove();
}

// ═══════════════════════════════════════
// File change watcher — detect external edits
// ═══════════════════════════════════════

void MainWindow::setupFileWatcher() {
    m_fileWatcher = new QFileSystemWatcher(this);

    connect(m_fileWatcher, &QFileSystemWatcher::fileChanged, this, [this](const QString &path) {
        // File was modified externally — find which tab has it
        for (int i = 0; i < m_tabs->count(); i++) {
            auto *e = m_tabs->editorAt(i);
            if (!e || e->filePath() != path) continue;

            // Check if file still exists
            QFileInfo fi(path);
            if (!fi.exists()) {
                // File was deleted
                m_tabs->setCurrentIndex(i);
                auto result = QMessageBox::warning(this, "File Deleted",
                    QString("The file \"%1\" has been deleted by another program.\n\n"
                            "Keep this file in editor?")
                    .arg(QFileInfo(path).fileName()),
                    QMessageBox::Yes | QMessageBox::No);
                if (result == QMessageBox::No) {
                    m_tabs->removeTab(i);
                    delete e;
                    if (m_tabs->count() == 0) newFile();
                }
                return;
            }

            // File was modified — check if content actually changed
            QDateTime newTime = fi.lastModified();
            if (m_fileTimestamps.contains(path) && newTime == m_fileTimestamps[path])
                return;  // same timestamp, ignore

            m_tabs->setCurrentIndex(i);
            auto result = QMessageBox::question(this, "File Changed",
                QString("The file \"%1\" has been modified by another program.\n\n"
                        "Do you want to reload it?\n\n"
                        "  Yes = Reload from disk (lose your changes)\n"
                        "  No = Keep your version")
                .arg(QFileInfo(path).fileName()),
                QMessageBox::Yes | QMessageBox::No);

            if (result == QMessageBox::Yes) {
                e->loadFile(path);
                updateTabTitle(i);
                m_fileTimestamps[path] = fi.lastModified();
            } else {
                // User chose to keep their version — mark as modified
                m_fileTimestamps[path] = fi.lastModified();
            }

            // Re-add to watcher (Qt removes it after signal)
            m_fileWatcher->addPath(path);
            return;
        }
    });
}

void MainWindow::checkFileChanges() {
    // Called periodically to catch any missed changes
    for (int i = 0; i < m_tabs->count(); i++) {
        auto *e = m_tabs->editorAt(i);
        if (!e || e->filePath().isEmpty()) continue;

        QFileInfo fi(e->filePath());
        if (!fi.exists()) continue;

        QDateTime newTime = fi.lastModified();
        if (m_fileTimestamps.contains(e->filePath()) && newTime != m_fileTimestamps[e->filePath()]) {
            // Trigger the watcher manually
            m_fileWatcher->removePath(e->filePath());
            m_fileWatcher->addPath(e->filePath());
        }
    }
}

void MainWindow::closeEvent(QCloseEvent *event) {
    for (int i = 0; i < m_tabs->count(); i++) {
        auto *e = m_tabs->editorAt(i);
        if (e && e->isModified()) {
            m_tabs->setCurrentIndex(i);
            QString name = m_tabs->tabText(i).remove(" *");
            auto result = QMessageBox::question(this, "Save",
                QString("Save changes to %1?").arg(name),
                QMessageBox::Save | QMessageBox::Discard | QMessageBox::Cancel);
            if (result == QMessageBox::Save) {
                if (!e->filePath().isEmpty()) e->saveFile();
                else {
                    QString path = QFileDialog::getSaveFileName(this, "Save");
                    if (!path.isEmpty()) e->saveFile(path);
                    else { event->ignore(); return; }
                }
            } else if (result == QMessageBox::Cancel) {
                event->ignore();
                return;
            }
        }
    }

    // Save session before closing (clean shutdown)
    saveSession();

    // Remove crash flag (clean exit)
    QFile::remove(recoveryDir() + "/.crash_flag");

    // Clean recovery files (not needed on clean exit)
    QDir recovDir(recoveryDir());
    for (const QString &rf : recovDir.entryList({"recovery_*"}, QDir::Files)) {
        QFile::remove(recoveryDir() + "/" + rf);
    }

    event->accept();
}

// ═══════════════════════════════════════
// Drag and drop — open files by dragging onto window
// ═══════════════════════════════════════

void MainWindow::dragEnterEvent(QDragEnterEvent *event) {
    if (event->mimeData()->hasUrls()) event->acceptProposedAction();
}

void MainWindow::dropEvent(QDropEvent *event) {
    for (const QUrl &url : event->mimeData()->urls()) {
        if (url.isLocalFile()) openFile(url.toLocalFile());
    }
}

// ═══════════════════════════════════════
// Recent files menu
// ═══════════════════════════════════════

void MainWindow::updateRecentMenu() {
    if (!m_recentMenu) return;
    m_recentMenu->clear();
    auto &cfg = Config::instance();
    for (int i = 0; i < cfg.recentFiles.size(); i++) {
        const QString &path = cfg.recentFiles[i];
        m_recentMenu->addAction(QString("&%1: %2").arg(i + 1).arg(path), this, [this, path]() {
            openFile(path);
        });
    }
    if (!cfg.recentFiles.isEmpty()) {
        m_recentMenu->addSeparator();
        m_recentMenu->addAction("Clear Recent Files", this, [this]() {
            Config::instance().recentFiles.clear();
            Config::instance().save();
            updateRecentMenu();
        });
    }
}

// ═══════════════════════════════════════
// Apply theme to entire application
// ═══════════════════════════════════════

void MainWindow::applyThemeToAll(const Theme &t) {
    // Editor tabs
    if (m_tabs) for (int i = 0; i < m_tabs->count(); i++) {
        auto *ed = m_tabs->editorAt(i);
        if (ed) ed->applyTheme(t.name);
    }

    for (QsciScintilla *sci : findChildren<QsciScintilla *>()) {
        if (!sci) continue;
        sci->setPaper(t.editorBg);
        sci->setColor(t.editorFg);
        sci->setCaretLineBackgroundColor(t.caretLine);
        sci->setCaretForegroundColor(t.caret);
        sci->setSelectionBackgroundColor(t.selection);
        sci->setMarginsBackgroundColor(t.marginBg);
        sci->setMarginsForegroundColor(t.marginFg);
        sci->setFoldMarginColors(t.foldBg, t.foldBg);
        sci->setMatchedBraceBackgroundColor(t.matchedBraceBg);
        sci->setMatchedBraceForegroundColor(t.matchedBraceFg);
        if (auto *lex = sci->lexer()) {
            lex->setDefaultPaper(t.editorBg);
            lex->setDefaultColor(t.editorFg);
            applyNotepadPlusPalette(lex, sci->font(), t.name);
        }
    }

    // Status bar
    if (m_statusBar)
        m_statusBar->applyColors(t.statusBg.name(), t.statusFg.name(),
                                  t.tabBorder.name());

    // Welcome tab doesn't listen for runtime theme changes — its child
    // widgets have baked-in stylesheets from welcomePalette() at
    // construction time. Rebuild any open Welcome tab in place so dark
    // mode actually looks dark on the Welcome screen.
    if (m_tabs) {
        for (int i = 0; i < m_tabs->count(); ++i) {
            if (qobject_cast<WelcomeWidget *>(m_tabs->widget(i))) {
                const QString label = m_tabs->tabText(i);
                const bool wasCurrent = (m_tabs->currentIndex() == i);
                auto *old = m_tabs->widget(i);
                m_tabs->removeTab(i);
                delete old;
                auto *fresh = new WelcomeWidget;
                // Rewire signals to the same slots we connect in
                // showWelcomeTab() so the feature cards still work.
                connect(fresh, &WelcomeWidget::actionNewFile, this, [this]() { newFile(); });
                connect(fresh, &WelcomeWidget::actionOpenFile, this, [this]() {
                    QStringList paths = QFileDialog::getOpenFileNames(this, "Open file(s)", QDir::homePath());
                    for (const QString &p : paths) openFile(p);
                });
                connect(fresh, &WelcomeWidget::actionOpenFolder, this, [this]() {
                    QString dir = QFileDialog::getExistingDirectory(this, "Open folder", QDir::homePath());
                    if (!dir.isEmpty() && m_explorer) m_explorer->setRoot(dir);
                });
                connect(fresh, &WelcomeWidget::actionOpenRecent, this,
                        [this](const QString &path) { openFile(path); });
                connect(fresh, &WelcomeWidget::actionOpenMenu, this, &MainWindow::triggerMenuAction);
                connect(fresh, &WelcomeWidget::actionDismissForever, this, []() {
                    auto &cfg = Config::instance();
                    cfg.showWelcomeOnStartup = false;
                    cfg.save();
                });
                m_tabs->insertTab(i, fresh, label);
                if (wasCurrent) m_tabs->setCurrentIndex(i);
                break;   // only one Welcome tab at a time
            }
        }
    }

    // Window stylesheet
    setStyleSheet(QString(
        "QMainWindow { background-color: %1; }"
        "QMenuBar { background-color: %2; color: %3; border-bottom: 1px solid %4; }"
        "QMenuBar::item:selected { background-color: %5; }"
        "QMenu { background-color: %2; color: %3; border: 1px solid %4; }"
        "QMenu::item:selected { background-color: %5; }"
        "QToolBar { background-color: %6; border-bottom: 1px solid %4; }"
        "QToolBar QToolButton { color: %7; font-size: 11px; padding: 3px 4px; border: none; }"
        "QToolBar QToolButton:hover { background-color: %5; }"
        "QTabBar::tab { background-color: %8; color: %9; padding: 5px 12px; border-right: 1px solid %4; }"
        "QTabBar::tab:selected { background-color: %10; border-bottom: 2px solid #4A90D9; }"
        "QTabBar::tab:hover:!selected { background-color: %5; }"
        "QScrollBar:vertical { background: %11; width: 14px; }"
        "QScrollBar::handle:vertical { background: %12; min-height: 30px; border-radius: 4px; margin: 2px; }"
        "QScrollBar::handle:vertical:hover { background: %13; }"
        "QScrollBar::add-line:vertical, QScrollBar::sub-line:vertical { height: 0; }"
        "QScrollBar:horizontal { background: %11; height: 14px; }"
        "QScrollBar::handle:horizontal { background: %12; min-width: 30px; border-radius: 4px; margin: 2px; }"
        "QScrollBar::handle:horizontal:hover { background: %13; }"
        "QScrollBar::add-line:horizontal, QScrollBar::sub-line:horizontal { width: 0; }"
    ).arg(t.windowBg.name(), t.menuBg.name(), t.menuFg.name(), t.tabBorder.name(),
          t.menuHover.name(), t.toolbarBg.name(), t.windowFg.name(),
          t.tabBg.name(), t.tabFg.name(), t.tabActiveBg.name(),
          t.scrollBg.name(), t.scrollHandle.name(), t.scrollHover.name()));

    // Notify every connected panel (AIPanel, ProjectSearch, TerminalWidget,
    // RestClient, HexEditor, GitPanel, CompareWidget, …) that the palette
    // changed. Each panel's onThemeChanged() slot re-applies its palette-
    // dependent stylesheets so the user doesn't have to restart.
    emit themeChanged();
}

// ─── Compare picker — shared by "Compare (inbuilt)" and "ComparePlus" ──
//
// Pops a 2-step QInputDialog (LEFT, then RIGHT). Each step lets the user
// pick any open editor tab (with ● unsaved markers) or browse for a file
// on disk. Once both sides are resolved, opens a CompareWidget tab with
// the chosen pair using `tabLabel` as the tab title — so users can tell
// multiple compare tabs apart in the tab bar.
void MainWindow::openComparePicker(const QString &tabLabel) {
    QStringList tabNames;
    QVector<int> tabIndices;
    for (int i = 0; i < m_tabs->count(); i++) {
        auto *ed = m_tabs->editorAt(i);
        if (ed) {
            QString name;
            QString marker;
            if (ed->filePath().isEmpty()) {
                name = m_tabs->tabText(i);
                marker = " ● untitled";
            } else {
                name = QFileInfo(ed->filePath()).fileName();
                if (ed->isModified()) marker = " ● unsaved";
            }
            tabNames << QString("Tab %1: %2%3").arg(i + 1).arg(name).arg(marker);
            tabIndices << i;
        }
    }

    if (tabNames.size() < 1) {
        QMessageBox::information(this, tabLabel, "Open at least 1 file first.");
        return;
    }

    tabNames << "Browse file from disk...";

    bool ok1;
    QString leftPick = QInputDialog::getItem(this, tabLabel + " — Select LEFT file",
        "Left side:", tabNames, 0, false, &ok1);
    if (!ok1) return;

    bool ok2;
    QString rightPick = QInputDialog::getItem(this, tabLabel + " — Select RIGHT file",
        "Right side:", tabNames, tabNames.size() > 1 ? 1 : 0, false, &ok2);
    if (!ok2) return;

    QString leftText, leftName;
    int leftIdx = tabNames.indexOf(leftPick);
    if (leftIdx >= 0 && leftIdx < tabIndices.size()) {
        auto *ed = m_tabs->editorAt(tabIndices[leftIdx]);
        leftText = ed->text();
        leftName = ed->filePath().isEmpty() ? m_tabs->tabText(tabIndices[leftIdx])
                                             : QFileInfo(ed->filePath()).fileName();
    } else {
        QString path = QFileDialog::getOpenFileName(this, "Select LEFT file", QDir::homePath());
        if (path.isEmpty()) return;
        QFile f(path);
        if (!f.open(QIODevice::ReadOnly | QIODevice::Text)) return;
        leftText = QTextStream(&f).readAll();
        leftName = QFileInfo(path).fileName();
    }

    QString rightText, rightName;
    int rightIdx = tabNames.indexOf(rightPick);
    if (rightIdx >= 0 && rightIdx < tabIndices.size()) {
        auto *ed = m_tabs->editorAt(tabIndices[rightIdx]);
        rightText = ed->text();
        rightName = ed->filePath().isEmpty() ? m_tabs->tabText(tabIndices[rightIdx])
                                              : QFileInfo(ed->filePath()).fileName();
    } else {
        QString path = QFileDialog::getOpenFileName(this, "Select RIGHT file", QDir::homePath());
        if (path.isEmpty()) return;
        QFile f(path);
        if (!f.open(QIODevice::ReadOnly | QIODevice::Text)) return;
        rightText = QTextStream(&f).readAll();
        rightName = QFileInfo(path).fileName();
    }

    auto *cmp = new CompareWidget;
    // Theme propagation — diff markers re-render on theme flip.
    connect(this, &MainWindow::themeChanged, cmp, &CompareWidget::onThemeChanged);
    int idx = m_tabs->addTab(cmp, tabLabel);
    m_tabs->setCurrentIndex(idx);
    connect(cmp, &CompareWidget::closeRequested, this, [this, cmp]() {
        int i = m_tabs->indexOf(cmp);
        if (i >= 0) closeTab(i);
    });
    // compare() must run AFTER the closeRequested connection in case the
    // files are identical and the user clicks "Close comparison" in the
    // popup — that path emits closeRequested immediately.
    cmp->compare(leftText, leftName, rightText, rightName);
}

// ── Cursor-style AI dock toggle ────────────────────────────────────────────
// Push the current workspace state (selection + current file + every open
// editor tab + workspace root + full file-tree listing) into an AIPanel.
// AIPanel installs this as a ContextProvider so it gets called right before
// each Send — that keeps the model's awareness in sync with tab switches
// and edits without us having to wire up a torrent of signals.
void MainWindow::populateAiContext(AIPanel *panel) {
    if (!panel) return;

    QVector<AIPanel::OpenTabInfo> openTabs;
    Editor *cur = currentEditor();
    for (int i = 0; i < m_tabs->count(); ++i) {
        auto *ed = qobject_cast<Editor *>(m_tabs->widget(i));
        if (!ed) continue;   // skip Welcome / AI / REST / Compare / … panels
        AIPanel::OpenTabInfo info;
        info.filePath    = ed->filePath();
        info.displayName = m_tabs->tabText(i).remove('&');  // strip mnemonic
        info.language    = ed->language();
        info.text        = ed->text();
        info.isCurrent   = (ed == cur);
        openTabs.append(info);
    }

    QString selected, curPath, curLang, curText, workspace;
    if (cur) {
        selected = cur->hasSelectedText() ? cur->selectedText() : QString();
        curPath  = cur->filePath();
        curLang  = cur->language();
        curText  = cur->text();
    }

    // Workspace root priority:
    //   1. Explorer root (if user did "Open Folder as Workspace")
    //   2. Directory of the current file
    // This way the AI reasons about the project the user actually opened,
    // not just the folder containing whichever file happens to be active.
    if (m_explorer && !m_explorer->rootPath().isEmpty())
        workspace = m_explorer->rootPath();
    else if (!curPath.isEmpty())
        workspace = QFileInfo(curPath).absolutePath();

    // Walk the workspace root (shallow-but-recursive) to hand the AI a
    // codebase file listing. Lets the model reference files the user
    // hasn't opened — "import from utils.py" even when utils.py isn't
    // in a tab. We cap entries so giant repos don't walk forever.
    QStringList workspaceFilePaths;
    if (!workspace.isEmpty()) {
        constexpr int kWalkCap = 800;
        const QDir rootDir(workspace);
        QDirIterator it(workspace,
                        QDir::Files | QDir::NoDotAndDotDot,
                        QDirIterator::Subdirectories);
        while (it.hasNext() && workspaceFilePaths.size() < kWalkCap) {
            const QString abs = it.next();
            // Filter out VCS / build / node_modules noise — those directories
            // contain thousands of files that dilute the listing and eat
            // the tree budget for no user benefit.
            const QString rel = rootDir.relativeFilePath(abs);
            if (rel.startsWith(QStringLiteral(".git/")) ||
                rel.contains(QStringLiteral("/.git/")) ||
                rel.startsWith(QStringLiteral("node_modules/")) ||
                rel.contains(QStringLiteral("/node_modules/")) ||
                rel.startsWith(QStringLiteral("build/")) ||
                rel.contains(QStringLiteral("/build/")) ||
                rel.startsWith(QStringLiteral("target/")) ||
                rel.contains(QStringLiteral("/target/")) ||
                rel.startsWith(QStringLiteral("dist/")) ||
                rel.contains(QStringLiteral("/dist/")) ||
                rel.startsWith(QStringLiteral(".venv/")) ||
                rel.contains(QStringLiteral("/.venv/")) ||
                rel.startsWith(QStringLiteral("__pycache__/")) ||
                rel.contains(QStringLiteral("/__pycache__/")))
                continue;
            workspaceFilePaths.append(rel);
        }
        workspaceFilePaths.sort();
    }

    panel->setWorkspaceContext(selected, curPath, curLang, curText,
                               openTabs, workspace, workspaceFilePaths);
}

// Shows the AIPanel on the right of the editor so the window reads
// left-to-right as: FileExplorer · EditorTabs · AIPanel. When toggled
// on, also auto-opens the file-tree sidebar so users get the full
// 3-column coding layout immediately.
void MainWindow::toggleAiDock() {
    if (!m_aiDockHost) return;
    const bool show = !m_aiDockHost->isVisible();
    m_aiDockHost->setVisible(show);
    if (show) {
        // DON'T auto-open the file explorer here — opening the AI chat is
        // a lightweight "start a conversation" action. The explorer only
        // appears when the user explicitly ticks Coding Mode (which flips
        // the 3-column layout). Matches user's "nothing in between" ask.
        populateAiContext(m_aiDockPanel);
    }
}

// ── Welcome tab ────────────────────────────────────────────────────────────

int MainWindow::showWelcomeTab() {
    // Reuse an existing Welcome tab if one's already open
    for (int i = 0; i < m_tabs->count(); ++i) {
        if (qobject_cast<WelcomeWidget *>(m_tabs->widget(i))) {
            m_tabs->setCurrentIndex(i);
            return i;
        }
    }

    auto *welcome = new WelcomeWidget;
    int idx = m_tabs->addTab(welcome, "Welcome");
    m_tabs->setCurrentIndex(idx);

    connect(welcome, &WelcomeWidget::actionNewFile, this, [this]() { newFile(); });
    connect(welcome, &WelcomeWidget::actionOpenFile, this, [this]() {
        QStringList paths = QFileDialog::getOpenFileNames(this, "Open file(s)", QDir::homePath());
        for (const QString &p : paths) openFile(p);
    });
    connect(welcome, &WelcomeWidget::actionOpenFolder, this, [this]() {
        QString dir = QFileDialog::getExistingDirectory(this, "Open folder", QDir::homePath());
        if (!dir.isEmpty() && m_explorer) {
            m_explorer->setRoot(dir);
        }
    });
    connect(welcome, &WelcomeWidget::actionOpenRecent, this,
            [this](const QString &path) { openFile(path); });
    connect(welcome, &WelcomeWidget::actionOpenMenu, this, &MainWindow::triggerMenuAction);
    connect(welcome, &WelcomeWidget::actionDismissForever, this, []() {
        auto &cfg = Config::instance();
        cfg.showWelcomeOnStartup = false;
        cfg.save();
    });
    return idx;
}

void MainWindow::triggerMenuAction(const QString &actionId) {
    // Map Welcome feature-card action IDs to the menu-action prefix used
    // by findActionByPrefix. Centralised here so the Welcome tab doesn't
    // need to know the exact menu-label text (which may be translated /
    // refactored over time).
    static const QMap<QString, QString> idToPrefix = {
        {"AIAssistant",   "AI Assistant"},
        {"ProjectSearch", "Project Search"},
        {"Terminal",      "Terminal"},
        {"Compare",       "Compare (inbuilt)"},
        {"JSONTools",     "JSON Tools"},
        {"HTMLTools",     "HTML Tools"},
        {"SQLFormatter",  "SQL Formatter"},
        {"BracketTools",  "Bracket Tools"},
        {"RESTClient",    "REST Client"},
        {"Git",           "Git Integration"},
    };
    QString prefix = idToPrefix.value(actionId);
    if (prefix.isEmpty()) return;
    if (QAction *act = findActionByPrefix(this, prefix)) act->trigger();
}

// ── Macro helpers ──────────────────────────────────────────────────────────

void MainWindow::macroUpdateActions() {
    bool hasMacro = !m_savedMacro.isEmpty() || (m_macro && !m_macro->save().isEmpty());
    m_macroStartAct->setEnabled(!m_macroRecording);
    m_macroStopAct->setEnabled(m_macroRecording);
    m_macroPlayAct->setEnabled(hasMacro && !m_macroRecording);
    m_macroRunMultiAct->setEnabled(hasMacro && !m_macroRecording);
    m_macroSaveAct->setEnabled(hasMacro && !m_macroRecording);
    m_macroLoadAct->setEnabled(!m_macroRecording);
}

void MainWindow::macroEnsureObject() {
    auto *ed = currentEditor();
    if (!ed) return;
    // If the macro is already attached to this editor, nothing to do
    if (m_macro && m_macro->parent() == ed) return;
    // Re-create the macro object on the current editor and load the saved data
    delete m_macro;
    m_macro = new QsciMacro(ed);
    if (!m_savedMacro.isEmpty())
        m_macro->load(m_savedMacro);
}

// ═══════════════════════════════════════════════════════════════════════
// Update check (notify-only, Notepad++ style)
// ═══════════════════════════════════════════════════════════════════════
//
// Flow:
//   1. GET https://api.github.com/repos/singhpratech/notepatra/releases/latest
//   2. Parse tag_name + html_url from the JSON response
//   3. Strip leading 'v' from tag_name, compare to NOTEPATRA_VERSION
//   4. If newer: QMessageBox with "Download" + "Release Notes" + "Later"
//      Download / Release Notes open the GitHub release page in the browser.
//   5. If up-to-date and silent=false: small "you're on the latest" dialog
//      If silent=true: say nothing (used by check-on-startup)
//
// Version comparison is a simple 3-tuple numeric compare (major.minor.patch).
// Anything non-numeric is treated as 0 so tags like "0.1.9-rc1" still work.
static int compareSemver(const QString &a, const QString &b) {
    auto parts = [](const QString &s) {
        QStringList out = s.split('.');
        QList<int> nums;
        for (const QString &p : out) {
            QString clean;
            for (QChar c : p) { if (c.isDigit()) clean += c; else break; }
            nums << clean.toInt();
        }
        while (nums.size() < 3) nums << 0;
        return nums;
    };
    QList<int> pa = parts(a), pb = parts(b);
    for (int i = 0; i < 3; ++i) {
        if (pa[i] != pb[i]) return pa[i] < pb[i] ? -1 : 1;
    }
    return 0;
}

void MainWindow::checkForUpdates(bool silent) {
    // ─── Early guard: SSL must be available. ─────────────────────────
    // Windows and macOS bundles without the OpenSSL DLLs / dylibs will
    // otherwise fail every https:// call with a cryptic "TLS
    // initialization failed". Instead, offer a one-click bailout that
    // opens the Releases page in the user's browser — they still get
    // to update, just not in-app.
    if (!QSslSocket::supportsSsl()) {
        if (silent) return;
        QMessageBox box(this);
        box.setWindowTitle("Check for Updates");
        box.setIcon(QMessageBox::Warning);
        box.setTextFormat(Qt::RichText);
        box.setText(
            "<b>Secure connection unavailable on this system.</b><br><br>"
            "Notepatra can't reach GitHub over HTTPS because the OpenSSL "
            "runtime isn't present. Open the release page in your browser "
            "to download the latest version manually.");
        QPushButton *open = box.addButton("Open Releases Page", QMessageBox::AcceptRole);
        box.addButton("Close", QMessageBox::RejectRole);
        box.exec();
        if (box.clickedButton() == open) {
            QDesktopServices::openUrl(QUrl(
                "https://github.com/singhpratech/notepatra/releases/latest"));
        }
        return;
    }

    static QNetworkAccessManager *nam = nullptr;
    if (!nam) nam = new QNetworkAccessManager(this);

    QNetworkRequest req(QUrl("https://api.github.com/repos/singhpratech/notepatra/releases/latest"));
    req.setRawHeader("Accept", "application/vnd.github+json");
    req.setRawHeader("User-Agent", "Notepatra-UpdateCheck");
    req.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::NoLessSafeRedirectPolicy);

    QNetworkReply *reply = nam->get(req);

    // Safety timeout — GitHub is usually fast but don't hang forever
    QTimer *killer = new QTimer(reply);
    killer->setSingleShot(true);
    killer->start(8000);
    connect(killer, &QTimer::timeout, reply, &QNetworkReply::abort);

    connect(reply, &QNetworkReply::finished, this, [this, reply, silent]() {
        reply->deleteLater();

        if (reply->error() != QNetworkReply::NoError) {
            if (!silent) {
                // Check if this looks like a TLS/SSL failure — give users
                // a helpful "open in browser" button instead of a raw
                // Qt error string they can't act on.
                const QString err = reply->errorString();
                const bool tlsish = err.contains("SSL", Qt::CaseInsensitive) ||
                                     err.contains("TLS", Qt::CaseInsensitive) ||
                                     err.contains("secure", Qt::CaseInsensitive) ||
                                     err.contains("certificate", Qt::CaseInsensitive);
                QMessageBox box(this);
                box.setWindowTitle("Check for Updates");
                box.setIcon(QMessageBox::Warning);
                box.setTextFormat(Qt::RichText);
                box.setText(QString(
                    "<b>Could not reach GitHub.</b><br><br>"
                    "Reason: <code>%1</code><br><br>"
                    "%2")
                    .arg(err.toHtmlEscaped(),
                         tlsish
                             ? "This looks like a TLS/SSL problem — the OpenSSL runtime may be missing. Open the Releases page in your browser to download manually."
                             : "Check your internet connection and try again."));
                QPushButton *open = box.addButton("Open Releases Page", QMessageBox::AcceptRole);
                box.addButton("Close", QMessageBox::RejectRole);
                box.exec();
                if (box.clickedButton() == open) {
                    QDesktopServices::openUrl(QUrl(
                        "https://github.com/singhpratech/notepatra/releases/latest"));
                }
            }
            return;
        }

        const QByteArray body = reply->readAll();
        QJsonParseError parseErr;
        QJsonDocument doc = QJsonDocument::fromJson(body, &parseErr);
        if (parseErr.error != QJsonParseError::NoError || !doc.isObject()) {
            if (!silent) {
                QMessageBox::warning(this, "Check for Updates",
                    "GitHub returned an unexpected response. Try again later.");
            }
            return;
        }

        const QJsonObject obj = doc.object();
        QString tag = obj.value("tag_name").toString();     // e.g. "v0.1.9"
        QString name = obj.value("name").toString();        // e.g. "v0.1.9"
        QString htmlUrl = obj.value("html_url").toString(); // release page
        QString body_md = obj.value("body").toString();     // release notes (markdown)
        QJsonArray assets = obj.value("assets").toArray();  // for Updater

        if (tag.isEmpty() || htmlUrl.isEmpty()) {
            if (!silent) {
                QMessageBox::warning(this, "Check for Updates",
                    "GitHub returned a response with no release tag.");
            }
            return;
        }

        // Strip leading 'v' so "v0.1.9" → "0.1.9"
        QString latest = tag;
        if (latest.startsWith('v') || latest.startsWith('V')) latest = latest.mid(1);

        QString current = QApplication::applicationVersion();
        if (current.isEmpty()) current = NOTEPATRA_VERSION;

        int cmp = compareSemver(current, latest);

        if (cmp >= 0) {
            // Already on the latest (or ahead — e.g. dev build)
            if (!silent) {
                QMessageBox::information(this, "Check for Updates",
                    QString("You're on the latest version.\n\nNotepatra v%1").arg(current));
            }
            return;
        }

        // A newer release is available — build a rich dialog with
        // Download, Release Notes, and Later buttons. "Download" opens
        // the release page (same as Notepad++) — we deliberately do NOT
        // auto-install because that needs per-platform signature
        // verification and is easy to get wrong.
        QString msg = QString(
            "<b>A new version of Notepatra is available.</b><br><br>"
            "Installed: <code>v%1</code><br>"
            "Latest:&nbsp;&nbsp;&nbsp;&nbsp; <code>%2</code><br><br>"
            "Click <b>Download</b> to open the release page in your browser.")
            .arg(current, tag);

        QMessageBox box(this);
        box.setWindowTitle("Update Available");
        box.setIcon(QMessageBox::Information);
        box.setTextFormat(Qt::RichText);
        box.setText(msg);
        QPushButton *downloadBtn = box.addButton("Download", QMessageBox::AcceptRole);
        QPushButton *notesBtn = box.addButton("Release Notes", QMessageBox::ActionRole);
        QPushButton *laterBtn = box.addButton("Later", QMessageBox::RejectRole);
        box.setDefaultButton(downloadBtn);
        Q_UNUSED(laterBtn);
        box.exec();

        QAbstractButton *clicked = box.clickedButton();
        if (clicked == notesBtn) {
            // Release Notes — always just opens the release page.
            QDesktopServices::openUrl(QUrl(htmlUrl));
        } else if (clicked == downloadBtn) {
            // Safe, verified download + handoff to the OS installer.
            // Updater::installReleaseInteractive NEVER modifies the running
            // binary — if anything fails, the current install is intact.
            // See src/updater.h for the full safety contract.
            Updater::installReleaseInteractive(this, assets, tag, htmlUrl);
        }
    });
}

// ─── v0.1.42: Config propagation + View menu state sync ────────────────────

// Push every Config field to every open editor + every piece of chrome
// that depends on it. Called after Preferences OK/Apply, and at startup
// once the menus are built.
void MainWindow::applyConfigEverywhere() {
    const auto &cfg = Config::instance();

    // 1. Per-editor settings (font, caret, tabs, wrap, fold, etc.)
    if (m_tabs) {
        for (int i = 0; i < m_tabs->count(); ++i) {
            if (auto *ed = m_tabs->editorAt(i)) ed->applyConfig();
        }
    }

    // 2. Toolbar visibility — Hide toolbar checkbox in Preferences.
    if (auto *tb = findChild<QToolBar*>("featureShortcutBar")) {
        tb->setVisible(!cfg.hideToolbar);
    }

    // 3. Tab-bar settings.
    if (m_tabs) {
        m_tabs->setTabsClosable(cfg.tabsClosable);
    }

    // 4. View menu checkmarks reflect the new state.
    syncViewMenuToActiveEditor();
}

// Pull editor state back into the View menu's checkable QActions so the
// menu reflects reality after a tab switch (or after applyConfig).
void MainWindow::syncViewMenuToActiveEditor() {
    Editor *e = m_tabs ? m_tabs->currentEditor() : nullptr;

    auto sync = [this](const char *name, bool on) {
        if (auto *act = findChild<QAction*>(QString::fromLatin1(name))) {
            const QSignalBlocker block(act);
            act->setChecked(on);
        }
    };

    if (e) {
        const bool ws = (e->whitespaceVisibility() != QsciScintilla::WsInvisible);
        const bool eolVis = e->eolVisibility();
        sync("viewShowAllCharacters", ws && eolVis);
        sync("viewShowWhitespace",    ws);
        sync("viewShowEol",           eolVis);
        sync("viewShowIndentGuide",   e->indentationGuides());
        sync("viewWordWrap",          e->wrapMode() != QsciScintilla::WrapNone);
    } else {
        sync("viewShowAllCharacters", false);
        sync("viewShowWhitespace",    false);
        sync("viewShowEol",           false);
        sync("viewShowIndentGuide",   Config::instance().showIndentGuides);
        sync("viewWordWrap",          Config::instance().wordWrap);
    }
}
