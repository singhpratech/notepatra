#include "fmtpanel.h"
#include "npp_palette.h"
#include "fonts.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QFont>
#include <QApplication>
#include <QClipboard>
#include <QDateTime>
#include <QListWidgetItem>

#include <Qsci/qscilexerjson.h>
#include <Qsci/qscilexerjavascript.h>
#include <Qsci/qscilexerhtml.h>
#include <Qsci/qscilexersql.h>
#include <Qsci/qscilexerpython.h>
#include <Qsci/qscilexerbash.h>
#include <Qsci/qscilexercpp.h>
#include <Qsci/qscilexercss.h>
#include <Qsci/qscilexerxml.h>
#include <Qsci/qscilexeryaml.h>

FormatterPanel::FormatterPanel(const QString &title, const QString &language, QWidget *parent)
    : QWidget(parent), m_language(language) {

    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);

    m_titleLabel = new QLabel("  " + title);
    m_titleLabel->setFixedHeight(24);
    m_titleLabel->setStyleSheet("font-weight: bold; background: #2D2D2D; color: #4EC9B0; padding: 2px 6px;");
    layout->addWidget(m_titleLabel);

    auto *btnWidget = new QWidget;
    m_btnRow = new QHBoxLayout(btnWidget);
    m_btnRow->setContentsMargins(4, 4, 4, 4);

    // Show Diff button — opens a side-by-side compare of the LAST action's
    // input vs output. Disabled until at least one button click produces
    // non-empty output. Works for ANY action (Format, Minify, Fix+Format,
    // AI Fix, etc.) — every transformation gets recorded automatically.
    m_diffBtn = new QPushButton("Show Diff");
    m_diffBtn->setFixedHeight(26);
    m_diffBtn->setEnabled(false);
    m_diffBtn->setToolTip("Open a side-by-side compare of the last action's input vs output");
    connect(m_diffBtn, &QPushButton::clicked, this, [this]() {
        if (!hasLastFix()) {
            setStatus("No transformation recorded yet — click Format / Fix / AI Fix first", true);
            return;
        }
        emit showDiffRequested(m_lastFixInput, m_lastFixOutput,
                               QString("Diff: %1").arg(m_lastFixActionName));
    });

    auto *copyBtn = new QPushButton("Copy Output");
    copyBtn->setFixedHeight(26);
    connect(copyBtn, &QPushButton::clicked, this, [this]() {
        QString text = m_output->text();
        if (text.isEmpty()) {
            setStatus("Nothing to copy — panel is empty", true);
            return;
        }
        QApplication::clipboard()->setText(text);
        setStatus(QString("✓ Copied %1 chars to clipboard").arg(text.length()), false);
    });

    m_btnRow->addStretch();
    m_btnRow->addWidget(m_diffBtn);
    m_btnRow->addWidget(copyBtn);
    layout->addWidget(btnWidget);

    // BIG status banner — every button press updates this so users see something happen
    m_statusLabel = new QLabel("💡 Paste content into the panel below, then click a button above");
    m_statusLabel->setStyleSheet(
        "background: #1e3a3a; color: #4EC9B0; padding: 8px 12px; "
        "font-size: 13px; font-weight: 600; border-left: 4px solid #4EC9B0;");
    m_statusLabel->setFixedHeight(36);
    m_statusLabel->setWordWrap(false);
    layout->addWidget(m_statusLabel);

    // Real Scintilla editor with syntax highlighting
    QFont mono = notepatraCodeFont();

    m_output = new QsciScintilla;
    m_output->setFont(mono);
    m_output->setMarginsFont(mono);
    m_output->setUtf8(true);
    m_output->setMarginType(0, QsciScintilla::NumberMargin);
    m_output->setMarginWidth(0, "00000");
    m_output->setMarginLineNumbers(0, true);
    m_output->setFolding(QsciScintilla::BoxedTreeFoldStyle, 2);
    m_output->setBraceMatching(QsciScintilla::StrictBraceMatch);
    m_output->setMatchedBraceBackgroundColor(QColor("#FFCCCC"));
    m_output->setMatchedBraceForegroundColor(QColor("#CC0000"));
    m_output->setCaretLineVisible(true);
    m_output->setCaretLineBackgroundColor(QColor("#E8F5E9"));
    m_output->setPaper(QColor("#FFFFFF"));

    layout->addWidget(m_output, 1);

    // Session log — every action taken on this panel during the session is
    // appended here so users can see "what action was taken and what size
    // was fixed for the session" at a glance. Compact, capped at 50 entries.
    auto *logHeader = new QLabel("  Session log");
    logHeader->setStyleSheet("background: #2D2D2D; color: #888; padding: 2px 6px; "
                             "font-size: 10px; font-weight: bold;");
    logHeader->setFixedHeight(18);
    layout->addWidget(logHeader);

    m_sessionLog = new QListWidget;
    m_sessionLog->setStyleSheet(
        QString("QListWidget { background: #1E1E1E; color: #D4D4D4; border: none; "
                "font-family: %1; font-size: 11px; padding: 2px; }"
        "QListWidget::item { padding: 2px 8px; border-bottom: 1px solid #2D2D2D; }"
        "QListWidget::item:hover { background: #2D3D3D; }")
        .arg(notepatraCodeCssFamily()));
    m_sessionLog->setFixedHeight(80);  // ~4 visible rows, scrollable
    layout->addWidget(m_sessionLog);

    applyLexer();
}

