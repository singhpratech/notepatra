// SPDX-License-Identifier: GPL-3.0-or-later
//
// Implementation of the three Noter slide-over panels. Each panel is a
// QFrame with a fixed left-aligned width — the integrator stacks them
// inside a QStackedWidget that overlays the canvas at z-index above
// the editor. See notes_panels.h for the data-contract structs the
// panels consume.

#include "notes_panels.h"

#include <QAbstractItemModel>
#include <QApplication>
#include <QCheckBox>
#include <QDate>
#include <QDateTime>
#include <QDir>
#include <QFileInfo>
#include <QFileSystemModel>
#include <QHBoxLayout>
#include <QContextMenuEvent>
#include <QDateTimeEdit>
#include <QDialog>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QHeaderView>
#include <QInputDialog>
#include <QKeyEvent>
#include <QLabel>
#include <QMenu>
#include <QMessageBox>
#include <QLineEdit>
#include <QModelIndex>
#include <QMouseEvent>
#include <QPushButton>
#include <QScrollArea>
#include <QSortFilterProxyModel>
#include <QStackedWidget>
#include <QStyle>
#include <QToolButton>
#include <QTreeView>
#include <QVBoxLayout>
#include <QVariant>

// ─── small shared style helpers ─────────────────────────────────────
// Kept local rather than reaching into npp_palette because the slide-
// overs are layered ON TOP of the canvas — they get their own panel-
// surface color (light translucent grey) regardless of editor theme.
// Theme polish is a follow-up; for v1 the panels just look readable.

static QString s_panelSurface() {
    // Soft white-paper background — design SURFACE 04/06/07 reference.
    // v0.1.95+ — also styles QMenu spawned from this panel's children so
    // right-click context menus aren't dark-on-dark in dark themes.
    // (Per feedback_qmenu_cascade_through_widget_qss memory rule.)
    return QStringLiteral(
        "QFrame { background: #FAFAF7; color: #1F2933; }"
        "QMenu { background: white; color: #111827; border: 1px solid #d5d0c0;"
        "  padding: 4px 0; }"
        "QMenu::item { padding: 6px 22px 6px 18px; }"
        "QMenu::item:selected { background: #FEF3C7; color: #0a0d12; }"
        "QMenu::separator { height: 1px; background: #e5e1d6; margin: 4px 8px; }"
    );
}

static QString s_groupHeaderStyle(const QString &accentHex) {
    // Small caps style with a tiny accent square on the left edge.
    return QStringLiteral(
        "QLabel { font-weight: 600; font-size: 11px; letter-spacing: 1px; "
        "text-transform: uppercase; color: #4B5563; padding: 4px 0; "
        "border-left: 3px solid %1; padding-left: 8px; }"
    ).arg(accentHex);
}

static QString s_countChipStyle() {
    return QStringLiteral(
        "QLabel { background: #E5E7EB; color: #374151; "
        "padding: 1px 8px; border-radius: 9px; "
        "font-size: 11px; font-weight: 600; min-width: 14px; }"
    );
}

static QString s_searchInputStyle() {
    return QStringLiteral(
        "QLineEdit { padding: 6px 8px; border: 1px solid #D1D5DB; "
        "border-radius: 6px; background: #FFFFFF; color: #111827; }"
        "QLineEdit:focus { border: 1px solid #6366F1; }"
    );
}

// Build the standard header row used by all three panels — icon,
// title label, count chip on the right, and a close × button.
static void buildHeaderRow(QFrame *panel, QVBoxLayout *outer,
                           const QString &titleText,
                           QLabel **titleOut, QLabel **countOut,
                           QPushButton **closeOut) {
    QWidget *header = new QWidget(panel);
    QHBoxLayout *row = new QHBoxLayout(header);
    row->setContentsMargins(12, 10, 12, 10);
    row->setSpacing(8);

    QLabel *icon = new QLabel(header);
    icon->setPixmap(panel->style()
        ->standardIcon(QStyle::SP_DirIcon)
        .pixmap(16, 16));
    row->addWidget(icon, 0);

    QLabel *title = new QLabel(titleText, header);
    title->setStyleSheet("QLabel { font-weight: 600; font-size: 13px; "
                         "color: #111827; }");
    row->addWidget(title, 0);

    QLabel *count = new QLabel(QStringLiteral("0"), header);
    count->setStyleSheet(s_countChipStyle());
    count->setAlignment(Qt::AlignCenter);
    row->addWidget(count, 0);

    row->addStretch(1);

    QPushButton *close = new QPushButton(header);
    close->setIcon(panel->style()->standardIcon(QStyle::SP_TitleBarCloseButton));
    close->setFlat(true);
    close->setFixedSize(22, 22);
    close->setCursor(Qt::PointingHandCursor);
    close->setToolTip(QObject::tr("Close panel"));
    row->addWidget(close, 0);

    outer->addWidget(header, 0);

    if (titleOut) *titleOut = title;
    if (countOut) *countOut = count;
    if (closeOut) *closeOut = close;
}

// ═══════════════════════════════════════════════════════════════════════
// NoterNotebooksPanel
//
// Filesystem tree of ~/Documents/Notepatra/Noter/. Only *.html files
// are shown; subfolders count as sections. Double-click → open note.
// ═══════════════════════════════════════════════════════════════════════

// Small QSortFilterProxyModel that hides everything that isn't *.html
// AND isn't a directory. We need this on top of QFileSystemModel's
// own nameFilter mechanism because the latter dims non-matching files
// instead of hiding them — bad UX inside a sidebar.
class NotebooksFilterProxy : public QSortFilterProxyModel {
public:
    explicit NotebooksFilterProxy(QObject *parent = nullptr)
        : QSortFilterProxyModel(parent) {}

