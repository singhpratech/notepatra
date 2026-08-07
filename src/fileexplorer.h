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
    // The folder the TREE is currently displaying. Never empty — it starts at
    // the home directory so the widget has something to render. Use this only
    // for view concerns.
    QString rootPath() const { return m_rootPath; }

    // The folder the user EXPLICITLY opened, or "" if they never opened one.
    //
    // These two are different questions and conflating them caused a privacy
    // bug: search_project treated the display root as a workspace, so with no
    // folder open it walked the user's entire home directory and returned
    // line-level content from files they had never opened — including, in the
    // report that found this, the transcript of the session driving the tool.
    // Anything that decides "what is the user working on" MUST use this one,
    // and must treat "" as "nothing is scoped", not as "start at home".
    QString workspaceRoot() const {
        return m_rootExplicit ? m_rootPath : QString();
    }

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
    // False until the user actually picks a folder. The ctor's home-directory
    // seed does NOT set it — that is a placeholder for the view, not a choice.
    bool m_rootExplicit = false;

    // v0.1.61 — proxy that filters out anything in m_hiddenPaths so the
    // tree view doesn't render hidden entries. Operates on absolute file
    // paths so the filter survives directory navigation.
    class HiddenPathProxy;
    HiddenPathProxy *m_proxy;
};

#endif
