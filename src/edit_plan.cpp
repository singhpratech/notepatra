// SPDX-License-Identifier: GPL-3.0-or-later

#include "edit_plan.h"

#include "diff_view.h"
#include "rustbridge.h"

#include <QCheckBox>
#include <QDir>
#include <QFontMetrics>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QPushButton>
#include <QSet>
#include <QSpacerItem>
#include <QStyle>
#include <QVBoxLayout>

// Slice B+C — EditPlanList implementation.
//
// Layout sketch:
//   ┌───────── EditPlanList (QWidget) ─────────┐
//   │ [Apply All] [Apply Selected] [Reject All]│  ← action bar
//   ├──────────────────────────────────────────┤
//   │ □ 📄 …rel/path.cpp  +12 -3   [Diff] [x]  │  ← EditPlanRow #1
//   │   ┌─ DiffView (lazy) ────────────────┐    │
//   │   │ <left pane>     │ <right pane>   │    │
//   │   └──────────────────────────────────┘    │
//   │ □ 📄 …other.h        +0  -2   [Diff] [x] │  ← EditPlanRow #2
//   └──────────────────────────────────────────┘
//
// We keep the row widget public (declared in edit_plan.h) so EditPlanList can
// hold a typed QVector<EditPlanRow *>. The host (Slice A) only ever sees the
// list — rows are constructed by addEdit() and disposed by clear() /
// onRowRemoveRequested().

namespace {

// Truncate a path with an ellipsis in the middle if it doesn't fit in the
// label's column width. We aim for the GitHub-style ".../foo/bar/baz.cpp"
// look — keep at least the last two segments, replace deeper ancestors with
// an ellipsis. We use a rough character budget rather than QFontMetrics so
// the calculation works pre-show (the row's QLabel may not be realised yet
// when addEdit() is called).
static QString truncateMiddle(const QString &path, int maxChars = 60) {
    if (path.size() <= maxChars) return path;
    // Walk backwards keeping segments until we exceed budget.
    QStringList segs = path.split('/');
    if (segs.size() <= 2) return path;  // can't truncate sensibly
    QStringList kept;
    int used = 0;
    for (int i = segs.size() - 1; i >= 0; --i) {
        // +1 for the slash we'll re-insert
        int chunk = segs[i].size() + 1;
        if (used + chunk + 4 /* "…/" */ > maxChars && kept.size() >= 2) break;
        kept.prepend(segs[i]);
        used += chunk;
    }
    return QStringLiteral("…/") + kept.join('/');
}

// Coloured "+12 -3" stats string. We assemble it as plain rich text rather
// than two QLabels so it stays inline with the path label even when the row
// is squeezed. Span colours match the GitHub diff palette.
static QString statsHtml(int added, int removed) {
    return QStringLiteral(
               "<span style='color:#22863A;'>+%1</span> "
               "<span style='color:#CB2431;'>-%2</span>")
        .arg(added)
        .arg(removed);
}

}  // namespace

// ──────────────────────────────────────────────────────────────────────────
// EditPlanRow
// ──────────────────────────────────────────────────────────────────────────

