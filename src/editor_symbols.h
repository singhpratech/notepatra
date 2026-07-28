// SPDX-License-Identifier: GPL-3.0-or-later
//
// Non-printing character display — the Notepad++ "Show Symbol" gap.
//
// Notepad++ renders invisible Unicode characters as small labelled blobs, so a
// file containing U+200B shows a visible "ZWSP". Notepatra drew nothing at all:
// the only symbol support was Scintilla's whitespace dots and EOL markers, so
// every zero-width, bidi, and exotic-space codepoint was literally invisible.
// That is the difference between "this file looks fine" and "this file has an
// invisible character breaking my build".
//
// The two tables below mirror Notepad++'s g_nonPrintingChars (49 entries) and
// g_ccUniEolChars (64 entries) exactly, including its abbreviations — parity
// means a user reading a Notepad++ screenshot sees the same four letters here.
//
// This is a free-function + table module rather than Editor methods, for the
// same reason applyNotepadPlusPalette() is public static: the table is the part
// worth testing, and testing it must not require constructing an Editor (which
// pulls in the Rust core).
//
// API CONSTRAINTS of the bundled Scintilla, established by runtime probe. Read
// these before touching the .cpp — every one of them is counter-intuitive:
//
//   * SCI_SETREPRESENTATION exists and renders. SCI_SETREPRESENTATIONAPPEARANCE,
//     SCI_SETREPRESENTATIONCOLOUR and SCI_CLEARALLREPRESENTATIONS DO NOT exist
//     in QScintilla 2.14.1 — they are absent from the header enum, and an
//     unsupported Scintilla message silently returns 0 instead of failing. So
//     there is no per-symbol colour and no Plain/Blob switch: representations
//     always draw as a blob and inherit the surrounding style.
//   * SCI_SETREPRESENTATION does NOT invalidate the line-layout cache. Setting
//     a representation while a file is already open has no visible effect at
//     all until something forces re-layout. See forceRelayout().
//   * SCI_CLEARREPRESENTATION takes its string in wParam. QScintilla's
//     SendScintilla(msg, const char *) overload puts it in lParam and leaves
//     wParam null, so the obvious-looking call SIGSEGVs.

#ifndef EDITOR_SYMBOLS_H
#define EDITOR_SYMBOLS_H

#include <QByteArray>
#include <QFlags>
#include <QString>
#include <QVector>

class QsciScintilla;

