// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef NOTEPATRA_THEME_DETECT_H
#define NOTEPATRA_THEME_DETECT_H

// ────────────────────────────────────────────────────────────────────
// Shared theme detection + palette for panels that would otherwise
// hardcode dark-only colors and break on Light theme.
//
// Backstory: REST, Git, Terminal, and a few others shipped with every
// color frozen to dark-mode values (`#1E1E1E` / `#D4D4D4`). On Light
// theme the user got black backgrounds in the middle of a warm-paper
// canvas, sometimes with white text on white — unreadable.
//
// This header gives every panel one call (`npPalette()`) that returns
// the right colors for the current Config::theme. Callers interpolate
// them into their QString stylesheets. Kept header-only so adding a
// panel to the theme-aware set is a single `#include`.
// ────────────────────────────────────────────────────────────────────

#include "config.h"
#include "themes.h"   // detectSystemTheme() — needed to resolve "System"
#include <QString>

// Resolve Config::theme to a concrete name ("Light" / "Dark" / "Monokai").
// "System" → detectSystemTheme() (OS preference). Everything else passes
// through. Returns the *resolved* name so callers don't have to know
// about the System sentinel.
//
// Why this exists: Config::theme stores the user's *preference* literally
// — "System" stays "System" forever (we don't rewrite it on startup so
// that the next launch can re-detect). Anything that needs to *act on*
// the theme (panel chrome, lexer paint) must resolve that preference
// first. Without resolution, "System" failed every "is this Dark?"
// check and panels rendered light-on-dark on dark-OS users.
inline QString npResolvedThemeName() {
    QString t = Config::instance().theme;
    if (t.compare("System", Qt::CaseInsensitive) == 0)
        t = detectSystemTheme();
    return t;
}

inline bool npIsDarkTheme() {
    const QString t = npResolvedThemeName();
    return t.compare("Dark", Qt::CaseInsensitive) == 0 ||
           t.compare("Monokai", Qt::CaseInsensitive) == 0;
}

struct NpPalette {
    // Background layers, darkest outside to lightest inside
    QString bg;           // full-page background
    QString chromeBg;     // top strip / header band
    QString cardBg;       // card inside a page (secondary surface)
    // Foreground
    QString text;         // primary readable text
    QString textMuted;    // secondary labels
    QString accent;       // brand / primary action
    // Input controls
    QString inputBg;
    QString inputFg;
    QString inputBorder;
    QString inputFocus;
    // Buttons
    QString btnBg;
    QString btnBorder;
    QString btnHover;
    QString btnFg;
    // Common divider
    QString border;
    // Selection highlight inside lists / trees
    QString selectionBg;
    QString selectionFg;
    // Status/success/error signals
    QString successFg;
    QString warningFg;
    QString errorFg;
};

inline NpPalette npPalette() {
    NpPalette p;
    if (npIsDarkTheme()) {
        p.bg          = "#1E1E1E";
        p.chromeBg    = "#252526";
        p.cardBg      = "#2D2D2D";
        p.text        = "#D4D4D4";
        p.textMuted   = "#888888";
        p.accent      = "#4EC9B0";
        p.inputBg     = "#252526";
        p.inputFg     = "#D4D4D4";
        p.inputBorder = "#3E3E3E";
        p.inputFocus  = "#4EC9B0";
        p.btnBg       = "#2B2B2B";
        p.btnBorder   = "#3A3A3A";
        p.btnHover    = "#323232";
        p.btnFg       = "#DCDCDC";
        p.border      = "#3E3E3E";
        p.selectionBg = "#094771";
        p.selectionFg = "#FFFFFF";
        p.successFg   = "#76D275";
        p.warningFg   = "#F2C14E";
        p.errorFg     = "#F48771";
    } else {
        // Light / Clay palette — matches the rest of Notepatra on Light.
        p.bg          = "#FAF9F5";
        p.chromeBg    = "#F5F4EE";
        p.cardBg      = "#FFFFFF";
        p.text        = "#141413";
        p.textMuted   = "#8E8C88";
        p.accent      = "#CC785C";
        p.inputBg     = "#FFFFFF";
        p.inputFg     = "#141413";
        p.inputBorder = "#D4D1C4";
        p.inputFocus  = "#CC785C";
        p.btnBg       = "#FFFFFF";
        p.btnBorder   = "#D4D1C4";
        p.btnHover    = "#F5F4EE";
        p.btnFg       = "#141413";
        p.border      = "#D4D1C4";
        p.selectionBg = "#CC785C";
        p.selectionFg = "#FFFFFF";
        p.successFg   = "#1E7A1E";
        p.warningFg   = "#8E6B1A";
        p.errorFg     = "#B83A1C";
    }
    return p;
}

#endif  // NOTEPATRA_THEME_DETECT_H
