#ifndef AIPANEL_H
#define AIPANEL_H

#include <QWidget>
#include <QTextEdit>
#include <QLineEdit>
#include <QComboBox>
#include <QPushButton>
#include <QLabel>
#include "ollama.h"

class AIPanel : public QWidget {
    Q_OBJECT
public:
    explicit AIPanel(QWidget *parent = nullptr);
    void setContext(const QString &selectedText, const QString &filePath, const QString &language);

public slots:
    void refreshModels();

signals:
    void insertText(const QString &text);
    void replaceSelection(const QString &text);

private:
    void sendPrompt(const QString &action);
    void setStatus(const QString &text, bool error = false);

    QTextEdit *m_output;
    QLineEdit *m_customInput;
    QComboBox *m_modelCombo;
    QPushButton *m_stopBtn;
    QPushButton *m_refreshBtn;
    QLabel *m_statusLabel;
    OllamaClient *m_ollama;
    QString m_context;
    QString m_language;
    QString m_lastResponse;
};

#endif
