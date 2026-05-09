#ifndef NOTEPATRA_FONTS_H
#define NOTEPATRA_FONTS_H

#include <QFont>
#include <QFontDatabase>
#include <QStringList>

#include "config.h"

inline QString notepatraFirstAvailableFamily(const QStringList &candidates) {
    const QStringList installed = QFontDatabase().families();
    for (const QString &candidate : candidates) {
        for (const QString &family : installed) {
            if (family.compare(candidate, Qt::CaseInsensitive) == 0)
                return family;
        }
    }
    return QString();
}

inline QString notepatraDefaultUiFamily() {
    const QString family = notepatraFirstAvailableFamily({
        "Inter",
        "SF Pro Text",
        "Segoe UI",
        "Noto Sans",
        "Cantarell",
        "Ubuntu",
        "DejaVu Sans",
        "Arial"
    });
    return family.isEmpty() ? QStringLiteral("Sans Serif") : family;
}

inline QString notepatraDefaultCodeFamily() {
    // v0.1.42 — broadened the curated monospace chain. The picker walks
    // this list top-down and uses the first one installed on the user's
    // system. To override, set Config::fontFamily in
    // ~/.config/notepatra/config.json (UI font picker arrives in v0.1.43).
    //
    // Order: hand-tuned. Modern proportional-ligature designer fonts
    // first (Geist / Berkeley / MonoLisa / Commit Mono / JetBrains /
    // Cascadia / Monaspace / IBM Plex), then platform-native classics
    // (SF Mono / Menlo / Consolas), then the always-available Linux
    // distro defaults (DejaVu / Liberation / Noto Sans Mono / Cousine).
    const QString family = notepatraFirstAvailableFamily({
        "Geist Mono",          // Vercel, 2024 — modern, very readable
        "Berkeley Mono",       // Berkeley Graphics — premium, popular w/ devs
        "MonoLisa",            // monolisa.dev — paid but very common
        "Commit Mono",         // commitmono.com — free, neutral, modern
        "JetBrains Mono",      // JetBrains — bundled with their IDEs
        "Cascadia Code",       // Microsoft — Windows Terminal default
        "IBM Plex Mono",       // IBM — well-tested across platforms
        "Monaspace Neon",      // GitHub Next — texture-healing variants
        "Monaspace Krypton",
        "Input Mono",          // input.djr.com
        "Fira Code",           // Mozilla, ligatures
        "SF Mono",             // Apple system mono (macOS)
        "Menlo",               // macOS classic
        "Consolas",            // Windows classic
        "DejaVu Sans Mono",    // Linux/Debian default
        "Liberation Mono",     // Red Hat / Fedora default
        "Noto Sans Mono",      // Google fallback
        "Cousine"              // Chromebook / Croscoreboard default
    });
    return family.isEmpty() ? QStringLiteral("Monospace") : family;
}

inline QString notepatraResolvedCodeFamily() {
    const QString configured = Config::instance().fontFamily.trimmed();
    if (!configured.isEmpty() && configured.compare("Consolas", Qt::CaseInsensitive) != 0)
        return configured;
    return notepatraDefaultCodeFamily();
}

inline QFont notepatraUiFont(int pointSize = -1, int weight = QFont::Normal) {
    QFont font(notepatraDefaultUiFamily(), pointSize > 0 ? pointSize : 10, weight);
    font.setStyleStrategy(QFont::PreferAntialias);
    return font;
}

// v0.1.55 — UI font WITH the emoji fallback chain baked in via
// QFont::setFamilies(). Use this for any QLabel / QPushButton / etc. that
// will display emoji glyphs (🌐 💡 🔀 🔎 🤖 etc.). The plain
// notepatraUiFont() above only sets a single family, so on Linux where
// the chosen family (DejaVu Sans, Cantarell, Inter, …) has no emoji
// glyphs, those codepoints render as tofu □. setFamilies() lets Qt's
// text shaper try each family in order until one has the glyph — same
// pattern as CSS `font-family: A, B, C` for QSS-rendered text. Qt5.13+
// supports setFamilies; for older Qt the call is a no-op fallback to
// setFamily(first). The chain mirrors NOTEPATRA_EMOJI_FALLBACK above.
inline QFont notepatraUiFontWithEmoji(int pointSize = -1,
                                      int weight = QFont::Normal) {
    QFont font(notepatraDefaultUiFamily(), pointSize > 0 ? pointSize : 10, weight);
    font.setStyleStrategy(QFont::PreferAntialias);
    QStringList families;
    families << notepatraDefaultUiFamily()
             << "Inter" << "Segoe UI" << "SF Pro Text" << "Noto Sans"
             << "Cantarell" << "Ubuntu" << "DejaVu Sans"
             // Emoji fallbacks — Qt walks these in order for any
             // codepoint the primary family doesn't have.
             << "Apple Color Emoji" << "Segoe UI Emoji"
             << "Noto Color Emoji" << "Twemoji Mozilla"
             << "Twitter Color Emoji" << "Symbola";
    font.setFamilies(families);
    return font;
}

inline QFont notepatraCodeFont(int pointSize = -1, int weight = QFont::Normal) {
    const int resolvedPointSize = pointSize > 0 ? pointSize : qMax(10, Config::instance().fontSize);
    QFont font(notepatraResolvedCodeFamily(), resolvedPointSize, weight);
    font.setFixedPitch(true);
    font.setStyleHint(QFont::Monospace, QFont::PreferAntialias);
    font.setStyleStrategy(QFont::PreferAntialias);
    return font;
}

// Emoji fallback chain — listed AFTER the text fonts so Qt's HarfBuzz
// text shaper only consults them for codepoints the primary font lacks
// (e.g. SMP-plane emoji like 👋 U+1F44B). Order: macOS → Windows →
// Linux/Android color → Linux monochrome (Symbola) → Twemoji 3rd-party.
//
// v0.1.33 added this after a user reported Linux AI Assistant responses
// showing tofu □ for emoji while Windows rendered them fine — Windows'
// "Segoe UI" family natively chains to "Segoe UI Emoji"; Linux text fonts
// (Noto Sans, DejaVu, Inter, etc.) don't ship emoji glyphs so the chain
// must be explicit.
#define NOTEPATRA_EMOJI_FALLBACK \
    "\"Apple Color Emoji\", \"Segoe UI Emoji\", \"Noto Color Emoji\", " \
    "\"Twemoji Mozilla\", \"Twitter Color Emoji\", \"Symbola\""

inline QString notepatraUiCssFamily() {
    return "\"Inter\", \"Segoe UI\", \"SF Pro Text\", \"Noto Sans\", \"Cantarell\", "
           "\"Ubuntu\", \"DejaVu Sans\", " NOTEPATRA_EMOJI_FALLBACK ", sans-serif";
}

inline QString notepatraCodeCssFamily() {
    return "\"Geist Mono\", \"Berkeley Mono\", \"MonoLisa\", \"Commit Mono\", "
           "\"JetBrains Mono\", \"Cascadia Code\", \"IBM Plex Mono\", "
           "\"Monaspace Neon\", \"Fira Code\", \"SF Mono\", \"Menlo\", "
           "\"Consolas\", \"DejaVu Sans Mono\", \"Liberation Mono\", "
           "\"Noto Sans Mono\", \"Cousine\", " NOTEPATRA_EMOJI_FALLBACK ", monospace";
}

#endif
