/**
 * Widget-level test for PasswordGenPanel.
 *
 * Offscreen-safe: constructs the panel, drives the real controls, and
 * asserts the behaviours that the core test cannot see — that the left
 * rail swaps the three pages, that an invalid configuration
 * disables the action buttons instead of producing a bad password, that
 * Copy actually reaches the clipboard, and that the timed clipboard wipe
 * only takes back a value the panel itself put there.
 *
 * Never opens a modal: a QMessageBox in an offscreen Windows CI job is a
 * known segfault, so this panel reports every problem through an inline
 * label instead. The SSH save path is driven through writeSshKeyTo() for
 * the same reason — the file dialog stays out of the test.
 */

#include "src/passwordgen.h"
#include "src/config.h"

#include <QApplication>
#include <QAbstractButton>
#include <QGroupBox>
#include <QSlider>
#include <QCheckBox>
#include <QClipboard>
#include <QComboBox>
#include <QPalette>
#include <QLabel>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QRadioButton>
#include <QDir>
#include <QFile>
#include <QSignalSpy>
#include <QSpinBox>
#include <QStackedWidget>
#include <QTemporaryDir>
#include <QTest>
#include <QTimer>

#include <cstdio>

static int g_pass = 0, g_fail = 0;
static void check(const char *what, bool ok, const QString &detail = {}) {
    if (ok) { std::printf("  [PASS] %s\n", what); ++g_pass; }
    else    {
        std::printf("  [FAIL] %s%s%s\n", what,
                    detail.isEmpty() ? "" : " — ",
                    detail.toUtf8().constData());
        ++g_fail;
    }
}

// The panel's children are unnamed, so reach them by type + visible text.
template <typename T>
static T *byText(QWidget *root, const QString &text) {
    for (T *w : root->findChildren<T *>())
        if (w->text() == text) return w;
    return nullptr;
}

// Two pages carry a button reading "Insert into editor" — the password one
// and the SSH one — so a label alone is ambiguous. Take the showing one.
template <typename T>
static T *byShownText(QWidget *root, const QString &text) {
    for (T *w : root->findChildren<T *>())
        if (w->text() == text && w->isVisibleTo(root)) return w;
    return byText<T>(root, text);
}

