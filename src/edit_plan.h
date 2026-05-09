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

    // Number of pending edits (rows). Mostly useful for tests.
    int count() const;

    // Configure the workspace root used to render relative paths. Optional —
    // when unset we fall back to the absolute path. Slice A will wire this
    // from the user's open project directory.
    void setWorkspaceRoot(const QString &absRoot);

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

private slots:
    void onApplyAll();
    void onApplySelected();
    void onRejectAll();
    void onRowRemoveRequested(EditPlanRow *row);

private:
    void buildActionBar();

    QVBoxLayout *m_listLayout = nullptr;  // holds EditPlanRow widgets
    QPushButton *m_applyAllBtn = nullptr;
    QPushButton *m_applySelectedBtn = nullptr;
    QPushButton *m_rejectAllBtn = nullptr;

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
    bool isSelected() const;

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

    QCheckBox *m_selectBox = nullptr;
    QPushButton *m_diffBtn = nullptr;
    QPushButton *m_removeBtn = nullptr;
    QWidget *m_diffContainer = nullptr;  // lazily filled with a DiffView
};

#endif // EDIT_PLAN_H
