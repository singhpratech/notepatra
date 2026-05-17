// SPDX-License-Identifier: GPL-3.0-or-later

// Merge-marker scanner. See merge_helper.h for the full contract.
//
// The scanner is deliberately string-only (no Qt regex). Marker lines
// MUST start at column 0 — anything indented is treated as ordinary
// content. That matches git's own behaviour: `git merge --no-edit`
// always emits markers at column 0, and any user typing "<<<<<<<" as
// part of a string literal or comment will (almost always) indent it.

#include "merge_helper.h"

namespace MergeHelper {

namespace {

// Returns true if `line` starts with `marker` followed by either end-of-
// line or whitespace. We require at least 7 marker chars at column 0
// (git always emits exactly 7) AND that the *next* character either is
// absent (end of line) or is a space / tab / newline. That keeps us
// from matching "<<<<<<<<" inside a comment that happens to lead a line.
bool isMarkerLine(const QString &line, QChar marker) {
    // Must have at least 7 occurrences at column 0.
    if (line.size() < 7) return false;
    for (int i = 0; i < 7; ++i) {
        if (line[i] != marker) return false;
    }
    // Tolerate 8+ markers (matches some merge drivers) but the char just
    // after the marker run must be EOL or whitespace, never the same
    // marker char being part of a longer run that's actually code.
    int runEnd = 7;
    while (runEnd < line.size() && line[runEnd] == marker) ++runEnd;
    if (runEnd >= line.size()) return true;  // line is just markers
    const QChar after = line[runEnd];
    return after.isSpace() || after == '\t';
}

// Extract the "label" (branch name / ref) after a marker run. E.g.
// "<<<<<<< HEAD" → "HEAD". Returns trimmed remainder, or empty.
QString markerLabel(const QString &line, QChar marker) {
    int i = 0;
    while (i < line.size() && line[i] == marker) ++i;
    return line.mid(i).trimmed();
}

} // namespace

QVector<ConflictRegion> scanConflicts(const QString &buffer) {
    QVector<ConflictRegion> out;

    // QString::split with KeepEmptyParts preserves the original line
    // count and makes line indices match Scintilla's line numbering.
    const QStringList lines = buffer.split('\n', Qt::KeepEmptyParts);

    int i = 0;
    while (i < lines.size()) {
        if (!isMarkerLine(lines[i], QLatin1Char('<'))) {
            ++i;
            continue;
        }

        const int openLine = i;
        // Walk forward looking for the matching "=======" then ">>>>>>>",
        // tolerating only column-0 markers. If we hit another "<<<<<<<"
        // before finding "=======" we treat the inner one as text (per
        // the contract — nested markers inside ours block are content).
        int sepLine = -1;
        int closeLine = -1;
        for (int j = openLine + 1; j < lines.size(); ++j) {
            if (sepLine < 0 && isMarkerLine(lines[j], QLatin1Char('='))) {
                sepLine = j;
            } else if (sepLine >= 0 && isMarkerLine(lines[j], QLatin1Char('>'))) {
                closeLine = j;
                break;
            }
            // Any other lines (including stray "<<<<<<<" at column 0
            // that we'd nominally treat as a new opener) are skipped:
            // by spec, only the FIRST close marker after a separator
            // closes the region. We don't restart on a second "<<<<<<<"
            // because that would corrupt the inner ours text. The user
            // can re-resolve afterwards if there's still a marker left.
        }

        if (sepLine < 0 || closeLine < 0) {
            // Malformed — skip this opener and continue past it.
            ++i;
            continue;
        }

        ConflictRegion region;
        region.startLine = openLine;
        region.separatorLine = sepLine;
        region.endLine = closeLine;
        region.oursLabel = markerLabel(lines[openLine], QLatin1Char('<'));
        region.theirsLabel = markerLabel(lines[closeLine], QLatin1Char('>'));

        QStringList oursLines;
        for (int k = openLine + 1; k < sepLine; ++k) oursLines << lines[k];
        QStringList theirsLines;
        for (int k = sepLine + 1; k < closeLine; ++k) theirsLines << lines[k];

        region.ours = oursLines.join('\n');
        region.theirs = theirsLines.join('\n');

        out.append(region);
        i = closeLine + 1;
    }

    return out;
}

QString applyResolution(const QString &buffer,
                        const QVector<ConflictRegion> &regions,
                        int regionIndex,
                        const QString &replacement) {
    if (regionIndex < 0 || regionIndex >= regions.size()) return buffer;

    const ConflictRegion &r = regions[regionIndex];
    const QStringList lines = buffer.split('\n', Qt::KeepEmptyParts);

    if (r.startLine < 0 || r.endLine >= lines.size()) return buffer;

    QStringList out;
    out.reserve(lines.size());
    for (int i = 0; i < r.startLine; ++i) out << lines[i];

    // Replacement may be multi-line. Split it so the line count of the
    // resulting buffer reflects each line of replacement as its own line.
    if (!replacement.isEmpty()) {
        const QStringList replLines = replacement.split('\n', Qt::KeepEmptyParts);
        for (const QString &rl : replLines) out << rl;
    }

    for (int i = r.endLine + 1; i < lines.size(); ++i) out << lines[i];

    return out.join('\n');
}

} // namespace MergeHelper
