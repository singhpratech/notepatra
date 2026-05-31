// SPDX-License-Identifier: GPL-3.0-or-later

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
class QDragEnterEvent;
class QDropEvent;
class EditPlanList;

// v0.1.67 — friend hook for the chat-history regression test. Lets
// test_ai_chat_history.cpp read the three per-mode vectors directly and
// flip the mode buttons without going through the toggled() signal
// (which would otherwise drag the entire GUI re-render path into a
// headless unit test). Kept narrow on purpose.
class AIPanelTestAccess;

class AIPanel : public QWidget {
    Q_OBJECT
    friend class AIPanelTestAccess;
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
    // v0.1.61 — accept image / PDF / DOCX / PPTX drops onto the AI panel as
    // attachments. Mirrors attachFile's happy-path: stash path + kind,
    // surface the attachment chip, and let send-time extraction handle the
    // rest. Vision-only kinds (image) gate on modelSupportsVision() — if
    // the active model can't read images, we refuse with a helpful bubble
    // listing concrete model tags the user can pull / switch to.
    void dragEnterEvent(QDragEnterEvent *event) override;
    void dropEvent(QDropEvent *event) override;

public slots:
    void refreshModels();
    // Re-apply the theme-aware stylesheets on the persistent chrome
    // (header band, input, buttons, chat scroll area) and re-render the
    // chat transcript so every bubble inline style is rebuilt against
    // the new aiPalette(). Wired to MainWindow::themeChanged().
    void onThemeChanged();
    // v0.1.67 — programmatically un-fullscreen the AI dock. Used by
    // MainWindow::exitAiFullscreenIfActive() so that opening a new tool
    // tab (Project Search, JSON Tools, REST, Git, …) while the AI dock
    // is expanded restores sibling widgets (file explorer, editor tabs)
    // and lets the user actually see the tab they just opened. We do this
    // by un-checking m_aiExpandBtn, which fires its toggled(false) slot
    // and emits fullscreenToggled(false) — the same path the user takes
    // when they click ⛶ themselves, so MainWindow's existing handler runs
    // unchanged. The AIPanel widget itself is preserved.
    void forceExitFullscreen();

    // v0.1.70 — Reset the panel to Chat mode. Called by MainWindow::toggleAiDock
    // when the AI dock is being shown (Ctrl+Shift+A from hidden → visible) so
    // every fresh open of the dock starts in Chat mode regardless of which
    // mode was active when the dock was last hidden. Per user UX rule:
    // "always default to chat whenever it opens from the button on AI Assistant".
    // No-op when m_chatMode is null or already checked.
    void resetToChatMode();

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

    // v0.1.73 — fired when the user clicks the red ✕ close button in the
    // panel header.  MainWindow hooks this into setAiDockVisible(false)
    // so the close path goes through the canonical hide handler (Config
    // persistence, toolbar button state, splitter rebalance setup, etc.)
    // instead of the panel poking its grandparent's visibility directly,
    // which left the toolbar button checked + Config out of sync and
    // sometimes left the splitter slot at 0 px on subsequent re-show.
    void closeDockRequested();

    // v0.1.61 — same payload as `codingModeRequested`, but with intent-
    // neutral wording. MainWindow listens to gate the file-explorer
    // sidebar visibility strictly to Coding mode. Kept separate so
    // callers that only care about the mode flip (no auto-3-column
    // layout side-effect) don't have to read intent into the older
    // signal name.
    void codingModeChanged(bool active);

public:
    // v0.1.61 — lets MainWindow query the current coding-mode state
    // after constructor wiring (the constructor's internal applyMode()
    // emit fires before MainWindow has hooked its slot, so we need a
    // pull path for the initial sync).
    bool isCodingMode() const;