    void setSearchPhrase(const QString &phrase) {
        m_phrase = phrase.trimmed().toLower();
        invalidateFilter();
    }

protected:
    bool filterAcceptsRow(int sourceRow,
                          const QModelIndex &sourceParent) const override {
        QFileSystemModel *fs =
            qobject_cast<QFileSystemModel *>(sourceModel());
        if (!fs) return true;
        QModelIndex idx = fs->index(sourceRow, 0, sourceParent);
        if (!idx.isValid()) return false;

        const QString name = fs->fileName(idx);
        const bool isDir = fs->isDir(idx);

        if (isDir) {
            // Always show directories — but if there's a search phrase,
            // only show ones whose subtree contains a match. We approx
            // this cheaply by always showing dirs; the user collapses
            // empty ones themselves.
            return true;
        }

        // File branch — only *.html.
        if (!name.endsWith(QLatin1String(".html"), Qt::CaseInsensitive))
            return false;

        if (!m_phrase.isEmpty() &&
            !name.toLower().contains(m_phrase))
            return false;

        return true;
    }

private:
    QString m_phrase;
};

NoterNotebooksPanel::NoterNotebooksPanel(QWidget *parent) : QFrame(parent) {
    setObjectName("NoterNotebooksPanel");
    setFrameShape(QFrame::NoFrame);
    setFixedWidth(360);
    setStyleSheet(s_panelSurface());
    buildUi();
}

NoterNotebooksPanel::~NoterNotebooksPanel() = default;

void NoterNotebooksPanel::buildUi() {
    QVBoxLayout *outer = new QVBoxLayout(this);
    outer->setContentsMargins(0, 0, 0, 0);
    outer->setSpacing(0);

    QPushButton *closeBtn = nullptr;
    buildHeaderRow(this, outer, tr("Notebooks"),
                   &m_title, &m_count, &closeBtn);
    connect(closeBtn, &QPushButton::clicked,
            this, &NoterNotebooksPanel::closeRequested);

    // Search row.
    QWidget *searchRow = new QWidget(this);
    QHBoxLayout *srl = new QHBoxLayout(searchRow);
    srl->setContentsMargins(12, 0, 12, 8);
    srl->setSpacing(6);
    m_search = new QLineEdit(searchRow);
    m_search->setPlaceholderText(tr("Search notebooks…  Ctrl+,"));
    m_search->setStyleSheet(s_searchInputStyle());
    srl->addWidget(m_search, 1);
    outer->addWidget(searchRow, 0);

    // Tree.
    m_tree = new QTreeView(this);
    m_tree->setHeaderHidden(true);
    m_tree->setRootIsDecorated(true);
    m_tree->setUniformRowHeights(true);
    m_tree->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_tree->setStyleSheet(
        "QTreeView { border: none; background: transparent; }"
        "QTreeView::item { padding: 4px 2px; }"
        "QTreeView::item:selected { background: #EEF2FF; color: #1E1B4B; }"
    );
    outer->addWidget(m_tree, 1);

    m_fs = new QFileSystemModel(this);
    m_fs->setReadOnly(true);
    m_fs->setFilter(QDir::AllDirs | QDir::Files | QDir::NoDotAndDotDot);
    QStringList globs;
    globs << QStringLiteral("*.html");
    m_fs->setNameFilters(globs);
    m_fs->setNameFilterDisables(false);  // hide non-html files outright

    m_proxy = new NotebooksFilterProxy(this);
    m_proxy->setSourceModel(m_fs);
    m_tree->setModel(m_proxy);

    // Hide all columns except the name column.
    for (int c = 1; c < m_proxy->columnCount(); ++c)
        m_tree->hideColumn(c);

    connect(m_search, &QLineEdit::textChanged,
            this, &NoterNotebooksPanel::onSearchTextChanged);
    connect(m_tree, &QTreeView::activated,
            this, &NoterNotebooksPanel::onActivated);
    connect(m_tree, &QTreeView::doubleClicked,
            this, &NoterNotebooksPanel::onActivated);
}

void NoterNotebooksPanel::setRoot(const QString &absoluteRootDir) {
    m_root = absoluteRootDir;
    rebuildModel();
}

QString NoterNotebooksPanel::root() const { return m_root; }

void NoterNotebooksPanel::rebuildModel() {
    if (m_root.isEmpty()) return;
    QDir().mkpath(m_root);
    const QModelIndex srcRoot = m_fs->setRootPath(m_root);
    const QModelIndex proxyRoot = m_proxy->mapFromSource(srcRoot);
    m_tree->setRootIndex(proxyRoot);

    // Update count chip — number of .html files in the root (recursive
    // counting is too slow on a huge tree; just show the top-level).
    QDir d(m_root);
    const QStringList topNotes = d.entryList(QStringList() << "*.html",
                                             QDir::Files);
    if (m_count)
        m_count->setText(QString::number(topNotes.size()));
}

void NoterNotebooksPanel::focusSearch() {
    if (m_search) {
        m_search->setFocus();
        m_search->selectAll();
    }
}

void NoterNotebooksPanel::onSearchTextChanged(const QString &text) {
    // Safe static_cast — we own the proxy and we know the concrete type.
    // qobject_cast would require Q_OBJECT on NotebooksFilterProxy which
    // we deliberately skip (private class, no signals).
    NotebooksFilterProxy *p = static_cast<NotebooksFilterProxy *>(m_proxy);
    if (p) p->setSearchPhrase(text);
}

void NoterNotebooksPanel::onActivated(const QModelIndex &index) {
    if (!index.isValid()) return;
    QModelIndex src = m_proxy->mapToSource(index);
    if (!src.isValid()) return;
    if (m_fs->isDir(src)) return;
    const QString abs = m_fs->filePath(src);
    if (abs.endsWith(QLatin1String(".html"), Qt::CaseInsensitive))
        emit noteOpenRequested(abs);
}

// ═══════════════════════════════════════════════════════════════════════
// NoterTodosPanel
//
// 4 active groups + 1 collapsed Done. Each row is its own QWidget with
// a checkbox + the title + a meta line. Row click → todoActivated;
// checkbox tick → todoMarkDone.
// ═══════════════════════════════════════════════════════════════════════

