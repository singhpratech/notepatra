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
