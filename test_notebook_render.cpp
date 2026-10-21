// SPDX-License-Identifier: GPL-3.0-or-later
//
// test_notebook_render — pure renderers behind the Preview pane:
// Markdown to HTML, nbformat-4 .ipynb to HTML, and the "-- %%" SQL cell split.

#include "notebook_render.h"

#include <QFontDatabase>
#include <QGuiApplication>
#include <QImage>
#include <QTextCharFormat>

#include <cstdio>

using namespace NotebookRender;

static int g_passed = 0;
static int g_failed = 0;

#define EXPECT_TRUE(label, cond) \
    do { \
        if (cond) { ++g_passed; std::printf("  [PASS] %s\n", label); } \
        else { ++g_failed; std::printf("  [FAIL] %s\n", label); } \
    } while (0)

static const char *kPng1x1 =
    "iVBORw0KGgoAAAANSUhEUgAAAAEAAAABCAYAAAAfFcSJAAAADUlEQVR42mNkYPhfDwAChwGA60e6kgAAAABJRU5ErkJggg==";

static QByteArray notebookFixture(bool sourceAsString) {
    const QString mdSource = sourceAsString
        ? QStringLiteral("\"# Notebook Heading\\n\\nSome *text*.\"")
        : QStringLiteral("[\"# Notebook Heading\\n\", \"\\n\", \"Some *text*.\"]");
    const QString codeSource = sourceAsString
        ? QStringLiteral("\"print('hello')\\n1 + 1\"")
        : QStringLiteral("[\"print('hello')\\n\", \"1 + 1\"]");
    const QString json = QStringLiteral(R"({
 "nbformat": 4, "nbformat_minor": 5,
 "metadata": {"language_info": {"name": "python"}},
 "cells": [
  {"cell_type": "markdown", "metadata": {}, "source": %1},
  {"cell_type": "code", "execution_count": 3, "metadata": {}, "source": %2,
   "outputs": [
    {"output_type": "stream", "name": "stdout", "text": ["hello\n"]},
    {"output_type": "execute_result", "execution_count": 3, "metadata": {},
     "data": {"text/plain": ["2"]}},
    {"output_type": "display_data", "metadata": {},
     "data": {"image/png": "%3", "text/plain": ["<Figure>"]}},
    {"output_type": "error", "ename": "ZeroDivisionError", "evalue": "division by zero",
     "traceback": ["\u001b[0;31m---------------------------------------------------------------------------\u001b[0m",
                   "\u001b[0;31mZeroDivisionError\u001b[0m: division by zero"]}
   ]}
 ]
})")
        .arg(mdSource, codeSource, QString::fromLatin1(kPng1x1));
    return json.toUtf8();
}

