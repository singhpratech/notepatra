// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef FINDREPLACE_H
#define FINDREPLACE_H

#include <QDialog>
#include <QTabWidget>
#include <QLineEdit>
#include <QCheckBox>
#include <QRadioButton>
#include <QComboBox>
#include <QTextEdit>
#include <QLabel>

class Editor;
class MainWindow;

class FindReplaceDialog : public QDialog {
    Q_OBJECT
public:
    explicit FindReplaceDialog(QWidget *parent = nullptr);
    void showFind();
    void showReplace();
    void showFindInFiles();
    void showGoto();
    void showMark();
    void findNext();
    void findPrevious();

    QComboBox *findInput() { return m_findInput; }

private:
    void buildFindTab(QWidget *tab);
    void buildReplaceTab(QWidget *tab);
    void buildFindInFilesTab(QWidget *tab);
    void buildMarkTab(QWidget *tab);
    void buildGotoTab(QWidget *tab);

    // v0.1.92 — Notepad++-style dialog-level status line. Shows the result
    // of the last action (find, replace, count, find-in-files) at the
    // bottom of the dialog, always visible regardless of active tab.
    void setDialogStatus(const QString &msg, bool isError = false);

    MainWindow *mainWindow();
    Editor *getEditor();

    void doFindNext(bool forward);
    void doCount();
    void doFindAllCurrent();
    void doFindAllOpened();
    void doReplace();
    void doReplaceAll();
    void doReplaceAllOpened();
    void doFindInFiles();
    void doMarkAll();

    QTabWidget *m_tabs;

    // Find tab
    QComboBox *m_findInput;
    QCheckBox *m_matchCase, *m_wholeWord, *m_wrapAround;
    QRadioButton *m_modeNormal, *m_modeExtended, *m_modeRegex;
    QRadioButton *m_dirUp, *m_dirDown;
    QCheckBox *m_dotMatchesNewline, *m_inSelection;
    QLabel *m_resultLabel;

    // Replace tab
    QComboBox *m_replFindInput, *m_replInput;
    QCheckBox *m_rMatchCase, *m_rWholeWord, *m_rWrapAround;
    QRadioButton *m_rModeNormal, *m_rModeExtended, *m_rModeRegex;
    QCheckBox *m_rInSelection;

    // Find in Files tab
    QComboBox *m_fifFindInput, *m_fifDirectory, *m_fifFilters;
    QCheckBox *m_fifMatchCase, *m_fifWholeWord, *m_fifRegex;
    QCheckBox *m_fifSubfolders, *m_fifHidden;

    // Mark tab
    QComboBox *m_markFindInput;
    QCheckBox *m_markCase, *m_markWholeWord, *m_markRegex, *m_markPurge;

    // Goto
    QLineEdit *m_gotoInput;
    QRadioButton *m_gotoLine, *m_gotoOffset;

    // Results
    QTextEdit *m_resultsOutput;

    // v0.1.92 — bottom-of-dialog status line (Notepad++ style).
    QLabel *m_dialogStatus = nullptr;

    // v0.1.92 — Find-cycle state. When the user iterates Find Next and
    // hits the end of the document, we STOP there (with a "Reached end —
    // press again to wrap" message) instead of silently wrapping. The
    // SECOND consecutive Find Next at that point actually wraps. Reset
    // when the search text, direction, or editor changes.
    bool m_atFindCycleEnd = false;
    QString m_lastFindText;
    bool m_lastFindForward = true;

    QString processExtended(const QString &text);
};

#endif
