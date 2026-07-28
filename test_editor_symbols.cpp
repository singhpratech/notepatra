// SPDX-License-Identifier: GPL-3.0-or-later
//
// Non-printing character display, pinned against the two ways it can rot.
//
// 1. TABLE DRIFT. The abbreviations are Notepad++'s, and their whole value is
//    that they match — someone comparing the two editors should see the same
//    four letters. A "tidy-up" that renamed ZWNBSP to BOM would look harmless
//    and would silently end parity.
//
// 2. SILENT NON-RENDERING. This is the one that matters. Every interesting
//    failure in this feature is invisible by construction: the representation
//    round-trips through SCI_GETREPRESENTATION perfectly while drawing nothing
//    at all. So the assertions below measure LAID-OUT PIXEL WIDTH via
//    SCI_POINTXFROMPOSITION rather than asking Scintilla what it stored.
//    SCI_TEXTWIDTH is not usable here — it measures raw glyphs and ignores
//    representations entirely, so it reports 0 for a perfectly visible ZWSP.

#include <QtTest>
#include <QApplication>
#include <Qsci/qsciscintilla.h>

#include <QSet>

#include "editor_symbols.h"

using namespace EditorSymbols;

class TestEditorSymbols : public QObject {
    Q_OBJECT

    QsciScintilla *m_sci = nullptr;

    // Width of the whole (single) line, in pixels, after forcing layout.
    long lineWidth() {
        QApplication::processEvents();
        const long len = m_sci->SendScintilla(QsciScintillaBase::SCI_GETLENGTH);
        return m_sci->SendScintilla(QsciScintillaBase::SCI_POINTXFROMPOSITION,
                                    (unsigned long)0, (long)len)
             - m_sci->SendScintilla(QsciScintillaBase::SCI_POINTXFROMPOSITION,
                                    (unsigned long)0, (long)0);
    }

    // Built via QChar rather than QString::fromUcs4, which swallows a lone
    // U+FEFF as a byte-order mark and would hand the editor "ab" — making the
    // ZWNBSP case look invisible no matter what the code under test does.
    // Every codepoint in the table is inside the BMP.
    static QString charFor(char32_t cp) { return QString(QChar(ushort(cp))); }

    // Width contributed by `cp` when sandwiched between two plain letters.
    long widthOf(char32_t cp) {
        m_sci->setText(QStringLiteral("ab"));
        forceRelayout(m_sci);
        const long without = lineWidth();

        m_sci->setText(QStringLiteral("a") + charFor(cp) + QStringLiteral("b"));
        forceRelayout(m_sci);
        return lineWidth() - without;
    }

private slots:

    void initTestCase() {
        m_sci = new QsciScintilla;
        m_sci->setUtf8(true);
        m_sci->resize(1200, 400);
        m_sci->show();
        QApplication::processEvents();
    }
    void cleanupTestCase() { delete m_sci; m_sci = nullptr; }

    void init() { apply(m_sci, NoSymbols); }

    // ── the measuring instrument itself ───────────────────────────────────
    //
    // Every negative assertion below ("width is 0") is worthless if the
    // measurement silently returns 0 for everything. Prove it can see a
    // character at all before trusting it to report an absence.
    void theWidthProbeCanActuallySeeCharacters() {
        QVERIFY2(widthOf(U'W') > 0, "the pixel probe reports 0 for a plain 'W' — "
                                    "every other assertion in this file is vacuous");
    }

    // ── table integrity ───────────────────────────────────────────────────
    void theTablesMatchNotepadPlusPlusRowCounts() {
        int npc = 0, cc = 0;
        for (const Entry &e : table()) {
            if (e.category == NonPrinting)                 ++npc;
            else if (e.category == ControlAndUnicodeEol)   ++cc;
        }
        QCOMPARE(npc, 49);   // g_nonPrintingChars
        QCOMPARE(cc,  64);   // g_ccUniEolChars: 30 C0 + 31 C1 + 3 Unicode EOL
    }

    void theControlTableSplitsIntoThirtyThirtyOneAndThree() {
        int c0 = 0, c1 = 0, eol = 0;
        for (const Entry &e : table()) {
            if (e.group == GroupC0)              ++c0;
            else if (e.group == GroupC1)         ++c1;
            else if (e.group == GroupUnicodeEol) ++eol;
        }
        QCOMPARE(c0, 30);
        QCOMPARE(c1, 31);
        QCOMPARE(eol, 3);
    }

    void noCodepointAppearsTwice() {
        QSet<char32_t> seen;
        for (const Entry &e : table()) {
            QVERIFY2(!seen.contains(e.codepoint),
                     qPrintable(QStringLiteral("U+%1 listed twice")
                                    .arg(uint(e.codepoint), 4, 16, QLatin1Char('0'))));
            seen.insert(e.codepoint);
        }
    }

    // Giving TAB/LF/CR a representation would hijack them from the
    // "Show Space and Tab" and "Show End of Line" toggles and break both.
    void tabLineFeedAndCarriageReturnAreNotInTheTable() {
        for (const Entry &e : table()) {
            QVERIFY2(e.codepoint != 0x09 && e.codepoint != 0x0A && e.codepoint != 0x0D,
                     qPrintable(QStringLiteral("U+%1 must be left to the whitespace/EOL toggles")
                                    .arg(uint(e.codepoint), 4, 16, QLatin1Char('0'))));
        }
    }

