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
#include <QDir>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QFutureWatcher>
#include <QKeyEvent>
#include <QPointer>
#include <QStackedWidget>
#include <QtConcurrent/QtConcurrentRun>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QAbstractItemView>
#include <QPainter>
#include <QPalette>
#include <QFontMetrics>
#include <QScrollArea>
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

#include <notepad_core.h>

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

// Runs on a pool thread. Copies every field out and frees the Rust result
// before returning, so a tab closed mid-generation cannot leak it.
SshKeyOut runSshKeygen(int alg, int bits, QByteArray comment, QByteArray passphrase) {
    SshKeyOut out;
    SshKeyResult r = npc_ssh_keygen(alg, bits,
                                    comment.isEmpty() ? nullptr : comment.constData(),
                                    passphrase.isEmpty() ? nullptr : passphrase.constData());
    if (r.ok == 1 && r.private_pem && r.public_line) {
        out.ok = true;
        out.priv = QString::fromUtf8(r.private_pem, int(r.private_len));
        out.pub  = QString::fromUtf8(r.public_line, int(r.public_len));
        if (r.fingerprint) out.fp = QString::fromUtf8(r.fingerprint, int(r.fingerprint_len));
    } else if (r.error_msg) {
        out.err = QString::fromUtf8(r.error_msg);
    }
    // error_msg is a CString and npc_free_ssh_key deliberately leaves it
    // alone; the key buffers are that call's job and only that call's.
    if (r.error_msg) { npc_free_string(r.error_msg); r.error_msg = nullptr; }
    npc_free_ssh_key(r);
    return out;
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

    // The rail runs the full height of the tab; the pages sit in a
    // fixed-width column, because stretched across a 1500 px window the
    // length slider and the readout become unreadable.
    auto *outer = new QHBoxLayout(this);
    outer->setContentsMargins(0, 0, 0, 0);
    outer->setSpacing(0);

    m_rail = buildRail();
    outer->addWidget(m_rail);

    auto *rightWrap = new QWidget;
    auto *rightLay = new QHBoxLayout(rightWrap);
    rightLay->setContentsMargins(16, 14, 16, 14);
    auto *page = new QWidget;
    page->setMinimumWidth(660);
    page->setMaximumWidth(920);
    rightLay->addWidget(page, 0, Qt::AlignTop);
    rightLay->addStretch(1);

    // An SSH key with its private half revealed is taller than the tab, and
    // without this the buttons draw on top of the key text.
    auto *scroll = new QScrollArea;
    scroll->setObjectName(QStringLiteral("pwScroll"));
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setWidget(rightWrap);
    // The viewport fills its own background from the app palette, which
    // would paint a light slab over the panel's themed one.
    scroll->viewport()->setAutoFillBackground(false);
    rightWrap->setAutoFillBackground(false);
    outer->addWidget(scroll, 1);

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
    m_out->setToolTip(tr("The generated value or values, one per line.\n\n"
                         "Read-only. Values never wrap: a wrapped password is "
                         "ambiguous about whether the break is a character, so a "
                         "long one scrolls sideways instead."));
    m_out->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    m_out->setMinimumWidth(520);

    m_copyBtn = new QPushButton(tr("Copy"));
    m_copyBtn->setIcon(style()->standardIcon(QStyle::SP_DialogSaveButton));
    m_copyBtn->setToolTip(tr("Copy to the clipboard. It is cleared again after "
                             "30 seconds unless you copy something else first."));

    // Readout and meter belong to the two password pages; the SSH page
    // has its own results block, so this whole strip hides there.
    m_valueArea = new QWidget;
    auto *valueLay = new QVBoxLayout(m_valueArea);
    valueLay->setContentsMargins(0, 0, 0, 0);
    valueLay->setSpacing(10);
    root->addWidget(m_valueArea);

    auto *outRow = new QHBoxLayout;
    outRow->setSpacing(8);
    outRow->addWidget(m_out, 1);
    // Top, not centre: with a batch of 20 the button would otherwise drift
    // down the side of the list, away from the thing it acts on.
    outRow->addWidget(m_copyBtn, 0, Qt::AlignTop);
    valueLay->addLayout(outRow);
    fitReadoutHeight(1);

    auto *meterRow = new QHBoxLayout;
    m_bar = new StrengthBar;
    m_bar->setFixedHeight(8);
    m_bar->setFixedWidth(220);
    m_entropy = new QLabel;
    m_entropy->setTextInteractionFlags(Qt::TextSelectableByMouse);
    const QString bitsTip =
        tr("How many bits of entropy these settings produce — the base-2 "
           "logarithm of the number of distinct values they can generate.\n\n"
           "This is a count, not a score. It does not look at the password on "
           "screen and guess how complicated it seems; it measures the size of "
           "the space it was drawn from.\n\n"
           "Bands: under 40 very weak · 40-59 weak · 60-79 fair · "
           "80-111 strong · 112+ excellent.");
    m_entropy->setToolTip(bitsTip);
    m_bar->setToolTip(bitsTip);
    meterRow->addWidget(m_bar, 0);
    meterRow->addWidget(m_entropy, 0);
    meterRow->addStretch(1);
    valueLay->addLayout(meterRow);

    // ── Pages ──────────────────────────────────────────────────────
    const QString countTip = tr("Generate this many independent passwords at once.");
    m_count = new QSpinBox;
    m_count->setRange(1, kMaxCount);
    m_count->setValue(1);
    m_count->setToolTip(countTip);
    m_count2 = new QSpinBox;
    m_count2->setRange(1, kMaxCount);
    m_count2->setValue(1);
    m_count2->setToolTip(countTip);
    // One value, two spin boxes: setValue does not re-emit on an unchanged
    // value, so the pair cannot loop. Only the first drives regenerate().
    connect(m_count,  QOverload<int>::of(&QSpinBox::valueChanged), m_count2, &QSpinBox::setValue);
    connect(m_count2, QOverload<int>::of(&QSpinBox::valueChanged), m_count,  &QSpinBox::setValue);
    connect(m_count,  QOverload<int>::of(&QSpinBox::valueChanged),
            this, &PasswordGenPanel::regenerate);

    m_charsGroup  = buildCharactersGroup();
    m_phraseGroup = buildPassphraseGroup();

    auto countRow = [this](QSpinBox *box) {
        auto *row = new QHBoxLayout;
        auto *lab = new QLabel(tr("How many:"));
        lab->setToolTip(box->toolTip());
        row->addStretch(1);
        row->addWidget(lab);
        row->addWidget(box);
        return row;
    };

    auto *pwPage = new QWidget;
    auto *pwLay = new QVBoxLayout(pwPage);
    pwLay->setContentsMargins(0, 0, 0, 0);
    pwLay->setSpacing(10);
    pwLay->addLayout(countRow(m_count));
    pwLay->addWidget(m_charsGroup);

    auto *phPage = new QWidget;
    auto *phLay = new QVBoxLayout(phPage);
    phLay->setContentsMargins(0, 0, 0, 0);
    phLay->setSpacing(10);
    phLay->addLayout(countRow(m_count2));
    phLay->addWidget(m_phraseGroup);

    m_stack = new QStackedWidget;
    m_stack->addWidget(pwPage);
    m_stack->addWidget(phPage);
    m_stack->addWidget(buildSshPage());
    root->addWidget(m_stack);
    connect(m_stack, &QStackedWidget::currentChanged,
            this, &PasswordGenPanel::syncEnabledState);

    // ── Actions ────────────────────────────────────────────────────
    m_actionRow = new QWidget;
    auto *btnRow = new QHBoxLayout(m_actionRow);
    btnRow->setContentsMargins(0, 0, 0, 0);
    auto *genBtn = new QPushButton(tr("Generate"));
    genBtn->setDefault(true);
    genBtn->setIcon(style()->standardIcon(QStyle::SP_BrowserReload));
    genBtn->setToolTip(tr("Generate a fresh value with the current settings.\n\n"
                          "Changing any setting also regenerates automatically, so "
                          "what is on screen always matches the bits figure above."));
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
    root->addWidget(m_actionRow);

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
            // The wipe is for secrets only. A public key is meant to be
            // pasted into authorized_keys, so it must never be snatched back.
            const bool secret = (!m_privateShown.isEmpty() && t == m_privateShown) ||
                                (currentPage() != PageSsh && t == currentText());
            if (!secret) return;
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

    m_sshWatcher = new QFutureWatcher<SshKeyOut>(this);
    connect(m_sshWatcher, &QFutureWatcher<SshKeyOut>::finished,
            this, &PasswordGenPanel::onSshKeyReady);

    selectPage(PagePassword);
    applyTheme();
    syncEnabledState();
    regenerate();
}

