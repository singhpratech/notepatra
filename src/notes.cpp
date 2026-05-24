// SPDX-License-Identifier: GPL-3.0-or-later
//
// notes.cpp — Noter, v0.1.95 redesign.
//
// Two-pane (sidebar + editor) layout inspired by Apple Notes / Bear /
// Granola. Sidebar lists past meetings grouped by recency, a single
// "+ New meeting" button, and a bottom "All Todos" toggle. Editor is a
// vanilla QTextEdit with ☐ checkboxes that toggle on click. One ✨
// Extract button bottom-right runs the AI sweep over the body. No
// slash menu, no insert bar, no header button row.
//
// Storage: HTML files in <Documents>/Notepatra/Noter/Inbox/, with a
// SQLite todos cache rebuilt from the .html on every save.

#include "notes.h"
#include "notes_storage.h"
#include "notes_template.h"
#include "notes_todos.h"
#include "notes_reminder.h"
#include "notes_panels.h"
#include "notes_popout.h"
#include "notes_context_menus.h"
#include "notes_sweep_dialog.h"
#include "notes_sweep_prompt.h"
#include "notes_export.h"
#include "ollama.h"
#include "config.h"

#include <QAction>
#include <QApplication>
#include <QComboBox>
#include <QDateTime>
#include <QDir>
#include <QFileInfo>
#include <QFrame>
#include <QHBoxLayout>
#include <QInputDialog>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QListWidgetItem>
#include <QMenu>
#include <QMessageBox>
#include <QMouseEvent>
#include <QRegExp>
#include <QPushButton>
#include <QShortcut>
#include <QToolButton>
#include <QSplitter>
#include <QStackedWidget>
#include <QStandardPaths>
#include <QStyle>
#include <QTextBlock>
#include <QTextCharFormat>
#include <QTextCursor>
#include <QTextEdit>
#include <QTimer>
#include <QVBoxLayout>

namespace {

// ─── styling helpers ────────────────────────────────────────────────
static const QString kNoterRed = QStringLiteral("#DC2626");
static const QString kSidebarBg = QStringLiteral("#f3f1ea");
static const QString kSidebarBorder = QStringLiteral("#e5e1d6");
static const QString kEditorBg = QStringLiteral("#fafaf6");
static const QString kActiveBg = QStringLiteral("#fef3c7");
static const QString kMutedText = QStringLiteral("#94a3b8");

QString sidebarStyle() {
    return QStringLiteral(
        "QWidget#noterSidebar { background: %1; }"
        "QLineEdit#noterSearch { border: 1px solid #d5d0c0; border-radius: 6px;"
        "  padding: 5px 9px; font-size: 12px; background: white; color: #525252; }"
        "QPushButton#noterNewBtn { background: %2; color: white; border: none;"
        "  border-radius: 6px; padding: 7px 10px; font-size: 13px; font-weight: 500; }"
        "QPushButton#noterNewBtn:hover { background: #b91c1c; }"
        "QPushButton#noterTodosBtn { background: transparent; border: none;"
        "  text-align: left; padding: 9px 14px; font-size: 13px; color: #525252;"
        "  border-top: 1px solid %3; }"
        "QPushButton#noterTodosBtn:hover { background: #ede9dc; }"
        "QListWidget#noterMeetingList { background: %1; border: none;"
        "  font-size: 13px; outline: none; }"
        "QListWidget#noterMeetingList::item { padding: 5px 14px 5px 22px; color: #525252; }"
        "QListWidget#noterMeetingList::item:hover { background: #ede9dc; }"
        "QListWidget#noterMeetingList::item:selected { background: %4;"
        "  color: #0a0d12; border-left: 3px solid %2; padding-left: 19px; }"
        // v0.1.95+ — QMenu must be styled inside the panel's QSS so the
        // right-click context menus aren't dark-on-dark (per the memory
        // rule feedback_qmenu_cascade_through_widget_qss).
        "QMenu { background: white; color: #111827; border: 1px solid #d5d0c0;"
        "  padding: 4px 0; }"
        "QMenu::item { padding: 6px 22px 6px 18px; }"
        "QMenu::item:selected { background: #FEF3C7; color: #0a0d12; }"
        "QMenu::separator { height: 1px; background: #e5e1d6; margin: 4px 8px; }"
        // Inline ✕ delete button on each meeting row (added via setItemWidget).
        "QToolButton#noterRowX { color: #94a3b8; background: transparent;"
        "  border: none; font-size: 14px; padding: 0 4px; }"
        "QToolButton#noterRowX:hover { color: #DC2626; background: #fee2e2;"
        "  border-radius: 3px; }"
    ).arg(kSidebarBg, kNoterRed, kSidebarBorder, kActiveBg);
}

QString editorPageStyle() {
    return QStringLiteral(
        "QWidget#noterEditorPage { background: %1; }"
        "QWidget#noterEditorFooter { background: %1; border-top: 1px solid #e5e1d6; }"
        "QTextEdit#noterEditor { background: %1; border: none;"
        "  padding: 32px 56px; font-family: 'IBM Plex Sans','Segoe UI',sans-serif;"
        "  font-size: 15px; color: #0a0d12; }"
        "QPushButton#noterExtractBtn { background: %2; color: white; border: none;"
        "  border-radius: 18px; padding: 7px 18px; font-size: 13px; font-weight: 500; }"
        "QPushButton#noterExtractBtn:hover { background: #b91c1c; }"
        "QLabel#noterSavedHint { color: #a0a0a0; font-size: 11px;"
        "  padding-right: 8px; }"
        "QLabel#noterModelLabel { color: #a0a0a0; font-size: 11px; }"
        "QComboBox#noterModelCombo { border: 1px solid #d5d0c0; border-radius: 4px;"
        "  padding: 3px 8px; font-size: 12px; min-width: 160px; background: white; color: #525252; }"
    ).arg(kEditorBg, kNoterRed);
}

QString emptyPageStyle() {
    return QStringLiteral(
        "QWidget#noterEmptyPage { background: %1; }"
        "QLabel#noterEmptyTitle { color: #c8c4b8; font-size: 28px; font-weight: 300; }"
        "QLabel#noterEmptySub { color: #a0a0a0; font-size: 14px; }"
        "QLabel#noterEmptyHint { color: #525252; font-size: 13px; padding: 12px 16px;"
        "  border: 1px dashed #d5d0c0; border-radius: 8px; background: white; }"
        "QLabel#noterEmptyNegs { color: #94a3b8; font-size: 12px; line-height: 1.7; }"
    ).arg(kEditorBg);
}

// Group a file's mtime into a recency bucket label.
QString recencyBucket(const QDateTime &mtime) {
    const QDate today = QDate::currentDate();
    const QDate mdate = mtime.date();
    if (mdate == today) return QStringLiteral("Today");
    if (mdate == today.addDays(-1)) return QStringLiteral("Yesterday");
    if (mtime.daysTo(QDateTime::currentDateTime()) <= 7)
        return QStringLiteral("This week");
    if (mdate.year() == today.year() && mdate.month() == today.month())
        return QStringLiteral("This month");
    return QStringLiteral("Older");
}

}  // namespace

