// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef NOTEPATRA_NOTES_SWEEP_PROMPT_H
#define NOTEPATRA_NOTES_SWEEP_PROMPT_H

// Noter "end-meeting AI sweep" prompt builder + reply parser.
//
// Pure functions, no Qt-widget construction here, so the contract can
// be unit-tested without a QApplication. The sweep dialog feeds the
// returned prompt to the existing OllamaClient API (see ollama.h)
// and routes the reply through parse() before rendering the dialog.
//
// Wire format the LLM is asked to emit:
//   { decisions:[{text}],
//     actions:[{text, owner, due}],
//     questions:[{text}],
//     risks:[{text}] }
//
// parse() is intentionally permissive — it tolerates markdown fences,
// trailing commas, missing keys, partial fragments. The dialog should
// always render *something* even when the model misbehaves.

#include <QDateTime>
#include <QString>
#include <QVector>

namespace NoterSweepPrompt {

// Build the system+user prompt sent to the local LLM. Strips HTML to
// a clean text representation before stitching the user turn so small
// 3B/7B models don't have to wade through Noter's block markup.
//
// `meetingTitle` is woven into the user-turn header so the model has
// the meeting name as a hint when it picks owners / decisions.
QString build(const QString &meetingHtml, const QString &meetingTitle);

// Parsed structured form of the LLM's JSON reply.
struct SweepResult {
    struct Item {
        // Optional glyph the dialog can render in front of the row
        // (defaulted by parse() per section: "*" for decisions,
        // "[]" for actions, "?" for questions, "!" for risks).
        QString glyph;
        QString text;
        // Action-only fields. owner format: "@name" if mentioned,
        // otherwise empty. dueAt invalid() means "no due time given".
        QString owner;
        QDateTime dueAt;
    };
    QVector<Item> decisions;
    QVector<Item> actions;
    QVector<Item> questions;
    QVector<Item> risks;
    // Always populated — the raw model reply (post-fence-strip). Useful
    // for debugging / "Show raw response" in the dialog.
    QString rawResponse;
    // Empty on success. Non-empty when parse() couldn't make sense of
    // any of the four sections (the dialog still renders, just empty).
    QString errorMessage;
};

SweepResult parse(const QString &llmReplyJson);

// Reduce a Noter HTML body to a plain-text representation suitable for
// stuffing into the prompt. Drops every tag, collapses runs of
// whitespace, replaces decision/action/question/risk class markers
// with short ASCII tags so the model sees structure without HTML.
QString htmlToPlainContext(const QString &html);

}  // namespace NoterSweepPrompt

#endif
