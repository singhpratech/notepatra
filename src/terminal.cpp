#include "terminal.h"
#include "fonts.h"
#include "theme_detect.h"
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
// Supports every common SGR code real CLIs use:
//   0  reset
//   1  bold        2  faint           3  italic
//   4  underline   9  strikethrough
//   22 normal-intensity (cancel bold/faint)
//   23 not-italic  24 not-underline   29 not-strikethrough
//   30-37 FG       40-47 BG           39 default FG     49 default BG
//   90-97 bright FG                   100-107 bright BG
//   38;5;N  / 48;5;N           — 256-colour palette
//   38;2;R;G;B / 48;2;R;G;B   — 24-bit truecolour
//
// Why the explicit support for 256-colour + truecolour: modern CLIs
// (`bat`, `eza`, `fzf`, `delta`, `gh`, recent `cargo`/`npm`) emit these
// by default. Without explicit handling, the 38;5;N segments leaked
// through as literal numbers in the output -- so `bat foo.py` looked
// like a wall of digit-prefixed text instead of syntax-highlighted code.
// ═══════════════════════════════════════════════════════════════════════

struct AnsiPalette {
    QString c[16];  // 0-7 = normal, 8-15 = bright
};
static const AnsiPalette kAnsi = {{
    // Classic VS Code dark palette — readable on #1E1E1E background.
    // Notepatra's terminal frame stays dark regardless of editor theme,
    // so this palette is theme-independent.
    "#1E1E1E", "#F14C4C", "#76D275", "#F2C14E",
    "#569CD6", "#C678DD", "#4EC9B0", "#D4D4D4",
    "#6C6C6C", "#FF8B8B", "#B5E2A9", "#FFE0A3",
    "#9CDCFE", "#E4B0F5", "#A8EAD9", "#FFFFFF",
}};

// xterm 256-colour palette generator. Codes 0-15 mirror our 16-colour
// palette; 16-231 form a 6×6×6 RGB cube; 232-255 are a 24-step grayscale.
static QString ansi256ToHex(int idx) {
    if (idx < 0 || idx > 255) return QString();
    if (idx < 16) return kAnsi.c[idx];
    if (idx >= 232) {
        // 24-step grayscale from #080808 to #EEEEEE
        const int v = 8 + (idx - 232) * 10;
        return QString::asprintf("#%02x%02x%02x", v, v, v);
    }
    // 6×6×6 RGB cube. Each channel is one of: 0, 95, 135, 175, 215, 255.
    const int n = idx - 16;
    static const int kCube[6] = {0, 95, 135, 175, 215, 255};
    const int r = kCube[(n / 36) % 6];
    const int g = kCube[(n / 6)  % 6];
    const int b = kCube[n        % 6];
    return QString::asprintf("#%02x%02x%02x", r, g, b);
}

static QString ansiColourToHex(int code) {
    if (code >= 30 && code <= 37)   return kAnsi.c[code - 30];
    if (code >= 90 && code <= 97)   return kAnsi.c[code - 90 + 8];
    if (code >= 40 && code <= 47)   return kAnsi.c[code - 40];
    if (code >= 100 && code <= 107) return kAnsi.c[code - 100 + 8];
    return QString();
}

// Style-attribute fragments. We track them separately so 22/23/24/29
// (cancel-bold, cancel-italic, etc.) can clear the relevant attribute
// without nuking the whole style state.
struct SgrState {
    QString fg;        // "color:#xxxxxx;" or empty
    QString bg;        // "background:#xxxxxx;" or empty
    bool bold = false;
    bool faint = false;
    bool italic = false;
    bool underline = false;
    bool strike = false;

    void reset() {
        fg.clear(); bg.clear();
        bold = faint = italic = underline = strike = false;
    }

    QString toCss() const {
        QString s;
        if (!fg.isEmpty()) s += fg;
        if (!bg.isEmpty()) s += bg;
        if (bold)      s += "font-weight:bold;";
        if (faint)     s += "opacity:0.6;";
        if (italic)    s += "font-style:italic;";
        if (underline) s += "text-decoration:underline;";
        if (strike) {
            // text-decoration is shorthand: combine with underline if both set
            if (underline) s.replace("text-decoration:underline;",
                                     "text-decoration:underline line-through;");
            else s += "text-decoration:line-through;";
        }
        return s;
    }
};

