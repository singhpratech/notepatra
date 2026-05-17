// SPDX-License-Identifier: GPL-3.0-or-later

#include "lexer_plaintext.h"

#include <Qsci/qsciscintilla.h>
#include <QByteArray>
#include <QRegularExpression>
#include <QRegularExpressionMatch>
#include <QRegularExpressionMatchIterator>
#include <QString>
#include <algorithm>
#include <vector>

// v0.1.84 — Custom plain-text lexer. See header for design rationale.
//
// IMPLEMENTATION NOTES
// ────────────────────
// Scintilla works in BYTES, not Qt UTF-16 code units. startStyling() /
// setStyling() count bytes. To avoid Scintilla-vs-Qt buffer-length
// mismatches at init time (which previously fired a CellBuffer assertion
// when editor->text() returned "" but Scintilla thought the buffer had
// bytes), we fetch the styled range directly via QsciScintilla::bytes(
// start, end). That gives us EXACTLY (end-start) bytes from Scintilla's
// own view of the buffer — the single source of truth for setStyling.
//
// Regex matching is done on the UTF-8 byte slice cast to QString via
// fromUtf8(). All our patterns are ASCII-anchored (URL, email, number,
// quote, backtick, ALL-CAPS heading) so multi-byte UTF-8 content is
// harmless — we don't tokenize through it, just match around it.
//
// For anchor-based patterns like ALL-CAPS headings (`(?:^|\\n)` etc.),
// we limit anchoring to the SLICE — re-styling a mid-document chunk
// won't see global line anchors, but headings still fire when the
// whole document re-styles (the common case for .txt files).

LexerPlainText::LexerPlainText(QObject *parent) : QsciLexerCustom(parent) {}

QString LexerPlainText::description(int style) const {
    switch (style) {
        case 0: return QStringLiteral("Default");
        case 1: return QStringLiteral("URL");
        case 2: return QStringLiteral("Email");
        case 3: return QStringLiteral("Number");
        case 4: return QStringLiteral("Heading");
        case 5: return QStringLiteral("String");
        case 6: return QStringLiteral("String alt");
        case 7: return QStringLiteral("Code");
        default: return QString();
    }
}

namespace {

struct Match {
    int byteStart;   // byte offset within slice
    int byteLen;     // byte length within slice
    int style;
};

const QRegularExpression &reUrlScheme() {
    static const QRegularExpression r(
        QStringLiteral("https?://[^\\s<>\"')\\]]+"));
    return r;
}
const QRegularExpression &reUrlWww() {
    static const QRegularExpression r(
        QStringLiteral("\\bwww\\.[^\\s<>\"')\\]]+"));
    return r;
}
const QRegularExpression &reEmail() {
    static const QRegularExpression r(
        QStringLiteral("[A-Za-z0-9._%+-]+@[A-Za-z0-9.-]+\\.[A-Za-z]{2,}"));
    return r;
}
const QRegularExpression &reCurrency() {
    static const QRegularExpression r(
        QStringLiteral("\\$\\d+(\\.\\d+)?"));
    return r;
}
const QRegularExpression &reNumber() {
    static const QRegularExpression r(
        QStringLiteral("\\b\\d+(\\.\\d+)?\\b"));
    return r;
}
const QRegularExpression &reHeading() {
    // 3+ ALL-CAPS words on their own line. Anchored to slice newlines or edges.
    static const QRegularExpression r(
        QStringLiteral("(?:^|(?<=\\n))[A-Z][A-Z0-9]+(?:[ \\t]+[A-Z][A-Z0-9]+){2,}(?=\\n|$)"));
    return r;
}
const QRegularExpression &reDoubleStr() {
    static const QRegularExpression r(
        QStringLiteral("\"[^\"\\n]{1,80}\""));
    return r;
}
const QRegularExpression &reSingleStr() {
    static const QRegularExpression r(
        QStringLiteral("'[^'\\n]{1,80}'"));
    return r;
}
const QRegularExpression &reBacktick() {
    static const QRegularExpression r(
        QStringLiteral("`[^`\\n]{1,80}`"));
    return r;
}

// Convert QString (UTF-16) char offset to UTF-8 byte offset within `s`.
int charToByte(const QString &s, int charPos) {
    if (charPos <= 0) return 0;
    if (charPos >= s.size()) return s.toUtf8().size();
    return s.left(charPos).toUtf8().size();
}

// Run regex over `slice` (a QString built from the UTF-8 byte slice),
// emit Match{byteStart, byteLen, style} for each hit.
void collect(const QString &slice, const QRegularExpression &re, int style,
             std::vector<Match> &out) {
    QRegularExpressionMatchIterator it = re.globalMatch(slice);
    while (it.hasNext()) {
        QRegularExpressionMatch m = it.next();
        const int cs = m.capturedStart();
        const int cl = m.capturedLength();
        if (cl <= 0) continue;
        const int bs = charToByte(slice, cs);
        const int be = charToByte(slice, cs + cl);
        out.push_back({bs, be - bs, style});
    }
}

}  // namespace

