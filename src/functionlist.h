#ifndef FUNCTIONLIST_H
#define FUNCTIONLIST_H

#include <QWidget>
#include <QTreeWidget>

class FunctionList : public QWidget {
    Q_OBJECT
public:
    explicit FunctionList(QWidget *parent = nullptr);
    void updateSymbols(const QString &text, const QString &language);

signals:
    void navigateRequested(int line);

private:
    QTreeWidget *m_tree;
};

#endif
