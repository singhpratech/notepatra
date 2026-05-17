// SPDX-License-Identifier: GPL-3.0-or-later

#include "lexer_csv.h"

#include <Qsci/qsciscintilla.h>
#include <Qsci/qsciscintillabase.h>

#include <QByteArray>

// ─── style ids ──────────────────────────────────────────────────────────
// Mirror the numbering documented in the header so npp_palette.cpp can
// match them via description() strings.
namespace {
constexpr int CSV_DEFAULT   = 0;
constexpr int CSV_HEADER    = 1;
constexpr int CSV_COLUMN_A  = 2;
constexpr int CSV_COLUMN_B  = 3;
constexpr int CSV_SEPARATOR = 4;
constexpr int CSV_QUOTED    = 5;
constexpr int CSV_NUMBER    = 6;
constexpr int CSV_COMMENT   = 7;

// Returns true if the byte slice [data + lo, data + hi) is a pure decimal
// number, optionally negative, with at most one '.'. Leading/trailing
// whitespace tolerated so cells like "  42  " also count as numeric.
// Empty cells are NOT numeric.
bool cellIsNumeric(const char *data, int lo, int hi) {
    // Trim leading whitespace.
    while (lo < hi && (data[lo] == ' ' || data[lo] == '\t')) ++lo;
    // Trim trailing whitespace.
    while (hi > lo && (data[hi - 1] == ' ' || data[hi - 1] == '\t')) --hi;
    if (lo >= hi) return false;
    int i = lo;
    if (data[i] == '-' || data[i] == '+') {
        ++i;
        if (i >= hi) return false;  // a lone "-" is not a number
    }
    bool sawDigit = false;
    bool sawDot = false;
    for (; i < hi; ++i) {
        const char c = data[i];
        if (c >= '0' && c <= '9') { sawDigit = true; continue; }
        if (c == '.' && !sawDot) { sawDot = true; continue; }
        return false;
    }
    return sawDigit;
}
}  // namespace

LexerCsv::LexerCsv(QObject *parent) : QsciLexerCustom(parent) {}

QString LexerCsv::description(int style) const {
    // Names are load-bearing — npp_palette.cpp's matcher chain looks for
    // substrings like "comment", "number", "separator" to apply themed
    // colours. Keep them human-readable and stable.
    switch (style) {
        case CSV_DEFAULT:   return QStringLiteral("Default");
        case CSV_HEADER:    return QStringLiteral("Header");
        case CSV_COLUMN_A:  return QStringLiteral("Column A");
        case CSV_COLUMN_B:  return QStringLiteral("Column B");
        case CSV_SEPARATOR: return QStringLiteral("Separator");
        case CSV_QUOTED:    return QStringLiteral("Quoted");
        case CSV_NUMBER:    return QStringLiteral("Number");
        case CSV_COMMENT:   return QStringLiteral("Comment");
        default:            return QString();
    }
}

