#ifndef SQLFMTPANEL_H
#define SQLFMTPANEL_H

#include <QWidget>
#include <QCheckBox>
#include <QSpinBox>
#include <QLabel>
#include <QComboBox>
#include <QPushButton>
#include <QElapsedTimer>
#include <Qsci/qsciscintilla.h>

class OllamaClient;
class OllamaStatus;

class SqlFmtPanel : public QWidget {
    Q_OBJECT
public:
    explicit SqlFmtPanel(QWidget *parent = nullptr);
    void setInput(const QString &sql);

public slots:
    void onThemeChanged();

signals:
    void applyFormatted(const QString &text);

private:
    void applyPalette();

    QsciScintilla *m_output;
    QCheckBox *m_uppercase;
    QSpinBox *m_indent;
    QComboBox *m_dialectCombo;
    QLabel *m_statusLabel;
    QLabel *m_header = nullptr;
    QLabel *m_dialectLabel = nullptr;
    QLabel *m_indentLabel = nullptr;
    QPushButton *m_fmtBtn = nullptr;
    QPushButton *m_compactBtn = nullptr;  // v0.1.49 — compact one-line-where-possible
    QPushButton *m_copyBtn = nullptr;
    QPushButton *m_aiBtn = nullptr;
    QString m_inputText;
    bool m_isStatusError = false;

    // Ollama plumbing for the AI Fix button. Mirrors the pattern the
    // JSON / HTML / Bracket panels use — OllamaClient for the request,
    // OllamaStatus for the "is ollama up?" dot + model dropdown.
    OllamaClient *m_ollama = nullptr;
    OllamaStatus *m_ollamaBar = nullptr;
    QString m_aiOriginalInput;
    QString m_aiStreamBuffer;
    QElapsedTimer m_aiTimer;

    void doFormat();
    void doCompactFormat();  // v0.1.49 — one-line-where-possible variant
    void doAiFix();
    void onAiToken(const QString &token);
    void onAiFinished(const QString &full);
    void onAiError(const QString &msg);
    void setStatus(const QString &text, bool error = false);
    void applySqlDialectKeywords();

    // Strip markdown fences / <think> blocks / leading prose preamble
    // from an LLM response. Mirrors mainwindow.cpp:2113-2131 (JSON AI Fix).
    static QString cleanAiSqlResponse(const QString &raw);
};

#endif
