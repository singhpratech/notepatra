#include "compare.h"
#include "rustbridge.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QSplitter>
#include <QFont>
#include <QScrollBar>

// Marker numbers for diff highlighting
#define MARKER_ADDED   4   // green
#define MARKER_DELETED 5   // red
#define MARKER_CHANGED 6   // yellow
#define MARKER_BLANK   7   // gray placeholder

void CompareWidget::setupEditor(QsciScintilla *ed) {
    QFont mono("Consolas", 10);
    mono.setStyleHint(QFont::Monospace);
    ed->setFont(mono);
    ed->setMarginsFont(mono);
    ed->setReadOnly(true);
    ed->setUtf8(true);

    // Line numbers
    ed->setMarginType(0, QsciScintilla::NumberMargin);
    ed->setMarginWidth(0, "00000");
    ed->setMarginLineNumbers(0, true);

    // Diff marker margin
    ed->setMarginType(1, QsciScintilla::SymbolMargin);
    ed->setMarginWidth(1, 4);

    // No folding
    ed->setFolding(QsciScintilla::NoFoldStyle);

    // Define markers with colored backgrounds (full line)
    ed->markerDefine(QsciScintilla::Background, MARKER_ADDED);
    ed->setMarkerBackgroundColor(QColor("#CCFFCC"), MARKER_ADDED);   // green

    ed->markerDefine(QsciScintilla::Background, MARKER_DELETED);
    ed->setMarkerBackgroundColor(QColor("#FFCCCC"), MARKER_DELETED); // red

    ed->markerDefine(QsciScintilla::Background, MARKER_CHANGED);
    ed->setMarkerBackgroundColor(QColor("#FFFFCC"), MARKER_CHANGED); // yellow

    ed->markerDefine(QsciScintilla::Background, MARKER_BLANK);
    ed->setMarkerBackgroundColor(QColor("#F0F0F0"), MARKER_BLANK);   // gray

    // Margin marker colors (small colored bar on left)
    ed->setMarkerForegroundColor(QColor("#4CAF50"), MARKER_ADDED);
    ed->setMarkerForegroundColor(QColor("#F44336"), MARKER_DELETED);
    ed->setMarkerForegroundColor(QColor("#FFC107"), MARKER_CHANGED);

    // Colors
    ed->setPaper(QColor("#FFFFFF"));
    ed->setColor(QColor("#000000"));
    ed->setMarginsBackgroundColor(QColor("#E4E4E4"));
    ed->setMarginsForegroundColor(QColor("#2B91AF"));
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

    // Sync scrolling — both vertical and horizontal
    connect(m_leftEditor->verticalScrollBar(), &QScrollBar::valueChanged, this, [this](int val) {
        m_rightEditor->verticalScrollBar()->setValue(val);
    });
    connect(m_rightEditor->verticalScrollBar(), &QScrollBar::valueChanged, this, [this](int val) {
        m_leftEditor->verticalScrollBar()->setValue(val);
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
    recompare();
}

void CompareWidget::recompare() {
    QString left = m_leftText;
    QString right = m_rightText;

    if (m_ignoreCase->isChecked()) { left = left.toLower(); right = right.toLower(); }

    auto diff = RustCore::computeDiff(left, right);

    // Build left and right text with +/- prefix
    QString leftBuf, rightBuf;
    m_diffLines.clear();
    m_currentDiff = -1;

    int lineNum = 0;
    for (const auto &entry : diff.entries) {
        bool skip = m_ignoreEmptyLines->isChecked() && entry.text.trimmed().isEmpty();

        switch (entry.tag) {
        case 0: // Equal — no prefix
            leftBuf += "  " + entry.text + "\n";
            rightBuf += "  " + entry.text + "\n";
            break;
        case 1: // Added (right only) — + prefix, green
            leftBuf += "\n";
            rightBuf += "+ " + entry.text + "\n";
            if (!skip) m_diffLines.append(lineNum);
            break;
        case 2: // Deleted (left only) — - prefix, red
            leftBuf += "- " + entry.text + "\n";
            rightBuf += "\n";
            if (!skip) m_diffLines.append(lineNum);
            break;
        }
        lineNum++;
    }

    // Set text
    m_leftEditor->setReadOnly(false);
    m_leftEditor->setText(leftBuf);
    m_leftEditor->setReadOnly(true);

    m_rightEditor->setReadOnly(false);
    m_rightEditor->setText(rightBuf);
    m_rightEditor->setReadOnly(true);

    // Setup indicators for word-level highlighting
    // Indicator 10 = deleted word (red)
    m_leftEditor->SendScintilla(QsciScintilla::SCI_INDICSETSTYLE, 10, QsciScintilla::INDIC_FULLBOX);
    m_leftEditor->SendScintilla(QsciScintilla::SCI_INDICSETFORE, 10, QColor("#D32F2F").rgb() & 0xFFFFFF);
    m_leftEditor->SendScintilla(QsciScintilla::SCI_INDICSETALPHA, 10, 80);
    m_leftEditor->SendScintilla(QsciScintilla::SCI_INDICSETOUTLINEALPHA, 10, 200);

    // Indicator 11 = added word (green)
    m_rightEditor->SendScintilla(QsciScintilla::SCI_INDICSETSTYLE, 11, QsciScintilla::INDIC_FULLBOX);
    m_rightEditor->SendScintilla(QsciScintilla::SCI_INDICSETFORE, 11, QColor("#2E7D32").rgb() & 0xFFFFFF);
    m_rightEditor->SendScintilla(QsciScintilla::SCI_INDICSETALPHA, 11, 80);
    m_rightEditor->SendScintilla(QsciScintilla::SCI_INDICSETOUTLINEALPHA, 11, 200);

    // Clear old markers and indicators
    m_leftEditor->markerDeleteAll(MARKER_ADDED);
    m_leftEditor->markerDeleteAll(MARKER_DELETED);
    m_leftEditor->markerDeleteAll(MARKER_BLANK);
    m_rightEditor->markerDeleteAll(MARKER_ADDED);
    m_rightEditor->markerDeleteAll(MARKER_DELETED);
    m_rightEditor->markerDeleteAll(MARKER_BLANK);

    m_leftEditor->SendScintilla(QsciScintilla::SCI_SETINDICATORCURRENT, 10);
    m_leftEditor->SendScintilla(QsciScintilla::SCI_INDICATORCLEARRANGE, 0,
                                 m_leftEditor->text().toUtf8().size());
    m_rightEditor->SendScintilla(QsciScintilla::SCI_SETINDICATORCURRENT, 11);
    m_rightEditor->SendScintilla(QsciScintilla::SCI_INDICATORCLEARRANGE, 0,
                                  m_rightEditor->text().toUtf8().size());

    // Apply line markers + word-level indicators
    int line = 0;
    int leftBytePos = 0, rightBytePos = 0;

    for (const auto &entry : diff.entries) {
        QString leftLine, rightLine;

        switch (entry.tag) {
        case 0: // Equal
            leftLine = "  " + entry.text + "\n";
            rightLine = "  " + entry.text + "\n";
            break;
        case 1: // Added
            leftLine = "\n";
            rightLine = "+ " + entry.text + "\n";
            m_rightEditor->markerAdd(line, MARKER_ADDED);
            m_leftEditor->markerAdd(line, MARKER_BLANK);
            // Highlight the entire added line text (after "+ ")
            {
                QByteArray rb = rightLine.toUtf8();
                int textStart = rightBytePos + 2; // skip "+ "
                int textLen = rb.size() - 3;      // skip "+ " and "\n"
                if (textLen > 0) {
                    m_rightEditor->SendScintilla(QsciScintilla::SCI_SETINDICATORCURRENT, 11);
                    m_rightEditor->SendScintilla(QsciScintilla::SCI_INDICATORFILLRANGE, textStart, textLen);
                }
            }
            break;
        case 2: // Deleted
            leftLine = "- " + entry.text + "\n";
            rightLine = "\n";
            m_leftEditor->markerAdd(line, MARKER_DELETED);
            m_rightEditor->markerAdd(line, MARKER_BLANK);
            // Highlight the entire deleted line text (after "- ")
            {
                QByteArray lb = leftLine.toUtf8();
                int textStart = leftBytePos + 2; // skip "- "
                int textLen = lb.size() - 3;     // skip "- " and "\n"
                if (textLen > 0) {
                    m_leftEditor->SendScintilla(QsciScintilla::SCI_SETINDICATORCURRENT, 10);
                    m_leftEditor->SendScintilla(QsciScintilla::SCI_INDICATORFILLRANGE, textStart, textLen);
                }
            }
            break;
        }

        leftBytePos += leftLine.toUtf8().size();
        rightBytePos += rightLine.toUtf8().size();
        line++;
    }

    m_statsLabel->setText(QString("+%1 added   -%2 removed   %3 diffs   %4 lines")
                          .arg(diff.added).arg(diff.removed)
                          .arg(m_diffLines.size()).arg(line));
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
