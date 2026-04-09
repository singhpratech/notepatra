#ifndef COMPARE_H
#define COMPARE_H

#include <QWidget>
#include <QLabel>
#include <QPushButton>
#include <QCheckBox>
#include <Qsci/qsciscintilla.h>

/**
 * ComparePlus-style diff — two real Scintilla editors side by side.
 * Added lines = green background, deleted = red, changed = yellow.
 * Navigate between diffs. Synced scrolling. Actual syntax highlighting.
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

private:
    void navigateNext();
    void navigatePrev();
    void recompare();
    void setupEditor(QsciScintilla *ed);

    QsciScintilla *m_leftEditor, *m_rightEditor;
    QLabel *m_leftHeader, *m_rightHeader, *m_statsLabel;
    QCheckBox *m_ignoreWhitespace, *m_ignoreCase, *m_ignoreEmptyLines;

    QString m_leftText, m_rightText;
    QVector<int> m_diffLines;
    int m_currentDiff = -1;
};

// Backward compat
class CompareDialog : public QWidget {
public:
    CompareDialog(const QString &l, const QString &ln, const QString &r, const QString &rn, QWidget *p = nullptr);
};

#endif