// ═══════════════════════════════════════════════════════════════════════
//  NotesPanel — construction
// ═══════════════════════════════════════════════════════════════════════

NotesPanel::NotesPanel(QWidget *parent) : QWidget(parent) {
    ensureNotesFolder();
    m_storage = new NotesStorage(notesRoot(), this);
    m_todos = new NotesTodos(notesRoot() + QStringLiteral("/.notepatra/todos.db"));
    m_todos->open(nullptr);

    buildUi();
    refreshSidebar();
    refreshTodosBadge();
    showEmptyPage();

    // Autosave tick — 5s, configurable via global setting.
    m_autosave = new QTimer(this);
    connect(m_autosave, &QTimer::timeout, this, &NotesPanel::onAutoSaveTick);
    const int interval = qBound(1, Config::instance().autoSaveIntervalSec, 300) * 1000;
    m_autosave->start(interval);

    // ── shortcuts ───────────────────────────────────────────────────
    // All Noter-scoped (active when this widget is in the focus chain).
    auto bind = [this](const char *seq, auto fn) {
        auto *s = new QShortcut(QKeySequence(QLatin1String(seq)), this);
        s->setContext(Qt::WidgetWithChildrenShortcut);
        connect(s, &QShortcut::activated, this, fn);
    };
    bind("Ctrl+Alt+M", [this]() { newMeetingNote(); });
    bind("Ctrl+Alt+J", [this]() { quickSwitchMeeting(); });
    bind("Ctrl+Alt+T", [this]() { toggleTodosPane(); });
    bind("Ctrl+Alt+E", [this]() { endMeetingSweep(); });
    bind("Ctrl+Alt+B", [this]() { toggleSidebar(); });
    bind("Ctrl+Alt+P", [this]() { popOutActive(); });
    bind("F4",         [this]() { toggleCheckboxOnCurrentLine(); });
}

NotesPanel::~NotesPanel() {
    if (m_dirty && !m_currentPath.isEmpty()) saveCurrentNote();
    if (m_todos) { delete m_todos; m_todos = nullptr; }
}

void NotesPanel::buildUi() {
    auto *outer = new QVBoxLayout(this);
    outer->setContentsMargins(0, 0, 0, 0);
    outer->setSpacing(0);

    m_splitter = new QSplitter(Qt::Horizontal, this);
    m_splitter->setChildrenCollapsible(false);
    m_splitter->setHandleWidth(1);
    m_splitter->setStyleSheet(
        QStringLiteral("QSplitter::handle { background: %1; }").arg(kSidebarBorder));

    m_sidebar = buildSidebar();
    m_splitter->addWidget(m_sidebar);

    m_rightStack = new QStackedWidget(this);
    m_emptyPage = buildEmptyPage();
    m_editorPage = buildEditorPage();
    m_rightStack->addWidget(m_emptyPage);
    m_rightStack->addWidget(m_editorPage);
    m_splitter->addWidget(m_rightStack);

    m_splitter->setStretchFactor(0, 0);
    m_splitter->setStretchFactor(1, 1);
    m_splitter->setSizes({240, 860});

    outer->addWidget(m_splitter, 1);
}

