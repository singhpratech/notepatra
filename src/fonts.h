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
    const QString family = notepatraFirstAvailableFamily({
        "JetBrains Mono",
        "Cascadia Code",
        "IBM Plex Mono",
        "Monaspace Neon",
        "Monaspace Krypton",
        "Input Mono",
        "SF Mono",
        "Menlo",
        "DejaVu Sans Mono",
        "Liberation Mono",
        "Noto Sans Mono",
        "Cousine",
        "Consolas"
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
    return "\"JetBrains Mono\", \"Cascadia Code\", \"IBM Plex Mono\", \"SF Mono\", "
           "\"Menlo\", \"DejaVu Sans Mono\", \"Liberation Mono\", \"Noto Sans Mono\", "
           "\"Cousine\", \"Consolas\", " NOTEPATRA_EMOJI_FALLBACK ", monospace";
}

#endif
