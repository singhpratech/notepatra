// Notepatra Compare panel — inspired by ComparePlus by Pavel Nedev.
//
// Original ComparePlus repo: https://github.com/pnedev/comparePlus
// Pavel's plugin is the gold-standard diff for Notepad++. The visual UX
// in this file (colored line markers for added/deleted/changed, side-by-
// side Scintilla editors with synced scrolling, prev/next diff navigation)
// borrows directly from his design. The implementation here is a fresh
// Qt + Rust port — Notepatra is a different codebase — but Pavel and the
// ComparePlus contributors deserve full credit for the UX patterns.

#include "compare.h"
#include "rustbridge.h"
#include "npp_palette.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QSplitter>
#include <QFont>
#include <QScrollBar>
#include <QFileInfo>

#include <Qsci/qscilexerjson.h>
#include <Qsci/qscilexerjavascript.h>
#include <Qsci/qscilexercpp.h>
#include <Qsci/qscilexerhtml.h>
#include <Qsci/qscilexersql.h>
#include <Qsci/qscilexerpython.h>
#include <Qsci/qscilexercss.h>
#include <Qsci/qscilexerxml.h>
#include <Qsci/qscilexeryaml.h>
#include <Qsci/qscilexermarkdown.h>
#include <Qsci/qscilexerbash.h>

// Pick a QScintilla lexer based on a filename's extension. Used by the
// compare view to apply syntax highlighting to both halves so the user
// sees JSON keys, SQL keywords, etc. — not just plain dark text.
static QsciLexer *lexerForName(const QString &name, QObject *parent) {
    QFileInfo fi(name);
    QString ext = fi.suffix().toLower();
    if (ext == "json" || ext == "jsonl") return new QsciLexerJSON(parent);
    if (ext == "js" || ext == "jsx" || ext == "ts" || ext == "tsx" || ext == "mjs")
        return new QsciLexerJavaScript(parent);
    if (ext == "cpp" || ext == "cc" || ext == "cxx" || ext == "h" || ext == "hpp" ||
        ext == "c" || ext == "hxx") return new QsciLexerCPP(parent);
    if (ext == "py" || ext == "pyw") return new QsciLexerPython(parent);
    if (ext == "html" || ext == "htm" || ext == "xhtml") return new QsciLexerHTML(parent);
    if (ext == "css" || ext == "scss" || ext == "less") return new QsciLexerCSS(parent);
    if (ext == "xml" || ext == "svg" || ext == "xsd") return new QsciLexerXML(parent);
    if (ext == "yaml" || ext == "yml") return new QsciLexerYAML(parent);
    if (ext == "md" || ext == "markdown") return new QsciLexerMarkdown(parent);
    if (ext == "sql") return new QsciLexerSQL(parent);
    if (ext == "sh" || ext == "bash") return new QsciLexerBash(parent);
    return nullptr;
}

// Marker numbers for diff highlighting
#define MARKER_ADDED   4   // green line bg
#define MARKER_DELETED 5   // red line bg
#define MARKER_CHANGED 6   // pale yellow line bg
#define MARKER_BLANK   7   // light blue placeholder line bg

// Symbol margin markers — small icons in the symbol margin (margin 1)
// that match the per-row state. Like ComparePlus's "~" and "+" indicators.
#define SYM_MOD     8     // pink ~ for modified
#define SYM_ADD     9     // green + for added
#define SYM_DEL     10    // red − for deleted