QWidget *NotesPanel::buildSidebar() {
    auto *w = new QWidget(this);
    w->setObjectName("noterSidebar");
    w->setStyleSheet(sidebarStyle());
    w->setMinimumWidth(180);
    w->setMaximumWidth(360);

    auto *v = new QVBoxLayout(w);
    v->setContentsMargins(0, 0, 0, 0);
    v->setSpacing(0);

    // Search
    auto *searchWrap = new QWidget(w);
    auto *searchLayout = new QHBoxLayout(searchWrap);
    searchLayout->setContentsMargins(12, 12, 12, 8);
    searchLayout->setSpacing(0);
    m_search = new QLineEdit(searchWrap);
    m_search->setObjectName("noterSearch");
    m_search->setPlaceholderText(tr("🔍  Search meetings…"));
    m_search->setClearButtonEnabled(true);
    searchLayout->addWidget(m_search);
    v->addWidget(searchWrap);
    connect(m_search, &QLineEdit::textChanged, this, &NotesPanel::onSearchChanged);

    // + New meeting
    auto *newWrap = new QWidget(w);
    auto *newLayout = new QHBoxLayout(newWrap);
    newLayout->setContentsMargins(12, 4, 12, 8);
    m_newBtn = new QPushButton(tr("+ New meeting"), newWrap);
    m_newBtn->setObjectName("noterNewBtn");
    m_newBtn->setCursor(Qt::PointingHandCursor);
    newLayout->addWidget(m_newBtn);
    v->addWidget(newWrap);
    connect(m_newBtn, &QPushButton::clicked, this, &NotesPanel::onNewMeetingClicked);

    // Meeting list
    m_meetingList = new QListWidget(w);
    m_meetingList->setObjectName("noterMeetingList");
    m_meetingList->setFrameShape(QFrame::NoFrame);
    m_meetingList->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_meetingList->setContextMenuPolicy(Qt::CustomContextMenu);
    v->addWidget(m_meetingList, 1);
    connect(m_meetingList, &QListWidget::itemActivated,
            this, &NotesPanel::onMeetingItemActivated);
    connect(m_meetingList, &QListWidget::itemClicked,
            this, &NotesPanel::onMeetingItemActivated);
    // v0.1.95+ — right-click → Delete (move to Trash) / Restore (when
    // the item is in Trash) / Delete permanently / Open. Trashed items
    // get a different menu so the user can recover them.
    connect(m_meetingList, &QListWidget::customContextMenuRequested,
            this, [this](const QPoint &pos) {
                QListWidgetItem *it = m_meetingList->itemAt(pos);
                if (!it) return;
                const QString path = it->data(Qt::UserRole).toString();
                if (path.isEmpty() || path == QStringLiteral("__header__")) return;
                const bool isTrash = path.contains(QStringLiteral("/Trash/"));
                QMenu menu(this);
                // v0.1.95+ — force light styling on the menu directly so it
                // doesn't inherit a dark MainWindow QSS. See
                // feedback_qmenu_cascade_through_widget_qss.
                menu.setStyleSheet(QStringLiteral(
                    "QMenu { background: #FFFFFF; color: #111827;"
                    "  border: 1px solid #d5d0c0; padding: 4px 0; }"
                    "QMenu::item { padding: 7px 24px 7px 22px; font-size: 13px; }"
                    "QMenu::item:selected { background: #FEF3C7; color: #0a0d12; }"
                    "QMenu::separator { height: 1px; background: #e5e1d6;"
                    "  margin: 4px 8px; }"
                ));
                if (isTrash) {
                    QAction *aOpen     = menu.addAction(tr("Open"));
                    menu.addSeparator();
                    QAction *aRestore  = menu.addAction(tr("↺ Restore"));
                    QAction *aPurge    = menu.addAction(tr("⚠ Delete permanently"));
                    QAction *picked = menu.exec(m_meetingList->mapToGlobal(pos));
                    if (!picked) return;
                    if (picked == aOpen) {
                        openNoteFile(path);
                    } else if (picked == aRestore) {
                        const QString restored = QDir(inboxFolder())
                            .absoluteFilePath(QFileInfo(path).fileName()
                                .remove(QRegExp(QStringLiteral("^\\.trashed-\\d+-"))));
                        if (QFile::rename(path, restored)) refreshSidebar();
                    } else if (picked == aPurge) {
                        if (QMessageBox::warning(this, tr("Delete permanently?"),
                            tr("Permanently delete this meeting?\n\n%1")
                                .arg(QFileInfo(path).fileName()),
                            QMessageBox::Yes | QMessageBox::No, QMessageBox::No)
                            == QMessageBox::Yes) {
                            // Sweep the backup ring too so nothing lingers.
                            QDir d(QFileInfo(path).dir());
                            const QString stem = QFileInfo(path).fileName();
                            for (const QString &f : d.entryList(
                                     QStringList() << stem + "*", QDir::Files)) {
                                QFile::remove(d.absoluteFilePath(f));
                            }
                            refreshSidebar();
                        }
                    }
                } else {
                    QAction *aOpen   = menu.addAction(tr("Open"));
                    menu.addSeparator();
                    QAction *aDelete = menu.addAction(tr("🗑  Delete"));
                    QAction *picked = menu.exec(m_meetingList->mapToGlobal(pos));
                    if (!picked) return;
                    if (picked == aOpen) {
                        openNoteFile(path);
                    } else if (picked == aDelete) {
                        // Move to Trash with a timestamp prefix to avoid
                        // collisions if two files were trashed at the same
                        // logical second.
                        const QString srcName = QFileInfo(path).fileName();
                        const QString trashed = QStringLiteral(".trashed-%1-%2")
                            .arg(QDateTime::currentMSecsSinceEpoch())
                            .arg(srcName);
                        const QString dest = QDir(trashFolder())
                                                  .absoluteFilePath(trashed);
                        if (path == m_currentPath) {
                            // Switch off this file before moving it.
                            m_dirty = false;
                            showEmptyPage();
                            m_currentPath.clear();
                        }
                        if (QFile::rename(path, dest)) refreshSidebar();
                    }
                }
            });

    // All Todos toggle at bottom
    m_todosBtn = new QPushButton(w);
    m_todosBtn->setObjectName("noterTodosBtn");
    m_todosBtn->setCursor(Qt::PointingHandCursor);
    m_todosBtn->setText(tr("📋  All Todos"));
    v->addWidget(m_todosBtn);
    connect(m_todosBtn, &QPushButton::clicked, this, &NotesPanel::onTodosToggleClicked);

    return w;
}

