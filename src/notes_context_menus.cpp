// SPDX-License-Identifier: GPL-3.0-or-later
//
// Right-click context menu factories for the Noter editor. See
// notes_context_menus.h for the public surface. Memory rule reminder:
// every icon comes from QStyle::standardIcon — emoji glyphs render as
// tofu on Linux without a color-emoji font and we got bitten by that
// in v0.1.67 / v0.1.68.

#include "notes_context_menus.h"

#include <QAction>
#include <QApplication>
#include <QClipboard>
#include <QKeySequence>
#include <QMenu>
#include <QStyle>
#include <QWidget>

namespace NoterContextMenus {

// Helper — add an action with text + optional shortcut hint and a
// std::function callback. Returns the action so callers can tweak it.
static QAction *addAction(QMenu *menu, const QIcon &icon,
                          const QString &text,
                          const QKeySequence &shortcut,
                          std::function<void()> cb) {
    QAction *a = menu->addAction(icon, text);
    if (!shortcut.isEmpty()) a->setShortcut(shortcut);
    // Even though the menu isn't a shortcut context, setting the
    // shortcut text makes Qt paint it on the right edge — which is
    // what users expect for discoverability.
    a->setShortcutVisibleInContextMenu(true);
    if (cb) {
        QObject::connect(a, &QAction::triggered, menu,
                         [cb]() { cb(); });
    }
    return a;
}

// ═══════════════════════════════════════════════════════════════════════
// 1. forPlainText
// ═══════════════════════════════════════════════════════════════════════
QMenu *forPlainText(QWidget *parent,
                    std::function<void(const QString &)> onPromote,
                    std::function<void(const QString &)> onInsertEmbed) {
    QMenu *m = new QMenu(parent);
    QStyle *st = m->style();

    // ── promote-to-block submenu ────────────────────────────────────
    QMenu *promote = m->addMenu(
        st->standardIcon(QStyle::SP_FileDialogDetailedView),
        QObject::tr("Promote to…"));

    auto addPromote = [&](const QString &label, const QString &type,
                          QStyle::StandardPixmap icon,
                          const QKeySequence &sc) {
        QAction *a = promote->addAction(st->standardIcon(icon), label);
        if (!sc.isEmpty()) a->setShortcut(sc);
        a->setShortcutVisibleInContextMenu(true);
        QObject::connect(a, &QAction::triggered, promote,
                         [onPromote, type]() {
                             if (onPromote) onPromote(type);
                         });
    };
    addPromote(QObject::tr("Decision"), "decision",
               QStyle::SP_DialogApplyButton,
               QKeySequence("Ctrl+Shift+D"));
    addPromote(QObject::tr("Action"),   "action",
               QStyle::SP_ArrowRight,
               QKeySequence("Ctrl+Shift+A"));
    addPromote(QObject::tr("Question"), "question",
               QStyle::SP_MessageBoxQuestion,
               QKeySequence("Ctrl+Shift+Q"));
    addPromote(QObject::tr("Risk"),     "risk",
               QStyle::SP_MessageBoxWarning,
               QKeySequence("Ctrl+Shift+R"));
    addPromote(QObject::tr("Share"),    "share",
               QStyle::SP_DialogYesButton,
               QKeySequence("Ctrl+Shift+S"));

    // ── insert-embed submenu ────────────────────────────────────────
    QMenu *embed = m->addMenu(
        st->standardIcon(QStyle::SP_FileLinkIcon),
        QObject::tr("Insert embed…"));

    auto addEmbed = [&](const QString &label, const QString &type,
                        QStyle::StandardPixmap icon) {
        QAction *a = embed->addAction(st->standardIcon(icon), label);
        QObject::connect(a, &QAction::triggered, embed,
                         [onInsertEmbed, type]() {
                             if (onInsertEmbed) onInsertEmbed(type);
                         });
    };
    addEmbed(QObject::tr("Code reference"), "coderef",
             QStyle::SP_FileIcon);
    addEmbed(QObject::tr("Image"),          "image",
             QStyle::SP_DesktopIcon);
    addEmbed(QObject::tr("Attachment"),     "attachment",
             QStyle::SP_DialogOpenButton);
    addEmbed(QObject::tr("Link"),           "link",
             QStyle::SP_ArrowForward);

    m->addSeparator();

    // ── clipboard ───────────────────────────────────────────────────
    addAction(m, st->standardIcon(QStyle::SP_DialogResetButton),
              QObject::tr("Cut"),  QKeySequence::Cut,
              [parent]() {
                  if (parent) QMetaObject::invokeMethod(parent, "cut");
              });
    addAction(m, st->standardIcon(QStyle::SP_FileDialogContentsView),
              QObject::tr("Copy"), QKeySequence::Copy,
              [parent]() {
                  if (parent) QMetaObject::invokeMethod(parent, "copy");
              });
    addAction(m, st->standardIcon(QStyle::SP_DialogOkButton),
              QObject::tr("Paste"), QKeySequence::Paste,
              [parent]() {
                  if (parent) QMetaObject::invokeMethod(parent, "paste");
              });

    m->addSeparator();

    addAction(m, st->standardIcon(QStyle::SP_FileDialogListView),
              QObject::tr("Toggle outline"),
              QKeySequence("Ctrl+Shift+O"),
              nullptr);  // wiring handled by integrator

    return m;
}

// ═══════════════════════════════════════════════════════════════════════
// 2. forTaggedBlock
// ═══════════════════════════════════════════════════════════════════════
QMenu *forTaggedBlock(QWidget *parent,
                      const QString &currentType,
                      const QString &currentOwner,
                      const QString &currentDue,
                      std::function<void(const QString &)> onChangeType,
                      std::function<void(const QString &)> onChangeOwner,
                      std::function<void()> onMarkDone,
                      std::function<void()> onSnooze1d,
                      std::function<void()> onSnooze1w,
                      std::function<void()> onDelete) {
    QMenu *m = new QMenu(parent);
    QStyle *st = m->style();

    // Disabled header showing current state — purely informational.
    QString hdr = QObject::tr("Tagged block · %1").arg(currentType);
    if (!currentOwner.isEmpty()) hdr += QStringLiteral(" · ") + currentOwner;
    if (!currentDue.isEmpty())   hdr += QStringLiteral(" · ") + currentDue;
    QAction *info = m->addAction(hdr);
    info->setEnabled(false);
    m->addSeparator();

    // ── change type ─────────────────────────────────────────────────
    QMenu *typeMenu = m->addMenu(
        st->standardIcon(QStyle::SP_FileDialogDetailedView),
        QObject::tr("Change type"));
    const QStringList types{
        QStringLiteral("decision"), QStringLiteral("action"),
        QStringLiteral("question"), QStringLiteral("risk"),
        QStringLiteral("share")
    };
    for (const QString &t : types) {
        QAction *a = typeMenu->addAction(t);
        a->setCheckable(true);
        a->setChecked(t == currentType);
        QObject::connect(a, &QAction::triggered, typeMenu,
                         [onChangeType, t]() {
                             if (onChangeType) onChangeType(t);
                         });
    }

    // ── change owner ────────────────────────────────────────────────
    QAction *ownerA = m->addAction(
        st->standardIcon(QStyle::SP_DirHomeIcon),
        QObject::tr("Change owner…"));
    QObject::connect(ownerA, &QAction::triggered, m,
                     [onChangeOwner]() {
                         if (onChangeOwner) onChangeOwner(QString());
                     });

    m->addSeparator();

    addAction(m, st->standardIcon(QStyle::SP_DialogApplyButton),
              QObject::tr("Mark done"),
              QKeySequence("Ctrl+Enter"),
              onMarkDone);
    addAction(m, st->standardIcon(QStyle::SP_BrowserReload),
              QObject::tr("Snooze 1 day"),
              QKeySequence(),
              onSnooze1d);
    addAction(m, st->standardIcon(QStyle::SP_BrowserReload),
              QObject::tr("Snooze 1 week"),
              QKeySequence(),
              onSnooze1w);

    m->addSeparator();

    addAction(m, st->standardIcon(QStyle::SP_TrashIcon),
              QObject::tr("Delete block"),
              QKeySequence::Delete,
              onDelete);

    return m;
}

// ═══════════════════════════════════════════════════════════════════════
// 3. forSelection
// ═══════════════════════════════════════════════════════════════════════
QMenu *forSelection(QWidget *parent,
                    const QString &selectedText,
                    std::function<void(const QString &)> onAi) {
    QMenu *m = new QMenu(parent);
    QStyle *st = m->style();

    // Header — italic preview of the selection, truncated to 40 chars.
    QString preview = selectedText.simplified();
    if (preview.size() > 40) preview = preview.left(40) + QStringLiteral("…");
    QAction *hdr = m->addAction(QObject::tr("Selection: \"%1\"")
                                .arg(preview));
    hdr->setEnabled(false);
    m->addSeparator();

    addAction(m, st->standardIcon(QStyle::SP_FileDialogContentsView),
              QObject::tr("Copy"), QKeySequence::Copy,
              [selectedText]() {
                  QApplication::clipboard()->setText(selectedText);
              });

    m->addSeparator();

    QMenu *ai = m->addMenu(
        st->standardIcon(QStyle::SP_ComputerIcon),
        QObject::tr("AI…"));
    QAction *rew = ai->addAction(QObject::tr("Rewrite"));
    rew->setShortcut(QKeySequence("Ctrl+Shift+E"));
    rew->setShortcutVisibleInContextMenu(true);
    QObject::connect(rew, &QAction::triggered, ai,
                     [onAi]() { if (onAi) onAi(QStringLiteral("rewrite")); });

    QAction *ext = ai->addAction(QObject::tr("Extract todo"));
    ext->setShortcut(QKeySequence("Ctrl+Shift+T"));
    ext->setShortcutVisibleInContextMenu(true);
    QObject::connect(ext, &QAction::triggered, ai,
                     [onAi]() { if (onAi) onAi(QStringLiteral("extract-todo")); });

    return m;
}

// ═══════════════════════════════════════════════════════════════════════
// 4. forAttendee
// ═══════════════════════════════════════════════════════════════════════
QMenu *forAttendee(QWidget *parent, const QString &name) {
    QMenu *m = new QMenu(parent);
    QStyle *st = m->style();

    QAction *hdr = m->addAction(
        st->standardIcon(QStyle::SP_DirHomeIcon),
        QObject::tr("@%1").arg(name));
    hdr->setEnabled(false);
    m->addSeparator();

    addAction(m, st->standardIcon(QStyle::SP_FileDialogListView),
              QObject::tr("Filter notes by %1").arg(name),
              QKeySequence(),
              nullptr);
    addAction(m, st->standardIcon(QStyle::SP_ArrowForward),
              QObject::tr("Show open todos for %1").arg(name),
              QKeySequence(),
              nullptr);
    addAction(m, st->standardIcon(QStyle::SP_FileDialogContentsView),
              QObject::tr("Copy as @%1").arg(name),
              QKeySequence::Copy,
              [name]() {
                  QApplication::clipboard()->setText(
                      QStringLiteral("@") + name);
              });

    m->addSeparator();
    addAction(m, st->standardIcon(QStyle::SP_TrashIcon),
              QObject::tr("Remove mention"),
              QKeySequence::Delete,
              nullptr);

    return m;
}

// ═══════════════════════════════════════════════════════════════════════
// 5. forCodeRef
// ═══════════════════════════════════════════════════════════════════════
QMenu *forCodeRef(QWidget *parent, const QString &filePath, int line) {
    QMenu *m = new QMenu(parent);
    QStyle *st = m->style();

    QString hdrText = filePath;
    if (line > 0) hdrText += QStringLiteral(":") + QString::number(line);
    QAction *hdr = m->addAction(
        st->standardIcon(QStyle::SP_FileIcon), hdrText);
    hdr->setEnabled(false);
    m->addSeparator();

    addAction(m, st->standardIcon(QStyle::SP_DialogOpenButton),
              QObject::tr("Open in editor"),
              QKeySequence("Enter"),
              nullptr);  // wiring handled by integrator
    addAction(m, st->standardIcon(QStyle::SP_BrowserReload),
              QObject::tr("Refresh snippet"),
              QKeySequence("F5"),
              nullptr);
    addAction(m, st->standardIcon(QStyle::SP_FileDialogContentsView),
              QObject::tr("Copy file path"),
              QKeySequence::Copy,
              [filePath]() {
                  QApplication::clipboard()->setText(filePath);
              });
    addAction(m, st->standardIcon(QStyle::SP_DirIcon),
              QObject::tr("Reveal in file manager"),
              QKeySequence(),
              nullptr);

    m->addSeparator();
    addAction(m, st->standardIcon(QStyle::SP_TrashIcon),
              QObject::tr("Remove card"),
              QKeySequence::Delete,
              nullptr);

    return m;
}

// ═══════════════════════════════════════════════════════════════════════
// 6. forNoteListEntry
// ═══════════════════════════════════════════════════════════════════════
QMenu *forNoteListEntry(QWidget *parent,
                        const QString &absolutePath,
                        std::function<void()> onOpen,
                        std::function<void()> onRename,
                        std::function<void()> onDuplicate,
                        std::function<void()> onMove,
                        std::function<void(const QString &)> onExport,
                        std::function<void()> onRevealInFM,
                        std::function<void()> onDelete) {
    QMenu *m = new QMenu(parent);
    QStyle *st = m->style();

    QAction *hdr = m->addAction(
        st->standardIcon(QStyle::SP_FileIcon), absolutePath);
    hdr->setEnabled(false);
    m->addSeparator();

    addAction(m, st->standardIcon(QStyle::SP_DialogOpenButton),
              QObject::tr("Open"),
              QKeySequence("Enter"),
              onOpen);
    addAction(m, st->standardIcon(QStyle::SP_FileDialogDetailedView),
              QObject::tr("Rename…"),
              QKeySequence("F2"),
              onRename);
    addAction(m, st->standardIcon(QStyle::SP_FileDialogNewFolder),
              QObject::tr("Duplicate"),
              QKeySequence("Ctrl+D"),
              onDuplicate);
    addAction(m, st->standardIcon(QStyle::SP_ArrowForward),
              QObject::tr("Move to notebook…"),
              QKeySequence(),
              onMove);

    m->addSeparator();

    QMenu *exp = m->addMenu(
        st->standardIcon(QStyle::SP_DriveFDIcon),
        QObject::tr("Export as…"));
    auto addExp = [&](const QString &label, const QString &fmt) {
        QAction *a = exp->addAction(label);
        QObject::connect(a, &QAction::triggered, exp,
                         [onExport, fmt]() {
                             if (onExport) onExport(fmt);
                         });
    };
    addExp(QObject::tr("PDF"),                  "pdf");
    addExp(QObject::tr("Markdown"),             "md");
    addExp(QObject::tr("Rich text (RTF / HTML)"), "rich");

    addAction(m, st->standardIcon(QStyle::SP_DirIcon),
              QObject::tr("Reveal in file manager"),
              QKeySequence(),
              onRevealInFM);

    m->addSeparator();
    addAction(m, st->standardIcon(QStyle::SP_TrashIcon),
              QObject::tr("Delete note"),
              QKeySequence::Delete,
              onDelete);

    return m;
}

} // namespace NoterContextMenus
