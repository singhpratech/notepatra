// SPDX-License-Identifier: GPL-3.0-or-later

#include "mainwindow.h"
#include "editor.h"
#include "lexerutils.h"
#include "build_flavor.h"
#include "inlineedit.h"
#include "npp_palette.h"
#include "preferences.h"
#include "rustbridge.h"
#include "fonts.h"
#include "updater.h"
#include "ai_log_dialog.h"
#include "ai_interaction_log.h"
#include "fontpack_dialog.h"
#include "mcp_bridge.h"
#include <QPointer>
#include <QScopeGuard>
#include <numeric>
#include <algorithm>
#ifdef Q_OS_WIN
// NOMINMAX: windows.h defines min/max as preprocessor macros that collide
// with std::min/std::max calls elsewhere in this file. Define before include.
#  define NOMINMAX
#  include <windows.h>
#endif
#ifdef Q_OS_LINUX
#  include <xcb/xcb.h>
#  include <cstring>
#endif
#ifndef Q_OS_WIN
#  include <csignal>   // kill(pid,0) liveness probe (session marker)
#  include <cerrno>
#endif
#include <cstdio>      // std::rename (atomic session write)
#include <QCoreApplication>
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
#include <QEventLoop>
#include <QSettings>
#include <QTimer>
#include <QUrl>
#include <QPainter>
#include <QMouseEvent>
#include <functional>

// ─── VerticalLabel ───────────────────────────────────────────────────
// v0.1.70 — JetBrains-style rotated tool-window rail label. Renders
// QLabel text rotated -90° (reads bottom-to-top). Used on the AI dock
// activity strip to label the file-tree toggle button without eating
// horizontal width. Click handler is stored as std::function so we can
// avoid the Q_OBJECT-in-cpp moc dance.
class VerticalLabel : public QLabel {
public:
    explicit VerticalLabel(const QString &text, QWidget *parent = nullptr)
        : QLabel(text, parent) {
        setAlignment(Qt::AlignCenter);
    }
    void setClickHandler(std::function<void()> h) { m_handler = std::move(h); }
protected:
    void paintEvent(QPaintEvent *) override {
        QPainter p(this);
        p.setRenderHint(QPainter::TextAntialiasing);
        p.setFont(font());
        p.setPen(palette().color(foregroundRole()));
        p.translate(width() / 2.0, height() / 2.0);
        p.rotate(-90);
        const QFontMetrics fm(font());
        const int tw = fm.horizontalAdvance(text());
        p.drawText(-tw / 2, fm.ascent() / 2 - 2, text());
    }
    QSize sizeHint() const override {
        const QFontMetrics fm(font());
        return QSize(fm.height() + 2, fm.horizontalAdvance(text()) + 10);
    }
    QSize minimumSizeHint() const override { return sizeHint(); }
    void mousePressEvent(QMouseEvent *ev) override {
        if (ev->button() == Qt::LeftButton && m_handler) m_handler();
        QLabel::mousePressEvent(ev);
    }
private:
    std::function<void()> m_handler;
};

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
#include "merge_helper_widget.h"
#include "hexeditor.h"
#include "gitgutter.h"
#include "fmtpanel.h"
#include "ollama.h"
#include "ollamastatus.h"
#include "notes.h"
#include "notes_reminder.h"
#include "notes_todos.h"
#include "notes_storage.h"
#include <QSystemTrayIcon>
#include "diagram/diagram_editor.h"
#include "passwordgen.h"
#include "diagram/diagram_view.h"
// v0.1.119 — MCP depth verbs reuse the real app code paths.
#include "git_tools.h"
#include "ai_tools.h"
#include "dbconnections.h"
#include "tool_colors.h"
// v0.1.120 — MCP phase 2 chart verbs (both safe in Lite; the renderer paints
// an "install charts pack" stub without WebEngine).
#include "chart_spec_to_vega.h"
#include "charts/vega_chart_renderer.h"
#include <QRegularExpression>
#include <QFileDialog>
#include <QFileSystemModel>
#include <QSortFilterProxyModel>
#include <QTreeView>
#include <QHeaderView>
#include <QDateTime>
#include <QFileInfo>
#include <QHash>
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
#include <QLineF>
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

// v0.1.119 — inject plain text as escaped <p> paragraphs into a Noter note's
// HTML body (used by the MCP create_note / append_note verbs). Insertion is
// before </main> (the notes-body region), else before </body>. Text is
// HTML-escaped and split per line so NotesStorage::saveNote's validate-body
// (QXmlStreamReader) always sees well-formed markup — no bare entities.
static QString mcpInjectNoteBody(QString html, const QString &text) {
    if (text.isEmpty()) return html;
    QString block;
    const QStringList lines = text.split(QLatin1Char('\n'));
    for (const QString &raw : lines) {
        QString e = raw;
        e.replace(QLatin1Char('&'), QLatin1String("&amp;"));
        e.replace(QLatin1Char('<'), QLatin1String("&lt;"));
        e.replace(QLatin1Char('>'), QLatin1String("&gt;"));
        block += QStringLiteral("<p class=\"p\">%1</p>").arg(e);
    }
    int at = html.lastIndexOf(QStringLiteral("</main>"), -1, Qt::CaseInsensitive);
    if (at < 0)
        at = html.lastIndexOf(QStringLiteral("</body>"), -1, Qt::CaseInsensitive);
    if (at < 0) return html + block;
    html.insert(at, block);
    return html;
}

#ifdef NOTEPATRA_WITH_WEBENGINE
// MCP render_chart/export_chart accept either a Vega-Lite v5 spec or the
// simplified {type,x,y,data} form; translate the latter. Empty result + *err
// on an unsupported/malformed simplified spec.
static QJsonObject mcpChartToVegaLite(const QJsonObject &spec, QString *err) {
    const bool isVegaLite = spec.contains(QLatin1String("mark")) ||
                            spec.contains(QLatin1String("$schema")) ||
                            spec.contains(QLatin1String("encoding"));
    if (isVegaLite) return spec;
    QString terr;
    const QJsonObject vl = ChartSpecToVega::translate(
        spec, ChartSpecToVega::Theme{false, QStringLiteral("Light")}, &terr);
    if (vl.isEmpty() && err)
        *err = terr.isEmpty() ? QStringLiteral("unsupported chart spec") : terr;
    return vl;
}
#endif

// SSOT for the Language surface: the menu, MCP set_language resolution, and
// list_languages all read these lists — add a language in ONE place.
static const QStringList &commonLanguageTokens() {
    static const QStringList list = {
        QStringLiteral("Bash"), QStringLiteral("C"), QStringLiteral("C#"),
        QStringLiteral("C++"), QStringLiteral("CSS"), QStringLiteral("HTML"),
        QStringLiteral("Java"), QStringLiteral("JavaScript"),
        QStringLiteral("JSON"), QStringLiteral("Lua"),
        QStringLiteral("Markdown"), QStringLiteral("Perl"),
        QStringLiteral("Python"), QStringLiteral("Ruby"),
        QStringLiteral("SQL"), QStringLiteral("XML"), QStringLiteral("YAML")};
    return list;
}

static const QStringList &moreLanguageTokens() {
    static const QStringList list = [] {
        QStringList l = {
            QStringLiteral("ASM"), QStringLiteral("Apex"), QStringLiteral("AVS"),
            QStringLiteral("Batch"), QStringLiteral("BibTeX"),
            QStringLiteral("CMake"), QStringLiteral("CoffeeScript"),
            QStringLiteral("Crystal"), QStringLiteral("Cython"),
            QStringLiteral("D"), QStringLiteral("Dart"), QStringLiteral("Diff"),
            QStringLiteral("Dockerfile"), QStringLiteral("DotEnv"),
            QStringLiteral("Elixir"), QStringLiteral("F#"), QStringLiteral("Fish"),
            QStringLiteral("Fortran"), QStringLiteral("Fortran77"),
            QStringLiteral("GDScript"), QStringLiteral("Gitignore"),
            QStringLiteral("Go"), QStringLiteral("GraphQL"), QStringLiteral("Groovy"),
            QStringLiteral("Hack"), QStringLiteral("HCL"), QStringLiteral("IDL"),
            QStringLiteral("IntelHex"), QStringLiteral("Jinja"),
            QStringLiteral("JSON5"), QStringLiteral("Julia"),
            QStringLiteral("Kotlin"), QStringLiteral("Liquid"),
            QStringLiteral("Makefile"), QStringLiteral("MASM"),
            QStringLiteral("Matlab"), QStringLiteral("Mojo"),
            QStringLiteral("NASM"), QStringLiteral("Nim"), QStringLiteral("Nushell"),
            QStringLiteral("Octave"), QStringLiteral("Pascal"), QStringLiteral("PO"),
            QStringLiteral("PostScript"), QStringLiteral("POV"),
            QStringLiteral("PowerShell"), QStringLiteral("Properties"),
            QStringLiteral("Protobuf"), QStringLiteral("R"), QStringLiteral("Rust"),
            QStringLiteral("Scala"), QStringLiteral("Solidity"),
            QStringLiteral("Spice"), QStringLiteral("SRecord"),
            QStringLiteral("Swift"), QStringLiteral("TCL"), QStringLiteral("TeX"),
            QStringLiteral("Thrift"), QStringLiteral("TOML"), QStringLiteral("Twig"),
            QStringLiteral("TypeScript"), QStringLiteral("Vala"),
            QStringLiteral("Verilog"), QStringLiteral("VHDL"), QStringLiteral("Zig")};
        l.sort(Qt::CaseInsensitive);
        return l;
    }();
    return list;
}

static const QStringList &allKnownLanguageTokens() {
    static const QStringList list = QStringList()
        << QStringLiteral("Plain Text") << commonLanguageTokens()
        << moreLanguageTokens();
    return list;
}