    // v0.1.110 — the model the user currently has selected (empty if none /
    // not ready). Lets other surfaces (e.g. the Ctrl+I inline-edit dialog)
    // reuse the user's chosen model instead of a hardcoded default —
    // see [[feedback_no_hardcoded_model_allowlists]].
    QString currentModelName() const;

private:
    void sendPrompt(const QString &action);
    // v0.1.110 — single source of truth for the chrome header label: shows the
    // active intent AND the Coding segment (Compose/Agent), so the user can
    // always read their effective posture ("AI · CODING · AGENT" in red when
    // writes hit disk live). Called on mode change, segment change, and theme
    // change. Theme-safe: reuses the same accent-on-chrome pattern already
    // proven on Light/Dark/Monokai.
    void refreshModeHeader();
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

    // v0.1.61 — does the currently-selected model accept image inputs?
    // Mirrors the dual-path strategy used by currentModelSupportsTools():
    //   - Ollama: trust the /api/show capabilities cache (m_ollamaModelCaps)
    //     when populated; if empty (probe still in flight) fall through
    //     to the prefix allowlist below — but be CONSERVATIVE: an empty
    //     cache plus an unknown model name returns false. Silent-drop on
    //     a non-vision Ollama model is the worst trap because the model
    //     just hallucinates a description of nothing.
    //   - Cloud / llama.cpp / OpenAI-compat: lowercase the visible name,
    //     strip any "<provider>/" prefix, then check a hardcoded May-2026
    //     allowlist of multimodal model families (claude-3.5+, gpt-4o /
    //     4.1 / 4.5 / 5, gemini-1.5+, etc.) plus known Ollama vision tags
    //     (qwen2.5vl, llava, gemma3, llama3.2-vision, …).
    bool modelSupportsVision() const;

    // v0.1.61 — shared gate between attachFile() (file picker) and
    // dropEvent() (drag-and-drop). Returns true if the attachment was
    // accepted: the caller should set m_pendingFilePath / Kind and show
    // the chip. Returns false after appending a refusal error bubble when
    // the kind is "image" but modelSupportsVision() is false — caller
    // should NOT proceed.
    bool acceptAttachment(const QString &path, const QString &kind);

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

    // v0.1.61 (Item 8) — chat surface is now ONE conversation with a
    // 3-segment bottom toggle (Chat / Compose / Agent). The previous
    // QTabWidget(Chat | Composer) split is gone — the Edit Plan list
    // now renders inline at the bottom of the chat scroll content,
    // visible whenever it has at least one pending hunk.
    // Edit Plan list — populated when the model returns a dry_run
    // write_file / apply_diff result. Apply All / Apply Selected fires
    // AIPanel::applyComposerEdits which writes the chosen files
    // atomically and emits fileWrittenByAgent. Parented into
    // m_chatContent so it renders inline below the bubbles.
    EditPlanList      *m_editPlan       = nullptr;

    // v0.1.111 — Composer rollback. After each applyComposerEdits we snapshot
    // exactly what we wrote so "Undo apply" can restore the pre-edit content
    // and detect drift (file changed since apply → don't clobber silently).
    // Only the LAST apply is retained (single-level undo).
    struct AppliedSnapshot {
        QString    absPath;
        QString    before;      // pre-edit content (revert target)
        QByteArray afterBytes;  // exact bytes we wrote — the drift baseline
        bool       wasNew;      // true → the apply created the file → revert deletes it
    };
    QVector<AppliedSnapshot> m_lastApplyBatch;

    // Shared atomic .tmp+rename write used by BOTH applyComposerEdits and
    // undoLastApply, so the two paths can never diverge. *wasNew (out) reports
    // whether the destination did not exist before the write.
    bool writeFileAtomic(const QString &absPath, const QByteArray &bytes,
                         bool *wasNew, QString *err);