EditPlanRow::EditPlanRow(const QString &absPath, const QString &displayPath,
                         const QString &before, const QString &after,
                         QWidget *parent)
    : QWidget(parent),
      m_absPath(absPath),
      m_displayPath(displayPath),
      m_before(before),
      m_after(after) {
    computeStats();

    auto *outer = new QVBoxLayout(this);
    outer->setContentsMargins(4, 4, 4, 4);
    outer->setSpacing(2);

    // Header line: checkbox, file icon, path, stats, [Diff], [x].
    auto *header = new QHBoxLayout;
    header->setSpacing(6);

    m_selectBox = new QCheckBox;
    m_selectBox->setChecked(true);  // default ON — Apply Selected behaves like Apply All until the user opts out
    m_selectBox->setToolTip(tr("Include in Apply Selected"));
    header->addWidget(m_selectBox);

    // File icon via QStyle (never an emoji codepoint — U+1F4C4 tofus on
    // Linux without a colour-emoji font; the project's standing icon rule).
    auto *icon = new QLabel;
    icon->setPixmap(style()->standardIcon(QStyle::SP_FileIcon).pixmap(14, 14));
    header->addWidget(icon);

    auto *pathLabel = new QLabel(truncateMiddle(m_displayPath));
    pathLabel->setToolTip(m_absPath);
    pathLabel->setStyleSheet("font-weight: 500;");
    header->addWidget(pathLabel, 1);  // stretch to push buttons right

    auto *stats = new QLabel(statsHtml(m_added, m_removed));
    stats->setTextFormat(Qt::RichText);
    header->addWidget(stats);

    // Hidden until setApplied() — a green ✓ badge that makes "this edit was
    // written to disk" unmissable, so the user never wonders whether Apply
    // did anything (the v0.1.110 trust fix).
    m_appliedTag = new QLabel(QStringLiteral("✓ applied"));
    m_appliedTag->setStyleSheet("color:#16a34a; font-weight:600;");
    m_appliedTag->setVisible(false);
    header->addWidget(m_appliedTag);

    m_diffBtn = new QPushButton(tr("Diff"));
    m_diffBtn->setCheckable(true);
    m_diffBtn->setToolTip(tr("Show before/after preview"));
    connect(m_diffBtn, &QPushButton::clicked, this, &EditPlanRow::onToggleDiff);
    header->addWidget(m_diffBtn);

    m_removeBtn = new QPushButton(QStringLiteral("✗"));
    m_removeBtn->setFixedWidth(28);
    m_removeBtn->setToolTip(tr("Remove this edit from the plan"));
    connect(m_removeBtn, &QPushButton::clicked, this, &EditPlanRow::removeRequested);
    header->addWidget(m_removeBtn);

    outer->addLayout(header);

    // Lazy DiffView container — empty until the user clicks [Diff]. We pre-
    // create the container so we can show/hide without re-laying out the
    // parent on every toggle, but we don't construct the (relatively heavy)
    // DiffView itself until first reveal.
    m_diffContainer = new QWidget;
    auto *diffLayout = new QVBoxLayout(m_diffContainer);
    diffLayout->setContentsMargins(28, 0, 4, 4);  // indent under the path label
    m_diffContainer->setVisible(false);
    outer->addWidget(m_diffContainer);

    // Subtle row separator so consecutive rows look like a list rather than
    // a wall of text.
    setStyleSheet("EditPlanRow { border-bottom: 1px solid palette(midlight); }");
}

bool EditPlanRow::isSelected() const {
    // An already-applied row is never re-selected — guards against a second
    // Apply silently re-writing files that already landed.
    return !m_applied && m_selectBox && m_selectBox->isChecked();
}

void EditPlanRow::setApplied() {
    if (m_applied) return;
    m_applied = true;
    if (m_selectBox) {
        m_selectBox->setChecked(false);
        m_selectBox->setEnabled(false);
    }
    if (m_removeBtn) m_removeBtn->setEnabled(false);
    if (m_appliedTag) m_appliedTag->setVisible(true);
    // Dim the row so applied edits visually recede behind still-pending ones.
    // m_appliedTag keeps its own green colour (a widget's own stylesheet wins
    // over an ancestor's cascade in Qt), so the ✓ stays vivid.
    setStyleSheet("EditPlanRow { border-bottom: 1px solid palette(midlight); }"
                  "EditPlanRow QLabel { color: palette(mid); }");
}

void EditPlanRow::setPending() {
    // v0.1.111 — inverse of setApplied(). The host calls this after reverting
    // the file on disk so the row stops claiming "applied" and becomes
    // re-appliable. Restores every control setApplied() disabled.
    if (!m_applied) return;
    m_applied = false;
    if (m_selectBox) {
        m_selectBox->setEnabled(true);
        m_selectBox->setChecked(true);  // re-arm for Apply Selected, like a fresh row
    }
    if (m_removeBtn) m_removeBtn->setEnabled(true);
    if (m_appliedTag) m_appliedTag->setVisible(false);
    // Restore the un-dimmed stylesheet (drop the muted-QLabel cascade).
    setStyleSheet("EditPlanRow { border-bottom: 1px solid palette(midlight); }");
}

void EditPlanRow::toggleSelected() {
    // v0.1.115 (item 2c) — no-op on an applied row (checkbox disabled): an
    // already-written edit must never be re-armed by a stray Space.
    if (m_applied || !m_selectBox || !m_selectBox->isEnabled()) return;
    m_selectBox->setChecked(!m_selectBox->isChecked());
}

void EditPlanRow::onToggleDiff() {
    const bool show = m_diffBtn->isChecked();
    if (show) {
        // Construct the DiffView lazily. Once built we keep it around so
        // subsequent toggles are cheap and the user's scroll position
        // inside the diff is preserved.
        if (m_diffContainer->layout()->count() == 0) {
            auto *view = new DiffView(m_before, m_after, m_diffContainer);
            view->setMinimumHeight(180);
            m_diffContainer->layout()->addWidget(view);
        }
    }
    m_diffContainer->setVisible(show);
}

