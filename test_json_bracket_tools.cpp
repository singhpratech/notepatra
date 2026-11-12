// SPDX-License-Identifier: GPL-3.0-or-later
//
// test_json_bracket_tools — the JSON Tools / HTML Tools / Bracket Tools
// panels, end to end: a real FormatterPanel with the SAME buttons MainWindow
// adds (FormatterTools::addJsonButtons / addHtmlButtons / addBracketButtons),
// the real rustbridge wrappers and the Rust core. Asserts what the user sees:
//
//   * Format / Minify on invalid JSON leave the text exactly as it was and
//     say "Not changed: ... line L, column C: <what is wrong>";
//   * Fix + Format puts ONLY the repaired JSON in the output (it used to
//     prepend a "/* FIX REPORT */" comment, so the output was not JSON); the
//     report is in the banner (one line) and its tooltip (every change);
//   * a declined repair changes nothing and says why, with a position;
//   * Bracket Check reports in the banner and leaves the text byte for byte
//     (it used to replace the text with its report, so a following Auto-Fix
//     ran on the report);
//   * HTML Minify keeps rendered whitespace, a document the formatter cannot
//     follow is left unchanged with a position, nothing adds end tags or a
//     report comment;
//   * the AI Fix reply checks never present an invalid reply as a fix.

#include <QApplication>
#include <QDir>
#include <QLabel>
#include <QListWidget>
#include <QPushButton>
#include <QTemporaryDir>
#include <cstdio>

#include "fmtpanel.h"
#include "formatter_tools.h"
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

// The banner as the user sees it: the visible label (possibly elided) and
// its hover text.
static QLabel *banner(FormatterPanel &p) {
    const QString full = p.statusText();
    for (QLabel *l : p.findChildren<QLabel *>()) {
        QString t = l->text();
        if (t.endsWith(QChar(0x2026))) t.chop(1);
        if (!t.isEmpty() && full.startsWith(t)) return l;
    }
    return nullptr;
}

static int countLines(const QString &s, const QString &prefix) {
    int n = 0;
    for (const QString &l : s.split('\n'))
        if (l.trimmed().startsWith(prefix)) ++n;
    return n;
}