void CompareWidget::setupEditor(QsciScintilla *ed) {
    QFont mono("Consolas", 11);
    mono.setStyleHint(QFont::Monospace);
    ed->setFont(mono);
    ed->setMarginsFont(mono);
    ed->setReadOnly(true);
    ed->setUtf8(true);
    // Reset zoom in case a previous session inherited a stale negative zoom
    ed->zoomTo(0);

    // Line numbers — TEXT margin so we can set custom per-line text via
    // setMarginText() instead of QScintilla's automatic row index. Lets
    // us show ORIGINAL source-file line numbers and a `+` placeholder
    // on the side that doesn't have a line for a given paired row.
    ed->setMarginType(0, QsciScintilla::TextMargin);
    ed->setMarginWidth(0, "00000");
    ed->setMarginsBackgroundColor(QColor("#F8F8F8"));
    ed->setMarginsForegroundColor(QColor("#A0A0A0"));

    // Diff marker symbol margin (margin 1) — wide enough to show small
    // icons (~, +, −) per row indicating the change kind.
    ed->setMarginType(1, QsciScintilla::SymbolMargin);
    ed->setMarginWidth(1, 18);
    // Bitmask of allowed marker types in margin 1
    ed->setMarginMarkerMask(1, (1 << SYM_MOD) | (1 << SYM_ADD) | (1 << SYM_DEL));

    // No folding
    ed->setFolding(QsciScintilla::NoFoldStyle);

    // Full-line background markers — colors picked to match ComparePlus.
    //
    //   ADDED   → soft mint green   (#D4F4D4) — line missing on left
    //   DELETED → soft salmon       (#F4D4D4) — line missing on right
    //   CHANGED → very pale cream   (#FFFBE6) — both sides differ. Subtle
    //             so the char-level red/green indicators on top stand out.
    //   BLANK   → very light blue   (#E8F0F8) — placeholder showing "the
    //             other side has content here"
    ed->markerDefine(QsciScintilla::Background, MARKER_ADDED);
    ed->setMarkerBackgroundColor(QColor("#D4F4D4"), MARKER_ADDED);

    ed->markerDefine(QsciScintilla::Background, MARKER_DELETED);
    ed->setMarkerBackgroundColor(QColor("#F4D4D4"), MARKER_DELETED);

    ed->markerDefine(QsciScintilla::Background, MARKER_CHANGED);
    ed->setMarkerBackgroundColor(QColor("#FFFBE6"), MARKER_CHANGED);

    ed->markerDefine(QsciScintilla::Background, MARKER_BLANK);
    ed->setMarkerBackgroundColor(QColor("#E8F0F8"), MARKER_BLANK);

    // Symbol margin markers (margin 1) — small icons next to the line number
    // showing the change kind. Pink ~ for modified, green + for added, red −
    // for deleted. Matches ComparePlus's per-row indicator stripe.
    ed->markerDefine(QsciScintilla::Circle, SYM_MOD);
    ed->setMarkerForegroundColor(QColor("#D88888"), SYM_MOD);
    ed->setMarkerBackgroundColor(QColor("#FFE0E0"), SYM_MOD);

    ed->markerDefine(QsciScintilla::Plus, SYM_ADD);
    ed->setMarkerForegroundColor(QColor("#4CAF50"), SYM_ADD);
    ed->setMarkerBackgroundColor(QColor("#D4F4D4"), SYM_ADD);

    ed->markerDefine(QsciScintilla::Minus, SYM_DEL);
    ed->setMarkerForegroundColor(QColor("#E53935"), SYM_DEL);
    ed->setMarkerBackgroundColor(QColor("#F4D4D4"), SYM_DEL);

    // Margin marker colors (small colored bar on left)
    ed->setMarkerForegroundColor(QColor("#4CAF50"), MARKER_ADDED);
    ed->setMarkerForegroundColor(QColor("#F44336"), MARKER_DELETED);
    ed->setMarkerForegroundColor(QColor("#FFC107"), MARKER_CHANGED);

    // Soft default text color — dark gray instead of pure black so the
    // unchanged context lines feel "soothing" and the colored diff
    // markers stand out more.
    ed->setPaper(QColor("#FFFFFF"));
    ed->setColor(QColor("#404040"));
    ed->setCaretLineVisible(false);
}

