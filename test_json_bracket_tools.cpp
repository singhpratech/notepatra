// SPDX-License-Identifier: GPL-3.0-or-later
//
// test_json_bracket_tools — the JSON Tools / Bracket Tools panels, end to end:
// a real FormatterPanel, real buttons, the real rustbridge wrappers and the
// Rust core. Asserts what the user sees:
//
//   * Format / Minify on invalid JSON leave the text exactly as it was and
//     say "✗ ... line L, column C: <what is wrong>" (they used to repair
//     silently, or strip spaces inside strings, and report "✓ done");
//   * Fix + Format either repairs with a positioned list of changes or, when
//     the repair would be a guess, changes nothing and says where;
//   * Format never rewrites token text (numbers, duplicate keys, escapes);
//   * Bracket Auto-Fix says which bracket it inserted/removed and where.
//
// The button wiring below mirrors MainWindow's JSON Tools / Bracket Tools
// menu actions (mainwindow.cpp), which are plain lambdas over RustCore.

#include <QApplication>
#include <QLabel>
#include <QPushButton>
#include <cstdio>

#include "fmtpanel.h"
#include "rustbridge.h"
#include "transform_note.h"

static int g_failed = 0;

static void check(bool ok, const char *what, const QString &detail = QString()) {
    if (ok) {
        std::printf("ok   %s\n", what);
    } else {
        std::printf("FAIL %s\n     %s\n", what, detail.toUtf8().constData());
        ++g_failed;
    }
}

static QString status(FormatterPanel &p) {
    // the banner is the only label whose text starts with a result mark
    for (QLabel *l : p.findChildren<QLabel *>()) {
        const QString t = l->text();
        if (t.startsWith(QStringLiteral("✓")) || t.startsWith(QStringLiteral("✗")))
            return t;
    }
    return QString();
}

static void click(FormatterPanel &p, const QString &label) {
    for (QPushButton *b : p.findChildren<QPushButton *>()) {
        if (b->text() == label) {
            b->click();
            return;
        }
    }
    std::printf("FAIL no button '%s'\n", label.toUtf8().constData());
    ++g_failed;
}

static void jsonPanel(FormatterPanel &p) {
    p.addButton("Format", [](const QString &s) { return RustCore::formatJson(s, 4); });
    p.addButton("Minify", [](const QString &s) { return RustCore::minifyJson(s); });
    p.addButton("Fix + Format", [](const QString &s) {
        QString report = RustCore::fixJsonReport(s);
        QString fixed = RustCore::fixJson(s);
        QString formatted = RustCore::formatJson(fixed, 4);
        return "/* ═══ FIX REPORT ═══\n" + report + "\n═══════════════════ */\n\n" + formatted;
    });
}

