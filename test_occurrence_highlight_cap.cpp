// D8 Part C — occurrence-highlight (double-click word) caps.
//
// The old Editor::highlightAllOccurrences extracted the document THREE times
// (text().toUtf8() for the clear range + text() again for findAll) and ran
// an UNCAPPED SCI_INDICATORFILLRANGE loop — Scintilla's RunStyles insertion
// per discontiguous range is the super-linear part, so a double-click on a
// common word in a multi-MB doc froze the UI for seconds-to-minutes.
//
// New contract (now public so this test can drive it directly):
//   - clears indicator 9 over the whole doc FIRST (no stale highlights),
//   - returns without highlighting when doc > 4 MB (kOccurrenceMaxDocBytes),
//   - otherwise fills at most 5000 matches (kOccurrenceMaxMatches).
//
// C2/C3's QElapsedTimer ceilings are the load-bearing pins — the correctness
// asserts alone pass on the broken code. Indicator state is read straight
// through QsciScintillaBase::SendScintilla (public), no modal, fully offline.

#include "editor.h"

#include <QApplication>
#include <QElapsedTimer>
#include <QTemporaryDir>

#include <cstdio>

static int g_pass = 0, g_fail = 0;
#define EXPECT(label, cond) \
    do { if (cond) { ++g_pass; std::printf("  [PASS] %s\n", label); } \
         else      { ++g_fail; std::printf("  [FAIL] %s\n", label); } \
         std::fflush(stdout); } while (0)

// Indicator-9 value at a byte position (non-zero = highlighted).
static long indVal(Editor &ed, long pos) {
    return ed.SendScintilla(QsciScintillaBase::SCI_INDICATORVALUEAT,
                            static_cast<unsigned long>(9), pos);
}

