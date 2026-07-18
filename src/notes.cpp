// SPDX-License-Identifier: GPL-3.0-or-later
//
// notes.cpp — Noter, v0.1.95 redesign.
//
// Two-pane (sidebar + editor) layout inspired by Apple Notes / Bear /
// Granola. Sidebar lists past meetings grouped by recency, a single
// "+ New meeting" button, and a bottom "All Todos" toggle. Editor is a
// vanilla QTextEdit with ☐ checkboxes that toggle on click. One ✨
// Extract button bottom-right runs the AI sweep over the body. No
// slash menu, no insert bar, no header button row.
//
// Storage: HTML files in <Documents>/Notepatra/Noter/Inbox/, with a
// SQLite todos cache rebuilt from the .html on every save.

#include "notes.h"
#include "notes_storage.h"
#include "notes_template.h"
#include "notes_todos.h"
#include "notes_reminder.h"
#include <QSystemTrayIcon>
#include "notes_popout.h"
#include "notes_sweep_dialog.h"
#include "notes_sweep_prompt.h"
#include "notes_extract_apply.h"
#include "notes_export.h"
#include "ollama.h"
#include "config.h"

#include <QAction>
#include <QApplication>
#include <QCalendarWidget>
#include <QDateTimeEdit>
#include <QDialog>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QStatusBar>
#include <QComboBox>
#include <QCompleter>
#include <QCryptographicHash>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QPair>
#include <QFrame>
#include <QHBoxLayout>
#include <QInputDialog>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QListWidgetItem>
#include <QMainWindow>
#include <QMap>
#include <QContextMenuEvent>
#include <QMenu>
#include <QTreeWidget>
#include <QTreeWidgetItem>
#include <QMessageBox>
#include <QMouseEvent>
#include <QPalette>
#include <QShowEvent>
#include <QRegExp>
#include <QRegularExpression>
#include <QUuid>
#include <QPushButton>
#include <QShortcut>
#include <QToolButton>
#include <QSplitter>
#include <QStackedWidget>
#include <QStandardPaths>
#include <QIcon>
#include <QPainter>
#include <QPixmap>
#include <QPolygonF>
#include <QStyle>
#include <QStyledItemDelegate>
#include <QStyleOptionViewItem>
#include <QScopedPointer>
#include <QTextBlock>
#include <QTextCharFormat>
#include <QTextCursor>
#include <QTextDocument>
#include <QTextEdit>
#include <QTimer>
#include <QVBoxLayout>

#include <algorithm>

