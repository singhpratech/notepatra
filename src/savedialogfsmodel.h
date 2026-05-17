// SPDX-License-Identifier: GPL-3.0-or-later

// v0.1.89 — proxy model that adds a "Date Created" column to QFileDialog's
// internal QFileSystemModel via QFileDialog::setProxyModel().
//
// v0.1.88 first attempt used a hand-rolled QSortFilterProxyModel that
// crashed Qt's tree view: index(row, extraCol) returned createIndex() with
// no source mapping → SIGSEGV on Ctrl+S. The supported pattern is
// QIdentityProxyModel (1:1 mapping for free) + overrides for columnCount,
// index, mapToSource, data, headerData on the extra column.
//
// Linux birth-time gotcha: QFileInfo::birthTime() needs ext4 + kernel ≥ 4.11
// (statx). Older filesystems return an invalid QDateTime — we fall back to
// metadataChangeTime() (ctime), then em-dash so the cell never crashes.

#pragma once

#include <QIdentityProxyModel>

class SaveDialogDateCreatedProxy : public QIdentityProxyModel {
    Q_OBJECT
public:
    explicit SaveDialogDateCreatedProxy(QObject *parent = nullptr);

    int columnCount(const QModelIndex &parent = {}) const override;
    QModelIndex index(int row, int column,
                      const QModelIndex &parent = {}) const override;
    QModelIndex sibling(int row, int column,
                        const QModelIndex &index) const override;
    QModelIndex mapToSource(const QModelIndex &proxyIndex) const override;
    QVariant data(const QModelIndex &proxyIndex,
                  int role = Qt::DisplayRole) const override;
    QVariant headerData(int section, Qt::Orientation orient,
                        int role = Qt::DisplayRole) const override;
};
