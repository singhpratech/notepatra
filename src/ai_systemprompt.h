#ifndef AI_SYSTEMPROMPT_H
#define AI_SYSTEMPROMPT_H

#include <QString>

namespace AiSystemPrompt {

// What the user is trying to do, derived from the action they invoked
// and the Coding Mode checkbox state. Drives both the system-prompt
// composition and the workspace-context-attachment gate.
//
// Why this enum exists: tool-calling fine-tuned models (Qwen3, Qwen3.5,
// Hermes-3, Llama 3.1+ Instruct, etc.) hallucinate JSON tool calls when
// they see anything that looks like an agent frame. Notepatra used to
// dump the workspace context block on every request, including casual
// "hi" greetings -- those models pattern-matched the header as an agent
// frame and produced {"command":...,"output":...} instead of chatting.
// Splitting by intent lets us keep workspace context only where it
// actually helps, and add anti-tool-call wording everywhere.
enum class Intent {
    Chat,         // "custom" action, Coding Mode OFF -- general Q&A
    Explain,      // explain / bugs / docs -- prose with code references
    Transform,    // refactor / optimize / tests / comment / translate -- code+brief prose
    CodingStrict, // Coding Mode ON -- byte-identical to legacy strict prompt
};

// Map (action, codingMode) -> Intent. Coding Mode always wins.
Intent classifyIntent(const QString &action, bool codingMode);

// Build the system prompt for this request. The prompt is layered so each
// concern (identity, anti-tool-call, mode-specific behaviour, language hint)
// can be reasoned about independently.
//
// Layers:
//   1. Identity       -- who the AI is, where it lives
//   2. Anti-tool-call -- "no executable tools, do not produce JSON tool calls"
//                        Suppresses the qwen3.5 / hermes / llama-3.1+ default
//                        agent-output behaviour. Cheap (~30 tokens) and harmless
//                        for models that don't have native tool calling.
//   3. Mode-specific  -- per Intent (Chat=friendly, Explain=clear, Transform=
//                        code+brief, CodingStrict=preserved verbatim)
//   4. Language hint  -- "user is working in <lang>" (when known)
QString build(Intent intent, const QString &language);

// Decide whether the workspace-context block should be prepended to the
// user prompt. Returns false for cases where workspace info is noise:
//
//   - CodingStrict (code-only output, doesn't need project tree)
//   - Explain / Transform with a non-empty selection (selection IS the context)
//   - Chat with a non-empty selection (focus on the selection, not workspace)
//   - Chat with a short greeting / casual reply ("hi", "thanks", "ok")
//
// Returns true when the user's question is project-level ("show me my files",
// "what's in this codebase?") -- detected by length + project keywords.
bool shouldAttachWorkspace(Intent intent,
                           const QString &selection,
                           const QString &userText);

} // namespace AiSystemPrompt

#endif
