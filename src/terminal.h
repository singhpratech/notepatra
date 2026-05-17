// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef TERMINAL_H
#define TERMINAL_H

#include <QWidget>
#include <QProcess>
#include <QTextEdit>
#include <QLineEdit>
#include <QLabel>

class QPushButton;

class TerminalWidget : public QWidget {
    Q_OBJECT
public:
    explicit TerminalWidget(QWidget *parent = nullptr);
    ~TerminalWidget();
    void setWorkingDirectory(const QString &dir);
    void runCommand(const QString &cmd);

public slots:
    // Re-apply theme-aware chrome (header strip, prompt pill, input bar,
    // copy button) when MainWindow emits themeChanged(). The QTextEdit
    // output view itself is intentionally frozen to the dark-calibrated
    // ANSI colour palette regardless of theme — see constructor comment.
    void onThemeChanged();

private slots:
    void onReadyRead();
    void onCommandEntered();

private:
    void applyPalette();
    QTextEdit *m_output;
    QLineEdit *m_input;
    QLabel *m_header = nullptr;
    QLabel *m_promptLabel = nullptr;
    QPushButton *m_copyBtn = nullptr;
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
