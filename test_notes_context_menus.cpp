// SPDX-License-Identifier: GPL-3.0-or-later
//
// Unit tests for the right-click context menu factories. We assert
// that each factory returns a QMenu whose top-level actions exist in
// the expected count + order, and that callbacks fire when the
// matching actions trigger.

#include "notes_context_menus.h"

#include <QAction>
#include <QApplication>
#include <QMenu>

#include <cstdio>

static int g_pass = 0, g_fail = 0;
#define EXPECT(label, cond) \
    do { if (cond) { ++g_pass; std::printf("  [PASS] %s\n", label); } \
         else      { ++g_fail; std::printf("  [FAIL] %s\n", label); } } while (0)

// Count top-level non-separator actions in a menu.
static int countActions(QMenu *m) {
    int n = 0;
    for (QAction *a : m->actions()) {
        if (!a->isSeparator()) ++n;
    }
    return n;
}

// Find a top-level action by visible text (handles QAction::text()
// containing the menu hint `&` accelerator the same way Qt does).
static QAction *findAction(QMenu *m, const QString &needle) {
    for (QAction *a : m->actions()) {
        if (a->text().contains(needle, Qt::CaseInsensitive)) return a;
    }
    return nullptr;
}

// Find a nested action by walking submenus too.
static QAction *findNested(QMenu *m, const QString &needle) {
    for (QAction *a : m->actions()) {
        if (a->text().contains(needle, Qt::CaseInsensitive)) return a;
        if (a->menu()) {
            QAction *r = findNested(a->menu(), needle);
            if (r) return r;
        }
    }
    return nullptr;
}

static void testPlainText() {
    std::printf("[test_notes_context_menus] forPlainText\n");

    int promoteFires = 0, embedFires = 0;
    QString lastPromote, lastEmbed;
    QMenu *m = NoterContextMenus::forPlainText(
        nullptr,
        [&](const QString &t) { ++promoteFires; lastPromote = t; },
        [&](const QString &t) { ++embedFires;   lastEmbed   = t; });

    EXPECT("menu is non-null", m != nullptr);
    if (!m) return;

    // Top level: Promote, Insert embed, (sep), Cut, Copy, Paste, (sep),
    // Toggle outline → 6 non-separator actions.
    EXPECT("top-level action count = 6", countActions(m) == 6);

    QAction *promote = findAction(m, "Promote");
    EXPECT("has Promote… submenu", promote && promote->menu());
    if (promote && promote->menu()) {
        // 5 promotion targets.
        EXPECT("promote has 5 entries",
               promote->menu()->actions().count() == 5);
    }

    QAction *embed = findAction(m, "Insert embed");
    EXPECT("has Insert embed submenu", embed && embed->menu());
    if (embed && embed->menu()) {
        EXPECT("embed has 4 entries",
               embed->menu()->actions().count() == 4);
    }

    // Fire a promote action and confirm the callback receives the type.
    if (promote && promote->menu()) {
        QAction *decision = nullptr;
        for (QAction *a : promote->menu()->actions())
            if (a->text().contains("Decision")) decision = a;
        EXPECT("promote has Decision item", decision != nullptr);
        if (decision) {
            decision->trigger();
            EXPECT("onPromote fired with 'decision'",
                   promoteFires == 1 && lastPromote == "decision");
        }
    }

    delete m;
}

static void testTaggedBlock() {
    std::printf("[test_notes_context_menus] forTaggedBlock\n");

    int done = 0, snooze1d = 0, snooze1w = 0, del = 0;
    QMenu *m = NoterContextMenus::forTaggedBlock(
        nullptr, "decision", "@alice", "May 22",
        nullptr, nullptr,
        [&](){ ++done; }, [&](){ ++snooze1d; },
        [&](){ ++snooze1w; }, [&](){ ++del; });
    EXPECT("menu is non-null", m != nullptr);
    if (!m) return;

    // Top level: header (disabled), (sep), Change type, Change owner,
    // (sep), Mark done, Snooze 1d, Snooze 1w, (sep), Delete →
    // 7 non-separator actions (header counts).
    EXPECT("top-level action count = 7", countActions(m) == 7);

    QAction *typeMenu = findAction(m, "Change type");
    EXPECT("has Change type submenu", typeMenu && typeMenu->menu());
    if (typeMenu && typeMenu->menu()) {
        EXPECT("type submenu has 5 entries",
               typeMenu->menu()->actions().count() == 5);

        // Decision is the currentType; it should be checked.
        bool decisionChecked = false;
        for (QAction *a : typeMenu->menu()->actions())
            if (a->text().contains("decision") && a->isChecked())
                decisionChecked = true;
        EXPECT("current type 'decision' is checked", decisionChecked);
    }

    QAction *doneAct = findAction(m, "Mark done");
    EXPECT("has Mark done", doneAct != nullptr);
    if (doneAct) {
        doneAct->trigger();
        EXPECT("onMarkDone fired", done == 1);
    }

    QAction *delAct = findAction(m, "Delete");
    EXPECT("has Delete", delAct != nullptr);
    if (delAct) {
        delAct->trigger();
        EXPECT("onDelete fired", del == 1);
    }

    delete m;
}

