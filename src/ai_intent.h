// v0.1.40 — detects "fix my json/html/sql" intents in chat input so the
// AIPanel can swap in a strict-patcher system prompt for that turn.
// Header-only-ish: declarations live here, regex engines + tests go in the
// .cpp file. No Qt UI dependencies; test_ai_intent.cpp links the .cpp alone.
#pragma once

#include <QString>

namespace AiIntent {

enum class FixKind {
    None,
    Json,
    Html,
    Sql,
};

// Inspect chat-input text for a "fix my X" intent. Returns the format
// being asked about, or FixKind::None if nothing matched. Case-insensitive.
//
// Heuristics intentionally narrow:
//  - "fix" / "repair" verbs (so "explain my json" doesn't match)
//  - direct subject reference (json/html/sql)
//  - or "broken json/html/sql" / "this json/html/sql is broken"
//  - mention of a .json / .html / .sql file in the same input also counts
//    if a fix verb is present
//
// Returns None on ambiguous prompts ("fix my code", "format this"). The
// goal is to upgrade obvious cases without intercepting normal chat.
FixKind detectFixIntent(const QString &userText);

// Produces the strict-patcher system prompt for the matched format.
// Mirrors the prompts at src/mainwindow.cpp:2209 (JSON) — minimal-change,
// preserve-line-order, no prose, no fences.
QString strictFixSystemPrompt(FixKind kind);

} // namespace AiIntent