int main(int argc, char **argv) {
    // QStandardPaths writes under $HOME; a GUI test must never touch the
    // real one.
    QTemporaryDir homeDir;
    if (homeDir.isValid()) {
        qputenv("HOME", homeDir.path().toUtf8());
        qputenv("XDG_CONFIG_HOME", (homeDir.path() + "/.config").toUtf8());
        qputenv("XDG_DATA_HOME", (homeDir.path() + "/.local/share").toUtf8());
    }
    QApplication app(argc, argv);
    std::printf("=== Password Generator panel tests ===\n\n");

    PasswordGenPanel panel;

    std::printf("— construction ───────────────────────────────────\n");
    {
        auto *out = panel.findChild<QPlainTextEdit *>();
        check("the panel builds and holds a readout", out != nullptr);
        check("it generates on construction (no empty first impression)",
              !panel.currentText().isEmpty(), panel.currentText());
        check("the default result is 20 characters",
              panel.currentText().size() == 20,
              QString::number(panel.currentText().size()));
        check("the readout is read-only", out && out->isReadOnly());
    }

    std::printf("\n— regenerate ─────────────────────────────────────\n");
    {
        const QString first = panel.currentText();
        panel.regenerate();
        check("regenerate produces a different value",
              panel.currentText() != first);
    }

    std::printf("\n— bulk count ─────────────────────────────────────\n");
    {
        // The count spin box is the only one ranging up to 100.
        QSpinBox *count = nullptr;
        for (QSpinBox *s : panel.findChildren<QSpinBox *>())
            if (s->maximum() == PasswordGen::kMaxCount) count = s;
        check("the count spin box exists", count != nullptr);
        if (count) {
            count->setValue(5);
            panel.regenerate();
            const QStringList lines = panel.currentText().split(QLatin1Char('\n'));
            check("asking for 5 yields 5 lines", lines.size() == 5,
                  QString::number(lines.size()));
            bool distinct = QSet<QString>(lines.begin(), lines.end()).size() == 5;
            check("the 5 are independent draws, not one repeated", distinct);
            count->setValue(1);
            panel.regenerate();
        }
    }

    std::printf("\n— rail ───────────────────────────────────────────\n");
    {
        const QList<QPushButton *> rail =
            panel.findChildren<QPushButton *>(QStringLiteral("pwRailItem"));
        check("the rail holds exactly three items", rail.size() == 3,
              QString::number(rail.size()));
        if (rail.size() == 3) {
            check("they are labelled Password / Passphrase / SSH key",
                  rail.at(0)->text() == QStringLiteral("Password") &&
                  rail.at(1)->text() == QStringLiteral("Passphrase") &&
                  rail.at(2)->text() == QStringLiteral("SSH key"),
                  rail.at(0)->text() + "|" + rail.at(1)->text() + "|" + rail.at(2)->text());
            check("the rail is auto-exclusive and checkable",
                  rail.at(0)->isCheckable() && rail.at(0)->autoExclusive());

            auto *lower = byText<QCheckBox>(&panel, QStringLiteral("a-z"));
            auto *caps  = byText<QCheckBox>(&panel, QStringLiteral("Capitalise each word"));
            auto *sshShow = byText<QCheckBox>(&panel, QStringLiteral("Show private key"));

            rail.at(1)->click();
            check("clicking Passphrase shows the passphrase page",
                  panel.currentPage() == PasswordGenPanel::PagePassphrase &&
                  caps && caps->isVisibleTo(&panel) && lower && !lower->isVisibleTo(&panel));
            check("and marks that item active",
                  rail.at(1)->property("active").toBool() &&
                  !rail.at(0)->property("active").toBool());

            rail.at(2)->click();
            auto *genKey = byText<QPushButton>(&panel, QStringLiteral("Generate key"));
            check("clicking SSH key shows the SSH page",
                  panel.currentPage() == PasswordGenPanel::PageSsh &&
                  genKey && genKey->isVisibleTo(&panel));
            check("the SSH results block stays hidden until a key exists",
                  sshShow && !sshShow->isVisibleTo(&panel));
            auto *copyBtn = byText<QPushButton>(&panel, QStringLiteral("Copy"));
            check("the SSH page hides the shared readout buttons",
                  copyBtn && !copyBtn->isVisibleTo(&panel));

            rail.at(0)->click();
            check("clicking Password comes back to the password page",
                  panel.currentPage() == PasswordGenPanel::PagePassword &&
                  lower && lower->isVisibleTo(&panel));

            // Alt+2 must reach the rail even with the focus on the page.
            QTest::keyClick(&panel, Qt::Key_2, Qt::AltModifier);
            check("Alt+2 selects Passphrase",
                  panel.currentPage() == PasswordGenPanel::PagePassphrase,
                  QString::number(panel.currentPage()));
            QTest::keyClick(&panel, Qt::Key_3, Qt::AltModifier);
            check("Alt+3 selects SSH key",
                  panel.currentPage() == PasswordGenPanel::PageSsh);
            QTest::keyClick(&panel, Qt::Key_1, Qt::AltModifier);
            check("Alt+1 selects Password",
                  panel.currentPage() == PasswordGenPanel::PagePassword);

            // Down/Up walk the rail when it has the focus.
            rail.at(0)->setFocus();
            QTest::keyClick(rail.at(0), Qt::Key_Down);
            check("Down moves the rail selection on",
                  panel.currentPage() == PasswordGenPanel::PagePassphrase);
            QTest::keyClick(panel.findChildren<QPushButton *>(
                                QStringLiteral("pwRailItem")).at(1), Qt::Key_Up);
            check("Up moves it back",
                  panel.currentPage() == PasswordGenPanel::PagePassword);
        }
    }

    std::printf("\n— mode switch ────────────────────────────────────\n");
    {
        const QList<QPushButton *> rail =
            panel.findChildren<QPushButton *>(QStringLiteral("pwRailItem"));
        QPushButton *phrase = rail.size() == 3 ? rail.at(1) : nullptr;
        QPushButton *chars  = rail.size() == 3 ? rail.at(0) : nullptr;
        check("both mode rail items exist", phrase && chars);
        if (phrase && chars) {
            auto *lower = byText<QCheckBox>(&panel, QStringLiteral("a-z"));
            auto *caps  = byText<QCheckBox>(&panel, QStringLiteral("Capitalise each word"));
            check("characters mode shows the character options",
                  lower && lower->isVisibleTo(&panel));
            check("characters mode hides the passphrase options",
                  caps && !caps->isVisibleTo(&panel));

            phrase->click();
            check("passphrase mode hides the character options",
                  lower && !lower->isVisibleTo(&panel));
            check("passphrase mode shows the passphrase options",
                  caps && caps->isVisibleTo(&panel));

            panel.regenerate();
            // Derive the expectation from the control, not a literal, so
            // changing the default word count cannot make this test lie.
            int wanted = 0;
            for (QSpinBox *s : panel.findChildren<QSpinBox *>())
                if (s->maximum() == PasswordGen::kMaxWords) wanted = s->value();
            const QStringList words = panel.currentText().split(QLatin1Char('-'));
            check("passphrase mode produces one word per the Words spin box",
                  wanted > 0 && words.size() == wanted,
                  QString("%1 words for a setting of %2").arg(words.size()).arg(wanted));

            chars->click();
            panel.regenerate();
            check("switching back restores a character password",
                  !panel.currentText().contains(QLatin1Char('-')) ||
                      panel.currentText().size() == 20);
        }
    }

    std::printf("\n— invalid configuration ──────────────────────────\n");
    {
        auto *copy = byText<QPushButton>(&panel, QStringLiteral("Copy"));
        check("the Copy button exists", copy != nullptr);

        QList<QCheckBox *> classes;
        for (const QString &t : { QStringLiteral("a-z"), QStringLiteral("A-Z"),
                                  QStringLiteral("0-9"), QStringLiteral("Symbols") })
            if (auto *c = byText<QCheckBox>(&panel, t)) classes << c;
        check("all four character-set checkboxes exist", classes.size() == 4);

        for (QCheckBox *c : classes) c->setChecked(false);
        check("unticking every set disables Copy", copy && !copy->isEnabled());

        panel.regenerate();
        check("and produces nothing rather than a bad password",
              panel.currentText().isEmpty(), panel.currentText());

        bool explained = false;
        for (QLabel *l : panel.findChildren<QLabel *>())
            if (l->text().contains(QStringLiteral("at least one character set")))
                explained = true;
        check("the reason is shown inline (no modal)", explained);

        for (QCheckBox *c : classes) c->setChecked(true);
        check("re-ticking a set re-enables Copy", copy && copy->isEnabled());
        panel.regenerate();
        check("and generation resumes", !panel.currentText().isEmpty());
    }

    std::printf("\n— live regeneration ──────────────────────────────\n");
    {
        // A settings change used to update the meter and leave the OLD
        // value on screen, so Copy handed over something the readout no
        // longer described.
        QSpinBox *len = nullptr;
        for (QSpinBox *sb : panel.findChildren<QSpinBox *>())
            if (sb->maximum() == PasswordGen::kMaxLength) len = sb;
        check("the length spin box exists", len != nullptr);
        if (len) {
            len->setValue(32);
            check("changing the length regenerates rather than going stale",
                  panel.currentText().size() == 32,
                  QString::number(panel.currentText().size()));
            len->setValue(20);
            check("and again on the way back", panel.currentText().size() == 20);
        }
    }

    std::printf("\n— theme ──────────────────────────────────────────\n");
    {
        // Config::theme defaults to "System"; comparing the raw string
        // left the panel light on a dark OS.
        const QString before = Config::instance().theme;
        Config::instance().theme = QStringLiteral("Dark");
        panel.onThemeChanged();
        const bool dark = panel.styleSheet().contains(QStringLiteral("#1E1E1E"));
        Config::instance().theme = QStringLiteral("Light");
        panel.onThemeChanged();
        const bool light = panel.styleSheet().contains(QStringLiteral("#FAF9F5"));
        check("Dark theme paints the dark palette", dark);
        check("Light theme paints the light palette", light);

        Config::instance().theme = QStringLiteral("System");
        panel.onThemeChanged();
        const QString ss = panel.styleSheet();
        check("\"System\" resolves to a real palette, not neither",
              ss.contains(QStringLiteral("#1E1E1E")) || ss.contains(QStringLiteral("#FAF9F5")));
        // Spin boxes and combos must stay OUT of the stylesheet: any rule
        // matching them hands their arrows to the QSS engine, which needs
        // an explicit image and renders the CSS triangle idiom as a solid
        // box. They are themed by palette so the platform keeps drawing a
        // real arrow on Linux, Windows and macOS alike.
        check("no stylesheet rule targets the spin box or combo box",
              !ss.contains(QStringLiteral("QSpinBox")) &&
              !ss.contains(QStringLiteral("QComboBox")), ss.left(400));
        QSpinBox *anySpin = panel.findChild<QSpinBox *>();
        QComboBox *anyCombo = panel.findChild<QComboBox *>();
        check("spin boxes are themed by palette instead",
              anySpin && anySpin->palette().color(QPalette::Base) != QColor());
        check("combo boxes are themed by palette instead",
              anyCombo && anyCombo->palette().color(QPalette::Base) != QColor());
        // And the palette must actually follow the theme.
        Config::instance().theme = QStringLiteral("Dark");
        panel.onThemeChanged();
        const QColor darkBase = anySpin ? anySpin->palette().color(QPalette::Base) : QColor();
        Config::instance().theme = QStringLiteral("Light");
        panel.onThemeChanged();
        const QColor lightBase = anySpin ? anySpin->palette().color(QPalette::Base) : QColor();
        check("the field palette flips with the theme",
              darkBase.isValid() && lightBase.isValid() && darkBase != lightBase,
              QString("%1 vs %2").arg(darkBase.name(), lightBase.name()));
        Config::instance().theme = before;
        panel.onThemeChanged();
    }

    std::printf("\n— clipboard ──────────────────────────────────────\n");
    {
        QClipboard *cb = QApplication::clipboard();
        cb->clear();
        auto *copy = byText<QPushButton>(&panel, QStringLiteral("Copy"));
        const QString value = panel.currentText();
        if (copy) copy->click();
        check("Copy puts the current value on the clipboard",
              cb->text() == value, cb->text());

        // The wipe must not take back something the user copied later.
        cb->setText(QStringLiteral("something the user copied"));
        QMetaObject::invokeMethod(&panel, "clearClipboardIfUnchanged");
        check("the timed wipe leaves a newer clipboard alone",
              cb->text() == QStringLiteral("something the user copied"), cb->text());

        panel.regenerate();
        if (copy) copy->click();
        QMetaObject::invokeMethod(&panel, "clearClipboardIfUnchanged");
        check("the timed wipe clears our own value", cb->text().isEmpty(), cb->text());

        // The wipe must outlive the tab. A timer parented to the panel
        // dies with it, stranding the password on the clipboard.
        const int before = qApp->findChildren<QTimer *>(QString(), Qt::FindDirectChildrenOnly).size();
        panel.regenerate();
        if (copy) copy->click();
        const int after = qApp->findChildren<QTimer *>(QString(), Qt::FindDirectChildrenOnly).size();
        check("Copy arms a wipe owned by the application, not by the panel",
              after > before, QString("%1 -> %2").arg(before).arg(after));
        cb->clear();
    }

    std::printf("\n— empty readout ─────────────────────────────────\n");
    {
        // Valid settings are not enough to enable Copy: a failed generate
        // leaves the readout empty and the buttons must follow.
        auto *copy = byText<QPushButton>(&panel, QStringLiteral("Copy"));
        QList<QCheckBox *> classes;
        for (const QString &t : { QStringLiteral("a-z"), QStringLiteral("A-Z"),
                                  QStringLiteral("0-9"), QStringLiteral("Symbols") })
            if (auto *c = byText<QCheckBox>(&panel, t)) classes << c;
        for (QCheckBox *c : classes) c->setChecked(false);
        check("an invalid state empties the readout", panel.currentText().isEmpty());
        check("and disables Copy", copy && !copy->isEnabled());
        for (QCheckBox *c : classes) c->setChecked(true);
        check("restoring a set regenerates a value", !panel.currentText().isEmpty());
        check("and re-enables Copy", copy && copy->isEnabled());
    }

    std::printf("\n— discoverability ───────────────────────────────\n");
    {
        // Every control the user can touch has to explain itself on hover.
        int missing = 0;
        QStringList names;
        for (QWidget *w : panel.findChildren<QWidget *>()) {
            const bool interactive = qobject_cast<QAbstractButton *>(w) ||
                                     qobject_cast<QSpinBox *>(w) ||
                                     qobject_cast<QComboBox *>(w) ||
                                     qobject_cast<QLineEdit *>(w) ||
                                     qobject_cast<QSlider *>(w) ||
                                     qobject_cast<QGroupBox *>(w) ||
                                     qobject_cast<QPlainTextEdit *>(w);
            if (!interactive) continue;
            // Qt walks up to the parent when a widget has no tooltip of its
            // own, so a QSpinBox's internal QLineEdit is covered by the spin
            // box. Count a control as explained if it or an ancestor inside
            // the panel carries text.
            bool explained = false;
            for (const QWidget *a = w; a && a != &panel; a = a->parentWidget())
                if (!a->toolTip().trimmed().isEmpty()) { explained = true; break; }
            if (!explained) {
                ++missing;
                if (auto *b = qobject_cast<QAbstractButton *>(w)) names << b->text();
                else names << w->metaObject()->className();
            }
        }
        check("every interactive control has a hover tooltip",
              missing == 0, names.join(QStringLiteral(", ")));

        // Labels that head a control should explain it too.
        int labelled = 0;
        for (QLabel *l : panel.findChildren<QLabel *>())
            if (!l->toolTip().trimmed().isEmpty()) ++labelled;
        check("the section labels carry explanations as well", labelled >= 4,
              QString::number(labelled));
    }

    std::printf("\n— signals ────────────────────────────────────────\n");
    {
        QSignalSpy insertSpy(&panel, &PasswordGenPanel::insertRequested);
        QSignalSpy tabSpy(&panel, &PasswordGenPanel::newTabRequested);
        auto *insert = byShownText<QPushButton>(&panel, QStringLiteral("Insert into editor"));
        auto *newTab = byShownText<QPushButton>(&panel, QStringLiteral("Open in new tab"));
        check("both hand-off buttons exist", insert && newTab);
        if (insert) insert->click();
        if (newTab) newTab->click();
        check("Insert emits the current value with it",
              insertSpy.size() == 1 &&
                  insertSpy.at(0).at(0).toString() == panel.currentText());
        check("Open in new tab emits the current value with it",
              tabSpy.size() == 1 &&
                  tabSpy.at(0).at(0).toString() == panel.currentText());
    }


    std::printf("\n— SSH key page ──────────────────────────────────\n");
    {
        const QList<QPushButton *> rail =
            panel.findChildren<QPushButton *>(QStringLiteral("pwRailItem"));
        if (rail.size() == 3) rail.at(2)->click();
        check("the rail reaches the SSH page",
              panel.currentPage() == PasswordGenPanel::PageSsh);

        auto *gen = byText<QPushButton>(&panel, QStringLiteral("Generate key"));
        auto *showPriv = byText<QCheckBox>(&panel, QStringLiteral("Show private key"));
        check("the SSH page has a Generate key button and a Show private key box",
              gen && showPriv);

        // The three SSH line edits: comment, passphrase, confirm.
        QLineEdit *comment = nullptr;
        QList<QLineEdit *> secrets;
        for (QLineEdit *e : panel.findChildren<QLineEdit *>()) {
            if (e->echoMode() == QLineEdit::Password) secrets << e;
            else if (e->placeholderText().contains(QStringLiteral("work-laptop")))
                comment = e;
        }
        check("the comment field exists and starts empty",
              comment && comment->text().isEmpty());
        check("the comment placeholder does not leak user@host",
              comment && !comment->placeholderText().contains(QLatin1Char('@')),
              comment ? comment->placeholderText() : QString());
        check("there are two masked passphrase fields", secrets.size() == 2,
              QString::number(secrets.size()));

        // ── key type list ──
        QComboBox *type = nullptr;
        for (QComboBox *c : panel.findChildren<QComboBox *>())
            if (c->count() == 6 && c->itemText(0).startsWith(QStringLiteral("Ed25519")))
                type = c;
        check("the key-type list offers six modern types", type != nullptr);
        if (type) {
            bool weak = false;
            for (int i = 0; i < type->count(); ++i) {
                const QString t = type->itemText(i);
                if (t.contains(QStringLiteral("DSA"), Qt::CaseSensitive) &&
                    !t.contains(QStringLiteral("ECDSA"))) weak = true;
                if (t.contains(QStringLiteral("1024"))) weak = true;
            }
            check("no DSA and no RSA 1024 in the list", !weak);
            bool tipped = true;
            for (int i = 0; i < type->count(); ++i)
                if (type->itemData(i, Qt::ToolTipRole).toString().trimmed().isEmpty())
                    tipped = false;
            check("every key type explains when to pick it", tipped);
        }

        // ── passphrase mismatch ──
        if (secrets.size() == 2 && gen) {
            secrets.at(0)->setText(QStringLiteral("one"));
            secrets.at(1)->setText(QStringLiteral("two"));
            check("a passphrase mismatch disables Generate", !gen->isEnabled());
            bool explained = false;
            for (QLabel *l : panel.findChildren<QLabel *>())
                if (l->text().contains(QStringLiteral("do not match"))) explained = true;
            check("and says so inline (no modal)", explained);
            secrets.at(0)->clear();
            secrets.at(1)->clear();
            check("matching again re-enables Generate", gen->isEnabled());
        }

        // ── generate an Ed25519 key ──
        if (comment) comment->setText(QStringLiteral("np-test-comment"));
        check("changing the comment leaves the results hidden",
              panel.currentText().isEmpty());

        if (gen) gen->click();
        const bool done = QTest::qWaitFor([&panel]() {
            return !panel.currentText().isEmpty();
        }, 20000);
        check("Generate produces a key off the GUI thread", done);

        const QString pub = panel.currentText();
        check("the public line is an ssh-ed25519 key",
              pub.startsWith(QStringLiteral("ssh-ed25519 AAAA")), pub.left(40));
        check("the comment round-trips into the public line",
              pub.contains(QStringLiteral("np-test-comment")), pub);
        check("Generate is enabled again afterwards", gen && gen->isEnabled());
        check("the button text is back to Generate key",
              gen && gen->text() == QStringLiteral("Generate key"),
              gen ? gen->text() : QString());

        QString fingerprint;
        for (QLabel *l : panel.findChildren<QLabel *>())
            if (l->text().startsWith(QStringLiteral("SHA256:"))) fingerprint = l->text();
        check("a SHA256 fingerprint is shown", !fingerprint.isEmpty(), fingerprint);

        // ── the private key stays hidden ──
        QPlainTextEdit *privBox = nullptr;
        for (QPlainTextEdit *e : panel.findChildren<QPlainTextEdit *>())
            if (e->toPlainText().contains(QStringLiteral("PRIVATE KEY"))) privBox = e;
        check("the private key box exists", privBox != nullptr);
        check("but is hidden until Show private key is ticked",
              privBox && !privBox->isVisibleTo(&panel));
        if (showPriv) showPriv->setChecked(true);
        check("ticking Show reveals it", privBox && privBox->isVisibleTo(&panel));

        const QString pem = privBox ? privBox->toPlainText() : QString();
        check("the private key is an OpenSSH private key",
              pem.startsWith(QStringLiteral("-----BEGIN OPENSSH PRIVATE KEY-----")),
              pem.left(40));
        check("currentText() hands out the PUBLIC key, never the private one",
              panel.currentText() == pub && !panel.currentText().contains(
                  QStringLiteral("PRIVATE KEY")));

        // ── clipboard: public is left alone, private is reclaimed ──
        QClipboard *cb = QApplication::clipboard();
        cb->clear();
        if (auto *cpPub = byText<QPushButton>(&panel, QStringLiteral("Copy public key"))) {
            cpPub->click();
            check("Copy public key reaches the clipboard", cb->text() == pub);
            QMetaObject::invokeMethod(&panel, "clearClipboardIfUnchanged");
            check("the wipe does NOT arm on a public key", cb->text() == pub, cb->text());
        }
        cb->clear();
        if (auto *cpPriv = byText<QPushButton>(&panel, QStringLiteral("Copy private key"))) {
            cpPriv->click();
            check("Copy private key reaches the clipboard", cb->text() == pem);
            QMetaObject::invokeMethod(&panel, "clearClipboardIfUnchanged");
            check("copying the private key arms the 30-second wipe",
                  cb->text().isEmpty(), cb->text());
        }
        cb->clear();

        // ── save ──
        QTemporaryDir keyDir;
        check("the test has a scratch directory", keyDir.isValid());
        if (keyDir.isValid()) {
            const QString path = keyDir.path() + QStringLiteral("/id_ed25519");
            const bool wrote = panel.writeSshKeyTo(path);
            check("Save writes the private key", wrote && QFile::exists(path));
            check("and the .pub beside it",
                  QFile::exists(path + QStringLiteral(".pub")));
            QFile pubFile(path + QStringLiteral(".pub"));
            pubFile.open(QIODevice::ReadOnly);
            check("the .pub holds the public line",
                  QString::fromUtf8(pubFile.readAll()).trimmed() == pub);
            pubFile.close();
            QFile privFile(path);
            privFile.open(QIODevice::ReadOnly);
            check("the private file holds the PEM",
                  QString::fromUtf8(privFile.readAll()) == pem);
            privFile.close();
#ifndef Q_OS_WIN
            const QFile::Permissions perms = QFile::permissions(path);
            const QFile::Permissions others =
                QFile::ReadGroup | QFile::WriteGroup | QFile::ExeGroup |
                QFile::ReadOther | QFile::WriteOther | QFile::ExeOther;
            check("the private key is 0600 — nobody else can read it",
                  int(perms & others) == 0 &&
                      perms.testFlag(QFile::ReadUser) && perms.testFlag(QFile::WriteUser),
                  QString::number(int(perms), 16));
            check("the .pub is readable by others",
                  QFile::permissions(path + QStringLiteral(".pub"))
                      .testFlag(QFile::ReadOther));
#endif
            // An existing path must be refused, not overwritten.
            const QString taken = keyDir.path() + QStringLiteral("/already_here");
            QFile sentinel(taken);
            sentinel.open(QIODevice::WriteOnly);
            sentinel.write("do not touch\n");
            sentinel.close();
            const bool refused = !panel.writeSshKeyTo(taken);
            check("saving over an existing file is refused", refused);
            QFile back(taken);
            back.open(QIODevice::ReadOnly);
            check("and the existing file is untouched",
                  back.readAll() == QByteArray("do not touch\n"));
            back.close();
            check("no .pub was created for the refused path",
                  !QFile::exists(taken + QStringLiteral(".pub")));
        }

        // ── a changed setting clears the key rather than regenerating ──
        if (comment) comment->setText(QStringLiteral("something-else"));
        check("changing the comment clears the result",
              panel.currentText().isEmpty(), panel.currentText());
        check("and hides the private box again",
              privBox && !privBox->isVisibleTo(&panel));
        check("regenerate() never touches the SSH page",
              (panel.regenerate(), panel.currentText().isEmpty()));

        if (rail.size() == 3) rail.at(0)->click();
    }

    std::printf("\n=== Summary: %d passed, %d failed ===\n", g_pass, g_fail);
    return g_fail == 0 ? 0 : 1;
}
