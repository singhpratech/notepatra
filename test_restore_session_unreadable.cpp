// v0.1.104 — restoreSession must not corrupt a saved file when a session tab
// points at an UNREADABLE file (perms 000 / root-owned / replaced by a dir).
//
// Bug (LOGIC-HIGH ship-blocker): restoreSession() called openFile(path) then
// read editorAt(count()-1) assuming a tab was appended. If the file exists()
// but is unreadable, openFile() early-returns WITHOUT adding a tab, so
// editorAt(count()-1) aliased the PREVIOUS restored editor. The modified
// branch then overwrote THAT prior tab's buffer with this entry's unsaved
// content and marked it modified — a later Save clobbered the wrong file.
//
// User-visible contract proven here: with a 3-tab session where tab #2 is an
// unreadable modified file, the unsaved content destined for tab #2 must NOT
// be written onto tab #1's buffer, and tab #1 must keep its own content (so a
// later Save of tab #1 writes the correct file, not gamma's content).

#include "mainwindow.h"
#include "editor.h"
#include "tabmanager.h"

#include <QApplication>
#include <QDir>
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

// Locate the restored tab whose filePath() ends with the given file name.
static Editor *tabForName(TabManager *tabs, const QString &name) {
    for (int i = 0; i < tabs->count(); ++i) {
        Editor *e = tabs->editorAt(i);
        if (e && QFileInfo(e->filePath()).fileName() == name) return e;
    }
    return nullptr;
}