static void testSelection() {
    std::printf("[test_notes_context_menus] forSelection\n");

    int aiCalls = 0;
    QString lastMode;
    QMenu *m = NoterContextMenus::forSelection(
        nullptr, "hello world",
        [&](const QString &mode) { ++aiCalls; lastMode = mode; });
    EXPECT("menu is non-null", m != nullptr);
    if (!m) return;

    // Top level: header (disabled), (sep), Copy, (sep), AI… submenu
    // → 3 non-separator actions.
    EXPECT("top-level action count = 3", countActions(m) == 3);

    QAction *ai = findAction(m, "AI");
    EXPECT("has AI submenu", ai && ai->menu());
    if (ai && ai->menu()) {
        EXPECT("AI submenu has 2 entries (rewrite + extract-todo)",
               ai->menu()->actions().count() == 2);

        QAction *rewrite = nullptr;
        for (QAction *a : ai->menu()->actions())
            if (a->text().contains("Rewrite")) rewrite = a;
        EXPECT("has Rewrite", rewrite != nullptr);
        if (rewrite) {
            rewrite->trigger();
            EXPECT("onAi fired with 'rewrite'",
                   aiCalls == 1 && lastMode == "rewrite");
        }
    }

    delete m;
}

static void testAttendee() {
    std::printf("[test_notes_context_menus] forAttendee\n");
    QMenu *m = NoterContextMenus::forAttendee(nullptr, "alice");
    EXPECT("menu is non-null", m != nullptr);
    // Header (disabled), (sep), Filter, Show open todos, Copy as,
    // (sep), Remove → 5 non-separator actions.
    EXPECT("top-level action count = 5",
           m && countActions(m) == 5);
    EXPECT("header mentions alice",
           m && m->actions().first()->text().contains("alice"));
    delete m;
}

static void testCodeRef() {
    std::printf("[test_notes_context_menus] forCodeRef\n");
    QMenu *m = NoterContextMenus::forCodeRef(nullptr,
                                             "/x/y/foo.cpp", 42);
    EXPECT("menu is non-null", m != nullptr);
    // Header, (sep), Open, Refresh, Copy path, Reveal, (sep), Remove
    // → 6 non-separator actions.
    EXPECT("top-level action count = 6",
           m && countActions(m) == 6);
    EXPECT("header shows path + line",
           m && m->actions().first()->text().contains("/x/y/foo.cpp:42"));
    delete m;
}

static void testNoteListEntry() {
    std::printf("[test_notes_context_menus] forNoteListEntry\n");
    int opens = 0, renames = 0, exports = 0;
    QString lastExport;
    QMenu *m = NoterContextMenus::forNoteListEntry(
        nullptr, "/tmp/x.html",
        [&]{ ++opens; },
        [&]{ ++renames; },
        nullptr, nullptr,
        [&](const QString &fmt) { ++exports; lastExport = fmt; },
        nullptr, nullptr);
    EXPECT("menu is non-null", m != nullptr);
    if (!m) return;

    // Header, (sep), Open, Rename, Duplicate, Move, (sep), Export…,
    // Reveal, (sep), Delete → 8 non-separator actions.
    EXPECT("top-level action count = 8", countActions(m) == 8);

    QAction *openA = findAction(m, "Open");
    if (openA) {
        openA->trigger();
        EXPECT("onOpen fired", opens == 1);
    }
    QAction *renA = findAction(m, "Rename");
    if (renA) {
        renA->trigger();
        EXPECT("onRename fired", renames == 1);
    }

    QAction *expMenu = findAction(m, "Export");
    EXPECT("has Export submenu", expMenu && expMenu->menu());
    if (expMenu && expMenu->menu()) {
        EXPECT("export submenu has 3 entries",
               expMenu->menu()->actions().count() == 3);
        QAction *pdf = nullptr;
        for (QAction *a : expMenu->menu()->actions())
            if (a->text().contains("PDF")) pdf = a;
        if (pdf) {
            pdf->trigger();
            EXPECT("onExport fired with 'pdf'",
                   exports == 1 && lastExport == "pdf");
        }
    }

    // Every menu uses QStyle::standardIcon for icons — verify at least
    // one non-header action has a non-null icon. (Memory rule.)
    bool anyIcon = false;
    for (QAction *a : m->actions())
        if (!a->isSeparator() && !a->icon().isNull()) {
            anyIcon = true;
            break;
        }
    EXPECT("at least one action has a QStyle icon", anyIcon);

    delete m;
}

int main(int argc, char *argv[]) {
    qputenv("QT_QPA_PLATFORM", "offscreen");
    QApplication app(argc, argv);

    testPlainText();
    testTaggedBlock();
    testSelection();
    testAttendee();
    testCodeRef();
    testNoteListEntry();

    std::printf("\n[test_notes_context_menus] %d passed, %d failed\n",
                g_pass, g_fail);
    return g_fail == 0 ? 0 : 1;
}
