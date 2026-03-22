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
    QString inputText() const { return m_inputText; }

signals:
    void applyToEditor(const QString &text);

private:
    QsciScintilla *m_output;
    QLabel *m_titleLabel;
    QHBoxLayout *m_btnRow;
    QString m_lastOutput;
    QString m_inputText;
    QString m_language;
    std::function<QString(const QString &)> m_firstAction;
    bool m_hasFirstAction = false;

    void applyLexer();
};

#endif
