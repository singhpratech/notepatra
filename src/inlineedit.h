// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef INLINEEDIT_H
#define INLINEEDIT_H

// ─────────────────────────────────────────────────────────────────────
// InlineEditDialog — Ctrl+I "rewrite this selection" modal.
//
// Workflow:
//   1. User selects code in the editor and presses Ctrl+I.
//   2. MainWindow constructs InlineEditDialog with the selected text,
//      the file path, and the language label (e.g. "Python", "C++").
//   3. The dialog shows a single-line description ("Python · 14 lines"),
//      a multi-line "tell me what you want…" prompt input, and Send +
//      Cancel buttons.
//   4. On Send, we drive the configured AI backend via OllamaClient
//      (which reads Config::instance() in its constructor — same flow
//      as AIPanel and the AI Fix actions in mainwindow.cpp).
//   5. When the model finishes, we render a simple line-by-line diff
//      (red=removed, green=added) in two side-by-side QPlainTextEdits.
//   6. Apply emits applyRequested(replacement) — MainWindow then calls
//      currentEditor()->replaceSelectedText(replacement).
//
// All AI access goes through OllamaClient — no raw HTTP. We construct
// a local instance because AIPanel does not currently expose its own.
// The cost (one extra QNetworkAccessManager per invocation) is fine
// since the dialog is short-lived and torn down after each use.
// ─────────────────────────────────────────────────────────────────────

#include <QDialog>
#include <QString>

class QLabel;
class QPlainTextEdit;
class QPushButton;
class QStackedWidget;
class QTextEdit;
class OllamaClient;

class InlineEditDialog : public QDialog {
    Q_OBJECT
public:
    InlineEditDialog(const QString &selectedText,
                     const QString &filePath,
                     const QString &language,
                     const QString &model = QString(),
                     QWidget *parent = nullptr);
    ~InlineEditDialog() override;

signals:
    // Emitted when the user clicks Apply on a successful rewrite.
    // Receiver should call editor->replaceSelectedText(replacement).
    void applyRequested(const QString &replacement);

private slots:
    void onSendClicked();
    void onApplyClicked();
    void onTokenReceived(const QString &token);
    void onResponseFinished(const QString &full);
    void onResponseError(const QString &message);

private:
    enum class Stage {
        Prompt,    // user is typing the instruction
        Streaming, // AI is generating
        Diff,      // diff shown, ready to apply / cancel
        ErrorView  // error displayed, user can retry
    };

    void setStage(Stage s);
    void renderDiff(const QString &oldText, const QString &newText);
    static QString stripFences(const QString &raw);

    // Inputs
    QString m_selected;
    QString m_filePath;
    QString m_language;
    QString m_model;   // user's chosen model (empty → OllamaClient default)

    // Promotion of latest streamed text — kept so we can render the diff
    // once the response is finished. Stripped of <think> blocks and
    // fenced-code backtick wrappers.
    QString m_lastResult;

    // Widgets
    QLabel *m_descLabel = nullptr;
    QStackedWidget *m_stack = nullptr;

    // Stage 1 — prompt input
    QPlainTextEdit *m_promptEdit = nullptr;
    QPushButton *m_sendBtn = nullptr;
    QPushButton *m_cancelBtn = nullptr;

    // Stage 2 — streaming feedback
    QLabel *m_streamLabel = nullptr;
    QPlainTextEdit *m_streamEdit = nullptr;

    // Stage 3 — diff
    QTextEdit *m_oldView = nullptr;
    QTextEdit *m_newView = nullptr;
    QPushButton *m_applyBtn = nullptr;
    QPushButton *m_discardBtn = nullptr;

    // Stage 4 — error
    QLabel *m_errorLabel = nullptr;
    QPushButton *m_retryBtn = nullptr;

    // AI client (owned; reads Config::instance() in its ctor).
    OllamaClient *m_ollama = nullptr;
};

#endif
