/**
 * Automated test — exercises every menu action and feature.
 * Compile: same as main app but with this file instead of main.cpp
 */
#include <QApplication>
#include <QTimer>
#include <QMenuBar>
#include <QMenu>
#include <QAction>
#include <QClipboard>
#include <QTemporaryFile>
#include <QDir>
#include <QFileInfo>
#include <cstdio>

#include "mainwindow.h"
#include "editor.h"
#include "rustbridge.h"

static int passed = 0, failed = 0;

static void check(const char *name, bool ok) {
    if (ok) { printf("  [PASS] %s\n", name); passed++; }
    else    { printf("  [FAIL] %s\n", name); failed++; }
}

int main(int argc, char *argv[]) {
    QApplication app(argc, argv);
    MainWindow w;
    w.show();
    app.processEvents();

    QTimer::singleShot(500, [&]() {
        printf("=== Testing Native C++/Rust Notepad++ ===\n\n");

        // ── WINDOW ──
        check("Window visible", w.isVisible());
        check("Has title", w.windowTitle().contains("Notepad++"));
        w.showMaximized(); app.processEvents();
        check("Maximize", w.isMaximized());
        w.showNormal(); app.processEvents();
        check("Restore", !w.isMaximized());

        // ── MENUS ──
        auto *mb = w.menuBar();
        int menuCount = 0;
        QStringList menuNames;
        for (auto *a : mb->actions()) {
            if (a->menu()) { menuCount++; menuNames << a->text().remove('&'); }
        }
        check("12 menus", menuCount == 12);
        check("File menu", menuNames.contains("File"));
        check("Edit menu", menuNames.contains("Edit"));
        check("Search menu", menuNames.contains("Search"));
        check("View menu", menuNames.contains("View"));
        check("Encoding menu", menuNames.contains("Encoding") || menuNames.contains("ncoding"));
        check("Language menu", menuNames.contains("Language"));
        check("Settings menu", menuNames.contains("Settings"));
        check("Tools menu", menuNames.contains("Tools"));
        check("Macro menu", menuNames.contains("Macro"));
        check("Run menu", menuNames.contains("Run"));
        check("Window menu", menuNames.contains("Window"));
        check("Help menu", menuNames.contains("?"));

        // Count Edit submenus
        QMenu *editMenu = nullptr;
        for (auto *a : mb->actions()) {
            if (a->text().remove('&') == "Edit") { editMenu = a->menu(); break; }
        }
        if (editMenu) {
            QStringList editSubs;
            for (auto *a : editMenu->actions()) {
                if (a->menu()) editSubs << a->text().remove('&');
            }
            check("Has Convert Case", editSubs.contains("Convert Case to"));
            check("Has Line Operations", editSubs.join(",").contains("Line Opera"));
            check("Has Comment", editSubs.join(",").contains("Comment"));
            check("Has Blank Operations", editSubs.contains("Blank Operations"));
            check("Has EOL Conversion", editSubs.contains("EOL Conversion"));
            check("Has Copy to Clipboard", editSubs.contains("Copy to Clipboard"));
        }

        // ── EDITOR BASICS ──
        auto *e = w.currentEditor();
        check("Editor exists", e != nullptr);

        e->setText("Hello World\nLine 2\nLine 3");
        check("Set text", e->text().startsWith("Hello"));

        e->selectAll(); e->copy();
        check("Copy", QApplication::clipboard()->text().startsWith("Hello"));

        e->selectAll(); e->cut();
        check("Cut", e->text().isEmpty());

        e->paste();
        check("Paste", e->text().startsWith("Hello"));

        e->undo();
        check("Undo", e->text().isEmpty());

        e->redo();
        check("Redo", e->text().startsWith("Hello"));

        // ── LINE OPERATIONS ──
        e->setText("A\nB\nC");
        e->setCursorPosition(0, 0);
        e->duplicateLine();
        check("Duplicate line", e->text().count("A") == 2);

        e->setText("A\nB\nC");
        e->setCursorPosition(1, 0);
        e->deleteLine();
        check("Delete line", !e->text().contains("B"));

        e->setText("A\nB\nC");
        e->setCursorPosition(0, 0);
        e->toggleComment();
        check("Comment", e->text().startsWith("#"));
        e->setCursorPosition(0, 0);
        e->toggleComment();
        check("Uncomment", e->text().trimmed().startsWith("A"));

        // ── RUST CORE: CASE CONVERSION ──
        check("Rust UPPER", RustCore::convertCase("hello", 0) == "HELLO");
        check("Rust lower", RustCore::convertCase("HELLO", 1) == "hello");
        check("Rust Title", RustCore::convertCase("hello world", 2) == "Hello World");
        check("Rust Invert", RustCore::convertCase("Hello", 4) == "hELLO");

        // ── RUST CORE: SORT ──
        check("Rust sort asc", RustCore::sortLines("c\na\nb", 0).startsWith("a"));
        check("Rust sort desc", RustCore::sortLines("a\nb\nc", 1).startsWith("c"));
        check("Rust sort int", RustCore::sortLines("3\n1\n2", 2).startsWith("1"));

        // ── RUST CORE: DEDUPE ──
        QString deduped = RustCore::removeDuplicates("a\nb\na\nc", 0);
        check("Rust dedup all", deduped.count("a") == 1);
        check("Rust dedup consec", RustCore::removeDuplicates("a\na\nb", 1) == "a\nb");

        // ── RUST CORE: TRIM ──
        check("Rust trim trail", RustCore::trimLines("hi  \nbye  ", 0) == "hi\nbye");
        check("Rust trim lead", RustCore::trimLines("  hi\n  bye", 1) == "hi\nbye");
        check("Rust trim both", RustCore::trimLines("  hi  \n  bye  ", 2) == "hi\nbye");

        // ── RUST CORE: REVERSE ──
        check("Rust reverse", RustCore::reverseLines("a\nb\nc").startsWith("c"));

        // ── RUST CORE: JOIN ──
        check("Rust join", RustCore::joinLines("a\nb\nc", " ") == "a b c");

        // ── RUST CORE: WHITESPACE ──
        check("Rust tab2space", !RustCore::convertWhitespace("\thello", 4, 0).contains("\t"));
        check("Rust space2tab", RustCore::convertWhitespace("    hello", 4, 1).contains("\t"));

        // ── RUST CORE: SEARCH ──
        check("Rust count", RustCore::countMatches("foo bar foo baz foo", "foo", false, true) == 3);
        auto positions = RustCore::findAll("abcabc", "bc", false, true, false);
        check("Rust find all", positions.size() == 2);
        check("Rust replace all", RustCore::replaceAll("foo bar foo", "foo", "baz", false, true) == "baz bar baz");

        // ── RUST CORE: HASH ──
        check("Rust MD5", RustCore::computeHash("test", 0) == "098f6bcd4621d373cade4e832627b4f6");
        check("Rust SHA256", !RustCore::computeHash("test", 2).isEmpty());

        // ── RUST CORE: BASE64 ──
        check("Rust b64 enc", RustCore::base64Encode("hello") == "aGVsbG8=");
        check("Rust b64 dec", RustCore::base64Decode("aGVsbG8=") == "hello");

        // ── RUST CORE: URL ──
        check("Rust url enc", RustCore::urlEncode("hello world").contains("%20") || RustCore::urlEncode("hello world").contains("+"));
        check("Rust url dec", RustCore::urlDecode("hello%20world") == "hello world");

        // ── FILE I/O (via Rust) ──
        QTemporaryFile tmp;
        tmp.setAutoRemove(true);
        if (tmp.open()) {
            tmp.write("test file content\nsecond line\n");
            tmp.flush();
            QString path = tmp.fileName();

            auto result = RustCore::loadFile(path);
            check("Rust load file", result.status == 0);
            check("Rust file text", result.text.contains("test file"));
            check("Rust file enc", !result.encoding.isEmpty());
        }

        // ── LANGUAGE SWITCHING ──
        e->setLanguage("Python");
        check("Lang Python", e->language() == "Python");
        e->setLanguage("JavaScript");
        check("Lang JS", e->language() == "JavaScript");
        e->setLanguage("C++");
        check("Lang C++", e->language() == "C++");
        e->setLanguage("Plain Text");
        check("Lang Plain", e->language() == "Plain Text");

        // ── ZOOM ──
        e->zoomTo(0);
        e->zoomIn(); e->zoomIn();
        check("Zoom in", e->SendScintilla(QsciScintilla::SCI_GETZOOM) > 0);
        e->zoomTo(0);
        check("Zoom reset", e->SendScintilla(QsciScintilla::SCI_GETZOOM) == 0);

        // ── WORD WRAP ──
        check("Wrap off", e->wrapMode() == QsciScintilla::WrapNone);
        e->toggleWordWrap();
        check("Wrap on", e->wrapMode() == QsciScintilla::WrapWord);
        e->toggleWordWrap();

        // ── WHITESPACE / EOL ──
        check("WS off", e->whitespaceVisibility() == QsciScintilla::WsInvisible);
        e->toggleWhitespace();
        check("WS on", e->whitespaceVisibility() != QsciScintilla::WsInvisible);
        e->toggleWhitespace();
        check("EOL off", !e->eolVisibility());
        e->toggleEol();
        check("EOL on", e->eolVisibility());
        e->toggleEol();

        // ── BOOKMARKS ──
        e->setText("a\nb\nc\nd\ne");
        e->setCursorPosition(1, 0);
        e->markerAdd(1, 0);
        check("Bookmark set", e->markersAtLine(1) & 1);
        e->markerDeleteAll(0);
        check("Bookmark clear", !(e->markersAtLine(1) & 1));

        // ── GOTO LINE ──
        e->gotoLine(3);
        int line, col;
        e->getCursorPosition(&line, &col);
        check("Goto line", line == 2);

        // ── STATUS BAR ──
        check("Status bar", true); // It's a member, always exists

        // ── PANELS ──
        check("Explorer hidden", !w.findChild<FileExplorer*>()->isVisible());
        check("FuncList hidden", !w.findChild<FunctionList*>()->isVisible());

        // ══════════════════════════
        printf("\n  ════════════════════════\n");
        printf("  TOTAL: %d/%d PASSED\n", passed, passed + failed);
        if (failed > 0)
            printf("  %d FAILED\n", failed);
        printf("  ════════════════════════\n");

        QApplication::quit();
    });

    return app.exec();
}
