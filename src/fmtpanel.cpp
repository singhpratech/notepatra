// SPDX-License-Identifier: GPL-3.0-or-later

#include "fmtpanel.h"
#include "editor_symbols.h"
#include "npp_palette.h"
#include "theme_detect.h"   // npIsDarkTheme / npResolvedThemeName
#include "fonts.h"
#include "config.h"
#include "transform_note.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QFont>
#include <QApplication>
#include <QClipboard>
#include <QDateTime>
#include <QListWidgetItem>
#include <QEvent>
#include <QTextDocument>   // Qt::convertFromPlainText

// ═══════════════════════════════════════════════════════════════════════
// Theme-aware palette for the formatter panels (JSON / HTML / SQL /
// Bracket / Compare-diff etc.). Reads Config::theme at construction
// time so switching themes picks up new panels. Light mode was showing
// dark-theme hard-coded colours — labels were invisible and the whole
// panel looked like it belonged to a different app.
// ═══════════════════════════════════════════════════════════════════════
namespace {
struct FmtPalette {
    QColor titleBg, titleFg;
    QColor statusBg, statusFg, statusAccent;
    QColor logHeaderBg, logHeaderFg;
    QColor logBg, logFg, logItemSep, logItemHover;
    QColor editorBg, editorFg;
    QColor caretLineBg;
    QColor matchBraceBg, matchBraceFg;
    QColor errorBg, errorFg;
};

static bool fmtIsDark() {
    // Resolve "System" → "Dark"/"Light" via the OS detector. Without this
    // a user with Config::theme = "System" on a dark OS still got the
    // light palette (white paper on dark chrome).
    return npIsDarkTheme();
}

static FmtPalette fmtPalette() {
    FmtPalette p;
    if (fmtIsDark()) {
        p.titleBg      = QColor("#2D2D2D");
        p.titleFg      = QColor("#4EC9B0");
        p.statusBg     = QColor("#1E3A3A");
        p.statusFg     = QColor("#4EC9B0");
        p.statusAccent = QColor("#4EC9B0");
        p.logHeaderBg  = QColor("#2D2D2D");
        p.logHeaderFg  = QColor("#888888");
        p.logBg        = QColor("#1E1E1E");
        p.logFg        = QColor("#D4D4D4");
        p.logItemSep   = QColor("#2D2D2D");
        p.logItemHover = QColor("#2D3D3D");
        p.editorBg     = QColor("#1E1E1E");
        p.editorFg     = QColor("#D4D4D4");
        p.caretLineBg  = QColor("#2A2D2E");
        p.matchBraceBg = QColor("#4E4E4E");
        p.matchBraceFg = QColor("#5FE07E");
        p.errorBg      = QColor("#5A1D1D");
        p.errorFg      = QColor("#FF7A7A");
    } else {
        p.titleBg      = QColor("#F5F4EE");
        p.titleFg      = QColor("#141413");
        p.statusBg     = QColor("#FFF3E8");
        p.statusFg     = QColor("#8A4318");
        p.statusAccent = QColor("#CC785C");
        p.logHeaderBg  = QColor("#F5F4EE");
        p.logHeaderFg  = QColor("#8E8C88");
        p.logBg        = QColor("#FFFFFF");
        p.logFg        = QColor("#141413");
        p.logItemSep   = QColor("#E8E6E0");
        p.logItemHover = QColor("#F5F4EE");
        p.editorBg     = QColor("#FFFFFF");
        p.editorFg     = QColor("#000000");
        p.caretLineBg  = QColor("#F7F4ED");
        p.matchBraceBg = QColor("#FFECB0");
        p.matchBraceFg = QColor("#B8860B");
        p.errorBg      = QColor("#FBCBCB");
        p.errorFg      = QColor("#C92A2A");
    }
    return p;
}
} // namespace

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

    const FmtPalette fp = fmtPalette();

    m_titleLabel = new QLabel("  " + title);
    // 24 px was tight on Windows where bold-font ascenders+descenders
    // clipped against the row below. minimumHeight lets it grow.
    m_titleLabel->setMinimumHeight(28);
    m_titleLabel->setStyleSheet(QString(
        "font-weight: bold; background: %1; color: %2; padding: 4px 6px;")
        .arg(fp.titleBg.name(), fp.titleFg.name()));
    layout->addWidget(m_titleLabel);

    auto *btnWidget = new QWidget;
    m_btnRow = new QHBoxLayout(btnWidget);
    // Right margin bumped 8→16 so the rightmost button (Copy Output)
    // doesn't kiss the panel edge — Windows was clipping the last
    // glyph against the scroll-region boundary.
    m_btnRow->setContentsMargins(8, 6, 16, 6);
    m_btnRow->setSpacing(8);

    // Show Diff button — opens a side-by-side compare of the LAST action's
    // input vs output. Disabled until at least one button click produces
    // non-empty output. Works for ANY action (Format, Minify, Fix+Format,
    // AI Fix, etc.) — every transformation gets recorded automatically.
    m_diffBtn = new QPushButton("Show Diff");
    m_diffBtn->setFixedHeight(26);
    // v0.1.49 — Windows Segoe UI is ~20 % wider than Linux DejaVu Sans Mono
    // for the same point size, so any button without an explicit minimum
    // width gets its label truncated with "..." on Windows. Compute the
    // natural label width + a safety margin so the button is wide enough
    // on every platform without forcing a fixed width that would over-pad
    // narrow labels on Linux/macOS.
    m_diffBtn->setMinimumWidth(m_diffBtn->fontMetrics().horizontalAdvance(m_diffBtn->text()) + 28);
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
    copyBtn->setMinimumWidth(copyBtn->fontMetrics().horizontalAdvance(copyBtn->text()) + 28);
    connect(copyBtn, &QPushButton::clicked, this, [this]() {
        QString text = m_output->text();
        if (text.isEmpty()) {
            setStatus("Nothing to copy — panel is empty", true);
            return;
        }
        QApplication::clipboard()->setText(text);
        setStatus(QString("Copied %1 chars to clipboard").arg(text.length()), false);
    });

    m_btnRow->addStretch();
    m_btnRow->addWidget(m_diffBtn);
    m_btnRow->addWidget(copyBtn);
    layout->addWidget(btnWidget);

    // BIG status banner — every button press updates this so users see something happen
    m_statusLabel = new QLabel("Paste content into the panel below, then click a button above");
    m_statusLabel->setStyleSheet(QString(
        "background: %1; color: %2; padding: 8px 12px; "
        "font-size: 13px; font-weight: 600; border-left: 4px solid %3;")
        .arg(fp.statusBg.name(), fp.statusFg.name(), fp.statusAccent.name()));
    m_statusLabel->setFixedHeight(36);
    m_statusLabel->setWordWrap(false);
    // Reports can be long (every change with its position): the banner shows
    // one elided line (refreshStatusLabel) and must never widen the panel.
    // Plain text: reports quote the user's own text, which may contain '<'.
    m_statusLabel->setTextFormat(Qt::PlainText);
    m_statusLabel->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Fixed);
    m_statusText = m_statusLabel->text();
    m_statusLabel->installEventFilter(this);
    layout->addWidget(m_statusLabel);

    // Real Scintilla editor with syntax highlighting
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
    m_output->setBraceMatching(QsciScintilla::StrictBraceMatch);
    m_output->setMatchedBraceBackgroundColor(fp.matchBraceBg);
    m_output->setMatchedBraceForegroundColor(fp.matchBraceFg);
    m_output->setCaretLineVisible(true);
    m_output->setCaretLineBackgroundColor(fp.caretLineBg);
    m_output->setPaper(fp.editorBg);
    m_output->setColor(fp.editorFg);
    m_output->setMarginsBackgroundColor(fp.titleBg);
    m_output->setMarginsForegroundColor(fp.logHeaderFg);
    // v0.1.34 fix: setFolding() above creates a fold margin at index 2,
    // but without setFoldMarginColors() Scintilla paints it default white.
    // On Dark theme that's a stark white strip between the line numbers
    // and the editor body — reported by user via SQL Formatter screenshot.
    // Paint it the same colour as the editor body so it's invisible.
    m_output->setFoldMarginColors(fp.editorBg, fp.editorBg);

    layout->addWidget(m_output, 1);

    // Session log — every action taken on this panel during the session is
    // appended here so users can see "what action was taken and what size
    // was fixed for the session" at a glance. Compact, capped at 50 entries.
    m_logHeader = new QLabel("  Session log");
    m_logHeader->setStyleSheet(QString("background: %1; color: %2; padding: 2px 6px; "
                             "font-size: 10px; font-weight: bold;")
        .arg(fp.logHeaderBg.name(), fp.logHeaderFg.name()));
    m_logHeader->setFixedHeight(18);
    layout->addWidget(m_logHeader);

    m_sessionLog = new QListWidget;
    m_sessionLog->setStyleSheet(
        QString("QListWidget { background: %1; color: %2; border: none; "
                "font-family: %3; font-size: 11px; padding: 2px; }"
        "QListWidget::item { padding: 2px 8px; border-bottom: 1px solid %4; }"
        "QListWidget::item:hover { background: %5; }")
        .arg(fp.logBg.name(), fp.logFg.name(), notepatraCodeCssFamily(),
             fp.logItemSep.name(), fp.logItemHover.name()));
    m_sessionLog->setFixedHeight(80);
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

    const FmtPalette fp = fmtPalette();
    // Resolved name (Light/Dark/Monokai) so "System" doesn't fall through
    // applyNotepadPlusPalette's "is dark" check.
    const QString themeName = npResolvedThemeName();
    if (lexer) {
        lexer->setDefaultFont(mono);
        lexer->setDefaultPaper(fp.editorBg);
        lexer->setDefaultColor(fp.editorFg);
        m_output->setLexer(lexer);
        // Pass the current theme name so the Notepad++ palette paints
        // dark-mode colours when the app is in Dark/Monokai. Without this,
        // applyNotepadPlusPalette defaults to LIGHT-MODE colours (white
        // paper + black text) regardless of the panel chrome — users on
        // Dark theme would see a blinding white editor inside a dark panel.
        applyNotepadPlusPalette(lexer, mono, themeName);
    }
    // Belt-and-braces: explicitly set the editor's own paper + foreground
    // so even text typed BEFORE the lexer kicks in (whitespace, plain text)
    // is visible, and so the colours track the current theme.
    m_output->setPaper(fp.editorBg);
    m_output->setColor(fp.editorFg);
    m_output->setCaretForegroundColor(fp.editorFg);
}

