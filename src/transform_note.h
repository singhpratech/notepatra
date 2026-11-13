// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef TRANSFORM_NOTE_H
#define TRANSFORM_NOTE_H

// Outcome of a text transform, for the formatter panels.
//
// The JSON / bracket wrappers in rustbridge return plain QStrings (the panel
// buttons are `QString(const QString &)` callbacks), so they had no way to
// say "this is NOT a result: the input is invalid at line 3, column 7" — a
// declined repair came back as the unchanged text and the panel announced
// "done". The wrappers now post a note here; FormatterPanel clears it
// before running a button and reads it afterwards. An Error note means the
// returned text is the untouched input and must not be presented as output.
//
//   text     one line for the panel's status banner
//   details  the full report (every change / issue with line and column),
//            shown as the banner's tooltip; empty when `text` says it all
//   keepText the action only REPORTS on the text (Bracket Tools > Check):
//            the panel shows the note and leaves its text exactly as it was,
//            Info or Error alike
//
// Header-only (no link dependency), thread-local, first note wins (so in a
// "fix, then format" chain the reason the FIX declined is what is shown).

#include <QString>
#include <QStringList>

namespace TransformNote {

enum class Kind { None, Info, Error };

struct Note {
    Kind kind = Kind::None;
    QString text;
    QString details;
    bool keepText = false;
};

inline Note &slot() {
    thread_local Note n;
    return n;
}

inline void clear() { slot() = Note{}; }

inline void post(Kind kind, const QString &text, const QString &details = QString()) {
    if (slot().kind == Kind::None)
        slot() = Note{kind, text, details, false};
}

// A report about the text, not a new text: the panel must not touch it.
inline void postReport(Kind kind, const QString &text, const QString &details = QString()) {
    if (slot().kind == Kind::None)
        slot() = Note{kind, text, details, true};
}

inline Note take() {
    Note n = slot();
    clear();
    return n;
}

// One banner line from a multi-line Rust report ("Repaired 2 issue(s); ...:"
// then one "line L, column C: ..." per change): the headline plus the first
// positioned item, " (hover for the full report)" when there is more. A
// leading "No changes made: " is dropped (the panel already says so).
inline QString summarize(const QString &report) {
    QStringList lines;
    for (const QString &l : report.split(QLatin1Char('\n'))) {
        const QString t = l.trimmed();
        if (!t.isEmpty())
            lines << t;
    }
    if (lines.isEmpty())
        return QString();
    QString head = lines.takeFirst();
    const QLatin1String declined("No changes made: ");
    if (head.startsWith(declined) && head.size() > declined.size())
        head = head.mid(declined.size());
    if (lines.isEmpty())
        return head;
    while (head.endsWith(QLatin1Char(':')) || head.endsWith(QLatin1Char('.')))
        head.chop(1);
    // The bracket reports' "how the text was read" clause is long; it stays
    // in the details so the banner keeps room for the position.
    for (const QLatin1String clause :
         {QLatin1String(" (read as "), QLatin1String(", reading the text as ")}) {
        const int at = head.indexOf(clause);
        if (at > 0)
            head.truncate(at);
    }
    int first = 0;
    for (int i = 0; i < lines.size(); ++i) {
        if (lines[i].startsWith(QLatin1String("line ")) ||
            lines[i].startsWith(QLatin1String("keyword mismatch"))) {
            first = i;
            break;
        }
    }
    QString s = head + QStringLiteral(" — ") + lines[first];
    if (lines.size() > 1)
        s += QStringLiteral(" (hover for the full report)");
    return s;
}

} // namespace TransformNote

#endif // TRANSFORM_NOTE_H
