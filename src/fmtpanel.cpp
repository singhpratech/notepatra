#include "fmtpanel.h"
#include "npp_palette.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QFont>
#include <QApplication>
#include <QClipboard>

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
    QFont mono("Consolas", 10);
    mono.setStyleHint(QFont::Monospace);

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

    applyLexer();
}

void FormatterPanel::applyLexer() {
    QsciLexer *lexer = nullptr;
    QFont mono("Consolas", 10);
    mono.setStyleHint(QFont::Monospace);

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
        // Seed the editable panel so the user sees their input immediately
        m_output->setText(text);
        if (m_hasFirstAction) {
            m_lastOutput = m_firstAction(text);
            m_output->setText(m_lastOutput);
            setStatus(QString("✓ %1 chars formatted on open").arg(m_lastOutput.length()), false);
        } else {
            setStatus(QString("Loaded %1 chars from editor").arg(text.length()), false);
        }
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

        m_lastOutput = output;
        m_output->setText(output);
        // Cache the result so the next button operates on the latest output
        m_inputText = output;
        setStatus(QString("✓ %1 done — %2 chars, %3 lines")
                  .arg(label).arg(output.length()).arg(output.count('\n') + 1), false);
    });
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
    m_statusLabel->setStyleSheet(
        QString("padding: 2px 8px; font-size: 11px; color: %1;")
        .arg(isError ? "#F48771" : "#4EC9B0"));
}
