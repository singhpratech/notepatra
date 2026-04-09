#ifndef FMTPANEL_H
#define FMTPANEL_H

#include <QWidget>
#include <QPushButton>
#include <QLabel>
#include <QHBoxLayout>
#include <QListWidget>
#include <Qsci/qsciscintilla.h>
#include <functional>

class FormatterPanel : public QWidget {
    Q_OBJECT
public:
    explicit FormatterPanel(const QString &title, const QString &language = "JSON", QWidget *parent = nullptr);

    void setInput(const QString &text);
    void addButton(const QString &label, std::function<QString(const QString &)> fn);
    void setOutput(const QString &text);
    void appendOutput(const QString &text);
    // Returns the panel's current effective input — prefers the editable
    // Scintilla content (so users can paste directly into the panel), falls
    // back to whatever was passed via setInput() at panel open time.
    QString inputText() const;
    void setStatus(const QString &text, bool error = false);
    // Append an entry to the panel's per-session history log. Each row
    // shows what action was taken and the size delta, so users can see
    // every change made during the session at a glance.
    void logAction(const QString &action, int beforeChars, int afterChars,
                   const QString &extra = QString());
    // Compute a short human description of what changed between two
    // strings: "+3 commas, +2 braces, -1 trailing comma". Used by the
    // session log so users can see WHAT the action fixed at a glance.
    static QString describeChanges(const QString &before, const QString &after);
    // Last successful transformation — input + output. Used by the
    // Show Diff button to open a CompareWidget tab side-by-side. Empty
    // until at least one button click produces non-empty output.
    QString lastFixInput() const { return m_lastFixInput; }
    QString lastFixOutput() const { return m_lastFixOutput; }
    bool hasLastFix() const { return !m_lastFixInput.isEmpty() && !m_lastFixOutput.isEmpty(); }
    // Called by external AI Fix handlers to record their input/output so
    // the built-in Show Diff button works for them too.
    void recordFix(const QString &before, const QString &after, const QString &actionName);

signals:
    void applyToEditor(const QString &text);
    // Emitted when user clicks the Show Diff button. mainwindow connects
    // this to open a side-by-side CompareWidget tab.
    void showDiffRequested(const QString &before, const QString &after, const QString &title);

private:
    QsciScintilla *m_output;
    QLabel *m_titleLabel;
    QLabel *m_statusLabel;
    QHBoxLayout *m_btnRow;
    QListWidget *m_sessionLog;
    QPushButton *m_diffBtn;
    QString m_lastOutput;
    QString m_inputText;
    QString m_language;
    QString m_lastFixInput;
    QString m_lastFixOutput;
    QString m_lastFixActionName;
    std::function<QString(const QString &)> m_firstAction;
    bool m_hasFirstAction = false;

    void applyLexer();
};

#endif
