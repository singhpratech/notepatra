// SPDX-License-Identifier: GPL-3.0-or-later
//
// Indent guides, pinned against the way they silently rot.
//
// STYLE_INDENTGUIDE is a RESERVED Scintilla style (37). The indexed
// lexer->setPaper(c, i) / setColor(c, i) forms that npp_palette.cpp uses only
// reach lexer styles 0-31, so a reserved style stays at Scintilla's built-in
// black-on-WHITE unless something styles it explicitly. A guide paints its own
// background, so an unstyled style 37 draws a pure-white 1px column down
// #1E1E1E paper — and indent guides default to ON, so it shipped to every
// dark-theme user.
//
// Two things are asserted, and the FIRST one is the actual bug:
//   • BACK must equal the theme's editor paper, or the guide's background
//     leaks a column of the wrong colour.
//   • FORE must be the theme-derived blend, not the raw default black.
//
// SCI_STYLEGETFORE / SCI_STYLEGETBACK answer in Scintilla's BGR packing, not
// RGB, so every read below is unpacked by hand — Monokai's editorBg (#272822)
// is the one theme colour where a swapped byte order would still be caught.

#include <QtTest>
#include <QApplication>
#include <QTemporaryDir>
#include <Qsci/qsciscintilla.h>
#include <Qsci/qsciscintillabase.h>

#include "editor.h"
#include "themes.h"

namespace {

QColor unpackBgr(long packed) {
    return QColor(int(packed & 0xFF),
                  int((packed >> 8) & 0xFF),
                  int((packed >> 16) & 0xFF));
}

QColor guideBack(const QsciScintillaBase *sci) {
    return unpackBgr(sci->SendScintilla(QsciScintillaBase::SCI_STYLEGETBACK,
                                        (unsigned long)QsciScintillaBase::STYLE_INDENTGUIDE));
}

QColor guideFore(const QsciScintillaBase *sci) {
    return unpackBgr(sci->SendScintilla(QsciScintillaBase::SCI_STYLEGETFORE,
                                        (unsigned long)QsciScintillaBase::STYLE_INDENTGUIDE));
}

}  // namespace

class TestIndentGuideTheme : public QObject {
    Q_OBJECT

private slots:
    void everyThemeStylesTheGuide_data() {
        QTest::addColumn<QString>("themeName");
        QTest::newRow("Light")   << QStringLiteral("Light");
        QTest::newRow("Dark")    << QStringLiteral("Dark");
        QTest::newRow("Monokai") << QStringLiteral("Monokai");
    }

    void everyThemeStylesTheGuide() {
        QFETCH(QString, themeName);
        const Theme theme = allThemes()[themeName];

        Editor ed;
        ed.applyTheme(themeName);

        QCOMPARE(guideBack(&ed), theme.editorBg);
        QCOMPARE(guideFore(&ed), indentGuideColor(theme));

        QVERIFY2(guideFore(&ed) != QColor(Qt::black),
                 "indent guide left at Scintilla's default black foreground");
        QVERIFY2(guideFore(&ed) != theme.editorBg,
                 "indent guide is invisible — foreground equals the paper");
        QVERIFY2(guideFore(&ed) != theme.editorFg,
                 "indent guide is as loud as the text it sits behind");
    }

    // Vacuity guard: without this the assertions above could pass on a build
    // where Scintilla's own defaults happened to match. A bare QsciScintilla
    // is the untouched baseline, and the dark themes must not look like it.
    void theStyledGuideDiffersFromScintillasDefault() {
        QsciScintilla bare;
        const QColor defaultBack = guideBack(&bare);
        QCOMPARE(defaultBack, QColor(Qt::white));

        for (const QString &name : {QStringLiteral("Dark"), QStringLiteral("Monokai")}) {
            Editor ed;
            ed.applyTheme(name);
            QVERIFY2(guideBack(&ed) != defaultBack,
                     qPrintable(name + ": guide background still Scintilla's white"));
        }
    }

    // Theme switching, not just first application — a guide styled once at
    // construction and never again would leave the old paper behind.
    void switchingThemesRestylesTheGuide() {
        Editor ed;
        for (const QString &name : {QStringLiteral("Dark"), QStringLiteral("Light"),
                                    QStringLiteral("Monokai"), QStringLiteral("Dark")}) {
            ed.applyTheme(name);
            const Theme theme = allThemes()[name];
            QCOMPARE(guideBack(&ed), theme.editorBg);
            QCOMPARE(guideFore(&ed), indentGuideColor(theme));
        }
    }
};

int main(int argc, char *argv[]) {
    // Isolate config: Editor's constructor reads the global Config, and
    // QStandardPaths would otherwise point at the user's real settings.
    QTemporaryDir cfg;
    qputenv("XDG_CONFIG_HOME", cfg.path().toUtf8());
    qputenv("XDG_DATA_HOME",   cfg.path().toUtf8());
    qputenv("QT_QPA_PLATFORM", "offscreen");

    QApplication app(argc, argv);
    TestIndentGuideTheme tc;
    return QTest::qExec(&tc, argc, argv);
}

#include "test_indent_guide_theme.moc"
