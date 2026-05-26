// SPDX-License-Identifier: GPL-3.0-or-later

// ═══════════════════════════════════════════════════════════════════════
// DiagramEditor — the .npd authoring surface (one Notepatra tab).
//
// Layout: a top toolbar + a horizontal QSplitter with the .npd source on the
// left (plain-text, native undo/redo) and a live DiagramView preview on the
// right. Editing the text re-renders the preview after a short debounce, so
// the text IS the diagram (the .npd source of truth — see project memory
// "diagramming-tool-direction"). Parse errors surface in a status strip
// without blocking the preview (the parser is lenient and renders what it can).
//
// Self-contained create→edit→save→export loop:
//   • New ▾   — insert a Flow / ER / System starter template
//   • Open    — load a .npd file
//   • Save    — write the .npd source
//   • AI ▾    — generate / edit a diagram from a natural-language prompt with
//               a review-before-insert step (added in the AI slice)
//   • Export ▾— PNG / WebP / JPEG / SVG / PDF / HTML via DiagramView
//   • Fit     — re-fit the diagram to the viewport
//
// The widget never needs WebEngine headers itself — it talks only to
// DiagramView's flavor-agnostic public API, so it compiles in lite + full.
// ═══════════════════════════════════════════════════════════════════════

#ifndef NOTEPATRA_DIAGRAM_EDITOR_H
#define NOTEPATRA_DIAGRAM_EDITOR_H

#include <QString>
#include <QWidget>

QT_BEGIN_NAMESPACE
class QPlainTextEdit;
class QLabel;
class QTimer;
class QToolButton;
QT_END_NAMESPACE

class DiagramView;

class DiagramEditor : public QWidget {
    Q_OBJECT
public:
    explicit DiagramEditor(QWidget *parent = nullptr);

    // Replace the editor's .npd text (used by Open / template / AI insert).
    void setNpdText(const QString &text);
    QString npdText() const;

    // Current on-disk path ("" if never saved). Used for the tab title.
    QString filePath() const { return m_path; }

signals:
    // Emitted when the document path or modified state changes, so the host
    // can retitle the tab ("Diagram", "flow.npd", "flow.npd •").
    void titleChanged(const QString &title);

public slots:
    void onThemeChanged();   // restyle toolbar/editor on app light↔dark flip

private slots:
    void scheduleRender();   // debounce text edits
    void renderNow();        // parse + push to the preview + update status
    void newFromTemplate();  // New ▾ menu
    void openFile();
    bool saveFile();         // returns false on cancel/failure
    void exportAs();         // Export ▾ menu
    void generateWithAi();   // AI ▾ — NL prompt → review → insert
    void importMermaid();    // paste Mermaid → translate → load
    void showSyntaxHelp();   // cheat-sheet: how to create + .npd grammar

private:
    void emitTitle();
    QToolButton *makeMenuButton(const QString &text, const QString &tip);

    QPlainTextEdit *m_edit = nullptr;
    DiagramView    *m_preview = nullptr;
    QLabel         *m_status = nullptr;   // parse-state strip
    QTimer         *m_debounce = nullptr;
    QString         m_path;
    QString         m_pendingExportFmt;   // set by the Export ▾ menu before exportAs()
    bool            m_dirty = false;
};

#endif // NOTEPATRA_DIAGRAM_EDITOR_H
