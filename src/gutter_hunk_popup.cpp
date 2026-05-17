// SPDX-License-Identifier: GPL-3.0-or-later

#include "gutter_hunk_popup.h"
#include "diff_view.h"

#include <QApplication>
#include <QClipboard>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QPushButton>
#include <QScreen>
#include <QShowEvent>
#include <QVBoxLayout>

// ─── Popup helper: clamp a global anchor point to fit the popup on-screen ──
//
// Without this, a click on a gutter near the bottom-right of a maximised
// window would put the popup partially off-screen. We use the screen
// containing the anchor point (multi-monitor friendly) rather than the
// primary screen.
static QPoint clampToScreen(const QPoint &globalAnchor, const QSize &popupSize) {
    QScreen *scr = QGuiApplication::screenAt(globalAnchor);
    if (!scr) scr = QGuiApplication::primaryScreen();
    if (!scr) return globalAnchor;
    const QRect avail = scr->availableGeometry();
    QPoint pos = globalAnchor;
    if (pos.x() + popupSize.width() > avail.right()) {
        pos.setX(avail.right() - popupSize.width());
    }
    if (pos.y() + popupSize.height() > avail.bottom()) {
        pos.setY(avail.bottom() - popupSize.height());
    }
    if (pos.x() < avail.left()) pos.setX(avail.left());
    if (pos.y() < avail.top())  pos.setY(avail.top());
    return pos;
}

GutterHunkPopup::GutterHunkPopup(const QString &absFilePath,
                                 const QString &repoRoot,
                                 const DiffHunk &hunk,
                                 const QString &beforeText,
                                 const QString &afterText,
                                 QWidget *parent)
    : QFrame(parent),
      m_absFilePath(absFilePath),
      m_repoRoot(repoRoot),
      m_hunk(hunk),
      m_beforeText(beforeText),
      m_afterText(afterText) {
    // Qt::Popup gives us auto-close on click-outside which is exactly the
    // VS Code interaction model. FramelessWindowHint removes the OS chrome
    // so the widget reads as a tooltip-ish overlay. Tool window keeps it
    // off the taskbar.
    setWindowFlags(Qt::Popup | Qt::FramelessWindowHint | Qt::Tool);
    setAttribute(Qt::WA_DeleteOnClose, true);
    setFrameShape(QFrame::Box);
    setFrameShadow(QFrame::Plain);
    setLineWidth(1);

    setObjectName("GutterHunkPopup");
    setStyleSheet(
        "QFrame#GutterHunkPopup { background: palette(window); "
        "    border: 1px solid palette(mid); }"
        "QFrame#GutterHunkPopup QLabel#hunkTitle { font-weight: 600; }"
        "QFrame#GutterHunkPopup QLabel#hunkError { color: #d32f2f; "
        "    font-size: 11px; }"
        "QFrame#GutterHunkPopup QPushButton { padding: 4px 10px; }");

    auto *outer = new QVBoxLayout(this);
    outer->setContentsMargins(8, 6, 8, 6);
    outer->setSpacing(6);

    // ── Header: title + action buttons ──
    const int firstLine = (hunk.newLen > 0) ? hunk.newStart : hunk.newStart;
    QString titleTxt;
    if (hunk.newLen > 0 && hunk.newLen != 1) {
        titleTxt = QString("Hunk · lines %1–%2")
                       .arg(firstLine)
                       .arg(firstLine + hunk.newLen - 1);
    } else if (hunk.newLen == 0) {
        titleTxt = QString("Deletion at line %1").arg(firstLine);
    } else {
        titleTxt = QString("Hunk · line %1").arg(firstLine);
    }
    m_titleLabel = new QLabel(titleTxt, this);
    m_titleLabel->setObjectName("hunkTitle");

    m_stageBtn  = new QPushButton(tr("Stage Hunk"), this);
    m_revertBtn = new QPushButton(tr("Revert Hunk"), this);
    m_copyBtn   = new QPushButton(tr("Copy Diff"), this);
    m_stageBtn->setToolTip(tr("git apply --cached for this hunk only"));
    m_revertBtn->setToolTip(tr("git apply --reverse — discard this hunk in the working copy"));
    m_copyBtn->setToolTip(tr("Copy unified-diff text to clipboard"));

    auto *btnRow = new QHBoxLayout;
    btnRow->setContentsMargins(0, 0, 0, 0);
    btnRow->setSpacing(6);
    btnRow->addWidget(m_titleLabel, /*stretch*/1);
    btnRow->addWidget(m_stageBtn);
    btnRow->addWidget(m_revertBtn);
    btnRow->addWidget(m_copyBtn);
    outer->addLayout(btnRow);

    // ── Inline error label (hidden until something fails) ──
    m_errorLabel = new QLabel(this);
    m_errorLabel->setObjectName("hunkError");
    m_errorLabel->setWordWrap(true);
    m_errorLabel->hide();
    outer->addWidget(m_errorLabel);

    // ── Embedded DiffView ──
    m_diffView = new DiffView(m_beforeText, m_afterText, this);
    m_diffView->setMinimumSize(560, 200);
    outer->addWidget(m_diffView, /*stretch*/1);

    setMinimumWidth(600);
    setMinimumHeight(260);

    connect(m_stageBtn,  &QPushButton::clicked, this, &GutterHunkPopup::onStageClicked);
    connect(m_revertBtn, &QPushButton::clicked, this, &GutterHunkPopup::onRevertClicked);
    connect(m_copyBtn,   &QPushButton::clicked, this, &GutterHunkPopup::onCopyDiffClicked);
}

