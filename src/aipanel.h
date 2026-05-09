#ifndef AIPANEL_H
#define AIPANEL_H

#include <QWidget>
#include <QTextBrowser>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QComboBox>
#include <QPushButton>
#include <QLabel>
#include <QCheckBox>
#include <QAbstractButton>
#include <QHash>
#include <QVector>
#include <functional>
#include "ollama.h"

class QProcess;
class QUrl;
class QTabWidget;
class QCompleter;

class AIPanel : public QWidget {
    Q_OBJECT
public:
    explicit AIPanel(QWidget *parent = nullptr);

    // One entry per Editor tab currently open in the main window. Passed in
    // via setWorkspaceContext() so the AI can reference other files the user
    // has open (Cursor / Copilot-style cross-file awareness).
    struct OpenTabInfo {
        QString filePath;       // absolute path (or "" for unsaved new files)
        QString displayName;    // tab title / basename — used when filePath is empty
        QString language;
        QString text;           // full editor buffer; truncated later by the token budgeter
        bool isCurrent = false;
    };

    // Backward-compat overload — callers that only know about a single file.
    void setContext(const QString &selectedText, const QString &filePath, const QString &language);

    // Full workspace-aware context. Preferred over the 3-arg overload.
    // `openTabs` should include the current tab (with isCurrent=true) plus
    // every other Editor tab; non-editor panels (Welcome, AI, REST …) should
    // be skipped by the caller.
    void setWorkspaceContext(const QString &selectedText,
                             const QString &currentFilePath,
                             const QString &language,
                             const QString &currentFileText,
                             const QVector<OpenTabInfo> &openTabs,
                             const QString &workspaceRoot,
                             const QStringList &workspaceFilePaths = {});

    // Install a pull-based provider. When the user clicks Send, AIPanel calls
    // `refresh(this)` first so the host MainWindow can push the newest editor
    // text + tab list. This avoids staleness without a stream of signals.
    using ContextProvider = std::function<void(AIPanel *)>;
    void setContextProvider(ContextProvider provider) { m_contextProvider = std::move(provider); }

    // Exposed for unit tests — pure function that assembles the
    // "workspace awareness" block the AI sees before the user's prompt.
    // Budget-capped so small local models don't overflow their context.
    static QString buildWorkspaceContextBlock(
        const QString &currentFilePath,
        const QString &currentFileText,
        const QVector<OpenTabInfo> &openTabs,
        const QString &workspaceRoot);

    struct ChatMessage {
        enum Role {
            User,
            Assistant,
            Error
        };

        Role role = User;
        QString text;
        QString model;
        // Per-response stats (Assistant role only). -1 means "not reported".
        // Rendered in the bubble header as "1234 tokens · 2.3s".
        int promptTokens = -1;
        int evalTokens = -1;
        qint64 elapsedMs = -1;
    };

protected:
    bool eventFilter(QObject *obj, QEvent *evt) override;

public slots:
    void refreshModels();
    // Re-apply the theme-aware stylesheets on the persistent chrome
    // (header band, input, buttons, chat scroll area) and re-render the
    // chat transcript so every bubble inline style is rebuilt against
    // the new aiPalette(). Wired to MainWindow::themeChanged().
    void onThemeChanged();

signals:
    void insertText(const QString &text);
    void replaceSelection(const QString &text);
    // Fired when user toggles Coding Mode. MainWindow listens so it
    // can auto-arrange the 3-column layout (file explorer | editor |
    // AI dock on right) when coding mode activates, mirroring VS Code
    // / Cursor's layout.
    void codingModeRequested(bool on);

    // v0.1.39 — fired when an agentic write_file or apply_diff tool
    // call modifies a file on disk. MainWindow connects this to its
    // openFile() so the new/edited file appears in a tab (or the open
    // editor reloads from disk if already open). `created` is true if
    // the tool just created a brand-new file (write_file mode=create or
    // a fresh overwrite where the path didn't exist before).
    void fileWrittenByAgent(const QString &absPath, bool created);

