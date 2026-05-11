#include "fontpack_dialog.h"

#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QMessageBox>
#include <QProgressBar>
#include <QPushButton>
#include <QTreeWidget>
#include <QTreeWidgetItem>
#include <QVBoxLayout>

using NotepatraFontPack::Category;
using NotepatraFontPack::Entry;
using NotepatraFontPack::Installer;

static QString categoryLabel(Category c) {
    switch (c) {
        case Category::CodeMono: return QObject::tr("Code · Monospace");
        case Category::UiSans:   return QObject::tr("UI · Sans-serif");
        case Category::Serif:    return QObject::tr("Serif · Prose");
        case Category::Display:  return QObject::tr("Display · Distinctive");
    }
    return QString();
}

static QString humanSize(qint64 bytes) {
    if (bytes <= 0) return QStringLiteral("—");
    const double kb = bytes / 1024.0;
    if (kb < 1024.0) return QStringLiteral("%1 KB").arg(kb, 0, 'f', 0);
    const double mb = kb / 1024.0;
    return QStringLiteral("%1 MB").arg(mb, 0, 'f', 1);
}

FontPackDialog::FontPackDialog(QWidget *parent)
    : QDialog(parent),
      m_installer(new Installer(this))
{
    setWindowTitle(tr("Manage Fonts"));
    resize(820, 560);

    auto *v = new QVBoxLayout(this);

    auto *hint = new QLabel(tr(
        "<b>Notepatra Font Pack</b> — install premium open-source fonts on demand. "
        "Files are downloaded to <code>%1</code> and registered with Qt immediately "
        "(no restart needed). All fonts are SIL OFL 1.1, Apache 2.0, or MIT — "
        "redistributable and royalty-free."
    ).arg(NotepatraFontPack::fontsDir()));
    hint->setWordWrap(true);
    v->addWidget(hint);

    m_tree = new QTreeWidget(this);
    m_tree->setColumnCount(6);
    m_tree->setHeaderLabels({tr("Font"), tr("Variant"), tr("Size"),
                             tr("License"), tr("Origin"), tr("Status")});
    m_tree->setRootIsDecorated(true);
    m_tree->setUniformRowHeights(true);
    m_tree->setSelectionMode(QAbstractItemView::NoSelection);
    m_tree->header()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    m_tree->header()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    m_tree->header()->setSectionResizeMode(2, QHeaderView::ResizeToContents);
    m_tree->header()->setSectionResizeMode(3, QHeaderView::ResizeToContents);
    m_tree->header()->setSectionResizeMode(4, QHeaderView::ResizeToContents);
    m_tree->header()->setSectionResizeMode(5, QHeaderView::Stretch);
    v->addWidget(m_tree, 1);

    auto *btnRow = new QHBoxLayout();
    m_btnAll  = new QPushButton(tr("Select all"));
    m_btnNone = new QPushButton(tr("Select none"));
    btnRow->addWidget(m_btnAll);
    btnRow->addWidget(m_btnNone);
    btnRow->addStretch();
    m_btnRemove  = new QPushButton(tr("Remove selected"));
    m_btnInstall = new QPushButton(tr("Install selected"));
    m_btnInstall->setDefault(true);
    btnRow->addWidget(m_btnRemove);
    btnRow->addWidget(m_btnInstall);
    v->addLayout(btnRow);

    m_progress = new QProgressBar(this);
    m_progress->setRange(0, 100);
    m_progress->setValue(0);
    m_progress->setVisible(false);
    v->addWidget(m_progress);

    m_status = new QLabel(this);
    m_status->setWordWrap(true);
    v->addWidget(m_status);

    auto *closeRow = new QHBoxLayout();
    closeRow->addStretch();
    auto *close = new QPushButton(tr("Close"));
    closeRow->addWidget(close);
    v->addLayout(closeRow);

    m_catalogue = NotepatraFontPack::manifest();
    rebuildTree();

    connect(m_btnAll,  &QPushButton::clicked, this, &FontPackDialog::onSelectAll);
    connect(m_btnNone, &QPushButton::clicked, this, &FontPackDialog::onSelectNone);
    connect(m_btnInstall, &QPushButton::clicked, this, &FontPackDialog::onInstallSelected);
    connect(m_btnRemove,  &QPushButton::clicked, this, &FontPackDialog::onRemoveSelected);
    connect(close, &QPushButton::clicked, this, &QDialog::accept);

    connect(m_installer, &Installer::progressOne,   this, &FontPackDialog::onFontProgress);
    connect(m_installer, &Installer::finishedOne,   this, &FontPackDialog::onFontFinishedOne);
    connect(m_installer, &Installer::finishedAll,   this, &FontPackDialog::onAllFinished);
}