// ── Rail ───────────────────────────────────────────────────────────────

QWidget *PasswordGenPanel::buildRail() {
    auto *rail = new QWidget;
    rail->setObjectName(QStringLiteral("pwRail"));
    rail->setFixedWidth(170);
    rail->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Expanding);

    auto *v = new QVBoxLayout(rail);
    v->setContentsMargins(0, 12, 0, 12);
    v->setSpacing(2);

    struct Item { const char *label; const char *tip; };
    const Item items[] = {
        { QT_TR_NOOP("Password"),
          QT_TR_NOOP("Draw single characters from the sets you tick.\n"
                     "Highest entropy per character; hardest to type by hand.\n\n"
                     "Alt+1") },
        { QT_TR_NOOP("Passphrase"),
          QT_TR_NOOP("Join whole words from a built-in word list.\n"
                     "Lower entropy per character but far easier to type, "
                     "read aloud and remember.\n\n"
                     "Alt+2") },
        { QT_TR_NOOP("SSH key"),
          QT_TR_NOOP("Generate an OpenSSH key pair to put in authorized_keys.\n"
                     "Nothing reaches the disk until you choose Save.\n\n"
                     "Alt+3") },
    };
    for (int i = 0; i < 3; ++i) {
        auto *b = new QPushButton(tr(items[i].label));
        b->setObjectName(QStringLiteral("pwRailItem"));
        b->setToolTip(tr(items[i].tip));
        b->setCheckable(true);
        b->setAutoExclusive(true);
        b->setFlat(true);
        b->setCursor(Qt::PointingHandCursor);
        b->setFocusPolicy(Qt::StrongFocus);
        b->setProperty("active", i == 0);
        b->installEventFilter(this);
        connect(b, &QPushButton::clicked, this, [this, i]() { selectPage(i); });
        v->addWidget(b);
        m_railItems.append(b);
    }
    m_railItems.at(0)->setChecked(true);
    v->addStretch(1);
    return rail;
}

void PasswordGenPanel::applyRailState() {
    const int p = currentPage();
    for (int i = 0; i < m_railItems.size(); ++i) {
        QPushButton *b = m_railItems.at(i);
        const bool on = (i == p);
        if (b->property("active").toBool() != on) {
            b->setProperty("active", on);
            // A dynamic property only reaches the stylesheet after a repolish.
            b->style()->unpolish(b);
            b->style()->polish(b);
            b->update();
        }
    }
}

void PasswordGenPanel::selectPage(int page) {
    const int p = qBound(0, page, int(PageSsh));
    for (int i = 0; i < m_railItems.size(); ++i)
        if (m_railItems.at(i)->isChecked() != (i == p))
            m_railItems.at(i)->setChecked(i == p);
    if (m_stack && m_stack->currentIndex() != p) m_stack->setCurrentIndex(p);
    applyRailState();
}

int PasswordGenPanel::currentPage() const {
    return m_stack ? m_stack->currentIndex() : int(PagePassword);
}

bool PasswordGenPanel::eventFilter(QObject *o, QEvent *e) {
    if (e->type() == QEvent::KeyPress && m_railItems.contains(qobject_cast<QPushButton *>(o))) {
        auto *k = static_cast<QKeyEvent *>(e);
        if (k->key() == Qt::Key_Down || k->key() == Qt::Key_Up) {
            const int step = (k->key() == Qt::Key_Down) ? 1 : -1;
            const int next = qBound(0, currentPage() + step, int(PageSsh));
            selectPage(next);
            m_railItems.at(next)->setFocus(Qt::TabFocusReason);
            return true;
        }
    }
    return QWidget::eventFilter(o, e);
}