// Single row widget — exposed via internal namespace for unit testing
// (we use findChildren to count by objectName rather than reach in).
namespace {

class TodoRowWidget : public QFrame {
public:
    TodoRowWidget(const NoterTodoRow &row,
                  const QString &accentHex,
                  QWidget *parent = nullptr)
        : QFrame(parent), m_row(row) {
        setObjectName(QStringLiteral("todoRow"));
        setFrameShape(QFrame::NoFrame);
        setCursor(Qt::PointingHandCursor);
        setStyleSheet(
            "QFrame#todoRow { background: #FFFFFF; border: 1px solid #E5E7EB; "
            "border-radius: 6px; }"
            "QFrame#todoRow:hover { background: #F9FAFB; border-color: #C7D2FE; }"
        );

        QHBoxLayout *h = new QHBoxLayout(this);
        h->setContentsMargins(8, 6, 8, 6);
        h->setSpacing(8);

        m_check = new QCheckBox(this);
        m_check->setChecked(row.done);
        m_check->setObjectName("todoCheck");
        h->addWidget(m_check, 0, Qt::AlignTop);

        QWidget *col = new QWidget(this);
        QVBoxLayout *v = new QVBoxLayout(col);
        v->setContentsMargins(0, 0, 0, 0);
        v->setSpacing(2);

        // v0.1.94 — editable title. Default mode = QLabel (page 0). Double-
        // click on this row swaps to QLineEdit (page 1) with text pre-
        // selected. Enter or focus-out commits via emitTextEditedThroughPanel
        // and swaps back; Esc cancels.
        m_titleStack = new QStackedWidget(col);
        m_titleStack->setObjectName(QStringLiteral("todoTitleStack"));

        m_title = new QLabel(row.text, m_titleStack);
        m_title->setWordWrap(true);
        m_title->setObjectName(QStringLiteral("todoTitle"));
        m_title->setToolTip(tr("Double-click to edit"));
        m_title->setStyleSheet(
            QStringLiteral("QLabel { color: #111827; font-size: 13px; "
                           "border-left: 2px solid %1; padding-left: 6px; }")
                .arg(accentHex));

        m_titleEdit = new QLineEdit(m_titleStack);
        m_titleEdit->setObjectName(QStringLiteral("todoTitleEdit"));
        m_titleEdit->setStyleSheet(
            QStringLiteral("QLineEdit { color: #111827; font-size: 13px; "
                           "border-left: 2px solid %1; padding: 2px 6px; "
                           "background: #FFFBEB; border-radius: 2px; }")
                .arg(accentHex));

        m_titleStack->addWidget(m_title);
        m_titleStack->addWidget(m_titleEdit);
        m_titleStack->setCurrentIndex(0);

        QObject::connect(m_titleEdit, &QLineEdit::editingFinished, this,
                         [this]() { commitTitleEdit(/*cancelled=*/false); });
        m_titleEdit->installEventFilter(this);  // catch Esc

        v->addWidget(m_titleStack);

        // Meta line: owner · meeting · due
        QStringList parts;
        if (!row.owner.isEmpty())   parts << row.owner;
        if (!row.meeting.isEmpty()) parts << row.meeting;
        if (row.due.isValid())
            parts << row.due.toString(QStringLiteral("MMM d HH:mm"));
        if (!parts.isEmpty()) {
            QLabel *meta = new QLabel(parts.join(QStringLiteral(" · ")), col);
            meta->setObjectName("todoMeta");
            meta->setStyleSheet(
                "QLabel { color: #6B7280; font-size: 11px; padding-left: 6px; }");
            v->addWidget(meta);
        }
        h->addWidget(col, 1);

        // v0.1.95+ — inline ✕ delete button on each todo row. User explicitly
        // asked for "X on each title" rather than right-click-only. For live
        // rows this trashes (soft delete); for trashed rows it restores.
        auto *xBtn = new QPushButton(this);
        xBtn->setObjectName(QStringLiteral("todoRowX"));
        xBtn->setCursor(Qt::PointingHandCursor);
        xBtn->setFlat(true);
        xBtn->setFixedSize(22, 22);
        xBtn->setText(row.trashed ? QStringLiteral("↺") : QStringLiteral("✕"));
        xBtn->setToolTip(row.trashed ? tr("Restore")
                                     : tr("Move to Trash"));
        xBtn->setStyleSheet(QStringLiteral(
            "QPushButton#todoRowX { color: #94a3b8; background: transparent;"
            "  border: none; font-size: 13px; padding: 0; }"
            "QPushButton#todoRowX:hover { color: %1; background: #fee2e2;"
            "  border-radius: 3px; }"
        ).arg(row.trashed ? QStringLiteral("#16a34a")
                          : QStringLiteral("#DC2626")));
        h->addWidget(xBtn, 0, Qt::AlignTop);
        QObject::connect(xBtn, &QPushButton::clicked, this, [this]() {
            QWidget *p = parentWidget();
            while (p) {
                if (auto *panel = qobject_cast<NoterTodosPanel *>(p)) {
                    if (m_row.trashed)
                        emit panel->restoreRequested(m_row.todoId);
                    else
                        emit panel->trashRequested(m_row.todoId);
                    break;
                }
                p = p->parentWidget();
            }
        });
    }

    const NoterTodoRow &row() const { return m_row; }
    QCheckBox *check() const { return m_check; }

    // Test hooks for the editable-title path.
    QLabel *titleLabel() const { return m_title; }
    QLineEdit *titleEdit() const { return m_titleEdit; }
    QStackedWidget *titleStack() const { return m_titleStack; }

protected:
    void mousePressEvent(QMouseEvent *ev) override;
    void mouseDoubleClickEvent(QMouseEvent *ev) override;
    void contextMenuEvent(QContextMenuEvent *ev) override;
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    void beginTitleEdit();
    void commitTitleEdit(bool cancelled);

    NoterTodoRow m_row;
    QCheckBox *m_check = nullptr;
    QStackedWidget *m_titleStack = nullptr;
    QLabel *m_title = nullptr;
    QLineEdit *m_titleEdit = nullptr;
    bool m_editingActive = false;
};

} // anon namespace

