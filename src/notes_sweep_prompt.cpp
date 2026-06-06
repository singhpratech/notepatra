// SPDX-License-Identifier: GPL-3.0-or-later

#include "notes_sweep_prompt.h"

#include <QDateTime>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonValue>
#include <QRegularExpression>
#include <QStringList>
#include <QTimeZone>

namespace NoterSweepPrompt {

// ═══════════════════════════════════════════════════════════════════════
// htmlToPlainContext — strip Noter's block markup to a flat text view.
// ═══════════════════════════════════════════════════════════════════════
//
// We do NOT use QTextDocument here on purpose:
//   1. It pulls QtGui and would force the headless unit test to spin
//      up a QGuiApplication.
//   2. We want to surface the block CLASS (b-dec / b-act / b-q / b-risk)
//      as an ASCII tag so the model sees "the user already wrote three
//      [DECISION] blocks" — knowing the user's grain helps it pick
//      reasonable thresholds for "what counts as a decision".
//
// Strategy: regex-based. For each block class, replace the opening
// <div class="b b-X" ...> with a tag literal, drop all other tags,
// decode HTML entities, collapse whitespace.

QString htmlToPlainContext(const QString &html) {
    if (html.isEmpty()) return QString();
    QString out = html;

    // ── Owner / due hints survive as inline tags ──
    // Re-emit data-owner / data-due on action blocks as
    // "[ACTION] (owner=@name due=ISO) " BEFORE the generic class tag
    // strip below — otherwise the generic pass swallows the opening
    // <div> and we lose the attributes. Attribute order in the source
    // HTML is variable so we tolerate both orderings.
    {
        QRegularExpression actA(
            "<div[^>]*class=\"[^\"]*\\bb-act\\b[^\"]*\"[^>]*"
            "data-owner=\"([^\"]*)\"[^>]*data-due=\"([^\"]*)\"[^>]*>",
            QRegularExpression::CaseInsensitiveOption);
        out.replace(actA, "\n[ACTION] (owner=\\1 due=\\2) ");
        QRegularExpression actB(
            "<div[^>]*class=\"[^\"]*\\bb-act\\b[^\"]*\"[^>]*"
            "data-due=\"([^\"]*)\"[^>]*data-owner=\"([^\"]*)\"[^>]*>",
            QRegularExpression::CaseInsensitiveOption);
        out.replace(actB, "\n[ACTION] (owner=\\2 due=\\1) ");
    }

    // ── Tag every typed Noter block with an inline ASCII marker ──
    // The class list is matched as a substring, so "class=\"b b-dec
    // foo\"" still hits. Both the order and the marker tokens match
    // what build() asks the model to emit in its JSON reply.
    // Plain b-act blocks without data attributes fall through to this
    // pass and get just "[ACTION] " — the owner+due variant above
    // already handled the rich case.
    struct ClassMark { const char *cls; const char *marker; };
    static const ClassMark kMarks[] = {
        { "b-dec",   "\n[DECISION] " },
        { "b-act",   "\n[ACTION] " },
        { "b-q",     "\n[QUESTION] " },
        { "b-risk",  "\n[RISK] " },
        { "b-quote", "\n[QUOTE] " },
        { "emb-pr",  "\n[PR] " },
        { "emb-vid", "\n[VIDEO] " },
        { "emb-img", "\n[IMG] " },
        { "emb",     "\n[REF] " },
    };
    for (const auto &m : kMarks) {
        QRegularExpression re(
            QString("<div[^>]*class=\"[^\"]*\\b%1\\b[^\"]*\"[^>]*>")
                .arg(QString::fromLatin1(m.cls)),
            QRegularExpression::CaseInsensitiveOption);
        out.replace(re, QString::fromLatin1(m.marker));
    }

    // ── Convert <br> / </p> / </h*> to newlines so paragraph breaks
    //    survive the tag-strip ──
    out.replace(QRegularExpression("<br\\s*/?>", QRegularExpression::CaseInsensitiveOption), "\n");
    out.replace(QRegularExpression("</p\\s*>",  QRegularExpression::CaseInsensitiveOption), "\n");
    out.replace(QRegularExpression("</div\\s*>",QRegularExpression::CaseInsensitiveOption), "\n");
    out.replace(QRegularExpression("</li\\s*>", QRegularExpression::CaseInsensitiveOption), "\n");
    out.replace(QRegularExpression("</h[1-6]\\s*>", QRegularExpression::CaseInsensitiveOption), "\n");

    // ── Drop every remaining tag ──
    out.replace(QRegularExpression("<[^>]+>"), "");

    // ── Decode the handful of entities Noter actually emits ──
    out.replace("&amp;",  "&");
    out.replace("&lt;",   "<");
    out.replace("&gt;",   ">");
    out.replace("&quot;", "\"");
    out.replace("&#39;",  "'");
    out.replace("&nbsp;", " ");

    // ── Collapse runs of horizontal whitespace; trim each line ──
    out.replace(QRegularExpression("[ \\t]+"), " ");
    QStringList lines = out.split('\n', Qt::SkipEmptyParts);
    for (QString &ln : lines) ln = ln.trimmed();
    lines.removeAll(QString());
    return lines.join('\n');
}

// ═══════════════════════════════════════════════════════════════════════
// build — the actual prompt the model sees.
// ═══════════════════════════════════════════════════════════════════════
//
// We put the strict JSON instruction in the SYSTEM turn and the meeting
// body in the USER turn. The system prompt is intentionally short — a
// long preamble starves the small-context Ollama defaults (3B / 7B
// models with 4K windows are still common on Linux dev boxes).

QString build(const QString &meetingHtml, const QString &meetingTitle) {
    // LOCAL wall-clock anchors. The model is asked to emit `due` as plain local
    // time (no timezone suffix) so "10am tomorrow" round-trips to the picker as
    // 10:00 — parse() keeps it local and storage converts to UTC. A concrete
    // today/tomorrow pair removes the model's guesswork on relative dates.
    const QDateTime nowLocal = QDateTime::currentDateTime();
    const QString nowIso   = nowLocal.toString(QStringLiteral("yyyy-MM-ddTHH:mm"));
    const QString todayStr = nowLocal.date().toString(QStringLiteral("yyyy-MM-dd"));
    const QString tomwStr  = nowLocal.date().addDays(1).toString(QStringLiteral("yyyy-MM-dd"));
    const QString dowToday = nowLocal.date().toString(QStringLiteral("dddd"));

    const QString plain = htmlToPlainContext(meetingHtml);

    QString sys;
    sys += "You are a meeting-note structure extractor.\n";
    sys += "Output VALID JSON only — no markdown fences, no prose, no <think> blocks.\n";
    sys += "Schema:\n";
    sys += "{\n";
    sys += "  \"summary\":   \"1-3 sentence plain-English summary of the note\",\n";
    sys += "  \"decisions\": [{\"text\": \"...\"}],\n";
    sys += "  \"actions\":   [{\"text\": \"...\", \"owner\": \"@name\" | null, \"due\": \"YYYY-MM-DDTHH:MM\" | null}],\n";
    sys += "  \"questions\": [{\"text\": \"...\"}],\n";
    sys += "  \"risks\":     [{\"text\": \"...\"}]\n";
    sys += "}\n";
    sys += "Rules:\n";
    sys += "1. DATES: NOW is " + nowIso + " (" + dowToday + "). Today=" + todayStr +
           ", tomorrow=" + tomwStr + ". Resolve every natural time phrase "
           "('10am tomorrow', 'Fri 5pm', 'next Wed', 'EOD', 'in 2 hours') to a "
           "concrete LOCAL timestamp \"YYYY-MM-DDTHH:MM\" — 24-hour, NO timezone "
           "suffix, NO 'Z'. Example: if today is " + todayStr + ", then "
           "'ship the build 10am tomorrow' → due \"" + tomwStr + "T10:00\".\n";
    sys += "2. Owner format: '@name' if a name or handle is mentioned, else null.\n";
    sys += "3. Be conservative — only emit a decision/action/question/risk if the "
           "note clearly indicates one. Empty arrays are fine.\n";
    sys += "4. Never invent owners, dates, or text not present in the note. If no "
           "time is stated for an action, set due to null (do NOT guess a time).\n";
    sys += "5. Each text field is one short sentence (<= 140 chars).\n";
    sys += "6. Action items are the PRIORITY: capture every concrete task, "
           "commitment, assignment, or 'I will / we should / let's / need to' "
           "as an action, with owner + due whenever stated. ALWAYS attach a due "
           "when the note mentions any time for that task. Decisions, questions "
           "and risks are secondary — include them only when clearly present.\n";
    sys += "7. ALWAYS fill \"summary\": a short, neutral recap of what the note is "
           "about and its key outcomes — your understanding of it.\n";

    QString user;
    user += "MEETING: " + (meetingTitle.isEmpty()
                              ? QStringLiteral("(untitled)")
                              : meetingTitle) + "\n";
    user += "NOTE BODY (plain text, with bracketed tags showing existing block types):\n";
    user += "---\n";
    user += plain + "\n";
    user += "---\n";
    user += "Extract. Output JSON only.";

    // Combined system+user prompt — the caller passes the SYSTEM half
    // to OllamaClient::generate(prompt, systemPrompt) and the USER half
    // as the prompt arg. We hand back both glued with a sentinel the
    // caller splits on, so this function stays a single-return API and
    // doesn't leak internal structure into the public header.
    return sys + "\n\x1f\n" + user;  // U+001F is the splitter
}

// ═══════════════════════════════════════════════════════════════════════
// splitPrompt — consume the U+001F sentinel build() glued in.
// ═══════════════════════════════════════════════════════════════════════
//
// v0.1.112 — the Extract flow used to pass build()'s combined string
// straight into generate() as the prompt, so the model saw the raw
// control byte AND the system instructions landed in the user turn.
// This is the single place the sentinel is interpreted; both halves are
// scrubbed of any stray U+001F so it can never reach the request builder.

PromptParts splitPrompt(const QString &combined) {
    PromptParts p;
    const QChar sentinel(0x1F);
    const int idx = combined.indexOf(sentinel);
    if (idx < 0) {
        // No sentinel — treat the whole thing as the user turn so no
        // prompt text is silently dropped.
        p.user = combined;
        return p;
    }
    p.system = combined.left(idx);
    p.user   = combined.mid(idx + 1);
    // Defensive: scrub any further sentinels (never expected — note text
    // is HTML-escaped — but a control byte must not hit the wire) and
    // drop the "\n…\n" glue around the splitter.
    p.system.remove(sentinel);
    p.user.remove(sentinel);
    p.system = p.system.trimmed();
    p.user   = p.user.trimmed();
    return p;
}

// ═══════════════════════════════════════════════════════════════════════
// parse — best-effort tolerant JSON parser for the model reply.
// ═══════════════════════════════════════════════════════════════════════

namespace {

// Trim leading ```json / ``` and trailing ``` fences, plus any stray
// preamble like "Here is the JSON:" the model might emit despite the
// system prompt.
QString stripFencesAndPreamble(const QString &raw) {
    QString s = raw.trimmed();

    // <think> ... </think> from Qwen3-style models — drop entirely.
    static const QRegularExpression thinkRe(
        "<think>[\\s\\S]*?</think>",
        QRegularExpression::CaseInsensitiveOption);
    s.remove(thinkRe);
    s = s.trimmed();

    // ```json … ``` or ``` … ```
    static const QRegularExpression fenceRe(
        "^```(?:json)?\\s*\\n?", QRegularExpression::CaseInsensitiveOption);
    if (fenceRe.match(s).hasMatch()) {
        s.remove(fenceRe);
        if (s.endsWith("```")) s.chop(3);
        s = s.trimmed();
    }

    // Find the first '{' and last '}' — anything outside is preamble /
    // postamble we don't care about. This salvages replies that look
    // like "Sure, here you go: { ... }".
    const int firstBrace = s.indexOf('{');
    const int lastBrace  = s.lastIndexOf('}');
    if (firstBrace >= 0 && lastBrace > firstBrace) {
        s = s.mid(firstBrace, lastBrace - firstBrace + 1);
    }
    return s;
}

// Repair trailing commas before } and ]. Qt5 QJsonDocument rejects them
// even though most models love emitting them.
QString repairTrailingCommas(const QString &json) {
    QString out = json;
    static const QRegularExpression trail(",\\s*([}\\]])");
    out.replace(trail, "\\1");
    return out;
}

QDateTime parseLooseIso(const QString &s) {
    if (s.trimmed().isEmpty()) return QDateTime();
    // Accept "2026-05-21T17:00" / "2026-05-21 17:00:00" / "2026-05-21T17:00:00Z"
    // / "2026-05-21T17:00:00+05:30".
    //
    // v0.1.98 — the build() prompt asks for LOCAL wall-clock with NO suffix, so
    // a no-offset string MUST stay local (the old code force-set UTC, which
    // shifted "10am tomorrow" by the user's offset — it showed/fired at the
    // wrong hour). Strings that DO carry Z / an offset parse to a real instant;
    // normalise those to local so the picker and storage agree on wall-clock.
    QString v = s.trimmed();
    v.replace(' ', 'T');
    QDateTime dt = QDateTime::fromString(v, Qt::ISODate);
    if (dt.isValid())
        return dt.timeSpec() == Qt::LocalTime ? dt : dt.toLocalTime();
    // Final fallback — date-only (local midnight).
    return QDateTime::fromString(v.left(10), QStringLiteral("yyyy-MM-dd"));
}

SweepResult::Item makeItem(const QJsonValue &v, const char *defaultGlyph) {
    SweepResult::Item it;
    it.glyph = QString::fromLatin1(defaultGlyph);
    if (v.isObject()) {
        const QJsonObject o = v.toObject();
        it.text  = o.value("text").toString().trimmed();
        // owner — string OR null. Accept either; coerce null to empty.
        const QJsonValue ow = o.value("owner");
        if (ow.isString()) it.owner = ow.toString().trimmed();
        // due — string OR null.
        const QJsonValue du = o.value("due");
        if (du.isString()) it.dueAt = parseLooseIso(du.toString());
    } else if (v.isString()) {
        // Permissive: model emitted bare strings instead of {text: ...}.
        it.text = v.toString().trimmed();
    }
    return it;
}

void appendSection(QVector<SweepResult::Item> &dst,
                   const QJsonValue &arr,
                   const char *defaultGlyph) {
    if (!arr.isArray()) return;
    for (const QJsonValue &v : arr.toArray()) {
        SweepResult::Item it = makeItem(v, defaultGlyph);
        if (!it.text.isEmpty()) dst.push_back(std::move(it));
    }
}

}  // namespace

SweepResult parse(const QString &llmReplyJson) {
    SweepResult r;
    r.rawResponse = llmReplyJson;

    const QString stripped = stripFencesAndPreamble(llmReplyJson);
    if (stripped.isEmpty()) {
        r.errorMessage = QStringLiteral("Empty model reply");
        return r;
    }

    QJsonParseError perr{};
    QJsonDocument doc = QJsonDocument::fromJson(stripped.toUtf8(), &perr);
    if (doc.isNull()) {
        // One repair pass — trailing commas. If that still fails we
        // keep the error message; the sweep dialog renders the empty
        // result + "Show raw" disclosure so the user can still read
        // what the model said.
        const QString repaired = repairTrailingCommas(stripped);
        doc = QJsonDocument::fromJson(repaired.toUtf8(), &perr);
        if (doc.isNull()) {
            r.errorMessage = QStringLiteral("Could not parse model reply as JSON: %1")
                                 .arg(perr.errorString());
            return r;
        }
    }

    if (!doc.isObject()) {
        r.errorMessage = QStringLiteral("Model reply was not a JSON object");
        return r;
    }
    const QJsonObject root = doc.object();

    r.summary = root.value("summary").toString().trimmed();

    appendSection(r.decisions, root.value("decisions"), "*");
    appendSection(r.actions,   root.value("actions"),   "[]");
    appendSection(r.questions, root.value("questions"), "?");
    appendSection(r.risks,     root.value("risks"),     "!");

    return r;
}

// ═══════════════════════════════════════════════════════════════════════
// classify — honest outcome triage for the result handler.
// ═══════════════════════════════════════════════════════════════════════

ExtractOutcome classify(const SweepResult &r) {
    if (!r.errorMessage.isEmpty())
        return ExtractOutcome::ParseError;
    if (r.decisions.isEmpty() && r.actions.isEmpty() &&
        r.questions.isEmpty() && r.risks.isEmpty())
        return ExtractOutcome::Empty;
    return ExtractOutcome::Items;
}

}  // namespace NoterSweepPrompt
