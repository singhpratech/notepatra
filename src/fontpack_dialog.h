// v0.1.74 — Settings → Manage Fonts… dialog.
//
// Lists every entry from NotepatraFontPack::manifest() grouped by
// category. Each row is a checkable QTreeWidgetItem; the install
// button downloads checked-and-not-yet-installed fonts via
// NotepatraFontPack::Installer.

#ifndef NOTEPATRA_FONTPACK_DIALOG_H
#define NOTEPATRA_FONTPACK_DIALOG_H

#include <QDialog>

#include "fontpack.h"

class QTreeWidget;
class QTreeWidgetItem;
class QLabel;
class QProgressBar;
class QPushButton;

class FontPackDialog : public QDialog {
    Q_OBJECT
public:
    explicit FontPackDialog(QWidget *parent = nullptr);

private slots:
    void onInstallSelected();
    void onRemoveSelected();
    void onSelectAll();
    void onSelectNone();
    void onFontProgress(const NotepatraFontPack::Entry &e,
                        qint64 received, qint64 total);
    void onFontFinishedOne(const NotepatraFontPack::Entry &e,
                           bool ok, const QString &error);
    void onAllFinished();

private:
    void rebuildTree();
    void refreshRowStatus(QTreeWidgetItem *item);
    QList<NotepatraFontPack::Entry> selectedEntries() const;

    QTreeWidget                    *m_tree    = nullptr;
    QLabel                         *m_status  = nullptr;
    QProgressBar                   *m_progress= nullptr;
    QPushButton                    *m_btnInstall = nullptr;
    QPushButton                    *m_btnRemove  = nullptr;
    QPushButton                    *m_btnAll  = nullptr;
    QPushButton                    *m_btnNone = nullptr;

    NotepatraFontPack::Installer   *m_installer = nullptr;
    int                             m_totalToInstall = 0;
    int                             m_doneSoFar      = 0;
    QList<NotepatraFontPack::Entry> m_catalogue;
};

#endif // NOTEPATRA_FONTPACK_DIALOG_H
