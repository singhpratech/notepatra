#ifndef AIPANEL_H
#define AIPANEL_H

#include <QWidget>
#include <QTextEdit>
#include <QLineEdit>
#include <QComboBox>
#include <QPushButton>
#include <QLabel>
#include <QCheckBox>
#include "ollama.h"

class AIPanel : public QWidget {
    Q_OBJECT
public:
    explicit AIPanel(QWidget *parent = nullptr);
    void setContext(const QString &selectedText, const QString &filePath, const QString &language);

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

    // Chat helpers — render the QTextEdit as a chat conversation with
    // bubble-style messages instead of a flat text dump.
    void appendUserBubble(const QString &text);
    void beginAssistantBubble();
    void streamIntoAssistantBubble(const QString &token);
    void endAssistantBubble();
    void clearChat();

    QTextEdit *m_output;
    QLineEdit *m_customInput;
    QComboBox *m_modelCombo;
    QPushButton *m_stopBtn;
    QPushButton *m_refreshBtn;
    QPushButton *m_clearBtn;
    QPushButton *m_attachBtn;
    QLabel *m_attachmentChip;
    QCheckBox *m_thinkingCheck;
    QLabel *m_statusLabel;
    OllamaClient *m_ollama;
    QString m_context;
    QString m_language;
    QString m_lastResponse;
    QString m_currentAssistantText;  // accumulating during stream
    bool m_inAssistantBubble = false;
    QString m_pendingFilePath;       // attached file waiting to be sent
    QString m_pendingFileKind;       // "image", "text", "pdf", "docx", "pptx", etc

private slots:
    void attachFile();
};

#endif
