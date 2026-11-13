// SPDX-License-Identifier: GPL-3.0-or-later

#include "formatter_tools.h"

#include "fmtpanel.h"
#include "rustbridge.h"
#include "transform_note.h"

namespace FormatterTools {

QString jsonFixAndFormat(const QString &text) {
    // fixJson posts the outcome (what it repaired and where, or why it
    // declined); formatJson of a declined (still invalid) text posts nothing
    // new because the first note wins, and returns it unchanged.
    return RustCore::formatJson(RustCore::fixJson(text), 4);
}

QString bracketCheck(const QString &text) {
    const QString report = RustCore::checkBrackets(text).trimmed();
    const bool clean = report.startsWith(QLatin1String("All brackets matched"));
    TransformNote::postReport(clean ? TransformNote::Kind::Info : TransformNote::Kind::Error,
                              TransformNote::summarize(report), clean ? QString() : report);
    return text;
}

// "HTML formatter left the text unchanged. line 2, column 10: ..." -> the
// part after the prefix (the panel banner already says "Not changed").
static QString htmlReason(const QString &message) {
    QString m = message.trimmed();
    const QLatin1String prefix("HTML formatter left the text unchanged");
    if (m.startsWith(prefix)) {
        m = m.mid(prefix.size());
        while (!m.isEmpty() && (m[0] == QLatin1Char('.') || m[0] == QLatin1Char(':') ||
                                m[0].isSpace()))
            m.remove(0, 1);
    }
    return m.isEmpty() ? message.trimmed() : m;
}

static QString htmlRun(const QString &text, int indent, bool minify) {
    QString message;
    const QString out = minify ? RustCore::minifyHtml(text, &message)
                               : RustCore::formatHtml(text, indent, &message);
    if (!message.isEmpty()) {
        TransformNote::post(TransformNote::Kind::Error, htmlReason(message), message.trimmed());
        return text;
    }
    return out;
}

QString htmlFormat(const QString &text, int indent) { return htmlRun(text, indent, false); }

QString htmlMinify(const QString &text) { return htmlRun(text, 0, true); }

QString htmlCheckAndFormat(const QString &text) {
    const QString out = htmlRun(text, 2, false);
    TransformNote::post(TransformNote::Kind::Info,
                        QStringLiteral("no mis-nested or stray tags found; re-indented with 2 "
                                       "spaces (whitespace only)"));
    return out;
}

void addHtmlButtons(FormatterPanel *panel) {
    panel->addButton("Format (2 spaces)", [](const QString &s) { return htmlFormat(s, 2); });
    panel->addButton("Format (4 spaces)", [](const QString &s) { return htmlFormat(s, 4); });
    panel->addButton("Minify", &htmlMinify);
    panel->addButton("Check + Format", &htmlCheckAndFormat);
}

AiJsonReply checkAiHtmlReply(const QString &reply) {
    AiJsonReply r;
    QString message;
    const QString out = RustCore::formatHtml(reply, 2, &message);
    r.ok = message.isEmpty();
    r.error = r.ok ? QString() : htmlReason(message);
    r.text = r.ok ? out : reply;
    return r;
}

void addJsonButtons(FormatterPanel *panel) {
    panel->addButton("Format", [](const QString &s) { return RustCore::formatJson(s, 4); });
    panel->addButton("Minify", [](const QString &s) { return RustCore::minifyJson(s); });
    panel->addButton("Fix + Format", &jsonFixAndFormat);
}

void addBracketButtons(FormatterPanel *panel) {
    panel->addButton("Check", &bracketCheck);
    panel->addButton("Auto-Fix", [](const QString &s) { return RustCore::fixBrackets(s); });
}

AiJsonReply checkAiJsonReply(const QString &reply) {
    AiJsonReply r;
    r.error = RustCore::jsonParseError(reply);
    r.ok = r.error.isEmpty();
    r.text = r.ok ? RustCore::formatJson(reply, 4) : reply;
    return r;
}

} // namespace FormatterTools
