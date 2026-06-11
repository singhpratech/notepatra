// D3 (win-open-ghost) — status-bar word-count amplifier. The old path ran
// 2x full-document text() + a QRegularExpression("\\s+") split on EVERY
// keystroke, file open, and tab switch (3 code copies). New contract: word
// count is per-Editor cached, recomputed lazily on a 300 ms debounce by one
// linear byte scan, suppressed (em dash) above 2 MiB. Sections 6/7 are the
// perf pins — correctness sections pass on both broken and fixed code.
// No modal is ever driven (all files are far below the large-file prompt),
// so no winOffscreenModalUnsafe() guard is needed. Fully offline.

#include "mainwindow.h"
#include "editor.h"
#include "tabmanager.h"
#include "config.h"
#include "statusbar.h"

#include <QApplication>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QFile>
#include <QFileInfo>
#include <QLabel>
#include <QTemporaryDir>

#include <cstdio>

static int g_pass = 0, g_fail = 0;
#define EXPECT(label, cond) \
    do { if (cond) { ++g_pass; std::printf("  [PASS] %s\n", label); } \
         else      { ++g_fail; std::printf("  [FAIL] %s\n", label); } } while (0)

static void pumpMs(int ms) {
    QElapsedTimer t;
    t.start();
    while (t.elapsed() < ms)
        QApplication::processEvents(QEventLoop::AllEvents, 10);
}

static QLabel *sizeLabel(NppStatusBar *bar) {
    const auto labels = bar->findChildren<QLabel *>();
    for (QLabel *l : labels)
        if (l->text().contains("length :")) return l;
    return nullptr;
}