namespace {

// ─── styling helpers ────────────────────────────────────────────────
// A5 — every colour comes from the NoterPalette (see notes_theme.h).
// The Light palette reproduces the pre-A5 hardcoded values verbatim, so
// the default rendering is unchanged; Dark/Monokai swap in sympathetic
// values via NotesPanel::applyNoterTheme().

// Shared QMenu rules — the panel-level QSS variant (also embedded in
// sidebarStyle below) per feedback_qmenu_cascade_through_widget_qss.
QString menuQss(const NoterPalette &pal) {
    return QStringLiteral(
        "QMenu { background: %1; color: %2; border: 1px solid %3;"
        "  padding: 4px 0; }"
        "QMenu::item { padding: 6px 22px 6px 18px; }"
        "QMenu::item:selected { background: %4; color: %5; }"
        "QMenu::separator { height: 1px; background: %6; margin: 4px 8px; }"
    ).arg(pal.menuBg, pal.menuFg, pal.menuBorder,
          pal.menuSelBg, pal.menuSelFg, pal.menuSep);
}

QString sidebarStyle(const NoterPalette &pal) {
    // (the QPushButton#noterTodosBtn rules died with the v0.1.98 Todos
    // button — placeholders renumbered when they were removed.)
    return (QStringLiteral(
        "QWidget#noterSidebar { background: %1; }"
        "QLineEdit#noterSearch { border: 1px solid %4; border-radius: 6px;"
        "  padding: 5px 9px; font-size: 12px; background: %5; color: %6; }"
        "QPushButton#noterNewBtn { background: %2; color: white; border: none;"
        "  border-radius: 6px; padding: 7px 10px; font-size: 13px; font-weight: 500; }"
        "QPushButton#noterNewBtn:hover { background: %7; }"
        // The sidebar is a QTreeWidget#noterSidebarTree (the old
        // QListWidget#noterMeetingList shape is gone). An item-view paints
        // its viewport from its OWN palette Base role — the parent
        // QWidget#noterSidebar background does NOT reach it — so without an
        // explicit background here the tree fell through to the system Base
        // and rendered dark-on-macOS while the rest of the panel stayed
        // light. palette Base is also pinned in code (buildSidebar) as a
        // belt-and-braces fallback for styles that bypass this selector.
        "QTreeWidget#noterSidebarTree, QTreeView#noterSidebarTree { background: %1;"
        "  border: none; font-size: 13px; outline: none; color: %6; }"
        "QTreeWidget#noterSidebarTree::item { padding: 5px 6px; color: %6; }"
        "QTreeWidget#noterSidebarTree::item:hover { background: %8; }"
        "QTreeWidget#noterSidebarTree::item:selected { background: %3; color: %9; }")
        // v0.1.95+ — QMenu must be styled inside the panel's QSS so the
        // right-click context menus aren't dark-on-dark (per the memory
        // rule feedback_qmenu_cascade_through_widget_qss).
        + menuQss(pal)
    ).arg(pal.sidebarBg, pal.accent, pal.leafHighlight, pal.inputBorder,
          pal.inputBg, pal.text, pal.accentHover, pal.hoverBg,
          pal.leafHighlightFg);
}

// Slim left-edge strip shown while the sidebar is hidden (Ctrl+Alt+B) —
// the only mouse affordance back. QStyle chevron icon, never an emoji.
QString sidebarRestoreStyle(const NoterPalette &pal) {
    return QStringLiteral(
        "QToolButton#noterSidebarRestore { background: %1; border: none;"
        "  border-right: 1px solid %2; }"
        "QToolButton#noterSidebarRestore:hover { background: %3; }")
        .arg(pal.sidebarBg, pal.sidebarBorder, pal.hoverBg);
}

// Modal-parity QMessageBox — applies the active NoterPalette (via
// applyNoterDialogTheme) before exec so Noter confirms/notices follow
// Light/Dark/Monokai like every other Noter surface.
QMessageBox::StandardButton noterMessageBox(
        QWidget *parent, const NoterPalette &pal, QMessageBox::Icon icon,
        const QString &title, const QString &text,
        QMessageBox::StandardButtons buttons = QMessageBox::Ok,
        QMessageBox::StandardButton def = QMessageBox::NoButton) {
    QMessageBox box(icon, title, text, buttons, parent);
    if (def != QMessageBox::NoButton) box.setDefaultButton(def);
    applyNoterDialogTheme(&box, pal);
    return static_cast<QMessageBox::StandardButton>(box.exec());
}

QString editorPageStyle(const NoterPalette &pal) {
    return QStringLiteral(
        "QWidget#noterEditorPage { background: %1; }"
        "QWidget#noterEditorFooter { background: %1; border-top: 1px solid %3; }"
        "QTextEdit#noterEditor { background: %1; border: none;"
        "  padding: 32px 56px; font-family: 'IBM Plex Sans','Segoe UI',sans-serif;"
        "  font-size: 15px; color: %4; }"
        "QPushButton#noterExtractBtn { background: %2; color: white; border: none;"
        "  border-radius: 18px; padding: 7px 18px; font-size: 13px; font-weight: 500; }"
        "QPushButton#noterExtractBtn:hover { background: %5; }"
        "QLabel#noterSavedHint { color: %6; font-size: 11px;"
        "  padding-right: 8px; }"
        "QLabel#noterModelLabel { color: %6; font-size: 11px; }"
        "QComboBox#noterModelCombo { border: 1px solid %7; border-radius: 4px;"
        "  padding: 3px 8px; font-size: 12px; min-width: 160px; background: %8; color: %9; }"
    ).arg(pal.pageBg, pal.accent, pal.sidebarBorder, pal.strongText,
          pal.accentHover, pal.hintFg, pal.inputBorder, pal.inputBg, pal.text);
}

QString emptyPageStyle(const NoterPalette &pal) {
    return QStringLiteral(
        "QWidget#noterEmptyPage { background: %1; }"
        "QLabel#noterEmptyTitle { color: %2; font-size: 28px; font-weight: 300; }"
        "QLabel#noterEmptySub { color: %3; font-size: 14px; }"
        "QLabel#noterEmptyHint { color: %4; font-size: 13px; padding: 12px 16px;"
        "  border: 1px dashed %5; border-radius: 8px; background: %6; }"
        "QLabel#noterEmptyNegs { color: %7; font-size: 12px; line-height: 1.7; }"
    ).arg(pal.pageBg, pal.emptyTitleFg, pal.hintFg, pal.text,
          pal.inputBorder, pal.inputBg, pal.mutedText);
}

// ─── icon helpers (anti-tofu) ───────────────────────────────────────
// v0.1.97 — painter-drawn checkbox for todo leaves. An OPEN todo gets an
// empty square (matches the editor's ☐); a DONE todo gets a green-checked
// square (matches ✓). Before this, every todo leaf used the same green
// SP_DialogApplyButton tick, so OPEN todos looked done in the sidebar even
// while the checklist showed them unchecked. Painter-drawn (not an emoji
// glyph) per feedback_qt_icons_no_emoji so it renders identically on
// Linux / macOS / Windows. Cached per state.
QIcon checkboxIcon(bool done) {
    static QIcon openIcon, doneIcon;
    QIcon &cached = done ? doneIcon : openIcon;
    if (!cached.isNull()) return cached;
    const int S = 32;
    QPixmap pm(S, S);
    pm.fill(Qt::transparent);
    QPainter p(&pm);
    p.setRenderHint(QPainter::Antialiasing, true);
    const QRectF box(6, 6, 20, 20);
    if (done) {
        p.setPen(QPen(QColor("#16A34A"), 2.4));
        p.setBrush(QColor("#DCFCE7"));
        p.drawRoundedRect(box, 4, 4);
        // Green checkmark drawn as two round-capped strokes (no QPainterPath
        // include needed — matches the pencilIcon drawing style).
        p.setBrush(Qt::NoBrush);
        p.setPen(QPen(QColor("#15803D"), 3.0, Qt::SolidLine,
                      Qt::RoundCap, Qt::RoundJoin));
        p.drawLine(QPointF(11, 16), QPointF(15, 20.5));
        p.drawLine(QPointF(15, 20.5), QPointF(22, 11.5));
    } else {
        p.setPen(QPen(QColor("#6B7280"), 2.4));
        p.setBrush(Qt::NoBrush);
        p.drawRoundedRect(box, 4, 4);
    }
    p.end();
    cached = QIcon(pm);
    return cached;
}

// v0.1.98 — painter-drawn clock for the Reminders root + reminder leaves.
// Amber dial with two hands. Painter-drawn (not an emoji ⏰) per
// feedback_qt_icons_no_emoji so it renders identically across Linux /
// macOS / Windows regardless of installed emoji fonts.
QIcon reminderClockIcon() {
    static QIcon cached;
    if (!cached.isNull()) return cached;
    const int S = 32;
    QPixmap pm(S, S);
    pm.fill(Qt::transparent);
    QPainter p(&pm);
    p.setRenderHint(QPainter::Antialiasing, true);
    const QRectF face(6, 6, 20, 20);
    p.setPen(QPen(QColor("#D97706"), 2.4));
    p.setBrush(QColor("#FEF3C7"));
    p.drawEllipse(face);
    p.setPen(QPen(QColor("#B45309"), 2.2, Qt::SolidLine, Qt::RoundCap));
    const QPointF c = face.center();
    p.drawLine(c, QPointF(c.x() + 5.0, c.y() - 2.5));   // hour hand
    p.drawLine(c, QPointF(c.x(),        c.y() - 7.0));   // minute hand
    p.end();
    cached = QIcon(pm);
    return cached;
}

// v0.1.97 — emoji glyphs like 📝 / ✓ / 🗑 render as tofu on Linux
// systems without a color emoji font (per the user's memory rule
// feedback_qt_icons_no_emoji). Every Noter tree item uses these
// QStyle::SP_* icons instead so the sidebar renders identically
// across Linux / macOS / Windows regardless of installed fonts.
QIcon iconForRoot(QStyle *st, const QString &kind) {
    if (!st) return {};
    if (kind == QStringLiteral("meetings"))  return st->standardIcon(QStyle::SP_DirOpenIcon);
    if (kind == QStringLiteral("todos"))     return st->standardIcon(QStyle::SP_DialogApplyButton);
    if (kind == QStringLiteral("reminders")) return reminderClockIcon();
    if (kind == QStringLiteral("trash"))     return st->standardIcon(QStyle::SP_TrashIcon);
    return {};
}

QIcon iconForLeaf(QStyle *st, const QString &kind) {
    if (!st) return {};
    if (kind == QStringLiteral("meeting"))         return st->standardIcon(QStyle::SP_FileIcon);
    if (kind == QStringLiteral("trashed_meeting")) return st->standardIcon(QStyle::SP_FileIcon);
    if (kind == QStringLiteral("todo"))            return checkboxIcon(false);
    if (kind == QStringLiteral("reminder"))        return reminderClockIcon();
    if (kind == QStringLiteral("trashed_todo"))    return st->standardIcon(QStyle::SP_TrashIcon);
    return {};
}

// v0.1.97 — actual pencil icon, drawn via QPainter so it renders
// identically on Linux/macOS/Windows regardless of which QStyle
// theme is active. Returns a cached static QIcon so the same shapes
// don't get re-painted on every refresh.
QIcon pencilIcon() {
    static QIcon cached;
    if (!cached.isNull()) return cached;
    // Render at 2x for crispness on HiDPI, then let QIcon downscale.
    const int S = 32;
    QPixmap pm(S, S);
    pm.fill(Qt::transparent);
    QPainter p(&pm);
    p.setRenderHint(QPainter::Antialiasing, true);

    // A clean diagonal pencil: a thick amber bar from lower-left to
    // upper-right, with a dark graphite tip at the lower-left and a
    // small notch (eraser ferrule) at the upper-right.
    p.translate(S / 2.0, S / 2.0);
    p.rotate(-45);                       // orient the pencil diagonally
    p.translate(-S / 2.0, -S / 2.0);

    const qreal x = 8, w = 16;           // horizontal body extent (in rotated space)
    const qreal yTop = 12, h = 8;        // vertical body extent

    // Shaft (amber).
    p.setPen(QPen(QColor("#92400E"), 1.2));
    p.setBrush(QColor("#F59E0B"));
    p.drawRect(QRectF(x, yTop, w, h));

    // Graphite tip — triangle pointing left.
    p.setPen(Qt::NoPen);
    p.setBrush(QColor("#1F2937"));
    QPolygonF tip;
    tip << QPointF(x, yTop) << QPointF(x, yTop + h)
        << QPointF(x - 5, yTop + h / 2.0);
    p.drawPolygon(tip);

    // Eraser ferrule — pink cap at the right end.
    p.setBrush(QColor("#FB7185"));
    p.drawRect(QRectF(x + w - 3, yTop, 4, h));

    p.end();
    cached = QIcon(pm);
    return cached;
}

// v0.1.97 — white sparkle for the red "Extract" button. The old label
// embedded a ✨ (U+2728) emoji which renders as a tofu box on Linux
// systems without a colour-emoji font (feedback_qt_icons_no_emoji).
// Painter-drawn so it shows identically everywhere; white to read on the
// red button fill.
QIcon sparkleIcon() {
    static QIcon cached;
    if (!cached.isNull()) return cached;
    const int S = 32;
    QPixmap pm(S, S);
    pm.fill(Qt::transparent);
    QPainter p(&pm);
    p.setRenderHint(QPainter::Antialiasing, true);
    p.setPen(Qt::NoPen);
    p.setBrush(Qt::white);
    auto star = [&](qreal cx, qreal cy, qreal r) {
        const qreal w = r * 0.32;            // waist of the 4-point star
        QPolygonF s;
        s << QPointF(cx, cy - r) << QPointF(cx + w, cy - w)
          << QPointF(cx + r, cy) << QPointF(cx + w, cy + w)
          << QPointF(cx, cy + r) << QPointF(cx - w, cy + w)
          << QPointF(cx - r, cy) << QPointF(cx - w, cy - w);
        p.drawPolygon(s);
    };
    star(13, 14, 9);                         // main sparkle
    star(24, 23, 5);                         // small companion
    p.end();
    cached = QIcon(pm);
    return cached;
}

// v0.1.97 — grey magnifier for the search field's leading slot. Replaces
// the 🔍 (U+1F50D) that tofu'd in the placeholder text on Linux.
QIcon searchIcon() {
    static QIcon cached;
    if (!cached.isNull()) return cached;
    const int S = 32;
    QPixmap pm(S, S);
    pm.fill(Qt::transparent);
    QPainter p(&pm);
    p.setRenderHint(QPainter::Antialiasing, true);
    p.setPen(QPen(QColor("#9aa0a6"), 3.0, Qt::SolidLine, Qt::RoundCap));
    p.setBrush(Qt::NoBrush);
    p.drawEllipse(QRectF(7, 7, 13, 13));     // lens
    p.drawLine(QPointF(19.5, 19.5), QPointF(25, 25)); // handle
    p.end();
    cached = QIcon(pm);
    return cached;
}

// v0.1.98 — tiny brand-color badge icons for the editor toolbar, matching
// the main tool-strip style (gradient rounded square + white glyph), per the
// user's ask. Painter-drawn — no emoji (feedback_qt_icons_no_emoji).
QIcon toolbarBadge(const QColor &base, char kind) {
    const int S = 32;
    QPixmap pm(S, S);
    pm.fill(Qt::transparent);
    QPainter p(&pm);
    p.setRenderHint(QPainter::Antialiasing, true);
    const QRectF rect(1, 1, S - 2, S - 2);
    QLinearGradient g(rect.topLeft(), rect.bottomRight());
    g.setColorAt(0.0, base.lighter(125));
    g.setColorAt(1.0, base.darker(110));
    p.setPen(QPen(base.darker(160), 1));
    p.setBrush(g);
    p.drawRoundedRect(rect, 8, 8);
    p.setBrush(Qt::NoBrush);
    p.setPen(QPen(Qt::white, 3.0, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
    if (kind == 'H') {                       // white "H"
        p.drawLine(QPointF(11, 9),  QPointF(11, 23));
        p.drawLine(QPointF(21, 9),  QPointF(21, 23));
        p.drawLine(QPointF(11, 16), QPointF(21, 16));
    } else if (kind == 'L') {                // checklist (Action Items)
        p.setPen(QPen(Qt::white, 2.4, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
        p.drawLine(QPointF(9, 12), QPointF(17, 12));   // item line 1
        p.drawLine(QPointF(9, 20), QPointF(15, 20));   // item line 2
        p.drawLine(QPointF(18, 19), QPointF(20.5, 22)); // a tick on the right
        p.drawLine(QPointF(20.5, 22), QPointF(24, 16));
    } else if (kind == 'F') {                // flag/pennant (What I plan)
        p.drawLine(QPointF(11, 8), QPointF(11, 24));   // pole
        p.setPen(Qt::NoPen);
        p.setBrush(Qt::white);
        QPolygonF pennant;
        pennant << QPointF(11, 9) << QPointF(23, 12.5) << QPointF(11, 16);
        p.drawPolygon(pennant);
    } else if (kind == 'T') {                // bulleted list (To-dos)
        p.setBrush(Qt::white);
        p.setPen(Qt::NoPen);
        p.drawRect(QRectF(8.5, 11, 3.2, 3.2));         // bullet 1
        p.drawRect(QRectF(8.5, 19, 3.2, 3.2));         // bullet 2
        p.setBrush(Qt::NoBrush);
        p.setPen(QPen(Qt::white, 2.2, Qt::SolidLine, Qt::RoundCap));
        p.drawLine(QPointF(15, 12.6), QPointF(23, 12.6));
        p.drawLine(QPointF(15, 20.6), QPointF(23, 20.6));
    } else {                                 // white check mark
        p.drawLine(QPointF(9, 16.5),  QPointF(14, 21.5));
        p.drawLine(QPointF(14, 21.5), QPointF(23, 10.5));
    }
    p.end();
    return QIcon(pm);
}
QIcon headerIcon() {                            // indigo badge + "H"
    static QIcon cached;
    if (cached.isNull()) cached = toolbarBadge(QColor("#4F46E5"), 'H');
    return cached;
}
QIcon checkBadgeIcon() {                        // green badge + check
    static QIcon cached;
    if (cached.isNull()) cached = toolbarBadge(QColor("#16A34A"), 'v');
    return cached;
}
QIcon actionItemsIcon() {                       // amber badge + checklist
    static QIcon cached;
    if (cached.isNull()) cached = toolbarBadge(QColor("#D97706"), 'L');
    return cached;
}
QIcon whatIPlanIcon() {                         // violet badge + flag
    static QIcon cached;
    if (cached.isNull()) cached = toolbarBadge(QColor("#7C3AED"), 'F');
    return cached;
}
QIcon toDosIcon() {                             // teal badge + bulleted list
    static QIcon cached;
    if (cached.isNull()) cached = toolbarBadge(QColor("#0D9488"), 'T');
    return cached;
}

// ─── row delegate — paints inline action buttons, native item stays ─
// v0.1.97 — the setItemWidget-per-row approach broke selection,
// click-to-open, AND editItem rename (the custom widget ate clicks and
// the native cell had no text to edit). A QStyledItemDelegate paints the
// buttons ON TOP of the normal row render, so the native item keeps all
// its behavior: click selects + opens, double-click / F2 edits, and the
// delegate's editorEvent intercepts clicks that land on a button.
//
// No Q_OBJECT — we use a std::function callback instead of signals, so
// this can live in the anonymous namespace.
class NoterRowDelegate : public QStyledItemDelegate {
public:
    explicit NoterRowDelegate(QObject *parent = nullptr)
        : QStyledItemDelegate(parent) {}

    static constexpr int kBtnW = 22;   // px per inline button
    static constexpr int kBtnGap = 2;

    // action is one of: "rename" | "trash" | "restore" | "delete".
    std::function<void(const QModelIndex &, const QString &)> onAction;

    // How many buttons this row type shows (0 for roots/sections).
    static int buttonCount(const QModelIndex &idx) {
        const QString kind = idx.data(Qt::UserRole).toString();
        if (kind == QLatin1String("meeting") || kind == QLatin1String("todo") ||
            kind == QLatin1String("reminder"))
            return 2;  // pencil/change + ✕
        if (kind == QLatin1String("trashed_meeting") ||
            kind == QLatin1String("trashed_todo"))
            return 2;  // ↺ + ✕
        return 0;
    }

    QSize sizeHint(const QStyleOptionViewItem &opt,
                   const QModelIndex &idx) const override {
        QSize s = QStyledItemDelegate::sizeHint(opt, idx);
        if (s.height() < 22) s.setHeight(22);
        return s;
    }

    void paint(QPainter *p, const QStyleOptionViewItem &opt,
               const QModelIndex &idx) const override {
        const int nBtns = buttonCount(idx);
        // Reserve space on the right so the text doesn't run under the
        // buttons: shrink the item rect we hand to the base paint.
        QStyleOptionViewItem o = opt;
        if (nBtns > 0)
            o.rect.adjust(0, 0, -(nBtns * (kBtnW + kBtnGap) + 4), 0);
        QStyledItemDelegate::paint(p, o, idx);
        if (nBtns == 0) return;

        const QString kind = idx.data(Qt::UserRole).toString();
        const bool trashed = (kind == QLatin1String("trashed_meeting") ||
                              kind == QLatin1String("trashed_todo"));
        QStyle *st = opt.widget ? opt.widget->style() : qApp->style();

        // Button 0 = rightmost. Button 1 = next-left.
        auto btnRect = [&](int slot) {
            const int right = opt.rect.right() - 4;
            const int x = right - (slot + 1) * kBtnW - slot * kBtnGap;
            return QRect(x, opt.rect.top() + (opt.rect.height() - kBtnW) / 2,
                         kBtnW, kBtnW);
        };

        p->save();
        p->setRenderHint(QPainter::Antialiasing, true);
        if (trashed) {
            // ↺ restore (slot 1) + ✕ delete (slot 0)
            st->standardIcon(QStyle::SP_BrowserReload).paint(p, btnRect(1));
            QIcon del = st->standardIcon(QStyle::SP_DialogCloseButton);
            del.paint(p, btnRect(0));
        } else {
            // pencil (slot 1) + ✕ trash (slot 0)
            pencilIcon().paint(p, btnRect(1));
            st->standardIcon(QStyle::SP_DialogCloseButton).paint(p, btnRect(0));
        }
        p->restore();
    }

    bool editorEvent(QEvent *ev, QAbstractItemModel *model,
                     const QStyleOptionViewItem &opt,
                     const QModelIndex &idx) override {
        const QEvent::Type t = ev->type();
        const bool isMouse = (t == QEvent::MouseButtonPress ||
                              t == QEvent::MouseButtonRelease ||
                              t == QEvent::MouseButtonDblClick);
        if (isMouse && buttonCount(idx) > 0) {
            auto *me = static_cast<QMouseEvent *>(ev);
            const int right = opt.rect.right() - 4;
            QRect btn0(right - kBtnW, opt.rect.top(), kBtnW, opt.rect.height());
            QRect btn1(right - 2 * kBtnW - kBtnGap, opt.rect.top(), kBtnW, opt.rect.height());
            const bool onBtn0 = btn0.contains(me->pos());
            const bool onBtn1 = btn1.contains(me->pos());
            if (onBtn0 || onBtn1) {
                // Consume press + double-click too, so a button interaction
                // NEVER falls through to row select / open / edit. Fire the
                // action once, on release.
                if (t == QEvent::MouseButtonRelease && onAction) {
                    const QString kind = idx.data(Qt::UserRole).toString();
                    const bool trashed = (kind == QLatin1String("trashed_meeting") ||
                                          kind == QLatin1String("trashed_todo"));
                    const bool isReminder = (kind == QLatin1String("reminder"));
                    if (isReminder)
                        onAction(idx, onBtn0 ? QStringLiteral("reminder-delete")
                                             : QStringLiteral("reminder-change"));
                    else if (onBtn0) onAction(idx, trashed ? QStringLiteral("delete")
                                                           : QStringLiteral("trash"));
                    else             onAction(idx, trashed ? QStringLiteral("restore")
                                                           : QStringLiteral("rename"));
                }
                return true;
            }
        }
        return QStyledItemDelegate::editorEvent(ev, model, opt, idx);
    }
};

// ─── trash movement — atomic across canonical + backup ring ─────────
// v0.1.97 — pre-fix: when a meeting was moved to Trash, only the
// canonical `name.html` was renamed; the `.bak1-.bak5` backup ring
// stayed orphaned in Inbox. Worse, an earlier run had moved the ring
// into Trash too, which showed up as separate "quick todos.bak1"
// entries in the user's trashed-meetings list.
//
// Fix: every move/restore/purge takes the CANONICAL stem as input
// and operates on the WHOLE family — `name.html`, `name.html.bak1..5`,
// `name.html.draft`, `name.html.lock`. The same timestamp is reused
// across all files in one move so they group as a single trashed
// meeting for the user.

// Cross-platform file-move with fallback. QFile::rename CAN fail on
// Windows + macOS when the source has a file handle held by another
// process (Defender scan, Spotlight indexer, file watcher). Falls back
// to copy + remove which works around that. Returns true on success.
static bool robustRename(const QString &src, const QString &dst) {
    if (QFile::rename(src, dst)) return true;
    // Fallback: copy then remove source.
    if (!QFile::copy(src, dst)) return false;
    if (!QFile::remove(src)) {
        // Half-state: dst exists but src couldn't be removed. Leave it —
        // the file watcher / locker will eventually release; user can
        // delete manually. Return true so the caller sees the move as
        // successful from the *visible* end (the file IS in the new place).
        return true;
    }
    return true;
}

// Move `absPath` (a canonical .html in Inbox) + its backup ring + draft
// to Trash. Returns the trashed canonical path, or empty on failure.
QString moveMeetingTreeToTrash(const QString &absPath, const QString &trashFolderPath) {
    QFileInfo fi(absPath);
    if (!fi.exists()) return {};
    const QString stem = fi.fileName();  // foo.html
    QDir srcDir = fi.dir();
    QDir().mkpath(trashFolderPath);
    const qint64 ts = QDateTime::currentMSecsSinceEpoch();
    QString trashedCanonical;
    // Family: foo.html, foo.html.bak1, foo.html.bak2, ..., foo.html.draft
    for (const QString &name : srcDir.entryList(QStringList() << stem + "*",
                                                QDir::Files | QDir::Hidden)) {
        const QString srcAbs = srcDir.absoluteFilePath(name);
        const QString dstName = QStringLiteral(".trashed-%1-%2").arg(ts).arg(name);
        const QString dstAbs = QDir(trashFolderPath).absoluteFilePath(dstName);
        if (robustRename(srcAbs, dstAbs) && name == stem) trashedCanonical = dstAbs;
    }
    return trashedCanonical;
}

// Restore `trashedAbsPath` (a canonical .trashed-<ts>-foo.html in Trash)
// + its backup ring + draft back to Inbox. Returns the restored
// canonical path, or empty on failure.
QString restoreMeetingTreeFromTrash(const QString &trashedAbsPath,
                                     const QString &inboxFolderPath) {
    QFileInfo fi(trashedAbsPath);
    if (!fi.exists()) return {};
    // Extract the timestamp prefix from `.trashed-<ts>-<rest>`.
    static const QRegExp tsPattern(QStringLiteral("^(\\.trashed-\\d+-)(.+)$"));
    QRegExp p = tsPattern;  // copy — exactMatch mutates
    if (p.indexIn(fi.fileName()) < 0) return {};
    const QString trashedPrefix = p.cap(1);
    const QString origName      = p.cap(2);
    const QString stem          = origName;  // foo.html
    QDir trashDir = fi.dir();
    QDir().mkpath(inboxFolderPath);
    QString restoredCanonical;
    // Find every trashed file sharing the same timestamp prefix.
    for (const QString &name : trashDir.entryList(QStringList() << trashedPrefix + "*",
                                                  QDir::Files | QDir::Hidden)) {
        const QString srcAbs = trashDir.absoluteFilePath(name);
        QString restoredName = name;
        restoredName.remove(trashedPrefix);  // strip ".trashed-<ts>-"
        const QString dstAbs = QDir(inboxFolderPath).absoluteFilePath(restoredName);
        if (robustRename(srcAbs, dstAbs) && restoredName == stem)
            restoredCanonical = dstAbs;
    }
    return restoredCanonical;
}

// Permanently delete `trashedAbsPath` + its backup ring + draft.
// Returns true if the canonical was removed.
bool permanentlyDeleteMeetingTree(const QString &trashedAbsPath) {
    QFileInfo fi(trashedAbsPath);
    if (!fi.exists()) return false;
    static const QRegExp tsPattern(QStringLiteral("^(\\.trashed-\\d+-)(.+)$"));
    QRegExp p = tsPattern;
    if (p.indexIn(fi.fileName()) < 0) return false;
    const QString trashedPrefix = p.cap(1);
    QDir trashDir = fi.dir();
    bool removedCanonical = false;
    for (const QString &name : trashDir.entryList(QStringList() << trashedPrefix + "*",
                                                  QDir::Files | QDir::Hidden)) {
        if (QFile::remove(trashDir.absoluteFilePath(name)) &&
            name == trashedPrefix + p.cap(2)) {
            removedCanonical = true;
        }
    }
    return removedCanonical;
}

// A7 — human-readable "how long ago" for the draft-recovery prompt.
QString relativeTimeString(const QDateTime &then) {
    if (!then.isValid()) return QObject::tr("an unknown time ago");
    const qint64 secs = qMax<qint64>(0, then.secsTo(QDateTime::currentDateTime()));
    if (secs < 90)      return QObject::tr("moments ago");
    if (secs < 3600)    return QObject::tr("%n minute(s) ago", "", int(secs / 60));
    if (secs < 86400)   return QObject::tr("%n hour(s) ago",   "", int(secs / 3600));
    return QObject::tr("%n day(s) ago", "", int(secs / 86400));
}

// Group a note's CREATION time into a recency bucket label. Older than
// "This month" subdivides by month: same year → "March" (locale name),
// prior years → "November 2025".
QString recencyBucket(const QDateTime &created) {
    const QDate today = QDate::currentDate();
    const QDate cdate = created.date();
    if (cdate == today) return QStringLiteral("Today");
    if (cdate == today.addDays(-1)) return QStringLiteral("Yesterday");
    if (created.daysTo(QDateTime::currentDateTime()) <= 7)
        return QStringLiteral("This week");
    if (cdate.year() == today.year() && cdate.month() == today.month())
        return QStringLiteral("This month");
    if (cdate.year() == today.year())
        return cdate.toString(QStringLiteral("MMMM"));
    return cdate.toString(QStringLiteral("MMMM yyyy"));
}

// v0.1.112 — creation time parsed from the filename stamp written by
// newMeetingNote ("yyyy-MM-dd-hhmmss-") or the legacy 4-digit HHmm
// form; mtime fallback for un-stamped / hand-renamed files. The rename
// handler preserves the prefix so renamed notes keep their creation
// date. Invalid stamps (e.g. 2026-13-99) fail isValid() → mtime.
QDateTime creationTimeOf(const QFileInfo &fi) {
    static const QRegularExpression kStamp(
        QStringLiteral("^(\\d{4}-\\d{2}-\\d{2})-(\\d{4}|\\d{6})-"));
    const QRegularExpressionMatch m = kStamp.match(fi.fileName());
    if (m.hasMatch()) {
        const QDate d = QDate::fromString(m.captured(1), Qt::ISODate);
        const QString t = m.captured(2);
        const QTime tm = (t.size() == 6)
            ? QTime::fromString(t, QStringLiteral("HHmmss"))
            : QTime::fromString(t, QStringLiteral("HHmm"));
        if (d.isValid() && tm.isValid()) return QDateTime(d, tm);
    }
    return fi.lastModified();
}

// v0.1.112 — every whitespace-split term must appear in the haystack.
// Strict superset of the old single contains() (a haystack containing the
// whole phrase contains every term). Terms arrive pre-lowercased
// (refreshSidebar trims+lowercases the filter); haystack must be
// lowercased by the caller.
bool matchesAllTerms(const QString &haystackLower, const QStringList &terms) {
    for (const QString &t : terms)
        if (!haystackLower.contains(t)) return false;
    return true;
}

// A3 — 5-entity escape for the closed-note rename's <h1> inner-text
// patch. Same table as NotesTemplate::escapeText / the notepatra-title
// meta writer, so a rename-written H1 round-trips identically to a
// template-written one.
QString titleEntityEscape(const QString &raw) {
    QString out;
    out.reserve(raw.size() + 16);
    for (const QChar c : raw) {
        switch (c.unicode()) {
            case '&':  out += QStringLiteral("&amp;");  break;
            case '<':  out += QStringLiteral("&lt;");   break;
            case '>':  out += QStringLiteral("&gt;");   break;
            case '"':  out += QStringLiteral("&quot;"); break;
            case '\'': out += QStringLiteral("&#39;");  break;
            default:   out += c;
        }
    }
    return out;
}

}  // namespace

// ═══════════════════════════════════════════════════════════════════════
//  NotesPanel — construction
// ═══════════════════════════════════════════════════════════════════════

NotesPanel::NotesPanel(QWidget *parent, NotesTodos *sharedTodos,
                       NotesReminderEngine *sharedEngine)
    : QWidget(parent) {
    // A5 — resolve the CURRENT app theme before buildUi() so construction
    // renders Light/Dark/Monokai correctly from the first frame (the
    // pre-A5 panel was a hardcoded light-cream island in dark themes).
    m_pal = noterPaletteForTheme(Config::instance().theme);
    ensureNotesFolder();
    m_storage = new NotesStorage(notesRoot(), this);
    m_ownsTodos = (sharedTodos == nullptr);
    m_todos = sharedTodos ? sharedTodos : new NotesTodos(todosDbPath());
    m_todos->open(nullptr);   // idempotent; self-heals a failed service open

    const bool ownsEngine = (sharedEngine == nullptr);
    m_reminders = sharedEngine ? sharedEngine
                               : new NotesReminderEngine(m_todos, this);
    // ALWAYS (owned or shared): in-window banner + live sidebar Reminders
    // count. Receiver is `this` → connections die with the panel; a shared
    // engine keeps polling for MainWindow's desktop-toast path.
    connect(m_reminders, &NotesReminderEngine::reminderDue, this,
            [this](const TodoRow &r) {
                enqueueReminder(r);
                refreshSidebar();   // fired row left allScheduledReminders
            });
    if (ownsEngine) {
        // Standalone construction (widget tests / screenshot harness): the
        // panel is also the notification surface — v0.1.97 behavior
        // verbatim. This fork is the zero-regression contract for default
        // construction; do NOT "simplify" it away — the app-lifetime path
        // (shared engine) keeps its toast lambdas in MainWindow instead.
        connect(m_reminders, &NotesReminderEngine::reminderDue, this,
                [](const TodoRow &r) {
                    const QString title = tr("Noter reminder");
                    QString body = r.text;
                    if (!r.owner.isEmpty()) body += QStringLiteral("  ") + r.owner;
                    if (!r.meetingTitle.isEmpty())
                        body += QStringLiteral("\n— from \"%1\"").arg(r.meetingTitle);
                    fireDesktopNotification(title, body);
                });
        connect(m_reminders, &NotesReminderEngine::missedBatch, this,
                [](const QVector<TodoRow> &batch) {
                    const QString title = tr("Noter — %n reminder(s) missed", "", batch.size());
                    QString body;
                    int n = 0;
                    for (const TodoRow &r : batch) {
                        if (n++ >= 4) { body += tr("\n…and %1 more").arg(batch.size() - n + 1); break; }
                        body += (n > 1 ? "\n" : "") + r.text;
                    }
                    fireDesktopNotification(title, body);
                });
        // catchUpMissed() runs first so notifications that should have
        // fired while the app was closed still surface (as a single
        // batch alert, not N popups). Then start() begins the 60s
        // poll loop. In shared mode MainWindow already did both.
        m_reminders->catchUpMissed();
        m_reminders->start();
    }

    buildUi();
    refreshSidebar();
    showEmptyPage();

    // v0.1.112 — warm the body-search cache off the critical path so the
    // first search keystroke never pays ~300 cold file reads. Receiver-
    // context singleShot: destruction of the panel cancels it.
    QTimer::singleShot(1200, this, [this] {
        m_prewarmQueue = QDir(inboxFolder()).entryList(
            QStringList() << QStringLiteral("*.html"), QDir::Files);
        prewarmBodyCache();
    });

    // Autosave tick — 5s, configurable via global setting.
    m_autosave = new QTimer(this);
    connect(m_autosave, &QTimer::timeout, this, &NotesPanel::onAutoSaveTick);
    const int interval = qBound(1, Config::instance().autoSaveIntervalSec, 300) * 1000;
    m_autosave->start(interval);

    // A7 — draft cadence. Armed by onEditorBodyChanged on the first dirty
    // keystroke; fires ~1.5s later and writes the .draft sidecar so a hard
    // crash loses at most ~2s of typing (the autosave window alone left up
    // to 5s on the floor — the .draft API existed but had ZERO callers).
    // Re-armed by the next keystroke after each write, so sustained typing
    // refreshes the draft roughly every 1.5s. saveNote() clears the draft
    // on every clean save.
    m_draftTimer = new QTimer(this);
    m_draftTimer->setSingleShot(true);
    m_draftTimer->setInterval(1500);
    connect(m_draftTimer, &QTimer::timeout, this, [this]() {
        if (!m_dirty || m_currentPath.isEmpty() || m_readError ||
            m_currentIsChecklist || !m_editor || !m_storage)
            return;
        m_storage->writeDraft(m_currentPath, m_editor->toHtml());
    });

    // ── shortcuts ───────────────────────────────────────────────────
    // All Noter-scoped (active when this widget is in the focus chain).
    auto bind = [this](const char *seq, auto fn) {
        auto *s = new QShortcut(QKeySequence(QLatin1String(seq)), this);
        s->setContext(Qt::WidgetWithChildrenShortcut);
        connect(s, &QShortcut::activated, this, fn);
    };
    bind("Ctrl+Alt+M", [this]() { newMeetingNote(); });
    bind("Ctrl+Alt+J", [this]() { quickSwitchMeeting(); });
    // Ctrl+Alt+T focuses the Reminders root in the sidebar tree (root
    // order: Notes / Reminders / Trash — the v0.1.97 Todos root became
    // Reminders in v0.1.98). Help text documents it as "jump to the
    // Reminders section"; keep the two in sync.
    bind("Ctrl+Alt+T", [this]() {
        if (m_sidebarTree && m_sidebarTree->topLevelItemCount() >= 2) {
            auto *reminders = m_sidebarTree->topLevelItem(1);
            reminders->setExpanded(true);
            m_sidebarTree->setCurrentItem(reminders);
            m_sidebarTree->setFocus();
        }
    });
    bind("Ctrl+Alt+E", [this]() { endMeetingSweep(); });
    bind("Ctrl+Alt+B", [this]() { toggleSidebar(); });
    bind("Ctrl+Alt+P", [this]() { popOutActive(); });
    bind("F4",         [this]() { toggleCheckboxOnCurrentLine(); });
}

NotesPanel::~NotesPanel() {
    // v0.1.112 — an app-wide override cursor must not outlive the panel
    // if it's torn down mid-Extract (the client + watchdog are children
    // and die with us; the override cursor is global state).
    if (m_extractBusy) {
        QApplication::restoreOverrideCursor();
        m_extractBusy = false;
    }
    if (m_dirty && !m_currentPath.isEmpty()) saveCurrentNote();
    // Shared (MainWindow-owned) todos must survive the panel; only a
    // self-owned instance is ours to delete. (Owned engine is panel-
    // parented → auto-deleted; shared engine is MainWindow-parented.)
    if (m_ownsTodos) delete m_todos.data();
    m_todos.clear();
}

void NotesPanel::buildUi() {
    auto *outer = new QVBoxLayout(this);
    outer->setContentsMargins(0, 0, 0, 0);
    outer->setSpacing(0);

    m_splitter = new QSplitter(Qt::Horizontal, this);
    m_splitter->setChildrenCollapsible(false);
    m_splitter->setHandleWidth(1);
    m_splitter->setStyleSheet(
        QStringLiteral("QSplitter::handle { background: %1; }").arg(m_pal.sidebarBorder));

    m_sidebar = buildSidebar();
    m_splitter->addWidget(m_sidebar);

    // Right pane = page stack with the reminder banner PINNED ABOVE it.
    // buildEditorPage() builds the banner into the editor page, but a
    // QStackedWidget hides its non-current page -- so the banner was
    // invisible whenever the empty page was current (the default on open and
    // the exact state during replayReminders()). Host it in a container above
    // the stack so a fired/replayed reminder shows over BOTH pages.
    auto *rightSide = new QWidget(this);
    auto *rv = new QVBoxLayout(rightSide);
    rv->setContentsMargins(0, 0, 0, 0);
    rv->setSpacing(0);

    m_rightStack = new QStackedWidget(rightSide);
    m_emptyPage = buildEmptyPage();
    m_editorPage = buildEditorPage();          // creates m_reminderBanner
    m_rightStack->addWidget(m_emptyPage);
    m_rightStack->addWidget(m_editorPage);

    // Lift the reminder banner out of the editor page's layout (added there
    // as its first row) and re-host it above the stack. removeWidget +
    // addWidget reparents it to rightSide; the save-failure banner stays on
    // the editor page (it only matters while a note is being edited).
    if (m_reminderBanner) {
        if (QLayout *epl = m_editorPage->layout())
            epl->removeWidget(m_reminderBanner);
        rv->addWidget(m_reminderBanner);
    }
    rv->addWidget(m_rightStack, 1);

    m_splitter->addWidget(rightSide);

    m_splitter->setStretchFactor(0, 0);
    m_splitter->setStretchFactor(1, 1);
    m_splitter->setSizes({240, 860});

    // Hidden-sidebar restore strip — Ctrl+Alt+B used to be the ONLY way
    // back once the sidebar was hidden. A slim chevron strip on the left
    // edge (inside the Noter tab) re-opens it with the mouse.
    m_sidebarRestoreBtn = new QToolButton(this);
    m_sidebarRestoreBtn->setObjectName(QStringLiteral("noterSidebarRestore"));
    m_sidebarRestoreBtn->setIcon(style()->standardIcon(QStyle::SP_ArrowRight));
    m_sidebarRestoreBtn->setToolTip(tr("Show sidebar (Ctrl+Alt+B)"));
    m_sidebarRestoreBtn->setCursor(Qt::PointingHandCursor);
    m_sidebarRestoreBtn->setFixedWidth(18);
    m_sidebarRestoreBtn->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Expanding);
    m_sidebarRestoreBtn->setStyleSheet(sidebarRestoreStyle(m_pal));
    m_sidebarRestoreBtn->hide();   // only visible while the sidebar is hidden
    connect(m_sidebarRestoreBtn, &QToolButton::clicked, this,
            [this]() { toggleSidebar(); });

    auto *row = new QHBoxLayout;
    row->setContentsMargins(0, 0, 0, 0);
    row->setSpacing(0);
    row->addWidget(m_sidebarRestoreBtn);
    row->addWidget(m_splitter, 1);
    outer->addLayout(row, 1);
}

QWidget *NotesPanel::buildSidebar() {
    auto *w = new QWidget(this);
    w->setObjectName("noterSidebar");
    w->setStyleSheet(sidebarStyle(m_pal));
    w->setMinimumWidth(180);
    w->setMaximumWidth(360);

    auto *v = new QVBoxLayout(w);
    v->setContentsMargins(0, 0, 0, 0);
    v->setSpacing(0);

    // Search
    auto *searchWrap = new QWidget(w);
    auto *searchLayout = new QHBoxLayout(searchWrap);
    searchLayout->setContentsMargins(12, 12, 12, 8);
    searchLayout->setSpacing(0);
    m_search = new QLineEdit(searchWrap);
    m_search->setObjectName("noterSearch");
    m_search->setPlaceholderText(tr("Search notes — titles & content"));
    m_search->setToolTip(tr("Search notes — titles & content (Ctrl+Alt+J)"));
    m_search->addAction(searchIcon(), QLineEdit::LeadingPosition);
    m_search->setClearButtonEnabled(true);
    searchLayout->addWidget(m_search);
    v->addWidget(searchWrap);
    connect(m_search, &QLineEdit::textChanged, this, &NotesPanel::onSearchChanged);

    // v0.1.112 — live match-count feedback under the search box. Hidden by
    // default → zero layout shift when not searching. refreshSidebar drives
    // its text ("%n match(es)" / "No matches") while a filter is active.
    m_searchStatus = new QLabel(w);
    m_searchStatus->setObjectName(QStringLiteral("noterSearchStatus"));
    m_searchStatus->setStyleSheet(
        QStringLiteral("color:%1; font-size:11px; padding:0 14px 4px;").arg(m_pal.mutedText));
    m_searchStatus->hide();
    v->addWidget(m_searchStatus);

    // v0.1.98 — single "+ Noter" create button. Todos were dropped
    // entirely (user 2026-05-24): a reminder now lives on the note itself
    // (right-click a Noter → Set reminder), so a separate todo type is
    // redundant. The NotesTodos store stays as the reminder backend only.
    auto *newWrap = new QWidget(w);
    auto *newLayout = new QHBoxLayout(newWrap);
    newLayout->setContentsMargins(12, 4, 12, 8);
    newLayout->setSpacing(6);
    m_newBtn = new QPushButton(tr("+ Noter"), newWrap);
    m_newBtn->setObjectName("noterNewBtn");
    m_newBtn->setToolTip(tr("New note (Ctrl+Alt+M)"));
    m_newBtn->setCursor(Qt::PointingHandCursor);
    newLayout->addWidget(m_newBtn, 1);
    v->addWidget(newWrap);
    connect(m_newBtn, &QPushButton::clicked, this, &NotesPanel::onNewMeetingClicked);
    // v0.1.98 — two-root tree sidebar after todos were dropped.
    // Root 1: Notes  (date-grouped children → Noter leaves)
    // Root 2: Trash  (trashed Noters). Click any leaf to open.
    m_sidebarTree = new QTreeWidget(w);
    m_sidebarTree->setObjectName(QStringLiteral("noterSidebarTree"));
    m_sidebarTree->setHeaderHidden(true);
    m_sidebarTree->setFrameShape(QFrame::NoFrame);
    m_sidebarTree->setRootIsDecorated(true);
    m_sidebarTree->setIndentation(14);
    m_sidebarTree->setUniformRowHeights(true);
    m_sidebarTree->setSelectionMode(QAbstractItemView::SingleSelection);
    m_sidebarTree->setContextMenuPolicy(Qt::CustomContextMenu);
    m_sidebarTree->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    // Pin the viewport palette to the light sidebar colour. The QSS rule in
    // sidebarStyle() is the primary fix; this guarantees a light Base even if
    // a platform style bypasses the stylesheet selector (the macOS dark
    // item-view-viewport bug). Exactly one mechanism is ever active and both
    // resolve to the same light colour.
    {
        QPalette tp = m_sidebarTree->palette();
        tp.setColor(QPalette::Base, QColor(m_pal.sidebarBg));
        tp.setColor(QPalette::Text, QColor(m_pal.text));
        m_sidebarTree->setPalette(tp);
    }
    v->addWidget(m_sidebarTree, 1);
    // v0.1.97 fix — single click SELECTS (native), double click / Enter
    // OPENS. The previous build wired itemClicked (single click) to open,
    // which (a) opened on every plain click and (b) opened the file when
    // you clicked a pencil/✕ button, because Qt emits clicked() BEFORE the
    // delegate's editorEvent gets to consume the button hit. So a button
    // click did "open + trash" or "open + rename" at once. Only
    // itemActivated (Enter) + itemDoubleClicked open now.
    connect(m_sidebarTree, &QTreeWidget::itemActivated,
            this, &NotesPanel::onSidebarItemActivated);
    connect(m_sidebarTree, &QTreeWidget::itemDoubleClicked,
            this, &NotesPanel::onSidebarItemActivated);
    // Rename is driven ONLY by the pencil button (delegate → edit()), F2,
    // and the context menu. NOT SelectedClicked — with double-click-to-open
    // that would turn the 2nd click of a double-click into an edit instead
    // of an open. Keep EditKeyPressed so F2 still renames.
    m_sidebarTree->setEditTriggers(QAbstractItemView::EditKeyPressed);
    connect(m_sidebarTree, &QTreeWidget::itemChanged, this,
            [this](QTreeWidgetItem *it, int) {
                if (m_loadingTree || !it) return;
                const QString kind = it->data(0, Qt::UserRole).toString();
                const QString payload = it->data(0, Qt::UserRole + 1).toString();
                QString newText = it->text(0).trimmed();
                if (newText.isEmpty()) { refreshSidebar(); return; }
                if (kind == QStringLiteral("meeting") && !payload.isEmpty()) {
                    // A3 — titles are full Unicode; cap at the same 200
                    // chars safeFilename / the meta writer enforce.
                    newText = newText.left(200);
                    // True no-op vs the RESOLVER display title. Slug-equality
                    // is no longer the no-op test: "Café Sync" → "Cafe Sync"
                    // shares a slug but IS a title change the meta must keep.
                    // Unchanged text → zero disk writes, zero renames.
                    if (newText == m_storage->displayTitleForFile(payload)) {
                        refreshSidebar();
                        return;
                    }
                    // Rename the file (canonical + backup ring + draft).
                    QFileInfo fi(payload);
                    QString slug = NotesStorage::safeFilename(newText);
                    if (slug.isEmpty()) { refreshSidebar(); return; }
                    // Keep the creation-date prefix from the existing filename
                    // so date-bucket sorting stays correct. New notes are
                    // stamped yyyy-MM-dd-hhmmss (6-digit time, newMeetingNote);
                    // legacy ones used a 4-digit HHMM — accept both, matching
                    // the display-side parser in populateMeetingsRoot. (\d{4}
                    // alone silently DROPPED the prefix off every new note.)
                    static const QRegExp prefixRx(QStringLiteral("^(\\d{4}-\\d{2}-\\d{2}-\\d{4,6}-)"));
                    QRegExp px = prefixRx;
                    QString prefix;
                    if (px.indexIn(fi.fileName()) >= 0) prefix = px.cap(1);
                    QString newName = prefix + slug + QStringLiteral(".html");
                    QDir d = fi.dir();
                    const QString oldStem = fi.fileName();
                    // A3 — the filename is a stable ASCII disk-id, not the
                    // title. A pure-CJK/emoji title folds to "untitled" and
                    // must KEEP the existing filename (sync tools watch this
                    // dir; "<prefix>-untitled.html" would be meaningless).
                    // The title still commits to the meta below.
                    const bool doRename = slug != QStringLiteral("untitled")
                                          && newName != oldStem;
                    QString newAbs = payload;
                    if (doRename) {
                    // Target already taken by another note? Dedup with " (n)"
                    // inserted BEFORE the extension — split on the LAST dot,
                    // never QFileInfo::baseName() (it splits on the FIRST dot
                    // and mangles dotted names like "spec-1.2.html").
                    if (d.exists(newName)) {
                        const int dot = newName.lastIndexOf(QChar('.'));
                        const QString stem = (dot > 0) ? newName.left(dot) : newName;
                        const QString ext  = (dot > 0) ? newName.mid(dot)  : QString();
                        for (int n = 2; n < 100 && d.exists(newName); ++n)
                            newName = stem + QStringLiteral(" (%1)").arg(n) + ext;
                    }
                    // Rename the canonical file FIRST and CHECK the result.
                    // QDir::rename fails on read-only dirs / existing targets;
                    // unchecked, the buffer got repointed at a file that was
                    // never renamed — i.e. at ANOTHER note, which the next
                    // autosave would then clobber. A failure bails BEFORE any
                    // title write, so the old label/title/meta stay intact
                    // and the message below stays honest.
                    if (d.exists(newName) || !d.rename(oldStem, newName)) {
                        if (auto *mw = window())
                            if (auto *sb = mw->findChild<QStatusBar *>())
                                sb->showMessage(tr("Rename failed — the note keeps its old name"), 4000);
                        refreshSidebar();   // restores the old label
                        return;
                    }
                    // Canonical renamed — carry the family along (best-effort).
                    for (const QString &name : d.entryList(QStringList() << oldStem + "*",
                                                           QDir::Files | QDir::Hidden)) {
                        QString suffix = name.mid(oldStem.size()); // .bak1 / .draft
                        d.rename(name, newName + suffix);
                    }
                    newAbs = d.absoluteFilePath(newName);
                    if (payload == m_currentPath) m_currentPath = newAbs;
                    // A3 — a live pop-out mirroring this note must follow the
                    // rename (its 2s reload loop reads the path each tick).
                    if (m_popOut && m_popOut->notePath() == payload)
                        m_popOut->setNotePath(newAbs);
                    // M5/A3 — reminders + todos key off source_file. Follow
                    // the file to newAbs so a reminder set on this note still
                    // opens it: the open-note save below re-points HTML action
                    // rows via reindexNote, but that SKIPS the synthetic
                    // reminder rows, which would otherwise strand at the dead
                    // path and red-error the editor on the next reminder click.
                    if (m_todos) m_todos->repathNote(payload, newAbs);
                    }   // doRename

                    // ── A3 title commit — disk rename FIRST, title SECOND,
                    // so a rename failure leaves *everything* untouched. ──
                    if (newAbs == m_currentPath && !m_currentIsChecklist) {
                        // Open note: rewrite the H1 in the live document and
                        // force-save — saveCurrentNote injects the meta,
                        // reindexes todos.db and refreshes the sidebar.
                        // m_lastSeenH1 is pre-set so the save's adoption
                        // block sees no H1 *change* (no double-adopt).
                        setEditorH1(newText);
                        m_currentTitle = newText;
                        m_lastSeenH1   = newText;
                        m_dirty = true;
                        saveCurrentNote();
                        // The sidebar rename is not an editor edit — Ctrl+Z
                        // must not resurrect the old title text (the next
                        // autosave would silently re-adopt it).
                        if (m_editor) m_editor->document()->clearUndoRedoStacks();
                        emit noteTitleChanged(m_currentTitle);
                    } else {
                        // Closed note: string-level head surgery on the raw
                        // bytes (NOT a QTextDocument round-trip — that would
                        // Qt-normalize a never-opened template file). Replace
                        // the first <h1>'s INNER text (tag + attrs kept so
                        // class="meet-title" survives for the todos parser),
                        // then upsert the meta; saveNote's sanitize/validate
                        // + atomic write gate corruption.
                        QString err;
                        QString raw = m_storage->readNote(newAbs, &err);
                        bool committed = false;
                        if (err.isEmpty()) {
                            static const QRegularExpression h1Re(
                                QStringLiteral("(<h1\\b[^>]*>).*?(</h1>)"),
                                QRegularExpression::CaseInsensitiveOption
                                    | QRegularExpression::DotMatchesEverythingOption);
                            const QRegularExpressionMatch hm = h1Re.match(raw);
                            if (hm.hasMatch())
                                raw.replace(hm.capturedStart(), hm.capturedLength(),
                                            hm.captured(1) + titleEntityEscape(newText)
                                                + hm.captured(2));
                            QString err2;
                            committed = m_storage->saveNote(
                                newAbs,
                                NotesStorage::withTitleMeta(raw, newText), &err2);
                        }
                        if (!committed) {
                            // Honest status — the file rename (if any) stands;
                            // the resolver falls back to the renamed filename,
                            // so the label still reads the new name.
                            if (auto *mw = window())
                                if (auto *sb = mw->findChild<QStatusBar *>())
                                    sb->showMessage(tr("Renamed, but the title could not be written into the note"), 4000);
                        }
                    }
                    if (m_popOut && m_popOut->notePath() == newAbs)
                        m_popOut->setDisplayTitle(newText);
                    m_storage->invalidateTitleCache(payload);
                    m_storage->invalidateTitleCache(newAbs);
                    refreshSidebar();
                } else if (kind == QStringLiteral("todo") && !payload.isEmpty()) {
                    if (m_todos) m_todos->setText(payload, newText);
                    refreshSidebar();
                }
            });
    // v0.1.97 — paint pencil/✕ (live rows) or ↺/✕ (trash rows) ON TOP of
    // the native item via a delegate, so click-to-open + double-click
    // rename keep working (the old setItemWidget ate every click).
    auto *rowDelegate = new NoterRowDelegate(m_sidebarTree);
    m_sidebarTree->setItemDelegate(rowDelegate);
    rowDelegate->onAction = [this](const QModelIndex &idx, const QString &action) {
        // Read kind + payload straight off the model index — itemFromIndex
        // is protected on QTreeWidget, and we don't need the item itself.
        const QString kind    = idx.data(Qt::UserRole).toString();
        const QString payload = idx.data(Qt::UserRole + 1).toString();
        if (action == QLatin1String("rename")) {
            // edit() must run AFTER the mouse-release that triggered us
            // fully unwinds — calling it re-entrantly from inside the
            // delegate's editorEvent silently no-ops. Defer one tick.
            QPersistentModelIndex pidx(idx);
            m_sidebarTree->setCurrentIndex(idx);
            QTimer::singleShot(0, this, [this, pidx]() {
                if (!pidx.isValid()) return;
                m_sidebarTree->edit(QModelIndex(pidx));
                QTimer::singleShot(0, this, [this]() {
                    if (auto *le = qobject_cast<QLineEdit *>(QApplication::focusWidget()))
                        le->selectAll();
                });
            });
        } else if (action == QLatin1String("trash")) {
            if (kind == QLatin1String("meeting")) {
                if (payload == m_currentPath) {
                    m_dirty = false; showEmptyPage(); m_currentPath.clear();
                }
                moveMeetingTreeToTrash(payload, trashFolder());
                refreshSidebar();
                if (auto *mw = window())
                    if (auto *sb = mw->findChild<QStatusBar *>())
                        sb->showMessage(tr("Meeting moved to Trash — recover it under Trash"), 4000);
            } else if (kind == QLatin1String("todo")) {
                if (m_todos) m_todos->trashRow(payload);
                refreshSidebar();
                if (auto *mw = window())
                    if (auto *sb = mw->findChild<QStatusBar *>())
                        sb->showMessage(tr("Todo moved to Trash — recover it under Trash"), 4000);
            }
        } else if (action == QLatin1String("restore")) {
            if (kind == QLatin1String("trashed_meeting"))
                restoreMeetingTreeFromTrash(payload, inboxFolder());
            else if (kind == QLatin1String("trashed_todo") && m_todos)
                m_todos->restoreRow(payload);
            refreshSidebar();
            if (auto *mw = window())
                if (auto *sb = mw->findChild<QStatusBar *>())
                    sb->showMessage(tr("Restored from Trash"), 3000);
        } else if (action == QLatin1String("delete")) {
            if (noterMessageBox(this, m_pal, QMessageBox::Warning,
                    tr("Delete permanently?"),
                    tr("Permanently delete this item? It cannot be recovered."),
                    QMessageBox::Yes | QMessageBox::No, QMessageBox::No)
                != QMessageBox::Yes) return;
            if (kind == QLatin1String("trashed_meeting"))
                permanentlyDeleteMeetingTree(payload);
            else if (kind == QLatin1String("trashed_todo") && m_todos)
                m_todos->deleteRow(payload);
            refreshSidebar();
        } else if (action == QLatin1String("reminder-delete")) {
            // ✕ on a reminder row — no scary confirm; a reminder is trivial to
            // recreate. The reminder id lives in UserRole+2 (UserRole+1 is the
            // note path, used for click-to-open).
            const QString id = idx.data(Qt::UserRole + 2).toString();
            if (m_todos && !id.isEmpty()) m_todos->deleteRow(id);
            refreshSidebar();
            if (auto *mw = window())
                if (auto *sb = mw->findChild<QStatusBar *>())
                    sb->showMessage(tr("Reminder deleted"), 3000);
        } else if (action == QLatin1String("reminder-change")) {
            // Defer the modal one tick so it opens AFTER editorEvent unwinds.
            const QString id = idx.data(Qt::UserRole + 2).toString();
            QTimer::singleShot(0, this, [this, id]() { changeReminderTime(id); });
        }
    };
    // v0.1.97 — context menu dispatches based on item type:
    // - "meeting" / "trashed_meeting" → Open / Delete-or-Restore
    // - "todo"   / "trashed_todo"     → Open source / Mark done / Trash / Restore
    // - root nodes / section headers  → no menu (let user expand/collapse natively)
    connect(m_sidebarTree, &QTreeWidget::customContextMenuRequested,
            this, [this](const QPoint &pos) {
                QTreeWidgetItem *it = m_sidebarTree->itemAt(pos);
                if (!it) return;
                const QString kind = it->data(0, Qt::UserRole).toString();
                if (kind.isEmpty() || kind == QStringLiteral("root") ||
                    kind == QStringLiteral("section")) return;
                const QString payload = it->data(0, Qt::UserRole + 1).toString();
                QMenu menu(this);
                menu.setStyleSheet(QStringLiteral(
                    "QMenu { background: %1; color: %2;"
                    "  border: 1px solid %3; padding: 4px 0; }"
                    "QMenu::item { padding: 7px 24px 7px 22px; font-size: 13px; }"
                    "QMenu::item:selected { background: %4; color: %5; }"
                    "QMenu::separator { height: 1px; background: %6;"
                    "  margin: 4px 8px; }"
                ).arg(m_pal.menuBg, m_pal.menuFg, m_pal.menuBorder,
                      m_pal.menuSelBg, m_pal.menuSelFg, m_pal.menuSep));
                if (kind == QStringLiteral("meeting")) {
                    // v0.1.98 CRASH FIX — `menu.exec()` below runs a nested
                    // event loop. The 5s autosave timer (onAutoSaveTick →
                    // saveCurrentNote → refreshSidebar → tree->clear()) can
                    // fire during it and DELETE `it`, so any deref of `it`
                    // after exec is use-after-free (SIGSEGV, core 871751).
                    // Capture everything we need from `it` BEFORE exec; for
                    // actions that need a live item (rename), re-find it by
                    // path afterwards.
                    const QString itemLabel = it->text(0);
                    QAction *aOpen   = menu.addAction(tr("Open"));
                    QAction *aRename = menu.addAction(tr("Rename…"));
                    // v0.1.98 — schedule a reminder on the note itself.
                    const bool hasRem =
                        m_todos && m_todos->noteReminderAt(payload).isValid();
                    QAction *aRemind = menu.addAction(
                        hasRem ? tr("Change reminder…") : tr("Set reminder…"));
                    menu.addSeparator();
                    QAction *aExpPdf = menu.addAction(tr("Export as PDF…"));
                    QAction *aExpMd  = menu.addAction(tr("Export as Markdown…"));
                    menu.addSeparator();
                    QAction *aDelete = menu.addAction(tr("Move to Trash"));
                    QAction *picked = menu.exec(m_sidebarTree->mapToGlobal(pos));
                    if (!picked) return;
                    if (picked == aOpen) openNoteFile(payload);
                    else if (picked == aRename) {
                        // Re-find the (possibly rebuilt) item by path.
                        for (QTreeWidgetItemIterator rit(m_sidebarTree); *rit; ++rit) {
                            if ((*rit)->data(0, Qt::UserRole).toString()
                                    == QLatin1String("meeting")
                                && (*rit)->data(0, Qt::UserRole + 1).toString()
                                    == payload) {
                                m_sidebarTree->editItem(*rit, 0);
                                break;
                            }
                        }
                    }
                    else if (picked == aRemind) promptReminderForNote(payload, itemLabel);
                    else if (picked == aExpPdf) exportNoteTo(payload, /*asPdf=*/true);
                    else if (picked == aExpMd)  exportNoteTo(payload, /*asPdf=*/false);
                    else if (picked == aDelete) {
                        if (payload == m_currentPath) {
                            m_dirty = false; showEmptyPage(); m_currentPath.clear();
                        }
                        // v0.1.97 — atomic move of canonical + backup ring.
                        moveMeetingTreeToTrash(payload, trashFolder());
                        refreshSidebar();
                    }
                } else if (kind == QStringLiteral("trashed_meeting")) {
                    // No "Open" — trash never opens. Restore first.
                    QAction *aRestore = menu.addAction(tr("Restore"));
                    QAction *aPurge   = menu.addAction(tr("Delete permanently"));
                    QAction *picked = menu.exec(m_sidebarTree->mapToGlobal(pos));
                    if (!picked) return;
                    if (picked == aRestore) {
                        restoreMeetingTreeFromTrash(payload, inboxFolder());
                        refreshSidebar();
                    } else if (picked == aPurge) {
                        // A3 — confirm with the display title, never the raw
                        // ".trashed-<ts>-…" filename (same resolver as the
                        // trash leaves themselves).
                        if (noterMessageBox(this, m_pal, QMessageBox::Warning,
                            tr("Delete permanently?"),
                            tr("Permanently delete this meeting?\n\n%1")
                                .arg(m_storage->displayTitleForFile(payload)),
                            QMessageBox::Yes | QMessageBox::No, QMessageBox::No)
                            == QMessageBox::Yes) {
                            permanentlyDeleteMeetingTree(payload);
                            refreshSidebar();
                        }
                    }
                } else if (kind == QStringLiteral("reminder")) {
                    // payload (UserRole+1) = source note; id = UserRole+2.
                    // Capture BOTH before exec — the autosave refresh can
                    // rebuild the tree during the nested loop and free `it`.
                    const QString id = it->data(0, Qt::UserRole + 2).toString();
                    QAction *aOpen   = menu.addAction(tr("Open note"));
                    QAction *aChange = menu.addAction(tr("Change time…"));
                    menu.addSeparator();
                    QAction *aDelete = menu.addAction(tr("Delete reminder"));
                    QAction *picked = menu.exec(m_sidebarTree->mapToGlobal(pos));
                    if (!picked) return;
                    if (picked == aOpen) openNoteFile(payload);
                    else if (picked == aChange) changeReminderTime(id);
                    else if (picked == aDelete) {
                        if (m_todos && !id.isEmpty()) m_todos->deleteRow(id);
                        refreshSidebar();
                    }
                }
            });

    return w;
}

QWidget *NotesPanel::buildEditorPage() {
    auto *w = new QWidget(this);
    w->setObjectName("noterEditorPage");
    w->setStyleSheet(editorPageStyle(m_pal));

    auto *v = new QVBoxLayout(w);
    v->setContentsMargins(0, 0, 0, 0);
    v->setSpacing(0);

    // v0.1.97 — reminder banner (hidden until a reminder fires). Sits
    // ABOVE everything in the editor page so it can't be missed.
    m_reminderBanner = new QWidget(w);
    m_reminderBanner->setObjectName(QStringLiteral("noterReminderBanner"));
    m_reminderBanner->setVisible(false);
    auto *banL = new QHBoxLayout(m_reminderBanner);
    banL->setContentsMargins(14, 8, 14, 8);
    banL->setSpacing(8);
    auto *bell = new QLabel(m_reminderBanner);
    bell->setPixmap(this->style()->standardIcon(QStyle::SP_MessageBoxWarning).pixmap(18, 18));
    banL->addWidget(bell);
    m_reminderLabel = new QLabel(m_reminderBanner);
    m_reminderLabel->setWordWrap(true);
    m_reminderLabel->setStyleSheet(
        QStringLiteral("color: %1; font-weight: 600;").arg(m_pal.bannerFg));
    banL->addWidget(m_reminderLabel, 1);
    m_reminderOpenSrcBtn = new QPushButton(tr("Open"), m_reminderBanner);
    m_reminderSnoozeBtn  = new QPushButton(tr("Snooze 10m"), m_reminderBanner);
    m_reminderDismissBtn = new QPushButton(tr("Dismiss"), m_reminderBanner);
    for (auto *b : {m_reminderOpenSrcBtn, m_reminderSnoozeBtn, m_reminderDismissBtn}) {
        b->setCursor(Qt::PointingHandCursor);
        b->setStyleSheet(QStringLiteral(
            "QPushButton { background: %1; color: white; border: none;"
            "  border-radius: 4px; padding: 4px 12px; font-size: 12px; }"
            "QPushButton:hover { background: %2; }")
                .arg(m_pal.bannerBtnBg, m_pal.bannerBtnHover));
        banL->addWidget(b);
    }
    v->addWidget(m_reminderBanner);
    // Open source meeting for the front-of-queue reminder.
    connect(m_reminderOpenSrcBtn, &QPushButton::clicked, this, [this]() {
        if (m_reminderQueue.isEmpty()) return;
        const QString src = m_reminderQueue.first().sourceFile;
        if (!src.isEmpty()) openNoteFile(src);
    });
    connect(m_reminderSnoozeBtn, &QPushButton::clicked, this, [this]() {
        if (m_reminderQueue.isEmpty()) return;
        const QString id = m_reminderQueue.first().id;
        if (m_todos) m_todos->setReminder(id,
            QDateTime::currentDateTime().addSecs(10 * 60));  // re-arm in 10m
        m_reminderQueue.removeFirst();
        refreshSidebar();
        showNextReminder();
    });
    connect(m_reminderDismissBtn, &QPushButton::clicked, this, [this]() {
        if (m_reminderQueue.isEmpty()) return;
        const QString id = m_reminderQueue.first().id;
        if (m_todos) m_todos->dismissReminder(id);  // status='dismissed'
        m_reminderQueue.removeFirst();
        refreshSidebar();
        showNextReminder();
    });

    // M2 — save-failure banner (hidden until autosave fails twice in a
    // row). Red sibling of the reminder banner: tells the user their edits
    // exist only in memory and offers the "Save a copy…" escape hatch.
    m_saveFailBanner = new QWidget(w);
    m_saveFailBanner->setObjectName(QStringLiteral("noterSaveFailBanner"));
    m_saveFailBanner->setStyleSheet(QStringLiteral(
        "QWidget#noterSaveFailBanner { background: %1;"
        "  border-bottom: 2px solid %2; }")
            .arg(m_pal.saveFailBg, m_pal.saveFailBorder));
    m_saveFailBanner->setVisible(false);
    auto *sfL = new QHBoxLayout(m_saveFailBanner);
    sfL->setContentsMargins(14, 8, 14, 8);
    sfL->setSpacing(8);
    auto *sfIcon = new QLabel(m_saveFailBanner);
    sfIcon->setPixmap(this->style()->standardIcon(QStyle::SP_MessageBoxCritical).pixmap(18, 18));
    sfL->addWidget(sfIcon);
    m_saveFailLabel = new QLabel(m_saveFailBanner);
    m_saveFailLabel->setWordWrap(true);
    m_saveFailLabel->setStyleSheet(
        QStringLiteral("color: %1; font-weight: 600;").arg(m_pal.saveFailFg));
    sfL->addWidget(m_saveFailLabel, 1);
    auto *sfCopyBtn = new QPushButton(tr("Save a copy…"), m_saveFailBanner);
    sfCopyBtn->setObjectName(QStringLiteral("noterSaveCopyBtn"));
    auto *sfHideBtn = new QPushButton(tr("Hide"), m_saveFailBanner);
    sfHideBtn->setObjectName(QStringLiteral("noterSaveFailHideBtn"));
    for (auto *b : {sfCopyBtn, sfHideBtn}) {
        b->setCursor(Qt::PointingHandCursor);
        b->setStyleSheet(QStringLiteral(
            "QPushButton { background: %1; color: white; border: none;"
            "  border-radius: 4px; padding: 4px 12px; font-size: 12px; }"
            "QPushButton:hover { background: %2; }")
                .arg(m_pal.saveFailBtnBg, m_pal.saveFailBtnHover));
        sfL->addWidget(b);
    }
    v->addWidget(m_saveFailBanner);
    connect(sfCopyBtn, &QPushButton::clicked, this, [this]() { promptSaveCopyAs(); });
    connect(sfHideBtn, &QPushButton::clicked, this, [this]() { hideSaveFailureBanner(); });

    // Top edge — "saved 2s ago" hint, right-aligned, very muted
    auto *topBar = new QWidget(w);
    auto *topLayout = new QHBoxLayout(topBar);
    topLayout->setContentsMargins(0, 6, 56, 0);
    topLayout->addStretch(1);
    m_savedHint = new QLabel(tr("auto-saved"), topBar);
    m_savedHint->setObjectName("noterSavedHint");
    topLayout->addWidget(m_savedHint);
    v->addWidget(topBar);

    // The QTextEdit body — that's the entire surface.
    m_editor = new QTextEdit(w);
    m_editor->setObjectName("noterEditor");
    m_editor->setAcceptRichText(true);
    m_editor->setUndoRedoEnabled(true);
    m_editor->installEventFilter(this);
    m_editor->viewport()->installEventFilter(this);

    // v0.1.98 — compact top toolbar mirroring the right-click insert options
    // as icon buttons (user asked for them "on the top line, each an icon").
    auto *toolbar = new QWidget(w);
    toolbar->setObjectName(QStringLiteral("noterEditorToolbar"));
    auto *tb = new QHBoxLayout(toolbar);
    tb->setContentsMargins(20, 6, 20, 2);
    tb->setSpacing(4);
    auto makeToolBtn = [&](const QIcon &ic, const QString &tip) {
        auto *b = new QToolButton(toolbar);
        b->setIcon(ic);
        b->setIconSize(QSize(18, 18));
        b->setToolTip(tip);
        b->setAutoRaise(true);
        b->setCursor(Qt::PointingHandCursor);
        return b;
    };
    // v0.1.98 — the two presets the user uses most get their OWN badges on the
    // shelf (user-chosen 2026-05-24): Action Items + What I plan. Both insert a
    // named section header + a ☐ bullet.
    auto *aiBtn = makeToolBtn(actionItemsIcon(), tr("Insert “Action Items” section"));
    connect(aiBtn, &QToolButton::clicked, this, [this]() { insertSubheader(tr("Action Items")); });
    tb->addWidget(aiBtn);
    auto *wpBtn = makeToolBtn(whatIPlanIcon(), tr("Insert “What I plan” section"));
    connect(wpBtn, &QToolButton::clicked, this, [this]() { insertSubheader(tr("What I plan")); });
    tb->addWidget(wpBtn);
    auto *tdBtn = makeToolBtn(toDosIcon(), tr("Insert “To-dos” section"));
    connect(tdBtn, &QToolButton::clicked, this, [this]() { insertSubheader(tr("To-dos")); });
    tb->addWidget(tdBtn);
    // Header badge → the remaining named presets (sizes were rejected).
    auto *hdrBtn = makeToolBtn(headerIcon(), tr("More section headers"));
    hdrBtn->setPopupMode(QToolButton::InstantPopup);
    {
        auto *m = new QMenu(hdrBtn);
        m->addAction(tr("Quotes"),    this, [this]() { insertSubheader(tr("Quotes")); });
        m->addAction(tr("Decisions"), this, [this]() { insertSubheader(tr("Decisions")); });
        m->addSeparator();
        m->addAction(tr("Custom…"),   this, [this]() { insertSubheader(QString()); });
        hdrBtn->setMenu(m);
    }
    tb->addWidget(hdrBtn);
    auto *chkBtn = makeToolBtn(checkBadgeIcon(),
                               tr("Insert checkbox — F4 toggles ☐/✓ on the current line"));
    connect(chkBtn, &QToolButton::clicked, this, [this]() { insertCheckboxAtCursor(); });
    tb->addWidget(chkBtn);
    tb->addStretch(1);
    v->addWidget(toolbar, 0);

    v->addWidget(m_editor, 1);
    connect(m_editor, &QTextEdit::textChanged, this, &NotesPanel::onEditorBodyChanged);

    // ─── Footer bar ────────────────────────────────────────────────
    // Hosts the model picker (left) and the Extract button (right).
    // Plain row at the bottom of the editor page — no overlay magic.
    m_editorFooter = new QWidget(w);
    m_editorFooter->setObjectName("noterEditorFooter");
    auto *fl = new QHBoxLayout(m_editorFooter);
    fl->setContentsMargins(20, 8, 20, 10);
    fl->setSpacing(8);

    auto *modelLbl = new QLabel(tr("AI:"), m_editorFooter);
    modelLbl->setObjectName("noterModelLabel");
    fl->addWidget(modelLbl);

    m_modelCombo = new QComboBox(m_editorFooter);
    m_modelCombo->setObjectName("noterModelCombo");
    m_modelCombo->setToolTip(tr("Model used by Extract. Type to filter — handy for "
                                "cloud backends like OpenRouter with hundreds of models. "
                                "Switch backend / set keys in the AI panel."));
    // v0.1.98 — type-to-filter: editable combo + a contains-match completer so
    // a 358-model OpenRouter list is usable (type "claude" / "gemini" to narrow).
    m_modelCombo->setEditable(true);
    m_modelCombo->setInsertPolicy(QComboBox::NoInsert);
    m_modelCombo->setMaxVisibleItems(18);
    if (QCompleter *c = m_modelCombo->completer()) {
        c->setCompletionMode(QCompleter::PopupCompletion);
        c->setFilterMode(Qt::MatchContains);
        c->setCaseSensitivity(Qt::CaseInsensitive);
    }
    // Seed with the persisted choice so it's visible even before listModels
    // round-trips.
    if (!Config::instance().aiNoterModel.isEmpty()) {
        m_modelCombo->addItem(Config::instance().aiNoterModel);
    } else {
        m_modelCombo->addItem(QStringLiteral("(loading…)"));
    }
    fl->addWidget(m_modelCombo, 1);
    // Persist on change — but only when the text is a REAL listed model (so
    // partial typing while filtering, or a placeholder, never gets saved).
    connect(m_modelCombo, QOverload<const QString &>::of(&QComboBox::currentTextChanged),
            this, [this](const QString &model) {
                if (model.isEmpty() || model.startsWith(QLatin1Char('('))) return;
                if (m_modelCombo->findText(model) < 0) return;  // not a real model yet
                Config::instance().aiNoterModel = model;
                Config::instance().save();
            });
    // Refresh button — re-list models from the current backend on demand, and
    // its tooltip tells the user where to change the backend / keys.
    auto *refreshBtn = new QToolButton(m_editorFooter);
    refreshBtn->setObjectName(QStringLiteral("noterModelRefresh"));
    refreshBtn->setIcon(style()->standardIcon(QStyle::SP_BrowserReload));
    refreshBtn->setAutoRaise(true);
    refreshBtn->setCursor(Qt::PointingHandCursor);
    refreshBtn->setToolTip(tr("Refresh the model list.\nBackend + API keys are set in the AI panel "
                              "(its Settings/gear button) — switch to Ollama / OpenRouter / llama.cpp there."));
    connect(refreshBtn, &QToolButton::clicked, this, [this]() { refreshNoterModels(); });
    fl->addWidget(refreshBtn, 0);
    // Populate from the backend now AND on every showEvent (see
    // refreshNoterModels) so switching the backend in the AI panel is
    // reflected here instead of the dropdown going stale at startup.
    refreshNoterModels();

    fl->addStretch(1);

    m_extractBtn = new QPushButton(tr("Extract"), m_editorFooter);
    m_extractBtn->setObjectName("noterExtractBtn");
    m_extractBtn->setIcon(sparkleIcon());
    m_extractBtn->setIconSize(QSize(16, 16));
    m_extractBtn->setCursor(Qt::PointingHandCursor);
    m_extractBtn->setToolTip(tr("AI: extract action items from this note (Ctrl+Alt+E)"));
    m_extractBtn->setMinimumWidth(108);
    connect(m_extractBtn, &QPushButton::clicked, this, &NotesPanel::onExtractClicked);
    fl->addWidget(m_extractBtn);

    v->addWidget(m_editorFooter, 0);
    return w;
}

QWidget *NotesPanel::buildEmptyPage() {
    auto *w = new QWidget(this);
    w->setObjectName("noterEmptyPage");
    w->setStyleSheet(emptyPageStyle(m_pal));

    auto *outer = new QVBoxLayout(w);
    outer->addStretch(1);

    auto *title = new QLabel(tr("Noter"), w);
    title->setObjectName("noterEmptyTitle");
    title->setAlignment(Qt::AlignCenter);
    outer->addWidget(title);

    auto *sub = new QLabel(tr("Your meetings, your typing. AI helps after — not during."), w);
    sub->setObjectName("noterEmptySub");
    sub->setAlignment(Qt::AlignCenter);
    outer->addWidget(sub);

    outer->addSpacing(28);

    auto *hint = new QLabel(
        tr("Press <b style='color:%1'>Ctrl+Alt+M</b> to start a new meeting note.")
            .arg(m_pal.accent), w);
    hint->setObjectName("noterEmptyHint");
    hint->setAlignment(Qt::AlignCenter);
    hint->setTextFormat(Qt::RichText);
    auto *hintWrap = new QHBoxLayout;
    hintWrap->addStretch(1);
    hintWrap->addWidget(hint);
    hintWrap->addStretch(1);
    outer->addLayout(hintWrap);

    outer->addSpacing(28);

    auto *negs = new QLabel(
        tr("Noter does not record audio.<br>"
           "Noter does not join calls.<br>"
           "Noter does not need the internet."), w);
    negs->setObjectName("noterEmptyNegs");
    negs->setAlignment(Qt::AlignCenter);
    negs->setTextFormat(Qt::RichText);
    outer->addWidget(negs);

    outer->addStretch(2);
    return w;
}

// ═══════════════════════════════════════════════════════════════════════
//  A5 — theme parity
// ═══════════════════════════════════════════════════════════════════════
//
// Re-issues every chrome stylesheet / palette / per-item foreground from
// the NoterPalette for `themeName`. Chrome only: the note DOCUMENT keeps
// its own in-file styling — the editor viewport background + default ink
// come from the page QSS, and the checklist done/undone char formats are
// re-derived (they are runtime-only; the sanitizer strips them on save).

void NotesPanel::onThemeChanged() {
    applyNoterTheme(Config::instance().theme);
}

void NotesPanel::applyNoterTheme(const QString &themeName) {
    m_pal = noterPaletteForTheme(themeName);

    // ── chrome stylesheets ──────────────────────────────────────────
    if (m_splitter)
        m_splitter->setStyleSheet(QStringLiteral(
            "QSplitter::handle { background: %1; }").arg(m_pal.sidebarBorder));
    if (m_sidebar) m_sidebar->setStyleSheet(sidebarStyle(m_pal));
    if (m_sidebarRestoreBtn)
        m_sidebarRestoreBtn->setStyleSheet(sidebarRestoreStyle(m_pal));
    if (m_sidebarTree) {
        // Mirror buildSidebar's belt-and-braces palette pin (macOS
        // item-view-viewport bug) — both mechanisms must agree.
        QPalette tp = m_sidebarTree->palette();
        tp.setColor(QPalette::Base, QColor(m_pal.sidebarBg));
        tp.setColor(QPalette::Text, QColor(m_pal.text));
        m_sidebarTree->setPalette(tp);
    }
    if (m_searchStatus)
        m_searchStatus->setStyleSheet(QStringLiteral(
            "color:%1; font-size:11px; padding:0 14px 4px;").arg(m_pal.mutedText));
    if (m_editorPage) m_editorPage->setStyleSheet(editorPageStyle(m_pal));
    if (m_emptyPage) {
        m_emptyPage->setStyleSheet(emptyPageStyle(m_pal));
        // The keyboard-hint label bakes the accent into rich text.
        if (auto *hint = m_emptyPage->findChild<QLabel *>(
                QStringLiteral("noterEmptyHint")))
            hint->setText(tr("Press <b style='color:%1'>Ctrl+Alt+M</b> "
                             "to start a new meeting note.").arg(m_pal.accent));
    }

    // ── reminder banner (label + buttons + live flash bg) ───────────
    if (m_reminderLabel)
        m_reminderLabel->setStyleSheet(QStringLiteral(
            "color: %1; font-weight: 600;").arg(m_pal.bannerFg));
    for (auto *b : {m_reminderOpenSrcBtn, m_reminderSnoozeBtn,
                    m_reminderDismissBtn}) {
        if (!b) continue;
        b->setStyleSheet(QStringLiteral(
            "QPushButton { background: %1; color: white; border: none;"
            "  border-radius: 4px; padding: 4px 12px; font-size: 12px; }"
            "QPushButton:hover { background: %2; }")
                .arg(m_pal.bannerBtnBg, m_pal.bannerBtnHover));
    }
    if (m_reminderBanner && m_reminderBanner->isVisible())
        m_reminderBanner->setStyleSheet(QStringLiteral(
            "QWidget#noterReminderBanner { background: %1;"
            "  border-bottom: 2px solid %2; }")
                .arg(m_reminderFlashOn ? m_pal.bannerFlashA : m_pal.bannerFlashB,
                     m_pal.bannerBorder));

    // ── save-failure banner ─────────────────────────────────────────
    if (m_saveFailBanner) {
        m_saveFailBanner->setStyleSheet(QStringLiteral(
            "QWidget#noterSaveFailBanner { background: %1;"
            "  border-bottom: 2px solid %2; }")
                .arg(m_pal.saveFailBg, m_pal.saveFailBorder));
        for (QPushButton *b : m_saveFailBanner->findChildren<QPushButton *>())
            b->setStyleSheet(QStringLiteral(
                "QPushButton { background: %1; color: white; border: none;"
                "  border-radius: 4px; padding: 4px 12px; font-size: 12px; }"
                "QPushButton:hover { background: %2; }")
                    .arg(m_pal.saveFailBtnBg, m_pal.saveFailBtnHover));
    }
    if (m_saveFailLabel)
        m_saveFailLabel->setStyleSheet(QStringLiteral(
            "color: %1; font-weight: 600;").arg(m_pal.saveFailFg));

    // ── savedHint — keep an active failure/conflict state red ───────
    // Normal state has an EMPTY stylesheet (inherits the page QSS hint
    // grey) — only re-issue when a failure style is currently applied.
    if (m_savedHint && !m_savedHint->styleSheet().isEmpty())
        m_savedHint->setStyleSheet(QStringLiteral(
            "color: %1; font-weight: 600; padding-right: 8px;").arg(m_pal.danger));

    // ── checklist colours follow the theme WITHOUT a document edit ──
    // Undone (☐) lines carry NO explicit foreground, so they inherit the
    // editor's themed default ink (editorPageStyle's `color:`, re-issued
    // above) and re-tint automatically with the QSS swap. Done (✓) lines
    // keep their baked muted+strike format from load/toggle. We must NOT
    // re-merge those formats here: the live theme switch runs IN PLACE (no
    // reload, so the load path's clearUndoRedoStacks never compensates) and
    // mergeCharFormat is an undo-recorded QTextDocument edit — it would
    // truncate the user's redo stack and push junk undo entries on every
    // Light↔Dark↔Monokai switch. blockSignals gates Qt SIGNALS, not the
    // undo journal. A done line's mid-grey lagging until the next reload is
    // cosmetic and always legible; a corrupted undo journal is not.

    // ── sidebar item foregrounds (muted sections, overdue red) ──────
    refreshSidebar();

    // ── live pop-out chrome ─────────────────────────────────────────
    if (m_popOut) m_popOut->applyNoterPalette(m_pal);
}

// ═══════════════════════════════════════════════════════════════════════
//  Folder helpers
// ═══════════════════════════════════════════════════════════════════════

QString NotesPanel::defaultNotesFolder() {
    const QString docs =
        QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation);
    return docs + QStringLiteral("/Notepatra/Noter");
}

QString NotesPanel::todosDbPath() {
    return defaultNotesFolder() + QStringLiteral("/.notepatra/todos.db");
}

QString NotesPanel::notesRoot() const { return defaultNotesFolder(); }
QString NotesPanel::inboxFolder() const { return notesRoot() + QStringLiteral("/Inbox"); }
QString NotesPanel::trashFolder() const { return notesRoot() + QStringLiteral("/Trash"); }
QString NotesPanel::todosChecklistPath() const {
    return inboxFolder() + QStringLiteral("/quick-todos.html");
}

void NotesPanel::ensureNotesFolder() {
    QDir().mkpath(inboxFolder());
    QDir().mkpath(trashFolder());
    QDir().mkpath(notesRoot() + QStringLiteral("/.notepatra"));
}

QString NotesPanel::slugifyTitle(const QString &title) const {
    return NotesStorage::safeFilename(title);
}

// ═══════════════════════════════════════════════════════════════════════
//  Sidebar refresh
// ═══════════════════════════════════════════════════════════════════════

// v0.1.97 — refreshSidebar populates the three-root QTreeWidget.
// Implementation legacy from the QListWidget shape was deleted here; the
// new populate-meetings / populate-todos / populate-trash helpers below
// produce the same logical content, just nested under their roots.
void NotesPanel::refreshSidebar() {
    if (!m_sidebarTree) return;
    // v0.1.97 — gate itemChanged signal so it doesn't fire while we
    // create / setText items during populate.
    m_loadingTree = true;
    const QString filter = m_search ? m_search->text().trimmed().toLower() : QString();

    // Preserve expand state by stable text-key. Counts change on every
    // refresh (e.g. "Meetings (5)" → "Meetings (4)") so we strip the
    // "  (N)" suffix before comparing. Without this, deleting any item
    // collapses every other expanded root/section — bad UX.
    auto stripCount = [](const QString &s) {
        const int idx = s.indexOf(QStringLiteral("  ("));
        return idx >= 0 ? s.left(idx) : s;
    };
    QSet<QString> wasExpanded;
    for (int i = 0; i < m_sidebarTree->topLevelItemCount(); ++i) {
        auto *root = m_sidebarTree->topLevelItem(i);
        const QString rk = stripCount(root->text(0));
        if (root->isExpanded()) wasExpanded.insert(rk);
        for (int j = 0; j < root->childCount(); ++j) {
            auto *child = root->child(j);
            if (child->isExpanded())
                wasExpanded.insert(rk + "/" + stripCount(child->text(0)));
        }
    }

    // v0.1.112 — filter-session expand-state machine. Entering a search
    // snapshots the user's REAL expand state exactly once; leaving restores
    // it verbatim. Mid-search refreshes (autosave, rename) hit the
    // active→active branch: snapshot untouched, force-expand re-applies.
    const bool filterActive = !filter.isEmpty();
    const bool justCleared  = !filterActive && m_filterWasActive;
    if (filterActive && !m_filterWasActive)
        m_preFilterExpanded = wasExpanded;
    else if (justCleared)
        wasExpanded = m_preFilterExpanded;
    m_filterWasActive = filterActive;

    m_sidebarTree->clear();

    // v0.1.97 — three-root tree with QStyle::SP_* icons (no emoji tofu)
    // and auto-collapsed roots so first launch isn't a wall of children.
    auto mkRoot = [&](const QString &iconKind, const QString &name) {
        auto *r = new QTreeWidgetItem(m_sidebarTree);
        r->setText(0, name);
        r->setIcon(0, iconForRoot(this->style(), iconKind));
        r->setData(0, Qt::UserRole, QStringLiteral("root"));
        QFont f = r->font(0);
        f.setBold(true);
        r->setFont(0, f);
        // Default-collapsed for ALL three roots on the very first load.
        // After that, restore the user's expand state across refreshes
        // by stripping the count from the comparison key.
        r->setExpanded(wasExpanded.contains(stripCount(r->text(0))));
        return r;
    };

    // v0.1.98 — todos dropped: only Notes + Trash roots now. The "meetings"
    // icon kind is reused for the Notes root.
    auto *notesRoot     = mkRoot(QStringLiteral("meetings"),  tr("Notes"));
    auto *remindersRoot = mkRoot(QStringLiteral("reminders"), tr("Reminders"));
    auto *trashRoot     = mkRoot(QStringLiteral("trash"),     tr("Trash"));

    populateMeetingsRoot(notesRoot, filter);
    populateRemindersRoot(remindersRoot, filter);
    populateTrashRoot(trashRoot, filter);

    // Restore second-level expand state via stable count-stripped keys.
    for (int i = 0; i < m_sidebarTree->topLevelItemCount(); ++i) {
        auto *root = m_sidebarTree->topLevelItem(i);
        const QString rk = stripCount(root->text(0));
        for (int j = 0; j < root->childCount(); ++j) {
            auto *child = root->child(j);
            if (wasExpanded.contains(rk + "/" + stripCount(child->text(0))))
                child->setExpanded(true);
        }
    }

    // v0.1.112 — while a filter is live, auto-expand every root/section that
    // holds a match (a hit hidden under a collapsed section reads as "search
    // is broken") and surface the match count. Sections are only created
    // non-empty under a filter (meetings pre-filtered before bucketing,
    // Trash deletes its empty section, Reminders skips empty buckets), so
    // summing depth-3 leaves is the match count.
    if (filterActive) {
        int matches = 0;
        for (int i = 0; i < m_sidebarTree->topLevelItemCount(); ++i) {
            auto *rootItem = m_sidebarTree->topLevelItem(i);
            int rootLeaves = 0;
            for (int j = 0; j < rootItem->childCount(); ++j) {
                auto *sect = rootItem->child(j);
                rootLeaves += sect->childCount();
                sect->setExpanded(sect->childCount() > 0);
            }
            rootItem->setExpanded(rootLeaves > 0);   // empty root collapsed = "nothing here"
            matches += rootLeaves;
        }
        if (m_searchStatus) {
            m_searchStatus->setText(matches > 0 ? tr("%n match(es)", "", matches)
                                                : tr("No matches"));
            m_searchStatus->show();
        }
    } else if (m_searchStatus) {
        m_searchStatus->hide();
    }
    // v0.1.112 — leaving a filter session restores the snapshot VERBATIM.
    // populateMeetingsRoot's setCurrentItem fires QTreeView::scrollTo while
    // the panel is visible, which auto-expands the current leaf's ancestors;
    // enforce the pre-search shape over that side effect on the one refresh
    // that exits the session. Ordinary refreshes are untouched.
    if (justCleared) {
        for (int i = 0; i < m_sidebarTree->topLevelItemCount(); ++i) {
            auto *rootItem = m_sidebarTree->topLevelItem(i);
            const QString rk = stripCount(rootItem->text(0));
            rootItem->setExpanded(wasExpanded.contains(rk));
            for (int j = 0; j < rootItem->childCount(); ++j) {
                auto *sect = rootItem->child(j);
                sect->setExpanded(
                    wasExpanded.contains(rk + "/" + stripCount(sect->text(0))));
            }
        }
    }
    m_loadingTree = false;
}

// User-requested 2026-05-24: auto-collapse the left sidebar each time
// Noter becomes visible. It opens tidy — every section label (Meetings /
// Todos / Trash) visible at a glance — instead of a wall of expanded
// meetings or 23 trashed rows. The user expands whichever section they
// want; the choice is preserved across data refreshes within the session,
// and re-collapsed the next time they switch back to the Noter tab.
void NotesPanel::showEvent(QShowEvent *event) {
    QWidget::showEvent(event);
    // v0.1.98 — re-fetch the AI model list from the CURRENT backend whenever
    // the Noter tab is shown. The dropdown used to list once at startup and go
    // stale when the backend changed in the AI panel (user: "dropdown not
    // working" after switching to OpenRouter). Cheap; keeps it in sync.
    refreshNoterModels();
    if (!m_sidebarTree) return;
    // v0.1.112 — a LIVE search's results must survive a tab switch; the
    // tidy-collapse preference (user 2026-05-24) applies only when no
    // filter is active. Normal collapse resumes when the box clears.
    if (m_search && !m_search->text().trimmed().isEmpty()) return;
    for (int i = 0; i < m_sidebarTree->topLevelItemCount(); ++i) {
        auto *root = m_sidebarTree->topLevelItem(i);
        root->setExpanded(false);
        for (int j = 0; j < root->childCount(); ++j)
            root->child(j)->setExpanded(false);
    }
}

void NotesPanel::refreshNoterModels() {
    if (!m_modelCombo) return;
    if (!m_modelListClient) {
        m_modelListClient = new OllamaClient(this);
        connect(m_modelListClient, &OllamaClient::modelsListed, this,
                [this](const QStringList &models) {
                    if (!m_modelCombo) return;
                    const QString pick = Config::instance().aiNoterModel;
                    m_modelCombo->blockSignals(true);
                    m_modelCombo->clear();
                    if (models.isEmpty()) {
                        m_modelCombo->addItem(tr("(no models — check AI panel backend)"));
                    } else {
                        m_modelCombo->addItems(models);
                        if (!pick.isEmpty() && models.contains(pick)) {
                            m_modelCombo->setCurrentText(pick);
                        } else {
                            m_modelCombo->setCurrentIndex(0);
                            Config::instance().aiNoterModel = m_modelCombo->currentText();
                            Config::instance().save();
                        }
                    }
                    m_modelCombo->blockSignals(false);
                });
        connect(m_modelListClient, &OllamaClient::modelsError, this,
                [this](const QString &) {
                    if (!m_modelCombo) return;
                    m_modelCombo->blockSignals(true);
                    m_modelCombo->clear();
                    m_modelCombo->addItem(tr("(no models — check AI panel backend)"));
                    m_modelCombo->blockSignals(false);
                });
    }
    const auto &cfg = Config::instance();
    m_modelListClient->setBackend(OllamaClient::backendFromString(
        cfg.aiBackend.isEmpty() ? QStringLiteral("Ollama") : cfg.aiBackend));
    QString url = cfg.aiBaseUrl;
    if (url.isEmpty())
        url = (cfg.aiBackend == QStringLiteral("llama.cpp"))
                  ? QStringLiteral("http://localhost:8080")
                  : QStringLiteral("http://localhost:11434");
    m_modelListClient->setBaseUrl(url);
    m_modelListClient->listModels();
}

// A3 — the filename prettifier moved to NotesStorage::prettyTitleFromFilename;
// every label now reads the title RESOLVER (m_storage->displayTitleForFile):
// notepatra-title meta first, legacy heuristic / filename-pretty fallback.

// v0.1.112 — search-plaintext for one note, served from the (mtime, size)-
// keyed in-memory cache; cold misses read the file once. NEVER touches the
// note (plain read), never blocks beyond one bounded read.
QString NotesPanel::bodyTextFor(const QFileInfo &fi) {
    const QString key = fi.absoluteFilePath();
    if (m_bodyCache.size() > 2048) m_bodyCache.clear();  // bound renamed/trashed strays
    auto it = m_bodyCache.constFind(key);
    if (it != m_bodyCache.constEnd() &&
        it->mtime == fi.lastModified() && it->size == fi.size())
        return it->plain;
    QFile f(key);
    if (!f.open(QIODevice::ReadOnly)) return QString();
    // Defensive cap: notes are tens of KB; never slurp a pathological file
    // a sync tool dropped in the Inbox.
    const QString html = QString::fromUtf8(f.read(2 * 1024 * 1024));
    BodySearchEntry e{ fi.lastModified(), fi.size(),
                       NotesStorage::plainTextForSearch(html) };
    m_bodyCache.insert(key, e);
    return e.plain;
}

// v0.1.112 — chunked idle prewarm of the body-search cache: ≤25 files per
// 16ms tick so a large Inbox never janks the UI. Re-arms itself via a
// receiver-context singleShot (cancelled automatically on destruction).
void NotesPanel::prewarmBodyCache() {
    int n = 0;
    QDir inbox(inboxFolder());
    while (!m_prewarmQueue.isEmpty() && n < 25) {
        const QString name = m_prewarmQueue.takeFirst();
        if (name == QStringLiteral("quick-todos.html")) continue;
        bodyTextFor(QFileInfo(inbox.absoluteFilePath(name)));
        ++n;
    }
    if (!m_prewarmQueue.isEmpty())
        QTimer::singleShot(16, this, [this] { prewarmBodyCache(); });
}

void NotesPanel::populateMeetingsRoot(QTreeWidgetItem *root, const QString &filter) {
    QDir d(inboxFolder());
    QFileInfoList all = d.entryInfoList(QStringList() << QStringLiteral("*.html"),
                                        QDir::Files, QDir::Time);

    // v0.1.112 — decorate-sort by CREATION time (filename stamp, mtime
    // fallback) so each stamp is parsed once, not O(n log n) times in a
    // comparator. stable_sort keeps QDir::Time (mtime desc) as tiebreak.
    QVector<QPair<QDateTime, QFileInfo>> dated;
    dated.reserve(all.size());
    for (const QFileInfo &afi : all) dated.append({creationTimeOf(afi), afi});
    std::stable_sort(dated.begin(), dated.end(),
                     [](const QPair<QDateTime, QFileInfo> &a,
                        const QPair<QDateTime, QFileInfo> &b) { return a.first > b.first; });

    // v0.1.112 — AND-of-terms matching over title-OR-body. Terms arrive
    // pre-lowercased; matchSnippets carries body-hit context to the leaf
    // TOOLTIP only (leaf text is load-bearing: the rename handler slugs it
    // into the filename, and uniformRowHeights bans multi-line rows).
    const QStringList terms = filter.split(QLatin1Char(' '), Qt::SkipEmptyParts);
    QHash<QString, QString> matchSnippets;   // absPath -> context snippet

    // Group meetings by date-bucket using a parallel map.
    QMap<QString, QList<QFileInfo>> buckets;  // bucket -> files
    QStringList bucketOrder;                  // insertion order
    for (const auto &df : dated) {
        const QFileInfo &fi = df.second;
        const QString name = fi.fileName();
        // The Todos checklist store lives in the Inbox too, but it is NOT a
        // meeting — it has its own Todos root + editable checklist view.
        // Without this it showed up as a bogus "quick todos" meeting leaf.
        if (name == QStringLiteral("quick-todos.html")) continue;
        if (name.endsWith(QStringLiteral(".bak1")) ||
            name.endsWith(QStringLiteral(".bak2")) ||
            name.endsWith(QStringLiteral(".bak3")) ||
            name.endsWith(QStringLiteral(".bak4")) ||
            name.endsWith(QStringLiteral(".bak5")) ||
            name.endsWith(QStringLiteral(".draft")) ||
            name.endsWith(QStringLiteral(".lock")) ||
            name.endsWith(QStringLiteral(".tmp"))) continue;

        // A3 — the DISPLAY title comes from the resolver (notepatra-title
        // meta first; legacy heuristic / filename-pretty fallback), used
        // for BOTH the search filter and the leaf text below.
        const QString display =
            m_storage->displayTitleForFile(fi.absoluteFilePath());
        // Legacy title haystack — KEPT BYTE-IDENTICAL (retrieval-spec hard
        // rule): its 4-digit-only prefix regex leaves the raw stamp of
        // 6-digit files in the haystack, so a search for "2026" keeps
        // matching today's notes. The resolver display above is an
        // ADDITIONAL match path (Unicode/meta titles), never a
        // replacement for this one.
        QString legacyHaystack = fi.completeBaseName();
        const QRegExp datePrefix(QStringLiteral("^\\d{4}-\\d{2}-\\d{2}-\\d{4}-"));
        legacyHaystack.remove(datePrefix);
        legacyHaystack.replace(QChar('-'), QChar(' '));
        if (legacyHaystack.isEmpty()) legacyHaystack = fi.completeBaseName();
        // v0.1.112 — AND-of-terms over title-OR-body. Strict superset of the
        // old single display.contains(filter): a title containing the whole
        // phrase contains every term, so everything that matched before
        // still matches. Body is read lazily — a title-only hit costs zero
        // disk reads. Filter chars are literal (contains/indexOf, never
        // regex). Body search reads last-saved disk state; the 5s autosave
        // bounds staleness — accepted, no dirty-buffer special case.
        if (!terms.isEmpty()) {
            const QString displayLower = display.toLower();
            const QString legacyLower  = legacyHaystack.toLower();
            QString body;
            bool bodyLoaded = false;
            int firstHit = -1, firstHitLen = 0;
            bool allMatch = true;
            for (const QString &t : terms) {
                if (displayLower.contains(t) ||
                    legacyLower.contains(t)) continue;           // title hit — no disk read
                if (!bodyLoaded) { body = bodyTextFor(fi); bodyLoaded = true; }
                const int idx = body.indexOf(t, 0, Qt::CaseInsensitive);
                if (idx < 0) { allMatch = false; break; }
                if (firstHit < 0) { firstHit = idx; firstHitLen = t.size(); }
            }
            if (!allMatch) continue;
            if (firstHit >= 0) {
                const int from = qMax(0, firstHit - 40);
                const int len  = qMin(firstHitLen + 80, int(body.size()) - from);
                matchSnippets.insert(fi.absoluteFilePath(),
                    (from > 0 ? QStringLiteral("…") : QString())
                    + body.mid(from, len).trimmed()
                    + (from + len < body.size() ? QStringLiteral("…") : QString()));
            }
        }

        const QString bucket = recencyBucket(df.first);
        if (!buckets.contains(bucket)) bucketOrder << bucket;
        buckets[bucket].append(fi);
    }

    // Emit a section per non-empty bucket. No bucket-icons (the section
    // labels are self-describing); the leaf icons mark meetings.
    for (const QString &b : bucketOrder) {
        auto *sect = new QTreeWidgetItem(root);
        sect->setText(0, QStringLiteral("%1  (%2)").arg(b, QString::number(buckets[b].size())));
        sect->setData(0, Qt::UserRole, QStringLiteral("section"));
        sect->setForeground(0, QColor(m_pal.mutedText));
        for (const QFileInfo &fi : buckets[b]) {
            // A3 — leaf text reads the title RESOLVER (cache hit — the
            // filter loop above already resolved this file): notepatra-title
            // meta first, legacy heuristic / filename-pretty fallback.
            // Shared with the pop-out titlebar so they can never disagree.
            const QString display =
                m_storage->displayTitleForFile(fi.absoluteFilePath());

            // v0.1.97 — NATIVE item (no setItemWidget). Click-to-open,
            // double-click rename, F2 all work. The pencil + ✕ buttons
            // are painted by NoterRowDelegate and its editorEvent routes
            // button clicks to onAction.
            auto *leaf = new QTreeWidgetItem(sect);
            leaf->setText(0, display);
            leaf->setIcon(0, iconForLeaf(this->style(), QStringLiteral("meeting")));
            leaf->setData(0, Qt::UserRole, QStringLiteral("meeting"));
            leaf->setData(0, Qt::UserRole + 1, fi.absoluteFilePath());
            // v0.1.112 — body-hit context goes in the TOOLTIP only. Leaf
            // text must stay the display title (the rename handler slugs
            // leaf text into the filename; uniformRowHeights bans rows
            // growing a second line).
            QString tip = QDir::toNativeSeparators(fi.absoluteFilePath());
            const QString snip = matchSnippets.value(fi.absoluteFilePath());
            if (!snip.isEmpty())
                tip += QStringLiteral("\n\n") + tr("Match: %1").arg(snip);
            tip += QStringLiteral("\n\nClick to open · pencil to rename · ✕ to trash");
            leaf->setToolTip(0, tip);
            leaf->setFlags(leaf->flags() | Qt::ItemIsEditable);
            if (fi.absoluteFilePath() == m_currentPath) {
                QFont bf = leaf->font(0); bf.setBold(true); leaf->setFont(0, bf);
                m_sidebarTree->setCurrentItem(leaf);
            }
        }
    }
}

// v0.1.98 — the central Reminders root. Lists EVERY scheduled reminder (the
// note-level ones set via right-click AND the per-action ones scheduled from
// Extract), grouped into Overdue / Today / This week / Later. Each leaf opens
// its source note on click; the inline pencil changes the time and ✕ deletes
// it (handled by NoterRowDelegate → onAction). The meeting-note view is
// untouched — this is purely additive (user: "what we see in the note stays").
void NotesPanel::populateRemindersRoot(QTreeWidgetItem *root, const QString &filter) {
    if (!m_todos) { root->setText(0, QStringLiteral("%1  (0)").arg(tr("Reminders"))); return; }

    // v0.1.112 — multi-word consistency with the meetings root: every term
    // must hit, over the SAME haystack as before (title only, not bodies).
    const QStringList terms = filter.split(QLatin1Char(' '), Qt::SkipEmptyParts);

    const QVector<TodoRow> rows = m_todos->allScheduledReminders();
    const QDateTime now = QDateTime::currentDateTime();
    const QDate today = now.date();

    struct Bucket { QString name; QVector<TodoRow> items; };
    QVector<Bucket> buckets = {
        { tr("Overdue"),    {} },
        { tr("Today"),      {} },
        { tr("This week"),  {} },
        { tr("Later"),      {} },
    };

    int total = 0;
    for (const TodoRow &r : rows) {
        if (!r.reminderAt.isValid()) continue;
        QString title = r.text.isEmpty() ? r.meetingTitle : r.text;
        if (title.isEmpty())   // A3 — pretty fallback, never the raw stem
            title = NotesStorage::prettyTitleFromFilename(r.sourceFile);
        if (!terms.isEmpty() && !matchesAllTerms(title.toLower(), terms)) continue;
        const QDateTime at = r.reminderAt.toLocalTime();
        int b;
        if (at < now)                          b = 0;   // overdue
        else if (at.date() == today)           b = 1;   // today
        else if (at.date() <= today.addDays(7)) b = 2;  // this week
        else                                   b = 3;   // later
        buckets[b].items.append(r);
        ++total;
    }
    root->setText(0, QStringLiteral("%1  (%2)").arg(tr("Reminders")).arg(total));

    for (const Bucket &bk : buckets) {
        if (bk.items.isEmpty()) continue;
        auto *sect = new QTreeWidgetItem(root);
        sect->setText(0, QStringLiteral("%1  (%2)").arg(bk.name).arg(bk.items.size()));
        sect->setData(0, Qt::UserRole, QStringLiteral("section"));
        sect->setForeground(0, QColor(m_pal.mutedText));

        for (const TodoRow &r : bk.items) {
            const QDateTime at = r.reminderAt.toLocalTime();
            QString title = r.text.isEmpty() ? r.meetingTitle : r.text;
            if (title.isEmpty())   // A3 — pretty fallback, never the raw stem
                title = NotesStorage::prettyTitleFromFilename(r.sourceFile);
            // Compact relative time: today→HH:mm, this week→ddd HH:mm, else→MMM d HH:mm.
            QString whenStr;
            if (at.date() == today)            whenStr = at.toString(QStringLiteral("HH:mm"));
            else if (at.date() <= today.addDays(7)) whenStr = at.toString(QStringLiteral("ddd HH:mm"));
            else                               whenStr = at.toString(QStringLiteral("MMM d HH:mm"));

            auto *leaf = new QTreeWidgetItem(sect);
            leaf->setText(0, QStringLiteral("%1   ·   %2").arg(title, whenStr));
            leaf->setIcon(0, iconForLeaf(this->style(), QStringLiteral("reminder")));
            leaf->setData(0, Qt::UserRole, QStringLiteral("reminder"));
            leaf->setData(0, Qt::UserRole + 1, r.sourceFile);   // click-to-open
            leaf->setData(0, Qt::UserRole + 2, r.id);           // change / delete
            leaf->setToolTip(0,
                tr("%1\nReminder: %2\n\nClick to open note · pencil to change time · ✕ to delete")
                    .arg(QDir::toNativeSeparators(r.sourceFile),
                         at.toString(QStringLiteral("ddd MMM d, yyyy  HH:mm"))));
            if (at < now) leaf->setForeground(0, QColor(m_pal.danger));  // overdue → red
        }
    }
}

void NotesPanel::populateTodosRoot(QTreeWidgetItem *root, const QString &filter) {
    if (!m_todos) return;
    const QDateTime now = QDateTime::currentDateTime();
    struct Group { QString name; QVector<TodoRow> rows; };
    QList<Group> groups = {
        { tr("Overdue"),   m_todos->dueGroupOverdue(now) },
        { tr("Today"),     m_todos->dueGroupToday(now) },
        { tr("This week"), m_todos->dueGroupWeek(now) },
        { tr("Someday"),   m_todos->dueGroupSomeday(now) },
        { tr("Done"),      m_todos->dueGroupDone(20) },
    };
    int totalOpen = 0;
    for (const auto &g : groups)
        if (g.name != tr("Done")) totalOpen += g.rows.size();
    // Update the root's label with the open-count summary.
    root->setText(0, QStringLiteral("%1  (%2)").arg(tr("Todos")).arg(totalOpen));

    for (const Group &g : groups) {
        if (g.rows.isEmpty()) continue;
        auto *sect = new QTreeWidgetItem(root);
        sect->setText(0, QStringLiteral("%1  (%2)").arg(g.name, QString::number(g.rows.size())));
        sect->setData(0, Qt::UserRole, QStringLiteral("section"));
        sect->setForeground(0, QColor(m_pal.mutedText));
        // Done group collapsed by default — too noisy.
        sect->setExpanded(g.name != tr("Done"));
        for (const TodoRow &r : g.rows) {
            if (!filter.isEmpty() && !r.text.toLower().contains(filter)) continue;
            auto *leaf = new QTreeWidgetItem(sect);
            QString label = r.text;
            if (r.dueAt.isValid())
                label += QStringLiteral("    [%1]").arg(
                    r.dueAt.toString(QStringLiteral("MMM d")));
            // Native item — delegate paints pencil + ✕.
            leaf->setText(0, label);
            // Icon reflects real status: ☐ for open, ✓ for done — so the
            // sidebar agrees with the checklist editor instead of painting
            // every todo with a green "done" tick.
            const bool done =
                (r.status.compare(QStringLiteral("done"), Qt::CaseInsensitive) == 0);
            leaf->setIcon(0, checkboxIcon(done));
            leaf->setData(0, Qt::UserRole, QStringLiteral("todo"));
            leaf->setData(0, Qt::UserRole + 1, r.id);
            leaf->setData(0, Qt::UserRole + 2, r.sourceFile);
            leaf->setFlags(leaf->flags() | Qt::ItemIsEditable);
            QString tip = r.text;
            if (!r.owner.isEmpty()) tip += QStringLiteral("\nOwner: ") + r.owner;
            if (!r.meetingTitle.isEmpty())
                tip += QStringLiteral("\nFrom: ") + r.meetingTitle;
            if (r.dueAt.isValid())
                tip += QStringLiteral("\nDue: ") +
                       r.dueAt.toString(QStringLiteral("yyyy-MM-dd HH:mm"));
            tip += QStringLiteral("\n\nClick to jump · pencil to rename · ✕ to trash");
            leaf->setToolTip(0, tip);
            if (g.name == tr("Done")) {
                QFont f = leaf->font(0); f.setStrikeOut(true); leaf->setFont(0, f);
                leaf->setForeground(0, QColor(m_pal.mutedText));
            }
        }
        if (sect->childCount() == 0) delete root->takeChild(root->indexOfChild(sect));
    }
}

void NotesPanel::populateTrashRoot(QTreeWidgetItem *root, const QString &filter) {
    // v0.1.112 — multi-word consistency with the meetings root: every term
    // must hit, over the SAME haystack as before (display only, not bodies —
    // trash leaves never open, so body snippets would be unverifiable).
    const QStringList terms = filter.split(QLatin1Char(' '), Qt::SkipEmptyParts);

    // Sub-group 1: trashed meetings — CANONICAL .html files only. The
    // backup ring (.bak1-.bak5) + draft sidecars travel with the
    // canonical when a meeting is trashed (see moveMeetingTreeToTrash)
    // but they shouldn't appear as separate user-visible entries.
    QDir tdir(trashFolder());
    QFileInfoList allTrashed = tdir.entryInfoList(QStringList() << ".trashed-*",
                                                  QDir::Files | QDir::Hidden,
                                                  QDir::Time);
    QFileInfoList trashedCanonical;
    for (const QFileInfo &fi : allTrashed) {
        const QString name = fi.fileName();
        // Strip ".trashed-<ts>-" prefix to get the original name.
        QString orig = name;
        orig.remove(QRegExp(QStringLiteral("^\\.trashed-\\d+-")));
        // Canonical iff it ends in `.html` (NOT .bak1/.bak2/.../.draft/.lock/.tmp).
        if (!orig.endsWith(QStringLiteral(".html"))) continue;
        // Defensive: also skip if the original was a .html.bak1 etc.
        if (orig.contains(QStringLiteral(".html."))) continue;
        trashedCanonical.append(fi);
    }

    // v0.1.98 — todos dropped, so Trash holds only trashed Notes now.
    root->setText(0, QStringLiteral("%1  (%2)").arg(tr("Trash")).arg(trashedCanonical.size()));

    if (!trashedCanonical.isEmpty()) {
        auto *sect = new QTreeWidgetItem(root);
        sect->setText(0, QStringLiteral("%1  (%2)").arg(tr("Trashed notes"))
                                                    .arg(trashedCanonical.size()));
        sect->setData(0, Qt::UserRole, QStringLiteral("section"));
        sect->setForeground(0, QColor(m_pal.mutedText));
        for (const QFileInfo &fi : trashedCanonical) {
            // A3 — trashed leaves read the title RESOLVER too: the
            // notepatra-title meta travels with the trashed file (Unicode
            // titles survive the Trash), and prettyTitleFromFilename's
            // ".trashed-<ts>-" strip covers the legacy fallback. Replaces
            // the third inline prettifier copy.
            const QString display =
                m_storage->displayTitleForFile(fi.absoluteFilePath());
            if (!terms.isEmpty() && !matchesAllTerms(display.toLower(), terms)) continue;
            // Native item — delegate paints ↺ restore + ✕ purge.
            auto *leaf = new QTreeWidgetItem(sect);
            leaf->setText(0, display);
            leaf->setIcon(0, iconForLeaf(this->style(), QStringLiteral("trashed_meeting")));
            QFont mf = leaf->font(0); mf.setItalic(true); leaf->setFont(0, mf);
            leaf->setForeground(0, QColor(m_pal.mutedText));
            leaf->setData(0, Qt::UserRole, QStringLiteral("trashed_meeting"));
            leaf->setData(0, Qt::UserRole + 1, fi.absoluteFilePath());
            leaf->setToolTip(0, QDir::toNativeSeparators(fi.absoluteFilePath()) +
                                "\n\n" + tr("Restore or delete with the buttons on the right · right-click for menu"));
        }
        if (sect->childCount() == 0) delete root->takeChild(root->indexOfChild(sect));
    }
}

// v0.1.98 — shared date+time picker (calendar popup + 5m/15m/30m/1h/tomorrow
// quick-picks). Returns the chosen local time, or an invalid QDateTime if the
// user cancelled. When allowClear is true a "Clear reminder" button is offered;
// hitting it sets *cleared=true and returns invalid so the caller can delete.
QDateTime NotesPanel::pickReminderDateTime(const QString &title,
                                           const QDateTime &initial,
                                           bool allowClear, bool *cleared) {
    if (cleared) *cleared = false;

    QDialog dlg(this);
    dlg.setWindowTitle(tr("Reminder — %1").arg(title));
    dlg.setMinimumWidth(380);
    auto *outer = new QVBoxLayout(&dlg);
    outer->addWidget(new QLabel(tr("Remind me at:"), &dlg));

    const QDateTime init = initial.isValid()
        ? initial.toLocalTime()
        : QDateTime::currentDateTime().addSecs(3600);
    auto *edit = new QDateTimeEdit(init, &dlg);
    edit->setCalendarPopup(true);
    edit->setDisplayFormat(QStringLiteral("ddd  yyyy-MM-dd   HH:mm"));
    edit->setMinimumDateTime(QDateTime(QDate::currentDate(), QTime(0, 0)));
    if (auto *cal = edit->calendarWidget())
        cal->setSelectedDate(init.date());
    outer->addWidget(edit);

    auto *chips = new QWidget(&dlg);
    auto *chipsL = new QHBoxLayout(chips);
    chipsL->setContentsMargins(0, 0, 0, 0);
    chipsL->setSpacing(4);
    auto addMins = [edit](int mins) {
        edit->setDateTime(QDateTime::currentDateTime().addSecs(mins * 60));
    };
    auto *c5  = new QPushButton(tr("5m"),  chips);
    auto *c15 = new QPushButton(tr("15m"), chips);
    auto *c30 = new QPushButton(tr("30m"), chips);
    auto *c1h = new QPushButton(tr("1h"),  chips);
    auto *cTom = new QPushButton(tr("tomorrow 9am"), chips);
    for (auto *b : {c5, c15, c30, c1h, cTom}) {
        b->setStyleSheet(QStringLiteral("QPushButton { padding: 3px 8px; font-size: 11px; }"));
        b->setCursor(Qt::PointingHandCursor);
        chipsL->addWidget(b);
    }
    connect(c5,  &QPushButton::clicked, &dlg, [addMins]() { addMins(5); });
    connect(c15, &QPushButton::clicked, &dlg, [addMins]() { addMins(15); });
    connect(c30, &QPushButton::clicked, &dlg, [addMins]() { addMins(30); });
    connect(c1h, &QPushButton::clicked, &dlg, [addMins]() { addMins(60); });
    connect(cTom, &QPushButton::clicked, &dlg, [edit]() {
        edit->setDateTime(QDateTime(QDate::currentDate().addDays(1), QTime(9, 0)));
    });
    outer->addWidget(chips);

    auto *bb = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dlg);
    QPushButton *clearBtn = allowClear
        ? bb->addButton(tr("Clear reminder"), QDialogButtonBox::DestructiveRole)
        : nullptr;
    connect(bb, &QDialogButtonBox::accepted, &dlg, &QDialog::accept);
    connect(bb, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);
    bool clearedLocal = false;
    if (clearBtn)
        connect(clearBtn, &QPushButton::clicked, &dlg, [&clearedLocal, &dlg]() {
            clearedLocal = true; dlg.accept();
        });
    outer->addWidget(bb);

    // Modal parity — the date picker follows Light/Dark/Monokai.
    applyNoterDialogTheme(&dlg, m_pal);

    if (dlg.exec() != QDialog::Accepted) return QDateTime();   // cancelled
    if (clearedLocal) { if (cleared) *cleared = true; return QDateTime(); }
    return edit->dateTime();
}

// v0.1.98 — Set / change / clear a reminder bound to a note FILE (right-click a
// Noter → Set reminder). Stored via setNoteReminder so the reminder engine
// fires it (tray notification + the editor banner whose "Open" opens this note)
// and it appears under the central Reminders root.
void NotesPanel::promptReminderForNote(const QString &notePath,
                                       const QString &title) {
    if (!m_todos || notePath.isEmpty()) return;
    const QDateTime existing = m_todos->noteReminderAt(notePath);

    bool cleared = false;
    const QDateTime when = pickReminderDateTime(title, existing,
                                                /*allowClear=*/existing.isValid(),
                                                &cleared);
    auto *sb = window() ? window()->findChild<QStatusBar *>() : nullptr;
    if (cleared) {
        m_todos->setNoteReminder(notePath, title, QDateTime());
        refreshSidebar();
        if (sb) sb->showMessage(tr("Reminder cleared"), 3000);
        return;
    }
    if (!when.isValid()) return;   // cancelled
    if (when <= QDateTime::currentDateTime()) {
        noterMessageBox(this, m_pal, QMessageBox::Information,
            tr("Reminder in the past"),
            tr("Pick a time in the future — that moment has already passed."));
        return;
    }
    m_todos->setNoteReminder(notePath, title, when);
    refreshSidebar();
    expandRemindersRoot();
    if (sb) sb->showMessage(tr("Reminder set for %1")
            .arg(when.toString(QStringLiteral("ddd MMM d, HH:mm"))), 4000);
}

// v0.1.98 — change (or clear) an existing reminder row by id; used by the
// central Reminders view (right-click → Change time… and the inline pencil).
// Clearing deletes the row.
void NotesPanel::changeReminderTime(const QString &id) {
    if (!m_todos || id.isEmpty()) return;
    const TodoRow row = m_todos->find(id);
    if (row.id.isEmpty()) return;
    const QString title = !row.text.isEmpty() ? row.text
        : (row.meetingTitle.isEmpty() ? tr("reminder") : row.meetingTitle);

    bool cleared = false;
    const QDateTime when = pickReminderDateTime(title, row.reminderAt,
                                                /*allowClear=*/true, &cleared);
    auto *sb = window() ? window()->findChild<QStatusBar *>() : nullptr;
    if (cleared) {
        m_todos->deleteRow(id);
        refreshSidebar();
        if (sb) sb->showMessage(tr("Reminder deleted"), 3000);
        return;
    }
    if (!when.isValid()) return;   // cancelled
    if (when <= QDateTime::currentDateTime()) {
        noterMessageBox(this, m_pal, QMessageBox::Information,
            tr("Reminder in the past"),
            tr("Pick a time in the future — that moment has already passed."));
        return;
    }
    m_todos->setReminder(id, when);
    refreshSidebar();
    expandRemindersRoot();
    if (sb) sb->showMessage(tr("Reminder updated to %1")
            .arg(when.toString(QStringLiteral("ddd MMM d, HH:mm"))), 4000);
}

// v0.1.98 — expand + scroll to the Reminders root so a just-set reminder is
// visible right away (user: "maybe [show it] when we set it").
void NotesPanel::expandRemindersRoot() {
    if (!m_sidebarTree) return;
    for (int i = 0; i < m_sidebarTree->topLevelItemCount(); ++i) {
        auto *root = m_sidebarTree->topLevelItem(i);
        if (root->text(0).startsWith(tr("Reminders"))) {
            root->setExpanded(true);
            for (int j = 0; j < root->childCount(); ++j)
                root->child(j)->setExpanded(true);
            m_sidebarTree->scrollToItem(root);
            return;
        }
    }
}

// ═══════════════════════════════════════════════════════════════════════
//  State transitions
// ═══════════════════════════════════════════════════════════════════════

void NotesPanel::showEmptyPage() {
    if (!m_rightStack) return;
    m_rightStack->setCurrentWidget(m_emptyPage);
    m_currentPath.clear();
    m_currentTitle.clear();
    m_lastSeenH1.clear();   // A3 — no note, no H1 snapshot
}

void NotesPanel::showEditorPage() {
    if (!m_rightStack || !m_editorPage) return;
    m_rightStack->setCurrentWidget(m_editorPage);
}

// ═══════════════════════════════════════════════════════════════════════
//  Public actions
// ═══════════════════════════════════════════════════════════════════════

void NotesPanel::newMeetingNote() {
    // v0.1.97 — autosave handles preserving the current note; no
    // confirm-on-new prompt. User explicitly asked for the dialog to
    // go away because the editor's 5s autosave timer (Config::
    // autoSaveIntervalSec) already protects unsaved work.
    if (m_dirty && !m_currentPath.isEmpty()) saveCurrentNote();

    // v0.1.97 — zero-padded counter ("Untitled meeting 01", "02", …).
    // Per the user's memory rule feedback_untitled_tab_label_derivation,
    // derive the counter from the MAX visible across Inbox + Trash, not
    // from a session-local counter that resets each launch. Scanning both
    // dirs means deleting #03 and creating new still picks #N+1, never
    // resurrecting #03.
    const QDateTime now = QDateTime::currentDateTime();
    const QString stamp = now.toString(QStringLiteral("yyyy-MM-dd-hhmmss"));
    int maxCounter = 0;
    // v0.1.98 — new notes are "noter-NN"; keep the counter monotonic across
    // the rename by also scanning legacy "untitled-meeting-NN" files, so a
    // fresh Noter never collides with an old Untitled number.
    QRegExp counterRx(QStringLiteral("(?:untitled-meeting|noter)-(\\d+)\\.html$"));
    auto scanDir = [&](const QString &path) {
        QDir d(path);
        for (const QFileInfo &fi : d.entryInfoList(QStringList()
                << QStringLiteral("*untitled-meeting-*.html")
                << QStringLiteral("*noter-*.html")
                << QStringLiteral(".trashed-*-untitled-meeting-*.html")
                << QStringLiteral(".trashed-*-noter-*.html"),
            QDir::Files | QDir::Hidden)) {
            QRegExp rx = counterRx;
            if (rx.indexIn(fi.fileName()) >= 0)
                maxCounter = qMax(maxCounter, rx.cap(1).toInt());
        }
    };
    scanDir(inboxFolder());
    scanDir(trashFolder());

    // A3 — the filename scan above stays as a floor; ALSO scan resolver
    // TITLES (display + legacy body H1) so a number that is still VISIBLE
    // anywhere is never reused: (a) a meta-titled "Noter 06" whose file
    // was renamed, (b) a legacy note whose stale body H1 still reads
    // "Noter 06". Numbers freed by genuine retitles stay reusable.
    // Nuance: a PRE-feature sidebar rename left its stale body H1 in
    // place, so that number stays reserved — slightly stricter than the
    // old "rename frees the counter" behavior; post-feature renames
    // rewrite the H1, so renames free numbers again.
    static const QRegularExpression liveTitleRx(
        QStringLiteral("^(?:Noter|Untitled(?: meeting)?)\\s*0*(\\d{1,3})$"),
        QRegularExpression::CaseInsensitiveOption);
    auto scanTitles = [&](const QString &path) {
        QDir d(path);
        for (const QFileInfo &fi : d.entryInfoList(QStringList()
                << QStringLiteral("*.html")
                << QStringLiteral(".trashed-*.html"),
            QDir::Files | QDir::Hidden)) {
            const QString name = fi.fileName();
            if (name == QStringLiteral("quick-todos.html")) continue;
            // Sidecar guard — the two-pattern glob shouldn't match
            // .bak/.draft/.lock/.tmp names, kept defensively anyway.
            if (name.endsWith(QStringLiteral(".bak1")) ||
                name.endsWith(QStringLiteral(".bak2")) ||
                name.endsWith(QStringLiteral(".bak3")) ||
                name.endsWith(QStringLiteral(".bak4")) ||
                name.endsWith(QStringLiteral(".bak5")) ||
                name.endsWith(QStringLiteral(".draft")) ||
                name.endsWith(QStringLiteral(".lock")) ||
                name.endsWith(QStringLiteral(".tmp"))) continue;
            const NotesStorage::TitleInfo ti =
                m_storage->titleInfoForFile(fi.absoluteFilePath());
            for (const QString &t : { ti.display, ti.legacyH1 }) {
                const QRegularExpressionMatch m = liveTitleRx.match(t);
                if (m.hasMatch())
                    maxCounter = qMax(maxCounter, m.captured(1).toInt());
            }
        }
    };
    scanTitles(inboxFolder());
    scanTitles(trashFolder());
    const int nextCounter = maxCounter + 1;

    // v0.1.112 — the 99-note hard cap + >=90 nag are gone: capture must
    // NEVER block. Past 99 the counter grows to 3+ digits ("Noter 100"):
    // rightJustified(2,'0') pads only below 100, the display regexes
    // (^noter\s*, counterRx (\d+)) are digit-count-agnostic, and the
    // monotonic Inbox+Trash scan above still prevents number reuse.
    // Scale is handled by body search + month buckets, not by refusing input.

    const QString counterStr = QString::number(nextCounter).rightJustified(2, '0');

    QString name = QStringLiteral("%1-noter-%2.html").arg(stamp, counterStr);
    QString abs = QDir(inboxFolder()).absoluteFilePath(name);
    // Defensive collision guard (shouldn't ever fire after the scan but
    // belt-and-braces in case two new-note calls race).
    int dedup = 0;
    while (QFileInfo::exists(abs) && dedup < 100) {
        ++dedup;
        name = QStringLiteral("%1-noter-%2-%3.html")
                   .arg(stamp, counterStr).arg(dedup);
        abs  = QDir(inboxFolder()).absoluteFilePath(name);
    }

    // H1 title in the editor is just "Noter NN" — clean. (The timestamp
    // suffix was removed 2026-05-24: the user found it noisy on every note.
    // Creation time still lives in the filename + the note's saved metadata.)
    const QString defaultTitle = tr("Noter %1").arg(counterStr);
    const QString html = m_storage->newNoteHtml(defaultTitle, now, QStringList());
    m_storage->saveNote(abs, html, nullptr);

    refreshSidebar();
    openNoteFile(abs);
}

void NotesPanel::openNoteFile(const QString &absolutePath) {
    // Re-opening the note that is ALREADY in the editor (reminder banner
    // "Open", reminder-leaf click, context-menu Open) must NOT reload from
    // disk — that reverted up to 5s of unsaved typing (the autosave
    // interval). Just make sure the editor page is front-most.
    if (!absolutePath.isEmpty() && absolutePath == m_currentPath
        && m_rightStack && m_editorPage
        && m_rightStack->currentWidget() == m_editorPage) {
        if (m_editor) m_editor->setFocus();
        return;
    }
    if (m_dirty && !m_currentPath.isEmpty() && m_currentPath != absolutePath) {
        saveCurrentNote();
    }
    renderNoteAtPath(absolutePath);
    showEditorPage();
    refreshSidebar();
}

// v0.1.119 — minimal public wrapper so the MCP bridge (via MainWindow) can
// refresh an open panel after an out-of-band create/append/set-reminder.
void NotesPanel::refreshFromDisk() {
    refreshSidebar();
}

// M2c — the Stay / Discard / Save-a-copy… guard shared by every
// navigate-away path. Returns false when the caller must ABORT (Stay, or a
// cancelled/failed "Save a copy…") so the dirty delta stays in the editor.
bool NotesPanel::confirmLeaveAfterFailedSave() {
    if (!(m_dirty && m_lastSaveFailed)) return true;
    QMessageBox box(this);
    box.setWindowTitle(tr("Unsaved changes"));
    box.setIcon(QMessageBox::Warning);
    box.setText(tr("\"%1\" could not be saved (%2).\n"
                   "Leaving now will discard those changes.")
                    .arg(m_currentTitle.isEmpty() ? tr("This note")
                                                  : m_currentTitle,
                         m_lastSaveError.isEmpty() ? tr("disk write failed")
                                                   : m_lastSaveError));
    QPushButton *stayBtn    = box.addButton(tr("Stay"),
                                            QMessageBox::RejectRole);
    QPushButton *discardBtn = box.addButton(tr("Discard changes"),
                                            QMessageBox::DestructiveRole);
    QPushButton *copyBtn    = box.addButton(tr("Save a copy…"),
                                            QMessageBox::ActionRole);
    Q_UNUSED(discardBtn);
    box.setDefaultButton(stayBtn);
    applyNoterDialogTheme(&box, m_pal);
    box.exec();
    if (box.clickedButton() == stayBtn) return false;
    if (box.clickedButton() == copyBtn && !promptSaveCopyAs())
        return false;   // copy cancelled / failed → stay, delta intact
    return true;
}

void NotesPanel::renderNoteAtPath(const QString &absolutePath) {
    if (absolutePath.isEmpty() || !m_editor) return;

    // M2 (c) — if the open note has an unsaved delta whose last save
    // FAILED, replacing the editor content below would destroy the ONLY
    // copy of those edits. Ask first: Stay / Discard / Save a copy…. The
    // same guard now protects the Todos-checklist nav (openTodosChecklist).
    if (!confirmLeaveAfterFailedSave()) return;

    // M2 (b) — propagate readNote's error channel. A locked/missing file
    // used to open as a BLANK editor still bound to the path: one
    // keystroke + the next autosave tick then OVERWROTE the real file.
    QString readErr;
    const QString html = m_storage->readNote(absolutePath, &readErr);
    if (!readErr.isEmpty()) {
        const QString name = QFileInfo(absolutePath).fileName();
        m_loadingInProgress = true;
        m_editor->blockSignals(true);
        m_editor->setHtml(QStringLiteral(
            "<div style=\"color:%1;font-weight:600;font-size:16px;\">%4</div>"
            "<div style=\"color:%2;margin-top:8px;\">%5</div>"
            "<div style=\"color:%3;margin-top:12px;\">%6</div>")
                .arg(m_pal.danger, m_pal.text, m_pal.hintFg)
                .arg(tr("Could not open %1").arg(name.toHtmlEscaped()),
                     readErr.toHtmlEscaped(),
                     tr("The file on disk was left untouched. Fix its "
                        "permissions (or restore it), then open it again "
                        "from the sidebar.")));
        m_editor->setReadOnly(true);
        m_editor->blockSignals(false);
        m_loadingInProgress = false;
        m_readError = true;
        m_currentPath.clear();           // NEVER bind the unreadable path —
        m_currentIsChecklist = false;    // autosave must not target it
        m_dirty = false;
        // A3 — the resolver's unreadable-file branch returns the PRETTY
        // filename fallback (never the raw stem), and is not cached so a
        // later permission fix self-heals.
        m_currentTitle = m_storage->displayTitleForFile(absolutePath);
        m_lastSeenH1.clear();            // error notice has no user H1
        emit noteTitleChanged(m_currentTitle);
        setSavedHintNormal(QString());   // no save state applies here
        return;
    }
    // A7 — crash recovery. A .draft newer than the .html means the last
    // session died hard (kill -9 / power loss) after the draft cadence
    // wrote it but before any clean save landed. Offer to restore it; a
    // stale or identical draft is silently dropped.
    QString contentHtml = html;
    bool restoredDraft = false;
    {
        const QString draftPath = absolutePath + QStringLiteral(".draft");
        const QFileInfo draftFi(draftPath);
        if (draftFi.exists()) {
            const QFileInfo noteFi(absolutePath);
            QString draftHtml;
            {
                QFile df(draftPath);
                if (df.open(QIODevice::ReadOnly))
                    draftHtml = QString::fromUtf8(df.readAll());
            }
            if (draftFi.lastModified() <= noteFi.lastModified() ||
                draftHtml.trimmed().isEmpty() || draftHtml == html) {
                // Superseded / empty / identical — no user delta in it.
                m_storage->clearDraft(absolutePath);
            } else {
                QMessageBox box(this);
                box.setWindowTitle(tr("Recover unsaved changes?"));
                box.setIcon(QMessageBox::Question);
                box.setText(tr("\"%1\" has unsaved changes from %2 — the app "
                               "closed before they could be saved.\n"
                               "Recover them?")
                                .arg(QFileInfo(absolutePath).completeBaseName(),
                                     relativeTimeString(draftFi.lastModified())));
                QPushButton *restoreBtn =
                    box.addButton(tr("Restore"), QMessageBox::AcceptRole);
                box.addButton(tr("Discard"), QMessageBox::DestructiveRole);
                box.setDefaultButton(restoreBtn);
                applyNoterDialogTheme(&box, m_pal);
                box.exec();
                if (box.clickedButton() == restoreBtn) {
                    contentHtml = draftHtml;
                    restoredDraft = true;   // marked dirty below → autosaves
                    // Draft stays on disk until the save lands (saveNote
                    // clears it) — a crash during THIS session must not
                    // lose the recovered text a second time.
                } else {
                    m_storage->clearDraft(absolutePath);
                }
            }
        }
    }

    m_editor->setReadOnly(false);   // may be leaving an earlier error state
    m_readError = false;
    m_loadingInProgress = true;
    m_editor->blockSignals(true);
    m_editor->setHtml(contentHtml);
    // v0.1.98 — older notes baked a "00:00" meeting-timer line above the
    // title before the template dropped it. Strip it on load so existing
    // notes lose it too (it's gone permanently on the next save).
    {
        QTextBlock first = m_editor->document()->firstBlock();
        if (first.isValid() &&
            first.text().trimmed() == QStringLiteral("00:00")) {
            QTextCursor tc(m_editor->document());
            tc.setPosition(first.position());
            tc.movePosition(QTextCursor::NextBlock, QTextCursor::KeepAnchor);
            tc.removeSelectedText();
        }
    }
    restyleChecklistLines();   // re-apply ✓ strike-through (sanitizer drops it)
    // A3 — the 00:00 strip + restyle above are LOAD-time doc surgery, not
    // user edits: Ctrl+Z must not resurrect what they removed (a revived
    // timer line above the H1 would also confuse the H1 snapshot below).
    m_editor->document()->clearUndoRedoStacks();
    m_editor->blockSignals(false);
    m_loadingInProgress = false;
    m_currentPath = absolutePath;
    m_currentIsChecklist = false;   // a normal note, not the Todos checklist
    // A7 — a restored draft IS an unsaved delta: dirty so the next
    // autosave tick persists it into the .html (which then clears the
    // draft). A plain load is clean.
    m_dirty = restoredDraft;
    // A7 — stamp the on-disk state we just loaded; the conflict guard
    // compares against this before every save.
    recordDiskState(absolutePath);
    // A note loaded cleanly — any earlier failure streak belonged to the
    // previous path. Reset to the normal hint cycle (screen == disk now).
    // Bug2 (A7) — EXCEPT when the navigation flush we just ran rescued the
    // PREVIOUS note to a conflict copy: calling noteSaveSucceeded() here
    // would hide that banner inside the same synchronous stack, so the user
    // would never see the conflict. Reset only the failure streak for this
    // freshly-loaded clean note and set its hint; leave the banner up.
    if (m_conflictNoticePending) {
        m_conflictNoticePending = false;
        m_lastSaveFailed      = false;
        m_saveFailureCount    = 0;
        m_saveFailBannerShown = false;
        m_lastSaveError.clear();
        setSavedHintNormal(tr("auto-saved"));
    } else {
        noteSaveSucceeded();
    }
    if (restoredDraft)
        setSavedHintNormal(tr("recovered draft — auto-saves in 5s"));

    // Move caret to end of body so user can start typing immediately.
    QTextCursor cur = m_editor->textCursor();
    cur.movePosition(QTextCursor::End);
    m_editor->setTextCursor(cur);
    m_editor->setFocus();

    // A3 — m_currentTitle is the DISPLAY title (resolver: meta first,
    // legacy heuristic / filename-pretty fallback), never a raw stem.
    // m_lastSeenH1 snapshots the editor H1 AFTER setHtml + the 00:00
    // strip — saveCurrentNote adopts the H1 only when it CHANGES vs this.
    m_currentTitle = m_storage->displayTitleForFile(absolutePath);
    m_lastSeenH1 = titleFromEditorH1();
    emit noteTitleChanged(m_currentTitle);
}

// ── Todos checklist (editable ☐/✓ view of the standalone todo store) ──
namespace {

struct ChecklistFileBlock { QString id, owner, dueIso, status, text; };

// Parse the <div class="b-act" data-id data-status data-due>text</div>
// blocks out of the raw quick-todos HTML (NOT sanitized — we need the
// data-* attributes intact). Mirrors NotesTodos::parseActionBlocks, kept
// local so notes.cpp doesn't depend on that private parser.
QVector<ChecklistFileBlock> parseChecklistFile(const QString &html) {
    QVector<ChecklistFileBlock> out;
    static const QRegularExpression kOpen(
        QStringLiteral("<div\\b([^>]*\\bclass\\s*=\\s*\"[^\"]*\\bb-act\\b[^\"]*\"[^>]*)>"),
        QRegularExpression::CaseInsensitiveOption);
    static const QRegularExpression kAttr(
        QStringLiteral("\\b([\\w-]+)\\s*=\\s*\"([^\"]*)\""));
    static const QRegularExpression kTag(QStringLiteral("<[^>]*>"));
    auto it = kOpen.globalMatch(html);
    while (it.hasNext()) {
        const auto m = it.next();
        const QString attrs = m.captured(1);
        const int bodyStart = m.capturedEnd();
        const int bodyEnd = html.indexOf(QStringLiteral("</div>"), bodyStart,
                                         Qt::CaseInsensitive);
        if (bodyEnd < 0) continue;
        ChecklistFileBlock b;
        auto ai = kAttr.globalMatch(attrs);
        while (ai.hasNext()) {
            const auto am = ai.next();
            const QString k = am.captured(1).toLower();
            const QString v = am.captured(2);
            if      (k == QLatin1String("data-id"))     b.id     = v;
            else if (k == QLatin1String("data-owner"))  b.owner  = v;
            else if (k == QLatin1String("data-due"))    b.dueIso = v;
            else if (k == QLatin1String("data-status")) b.status = v;
        }
        QString body = html.mid(bodyStart, bodyEnd - bodyStart);
        body.remove(kTag);
        body.replace(QStringLiteral("&lt;"), QStringLiteral("<"));
        body.replace(QStringLiteral("&gt;"), QStringLiteral(">"));
        body.replace(QStringLiteral("&quot;"), QStringLiteral("\""));
        body.replace(QStringLiteral("&amp;"), QStringLiteral("&"));
        b.text = body.trimmed();
        if (b.id.isEmpty() && b.text.isEmpty()) continue;
        out.append(b);
    }
    return out;
}

QString checklistEscape(QString s) {
    s.replace(QLatin1Char('&'), QLatin1String("&amp;"));
    s.replace(QLatin1Char('<'), QLatin1String("&lt;"));
    s.replace(QLatin1Char('>'), QLatin1String("&gt;"));
    return s;
}

} // namespace

void NotesPanel::openTodosChecklist() {
    if (!m_editor) return;
    // Persist any pending edit to the previously-open note first.
    if (m_dirty && !m_currentPath.isEmpty()) saveCurrentNote();
    // M2c — that flush can FAIL (read-only dir / full disk), leaving the
    // ONLY copy of the delta in the editor; the setPlainText below would
    // destroy it with no prompt and no disk copy. Gate on the same
    // Stay/Discard/Save-a-copy guard a note-switch uses in renderNoteAtPath.
    if (!confirmLeaveAfterFailedSave()) return;

    const QString path = todosChecklistPath();
    QString html;
    if (QFile::exists(path)) {
        QFile f(path);
        if (f.open(QIODevice::ReadOnly | QIODevice::Text))
            html = QString::fromUtf8(f.readAll());
    }

    m_checklistBlocks.clear();
    QString body;
    for (const ChecklistFileBlock &b : parseChecklistFile(html)) {
        m_checklistBlocks.append({ b.id, b.owner, b.dueIso, b.status, b.text });
        const bool done =
            (b.status.compare(QStringLiteral("done"), Qt::CaseInsensitive) == 0);
        body += (done ? QStringLiteral("✓ ") : QStringLiteral("☐ "))
                + b.text + QLatin1Char('\n');
    }
    if (body.isEmpty()) body = QStringLiteral("☐ ");  // one empty checkbox to type into

    m_loadingInProgress = true;
    m_editor->blockSignals(true);
    m_editor->setReadOnly(false);   // may be leaving an M2 read-error state
    m_readError = false;
    m_editor->setPlainText(body);
    m_editor->blockSignals(false);
    m_loadingInProgress = false;
    m_currentPath = path;
    m_currentIsChecklist = true;
    m_dirty = false;

    QTextCursor cur = m_editor->textCursor();
    cur.movePosition(QTextCursor::End);
    m_editor->setTextCursor(cur);
    m_editor->setFocus();

    m_currentTitle = tr("Todos");
    // A3 — checklist saves exit saveCurrentNote before the H1-adoption
    // block ever runs; clear the snapshot anyway (belt and braces).
    m_lastSeenH1.clear();
    emit noteTitleChanged(m_currentTitle);
    showEditorPage();
    refreshSidebar();
}

void NotesPanel::saveTodosChecklist() {
    if (!m_editor) return;
    const QStringList lines = m_editor->toPlainText().split(QLatin1Char('\n'));
    QVector<ChecklistBlock> rebuilt;
    QString body = QStringLiteral(
        "<!doctype html>\n<html><head><meta charset=\"utf-8\">"
        "<title>Todos</title></head><body>\n"
        "<h1 class=\"meet-title\">Todos</h1>\n");
    int matchIdx = 0;
    for (const QString &raw : lines) {
        QString line = raw.trimmed();
        if (line.isEmpty()) continue;
        QString status = QStringLiteral("open");
        if (line.startsWith(QStringLiteral("✓"))) {        // ✓ done
            status = QStringLiteral("done");
            line = line.mid(1).trimmed();
        } else if (line.startsWith(QStringLiteral("☐"))) { // ☐ open
            line = line.mid(1).trimmed();
        }
        if (line.isEmpty()) continue;   // a bare checkbox with no text → skip

        ChecklistBlock cb;
        if (matchIdx < m_checklistBlocks.size())
            cb = m_checklistBlocks[matchIdx];   // inherit id / owner from open
        if (cb.id.isEmpty())
            cb.id = QUuid::createUuid().toString(QUuid::WithoutBraces).toLower();
        // Pull the AUTHORITATIVE due from the todos DB so a reindex driven by
        // this save can't clobber a due/reminder set via right-click (which
        // writes SQLite, not the file). Reminder columns are preserved by id.
        QString dueIso;
        if (m_todos) {
            const TodoRow row = m_todos->find(cb.id);
            if (row.dueAt.isValid())
                dueIso = row.dueAt.toUTC().toString(Qt::ISODate);
        }
        if (dueIso.isEmpty()) dueIso = cb.dueIso;   // fall back to file value
        cb.dueIso = dueIso;
        cb.status = status;
        cb.text = line;
        rebuilt.append(cb);
        ++matchIdx;

        const QString ownerAttr = cb.owner.isEmpty() ? QString()
            : QStringLiteral(" data-owner=\"%1\"").arg(checklistEscape(cb.owner));
        const QString dueAttr = dueIso.isEmpty() ? QString()
            : QStringLiteral(" data-due=\"%1\"").arg(dueIso);
        body += QStringLiteral(
                    "<div class=\"b b-act\" data-id=\"%1\" data-status=\"%2\"%3%4>%5</div>\n")
                    .arg(cb.id, status, ownerAttr, dueAttr, checklistEscape(cb.text));
    }
    body += QStringLiteral("</body></html>\n");

    QString err;
    if (!m_storage->saveNote(m_currentPath, body, &err)) {
        fprintf(stderr, "Noter: saveTodosChecklist failed: %s\n", qPrintable(err));
        noteSaveFailed(err);   // M2 — visible failure state, not stderr-only
        return;
    }
    m_checklistBlocks = rebuilt;
    m_dirty = false;
    m_lastSavedAt = QDateTime::currentDateTime();
    noteSaveSucceeded();
    if (m_todos) m_todos->reindexNote(m_currentPath, body);
    refreshSidebar();
    emit noteSaved(m_currentPath);
}

void NotesPanel::saveCurrentNote() {
    if (m_currentPath.isEmpty() || !m_editor) return;
    if (!m_dirty) return;
    if (m_currentIsChecklist) { saveTodosChecklist(); return; }

    // A3 — H1-edit adoption (Bear/Apple-Notes semantics): a CHANGED first
    // H1 becomes the display title at save time (never per keystroke —
    // the 5s autosave is the debounce). An unchanged or deleted H1 leaves
    // the title sticky, so a stale legacy "Noter 01" body H1 can never
    // hijack a renamed note. The filename NEVER changes on this path.
    const QString h1Now = titleFromEditorH1();
    if (!h1Now.isEmpty() && h1Now != m_lastSeenH1) {
        m_currentTitle = h1Now;
        m_lastSeenH1   = h1Now;
        emit noteTitleChanged(m_currentTitle);
        if (m_popOut && m_popOut->notePath() == m_currentPath)
            m_popOut->setDisplayTitle(m_currentTitle);   // live follow
    }
    // The meta is regenerated on EVERY save (QTextEdit::setHtml drops it
    // on load, so it is injected — never round-tripped). This also seeds
    // the meta on a legacy note's first save: m_currentTitle equals the
    // resolver output the user has been seeing, so the label never flips.
    const QString body =
        NotesStorage::withTitleMeta(m_editor->toHtml(), m_currentTitle);

    // A7 — external-edit conflict guard. If the file on disk no longer
    // matches the stamp we recorded at load / last save (sync tool,
    // another editor, deletion), DO NOT overwrite it: rescue the buffer
    // to a "<name> (conflict …).html" sibling instead. An mtime-only
    // touch(1) passes (content hash unchanged).
    if (diskChangedSinceLoad(m_currentPath)) {
        rescueToConflictCopy(body);
        return;
    }

    QString err;
    if (!m_storage->saveNote(m_currentPath, body, &err)) {
        // M2 — failures used to be stderr-only while the hint kept reading
        // "editing… (auto-saves in 5s)". Now the hint flips to a red
        // NOT SAVED state; a 2nd consecutive failure raises the banner.
        fprintf(stderr, "Noter: saveNote failed: %s\n", qPrintable(err));
        noteSaveFailed(err);
        return;
    }
    m_dirty = false;
    m_lastSavedAt = QDateTime::currentDateTime();
    recordDiskState(m_currentPath);   // A7 — refresh the conflict stamp
    noteSaveSucceeded();

    // Re-index todos so the sidebar tree reflects current state.
    if (m_todos) m_todos->reindexNote(m_currentPath, body);
    refreshSidebar();

    emit noteSaved(m_currentPath);
}

// ═══════════════════════════════════════════════════════════════════════
//  A3 — title identity helpers (H1 ↔ display title sync)
// ═══════════════════════════════════════════════════════════════════════

// The text of the FIRST headingLevel==1 block, simplified and capped at
// 200 chars (matching safeFilename / the meta writer). Scans at most the
// first 200 blocks; empty when the document has no H1. Qt's HTML import
// sets blockFormat().headingLevel() for <h1> (pinned by widget test).
QString NotesPanel::titleFromEditorH1() const {
    if (!m_editor) return QString();
    int scanned = 0;
    for (QTextBlock b = m_editor->document()->firstBlock();
         b.isValid() && scanned < 200; b = b.next(), ++scanned) {
        if (b.blockFormat().headingLevel() == 1)
            return b.text().simplified().left(200);
    }
    return QString();
}

// Rewrite that same block's text in place (sidebar rename → H1 heal).
// Doc surgery, not a user edit: signals blocked + m_loadingInProgress so
// onEditorBodyChanged / the markdown-shortcut hook never fire mid-write;
// m_dirty is set manually by the caller's commit path. No H1 block →
// no-op (the title lives in the meta only).
void NotesPanel::setEditorH1(const QString &title) {
    if (!m_editor) return;
    QTextDocument *doc = m_editor->document();
    QTextBlock target;
    int scanned = 0;
    for (QTextBlock b = doc->firstBlock(); b.isValid() && scanned < 200;
         b = b.next(), ++scanned) {
        if (b.blockFormat().headingLevel() == 1) { target = b; break; }
    }
    if (!target.isValid()) return;

    const bool wasLoading = m_loadingInProgress;
    m_loadingInProgress = true;
    m_editor->blockSignals(true);
    // Keep the block's existing char format (or rebuild the bold/18pt
    // heading look for an empty block, like insertHeadingBlock does).
    QTextCharFormat fmt;
    if (target.length() > 1) {
        QTextCursor probe(doc);
        probe.setPosition(target.position() + 1);
        fmt = probe.charFormat();
    } else {
        fmt.setFontWeight(QFont::Bold);
        fmt.setFontPointSize(NoterExtractApply::headingPointSize(1));
    }
    QTextCursor cur(doc);
    cur.setPosition(target.position());
    cur.movePosition(QTextCursor::EndOfBlock, QTextCursor::KeepAnchor);
    cur.insertText(title.left(200), fmt);
    m_editor->blockSignals(false);
    m_loadingInProgress = wasLoading;
    m_dirty = true;
}

// ═══════════════════════════════════════════════════════════════════════
//  M2 — visible save-failure state + "Save a copy…" escape hatch
//
//  Before this block a failed autosave was stderr-only while the footer
//  hint kept reading "editing… (auto-saves in 5s)" — the user had NO
//  signal that minutes of typing existed only in memory.
// ═══════════════════════════════════════════════════════════════════════

void NotesPanel::setSavedHintNormal(const QString &text) {
    if (!m_savedHint) return;
    m_savedHint->setText(text);
    m_savedHint->setStyleSheet(QString());   // back to the page QSS (muted grey)
    m_savedHint->setProperty("saveFailed", false);
}

void NotesPanel::setSavedHintFailure(const QString &reason) {
    if (!m_savedHint) return;
    QString shortReason = reason.simplified();
    if (shortReason.isEmpty()) shortReason = tr("disk write failed");
    if (shortReason.size() > 80)
        shortReason = shortReason.left(79) + QChar(0x2026);
    m_savedHint->setText(tr("NOT SAVED — %1").arg(shortReason));
    m_savedHint->setStyleSheet(QStringLiteral(
        "color: %1; font-weight: 600; padding-right: 8px;").arg(m_pal.danger));
    m_savedHint->setProperty("saveFailed", true);
}

void NotesPanel::noteSaveFailed(const QString &err) {
    m_conflictNoticePending = false;   // a real write failure supersedes it
    m_lastSaveFailed = true;
    ++m_saveFailureCount;
    m_lastSaveError = err.simplified();
    setSavedHintFailure(m_lastSaveError);
    // 2+ consecutive failures is a pattern, not a blip — surface the
    // one-shot banner with the "Save a copy…" escape hatch. One-shot per
    // failure streak so it doesn't re-pop every 5s autosave tick after the
    // user hides it; the red hint stays on regardless.
    if (m_saveFailureCount >= 2 && !m_saveFailBannerShown) {
        m_saveFailBannerShown = true;
        showSaveFailureBanner();
    }
}

void NotesPanel::noteSaveSucceeded() {
    m_conflictNoticePending = false;   // a clean save clears any pending notice
    m_lastSaveFailed = false;
    m_saveFailureCount = 0;
    m_saveFailBannerShown = false;
    m_lastSaveError.clear();
    hideSaveFailureBanner();
    setSavedHintNormal(tr("auto-saved"));
}

void NotesPanel::showSaveFailureBanner() {
    if (!m_saveFailBanner || !m_saveFailLabel) return;
    m_saveFailLabel->setText(tr(
        "This note can't be written to disk (%1). Your latest edits exist "
        "only in this window — save a copy somewhere safe.")
            .arg(m_lastSaveError.isEmpty() ? tr("unknown error")
                                           : m_lastSaveError));
    if (m_rightStack && m_editorPage) showEditorPage();
    m_saveFailBanner->setVisible(true);
}

void NotesPanel::hideSaveFailureBanner() {
    if (m_saveFailBanner) m_saveFailBanner->setVisible(false);
}

// "Save a copy…" — rescue an in-memory delta to a user-picked path when
// the canonical note location is unwritable. Default suggested name is the
// note title + .html, in the user's Documents folder.
bool NotesPanel::promptSaveCopyAs() {
    if (!m_editor) return false;
    QString base = m_currentTitle.isEmpty() ? tr("note") : m_currentTitle;
    base.replace(QLatin1Char('/'), QLatin1Char('-'));
    const QString docs =
        QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation);
    const QString suggested =
        QDir(docs).filePath(base + QStringLiteral(".html"));
    const QString picked = QFileDialog::getSaveFileName(
        this, tr("Save a copy of this note"), suggested,
        tr("HTML files (*.html);;All files (*)"));
    if (picked.isEmpty()) return false;
    // Write through the storage layer so the copy gets the same sanitize +
    // atomic-write treatment as a normal note (and reopens cleanly later).
    // A3 — rescued copies keep their display title (meta injected).
    QString err;
    if (!m_storage->saveNote(picked,
                             NotesStorage::withTitleMeta(m_editor->toHtml(),
                                                         m_currentTitle),
                             &err)) {
        noterMessageBox(this, m_pal, QMessageBox::Warning,
                        tr("Save a copy failed"),
                        tr("Could not write %1: %2").arg(picked, err));
        return false;
    }
    if (auto *sb = window() ? window()->findChild<QStatusBar *>() : nullptr)
        sb->showMessage(tr("Copy saved to %1").arg(picked), 5000);
    return true;
}

