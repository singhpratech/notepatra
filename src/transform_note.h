// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef TRANSFORM_NOTE_H
#define TRANSFORM_NOTE_H

// One-line outcome of a text transform, for the formatter panels.
//
// The JSON / bracket wrappers in rustbridge return plain QStrings (the panel
// buttons are `QString(const QString &)` callbacks), so they had no way to
// say "this is NOT a result: the input is invalid at line 3, column 7" — a
// declined repair came back as the unchanged text and the panel announced
// "✓ done". The wrappers now post a note here; FormatterPanel clears it
// before running a button and reads it afterwards. An Error note means the
// returned text is the untouched input and must not be presented as output.
//
// Header-only (no link dependency), thread-local, first note wins (so in a
// "fix, then format" chain the reason the FIX declined is what is shown).

#include <QString>

namespace TransformNote {

enum class Kind { None, Info, Error };

struct Note {
    Kind kind = Kind::None;
    QString text;
};

inline Note &slot() {
    thread_local Note n;
    return n;
}

inline void clear() { slot() = Note{}; }

inline void post(Kind kind, const QString &text) {
    if (slot().kind == Kind::None)
        slot() = Note{kind, text};
}

inline Note take() {
    Note n = slot();
    clear();
    return n;
}

} // namespace TransformNote

#endif // TRANSFORM_NOTE_H
