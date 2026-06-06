// SPDX-License-Identifier: GPL-3.0-or-later
//
// NoterPopOut — borderless always-on-top mini-window that shows a
// snapshot of the current note. Designed for live-meeting use: the
// user keeps the main editor open behind a video call while a tiny
// 480×540 floating window stays glued to the foreground.
//
// The pop-out is a READ-ONLY snapshot. It re-reads the file from disk
// every 2 s so manual edits in the main editor are reflected without
// the pop-out and main editor competing over the same QTextDocument.
//
// Title bar is custom (Qt::FramelessWindowHint), draggable, with pin
// / meeting-title / live-timer / minimize / maximize / close buttons.

#ifndef NOTES_POPOUT_H
#define NOTES_POPOUT_H

#include <QElapsedTimer>
#include <QPoint>
#include <QString>
#include <QWidget>

class QLabel;
class QPushButton;
class QTextEdit;
class QTimer;

class NoterPopOut : public QWidget {
    Q_OBJECT
public:
    explicit NoterPopOut(const QString &noteAbsPath,
                         QWidget *parent = nullptr);
    ~NoterPopOut() override;

    // Path to the note this pop-out is mirroring. Read-only after ctor.
    QString notePath() const { return m_notePath; }

    // Titlebar text. The ctor seeds the raw filename stem as a fallback;
    // NotesPanel overrides it with the same prettified label the sidebar
    // shows ("Noter 06", not "2026-06-06-145233-noter-06").
    void setDisplayTitle(const QString &title);

    // Test hooks — let unit tests inspect chrome without yanking it
    // out via objectName lookup.
    QLabel      *titleLabelForTesting()  const { return m_titleLabel; }
    QLabel      *timerLabelForTesting()  const { return m_timerLabel; }
    QTextEdit   *bodyForTesting()        const { return m_body; }
    QPushButton *closeBtnForTesting()    const { return m_closeBtn; }

signals:
    void closed();

protected:
    // Custom drag support — Qt::FramelessWindowHint kills the native
    // titlebar so we drive movement from mousePress / mouseMove on
    // the title strip widget.
    void mousePressEvent(QMouseEvent *ev) override;
    void mouseMoveEvent(QMouseEvent *ev) override;
    void mouseReleaseEvent(QMouseEvent *ev) override;
    void closeEvent(QCloseEvent *ev) override;

private:
    void buildUi();
    void reloadFromDisk();
    void tickTimer();
    void onTogglePin();
    void onToggleMinMax();

    QString m_notePath;

    // Header chrome.
    QWidget     *m_titleBar = nullptr;
    QPushButton *m_pinBtn = nullptr;
    QLabel      *m_titleLabel = nullptr;
    QLabel      *m_timerLabel = nullptr;
    QPushButton *m_minBtn = nullptr;
    QPushButton *m_maxBtn = nullptr;
    QPushButton *m_closeBtn = nullptr;

    QTextEdit   *m_body = nullptr;

    QTimer        *m_reloadTimer = nullptr;
    QTimer        *m_clockTimer = nullptr;
    QElapsedTimer  m_session;

    // Drag state.
    bool   m_dragging = false;
    QPoint m_dragStartWindow;
    QPoint m_dragStartMouse;

    bool   m_pinned = true;        // always-on-top, default ON
    bool   m_userMaximized = false;
};

#endif // NOTES_POPOUT_H
