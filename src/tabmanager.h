// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef TABMANAGER_H
#define TABMANAGER_H

#include <QTabWidget>
#include <QMap>
#include <QColor>

class Editor;

class TabManager : public QTabWidget {
    Q_OBJECT
public:
    explicit TabManager(QWidget *parent = nullptr);
    Editor *currentEditor();
    Editor *editorAt(int index);

signals:
    void tabContextClose(int index);
    void tabContextCloseOthers(int index);
    void tabContextCloseLeft(int index);
    void tabContextCloseRight(int index);
    void tabContextCloseAll();
    void tabContextSave(int index);
    void tabContextSaveAs(int index);
    void tabContextRename(int index);
    void tabContextNew();

protected:
    bool eventFilter(QObject *obj, QEvent *event) override;

public:
    // Walk every tab and apply the tool-brand accent (red for Noter,
    // teal for REST, orange for Project Search, etc.). Called whenever
    // a new tab is added so the colored accent bar painted on top of
    // each tab matches the tool it hosts.
    void applyToolAccents();
    // Accent color for the tab at `index` (auto-assigned or user-picked).
    // Keyed by the tab's page widget so colors survive reorder / close.
    QColor tabAccentColor(int index) const { return m_tabColors.value(widget(index)); }

protected:
    void tabRemoved(int index) override;

private:
    void showTabContextMenu(int index, const QPoint &globalPos);
    void setTabColor(int index, const QColor &color);
    QMap<QWidget *, QColor> m_tabColors;
};

#endif
