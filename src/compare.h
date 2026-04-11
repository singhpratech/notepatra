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

    QsciScintilla *m_leftEditor, *m_rightEditor;
    CompareNavBar *m_navBar;
    QPushButton *m_editToggle = nullptr;
    QLabel *m_leftHeader, *m_rightHeader, *m_statsLabel;
    QCheckBox *m_ignoreWhitespace, *m_ignoreCase, *m_ignoreEmptyLines;

    QString m_leftText, m_rightText;
    QVector<int> m_rowKinds;
    QVector<int> m_diffLines;
    int m_currentDiff = -1;
    bool m_editable = false;
};

// Backward compat
class CompareDialog : public QWidget {
public:
    CompareDialog(const QString &l, const QString &ln, const QString &r, const QString &rn, QWidget *p = nullptr);
};

#endif