    void abbreviationsAreNotepadPlusPlusSpelling_data() {
        QTest::addColumn<uint>("cp");
        QTest::addColumn<QString>("abbrev");
        QTest::newRow("ZWSP")   << 0x200Bu << "ZWSP";
        QTest::newRow("ZWNBSP") << 0xFEFFu << "ZWNBSP";  // NOT "BOM"
        QTest::newRow("OSPM")   << 0x1680u << "OSPM";    // NOT "OSM"
        QTest::newRow("SGCI")   << 0x0099u << "SGCI";    // NOT "SGC"
        QTest::newRow("3/MSP")  << 0x2004u << "3/MSP";
        QTest::newRow("IDSP")   << 0x3000u << "IDSP";
        QTest::newRow("NBSP")   << 0x00A0u << "NBSP";
        QTest::newRow("DEL")    << 0x007Fu << "DEL";
        QTest::newRow("NEL")    << 0x0085u << "NEL";
        QTest::newRow("PDI")    << 0x2069u << "PDI";
    }
    void abbreviationsAreNotepadPlusPlusSpelling() {
        QFETCH(uint, cp);
        QFETCH(QString, abbrev);
        for (const Entry &e : table()) {
            if (uint(e.codepoint) == cp) {
                QCOMPARE(QString::fromLatin1(e.abbreviation), abbrev);
                return;
            }
        }
        QFAIL(qPrintable(QStringLiteral("U+%1 missing from the table")
                             .arg(cp, 4, 16, QLatin1Char('0'))));
    }

    void utf8EncodingIsCorrect() {
        QCOMPARE(utf8Of(0x200B), QByteArray("\xE2\x80\x8B"));
        QCOMPARE(utf8Of(0x00A0), QByteArray("\xC2\xA0"));
        QCOMPARE(utf8Of(0xFEFF), QByteArray("\xEF\xBB\xBF"));
        QCOMPARE(utf8Of(0x07),   QByteArray("\x07"));
    }

    // ── THE REPORTED BUG ──────────────────────────────────────────────────
    //
    // "there is symbol zwsp and the file says zwsp in notepad++ and my
    //  notepatra is not able to catch it"
    void zeroWidthSpaceIsInvisibleUntilTheFeatureIsOn() {
        apply(m_sci, NoSymbols);
        QCOMPARE(widthOf(0x200B), 0L);          // the bug, as it shipped

        apply(m_sci, NonPrinting);
        QVERIFY2(widthOf(0x200B) > 0,
                 "U+200B still renders as nothing with Show Non-Printing Characters on");
    }

    void everyNonPrintingCharacterBecomesVisible() {
        apply(m_sci, NonPrinting);
        QStringList invisible;
        for (const Entry &e : table()) {
            if (e.category != NonPrinting) continue;
            if (widthOf(e.codepoint) <= 0)
                invisible << QStringLiteral("U+%1 (%2)")
                                 .arg(uint(e.codepoint), 4, 16, QLatin1Char('0'))
                                 .arg(QLatin1String(e.abbreviation));
        }
        QVERIFY2(invisible.isEmpty(),
                 qPrintable(QStringLiteral("these draw nothing: ") + invisible.join(", ")));
    }

    // ── the layout-cache trap ─────────────────────────────────────────────
    //
    // SCI_SETREPRESENTATION does not invalidate the line-layout cache in this
    // Scintilla. Without forceRelayout(), toggling the menu on an already-open
    // file appears to do nothing until the user reloads — the single most
    // likely way for this feature to ship broken.
    void togglingWorksOnADocumentThatIsAlreadyOpen() {
        apply(m_sci, NoSymbols);

        m_sci->setText(QStringLiteral("a") + charFor(0x200B) + QStringLiteral("b"));
        forceRelayout(m_sci);
        const long before = lineWidth();

        // The document is laid out. NOW turn the feature on, as a user would.
        apply(m_sci, NonPrinting);
        const long after = lineWidth();

        QVERIFY2(after > before,
                 "turning the feature on did not affect a document that was already "
                 "laid out — the layout cache was not invalidated");
    }

    void turningItBackOffRestoresTheOriginalRendering() {
        apply(m_sci, NonPrinting);
        QVERIFY(widthOf(0x200B) > 0);
        apply(m_sci, NoSymbols);
        QCOMPARE(widthOf(0x200B), 0L);
    }

    // ── the segfault ──────────────────────────────────────────────────────
    //
    // SCI_CLEARREPRESENTATION reads its string from wParam. The natural
    // SendScintilla(msg, str) overload puts it in lParam and leaves wParam
    // null, and Scintilla dereferences it. This test is only meaningful
    // because it would take the whole process down rather than fail.
    void clearingRepresentationsDoesNotCrash() {
        for (int i = 0; i < 3; ++i) {
            apply(m_sci, allCategories());
            apply(m_sci, NoSymbols);
        }
        QVERIFY(true);   // reaching this line IS the assertion
    }

    // ── control characters ────────────────────────────────────────────────
    void controlCharactersAreVisibleByDefaultAndCanBeSuppressed() {
        apply(m_sci, ControlAndUnicodeEol);
        const long on = widthOf(0x07);          // BEL
        QVERIFY2(on > 0, "BEL draws nothing with the control-character category on");

        apply(m_sci, NoSymbols);
        const long off = widthOf(0x07);
        QVERIFY2(off < on,
                 "turning control characters off did not shrink BEL — Scintilla's "
                 "built-in mnemonic was not overridden");
    }

    // ── the toggles must stay independent ─────────────────────────────────
    void theNonPrintingToggleDoesNotDisturbTabsOrNewlines() {
        m_sci->setWhitespaceVisibility(QsciScintilla::WsVisible);
        m_sci->setText(QStringLiteral("a\tb"));
        forceRelayout(m_sci);
        const long tabbed = lineWidth();

        apply(m_sci, allCategories());
        m_sci->setText(QStringLiteral("a\tb"));
        forceRelayout(m_sci);
        QCOMPARE(lineWidth(), tabbed);
    }
};

QTEST_MAIN(TestEditorSymbols)
#include "test_editor_symbols.moc"
