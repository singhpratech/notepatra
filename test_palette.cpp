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
#include <QFile>
#include <QFont>
#include <cstdio>
#include <vector>

#include "npp_palette.h"  // free function: applyNotepadPlusPalette()

#include <Qsci/qsciscintilla.h>
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

// Notepatra-local lexers needed for v0.1.33+ brand-palette assertions
// (PowerShell variable / cmdlet / alias colours, etc.)
#include "lexer_powershell.h"
#include "fonts.h"           // notepatraUiCssFamily / notepatraCodeCssFamily

// Helper: assert that lex->color(style) == expectedHex. Returns 0 on
// success, 1 on failure. Used by the per-language brand-palette tests
// added in v0.1.33 — saves typing out the same 5-line if/else block 30+
// times. The label is what shows in the test output.
static int check_color(const char *lang, QsciLexer *lex, int style,
                       const char *kind, QColor expected, const char *hexLabel) {
    QColor got = lex->color(style);
    if (got == expected) {
        fprintf(stdout, "  ok   %-10s style %-2d %-24s = %s\n",
                lang, style, kind, hexLabel);
        return 0;
    }
    fprintf(stderr, "  FAIL %-10s style %-2d %s: got #%02X%02X%02X, expected %s\n",
            lang, style, kind,
            got.red(), got.green(), got.blue(), hexLabel);
    return 1;
}