int main(int argc, char *argv[]) {
    qputenv("QT_QPA_PLATFORM", "offscreen");
    QGuiApplication app(argc, argv);

    Colors c{"#ffffff", "#000000", "#777777", "#f0f0f0", "#cccccc", "#0b7a75", "#1a5fb4"};

    std::printf("-- markdownToHtml --\n");
    {
        const QString md = QStringLiteral(
            "# Title Heading\n\n| a | b |\n|---|---|\n| 1 | 2 |\n\n```python\nprint('fenced code')\n```\n");
        const QString html = markdownToHtml(md);
        EXPECT_TRUE("heading text present", html.contains(QLatin1String("Title Heading")));
        EXPECT_TRUE("pipe table rendered as <table", html.contains(QLatin1String("<table")));
        EXPECT_TRUE("fenced code text present", html.contains(QLatin1String("fenced code")));
        EXPECT_TRUE("fenced code exported as <pre> in monospace",
                    html.contains(QLatin1String("<pre")) && html.contains(QLatin1String("monospace")));
        const QString themed = markdownToHtml(QStringLiteral("see [docs](https://example.org) and `x`"), &c);
        EXPECT_TRUE("link tinted with Colors::linkFg", themed.contains(QLatin1String("color:#1a5fb4")));
        EXPECT_TRUE("inline code gets Colors::codeBg",
                    themed.contains(QLatin1String("background-color:#f0f0f0")));
        EXPECT_TRUE("htmlBodyContent strips the <body> wrapper",
                    !htmlBodyContent(html).contains(QLatin1String("<body")) &&
                        htmlBodyContent(html).contains(QLatin1String("Title Heading")));
        // Qt 5.15.2's importer marks inline code with setFont(systemFont(FixedFont))
        // only, no fixed-pitch flag (Windows CI); 5.15.3+ adds the flag.
        QTextCharFormat qt5152;
        qt5152.setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
        qt5152.setFontFixedPitch(false);
        EXPECT_TRUE("5.15.2-shaped inline code (mono family, no flag) is code",
                    isCodeCharFormat(qt5152));
        QTextCharFormat flagged;
        flagged.setFontFixedPitch(true);
        EXPECT_TRUE("fixed-pitch flag alone is code", isCodeCharFormat(flagged));
        QTextCharFormat plain;
        plain.setFontFamily(QStringLiteral("NotepatraNoSuchProportionalFont"));
        EXPECT_TRUE("proportional family without flag is not code", !isCodeCharFormat(plain));
        EXPECT_TRUE("empty format is not code", !isCodeCharFormat(QTextCharFormat()));
    }

    std::printf("-- stripAnsi --\n");
    {
        const QString s = stripAnsi(QStringLiteral("\x1b[0;31mred\x1b[0m plain \x1b]0;title\x07" "end"));
        EXPECT_TRUE("CSI and OSC sequences removed", s == QLatin1String("red plain end"));
    }

    std::printf("-- ipynbToHtml (source as list) --\n");
    QString listHtml;
    {
        QVector<NotebookImage> images;
        QString err;
        listHtml = ipynbToHtml(notebookFixture(false), c, &images, &err);
        EXPECT_TRUE("no error", err.isEmpty());
        EXPECT_TRUE("markdown heading rendered", listHtml.contains(QLatin1String("Notebook Heading")));
        EXPECT_TRUE("stream stdout 'hello' present", listHtml.contains(QLatin1String("hello")));
        EXPECT_TRUE("execution count label In [3]", listHtml.contains(QLatin1String("In [3]:")));
        EXPECT_TRUE("language label python", listHtml.contains(QLatin1String("python")));
        EXPECT_TRUE("execute_result Out [3] label", listHtml.contains(QLatin1String("Out [3]:")));
        EXPECT_TRUE("code is escaped", listHtml.contains(QLatin1String("print(&#x27;hello&#x27;)")) ||
                                           listHtml.contains(QLatin1String("print('hello')")));
        EXPECT_TRUE("image referenced as nb://img/0", listHtml.contains(QLatin1String("nb://img/0")));
        EXPECT_TRUE("exactly one image returned", images.size() == 1);
        EXPECT_TRUE("image bytes decode to a 1x1 PNG",
                    images.size() == 1 && QImage::fromData(images.at(0).pngBytes, "PNG").size() == QSize(1, 1));
        EXPECT_TRUE("traceback text present", listHtml.contains(QLatin1String("ZeroDivisionError")));
        EXPECT_TRUE("no ANSI escape left", !listHtml.contains(QLatin1String("\x1b[")) &&
                                               !listHtml.contains(QChar(0x1b)));
    }

    std::printf("-- ipynbToHtml (source as string) --\n");
    {
        QVector<NotebookImage> images;
        QString err;
        const QString strHtml = ipynbToHtml(notebookFixture(true), c, &images, &err);
        EXPECT_TRUE("no error", err.isEmpty());
        EXPECT_TRUE("string source renders the same as list source", strHtml == listHtml);
        EXPECT_TRUE("one image", images.size() == 1);
    }

    std::printf("-- ipynbToHtml errors --\n");
    {
        QString err;
        QVector<NotebookImage> images;
        const QString h3 = ipynbToHtml(R"({"nbformat": 3, "worksheets": []})", c, &images, &err);
        EXPECT_TRUE("nbformat 3 -> error", !err.isEmpty() && h3.isEmpty());
        err.clear();
        const QString hg = ipynbToHtml("this is { not json", c, &images, &err);
        EXPECT_TRUE("garbage -> error", !err.isEmpty() && hg.isEmpty());
        err.clear();
        ipynbToHtml("[1, 2, 3]", c, &images, &err);
        EXPECT_TRUE("non-object JSON -> error", !err.isEmpty());
    }

    std::printf("-- splitSqlCells --\n");
    {
        const QString sql = QStringLiteral(
            "select 1;\n-- %%\nselect 2;\r\n-- %% [markdown]\n# notes\n-- %%\n\n");
        const QVector<SqlCell> cells = splitSqlCells(sql);
        EXPECT_TRUE("3 cells (trailing empty cell dropped)", cells.size() == 3);
        if (cells.size() == 3) {
            EXPECT_TRUE("cell 0 text", cells[0].text == QLatin1String("select 1;"));
            EXPECT_TRUE("cell 0 lines 0..0", cells[0].startLine == 0 && cells[0].endLine == 0);
            EXPECT_TRUE("cell 1 text (CR stripped)", cells[1].text == QLatin1String("select 2;"));
            EXPECT_TRUE("cell 1 lines 1..2", cells[1].startLine == 1 && cells[1].endLine == 2);
            EXPECT_TRUE("cell 1 is SQL", !cells[1].markdown);
            EXPECT_TRUE("cell 2 is markdown", cells[2].markdown);
            EXPECT_TRUE("cell 2 text", cells[2].text == QLatin1String("# notes"));
            EXPECT_TRUE("cell 2 lines 3..4", cells[2].startLine == 3 && cells[2].endLine == 4);

            EXPECT_TRUE("line 0 -> cell 0", cellIndexForLine(cells, 0) == 0);
            EXPECT_TRUE("line 1 (separator) -> cell 1", cellIndexForLine(cells, 1) == 1);
            EXPECT_TRUE("line 2 -> cell 1", cellIndexForLine(cells, 2) == 1);
            EXPECT_TRUE("line 3 -> cell 2", cellIndexForLine(cells, 3) == 2);
            EXPECT_TRUE("line 4 -> cell 2", cellIndexForLine(cells, 4) == 2);
            EXPECT_TRUE("line 5 (dropped empty cell) -> -1", cellIndexForLine(cells, 5) == -1);
            EXPECT_TRUE("line 99 -> -1", cellIndexForLine(cells, 99) == -1);
        }

        const QVector<SqlCell> one = splitSqlCells(QStringLiteral("select 1;\nselect 2;\n"));
        EXPECT_TRUE("no separator -> 1 cell", one.size() == 1);
        EXPECT_TRUE("single cell keeps both lines",
                    one.size() == 1 && one[0].text == QLatin1String("select 1;\nselect 2;"));

        const QVector<SqlCell> md = splitSqlCells(QStringLiteral("-- %% md\n-- ## Commented heading\n-- %%\nselect 3;"));
        EXPECT_TRUE("'-- %% md' marks markdown and strips '-- ' prefixes",
                    md.size() == 2 && md[0].markdown && md[0].text == QLatin1String("## Commented heading"));
        EXPECT_TRUE("leading separator does not create an empty cell 0",
                    md.size() == 2 && md[0].startLine == 0 && md[1].text == QLatin1String("select 3;"));
        EXPECT_TRUE("empty input -> 0 cells", splitSqlCells(QString()).isEmpty());
        EXPECT_TRUE("'--%%' without a space is a separator",
                    splitSqlCells(QStringLiteral("a;\n--%%\nb;")).size() == 2);
        EXPECT_TRUE("an ordinary comment is not a separator",
                    splitSqlCells(QStringLiteral("a;\n-- note\nb;")).size() == 1);
    }

    std::printf("\n%d passed, %d failed\n", g_passed, g_failed);
    return g_failed == 0 ? 0 : 1;
}