int main(int argc, char *argv[]) {
    QApplication app(argc, argv);

    // ── Format on invalid JSON: untouched, positioned error ──────────────
    {
        FormatterPanel p("JSON Tools", "JSON");
        jsonPanel(p);
        p.setInput("[1,2");
        click(p, "Format");
        check(p.inputText() == "[1,2", "Format leaves truncated JSON untouched", p.inputText());
        check(status(p).startsWith("✗ Format: not valid JSON, nothing changed: line 1, column 5:"),
              "Format says what and where", status(p));

        p.setInput("{\"a\": \"x  y\" @}");
        click(p, "Minify");
        check(p.inputText() == "{\"a\": \"x  y\" @}", "Minify leaves invalid JSON untouched",
              p.inputText());
        check(status(p).contains("line 1, column 14: expected ',' or '}'"),
              "Minify error names the column", status(p));
    }

    // ── Format / Minify on valid JSON: whitespace only ───────────────────
    {
        FormatterPanel p("JSON Tools", "JSON");
        jsonPanel(p);
        const QString s = "{\"n\":[1.0,-0,1e400,123456789012345678901234567890],"
                          "\"s\":\"\\u00e9 x  y\",\"a\":1,\"a\":2}";
        p.setInput(s);
        click(p, "Format");
        const QString f = p.inputText();
        check(f.contains("1.0") && f.contains("-0") && f.contains("1e400") &&
                  f.contains("123456789012345678901234567890"),
              "Format keeps number text", f);
        check(f.contains("\"\\u00e9 x  y\""), "Format keeps escapes and inner spaces", f);
        check(f.count("\"a\"") == 2, "Format keeps duplicate keys", f);
        check(status(p).startsWith("✓ Format done"), "Format reports success", status(p));
        click(p, "Minify");
        check(p.inputText() == s, "Minify(Format(x)) == x for minified x", p.inputText());
    }

    // ── Fix + Format ─────────────────────────────────────────────────────
    {
        FormatterPanel p("JSON Tools", "JSON");
        jsonPanel(p);
        p.setInput("{a: 1, 'b': True,}");
        click(p, "Fix + Format");
        check(p.inputText().contains("\"a\": 1") && p.inputText().contains("\"b\": true"),
              "Fix + Format repairs unambiguous faults", p.inputText());
        check(status(p).startsWith("✓ Fix + Format: Repaired 4 issue(s)") &&
                  status(p).contains("line 1, column 2: quoted the key 'a'"),
              "Fix + Format lists the changes with positions", status(p));

        p.setInput("{\"a\": [1, 2");
        click(p, "Fix + Format");
        check(p.inputText() == "{\"a\": [1, 2", "Fix + Format does not invent closers",
              p.inputText());
        check(status(p).startsWith("✗ Fix + Format: No changes made") &&
                  status(p).contains("line 1, column 12") && status(p).contains("\"]}\""),
              "Fix + Format says why it declined and what to add", status(p));

        p.setInput("[NaN]");
        click(p, "Fix + Format");
        check(p.inputText() == "[NaN]" && status(p).contains("'NaN' has no JSON equivalent"),
              "NaN is refused with a specific message", status(p));
    }

    // ── bridge-level contract (also what the MCP format path relies on) ──
    {
        TransformNote::clear();
        check(RustCore::formatJson("[1 2]") == "[1 2]", "formatJson returns invalid input unchanged");
        TransformNote::Note n = TransformNote::take();
        check(n.kind == TransformNote::Kind::Error && n.text.contains("line 1, column 4"),
              "formatJson posts a positioned error note", n.text);
        check(RustCore::jsonParseError("{\"a\":1,}").startsWith("line 1, column 7: trailing comma"),
              "jsonParseError is specific", RustCore::jsonParseError("{\"a\":1,}"));
        check(RustCore::jsonParseError("{\"a\":1}").isEmpty(), "jsonParseError accepts valid JSON");
        TransformNote::clear();
        RustCore::formatJson("{}");
        check(TransformNote::take().kind == TransformNote::Kind::None,
              "valid input posts no note");
    }

    // ── Bracket Tools ────────────────────────────────────────────────────
    {
        FormatterPanel p("Bracket Tools", "JavaScript");
        p.addButton("Check", [](const QString &s) { return RustCore::checkBrackets(s); });
        p.addButton("Auto-Fix", [](const QString &s) { return RustCore::fixBrackets(s); });

        p.setInput("foo(a, b\nbar();");
        click(p, "Auto-Fix");
        check(p.inputText() == "foo(a, b)\nbar();", "Auto-Fix closes the call on its line",
              p.inputText());
        check(status(p).contains("line 1, column 9: inserted ')' to close the '(' at line 1, column 4"),
              "Auto-Fix says what it inserted and where", status(p));

        const QString balanced = "if (x) { s = \"(\"; } // )";
        p.setInput(balanced);
        click(p, "Auto-Fix");
        check(p.inputText() == balanced, "Auto-Fix leaves balanced code alone", p.inputText());
        check(status(p).contains("All brackets matched"), "Auto-Fix says nothing was needed",
              status(p));

        p.setInput("x = [1, (2]");
        click(p, "Check");
        check(p.inputText().contains("line 1, column 9: '(' is not closed before the ']' at line 1, column 11"),
              "Check names both brackets", p.inputText());
    }

    std::printf("%s (%d failure%s)\n", g_failed ? "FAILED" : "PASSED", g_failed,
                g_failed == 1 ? "" : "s");
    return g_failed ? 1 : 0;
}