// Forward — defined out-of-line because it emits a panel signal and we
// need the panel symbol visible. We resolve at runtime by walking up
// the parent chain to find the owning NoterTodosPanel.
void TodoRowWidget::mousePressEvent(QMouseEvent *ev) {
    // Suppress the activate-signal while we're in title-edit mode —
    // otherwise clicking inside the QLineEdit to position the caret
    // would also open the source file and tear down the editor.
    if (m_editingActive) {
        QFrame::mousePressEvent(ev);
        return;
    }
    // Find the enclosing panel and emit its activation signal.
    QWidget *p = parentWidget();
    while (p) {
        NoterTodosPanel *panel = qobject_cast<NoterTodosPanel *>(p);
        if (panel) {
            emit panel->todoActivated(m_row.sourceFile, m_row.blockId);
            break;
        }
        p = p->parentWidget();
    }
    QFrame::mousePressEvent(ev);
}

void TodoRowWidget::mouseDoubleClickEvent(QMouseEvent *ev) {
    // v0.1.94 — inline-edit the title text. Double-click anywhere on the
    // row surfaces the QLineEdit. Existing single-click "activate" still
    // fires on each press, but the editor is now overlayed and grabs
    // focus, so the source-file opens behind the edit field — which is
    // OK because committing the edit + the user returning to the panel
    // lands them on the same row.
    beginTitleEdit();
    QFrame::mouseDoubleClickEvent(ev);
}

// v0.1.95+ — light QMenu style applied directly to each context menu
// instance. The memory rule `feedback_qmenu_cascade_through_widget_qss`
// says we have to do this per-instance because the parent panel's QSS
// rule for QMenu doesn't actually cascade when intermediate widgets set
// their own stylesheets — TodoRowWidget has its own QFrame#todoRow style
// which breaks the chain. Direct setStyleSheet on the menu itself wins.
static void applyLightMenuStyle(QMenu &menu) {
    menu.setStyleSheet(QStringLiteral(
        "QMenu { background: #FFFFFF; color: #111827;"
        "  border: 1px solid #d5d0c0; padding: 4px 0; }"
        "QMenu::item { padding: 7px 24px 7px 22px; font-size: 13px; }"
        "QMenu::item:selected { background: #FEF3C7; color: #0a0d12; }"
        "QMenu::separator { height: 1px; background: #e5e1d6;"
        "  margin: 4px 8px; }"
    ));
}

// v0.1.95+ — right-click context menu on a todo row.
// Set due / Set reminder open a popup with QDateTimeEdit + calendar popup.
// Mark done / Delete are one-shot actions confirmed via the row's panel.
static QDateTime promptForDateTime(QWidget *parent, const QString &title,
                                   const QDateTime &initial,
                                   bool *cleared = nullptr) {
    if (cleared) *cleared = false;
    QDialog dlg(parent);
    dlg.setWindowTitle(title);
    auto *outer = new QVBoxLayout(&dlg);
    auto *form = new QFormLayout;
    auto *edit = new QDateTimeEdit(initial.isValid() ? initial
                                                     : QDateTime::currentDateTime().addDays(1),
                                   &dlg);
    edit->setCalendarPopup(true);
    edit->setDisplayFormat(QStringLiteral("yyyy-MM-dd  HH:mm"));
    form->addRow(QStringLiteral("Date / time:"), edit);
    outer->addLayout(form);

    auto *bb = new QDialogButtonBox(&dlg);
    bb->addButton(QDialogButtonBox::Ok);
    bb->addButton(QDialogButtonBox::Cancel);
    auto *clearBtn = bb->addButton(QStringLiteral("Clear"), QDialogButtonBox::ResetRole);
    QObject::connect(bb, &QDialogButtonBox::accepted, &dlg, &QDialog::accept);
    QObject::connect(bb, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);
    bool wasCleared = false;
    QObject::connect(clearBtn, &QPushButton::clicked, [&]() {
        wasCleared = true;
        dlg.accept();
    });
    outer->addWidget(bb);

    if (dlg.exec() != QDialog::Accepted) return QDateTime();
    if (wasCleared) {
        if (cleared) *cleared = true;
        return QDateTime();
    }
    return edit->dateTime();
}

void TodoRowWidget::contextMenuEvent(QContextMenuEvent *ev) {
    QWidget *p = parentWidget();
    NoterTodosPanel *panel = nullptr;
    while (p) {
        if (auto *pp = qobject_cast<NoterTodosPanel *>(p)) { panel = pp; break; }
        p = p->parentWidget();
    }
    if (!panel) return;

    QMenu menu(this);
    applyLightMenuStyle(menu);
    if (m_row.trashed) {
        // Trashed-row menu: Restore or Delete permanently.
        QAction *aRestore = menu.addAction(tr("↺ Restore"));
        menu.addSeparator();
        QAction *aDelete  = menu.addAction(tr("⚠ Delete permanently"));
        QAction *picked = menu.exec(ev->globalPos());
        if (!picked) return;
        if (picked == aRestore) {
            emit panel->restoreRequested(m_row.todoId);
        } else if (picked == aDelete) {
            // Confirm — this one is irreversible.
            // We use a plain QMenu without confirmation for the trash-soft
            // delete, but permanent delete deserves a yes/no.
            if (QMessageBox::warning(panel, tr("Delete permanently?"),
                    tr("This todo will be permanently removed and cannot be recovered.\n\n%1")
                        .arg(m_row.text),
                    QMessageBox::Yes | QMessageBox::No, QMessageBox::No)
                == QMessageBox::Yes) {
                emit panel->deleteRequested(m_row.todoId);
            }
        }
        return;
    }

    // Live-row menu: due / reminder / done / move-to-trash.
    QAction *aDue    = menu.addAction(tr("📅 Set due date…"));
    QAction *aRemind = menu.addAction(tr("⏰ Set reminder…"));
    menu.addSeparator();
    QAction *aDone   = menu.addAction(m_row.done ? tr("Mark open")
                                                 : tr("Mark done"));
    menu.addSeparator();
    QAction *aTrash  = menu.addAction(tr("🗑  Delete"));

    QAction *picked = menu.exec(ev->globalPos());
    if (!picked) return;

    if (picked == aDue) {
        bool cleared = false;
        QDateTime dt = promptForDateTime(panel, tr("Set due date"),
                                         m_row.due, &cleared);
        if (cleared) emit panel->setDueRequested(m_row.todoId, QDateTime());
        else if (dt.isValid()) emit panel->setDueRequested(m_row.todoId, dt);
    } else if (picked == aRemind) {
        bool cleared = false;
        QDateTime dt = promptForDateTime(
            panel, tr("Set reminder"),
            m_row.due.isValid() ? m_row.due.addSecs(-3600)
                                : QDateTime::currentDateTime().addDays(1),
            &cleared);
        if (cleared) emit panel->setReminderRequested(m_row.todoId, QDateTime());
        else if (dt.isValid()) emit panel->setReminderRequested(m_row.todoId, dt);
    } else if (picked == aDone) {
        emit panel->markDoneRequested(m_row.todoId);
    } else if (picked == aTrash) {
        emit panel->trashRequested(m_row.todoId);
    }
}

