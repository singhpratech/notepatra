// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef SHORTCUT_HINTS_H
#define SHORTCUT_HINTS_H

// Platform shortcut SSOT (Qt maps "Ctrl" to Cmd on macOS) + NativeText hints.

#include <QKeySequence>
#include <QList>
#include <QString>
#include <QStringList>

namespace Shortcuts {

// Cmd+Q must stay Quit on macOS, so the AI toggle keeps only Ctrl+Shift+A.
inline QList<QKeySequence> aiAssistant() {
#ifdef Q_OS_MACOS
    return {QKeySequence(QStringLiteral("Ctrl+Shift+A"))};
#else
    return {QKeySequence(QStringLiteral("Ctrl+Q")), QKeySequence(QStringLiteral("Ctrl+Shift+A"))};
#endif
}

inline QKeySequence quit() {
#ifdef Q_OS_MACOS
    return QKeySequence(QKeySequence::Quit);
#else
    return QKeySequence(QStringLiteral("Alt+F4"));
#endif
}

// Cmd+H is the system Hide on macOS; Qt has no mac Replace binding.
inline QKeySequence replace() {
#ifdef Q_OS_MACOS
    for (const QKeySequence &k : QKeySequence::keyBindings(QKeySequence::Replace))
        if (k != QKeySequence(QStringLiteral("Ctrl+H"))) return k;
    return QKeySequence(QStringLiteral("Ctrl+Alt+F"));
#else
    return QKeySequence(QStringLiteral("Ctrl+H"));
#endif
}

inline QKeySequence fullScreen() {
#ifdef Q_OS_MACOS
    return QKeySequence(QKeySequence::FullScreen);
#else
    return QKeySequence(QStringLiteral("F11"));
#endif
}

// Cmd+` cycles windows on macOS; Control+` matches VS Code's mac terminal key.
inline QKeySequence terminal() {
#ifdef Q_OS_MACOS
    return QKeySequence(QStringLiteral("Meta+`"));
#else
    return QKeySequence(QStringLiteral("Ctrl+`"));
#endif
}

inline QString nativeText(const QKeySequence &k) {
    return k.toString(QKeySequence::NativeText);
}

inline QString hint(const QList<QKeySequence> &keys) {
    QStringList parts;
    for (const QKeySequence &k : keys) parts << nativeText(k);
    return parts.join(QStringLiteral(" or "));
}

inline QString aiAssistantHint() { return hint(aiAssistant()); }
inline QString aiAssistantPrimary() { return nativeText(aiAssistant().first()); }

} // namespace Shortcuts

#endif // SHORTCUT_HINTS_H
