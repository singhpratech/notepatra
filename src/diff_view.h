// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef DIFF_VIEW_H
#define DIFF_VIEW_H

// Slice B+C of the v0.2 Coding-mode revamp.
//
// DiffView is a side-by-side before/after preview widget. It is intentionally
// dependency-light: it takes plain QStrings, computes a line-level Myers diff
// via the existing Rust core (RustCore::computeDiff in rustbridge.h), and
// tints lines red on the "before" side and green on the "after" side.
//
// This widget is consumed inline by EditPlanList (see edit_plan.h) which
// embeds it underneath a per-file row when the user clicks [Diff]. It is
// designed to drop into Slice A's Composer tab without modification — the
// only requirement is a parent QWidget.

#include <QString>
#include <QWidget>

class QPlainTextEdit;

class DiffView : public QWidget {
    Q_OBJECT
public:
    // Construct with the textual snapshot of the file before the edit and the
    // model-proposed text after the edit. The widget computes the line-level
    // diff once at construction time; if the parent later wants to refresh
    // the contents it should destroy and recreate the widget. (Edit plans are
    // immutable per row in the current design — re-running an edit produces
    // a fresh row, so a one-shot constructor is the right fit.)
    DiffView(const QString &beforeText, const QString &afterText, QWidget *parent = nullptr);

private:
    // Apply per-line red / green background tints. We do this with QTextCursor
    // and per-block QTextBlockFormat rather than a stylesheet because Qt
    // stylesheets can't address individual lines inside a QPlainTextEdit.
    void renderTints();

    QPlainTextEdit *m_left = nullptr;
    QPlainTextEdit *m_right = nullptr;

    // Per-line tint flags computed from the diff. true = tint this line.
    QVector<bool> m_leftRemoved;   // size == leftLines.count()
    QVector<bool> m_rightAdded;    // size == rightLines.count()

    QStringList m_leftLines;
    QStringList m_rightLines;
};

#endif // DIFF_VIEW_H
