// SPDX-License-Identifier: GPL-3.0-or-later

#include "notes_sweep_dialog.h"
#include "notes_theme.h"
#include "config.h"

#include <QApplication>
#include <QCheckBox>
#include <QDateTime>
#include <QDateTimeEdit>
#include <QDialogButtonBox>
#include <QFrame>
#include <QGridLayout>
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPalette>
#include <QPushButton>
#include <QRegularExpression>
#include <QScreen>
#include <QScrollArea>
#include <QStyle>
#include <QToolButton>
#include <QVBoxLayout>

// normalizeForMatch moved to NoterSweepPrompt (v0.1.112) — the extract-
// apply done-state carry needs the SAME normalization, so it now lives in
// the shared QtCore-only module. See notes_sweep_prompt.{h,cpp}.

// ═══════════════════════════════════════════════════════════════════════
// Construction
// ═══════════════════════════════════════════════════════════════════════

NoterSweepDialog::NoterSweepDialog(const NoterSweepPrompt::SweepResult &result,
                                   QWidget *parent)
    : QDialog(parent), m_result(result) {
    setWindowTitle(tr("Extract — action items"));
    setModal(true);
    setMinimumWidth(780);
    resize(780, 640);

    m_outerLayout = new QVBoxLayout(this);
    m_outerLayout->setContentsMargins(20, 18, 20, 16);
    m_outerLayout->setSpacing(12);

    buildHeader();
    buildSections();
    buildFooter();

    refreshCounts();
    refreshFooterStatus();

    // Centre on the parent screen — QDialog's default geometry isn't
    // always centered when the parent has a non-default geometry.
    if (parent) {
        const QRect g = parent->geometry();
        move(g.center().x() - width() / 2,
             g.center().y() - height() / 2);
    } else if (QScreen *s = QGuiApplication::primaryScreen()) {
        const QRect g = s->availableGeometry();
        move(g.center().x() - width() / 2,
             g.center().y() - height() / 2);
    }
}

// ═══════════════════════════════════════════════════════════════════════
// Setters
// ═══════════════════════════════════════════════════════════════════════

void NoterSweepDialog::setEyebrow(const QString &modelName, qint64 durationMs) {
    m_modelName  = modelName;
    m_durationMs = durationMs;
    if (m_eyebrowLabel) {
        QString s = QStringLiteral("AI extract");
        if (!modelName.isEmpty()) s += QStringLiteral(" · ") + modelName;
        if (durationMs > 0) {
            const double secs = durationMs / 1000.0;
            s += QString(" · %1s").arg(secs, 0, 'f', 1);
        }
        m_eyebrowLabel->setText(s);
    }
}

void NoterSweepDialog::setSweepTitle(const QString &title) {
    m_sweepTitle = title;
    if (m_titleLabel) {
        m_titleLabel->setText(title.isEmpty()
                                  ? tr("Extract")
                                  : title);
    }
}

void NoterSweepDialog::setTargetPath(const QString &absPath) {
    m_targetPath = absPath;
    refreshFooterStatus();
}

// v0.1.112 — honest long-note coverage notice. Shown when build()
// truncated the note body to fit the model's context window.
void NoterSweepDialog::setTruncationNotice(int wordsUsed, int wordsTotal) {
    if (!m_truncLabel) return;
    if (wordsUsed <= 0 || wordsTotal <= wordsUsed) {
        m_truncLabel->setVisible(false);
        return;
    }
    m_truncLabel->setText(
        tr("Long note: the AI read only the first %L1 of %L2 words. "
           "Items mentioned after that point were not extracted — split or "
           "trim the note to capture them.").arg(wordsUsed).arg(wordsTotal));
    m_truncLabel->setVisible(true);
}

// ═══════════════════════════════════════════════════════════════════════
// Header — eyebrow + bold title + counts row
// ═══════════════════════════════════════════════════════════════════════

