// ═══════════════════════════════════════════════════════════════════════
// test_ai_fullscreen_exit — v0.1.68 integration test for the AI dock
// auto-exit-fullscreen behaviour on tab switching.
//
// What this test covers — the actual user-facing state machine, not just
// the C++ unit-test layer:
//
//   1. Open AI dock, fullscreen it, then openFile(path) → fullscreen
//      MUST exit (user-initiated tab switch).
//   2. Open AI dock, fullscreen it, then setCurrentIndex(other tab) →
//      fullscreen MUST exit (Ctrl+Tab-style switch).
//   3. Open AI dock, fullscreen it, trigger Ctrl+N → fullscreen MUST
//      STAY ON (v0.1.61 background-tab UX rule — the new editor tab is
//      created but the AI dock keeps the screen so the user doesn't
//      lose AI flow).
//
// Approach: construct a real MainWindow under QT_QPA_PLATFORM=offscreen,
// use QApplication + Qt findChild to locate the AIPanel + its expand
// button (no friend class needed — every widget we touch is reachable
// via public Qt traversal). The fullscreen state is observed via the
// expand button's checked state (m_aiExpandBtn) — it's the single
// source of truth that AIPanel::forceExitFullscreen() drives.
// ═══════════════════════════════════════════════════════════════════════

#include "mainwindow.h"
#include "aipanel.h"
#include "config.h"

#include <QAction>
#include <QApplication>
#include <QDir>
#include <QFile>
#include <QPushButton>
#include <QStandardPaths>
#include <QTabWidget>
#include <QTemporaryDir>
#include <QTextStream>

#include <cstdio>
#include <cstdlib>

static int g_passed = 0;
static int g_failed = 0;

#define EXPECT_TRUE(label, cond) \
    do { \
        if (cond) { ++g_passed; std::printf("  [PASS] %s\n", label); } \
        else { ++g_failed; std::printf("  [FAIL] %s\n", label); } \
    } while (0)

#define EXPECT_FALSE(label, cond) EXPECT_TRUE(label, !(cond))

// Find the AI dock's expand button (m_aiExpandBtn) inside a given AIPanel.
// It has no objectName, so we filter by tooltip — the production code
// at aipanel.cpp:582 sets tooltip starting with "Expand the AI panel".
static QPushButton *findExpandButton(AIPanel *panel) {
    if (!panel) return nullptr;
    const auto buttons = panel->findChildren<QPushButton *>();
    for (QPushButton *b : buttons) {
        if (b->toolTip().startsWith(QStringLiteral("Expand the AI panel"))) {
            return b;
        }
    }
    return nullptr;
}

// Find the QAction wired to a given key sequence (e.g. Ctrl+Shift+A for
// AI Assistant toggle, Ctrl+N for new file). MainWindow attaches these
// to the menubar at construction time.
static QAction *findActionByShortcut(QWidget *root, const QKeySequence &seq) {
    const auto actions = root->findChildren<QAction *>();
    for (QAction *a : actions) {
        if (a->shortcut() == seq) return a;
    }
    return nullptr;
}

// Write a small file into a temp dir we control, returning its absolute
// path. openFile() requires the path to exist.
static QString writeTempFile(const QString &dir, const QString &name, const QString &content) {
    const QString path = QDir(dir).absoluteFilePath(name);
    QFile f(path);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate)) return {};
    QTextStream ts(&f);
    ts << content;
    f.close();
    return path;
}

