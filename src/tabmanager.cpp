// SPDX-License-Identifier: GPL-3.0-or-later

#include "tabmanager.h"
#include "editor.h"
#include "tool_colors.h"
#include <QEvent>
#include <QMouseEvent>
#include <QTabBar>
#include <QMenu>
#include <QAction>
#include <QApplication>
#include <QClipboard>
#include <QDir>
#include <QFileInfo>
#include <QInputDialog>
#include <QColorDialog>
#include <QProcess>
#include <QDesktopServices>
#include <QUrl>
#include <QPainter>
#include <QStylePainter>
#include <QStyleOptionTab>

// ─── AccentTabBar — paints a 3 px tool-color strip on top of each tab.
// Falls back to default QTabBar rendering when no accent is set.
class AccentTabBar : public QTabBar {
public:
    explicit AccentTabBar(TabManager *owner) : QTabBar(owner), m_owner(owner) {}
protected:
    void paintEvent(QPaintEvent *) override {
        // Crash fix: one QPainter only. Reuse the QStylePainter which IS a
        // QPainter under the hood. Opening a second `QPainter(this)` while
        // sp is alive caused SIGSEGV on tab clicks.
        QStylePainter sp(this);
        for (int i = 0; i < count(); ++i) {
            QStyleOptionTab opt;
            initStyleOption(&opt, i);
            sp.drawControl(QStyle::CE_TabBarTab, opt);
        }
        // Second pass over the same painter — draw accent strips on top
        // of the tabs that have a color.
        for (int i = 0; i < count(); ++i) {
            const QColor accent = m_owner ? m_owner->tabAccentColor(i) : QColor();
            if (!accent.isValid()) continue;
            const QRect r = tabRect(i);
            sp.fillRect(r.left(), r.top(), r.width(), 3, accent);
        }
    }
private:
    TabManager *m_owner = nullptr;
};

TabManager::TabManager(QWidget *parent) : QTabWidget(parent) {
    // Install the custom AccentTabBar that paints tool-brand color strips.
    setTabBar(new AccentTabBar(this));
    setTabsClosable(true);
    setMovable(true);
    setDocumentMode(true);
    setUsesScrollButtons(true);
    tabBar()->setExpanding(false);
    tabBar()->installEventFilter(this);
    tabBar()->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(tabBar(), &QWidget::customContextMenuRequested, this, [this](const QPoint &pos) {
        int idx = tabBar()->tabAt(pos);
        if (idx >= 0) showTabContextMenu(idx, tabBar()->mapToGlobal(pos));
    });
    // Apply tool accents on every tab add / remove / rename so the
    // color stays in sync with what each tab actually contains.
    // Only listen to currentChanged — tabBarClicked + currentChanged
    // can recurse via setTabTextColor → update → next-event → click loop.
    connect(this, &QTabWidget::currentChanged, this, [this](int) { applyToolAccents(); });
}

Editor *TabManager::currentEditor() {
    return qobject_cast<Editor *>(currentWidget());
}

Editor *TabManager::editorAt(int index) {
    return qobject_cast<Editor *>(widget(index));
}

bool TabManager::eventFilter(QObject *obj, QEvent *event) {
    if (obj == tabBar()) {
        if (event->type() == QEvent::MouseButtonDblClick) {
            auto *me = static_cast<QMouseEvent *>(event);
            if (tabBar()->tabAt(me->pos()) == -1) {
                emit tabContextNew();
                return true;
            }
        } else if (event->type() == QEvent::MouseButtonRelease) {
            auto *me = static_cast<QMouseEvent *>(event);
            if (me->button() == Qt::MiddleButton) {
                int idx = tabBar()->tabAt(me->pos());
                if (idx >= 0) {
                    emit tabCloseRequested(idx);
                    return true;
                }
            }
        }
    }
    return QTabWidget::eventFilter(obj, event);
}

