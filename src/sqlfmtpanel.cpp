// SPDX-License-Identifier: GPL-3.0-or-later

#include "sqlfmtpanel.h"
#include "editor_symbols.h"
#include "rustbridge.h"
#include "npp_palette.h"
#include "fonts.h"
#include "theme_detect.h"
#include "config.h"
#include "ollama.h"
#include "ollamastatus.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QSpinBox>
#include <QFont>
#include <QApplication>
#include <QClipboard>
#include <QRegularExpression>
#include <cstdlib>
#include <Qsci/qscilexersql.h>

SqlFmtPanel::SqlFmtPanel(QWidget *parent) : QWidget(parent) {
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);

    m_header = new QLabel("  SQL Formatter");
    // 24 px was tight on Windows where bold-font ascenders+descenders
    // (~18 px) + padding clipped against the next row. 28 gives room.
    m_header->setMinimumHeight(28);
    layout->addWidget(m_header);

    auto *optRow = new QHBoxLayout;
    // Right margin bumped 8→16 — the rightmost button (Copy Output) was
    // clipping against the panel edge on Windows.
    optRow->setContentsMargins(8, 6, 16, 6);
    // Explicit spacing — without it some Qt styles collapse inter-widget
    // gaps to 0 and the checkbox label runs into the "Indent:" label.
    optRow->setSpacing(10);

    m_dialectLabel = new QLabel("Dialect:");
    optRow->addWidget(m_dialectLabel);
    m_dialectCombo = new QComboBox;
    m_dialectCombo->addItems({"ANSI SQL", "T-SQL (SQL Server)", "PL/SQL (Oracle)",
                              "MySQL", "PostgreSQL", "SQLite"});
    optRow->addWidget(m_dialectCombo);

    m_uppercase = new QCheckBox("UPPERCASE keywords");
    m_uppercase->setChecked(true);
    optRow->addWidget(m_uppercase);
    m_indentLabel = new QLabel("Indent:");
    optRow->addWidget(m_indentLabel);
    m_indent = new QSpinBox;
    m_indent->setRange(1, 8);
    m_indent->setValue(4);
    optRow->addWidget(m_indent);

    m_fmtBtn = new QPushButton("Format");
    m_fmtBtn->setFixedHeight(26);
    m_fmtBtn->setMinimumWidth(m_fmtBtn->fontMetrics().horizontalAdvance(m_fmtBtn->text()) + 28);
    m_fmtBtn->setToolTip(
        "Claude-style expanded formatting — one column per line, JOINs aligned, "
        "WHERE predicates stacked, CASE/WHEN expanded.");
    optRow->addWidget(m_fmtBtn);

    // v0.1.49 — Compact / one-line-where-possible variant. Same parser +
    // dialect support as Format, but keeps short statements on a single
    // line and only breaks at major clause boundaries when the query is
    // long. Useful for pasting many short queries in a row.
    m_compactBtn = new QPushButton("Compact");
    m_compactBtn->setFixedHeight(26);
    m_compactBtn->setMinimumWidth(m_compactBtn->fontMetrics().horizontalAdvance(m_compactBtn->text()) + 28);
    m_compactBtn->setToolTip(
        "Compact / one-line-where-possible. Short queries stay on a single line; "
        "long ones break only at major clauses (SELECT / FROM / WHERE / GROUP BY).");
    optRow->addWidget(m_compactBtn);

    // AI Fix (Ollama) — patches syntax errors with a local LLM, then
    // re-runs the Claude-style formatter so the output stays beautiful.
    m_aiBtn = new QPushButton("AI Fix (Ollama)");
    m_aiBtn->setFixedHeight(26);
    m_aiBtn->setMinimumWidth(m_aiBtn->fontMetrics().horizontalAdvance(m_aiBtn->text()) + 28);
    m_aiBtn->setToolTip("Ask a local LLM to fix SQL syntax errors (preserves intent).");
    optRow->addWidget(m_aiBtn);

    optRow->addStretch();

    m_copyBtn = new QPushButton("Copy Output");
    m_copyBtn->setFixedHeight(26);
    m_copyBtn->setMinimumWidth(m_copyBtn->fontMetrics().horizontalAdvance(m_copyBtn->text()) + 28);
    optRow->addWidget(m_copyBtn);
    layout->addLayout(optRow);

    m_ollamaBar = new OllamaStatus(this);
    layout->addWidget(m_ollamaBar);

    m_ollama = new OllamaClient(this);
    connect(m_ollama, &OllamaClient::tokenReceived, this, &SqlFmtPanel::onAiToken);
    connect(m_ollama, &OllamaClient::finished,      this, &SqlFmtPanel::onAiFinished);
    connect(m_ollama, &OllamaClient::error,         this, &SqlFmtPanel::onAiError);

    m_statusLabel = new QLabel("💡 Paste SQL into the panel below, choose dialect, click Format");
    m_statusLabel->setFixedHeight(36);
    layout->addWidget(m_statusLabel);

    QFont mono = notepatraCodeFont();

    m_output = new QsciScintilla;
    m_output->setFont(mono);
    m_output->setMarginsFont(mono);
    m_output->setUtf8(true);
    // Formatter output is a plain QsciScintilla, so it never runs
    // Editor::applySymbolSettings(); apply Show Symbol explicitly.
    EditorSymbols::applyFromConfig(m_output);
    m_output->setMarginType(0, QsciScintilla::NumberMargin);
    m_output->setMarginWidth(0, "00000");
    m_output->setMarginLineNumbers(0, true);
    m_output->setFolding(QsciScintilla::BoxedTreeFoldStyle, 2);
    m_output->setCaretLineVisible(true);

    auto *lexer = new QsciLexerSQL(m_output);
    lexer->setDefaultFont(mono);
    m_output->setLexer(lexer);

    applySqlDialectKeywords();
    layout->addWidget(m_output, 1);

    // Paint everything from a fresh palette — also re-paints on onThemeChanged.
    applyPalette();

    connect(m_fmtBtn, &QPushButton::clicked, this, &SqlFmtPanel::doFormat);
    connect(m_compactBtn, &QPushButton::clicked, this, &SqlFmtPanel::doCompactFormat);
    connect(m_aiBtn, &QPushButton::clicked, this, &SqlFmtPanel::doAiFix);
    connect(m_dialectCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, [this](int) {
        applySqlDialectKeywords();
        setStatus(QString("Dialect: %1 — keyword set updated").arg(m_dialectCombo->currentText()), false);
    });
    connect(m_copyBtn, &QPushButton::clicked, this, [this]() {
        QString text = m_output->text();
        if (text.isEmpty()) {
            setStatus("Nothing to copy — panel is empty", true);
            return;
        }
        QApplication::clipboard()->setText(text);
        setStatus(QString("✓ Copied %1 chars to clipboard").arg(text.length()), false);
    });
}