QWidget *NotesPanel::buildEditorPage() {
    auto *w = new QWidget(this);
    w->setObjectName("noterEditorPage");
    w->setStyleSheet(editorPageStyle());

    auto *v = new QVBoxLayout(w);
    v->setContentsMargins(0, 0, 0, 0);
    v->setSpacing(0);

    // Top edge — "saved 2s ago" hint, right-aligned, very muted
    auto *topBar = new QWidget(w);
    auto *topLayout = new QHBoxLayout(topBar);
    topLayout->setContentsMargins(0, 6, 56, 0);
    topLayout->addStretch(1);
    m_savedHint = new QLabel(tr("auto-saved"), topBar);
    m_savedHint->setObjectName("noterSavedHint");
    topLayout->addWidget(m_savedHint);
    v->addWidget(topBar);

    // The QTextEdit body — that's the entire surface.
    m_editor = new QTextEdit(w);
    m_editor->setObjectName("noterEditor");
    m_editor->setAcceptRichText(true);
    m_editor->setUndoRedoEnabled(true);
    m_editor->installEventFilter(this);
    m_editor->viewport()->installEventFilter(this);
    v->addWidget(m_editor, 1);
    connect(m_editor, &QTextEdit::textChanged, this, &NotesPanel::onEditorBodyChanged);

    // ─── Footer bar ────────────────────────────────────────────────
    // Hosts the model picker (left) and the ✨ Extract button (right).
    // Plain row at the bottom of the editor page — no overlay magic.
    m_editorFooter = new QWidget(w);
    m_editorFooter->setObjectName("noterEditorFooter");
    auto *fl = new QHBoxLayout(m_editorFooter);
    fl->setContentsMargins(20, 8, 20, 10);
    fl->setSpacing(8);

    auto *modelLbl = new QLabel(tr("AI:"), m_editorFooter);
    modelLbl->setObjectName("noterModelLabel");
    fl->addWidget(modelLbl);

    m_modelCombo = new QComboBox(m_editorFooter);
    m_modelCombo->setObjectName("noterModelCombo");
    m_modelCombo->setToolTip(tr("Model used by ✨ Extract — auto-populated from your local Ollama / configured backend."));
    m_modelCombo->setEditable(false);
    // Seed with the persisted choice so it's visible even before listModels
    // round-trips.
    if (!Config::instance().aiNoterModel.isEmpty()) {
        m_modelCombo->addItem(Config::instance().aiNoterModel);
    } else {
        m_modelCombo->addItem(QStringLiteral("(loading…)"));
    }
    fl->addWidget(m_modelCombo, 0);
    // Persist on change.
    connect(m_modelCombo, QOverload<const QString &>::of(&QComboBox::currentTextChanged),
            this, [](const QString &model) {
                if (model.isEmpty() || model == QStringLiteral("(loading…)")) return;
                Config::instance().aiNoterModel = model;
                Config::instance().save();
            });
    // Async populate from the backend.
    {
        auto *listClient = new OllamaClient(this);
        listClient->setBackend(OllamaClient::backendFromString(
            Config::instance().aiBackend.isEmpty() ? QStringLiteral("Ollama")
                                                   : Config::instance().aiBackend));
        if (!Config::instance().aiBaseUrl.isEmpty())
            listClient->setBaseUrl(Config::instance().aiBaseUrl);
        connect(listClient, &OllamaClient::modelsListed, this,
                [this, listClient](const QStringList &models) {
                    listClient->deleteLater();
                    if (!m_modelCombo) return;
                    const QString currentPick = Config::instance().aiNoterModel;
                    m_modelCombo->blockSignals(true);
                    m_modelCombo->clear();
                    m_modelCombo->addItems(models);
                    if (!currentPick.isEmpty() && models.contains(currentPick)) {
                        m_modelCombo->setCurrentText(currentPick);
                    } else if (!models.isEmpty()) {
                        m_modelCombo->setCurrentIndex(0);
                        Config::instance().aiNoterModel = m_modelCombo->currentText();
                        Config::instance().save();
                    }
                    m_modelCombo->blockSignals(false);
                });
        connect(listClient, &OllamaClient::modelsError, this,
                [this, listClient](const QString &) {
                    listClient->deleteLater();
                    if (!m_modelCombo) return;
                    if (m_modelCombo->count() == 1 &&
                        m_modelCombo->itemText(0) == QStringLiteral("(loading…)")) {
                        m_modelCombo->clear();
                        m_modelCombo->addItem(QStringLiteral("(no models — start Ollama)"));
                    }
                });
        listClient->listModels();
    }

    fl->addStretch(1);

    m_extractBtn = new QPushButton(tr("✨  Extract"), m_editorFooter);
    m_extractBtn->setObjectName("noterExtractBtn");
    m_extractBtn->setCursor(Qt::PointingHandCursor);
    m_extractBtn->setToolTip(tr("AI: extract todos and reminders (Ctrl+Alt+E)"));
    m_extractBtn->setMinimumWidth(108);
    connect(m_extractBtn, &QPushButton::clicked, this, &NotesPanel::onExtractClicked);
    fl->addWidget(m_extractBtn);

    v->addWidget(m_editorFooter, 0);
    return w;
}

QWidget *NotesPanel::buildEmptyPage() {
    auto *w = new QWidget(this);
    w->setObjectName("noterEmptyPage");
    w->setStyleSheet(emptyPageStyle());

    auto *outer = new QVBoxLayout(w);
    outer->addStretch(1);

    auto *title = new QLabel(tr("Noter"), w);
    title->setObjectName("noterEmptyTitle");
    title->setAlignment(Qt::AlignCenter);
    outer->addWidget(title);

    auto *sub = new QLabel(tr("Your meetings, your typing. AI helps after — not during."), w);
    sub->setObjectName("noterEmptySub");
    sub->setAlignment(Qt::AlignCenter);
    outer->addWidget(sub);

    outer->addSpacing(28);

    auto *hint = new QLabel(
        tr("Press <b style='color:%1'>Ctrl+Alt+M</b> to start a new meeting note.")
            .arg(kNoterRed), w);
    hint->setObjectName("noterEmptyHint");
    hint->setAlignment(Qt::AlignCenter);
    hint->setTextFormat(Qt::RichText);
    auto *hintWrap = new QHBoxLayout;
    hintWrap->addStretch(1);
    hintWrap->addWidget(hint);
    hintWrap->addStretch(1);
    outer->addLayout(hintWrap);

    outer->addSpacing(28);

    auto *negs = new QLabel(
        tr("Noter does not record audio.<br>"
           "Noter does not join calls.<br>"
           "Noter does not need the internet."), w);
    negs->setObjectName("noterEmptyNegs");
    negs->setAlignment(Qt::AlignCenter);
    negs->setTextFormat(Qt::RichText);
    outer->addWidget(negs);

    outer->addStretch(2);
    return w;
}

// ═══════════════════════════════════════════════════════════════════════
//  Folder helpers
// ═══════════════════════════════════════════════════════════════════════

QString NotesPanel::defaultNotesFolder() const {
    const QString docs =
        QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation);
    return docs + QStringLiteral("/Notepatra/Noter");
}

QString NotesPanel::notesRoot() const { return defaultNotesFolder(); }
QString NotesPanel::inboxFolder() const { return notesRoot() + QStringLiteral("/Inbox"); }
QString NotesPanel::trashFolder() const { return notesRoot() + QStringLiteral("/Trash"); }

void NotesPanel::ensureNotesFolder() {
    QDir().mkpath(inboxFolder());
    QDir().mkpath(trashFolder());
    QDir().mkpath(notesRoot() + QStringLiteral("/.notepatra"));
}

QString NotesPanel::slugifyTitle(const QString &title) const {
    return NotesStorage::safeFilename(title);
}

// ═══════════════════════════════════════════════════════════════════════
//  Sidebar refresh
// ═══════════════════════════════════════════════════════════════════════

