// SPDX-License-Identifier: GPL-3.0-or-later
//
// Render-dump harness — constructs a real NotesPanel, populates it with
// a couple of meetings + a todo, expands the tree, and grabs the whole
// widget to a PNG so we can VISUALLY verify there's no tofu / placeholder
// / overlap. Not a pass/fail test — it writes /tmp/noter-render.png +
// /tmp/noter-pencil.png for human (or multimodal) inspection.

#include "notes.h"

#include <QApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QKeySequence>
#include <QPixmap>
#include <QShortcut>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QToolButton>
#include <QTreeWidget>

#include <cstdio>

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
    return 0;
}
