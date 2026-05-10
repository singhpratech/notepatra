#ifndef NOTEPATRA_MERGE_HELPER_WIDGET_H
#define NOTEPATRA_MERGE_HELPER_WIDGET_H

// ═══════════════════════════════════════════════════════════════════════
// MergeHelperWidget — v0.1.62 MVP companion UI for git merge conflicts.
//
// Attaches non-intrusively above an existing Editor (QsciScintilla
// subclass) and offers a per-conflict action row for every region the
// scanner finds:
//
//   Conflict 1/3   <main>  ↔  <feature/foo>
//     [Take ours] [Take theirs] [Take both]   [Jump to →]
//
// Action buttons rewrite the editor's buffer in place by:
//   1. Reading editor->text()
//   2. Calling MergeHelper::applyResolution() with the chosen replacement
//   3. setText()-ing the result and refreshing the conflict list
//
// Save is intentionally NOT performed here — the user keeps control of
// when to commit / save. Once every region is resolved, the widget hides
// itself so the editor pane returns to normal.
//
// QScintilla annotations are also used: an annotation above each region
// (rendered via SCI_ANNOTATIONSETTEXT with ANNOTATION_BOXED) labels the
// region with "▼ Conflict #N — ours / theirs" so the user can locate
// each block while scrolling.
// ═══════════════════════════════════════════════════════════════════════

#include "merge_helper.h"

#include <QWidget>
#include <QVector>

class QLabel;
class QPushButton;
class QVBoxLayout;
class QsciScintilla;

class MergeHelperWidget : public QWidget {
    Q_OBJECT
public:
    explicit MergeHelperWidget(QWidget *parent = nullptr);

    // Bind to an editor and a file path. The path is informational
    // (shown in the header); the actual buffer comes from `editor`.
    // Calling attach() with a different editor re-binds.
    void attach(QsciScintilla *editor, const QString &filePath);

    // Re-scan the editor's current text and rebuild the per-region rows.
    // Called automatically after each resolution but exposed so the
    // host can refresh if the buffer changes externally.
    void rescan();

signals:
    // Emitted when the user resolves the last conflict in the buffer.
    // Host can use this to flip a status badge / show a "ready to add"
    // hint, but is NOT obliged to save automatically.
    void allConflictsResolved();

private:
    void buildRowsUi();
    void clearRowsUi();
    void applyAnnotations();
    void clearAnnotations();
    void resolveRegion(int regionIndex, const QString &replacement);
    void jumpToRegion(int regionIndex);

    QsciScintilla *m_editor = nullptr;
    QString m_filePath;

    QLabel *m_header = nullptr;
    QWidget *m_rowsHost = nullptr;
    QVBoxLayout *m_rowsLayout = nullptr;
    QVector<MergeHelper::ConflictRegion> m_regions;
};

#endif // NOTEPATRA_MERGE_HELPER_WIDGET_H