// ─── Theme ───────────────────────────────────────────────────────────
// Re-read npPalette() and re-apply every stylesheet + re-paint the Scintilla
// lexer with the correct theme name. Called from the constructor and whenever
// MainWindow::themeChanged() fires.
void SqlFmtPanel::applyPalette() {
    const NpPalette pal = npPalette();
    // Use the *resolved* theme name (Light/Dark/Monokai), not the raw
    // Config::theme — otherwise "System" falls through every "is dark"
    // check in applyNotepadPlusPalette and the editor paints with a
    // white paper on top of dark chrome.
    const QString themeName = npResolvedThemeName();

    // Whole-panel base styling — cascades to labels, comboboxes, spinboxes,
    // buttons that don't have explicit inline styles.
    setStyleSheet(QString(
        "QWidget { background: %1; color: %2; }"
        "QLabel { background: transparent; color: %2; }"
        // QSpinBox deliberately NOT listed here. A QSS rule on a spin box
        // hands its up/down buttons to the stylesheet engine, which draws
        // nothing without ::up-button/::down-button images — the Indent
        // spinner shipped with no arrows. It is themed by palette below
        // instead, so the platform style keeps drawing its own.
        "QComboBox { background: %3; color: %4; border: 1px solid %5;"
        "            border-radius: 4px; padding: 2px 6px; min-height: 22px; }"
        "QComboBox:focus { border: 1px solid %6; }"
        "QComboBox QAbstractItemView { background: %3; color: %4; selection-background-color: %7; selection-color: %8; }"
        "QCheckBox { color: %2; spacing: 6px; }"
        "QCheckBox::indicator { width: 14px; height: 14px; border: 1px solid %5; background: %3; border-radius: 3px; }"
        "QCheckBox::indicator:checked { background: %6; border-color: %6; }"
        "QPushButton { background: %9; color: %10; border: 1px solid %11; border-radius: 4px; padding: 3px 10px; }"
        "QPushButton:hover { background: %12; }"
        "QPushButton:disabled { color: %13; }"
    ).arg(pal.bg, pal.text,
          pal.inputBg, pal.inputFg, pal.inputBorder, pal.inputFocus,
          pal.selectionBg, pal.selectionFg,
          pal.btnBg, pal.btnFg, pal.btnBorder, pal.btnHover, pal.textMuted));

    // Spin boxes are themed by palette, never by stylesheet — see the note
    // in the block above. This keeps the native arrows on every platform.
    for (QSpinBox *sb : findChildren<QSpinBox *>()) {
        QPalette sp = sb->palette();
        sp.setColor(QPalette::Base,       QColor(pal.inputBg));
        sp.setColor(QPalette::Text,       QColor(pal.inputFg));
        sp.setColor(QPalette::Button,     QColor(pal.inputBg));
        sp.setColor(QPalette::ButtonText, QColor(pal.inputFg));
        sp.setColor(QPalette::Window,     QColor(pal.bg));
        sp.setColor(QPalette::WindowText, QColor(pal.text));
        sp.setColor(QPalette::Mid,        QColor(pal.inputBorder));
        sp.setColor(QPalette::Dark,       QColor(pal.inputBorder));
        sb->setPalette(sp);
    }

    if (m_header) {
        // padding 4px top/bottom (was 2px) so Windows bold-font descenders
        // don't clip against the row below.
        m_header->setStyleSheet(QString(
            "font-weight: bold; background: %1; color: %2; padding: 4px 6px;"
        ).arg(pal.chromeBg, pal.accent));
    }

    // Re-paint the status label so its border + text track the current theme.
    if (m_statusLabel) {
        QString accent = m_isStatusError ? pal.errorFg : pal.accent;
        m_statusLabel->setStyleSheet(QString(
            "background: %1; color: %2; padding: 8px 12px; "
            "font-size: 13px; font-weight: 600; border-left: 4px solid %2;"
        ).arg(pal.chromeBg, accent));
    }

    // Scintilla editor — paper + text + caret + caret-line bg + ALL margins.
    // v0.1.34 fix: pre-v0.1.34 we missed setMarginsBackgroundColor /
    // ForegroundColor / setFoldMarginColors entirely on this widget. The
    // line-number column AND the fold margin (created by setFolding(...) in
    // setupOutput) both stayed at QScintilla's default WHITE. On Dark
    // theme the user saw a glaring white strip between the line numbers
    // and the SQL content (reported via Windows screenshot, /home/.../
    // issuesinwindows/white sidepale for sl formatter.png). Now we paint
    // every margin to match the rest of the theme.
    if (m_output) {
        m_output->setCaretLineBackgroundColor(QColor(pal.chromeBg));
        m_output->setCaretForegroundColor(QColor(pal.text));
        m_output->setPaper(QColor(pal.cardBg));
        m_output->setColor(QColor(pal.text));
        m_output->setMarginsBackgroundColor(QColor(pal.bg));
        m_output->setMarginsForegroundColor(QColor(pal.textMuted));
        m_output->setFoldMarginColors(QColor(pal.bg), QColor(pal.bg));

        if (auto *lexer = qobject_cast<QsciLexerSQL *>(m_output->lexer())) {
            lexer->setDefaultPaper(QColor(pal.cardBg));
            lexer->setDefaultColor(QColor(pal.text));
            // Pass the current theme name so the lexer paints dark-mode
            // colours when the app is in Dark/Monokai. Without this,
            // applyNotepadPlusPalette defaults to LIGHT-MODE colours
            // (white paper + black text) regardless of the panel chrome.
            applyNotepadPlusPalette(lexer, notepatraCodeFont(), themeName);
            // Reinstate dialect keywords — recolor covers the visible buffer.
            applySqlDialectKeywords();
            if (int len = m_output->length()) m_output->recolor(0, len);
        }
    }
}

