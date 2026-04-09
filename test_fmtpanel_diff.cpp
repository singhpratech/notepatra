// test_fmtpanel_diff.cpp — direct unit test of FormatterPanel::recordFix()
// to verify the Show Diff button enables for ANY action including AI Fix.
//
// Bypasses the GUI entirely. Creates a FormatterPanel programmatically,
// calls recordFix() with sample inputs, then introspects the panel to
// verify hasLastFix() returns true and the button is enabled.

#include <QApplication>
#include <QPushButton>
#include <cstdio>

#include "fmtpanel.h"

int main(int argc, char *argv[]) {
    QApplication app(argc, argv);

    int failed = 0;

    // Test 1: recordFix with non-empty before/after
    {
        FormatterPanel p("Test JSON Tools", "JSON");
        if (p.hasLastFix()) {
            fprintf(stderr, "✗ Test 1: hasLastFix() should be false BEFORE recordFix\n");
            failed++;
        }
        p.recordFix("{broken}", "{\"fixed\":true}", "AI Fix (Ollama)");
        if (!p.hasLastFix()) {
            fprintf(stderr, "✗ Test 1: hasLastFix() should be true AFTER recordFix\n");
            failed++;
        } else {
            fprintf(stdout, "✓ Test 1: recordFix enables hasLastFix()\n");
        }
        if (p.lastFixInput() != "{broken}") {
            fprintf(stderr, "✗ Test 1: lastFixInput mismatch — got '%s'\n",
                    p.lastFixInput().toStdString().c_str());
            failed++;
        }
        if (p.lastFixOutput() != "{\"fixed\":true}") {
            fprintf(stderr, "✗ Test 1: lastFixOutput mismatch\n");
            failed++;
        }
    }

    // Test 2: recordFix when before == after (no actual change)
    {
        FormatterPanel p("Test", "JSON");
        p.recordFix("{\"a\":1}", "{\"a\":1}", "Format");
        // With my latest change, this should STILL enable (only empty input skipped)
        if (!p.hasLastFix()) {
            fprintf(stderr, "✗ Test 2: hasLastFix() should be true even when before==after\n");
            failed++;
        } else {
            fprintf(stdout, "✓ Test 2: recordFix enables even on no-op (so user can verify)\n");
        }
    }

    // Test 3: recordFix with empty input (should skip)
    {
        FormatterPanel p("Test", "JSON");
        p.recordFix("", "{}", "AI Fix (Ollama)");
        if (p.hasLastFix()) {
            fprintf(stderr, "✗ Test 3: hasLastFix() should be FALSE for empty input\n");
            failed++;
        } else {
            fprintf(stdout, "✓ Test 3: empty input correctly skipped\n");
        }
    }

    // Test 4: showDiffRequested signal fires when button is clicked
    {
        FormatterPanel p("Test", "JSON");
        p.recordFix("a", "b", "Test Action");

        bool signalFired = false;
        QString sigBefore, sigAfter, sigTitle;
        QObject::connect(&p, &FormatterPanel::showDiffRequested,
                         [&](const QString &b, const QString &a, const QString &t) {
            signalFired = true;
            sigBefore = b;
            sigAfter = a;
            sigTitle = t;
        });

        // Find the Show Diff button by walking children — it has tooltip
        // mentioning "side-by-side compare"
        QPushButton *diffBtn = nullptr;
        for (auto *btn : p.findChildren<QPushButton *>()) {
            if (btn->text() == "Show Diff") { diffBtn = btn; break; }
        }
        if (!diffBtn) {
            fprintf(stderr, "✗ Test 4: Show Diff button not found in panel\n");
            failed++;
        } else if (!diffBtn->isEnabled()) {
            fprintf(stderr, "✗ Test 4: Show Diff button is DISABLED after recordFix\n");
            failed++;
        } else {
            // Simulate a click
            diffBtn->click();
            if (!signalFired) {
                fprintf(stderr, "✗ Test 4: showDiffRequested signal did not fire\n");
                failed++;
            } else if (sigBefore != "a" || sigAfter != "b") {
                fprintf(stderr, "✗ Test 4: signal payload wrong\n");
                failed++;
            } else {
                fprintf(stdout, "✓ Test 4: button click → signal fires with correct payload (title='%s')\n",
                        sigTitle.toStdString().c_str());
            }
        }
    }

    if (failed == 0) {
        fprintf(stdout, "\n=== ALL %d TESTS PASS ===\n", 4);
        return 0;
    } else {
        fprintf(stderr, "\n=== %d TESTS FAILED ===\n", failed);
        return 1;
    }
}