// ═══════════════════════════════════════════════════════════════════════
//  A7 — external-edit conflict guard
//
//  Before this block there were ZERO mtime checks anywhere in the Noter
//  save path: a note rewritten on disk by a sync tool (or any other
//  program) while open here was silently overwritten by the next 5s
//  autosave tick — and the external version was rotated out of the .bak
//  ring within ~25s of continued typing. The guard stamps the on-disk
//  state (mtime + size + SHA-256) at load and after every successful
//  save; a pre-save mismatch reroutes the buffer to a conflict copy.
// ═══════════════════════════════════════════════════════════════════════

void NotesPanel::recordDiskState(const QString &absolutePath) {
    const QFileInfo fi(absolutePath);
    m_diskStateValid = fi.exists();
    if (!m_diskStateValid) {
        m_diskMtime = QDateTime();
        m_diskSize = -1;
        m_diskHash.clear();
        return;
    }
    m_diskMtime = fi.lastModified();
    m_diskSize = fi.size();
    QFile f(absolutePath);
    m_diskHash = f.open(QIODevice::ReadOnly)
                     ? QCryptographicHash::hash(f.readAll(),
                                                QCryptographicHash::Sha256)
                     : QByteArray();
}

bool NotesPanel::diskChangedSinceLoad(const QString &absolutePath) {
    // No stamp recorded (e.g. the Todos checklist path, which doesn't go
    // through renderNoteAtPath) → behave as before the guard existed.
    if (!m_diskStateValid) return false;
    const QFileInfo fi(absolutePath);
    if (!fi.exists()) return true;            // deleted under us → conflict
    if (fi.lastModified() == m_diskMtime && fi.size() == m_diskSize)
        return false;                         // fast path: stamp untouched
    // mtime or size moved — confirm with the content hash. touch(1) (and
    // sync tools that re-stamp without rewriting) bump mtime only; that
    // is NOT a conflict, just refresh the stamp.
    QFile f(absolutePath);
    if (!f.open(QIODevice::ReadOnly)) return true;   // unreadable → conflict
    const QByteArray hash =
        QCryptographicHash::hash(f.readAll(), QCryptographicHash::Sha256);
    if (hash == m_diskHash) {
        m_diskMtime = fi.lastModified();
        m_diskSize = fi.size();
        return false;
    }
    return true;
}

