// SPDX-License-Identifier: GPL-3.0-or-later
//
// notes_theme.h — Noter theme palette (A5: theme parity).
//
// Noter used to be a hardcoded light-cream island that ignored the app's
// Light/Dark/Monokai switch (dark-parity-mandatory rule violation). Every
// colour the Noter chrome paints now comes from a NoterPalette role; the
// three factories below are the single source of truth:
//
//   - noterLightPalette()   — byte-for-byte today's shipped colours. The
//     Light rendering is the zero-regression contract: do NOT "improve"
//     these values.
//   - noterDarkPalette()    — derived sympathetically from themes.h
//     darkTheme() (#1E1E1E editor / #252526 window).
//   - noterMonokaiPalette() — derived from themes.h monokaiTheme()
//     (#272822 editor / #1E1F1C window, pink/yellow accents).
//
// IMPORTANT SCOPE NOTE — chrome only. The note DOCUMENT on disk is an
// HTML artifact with its own template CSS and is never rewritten for a
// theme. Checklist done/undone colours below are applied as runtime
// QTextCharFormat only; the storage sanitizer strips inline style
// attributes on save, so themed colours can never leak into note files.
//
// "System" resolution intentionally mirrors AIPanel::aiIsDark(): the
// stored config key "System" maps to Light (the AI dock does the same),
// keeping panel behavior consistent and tests deterministic (no
// gsettings/OS probe from inside the panel).

#pragma once

#include <QColor>
#include <QPalette>
#include <QString>
#include <QWidget>

struct NoterPalette {
    // ── surfaces ───────────────────────────────────────────────────
    QString sidebarBg;        // sidebar pane background
    QString sidebarBorder;    // hairlines: splitter handle, footer top
    QString pageBg;           // editor + empty page background
    // ── inks ───────────────────────────────────────────────────────
    QString text;             // default chrome ink (sidebar rows, inputs)
    QString strongText;       // emphasized ink (editor body, selected leaf)
    QString mutedText;        // section headers, trashed rows, empty negs
    QString hintFg;           // "auto-saved" hint, AI: label, empty sub
    QString emptyTitleFg;     // big faint "Noter" on the empty page
    // ── inputs ─────────────────────────────────────────────────────
    QString inputBg;          // search box, model combo, empty-page hint card
    QString inputBorder;
    // ── interaction ────────────────────────────────────────────────
    QString hoverBg;          // tree item / todos button hover
    QString leafHighlight;    // selected tree row background
    QString leafHighlightFg;  // selected tree row text
    // ── accent / danger ────────────────────────────────────────────
    QString accent;           // + Noter / Extract buttons, hint key cap
    QString accentHover;
    QString danger;           // overdue leaves, NOT SAVED hint, error notice
    // ── menus (context menus spawned from Noter) ───────────────────
    QString menuBg, menuFg, menuBorder, menuSelBg, menuSelFg, menuSep;
    // ── reminder banner (amber, flashing) ──────────────────────────
    QString bannerFg;         // banner label text
    QString bannerBtnBg, bannerBtnHover;
    QString bannerBorder;     // bottom border
    QString bannerFlashA, bannerFlashB;   // alternating flash backgrounds
    // ── save-failure banner (red) ──────────────────────────────────
    QString saveFailBg, saveFailBorder, saveFailFg;
    QString saveFailBtnBg, saveFailBtnHover;
    // ── checklist runtime char formats (never persisted — see above) ─
    QString checkDoneFg;      // struck-through done line
    QString checkUndoneFg;    // reopened / normal checklist line
    // ── pop-out chrome ─────────────────────────────────────────────
    QString popoutBg, popoutBorder;
    QString popoutTitleBg, popoutTitleFg;
    QString popoutChipBg, popoutChipFg;   // mm:ss timer chip
    QString popoutHoverBg;                // titlebar button hover
    QString popoutBodyFg;                 // read-only body text
    // ── amber notices (sweep dialog "already scheduled"/truncation) ─
    QString noticeFg;
};

