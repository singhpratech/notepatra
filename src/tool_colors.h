// SPDX-License-Identifier: GPL-3.0-or-later
//
// v0.1.94 — single source of truth for Notepatra tool brand colors.
// Used by the tab accent strip (tabmanager.cpp), the feature-toolbar
// icons (mainwindow.cpp addFeatureShortcut), the Welcome cards
// (welcome.cpp), and any future surface that needs to draw "the colour
// of the Noter tool" / "the colour of REST tool" etc.
//
// Lookup is case- and space-insensitive, prefix-matched, so all of
// these resolve to the same Noter red:
//   "Noter"
//   "noter"
//   "Noter — Meeting Thinkpad        Ctrl+Alt+N"
//   "noter-card"

#pragma once

#include <QColor>
#include <QString>

// Returns the canonical brand colour for a Notepatra tool. Pass any
// reasonable descriptor (tab text, welcome-card action-id, etc.). On no
// match, returns an invalid QColor — callers should treat that as
// "neutral, don't paint an accent".
QColor notepatraToolAccent(const QString &toolKey);
