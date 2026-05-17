// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef NOTEPATRA_GIT_HUNK_APPLY_H
#define NOTEPATRA_GIT_HUNK_APPLY_H

// ═══════════════════════════════════════════════════════════════════════
// Per-hunk stage / revert via `git apply --cached`.
//
// Given a parsed DiffHunk (from GitGutter::hunksForFile), synthesizes an
// in-memory unified-diff patch and pipes it to `git apply --cached
// --whitespace=nowarn` via QProcess stdin. On success the index is
// updated for the hunk only — the working copy is untouched. The caller
// is expected to refresh the gutter + the Git panel afterwards.
//
// SECURITY MODEL — same as src/git_tools.cpp:
//   1. Workspace anchor: the file path is resolved through
//      AiTools::resolveSafePath against the repo root. Paths outside
//      the workspace are rejected ("outside_workspace").
//   2. Deny-list: ~/.ssh/, *.pem, *.key, ~/.aws/, /etc/passwd, …
//      (see AiTools::isHardDenied). Even if a symlinked path passes
//      the workspace check, we refuse to stage hunks for credential
//      files. ("denied")
//   3. Size cap: 5000 lines per patch body. ("too_large")
//   4. Line-ending normalization: CRLF → LF before synthesizing.
//      We also pass `-c core.autocrlf=false` to the git subprocess
//      so it doesn't re-massage on its way to the index.
//   5. 5-second timeout on the `git apply` subprocess. ("timeout")
//
// We expose a thin POD result rather than a JSON wire blob — this is
// not part of the AI tool-call surface and not part of the agentic
// loop. The popup widget consumes the result directly.
// ═══════════════════════════════════════════════════════════════════════

#include "gitgutter.h"

#include <QString>
#include <QStringList>
#include <QVector>

namespace GitHunkApply {

// Mode for what to do with a hunk. STAGE applies the patch to the index
// (i.e. `git apply --cached`); REVERT applies a REVERSED patch to the
// working copy itself (`git apply --reverse`). For the v0.1.62 popup we
// only wire STAGE end-to-end; REVERT is supported by the API so the
// popup widget can offer the button.
enum class Mode {
    Stage,    // index-only ; works tree untouched
    Revert,   // working-tree only ; index untouched
};

struct Result {
    bool ok = false;
    QString errorKind;  // "", "no_workspace", "outside_workspace",
                        // "denied", "not_a_repo", "too_large",
                        // "timeout", "git_error", "io_error".
    QString message;    // human-readable detail (often git's stderr).
    QString patch;      // the literal patch we sent to git (for debugging
                        // and for the popup's "Copy Diff" button).
};

// Synthesize a unified-diff patch for one or more hunks targeting
// `absFilePath`. The path is normalised to a workspace-relative form
// in the `a/...` and `b/...` diff headers. CRLF in any line is
// converted to LF before assembly so `git apply` doesn't reject on
// EOL drift. Returns the assembled patch (no trailing-newline guarantees
// beyond what git itself wants).
//
// If the combined body exceeds 5000 lines, returns "" and sets
// outErrorKind=too_large.
QString synthesizePatch(const QString &absFilePath,
                        const QString &repoRoot,
                        const QVector<DiffHunk> &hunks,
                        QString *outErrorKind = nullptr);

// One-hunk convenience wrapper — the most common call site.
QString synthesizePatch(const QString &absFilePath,
                        const QString &repoRoot,
                        const DiffHunk &hunk,
                        QString *outErrorKind = nullptr);

// Stage or revert a single hunk. Wraps synthesizePatch + the QProcess
// invocation. `repoRoot` should be the canonical absolute path of the
// repository root (NOT the file's directory) — same workspace anchor
// the AI tools use.
Result applyHunk(const QString &absFilePath,
                 const QString &repoRoot,
                 const DiffHunk &hunk,
                 Mode mode);

// Discover the repo root for an absolute file path, by spawning
// `git rev-parse --show-toplevel` in the file's directory. Returns
// the canonical absolute path on success, empty string on failure.
// Convenience for callers that have a file path but no separate
// notion of "the workspace root".
QString repoRootForFile(const QString &absFilePath);

} // namespace GitHunkApply

#endif // NOTEPATRA_GIT_HUNK_APPLY_H