void EditPlanRow::computeStats() {
    // We could just count line-count deltas, but that hides edits inside a
    // file with the same line count. Use the same Rust diff routine the
    // DiffView uses so the stats and the visual preview stay consistent.
    const RustCore::DiffInfo diff = RustCore::computeDiff(m_before, m_after);
    m_added = diff.added;
    m_removed = diff.removed;
    // Rust's `changed` field counts lines that exist on both sides but
    // differ; for a per-file +/- summary we fold those into both buckets so
    // a 1-line modification reads as "+1 -1" rather than "+0 -0".
    m_added += diff.changed;
    m_removed += diff.changed;
}

// ──────────────────────────────────────────────────────────────────────────
// EditPlanList
// ──────────────────────────────────────────────────────────────────────────

EditPlanList::EditPlanList(QWidget *parent) : QWidget(parent) {
    auto *outer = new QVBoxLayout(this);
    outer->setContentsMargins(4, 4, 4, 4);
    outer->setSpacing(4);

    buildActionBar();
    outer->addWidget(m_applyAllBtn->parentWidget());

    // Row container. We use a QVBoxLayout directly on the host widget rather
    // than wrapping in a QScrollArea — Slice A's Composer tab is itself
    // expected to be scrollable, so adding a second scroll area here would
    // double-nest and feel janky on small screens.
    auto *rowsHost = new QWidget;
    m_listLayout = new QVBoxLayout(rowsHost);
    m_listLayout->setContentsMargins(0, 0, 0, 0);
    m_listLayout->setSpacing(0);
    m_listLayout->addStretch(1);  // keeps rows top-aligned
    outer->addWidget(rowsHost, 1);
}

void EditPlanList::buildActionBar() {
    auto *bar = new QWidget(this);
    auto *layout = new QHBoxLayout(bar);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(6);

    m_applyAllBtn = new QPushButton(tr("Apply All"));
    m_applyAllBtn->setToolTip(tr("Write every file in the plan to disk"));
    connect(m_applyAllBtn, &QPushButton::clicked, this, &EditPlanList::onApplyAll);
    layout->addWidget(m_applyAllBtn);

    m_applySelectedBtn = new QPushButton(tr("Apply Selected"));
    m_applySelectedBtn->setToolTip(tr("Write only checked files"));
    connect(m_applySelectedBtn, &QPushButton::clicked, this, &EditPlanList::onApplySelected);
    layout->addWidget(m_applySelectedBtn);

    m_rejectAllBtn = new QPushButton(tr("Reject All"));
    m_rejectAllBtn->setToolTip(tr("Discard the plan without writing any files"));
    connect(m_rejectAllBtn, &QPushButton::clicked, this, &EditPlanList::onRejectAll);
    layout->addWidget(m_rejectAllBtn);

    // v0.1.111 — "Undo apply". Hidden until an Apply lands a revertible batch.
    // U+21B6 (↶) is a plain dingbat, not a colour-emoji (no Linux tofu).
    m_undoBtn = new QPushButton(QStringLiteral("↶ Undo apply"));
    m_undoBtn->setToolTip(tr("Restore the files from the last Apply to their previous content"));
    connect(m_undoBtn, &QPushButton::clicked, this, &EditPlanList::undoApplyRequested);
    m_undoBtn->setVisible(false);
    layout->addWidget(m_undoBtn);

    layout->addStretch(1);
}

void EditPlanList::showUndoButton(bool show) {
    if (m_undoBtn) m_undoBtn->setVisible(show);
}

void EditPlanList::markPending(const QList<QString> &absPaths) {
    if (absPaths.isEmpty()) return;
    const QSet<QString> reverted(absPaths.begin(), absPaths.end());
    for (EditPlanRow *row : m_rows) {
        if (reverted.contains(row->absPath())) row->setPending();
    }
    updateActionBarEnabled();
}

void EditPlanList::addEdit(const QString &absPath, const QString &before,
                           const QString &after) {
    // Compute display path: strip workspace root if it's a prefix, otherwise
    // fall back to the absolute path. We use QDir to handle trailing
    // separators uniformly across platforms.
    QString display = absPath;
    if (!m_workspaceRoot.isEmpty()) {
        QDir root(m_workspaceRoot);
        const QString rel = root.relativeFilePath(absPath);
        if (!rel.startsWith("..")) display = rel;
    }

    auto *row = new EditPlanRow(absPath, display, before, after, this);
    connect(row, &EditPlanRow::removeRequested, this, [this, row]() {
        onRowRemoveRequested(row);
    });

    // Insert before the trailing stretch so the stretch stays at the bottom.
    const int insertAt = m_listLayout->count() - 1;
    m_listLayout->insertWidget(insertAt, row);
    m_rows.append(row);
    // A fresh proposal supersedes any previous applied batch — its Undo is
    // stale, so hide it (the host also drops m_lastApplyBatch on a new apply).
    showUndoButton(false);
    updateActionBarEnabled();
}

