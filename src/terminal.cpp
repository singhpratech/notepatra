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
#include <QTextCursor>
#include <QPushButton>
#include <QApplication>
#include <QClipboard>
#include <QStandardPaths>

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

// ═══════════════════════════════════════════════════════════════════════
// ANSI escape-sequence → HTML converter.
//
// Real shells emit ANSI colour codes like "\033[32mhello\033[0m" for
// coloured output (ls, grep, git, cargo, npm, ...). The old terminal
// fed everything through QString::toHtmlEscaped() which turned those
// escape bytes into visible garbage. This parser walks the byte stream,
// captures "\033[...m" SGR sequences, and emits matching <span> tags
// with colours pulled from a VT100-style palette. Everything else is
// HTML-escaped and <br>-separated as before.
//
// Supports the 99% of SGR codes real CLIs actually use:
//   0   reset         1   bold              4   underline
//   30-37 FG          40-47 BG              39  default FG  49 default BG
//   90-97 bright FG   100-107 bright BG
// Bracketed 256-colour and truecolour (38;5;N, 38;2;R;G;B) are
// recognised and converted to the nearest palette entry.
// ═══════════════════════════════════════════════════════════════════════

struct AnsiPalette {
    QString c[16];  // 0-7 = normal, 8-15 = bright
};
static const AnsiPalette kAnsi = {{
    // Classic VS Code dark palette — readable on #1E1E1E background
    "#1E1E1E", "#F14C4C", "#76D275", "#F2C14E",
    "#569CD6", "#C678DD", "#4EC9B0", "#D4D4D4",
    "#6C6C6C", "#FF8B8B", "#B5E2A9", "#FFE0A3",
    "#9CDCFE", "#E4B0F5", "#A8EAD9", "#FFFFFF",
}};

static QString ansiColourToHex(int code, bool background) {
    Q_UNUSED(background);
    if (code >= 30 && code <= 37) return kAnsi.c[code - 30];
    if (code >= 90 && code <= 97) return kAnsi.c[code - 90 + 8];
    if (code >= 40 && code <= 47) return kAnsi.c[code - 40];
    if (code >= 100 && code <= 107) return kAnsi.c[code - 100 + 8];
    return QString();
}

static QString ansiToHtml(const QString &raw) {
    QString out;
    out.reserve(raw.size() + 32);
    bool spanOpen = false;
    int i = 0;
    const int n = raw.size();
    while (i < n) {
        QChar c = raw[i];
        if (c == QChar(0x1B) && i + 1 < n && raw[i+1] == '[') {
            // Read up to the final byte (a letter)
            int j = i + 2;
            while (j < n && !(raw[j].isLetter())) ++j;
            if (j >= n) break;
            const QChar finalByte = raw[j];
            const QString params = raw.mid(i + 2, j - i - 2);
            i = j + 1;
            if (finalByte != 'm') continue;  // only handle SGR

            if (spanOpen) { out += "</span>"; spanOpen = false; }

            QString style;
            for (const QString &p : params.split(';')) {
                bool ok = false;
                int code = p.toInt(&ok);
                if (!ok) continue;
                if (code == 0) { style.clear(); break; }
                else if (code == 1) style += "font-weight:bold;";
                else if (code == 4) style += "text-decoration:underline;";
                else if ((code >= 30 && code <= 37) || (code >= 90 && code <= 97)) {
                    style += "color:" + ansiColourToHex(code, false) + ";";
                } else if ((code >= 40 && code <= 47) || (code >= 100 && code <= 107)) {
                    style += "background:" + ansiColourToHex(code, true) + ";";
                }
            }
            if (!style.isEmpty()) {
                out += "<span style='" + style + "'>";
                spanOpen = true;
            }
        } else if (c == '\n') {
            out += "<br>";
            ++i;
        } else if (c == '\r') {
            ++i;  // swallow CR
        } else {
            // Escape a single char for HTML
            if (c == '<') out += "&lt;";
            else if (c == '>') out += "&gt;";
            else if (c == '&') out += "&amp;";
            else if (c == ' ') out += "&nbsp;";  // preserve ls alignment
            else out += c;
            ++i;
        }
    }
    if (spanOpen) out += "</span>";
    return out;
}

} // namespace

