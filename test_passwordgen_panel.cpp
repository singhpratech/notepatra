/**
 * Widget-level test for PasswordGenPanel.
 *
 * Offscreen-safe: constructs the panel, drives the real controls, and
 * asserts the behaviours that the core test cannot see — that the mode
 * radio swaps the two option groups, that an invalid configuration
 * disables the action buttons instead of producing a bad password, that
 * Copy actually reaches the clipboard, and that the timed clipboard wipe
 * only takes back a value the panel itself put there.
 *
 * Never opens a modal: a QMessageBox in an offscreen Windows CI job is a
 * known segfault, so this panel reports every problem through an inline
 * label instead.
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
#include <QSignalSpy>
#include <QSpinBox>
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

int main(int argc, char **argv) {
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

    std::printf("\n— mode switch ────────────────────────────────────\n");
    {
        auto *phrase = byText<QRadioButton>(&panel, QStringLiteral("Passphrase"));
        auto *chars  = byText<QRadioButton>(&panel, QStringLiteral("Random characters"));
        check("both mode radios exist", phrase && chars);
        if (phrase && chars) {
            auto *lower = byText<QCheckBox>(&panel, QStringLiteral("a-z"));
            auto *caps  = byText<QCheckBox>(&panel, QStringLiteral("Capitalise each word"));
            check("characters mode shows the character options",
                  lower && lower->isVisibleTo(&panel));
            check("characters mode hides the passphrase options",
                  caps && !caps->isVisibleTo(&panel));

            phrase->setChecked(true);
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

            chars->setChecked(true);
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
        auto *insert = byText<QPushButton>(&panel, QStringLiteral("Insert into editor"));
        auto *newTab = byText<QPushButton>(&panel, QStringLiteral("Open in new tab"));
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

    std::printf("\n=== Summary: %d passed, %d failed ===\n", g_pass, g_fail);
    return g_fail == 0 ? 0 : 1;
}