void NoterSweepDialog::buildHeader() {
    auto *header = new QWidget(this);
    auto *hv = new QVBoxLayout(header);
    hv->setContentsMargins(0, 0, 0, 0);
    hv->setSpacing(4);

    m_eyebrowLabel = new QLabel(QStringLiteral("AI extract"), header);
    {
        QFont f = m_eyebrowLabel->font();
        f.setCapitalization(QFont::AllUppercase);
        f.setLetterSpacing(QFont::AbsoluteSpacing, 0.6);
        f.setPointSizeF(f.pointSizeF() * 0.82);
        m_eyebrowLabel->setFont(f);
        QPalette p = m_eyebrowLabel->palette();
        // Dim the eyebrow against the dialog background.
        QColor c = p.color(QPalette::WindowText);
        c.setAlpha(160);
        p.setColor(QPalette::WindowText, c);
        m_eyebrowLabel->setPalette(p);
    }

    m_titleLabel = new QLabel(tr("Extract"), header);
    {
        QFont f = m_titleLabel->font();
        f.setPointSizeF(f.pointSizeF() * 1.45);
        f.setBold(true);
        m_titleLabel->setFont(f);
    }

    m_countsLabel = new QLabel(header);
    {
        QFont f = m_countsLabel->font();
        f.setPointSizeF(f.pointSizeF() * 0.95);
        m_countsLabel->setFont(f);
    }

    hv->addWidget(m_eyebrowLabel);
    hv->addWidget(m_titleLabel);
    hv->addWidget(m_countsLabel);

    // v0.1.98 — the model's plain-English summary ("summarize your
    // understanding"). Shown only when present; italic + dimmed so it reads as
    // a recap, not a section.
    if (!m_result.summary.isEmpty()) {
        auto *summaryLabel = new QLabel(m_result.summary, header);
        summaryLabel->setWordWrap(true);
        QFont sf = summaryLabel->font();
        sf.setItalic(true);
        summaryLabel->setFont(sf);
        QPalette sp = summaryLabel->palette();
        QColor sc = sp.color(QPalette::WindowText);
        sc.setAlpha(190);
        sp.setColor(QPalette::WindowText, sc);
        summaryLabel->setPalette(sp);
        hv->addWidget(summaryLabel);
    }

    // v0.1.98 — "Already scheduled for this note: …" line. Empty + hidden until
    // setExistingReminders() fills it. Amber to match the reminder clock icon.
    m_existingLabel = new QLabel(header);
    m_existingLabel->setWordWrap(true);
    m_existingLabel->setVisible(false);
    {
        QFont ef = m_existingLabel->font();
        ef.setPointSizeF(ef.pointSizeF() * 0.9);
        m_existingLabel->setFont(ef);
        // A5 — amber notice ink from the Noter palette so it stays legible
        // on dark dialog backgrounds (#B45309 in Light, brighter in Dark/
        // Monokai). The dialog is modal, so the theme can't change while
        // it's open — baking at construction is safe.
        m_existingLabel->setStyleSheet(QStringLiteral("color: %1;").arg(
            noterPaletteForTheme(Config::instance().theme).noticeFg));
    }
    hv->addWidget(m_existingLabel);

    // v0.1.112 — honest long-note truncation notice. Empty + hidden until
    // setTruncationNotice() fills it. Amber, mirroring m_existingLabel.
    m_truncLabel = new QLabel(header);
    m_truncLabel->setWordWrap(true);
    m_truncLabel->setVisible(false);
    {
        QFont tf = m_truncLabel->font();
        tf.setPointSizeF(tf.pointSizeF() * 0.9);
        m_truncLabel->setFont(tf);
        m_truncLabel->setStyleSheet(QStringLiteral("color: %1;").arg(
            noterPaletteForTheme(Config::instance().theme).noticeFg));
    }
    hv->addWidget(m_truncLabel);

    m_outerLayout->addWidget(header);

    // Divider hairline under the header.
    auto *rule = new QFrame(this);
    rule->setFrameShape(QFrame::HLine);
    rule->setFrameShadow(QFrame::Plain);
    rule->setLineWidth(1);
    m_outerLayout->addWidget(rule);
}

// ═══════════════════════════════════════════════════════════════════════
// Sections — collapsible group cards for decisions / actions / etc.
// ═══════════════════════════════════════════════════════════════════════

