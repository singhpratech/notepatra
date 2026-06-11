// D4 (win-open-ghost) — bulk setText guard. A wholesale programmatic
// setText() of an N-line doc used to replay the per-line change-history loop
// (markerDelete(22)/markerAdd(23) + QSet churn = ~4 Scintilla calls/line) in
// onScintillaModified — O(N) GUI-thread stall on session restore of a big
// modified tab. Contract: Editor::ScopedBulkLoad (layer 1) suppresses the
// churn and resets history; a >1000-linesAdded threshold (layer 2) catches
// unguarded bulk inserts. No modal is ever driven (checkCrashRecovery is not
// exercised: fresh temp config has no .crash_flag). Fully offline.

#include "mainwindow.h"
#include "editor.h"
#include "tabmanager.h"
#include "config.h"

#include <QApplication>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>

#include <cstdio>

static int g_pass = 0, g_fail = 0;
#define EXPECT(label, cond) \
    do { if (cond) { ++g_pass; std::printf("  [PASS] %s\n", label); } \
         else      { ++g_fail; std::printf("  [FAIL] %s\n", label); } } while (0)

static unsigned markersAt(Editor &e, int line) {
    return (unsigned)e.SendScintilla(QsciScintillaBase::SCI_MARKERGET,
                                      (unsigned long)line);
}