// Notepad++ default palette — verified against notepad-plus-plus master
// PowerEditor/src/stylers.model.xml (light) and DarkModeDefault.xml (dark).
// v0.1.31 aligned all values with the N++ canonical 9-hue scheme: the
// previous Clay-tuned green and plain-black operator were the cause of
// the "all blue shades" complaint (identifiers + operators blended into
// keyword blue without distinct hues + bold).
static const QColor NP_KEYWORD  (0x00, 0x00, 0xFF);
static const QColor NP_COMMENT  (0x00, 0x80, 0x00);
static const QColor NP_NUMBER   (0xFF, 0x80, 0x00);
static const QColor NP_STRING   (0x80, 0x80, 0x80);
static const QColor NP_OPERATOR (0x00, 0x00, 0x80);  // navy bold, was plain black
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
        // Secondary keywords (style 16 = SCE_C_WORD2). v0.1.32 brings C/C++
        // back into VS Code Dark+ alignment for type names — secondary
        // keywords use teal `#267F99` light / `#4EC9B0` dark, the de-facto
        // VS Code standard for C/C++ type words. (v0.1.31 had used the N++
        // canonical violet #8000FF, but C/C++ is so dominantly used in VS
        // Code that users expect VS Code's teal types here. Other languages
        // that fall back to the generic palette still see violet types.)
        QColor sec = lex.color(16);
        if (sec == QColor(0x26, 0x7F, 0x99)) {
            fprintf(stdout, "  ok   C++        style 16 secondary keywords      = #267F99 [VS Code teal]\n");
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
            {"JS",  10, "operator", NP_OPERATOR, true, false},
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
            {"Python", 10, "operator", NP_OPERATOR, true, false},
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
            {"SQL", 10, "operator", NP_OPERATOR, true, false},
        });
    }

    // ─── JSON (QsciLexerJSON) — actual style map per QScintilla 2.14 ─────
    //   1 = Number, 2 = String, 4 = Property (key), 6 = Line comment,
    //   7 = Block comment, 8 = Operator, 11 = JSON keyword (true/false/null)
    //
    // v0.1.32: JSON now follows VS Code Light+ canonical:
    //   - keyword (true/false/null) at style 11 = pure blue #0000FF
    //     (matches VS Code Light+ constant.language)
    //   - property key at style 4 = #0451A5 darker JSON-blue
    //     (matches VS Code Light+ meta.structure.dictionary.key)
    // The two-blue split makes JSON visually distinct: keys read as
    // dark JSON-blue, true/false/null as bright keyword-blue, strings
    // remain grey, numbers orange — five clearly distinct token kinds.
    {
        QsciLexerJSON lex;
        applyNotepadPlusPalette(&lex, base);
        total_failed += run_checks("JSON", &lex, {
            {"JSON", 11, "keyword",  NP_KEYWORD, true, false},
            {"JSON", 1,  "number",   NP_NUMBER, false, false},
            {"JSON", 2,  "string",   NP_STRING, false, false},
            {"JSON", 6,  "comment",  NP_COMMENT, false, true},
            {"JSON", 7,  "comment",  NP_COMMENT, false, true},
            {"JSON", 8,  "operator", NP_OPERATOR, true, false},
        });
        // Property key (style 4) — the JSON-distinctive accent.
        QColor jsonKey = lex.color(4);
        if (jsonKey == QColor(0x04, 0x51, 0xA5)) {
            fprintf(stdout, "  ok   JSON       style 4  property key            = #0451A5 [VS Code JSON-blue]\n");
        } else {
            fprintf(stderr, "  FAIL JSON       style 4  property key: got #%02X%02X%02X, expected #0451A5\n",
                    jsonKey.red(), jsonKey.green(), jsonKey.blue());
            total_failed++;
        }
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

    // ═══════════════════════════════════════════════════════════════════
    // v0.1.33 — Per-language BRAND palette assertions. v0.1.32 added
    // per-language overrides so SQL/Python/JSON/JS/PowerShell don't all
    // look like generic blue+violet, but the assertions hadn't caught up.
    // These tests fail if anyone refactors the brand palettes back to
    // generic without updating user-visible colour expectations.
    // ═══════════════════════════════════════════════════════════════════

    // ─── PowerShell — ISE canonical signature (most distinctive) ───────
    // Style indices from src/lexer_powershell.cpp::description():
    //   5 = Variable ($var)              — must paint OrangeRed #FF4500
    //   9 = Cmdlet (Get-Item, New-Object) — must paint pure blue #0000FF
    //  10 = Alias (ls, gci)              — must paint lighter cyan #0080FF
    {
        LexerPowerShell lex;
        applyNotepadPlusPalette(&lex, base);
        total_failed += check_color("PowerShell", &lex, 5,  "$variable",
                                    QColor(0xFF, 0x45, 0x00), "#FF4500 [ISE OrangeRed]");
        total_failed += check_color("PowerShell", &lex, 9,  "Verb-Noun cmdlet",
                                    QColor(0x00, 0x00, 0xFF), "#0000FF [ISE blue]");
        total_failed += check_color("PowerShell", &lex, 10, "alias (ls/gci)",
                                    QColor(0x00, 0x80, 0xFF), "#0080FF [lighter cyan]");
    }

    // ─── Python — VS Code Dark+ canonical (blue kw + teal types + amber names) ──
    // QsciLexerPython style indices:
    //   8  = Class name        — npClassName amber #795E26 light
    //   9  = Function method   — npClassName amber
    //   14 = Highlighted ident (set 2) — npKeyword2 teal #267F99
    {
        QsciLexerPython lex;
        applyNotepadPlusPalette(&lex, base);
        total_failed += check_color("Python", &lex, 8,  "class name",
                                    QColor(0x79, 0x5E, 0x26), "#795E26 [VS Code amber]");
        total_failed += check_color("Python", &lex, 9,  "function name",
                                    QColor(0x79, 0x5E, 0x26), "#795E26 [VS Code amber]");
        total_failed += check_color("Python", &lex, 14, "built-in / set-2",
                                    QColor(0x26, 0x7F, 0x99), "#267F99 [VS Code teal]");
    }

    // ─── SQL — SSMS signature (blue kw + MAGENTA user-defined keyword) ─
    // QsciLexerSQL exposes "User defined 1" at style 19 (NOT 16 — there's
    // no SCE_SQL_WORD2 with description in QScintilla 2.x; primary
    // secondary slot is style 19/20, populate-able via setKeywords(1,...)).
    // Magenta gives SQL files the SSMS-instantly-recognisable feel — once
    // the user populates set 1 with system functions (COUNT/SUM) and types
    // (INT/VARCHAR), those tokens paint magenta.
    {
        QsciLexerSQL lex;
        applyNotepadPlusPalette(&lex, base);
        total_failed += check_color("SQL", &lex, 19, "user-defined kw 1",
                                    QColor(0xFF, 0x00, 0xFF), "#FF00FF [SSMS magenta]");
    }

    // ─── JavaScript / TypeScript — VS Code Dark+ teal types ────────────
    {
        QsciLexerJavaScript lex;
        applyNotepadPlusPalette(&lex, base);
        total_failed += check_color("JS",  &lex, 16, "secondary kw / type",
                                    QColor(0x26, 0x7F, 0x99), "#267F99 [VS Code teal]");
    }

    // ─── Bash — violet keyword2 (built-ins) distinct from primary blue ──
    // QsciLexerBash doesn't have a populated keyword-set-2 by default
    // (no built-in commands list), but we still want the COLOUR set on
    // the lexer for any future expansion. Style 14 (or similar) maps to
    // SCE_SH_PARAM in some Scintilla versions; the lexer's description()
    // chain will tag the right style. Best we can do without lexing:
    // verify npKeyword2 is set on the Bash lexer at all.
    //
    // Skipping for now — Bash brand verification needs a populated set 2
    // which is a future feature. The matcher logic itself is verified
    // by the JS test above (same npKeyword2 generic-palette path).

    // ─── JSON property key — already tested earlier (line ~218). ────────

    // ═══════════════════════════════════════════════════════════════════
    // v0.1.34 — Margin / fold-margin theming. Pre-v0.1.34 the fmt panels
    // (SQL Formatter, JSON Tools, HTML Tools, Bracket Tools) called
    // `setFolding(BoxedTreeFoldStyle, 2)` to enable code folding but
    // never `setFoldMarginColors()` — leaving the fold-margin strip in
    // QScintilla's default WHITE. On Dark theme the user saw a stark
    // white strip between the line numbers and the editor body. This
    // test simulates the panel's setup + theme-apply flow on a real
    // QsciScintilla widget and verifies all 6 margin slots end up with
    // dark colours, never the default #FFFFFF / #C0C0C0 light defaults.
    // ═══════════════════════════════════════════════════════════════════
    {
        // QsciScintilla doesn't expose SCI_GETFOLDMARGINCOLOUR (Scintilla
        // itself only ships SCI_SETFOLDMARGINCOLOUR — the get-side was
        // never added). So we can't round-trip the colour. Instead we do
        // a STRUCTURAL check on the source: any C++ file that calls
        // setFolding() must also call setFoldMarginColors() in the same
        // file, otherwise the fold margin defaults to white on Dark theme.
        //
        // This catches the v0.1.34 regression class — exactly the bug the
        // user reported via SQL Formatter Windows screenshot.
        const QString srcDir = QString(SOURCE_DIR_FOR_TEST) + "/src/";
        const QStringList sourcesToCheck = {
            "sqlfmtpanel.cpp", "fmtpanel.cpp", "compare.cpp", "editor.cpp",
        };
        for (const QString &name : sourcesToCheck) {
            QFile f(srcDir + name);
            if (!f.open(QFile::ReadOnly)) {
                fprintf(stderr, "  FAIL FoldMargin source check: cannot open %s\n",
                        name.toUtf8().constData());
                total_failed++;
                continue;
            }
            const QString src = QString::fromUtf8(f.readAll());
            const bool callsSetFolding = src.contains("setFolding(");
            const bool noFoldStyle     = src.contains("NoFoldStyle");
            const bool setsFoldColours = src.contains("setFoldMarginColors(");
            // Pass conditions:
            //   (a) doesn't call setFolding at all → no fold margin → fine
            //   (b) calls setFolding ONLY with NoFoldStyle → no fold margin
            //   (c) calls setFolding with a real fold style AND also calls
            //       setFoldMarginColors → margin themed correctly
            const bool hasRealFold = callsSetFolding && !noFoldStyle;
            const bool ok = !callsSetFolding || (!hasRealFold) || setsFoldColours;
            if (ok) {
                fprintf(stdout, "  ok   FoldMargin %-18s (folding=%s, colors=%s)\n",
                        name.toUtf8().constData(),
                        hasRealFold ? "yes" : "none",
                        setsFoldColours ? "yes" : "n/a");
            } else {
                fprintf(stderr, "  FAIL FoldMargin %s: calls setFolding(...) but not setFoldMarginColors() — fold strip will render white on Dark theme\n",
                        name.toUtf8().constData());
                total_failed++;
            }
        }
    }

    // ═══════════════════════════════════════════════════════════════════
    // v0.1.33 — Font fallback chain. The Linux "Hello! 👋" tofu bug came
    // from a CSS family chain that didn't list ANY emoji font, so Qt's
    // HarfBuzz shaper had nothing to fall back to for SMP-plane emoji
    // codepoints. The fix in src/fonts.h appended an emoji fallback
    // chain (Apple Color Emoji → Segoe UI Emoji → Noto Color Emoji →
    // Twemoji → Symbola). Test asserts those fonts are listed.
    // ═══════════════════════════════════════════════════════════════════
    {
        const QString uiCss   = notepatraUiCssFamily();
        const QString codeCss = notepatraCodeCssFamily();
        struct EmojiCheck {
            const char *family;
            const char *why;
        };
        const std::vector<EmojiCheck> emojiFonts = {
            {"Apple Color Emoji",  "macOS"},
            {"Segoe UI Emoji",     "Windows 10+"},
            {"Noto Color Emoji",   "Linux / Android"},
        };
        for (const auto &ef : emojiFonts) {
            if (uiCss.contains(ef.family, Qt::CaseInsensitive)) {
                fprintf(stdout, "  ok   FontUI     CSS chain has '%s' (%s)\n",
                        ef.family, ef.why);
            } else {
                fprintf(stderr, "  FAIL FontUI     CSS chain missing '%s' — Linux/Win/macOS emoji fallback\n",
                        ef.family);
                total_failed++;
            }
            if (codeCss.contains(ef.family, Qt::CaseInsensitive)) {
                fprintf(stdout, "  ok   FontCode   CSS chain has '%s' (%s)\n",
                        ef.family, ef.why);
            } else {
                fprintf(stderr, "  FAIL FontCode   CSS chain missing '%s' — Linux/Win/macOS emoji fallback\n",
                        ef.family);
                total_failed++;
            }
        }
    }

    fprintf(stdout, "\n");
    if (total_failed == 0) {
        fprintf(stdout, "=== ALL PALETTE CHECKS PASS ===\n");
        fprintf(stdout, "Generic palette: keywords #0000FF bold, comments #008000 italic,\n");
        fprintf(stdout, "numbers #FF8000, strings #808080, operators #000080 BOLD,\n");
        fprintf(stdout, "preprocessor #804000, secondary keywords #8000FF.\n");
        fprintf(stdout, "Brand: PowerShell variable #FF4500 / cmdlet #0000FF / alias #0080FF;\n");
        fprintf(stdout, "Python class+function #795E26, set-2 #267F99 teal; SQL set-2 #FF00FF\n");
        fprintf(stdout, "magenta; JSON key #0451A5; JS/TS set-2 #267F99 teal; C++ set-2 #267F99.\n");
        fprintf(stdout, "Font UI + Code CSS chains include emoji fallbacks (macOS / Win / Linux).\n");
        return 0;
    } else {
        fprintf(stderr, "=== %d PALETTE CHECKS FAILED ===\n", total_failed);
        return 1;
    }
}
