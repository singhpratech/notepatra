// Slice B+C smoke test for EditPlanList / DiffView.
//
// We deliberately keep this tiny: construct EditPlanList, push three
// synthetic rows, call clear(), construct a DiffView directly, and exit.
// The harness runs with QT_QPA_PLATFORM=offscreen (set by
// notepatra_add_qt_test in CMakeLists.txt) so no display is required.
//
// What this protects against:
//   • EditPlanList constructor blowing up when there is no workspace root
//   • addEdit() crashing on synthetic before/after content
//   • clear() removing rows without leaking widgets in a way that crashes
//     during QApplication::processEvents
//   • DiffView constructing with non-trivial inputs and computing tints

#include "src/diff_view.h"
#include "src/edit_plan.h"

#include <QApplication>
#include <QCoreApplication>
#include <QString>
#include <QtGlobal>

#include <cstdio>
#include <cstdlib>

#define EXPECT(cond) \
    do { \
        if (!(cond)) { \
            std::fprintf(stderr, "FAIL: %s line %d: %s\n", __FILE__, __LINE__, #cond); \
            std::exit(1); \
        } \
    } while (0)

int main(int argc, char **argv) {
    // QApplication (not QCoreApplication) — DiffView/EditPlanList own
    // QWidget children that need a GUI app instance even under -platform
    // offscreen.
    QApplication app(argc, argv);

    // ── Slice C: EditPlanList ────────────────────────────────────────────
    EditPlanList plan;
    EXPECT(plan.count() == 0);

    plan.setWorkspaceRoot(QStringLiteral("/tmp/synthetic"));

    plan.addEdit(QStringLiteral("/tmp/synthetic/a.cpp"),
                 QStringLiteral("int main() {\n    return 0;\n}\n"),
                 QStringLiteral("int main() {\n    // hi\n    return 1;\n}\n"));
    EXPECT(plan.count() == 1);

    plan.addEdit(QStringLiteral("/tmp/synthetic/long/nested/path/to/some/file.h"),
                 QStringLiteral(""),
                 QStringLiteral("#pragma once\nclass Foo {};\n"));
    EXPECT(plan.count() == 2);

    plan.addEdit(QStringLiteral("/elsewhere/outside_workspace.txt"),
                 QStringLiteral("delete me\n"),
                 QStringLiteral(""));
    EXPECT(plan.count() == 3);

    // Process pending events so any deferred construction runs at least
    // once — this catches lazy-init bugs that wouldn't fire in pure
    // construct/destruct ordering.
    QCoreApplication::processEvents();

    plan.clear();
    EXPECT(plan.count() == 0);

    // Add and clear again to make sure we can re-use the widget. Earlier
    // versions of the row container left a stale stretch entry behind which
    // would crash on the second insertWidget(); this guards against
    // regressions.
    plan.addEdit(QStringLiteral("/tmp/x.cpp"),
                 QStringLiteral("a\nb\nc\n"),
                 QStringLiteral("a\nB\nc\n"));
    EXPECT(plan.count() == 1);
    plan.clear();
    EXPECT(plan.count() == 0);

    // ── v0.1.110: apply-blocker fix — markApplied() marks the right rows ──
    // Contract: after the host writes files, markApplied(paths) flips exactly
    // those rows to applied so the UI reflects reality and they're excluded
    // from any later Apply (no silent double-write).
    EditPlanList plan2;
    plan2.setWorkspaceRoot(QStringLiteral("/tmp/syn2"));
    plan2.addEdit(QStringLiteral("/tmp/syn2/a.cpp"), QStringLiteral("x\n"), QStringLiteral("y\n"));
    plan2.addEdit(QStringLiteral("/tmp/syn2/b.cpp"), QStringLiteral("p\n"), QStringLiteral("q\n"));
    EXPECT(plan2.count() == 2);
    EXPECT(plan2.appliedCount() == 0);
    plan2.markApplied({QStringLiteral("/tmp/syn2/a.cpp")});
    EXPECT(plan2.appliedCount() == 1);                 // exactly the marked path
    plan2.markApplied({QStringLiteral("/tmp/syn2/a.cpp")});
    EXPECT(plan2.appliedCount() == 1);                 // idempotent — no double count
    plan2.markApplied({QStringLiteral("/tmp/syn2/nonexistent.cpp")});
    EXPECT(plan2.appliedCount() == 1);                 // unknown path is a no-op
    plan2.markApplied({QStringLiteral("/tmp/syn2/b.cpp")});
    EXPECT(plan2.appliedCount() == 2);                 // both now applied
    QCoreApplication::processEvents();

    // ── Slice B: DiffView ────────────────────────────────────────────────
    // Construct directly so we exercise the Rust diff path without needing
    // to click [Diff] on a hidden row.
    DiffView dv(QStringLiteral("hello\nworld\n"),
                QStringLiteral("hello\nbeautiful\nworld\n"));
    Q_UNUSED(dv);

    // Empty inputs — should not crash. Use brace-init to dodge GCC's
    // most-vexing-parse warning (DiffView empty(QString(), QString()) parses
    // as a function declaration).
    DiffView empty{QString(), QString()};
    Q_UNUSED(empty);

    // Identical inputs — no tints, just shouldn't blow up.
    DiffView same{QStringLiteral("equal\n"), QStringLiteral("equal\n")};
    Q_UNUSED(same);

    QCoreApplication::processEvents();

    std::fprintf(stdout, "test_edit_plan: ok\n");
    return 0;
}
