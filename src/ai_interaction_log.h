#ifndef NOTEPATRA_AI_INTERACTION_LOG_H
#define NOTEPATRA_AI_INTERACTION_LOG_H

// ═══════════════════════════════════════════════════════════════════════
// v0.1.71 — AI Interaction Log
// Records every cloud / local LLM exchange (user message, system prompt,
// tool calls, model response, token counts, elapsed) into a small SQLite
// database at ~/.config/notepatra/ai-logs/interactions.db.
//
// Why:
//   The user asked for "full logs of the interaction for past 7 days in
//   rotation" — for data-safety / audit so they can inspect exactly what
//   went to and came back from cloud APIs (OpenAI, OpenRouter, Azure,
//   Ollama Cloud) and local LLMs (Ollama, llama.cpp). Default ON for
//   privacy-as-transparency; user can opt out from Settings → Privacy.
//
// Storage shape:
//   interactions(
//     id INTEGER PRIMARY KEY AUTOINCREMENT,
//     ts INTEGER NOT NULL,         -- unix epoch seconds
//     session_id TEXT NOT NULL,    -- one id per app launch
//     backend TEXT NOT NULL,       -- "ollama" | "ollama-cloud" | "openrouter"
//                                  -- | "openai" | "azure-openai" | "llama.cpp"
//     model TEXT,                  -- e.g. "gemma4:26b" or "gpt-4o"
//     mode TEXT,                   -- "chat" | "coding" | "data" | ""
//     role TEXT NOT NULL,          -- "user" | "system" | "assistant"
//                                  --  | "tool_call" | "tool_result"
//     content TEXT,                -- the message body (scrubbed)
//     tool_name TEXT,              -- when role is tool_call/tool_result
//     tool_args TEXT,              -- JSON
//     tool_result TEXT,            -- truncated JSON / stringified
//     prompt_tokens INTEGER,
//     eval_tokens INTEGER,
//     elapsed_ms INTEGER,
//     error TEXT                   -- non-empty on backend errors
//   );
//   CREATE INDEX idx_ts ON interactions(ts);
//   CREATE INDEX idx_session ON interactions(session_id);
//
// Lifecycle:
//   * pruneOld() called on app start + every 1 hour while running.
//     Drops rows older than 7 days. Caps total db file size to 50 MB
//     (drops oldest rows if exceeded).
//   * record(...) is the only writer. Cheap (<1 ms typical) on the GUI
//     thread; uses WAL mode so the periodic stream-finished hook
//     doesn't block.
//   * If Config::aiInteractionLogging is false the recorder is a no-op
//     and the database file isn't even opened.
//
// Credentials safety:
//   The same redaction the existing AI request scrubber applies (Bearer
//   tokens, OpenAI sk-, AWS access keys, etc.) is run on content/
//   tool_args/tool_result BEFORE the row is written. The log can NEVER
//   become a new credential-exfil path.
// ═══════════════════════════════════════════════════════════════════════

#include <QString>
#include <QVector>
#include <QDateTime>

namespace AiInteractionLog {

enum class Role {
    User,
    System,
    Assistant,
    ToolCall,
    ToolResult,
};

struct Event {
    qint64   id = 0;
    qint64   ts = 0;                // unix seconds
    QString  sessionId;
    QString  backend;               // "ollama" | "openrouter" | …
    QString  model;
    QString  mode;                  // "chat" | "coding" | "data" | ""
    Role     role = Role::User;
    QString  content;
    QString  toolName;
    QString  toolArgs;
    QString  toolResult;
    int      promptTokens = -1;
    int      evalTokens = -1;
    int      elapsedMs = -1;
    QString  error;
};

// Default-on. Persisted via Config::aiInteractionLogging. When false the
// record() / query() paths short-circuit.
bool isEnabled();
void setEnabled(bool on);

// Unique id for the current app launch. Auto-generated at first use.
QString sessionId();

// Async-safe: returns immediately, the row is queued onto a small writer
// thread. Drops the row silently if logging is disabled.
void record(const Event &e);

// Convenience constructors for the four common shapes.
void recordUser(const QString &backend, const QString &model,
                const QString &mode, const QString &content);
void recordAssistant(const QString &backend, const QString &model,
                     const QString &mode, const QString &content,
                     int promptTokens, int evalTokens, int elapsedMs);
void recordToolCall(const QString &backend, const QString &model,
                    const QString &mode, const QString &toolName,
                    const QString &toolArgs);
void recordToolResult(const QString &backend, const QString &model,
                      const QString &mode, const QString &toolName,
                      const QString &toolResult);
void recordError(const QString &backend, const QString &model,
                 const QString &mode, const QString &error);

// Query last N rows or by date range. Returns newest first.
struct Filter {
    qint64  sinceTs = 0;            // 0 = no lower bound
    qint64  untilTs = 0;            // 0 = no upper bound
    QString backend;                // empty = all backends
    QString model;                  // empty = all models
    QString mode;                   // empty = all modes
    int     limit = 1000;
};
QVector<Event> query(const Filter &f);

// Maintenance.
void pruneOld();                    // drop rows >7 days + cap to 50 MB
QString databasePath();             // absolute path for the dialog

} // namespace AiInteractionLog

#endif // NOTEPATRA_AI_INTERACTION_LOG_H
