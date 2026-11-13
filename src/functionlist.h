// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef FUNCTIONLIST_H
#define FUNCTIONLIST_H

#include <QWidget>
#include <QTreeWidget>
#include <QVector>

class FunctionList : public QWidget {
    Q_OBJECT
public:
    explicit FunctionList(QWidget *parent = nullptr);
    void updateSymbols(const QString &text, const QString &language);

    // The parse the dock shows, without the dock: kind is "class" or
    // "function", line is 1-based. (Also used by MCP list_symbols.)
    struct Symbol { QString name; QString kind; int line; };
    static QVector<Symbol> parseSymbols(const QString &text, const QString &language);

signals:
    void navigateRequested(int line);

private:
    QTreeWidget *m_tree;
};

#endif