void NotesPanel::refreshSidebar() {
    if (!m_meetingList) return;
    const QString filter = m_search ? m_search->text().trimmed().toLower() : QString();

    m_meetingList->clear();
    QDir d(inboxFolder());
    QFileInfoList all = d.entryInfoList(QStringList() << QStringLiteral("*.html"),
                                        QDir::Files, QDir::Time);

    QString lastBucket;
    for (const QFileInfo &fi : all) {
        const QString name = fi.fileName();
        if (name.endsWith(QStringLiteral(".bak1")) ||
            name.endsWith(QStringLiteral(".bak2")) ||
            name.endsWith(QStringLiteral(".bak3")) ||
            name.endsWith(QStringLiteral(".bak4")) ||
            name.endsWith(QStringLiteral(".bak5")) ||
            name.endsWith(QStringLiteral(".draft")) ||
            name.endsWith(QStringLiteral(".lock")) ||
            name.endsWith(QStringLiteral(".tmp"))) continue;

        // Derive a friendly display title from the filename. Strip the
        // date-prefix slug + extension. Fall back to the bare filename.
        QString display = fi.completeBaseName();
        // Pattern: YYYY-MM-DD-HHMM-slug → keep "slug"
        const QRegExp datePrefix(QStringLiteral("^\\d{4}-\\d{2}-\\d{2}-\\d{4}-"));
        display.remove(datePrefix);
        display.replace(QChar('-'), QChar(' '));
        if (display.isEmpty()) display = fi.completeBaseName();

        // Filter by search text.
        if (!filter.isEmpty() && !display.toLower().contains(filter))
            continue;

        const QString bucket = recencyBucket(fi.lastModified());
        if (bucket != lastBucket) {
            auto *header = new QListWidgetItem(bucket.toUpper(), m_meetingList);
            QFont f = header->font();
            f.setPointSize(qMax(7, f.pointSize() - 2));
            f.setLetterSpacing(QFont::AbsoluteSpacing, 1);
            header->setFont(f);
            header->setForeground(QColor(kMutedText));
            header->setFlags(Qt::NoItemFlags);  // not selectable
            header->setData(Qt::UserRole, QStringLiteral("__header__"));
            lastBucket = bucket;
        }

        const QString absPath = fi.absoluteFilePath();
        auto *item = new QListWidgetItem(m_meetingList);
        item->setData(Qt::UserRole, absPath);
        item->setToolTip(QDir::toNativeSeparators(absPath));

        // Custom row widget: title label + ✕ button.
        auto *rowW = new QWidget(m_meetingList);
        auto *rowL = new QHBoxLayout(rowW);
        rowL->setContentsMargins(22, 4, 8, 4);
        rowL->setSpacing(4);
        auto *titleLbl = new QLabel(display, rowW);
        titleLbl->setStyleSheet(QStringLiteral("color: #525252;"));
        titleLbl->setTextInteractionFlags(Qt::NoTextInteraction);
        titleLbl->setAttribute(Qt::WA_TransparentForMouseEvents);
        rowL->addWidget(titleLbl, 1);
        auto *xBtn = new QToolButton(rowW);
        xBtn->setObjectName("noterRowX");
        xBtn->setText(QStringLiteral("✕"));
        xBtn->setCursor(Qt::PointingHandCursor);
        xBtn->setToolTip(tr("Move to Trash"));
        xBtn->setFixedSize(20, 20);
        rowL->addWidget(xBtn, 0);
        m_meetingList->setItemWidget(item, rowW);
        item->setSizeHint(rowW->sizeHint());

        // Click ✕ → move file to Trash. Stops propagation so the row
        // doesn't also activate.
        connect(xBtn, &QToolButton::clicked, this,
                [this, absPath]() {
                    const QString srcName = QFileInfo(absPath).fileName();
                    const QString trashed = QStringLiteral(".trashed-%1-%2")
                        .arg(QDateTime::currentMSecsSinceEpoch())
                        .arg(srcName);
                    const QString dest = QDir(trashFolder())
                                              .absoluteFilePath(trashed);
                    if (absPath == m_currentPath) {
                        m_dirty = false;
                        showEmptyPage();
                        m_currentPath.clear();
                    }
                    if (QFile::rename(absPath, dest)) refreshSidebar();
                });

        // Re-select if it's the currently-open note
        if (absPath == m_currentPath) {
            m_meetingList->setCurrentItem(item);
        }
    }

    // ── TRASH group (only rendered when non-empty) ────────────────
    // QDir::Hidden — files starting with "." are hidden on Linux, default
    // filters exclude them. We use ".trashed-*" prefix so they don't
    // clutter file managers; need explicit Hidden flag to read them back.
    QDir tdir(trashFolder());
    QFileInfoList trashed = tdir.entryInfoList(QStringList() << ".trashed-*",
                                               QDir::Files | QDir::Hidden,
                                               QDir::Time);
    if (!trashed.isEmpty()) {
        auto *header = new QListWidgetItem(QStringLiteral("TRASH"), m_meetingList);
        QFont f = header->font();
        f.setPointSize(qMax(7, f.pointSize() - 2));
        f.setLetterSpacing(QFont::AbsoluteSpacing, 1);
        header->setFont(f);
        header->setForeground(QColor(kMutedText));
        header->setFlags(Qt::NoItemFlags);
        header->setData(Qt::UserRole, QStringLiteral("__header__"));

        for (const QFileInfo &fi : trashed) {
            QString display = fi.fileName();
            // Strip ".trashed-<timestamp>-" prefix.
            display.remove(QRegExp(QStringLiteral("^\\.trashed-\\d+-")));
            // Strip date prefix + extension for display.
            display.remove(QRegExp(QStringLiteral("^\\d{4}-\\d{2}-\\d{2}-\\d{4}-")));
            display.remove(QStringLiteral(".html"));
            display.replace(QChar('-'), QChar(' '));
            if (!filter.isEmpty() && !display.toLower().contains(filter))
                continue;

            const QString absPath = fi.absoluteFilePath();
            auto *item = new QListWidgetItem(m_meetingList);
            item->setData(Qt::UserRole, absPath);
            item->setToolTip(QDir::toNativeSeparators(absPath) +
                             QStringLiteral("\n\n↺ to restore · right-click to delete permanently"));

            auto *rowW = new QWidget(m_meetingList);
            auto *rowL = new QHBoxLayout(rowW);
            rowL->setContentsMargins(22, 4, 8, 4);
            rowL->setSpacing(4);
            auto *titleLbl = new QLabel(QStringLiteral("🗑  ") + display, rowW);
            QFont itf = titleLbl->font();
            itf.setItalic(true);
            titleLbl->setFont(itf);
            titleLbl->setStyleSheet(QStringLiteral("color: %1;").arg(kMutedText));
            titleLbl->setAttribute(Qt::WA_TransparentForMouseEvents);
            rowL->addWidget(titleLbl, 1);

            auto *restoreBtn = new QToolButton(rowW);
            restoreBtn->setObjectName("noterRowX");
            restoreBtn->setText(QStringLiteral("↺"));
            restoreBtn->setCursor(Qt::PointingHandCursor);
            restoreBtn->setToolTip(tr("Restore from Trash"));
            restoreBtn->setFixedSize(20, 20);
            rowL->addWidget(restoreBtn, 0);
            m_meetingList->setItemWidget(item, rowW);
            item->setSizeHint(rowW->sizeHint());

            connect(restoreBtn, &QToolButton::clicked, this,
                    [this, absPath]() {
                        const QString restored = QDir(inboxFolder())
                            .absoluteFilePath(QFileInfo(absPath).fileName()
                                .remove(QRegExp(QStringLiteral("^\\.trashed-\\d+-"))));
                        if (QFile::rename(absPath, restored)) refreshSidebar();
                    });
        }
    }
}