    // v0.1.56 — fired when the user clicks the expand (⛶) button in the
    // header. MainWindow handles this by hiding sibling splitter widgets
    // and resizing the AI dock to fill the window. `on=false` restores the
    // previous splitter sizes and visibility.
    void fullscreenToggled(bool on);

private:
    void sendPrompt(const QString &action);
    void setStatus(const QString &text, bool error = false);
    void updateVoiceButtonVisual(bool recording);
    void renderTranscript();
    // v0.1.55 — populate m_modelCombo with a tree-grouped view of an
    // OpenRouter / OpenAI /v1/models response. Groups by provider prefix
    // (anthropic / openai / google / meta-llama / mistralai / qwen / x-ai
    // / minimax / moonshotai / deepseek / nvidia / others), sorted by a
    // curated provider-importance order. Each entry includes a price-per-
    // M-tokens suffix so users can tell flagship from cheap at a glance.
    void renderGroupedModelTree(const QJsonArray &dataArr);

    // v0.1.55 — does the currently-selected model support tool calling?
    // For Ollama backends, prefers /api/show capabilities (cached in
    // m_ollamaModelCaps); falls back to AiTools::modelLikelySupportsTools
    // when the cache is empty (probe still in flight, or model probe
    // failed). For non-Ollama backends the cache is irrelevant — those
    // servers handle support detection server-side and silently ignore
    // the tools field for non-tool models, so we always send tools and
    // let the server decide.
    bool currentModelSupportsTools() const;

    // v0.1.56 — paint each row in the model dropdown so the user can see at
    // a glance which models are suitable for the active mode. Coding mode
    // checks tool-call support (via the /api/show capabilities cache for
    // Ollama, or the substring allowlist for cloud / llama.cpp). Data mode
    // checks modelCapableOfDataAnalysis() — local must be ≥7B from a strong
    // family. Chat mode is a no-op (any model is fine for chat). Called
    // after every dropdown repopulate AND on mode toggle. Does NOT remove
    // models from the list — the user can still pick them — it just dims
    // and tooltips them so the choice is informed.
    void decorateModelsByMode();
    // v0.1.55 — guided AI Settings dialog. Replaces the inline API-key
    // QLineEdit on the AI panel and the v0.1.54 ⚙ popup menu. Has one
    // section per cloud provider (OpenRouter, OpenAI) with: a link to
    // the provider's keys page, a password-masked input, Test / Save /
    // Forget buttons, and an inline status label that reports either
    // "✓ Valid · N models" or the precise HTTP error returned by the
    // provider. Both keys can be saved at once.
    void openAiSettingsDialog();
    void appendErrorBubble(const QString &text);
    void handleChatLink(const QUrl &url);

    // Chat helpers — render the QTextEdit as a chat conversation with
    // bubble-style messages instead of a flat text dump.
    void appendUserBubble(const QString &text);
    void beginAssistantBubble();
    void streamIntoAssistantBubble(const QString &token);
    void endAssistantBubble();
    void clearChat();

    // v0.1.35 — Coding-Mode agentic tool-call support. Receives a tool
    // call from OllamaClient::toolCallReceived, executes it against the
    // workspace via AiTools::execute, renders a "🔧 read_file ..." card
    // for the user, and queues the result for continueWithToolResults.
    // Multiple tool calls in one model turn are batched until the
    // stream's `finished` signal arrives, then dispatched together.
    void handleToolCall(const QString &id, const QString &name,
                        const QJsonObject &args);
    // Called when finished() fires to flush queued tool results back
    // to the model and continue the agent loop, OR to settle the
    // conversation if no tool calls were made this turn.
    void flushPendingToolResults();
    void toggleSpeechToText();
    void startTranscription(const QString &audioPath);
    void handleRecordFinished(int exitCode, QProcess *process);
    void handleTranscriptionFinished(int exitCode, QProcess *process, const QString &audioPath);

    // Chat rendering uses REAL Qt widgets — not an HTML string rendered by
    // QTextBrowser. Each message is its own QFrame with a guaranteed
    // stylesheet (Qt widget QSS, not the CSS subset used by rich-text docs,
    // which silently drops a lot of properties). That's what finally lets
    // bubbles read as bubbles with reliable backgrounds and borders.
    class QScrollArea *m_chatArea       = nullptr;
    class QWidget     *m_chatContent    = nullptr;
    class QVBoxLayout *m_chatLayout     = nullptr;
    class QFrame      *m_streamingCard  = nullptr;   // active during a stream
    QTextBrowser      *m_streamingBody  = nullptr;   // inner body of ^

