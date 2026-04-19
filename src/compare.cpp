// Notepatra Compare panel — inspired by ComparePlus by Pavel Nedev.
//
// Original ComparePlus repo: https://github.com/pnedev/comparePlus
// Pavel's plugin is the gold-standard diff for Notepad++. The visual UX
// in this file (colored line markers for added/deleted/changed, side-by-
// side editors, overview bar, and synced scrolling) borrows from that
// design language. The implementation here is a fresh Qt + Rust port.

#include "compare.h"
#include "lexerutils.h"
#include "npp_palette.h"
#include "rustbridge.h"
#include "fonts.h"
#include "config.h"

#include <QEvent>
#include <QFont>
#include <QHBoxLayout>
#include <QMouseEvent>
#include <QPainter>
#include <QScrollBar>
#include <QSplitter>
#include <QVBoxLayout>
#include <Qsci/qscilexer.h>
#include <Qsci/qscistyle.h>

namespace {

enum CompareRowKind {
    RowEqual = 0,
    RowAdded = 1,
    RowDeleted = 2,
    RowChanged = 3,
};

struct ComparableLine {
    QString originalText;
    QString compareText;
    int originalLineNumber = 0;
};

struct CompareDisplayRow {
    int kind = RowEqual;
    QString leftText;
    QString rightText;
    int leftLineNumber = 0;
    int rightLineNumber = 0;
};

// ═══════════════════════════════════════════════════════════════════════
// Theme-aware colour palette.
//
// Two palettes below: LIGHT is used when Config::theme == "Light" (and is
// the historical default). DARK is used for "Dark" and "Monokai" themes.
// Colours picked to be visually distinct from each other AND from typical
// syntax highlighting — ComparePlus on Notepad++ uses similar saturation
// so diffs stand out on top of coloured lexer output.
// ═══════════════════════════════════════════════════════════════════════

struct ComparePalette {
    // Line-background markers (added/deleted/changed/blank)
    QColor bgAdded, bgDeleted, bgChangedLeft, bgChangedRight, bgBlank;
    // Margin symbol bg/fg
    QColor symAddedFg, symDeletedFg, symChangedFg;
    // Intra-line indicator colours (BGR→RGB handled at call site)
    QColor inlineRemovedFg, inlineAddedFg;
    // Nav bar
    QColor navBg, navLaneBg, navAdded, navDeleted, navChanged, navViewport, navViewportBorder;
    // Editor surface
    QColor editorBg, editorFg, marginBg, marginFg;
    // Header
    QColor headerBg, headerFg, headerBorder;
    // Splitter
    QColor splitterBg, splitterBorder;
};

static bool compareIsDark() {
    const QString &t = Config::instance().theme;
    return t.compare("Dark", Qt::CaseInsensitive) == 0 ||
           t.compare("Monokai", Qt::CaseInsensitive) == 0;
}

static ComparePalette comparePalette() {
    ComparePalette p;
    if (compareIsDark()) {
        // Dark mode — saturated backgrounds dark enough to read white text on
        p.bgAdded         = QColor("#1E4D2B");   // forest green
        p.bgDeleted       = QColor("#5A1D1D");   // dark crimson
        p.bgChangedLeft   = QColor("#4A3A10");   // amber brown (left half)
        p.bgChangedRight  = QColor("#4A3A10");
        p.bgBlank         = QColor("#1A1A1A");   // near-black filler
        p.symAddedFg      = QColor("#5FE07E");
        p.symDeletedFg    = QColor("#FF7A7A");
        p.symChangedFg    = QColor("#FFC857");
        p.inlineRemovedFg = QColor("#FF5F5F");   // bright red char highlight on left
        p.inlineAddedFg   = QColor("#3CCB5A");   // bright green char highlight on right
        p.navBg           = QColor("#1E1E1E");
        p.navLaneBg       = QColor("#2A2A2A");
        p.navAdded        = QColor("#3CCB5A");
        p.navDeleted      = QColor("#FF5F5F");
        p.navChanged      = QColor("#FFC857");
        p.navViewport     = QColor(100, 170, 255, 58);
        p.navViewportBorder = QColor("#4FA3FF");
        p.editorBg        = QColor("#1E1E1E");
        p.editorFg        = QColor("#D4D4D4");
        p.marginBg        = QColor("#252526");
        p.marginFg        = QColor("#858585");
        p.headerBg        = QColor("#2D2D2D");
        p.headerFg        = QColor("#D4D4D4");
        p.headerBorder    = QColor("#3E3E3E");
        p.splitterBg      = QColor("#2D2D2D");
        p.splitterBorder  = QColor("#1E1E1E");
    } else {
        // Light mode — more saturated than the old pastels so diffs pop
        // against syntax-highlighted text.
        p.bgAdded         = QColor("#C8F0C4");   // punchy mint green
        p.bgDeleted       = QColor("#FBCBCB");   // punchy pink-red
        p.bgChangedLeft   = QColor("#FFECB0");   // warm amber
        p.bgChangedRight  = QColor("#FFECB0");
        p.bgBlank         = QColor("#F5F5F5");
        p.symAddedFg      = QColor("#18A02E");
        p.symDeletedFg    = QColor("#C92A2A");
        p.symChangedFg    = QColor("#B8860B");
        p.inlineRemovedFg = QColor("#D92020");   // deep red for removed chars on left
        p.inlineAddedFg   = QColor("#169930");   // deep green for added chars on right
        p.navBg           = QColor("#FAFBFC");
        p.navLaneBg       = QColor("#E7ECF2");
        p.navAdded        = QColor("#18A02E");
        p.navDeleted      = QColor("#C92A2A");
        p.navChanged      = QColor("#E5A317");
        p.navViewport     = QColor(33, 150, 243, 48);
        p.navViewportBorder = QColor("#1565C0");
        p.editorBg        = QColor("#FFFFFF");
        p.editorFg        = QColor("#000000");
        p.marginBg        = QColor("#F2F2F2");
        p.marginFg        = QColor("#8390A4");
        p.headerBg        = QColor("#F7F7F7");
        p.headerFg        = QColor("#4B4B4B");
        p.headerBorder    = QColor("#D9D9D9");
        p.splitterBg      = QColor("#F1F1F1");
        p.splitterBorder  = QColor("#D8D8D8");
    }
    return p;
}

static QColor navAddedColor()        { return comparePalette().navAdded; }
static QColor navDeletedColor()      { return comparePalette().navDeleted; }
static QColor navChangedLeftColor()  { return comparePalette().navChanged; }
static QColor navChangedRightColor() { return comparePalette().navChanged; }

// ═══════════════════════════════════════════════════════════════════════
// Word-level LCS intra-line diff.
//
// For RowChanged pairs we need to highlight exactly which *tokens* (words
// or punctuation) changed. The previous implementation only used common
// prefix + common suffix matching, which over-highlights when a single
// word changes in the middle ("foo bar baz" vs "foo BUX baz" would be
// marked "bar" vs "BUX" only — correct for that case, but for something
// like "alpha beta gamma delta" vs "alpha GAMMA beta delta" the prefix/
// suffix approach flags nearly everything.)
//
// This is standard dynamic-programming LCS on token arrays — O(n·m) time,
// fine for single lines. Tokens are consecutive runs of word-characters
// OR single non-word characters, so whitespace / punctuation boundaries
// are respected.
// ═══════════════════════════════════════════════════════════════════════

struct InlineToken {
    int start;        // char offset into the line
    int length;       // char count
    QString text;
};

static QVector<InlineToken> tokenizeLine(const QString &line) {
    QVector<InlineToken> tokens;
    const int n = line.size();
    int i = 0;
    while (i < n) {
        int j = i;
        if (line[i].isLetterOrNumber() || line[i] == '_') {
            while (j < n && (line[j].isLetterOrNumber() || line[j] == '_')) ++j;
        } else {
            // one-char token for punctuation, whitespace, etc
            j = i + 1;
        }
        tokens.append({i, j - i, line.mid(i, j - i)});
        i = j;
    }
    return tokens;
}

// Returns two bitmasks: leftChanged[i] = true if token i on the left is
// not part of the LCS (i.e. was removed); rightChanged[i] = true if token
// i on the right was added.
static void computeTokenDiff(const QVector<InlineToken> &left,
                             const QVector<InlineToken> &right,
                             QVector<bool> &leftChanged,
                             QVector<bool> &rightChanged) {
    const int n = left.size();
    const int m = right.size();
    leftChanged.fill(false, n);
    rightChanged.fill(false, m);

    if (n == 0 || m == 0) {
        for (int i = 0; i < n; ++i) leftChanged[i] = true;
        for (int j = 0; j < m; ++j) rightChanged[j] = true;
        return;
    }

    // dp[i][j] = length of LCS of left[0..i) and right[0..j)
    QVector<QVector<int>> dp(n + 1, QVector<int>(m + 1, 0));
    for (int i = 1; i <= n; ++i) {
        for (int j = 1; j <= m; ++j) {
            if (left[i-1].text == right[j-1].text)
                dp[i][j] = dp[i-1][j-1] + 1;
            else
                dp[i][j] = qMax(dp[i-1][j], dp[i][j-1]);
        }
    }

    // Backtrack: whatever isn't on the LCS path is a change
    int i = n, j = m;
    while (i > 0 && j > 0) {
        if (left[i-1].text == right[j-1].text) {
            --i; --j;
        } else if (dp[i-1][j] >= dp[i][j-1]) {
            leftChanged[i-1] = true;
            --i;
        } else {
            rightChanged[j-1] = true;
            --j;
        }
    }
    while (i > 0) { leftChanged[--i] = true; }
    while (j > 0) { rightChanged[--j] = true; }
}

static void applyCompareLexer(QsciScintilla *editor, const QString &name, const QString &text) {
    if (!editor) return;

    const QString lang = detectLanguageFromPath(name, text);
    QFont font = notepatraCodeFont();
    const ComparePalette pal = comparePalette();

    if (QsciLexer *lexer = createLexerForLanguage(lang, editor)) {
        lexer->setDefaultFont(font);
        lexer->setDefaultPaper(pal.editorBg);
        lexer->setDefaultColor(pal.editorFg);
        editor->setLexer(lexer);
        applyNotepadPlusPalette(lexer, font);
        // Applied palette above may have light-mode colours — override default
        // paper to match our compare theme so background matches the markers.
        editor->setPaper(pal.editorBg);
        editor->setColor(pal.editorFg);
    } else {
        editor->setLexer(nullptr);
        editor->setFont(font);
        editor->setPaper(pal.editorBg);
        editor->setColor(pal.editorFg);
    }
}

static QString stripWhitespace(const QString &text) {
    QString stripped;
    stripped.reserve(text.size());
    for (QChar ch : text) {
        if (!ch.isSpace()) stripped.append(ch);
    }
    return stripped;
}

static QStringList splitLinesForCompare(const QString &text) {
    if (text.isEmpty()) return {};

    QStringList lines = text.split('\n', Qt::KeepEmptyParts);
    if (text.endsWith('\n') && !lines.isEmpty()) lines.removeLast();
    return lines;
}

static QVector<ComparableLine> buildComparableLines(const QString &text,
                                                    bool ignoreWhitespace,
                                                    bool ignoreCase,
                                                    bool ignoreEmptyLines) {
    QVector<ComparableLine> lines;
    const QStringList sourceLines = splitLinesForCompare(text);
    lines.reserve(sourceLines.size());

    for (int i = 0; i < sourceLines.size(); ++i) {
        QString compareText = sourceLines[i];
        if (ignoreWhitespace) compareText = stripWhitespace(compareText);
        if (ignoreCase) compareText = compareText.toLower();
        if (ignoreEmptyLines && compareText.trimmed().isEmpty()) continue;

        lines.append({sourceLines[i], compareText, i + 1});
    }

    return lines;
}

static QString joinedComparableText(const QVector<ComparableLine> &lines) {
    QStringList parts;
    parts.reserve(lines.size());
    for (const ComparableLine &line : lines) parts << line.compareText;
    return parts.join('\n');
}

static const ComparableLine *lineAt(const QVector<ComparableLine> &lines, int oneBasedIndex) {
    if (oneBasedIndex <= 0 || oneBasedIndex > lines.size()) return nullptr;
    return &lines[oneBasedIndex - 1];
}

static QVector<CompareDisplayRow> buildDisplayRows(const RustCore::DiffInfo &diff,
                                                   const QVector<ComparableLine> &leftLines,
                                                   const QVector<ComparableLine> &rightLines) {
    QVector<CompareDisplayRow> rows;
    rows.reserve(diff.entries.size());

    int i = 0;
    while (i < diff.entries.size()) {
        const auto &entry = diff.entries[i];
        if (entry.tag == 0) {
            const ComparableLine *left = lineAt(leftLines, entry.leftLine);
            const ComparableLine *right = lineAt(rightLines, entry.rightLine);
            rows.append({
                RowEqual,
                left ? left->originalText : QString(),
                right ? right->originalText : QString(),
                left ? left->originalLineNumber : 0,
                right ? right->originalLineNumber : 0,
            });
            ++i;
            continue;
        }

        QVector<const ComparableLine *> deletes;
        while (i < diff.entries.size() && diff.entries[i].tag == 2) {
            deletes.append(lineAt(leftLines, diff.entries[i].leftLine));
            ++i;
        }

        QVector<const ComparableLine *> adds;
        while (i < diff.entries.size() && diff.entries[i].tag == 1) {
            adds.append(lineAt(rightLines, diff.entries[i].rightLine));
            ++i;
        }

        const int pairedCount = qMin(deletes.size(), adds.size());
        for (int j = 0; j < pairedCount; ++j) {
            rows.append({
                RowChanged,
                deletes[j] ? deletes[j]->originalText : QString(),
                adds[j] ? adds[j]->originalText : QString(),
                deletes[j] ? deletes[j]->originalLineNumber : 0,
                adds[j] ? adds[j]->originalLineNumber : 0,
            });
        }

        for (int j = pairedCount; j < deletes.size(); ++j) {
            rows.append({
                RowDeleted,
                deletes[j] ? deletes[j]->originalText : QString(),
                QString(),
                deletes[j] ? deletes[j]->originalLineNumber : 0,
                0,
            });
        }

        for (int j = pairedCount; j < adds.size(); ++j) {
            rows.append({
                RowAdded,
                QString(),
                adds[j] ? adds[j]->originalText : QString(),
                0,
                adds[j] ? adds[j]->originalLineNumber : 0,
            });
        }
    }

    return rows;
}

} // namespace