void NotesPanel::refreshTodosBadge() {
    if (!m_todosBtn || !m_todos) return;
    const QDateTime now = QDateTime::currentDateTime();
    const int openTotal = m_todos->dueGroupOverdue(now).size()
                        + m_todos->dueGroupToday(now).size()
                        + m_todos->dueGroupWeek(now).size()
                        + m_todos->dueGroupSomeday(now).size();
    m_todosBtn->setText(openTotal > 0
                            ? tr("📋  All Todos  (%1)").arg(openTotal)
                            : tr("📋  All Todos"));
}

// ═══════════════════════════════════════════════════════════════════════
//  State transitions
// ═══════════════════════════════════════════════════════════════════════

void NotesPanel::showEmptyPage() {
    if (!m_rightStack) return;
    m_rightStack->setCurrentWidget(m_emptyPage);
    m_currentPath.clear();
    m_currentTitle.clear();
}

void NotesPanel::showEditorPage() {
    if (!m_rightStack || !m_editorPage) return;
    m_rightStack->setCurrentWidget(m_editorPage);
}

// ═══════════════════════════════════════════════════════════════════════
//  Public actions
// ═══════════════════════════════════════════════════════════════════════

void NotesPanel::newMeetingNote() {
    if (m_dirty && !m_currentPath.isEmpty()) saveCurrentNote();

    const QDateTime now = QDateTime::currentDateTime();
    const QString stamp = now.toString(QStringLiteral("yyyy-MM-dd-hhmm"));
    const QString name = QStringLiteral("%1-untitled-meeting.html").arg(stamp);
    const QString abs = QDir(inboxFolder()).absoluteFilePath(name);

    const QString html = m_storage->newNoteHtml(
        tr("Untitled meeting"), now, QStringList());
    m_storage->saveNote(abs, html, nullptr);

    refreshSidebar();
    openNoteFile(abs);
}

void NotesPanel::openNoteFile(const QString &absolutePath) {
    if (m_dirty && !m_currentPath.isEmpty() && m_currentPath != absolutePath) {
        saveCurrentNote();
    }
    renderNoteAtPath(absolutePath);
    showEditorPage();
    refreshSidebar();
    refreshTodosBadge();
}

void NotesPanel::renderNoteAtPath(const QString &absolutePath) {
    if (absolutePath.isEmpty() || !m_editor) return;
    const QString html = m_storage->readNote(absolutePath, nullptr);
    m_loadingInProgress = true;
    m_editor->blockSignals(true);
    m_editor->setHtml(html);
    m_editor->blockSignals(false);
    m_loadingInProgress = false;
    m_currentPath = absolutePath;
    m_dirty = false;

    // Move caret to end of body so user can start typing immediately.
    QTextCursor cur = m_editor->textCursor();
    cur.movePosition(QTextCursor::End);
    m_editor->setTextCursor(cur);
    m_editor->setFocus();

    // Update tab title via signal (mainwindow listens).
    m_currentTitle = QFileInfo(absolutePath).completeBaseName();
    emit noteTitleChanged(m_currentTitle);
}

void NotesPanel::saveCurrentNote() {
    if (m_currentPath.isEmpty() || !m_editor) return;
    if (!m_dirty) return;
    const QString body = m_editor->toHtml();
    QString err;
    if (!m_storage->saveNote(m_currentPath, body, &err)) {
        // Don't pop a dialog on every autosave failure — the user will see
        // the "saved" hint disappear. Log to stderr.
        fprintf(stderr, "Noter: saveNote failed: %s\n", qPrintable(err));
        return;
    }
    m_dirty = false;
    m_lastSavedAt = QDateTime::currentDateTime();
    if (m_savedHint) m_savedHint->setText(tr("auto-saved"));

    // Re-index todos so the badge / panel reflects current state.
    if (m_todos) m_todos->reindexNote(m_currentPath, body);
    refreshTodosBadge();
    if (m_todosPane && m_todosPane->isVisible()) refreshTodosPanel();

    emit noteSaved(m_currentPath);
}

void NotesPanel::popOutActive() {
    if (m_currentPath.isEmpty()) return;
    if (m_popOut) {
        m_popOut->raise();
        m_popOut->activateWindow();
        return;
    }
    m_popOut = new NoterPopOut(m_currentPath);
    connect(m_popOut, &QObject::destroyed, this, [this]() { m_popOut = nullptr; });
    m_popOut->show();
}

