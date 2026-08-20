// SPDX-License-Identifier: GPL-3.0-or-later

#include "passwordgen.h"
#include "config.h"
#include "themes.h"
#include "fonts.h"

#include <QApplication>
#include <QAbstractButton>
#include <QCheckBox>
#include <QClipboard>
#include <QComboBox>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPainter>
#include <QFontMetrics>
#include <QScrollBar>
#include <QPlainTextEdit>
#include <QTextDocument>
#include <QPushButton>
#include <QRadioButton>
#include <QSlider>
#include <QSpinBox>
#include <QStyle>
#include <QTimer>
#include <QVBoxLayout>

#include <cmath>

using namespace PasswordGen;

// How long a copied password stays on the clipboard before we take it
// back. Only cleared if the clipboard still holds OUR value — if the
// user copied something else in the meantime we leave theirs alone.
static constexpr int kClipboardClearMs = 30000;

namespace {

// Owned by the application, not the panel. A timer parented to the panel
// dies with the tab, so closing the tab inside the 30 s window used to
// strand the password on the clipboard forever — the one case where the
// promise printed under the buttons was false.
void armClipboardWipe(const QString &owned) {
    if (owned.isEmpty() || !qApp) return;
    auto *t = new QTimer(qApp);
    t->setSingleShot(true);
    QObject::connect(t, &QTimer::timeout, qApp, [owned, t]() {
        if (QClipboard *cb = QApplication::clipboard()) {
            if (cb->text() == owned) cb->clear();
        }
        t->deleteLater();
    });
    t->start(kClipboardClearMs);
}

}  // namespace

// ── StrengthBar ────────────────────────────────────────────────────────

StrengthBar::StrengthBar(QWidget *parent) : QWidget(parent) {
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
}

QSize StrengthBar::sizeHint() const { return QSize(160, 8); }

void StrengthBar::setEmptyColour(const QColor &c) {
    if (m_empty == c) return;
    m_empty = c;
    update();
}

void StrengthBar::setScore(int score) {
    if (m_score == score) return;
    m_score = score;
    update();
}

void StrengthBar::paintEvent(QPaintEvent *) {
    // Hues chosen to stay legible on both a white and a near-black
    // background — no theme branch needed.
    static const QColor kColours[5] = {
        QColor("#DC2626"), QColor("#EA580C"), QColor("#CA8A04"),
        QColor("#16A34A"), QColor("#0891B2"),
    };
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    p.setPen(Qt::NoPen);

    const int segments = 5;
    const qreal gap = 4.0;
    const qreal w = (width() - gap * (segments - 1)) / segments;
    // Not palette(): a stylesheet on the parent does not move QPalette,
    // so an unfilled segment would keep the light-theme grey on dark.
    const QColor empty = m_empty.isValid() ? m_empty : palette().color(QPalette::Mid);

    for (int i = 0; i < segments; ++i) {
        const QRectF r(i * (w + gap), 0, w, height());
        p.setBrush(i <= m_score ? kColours[qBound(0, m_score, 4)] : empty);
        p.drawRoundedRect(r, height() / 2.0, height() / 2.0);
    }
}

// ── PasswordGenPanel ───────────────────────────────────────────────────