CompareNavBar::CompareNavBar(QWidget *parent) : QWidget(parent) {
    setObjectName("compareNavBar");
    setMinimumWidth(26);
    setMaximumWidth(26);
    setMouseTracking(true);
    setCursor(Qt::PointingHandCursor);
}

void CompareNavBar::setRows(const QVector<int> &rowKinds) {
    m_rowKinds = rowKinds;
    if (m_rowKinds.isEmpty()) {
        m_firstVisibleRow = 0;
        m_visibleRows = 0;
    } else {
        m_firstVisibleRow = qBound(0, m_firstVisibleRow, m_rowKinds.size() - 1);
        m_visibleRows = qMax(0, m_visibleRows);
    }
    update();
}

void CompareNavBar::setViewport(int firstVisibleRow, int visibleRows) {
    if (m_rowKinds.isEmpty()) {
        m_firstVisibleRow = 0;
        m_visibleRows = 0;
    } else {
        m_firstVisibleRow = qBound(0, firstVisibleRow, m_rowKinds.size() - 1);
        m_visibleRows = qBound(1, visibleRows, qMax(1, m_rowKinds.size()));
    }
    update();
}

int CompareNavBar::totalRows() const {
    return m_rowKinds.size();
}

int CompareNavBar::diffMarkerCount() const {
    int count = 0;
    for (int kind : m_rowKinds) {
        if (kind != RowEqual) ++count;
    }
    return count;
}