// "<name> (conflict yyyy-MM-dd hhmmss).html" next to the original. The
// extension split is on the LAST dot (completeBaseName) — see
// feedback_qfileinfo_basename_splits_first_dot. A numeric suffix dedups
// the (unlikely) same-second double conflict.
QString NotesPanel::conflictCopyPathFor(const QString &absolutePath) {
    const QFileInfo fi(absolutePath);
    const QString base = fi.completeBaseName();
    const QString ext =
        fi.suffix().isEmpty() ? QStringLiteral("html") : fi.suffix();
    const QString stamp = QDateTime::currentDateTime().toString(
        QStringLiteral("yyyy-MM-dd hhmmss"));
    QString candidate = fi.dir().filePath(
        QStringLiteral("%1 (conflict %2).%3").arg(base, stamp, ext));
    int n = 2;
    while (QFile::exists(candidate)) {
        candidate = fi.dir().filePath(
            QStringLiteral("%1 (conflict %2) (%3).%4")
                .arg(base, stamp, QString::number(n++), ext));
    }
    return candidate;
}

void NotesPanel::rescueToConflictCopy(const QString &bodyHtml) {
    const QString origPath = m_currentPath;
    const QString origName = QFileInfo(origPath).fileName();
    const QString conflictPath = conflictCopyPathFor(origPath);
    QString err;
    if (!m_storage->saveNote(conflictPath, bodyHtml, &err)) {
        // Couldn't even write the rescue copy — fall back to the plain M2
        // failure path (red hint; banner on the 2nd consecutive failure).
        fprintf(stderr, "Noter: conflict-copy save failed: %s\n",
                qPrintable(err));
        noteSaveFailed(err);
        return;
    }
    // The buffer's home is now the conflict copy. The externally-modified
    // original stays untouched on disk; its sidebar leaf reloads it.
    m_storage->clearDraft(origPath);   // the delta lives in the copy now
    m_currentPath = conflictPath;
    // A3 ↔ A7 — m_currentTitle is the resolver DISPLAY title (meta-first),
    // NEVER the raw filename stem. The conflict body we just wrote carries
    // the correct notepatra-title meta (computed PRE-rescue from the old
    // m_currentTitle), so re-resolving from the new path returns the real
    // title — matching the two renderNoteAtPath sites and keeping the NEXT
    // save's withTitleMeta + reindexNote honest.
    m_currentTitle = m_storage->displayTitleForFile(conflictPath);
    emit noteTitleChanged(m_currentTitle);
    recordDiskState(conflictPath);
    m_dirty = false;
    m_lastSavedAt = QDateTime::currentDateTime();
    // NOT noteSaveFailed(): the user's version IS safely on disk, so the
    // M2c navigate-away guard must stay disarmed.
    m_lastSaveFailed = false;
    m_saveFailureCount = 0;
    m_lastSaveError.clear();
    noteSaveConflicted(origName, QFileInfo(conflictPath).fileName());
    if (m_todos) m_todos->reindexNote(m_currentPath, bodyHtml);
    refreshSidebar();
    emit noteSaved(m_currentPath);
}