PasswordGenPanel::PasswordGenPanel(QWidget *parent) : QWidget(parent) {
    setObjectName(QStringLiteral("passwordGenPanel"));

    // Controls sit in a fixed-width column: stretched across a 1500 px
    // window the length slider and the readout become unreadable.
    auto *outer = new QHBoxLayout(this);
    outer->setContentsMargins(16, 14, 16, 14);
    auto *page = new QWidget;
    page->setMinimumWidth(660);
    page->setMaximumWidth(920);
    outer->addWidget(page, 0, Qt::AlignTop);
    outer->addStretch(1);

    auto *root = new QVBoxLayout(page);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(10);

    // ── Readout ────────────────────────────────────────────────────
    m_out = new QPlainTextEdit;
    m_out->setReadOnly(true);
    m_out->setLineWrapMode(QPlainTextEdit::NoWrap);
    m_out->setFont(notepatraCodeFont(13));
    m_out->setPlaceholderText(tr("Press Generate."));
    m_out->setTextInteractionFlags(Qt::TextSelectableByMouse | Qt::TextSelectableByKeyboard);
    // A blinking caret in front of the value reads as a literal '|'
    // character in the password. Nothing here is typed into.
    m_out->setCursorWidth(0);
    m_out->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    m_out->setMinimumWidth(520);

    m_copyBtn = new QPushButton(tr("Copy"));
    m_copyBtn->setIcon(style()->standardIcon(QStyle::SP_DialogSaveButton));
    m_copyBtn->setToolTip(tr("Copy to the clipboard. It is cleared again after "
                             "30 seconds unless you copy something else first."));

    auto *outRow = new QHBoxLayout;
    outRow->setSpacing(8);
    outRow->addWidget(m_out, 1);
    // Top, not centre: with a batch of 20 the button would otherwise drift
    // down the side of the list, away from the thing it acts on.
    outRow->addWidget(m_copyBtn, 0, Qt::AlignTop);
    root->addLayout(outRow);
    fitReadoutHeight(1);

    auto *meterRow = new QHBoxLayout;
    m_bar = new StrengthBar;
    m_bar->setFixedHeight(8);
    m_bar->setFixedWidth(220);
    m_entropy = new QLabel;
    m_entropy->setTextInteractionFlags(Qt::TextSelectableByMouse);
    meterRow->addWidget(m_bar, 0);
    meterRow->addWidget(m_entropy, 0);
    meterRow->addStretch(1);
    root->addLayout(meterRow);

    // ── Mode ───────────────────────────────────────────────────────
    auto *modeRow = new QHBoxLayout;
    m_modeChars  = new QRadioButton(tr("Random characters"));
    m_modePhrase = new QRadioButton(tr("Passphrase"));
    m_modeChars->setChecked(true);
    modeRow->addWidget(m_modeChars);
    modeRow->addWidget(m_modePhrase);
    modeRow->addStretch(1);
    modeRow->addWidget(new QLabel(tr("How many:")));
    m_count = new QSpinBox;
    m_count->setRange(1, kMaxCount);
    m_count->setValue(1);
    m_count->setToolTip(tr("Generate this many independent passwords at once."));
    connect(m_count, QOverload<int>::of(&QSpinBox::valueChanged),
            this, &PasswordGenPanel::regenerate);
    modeRow->addWidget(m_count);
    root->addLayout(modeRow);

    m_charsGroup  = buildCharactersGroup();
    m_phraseGroup = buildPassphraseGroup();
    root->addWidget(m_charsGroup);
    root->addWidget(m_phraseGroup);

    // ── Actions ────────────────────────────────────────────────────
    auto *btnRow = new QHBoxLayout;
    auto *genBtn = new QPushButton(tr("Generate"));
    genBtn->setDefault(true);
    genBtn->setIcon(style()->standardIcon(QStyle::SP_BrowserReload));
    m_insertBtn = new QPushButton(tr("Insert into editor"));
    m_insertBtn->setToolTip(tr("Insert at the caret of the editor tab you were last in.\n\n"
                               "Once it is in an editor buffer it is no longer covered by "
                               "this panel's guarantees — an unsaved buffer is written to "
                               "the session file, and its contents reach the AI context."));
    m_newTabBtn = new QPushButton(tr("Open in new tab"));
    m_newTabBtn->setToolTip(tr("Open the whole batch as a new untitled editor tab.\n\n"
                               "Once it is in an editor buffer it is no longer covered by "
                               "this panel's guarantees — an unsaved buffer is written to "
                               "the session file, and its contents reach the AI context."));
    m_newTabBtn->setIcon(style()->standardIcon(QStyle::SP_FileIcon));
    btnRow->addWidget(genBtn);
    btnRow->addWidget(m_insertBtn);
    btnRow->addWidget(m_newTabBtn);
    btnRow->addStretch(1);
    root->addLayout(btnRow);

    m_status = new QLabel;
    m_status->setWordWrap(true);
    root->addWidget(m_status);
    root->addStretch(1);

    // G — Ctrl+C or right-click > Copy inside the readout never reaches
    // copyToClipboard(), so watch the clipboard itself and arm the wipe
    // for any value that is the one we are displaying.
    if (QClipboard *cb = QApplication::clipboard()) {
        connect(cb, &QClipboard::dataChanged, this, [this]() {
            QClipboard *c = QApplication::clipboard();
            if (!c) return;
            const QString t = c->text();
            if (t.isEmpty() || t == m_clipboardOwned) return;
            if (t != currentText()) return;   // the user copied something else
            m_clipboardOwned = t;
            armClipboardWipe(t);
        });
    }

    connect(genBtn,      &QPushButton::clicked, this, &PasswordGenPanel::regenerate);
    connect(m_copyBtn,   &QPushButton::clicked, this, &PasswordGenPanel::copyToClipboard);
    connect(m_insertBtn, &QPushButton::clicked, this, [this]() {
        const QString t = currentText();
        if (!t.isEmpty()) emit insertRequested(t);
    });
    connect(m_newTabBtn, &QPushButton::clicked, this, [this]() {
        const QString t = currentText();
        if (!t.isEmpty()) emit newTabRequested(t);
    });

    connect(m_modeChars, &QRadioButton::toggled, this, &PasswordGenPanel::syncEnabledState);

    applyTheme();
    syncEnabledState();
    regenerate();
}