void SqlFmtPanel::onThemeChanged() {
    applyPalette();
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
        formatted = RustCore::formatSql(input, m_indent->value(),
                                        m_uppercase->isChecked(),
                                        m_dialectCombo->currentText());
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

// v0.1.49 — Compact one-line-where-possible formatter. Same parser path
// as doFormat; only the line-break policy differs (Rust handles it).
void SqlFmtPanel::doCompactFormat() {
    QString input = m_output->text();
    if (input.isEmpty()) input = m_inputText;
    if (input.isEmpty()) {
        setStatus("Empty input — paste SQL into the panel below first", true);
        return;
    }

    setStatus(QString("Compacting %1 chars (%2)...")
              .arg(input.length()).arg(m_dialectCombo->currentText()), false);

    QString out;
    try {
        out = RustCore::formatSqlCompact(input, m_indent->value(),
                                         m_uppercase->isChecked(),
                                         m_dialectCombo->currentText());
    } catch (const std::exception &e) {
        setStatus(QString("✗ Compact failed: %1").arg(e.what()), true);
        return;
    } catch (...) {
        setStatus("✗ Compact failed (unknown error)", true);
        return;
    }

    if (out.isEmpty()) {
        setStatus("✗ Compact formatter returned empty output", true);
        return;
    }
    m_output->setText(out);
    m_inputText = out;
    setStatus(QString("✓ Compact — %1 chars, %2 lines")
              .arg(out.length()).arg(out.count('\n') + 1), false);
}

void SqlFmtPanel::setStatus(const QString &text, bool isError) {
    if (!m_statusLabel) return;
    m_statusLabel->setText(text);
    const NpPalette pal = npPalette();
    QString accent = isError ? pal.errorFg : pal.accent;
    QString bg     = pal.chromeBg;
    m_statusLabel->setStyleSheet(
        QString("background: %1; color: %2; padding: 8px 12px; "
                "font-size: 13px; font-weight: 600; border-left: 4px solid %2;")
        .arg(bg, accent));
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

// ─── AI Fix (Ollama) ─────────────────────────────────────────────────
// Send the current SQL to a local LLM with a strict "fix syntax only"
// system prompt, stream the response back, strip any markdown / <think>
// scaffolding the model emits, then re-run the Claude-style formatter so
// the output stays beautiful regardless of what the model returned.
void SqlFmtPanel::doAiFix() {
    QString input = m_output->text();
    if (input.isEmpty()) input = m_inputText;
    if (input.trimmed().isEmpty()) {
        setStatus("Empty input — paste SQL into the panel below first", true);
        return;
    }

    // v0.1.48 — use the OllamaStatus widget's cached probe instead of
    // calling m_ollama->isAvailable() directly. The OllamaClient version
    // is synchronous with a 3-second QEventLoop spin, which froze the UI
    // and made the AI Assistant feel "locked" while SQL Formatter was
    // checking. OllamaStatus polls in the background and just returns a
    // cached bool — no blocking.
    if (!m_ollamaBar->isAvailable()) {
        setStatus("Ollama not running — start it: ollama serve", true);
        m_output->setText(
            "-- Ollama is not running.\n"
            "-- Setup:\n"
            "--   1. Install:  curl -fsSL https://ollama.com/install.sh | sh\n"
            "--   2. Pull:     ollama pull qwen2.5-coder:7b\n"
            "--   3. Start:    ollama serve\n"
            "--   4. Click AI Fix again\n");
        return;
    }

    QString model = m_ollamaBar->selectedModel();
    if (model.isEmpty() || model.startsWith("(")) {
        m_ollamaBar->checkStatus();
        model = m_ollamaBar->selectedModel();
        if (model.isEmpty() || model.startsWith("(")) {
            setStatus("No models installed — run: ollama pull qwen2.5-coder:7b", true);
            return;
        }
    }

    const QString dialect = m_dialectCombo->currentText();
    m_aiOriginalInput = input;
    m_aiStreamBuffer.clear();
    m_aiTimer.restart();
    m_ollama->setModel(model);

    QString systemPrompt = QString(
        "You are a minimal-change SQL syntax patcher for %1. "
        "Fix syntax errors only. Do NOT rewrite, reorder, or change semantics. "
        "Output ONLY valid %1 SQL — no prose, no markdown fences."
    ).arg(dialect);

    QString userPrompt = QString(
        "Dialect: %1\n"
        "Fix ONLY syntax errors in the following SQL. "
        "Preserve column order, table names, aliases, and query intent exactly. "
        "Return ONLY the corrected SQL statement(s) — no explanation, no fences.\n\n"
        "SQL:\n%2"
    ).arg(dialect, input);

    setStatus(QString("Calling Ollama (%1)…").arg(model), false);
    m_aiBtn->setEnabled(false);
    m_aiBtn->setText("AI Fixing…");

    m_ollama->generate(userPrompt, systemPrompt, /*enableThinking=*/false);
}

void SqlFmtPanel::onAiToken(const QString &token) {
    // Accumulate locally — don't paint partial tokens into the editor, they
    // usually contain scratch text the model retracts (e.g. opening of a
    // ``` fence). We render the cleaned result in onAiFinished().
    m_aiStreamBuffer += token;
    m_aiBtn->setText(QString("AI Fixing… (%1 chars)").arg(m_aiStreamBuffer.length()));
}

void SqlFmtPanel::onAiFinished(const QString &full) {
    m_aiBtn->setEnabled(true);
    m_aiBtn->setText("AI Fix (Ollama)");

    QString cleaned = cleanAiSqlResponse(full);
    if (cleaned.isEmpty()) {
        setStatus("AI fix returned empty output — try a different model", true);
        return;
    }

    // Re-run the Claude-style formatter so the AI output stays beautiful.
    QString formatted;
    try {
        formatted = RustCore::formatSql(cleaned, m_indent->value(),
                                        m_uppercase->isChecked(),
                                        m_dialectCombo->currentText());
    } catch (...) {
        formatted = cleaned;
    }
    if (formatted.isEmpty()) formatted = cleaned;

    int origLen = m_aiOriginalInput.length();
    int newLen  = formatted.length();
    int delta   = newLen - origLen;
    double secs = m_aiTimer.elapsed() / 1000.0;

    m_output->setText(formatted);
    m_inputText = formatted;

    setStatus(QString("Fixed in %1 s with %2 char%3 changed")
              .arg(secs, 0, 'f', 1)
              .arg(delta >= 0 ? QString("+%1").arg(delta) : QString::number(delta))
              .arg(std::abs(delta) == 1 ? "" : "s"),
              false);
}

void SqlFmtPanel::onAiError(const QString &msg) {
    m_aiBtn->setEnabled(true);
    m_aiBtn->setText("AI Fix (Ollama)");
    setStatus(QString("AI fix failed: %1").arg(msg), true);
}

// Cleanup logic mirrors mainwindow.cpp:2113-2131 (JSON AI Fix):
//   1. strip <think>…</think> blocks,
//   2. strip markdown ``` fences,
//   3. trim leading prose by finding the first SQL-ish token and dropping
//      everything before it.
QString SqlFmtPanel::cleanAiSqlResponse(const QString &raw) {
    QString cleaned = raw.trimmed();

    QRegularExpression thinkRe("<think>.*?</think>",
                               QRegularExpression::DotMatchesEverythingOption);
    cleaned.remove(thinkRe);
    cleaned = cleaned.trimmed();

    if (cleaned.startsWith("```")) {
        int f = cleaned.indexOf('\n');
        int l = cleaned.lastIndexOf("```");
        if (f > 0 && l > f) cleaned = cleaned.mid(f + 1, l - f - 1).trimmed();
    }

    static const QStringList openers = {
        "SELECT", "WITH", "INSERT", "UPDATE", "DELETE",
        "CREATE", "ALTER", "DROP", "TRUNCATE", "MERGE",
        "EXPLAIN", "SHOW", "--", "/*"
    };
    int bestIdx = -1;
    for (const QString &kw : openers) {
        int i = cleaned.indexOf(kw, 0, Qt::CaseInsensitive);
        if (i >= 0 && (bestIdx < 0 || i < bestIdx)) bestIdx = i;
    }
    if (bestIdx > 0) cleaned = cleaned.mid(bestIdx);

    return cleaned.trimmed();
}