void PasswordGenPanel::keyPressEvent(QKeyEvent *e) {
    // Alt+1/2/3. Handled here rather than as a QShortcut so it still works
    // when the focus sits inside a field on the page.
    if (e->modifiers().testFlag(Qt::AltModifier) &&
        e->key() >= Qt::Key_1 && e->key() <= Qt::Key_3) {
        selectPage(e->key() - Qt::Key_1);
        m_railItems.at(currentPage())->setFocus(Qt::ShortcutFocusReason);
        e->accept();
        return;
    }
    QWidget::keyPressEvent(e);
}

QWidget *PasswordGenPanel::buildCharactersGroup() {
    auto *box = new QGroupBox(tr("Characters"));
    box->setToolTip(tr("Settings for the random-characters mode: how long the "
                       "password is, and which characters it may be built from."));
    auto *v = new QVBoxLayout(box);
    // Match the frame inset by hand: QSS padding on a QGroupBox does
    // not move the layout, so the last row draws over the border.
    v->setContentsMargins(12, 14, 12, 12);

    auto *lenRow = new QHBoxLayout;
    auto *lenLabel = new QLabel(tr("Length"));
    const QString lenTip = tr("How many characters to generate, from %1 to %2.\n\n"
                              "Length buys entropy faster than adding character "
                              "sets does: one extra character is worth about "
                              "6.5 bits at the default alphabet.")
                               .arg(kMinLength).arg(kMaxLength);
    lenLabel->setToolTip(lenTip);
    lenRow->addWidget(lenLabel);
    m_length = new QSlider(Qt::Horizontal);
    m_length->setRange(kMinLength, kMaxLength);
    m_length->setValue(20);
    m_length->setToolTip(lenTip);
    m_lengthBox = new QSpinBox;
    m_lengthBox->setRange(kMinLength, kMaxLength);
    m_lengthBox->setValue(20);
    m_lengthBox->setToolTip(lenTip);
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
    m_lower->setToolTip(tr("Include the 26 lowercase letters a-z."));
    m_upper->setToolTip(tr("Include the 26 uppercase letters A-Z."));
    m_digits->setToolTip(tr("Include the 10 digits 0-9."));
    for (QCheckBox *c : { m_lower, m_upper, m_digits, m_symbols }) {
        c->setChecked(true);
        classRow->addWidget(c);
    }
    m_symbols->setToolTip(
        tr("%1\n\nQuotes, backslash, backtick and pipe are left out so a "
           "password survives being pasted into a shell command, a YAML "
           "file or a connection string.")
            .arg(classChars(Symbols, false)));
    auto *alsoLabel = new QLabel(tr("Also:"));
    alsoLabel->setToolTip(tr("Extra characters to draw from, beyond the sets above."));
    classRow->addWidget(alsoLabel);
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
    m_noLookalikes->setToolTip(tr("Leave out the five glyphs that get misread off a "
                                  "screen or a printout: zero, capital O, one, "
                                  "lowercase L and capital i.\n\n"
                                  "Costs a little entropy — the alphabet shrinks — "
                                  "but worth it for anything you will read aloud or "
                                  "type from paper."));
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
    box->setToolTip(tr("Settings for the passphrase mode: how many words to join, "
                       "and how to join them."));
    auto *v = new QVBoxLayout(box);
    // Match the frame inset by hand: QSS padding on a QGroupBox does
    // not move the layout, so the last row draws over the border.
    v->setContentsMargins(12, 14, 12, 12);

    auto *row = new QHBoxLayout;
    auto *wordsLabel = new QLabel(tr("Words"));
    const QString wordsTip = tr("How many words to join, from %1 to %2.\n\n"
                                "Each word is worth exactly %3 bits, so the total is "
                                "simply words × %3.")
                                 .arg(kMinWords).arg(kMaxWords)
                                 .arg(qRound(std::log2(double(wordlistSize()))));
    wordsLabel->setToolTip(wordsTip);
    row->addWidget(wordsLabel);
    m_words = new QSpinBox;
    m_words->setRange(kMinWords, kMaxWords);
    m_words->setValue(6);
    m_words->setToolTip(wordsTip);
    row->addWidget(m_words);

    row->addSpacing(12);
    auto *sepLabel = new QLabel(tr("Separator"));
    const QString sepTip = tr("What to put between the words.\n\n"
                              "The separator adds no entropy — it is fixed, not "
                              "chosen at random — but it makes the phrase far easier "
                              "to read and to type correctly.");
    sepLabel->setToolTip(sepTip);
    row->addWidget(sepLabel);
    m_separator = new QComboBox;
    m_separator->addItem(tr("hyphen  -"), QStringLiteral("-"));
    m_separator->addItem(tr("period  ."), QStringLiteral("."));
    m_separator->addItem(tr("underscore  _"), QStringLiteral("_"));
    m_separator->addItem(tr("space"), QStringLiteral(" "));
    m_separator->addItem(tr("none"), QString());
    m_separator->setToolTip(sepTip);
    row->addWidget(m_separator);
    row->addStretch(1);
    v->addLayout(row);

    auto *opt = new QHBoxLayout;
    m_capitalise   = new QCheckBox(tr("Capitalise each word"));
    m_capitalise->setToolTip(tr("Upper-case the first letter of every word.\n\n"
                                "Adds no entropy — the change is applied to every "
                                "word, so it is not a random choice — but some "
                                "sites demand a capital letter."));
    m_appendDigits = new QCheckBox(tr("Append two digits"));
    m_appendDigits->setToolTip(tr("Add a random two-digit group on the end.\n\n"
                                  "Worth log2(100) ≈ 6.6 bits, and satisfies the "
                                  "sites that insist on a digit."));
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
    applyRailState();
    // The shared readout, meter, buttons and status line belong to the two
    // password pages. The SSH page carries its own results block.
    const bool value = currentPage() != PageSsh;
    if (m_valueArea) m_valueArea->setVisible(value);
    if (m_actionRow) m_actionRow->setVisible(value);
    if (m_status)    m_status->setVisible(value);
    // Regenerate rather than only refresh: the meter belongs to the page you
    // just picked, and a stale value under a new bits figure is a lie.
    if (value) regenerate();
    else       syncSshEnabledState();
}

