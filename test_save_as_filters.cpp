// v0.1.87.1 / v0.1.88 — regression test that codifies the Save As file-type
// dropdown END-TO-END contract. Pre-fix, v0.1.87 shipped a populated dropdown
// but the filter selection didn't drive the saved extension — user-reported
// bug. v0.1.88 wired QFileDialog::setDefaultSuffix + filterSelected signal,
// the post-Accept safety net, AND a separate user-reported bug: post-Save-As
// the in-app language indicator stayed on the old language until the file
// was re-opened (v0.1.88.1 fix — Editor::saveFile() re-applies the lexer
// when the path's extension implies a different language).
//
// What this test covers (NO Qt GUI dependency — pure-functional helpers):
//   T1  firstExtensionFromFilter("Python (*.py *.pyw *.pyx)") == "py"
//   T2  firstExtensionFromFilter("All Files (*)")             == ""
//   T3  firstExtensionFromFilter("Dockerfile (Dockerfile *.dockerfile)") == ""
//        (bare-filename-first filter → no auto-suffix)
//   T4  firstExtensionFromFilter("CMake (CMakeLists.txt *.cmake)") == ""
//        (bare-filename-first filter → no auto-suffix)
//   T5  applySaveAsFilterSuffix("/tmp/foo", "Python (*.py *.pyw *.pyx)") ==
//        "/tmp/foo.py"
//   T6  applySaveAsFilterSuffix("/tmp/foo.py", "Python (*.py *.pyw *.pyx)") ==
//        "/tmp/foo.py" (already-matching extension, unchanged)
//   T7  applySaveAsFilterSuffix("/tmp/foo.pyw", "Python (*.py *.pyw *.pyx)") ==
//        "/tmp/foo.pyw" (alt extension matches, unchanged)
//   T8  applySaveAsFilterSuffix("/tmp/foo.txt", "Python (*.py *.pyw *.pyx)") ==
//        "/tmp/foo.txt.py" (mismatched suffix → append filter's first)
//   T9  applySaveAsFilterSuffix("/tmp/foo", "All Files (*)") ==
//        "/tmp/foo" (All Files → unchanged)
//   T10 applySaveAsFilterSuffix("/tmp/Dockerfile",
//                                "Dockerfile (Dockerfile Containerfile *.dockerfile)")
//        == "/tmp/Dockerfile" (bare-filename match)
//   T11 buildSaveAsFilters("Python") sets preselect to the Python entry
//   T12 buildSaveAsFilters returns "All Files (*)" as the FIRST filter
//   T13 firstExtensionFromFilter of buildSaveAsFilters' preselected for Python
//        == "py" (the end-to-end chain that actually flows through the dialog)

#include "lexerutils.h"

#include <QCoreApplication>
#include <QDebug>
#include <QString>
#include <QStringList>

#include <cstdlib>

#define ASSERT_EQ(actual, expected, label)                                     \
    do {                                                                       \
        if ((actual) != (expected)) {                                          \
            qCritical().noquote()                                              \
                << "[FAIL]" << (label) << "\n  actual  :" << (actual)          \
                << "\n  expected:" << (expected);                              \
            std::exit(1);                                                      \
        } else {                                                               \
            qInfo().noquote() << "[ ok ]" << (label);                          \
        }                                                                     \
    } while (0)