static int tabIndexForName(TabManager *tabs, const QString &name) {
    for (int i = 0; i < tabs->count(); ++i) {
        Editor *e = tabs->editorAt(i);
        if (e && QFileInfo(e->filePath()).fileName() == name) return i;
    }
    return -1;
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

    std::printf("=== test_statusbar_wordcount_perf ===\n\n");

    QTemporaryDir wd;

    // ── Section 3 — Editor unit contract (standalone, no MainWindow) ──
    std::printf("Section 3 — Editor cache unit contract\n");
    {
        Editor ed;
        ed.setText("one  two\tthree\n\nfour");
        EXPECT("recompute counts 4 words", ed.recomputeWordCount() == 4);
        EXPECT("cache clean after recompute", !ed.wordCountDirty());
        EXPECT("lastWordCount serves the cache", ed.lastWordCount() == 4);
        ed.setText("a b");
        EXPECT("setText dirties the cache", ed.wordCountDirty());
        EXPECT("recompute after edit counts 2", ed.recomputeWordCount() == 2);

        Editor ed2;
        EXPECT("fresh editor reports -1 (unknown)", ed2.lastWordCount() == -1);
    }

    // ── Section 4 — threshold suppression (unit) ──
    std::printf("\nSection 4 — > 2 MiB suppression\n");
    {
        Editor ed;
        const QString big = QStringLiteral("word ").repeated(600000);  // 3 MB
        ed.setText(big);
        EXPECT("recompute over threshold returns -1",
               ed.recomputeWordCount() == -1);
        EXPECT("suppressed result is cached (not dirty)", !ed.wordCountDirty());
    }

    // ── Section 5 — sub-threshold recompute is linear and fast ──
    std::printf("\nSection 5 — 1.5 MB recompute speed\n");
    {
        Editor ed;
        ed.setText(QStringLiteral("ab ").repeated(500000));  // 1.5 MB
        QElapsedTimer t;
        t.start();
        const int w = ed.recomputeWordCount();
        const qint64 elapsed = t.elapsed();
        std::printf("  recompute(1.5MB) took %lld ms\n",
                    static_cast<long long>(elapsed));
        EXPECT("counts 500000 words", w == 500000);
        EXPECT("linear scan under 500 ms", elapsed < 500);
    }

    // ── Integration sections — MainWindow + status bar ──
    MainWindow mw;
    auto *tm  = mw.findChild<TabManager *>();
    auto *bar = mw.findChild<NppStatusBar *>();
    EXPECT("TabManager found", tm != nullptr);
    EXPECT("NppStatusBar found", bar != nullptr);
    if (!tm || !bar) { std::printf("\n%d passed, %d failed\n", g_pass, g_fail); return 1; }
    mw.show();
    pumpMs(100);  // deferred startup (D1); empty session = no-op

    // ── Section 1 — correctness parity with the old \s+ split ──
    std::printf("\nSection 1 — small-doc correctness\n");
    const QString smallTxt = wd.path() + "/small.txt";
    {
        QFile f(smallTxt);
        f.open(QIODevice::WriteOnly);
        f.write("alpha bravo  charlie\n\tdelta\r\nepsilon\n");  // 5 words
    }
    mw.openFile(smallTxt);
    pumpMs(600);
    {
        QLabel *l = sizeLabel(bar);
        EXPECT("size label exists", l != nullptr);
        Editor *e = tm->currentEditor();
        EXPECT("small.txt is current",
               e && QFileInfo(e->filePath()).fileName() == "small.txt");
        if (l && e) {
            EXPECT("words : 5 after debounce", l->text().contains("words : 5"));
            EXPECT("lines field present", l->text().contains("lines :"));
            EXPECT("length is the byte count",
                   l->text().contains("length : " + QString::number(e->length())));
        }
    }

    // ── Section 2 — invalidation on edit ──
    std::printf("\nSection 2 — edit invalidates + refreshes\n");
    {
        Editor *e = tm->currentEditor();
        if (e) {
            e->setCursorPosition(0, 0);
            e->insert("zulu ");
            pumpMs(600);
            QLabel *l = sizeLabel(bar);
            EXPECT("words : 6 after insert", l && l->text().contains("words : 6"));
        }
    }

    // ── Section 4b — integration suppression renders an em dash ──
    std::printf("\nSection 4b — big doc renders em dash\n");
    const QString big3Txt = wd.path() + "/big3mb.txt";
    {
        QFile f(big3Txt);
        f.open(QIODevice::WriteOnly);
        const QByteArray chunk = QByteArray("word ").repeated(20000);  // 100 KB
        for (int i = 0; i < 30; ++i) f.write(chunk);                   // 3 MB
    }
    mw.openFile(big3Txt);
    pumpMs(600);
    {
        QLabel *l = sizeLabel(bar);
        EXPECT("words : \xE2\x80\x94 (em dash) for 3 MB doc",
               l && l->text().contains(QString("words : ") + QChar(0x2014)));
    }

    // ── Section 6 — PERF CEILING A: keystrokes on an 8 MB doc ──
    std::printf("\nSection 6 — 200 keystrokes on 8 MB doc\n");
    const QString big8Txt = wd.path() + "/big8mb.txt";
    {
        QFile f(big8Txt);
        f.open(QIODevice::WriteOnly);
        // Newline-terminated lines: a single 8 MB line would make the whole
        // document the lexer's visible range and measure Scintilla's
        // single-line layout instead of the word-count path.
        const QByteArray chunk =
            QByteArray("lorem ipsum dolor sit amet\n").repeated(4096);  // ~108 KB
        for (int i = 0; i < 76; ++i) f.write(chunk);                   // ~8 MB
    }
    mw.openFile(big8Txt);
    pumpMs(600);  // settle the open + debounce
    {
        Editor *e = tm->currentEditor();
        EXPECT("big8mb.txt is current",
               e && QFileInfo(e->filePath()).fileName() == "big8mb.txt");
        if (e) {
            e->setCursorPosition(0, 0);
            QElapsedTimer t;
            t.start();
            for (int i = 0; i < 200; ++i) {
                e->insert("x");
                QApplication::processEvents();
            }
            const qint64 elapsed = t.elapsed();
            std::printf("  200 keystrokes took %lld ms\n",
                        static_cast<long long>(elapsed));
            EXPECT("200 keystrokes under 3000 ms (old regex path >= 20 s)",
                   elapsed < 3000);
        }
    }

    // ── Section 7 — PERF CEILING B: tab switches onto the 8 MB doc ──
    std::printf("\nSection 7 — 100 tab switches with 8 MB tab\n");
    {
        const int bigIdx   = tabIndexForName(tm, "big8mb.txt");
        const int smallIdx = tabIndexForName(tm, "small.txt");
        EXPECT("both tabs present", bigIdx >= 0 && smallIdx >= 0);
        if (bigIdx >= 0 && smallIdx >= 0) {
            QElapsedTimer t;
            t.start();
            for (int i = 0; i < 50; ++i) {
                tm->setCurrentIndex(bigIdx);
                QApplication::processEvents();
                tm->setCurrentIndex(smallIdx);
                QApplication::processEvents();
            }
            const qint64 elapsed = t.elapsed();
            std::printf("  100 switches took %lld ms\n",
                        static_cast<long long>(elapsed));
            EXPECT("100 tab switches under 2000 ms (old path >= 10 s)",
                   elapsed < 2000);
        }
    }

    std::printf("\n%d passed, %d failed\n", g_pass, g_fail);
    return g_fail == 0 ? 0 : 1;
}