void NoterSweepDialog::buildSections() {
    // Scroll body so the dialog still works when the sweep has 30
    // actions and the screen is short.
    auto *scroll = new QScrollArea(this);
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);

    auto *body = new QWidget(scroll);
    auto *bv = new QVBoxLayout(body);
    bv->setContentsMargins(0, 0, 0, 0);
    bv->setSpacing(14);

    // Accent colours pulled from the design surface — kept legible
    // against both light and dark backgrounds. These are HEX strings
    // applied via QSS, so dark mode's automatic luminance contrast
    // (Qt::darken/lighten on the palette) takes over only if the
    // palette overrides them; standard system themes leave them as-is.
    // v0.1.98 — Action Items first (the common case), and render a section
    // ONLY when the AI actually found items: no empty "(none)" preset buckets
    // (user: "keep all, basically action items most of the time"). The all-
    // empty case never reaches here — the caller shows "nothing found" and
    // skips opening the dialog.
    if (!m_result.actions.isEmpty())
        bv->addWidget(buildSection(tr("Action Items"), "#43A047",
                                   m_result.actions, &m_actionRows,
                                   /*isActions=*/true));
    if (!m_result.decisions.isEmpty())
        bv->addWidget(buildSection(tr("Decisions"), "#1E88E5",
                                   m_result.decisions, &m_decisionRows,
                                   /*isActions=*/false));
    if (!m_result.questions.isEmpty())
        bv->addWidget(buildSection(tr("Questions"), "#8E24AA",
                                   m_result.questions, &m_questionRows,
                                   /*isActions=*/false));
    if (!m_result.risks.isEmpty())
        bv->addWidget(buildSection(tr("Risks"), "#E53935",
                                   m_result.risks, &m_riskRows,
                                   /*isActions=*/false));
    bv->addStretch(1);

    scroll->setWidget(body);
    m_outerLayout->addWidget(scroll, /*stretch=*/1);
}

QWidget *NoterSweepDialog::buildSection(
    const QString &heading,
    const QString &accentHex,
    const QVector<NoterSweepPrompt::SweepResult::Item> &items,
    QVector<RowWidgets> *rowsOut,
    bool isActions) {

    auto *card = new QFrame(this);
    card->setFrameShape(QFrame::StyledPanel);
    auto *cv = new QVBoxLayout(card);
    cv->setContentsMargins(12, 10, 12, 12);
    cv->setSpacing(8);

    // ── Heading row: small-caps label + collapse chevron ──
    auto *headRow = new QWidget(card);
    auto *hh = new QHBoxLayout(headRow);
    hh->setContentsMargins(0, 0, 0, 0);
    hh->setSpacing(8);

    auto *toggleBtn = new QToolButton(headRow);
    toggleBtn->setArrowType(Qt::DownArrow);
    toggleBtn->setAutoRaise(true);
    toggleBtn->setCheckable(true);
    toggleBtn->setChecked(true);

    auto *headingLabel = new QLabel(
        QString("%1  (%2)").arg(heading).arg(items.size()), headRow);
    {
        QFont f = headingLabel->font();
        f.setCapitalization(QFont::AllUppercase);
        f.setLetterSpacing(QFont::AbsoluteSpacing, 1.2);
        f.setBold(true);
        f.setPointSizeF(f.pointSizeF() * 0.90);
        headingLabel->setFont(f);
        // Apply accent colour via stylesheet so it survives palette
        // recompute in dark mode.
        headingLabel->setStyleSheet(QString("color: %1;").arg(accentHex));
    }

    hh->addWidget(toggleBtn);
    hh->addWidget(headingLabel, 1);
    cv->addWidget(headRow);

    // ── Rows container — toggled by the chevron ──
    auto *rowsHost = new QWidget(card);
    auto *rv = new QVBoxLayout(rowsHost);
    rv->setContentsMargins(0, 0, 0, 0);
    rv->setSpacing(6);

    if (items.isEmpty()) {
        auto *empty = new QLabel(tr("(none)"), rowsHost);
        QPalette p = empty->palette();
        QColor c = p.color(QPalette::WindowText);
        c.setAlpha(120);
        p.setColor(QPalette::WindowText, c);
        empty->setPalette(p);
        rv->addWidget(empty);
    } else {
        for (const auto &item : items) {
            RowWidgets rw;

            auto *row = new QFrame(rowsHost);
            auto *grid = new QGridLayout(row);
            grid->setContentsMargins(0, 2, 0, 2);
            grid->setHorizontalSpacing(8);
            grid->setVerticalSpacing(2);

            int col = 0;

            // For actions: prepend the remind toggle. ON by default.
            if (isActions) {
                rw.remind = new QCheckBox(tr("Remind"), row);
                // v0.1.98 — default ON only when the model extracted a concrete
                // time; actions with no stated time stay OFF so we don't
                // auto-schedule a guessed "tomorrow 9am" for every task. Intent:
                // remind me for the things I actually gave a time for.
                rw.remind->setChecked(item.dueAt.isValid());
                rw.remind->setToolTip(
                    tr("Schedule a reminder for this action item. "
                       "Uses cross-platform notifications (v0.1.93+)."));
                grid->addWidget(rw.remind, 0, col++);
            } else {
                auto *glyph = new QLabel(item.glyph, row);
                glyph->setFixedWidth(20);
                grid->addWidget(glyph, 0, col++);
            }

            rw.text = new QLineEdit(item.text, row);
            rw.text->setClearButtonEnabled(true);
            grid->addWidget(rw.text, 0, col++, 1, isActions ? 1 : 2);

            if (isActions) {
                rw.owner = new QLineEdit(item.owner, row);
                rw.owner->setPlaceholderText(tr("@owner"));
                rw.owner->setMaximumWidth(110);
                grid->addWidget(rw.owner, 0, col++);

                // v0.1.98 — real calendar+time picker (was a free-text
                // "YYYY-MM-DDTHH:MM" line edit). This is both the displayed
                // due date AND, when "Remind" is checked, the reminder time
                // the central Reminders root schedules. Defaults to tomorrow
                // 9am when the AI didn't extract a concrete due.
                const QDateTime initDue = item.dueAt.isValid()
                    ? item.dueAt.toLocalTime()
                    : QDateTime(QDate::currentDate().addDays(1), QTime(9, 0));
                rw.remindAt = new QDateTimeEdit(initDue, row);
                rw.remindAt->setCalendarPopup(true);
                rw.remindAt->setDisplayFormat(QStringLiteral("MMM d  HH:mm"));
                rw.remindAt->setToolTip(
                    tr("Date + time for this action — used as the reminder "
                       "when “Remind” is checked."));
                rw.remindAt->setMaximumWidth(150);
                grid->addWidget(rw.remindAt, 0, col++);
            }

            rv->addWidget(row);
            rowsOut->push_back(rw);
        }
    }
    cv->addWidget(rowsHost);

    QObject::connect(toggleBtn, &QToolButton::toggled, this,
                     [rowsHost, toggleBtn](bool on) {
                         rowsHost->setVisible(on);
                         toggleBtn->setArrowType(on ? Qt::DownArrow : Qt::RightArrow);
                     });

    // Refresh the counts whenever the action toggle changes so the
    // header line "scheduling N reminders" stays current.
    for (const RowWidgets &rw : *rowsOut) {
        if (rw.remind) {
            QObject::connect(rw.remind, &QCheckBox::toggled, this,
                             [this](bool){ refreshFooterStatus(); });
        }
    }

    return card;
}

