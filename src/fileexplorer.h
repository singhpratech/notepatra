// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef FILEEXPLORER_H
#define FILEEXPLORER_H

#include <QWidget>
#include <QTreeView>
#include <QFileSystemModel>
#include <QComboBox>
#include <QLineEdit>
#include <QSortFilterProxyModel>
#include <QSet>
#include <QString>

class FileExplorer : public QWidget {
    Q_OBJECT
public:
    explicit FileExplorer(QWidget *parent = nullptr);
    void setRoot(const QString &path);
    // Expose the current root so MainWindow can seed the AI workspace
    // context from it (so the AI knows *which* folder to reason about).
    QString rootPath() const { return m_rootPath; }

    // v0.1.61 — hidden-path filter. Right-click any tree node to add it
    // to the hidden set; "Show hidden" empties the set. Persisted across
    // sessions via Config::explorerHiddenPaths so users don't have to
    // re-hide vendor / node_modules etc. on every launch.
    QStringList hiddenPaths() const;
    void setHiddenPaths(const QStringList &paths);

signals:
    void fileOpenRequested(const QString &path);
    // v0.1.61 — fired after the user picks Hide / Show hidden from the
    // tree's context menu. MainWindow listens to persist the new list
    // into Config.
    void hiddenPathsChanged(const QStringList &paths);

private:
    QFileSystemModel *m_model;
    QTreeView *m_tree;
    QComboBox *m_pathCombo;
    QString m_rootPath;

    // v0.1.61 — proxy that filters out anything in m_hiddenPaths so the
    // tree view doesn't render hidden entries. Operates on absolute file
    // paths so the filter survives directory navigation.
    class HiddenPathProxy;
    HiddenPathProxy *m_proxy;
};

#endif
