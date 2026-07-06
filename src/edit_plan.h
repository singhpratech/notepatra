// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef EDIT_PLAN_H
#define EDIT_PLAN_H

// Slice B+C of the v0.2 Coding-mode revamp.
//
// EditPlanList renders the model's proposed multi-file edit set as a vertical
// stack of rows that the user can review / cherry-pick / reject. It is the
// payload widget that lives inside Slice A's Composer tab. Each row is a
// self-contained mini-card: header (path + +x/-y stats + buttons) plus an
// optional inline DiffView expander.
//
// Slice A is responsible for hosting this widget — typically by adding the
// EditPlanList instance to a vertical layout inside m_composerArea. This file
// has zero coupling to AiPanel; it speaks only QString paths and emits Qt
// signals so any host (today the Composer; tomorrow possibly a Plan dialog)
// can drive it.

#include <QList>
#include <QPair>
#include <QString>
#include <QVector>
#include <QWidget>

class QCheckBox;
class QLabel;
class QPushButton;
class QVBoxLayout;
class EditPlanRow;

class EditPlanList : public QWidget {
    Q_OBJECT
public:
    explicit EditPlanList(QWidget *parent = nullptr);

    // Append a new file edit. The row is inserted at the bottom of the list
    // and is rendered immediately. We store both the absolute path (for
    // round-tripping back to the host on Apply) and a workspace-relative
    // version for the on-screen label; relativisation happens here so the
    // host doesn't need to know what "workspace" means.
    void addEdit(const QString &absPath, const QString &before, const QString &after);

    // Drop every row. Used by the host on tab-clear, on conversation reset,
    // or after a successful Apply All.
    void clear();

    // Mark the rows whose absolute path is in `absPaths` as APPLIED — the
    // host calls this after AIPanel::applyComposerEdits has written those
    // files to disk. An applied row shows a green ✓, is unchecked + disabled,
    // and is excluded from any later Apply All / Apply Selected so the user
    // can never silently double-write the same edit. (v0.1.110 — fixes the
    // verified "Apply did nothing visible / re-apply re-writes" trust bug.)
    void markApplied(const QList<QString> &absPaths);

    // Number of pending edits (rows). Mostly useful for tests.
    int count() const;

    // Number of rows that have been marked applied (written to disk). Used by
    // tests and lets a host show "2 of 3 applied". (v0.1.110)
    int appliedCount() const;

    // v0.1.111 — Composer rollback. After the host reverts applied files back
    // to their pre-edit content, it calls markPending() to flip those rows
    // from applied back to PENDING (badge cleared, checkbox re-enabled,
    // re-appliable). Symmetric inverse of markApplied().
    void markPending(const QList<QString> &absPaths);

    // Show / hide the "Undo apply" action-bar button. The host shows it after
    // a successful Apply (when a revertible batch exists) and hides it after
    // the undo lands, a new apply supersedes it, or the plan is cleared.
    void showUndoButton(bool show);

    // Configure the workspace root used to render relative paths. Optional —
    // when unset we fall back to the absolute path. Slice A will wire this
    // from the user's open project directory.
    void setWorkspaceRoot(const QString &absRoot);

protected:
    // v0.1.115 (item 2c) — keyboard operability. Space toggles the inclusion of
    // the focused row; Enter/Return confirms (Apply Selected). Tabbing moves
    // between rows' checkboxes natively.
    void keyPressEvent(QKeyEvent *e) override;

signals:
    // Emitted when the user clicks Apply All / Apply Selected. The payload
    // is a list of (absPath, afterText) pairs; the host writes those files
    // and is responsible for marking them dirty / saving / reloading the
    // editor view.
    void applyRequested(const QList<QPair<QString, QString>> &edits);

    // Emitted when a row is removed (either via the per-row [x] button or
    // Reject All). The host can use this to drop matching tool-call records
    // from the conversation transcript.
    void editRemoved(const QString &absPath);

    // v0.1.111 — emitted when the user clicks "Undo apply". The host
    // (AIPanel::undoLastApply) reverts the last applied batch back to its
    // pre-edit content (with drift protection) and calls markPending().
    void undoApplyRequested();

private slots:
    void onApplyAll();
    void onApplySelected();
    void onRejectAll();
    void onRowRemoveRequested(EditPlanRow *row);

private:
    void buildActionBar();
    void updateActionBarEnabled();  // grey out Apply when nothing is pending

    QVBoxLayout *m_listLayout = nullptr;  // holds EditPlanRow widgets
    QPushButton *m_applyAllBtn = nullptr;
    QPushButton *m_applySelectedBtn = nullptr;
    QPushButton *m_rejectAllBtn = nullptr;
    QPushButton *m_undoBtn = nullptr;     // v0.1.111 — "Undo apply" (hidden until an apply lands)

    QString m_workspaceRoot;
    QVector<EditPlanRow *> m_rows;  // non-owning pointers; QObject parent owns
};

// EditPlanRow is the per-file item inside EditPlanList. It is declared in the
// public header (rather than hidden in the .cpp) because EditPlanList holds
// QVector<EditPlanRow *> and we need a complete type for that. It is not part
// of the public API surface — hosts should only interact with EditPlanList.
class EditPlanRow : public QWidget {
    Q_OBJECT
public:
    EditPlanRow(const QString &absPath, const QString &displayPath,
                const QString &before, const QString &after,
                QWidget *parent = nullptr);

    QString absPath() const { return m_absPath; }
    QString afterText() const { return m_after; }
    QString beforeText() const { return m_before; }  // v0.1.111 — for rollback
    bool isSelected() const;

    // v0.1.110 — applied-state tracking so the row reflects reality after a
    // successful write and can't be re-applied.
    void setApplied();
    bool isApplied() const { return m_applied; }
    // v0.1.111 — inverse of setApplied(): flip an applied row back to pending
    // after the host reverts the file on disk (badge cleared, controls
    // re-enabled, re-appliable).
    void setPending();
    int added() const { return m_added; }
    int removed() const { return m_removed; }

    // v0.1.115 (item 2c) — flip the include checkbox (no-op on an applied /
    // disabled row). Used by EditPlanList's Space-key handler.
    void toggleSelected();

signals:
    void removeRequested();

private slots:
    void onToggleDiff();

private:
    void computeStats();

    QString m_absPath;
    QString m_displayPath;
    QString m_before;
    QString m_after;

    int m_added = 0;
    int m_removed = 0;

    bool m_applied = false;
    QCheckBox *m_selectBox = nullptr;
    QLabel *m_appliedTag = nullptr;      // hidden "✓ applied" badge, shown by setApplied()
    QPushButton *m_diffBtn = nullptr;
    QPushButton *m_removeBtn = nullptr;
    QWidget *m_diffContainer = nullptr;  // lazily filled with a DiffView
};

#endif // EDIT_PLAN_H