Options PasswordGenPanel::collectOptions() const {
    Options o;
    o.mode = currentPage() == PagePassphrase ? Mode::Passphrase : Mode::Characters;

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
    if (currentPage() == PageSsh) return;   // that page owns its own buttons
    const bool ready = validate(collectOptions()).isEmpty() && !currentText().isEmpty();
    m_copyBtn->setEnabled(ready);
    m_insertBtn->setEnabled(ready);
    m_newTabBtn->setEnabled(ready);
}

void PasswordGenPanel::regenerate() {
    // An SSH key costs seconds and replaces a key the user may still need,
    // so this page never regenerates behind their back.
    if (currentPage() == PageSsh) return;
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
    // On the SSH page this is the PUBLIC key line. The private key is never
    // handed out through here — Insert, Open in new tab and the clipboard
    // watcher all read this.
    if (currentPage() == PageSsh) return m_sshPublicLine;
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
        "QLineEdit { background: %3; color: %6; border: 1px solid %4;"
        "            border-radius: 5px; padding: 3px 6px; }"
        "QPushButton { background: %2; color: %6; border: 1px solid %4;"
        "              border-radius: 6px; padding: 5px 12px; }"
        "QPushButton:hover { border-color: %5; }"
        "QPushButton:default { border: 1px solid %5; }"
        "QPushButton:disabled { color: %8; border-color: %4; }"
        "QSlider::groove:horizontal { height: 4px; background: %4; border-radius: 2px; }"
        "QSlider::handle:horizontal { background: %5; width: 14px; margin: -6px 0;"
        "                             border-radius: 7px; }"
        // The rail. An ID selector outranks the QPushButton rule above, so
        // the items keep their flat look on all three themes.
        "#pwRail { background: %2; border-right: 1px solid %4; }"
        "#pwRailItem { background: transparent; color: %7; border: none;"
        "              border-left: 3px solid transparent; text-align: left;"
        "              padding: 9px 14px; }"
        "#pwRailItem:hover { color: %6; }"
        "#pwRailItem[active=\"true\"] { color: %5; border-left: 3px solid %5; }"
        "#pwHint { color: %8; background: transparent; }"
        "#pwWarn { color: #E06C5A; background: transparent; }"
    ).arg(p.bg, p.card, p.input, p.border, p.accent, p.text, p.sub, p.muted));
    m_status->setStyleSheet(QStringLiteral("color: %1; background: transparent;").arg(p.muted));
    m_entropy->setStyleSheet(QStringLiteral("color: %1; background: transparent;").arg(p.sub));

    // A hidden child does not repolish itself when the sheet changes, so the
    // SSH results block kept the previous theme's colours until it appeared.
    for (QWidget *w : findChildren<QWidget *>()) {
        w->style()->unpolish(w);
        w->style()->polish(w);
        w->update();
    }

    // Spin boxes and combo boxes deliberately get a PALETTE, not a
    // stylesheet. The moment a QSS rule matches them, Qt hands their
    // sub-controls to the stylesheet engine, which then needs an explicit
    // image for every arrow — and the CSS border-triangle idiom renders
    // as a filled box, not a triangle. Leaving them to the platform style
    // keeps a real arrow on Linux and the native glyph on Windows/macOS.
    QPalette fieldPal = palette();
    fieldPal.setColor(QPalette::Base,            QColor(p.input));
    fieldPal.setColor(QPalette::Text,            QColor(p.text));
    fieldPal.setColor(QPalette::Window,          QColor(p.card));
    fieldPal.setColor(QPalette::WindowText,      QColor(p.text));
    fieldPal.setColor(QPalette::Button,          QColor(p.input));
    fieldPal.setColor(QPalette::ButtonText,      QColor(p.text));
    fieldPal.setColor(QPalette::Highlight,       QColor(p.accent));
    fieldPal.setColor(QPalette::HighlightedText, QColor("#FFFFFF"));
    fieldPal.setColor(QPalette::Mid,             QColor(p.border));
    fieldPal.setColor(QPalette::Dark,            QColor(p.border));
    fieldPal.setColor(QPalette::Light,           QColor(p.card));
    fieldPal.setColor(QPalette::Shadow,          QColor(p.border));
    for (QWidget *w : findChildren<QWidget *>()) {
        if (qobject_cast<QSpinBox *>(w) || qobject_cast<QComboBox *>(w)) {
            w->setPalette(fieldPal);
            if (auto *cb = qobject_cast<QComboBox *>(w))
                if (QAbstractItemView *v = cb->view()) v->setPalette(fieldPal);
        }
    }

    // Styling ::indicator changes its width, but QCheckBox/QRadioButton
    // compute sizeHint() from the STYLE metric, not the stylesheet — so the
    // longest labels come up a few pixels short and clip. Re-polish and take
    // the width the widget now actually needs.
    for (QAbstractButton *b : findChildren<QAbstractButton *>()) {
        if (!qobject_cast<QCheckBox *>(b) && !qobject_cast<QRadioButton *>(b)) continue;
        b->ensurePolished();
        b->setMinimumWidth(b->sizeHint().width() + 6);
    }

    // A fresh stylesheet drops the resolved [active] rule, so re-run it.
    for (QPushButton *b : m_railItems) {
        b->style()->unpolish(b);
        b->style()->polish(b);
        b->update();
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

// ── SSH key page ───────────────────────────────────────────────────────

namespace {

// alg, bits and the security one-liner, in the order the combo lists them.
struct SshType { int alg; int bits; const char *label; const char *security; const char *tip; };

const SshType kSshTypes[] = {
    { 0, 0, QT_TR_NOOP("Ed25519 — recommended"),
      QT_TR_NOOP("256-bit key, ~128-bit security"),
      QT_TR_NOOP("The default for new keys. Small (one short line in "
                 "authorized_keys), fast to sign with, and constant-time by "
                 "construction. Supported by OpenSSH 6.5 and newer, which is "
                 "everything shipped since 2014.") },
    { 1, 0, QT_TR_NOOP("ECDSA P-256"),
      QT_TR_NOOP("~128-bit security"),
      QT_TR_NOOP("Pick this only when a policy names a NIST curve — a "
                 "FIPS-constrained environment, or a smartcard that offers "
                 "nothing else. Ed25519 is the better key everywhere else.") },
    { 2, 0, QT_TR_NOOP("ECDSA P-384"),
      QT_TR_NOOP("~192-bit security"),
      QT_TR_NOOP("The larger NIST curve, for the same FIPS-constrained case "
                 "as P-256 when a policy asks for more than 128-bit strength.") },
    { 3, 3072, QT_TR_NOOP("RSA 3072"),
      QT_TR_NOOP("~128-bit security"),
      QT_TR_NOOP("Use RSA only for an old server or a hardware token that "
                 "cannot do Ed25519. 3072 bits is the size that matches "
                 "Ed25519's strength. Generating it takes a moment.") },
    { 3, 4096, QT_TR_NOOP("RSA 4096"),
      QT_TR_NOOP("~140-bit security"),
      QT_TR_NOOP("Larger RSA, for the same old-server case. The extra bits "
                 "buy little over 3072 and cost noticeably more time to "
                 "generate and to verify on every connection.") },
    { 3, 2048, QT_TR_NOOP("RSA 2048 — legacy"),
      QT_TR_NOOP("~112-bit security"),
      QT_TR_NOOP("The smallest RSA size still worth issuing, and only for "
                 "hardware that refuses anything bigger. Below 112-bit "
                 "strength; do not pick it for a key you will keep.") },
};
constexpr int kSshTypeCount = int(sizeof(kSshTypes) / sizeof(kSshTypes[0]));

}  // namespace

int PasswordGenPanel::sshAlg() const {
    const int i = qBound(0, m_sshType->currentIndex(), kSshTypeCount - 1);
    return kSshTypes[i].alg;
}

int PasswordGenPanel::sshBits() const {
    const int i = qBound(0, m_sshType->currentIndex(), kSshTypeCount - 1);
    return kSshTypes[i].bits;
}

QString PasswordGenPanel::sshDefaultFileName() const {
    switch (sshAlg()) {
    case 0:  return QStringLiteral("id_ed25519");
    case 1:
    case 2:  return QStringLiteral("id_ecdsa");
    default: return QStringLiteral("id_rsa");
    }
}

QWidget *PasswordGenPanel::buildSshPage() {
    auto *page = new QWidget;
    auto *v = new QVBoxLayout(page);
    v->setContentsMargins(0, 0, 0, 0);
    v->setSpacing(10);

    auto *box = new QGroupBox(tr("SSH key"));
    box->setToolTip(tr("Settings for a new OpenSSH key pair: which algorithm, "
                       "what comment to label it with, and whether the private "
                       "key is encrypted with a passphrase."));
    auto *form = new QFormLayout(box);
    form->setContentsMargins(12, 14, 12, 12);
    form->setFieldGrowthPolicy(QFormLayout::ExpandingFieldsGrow);

    m_sshType = new QComboBox;
    for (int i = 0; i < kSshTypeCount; ++i) {
        m_sshType->addItem(tr(kSshTypes[i].label));
        m_sshType->setItemData(i, tr(kSshTypes[i].tip), Qt::ToolTipRole);
    }
    m_sshType->setToolTip(tr("Which signature algorithm the key uses. Hover an "
                             "entry in the list for when to pick it.\n\n"
                             "Ed25519 unless something forces your hand."));
    auto *typeLabel = new QLabel(tr("Key type"));
    typeLabel->setToolTip(m_sshType->toolTip());
    form->addRow(typeLabel, m_sshType);

    m_sshComment = new QLineEdit;
    m_sshComment->setPlaceholderText(tr("optional — e.g. work-laptop"));
    m_sshComment->setToolTip(tr("A label stored in the public key, and in every "
                                "authorized_keys file you paste it into.\n\n"
                                "ssh-keygen defaults this to user@host. This panel "
                                "deliberately does not, so your username and machine "
                                "name are not copied onto every server you use."));
    auto *commentLabel = new QLabel(tr("Comment"));
    commentLabel->setToolTip(m_sshComment->toolTip());
    form->addRow(commentLabel, m_sshComment);

    m_sshPass = new QLineEdit;
    m_sshPass->setEchoMode(QLineEdit::Password);
    m_sshPass->setToolTip(tr("Encrypts the private key on disk (aes256-ctr with "
                             "bcrypt-pbkdf, the same as ssh-keygen).\n\n"
                             "Without one, anyone who can read the file can log in "
                             "as you."));
    auto *passLabel = new QLabel(tr("Passphrase"));
    passLabel->setToolTip(m_sshPass->toolTip());
    form->addRow(passLabel, m_sshPass);

    m_sshPass2 = new QLineEdit;
    m_sshPass2->setEchoMode(QLineEdit::Password);
    m_sshPass2->setToolTip(tr("Type the passphrase again. A key you cannot unlock "
                              "is a key you have lost, so Generate stays off until "
                              "these two match."));
    auto *pass2Label = new QLabel(tr("Confirm"));
    pass2Label->setToolTip(m_sshPass2->toolTip());
    form->addRow(pass2Label, m_sshPass2);

    m_sshShowPass = new QCheckBox(tr("Show"));
    m_sshShowPass->setToolTip(tr("Reveal both passphrase fields so you can check "
                                 "what you typed."));
    form->addRow(QString(), m_sshShowPass);

    auto *hint = new QLabel(tr("Leave empty for an unencrypted key — fine for "
                               "automation, not for a laptop."));
    hint->setObjectName(QStringLiteral("pwHint"));
    hint->setWordWrap(true);
    form->addRow(QString(), hint);
    v->addWidget(box);

    auto *genRow = new QHBoxLayout;
    m_sshGenBtn = new QPushButton(tr("Generate key"));
    m_sshGenBtn->setDefault(true);
    m_sshGenBtn->setIcon(style()->standardIcon(QStyle::SP_BrowserReload));
    m_sshGenBtn->setToolTip(tr("Generate a fresh key pair with the settings above.\n\n"
                               "Unlike the password pages this never runs on its own: "
                               "changing a setting clears the result instead, so a key "
                               "you are still using cannot be replaced under you."));
    genRow->addWidget(m_sshGenBtn);
    genRow->addStretch(1);
    v->addLayout(genRow);

    // ── Results ────────────────────────────────────────────────────
    m_sshResults = new QWidget;
    auto *res = new QVBoxLayout(m_sshResults);
    res->setContentsMargins(0, 0, 0, 0);
    res->setSpacing(8);

    auto *pubLabel = new QLabel(tr("Public key"));
    pubLabel->setToolTip(tr("The authorized_keys line. This half is meant to be "
                            "handed out — paste it into a server, a Git host or a "
                            "colleague's message."));
    res->addWidget(pubLabel);

    m_sshPublic = new QPlainTextEdit;
    m_sshPublic->setReadOnly(true);
    m_sshPublic->setLineWrapMode(QPlainTextEdit::WidgetWidth);
    m_sshPublic->setFont(notepatraCodeFont(12));
    m_sshPublic->setCursorWidth(0);
    m_sshPublic->setTextInteractionFlags(Qt::TextSelectableByMouse | Qt::TextSelectableByKeyboard);
    m_sshPublic->setToolTip(pubLabel->toolTip());
    {
        const QFontMetrics fm(m_sshPublic->font());
        m_sshPublic->setFixedHeight(fm.lineSpacing() * 3 + 18);
    }

    m_sshCopyPubBtn = new QPushButton(tr("Copy public key"));
    m_sshCopyPubBtn->setIcon(style()->standardIcon(QStyle::SP_DialogSaveButton));
    m_sshCopyPubBtn->setToolTip(tr("Copy the authorized_keys line. This one is not "
                                   "taken back off the clipboard — a public key is "
                                   "meant to be pasted."));
    m_sshInsertBtn = new QPushButton(tr("Insert into editor"));
    m_sshInsertBtn->setToolTip(tr("Insert the PUBLIC key at the caret of the editor "
                                  "tab you were last in. The private key is never "
                                  "handed to an editor buffer."));
    m_sshNewTabBtn = new QPushButton(tr("Open in new tab"));
    m_sshNewTabBtn->setIcon(style()->standardIcon(QStyle::SP_FileIcon));
    m_sshNewTabBtn->setToolTip(tr("Open the PUBLIC key as a new untitled editor tab. "
                                  "The private key is never handed to an editor "
                                  "buffer."));

    auto *pubRow = new QHBoxLayout;
    pubRow->setSpacing(8);
    pubRow->addWidget(m_sshPublic, 1);
    auto *pubBtns = new QVBoxLayout;
    pubBtns->setSpacing(6);
    pubBtns->addWidget(m_sshCopyPubBtn);
    pubBtns->addWidget(m_sshInsertBtn);
    pubBtns->addWidget(m_sshNewTabBtn);
    pubBtns->addStretch(1);
    pubRow->addLayout(pubBtns);
    res->addLayout(pubRow);

    auto *fpRow = new QHBoxLayout;
    auto *fpLabel = new QLabel(tr("Fingerprint"));
    const QString fpTip = tr("The SHA256 fingerprint, in the same form ssh-keygen -l "
                            "prints. Compare it against what the server shows to be "
                            "sure you installed the key you meant to.");
    fpLabel->setToolTip(fpTip);
    m_sshFingerprint = new QLabel;
    m_sshFingerprint->setFont(notepatraCodeFont(11));
    m_sshFingerprint->setTextInteractionFlags(Qt::TextSelectableByMouse);
    m_sshFingerprint->setToolTip(fpTip);
    fpRow->addWidget(fpLabel);
    fpRow->addWidget(m_sshFingerprint, 1);
    res->addLayout(fpRow);

    auto *privRow = new QHBoxLayout;
    m_sshSaveBtn = new QPushButton(tr("Save private key…"));
    m_sshSaveBtn->setIcon(style()->standardIcon(QStyle::SP_DialogSaveButton));
    m_sshSaveBtn->setToolTip(tr("Write the private key and its .pub companion to "
                                "disk. Nothing has reached the disk before this.\n\n"
                                "An existing file is never overwritten — pick another "
                                "name instead."));
    m_sshShowPrivate = new QCheckBox(tr("Show private key"));
    m_sshShowPrivate->setToolTip(tr("Reveal the private key text. It stays hidden by "
                                    "default so it cannot end up in a screenshot or "
                                    "a screen share by accident."));
    privRow->addWidget(m_sshSaveBtn);
    privRow->addWidget(m_sshShowPrivate);
    privRow->addStretch(1);
    res->addLayout(privRow);

    m_sshPrivateBox = new QWidget;
    auto *pv = new QVBoxLayout(m_sshPrivateBox);
    pv->setContentsMargins(0, 0, 0, 0);
    pv->setSpacing(6);
    auto *warn = new QLabel(tr("Anyone who can read this can log in as you."));
    warn->setObjectName(QStringLiteral("pwWarn"));
    warn->setWordWrap(true);
    pv->addWidget(warn);
    m_sshPrivate = new QPlainTextEdit;
    m_sshPrivate->setReadOnly(true);
    m_sshPrivate->setFont(notepatraCodeFont(11));
    m_sshPrivate->setCursorWidth(0);
    m_sshPrivate->setLineWrapMode(QPlainTextEdit::NoWrap);
    m_sshPrivate->setTextInteractionFlags(Qt::TextSelectableByMouse | Qt::TextSelectableByKeyboard);
    m_sshPrivate->setMinimumHeight(150);
    m_sshPrivate->setToolTip(tr("The private key, in OpenSSH format. Treat it the "
                                "way you would treat a password."));
    pv->addWidget(m_sshPrivate);
    m_sshCopyPrivBtn = new QPushButton(tr("Copy private key"));
    m_sshCopyPrivBtn->setToolTip(tr("Copy the private key. It is taken back off the "
                                    "clipboard after %1 seconds unless you copy "
                                    "something else first.")
                                     .arg(kClipboardClearMs / 1000));
    auto *cpRow = new QHBoxLayout;
    cpRow->addWidget(m_sshCopyPrivBtn);
    cpRow->addStretch(1);
    pv->addLayout(cpRow);
    m_sshPrivateBox->setVisible(false);
    res->addWidget(m_sshPrivateBox);

    m_sshResults->setVisible(false);
    v->addWidget(m_sshResults);

    m_sshStatus = new QLabel;
    m_sshStatus->setObjectName(QStringLiteral("pwHint"));
    m_sshStatus->setWordWrap(true);
    v->addWidget(m_sshStatus);

    m_sshSecurity = new QLabel;
    m_sshSecurity->setObjectName(QStringLiteral("pwHint"));
    m_sshSecurity->setWordWrap(true);
    v->addWidget(m_sshSecurity);
    v->addStretch(1);

    // ── Wiring ─────────────────────────────────────────────────────
    connect(m_sshGenBtn, &QPushButton::clicked, this, &PasswordGenPanel::generateSshKey);
    connect(m_sshShowPass, &QCheckBox::toggled, this, [this](bool on) {
        const QLineEdit::EchoMode m = on ? QLineEdit::Normal : QLineEdit::Password;
        m_sshPass->setEchoMode(m);
        m_sshPass2->setEchoMode(m);
    });
    connect(m_sshShowPrivate, &QCheckBox::toggled, this, [this](bool on) {
        m_sshPrivateBox->setVisible(on && !m_sshPrivatePem.isEmpty());
    });
    // A changed setting invalidates the key on screen rather than quietly
    // regenerating one the user may already have installed.
    connect(m_sshType, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this]() {
        clearSshResult();
        setSshStatus(QString());
        syncSshEnabledState();
    });
    for (QLineEdit *e : { m_sshComment, m_sshPass, m_sshPass2 })
        connect(e, &QLineEdit::textChanged, this, [this]() {
            clearSshResult();
            setSshStatus(QString());
            syncSshEnabledState();
        });
    connect(m_sshCopyPubBtn, &QPushButton::clicked, this, [this]() {
        if (m_sshPublicLine.isEmpty()) return;
        if (QClipboard *cb = QApplication::clipboard()) cb->setText(m_sshPublicLine);
        emit statusMessage(tr("Public key copied."));
    });
    connect(m_sshInsertBtn, &QPushButton::clicked, this, [this]() {
        if (!m_sshPublicLine.isEmpty()) emit insertRequested(m_sshPublicLine);
    });
    connect(m_sshNewTabBtn, &QPushButton::clicked, this, [this]() {
        if (!m_sshPublicLine.isEmpty()) emit newTabRequested(m_sshPublicLine);
    });
    connect(m_sshCopyPrivBtn, &QPushButton::clicked, this, [this]() {
        if (m_sshPrivatePem.isEmpty()) return;
        QClipboard *cb = QApplication::clipboard();
        if (!cb) return;
        // Set before the clipboard so the watcher sees it as already armed.
        m_clipboardOwned = m_sshPrivatePem;
        cb->setText(m_sshPrivatePem);
        armClipboardWipe(m_sshPrivatePem);
        emit statusMessage(tr("Private key copied — clipboard clears in %1 seconds.")
                               .arg(kClipboardClearMs / 1000));
    });
    connect(m_sshSaveBtn, &QPushButton::clicked, this, &PasswordGenPanel::saveSshPrivateKey);
    return page;
}