int CompareNavBar::firstVisibleRow() const {
    return m_firstVisibleRow;
}

int CompareNavBar::visibleRows() const {
    return m_visibleRows;
}

void CompareNavBar::paintEvent(QPaintEvent *event) {
    Q_UNUSED(event);

    const ComparePalette pal = comparePalette();
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.fillRect(rect(), pal.navBg);

    QRectF lane = rect().adjusted(8, 6, -8, -6);
    painter.setPen(Qt::NoPen);
    painter.setBrush(pal.navLaneBg);
    painter.drawRoundedRect(lane, 6, 6);

    if (m_rowKinds.isEmpty()) return;

    const qreal rowCount = static_cast<qreal>(m_rowKinds.size());
    for (int i = 0; i < m_rowKinds.size(); ++i) {
        if (m_rowKinds[i] == RowEqual) continue;

        qreal top = lane.top() + (static_cast<qreal>(i) / rowCount) * lane.height();
        qreal bottom = lane.top() + (static_cast<qreal>(i + 1) / rowCount) * lane.height();
        if (bottom - top < 2.0) bottom = top + 2.0;

        QRectF marker(lane.left() + 3, top, lane.width() - 6, bottom - top);
        if (m_rowKinds[i] == RowChanged) {
            QRectF leftHalf = marker;
            leftHalf.setWidth(marker.width() / 2.0);

            QRectF rightHalf = marker;
            rightHalf.setLeft(leftHalf.right());

            painter.setBrush(navChangedLeftColor());
            painter.drawRoundedRect(leftHalf, 2, 2);
            painter.setBrush(navChangedRightColor());
            painter.drawRoundedRect(rightHalf, 2, 2);
        } else {
            painter.setBrush(m_rowKinds[i] == RowAdded ? navAddedColor() : navDeletedColor());
            painter.drawRoundedRect(marker, 2, 2);
        }
    }

    const int clampedVisibleRows = qBound(1, m_visibleRows, m_rowKinds.size());
    const qreal viewportTop = lane.top() +
        (static_cast<qreal>(m_firstVisibleRow) / rowCount) * lane.height();
    const qreal viewportBottom = lane.top() +
        (static_cast<qreal>(qMin(m_rowKinds.size(), m_firstVisibleRow + clampedVisibleRows)) / rowCount) *
        lane.height();

    QRectF viewport(lane.left() + 1.5, viewportTop, lane.width() - 3.0,
                    qMax<qreal>(12.0, viewportBottom - viewportTop));
    if (viewport.bottom() > lane.bottom()) viewport.moveBottom(lane.bottom());

    painter.setBrush(pal.navViewport);
    painter.setPen(QPen(pal.navViewportBorder, 1.2));
    painter.drawRoundedRect(viewport, 4, 4);
}

