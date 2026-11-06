// Menu shortcut hygiene + discoverability surfaces.
//  * No two enabled menu-bar actions / window QShortcuts share a key.
//  * macOS: Cmd+Q quits, Cmd+H is not Replace, Cmd+` is not Terminal, and
//    no menu item is wrongly hoisted into the app menu by Qt's text
//    heuristic. Linux/Windows keep Ctrl+Q for the AI toggle.
//  * Welcome has the Preview Pane / Data Analyst / Diagram / MCP cards and
//    no emoji icons; Help has Documentation + Show Welcome Tab; the
//    Feature Guide covers the four features.

#include "mainwindow.h"
#include "tabmanager.h"
#include "welcome.h"

#include <QAction>
#include <QApplication>
#include <QFile>
#include <QHash>
#include <QLabel>
#include <QMenu>
#include <QMenuBar>
#include <QRegularExpression>
#include <QSet>
#include <QShortcut>
#include <QTemporaryDir>
#include <QTextBrowser>

#include <cstdio>

static int g_pass = 0, g_fail = 0;
#define EXPECT(label, cond) \
    do { if (cond) { ++g_pass; std::printf("  [PASS] %s\n", label); } \
         else      { ++g_fail; std::printf("  [FAIL] %s\n", label); } } while (0)

static QString plain(const QString &t) { return QString(t).remove(QLatin1Char('&')).trimmed(); }

static void collect(QMenu *menu, QList<QAction *> &out, QSet<QAction *> &seen) {
    for (QAction *a : menu->actions()) {
        if (a->isSeparator() || seen.contains(a)) continue;
        seen.insert(a);
        out << a;
        if (a->menu()) collect(a->menu(), out, seen);
    }
}

static QAction *findByText(const QList<QAction *> &acts, const QString &text) {
    for (QAction *a : acts)
        if (plain(a->text()) == text) return a;
    return nullptr;
}

// Mirror of Qt 5's cocoa detectMenuRole() (qcocoahelpers.mm).
static QAction::MenuRole heuristicRole(const QString &caption) {
    const QString c = plain(caption);
    auto starts = [&](const char *w) { return c.startsWith(QLatin1String(w), Qt::CaseInsensitive); };
    if (starts("About") || c.endsWith(QLatin1String("About"), Qt::CaseInsensitive))
        return c.contains(QRegularExpression(QStringLiteral("qt$"),
                                             QRegularExpression::CaseInsensitiveOption))
                   ? QAction::AboutQtRole : QAction::AboutRole;
    if (starts("Config") || starts("Preference") || starts("Options") || starts("Setting")
        || starts("Setup"))
        return QAction::PreferencesRole;
    if (starts("Quit") || starts("Exit")) return QAction::QuitRole;
    return QAction::NoRole;
}

static bool hasEmoji(const QString &s) {
    for (uint cp : s.toUcs4())
        if ((cp >= 0x1F300 && cp <= 0x1FAFF) || (cp >= 0x2600 && cp <= 0x27BF)) return true;
    return false;
}

static int welcomeCount(TabManager *tabs, int *idx = nullptr) {
    int n = 0;
    for (int i = 0; i < tabs->count(); ++i)
        if (qobject_cast<WelcomeWidget *>(tabs->widget(i))) { ++n; if (idx) *idx = i; }
    return n;
}

