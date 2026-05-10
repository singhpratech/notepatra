#include "fileexplorer.h"
#include <QHeaderView>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QPushButton>
#include <QDir>
#include <QFileInfo>
#include <QMenu>
#include <QAction>
#include <QSortFilterProxyModel>

// v0.1.61 — proxy that hides entries whose absolute path is in m_hidden.
// Subclass kept inside the .cpp because nothing else needs to touch it;
// only FileExplorer constructs / mutates the hidden set.
class FileExplorer::HiddenPathProxy : public QSortFilterProxyModel {
public:
    explicit HiddenPathProxy(QObject *parent = nullptr)
        : QSortFilterProxyModel(parent) {}

    void setHidden(const QSet<QString> &paths) {
        m_hidden = paths;
        invalidateFilter();
    }
    const QSet<QString> &hidden() const { return m_hidden; }

    void addHidden(const QString &absPath) {
        m_hidden.insert(absPath);
        invalidateFilter();
    }
    void clearHidden() {
        m_hidden.clear();
        invalidateFilter();
    }

protected:
    bool filterAcceptsRow(int sourceRow, const QModelIndex &sourceParent) const override {
        if (m_hidden.isEmpty()) return true;
        auto *fs = qobject_cast<QFileSystemModel *>(sourceModel());
        if (!fs) return true;
        const QModelIndex idx = fs->index(sourceRow, 0, sourceParent);
        return !m_hidden.contains(fs->filePath(idx));
    }

private:
    QSet<QString> m_hidden;
};

FileExplorer::FileExplorer(QWidget *parent) : QWidget(parent) {
    m_rootPath = QDir::homePath();

    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(2);

    auto *header = new QHBoxLayout;
    header->setContentsMargins(4, 4, 4, 0);

    m_pathCombo = new QComboBox;
    m_pathCombo->setEditable(true);
    m_pathCombo->addItem(QDir::homePath());
    m_pathCombo->addItem("/");
    header->addWidget(m_pathCombo, 1);

    auto *upBtn = new QPushButton("↑");
    upBtn->setFixedWidth(30);
    header->addWidget(upBtn);
    layout->addLayout(header);

    m_model = new QFileSystemModel(this);
    m_model->setRootPath(m_rootPath);
    m_model->setFilter(QDir::AllEntries | QDir::NoDotAndDotDot | QDir::Hidden);

    m_proxy = new HiddenPathProxy(this);
    m_proxy->setSourceModel(m_model);

    m_tree = new QTreeView;
    m_tree->setModel(m_proxy);
    m_tree->setRootIndex(m_proxy->mapFromSource(m_model->index(m_rootPath)));
    m_tree->setAnimated(true);
    m_tree->setSortingEnabled(true);
    m_tree->setColumnHidden(1, true);
    m_tree->setColumnHidden(2, true);
    m_tree->setColumnHidden(3, true);
    m_tree->header()->setVisible(false);
    m_tree->setContextMenuPolicy(Qt::CustomContextMenu);
    layout->addWidget(m_tree);

    connect(m_pathCombo, &QComboBox::currentTextChanged, this, [this](const QString &path) {
        if (QFileInfo(path).isDir()) {
            m_rootPath = path;
            m_model->setRootPath(path);
            m_tree->setRootIndex(m_proxy->mapFromSource(m_model->index(path)));
        }
    });

    connect(upBtn, &QPushButton::clicked, this, [this]() {
        QDir dir(m_rootPath);
        if (dir.cdUp())
            m_pathCombo->setCurrentText(dir.absolutePath());
    });

    connect(m_tree, &QTreeView::doubleClicked, this, [this](const QModelIndex &proxyIdx) {
        const QModelIndex srcIdx = m_proxy->mapToSource(proxyIdx);
        QString path = m_model->filePath(srcIdx);
        if (QFileInfo(path).isFile())
            emit fileOpenRequested(path);
        else if (QFileInfo(path).isDir())
            m_pathCombo->setCurrentText(path);
    });

    // v0.1.61 — right-click menu: Hide adds the clicked node's absolute
    // path to the proxy filter; Show hidden empties the filter. Both
    // actions emit hiddenPathsChanged so MainWindow can persist via
    // Config. Always-visible items (no selection) still get a "Show
    // hidden" entry so the user can recover without right-clicking on a
    // node that's already in the visible set.
    connect(m_tree, &QTreeView::customContextMenuRequested,
            this, [this](const QPoint &pos) {
        QMenu menu;
        const QModelIndex proxyIdx = m_tree->indexAt(pos);
        QString targetPath;
        if (proxyIdx.isValid()) {
            const QModelIndex srcIdx = m_proxy->mapToSource(proxyIdx);
            targetPath = m_model->filePath(srcIdx);
            QAction *hideAct = menu.addAction(tr("Hide \"%1\"")
                .arg(QFileInfo(targetPath).fileName()));
            connect(hideAct, &QAction::triggered, this, [this, targetPath]() {
                if (targetPath.isEmpty()) return;
                m_proxy->addHidden(targetPath);
                emit hiddenPathsChanged(hiddenPaths());
            });
        }
        if (!m_proxy->hidden().isEmpty()) {
            QAction *showAllAct = menu.addAction(tr("Show hidden (%1)")
                .arg(m_proxy->hidden().size()));
            connect(showAllAct, &QAction::triggered, this, [this]() {
                m_proxy->clearHidden();
                emit hiddenPathsChanged(hiddenPaths());
            });
        }
        if (!menu.isEmpty())
            menu.exec(m_tree->viewport()->mapToGlobal(pos));
    });
}

void FileExplorer::setRoot(const QString &path) {
    if (QFileInfo(path).isDir()) {
        m_rootPath = path;
        m_pathCombo->setCurrentText(path);
    }
}

QStringList FileExplorer::hiddenPaths() const {
    QStringList out;
    for (const QString &p : m_proxy->hidden()) out.append(p);
    return out;
}

void FileExplorer::setHiddenPaths(const QStringList &paths) {
    QSet<QString> s;
    for (const QString &p : paths) s.insert(p);
    m_proxy->setHidden(s);
}