void GutterHunkPopup::showAt(const QPoint &globalPos) {
    // We size-hint first so we can clamp accurately. show() does the rest.
    adjustSize();
    const QPoint pos = clampToScreen(globalPos, size());
    move(pos);
    show();
    raise();
    activateWindow();
    setFocus();
}

void GutterHunkPopup::setErrorMessage(const QString &text) {
    if (!m_errorLabel) return;
    m_errorLabel->setText(text);
    m_errorLabel->show();
    adjustSize();
}

void GutterHunkPopup::clearErrorMessage() {
    if (!m_errorLabel) return;
    m_errorLabel->clear();
    m_errorLabel->hide();
}

void GutterHunkPopup::onStageClicked() {
    clearErrorMessage();
    m_stageBtn->setEnabled(false);
    GitHunkApply::Result r = GitHunkApply::applyHunk(
        m_absFilePath, m_repoRoot, m_hunk, GitHunkApply::Mode::Stage);
    if (!r.ok) {
        m_stageBtn->setEnabled(true);
        setErrorMessage(tr("Stage failed (%1): %2")
                            .arg(r.errorKind.isEmpty() ? "error" : r.errorKind)
                            .arg(r.message));
        return;
    }
    emit hunkStaged(m_absFilePath);
    close();
}

void GutterHunkPopup::onRevertClicked() {
    clearErrorMessage();
    m_revertBtn->setEnabled(false);
    GitHunkApply::Result r = GitHunkApply::applyHunk(
        m_absFilePath, m_repoRoot, m_hunk, GitHunkApply::Mode::Revert);
    if (!r.ok) {
        m_revertBtn->setEnabled(true);
        setErrorMessage(tr("Revert failed (%1): %2")
                            .arg(r.errorKind.isEmpty() ? "error" : r.errorKind)
                            .arg(r.message));
        return;
    }
    emit hunkReverted(m_absFilePath);
    close();
}

void GutterHunkPopup::onCopyDiffClicked() {
    // We synthesise the patch even if the user hasn't clicked Stage — that
    // way "Copy Diff" gives them the exact text we'd send to `git apply`.
    // This is the same lineage as VS Code's "Copy as Diff" gesture.
    QString errKind;
    QString patch = GitHunkApply::synthesizePatch(
        m_absFilePath, m_repoRoot, m_hunk, &errKind);
    if (patch.isEmpty()) {
        setErrorMessage(tr("Cannot synthesize patch (%1).")
                            .arg(errKind.isEmpty() ? "error" : errKind));
        return;
    }
    QApplication::clipboard()->setText(patch);
    m_copyBtn->setText(tr("Copied!"));
    // The popup auto-closes on click-outside; the "Copied!" label is for
    // the brief moment before the user does that. No timer-driven revert
    // needed — popup is short-lived.
}