int main(int argc, char *argv[]) {
    QTemporaryDir cfg;
    qputenv("XDG_CONFIG_HOME", cfg.path().toUtf8());
    qputenv("XDG_DATA_HOME",   cfg.path().toUtf8());
    qputenv("QT_QPA_PLATFORM", "offscreen");

    QApplication app(argc, argv);

    std::printf("=== test_restore_session_unreadable ===\n\n");

    // ── Three on-disk entries in a workspace dir ──
    QTemporaryDir wd;
    const QString p1 = wd.path() + "/alpha.txt";   // tab 1: modified, readable
    const QString p2 = wd.path() + "/beta.txt";    // tab 2: modified, UNREADABLE
    const QString p3 = wd.path() + "/gamma.txt";   // tab 3: modified, readable

    const QByteArray alphaDisk = "ALPHA DISK CONTENT\n";
    const QByteArray gammaDisk = "GAMMA DISK CONTENT\n";
    {
        QFile f(p1); f.open(QIODevice::WriteOnly); f.write(alphaDisk); f.close();
        QFile h(p3); h.open(QIODevice::WriteOnly); h.write(gammaDisk); h.close();
    }

    // Make beta.txt an entry that exists() but is NOT a readable file: replace
    // it with a DIRECTORY (one of the audit's stated triggers — "replaced by a
    // dir"). QFileInfo(p2).exists() is then true, so restoreSession enters the
    // modified branch, but openFile() early-returns (path is not isFile()),
    // appending NO tab. That is the exact precondition that made the old code
    // alias the PREVIOUS restored editor. (A perms-000 file is equivalent but
    // would trip loadFile()'s modal QMessageBox under offscreen Qt, hanging the
    // test; the directory form reproduces the identical openFile-appended-
    // nothing path without any modal.)
    EXPECT("created beta.txt as a directory", QDir().mkpath(p2));
    {
        // Precondition: confirm beta exists() but is not a regular file, so
        // openFile() will refuse it and append no tab.
        QFileInfo bi(p2);
        EXPECT("precondition: beta.txt exists() but is NOT a file",
               bi.exists() && !bi.isFile());
        if (bi.isFile()) {
            std::printf("\n%d passed, %d failed\n", g_pass, g_fail);
            return 1;
        }
    }

    // The unsaved buffers each tab carried at last save. These are what the
    // restore overlays for modified tabs.
    const QString alphaUnsaved = "ALPHA UNSAVED BUFFER";
    const QString betaUnsaved  = "BETA UNSAVED BUFFER";   // destined for the dead tab
    const QString gammaUnsaved = "GAMMA UNSAVED BUFFER";

    // ── Hand-craft a session.json with the three modified tabs ──
    auto makeTab = [](const QString &path, const QString &unsaved) {
        QJsonObject t;
        t["path"] = path;
        t["tabName"] = QFileInfo(path).fileName();
        t["line"] = 0;
        t["col"] = 0;
        t["active"] = false;
        t["modified"] = true;
        t["unsavedContent"] = unsaved;
        return t;
    };
    QJsonArray tabs;
    tabs.append(makeTab(p1, alphaUnsaved));
    tabs.append(makeTab(p2, betaUnsaved));
    tabs.append(makeTab(p3, gammaUnsaved));

    QJsonObject session;
    session["tabs"] = tabs;
    session["windowW"] = 1000;
    session["windowH"] = 700;
    session["maximized"] = false;

    const QString cfgDir = cfg.path() + "/notepatra";
    QDir().mkpath(cfgDir);
    const QString sessionPath = cfgDir + "/session.json";
    {
        QFile sf(sessionPath);
        EXPECT("wrote session.json", sf.open(QIODevice::WriteOnly));
        sf.write(QJsonDocument(session).toJson());
        sf.close();
    }

    // ── Constructing MainWindow runs restoreSession() in the ctor ──
    MainWindow mw;
    QApplication::processEvents();

    auto *tm = mw.findChild<TabManager *>();
    EXPECT("TabManager found", tm != nullptr);
    if (!tm) { std::printf("\n%d passed, %d failed\n", g_pass, g_fail); return 1; }

    Editor *alpha = tabForName(tm, "alpha.txt");
    Editor *gamma = tabForName(tm, "gamma.txt");
    EXPECT("alpha.txt tab restored", alpha != nullptr);
    EXPECT("gamma.txt tab restored", gamma != nullptr);

    // No restored file-backed tab should carry beta.txt's path — the
    // unreadable file must never have been associated with a tab.
    EXPECT("no tab is path-associated with the unreadable beta.txt",
           tabForName(tm, "beta.txt") == nullptr);

    if (alpha) {
        // THE CORE CONTRACT: alpha's buffer must hold ITS OWN unsaved content,
        // never beta's (pre-fix bug) and never gamma's. Before the fix, the
        // dead beta tab aliased alpha and overwrote it with betaUnsaved.
        const QString alphaText = alpha->text();
        EXPECT("alpha tab holds alpha's own unsaved content",
               alphaText == alphaUnsaved);
        EXPECT("alpha tab was NOT overwritten with beta's content",
               alphaText != betaUnsaved);
        EXPECT("alpha tab was NOT overwritten with gamma's content",
               alphaText != gammaUnsaved);
        EXPECT("alpha tab still maps to alpha.txt on disk",
               QFileInfo(alpha->filePath()).fileName() == "alpha.txt");
    }

    if (gamma) {
        EXPECT("gamma tab holds gamma's own unsaved content",
               gamma->text() == gammaUnsaved);
        EXPECT("gamma tab still maps to gamma.txt on disk",
               QFileInfo(gamma->filePath()).fileName() == "gamma.txt");
    }

    // ── No wrong-file Save: prove a Save of the (modified) alpha tab writes
    //    alpha.txt with alpha's content, and that gamma.txt on disk is
    //    untouched by alpha's save (i.e. paths weren't crossed). We write the
    //    buffer to alpha's own path directly to model MainWindow::saveFile,
    //    which persists currentEditor()->text() to currentEditor()->filePath().
    if (alpha && QFileInfo(alpha->filePath()).fileName() == "alpha.txt") {
        QFile out(alpha->filePath());
        if (out.open(QIODevice::WriteOnly)) {
            out.write(alpha->text().toUtf8());
            out.close();
        }
        QFile rb(p1);
        rb.open(QIODevice::ReadOnly);
        const QString alphaOnDisk = QString::fromUtf8(rb.readAll());
        rb.close();
        EXPECT("Save of alpha tab writes alpha's content to alpha.txt",
               alphaOnDisk == alphaUnsaved);

        // gamma.txt on disk must NOT have been clobbered with alpha's content.
        QFile gd(p3);
        gd.open(QIODevice::ReadOnly);
        const QString gammaOnDisk = QString::fromUtf8(gd.readAll());
        gd.close();
        EXPECT("gamma.txt on disk NOT clobbered by alpha's save",
               gammaOnDisk != alphaUnsaved);
    }

    std::printf("\n%d passed, %d failed\n", g_pass, g_fail);
    return g_fail == 0 ? 0 : 1;
}