void NotesPanel::noteSaveConflicted(const QString &origName,
                                    const QString &conflictName) {
    // Bug2 (A7) — survive a clean load of another note in the SAME nav stack
    // (renderNoteAtPath's success path would otherwise hide the banner before
    // it ever paints). Consumed once by the next clean load.
    m_conflictNoticePending = true;
    // Reuse the M2 surfaces (red hint + banner) with conflict wording —
    // shown immediately: a conflict is never a blip, and silence here is
    // exactly the data-loss class this guard exists to kill.
    if (m_savedHint) {
        m_savedHint->setText(tr("CONFLICT — saved as a copy"));
        m_savedHint->setStyleSheet(QStringLiteral(
            "color: %1; font-weight: 600; padding-right: 8px;").arg(m_pal.danger));
        m_savedHint->setProperty("saveFailed", false);
    }
    if (m_saveFailBanner && m_saveFailLabel) {
        m_saveFailLabel->setText(tr(
            "Note changed on disk — your version was saved as %1. "
            "It is open here now; click %2 in the sidebar to see the "
            "version from disk.").arg(conflictName, origName));
        if (m_rightStack && m_editorPage) showEditorPage();
        m_saveFailBanner->setVisible(true);
    }
}

void NotesPanel::popOutActive() {
    if (m_currentPath.isEmpty()) return;
    // Reuse only a LIVE pop-out that is still on screen AND mirrors the
    // current note. The window is WA_DeleteOnClose (closing destroys it and
    // the destroyed-connection below nulls the pointer), but the pointer
    // can also be stale-but-alive: hidden (close not fully processed yet)
    // or pointing at a previously open note. Both get a fresh pop-out —
    // pre-fix the unconditional early-return left the pop-out permanently
    // dead after its first close.
    if (m_popOut && m_popOut->isVisible() &&
        m_popOut->notePath() == m_currentPath) {
        m_popOut->raise();
        m_popOut->activateWindow();
        return;
    }
    if (m_popOut) {
        m_popOut->deleteLater();
        m_popOut = nullptr;
    }
    m_popOut = new NoterPopOut(m_currentPath);
    // A5 — restyle the pop-out chrome with the panel's active palette
    // (its constructor defaults to Light for standalone construction).
    m_popOut->applyNoterPalette(m_pal);
    // Titlebar shows the same display title as the sidebar leaf
    // ("Noter 06" / the meta title), never the raw filename stem.
    // m_currentTitle is resolver-fed at load; the empty fallback covers
    // a pop-out raised before any title state exists.
    m_popOut->setDisplayTitle(m_currentTitle.isEmpty()
                                  ? m_storage->displayTitleForFile(m_currentPath)
                                  : m_currentTitle);
    // Null the pointer when the window dies — but only if the dying object
    // is the one we currently track, so the deferred delete of a REPLACED
    // pop-out can't wipe its successor.
    connect(m_popOut, &QObject::destroyed, this, [this](QObject *gone) {
        if (static_cast<QObject *>(m_popOut) == gone) m_popOut = nullptr;
    });
    m_popOut->show();
}

