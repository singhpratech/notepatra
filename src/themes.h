#ifndef THEMES_H
#define THEMES_H

#include <QString>
#include <QColor>
#include <QMap>

struct Theme {
    QString name;
    // Editor
    QColor editorBg, editorFg, caretLine, selection, caret;
    QColor marginBg, marginFg, foldBg;
    QColor matchedBraceBg, matchedBraceFg;
    // UI
    QColor windowBg, windowFg, toolbarBg;
    QColor tabBg, tabActiveBg, tabFg, tabBorder;
    QColor statusBg, statusFg;
    QColor menuBg, menuFg, menuHover;
    QColor scrollBg, scrollHandle, scrollHover;
};

inline Theme lightTheme() {
    Theme t;
    t.name = "Light";
    t.editorBg = QColor("#FFFFFF"); t.editorFg = QColor("#000000");
    t.caretLine = QColor("#E8F5E9"); t.selection = QColor("#5BC8FA"); t.caret = QColor("#000000");
    t.marginBg = QColor("#E4E4E4"); t.marginFg = QColor("#2B91AF"); t.foldBg = QColor("#F0F0F0");
    t.matchedBraceBg = QColor("#FFCCCC"); t.matchedBraceFg = QColor("#CC0000");
    t.windowBg = QColor("#F0F0F0"); t.windowFg = QColor("#000000"); t.toolbarBg = QColor("#F0F0F0");
    t.tabBg = QColor("#ECECEC"); t.tabActiveBg = QColor("#FFFFFF"); t.tabFg = QColor("#000000"); t.tabBorder = QColor("#C0C0C0");
    t.statusBg = QColor("#C8C8C8"); t.statusFg = QColor("#000000");
    t.menuBg = QColor("#F0F0F0"); t.menuFg = QColor("#000000"); t.menuHover = QColor("#ADD6FF");
    t.scrollBg = QColor("#E8E8E8"); t.scrollHandle = QColor("#A0A0A0"); t.scrollHover = QColor("#888888");
    return t;
}

inline Theme darkTheme() {
    Theme t;
    t.name = "Dark";
    t.editorBg = QColor("#1E1E1E"); t.editorFg = QColor("#D4D4D4");
    t.caretLine = QColor("#2A2D2E"); t.selection = QColor("#264F78"); t.caret = QColor("#AEAFAD");
    t.marginBg = QColor("#1E1E1E"); t.marginFg = QColor("#858585"); t.foldBg = QColor("#252526");
    t.matchedBraceBg = QColor("#4E4E4E"); t.matchedBraceFg = QColor("#00FF00");
    t.windowBg = QColor("#252526"); t.windowFg = QColor("#CCCCCC"); t.toolbarBg = QColor("#333333");
    t.tabBg = QColor("#2D2D2D"); t.tabActiveBg = QColor("#1E1E1E"); t.tabFg = QColor("#CCCCCC"); t.tabBorder = QColor("#404040");
    t.statusBg = QColor("#007ACC"); t.statusFg = QColor("#FFFFFF");
    t.menuBg = QColor("#2D2D2D"); t.menuFg = QColor("#CCCCCC"); t.menuHover = QColor("#094771");
    t.scrollBg = QColor("#1E1E1E"); t.scrollHandle = QColor("#424242"); t.scrollHover = QColor("#555555");
    return t;
}

inline Theme monokaiTheme() {
    Theme t;
    t.name = "Monokai";
    t.editorBg = QColor("#272822"); t.editorFg = QColor("#F8F8F2");
    t.caretLine = QColor("#3E3D32"); t.selection = QColor("#49483E"); t.caret = QColor("#F8F8F0");
    t.marginBg = QColor("#272822"); t.marginFg = QColor("#90908A"); t.foldBg = QColor("#2D2E27");
    t.matchedBraceBg = QColor("#49483E"); t.matchedBraceFg = QColor("#E6DB74");
    t.windowBg = QColor("#1E1F1C"); t.windowFg = QColor("#F8F8F2"); t.toolbarBg = QColor("#1E1F1C");
    t.tabBg = QColor("#2D2E27"); t.tabActiveBg = QColor("#272822"); t.tabFg = QColor("#F8F8F2"); t.tabBorder = QColor("#464741");
    t.statusBg = QColor("#414339"); t.statusFg = QColor("#F8F8F2");
    t.menuBg = QColor("#2D2E27"); t.menuFg = QColor("#F8F8F2"); t.menuHover = QColor("#49483E");
    t.scrollBg = QColor("#272822"); t.scrollHandle = QColor("#49483E"); t.scrollHover = QColor("#75715E");
    return t;
}