void EditPlanList::clear() {
    for (EditPlanRow *row : m_rows) {
        m_listLayout->removeWidget(row);
        row->deleteLater();
    }
    m_rows.clear();
    showUndoButton(false);  // no batch left to undo
    updateActionBarEnabled();
}

int EditPlanList::count() const {
    return m_rows.size();
}

int EditPlanList::appliedCount() const {
    int n = 0;
    for (EditPlanRow *row : m_rows)
        if (row->isApplied()) ++n;
    return n;
}

void EditPlanList::updateActionBarEnabled() {
    // Apply All / Apply Selected only make sense while there's a pending
    // (not-yet-applied) row — otherwise a click is a silent no-op. Reject All
    // stays available as long as any rows are listed.
    int pending = 0;
    for (EditPlanRow *row : m_rows)
        if (!row->isApplied()) ++pending;
    if (m_applyAllBtn)      m_applyAllBtn->setEnabled(pending > 0);
    if (m_applySelectedBtn) m_applySelectedBtn->setEnabled(pending > 0);
    if (m_rejectAllBtn)     m_rejectAllBtn->setEnabled(!m_rows.isEmpty());
}

void EditPlanList::setWorkspaceRoot(const QString &absRoot) {
    m_workspaceRoot = absRoot;
}

void EditPlanList::onApplyAll() {
    QList<QPair<QString, QString>> edits;
    edits.reserve(m_rows.size());
    for (EditPlanRow *row : m_rows) {
        if (row->isApplied()) continue;  // never re-write an already-applied edit
        edits.append({row->absPath(), row->afterText()});
    }
    if (!edits.isEmpty()) emit applyRequested(edits);
}

void EditPlanList::markApplied(const QList<QString> &absPaths) {
    if (absPaths.isEmpty()) return;
    const QSet<QString> applied(absPaths.begin(), absPaths.end());
    for (EditPlanRow *row : m_rows) {
        if (applied.contains(row->absPath())) row->setApplied();
    }
    updateActionBarEnabled();
}

void EditPlanList::onApplySelected() {
    QList<QPair<QString, QString>> edits;
    for (EditPlanRow *row : m_rows) {
        if (row->isSelected()) {
            edits.append({row->absPath(), row->afterText()});
        }
    }
    if (!edits.isEmpty()) emit applyRequested(edits);
}

void EditPlanList::keyPressEvent(QKeyEvent *e) {
    const int key = e->key();
    if (key == Qt::Key_Return || key == Qt::Key_Enter) {
        // v0.1.115 (item 2c) — Enter confirms: apply the checked rows. QCheckBox
        // ignores Enter, so this reliably propagates up from a focused row.
        bool anyPending = false;
        for (EditPlanRow *r : m_rows)
            if (!r->isApplied()) { anyPending = true; break; }
        if (anyPending) {
            onApplySelected();
            e->accept();
            return;
        }
    } else if (key == Qt::Key_Space) {
        // Toggle inclusion of the row that (transitively) holds focus. When the
        // checkbox itself is focused it consumes Space first (same outcome);
        // this catches focus on the row / a sibling control.
        QWidget *f = focusWidget();
        while (f && f != this) {
            if (auto *row = qobject_cast<EditPlanRow *>(f)) {
                row->toggleSelected();
                e->accept();
                return;
            }
            f = f->parentWidget();
        }
    }
    QWidget::keyPressEvent(e);
}

void EditPlanList::onRejectAll() {
    // Snapshot paths before clearing so we can emit editRemoved per-file
    // (the host may want to drop transcript records, etc.).
    QStringList paths;
    paths.reserve(m_rows.size());
    for (EditPlanRow *row : m_rows) paths.append(row->absPath());
    clear();
    for (const QString &p : paths) emit editRemoved(p);
}

void EditPlanList::onRowRemoveRequested(EditPlanRow *row) {
    if (!row) return;
    const QString path = row->absPath();
    m_rows.removeAll(row);
    m_listLayout->removeWidget(row);
    row->deleteLater();
    emit editRemoved(path);
}