int main(int argc, char *argv[]) {
    // Nothing here should write settings, but the panel reads Config: keep
    // any QStandardPaths access away from the real profile on every OS.
    QTemporaryDir home;
    if (home.isValid()) {
        const QByteArray h = QDir::toNativeSeparators(home.path()).toUtf8();
        qputenv("HOME", h);
        qputenv("XDG_CONFIG_HOME", h + "/.config");
        qputenv("XDG_DATA_HOME", h + "/.local/share");
        qputenv("XDG_CACHE_HOME", h + "/.cache");
#ifdef Q_OS_WIN
        qputenv("APPDATA", h);
        qputenv("LOCALAPPDATA", h);
        qputenv("USERPROFILE", h);
#endif
    }
    QApplication app(argc, argv);

    // ── Format on invalid JSON: untouched, positioned error ──────────────
    {
        FormatterPanel p("JSON Tools", "JSON");
        FormatterTools::addJsonButtons(&p);
        p.setInput("[1,2");
        click(p, "Format");
        check(p.inputText() == "[1,2", "Format leaves truncated JSON untouched", p.inputText());
        check(p.statusText().startsWith("Not changed: Format — not valid JSON: line 1, column 5:") &&
                  p.statusIsError(),
              "Format says what and where", p.statusText());

        p.setInput("{\"a\": \"x  y\" @}");
        click(p, "Minify");
        check(p.inputText() == "{\"a\": \"x  y\" @}", "Minify leaves invalid JSON untouched",
              p.inputText());
        check(p.statusText().contains("line 1, column 14: expected ',' or '}'"),
              "Minify error names the column", p.statusText());
    }

    // ── Format / Minify on valid JSON: whitespace only ───────────────────
    {
        FormatterPanel p("JSON Tools", "JSON");
        FormatterTools::addJsonButtons(&p);
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
        check(p.statusText().startsWith("Done: Format — ") && !p.statusIsError(),
              "Format reports success", p.statusText());
        click(p, "Minify");
        check(p.inputText() == s, "Minify(Format(x)) == x for minified x", p.inputText());
    }

    // ── Format refused by the output size cap: said so, never "invalid" ──
    // Valid JSON whose pretty form would pass 256 MB came back unchanged with
    // no message and the panel said "Done" (v0.1.133 review).
    {
        FormatterPanel p("JSON Tools", "JSON");
        FormatterTools::addJsonButtons(&p);
        const QString deep = QString("[").repeated(10000) + QString("]").repeated(10000);
        check(RustCore::jsonParseError(deep).isEmpty(), "deep nesting is valid JSON (vacuity)");
        p.setInput(deep);
        click(p, "Format");
        check(p.inputText() == deep, "over-cap Format leaves the text untouched");
        check(p.statusIsError() &&
                  p.statusText().startsWith(
                      "Not changed: Format — left unchanged: formatted output would exceed 256 MB"),
              "over-cap Format says why", p.statusText());
        check(!p.statusText().contains("invalid", Qt::CaseInsensitive) &&
                  !p.statusText().contains("not valid"),
              "over-cap Format does not call valid JSON invalid", p.statusText());
        click(p, "Minify");
        check(p.statusText().startsWith("Done: Minify") && !p.statusIsError(),
              "Minify still works on it", p.statusText());
    }

    // ── Fix + Format: output is only JSON, report in the banner ──────────
    {
        FormatterPanel p("JSON Tools", "JSON");
        FormatterTools::addJsonButtons(&p);
        p.resize(900, 600);
        p.show();
        QApplication::processEvents();

        p.setInput("{name: 'x', \"a\": [1,2,],}");
        click(p, "Fix + Format");
        const QString out = p.inputText();
        check(RustCore::jsonParseError(out).isEmpty(), "Fix + Format output parses as JSON", out);
        check(!out.contains("FIX REPORT") && !out.contains("/*"),
              "Fix + Format output has no report comment", out);
        check(out.contains("\"name\": \"x\"") && out.contains("\"a\": ["),
              "Fix + Format keeps the content", out);
        check(p.statusText().startsWith("Done: Fix + Format — Repaired ") &&
                  p.statusText().contains("line 1, column 2: quoted the key 'name'"),
              "banner summarises the repair with a position", p.statusText());
        // Every change is in the details, one positioned line each.
        const QString d = p.statusDetails();
        const int nChanges = countLines(d, "line ");
        check(d.startsWith("Repaired ") && nChanges >= 3 &&
                  d.startsWith(QString("Repaired %1 issue(s)").arg(nChanges)),
              "details list every change", d);
        check(d.contains("refer to the text before the fix"),
              "details say which text the positions are in", d);
        QLabel *b = banner(p);
        check(b && b->toolTip().contains("quoted the key") &&
                  b->toolTip().contains("removed a trailing comma"),
              "the full report is the banner's tooltip", b ? b->toolTip() : QString("no banner"));
        auto *log = p.findChild<QListWidget *>();
        check(log && log->count() > 0 &&
                  log->item(log->count() - 1)->toolTip().contains("quoted the key"),
              "session log row carries the full report", QString());

        p.setInput("{a: 1, 'b': True,}");
        click(p, "Fix + Format");
        check(p.inputText().contains("\"a\": 1") && p.inputText().contains("\"b\": true") &&
                  RustCore::jsonParseError(p.inputText()).isEmpty(),
              "Fix + Format repairs unambiguous faults", p.inputText());
        check(p.statusText().startsWith("Done: Fix + Format — Repaired 4 issue(s)") &&
                  p.statusText().contains("line 1, column 2: quoted the key 'a'"),
              "Fix + Format lists the changes with positions", p.statusText());

        // Declined repairs: text byte-identical, reason with line:column.
        p.setInput("[1,2");
        click(p, "Fix + Format");
        check(p.inputText() == "[1,2", "declined repair leaves truncated JSON untouched",
              p.inputText());
        check(p.statusIsError() && p.statusText().startsWith("Not changed: Fix + Format — ") &&
                  p.statusText().contains("line 1, column 5"),
              "declined repair says why and where", p.statusText());
        check(!p.statusText().contains("No changes made"),
              "banner does not repeat 'not changed'", p.statusText());
        check(p.statusDetails().startsWith("No changes made"), "details keep the full reason",
              p.statusDetails());

        p.setInput("{\"a\": [1, 2");
        click(p, "Fix + Format");
        check(p.inputText() == "{\"a\": [1, 2", "Fix + Format does not invent closers",
              p.inputText());
        check(p.statusText().contains("line 1, column 12") && p.statusText().contains("\"]}\""),
              "Fix + Format says what to add", p.statusText());

        p.setInput("[NaN]");
        click(p, "Fix + Format");
        check(p.inputText() == "[NaN]" && p.statusText().contains("'NaN' has no JSON equivalent"),
              "NaN is refused with a specific message", p.statusText());
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

    // ── AI Fix reply checks ──────────────────────────────────────────────
    {
        const auto bad = FormatterTools::checkAiJsonReply("{\"a\": 1,\n \"b\": }");
        check(!bad.ok && bad.error.startsWith("line 2, column ") &&
                  bad.text == "{\"a\": 1,\n \"b\": }",
              "invalid AI JSON reply is not a fix and says where", bad.error);
        const auto good = FormatterTools::checkAiJsonReply("{\"a\":1}");
        check(good.ok && good.text == "{\n    \"a\": 1\n}" && good.error.isEmpty(),
              "valid AI JSON reply is formatted", good.text);
        const QString nested = "<div><p><b>x</p></div>";
        const auto hbad = FormatterTools::checkAiHtmlReply(nested);
        check(!hbad.ok && hbad.text == nested && hbad.error.contains("line 1, column"),
              "declined AI HTML reply is not presented as formatted", hbad.error);
        const auto hgood = FormatterTools::checkAiHtmlReply("<ul><li>a</li></ul>");
        check(hgood.ok && hgood.text == "<ul>\n  <li>a</li>\n</ul>", "AI HTML reply re-indented",
              hgood.text);
    }

    // ── HTML Tools ───────────────────────────────────────────────────────
    {
        FormatterPanel p("HTML Tools", "HTML");
        FormatterTools::addHtmlButtons(&p);

        const QString pre = "<pre>a\n  b</pre>";
        p.setInput(pre);
        click(p, "Minify");
        check(p.inputText() == pre, "Minify keeps <pre> contents", p.inputText());

        const QString inl = "<p><b>a</b> <i>b</i></p>";
        p.setInput(inl);
        click(p, "Minify");
        check(p.inputText().contains("</b> <i>"), "Minify keeps the space between inline elements",
              p.inputText());

        const QString text = "<p>Hello\nworld</p>";
        p.setInput(text);
        click(p, "Minify");
        check(!p.inputText().contains("Helloworld"), "Minify never joins words across a newline",
              p.inputText());

        const QString script = "<script>\n// note\nvar x = 1;\n</script>";
        p.setInput(script);
        click(p, "Minify");
        check(p.inputText().contains("// note\nvar x = 1;"),
              "Minify never joins script lines (a // comment would swallow code)", p.inputText());

        const QString bad = "<div>\n  <p><b>x</p>\n</div>";
        for (const QString &btn : {QString("Format (2 spaces)"), QString("Format (4 spaces)"),
                                   QString("Minify"), QString("Check + Format")}) {
            p.setInput(bad);
            click(p, btn);
            const QString what = btn + ": mis-nested document left byte-identical";
            check(p.inputText() == bad, what.toUtf8().constData(), p.inputText());
            const QString why = btn + ": says what and where";
            check(p.statusIsError() && p.statusText().startsWith("Not changed: " + btn + " — ") &&
                      p.statusText().contains("line 2, column"),
                  why.toUtf8().constData(), p.statusText());
        }

        const QString implied = "<p>a<p>b";
        p.setInput(implied);
        click(p, "Check + Format");
        check(!p.inputText().contains("</p>"), "valid <p>a<p>b gets no added end tags",
              p.inputText());
        check(!p.inputText().contains("FIX REPORT") && !p.inputText().contains("/*"),
              "Check + Format adds no report comment", p.inputText());
        check(p.statusText().startsWith("Done: Check + Format — no mis-nested or stray tags found"),
              "Check + Format says what it checked", p.statusText());

        const QString doc = "<!DOCTYPE html>\n<html><body><img alt=\">\"><br></body></html>";
        p.setInput(doc);
        click(p, "Check + Format");
        check(p.inputText().startsWith("<!DOCTYPE html>"), "doctype stays first", p.inputText());
        check(p.inputText().contains("<img alt=\">\">") && p.inputText().contains("<br>"),
              "void elements are not rewritten", p.inputText());
    }

    // ── Bracket Tools ────────────────────────────────────────────────────
    {
        FormatterPanel p("Bracket Tools", "JavaScript");
        FormatterTools::addBracketButtons(&p);

        p.setInput("foo(a, b\nbar();");
        click(p, "Auto-Fix");
        check(p.inputText() == "foo(a, b)\nbar();", "Auto-Fix closes the call on its line",
              p.inputText());
        check(p.statusText().contains(
                  "line 1, column 9: inserted ')' to close the '(' at line 1, column 4"),
              "Auto-Fix says what it inserted and where", p.statusText());

        const QString balanced = "if (x) { s = \"(\"; } // )";
        p.setInput(balanced);
        click(p, "Auto-Fix");
        check(p.inputText() == balanced, "Auto-Fix leaves balanced code alone", p.inputText());
        check(p.statusText().contains("All brackets matched"), "Auto-Fix says nothing was needed",
              p.statusText());

        // Check reports and leaves the text alone; Auto-Fix then fixes CODE.
        const QString code = "if (x { y(); }";
        p.setInput(code);
        click(p, "Check");
        check(p.inputText() == code, "Check leaves the text byte-identical", p.inputText());
        check(p.statusIsError() && p.statusText().startsWith("Check: Found ") &&
                  p.statusText().contains("line 1, column"),
              "Check shows the issue count and the first issue with line:column",
              p.statusText());
        check(countLines(p.statusDetails(), "line ") >= 1 &&
                  p.statusDetails().startsWith("Found "),
              "Check details list each issue", p.statusDetails());
        click(p, "Auto-Fix");
        const QString fixed = p.inputText();
        check(!fixed.contains("Found") && !fixed.contains("issue"),
              "Auto-Fix after Check runs on the code, not the report", fixed);
        check(fixed.startsWith("if (x") && fixed.contains("y();") &&
                  RustCore::checkBrackets(fixed).startsWith("All brackets matched"),
              "Auto-Fix after Check balances the code", fixed);

        p.setInput("x = [1, (2]");
        click(p, "Check");
        check(p.inputText() == "x = [1, (2]", "Check never replaces the text", p.inputText());
        check(p.statusDetails().contains(
                  "line 1, column 9: '(' is not closed before the ']' at line 1, column 11"),
              "Check names both brackets", p.statusDetails());

        p.setInput("f(a);");
        click(p, "Check");
        check(p.inputText() == "f(a);" && !p.statusIsError() &&
                  p.statusText() == "Check: All brackets matched. No issues found.",
              "Check on balanced code says so", p.statusText());
    }

    std::printf("%s (%d failure%s)\n", g_failed ? "FAILED" : "PASSED", g_failed,
                g_failed == 1 ? "" : "s");
    return g_failed ? 1 : 0;
}