static QString ansiToHtml(const QString &raw) {
    QString out;
    out.reserve(raw.size() + 32);
    SgrState st;
    bool spanOpen = false;
    int i = 0;
    const int n = raw.size();

    auto reopenSpan = [&]() {
        if (spanOpen) { out += "</span>"; spanOpen = false; }
        const QString css = st.toCss();
        if (!css.isEmpty()) {
            out += "<span style='" + css + "'>";
            spanOpen = true;
        }
    };

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

            // Walk the segments index-by-index so we can look ahead for
            // 38;5;N, 38;2;R;G;B, 48;5;N, 48;2;R;G;B sequences.
            const QStringList parts = params.isEmpty() ? QStringList{"0"} : params.split(';');
            int p = 0;
            while (p < parts.size()) {
                bool ok = false;
                const int code = parts[p].toInt(&ok);
                if (!ok) { ++p; continue; }

                // 38 / 48 are extended-colour introducers and consume
                // either 2 ([5;N]) or 4 ([2;R;G;B]) extra params.
                if (code == 38 || code == 48) {
                    const bool isFg = (code == 38);
                    if (p + 1 < parts.size()) {
                        const int kind = parts[p + 1].toInt();
                        if (kind == 5 && p + 2 < parts.size()) {
                            const int idx = parts[p + 2].toInt();
                            const QString hex = ansi256ToHex(idx);
                            if (!hex.isEmpty()) {
                                if (isFg) st.fg = "color:" + hex + ";";
                                else      st.bg = "background:" + hex + ";";
                            }
                            p += 3;
                            continue;
                        }
                        if (kind == 2 && p + 4 < parts.size()) {
                            const int r = parts[p + 2].toInt();
                            const int g = parts[p + 3].toInt();
                            const int b = parts[p + 4].toInt();
                            const QString hex = QString::asprintf("#%02x%02x%02x",
                                qBound(0, r, 255), qBound(0, g, 255), qBound(0, b, 255));
                            if (isFg) st.fg = "color:" + hex + ";";
                            else      st.bg = "background:" + hex + ";";
                            p += 5;
                            continue;
                        }
                    }
                    ++p;
                    continue;
                }

                switch (code) {
                    case 0:  st.reset();             break;
                    case 1:  st.bold = true;         break;
                    case 2:  st.faint = true;        break;
                    case 3:  st.italic = true;       break;
                    case 4:  st.underline = true;    break;
                    case 9:  st.strike = true;       break;
                    case 22: st.bold = false; st.faint = false; break;
                    case 23: st.italic = false;      break;
                    case 24: st.underline = false;   break;
                    case 29: st.strike = false;      break;
                    case 39: st.fg.clear();          break;
                    case 49: st.bg.clear();          break;
                    default:
                        if ((code >= 30 && code <= 37) || (code >= 90 && code <= 97)) {
                            st.fg = "color:" + ansiColourToHex(code) + ";";
                        } else if ((code >= 40 && code <= 47) || (code >= 100 && code <= 107)) {
                            st.bg = "background:" + ansiColourToHex(code) + ";";
                        }
                        break;
                }
                ++p;
            }

            reopenSpan();
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

    // Theme-aware chrome (header strip + prompt pill + input box + buttons)
    // so the panel integrates with Light theme. The QTextEdit *output* view
    // itself stays dark regardless of theme — the ANSI palette in `kAnsi`
    // is calibrated for a dark background (bright yellows / greens that
    // turn unreadable on a pale canvas), and every mainstream IDE keeps
    // its integrated terminal dark. We theme the frame, not the screen.
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);

    m_header = new QLabel("  Terminal");
    m_header->setMinimumHeight(28);
    m_header->setStyleSheet("QLabel { font-weight: 600; padding: 4px 6px; }");
    layout->addWidget(m_header);

    m_output = new QTextEdit;
    m_output->setReadOnly(true);
    QFont mono = notepatraCodeFont();
    m_output->setFont(mono);
    layout->addWidget(m_output, 1);

    // Colourful zsh-ish prompt: ❯ in accent, directory name in warm tone,
    // inside a rounded pill that spans the full terminal width. Chrome
    // colours come from the shared palette so Light theme gets a pale pill.
    auto *inputRow = new QHBoxLayout;
    inputRow->setContentsMargins(8, 6, 8, 8);
    inputRow->setSpacing(0);

    m_promptLabel = new QLabel();
    m_promptLabel->setFont(mono);
    inputRow->addWidget(m_promptLabel);

    m_input = new QLineEdit;
    m_input->setFont(mono);
    m_input->setPlaceholderText("Type a command and press Enter…");
    inputRow->addWidget(m_input, 1);
    updatePrompt();
    m_copyBtn = new QPushButton("Copy Output");
    m_copyBtn->setFixedHeight(26);
    m_copyBtn->setFixedWidth(90);
    inputRow->addWidget(m_copyBtn);
    layout->addLayout(inputRow);

    applyPalette();

    connect(m_copyBtn, &QPushButton::clicked, this, [this]() {
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
        // Exit interactive mode if it was in effect — restore the normal
        // working-directory prompt and re-enable the input box.
        if (m_interactive) {
            m_interactive = false;
            m_interactiveCmdName.clear();
            updatePrompt();
        }
        m_input->setEnabled(true);
        m_input->setFocus();
    });

    connect(m_input, &QLineEdit::returnPressed, this, &TerminalWidget::onCommandEntered);

    // Banner — tells the user exactly which shell is being used. If
    // they're on macOS the message says "zsh", on Linux with
    // SHELL=fish it says "fish", on Windows with PowerShell available
    // it says "pwsh". No more guessing.
    const ShellInfo si = detectShell();
    m_header->setText(QString("  Terminal — %1").arg(si.display));
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