static constexpr unsigned kOrange = 1u << 23;
static constexpr unsigned kGreen  = 1u << 22;

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

    std::printf("=== test_bulk_settext_guard ===\n\n");

    QString big;
    big.reserve(300000 * 10 + 16);
    for (int i = 0; i < 300000; ++i) big += QStringLiteral("line 0123\n");

    // ── Section 1 — ScopedBulkLoad semantics + perf ceiling ──
    std::printf("Section 1 — ScopedBulkLoad on a 300k-line doc\n");
    {
        Editor e;
        e.setText("a\nb\nc\n");  // small unguarded edit seeds dirty state
        EXPECT("sanity: small setText tracked per-line",
               e.modifiedLineCount() > 0);

        QElapsedTimer t;
        t.start();
        { Editor::ScopedBulkLoad g(&e); e.setText(big); }
        const qint64 guardedMs = t.elapsed();
        std::printf("  guarded setText(300k lines) took %lld ms\n",
                    static_cast<long long>(guardedMs));
        EXPECT("guarded bulk setText under 1500 ms", guardedMs < 1500);
        EXPECT("modifiedLineCount reset to 0", e.modifiedLineCount() == 0);
        EXPECT("savedLineCount reset to 0", e.savedLineCount() == 0);
        EXPECT("no history markers on line 0",
               (markersAt(e, 0) & (kOrange | kGreen)) == 0);
        EXPECT("no history markers on line 12345",
               (markersAt(e, 12345) & (kOrange | kGreen)) == 0);
        EXPECT("no history markers on line 299999",
               (markersAt(e, 299999) & (kOrange | kGreen)) == 0);

        e.setModified(true);
        EXPECT("isModified(true) contract for restore overlay",
               e.isModified());

        // Tracking resumes after the guard.
        e.setCursorPosition(5, 0);
        e.insert("X");
        EXPECT("post-guard edit paints orange on line 5",
               (markersAt(e, 5) & kOrange) != 0);
        EXPECT("post-guard modifiedLineCount == 1",
               e.modifiedLineCount() == 1);
    }

    // ── Section 2 — layer-2 threshold catches UNGUARDED bulk setText ──
    std::printf("\nSection 2 — raw 300k-line setText hits the threshold\n");
    {
        Editor e2;
        QElapsedTimer t2;
        t2.start();
        e2.setText(big);
        const qint64 rawMs = t2.elapsed();
        std::printf("  raw setText(300k lines) took %lld ms\n",
                    static_cast<long long>(rawMs));
        EXPECT("raw bulk setText under 1500 ms", rawMs < 1500);
        EXPECT("threshold reset: modifiedLineCount == 0",
               e2.modifiedLineCount() == 0);
        EXPECT("no markers at 0/150000/299999",
               (markersAt(e2, 0) & (kOrange | kGreen)) == 0 &&
               (markersAt(e2, 150000) & (kOrange | kGreen)) == 0 &&
               (markersAt(e2, 299999) & (kOrange | kGreen)) == 0);

        e2.setCursorPosition(7, 0);
        e2.insert("Y");
        EXPECT("small edit after threshold reset tracks normally",
               (markersAt(e2, 7) & kOrange) != 0 &&
               e2.modifiedLineCount() == 1);

        Editor e3;
        e3.setText(QStringLiteral("x\n").repeated(10));
        EXPECT("below-threshold setText still tracked per-line",
               e3.modifiedLineCount() > 0);
    }

    // ── Section 3 — reloadWithEncoding ends fully clean ──
    std::printf("\nSection 3 — reloadWithEncoding clean end-state\n");
    QTemporaryDir wd;
    {
        const QString p = wd.path() + "/two_thousand.txt";
        {
            QFile f(p);
            f.open(QIODevice::WriteOnly);
            for (int i = 0; i < 2000; ++i)
                f.write(QString("ascii line %1\n").arg(i).toUtf8());
        }
        Editor e4;
        EXPECT("loadFile succeeded", e4.loadFile(p));
        e4.setCursorPosition(1, 0);
        e4.insert("Z");
        EXPECT("pre-reload: one modified line",
               e4.modifiedLineCount() == 1 &&
               (markersAt(e4, 1) & kOrange) != 0);

        EXPECT("reloadWithEncoding succeeded",
               e4.reloadWithEncoding("UTF-8", true));
        EXPECT("reload leaves doc unmodified", !e4.isModified());
        EXPECT("reload clears modified count", e4.modifiedLineCount() == 0);
        EXPECT("reload clears saved count", e4.savedLineCount() == 0);
        EXPECT("no markers at 0/1/1000/1999",
               (markersAt(e4, 0) & (kOrange | kGreen)) == 0 &&
               (markersAt(e4, 1) & (kOrange | kGreen)) == 0 &&
               (markersAt(e4, 1000) & (kOrange | kGreen)) == 0 &&
               (markersAt(e4, 1999) & (kOrange | kGreen)) == 0);
    }

    // ── Section 4 — restore-overlay sites (small buffers: layer 2 can't
    //    mask a missing layer-1 guard) ──
    std::printf("\nSection 4 — session-restore overlay is history-clean\n");
    {
        const QString alphaPath = wd.path() + "/alpha.txt";
        {
            QFile f(alphaPath);
            f.open(QIODevice::WriteOnly);
            f.write("DISK L0\nDISK L1\n");
        }
        const QString alphaUnsaved = "EDITED L0\nEDITED L1\n";
        QString untitledUnsaved;
        for (int i = 0; i < 20; ++i)
            untitledUnsaved += QString("untitled line %1\n").arg(i);

        auto makeTab = [](const QString &path, const QString &unsaved,
                          const QString &tabName) {
            QJsonObject t;
            t["path"] = path;
            t["tabName"] = tabName;
            t["line"] = 0;
            t["col"] = 0;
            t["active"] = false;
            t["modified"] = true;
            t["unsavedContent"] = unsaved;
            return t;
        };
        QJsonArray tabs;
        tabs.append(makeTab(alphaPath, alphaUnsaved, "alpha.txt"));
        tabs.append(makeTab("", untitledUnsaved, "scratch"));
        QJsonObject session;
        session["tabs"] = tabs;
        const QString sessionPath = Config::appConfigDir() + "/session.json";
        {
            QFile sf(sessionPath);
            sf.open(QIODevice::WriteOnly);
            sf.write(QJsonDocument(session).toJson());
        }

        MainWindow mw;
        mw.runStartupNow();
        QApplication::processEvents();
        auto *tm = mw.findChild<TabManager *>();
        EXPECT("TabManager found", tm != nullptr);

        Editor *alpha = nullptr, *scratch = nullptr;
        for (int i = 0; tm && i < tm->count(); ++i) {
            Editor *e = tm->editorAt(i);
            if (!e) continue;
            if (QFileInfo(e->filePath()).fileName() == "alpha.txt") alpha = e;
            else if (e->filePath().isEmpty() && e->text() == untitledUnsaved)
                scratch = e;
        }
        EXPECT("alpha tab restored", alpha != nullptr);
        EXPECT("untitled tab restored", scratch != nullptr);
        if (alpha) {
            EXPECT("alpha holds its unsaved content",
                   alpha->text() == alphaUnsaved);
            EXPECT("alpha isModified()", alpha->isModified());
            EXPECT("alpha history counts are 0/0",
                   alpha->modifiedLineCount() == 0 &&
                   alpha->savedLineCount() == 0);
            EXPECT("alpha lines 0-2 marker-free",
                   (markersAt(*alpha, 0) & (kOrange | kGreen)) == 0 &&
                   (markersAt(*alpha, 1) & (kOrange | kGreen)) == 0 &&
                   (markersAt(*alpha, 2) & (kOrange | kGreen)) == 0);
        }
        if (scratch) {
            EXPECT("scratch isModified()", scratch->isModified());
            EXPECT("scratch history counts are 0/0",
                   scratch->modifiedLineCount() == 0 &&
                   scratch->savedLineCount() == 0);
            EXPECT("scratch lines 0-2 marker-free",
                   (markersAt(*scratch, 0) & (kOrange | kGreen)) == 0 &&
                   (markersAt(*scratch, 1) & (kOrange | kGreen)) == 0 &&
                   (markersAt(*scratch, 2) & (kOrange | kGreen)) == 0);
        }
    }

    std::printf("\n%d passed, %d failed\n", g_pass, g_fail);
    return g_fail == 0 ? 0 : 1;
}
