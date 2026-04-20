#ifndef AI_CONTEXT_H
#define AI_CONTEXT_H

#include <QString>
#include <QVector>

namespace AiContext {

struct OpenTabInfo {
    QString filePath;
    QString displayName;
    QString language;
    QString text;
    bool isCurrent = false;
};

// Assemble the workspace-awareness block prepended to the user prompt
// before it reaches the local model. Pure function; safe to call from tests.
// Budgeted so small local models (3B, 4K–8K context) don't overflow.
QString buildWorkspaceContextBlock(
    const QString &currentFilePath,
    const QString &currentFileText,
    const QVector<OpenTabInfo> &openTabs,
    const QString &workspaceRoot);

// Extended variant that also accepts a flat list of every file path in
// the workspace (relative to workspaceRoot). Gives the AI a Cursor-like
// "@codebase" awareness: it can recommend files it hasn't seen opened yet.
// Only paths are included, not contents — keeps the token cost tiny even
// for large projects. Caller should pre-filter binaries and .git etc.
QString buildWorkspaceContextBlockWithTree(
    const QString &currentFilePath,
    const QString &currentFileText,
    const QVector<OpenTabInfo> &openTabs,
    const QString &workspaceRoot,
    const QStringList &workspaceFilePaths);

} // namespace AiContext

#endif
