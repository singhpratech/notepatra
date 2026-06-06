// SPDX-License-Identifier: GPL-3.0-or-later
//
// Render-dump harness — constructs a real NotesPanel, populates it with
// a couple of meetings + a todo, expands the tree, and grabs the whole
// widget to a PNG so we can VISUALLY verify there's no tofu / placeholder
// / overlap. Not a pass/fail test — it writes /tmp/noter-render.png +
// /tmp/noter-pencil.png for human (or multimodal) inspection.

#include "notes.h"
#include "notes_theme.h"
#include "notes_todos.h"
#include "config.h"

#include <QApplication>
#include <QColor>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QKeySequence>
#include <QLabel>
#include <QPixmap>
#include <QShortcut>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QTextCursor>
#include <QTextEdit>
#include <QToolButton>
#include <QTreeWidget>
#include <QWidget>

#include <algorithm>
#include <cmath>
#include <cstdio>

// ── A5 theme-parity helpers ─────────────────────────────────────────
// WCAG relative luminance + contrast ratio, computed straight from the
// palette hex strings (QColor parses both "#rrggbb" and named colours).
static double relLum(const QString &hex) {
    const QColor c(hex);
    auto lin = [](double v) {
        return v <= 0.03928 ? v / 12.92 : std::pow((v + 0.055) / 1.055, 2.4);
    };
    return 0.2126 * lin(c.redF()) + 0.7152 * lin(c.greenF()) +
           0.0722 * lin(c.blueF());
}
static double contrastRatio(const QString &fg, const QString &bg) {
    double a = relLum(fg), b = relLum(bg);
    if (a < b) std::swap(a, b);
    return (a + 0.05) / (b + 0.05);
}

