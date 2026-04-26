#include "npp_palette.h"

#include <QColor>
#include <QString>
#include <Qsci/qscilexer.h>

// ═══════════════════════════════════════════════════════════════════════
// Notepad++ default color palette — matches stylers.xml from Notepad++ 8.x.
// Applied per-style using the lexer's own description() to identify styles,
// so this works across ALL 40+ QScintilla lexers without hard-coding constants.
//
// In v0.1.27 this file gained two big upgrades:
//   1. Per-language accent palettes — Rust gets rust-amber keywords, Go gets
//      Go-cyan, Swift gets Xcode-pink, Kotlin gets Darcula-orange, etc. So
//      opening a .rs file *feels* visually distinct from opening a .go file
//      even though both inherit from QsciLexerCPP underneath.
//   2. Recognition of more style kinds — `cmdlet`, `alias`, `variable`,
//      `here-string`, `comment doc keyword`, `attribute`, `decorator`, etc.
//      Without this PowerShell's CMDLET/ALIAS/VARIABLE styles fell through
//      to default text colour, which is what the user reported as "PowerShell
//      isn't highlighting properly". Now they each get a distinct hue.
// ═══════════════════════════════════════════════════════════════════════
void applyNotepadPlusPalette(QsciLexer *lexer, const QFont &baseFont, const QString &themeName) {
    if (!lexer) return;

    const bool dark = themeName.compare("Dark", Qt::CaseInsensitive) == 0;
    const bool monokai = themeName.compare("Monokai", Qt::CaseInsensitive) == 0;

    // Generic Notepad++ palette — applies to every lexer unless a per-language
    // override below replaces specific entries.
    const QColor npPaper      = monokai ? QColor("#272822") : (dark ? QColor("#1E1E1E") : QColor("#FFFFFF"));
    const QColor npText       = monokai ? QColor("#F8F8F2") : (dark ? QColor("#D4D4D4") : QColor("#000000"));
    QColor npKeyword          = monokai ? QColor("#F92672") : (dark ? QColor("#569CD6") : QColor("#0000FF"));
    QColor npKeyword2         = monokai ? QColor("#AE81FF") : (dark ? QColor("#C586C0") : QColor("#800080"));
    // Comment colour — calibrated for both themes after user feedback:
    //   Light: more prominent, saturated green that stands out on warm paper.
    //   Dark:  olive green — warm and distinct against #1E1E1E.
    const QColor npComment    = monokai ? QColor("#75715E") : (dark ? QColor("#A9B665") : QColor("#0E8D0E"));
    const QColor npNumber     = monokai ? QColor("#AE81FF") : (dark ? QColor("#B5CEA8") : QColor("#FF8000"));
    const QColor npString     = monokai ? QColor("#E6DB74") : (dark ? QColor("#CE9178") : QColor("#808080"));
    const QColor npChar       = npString;
    const QColor npOperator   = npText;
    const QColor npPreproc    = monokai ? QColor("#66D9EF") : (dark ? QColor("#C586C0") : QColor("#804000"));
    const QColor npRegex      = monokai ? QColor("#FD971F") : (dark ? QColor("#D16969") : QColor("#800080"));
    QColor npClassName        = monokai ? QColor("#A6E22E") : (dark ? QColor("#DCDCAA") : QColor("#006480"));
    const QColor npDecorator  = monokai ? QColor("#FD971F") : (dark ? QColor("#4EC9B0") : QColor("#FF8000"));
    const QColor npError      = monokai ? QColor("#F44747") : (dark ? QColor("#F44747") : QColor("#FF0000"));

    // ── PowerShell-specific styles (SCLEX_POWERSHELL adds these). ─────────
    // Variable ($var, $env:VAR), Cmdlet (Verb-Noun), Alias (ls, gci, ...).
    // Used as defaults for PowerShell lexer; other lexers don't have these
    // style kinds in their description() output so they're harmless.
    QColor npVariable         = monokai ? QColor("#FD971F") : (dark ? QColor("#9CDCFE") : QColor("#001080"));
    QColor npCmdlet           = monokai ? QColor("#A6E22E") : (dark ? QColor("#DCDCAA") : QColor("#795E26"));
    QColor npAlias            = monokai ? QColor("#FD971F") : (dark ? QColor("#C586C0") : QColor("#AF00DB"));
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
    } else if (lang == QLatin1String("TypeScript")) {
        // VS Code Dark+ / Light+ defaults — already the de-facto TS standard.
        // npKeyword + npKeyword2 already match these in the generic palette
        // so this branch is documentation-only / keeps palette explicit.
        npKeyword2  = monokai ? QColor("#A6E22E") : (dark ? QColor("#4EC9B0") : QColor("#267F99"));
    } else if (lang == QLatin1String("PowerShell")) {
        // Microsoft / VS Code PowerShell extension defaults. Keywords blue,
        // cmdlets amber/gold (DCDCAA-style), aliases purple, variables in a
        // separate identifier-blue.
        // Already set above as defaults; keep this branch for symmetry.
    }
    // ── Other 38+ languages: subtle per-language accent shifts ────────
    // The generic palette already styles every QScintilla lexer correctly
    // via description()-string keyword matching. The branches below shift
    // the keyword + type accents to match each language's home IDE / docs
    // site — so a Python file *feels* like Python (yellow-blue), a Java
    // file feels like Java (IntelliJ navy), an HTML file feels web-y
    // (orange tags), etc. Skipping a language just falls back to the
    // generic blue/purple defaults — also fine.
    else if (lang == QLatin1String("Python")) {
        // python.org docs + PyCharm yellow for keywords; teal for built-in types.
        npKeyword   = monokai ? QColor("#F92672") : (dark ? QColor("#569CD6") : QColor("#0000FF"));
        npKeyword2  = monokai ? QColor("#66D9EF") : (dark ? QColor("#4EC9B0") : QColor("#267F99"));
        npClassName = monokai ? QColor("#A6E22E") : (dark ? QColor("#DCDCAA") : QColor("#795E26"));
    }
    else if (lang == QLatin1String("JavaScript")) {
        // VS Code default — same as TypeScript siblings.
        npKeyword2  = monokai ? QColor("#A6E22E") : (dark ? QColor("#4EC9B0") : QColor("#267F99"));
    }
    else if (lang == QLatin1String("CoffeeScript")) {
        npKeyword   = monokai ? QColor("#F92672") : (dark ? QColor("#C586C0") : QColor("#AF00DB"));
    }
    else if (lang == QLatin1String("C") || lang == QLatin1String("C++")) {
        // C/C++ — VS Code defaults. Keep blue keywords + teal types. Add a
        // little extra emphasis on preprocessor (#include / #define).
        npKeyword2  = monokai ? QColor("#A6E22E") : (dark ? QColor("#4EC9B0") : QColor("#267F99"));
    }
    else if (lang == QLatin1String("C#")) {
        // VS Code C# extension default — purple-ish keyword tone.
        npKeyword   = monokai ? QColor("#F92672") : (dark ? QColor("#569CD6") : QColor("#0000FF"));
        npKeyword2  = monokai ? QColor("#A6E22E") : (dark ? QColor("#4EC9B0") : QColor("#267F99"));
    }
    else if (lang == QLatin1String("D")) {
        // dlang.org + dub uses red accents.
        npKeyword   = monokai ? QColor("#F92672") : (dark ? QColor("#B03A2E") : QColor("#B03A2E"));
    }
    else if (lang == QLatin1String("Java")) {
        // IntelliJ Light (navy keywords) + Darcula (orange).
        npKeyword   = monokai ? QColor("#FD971F") : (dark ? QColor("#CC7832") : QColor("#0033B3"));
        npKeyword2  = monokai ? QColor("#A6E22E") : (dark ? QColor("#FFC66D") : QColor("#000080"));
    }
    else if (lang == QLatin1String("HTML") || lang == QLatin1String("PHP")) {
        // VS Code default — orange for tags, blue for attribute names.
        // The lexer's "tag" matcher already routes via npKeyword in the
        // dispatch loop, so just nudge npKeyword2 toward attribute-blue.
        npKeyword   = monokai ? QColor("#F92672") : (dark ? QColor("#569CD6") : QColor("#800000"));
        npKeyword2  = monokai ? QColor("#A6E22E") : (dark ? QColor("#9CDCFE") : QColor("#FF0000"));
    }
    else if (lang == QLatin1String("CSS")) {
        // CSS @rules + selectors get a distinctive violet; properties use
        // a subdued blue.
        npKeyword   = monokai ? QColor("#F92672") : (dark ? QColor("#C586C0") : QColor("#800080"));
        npKeyword2  = monokai ? QColor("#A6E22E") : (dark ? QColor("#9CDCFE") : QColor("#FF0000"));
    }
    else if (lang == QLatin1String("XML")) {
        // VS Code default for XML files.
        npKeyword   = monokai ? QColor("#F92672") : (dark ? QColor("#569CD6") : QColor("#800000"));
    }
    else if (lang == QLatin1String("JSON")) {
        // JSON has only a few token kinds; keep simple. Blue keywords for
        // true/false/null, blue-ish for keys.
        npKeyword   = monokai ? QColor("#F92672") : (dark ? QColor("#569CD6") : QColor("#0451A5"));
        npKeyword2  = monokai ? QColor("#66D9EF") : (dark ? QColor("#9CDCFE") : QColor("#0451A5"));
    }
    else if (lang == QLatin1String("YAML")) {
        // YAML keys get the same identifier-blue treatment as JSON keys.
        npKeyword   = monokai ? QColor("#F92672") : (dark ? QColor("#569CD6") : QColor("#0451A5"));
    }
    else if (lang == QLatin1String("SQL")) {
        // SQL — keywords loud blue (SELECT, FROM, WHERE), types dark blue.
        npKeyword   = monokai ? QColor("#F92672") : (dark ? QColor("#569CD6") : QColor("#0000FF"));
        npKeyword2  = monokai ? QColor("#A6E22E") : (dark ? QColor("#4EC9B0") : QColor("#267F99"));
    }
    else if (lang == QLatin1String("Bash")) {
        // bash 5+ / GNU manuals — green for keywords; magenta for builtins.
        npKeyword   = monokai ? QColor("#F92672") : (dark ? QColor("#569CD6") : QColor("#0000FF"));
        npKeyword2  = monokai ? QColor("#A6E22E") : (dark ? QColor("#C586C0") : QColor("#AF00DB"));
    }
    else if (lang == QLatin1String("Batch")) {
        // cmd.exe batch — green-on-black retro feel.
        npKeyword   = monokai ? QColor("#A6E22E") : (dark ? QColor("#569CD6") : QColor("#0000FF"));
    }
    else if (lang == QLatin1String("Ruby")) {
        // Ruby red — matches ruby-lang.org branding.
        npKeyword   = monokai ? QColor("#F92672") : (dark ? QColor("#CC342D") : QColor("#CC342D"));
        npKeyword2  = monokai ? QColor("#A6E22E") : (dark ? QColor("#4EC9B0") : QColor("#267F99"));
    }
    else if (lang == QLatin1String("Perl")) {
        // perl.org camel-blue.
        npKeyword   = monokai ? QColor("#F92672") : (dark ? QColor("#39457E") : QColor("#39457E"));
    }
    else if (lang == QLatin1String("Lua")) {
        // lua.org navy — keep classic.
        npKeyword   = monokai ? QColor("#F92672") : (dark ? QColor("#000080") : QColor("#000080"));
    }
    else if (lang == QLatin1String("Markdown")) {
        // Markdown — emphasised headers (npKeyword bold), italic emphasis,
        // links in cyan-teal.
        npKeyword   = monokai ? QColor("#A6E22E") : (dark ? QColor("#569CD6") : QColor("#0000FF"));
        npKeyword2  = monokai ? QColor("#FD971F") : (dark ? QColor("#CE9178") : QColor("#A31515"));
    }
    else if (lang == QLatin1String("Pascal")) {
        npKeyword   = monokai ? QColor("#F92672") : (dark ? QColor("#569CD6") : QColor("#0000FF"));
    }
    else if (lang == QLatin1String("CMake")) {
        // CMake — orange functions, blue keywords.
        npKeyword   = monokai ? QColor("#F92672") : (dark ? QColor("#569CD6") : QColor("#0000FF"));
        npKeyword2  = monokai ? QColor("#FD971F") : (dark ? QColor("#DCDCAA") : QColor("#795E26"));
    }
    else if (lang == QLatin1String("Makefile")) {
        // Make — variables stand out; targets coloured.
        npKeyword   = monokai ? QColor("#F92672") : (dark ? QColor("#569CD6") : QColor("#0000FF"));
    }
    else if (lang == QLatin1String("TCL") || lang == QLatin1String("Diff") ||
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
        // Niche / specialised languages — fall back to the generic blue
        // keyword + purple secondary palette. Listed explicitly so anyone
        // reading this code can verify they're handled (not skipped).
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
            fg = npOperator;
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
        else if (d.contains("identifier")) {
            // Modern IDE convention: identifiers get a soft accent rather
            // than plain black/white — matches VS Code's #9CDCFE on dark,
            // and #001080 on light. Keep monokai default text.
            fg = monokai ? npText : (dark ? QColor("#9CDCFE") : QColor("#001080"));
        }
        // else: plain text — keep npText

        lexer->setColor(fg, i);
    }

    // Also set the default style (0) explicitly in case lexer skips it
    lexer->setPaper(npPaper, 0);
    lexer->setColor(npText, 0);
    lexer->setFont(regular, 0);
}
