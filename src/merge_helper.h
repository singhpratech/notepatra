// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef NOTEPATRA_MERGE_HELPER_H
#define NOTEPATRA_MERGE_HELPER_H

// ═══════════════════════════════════════════════════════════════════════
// Merge-marker scanner — v0.1.62 MVP for git conflict resolution.
//
// Scans a text buffer for the standard conflict marker block produced by
// `git merge` / `git rebase` when two branches modify the same lines:
//
//   <<<<<<< HEAD
//   our content
//   =======
//   their content
//   >>>>>>> other-branch
//
// Each scanned region carries enough info for the UI helper to:
//   • render an annotation row above it with Take ours / Take theirs /
//     Take both buttons,
//   • rewrite the surrounding lines when the user picks one option.
//
// Robustness notes:
//   • Nested markers are tolerated — a `<<<<<<<` inside the *content* of
//     an `ours` block is treated as plain text unless it sits at column 0
//     of its line. Same rule for `=======` and `>>>>>>>`.
//   • If the marker triplet is incomplete (e.g. a `<<<<<<<` without a
//     matching `=======` later in the buffer), that opener is skipped
//     and the scanner continues past it.
//   • Line numbers are zero-based to match QScintilla's API.
// ═══════════════════════════════════════════════════════════════════════

#include <QString>
#include <QStringList>
#include <QVector>

namespace MergeHelper {

struct ConflictRegion {
    int startLine     = -1;  // line containing "<<<<<<<"
    int separatorLine = -1;  // line containing "======="
    int endLine       = -1;  // line containing ">>>>>>>"
    QString ours;            // text between start and separator (exclusive of markers)
    QString theirs;          // text between separator and end (exclusive of markers)
    QString oursLabel;       // text after "<<<<<<<", e.g. "HEAD" or "main"
    QString theirsLabel;     // text after ">>>>>>>", e.g. "feature-branch"
};

// Scans `buffer` for conflict regions. `buffer` is split by '\n' so any
// EOL flavour is handled (CR characters are kept in the returned ours /
// theirs strings as-is — that's the user's data).
//
// Returns regions in textual order. An empty result means no markers
// were found (or only malformed ones).
QVector<ConflictRegion> scanConflicts(const QString &buffer);

// Helper for the widget — rebuilds the buffer with one region replaced
// by `replacement` text. The replacement is inserted *in place of all
// three marker lines and everything between them*. Returns the new
// buffer. If `regionIndex` is out of range, returns `buffer` unchanged.
QString applyResolution(const QString &buffer,
                        const QVector<ConflictRegion> &regions,
                        int regionIndex,
                        const QString &replacement);

} // namespace MergeHelper

#endif // NOTEPATRA_MERGE_HELPER_H