void NotesPanel::endMeetingSweep() {
    // v0.1.112 — one Extract in flight at a time. Re-triggering (button
    // routes to cancelExtract(); this guards the Ctrl+Alt+E path) used to
    // stack another override cursor + a duplicate network request.
    // v0.1.113 — m_extractStarting also latches the pre-flight-probe window
    // (m_extractBusy is set only AFTER the nested-event-loop probe).
    if (m_extractBusy || m_extractStarting) return;
    if (m_currentPath.isEmpty() || !m_editor) {
        noterMessageBox(this, m_pal, QMessageBox::Information, tr("Extract"),
                        tr("Open or create a meeting note first."));
        return;
    }
    if (m_editor->toPlainText().trimmed().isEmpty()) {
        noterMessageBox(this, m_pal, QMessageBox::Information, tr("Extract"),
                        tr("Nothing to extract — the note is empty."));
        return;
    }
    // v0.1.112 — exclude the previous AI-Extract region from the prompt:
    // the model must not re-extract its own output, and a long note gets
    // its ctx budget back. Done on a CLONE so the editor is untouched.
    QString bodyHtml;
    {
        const auto reg = NoterExtractApply::findExtractRegion(m_editor->document());
        if (reg.found) {
            QScopedPointer<QTextDocument> clone(m_editor->document()->clone());
            QTextCursor c(clone.data());
            c.setPosition(reg.beginBlockPos);
            c.setPosition(reg.endBlockPos + reg.endBlockLen - 1,
                          QTextCursor::KeepAnchor);
            c.removeSelectedText();
            bodyHtml = clone->toHtml();
        } else {
            bodyHtml = m_editor->toHtml();
        }
    }

    // Build the prompt + spawn an Ollama request. The model is whatever
    // the user picked in Config::aiNoterModel; baseUrl + backend come
    // from the global AI settings so cloud-via-OpenAI-compat works too.
    const QString model = Config::instance().aiNoterModel.isEmpty()
                              ? QStringLiteral("llama3.1:8b")
                              : Config::instance().aiNoterModel;

    const auto backend = OllamaClient::backendFromString(
        Config::instance().aiBackend.isEmpty() ? QStringLiteral("Ollama")
                                               : Config::instance().aiBackend);
    auto *client = new OllamaClient(this);
    client->setBackend(backend);
    if (!Config::instance().aiBaseUrl.isEmpty())
        client->setBaseUrl(Config::instance().aiBaseUrl);
    client->setModel(model);
    // v0.1.112 — tag the request so the AI Interaction Log can attribute
    // it (it logged Extract rows with an empty mode before).
    client->setMode(QStringLiteral("noter-extract"));

    // v0.1.112 PRE-FLIGHT — synchronous 3s reachability probe BEFORE any
    // cursor override or request. With the backend down, the generate()
    // stream emits neither finished nor error (ollama.cpp's silent-hang
    // path), so pre-v0.1.112 the wait cursor stayed forever. Failing fast
    // here costs one /api/tags GET when the backend is up.
    // v0.1.113 — latch BEFORE the nested-event-loop probe so a second
    // Extract trigger delivered during it re-enters endMeetingSweep and
    // hits the guard above instead of starting a second flight.
    m_extractStarting = true;
    // RAII so the latch can NEVER stick: if isAvailable()'s nested event
    // loop, the prompt build, or the QMessageBox throws between here and the
    // live request, a permanently-true m_extractStarting would deadlock ALL
    // future Extracts at the guard above. This clears it on EVERY exit —
    // fail-return, exception, AND the success path (where m_extractBusy then
    // owns single-flight, so dropping the latch on return is correct).
    struct ClearOnExit {
        bool &flag;
        ~ClearOnExit() { flag = false; }
    } clearStarting{ m_extractStarting };
    if (!client->isAvailable()) {
        const QString msg = (backend == OllamaClient::Ollama)
            ? tr("Ollama isn't running — start it with: ollama serve")
            : tr("The AI backend at %1 isn't reachable. Check the backend "
                 "settings in the AI panel.").arg(client->baseUrl());
        client->deleteLater();
        noterMessageBox(this, m_pal, QMessageBox::Warning, tr("Extract"), msg);
        return;   // clearStarting resets m_extractStarting
    }

    // v0.1.112 — split the prompt at the U+001F sentinel into its system
    // and user halves. The combined string used to be sent verbatim as the
    // prompt arg — raw control byte on the wire, system rules in the user
    // turn. binfo carries the honest ctx-budget truncation report through
    // to the dialog notice + the persisted caption.
    NoterSweepPrompt::BuildInfo binfo;
    const QString combined = NoterSweepPrompt::build(bodyHtml, m_currentTitle, &binfo);
    const NoterSweepPrompt::PromptParts parts =
        NoterSweepPrompt::splitPrompt(combined);

    m_extractClient = client;
    beginExtractBusy();   // request is live — m_extractBusy owns single-flight now;
                          // clearStarting drops m_extractStarting on return.
    // v0.1.113 — PIN the note Extract was launched on. The reply lands up
    // to ~125s later and the sidebar stays live, so m_currentPath may be a
    // DIFFERENT note when it does; apply must target the launch note.
    const QString launchPath = m_currentPath;

    connect(client, &OllamaClient::finished, this,
            [this, client, model, binfo, launchPath](const QString &response) {
                // v0.1.98 CRASH FIX (core 1321961) — `finished` is emitted from
                // INSIDE the client's reply-handling slot while the QNetworkReply
                // is still mid-teardown. The previous code opened the sweep dialog
                // HERE, running a NESTED event loop on that stack; the reply's
                // trailing queued downloadProgress then landed in freed memory
                // when the modal closed → SIGSEGV (on Cancel AND Accept; earlier
                // cores 872149 / 890509 were the same class). Fix: sever + delete
                // the network client NOW, then run the dialog on the NEXT clean
                // event-loop tick — no live reply, no nested loop on the signal
                // emission stack.
                client->cancel();          // disconnect + abort + free the reply
                client->deleteLater();
                if (m_extractClient == client) m_extractClient.clear();
                finishExtractCleanup();    // cursor + button + watchdog
                const int wu = binfo.truncated ? binfo.wordsUsed : 0;
                const int wt = binfo.truncated ? binfo.wordsTotal : 0;
                QTimer::singleShot(0, this, [this, response, model, wu, wt, launchPath]() {
                    showExtractResult(response, model, wu, wt, launchPath);
                });
            });
    connect(client, &OllamaClient::error, this,
            [this, client](const QString &err) {
                client->cancel();
                client->deleteLater();
                if (m_extractClient == client) m_extractClient.clear();
                finishExtractCleanup();
                // Defer the modal off the signal stack too (same crash class).
                QTimer::singleShot(0, this, [this, err]() {
                    noterMessageBox(this, m_pal, QMessageBox::Warning,
                        tr("Extract failed"),
                        tr("Could not reach the AI backend: %1").arg(err));
                });
            });
    client->generate(parts.user, parts.system, /*enableThinking=*/false);
}