void FormatterPanel::applyLexer() {
    QsciLexer *lexer = nullptr;
    QFont mono = notepatraCodeFont();

    if (m_language == "JSON") lexer = new QsciLexerJSON(m_output);
    else if (m_language == "JavaScript") lexer = new QsciLexerJavaScript(m_output);
    else if (m_language == "HTML") lexer = new QsciLexerHTML(m_output);
    else if (m_language == "SQL") lexer = new QsciLexerSQL(m_output);
    else if (m_language == "Python") lexer = new QsciLexerPython(m_output);
    else if (m_language == "Bash") lexer = new QsciLexerBash(m_output);
    else if (m_language == "C++") lexer = new QsciLexerCPP(m_output);
    else if (m_language == "CSS") lexer = new QsciLexerCSS(m_output);
    else if (m_language == "XML") lexer = new QsciLexerXML(m_output);
    else if (m_language == "YAML") lexer = new QsciLexerYAML(m_output);

    if (lexer) {
        lexer->setDefaultFont(mono);
        lexer->setDefaultPaper(QColor("#FFFFFF"));
        lexer->setDefaultColor(QColor("#000000"));
        m_output->setLexer(lexer);
        // Apply Notepad++ default palette so user-typed text is visible (was
        // rendering white-on-white because the lexer's default per-style
        // colors weren't initialized — same root cause as the Windows v0.1.1
        // editor bug fixed in v0.1.2).
        applyNotepadPlusPalette(lexer, mono);
    }
    // Belt-and-braces: explicitly set the editor's own paper + foreground
    // so even text typed BEFORE the lexer kicks in (whitespace, plain text)
    // is visible.
    m_output->setPaper(QColor("#FFFFFF"));
    m_output->setColor(QColor("#000000"));
    m_output->setCaretForegroundColor(QColor("#000000"));
}

QString FormatterPanel::inputText() const {
    // Prefer the editable Scintilla content (so users can paste directly into
    // the panel and click a button). Fall back to the seeded m_inputText that
    // was passed in from the editor when the panel was opened.
    QString panelText = m_output ? m_output->text() : QString();
    if (!panelText.isEmpty()) return panelText;
    return m_inputText;
}

