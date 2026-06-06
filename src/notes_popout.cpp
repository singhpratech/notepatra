// SPDX-License-Identifier: GPL-3.0-or-later
//
// NoterPopOut implementation. See notes_popout.h for design notes.

#include "notes_popout.h"

#include <QCloseEvent>
#include <QFile>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QLabel>
#include <QMouseEvent>
#include <QPushButton>
#include <QScrollBar>
#include <QStyle>
#include <QTextEdit>
#include <QTextStream>
#include <QTimer>
#include <QVBoxLayout>

NoterPopOut::NoterPopOut(const QString &noteAbsPath, QWidget *parent)
    : QWidget(parent), m_notePath(noteAbsPath) {
    // Close destroys the window. The panel holds a raw pointer guarded by
    // a destroyed-connection; with DeleteOnClose OFF the window merely hid
    // on close, the pointer stayed non-null, and popOutActive() early-
    // returned forever — the pop-out was permanently dead after its first
    // close (v0.1.111 audit, CRITICAL).
    setAttribute(Qt::WA_DeleteOnClose, true);
    setAttribute(Qt::WA_ShowWithoutActivating, true);

    // Frameless + always-on-top window. We keep Qt::Window so the
    // platform still creates a top-level surface for it.
    setWindowFlags(Qt::Window
                   | Qt::FramelessWindowHint
                   | Qt::WindowStaysOnTopHint);

    resize(480, 540);
    setMinimumSize(320, 240);

    setStyleSheet(
        // Subtle drop-shadow effect courtesy of a 1px outer border;
        // real shadow needs a platform-specific blur which Qt5 lacks
        // on X11. The border is enough to read against video.
        "NoterPopOut { background: #FFFFFF; border: 1px solid #C7D2FE; }"
    );

    buildUi();

    m_session.start();
    m_clockTimer = new QTimer(this);
    m_clockTimer->setInterval(1000);
    connect(m_clockTimer, &QTimer::timeout, this, &NoterPopOut::tickTimer);
    m_clockTimer->start();

    m_reloadTimer = new QTimer(this);
    m_reloadTimer->setInterval(2000);
    connect(m_reloadTimer, &QTimer::timeout, this, &NoterPopOut::reloadFromDisk);
    m_reloadTimer->start();

    reloadFromDisk();
    tickTimer();
}

NoterPopOut::~NoterPopOut() = default;

void NoterPopOut::buildUi() {
    QVBoxLayout *outer = new QVBoxLayout(this);
    outer->setContentsMargins(0, 0, 0, 0);
    outer->setSpacing(0);

    // ─── title bar ──────────────────────────────────────────────────
    m_titleBar = new QWidget(this);
    m_titleBar->setObjectName("popOutTitleBar");
    m_titleBar->setFixedHeight(30);
    m_titleBar->setStyleSheet(
        "QWidget#popOutTitleBar { background: #EEF2FF; "
        "border-bottom: 1px solid #C7D2FE; }"
        "QPushButton { background: transparent; border: none; "
        "padding: 0 6px; }"
        "QPushButton:hover { background: #C7D2FE; border-radius: 3px; }"
    );

    QHBoxLayout *tl = new QHBoxLayout(m_titleBar);
    tl->setContentsMargins(8, 0, 4, 0);
    tl->setSpacing(6);

    // Pin button — toggles always-on-top.
    m_pinBtn = new QPushButton(m_titleBar);
    m_pinBtn->setIcon(style()->standardIcon(QStyle::SP_DialogApplyButton));
    m_pinBtn->setToolTip(tr("Always on top"));
    m_pinBtn->setCheckable(true);
    m_pinBtn->setChecked(true);
    m_pinBtn->setFixedSize(22, 22);
    m_pinBtn->setCursor(Qt::PointingHandCursor);
    connect(m_pinBtn, &QPushButton::clicked, this, &NoterPopOut::onTogglePin);
    tl->addWidget(m_pinBtn);

    // Title — seeded from the filename stem as a fallback; the panel
    // overrides it with the prettified sidebar label via setDisplayTitle()
    // right after construction (raw "2026-06-06-145233-noter-06" stems
    // are not user-facing).
    QString stem = QFileInfo(m_notePath).completeBaseName();
    if (stem.isEmpty()) stem = tr("Note");
    m_titleLabel = new QLabel(stem, m_titleBar);
    m_titleLabel->setStyleSheet(
        "QLabel { color: #1E1B4B; font-weight: 600; font-size: 12px; }"
    );
    tl->addWidget(m_titleLabel, 1);

    // Live timer — mm:ss since the pop-out opened.
    m_timerLabel = new QLabel(QStringLiteral("00:00"), m_titleBar);
    m_timerLabel->setStyleSheet(
        "QLabel { color: #4338CA; font-family: monospace; "
        "font-size: 12px; padding: 0 6px; "
        "background: #C7D2FE; border-radius: 4px; }"
    );
    tl->addWidget(m_timerLabel);

    m_minBtn = new QPushButton(m_titleBar);
    m_minBtn->setIcon(style()->standardIcon(QStyle::SP_TitleBarMinButton));
    m_minBtn->setFixedSize(22, 22);
    m_minBtn->setCursor(Qt::PointingHandCursor);
    m_minBtn->setToolTip(tr("Minimize"));
    connect(m_minBtn, &QPushButton::clicked, this, &QWidget::showMinimized);
    tl->addWidget(m_minBtn);

    m_maxBtn = new QPushButton(m_titleBar);
    m_maxBtn->setIcon(style()->standardIcon(QStyle::SP_TitleBarMaxButton));
    m_maxBtn->setFixedSize(22, 22);
    m_maxBtn->setCursor(Qt::PointingHandCursor);
    m_maxBtn->setToolTip(tr("Maximize"));
    connect(m_maxBtn, &QPushButton::clicked, this, &NoterPopOut::onToggleMinMax);
    tl->addWidget(m_maxBtn);

    m_closeBtn = new QPushButton(m_titleBar);
    m_closeBtn->setIcon(style()->standardIcon(QStyle::SP_TitleBarCloseButton));
    m_closeBtn->setFixedSize(22, 22);
    m_closeBtn->setCursor(Qt::PointingHandCursor);
    m_closeBtn->setToolTip(tr("Close pop-out"));
    connect(m_closeBtn, &QPushButton::clicked, this, &QWidget::close);
    tl->addWidget(m_closeBtn);

    outer->addWidget(m_titleBar, 0);

    // ─── body ───────────────────────────────────────────────────────
    m_body = new QTextEdit(this);
    m_body->setReadOnly(true);
    m_body->setFrameShape(QFrame::NoFrame);
    m_body->setStyleSheet(
        "QTextEdit { background: #FFFFFF; color: #111827; "
        "padding: 10px 14px; font-size: 13px; }"
    );
    outer->addWidget(m_body, 1);
}

