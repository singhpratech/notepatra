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
#include <QVector>
#include <functional>
#include "ollama.h"

class QProcess;
class QUrl;

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
    };

protected:
    bool eventFilter(QObject *obj, QEvent *evt) override;

public slots:
    void refreshModels();

signals:
    void insertText(const QString &text);
    void replaceSelection(const QString &text);
    // Fired when user toggles Coding Mode. MainWindow listens so it
    // can auto-arrange the 3-column layout (file explorer | editor |
    // AI dock on right) when coding mode activates, mirroring VS Code
    // / Cursor's layout.
    void codingModeRequested(bool on);

private:
    void sendPrompt(const QString &action);
    void setStatus(const QString &text, bool error = false);
    void updateVoiceButtonVisual(bool recording);
    void renderTranscript();
    void appendErrorBubble(const QString &text);
    void handleChatLink(const QUrl &url);

    // Chat helpers — render the QTextEdit as a chat conversation with
    // bubble-style messages instead of a flat text dump.
    void appendUserBubble(const QString &text);
    void beginAssistantBubble();
    void streamIntoAssistantBubble(const QString &token);
    void endAssistantBubble();
    void clearChat();
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
    QCheckBox *m_codingMode;     // Cursor/Copilot-style "output code, not prose"
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

private slots:
    void attachFile();
};

#endif
