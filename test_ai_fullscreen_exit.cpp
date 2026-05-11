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
#include "fileexplorer.h"

#include <QAbstractButton>
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
    // v0.1.70 — primary shortcut moved from Ctrl+Shift+A → Ctrl+Q.
    // Ctrl+Shift+A is still registered as a secondary shortcut.
    QAction *aiToggle = findActionByShortcut(&mw, QKeySequence("Ctrl+Q"));
    EXPECT_TRUE("AI Assistant action found (Ctrl+Q)", aiToggle != nullptr);
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

    // applyMode + renderTranscript rebuild AIPanel widgets on every mode
    // switch, so stored mode-button pointers become stale after the first
    // toggle. Re-find them on demand via this helper. Returns nullptr
    // for any role missing (caller checks).
    auto findModeBtn = [dockPanel](const QString &text) -> QAbstractButton * {
        for (QAbstractButton *b : dockPanel->findChildren<QAbstractButton *>()) {
            if (b->text() == text) return b;
        }
        return nullptr;
    };

    // Baseline: chat mode, NOT fullscreen.
    EXPECT_TRUE("Chat / Coding / Data mode buttons located",
                findModeBtn(QStringLiteral("Chat"))
                    && findModeBtn(QStringLiteral("Coding"))
                    && findModeBtn(QStringLiteral("Data")));
    if (auto *chat = findModeBtn(QStringLiteral("Chat"))) chat->setChecked(true);
    QApplication::processEvents();

    // ───────────────────────────────────────────────────────────────
    // SCENARIO 5: Coding mode no longer auto-fullscreens (v0.1.70 final).
    // Editor + FileExplorer remain visible — the VS Code 3-column layout.
    // Tool button clicks just open a tab; nothing to "exit" since dock
    // is already at 50/50.
    // ───────────────────────────────────────────────────────────────
    std::printf("\n-- Scenario 5: Coding mode (no auto-fullscreen) + Project Search --\n");
    if (auto *codingBtn = findModeBtn(QStringLiteral("Coding"))) {
        codingBtn->setChecked(true);
        QApplication::processEvents();
        EXPECT_TRUE("S5 setup: m_tabs VISIBLE in Coding mode (no auto-fullscreen)",
                    tabs->isVisible());

        QAction *psAct = findActionByShortcut(&mw, QKeySequence("Ctrl+Shift+G"));
        EXPECT_TRUE("S5: Project Search action found (Ctrl+Shift+G)", psAct != nullptr);
        if (psAct) {
            psAct->trigger();
            QApplication::processEvents();
            EXPECT_TRUE("S5: m_tabs still VISIBLE after Project Search click",
                        tabs->isVisible());
            EXPECT_TRUE("S5: Project Search tab is current",
                        tabs->tabText(tabs->currentIndex()).contains(
                            QStringLiteral("Project Search")));
        }
    }

    // Reset to chat mode before S6
    if (auto *chat = findModeBtn(QStringLiteral("Chat"))) chat->setChecked(true);
    QApplication::processEvents();

    // ───────────────────────────────────────────────────────────────
    // SCENARIO 6: Data mode auto-fullscreens (data-analyst surface).
    // Terminal click must exit fullscreen so the tool tab is visible.
    // ───────────────────────────────────────────────────────────────
    std::printf("\n-- Scenario 6: Data mode 50/50 (no auto-fullscreen) + Terminal --\n");
    if (auto *dataBtn = findModeBtn(QStringLiteral("Data"))) {
        dataBtn->setChecked(true);
        QApplication::processEvents();
        EXPECT_TRUE("S6 setup: m_tabs VISIBLE in Data mode (50/50, no auto-fullscreen)",
                    tabs->isVisible());

        QAction *termAct = findActionByShortcut(&mw, QKeySequence("Ctrl+`"));
        EXPECT_TRUE("S6: Terminal action found (Ctrl+`)", termAct != nullptr);
        if (termAct) {
            termAct->trigger();
            QApplication::processEvents();
            EXPECT_TRUE("S6: m_tabs still VISIBLE after Terminal click (Data mode)",
                        tabs->isVisible());
            EXPECT_TRUE("S6: Terminal tab is current",
                        tabs->tabText(tabs->currentIndex()).contains(
                            QStringLiteral("Terminal")));
        }
    }

    // Reset to chat mode before S7
    if (auto *chat = findModeBtn(QStringLiteral("Chat"))) chat->setChecked(true);
    QApplication::processEvents();

    // ───────────────────────────────────────────────────────────────
    // FileExplorer visibility rule — ONLY visible when AI dock is open
    // AND coding mode is active. Hidden in Chat, Data, or when AI dock
    // is closed entirely. User reported in v0.1.69 testing.
    // ───────────────────────────────────────────────────────────────
    FileExplorer *explorer = mw.findChild<FileExplorer *>();
    EXPECT_TRUE("FileExplorer widget located", explorer != nullptr);
    if (!explorer) {
        std::printf("\n=== test_ai_fullscreen_exit: %d passed, %d failed ===\n",
                    g_passed, g_failed);
        return g_failed == 0 ? 0 : 1;
    }

    std::printf("\n-- Scenario 7: FileExplorer hidden in Chat mode --\n");
    EXPECT_FALSE("S7: explorer hidden in chat mode (baseline)", explorer->isVisible());

    // Coding/Data modes auto-fullscreen the dock (per user UX choice) which
    // hides all siblings, including the file explorer. So even though the
    // codingModeRequested signal asks the explorer to show, the fullscreen
    // handler immediately hides it again. From a user perspective: while
    // Coding/Data mode is the ACTIVE state, the explorer is not visible —
    // it only becomes visible after the user clicks a tool button (which
    // calls exitAiFullscreenIfActive). These scenarios verify that the
    // auto-fullscreen behaviour wins.
    std::printf("\n-- Scenario 8: switch to Coding → FileExplorer auto-shows (VS Code 3-column) --\n");
    if (auto *codingBtn = findModeBtn(QStringLiteral("Coding"))) {
        codingBtn->setChecked(true);
        QApplication::processEvents();
        // v0.1.70 final — Coding mode auto-shows the explorer (VS Code
        // / Cursor 3-column layout: explorer | editor | AI dock).
        EXPECT_TRUE("S8: explorer VISIBLE in Coding mode (3-column layout)",
                    explorer->isVisible());
    }

    std::printf("\n-- Scenario 9: switch Coding → Chat → FileExplorer hides --\n");
    if (auto *chat = findModeBtn(QStringLiteral("Chat"))) {
        chat->setChecked(true);
        QApplication::processEvents();
        EXPECT_FALSE("S9: explorer hidden in Chat mode",
                     explorer->isVisible());
    }

    std::printf("\n-- Scenario 10: switch Chat → Data → FileExplorer stays hidden --\n");
    {
        auto *data = findModeBtn(QStringLiteral("Data"));
        if (data) {
            data->setChecked(true);
            QApplication::processEvents();
            EXPECT_FALSE("S10: explorer hidden in Data mode",
                         explorer->isVisible());
        }
    }

    std::printf("\n-- Scenario 11: close AI dock while in Coding → tabs + explorer correct --\n");
    {
        auto *coding = findModeBtn(QStringLiteral("Coding"));
        if (coding && aiToggle) {
            coding->setChecked(true);
            QApplication::processEvents();
            EXPECT_TRUE("S11 setup: tabs visible in Coding (no fullscreen)",
                        tabs->isVisible());
            EXPECT_TRUE("S11 setup: explorer visible in Coding (3-column)",
                        explorer->isVisible());
            aiToggle->trigger();           // close AI dock
            QApplication::processEvents();
            EXPECT_TRUE("S11: tabs VISIBLE after closing AI dock",
                        tabs->isVisible());
            EXPECT_FALSE("S11: explorer hidden after closing AI dock",
                         explorer->isVisible());
        }
    }

    // ───────────────────────────────────────────────────────────────
    // SCENARIO 12: setAiDockVisible persists Config::aiDockVisible.
    // ───────────────────────────────────────────────────────────────
    std::printf("\n-- Scenario 12: setAiDockVisible(false/true) persists Config flag --\n");
    mw.setAiDockVisible(false);
    QApplication::processEvents();
    EXPECT_FALSE("S12: Config::aiDockVisible == false after hide",
                 Config::instance().aiDockVisible);
    EXPECT_FALSE("S12: isAiDockVisible() == false after hide", mw.isAiDockVisible());

    mw.setAiDockVisible(true);
    QApplication::processEvents();
    EXPECT_TRUE("S12: Config::aiDockVisible == true after show",
                Config::instance().aiDockVisible);
    EXPECT_TRUE("S12: isAiDockVisible() == true after show", mw.isAiDockVisible());

    // ───────────────────────────────────────────────────────────────
    // SCENARIO 13: showAiDockForInvocation() auto-opens hidden dock.
    // ───────────────────────────────────────────────────────────────
    std::printf("\n-- Scenario 13: showAiDockForInvocation() auto-opens hidden dock --\n");
    mw.setAiDockVisible(false);
    QApplication::processEvents();
    EXPECT_FALSE("S13 setup: dock hidden", mw.isAiDockVisible());

    mw.showAiDockForInvocation();
    QApplication::processEvents();
    EXPECT_TRUE("S13: dock visible after showAiDockForInvocation()",
                mw.isAiDockVisible());

    // Idempotent — calling again when visible is no-op.
    mw.showAiDockForInvocation();
    QApplication::processEvents();
    EXPECT_TRUE("S13: dock still visible (idempotent)", mw.isAiDockVisible());

    // ───────────────────────────────────────────────────────────────
    // SCENARIO 14: Sub-mode RESTORED on dock close + reopen (NOT reset
    // to Chat). v0.1.70 final design via plan-mode discussion.
    // ───────────────────────────────────────────────────────────────
    std::printf("\n-- Scenario 14: close + reopen AI dock → restore last sub-mode --\n");
    if (auto *codingPre = findModeBtn(QStringLiteral("Coding"))) {
        codingPre->setChecked(true);
        QApplication::processEvents();
    }
    mw.setAiDockVisible(false);
    QApplication::processEvents();
    mw.setAiDockVisible(true);
    QApplication::processEvents();

    {
        auto *chat   = findModeBtn(QStringLiteral("Chat"));
        auto *coding = findModeBtn(QStringLiteral("Coding"));
        EXPECT_TRUE("S14: Coding sub-mode restored after close+reopen",
                    coding && coding->isChecked());
        EXPECT_FALSE("S14: Chat sub-mode NOT active (no auto-reset)",
                     chat && chat->isChecked());
    }

    // ───────────────────────────────────────────────────────────────
    // SCENARIO 15: Ctrl+Q shortcut maps to AI toggle.
    // ───────────────────────────────────────────────────────────────
    std::printf("\n-- Scenario 15: Ctrl+Q action exists + triggers toggle --\n");
    QAction *ctrlQAction = findActionByShortcut(&mw, QKeySequence("Ctrl+Q"));
    EXPECT_TRUE("S15: Ctrl+Q action found (the new AI toggle shortcut)",
                ctrlQAction != nullptr);
    if (ctrlQAction) {
        const bool wasVisible = mw.isAiDockVisible();
        ctrlQAction->trigger();
        QApplication::processEvents();
        EXPECT_TRUE("S15: dock visibility flipped by Ctrl+Q",
                    mw.isAiDockVisible() != wasVisible);
        // Flip back to leave a clean state.
        ctrlQAction->trigger();
        QApplication::processEvents();
    }

    // ───────────────────────────────────────────────────────────────
    std::printf("\n=== test_ai_fullscreen_exit: %d passed, %d failed ===\n",
                g_passed, g_failed);
    return g_failed == 0 ? 0 : 1;
}