// Today's exact shipped colours — the Light zero-regression contract.
inline NoterPalette noterLightPalette() {
    NoterPalette p;
    p.sidebarBg      = QStringLiteral("#f3f1ea");
    p.sidebarBorder  = QStringLiteral("#e5e1d6");
    p.pageBg         = QStringLiteral("#fafaf6");
    p.text           = QStringLiteral("#525252");
    p.strongText     = QStringLiteral("#0a0d12");
    p.mutedText      = QStringLiteral("#94a3b8");
    p.hintFg         = QStringLiteral("#a0a0a0");
    p.emptyTitleFg   = QStringLiteral("#c8c4b8");
    p.inputBg        = QStringLiteral("white");
    p.inputBorder    = QStringLiteral("#d5d0c0");
    p.hoverBg        = QStringLiteral("#ede9dc");
    p.leafHighlight  = QStringLiteral("#fef3c7");
    p.leafHighlightFg= QStringLiteral("#0a0d12");
    p.accent         = QStringLiteral("#DC2626");
    p.accentHover    = QStringLiteral("#b91c1c");
    p.danger         = QStringLiteral("#DC2626");
    p.menuBg         = QStringLiteral("#FFFFFF");
    p.menuFg         = QStringLiteral("#111827");
    p.menuBorder     = QStringLiteral("#d5d0c0");
    p.menuSelBg      = QStringLiteral("#FEF3C7");
    p.menuSelFg      = QStringLiteral("#0a0d12");
    p.menuSep        = QStringLiteral("#e5e1d6");
    p.bannerFg       = QStringLiteral("#7c2d12");
    p.bannerBtnBg    = QStringLiteral("#7c2d12");
    p.bannerBtnHover = QStringLiteral("#9a3412");
    p.bannerBorder   = QStringLiteral("#D97706");
    p.bannerFlashA   = QStringLiteral("#FDE68A");
    p.bannerFlashB   = QStringLiteral("#FCD34D");
    p.saveFailBg     = QStringLiteral("#FEE2E2");
    p.saveFailBorder = QStringLiteral("#DC2626");
    p.saveFailFg     = QStringLiteral("#7F1D1D");
    p.saveFailBtnBg  = QStringLiteral("#DC2626");
    p.saveFailBtnHover = QStringLiteral("#B91C1C");
    p.checkDoneFg    = QStringLiteral("#9ca3af");
    p.checkUndoneFg  = QStringLiteral("#0a0d12");
    p.popoutBg       = QStringLiteral("#FFFFFF");
    p.popoutBorder   = QStringLiteral("#C7D2FE");
    p.popoutTitleBg  = QStringLiteral("#EEF2FF");
    p.popoutTitleFg  = QStringLiteral("#1E1B4B");
    p.popoutChipBg   = QStringLiteral("#C7D2FE");
    p.popoutChipFg   = QStringLiteral("#4338CA");
    p.popoutHoverBg  = QStringLiteral("#C7D2FE");
    p.popoutBodyFg   = QStringLiteral("#111827");
    p.noticeFg       = QStringLiteral("#B45309");
    return p;
}

