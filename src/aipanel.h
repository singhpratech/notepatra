#ifndef AIPANEL_H
#define AIPANEL_H

#include <QWidget>
#include <QTextBrowser>
#include <QLineEdit>
#include <QComboBox>
#include <QPushButton>
#include <QLabel>
#include <QCheckBox>
#include <QVector>
#include "ollama.h"

class QProcess;
class QUrl;

class AIPanel : public QWidget {
    Q_OBJECT
public:
    explicit AIPanel(QWidget *parent = nullptr);
    void setContext(const QString &selectedText, const QString &filePath, const QString &language);

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

    QTextBrowser *m_output;
    QLineEdit *m_customInput;
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
    OllamaClient *m_ollama;
    QString m_context;
    QString m_language;
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