// ═══════════════════════════════════════════════════════════════════════
// Footer — status text + Discard / Save buttons
// ═══════════════════════════════════════════════════════════════════════

void NoterSweepDialog::buildFooter() {
    auto *footer = new QWidget(this);
    auto *fh = new QHBoxLayout(footer);
    fh->setContentsMargins(0, 4, 0, 0);
    fh->setSpacing(8);

    m_footerStatus = new QLabel(footer);
    {
        QFont f = m_footerStatus->font();
        f.setPointSizeF(f.pointSizeF() * 0.92);
        m_footerStatus->setFont(f);
        QPalette p = m_footerStatus->palette();
        QColor c = p.color(QPalette::WindowText);
        c.setAlpha(150);
        p.setColor(QPalette::WindowText, c);
        m_footerStatus->setPalette(p);
    }

    m_discardBtn = new QPushButton(tr("Discard"), footer);
    m_discardBtn->setIcon(style()->standardIcon(QStyle::SP_DialogCancelButton));

    m_saveBtn = new QPushButton(tr("Save · schedule"), footer);
    m_saveBtn->setIcon(style()->standardIcon(QStyle::SP_DialogOkButton));
    m_saveBtn->setDefault(true);

    fh->addWidget(m_footerStatus, 1);
    fh->addWidget(m_discardBtn);
    fh->addWidget(m_saveBtn);
    m_outerLayout->addWidget(footer);

    connect(m_discardBtn, &QPushButton::clicked, this, [this]() {
        emit discardRequested();
        reject();
    });
    connect(m_saveBtn, &QPushButton::clicked, this, [this]() {
        emit saveRequested();
        accept();
    });
}

// ═══════════════════════════════════════════════════════════════════════
// Live-update helpers
// ═══════════════════════════════════════════════════════════════════════

void NoterSweepDialog::refreshCounts() {
    if (!m_countsLabel) return;
    const int d = m_result.decisions.size();
    const int a = m_result.actions.size();
    const int q = m_result.questions.size();
    const int r = m_result.risks.size();
    m_countsLabel->setText(
        tr("%1 decisions · %2 actions · %3 questions · %4 risks")
            .arg(d).arg(a).arg(q).arg(r));
}

void NoterSweepDialog::refreshFooterStatus() {
    if (!m_footerStatus) return;
    int reminders = 0;
    for (const RowWidgets &rw : m_actionRows) {
        if (rw.remind && rw.remind->isChecked()) ++reminders;
    }
    QString s;
    if (reminders > 0) {
        s = tr("scheduling %1 reminder%2")
                .arg(reminders).arg(reminders == 1 ? "" : "s");
    } else {
        s = tr("no reminders");
    }
    if (!m_targetPath.isEmpty()) {
        s += tr(" · writing to %1").arg(m_targetPath);
    }
    m_footerStatus->setText(s);
}