int main(int argc, char *argv[]) {
    QApplication app(argc, argv);

    QTemporaryDir home;
    qputenv("HOME", home.path().toUtf8());
    qputenv("XDG_CONFIG_HOME", (home.path() + "/.config").toUtf8());
    QStandardPaths::setTestModeEnabled(true);
    QDir().mkpath(home.path() + "/Documents");

    NotesPanel panel;
    panel.resize(1100, 720);   // realistic Noter-tab width → sidebar ~280px

    // Create a few meetings + one quick todo so the tree has content.
    panel.newMeetingNote();
    panel.newMeetingNote();
    panel.newMeetingNote();

    auto *tree = panel.findChild<QTreeWidget *>(QStringLiteral("noterSidebarTree"));

    panel.show();
    for (int i = 0; i < 5; ++i) QApplication::processEvents();
    // Expand AFTER show — NotesPanel::showEvent auto-collapses the tree
    // (so Noter opens tidy), so expand here to render leaves (with pencil +
    // ✕ buttons) for visual inspection.
    if (tree) {
        tree->expandAll();
        for (int i = 0; i < 5; ++i) QApplication::processEvents();
    }

    // Grab the whole panel.
    QPixmap full = panel.grab();
    full.save(QStringLiteral("/tmp/noter-render.png"));
    std::printf("wrote /tmp/noter-render.png (%dx%d)\n", full.width(), full.height());

    // Grab the sidebar tree alone (tighter crop on the rows).
    if (tree) {
        QPixmap t = tree->grab();
        t.save(QStringLiteral("/tmp/noter-tree.png"));
        std::printf("wrote /tmp/noter-tree.png (%dx%d)\n", t.width(), t.height());
    }

    // Dump every QToolButton's icon so we can eyeball the pencil + ✕.
    const auto btns = panel.findChildren<QToolButton *>();
    std::printf("toolbuttons: %d\n", int(btns.size()));
    int idx = 0;
    for (auto *b : btns) {
        if (b->icon().isNull()) {
            std::printf("  btn[%d] tooltip='%s' ICON=NULL (placeholder risk!)\n",
                        idx, qPrintable(b->toolTip()));
        } else {
            QPixmap ip = b->icon().pixmap(32, 32);
            const QString f = QStringLiteral("/tmp/noter-icon-%1.png").arg(idx);
            ip.save(f);
            std::printf("  btn[%d] tooltip='%s' icon=%dx%d -> %s\n",
                        idx, qPrintable(b->toolTip()), ip.width(), ip.height(),
                        qPrintable(f));
        }
        ++idx;
    }

    // ── M6 discoverability gate (pass/fail) ────────────────────────────
    // Every QShortcut the live NotesPanel registers MUST be documented in
    // the Help guide's Noter section (mainwindow.cpp). The help HTML lives
    // in a static fn inside mainwindow.cpp which this harness doesn't link,
    // so we assert against the SOURCE — a new/changed Noter shortcut that
    // isn't added to Help fails this run.
    int docFailures = 0;
    {
        QStringList keys;
        const auto shortcuts = panel.findChildren<QShortcut *>();
        for (auto *sc : shortcuts) {
            const QString k = sc->key().toString(QKeySequence::PortableText);
            if (!k.isEmpty()) keys << k;
        }
        keys << QStringLiteral("Ctrl+Alt+N");  // global toggle (MainWindow-owned)
        keys.removeDuplicates();
        std::printf("noter shortcuts registered: %d\n", int(keys.size()));

        // mainwindow.cpp sits next to this file's src/ dir; __FILE__ is the
        // compile-time path of this harness at the repo root. Fall back to
        // cwd-relative for odd invocations.
        QString mwPath = QFileInfo(QString::fromUtf8(__FILE__))
                             .dir().filePath(QStringLiteral("src/mainwindow.cpp"));
        if (!QFileInfo::exists(mwPath))
            mwPath = QStringLiteral("src/mainwindow.cpp");

        QFile mw(mwPath);
        if (!mw.open(QIODevice::ReadOnly)) {
            std::printf("FAIL: cannot open %s to verify Help text\n",
                        qPrintable(mwPath));
            ++docFailures;
        } else {
            const QString src = QString::fromUtf8(mw.readAll());
            const int noterIdx = src.indexOf(QStringLiteral("<h3>Noter</h3>"));
            const int endIdx = noterIdx >= 0
                ? src.indexOf(QStringLiteral("<h2>"), noterIdx) : -1;
            if (noterIdx < 0 || endIdx < 0) {
                std::printf("FAIL: <h3>Noter</h3> help section not found in %s\n",
                            qPrintable(mwPath));
                ++docFailures;
            } else {
                const QString section = src.mid(noterIdx, endIdx - noterIdx);
                for (const QString &k : keys) {
                    const bool ok = section.contains(k);
                    std::printf("  help documents %-11s : %s\n",
                                qPrintable(k), ok ? "yes" : "MISSING");
                    if (!ok) ++docFailures;
                }
            }
            // Naming honesty — the Features action toggles, so its surfaces
            // must say so and advertise the new-note key; no stale "Open
            // Noter" claim may survive anywhere in mainwindow.cpp.
            struct Claim { const char *what; bool ok; };
            const Claim claims[] = {
                {"status tip mentions 'New note: Ctrl+Alt+M'",
                 src.contains(QStringLiteral("New note: Ctrl+Alt+M"))},
                {"a Noter surface says 'Toggle Noter'",
                 src.contains(QStringLiteral("Toggle Noter"))},
                {"no stale 'Open Noter' claim",
                 !src.contains(QStringLiteral("Open Noter"))},
            };
            for (const Claim &c : claims) {
                std::printf("  %-45s : %s\n", c.what, c.ok ? "yes" : "FAIL");
                if (!c.ok) ++docFailures;
            }
        }
    }
    if (docFailures > 0) {
        std::printf("noter-doc gate: %d failure(s)\n", docFailures);
        return 1;
    }
    std::printf("noter-doc gate: OK\n");

    // ── A5 theme-parity gate (pass/fail) ───────────────────────────────
    // Renders the Noter panel in ALL THREE themes (empty state, populated
    // sidebar, note with checklist, reminder banner) into
    // /tmp/noter_theme_shots/ for visual inspection, and asserts:
    //   (a) the chrome stylesheets actually differ per theme,
    //   (b) the Light palette still carries today's exact colours
    //       (zero-regression canary for the byte-identical contract),
    //   (c) WCAG-ish contrast (relative-luminance ratio >= 3) for the
    //       text-on-page / muted-on-page / banner-fg-on-banner-bg pairs.
    int themeFailures = 0;
    {
        auto check = [&themeFailures](const char *label, bool ok) {
            std::printf("  %-58s : %s\n", label, ok ? "yes" : "FAIL");
            if (!ok) ++themeFailures;
        };

        QDir().mkpath(QStringLiteral("/tmp/noter_theme_shots"));
        const struct { const char *cfg; const char *file; } themes[] = {
            {"Light", "light"}, {"Dark", "dark"}, {"Monokai", "monokai"},
        };

        QString sidebarQss[3], editorQss[3];
        for (int t = 0; t < 3; ++t) {
            Config::instance().theme = QString::fromLatin1(themes[t].cfg);

            NotesPanel p;                 // ctor reads Config::theme
            p.resize(1100, 720);
            p.show();
            for (int i = 0; i < 5; ++i) QApplication::processEvents();

            const QString base =
                QStringLiteral("/tmp/noter_theme_shots/%1_")
                    .arg(QLatin1String(themes[t].file));

            // 1. empty state
            p.grab().save(base + QStringLiteral("empty.png"));

            // 2. populated sidebar (meetings from the earlier harness run)
            if (auto *tr2 = p.findChild<QTreeWidget *>(
                    QStringLiteral("noterSidebarTree"))) {
                tr2->expandAll();
                for (int i = 0; i < 5; ++i) QApplication::processEvents();
            }
            p.grab().save(base + QStringLiteral("sidebar.png"));

            // 3. note with a ☐/✓ checklist (themed done/undone formats)
            p.newMeetingNote();
            QApplication::processEvents();
            if (auto *ed = p.findChild<QTextEdit *>(
                    QStringLiteral("noterEditor"))) {
                QTextCursor cur = ed->textCursor();
                cur.movePosition(QTextCursor::End);
                cur.insertText(QStringLiteral(
                    "\n☐ open themed item\n✓ done themed item"));
            }
            p.applyNoterTheme(QString::fromLatin1(themes[t].cfg));
            QApplication::processEvents();
            p.grab().save(base + QStringLiteral("checklist.png"));

            // 4. reminder banner visible (flash-off background applied by
            // applyNoterTheme so the shot doesn't depend on timer phase)
            TodoRow row;
            row.id = QStringLiteral("theme-shot-%1").arg(t);
            row.text = QStringLiteral("theme parity reminder");
            p.replayReminders({row});
            QApplication::processEvents();
            p.applyNoterTheme(QString::fromLatin1(themes[t].cfg));
            QApplication::processEvents();
            p.grab().save(base + QStringLiteral("banner.png"));
            std::printf("wrote /tmp/noter_theme_shots/%s_{empty,sidebar,"
                        "checklist,banner}.png\n", themes[t].file);

            if (auto *sb = p.findChild<QWidget *>(QStringLiteral("noterSidebar")))
                sidebarQss[t] = sb->styleSheet();
            if (auto *ep = p.findChild<QWidget *>(QStringLiteral("noterEditorPage")))
                editorQss[t] = ep->styleSheet();
        }
        Config::instance().theme = QStringLiteral("Light");   // restore

        // (a) stylesheets differ pairwise per theme
        check("sidebar QSS: Light != Dark",
              !sidebarQss[0].isEmpty() && sidebarQss[0] != sidebarQss[1]);
        check("sidebar QSS: Light != Monokai", sidebarQss[0] != sidebarQss[2]);
        check("sidebar QSS: Dark != Monokai",
              !sidebarQss[1].isEmpty() && sidebarQss[1] != sidebarQss[2]);
        check("editor-page QSS: Light != Dark",
              !editorQss[0].isEmpty() && editorQss[0] != editorQss[1]);
        check("editor-page QSS: Light != Monokai", editorQss[0] != editorQss[2]);
        check("editor-page QSS: Dark != Monokai",
              !editorQss[1].isEmpty() && editorQss[1] != editorQss[2]);

        // (b) Light canary — today's exact shipped colours still render
        check("Light sidebar keeps #f3f1ea cream",
              sidebarQss[0].contains(QStringLiteral("#f3f1ea")));
        check("Light sidebar keeps #fef3c7 leaf highlight",
              sidebarQss[0].contains(QStringLiteral("#fef3c7")));
        check("Light editor page keeps #fafaf6 paper",
              editorQss[0].contains(QStringLiteral("#fafaf6")));
        check("Light editor page keeps #DC2626 Extract red",
              editorQss[0].contains(QStringLiteral("#DC2626")));

        // (c) contrast pairs — ratio >= 3 (WCAG-ish large-text floor).
        // EXCEPTION, documented: Light mutedText (#94a3b8 on #fafaf6)
        // ships at ~2.4 today and is grandfathered byte-identical by the
        // zero-regression contract; it is asserted >= 2.2 so any further
        // drift still fails, while Dark/Monokai must clear the full 3.0.
        for (int t = 0; t < 3; ++t) {
            const NoterPalette pal =
                noterPaletteForTheme(QString::fromLatin1(themes[t].cfg));
            const QString n = QLatin1String(themes[t].cfg);
            auto pairCheck = [&](const char *what, const QString &fg,
                                 const QString &bg, double minRatio) {
                const double r = contrastRatio(fg, bg);
                std::printf("  [%-7s] %-38s ratio %.2f (>= %.1f) : %s\n",
                            qPrintable(n), what, r, minRatio,
                            r >= minRatio ? "yes" : "FAIL");
                if (r < minRatio) ++themeFailures;
            };
            pairCheck("editor text on page", pal.strongText, pal.pageBg, 3.0);
            pairCheck("sidebar text on sidebar", pal.text, pal.sidebarBg, 3.0);
            pairCheck("muted text on page", pal.mutedText, pal.pageBg,
                      t == 0 ? 2.2 : 3.0);
            pairCheck("banner fg on flash A", pal.bannerFg, pal.bannerFlashA, 3.0);
            pairCheck("banner fg on flash B", pal.bannerFg, pal.bannerFlashB, 3.0);
            pairCheck("save-fail fg on banner bg", pal.saveFailFg,
                      pal.saveFailBg, 3.0);
            pairCheck("checklist done on page", pal.checkDoneFg, pal.pageBg,
                      t == 0 ? 2.2 : 3.0);
            pairCheck("checklist undone on page", pal.checkUndoneFg,
                      pal.pageBg, 3.0);
        }
    }
    if (themeFailures > 0) {
        std::printf("noter-theme gate: %d failure(s)\n", themeFailures);
        return 1;
    }
    std::printf("noter-theme gate: OK\n");
    return 0;
}