// ─── Theme-aware repaint of every styled child ────────────────────────
void FormatterPanel::applyPalette() {
    const FmtPalette fp = fmtPalette();

    if (m_titleLabel) {
        m_titleLabel->setStyleSheet(QString(
            "font-weight: bold; background: %1; color: %2; padding: 2px 6px;"
        ).arg(fp.titleBg.name(), fp.titleFg.name()));
    }
    if (m_logHeader) {
        m_logHeader->setStyleSheet(QString(
            "background: %1; color: %2; padding: 2px 6px; "
            "font-size: 10px; font-weight: bold;"
        ).arg(fp.logHeaderBg.name(), fp.logHeaderFg.name()));
    }
    if (m_sessionLog) {
        m_sessionLog->setStyleSheet(QString(
            "QListWidget { background: %1; color: %2; border: none; "
            "font-family: %3; font-size: 11px; padding: 2px; }"
            "QListWidget::item { padding: 2px 8px; border-bottom: 1px solid %4; }"
            "QListWidget::item:hover { background: %5; }"
        ).arg(fp.logBg.name(), fp.logFg.name(), notepatraCodeCssFamily(),
              fp.logItemSep.name(), fp.logItemHover.name()));
    }
    if (m_output) {
        m_output->setMatchedBraceBackgroundColor(fp.matchBraceBg);
        m_output->setMatchedBraceForegroundColor(fp.matchBraceFg);
        m_output->setCaretLineBackgroundColor(fp.caretLineBg);
        m_output->setMarginsBackgroundColor(fp.titleBg);
        m_output->setMarginsForegroundColor(fp.logHeaderFg);
        m_output->setFoldMarginColors(fp.editorBg, fp.editorBg);
        applyLexer();
        if (int len = m_output->length()) m_output->recolor(0, len);
    }
    // Re-apply the status banner so its colours track the theme — preserving
    // whether the last message was an error.
    setStatus(m_statusText, m_lastStatusWasError, m_statusDetails);
}