void CompareNavBar::mousePressEvent(QMouseEvent *event) {
    if (event->button() == Qt::LeftButton) activateRowAt(event->pos().y());
}

void CompareNavBar::mouseMoveEvent(QMouseEvent *event) {
    if (event->buttons() & Qt::LeftButton) activateRowAt(event->pos().y());
}

void CompareNavBar::activateRowAt(int y) {
    if (m_rowKinds.isEmpty() || height() <= 0) return;

    const int clampedY = qBound(0, y, height() - 1);
    const int row = qBound(0,
                           static_cast<int>((static_cast<double>(clampedY) / height()) * m_rowKinds.size()),
                           m_rowKinds.size() - 1);
    emit rowActivated(row);
}

// Marker numbers for diff highlighting
#define MARKER_ADDED         4
#define MARKER_DELETED       5
#define MARKER_CHANGED_LEFT  6
#define MARKER_CHANGED_RIGHT 7
#define MARKER_BLANK         8

// Symbol margin markers — small icons in the symbol margin
#define SYM_CHANGE_LEFT  9
#define SYM_CHANGE_RIGHT 10
#define SYM_ADD          11
#define SYM_DEL          12

void CompareWidget::setupEditor(QsciScintilla *ed) {
    const ComparePalette pal = comparePalette();
    QFont mono = notepatraCodeFont();
    ed->setFont(mono);
    ed->setMarginsFont(mono);
    ed->setReadOnly(true);
    ed->setUtf8(true);
    ed->zoomTo(0);
    ed->setCaretWidth(0);

    ed->setMarginType(0, QsciScintilla::TextMargin);
    ed->setMarginWidth(0, "00000");
    ed->setMarginBackgroundColor(0, pal.marginBg);
    ed->setMarginsForegroundColor(pal.marginFg);

    ed->setMarginType(1, QsciScintilla::SymbolMargin);
    ed->setMarginWidth(1, 14);
    ed->setMarginBackgroundColor(1, pal.marginBg);
    ed->setMarginMarkerMask(
        1, (1 << SYM_CHANGE_LEFT) | (1 << SYM_CHANGE_RIGHT) | (1 << SYM_ADD) | (1 << SYM_DEL));

    ed->setMarginType(2, QsciScintilla::SymbolMarginColor);
    ed->setMarginWidth(2, 4);
    ed->setMarginSensitivity(2, false);
    ed->setMarginBackgroundColor(2, pal.marginBg);
    ed->setFolding(QsciScintilla::NoFoldStyle);

    ed->markerDefine(QsciScintilla::Background, MARKER_ADDED);
    ed->setMarkerBackgroundColor(pal.bgAdded, MARKER_ADDED);

    ed->markerDefine(QsciScintilla::Background, MARKER_DELETED);
    ed->setMarkerBackgroundColor(pal.bgDeleted, MARKER_DELETED);

    ed->markerDefine(QsciScintilla::Background, MARKER_CHANGED_LEFT);
    ed->setMarkerBackgroundColor(pal.bgChangedLeft, MARKER_CHANGED_LEFT);

    ed->markerDefine(QsciScintilla::Background, MARKER_CHANGED_RIGHT);
    ed->setMarkerBackgroundColor(pal.bgChangedRight, MARKER_CHANGED_RIGHT);

    ed->markerDefine(QsciScintilla::Background, MARKER_BLANK);
    ed->setMarkerBackgroundColor(pal.bgBlank, MARKER_BLANK);

    ed->markerDefine('#', SYM_CHANGE_LEFT);
    ed->setMarkerForegroundColor(pal.symChangedFg, SYM_CHANGE_LEFT);
    ed->setMarkerBackgroundColor(pal.bgChangedLeft, SYM_CHANGE_LEFT);

    ed->markerDefine('#', SYM_CHANGE_RIGHT);
    ed->setMarkerForegroundColor(pal.symChangedFg, SYM_CHANGE_RIGHT);
    ed->setMarkerBackgroundColor(pal.bgChangedRight, SYM_CHANGE_RIGHT);

    ed->markerDefine(QsciScintilla::Plus, SYM_ADD);
    ed->setMarkerForegroundColor(pal.symAddedFg, SYM_ADD);
    ed->setMarkerBackgroundColor(pal.bgAdded, SYM_ADD);

    ed->markerDefine(QsciScintilla::Minus, SYM_DEL);
    ed->setMarkerForegroundColor(pal.symDeletedFg, SYM_DEL);
    ed->setMarkerBackgroundColor(pal.bgDeleted, SYM_DEL);

    ed->setMarkerForegroundColor(pal.symAddedFg,   MARKER_ADDED);
    ed->setMarkerForegroundColor(pal.symDeletedFg, MARKER_DELETED);
    ed->setMarkerForegroundColor(pal.symChangedFg, MARKER_CHANGED_LEFT);
    ed->setMarkerForegroundColor(pal.symChangedFg, MARKER_CHANGED_RIGHT);

    ed->setPaper(pal.editorBg);
    ed->setColor(pal.editorFg);
    ed->setCaretLineVisible(false);
    ed->setStyleSheet(QString("QsciScintilla { border: none; background: %1; }")
                          .arg(pal.editorBg.name()));
}

void CompareWidget::setEditorsEditable(bool editable) {
    for (QsciScintilla *editor : {m_leftEditor, m_rightEditor}) {
        editor->setReadOnly(!editable);
        editor->setCaretWidth(editable ? 2 : 0);
        editor->setCaretLineVisible(editable);
    }
}

