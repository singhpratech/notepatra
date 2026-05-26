// SPDX-License-Identifier: GPL-3.0-or-later

// ═══════════════════════════════════════════════════════════════════════
// DiagramAiDialog — natural-language → .npd generation with a review step.
//
// The user describes a diagram ("a login flow with validation"); the local
// Ollama model returns .npd source which is shown in an EDITABLE review pane.
// Nothing touches the canvas until the user clicks Insert — they can Regenerate
// or hand-tweak first. This is the "always allow review at each step" contract;
// once inserted it's an ordinary undoable editor change (redo/undo for free).
//
// Local-only: drives the same OllamaClient the AI panel uses (no cloud unless
// the user has configured a cloud backend there).
// ═══════════════════════════════════════════════════════════════════════

#ifndef NOTEPATRA_DIAGRAM_AI_DIALOG_H
#define NOTEPATRA_DIAGRAM_AI_DIALOG_H

#include <QDialog>
#include <QString>

QT_BEGIN_NAMESPACE
class QComboBox;
class QPlainTextEdit;
class QPushButton;
class QLabel;
QT_END_NAMESPACE

class OllamaClient;

class DiagramAiDialog : public QDialog {
    Q_OBJECT
public:
    explicit DiagramAiDialog(QWidget *parent = nullptr);

    // The reviewed .npd the user accepted (valid only after exec() == Accepted).
    QString resultNpd() const;

private slots:
    void onGenerate();
    void onStreamFinished(const QString &full);
    void onError(const QString &msg);

private:
    void setBusy(bool busy);

    QComboBox      *m_model = nullptr;
    QPlainTextEdit *m_prompt = nullptr;
    QPlainTextEdit *m_review = nullptr;   // generated .npd, editable before insert
    QPushButton    *m_genBtn = nullptr;
    QPushButton    *m_insertBtn = nullptr;
    QLabel         *m_status = nullptr;
    OllamaClient   *m_client = nullptr;
};

#endif  // NOTEPATRA_DIAGRAM_AI_DIALOG_H