    // Slice A — Coding-mode revamp. The chat scroll lives inside a
    // two-tab widget (Chat | Composer). In Chat / Data modes the tab
    // bar is hidden so the dock looks identical to today's UX. In
    // Coding mode the tab bar is shown so the user can flip between
    // the live chat transcript and the agent's Edit Plan / diff
    // viewer (filled in by Slices B/C/D).
    QTabWidget        *m_chatTabs       = nullptr;
    QScrollArea       *m_composerArea   = nullptr;
    QWidget           *m_composerInner  = nullptr;

    // Live streaming-stats display — shows "GENERATING: 145 tok · 23
    // tok/s · 6.3 s" updating every 250 ms while the model is producing
    // output. Hidden between streams. Final per-response stats still
    // bake into the bubble header via responseStats / renderTranscript
    // so the chat history retains them after streaming ends.
    class QLabel      *m_streamingStats        = nullptr;
    class QTimer      *m_streamingStatsTimer   = nullptr;
    int                m_streamingTokenCount   = 0;
    qint64             m_streamingStartMs      = 0;

    // v0.1.35 — Agent-loop state. Per-turn budget of tool calls executed,
    // plus a queue of completed tool results waiting to be flushed back
    // to the model when the stream's `finished` arrives. Cleared at the
    // start of every fresh user prompt.
    QJsonArray m_pendingToolResults;       // results queued for continuation
    int        m_toolCallsThisTurn  = 0;   // for the consecutive-call budget
    int        m_toolCallsTotal     = 0;   // hard cap across the whole turn
    QString    m_lastSystemPromptForTools; // remember system prompt for continuations
    QJsonArray m_lastToolsArray;           // remember tools array for continuations
    bool       m_toolsActiveThisTurn = false;
    // Legacy placeholder — retained so any stray references still compile.
    // All rendering now goes through m_chatLayout.
    QTextBrowser *m_output = nullptr;
    QPlainTextEdit *m_customInput;   // multi-line, Cursor-style. Enter sends, Shift+Enter newlines.
    QComboBox *m_modelCombo;
    QPushButton *m_stopBtn;
    QPushButton *m_refreshBtn;
    QPushButton *m_clearBtn;
    QPushButton *m_attachBtn;
    QPushButton *m_voiceBtn;
    QLabel *m_attachmentChip;
    QCheckBox *m_thinkingCheck;
    // v0.1.55 — privacy toggle. When unchecked (default), Notepatra
    // suppresses every implicit file-share path: workspace context block,
    // currently-open-file pin in the system prompt, and the empty-
    // selection fallback to whole-file content for quick actions. The
    // user explicitly opts INTO sharing.
    QCheckBox *m_shareFileCheck = nullptr;
    // v0.1.48 — Coding/Data are no longer two checkboxes; they're two
    // segmented buttons in a 3-way mode selector (Chat | Coding | Data).
    // Kept as QAbstractButton* so existing isChecked()/setChecked()/
    // toggled() call sites compile unchanged.
    QAbstractButton *m_codingMode = nullptr;  // Coding agent — write_file / apply_diff / search / run_command
    QAbstractButton *m_dataMode = nullptr;    // Data Analyst — query_sql / csv_query / chart_spec
    QAbstractButton *m_chatMode = nullptr;    // Default — general chat assistant (no flag set)
    QPushButton *m_manageConnsBtn = nullptr;
    // v0.1.55 — "Browse Schemas..." button next to Manage Connections.
    // Opens DbTreeDialog. Visible only in Data mode.
    QPushButton *m_browseSchemasBtn = nullptr;
    // v0.1.55 — when the user picks "Send schema to AI" on a table in
    // the DbTreeDialog, the schema blob is staged here until the next
    // sendPrompt, which prepends it to the user message.
    QString m_pendingSchemaPin;
    QLabel *m_dataCapBanner = nullptr;
    // v0.1.53 — Data Analyst welcome card (model capability + connection
    // status + example-prompt chips + Hide button). Lives inside the chat
    // scroll area, shown only when Data mode is on AND chat is empty AND
    // Config::aiHideDataWelcome is false.
    QFrame *m_dataWelcomeFrame = nullptr;
    void renderDataWelcomeCard();
    void removeDataWelcomeCard();
    QLabel *m_statusLabel;
    QPushButton *m_applyCodeBtn; // one-click "replace selection with response code"
    // Clutter that hides when Coding Mode is on — we want the dock to feel
    // like Cursor/Copilot when the user opts in: just model picker, chat,
    // input bar. Everything else is still there in the default view.
    QWidget *m_quickActionsWrap = nullptr;  // 8 quick-action buttons
    QWidget *m_resultActionsWrap = nullptr; // Insert/Replace/Copy row
    QLabel  *m_headerLabel = nullptr;       // top "AI Assistant" strip — swaps colour/text in Coding Mode
    OllamaClient *m_ollama;
    QString m_context;          // selected text (if any) or current file text
    // v0.1.38 — true if m_context is a real user selection (highlighted text);
    // false if m_context is the fallback "whole current file" content.
    // Used by sendPrompt's "custom" action to decide whether to inline the
    // context into the prompt. Without this flag, every casual chat ("hi")
    // got the entire open file appended — too aggressive a default.
    bool    m_contextIsSelection = false;
    QString m_language;
    // Workspace awareness — populated by setWorkspaceContext() / provider.
    QString m_currentFilePath;
    QString m_currentFileText;  // full buffer of the current tab (not just selection)
    QVector<OpenTabInfo> m_openTabs;
    QString m_workspaceRoot;
    QStringList m_workspaceFilePaths;  // relative paths of every file under the workspace
    ContextProvider m_contextProvider;
    QString m_lastResponse;
    QString m_currentAssistantText;  // accumulating during stream
    bool m_inAssistantBubble = false;
    QString m_pendingFilePath;       // attached file waiting to be sent
    QString m_pendingFileKind;       // "image", "text", "pdf", "docx", "pptx", etc
    QProcess *m_recordProcess = nullptr;
    QProcess *m_transcribeProcess = nullptr;
    QString m_recordedAudioPath;
    QVector<ChatMessage> m_messages;

