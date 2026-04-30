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
    DataAnalyst,  // v0.1.43 -- Data Analyst Mode ON (csv/db/charts).
                  // Can attach query_sql + csv_query tools alongside the
                  // file tools, and instructs the model to emit ```chart
                  // fenced JSON specs when a visualization helps.
};

// Map (action, codingMode, dataMode) -> Intent. CodingMode > DataMode > action.
// dataMode default is false to preserve the 2-arg overload contract for
// existing call sites and tests.
Intent classifyIntent(const QString &action, bool codingMode, bool dataMode = false);

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
// `toolsActive` (v0.1.35): if true, the request will carry a `tools`
// array (Coding Mode + tool-capable model). In that case the
// anti-tool-call layer is SUPPRESSED — telling the model "no tools
// exist" while attaching tool definitions produces contradictory
// guidance and makes the model emit tool calls in plain text instead
// of the structured tool_calls field. When tools are present, we
// instead append a brief tool-mode preamble explaining the available
// tools so the model uses them confidently.
QString build(Intent intent, const QString &language, bool toolsActive = false);

// v0.1.43 — same as build() but allows callers to inject a "Project data
// context" layer read from .notepatra/data-analyst.md (or similar). For
// DataAnalyst intent only — silently ignored for the other intents to
// keep their prompts untouched. Capped at 8KB inside the function.
QString buildWithProjectContext(Intent intent,
                                const QString &language,
                                bool toolsActive,
                                const QString &projectContext);

// v0.1.43 — read the workspace's .notepatra/data-analyst.md instruction
// file (if present) and return its content. Returns empty string when:
// the workspace root is empty, the directory or file doesn't exist, the
// file is unreadable, or the file is empty. Cap at 16KB on read so a
// runaway file can't blow up memory; the system-prompt builder caps
// again at 8KB before it lands in the prompt.
QString readDataAnalystInstructions(const QString &workspaceRoot);

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
