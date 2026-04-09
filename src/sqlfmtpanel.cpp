#include "sqlfmtpanel.h"
#include "rustbridge.h"
#include "npp_palette.h"
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

    optRow->addWidget(new QLabel("Dialect:"));
    m_dialectCombo = new QComboBox;
    m_dialectCombo->addItems({"ANSI SQL", "T-SQL (SQL Server)", "PL/SQL (Oracle)",
                              "MySQL", "PostgreSQL", "SQLite"});
    optRow->addWidget(m_dialectCombo);

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

    // BIG status banner — same style as FormatterPanel for consistency
    m_statusLabel = new QLabel("💡 Paste SQL into the panel below, choose dialect, click Format");
    m_statusLabel->setStyleSheet(
        "background: #1e3a3a; color: #569CD6; padding: 8px 12px; "
        "font-size: 13px; font-weight: 600; border-left: 4px solid #569CD6;");
    m_statusLabel->setFixedHeight(36);
    layout->addWidget(m_statusLabel);

    // Real Scintilla editor with SQL syntax highlighting
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
    m_output->setCaretLineVisible(true);
    m_output->setCaretLineBackgroundColor(QColor("#E8F5E9"));

    auto *lexer = new QsciLexerSQL(m_output);
    lexer->setDefaultFont(mono);
    lexer->setDefaultPaper(QColor("#FFFFFF"));
    lexer->setDefaultColor(QColor("#000000"));
    m_output->setLexer(lexer);
    // Apply Notepad++ palette so user-typed text isn't invisible
    applyNotepadPlusPalette(lexer, mono);
    // Belt-and-braces fallback colors
    m_output->setPaper(QColor("#FFFFFF"));
    m_output->setColor(QColor("#000000"));
    m_output->setCaretForegroundColor(QColor("#000000"));

    // Apply dialect keywords now and on dialect change
    applySqlDialectKeywords();

    layout->addWidget(m_output, 1);

    connect(fmtBtn, &QPushButton::clicked, this, &SqlFmtPanel::doFormat);
    connect(m_dialectCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, [this](int) {
        applySqlDialectKeywords();
        setStatus(QString("Dialect: %1 — keyword set updated").arg(m_dialectCombo->currentText()), false);
    });
    connect(copyBtn, &QPushButton::clicked, this, [this]() {
        QString text = m_output->text();
        if (text.isEmpty()) {
            setStatus("Nothing to copy — panel is empty", true);
            return;
        }
        QApplication::clipboard()->setText(text);
        setStatus(QString("✓ Copied %1 chars to clipboard").arg(text.length()), false);
    });
}

void SqlFmtPanel::setInput(const QString &sql) {
    m_inputText = sql;
    if (!sql.isEmpty()) {
        m_output->setText(sql);
        doFormat();
    }
}

void SqlFmtPanel::doFormat() {
    // Read from the editable panel content first, fall back to seeded input
    QString input = m_output->text();
    if (input.isEmpty()) input = m_inputText;
    if (input.isEmpty()) {
        setStatus("Empty input — paste SQL into the panel below first", true);
        return;
    }

    setStatus(QString("Formatting %1 chars (%2)...")
              .arg(input.length()).arg(m_dialectCombo->currentText()), false);

    QString formatted;
    try {
        formatted = RustCore::formatSql(input, m_indent->value(), m_uppercase->isChecked());
    } catch (const std::exception &e) {
        setStatus(QString("✗ Format failed: %1").arg(e.what()), true);
        return;
    } catch (...) {
        setStatus("✗ Format failed (unknown error)", true);
        return;
    }

    if (formatted.isEmpty()) {
        setStatus("✗ Formatter returned empty output", true);
        return;
    }
    m_output->setText(formatted);
    m_inputText = formatted;
    setStatus(QString("✓ Formatted — %1 chars, %2 lines")
              .arg(formatted.length()).arg(formatted.count('\n') + 1), false);
}

void SqlFmtPanel::setStatus(const QString &text, bool isError) {
    if (!m_statusLabel) return;
    m_statusLabel->setText(text);
    QString color = isError ? "#F48771" : "#569CD6";
    m_statusLabel->setStyleSheet(
        QString("background: #1e3a3a; color: %1; padding: 8px 12px; "
                "font-size: 13px; font-weight: 600; border-left: 4px solid %1;")
        .arg(color));
}