void LexerCsv::styleText(int start, int end) {
    QsciScintilla *e = editor();
    if (!e) return;
    if (end <= start) return;
    const int wantBytes = end - start;  // exact byte budget; never exceed

    // Scintilla operates on UTF-8 bytes. The chars we discriminate on
    // (',', '\t', '"', '#', '\n', '\r', digits, '.', '-', '+', space)
    // are all ASCII, so single-byte scanning is correct even when cell
    // contents contain multi-byte UTF-8 — those bytes just fall through
    // to the "default column colour" branch.
    //
    // QsciScintilla::bytes(start, end) hands us the raw UTF-8 slice the
    // document holds, which is exactly the addressing space the
    // startStyling/setStyling API speaks.
    const QByteArray buf = e->bytes(start, end);
    const int len = buf.size();
    const char *data = buf.constData();

    // Determine whether `start` is at the very first line of the document.
    // QsciLexerCustom guarantees `start` is at a line boundary, so we can
    // ask Scintilla which line that is and treat line 0 as header.
    const int startLine = e->SendScintilla(
        QsciScintillaBase::SCI_LINEFROMPOSITION, static_cast<unsigned long>(start));

    startStyling(start);

    const char sep = m_separator.toLatin1();  // ',' or '\t' — both ASCII

    int i = 0;                  // byte offset within buf
    int line = startLine;       // current absolute line number
    int colIndex = 0;           // current column within line
    int cellStart = 0;          // byte offset where current cell began
    bool lineIsComment = false; // whole-line styling
    bool firstNonWsSeen = false; // for comment-line detection

    // ── HARD CAP: cumulative bytes setStyling-d must NEVER exceed wantBytes.
    // Going one byte over crashes Scintilla at CellBuffer.cpp:635 — the
    // v0.1.84 first-cut bug. All setStyling calls go through paint() which
    // clamps to the remaining budget.
    int painted = 0;
    auto paint = [&](int n, int style) {
        if (n <= 0) return;
        if (painted + n > wantBytes) n = wantBytes - painted;
        if (n <= 0) return;
        setStyling(n, style);
        painted += n;
    };

    // Helper closure — flush [cellStart, cellEnd) as the appropriate cell
    // style. Quoted cells are pre-painted while scanning; this only fires
    // for plain (unquoted) cells.
    auto flushCell = [&](int cellEnd) {
        if (cellEnd <= cellStart) return;
        int style;
        if (line == 0) {
            style = CSV_HEADER;
        } else if (cellIsNumeric(data, cellStart, cellEnd)) {
            style = CSV_NUMBER;
        } else {
            style = (colIndex % 2 == 0) ? CSV_COLUMN_A : CSV_COLUMN_B;
        }
        paint(cellEnd - cellStart, style);
    };

    while (i < len) {
        const char c = data[i];

        // ─── start-of-line bookkeeping ────────────────────────────────
        if (!firstNonWsSeen) {
            // Skip leading spaces/tabs (but only if the separator isn't
            // tab — otherwise leading-tab is a real empty cell).
            if (c == ' ' || (c == '\t' && sep != '\t')) {
                // Treat leading whitespace as part of the cell so we
                // don't break alignment of cellStart.
                firstNonWsSeen = true;
            } else if (c == '#') {
                lineIsComment = true;
                firstNonWsSeen = true;
            } else if (c != '\n' && c != '\r') {
                firstNonWsSeen = true;
            }
        }

        // ─── comment-line shortcut ────────────────────────────────────
        if (lineIsComment) {
            // Eat up to and including the line terminator as one comment
            // run. Handle CRLF as a single newline sequence.
            int j = i;
            while (j < len && data[j] != '\n' && data[j] != '\r') ++j;
            paint(j - i, CSV_COMMENT);
            // Style the EOL bytes themselves as default so subsequent
            // lines compute fresh.
            int k = j;
            if (k < len && data[k] == '\r') ++k;
            if (k < len && data[k] == '\n') ++k;
            if (k > j) paint(k - j, CSV_DEFAULT);
            i = k;
            ++line;
            colIndex = 0;
            cellStart = i;
            lineIsComment = false;
            firstNonWsSeen = false;
            continue;
        }

        // ─── quoted field ─────────────────────────────────────────────
        if (c == '"') {
            // Flush any plain text accumulated before the quote (rare —
            // quoted cells usually start at cellStart, but a CSV with
            // mixed `abc"def"` is still tolerated).
            if (i > cellStart) {
                flushCell(i);
            }
            // Scan to matching closing quote, honouring "" as an escaped
            // quote inside the field.
            int j = i + 1;
            while (j < len) {
                if (data[j] == '"') {
                    if (j + 1 < len && data[j + 1] == '"') {
                        j += 2;  // escaped quote, keep scanning
                        continue;
                    }
                    ++j;        // include closing quote
                    break;
                }
                ++j;
            }
            paint(j - i, CSV_QUOTED);
            i = j;
            cellStart = i;  // anything after the closing quote up to the
                            // next separator is treated as a trailing
                            // remainder of the same cell.
            continue;
        }

        // ─── separator ────────────────────────────────────────────────
        if (c == sep) {
            flushCell(i);
            paint(1, CSV_SEPARATOR);
            ++i;
            ++colIndex;
            cellStart = i;
            continue;
        }

        // ─── newline ──────────────────────────────────────────────────
        if (c == '\n' || c == '\r') {
            flushCell(i);
            // Consume CRLF / LF / CR as a single EOL.
            int j = i;
            if (j < len && data[j] == '\r') ++j;
            if (j < len && data[j] == '\n') ++j;
            paint(j - i, CSV_DEFAULT);
            i = j;
            ++line;
            colIndex = 0;
            cellStart = i;
            firstNonWsSeen = false;
            continue;
        }

        // ─── ordinary cell byte ───────────────────────────────────────
        ++i;
    }

    // Trailing cell with no terminating newline.
    if (cellStart < len) {
        flushCell(len);
    }

    // Defensive tail-fill: if we under-painted (which is safe but leaves the
    // tail at previous style), top up with default so the styled region
    // matches wantBytes exactly.
    if (painted < wantBytes) {
        paint(wantBytes - painted, CSV_DEFAULT);
    }
}