bool TodoRowWidget::eventFilter(QObject *watched, QEvent *event) {
    if (watched == m_titleEdit && event->type() == QEvent::KeyPress) {
        auto *ke = static_cast<QKeyEvent *>(event);
        if (ke->key() == Qt::Key_Escape) {
            commitTitleEdit(/*cancelled=*/true);
            return true;
        }
    }
    return QFrame::eventFilter(watched, event);
}

void TodoRowWidget::beginTitleEdit() {
    if (!m_titleStack || !m_titleEdit) return;
    if (m_editingActive) return;
    m_editingActive = true;
    m_titleEdit->setText(m_title->text());
    m_titleStack->setCurrentIndex(1);
    m_titleEdit->setFocus(Qt::MouseFocusReason);
    m_titleEdit->selectAll();
}

void TodoRowWidget::commitTitleEdit(bool cancelled) {
    if (!m_titleStack || !m_titleEdit) return;
    if (!m_editingActive) return;
    m_editingActive = false;
    const QString newText = m_titleEdit->text().trimmed();
    m_titleStack->setCurrentIndex(0);  // swap back regardless

    if (cancelled) return;
    if (newText.isEmpty()) return;                 // empty = no-op (no delete)
    if (newText == m_title->text()) return;        // unchanged

    // Commit by emitting through the owning panel. The integrator pulls
    // the todoId off our cached row and persists to NotesTodos.
    m_title->setText(newText);
    m_row.text = newText;
    QWidget *p = parentWidget();
    while (p) {
        if (auto *panel = qobject_cast<NoterTodosPanel *>(p)) {
            emit panel->todoTextEdited(m_row.todoId, newText);
            break;
        }
        p = p->parentWidget();
    }
}

NoterTodosPanel::NoterTodosPanel(QWidget *parent) : QFrame(parent) {
    setObjectName("NoterTodosPanel");
    setFrameShape(QFrame::NoFrame);
    setFixedWidth(360);
    setStyleSheet(s_panelSurface());
    buildUi();
}

NoterTodosPanel::~NoterTodosPanel() = default;