namespace EditorSymbols {

// The first two mirror Notepad++'s View > Show Symbol exactly. The third goes
// past it.
//
// Notepad++'s tables are a fixed 113 codepoints, so every invisible character
// it does not list is invisible in Notepad++ too — including variation
// selectors (U+FE0F), the Hangul fillers, and the Unicode TAG block
// (U+E0020-E007F) that is used to smuggle unreadable text into a file. Matching
// its tables byte-for-byte therefore inherits its blind spots, and "Show All
// Characters" would be a promise the feature could not keep.
enum Category {
    NoSymbols            = 0x00,
    NonPrinting          = 0x01,  // "Show Non-Printing Characters"      (49)
    ControlAndUnicodeEol = 0x02,  // "Show Control Characters & Unicode EOL" (64)
    OtherInvisible       = 0x04,  // everything else that draws nothing
};
Q_DECLARE_FLAGS(Categories, Category)

// Finer classification, used for tooltips and to let tests assert the tables
// were transcribed correctly rather than just counted.
enum Group {
    GroupSpace,       // NBSP and the typographic space zoo
    GroupFormat,      // ZWSP/ZWNJ/ZWJ/WJ/SHY/ZWNBSP and friends
    GroupBidi,        // LRM/RLM, embeddings, overrides, isolates
    GroupAnnotation,  // interlinear annotation
    GroupC0,          // U+0000-U+001F and U+007F
    GroupC1,          // U+0080-U+009F
    GroupUnicodeEol,  // NEL, LS, PS
    GroupVariation,   // variation selectors, incl. the 240 in plane 14
    GroupTag,         // U+E0001, U+E0020-U+E007F — invisible smuggled text
    GroupFiller,      // Hangul fillers, CGJ, Braille blank and friends
    GroupOtherFormat, // the rest of Unicode's Cf category
};

struct Entry {
    char32_t    codepoint;
    // What gets drawn, e.g. "ZWSP". NULL for the OtherInvisible ranges, where
    // there are hundreds of codepoints and no Notepad++ spelling to match:
    // labelFor() derives those (VS1..VS256, FVS1..3, TAG, else U+XXXX). Storing
    // 400-odd literals would be data that can drift from the codepoint beside it.
    const char *abbreviation;
    Category    category;
    Group       group;
};

// What the blob says. Notepad++ keys the same choice off its _npcMode enum.
//
// Notepad++ has a third mode, identity=0, which draws the character itself.
// It is deliberately not offered here: for these codepoints "the character
// itself" is by definition invisible, which is the exact problem this feature
// exists to solve, so identity is indistinguishable from switching the
// category off — a menu entry that silently undoes the menu.
enum DisplayMode {
    Abbreviation = 0,  // "ZWSP"   — Notepad++ _npcMode 1
    Codepoint    = 1,  // "U+200B" — Notepad++ _npcMode 2
};

// The text drawn for `e` in `mode`. Codepoint form is uppercase hex, zero
// padded to at least four digits, derived from Entry::codepoint rather than
// stored — a third table column would be redundant data free to drift out of
// step with the first.
QString labelFor(const Entry &e, DisplayMode mode);

// Both tables concatenated, in codepoint order within each category.
//
// TAB, LF and CR are deliberately absent even though they are C0: they belong
// to the "Show Space and Tab" and "Show End of Line" toggles, and giving them a
// representation here would break both. Notepad++ omits them for the same reason.
const QVector<Entry> &table();

// Every category we manage.
Categories allCategories();

// UTF-8 encoding of a single codepoint — the form every SCI_*REPRESENTATION
// message expects.
QByteArray utf8Of(char32_t cp);

// True where Scintilla ships its own visible representation, which is exactly
// the C0 range. It matters because for these, "off" is not the absence of a
// representation — see apply().
bool hasScintillaBuiltIn(char32_t cp);

// Install representations for `cats` and remove them for everything else.
// Idempotent, and safe to call with NoSymbols.
//
// Note the asymmetry, which is the trap in this whole feature: for most
// codepoints "off" means clearing the representation, but Scintilla draws C0
// controls as mnemonic blobs by DEFAULT. Simply declining to set a
// representation there leaves them visible, so turning the category off has to
// actively overwrite each one with an empty representation. Notepad++ hits the
// same wall and solves it the same way.
void apply(QsciScintilla *sci, Categories cats, DisplayMode mode = Abbreviation);

// apply() with the categories and mode the user has actually chosen.
//
// Lives here rather than on Editor because the views that need it most — the
// Compare panes and the two formatter output panels — are plain QsciScintilla,
// and routing them through Editor forced every test target that builds those
// files to link the whole editor and its Rust core.
void applyFromConfig(QsciScintilla *sci);

// Force Scintilla to re-lay-out every line.
//
// Exposed because it is the non-obvious half of apply(). The bundled Scintilla
// does not invalidate its line-layout cache when a representation changes, so
// toggling the menu on an already-open file would otherwise appear to do
// nothing until the user reloaded. Bouncing the layout cache to a different
// value and back is the cheapest invalidation that actually works —
// SCI_COLOURISE, recolor() and a viewport update were all measured to have no
// effect whatsoever.
void forceRelayout(QsciScintilla *sci);

}  // namespace EditorSymbols

Q_DECLARE_OPERATORS_FOR_FLAGS(EditorSymbols::Categories)

#endif  // EDITOR_SYMBOLS_H