inline QMap<QString, Theme> allThemes() {
    QMap<QString, Theme> m;
    m["Light"] = lightTheme();
    m["Dark"] = darkTheme();
    m["Monokai"] = monokaiTheme();
    return m;
}

// Detect the current OS/desktop colour-scheme preference.
// Returns "Dark" if the system prefers dark UI, "Light" otherwise.
// Probes (in order):
//   • Qt 6.5+ QStyleHints::colorScheme()  (not available on our Qt 5 build,
//     falls through)
//   • macOS       → `defaults read -g AppleInterfaceStyle`  (prints
//                    "Dark\n" on dark, fails on light)
//   • Windows     → HKCU\Software\Microsoft\Windows\CurrentVersion
//                    \Themes\Personalize\AppsUseLightTheme  (DWORD: 0=dark)
//   • GNOME/Linux → `gsettings get org.gnome.desktop.interface color-scheme`
//                    or prefer-dark theme hint from gtk-theme
//   • env var     → respect $NOTEPATRA_THEME / $COLOR_SCHEME / $GTK_THEME
// Falls back to "Light" when nothing is detected — matches the request
// "default to Light when the system preference can't be read".
#include <QProcess>
#include <QByteArray>
#include <QSettings>

inline QString detectSystemTheme() {
    // 1. Explicit override via env so power users / CI can pin this
    QByteArray np = qgetenv("NOTEPATRA_THEME");
    if (!np.isEmpty()) return QString::fromUtf8(np);

#if defined(Q_OS_MAC)
    QProcess p;
    p.start("defaults", {"read", "-g", "AppleInterfaceStyle"});
    p.waitForFinished(800);
    QString out = QString::fromUtf8(p.readAllStandardOutput()).trimmed();
    if (out.compare("Dark", Qt::CaseInsensitive) == 0) return "Dark";
    return "Light";
#elif defined(Q_OS_WIN)
    QSettings s("HKEY_CURRENT_USER\\Software\\Microsoft\\Windows\\"
                "CurrentVersion\\Themes\\Personalize",
                QSettings::NativeFormat);
    QVariant v = s.value("AppsUseLightTheme");
    if (v.isValid()) return v.toInt() == 0 ? "Dark" : "Light";
    return "Light";
#else
    // GNOME / Linux — gsettings is present on every mainstream distro
    QProcess p;
    p.start("gsettings", {"get", "org.gnome.desktop.interface", "color-scheme"});
    p.waitForFinished(500);
    QString out = QString::fromUtf8(p.readAllStandardOutput()).toLower();
    if (out.contains("dark")) return "Dark";
    if (out.contains("light")) return "Light";
    // Fallback: probe gtk-theme name
    p.start("gsettings", {"get", "org.gnome.desktop.interface", "gtk-theme"});
    p.waitForFinished(500);
    out = QString::fromUtf8(p.readAllStandardOutput()).toLower();
    if (out.contains("dark")) return "Dark";
    // Env fallbacks
    QByteArray gtkTheme = qgetenv("GTK_THEME");
    if (QString::fromUtf8(gtkTheme).toLower().contains("dark")) return "Dark";
    return "Light";
#endif
}

// Resolve a theme name stored in Config::theme to a concrete Theme
// struct. "System" → detectSystemTheme(); everything else looks up the
// static map. Callers should use this instead of the raw allThemes()
// lookup so the "System" sentinel is handled centrally.
inline Theme resolveTheme(const QString &name) {
    QString effective = name;
    if (effective.compare("System", Qt::CaseInsensitive) == 0)
        effective = detectSystemTheme();
    auto m = allThemes();
    if (m.contains(effective)) return m[effective];
    return lightTheme();   // final fallback
}

#endif