void NotesPanel::endMeetingSweep() {
    if (m_currentPath.isEmpty() || !m_editor) {
        QMessageBox::information(this, tr("Extract"),
                                 tr("Open or create a meeting note first."));
        return;
    }
    const QString bodyHtml = m_editor->toHtml();
    if (m_editor->toPlainText().trimmed().isEmpty()) {
        QMessageBox::information(this, tr("Extract"),
                                 tr("Nothing to extract — the note is empty."));
        return;
    }

    // Build the prompt + spawn an Ollama request. The model is whatever
    // the user picked in Config::aiNoterModel; baseUrl + backend come
    // from the global AI settings so cloud-via-OpenAI-compat works too.
    const QString prompt = NoterSweepPrompt::build(bodyHtml, m_currentTitle);
    const QString model = Config::instance().aiNoterModel.isEmpty()
                              ? QStringLiteral("llama3.1:8b")
                              : Config::instance().aiNoterModel;

    auto *client = new OllamaClient(this);
    client->setBackend(OllamaClient::backendFromString(
        Config::instance().aiBackend.isEmpty() ? QStringLiteral("Ollama")
                                               : Config::instance().aiBackend));
    if (!Config::instance().aiBaseUrl.isEmpty())
        client->setBaseUrl(Config::instance().aiBaseUrl);
    client->setModel(model);
    QApplication::setOverrideCursor(Qt::WaitCursor);

    connect(client, &OllamaClient::finished, this,
            [this, client, model](const QString &response) {
                QApplication::restoreOverrideCursor();
                client->deleteLater();
                NoterSweepPrompt::SweepResult result =
                    NoterSweepPrompt::parse(response);
                if (result.actions.isEmpty() && result.decisions.isEmpty() &&
                    result.questions.isEmpty() && result.risks.isEmpty()) {
                    QMessageBox::information(this, tr("Extract"),
                        tr("AI didn't find any actionable items in this note."));
                    return;
                }
                NoterSweepDialog dlg(result, this);
                dlg.setEyebrow(model, 0);
                dlg.setTargetPath(m_currentPath);
                if (dlg.exec() == QDialog::Accepted) {
                    // Apply: append a markdown-style "Action Items" section
                    // at the bottom of the body, then save.
                    const auto finalResult = dlg.finalResult();
                    QTextCursor cur(m_editor->document());
                    cur.movePosition(QTextCursor::End);
                    cur.insertBlock();
                    QTextCharFormat hdr;
                    hdr.setFontWeight(QFont::Bold);
                    cur.insertText(tr("Action Items"), hdr);
                    cur.insertBlock();
                    QTextCharFormat plain;
                    for (const auto &item : finalResult.actions) {
                        QString line = QStringLiteral("☐ ") + item.text;
                        if (!item.owner.isEmpty()) line += QStringLiteral("  ") + item.owner;
                        if (item.dueAt.isValid()) {
                            line += QStringLiteral("  ⏰ ") +
                                    item.dueAt.toString(
                                        QStringLiteral("MMM d HH:mm"));
                        }
                        cur.insertText(line, plain);
                        cur.insertBlock();
                    }
                    m_dirty = true;
                    saveCurrentNote();
                }
            });
    connect(client, &OllamaClient::error, this,
            [this, client](const QString &err) {
                QApplication::restoreOverrideCursor();
                client->deleteLater();
                QMessageBox::warning(this, tr("Extract failed"),
                    tr("Could not reach the AI backend: %1").arg(err));
            });
    client->generate(prompt, QString(), /*enableThinking=*/false);
}

// ═══════════════════════════════════════════════════════════════════════
//  Slots
// ═══════════════════════════════════════════════════════════════════════

void NotesPanel::onAutoSaveTick() {
    if (m_dirty && !m_currentPath.isEmpty()) saveCurrentNote();
}

void NotesPanel::onMeetingItemActivated(QListWidgetItem *item) {
    if (!item) return;
    const QString path = item->data(Qt::UserRole).toString();
    if (path == QStringLiteral("__header__") || path.isEmpty()) return;
    openNoteFile(path);
}

void NotesPanel::onSearchChanged(const QString &) {
    refreshSidebar();
}

void NotesPanel::onNewMeetingClicked() {
    newMeetingNote();
}

void NotesPanel::onTodosToggleClicked() {
    toggleTodosPane();
}

void NotesPanel::onExtractClicked() {
    endMeetingSweep();
}

void NotesPanel::onEditorBodyChanged() {
    if (m_loadingInProgress) return;
    m_dirty = true;
    if (m_savedHint) m_savedHint->setText(tr("editing… (auto-saves in 5s)"));
}

// ═══════════════════════════════════════════════════════════════════════
//  Editor behaviors — markdown shortcuts + checkbox clicks
// ═══════════════════════════════════════════════════════════════════════

bool NotesPanel::eventFilter(QObject *watched, QEvent *event) {
    if (!m_editor) return QWidget::eventFilter(watched, event);

    // Click on the editor viewport — check whether the click landed on a
    // "☐ " or "✓ " character. If so, toggle it.
    if (watched == m_editor->viewport() && event->type() == QEvent::MouseButtonRelease) {
        auto *me = static_cast<QMouseEvent *>(event);
        if (me->button() == Qt::LeftButton) {
            QTextCursor cur = m_editor->cursorForPosition(me->pos());
            const QTextBlock block = cur.block();
            const QString line = block.text();
            if (line.startsWith(QStringLiteral("☐ ")) ||
                line.startsWith(QStringLiteral("✓ "))) {
                // Toggle if click was within the first ~3 characters.
                cur.setPosition(block.position());
                cur.movePosition(QTextCursor::Right, QTextCursor::KeepAnchor, 2);
                const QString token = cur.selectedText();
                if (token == QStringLiteral("☐ ")) {
                    cur.insertText(QStringLiteral("✓ "));
                    m_dirty = true;
                    return true;
                } else if (token == QStringLiteral("✓ ")) {
                    cur.insertText(QStringLiteral("☐ "));
                    m_dirty = true;
                    return true;
                }
            }
        }
        return false;
    }

    // Key events in the editor — handle markdown shortcut & Enter on ☐.
    if (watched == m_editor && event->type() == QEvent::KeyPress) {
        auto *ke = static_cast<QKeyEvent *>(event);

        // Enter on a "☐ ..." line creates a new "☐ " on the next line so
        // typing a list flows naturally.
        if (ke->key() == Qt::Key_Return && !(ke->modifiers() & Qt::ShiftModifier)) {
            QTextCursor cur = m_editor->textCursor();
            const QString line = cur.block().text();
            if (line.startsWith(QStringLiteral("☐ ")) ||
                line.startsWith(QStringLiteral("✓ "))) {
                if (line.length() <= 2) {
                    // Empty checkbox — break out of the list, clear the marker.
                    cur.movePosition(QTextCursor::StartOfBlock);
                    cur.movePosition(QTextCursor::EndOfBlock,
                                     QTextCursor::KeepAnchor);
                    cur.removeSelectedText();
                    return true;  // suppress the Return — line is now empty
                }
                cur.insertBlock();
                cur.insertText(QStringLiteral("☐ "));
                m_dirty = true;
                return true;
            }
        }

        // F4 → toggle checkbox on current line.
        if (ke->key() == Qt::Key_F4) {
            toggleCheckboxOnCurrentLine();
            return true;
        }

        // Markdown auto-replace: "- [ ] " → "☐ ", "- [x] " → "✓ ".
        // Trigger on Space — check the prefix of the current line.
        if (ke->key() == Qt::Key_Space) {
            QTextCursor cur = m_editor->textCursor();
            const QString line = cur.block().text();
            const int colInBlock = cur.positionInBlock();
            if (colInBlock == 5 && line == QStringLiteral("- [ ]")) {
                cur.movePosition(QTextCursor::StartOfBlock);
                cur.movePosition(QTextCursor::EndOfBlock,
                                 QTextCursor::KeepAnchor);
                cur.insertText(QStringLiteral("☐ "));
                m_dirty = true;
                return true;
            }
            if (colInBlock == 5 && line == QStringLiteral("- [x]")) {
                cur.movePosition(QTextCursor::StartOfBlock);
                cur.movePosition(QTextCursor::EndOfBlock,
                                 QTextCursor::KeepAnchor);
                cur.insertText(QStringLiteral("✓ "));
                m_dirty = true;
                return true;
            }
        }
    }
    return QWidget::eventFilter(watched, event);
}