void PasswordGenPanel::setSshStatus(const QString &text) {
    m_sshStatusText = text;
    if (m_sshStatus) m_sshStatus->setText(text);
}

void PasswordGenPanel::clearSshResult() {
    // Best effort only: QString is copy-on-write, so overwriting one copy
    // cannot promise the bytes are gone from memory.
    m_sshPrivatePem.fill(QLatin1Char('\0'));
    m_sshPrivatePem.clear();
    m_privateShown.fill(QLatin1Char('\0'));
    m_privateShown.clear();
    m_sshPublicLine.clear();
    m_sshFpText.clear();
    if (m_sshPublic)      m_sshPublic->clear();
    if (m_sshPrivate)     m_sshPrivate->clear();
    if (m_sshFingerprint) m_sshFingerprint->clear();
    if (m_sshPrivateBox)  m_sshPrivateBox->setVisible(false);
    if (m_sshResults)     m_sshResults->setVisible(false);
}

void PasswordGenPanel::syncSshEnabledState() {
    if (!m_sshGenBtn) return;
    const bool match = m_sshPass->text() == m_sshPass2->text();
    m_sshGenBtn->setText(m_sshBusy ? tr("Generating…") : tr("Generate key"));
    m_sshGenBtn->setEnabled(!m_sshBusy && match);
    for (QWidget *w : { static_cast<QWidget *>(m_sshType),
                        static_cast<QWidget *>(m_sshComment),
                        static_cast<QWidget *>(m_sshPass),
                        static_cast<QWidget *>(m_sshPass2),
                        static_cast<QWidget *>(m_sshShowPass),
                        static_cast<QWidget *>(m_sshResults) })
        w->setEnabled(!m_sshBusy);

    const int i = qBound(0, m_sshType->currentIndex(), kSshTypeCount - 1);
    m_sshSecurity->setText(
        tr("Generated in Rust from the OS random source (getrandom). Nothing is "
           "written to disk until you choose Save. An unencrypted private key is "
           "only as secret as the file it lives in.\n\n%1: %2.")
            .arg(tr(kSshTypes[i].label), tr(kSshTypes[i].security)));

    if (m_sshBusy) return;
    if (!match)
        m_sshStatus->setText(tr("The passphrase and its confirmation do not match — "
                                "Generate stays off until they do."));
    else
        m_sshStatus->setText(m_sshStatusText);
}