void CompareWidget::syncTextsFromEditors() {
    const QStringList leftLines = splitLinesForCompare(m_leftEditor->text());
    const QStringList rightLines = splitLinesForCompare(m_rightEditor->text());

    if (leftLines.size() != m_rowKinds.size() || rightLines.size() != m_rowKinds.size()) {
        m_leftText = m_leftEditor->text();
        m_rightText = m_rightEditor->text();
        return;
    }

    QStringList leftSource;
    QStringList rightSource;
    leftSource.reserve(leftLines.size());
    rightSource.reserve(rightLines.size());

    for (int i = 0; i < m_rowKinds.size(); ++i) {
        const QString leftLine = leftLines.value(i);
        const QString rightLine = rightLines.value(i);

        switch (m_rowKinds[i]) {
        case RowEqual:
        case RowChanged:
            leftSource << leftLine;
            rightSource << rightLine;
            break;
        case RowAdded:
            if (!leftLine.isEmpty()) leftSource << leftLine;
            rightSource << rightLine;
            break;
        case RowDeleted:
            leftSource << leftLine;
            if (!rightLine.isEmpty()) rightSource << rightLine;
            break;
        }
    }

    m_leftText = leftSource.join('\n');
    m_rightText = rightSource.join('\n');
    if (!leftSource.isEmpty()) m_leftText += '\n';
    if (!rightSource.isEmpty()) m_rightText += '\n';
}

void CompareWidget::updateEditToggle() {
    if (!m_editToggle) return;

    if (m_editable) {
        m_editToggle->setText("Lock Editing");
        m_editToggle->setToolTip("Editing is unlocked. Lock to freeze the panes and recompare.");
        m_editToggle->setStyleSheet("font-weight: 600; color: #8A5A00;");
    } else {
        m_editToggle->setText("Unlock Editing");
        m_editToggle->setToolTip("Locked by default. Unlock to edit the compare panes directly.");
        m_editToggle->setStyleSheet("font-weight: 600; color: #555;");
    }
}

CompareWidget::CompareWidget(QWidget *parent) : QWidget(parent) {
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);

    auto *toolbar = new QHBoxLayout;
    toolbar->setContentsMargins(6, 4, 6, 4);

    auto *prevBtn = new QPushButton("< Prev");
    prevBtn->setFixedSize(70, 26);
    auto *nextBtn = new QPushButton("Next >");
    nextBtn->setFixedSize(70, 26);
    auto *recompBtn = new QPushButton("Recompare");
    recompBtn->setFixedSize(90, 26);
    m_editToggle = new QPushButton("Unlock Editing");
    m_editToggle->setObjectName("compareEditToggle");
    m_editToggle->setCheckable(true);
    m_editToggle->setFixedHeight(26);

    m_ignoreWhitespace = new QCheckBox("Ignore spaces");
    m_ignoreCase = new QCheckBox("Ignore case");
    m_ignoreEmptyLines = new QCheckBox("Ignore empty lines");

    m_statsLabel = new QLabel;
    m_statsLabel->setStyleSheet("font-weight: bold; color: #333;");

    toolbar->addWidget(prevBtn);
    toolbar->addWidget(nextBtn);
    toolbar->addWidget(recompBtn);
    toolbar->addWidget(m_editToggle);
    toolbar->addSpacing(16);
    toolbar->addWidget(m_ignoreWhitespace);
    toolbar->addWidget(m_ignoreCase);
    toolbar->addWidget(m_ignoreEmptyLines);
    toolbar->addStretch();
    toolbar->addWidget(m_statsLabel);
    layout->addLayout(toolbar);

    auto *headerRow = new QHBoxLayout;
    headerRow->setContentsMargins(0, 0, 0, 0);
    headerRow->setSpacing(2);
    m_leftHeader = new QLabel("  Left file");
    m_leftHeader->setFixedHeight(20);
    m_leftHeader->setStyleSheet(
        "font-weight: 600; background: #F7F7F7; color: #4B4B4B; "
        "padding: 1px 8px; border-bottom: 1px solid #D9D9D9;");
    m_rightHeader = new QLabel("  Right file");
    m_rightHeader->setFixedHeight(20);
    m_rightHeader->setStyleSheet(
        "font-weight: 600; background: #F7F7F7; color: #4B4B4B; "
        "padding: 1px 8px; border-bottom: 1px solid #D9D9D9;");
    headerRow->addWidget(m_leftHeader, 1);
    headerRow->addWidget(m_rightHeader, 1);
    layout->addLayout(headerRow);

    const ComparePalette uiPal = comparePalette();
    m_leftHeader->setStyleSheet(QString(
        "font-weight: 600; background: %1; color: %2; "
        "padding: 1px 8px; border-bottom: 1px solid %3;")
        .arg(uiPal.headerBg.name(), uiPal.headerFg.name(), uiPal.headerBorder.name()));
    m_rightHeader->setStyleSheet(QString(
        "font-weight: 600; background: %1; color: %2; "
        "padding: 1px 8px; border-bottom: 1px solid %3;")
        .arg(uiPal.headerBg.name(), uiPal.headerFg.name(), uiPal.headerBorder.name()));
    m_statsLabel->setStyleSheet(QString("font-weight: bold; color: %1;")
        .arg(uiPal.headerFg.name()));

    auto *splitter = new QSplitter(Qt::Horizontal);
    splitter->setHandleWidth(10);
    splitter->setStyleSheet(QString(
        "QSplitter::handle { background: %1; border-left: 1px solid %2; "
        "border-right: 1px solid %2; }")
        .arg(uiPal.splitterBg.name(), uiPal.splitterBorder.name()));

    m_leftEditor = new QsciScintilla;
    m_leftEditor->setObjectName("compareLeftEditor");
    setupEditor(m_leftEditor);
    splitter->addWidget(m_leftEditor);

    m_rightEditor = new QsciScintilla;
    m_rightEditor->setObjectName("compareRightEditor");
    setupEditor(m_rightEditor);
    splitter->addWidget(m_rightEditor);

    m_navBar = new CompareNavBar;

    auto *compareRow = new QHBoxLayout;
    compareRow->setContentsMargins(0, 0, 0, 0);
    compareRow->setSpacing(0);
    compareRow->addWidget(splitter, 1);
    compareRow->addWidget(m_navBar);
    layout->addLayout(compareRow, 1);

    m_leftEditor->installEventFilter(this);
    m_rightEditor->installEventFilter(this);

    connect(m_leftEditor->verticalScrollBar(), &QScrollBar::valueChanged, this, [this](int val) {
        m_rightEditor->verticalScrollBar()->setValue(val);
        updateOverviewViewport();
    });
    connect(m_rightEditor->verticalScrollBar(), &QScrollBar::valueChanged, this, [this](int val) {
        m_leftEditor->verticalScrollBar()->setValue(val);
        updateOverviewViewport();
    });
    connect(m_leftEditor->horizontalScrollBar(), &QScrollBar::valueChanged, this, [this](int val) {
        m_rightEditor->horizontalScrollBar()->setValue(val);
    });
    connect(m_rightEditor->horizontalScrollBar(), &QScrollBar::valueChanged, this, [this](int val) {
        m_leftEditor->horizontalScrollBar()->setValue(val);
    });

    connect(prevBtn, &QPushButton::clicked, this, &CompareWidget::navigatePrev);
    connect(nextBtn, &QPushButton::clicked, this, &CompareWidget::navigateNext);
    connect(recompBtn, &QPushButton::clicked, this, &CompareWidget::recompare);
    connect(m_editToggle, &QPushButton::toggled, this, [this](bool checked) {
        m_editable = checked;
        updateEditToggle();
        if (m_editable) {
            setEditorsEditable(true);
        } else {
            syncTextsFromEditors();
            recompare();
        }
    });
    connect(m_ignoreWhitespace, &QCheckBox::toggled, this, [this]() { recompare(); });
    connect(m_ignoreCase, &QCheckBox::toggled, this, [this]() { recompare(); });
    connect(m_ignoreEmptyLines, &QCheckBox::toggled, this, [this]() { recompare(); });
    connect(m_navBar, &CompareNavBar::rowActivated, this, &CompareWidget::jumpToRow);

    updateEditToggle();
}

