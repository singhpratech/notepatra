// test_lexer_coverage.cpp — verify EVERY lexer style gets a non-default
// color on EVERY theme.
//
// User asked for "all program lexers to work simply — the keywords, cmdlets,
// or anything easily identified on all themes". This test enforces that:
// for every QsciLexer subclass we ship, applyNotepadPlusPalette must paint
// every non-Default style with a colour distinct from npText. If a style
// falls through to default text, the test reports the lexer + style index
// + description so we can add a matcher.
//
// Runs across Light, Dark, and Monokai themes. Strict mode: NO style may
// equal default text (with two known exceptions per language documented
// inline).

#include <QApplication>
#include <QColor>
#include <QFont>
#include <cstdio>
#include <vector>

#include "npp_palette.h"

#include <Qsci/qscilexer.h>
#include <Qsci/qscilexerbash.h>
#include <Qsci/qscilexerbatch.h>
#include <Qsci/qscilexercmake.h>
#include <Qsci/qscilexercpp.h>
#include <Qsci/qscilexercsharp.h>
#include <Qsci/qscilexercss.h>
#include <Qsci/qscilexerdiff.h>
#include <Qsci/qscilexerhtml.h>
#include <Qsci/qscilexerjava.h>
#include <Qsci/qscilexerjavascript.h>
#include <Qsci/qscilexerjson.h>
#include <Qsci/qscilexerlua.h>
#include <Qsci/qscilexermakefile.h>
#include <Qsci/qscilexermarkdown.h>
#include <Qsci/qscilexerpascal.h>
#include <Qsci/qscilexerperl.h>
#include <Qsci/qscilexerproperties.h>
#include <Qsci/qscilexerpython.h>
#include <Qsci/qscilexerruby.h>
#include <Qsci/qscilexersql.h>
#include <Qsci/qscilexerxml.h>
#include <Qsci/qscilexeryaml.h>

#include "lexer_powershell.h"
#include "lexer_rust.h"
#include "lexer_go.h"
#include "lexer_swift.h"
#include "lexer_kotlin.h"
#include "lexer_typescript.h"

static int g_total = 0;
static int g_gaps  = 0;

// Style descriptions that are intentionally allowed to render as default
// text. For each: (lexerName, descriptionLowercased) — these are styles
// that ARE just default text by convention, e.g. the "Default" style
// itself, or unstyled identifiers.
static bool isAllowedDefault(const QString &lexerName, const QString &descLower) {
    // The "Default" style itself MUST be default text — that's the whole
    // point of the style. Same for "Plain text", "Identifier" (in
    // languages where identifiers are intentionally unstyled like C++/
    // Python/Java per the v0.1.31 "all blue shades" fix).
    if (descLower == "default") return true;
    if (descLower == "plain text") return true;
    if (descLower == "html default") return true;
    if (descLower == "sgml default") return true;
    if (descLower == "sgml block default") return true;

    // Generic identifier intentionally falls to default text (per v0.1.31
    // — the fix for the "keywords + identifiers all look blue" complaint).
    // Exception: YAML "Identifier" is the KEY, not a generic identifier;
    // it must be styled.
    if (descLower == "identifier" && lexerName != "QsciLexerYAML") return true;

    // CSS "html default" sub-styles are inherited
    return false;
}

struct GapReport {
    QString lexerName;
    QString themeName;
    int     styleIndex;
    QString description;
    QColor  defaultText;
    QColor  got;
};

static std::vector<GapReport> g_reports;

// Run a single lexer against a single theme and find gaps.
static void check(QsciLexer *lex, const char *lexerName,
                  const QString &theme, QFont base,
                  QColor expectedDefaultText) {
    applyNotepadPlusPalette(lex, base, theme);
    for (int i = 0; i < 32; ++i) {
        QString desc = lex->description(i);
        if (desc.isEmpty()) continue;
        const QString d = desc.toLower();
        if (isAllowedDefault(QString::fromLatin1(lexerName), d)) continue;
        ++g_total;
        QColor got = lex->color(i);
        if (got == expectedDefaultText) {
            // Gap: this style was painted default text but isn't an
            // allowed exception. Record it for the report.
            ++g_gaps;
            g_reports.push_back({
                QString::fromLatin1(lexerName),
                theme,
                i,
                desc,
                expectedDefaultText,
                got
            });
        }
    }
}

