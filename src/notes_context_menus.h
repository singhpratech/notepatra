// SPDX-License-Identifier: GPL-3.0-or-later
//
// NoterContextMenus — factory namespace producing QMenu* instances
// pre-wired for the 5 (plus 1 sidebar) right-click contexts inside
// the Noter editor:
//
//   1. forPlainText     — paragraph / empty-line right-click
//   2. forTaggedBlock   — right-click on an existing decision / action /
//                         question / risk / share block
//   3. forSelection     — right-click while text is selected
//   4. forAttendee      — right-click on an @mention chip
//   5. forCodeRef       — right-click on a code-reference card
//   6. forNoteListEntry — right-click on a note row in the sidebar
//
// Each factory returns a heap-allocated QMenu* parented to `parent`.
// Callers are responsible for `popup()` + lifetime — typical idiom:
//     QMenu *m = NoterContextMenus::forPlainText(this, onPromote, onEmbed);
//     m->setAttribute(Qt::WA_DeleteOnClose);
//     m->popup(QCursor::pos());

#ifndef NOTES_CONTEXT_MENUS_H
#define NOTES_CONTEXT_MENUS_H

#include <QString>
#include <functional>

class QMenu;
class QWidget;

namespace NoterContextMenus {

// ─── 1. plain text / empty paragraph ────────────────────────────────
// Items: promote-to-block (5 types), insert-embed (4 types),
//        cut / copy / paste, toggle outline.
QMenu *forPlainText(QWidget *parent,
                    std::function<void(const QString &blockType)> onPromote,
                    std::function<void(const QString &embedType)> onInsertEmbed);

// ─── 2. tagged block ────────────────────────────────────────────────
// `currentType` is one of decision / action / question / risk / share.
// `currentOwner` / `currentDue` are display-only — the menu just hints
// the active values back to the user.
QMenu *forTaggedBlock(QWidget *parent,
                      const QString &currentType,
                      const QString &currentOwner,
                      const QString &currentDue,
                      std::function<void(const QString &newType)> onChangeType,
                      std::function<void(const QString &newOwner)> onChangeOwner,
                      std::function<void()> onMarkDone,
                      std::function<void()> onSnooze1d,
                      std::function<void()> onSnooze1w,
                      std::function<void()> onDelete);

// ─── 3. selection ───────────────────────────────────────────────────
// `selectedText` is shown in italics in the header for context. `onAi`
// is invoked with a mode string — "rewrite" or "extract-todo" today,
// more once the AI integrator adds them.
QMenu *forSelection(QWidget *parent,
                    const QString &selectedText,
                    std::function<void(const QString &mode)> onAi);

// ─── 4. attendee mention ────────────────────────────────────────────
QMenu *forAttendee(QWidget *parent, const QString &name);

// ─── 5. code reference card ─────────────────────────────────────────
QMenu *forCodeRef(QWidget *parent, const QString &filePath, int line);

// ─── 6. note list entry (sidebar) ───────────────────────────────────
// `onExport` is called with "pdf" | "md" | "rich". The factory builds
// the export submenu and dispatches to one callback so the caller has
// a single place to handle every format.
QMenu *forNoteListEntry(QWidget *parent,
                        const QString &absolutePath,
                        std::function<void()> onOpen,
                        std::function<void()> onRename,
                        std::function<void()> onDuplicate,
                        std::function<void()> onMove,
                        std::function<void(const QString &format)> onExport,
                        std::function<void()> onRevealInFM,
                        std::function<void()> onDelete);

} // namespace NoterContextMenus

#endif // NOTES_CONTEXT_MENUS_H