int CompareWidget::diffCount() const {
    return m_diffLines.size();
}

int CompareWidget::rowCount() const {
    return m_rowKinds.size();
}

bool CompareWidget::eventFilter(QObject *watched, QEvent *event) {
    if ((watched == m_leftEditor || watched == m_rightEditor) &&
        (event->type() == QEvent::Resize || event->type() == QEvent::Show)) {
        updateOverviewViewport();
    }

    return QWidget::eventFilter(watched, event);
}

void CompareWidget::compare(const QString &leftText, const QString &leftName,
                            const QString &rightText, const QString &rightName) {
    const ComparePalette pal = comparePalette();
    m_leftText = leftText;
    m_rightText = rightText;
    m_editable = false;
    if (m_editToggle) m_editToggle->setChecked(false);
    updateEditToggle();
    m_leftHeader->setText("  " + leftName);
    m_rightHeader->setText("  " + rightName);

    QFont mono = notepatraCodeFont();
    m_leftEditor->setFont(mono);
    m_rightEditor->setFont(mono);

    m_leftEditor->setColor(pal.editorFg);
    m_rightEditor->setColor(pal.editorFg);
    m_leftEditor->setPaper(pal.editorBg);
    m_rightEditor->setPaper(pal.editorBg);

    m_leftEditor->setMarginsBackgroundColor(pal.marginBg);
    m_leftEditor->setMarginsForegroundColor(pal.marginFg);
    m_rightEditor->setMarginsBackgroundColor(pal.marginBg);
    m_rightEditor->setMarginsForegroundColor(pal.marginFg);

    applyCompareLexer(m_leftEditor, leftName, leftText);
    applyCompareLexer(m_rightEditor, rightName, rightText);

    recompare();
}