    // v0.1.61 (Item 8) — bottom-of-panel 3-segment toggle.
    //   Chat    = plain conversation, no tools
    //   Compose = tools enabled but write_file/apply_diff are forced
    //             to dry_run → routed to the Edit Plan list inline
    //   Agent   = full autonomous loop (tools fire, edits hit disk)
    // This is INDEPENDENT of the existing Chat/Coding/Data intent
    // selector — that one picks WHICH model gating + system-prompt
    // layer applies; this one picks how the CURRENT conversation
    // should be presented and which tool surface is active.
    enum class ChatModeSegment { Chat = 0, Compose = 1, Agent = 2 };
    ChatModeSegment m_chatModeSegment = ChatModeSegment::Chat;
    QAbstractButton *m_chatSegBtn    = nullptr;
    QAbstractButton *m_composeSegBtn = nullptr;
    QAbstractButton *m_agentSegBtn   = nullptr;
    // v0.1.70 — wrapper around the Chat/Compose/Agent bottom strip so
    // applyMode can hide it in Chat and Data modes (where Compose +
    // Agent don't apply) and show it only in Coding mode. Hiding the
    // frame collapses the QVBoxLayout slot, so the prompt input grows
    // into the space.
    QFrame *m_chatModeSegFrame = nullptr;

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

    // v0.1.111 — Agent-mode write confirmation gate. In Agent segment a
    // mutating tool (write_file / apply_diff) does NOT hit disk until the
    // user approves it via an inline card. Because the agent loop is
    // event-driven (handleToolCall queues a result; the OllamaClient::finished
    // lambda flushes it), we hold the flush while approvals are pending and
    // let each card's button resume it.
    int  m_pendingWriteApprovals = 0;          // outstanding approval cards
    bool m_turnWriteApproval = false;          // "Approve all writes this turn"
    bool m_streamFinishedAwaitingApproval = false;  // stream ended while pending
    // Render an inline Approve/Reject/Approve-all card for a held mutating
    // tool call and pause the turn until the user decides.
    void enqueueWriteApproval(const QString &id, const QString &name,
                              const QJsonObject &args, const QString &argsSummary);
    // Resume the agent loop once the last pending approval resolves.
    void maybeResumeAfterApprovals();

    // Legacy placeholder — retained so any stray references still compile.
    // All rendering now goes through m_chatLayout.
    QTextBrowser *m_output = nullptr;
    QPlainTextEdit *m_customInput;   // multi-line, Cursor-style. Enter sends, Shift+Enter newlines.
    QComboBox *m_modelCombo;
    QPushButton *m_sendBtn = nullptr;
    QPushButton *m_stopBtn;
    QPushButton *m_refreshBtn;
    QPushButton *m_clearBtn;
    QPushButton *m_attachBtn;
    QPushButton *m_voiceBtn;
    // v0.1.61 — promoted to member so applyMode can hide the ⛶ expand
    // toggle while in Coding/Data mode (those modes commit to fullscreen;
    // user exits by switching back to Chat). In Chat mode it's visible
    // and toggles split↔fullscreen as before.
    QPushButton *m_aiExpandBtn = nullptr;
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
    // Attached-DB confirmation chip — small green pill below the Manage
    // Connections / Browse Schemas row in Data mode. Shows the list of
    // saved connections with their table counts so the user knows what
    // the model will see; refreshed whenever the connections dialog or
    // schema browser closes.
    QLabel *m_attachedDbsChip = nullptr;
    void refreshAttachedDbsChip();
    // v0.1.53 — Data Analyst welcome card (model capability + connection
    // status + example-prompt chips + Hide button). Lives inside the chat
    // scroll area, shown only when Data mode is on AND chat is empty AND
    // Config::aiHideDataWelcome is false.
    QFrame *m_dataWelcomeFrame = nullptr;
    void renderDataWelcomeCard();
    void removeDataWelcomeCard();
    // v0.1.57 — Coding-mode welcome card (Composer / @file mention / Ctrl+I
    // inline edit / agentic git tools intro). Lives inside the chat scroll
    // area, shown only when Coding mode is on AND chat is empty AND
    // Config::aiHideCodingWelcome is false. Parallel to the Data version.
    QFrame *m_codingWelcomeFrame = nullptr;
    void renderCodingWelcomeCard();
    void removeCodingWelcomeCard();
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

