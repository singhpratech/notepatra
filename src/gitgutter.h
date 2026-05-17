// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef GITGUTTER_H
#define GITGUTTER_H

#include <QString>
#include <QStringList>
#include <QVector>
#include <QProcess>

struct GitLineStatus {
    int line;       // 1-based line number
    int status;     // 0=unchanged, 1=added, 2=modified, 3=deleted
};

// v0.1.62 — A single contiguous diff hunk parsed from `git diff` /
// `diff --no-index` output, in the form needed by the gutter-popup
// stage / revert flow. Line numbers are 1-based, matching git's
// unified-diff convention.
//
// `rawDiffLines` is the literal hunk body (the lines that begin with
// ' ', '+' or '-'). It does NOT include the `@@` header line — that
// is reconstructed from oldStart/oldLen/newStart/newLen when we
// synthesize a patch for `git apply --cached`. Keeping the body
// verbatim means CRLF / trailing-newline quirks survive the round
// trip intact, which is what `git apply` is most strict about.
struct DiffHunk {
    int oldStart = 0;          // 1-based line in HEAD blob ('-' side)
    int oldLen   = 0;          // number of '-' / ' ' lines in hunk
    int newStart = 0;          // 1-based line in working copy ('+' side)
    int newLen   = 0;          // number of '+' / ' ' lines in hunk
    QString hunkHeader;        // verbatim "@@ -a,b +c,d @@" line
    QStringList rawDiffLines;  // hunk body, one element per line
};

class GitGutter {
public:
    static QVector<GitLineStatus> getChangedLines(const QString &filePath, const QString &currentText);
    static bool isGitRepo(const QString &filePath);
    static QString getGitBranch(const QString &filePath);

    // v0.1.62 — return every diff hunk for `absPath` against HEAD,
    // computed from the WORKING COPY ON DISK (NOT in-memory text).
    // The caller (GutterHunkPopup) wants the same view of the file
    // that `git diff` would show, because that is exactly what
    // `git apply --cached` will accept.
    //
    // We deliberately use the on-disk version rather than the dirty
    // editor buffer: staging a hunk that exists only in unsaved
    // memory would be a foot-gun (git can't see those bytes). The
    // editor saves on Ctrl+S; the gutter refreshes on save; the
    // popup only appears after the user clicks an already-visible
    // gutter marker. So "on-disk" is exactly what they're looking
    // at.
    //
    // Returns an empty vector if `absPath` is empty, not in a git
    // repo, or git itself reports no diff.
    static QVector<DiffHunk> hunksForFile(const QString &absPath);

    // v0.1.62 — locate the hunk whose post-change line range covers
    // `oneBasedLine`. Returns -1 if no hunk contains that line.
    // Used by the editor's margin-3 click handler to translate
    // "user clicked here" into "stage *this* hunk".
    static int hunkIndexForLine(const QVector<DiffHunk> &hunks, int oneBasedLine);
};

#endif