void CompareWidget::recompare() {
    if (m_editable) syncTextsFromEditors();

    const bool ignoreWhitespace = m_ignoreWhitespace->isChecked();
    const bool ignoreCase = m_ignoreCase->isChecked();
    const bool ignoreEmptyLines = m_ignoreEmptyLines->isChecked();

    const QVector<ComparableLine> leftLines =
        buildComparableLines(m_leftText, ignoreWhitespace, ignoreCase, ignoreEmptyLines);
    const QVector<ComparableLine> rightLines =
        buildComparableLines(m_rightText, ignoreWhitespace, ignoreCase, ignoreEmptyLines);

    const RustCore::DiffInfo diff =
        RustCore::computeDiff(joinedComparableText(leftLines), joinedComparableText(rightLines));
    const QVector<CompareDisplayRow> rows = buildDisplayRows(diff, leftLines, rightLines);

    QString leftBuf;
    QString rightBuf;
    m_diffLines.clear();
    m_rowKinds.clear();
    m_currentDiff = -1;

    int line = 0;
    for (const CompareDisplayRow &row : rows) {
        switch (row.kind) {
        case RowEqual:
            leftBuf += row.leftText + "\n";
            rightBuf += row.rightText + "\n";
            break;
        case RowAdded:
            leftBuf += "\n";
            rightBuf += row.rightText + "\n";
            m_diffLines.append(line);
            break;
        case RowDeleted:
            leftBuf += row.leftText + "\n";
            rightBuf += "\n";
            m_diffLines.append(line);
            break;
        case RowChanged:
            leftBuf += row.leftText + "\n";
            rightBuf += row.rightText + "\n";
            m_diffLines.append(line);
            break;
        }

        m_rowKinds.append(row.kind);
        ++line;
    }

    m_leftEditor->setReadOnly(false);
    m_leftEditor->setText(leftBuf);
    m_leftEditor->setReadOnly(!m_editable);

    m_rightEditor->setReadOnly(false);
    m_rightEditor->setText(rightBuf);
    m_rightEditor->setReadOnly(!m_editable);

    // Indicator 10 = red "removed token" highlight on the LEFT pane.
    // Indicator 11 = green "added token" highlight on the RIGHT pane.
    // Scintilla wants 0x00BBGGRR, so swap R and B bytes.
    const ComparePalette indPal = comparePalette();
    auto bgr = [](const QColor &c) -> long {
        return (long(c.blue()) << 16) | (long(c.green()) << 8) | long(c.red());
    };
    m_leftEditor->SendScintilla(QsciScintilla::SCI_INDICSETSTYLE, 10, QsciScintilla::INDIC_ROUNDBOX);
    m_leftEditor->SendScintilla(QsciScintilla::SCI_INDICSETFORE, 10, bgr(indPal.inlineRemovedFg));
    m_leftEditor->SendScintilla(QsciScintilla::SCI_INDICSETALPHA, 10, compareIsDark() ? 140 : 180);
    m_leftEditor->SendScintilla(QsciScintilla::SCI_INDICSETOUTLINEALPHA, 10, 255);
    m_leftEditor->SendScintilla(QsciScintilla::SCI_INDICSETUNDER, 10, 1);

    m_rightEditor->SendScintilla(QsciScintilla::SCI_INDICSETSTYLE, 11, QsciScintilla::INDIC_ROUNDBOX);
    m_rightEditor->SendScintilla(QsciScintilla::SCI_INDICSETFORE, 11, bgr(indPal.inlineAddedFg));
    m_rightEditor->SendScintilla(QsciScintilla::SCI_INDICSETALPHA, 11, compareIsDark() ? 140 : 180);
    m_rightEditor->SendScintilla(QsciScintilla::SCI_INDICSETOUTLINEALPHA, 11, 255);
    m_rightEditor->SendScintilla(QsciScintilla::SCI_INDICSETUNDER, 11, 1);

    m_leftEditor->markerDeleteAll(MARKER_ADDED);
    m_leftEditor->markerDeleteAll(MARKER_DELETED);
    m_leftEditor->markerDeleteAll(MARKER_CHANGED_LEFT);
    m_leftEditor->markerDeleteAll(MARKER_CHANGED_RIGHT);
    m_leftEditor->markerDeleteAll(MARKER_BLANK);
    m_leftEditor->markerDeleteAll(SYM_CHANGE_LEFT);
    m_leftEditor->markerDeleteAll(SYM_CHANGE_RIGHT);
    m_leftEditor->markerDeleteAll(SYM_ADD);
    m_leftEditor->markerDeleteAll(SYM_DEL);
    m_rightEditor->markerDeleteAll(MARKER_ADDED);
    m_rightEditor->markerDeleteAll(MARKER_DELETED);
    m_rightEditor->markerDeleteAll(MARKER_CHANGED_LEFT);
    m_rightEditor->markerDeleteAll(MARKER_CHANGED_RIGHT);
    m_rightEditor->markerDeleteAll(MARKER_BLANK);
    m_rightEditor->markerDeleteAll(SYM_CHANGE_LEFT);
    m_rightEditor->markerDeleteAll(SYM_CHANGE_RIGHT);
    m_rightEditor->markerDeleteAll(SYM_ADD);
    m_rightEditor->markerDeleteAll(SYM_DEL);

    m_leftEditor->SendScintilla(QsciScintilla::SCI_SETINDICATORCURRENT, 10);
    m_leftEditor->SendScintilla(QsciScintilla::SCI_INDICATORCLEARRANGE, 0,
                                m_leftEditor->text().toUtf8().size());
    m_rightEditor->SendScintilla(QsciScintilla::SCI_SETINDICATORCURRENT, 11);
    m_rightEditor->SendScintilla(QsciScintilla::SCI_INDICATORCLEARRANGE, 0,
                                 m_rightEditor->text().toUtf8().size());

    const ComparePalette rowPal = comparePalette();
    const QFont marginFont = m_leftEditor->font();
    const QsciStyle lineNumberStyle(200, "compareLineNumber",
                                    rowPal.marginFg, rowPal.marginBg, marginFont);
    const QsciStyle blankLineNumberStyle(201, "compareBlankLineNumber",
                                         rowPal.marginFg, rowPal.marginBg, marginFont);

    for (int rowIndex = 0; rowIndex < rows.size(); ++rowIndex) {
        const CompareDisplayRow &row = rows[rowIndex];
        switch (row.kind) {
        case RowEqual:
        case RowChanged:
            m_leftEditor->setMarginText(rowIndex, QString::number(row.leftLineNumber), lineNumberStyle);
            m_rightEditor->setMarginText(rowIndex, QString::number(row.rightLineNumber), lineNumberStyle);
            break;
        case RowAdded:
            m_leftEditor->setMarginText(rowIndex, QString(), blankLineNumberStyle);
            m_rightEditor->setMarginText(rowIndex, QString::number(row.rightLineNumber), lineNumberStyle);
            break;
        case RowDeleted:
            m_leftEditor->setMarginText(rowIndex, QString::number(row.leftLineNumber), lineNumberStyle);
            m_rightEditor->setMarginText(rowIndex, QString(), blankLineNumberStyle);
            break;
        }
    }

    int leftBytePos = 0;
    int rightBytePos = 0;
    int rowIndex = 0;
    for (const CompareDisplayRow &row : rows) {
        QString leftLine;
        QString rightLine;

        switch (row.kind) {
        case RowEqual:
            leftLine = row.leftText + "\n";
            rightLine = row.rightText + "\n";
            break;

        case RowAdded:
            leftLine = "\n";
            rightLine = row.rightText + "\n";
            m_rightEditor->markerAdd(rowIndex, MARKER_ADDED);
            m_rightEditor->markerAdd(rowIndex, SYM_ADD);
            m_leftEditor->markerAdd(rowIndex, MARKER_BLANK);
            if (!row.rightText.isEmpty()) {
                const QByteArray bytes = rightLine.toUtf8();
                const int textStart = rightBytePos;
                const int textLen = bytes.size() - 1;
                if (textLen > 0) {
                    m_rightEditor->SendScintilla(QsciScintilla::SCI_SETINDICATORCURRENT, 11);
                    m_rightEditor->SendScintilla(QsciScintilla::SCI_INDICATORFILLRANGE, textStart, textLen);
                }
            }
            break;

        case RowDeleted:
            leftLine = row.leftText + "\n";
            rightLine = "\n";
            m_leftEditor->markerAdd(rowIndex, MARKER_DELETED);
            m_leftEditor->markerAdd(rowIndex, SYM_DEL);
            m_rightEditor->markerAdd(rowIndex, MARKER_BLANK);
            if (!row.leftText.isEmpty()) {
                const QByteArray bytes = leftLine.toUtf8();
                const int textStart = leftBytePos;
                const int textLen = bytes.size() - 1;
                if (textLen > 0) {
                    m_leftEditor->SendScintilla(QsciScintilla::SCI_SETINDICATORCURRENT, 10);
                    m_leftEditor->SendScintilla(QsciScintilla::SCI_INDICATORFILLRANGE, textStart, textLen);
                }
            }
            break;

        case RowChanged: {
            leftLine = row.leftText + "\n";
            rightLine = row.rightText + "\n";
            m_leftEditor->markerAdd(rowIndex, MARKER_CHANGED_LEFT);
            m_leftEditor->markerAdd(rowIndex, SYM_CHANGE_LEFT);
            m_rightEditor->markerAdd(rowIndex, MARKER_CHANGED_RIGHT);
            m_rightEditor->markerAdd(rowIndex, SYM_CHANGE_RIGHT);

            // Word-level LCS diff: tokenize, compute diff, highlight the
            // specific tokens that were added/removed. Falls back to full-
            // line highlight if the lines are empty (nothing to tokenize).
            const QString &leftText  = row.leftText;
            const QString &rightText = row.rightText;

            const QVector<InlineToken> leftToks  = tokenizeLine(leftText);
            const QVector<InlineToken> rightToks = tokenizeLine(rightText);
            QVector<bool> leftChanged, rightChanged;
            computeTokenDiff(leftToks, rightToks, leftChanged, rightChanged);

            auto utf8Bytes = [](const QString &text, int chars) -> int {
                return text.left(chars).toUtf8().size();
            };

            // Paint LEFT side — removed tokens in red
            m_leftEditor->SendScintilla(QsciScintilla::SCI_SETINDICATORCURRENT, 10);
            for (int t = 0; t < leftToks.size(); ++t) {
                if (!leftChanged[t]) continue;
                const InlineToken &tok = leftToks[t];
                // Skip pure-whitespace tokens so we only highlight the
                // meaningful changed word, not the leading/trailing space.
                if (tok.text.trimmed().isEmpty()) continue;
                const int byteStart = leftBytePos + utf8Bytes(leftText, tok.start);
                const int byteLen   = utf8Bytes(leftText, tok.start + tok.length) -
                                      utf8Bytes(leftText, tok.start);
                if (byteLen > 0)
                    m_leftEditor->SendScintilla(QsciScintilla::SCI_INDICATORFILLRANGE,
                                                byteStart, byteLen);
            }

            // Paint RIGHT side — added tokens in green
            m_rightEditor->SendScintilla(QsciScintilla::SCI_SETINDICATORCURRENT, 11);
            for (int t = 0; t < rightToks.size(); ++t) {
                if (!rightChanged[t]) continue;
                const InlineToken &tok = rightToks[t];
                if (tok.text.trimmed().isEmpty()) continue;
                const int byteStart = rightBytePos + utf8Bytes(rightText, tok.start);
                const int byteLen   = utf8Bytes(rightText, tok.start + tok.length) -
                                      utf8Bytes(rightText, tok.start);
                if (byteLen > 0)
                    m_rightEditor->SendScintilla(QsciScintilla::SCI_INDICATORFILLRANGE,
                                                 byteStart, byteLen);
            }
            break;
        }
        }

        leftBytePos += leftLine.toUtf8().size();
        rightBytePos += rightLine.toUtf8().size();
        ++rowIndex;
    }

    m_navBar->setRows(m_rowKinds);
    updateOverviewViewport();

    m_statsLabel->setText(QString("+%1 added   -%2 removed   %3 diffs   %4 lines")
                              .arg(diff.added)
                              .arg(diff.removed)
                              .arg(m_diffLines.size())
                              .arg(rows.size()));
    setEditorsEditable(m_editable);
}

