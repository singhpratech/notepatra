// SPDX-License-Identifier: GPL-3.0-or-later

#include "statusbar.h"
#include <QHBoxLayout>
#include <QFont>
#include <QMouseEvent>

static QLabel *makeSep() {
    auto *sep = new QLabel;
    sep->setFixedWidth(1);
    sep->setFixedHeight(18);
    sep->setObjectName("statusSep");
    return sep;
}

static QLabel *makeLabel(const QString &text, int minW, bool bold = false) {
    auto *l = new QLabel(text);
    l->setMinimumWidth(minW);
    l->setAlignment(Qt::AlignCenter | Qt::AlignVCenter);
    QFont f = l->font();
    f.setPointSize(9);
    f.setBold(bold);
    l->setFont(f);
    return l;
}

NppStatusBar::NppStatusBar(QWidget *parent) : QWidget(parent) {
    setFixedHeight(26);
    auto *lay = new QHBoxLayout(this);
    lay->setContentsMargins(4, 0, 4, 0);
    lay->setSpacing(0);

    m_lang = makeLabel(" Normal text", 120, true);
    m_lang->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    m_lang->setCursor(Qt::PointingHandCursor);
    m_lang->setToolTip("Click to change language");
    lay->addWidget(m_lang, 1);
    lay->addWidget(makeSep());

    m_size = makeLabel("length : 0   lines : 1", 170);
    lay->addWidget(m_size);
    lay->addWidget(makeSep());

    m_pos = makeLabel("Ln : 1   Col : 1   Pos : 1", 260);
    lay->addWidget(m_pos);
    lay->addWidget(makeSep());

    m_eol = makeLabel("Unix (LF)", 90);
    m_eol->setCursor(Qt::PointingHandCursor);
    m_eol->setToolTip("Click to change line endings");
    lay->addWidget(m_eol);
    lay->addWidget(makeSep());

    m_enc = makeLabel("UTF-8", 70);
    m_enc->setCursor(Qt::PointingHandCursor);
    m_enc->setToolTip("Click to change encoding");
    lay->addWidget(m_enc);
    lay->addWidget(makeSep());

    m_ins = makeLabel("INS", 36, true);
    lay->addWidget(m_ins);
}

void NppStatusBar::updatePosition(int line, int col, int pos) {
    m_pos->setText(QString("Ln : %1   Col : %2   Pos : %3").arg(line).arg(col).arg(pos));
}

void NppStatusBar::updateSelection(int chars, int lines) {
    if (chars > 0) {
        QString base = m_pos->text().split("|").first().trimmed();
        m_pos->setText(QString("%1   |   Sel : %2 | %3").arg(base).arg(chars).arg(lines));
    }
}

void NppStatusBar::updateLines(int count) {
    QString t = m_size->text();
    int idx = t.indexOf("lines");
    if (idx >= 0)
        m_size->setText(t.left(idx) + QString("lines : %1").arg(count));
}

void NppStatusBar::updateLength(int length) {
    QString t = m_size->text();
    int idx = t.indexOf("lines");
    QString linesPart = (idx >= 0) ? t.mid(idx) : "lines : 1";
    m_size->setText(QString("length : %1   %2").arg(length).arg(linesPart));
}

void NppStatusBar::updateWords(int count) {
    // Append word count to size label
    QString t = m_size->text();
    int wIdx = t.indexOf("words");
    if (wIdx >= 0) t = t.left(wIdx).trimmed();
    m_size->setText(t + QString("   words : %1").arg(count));
}

void NppStatusBar::updateLanguage(const QString &lang) { m_lang->setText(" " + lang); }
void NppStatusBar::updateEncoding(const QString &enc) { m_enc->setText(enc); }
void NppStatusBar::updateEol(const QString &eol) { m_eol->setText(eol); }
void NppStatusBar::updateInsertMode(bool ovr) { m_ins->setText(ovr ? "OVR" : "INS"); }

void NppStatusBar::mousePressEvent(QMouseEvent *event) {
    if (event->button() != Qt::LeftButton) { QWidget::mousePressEvent(event); return; }
    QWidget *target = childAt(event->pos());
    const QPoint g = event->globalPos();
    if (target == m_lang)      { emit languageClicked(g); event->accept(); return; }
    if (target == m_enc)       { emit encodingClicked(g); event->accept(); return; }
    if (target == m_eol)       { emit eolClicked(g);      event->accept(); return; }
    QWidget::mousePressEvent(event);
}

void NppStatusBar::applyColors(const QString &bg, const QString &fg, const QString &sep) {
    setStyleSheet(QString(
        "NppStatusBar { background-color: %1; border-top: 1px solid %3; }"
        "NppStatusBar QLabel { color: %2; background: transparent; padding: 0 4px; }"
        "NppStatusBar #statusSep { background-color: %3; padding: 0; margin: 4px 2px; }"
    ).arg(bg, fg, sep));
}