// Line-based interactive CLIs: prompt-in / text-out, no alternate screen,
// no raw-mode keystrokes required. Wrapping with script(1) gives them a
// real PTY and they work perfectly inline.
static bool isLineInteractive(const QString &cmd) {
    const QString first = cmd.trimmed().section(' ', 0, 0);
    static const QStringList linePrompt = {
        "claude", "codex", "aider", "gh",
        "ssh", "telnet", "mosh",
        "python", "python3", "ipython", "node", "irb", "pry",
        "psql", "mysql", "sqlite3",
        "gdb", "lldb",
    };
    return linePrompt.contains(first);
}

// Curses / fullscreen apps — need alternate-screen + cursor-positioning.
// Our line-based output view can't host them cleanly, so those still get
// the external-terminal handoff.
static bool isFullscreenTty(const QString &cmd) {
    const QString first = cmd.trimmed().section(' ', 0, 0);
    static const QStringList fullscreen = {
        "vim", "nvim", "vi", "nano", "emacs", "micro",
        "top", "htop", "btop", "iotop", "atop",
        "less", "more", "man", "info",
        "tmux", "screen", "byobu",
    };
    return fullscreen.contains(first);
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

    // Line-based interactive CLIs (claude, codex, python REPL, ssh, psql,
    // gdb…) get a PTY via script(1) and keep running inside *our* terminal.
    // The QLineEdit input box switches to stdin mode for the child —
    // pressing Enter sends the line straight to the running process,
    // instead of starting a new `shell -c` invocation.
    if (isLineInteractive(cmd)) {
        const QString scriptBin = QStandardPaths::findExecutable("script");
        if (scriptBin.isEmpty()) {
            appendLine(QString("<span style='color:#F44747;'>`script` not found — "
                               "install util-linux to run interactive CLIs inline.</span>"));
            return;
        }
        m_process->setWorkingDirectory(m_cwd);
        QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
        env.insert("CLICOLOR",      "1");
        env.insert("CLICOLOR_FORCE","1");
        env.insert("FORCE_COLOR",   "1");
        env.insert("TERM",          "xterm-256color");
        m_process->setProcessEnvironment(env);
        m_process->setProcessChannelMode(QProcess::MergedChannels);
        // -q suppresses script's banner, -c runs the command, /dev/null
        // discards the typescript file we don't need.
        m_process->start(scriptBin, {"-q", "-c", cmd, "/dev/null"});
        m_interactive = true;
        m_interactiveCmdName = cmd.section(' ', 0, 0);
        // Keep input enabled so stdin can be fed — change the prompt to
        // signal interactive mode and avoid misleading the user.
        m_input->setEnabled(true);
        m_input->setFocus();
        if (m_promptLabel) {
            m_promptLabel->setText(QString("%1 ▷ ").arg(m_interactiveCmdName));
        }
        appendLine(QString("<span style='color:#4EC9B0;'>▶ %1 running — "
                          "type below to send input, Ctrl+C in the editor to stop</span>")
                       .arg(cmd.toHtmlEscaped()));
        return;
    }

    // Fullscreen TTY apps (vim, top, less…) genuinely need alternate-screen
    // + cursor-positioning; our line-based output can't host them. Keep the
    // external-terminal handoff for these.
    if (isFullscreenTty(cmd)) {
        appendLine(QString(
            "<span style='color:#F2C14E;'>⚠ <b>%1</b> is a fullscreen TTY app — "
            "opening in your system terminal.</span>")
            .arg(cmd.section(' ', 0, 0).toHtmlEscaped()));
        if (!launchExternalTerminal(cmd, m_cwd)) {
            appendLine("<span style='color:#F44747;'>No terminal emulator found on PATH.</span>");
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
    const QString raw = m_input->text();
    m_input->clear();

    // When a line-based interactive CLI is running (claude / python / ssh …)
    // the input line writes to stdin instead of launching a new command.
    // Empty input is still forwarded — that's how you send a bare newline
    // at a REPL prompt.
    if (m_interactive && m_process->state() == QProcess::Running) {
        // Echo what the user typed so the transcript shows their input
        // even though script's cooked mode won't echo it back to us.
        auto cur = m_output->textCursor();
        cur.movePosition(QTextCursor::End);
        m_output->setTextCursor(cur);
        m_output->insertHtml(QString("<span style='color:#DCDCAA;'>%1</span><br>")
                                 .arg(raw.toHtmlEscaped()));
        m_process->write((raw + "\n").toUtf8());
        return;
    }

    if (raw.trimmed().isEmpty()) return;
    runCommand(raw.trimmed());
}

void TerminalWidget::applyPalette() {
    const NpPalette pal = npPalette();

    if (m_header) {
        m_header->setStyleSheet(QString(
            "font-weight: 600; background: %1; color: %2; "
            "padding: 3px 10px; border-bottom: 1px solid %3; "
            "font-size: 11px; letter-spacing: 0.05em;")
            .arg(pal.chromeBg, pal.accent, pal.border));
    }

    // VS-Code-ish "Integrated Terminal" — intentionally dark in both
    // themes so ANSI colour output stays legible. `kAnsi` encodes
    // VT100 colours tuned for #1E1E1E; flipping this to a Light surface
    // would turn yellows to eye-bleach and light greys to invisible.
    if (m_output) {
        m_output->setStyleSheet(
            "QTextEdit { background: #1E1E1E; color: #D4D4D4; border: none; "
            "padding: 8px 12px; selection-background-color: #264F78; "
            "selection-color: #FFFFFF; }"
            "QScrollBar:vertical { background: #1E1E1E; width: 10px; }"
            "QScrollBar::handle:vertical { background: #3E3E3E; "
            "border-radius: 5px; min-height: 40px; margin: 2px; }"
            "QScrollBar::handle:vertical:hover { background: #555; }"
            "QScrollBar::add-line:vertical, QScrollBar::sub-line:vertical { height: 0; }");
    }

    if (m_promptLabel) {
        m_promptLabel->setStyleSheet(QString(
            "background: %1; color: %2; "
            "padding: 6px 10px 6px 12px; border-top-left-radius: 6px; "
            "border-bottom-left-radius: 6px; border: 1px solid %3; "
            "border-right: none;")
            .arg(pal.inputBg, pal.accent, pal.inputBorder));
    }

    if (m_input) {
        m_input->setStyleSheet(QString(
            "QLineEdit { background: %1; color: %2; "
            "border: 1px solid %3; border-left: none; "
            "border-top-right-radius: 6px; border-bottom-right-radius: 6px; "
            "padding: 6px 10px; selection-background-color: %4; "
            "selection-color: %5; }"
            "QLineEdit:focus { border-color: %6; }")
            .arg(pal.inputBg, pal.inputFg, pal.inputBorder,
                 pal.selectionBg, pal.selectionFg, pal.inputFocus));
    }

    if (m_copyBtn) {
        m_copyBtn->setStyleSheet(QString(
            "QPushButton { background: %1; color: %2; "
            "border: 1px solid %3; border-radius: 4px; "
            "padding: 3px 8px; margin-left: 6px; }"
            "QPushButton:hover { background: %4; }")
            .arg(pal.btnBg, pal.btnFg, pal.btnBorder, pal.btnHover));
    }
}

void TerminalWidget::onThemeChanged() {
    applyPalette();
    update();
}