void CompareWidget::jumpToRow(int row) {
    if (m_rowKinds.isEmpty()) return;

    row = qBound(0, row, m_rowKinds.size() - 1);
    m_leftEditor->setFirstVisibleLine(row);
    m_rightEditor->setFirstVisibleLine(row);
    m_leftEditor->setCursorPosition(row, 0);
    m_rightEditor->setCursorPosition(row, 0);
    updateOverviewViewport();
}

void CompareWidget::updateOverviewViewport() {
    if (!m_navBar || m_rowKinds.isEmpty()) {
        if (m_navBar) m_navBar->setViewport(0, 0);
        return;
    }

    const int firstVisible = qBound(0, m_leftEditor->firstVisibleLine(), m_rowKinds.size() - 1);
    const int visibleRows = qMax(1, static_cast<int>(
        m_leftEditor->SendScintilla(QsciScintillaBase::SCI_LINESONSCREEN)));
    m_navBar->setViewport(firstVisible, visibleRows);
}

void CompareWidget::navigateNext() {
    if (m_diffLines.isEmpty()) return;

    m_currentDiff = (m_currentDiff + 1) % m_diffLines.size();
    jumpToRow(m_diffLines[m_currentDiff]);
    m_statsLabel->setText(m_statsLabel->text().split("|").first().trimmed() +
                          QString("   |   Diff %1/%2")
                              .arg(m_currentDiff + 1)
                              .arg(m_diffLines.size()));
}

void CompareWidget::navigatePrev() {
    if (m_diffLines.isEmpty()) return;

    m_currentDiff = (m_currentDiff - 1 + m_diffLines.size()) % m_diffLines.size();
    jumpToRow(m_diffLines[m_currentDiff]);
    m_statsLabel->setText(m_statsLabel->text().split("|").first().trimmed() +
                          QString("   |   Diff %1/%2")
                              .arg(m_currentDiff + 1)
                              .arg(m_diffLines.size()));
}

CompareDialog::CompareDialog(const QString &leftText, const QString &leftName,
                             const QString &rightText, const QString &rightName, QWidget *parent)
    : QWidget(parent) {
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    auto *widget = new CompareWidget;
    layout->addWidget(widget);
    widget->compare(leftText, leftName, rightText, rightName);
}