// ═══════════════════════════════════════════════════════════════════════
// Result accessors
// ═══════════════════════════════════════════════════════════════════════

QVector<NoterSweepPrompt::SweepResult::Item> NoterSweepDialog::reminderItems() const {
    QVector<NoterSweepPrompt::SweepResult::Item> out;
    if (result() != QDialog::Accepted) return out;  // user discarded

    // Walk the edited action rows so the caller schedules the
    // CURRENT (possibly user-edited) text / owner / due — not the
    // original LLM output.
    for (int i = 0; i < m_actionRows.size() && i < m_result.actions.size(); ++i) {
        const RowWidgets &rw = m_actionRows[i];
        if (!rw.remind || !rw.remind->isChecked()) continue;
        NoterSweepPrompt::SweepResult::Item it = m_result.actions[i];
        if (rw.text)     it.text  = rw.text->text().trimmed();
        if (rw.owner)    it.owner = rw.owner->text().trimmed();
        if (rw.remindAt) it.dueAt = rw.remindAt->dateTime();
        if (!it.text.isEmpty()) out.push_back(it);
    }
    return out;
}

NoterSweepPrompt::SweepResult NoterSweepDialog::finalResult() const {
    NoterSweepPrompt::SweepResult out = m_result;

    auto pullSection = [](QVector<NoterSweepPrompt::SweepResult::Item> &dst,
                          const QVector<RowWidgets> &rows,
                          bool isActions) {
        for (int i = 0; i < rows.size() && i < dst.size(); ++i) {
            const RowWidgets &rw = rows[i];
            if (rw.text) dst[i].text = rw.text->text().trimmed();
            if (isActions) {
                if (rw.owner)    dst[i].owner = rw.owner->text().trimmed();
                if (rw.remindAt) dst[i].dueAt = rw.remindAt->dateTime();
            }
        }
        // Drop rows the user blanked out.
        dst.erase(std::remove_if(dst.begin(), dst.end(),
                                 [](const NoterSweepPrompt::SweepResult::Item &it){
                                     return it.text.isEmpty();
                                 }),
                  dst.end());
    };

    pullSection(out.decisions, m_decisionRows, /*isActions=*/false);
    pullSection(out.actions,   m_actionRows,   /*isActions=*/true);
    pullSection(out.questions, m_questionRows, /*isActions=*/false);
    pullSection(out.risks,     m_riskRows,     /*isActions=*/false);
    return out;
}

void NoterSweepDialog::setExistingReminders(
        const QVector<QPair<QString, QDateTime>> &existing) {
    m_existing = existing;

    // Header line: what's already scheduled for this note.
    if (m_existingLabel) {
        if (existing.isEmpty()) {
            m_existingLabel->setVisible(false);
        } else {
            QStringList lines;
            for (const auto &e : existing)
                lines << QStringLiteral("• %1 — %2").arg(
                    e.first,
                    e.second.toLocalTime().toString(QStringLiteral("MMM d HH:mm")));
            m_existingLabel->setText(
                tr("Already scheduled for this note:") + QStringLiteral("\n") +
                lines.join(QLatin1Char('\n')));
            m_existingLabel->setVisible(true);
        }
    }

    // Default-uncheck any action row that fuzzy-matches an existing reminder so
    // re-running Extract doesn't duplicate. The user can re-check deliberately.
    for (int i = 0; i < m_actionRows.size() && i < m_result.actions.size(); ++i) {
        if (!m_actionRows[i].remind) continue;
        const QString aNorm = NoterSweepPrompt::normalizeForMatch(m_result.actions[i].text);
        if (aNorm.size() < 4) continue;
        bool matched = false;
        for (const auto &e : existing) {
            const QString eNorm = NoterSweepPrompt::normalizeForMatch(e.first);
            if (eNorm.isEmpty()) continue;
            if (eNorm.contains(aNorm) || aNorm.contains(eNorm)) { matched = true; break; }
        }
        if (matched) {
            m_actionRows[i].remind->setChecked(false);
            m_actionRows[i].remind->setText(tr("Remind (already scheduled)"));
            m_actionRows[i].remind->setToolTip(
                tr("This action looks already scheduled for this note. "
                   "Check it to add another reminder anyway."));
        }
    }
    refreshFooterStatus();
}