// Sympathetic to themes.h darkTheme() — VS Code-ish neutral darks.
inline NoterPalette noterDarkPalette() {
    NoterPalette p;
    p.sidebarBg      = QStringLiteral("#252526");
    p.sidebarBorder  = QStringLiteral("#3c3c3c");
    p.pageBg         = QStringLiteral("#1e1e1e");
    p.text           = QStringLiteral("#cccccc");
    p.strongText     = QStringLiteral("#e8e8e8");
    p.mutedText      = QStringLiteral("#94a3b8");
    p.hintFg         = QStringLiteral("#909090");
    p.emptyTitleFg   = QStringLiteral("#5a5a55");
    p.inputBg        = QStringLiteral("#2d2d2d");
    p.inputBorder    = QStringLiteral("#3f3f46");
    p.hoverBg        = QStringLiteral("#2a2d2e");
    p.leafHighlight  = QStringLiteral("#37373d");
    p.leafHighlightFg= QStringLiteral("#ffffff");
    p.accent         = QStringLiteral("#DC2626");
    p.accentHover    = QStringLiteral("#ef4444");   // lighten on hover in dark
    p.danger         = QStringLiteral("#f87171");
    p.menuBg         = QStringLiteral("#2d2d2d");
    p.menuFg         = QStringLiteral("#cccccc");
    p.menuBorder     = QStringLiteral("#3f3f46");
    p.menuSelBg      = QStringLiteral("#094771");   // app dark menuHover
    p.menuSelFg      = QStringLiteral("#ffffff");
    p.menuSep        = QStringLiteral("#3f3f46");
    p.bannerFg       = QStringLiteral("#fde68a");
    p.bannerBtnBg    = QStringLiteral("#92400e");
    p.bannerBtnHover = QStringLiteral("#b45309");
    p.bannerBorder   = QStringLiteral("#D97706");
    p.bannerFlashA   = QStringLiteral("#423306");
    p.bannerFlashB   = QStringLiteral("#332805");
    p.saveFailBg     = QStringLiteral("#3b1111");
    p.saveFailBorder = QStringLiteral("#DC2626");
    p.saveFailFg     = QStringLiteral("#fca5a5");
    p.saveFailBtnBg  = QStringLiteral("#DC2626");
    p.saveFailBtnHover = QStringLiteral("#ef4444");
    p.checkDoneFg    = QStringLiteral("#6e7681");
    p.checkUndoneFg  = QStringLiteral("#d4d4d4");
    p.popoutBg       = QStringLiteral("#1e1e1e");
    p.popoutBorder   = QStringLiteral("#3f3f46");
    p.popoutTitleBg  = QStringLiteral("#2d2d31");
    p.popoutTitleFg  = QStringLiteral("#c7d2fe");
    p.popoutChipBg   = QStringLiteral("#3730a3");
    p.popoutChipFg   = QStringLiteral("#c7d2fe");
    p.popoutHoverBg  = QStringLiteral("#3730a3");
    p.popoutBodyFg   = QStringLiteral("#d4d4d4");
    p.noticeFg       = QStringLiteral("#fbbf24");
    return p;
}

// Sympathetic to themes.h monokaiTheme() — warm olive darks, pink accent.
inline NoterPalette noterMonokaiPalette() {
    NoterPalette p;
    p.sidebarBg      = QStringLiteral("#1e1f1c");
    p.sidebarBorder  = QStringLiteral("#464741");
    p.pageBg         = QStringLiteral("#272822");
    p.text           = QStringLiteral("#c5c4b9");
    p.strongText     = QStringLiteral("#f8f8f2");
    p.mutedText      = QStringLiteral("#90908a");
    p.hintFg         = QStringLiteral("#90908a");
    p.emptyTitleFg   = QStringLiteral("#57584f");
    p.inputBg        = QStringLiteral("#2d2e27");
    p.inputBorder    = QStringLiteral("#49483e");
    p.hoverBg        = QStringLiteral("#2d2e27");
    p.leafHighlight  = QStringLiteral("#49483e");
    p.leafHighlightFg= QStringLiteral("#f8f8f2");
    p.accent         = QStringLiteral("#f92672");   // monokai pink
    p.accentHover    = QStringLiteral("#fa5c95");
    p.danger         = QStringLiteral("#f92672");
    p.menuBg         = QStringLiteral("#2d2e27");
    p.menuFg         = QStringLiteral("#f8f8f2");
    p.menuBorder     = QStringLiteral("#464741");
    p.menuSelBg      = QStringLiteral("#49483e");
    p.menuSelFg      = QStringLiteral("#f8f8f2");
    p.menuSep        = QStringLiteral("#464741");
    p.bannerFg       = QStringLiteral("#e6db74");   // monokai yellow
    p.bannerBtnBg    = QStringLiteral("#75715e");
    p.bannerBtnHover = QStringLiteral("#8a8570");
    p.bannerBorder   = QStringLiteral("#e6db74");
    p.bannerFlashA   = QStringLiteral("#45431f");
    p.bannerFlashB   = QStringLiteral("#35341a");
    p.saveFailBg     = QStringLiteral("#3e1a24");
    p.saveFailBorder = QStringLiteral("#f92672");
    p.saveFailFg     = QStringLiteral("#f8b3c8");
    p.saveFailBtnBg  = QStringLiteral("#f92672");
    p.saveFailBtnHover = QStringLiteral("#fa5c95");
    p.checkDoneFg    = QStringLiteral("#75715e");
    p.checkUndoneFg  = QStringLiteral("#f8f8f2");
    p.popoutBg       = QStringLiteral("#272822");
    p.popoutBorder   = QStringLiteral("#49483e");
    p.popoutTitleBg  = QStringLiteral("#1e1f1c");
    p.popoutTitleFg  = QStringLiteral("#e6db74");
    p.popoutChipBg   = QStringLiteral("#49483e");
    p.popoutChipFg   = QStringLiteral("#e6db74");
    p.popoutHoverBg  = QStringLiteral("#49483e");
    p.popoutBodyFg   = QStringLiteral("#f8f8f2");
    p.noticeFg       = QStringLiteral("#e6db74");
    return p;
}

