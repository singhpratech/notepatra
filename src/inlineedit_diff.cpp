// SPDX-License-Identifier: GPL-3.0-or-later

#include "inlineedit_diff.h"
#include "rustbridge.h"

#include <QStringList>

namespace InlineDiff {

QVector<Row> computeInlineDiffRows(const QString &oldText, const QString &newText) {
    // Split the same way DiffView does (KeepEmptyParts) so a trailing
    // newline keeps its line. The Rust diff splits internally too; the
    // bounds guards below absorb the one-line off-by-one between Qt's
    // KeepEmptyParts ("a\nb\n" → 3 parts) and similar::from_lines
    // ("a\nb\n" → 2 lines) — the trailing empty part is simply never
    // referenced by any entry, so it renders neutral. Do NOT "fix" that
    // by resizing; the guards keep it safe.
    const QStringList oldLines = oldText.split('\n', Qt::KeepEmptyParts);
    const QStringList newLines = newText.split('\n', Qt::KeepEmptyParts);

    const RustCore::DiffInfo diff = RustCore::computeDiff(oldText, newText);

    QVector<Row> rows;
    rows.reserve(diff.entries.size());
    for (const RustCore::DiffEntry &e : diff.entries) {
        Row r;
        r.leftFiller = r.rightFiller = r.changed = false;
        if (e.tag == 0) {
            // Equal — populate both sides. Prefer the entry text; fall back
            // to the split arrays if a 1-indexed line is in range.
            r.left  = (e.leftLine  > 0 && e.leftLine  <= oldLines.size())
                          ? oldLines.at(e.leftLine - 1) : e.text;
            r.right = (e.rightLine > 0 && e.rightLine <= newLines.size())
                          ? newLines.at(e.rightLine - 1) : e.text;
        } else if (e.tag == 2) {
            // Delete — old line on the left, filler on the right.
            r.left  = (e.leftLine > 0 && e.leftLine <= oldLines.size())
                          ? oldLines.at(e.leftLine - 1) : e.text;
            r.right = QString();
            r.rightFiller = true;
            r.changed = true;
        } else { // e.tag == 1 — insert
            // Insert — filler on the left, new line on the right.
            r.left  = QString();
            r.leftFiller = true;
            r.right = (e.rightLine > 0 && e.rightLine <= newLines.size())
                          ? newLines.at(e.rightLine - 1) : e.text;
            r.changed = true;
        }
        rows.append(r);
    }
    return rows;
}

} // namespace InlineDiff
