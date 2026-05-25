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
#include <QPixmap>
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
    return 0;
}