int main(int argc, char *argv[]) {
    QTemporaryDir cfg;
    qputenv("XDG_CONFIG_HOME", cfg.path().toUtf8());
    qputenv("XDG_DATA_HOME",   cfg.path().toUtf8());
    qputenv("HOME", cfg.path().toUtf8());
#ifdef Q_OS_WIN
    qputenv("APPDATA",     cfg.path().toUtf8());
    qputenv("USERPROFILE", cfg.path().toUtf8());
#endif
    qputenv("QT_QPA_PLATFORM", "offscreen");

    QApplication app(argc, argv);
    std::printf("=== test_shortcuts_discoverability ===\n\n");

    MainWindow mw;
    mw.show();
    QApplication::processEvents();
    auto *tabs = mw.findChild<TabManager *>();
    EXPECT("TabManager found", tabs != nullptr);
    if (!tabs) return 1;

    QList<QAction *> all;
    QSet<QAction *> seen;
    QList<QMenu *> topMenus;
    for (QAction *top : mw.menuBar()->actions())
        if (top->menu()) { topMenus << top->menu(); collect(top->menu(), all, seen); }
    EXPECT("menu bar has menus", topMenus.size() >= 5);

    std::printf("Phase A — shortcut collisions\n");
    QHash<QString, QStringList> owners;
    for (QAction *a : all) {
        if (!a->isEnabled()) continue;
        for (const QKeySequence &k : a->shortcuts())
            if (!k.isEmpty()) owners[k.toString(QKeySequence::PortableText)] << plain(a->text());
    }
    for (QAction *a : mw.actions()) {
        if (seen.contains(a) || !a->isEnabled()) continue;
        for (const QKeySequence &k : a->shortcuts())
            if (!k.isEmpty()) owners[k.toString(QKeySequence::PortableText)] << plain(a->text());
    }
    for (QShortcut *sc : mw.findChildren<QShortcut *>()) {
        if (!sc->isEnabled() || sc->key().isEmpty()) continue;
        if (sc->context() != Qt::WindowShortcut && sc->context() != Qt::ApplicationShortcut) continue;
        if (sc->parentWidget() != &mw) continue;
        owners[sc->key().toString(QKeySequence::PortableText)] << QStringLiteral("<QShortcut>");
    }
    int collisions = 0;
    for (auto it = owners.cbegin(); it != owners.cend(); ++it) {
        if (it.value().size() < 2) continue;
        ++collisions;
        std::printf("    collision %s: %s\n", qPrintable(it.key()),
                    qPrintable(it.value().join(QStringLiteral(" | "))));
    }
    EXPECT("no two enabled actions share a shortcut", collisions == 0);

    std::printf("Phase B — platform bindings\n");
    QAction *ai = mw.findChild<QAction *>(QStringLiteral("actAiAssistant"));
    QAction *exitAct = findByText(all, QStringLiteral("Exit"));
    QAction *replaceAct = findByText(all, QStringLiteral("Replace..."));
    QAction *termAct = findByText(all, QStringLiteral("Terminal"));
    QAction *fsAct = findByText(all, QStringLiteral("Toggle Full-Screen Mode"));
    EXPECT("AI / Exit / Replace / Terminal / Full-Screen actions found",
           ai && exitAct && replaceAct && termAct && fsAct);
    if (!(ai && exitAct && replaceAct && termAct && fsAct)) return 1;
    const QKeySequence ctrlQ(QStringLiteral("Ctrl+Q"));
    EXPECT("AI keeps Ctrl+Shift+A",
           ai->shortcuts().contains(QKeySequence(QStringLiteral("Ctrl+Shift+A"))));
    EXPECT("AI label carries no hard-coded key hint", plain(ai->text()) == "AI Assistant");
#ifdef Q_OS_MACOS
    EXPECT("macOS: AI does not own Cmd+Q", !ai->shortcuts().contains(ctrlQ));
    EXPECT("macOS: Exit is Cmd+Q", exitAct->shortcut() == QKeySequence(QKeySequence::Quit));
    EXPECT("macOS: Replace is not Cmd+H",
           !replaceAct->shortcut().isEmpty()
               && replaceAct->shortcut() != QKeySequence(QStringLiteral("Ctrl+H")));
    EXPECT("macOS: Terminal is not Cmd+`",
           !termAct->shortcut().isEmpty()
               && termAct->shortcut() != QKeySequence(QStringLiteral("Ctrl+`")));
    EXPECT("macOS: Full screen is the standard key",
           fsAct->shortcut() == QKeySequence(QKeySequence::FullScreen));
#else
    EXPECT("AI keeps Ctrl+Q off macOS", ai->shortcuts().contains(ctrlQ));
    EXPECT("Exit stays Alt+F4", exitAct->shortcut() == QKeySequence(QStringLiteral("Alt+F4")));
    EXPECT("Replace stays Ctrl+H",
           replaceAct->shortcut() == QKeySequence(QStringLiteral("Ctrl+H")));
    EXPECT("Terminal stays Ctrl+`", termAct->shortcut() == QKeySequence(QStringLiteral("Ctrl+`")));
    EXPECT("Full screen stays F11", fsAct->shortcut() == QKeySequence(QStringLiteral("F11")));
#endif

    std::printf("Phase C — macOS app-menu roles\n");
    QAction *prefs = findByText(all, QStringLiteral("Preferences..."));
    QAction *about = findByText(all, QStringLiteral("About Notepatra"));
    EXPECT("Exit has QuitRole", exitAct->menuRole() == QAction::QuitRole);
    EXPECT("Preferences has PreferencesRole", prefs && prefs->menuRole() == QAction::PreferencesRole);
    EXPECT("About has AboutRole", about && about->menuRole() == QAction::AboutRole);
    int stray = 0;
    for (QMenu *m : topMenus) {
        for (QAction *a : m->actions()) {
            if (a->isSeparator() || a->menuRole() != QAction::TextHeuristicRole) continue;
            if (heuristicRole(a->text()) == QAction::NoRole) continue;
            ++stray;
            std::printf("    would be hoisted: %s\n", qPrintable(plain(a->text())));
        }
    }
    EXPECT("no top-level menu item is hoisted by Qt's text heuristic", stray == 0);

    std::printf("Phase D — Help menu + Welcome\n");
    QMenu *help = nullptr;
    for (QMenu *m : topMenus)
        if (plain(m->title()) == "Help") help = m;
    EXPECT("Help menu found", help != nullptr);
    if (!help) return 1;
    QAction *docs = findByText(help->actions(), QStringLiteral("Documentation"));
    QAction *showWelcome = findByText(help->actions(), QStringLiteral("Show Welcome Tab"));
    EXPECT("Help has Documentation", docs != nullptr);
    EXPECT("Help has Show Welcome Tab", showWelcome != nullptr);
    if (!showWelcome) return 1;

    while (welcomeCount(tabs) > 0) {
        int idx = -1;
        welcomeCount(tabs, &idx);
        QWidget *w = tabs->widget(idx);
        tabs->removeTab(idx);
        delete w;
    }
    showWelcome->trigger();
    QApplication::processEvents();
    int wIdx = -1;
    EXPECT("Show Welcome Tab opens the Welcome tab",
           welcomeCount(tabs, &wIdx) == 1 && tabs->currentIndex() == wIdx);
    if (wIdx < 0) return 1;
    tabs->setCurrentIndex(wIdx == 0 ? tabs->count() - 1 : 0);
    showWelcome->trigger();
    QApplication::processEvents();
    EXPECT("second trigger activates it without duplicating",
           welcomeCount(tabs) == 1 && tabs->currentIndex() == wIdx);

    auto *welcome = qobject_cast<WelcomeWidget *>(tabs->widget(wIdx));
    QStringList labels;
    for (QLabel *l : welcome->findChildren<QLabel *>()) labels << l->text();
    auto hasTitle = [&](const QString &t) {
        for (const QString &l : labels)
            if (l == t || l.startsWith(t + QLatin1String(" ("))) return true;
        return false;
    };
    EXPECT("Welcome card: Preview Pane", hasTitle(QStringLiteral("Preview Pane")));
    EXPECT("Welcome card: Data Analyst", hasTitle(QStringLiteral("Data Analyst")));
    EXPECT("Welcome card: Diagram", hasTitle(QStringLiteral("Diagram")));
    EXPECT("Welcome card: MCP", hasTitle(QStringLiteral("MCP")));
    bool emojiLabel = false;
    for (const QString &l : labels) emojiLabel = emojiLabel || hasEmoji(l);
    EXPECT("no Welcome label renders an emoji", !emojiLabel);
#ifndef Q_OS_MACOS
    bool aiRow = false;
    for (const QString &l : labels) aiRow = aiRow || l == "Ctrl+Q or Ctrl+Shift+A";
    EXPECT("Welcome lists both AI shortcuts", aiRow);
#endif

    std::printf("Phase E — Feature Guide\n");
    QAction *guide = findByText(help->actions(), QStringLiteral("Feature and Tool Guide..."));
    EXPECT("Feature Guide action found", guide != nullptr);
    if (guide) {
        guide->trigger();
        QApplication::processEvents();
        QString text;
        for (QTextBrowser *b : mw.findChildren<QTextBrowser *>()) text += b->toPlainText();
        EXPECT("guide covers Preview Pane", text.contains("Preview Pane"));
        EXPECT("guide covers Data Analyst", text.contains("Data Analyst"));
        EXPECT("guide covers Diagram", text.contains("Diagram"));
        EXPECT("guide covers the MCP sidecar", text.contains("notepatra-mcp"));
        EXPECT("guide has no unfilled %N placeholder",
               !text.contains(QRegularExpression(QStringLiteral("%[0-9]"))));
    }

    std::printf("Phase F — source guards\n");
    const QString src = QStringLiteral(NOTEPATRA_SOURCE_DIR);
    QFile wf(src + "/src/welcome.cpp");
    EXPECT("welcome.cpp readable", wf.open(QIODevice::ReadOnly));
    EXPECT("welcome.cpp has no emoji codepoints", !hasEmoji(QString::fromUtf8(wf.readAll())));
    QFile ef(src + "/src/editor.cpp");
    EXPECT("editor.cpp readable", ef.open(QIODevice::ReadOnly));
    EXPECT("editor context menu no longer claims Ctrl+Q",
           !QString::fromUtf8(ef.readAll()).contains(QLatin1String("QKeySequence(\"Ctrl+Q\")")));

    std::printf("\n%d passed, %d failed\n", g_pass, g_fail);
    return g_fail == 0 ? 0 : 1;
}