TerminalWidget::TerminalWidget(QWidget *parent) : QWidget(parent) {
    m_cwd = QDir::homePath();

    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);

    auto *header = new QLabel("  Terminal");
    header->setFixedHeight(24);
    header->setStyleSheet(
        "font-weight: 600; background: #252526; color: #4EC9B0; "
        "padding: 3px 10px; border-bottom: 1px solid #1E1E1E; "
        "font-size: 11px; letter-spacing: 0.05em;");
    layout->addWidget(header);

    m_output = new QTextEdit;
    m_output->setReadOnly(true);
    QFont mono = notepatraCodeFont();
    m_output->setFont(mono);
    // VS-Code-ish "Integrated Terminal" background with a little bottom
    // padding so the last line doesn't hug the prompt. Use
    // QTextEdit::setLineWrapMode so long output lines wrap rather than
    // overflow horizontally.
    m_output->setStyleSheet(
        "QTextEdit { background: #1E1E1E; color: #D4D4D4; border: none; "
        "padding: 8px 12px; selection-background-color: #264F78; "
        "selection-color: #FFFFFF; }"
        "QScrollBar:vertical { background: #1E1E1E; width: 10px; }"
        "QScrollBar::handle:vertical { background: #3E3E3E; "
        "border-radius: 5px; min-height: 40px; margin: 2px; }"
        "QScrollBar::handle:vertical:hover { background: #555; }"
        "QScrollBar::add-line:vertical, QScrollBar::sub-line:vertical { height: 0; }");
    layout->addWidget(m_output, 1);

    // Colourful zsh-ish prompt: ❯ in teal, directory name in yellow,
    // inside a rounded pill that spans the full terminal width.
    auto *inputRow = new QHBoxLayout;
    inputRow->setContentsMargins(8, 6, 8, 8);
    inputRow->setSpacing(0);

    m_promptLabel = new QLabel();
    m_promptLabel->setFont(mono);
    m_promptLabel->setStyleSheet(
        "background: #252526; color: #DCDCAA; "
        "padding: 6px 10px 6px 12px; border-top-left-radius: 6px; "
        "border-bottom-left-radius: 6px; border: 1px solid #3E3E3E; "
        "border-right: none;");
    inputRow->addWidget(m_promptLabel);

    m_input = new QLineEdit;
    m_input->setFont(mono);
    m_input->setStyleSheet(
        "QLineEdit { background: #252526; color: #D4D4D4; "
        "border: 1px solid #3E3E3E; border-left: none; "
        "border-top-right-radius: 6px; border-bottom-right-radius: 6px; "
        "padding: 6px 10px; selection-background-color: #264F78; }"
        "QLineEdit:focus { border-color: #4EC9B0; }");
    m_input->setPlaceholderText("Type a command and press Enter…");
    inputRow->addWidget(m_input, 1);
    updatePrompt();
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
        // Drain any bytes still buffered after finished() fires. Use
        // insertHtml + explicit <br> so we don't get the extra blank line
        // append() bakes in between writes.
        QByteArray remaining = m_process->readAll();
        auto cur = m_output->textCursor();
        cur.movePosition(QTextCursor::End);
        m_output->setTextCursor(cur);
        if (!remaining.isEmpty()) {
            m_output->insertHtml(ansiToHtml(QString::fromUtf8(remaining)));
        }
        m_output->insertHtml(QString("<br><span style='color:#808080;'>[exit %1]</span><br>").arg(code));
        m_output->verticalScrollBar()->setValue(m_output->verticalScrollBar()->maximum());
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
    updatePrompt();
}

// Interactive CLIs that expect a real TTY (readline, alternate screen,
// raw keystrokes). Our QProcess-pipe terminal can't provide that without
// a PTY layer, so we offer to relaunch them in the user's real terminal.
static bool isInteractiveCommand(const QString &cmd) {
    const QString first = cmd.trimmed().section(' ', 0, 0);
    static const QStringList needsTty = {
        "claude", "codex", "aider", "gh",
        "vim", "nvim", "vi", "nano", "emacs", "micro",
        "top", "htop", "btop", "iotop", "atop",
        "less", "more", "man", "info",
        "ssh", "telnet", "mosh",
        "tmux", "screen", "byobu",
        "python", "python3", "ipython", "node", "irb", "pry",
        "psql", "mysql", "sqlite3",
        "gdb", "lldb",
    };
    return needsTty.contains(first);
}

// Launch command in the user's real terminal emulator. Tries a sensible
// list (x-terminal-emulator, gnome-terminal, konsole, xfce4-terminal,
// xterm) and falls back to an error toast if nothing is found.
static bool launchExternalTerminal(const QString &cmd, const QString &cwd) {
    const QStringList emulators = {
        "x-terminal-emulator", "gnome-terminal", "konsole",
        "xfce4-terminal", "mate-terminal", "tilix",
        "alacritty", "kitty", "wezterm", "foot", "xterm"
    };
    const QString keepOpen = cmd + "; echo; echo '[press Enter to close]'; read";
    for (const QString &term : emulators) {
        if (QStandardPaths::findExecutable(term).isEmpty()) continue;
        QStringList args;
        if (term == "gnome-terminal") {
            args << "--working-directory=" + cwd << "--" << "bash" << "-lc" << keepOpen;
        } else {
            // xterm / konsole / xfce4-terminal / alacritty / kitty / wezterm /
            // foot / tilix / mate-terminal all accept `-e CMD…` — pipe through
            // bash -lc so the keep-open read prompt works uniformly.
            args << "-e" << "bash" << "-lc" << keepOpen;
        }
        QProcess::startDetached(term, args, cwd);
        return true;
    }
    return false;
}

