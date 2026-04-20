#ifndef TERMINAL_H
#define TERMINAL_H

#include <QWidget>
#include <QProcess>
#include <QTextEdit>
#include <QLineEdit>
#include <QLabel>

class TerminalWidget : public QWidget {
    Q_OBJECT
public:
    explicit TerminalWidget(QWidget *parent = nullptr);
    ~TerminalWidget();
    void setWorkingDirectory(const QString &dir);
    void runCommand(const QString &cmd);

private slots:
    void onReadyRead();
    void onCommandEntered();

private:
    QTextEdit *m_output;
    QLineEdit *m_input;
    QLabel *m_promptLabel = nullptr;
    QProcess *m_process;
    QString m_cwd;
    QString m_prompt;
    // Interactive (line-based) CLI running via a PTY wrapper — input box
    // feeds stdin instead of starting a new shell -c invocation on Enter.
    bool m_interactive = false;
    QString m_interactiveCmdName;
    void updatePrompt();
};

#endif
