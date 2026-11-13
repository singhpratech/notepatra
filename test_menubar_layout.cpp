// test_menubar_layout — the compact (macOS) menu bar and the full one.
//
// macOS truncates 14 top-level titles, so there the bar is
//   File | Edit | Search | View | Tools | Format | Settings | Window | Help
// with Encoding + Language under Format and Utilities, Plugins, Macro, Run
// under Tools. NOTEPATRA_TEST_COMPACT_MENUBAR picks the layout on any OS.
//
//   M1  Full layout: the 14 titles in today's order.
//   M2  Compact layout: the 9 titles in order; Format and Tools submenus;
//       F&ormat's mnemonic is not used by another top-level menu.
//   M3  Same leaf actions (texts + shortcuts) in both layouts, and each
//       moved menu keeps exactly its own items.
//   M4  Status-bar Language / Encoding clicks still open those menus.
//   M5  No item the move puts at the top of a menu would be hoisted into
//       the macOS app menu by Qt's text heuristic.

#include "mainwindow.h"
#include "statusbar.h"

#include <QAction>
#include <QApplication>
#include <QMenu>
#include <QMenuBar>
#include <QRegularExpression>
#include <QTemporaryDir>
#include <QTimer>

#include <algorithm>
#include <cstdio>

static int g_pass = 0, g_fail = 0;
#define EXPECT(label, cond) \
    do { if (cond) { ++g_pass; std::printf("  [PASS] %s\n", label); } \
         else      { ++g_fail; std::printf("  [FAIL] %s\n", label); } } while (0)

static QString plain(const QString &t) { return QString(t).remove(QLatin1Char('&')).trimmed(); }

static QList<QMenu *> topMenus(MainWindow &mw) {
    QList<QMenu *> out;
    for (QAction *a : mw.menuBar()->actions())
        if (a->menu()) out << a->menu();
    return out;
}

static QStringList titles(const QList<QMenu *> &menus) {
    QStringList out;
    for (QMenu *m : menus) out << plain(m->title());
    return out;
}

static QMenu *submenu(QMenu *parent, const QString &title) {
    if (!parent) return nullptr;
    for (QAction *a : parent->actions())
        if (a->menu() && plain(a->text()) == title) return a->menu();
    return nullptr;
}

static QMenu *topMenu(MainWindow &mw, const QString &title) {
    for (QMenu *m : topMenus(mw))
        if (plain(m->title()) == title) return m;
    return nullptr;
}

// "text\tshortcut" for every non-submenu, non-separator action under `menu`.
static void leaves(QMenu *menu, QStringList &out) {
    for (QAction *a : menu->actions()) {
        if (a->isSeparator()) continue;
        if (a->menu()) { leaves(a->menu(), out); continue; }
        QStringList keys;
        for (const QKeySequence &k : a->shortcuts()) keys << k.toString(QKeySequence::PortableText);
        out << plain(a->text()) + QLatin1Char('\t') + keys.join(QLatin1Char(','));
    }
}

static QStringList allLeaves(MainWindow &mw) {
    QStringList out;
    for (QMenu *m : topMenus(mw)) leaves(m, out);
    std::sort(out.begin(), out.end());
    return out;
}

static QStringList leavesOf(QMenu *m) {
    QStringList out;
    if (m) leaves(m, out);
    return out;
}

static QChar mnemonic(const QString &title) {
    const int i = title.indexOf(QLatin1Char('&'));
    return i >= 0 && i + 1 < title.size() ? title.at(i + 1).toLower() : QChar();
}

// Mirror of the app-menu part of Qt 5's cocoa detectMenuRole()
// (qcocoamenuitem.mm); roles are only detected for items directly inside a
// top-level menu.
static bool wouldBeHoisted(const QString &caption) {
    const QString c = plain(caption);
    auto starts = [&](const char *w) { return c.startsWith(QLatin1String(w), Qt::CaseInsensitive); };
    return starts("About") || c.endsWith(QLatin1String("About"), Qt::CaseInsensitive) ||
           starts("Config") || starts("Preference") || starts("Options") || starts("Setting") ||
           starts("Setup") || starts("Quit") || starts("Exit");
}

// Clicks the status-bar indicator and reports which menu popped up.
static QString popupAfter(MainWindow &mw, void (NppStatusBar::*signal)(const QPoint &)) {
    auto *bar = mw.findChild<NppStatusBar *>();
    if (!bar) return QStringLiteral("<no status bar>");
    emit(bar->*signal)(mw.mapToGlobal(QPoint(40, 40)));
    QApplication::processEvents();
    auto *menu = qobject_cast<QMenu *>(QApplication::activePopupWidget());
    const QString title = menu ? plain(menu->title()) : QStringLiteral("<none>");
    if (menu) menu->close();
    QApplication::processEvents();
    return title;
}

static const QStringList kMoved = {"Encoding", "Language", "Utilities", "Plugins", "Macro", "Run"};

