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
#include <QEventLoop>
#include <QSplitter>
#include <QStandardPaths>
#include <QTabWidget>
#include <QTemporaryDir>
#include <QTextStream>
#include <QTimer>

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
    // SCENARIO 16: full hide-then-show cycle leaves the AI dock with a
    // non-zero splitter slot width.  User-reported regression in v0.1.72:
    // "ai toggle space goes out but the actual AI assistant is not
    // showing up at all".  Root cause was rebalanceAiDockSplit() bailing
    // on m_aiDockSizedOnce ⇒ after a hide the splitter slot stayed at 0
    // and the dock's setVisible(true) alone didn't restore the width.
    // Fix: under m_aiDockSizedOnce, only bail if the current slot is
    // wider than 40 px (deliberate user drag); below that we re-apply
    // the 60/40 default.
    // ───────────────────────────────────────────────────────────────
    std::printf("\n-- Scenario 16: hide-show cycle restores dock width --\n");
    QSplitter *mainSplit = mw.findChild<QSplitter *>();
    // Find the splitter whose children include the AI dock host.  The
    // window has several QSplitters; we want the one m_splitter points at.
    QSplitter *aiHostSplitter = nullptr;
    {
        const auto allSplits = mw.findChildren<QSplitter *>();
        for (auto *sp : allSplits) {
            for (int i = 0; i < sp->count(); ++i) {
                const QString name = sp->widget(i)->objectName();
                if (name == QStringLiteral("aiDockHost") ||
                    sp->widget(i)->inherits("QFrame") /* dock host wrapper */) {
                    // Heuristic — final test is whether toggling the dock
                    // changes the visible state of any child of this
                    // splitter.  We'll find the right one by trying each.
                    aiHostSplitter = sp;
                    break;
                }
            }
            if (aiHostSplitter) break;
        }
    }
    (void)mainSplit;   // suppress unused-warning in non-debug builds

    // Make sure dock is visible to start.
    if (!mw.isAiDockVisible()) mw.setAiDockVisible(true);
    QApplication::processEvents();
    QApplication::processEvents();

    // Find the dock slot index BEFORE we hide.
    auto findAiDockSlotW = [&]() -> int {
        const auto splits = mw.findChildren<QSplitter *>();
        for (auto *sp : splits) {
            const auto sizes = sp->sizes();
            for (int i = 0; i < sp->count() && i < sizes.size(); ++i) {
                if (sp->widget(i)->isVisible() && sp->widget(i)->maximumWidth() > 200) {
                    // Look for the slot whose width drops to ~0 after hide.
                }
            }
        }
        // Simpler: read m_aiDockHost width directly via the panel's parent.
        AIPanel *panel = mw.findChild<AIPanel *>();
        QWidget *host = panel ? panel->parentWidget() : nullptr;
        return host ? host->width() : 0;
    };

    const int wBeforeHide = findAiDockSlotW();
    EXPECT_TRUE("S16: dock has nonzero width before hide", wBeforeHide > 50);

    mw.setAiDockVisible(false);
    QApplication::processEvents();
    QApplication::processEvents();
    EXPECT_TRUE("S16: dock isVisible == false after hide", !mw.isAiDockVisible());

    mw.setAiDockVisible(true);
    QApplication::processEvents();
    QApplication::processEvents();
    // Give the singleShot(0) timer inside rebalanceAiDockSplit() a chance
    // to fire — the deferred apply() lambda is what actually sets sizes.
    {
        QEventLoop wait;
        QTimer::singleShot(50, &wait, &QEventLoop::quit);
        wait.exec();
    }

    EXPECT_TRUE("S16: dock isVisible == true after re-show", mw.isAiDockVisible());

    const int wAfterShow = findAiDockSlotW();
    // The key assertion: width must NOT be collapsed to 0/small.  Before
    // the v0.1.72 fix this would be ~0 px → user sees a blank dock.
    EXPECT_TRUE("S16: dock has nonzero width after hide-show cycle",
                wAfterShow > 50);

    // ───────────────────────────────────────────────────────────────
    // SCENARIO 17: red ✕ close button → toolbar re-open round-trip.
    // v0.1.73 fix.  Pre-v0.1.73 the close button called
    // parentWidget()->setVisible(false) directly, leaving Config out of
    // sync and the splitter slot at 0 → subsequent re-open showed a
    // blank zero-width dock.  v0.1.73 routes the button through
    // setAiDockVisible(false) via a new closeDockRequested signal.
    // ───────────────────────────────────────────────────────────────
    std::printf("\n-- Scenario 17: red ✕ close → toolbar re-open works --\n");

    // Make sure dock is visible to start the scenario.
    if (!mw.isAiDockVisible()) mw.setAiDockVisible(true);
    QApplication::processEvents();
    QApplication::processEvents();
    EXPECT_TRUE("S17 setup: dock visible before ✕", mw.isAiDockVisible());

    // Find the AI panel + its ✕ close button (set with toolTip
    // containing "Close the AI dock"). Click it.
    AIPanel *panel = mw.findChild<AIPanel *>();
    QPushButton *closeBtn = nullptr;
    if (panel) {
        for (auto *btn : panel->findChildren<QPushButton *>()) {
            if (btn->toolTip().contains(QStringLiteral("Close the AI dock"))) {
                closeBtn = btn;
                break;
            }
        }
    }
    EXPECT_TRUE("S17: found red ✕ close button by tooltip", closeBtn != nullptr);

    if (closeBtn) {
        closeBtn->click();
        QApplication::processEvents();
        QApplication::processEvents();
        EXPECT_TRUE("S17: dock hidden after ✕ click", !mw.isAiDockVisible());
        EXPECT_TRUE("S17: Config::aiDockVisible == false after ✕",
                    !Config::instance().aiDockVisible);

        // Re-open via the canonical handler (simulates the toolbar AI button).
        mw.setAiDockVisible(true);
        QApplication::processEvents();
        QApplication::processEvents();
        {
            QEventLoop wait;
            QTimer::singleShot(50, &wait, &QEventLoop::quit);
            wait.exec();
        }
        EXPECT_TRUE("S17: dock visible after re-open", mw.isAiDockVisible());

        // The whole point: the dock must NOT be 0-px wide after re-open
        // following a ✕ close.  Walk to the dock host widget and check
        // its rendered width.
        QWidget *hostAfter = panel->parentWidget();
        const int reopenedW = hostAfter ? hostAfter->width() : 0;
        EXPECT_TRUE("S17: dock has nonzero width after ✕ → re-open cycle",
                    reopenedW > 50);
    }


    // ───────────────────────────────────────────────────────────────
    // SCENARIO 18: the AI's project root is STABLE across tab switches.
    //
    // The AI root used to be recomputed per call as "explorer folder, else
    // the CURRENT tab's directory". With no folder open that flapped on every
    // Ctrl+Tab: the chat history is keyed on the root, so switching tabs
    // silently swapped the conversation out from under the user and cancelled
    // any pending write approval. aiWorkspaceRoot() latches once per session
    // and only an EXPLICIT folder re-keys it.
    // ───────────────────────────────────────────────────────────────
    std::printf("\n-- Scenario 18: AI workspace root stable across tab switches --\n");
    {
        QTemporaryDir otherDir;
        FileExplorer *explorer = mw.findChild<FileExplorer *>();
        EXPECT_TRUE("S18 setup: FileExplorer located", explorer != nullptr);
        EXPECT_TRUE("S18 setup: second temp dir valid", otherDir.isValid());

        if (explorer && otherDir.isValid()) {
            // Vacuity guard. Every "stayed the same" assertion below is
            // meaningless if a folder is already open (then the root is
            // pinned for the boring reason) or if the root is empty (then
            // it trivially never changes). Both must be false to proceed.
            EXPECT_TRUE("S18 guard: no explicit workspace folder is open",
                        mw.workspaceFolder().isEmpty());

            const QString firstRoot = mw.aiWorkspaceRoot();
            EXPECT_TRUE("S18: root latched to the open file's directory",
                        firstRoot == workDir.path());
            EXPECT_FALSE("S18 guard: latched root is not empty",
                         firstRoot.isEmpty());

            // Open a file living in a DIFFERENT directory and focus it.
            const QString fileD =
                writeTempFile(otherDir.path(), "d.txt", "delta\n");
            EXPECT_FALSE("S18 setup: wrote d.txt in the other dir",
                         fileD.isEmpty());
            mw.openFile(fileD);
            QApplication::processEvents();

            // Vacuity guard #2: prove the switch actually LANDED on the new
            // directory. Without this, "root unchanged" would pass even if
            // openFile had silently failed.
            EXPECT_TRUE("S18 guard: current tab is now the other dir's file",
                        mw.suggestedDialogFolder() == otherDir.path());
            EXPECT_TRUE("S18: AI root UNCHANGED after opening a file "
                        "elsewhere",
                        mw.aiWorkspaceRoot() == firstRoot);

            // ...and unchanged again after a plain Ctrl+Tab-style switch.
            //
            // The switch must END on the OTHER directory's tab. Switching away
            // to a workDir tab and asserting there is vacuous: the pre-fix
            // recompute-per-call also returns workDir from a workDir tab, so
            // the assertion passed against the very bug it exists to catch.
            const int otherTab = tabs->currentIndex();
            int homeTab = -1;
            for (int i = 0; i < tabs->count(); ++i) {
                if (i == otherTab) continue;
                tabs->setCurrentIndex(i);
                QApplication::processEvents();
                if (mw.suggestedDialogFolder() == workDir.path()) {
                    homeTab = i;
                    break;
                }
            }
            EXPECT_TRUE("S18 guard: switched away to a workDir tab",
                        homeTab >= 0);
            tabs->setCurrentIndex(otherTab);        // Ctrl+Tab back
            QApplication::processEvents();
            EXPECT_TRUE("S18 guard: landed back on the other dir's tab",
                        tabs->currentIndex() == otherTab
                            && mw.suggestedDialogFolder() == otherDir.path());
            EXPECT_TRUE("S18: AI root UNCHANGED after a tab switch",
                        mw.aiWorkspaceRoot() == firstRoot);

            // An EXPLICIT folder is the one thing that may re-key it — that
            // is the user saying "this is my project now".
            explorer->setRoot(otherDir.path());
            QApplication::processEvents();
            EXPECT_TRUE("S18 guard: explorer now reports an explicit folder",
                        explorer->workspaceRoot() == otherDir.path());
            EXPECT_TRUE("S18: opening a folder DOES re-latch the AI root",
                        mw.aiWorkspaceRoot() == otherDir.path());
        }
    }

    // ───────────────────────────────────────────────────────────────
    std::printf("\n=== test_ai_fullscreen_exit: %d passed, %d failed ===\n",
                g_passed, g_failed);
    return g_failed == 0 ? 0 : 1;
}