// ═══════════════════════════════════════════════════════════════════════
//  Extract busy-state machine (v0.1.112)
// ═══════════════════════════════════════════════════════════════════════
//
// Every way an Extract can end — finished, error, user Cancel, watchdog
// timeout, panel-level pre-flight refusal — funnels through
// finishExtractCleanup() so the override cursor and the button can never
// be left stranded again (the original hang left BOTH stuck forever).

void NotesPanel::beginExtractBusy() {
    m_extractBusy = true;
    QApplication::setOverrideCursor(Qt::WaitCursor);
    if (m_extractBtn) {
        // The button stays ENABLED — it becomes the Cancel control while
        // the request is in flight (onExtractClicked branches on busy).
        m_extractBtn->setText(tr("Cancel"));
        m_extractBtn->setIcon(
            style()->standardIcon(QStyle::SP_BrowserStop));
        m_extractBtn->setToolTip(tr("Cancel the in-flight AI extraction"));
    }
    if (!m_extractWatchdog) {
        m_extractWatchdog = new QTimer(this);
        m_extractWatchdog->setSingleShot(true);
        // ~125s — generous for a slow local 8B model on CPU, but finite.
        // This is the Noter-side net for the backend's silent-hang path
        // (mid-stream connection drop emits neither finished nor error).
        m_extractWatchdog->setInterval(125000);
        connect(m_extractWatchdog, &QTimer::timeout, this, [this]() {
            cancelExtract();
            // Defer the modal off the timer slot (same crash class as the
            // finished/error handlers — never nest an event loop here).
            QTimer::singleShot(0, this, [this]() {
                noterMessageBox(this, m_pal, QMessageBox::Warning,
                    tr("Extract timed out"),
                    tr("The AI backend didn't answer within ~2 minutes. "
                       "The request was cancelled — try again, or pick a "
                       "smaller model."));
            });
        });
    }
    m_extractWatchdog->start();
}

void NotesPanel::finishExtractCleanup() {
    if (m_extractWatchdog) m_extractWatchdog->stop();
    // beginExtractBusy() pushes exactly one override per flight (the
    // m_extractBusy guard prevents stacking), so restore exactly one —
    // but only if we actually own one, so a stray call can't pop some
    // other feature's override.
    if (m_extractBusy)
        QApplication::restoreOverrideCursor();
    if (m_extractBtn) {
        m_extractBtn->setEnabled(true);
        m_extractBtn->setText(tr("Extract"));
        m_extractBtn->setIcon(sparkleIcon());
        m_extractBtn->setToolTip(
            tr("AI: extract action items from this note (Ctrl+Alt+E)"));
    }
    m_extractBusy = false;
}

void NotesPanel::cancelExtract() {
    if (m_extractClient) {
        // Sever the panel-side connections FIRST so a queued finished()/
        // error() from the dying reply can't re-enter the handlers after
        // cleanup, then abort the wire request.
        disconnect(m_extractClient, nullptr, this, nullptr);
        m_extractClient->cancel();
        m_extractClient->deleteLater();
        m_extractClient.clear();
    }
    finishExtractCleanup();
}

// ── Real heading blocks ──────────────────────────────────────────────────
// Section headers used to be bold-only char formats — they EVAPORATED on
// save+reload. headingPointSize/insertHeadingBlock (which write REAL
// heading blocks that round-trip as <hN>) moved to notes_extract_apply.cpp
// (v0.1.112) so the Extract region writer and insertSubheader share one
// implementation.

// Runs the Extract review dialog + applies the result. Deferred (called via
// singleShot from the finished handler) so it NEVER opens a nested event loop
// while the network reply is still being torn down — see the crash note above.
void NotesPanel::showExtractResult(const QString &response, const QString &model,
                                   int wordsUsed, int wordsTotal,
                                   const QString &launchPath) {
    // v0.1.112 — Extract is for meeting notes. Accepting it on the Todos
    // checklist would feed the written headings/bullets through
    // saveTodosChecklist, which converts EVERY line into a todo row —
    // corrupting the todo store.
    if (m_currentIsChecklist) {
        noterMessageBox(this, m_pal, QMessageBox::Information, tr("Extract"),
            tr("Extract works on meeting notes — open a meeting note to use it."));
        return;
    }
    // v0.1.113 — refuse to apply A's extract into a note switched-to mid-
    // flight. launchPath = the note Extract ran on; empty = legacy direct
    // call (apply to current, unchanged). A non-empty mismatch means the
    // live editor/document/path now belong to a different note.
    if (!launchPath.isEmpty() && launchPath != m_currentPath) {
        noterMessageBox(this, m_pal, QMessageBox::Information, tr("Extract"),
            tr("You switched notes while the AI was working. Open the note "
               "you ran Extract on, then run Extract again to apply it."));
        return;
    }
    NoterSweepPrompt::SweepResult result = NoterSweepPrompt::parse(response);
    // v0.1.112 — honest outcome triage. An unparseable reply used to fall
    // into the same 'no actionable items' info box as a genuinely empty
    // result, hiding real model failures from the user.
    switch (NoterSweepPrompt::classify(result)) {
    case NoterSweepPrompt::ExtractOutcome::ParseError: {
        QMessageBox box(QMessageBox::Warning, tr("Extract failed"),
            tr("The model returned something I could not parse (%1). "
               "The raw reply is under Details.").arg(result.errorMessage),
            QMessageBox::Ok, this);
        box.setDetailedText(result.rawResponse);
        applyNoterDialogTheme(&box, m_pal);
        box.exec();
        return;
    }
    case NoterSweepPrompt::ExtractOutcome::Empty:
        noterMessageBox(this, m_pal, QMessageBox::Information, tr("Extract"),
            tr("AI didn't find any actionable items in this note."));
        return;
    case NoterSweepPrompt::ExtractOutcome::Items:
        break;
    }
    NoterSweepDialog dlg(result, this);
    dlg.setEyebrow(model, 0);
    dlg.setTargetPath(m_currentPath);
    // v0.1.112 — honest long-note coverage: tell the user how much of the
    // note the model actually read (build() truncates to fit the ctx).
    if (wordsUsed > 0 && wordsTotal > wordsUsed)
        dlg.setTruncationNotice(wordsUsed, wordsTotal);
    // v0.1.98 — tell the dialog what's ALREADY scheduled for this note so it
    // flags duplicates (user: "already-scheduled should be flagged"). It lists
    // them + default-unchecks matching action rows.
    if (m_todos) {
        QVector<QPair<QString, QDateTime>> existing;
        for (const TodoRow &r : m_todos->allScheduledReminders())
            if (r.sourceFile == m_currentPath && r.reminderAt.isValid())
                existing.append({ r.text.isEmpty() ? r.meetingTitle : r.text,
                                  r.reminderAt });
        dlg.setExistingReminders(existing);
    }
    if (dlg.exec() != QDialog::Accepted) return;

    // v0.1.112 — persist ALL reviewed sections (incl. Decisions /
    // Questions / Risks, which used to be discarded) as the note's owned
    // marked region: replace-in-place on re-runs instead of stacking.
    applyExtractResultToNote(dlg.finalResult(), model, wordsUsed, wordsTotal);
    m_dirty = true;
    saveCurrentNote();

    // Schedule a reminder for each action the user kept checked. Each becomes
    // its own row under the central Reminders root (addReminder allows many per
    // note). Owner is folded into the title so the notification reads e.g.
    // "Ship build  @prateek". Past-due picks are skipped.
    if (m_todos) {
        int scheduled = 0;
        for (const auto &item : dlg.reminderItems()) {
            // v0.1.98 — schedule whatever the user kept checked, even if the
            // picked time is slightly in the past (it shows under "Overdue" and
            // fires on the next tick). Previously a just-passed time like "23:46
            // today" was silently dropped (user: "the new one did not pick").
            if (!item.dueAt.isValid()) continue;
            QString title = item.text;
            if (!item.owner.isEmpty())
                title += QStringLiteral("  ") + item.owner;
            if (!m_todos->addReminder(m_currentPath, title, item.dueAt).isEmpty())
                ++scheduled;
        }
        refreshSidebar();
        if (scheduled > 0) {
            expandRemindersRoot();
            if (auto *sb = window() ? window()->findChild<QStatusBar *>() : nullptr)
                sb->showMessage(
                    tr("Scheduled %n reminder(s) — see the Reminders list",
                       "", scheduled), 4000);
        }
    }
}

