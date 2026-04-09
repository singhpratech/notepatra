#include "npp_palette.h"

#include <QColor>
#include <QString>
#include <Qsci/qscilexer.h>

// ═══════════════════════════════════════════════════════════════════════
// Notepad++ default color palette — matches stylers.xml from Notepad++ 8.x
// Applied per-style using the lexer's own description() to identify styles,
// so this works across ALL 40+ QScintilla lexers without hard-coding constants.
// ═══════════════════════════════════════════════════════════════════════
void applyNotepadPlusPalette(QsciLexer *lexer, const QFont &baseFont) {
    if (!lexer) return;

    // Notepad++ "Default" theme colors
    const QColor npPaper     (0xFF, 0xFF, 0xFF);  // White background
    const QColor npText      (0x00, 0x00, 0x00);  // Black text
    const QColor npKeyword   (0x00, 0x00, 0xFF);  // Blue bold
    const QColor npKeyword2  (0x80, 0x00, 0x80);  // Purple (KEYWORD2 / type)
    const QColor npComment   (0x00, 0x80, 0x00);  // Green italic
    const QColor npNumber    (0xFF, 0x80, 0x00);  // Orange
    const QColor npString    (0x80, 0x80, 0x80);  // Gray
    const QColor npChar      (0x80, 0x80, 0x80);  // Gray
    const QColor npOperator  (0x00, 0x00, 0x00);  // Black bold
    const QColor npPreproc   (0x80, 0x40, 0x00);  // Brown bold
    const QColor npRegex     (0x80, 0x00, 0x80);  // Purple
    const QColor npClassName (0x00, 0x64, 0x80);  // Dark cyan (class/function)
    const QColor npDecorator (0xFF, 0x80, 0x00);  // Orange (decorators/attrs)
    const QColor npError     (0xFF, 0x00, 0x00);  // Red

    QFont regular = baseFont; regular.setBold(false); regular.setItalic(false);
    QFont bold    = baseFont; bold.setBold(true);    bold.setItalic(false);
    QFont italic  = baseFont; italic.setBold(false); italic.setItalic(true);

    // Paint all 128 possible style slots
    for (int i = 0; i < 128; ++i) {
        QString desc = lexer->description(i);
        if (desc.isEmpty()) continue;
        const QString d = desc.toLower();

        lexer->setPaper(npPaper, i);
        lexer->setFont(regular, i);
        QColor fg = npText;

        if (d.contains("keyword")) {
            // Secondary keyword sets — use purple to distinguish from primary blue
            if (d.contains("set 2") || d.contains("set2") ||
                d.contains("secondary") || d.contains("user")) {
                fg = npKeyword2;
            } else {
                fg = npKeyword;
            }
            lexer->setFont(bold, i);
        }
        else if (d.contains("comment")) {
            fg = npComment;
            lexer->setFont(italic, i);
        }
        else if (d.contains("number") || d.contains("numeric")) {
            fg = npNumber;
        }
        else if (d.contains("regex") || d.contains("regular expression")) {
            fg = npRegex;
        }
        else if (d.contains("string") || d.contains("char") ||
                 d.contains("literal") || d.contains("heredoc") ||
                 d.contains("backtick") || d.contains("verbatim")) {
            fg = (d.contains("char") ? npChar : npString);
        }
        else if (d.contains("preproc") || d.contains("pre-proc") ||
                 d.contains("processor")) {
            // Notepad++ paints preprocessor in brown but NOT bold — bold
            // everywhere makes the page feel heavy/aggressive.
            fg = npPreproc;
        }
        else if (d.contains("operator")) {
            // Plain black for operators (Notepad++ does NOT bold operators
            // by default — keep the page feeling light).
            fg = npOperator;
        }
        else if (d.contains("decorator") || d.contains("attribute")) {
            fg = npDecorator;
        }
        else if (d.contains("class") || d.contains("function") ||
                 d.contains("method") || d.contains("global")) {
            // Dark cyan but not bold — distinguishes class names without
            // making them visually heavy.
            fg = npClassName;
        }
        else if (d.contains("error") || d.contains("unclosed")) {
            fg = npError;
        }
        else if (d.contains("tag") || d.contains("element")) {
            // HTML/XML tag — blue but not bold
            fg = npKeyword;
        }
        else if (d.contains("entity")) {
            fg = npNumber;
        }
        else if (d.contains("header") || d.contains("header1") ||
                 d.contains("strong") || d.contains("bold")) {
            // Markdown headers — keep bold here because that's the actual
            // semantics of "header" / "strong"
            fg = npKeyword;
            lexer->setFont(bold, i);
        }
        else if (d.contains("emphasis") || d.contains("italic")) {
            fg = npKeyword2;
            lexer->setFont(italic, i);
        }
        else if (d.contains("link") || d.contains("url")) {
            fg = npClassName;
        }
        // else: plain text / identifier — keep npText

        lexer->setColor(fg, i);
    }

    // Also set the default style (0) explicitly in case lexer skips it
    lexer->setPaper(npPaper, 0);
    lexer->setColor(npText, 0);
    lexer->setFont(regular, 0);
}