int main(int argc, char *argv[]) {
    QTemporaryDir cfg;
    qputenv("XDG_CONFIG_HOME", cfg.path().toUtf8());
    qputenv("XDG_DATA_HOME",   cfg.path().toUtf8());
#ifdef Q_OS_MAC
    qputenv("HOME", cfg.path().toUtf8());
#endif
#ifdef Q_OS_WIN
    qputenv("APPDATA",     cfg.path().toUtf8());
    qputenv("USERPROFILE", cfg.path().toUtf8());
#endif
    qputenv("QT_QPA_PLATFORM", "offscreen");

    QApplication app(argc, argv);

    std::printf("=== test_occurrence_highlight_cap ===\n\n");

    Editor ed;

    // ── C1 — correctness below both thresholds (semantics unchanged) ──
    std::printf("C1 — small-doc correctness\n");
    {
        //            0123456789012345678
        ed.setText(QStringLiteral("foo bar foo baz foo"));
        ed.highlightAllOccurrences(QStringLiteral("foo"));
        EXPECT("C1: byte 0 highlighted",  indVal(ed, 0)  != 0);
        EXPECT("C1: byte 8 highlighted",  indVal(ed, 8)  != 0);
        EXPECT("C1: byte 16 highlighted", indVal(ed, 16) != 0);
        EXPECT("C1: byte 4 ('bar') not highlighted", indVal(ed, 4) == 0);

        // Re-run with a different word: old ranges must be cleared first.
        ed.highlightAllOccurrences(QStringLiteral("bar"));
        EXPECT("C1: re-run clears the old 'foo' range at byte 0",
               indVal(ed, 0) == 0);
        EXPECT("C1: re-run fills the new 'bar' range at byte 4",
               indVal(ed, 4) != 0);
    }

    // ── C2 — doc-size gate + PERF CEILING ──
    // ~6 MB doc (> 4 MB gate) with ~300k "alpha" matches. Broken code: three
    // 6 MB document copies + ~300k uncapped RunStyles fills — seconds to
    // minutes. Fixed code: SCI_GETLENGTH + SCI_INDICATORCLEARRANGE + return.
    // The 300 ms ceiling is the contract (QElapsedTimer rule).
    const QString big6 = QStringLiteral("alpha bravo charlie\n")
                             .repeated(300000);  // 20 B × 300k = 6,000,000 B
    std::printf("\nC2 — > 4 MB doc-size gate\n");
    {
        ed.setText(big6);
        QElapsedTimer t;
        t.start();
        ed.highlightAllOccurrences(QStringLiteral("alpha"));
        const qint64 elapsed = t.elapsed();
        std::printf("  highlight on 6 MB doc took %lld ms\n",
                    static_cast<long long>(elapsed));
        std::fflush(stdout);
        EXPECT("C2 PERF: > 4 MB highlight under 300 ms "
               "(broken path copies the doc 3x + 300k fills)",
               elapsed < 300);
        EXPECT("C2: byte 0 NOT highlighted (gate skipped highlighting)",
               indVal(ed, 0) == 0);
    }

    // ── C3 — match cap below the doc gate + PERF CEILING ──
    // ~3.6 MB (< 4 MB gate) of "ab " ×1.2M — 1.2M whole-word matches, match
    // k starting at byte 3k. Broken code performs 1.2M discontiguous
    // INDICATORFILLRANGE inserts — far above the 1500 ms ceiling. Fixed code
    // stops filling at kOccurrenceMaxMatches = 5000.
    std::printf("\nC3 — 5000-match cap\n");
    {
        ed.setText(QStringLiteral("ab ").repeated(1200000));  // 3,600,000 B
        QElapsedTimer t;
        t.start();
        ed.highlightAllOccurrences(QStringLiteral("ab"));
        const qint64 elapsed = t.elapsed();
        std::printf("  highlight of 1.2M matches took %lld ms\n",
                    static_cast<long long>(elapsed));
        std::fflush(stdout);
        EXPECT("C3 PERF: 1.2M-match doc under 1500 ms "
               "(broken path fills 1.2M discontiguous ranges)",
               elapsed < 1500);
        EXPECT("C3: first match filled at byte 0", indVal(ed, 0) != 0);
        // Cap boundary, pinned exactly: match #5000 (index 4999) is the last
        // one filled; match #5001 (index 5000) must not be.
        EXPECT("C3: match #5000 (last under cap) filled at byte 3*4999",
               indVal(ed, 3 * 4999) != 0);
        EXPECT("C3: match #5001 (just past cap) NOT filled at byte 3*5000",
               indVal(ed, 3 * 5000) == 0);
        EXPECT("C3: match #300001 far past cap NOT filled at byte 900000",
               indVal(ed, 3 * 300000) == 0);
    }

    // ── C4 — stale-clear above the gate (clear-before-gate ordering) ──
    std::printf("\nC4 — stale highlight cleared above the gate\n");
    {
        ed.setText(QStringLiteral("foo bar foo"));
        ed.highlightAllOccurrences(QStringLiteral("foo"));
        EXPECT("C4: precondition — small-doc highlight at byte 0",
               indVal(ed, 0) != 0);

        // Swap in the 6 MB doc. setText replaces the document and drops its
        // decorations with the old text, so re-seed a stale indicator-9
        // range by hand — this is what a leftover highlight on an
        // already-huge doc looks like. Without the re-seed the byte-0 assert
        // below would pass vacuously even if the clear-before-gate ordering
        // regressed.
        ed.setText(big6);
        ed.SendScintilla(QsciScintillaBase::SCI_SETINDICATORCURRENT,
                         static_cast<unsigned long>(9));
        ed.SendScintilla(QsciScintillaBase::SCI_INDICATORFILLRANGE,
                         static_cast<unsigned long>(0),
                         static_cast<long>(32));
        EXPECT("C4: stale indicator seeded on the 6 MB doc",
               indVal(ed, 0) != 0);

        // The > 4 MB path must STILL clear before bailing out.
        ed.highlightAllOccurrences(QStringLiteral("alpha"));
        EXPECT("C4: over-gate call clears the stale highlight at byte 0",
               indVal(ed, 0) == 0);
    }

    std::printf("\n%d passed, %d failed\n", g_pass, g_fail);
    std::fflush(stdout);
    return g_fail;
}
