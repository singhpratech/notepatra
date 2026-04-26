#include "npp_palette.h"

#include <QColor>
#include <QString>
#include <Qsci/qscilexer.h>

// ═══════════════════════════════════════════════════════════════════════
// Notepad++ default color palette — hewn directly from stylers.model.xml
// (light) and DarkModeDefault.xml (dark) shipped by notepad-plus-plus
// master (verified April 2026, v0.1.31 overhaul).
//
// PALETTE PHILOSOPHY: SEVEN+ clearly distinct HUES, never shades of one
// colour. v0.1.30 user complaint was "keywords + actual syntax are all
// just shades of blue". Root cause: identifiers were painted blue (#001080
// light / #9CDCFE dark) and operators stayed at default text colour, so
// keyword (blue) + identifier (blue) + class (teal-blue) blurred together.
// v0.1.31 fixes the three bugs at once:
//
//   1. IDENTIFIERS = default text colour (black/sand) — they read as
//      "names", not as keywords. This single change kills the "all blue"
//      effect on every C-family language.
//   2. OPERATORS = navy bold (light) / olive bold (dark) — distinct hue
//      AND distinct font weight, so + - = ( ) { } ; , don't blend with
//      identifier text.
//   3. KEYWORD2 (types) = vivid violet #8000FF (light) / sage #CEDF99
//      (dark) — N++ canonical, clearly NOT blue.
//
// LIGHT theme (paper #FFFFFF, text #000000):
//   #0000FF blue   bold   keywords (if/for/return/def/class)
//   #8000FF violet        types / secondary keywords (int, char, std::*)
//   #008000 green  italic comments
//   #FF8000 orange        numbers, decorators (italic)
//   #808080 grey          strings, characters
//   #000080 navy   bold   operators (+ - = ( ) { } ;)
//   #804000 brown         preprocessor (#include, #define)
//   #7F0000 maroon        class names, function names
//   #800080 magenta       regex literals
//   #FF0000 red           errors, unclosed strings
//   #000000 plain         identifiers, default text
//
// DARK theme (paper #1E1E1E, text #DCDCCC — Zenburn-derived hues from
// N++ DarkModeDefault.xml; readable against Notepatra's deeper paper):
//   #DFC47D sand   bold   keywords
//   #CEDF99 sage          types
//   #7F9F7F sage-g italic comments
//   #8CD0D3 cyan          numbers
//   #CC9393 rose          strings
//   #DCA3A3 dusty rose    characters
//   #9F9D6D olive  bold   operators
//   #FFCFAF peach         preprocessor
//   #DCDCAA s-yellow      class / function names
//   #93E0E3 l-cyan italic decorators / attributes
//   #D16969 brick         regex
//   #F44747 red           errors
//   #DCDCCC default       identifiers, default text
//
// Bold + italic provide a SECOND differentiation axis on top of hue —
// useful for users with reduced colour perception.
//
// Per-language brand overrides below tweak only the keyword + type
// accents for languages with a strong visual identity (Rust amber, Go
// cyan, Swift Xcode-pink, Kotlin Darcula-orange, Ruby red, Java IntelliJ
// navy, D coral, etc.). Every other language falls back to the generic
// 9-hue palette which already differentiates clearly without per-language
// tweaks — pruned in v0.1.31 to remove redundant overrides that just
// re-stated the generic blue/teal pattern.
// ═══════════════════════════════════════════════════════════════════════
void applyNotepadPlusPalette(QsciLexer *lexer, const QFont &baseFont, const QString &themeName) {
    if (!lexer) return;

    const bool dark = themeName.compare("Dark", Qt::CaseInsensitive) == 0;
    const bool monokai = themeName.compare("Monokai", Qt::CaseInsensitive) == 0;

    // Generic Notepad++ palette — applies to every lexer unless a per-language
    // override below replaces specific entries. Hex codes verified against
    // notepad-plus-plus/notepad-plus-plus master (PowerEditor/src/
    // stylers.model.xml + installer/themes/DarkModeDefault.xml).
    const QColor npPaper      = monokai ? QColor("#272822") : (dark ? QColor("#1E1E1E") : QColor("#FFFFFF"));
    const QColor npText       = monokai ? QColor("#F8F8F2") : (dark ? QColor("#DCDCCC") : QColor("#000000"));
    QColor npKeyword          = monokai ? QColor("#F92672") : (dark ? QColor("#DFC47D") : QColor("#0000FF"));
    QColor npKeyword2         = monokai ? QColor("#AE81FF") : (dark ? QColor("#CEDF99") : QColor("#8000FF"));
    const QColor npComment    = monokai ? QColor("#75715E") : (dark ? QColor("#7F9F7F") : QColor("#008000"));
    const QColor npNumber     = monokai ? QColor("#AE81FF") : (dark ? QColor("#8CD0D3") : QColor("#FF8000"));
    const QColor npString     = monokai ? QColor("#E6DB74") : (dark ? QColor("#CC9393") : QColor("#808080"));
    const QColor npChar       = monokai ? QColor("#E6DB74") : (dark ? QColor("#DCA3A3") : QColor("#808080"));
    const QColor npOperator   = monokai ? QColor("#F8F8F2") : (dark ? QColor("#9F9D6D") : QColor("#000080"));
    const QColor npPreproc    = monokai ? QColor("#66D9EF") : (dark ? QColor("#FFCFAF") : QColor("#804000"));
    const QColor npRegex      = monokai ? QColor("#FD971F") : (dark ? QColor("#D16969") : QColor("#800080"));
    QColor npClassName        = monokai ? QColor("#A6E22E") : (dark ? QColor("#DCDCAA") : QColor("#7F0000"));
    const QColor npDecorator  = monokai ? QColor("#FD971F") : (dark ? QColor("#93E0E3") : QColor("#FF8000"));
    const QColor npError      = monokai ? QColor("#F44747") : (dark ? QColor("#F44747") : QColor("#FF0000"));

    // ── PowerShell-specific styles (SCLEX_POWERSHELL adds these). ─────────
    // Variable ($var, $env:VAR), Cmdlet (Verb-Noun), Alias (ls, gci, ...).
    // Used as defaults for PowerShell lexer; other lexers don't have these
    // style kinds in their description() output so they're harmless.
    //
    // v0.1.32 — Microsoft PowerShell ISE canonical palette, verified against
    // ISE TokenColors + the official "PowerShell ISE" theme bundled with the
    // VS Code PowerShell extension (PowerShell/vscode-powershell repo,
    // themes/theme-psise/theme.json). The signature triple:
    //   $variable  = #FF4500 OrangeRed     (most distinctive — ISE signature)
    //   Get-Item   = #0000FF pure blue     (cmdlets always blue in ISE)
    //   ls / gci   = #0080FF lighter blue  (aliases distinct from cmdlets)
    QColor npVariable         = monokai ? QColor("#FD971F") : (dark ? QColor("#FF7F50") : QColor("#FF4500"));
    QColor npCmdlet           = monokai ? QColor("#A6E22E") : (dark ? QColor("#9CDCFE") : QColor("#0000FF"));
    QColor npAlias            = monokai ? QColor("#FD971F") : (dark ? QColor("#9CDCFE") : QColor("#0080FF"));
    QColor npHereString       = npString;

    // ── Per-language accent palette ───────────────────────────────────────
    // Identifies the language and tunes the keyword + type colours so each
    // language has a distinct visual identity. Keeps file recognition fast.
    const QString lang = QString::fromLatin1(lexer->language() ? lexer->language() : "");

    if (lang == QLatin1String("Rust")) {
        // rust-analyzer / IntelliJ Rust default — orange-amber for keywords
        // (matches the Rust logo gradient) and teal-cyan for types.
        npKeyword   = monokai ? QColor("#FD971F") : (dark ? QColor("#DEA584") : QColor("#CE422B"));
        npKeyword2  = monokai ? QColor("#A6E22E") : (dark ? QColor("#4EC9B0") : QColor("#267F99"));
        npClassName = monokai ? QColor("#A6E22E") : (dark ? QColor("#4EC9B0") : QColor("#267F99"));
    } else if (lang == QLatin1String("Go")) {
        // go.dev playground / VS Code Go ext — Go-logo cyan #00ADD8 for
        // keywords pops on both themes; lighter cyan for types.
        npKeyword   = QColor("#00ADD8");
        npKeyword2  = monokai ? QColor("#A6E22E") : QColor("#5DC9E2");
        npClassName = monokai ? QColor("#A6E22E") : QColor("#5DC9E2");
    } else if (lang == QLatin1String("Swift")) {
        // Xcode default — pink keywords on dark, magenta on light, lavender
        // for stdlib types. Swift-orange (#F05138) reserved for attributes.
        npKeyword   = monokai ? QColor("#F92672") : (dark ? QColor("#FC5FA3") : QColor("#AD3DA4"));
        npKeyword2  = monokai ? QColor("#AE81FF") : (dark ? QColor("#D0A8FF") : QColor("#703DAA"));
        npClassName = QColor("#F05138"); // Swift orange for class/attribute
    } else if (lang == QLatin1String("Kotlin")) {
        // IntelliJ Light + Darcula — Darcula orange #CC7832 for keywords (the
        // "Kotlin orange" devs recognise from IntelliJ), gold #FFC66D for
        // stdlib types in dark, dark-navy in light.
        npKeyword   = monokai ? QColor("#FD971F") : (dark ? QColor("#CC7832") : QColor("#0033B3"));
        npKeyword2  = monokai ? QColor("#A6E22E") : (dark ? QColor("#FFC66D") : QColor("#000080"));
        npClassName = monokai ? QColor("#A6E22E") : (dark ? QColor("#FFC66D") : QColor("#000080"));
    } else if (lang == QLatin1String("Python")) {
        // VS Code Dark+ canonical for Python — the most-used Python theme on
        // earth. Keywords stay blue (consensus across N++ Light + VS Code
        // Light/Dark+); types and built-ins shift to teal so `print`/`len`/
        // `int` stand out from the keyword-blue. Class/function names get
        // amber so def NAME pops out — same convention as VS Code, PyCharm.
        // Decorators (@property, @dataclass) inherit the generic
        // npDecorator (orange italic) which already gives them their own
        // hue independent of keyword + type.
        npKeyword2  = monokai ? QColor("#66D9EF") : (dark ? QColor("#4EC9B0") : QColor("#267F99"));
        npClassName = monokai ? QColor("#A6E22E") : (dark ? QColor("#DCDCAA") : QColor("#795E26"));
    }
    else if (lang == QLatin1String("SQL")) {
        // SSMS / Azure Data Studio canonical — keywords blue (SELECT, FROM,
        // WHERE, JOIN), keyword2 MAGENTA for system functions and data
        // types (COUNT, SUM, INT, VARCHAR, DATETIME). The blue+magenta
        // combination is the SSMS signature — instantly recognisable as SQL.
        // VS Code SQL Server (mssql) extension uses a similar pattern.
        npKeyword2  = monokai ? QColor("#AE81FF") : (dark ? QColor("#C586C0") : QColor("#FF00FF"));
        npClassName = monokai ? QColor("#A6E22E") : (dark ? QColor("#DCDCAA") : QColor("#7F0000"));
    }
    else if (lang == QLatin1String("JavaScript") ||
             lang == QLatin1String("TypeScript") ||
             lang == QLatin1String("CoffeeScript")) {
        // VS Code Dark+ canonical for JS/TS — already the de-facto industry
        // default. Keywords blue, types teal `#4EC9B0`, function/class names
        // amber `#DCDCAA`. Light theme uses the VS Code Light+ analog.
        npKeyword2  = monokai ? QColor("#66D9EF") : (dark ? QColor("#4EC9B0") : QColor("#267F99"));
        npClassName = monokai ? QColor("#A6E22E") : (dark ? QColor("#DCDCAA") : QColor("#795E26"));
    }
    else if (lang == QLatin1String("C") || lang == QLatin1String("C++") ||
             lang == QLatin1String("C#")) {
        // VS Code C/C++ extension defaults — keywords blue, types teal,
        // function/class names amber, preprocessor brown (light) / peach
        // (dark) — already in the generic palette. Just tighten secondary
        // keywords (types like int, char, std::string) to the VS Code teal.
        npKeyword2  = monokai ? QColor("#66D9EF") : (dark ? QColor("#4EC9B0") : QColor("#267F99"));
        npClassName = monokai ? QColor("#A6E22E") : (dark ? QColor("#DCDCAA") : QColor("#795E26"));
    }
    else if (lang == QLatin1String("Bash")) {
        // Bash / shell — stay close to VS Code's shellscript scope. Keywords
        // blue (if/then/for/while), built-ins also blue (echo/cd/read).
        // Variables (`$var`, `${var}`) get the SCALAR style which the
        // generic identifier matcher catches as plain text — fine. Operators
        // and pipes get the operator navy bold treatment.
        npKeyword2  = monokai ? QColor("#A6E22E") : (dark ? QColor("#CEDF99") : QColor("#8000FF"));
    }
    else if (lang == QLatin1String("Batch") ||
             lang == QLatin1String("Perl") || lang == QLatin1String("Lua") ||
             lang == QLatin1String("Pascal") || lang == QLatin1String("Makefile") ||
             lang == QLatin1String("YAML") ||
             lang == QLatin1String("PowerShell") ||
             lang == QLatin1String("TCL") || lang == QLatin1String("Diff") ||
             lang == QLatin1String("Fortran") || lang == QLatin1String("Fortran77") ||
             lang == QLatin1String("Matlab") || lang == QLatin1String("Octave") ||
             lang == QLatin1String("IDL") || lang == QLatin1String("Verilog") ||
             lang == QLatin1String("VHDL") || lang == QLatin1String("TeX") ||
             lang == QLatin1String("PostScript") || lang == QLatin1String("POV") ||
             lang == QLatin1String("Spice") || lang == QLatin1String("AVS") ||
             lang == QLatin1String("Properties") || lang == QLatin1String("PO") ||
             lang == QLatin1String("IntelHex") || lang == QLatin1String("SRecord") ||
             lang == QLatin1String("ASM") || lang == QLatin1String("NASM") ||
             lang == QLatin1String("MASM")) {
        // Generic-palette languages — fall through to the canonical N++
        // 9-hue palette set above (blue keyword + violet type + green
        // comment + orange number + grey string + navy operator + brown
        // preprocessor + maroon class + plain identifier). PowerShell sits
        // here because its distinctive hues come from the PS-specific
        // npVariable / npCmdlet / npAlias style matchers — not from the
        // generic keyword/type colours.
    }
    // ── Per-language brand overrides (kept for visual identity) ──────────
    // Below: only languages where a brand-specific accent gives a stronger
    // visual identity than the generic palette. Other languages use the
    // generic palette. Skipping a language falls through to generic — also
    // perfectly fine.
    else if (lang == QLatin1String("D")) {
        // dlang.org + dub. Light theme keeps red-brick accent; dark theme
        // uses brighter coral so it's actually readable on #1E1E1E.
        npKeyword   = monokai ? QColor("#F92672") : (dark ? QColor("#FF6E6E") : QColor("#B03A2E"));
        npKeyword2  = monokai ? QColor("#A6E22E") : (dark ? QColor("#CEDF99") : QColor("#8000FF"));
    }
    else if (lang == QLatin1String("Java")) {
        // IntelliJ Light (navy keywords) + Darcula (orange) — JetBrains brand.
        npKeyword   = monokai ? QColor("#FD971F") : (dark ? QColor("#CC7832") : QColor("#0033B3"));
        npKeyword2  = monokai ? QColor("#A6E22E") : (dark ? QColor("#FFC66D") : QColor("#000080"));
    }
    else if (lang == QLatin1String("HTML") || lang == QLatin1String("PHP")) {
        // N++ canonical — tags blue (#0000FF), attributes red (#FF0000),
        // strings violet bold. Already ironically distinct on light; on dark
        // we use sand/sage/peach from Zenburn for clear differentiation.
        npKeyword   = monokai ? QColor("#F92672") : (dark ? QColor("#E3CEAB") : QColor("#0000FF"));
        npKeyword2  = monokai ? QColor("#A6E22E") : (dark ? QColor("#DFDFDF") : QColor("#FF0000"));
    }
    else if (lang == QLatin1String("CSS")) {
        // CSS @rules + selectors get a distinctive violet; properties red.
        npKeyword   = monokai ? QColor("#F92672") : (dark ? QColor("#CEDF99") : QColor("#8000FF"));
        npKeyword2  = monokai ? QColor("#A6E22E") : (dark ? QColor("#DFDFDF") : QColor("#FF0000"));
    }
    else if (lang == QLatin1String("XML")) {
        // XML tag names blue (N++ canonical for <tag>); attributes red.
        npKeyword   = monokai ? QColor("#F92672") : (dark ? QColor("#E3CEAB") : QColor("#0000FF"));
    }
    else if (lang == QLatin1String("JSON")) {
        // VS Code Dark+ canonical for JSON — the de-facto JSON expectation.
        //   Light: key #0451A5 darker JSON-blue, value-string maroon
        //          (handled by npString), keyword (true/false/null) #0000FF
        //   Dark:  key #9CDCFE light-blue (VS Code variable colour), keyword
        //          (true/false/null) #569CD6 (VS Code constant.language)
        // Distinctly different visual from C/C++ even though both use blues.
        npKeyword   = monokai ? QColor("#F92672") : (dark ? QColor("#569CD6") : QColor("#0000FF"));
        npKeyword2  = monokai ? QColor("#66D9EF") : (dark ? QColor("#9CDCFE") : QColor("#0451A5"));
    }
    else if (lang == QLatin1String("Ruby")) {
        // Ruby red — matches ruby-lang.org branding. Light keeps the brand
        // red; dark uses VS Code's softer salmon #FF7B72 so it doesn't burn
        // the eyes against #1E1E1E.
        npKeyword   = monokai ? QColor("#F92672") : (dark ? QColor("#FF7B72") : QColor("#CC342D"));
        npKeyword2  = monokai ? QColor("#A6E22E") : (dark ? QColor("#CEDF99") : QColor("#8000FF"));
    }
    else if (lang == QLatin1String("Markdown")) {
        // Markdown — emphasised headers (npKeyword bold), italic emphasis,
        // links in cyan-teal. Light: blue headers + maroon emphasis (N++).
        npKeyword   = monokai ? QColor("#A6E22E") : (dark ? QColor("#DFC47D") : QColor("#0000FF"));
        npKeyword2  = monokai ? QColor("#FD971F") : (dark ? QColor("#CC9393") : QColor("#A31515"));
    }
    else if (lang == QLatin1String("CMake")) {
        // CMake — blue commands, olive function names (matches CMake docs).
        npKeyword   = monokai ? QColor("#F92672") : (dark ? QColor("#DFC47D") : QColor("#0000FF"));
        npKeyword2  = monokai ? QColor("#FD971F") : (dark ? QColor("#DCDCAA") : QColor("#795E26"));
    }

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

        // ── PowerShell-specific style kinds (checked first so they take
        //    precedence over the generic "keyword" / "function" matches). ──
        if (d.contains("variable")) {
            fg = npVariable;
        }
        else if (d.contains("cmdlet")) {
            fg = npCmdlet;
            lexer->setFont(bold, i);
        }
        else if (d == "alias" || d.contains("alias ") || d.endsWith(" alias")) {
            fg = npAlias;
        }
        else if (d.contains("here-string") || d.contains("here string") ||
                 d.contains("here-character") || d.contains("here character")) {
            fg = npHereString;
        }
        else if (d.contains("comment doc keyword") ||
                 d.contains("commentdockeyword") ||
                 d.contains("comment-doc keyword")) {
            // Doc-comment tags like .SYNOPSIS, .PARAMETER, @param, @return,
            // - Parameter, - Returns -- shown in a distinct dim colour.
            fg = monokai ? QColor("#66D9EF") : (dark ? QColor("#608B4E") : QColor("#3F5FBF"));
            lexer->setFont(italic, i);
        }
        // ── Generic style kinds (cover most lexers). ──
        else if (d.contains("keyword")) {
            // Secondary keyword sets — types/std-lib — get the secondary
            // colour to distinguish from primary keywords.
            if (d.contains("set 2") || d.contains("set2") ||
                d.contains("secondary") || d.contains("user") ||
                d.contains("user-defined")) {
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
            fg = npPreproc;
        }
        else if (d.contains("operator")) {
            // N++ canonical: operators painted bold in their distinctive
            // hue (navy on light, olive on dark). The bold weight gives a
            // second differentiation axis so + - = ( ) { } ; do not blend
            // with identifier text.
            fg = npOperator;
            lexer->setFont(bold, i);
        }
        else if (d.contains("decorator") || d.contains("attribute")) {
            fg = npDecorator;
        }
        else if (d.contains("class") || d.contains("function") ||
                 d.contains("method") || d.contains("global")) {
            fg = npClassName;
        }
        else if (d.contains("error") || d.contains("unclosed")) {
            fg = npError;
        }
        else if (d.contains("tag") || d.contains("element")) {
            fg = npKeyword;
        }
        else if (d.contains("entity")) {
            fg = npNumber;
        }
        else if (d.contains("header") || d.contains("header1") ||
                 d.contains("strong") || d.contains("bold")) {
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
        else if (d.contains("property")) {
            // JSON property keys (`"name":`), CSS property names (`color:`),
            // YAML keys, TOML keys etc. Use npKeyword2 so the per-language
            // brand override controls the colour — JSON gets its
            // `#0451A5` JSON-blue, CSS gets its red, etc.
            fg = npKeyword2;
        }
        else if (d.contains("identifier")) {
            // v0.1.31 fix: identifiers paint as default text colour instead
            // of an accent blue. Painting identifiers blue (#001080 light /
            // #9CDCFE dark) was the root cause of the v0.1.30 user complaint
            // that "keywords + actual syntax are all just shades of blue" —
            // identifier text and keyword text both rendered blue. Letting
            // identifiers stay default-coloured (black on light, sand-grey
            // on dark) makes them read as "names", clearly distinct from
            // the bold-blue keywords. Notepad++ canonical behaviour.
            fg = npText;
        }
        // else: plain text — keep npText

        lexer->setColor(fg, i);
    }

    // Also set the default style (0) explicitly in case lexer skips it
    lexer->setPaper(npPaper, 0);
    lexer->setColor(npText, 0);
    lexer->setFont(regular, 0);
}
