#ifndef FMTPANEL_H
#define FMTPANEL_H

#include <QWidget>
#include <QPushButton>
#include <QLabel>
#include <QHBoxLayout>
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

signals:
    void applyToEditor(const QString &text);

private:
    QsciScintilla *m_output;
    QLabel *m_titleLabel;
    QLabel *m_statusLabel;
    QHBoxLayout *m_btnRow;
    QString m_lastOutput;
    QString m_inputText;
    QString m_language;
    std::function<QString(const QString &)> m_firstAction;
    bool m_hasFirstAction = false;

    void applyLexer();
};

#endif
