#ifndef FILEEXPLORER_H
#define FILEEXPLORER_H

#include <QWidget>
#include <QTreeView>
#include <QFileSystemModel>
#include <QComboBox>
#include <QLineEdit>

class FileExplorer : public QWidget {
    Q_OBJECT
public:
    explicit FileExplorer(QWidget *parent = nullptr);
    void setRoot(const QString &path);
    // Expose the current root so MainWindow can seed the AI workspace
    // context from it (so the AI knows *which* folder to reason about).
    QString rootPath() const { return m_rootPath; }

signals:
    void fileOpenRequested(const QString &path);

private:
    QFileSystemModel *m_model;
    QTreeView *m_tree;
    QComboBox *m_pathCombo;
    QString m_rootPath;
};

#endif