void FontPackDialog::rebuildTree() {
    m_tree->clear();

    // Group catalogue by category, preserving manifest order.
    QMap<Category, QTreeWidgetItem*> roots;
    const QList<Category> order{Category::CodeMono, Category::UiSans,
                                Category::Serif,    Category::Display};
    for (Category c : order) {
        auto *root = new QTreeWidgetItem(m_tree);
        root->setText(0, categoryLabel(c));
        QFont rf = root->font(0);
        rf.setBold(true);
        root->setFont(0, rf);
        root->setFirstColumnSpanned(true);
        root->setExpanded(true);
        roots.insert(c, root);
    }

    for (int i = 0; i < m_catalogue.size(); ++i) {
        const Entry &e = m_catalogue.at(i);
        auto *root = roots.value(e.category);
        if (!root) continue;
        auto *it = new QTreeWidgetItem(root);
        it->setData(0, Qt::UserRole, i);  // index into m_catalogue
        it->setFlags(it->flags() | Qt::ItemIsUserCheckable);
        it->setCheckState(0, Qt::Unchecked);
        it->setText(0, e.family);
        it->setText(1, e.variant);
        it->setText(2, humanSize(e.approxSize));
        it->setText(3, e.license);
        it->setText(4, e.origin);
        refreshRowStatus(it);
    }
}

void FontPackDialog::refreshRowStatus(QTreeWidgetItem *item) {
    const int idx = item->data(0, Qt::UserRole).toInt();
    if (idx < 0 || idx >= m_catalogue.size()) return;
    const Entry &e = m_catalogue.at(idx);
    const bool installed = NotepatraFontPack::isInstalled(e);
    item->setText(5, installed ? tr("Installed") : tr("Not installed"));
    if (installed) {
        QFont f = item->font(5);
        f.setBold(true);
        item->setFont(5, f);
    }
}

QList<Entry> FontPackDialog::selectedEntries() const {
    QList<Entry> out;
    for (int i = 0; i < m_tree->topLevelItemCount(); ++i) {
        auto *root = m_tree->topLevelItem(i);
        for (int j = 0; j < root->childCount(); ++j) {
            auto *it = root->child(j);
            if (it->checkState(0) != Qt::Checked) continue;
            const int idx = it->data(0, Qt::UserRole).toInt();
            if (idx >= 0 && idx < m_catalogue.size())
                out.append(m_catalogue.at(idx));
        }
    }
    return out;
}

void FontPackDialog::onSelectAll() {
    for (int i = 0; i < m_tree->topLevelItemCount(); ++i) {
        auto *root = m_tree->topLevelItem(i);
        for (int j = 0; j < root->childCount(); ++j) {
            root->child(j)->setCheckState(0, Qt::Checked);
        }
    }
}

void FontPackDialog::onSelectNone() {
    for (int i = 0; i < m_tree->topLevelItemCount(); ++i) {
        auto *root = m_tree->topLevelItem(i);
        for (int j = 0; j < root->childCount(); ++j) {
            root->child(j)->setCheckState(0, Qt::Unchecked);
        }
    }
}

void FontPackDialog::onInstallSelected() {
    const QList<Entry> selected = selectedEntries();
    if (selected.isEmpty()) {
        QMessageBox::information(this, tr("No fonts selected"),
            tr("Tick the fonts you want to install, then click Install."));
        return;
    }
    QList<Entry> toFetch;
    for (const auto &e : selected) {
        if (!NotepatraFontPack::isInstalled(e)) toFetch.append(e);
    }
    if (toFetch.isEmpty()) {
        m_status->setText(tr("Selected fonts are already installed."));
        return;
    }
    m_totalToInstall = toFetch.size();
    m_doneSoFar = 0;
    m_progress->setVisible(true);
    m_progress->setRange(0, 100);
    m_progress->setValue(0);
    m_btnInstall->setEnabled(false);
    m_btnRemove->setEnabled(false);
    m_status->setText(tr("Downloading %1 font(s)…").arg(m_totalToInstall));
    m_installer->install(toFetch);
}

void FontPackDialog::onRemoveSelected() {
    const QList<Entry> selected = selectedEntries();
    if (selected.isEmpty()) {
        QMessageBox::information(this, tr("No fonts selected"),
            tr("Tick the fonts you want to remove, then click Remove."));
        return;
    }
    int removed = 0;
    for (const auto &e : selected) {
        if (NotepatraFontPack::isInstalled(e) &&
            NotepatraFontPack::uninstall(e)) {
            ++removed;
        }
    }
    rebuildTree();
    m_status->setText(tr("Removed %1 font(s). Currently-rendered text keeps the previous family until you restart Notepatra.").arg(removed));
}

void FontPackDialog::onFontProgress(const Entry &e,
                                    qint64 received, qint64 total) {
    if (total <= 0) {
        m_progress->setRange(0, 0);
        return;
    }
    // Compose overall progress: completed fonts + fractional current.
    const double current = double(received) / double(total);
    const double overall = (m_doneSoFar + current) / double(m_totalToInstall);
    m_progress->setRange(0, 100);
    m_progress->setValue(int(overall * 100.0));
    m_status->setText(tr("Downloading %1 %2 — %3 / %4")
                      .arg(e.family, e.variant,
                           humanSize(received), humanSize(total)));
}

void FontPackDialog::onFontFinishedOne(const Entry &e,
                                       bool ok, const QString &error) {
    ++m_doneSoFar;
    if (!ok) {
        m_status->setText(tr("Failed: %1 %2 — %3")
                          .arg(e.family, e.variant, error));
    }
}

void FontPackDialog::onAllFinished() {
    m_progress->setVisible(false);
    m_btnInstall->setEnabled(true);
    m_btnRemove->setEnabled(true);
    rebuildTree();
    m_status->setText(tr("Done — installed %1 font(s).").arg(m_doneSoFar));
    m_doneSoFar = 0;
    m_totalToInstall = 0;
}