void NoterTodosPanel::buildUi() {
    QVBoxLayout *outer = new QVBoxLayout(this);
    outer->setContentsMargins(0, 0, 0, 0);
    outer->setSpacing(0);

    QPushButton *closeBtn = nullptr;
    buildHeaderRow(this, outer, tr("Todos"),
                   &m_title, &m_count, &closeBtn);
    connect(closeBtn, &QPushButton::clicked,
            this, &NoterTodosPanel::closeRequested);

    // Search + Add row.
    QWidget *searchRow = new QWidget(this);
    QHBoxLayout *srl = new QHBoxLayout(searchRow);
    srl->setContentsMargins(12, 0, 12, 8);
    srl->setSpacing(6);
    m_search = new QLineEdit(searchRow);
    m_search->setPlaceholderText(tr("Filter todos…  Ctrl+Alt+T"));
    m_search->setStyleSheet(s_searchInputStyle());
    srl->addWidget(m_search, 1);

    // v0.1.95 — quick-add button. User reported: "todo doesn't have a
    // way to add" — fixed via QInputDialog popping on click. Persists
    // through NotesTodos::addQuickTodo (Inbox/quick-todos.html).
    auto *addBtn = new QPushButton(tr("+ Add"), searchRow);
    addBtn->setObjectName(QStringLiteral("noterAddTodoBtn"));
    addBtn->setCursor(Qt::PointingHandCursor);
    addBtn->setStyleSheet(QStringLiteral(
        "QPushButton#noterAddTodoBtn { background: #DC2626; color: white;"
        "  border: none; border-radius: 4px; padding: 4px 12px; font-size: 12px;"
        "  font-weight: 500; }"
        "QPushButton#noterAddTodoBtn:hover { background: #b91c1c; }"
    ));
    addBtn->setToolTip(tr("Add a quick todo (not tied to any meeting)"));
    srl->addWidget(addBtn, 0);
    connect(addBtn, &QPushButton::clicked, this, [this]() {
        // v0.1.95+ — richer add dialog: text + optional due + optional
        // reminder, each with calendar popup. "Clear" on a date field
        // sets the QDateTimeEdit's special "no date" value so we know
        // to skip the corresponding NotesTodos::setDue / setReminder.
        QDialog dlg(this);
        dlg.setWindowTitle(tr("Add todo"));
        dlg.setMinimumWidth(360);
        auto *outer = new QVBoxLayout(&dlg);
        auto *form = new QFormLayout;

        auto *textEdit = new QLineEdit(&dlg);
        textEdit->setPlaceholderText(tr("What needs doing?"));
        form->addRow(tr("Text:"), textEdit);

        const QDateTime placeholder(QDate(1900, 1, 1), QTime(0, 0));
        auto *dueEdit = new QDateTimeEdit(placeholder, &dlg);
        dueEdit->setCalendarPopup(true);
        dueEdit->setDisplayFormat(QStringLiteral("yyyy-MM-dd  HH:mm"));
        dueEdit->setMinimumDateTime(placeholder);
        dueEdit->setSpecialValueText(tr("(no due date)"));
        auto *dueRow = new QWidget;
        auto *dueRowL = new QHBoxLayout(dueRow);
        dueRowL->setContentsMargins(0, 0, 0, 0);
        dueRowL->addWidget(dueEdit, 1);
        auto *dueSetToday = new QPushButton(tr("today"), dueRow);
        auto *dueSetTomorrow = new QPushButton(tr("tomorrow"), dueRow);
        auto *dueClear = new QPushButton(tr("clear"), dueRow);
        for (auto *b : {dueSetToday, dueSetTomorrow, dueClear})
            b->setStyleSheet(QStringLiteral("QPushButton { padding: 2px 8px; font-size: 11px; }"));
        dueRowL->addWidget(dueSetToday);
        dueRowL->addWidget(dueSetTomorrow);
        dueRowL->addWidget(dueClear);
        QObject::connect(dueSetToday, &QPushButton::clicked, [dueEdit]() {
            dueEdit->setDateTime(QDateTime(QDate::currentDate(), QTime(17, 0)));
        });
        QObject::connect(dueSetTomorrow, &QPushButton::clicked, [dueEdit]() {
            dueEdit->setDateTime(QDateTime(QDate::currentDate().addDays(1), QTime(9, 0)));
        });
        QObject::connect(dueClear, &QPushButton::clicked, [dueEdit, placeholder]() {
            dueEdit->setDateTime(placeholder);
        });
        form->addRow(tr("Due:"), dueRow);

        auto *remindEdit = new QDateTimeEdit(placeholder, &dlg);
        remindEdit->setCalendarPopup(true);
        remindEdit->setDisplayFormat(QStringLiteral("yyyy-MM-dd  HH:mm"));
        remindEdit->setMinimumDateTime(placeholder);
        remindEdit->setSpecialValueText(tr("(no reminder)"));
        auto *remRow = new QWidget;
        auto *remRowL = new QHBoxLayout(remRow);
        remRowL->setContentsMargins(0, 0, 0, 0);
        remRowL->addWidget(remindEdit, 1);
        // Match the Due row's quick-pick affordances.
        auto *remIn1h    = new QPushButton(tr("in 1h"),   remRow);
        auto *remTomMorn = new QPushButton(tr("tomorrow"), remRow);
        auto *remBefore  = new QPushButton(tr("1h before due"), remRow);
        auto *remClear   = new QPushButton(tr("clear"),    remRow);
        for (auto *b : {remIn1h, remTomMorn, remBefore, remClear})
            b->setStyleSheet(QStringLiteral("QPushButton { padding: 2px 8px; font-size: 11px; }"));
        remRowL->addWidget(remIn1h);
        remRowL->addWidget(remTomMorn);
        remRowL->addWidget(remBefore);
        remRowL->addWidget(remClear);
        QObject::connect(remIn1h, &QPushButton::clicked, [remindEdit]() {
            remindEdit->setDateTime(QDateTime::currentDateTime().addSecs(3600));
        });
        QObject::connect(remTomMorn, &QPushButton::clicked, [remindEdit]() {
            remindEdit->setDateTime(QDateTime(QDate::currentDate().addDays(1),
                                              QTime(9, 0)));
        });
        QObject::connect(remBefore, &QPushButton::clicked, [remindEdit, dueEdit, placeholder]() {
            if (dueEdit->dateTime() > placeholder)
                remindEdit->setDateTime(dueEdit->dateTime().addSecs(-3600));
            else
                remindEdit->setDateTime(QDateTime::currentDateTime().addSecs(3600));
        });
        QObject::connect(remClear, &QPushButton::clicked, [remindEdit, placeholder]() {
            remindEdit->setDateTime(placeholder);
        });
        form->addRow(tr("Remind:"), remRow);
        outer->addLayout(form);

        auto *bb = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dlg);
        QObject::connect(bb, &QDialogButtonBox::accepted, &dlg, &QDialog::accept);
        QObject::connect(bb, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);
        outer->addWidget(bb);

        textEdit->setFocus();

        if (dlg.exec() != QDialog::Accepted) return;
        const QString text = textEdit->text().trimmed();
        if (text.isEmpty()) return;
        QDateTime due, remind;
        if (dueEdit->dateTime() > placeholder)  due  = dueEdit->dateTime();
        if (remindEdit->dateTime() > placeholder) remind = remindEdit->dateTime();
        emit addTodoWithDateRequested(text, due, remind);
    });

    outer->addWidget(searchRow, 0);

    // Body — vertical stack inside a scroll area.
    m_scroll = new QScrollArea(this);
    m_scroll->setWidgetResizable(true);
    m_scroll->setFrameShape(QFrame::NoFrame);
    m_scroll->setStyleSheet("QScrollArea { background: transparent; }");

    m_body = new QWidget(m_scroll);
    m_body->setStyleSheet("background: transparent;");
    m_bodyLayout = new QVBoxLayout(m_body);
    m_bodyLayout->setContentsMargins(12, 4, 12, 12);
    m_bodyLayout->setSpacing(10);
    m_bodyLayout->addStretch(1);

    m_scroll->setWidget(m_body);
    outer->addWidget(m_scroll, 1);

    connect(m_search, &QLineEdit::textChanged,
            this, &NoterTodosPanel::onSearchTextChanged);
}

void NoterTodosPanel::clearGroups() {
    // Remove every child of m_body except the trailing stretch.
    QLayoutItem *child = nullptr;
    while ((child = m_bodyLayout->takeAt(0)) != nullptr) {
        if (child->widget()) child->widget()->deleteLater();
        delete child;
    }
    m_visibleRows.clear();
    m_bodyLayout->addStretch(1);
}