int main(int argc, char *argv[]) {
    QTemporaryDir cfg;
    qputenv("XDG_CONFIG_HOME", cfg.path().toUtf8());
    qputenv("XDG_DATA_HOME", cfg.path().toUtf8());
    qputenv("HOME", cfg.path().toUtf8());
#ifdef Q_OS_WIN
    qputenv("APPDATA", cfg.path().toUtf8());
    qputenv("LOCALAPPDATA", cfg.path().toUtf8());
    qputenv("USERPROFILE", cfg.path().toUtf8());
#endif
    qputenv("QT_QPA_PLATFORM", "offscreen");
    QApplication app(argc, argv);
    QTimer::singleShot(100000, []() {
        std::printf("WATCHDOG: test_menubar_layout wedged\n");
        std::fflush(stdout);
        std::_Exit(3);
    });
    std::printf("=== test_menubar_layout ===\n");

    QStringList fullLeaves, compactLeaves;
    QHash<QString, QStringList> fullMoved;

    std::printf("-- M1: full layout --\n");
    {
        qputenv("NOTEPATRA_TEST_COMPACT_MENUBAR", "0");
        MainWindow mw(true);
        mw.show();
        QApplication::processEvents();
        const QStringList t = titles(topMenus(mw));
        std::printf("     %s\n", qPrintable(t.join(" | ")));
        EXPECT("M1: 14 top-level menus in today's order",
               t == QStringList({"File", "Edit", "Search", "View", "Tools", "Encoding", "Language",
                                 "Settings", "Utilities", "Plugins", "Macro", "Run", "Window",
                                 "Help"}));
        fullLeaves = allLeaves(mw);
        for (const QString &name : kMoved) fullMoved[name] = leavesOf(topMenu(mw, name));
        std::printf("-- M4: status-bar clicks (full) --\n");
        EXPECT("M4: Language indicator opens the Language menu",
               popupAfter(mw, &NppStatusBar::languageClicked) == "Language");
        EXPECT("M4: Encoding indicator opens the Encoding menu",
               popupAfter(mw, &NppStatusBar::encodingClicked) == "Encoding");
    }

    std::printf("-- M2: compact layout --\n");
    {
        qputenv("NOTEPATRA_TEST_COMPACT_MENUBAR", "1");
        MainWindow mw(true);
        mw.show();
        QApplication::processEvents();
        const QList<QMenu *> tops = topMenus(mw);
        const QStringList t = titles(tops);
        std::printf("     %s\n", qPrintable(t.join(" | ")));
        EXPECT("M2: 9 top-level menus in order",
               t == QStringList({"File", "Edit", "Search", "View", "Tools", "Format", "Settings",
                                 "Window", "Help"}));
        QMenu *format = topMenu(mw, "Format");
        QMenu *tools = topMenu(mw, "Tools");
        QStringList formatSubs, toolsTail;
        if (format)
            for (QAction *a : format->actions()) formatSubs << plain(a->text());
        EXPECT("M2: Format holds exactly Encoding, Language", formatSubs == QStringList({"Encoding", "Language"}));
        if (tools) {
            const QList<QAction *> acts = tools->actions();
            for (int i = std::max(0, int(acts.size()) - 5); i < acts.size(); ++i)
                toolsTail << (acts[i]->isSeparator() ? QStringLiteral("|") : plain(acts[i]->text()));
        }
        std::printf("     Tools ends with: %s\n", qPrintable(toolsTail.join(" ")));
        EXPECT("M2: Tools ends with a separator then Utilities, Plugins, Macro, Run",
               toolsTail == QStringList({"|", "Utilities", "Plugins", "Macro", "Run"}));
        for (const QString &name : {QStringLiteral("Utilities"), QStringLiteral("Plugins"),
                                    QStringLiteral("Macro"), QStringLiteral("Run")})
            EXPECT(qPrintable("M2: Tools > " + name + " is a submenu"), submenu(tools, name) != nullptr);
        const QChar fm = format ? mnemonic(format->title()) : QChar();
        int clashes = 0;
        for (QMenu *m : tops)
            if (m != format && mnemonic(m->title()) == fm) ++clashes;
        EXPECT("M2: Format has a mnemonic no other top-level menu uses", !fm.isNull() && clashes == 0);

        std::printf("-- M3: same actions in both layouts --\n");
        compactLeaves = allLeaves(mw);
        std::printf("     leaf actions: full %d, compact %d\n", int(fullLeaves.size()),
                    int(compactLeaves.size()));
        EXPECT("M3: leaf action count matches", !fullLeaves.isEmpty() && fullLeaves.size() == compactLeaves.size());
        EXPECT("M3: every leaf action text + shortcut matches", fullLeaves == compactLeaves);
        for (const QString &name : kMoved) {
            QMenu *m = (name == "Encoding" || name == "Language") ? submenu(format, name) : submenu(tools, name);
            const QStringList got = leavesOf(m);
            EXPECT(qPrintable("M3: moved " + name + " keeps its own items"),
                   !got.isEmpty() && got == fullMoved.value(name));
        }

        std::printf("-- M4: status-bar clicks (compact) --\n");
        EXPECT("M4: Language indicator opens the nested Language menu",
               popupAfter(mw, &NppStatusBar::languageClicked) == "Language");
        EXPECT("M4: Encoding indicator opens the nested Encoding menu",
               popupAfter(mw, &NppStatusBar::encodingClicked) == "Encoding");

        std::printf("-- M5: macOS text-heuristic roles --\n");
        int stray = 0;
        for (QMenu *m : tops)
            for (QAction *a : m->actions()) {
                if (a->isSeparator() || a->menuRole() != QAction::TextHeuristicRole) continue;
                if (!wouldBeHoisted(a->text())) continue;
                ++stray;
                std::printf("     would be hoisted: %s > %s\n", qPrintable(plain(m->title())),
                            qPrintable(plain(a->text())));
            }
        EXPECT("M5: no top-level item would be hoisted into the app menu", stray == 0);
    }

    std::printf("=== %d passed, %d failed ===\n", g_pass, g_fail);
    return g_fail == 0 ? 0 : 1;
}
