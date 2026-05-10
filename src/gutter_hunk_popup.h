#ifndef NOTEPATRA_GUTTER_HUNK_POPUP_H
#define NOTEPATRA_GUTTER_HUNK_POPUP_H

// ═══════════════════════════════════════════════════════════════════════
// v0.1.62 — VS Code-parity per-hunk popup.
//
// A frameless QFrame that pops up next to a clicked git-gutter marker.
// Layout (top → bottom):
//
//   ┌──────────────────────────────────────────────────────┐
//   │  Hunk at line 42                                     │
//   │  [Stage Hunk]  [Revert Hunk]  [Copy Diff]            │
//   ├──────────────────────────────────────────────────────┤
//   │  <DiffView — before / after side-by-side>            │
//   └──────────────────────────────────────────────────────┘
//
// The popup owns its DiffView. Closing it (focus-out, Esc, or after a
// successful apply) deletes the widget via deleteLater().
// ═══════════════════════════════════════════════════════════════════════

#include "gitgutter.h"
#include "git_hunk_apply.h"

#include <QFrame>
#include <QString>

class QLabel;
class QPushButton;
class DiffView;

class GutterHunkPopup : public QFrame {
    Q_OBJECT
public:
    // `absFilePath` — file the hunk belongs to (absolute, on-disk path)
    // `repoRoot`    — canonical absolute path of the git repo root
    // `hunk`        — the parsed hunk (from GitGutter::hunksForFile)
    // `beforeText`  — pre-change text for the DiffView preview pane
    // `afterText`   — post-change text for the DiffView preview pane
    //
    // The popup is constructed with Qt::Popup | Qt::FramelessWindowHint
    // so it closes on click-outside automatically (Qt::Popup semantic).
    GutterHunkPopup(const QString &absFilePath,
                    const QString &repoRoot,
                    const DiffHunk &hunk,
                    const QString &beforeText,
                    const QString &afterText,
                    QWidget *parent = nullptr);

    // Show the popup with its top-left at the given GLOBAL screen point.
    // Caller is expected to compute that from
    // `editor->mapToGlobal(editor->pointFromPosition(...))`.
    void showAt(const QPoint &globalPos);

signals:
    // Fired after `git apply --cached` returns success. The caller is
    // expected to refresh the gutter on the active editor and ask the
    // GitPanel (if mounted) to re-read porcelain status. Argument is the
    // file path that was just modified — same as what was passed in.
    void hunkStaged(const QString &filePath);
    // Fired after a successful Revert. The file on disk has been
    // modified; the caller should re-read it.
    void hunkReverted(const QString &filePath);

private slots:
    void onStageClicked();
    void onRevertClicked();
    void onCopyDiffClicked();

private:
    void setErrorMessage(const QString &text);
    void clearErrorMessage();

    QString m_absFilePath;
    QString m_repoRoot;
    DiffHunk m_hunk;
    QString m_beforeText;
    QString m_afterText;

    QLabel *m_titleLabel = nullptr;
    QPushButton *m_stageBtn = nullptr;
    QPushButton *m_revertBtn = nullptr;
    QPushButton *m_copyBtn = nullptr;
    QLabel *m_errorLabel = nullptr;
    DiffView *m_diffView = nullptr;
};

#endif // NOTEPATRA_GUTTER_HUNK_POPUP_H