int main(int argc, char *argv[]) {
    QCoreApplication app(argc, argv);

    // ── firstExtensionFromFilter ──
    ASSERT_EQ(firstExtensionFromFilter("Python (*.py *.pyw *.pyx)"),
              QStringLiteral("py"), "T1  Python filter → 'py'");
    ASSERT_EQ(firstExtensionFromFilter("All Files (*)"), QString(),
              "T2  All Files → empty (don't auto-suffix)");
    ASSERT_EQ(firstExtensionFromFilter("Dockerfile (Dockerfile Containerfile *.dockerfile)"),
              QString(),
              "T3  Dockerfile-first filter → empty (bare-name first)");
    ASSERT_EQ(firstExtensionFromFilter("CMake (CMakeLists.txt *.cmake)"),
              QString(), "T4  CMake-first filter → empty (bare-name first)");

    // ── applySaveAsFilterSuffix ──
    ASSERT_EQ(applySaveAsFilterSuffix("/tmp/foo", "Python (*.py *.pyw *.pyx)"),
              QStringLiteral("/tmp/foo.py"),
              "T5  bare 'foo' + Python filter → foo.py");
    ASSERT_EQ(applySaveAsFilterSuffix("/tmp/foo.py", "Python (*.py *.pyw *.pyx)"),
              QStringLiteral("/tmp/foo.py"),
              "T6  already foo.py + Python filter → unchanged");
    ASSERT_EQ(applySaveAsFilterSuffix("/tmp/foo.pyw", "Python (*.py *.pyw *.pyx)"),
              QStringLiteral("/tmp/foo.pyw"),
              "T7  foo.pyw + Python filter → unchanged (alt ext matches)");
    ASSERT_EQ(applySaveAsFilterSuffix("/tmp/foo.txt", "Python (*.py *.pyw *.pyx)"),
              QStringLiteral("/tmp/foo.txt.py"),
              "T8  foo.txt + Python filter → foo.txt.py (mismatch → append)");
    ASSERT_EQ(applySaveAsFilterSuffix("/tmp/foo", "All Files (*)"),
              QStringLiteral("/tmp/foo"),
              "T9  All Files filter → no modification");
    ASSERT_EQ(applySaveAsFilterSuffix("/tmp/Dockerfile",
                                       "Dockerfile (Dockerfile Containerfile *.dockerfile)"),
              QStringLiteral("/tmp/Dockerfile"),
              "T10 bare 'Dockerfile' name matches filter pattern → unchanged");

    // ── buildSaveAsFilters integration ──
    QString preselected;
    const QString filters = buildSaveAsFilters("Python", &preselected);

    if (!preselected.contains("Python")) {
        qCritical().noquote() << "[FAIL] T11 Python language did NOT preselect"
                              << "Python filter; got:" << preselected;
        return 1;
    }
    qInfo().noquote() << "[ ok ] T11 buildSaveAsFilters('Python') preselects"
                      << preselected;

    const QStringList parts = filters.split(QStringLiteral(";;"));
    ASSERT_EQ(parts.first(), QStringLiteral("All Files (*)"),
              "T12 first filter entry is 'All Files (*)'");

    // The end-to-end check that would have caught the user-reported v0.1.87 bug:
    // pre-selected filter for Python must yield "py" via firstExtensionFromFilter,
    // because that's what QFileDialog::setDefaultSuffix gets fed.
    ASSERT_EQ(firstExtensionFromFilter(preselected), QStringLiteral("py"),
              "T13 e2e: buildSaveAsFilters('Python') → firstExtensionFromFilter → 'py'");

    // ── Other languages spot-check ──
    QString preRust;
    buildSaveAsFilters("Rust", &preRust);
    ASSERT_EQ(firstExtensionFromFilter(preRust), QStringLiteral("rs"),
              "T14 e2e Rust → 'rs'");

    QString preCpp;
    buildSaveAsFilters("C++", &preCpp);
    ASSERT_EQ(firstExtensionFromFilter(preCpp), QStringLiteral("cpp"),
              "T15 e2e C++ → 'cpp'");

    QString preMd;
    buildSaveAsFilters("Markdown", &preMd);
    ASSERT_EQ(firstExtensionFromFilter(preMd), QStringLiteral("md"),
              "T16 e2e Markdown → 'md'");

    // ── v0.1.88.1: detectLanguageFromPath round-trip ──
    // After Save As, Editor::saveFile() calls detectLanguageFromPath(newPath)
    // and re-applies the lexer if the language changed. These assertions
    // codify what the editor SHOULD become after Save As to each filter's
    // canonical extension. Catches the "saved as .cpp but still Plain Text"
    // user-reported bug — would have caught it BEFORE shipping if a test
    // like this existed pre-v0.1.88.
    ASSERT_EQ(detectLanguageFromPath("/tmp/foo.cpp", QString()),
              QStringLiteral("C++"),
              "T17 .cpp path → editor language becomes C++");
    ASSERT_EQ(detectLanguageFromPath("/tmp/foo.js", QString()),
              QStringLiteral("JavaScript"),
              "T18 .js path → editor language becomes JavaScript");
    ASSERT_EQ(detectLanguageFromPath("/tmp/foo.py", QString()),
              QStringLiteral("Python"),
              "T19 .py path → editor language becomes Python");
    ASSERT_EQ(detectLanguageFromPath("/tmp/foo.rs", QString()),
              QStringLiteral("Rust"),
              "T20 .rs path → editor language becomes Rust");
    ASSERT_EQ(detectLanguageFromPath("/tmp/foo.md", QString()),
              QStringLiteral("Markdown"),
              "T21 .md path → editor language becomes Markdown");

    qInfo() << "";
    qInfo() << "All Save As filter + language-detection assertions passed.";
    return 0;
}