    // v0.1.39 — persistent chat history. Stored at
    // ~/.config/notepatra/chat-history/<sha1-of-workspace-root>.json
    // (one file per workspace). Loaded on setWorkspaceContext when the
    // workspace changes; saved (debounced 2s) after every push to
    // m_messages. Cleared (file deleted) by clearChat(). Capped at 1MB
    // — older messages roll off the front when the file would exceed.
    // Only User / Assistant / Error roles are persisted; transient tool-
    // call cards are not part of m_messages and aren't saved.
    QString m_chatHistoryPath;
    class QTimer *m_chatSaveTimer = nullptr;
    void updateChatHistoryPath();
    void saveChatHistory();
    void loadChatHistory();
    void scheduleChatSave();

    // v0.1.55 — Ollama /api/show capabilities cache. Keyed by model name
    // (e.g. "qwen3-coder:7b"), value is the lowercased capabilities array
    // ("tools", "thinking", "vision", "completion", "embedding"). Probed
    // on model dropdown change for the Ollama backend; preferred over
    // AiTools::modelLikelySupportsTools when present.
    QHash<QString, QStringList> m_ollamaModelCaps;

    // v0.1.56 — @file mention picker. When the user types `@` in the
    // chat input on a word boundary, the completer pops up listing
    // workspace files (m_workspaceFilePaths). Picking one inserts
    // `@<relative-path> ` into the input. Before Send, sendPrompt calls
    // resolveFileMentions() which scans userText for `@<path>` tokens,
    // resolves each via AiTools::resolveSafePath, reads the file (capped
    // at 64 KB / file, 256 KB total) and appends the contents as
    // `[file: <relpath>]\n<content>\n[end file]` onto the prompt.
    QCompleter *m_filementionCompleter = nullptr;
    void resolveFileMentions(QString &userText);

private slots:
    void attachFile();
};

#endif