void NoterPopOut::setDisplayTitle(const QString &title) {
    if (title.isEmpty()) return;
    if (m_titleLabel) m_titleLabel->setText(title);
    setWindowTitle(title);   // taskbar / alt-tab label too
}

void NoterPopOut::reloadFromDisk() {
    if (m_notePath.isEmpty() || !m_body) return;
    QFile f(m_notePath);
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text)) {
        // File missing or unreadable — show a small placeholder rather
        // than blanking the body, so the user sees something is wrong.
        m_body->setPlainText(tr("(note file unavailable)"));
        return;
    }
    QTextStream ts(&f);
    ts.setCodec("UTF-8");
    const QString content = ts.readAll();
    f.close();

    // Preserve scroll position across reloads so the user's view
    // doesn't snap to the top every 2 s.
    const int sv = m_body->verticalScrollBar() ?
        m_body->verticalScrollBar()->value() : 0;
    if (content.startsWith(QLatin1String("<")) ||
        content.contains(QLatin1String("<html"), Qt::CaseInsensitive)) {
        m_body->setHtml(content);
    } else {
        m_body->setPlainText(content);
    }
    if (m_body->verticalScrollBar())
        m_body->verticalScrollBar()->setValue(sv);
}

void NoterPopOut::tickTimer() {
    if (!m_timerLabel) return;
    const qint64 secs = m_session.elapsed() / 1000;
    const int mm = static_cast<int>(secs / 60);
    const int ss = static_cast<int>(secs % 60);
    m_timerLabel->setText(QString::asprintf("%02d:%02d", mm, ss));
}

void NoterPopOut::onTogglePin() {
    m_pinned = m_pinBtn->isChecked();
    Qt::WindowFlags f = windowFlags();
    if (m_pinned) f |= Qt::WindowStaysOnTopHint;
    else          f &= ~Qt::WindowStaysOnTopHint;
    // Have to re-show after a flag change on X11.
    setWindowFlags(f);
    show();
}

void NoterPopOut::onToggleMinMax() {
    if (m_userMaximized) {
        showNormal();
        m_userMaximized = false;
    } else {
        showMaximized();
        m_userMaximized = true;
    }
}

void NoterPopOut::mousePressEvent(QMouseEvent *ev) {
    // Only start a drag if the user clicked inside the title strip —
    // dragging from the body would conflict with text selection.
    if (ev->button() == Qt::LeftButton && m_titleBar &&
        m_titleBar->geometry().contains(ev->pos())) {
        m_dragging = true;
        m_dragStartWindow = pos();
        m_dragStartMouse = ev->globalPos();
        ev->accept();
        return;
    }
    QWidget::mousePressEvent(ev);
}

void NoterPopOut::mouseMoveEvent(QMouseEvent *ev) {
    if (m_dragging) {
        const QPoint delta = ev->globalPos() - m_dragStartMouse;
        move(m_dragStartWindow + delta);
        ev->accept();
        return;
    }
    QWidget::mouseMoveEvent(ev);
}

void NoterPopOut::mouseReleaseEvent(QMouseEvent *ev) {
    m_dragging = false;
    QWidget::mouseReleaseEvent(ev);
}

void NoterPopOut::closeEvent(QCloseEvent *ev) {
    emit closed();
    QWidget::closeEvent(ev);
}