void TabManager::showTabContextMenu(int index, const QPoint &globalPos) {
    auto *editor = editorAt(index);
    QMenu menu;

    // ── Close options ──
    menu.addAction("Close", this, [this, index]() { emit tabContextClose(index); });

    auto *closeMenu = menu.addMenu("Close Multiple");
    closeMenu->addAction("Close All BUT This", this, [this, index]() { emit tabContextCloseOthers(index); });
    closeMenu->addAction("Close All to the Left", this, [this, index]() { emit tabContextCloseLeft(index); });
    closeMenu->addAction("Close All to the Right", this, [this, index]() { emit tabContextCloseRight(index); });
    closeMenu->addAction("Close All", this, [this]() { emit tabContextCloseAll(); });

    menu.addSeparator();

    // ── Save options ──
    menu.addAction("Save", this, [this, index]() { emit tabContextSave(index); });
    menu.addAction("Save As...", this, [this, index]() { emit tabContextSaveAs(index); });

    menu.addSeparator();

    // ── Rename ──
    menu.addAction("Rename...", this, [this, index]() { emit tabContextRename(index); });

    menu.addSeparator();

    if (editor && !editor->filePath().isEmpty()) {
        // ── Copy to Clipboard — flat, not submenu ──
        menu.addAction("Copy Full Path", this, [editor]() {
            QApplication::clipboard()->setText(QDir::toNativeSeparators(editor->filePath()));
        });
        menu.addAction("Copy Filename", this, [editor]() {
            QApplication::clipboard()->setText(QFileInfo(editor->filePath()).fileName());
        });
        menu.addAction("Copy Directory Path", this, [editor]() {
            QApplication::clipboard()->setText(QDir::toNativeSeparators(QFileInfo(editor->filePath()).path()));
        });

        menu.addSeparator();

        // ── Open Into ──
        menu.addAction("Open Containing Folder", this, [editor]() {
            QDesktopServices::openUrl(QUrl::fromLocalFile(QFileInfo(editor->filePath()).path()));
        });
        menu.addAction("Open Terminal Here", this, [editor]() {
            QString dir = QFileInfo(editor->filePath()).path();
#ifdef Q_OS_WIN
            QProcess::startDetached("cmd.exe", {"/k", "cd /d " + dir}, dir);
#elif defined(Q_OS_MAC)
            QProcess::startDetached("open", {"-a", "Terminal", dir});
#else
            QProcess::startDetached("x-terminal-emulator", {}, dir);
#endif
        });
    }

    menu.addSeparator();

    // ── Read-Only ──
    if (editor) {
        auto *readOnly = menu.addAction("Read-Only");
        readOnly->setCheckable(true);
        readOnly->setChecked(editor->isReadOnly());
        connect(readOnly, &QAction::triggered, this, [editor](bool checked) {
            editor->setReadOnly(checked);
        });
    }

    menu.addSeparator();

    // ── Color Tag ──
    auto *colorMenu = menu.addMenu("Apply Color to Tab");

    struct ColorEntry { const char *name; const char *hex; };
    ColorEntry colors[] = {
        {"Red",    "#FF6B6B"},
        {"Orange", "#FFA94D"},
        {"Yellow", "#FFD43B"},
        {"Green",  "#69DB7C"},
        {"Blue",   "#74C0FC"},
        {"Purple", "#B197FC"},
        {"Pink",   "#F783AC"},
    };

    for (auto &c : colors) {
        auto *act = colorMenu->addAction(c.name);
        QPixmap px(16, 16);
        px.fill(QColor(c.hex));
        act->setIcon(QIcon(px));
        connect(act, &QAction::triggered, this, [this, index, c]() {
            setTabColor(index, QColor(c.hex));
        });
    }

    colorMenu->addSeparator();
    colorMenu->addAction("Custom Color...", this, [this, index]() {
        QColor color = QColorDialog::getColor(Qt::white, this, "Pick Tab Color");
        if (color.isValid()) setTabColor(index, color);
    });
    colorMenu->addAction("Remove Color", this, [this, index]() {
        setTabColor(index, QColor());
    });

    menu.exec(globalPos);
}

void TabManager::setTabColor(int index, const QColor &color) {
    if (color.isValid()) {
        m_tabColors[index] = color;
        tabBar()->setTabTextColor(index, color.darker(120));
    } else {
        m_tabColors.remove(index);
        tabBar()->setTabTextColor(index, QColor());
    }
    tabBar()->update();
}

// ─── Auto tool-accent ─────────────────────────────────────────────────
// Inspect the tab's widget + label to decide which Notepatra tool it
// belongs to, then assign the brand color for that tool. Painted as a
// 3 px strip at the top of the tab by tabBar()'s paintEvent override.
//
// The actual hex → tool mapping lives in src/tool_colors.cpp so the
// feature-toolbar icons and the Welcome cards source from the SAME
// table — the strip, the toolbar button, and the Welcome card for any
// given tool are guaranteed to render the same colour.
static inline QColor toolAccentForText(const QString &label) {
    return notepatraToolAccent(label);
}

void TabManager::applyToolAccents() {
    for (int i = 0; i < count(); ++i) {
        const QColor c = toolAccentForText(tabText(i));
        if (c.isValid()) {
            m_tabColors[i] = c;
            tabBar()->setTabTextColor(i, c.darker(125));
        }
    }
    tabBar()->update();
}