int main(int argc, char *argv[]) {
    qputenv("QT_QPA_PLATFORM", "offscreen");
    // Isolate Config / session / chat-history so the test doesn't
    // touch the user's real ~/.config/notepatra/.
    QTemporaryDir cfgDir;
    qputenv("XDG_CONFIG_HOME", cfgDir.path().toUtf8());
    qputenv("XDG_DATA_HOME", cfgDir.path().toUtf8());

    QApplication app(argc, argv);

    QTemporaryDir workDir;
    if (!workDir.isValid()) {
        std::printf("FAIL — could not create temp workspace dir\n");
        return 2;
    }
    const QString fileA = writeTempFile(workDir.path(), "a.txt", "alpha\n");
    const QString fileB = writeTempFile(workDir.path(), "b.txt", "beta\n");
    if (fileA.isEmpty() || fileB.isEmpty()) {
        std::printf("FAIL — could not write temp files\n");
        return 2;
    }

    std::printf("=== test_ai_fullscreen_exit — v0.1.68 integration ===\n\n");

    // ───────────────────────────────────────────────────────────────
    // Construct MainWindow
    // ───────────────────────────────────────────────────────────────
    MainWindow mw;
    mw.show();
    QApplication::processEvents();

    // ───────────────────────────────────────────────────────────────
    // Locate the AI dock panel + its expand button.
    // MainWindow creates two AIPanel instances (m_aiPanel — the
    // legacy / tear-out version — and m_aiDockPanel — the right
    // dock). We want the dock one. m_aiDockPanel is the one whose
    // parent chain leads to m_aiDockHost. In practice findChildren
    // returns them in construction order; the dock is the second.
    // To be deterministic, pick the AIPanel that has a parent which
    // is part of the splitter.
    // ───────────────────────────────────────────────────────────────
    const auto panels = mw.findChildren<AIPanel *>();
    AIPanel *dockPanel = nullptr;
    QPushButton *expandBtn = nullptr;
    for (AIPanel *p : panels) {
        if (auto *btn = findExpandButton(p)) {
            // Prefer the panel whose expand button is the visible one
            // attached to the dock; both panels may have one, so we
            // keep the last one (the dock is constructed second).
            dockPanel = p;
            expandBtn = btn;
        }
    }
    EXPECT_TRUE("AI dock panel located", dockPanel != nullptr);
    EXPECT_TRUE("AI expand button located", expandBtn != nullptr);
    if (!dockPanel || !expandBtn) {
        std::printf("\n=== ABORTED: could not locate AI dock widgets ===\n");
        return 2;
    }

    // ───────────────────────────────────────────────────────────────
    // Open the AI dock (Ctrl+Shift+A toggles it on).
    // ───────────────────────────────────────────────────────────────
    QAction *aiToggle = findActionByShortcut(&mw, QKeySequence("Ctrl+Shift+A"));
    EXPECT_TRUE("AI Assistant action found (Ctrl+Shift+A)", aiToggle != nullptr);
    if (aiToggle) aiToggle->trigger();
    QApplication::processEvents();

    // ───────────────────────────────────────────────────────────────
    // Locate the QTabWidget. TabManager inherits QTabWidget.
    // ───────────────────────────────────────────────────────────────
    QTabWidget *tabs = mw.findChild<QTabWidget *>();
    EXPECT_TRUE("Main QTabWidget located", tabs != nullptr);
    if (!tabs) return 2;

    // Open two files so we have two real editor tabs to switch between.
    mw.openFile(fileA);
    mw.openFile(fileB);
    QApplication::processEvents();
    EXPECT_TRUE("Two editor tabs present (Welcome may add a 3rd)",
                tabs->count() >= 2);

    // ───────────────────────────────────────────────────────────────
    // SCENARIO 1: fullscreen → openFile(third file) → exits
    // ───────────────────────────────────────────────────────────────
    std::printf("\n-- Scenario 1: fullscreen + openFile(new file) → exits --\n");
    expandBtn->setChecked(true);                 // enter fullscreen
    QApplication::processEvents();
    EXPECT_TRUE("S1 setup: dock is fullscreen",  expandBtn->isChecked());

    const QString fileC = writeTempFile(workDir.path(), "c.txt", "gamma\n");
    mw.openFile(fileC);
    QApplication::processEvents();
    EXPECT_FALSE("S1: fullscreen exited after openFile(new path)",
                 expandBtn->isChecked());

    // ───────────────────────────────────────────────────────────────
    // SCENARIO 2: fullscreen → setCurrentIndex(other) → exits
    //   This is the Ctrl+Tab / tab-bar-click path — programmatic
    //   tab switches with no skip flag set.
    // ───────────────────────────────────────────────────────────────
    std::printf("\n-- Scenario 2: fullscreen + setCurrentIndex(other) → exits --\n");
    expandBtn->setChecked(true);
    QApplication::processEvents();
    EXPECT_TRUE("S2 setup: dock is fullscreen", expandBtn->isChecked());

    int currentIdx = tabs->currentIndex();
    int otherIdx = (currentIdx == 0) ? 1 : 0;
    tabs->setCurrentIndex(otherIdx);
    QApplication::processEvents();
    EXPECT_FALSE("S2: fullscreen exited after setCurrentIndex (Ctrl+Tab equiv)",
                 expandBtn->isChecked());

    // ───────────────────────────────────────────────────────────────
    // SCENARIO 3: fullscreen → Ctrl+N (newFile) → STAYS fullscreen
    //   v0.1.61 background-tab UX rule. newFile() sets the skip flag
    //   immediately before setCurrentIndex so currentChanged does NOT
    //   call exitAiFullscreenIfActive.
    // ───────────────────────────────────────────────────────────────
    std::printf("\n-- Scenario 3: fullscreen + Ctrl+N → STAYS on (v0.1.61 rule) --\n");
    expandBtn->setChecked(true);
    QApplication::processEvents();
    EXPECT_TRUE("S3 setup: dock is fullscreen", expandBtn->isChecked());

    QAction *newFileAct = findActionByShortcut(&mw, QKeySequence::New); // Ctrl+N
    EXPECT_TRUE("Ctrl+N action found", newFileAct != nullptr);
    if (newFileAct) {
        newFileAct->trigger();
        QApplication::processEvents();
        EXPECT_TRUE("S3: fullscreen REMAINS on after Ctrl+N (background-tab UX)",
                    expandBtn->isChecked());
    }

    // ───────────────────────────────────────────────────────────────
    // SCENARIO 4: fullscreen → openFile(ALREADY-OPEN path) → exits
    //   Even when openFile short-circuits to an existing tab via
    //   setCurrentIndex, it should still collapse the dock — that
    //   path is the user double-clicking a search result or a file
    //   that's already open in another tab.
    // ───────────────────────────────────────────────────────────────
    std::printf("\n-- Scenario 4: fullscreen + openFile(already-open path) → exits --\n");
    expandBtn->setChecked(true);
    QApplication::processEvents();
    EXPECT_TRUE("S4 setup: dock is fullscreen", expandBtn->isChecked());

    mw.openFile(fileA);                          // already open, will setCurrentIndex
    QApplication::processEvents();
    EXPECT_FALSE("S4: fullscreen exited after openFile(already-open)",
                 expandBtn->isChecked());

    // ───────────────────────────────────────────────────────────────
    std::printf("\n=== test_ai_fullscreen_exit: %d passed, %d failed ===\n",
                g_passed, g_failed);
    return g_failed == 0 ? 0 : 1;
}