void LexerPlainText::styleText(int start, int end) {
    QsciScintilla *editor = qobject_cast<QsciScintilla *>(parent());
    if (!editor) return;
    if (end <= start) return;
    const int wantBytes = end - start;  // EXACTLY this many setStyling-bytes — never more

    // ── Single source of truth: Scintilla's own byte buffer for [start, end). ──
    // bytes() returns exactly the UTF-8 bytes Scintilla holds in that range.
    // .size() tells us how many bytes are ACTUALLY there — never out of sync.
    const QByteArray slice = editor->bytes(start, end);
    const int sliceLen = slice.size();
    if (sliceLen <= 0) return;  // nothing to paint; Scintilla buffer was empty here

    // Convert to QString for regex; multi-byte UTF-8 chars become single QChars,
    // but we only restyle on offsets we map back via charToByte() so spans align.
    const QString sliceQs = QString::fromUtf8(slice);

    std::vector<Match> matches;
    matches.reserve(32);
    collect(sliceQs, reUrlScheme(), 1, matches);
    collect(sliceQs, reUrlWww(),    1, matches);
    collect(sliceQs, reEmail(),     2, matches);
    collect(sliceQs, reCurrency(),  3, matches);
    collect(sliceQs, reNumber(),    3, matches);
    collect(sliceQs, reHeading(),   4, matches);
    collect(sliceQs, reDoubleStr(), 5, matches);
    collect(sliceQs, reSingleStr(), 6, matches);
    collect(sliceQs, reBacktick(),  7, matches);

    // Sort by start, drop overlaps (first-painter-wins by regex order).
    std::sort(matches.begin(), matches.end(),
              [](const Match &a, const Match &b) {
                  if (a.byteStart != b.byteStart) return a.byteStart < b.byteStart;
                  return a.style < b.style;
              });
    std::vector<Match> kept;
    kept.reserve(matches.size());
    int blocker = 0;
    for (const Match &m : matches) {
        if (m.byteStart < blocker) continue;
        // Clamp every match to the slice — defence-in-depth against any
        // pathological match length crossing the styled range.
        if (m.byteStart >= sliceLen) continue;
        int safeLen = m.byteLen;
        if (m.byteStart + safeLen > sliceLen) safeLen = sliceLen - m.byteStart;
        if (safeLen <= 0) continue;
        kept.push_back({m.byteStart, safeLen, m.style});
        blocker = m.byteStart + safeLen;
    }

    // Paint. CRITICAL invariant: cumulative bytes setStyling'd MUST equal
    // EXACTLY wantBytes (end - start). One byte more crashes Scintilla at
    // CellBuffer.cpp:635 (the v0.1.84 first-attempt bug). One byte less is
    // harmless (Scintilla keeps the trailing tail as previous style).
    //
    // Safety strategy: every setStyling() call is guarded by `wouldOverflow`
    // — if we'd exceed wantBytes, we either clamp the length or skip.
    startStyling(start);
    int painted = 0;  // cumulative bytes setStyling-d so far
    auto paint = [&](int n, int style) {
        if (n <= 0) return;
        if (painted + n > wantBytes) n = wantBytes - painted;
        if (n <= 0) return;
        setStyling(n, style);
        painted += n;
    };
    int cursor = 0;  // byte offset within slice
    for (const Match &m : kept) {
        if (painted >= wantBytes) break;
        if (m.byteStart > cursor) {
            paint(m.byteStart - cursor, 0);
            cursor = m.byteStart;
        }
        paint(m.byteLen, m.style);
        cursor += m.byteLen;
    }
    if (painted < wantBytes) {
        paint(wantBytes - painted, 0);
    }
}
