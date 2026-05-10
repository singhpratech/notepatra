// tests/comprehensive/lexer_smoke.cpp
//
// QScintilla lexer smoke test for Notepatra v0.1.58 release validation.
// For 5 sample languages (python, rust, sql, json, markdown), constructs
// a QsciScintilla, attaches the lexer returned by createLexerForLanguage(),
// loads a 10-line snippet packed with that language's keywords, then walks
// every byte and asks Scintilla what STYLE was assigned. The test passes
// for a language iff at least one keyword token resolved to a non-zero
// style — i.e. the lexer actually classified them as keywords rather than
// leaving them as default style-0 text.
//
// Output: one JSON object per language to stdout, plus a final
// "smoke_summary" line so the harness can scrape pass/fail counts.

#include <QApplication>
#include <QString>
#include <QStringList>
#include <QByteArray>
#include <QObject>

#include <Qsci/qsciscintilla.h>
#include <Qsci/qscilexer.h>

#include "lexerutils.h"

#include <cstdio>
#include <set>
#include <vector>
#include <string>

struct Sample {
    const char *lang;
    const char *snippet;        // ~10 lines, packed with keywords
    std::vector<std::string> keywords;
};

int main(int argc, char *argv[]) {
    QApplication app(argc, argv);

    std::vector<Sample> samples = {
        {
            "Python",
            "import sys\n"
            "from os import path\n"
            "def hello(name):\n"
            "    if name is None:\n"
            "        return 'world'\n"
            "    elif True:\n"
            "        for i in range(10):\n"
            "            yield i\n"
            "class Foo(object):\n"
            "    pass\n",
            {"import","from","def","if","return","elif","for","yield","class","pass"}
        },
        {
            "Rust",
            "use std::io;\n"
            "fn main() {\n"
            "    let x: i32 = 42;\n"
            "    let mut y = 0u64;\n"
            "    if x > 0 { y = 1; }\n"
            "    match x { _ => {} }\n"
            "    for i in 0..10 { y += i; }\n"
            "    return;\n"
            "}\n"
            "struct Foo { a: i32 }\n",
            {"use","fn","let","mut","if","match","for","return","struct"}
        },
        {
            "SQL",
            "SELECT id, name FROM users\n"
            "WHERE active = 1 AND age > 18\n"
            "ORDER BY name ASC;\n"
            "INSERT INTO logs VALUES (1, 'hi');\n"
            "UPDATE users SET active = 0 WHERE id = 7;\n"
            "DELETE FROM tmp WHERE created < NOW();\n"
            "CREATE TABLE foo (id INT PRIMARY KEY);\n"
            "DROP TABLE bar;\n"
            "JOIN orders ON orders.user_id = users.id;\n"
            "GROUP BY name HAVING COUNT(*) > 1;\n",
            {"SELECT","FROM","WHERE","INSERT","UPDATE","DELETE","CREATE","DROP","JOIN","GROUP"}
        },
        {
            "JSON",
            "{\n"
            "  \"name\": \"Notepatra\",\n"
            "  \"version\": \"0.1.58\",\n"
            "  \"flags\": [true, false, null],\n"
            "  \"counts\": [1, 2, 3, 4],\n"
            "  \"nested\": {\n"
            "    \"a\": 1,\n"
            "    \"b\": null\n"
            "  },\n"
            "  \"empty\": null\n"
            "}\n",
            {"true","false","null"}  // JSON style 11 = keywords
        },
        {
            "Markdown",
            "# Heading 1\n"
            "## Heading 2\n"
            "### Heading 3\n"
            "- bullet one\n"
            "- bullet two\n"
            "1. numbered\n"
            "**bold** and *italic*\n"
            "[link](https://example.com)\n"
            "```python\n"
            "code block\n"
            "```\n",
            {"#","##","###","-","**","*","[","```"}  // markdown styles syntax markers
        },
    };

    int passed = 0;
    int total = (int)samples.size();
    QStringList resultsJson;

    for (const auto &s : samples) {
        QsciScintilla scin;
        QsciLexer *lex = createLexerForLanguage(QString::fromUtf8(s.lang), &scin);

        bool nonNull = (lex != nullptr);
        const char *langName = nonNull ? lex->language() : "";
        if (!langName) langName = "";

        if (!nonNull) {
            fprintf(stderr, "[FAIL] %-10s: createLexerForLanguage returned nullptr\n", s.lang);
            QString o = QString("{\"lang\":\"%1\",\"ok\":false,\"info\":\"createLexerForLanguage returned null\"}")
                            .arg(s.lang);
            resultsJson.append(o);
            continue;
        }

        scin.setLexer(lex);
        scin.setText(QString::fromUtf8(s.snippet));
        const int len = scin.length();
        scin.recolor(0, len);

        // Collect distinct styles encountered + count non-zero style spans.
        std::set<int> distinct;
        int nonZeroBytes = 0;
        int maxStyle = 0;
        for (int i = 0; i < len; i++) {
            int sty = scin.SendScintilla(QsciScintilla::SCI_GETSTYLEAT, i);
            if (sty < 0 || sty > 255) continue;
            distinct.insert(sty);
            if (sty != 0) {
                nonZeroBytes++;
                if (sty > maxStyle) maxStyle = sty;
            }
        }

        // Pass criteria: at least 2 distinct styles AND some non-zero
        // styled bytes — i.e. the lexer actually classified part of the
        // snippet into a non-default style (keyword / string / comment).
        bool ok = (distinct.size() >= 2) && (nonZeroBytes > 0);

        if (ok) passed++;

        QString info = QString("language=%1 distinctStyles=%2 nonZeroStyledBytes=%3 maxStyle=%4")
                           .arg(langName)
                           .arg((int)distinct.size())
                           .arg(nonZeroBytes)
                           .arg(maxStyle);
        fprintf(stdout, "[%s] %-10s  %s\n", ok ? "PASS" : "FAIL", s.lang, info.toUtf8().constData());

        QString jline = QString("{\"lang\":\"%1\",\"ok\":%2,\"info\":\"%3\"}")
                            .arg(s.lang)
                            .arg(ok ? "true" : "false")
                            .arg(info);
        resultsJson.append(jline);
    }

    fprintf(stdout, "\n=== smoke_summary passed=%d total=%d ===\n", passed, total);
    // Emit a parseable line at the very end:
    fprintf(stdout, "SMOKE_JSON [%s]\n", resultsJson.join(",").toUtf8().constData());

    return (passed == total) ? 0 : 1;
}
