// SPDX-License-Identifier: GPL-3.0-or-later

#include "savedialogfsmodel.h"

#include <QDateTime>
#include <QFileInfo>
#include <QFileSystemModel>

SaveDialogDateCreatedProxy::SaveDialogDateCreatedProxy(QObject *parent)
    : QIdentityProxyModel(parent) {}

int SaveDialogDateCreatedProxy::columnCount(const QModelIndex &parent) const {
    if (!sourceModel()) return 0;
    return sourceModel()->columnCount(mapToSource(parent)) + 1;
}

QModelIndex SaveDialogDateCreatedProxy::index(int row, int column,
                                               const QModelIndex &parent) const {
    if (!sourceModel()) return {};
    const int baseCols = sourceModel()->columnCount(mapToSource(parent));
    if (column < baseCols) {
        return QIdentityProxyModel::index(row, column, parent);
    }
    if (column == baseCols) {
        const QModelIndex sourceAnchor =
            sourceModel()->index(row, 0, mapToSource(parent));
        if (!sourceAnchor.isValid()) return {};
        return createIndex(row, column, sourceAnchor.internalPointer());
    }
    return {};
}

QModelIndex SaveDialogDateCreatedProxy::sibling(int row, int column,
                                                  const QModelIndex &idx) const {
    if (!idx.isValid()) return {};
    return index(row, column, idx.parent());
}

QModelIndex SaveDialogDateCreatedProxy::mapToSource(
    const QModelIndex &proxyIndex) const {
    if (!proxyIndex.isValid() || !sourceModel()) return {};
    const int baseCols = sourceModel()->columnCount({});
    if (proxyIndex.column() >= baseCols) {
        // Extra column maps to source column 0 (same row, same internalPointer)
        return sourceModel()->sibling(proxyIndex.row(), 0,
                                      QIdentityProxyModel::mapToSource(
                                          QIdentityProxyModel::index(
                                              proxyIndex.row(), 0,
                                              proxyIndex.parent())));
    }
    return QIdentityProxyModel::mapToSource(proxyIndex);
}

QVariant SaveDialogDateCreatedProxy::data(const QModelIndex &proxyIndex,
                                            int role) const {
    if (!proxyIndex.isValid() || !sourceModel()) return {};
    const int baseCols = sourceModel()->columnCount({});

    if (proxyIndex.column() == baseCols) {
        if (role == Qt::DisplayRole) {
            auto *fs = qobject_cast<QFileSystemModel *>(sourceModel());
            if (!fs) return {};
            const QFileInfo info = fs->fileInfo(mapToSource(proxyIndex));
            QDateTime created = info.birthTime();
            if (!created.isValid()) created = info.metadataChangeTime();
            if (!created.isValid()) return QStringLiteral("—");
            return created.toString(QStringLiteral("yyyy-MM-dd hh:mm"));
        }
        if (role == Qt::TextAlignmentRole) {
            return int(Qt::AlignLeft | Qt::AlignVCenter);
        }
        return {};
    }

    return QIdentityProxyModel::data(proxyIndex, role);
}

QVariant SaveDialogDateCreatedProxy::headerData(int section,
                                                  Qt::Orientation orient,
                                                  int role) const {
    if (!sourceModel()) return {};
    if (orient == Qt::Horizontal && role == Qt::DisplayRole &&
        section == sourceModel()->columnCount({})) {
        return QStringLiteral("Date Created");
    }
    return QIdentityProxyModel::headerData(section, orient, role);
}
