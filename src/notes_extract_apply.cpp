// SPDX-License-Identifier: GPL-3.0-or-later

#include "notes_extract_apply.h"

#include <QCoreApplication>
#include <QCryptographicHash>
#include <QRegularExpression>
#include <QTextBlock>
#include <QTextCursor>
#include <QTextDocument>
#include <QTextFragment>

namespace NoterExtractApply {

namespace {
// tr() shim — namespace functions have no QObject context. The context
// string keeps these extractable by lupdate alongside the panel's.
QString trx(const char *src) {
    return QCoreApplication::translate("NoterExtractApply", src);
}
}  // namespace

// ═══════════════════════════════════════════════════════════════════════
// Heading helpers — relocated VERBATIM from notes.cpp's anonymous
// namespace (the v0.1.112 heading-persistence wave), plus the optional
// anchor carrier on the heading text.
// ═══════════════════════════════════════════════════════════════════════

// H1 biggest → H3 smallest; body text is ~11pt. Matches the in-editor look
// the toolbar presets have always had.
qreal headingPointSize(int level) {
    return (level <= 1) ? 18.0 : (level == 2 ? 15.0 : 13.0);
}

// Writes `title` as a heading block at `level`, then leaves `cur` at the
// start of a fresh PLAIN body block below it (headingLevel cleared, normal
// weight) so the following text never inherits the heading look.
void insertHeadingBlock(QTextCursor &cur, const QString &title, int level,
                        const QString &anchorName) {
    QTextBlockFormat hb = cur.blockFormat();
    hb.setHeadingLevel(level);
    cur.setBlockFormat(hb);
    QTextCharFormat hf;
    hf.setFontWeight(QFont::Bold);
    hf.setFontPointSize(headingPointSize(level));
    cur.setBlockCharFormat(hf);
    if (!anchorName.isEmpty()) {
        // The invisible region marker: toHtml() emits <a name="…"></a>
        // before the heading text; anchored text renders with NO link
        // styling (verified on Qt 5.15.13 — see notes_extract_apply.h).
        QTextCharFormat hfa = hf;
        hfa.setAnchor(true);
        hfa.setAnchorNames({ anchorName });
        cur.insertText(title, hfa);
    } else {
        cur.insertText(title, hf);
    }

    cur.insertBlock();
    QTextBlockFormat bb = cur.blockFormat();
    bb.setHeadingLevel(0);
    cur.setBlockFormat(bb);
    QTextCharFormat plain;
    plain.setFontWeight(QFont::Normal);
    plain.setFontItalic(false);
    cur.setBlockCharFormat(plain);
    cur.setCharFormat(plain);
}

// ═══════════════════════════════════════════════════════════════════════
// Markers
// ═══════════════════════════════════════════════════════════════════════

QString beginAnchorName(const QString &sig8) {
    return QStringLiteral("np-extract-begin-") + sig8;
}

bool isBeginAnchorName(const QString &name, QString *sig8Out) {
    static const QRegularExpression re(
        QStringLiteral("^np-extract-begin-([0-9a-f]{8})$"));
    const QRegularExpressionMatch m = re.match(name);
    if (!m.hasMatch()) return false;
    if (sig8Out) *sig8Out = m.captured(1);
    return true;
}

// ═══════════════════════════════════════════════════════════════════════
// renderExtractLines — the region's content, in document order.
// ═══════════════════════════════════════════════════════════════════════
//
// Heading literals are byte-identical to the pre-region writer ("Summary",
// "Action Items") so the existing widget assertions stay green. All
// headings are level 2. The action-line formatting (two spaces before
// @owner, two before "(due …)") is copied EXACTLY from the original
// notes.cpp writer — the checkbox click toggle, Enter continuation and
// strike-restyle all key on that literal shape.

QVector<ExtractLine> renderExtractLines(const NoterSweepPrompt::SweepResult &r,
                                        const QString &model,
                                        const QDateTime &when,
                                        int wordsUsed, int wordsTotal) {
    QVector<ExtractLine> out;
    out.append({ ExtractLine::Heading, trx("AI Extract") });

    if (!r.summary.isEmpty()) {
        out.append({ ExtractLine::Heading, trx("Summary") });
        out.append({ ExtractLine::Body, r.summary });
        out.append({ ExtractLine::Body, QString() });
    }

    if (!r.actions.isEmpty()) {
        out.append({ ExtractLine::Heading, trx("Action Items") });
        for (const auto &item : r.actions) {
            QString line = item.text;
            if (!item.owner.isEmpty()) line += QStringLiteral("  ") + item.owner;
            if (item.dueAt.isValid()) {
                line += QStringLiteral("  (") +
                        trx("due %1").arg(item.dueAt.toString(
                            QStringLiteral("MMM d HH:mm"))) +
                        QStringLiteral(")");
            }
            out.append({ ExtractLine::Check, line });
        }
        out.append({ ExtractLine::Body, QString() });
    }

    // Decisions / Questions / Risks — "• " body bullets (U+2022 text
    // glyph, same as the sweep dialog's list rendering; never an emoji).
    const auto bulletSection = [&out](const QString &heading,
                                      const QVector<NoterSweepPrompt::SweepResult::Item> &items) {
        if (items.isEmpty()) return;
        out.append({ ExtractLine::Heading, heading });
        for (const auto &item : items)
            out.append({ ExtractLine::Body, QStringLiteral("• ") + item.text });
        out.append({ ExtractLine::Body, QString() });
    };
    bulletSection(trx("Decisions"), r.decisions);
    bulletSection(trx("Questions"), r.questions);
    bulletSection(trx("Risks"),     r.risks);

    // Provenance caption — always last; hosts the end anchor. Coverage is
    // the honest-truncation label.
    const QString coverage =
        (wordsUsed > 0 && wordsTotal > wordsUsed)
            ? trx("first ~%L1 of %L2 words").arg(wordsUsed).arg(wordsTotal)
            : trx("full note");
    out.append({ ExtractLine::Body,
                 trx("Extracted by %1 · %2 · %3")
                     .arg(model,
                          when.toString(QStringLiteral("yyyy-MM-dd HH:mm")),
                          coverage) });
    return out;
}

// ═══════════════════════════════════════════════════════════════════════
// regionSig — content signature with toggle/whitespace invariance.
// ═══════════════════════════════════════════════════════════════════════

QString regionSig(const QStringList &lineTexts) {
    static const QRegularExpression wsRun(QStringLiteral("\\s+"));
    QStringList norm;
    norm.reserve(lineTexts.size());
    for (const QString &raw : lineTexts) {
        QString t = raw.trimmed();
        if (t.isEmpty()) continue;          // blank spacer lines are layout
        // ✓→☐: marking an action done is a state change, not an edit.
        if (t.startsWith(QStringLiteral("✓ ")))
            t = QStringLiteral("☐ ") + t.mid(2);
        // Collapse internal whitespace runs — HTML round-trips must not
        // count as edits (the on-disk artifact is whitespace-fragile).
        t.replace(wsRun, QStringLiteral(" "));
        norm << t;
    }
    const QByteArray digest = QCryptographicHash::hash(
        norm.join(QLatin1Char('\n')).toUtf8(), QCryptographicHash::Sha256);
    return QString::fromLatin1(digest.toHex().left(8));
}

// ═══════════════════════════════════════════════════════════════════════
// findExtractRegion — fragment-level anchor scan.
// ═══════════════════════════════════════════════════════════════════════

Region findExtractRegion(const QTextDocument *doc) {
    Region out;
    if (!doc) return out;

    struct AnchorHit {
        int blockPos;
        int blockLen;
        QString name;
    };
    QVector<AnchorHit> hits;

    for (QTextBlock b = doc->begin(); b.isValid(); b = b.next()) {
        for (QTextBlock::iterator it = b.begin(); !it.atEnd(); ++it) {
            const QTextFragment f = it.fragment();
            if (!f.isValid()) continue;
            const QTextCharFormat cf = f.charFormat();
            if (!cf.isAnchor()) continue;
            const QStringList names = cf.anchorNames();
            for (const QString &n : names) {
                // One hit per (block, name): formats can split the
                // anchored text into several fragments.
                if (!hits.isEmpty() && hits.last().blockPos == b.position() &&
                    hits.last().name == n)
                    continue;
                hits.append({ b.position(), b.length(), n });
            }
        }
    }

    // Pick the LAST begin-anchor that has an end-anchor strictly after it.
    // Earlier pairs (e.g. after a Keep-both) become ordinary user content.
    int beginPos = -1;
    int endPos = -1, endLen = 0;
    QString sig;
    for (const AnchorHit &h : hits) {
        QString s;
        if (!isBeginAnchorName(h.name, &s)) continue;
        for (const AnchorHit &e : hits) {
            if (e.name != kEndAnchorName) continue;
            if (e.blockPos <= h.blockPos) continue;
            // First end after this begin.
            if (h.blockPos > beginPos) {
                beginPos = h.blockPos;
                endPos   = e.blockPos;
                endLen   = e.blockLen;
                sig      = s;
            }
            break;
        }
    }
    if (beginPos < 0) return out;   // no begin-with-end pair → not found

    out.found = true;
    out.beginBlockPos = beginPos;
    out.endBlockPos   = endPos;
    out.endBlockLen   = endLen;
    out.storedSig     = sig;
    for (QTextBlock b = doc->findBlock(beginPos);
         b.isValid() && b.position() <= endPos; b = b.next())
        out.innerTexts << b.text();
    return out;
}

// ═══════════════════════════════════════════════════════════════════════
// Done-state carry
// ═══════════════════════════════════════════════════════════════════════

QString actionKey(const QString &lineOrItemText) {
    QString t = lineOrItemText;
    if (t.startsWith(QStringLiteral("☐ ")) ||
        t.startsWith(QStringLiteral("✓ ")))
        t = t.mid(2);
    // A changed due time must not lose done-state — strip the suffix.
    static const QRegularExpression dueSuffix(
        QStringLiteral("\\s*\\(due [^)]*\\)\\s*$"));
    t.remove(dueSuffix);
    return NoterSweepPrompt::normalizeForMatch(t);
}

QSet<QString> collectDoneKeys(const QStringList &innerTexts) {
    QSet<QString> keys;
    for (const QString &t : innerTexts) {
        if (!t.startsWith(QStringLiteral("✓ "))) continue;
        const QString k = actionKey(t);
        if (!k.isEmpty()) keys.insert(k);
    }
    return keys;
}

// ═══════════════════════════════════════════════════════════════════════
// writeRegion
// ═══════════════════════════════════════════════════════════════════════

void writeRegion(QTextCursor &cur, const QVector<ExtractLine> &lines,
                 const QString &sig8, const QSet<QString> &doneKeys) {
    if (lines.isEmpty()) return;

    for (int i = 0; i < lines.size(); ++i) {
        const ExtractLine &ln = lines.at(i);
        const bool first = (i == 0);
        const bool last  = (i == lines.size() - 1);
        if (!first) cur.insertBlock();

        QTextBlockFormat bf = cur.blockFormat();
        QTextCharFormat cf;
        if (ln.kind == ExtractLine::Heading) {
            bf.setHeadingLevel(2);
            cf.setFontWeight(QFont::Bold);
            cf.setFontPointSize(headingPointSize(2));
        } else {
            bf.setHeadingLevel(0);
            cf.setFontWeight(QFont::Normal);
            cf.setFontItalic(false);
        }
        cur.setBlockFormat(bf);
        cur.setBlockCharFormat(cf);

        QString text = ln.text;
        if (ln.kind == ExtractLine::Check) {
            const bool done = doneKeys.contains(actionKey(ln.text));
            text = (done ? QStringLiteral("✓ ") : QStringLiteral("☐ ")) + text;
        }

        QTextCharFormat tf = cf;
        if (first) {
            tf.setAnchor(true);
            tf.setAnchorNames({ beginAnchorName(sig8) });
        } else if (last) {
            tf.setAnchor(true);
            tf.setAnchorNames({ kEndAnchorName });
        }
        if (!text.isEmpty())
            cur.insertText(text, tf);
        else if (last) {
            // Degenerate (never produced by renderExtractLines — the
            // caption is always non-empty) but keep the invariant: the
            // last line must host the end anchor.
            cur.insertText(QStringLiteral(" "), tf);
        }
    }

    // Reset to a plain format so typing after the caption never inherits
    // the end anchor (which would grow the region and trip the sig).
    QTextCharFormat plainReset;
    cur.setCharFormat(plainReset);
    cur.setBlockCharFormat(plainReset);
}

}  // namespace NoterExtractApply
