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

inline QString notepatraUiCssFamily() {
    return "\"Inter\", \"Segoe UI\", \"SF Pro Text\", \"Noto Sans\", \"Cantarell\", "
           "\"Ubuntu\", \"DejaVu Sans\", sans-serif";
}

inline QString notepatraCodeCssFamily() {
    return "\"JetBrains Mono\", \"Cascadia Code\", \"IBM Plex Mono\", \"SF Mono\", "
           "\"Menlo\", \"DejaVu Sans Mono\", \"Liberation Mono\", \"Noto Sans Mono\", "
           "\"Cousine\", \"Consolas\", monospace";
}

#endif
