#include "npp_palette.h"

#include <QColor>
#include <QString>
#include <Qsci/qscilexer.h>

// ═══════════════════════════════════════════════════════════════════════
// Notepad++ default color palette — matches stylers.xml from Notepad++ 8.x
// Applied per-style using the lexer's own description() to identify styles,
// so this works across ALL 40+ QScintilla lexers without hard-coding constants.
// ═══════════════════════════════════════════════════════════════════════
void applyNotepadPlusPalette(QsciLexer *lexer, const QFont &baseFont, const QString &themeName) {
    if (!lexer) return;

    const bool dark = themeName.compare("Dark", Qt::CaseInsensitive) == 0;
    const bool monokai = themeName.compare("Monokai", Qt::CaseInsensitive) == 0;

    const QColor npPaper      = monokai ? QColor("#272822") : (dark ? QColor("#1E1E1E") : QColor("#FFFFFF"));
    const QColor npText       = monokai ? QColor("#F8F8F2") : (dark ? QColor("#D4D4D4") : QColor("#000000"));
    const QColor npKeyword    = monokai ? QColor("#F92672") : (dark ? QColor("#569CD6") : QColor("#0000FF"));
    const QColor npKeyword2   = monokai ? QColor("#AE81FF") : (dark ? QColor("#C586C0") : QColor("#800080"));
    const QColor npComment    = monokai ? QColor("#75715E") : (dark ? QColor("#6A9955") : QColor("#008000"));
    const QColor npNumber     = monokai ? QColor("#AE81FF") : (dark ? QColor("#B5CEA8") : QColor("#FF8000"));
    const QColor npString     = monokai ? QColor("#E6DB74") : (dark ? QColor("#CE9178") : QColor("#808080"));
    const QColor npChar       = npString;
    const QColor npOperator   = npText;
    const QColor npPreproc    = monokai ? QColor("#66D9EF") : (dark ? QColor("#C586C0") : QColor("#804000"));
    const QColor npRegex      = monokai ? QColor("#FD971F") : (dark ? QColor("#D16969") : QColor("#800080"));
    const QColor npClassName  = monokai ? QColor("#A6E22E") : (dark ? QColor("#DCDCAA") : QColor("#006480"));
    const QColor npDecorator  = monokai ? QColor("#FD971F") : (dark ? QColor("#4EC9B0") : QColor("#FF8000"));
    const QColor npError      = monokai ? QColor("#F44747") : (dark ? QColor("#F44747") : QColor("#FF0000"));

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