QWidget *PasswordGenPanel::buildCharactersGroup() {
    auto *box = new QGroupBox(tr("Characters"));
    auto *v = new QVBoxLayout(box);
    // Match the frame inset by hand: QSS padding on a QGroupBox does
    // not move the layout, so the last row draws over the border.
    v->setContentsMargins(12, 14, 12, 12);

    auto *lenRow = new QHBoxLayout;
    lenRow->addWidget(new QLabel(tr("Length")));
    m_length = new QSlider(Qt::Horizontal);
    m_length->setRange(kMinLength, kMaxLength);
    m_length->setValue(20);
    m_lengthBox = new QSpinBox;
    m_lengthBox->setRange(kMinLength, kMaxLength);
    m_lengthBox->setValue(20);
    connect(m_length,    QOverload<int>::of(&QSlider::valueChanged),
            m_lengthBox, &QSpinBox::setValue);
    connect(m_lengthBox, QOverload<int>::of(&QSpinBox::valueChanged),
            m_length,    &QSlider::setValue);
    lenRow->addWidget(m_length, 1);
    lenRow->addWidget(m_lengthBox);
    v->addLayout(lenRow);

    auto *classRow = new QHBoxLayout;
    m_lower   = new QCheckBox(tr("a-z"));
    m_upper   = new QCheckBox(tr("A-Z"));
    m_digits  = new QCheckBox(tr("0-9"));
    m_symbols = new QCheckBox(tr("Symbols"));
    for (QCheckBox *c : { m_lower, m_upper, m_digits, m_symbols }) {
        c->setChecked(true);
        classRow->addWidget(c);
    }
    m_symbols->setToolTip(
        tr("%1\n\nQuotes, backslash, backtick and pipe are left out so a "
           "password survives being pasted into a shell command, a YAML "
           "file or a connection string.")
            .arg(classChars(Symbols, false)));
    classRow->addWidget(new QLabel(tr("Also:")));
    m_extra = new QLineEdit;
    m_extra->setPlaceholderText(tr("extra characters"));
    m_extra->setMaximumWidth(160);
    m_extra->setToolTip(tr("Any additional characters to draw from. Whitespace, "
                           "control and invisible formatting characters are "
                           "ignored, as is anything outside the Basic "
                           "Multilingual Plane."));
    classRow->addWidget(m_extra);
    classRow->addStretch(1);
    v->addLayout(classRow);

    auto *optRow = new QHBoxLayout;
    m_noLookalikes = new QCheckBox(tr("Exclude look-alikes (0 O 1 l I)"));
    m_requireEach  = new QCheckBox(tr("At least one from each set"));
    m_requireEach->setChecked(true);
    m_requireEach->setToolTip(
        tr("Guarantees every selected set appears. This narrows the set of "
           "possible passwords, so the entropy shown above drops slightly "
           "when it is on — that is correct, not a display bug."));
    optRow->addWidget(m_noLookalikes);
    optRow->addWidget(m_requireEach);
    optRow->addStretch(1);
    v->addLayout(optRow);

    for (QCheckBox *c : { m_lower, m_upper, m_digits, m_symbols,
                          m_noLookalikes, m_requireEach })
        connect(c, &QCheckBox::toggled, this, &PasswordGenPanel::regenerate);
    connect(m_extra,     &QLineEdit::textChanged, this, &PasswordGenPanel::regenerate);
    connect(m_lengthBox, QOverload<int>::of(&QSpinBox::valueChanged),
            this, &PasswordGenPanel::regenerate);
    return box;
}

