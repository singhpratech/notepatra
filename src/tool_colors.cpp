// SPDX-License-Identifier: GPL-3.0-or-later

#include "tool_colors.h"

QColor notepatraToolAccent(const QString &toolKey) {
    // Normalise — lowercase + strip spaces/dashes/underscores so any
    // descriptor variant ("Project Search", "ProjectSearch", "project-
    // search", "PROJECT_SEARCH") hits the same branch.
    QString k = toolKey.toLower();
    k.remove(QChar(' '));
    k.remove(QChar('-'));
    k.remove(QChar('_'));

    // Order matters: longer prefixes first so "project" beats "p".
    if (k.startsWith(QStringLiteral("welcome")))         return QColor();
    if (k.startsWith(QStringLiteral("projectsearch")) ||
        k.startsWith(QStringLiteral("search")))          return QColor("#D47A1E"); // orange — USER PINNED
    if (k.startsWith(QStringLiteral("ai")))              return QColor("#2563EB"); // royal blue 222°
    if (k.startsWith(QStringLiteral("terminal")))        return QColor("#15803D"); // forest green 145°
    if (k.startsWith(QStringLiteral("compare")))         return QColor("#CA8A04"); // deep gold 41°
    if (k.startsWith(QStringLiteral("json")))            return QColor("#0891B2"); // cyan 191°
    if (k.startsWith(QStringLiteral("html")))            return QColor("#DB2777"); // hot pink 330°
    if (k.startsWith(QStringLiteral("sql")))             return QColor("#7C3AED"); // violet 262°
    if (k.startsWith(QStringLiteral("bracket")))         return QColor("#78350F"); // saddle brown 28°
    if (k.startsWith(QStringLiteral("rest")))            return QColor("#0D9488"); // teal 173°
    if (k.startsWith(QStringLiteral("noter")))           return QColor("#DC2626"); // red 0°
    if (k.startsWith(QStringLiteral("diagram")))         return QColor("#4F46E5"); // indigo 244°
    if (k.startsWith(QStringLiteral("git")))             return QColor("#65A30D"); // lime green 74°
    if (k.startsWith(QStringLiteral("hex")))             return QColor("#475569"); // slate gray
    return QColor();
}
