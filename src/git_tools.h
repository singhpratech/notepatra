#ifndef NOTEPATRA_GIT_TOOLS_H
#define NOTEPATRA_GIT_TOOLS_H

// ═══════════════════════════════════════════════════════════════════════
// Agentic git tools — read-only slice.
//
// Exposes five git operations to the AI Coding mode tool-call agent loop:
//   - git_status      : branch + ahead/behind + staged/modified/untracked
//   - git_diff        : working-tree or staged diff (capped at 32 KB)
//   - git_log         : recent commits as structured array (max 100)
//   - git_branch_list : current branch + all locals + remote tracking
//   - git_show        : full commit metadata + diff (capped at 32 KB)
//
// SECURITY MODEL:
//   1. WRITE-DISABLED. The QProcess argv list is constructed in this
//      module and the verb is one of {status, diff, log, rev-parse,
//      branch, show}. There is no path through this code to add /
//      commit / push / fetch / pull / merge / reset / rebase / checkout.
//   2. Workspace anchor — every call sets QProcess::setWorkingDirectory
//      to the canonical workspace root. If workspaceRoot is empty we
//      refuse with error_kind:no_workspace.
//   3. If the workspace is not a git checkout, `git rev-parse --git-dir`
//      fails and we return error_kind:not_a_repo.
//   4. The optional `path` argument is run through AiTools::resolveSafePath
//      so paths like ../../etc/passwd are rejected.
//   5. 5-second timeout per call. If git exceeds it we kill the process
//      and return error_kind:timeout.
//
// Wire format mirrors AiTools — JSON body { ok:bool, result:{...} } on
// success, { ok:false, error_kind:..., message:... } on failure.
// ═══════════════════════════════════════════════════════════════════════

#include "ai_tools.h"

namespace GitTools {

// Each helper executes one git verb against the workspace, packaged as
// an AiTools::ToolResult so the agent loop can splice the result into
// the conversation as a role:tool message.
//
// `workspaceRoot` should be the canonicalized absolute path of the
// directory the user has open in Notepatra. Every helper validates it
// before invoking git.
AiTools::ToolResult executeGitStatus(const AiTools::ToolCall &call,
                                     const QString &workspaceRoot);

AiTools::ToolResult executeGitDiff(const AiTools::ToolCall &call,
                                   const QString &workspaceRoot);

AiTools::ToolResult executeGitLog(const AiTools::ToolCall &call,
                                  const QString &workspaceRoot);

AiTools::ToolResult executeGitBranchList(const AiTools::ToolCall &call,
                                         const QString &workspaceRoot);

AiTools::ToolResult executeGitShow(const AiTools::ToolCall &call,
                                   const QString &workspaceRoot);

// ── Parsing helpers (exposed for unit testing) ────────────────────────

// Parse `git status --porcelain=v2 --branch -z` output into a structured
// JSON shape: { branch, upstream, ahead, behind, staged:[], modified:[],
// untracked:[], conflicts:[] }. Mirrors the parser in gitpanel.cpp but
// emits JSON rather than UI rows.
QJsonObject parsePorcelainV2(const QByteArray &out);

// Parse `git log --pretty=format:%H%x1f%an%x1f%aI%x1f%s%x1e` output
// (1f = unit-separator field delimiter, 1e = record-separator). Returns
// an array of { hash, author, date, subject } objects.
QJsonArray parseLogRecords(const QByteArray &out);

} // namespace GitTools

#endif // NOTEPATRA_GIT_TOOLS_H