int main(int argc, char *argv[]) {
    QApplication app(argc, argv);
    QFont base("Consolas", 11);

    // Default-text colour per theme (from npp_palette.cpp generic palette).
    const QColor lightDefault("#000000");
    const QColor darkDefault("#DCDCCC");
    const QColor monokaiDefault("#F8F8F2");

    fprintf(stdout, "=== Lexer coverage probe — %s ===\n",
            "Light + Dark + Monokai");

    // List of lexers to verify. For each: instantiate, run all 3 themes.
#define RUN(L, name) do { \
        L lex; \
        check(&lex, name, "Light",   base, lightDefault); \
        check(&lex, name, "Dark",    base, darkDefault); \
        check(&lex, name, "Monokai", base, monokaiDefault); \
    } while (0)

    RUN(QsciLexerBash,        "QsciLexerBash");
    RUN(QsciLexerBatch,       "QsciLexerBatch");
    RUN(QsciLexerCMake,       "QsciLexerCMake");
    RUN(QsciLexerCPP,         "QsciLexerCPP");
    RUN(QsciLexerCSharp,      "QsciLexerCSharp");
    RUN(QsciLexerCSS,         "QsciLexerCSS");
    RUN(QsciLexerDiff,        "QsciLexerDiff");
    RUN(QsciLexerHTML,        "QsciLexerHTML");
    RUN(QsciLexerJava,        "QsciLexerJava");
    RUN(QsciLexerJavaScript,  "QsciLexerJavaScript");
    RUN(QsciLexerJSON,        "QsciLexerJSON");
    RUN(QsciLexerLua,         "QsciLexerLua");
    RUN(QsciLexerMakefile,    "QsciLexerMakefile");
    RUN(QsciLexerMarkdown,    "QsciLexerMarkdown");
    RUN(QsciLexerPascal,      "QsciLexerPascal");
    RUN(QsciLexerPerl,        "QsciLexerPerl");
    RUN(QsciLexerProperties,  "QsciLexerProperties");
    RUN(QsciLexerPython,      "QsciLexerPython");
    RUN(QsciLexerRuby,        "QsciLexerRuby");
    RUN(QsciLexerSQL,         "QsciLexerSQL");
    RUN(QsciLexerXML,         "QsciLexerXML");
    RUN(QsciLexerYAML,        "QsciLexerYAML");

    // Notepatra-local lexers
    RUN(LexerPowerShell,      "LexerPowerShell");
    RUN(LexerRust,            "LexerRust");
    RUN(LexerGo,              "LexerGo");
    RUN(LexerSwift,           "LexerSwift");
    RUN(LexerKotlin,          "LexerKotlin");
    RUN(LexerTypeScript,      "LexerTypeScript");

#undef RUN

    fprintf(stdout, "\n--- Summary ---\n");
    fprintf(stdout, "Total non-default styles checked: %d\n", g_total);
    fprintf(stdout, "Gaps (style painted default text):  %d\n", g_gaps);

    if (g_gaps > 0) {
        fprintf(stdout, "\n--- Gaps ---\n");
        for (const auto &r : g_reports) {
            fprintf(stderr,
                    "  GAP %-22s [%-7s] style %2d \"%s\" → #%02X%02X%02X (default text)\n",
                    r.lexerName.toUtf8().constData(),
                    r.themeName.toUtf8().constData(),
                    r.styleIndex,
                    r.description.toUtf8().constData(),
                    r.got.red(), r.got.green(), r.got.blue());
        }
    }

    fprintf(stdout, "\n");
    if (g_gaps == 0) {
        fprintf(stdout, "=== ALL %d STYLES THEMED ===\n", g_total);
        return 0;
    } else {
        fprintf(stderr, "=== %d STYLE GAPS — palette must theme these ===\n", g_gaps);
        return 1;
    }
}
