#include "sqlfmtpanel.h"
#include "rustbridge.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QFont>
#include <QApplication>
#include <QClipboard>
#include <Qsci/qscilexersql.h>

SqlFmtPanel::SqlFmtPanel(QWidget *parent) : QWidget(parent) {
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);

    auto *header = new QLabel("  SQL Formatter");
    header->setFixedHeight(24);
    header->setStyleSheet("font-weight: bold; background: #2D2D2D; color: #569CD6; padding: 2px 6px;");
    layout->addWidget(header);

    auto *optRow = new QHBoxLayout;
    optRow->setContentsMargins(4, 4, 4, 4);
    m_uppercase = new QCheckBox("UPPERCASE keywords");
    m_uppercase->setChecked(true);
    optRow->addWidget(m_uppercase);
    optRow->addWidget(new QLabel("Indent:"));
    m_indent = new QSpinBox;
    m_indent->setRange(1, 8);
    m_indent->setValue(4);
    optRow->addWidget(m_indent);

    auto *fmtBtn = new QPushButton("Format");
    fmtBtn->setFixedHeight(26);
    optRow->addWidget(fmtBtn);
    optRow->addStretch();

    auto *copyBtn = new QPushButton("Copy Output");
    copyBtn->setFixedHeight(26);
    optRow->addWidget(copyBtn);
    layout->addLayout(optRow);

    // Real Scintilla editor with SQL syntax highlighting
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
    m_output->setCaretLineVisible(true);
    m_output->setCaretLineBackgroundColor(QColor("#E8F5E9"));

    auto *lexer = new QsciLexerSQL(m_output);
    lexer->setDefaultFont(mono);
    m_output->setLexer(lexer);
    m_output->setPaper(QColor("#FFFFFF"));

    layout->addWidget(m_output, 1);

    connect(fmtBtn, &QPushButton::clicked, this, &SqlFmtPanel::doFormat);
    connect(copyBtn, &QPushButton::clicked, this, [this]() {
        QString text = m_output->text();
        if (!text.isEmpty()) QApplication::clipboard()->setText(text);
    });
}

void SqlFmtPanel::setInput(const QString &sql) {
    m_inputText = sql;
    doFormat();
}

void SqlFmtPanel::doFormat() {
    if (m_inputText.isEmpty()) return;
    QString formatted = RustCore::formatSql(m_inputText, m_indent->value(), m_uppercase->isChecked());
    m_output->setText(formatted);
}
