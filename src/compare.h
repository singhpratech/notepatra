#ifndef COMPARE_H
#define COMPARE_H

#include <QWidget>
#include <QLabel>
#include <QPushButton>
#include <QCheckBox>
#include <QVector>
#include <Qsci/qsciscintilla.h>

class QEvent;
class QMouseEvent;
class QPaintEvent;
class QSplitter;

class CompareNavBar : public QWidget {
    Q_OBJECT
public:
    explicit CompareNavBar(QWidget *parent = nullptr);

    void setRows(const QVector<int> &rowKinds);
    void setViewport(int firstVisibleRow, int visibleRows);

    int totalRows() const;
    int diffMarkerCount() const;
    int firstVisibleRow() const;
    int visibleRows() const;

signals:
    void rowActivated(int row);

protected:
    void paintEvent(QPaintEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;

private:
    void activateRowAt(int y);

    QVector<int> m_rowKinds;
    int m_firstVisibleRow = 0;
    int m_visibleRows = 0;
};

/**
 * Compare-inspired diff — two real Scintilla editors side by side.
 * Added lines = green background, deleted = red, changed = light gold with bright
 * per-character highlights. Synced scrolling. Locked by default, with optional
 * unlock-for-editing mode.
 *
 * INSPIRED BY: ComparePlus by Pavel Nedev (https://github.com/pnedev/comparePlus)
 *
 * Pavel's ComparePlus is the gold-standard diff plugin for Notepad++. Notepatra's
 * compare panel borrows the visual conventions (colored line markers, side-by-side
 * Scintilla editors with synced scrolling, prev/next navigation) but is written
 * from scratch in Qt + Rust because Notepatra is a different codebase. All credit
 * for the UX patterns goes to Pavel and the ComparePlus contributors.
 */
class CompareWidget : public QWidget {
    Q_OBJECT
public:
    explicit CompareWidget(QWidget *parent = nullptr);
    void compare(const QString &leftText, const QString &leftName,
                 const QString &rightText, const QString &rightName);

    int diffCount() const;
    int rowCount() const;

    // v0.1.62 — when this compare view represents "HEAD vs working copy"
    // for a tracked file, the host sets the workspace-relative path so
    // the per-hunk Stage / Revert button row can route operations to the
    // right file. When empty (the default), the hunk strip is hidden.
    void setGitContext(const QString &repoRoot, const QString &filePath);

public slots:
    // Re-apply the header / stats / splitter stylesheets and re-run
    // setupEditor() on both Scintilla panes so markers, paper, and
    // foreground colours flip to the new theme. If a compare has
    // already been rendered we recompare() so per-line marker
    // backgrounds repaint against the new palette.
    void onThemeChanged();

signals:
    // Emitted when the user clicks the "Close" button in the compare
    // toolbar. The host (CompareDialog or the tab-based opener in
    // MainWindow) is responsible for actually closing the window/tab.
    void closeRequested();

    // v0.1.62 — emitted by the per-hunk Stage / Revert buttons. The
    // GitHunkApply API lands separately (sibling agent); MainWindow
    // will route these signals to that API once it's wired. Until
    // then, the buttons surface a TODO message so the user knows the
    // wiring is pending.
    void stageHunkRequested(const QString &filePath, int hunkIndex);
    void revertHunkRequested(const QString &filePath, int hunkIndex);

private:
    bool eventFilter(QObject *watched, QEvent *event) override;
    void navigateNext();
    void navigatePrev();
    void recompare();
    void setupEditor(QsciScintilla *ed);
    void setEditorsEditable(bool editable);
    void syncTextsFromEditors();
    void updateEditToggle();
    void jumpToRow(int row);
    void updateOverviewViewport();

    // v0.1.62 — rebuild the per-hunk action strip from m_rowKinds. Each
    // contiguous run of non-RowEqual rows becomes one hunk with a row
    // containing { "Hunk N (lines a–b)", [Stage], [Revert], [Jump →] }.
    void rebuildHunkStrip();
    void clearHunkStrip();

    QsciScintilla *m_leftEditor, *m_rightEditor;
    CompareNavBar *m_navBar;
    QPushButton *m_editToggle = nullptr;
    QSplitter *m_splitter = nullptr;
    QLabel *m_leftHeader, *m_rightHeader, *m_statsLabel;
    QCheckBox *m_ignoreWhitespace, *m_ignoreCase, *m_ignoreEmptyLines;
    // v0.1.41 — toolbar checkbox that hides matching lines so only
    // Added / Deleted / Changed rows render. Default OFF (full-files view).
    QCheckBox *m_diffOnly = nullptr;

    QString m_leftText, m_rightText;
    QVector<int> m_rowKinds;
    QVector<int> m_diffLines;
    int m_currentDiff = -1;
    bool m_editable = false;
    bool m_firstCompareDone = false;

    // v0.1.62 — git-context for the per-hunk action strip. Both empty
    // when the compare view is not anchored to a git working copy.
    QString m_gitRepoRoot;
    QString m_gitFilePath;
    class QWidget *m_hunkStrip = nullptr;
    class QVBoxLayout *m_hunkStripLayout = nullptr;
    class QScrollArea *m_hunkStripScroll = nullptr;
};

// Backward compat
class CompareDialog : public QWidget {
public:
    CompareDialog(const QString &l, const QString &ln, const QString &r, const QString &rn, QWidget *p = nullptr);
};

#endif
