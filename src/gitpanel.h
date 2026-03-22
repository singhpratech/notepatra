#ifndef GITPANEL_H
#define GITPANEL_H

#include <QWidget>
#include <QTreeWidget>
#include <QLabel>
#include <QProcess>

class GitPanel : public QWidget {
    Q_OBJECT
public:
    explicit GitPanel(QWidget *parent = nullptr);
    void refresh(const QString &filePath);

signals:
    void fileClicked(const QString &path);

private:
    QLabel *m_branchLabel;
    QLabel *m_statusLabel;
    QTreeWidget *m_tree;
    QString m_repoRoot;

    void runGit(const QStringList &args, std::function<void(const QString &)> callback);
};

#endif
