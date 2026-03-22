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

private:
    void showTabContextMenu(int index, const QPoint &globalPos);
    void setTabColor(int index, const QColor &color);
    QMap<int, QColor> m_tabColors;
};

#endif