void FormatterPanel::onThemeChanged() {
    applyPalette();
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
    // v0.1.49 — guarantee buttons are wide enough on Windows where Segoe UI
    // (the system default) is ~20 % wider than Linux DejaVu Sans Mono for
    // the same point size. Without this, multi-word labels like
    // "Format (2 spaces)" / "Generate Docs" / "Add Comments" got truncated
    // with "..." on Windows. fontMetrics() reads from the actual button so
    // it picks up the platform-appropriate font automatically.
    btn->setMinimumWidth(btn->fontMetrics().horizontalAdvance(label) + 28);
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
        TransformNote::clear();
        try {
            output = fn(input);
        } catch (const std::exception &e) {
            TransformNote::clear();
            setStatus(QString("Failed: %1 — %2").arg(label).arg(e.what()), true);
            return;
        } catch (...) {
            TransformNote::clear();
            setStatus(QString("Failed: %1 (unknown error)").arg(label), true);
            return;
        }
        // The JSON / bracket wrappers report outcomes they cannot express in
        // the returned text (transform_note.h). The banner gets one line, its
        // tooltip the full report (every change / issue with its position).
        const TransformNote::Note note = TransformNote::take();
        if (note.keepText) {
            // A report ABOUT the text (Bracket Tools > Check): the text stays
            // exactly as it was, so the next action still runs on the code.
            setStatus(QString("%1: %2").arg(label, note.text),
                      note.kind == TransformNote::Kind::Error, note.details);
            logAction(label, input.length(), input.length(), note.text, note.details);
            return;
        }
        // An Error means the input was NOT transformed (invalid JSON, a
        // repair that would be a guess): say exactly why and where, and leave
        // the user's text untouched instead of presenting it as a result.
        if (note.kind == TransformNote::Kind::Error) {
            setStatus(QString("Not changed: %1 — %2").arg(label, note.text), true,
                      note.details);
            logAction(label, input.length(), input.length(),
                      QStringLiteral("not changed: ") + note.text, note.details);
            return;
        }

        if (output.isEmpty()) {
            setStatus(QString("Failed: %1 returned empty output").arg(label), true);
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
        if (note.kind == TransformNote::Kind::Info) {
            // What the fix actually did, with positions.
            setStatus(QString("Done: %1 — %2").arg(label, note.text), false, note.details);
            logAction(label, beforeLen, output.length(), note.text, note.details);
            return;
        }
        setStatus(QString("Done: %1 — %2 chars, %3 lines (%4). Click Show Diff to see changes.")
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

void FormatterPanel::setStatus(const QString &text, bool isError, const QString &details) {
    if (!m_statusLabel) return;
    m_statusText = text;
    m_statusDetails = details;
    m_lastStatusWasError = isError;
    // Use the shared fmtPalette so the banner colours track the current
    // theme instead of freezing at dark-mode literals. Success / failure is
    // carried by the colours and a leading word ("Done:", "Not changed:",
    // "Failed:"), not by symbol glyphs the UI font may not have.
    const FmtPalette fp = fmtPalette();
    QColor bg     = isError ? fp.errorBg : fp.statusBg;
    QColor accent = isError ? fp.errorFg : fp.statusAccent;
    m_statusLabel->setStyleSheet(
        QString("background: %1; color: %2; padding: 8px 12px; "
                "font-size: 13px; font-weight: 600; border-left: 4px solid %2;")
        .arg(bg.name(), accent.name()));
    refreshStatusLabel();
}

void FormatterPanel::refreshStatusLabel() {
    if (!m_statusLabel) return;
    m_statusLabel->ensurePolished();
    // 12 px padding each side + the 4 px accent border (setStatus QSS).
    const int avail = m_statusLabel->width() - 2 * 12 - 4 - 4;
    QString shown = m_statusText;
    if (avail > 40)
        shown = m_statusLabel->fontMetrics().elidedText(m_statusText, Qt::ElideRight, avail);
    m_statusLabel->setText(shown);
    // The full report (or, when the banner is cut, the whole banner line) is
    // one hover away. convertFromPlainText: reports quote the user's text.
    QString tip = m_statusDetails;
    if (tip.isEmpty() && shown != m_statusText) tip = m_statusText;
    m_statusLabel->setToolTip(tip.isEmpty()
                                  ? QString()
                                  : Qt::convertFromPlainText(tip, Qt::WhiteSpaceNormal));
}

bool FormatterPanel::eventFilter(QObject *watched, QEvent *event) {
    // Re-elide whenever the banner itself changes width (panel resize, tab
    // shown for the first time, dock/splitter moves).
    if (watched == m_statusLabel && event->type() == QEvent::Resize)
        refreshStatusLabel();
    return QWidget::eventFilter(watched, event);
}

void FormatterPanel::logAction(const QString &action, int beforeChars, int afterChars,
                               const QString &extra, const QString &details) {
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
    if (!details.isEmpty())
        item->setToolTip(Qt::convertFromPlainText(details, Qt::WhiteSpaceNormal));
    // Color the entry based on whether it changed anything, using the
    // theme-aware fmtPalette instead of dark-mode-only hex literals.
    const FmtPalette fp = fmtPalette();
    if (delta > 0)      item->setForeground(fp.statusAccent);    // theme-accent = fixed/added
    else if (delta < 0) item->setForeground(fp.matchBraceFg);    // warm = minified/shrunk
    else                item->setForeground(fp.logHeaderFg);     // muted = no-op
    m_sessionLog->addItem(item);
    m_sessionLog->scrollToBottom();
    // Cap history at 50 entries
    while (m_sessionLog->count() > 50) {
        delete m_sessionLog->takeItem(0);
    }
}
