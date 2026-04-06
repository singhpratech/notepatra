#include "terminal.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QFont>
#include <QDir>
#include <QScrollBar>
#include <QKeyEvent>
#include <QPushButton>
#include <QApplication>
#include <QClipboard>

TerminalWidget::TerminalWidget(QWidget *parent) : QWidget(parent) {
    m_cwd = QDir::homePath();

    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);

    auto *header = new QLabel("  Terminal");
    header->setFixedHeight(22);
    header->setStyleSheet("font-weight: bold; background: #2D2D2D; color: #CCC; padding: 2px 6px;");
    layout->addWidget(header);

    m_output = new QTextEdit;
    m_output->setReadOnly(true);
    QFont mono("Consolas", 10);
    mono.setStyleHint(QFont::Monospace);
    m_output->setFont(mono);
    m_output->setStyleSheet("QTextEdit { background: #1E1E1E; color: #D4D4D4; border: none; padding: 4px; }");
    layout->addWidget(m_output, 1);

    auto *inputRow = new QHBoxLayout;
    inputRow->setContentsMargins(4, 2, 4, 2);
    auto *promptLabel = new QLabel("$");
    promptLabel->setStyleSheet("color: #4EC9B0; font-family: monospace; font-weight: bold;");
    inputRow->addWidget(promptLabel);

    m_input = new QLineEdit;
    m_input->setFont(mono);
    m_input->setStyleSheet("QLineEdit { background: #2D2D2D; color: #D4D4D4; border: 1px solid #555; padding: 4px; }");
    m_input->setPlaceholderText("Type command and press Enter...");
    inputRow->addWidget(m_input, 1);
    auto *copyBtn = new QPushButton("Copy Output");
    copyBtn->setFixedHeight(26);
    copyBtn->setFixedWidth(90);
    inputRow->addWidget(copyBtn);
    layout->addLayout(inputRow);

    connect(copyBtn, &QPushButton::clicked, this, [this]() {
        QApplication::clipboard()->setText(m_output->toPlainText());
    });

    m_process = new QProcess(this);
    m_process->setProcessChannelMode(QProcess::MergedChannels);

    connect(m_process, &QProcess::readyReadStandardOutput, this, &TerminalWidget::onReadyRead);
    connect(m_process, &QProcess::readyRead, this, &TerminalWidget::onReadyRead);
    connect(m_process, static_cast<void(QProcess::*)(int, QProcess::ExitStatus)>(&QProcess::finished),
            this, [this](int code, QProcess::ExitStatus) {
        QByteArray remaining = m_process->readAll();
        if (!remaining.isEmpty())
            m_output->append(QString::fromUtf8(remaining).toHtmlEscaped().replace("\n", "<br>"));
        m_output->append(QString("<span style='color:#808080;'>[exit %1]</span>").arg(code));
        m_input->setEnabled(true);
        m_input->setFocus();
    });

    connect(m_input, &QLineEdit::returnPressed, this, &TerminalWidget::onCommandEntered);

    m_output->append("<span style='color:#4EC9B0;'>Notepatra Terminal</span>");
    m_output->append("<span style='color:#808080;'>Type commands below. cd, clear work. Commands run in bash.</span>\n");
    m_input->setFocus();
}

TerminalWidget::~TerminalWidget() {
    if (m_process->state() != QProcess::NotRunning) {
        m_process->kill();
        m_process->waitForFinished(1000);
    }
}

void TerminalWidget::setWorkingDirectory(const QString &dir) {
    m_cwd = dir;
}

void TerminalWidget::runCommand(const QString &cmd) {
    m_output->append(QString("<br><span style='color:#4EC9B0;'>%1 $</span> <span style='color:#DCDCAA;'>%2</span>")
                     .arg(QDir(m_cwd).dirName(), cmd.toHtmlEscaped()));

    if (cmd.trimmed() == "clear" || cmd.trimmed() == "cls") {
        m_output->clear();
        return;
    }

    if (cmd.trimmed().startsWith("cd ")) {
        QString dir = cmd.trimmed().mid(3).trimmed();
        if (dir == "~") dir = QDir::homePath();
        else if (dir.startsWith("~/")) dir = QDir::homePath() + dir.mid(1);
        else if (!dir.startsWith("/")) dir = m_cwd + "/" + dir;
        QDir d(dir);
        if (d.exists()) {
            m_cwd = d.canonicalPath();
            m_output->append(QString("<span style='color:#808080;'>%1</span>").arg(m_cwd));
        } else {
            m_output->append(QString("<span style='color:#F44747;'>cd: %1: No such directory</span>").arg(dir));
        }
        return;
    }

    // Run async
    m_input->setEnabled(false);
    m_process->setWorkingDirectory(m_cwd);
#ifdef Q_OS_WIN
    m_process->start("cmd.exe", {"/c", cmd});
#elif defined(Q_OS_MAC)
    m_process->start("/bin/zsh", {"-c", cmd});
#else
    m_process->start("/bin/bash", {"-c", cmd});
#endif
}

void TerminalWidget::onReadyRead() {
    QByteArray data = m_process->readAll();
    if (data.isEmpty()) return;
    QString text = QString::fromUtf8(data);
    m_output->append(text.toHtmlEscaped().replace("\n", "<br>"));
    m_output->verticalScrollBar()->setValue(m_output->verticalScrollBar()->maximum());
}

void TerminalWidget::onCommandEntered() {
    QString cmd = m_input->text().trimmed();
    m_input->clear();
    if (cmd.isEmpty()) return;
    runCommand(cmd);
}
