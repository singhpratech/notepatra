#include "diff_view.h"

#include "rustbridge.h"

#include <QApplication>
#include <QFontDatabase>
#include <QHBoxLayout>
#include <QPalette>
#include <QPlainTextEdit>
#include <QTextBlock>
#include <QTextBlockFormat>
#include <QTextCursor>

// Slice B+C — DiffView implementation.
//
// We split each input on '\n' (preserving empty trailing lines), feed the two
// blobs to RustCore::computeDiff which calls into rust-core/src/diff.rs (the
// `similar` crate, Myers under the hood), then walk the returned entries to
// build per-line "removed" / "added" flags that drive the background tint.
//
// Why two QPlainTextEdits and not one merged view: the Composer's [Diff]
// expander appears inline below a row that may itself sit inside an outer
// scroll area. A side-by-side layout makes the before/after columns visually
// independent so users can scroll one while comparing visually with the
// other; it also keeps line numbering trivial since we just emit each list
// of lines verbatim.
//
// We intentionally do NOT use Qt stylesheets to colour individual lines:
// stylesheet rules can't target a single QTextBlock. Instead we set a
// QTextBlockFormat::backgroundColor() on each block via QTextCursor.
// QPlainTextEdit honours those backgrounds during paint.

namespace {

// Light/dark-aware tint pair for removed-from-left / added-to-right blocks.
// The exact hexes were chosen by the spec to match GitHub's diff palette
// closely enough that returning users feel at home.
struct TintPair {
    QColor removed;  // red — appears on the left pane
    QColor added;    // green — appears on the right pane
};

static bool isDarkPalette() {
    // We look at the application palette rather than probing the OS again
    // (themes.h::detectSystemTheme spawns gsettings/QSettings, which is too
    // heavy for a constructor that may be called many times). The
    // QApplication palette is updated whenever the user switches themes via
    // mainwindow.cpp::applyTheme(), so this stays correct.
    const QColor bg = QApplication::palette().color(QPalette::Base);
    // Standard luminance check — light backgrounds have luminance > 0.5.
    const qreal lum =
        (0.299 * bg.redF()) + (0.587 * bg.greenF()) + (0.114 * bg.blueF());
    return lum < 0.5;
}

static TintPair tintsForCurrentTheme() {
    if (isDarkPalette()) {
        return {QColor("#5C2424"), QColor("#1E4620")};
    }
    return {QColor("#FFD7D7"), QColor("#D4EDDA")};
}

// Split text on '\n' while preserving trailing empty lines so a 3-line file
// stays a 3-line file even after a trailing newline. QString::split with
// Qt::KeepEmptyParts is what we want — it matches how the Rust diff routine
// also splits internally so line indices line up 1:1.
static QStringList splitLines(const QString &s) {
    return s.split('\n', Qt::KeepEmptyParts);
}

}  // namespace

DiffView::DiffView(const QString &beforeText, const QString &afterText, QWidget *parent)
    : QWidget(parent) {
    m_leftLines = splitLines(beforeText);
    m_rightLines = splitLines(afterText);
    m_leftRemoved.fill(false, m_leftLines.size());
    m_rightAdded.fill(false, m_rightLines.size());

    // Compute the diff via Rust. Tag mapping (mirrors RustCore::DiffEntry):
    //   0 = equal      → no tint on either side
    //   1 = insert     → only present on the right; mark m_rightAdded[rightLine-1]
    //   2 = delete     → only present on the left;  mark m_leftRemoved[leftLine-1]
    // leftLine/rightLine are 1-indexed in the Rust output, with 0 meaning
    // "this entry doesn't reference that side."
    const RustCore::DiffInfo diff = RustCore::computeDiff(beforeText, afterText);
    for (const RustCore::DiffEntry &e : diff.entries) {
        if (e.tag == 1 && e.rightLine > 0 && e.rightLine <= m_rightLines.size()) {
            m_rightAdded[e.rightLine - 1] = true;
        } else if (e.tag == 2 && e.leftLine > 0 && e.leftLine <= m_leftLines.size()) {
            m_leftRemoved[e.leftLine - 1] = true;
        }
    }

    // Build the side-by-side widgets. We use a fixed monospaced font because
    // a diff is fundamentally about column alignment; the editor's main font
    // (which may be proportional) would obscure indentation drift.
    const QFont mono = QFontDatabase::systemFont(QFontDatabase::FixedFont);

    m_left = new QPlainTextEdit;
    m_left->setReadOnly(true);
    m_left->setFont(mono);
    m_left->setLineWrapMode(QPlainTextEdit::NoWrap);
    m_left->setPlainText(beforeText);

    m_right = new QPlainTextEdit;
    m_right->setReadOnly(true);
    m_right->setFont(mono);
    m_right->setLineWrapMode(QPlainTextEdit::NoWrap);
    m_right->setPlainText(afterText);

    auto *layout = new QHBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(4);
    layout->addWidget(m_left, 1);
    layout->addWidget(m_right, 1);

    renderTints();
}

void DiffView::renderTints() {
    const TintPair pair = tintsForCurrentTheme();

    auto paint = [](QPlainTextEdit *edit, const QVector<bool> &flags, const QColor &tint) {
        QTextDocument *doc = edit->document();
        // Iterate blocks rather than line indices: QTextDocument blocks are
        // 1:1 with lines for QPlainTextEdit (no rich-text wrapping mid-line),
        // and walking via firstBlock()/next() avoids O(n) lookups via
        // findBlockByLineNumber on every iteration.
        QTextBlock block = doc->firstBlock();
        int idx = 0;
        while (block.isValid() && idx < flags.size()) {
            if (flags[idx]) {
                QTextCursor cursor(block);
                QTextBlockFormat fmt = cursor.blockFormat();
                fmt.setBackground(tint);
                cursor.setBlockFormat(fmt);
            }
            block = block.next();
            ++idx;
        }
    };

    paint(m_left, m_leftRemoved, pair.removed);
    paint(m_right, m_rightAdded, pair.added);
}
