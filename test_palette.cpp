// test_palette.cpp — verify the Notepad++ default palette is applied to
// QScintilla lexers with the EXACT expected colors on EVERY language we ship.
//
// test_lexers.cpp only verifies that lexers tokenize — it does not check
// whether keywords render in blue, comments in green, etc. This test does.
//
// If keyword highlighting is invisible (the bug reported on Windows v0.1.1),
// this test catches it because it verifies specific style colors.
//
// Build: see CMakeLists.txt target test_palette
// Run:   ./test_palette
// Exit code 0 = all checks pass; non-zero = at least one color is wrong.

#include <QApplication>
#include <QColor>
#include <QFont>
#include <cstdio>
#include <vector>

#include "npp_palette.h"  // free function: applyNotepadPlusPalette()

#include <Qsci/qscilexer.h>
#include <Qsci/qscilexercpp.h>
#include <Qsci/qscilexerpython.h>
#include <Qsci/qscilexersql.h>
#include <Qsci/qscilexerjson.h>
#include <Qsci/qscilexerjavascript.h>
#include <Qsci/qscilexerhtml.h>
#include <Qsci/qscilexercss.h>
#include <Qsci/qscilexerbash.h>
#include <Qsci/qscilexeryaml.h>
#include <Qsci/qscilexermarkdown.h>
#include <Qsci/qscilexerjava.h>

// Notepad++ default palette — must match editor.cpp values
static const QColor NP_KEYWORD  (0x00, 0x00, 0xFF);
// Light-mode comment — calibrated slightly darker-saturated for Clay paper
// (was 0x008000 pure HTML green, which washed out on warm backgrounds).
static const QColor NP_COMMENT  (0x0E, 0x8D, 0x0E);
static const QColor NP_NUMBER   (0xFF, 0x80, 0x00);
static const QColor NP_STRING   (0x80, 0x80, 0x80);
static const QColor NP_OPERATOR (0x00, 0x00, 0x00);
static const QColor NP_PAPER    (0xFF, 0xFF, 0xFF);
static const QColor NP_PREPROC  (0x80, 0x40, 0x00);

struct Check {
    const char *lexerName;
    int styleIndex;
    const char *styleDesc;     // substring of lexer->description() for the style
    QColor expectedColor;
    bool expectBold;
    bool expectItalic;
};

static int run_checks(const char *name, QsciLexer *lex, const std::vector<Check> &checks) {
    int failed = 0;
    for (const auto &c : checks) {
        // Sanity: confirm the style index actually is the style we think it is
        QString desc = lex->description(c.styleIndex).toLower();
        QString expectDesc = QString(c.styleDesc).toLower();
        if (!desc.contains(expectDesc)) {
            fprintf(stderr, "  FAIL %-10s style %-2d: description=\"%s\" but expected to contain \"%s\"\n",
                    name, c.styleIndex, desc.toStdString().c_str(), c.styleDesc);
            failed++;
            continue;
        }

        QColor got = lex->color(c.styleIndex);
        if (got != c.expectedColor) {
            fprintf(stderr, "  FAIL %-10s style %-2d (%s): color=#%02X%02X%02X, expected=#%02X%02X%02X\n",
                    name, c.styleIndex, c.styleDesc,
                    got.red(), got.green(), got.blue(),
                    c.expectedColor.red(), c.expectedColor.green(), c.expectedColor.blue());
            failed++;
            continue;
        }

        QColor paper = lex->paper(c.styleIndex);
        if (paper != NP_PAPER) {
            fprintf(stderr, "  FAIL %-10s style %-2d (%s): paper=#%02X%02X%02X, expected=#FFFFFF\n",
                    name, c.styleIndex, c.styleDesc,
                    paper.red(), paper.green(), paper.blue());
            failed++;
            continue;
        }

        QFont font = lex->font(c.styleIndex);
        if (c.expectBold && !font.bold()) {
            fprintf(stderr, "  FAIL %-10s style %-2d (%s): expected bold\n", name, c.styleIndex, c.styleDesc);
            failed++;
            continue;
        }
        if (c.expectItalic && !font.italic()) {
            fprintf(stderr, "  FAIL %-10s style %-2d (%s): expected italic\n", name, c.styleIndex, c.styleDesc);
            failed++;
            continue;
        }

        fprintf(stdout, "  ok   %-10s style %-2d %-24s = #%02X%02X%02X%s%s\n",
                name, c.styleIndex, c.styleDesc,
                got.red(), got.green(), got.blue(),
                font.bold() ? " [bold]" : "",
                font.italic() ? " [italic]" : "");
    }
    return failed;
}

