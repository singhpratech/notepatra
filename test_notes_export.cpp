// Noter export regression test. Drives:
//   * htmlToMarkdown across every Noter block type + each embed kind
//   * exportMarkdown end-to-end (file in → MD file out + .assets sidecar)
//   * exportPdf end-to-end (file in → non-empty PDF on disk) when
//     NOTEPATRA_WITH_WEBENGINE is defined; otherwise the PDF test just
//     asserts the function reports the expected "WebEngine unavailable"
//     error so the lite-build path is still exercised.

#include "src/notes_export.h"

#include <QApplication>
#include <QByteArray>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QTemporaryDir>

#include <cstdio>
#include <cstdlib>

static int g_pass = 0, g_fail = 0;
#define EXPECT(label, cond) \
    do { if (cond) { ++g_pass; std::printf("  [PASS] %s\n", label); } \
         else      { ++g_fail; std::printf("  [FAIL] %s\n", label); } } while (0)

// QVERIFY_X tiny shim — used by tests that need a hard-fail (e.g. when
// the temp dir is invalid we can't continue).  Defined up here so it's
// visible to every test function below.
static void __verify_fail(const char *file, int line, const char *msg) {
    std::fprintf(stderr, "[FATAL] %s:%d %s\n", file, line, msg);
    std::exit(2);
}
#define QVERIFY_X(cond, label, msg) \
    do { if (!(cond)) __verify_fail(__FILE__, __LINE__, msg); } while (0)

// ─────────────────────────────────────────────────────────────────────
// htmlToMarkdown — every block shape
// ─────────────────────────────────────────────────────────────────────

static void test_markdown_all_block_types() {
    std::printf("test_markdown_all_block_types\n");

    const QString html =
        "<h1>Title</h1>"
        "<h2>Sub</h2>"
        "<p>Paragraph with <strong>bold</strong> + <em>italic</em> + "
        "<code>code</code> + <a href=\"https://x.com\">link</a>.</p>"
        "<div class=\"b b-dec\">Ship Noter in v0.1.94</div>"
        "<div class=\"b b-act\" data-owner=\"@alice\" data-due=\"2026-05-25\">"
        "Wire export menu</div>"
        "<div class=\"b b-q\">Offline reminders?</div>"
        "<div class=\"b b-risk\">QtWebEngine size on Lite</div>"
        "<div class=\"b b-quote\" data-attribution=\"Linus\">Talk is cheap.</div>";

    const QString md = NoterExport::htmlToMarkdown(html, QString(), QString());

    EXPECT("md: h1",            md.contains("# Title"));
    EXPECT("md: h2",            md.contains("## Sub"));
    EXPECT("md: bold",          md.contains("**bold**"));
    EXPECT("md: italic",        md.contains("*italic*"));
    EXPECT("md: code",          md.contains("`code`"));
    EXPECT("md: link",          md.contains("[link](https://x.com)"));
    EXPECT("md: decision",      md.contains("**DECISION:** Ship Noter in v0.1.94"));
    EXPECT("md: action checkbox",
                                md.contains("- [ ] Wire export menu"));
    EXPECT("md: action owner",  md.contains("owner: @alice"));
    EXPECT("md: action due",    md.contains("due: 2026-05-25"));
    EXPECT("md: question",      md.contains("**QUESTION:** Offline reminders?"));
    EXPECT("md: risk",          md.contains("**RISK:** QtWebEngine"));
    EXPECT("md: quote+attribution",
                                md.contains("> Talk is cheap.") &&
                                md.contains("Linus"));
}

// ─────────────────────────────────────────────────────────────────────
// htmlToMarkdown — embed types (PR / video / code-ref)
// ─────────────────────────────────────────────────────────────────────

