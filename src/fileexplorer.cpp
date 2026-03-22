#include "fileexplorer.h"
#include <QHeaderView>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QPushButton>
#include <QDir>
#include <QFileInfo>

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

    m_tree = new QTreeView;
    m_tree->setModel(m_model);
    m_tree->setRootIndex(m_model->index(m_rootPath));
    m_tree->setAnimated(true);
    m_tree->setSortingEnabled(true);
    m_tree->setColumnHidden(1, true);
    m_tree->setColumnHidden(2, true);
    m_tree->setColumnHidden(3, true);
    m_tree->header()->setVisible(false);
    layout->addWidget(m_tree);

    connect(m_pathCombo, &QComboBox::currentTextChanged, this, [this](const QString &path) {
        if (QFileInfo(path).isDir()) {
            m_rootPath = path;
            m_model->setRootPath(path);
            m_tree->setRootIndex(m_model->index(path));
        }
    });

    connect(upBtn, &QPushButton::clicked, this, [this]() {
        QDir dir(m_rootPath);
        if (dir.cdUp())
            m_pathCombo->setCurrentText(dir.absolutePath());
    });

    connect(m_tree, &QTreeView::doubleClicked, this, [this](const QModelIndex &index) {
        QString path = m_model->filePath(index);
        if (QFileInfo(path).isFile())
            emit fileOpenRequested(path);
        else if (QFileInfo(path).isDir())
            m_pathCombo->setCurrentText(path);
    });
}

void FileExplorer::setRoot(const QString &path) {
    if (QFileInfo(path).isDir()) {
        m_rootPath = path;
        m_pathCombo->setCurrentText(path);
    }
}