void FormatterPanel::setInput(const QString &text) {
    m_inputText = text;
    if (!text.isEmpty()) {
        // Show the user's ORIGINAL text untouched. Do NOT auto-Format on
        // open — for broken JSON, Rust's format_json falls through to
        // manual_pretty_print which strips all whitespace and rebuilds
        // using only `{`, `}`, `,` as delimiters. Broken JSON with missing
        // commas (the most common kind of broken JSON) gets COLLAPSED into
        // a single line, which then makes Show Diff useless because both
        // sides have completely different line structures.
        //
        // Just show the original text. The user clicks Format, Fix +
        // Format, or AI Fix manually when they want.
        m_output->setText(text);
        setStatus(QString("Loaded %1 chars, %2 lines — click a button to transform")
                  .arg(text.length()).arg(text.count('\n') + 1), false);
    }
}

void FormatterPanel::addButton(const QString &label, std::function<QString(const QString &)> fn) {
    auto *btn = new QPushButton(label);
    btn->setFixedHeight(26);
    m_btnRow->insertWidget(m_btnRow->count() - 2, btn);

    if (!m_hasFirstAction) {
        m_firstAction = fn;
        m_hasFirstAction = true;
    }

    connect(btn, &QPushButton::clicked, this, [this, fn, label]() {
        QString input = inputText();
        if (input.isEmpty()) {
            setStatus("Empty input — paste content into the panel below first", true);
            return;
        }
        setStatus(QString("Running %1 on %2 chars...").arg(label).arg(input.length()), false);

        // Catch any exception from the formatter (Rust panic, std::exception)
        // and surface it instead of letting it crash the app or fail silently.
        QString output;
        try {
            output = fn(input);
        } catch (const std::exception &e) {
            setStatus(QString("✗ %1 failed: %2").arg(label).arg(e.what()), true);
            return;
        } catch (...) {
            setStatus(QString("✗ %1 failed (unknown error)").arg(label), true);
            return;
        }

        if (output.isEmpty()) {
            setStatus(QString("✗ %1 returned empty output").arg(label), true);
            return;
        }

        int beforeLen = input.length();
        // Record this transformation for the Show Diff button — capture
        // BEFORE we mutate m_inputText below.
        recordFix(input, output, label);
        m_lastOutput = output;
        m_output->setText(output);
        // Cache the result so the next button operates on the latest output
        m_inputText = output;
        // Compute a short description of what actually changed
        QString desc = describeChanges(input, output);
        setStatus(QString("✓ %1 done — %2 chars, %3 lines (%4). Click Show Diff to see changes.")
                  .arg(label).arg(output.length()).arg(output.count('\n') + 1).arg(desc), false);
        // Log to session history with the change description
        logAction(label, beforeLen, output.length(), desc);
    });
}

QString FormatterPanel::describeChanges(const QString &before, const QString &after) {
    // Count specific characters in both strings, report deltas. Quick &
    // dirty but accurate enough for the session log — the user can click
    // Show Diff for the exact line-by-line view.
    auto count = [](const QString &s, QChar c) {
        int n = 0;
        for (QChar ch : s) if (ch == c) n++;
        return n;
    };
    int dCommas       = count(after, ',') - count(before, ',');
    int dOpenBrace    = count(after, '{') - count(before, '{');
    int dCloseBrace   = count(after, '}') - count(before, '}');
    int dOpenBracket  = count(after, '[') - count(before, '[');
    int dCloseBracket = count(after, ']') - count(before, ']');
    int dDoubleQuotes = count(after, '"') - count(before, '"');
    int dSingleQuotes = count(after, '\'') - count(before, '\'');
    int dColons       = count(after, ':') - count(before, ':');
    int dLines        = (after.count('\n') + 1) - (before.count('\n') + 1);

    QStringList parts;
    auto add = [&](int delta, const QString &singular, const QString &plural) {
        if (delta == 0) return;
        QString sign = delta > 0 ? "+" : "−";
        int abs = delta > 0 ? delta : -delta;
        QString noun = (abs == 1) ? singular : plural;
        parts << QString("%1%2 %3").arg(sign).arg(abs).arg(noun);
    };

    add(dCommas, "comma", "commas");
    int dBraces = dOpenBrace + dCloseBrace;
    add(dBraces, "brace", "braces");
    int dBrackets = dOpenBracket + dCloseBracket;
    add(dBrackets, "bracket", "brackets");
    add(dDoubleQuotes, "quote", "quotes");
    if (dSingleQuotes < 0) {
        // Single quotes removed → likely converted to double quotes
        parts << QString("−%1 single→double").arg(-dSingleQuotes);
    } else if (dSingleQuotes > 0) {
        parts << QString("+%1 single quote%2").arg(dSingleQuotes).arg(dSingleQuotes == 1 ? "" : "s");
    }
    add(dColons, "colon", "colons");

    if (parts.isEmpty()) {
        // No char-class delta. Maybe just whitespace or content reordering.
        if (dLines != 0) {
            return QString("%1%2 lines").arg(dLines > 0 ? "+" : "−").arg(qAbs(dLines));
        }
        return "no structural changes";
    }
    return parts.join(", ");
}

