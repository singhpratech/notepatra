#include "fmtpanel.h"
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
        if (!text.isEmpty()) QApplication::clipboard()->setText(text);
    });

    m_btnRow->addStretch();
    m_btnRow->addWidget(copyBtn);
    layout->addWidget(btnWidget);

    // Real Scintilla editor with syntax highlighting
    QFont mono("Consolas", 11);
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
    QFont mono("Consolas", 11);
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
        m_output->setLexer(lexer);
    }
    m_output->setPaper(QColor("#FFFFFF"));
}

void FormatterPanel::setInput(const QString &text) {
    m_inputText = text;
    if (m_hasFirstAction && !m_inputText.isEmpty()) {
        m_lastOutput = m_firstAction(m_inputText);
        m_output->setText(m_lastOutput);
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

    connect(btn, &QPushButton::clicked, this, [this, fn]() {
        if (m_inputText.isEmpty()) return;
        m_lastOutput = fn(m_inputText);
        m_output->setText(m_lastOutput);
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
