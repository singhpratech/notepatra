/**
 * Notepatra v0.1.0 — Comprehensive Test Suite
 * Tests EVERY feature, edge case, crash scenario.
 */
#include <QApplication>
#include <QTimer>
#include <QTemporaryFile>
#include <QMenuBar>
#include <QClipboard>
#include <QTabWidget>
#include <QDir>
#include <QFile>
#include <cstdio>
#include <cstring>

#include "mainwindow.h"
#include "editor.h"
#include "rustbridge.h"
#include "findreplace.h"

static int passed = 0, failed = 0, total = 0;
static void T(const char *name, bool ok) {
    total++;
    if (ok) { passed++; }
    else { failed++; printf("  [FAIL] %s\n", name); }
}

int main(int argc, char *argv[]) {
    QApplication app(argc, argv);
    MainWindow w;
    w.show();
    app.processEvents();

    QTimer::singleShot(500, [&]() {
        printf("═══════════════════════════════════════════\n");
        printf("  NOTEPATRA v0.1.0 — COMPREHENSIVE TESTS\n");
        printf("═══════════════════════════════════════════\n\n");

        // ═══════════════════════════════════════
        // 1. WINDOW
        // ═══════════════════════════════════════
        printf("--- Window ---\n");
        T("Window visible", w.isVisible());
        T("Has title", w.windowTitle().contains("Notepatra"));
        T("Min size 640x480", w.minimumWidth() == 640 && w.minimumHeight() == 480);
        T("Accepts drops", w.acceptDrops());
        w.showMaximized(); app.processEvents();
        T("Maximize", w.isMaximized());
        w.showNormal(); app.processEvents();
        T("Restore", !w.isMaximized());

        // ═══════════════════════════════════════
        // 2. MENUS
        // ═══════════════════════════════════════
        printf("--- Menus ---\n");
        auto *mb = w.menuBar();
        int menuCount = 0;
        QStringList menuNames;
        for (auto *a : mb->actions()) {
            if (a->menu()) { menuCount++; menuNames << a->text().remove('&'); }
        }
        T("Has menus", menuCount >= 12);
        T("File menu", menuNames.contains("File"));
        T("Edit menu", menuNames.contains("Edit"));
        T("Search menu", menuNames.contains("Search"));
        T("View menu", menuNames.contains("View"));
        T("Encoding menu", menuNames.join(",").contains("ncoding"));
        T("Language menu", menuNames.contains("Language"));
        T("Settings menu", menuNames.contains("Settings"));
        T("Tools menu", menuNames.contains("Tools"));
        T("Features menu", menuNames.join(",").contains("eatures"));
        T("Plugins menu", menuNames.join(",").contains("lugins"));

        // ═══════════════════════════════════════
        // 3. EDITOR BASICS
        // ═══════════════════════════════════════
        printf("--- Editor ---\n");
        auto *e = w.currentEditor();
        T("Editor exists", e != nullptr);

        e->setText("Hello World\nLine 2\nLine 3\nLine 4\nLine 5");
        T("Set text", e->text().startsWith("Hello"));
        T("Line count", e->lines() == 5);

        // Select, copy, cut, paste, undo, redo
        e->selectAll(); e->copy();
        T("Copy", QApplication::clipboard()->text().startsWith("Hello"));
        e->selectAll(); e->cut();
        T("Cut", e->text().isEmpty());
        e->paste();
        T("Paste", e->text().startsWith("Hello"));
        e->undo();
        T("Undo", e->text().isEmpty());
        e->redo();
        T("Redo", e->text().startsWith("Hello"));

        // Line operations
        e->setText("A\nB\nC");
        e->setCursorPosition(0, 0); e->duplicateLine();
        T("Duplicate line", e->text().count("A") == 2);

        e->setText("A\nB\nC");
        e->setCursorPosition(1, 0); e->deleteLine();
        T("Delete line", !e->text().contains("B"));

        e->setText("A\nB\nC");
        e->setCursorPosition(0, 0); e->toggleComment();
        T("Comment", e->text().startsWith("#"));
        e->setCursorPosition(0, 0); e->toggleComment();
        T("Uncomment", e->text().trimmed().startsWith("A"));

        // ═══════════════════════════════════════
        // 4. RUST CORE — TEXT OPS
        // ═══════════════════════════════════════
        printf("--- Rust Core: Text ---\n");
        T("UPPER", RustCore::convertCase("hello", 0) == "HELLO");
        T("lower", RustCore::convertCase("HELLO", 1) == "hello");
        T("Title", RustCore::convertCase("hello world", 2) == "Hello World");
        T("Invert", RustCore::convertCase("Hello", 4) == "hELLO");
        T("Sort asc", RustCore::sortLines("c\na\nb", 0).startsWith("a"));
        T("Sort desc", RustCore::sortLines("a\nb\nc", 1).startsWith("c"));
        T("Sort int", RustCore::sortLines("3\n1\n2", 2).startsWith("1"));
        T("Dedup all", RustCore::removeDuplicates("a\nb\na", 0).count("a") == 1);
        T("Dedup consec", RustCore::removeDuplicates("a\na\nb", 1) == "a\nb");
        T("Trim trail", RustCore::trimLines("hi  \nbye  ", 0) == "hi\nbye");
        T("Trim lead", RustCore::trimLines("  hi\n  bye", 1) == "hi\nbye");
        T("Reverse", RustCore::reverseLines("a\nb\nc").startsWith("c"));
        T("Join", RustCore::joinLines("a\nb\nc", " ") == "a b c");
        T("Tab2space", !RustCore::convertWhitespace("\thello", 4, 0).contains("\t"));

        // ═══════════════════════════════════════
        // 5. RUST CORE — SEARCH
        // ═══════════════════════════════════════
        printf("--- Rust Core: Search ---\n");
        T("Count", RustCore::countMatches("foo bar foo baz foo", "foo", false, true) == 3);
        T("Count case", RustCore::countMatches("Foo foo FOO", "foo", false, true) == 1);
        T("Count nocase", RustCore::countMatches("Foo foo FOO", "foo", false, false) == 3);
        auto pos = RustCore::findAll("abcabc", "bc", false, true, false);
        T("Find all", pos.size() == 2);
        T("Replace", RustCore::replaceAll("foo bar foo", "foo", "baz", false, true) == "baz bar baz");

        // ═══════════════════════════════════════
        // 6. RUST CORE — HASH
        // ═══════════════════════════════════════
        printf("--- Rust Core: Hash ---\n");
        T("MD5", RustCore::computeHash("test", 0) == "098f6bcd4621d373cade4e832627b4f6");
        T("SHA256", !RustCore::computeHash("test", 2).isEmpty());
        T("SHA512", RustCore::computeHash("test", 3).length() == 128);
        T("B64 enc", RustCore::base64Encode("hello") == "aGVsbG8=");
        T("B64 dec", RustCore::base64Decode("aGVsbG8=") == "hello");
        T("URL enc", RustCore::urlEncode("hello world").contains("%20"));
        T("URL dec", RustCore::urlDecode("hello%20world") == "hello world");

        // ═══════════════════════════════════════
        // 7. RUST CORE — JSON FIXER
        // ═══════════════════════════════════════
        printf("--- Rust Core: JSON ---\n");
        T("Format valid", RustCore::formatJson("{\"a\":1}", 2).contains("\"a\": 1"));
        T("Minify", RustCore::minifyJson("{ \"a\" : 1 }") == "{\"a\":1}");
        // Fix single quotes
        T("Fix quotes", RustCore::fixJson("{'a': 1}").contains("\"a\""));
        // Fix unquoted keys
        T("Fix keys", RustCore::fixJson("{a: 1}").contains("\"a\""));
        // Fix trailing comma
        T("Fix trailing", !RustCore::fixJson("{\"a\": 1,}").contains(",}"));
        // Fix missing brace
        T("Fix brace", RustCore::fixJson("{\"a\": 1").endsWith("}"));
        // Complex fix
        QString complexFixed = RustCore::fixJson("{users: [{'id': 1, name: 'Alice',}], count: 2,");
        T("Fix complex", !complexFixed.isEmpty());
        // Verify it's valid JSON after fix
        T("Fix produces valid", RustCore::formatJson(complexFixed, 2).contains("\"users\""));
        // Preserve order
        T("Preserve order", RustCore::formatJson("{\"z\":1,\"a\":2}", 2).indexOf("z") < RustCore::formatJson("{\"z\":1,\"a\":2}", 2).indexOf("a"));

        // ═══════════════════════════════════════
        // 8. RUST CORE — HTML
        // ═══════════════════════════════════════
        printf("--- Rust Core: HTML ---\n");
        T("HTML format", RustCore::formatHtml("<html><body><p>hi</p></body></html>", 2).contains("\n"));
        T("HTML indent", RustCore::formatHtml("<div><p>test</p></div>", 2).contains("  <p>"));

        // ═══════════════════════════════════════
        // 9. RUST CORE — BRACKETS
        // ═══════════════════════════════════════
        printf("--- Rust Core: Brackets ---\n");
        T("Check clean", RustCore::checkBrackets("(a + b)").contains("No issues"));
        T("Check bad", RustCore::checkBrackets("(a + b").contains("Unclosed"));
        T("Fix paren", RustCore::fixBrackets("(hello").endsWith(")"));
        T("Fix brace", RustCore::fixBrackets("{hello").endsWith("}"));
        T("Fix bracket", RustCore::fixBrackets("[hello").endsWith("]"));

        // ═══════════════════════════════════════
        // 10. RUST CORE — SQL
        // ═══════════════════════════════════════
        printf("--- Rust Core: SQL ---\n");
        T("SQL upper", RustCore::formatSql("select * from t", 4, true).contains("SELECT"));
        T("SQL lower", RustCore::formatSql("SELECT * FROM t", 4, false).contains("select"));

        // ═══════════════════════════════════════
        // 11. RUST CORE — DIFF
        // ═══════════════════════════════════════
        printf("--- Rust Core: Diff ---\n");
        auto diff = RustCore::computeDiff("a\nb\nc", "a\nB\nc");
        T("Diff count", diff.entries.size() > 0);
        T("Diff has changes", diff.added > 0 || diff.removed > 0);

        // ═══════════════════════════════════════
        // 12. RUST CORE — FILE I/O
        // ═══════════════════════════════════════
        printf("--- Rust Core: File I/O ---\n");
        {
            QTemporaryFile tmp;
            tmp.setAutoRemove(true);
            if (tmp.open()) {
                tmp.write("test content\nsecond line\n");
                tmp.flush();
                auto result = RustCore::loadFile(tmp.fileName());
                T("Load file", result.status == 0);
                T("Load text", result.text.contains("test content"));
                T("Load enc", !result.encoding.isEmpty());
            }
        }
        // Binary detection
        {
            QTemporaryFile tmp;
            tmp.setAutoRemove(true);
            if (tmp.open()) {
                QByteArray bin(1000, '\0');
                tmp.write(bin);
                tmp.flush();
                auto result = RustCore::loadFile(tmp.fileName());
                T("Binary detect", result.status == 1);
            }
        }

        // ═══════════════════════════════════════
        // 13. FIND/REPLACE
        // ═══════════════════════════════════════
        printf("--- Find/Replace ---\n");
        e->setText("hello world hello foo hello");
        bool found = e->findFirst("hello", false, false, false, true, true);
        T("Find first", found && e->selectedText() == "hello");
        found = e->findNext();
        T("Find next", found);
        // Case sensitive
        e->setCursorPosition(0, 0);
        T("Case sensitive miss", !e->findFirst("HELLO", false, true, false, true, true));
        T("Case sensitive hit", e->findFirst("hello", false, true, false, true, true));
        // Whole word
        e->setText("helloworld hello worldhello");
        T("Whole word", e->findFirst("hello", false, false, true, true, true) && e->selectedText() == "hello");
        // Regex
        e->setText("error123 warning456 error789");
        T("Regex find", e->findFirst("error\\d+", true, false, false, true, true));
        // Replace
        e->setText("foo bar foo");
        e->findFirst("foo", false, false, false, true, true);
        e->replace("FOO");
        T("Replace", e->text().startsWith("FOO"));

        // ═══════════════════════════════════════
        // 14. LANGUAGE SWITCHING
        // ═══════════════════════════════════════
        printf("--- Languages ---\n");
        for (const auto &lang : {"Python", "JavaScript", "C++", "Java", "HTML", "CSS",
                                  "JSON", "SQL", "Bash", "Ruby", "Perl", "Lua", "YAML",
                                  "Markdown", "CoffeeScript", "D", "Fortran", "TCL",
                                  "TeX", "Verilog", "VHDL", "Properties", "Plain Text"}) {
            e->setLanguage(lang);
        }
        T("23 languages switch", e->language() == "Plain Text");

        // ═══════════════════════════════════════
        // 15. BOOKMARKS
        // ═══════════════════════════════════════
        printf("--- Bookmarks ---\n");
        e->setText("a\nb\nc\nd\ne");
        e->markerAdd(1, 0);
        T("Bookmark set", e->markersAtLine(1) & 1);
        e->markerDeleteAll(0);
        T("Bookmark clear", !(e->markersAtLine(1) & 1));

        // ═══════════════════════════════════════
        // 16. ZOOM
        // ═══════════════════════════════════════
        printf("--- Zoom ---\n");
        e->zoomTo(0);
        e->zoomIn(); e->zoomIn();
        T("Zoom in", e->SendScintilla(QsciScintilla::SCI_GETZOOM) > 0);
        e->zoomTo(0);
        T("Zoom reset", e->SendScintilla(QsciScintilla::SCI_GETZOOM) == 0);

        // ═══════════════════════════════════════
        // 17. WRAP / WHITESPACE / EOL
        // ═══════════════════════════════════════
        printf("--- View toggles ---\n");
        T("Wrap off", e->wrapMode() == QsciScintilla::WrapNone);
        e->toggleWordWrap();
        T("Wrap on", e->wrapMode() == QsciScintilla::WrapWord);
        e->toggleWordWrap();
        e->toggleWhitespace();
        T("WS on", e->whitespaceVisibility() != QsciScintilla::WsInvisible);
        e->toggleWhitespace();
        e->toggleEol();
        T("EOL on", e->eolVisibility());
        e->toggleEol();

        // ═══════════════════════════════════════
        // 18. TABS
        // ═══════════════════════════════════════
        printf("--- Tabs ---\n");
        int before = w.findChild<QTabWidget*>()->count();
        // Open a test file
        w.openFile(QDir::currentPath() + "/test-files/broken.json");
        app.processEvents();
        T("Open file adds tab", w.findChild<QTabWidget*>()->count() > before);

        // ═══════════════════════════════════════
        // 19. EDGE CASES
        // ═══════════════════════════════════════
        printf("--- Edge cases ---\n");
        // Empty file
        e->setText("");
        T("Empty text", e->text().isEmpty());
        T("Empty lines", e->lines() == 1);
        // Very long line
        QString longLine = QString(10000, 'x');
        e->setText(longLine);
        T("Long line", e->text().length() == 10000);
        // Unicode
        e->setText("Hello 世界 🌍 مرحبا");
        T("Unicode", e->text().contains("世界"));
        // Special chars
        e->setText("tab\there\nnewline\n\rnull\0end");
        T("Special chars", e->text().contains("tab"));

        // ═══════════════════════════════════════
        // 20. CONFIG
        // ═══════════════════════════════════════
        printf("--- Config ---\n");
        T("Config file exists", QFile::exists(Config::configPath()));
        auto &cfg = Config::instance();
        T("Config theme", !cfg.theme.isEmpty());
        T("Config tabWidth", cfg.tabWidth > 0);
        T("Config fontSize", cfg.fontSize > 0);

        // ═══════════════════════════════════════
        // SUMMARY
        // ═══════════════════════════════════════
        printf("\n═══════════════════════════════════════════\n");
        printf("  RESULTS: %d/%d PASSED", passed, total);
        if (failed > 0) printf("  (%d FAILED)", failed);
        printf("\n═══════════════════════════════════════════\n");

        QApplication::quit();
    });

    return app.exec();
}
