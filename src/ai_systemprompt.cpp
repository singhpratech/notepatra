#include "ai_systemprompt.h"

#include <QRegularExpression>

namespace AiSystemPrompt {

Intent classifyIntent(const QString &action, bool codingMode) {
    // Coding Mode always wins. The user has explicitly said "I want code,
    // nothing else" -- the action they picked is just the prompt template,
    // not the output style.
    if (codingMode) return Intent::CodingStrict;

    if (action == QLatin1String("explain") ||
        action == QLatin1String("bugs")    ||
        action == QLatin1String("docs")) {
        return Intent::Explain;
    }
    if (action == QLatin1String("refactor")  ||
        action == QLatin1String("optimize")  ||
        action == QLatin1String("tests")     ||
        action == QLatin1String("comment")   ||
        action == QLatin1String("translate")) {
        return Intent::Transform;
    }
    // "custom" with Coding Mode OFF, or any unknown action: treat as Chat.
    return Intent::Chat;
}

static QString identityLayer() {
    return QStringLiteral(
        "You are Notepatra's AI assistant, embedded in a code editor. "
        "You help the user understand and edit their code.");
}

// The anti-tool-call layer is the load-bearing fix for tool-calling
// fine-tuned models (Qwen3 / Qwen3.5 / Hermes-3 / Llama 3.1+ Instruct,
// Mistral Large, Command R, GLM-4, GPT-OSS). Their training data is
// saturated with tool/function-call output formats, so when they see
// anything that looks like an agent frame (e.g. our workspace-context
// header) they emit JSON instead of chatting.
//
// We are explicit about three things:
//   1. There are NO tools in this environment.
//   2. The expected output is plain text or code blocks (markdown fences).
//   3. The forbidden output is the {"command":..., "output":...} shape
//      itself -- naming it concretely is more effective than a generic
//      "be conversational" because tool-calling models recognise the
//      shape they were trained to produce.
//
// For non-tool-calling models (Llama 3.2, Gemma 2, Phi-3.5, Claude, GPT-4)
// this layer is roughly 30 tokens of redundant instruction. Harmless --
// they ignore it because they were already going to chat in plain text.
static QString antiToolCallLayer() {
    return QStringLiteral(
        "This is a chat interface with no executable tools. "
        "Respond in plain natural language, optionally with markdown code "
        "blocks for code. Do not produce function calls, tool calls, or "
        "structured agent output. Do not wrap your response in JSON like "
        "{\"command\": ..., \"output\": ...} or {\"name\": ..., "
        "\"arguments\": ...}. If you would normally call a tool, instead "
        "describe what you would do in plain language.");
}

static QString modeLayer(Intent intent) {
    switch (intent) {
        case Intent::Chat:
            return QStringLiteral(
                "Be friendly and concise. When the user asks about code, "
                "explain it clearly; when they chat, chat back naturally.");

        case Intent::Explain:
            return QStringLiteral(
                "Explain clearly and concisely. Reference specific lines, "
                "function names, or variables when it helps the explanation. "
                "Use markdown code blocks for any code you reference.");

        case Intent::Transform:
            return QStringLiteral(
                "When you output code, preserve the user's indentation style "
                "(tabs vs spaces) and quote style. A brief one-or-two-sentence "
                "explanation before or after the code is fine. Use markdown "
                "code blocks for the code itself.");

        case Intent::CodingStrict:
            // Byte-identical to the legacy Coding Mode prompt body. Preserved
            // verbatim so existing Coding Mode users see no behaviour change
            // from this refactor. The leading "code-editor agent" framing is
            // intentionally retained -- tool-calling models stay focused on
            // code output because the anti-tool-call layer above already
            // suppresses the JSON-tool-call drift.
            return QStringLiteral(
                "Return ONLY the modified source code. No explanations, no "
                "prose, no preambles like 'Here is the code'. Do NOT wrap "
                "the output in markdown code fences (```). Preserve the "
                "original indentation style (tabs vs spaces). Output must "
                "be directly pasteable into the file -- nothing else.");
    }
    return QString(); // unreachable; quiets some compilers
}

static QString languageHint(const QString &language) {
    if (language.isEmpty()) return QString();
    return QStringLiteral("The user is working in ") + language +
           QStringLiteral(".");
}

// v0.1.35 — when tools are attached to the request, the anti-tool-call
// layer is replaced with this brief preamble that tells the model the
// tools ARE available and structured. Keeps the model focused on using
// `tool_calls` (the structured field) instead of typing JSON in plain
// text content.
static QString toolModeLayer() {
    return QStringLiteral(
        "You have agentic workspace tools available. READ side: "
        "`read_file(path, offset?, limit?, with_line_numbers?)` to read a file "
        "(default output prefixes each line with '      N\\t' — "
        "use with_line_numbers=false when you plan to feed lines into "
        "apply_diff old_lines so you do NOT have to strip the prefix yourself), "
        "`list_dir(path)` to list directory entries, "
        "`search(pattern, path?, regex?, glob?, case_sensitive?, max_matches?)` "
        "to find a string/pattern across the workspace. WRITE side: "
        "`write_file(path, content, mode?)` to create or overwrite a file "
        "(mode: 'overwrite' default, 'create' fails if exists, 'append' adds to end), "
        "`apply_diff(path, hunks)` for line-level edits where each hunk has "
        "old_start_line + old_lines (expected current text — must match the "
        "file BYTE-FOR-BYTE; do NOT include the '      N\\t' prefix from "
        "read_file's default output) + new_lines (replacement). Atomic; if "
        "any hunk's old_lines doesn't match the file the call returns "
        "error_kind:conflict and nothing is written. The tool will fall "
        "back to a prefix-strip / whitespace-normalised match and report it "
        "in result.warnings — if you see those warnings, re-read the file "
        "with with_line_numbers=false to avoid them. "
        "Use the tool_calls structured field — never type JSON in your text "
        "response. Paths are workspace-relative; secret/credential paths "
        "(.ssh, .pem, .key, /etc/passwd, etc.) are refused. Prefer write_file "
        "for new files or full rewrites; prefer apply_diff for surgical edits "
        "to large existing files. After writing, the user's editor auto-opens "
        "or reloads the file — no need to ask them to refresh."
    );
}

QString build(Intent intent, const QString &language, bool toolsActive) {
    QString out;
    out.reserve(900);

    out += identityLayer();
    out += QLatin1Char(' ');
    if (toolsActive) {
        // Replace anti-tool-call with tool-mode preamble.
        out += toolModeLayer();
    } else {
        out += antiToolCallLayer();
    }

    const QString mode = modeLayer(intent);
    if (!mode.isEmpty()) { out += QLatin1Char(' '); out += mode; }

    const QString lang = languageHint(language);
    if (!lang.isEmpty()) { out += QLatin1Char(' '); out += lang; }

    return out;
}

// Heuristics for the casual-chat detector. Tuned conservatively -- when in
// doubt, attach workspace context (it's only noise for the chat case, and
// the anti-tool-call layer absorbs most of the damage anyway).
static bool looksLikeCasualChat(const QString &userText) {
    const QString t = userText.trimmed();
    if (t.isEmpty()) return true;        // empty input -- definitely not a code task
    if (t.length() < 30) return true;    // "hi", "thanks", "ok", "yes please" all under 30
    return false;
}

static bool hasCodeShape(const QString &userText) {
    // A handful of strong signals that this is a code-shaped question. Any
    // one match flips the classification toward "needs workspace context".
    static const QRegularExpression kCodeChars(QStringLiteral(
        R"([\{\}\(\)\[\];=]|=>|->|::|\bclass\b|\bfunction\b|\bdef\b|\bimport\b|\binclude\b)"
    ));
    if (kCodeChars.match(userText).hasMatch()) return true;

    // File-extension mentions ("fix the bug in main.cpp", "look at app.py")
    static const QRegularExpression kFileExt(QStringLiteral(
        R"(\.(?:py|js|ts|tsx|jsx|cpp|cxx|cc|c|h|hpp|hxx|java|cs|go|rs|rb|php|swift|kt|m|mm|sh|bat|ps1|json|xml|yml|yaml|toml|ini|conf|sql|html|htm|css|scss|md|txt|log)\b)"
    ));
    if (kFileExt.match(userText).hasMatch()) return true;

    return false;
}

static bool hasProjectKeyword(const QString &userText) {
    static const QRegularExpression kProj(QStringLiteral(
        R"(\b(?:project|workspace|codebase|directory|folder|repo|repository|files?|tree)\b)"),
        QRegularExpression::CaseInsensitiveOption);
    return kProj.match(userText).hasMatch();
}

bool shouldAttachWorkspace(Intent intent,
                           const QString &selection,
                           const QString &userText) {
    // Coding Mode strict: code-only output. Workspace context just bloats
    // the prompt and risks the model echoing parts of it back.
    if (intent == Intent::CodingStrict) return false;

    // Explain / Transform with a selection: the selection IS the focus.
    // Other open files / project tree are noise that distracts the model.
    if (intent == Intent::Explain || intent == Intent::Transform) {
        return selection.isEmpty();
    }

    // Chat with a non-empty selection: same logic -- focus on the selection.
    if (intent == Intent::Chat && !selection.isEmpty()) return false;

    // Chat with no selection: distinguish casual greeting from project query.
    if (intent == Intent::Chat) {
        if (looksLikeCasualChat(userText)) return false;            // "hi", "thanks"
        if (hasProjectKeyword(userText))   return true;             // "show me my files"
        if (hasCodeShape(userText))        return true;             // mentions code shapes
        // Mid-length conversational message with no code/project signals --
        // probably a generic question ("what's a closure?"). Don't attach.
        return false;
    }

    return true; // safe default for any future Intent values
}

} // namespace AiSystemPrompt