CompareWidget::CompareWidget(QWidget *parent) : QWidget(parent) {
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);

    // Toolbar
    auto *toolbar = new QHBoxLayout;
    toolbar->setContentsMargins(6, 4, 6, 4);

    auto *prevBtn = new QPushButton("< Prev");
    prevBtn->setFixedSize(70, 26);
    auto *nextBtn = new QPushButton("Next >");
    nextBtn->setFixedSize(70, 26);
    auto *recompBtn = new QPushButton("Recompare");
    recompBtn->setFixedSize(90, 26);

    m_ignoreWhitespace = new QCheckBox("Ignore spaces");
    m_ignoreCase = new QCheckBox("Ignore case");
    m_ignoreEmptyLines = new QCheckBox("Ignore empty lines");

    m_statsLabel = new QLabel;
    m_statsLabel->setStyleSheet("font-weight: bold; color: #333;");

    toolbar->addWidget(prevBtn);
    toolbar->addWidget(nextBtn);
    toolbar->addWidget(recompBtn);
    toolbar->addSpacing(16);
    toolbar->addWidget(m_ignoreWhitespace);
    toolbar->addWidget(m_ignoreCase);
    toolbar->addWidget(m_ignoreEmptyLines);
    toolbar->addStretch();
    toolbar->addWidget(m_statsLabel);
    layout->addLayout(toolbar);

    // File headers
    auto *headerRow = new QHBoxLayout;
    headerRow->setContentsMargins(0, 0, 0, 0);
    headerRow->setSpacing(2);
    m_leftHeader = new QLabel("  Left file");
    m_leftHeader->setFixedHeight(20);
    m_leftHeader->setStyleSheet("font-weight: bold; background: #FFCCCC; color: #990000; padding: 1px 8px;");
    m_rightHeader = new QLabel("  Right file");
    m_rightHeader->setFixedHeight(20);
    m_rightHeader->setStyleSheet("font-weight: bold; background: #CCFFCC; color: #006600; padding: 1px 8px;");
    headerRow->addWidget(m_leftHeader, 1);
    headerRow->addWidget(m_rightHeader, 1);
    layout->addLayout(headerRow);

    // Two real Scintilla editors
    auto *splitter = new QSplitter(Qt::Horizontal);

    m_leftEditor = new QsciScintilla;
    setupEditor(m_leftEditor);
    splitter->addWidget(m_leftEditor);

    m_rightEditor = new QsciScintilla;
    setupEditor(m_rightEditor);
    splitter->addWidget(m_rightEditor);

    layout->addWidget(splitter, 1);

    // Sync scrolling — vertical AND horizontal. Both scrollbars on one
    // side mirror to the other so the user always sees the same line +
    // column on both halves of the compare view (like ComparePlus).
    //
    // Guard against feedback loops: setValue(val) re-fires valueChanged,
    // which would call setValue back on the original — Qt's QScrollBar
    // detects val == current and skips the signal, so this is naturally
    // safe without an explicit blocker flag.
    connect(m_leftEditor->verticalScrollBar(), &QScrollBar::valueChanged, this, [this](int val) {
        m_rightEditor->verticalScrollBar()->setValue(val);
    });
    connect(m_rightEditor->verticalScrollBar(), &QScrollBar::valueChanged, this, [this](int val) {
        m_leftEditor->verticalScrollBar()->setValue(val);
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
    connect(m_ignoreWhitespace, &QCheckBox::toggled, this, [this]() { recompare(); });
    connect(m_ignoreCase, &QCheckBox::toggled, this, [this]() { recompare(); });
    connect(m_ignoreEmptyLines, &QCheckBox::toggled, this, [this]() { recompare(); });
}

void CompareWidget::compare(const QString &leftText, const QString &leftName,
                             const QString &rightText, const QString &rightName) {
    m_leftText = leftText;
    m_rightText = rightText;
    m_leftHeader->setText("  " + leftName);
    m_rightHeader->setText("  " + rightName);

    // NO SYNTAX HIGHLIGHTING in the compare view. Per the user spec:
    // "all the lines are grayed out, no highlighting nothing". Equal/
    // context lines should be plain dim gray text so the diff markers
    // (yellow modified bg, character-level red/green, full green/red
    // for adds/deletes) are the ONLY thing that draws the eye.
    m_leftEditor->setLexer(nullptr);
    m_rightEditor->setLexer(nullptr);

    QFont mono("Consolas", 11);
    mono.setStyleHint(QFont::Monospace);
    m_leftEditor->setFont(mono);
    m_rightEditor->setFont(mono);

    // Soft mid-gray text for everything — dim enough that any colored
    // diff marker on top stands out, dark enough to still be readable.
    m_leftEditor->setColor(QColor("#606060"));
    m_rightEditor->setColor(QColor("#606060"));
    m_leftEditor->setPaper(QColor("#FFFFFF"));
    m_rightEditor->setPaper(QColor("#FFFFFF"));

    // Re-apply soft margin colors (lexer reset above doesn't touch them
    // but make sure the style is consistent)
    m_leftEditor->setMarginsBackgroundColor(QColor("#F8F8F8"));
    m_leftEditor->setMarginsForegroundColor(QColor("#A0A0A0"));
    m_rightEditor->setMarginsBackgroundColor(QColor("#F8F8F8"));
    m_rightEditor->setMarginsForegroundColor(QColor("#A0A0A0"));

    recompare();
}

void CompareWidget::recompare() {
    QString left = m_leftText;
    QString right = m_rightText;

    if (m_ignoreCase->isChecked()) { left = left.toLower(); right = right.toLower(); }

    auto diff = RustCore::computeDiff(left, right);

    // ─── Pair consecutive Delete + Add blocks into "modified" rows ──────
    // Without pairing: 5 deleted lines + 5 added lines = 10 visual rows
    // (left lines 1-5 deleted with blank right, right lines 6-10 added
    // with blank left). With pairing: 5 visual rows where line N has the
    // deleted text on the LEFT and the added text on the RIGHT side by
    // side. This is what ComparePlus / Notepad++ Compare does.
    //
    // Rebuild the diff entries by walking the original list and merging
    // consecutive runs of (Delete...Delete Add...Add) into a sequence of
    // paired "modified" rows + remainder.
    struct Row {
        int kind;            // 0=equal, 1=add-only, 2=delete-only, 3=modified
        QString leftText;    // text shown on the left side
        QString rightText;   // text shown on the right side
    };
    QList<Row> rows;

    int i = 0;
    while (i < diff.entries.size()) {
        const auto &e = diff.entries[i];
        if (e.tag == 0) {
            // Equal: same text both sides
            rows.append({0, e.text, e.text});
            i++;
            continue;
        }
        // Collect consecutive deletes
        QStringList dels;
        while (i < diff.entries.size() && diff.entries[i].tag == 2) {
            dels << diff.entries[i].text;
            i++;
        }
        // Collect consecutive adds (immediately following the delete block)
        QStringList adds;
        while (i < diff.entries.size() && diff.entries[i].tag == 1) {
            adds << diff.entries[i].text;
            i++;
        }
        // Pair as many delete↔add rows as possible
        int n = qMin(dels.size(), adds.size());
        for (int j = 0; j < n; j++) {
            rows.append({3, dels[j], adds[j]});
        }
        // Remaining deletes (no add to pair with) → delete-only rows
        for (int j = n; j < dels.size(); j++) {
            rows.append({2, dels[j], QString()});
        }
        // Remaining adds (no delete to pair with) → add-only rows
        for (int j = n; j < adds.size(); j++) {
            rows.append({1, QString(), adds[j]});
        }
    }

    // ─── Render rows into left/right buffers and collect diff line numbers ─
    QString leftBuf, rightBuf;
    m_diffLines.clear();
    m_currentDiff = -1;

    int line = 0;
    for (const Row &r : rows) {
        switch (r.kind) {
        case 0: // equal — no prefix, no marker
            leftBuf  += "  " + r.leftText  + "\n";
            rightBuf += "  " + r.rightText + "\n";
            break;
        case 1: // add only — blank left, "+ " right
            leftBuf  += "\n";
            rightBuf += "+ " + r.rightText + "\n";
            m_diffLines.append(line);
            break;
        case 2: // delete only — "- " left, blank right
            leftBuf  += "- " + r.leftText + "\n";
            rightBuf += "\n";
            m_diffLines.append(line);
            break;
        case 3: // modified — "- " left + "+ " right on the SAME line
            leftBuf  += "- " + r.leftText  + "\n";
            rightBuf += "+ " + r.rightText + "\n";
            m_diffLines.append(line);
            break;
        }
        line++;
    }

    // Push to editors
    m_leftEditor->setReadOnly(false);
    m_leftEditor->setText(leftBuf);
    m_leftEditor->setReadOnly(true);

    m_rightEditor->setReadOnly(false);
    m_rightEditor->setText(rightBuf);
    m_rightEditor->setReadOnly(true);

    // Setup indicators for word-level highlighting
    m_leftEditor->SendScintilla(QsciScintilla::SCI_INDICSETSTYLE, 10, QsciScintilla::INDIC_FULLBOX);
    m_leftEditor->SendScintilla(QsciScintilla::SCI_INDICSETFORE, 10, QColor("#D32F2F").rgb() & 0xFFFFFF);
    m_leftEditor->SendScintilla(QsciScintilla::SCI_INDICSETALPHA, 10, 80);
    m_leftEditor->SendScintilla(QsciScintilla::SCI_INDICSETOUTLINEALPHA, 10, 200);

    m_rightEditor->SendScintilla(QsciScintilla::SCI_INDICSETSTYLE, 11, QsciScintilla::INDIC_FULLBOX);
    m_rightEditor->SendScintilla(QsciScintilla::SCI_INDICSETFORE, 11, QColor("#2E7D32").rgb() & 0xFFFFFF);
    m_rightEditor->SendScintilla(QsciScintilla::SCI_INDICSETALPHA, 11, 80);
    m_rightEditor->SendScintilla(QsciScintilla::SCI_INDICSETOUTLINEALPHA, 11, 200);

    m_leftEditor->markerDeleteAll(MARKER_ADDED);
    m_leftEditor->markerDeleteAll(MARKER_DELETED);
    m_leftEditor->markerDeleteAll(MARKER_CHANGED);
    m_leftEditor->markerDeleteAll(MARKER_BLANK);
    m_leftEditor->markerDeleteAll(SYM_MOD);
    m_leftEditor->markerDeleteAll(SYM_ADD);
    m_leftEditor->markerDeleteAll(SYM_DEL);
    m_rightEditor->markerDeleteAll(MARKER_ADDED);
    m_rightEditor->markerDeleteAll(MARKER_DELETED);
    m_rightEditor->markerDeleteAll(MARKER_CHANGED);
    m_rightEditor->markerDeleteAll(MARKER_BLANK);
    m_rightEditor->markerDeleteAll(SYM_MOD);
    m_rightEditor->markerDeleteAll(SYM_ADD);
    m_rightEditor->markerDeleteAll(SYM_DEL);

    m_leftEditor->SendScintilla(QsciScintilla::SCI_SETINDICATORCURRENT, 10);
    m_leftEditor->SendScintilla(QsciScintilla::SCI_INDICATORCLEARRANGE, 0,
                                 m_leftEditor->text().toUtf8().size());
    m_rightEditor->SendScintilla(QsciScintilla::SCI_SETINDICATORCURRENT, 11);
    m_rightEditor->SendScintilla(QsciScintilla::SCI_INDICATORCLEARRANGE, 0,
                                  m_rightEditor->text().toUtf8().size());

    // ─── Apply custom per-row line numbers in the text margin ──────────
    // Each row gets:
    //   - LEFT margin: original left-source line number, or "+" if the row
    //     is a placeholder (line exists on right but not left)
    //   - RIGHT margin: original right-source line number, or "+" if the
    //     row is a placeholder (line exists on left but not right)
    // The "+" placeholder text is shown in green to match ComparePlus.
    int leftSourceLineNum = 1;   // tracks position in the ORIGINAL left text
    int rightSourceLineNum = 1;  // tracks position in the ORIGINAL right text

    // Margin text styles: style 0 = soft gray (numbers), style 1 = green ("+")
    for (auto *ed : {m_leftEditor, m_rightEditor}) {
        ed->SendScintilla(QsciScintilla::SCI_STYLESETFORE, 0,
                          QColor("#A0A0A0").rgb() & 0xFFFFFF);
        ed->SendScintilla(QsciScintilla::SCI_STYLESETBACK, 0,
                          QColor("#F8F8F8").rgb() & 0xFFFFFF);
        ed->SendScintilla(QsciScintilla::SCI_STYLESETFORE, 1,
                          QColor("#2E7D32").rgb() & 0xFFFFFF);
        ed->SendScintilla(QsciScintilla::SCI_STYLESETBACK, 1,
                          QColor("#E8F5E9").rgb() & 0xFFFFFF);
        ed->SendScintilla(QsciScintilla::SCI_STYLESETBOLD, 1, 1);
    }

    for (int rIdx = 0; rIdx < rows.size(); ++rIdx) {
        const Row &r = rows[rIdx];
        switch (r.kind) {
        case 0: // equal — both sides advance, both show their numbers
            m_leftEditor->setMarginText(rIdx,
                QString::number(leftSourceLineNum), 0);
            m_rightEditor->setMarginText(rIdx,
                QString::number(rightSourceLineNum), 0);
            leftSourceLineNum++;
            rightSourceLineNum++;
            break;
        case 1: // add-only — left has no source line; right advances
            m_leftEditor->setMarginText(rIdx, "+", 1);
            m_rightEditor->setMarginText(rIdx,
                QString::number(rightSourceLineNum), 0);
            rightSourceLineNum++;
            break;
        case 2: // delete-only — left advances; right has no source line
            m_leftEditor->setMarginText(rIdx,
                QString::number(leftSourceLineNum), 0);
            m_rightEditor->setMarginText(rIdx, "+", 1);
            leftSourceLineNum++;
            break;
        case 3: // modified — both sides have a source line, both advance
            m_leftEditor->setMarginText(rIdx,
                QString::number(leftSourceLineNum), 0);
            m_rightEditor->setMarginText(rIdx,
                QString::number(rightSourceLineNum), 0);
            leftSourceLineNum++;
            rightSourceLineNum++;
            break;
        }
    }

    // Apply line markers + word-level indicators row by row
    int leftBytePos = 0, rightBytePos = 0;
    int rowIdx = 0;
    for (const Row &r : rows) {
        QString leftLine, rightLine;
        switch (r.kind) {
        case 0:
            leftLine  = "  " + r.leftText  + "\n";
            rightLine = "  " + r.rightText + "\n";
            break;
        case 1:
            leftLine  = "\n";
            rightLine = "+ " + r.rightText + "\n";
            m_rightEditor->markerAdd(rowIdx, MARKER_ADDED);
            m_rightEditor->markerAdd(rowIdx, SYM_ADD);
            m_leftEditor->markerAdd(rowIdx, MARKER_BLANK);
            m_leftEditor->markerAdd(rowIdx, SYM_ADD);
            {
                QByteArray rb = rightLine.toUtf8();
                int textStart = rightBytePos + 2;
                int textLen = rb.size() - 3;
                if (textLen > 0) {
                    m_rightEditor->SendScintilla(QsciScintilla::SCI_SETINDICATORCURRENT, 11);
                    m_rightEditor->SendScintilla(QsciScintilla::SCI_INDICATORFILLRANGE, textStart, textLen);
                }
            }
            break;
        case 2:
            leftLine  = "- " + r.leftText + "\n";
            rightLine = "\n";
            m_leftEditor->markerAdd(rowIdx, MARKER_DELETED);
            m_leftEditor->markerAdd(rowIdx, SYM_DEL);
            m_rightEditor->markerAdd(rowIdx, MARKER_BLANK);
            m_rightEditor->markerAdd(rowIdx, SYM_DEL);
            {
                QByteArray lb = leftLine.toUtf8();
                int textStart = leftBytePos + 2;
                int textLen = lb.size() - 3;
                if (textLen > 0) {
                    m_leftEditor->SendScintilla(QsciScintilla::SCI_SETINDICATORCURRENT, 10);
                    m_leftEditor->SendScintilla(QsciScintilla::SCI_INDICATORFILLRANGE, textStart, textLen);
                }
            }
            break;
        case 3: // modified — SUBTLE yellow line bg + character-level indicators
            leftLine  = "  " + r.leftText  + "\n";
            rightLine = "  " + r.rightText + "\n";
            m_leftEditor->markerAdd(rowIdx, MARKER_CHANGED);
            m_leftEditor->markerAdd(rowIdx, SYM_MOD);
            m_rightEditor->markerAdd(rowIdx, MARKER_CHANGED);
            m_rightEditor->markerAdd(rowIdx, SYM_MOD);
            {
                // ─── Character-level diff via common-prefix + common-suffix ─
                // The changed portion is a single contiguous region between
                // the matching prefix and matching suffix. Highlight ONLY
                // those bytes — not the whole line.
                QString L = r.leftText;
                QString R = r.rightText;
                int prefixLen = 0;
                int maxPrefix = qMin(L.length(), R.length());
                while (prefixLen < maxPrefix && L[prefixLen] == R[prefixLen]) prefixLen++;

                int suffixLen = 0;
                int maxSuffix = qMin(L.length() - prefixLen, R.length() - prefixLen);
                while (suffixLen < maxSuffix &&
                       L[L.length() - 1 - suffixLen] == R[R.length() - 1 - suffixLen]) suffixLen++;

                // Buffer line layout: "  " + text + "\n" → text starts at +2.
                auto utf8Bytes = [](const QString &s, int chars) -> int {
                    return s.left(chars).toUtf8().size();
                };

                // LEFT side highlight (red, indicator 10)
                int lChangedChars = L.length() - prefixLen - suffixLen;
                if (lChangedChars > 0) {
                    int lByteStart = leftBytePos + 2 + utf8Bytes(L, prefixLen);
                    int lByteLen   = utf8Bytes(L, prefixLen + lChangedChars) -
                                     utf8Bytes(L, prefixLen);
                    m_leftEditor->SendScintilla(QsciScintilla::SCI_SETINDICATORCURRENT, 10);
                    m_leftEditor->SendScintilla(QsciScintilla::SCI_INDICATORFILLRANGE,
                                                lByteStart, lByteLen);
                }

                // RIGHT side highlight (green, indicator 11)
                int rChangedChars = R.length() - prefixLen - suffixLen;
                if (rChangedChars > 0) {
                    int rByteStart = rightBytePos + 2 + utf8Bytes(R, prefixLen);
                    int rByteLen   = utf8Bytes(R, prefixLen + rChangedChars) -
                                     utf8Bytes(R, prefixLen);
                    m_rightEditor->SendScintilla(QsciScintilla::SCI_SETINDICATORCURRENT, 11);
                    m_rightEditor->SendScintilla(QsciScintilla::SCI_INDICATORFILLRANGE,
                                                 rByteStart, rByteLen);
                }
            }
            break;
        }
        leftBytePos += leftLine.toUtf8().size();
        rightBytePos += rightLine.toUtf8().size();
        rowIdx++;
    }

    m_statsLabel->setText(QString("+%1 added   -%2 removed   %3 diffs   %4 lines")
                          .arg(diff.added).arg(diff.removed)
                          .arg(m_diffLines.size()).arg(rows.size()));
}

void CompareWidget::navigateNext() {
    if (m_diffLines.isEmpty()) return;
    m_currentDiff = (m_currentDiff + 1) % m_diffLines.size();
    int line = m_diffLines[m_currentDiff];
    m_leftEditor->ensureLineVisible(line);
    m_leftEditor->setCursorPosition(line, 0);
    m_statsLabel->setText(m_statsLabel->text().split("|").first().trimmed() +
                          QString("   |   Diff %1/%2").arg(m_currentDiff + 1).arg(m_diffLines.size()));
}

void CompareWidget::navigatePrev() {
    if (m_diffLines.isEmpty()) return;
    m_currentDiff = (m_currentDiff - 1 + m_diffLines.size()) % m_diffLines.size();
    int line = m_diffLines[m_currentDiff];
    m_leftEditor->ensureLineVisible(line);
    m_leftEditor->setCursorPosition(line, 0);
    m_statsLabel->setText(m_statsLabel->text().split("|").first().trimmed() +
                          QString("   |   Diff %1/%2").arg(m_currentDiff + 1).arg(m_diffLines.size()));
}

CompareDialog::CompareDialog(const QString &l, const QString &ln,
                             const QString &r, const QString &rn, QWidget *p)
    : QWidget(p) {
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    auto *w = new CompareWidget;
    layout->addWidget(w);
    w->compare(l, ln, r, rn);
}
