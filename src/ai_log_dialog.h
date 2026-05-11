#ifndef NOTEPATRA_AI_LOG_DIALOG_H
#define NOTEPATRA_AI_LOG_DIALOG_H

// ═══════════════════════════════════════════════════════════════════════
// v0.1.71 — AI Interaction Log viewer dialog.
//
// Opens from Tools → AI Interaction Log… and lets the user audit every
// request / response that has hit a cloud or local LLM in the last 7
// days. Filters by backend / model / mode and ships an "Export to JSON"
// button so the user can keep a copy outside Notepatra. Read-only.
// ═══════════════════════════════════════════════════════════════════════

#include <QDialog>

class QTableWidget;
class QLineEdit;
class QComboBox;
class QCheckBox;
class QPlainTextEdit;

class AiLogDialog : public QDialog {
    Q_OBJECT
public:
    explicit AiLogDialog(QWidget *parent = nullptr);

private slots:
    void onRefresh();
    void onExportJson();
    void onRowChanged();
    void onLoggingToggled(bool on);
    void onPruneNow();

private:
    void reloadFromDb();

    QTableWidget   *m_table = nullptr;
    QPlainTextEdit *m_detail = nullptr;
    QComboBox      *m_backendFilter = nullptr;
    QComboBox      *m_modeFilter = nullptr;
    QLineEdit      *m_modelFilter = nullptr;
    QCheckBox      *m_enabledToggle = nullptr;
};

#endif // NOTEPATRA_AI_LOG_DIALOG_H