// v0.1.112 — write the accepted Extract result as the note's OWNED MARKED
// REGION (see notes_extract_apply.h for the marker design):
//   * no previous region        → append at the end (first run, legacy
//     pre-marker notes, deleted-marker recovery — old stacked blocks stay
//     untouched).
//   * region found + sig match  → replace in place, carrying ✓ done-state
//     across (checkbox toggles are normalized OUT of the sig, so marking
//     an item done still counts as "untouched").
//   * region found + sig MISMATCH (user edited inside) → ask Replace /
//     Keep both, DEFAULT Keep both — a re-run can never destroy user
//     edits. Modal-safety: we are already past dlg.exec() on the deferred
//     singleShot stack, the same modal context as the review dialog (NOT
//     the v0.1.98 nested-event-loop class).
void NotesPanel::applyExtractResultToNote(
        const NoterSweepPrompt::SweepResult &finalResult,
        const QString &model, int wordsUsed, int wordsTotal) {
    if (!m_editor) return;

    const QVector<NoterExtractApply::ExtractLine> lines =
        NoterExtractApply::renderExtractLines(
            finalResult, model, QDateTime::currentDateTime(),
            wordsUsed, wordsTotal);

    // Sig over the lines EXACTLY as they will appear in the document
    // (Check lines carry the "☐ " marker writeRegion prepends).
    QStringList lineTexts;
    lineTexts.reserve(lines.size());
    for (const auto &ln : lines)
        lineTexts << (ln.kind == NoterExtractApply::ExtractLine::Check
                          ? QStringLiteral("☐ ") + ln.text
                          : ln.text);
    const QString sig = NoterExtractApply::regionSig(lineTexts);

    const NoterExtractApply::Region reg =
        NoterExtractApply::findExtractRegion(m_editor->document());

    bool replaceInPlace = false;
    QSet<QString> doneKeys;
    if (reg.found) {
        // Harvest done-state BEFORE any removal/ask — both paths may use it.
        doneKeys = NoterExtractApply::collectDoneKeys(reg.innerTexts);
        if (NoterExtractApply::regionSig(reg.innerTexts) == reg.storedSig) {
            replaceInPlace = true;
        } else {
            QMessageBox box(QMessageBox::Question, tr("Extract"),
                tr("The previous AI Extract block was edited since it was "
                   "written. Replace it with the new extract, or keep both?"),
                QMessageBox::NoButton, this);
            QPushButton *replaceBtn =
                box.addButton(tr("Replace"), QMessageBox::DestructiveRole);
            QPushButton *keepBtn =
                box.addButton(tr("Keep both"), QMessageBox::AcceptRole);
            box.setDefaultButton(keepBtn);   // never destroy edits by default
            applyNoterDialogTheme(&box, m_pal);
            box.exec();
            replaceInPlace = (box.clickedButton() == replaceBtn);
        }
    }

    // One edit block: a single Ctrl+Z restores the previous state.
    QTextCursor cur(m_editor->document());
    cur.beginEditBlock();
    if (replaceInPlace) {
        cur.setPosition(reg.beginBlockPos);
        cur.setPosition(reg.endBlockPos + reg.endBlockLen - 1,
                        QTextCursor::KeepAnchor);
        cur.removeSelectedText();
        NoterExtractApply::writeRegion(cur, lines, sig, doneKeys);
    } else {
        cur.movePosition(QTextCursor::End);
        cur.insertBlock();
        NoterExtractApply::writeRegion(cur, lines, sig, QSet<QString>());
    }
    cur.endEditBlock();

    // Re-derive strike-through for carried-over "✓ " lines — same pass the
    // load path runs after setHtml.
    restyleChecklistLines();
}

// ═══════════════════════════════════════════════════════════════════════
//  Slots
// ═══════════════════════════════════════════════════════════════════════

void NotesPanel::onAutoSaveTick() {
    if (m_dirty && !m_currentPath.isEmpty()) saveCurrentNote();
}

// v0.1.97 — three-root sidebar dispatch. itemActivated AND itemClicked
// both route through here. Kind-based dispatch:
//   meeting / trashed_meeting   → open the file in the editor
//   todo                         → open the editable Todos checklist
//                                  (NOT the source meeting — user asked
//                                   for todos to open & edit on their own)
//   trashed_todo                 → no-op on click (right-click → Restore)
//   root / section               → let the tree handle expand/collapse
void NotesPanel::onSidebarItemActivated(QTreeWidgetItem *item, int) {
    if (!item) return;
    const QString kind = item->data(0, Qt::UserRole).toString();
    // itemActivated + itemDoubleClicked can BOTH fire on one double-click
    // (style-dependent). Guard against reopening the already-open file so
    // we don't thrash the editor / lose the cursor.
    auto openIfDifferent = [this](const QString &path) {
        if (!path.isEmpty() && path != m_currentPath) openNoteFile(path);
    };
    // Trash is a holding area — trashed_meeting / trashed_todo NEVER open
    // (not even read-only). Restore first, then open. Only live items open.
    if (kind == QStringLiteral("meeting")) {
        openIfDifferent(item->data(0, Qt::UserRole + 1).toString());
    } else if (kind == QStringLiteral("reminder")) {
        // A reminder leaf opens the note it's bound to (UserRole+1).
        openIfDifferent(item->data(0, Qt::UserRole + 1).toString());
    } else if (kind == QStringLiteral("todo")) {
        // Open the standalone Todos checklist for editing — never jump to
        // the source meeting.
        if (!m_currentIsChecklist || m_currentPath != todosChecklistPath())
            openTodosChecklist();
    }
}

void NotesPanel::onSearchChanged(const QString &) {
    refreshSidebar();
}

void NotesPanel::onNewMeetingClicked() {
    newMeetingNote();
}

void NotesPanel::onExtractClicked() {
    // v0.1.112 — while a request is in flight the button reads "Cancel"
    // and aborts it (same path as the watchdog). No more duplicate
    // requests from retry-clicking.
    if (m_extractBusy) {
        cancelExtract();
        return;
    }
    endMeetingSweep();
}

void NotesPanel::onEditorBodyChanged() {
    if (m_loadingInProgress) return;
    if (m_readError) return;   // M2 — error notice is not user content; never dirty
    m_dirty = true;
    // A7 — arm the draft timer on the FIRST dirty keystroke (don't restart
    // on every keystroke or sustained typing would postpone the draft
    // forever; single-shot + re-arm gives a steady ~1.5s cadence).
    if (m_draftTimer && !m_draftTimer->isActive()) m_draftTimer->start();
    // M2 — keep the red NOT SAVED state visible while the last save failed;
    // "editing…" would mask an active data-loss condition. The next
    // SUCCESSFUL save restores the normal hint cycle.
    if (m_lastSaveFailed) return;
    if (m_savedHint) m_savedHint->setText(tr("editing… (auto-saves in 5s)"));
}

// ═══════════════════════════════════════════════════════════════════════
//  Editor behaviors — markdown shortcuts + checkbox clicks
// ═══════════════════════════════════════════════════════════════════════

bool NotesPanel::eventFilter(QObject *watched, QEvent *event) {
    if (!m_editor) return QWidget::eventFilter(watched, event);

    // Click on the editor viewport — check whether the click landed on a
    // "☐ " or "✓ " character. If so, toggle it.
    if (watched == m_editor->viewport() && event->type() == QEvent::MouseButtonRelease) {
        auto *me = static_cast<QMouseEvent *>(event);
        if (me->button() == Qt::LeftButton) {
            QTextCursor cur = m_editor->cursorForPosition(me->pos());
            const QTextBlock block = cur.block();
            const QString line = block.text();
            if (line.startsWith(QStringLiteral("☐ ")) ||
                line.startsWith(QStringLiteral("✓ "))) {
                // Toggle ONLY when the click landed on the marker itself
                // (columns 0–2: the box glyph, its trailing space, or just
                // past it). A click anywhere in the item TEXT must place the
                // caret normally — pre-fix the column was discarded, so any
                // click on the line flipped its done-state and editing a
                // checklist item by mouse was impossible.
                if (cur.positionInBlock() > 2)
                    return false;   // let QTextEdit position the caret
                cur.setPosition(block.position());
                cur.movePosition(QTextCursor::Right, QTextCursor::KeepAnchor, 2);
                const QString token = cur.selectedText();
                if (token == QStringLiteral("☐ ")) {
                    cur.insertText(QStringLiteral("✓ "));
                    applyChecklistDoneStyle(cur.block(), true);   // strike through
                    m_dirty = true;
                    return true;
                } else if (token == QStringLiteral("✓ ")) {
                    cur.insertText(QStringLiteral("☐ "));
                    applyChecklistDoneStyle(cur.block(), false);  // un-strike
                    m_dirty = true;
                    return true;
                }
            }
        }
        return false;
    }

    // v0.1.98 — right-click in the editor: standard Cut/Copy/Paste PLUS an
    // "Insert subheader" submenu (each preset drops a bold heading + a ☐
    // bullet) and "Insert checkbox". ContextMenu may land on the QTextEdit
    // or its viewport depending on platform — handle both.
    if ((watched == m_editor || watched == m_editor->viewport())
        && event->type() == QEvent::ContextMenu) {
        auto *ce = static_cast<QContextMenuEvent *>(event);
        QMenu *menu = m_editor->createStandardContextMenu();
        menu->addSeparator();
        QMenu *sub = menu->addMenu(tr("Insert header"));
        QAction *aAct   = sub->addAction(tr("Action Items"));
        QAction *aPlan  = sub->addAction(tr("What I plan"));
        QAction *aTodo  = sub->addAction(tr("To-dos"));
        QAction *aQuote = sub->addAction(tr("Quotes"));
        QAction *aDec   = sub->addAction(tr("Decisions"));
        sub->addSeparator();
        QAction *aCustom = sub->addAction(tr("Custom…"));
        QAction *aChk = menu->addAction(tr("Insert checkbox"));
        // v0.1.117 — export the OPEN note (skip read-error notices; the
        // checklist is still a real .html document, so it may export too).
        QAction *aExpPdf = nullptr;
        QAction *aExpMd  = nullptr;
        if (!m_currentPath.isEmpty() && !m_readError) {
            menu->addSeparator();
            aExpPdf = menu->addAction(tr("Export as PDF…"));
            aExpMd  = menu->addAction(tr("Export as Markdown…"));
        }
        QAction *picked = menu->exec(ce->globalPos());
        if      (picked == aChk)    insertCheckboxAtCursor();
        else if (picked == aCustom) insertSubheader(QString());
        else if (picked == aAct)    insertSubheader(tr("Action Items"));
        else if (picked == aPlan)   insertSubheader(tr("What I plan"));
        else if (picked == aTodo)   insertSubheader(tr("To-dos"));
        else if (picked == aQuote)  insertSubheader(tr("Quotes"));
        else if (picked == aDec)    insertSubheader(tr("Decisions"));
        else if (aExpPdf && picked == aExpPdf)
            exportNoteTo(m_currentPath, /*asPdf=*/true);
        else if (aExpMd && picked == aExpMd)
            exportNoteTo(m_currentPath, /*asPdf=*/false);
        menu->deleteLater();
        return true;
    }

    // Key events in the editor — handle markdown shortcut & Enter on ☐.
    if (watched == m_editor && event->type() == QEvent::KeyPress) {
        auto *ke = static_cast<QKeyEvent *>(event);

        // Enter on a "☐ ..." line creates a new "☐ " on the next line so
        // typing a list flows naturally.
        if (ke->key() == Qt::Key_Return && !(ke->modifiers() & Qt::ShiftModifier)) {
            QTextCursor cur = m_editor->textCursor();
            const QString line = cur.block().text();
            const bool isCheckLine = line.startsWith(QStringLiteral("☐ ")) ||
                                     line.startsWith(QStringLiteral("✓ "));

            // In the dedicated Todos checklist EVERY line is a todo. So Enter
            // always starts a fresh "☐ " item — and a bare line the user typed
            // without a leading box gets one retro-fitted now, so the whole
            // list reads as checkboxes instead of stray plain text.
            if (m_currentIsChecklist) {
                const QString stripped = line.trimmed();
                const bool emptyItem = stripped.isEmpty() ||
                    stripped == QStringLiteral("☐") || stripped == QStringLiteral("✓");
                if (emptyItem) {
                    // Empty item → break out: leave a plain blank line.
                    cur.movePosition(QTextCursor::StartOfBlock);
                    cur.movePosition(QTextCursor::EndOfBlock,
                                     QTextCursor::KeepAnchor);
                    cur.removeSelectedText();
                    return true;
                }
                if (!isCheckLine) {
                    QTextCursor fix(cur);
                    fix.movePosition(QTextCursor::StartOfBlock);
                    fix.insertText(QStringLiteral("☐ "));   // cur shifts right with it
                }
                cur.movePosition(QTextCursor::EndOfBlock);
                cur.insertBlock();
                cur.insertText(QStringLiteral("☐ "));
                m_editor->setTextCursor(cur);
                m_dirty = true;
                return true;
            }

            if (isCheckLine) {
                if (line.length() <= 2) {
                    // Empty checkbox — break out of the list, clear the marker.
                    cur.movePosition(QTextCursor::StartOfBlock);
                    cur.movePosition(QTextCursor::EndOfBlock,
                                     QTextCursor::KeepAnchor);
                    cur.removeSelectedText();
                    return true;  // suppress the Return — line is now empty
                }
                cur.insertBlock();
                cur.insertText(QStringLiteral("☐ "));
                m_dirty = true;
                return true;
            }

            // Enter at the END of a heading line starts a PLAIN body block.
            // Without this the new block inherits headingLevel + bold from
            // the heading, so the next line typed would save as another
            // <hN> instead of body text.
            if (cur.atBlockEnd() && !cur.hasSelection() &&
                cur.blockFormat().headingLevel() > 0) {
                cur.insertBlock();
                QTextBlockFormat bf = cur.blockFormat();
                bf.setHeadingLevel(0);
                cur.setBlockFormat(bf);
                QTextCharFormat plain;
                plain.setFontWeight(QFont::Normal);
                plain.setFontItalic(false);
                cur.setBlockCharFormat(plain);
                cur.setCharFormat(plain);
                m_editor->setTextCursor(cur);
                m_dirty = true;
                return true;
            }
        }

        // F4 → toggle checkbox on current line.
        if (ke->key() == Qt::Key_F4) {
            toggleCheckboxOnCurrentLine();
            return true;
        }

        // Markdown auto-replace: "- [ ] " → "☐ ", "- [x] " → "✓ ".
        // Trigger on Space — check the prefix of the current line.
        if (ke->key() == Qt::Key_Space) {
            QTextCursor cur = m_editor->textCursor();
            const QString line = cur.block().text();
            const int colInBlock = cur.positionInBlock();
            if (colInBlock == 5 && line == QStringLiteral("- [ ]")) {
                cur.movePosition(QTextCursor::StartOfBlock);
                cur.movePosition(QTextCursor::EndOfBlock,
                                 QTextCursor::KeepAnchor);
                cur.insertText(QStringLiteral("☐ "));
                m_dirty = true;
                return true;
            }
            if (colInBlock == 5 && line == QStringLiteral("- [x]")) {
                cur.movePosition(QTextCursor::StartOfBlock);
                cur.movePosition(QTextCursor::EndOfBlock,
                                 QTextCursor::KeepAnchor);
                cur.insertText(QStringLiteral("✓ "));
                m_dirty = true;
                return true;
            }
        }
    }
    return QWidget::eventFilter(watched, event);
}

void NotesPanel::toggleCheckboxOnCurrentLine() {
    if (!m_editor || m_readError) return;   // M2 — keep the error notice intact
    QTextCursor cur = m_editor->textCursor();
    const QTextBlock block = cur.block();
    const QString line = block.text();
    cur.beginEditBlock();
    cur.setPosition(block.position());
    cur.movePosition(QTextCursor::EndOfBlock, QTextCursor::KeepAnchor);
    bool done = false;
    if (line.startsWith(QStringLiteral("☐ "))) {
        cur.insertText(QStringLiteral("✓ ") + line.mid(2));
        done = true;
    } else if (line.startsWith(QStringLiteral("✓ "))) {
        cur.insertText(QStringLiteral("☐ ") + line.mid(2));
    } else {
        cur.insertText(QStringLiteral("☐ ") + line);
    }
    cur.endEditBlock();
    applyChecklistDoneStyle(cur.block(), done);
    m_dirty = true;
}

void NotesPanel::insertCheckboxAtCursor() {
    if (!m_editor || m_readError) return;   // m_readError: keep the error notice intact
    // The checkbox is a LINE marker, not an inline glyph — normalize to the
    // start of the block so a mid-line caret turns the WHOLE line into a
    // checklist item (the existing text becomes the item text) instead of
    // dropping "☐ " into the middle of a word. No-op if the line already
    // carries a marker.
    QTextCursor cur = m_editor->textCursor();
    cur.movePosition(QTextCursor::StartOfBlock);
    const QString line = cur.block().text();
    if (!line.startsWith(QStringLiteral("☐ ")) &&
        !line.startsWith(QStringLiteral("✓ ")))
        cur.insertText(QStringLiteral("☐ "));
    m_editor->setFocus();
    m_dirty = true;
}

void NotesPanel::applyChecklistDoneStyle(const QTextBlock &block, bool done) {
    if (!m_editor || !block.isValid()) return;
    // Style only the TEXT after the "✓ " / "☐ " marker (2 chars). When done,
    // strike through + mute the line; when reopened, restore normal colour.
    // Rich-text formatting persists through saveNote (toHtml), so a checked
    // item stays struck after reload.
    const int start = block.position() + 2;
    const int end   = block.position() + block.length() - 1;  // drop block sep
    if (end <= start) return;                                  // empty line
    QTextCursor c(m_editor->document());
    c.setPosition(start);
    c.setPosition(end, QTextCursor::KeepAnchor);
    QTextCharFormat fmt;
    fmt.setFontStrikeOut(done);
    // A5 — themed runtime format only; the storage sanitizer strips inline
    // style attributes on save, so these colours never reach the file.
    fmt.setForeground(done ? QColor(m_pal.checkDoneFg)
                           : QColor(m_pal.checkUndoneFg));
    c.mergeCharFormat(fmt);
}

void NotesPanel::restyleChecklistLines() {
    // v0.1.98 — re-derive the done strike-through from the "✓ " marker on
    // load. The marker is plain text and survives save/reload, but the
    // rich-text strike-through format is dropped by the HTML sanitizer — so
    // without this, a checked item looked un-checked again after closing and
    // reopening the note. The ✓ marker is the source of truth.
    if (!m_editor) return;
    QTextDocument *doc = m_editor->document();
    for (QTextBlock b = doc->begin(); b.isValid(); b = b.next()) {
        // Only DONE lines need a baked format (muted ink + strike-through).
        // Undone (☐) lines are left UNSTYLED so they inherit the editor's
        // themed default ink — that lets them re-tint on a theme switch with
        // no per-line document edit (which would corrupt undo/redo).
        if (b.text().startsWith(QStringLiteral("✓ ")))
            applyChecklistDoneStyle(b, true);
    }
}

void NotesPanel::insertSubheader(const QString &titleIn, int level) {
    if (!m_editor || m_readError) return;   // M2 — keep the error notice intact
    QString title = titleIn;
    if (title.isEmpty()) {
        bool ok = false;
        title = QInputDialog::getText(this,
                                      tr("Insert H%1 header").arg(level),
                                      tr("Header text:"), QLineEdit::Normal,
                                      QString(), &ok).trimmed();
        if (!ok || title.isEmpty()) return;
    }

    QTextCursor cur = m_editor->textCursor();
    cur.beginEditBlock();

    // The header gets its own line. If the current line already has text,
    // start a fresh block; otherwise reuse the empty line.
    if (!cur.block().text().trimmed().isEmpty()) {
        cur.movePosition(QTextCursor::EndOfBlock);
        cur.insertBlock();
    } else {
        cur.movePosition(QTextCursor::StartOfBlock);
    }

    // REAL heading block (headingLevel + bold sized by level) — bold-only
    // text used to lose the header look on save+reload because toHtml()
    // never emitted <hN>. Leaves the cursor on a fresh PLAIN block below.
    NoterExtractApply::insertHeadingBlock(cur, title, level);

    // Always seed the section with a checkbox bullet in NORMAL format, so
    // every subheader comes with a checkable/cancellable line ready to type
    // (user: "always a checkbox as bullet point"). Enter then continues the
    // ☐ list via the editor's Key_Return handler.
    cur.insertText(QStringLiteral("☐ "));

    cur.endEditBlock();
    cur.movePosition(QTextCursor::EndOfBlock);
    m_editor->setTextCursor(cur);
    m_editor->setFocus();
    m_dirty = true;
    if (m_savedHint && !m_lastSaveFailed)   // M2 — don't mask NOT SAVED
        m_savedHint->setText(tr("editing… (auto-saves in 5s)"));
}

// ═══════════════════════════════════════════════════════════════════════
//  Pane toggles + nav
// ═══════════════════════════════════════════════════════════════════════

void NotesPanel::toggleSidebar() {
    if (!m_sidebar) return;
    const bool showNow = !m_sidebar->isVisible();
    m_sidebar->setVisible(showNow);
    // The slim left-edge strip is the mouse way back while hidden.
    if (m_sidebarRestoreBtn) m_sidebarRestoreBtn->setVisible(!showNow);
}

// v0.1.117 — export a note to PDF / Markdown. NoterExport shipped in
// v0.1.116 with zero call sites; wired to the sidebar note context menu
// and the editor right-click menu.
void NotesPanel::exportNoteTo(const QString &notePath, bool asPdf) {
    if (notePath.isEmpty()) return;
    // Flush live edits first so the export matches what's on screen.
    if (notePath == m_currentPath && m_dirty) saveCurrentNote();
    const QString out = NoterExport::chooseExportPath(
        this, notePath, asPdf ? QStringLiteral("pdf") : QStringLiteral("md"));
    if (out.isEmpty()) return;   // cancelled
    QString err;
    const bool ok = asPdf ? NoterExport::exportPdf(notePath, out, &err)
                          : NoterExport::exportMarkdown(notePath, out, &err);
    if (!ok) {
        noterMessageBox(this, m_pal, QMessageBox::Warning, tr("Export failed"),
                        err.isEmpty() ? tr("Could not export the note.") : err);
        return;
    }
    if (auto *sb = window() ? window()->findChild<QStatusBar *>() : nullptr)
        sb->showMessage(tr("Exported to %1")
                            .arg(QDir::toNativeSeparators(out)), 5000);
}

// v0.1.97 — the standalone NoterTodosPanel third pane is gone. Todos
// now live as their own root in the sidebar tree (see populateTodosRoot).
// The Ctrl+Alt+T shortcut focuses the Todos root instead of opening a
// separate pane.

// v0.1.97 — cross-platform reminder notification.
//
// QSystemTrayIcon is Qt's portable wrapper. On Linux it talks to
// org.freedesktop.Notifications over D-Bus (GNOME / KDE / xfce4-notifyd
// all implement this). On macOS Qt5 uses NSUserNotificationCenter; Qt6
// switches to UNUserNotificationCenter automatically. On Windows the
// call routes through Windows.UI.Notifications.ToastNotification (≥ 10).
//
// We lazy-construct a single static tray icon shared across the app
// (matching projectsearch.cpp's pattern). It stays alive for the
// session and survives the NotesPanel being torn down (e.g. tab
// close + reopen).
QSystemTrayIcon *NotesPanel::notificationTray() {
    if (!QSystemTrayIcon::isSystemTrayAvailable() ||
        !QSystemTrayIcon::supportsMessages())
        return nullptr;
    static QSystemTrayIcon *s_tray = nullptr;
    if (!s_tray) {
        QIcon appIcon = QApplication::windowIcon();
        if (appIcon.isNull())
            appIcon = QApplication::style()->standardIcon(QStyle::SP_MessageBoxInformation);
        s_tray = new QSystemTrayIcon(appIcon, qApp);
        s_tray->setToolTip(QStringLiteral("Notepatra"));
        s_tray->show();
        // Hide the tray icon footprint right after the platform registers
        // it — we only need it for the showMessage call, not visible to
        // the user. On most platforms hiding doesn't cancel pending
        // notifications.
        s_tray->hide();
        s_tray->show();
    }
    return s_tray;
}

bool NotesPanel::fireDesktopNotification(const QString &title, const QString &body) {
    if (QSystemTrayIcon *tray = notificationTray()) {
        tray->showMessage(title, body, QSystemTrayIcon::Information,
                          /*timeoutMs=*/8000);
        return true;
    }
    // No notification daemon available — fall back to a transient
    // status-bar message on the first top-level QMainWindow (static fn:
    // no `window()` to reach for, but in-app that QMainWindow IS the
    // panel's window). Better than silent; still a no-op headless.
    for (QWidget *top : QApplication::topLevelWidgets()) {
        if (auto *mw = qobject_cast<QMainWindow *>(top)) {
            if (auto *sb = mw->findChild<QStatusBar *>())
                sb->showMessage(QStringLiteral("%1: %2").arg(title, body), 8000);
            break;
        }
    }
    return false;
}

// App-lifetime reminder service replay — reminders whose desktop delivery
// failed while no panel existed land back in the in-window banner here.
void NotesPanel::replayReminders(const QVector<TodoRow> &rows) {
    for (const TodoRow &r : rows) enqueueReminder(r);
}

// v0.1.97 — reminder banner queue + flash.
void NotesPanel::enqueueReminder(const TodoRow &r) {
    // De-dupe — if the same id is already queued, don't stack it.
    for (const TodoRow &q : m_reminderQueue)
        if (q.id == r.id) return;
    m_reminderQueue.append(r);
    // If the banner isn't already showing one, show this.
    if (!m_reminderBanner || m_reminderBanner->isVisible()) return;
    showNextReminder();
}

void NotesPanel::showNextReminder() {
    if (!m_reminderBanner || !m_reminderLabel) return;
    if (m_reminderQueue.isEmpty()) { hideReminderBanner(); return; }
    const TodoRow &r = m_reminderQueue.first();
    QString text = tr("Reminder: %1").arg(r.text);
    if (m_reminderQueue.size() > 1)
        text += tr("    (+%1 more)").arg(m_reminderQueue.size() - 1);
    m_reminderLabel->setText(text);
    m_reminderOpenSrcBtn->setVisible(!r.sourceFile.isEmpty());

    // Make sure the editor page is the one showing so the banner is seen.
    if (m_rightStack && m_editorPage && !m_currentPath.isEmpty())
        showEditorPage();

    m_reminderBanner->setVisible(true);

    // Start (or restart) the flash timer — alternate the banner bg
    // between amber and a lighter amber every 600ms.
    if (!m_reminderFlashTimer) {
        m_reminderFlashTimer = new QTimer(this);
        connect(m_reminderFlashTimer, &QTimer::timeout, this, [this]() {
            if (!m_reminderBanner) return;
            // Strobe just long enough to catch the eye (~5 cycles), then
            // settle on the calm flash-A amber. The banner itself stays
            // visible until the user dismisses/snoozes it.
            if (++m_reminderFlashTicks >= 10) {
                m_reminderFlashTimer->stop();
                m_reminderFlashOn = true;   // settle on bannerFlashA
            } else {
                m_reminderFlashOn = !m_reminderFlashOn;
            }
            // A5 — read m_pal fresh each tick so a live theme switch
            // re-colours the flash without restarting the timer.
            const QString bg = m_reminderFlashOn ? m_pal.bannerFlashA
                                                 : m_pal.bannerFlashB;
            m_reminderBanner->setStyleSheet(QStringLiteral(
                "QWidget#noterReminderBanner { background: %1;"
                "  border-bottom: 2px solid %2; }").arg(bg, m_pal.bannerBorder));
        });
    }
    m_reminderFlashOn = false;
    m_reminderFlashTicks = 0;
    m_reminderFlashTimer->start(600);
}

void NotesPanel::hideReminderBanner() {
    if (m_reminderFlashTimer) m_reminderFlashTimer->stop();
    if (m_reminderBanner) m_reminderBanner->setVisible(false);
}

void NotesPanel::quickSwitchMeeting() {
    if (m_search) {
        m_search->setFocus();
        m_search->selectAll();
    }
}