// ─── Per-dialect SQL keyword sets ──────────────────────────────────────
// QsciLexerSQL has up to 8 keyword sets. We populate set 0 with a UNION
// of ANSI SQL + the chosen dialect's specific keywords so all of them
// paint blue when the user types or pastes them.
void SqlFmtPanel::applySqlDialectKeywords() {
    auto *lexer = qobject_cast<QsciLexerSQL *>(m_output->lexer());
    if (!lexer) return;

    // ANSI SQL — common to every dialect
    static const QByteArray ANSI =
        "select from where group by order having insert update delete into "
        "values set create drop alter table view index trigger procedure "
        "function as join inner left right outer full on cross union all "
        "distinct case when then else end if exists between like in is null "
        "not and or true false primary key foreign references constraint "
        "default check unique cascade restrict with begin commit rollback "
        "transaction grant revoke schema database column row count sum avg "
        "min max int integer varchar char text date time timestamp boolean "
        "decimal numeric float double precision blob clob bigint smallint";

    // T-SQL (SQL Server) extras
    static const QByteArray TSQL =
        " declare top output merge for system_time go nvarchar nchar "
        "datetime datetime2 datetimeoffset uniqueidentifier sysname "
        "identity rowversion percent into try catch throw raiserror "
        "openquery openrowset openxml waitfor offset fetch next rows only "
        "pivot unpivot apply outer cross over partition rank dense_rank "
        "row_number cte recursive";

    // PL/SQL (Oracle) extras
    static const QByteArray PLSQL =
        " number varchar2 nvarchar2 clob blob date timestamp interval "
        "rowid urowid pls_integer binary_integer boolean dbms_output "
        "package body record cursor refcursor exception raise pragma "
        "exit loop while for forall bulk collect rownum dual sysdate "
        "sysdate nvl nvl2 decode connect by start with prior level "
        "minus intersect rownum nextval currval ";

    // MySQL extras
    static const QByteArray MYSQL =
        " auto_increment engine innodb myisam unsigned zerofill enum "
        "tinyint mediumint year datetime tinytext mediumtext longtext "
        "tinyblob mediumblob longblob set show describe explain limit "
        "offset replace ignore delayed straight_join high_priority "
        "low_priority on duplicate key collate charset utf8mb4 ";

    // PostgreSQL extras
    static const QByteArray PGSQL =
        " serial bigserial smallserial bytea jsonb json uuid inet cidr "
        "macaddr point line lseg box path polygon circle interval "
        "returning conflict do nothing using language plpgsql trigger "
        "stored procedure refresh materialized view extension role "
        "tablespace ilike similar overlaps array unnest generate_series "
        "lateral window over partition cte recursive ";

    // SQLite extras
    static const QByteArray SQLITE =
        " autoincrement without rowid attach detach pragma vacuum "
        "rowid integer text real blob numeric collate nocase rtrim "
        "binary glob match conflict abort fail ignore replace ";

    QByteArray keywords = ANSI;
    QString dialect = m_dialectCombo->currentText();
    if (dialect.contains("T-SQL"))      keywords += TSQL;
    else if (dialect.contains("PL/SQL")) keywords += PLSQL;
    else if (dialect == "MySQL")        keywords += MYSQL;
    else if (dialect == "PostgreSQL")   keywords += PGSQL;
    else if (dialect == "SQLite")       keywords += SQLITE;

    // QsciLexerSQL::setKeywords is protected — go straight to Scintilla
    // via SCI_SETKEYWORDS, which works for any lexer.
    //
    // Cast wParam to uintptr_t (not unsigned long) so MSVC unambiguously
    // picks the SendScintilla(uint, uintptr_t, const char*) overload.
    // On MSVC x64, unsigned long is 32-bit and uintptr_t is 64-bit, so a
    // bare 0 + const char* is ambiguous between (unsigned long, void*) and
    // (uintptr_t, const char*). Casting to uintptr_t makes the latter exact.
    m_output->SendScintilla(QsciScintilla::SCI_SETKEYWORDS,
                            (uintptr_t)0,
                            keywords.constData());
    // Re-colourise the visible buffer so the new keywords paint immediately
    int len = m_output->length();
    if (len > 0) m_output->recolor(0, len);
}
