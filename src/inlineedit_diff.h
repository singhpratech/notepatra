// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef INLINEEDIT_DIFF_H
#define INLINEEDIT_DIFF_H

// ─────────────────────────────────────────────────────────────────────
// Pure, widget-free line-diff mapping for the Ctrl+I inline-edit dialog.
//
// The dialog renders a side-by-side diff of the user's selection (old)
// vs the AI rewrite (new). The historic renderer compared lines by INDEX
// (old[i] == new[i]), which mis-aligns the moment a line is inserted or
// removed: every subsequent line reads as changed, so a one-line insert
// looked like a whole-block rewrite.
//
// computeInlineDiffRows() drives the render off the real Myers diff
// (RustCore::computeDiff, the same engine DiffView/EditPlan use), emitting
// one aligned visual row per diff entry. Inserts get a filler cell on the
// old side; deletes get a filler cell on the new side. The result is true
// alignment: a one-line insert highlights exactly one line.
//
// Kept free of any QWidget include so it unit-tests without a QApplication
// (mirrors the ai_context / chart_spec_to_vega pure-function pattern).
// ─────────────────────────────────────────────────────────────────────

#include <QString>
#include <QVector>

namespace InlineDiff {

struct Row {
    QString left;        // old-side line text ("" when leftFiller)
    QString right;       // new-side line text ("" when rightFiller)
    bool    leftFiller;  // old side is a blank spacer (this row is an insert)
    bool    rightFiller; // new side is a blank spacer (this row is a delete)
    bool    changed;     // tint the non-filler side (red=remove / green=add)
};

// Map (oldText, newText) onto aligned visual rows using the real Myers
// diff. Equal lines produce a neutral row with both sides populated;
// a deleted line produces a red row with a right-side filler; an inserted
// line produces a green row with a left-side filler.
QVector<Row> computeInlineDiffRows(const QString &oldText, const QString &newText);

} // namespace InlineDiff

#endif // INLINEEDIT_DIFF_H
