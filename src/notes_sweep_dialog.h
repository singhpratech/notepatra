// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef NOTEPATRA_NOTES_SWEEP_DIALOG_H
#define NOTEPATRA_NOTES_SWEEP_DIALOG_H

// Noter end-meeting sweep dialog — the modal the AI sweep flow opens
// after the LLM has emitted its structured JSON reply. Displays the
// four sections (decisions / actions / questions / risks), lets the
// user edit each row's text + owner + due, and lets them toggle
// per-action reminders before saving.
//
// Style notes:
//   * 780 px wide, modal, centered on parent
//   * 4 collapsible sections with small-caps coloured headers
//   * Each action row has a "remind" toggle (default ON)
//   * Theme-aware — every colour derives from the widget palette so
//     dark + Monokai mode look right with no extra wiring
//   * Reuses QStyle::SP_* glyphs for buttons per the Notepatra UI rule
//     (no emoji codepoints — see feedback_qt_icons_no_emoji.md)

#include "notes_sweep_prompt.h"

#include <QDateTime>
#include <QDialog>
#include <QPair>
#include <QString>
#include <QVector>

class QCheckBox;
class QDateTimeEdit;
class QLabel;
class QLineEdit;
class QPushButton;
class QVBoxLayout;
class QWidget;

class NoterSweepDialog : public QDialog {
    Q_OBJECT
public:
    explicit NoterSweepDialog(const NoterSweepPrompt::SweepResult &result,
                              QWidget *parent = nullptr);

    // Eyebrow line shown at the top of the dialog: "AI sweep · {model}
    // · {duration}s". Optional — defaults to "AI sweep" when not set.
    void setEyebrow(const QString &modelName, qint64 durationMs);
    // Title shown in the eyebrow row. Defaults to "End-meeting sweep".
    void setSweepTitle(const QString &title);
    // The save-target path shown in the footer status row. Display-only.
    void setTargetPath(const QString &absPath);

    // Subset of result.actions whose reminder toggle is ON when the
    // user clicked "Save & schedule". Empty if user clicked Discard.
    QVector<NoterSweepPrompt::SweepResult::Item> reminderItems() const;

    // The full result with any in-dialog edits the user made (text,
    // owner, due). Mirrors the original on Discard.
    NoterSweepPrompt::SweepResult finalResult() const;

    // v0.1.98 — tell the dialog which reminders are ALREADY scheduled for this
    // note (title + time). Call AFTER construction, before exec(). The dialog
    // lists them in the header ("Already scheduled: …") and default-UNCHECKS the
    // "Remind" toggle on any action row that fuzzy-matches one, so re-running
    // Extract doesn't silently create duplicate reminders. The user can still
    // re-check a row deliberately.
    void setExistingReminders(const QVector<QPair<QString, QDateTime>> &existing);

    // v0.1.112 — honest long-note coverage notice: "the AI read only the
    // first N of M words". Call when build() reported truncation; no-op /
    // hides the label for the full-coverage case.
    void setTruncationNotice(int wordsUsed, int wordsTotal);

signals:
    void saveRequested();
    void discardRequested();

private:
    // Per-row editor state. We hold a parallel vector for each section
    // so save-time we can rebuild the result with edits applied. The
    // reminder toggle only exists for the actions section.
    struct RowWidgets {
        QLineEdit     *text     = nullptr;
        QLineEdit     *owner    = nullptr;   // actions only — null otherwise
        QDateTimeEdit *remindAt = nullptr;   // actions only — calendar+time picker
        QCheckBox     *remind   = nullptr;   // actions only — schedule toggle
    };

    void buildHeader();
    void buildSections();
    void buildFooter();

    QWidget   *buildSection(const QString &heading,
                            const QString &accentHex,
                            const QVector<NoterSweepPrompt::SweepResult::Item> &items,
                            QVector<RowWidgets> *rowsOut,
                            bool isActions);

    void refreshCounts();
    void refreshFooterStatus();

    NoterSweepPrompt::SweepResult m_result;
    QString m_modelName;
    qint64  m_durationMs = 0;
    QString m_sweepTitle;
    QString m_targetPath;

    QVBoxLayout *m_outerLayout = nullptr;
    QLabel      *m_eyebrowLabel = nullptr;
    QLabel      *m_titleLabel   = nullptr;
    QLabel      *m_countsLabel  = nullptr;
    QLabel      *m_existingLabel = nullptr;   // "Already scheduled: …" (v0.1.98)
    QLabel      *m_truncLabel    = nullptr;   // "Long note: read first N of M words" (v0.1.112)
    QLabel      *m_footerStatus = nullptr;
    QVector<QPair<QString, QDateTime>> m_existing;   // reminders already on this note
    QPushButton *m_discardBtn   = nullptr;
    QPushButton *m_saveBtn      = nullptr;

    QVector<RowWidgets> m_decisionRows;
    QVector<RowWidgets> m_actionRows;
    QVector<RowWidgets> m_questionRows;
    QVector<RowWidgets> m_riskRows;
};

#endif
