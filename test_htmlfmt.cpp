/**
 * Regression test for the HTML formatter through the C++ bridge
 *   (Rust: rust-core/src/html_fmt.rs, called via RustCore::formatHtml /
 *   RustCore::minifyHtml). The exhaustive properties (DOM equivalence
 *   against html5ever, idempotence, millions of generated cases) live in
 *   the Rust tests; this one pins the FFI contract the UI relies on:
 *   success = formatted text + empty message, decline = the input
 *   unchanged + a message naming line and column.
 */

#include "src/rustbridge.h"

#include <QCoreApplication>
#include <QString>

#include <cstdio>

static int g_pass = 0, g_fail = 0;

static void check(const char *what, bool ok, const QString &detail = {}) {
    if (ok) {
        std::printf("  [PASS] %s\n", what);
        ++g_pass;
    } else {
        std::printf("  [FAIL] %s%s%s\n", what, detail.isEmpty() ? "" : " — ",
                    detail.toUtf8().constData());
        ++g_fail;
    }
}

int main(int argc, char **argv) {
    QCoreApplication app(argc, argv);

    const QString list = QStringLiteral("<ul><li>One</li><li>Two</li></ul>");
    const QString listFmt = QStringLiteral("<ul>\n  <li>One</li>\n  <li>Two</li>\n</ul>");

    // Legacy overload keeps working.
    check("formatHtml(2) indents block elements", RustCore::formatHtml(list, 2) == listFmt,
          RustCore::formatHtml(list, 2));
    check("formatHtml(4) uses four spaces",
          RustCore::formatHtml(list, 4).contains(QStringLiteral("\n    <li>One</li>")));

    // Message overload: empty on success.
    QString msg = QStringLiteral("sentinel");
    const QString out = RustCore::formatHtml(list, 2, &msg);
    check("message overload formats", out == listFmt, out);
    check("message is empty on success", msg.isEmpty(), msg);

    // Inline whitespace is meaning: never added or removed.
    const QString inl = QStringLiteral("<p><b>a</b><i>b</i> <u>c</u></p>");
    check("inline runs untouched",
          RustCore::formatHtml(inl, 2) ==
              QStringLiteral("<p>\n  <b>a</b><i>b</i> <u>c</u>\n</p>"),
          RustCore::formatHtml(inl, 2));

    // Decline: unchanged text + a positioned, specific message.
    const QString bad = QStringLiteral("<div>\n  <p><b>x</p>\n</div>");
    msg.clear();
    const QString kept = RustCore::formatHtml(bad, 2, &msg);
    check("declined document is returned unchanged", kept == bad, kept);
    check("decline message names line and column",
          msg.contains(QStringLiteral("line 2, column 10")), msg);
    check("decline message names the element", msg.contains(QStringLiteral("<b>")), msg);
    check("legacy overload also returns it unchanged", RustCore::formatHtml(bad, 2) == bad);

    // Non-ASCII survives the UTF-8 round trip byte for byte.
    const QString uni = QStringLiteral("<div><p>你好 é &amp;</p></div>");
    check("non-ASCII and entities survive",
          RustCore::formatHtml(uni, 2) == QStringLiteral("<div>\n  <p>你好 é &amp;</p>\n</div>"),
          RustCore::formatHtml(uni, 2));

    // Minify removes only unrendered whitespace.
    msg = QStringLiteral("sentinel");
    const QString mini = RustCore::minifyHtml(
        QStringLiteral("<ul>\n  <li>a</li>\n  <li><b>x</b> <i>y</i></li>\n</ul>"), &msg);
    check("minify keeps inline spaces",
          mini == QStringLiteral("<ul><li>a</li><li><b>x</b> <i>y</i></li></ul>"), mini);
    check("minify message empty on success", msg.isEmpty(), msg);
    const QString pre = QStringLiteral("<pre>\n  a\n</pre>");
    check("minify leaves <pre> alone", RustCore::minifyHtml(pre) == pre);

    std::printf("=== Summary: %d passed, %d failed ===\n", g_pass, g_fail);
    return g_fail == 0 ? 0 : 1;
}