    // v0.1.67 — THREE independent conversation vectors, one per mode.
    // Replaces the v0.1.39-v0.1.66 single m_messages vector that mixed
    // every mode's history together. Flipping Chat → Coding → Data now
    // swaps the entire transcript; cross-mode contamination (e.g. the
    // coding-agent system prompt seeing data-analyst chat) is gone.
    //
    // The currently visible vector is selected by activeMessages() based
    // on which of m_chatMode / m_codingMode / m_dataMode is checked.
    // Reads and writes everywhere go through activeMessages(); the
    // three named members are only touched directly in load/save and
    // in tests that need to inspect cross-mode state.
    QVector<ChatMessage> m_chatMessages;     // Chat mode (no tools, default)
    QVector<ChatMessage> m_codingMessages;   // Coding Mode (agentic, file ops)
    QVector<ChatMessage> m_dataMessages;     // Data Analyst Mode (csv_query / query_sql / generate_chart)
    QVector<ChatMessage>       &activeMessages();
    const QVector<ChatMessage> &activeMessages() const;

    // v0.1.39 — persistent chat history. Stored at
    // ~/.config/notepatra/chat-history/<sha1-of-workspace-root>.json
    // (one file per workspace). Loaded on setWorkspaceContext when the
    // workspace changes; saved (debounced 2s) after every push to the
    // active vector. Cleared (file deleted) by clearChat(). Capped at 1MB
    // — older messages roll off the front when the file would exceed.
    // Only User / Assistant / Error roles are persisted; transient tool-
    // call cards are not part of the vectors and aren't saved.
    //
    // v0.1.67 — file format is now { version: 2, chat: [...], coding:
    // [...], data: [...] } so all three vectors round-trip. Old (v1)
    // flat-array files migrate into m_chatMessages; coding/data start
    // empty.
    QString m_chatHistoryPath;
    class QTimer *m_chatSaveTimer = nullptr;
    void updateChatHistoryPath();
    void saveChatHistory();
    void saveChatHistoryNow();  // v0.1.67 — cancel pending debounce + save synchronously
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
    // Slice D — apply the user-approved subset of the Edit Plan. Each
    // (absPath, afterText) pair is written atomically (.tmp + rename),
    // then fileWrittenByAgent is emitted so the editor opens / reloads
    // the buffer. Failures surface as an error bubble; successes drop
    // the row from the list.
    void applyComposerEdits(const QList<QPair<QString, QString>> &edits);
    // v0.1.111 — Composer rollback. Reverts the files written by the LAST
    // applyComposerEdits back to their pre-edit content. Drift-protected:
    // if a file changed on disk since it was applied (user edit, external
    // tool), the user is asked before any clobber. Single-level (last apply
    // only). Wired to EditPlanList::undoApplyRequested.
    void undoLastApply();
    // v0.1.59 — gate the chat input + Send button on model readiness.
    // The dropdown shows "(detecting…)", "(Ollama offline)", "(no models
    // installed)", or "(API key required)" when no model is usable; the
    // input must reflect that with a disabled state + explanatory
    // placeholder so the user doesn't type into a dead field. Called from
    // dropdown change handlers, backend switches, and ollama/openai probes.
    void updateInputAvailability();

    // v0.1.61 (Item 8) — handler for the bottom 3-segment toggle.
    // Stores the new value in m_chatModeSegment, refreshes the placeholder
    // hint, and toggles the inline Edit Plan list's visibility. Purely
    // internal state — no signals are emitted to the host MainWindow.
    void chatModeSelectorChanged(int segment);

    // v0.1.64 — Charts Pack install hand-off. Invoked when the user
    // clicks [Install charts pack] on the lite-mode VegaChartRenderer
    // stub. Opens the GitHub Releases page for the current Notepatra
    // version in the user's default browser, and shows a one-shot
    // QMessageBox with platform-specific install instructions so the
    // user knows which of the two release flavors ("lite" / "full") to
    // download. In-app download lands in v0.1.65 once the Qt plugin
    // shim and cross-platform CI testing are in place.
    void openChartsPackInstall();
};

#endif
