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

    QString processExtended(const QString &text);
};

#endif
