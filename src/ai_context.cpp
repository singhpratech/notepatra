#include "ai_context.h"

#include <QStringList>

namespace AiContext {

QString buildWorkspaceContextBlockWithTree(const QString &currentFilePath,
                                           const QString &currentFileText,
                                           const QVector<OpenTabInfo> &openTabs,
                                           const QString &workspaceRoot,
                                           const QStringList &workspaceFilePaths) {
    // Cap the tree listing so huge monorepos don't blow the context. We
    // pick a generous cap — path text is cheap (~30 bytes per line). The
    // first N files alphabetically keep the output deterministic; caller
    // can prioritise which files appear first before passing them in.
    constexpr int kMaxTreeEntries = 400;
    constexpr int kMaxTreeBytes   = 6000;

    QString base = buildWorkspaceContextBlock(
        currentFilePath, currentFileText, openTabs, workspaceRoot);

    if (workspaceFilePaths.isEmpty()) return base;

    QString tree = QStringLiteral("\n## All files in workspace");
    if (!workspaceRoot.isEmpty()) tree += QStringLiteral(" (rooted at ") + workspaceRoot + QStringLiteral(")");
    tree += QStringLiteral("\n");

    int count = 0;
    for (const QString &p : workspaceFilePaths) {
        if (count >= kMaxTreeEntries || tree.size() >= kMaxTreeBytes) {
            tree += QStringLiteral("  … (") +
                    QString::number(workspaceFilePaths.size() - count) +
                    QStringLiteral(" more files omitted)\n");
            break;
        }
        tree += QStringLiteral("  ") + p + QStringLiteral("\n");
        ++count;
    }

    // If the base block was empty (no current file / open tabs / root)
    // we still want the header so the AI knows this is workspace data.
    if (base.isEmpty()) {
        base = QStringLiteral("# Workspace context (for reference — do not echo back)\n");
        if (!workspaceRoot.isEmpty())
            base += QStringLiteral("Workspace root: ") + workspaceRoot + QStringLiteral("\n");
    }
    return base + tree;
}

QString buildWorkspaceContextBlock(const QString &currentFilePath,
                                   const QString &currentFileText,
                                   const QVector<OpenTabInfo> &openTabs,
                                   const QString &workspaceRoot) {
    constexpr int kCurrentFileCap    = 12000;
    constexpr int kPerOtherTabCap    = 2500;
    constexpr int kOtherTabsTotalCap = 10000;

    const bool hasCurrent = !currentFilePath.isEmpty() || !currentFileText.isEmpty();
    if (!hasCurrent && openTabs.isEmpty() && workspaceRoot.isEmpty()) return {};

    auto truncate = [](const QString &s, int cap) -> QString {
        if (s.size() <= cap) return s;
        int cut = s.lastIndexOf('\n', cap);
        if (cut < cap - 400) cut = cap;
        return s.left(cut) + QStringLiteral("\n… [truncated — file continues]");
    };

    QString out;
    out.reserve(24000);
    out += QStringLiteral("# Workspace context (for reference — do not echo back)\n");
    if (!workspaceRoot.isEmpty())
        out += QStringLiteral("Workspace root: ") + workspaceRoot + QLatin1Char('\n');

    if (!openTabs.isEmpty()) {
        out += QStringLiteral("Open editor tabs (") + QString::number(openTabs.size())
             + QStringLiteral("):\n");
        for (const auto &t : openTabs) {
            const QString label = t.filePath.isEmpty() ? t.displayName : t.filePath;
            out += QStringLiteral("  - %1%2%3\n")
                       .arg(label)
                       .arg(t.language.isEmpty() ? QString()
                                                 : QStringLiteral(" [") + t.language + QStringLiteral("]"))
                       .arg(t.isCurrent ? QStringLiteral("  ← current") : QString());
        }
    }

    if (hasCurrent) {
        out += QStringLiteral("\n## Current file");
        if (!currentFilePath.isEmpty()) out += QStringLiteral(": ") + currentFilePath;
        out += QStringLiteral("\n```\n");
        out += truncate(currentFileText, kCurrentFileCap);
        out += QStringLiteral("\n```\n");
    }

    int spent = 0;
    bool headerEmitted = false;
    for (const auto &t : openTabs) {
        if (t.isCurrent) continue;
        if (t.text.isEmpty()) continue;
        if (spent >= kOtherTabsTotalCap) {
            out += QStringLiteral("… [additional open files omitted to stay within context]\n");
            break;
        }
        const int remaining = kOtherTabsTotalCap - spent;
        const int cap = qMin(kPerOtherTabCap, remaining);
        const QString excerpt = truncate(t.text, cap);
        if (!headerEmitted) {
            out += QStringLiteral("\n## Other open files (excerpts)\n");
            headerEmitted = true;
        }
        const QString label = t.filePath.isEmpty() ? t.displayName : t.filePath;
        out += QStringLiteral("\n### ") + label;
        if (!t.language.isEmpty()) out += QStringLiteral("  [") + t.language + QLatin1Char(']');
        out += QStringLiteral("\n```\n") + excerpt + QStringLiteral("\n```\n");
        spent += excerpt.size();
    }

    return out;
}

} // namespace AiContext
