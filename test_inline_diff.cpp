// v0.1.111 — unit test for the Myers inline-edit diff mapping.
//
// Pins the user-visible win of swapping the naive line-index diff for the
// real Myers diff: a line inserted ABOVE an unchanged block must highlight
// exactly that one inserted line, not cascade red/green down every shifted
// line below it. The naive (old) renderer got this WRONG; computeInlineDiffRows
// gets it RIGHT.
//
// Pure function — no QApplication needed (links rustbridge + the Rust lib
// only). Runs in the full-suite gate via notepatra_all_tests.

#include "src/inlineedit_diff.h"

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

using InlineDiff::Row;

static int countChangedInserts(const QVector<Row> &rows) {
    int n = 0;
    for (const Row &r : rows) if (r.leftFiller && r.changed) ++n;
    return n;
}
static int countChangedDeletes(const QVector<Row> &rows) {
    int n = 0;
    for (const Row &r : rows) if (r.rightFiller && r.changed) ++n;
    return n;
}

int main() {
    // ── Case 1: the discriminator — an inserted line shifts the block ──────
    // OLD:                NEW:
    //   int a = 1;          int a = 1;
    //   int b = 2;          int log = 0;   <- inserted
    //   int c = 3;          int b = 2;
    //                       int c = 3;
    // Naive index-diff marks b/c as removed+added (whole tail "rewritten").
    // Myers must report exactly ONE insert and ZERO removals.
    {
        const QString oldText = "int a = 1;\nint b = 2;\nint c = 3;";
        const QString newText = "int a = 1;\nint log = 0;\nint b = 2;\nint c = 3;";
        const QVector<Row> rows = InlineDiff::computeInlineDiffRows(oldText, newText);

        EXPECT(countChangedInserts(rows) == 1);  // exactly one added line
        EXPECT(countChangedDeletes(rows) == 0);  // nothing removed

        // The added line is the log line, not the shifted b/c lines.
        bool foundLog = false;
        for (const Row &r : rows) {
            if (r.leftFiller && r.changed) {
                EXPECT(r.right == "int log = 0;");
                foundLog = true;
            }
            // Unchanged lines must NOT be tinted.
            if (!r.leftFiller && !r.rightFiller) {
                EXPECT(!r.changed);
                EXPECT(r.left == r.right);   // equal rows carry both sides
            }
        }
        EXPECT(foundLog);
        // "int b = 2;" survives as an equal (untinted) row on both sides.
        bool bIsEqual = false;
        for (const Row &r : rows)
            if (!r.changed && r.left == "int b = 2;" && r.right == "int b = 2;")
                bIsEqual = true;
        EXPECT(bIsEqual);
    }

    // ── Case 2: intra-block single-line modify (the headline contract) ─────
    // Only line 2 changes: B -> X. Myers emits delete(line2) + insert(line2).
    {
        const QString oldText = "a\nB\nc\nd";
        const QString newText = "a\nX\nc\nd";
        const QVector<Row> rows = InlineDiff::computeInlineDiffRows(oldText, newText);
        EXPECT(countChangedDeletes(rows) == 1);   // exactly B removed
        EXPECT(countChangedInserts(rows) == 1);   // exactly X added
        // a, c, d remain equal/untinted.
        int equalCount = 0;
        for (const Row &r : rows)
            if (!r.changed && !r.leftFiller && !r.rightFiller) ++equalCount;
        EXPECT(equalCount == 3);
    }

    // ── Case 3: identical text — zero changes, no crash ────────────────────
    {
        const QVector<Row> rows =
            InlineDiff::computeInlineDiffRows("x\ny\nz", "x\ny\nz");
        EXPECT(countChangedInserts(rows) == 0);
        EXPECT(countChangedDeletes(rows) == 0);
        for (const Row &r : rows) EXPECT(!r.changed);
    }

    // ── Case 4: trailing-newline off-by-one guard (must not crash) ─────────
    // Qt KeepEmptyParts("a\nb\n") -> 3 parts; Rust from_lines -> 2 lines.
    // The trailing empty part must render neutral, never OOB.
    {
        const QVector<Row> rows =
            InlineDiff::computeInlineDiffRows("a\nb\n", "a\nb\n");
        EXPECT(countChangedInserts(rows) == 0);
        EXPECT(countChangedDeletes(rows) == 0);
    }

    // ── Case 5: pure insert into empty old ─────────────────────────────────
    {
        const QVector<Row> rows = InlineDiff::computeInlineDiffRows("", "new line");
        EXPECT(countChangedInserts(rows) >= 1);
        EXPECT(countChangedDeletes(rows) == 0);
    }

    std::printf("test_inline_diff: all cases passed\n");
    return 0;
}
