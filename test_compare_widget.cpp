#include <QApplication>
#include <QCheckBox>
#include <QPushButton>
#include <Qsci/qsciscintilla.h>
#include <cstdio>

#include "compare.h"

static QCheckBox *findCheckBox(CompareWidget &widget, const QString &label) {
    for (QCheckBox *check : widget.findChildren<QCheckBox *>()) {
        if (check->text() == label) return check;
    }
    return nullptr;
}

static QsciScintilla *findEditor(CompareWidget &widget, const QString &objectName) {
    return widget.findChild<QsciScintilla *>(objectName);
}

static QPushButton *findButton(CompareWidget &widget, const QString &objectName) {
    return widget.findChild<QPushButton *>(objectName);
}

int main(int argc, char *argv[]) {
    QApplication app(argc, argv);

    int failed = 0;

    // Test 1: ignore spaces must change the actual diff result, not just the UI.
    {
        CompareWidget widget;
        widget.resize(1100, 720);
        widget.show();
        widget.compare("alpha = 1;\nshared\n", "left.cpp",
                       "alpha=1;\nshared\n", "right.cpp");
        QApplication::processEvents();

        auto *navBar = widget.findChild<CompareNavBar *>("compareNavBar");
        if (!navBar) {
            std::fprintf(stderr, "✗ Test 1: compare nav bar not found\n");
            failed++;
        } else if (navBar->diffMarkerCount() != 1) {
            std::fprintf(stderr, "✗ Test 1: expected 1 nav marker before ignore-spaces, got %d\n",
                         navBar->diffMarkerCount());
            failed++;
        }

        if (widget.diffCount() != 1) {
            std::fprintf(stderr, "✗ Test 1: expected 1 diff before ignore-spaces, got %d\n",
                         widget.diffCount());
            failed++;
        }

        auto *ignoreSpaces = findCheckBox(widget, "Ignore spaces");
        if (!ignoreSpaces) {
            std::fprintf(stderr, "✗ Test 1: Ignore spaces checkbox not found\n");
            failed++;
        } else {
            ignoreSpaces->setChecked(true);
            QApplication::processEvents();

            if (widget.diffCount() != 0) {
                std::fprintf(stderr, "✗ Test 1: ignore spaces should reduce diff count to 0, got %d\n",
                             widget.diffCount());
                failed++;
            } else {
                std::fprintf(stdout, "✓ Test 1: ignore spaces affects real diffing\n");
            }

            if (navBar && navBar->diffMarkerCount() != 0) {
                std::fprintf(stderr, "✗ Test 1: nav bar should show 0 diff markers after ignore-spaces\n");
                failed++;
            }
        }
    }

    // Test 2: ignore empty lines should collapse empty-line-only diffs.
    {
        CompareWidget widget;
        widget.resize(1100, 720);
        widget.show();
        widget.compare("one\n\n\ntwo\n", "left.txt",
                       "one\ntwo\n", "right.txt");
        QApplication::processEvents();

        if (widget.diffCount() == 0) {
            std::fprintf(stderr, "✗ Test 2: expected at least one diff before ignore-empty-lines\n");
            failed++;
        }

        auto *ignoreEmpty = findCheckBox(widget, "Ignore empty lines");
        if (!ignoreEmpty) {
            std::fprintf(stderr, "✗ Test 2: Ignore empty lines checkbox not found\n");
            failed++;
        } else {
            ignoreEmpty->setChecked(true);
            QApplication::processEvents();

            if (widget.diffCount() != 0) {
                std::fprintf(stderr, "✗ Test 2: ignore empty lines should reduce diff count to 0, got %d\n",
                             widget.diffCount());
                failed++;
            } else {
                std::fprintf(stdout, "✓ Test 2: ignore empty lines affects real diffing\n");
            }
        }
    }

    // Test 3: nav bar metadata must stay in sync with the rendered compare rows.
    {
        CompareWidget widget;
        widget.resize(1100, 720);
        widget.show();
        widget.compare("a\nb\nc\nd\ne\nf\n", "left.cpp",
                       "a\nB\nc\nD\ne\nf\n", "right.cpp");
        QApplication::processEvents();

        auto *navBar = widget.findChild<CompareNavBar *>("compareNavBar");
        if (!navBar) {
            std::fprintf(stderr, "✗ Test 3: compare nav bar not found\n");
            failed++;
        } else {
            if (navBar->totalRows() != widget.rowCount()) {
                std::fprintf(stderr, "✗ Test 3: nav bar row count mismatch (%d vs %d)\n",
                             navBar->totalRows(), widget.rowCount());
                failed++;
            } else {
                std::fprintf(stdout, "✓ Test 3: nav bar row count matches compare rows\n");
            }

            if (navBar->diffMarkerCount() != widget.diffCount()) {
                std::fprintf(stderr, "✗ Test 3: nav bar diff marker mismatch (%d vs %d)\n",
                             navBar->diffMarkerCount(), widget.diffCount());
                failed++;
            }

            if (navBar->visibleRows() <= 0) {
                std::fprintf(stderr, "✗ Test 3: nav bar viewport not initialised\n");
                failed++;
            }
        }
    }

    // Test 4: changed rows must not use identical visual markers on both panes.
    {
        CompareWidget widget;
        widget.resize(1100, 720);
        widget.show();
        widget.compare("alpha\nbefore value\nomega\n", "left.txt",
                       "alpha\nafter value\nomega\n", "right.txt");
        QApplication::processEvents();

        auto *leftEditor = findEditor(widget, "compareLeftEditor");
        auto *rightEditor = findEditor(widget, "compareRightEditor");
        if (!leftEditor || !rightEditor) {
            std::fprintf(stderr, "✗ Test 4: compare editors not found\n");
            failed++;
        } else {
            const unsigned leftMarkers = leftEditor->markersAtLine(1);
            const unsigned rightMarkers = rightEditor->markersAtLine(1);
            if (leftMarkers == 0 || rightMarkers == 0) {
                std::fprintf(stderr, "✗ Test 4: changed row should have visible markers on both panes\n");
                failed++;
            } else if (leftMarkers == rightMarkers) {
                std::fprintf(stderr, "✗ Test 4: changed row markers should differ between left and right panes\n");
                failed++;
            } else {
                std::fprintf(stdout, "✓ Test 4: changed rows use pane-specific markers\n");
            }
        }
    }

    // Test 5: compare panes should preserve syntax highlighting based on file name.
    {
        CompareWidget widget;
        widget.resize(1100, 720);
        widget.show();
        widget.compare("select a.Zip,\n  b.GeoName\nfrom demo;\n", "left.sql",
                       "select a.Zip,\n  b.GeoName,\n  b.State\nfrom demo;\n", "right.sql");
        QApplication::processEvents();

        auto *leftEditor = findEditor(widget, "compareLeftEditor");
        auto *rightEditor = findEditor(widget, "compareRightEditor");
        if (!leftEditor || !rightEditor) {
            std::fprintf(stderr, "✗ Test 5: compare editors not found\n");
            failed++;
        } else if (!leftEditor->lexer() || !rightEditor->lexer()) {
            std::fprintf(stderr, "✗ Test 5: SQL compare should keep syntax highlighting active\n");
            failed++;
        } else {
            std::fprintf(stdout, "✓ Test 5: syntax highlighting stays active in compare panes\n");
        }
    }

    // Test 6: unlock-editing should allow in-place edits and relock should recompare them.
    {
        CompareWidget widget;
        widget.resize(1100, 720);
        widget.show();
        widget.compare("alpha\nbeta\n", "left.txt",
                       "alpha\ngamma\n", "right.txt");
        QApplication::processEvents();

        auto *leftEditor = findEditor(widget, "compareLeftEditor");
        auto *rightEditor = findEditor(widget, "compareRightEditor");
        auto *editToggle = findButton(widget, "compareEditToggle");
        if (!leftEditor || !rightEditor || !editToggle) {
            std::fprintf(stderr, "✗ Test 6: compare editors or edit toggle not found\n");
            failed++;
        } else if (!leftEditor->isReadOnly() || !rightEditor->isReadOnly()) {
            std::fprintf(stderr, "✗ Test 6: compare editors should start locked\n");
            failed++;
        } else {
            editToggle->click();
            QApplication::processEvents();

            if (leftEditor->isReadOnly() || rightEditor->isReadOnly()) {
                std::fprintf(stderr, "✗ Test 6: edit toggle should unlock both panes\n");
                failed++;
            } else {
                rightEditor->setText("alpha\nbeta\n");
                QApplication::processEvents();

                editToggle->click();
                QApplication::processEvents();

                if (widget.diffCount() != 0) {
                    std::fprintf(stderr, "✗ Test 6: relocking after edits should recompare current pane text\n");
                    failed++;
                } else {
                    std::fprintf(stdout, "✓ Test 6: unlock/lock editing preserves pane edits for recompare\n");
                }
            }
        }
    }

    if (failed == 0) {
        std::fprintf(stdout, "\n=== ALL %d TESTS PASS ===\n", 6);
        return 0;
    }

    std::fprintf(stderr, "\n=== %d TESTS FAILED ===\n", failed);
    return 1;
}
