// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef NOTEPATRA_NOTES_EXTRACT_APPLY_H
#define NOTEPATRA_NOTES_EXTRACT_APPLY_H

// Noter AI-Extract "owned marked region" apply layer (v0.1.112).
//
// Accepting the Extract dialog used to append ONLY summary + actions and
// every re-run stacked a fresh block at the bottom of the note. This
// module makes the applied extract a single OWNED REGION the panel can
// find again and replace in place:
//
//   * ALL reviewed sections persist (Summary, Action Items, Decisions,
//     Questions, Risks) plus a human-readable provenance caption
//     ("Extracted by <model> · <date> · <coverage>").
//   * The region is delimited by two INVISIBLE anchor markers carried as
//     QTextCharFormat anchors — toHtml() emits <a name="np-extract-…">,
//     the storage sanitizer keeps exactly that prefix-gated form, and
//     setHtml() re-attaches the name to the first character fragment of
//     the following text (empirically verified stable across 3+
//     toHtml→sanitize→setHtml cycles on Qt 5.15.13; anchored text renders
//     with NO link styling).
//   * A SHA-256 content signature is fused into the begin-anchor name
//     (np-extract-begin-<sig8>) so the panel can tell "untouched region —
//     replace in place" from "user edited inside — ask before replacing".
//     Checkbox toggles (☐↔✓) are normalized out of the signature, so
//     marking an action done is NOT an edit.
//
// QtGui-only (QTextDocument / QTextCursor) — headless-testable under
// QT_QPA_PLATFORM=offscreen with a QGuiApplication, no widgets.

#include "notes_sweep_prompt.h"

#include <QDateTime>
#include <QSet>
#include <QString>
#include <QStringList>
#include <QVector>

class QTextCursor;
class QTextDocument;

namespace NoterExtractApply {

// ── Heading helpers ──────────────────────────────────────────────────
// Relocated from notes.cpp's anonymous namespace so the region writer and
// NotesPanel::insertSubheader share one implementation. H1 biggest → H3
// smallest; body text is ~11pt.
qreal headingPointSize(int level);

// Writes `title` as a REAL heading block at `level` (headingLevel + bold
// char format — bold-only text evaporated on save+reload), then leaves
// `cur` at the start of a fresh PLAIN body block below it. When
// `anchorName` is non-empty the heading text additionally carries that
// anchor name (the invisible marker carrier — see module comment).
void insertHeadingBlock(QTextCursor &cur, const QString &title, int level,
                        const QString &anchorName = QString());

// ── Markers ──────────────────────────────────────────────────────────
// NEVER tr() anchor names — they are wire format, not display text.
QString beginAnchorName(const QString &sig8);   // "np-extract-begin-" + sig8
inline const QString kEndAnchorName = QStringLiteral("np-extract-end");

// True iff `name` matches ^np-extract-begin-([0-9a-f]{8})$ — anything
// else is not a region begin. sig8Out (optional) receives the hex sig.
bool isBeginAnchorName(const QString &name, QString *sig8Out = nullptr);

// ── Region content model ─────────────────────────────────────────────
struct ExtractLine {
    enum Kind { Heading, Body, Check } kind;
    QString text;   // Check lines carry the ITEM text only — writeRegion
                    // prepends the "☐ "/"✓ " marker at write time.
};

// Renders the reviewed result into the region's line list:
//   AI Extract (h2, begin-anchor host) → Summary → Action Items →
//   Decisions → Questions → Risks (all h2; empty sections omitted) →
//   provenance caption (Body, end-anchor host, always last).
// Coverage reads "full note", or "first ~N of M words" when
// 0 < wordsUsed < wordsTotal (the honest-truncation label).
QVector<ExtractLine> renderExtractLines(const NoterSweepPrompt::SweepResult &r,
                                        const QString &model,
                                        const QDateTime &when,
                                        int wordsUsed, int wordsTotal);

// ── Signature ────────────────────────────────────────────────────────
// Trim each line, drop blanks, collapse internal whitespace runs,
// normalize a leading "✓ " to "☐ " (checkbox toggles are NOT edits),
// join with '\n', SHA-256, first 8 hex chars. Used identically on the
// write side (rendered line texts) and the read-back side (block texts).
QString regionSig(const QStringList &lineTexts);

// ── Region scan ──────────────────────────────────────────────────────
struct Region {
    bool found = false;
    int  beginBlockPos = -1;   // position of the block hosting the begin anchor
    int  endBlockPos   = -1;   //   "    of the block hosting the end anchor
    int  endBlockLen   = 0;    // that block's length()
    QString storedSig;         // sig8 parsed out of the begin anchor name
    QStringList innerTexts;    // text() of every block begin..end inclusive
};

// Scans every block's fragments for charFormat().anchorNames(). Works for
// freshly written regions (name on the whole text fragment) AND reloaded
// ones (name re-attached to the first character only — the Qt quirk).
// Picks the LAST begin-anchor that has an end-anchor after it; the region
// runs from that begin's block through the FIRST end-anchor block after
// it. begin-without-end, end-before-begin, or no anchors → found=false.
Region findExtractRegion(const QTextDocument *doc);

// ── Done-state carry ─────────────────────────────────────────────────
// Key for matching action lines across runs: strip a leading "☐ "/"✓ ",
// strip a trailing "(due …)" (a changed due time must not lose
// done-state), then NoterSweepPrompt::normalizeForMatch (drops @owner,
// punctuation, case).
QString actionKey(const QString &lineOrItemText);

// { actionKey(t) : t in innerTexts if t starts with "✓ " }
QSet<QString> collectDoneKeys(const QStringList &innerTexts);

// ── Writer ───────────────────────────────────────────────────────────
// Writes the region's blocks at `cur` (which the caller has positioned on
// a fresh/empty block), inside the CALLER's beginEditBlock. Check lines
// render "☐ " but flip to "✓ " when doneKeys contains actionKey(line).
// The FIRST line (the "AI Extract" heading) carries beginAnchorName(sig8);
// the LAST line (the caption) carries kEndAnchorName. After the caption
// the cursor's char/blockCharFormat is reset to a plain QTextCharFormat
// so later typing never inherits the anchor.
void writeRegion(QTextCursor &cur, const QVector<ExtractLine> &lines,
                 const QString &sig8, const QSet<QString> &doneKeys);

}  // namespace NoterExtractApply

#endif
