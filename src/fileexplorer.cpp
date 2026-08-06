// SPDX-License-Identifier: GPL-3.0-or-later

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
#include <QProxyStyle>
#include <QPainter>
#include <QApplication>

// v0.1.70 — SSMS Object Explorer-style `+/-` branch indicators for the
// file tree. SSMS uses small bordered +/- boxes (not Qt's default
// triangle arrows) for expand/collapse on database/table/view nodes.
// User asked for the same look on Notepatra's file tree.
//
// QProxyStyle intercepts the QStyle::PE_IndicatorBranch primitive and
// paints a 9×9 bordered box with a horizontal line (− for expanded)
// plus a vertical line (− → + for collapsed). Falls through to the
// default style for every other primitive so dock-tabs, scroll-bars,
// and selection highlights stay native.
class SsmsBranchStyle : public QProxyStyle {
public:
    using QProxyStyle::QProxyStyle;
    void drawPrimitive(PrimitiveElement element, const QStyleOption *option,
                       QPainter *painter, const QWidget *widget = nullptr) const override {
        if (element == PE_IndicatorBranch && option) {
            const bool hasChildren = option->state & QStyle::State_Children;
            if (!hasChildren) return;   // leaf node — no indicator

            painter->save();
            painter->setRenderHint(QPainter::Antialiasing, false);

            const QRect r = option->rect;
            const int cx = r.center().x();
            const int cy = r.center().y();
            const int sz = 4;   // half-edge of the +/- box

            QPen pen(option->palette.color(QPalette::Mid));
            pen.setWidth(1);
            painter->setPen(pen);
            painter->setBrush(option->palette.base());
            painter->drawRect(cx - sz, cy - sz, sz * 2, sz * 2);

            // Horizontal stroke (always — represents the minus in −).
            painter->drawLine(cx - sz + 2, cy, cx + sz - 2, cy);

            // Vertical stroke — present only when collapsed (turns − into +).
            const bool open = option->state & QStyle::State_Open;
            if (!open) {
                painter->drawLine(cx, cy - sz + 2, cx, cy + sz - 2);
            }
            painter->restore();
            return;
        }
        QProxyStyle::drawPrimitive(element, option, painter, widget);
    }
};

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
    // Display placeholder ONLY — deliberately leaves m_rootExplicit false, so
    // workspaceRoot() stays empty until the user opens a folder. Do not treat
    // this as a workspace; see the comment on workspaceRoot() in the header.
    m_rootPath = QDir::homePath();

    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(2);

    auto *header = new QHBoxLayout;
    header->setContentsMargins(4, 4, 4, 0);

    m_pathCombo = new QComboBox;
    m_pathCombo->setEditable(true);
    m_pathCombo->addItem(QDir::toNativeSeparators(QDir::homePath()));
    m_pathCombo->addItem(QDir::toNativeSeparators("/"));
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
    // v0.1.70 — SSMS Object Explorer-style +/- branch indicators (user
    // request: "like in ssms where we see database table views"). The
    // SsmsBranchStyle proxy paints a bordered +/- box for nodes with
    // children; leaf files get nothing (cleaner than Qt's default arrow).
    m_tree->setStyle(new SsmsBranchStyle(qApp->style()));
    layout->addWidget(m_tree);

    // Navigate on COMMIT only — a dropdown pick or Enter — never on
    // currentTextChanged.
    //
    // currentTextChanged fires on every keystroke of an editable combo, so
    // typing a path re-rooted the tree once per character AND, worse, latched
    // m_rootExplicit on whatever prefix happened to be a directory. Typing a
    // single "/" was enough to make "/" the user's "explicit workspace"
    // permanently — which then anchors the AI file sandbox at the filesystem
    // root and hands search_project the whole disk. The flag has no way back.
    //
    // Both signals below are user-commit events; neither can fire mid-typing.
    auto navigateTo = [this](const QString &path) {
        if (!QFileInfo(path).isDir()) return;
        m_rootPath = path;
        m_rootExplicit = true;  // a deliberate, committed choice
        m_model->setRootPath(path);
        m_tree->setRootIndex(m_proxy->mapFromSource(m_model->index(path)));
    };
    connect(m_pathCombo, QOverload<int>::of(&QComboBox::activated), this,
            [this, navigateTo](int) { navigateTo(m_pathCombo->currentText()); });
    if (auto *edit = m_pathCombo->lineEdit())
        connect(edit, &QLineEdit::returnPressed, this,
                [this, navigateTo] { navigateTo(m_pathCombo->currentText()); });

    connect(upBtn, &QPushButton::clicked, this, [this]() {
        QDir dir(m_rootPath);
        if (dir.cdUp())
            m_pathCombo->setCurrentText(QDir::toNativeSeparators(dir.absolutePath()));
    });

    connect(m_tree, &QTreeView::doubleClicked, this, [this](const QModelIndex &proxyIdx) {
        const QModelIndex srcIdx = m_proxy->mapToSource(proxyIdx);
        QString path = m_model->filePath(srcIdx);
        if (QFileInfo(path).isFile())
            emit fileOpenRequested(path);
        else if (QFileInfo(path).isDir())
            m_pathCombo->setCurrentText(QDir::toNativeSeparators(path));
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
        m_rootExplicit = true;  // Open Folder / restored workspace
        m_pathCombo->setCurrentText(QDir::toNativeSeparators(path));
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
