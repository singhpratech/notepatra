#include "terminal.h"
#include "fonts.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QFont>
#include <QDir>
#include <QFileInfo>
#include <QScrollBar>
#include <QKeyEvent>
#include <QProcessEnvironment>
#include <QPushButton>
#include <QApplication>
#include <QClipboard>

namespace {

// Pick the user's actual shell rather than a hardcoded /bin/bash.
//
// Resolution order:
//   1. $SHELL env var (respects user's preference — /bin/zsh for modern
//      macOS users, /usr/bin/fish for fish lovers, /bin/bash for Linux
//      defaults, etc.)
//   2. Platform default (zsh on macOS since Catalina 2019, pwsh→cmd on
//      Windows, bash on Linux)
//   3. Final fallback: sh
//
// Returns a {exePath, invocationFlag} pair where invocationFlag is what
// we pass before the command string (-c for POSIX shells, /c for cmd.exe,
// -Command for PowerShell).
struct ShellInfo {
    QString path;     // full path to the shell binary
    QString flag;     // -c  /  /c  /  -Command
    QString display;  // "bash" / "zsh" / "fish" / "pwsh" / "cmd"
};

static ShellInfo detectShell() {
    const QProcessEnvironment env = QProcessEnvironment::systemEnvironment();

#ifdef Q_OS_WIN
    // Prefer modern PowerShell (pwsh) if installed, else Windows
    // PowerShell, else cmd.exe. $COMSPEC is the Windows equivalent of
    // $SHELL and points to cmd.exe by default.
    if (QFileInfo::exists("C:/Program Files/PowerShell/7/pwsh.exe"))
        return {"C:/Program Files/PowerShell/7/pwsh.exe", "-Command", "pwsh"};
    if (QFileInfo::exists("C:/Windows/System32/WindowsPowerShell/v1.0/powershell.exe"))
        return {"C:/Windows/System32/WindowsPowerShell/v1.0/powershell.exe",
                "-Command", "powershell"};
    const QString cs = env.value("COMSPEC", "C:/Windows/System32/cmd.exe");
    return {cs, "/c", QFileInfo(cs).baseName().toLower()};
#else
    // Honour $SHELL if the user has a login shell configured. This is
    // the single source of truth users expect — if they've set fish in
    // chsh, the terminal tab should run fish.
    const QString shellEnv = env.value("SHELL");
    if (!shellEnv.isEmpty() && QFileInfo::exists(shellEnv)) {
        return {shellEnv, "-c", QFileInfo(shellEnv).baseName()};
    }

#ifdef Q_OS_MAC
    // macOS default since Catalina (2019).
    if (QFileInfo::exists("/bin/zsh")) return {"/bin/zsh", "-c", "zsh"};
#endif
    if (QFileInfo::exists("/bin/bash")) return {"/bin/bash", "-c", "bash"};
    if (QFileInfo::exists("/usr/bin/fish")) return {"/usr/bin/fish", "-c", "fish"};
    // POSIX floor
    return {"/bin/sh", "-c", "sh"};
#endif
}

} // namespace

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
    QFont mono = notepatraCodeFont();
    m_output->setFont(mono);
    m_output->setStyleSheet("QTextEdit { background: #1E1E1E; color: #D4D4D4; border: none; padding: 4px; }");
    layout->addWidget(m_output, 1);

    auto *inputRow = new QHBoxLayout;
    inputRow->setContentsMargins(4, 2, 4, 2);
    auto *promptLabel = new QLabel("$");
    promptLabel->setStyleSheet(QString("color: #4EC9B0; font-family: %1; font-weight: 600;")
                               .arg(notepatraCodeCssFamily()));
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

    // Banner — tells the user exactly which shell is being used. If
    // they're on macOS the message says "zsh", on Linux with
    // SHELL=fish it says "fish", on Windows with PowerShell available
    // it says "pwsh". No more guessing.
    const ShellInfo si = detectShell();
    header->setText(QString("  Terminal — %1").arg(si.display));
    m_output->append("<span style='color:#4EC9B0;'>Notepatra Terminal</span>");
    m_output->append(QString("<span style='color:#808080;'>Shell: <b>%1</b> &nbsp; · &nbsp; %2</span>")
                         .arg(si.display.toHtmlEscaped(), si.path.toHtmlEscaped()));
    m_output->append("<span style='color:#808080;'>Type commands below. cd, clear, and any " +
                     si.display.toHtmlEscaped() + " syntax work.</span>\n");
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

    // Run async through whatever shell detectShell() picked (honours
    // $SHELL, falls back to platform default, uses PowerShell on
    // Windows if available).
    m_input->setEnabled(false);
    m_process->setWorkingDirectory(m_cwd);
    const ShellInfo si = detectShell();
    m_process->start(si.path, {si.flag, cmd});
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