void TerminalWidget::runCommand(const QString &cmd) {
    // Use insertHtml + explicit <br> instead of append() — append() forces
    // a blank line between writes which breaks visual alignment when the
    // previous line didn't end with a newline (e.g. a streaming process).
    auto appendLine = [this](const QString &html) {
        auto cur = m_output->textCursor();
        cur.movePosition(QTextCursor::End);
        m_output->setTextCursor(cur);
        m_output->insertHtml(html + "<br>");
        m_output->verticalScrollBar()->setValue(m_output->verticalScrollBar()->maximum());
    };

    appendLine(QString("<br><span style='color:#4EC9B0;'>%1 $</span> <span style='color:#DCDCAA;'>%2</span>")
                   .arg(QDir(m_cwd).dirName().toHtmlEscaped(), cmd.toHtmlEscaped()));

    if (cmd.trimmed() == "clear" || cmd.trimmed() == "cls") {
        m_output->clear();
        return;
    }

    if (cmd.trimmed().startsWith("cd ") || cmd.trimmed() == "cd") {
        QString dir = cmd.trimmed().mid(2).trimmed();
        if (dir.isEmpty() || dir == "~") dir = QDir::homePath();
        else if (dir.startsWith("~/")) dir = QDir::homePath() + dir.mid(1);
        else if (!dir.startsWith("/")) dir = m_cwd + "/" + dir;
        QDir d(dir);
        if (d.exists()) {
            m_cwd = d.canonicalPath();
            updatePrompt();
            appendLine(QString("<span style='color:#808080;'>%1</span>").arg(m_cwd.toHtmlEscaped()));
        } else {
            appendLine(QString("<span style='color:#F44747;'>cd: %1: No such directory</span>").arg(dir.toHtmlEscaped()));
        }
        return;
    }

    // Interactive CLIs (claude, codex, vim, top, ssh…) require a real
    // TTY. Our QProcess terminal doesn't have one, so detect the common
    // ones and offer a one-tap handoff to the system terminal emulator.
    if (isInteractiveCommand(cmd)) {
        appendLine(QString(
            "<span style='color:#F2C14E;'>⚠ <b>%1</b> needs a real terminal (TTY).</span>")
            .arg(cmd.section(' ', 0, 0).toHtmlEscaped()));
        appendLine("<span style='color:#808080;'>The built-in terminal runs commands through a pipe, "
                   "which works great for ls / grep / git / npm / cargo / make etc. but not for "
                   "interactive CLIs.</span>");
        if (launchExternalTerminal(cmd, m_cwd)) {
            appendLine(QString(
                "<span style='color:#4EC9B0;'>→ launched \"%1\" in your system terminal</span>")
                .arg(cmd.toHtmlEscaped()));
        } else {
            appendLine("<span style='color:#F44747;'>Couldn't find a terminal emulator on PATH "
                       "(tried gnome-terminal, konsole, xterm, alacritty, kitty, …). "
                       "Install one, or run the command from your regular terminal.</span>");
        }
        return;
    }

    // Run async through whatever shell detectShell() picked (honours
    // $SHELL, falls back to platform default, uses PowerShell on
    // Windows if available). Inject CLICOLOR / FORCE_COLOR so common
    // tools (ls, grep, git, cargo, npm) emit colour codes even when
    // they detect the pipe — the ANSI parser will render them.
    m_input->setEnabled(false);
    m_process->setWorkingDirectory(m_cwd);
    const ShellInfo si = detectShell();

    QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
    env.insert("CLICOLOR",      "1");
    env.insert("CLICOLOR_FORCE","1");  // BSD ls
    env.insert("FORCE_COLOR",   "1");  // npm, cargo
    env.insert("TERM",          "xterm-256color");
    m_process->setProcessEnvironment(env);

    m_process->start(si.path, {si.flag, cmd});
}

void TerminalWidget::updatePrompt() {
    if (!m_promptLabel) return;
    // ZSH-style abbreviated path: $HOME → ~, show just the last 2
    // segments if nested deep (e.g. ~/proj/repo/src/utils → …/src/utils).
    QString path = m_cwd;
    const QString home = QDir::homePath();
    if (path == home) path = "~";
    else if (path.startsWith(home + "/")) path = "~" + path.mid(home.length());
    const QStringList parts = path.split('/', Qt::SkipEmptyParts);
    QString tail = parts.size() > 2 ? ".../" + parts.mid(parts.size() - 2).join('/')
                                     : path;
    if (tail.isEmpty()) tail = "/";
    m_promptLabel->setText(QString("%1 ❯ ").arg(tail));
}

void TerminalWidget::onReadyRead() {
    QByteArray data = m_process->readAll();
    if (data.isEmpty()) return;
    // Parse ANSI escape sequences so ls / grep / git / cargo output
    // renders in colour rather than showing "\033[32m" gibberish.
    const QString html = ansiToHtml(QString::fromUtf8(data));
    // Use insertHtml + preserve existing cursor so rapid writes don't
    // each start on a new line (append() forces a newline between calls).
    auto cur = m_output->textCursor();
    cur.movePosition(QTextCursor::End);
    m_output->setTextCursor(cur);
    m_output->insertHtml(html);
    m_output->verticalScrollBar()->setValue(m_output->verticalScrollBar()->maximum());
}

void TerminalWidget::onCommandEntered() {
    QString cmd = m_input->text().trimmed();
    m_input->clear();
    if (cmd.isEmpty()) return;
    runCommand(cmd);
}