// Modal parity — give any Noter-spawned QDialog / QMessageBox an explicit
// QPalette + minimal QSS from the active palette so modals follow
// Light/Dark/Monokai instead of the system default. The calendar popup's
// item view needs explicit Base + a QSS background (item-view viewports
// paint from their OWN palette Base, never the parent's). No QLabel rule
// on purpose: labels inherit WindowText via the palette, so per-widget
// alpha-dimmed / accent label palettes keep winning.
inline void applyNoterDialogTheme(QWidget *w, const NoterPalette &pal) {
    QPalette p = w->palette();
    p.setColor(QPalette::Window,          QColor(pal.pageBg));
    p.setColor(QPalette::WindowText,      QColor(pal.text));
    p.setColor(QPalette::Base,            QColor(pal.inputBg));
    p.setColor(QPalette::Text,            QColor(pal.text));
    p.setColor(QPalette::Button,          QColor(pal.inputBg));
    p.setColor(QPalette::ButtonText,      QColor(pal.text));
    p.setColor(QPalette::Highlight,       QColor(pal.leafHighlight));
    p.setColor(QPalette::HighlightedText, QColor(pal.leafHighlightFg));
    w->setPalette(p);
    w->setStyleSheet(QStringLiteral(
        "QDialog, QMessageBox { background: %1; }"
        "QPushButton { background: %2; color: %3; border: 1px solid %4;"
        "  border-radius: 4px; padding: 4px 12px; }"
        "QPushButton:hover { background: %5; }"
        "QLineEdit, QDateTimeEdit, QComboBox, QAbstractSpinBox {"
        "  background: %2; color: %3; border: 1px solid %4;"
        "  border-radius: 4px; padding: 3px 6px; }"
        "QCalendarWidget QAbstractItemView { background: %2; color: %3;"
        "  selection-background-color: %6; selection-color: %7; }")
            .arg(pal.pageBg, pal.inputBg, pal.text, pal.inputBorder,
                 pal.hoverBg, pal.leafHighlight, pal.leafHighlightFg));
}

// Map a Config::theme key to a palette. "System" → Light, matching
// AIPanel::aiIsDark() — see header comment. Unknown names → Light.
inline NoterPalette noterPaletteForTheme(const QString &themeName) {
    if (themeName.compare(QStringLiteral("Dark"), Qt::CaseInsensitive) == 0)
        return noterDarkPalette();
    if (themeName.compare(QStringLiteral("Monokai"), Qt::CaseInsensitive) == 0)
        return noterMonokaiPalette();
    return noterLightPalette();
}