QWidget *PasswordGenPanel::buildPassphraseGroup() {
    auto *box = new QGroupBox(tr("Passphrase"));
    auto *v = new QVBoxLayout(box);
    // Match the frame inset by hand: QSS padding on a QGroupBox does
    // not move the layout, so the last row draws over the border.
    v->setContentsMargins(12, 14, 12, 12);

    auto *row = new QHBoxLayout;
    row->addWidget(new QLabel(tr("Words")));
    m_words = new QSpinBox;
    m_words->setRange(kMinWords, kMaxWords);
    m_words->setValue(6);
    row->addWidget(m_words);

    row->addSpacing(12);
    row->addWidget(new QLabel(tr("Separator")));
    m_separator = new QComboBox;
    m_separator->addItem(tr("hyphen  -"), QStringLiteral("-"));
    m_separator->addItem(tr("period  ."), QStringLiteral("."));
    m_separator->addItem(tr("underscore  _"), QStringLiteral("_"));
    m_separator->addItem(tr("space"), QStringLiteral(" "));
    m_separator->addItem(tr("none"), QString());
    row->addWidget(m_separator);
    row->addStretch(1);
    v->addLayout(row);

    auto *opt = new QHBoxLayout;
    m_capitalise   = new QCheckBox(tr("Capitalise each word"));
    m_appendDigits = new QCheckBox(tr("Append two digits"));
    opt->addWidget(m_capitalise);
    opt->addWidget(m_appendDigits);
    opt->addStretch(1);
    v->addLayout(opt);

    auto *note = new QLabel(
        tr("%1 words in the built-in list — %2 bits per word. The list ships "
           "with Notepatra; no dictionary file is read at runtime.")
            .arg(wordlistSize())
            .arg(qRound(std::log2(double(wordlistSize())))));
    note->setWordWrap(true);
    v->addWidget(note);

    connect(m_words, QOverload<int>::of(&QSpinBox::valueChanged),
            this, &PasswordGenPanel::regenerate);
    connect(m_separator, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &PasswordGenPanel::regenerate);
    for (QCheckBox *c : { m_capitalise, m_appendDigits })
        connect(c, &QCheckBox::toggled, this, &PasswordGenPanel::regenerate);
    return box;
}

void PasswordGenPanel::syncEnabledState() {
    const bool chars = m_modeChars->isChecked();
    m_charsGroup->setVisible(chars);
    m_phraseGroup->setVisible(!chars);
    refreshReadout();
}

Options PasswordGenPanel::collectOptions() const {
    Options o;
    o.mode = m_modeChars->isChecked() ? Mode::Characters : Mode::Passphrase;

    o.length = m_lengthBox->value();
    o.classes = 0;
    if (m_lower->isChecked())   o.classes |= Lower;
    if (m_upper->isChecked())   o.classes |= Upper;
    if (m_digits->isChecked())  o.classes |= Digits;
    if (m_symbols->isChecked()) o.classes |= Symbols;
    o.extra = m_extra->text();
    o.excludeLookalikes = m_noLookalikes->isChecked();
    o.requireEachClass  = m_requireEach->isChecked();

    o.words = m_words->value();
    const QString sep = m_separator->currentData().toString();
    o.separator = sep.isEmpty() ? QChar() : sep.at(0);
    o.capitalise   = m_capitalise->isChecked();
    o.appendDigits = m_appendDigits->isChecked();
    return o;
}

void PasswordGenPanel::refreshReadout() {
    const Options o = collectOptions();
    const QString problem = validate(o);

    if (!problem.isEmpty()) {
        updateActions();
        m_bar->setScore(-1);
        m_entropy->setText(QString());
        m_status->setText(problem);
        return;
    }
    updateActions();

    const double bits = entropyBits(o);
    const Strength s = strength(bits);
    m_bar->setScore(s.score);
    m_entropy->setText(tr("%1 bits · %2").arg(qRound(bits)).arg(s.label));

    if (o.mode == Mode::Characters) {
        m_status->setText(tr("Drawing from %1 characters. This panel writes nothing "
                             "to disk; a copied password is taken back off the "
                             "clipboard after %2 seconds, though a clipboard-history "
                             "tool may still keep its own copy.")
                              .arg(alphabet(o).size())
                              .arg(kClipboardClearMs / 1000));
    } else {
        m_status->setText(tr("This panel writes nothing to disk; a copied passphrase "
                             "is taken back off the clipboard after %1 seconds, though "
                             "a clipboard-history tool may still keep its own copy.")
                              .arg(kClipboardClearMs / 1000));
    }
}

