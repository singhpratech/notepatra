// SPDX-License-Identifier: GPL-3.0-or-later

#include "editor_symbols.h"

#include <Qsci/qsciscintilla.h>

#include <cstdint>

namespace EditorSymbols {

namespace {

// Notepad++'s g_nonPrintingChars and g_ccUniEolChars, transcribed. The
// abbreviations are Notepad++'s, which are in turn the Unicode chart
// abbreviations — "ZWNBSP" for U+FEFF rather than the colloquial "BOM", and
// "OSPM" for U+1680. Diverging here would defeat the point.
const Entry kEntries[] = {
    // ── Table A: non-printing characters (49) ────────────────────────────
    { 0x00A0, "NBSP",   NonPrinting, GroupSpace  },
    { 0x00AD, "SHY",    NonPrinting, GroupFormat },
    { 0x061C, "ALM",    NonPrinting, GroupBidi   },
    { 0x070F, "SAM",    NonPrinting, GroupFormat },
    { 0x1680, "OSPM",   NonPrinting, GroupSpace  },
    { 0x180E, "MVS",    NonPrinting, GroupFormat },
    { 0x2000, "NQSP",   NonPrinting, GroupSpace  },
    { 0x2001, "MQSP",   NonPrinting, GroupSpace  },
    { 0x2002, "ENSP",   NonPrinting, GroupSpace  },
    { 0x2003, "EMSP",   NonPrinting, GroupSpace  },
    { 0x2004, "3/MSP",  NonPrinting, GroupSpace  },
    { 0x2005, "4/MSP",  NonPrinting, GroupSpace  },
    { 0x2006, "6/MSP",  NonPrinting, GroupSpace  },
    { 0x2007, "FSP",    NonPrinting, GroupSpace  },
    { 0x2008, "PSP",    NonPrinting, GroupSpace  },
    { 0x2009, "THSP",   NonPrinting, GroupSpace  },
    { 0x200A, "HSP",    NonPrinting, GroupSpace  },
    { 0x200B, "ZWSP",   NonPrinting, GroupFormat },
    { 0x200C, "ZWNJ",   NonPrinting, GroupFormat },
    { 0x200D, "ZWJ",    NonPrinting, GroupFormat },
    { 0x200E, "LRM",    NonPrinting, GroupBidi   },
    { 0x200F, "RLM",    NonPrinting, GroupBidi   },
    { 0x202A, "LRE",    NonPrinting, GroupBidi   },
    { 0x202B, "RLE",    NonPrinting, GroupBidi   },
    { 0x202C, "PDF",    NonPrinting, GroupBidi   },
    { 0x202D, "LRO",    NonPrinting, GroupBidi   },
    { 0x202E, "RLO",    NonPrinting, GroupBidi   },
    { 0x202F, "NNBSP",  NonPrinting, GroupSpace  },
    { 0x205F, "MMSP",   NonPrinting, GroupSpace  },
    { 0x2060, "WJ",     NonPrinting, GroupFormat },
    { 0x2061, "(FA)",   NonPrinting, GroupFormat },
    { 0x2062, "(IT)",   NonPrinting, GroupFormat },
    { 0x2063, "(IS)",   NonPrinting, GroupFormat },
    { 0x2064, "(IP)",   NonPrinting, GroupFormat },
    { 0x2066, "LRI",    NonPrinting, GroupBidi   },
    { 0x2067, "RLI",    NonPrinting, GroupBidi   },
    { 0x2068, "FSI",    NonPrinting, GroupBidi   },
    { 0x2069, "PDI",    NonPrinting, GroupBidi   },
    { 0x206A, "ISS",    NonPrinting, GroupFormat },
    { 0x206B, "ASS",    NonPrinting, GroupFormat },
    { 0x206C, "IAFS",   NonPrinting, GroupFormat },
    { 0x206D, "AAFS",   NonPrinting, GroupFormat },
    { 0x206E, "NADS",   NonPrinting, GroupFormat },
    { 0x206F, "NODS",   NonPrinting, GroupFormat },
    { 0x3000, "IDSP",   NonPrinting, GroupSpace  },
    { 0xFEFF, "ZWNBSP", NonPrinting, GroupFormat },
    { 0xFFF9, "IAA",    NonPrinting, GroupAnnotation },
    { 0xFFFA, "IAS",    NonPrinting, GroupAnnotation },
    { 0xFFFB, "IAT",    NonPrinting, GroupAnnotation },

    // ── Table B: C0 (30). TAB/LF/CR omitted on purpose. ──────────────────
    { 0x0000, "NUL", ControlAndUnicodeEol, GroupC0 },
    { 0x0001, "SOH", ControlAndUnicodeEol, GroupC0 },
    { 0x0002, "STX", ControlAndUnicodeEol, GroupC0 },
    { 0x0003, "ETX", ControlAndUnicodeEol, GroupC0 },
    { 0x0004, "EOT", ControlAndUnicodeEol, GroupC0 },
    { 0x0005, "ENQ", ControlAndUnicodeEol, GroupC0 },
    { 0x0006, "ACK", ControlAndUnicodeEol, GroupC0 },
    { 0x0007, "BEL", ControlAndUnicodeEol, GroupC0 },
    { 0x0008, "BS",  ControlAndUnicodeEol, GroupC0 },
    { 0x000B, "VT",  ControlAndUnicodeEol, GroupC0 },
    { 0x000C, "FF",  ControlAndUnicodeEol, GroupC0 },
    { 0x000E, "SO",  ControlAndUnicodeEol, GroupC0 },
    { 0x000F, "SI",  ControlAndUnicodeEol, GroupC0 },
    { 0x0010, "DLE", ControlAndUnicodeEol, GroupC0 },
    { 0x0011, "DC1", ControlAndUnicodeEol, GroupC0 },
    { 0x0012, "DC2", ControlAndUnicodeEol, GroupC0 },
    { 0x0013, "DC3", ControlAndUnicodeEol, GroupC0 },
    { 0x0014, "DC4", ControlAndUnicodeEol, GroupC0 },
    { 0x0015, "NAK", ControlAndUnicodeEol, GroupC0 },
    { 0x0016, "SYN", ControlAndUnicodeEol, GroupC0 },
    { 0x0017, "ETB", ControlAndUnicodeEol, GroupC0 },
    { 0x0018, "CAN", ControlAndUnicodeEol, GroupC0 },
    { 0x0019, "EM",  ControlAndUnicodeEol, GroupC0 },
    { 0x001A, "SUB", ControlAndUnicodeEol, GroupC0 },
    { 0x001B, "ESC", ControlAndUnicodeEol, GroupC0 },
    { 0x001C, "FS",  ControlAndUnicodeEol, GroupC0 },
    { 0x001D, "GS",  ControlAndUnicodeEol, GroupC0 },
    { 0x001E, "RS",  ControlAndUnicodeEol, GroupC0 },
    { 0x001F, "US",  ControlAndUnicodeEol, GroupC0 },
    { 0x007F, "DEL", ControlAndUnicodeEol, GroupC0 },

    // ── Table B: C1 (31). U+0085 lives with the Unicode EOLs instead. ────
    { 0x0080, "PAD",  ControlAndUnicodeEol, GroupC1 },
    { 0x0081, "HOP",  ControlAndUnicodeEol, GroupC1 },
    { 0x0082, "BPH",  ControlAndUnicodeEol, GroupC1 },
    { 0x0083, "NBH",  ControlAndUnicodeEol, GroupC1 },
    { 0x0084, "IND",  ControlAndUnicodeEol, GroupC1 },
    { 0x0086, "SSA",  ControlAndUnicodeEol, GroupC1 },
    { 0x0087, "ESA",  ControlAndUnicodeEol, GroupC1 },
    { 0x0088, "HTS",  ControlAndUnicodeEol, GroupC1 },
    { 0x0089, "HTJ",  ControlAndUnicodeEol, GroupC1 },
    { 0x008A, "VTS",  ControlAndUnicodeEol, GroupC1 },
    { 0x008B, "PLD",  ControlAndUnicodeEol, GroupC1 },
    { 0x008C, "PLU",  ControlAndUnicodeEol, GroupC1 },
    { 0x008D, "RI",   ControlAndUnicodeEol, GroupC1 },
    { 0x008E, "SS2",  ControlAndUnicodeEol, GroupC1 },
    { 0x008F, "SS3",  ControlAndUnicodeEol, GroupC1 },
    { 0x0090, "DCS",  ControlAndUnicodeEol, GroupC1 },
    { 0x0091, "PU1",  ControlAndUnicodeEol, GroupC1 },
    { 0x0092, "PU2",  ControlAndUnicodeEol, GroupC1 },
    { 0x0093, "STS",  ControlAndUnicodeEol, GroupC1 },
    { 0x0094, "CCH",  ControlAndUnicodeEol, GroupC1 },
    { 0x0095, "MW",   ControlAndUnicodeEol, GroupC1 },
    { 0x0096, "SPA",  ControlAndUnicodeEol, GroupC1 },
    { 0x0097, "EPA",  ControlAndUnicodeEol, GroupC1 },
    { 0x0098, "SOS",  ControlAndUnicodeEol, GroupC1 },
    { 0x0099, "SGCI", ControlAndUnicodeEol, GroupC1 },
    { 0x009A, "SCI",  ControlAndUnicodeEol, GroupC1 },
    { 0x009B, "CSI",  ControlAndUnicodeEol, GroupC1 },
    { 0x009C, "ST",   ControlAndUnicodeEol, GroupC1 },
    { 0x009D, "OSC",  ControlAndUnicodeEol, GroupC1 },
    { 0x009E, "PM",   ControlAndUnicodeEol, GroupC1 },
    { 0x009F, "APC",  ControlAndUnicodeEol, GroupC1 },

    // ── Table B: Unicode EOL (3) ─────────────────────────────────────────
    { 0x0085, "NEL", ControlAndUnicodeEol, GroupUnicodeEol },
    { 0x2028, "LS",  ControlAndUnicodeEol, GroupUnicodeEol },
    { 0x2029, "PS",  ControlAndUnicodeEol, GroupUnicodeEol },
};

}  // namespace

const QVector<Entry> &table() {
    static const QVector<Entry> t = [] {
        QVector<Entry> v;
        v.reserve(int(sizeof(kEntries) / sizeof(kEntries[0])));
        for (const Entry &e : kEntries) v.append(e);
        return v;
    }();
    return t;
}

Categories allCategories() {
    return Categories(NonPrinting) | ControlAndUnicodeEol;
}

QByteArray utf8Of(char32_t cp) {
    // Encoded by hand rather than via QString. The obvious
    // QString::fromUcs4(&cp, 1).toUtf8() silently returns an EMPTY array for
    // U+FEFF, because fromUcs4 reads a leading U+FEFF as a byte-order mark and
    // consumes it. That produced an empty lookup key, and ZWNBSP — one of the
    // characters users most need to see — rendered as nothing at all.
    QByteArray out;
    const quint32 c = quint32(cp);
    if (c < 0x80) {
        out.append(char(c));
    } else if (c < 0x800) {
        out.append(char(0xC0 | (c >> 6)));
        out.append(char(0x80 | (c & 0x3F)));
    } else if (c < 0x10000) {
        out.append(char(0xE0 | (c >> 12)));
        out.append(char(0x80 | ((c >> 6) & 0x3F)));
        out.append(char(0x80 | (c & 0x3F)));
    } else {
        out.append(char(0xF0 | (c >> 18)));
        out.append(char(0x80 | ((c >> 12) & 0x3F)));
        out.append(char(0x80 | ((c >> 6) & 0x3F)));
        out.append(char(0x80 | (c & 0x3F)));
    }
    return out;
}

bool hasScintillaBuiltIn(char32_t cp) {
    return cp <= 0x1F || cp == 0x7F;
}

void forceRelayout(QsciScintilla *sci) {
    if (!sci) return;
    // Bounce the cache to a different level and back. SetLevel() deallocates
    // the cached LineLayouts only when the value actually changes, so bouncing
    // 0 -> 0 would be a no-op and the whole toggle would silently do nothing.
    const long cur = sci->SendScintilla(QsciScintillaBase::SCI_GETLAYOUTCACHE);
    const long other = (cur == 0) ? 1 : 0;
    sci->SendScintilla(QsciScintillaBase::SCI_SETLAYOUTCACHE, (unsigned long)other);
    sci->SendScintilla(QsciScintillaBase::SCI_SETLAYOUTCACHE, (unsigned long)cur);
}

void apply(QsciScintilla *sci, Categories cats) {
    if (!sci) return;

    for (const Entry &e : table()) {
        const QByteArray u8 = utf8Of(e.codepoint);

        // U+0000 cannot be keyed at all: every SCI_*REPRESENTATION message
        // takes a NUL-terminated C string, so the key for NUL is
        // indistinguishable from the empty string. Notepad++ has the same
        // limitation. Scintilla's built-in already draws "NUL", so leaving it
        // alone is both the only option and the right-looking one — the cost is
        // that NUL stays visible when the category is switched off.
        if (u8.isEmpty() || u8.at(0) == '\0') continue;

        if (cats.testFlag(e.category)) {
            sci->SendScintilla(QsciScintillaBase::SCI_SETREPRESENTATION,
                               u8.constData(), e.abbreviation);
        } else if (hasScintillaBuiltIn(e.codepoint)) {
            // Scintilla would draw its own mnemonic here, so "off" has to be an
            // explicit empty representation rather than no representation.
            // Without SCI_SETREPRESENTATIONAPPEARANCE we cannot ask for the
            // Plain style, so a ~3px blob border survives instead of nothing.
            //
            // That residue is the floor, not an oversight. Measured on
            // "a<U+0001>b", where the plain "ab" baseline is 21px:
            //   built-in "SOH" mnemonic ............ 61px
            //   empty representation (this) ........ 24px   <- closest to gone
            //   SCI_SETCONTROLCHARSYMBOL = space ... 38px
            // Clearing instead would restore the mnemonic, i.e. the menu item
            // would do nothing for C0. Don't "fix" this by reaching for
            // controlCharSymbol; it measures worse.
            sci->SendScintilla(QsciScintillaBase::SCI_SETREPRESENTATION,
                               u8.constData(), "");
        } else {
            // SCI_CLEARREPRESENTATION wants the string in wParam. Routing it
            // through the (uintptr_t, const char *) overload is not a style
            // preference: the tempting SendScintilla(msg, u8.constData()) puts
            // it in lParam with a null wParam and segfaults, and casting to
            // unsigned long would truncate the pointer on Windows.
            sci->SendScintilla(QsciScintillaBase::SCI_CLEARREPRESENTATION,
                               (uintptr_t)u8.constData(), (const char *)nullptr);
        }
    }

    forceRelayout(sci);
}

}  // namespace EditorSymbols
