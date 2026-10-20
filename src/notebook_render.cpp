// SPDX-License-Identifier: GPL-3.0-or-later

#include "notebook_render.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonValue>
#include <QRegularExpression>
#include <QStringList>
#include <QColor>
#include <QTextBlock>
#include <QTextCursor>
#include <QTextDocument>
#include <QTextFragment>

namespace NotebookRender {

QString escapeHtml(const QString &s) {
    return s.toHtmlEscaped();
}

QString markdownToHtml(const QString &md, const Colors *colors) {
#if QT_VERSION >= QT_VERSION_CHECK(5, 14, 0)
    QTextDocument doc;
    doc.setMarkdown(md, QTextDocument::MarkdownDialectGitHub);

    // toHtml() drops the importer's fixed-pitch hint, so code would export as
    // body text; collect code/link ranges first, then restyle them explicitly.
    struct Range { int pos; int len; bool link; };
    QVector<Range> spans;
    QVector<QTextBlock> codeBlocks;
    for (QTextBlock b = doc.begin(); b.isValid(); b = b.next()) {
        const QTextBlockFormat bf = b.blockFormat();
        bool allFixed = !b.text().isEmpty();
        QVector<Range> blockSpans;
        for (auto it = b.begin(); !it.atEnd(); ++it) {
            const QTextFragment f = it.fragment();
            const QTextCharFormat cf = f.charFormat();
            if (cf.isAnchor()) blockSpans.append({f.position(), f.length(), true});
            if (cf.fontFixedPitch()) blockSpans.append({f.position(), f.length(), false});
            else allFixed = false;
        }
        if (bf.hasProperty(QTextFormat::BlockCodeFence) ||
            bf.hasProperty(QTextFormat::BlockCodeLanguage) || allFixed)
            codeBlocks.append(b);
        else
            spans += blockSpans;
    }
    QTextCursor cur(&doc);
    QTextCharFormat mono;
    mono.setFontFamily(QStringLiteral("monospace"));
    mono.setFontFixedPitch(true);
    for (const QTextBlock &b : qAsConst(codeBlocks)) {
        QTextBlockFormat bf;
        bf.setNonBreakableLines(true);
        if (colors) bf.setBackground(QColor(colors->codeBg));
        cur.setPosition(b.position());
        cur.mergeBlockFormat(bf);
        cur.setPosition(b.position() + b.length() - 1, QTextCursor::KeepAnchor);
        cur.mergeCharFormat(mono);
    }
    for (const Range &r : qAsConst(spans)) {
        QTextCharFormat cf;
        if (r.link) {
            if (!colors) continue;
            cf.setForeground(QColor(colors->linkFg));
        } else {
            cf = mono;
            if (colors) cf.setBackground(QColor(colors->codeBg));
        }
        cur.setPosition(r.pos);
        cur.setPosition(r.pos + r.len, QTextCursor::KeepAnchor);
        cur.mergeCharFormat(cf);
    }
    return doc.toHtml();
#else
    Q_UNUSED(colors);
    return QStringLiteral("<pre>") + escapeHtml(md) + QStringLiteral("</pre>");
#endif
}

QString htmlBodyContent(const QString &html) {
    const int bodyOpen = html.indexOf(QLatin1String("<body"), 0, Qt::CaseInsensitive);
    if (bodyOpen < 0) return html;
    const int contentStart = html.indexOf(QLatin1Char('>'), bodyOpen);
    if (contentStart < 0) return html;
    int bodyClose = html.lastIndexOf(QLatin1String("</body>"), -1, Qt::CaseInsensitive);
    if (bodyClose < contentStart) bodyClose = html.size();
    return html.mid(contentStart + 1, bodyClose - contentStart - 1);
}

QString stripAnsi(const QString &s) {
    QString out;
    out.reserve(s.size());
    const int n = s.size();
    for (int i = 0; i < n; ++i) {
        const QChar ch = s.at(i);
        if (ch.unicode() != 0x1b) {
            out.append(ch);
            continue;
        }
        if (i + 1 >= n) break;
        const QChar kind = s.at(i + 1);
        if (kind == QLatin1Char('[')) {
            // CSI: parameters/intermediates, then one final byte in 0x40..0x7E.
            int j = i + 2;
            while (j < n && !(s.at(j).unicode() >= 0x40 && s.at(j).unicode() <= 0x7e)) ++j;
            i = j;
        } else if (kind == QLatin1Char(']')) {
            // OSC: terminated by BEL or ESC backslash.
            int j = i + 2;
            while (j < n) {
                if (s.at(j).unicode() == 0x07) break;
                if (s.at(j).unicode() == 0x1b && j + 1 < n && s.at(j + 1) == QLatin1Char('\\')) {
                    ++j;
                    break;
                }
                ++j;
            }
            i = j;
        } else {
            i += 1;
        }
    }
    return out;
}

namespace {

QString joinedText(const QJsonValue &v) {
    if (v.isString()) return v.toString();
    if (v.isArray()) {
        QString out;
        for (const QJsonValue &part : v.toArray()) out += part.toString();
        return out;
    }
    return QString();
}

QString preBlock(const QString &text, const QString &bg, const QString &fg) {
    return QStringLiteral("<table width=\"100%\" cellspacing=\"0\" cellpadding=\"6\" "
                          "style=\"background-color:%1; border-collapse:collapse;\">"
                          "<tr><td><pre style=\"color:%2; margin:0;\">%3</pre></td></tr></table>")
        .arg(bg, fg, escapeHtml(text));
}

QString label(const QString &text, const QString &color) {
    return QStringLiteral("<p style=\"color:%1; font-size:small; margin-top:10px; margin-bottom:2px;\">%2</p>")
        .arg(color, escapeHtml(text));
}

QString sanitizeOutputHtml(QString html) {
    static const QRegularExpression scriptRe(QStringLiteral("<script\\b[^>]*>.*?</script\\s*>"),
                                             QRegularExpression::CaseInsensitiveOption |
                                                 QRegularExpression::DotMatchesEverythingOption);
    html.remove(scriptRe);
    return html;
}

} // namespace

QString ipynbToHtml(const QByteArray &json, const Colors &c,
                    QVector<NotebookImage> *images, QString *err) {
    if (err) err->clear();
    QJsonParseError pe;
    const QJsonDocument doc = QJsonDocument::fromJson(json, &pe);
    if (pe.error != QJsonParseError::NoError || !doc.isObject()) {
        if (err) *err = pe.error != QJsonParseError::NoError
                            ? QStringLiteral("invalid notebook JSON: %1").arg(pe.errorString())
                            : QStringLiteral("invalid notebook JSON: top level is not an object");
        return QString();
    }
    const QJsonObject root = doc.object();
    const int nbformat = root.value(QStringLiteral("nbformat")).toInt(0);
    if (nbformat < 4) {
        if (err) *err = QStringLiteral("unsupported nbformat %1 (only nbformat 4 is supported)").arg(nbformat);
        return QString();
    }
    const QJsonObject meta = root.value(QStringLiteral("metadata")).toObject();
    QString language = meta.value(QStringLiteral("language_info")).toObject()
                           .value(QStringLiteral("name")).toString();
    if (language.isEmpty())
        language = meta.value(QStringLiteral("kernelspec")).toObject()
                       .value(QStringLiteral("language")).toString();

    QString body;
    int imageIndex = images ? images->size() : 0;
    const QJsonArray cells = root.value(QStringLiteral("cells")).toArray();
    for (const QJsonValue &cv : cells) {
        const QJsonObject cell = cv.toObject();
        const QString type = cell.value(QStringLiteral("cell_type")).toString();
        const QString src = joinedText(cell.value(QStringLiteral("source")));

        if (type == QLatin1String("markdown")) {
            body += QStringLiteral("<div>") + htmlBodyContent(markdownToHtml(src, &c)) +
                    QStringLiteral("</div>");
            continue;
        }
        if (type != QLatin1String("code")) {
            body += label(type.isEmpty() ? QStringLiteral("raw") : type, c.dimFg);
            body += preBlock(src, c.codeBg, c.fg);
            continue;
        }

        const QJsonValue ec = cell.value(QStringLiteral("execution_count"));
        const QString count = ec.isDouble() ? QString::number(ec.toInt()) : QStringLiteral(" ");
        QString head = QStringLiteral("In [%1]:").arg(count);
        if (!language.isEmpty()) head += QStringLiteral("  ") + language;
        body += label(head, c.accent);
        body += preBlock(src, c.codeBg, c.fg);

        for (const QJsonValue &ov : cell.value(QStringLiteral("outputs")).toArray()) {
            const QJsonObject out = ov.toObject();
            const QString otype = out.value(QStringLiteral("output_type")).toString();
            if (otype == QLatin1String("stream")) {
                const bool isErr = out.value(QStringLiteral("name")).toString() == QLatin1String("stderr");
                body += QStringLiteral("<pre style=\"color:%1;\">%2</pre>")
                            .arg(isErr ? QStringLiteral("#d9534f") : c.fg,
                                 escapeHtml(joinedText(out.value(QStringLiteral("text")))));
            } else if (otype == QLatin1String("execute_result") ||
                       otype == QLatin1String("display_data")) {
                if (otype == QLatin1String("execute_result")) {
                    const QJsonValue oc = out.value(QStringLiteral("execution_count"));
                    body += label(QStringLiteral("Out [%1]:")
                                      .arg(oc.isDouble() ? QString::number(oc.toInt()) : count),
                                  c.dimFg);
                }
                const QJsonObject data = out.value(QStringLiteral("data")).toObject();
                const QString png = joinedText(data.value(QStringLiteral("image/png")));
                const QString html = joinedText(data.value(QStringLiteral("text/html")));
                const QString plain = joinedText(data.value(QStringLiteral("text/plain")));
                if (!png.isEmpty()) {
                    const QByteArray bytes = QByteArray::fromBase64(png.toLatin1());
                    if (!bytes.isEmpty()) {
                        const QString name = QStringLiteral("nb://img/%1").arg(imageIndex++);
                        if (images) images->append({name, bytes});
                        body += QStringLiteral("<p><img src=\"%1\"></p>").arg(name);
                    }
                } else if (!html.isEmpty()) {
                    body += QStringLiteral("<div>") + sanitizeOutputHtml(html) + QStringLiteral("</div>");
                } else if (!plain.isEmpty()) {
                    body += QStringLiteral("<pre style=\"color:%1;\">%2</pre>").arg(c.fg, escapeHtml(plain));
                }
            } else if (otype == QLatin1String("error")) {
                QStringList tb;
                for (const QJsonValue &line : out.value(QStringLiteral("traceback")).toArray())
                    tb << line.toString();
                QString text = tb.join(QLatin1Char('\n'));
                if (text.isEmpty())
                    text = out.value(QStringLiteral("ename")).toString() + QStringLiteral(": ") +
                           out.value(QStringLiteral("evalue")).toString();
                body += QStringLiteral("<pre style=\"color:#d9534f;\">%1</pre>")
                            .arg(escapeHtml(stripAnsi(text)));
            }
        }
    }
    if (cells.isEmpty()) body = label(QStringLiteral("(empty notebook)"), c.dimFg);

    return QStringLiteral("<html><body style=\"color:%1;\">").arg(c.fg) + body +
           QStringLiteral("</body></html>");
}

namespace {

// "-- %% ..." separator: returns true and the text after "%%" in *rest.
bool parseSeparator(const QString &line, QString *rest) {
    const QString t = line.trimmed();
    if (!t.startsWith(QLatin1String("--"))) return false;
    int i = 2;
    while (i < t.size() && t.at(i).isSpace()) ++i;
    if (!t.midRef(i).startsWith(QLatin1String("%%"))) return false;
    *rest = t.mid(i + 2).trimmed();
    return true;
}

bool isMarkdownMarker(const QString &rest) {
    const QString r = rest.toLower();
    return r.contains(QLatin1String("[markdown]")) || r.contains(QLatin1String("[md]")) ||
           r == QLatin1String("md") || r.startsWith(QLatin1String("md ")) ||
           r == QLatin1String("markdown");
}

QString uncommentMarkdown(const QStringList &lines) {
    QStringList out;
    out.reserve(lines.size());
    for (const QString &line : lines) {
        int lead = 0;
        while (lead < line.size() && line.at(lead).isSpace()) ++lead;
        if (line.midRef(lead).startsWith(QLatin1String("--"))) {
            QString rest = line.mid(lead + 2);
            if (rest.startsWith(QLatin1Char(' '))) rest.remove(0, 1);
            out << rest;
        } else {
            out << line;
        }
    }
    return out.join(QLatin1Char('\n'));
}

} // namespace

QVector<SqlCell> splitSqlCells(const QString &sql) {
    QVector<SqlCell> cells;
    QStringList lines = sql.split(QLatin1Char('\n'));
    for (QString &l : lines)
        if (l.endsWith(QLatin1Char('\r'))) l.chop(1);

    int start = 0;
    bool markdown = false;
    QStringList body;
    auto flush = [&](int endLine) {
        const QString text = (markdown ? uncommentMarkdown(body) : body.join(QLatin1Char('\n'))).trimmed();
        if (!text.isEmpty()) {
            SqlCell c;
            c.startLine = start;
            c.endLine = endLine;
            c.text = text;
            c.markdown = markdown;
            cells.append(c);
        }
        body.clear();
    };
    for (int i = 0; i < lines.size(); ++i) {
        QString rest;
        if (parseSeparator(lines.at(i), &rest)) {
            flush(i - 1);
            start = i;
            markdown = isMarkdownMarker(rest);
            continue;
        }
        body << lines.at(i);
    }
    flush(lines.size() - 1);
    return cells;
}

int cellIndexForLine(const QVector<SqlCell> &cells, int line) {
    for (int i = 0; i < cells.size(); ++i)
        if (line >= cells.at(i).startLine && line <= cells.at(i).endLine) return i;
    return -1;
}

} // namespace NotebookRender
