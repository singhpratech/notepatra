// test_lexers.cpp — verify every QScintilla lexer class instantiates and
// produces non-empty styling on a tiny test document.
//
// If a lexer is missing from the linked QScintilla build (the bug the user
// reported on Windows where .md/.sql/.json files showed no syntax highlighting)
// this test will either fail to LINK (compile-time check) or produce empty
// styling output (runtime check).
//
// Build: g++ -std=c++17 test_lexers.cpp -o test_lexers $(pkg-config --cflags --libs Qt5Widgets) -lqscintilla2_qt5
// Run:   ./test_lexers
// Exit code 0 = all lexers OK; non-zero = at least one is broken.

#include <QApplication>
#include <QString>
#include <QTextStream>
#include <Qsci/qsciscintilla.h>
#include <Qsci/qscilexer.h>

#include <Qsci/qscilexerpython.h>
#include <Qsci/qscilexerjavascript.h>
#include <Qsci/qscilexercpp.h>
#include <Qsci/qscilexerhtml.h>
#include <Qsci/qscilexercss.h>
#include <Qsci/qscilexersql.h>
#include <Qsci/qscilexerbash.h>
#include <Qsci/qscilexerjava.h>
#include <Qsci/qscilexerruby.h>
#include <Qsci/qscilexerperl.h>
#include <Qsci/qscilexerlua.h>
#include <Qsci/qscilexermarkdown.h>
#include <Qsci/qscilexerjson.h>
#include <Qsci/qscilexerxml.h>
#include <Qsci/qscilexeryaml.h>
#include <Qsci/qscilexercsharp.h>
#include <Qsci/qscilexerbatch.h>
#include <Qsci/qscilexerdiff.h>
#include <Qsci/qscilexermakefile.h>
#include <Qsci/qscilexercmake.h>
#include <Qsci/qscilexerpascal.h>

#include <vector>
#include <cstdio>

struct LexerCheck {
    const char *name;
    QsciLexer *(*make)(QObject *parent);
    const char *sample;
};

#define LX(N, CLS, SAMPLE) { #N, [](QObject *p) -> QsciLexer * { return new CLS(p); }, SAMPLE }

int main(int argc, char *argv[]) {
    QApplication app(argc, argv);

    std::vector<LexerCheck> checks = {
        LX(Python,     QsciLexerPython,     "def hello():\n    return 'world'\n"),
        LX(JavaScript, QsciLexerJavaScript, "function hello() { return 'world'; }\n"),
        LX(C++,        QsciLexerCPP,        "int main() { return 0; }\n"),
        LX(C#,         QsciLexerCSharp,     "class Foo { void Bar() {} }\n"),
        LX(Java,       QsciLexerJava,       "class Foo { void bar() {} }\n"),
        LX(HTML,       QsciLexerHTML,       "<html><body>hi</body></html>\n"),
        LX(CSS,        QsciLexerCSS,        "body { color: red; }\n"),
        LX(XML,        QsciLexerXML,        "<root><child/></root>\n"),
        LX(JSON,       QsciLexerJSON,       "{\"key\": \"value\", \"n\": 42}\n"),
        LX(SQL,        QsciLexerSQL,        "SELECT * FROM users WHERE id = 1;\n"),
        LX(Bash,       QsciLexerBash,       "#!/bin/bash\necho hello\n"),
        LX(Batch,      QsciLexerBatch,      "@echo off\necho hello\n"),
        LX(Ruby,       QsciLexerRuby,       "def hello\n  'world'\nend\n"),
        LX(Perl,       QsciLexerPerl,       "use strict; print 'hi';\n"),
        LX(Lua,        QsciLexerLua,        "function hello() return 'world' end\n"),
        LX(Markdown,   QsciLexerMarkdown,   "# Title\n\n- bullet\n\n```code```\n"),
        LX(YAML,       QsciLexerYAML,       "key: value\nlist:\n  - one\n  - two\n"),
        LX(Diff,       QsciLexerDiff,       "--- a\n+++ b\n@@ -1 +1 @@\n-old\n+new\n"),
        LX(Pascal,     QsciLexerPascal,     "program Hello; begin writeln('hi'); end.\n"),
        LX(CMake,      QsciLexerCMake,      "cmake_minimum_required(VERSION 3.16)\nproject(foo)\n"),
        LX(Makefile,   QsciLexerMakefile,   "all:\n\techo hello\n"),
    };

    int failed = 0;
    int passed = 0;

    for (const auto &c : checks) {
        QsciScintilla scin;
        QsciLexer *lex = c.make(&scin);
        if (!lex) {
            fprintf(stderr, "  FAIL %-12s: lexer constructor returned null\n", c.name);
            failed++;
            continue;
        }
        scin.setLexer(lex);
        scin.setText(c.sample);

        // The lexer language() should be non-empty for a real lexer.
        const char *langName = lex->language();
        if (!langName || !*langName) {
            fprintf(stderr, "  FAIL %-12s: lexer->language() is empty\n", c.name);
            failed++;
            continue;
        }

        // Force a colourise across the whole document. If the lexer is a
        // stub, this will silently do nothing — but the styleAt() check
        // below will tell us.
        int len = scin.length();
        scin.recolor(0, len);

        // Walk every byte and confirm AT LEAST one non-zero style appears.
        // Style 0 = default (everything unstyled). A working lexer will mark
        // keywords, strings, comments etc with style > 0 somewhere.
        int distinctStyles = 0;
        bool styles[256] = {false};
        for (int i = 0; i < len; i++) {
            int s = scin.SendScintilla(QsciScintilla::SCI_GETSTYLEAT, i);
            if (s >= 0 && s < 256 && !styles[s]) {
                styles[s] = true;
                distinctStyles++;
            }
        }

        if (distinctStyles < 2) {
            fprintf(stderr, "  FAIL %-12s: only %d distinct styles produced (expected >= 2). Lexer is a STUB.\n",
                    c.name, distinctStyles);
            failed++;
        } else {
            fprintf(stdout, "  ok   %-12s: %d distinct styles, language=%s\n",
                    c.name, distinctStyles, langName);
            passed++;
        }
    }

    fprintf(stdout, "\n=== %d/%d lexers OK ===\n", passed, passed + failed);
    return failed == 0 ? 0 : 1;
}