static void test_markdown_embeds() {
    std::printf("test_markdown_embeds\n");

    const QString html =
        "<div class=\"emb-pr\" data-url=\"https://github.com/x/y/pull/42\" "
        "data-number=\"42\" data-title=\"Fix flaky test\" data-author=\"bob\" "
        "data-additions=\"12\" data-deletions=\"3\"></div>"
        "<div class=\"emb-vid\" data-url=\"https://youtu.be/abc\" "
        "data-title=\"Demo\" data-host=\"YouTube\" data-duration=\"3:21\"></div>"
        "<div class=\"emb\" data-path=\"src/foo.cpp\" data-lang=\"cpp\">"
        "int main() {}"
        "</div>";

    const QString md = NoterExport::htmlToMarkdown(html, QString(), QString());
    EXPECT("md: PR link",   md.contains("[PR #42: Fix flaky test]"));
    EXPECT("md: PR meta",   md.contains("bob") && md.contains("+12/-3"));
    EXPECT("md: video",     md.contains("[Video: Demo]") &&
                            md.contains("YouTube") &&
                            md.contains("3:21"));
    EXPECT("md: code-ref",  md.contains("```cpp") &&
                            md.contains("// src/foo.cpp") &&
                            md.contains("int main()"));
}

// ─────────────────────────────────────────────────────────────────────
// exportMarkdown — round-trip with base64 image
// ─────────────────────────────────────────────────────────────────────

static void test_export_markdown_with_image() {
    std::printf("test_export_markdown_with_image\n");

    QTemporaryDir tmp;
    QVERIFY_X(tmp.isValid(), "tempdir", "tempdir invalid");

    // Minimal 1-pixel PNG, base64-encoded inline.
    const QString pngB64 =
        "iVBORw0KGgoAAAANSUhEUgAAAAEAAAABCAQAAAC1HAwCAAAAC0lEQVR4nGNgAAIAAAUA"
        "AeImBZsAAAAASUVORK5CYII=";
    const QString html =
        "<h1>Note with image</h1>"
        "<img src=\"data:image/png;base64," + pngB64 + "\" alt=\"pixel\">";

    const QString notePath = tmp.path() + "/in.html";
    QFile in(notePath);
    QVERIFY_X(in.open(QIODevice::WriteOnly), "input write", "could not open input");
    in.write(html.toUtf8());
    in.close();

    const QString mdPath = tmp.path() + "/out.md";
    QString err;
    const bool ok = NoterExport::exportMarkdown(notePath, mdPath, &err);
    EXPECT("export: returns true",       ok);
    EXPECT("export: out.md exists",      QFileInfo::exists(mdPath));
    EXPECT("export: assets dir exists",  QFileInfo::exists(mdPath + ".assets"));

    // Read the md back; assert it references the asset.
    QFile out(mdPath);
    QVERIFY_X(out.open(QIODevice::ReadOnly), "output read", "could not read md");
    const QString md = QString::fromUtf8(out.readAll());
    EXPECT("export: md references asset", md.contains(".assets/img-1.png"));
    EXPECT("export: alt preserved",       md.contains("![pixel]"));
}

// ─────────────────────────────────────────────────────────────────────
// exportPdf — depends on NOTEPATRA_WITH_WEBENGINE
// ─────────────────────────────────────────────────────────────────────

static void test_export_pdf() {
    std::printf("test_export_pdf\n");

    QTemporaryDir tmp;
    QVERIFY_X(tmp.isValid(), "tempdir", "tempdir invalid");

    const QString html =
        "<html><body><h1>PDF test</h1>"
        "<p>This note should land in a PDF.</p></body></html>";
    const QString notePath = tmp.path() + "/in.html";
    QFile in(notePath);
    QVERIFY_X(in.open(QIODevice::WriteOnly), "input write", "could not open input");
    in.write(html.toUtf8());
    in.close();

    const QString pdfPath = tmp.path() + "/out.pdf";
    QString err;
    const bool ok = NoterExport::exportPdf(notePath, pdfPath, &err);

#ifdef NOTEPATRA_WITH_WEBENGINE
    EXPECT("pdf: returns true",      ok);
    EXPECT("pdf: file exists",       QFileInfo::exists(pdfPath));
    EXPECT("pdf: file non-empty",    QFileInfo(pdfPath).size() > 200);
#else
    EXPECT("pdf: lite build refuses cleanly", !ok);
    EXPECT("pdf: lite build reports a reason", !err.isEmpty());
#endif
}

int main(int argc, char *argv[]) {
    qputenv("QT_QPA_PLATFORM", "offscreen");
    QApplication app(argc, argv);

    test_markdown_all_block_types();
    test_markdown_embeds();
    test_export_markdown_with_image();
    test_export_pdf();

    std::printf("\n[%d passed, %d failed]\n", g_pass, g_fail);
    return g_fail == 0 ? 0 : 1;
}