// Acting on a value requires a value: the settings being valid is not
// enough, because a failed generate leaves the readout empty.
void PasswordGenPanel::updateActions() {
    const bool ready = validate(collectOptions()).isEmpty() && !currentText().isEmpty();
    m_copyBtn->setEnabled(ready);
    m_insertBtn->setEnabled(ready);
    m_newTabBtn->setEnabled(ready);
}

void PasswordGenPanel::regenerate() {
    refreshReadout();
    const Options o = collectOptions();
    if (!validate(o).isEmpty()) {
        m_out->clear();
        fitReadoutHeight(1);
        updateActions();
        return;
    }
    const QStringList batch = generateMany(o, m_count->value());
    if (batch.isEmpty()) {
        m_out->clear();
        fitReadoutHeight(1);
        m_status->setText(tr("Could not generate with these settings."));
        updateActions();
        return;
    }
    m_out->setPlainText(batch.join(QLatin1Char('\n')));
    fitReadoutHeight(batch.size());
    updateActions();
}

// One line looks like a field; a hundred should scroll rather than push
// the controls off the bottom of the tab.
void PasswordGenPanel::fitReadoutHeight(int lines) {
    const int rows = qBound(1, lines, 8);
    const QFontMetrics fm(m_out->font());
    const int chrome = m_out->frameWidth() * 2 +
                       int(m_out->document()->documentMargin()) * 2 + 6;
    int h = rows * fm.lineSpacing() + chrome;

    // Values never wrap — a wrapped password is ambiguous about whether the
    // break is a character. So a long line scrolls sideways instead, and the
    // scrollbar needs its own room or it covers the last value.
    int longest = 0;
    const QStringList shown = m_out->toPlainText().split(QLatin1Char('\n'));
    for (const QString &line : shown)
        longest = qMax(longest, fm.horizontalAdvance(line));
    const int viewport = m_out->viewport()->width() > 10 ? m_out->viewport()->width()
                                                         : m_out->minimumWidth();
    if (longest > viewport - 8)
        h += m_out->horizontalScrollBar()->sizeHint().height();

    m_out->setFixedHeight(h);
}

QString PasswordGenPanel::currentText() const {
    return m_out->toPlainText();
}

namespace {

bool passwordGenIsDark() {
    // Config::theme defaults to "System" and is never rewritten, so the
    // raw string has to go through resolveTheme() first — comparing it
    // directly leaves the panel light on a dark OS. fmtpanel.cpp and
    // sqlfmtpanel.cpp carry the same note; this is a known trap.
    const QString t = resolveTheme(Config::instance().theme).name;
    return t.compare(QStringLiteral("Dark"), Qt::CaseInsensitive) == 0 ||
           t.compare(QStringLiteral("Monokai"), Qt::CaseInsensitive) == 0;
}

struct PwPalette { QString bg, card, input, border, accent, text, sub, muted; };

PwPalette passwordGenPalette() {
    if (passwordGenIsDark())
        return { "#1E1E1E", "#252526", "#2D2D2F", "#3E3E42",
                 "#CC785C", "#E8E6E3", "#B8B5B1", "#6C6C6C" };
    return { "#FAF9F5", "#FFFFFF", "#FFFFFF", "#D4D1C4",
             "#CC785C", "#141413", "#54524E", "#8E8C88" };
}

}  // namespace