void FormatterPanel::recordFix(const QString &before, const QString &after,
                                const QString &actionName) {
    // Skip ONLY if input was completely empty — otherwise enable Show Diff
    // even when before == after (user might still want to see "no diff").
    if (before.isEmpty()) return;
    m_lastFixInput = before;
    m_lastFixOutput = after;
    m_lastFixActionName = actionName;
    if (m_diffBtn) m_diffBtn->setEnabled(true);
}

void FormatterPanel::setOutput(const QString &text) {
    m_lastOutput = text;
    m_output->setText(text);
}

void FormatterPanel::appendOutput(const QString &text) {
    m_lastOutput += text;
    m_output->append(text);
}

void FormatterPanel::setStatus(const QString &text, bool isError) {
    if (!m_statusLabel) return;
    m_statusLabel->setText(text);
    // Preserve the BIG dark banner styling — only swap the color/border
    // accent for error vs success. Was previously stripping the dark
    // background, leaving the colored text on a white default background
    // (invisible). Match the constructor's initial style.
    QString accent = isError ? "#F48771" : "#4EC9B0";
    QString bg     = isError ? "#3a1e1e" : "#1e3a3a";
    m_statusLabel->setStyleSheet(
        QString("background: %1; color: %2; padding: 8px 12px; "
                "font-size: 13px; font-weight: 600; border-left: 4px solid %2;")
        .arg(bg).arg(accent));
}

void FormatterPanel::logAction(const QString &action, int beforeChars, int afterChars,
                               const QString &extra) {
    if (!m_sessionLog) return;
    int delta = afterChars - beforeChars;
    QString deltaStr = (delta == 0)
        ? "  ±0"
        : (delta > 0 ? QString("+%1").arg(delta) : QString::number(delta));
    QString timestamp = QDateTime::currentDateTime().toString("HH:mm:ss");
    QString line = QString("[%1] %2: %3 → %4 chars (%5)")
        .arg(timestamp).arg(action).arg(beforeChars).arg(afterChars).arg(deltaStr);
    if (!extra.isEmpty()) line += "  " + extra;
    auto *item = new QListWidgetItem(line);
    // Color the entry based on whether it changed anything
    if (delta > 0) item->setForeground(QColor("#4EC9B0"));      // green = fixed/added
    else if (delta < 0) item->setForeground(QColor("#FFB000")); // amber = minified/shrunk
    else item->setForeground(QColor("#888888"));                 // gray = no-op
    m_sessionLog->addItem(item);
    m_sessionLog->scrollToBottom();
    // Cap history at 50 entries
    while (m_sessionLog->count() > 50) {
        delete m_sessionLog->takeItem(0);
    }
}
