#ifndef NOTEPATRA_AI_TOOLS_H
#define NOTEPATRA_AI_TOOLS_H

// ═══════════════════════════════════════════════════════════════════════
// Agentic file-reading tools for Coding Mode.
//
// v0.1.35 introduces tool-calling for Ollama backend models that support
// it (qwen3, llama3.1+, hermes3, mistral-nemo, granite3, etc.). When
// Coding Mode is on AND the active model is in the tool-allowlist, the
// chat request includes a `tools` array describing read_file and
// list_dir. The model can call them; OllamaClient parses the
// `tool_calls` frames out of the NDJSON stream and re-emits as Qt
// signals; AIPanel runs an agent loop that executes each call against
// the workspace and feeds the result back into the conversation.
//
// SECURITY MODEL (defense in depth):
//   1. Workspace anchor — every tool path is resolved relative to
//      AIPanel::m_workspaceRoot. Absolute paths are accepted only if
//      they canonicalize to a path inside the workspace root.
//   2. Canonicalize-and-check — symlinks are followed; the canonical
//      result must start with canonical(workspaceRoot) + "/" (or BE
//      the workspace root itself for list_dir).
//   3. Hardcoded deny-list — even paths inside the workspace are
//      rejected if they match secret patterns (~/.ssh/*, *.pem,
//      *.key, id_rsa*, /etc/{passwd,shadow}, ~/.gnupg/*, ~/.aws/*,
//      ~/.netrc). Catches symlinks-to-secrets that survive (1) + (2).
//   4. Size + count limits — read_file caps at 1500 lines / 2000
//      chars-per-line; list_dir caps at 500 entries. Bigger reads
//      return truncated content + truncated:true so the model can
//      paginate via offset/limit.
//
// Wire format (per the multi-editor research): JSON-Schema function
// definitions, OpenAI-style; arguments come back as a parsed
// QJsonObject (Ollama wire format — note that's an OBJECT, not the
// stringified JSON OpenAI canonical uses; defensive parsing handles
// both shapes).
// ═══════════════════════════════════════════════════════════════════════

#include <QJsonArray>
#include <QJsonObject>
#include <QString>

namespace AiTools {

// ── Tool call descriptors ────────────────────────────────────────────

// One tool invocation requested by the model. `id` is synthesized
// client-side when the backend (Ollama) doesn't supply one — the agent
// loop uses it to correlate result messages.
struct ToolCall {
    QString id;        // synthetic if Ollama; backend-supplied if OpenAI/llama.cpp
    QString name;      // "read_file" or "list_dir"
    QJsonObject args;  // already-parsed arguments object
};

// Result of executing a tool call. Even errors are returned as
// structured results (never thrown) so the agent loop can give the
// model a recoverable error rather than crashing the conversation.
//
// v0.1.56 — write_file / apply_diff support a `dry_run: true` arg
// that DOES NOT touch the filesystem. The result body carries
// {ok:true, result: {dry_run:true, proposed: {path, before, after,
// mode}}} so the Composer UI can render a diff preview. When
// dry_run is false (default) behaviour is unchanged: the file IS
// written and the result body has the legacy shape.
struct ToolResult {
    QString id;        // matches ToolCall.id
    QString name;      // matches ToolCall.name
    QString content;   // text body for the role:tool message
    bool isError = false;
    QString errorKind; // "not_found" | "denied" | "too_large" |
                       // "outside_workspace" | "binary" | "io_error" |
                       // "exists" (write_file mode=create, target exists) |
                       // "conflict" (apply_diff, file drifted from expected) |
                       // "no_connection" (query_sql, name not in db-connections.json) |
                       // "non_select" (query_sql, mutation without confirm) |
                       // "open_failed" (query_sql, driver/connection error) |
                       // "exec_failed" (query_sql / csv_query, SQL error)
};

// ── Tool registry ─────────────────────────────────────────────────────

// Returns the JSON-Schema tool definitions to send in chat requests.
// Shape per Ollama / OpenAI tools spec:
//   [{ "type": "function",
//      "function": {
//        "name": "...",
//        "description": "...",
//        "parameters": { "type": "object", "properties": {...}, "required": [...] }
//      }}, ...]
QJsonArray availableTools();

// Returns true if the model name is in the allowlist of tool-trained
// Ollama models (qwen3*, llama3.1+, hermes3, mistral-nemo, etc.).
// Used by AIPanel to decide whether to attach the tools array. Match
// is case-insensitive substring against well-known tool-trained
// families. False is the safe default.
bool modelLikelySupportsTools(const QString &modelName);

// v0.1.43 — stricter bar than tool support. Returns true only if the
// model is plausibly capable of: writing correct SQL across multiple
// dialects, reasoning about CSV schemas, and emitting correct chart-
// spec JSON. Most cloud frontier models pass. Local models pass only
// when the param-count tag in the name is ≥7B or it's a known-strong
// family (qwen-coder, deepseek-coder, etc.).
//
// Used by AIPanel to show a "weak model" banner when the user toggles
// Data Analyst Mode on with an underpowered model. Mode still works —
// banner is a heads-up.
bool modelCapableOfDataAnalysis(const QString &modelName);

// List of model names known to be strong for data-analysis work. The
// banner shown for unknown / weak models cites a few of these. Order
// is rough preference (most capable first).
QStringList suggestedModelsForDataAnalysis();

// ── Execution ─────────────────────────────────────────────────────────

// Execute a tool call. Always returns a ToolResult — never throws.
// `workspaceRoot` is the canonical absolute path the user has open
// in Notepatra (Explorer root or current-file directory). All paths
// in `call.args` are resolved relative to it; absolute paths must
// canonicalize to a location inside it.
ToolResult execute(const ToolCall &call, const QString &workspaceRoot);

// ── Path safety helpers (exposed for unit testing) ────────────────────

// Returns true if `absPath` matches a hardcoded secret/credential
// pattern (~/.ssh/, ~/.gnupg/, ~/.aws/, ~/.netrc, *.pem, *.key,
// id_rsa*, /etc/passwd, /etc/shadow). Matched on the lowercased
// absolute path with platform-aware separators. Defense in depth —
// even paths that survived the workspace-anchor check via symlink
// shenanigans are caught here.
bool isHardDenied(const QString &absPath);

// Resolves `pathArg` relative to `workspaceRoot` and verifies the
// resulting canonical path is inside the workspace. Sets `outCanonical`
// to the canonical path on success. Returns false if:
//   - pathArg empty
//   - resolved path doesn't exist
//   - canonical path falls outside workspaceRoot (after symlink resolve)
//   - canonical path matches the hardcoded deny-list
bool resolveSafePath(const QString &pathArg,
                     const QString &workspaceRoot,
                     QString *outCanonical,
                     QString *outErrorKind = nullptr);

// Like resolveSafePath but for WRITE-side tools (write_file / apply_diff
// targeting a file that may not yet exist). The target itself need not
// exist, but the PARENT directory must exist + be inside the workspace.
// On success, outCanonical is set to the resolved absolute path of the
// target (the path the tool will write to). The hardcoded deny-list is
// also checked against that path so we never create `~/.ssh/foo` even
// when the parent exists.
//
// outAbsTarget: same as outCanonical but always populated (even if the
// target file doesn't yet exist; in that case it's parent + filename).
bool resolveSafeWritePath(const QString &pathArg,
                          const QString &workspaceRoot,
                          QString *outAbsTarget,
                          QString *outErrorKind = nullptr);

} // namespace AiTools

#endif // NOTEPATRA_AI_TOOLS_H