void PasswordGenPanel::applyTheme() {
    const PwPalette p = passwordGenPalette();
    m_bar->setEmptyColour(QColor(p.border));
    setStyleSheet(QStringLiteral(
        "#passwordGenPanel { background: %1; }"
        "QLabel { color: %6; background: transparent; }"
        "QCheckBox, QRadioButton { color: %6; background: transparent; spacing: 7px; }"
        "QCheckBox::indicator, QRadioButton::indicator { width: 14px; height: 14px; }"
        "QCheckBox::indicator { border: 1px solid %4; border-radius: 3px; background: %3; }"
        "QCheckBox::indicator:checked { background: %5; border: 1px solid %5; }"
        "QCheckBox::indicator:hover { border: 1px solid %5; }"
        "QRadioButton::indicator { border: 1px solid %4; border-radius: 8px; background: %3; }"
        "QRadioButton::indicator:checked { border: 4px solid %5; background: %3; }"
        "QRadioButton::indicator:hover { border-color: %5; }"
        "QGroupBox { color: %7; background: %2; border: 1px solid %4;"
        "            border-radius: 8px; margin-top: 10px; }"
        "QGroupBox::title { subcontrol-origin: margin; left: 10px; padding: 0 4px; color: %7; }"
        "QPlainTextEdit { background: %3; color: %6; border: 1px solid %4;"
        "                 border-radius: 6px; padding: 6px 8px;"
        "                 selection-background-color: %5; selection-color: #FFFFFF; }"
        "QLineEdit, QSpinBox, QComboBox { background: %3; color: %6; border: 1px solid %4;"
        "                                 border-radius: 5px; padding: 3px 6px; }"
        "QComboBox QAbstractItemView { background: %2; color: %6; border: 1px solid %4;"
        "                              selection-background-color: %5; selection-color: #FFFFFF; }"
        "QPushButton { background: %2; color: %6; border: 1px solid %4;"
        "              border-radius: 6px; padding: 5px 12px; }"
        "QPushButton:hover { border-color: %5; }"
        "QPushButton:default { border: 1px solid %5; }"
        "QPushButton:disabled { color: %8; border-color: %4; }"
        "QSpinBox::up-button, QSpinBox::down-button { subcontrol-origin: border;"
        "    width: 16px; background: transparent; border-left: 1px solid %4; }"
        "QSpinBox::up-button { subcontrol-position: top right; }"
        "QSpinBox::down-button { subcontrol-position: bottom right; }"
        "QSpinBox::up-arrow { width: 0; height: 0; border-left: 4px solid transparent;"
        "    border-right: 4px solid transparent; border-bottom: 5px solid %6; }"
        "QSpinBox::down-arrow { width: 0; height: 0; border-left: 4px solid transparent;"
        "    border-right: 4px solid transparent; border-top: 5px solid %6; }"
        "QSpinBox::up-arrow:disabled, QSpinBox::down-arrow:disabled { border-bottom-color: %8;"
        "    border-top-color: %8; }"
        "QComboBox::drop-down { subcontrol-origin: padding; subcontrol-position: top right;"
        "    width: 18px; border-left: 1px solid %4; }"
        "QComboBox::down-arrow { width: 0; height: 0; border-left: 4px solid transparent;"
        "    border-right: 4px solid transparent; border-top: 5px solid %6; }"
        "QSlider::groove:horizontal { height: 4px; background: %4; border-radius: 2px; }"
        "QSlider::handle:horizontal { background: %5; width: 14px; margin: -6px 0;"
        "                             border-radius: 7px; }"
    ).arg(p.bg, p.card, p.input, p.border, p.accent, p.text, p.sub, p.muted));
    m_status->setStyleSheet(QStringLiteral("color: %1; background: transparent;").arg(p.muted));
    m_entropy->setStyleSheet(QStringLiteral("color: %1; background: transparent;").arg(p.sub));

    // Styling ::indicator changes its width, but QCheckBox/QRadioButton
    // compute sizeHint() from the STYLE metric, not the stylesheet — so the
    // longest labels come up a few pixels short and clip. Re-polish and take
    // the width the widget now actually needs.
    for (QAbstractButton *b : findChildren<QAbstractButton *>()) {
        if (!qobject_cast<QCheckBox *>(b) && !qobject_cast<QRadioButton *>(b)) continue;
        b->ensurePolished();
        b->setMinimumWidth(b->sizeHint().width() + 6);
    }
}

void PasswordGenPanel::onThemeChanged() { applyTheme(); }

void PasswordGenPanel::copyToClipboard() {
    const QString t = currentText();
    if (t.isEmpty()) return;
    QClipboard *cb = QApplication::clipboard();
    if (!cb) return;
    // Set before the clipboard so the dataChanged watcher recognises it
    // as already armed rather than arming a second wipe.
    m_clipboardOwned = t;
    cb->setText(t);
    armClipboardWipe(t);
    emit statusMessage(tr("Copied — clipboard clears in %1 seconds.")
                           .arg(kClipboardClearMs / 1000));
}

void PasswordGenPanel::clearClipboardIfUnchanged() {
    QClipboard *cb = QApplication::clipboard();
    if (!cb || m_clipboardOwned.isEmpty()) return;
    if (cb->text() == m_clipboardOwned) {
        cb->clear();
        emit statusMessage(tr("Clipboard cleared."));
    }
    m_clipboardOwned.clear();
}
