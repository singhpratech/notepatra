/**
 * Unit tests for AiIntent — the v0.1.40 helper that detects
 * "fix my json/html/sql" prompts in chat input so AIPanel can swap in
 * a strict-patcher system prompt for that turn.
 *
 * Runs headless. Links only ai_intent.cpp + Qt5::Core (the regex impl
 * lives there).
 */
#include "src/ai_intent.h"

#include <QCoreApplication>
#include <QString>
#include <cstdio>

static int passed = 0, failed = 0;

static void check(const char *name, bool ok, const QString &detail = {}) {
    if (ok) { std::printf("  [PASS] %s\n", name); ++passed; }
    else    {
        std::printf("  [FAIL] %s%s%s\n", name,
                    detail.isEmpty() ? "" : " — ",
                    detail.toUtf8().constData());
        ++failed;
    }
}

static const char *kindName(AiIntent::FixKind k) {
    switch (k) {
        case AiIntent::FixKind::Json: return "Json";
        case AiIntent::FixKind::Html: return "Html";
        case AiIntent::FixKind::Sql:  return "Sql";
        case AiIntent::FixKind::None: return "None";
    }
    return "??";
}

static void expectKind(const char *name, const QString &input, AiIntent::FixKind want) {
    AiIntent::FixKind got = AiIntent::detectFixIntent(input);
    if (got == want) {
        std::printf("  [PASS] %s\n", name);
        ++passed;
    } else {
        std::printf("  [FAIL] %s — input=\"%s\" got=%s want=%s\n",
                    name, input.toUtf8().constData(),
                    kindName(got), kindName(want));
        ++failed;
    }
}

int main(int argc, char *argv[]) {
    QCoreApplication app(argc, argv);
    std::printf("=== AiIntent::detectFixIntent ===\n\n");

    // ─── Positive: JSON fix intents ───────────────────────────────────
    expectKind("fix my json (lowercase)",
               "fix my json", AiIntent::FixKind::Json);
    expectKind("FIX MY JSON (uppercase)",
               "FIX MY JSON", AiIntent::FixKind::Json);
    expectKind("Fix My JSON (mixed case)",
               "Fix My JSON", AiIntent::FixKind::Json);
    expectKind("fix the json",
               "fix the json please", AiIntent::FixKind::Json);
    expectKind("fix this broken json",
               "can you fix this broken json", AiIntent::FixKind::Json);
    expectKind("repair my json",
               "repair my json file", AiIntent::FixKind::Json);
    expectKind("broken json (verb-less but unambiguous)",
               "this is broken json", AiIntent::FixKind::Json);
    expectKind("malformed json",
               "the malformed json below", AiIntent::FixKind::Json);
    expectKind("invalid json",
               "this invalid json doesn't parse", AiIntent::FixKind::Json);
    expectKind("json fix (reverse order)",
               "json fix needed here", AiIntent::FixKind::Json);

    // ─── Positive: HTML fix intents ───────────────────────────────────
    expectKind("fix my html",
               "fix my html", AiIntent::FixKind::Html);
    expectKind("repair this html",
               "repair this html", AiIntent::FixKind::Html);
    expectKind("broken html",
               "this is broken html", AiIntent::FixKind::Html);
    expectKind("unclosed tag",
               "fix the unclosed tag in this html", AiIntent::FixKind::Html);

    // ─── Positive: SQL fix intents ────────────────────────────────────
    expectKind("fix my sql",
               "fix my sql query", AiIntent::FixKind::Sql);
    expectKind("repair sql",
               "repair this sql statement", AiIntent::FixKind::Sql);
    expectKind("broken sql",
               "this sql is broken", AiIntent::FixKind::Sql);
    expectKind("malformed sql",
               "got some malformed sql", AiIntent::FixKind::Sql);

    // ─── Negative: explanation / non-fix verbs ────────────────────────
    expectKind("explain my json (non-fix verb)",
               "explain my json", AiIntent::FixKind::None);
    expectKind("describe my json",
               "describe my json structure", AiIntent::FixKind::None);
    expectKind("what is json",
               "what is json", AiIntent::FixKind::None);
    expectKind("teach me json",
               "teach me about json", AiIntent::FixKind::None);
    expectKind("show me json",
               "show me my json files", AiIntent::FixKind::None);
    expectKind("find json",
               "find me a json parser", AiIntent::FixKind::None);
    expectKind("list json",
               "list all json files", AiIntent::FixKind::None);
    expectKind("explain my html",
               "explain my html structure", AiIntent::FixKind::None);
    expectKind("what is sql",
               "what is sql", AiIntent::FixKind::None);

    // ─── Negative: non-fix prompts entirely ───────────────────────────
    expectKind("empty",
               "", AiIntent::FixKind::None);
    expectKind("only whitespace",
               "   \t\n  ", AiIntent::FixKind::None);
    expectKind("hello",
               "hello", AiIntent::FixKind::None);
    expectKind("how are you",
               "how are you today?", AiIntent::FixKind::None);
    expectKind("write me a function",
               "write me a python function", AiIntent::FixKind::None);
    expectKind("fix my code (too generic)",
               "fix my code", AiIntent::FixKind::None);
    expectKind("fix the bug",
               "fix the bug in main.cpp", AiIntent::FixKind::None);

    // ─── Edge: prompts with file mentions ─────────────────────────────
    expectKind("fix my json (with @-mention)",
               "fix my json @config.json", AiIntent::FixKind::Json);
    expectKind("multi-line fix request",
               "this is broken json:\n{ foo: 'bar', }\n\nfix it please",
               AiIntent::FixKind::Json);

    // ─── strictFixSystemPrompt sanity ─────────────────────────────────
    {
        QString p = AiIntent::strictFixSystemPrompt(AiIntent::FixKind::Json);
        check("Json prompt non-empty", !p.isEmpty());
        check("Json prompt mentions JSON",
              p.contains("JSON", Qt::CaseSensitive));
        check("Json prompt mentions PRESERVE",
              p.contains("PRESERVE", Qt::CaseInsensitive));
        check("Json prompt mentions minimal-change",
              p.contains("minimal", Qt::CaseInsensitive));
        check("Json prompt forbids markdown fences",
              p.contains("markdown") || p.contains("```") || p.contains("fences"));
        check("Json prompt warns against reorder",
              p.contains("reorder", Qt::CaseInsensitive));
    }
    {
        QString p = AiIntent::strictFixSystemPrompt(AiIntent::FixKind::Html);
        check("Html prompt non-empty", !p.isEmpty());
        check("Html prompt mentions HTML",
              p.contains("HTML", Qt::CaseSensitive));
        check("Html prompt warns against reorder",
              p.contains("reorder", Qt::CaseInsensitive));
    }
    {
        QString p = AiIntent::strictFixSystemPrompt(AiIntent::FixKind::Sql);
        check("Sql prompt non-empty", !p.isEmpty());
        check("Sql prompt mentions SQL",
              p.contains("SQL", Qt::CaseSensitive));
        check("Sql prompt warns against reformat",
              p.contains("reformat", Qt::CaseInsensitive));
    }
    {
        QString p = AiIntent::strictFixSystemPrompt(AiIntent::FixKind::None);
        check("None → empty prompt", p.isEmpty());
    }

    std::printf("\n--- %d passed, %d failed ---\n", passed, failed);
    return failed == 0 ? 0 : 1;
}