void PasswordGenPanel::generateSshKey() {
    if (m_sshBusy) return;
    if (m_sshPass->text() != m_sshPass2->text()) { syncSshEnabledState(); return; }
    clearSshResult();

    const int alg = sshAlg();
    const int bits = sshBits();
    const QByteArray comment = m_sshComment->text().trimmed().toUtf8();
    const QByteArray pass = m_sshPass->text().toUtf8();

    m_sshBusy = true;
    setSshStatus(tr("Generating…"));
    syncSshEnabledState();
    // Off the GUI thread: an RSA 4096 key takes seconds. The worker captures
    // only value types, so closing the tab mid-run cannot reach back here.
    m_sshWatcher->setFuture(QtConcurrent::run(runSshKeygen, alg, bits, comment, pass));
}

void PasswordGenPanel::onSshKeyReady() {
    m_sshBusy = false;
    if (!m_sshWatcher->isFinished()) { syncSshEnabledState(); return; }
    const SshKeyOut r = m_sshWatcher->result();
    if (!r.ok) {
        setSshStatus(r.err.isEmpty() ? tr("Key generation failed.") : r.err);
        syncSshEnabledState();
        return;
    }
    m_sshPrivatePem = r.priv;
    m_privateShown  = r.priv;
    m_sshPublicLine = r.pub.trimmed();
    m_sshFpText     = r.fp.trimmed();
    m_sshPublic->setPlainText(m_sshPublicLine);
    m_sshPrivate->setPlainText(m_sshPrivatePem);
    m_sshFingerprint->setText(m_sshFpText);
    m_sshResults->setVisible(true);
    m_sshPrivateBox->setVisible(m_sshShowPrivate->isChecked());
    setSshStatus(tr("Key ready. Nothing has been written to disk."));
    syncSshEnabledState();
}