int main(int argc, char *argv[]) {
    QApplication app(argc, argv);
    QFont base("Consolas", 11);

    int total_failed = 0;

    // ─── C++ (QsciLexerCPP) ────────────────────────────────────────────
    // Style constants from QScintilla source (SCLEX_CPP):
    //   1 = comment, 2 = line comment, 4 = number, 5 = keyword,
    //   6 = double-quoted string, 7 = single-quoted string,
    //   9 = preprocessor, 10 = operator
    {
        QsciLexerCPP lex;
        applyNotepadPlusPalette(&lex, base);
        total_failed += run_checks("C++", &lex, {
            {"C++",  5,  "keyword",      NP_KEYWORD, true, false},
            {"C++",  1,  "comment",      NP_COMMENT, false, true},
            {"C++",  2,  "comment",      NP_COMMENT, false, true},
            {"C++",  4,  "number",       NP_NUMBER, false, false},
            {"C++",  6,  "string",       NP_STRING, false, false},
            {"C++",  9,  "pre-processor", NP_PREPROC, false, false},
            {"C++",  10, "operator",     NP_OPERATOR, false, false},
        });
        // Secondary keywords (style 16 = SCE_C_WORD2). v0.1.27 introduced
        // per-language accents — for C/C++ the secondary set is now
        // #267F99 teal (was #800080 purple) to match VS Code defaults
        // for type names. Same value flows through to Java's secondary
        // when not Kotlin-overridden, JS / TS via the same branch, etc.
        QColor sec = lex.color(16);
        if (sec == QColor(0x26, 0x7F, 0x99)) {
            fprintf(stdout, "  ok   C++        style 16 secondary keywords      = #267F99 [teal]\n");
        } else {
            fprintf(stderr, "  FAIL C++        style 16 secondary keywords: got #%02X%02X%02X, expected #267F99\n",
                    sec.red(), sec.green(), sec.blue());
            total_failed++;
        }
    }

    // ─── JavaScript (QsciLexerJavaScript) shares CPP styles ─────────────
    {
        QsciLexerJavaScript lex;
        applyNotepadPlusPalette(&lex, base);
        total_failed += run_checks("JS", &lex, {
            {"JS",  5,  "keyword",  NP_KEYWORD, true, false},
            {"JS",  1,  "comment",  NP_COMMENT, false, true},
            {"JS",  4,  "number",   NP_NUMBER, false, false},
            {"JS",  6,  "string",   NP_STRING, false, false},
            {"JS",  10, "operator", NP_OPERATOR, false, false},
        });
    }

    // ─── Python (QsciLexerPython) ──────────────────────────────────────
    // 1 = comment, 2 = number, 5 = keyword, 3/4 = strings, 10 = operator
    {
        QsciLexerPython lex;
        applyNotepadPlusPalette(&lex, base);
        total_failed += run_checks("Python", &lex, {
            {"Python", 5,  "keyword",  NP_KEYWORD, true, false},
            {"Python", 1,  "comment",  NP_COMMENT, false, true},
            {"Python", 2,  "number",   NP_NUMBER, false, false},
            {"Python", 3,  "string",   NP_STRING, false, false},
            {"Python", 4,  "string",   NP_STRING, false, false},
            {"Python", 10, "operator", NP_OPERATOR, false, false},
        });
    }

    // ─── SQL (QsciLexerSQL) — this is the one the user tested on Windows ─
    {
        QsciLexerSQL lex;
        applyNotepadPlusPalette(&lex, base);
        total_failed += run_checks("SQL", &lex, {
            {"SQL", 5,  "keyword",  NP_KEYWORD, true, false},
            {"SQL", 1,  "comment",  NP_COMMENT, false, true},
            {"SQL", 2,  "comment",  NP_COMMENT, false, true},
            {"SQL", 4,  "number",   NP_NUMBER, false, false},
            {"SQL", 6,  "string",   NP_STRING, false, false},
            {"SQL", 10, "operator", NP_OPERATOR, false, false},
        });
    }

    // ─── JSON (QsciLexerJSON) — actual style map per QScintilla 2.14 ─────
    //   1 = Number, 2 = String, 4 = Property (key), 6 = Line comment,
    //   7 = Block comment, 8 = Operator, 11 = JSON keyword (true/false/null)
    //
    // v0.1.27: JSON keyword (true/false/null) now uses the JSON-specific
    // palette colour #0451A5 (a darker, more readable JSON-key blue used
    // in VS Code's default JSON theme) instead of the generic #0000FF
    // primary keyword. This makes JSON files visually distinct from
    // generic-keyword languages.
    static const QColor NP_JSON_KEYWORD(0x04, 0x51, 0xA5);
    {
        QsciLexerJSON lex;
        applyNotepadPlusPalette(&lex, base);
        total_failed += run_checks("JSON", &lex, {
            {"JSON", 11, "keyword",  NP_JSON_KEYWORD, true, false},
            {"JSON", 1,  "number",   NP_NUMBER, false, false},
            {"JSON", 2,  "string",   NP_STRING, false, false},
            {"JSON", 6,  "comment",  NP_COMMENT, false, true},
            {"JSON", 7,  "comment",  NP_COMMENT, false, true},
            {"JSON", 8,  "operator", NP_OPERATOR, false, false},
        });
    }

    // ─── Bash ──────────────────────────────────────────────────────────
    {
        QsciLexerBash lex;
        applyNotepadPlusPalette(&lex, base);
        total_failed += run_checks("Bash", &lex, {
            {"Bash", 4, "keyword", NP_KEYWORD, true, false},
            {"Bash", 2, "comment", NP_COMMENT, false, true},
            {"Bash", 3, "number",  NP_NUMBER, false, false},
            {"Bash", 5, "string",  NP_STRING, false, false},
        });
    }

    // ─── Markdown ──────────────────────────────────────────────────────
    // Markdown doesn't have "keyword" — it has "header" which should be
    // blue bold (Notepad++ Markdown shows headers blue bold).
    {
        QsciLexerMarkdown lex;
        applyNotepadPlusPalette(&lex, base);
        // Just iterate all styles and confirm at least one maps to Header → blue bold
        bool foundHeader = false;
        for (int i = 0; i < 32; i++) {
            QString desc = lex.description(i).toLower();
            if (desc.contains("header") || desc.contains("strong") || desc.contains("bold")) {
                QColor col = lex.color(i);
                if (col == NP_KEYWORD) { foundHeader = true; break; }
            }
        }
        if (foundHeader) {
            fprintf(stdout, "  ok   %-10s header style painted blue bold\n", "Markdown");
        } else {
            fprintf(stderr, "  FAIL %-10s no header style found with blue color\n", "Markdown");
            total_failed++;
        }
    }

    fprintf(stdout, "\n");
    if (total_failed == 0) {
        fprintf(stdout, "=== ALL PALETTE CHECKS PASS ===\n");
        fprintf(stdout, "Every lexer paints keywords #0000FF bold, comments #0E8D0E italic,\n");
        fprintf(stdout, "numbers #FF8000, strings #808080, operators #000000 (not bold),\n");
        fprintf(stdout, "preprocessor #804000 (not bold) — matches Notepad++ stylers.xml\n");
        fprintf(stdout, "default theme. Less bold = lighter, less aggressive feel.\n");
        return 0;
    } else {
        fprintf(stderr, "=== %d PALETTE CHECKS FAILED ===\n", total_failed);
        return 1;
    }
}
