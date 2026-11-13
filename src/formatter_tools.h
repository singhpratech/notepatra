// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef FORMATTER_TOOLS_H
#define FORMATTER_TOOLS_H

// The JSON Tools / Bracket Tools actions behind Tools > Utilities, shared by
// MainWindow (which adds the AI Fix row, Show Diff wiring and the tab) and
// test_json_bracket_tools, so the test drives the same buttons users click.
//
// Every action returns either the new text or the untouched input, and says
// what happened through TransformNote (transform_note.h): the panel banner
// shows one line, its tooltip the full report with line and column numbers.
// Reports never go into the text itself.

#include <QString>

class FormatterPanel;

namespace FormatterTools {

// JSON Tools: Format, Minify, Fix + Format.
void addJsonButtons(FormatterPanel *panel);
// Bracket Tools: Check (report only, text untouched), Auto-Fix.
void addBracketButtons(FormatterPanel *panel);
// HTML Tools: Format (2 spaces), Format (4 spaces), Minify, Check + Format.
void addHtmlButtons(FormatterPanel *panel);

// HTML through the DOM-preserving Rust formatter. A decline returns the
// input unchanged with an Error note naming what and where (line, column).
QString htmlFormat(const QString &text, int indent);
QString htmlMinify(const QString &text);
// Check + Format: the same formatter, plus a success note saying the check
// found nothing it could not follow (it never adds or repairs tags).
QString htmlCheckAndFormat(const QString &text);

// Fix + Format: conservative repair, then whitespace-only formatting. The
// result is only JSON (no report header); a declined repair returns the
// input unchanged with the reason as an Error note.
QString jsonFixAndFormat(const QString &text);
// Check: posts the bracket report (issue count, each issue with line and
// column) as a keep-text note and returns the input unchanged.
QString bracketCheck(const QString &text);

// AI Fix (JSON Tools): the model's reply, already stripped of fences/prose.
// ok == true: `text` is the reply formatted. ok == false: `error` is the
// strict parser's "line L, column C: ..." and `text` the reply as is.
struct AiJsonReply {
    bool ok = false;
    QString text;
    QString error;
};
AiJsonReply checkAiJsonReply(const QString &reply);
// AI Fix (HTML Tools): ok == true: `text` is the reply re-indented. ok ==
// false: the formatter declined the reply (mis-nested / stray tags, ...);
// `text` is the reply as is and `error` says what and where.
AiJsonReply checkAiHtmlReply(const QString &reply);

} // namespace FormatterTools

#endif // FORMATTER_TOOLS_H