// Canonicalizes an agent-supplied language token; "" when unknown.
static QString resolveLanguageToken(const QString &input) {
    const QString token = input.trimmed();
    if (token.isEmpty()) return QString();
    // (a) exact factory success — accepts every token the menu can set.
    if (QsciLexer *probe = createLexerForLanguage(token, nullptr)) {
        delete probe;
        return token;
    }
    // (b) case-insensitive match against the canonical set.
    for (const QString &l : allKnownLanguageTokens())
        if (l.compare(token, Qt::CaseInsensitive) == 0) return l;
    // (c) common agent aliases.
    static const QHash<QString, QString> aliases = {
        {QStringLiteral("python"), QStringLiteral("Python")},
        {QStringLiteral("py"), QStringLiteral("Python")},
        {QStringLiteral("js"), QStringLiteral("JavaScript")},
        {QStringLiteral("ts"), QStringLiteral("TypeScript")},
        {QStringLiteral("cpp"), QStringLiteral("C++")},
        {QStringLiteral("c++"), QStringLiteral("C++")},
        {QStringLiteral("csharp"), QStringLiteral("C#")},
        {QStringLiteral("c#"), QStringLiteral("C#")},
        {QStringLiteral("sh"), QStringLiteral("Bash")},
        {QStringLiteral("shell"), QStringLiteral("Bash")},
        {QStringLiteral("bash"), QStringLiteral("Bash")},
        {QStringLiteral("yml"), QStringLiteral("YAML")},
        {QStringLiteral("yaml"), QStringLiteral("YAML")},
        {QStringLiteral("md"), QStringLiteral("Markdown")},
        {QStringLiteral("golang"), QStringLiteral("Go")},
        {QStringLiteral("go"), QStringLiteral("Go")},
        {QStringLiteral("rs"), QStringLiteral("Rust")},
        {QStringLiteral("rb"), QStringLiteral("Ruby")},
        {QStringLiteral("kt"), QStringLiteral("Kotlin")}};
    return aliases.value(token.toLower());
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
    //
    // v0.1.54 — pulled the lens 4 % toward centre (0.40 → 0.44) and shrank
    // the radius from 0.28 to 0.25 to keep the stroke clear of the
    // rounded-square's corner curve at every DPI. Pre-fix the lens's
    // top-left stroke pixel sat at logical (3.6, 3.6) which is only 0.36
    // logical px inside the rounded corner — at 150 % DPI antialiasing
    // smeared the corner curve and the lens stroke into the same physical
    // pixel and the lens read as "clipped at the top". With the new
    // values the lens stroke sits ≥ 1.5 logical px inside the corner —
    // crisp on every monitor.
    painter.setPen(QPen(Qt::white, 2.0, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
    painter.setBrush(Qt::NoBrush);

    const qreal r = rect.width() * 0.25;
    const QPointF center(rect.left() + rect.width() * 0.44,
                         rect.top()  + rect.height() * 0.44);
    painter.drawEllipse(center, r, r);

    // Handle: 45° line from lens edge to rect corner.
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

// Noter — pad with folded corner + 3 ruled lines + accent action-bullet.
// Reads as "meeting notes with a todo" without resorting to emoji.
static void drawNoterFeatureGlyph(QPainter &painter, const QRectF &rect) {
    painter.setPen(QPen(Qt::white, 1.7, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
    painter.setBrush(Qt::NoBrush);

    const qreal pad = 7.0;
    const qreal foldSize = 5.0;

    // Page outline with folded top-right corner.
    QPainterPath page;
    page.moveTo(rect.left() + pad, rect.top() + pad);
    page.lineTo(rect.right() - pad - foldSize, rect.top() + pad);
    page.lineTo(rect.right() - pad, rect.top() + pad + foldSize);
    page.lineTo(rect.right() - pad, rect.bottom() - pad);
    page.lineTo(rect.left() + pad, rect.bottom() - pad);
    page.closeSubpath();
    painter.drawPath(page);

    // Folded-corner notch.
    painter.drawLine(QPointF(rect.right() - pad - foldSize, rect.top() + pad),
                     QPointF(rect.right() - pad - foldSize, rect.top() + pad + foldSize));
    painter.drawLine(QPointF(rect.right() - pad - foldSize, rect.top() + pad + foldSize),
                     QPointF(rect.right() - pad, rect.top() + pad + foldSize));

    // Three ruled lines suggesting note content.
    const qreal lineX1 = rect.left() + pad + 4.0;
    const qreal lineX2 = rect.right() - pad - 3.0;
    const qreal y0 = rect.top() + pad + foldSize + 4.0;
    const qreal step = 4.5;

    painter.drawLine(QPointF(lineX1, y0),            QPointF(lineX2 - 5, y0));
    painter.drawLine(QPointF(lineX1, y0 + step),     QPointF(lineX2,     y0 + step));
    painter.drawLine(QPointF(lineX1, y0 + 2 * step), QPointF(lineX2 - 9, y0 + 2 * step));

    // Action-item bullet — small peach dot at the start of one line.
    painter.setPen(Qt::NoPen);
    painter.setBrush(QColor("#FFE4D6"));
    painter.drawEllipse(QPointF(rect.left() + pad + 1.8, y0 + 2 * step), 1.6, 1.6);
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

static void drawDiagramFeatureGlyph(QPainter &painter, const QRectF &rect) {
    // Two nodes joined by an arrow — a mini flowchart for the .npd diagram tool.
    QRectF nodeA(rect.left() + 3.5, rect.top() + 4.5, 12.0, 8.5);
    QRectF nodeB(rect.right() - 15.5, rect.bottom() - 13.0, 12.0, 8.5);

    painter.setPen(QPen(Qt::white, 1.3, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
    painter.setBrush(QColor(255, 255, 255, 32));
    painter.drawRoundedRect(nodeA, 2.4, 2.4);
    painter.drawRoundedRect(nodeB, 2.4, 2.4);

    const QPointF from(nodeA.center().x() + 2.0, nodeA.bottom());
    const QPointF to(nodeB.center().x() - 2.0, nodeB.top());
    painter.setPen(QPen(Qt::white, 1.4, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
    painter.drawLine(from, to);

    // Arrowhead at the target end (unit vector via QLineF — no <cmath> needed).
    const QLineF u = QLineF(from, to).unitVector();
    const qreal ux = u.dx(), uy = u.dy();
    const qreal nx = -uy, ny = ux;
    const qreal a = 4.0, w = 2.6;
    const QPointF base(to.x() - ux * a, to.y() - uy * a);
    QPainterPath head;
    head.moveTo(to);
    head.lineTo(base.x() + nx * w, base.y() + ny * w);
    head.lineTo(base.x() - nx * w, base.y() - ny * w);
    head.closeSubpath();
    painter.setPen(Qt::NoPen);
    painter.setBrush(Qt::white);
    painter.drawPath(head);
}

static void drawPasswordFeatureGlyph(QPainter &painter, const QRectF &rect) {
    // A padlock: shackle arc above a rounded body with a keyhole. Drawn
    // with primitives, never an emoji codepoint — an emoji glyph tofus on
    // any Linux box without a colour-emoji font.
    const qreal cx = rect.center().x();
    const QRectF body(cx - 8.0, rect.center().y() - 2.0, 16.0, 12.5);

    // Shackle — a half-ring whose ends meet the top of the body.
    QRectF shackle(cx - 5.2, body.top() - 9.0, 10.4, 13.0);
    painter.setPen(QPen(Qt::white, 2.0, Qt::SolidLine, Qt::RoundCap));
    painter.setBrush(Qt::NoBrush);
    painter.drawArc(shackle, 0 * 16, 180 * 16);

    painter.setPen(QPen(Qt::white, 1.4, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
    painter.setBrush(QColor(255, 255, 255, 40));
    painter.drawRoundedRect(body, 2.6, 2.6);

    // Keyhole — a dot with a short stem, so the lock reads at 16 px too.
    painter.setPen(Qt::NoPen);
    painter.setBrush(Qt::white);
    painter.drawEllipse(QPointF(cx, body.center().y() - 1.4), 1.8, 1.8);
    painter.setPen(QPen(Qt::white, 1.6, Qt::SolidLine, Qt::RoundCap));
    painter.drawLine(QPointF(cx, body.center().y() - 0.4),
                     QPointF(cx, body.center().y() + 3.0));
}

static QIcon makeFeatureIcon(const QColor &base, const QString &iconKind, const QString &glyph = QString()) {
    // v0.1.51 — paint at native device-pixel resolution so the toolbar
    // icons stay crisp on Windows 150 % display zoom and other fractional
    // / Retina configurations. Pre-fix the icon was always rasterized
    // into a 32×32 pixmap, then bilinear-scaled by Qt to ~48 device px
    // at 150 %, which produced the pixelation the user reported. Now we:
    //
    //   1. Multiply backing-store size by `devicePixelRatio()` so the
    //      pixmap holds enough pixels for the actual display density.
    //   2. Tag the pixmap with `setDevicePixelRatio(dpr)` so Qt treats
    //      it as logical 32×32 (and doesn't re-scale it again on draw).
    //   3. Scale every QPainter coordinate by `dpr` so the gradient,
    //      rounded-rect, and glyph are drawn in real device pixels —
    //      i.e. each pen stroke + circle remains sub-pixel-precise
    //      instead of being rasterized at 32-px resolution and then
    //      stretched.
    //
    // The drawXxxFeatureGlyph() helpers take a QRect in painter coords;
    // we pass a scaled rect so they produce a 48×48 (at 1.5×) image
    // already at the correct density. AA_UseHighDpiPixmaps from main.cpp
    // tells Qt to use the result without any further scaling.
    const qreal dpr = qApp ? qApp->devicePixelRatio() : 1.0;
    const int   nat = static_cast<int>(32 * dpr + 0.5);  // device-px backing
    QPixmap pixmap(nat, nat);
    pixmap.setDevicePixelRatio(dpr);
    pixmap.fill(Qt::transparent);

    QPainter painter(&pixmap);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.setRenderHint(QPainter::SmoothPixmapTransform);
    // Scale the painter so every drawXxxFeatureGlyph() helper's "32"
    // means "32 logical px"; we render that into `nat × nat` device px.
    painter.scale(dpr, dpr);

    QRect rect = QRect(0, 0, 32, 32).adjusted(1, 1, -1, -1);
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
    } else if (iconKind == "noter") {
        drawNoterFeatureGlyph(painter, rect);
    } else if (iconKind == "git") {
        drawGitFeatureGlyph(painter, rect);
    } else if (iconKind == "diagram") {
        drawDiagramFeatureGlyph(painter, rect);
    } else if (iconKind == "password") {
        drawPasswordFeatureGlyph(painter, rect);
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
                               const QString &label, const QString &tooltip = QString(),
                               bool showCheckedState = false) {
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

    // v0.1.70 — only the AI Assistant button reflects its action's checked
    // state visually (ON/OFF toggle). Other tools (Terminal, JSON, etc.)
    // have checkable actions internally (so the View menu shows a check
    // when their tab is open) but the user wants the toolbar buttons to
    // stay plain trigger-style. Pass showCheckedState=true to opt in.
    if (showCheckedState && action->isCheckable()) {
        button->setCheckable(true);
        button->setChecked(action->isChecked());
        QObject::connect(action, &QAction::toggled, button, &QToolButton::setChecked);
    }

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
<li><b>AI Interaction Log:</b> every request and response to any AI backend — local or cloud, including Noter's Extract — is recorded for 7 days in <b>Features &gt; AI Interaction Log…</b>, a browsable, credential-scrubbed audit table. It is on by default and can be turned off in <b>Settings &gt; AI</b>.</li>
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

<h3>Noter</h3>
<ul>
<li><code>Ctrl+Alt+N</code> (or the icon row, or <b>Features &gt; Noter — Meeting Thinkpad</b>) toggles Noter: it opens the tab, and pressing it again while the tab is focused closes it. Noter is a two-pane meeting workspace (notes list on the left, editor on the right). Fully local — notes live under <code>~/Documents/Notepatra/Noter/</code>, with no accounts and no bots.</li>
<li><b>+ Noter</b> (or <code>Ctrl+Alt+M</code>) creates a note. A small top toolbar inserts <b>Action Items</b> / <b>What I plan</b> / <b>To-dos</b> section headers and checkbox bullets; click a checkbox to mark it done and strike the line through.</li>
<li><b>Extract</b> (footer button or <code>Ctrl+Alt+E</code>) runs your configured AI backend over the note and returns a short <b>summary</b> plus <b>action items, decisions, questions and risks</b>. An action that mentions a time ("ship the build 10am tomorrow") comes back with that date/time pre-filled.</li>
<li><b>Reminders:</b> right-click a note to set a reminder, or schedule action items straight from Extract (each with a calendar + time picker). Every reminder appears in the central <b>Reminders</b> section of the sidebar, grouped <i>Overdue / Today / This week / Later</i> — click to open the note, the pencil to change the time, the ✕ to delete. Desktop notifications fire at the due time whenever Notepatra is running — the Noter tab does not need to be open. Reminders that come due while Notepatra is closed arrive as a single catch-up notification on the next launch. Re-running Extract flags items already scheduled so you don't get duplicates.</li>
<li><b>Noter shortcuts</b> (active while the Noter tab has focus): <code>Ctrl+Alt+M</code> new note, <code>Ctrl+Alt+J</code> jump to the note search box (quick-switch between notes), <code>Ctrl+Alt+E</code> Extract, <code>Ctrl+Alt+T</code> jump to the Reminders section in the sidebar, <code>Ctrl+Alt+B</code> show/hide the sidebar, <code>Ctrl+Alt+P</code> pop the current note out into its own window, <code>F4</code> toggle the checkbox on the current line. <code>Ctrl+Alt+N</code> toggles the Noter tab itself from anywhere.</li>
</ul>

<h3>Password Generator</h3>
<ul>
<li>The icon row (next to <b>AI</b>) or <b>Tools &gt; Password Generator — Passwords / Passphrases / SSH keys</b> toggles it: pressing it again while the tab is focused closes it. It runs entirely offline — there is no wordlist download, no network call, and no backend.</li>
<li><b>Three pages</b>, chosen from the left rail with <code>Alt+1</code>, <code>Alt+2</code> and <code>Alt+3</code>. <i>Password</i> draws single characters from a-z, A-Z, 0-9 and a shell-safe symbol set (quotes, backslash, backtick and pipe are left out so a password survives being pasted into a command line, a YAML file or a connection string); you can add your own characters, exclude the look-alikes <code>0 O 1 l I</code>, and require at least one character from every set. <i>Passphrase</i> joins words from a 2,048-word built-in list, so each word is worth exactly 11 bits.</li>
<li><b>SSH key.</b> Generates an OpenSSH pair — Ed25519 (recommended), ECDSA P-256 / P-384, RSA 3072 / 4096, or RSA 2048 (legacy) — with an optional passphrase that encrypts the private key the same way <code>ssh-keygen</code> does. The comment is left <b>empty</b> on purpose: <code>ssh-keygen</code> would put <code>user@host</code> there, which copies your username and machine name into every <code>authorized_keys</code> the key is pasted into. The public line and its SHA256 fingerprint are shown at once; the private key stays hidden until you tick <b>Show private key</b>. <b>Save private key…</b> sets the file to <code>0600</code> before writing, refuses to overwrite, and writes the <code>.pub</code> beside it. Generation happens off the UI thread, so RSA-4096 takes a few seconds without freezing the window.</li>
<li><b>The bits figure is the real one.</b> It is the base-2 logarithm of how many distinct values the current settings can produce, not a guess at how complicated the result looks. Ticking <b>At least one from each set</b> makes the number drop slightly — that is correct, because the guarantee rules out every password that misses a set.</li>
<li><b>Nothing is stored.</b> The tab is not an editor, so nothing it shows is written to <code>session.json</code>, sent to an AI backend, or readable by a connected MCP client. Nothing reaches the disk unless you choose <b>Save private key…</b>. <b>Copy</b> puts the value on the system clipboard and takes it back after 30 seconds, unless you copied something else in the meantime — that wipe arms for the private key, not for the public one. Use <b>Insert into editor</b> or <b>Open in new tab</b> when you do want it in a file.</li>
<li><b>Randomness</b> comes from the operating system's random source through the Rust core, for passwords, passphrases and keys alike.</li>
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
<li><code>Ctrl+Alt+N</code> toggle Noter, <code>Ctrl+Alt+M</code> new note in Noter (full Noter shortcut list in the Noter section above)</li>
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

// v0.1.94 — clamp a desired window rect to the union of currently-connected
// monitors. Pre-fix bug: a previous session on a now-disconnected secondary
// monitor would persist e.g. windowX=2500,windowY=200 in config; on relaunch
// move(2500,200) put the window off-screen. User saw "Notepatra invisible",
// then double-clicked the launcher repeatedly accumulating PIDs while the
// orphaned first instance kept running off-screen — fed the multi-PID bug.
//
// Strategy: if `want` intersects ANY available screen by at least 100 px in
// each axis, accept it. Else recenter on the primary screen at the requested
// size (capped at 90 % of primary's available area so we never overflow).
static QRect clampWindowToScreens(const QRect &want) {
    const auto screens = QGuiApplication::screens();
    if (screens.isEmpty()) return want;  // headless test; trust caller
    for (QScreen *s : screens) {
        const QRect inter = s->availableGeometry().intersected(want);
        if (inter.width() >= 100 && inter.height() >= 100) {
            return want;  // Visible enough on at least one monitor.
        }
    }
    const QRect avail = (QGuiApplication::primaryScreen()
                             ? QGuiApplication::primaryScreen()->availableGeometry()
                             : screens.first()->availableGeometry());
    int w = std::min(want.width(),  (avail.width()  * 9) / 10);
    int h = std::min(want.height(), (avail.height() * 9) / 10);
    if (w < 400) w = 400;
    if (h < 300) h = 300;
    return QRect(avail.x() + (avail.width()  - w) / 2,
                 avail.y() + (avail.height() - h) / 2,
                 w, h);
}

// v0.1.100 — produce a CENTERED window rect of the requested size that always
// keeps the native title bar on-screen.
//
// Why center instead of restoring the saved x/y: the save side records x()/y()
// (Qt FRAME coords — top-left INCLUDING the title bar) but the restore side
// calls setGeometry() (Qt CLIENT coords — excludes the frame). On Windows that
// mismatch set the client top to the old frame top, so the title bar climbed
// up by one title-bar height on every cold start until the min/max/close
// buttons walked off the top of the screen and became unreachable. Centering
// on the saved SIZE sidesteps the frame/client mismatch entirely.
//
// The client top is kept at least kTitleAllow below the work-area top, so the
// frame's title bar (which sits ABOVE the client rect setGeometry positions)
// is never clipped — even at large sizes or 150 % display scaling.
static QRect centeredWindowRect(int wWant, int hWant) {
    const QRect avail = (QGuiApplication::primaryScreen()
                             ? QGuiApplication::primaryScreen()->availableGeometry()
                             : QRect(0, 0, 1280, 800));
    const int kTitleAllow = 48;  // worst-case Windows title bar incl. scaling
    int w = qBound(400, wWant, qMax(400, avail.width()  - 40));
    int h = qBound(300, hWant, qMax(300, avail.height() - kTitleAllow));
    int x = avail.x() + (avail.width() - w) / 2;
    int y = qMax(avail.y() + kTitleAllow, avail.y() + (avail.height() - h) / 2);
    return clampWindowToScreens(QRect(x, y, w, h));
}

MainWindow::MainWindow(bool standaloneNoSession)
    : m_standaloneNoSession(standaloneNoSession) {
    setWindowTitle("new 1 - " NOTEPATRA_FLAVOR_NAME);
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

    // File explorer (hidden by default — only shown when Coding Mode
    // is active inside the AI dock). v0.1.70 — moved from the main
    // splitter (where it sat at index 0 on the far left) to live INSIDE
    // the AI dock host as the left half of an internal splitter. User
    // explicitly asked: "left panel should be inside the AI mode not
    // outside" (red-arrow screenshot showing tree → AI dock).
    m_explorer = new FileExplorer;
    m_explorer->setMinimumWidth(180);
    m_explorer->setMaximumWidth(400);
    m_explorer->setVisible(false);
    // NOTE: parenting happens below inside the aiDockHost construction.

    connect(m_explorer, &FileExplorer::fileOpenRequested, this, &MainWindow::openFile);
    // v0.1.61 — restore the user's hidden-paths set from Config, and
    // persist any changes the user makes via the right-click menu.
    m_explorer->setHiddenPaths(Config::instance().explorerHiddenPaths);
    connect(m_explorer, &FileExplorer::hiddenPathsChanged, this,
            [](const QStringList &paths) {
        Config::instance().explorerHiddenPaths = paths;
        Config::instance().save();
    });

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
    // v0.1.70 — lowered from 320 → 200 so a true 20% dock-width fits on
    // typical windows. The earlier 320 min silently bumped Qt's setSizes
    // up, distorting the user-requested 80/20 ratio to ~72/28 on a
    // 1140px window. 200 gives the dock at least the activity strip
    // (32px) + chat input (~150px) on the narrowest reasonable window.
    m_aiDockHost->setMinimumWidth(200);
    // v0.1.42 — removed setMaximumWidth(640). The hard 640 cap stopped
    // users from dragging the QSplitter handle past that width on
    // Windows + macOS, where it presented as "manual resize doesn't
    // work" (the splitter would refuse to widen the dock further).
    // The QSplitter parent still enforces a sane minimum on the editor
    // pane, so removing the max here just lets users size the chat as
    // wide as their screen allows.
    // v0.1.70 — dock host is now a horizontal layout: [activity strip |
    // internal split [tree | AI panel]]. The activity strip is a thin
    // VS-Code-style icon column. Currently it holds one 📁 toggle for the
    // file tree; future icons (search-in-AI-context, git status, etc.)
    // can slot in below.
    auto *aiDockLayout = new QHBoxLayout(m_aiDockHost);
    aiDockLayout->setContentsMargins(0, 0, 0, 0);
    aiDockLayout->setSpacing(0);
    m_aiDockPanel = new AIPanel;

    // Activity strip — fixed 32px wide, full height. Holds:
    //   • [+] menu button at the top (SSMS-style "Add…" affordance —
    //     new file, open file, open folder, add database connection).
    //   • 📁 file-tree toggle with a vertical "FILES" label below it
    //     (JetBrains-style rotated tool-window label).
    auto *activityStrip = new QWidget;
    activityStrip->setFixedWidth(32);
    activityStrip->setObjectName("aiActivityStrip");
    activityStrip->setStyleSheet(
        "#aiActivityStrip { background: rgba(0,0,0,0.05); "
        "border-right: 1px solid rgba(0,0,0,0.08); }");
    auto *stripLayout = new QVBoxLayout(activityStrip);
    stripLayout->setContentsMargins(2, 6, 2, 6);
    stripLayout->setSpacing(6);

    // v0.1.70 — the standalone `+` button on the activity strip is removed.
    // User clarification: the SSMS-style `+/-` belongs on the TREE BRANCH
    // indicators (next to each folder, like SSMS Object Explorer shows for
    // databases / tables / views), not on the strip. See FileExplorer's
    // SsmsBranchStyle for that change. Activity strip now only hosts the
    // folder-tree toggle + vertical FILES label.
    m_explorerToggleBtn = new QToolButton;
    m_explorerToggleBtn->setIcon(style()->standardIcon(QStyle::SP_DirIcon));
    m_explorerToggleBtn->setIconSize(QSize(18, 18));
    m_explorerToggleBtn->setCheckable(true);
    m_explorerToggleBtn->setFixedSize(28, 28);
    m_explorerToggleBtn->setToolTip(
        tr("Toggle File Explorer · visible inside the AI dock when on (Ctrl+Shift+E)"));
    m_explorerToggleBtn->setCursor(Qt::PointingHandCursor);
    m_explorerToggleBtn->setStyleSheet(
        "QToolButton { background: transparent; border: none; "
        "border-radius: 4px; padding: 2px; }"
        "QToolButton:hover { background: rgba(0,0,0,0.10); }"
        "QToolButton:checked { background: rgba(78,201,176,0.30); }");
    connect(m_explorerToggleBtn, &QToolButton::clicked, this, [this](bool checked) {
        if (!m_explorer) return;
        m_explorer->setVisible(checked);
        // v0.1.70 — main splitter must re-balance because dock width
        // depends on tree visibility (60/40 when tree shown, 80/20 when
        // hidden). Order: setVisible above must run first so the
        // rebalance reads the correct treeVisible state.
        rebalanceAiDockSplit();
        if (m_aiDockInternalSplit) {
            const int dockW = m_aiDockInternalSplit->size().width();
            if (checked) {
                const int treeW = 220;
                const int chatW = std::max(280, dockW - treeW);
                m_aiDockInternalSplit->setSizes({treeW, chatW});
            } else {
                m_aiDockInternalSplit->setSizes({0, std::max(1, dockW)});
            }
        }
    });
    stripLayout->addWidget(m_explorerToggleBtn, 0, Qt::AlignHCenter);

    // Vertical rotated "FILES" label under the folder icon —
    // JetBrains-style tool-window rail label. Uses a custom-paint QLabel.
    auto *filesVLabel = new VerticalLabel(tr("FILES"));
    filesVLabel->setStyleSheet("color: rgba(0,0,0,0.55); font-size: 10px; "
                               "font-weight: 600; letter-spacing: 1px;");
    filesVLabel->setCursor(Qt::PointingHandCursor);
    filesVLabel->setToolTip(tr("Click to toggle file explorer"));
    // Clicking the rotated label is a shortcut for toggling the explorer.
    filesVLabel->setClickHandler([this]() {
        if (m_explorerToggleBtn) {
            m_explorerToggleBtn->setChecked(!m_explorerToggleBtn->isChecked());
            emit m_explorerToggleBtn->clicked(m_explorerToggleBtn->isChecked());
        }
    });
    stripLayout->addWidget(filesVLabel, 0, Qt::AlignHCenter);

    stripLayout->addStretch(1);

    aiDockLayout->addWidget(activityStrip);

    // Internal horizontal split: [file tree | AI panel]. File tree is
    // hidden (zero width) by default; activity strip button + coding-mode
    // signal both manage its visibility.
    auto *dockSplit = new QSplitter(Qt::Horizontal);
    dockSplit->setHandleWidth(4);
    dockSplit->setChildrenCollapsible(false);
    dockSplit->addWidget(m_explorer);     // index 0 — file tree (hidden by default)
    dockSplit->addWidget(m_aiDockPanel);  // index 1 — AI chat surface
    dockSplit->setStretchFactor(0, 0);    // file tree takes a fixed-ish width
    dockSplit->setStretchFactor(1, 1);    // AI chat takes the rest
    dockSplit->setSizes({0, 1000});       // start with tree collapsed
    aiDockLayout->addWidget(dockSplit, 1);
    m_aiDockInternalSplit = dockSplit;
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
        if (!m_explorer) return;
        // v0.1.70 — Coding mode auto-shows the file explorer INSIDE the
        // AI dock; chat / data hide it. The activity-strip toggle button
        // mirrors the same state so the user can manually flip later.
        m_explorer->setVisible(on);
        if (m_explorerToggleBtn) m_explorerToggleBtn->setChecked(on);

        // Main splitter — tree-visible needs more dock width (60/40)
        // than tree-hidden (80/20). Re-balance based on the new state.
        rebalanceAiDockSplit();

        // Internal dock split: allocate ~220px for the file tree, give the
        // rest to the AI chat. When mode flips off, collapse tree to 0.
        if (m_aiDockInternalSplit) {
            const int dockW = m_aiDockInternalSplit->size().width();
            if (on) {
                const int treeW = 220;
                const int chatW = std::max(280, dockW - treeW);
                m_aiDockInternalSplit->setSizes({treeW, chatW});
            } else {
                m_aiDockInternalSplit->setSizes({0, std::max(1, dockW)});
            }
        }

        // v0.1.70 — VS Code-style "Open Folder / Open File / Skip" picker.
        // When the user enters Coding mode for the first time in this
        // session AND no workspace is set, prompt them to pick a folder
        // (or file) so the AI has something concrete to read/edit. One-
        // shot per session via m_codingFolderPromptShown — re-entering
        // Coding mode after dismissing doesn't re-pester.
        // Fire ONLY when the user genuinely has nothing to work on: no folder
        // opened AND not a single file-backed tab anywhere.
        //
        // The tab scan matters: with a Terminal or Welcome tab focused, any
        // "current file" check reads empty even though real files are open one
        // tab over, and the dialog would contradict itself by demanding "a
        // folder or file" while both are already there.
        //
        // Worth stating plainly: this prompt has never once fired in a shipped
        // build. Its old gate was FileExplorer::rootPath().isEmpty(), and that
        // defaults to $HOME, so the condition was permanently false. Fixing the
        // workspace-root bug is what woke it up.
        bool anyFileOpen = false;
        for (int i = 0; m_tabs && i < m_tabs->count() && !anyFileOpen; ++i)
            if (auto *ed = m_tabs->editorAt(i))
                anyFileOpen = !ed->filePath().isEmpty();

        if (on && !m_codingFolderPromptShown && !anyFileOpen
            && workspaceFolder().isEmpty()) {
            m_codingFolderPromptShown = true;
            QMessageBox box(this);
            box.setWindowTitle(tr("Coding Mode — open something to work on"));
            box.setIcon(QMessageBox::Information);
            box.setTextFormat(Qt::RichText);
            box.setText(tr(
                "<b>Coding Mode</b> works best with a folder or file open "
                "so the AI can read, edit, and run git operations against "
                "real workspace files."));
            box.setInformativeText(tr(
                "Pick one — you can always change later:"));
            QPushButton *openFolderBtn = box.addButton(
                tr("Open Folder…"), QMessageBox::AcceptRole);
            QPushButton *openFileBtn = box.addButton(
                tr("Open File…"), QMessageBox::AcceptRole);
            QPushButton *skipBtn = box.addButton(
                tr("Skip — continue without a workspace"),
                QMessageBox::RejectRole);
            box.setDefaultButton(openFolderBtn);
            box.exec();
            QAbstractButton *clicked = box.clickedButton();
            if (clicked == openFolderBtn) {
                const QString p = QFileDialog::getExistingDirectory(this,
                    tr("Open Folder as Workspace"), QDir::homePath());
                if (!p.isEmpty()) {
                    m_explorer->setRoot(p);
                }
            } else if (clicked == openFileBtn) {
                const QStringList paths = QFileDialog::getOpenFileNames(this,
                    tr("Open File(s)"), QDir::homePath());
                for (const QString &f : paths) openFile(f);
            }
            // skipBtn or close-window → no-op; user stays in Coding mode
            // without a workspace. AI tools that need one will say so.
        }
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
    // v0.1.56 — fullscreen toggle for the AI dock. When the user clicks the
    // ⛶ button in the panel header, hide every sibling widget in the
    // splitter (file explorer, editor tabs) and resize the AI dock to take
    // the whole window. Click again to restore. Splitter sizes + sibling
    // visibility are saved at expand-time so restore is exact. Useful for
    // heavy data-analyst sessions where the chat is the primary surface.
    // v0.1.73 — red ✕ close button inside the dock now routes through the
    // canonical hide handler.  Pre-v0.1.73 the button called
    // parentWidget()->setVisible(false) directly, which left
    // Config::aiDockVisible + toolbar button state out of sync AND
    // sometimes squashed the splitter slot to 0 so subsequent re-shows
    // (via the toolbar) opened a 0-width "blank" dock.  Now AIPanel
    // emits closeDockRequested() and we drive the same path the toolbar
    // does.
    connect(m_aiDockPanel, &AIPanel::closeDockRequested, this, [this]() {
        setAiDockVisible(false);
    });

    connect(m_aiDockPanel, &AIPanel::fullscreenToggled, this, [this](bool on) {
        if (!m_splitter || !m_aiDockHost) return;
        if (on) {
            // v0.1.70 — Only save siblings the FIRST time we enter fullscreen.
            // Coding → Data (and back) re-fires fullscreenToggled(true) while
            // already in fullscreen; if we cleared + re-saved here, we'd
            // overwrite the original pre-fullscreen state with the (now-
            // hidden) current state — so the subsequent fullscreen-off
            // restore would leave siblings hidden. Reproducer:
            // test_ai_fullscreen_exit S11.
            if (m_aiSavedSiblingVisibility.isEmpty()) {
                m_aiSavedSplitterSizes = m_splitter->sizes();
                for (int i = 0; i < m_splitter->count(); ++i) {
                    QWidget *w = m_splitter->widget(i);
                    if (w == m_aiDockHost) continue;
                    m_aiSavedSiblingVisibility.insert(w, w->isVisible());
                }
            }
            // Hide all siblings (idempotent — they may already be hidden
            // from a prior fullscreen-toggled(true) within the same session).
            for (int i = 0; i < m_splitter->count(); ++i) {
                QWidget *w = m_splitter->widget(i);
                if (w == m_aiDockHost) continue;
                w->setVisible(false);
            }
            // Make sure the AI dock is visible AND takes 100 % of the splitter.
            m_aiDockHost->setVisible(true);
            QList<int> sizes;
            const int total = m_splitter->size().width();
            for (int i = 0; i < m_splitter->count(); ++i) {
                sizes.append(m_splitter->widget(i) == m_aiDockHost ? total : 0);
            }
            m_splitter->setSizes(sizes);
        } else {
            // Restore sibling visibility BEFORE applying saved sizes — Qt
            // ignores size hints for hidden widgets and would silently drop
            // their slots back to 0.
            for (auto it = m_aiSavedSiblingVisibility.constBegin();
                 it != m_aiSavedSiblingVisibility.constEnd(); ++it) {
                if (it.key()) it.key()->setVisible(it.value());
            }
            if (!m_aiSavedSplitterSizes.isEmpty()) {
                m_splitter->setSizes(m_aiSavedSplitterSizes);
            }
            m_aiSavedSiblingVisibility.clear();
            m_aiSavedSplitterSizes.clear();
        }
    });

    // v0.1.70 — AI dock ALWAYS starts hidden on app launch, regardless of
    // saved Config::aiDockVisible. User UX rule: "AI cannot start on new
    // app start only when AI assistant is clicked". The user opens it via
    // Ctrl+Q / the AI toolbar icon / Tools menu. Config is synced to false
    // here so the toggle logic stays consistent (next toggle flips to true).
    m_aiDockHost->setVisible(false);
    Config::instance().aiDockVisible = false;
    m_splitter->addWidget(m_aiDockHost);

    connect(m_tabs, &QTabWidget::tabCloseRequested, this, &MainWindow::closeTab);
    connect(m_tabs, &QTabWidget::currentChanged, this, [this](int) {
        if (Editor *ed = m_tabs->currentEditor()) m_lastEditor = ed;
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
        // v0.1.68 — if the user (or anything other than newFile/openFile)
        // switched the active tab while the AI dock is fullscreen, the new
        // active tab is currently hidden behind the dock. Exit fullscreen
        // so the switch actually becomes visible. newFile/openFile set
        // m_skipAiAutoExitOnNextTabChange before their setCurrentIndex
        // so this branch is skipped during the v0.1.61 background-tab
        // create flow.
        if (m_skipAiAutoExitOnNextTabChange) {
            m_skipAiAutoExitOnNextTabChange = false;
        } else {
            exitAiFullscreenIfActive();
        }
    });
    connect(m_tabs, &TabManager::tabContextNew, this, [this]() { newFile(); });
    connect(m_tabs, &TabManager::tabContextClose, this, [this](int idx) { closeTab(idx); });
    connect(m_tabs, &TabManager::tabContextCloseAll, this, [this]() {
        closeAllTabs();
    });
    connect(m_tabs, &TabManager::tabContextCloseOthers, this, [this](int keep) {
        // Track the kept tab by widget, not index — each closeTab() above
        // it shifts `keep` down and the stale index closed the wrong tab.
        QWidget *keepW = m_tabs->widget(keep);
        for (int i = m_tabs->count() - 1; i >= 0; i--)
            if (m_tabs->widget(i) != keepW) closeTab(i);
    });
    connect(m_tabs, &TabManager::tabContextCloseLeft, this, [this](int idx) {
        for (int i = idx - 1; i >= 0; i--) closeTab(i);
    });
    connect(m_tabs, &TabManager::tabContextCloseRight, this, [this](int idx) {
        for (int i = m_tabs->count() - 1; i > idx; i--) closeTab(i);
    });
    connect(m_tabs, &TabManager::tabContextSave, this, [this](int idx) {
        auto *ed = m_tabs->editorAt(idx);
        if (ed && !ed->filePath().isEmpty()) {
            if (!ed->saveFile()) {
                QMessageBox::warning(this, QStringLiteral("Save failed"),
                    QStringLiteral("Could not write to:\n\n%1")
                        .arg(QDir::toNativeSeparators(ed->filePath())));
                return;
            }
            updateTabTitle(idx);
        } else {
            saveFileAs();
        }
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
            m_tabs->setTabToolTip(idx, QDir::toNativeSeparators(newPath));
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

    // D3 — debounced word-count recompute; one linear pass, per-editor cache.
    m_wordCountTimer = new QTimer(this);
    m_wordCountTimer->setSingleShot(true);
    m_wordCountTimer->setInterval(300);
    connect(m_wordCountTimer, &QTimer::timeout, this, [this]() {
        if (auto *e = currentEditor())
            m_statusBar->updateWords(e->recomputeWordCount());
    });

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

    // Restore window geometry from config. v0.1.94 — every restore path
    // goes through clampWindowToScreens() so a previous session on a now-
    // disconnected monitor cannot leave us invisible.
    {
        auto &cfg = Config::instance();
        if (cfg.maximized) {
            showMaximized();
        } else if (cfg.windowW > 100 && cfg.windowH > 100) {
            // v0.1.100 — center on the saved SIZE rather than restoring the
            // saved x/y. See centeredWindowRect(): the saved x()/y() are FRAME
            // coords but setGeometry() takes CLIENT coords, so restoring them
            // walked the title bar off the top of the screen on Windows.
            setGeometry(centeredWindowRect(cfg.windowW, cfg.windowH));
        } else {
            // First launch / no saved size — a modest, clearly-NOT-maximized
            // centered default the user can maximize themselves.
            setGeometry(centeredWindowRect(1100, 760));
        }
    }

    // v0.1.71 — AI interaction log housekeeping. Prune rows older than
    // 7 days + enforce the 50 MB size cap on startup. Cheap (millisecond)
    // SQLite DELETE; no-op when the user has opted out of logging.
    AiInteractionLog::pruneOld();

    // D1 — session restore + CLI opens are deferred to runStartupNow() (0 ms
    // timer) so the window is shown and painting before any heavy file work.
    if (!m_standaloneNoSession && QFileInfo::exists(sessionFilePath()))
        m_statusBar->updateLanguage(tr("Restoring session..."));
    QTimer::singleShot(0, this, &MainWindow::runStartupNow);

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

    // Noter reminders are app-lifetime. One stat() decides — users who never
    // opened Noter pay nothing and get zero Documents/ side effects (the
    // service ctor path would mkpath .notepatra). The 1.5s delay lets the
    // window paint before a catch-up digest can pop. If the user opens Noter
    // within the window, ensureNoterTab ran the service first and this
    // no-ops via the idempotence guard.
    QTimer::singleShot(1500, this, [this]() {
        if (QFileInfo::exists(NotesPanel::todosDbPath()))
            ensureNoterReminderService();
    });

    // Auto-save session every autoSaveIntervalSec seconds (default 5,
    // bounded [1s, 300s]). saveSession() now writes full
    // unsaved-buffer content into session.json, so the legacy recovery_*.txt
    // pile is no longer needed — autoSaveRecovery() removed from this tick.
    m_autoSaveTimer = new QTimer(this);
    connect(m_autoSaveTimer, &QTimer::timeout, this, [this]() {
        saveSession();       // no-ops in standalone mode
        checkFileChanges();
        // Persist window geometry + config. Standalone windows skip it —
        // their throwaway geometry must not clobber the primary's.
        if (!m_standaloneNoSession) {
            auto &cfg = Config::instance();
            cfg.windowX = x(); cfg.windowY = y();
            cfg.windowW = width(); cfg.windowH = height();
            cfg.maximized = isMaximized();
            cfg.save();
        }
    });
    m_autoSaveTimer->start(qBound(1, Config::instance().autoSaveIntervalSec, 300) * 1000);

    // File change watcher — detects external modifications
    setupFileWatcher();

    // v0.1.118 — MCP bridge: editor access for the AI sidecar over a
    // dedicated QLocalServer (single-instance name + "-mcp"). Read verbs
    // answer directly; write verbs run only after the human approves the
    // bridge's in-window card. Primary only; standalone windows never serve it.
    if (!m_standaloneNoSession) {
        // Capture the launch directory now (before anything can chdir): the
        // git verbs use it as a last-resort repo root so an agent that starts
        // Notepatra from inside a checkout still gets git even with no folder
        // open and only untitled tabs (issue #3).
        m_startupCwd = QDir::currentPath();
        McpEditorHost host;
        host.tabCount = [this] { return m_tabs->count(); };
        host.tabTitle = [this](int i) { return m_tabs->tabText(i); };
        host.tabPath = [this](int i) {
            auto *ed = m_tabs->editorAt(i);
            return ed ? ed->filePath() : QString();
        };
        host.tabModified = [this](int i) {
            auto *ed = m_tabs->editorAt(i);
            return ed && ed->isModified();
        };
        host.tabText = [this](int i) {
            auto *ed = m_tabs->editorAt(i);
            return ed ? ed->text() : QString();
        };
        // v0.1.121 (issue #1): only tabs backed by a real Editor are editable
        // text buffers. editorAt() is nullptr for the Welcome page, a Diagram
        // canvas, and the Noter panel — exactly the tabs an agent must not
        // read_tab / insert_text / save_tab against.
        host.tabEditable = [this](int i) {
            return m_tabs->editorAt(i) != nullptr;
        };
        // v0.1.126 (NP-13): identity that outlives a reorder.
        //
        // The tab WIDGET is the document — it survives reordering, save-as and
        // its neighbours closing, all of which move the index. Stamp a
        // monotonic id on it the first time anyone asks, and the id is stable
        // for that document's lifetime. Ids are never reused: a reopened file
        // is a new tab and correctly gets a new one.
        host.tabId = [this, nextTabId = std::make_shared<qint64>(1)](
                         int i) -> qint64 {
            QWidget *w = m_tabs->widget(i);
            if (!w) return -1;
            static const char kProp[] = "npMcpTabId";
            const QVariant existing = w->property(kProp);
            if (existing.isValid()) return existing.toLongLong();
            const qint64 assigned = (*nextTabId)++;
            w->setProperty(kProp, assigned);
            return assigned;
        };
        host.openFile = [this](const QString &p) {
            openFile(p);   // same internal path the single-instance handoff uses
            const QString abs = QFileInfo(p).absoluteFilePath();
            for (int i = 0; i < m_tabs->count(); ++i) {
                auto *ed = m_tabs->editorAt(i);
                if (ed && ed->filePath() == abs) return i;
            }
            return -1;
        };
        host.selection = [this](int *tabIndex) {
            if (tabIndex) *tabIndex = m_tabs->currentIndex();
            auto *ed = currentEditor();
            return ed ? ed->selectedText() : QString();
        };
        // STRICT folder-only. NOT a current-file fallback: with a single file
        // open in $HOME that fallback resolves to $HOME, and search_project
        // walks the user's entire profile — the exact leak this release fixes,
        // reached through a one-file door instead of a no-file door.
        host.workspaceRoot = [this] { return workspaceFolder(); };
        // ── v0.1.118 expansive wave — every lambda routes through the SAME
        //    code path the equivalent menu/status-bar surface uses. ──
        host.currentTabIndex = [this] { return m_tabs->currentIndex(); };
        host.tabLanguage = [this](int i) {
            auto *ed = m_tabs->editorAt(i);
            return ed ? ed->language() : QString();
        };
        host.tabEncoding = [this](int i) {
            auto *ed = m_tabs->editorAt(i);
            return ed ? ed->encoding() : QString();
        };
        host.cursorPosition = [this](int *line, int *col) {
            int l = 0, c = 0;
            if (auto *ed = currentEditor()) {
                ed->getCursorPosition(&l, &c);
                ++l; ++c; // status-bar convention: 1-based
            }
            if (line) *line = l;
            if (col) *col = c;
        };
        host.recentFiles = [] { return Config::instance().recentFiles; };
        host.newTab = [this](const QString &text) {
            Editor *ed = newFile(); // same path as Ctrl+N
            if (!ed) return -1;
            if (!text.isEmpty()) {
                Editor::ScopedBulkLoad bulk(ed); // no change-history churn
                ed->setText(text);
            }
            return m_tabs->currentIndex(); // newFile() focuses the new tab
        };
        host.gotoLine = [this](int tabIndex, int line) -> int {
            if (tabIndex < 0 || tabIndex >= m_tabs->count()) return -1;
            auto *ed = m_tabs->editorAt(tabIndex);
            if (!ed) return -1;
            m_tabs->setCurrentIndex(tabIndex);
            // 1-based; clamps to the buffer and ensures the line is visible.
            const int landed = ed->gotoLine(line);
            ed->setFocus();
            return landed;
        };
        // v0.1.121 (issue #5): move the selection to a 1-based range. Cols are
        // clamped to each line's text length (EOL excluded) so an over-long
        // col lands at line end, never on the next line.
        host.selectRange = [this](int tabIndex, int startLine, int startCol,
                                  int endLine, int endCol) {
            auto *ed = m_tabs->editorAt(tabIndex);
            if (!ed) return false;
            const int lineCount = ed->lines();
            const int lineFrom = startLine - 1;
            const int lineTo = endLine - 1;
            if (lineFrom < 0 || lineFrom >= lineCount || lineTo < 0 ||
                lineTo >= lineCount)
                return false;
            auto clampCol = [ed](int line, int col1based) {
                const QString t = ed->text(line);
                int len = t.size();
                while (len > 0 &&
                       (t.at(len - 1) == QLatin1Char('\n') ||
                        t.at(len - 1) == QLatin1Char('\r')))
                    --len;
                return qBound(0, col1based - 1, len);
            };
            m_tabs->setCurrentIndex(tabIndex);
            ed->setSelection(lineFrom, clampCol(lineFrom, startCol), lineTo,
                             clampCol(lineTo, endCol));
            ed->setFocus();
            return true;
        };
        host.setLanguage = [this](int tabIndex, const QString &lang) {
            auto *ed = m_tabs->editorAt(tabIndex);
            if (!ed) return false;
            // Validate through the same factory the Language menu feeds.
            QsciLexer *probe = createLexerForLanguage(lang, nullptr);
            if (!probe) return false;
            delete probe;
            ed->setLanguage(lang);
            if (m_tabs->currentIndex() == tabIndex)
                m_statusBar->updateLanguage(lang);
            return true;
        };
        host.compareTabs = [this](int a, int b) {
            // Same code path as Tools → Compare Two Open Tabs, with
            // explicit indices instead of current/next.
            if (a < 0 || b < 0 || a >= m_tabs->count() ||
                b >= m_tabs->count() || a == b)
                return false;
            auto *left = m_tabs->editorAt(a);
            auto *right = m_tabs->editorAt(b);
            if (!left || !right) return false;
            const QString leftName = left->filePath().isEmpty()
                ? m_tabs->tabText(a)
                : QFileInfo(left->filePath()).fileName();
            const QString rightName = right->filePath().isEmpty()
                ? m_tabs->tabText(b)
                : QFileInfo(right->filePath()).fileName();
            auto *dlg = new CompareDialog(left->text(), leftName,
                                          right->text(), rightName, this);
            dlg->setAttribute(Qt::WA_DeleteOnClose);
            dlg->show(); // non-modal, user closes it — visibly reversible
            return true;
        };
        host.formatText = [](const QString &kind, const QString &text,
                             QString *errorOut) -> QString {
            // Pure function over the SAME rustbridge calls fmtpanel /
            // sqlfmtpanel use. Never touches any buffer.
            QString out;
            try {
                if (kind == QLatin1String("json")) {
                    // v0.1.126 (NP-09): validate BEFORE formatting, on the MCP
                    // path only.
                    //
                    // RustCore::formatJson is the JSON panel's auto-fixer: it
                    // closes unbalanced brackets, strips trailing commas and
                    // quotes bare keys. In the panel a human sees that happen
                    // and can undo it. Over MCP nobody does — so `[1,2` came
                    // back as `[1,2]` with isError:false, and an agent that
                    // formatted a truncated file then wrote it back had
                    // fabricated data the user never had. The tool's own
                    // description has always said "Fails on invalid JSON";
                    // this makes that true. The panel keeps its fixer.
                    const QString parseErr = RustCore::jsonParseError(text);
                    if (!parseErr.isEmpty()) {
                        if (errorOut)
                            *errorOut =
                                QStringLiteral("invalid JSON: %1").arg(parseErr);
                        return QString();
                    }
                    out = RustCore::formatJson(text);
                }
                else if (kind == QLatin1String("sql"))
                    out = RustCore::formatSql(text);
                else if (kind == QLatin1String("html"))
                    out = RustCore::formatHtml(text);
            } catch (const std::exception &e) {
                if (errorOut)
                    *errorOut = QString::fromUtf8(e.what());
                return QString();
            } catch (...) {
                if (errorOut) *errorOut = QStringLiteral("formatter failed");
                return QString();
            }
            if (out.isEmpty() && errorOut)
                *errorOut = QStringLiteral("formatter returned empty output");
            return out;
        };
        host.notesRoot = [] { return NotesPanel::defaultNotesFolder(); };
        // ── Phase 0A — language SSOT surface for the MCP bridge. ──
        host.knownLanguages = [] { return allKnownLanguageTokens(); };
        host.resolveLanguage = [](const QString &in) {
            return resolveLanguageToken(in);
        };
        // ── v0.1.118 WRITE tier — each lambda reuses the SAME Editor/save
        //    path the equivalent menu action uses, wrapped in one undo step.
        //    The bridge calls them only after the human clicks Approve. ──
        host.approvalParent = [this]() -> QWidget * { return this; };
        host.hasSelection = [this](int i) {
            auto *ed = m_tabs->editorAt(i);
            return ed && ed->hasSelectedText();
        };
        host.insertText = [this](int i, int line, int col,
                                 const QString &text) {
            auto *ed = m_tabs->editorAt(i);
            if (!ed) return false;
            ed->beginUndoAction();
            if (line >= 1) {
                if (line - 1 >= ed->lines()) {
                    ed->endUndoAction();
                    return false;
                }
                // Clamp col to the line's text length (EOL chars excluded)
                // so an over-long col lands at line end, never on the next
                // line past the \n.
                const QString lineText = ed->text(line - 1);
                int len = lineText.size();
                while (len > 0 && (lineText.at(len - 1) == QLatin1Char('\n') ||
                                   lineText.at(len - 1) == QLatin1Char('\r')))
                    --len;
                ed->insertAt(text, line - 1, qBound(0, col - 1, len));
            } else {
                ed->insert(text); // at cursor
            }
            ed->endUndoAction();
            return true;
        };
        host.replaceSelection = [this](int i, const QString &text) {
            auto *ed = m_tabs->editorAt(i);
            if (!ed || !ed->hasSelectedText()) return false;
            ed->beginUndoAction();
            ed->replaceSelectedText(text);
            ed->endUndoAction();
            return true;
        };
        host.applyEdit = [this](int i, const QString &find,
                                const QString &replace, bool all) {
            auto *ed = m_tabs->editorAt(i);
            if (!ed) return -1;
            int count = 0;
            ed->beginUndoAction();
            // Literal (non-regex), case-sensitive, NO wrap: the scan always
            // terminates at EOF even when `replace` contains `find`.
            bool found = ed->findFirst(find, false, true, false, false, true,
                                       0, 0, false);
            while (found) {
                ed->replace(replace);
                ++count;
                if (!all) break;
                found = ed->findNext();
            }
            ed->endUndoAction();
            return count;
        };
        host.saveTab = [this](int i) {
            auto *ed = m_tabs->editorAt(i);
            // Untitled tabs are refused by the bridge before this runs; the
            // re-check keeps a Save As dialog impossible on any path.
            if (!ed || ed->filePath().isEmpty()) return false;
            if (!ed->saveFile()) return false;
            updateTabTitle(i);
            ed->updateGitGutter();
            if (m_fileWatcher) {
                const QString path = ed->filePath();
                m_fileTimestamps[path] = QFileInfo(path).lastModified();
            }
            return true;
        };
        // v0.1.121 (issue #4): "Save As" to an explicit path the bridge has
        // already validated (absolute, parent folder exists). Same headless
        // path as host.saveTab — Editor::saveFile(path) — never a dialog.
        host.saveTabAs = [this](int i, const QString &path) {
            auto *ed = m_tabs->editorAt(i);
            if (!ed || path.isEmpty()) return false;
            if (!ed->saveFile(path)) return false;
            updateTabTitle(i);
            ed->updateGitGutter();
            if (m_fileWatcher) {
                const QString saved = ed->filePath();
                m_fileTimestamps[saved] = QFileInfo(saved).lastModified();
            }
            return true;
        };
        // ── v0.1.119 depth wave — each lambda reuses the SAME real code path
        //    the equivalent feature uses (git_tools, DbConnections classifier
        //    + runQuery, NotesStorage/NotesTodos, DiagramView export). ──
        // READ: Noter reminders (unbucketed; the bridge buckets them).
        host.reminders = [this]() -> QJsonArray {
            ensureNoterReminderService(); // idempotent; guarantees m_noterTodos
            QJsonArray arr;
            if (!m_noterTodos) return arr;
            const QVector<TodoRow> rows = m_noterTodos->allScheduledReminders();
            for (const TodoRow &r : rows) {
                if (!r.reminderAt.isValid()) continue;
                QJsonObject o;
                o[QStringLiteral("note_file")] =
                    QDir::toNativeSeparators(r.sourceFile);
                o[QStringLiteral("note_title")] =
                    r.meetingTitle.isEmpty()
                        ? QFileInfo(r.sourceFile).fileName()
                        : r.meetingTitle;
                o[QStringLiteral("due_iso")] =
                    r.reminderAt.toUTC().toString(Qt::ISODate);
                arr.append(o);
            }
            return arr;
        };
        // READ: read-only git — reuses git_tools.cpp (no new QProcess path).
        host.runGit = [this](const QString &sub, const QJsonObject &args,
                             QString *err) -> QString {
            // Candidate roots: the workspace (Explorer) folder first, then the
            // current file's directory — so git "just works" on an open repo
            // file even when the workspace folder isn't itself a repository.
            QStringList roots;
            const QString wsRoot = workspaceFolder();
            if (!wsRoot.isEmpty()) roots << wsRoot;
            if (auto *ed = currentEditor())
                if (!ed->filePath().isEmpty()) {
                    const QString fdir =
                        QFileInfo(ed->filePath()).absolutePath();
                    if (!roots.contains(fdir)) roots << fdir;
                }
            // Last resort (issue #3): the process launch directory, so git
            // still resolves when no folder is open and every tab is untitled
            // (an agent launched Notepatra from inside the repo). Appended
            // LAST — the workspace and the file's own directory always win.
            if (!m_startupCwd.isEmpty() && !roots.contains(m_startupCwd))
                roots << m_startupCwd;
            if (roots.isEmpty()) {
                if (err) *err = QStringLiteral("no workspace folder open");
                return QString();
            }
            QJsonObject a = args;
            if (sub == QLatin1String("log") &&
                a.contains(QLatin1String("limit")))
                a[QStringLiteral("max_count")] = a.value(QLatin1String("limit"));
            if (sub == QLatin1String("show") &&
                a.contains(QLatin1String("ref")))
                a[QStringLiteral("commit")] = a.value(QLatin1String("ref"));
            AiTools::ToolCall call;
            call.name = QStringLiteral("git_") + sub;
            call.args = a;
            auto runOnce = [&](const QString &root) -> AiTools::ToolResult {
                if (sub == QLatin1String("status"))
                    return GitTools::executeGitStatus(call, root);
                if (sub == QLatin1String("diff"))
                    return GitTools::executeGitDiff(call, root);
                if (sub == QLatin1String("log"))
                    return GitTools::executeGitLog(call, root);
                if (sub == QLatin1String("show"))
                    return GitTools::executeGitShow(call, root);
                if (sub == QLatin1String("branch"))
                    return GitTools::executeGitBranchList(call, root);
                AiTools::ToolResult e;
                e.isError = true;
                e.content =
                    QStringLiteral("{\"message\":\"unknown git subcommand\"}");
                return e;
            };
            // Workspace root wins when it is a repo; otherwise fall through to
            // the file's repo so an agent editing a repo file always gets git.
            AiTools::ToolResult res = runOnce(roots.first());
            for (int i = 1; i < roots.size() && res.isError; ++i)
                res = runOnce(roots.at(i));
            if (res.isError) {
                const QJsonObject body =
                    QJsonDocument::fromJson(res.content.toUtf8()).object();
                QString msg = body.value(QLatin1String("message")).toString();
                if (msg.isEmpty())
                    msg = res.errorKind.isEmpty()
                              ? QStringLiteral("git error")
                              : res.errorKind;
                if (err) *err = msg;
                return QString();
            }
            return res.content;
        };
        // READ: .npd source of a diagram tab (for validate_npd tab_index).
        host.diagramSource = [this](int i, QString *out) -> bool {
            auto *de = qobject_cast<DiagramEditor *>(m_tabs->widget(i));
            if (!de) return false;
            if (out) *out = de->npdText();
            return true;
        };
        // READ: SELECT-only SQL through the REAL classifier + runQuery.
        host.runSql = [this](const QString &sql, const QString &csvPath,
                             QString *err) -> QJsonObject {
            // restrictFilesystem=true: the MCP sandbox must never become an
            // arbitrary-file-read primitive via DuckDB's read_text/read_csv_auto/
            // read_blob/… table functions (all read-only SQL, so the base
            // classifier passes them). The desktop data-analyst calls classifySql
            // WITHOUT this flag, so a power user typing read_csv_auto('x.csv')
            // in the SQL console is unaffected.
            const DbConnections::SqlVerdict v =
                DbConnections::classifySql(sql, /*restrictFilesystem=*/true);
            if (!(v.singleStatement && v.readOnly)) {
                if (err)
                    *err = v.reason.isEmpty()
                        ? QStringLiteral("query is not read-only (SELECT only)")
                        : QStringLiteral("query rejected: %1").arg(v.reason);
                return QJsonObject();
            }
            DbConnections::Record rec;
            QString engine;
            if (!csvPath.isEmpty()) {
#ifdef NOTEPATRA_HAVE_DUCKDB
                // Confine csv_path to the workspace root the git verbs use
                // (FileExplorer::rootPath), via the SAME resolveSafePath helper:
                // canonicalize + symlink-escape rejection + credential deny-list.
                // Also require an EXISTING regular file (resolveSafePath already
                // rejects non-existent paths; the isFile() guard blocks a
                // directory canonicalizing cleanly). When no folder root is open
                // the root is empty and resolveSafePath rejects everything —
                // matching the git verbs' behavior.
                // Deliberately NOT the workspaceRoot fallback: csv sandbox root
                // stays folder-open-only.
                const QString wsRoot =
                    m_explorer ? m_explorer->workspaceRoot() : QString();
                QString csvCanon;
                if (!AiTools::resolveSafePath(csvPath, wsRoot, &csvCanon,
                                              nullptr)
                    || !QFileInfo(csvCanon).isFile()) {
                    if (err) *err = QStringLiteral("csv_path is not an allowed file");
                    return QJsonObject();
                }
                rec.driver = QStringLiteral("DUCKDB");
                rec.database = csvCanon;
                // Engine-level filesystem sandbox — the AUTHORITATIVE control
                // that stops this MCP path becoming an arbitrary-host-file-read
                // primitive. openDuckDbForRecord materializes the CSV into an
                // in-memory table (one read) then issues `SET
                // enable_external_access=false`, so the untrusted SQL can query
                // `data` but the DuckDB replacement scan (SELECT * FROM
                // '/etc/passwd') and read_text/read_csv_auto/read_blob/glob all
                // fail at the engine — the classifier denylist is now only
                // defense-in-depth.
                rec.sandboxFilesystem = true;
                engine = QStringLiteral("duckdb");
#else
                if (err)
                    *err = QStringLiteral(
                        "CSV queries require the Full edition (DuckDB)");
                return QJsonObject();
#endif
            } else {
                rec.driver = QStringLiteral("QSQLITE");
                rec.database = QStringLiteral(":memory:");
                engine = QStringLiteral("sqlite");
            }
            // Fetch one past the bridge's 200-row cap so truncation is exact;
            // allowMutation=false is the second, engine-level read-only gate.
            const DbConnections::QueryResult qr =
                DbConnections::runQuery(rec, sql, 201, false, nullptr);
            if (!qr.ok) {
                if (err)
                    *err = qr.error.isEmpty() ? QStringLiteral("query failed")
                                              : qr.error;
                return QJsonObject();
            }
            QJsonArray cols;
            for (const QString &c : qr.columns) cols.append(c);
            QJsonArray rows;
            for (const QVector<QString> &row : qr.rows) {
                QJsonArray jr;
                for (const QString &cell : row) jr.append(cell);
                rows.append(jr);
            }
            QJsonObject out;
            out[QStringLiteral("columns")] = cols;
            out[QStringLiteral("rows")] = rows;
            out[QStringLiteral("truncated")] = qr.truncated;
            out[QStringLiteral("engine")] = engine;
            return out;
        };
        // ACT: open a Noter note in the Noter tab (path already root-confined).
        host.openNote = [this](const QString &absFile,
                               QString *err) -> QString {
            NotesPanel *noter = ensureNoterTab();
            if (!noter) {
                if (err) *err = QStringLiteral("could not open Noter");
                return QString();
            }
            int idx = -1;
            findNoterPanel(&idx);
            if (idx >= 0) m_tabs->setCurrentIndex(idx);
            noter->openNoteFile(absFile);
            NotesStorage storage(NotesPanel::defaultNotesFolder());
            return storage.displayTitleForFile(absFile);
        };
        // WRITE: create a Noter note the way Noter does (Inbox, shell HTML).
        host.createNote = [this](const QString &title, const QString &body,
                                 QString *err) -> QString {
            const QString root = NotesPanel::defaultNotesFolder();
            const QString inbox = root + QStringLiteral("/Inbox");
            QDir().mkpath(inbox);
            NotesStorage storage(root);
            const QDateTime now = QDateTime::currentDateTime();
            QString html = storage.newNoteHtml(title, now, QStringList());
            html = mcpInjectNoteBody(html, body);
            html = NotesStorage::withTitleMeta(html, title);
            const QString stamp =
                now.toString(QStringLiteral("yyyy-MM-dd-hhmmss"));
            QString abs;
            for (int seq = 1; seq < 1000; ++seq) {
                abs = inbox + QLatin1Char('/') +
                      QStringLiteral("%1-noter-%2.html")
                          .arg(stamp)
                          .arg(seq, 2, 10, QLatin1Char('0'));
                if (!QFileInfo::exists(abs)) break;
            }
            QString saveErr;
            if (!storage.saveNote(abs, html, &saveErr)) {
                if (err)
                    *err = saveErr.isEmpty()
                               ? QStringLiteral("could not save note")
                               : saveErr;
                return QString();
            }
            if (NotesPanel *np = findNoterPanel()) np->refreshFromDisk();
            return QFileInfo(abs).absoluteFilePath();
        };
        // WRITE: append a paragraph to an existing note (root-confined).
        host.appendNote = [this](const QString &absFile, const QString &text,
                                 QString *err) -> bool {
            NotesStorage storage(NotesPanel::defaultNotesFolder());
            QString readErr;
            QString html = storage.readNote(absFile, &readErr);
            if (html.isEmpty()) {
                if (err)
                    *err = readErr.isEmpty()
                               ? QStringLiteral("could not read note")
                               : readErr;
                return false;
            }
            html = mcpInjectNoteBody(html, text);
            QString saveErr;
            if (!storage.saveNote(absFile, html, &saveErr)) {
                if (err)
                    *err = saveErr.isEmpty()
                               ? QStringLiteral("could not save note")
                               : saveErr;
                return false;
            }
            storage.invalidateTitleCache(absFile);
            if (NotesPanel *np = findNoterPanel()) np->refreshFromDisk();
            return true;
        };
        // WRITE: bind a reminder to a note exactly like the UI does.
        host.setReminder = [this](const QString &absFile, const QDateTime &due,
                                  QString *err) -> bool {
            ensureNoterReminderService();
            if (!m_noterTodos) {
                if (err) *err = QStringLiteral("reminder store unavailable");
                return false;
            }
            NotesStorage storage(NotesPanel::defaultNotesFolder());
            QString title = storage.displayTitleForFile(absFile);
            if (title.isEmpty()) title = QFileInfo(absFile).fileName();
            const QString rid =
                m_noterTodos->setNoteReminder(absFile, title, due);
            if (rid.isEmpty()) {
                if (err) *err = QStringLiteral("could not set reminder");
                return false;
            }
            if (NotesPanel *np = findNoterPanel()) np->refreshFromDisk();
            return true;
        };
        // WRITE: render an open .npd tab off-screen via DiagramView::exportTo.
        host.exportDiagram = [this](int i, const QString &path,
                                    const QString &format,
                                    QString *err) -> bool {
            auto *de = qobject_cast<DiagramEditor *>(m_tabs->widget(i));
            if (!de) {
                if (err) *err = QStringLiteral("not a diagram tab");
                return false;
            }
            DiagramView view; // off-screen; same route as npd_render_qt.cpp
            view.resize(1000, 800);
            view.setSource(de->npdText());
            if (!view.exportTo(format, path)) {
                if (err) *err = QStringLiteral("export failed");
                return false;
            }
            return true;
        };
        // ACT: create a Diagram tab via the same helper the menu uses.
        host.createDiagram = [this](const QString &source, const QString &title) {
            return newDiagramTab(source, title);
        };
        // WRITE: replace a DiagramEditor's .npd source (canvas re-renders).
        host.setDiagramSource = [this](int i, const QString &src) -> bool {
            auto *de = qobject_cast<DiagramEditor *>(m_tabs->widget(i));
            if (!de) return false;
            de->setNpdText(src);
            return true;
        };
        // ACT: reveal/focus the Noter tab — same path as the "+ Noter" UI.
        host.openNoter = [this]() -> bool {
            return ensureNoterTab() != nullptr;
        };
        // ── Phase 2: Data-analyst + Charts ──
        // READ: sanitized saved-connection list — name/driver/database only,
        // never credentials.
        host.listConnections = [] {
            QJsonArray out;
            const QVector<DbConnections::Record> recs = DbConnections::loadAll();
            for (const DbConnections::Record &r : recs) {
                QJsonObject o;
                o[QStringLiteral("name")] = r.name;
                o[QStringLiteral("driver")] = r.driver;
                o[QStringLiteral("database")] = r.database;
                // The MCP surface is read-only by construction (allowMutation
                // is false everywhere it can reach a named connection).
                o[QStringLiteral("read_only")] = true;
                out.append(o);
            }
            return out;
        };
        // Pure classification for the bridge's export fail-fast.
        host.classifySqlReadOnly = [](const QString &sql, QString *reason) {
            const DbConnections::SqlVerdict v =
                DbConnections::classifySql(sql, /*restrictFilesystem=*/true);
            if (v.singleStatement && v.readOnly) return true;
            if (reason)
                *reason = v.reason.isEmpty()
                              ? QStringLiteral("query is not read-only (SELECT only)")
                              : v.reason;
            return false;
        };
        // READ: SELECT-only query on a SAVED connection (what run_sql can't reach).
        host.runNamedQuery = [](const QString &name, const QString &sql,
                                int maxRows, QString *err) -> QJsonObject {
            const DbConnections::SqlVerdict v =
                DbConnections::classifySql(sql, /*restrictFilesystem=*/true);
            if (!(v.singleStatement && v.readOnly)) {
                if (err)
                    *err = v.reason.isEmpty()
                               ? QStringLiteral("query is not read-only (SELECT only)")
                               : QStringLiteral("query rejected: %1").arg(v.reason);
                return QJsonObject();
            }
            DbConnections::Record rec;
            if (!DbConnections::findByName(name, &rec)) {
                if (err) *err = QStringLiteral("no connection named: %1").arg(name);
                return QJsonObject();
            }
            if (rec.driver == QLatin1String("DUCKDB")) {
#ifdef NOTEPATRA_HAVE_DUCKDB
                rec.sandboxFilesystem = true; // engine-level file-read lockdown
#else
                if (err)
                    *err = QStringLiteral(
                        "this connection requires the Full edition (DuckDB)");
                return QJsonObject();
#endif
            }
            const DbConnections::QueryResult qr =
                DbConnections::runQuery(rec, sql, maxRows, /*allowMutation=*/false,
                                        nullptr);
            if (!qr.ok) {
                if (err)
                    *err = qr.error.isEmpty() ? QStringLiteral("query failed")
                                              : qr.error;
                return QJsonObject();
            }
            QJsonArray cols;
            for (const QString &c : qr.columns) cols.append(c);
            QJsonArray rows;
            for (const QVector<QString> &row : qr.rows) {
                QJsonArray jr;
                for (const QString &cell : row) jr.append(cell);
                rows.append(jr);
            }
            QString engine = QStringLiteral("odbc");
            if (rec.driver == QLatin1String("QSQLITE"))
                engine = QStringLiteral("sqlite");
            else if (rec.driver == QLatin1String("QPSQL"))
                engine = QStringLiteral("postgres");
            else if (rec.driver == QLatin1String("QMYSQL"))
                engine = QStringLiteral("mysql");
            else if (rec.driver == QLatin1String("DUCKDB"))
                engine = QStringLiteral("duckdb");
            QJsonObject out;
            out[QStringLiteral("columns")] = cols;
            out[QStringLiteral("rows")] = rows;
            out[QStringLiteral("truncated")] = qr.truncated;
            out[QStringLiteral("engine")] = engine;
            return out;
        };
        // READ: table list over a saved connection.
        host.listTables = [](const QString &name, QString *err) -> QJsonArray {
            DbConnections::Record rec;
            if (!DbConnections::findByName(name, &rec)) {
                if (err) *err = QStringLiteral("no connection named: %1").arg(name);
                return QJsonArray();
            }
            if (rec.driver == QLatin1String("DUCKDB")) {
#ifdef NOTEPATRA_HAVE_DUCKDB
                rec.sandboxFilesystem = true; // engine-level file-read lockdown
#else
                if (err)
                    *err = QStringLiteral(
                        "this connection requires the Full edition (DuckDB)");
                return QJsonArray();
#endif
            }
            bool ok = true;
            const QStringList tables = DbConnections::listTables(rec, &ok);
            if (!ok) {
                if (err) *err = QStringLiteral("could not connect to: %1").arg(name);
                return QJsonArray();
            }
            QJsonArray out;
            for (const QString &t : tables) out.append(t);
            return out;
        };
        // ACT: reveal the AI dock in Data Analyst mode.
        host.openDataAnalyst = [this]() -> bool {
            if (!m_aiDockPanel) return false;
            showAiDockForInvocation();
            m_aiDockPanel->showDataMode();
            return true;
        };
#ifdef NOTEPATRA_WITH_WEBENGINE
        // ACT: inline chart card in the Data transcript (Full/WebEngine only).
        host.renderChart = [this](const QJsonObject &spec, const QString &title,
                                  QString *err) -> QJsonObject {
            const QJsonObject vl = mcpChartToVegaLite(spec, err);
            if (vl.isEmpty()) return QJsonObject();
            if (!m_aiDockPanel) {
                if (err) *err = QStringLiteral("AI panel unavailable");
                return QJsonObject();
            }
            showAiDockForInvocation();
            m_aiDockPanel->showDataMode();
            VegaChartRenderer *r = m_aiDockPanel->addChartCard(vl, title);
            QJsonObject out;
            out[QStringLiteral("chart_id")] = r->chartId();
            out[QStringLiteral("rendered")] = !r->isLiteStub();
            return out;
        };
        // WRITE: off-screen render + async export → file (Full/WebEngine only).
        host.exportChart = [this](const QJsonObject &spec, const QString &path,
                                  const QString &format, int scale,
                                  QString *err) -> bool {
            const QJsonObject vl = mcpChartToVegaLite(spec, err);
            if (vl.isEmpty()) return false;
            VegaChartRenderer renderer;
            renderer.setAttribute(Qt::WA_DontShowOnScreen);
            renderer.resize(900, 600);
            renderer.show(); // realizes the WebEngine page off-screen
            // Bounded wait for the first render before exporting.
            {
                QEventLoop loop;
                bool done = false;
                QObject::connect(&renderer, &VegaChartRenderer::renderReady,
                                 &loop, [&] { done = true; loop.quit(); });
                QObject::connect(&renderer, &VegaChartRenderer::renderError,
                                 &loop, [&](const QString &m) {
                                     if (err) *err = m;
                                     loop.quit();
                                 });
                QTimer::singleShot(20000, &loop, [&] { loop.quit(); });
                renderer.setSpec(vl);
                if (!done) loop.exec();
                if (!done) {
                    if (err && err->isEmpty())
                        *err = QStringLiteral("chart render timed out");
                    return false;
                }
            }
            QByteArray bytes;
            {
                QEventLoop loop;
                bool got = false;
                auto cb = [&](const QByteArray &b) {
                    bytes = b;
                    got = true;
                    loop.quit();
                };
                if (format == QLatin1String("png"))
                    renderer.exportPngAsync(scale, cb);
                else if (format == QLatin1String("svg"))
                    renderer.exportSvgAsync(cb);
                else if (format == QLatin1String("html"))
                    renderer.exportHtmlAsync(cb);
                else
                    renderer.exportSpecAsync(cb);
                QTimer::singleShot(20000, &loop, [&] { loop.quit(); });
                if (!got) loop.exec();
            }
            if (bytes.isEmpty()) {
                if (err && err->isEmpty())
                    *err = QStringLiteral("chart export produced no data");
                return false;
            }
            QFile f(path);
            if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
                if (err) *err = QStringLiteral("could not write: %1")
                                    .arg(QDir::toNativeSeparators(path));
                return false;
            }
            f.write(bytes);
            f.close();
            return true;
        };
#endif
        new McpBridge(std::move(host), this);
    }
}

Editor *MainWindow::currentEditor() {
    return m_tabs->currentEditor();
}

// See the header for why these are four functions and not one.

QString MainWindow::workspaceFolder() const {
    return m_explorer ? m_explorer->workspaceRoot() : QString();
}

QString MainWindow::firstOpenFileDir() const {
    for (int i = 0; m_tabs && i < m_tabs->count(); ++i)
        if (auto *ed = m_tabs->editorAt(i))
            if (!ed->filePath().isEmpty())
                return QFileInfo(ed->filePath()).absolutePath();
    return QString();
}

QString MainWindow::suggestedDialogFolder() const {
    const QString folder = workspaceFolder();
    if (!folder.isEmpty()) return folder;
    if (auto *ed = m_tabs ? m_tabs->currentEditor() : nullptr)
        if (!ed->filePath().isEmpty())
            return QFileInfo(ed->filePath()).absolutePath();
    return firstOpenFileDir();
}

QString MainWindow::aiWorkspaceRoot() {
    // An explicit folder always wins and re-latches — that IS the user saying
    // "this is my project now", and re-keying the conversation is correct there.
    const QString folder = workspaceFolder();
    if (!folder.isEmpty()) {
        m_aiWorkspaceLatched = folder;
        return folder;
    }
    // Otherwise keep whatever we first settled on. Deriving this from the
    // CURRENT tab made the root flap on every Ctrl+Tab.
    if (m_aiWorkspaceLatched.isEmpty())
        m_aiWorkspaceLatched = firstOpenFileDir();
    return m_aiWorkspaceLatched;
}

// ── File operations ──

Editor *MainWindow::newFile() {
    // Dynamically pick the next "new N" by scanning visible untitled tabs.
    // Replaces a plain `m_newCount++` that had two failure modes:
    //   (a) After session restore brought back "new 5", the fresh counter
    //       started at 1, so the next new tab would be "new 2" — lower than
    //       the visible saved tab.
    //   (b) See updateTabTitle below — closing a lower-indexed tab used to
    //       renumber every higher untitled tab from its current index.
    // The visible-tab scan is monotonic over what's on screen and self-heals
    // after close / restore / rename, without needing the counter persisted.
    int nextN = 1;
    static const QRegularExpression kNewNameRe(QStringLiteral("^new (\\d+)$"));
    for (int i = 0; i < m_tabs->count(); i++) {
        QString existing = m_tabs->tabText(i);
        existing.remove(QStringLiteral(" *"));
        existing.remove(QStringLiteral(" [recovered]"));
        const auto m = kNewNameRe.match(existing);
        if (m.hasMatch()) {
            nextN = qMax(nextN, m.captured(1).toInt() + 1);
        }
    }
    m_newCount = nextN;
    auto *editor = new Editor(this);
    editor->applyTheme(Config::instance().theme);
    int idx = m_tabs->addTab(editor, QString("new %1").arg(nextN));
    // v0.1.68 — preserve v0.1.61 background-tab UX: Ctrl+N while the AI dock
    // is fullscreen creates the new tab in the background (focus shifts but
    // the dock stays fullscreen so the user can keep using AI). The flag is
    // consumed by the currentChanged slot installed in createCentralWidget.
    m_skipAiAutoExitOnNextTabChange = true;
    m_tabs->setCurrentIndex(idx);

    connect(editor, &QsciScintilla::modificationChanged, this, [this, editor](bool) {
        updateTabTitle(m_tabs->indexOf(editor));
    });
    connect(editor, &Editor::cursorPositionUpdated, this, [this](int line, int col, int pos) {
        m_statusBar->updatePosition(line, col, pos);
    });
    connect(editor, &QsciScintilla::textChanged, this, &MainWindow::updateDocStats);
    connect(editor, &Editor::changeHistoryUpdated, this, [this, editor]() {
        if (currentEditor() == editor)
            m_statusBar->updateChangeHistory(editor->modifiedLineCount(), editor->savedLineCount());
    });

    return editor;
}

void MainWindow::openFile(const QString &path) {
    if (path.isEmpty()) return;
    if (!QFileInfo(path).isFile()) {
        // Visible, non-modal. Silent return here was the last "open did
        // nothing" hole: forwarded not-found args, stale recent-files
        // entries and restored tabs whose file vanished all ended here.
        queueStartupNotice(tr("Could not open: %1 (not found or not a file)")
                               .arg(QDir::toNativeSeparators(path)));
        return;
    }

    // Check if already open
    for (int i = 0; i < m_tabs->count(); i++) {
        auto *ed = m_tabs->editorAt(i);
        if (ed && ed->filePath() == QFileInfo(path).absoluteFilePath()) {
            m_tabs->setCurrentIndex(i);
            return;
        }
    }

    auto *editor = new Editor(this);
    QString loadErr;
    if (!editor->loadFile(path, &loadErr)) {
        delete editor;
        if (!loadErr.isEmpty()) queueStartupNotice(loadErr);
        return;
    }
    editor->applyTheme(Config::instance().theme);

    int idx = m_tabs->addTab(editor, QFileInfo(path).fileName());
    m_tabs->setTabToolTip(idx, QDir::toNativeSeparators(path));
    m_tabs->setCurrentIndex(idx);

    connect(editor, &QsciScintilla::modificationChanged, this, [this, editor](bool) {
        updateTabTitle(m_tabs->indexOf(editor));
    });
    connect(editor, &Editor::cursorPositionUpdated, this, [this](int line, int col, int pos) {
        m_statusBar->updatePosition(line, col, pos);
    });
    connect(editor, &QsciScintilla::textChanged, this, &MainWindow::updateDocStats);
    connect(editor, &Editor::changeHistoryUpdated, this, [this, editor]() {
        if (currentEditor() == editor)
            m_statusBar->updateChangeHistory(editor->modifiedLineCount(), editor->savedLineCount());
    });

    updateTitle();
    updateStatusBar();

    // Watch this file for external changes
    QString absPath = QFileInfo(path).absoluteFilePath();
    if (m_fileWatcher) {
        m_fileWatcher->addPath(absPath);
        m_fileTimestamps[absPath] = QFileInfo(absPath).lastModified();
    }

    // Add to recent files + remember this folder for the next dialog.
    // setLastDir() only mutates the member; the save() below persists both.
    Config::instance().addRecent(absPath);
    Config::instance().setLastDir(absPath);
    Config::instance().save();
    updateRecentMenu();
}

void MainWindow::handleRemoteOpen(const QStringList &paths, int gotoLine,
                                  const QByteArray &startupId) {
    // Un-minimize if needed, then bring to front. The second-instance
    // process called AllowSetForegroundWindow(ASFW_ANY) before exiting,
    // so SetForegroundWindow now succeeds instead of flashing the taskbar.
    if (isMinimized()) showNormal();
    else show();
    raise();
    activateWindow();
#ifdef Q_OS_WIN
    // Belt-and-braces: Qt's activateWindow() maps to SetForegroundWindow,
    // but a brief TOPMOST flip is the documented workaround when an app
    // wants to guarantee z-order on Windows even after focus succeeds.
    const HWND hwnd = reinterpret_cast<HWND>(winId());
    if (hwnd) {
        ::SetWindowPos(hwnd, HWND_TOPMOST,   0, 0, 0, 0,
                       SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
        ::SetWindowPos(hwnd, HWND_NOTOPMOST, 0, 0, 0, 0,
                       SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
        ::SetForegroundWindow(hwnd);
    }
#endif
#ifdef Q_OS_LINUX
    // X11 focus handoff. Faithful port of wmctrl's `activate_window`
    // sequence (verified by reading wmctrl source + ltrace), which is
    // the only pattern that consistently wins against Cinnamon/Muffin's
    // focus-stealing-prevention when the requesting client is not the
    // currently focused window — i.e. exactly our case (Nemo dispatches
    // the open, Notepatra is background, we want to come forward).
    //
    // The sequence has THREE parts and ALL THREE are required:
    //   (a) _NET_ACTIVE_WINDOW ClientMessage, source=2 (pager/user) +
    //       timestamp from the DESKTOP_STARTUP_ID's "_TIME<N>" suffix
    //       (or 0 when absent, which Muffin reads as CurrentTime).
    //   (b) xcb_map_window + xcb_configure_window STACK_MODE_ABOVE.
    //       This is wmctrl's XMapRaised. It goes to the WM as a
    //       ConfigureRequest, which is a DIFFERENT code path from
    //       activation ClientMessages and is NOT gated by FSP. Without
    //       (b), Muffin demotes (a) to _NET_WM_STATE_DEMANDS_ATTENTION
    //       and the window stays in the background. Adding (b) alone
    //       (no ClientMessage) also doesn't work because the WM won't
    //       give input focus without a corresponding activation hint.
    //   (c) Round-trip fence (xcb_get_input_focus + reply) before
    //       xcb_disconnect, otherwise queued async requests can be
    //       dropped on connection close.
    //
    // Earlier attempts also tried xcb_set_input_focus + _NET_WM_USER_TIME
    // updates; both are harmful: set_input_focus bypasses the WM which
    // then "corrects" by reverting focus, and aggressively bumping
    // user_time triggered FSP comparison failures.
    const xcb_window_t xwin = static_cast<xcb_window_t>(winId());
    if (xwin) {
        xcb_connection_t *conn = xcb_connect(nullptr, nullptr);
        if (conn && !xcb_connection_has_error(conn)) {
            auto intern = [&](const char *name) -> xcb_atom_t {
                xcb_intern_atom_cookie_t c =
                    xcb_intern_atom(conn, 0, std::strlen(name), name);
                xcb_intern_atom_reply_t *r =
                    xcb_intern_atom_reply(conn, c, nullptr);
                const xcb_atom_t a = r ? r->atom : XCB_ATOM_NONE;
                free(r);
                return a;
            };
            const xcb_atom_t aActiveWin = intern("_NET_ACTIVE_WINDOW");
            const xcb_atom_t aStartupId = intern("_NET_STARTUP_ID");
            const xcb_atom_t aUtf8      = intern("UTF8_STRING");
            const xcb_screen_t *screen =
                xcb_setup_roots_iterator(xcb_get_setup(conn)).data;

            uint32_t reqTimestamp = 0;
            if (!startupId.isEmpty()) {
                const int idx = startupId.lastIndexOf("_TIME");
                if (idx >= 0) {
                    bool ok = false;
                    const uint v =
                        startupId.mid(idx + 5).toUInt(&ok);
                    if (ok) reqTimestamp = static_cast<uint32_t>(v);
                }
                // Set _NET_STARTUP_ID on our window. Cinnamon's launch-
                // feedback tracker (and other compliant WMs) stops the
                // spinner when a window mapped with the matching ID
                // appears — REMOVE messages alone are not enough for
                // Cinnamon in practice. Setting the property after the
                // window is already mapped still works: the WM watches
                // PropertyNotify on managed windows and updates its
                // internal startup-id → window mapping.
                if (aStartupId != XCB_ATOM_NONE) {
                    xcb_change_property(
                        conn, XCB_PROP_MODE_REPLACE, xwin,
                        aStartupId,
                        aUtf8 != XCB_ATOM_NONE ? aUtf8 : XCB_ATOM_STRING,
                        8, startupId.size(), startupId.constData());
                }
            }

            if (aActiveWin != XCB_ATOM_NONE && screen) {
                // (a) _NET_ACTIVE_WINDOW ClientMessage
                xcb_client_message_event_t ev{};
                ev.response_type = XCB_CLIENT_MESSAGE;
                ev.format = 32;
                ev.window = xwin;
                ev.type = aActiveWin;
                ev.data.data32[0] = 2;  // source = pager/user
                ev.data.data32[1] = reqTimestamp;
                ev.data.data32[2] = 0;
                ev.data.data32[3] = 0;
                ev.data.data32[4] = 0;
                xcb_send_event(conn, false, screen->root,
                               XCB_EVENT_MASK_SUBSTRUCTURE_REDIRECT |
                               XCB_EVENT_MASK_SUBSTRUCTURE_NOTIFY,
                               reinterpret_cast<const char *>(&ev));

                // (b) Map + raise. This is the part that bypasses FSP.
                xcb_map_window(conn, xwin);
                const uint32_t stackVals[] = { XCB_STACK_MODE_ABOVE };
                xcb_configure_window(conn, xwin,
                                     XCB_CONFIG_WINDOW_STACK_MODE,
                                     stackVals);

                xcb_flush(conn);

                // (c) Round-trip fence before disconnect so the X
                // server processes everything before we close.
                xcb_get_input_focus_reply_t *fr =
                    xcb_get_input_focus_reply(
                        conn, xcb_get_input_focus(conn), nullptr);
                free(fr);
            }
        }
        if (conn) xcb_disconnect(conn);
    }
#endif
    // Loads are deferred one event-loop turn so the raise above paints first.
    m_pendingRemoteOpens.append({paths, gotoLine});
    scheduleRemoteOpenFlush();
}

void MainWindow::scheduleRemoteOpenFlush() {
    if (m_pendingRemoteOpens.isEmpty() || m_remoteFlushQueued || !m_startupDone)
        return;
    m_remoteFlushQueued = true;
    QTimer::singleShot(0, this, [this]() { flushPendingRemoteOpens(); });
}

void MainWindow::flushPendingRemoteOpens() {
    m_remoteFlushQueued = false;
    if (!m_startupDone) return;  // runStartupNow() re-schedules after restore
    // Re-entered through a load modal's nested event loop — the outer drain
    // below picks the new request up once the modal closes (see header).
    if (m_remoteFlushActive) return;
    m_remoteFlushActive = true;
    while (!m_pendingRemoteOpens.isEmpty()) {
        const RemoteOpenRequest req = m_pendingRemoteOpens.takeFirst();
        for (const QString &p : req.paths) openFile(p);
        if (req.gotoLine > 0) {
            // Same "first file" promise as runStartupNow(). If the first
            // file failed to open (notice already queued by openFile),
            // don't jump an unrelated surviving tab to that line.
            if (!req.paths.isEmpty()) {
                const int idx = tabIndexForPath(
                    QFileInfo(req.paths.first()).absoluteFilePath());
                if (idx >= 0) {
                    m_tabs->setCurrentIndex(idx);
                    if (auto *e = m_tabs->editorAt(idx))
                        e->gotoLine(req.gotoLine);
                }
            } else if (auto *e = currentEditor()) {
                e->gotoLine(req.gotoLine);  // bare `--line N` forward
            }
        }
    }
    m_remoteFlushActive = false;
}

// Save As dialog UX baseline:
//   * bigger geometry (960×640 vs Qt default 640×480)
//   * Detail view default (Name / Size / Type / Date Modified)
//   * sort by Date Modified descending (newest files first)
//
// v0.1.94 — Date Created column DROPPED. The v0.1.89 attempt shipped a
// QIdentityProxyModel that fabricated a synthetic column via direct
// createIndex(), which on Windows crashed File→Save / Save As / right-
// click Save reliably. The QFileSystemModel's async QFileInfoGatherer
// races the proxy's index/parent traversal, and setProxyModel called
// after setNameFilter leaks dangling QPersistentModelIndex into the
// completer. Saving the user's file is more important than a Date
// Created column — column gone, dialog is stable again.
namespace {
void configureSaveDialogUx(QFileDialog &dialog) {
    dialog.resize(QSize(960, 640));
    dialog.setViewMode(QFileDialog::Detail);

    auto applyView = [&dialog]() {
        if (auto *tv = dialog.findChild<QTreeView *>()) {
            tv->setSortingEnabled(true);
            tv->sortByColumn(3, Qt::DescendingOrder);  // 3 = Date Modified
            tv->header()->setSectionResizeMode(QHeaderView::Interactive);
            tv->header()->setStretchLastSection(false);
        }
    };
    QObject::connect(&dialog, &QFileDialog::directoryEntered,
                     [applyView](const QString &) { applyView(); });
    applyView();
}
}  // namespace

void MainWindow::saveFile() {
    auto *e = currentEditor();
    if (!e) return;
    if (!e->filePath().isEmpty()) {
        // v0.1.94 — surface failures. Pre-fix the bool return was ignored,
        // so a read-only / AV-locked / perms-denied file silently dropped
        // the user's edits while updateGitGutter + watcher timestamps ran
        // on stale data ("looked saved, wasn't").
        if (!e->saveFile()) {
            QMessageBox::warning(this, QStringLiteral("Save failed"),
                QStringLiteral("Could not write to:\n\n%1\n\n"
                               "The file may be read-only, locked by another "
                               "application, or you may lack write permission.")
                    .arg(QDir::toNativeSeparators(e->filePath())));
            return;
        }
        updateTabTitle(m_tabs->currentIndex());
        e->updateGitGutter();
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

    // v0.1.87 — populate Save As file-type dropdown with ~72 language entries
    // (was a single "All Files (*)" pre-fix, which made the dropdown dead).
    // Pre-select the filter matching the current tab's language so the user's
    // intent ("save this Python file") is the default.
    QString preselected;
    const QString filters = buildSaveAsFilters(e->language(), &preselected);
    // The current tab's language extension ("py", may be "") — the reliable
    // default suffix, derived from what we KNOW the tab is, not from any
    // dialog filter selection.
    const QString defExt = firstExtensionFromFilter(preselected);

    // Start in the directory of the current file if there is one, else the
    // last directory a dialog used (falls back to home on a fresh install).
    const QString startDir = e->filePath().isEmpty()
        ? Config::instance().lastDirOrHome()
        : QFileInfo(e->filePath()).absolutePath();
    const QString startName = QFileInfo(e->filePath().isEmpty()
        ? m_tabs->tabText(m_tabs->currentIndex()).remove(" *")
        : e->filePath()).fileName();
    const QString startPath = startDir + QLatin1Char('/') + startName;

    QFileDialog dialog(this, QStringLiteral("Save As"), startPath);
    dialog.setAcceptMode(QFileDialog::AcceptSave);
    // v0.1.123 — use the NATIVE OS Save dialog (Windows Explorer tree, native
    // GTK / macOS panels), matching Open and every other Notepatra dialog.
    // This replaces the Qt-own dialog forced since v0.1.88.1.
    //
    // v0.1.88.1's reason for going non-native was that native dialogs drop the
    // `filterSelected` signal and return a stale `selectedNameFilter()` after
    // Accept, so a mid-session filter change could save the wrong extension.
    // We now sidestep that WITHOUT ever reading the selected filter:
    //   * setDefaultSuffix(defExt) — native dialogs DO honour this; it supplies
    //     the current tab's language extension when the user types none. On
    //     Windows the native dialog also appends the picked filter's extension
    //     itself, so changing the type in the dropdown still works there.
    //   * a post-Accept net (below) appends defExt only if the path came back
    //     with NO extension — it never overrides an extension the user typed.
    dialog.setNameFilter(filters);
    if (!preselected.isEmpty()) dialog.selectNameFilter(preselected);
    if (!defExt.isEmpty()) dialog.setDefaultSuffix(defExt);

    // v0.1.88 UX — bigger geometry, detail view (no-ops on the native dialog,
    // which manages its own sizing; still applies if Qt falls back to non-native).
    configureSaveDialogUx(dialog);

    if (dialog.exec() != QDialog::Accepted) return;
    const QStringList chosen = dialog.selectedFiles();
    if (chosen.isEmpty()) return;
    QString path = chosen.first();
    if (path.isEmpty()) return;

    // Post-Accept safety net: some Linux GTK builds drop setDefaultSuffix and
    // return a bare path. Append the current tab's language extension ONLY when
    // the path has no extension at all — this can never turn "foo.js" into
    // "foo.js.py", and it does NOT consult the (unreliable-on-native)
    // selectedNameFilter().
    if (!defExt.isEmpty() && QFileInfo(path).suffix().isEmpty())
        path += QLatin1Char('.') + defExt;

    // v0.1.94 — surface failures. saveFile returns false on write/permission
    // errors and the user was previously left believing the file had saved.
    if (!e->saveFile(path)) {
        QMessageBox::warning(this, QStringLiteral("Save failed"),
            QStringLiteral("Could not write to:\n\n%1\n\n"
                           "The path may be invalid, the target drive read-only, "
                           "or you may lack write permission.")
                .arg(QDir::toNativeSeparators(path)));
        return;
    }
    Config::instance().noteLastDir(path);
    m_tabs->setTabText(m_tabs->currentIndex(), QFileInfo(path).fileName());
    m_tabs->setTabToolTip(m_tabs->currentIndex(), QDir::toNativeSeparators(path));
    updateTitle();
    updateStatusBar();
}

void MainWindow::closeTab(int index) {
    QWidget *widget = m_tabs->widget(index);
    if (!widget) return;

    // If it's an editor, check for unsaved changes
    auto *editor = qobject_cast<Editor *>(widget);
    if (editor && editor->isModified()) {
        // Defer watcher prompts while OUR prompt/dialog is up: a fileChanged
        // delivered into the nested event loop would stack its own modal,
        // and the File-Deleted "No" branch deletes the very editor this
        // frame holds across the prompt. Drained on scope exit.
        const bool prevGate = m_anyFileChangePromptOpen;
        m_anyFileChangePromptOpen = true;
        const auto watcherGate = qScopeGuard([this, prevGate]() {
            m_anyFileChangePromptOpen = prevGate;
            if (!prevGate) drainDeferredFileChanges();
        });
        QString name = m_tabs->tabText(index).remove(" *");
        auto result = QMessageBox::question(this, "Save",
            QString("Save changes to %1?").arg(name),
            QMessageBox::Save | QMessageBox::Discard | QMessageBox::Cancel);

        if (result == QMessageBox::Save) {
            if (!editor->filePath().isEmpty()) {
                if (!editor->saveFile()) {
                    QMessageBox::warning(this, QStringLiteral("Save failed"),
                        QStringLiteral("Could not write to:\n\n%1\n\n"
                                       "Close cancelled — your edits are still in the tab.")
                            .arg(QDir::toNativeSeparators(editor->filePath())));
                    return;
                }
            }
            else {
                // v0.1.87 — same filter-list treatment as saveFileAs().
                // v0.1.123 — native dialog (Explorer tree), same suffix strategy
                // as saveFileAs(): setDefaultSuffix + a bare-path-only net.
                QString preselected;
                const QString filters = buildSaveAsFilters(editor->language(), &preselected);
                const QString defExt = firstExtensionFromFilter(preselected);
                QFileDialog d(this, QStringLiteral("Save File"), Config::instance().lastDirOrHome());
                d.setAcceptMode(QFileDialog::AcceptSave);
                d.setNameFilter(filters);
                if (!preselected.isEmpty()) d.selectNameFilter(preselected);
                if (!defExt.isEmpty()) d.setDefaultSuffix(defExt);
                configureSaveDialogUx(d);  // v0.1.88 UX (no-op on native)
                if (d.exec() != QDialog::Accepted) return;
                const QStringList chosen = d.selectedFiles();
                if (chosen.isEmpty() || chosen.first().isEmpty()) return;
                QString finalPath = chosen.first();
                if (!defExt.isEmpty() && QFileInfo(finalPath).suffix().isEmpty())
                    finalPath += QLatin1Char('.') + defExt;
                if (!editor->saveFile(finalPath)) {
                    QMessageBox::warning(this, QStringLiteral("Save failed"),
                        QStringLiteral("Could not write to:\n\n%1\n\n"
                                       "Close cancelled.")
                            .arg(QDir::toNativeSeparators(finalPath)));
                    return;
                }
                Config::instance().noteLastDir(finalPath);
            }
        } else if (result == QMessageBox::Cancel) {
            return;
        }
    }

    // The prompt's nested event loop can add or reorder tabs (deferred
    // remote opens) — re-resolve before removing by index.
    index = m_tabs->indexOf(widget);
    if (index < 0) return;   // tab vanished while we prompted

    // Remove file from watcher if it's an editor
    if (editor && !editor->filePath().isEmpty() && m_fileWatcher) {
        m_fileWatcher->removePath(editor->filePath());
        m_fileTimestamps.remove(editor->filePath());
    }

    m_tabs->removeTab(index);
    delete widget;
    if (m_tabs->count() == 0) newFile();
}

void MainWindow::closeAllTabs() {
    // Descending bounded sweep — `while (count() > 0) closeTab(0)` never
    // terminated: closing the last tab backfills a fresh untitled
    // (newFile-on-zero), and Cancel on a modified tab re-prompted forever.
    for (int i = m_tabs->count() - 1; i >= 0; i--) {
        QPointer<QWidget> alive(m_tabs->widget(i));
        closeTab(i);
        if (alive) return;   // Cancel / failed save — abort the sweep
    }
}

// ── UI updates ──

void MainWindow::updateTitle() {
    auto *e = currentEditor();
    if (e && !e->filePath().isEmpty())
        setWindowTitle(QDir::toNativeSeparators(e->filePath()) + " - " NOTEPATRA_FLAVOR_NAME);
    else if (e)
        setWindowTitle(m_tabs->tabText(m_tabs->currentIndex()) + " - " NOTEPATRA_FLAVOR_NAME);
    else
        setWindowTitle(NOTEPATRA_FLAVOR_NAME);
}

// D3 — O(1) doc stats: cached word count shows instantly, the debounce
// timer refreshes it async. No text() materialization, no regex.
void MainWindow::updateDocStats() {
    auto *e = currentEditor();
    if (!e) return;
    m_statusBar->updateLines(e->lines());
    m_statusBar->updateLength(e->length());
    m_statusBar->updateWords(e->lastWordCount());
    if (e->wordCountDirty()) m_wordCountTimer->start();
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
    updateDocStats();
    m_statusBar->updateChangeHistory(e->modifiedLineCount(), e->savedLineCount());
}

void MainWindow::updateTabTitle(int index) {
    if (index < 0) return;
    auto *e = m_tabs->editorAt(index);
    if (!e) return;
    QString name;
    if (!e->filePath().isEmpty()) {
        name = QFileInfo(e->filePath()).fileName();
    } else {
        // Untitled tab — preserve the assigned name across tab close/reorder.
        // Previously this re-derived from `index + 1`, which made tabs appear
        // to "rename themselves" lower when a lower-indexed tab was closed
        // (e.g. close "new 1" → existing "new 2" became "new 1").
        name = m_tabs->tabText(index);
        name.remove(QStringLiteral(" *"));
    }
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
        // openFile() records lastDir for each file opened, seeding the next dialog.
        for (const auto &p : QFileDialog::getOpenFileNames(this, "Open", Config::instance().lastDirOrHome(), "All Files (*)"))
            openFile(p);
    }, QKeySequence("Ctrl+O"));
    file->addAction("Open Folder as Workspace...", this, [this]() {
        QString p = QFileDialog::getExistingDirectory(this, "Open Folder", Config::instance().lastDirOrHome());
        if (p.isEmpty()) return;
        Config::instance().noteLastDir(p);
        m_explorer->setRoot(p);
        // v0.1.70 — explorer does NOT auto-show even in Coding mode. Setting
        // the workspace folder here primes the root only; user manually
        // shows the sidebar via View > Files Explorer if they want it.
    });
    file->addAction("Reload from Disk", this, [E]() {
        if (auto *e = E(); e && !e->filePath().isEmpty()) e->loadFile(e->filePath());
    });
    file->addSeparator();
    file->addAction("&Save", this, [this]() { saveFile(); }, QKeySequence("Ctrl+S"));
    file->addAction("Save &As...", this, [this]() { saveFileAs(); }, QKeySequence("Ctrl+Shift+S"));
    file->addAction("Save a Copy As...", this, [E]() {
        if (auto *e = E()) {
            QString p = QFileDialog::getSaveFileName(nullptr, "Save a Copy As", Config::instance().lastDirOrHome());
            if (!p.isEmpty()) { QFile f(p); if (f.open(QIODevice::WriteOnly)) { f.write(e->text().toUtf8()); Config::instance().noteLastDir(p); } }
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
    file->addAction("Close All", this, [this]() { closeAllTabs(); });
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
        if (auto *e = E(); e && !e->filePath().isEmpty()) QApplication::clipboard()->setText(QDir::toNativeSeparators(e->filePath()));
    });
    copyClip->addAction("Copy Filename", this, [E]() {
        if (auto *e = E(); e && !e->filePath().isEmpty()) QApplication::clipboard()->setText(QFileInfo(e->filePath()).fileName());
    });
    copyClip->addAction("Copy Directory", this, [E]() {
        if (auto *e = E(); e && !e->filePath().isEmpty()) QApplication::clipboard()->setText(QDir::toNativeSeparators(QFileInfo(e->filePath()).path()));
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
    // v0.1.70 — Ctrl+Q reassigned to AI dock toggle (user preference,
    // closer to left hand than Ctrl+Shift+A). Line Comment keeps Ctrl+/
    // only.
    lineCommentAct->setShortcuts({QKeySequence("Ctrl+/")});
    auto *blockCommentAct = commentMenu->addAction("Toggle &Block Comment", this,
        [E]() { if (auto *e = E()) e->toggleBlockComment(); });
    blockCommentAct->setShortcut(QKeySequence("Ctrl+Shift+Q"));
    // v0.1.45 — explicit Comment / Uncomment per kind, NPP-style.
    // Comment Line = Ctrl+K. Uncomment Line = Ctrl+Alt+U — Ctrl+Shift+K is
    // reserved for Delete Current Line (Line Operations above); binding both
    // to Ctrl+Shift+K shadowed Delete Line (fixed v0.1.107).
    commentMenu->addSeparator();
    auto *commentLineAct = commentMenu->addAction("&Comment Line", this,
        [E]() { if (auto *e = E()) e->commentLine(); });
    commentLineAct->setShortcut(QKeySequence("Ctrl+K"));
    auto *uncommentLineAct = commentMenu->addAction("&Uncomment Line", this,
        [E]() { if (auto *e = E()) e->uncommentLine(); });
    uncommentLineAct->setShortcut(QKeySequence("Ctrl+Alt+U"));
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
    search->addAction("&Mark...", this, [FD]() { FD()->showMark(); });
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
    // v0.1.124 — Show Symbol, rebuilt to Notepad++'s structure and semantics.
    //
    // The old menu had three items that poked the editor directly, persisted
    // nothing, and could not show a single invisible Unicode character. A file
    // containing U+200B looked identical to one without it. The two new items
    // are the ones that close that gap; the rest is the same set Notepad++
    // ships, in the same order, so a user moving between the two editors finds
    // what they expect where they expect it.
    //
    // All state now lives in Config and is applied through
    // Editor::applySymbolSettings(), so it propagates to every tab and
    // survives a restart.
    auto *symMenu = view->addMenu("Show Symbol");
    // QMenu swallows QAction tooltips unless this is set, and the two new
    // entries are the ones a user is least likely to guess from the label.
    symMenu->setToolTipsVisible(true);

    // Push the current Config to every open tab and refresh the checkmarks.
    auto applySymbolsEverywhere = [this]() {
        Config::instance().save();
        for (int i = 0; i < m_tabs->count(); ++i)
            if (auto *e = m_tabs->editorAt(i)) e->applySymbolSettings();

        // Secondary views are plain QsciScintilla, not Editor, so they never
        // see applySymbolSettings(). Sweeping every live widget catches the
        // Compare panes and the formatter output without this menu needing to
        // know they exist — and Compare is the one that matters, since two
        // files differing only by an invisible character would otherwise show
        // as two identical lines flagged as different.
        for (QWidget *w : QApplication::allWidgets())
            if (auto *sci = qobject_cast<QsciScintilla *>(w))
                if (!qobject_cast<Editor *>(w)) Editor::applySymbolsTo(sci);

        syncViewMenuToActiveEditor();
    };

    auto addSymbolToggle = [&](const char *label, const char *objName,
                               bool Config::*field) {
        auto *act = symMenu->addAction(QString::fromLatin1(label));
        act->setObjectName(QString::fromLatin1(objName));
        act->setCheckable(true);
        act->setChecked(Config::instance().*field);
        QObject::connect(act, &QAction::toggled, this,
                         [applySymbolsEverywhere, field](bool on) {
            Config::instance().*field = on;
            applySymbolsEverywhere();
        });
        return act;
    };

    addSymbolToggle("Show Space and Tab", "viewShowWhitespace",
                    &Config::showWhitespace);
    addSymbolToggle("Show End of Line", "viewShowEol", &Config::showEol);
    addSymbolToggle("Show Non-Printing Characters", "viewShowNonPrinting",
                    &Config::showNonPrintingChars)
        ->setToolTip("Reveal invisible Unicode characters such as ZWSP, NBSP "
                     "and bidirectional controls");
    addSymbolToggle("Show Control Characters && Unicode EOL", "viewShowControlChars",
                    &Config::showControlChars)
        ->setToolTip("Reveal C0/C1 control characters and NEL, LS and PS");

    // Past Notepad++. Its tables are a fixed 113 codepoints, so anything it
    // does not list stays invisible there too — variation selectors, the
    // Hangul fillers, and the TAG block used to hide unreadable text in a file.
    addSymbolToggle("Show Every Other Invisible Character", "viewShowOtherInvisible",
                    &Config::showOtherInvisible)
        ->setToolTip("Everything Notepad++'s tables miss: variation selectors, "
                     "tag characters, Hangul fillers and the rest of Unicode's "
                     "format category");

    // "Show All Characters" is a fan-out, not another independent setting —
    // it drives the five above, so it means what it says. Its own checkmark is
    // derived in syncViewMenuToActiveEditor() and reads as checked only when
    // all five really are on.
    auto *actShowAll = symMenu->addAction("Show All Characters");
    actShowAll->setObjectName("viewShowAllCharacters");
    actShowAll->setCheckable(true);
    QObject::connect(actShowAll, &QAction::toggled, this,
                     [applySymbolsEverywhere](bool on) {
        auto &cfg = Config::instance();
        cfg.showWhitespace = cfg.showEol = on;
        cfg.showNonPrintingChars = cfg.showControlChars = on;
        cfg.showOtherInvisible = on;
        applySymbolsEverywhere();
    });

    // What the blobs above SAY. Notepad++ calls this the NPC display mode; its
    // third option, "identity", is omitted because drawing an invisible
    // character as itself is the same as turning the category off.
    auto *npcModeMenu = symMenu->addMenu("Non-Printing Character Display");
    auto *npcModeGroup = new QActionGroup(this);
    npcModeGroup->setExclusive(true);

    auto addNpcModeAction = [&](const char *label, const char *objName, int mode) {
        auto *act = npcModeMenu->addAction(QString::fromLatin1(label));
        act->setObjectName(QString::fromLatin1(objName));
        act->setCheckable(true);
        act->setActionGroup(npcModeGroup);
        // Checked BEFORE connecting: doing it after fires toggled() during
        // construction, which would write and save Config before the user has
        // touched anything.
        act->setChecked(Config::instance().npcDisplayMode == mode);
        QObject::connect(act, &QAction::toggled, this,
                         [applySymbolsEverywhere, mode](bool on) {
            if (!on) return;   // the group also un-checks the previous action
            Config::instance().npcDisplayMode = mode;
            applySymbolsEverywhere();
        });
        return act;
    };

    addNpcModeAction("Abbreviation", "viewNpcModeAbbreviation", 0)
        ->setToolTip("Draw U+200B as \"ZWSP\"");
    addNpcModeAction("Codepoint", "viewNpcModeCodepoint", 1)
        ->setToolTip("Draw U+200B as \"U+200B\"");

    symMenu->addSeparator();

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

    addSymbolToggle("Show Wrap Symbol", "viewShowWrapSymbol",
                    &Config::showWrapSymbol);

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
    // v0.1.61 — explorer is gated to Coding mode. The View toggle only
    // works while Coding mode is active in the AI dock; otherwise it's
    // a no-op so we preserve the "explorer == coding workspace" invariant.
    connect(explorerAct, &QAction::triggered, this, [this, explorerAct]() {
        const bool codingOn = m_aiDockPanel && m_aiDockPanel->isCodingMode()
                              && m_aiDockHost && m_aiDockHost->isVisible();
        if (!codingOn) {
            explorerAct->setChecked(false);
            statusBar()->showMessage(
                tr("Folder as Workspace requires Coding mode in the AI dock."), 4000);
            return;
        }
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
    // v0.1.70 — shortcut moved from Ctrl+Shift+A → Ctrl+Q (user preference;
    // closer to left hand). Both still register the same toggle.
    auto *aiAct = feat->addAction("AI Assistant      Ctrl+Q");
    aiAct->setCheckable(true);
    aiAct->setShortcuts({QKeySequence("Ctrl+Q"), QKeySequence("Ctrl+Shift+A")});
    aiAct->setStatusTip("Toggle the AI Assistant dock (right side). Persistent chat that sees all your open files + workspace. Configure backends in Settings → Preferences → AI.");
    connect(aiAct, &QAction::triggered, this, [this, aiAct]() {
        toggleAiDock();
        aiAct->setChecked(isAiDockVisible());
    });

    // v0.1.71 — AI interaction log viewer. Opens a read-only dialog with
    // every request/response that has hit any cloud or local LLM over
    // the last 7 days. Filters by backend / model / mode and exports
    // JSON. Includes a toggle to opt out of logging entirely.
    auto *aiLogAct = feat->addAction("AI Interaction Log…");
    aiLogAct->setStatusTip("Audit every request/response sent to cloud or local LLMs in the last 7 days. SQLite-backed, credential-scrubbed, opt-out-able.");
    connect(aiLogAct, &QAction::triggered, this, [this]() {
        AiLogDialog dlg(this);
        dlg.exec();
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
        else if (m_explorer && !m_explorer->workspaceRoot().isEmpty())
            defaultFolder = m_explorer->workspaceRoot();
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
        exitAiFullscreenIfActive();
        int idx = m_tabs->addTab(ps, "Project Search");
        m_tabs->setCurrentIndex(idx);
        ps->focusQuery();
    });

    feat->addSeparator();
    sectionHeader("Workflow");

    // --- Terminal ---
    // Not checkable — every trigger opens a NEW terminal tab, so a
    // checked state would lie about visibility.
    auto *termAct = feat->addAction("Terminal                  Ctrl+`");
    termAct->setShortcut(QKeySequence("Ctrl+`"));
    termAct->setStatusTip("Opens a terminal in a new tab.");
    connect(termAct, &QAction::triggered, this, [this, E]() {
        auto *term = new TerminalWidget;
        // Theme propagation — re-colour chrome when user flips themes.
        connect(this, &MainWindow::themeChanged, term, &TerminalWidget::onThemeChanged);
        if (auto *e = E(); e && !e->filePath().isEmpty())
            term->setWorkingDirectory(QFileInfo(e->filePath()).path());
        exitAiFullscreenIfActive();
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
    auto *restAct = feat->addAction("REST Client (.http)");
    restAct->setShortcut(QKeySequence("Ctrl+Shift+R"));
    restAct->setStatusTip("Opens REST client in a new tab. Select HTTP request first.");
    connect(restAct, &QAction::triggered, this, [this, E]() {
        auto *rest = new RestClient;
        // Theme propagation — palette-driven stylesheets re-render.
        connect(this, &MainWindow::themeChanged, rest, &RestClient::onThemeChanged);
        if (E() && E()->hasSelectedText()) rest->executeRequest(E()->selectedText());
        exitAiFullscreenIfActive();
        int idx = m_tabs->addTab(rest, "REST Client");
        m_tabs->setCurrentIndex(idx);
    });

    // --- Noter (meeting thinkpad) ---
    // v0.1.112 — "(toggle)" in the label because triggering it CLOSES the
    // tab when Noter is already focused (see the connect below). The text
    // keeps the "Noter — Meeting Thinkpad" prefix so findActionByPrefix
    // lookups (feature toolbar + Welcome cards) stay stable.
    m_noterAct = feat->addAction("Noter — Meeting Thinkpad (toggle)        Ctrl+Alt+N");
    m_noterAct->setCheckable(true);
    m_noterAct->setShortcut(QKeySequence("Ctrl+Alt+N"));
    m_noterAct->setStatusTip("Toggle Noter — meeting notes with AI Extract (summary + action items) and reminders that fire while Notepatra is running. New note: Ctrl+Alt+M.");
    connect(m_noterAct, &QAction::triggered, this, [this]() {
        // v0.1.97 — toggle behavior, matches the AI dock pattern (Ctrl+Q).
        // Three states:
        //   - Tab doesn't exist  → create + focus
        //   - Tab exists, NOT current → focus
        //   - Tab exists, IS current → close
        // This makes the toolbar/keybind a true on/off switch.
        int existingIdx = -1;
        NotesPanel *noter = findNoterPanel(&existingIdx);
        if (noter && existingIdx == m_tabs->currentIndex()) {
            // Currently focused → close. (The destroyed-connection wired in
            // ensureNoterTab also clears the checkmark — redundant but
            // harmless here; it exists for the generic closeTab path.)
            m_tabs->removeTab(existingIdx);
            delete noter;
            m_noterAct->setChecked(false);
            return;
        }
        ensureNoterTab();
    });

    // --- Diagram (flow / ER / system, .npd) ---
    auto *diagAct = feat->addAction("Diagram — Flow / ER / System (.npd)");
    diagAct->setCheckable(true);
    diagAct->setStatusTip("Author flow charts, ER diagrams and system designs in .npd text with a "
                          "live canvas preview, AI generation, and PNG/SVG/PDF/HTML export.");
    connect(diagAct, &QAction::triggered, this, [this, diagAct]() {
        // Same on/off toggle as Noter: absent → create+focus; present-not-current
        // → focus; present-and-current → close.
        DiagramEditor *diag = nullptr;
        int existingIdx = -1;
        for (int i = 0; i < m_tabs->count(); ++i) {
            if (auto *de = qobject_cast<DiagramEditor*>(m_tabs->widget(i))) {
                diag = de;
                existingIdx = i;
                break;
            }
        }
        if (diag && existingIdx == m_tabs->currentIndex()) {
            m_tabs->removeTab(existingIdx);
            delete diag;
            diagAct->setChecked(false);
            return;
        }
        if (!diag) existingIdx = newDiagramTab(QString(), QString());
        else m_tabs->setCurrentIndex(existingIdx);
        diagAct->setChecked(true);
    });

    // --- Password Generator ---
    m_passwordAct = feat->addAction("Password Generator — Passwords / Passphrases / SSH keys");
    m_passwordAct->setCheckable(true);
    m_passwordAct->setStatusTip("Generate random passwords, word passphrases or SSH key pairs "
                                "(Ed25519, ECDSA, RSA) from the OS random source, with a live "
                                "entropy readout. Nothing is written to disk until you save "
                                "a key, and the clipboard self-clears.");
    connect(m_passwordAct, &QAction::triggered, this, [this]() {
        // Same on/off toggle as Noter and Diagram: absent -> create+focus;
        // present-not-current -> focus; present-and-current -> close.
        PasswordGenPanel *gen = nullptr;
        int existingIdx = -1;
        for (int i = 0; i < m_tabs->count(); ++i) {
            if (auto *pg = qobject_cast<PasswordGenPanel*>(m_tabs->widget(i))) {
                gen = pg;
                existingIdx = i;
                break;
            }
        }
        if (gen && existingIdx == m_tabs->currentIndex()) {
            // The destroyed-connection wired in newPasswordTab() clears the
            // checkmark, so every close path agrees — this one, Ctrl+W, the
            // tab's X, and Close All.
            m_tabs->removeTab(existingIdx);
            delete gen;
            if (m_tabs->count() == 0) newFile();
            return;
        }
        if (!gen) {
            newPasswordTab();
            return;
        }
        // Focusing an already-open tab. trigger() has ALREADY flipped the
        // action to unchecked on the way in, so without this the button
        // sits dark next to an open tab.
        m_tabs->setCurrentIndex(existingIdx);
        m_passwordAct->setChecked(true);
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
        "UTF-8", "UTF-8 BOM",
        "UTF-16 LE", "UTF-16 LE BOM",
        "UTF-16 BE", "UTF-16 BE BOM",
        "UTF-32 LE BOM", "UTF-32 BE BOM",
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
        "UTF-8", "UTF-8 BOM",
        "UTF-16 LE", "UTF-16 LE BOM",
        "UTF-16 BE", "UTF-16 BE BOM",
        "UTF-32 LE BOM", "UTF-32 BE BOM",
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

    // ═══ LANGUAGE — narrow two-tier layout, 82 lexers / 238 file extensions ═══
    // v0.1.55 — keeps the original simple structure (Normal Text + Common
    // + SQL Dialects + More Languages submenu) the user is used to. The
    // 31 newly-added languages are folded into "More Languages" in one
    // alphabetical sweep so the top-level Language menu doesn't bloat.
    auto *lang = mb->addMenu("&Language");
    lang->addAction("Normal Text", this, [this, E]() {
        if (auto *e = E()) { e->setLanguage("Plain Text"); m_statusBar->updateLanguage("Plain Text"); }
    });
    lang->addSeparator();

    // Common languages — flat, alphabetical for predictable scanning.
    // SSOT: commonLanguageTokens() (also feeds MCP list_languages).
    for (const QString &l : commonLanguageTokens()) {
        lang->addAction(l, this, [this, E, l]() {
            if (auto *e = E()) { e->setLanguage(l); m_statusBar->updateLanguage(l); }
        });
    }
    lang->addSeparator();

    // SQL dialect submenu — display-only labels (lexer is QsciLexerSQL).
    auto *sqlMenu = lang->addMenu("SQL Dialects");
    sqlMenu->addAction("SQL (ANSI / Generic)", this, [this, E]() {
        if (auto *e = E()) { e->setLanguage("SQL"); m_statusBar->updateLanguage("SQL"); }
    });
    sqlMenu->addAction("T-SQL (SQL Server)", this, [this, E]() {
        if (auto *e = E()) { e->setLanguage("SQL"); m_statusBar->updateLanguage("T-SQL (SQL Server)"); }
    });
    sqlMenu->addAction("PL/SQL (Oracle)", this, [this, E]() {
        if (auto *e = E()) { e->setLanguage("SQL"); m_statusBar->updateLanguage("PL/SQL (Oracle)"); }
    });
    sqlMenu->addAction("MySQL", this, [this, E]() {
        if (auto *e = E()) { e->setLanguage("SQL"); m_statusBar->updateLanguage("MySQL"); }
    });
    sqlMenu->addAction("PostgreSQL", this, [this, E]() {
        if (auto *e = E()) { e->setLanguage("SQL"); m_statusBar->updateLanguage("PostgreSQL"); }
    });
    sqlMenu->addAction("SQLite", this, [this, E]() {
        if (auto *e = E()) { e->setLanguage("SQL"); m_statusBar->updateLanguage("SQLite"); }
    });
    lang->addSeparator();

    // More Languages — the long tail. Single alphabetical submenu listing
    // everything else. Adding a new language is one line: append to
    // moreLanguageTokens() (pre-sorted; also feeds MCP list_languages).
    auto *moreLang = lang->addMenu("More Languages");
    const QStringList &more = moreLanguageTokens();
    for (const QString &l : more) {
        moreLang->addAction(l, this, [this, E, l]() {
            if (auto *e = E()) { e->setLanguage(l); m_statusBar->updateLanguage(l); }
        });
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
    // v0.1.75 — runtime font-pack installer. Lite binary stays lite;
    // ~27 premium fonts (JetBrains Mono / Fira Code / Cascadia / IBM
    // Plex / Geist / Inter / Source Serif …) are fetched on demand to
    // ~/.local/share/notepatra/fonts and picked up by Qt immediately.
    auto *manageFontsAct = settings->addAction(tr("Manage &Fonts..."));
    manageFontsAct->setStatusTip(tr("Download premium open-source fonts (JetBrains Mono, Fira Code, Cascadia Code, IBM Plex, Inter, Source Serif, …) to use as the editor or UI font."));
    connect(manageFontsAct, &QAction::triggered, this, [this]() {
        FontPackDialog dlg(this);
        dlg.exec();
        // After the dialog closes, refresh the editor font in case the
        // user installed the family their config points at.
        applyConfigEverywhere();
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
    // v0.1.96 — platform-conventional config dir.
    QString pluginDir = Config::appConfigDir() + QStringLiteral("/plugins");
    m_pluginManager.loadPlugins(pluginDir);
    QMenu *pluginsMenu = feat;  // "inbuilt plugin" actions append to Tools

    // SQL Formatter (inbuilt) — opens in a new tab
    pluginsMenu->addAction("SQL Formatter (inbuilt)", this, [this, E]() {
        auto *panel = new SqlFmtPanel;
        // Theme propagation — SQL Formatter chrome + lexer colours track the
        // current theme on runtime flip.
        connect(this, &MainWindow::themeChanged, panel, &SqlFmtPanel::onThemeChanged);
        if (E()) panel->setInput(E()->hasSelectedText() ? E()->selectedText() : E()->text());
        exitAiFullscreenIfActive();
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
            if (repoRoot.isEmpty()) return;
            m_explorer->setRoot(repoRoot);
            // v0.1.70 — don't auto-show explorer; seed root only. User
            // shows it via View > Files Explorer if they want.
        });
        // New signals from the v2 GitPanel rewrite — `openFileInTab` opens a
        // plain-file tab, `openDiffInTab` opens a CompareWidget tab showing
        // HEAD-vs-working-copy (same pattern as the FormatterPanel diff path
        // a few dozen lines below).
        connect(panel, &GitPanel::openFileInTab, this, &MainWindow::openFile);
        // v0.1.62 — git-aware diff opener. The CompareWidget receives the
        // repo root + relative file path so it can render its per-hunk
        // Stage / Revert strip. Legacy openDiffInTab signal stays defined
        // for out-of-tree consumers but no longer fires from the panel.
        connect(panel, &GitPanel::openDiffInTabWithGit, this,
                [this](const QString &title, const QString &leftText,
                       const QString &rightText, const QString &repoRoot,
                       const QString &relPath) {
            auto *cmp = new CompareWidget;
            connect(this, &MainWindow::themeChanged, cmp, &CompareWidget::onThemeChanged);
            exitAiFullscreenIfActive();
            int idx = m_tabs->addTab(cmp, title);
            m_tabs->setCurrentIndex(idx);
            connect(cmp, &CompareWidget::closeRequested, this, [this, cmp]() {
                int i = m_tabs->indexOf(cmp);
                if (i >= 0) closeTab(i);
            });
            // setGitContext BEFORE compare(); compare() calls recompare()
            // which rebuilds the per-hunk strip. If we set context after,
            // the first paint would have an empty strip until the next
            // recompare cycle.
            cmp->setGitContext(repoRoot, relPath);
            cmp->compare(leftText, "HEAD", rightText, "Working copy");
        });

        // v0.1.62 — conflict resolver. The user clicked the inline
        // "Resolve" button on a UU file row. Open the file in an editor
        // tab and dock a MergeHelperWidget at the bottom so the per-
        // region action buttons render alongside the buffer.
        connect(panel, &GitPanel::openMergeHelperRequested, this,
                [this](const QString &repoRoot, const QString &relPath) {
            const QString abs = QDir(repoRoot).filePath(relPath);
            openFile(abs);

            // Locate the editor we just opened. openFile() ends with the
            // new tab as current; the widget there is an Editor*.
            Editor *editor = nullptr;
            if (m_tabs && m_tabs->currentWidget()) {
                editor = qobject_cast<Editor *>(m_tabs->currentWidget());
            }
            if (!editor) return;

            // The merge helper is constructed as a top-level window so
            // it doesn't fight the tab layout (which is owned by m_tabs
            // and not easily augmented per-tab without invasive rewiring).
            auto *helper = new MergeHelperWidget;
            helper->setWindowFlags(Qt::Window);
            helper->setWindowTitle(QString("Resolve conflicts — %1")
                                       .arg(QFileInfo(relPath).fileName()));
            helper->resize(720, 240);
            // Auto-close on full resolution so the workflow ends cleanly.
            // Connected BEFORE attach() — attach() rescans and can emit
            // allConflictsResolved immediately when zero conflicts remain.
            connect(helper, &MergeHelperWidget::allConflictsResolved,
                    helper, [helper]() {
                        helper->close();
                        helper->deleteLater();
                    });
            helper->attach(editor, abs);
            helper->show();
        });
        if (auto *e = E(); e && !e->filePath().isEmpty()) {
            panel->refresh(e->filePath());
            e->updateGitGutter();
        }
        exitAiFullscreenIfActive();
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
            exitAiFullscreenIfActive();
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

            // v0.1.48 — use OllamaStatus widget's cached probe (background
            // poll, non-blocking) instead of OllamaClient::isAvailable()
            // which spins a 3-second QEventLoop and froze the UI.
            if (!ollamaBar->isAvailable()) {
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
        exitAiFullscreenIfActive();
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

        // v0.1.48 — Show thinking toggle (was missing on HTML Tools).
        auto *thinkingCheckHtml = new QCheckBox("Show thinking (slower, may break format)");
        thinkingCheckHtml->setChecked(false);
        thinkingCheckHtml->setStyleSheet("padding: 4px 8px; font-size: 11px;");
        if (panelLayout) panelLayout->addWidget(thinkingCheckHtml);

        auto *aiBtn = new QPushButton("AI Fix (Ollama)");
        aiBtn->setFixedHeight(26);
        auto *btnLayout = p->findChild<QHBoxLayout *>();
        if (btnLayout) btnLayout->insertWidget(btnLayout->count() - 3, aiBtn);

        // v0.1.48 — wire Show Diff (was missing for HTML Tools)
        connect(p, &FormatterPanel::showDiffRequested, this,
                [this](const QString &before, const QString &after, const QString &title) {
            auto *cmp = new CompareWidget;
            connect(this, &MainWindow::themeChanged, cmp, &CompareWidget::onThemeChanged);
            cmp->compare(before, "Before", after, "After");
            exitAiFullscreenIfActive();
            int idx = m_tabs->addTab(cmp, title);
            m_tabs->setCurrentIndex(idx);
            connect(cmp, &CompareWidget::closeRequested, this, [this, cmp]() {
                int i = m_tabs->indexOf(cmp);
                if (i >= 0) closeTab(i);
            });
        });

        struct HtmlFixState { QString originalInput; bool hasResult = false; };
        auto *fixState = new HtmlFixState;

        auto *ollama = new OllamaClient(p);

        connect(aiBtn, &QPushButton::clicked, p, [p, ollama, ollamaBar, thinkingCheckHtml, fixState]() {
            QString input = p->inputText();
            if (input.isEmpty()) {
                p->setStatus("Empty input — paste HTML into the panel below first", true);
                return;
            }
            fixState->originalInput = input;
            fixState->hasResult = false;

            if (!ollamaBar->isAvailable()) {  // v0.1.48 — cached, non-blocking
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
                ollamaBar->checkStatus();
                model = ollamaBar->selectedModel();
                if (model.isEmpty() || model.startsWith("(")) {
                    p->setStatus("No models installed — run: ollama pull qwen2.5:7b", true);
                    return;
                }
            }

            bool wantThinking = thinkingCheckHtml->isChecked();
            ollama->setModel(model);
            p->setStatus(QString("Asking %1 to fix the HTML%2...")
                         .arg(model)
                         .arg(wantThinking ? " (with reasoning)" : ""), false);
            p->setOutput("Asking " + model + "...\n");

            // ─── Strict minimal-change prompt ─────────────────────────
            // Same framework JSON Tools uses — numbered rules, "preserve
            // everything", "do NOT add new content". v0.1.48 brought HTML
            // up to JSON parity after user reported the AI was occasionally
            // adding tags / restructuring documents.
            const QString rules =
                "RULES (follow ALL of these):\n"
                "1. PRESERVE the original element order, attribute order, content text, "
                "indentation, and whitespace EXACTLY where the input was already correct.\n"
                "2. Make MINIMAL changes — only patch the broken parts.\n"
                "3. PRESERVE ALL CONTENT. Never delete elements, attributes, text nodes, "
                "or comments. Never add new tags, attributes, or content the user didn't write.\n"
                "4. Fix ONLY broken syntax:\n"
                "   - Close unclosed tags\n"
                "   - Self-close void elements (br hr img input meta link area base col embed source track wbr) as ' />'\n"
                "   - Quote unquoted attribute values\n"
                "   - Lowercase tag names if the document is otherwise lowercase\n"
                "   - Remove stray < or > that aren't part of a valid tag\n"
                "5. Output ONLY the corrected HTML. No prose, no markdown ``` "
                "fences, no comments, no <think> blocks, no preamble.\n"
                "6. If the input is already valid HTML, output it UNCHANGED.\n";

            QString systemPrompt =
                "You are a minimal-change HTML patcher. Your job is to take broken HTML "
                "and return the SAME HTML with ONLY the broken parts fixed. Preserve "
                "everything else exactly — element order, attribute order, content text, "
                "indentation, whitespace.\n\n" + rules;

            QString modelLower = model.toLower();
            bool isGemmaLike = modelLower.contains("gemma") ||
                               modelLower.contains("phi") ||
                               modelLower.contains("tiny");

            QString userPrompt;
            if (isGemmaLike) {
                userPrompt = rules + "\n"
                    "Fix this broken HTML now with MINIMAL changes. Return ONLY the corrected HTML.\n\n"
                    "BROKEN HTML:\n" + input;
            } else {
                userPrompt =
                    "Fix ONLY the broken parts of this HTML. Make MINIMAL changes. "
                    "PRESERVE the original element order, attribute order, and content. "
                    "Do NOT add new tags. Do NOT remove content. Return ONLY the corrected HTML.\n\n"
                    "BROKEN HTML:\n" + input;
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

            // 1. Strip <think>...</think> blocks
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

            // 3. Strip "Here is the fixed HTML:" prose prefix — find first <
            //    that looks like a real tag start.
            int firstAngle = cleaned.indexOf('<');
            if (firstAngle > 0) cleaned = cleaned.mid(firstAngle);

            // 4. Format the result; fall back to cleaned text if formatHtml
            //    returns something useless (empty / single space).
            QString formatted = RustCore::formatHtml(cleaned, 2);
            QString result = formatted.length() > 1 ? formatted : cleaned;

            if (result.isEmpty()) {
                p->setOutput("(model returned empty response after stripping)\n\nRaw response:\n" + response);
                p->setStatus("✗ AI fix returned empty — try a different model or enable Show thinking", true);
            } else {
                p->setOutput(result);
                QString desc = FormatterPanel::describeChanges(fixState->originalInput, result);
                int origChars = fixState->originalInput.length();
                int newChars  = result.length();
                int newLines  = result.count('\n') + 1;
                p->setStatus(QString("✓ AI fix complete — %1 chars, %2 lines (%3). Click 'Show Diff' to see changes.")
                             .arg(newChars).arg(newLines).arg(desc), false);
                p->logAction("AI Fix (Ollama)", origChars, newChars, desc);
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
        exitAiFullscreenIfActive();
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

        // v0.1.48 — Show thinking toggle (was missing on Bracket Tools).
        auto *thinkingCheckBr = new QCheckBox("Show thinking (slower, may break format)");
        thinkingCheckBr->setChecked(false);
        thinkingCheckBr->setStyleSheet("padding: 4px 8px; font-size: 11px;");
        if (panelLayout) panelLayout->addWidget(thinkingCheckBr);

        auto *aiBtn = new QPushButton("AI Fix (Ollama)");
        aiBtn->setFixedHeight(26);
        auto *btnLayout = p->findChild<QHBoxLayout *>();
        if (btnLayout) btnLayout->insertWidget(btnLayout->count() - 3, aiBtn);

        // v0.1.48 — wire Show Diff (was missing for Bracket Tools)
        connect(p, &FormatterPanel::showDiffRequested, this,
                [this](const QString &before, const QString &after, const QString &title) {
            auto *cmp = new CompareWidget;
            connect(this, &MainWindow::themeChanged, cmp, &CompareWidget::onThemeChanged);
            cmp->compare(before, "Before", after, "After");
            exitAiFullscreenIfActive();
            int idx = m_tabs->addTab(cmp, title);
            m_tabs->setCurrentIndex(idx);
            connect(cmp, &CompareWidget::closeRequested, this, [this, cmp]() {
                int i = m_tabs->indexOf(cmp);
                if (i >= 0) closeTab(i);
            });
        });

        struct BrFixState { QString originalInput; bool hasResult = false; };
        auto *fixState = new BrFixState;

        auto *ollama = new OllamaClient(p);

        connect(aiBtn, &QPushButton::clicked, p, [p, ollama, ollamaBar, thinkingCheckBr, fixState]() {
            QString input = p->inputText();
            if (input.isEmpty()) {
                p->setStatus("Empty input — paste code into the panel below first", true);
                return;
            }
            fixState->originalInput = input;
            fixState->hasResult = false;

            if (!ollamaBar->isAvailable()) {  // v0.1.48 — cached, non-blocking
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
                ollamaBar->checkStatus();
                model = ollamaBar->selectedModel();
                if (model.isEmpty() || model.startsWith("(")) {
                    p->setStatus("No models installed — run: ollama pull qwen2.5:7b", true);
                    return;
                }
            }

            bool wantThinking = thinkingCheckBr->isChecked();
            ollama->setModel(model);
            p->setStatus(QString("Asking %1 to fix brackets%2...")
                         .arg(model)
                         .arg(wantThinking ? " (with reasoning)" : ""), false);
            p->setOutput("Asking " + model + "...\n");

            // ─── Strict minimal-change prompt ─────────────────────────
            // The pre-v0.1.48 prompt said "Fix ALL bracket issues" — which
            // many models read as license to "improve" the code by adding
            // missing semicolons, fixing typos, renaming variables, etc.
            // The new prompt is laser-focused on bracket / paren / brace /
            // keyword-pair syntax ONLY. Same numbered-rules framework JSON
            // and HTML Tools use.
            const QString rules =
                "RULES (follow ALL of these):\n"
                "1. PRESERVE the code EXACTLY. Same line order, same indentation, "
                "same identifiers, same operators, same strings, same comments.\n"
                "2. Fix ONLY bracket / paren / brace / keyword-pair syntax:\n"
                "   - Add missing close brackets: )  ]  }\n"
                "   - Add missing open brackets: (  [  { (rare; only when clearly needed)\n"
                "   - Match shell/SQL keyword pairs: if/fi · do/done · case/esac · BEGIN/END\n"
                "   - Remove stray duplicate brackets that don't match anything\n"
                "3. Do NOT add, remove, or rename any code statements.\n"
                "4. Do NOT add semicolons, fix typos, change variable names, "
                "reformat indentation, or 'improve' the code in any way.\n"
                "5. Do NOT add new functions, imports, or comments.\n"
                "6. Output ONLY the corrected code. No prose, no markdown ``` "
                "fences, no explanation, no <think> blocks, no preamble.\n"
                "7. If the input has balanced brackets already, output it UNCHANGED.\n";

            QString systemPrompt =
                "You are a minimal-change bracket patcher. Your only job is to balance "
                "brackets, parentheses, braces, and keyword pairs (if/fi, do/done, BEGIN/END, etc.). "
                "Preserve EVERY other character of the source code exactly.\n\n" + rules;

            QString modelLower = model.toLower();
            bool isGemmaLike = modelLower.contains("gemma") ||
                               modelLower.contains("phi") ||
                               modelLower.contains("tiny");

            QString userPrompt;
            if (isGemmaLike) {
                userPrompt = rules + "\n"
                    "Fix ONLY bracket/paren/brace mismatches. Make NO other changes. "
                    "Return ONLY the corrected code.\n\n"
                    "BROKEN CODE:\n" + input;
            } else {
                userPrompt =
                    "Fix ONLY bracket / paren / brace / keyword-pair syntax. "
                    "Make MINIMAL changes. Do NOT 'improve' the code. "
                    "Return ONLY the corrected code.\n\n"
                    "BROKEN CODE:\n" + input;
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

            // 1. Strip <think>...</think> blocks
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

            // 3. For brackets we don't strip a "Here is..." prose prefix —
            //    code can legitimately start with any character (including
            //    text in shell scripts that begin with comments). The
            //    fence-stripping above + <think> removal are usually enough.

            QString result = cleaned;

            if (result.isEmpty()) {
                p->setOutput("(model returned empty response after stripping)\n\nRaw response:\n" + response);
                p->setStatus("✗ AI fix returned empty — try a different model or enable Show thinking", true);
            } else {
                p->setOutput(result);
                QString desc = FormatterPanel::describeChanges(fixState->originalInput, result);
                int origChars = fixState->originalInput.length();
                int newChars  = result.length();
                int newLines  = result.count('\n') + 1;
                p->setStatus(QString("✓ AI fix complete — %1 chars, %2 lines (%3). Click 'Show Diff' to see changes.")
                             .arg(newChars).arg(newLines).arg(desc), false);
                p->logAction("AI Fix (Ollama)", origChars, newChars, desc);
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
        exitAiFullscreenIfActive();
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
        box.setWindowTitle("About " NOTEPATRA_FLAVOR_NAME);
        box.setIconPixmap(windowIcon().pixmap(64, 64));
        box.setTextFormat(Qt::RichText);
        box.setTextInteractionFlags(Qt::TextBrowserInteraction);
        box.setText(QString(
            "<h2 style='margin:0 0 6px 0;'>" NOTEPATRA_FLAVOR_NAME " v%1</h2>"
            "<p style='color:#888; margin:0 0 14px 0;'>The first editor built for the AI era.</p>"
            "<p>A blazing-fast native code editor for Linux, macOS, and Windows.<br>"
            "Native C++ + Rust. No Electron.<br>"
            "238 file extensions · 82 language lexers. Plugin system. 2 GB files.<br>"
            "6 AI backends — Ollama / llama.cpp / OpenRouter / Ollama Cloud / OpenAI / Azure OpenAI. Zero telemetry.</p>"
            "<p>"
            "🌐 Website: <a href='https://notepatra.org'>notepatra.org</a><br>"
            "💻 Source: <a href='https://github.com/singhpratech/notepatra'>github.com/singhpratech/notepatra</a>"
            "</p>"
            "<p style='color:#888; font-size:11px; margin-top:14px;'>"
            "Envisioned by <a href='https://theaivibe.org/about'>Prateek Singh</a>. Built with Claude."
            "</p>")
            .arg(version));
        box.exec();
    });
}

// ── Noter app-lifetime reminder service ────────────────────────────────
// Audit fix: reminders used to die with the Noter tab (the engine lived
// inside NotesPanel). The service below is MainWindow-owned, so reminders
// fire whenever Notepatra is running — any tab, Noter open or not.
// Reminders missed while the app was CLOSED arrive as ONE catch-up digest
// shortly after launch (OS-level scheduling is explicitly out of scope).

NotesPanel *MainWindow::findNoterPanel(int *indexOut) const {
    for (int i = 0; i < m_tabs->count(); ++i)
        if (auto *np = qobject_cast<NotesPanel*>(m_tabs->widget(i))) {
            if (indexOut) *indexOut = i;
            return np;
        }
    if (indexOut) *indexOut = -1;
    return nullptr;
}

void MainWindow::ensureNoterReminderService() {
    if (m_noterReminderEngine) return;                      // idempotent
    m_noterTodos = new NotesTodos(NotesPanel::todosDbPath(), this);
    m_noterTodos->open(nullptr);
    m_noterReminderEngine = new NotesReminderEngine(m_noterTodos, this);
    // Live fire → desktop toast. EXACT body format from the old panel
    // handler (notes.cpp reminderDue lambda) so user-visible text is
    // unchanged.
    connect(m_noterReminderEngine, &NotesReminderEngine::reminderDue, this,
            [this](const TodoRow &r) {
        QString body = r.text;
        if (!r.owner.isEmpty()) body += QStringLiteral("  ") + r.owner;
        if (!r.meetingTitle.isEmpty())
            body += QStringLiteral("\n— from \"%1\"").arg(r.meetingTitle);
        m_noterToastNote    = r.sourceFile;
        m_noterToastShownMs = QDateTime::currentMSecsSinceEpoch();
        if (QSystemTrayIcon *tray = NotesPanel::notificationTray())
            connect(tray, &QSystemTrayIcon::messageClicked,
                    this, &MainWindow::onNoterTrayMessageClicked,
                    Qt::UniqueConnection);   // member-fn ptr → UniqueConnection valid
        const bool delivered =
            NotesPanel::fireDesktopNotification(tr("Noter reminder"), body);
        // Tray-less AND tab closed → stash for banner replay on next open.
        // (Panel open: its own banner already showed via reminderDue.)
        if (!delivered && !findNoterPanel()) m_noterUndelivered.append(r);
    });
    // Catch-up → ONE batched digest.
    connect(m_noterReminderEngine, &NotesReminderEngine::missedBatch, this,
            [this](const QVector<TodoRow> &batch) {
        const QString title =
            tr("Noter — %n reminder(s) fired while you were away", "", batch.size());
        QString body; int n = 0;
        for (const TodoRow &r : batch) {
            if (n++ >= 4) { body += tr("\n…and %1 more").arg(batch.size() - n + 1); break; }
            body += (n > 1 ? "\n" : "") + r.text;
        }
        m_noterToastNote    = (batch.size() == 1) ? batch.first().sourceFile : QString();
        m_noterToastShownMs = QDateTime::currentMSecsSinceEpoch();
        if (QSystemTrayIcon *tray = NotesPanel::notificationTray())
            connect(tray, &QSystemTrayIcon::messageClicked,
                    this, &MainWindow::onNoterTrayMessageClicked,
                    Qt::UniqueConnection);
        const bool delivered = NotesPanel::fireDesktopNotification(title, body);
        if (!delivered && !findNoterPanel()) m_noterUndelivered += batch;
    });
    // Synchronous catch-up BEFORE start(): rows are marked 'fired' before
    // the first 60s tick can see them — the N-popup leak is closed by
    // construction. Also preserves the invariant that missedBatch never
    // fires while a panel exists (panel creation ensures the service FIRST).
    m_noterReminderEngine->catchUpMissed();
    m_noterReminderEngine->start();
}

void MainWindow::onNoterTrayMessageClicked() {
    // messageClicked carries no payload and fires for ANY message from our
    // tray icon — only honor it within 60s of showing a Noter toast/digest.
    if (m_noterToastShownMs == 0 ||
        QDateTime::currentMSecsSinceEpoch() - m_noterToastShownMs > 60000) return;
    m_noterToastShownMs = 0;
    const QString note = m_noterToastNote;
    NotesPanel *noter = ensureNoterTab();
    if (noter && !note.isEmpty()) noter->openNoteFile(note);
}

NotesPanel *MainWindow::ensureNoterTab() {
    int idx = -1;
    NotesPanel *noter = findNoterPanel(&idx);
    if (!noter) {
        ensureNoterReminderService();   // idempotent; creates dirs now (user asked for Noter)
        noter = new NotesPanel(nullptr, m_noterTodos, m_noterReminderEngine);
        // A5 — theme parity. The constructor already applied the CURRENT
        // Config theme; this keeps a live switch (Settings → Theme) in
        // sync without reopening the tab, matching the AIPanel pattern.
        connect(this, &MainWindow::themeChanged,
                noter, &NotesPanel::onThemeChanged);
        // Truthful Features-menu indicator on EVERY deletion path (toggle-
        // close AND generic closeTab). Receiver = the action → connection
        // auto-dies with it at shutdown.
        if (m_noterAct)
            connect(noter, &QObject::destroyed, m_noterAct,
                    [act = m_noterAct]() { act->setChecked(false); });
        exitAiFullscreenIfActive();
        idx = m_tabs->addTab(noter, "Noter");
        // Replay reminders that fired tray-less while no panel existed.
        if (!m_noterUndelivered.isEmpty()) {
            noter->replayReminders(m_noterUndelivered);
            m_noterUndelivered.clear();
        }
    }
    m_tabs->setCurrentIndex(idx);
    if (m_noterAct) m_noterAct->setChecked(true);
    return noter;
}

// One diagram-tab creation path — the Features->Diagram menu action and the
// MCP create_diagram verb both call this (no duplicated creation logic).
int MainWindow::newDiagramTab(const QString &source, const QString &title) {
    auto *diag = new DiagramEditor;
    if (!source.isEmpty()) diag->setNpdText(source);
    exitAiFullscreenIfActive();
    const int idx = m_tabs->addTab(diag, title.isEmpty()
                                             ? QStringLiteral("Diagram") : title);
    connect(diag, &DiagramEditor::titleChanged, this,
            [this, diag](const QString &t) {
        const int i = m_tabs->indexOf(diag);
        if (i >= 0) m_tabs->setTabText(i, t);
    });
    m_tabs->setCurrentIndex(idx);
    return idx;
}

// One password-tab creation path. The panel is a plain QWidget, never an
// Editor subclass — that is what keeps generated values out of
// session.json, out of the AI context sweep, and unreadable over MCP,
// all three of which gate on qobject_cast<Editor*>.
int MainWindow::newPasswordTab() {
    auto *gen = new PasswordGenPanel;
    // Live Settings > Theme switch, matching every other panel — the
    // app-wide stylesheet does not reach ordinary widgets.
    connect(this, &MainWindow::themeChanged, gen, &PasswordGenPanel::onThemeChanged);
    // Truthful menu/toolbar checked-state on EVERY close path, including
    // Ctrl+W and Close All, which route through the generic closeTab().
    if (m_passwordAct)
        connect(gen, &QObject::destroyed, m_passwordAct,
                [act = m_passwordAct]() { act->setChecked(false); });
    connect(gen, &PasswordGenPanel::insertRequested, this,
            [this](const QString &text) {
        // NOT currentEditor(): the Password panel is the current tab
        // whenever its own button is clickable, so that is always null.
        Editor *target = m_lastEditor;
        if (!target) {
            for (int i = 0; i < m_tabs->count(); ++i) {
                if (Editor *ed = m_tabs->editorAt(i)) { target = ed; break; }
            }
        }
        if (!target) {
            statusBar()->showMessage(tr("No editor tab to insert into — "
                                        "open a file first."), 4000);
            return;
        }
        target->insert(text);
        const int idx = m_tabs->indexOf(target);
        if (idx >= 0) m_tabs->setCurrentIndex(idx);
        statusBar()->showMessage(
            tr("Inserted into \"%1\". It is an editor buffer now — it will be "
               "written to the session file if you leave it open.")
                .arg(idx >= 0 ? m_tabs->tabText(idx) : QString()), 6000);
    });
    connect(gen, &PasswordGenPanel::newTabRequested, this,
            [this](const QString &text) {
        newFile();
        if (auto *e = currentEditor()) e->setText(text);
        statusBar()->showMessage(
            tr("Opened as an editor tab. This is no longer covered by the "
               "generator's guarantees — an unsaved buffer is written to the "
               "session file and its contents reach the AI context."), 8000);
    });
    connect(gen, &PasswordGenPanel::statusMessage, this,
            [this](const QString &text) { statusBar()->showMessage(text, 5000); });
    exitAiFullscreenIfActive();
    // The tab title is visible to any connected MCP client through
    // list_open_tabs, so it stays a constant label and never carries a
    // generated value.
    const int idx = m_tabs->addTab(gen, QStringLiteral("Password Generator"));
    m_tabs->setCurrentIndex(idx);
    if (m_passwordAct) m_passwordAct->setChecked(true);
    return idx;
}

void MainWindow::buildToolbar() {
    auto *featureTb = addToolBar("Built-in Tools");
    featureTb->setObjectName("featureShortcutBar");
    featureTb->setMovable(false);
    featureTb->setFloatable(false);
    featureTb->setIconSize(QSize(32, 32));
    featureTb->setToolButtonStyle(Qt::ToolButtonTextUnderIcon);
    // v0.1.70 — when a feature button is checkable AND checked (AI dock
    // open being the main case), render a subtle highlighted background +
    // accent border so the user sees the ON state at a glance.
    featureTb->setStyleSheet(
        "QToolButton#featureShortcutButton:checked { "
        "  background: rgba(78,201,176,0.18); "
        "  border: 1px solid rgba(78,201,176,0.55); "
        "  border-radius: 6px; "
        "}");

    // v0.1.94 — every accent here sources from notepatraToolAccent() in
    // tool_colors.cpp so the toolbar button, the tab strip, and the
    // Welcome card all paint the same colour for any given tool. To
    // shift a tool's brand colour, edit ONLY tool_colors.cpp.
    addFeatureShortcut(featureTb, findActionByPrefix(this, "Project Search"),
                       notepatraToolAccent("ProjectSearch"), "search", "Search",
                       "Recursively search file names + contents (Ctrl+Shift+G)");
    addFeatureShortcut(featureTb, findActionByPrefix(this, "AI Assistant"),
                       notepatraToolAccent("AI"), "ai", "AI",
                       "Toggle AI Assistant dock (Ctrl+Q) — ON / OFF",
                       /*showCheckedState=*/true);
    addFeatureShortcut(featureTb, findActionByPrefix(this, "Password Generator"),
                       notepatraToolAccent("Password"), "password", "Password",
                       "Toggle Password Generator — passwords, passphrases "
                       "and SSH keys, offline — ON / OFF",
                       /*showCheckedState=*/true);
    addFeatureShortcut(featureTb, findActionByPrefix(this, "Terminal"),
                       notepatraToolAccent("Terminal"), "terminal", "Terminal",
                       "Open the built-in terminal");
    addFeatureShortcut(featureTb, findActionByPrefix(this, "Compare (inbuilt)"),
                       notepatraToolAccent("Compare"), "compare", "Compare",
                       "Open Compare");
    addFeatureShortcut(featureTb, findActionByPrefix(this, "JSON Tools"),
                       notepatraToolAccent("JSON"), "json", "JSON",
                       "Open JSON Tools");
    addFeatureShortcut(featureTb, findActionByPrefix(this, "HTML Tools"),
                       notepatraToolAccent("HTML"), "html", "HTML",
                       "Open HTML Tools");
    addFeatureShortcut(featureTb, findActionByPrefix(this, "SQL Formatter"),
                       notepatraToolAccent("SQL"), "sql", "SQL",
                       "Open SQL Formatter");
    addFeatureShortcut(featureTb, findActionByPrefix(this, "Bracket Tools"),
                       notepatraToolAccent("Bracket"), "brackets", "Brackets",
                       "Open Bracket Tools");
    addFeatureShortcut(featureTb, findActionByPrefix(this, "REST Client"),
                       notepatraToolAccent("REST"), "rest", "REST",
                       "Open REST Client");
    addFeatureShortcut(featureTb, findActionByPrefix(this, "Noter"),
                       notepatraToolAccent("Noter"), "noter", "Noter",
                       "Toggle Noter — meeting thinkpad (Ctrl+Alt+N) — ON / OFF",
                       /*showCheckedState=*/true);
    addFeatureShortcut(featureTb, findActionByPrefix(this, "Diagram"),
                       notepatraToolAccent("Diagram"), "diagram", "Diagram",
                       "Toggle Diagram — flow / ER / system (.npd) — ON / OFF",
                       /*showCheckedState=*/true);
    // v0.1.61 — dropped the standalone Git Integration toolbar shortcut.
    // Full VS Code-parity Source Control integration inside Coding mode
    // lands in v0.1.62 (agent-A roadmap: per-hunk gutter popup, stage/
    // unstage, branch picker, sync). Until then, the Plugins menu entry
    // (`Plugins → Git Integration (inbuilt)`) is still the way to open
    // the existing tab-based Git panel. Removing the toolbar shortcut
    // declutters the chrome and prevents the user from forming a habit
    // around a button we're about to relocate.
}

void MainWindow::setupShortcuts() {
    new QShortcut(QKeySequence("Ctrl+Tab"), this, [this]() {
        int idx = (m_tabs->currentIndex() + 1) % qMax(m_tabs->count(), 1);
        m_tabs->setCurrentIndex(idx);
    });

    // v0.1.115 — jump straight to the AI chat input. Ctrl+Shift+A / Ctrl+Q
    // TOGGLE the dock (hiding it if already open), which is the wrong verb for
    // "let me type a prompt now". Ctrl+Shift+I ("AI Input") reveals the dock if
    // hidden, then focuses the prompt field. ApplicationShortcut so it fires
    // even when the editor is intercepting keys.
    auto *focusAiInputSc = new QShortcut(QKeySequence("Ctrl+Shift+I"), this);
    focusAiInputSc->setContext(Qt::ApplicationShortcut);
    connect(focusAiInputSc, &QShortcut::activated, this, [this]() {
        setAiDockVisible(true);
        if (m_aiDockPanel) m_aiDockPanel->focusInput();
    });
    new QShortcut(QKeySequence("Ctrl+Shift+Tab"), this, [this]() {
        int idx = (m_tabs->currentIndex() - 1 + m_tabs->count()) % qMax(m_tabs->count(), 1);
        m_tabs->setCurrentIndex(idx);
    });

    // Ctrl+I — inline AI rewrite. Grabs the current selection, opens
    // InlineEditDialog, on Apply replaces the selection in place. The
    // shortcut is registered on `this` (the MainWindow) and given
    // ApplicationShortcut context so it fires even when the editor
    // (a QsciScintilla subclass) has focus and is intercepting key
    // events from its own command map.
    auto *inlineEditSc = new QShortcut(QKeySequence("Ctrl+I"), this);
    inlineEditSc->setContext(Qt::ApplicationShortcut);
    connect(inlineEditSc, &QShortcut::activated, this, [this]() {
        auto *e = currentEditor();
        if (!e) {
            statusBar()->showMessage("Open a file first", 2500);
            return;
        }
        if (!e->hasSelectedText()) {
            statusBar()->showMessage("Select code first, then press Ctrl+I", 3000);
            return;
        }
        const QString sel  = e->selectedText();
        const QString path = e->filePath();
        const QString lang = e->language();
        // v0.1.110 — Ctrl+I uses the model the user selected in the AI dock
        // (not a hardcoded 3B default). Empty → InlineEditDialog falls back to
        // the OllamaClient default.
        const QString model = m_aiDockPanel ? m_aiDockPanel->currentModelName()
                                            : QString();
        auto *dlg = new InlineEditDialog(sel, path, lang, model, this);
        dlg->setAttribute(Qt::WA_DeleteOnClose);
        connect(dlg, &InlineEditDialog::applyRequested, this, [this](const QString &text) {
            if (auto *ed = currentEditor(); ed && ed->hasSelectedText()) {
                // v0.1.110 — group the AI rewrite as ONE undo action so a
                // single Ctrl+Z cleanly reverts the whole inline edit (the
                // "safe to try" contract), matching every other multi-step
                // edit in editor.cpp.
                ed->beginUndoAction();
                ed->replaceSelectedText(text);
                ed->endUndoAction();
            }
        });
        dlg->show();
    });
}

// ═══════════════════════════════════════
// Session persistence + crash recovery
// ═══════════════════════════════════════

// Marker liveness window: a live owner with a marker older than this is
// assumed to be PID reuse after a crash, not a concurrent restore.
static const qint64 kRestoringMarkerLiveWindowMs = 10 * 60 * 1000;

static bool notepatraPidAlive(qint64 pid) {
    if (pid <= 0) return false;
#ifdef Q_OS_WIN
    HANDLE h = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, (DWORD)pid);
    if (!h) return false;
    DWORD code = 0;
    const bool alive = GetExitCodeProcess(h, &code) && code == STILL_ACTIVE;
    CloseHandle(h);
    return alive;
#else
    return ::kill((pid_t)pid, 0) == 0 || errno == EPERM;
#endif
}

QString MainWindow::sessionFilePath() {
    // v0.1.96 — use the platform-conventional config dir from Config.
    // Linux: ~/.config/notepatra/session.json
    // macOS: ~/Library/Application Support/Notepatra/session.json
    // Windows: %APPDATA%\Notepatra\session.json
    return Config::appConfigDir() + QStringLiteral("/session.json");
}

QString MainWindow::recoveryDir() {
    return Config::appConfigDir() + QStringLiteral("/recovery");
}

void MainWindow::saveSession() {
    if (m_standaloneNoSession) return;  // never clobber the primary's session.json
    // Never overwrite session.json before the deferred restore has run.
    if (!m_startupDone) return;
    // Live-owner skip (D8): another live instance is mid-restore and owns
    // session.json — writing here (autosave fires within ~5 s) clobbers the
    // very session it is restoring. Stay session-passive for our lifetime.
    if (m_restoreSkippedLiveOwner) return;

    QJsonArray tabs;
    bool anyTextDirty = false;
    QVector<QPair<int, Editor*>> contentTabs;  // tabs[] index -> editor

    for (int i = 0; i < m_tabs->count(); i++) {
        auto *e = m_tabs->editorAt(i);
        if (!e) continue;

        const QString path = e->filePath();
        const bool modified = e->isModified();
        // Skip pristine empty untitled tabs — nothing to restore.
        // length()==0 replaces text().isEmpty() — O(1), no extraction.
        if (path.isEmpty() && !modified && e->length() == 0) continue;

        QJsonObject tab;
        tab["path"] = path;
        tab["tabName"] = m_tabs->tabText(i).remove(" *").remove(" [recovered]");
        int line, col;
        e->getCursorPosition(&line, &col);
        tab["line"] = line;
        tab["col"] = col;
        tab["active"] = (i == m_tabs->currentIndex());
        tab["modified"] = modified;
        // Notepad++-style: unsaved content persists so the buffer survives
        // close → relaunch. Untitled tabs and modified file-backed tabs
        // store text; pristine file-backed tabs re-read from disk on restore.
        // Extraction is deferred until we know a write will happen.
        if (path.isEmpty() || modified) {
            anyTextDirty = anyTextDirty || e->sessionTextDirty();
            contentTabs.append(qMakePair(tabs.size(), e));
        }
        tabs.append(tab);
    }

    QJsonObject session;
    session["tabs"] = tabs;
    session["windowX"] = x();
    session["windowY"] = y();
    session["windowW"] = width();
    session["windowH"] = height();
    session["maximized"] = isMaximized();

    // Cheap change detection: metadata fingerprint (no buffer text) + the
    // per-editor text-dirty flags. Idle app with a big modified buffer →
    // identical fingerprint, no dirty flag → zero work, zero disk write.
    // (Old code re-serialized every open buffer to disk every 5 s, forever.)
    const QByteArray metaBytes =
        QJsonDocument(session).toJson(QJsonDocument::Compact);
    if (!anyTextDirty && metaBytes == m_lastSessionMeta
        && QFileInfo::exists(sessionFilePath()))
        return;

    // Something changed — splice in the expensive buffer extractions.
    for (const auto &ct : contentTabs) {
        QJsonObject t = tabs[ct.first].toObject();
        t["unsavedContent"] = ct.second->text();
        tabs[ct.first] = t;
    }
    session["tabs"] = tabs;

    QDir().mkpath(QFileInfo(sessionFilePath()).path());
    // Atomic write — .tmp + rename, so a crash mid-write can never leave a
    // truncated session.json (QSaveFile deliberately avoided, see
    // notes_storage.cpp).
    const QString target = sessionFilePath();
    const QString tmpPath = target + QStringLiteral(".tmp");
    QFile tmp(tmpPath);
    if (!tmp.open(QIODevice::WriteOnly | QIODevice::Truncate)) return;
    const QByteArray payload = QJsonDocument(session).toJson();
    const bool wroteAll = tmp.write(payload) == payload.size();
    tmp.close();
    if (!wroteAll) { QFile::remove(tmpPath); return; }
#ifdef Q_OS_WIN
    // MoveFileExW(REPLACE_EXISTING) swaps atomically. The old remove-then-
    // rename left a window with NO session.json on disk, and a rename
    // failure after the remove deleted the only surviving copy. On failure
    // keep the .tmp — it may be the freshest copy that exists.
    if (!::MoveFileExW(
            reinterpret_cast<const wchar_t *>(
                QDir::toNativeSeparators(tmpPath).utf16()),
            reinterpret_cast<const wchar_t *>(
                QDir::toNativeSeparators(target).utf16()),
            MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
        return;
    }
#else
    if (std::rename(QFile::encodeName(tmpPath).constData(),
                    QFile::encodeName(target).constData()) != 0) {
        QFile::remove(tmpPath);
        return;
    }
#endif

    // Only after a successful rename — a failed write leaves the flags set
    // so the next tick retries.
    m_lastSessionMeta = metaBytes;
    for (int i = 0; i < m_tabs->count(); i++)
        if (auto *e = m_tabs->editorAt(i)) e->clearSessionTextDirty();
}

void MainWindow::restoreSession() {
    // v0.1.96 — crash-safe restore.
    //
    // Pre-fix: a stale `.aws/config` entry in session.json caused a
    // Windows user's Notepatra to hang on every launch. session.json
    // never got rewritten — so the bad entry persisted across the
    // user killing the process via Task Manager, looping the hang.
    // Only a manual rename of session.json fixed it.
    //
    // Fix: write a `session.json.restoring` marker BEFORE doing any
    // restore work; delete it on success. On next launch, if the
    // marker still exists, the previous restore hung — move
    // session.json aside (preserving the data) and bail out without
    // restoring. User sees a deferred dialog ("Previous session
    // couldn't restore — your tabs are saved at …") AFTER the empty
    // editor is up, NOT during construction.
    // Belt-and-braces: the sole caller (runStartupNow) is already gated, but
    // this also guarantees a standalone window never creates the marker.
    if (m_standaloneNoSession) return;

    const QString sessionPath = sessionFilePath();
    const QString marker = sessionPath + QStringLiteral(".restoring");

    if (QFileInfo::exists(marker)) {
        QByteArray stage;
        { QFile mf(marker); if (mf.open(QIODevice::ReadOnly)) stage = mf.readAll(); }
        if (stage.startsWith("cliopen")) {
            // Previous hang was in the post-restore opens — session is fine.
            QFile::remove(marker);
        } else {
        // D8 — marker format: "<pid> <msecsSinceEpoch>" (legacy: stage tag
        // or msecs only — both parse as pid-unknown and take the stale path).
        qint64 ownerPid = -1, stampMs = -1;
        {
            const QList<QByteArray> parts = stage.trimmed().split(' ');
            if (parts.size() >= 2) {
                bool okP = false, okT = false;
                const qint64 p = parts.at(0).toLongLong(&okP);
                const qint64 t = parts.at(1).toLongLong(&okT);
                if (okP) ownerPid = p;
                if (okT) stampMs = t;
            }
        }
        const qint64 ageMs = stampMs > 0
            ? QDateTime::currentMSecsSinceEpoch() - stampMs : -1;
        if (ownerPid > 0
            && ownerPid != QCoreApplication::applicationPid()
            && notepatraPidAlive(ownerPid)
            && ageMs >= 0 && ageMs < kRestoringMarkerLiveWindowMs) {
            // Another live instance is restoring this session RIGHT NOW —
            // not a stale crash. Leave session.json and the marker alone
            // (the owner deletes it when its restore completes). The
            // pid != ours guard handles OS PID reuse handing us the crashed
            // owner's PID; the age window bounds reuse by unrelated processes.
            qWarning("notepatra: session restore skipped; instance %lld "
                     "is mid-restore", (long long)ownerPid);
            m_restoreSkippedLiveOwner = true;
            // Session-passive for our lifetime (saveSession no-ops; close
            // prompts per modified tab) — say so instead of failing silently.
            queueStartupNotice(tr("Another Notepatra window is restoring your "
                                  "saved session — this window opened separately "
                                  "and won't auto-save the session."));
            return;
        }
        // Previous restore was interrupted (likely a hang). Move
        // session.json aside so the next launch starts clean.
        const QString failedAside = sessionPath + QStringLiteral(".failed-%1")
                                        .arg(QDateTime::currentMSecsSinceEpoch());
        QFile::rename(sessionPath, failedAside);
        QFile::remove(marker);
        // D5 — joins the startup-notice queue: surfaces non-modally after
        // first show instead of racing show() with a deferred exec().
        queueStartupNotice(tr("The previous Notepatra session didn't finish restoring — likely a file "
                              "in your tab list hung on open.\nYour tab list is preserved at:\n%1\n"
                              "To retry, remove the problematic entry from that file, then rename it "
                              "back to session.json.").arg(QDir::toNativeSeparators(failedAside)));
        return;
        }
    }

    QFile f(sessionPath);
    if (!f.open(QIODevice::ReadOnly)) return;

    QJsonDocument doc = QJsonDocument::fromJson(f.readAll());
    f.close();
    if (doc.isNull()) return;
    QJsonObject session = doc.object();

    // Drop the marker — if the restore loop below hangs, this file
    // persists and the next launch will hit the bail-out path above.
    // Format "<pid> <ms>" lets that launch tell a live concurrent restore
    // from a dead one (D8).
    {
        QFile m(marker);
        if (m.open(QIODevice::WriteOnly)) {
            m.write(QByteArray::number(
                        (qint64)QCoreApplication::applicationPid())
                    + ' '
                    + QByteArray::number(QDateTime::currentMSecsSinceEpoch()));
            m.close();
        }
    }

    // Restore window geometry. v0.1.94 — clamp to currently-connected
    // screens so a session captured on a disconnected monitor doesn't
    // restore the window off-screen.
    if (session.contains("windowW")) {
        int sw = session["windowW"].toInt();
        int sh = session["windowH"].toInt();
        if (sw > 100 && sh > 100) {
            // v0.1.100 — center on the saved SIZE (see centeredWindowRect):
            // restoring frame-coord x/y via setGeometry walked the title bar
            // off the top of the screen on Windows on every launch.
            setGeometry(centeredWindowRect(sw, sh));
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
        const bool hasUnsaved = tab.contains("unsavedContent");
        const QString unsavedContent = tab["unsavedContent"].toString();
        const bool wasModified = tab["modified"].toBool();
        const QString tabName = tab["tabName"].toString();

        Editor *e = nullptr;
        // v0.1.104 — openFile() does NOT always append a tab: it early-returns
        // without adding one when the file is unreadable (perms/replaced-by-dir
        // → loadFile fails) or is already open in an earlier-restored tab. In
        // those cases editorAt(count()-1) would alias the PREVIOUS restored
        // editor; the modified branch then overwrote that prior tab's buffer
        // and a later Save clobbered the wrong file. Capture the count before
        // the call and only reuse the appended editor if the count grew.
        if (!path.isEmpty() && QFileInfo(path).exists() && !wasModified) {
            // Pristine file-backed tab — re-read from disk.
            const int before = m_tabs->count();
            openFile(path);
            if (m_tabs->count() > before)
                e = m_tabs->editorAt(m_tabs->count() - 1);
        } else if (!path.isEmpty() && QFileInfo(path).exists() && wasModified && hasUnsaved) {
            // Modified file-backed tab — open the file first (for path
            // association + watcher + syntax), then overlay the unsaved
            // buffer content and mark as modified.
            const int before = m_tabs->count();
            openFile(path);
            if (m_tabs->count() > before) {
                e = m_tabs->editorAt(m_tabs->count() - 1);
                if (e) {
                    { Editor::ScopedBulkLoad bulk(e); e->setText(unsavedContent); }
                    e->setModified(true);
                }
            } else {
                // openFile() appended nothing (unreadable file, or already
                // open). Do NOT touch any existing tab — preserve the unsaved
                // buffer in a fresh untitled tab instead so a later Save can't
                // clobber the wrong file.
                e = newFile();
                if (e) {
                    { Editor::ScopedBulkLoad bulk(e); e->setText(unsavedContent); }
                    e->setModified(true);
                    if (!tabName.isEmpty()) {
                        int idx = m_tabs->indexOf(e);
                        if (idx >= 0) m_tabs->setTabText(idx, tabName);
                    }
                }
            }
        } else if (hasUnsaved) {
            // Untitled tab or file-no-longer-exists — recreate as new buffer.
            e = newFile();
            if (e) {
                { Editor::ScopedBulkLoad bulk(e); e->setText(unsavedContent); }
                e->setModified(true);
                if (!tabName.isEmpty()) {
                    int idx = m_tabs->indexOf(e);
                    if (idx >= 0) m_tabs->setTabText(idx, tabName);
                }
            }
        } else {
            // Pristine tab whose file vanished since the session was saved —
            // surface it instead of silently dropping the tab.
            if (!path.isEmpty())
                queueStartupNotice(tr("Session tab dropped — file no longer "
                                      "exists: %1")
                                       .arg(QDir::toNativeSeparators(path)));
            continue;
        }

        if (e) {
            e->setCursorPosition(tab["line"].toInt(), tab["col"].toInt());
        }
        // Only honour this entry's "active" flag if we actually materialised a
        // tab for it — otherwise (skipped unreadable pristine tab) leave the
        // active index pointing at a real, correctly-mapped tab.
        if (e && tab["active"].toBool()) {
            int idx = m_tabs->indexOf(e);
            if (idx >= 0) activeIdx = idx;
        }
    }

    if (m_tabs->count() > 1) {
        // Remove the initial empty "new 1" tab if we restored files
        auto *first = m_tabs->editorAt(0);
        if (first && first->filePath().isEmpty() && !first->isModified()
            && first->text().isEmpty()) {
            m_tabs->removeTab(0);
            if (activeIdx > 0) activeIdx--;
        }
    }

    m_tabs->setCurrentIndex(activeIdx);
    // Marker is removed by runStartupNow() after the deferred opens complete.
}

void MainWindow::setStartupActions(const QStringList &files, int gotoLine) {
    m_startupFiles = files;
    m_startupGotoLine = gotoLine;
}

void MainWindow::runStartupNow() {
    if (m_startupDone) return;

    if (!m_standaloneNoSession) {
        // session.json IS the recovery mechanism (full unsaved-buffer
        // content on every autosave tick); restoring it needs no prompt.
        restoreSession();
        // Legacy recovery_*.txt residue from pre-v0.1.96 builds — nothing
        // writes these anymore (autoSaveRecovery is gone). .crash_flag is
        // owned by main(): surfaced on the statusbar post-show, THEN
        // cleared — the old code here wiped it silently before anyone saw it.
        QDir recovDir(recoveryDir());
        for (const QString &rf : recovDir.entryList({"recovery_*"}, QDir::Files))
            QFile::remove(recoveryDir() + "/" + rf);
    }

    // Marker now also covers the deferred CLI opens (kill-retry trap); stage
    // "cliopen" tells the next launch the session itself restored fine.
    // Queued remote opens run from their own flushed slot AFTER the marker is
    // removed — by then session.json is known-good and needs no protection.
    // Standalone windows never touch the marker — it belongs to the primary.
    // Live-owner skip: the marker belongs to ANOTHER live instance mid-restore;
    // neither overwrite nor remove it (the owner clears it itself).
    const QString marker = sessionFilePath() + QStringLiteral(".restoring");
    const bool markerIsOurs = !m_standaloneNoSession && !m_restoreSkippedLiveOwner;
    if (markerIsOurs && !m_startupFiles.isEmpty()) {
        QFile m(marker);
        if (m.open(QIODevice::WriteOnly)) m.write("cliopen");
    }

    const QStringList files = m_startupFiles;
    m_startupFiles.clear();
    for (const QString &path : files) openFile(path);
    if (m_startupGotoLine > 0) {
        // --help promises "the first file"; openFile leaves the LAST opened
        // tab active, so re-resolve the first by path. If the first file
        // failed to open (notice already queued), don't jump an unrelated
        // surviving tab to that line.
        if (!files.isEmpty()) {
            const int idx =
                tabIndexForPath(QFileInfo(files.first()).absoluteFilePath());
            if (idx >= 0) {
                m_tabs->setCurrentIndex(idx);
                if (auto *e = m_tabs->editorAt(idx))
                    e->gotoLine(m_startupGotoLine);
            }
        } else if (auto *e = currentEditor()) {
            e->gotoLine(m_startupGotoLine);
        }
    }

    m_startupDone = true;
    scheduleRemoteOpenFlush();

    // Clears BOTH marker stages (restoreSession's "<pid> <ms>" and the
    // "cliopen" overwrite above). Standalone windows and live-owner skips
    // never touch it — it belongs to the primary / the live owner.
    if (markerIsOurs) QFile::remove(marker);

    if (currentEditor()) updateStatusBar();
    else m_statusBar->updateLanguage(tr("Normal text"));
    updateTitle();
}

// ═══════════════════════════════════════
// File change watcher — detect external edits
// ═══════════════════════════════════════

void MainWindow::queueStartupNotice(const QString &msg) {
    if (msg.isEmpty()) return;
    m_startupNotices.append(msg);
    if (m_everShown && !m_startupNoticeFlushScheduled) {
        m_startupNoticeFlushScheduled = true;
        QTimer::singleShot(0, this, &MainWindow::flushStartupNotices);
    }
}

void MainWindow::flushStartupNotices() {
    m_startupNoticeFlushScheduled = false;
    if (m_startupNotices.isEmpty()) return;
    QStringList items = m_startupNotices;
    m_startupNotices.clear();
    const int extra = items.size() - 10;
    if (extra > 0) { items = items.mid(0, 10); items.append(tr("…and %1 more").arg(extra)); }
    // Title stays generic: besides open failures, main() routes the crash-
    // recovery and standalone-fallback notices through this queue too.
    auto *box = new QMessageBox(QMessageBox::Warning, tr("Notepatra — startup notices"),
                                items.join(QStringLiteral("\n\n")), QMessageBox::Ok, this);
    box->setAttribute(Qt::WA_DeleteOnClose);
    box->setModal(false);
    box->show();   // never exec() — non-blocking by contract
}

void MainWindow::showEvent(QShowEvent *event) {
    QMainWindow::showEvent(event);
    if (m_everShown) return;
    m_everShown = true;
    if (!m_startupNotices.isEmpty() && !m_startupNoticeFlushScheduled) {
        m_startupNoticeFlushScheduled = true;
        QTimer::singleShot(0, this, &MainWindow::flushStartupNotices);
    }
    if (!m_deferredFileChangePaths.isEmpty()) {
        const QStringList deferred = m_deferredFileChangePaths.values();
        m_deferredFileChangePaths.clear();
        for (const QString &p : deferred)
            QTimer::singleShot(0, this, [this, p]() { onWatchedFileChanged(p); });
    }
}

void MainWindow::changeEvent(QEvent *event) {
    QMainWindow::changeEvent(event);
    if (event->type() != QEvent::WindowStateChange) return;
    if (isMinimized()) return;
    drainDeferredFileChanges();
}

void MainWindow::setupFileWatcher() {
    m_fileWatcher = new QFileSystemWatcher(this);
    connect(m_fileWatcher, &QFileSystemWatcher::fileChanged,
            this, &MainWindow::onWatchedFileChanged);
}

void MainWindow::onWatchedFileChanged(const QString &path) {
    // Never-shown or minimized → defer until first show / un-minimize.
    if (!m_everShown || isMinimized()) {
        m_deferredFileChangePaths.insert(path);
        if (QFileInfo::exists(path)) m_fileWatcher->addPath(path);
        return;
    }
    // ANY prompt up (same path or not) → defer; processed after it closes.
    // Without this, a fileChanged for a DIFFERENT path delivered into the
    // open prompt's nested event loop stacked a second modal on top.
    if (m_anyFileChangePromptOpen) {
        m_deferredFileChangePaths.insert(path);
        if (QFileInfo::exists(path)) m_fileWatcher->addPath(path);
        return;
    }
    // Prompt already up for this path → coalesce; the post-dialog re-stat
    // absorbs whatever changed while it was open.
    if (m_fileChangePromptOpen.contains(path)) {
        if (QFileInfo::exists(path)) m_fileWatcher->addPath(path);
        return;
    }
    // Debounce: at most one prompt per path per 1500 ms; re-check later,
    // never drop.
    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    const qint64 closed = m_fileChangePromptClosedMs.value(path, 0);
    if (closed && now - closed < 1500) {
        if (QFileInfo::exists(path)) m_fileWatcher->addPath(path);
        if (!m_fileChangeRecheckPending.contains(path)) {
            m_fileChangeRecheckPending.insert(path);
            QTimer::singleShot(int(1500 - (now - closed)) + 50, this, [this, path]() {
                m_fileChangeRecheckPending.remove(path);
                onWatchedFileChanged(path);
            });
        }
        return;
    }
    for (int i = 0; i < m_tabs->count(); i++) {
        auto *e = m_tabs->editorAt(i);
        if (!e || e->filePath() != path) continue;

        QFileInfo fi(path);
        if (!fi.exists()) {
            // File was deleted
            m_tabs->setCurrentIndex(i);
            m_fileChangePromptOpen.insert(path);
            m_anyFileChangePromptOpen = true;
            auto result = QMessageBox::warning(this, "File Deleted",
                QString("The file \"%1\" has been deleted by another program.\n\n"
                        "Keep this file in editor?")
                .arg(QFileInfo(path).fileName()),
                QMessageBox::Yes | QMessageBox::No);
            m_fileChangePromptOpen.remove(path);
            m_fileChangePromptClosedMs[path] = QDateTime::currentMSecsSinceEpoch();
            if (result == QMessageBox::No) {
                // Re-resolve: the prompt's nested event loop may have
                // reordered or removed tabs — i and e can be stale.
                const int idx = tabIndexForPath(path);
                if (idx >= 0) {
                    auto *cur = m_tabs->editorAt(idx);
                    m_tabs->removeTab(idx);
                    delete cur;
                    if (m_tabs->count() == 0) newFile();
                }
            }
            // Gate stays up through the mutation above — cleared only when
            // this dispatch is fully done, so drained re-dispatches (and any
            // modal an inner call opens) see settled tab state.
            m_anyFileChangePromptOpen = false;
            drainDeferredFileChanges();
            return;
        }

        // File was modified — check if content actually changed
        QDateTime newTime = fi.lastModified();
        if (m_fileTimestamps.contains(path) && newTime == m_fileTimestamps[path])
            return;  // same timestamp, ignore

        m_tabs->setCurrentIndex(i);
        m_fileChangePromptOpen.insert(path);
        m_anyFileChangePromptOpen = true;
        auto result = QMessageBox::question(this, "File Changed",
            QString("The file \"%1\" has been modified by another program.\n\n"
                    "Do you want to reload it?\n\n"
                    "  Yes = Reload from disk (lose your changes)\n"
                    "  No = Keep your version")
            .arg(QFileInfo(path).fileName()),
            QMessageBox::Yes | QMessageBox::No);
        m_fileChangePromptOpen.remove(path);
        m_fileChangePromptClosedMs[path] = QDateTime::currentMSecsSinceEpoch();

        // Re-stat AFTER the dialog: changes made while it was up are
        // absorbed by this one prompt instead of stacking another.
        const QFileInfo post(path);
        // Re-resolve by path — i and e can be stale after the nested loop.
        int idx = tabIndexForPath(path);
        if (result == QMessageBox::Yes && idx >= 0) {
            if (auto *cur = m_tabs->editorAt(idx)) {
                // loadFile can modal (large-file confirm, load-error) — the
                // gate is deliberately still up so nothing stacks on it.
                cur->loadFile(path);
                idx = tabIndexForPath(path);  // its nested loop can shift tabs
                if (idx >= 0) updateTabTitle(idx);
            }
        }
        m_fileTimestamps[path] = post.lastModified();

        // Re-add to watcher (Qt removes it after signal)
        m_fileWatcher->addPath(path);
        // Gate cleared only now — clearing before the reload let deferred
        // changes re-dispatch into loadFile's own modal and stack.
        m_anyFileChangePromptOpen = false;
        drainDeferredFileChanges();
        return;
    }
}

int MainWindow::tabIndexForPath(const QString &path) const {
    for (int i = 0; i < m_tabs->count(); i++) {
        auto *e = m_tabs->editorAt(i);
        if (e && e->filePath() == path) return i;
    }
    return -1;
}

void MainWindow::drainDeferredFileChanges() {
    if (m_deferredFileChangePaths.isEmpty()) return;
    const QStringList deferred = m_deferredFileChangePaths.values();
    m_deferredFileChangePaths.clear();
    for (const QString &p : deferred)
        QTimer::singleShot(0, this, [this, p]() { onWatchedFileChanged(p); });
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
    // Notepad++-style persistence: app close NEVER prompts. All open tabs —
    // including unsaved buffers and untitled tabs — are serialised to
    // session.json so they reappear verbatim on next launch (with their
    // modified flag intact). The Save / Discard / Cancel dialog is reserved
    // for *individual* tab close (the X on a tab), where the user has
    // explicitly chosen to dismiss one buffer. See closeTab() for that path.
    //
    // EXCEPT when this window has no session backing (saveSession() no-ops):
    // --new / hung-primary-fallback standalone windows, and live-owner
    // restore-skip windows (another live instance owns session.json). There
    // the prompt-less contract would silently discard unsaved buffers —
    // route each modified tab through Save / Discard / Cancel instead.
    const bool sessionless = m_standaloneNoSession || m_restoreSkippedLiveOwner;
    if (sessionless) {
        for (int i = m_tabs->count() - 1; i >= 0; i--) {
            auto *e = m_tabs->editorAt(i);
            if (!e || !e->isModified()) continue;
            m_tabs->setCurrentIndex(i);
            QPointer<Editor> alive(e);
            closeTab(i);
            if (alive) {   // Cancel (or a failed save) kept the tab open
                event->ignore();
                return;
            }
        }
    }

    // Save session including unsaved buffers and untitled tabs.
    saveSession();   // no-ops in both session-less modes

    // Session-less windows must not destroy the owner's crash evidence.
    if (!sessionless) {
        // Remove crash flag (clean exit) so next launch knows this was tidy.
        QFile::remove(recoveryDir() + "/.crash_flag");

        // Clean per-tab recovery files — session.json carries unsaved state.
        QDir recovDir(recoveryDir());
        for (const QString &rf : recovDir.entryList({"recovery_*"}, QDir::Files)) {
            QFile::remove(recoveryDir() + "/" + rf);
        }
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
        if (!url.isLocalFile()) continue;
        const QString path = url.toLocalFile();
        // A dropped DIRECTORY opens as the workspace, matching what the Coding
        // Mode refusal has always told users to do ("drag a folder onto the
        // window"). Until v0.1.125 that instruction was false: every drop went
        // to openFile(), which rejects a directory — and the refusal only became
        // reachable in this release, so the advice had never been exercised.
        if (QFileInfo(path).isDir()) {
            if (m_explorer) {
                Config::instance().noteLastDir(path);
                m_explorer->setRoot(path);
            }
            continue;
        }
        openFile(path);
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
        m_recentMenu->addAction(QString("&%1: %2").arg(i + 1).arg(QDir::toNativeSeparators(path)), this, [this, path]() {
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
                    if (!dir.isEmpty() && m_explorer) {
                        m_explorer->setRoot(dir);
                        // Visible feedback — the explorer is gated on Coding
                        // mode, so without this the pick looks like a no-op.
                        const bool codingOn = m_aiDockPanel && m_aiDockPanel->isCodingMode()
                                              && m_aiDockHost && m_aiDockHost->isVisible();
                        if (codingOn) {
                            m_explorer->setVisible(true);
                            if (m_explorerToggleBtn) m_explorerToggleBtn->setChecked(true);
                            statusBar()->showMessage(
                                tr("Workspace: %1").arg(QDir::toNativeSeparators(dir)), 4000);
                        } else {
                            statusBar()->showMessage(
                                tr("Workspace set to %1 — enable Coding mode in the AI dock (Ctrl+Q) to browse it.")
                                    .arg(QDir::toNativeSeparators(dir)), 6000);
                        }
                    }
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
    exitAiFullscreenIfActive();
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
    // Sticky for the session — see aiWorkspaceRoot(). A per-tab root cancelled
    // pending write approvals and swapped the chat history on every tab switch.
    workspace = aiWorkspaceRoot();

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
//
// v0.1.70 — Now a thin wrapper around setAiDockVisible() which is the
// single source of truth for AI dock visibility. Config::aiDockVisible
// is persisted on every toggle so the layout survives quit/relaunch.
void MainWindow::toggleAiDock() {
    setAiDockVisible(!isAiDockVisible());
}

bool MainWindow::isAiDockVisible() const {
    return m_aiDockHost && m_aiDockHost->isVisible();
}

void MainWindow::setAiDockVisible(bool show) {
    if (!m_aiDockHost) return;
    if (show == m_aiDockHost->isVisible()) {
        // Idempotent — ensure Config + button state agree but don't churn
        // the splitter or kick off a re-populate.
        Config::instance().aiDockVisible = show;
        Config::instance().save();
        return;
    }

    if (!show) {
        // Closing — exit any fullscreen state first so siblings (editor,
        // explorer) come back. Without this the splitter stays squashed
        // and the user sees an empty window after closing.
        exitAiFullscreenIfActive();
    }

    m_aiDockHost->setVisible(show);
    Config::instance().aiDockVisible = show;
    Config::instance().save();

    if (show) {
        // v0.1.70 — DO NOT reset to Chat mode. The plan-mode design
        // explicitly chose "restore last sub-mode on re-entry" — the
        // AIPanel widget stayed alive while hidden so m_chatMode /
        // m_codingMode / m_dataMode button states + the per-mode chat
        // history are all intact. Just refresh workspace context.
        populateAiContext(m_aiDockPanel);
        rebalanceAiDockSplit();
    }

    // FileExplorer is gated on Coding mode + dock visible. Closing the
    // dock hides it; opening doesn't show it (user opts in via View menu).
    if (m_explorer && !show) {
        m_explorer->setVisible(false);
    }
}

void MainWindow::showAiDockForInvocation() {
    if (!isAiDockVisible()) {
        setAiDockVisible(true);
    }
}

void MainWindow::rebalanceAiDockSplit() {
    if (!m_splitter) return;
    // v0.1.70 — only force the 60/40 default on the FIRST open per session
    // so the user's mid-session splitter drag survives subsequent opens.
    //
    // v0.1.72 — but ALSO force the split when the dock slot has collapsed
    // to ~0 (after a full hide / show cycle).  Qt's QSplitter doesn't
    // restore a slot's width on setVisible(true) — it stays at 0px and
    // the user sees an empty-looking dock with no AI panel inside.
    // Caught when @user reported "ai toggle space goes out but the actual
    // AI assistant is not showing up at all".  After a hide-then-show the
    // user's last drag intent no longer applies; we must reset to the
    // 60/40 default.
    int aiSlotW = 0;
    if (m_aiDockHost) {
        const QList<int> cur = m_splitter->sizes();
        for (int i = 0; i < m_splitter->count() && i < cur.size(); ++i) {
            if (m_splitter->widget(i) == m_aiDockHost) {
                aiSlotW = cur.at(i);
                break;
            }
        }
    }
    // Threshold of 40 px: anything below that is "collapsed" — too small
    // to be a deliberate user drag (the AI dock has a 320 px minimum width
    // when actually rendered, see line ~840).
    if (m_aiDockSizedOnce && aiSlotW > 40) return;
    //
    // Apply via a lambda we can defer. Calling setSizes() before Qt's
    // layout pass has settled (e.g. right after setVisible(true) on the
    // host) silently no-ops because total width is 0 — so the splitter
    // falls back to its default 50/50 / 33/33/33 share. We invoke the
    // lambda immediately AND via QTimer::singleShot(0) so it runs after
    // the upcoming layout pass too. The second call is idempotent.
    auto apply = [this]() {
        if (!m_splitter) return;
        const QList<int> sizes = m_splitter->sizes();
        const int total = std::accumulate(sizes.begin(), sizes.end(), 0);
        if (total <= 0 || sizes.size() < 2) return;
        const int funcListW = sizes.size() >= 3 ? sizes.value(2, 0) : 0;
        const int usable = total - funcListW;
        // v0.1.70 — 60/40 split, AI dock gets the 60%. Notepatra is
        // AI-first: the dock is the headline surface, editor tabs are the
        // sidebar. Tried 80/20 and 70/30 along the way; settled on 60/40
        // as the most usable for both AI conversations and code visibility.
        const int aiW = (usable * 3) / 5;     // 60% AI dock
        const int tabsW = usable - aiW;       // 40% editor sidebar
        QList<int> next;
        next << tabsW << aiW;
        if (sizes.size() >= 3) next << funcListW;
        m_splitter->setSizes(next);
    };
    apply();
    QTimer::singleShot(0, this, apply);
    m_aiDockSizedOnce = true;  // kept for back-compat
}

// v0.1.67 — bail out of AI dock fullscreen if we're currently inside it,
// so a newly-opened tool tab is actually visible alongside the (now-
// shrunk) AI conversation. Called from every tool-action lambda that
// adds a tab via m_tabs->addTab(...). m_aiSavedSiblingVisibility is the
// canonical "are we in fullscreen?" signal: populated by the on-branch
// of the fullscreenToggled handler (see ~line 864 above) and emptied by
// the off-branch. Non-empty ⇒ we're fullscreened.
//
// We trigger the un-fullscreen path by asking AIPanel itself to flip its
// ⛶ toggle off — that fires the existing fullscreenToggled(false)
// signal, which the handler above already drives correctly (restores
// sibling visibility, restores saved splitter sizes). The AI session
// itself is preserved: AIPanel is not destroyed, only visually resized.
void MainWindow::exitAiFullscreenIfActive() {
    if (m_aiSavedSiblingVisibility.isEmpty()) return;   // not fullscreen
    if (m_aiDockPanel) m_aiDockPanel->forceExitFullscreen();
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
    exitAiFullscreenIfActive();
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
            // Visible feedback — the explorer is gated on Coding mode, so
            // without this the pick looks like a no-op.
            const bool codingOn = m_aiDockPanel && m_aiDockPanel->isCodingMode()
                                  && m_aiDockHost && m_aiDockHost->isVisible();
            if (codingOn) {
                m_explorer->setVisible(true);
                if (m_explorerToggleBtn) m_explorerToggleBtn->setChecked(true);
                statusBar()->showMessage(
                    tr("Workspace: %1").arg(QDir::toNativeSeparators(dir)), 4000);
            } else {
                statusBar()->showMessage(
                    tr("Workspace set to %1 — enable Coding mode in the AI dock (Ctrl+Q) to browse it.")
                        .arg(QDir::toNativeSeparators(dir)), 6000);
            }
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
        {"Noter",         "Noter — Meeting Thinkpad"},
        {"PasswordGenerator", "Password Generator"},
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

    // v0.1.124 — the Show Symbol toggles are Config-backed and global, so they
    // read from Config rather than from the active editor. Two of them
    // (non-printing and control characters) have no getter on QsciScintilla at
    // all, and deriving them from the editor was never possible.
    const auto &cfg = Config::instance();
    sync("viewShowWhitespace",    cfg.showWhitespace);
    sync("viewShowEol",           cfg.showEol);
    sync("viewShowNonPrinting",   cfg.showNonPrintingChars);
    sync("viewShowControlChars",  cfg.showControlChars);
    sync("viewShowOtherInvisible", cfg.showOtherInvisible);
    sync("viewShowIndentGuide",   cfg.showIndentGuides);
    sync("viewShowWrapSymbol",    cfg.showWrapSymbol);

    // Both are set explicitly: sync() blocks signals, so QActionGroup never
    // sees the change and will not un-check the other one for us.
    sync("viewNpcModeAbbreviation", cfg.npcDisplayMode != 1);
    sync("viewNpcModeCodepoint",    cfg.npcDisplayMode == 1);

    // Derived: checked only when all four it drives are on.
    sync("viewShowAllCharacters", cfg.showWhitespace && cfg.showEol
                                  && cfg.showNonPrintingChars
                                  && cfg.showControlChars
                                  && cfg.showOtherInvisible);

    // Word wrap stays per-editor — it is a property of the view, not a
    // global display preference.
    if (e) sync("viewWordWrap", e->wrapMode() != QsciScintilla::WrapNone);
    else   sync("viewWordWrap", cfg.wordWrap);
}