void NoterTodosPanel::appendGroup(const QString &title,
                                  const QString &accentHex,
                                  const QList<NoterTodoRow> &rows,
                                  bool collapsedDefault) {
    Q_UNUSED(collapsedDefault);  // v1: no collapse toggle yet
    // Header strip with count.
    QWidget *hdr = new QWidget(m_body);
    QHBoxLayout *hl = new QHBoxLayout(hdr);
    hl->setContentsMargins(0, 4, 0, 2);
    hl->setSpacing(8);

    QLabel *t = new QLabel(title, hdr);
    t->setStyleSheet(s_groupHeaderStyle(accentHex));
    hl->addWidget(t, 0);

    QLabel *c = new QLabel(QString::number(rows.size()), hdr);
    c->setStyleSheet(s_countChipStyle());
    c->setAlignment(Qt::AlignCenter);
    hl->addWidget(c, 0);
    hl->addStretch(1);

    // Insert above the trailing stretch.
    m_bodyLayout->insertWidget(m_bodyLayout->count() - 1, hdr);

    for (const NoterTodoRow &r : rows) {
        TodoRowWidget *row = new TodoRowWidget(r, accentHex, m_body);
        // Wire the checkbox to the mark-done signal.
        const QString rid = r.todoId;
        connect(row->check(), &QCheckBox::toggled, this,
                [this, rid](bool on) {
                    if (on) emit todoMarkDone(rid);
                });
        m_bodyLayout->insertWidget(m_bodyLayout->count() - 1, row);
        m_visibleRows.append(qMakePair(static_cast<QFrame *>(row), r));
    }
}

void NoterTodosPanel::setTodos(const QList<NoterTodoRow> &overdue,
                               const QList<NoterTodoRow> &today,
                               const QList<NoterTodoRow> &week,
                               const QList<NoterTodoRow> &someday,
                               const QList<NoterTodoRow> &done,
                               const QList<NoterTodoRow> &trashed) {
    clearGroups();
    m_overdueCount = overdue.size();
    m_todayCount   = today.size();
    m_weekCount    = week.size();
    m_somedayCount = someday.size();
    m_doneCount    = done.size();

    // Mark trashed rows so the row widget's context menu can flip to
    // Restore / Delete permanently. Done rows show a plain check; trash
    // rows show muted-strikethrough.
    QList<NoterTodoRow> trashedWithFlag;
    trashedWithFlag.reserve(trashed.size());
    for (NoterTodoRow r : trashed) { r.trashed = true; trashedWithFlag << r; }

    // Design SURFACE 04 colors: red / orange / yellow / grey / muted-green / trash-slate.
    appendGroup(tr("Overdue"),      QStringLiteral("#DC2626"), overdue,         false);
    appendGroup(tr("Today"),        QStringLiteral("#F97316"), today,           false);
    appendGroup(tr("This week"),    QStringLiteral("#EAB308"), week,            false);
    appendGroup(tr("Someday"),      QStringLiteral("#6B7280"), someday,         false);
    if (!done.isEmpty())
        appendGroup(tr("Done"),     QStringLiteral("#16A34A"), done,            true);
    if (!trashedWithFlag.isEmpty())
        appendGroup(tr("Trash"),    QStringLiteral("#9CA3AF"), trashedWithFlag, true);

    const int total = m_overdueCount + m_todayCount + m_weekCount +
                      m_somedayCount;
    if (m_count) m_count->setText(QString::number(total));
}

void NoterTodosPanel::focusSearch() {
    if (m_search) {
        m_search->setFocus();
        m_search->selectAll();
    }
}

void NoterTodosPanel::onSearchTextChanged(const QString &text) {
    const QString needle = text.trimmed().toLower();
    // Hide rows whose title/owner/meeting don't contain the phrase.
    // We walk the parallel data list (m_visibleRows) instead of
    // findChildren<TodoRowWidget*> — the row widget lives in an anon
    // namespace + has no Q_OBJECT, so findChildren can't safely cast.
    for (auto &pair : m_visibleRows) {
        if (!pair.first) continue;
        if (needle.isEmpty()) {
            pair.first->setVisible(true);
            continue;
        }
        const NoterTodoRow &d = pair.second;
        const bool hit =
            d.text.toLower().contains(needle) ||
            d.owner.toLower().contains(needle) ||
            d.meeting.toLower().contains(needle);
        pair.first->setVisible(hit);
    }
}

// ═══════════════════════════════════════════════════════════════════════
// NoterRemindersPanel
//
// History log bucketed into Today / Yesterday / Earlier this week /
// Older. Each row has a small colored dot at left + text + relative
// time at right.
// ═══════════════════════════════════════════════════════════════════════

namespace {

static QString stateColor(const QString &state) {
    if (state == QLatin1String("fired"))    return QStringLiteral("#0D9488"); // teal
    if (state == QLatin1String("snoozed"))  return QStringLiteral("#D97706"); // amber
    if (state == QLatin1String("pending"))  return QStringLiteral("#4F46E5"); // indigo
    if (state == QLatin1String("missed"))   return QStringLiteral("#E11D48"); // rose
    return QStringLiteral("#9CA3AF"); // dismissed / unknown
}

class ReminderRowWidget : public QFrame {
public:
    ReminderRowWidget(const NoterReminderRow &row, QWidget *parent = nullptr)
        : QFrame(parent), m_row(row) {
        setObjectName(QStringLiteral("reminderRow"));
        setFrameShape(QFrame::NoFrame);
        setCursor(Qt::PointingHandCursor);
        setStyleSheet(
            "QFrame#reminderRow { background: #FFFFFF; border: 1px solid #E5E7EB; "
            "border-radius: 6px; }"
            "QFrame#reminderRow:hover { background: #F9FAFB; border-color: #C7D2FE; }"
        );

        QHBoxLayout *h = new QHBoxLayout(this);
        h->setContentsMargins(10, 6, 10, 6);
        h->setSpacing(8);

        // Colored dot — small 10x10 round QLabel.
        QLabel *dot = new QLabel(this);
        dot->setFixedSize(10, 10);
        dot->setStyleSheet(QStringLiteral(
            "QLabel { background: %1; border-radius: 5px; }"
        ).arg(stateColor(row.state)));
        h->addWidget(dot, 0, Qt::AlignVCenter);

        QLabel *text = new QLabel(row.text, this);
        text->setStyleSheet("QLabel { color: #111827; font-size: 13px; }");
        text->setWordWrap(true);
        h->addWidget(text, 1);

        if (row.when.isValid()) {
            QLabel *when = new QLabel(
                row.when.toString(QStringLiteral("MMM d HH:mm")), this);
            when->setStyleSheet(
                "QLabel { color: #6B7280; font-size: 11px; }");
            h->addWidget(when, 0);
        }
    }