void NotesPanel::toggleCheckboxOnCurrentLine() {
    if (!m_editor) return;
    QTextCursor cur = m_editor->textCursor();
    const QTextBlock block = cur.block();
    const QString line = block.text();
    cur.beginEditBlock();
    cur.setPosition(block.position());
    cur.movePosition(QTextCursor::EndOfBlock, QTextCursor::KeepAnchor);
    if (line.startsWith(QStringLiteral("☐ "))) {
        cur.insertText(QStringLiteral("✓ ") + line.mid(2));
    } else if (line.startsWith(QStringLiteral("✓ "))) {
        cur.insertText(QStringLiteral("☐ ") + line.mid(2));
    } else {
        cur.insertText(QStringLiteral("☐ ") + line);
    }
    cur.endEditBlock();
    m_dirty = true;
}

void NotesPanel::insertCheckboxAtCursor() {
    if (!m_editor) return;
    QTextCursor cur = m_editor->textCursor();
    cur.insertText(QStringLiteral("☐ "));
    m_editor->setFocus();
    m_dirty = true;
}

// ═══════════════════════════════════════════════════════════════════════
//  Pane toggles + nav
// ═══════════════════════════════════════════════════════════════════════

void NotesPanel::toggleSidebar() {
    if (!m_sidebar) return;
    m_sidebar->setVisible(!m_sidebar->isVisible());
}

void NotesPanel::toggleTodosPane() {
    if (!m_todosPane) {
        m_todosPane = new NoterTodosPanel(this);
        m_splitter->addWidget(m_todosPane);
        m_splitter->setStretchFactor(2, 0);

        connect(m_todosPane, &NoterTodosPanel::todoActivated,
                this, [this](const QString &src, const QString &) {
                    if (!src.isEmpty()) openNoteFile(src);
                });
        connect(m_todosPane, &NoterTodosPanel::closeRequested,
                m_todosPane, &QWidget::hide);
        connect(m_todosPane, &NoterTodosPanel::todoTextEdited,
                this, [this](const QString &id, const QString &newText) {
                    if (m_todos) m_todos->setText(id, newText);
                    refreshTodosPanel();
                });
        connect(m_todosPane, &NoterTodosPanel::addTodoRequested,
                this, [this](const QString &text) {
                    if (!m_todos) return;
                    m_todos->addQuickTodo(text, QString(), QDateTime());
                    refreshTodosPanel();
                    refreshTodosBadge();
                });
        connect(m_todosPane, &NoterTodosPanel::addTodoWithDateRequested,
                this, [this](const QString &text, const QDateTime &due,
                             const QDateTime &remind) {
                    if (!m_todos) return;
                    const QString id = m_todos->addQuickTodo(text, QString(), due);
                    if (!id.isEmpty() && remind.isValid()) {
                        m_todos->setReminder(id, remind);
                    }
                    refreshTodosPanel();
                    refreshTodosBadge();
                });
        connect(m_todosPane, &NoterTodosPanel::setDueRequested,
                this, [this](const QString &id, const QDateTime &due) {
                    if (m_todos) m_todos->setDue(id, due);
                    refreshTodosPanel();
                    refreshTodosBadge();
                });
        connect(m_todosPane, &NoterTodosPanel::setReminderRequested,
                this, [this](const QString &id, const QDateTime &remind) {
                    if (m_todos) m_todos->setReminder(id, remind);
                    refreshTodosPanel();
                });
        connect(m_todosPane, &NoterTodosPanel::markDoneRequested,
                this, [this](const QString &id) {
                    if (!m_todos) return;
                    const TodoRow t = m_todos->find(id);
                    if (t.status == QStringLiteral("done")) m_todos->markOpen(id);
                    else m_todos->markDone(id, QDateTime::currentDateTimeUtc());
                    refreshTodosPanel();
                    refreshTodosBadge();
                });
        connect(m_todosPane, &NoterTodosPanel::trashRequested,
                this, [this](const QString &id) {
                    if (m_todos) m_todos->trashRow(id);
                    refreshTodosPanel();
                    refreshTodosBadge();
                });
        connect(m_todosPane, &NoterTodosPanel::restoreRequested,
                this, [this](const QString &id) {
                    if (m_todos) m_todos->restoreRow(id);
                    refreshTodosPanel();
                    refreshTodosBadge();
                });
        connect(m_todosPane, &NoterTodosPanel::deleteRequested,
                this, [this](const QString &id) {
                    if (m_todos) m_todos->deleteRow(id);  // permanent
                    refreshTodosPanel();
                    refreshTodosBadge();
                });
    }
    if (m_todosPane->isVisible()) {
        m_todosPane->hide();
    } else {
        refreshTodosPanel();
        m_todosPane->show();
    }
}

void NotesPanel::refreshTodosPanel() {
    if (!m_todosPane || !m_todos) return;
    const QDateTime now = QDateTime::currentDateTime();

    auto toRow = [](const TodoRow &t) {
        NoterTodoRow r;
        r.todoId = t.id;
        r.text = t.text;
        r.owner = t.owner;
        r.meeting = t.meetingTitle;
        r.sourceFile = t.sourceFile;
        r.blockId = t.sourceBlockId;
        r.due = t.dueAt;
        r.done = (t.status == QStringLiteral("done"));
        return r;
    };

    auto convert = [&](const QVector<TodoRow> &in) {
        QList<NoterTodoRow> out;
        out.reserve(in.size());
        for (const auto &t : in) out << toRow(t);
        return out;
    };

    m_todosPane->setTodos(convert(m_todos->dueGroupOverdue(now)),
                          convert(m_todos->dueGroupToday(now)),
                          convert(m_todos->dueGroupWeek(now)),
                          convert(m_todos->dueGroupSomeday(now)),
                          convert(m_todos->dueGroupDone(20)),
                          convert(m_todos->dueGroupTrashed(100)));
}

void NotesPanel::quickSwitchMeeting() {
    if (m_search) {
        m_search->setFocus();
        m_search->selectAll();
    }
}