bool PasswordGenPanel::writeSshKeyTo(const QString &path) {
    if (m_sshPrivatePem.isEmpty()) {
        setSshStatus(tr("Generate a key first."));
        return false;
    }
    const QString pubPath = path + QStringLiteral(".pub");
    if (QFile::exists(path) || QFile::exists(pubPath)) {
        const QString clash = QFile::exists(path) ? path : pubPath;
        setSshStatus(tr("%1 already exists. Pick another name — a key file is "
                        "never overwritten.").arg(QDir::toNativeSeparators(clash)));
        return false;
    }

    // Create the file empty, tighten the permissions, and only then let the
    // key bytes land. The other order leaves a readable window.
    QFile priv(path);
    if (!priv.open(QIODevice::WriteOnly | QIODevice::NewOnly)) {
        setSshStatus(tr("Could not create %1: %2")
                         .arg(QDir::toNativeSeparators(path), priv.errorString()));
        return false;
    }
    priv.close();
#ifndef Q_OS_WIN
    if (!priv.setPermissions(QFile::ReadOwner | QFile::WriteOwner)) {
        priv.remove();
        setSshStatus(tr("Could not restrict the permissions on %1, so nothing was "
                        "written.").arg(QDir::toNativeSeparators(path)));
        return false;
    }
#endif
    if (!priv.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        priv.remove();
        setSshStatus(tr("Could not write %1: %2")
                         .arg(QDir::toNativeSeparators(path), priv.errorString()));
        return false;
    }
    const QByteArray pem = m_sshPrivatePem.toUtf8();
    if (priv.write(pem) != pem.size() || !priv.flush()) {
        priv.close();
        priv.remove();
        setSshStatus(tr("Could not write %1: %2")
                         .arg(QDir::toNativeSeparators(path), priv.errorString()));
        return false;
    }
    priv.close();

    QFile pub(pubPath);
    if (!pub.open(QIODevice::WriteOnly | QIODevice::NewOnly)) {
        setSshStatus(tr("Wrote %1 but could not create %2: %3")
                         .arg(QDir::toNativeSeparators(path),
                              QDir::toNativeSeparators(pubPath), pub.errorString()));
        return false;
    }
    pub.close();
#ifndef Q_OS_WIN
    pub.setPermissions(QFile::ReadOwner | QFile::WriteOwner |
                       QFile::ReadGroup | QFile::ReadOther);
#endif
    if (!pub.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        setSshStatus(tr("Wrote %1 but could not write %2: %3")
                         .arg(QDir::toNativeSeparators(path),
                              QDir::toNativeSeparators(pubPath), pub.errorString()));
        return false;
    }
    QByteArray line = m_sshPublicLine.toUtf8();
    if (!line.endsWith('\n')) line.append('\n');
    pub.write(line);
    pub.close();

    QString msg = tr("Saved %1 and %2.")
                      .arg(QDir::toNativeSeparators(path),
                           QDir::toNativeSeparators(pubPath));
#ifdef Q_OS_WIN
    msg += QLatin1Char(' ') + tr("Check the file's permissions — OpenSSH refuses a "
                                 "private key other users can read.");
#endif
    setSshStatus(msg);
    emit statusMessage(msg);
    return true;
}

void PasswordGenPanel::saveSshPrivateKey() {
    if (m_sshPrivatePem.isEmpty()) {
        setSshStatus(tr("Generate a key first."));
        return;
    }
    // Start in ~/.ssh when it is already there; this panel creates no
    // directories of its own.
    QString dir = QDir::homePath() + QStringLiteral("/.ssh");
    if (!QFileInfo(dir).isDir()) dir = QDir::homePath();

    QFileDialog dlg(this, tr("Save private key"));
    dlg.setAcceptMode(QFileDialog::AcceptSave);
    dlg.setFileMode(QFileDialog::AnyFile);
    dlg.setOption(QFileDialog::DontUseNativeDialog, true);
    // We refuse an existing path ourselves with a clearer message than the
    // dialog's overwrite prompt, which offers exactly the wrong answer.
    dlg.setOption(QFileDialog::DontConfirmOverwrite, true);
    dlg.setDirectory(dir);
    dlg.setNameFilter(tr("All Files (*)"));
    dlg.selectFile(sshDefaultFileName());
    if (dlg.exec() != QDialog::Accepted) return;
    const QStringList picked = dlg.selectedFiles();
    if (picked.isEmpty()) return;
    writeSshKeyTo(picked.first());
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
