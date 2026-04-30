#include "ai_intent.h"

#include <QRegularExpression>

namespace AiIntent {

namespace {

// Match a "fix verb + subject noun" in user text. Order matters: tests
// first against explicit subject mentions, then a "broken X" fallback.
// kind passed by reference so the caller knows which prompt to use.
bool matchFix(const QString &textLower, FixKind &kind) {
    // Explicit fix-verb + format combos. Matches:
    //   "fix my json"      "fix the json"     "fix this json"
    //   "fix json"         "repair this json" "fix the broken json"
    //   "json fix"         "fix my .json file"
    //
    // Anchored to word-boundaries so "prefixed" / "json-rpc" don't match.
    static const QRegularExpression reJson(
        QStringLiteral("\\b(?:fix|repair)\\b[^\\n]{0,40}\\bjson\\b|"
                       "\\bjson\\b[^\\n]{0,30}\\b(?:fix|repair|broken|invalid|malformed)\\b|"
                       "\\bbroken\\s+json\\b|"
                       "\\bmalformed\\s+json\\b|"
                       "\\binvalid\\s+json\\b"),
        QRegularExpression::CaseInsensitiveOption);

    static const QRegularExpression reHtml(
        QStringLiteral("\\b(?:fix|repair)\\b[^\\n]{0,40}\\bhtml\\b|"
                       "\\bhtml\\b[^\\n]{0,30}\\b(?:fix|repair|broken|invalid|malformed|unclosed)\\b|"
                       "\\bbroken\\s+html\\b|"
                       "\\bmalformed\\s+html\\b|"
                       "\\bunclosed\\s+(?:tag|html)\\b"),
        QRegularExpression::CaseInsensitiveOption);

    static const QRegularExpression reSql(
        QStringLiteral("\\b(?:fix|repair|format)\\b[^\\n]{0,40}\\bsql\\b|"
                       "\\bsql\\b[^\\n]{0,30}\\b(?:fix|repair|broken|invalid|malformed)\\b|"
                       "\\bbroken\\s+sql\\b|"
                       "\\bmalformed\\s+sql\\b"),
        QRegularExpression::CaseInsensitiveOption);

    // JSON wins ties — it's the highest-volume use case the user reported.
    if (reJson.match(textLower).hasMatch()) { kind = FixKind::Json; return true; }
    if (reHtml.match(textLower).hasMatch()) { kind = FixKind::Html; return true; }
    if (reSql.match(textLower).hasMatch())  { kind = FixKind::Sql;  return true; }
    return false;
}

} // namespace

FixKind detectFixIntent(const QString &userText) {
    if (userText.trimmed().isEmpty()) return FixKind::None;
    // Cheap exclusion: verb-less informational prompts shouldn't match.
    // "explain my json" / "what is json" etc. — make sure a fix verb is
    // present before the format word.
    QString lower = userText.toLower();

    // Filter out obvious non-fix intents up front (cheap and avoids regex work).
    static const QRegularExpression reExplain(
        QStringLiteral("\\b(?:explain|describe|what\\s+is|what\\s+are|teach|tell\\s+me\\s+about|"
                       "show\\s+me|list|find|search|grep)\\b[^\\n]{0,20}\\b(?:json|html|sql)\\b"),
        QRegularExpression::CaseInsensitiveOption);
    if (reExplain.match(lower).hasMatch()) return FixKind::None;

    FixKind kind = FixKind::None;
    return matchFix(lower, kind) ? kind : FixKind::None;
}

QString strictFixSystemPrompt(FixKind kind) {
    // Same patcher rules as src/mainwindow.cpp:2209 — proven on small
    // models (gemma2:2b, qwen2.5:3b). Format-specific subject swap.
    QString format;
    QString rules;
    switch (kind) {
    case FixKind::Json:
        format = QStringLiteral("JSON");
        rules = QStringLiteral(
            "Rules — apply ONLY these, do NOT improve content:\n"
            "1. Add missing closing braces/brackets — match what's open.\n"
            "2. Remove trailing commas (JSON spec forbids them).\n"
            "3. Wrap unquoted object keys in double quotes.\n"
            "4. Convert single-quoted strings to double-quoted.\n"
            "5. Convert Python True/False/None to true/false/null.\n"
            "6. Strip // and /* */ comments.\n"
            "7. PRESERVE the original line order, key order, and indentation.\n"
            "8. Do NOT reorder keys. Do NOT reformat. Do NOT add fields.\n"
            "9. Output ONLY the corrected JSON. No prose, no markdown ``` "
            "fences, no comments, no <think> blocks, no preamble.\n"
            "10. If the input is already valid JSON, output it UNCHANGED.\n");
        break;
    case FixKind::Html:
        format = QStringLiteral("HTML");
        rules = QStringLiteral(
            "Rules — apply ONLY these, do NOT improve content:\n"
            "1. Close unclosed tags. Self-closing tags (img/br/hr/input/meta/link) "
            "use ' />' form.\n"
            "2. Lowercase tag names if they're inconsistent.\n"
            "3. Quote unquoted attribute values.\n"
            "4. PRESERVE the original element order, attribute order, content text, "
            "indentation, and whitespace structure.\n"
            "5. Do NOT remove tags. Do NOT add new tags. Do NOT reorder attributes.\n"
            "6. Output ONLY the corrected HTML. No prose, no markdown fences, "
            "no <think> blocks, no preamble.\n"
            "7. If the input is already valid HTML, output it UNCHANGED.\n");
        break;
    case FixKind::Sql:
        format = QStringLiteral("SQL");
        rules = QStringLiteral(
            "Rules — apply ONLY these, do NOT improve content:\n"
            "1. Add missing closing parentheses, balanced quotes, missing semicolons "
            "between statements.\n"
            "2. Fix obvious typos in keywords (SELCT → SELECT, FROMM → FROM).\n"
            "3. PRESERVE the original column order, predicate order, indentation, "
            "and whitespace.\n"
            "4. Do NOT reformat. Do NOT add columns or predicates. Do NOT optimize "
            "the query.\n"
            "5. Output ONLY the corrected SQL. No prose, no markdown fences, "
            "no <think> blocks, no preamble.\n"
            "6. If the input is already valid SQL, output it UNCHANGED.\n");
        break;
    case FixKind::None:
        return {};
    }

    return QStringLiteral(
        "You are a minimal-change %1 patcher. Your job is to take broken %1 "
        "and return the SAME %1 with ONLY the broken parts fixed. Preserve "
        "everything else exactly — line order, key order, indentation, "
        "whitespace.\n\n").arg(format) + rules;
}

} // namespace AiIntent