    const NoterReminderRow &row() const { return m_row; }

protected:
    void mousePressEvent(QMouseEvent *ev) override;

private:
    NoterReminderRow m_row;
};

} // anon namespace

void ReminderRowWidget::mousePressEvent(QMouseEvent *ev) {
    QWidget *p = parentWidget();
    while (p) {
        NoterRemindersPanel *panel = qobject_cast<NoterRemindersPanel *>(p);
        if (panel) {
            emit panel->reminderActivated(m_row.sourceFile, m_row.blockId);
            break;
        }
        p = p->parentWidget();
    }
    QFrame::mousePressEvent(ev);
}

NoterRemindersPanel::NoterRemindersPanel(QWidget *parent) : QFrame(parent) {
    setObjectName("NoterRemindersPanel");
    setFrameShape(QFrame::NoFrame);
    setFixedWidth(360);
    setStyleSheet(s_panelSurface());
    buildUi();
}

NoterRemindersPanel::~NoterRemindersPanel() = default;

void NoterRemindersPanel::buildUi() {
    QVBoxLayout *outer = new QVBoxLayout(this);
    outer->setContentsMargins(0, 0, 0, 0);
    outer->setSpacing(0);

    QPushButton *closeBtn = nullptr;
    buildHeaderRow(this, outer, tr("Reminders"),
                   &m_title, &m_count, &closeBtn);
    connect(closeBtn, &QPushButton::clicked,
            this, &NoterRemindersPanel::closeRequested);

    QWidget *searchRow = new QWidget(this);
    QHBoxLayout *srl = new QHBoxLayout(searchRow);
    srl->setContentsMargins(12, 0, 12, 8);
    srl->setSpacing(6);
    m_search = new QLineEdit(searchRow);
    m_search->setPlaceholderText(tr("Filter reminders…  Ctrl+Alt+R"));
    m_search->setStyleSheet(s_searchInputStyle());
    srl->addWidget(m_search, 1);
    outer->addWidget(searchRow, 0);

    m_scroll = new QScrollArea(this);
    m_scroll->setWidgetResizable(true);
    m_scroll->setFrameShape(QFrame::NoFrame);
    m_scroll->setStyleSheet("QScrollArea { background: transparent; }");

    m_body = new QWidget(m_scroll);
    m_body->setStyleSheet("background: transparent;");
    m_bodyLayout = new QVBoxLayout(m_body);
    m_bodyLayout->setContentsMargins(12, 4, 12, 12);
    m_bodyLayout->setSpacing(10);
    m_bodyLayout->addStretch(1);

    m_scroll->setWidget(m_body);
    outer->addWidget(m_scroll, 1);

    connect(m_search, &QLineEdit::textChanged,
            this, &NoterRemindersPanel::onSearchTextChanged);
}

void NoterRemindersPanel::clearGroups() {
    QLayoutItem *child = nullptr;
    while ((child = m_bodyLayout->takeAt(0)) != nullptr) {
        if (child->widget()) child->widget()->deleteLater();
        delete child;
    }
    m_visibleRows.clear();
    m_bodyLayout->addStretch(1);
}

void NoterRemindersPanel::appendGroup(const QString &title,
                                      const QList<NoterReminderRow> &rows) {
    if (rows.isEmpty()) return;

    QWidget *hdr = new QWidget(m_body);
    QHBoxLayout *hl = new QHBoxLayout(hdr);
    hl->setContentsMargins(0, 4, 0, 2);
    hl->setSpacing(8);

    QLabel *t = new QLabel(title, hdr);
    t->setStyleSheet(s_groupHeaderStyle(QStringLiteral("#6366F1")));
    hl->addWidget(t, 0);

    QLabel *c = new QLabel(QString::number(rows.size()), hdr);
    c->setStyleSheet(s_countChipStyle());
    c->setAlignment(Qt::AlignCenter);
    hl->addWidget(c, 0);
    hl->addStretch(1);

    m_bodyLayout->insertWidget(m_bodyLayout->count() - 1, hdr);

    for (const NoterReminderRow &r : rows) {
        ReminderRowWidget *row = new ReminderRowWidget(r, m_body);
        m_bodyLayout->insertWidget(m_bodyLayout->count() - 1, row);
        m_visibleRows.append(qMakePair(static_cast<QFrame *>(row), r));
    }
}

void NoterRemindersPanel::setReminders(const QList<NoterReminderRow> &rows) {
    clearGroups();
    m_todayCount = m_yesterdayCount = m_weekCount = m_olderCount = 0;

    const QDate today = QDate::currentDate();
    const QDate yesterday = today.addDays(-1);

    QList<NoterReminderRow> tToday, tYesterday, tWeek, tOlder;
    for (const NoterReminderRow &r : rows) {
        const QDate d = r.when.date();
        if (d == today)              tToday.append(r);
        else if (d == yesterday)     tYesterday.append(r);
        else if (d >= today.addDays(-6) && d < yesterday)
                                     tWeek.append(r);
        else                         tOlder.append(r);
    }
    m_todayCount     = tToday.size();
    m_yesterdayCount = tYesterday.size();
    m_weekCount      = tWeek.size();
    m_olderCount     = tOlder.size();

    appendGroup(tr("Today"),              tToday);
    appendGroup(tr("Yesterday"),          tYesterday);
    appendGroup(tr("Earlier this week"),  tWeek);
    appendGroup(tr("Older"),              tOlder);

    if (m_count)
        m_count->setText(QString::number(rows.size()));
}

void NoterRemindersPanel::focusSearch() {
    if (m_search) {
        m_search->setFocus();
        m_search->selectAll();
    }
}

void NoterRemindersPanel::onSearchTextChanged(const QString &text) {
    const QString needle = text.trimmed().toLower();
    for (auto &pair : m_visibleRows) {
        if (!pair.first) continue;
        if (needle.isEmpty()) {
            pair.first->setVisible(true);
            continue;
        }
        const bool hit = pair.second.text.toLower().contains(needle);
        pair.first->setVisible(hit);
    }
}
