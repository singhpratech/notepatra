// SPDX-License-Identifier: GPL-3.0-or-later

#include "findreplace.h"
#include "editor.h"
#include "fonts.h"
#include "mainwindow.h"
#include "rustbridge.h"
#include "searchresults.h"
#include "theme_detect.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGridLayout>
#include <QGroupBox>
#include <QLabel>
#include <QPushButton>
#include <QMessageBox>
#include <QFileDialog>
#include <QDir>
#include <QDirIterator>
#include <QTextStream>
#include <QRegularExpression>
#include <QSplitter>
#include <QFrame>
#include <algorithm>

// Helper: get text from QComboBox reliably
static QString comboText(QComboBox *cb) {
    if (cb->lineEdit()) return cb->lineEdit()->text();
    return cb->currentText();
}

// countMatches/replaceAll have no whole-word flag — wrap the pattern in
// \b anchors, mirroring rust-core's literal whole-word findAll path.
static QString wholeWordWrap(const QString &pattern, bool isRegex) {
    return QStringLiteral("\\b(?:%1)\\b")
        .arg(isRegex ? pattern : QRegularExpression::escape(pattern));
}

void FindReplaceDialog::setDialogStatus(const QString &msg, bool isError) {
    // The bottom dialog status bar is the SINGLE source of truth — visible
    // regardless of active tab. The Find-tab inline m_resultLabel is kept
    // alive as a hidden widget so existing layout slots don't shift.
    const NpPalette pal = npPalette();
    const QString col = isError ? pal.errorFg : pal.accent;
    if (m_dialogStatus) {
        m_dialogStatus->setStyleSheet(QString("color: %1;").arg(col));
        m_dialogStatus->setText(msg);
    }
}

FindReplaceDialog::FindReplaceDialog(QWidget *parent) : QDialog(parent) {
    setWindowTitle("Find / Replace");
    // 580 was enough on Linux/GTK; Windows wider buttons + bold font need
    // more room or the right column ate into the input fields. v0.1.92
    // bumped to 720 + 440 so the bottom status bar fits the longest
    // message ("Reached end — press Find Next again to wrap to top") on
    // a single line at the default size.
    setMinimumWidth(720);
    setMinimumHeight(440);
    setWindowFlags(windowFlags() & ~Qt::WindowContextHelpButtonHint);

    auto *mainLayout = new QVBoxLayout(this);

    m_tabs = new QTabWidget;
    mainLayout->addWidget(m_tabs);

    // v0.1.92 — Notepad++-style status bar at the BOTTOM of the dialog,
    // outside the tab widget. Always visible, single source of truth for
    // result messages from Find / Replace / Count / Find in Files / Mark.
    // Previously the Find tab's m_resultLabel was the only display, so
    // results from Replace All / Replace tab silently dropped on the
    // floor (m_resultsOutput was set to nullptr by v0.1.48 cleanup).
    auto *statusFrame = new QFrame;
    statusFrame->setFrameShape(QFrame::StyledPanel);
    statusFrame->setFrameShadow(QFrame::Sunken);
    auto *statusLay = new QHBoxLayout(statusFrame);
    statusLay->setContentsMargins(8, 4, 8, 4);
    m_dialogStatus = new QLabel("");
    QFont sf = m_dialogStatus->font();
    sf.setItalic(true);
    m_dialogStatus->setFont(sf);
    m_dialogStatus->setTextInteractionFlags(Qt::TextSelectableByMouse);
    // Word-wrap + expand horizontally so long messages
    // ("Reached end of document — press Find Next again to wrap to top")
    // wrap to a 2nd line instead of truncating. setMinimumWidth(0) lets
    // the bar shrink with the dialog; setSizePolicy(Expanding, Preferred)
    // lets it eat all available horizontal space.
    m_dialogStatus->setWordWrap(true);
    m_dialogStatus->setMinimumWidth(0);
    m_dialogStatus->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    statusLay->addWidget(m_dialogStatus, 1);
    mainLayout->addWidget(statusFrame);

    auto *findTab = new QWidget;    buildFindTab(findTab);      m_tabs->addTab(findTab, "Find");
    auto *replTab = new QWidget;    buildReplaceTab(replTab);    m_tabs->addTab(replTab, "Replace");
    auto *fifTab = new QWidget;     buildFindInFilesTab(fifTab); m_tabs->addTab(fifTab, "Find in Files");
    auto *markTab = new QWidget;    buildMarkTab(markTab);       m_tabs->addTab(markTab, "Mark");
    auto *gotoTab = new QWidget;    buildGotoTab(gotoTab);       m_tabs->addTab(gotoTab, "Go to");

    // v0.1.48 — the stale "Search results will appear here…" QTextEdit was
    // removed. Find in Files now publishes results to the dockable
    // SearchResultsPanel at the bottom of the main window (v0.1.46), and
    // single-file Find/Replace writes a one-line status into the dialog
    // status bar instead of into a redundant 150-px panel. m_resultsOutput
    // is kept as a nullptr field so the few remaining writers stay null-
    // safe without needing call-site changes elsewhere.
    m_resultsOutput = nullptr;

    // v0.1.92 — carry the Find string forward when the user switches tabs
    // so they don't have to retype "foo" after pressing Replace. Only fills
    // the destination if it's currently empty — never clobbers a value
    // the user has already typed into the other tab.
    connect(m_tabs, &QTabWidget::currentChanged, this, [this](int idx) {
        const QString findText = comboText(m_findInput);
        const QString replFindText = comboText(m_replFindInput);
        if (idx == 1 && replFindText.isEmpty() && !findText.isEmpty()) {
            m_replFindInput->setCurrentText(findText);
        } else if (idx == 0 && findText.isEmpty() && !replFindText.isEmpty()) {
            m_findInput->setCurrentText(replFindText);
        }
    });
}

// ═══════════════════════════════════════
// Find tab
// ═══════════════════════════════════════

void FindReplaceDialog::buildFindTab(QWidget *tab) {
    auto *layout = new QGridLayout(tab);

    layout->addWidget(new QLabel("Find what:"), 0, 0);
    m_findInput = new QComboBox;
    m_findInput->setEditable(true);
    m_findInput->setInsertPolicy(QComboBox::InsertAtTop);
    m_findInput->setMaxCount(20);
    m_findInput->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    layout->addWidget(m_findInput, 0, 1, 1, 3);

    // Options row 1
    auto *opts1 = new QHBoxLayout;
    // Explicit spacing — Windows fusion/vista style collapses default
    // QHBoxLayout gaps to 0, so the checkboxes used to butt against each
    // other ("Match caseMatch whole word"). Linux gtk style was fine.
    opts1->setSpacing(12);
    m_matchCase = new QCheckBox("Match &case");
    m_wholeWord = new QCheckBox("Match &whole word only");
    m_wrapAround = new QCheckBox("Wra&p around");
    m_wrapAround->setChecked(true);
    opts1->addWidget(m_matchCase);
    opts1->addWidget(m_wholeWord);
    opts1->addWidget(m_wrapAround);
    layout->addLayout(opts1, 1, 0, 1, 4);

    // Search mode
    auto *modeGroup = new QGroupBox("Search Mode");
    auto *modeLay = new QVBoxLayout(modeGroup);
    m_modeNormal = new QRadioButton("&Normal");
    m_modeNormal->setChecked(true);
    m_modeExtended = new QRadioButton("E&xtended (\\n, \\r, \\t, \\0, \\x...)");
    m_modeRegex = new QRadioButton("Regular &expression");
    modeLay->addWidget(m_modeNormal);
    modeLay->addWidget(m_modeExtended);
    modeLay->addWidget(m_modeRegex);
    m_dotMatchesNewline = new QCheckBox(". matches &newline");
    m_dotMatchesNewline->setEnabled(false);
    modeLay->addWidget(m_dotMatchesNewline);
    layout->addWidget(modeGroup, 2, 0, 1, 2);

    // Hidden until wired into the search engine — no code reads it yet.
    m_dotMatchesNewline->setVisible(false);

    // Direction
    auto *dirGroup = new QGroupBox("Direction");
    auto *dirLay = new QHBoxLayout(dirGroup);
    m_dirUp = new QRadioButton("&Up");
    m_dirDown = new QRadioButton("&Down");
    m_dirDown->setChecked(true);
    dirLay->addWidget(m_dirUp);
    dirLay->addWidget(m_dirDown);
    layout->addWidget(dirGroup, 2, 2, 1, 2);
    // Hidden until directional search is implemented — Find Previous
    // already covers upward search; nothing reads these radios yet.
    dirGroup->setVisible(false);

    m_inSelection = new QCheckBox("In se&lection");
    layout->addWidget(m_inSelection, 3, 0, 1, 2);
    // Hidden until selection-scoped search is implemented.
    m_inSelection->setVisible(false);

    // v0.1.92 — m_resultLabel is now hidden. All status text routes
    // through the dialog-level bottom bar (m_dialogStatus). Kept as a
    // hidden widget so the grid layout indices below don't have to be
    // renumbered.
    m_resultLabel = new QLabel("");
    m_resultLabel->setVisible(false);

    // Buttons
    auto *btnLay = new QVBoxLayout;
    auto *findNextBtn = new QPushButton("Find &Next");     findNextBtn->setDefault(true);
    auto *findPrevBtn = new QPushButton("Find Pre&vious");
    auto *countBtn = new QPushButton("Coun&t");
    auto *findAllCurBtn = new QPushButton("Find All in Current");
    auto *findAllOpenBtn = new QPushButton("Find All in All &Opened");
    auto *closeBtn = new QPushButton("Close");

    // setMinimumWidth (not setFixedWidth) — Windows' default font renders
    // "Find All in All Opened" wider than 170 px so the text was cropping
    // to "nd All in All Opene". Letting the button grow past the floor
    // fixes the truncation without affecting Linux/macOS where 170 px was
    // already enough.
    for (auto *b : {findNextBtn, findPrevBtn, countBtn, findAllCurBtn, findAllOpenBtn, closeBtn})
        b->setMinimumWidth(210);

    btnLay->addWidget(findNextBtn);
    btnLay->addWidget(findPrevBtn);
    btnLay->addWidget(countBtn);
    btnLay->addWidget(findAllCurBtn);
    btnLay->addWidget(findAllOpenBtn);
    btnLay->addStretch();
    btnLay->addWidget(closeBtn);
    layout->addLayout(btnLay, 0, 4, 4, 1);

    connect(findNextBtn, &QPushButton::clicked, this, [this]() { doFindNext(true); });
    connect(findPrevBtn, &QPushButton::clicked, this, [this]() { doFindNext(false); });
    connect(countBtn, &QPushButton::clicked, this, &FindReplaceDialog::doCount);
    connect(findAllCurBtn, &QPushButton::clicked, this, &FindReplaceDialog::doFindAllCurrent);
    connect(findAllOpenBtn, &QPushButton::clicked, this, &FindReplaceDialog::doFindAllOpened);
    connect(closeBtn, &QPushButton::clicked, this, &QDialog::close);
    connect(m_findInput->lineEdit(), &QLineEdit::returnPressed, this, [this]() { doFindNext(true); });
}

// ═══════════════════════════════════════
// Replace tab
// ═══════════════════════════════════════

void FindReplaceDialog::buildReplaceTab(QWidget *tab) {
    auto *layout = new QGridLayout(tab);

    layout->addWidget(new QLabel("Find what:"), 0, 0);
    m_replFindInput = new QComboBox;
    m_replFindInput->setEditable(true);
    m_replFindInput->setInsertPolicy(QComboBox::InsertAtTop);
    m_replFindInput->setMaxCount(20);
    m_replFindInput->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    layout->addWidget(m_replFindInput, 0, 1, 1, 3);

    layout->addWidget(new QLabel("Replace with:"), 1, 0);
    m_replInput = new QComboBox;
    m_replInput->setEditable(true);
    m_replInput->setInsertPolicy(QComboBox::InsertAtTop);
    m_replInput->setMaxCount(20);
    m_replInput->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    layout->addWidget(m_replInput, 1, 1, 1, 3);

    // Options
    auto *opts = new QHBoxLayout;
    opts->setSpacing(12);
    m_rMatchCase = new QCheckBox("Match &case");
    m_rWholeWord = new QCheckBox("Match &whole word only");
    m_rWrapAround = new QCheckBox("Wra&p around");
    m_rWrapAround->setChecked(true);
    opts->addWidget(m_rMatchCase);
    opts->addWidget(m_rWholeWord);
    opts->addWidget(m_rWrapAround);
    layout->addLayout(opts, 2, 0, 1, 4);

    // Search mode
    auto *modeGroup = new QGroupBox("Search Mode");
    auto *modeLay = new QHBoxLayout(modeGroup);
    modeLay->setSpacing(12);
    m_rModeNormal = new QRadioButton("Normal");
    m_rModeNormal->setChecked(true);
    m_rModeExtended = new QRadioButton("Extended");
    m_rModeRegex = new QRadioButton("Regex");
    modeLay->addWidget(m_rModeNormal);
    modeLay->addWidget(m_rModeExtended);
    modeLay->addWidget(m_rModeRegex);
    layout->addWidget(modeGroup, 3, 0, 1, 2);

    m_rInSelection = new QCheckBox("In selection");
    layout->addWidget(m_rInSelection, 3, 2, 1, 2);
    // Hidden until selection-scoped replace is implemented.
    m_rInSelection->setVisible(false);

    // Buttons
    auto *btnLay = new QVBoxLayout;
    auto *findBtn = new QPushButton("Find &Next");
    auto *replBtn = new QPushButton("&Replace");
    auto *replAllBtn = new QPushButton("Replace &All");
    auto *replAllOpenBtn = new QPushButton("Replace All in All &Opened");
    auto *closeBtn = new QPushButton("Close");

    // setMinimumWidth — see comment on the Find tab. Same Windows-font
    // truncation risk for "Replace All in All Opened".
    for (auto *b : {findBtn, replBtn, replAllBtn, replAllOpenBtn, closeBtn})
        b->setMinimumWidth(230);

    btnLay->addWidget(findBtn);
    btnLay->addWidget(replBtn);
    btnLay->addWidget(replAllBtn);
    btnLay->addWidget(replAllOpenBtn);
    btnLay->addStretch();
    btnLay->addWidget(closeBtn);
    layout->addLayout(btnLay, 0, 4, 4, 1);

    connect(findBtn, &QPushButton::clicked, this, [this]() {
        auto *e = getEditor(); if (!e) return;
        QString text = comboText(m_replFindInput); if (text.isEmpty()) return;
        if (m_rModeExtended->isChecked()) text = processExtended(text);
        bool isRegex = m_rModeRegex->isChecked();
        e->findFirst(text, isRegex, m_rMatchCase->isChecked(), m_rWholeWord->isChecked(),
                     m_rWrapAround->isChecked(), true);
    });
    connect(replBtn, &QPushButton::clicked, this, &FindReplaceDialog::doReplace);
    connect(replAllBtn, &QPushButton::clicked, this, &FindReplaceDialog::doReplaceAll);
    connect(replAllOpenBtn, &QPushButton::clicked, this, &FindReplaceDialog::doReplaceAllOpened);
    connect(closeBtn, &QPushButton::clicked, this, &QDialog::close);
}

// ═══════════════════════════════════════
// Find in Files tab
// ═══════════════════════════════════════

void FindReplaceDialog::buildFindInFilesTab(QWidget *tab) {
    auto *layout = new QGridLayout(tab);

    layout->addWidget(new QLabel("Find what:"), 0, 0);
    m_fifFindInput = new QComboBox;
    m_fifFindInput->setEditable(true);
    m_fifFindInput->setMaxCount(20);
    m_fifFindInput->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    layout->addWidget(m_fifFindInput, 0, 1, 1, 2);

    layout->addWidget(new QLabel("Directory:"), 1, 0);
    m_fifDirectory = new QComboBox;
    m_fifDirectory->setEditable(true);
    m_fifDirectory->setMaxCount(10);
    m_fifDirectory->addItem(QDir::toNativeSeparators(QDir::homePath()));
    m_fifDirectory->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    layout->addWidget(m_fifDirectory, 1, 1);

    auto *browseBtn = new QPushButton("...");
    browseBtn->setFixedWidth(30);
    layout->addWidget(browseBtn, 1, 2);

    layout->addWidget(new QLabel("Filters:"), 2, 0);
    m_fifFilters = new QComboBox;
    m_fifFilters->setEditable(true);
    m_fifFilters->addItems({"*.*", "*.py", "*.js *.ts", "*.cpp *.h", "*.java", "*.html *.css",
                            "*.json *.xml *.yaml", "*.sql", "*.rs", "*.go", "*.sh"});
    layout->addWidget(m_fifFilters, 2, 1, 1, 2);

    // Options
    auto *opts = new QHBoxLayout;
    opts->setSpacing(12);
    m_fifMatchCase = new QCheckBox("Match case");
    m_fifWholeWord = new QCheckBox("Whole word");
    m_fifRegex = new QCheckBox("Regex");
    m_fifSubfolders = new QCheckBox("In all sub-folders");
    m_fifSubfolders->setChecked(true);
    m_fifHidden = new QCheckBox("Hidden folders");
    opts->addWidget(m_fifMatchCase);
    opts->addWidget(m_fifWholeWord);
    opts->addWidget(m_fifRegex);
    opts->addWidget(m_fifSubfolders);
    opts->addWidget(m_fifHidden);
    layout->addLayout(opts, 3, 0, 1, 3);

    // Buttons
    auto *btnLay = new QVBoxLayout;
    auto *findAllBtn = new QPushButton("Find All");
    findAllBtn->setFixedWidth(140);
    auto *closeBtn = new QPushButton("Close");
    closeBtn->setFixedWidth(140);
    btnLay->addWidget(findAllBtn);
    btnLay->addStretch();
    btnLay->addWidget(closeBtn);
    layout->addLayout(btnLay, 0, 3, 4, 1);

    connect(browseBtn, &QPushButton::clicked, this, [this]() {
        QString dir = QFileDialog::getExistingDirectory(this, "Select Directory", comboText(m_fifDirectory));
        if (!dir.isEmpty()) m_fifDirectory->setCurrentText(QDir::toNativeSeparators(dir));
    });
    connect(findAllBtn, &QPushButton::clicked, this, &FindReplaceDialog::doFindInFiles);
    connect(closeBtn, &QPushButton::clicked, this, &QDialog::close);
}

// ═══════════════════════════════════════
// Mark tab
// ═══════════════════════════════════════

void FindReplaceDialog::buildMarkTab(QWidget *tab) {
    auto *layout = new QGridLayout(tab);

    layout->addWidget(new QLabel("Find what:"), 0, 0);
    m_markFindInput = new QComboBox;
    m_markFindInput->setEditable(true);
    m_markFindInput->setMaxCount(20);
    m_markFindInput->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    layout->addWidget(m_markFindInput, 0, 1, 1, 2);

    auto *opts = new QHBoxLayout;
    opts->setSpacing(12);
    m_markCase = new QCheckBox("Match case");
    m_markWholeWord = new QCheckBox("Whole word");
    m_markRegex = new QCheckBox("Regex");
    m_markPurge = new QCheckBox("Purge for each search");
    m_markPurge->setChecked(true);
    opts->addWidget(m_markCase);
    opts->addWidget(m_markWholeWord);
    opts->addWidget(m_markRegex);
    opts->addWidget(m_markPurge);
    layout->addLayout(opts, 1, 0, 1, 3);

    auto *btnLay = new QVBoxLayout;
    auto *markAllBtn = new QPushButton("Mark All");
    markAllBtn->setFixedWidth(140);
    auto *clearBtn = new QPushButton("Clear All Marks");
    clearBtn->setFixedWidth(140);
    auto *closeBtn = new QPushButton("Close");
    closeBtn->setFixedWidth(140);
    btnLay->addWidget(markAllBtn);
    btnLay->addWidget(clearBtn);
    btnLay->addStretch();
    btnLay->addWidget(closeBtn);
    layout->addLayout(btnLay, 0, 3, 2, 1);

    connect(markAllBtn, &QPushButton::clicked, this, &FindReplaceDialog::doMarkAll);
    connect(clearBtn, &QPushButton::clicked, this, [this]() {
        auto *e = getEditor(); if (!e) return;
        e->SendScintilla(QsciScintilla::SCI_SETINDICATORCURRENT, 8);
        e->SendScintilla(QsciScintilla::SCI_INDICATORCLEARRANGE, 0, e->text().toUtf8().size());
        setDialogStatus("All marks cleared");
    });
    connect(closeBtn, &QPushButton::clicked, this, &QDialog::close);
}

// ═══════════════════════════════════════
// Go to tab
// ═══════════════════════════════════════

void FindReplaceDialog::buildGotoTab(QWidget *tab) {
    auto *layout = new QVBoxLayout(tab);
    layout->addStretch();

    auto *row1 = new QHBoxLayout;
    m_gotoLine = new QRadioButton("Line");
    m_gotoLine->setChecked(true);
    m_gotoOffset = new QRadioButton("Offset");
    row1->addWidget(m_gotoLine);
    row1->addWidget(m_gotoOffset);
    row1->addStretch();
    layout->addLayout(row1);

    auto *row2 = new QHBoxLayout;
    m_gotoInput = new QLineEdit;
    m_gotoInput->setPlaceholderText("Enter line number or byte offset...");
    row2->addWidget(m_gotoInput);
    layout->addLayout(row2);

    auto *row3 = new QHBoxLayout;
    row3->addStretch();
    auto *goBtn = new QPushButton("Go");
    goBtn->setFixedWidth(100);
    auto *closeBtn = new QPushButton("Close");
    closeBtn->setFixedWidth(100);
    row3->addWidget(goBtn);
    row3->addWidget(closeBtn);
    layout->addLayout(row3);
    layout->addStretch();

    connect(closeBtn, &QPushButton::clicked, this, &QDialog::close);
    connect(goBtn, &QPushButton::clicked, this, [this]() {
        auto *e = getEditor(); if (!e) return;
        bool ok;
        int val = m_gotoInput->text().toInt(&ok);
        if (!ok || val < 1) return;
        if (m_gotoLine->isChecked()) {
            e->gotoLine(val);
        } else {
            // Go to byte offset
            long pos = 0; int line = 0;
            while (line < e->lines() && pos < val) {
                pos += e->text(line).toUtf8().size();
                line++;
            }
            e->setCursorPosition(line > 0 ? line - 1 : 0, 0);
        }
        close();
    });
    connect(m_gotoInput, &QLineEdit::returnPressed, goBtn, &QPushButton::click);
}

// ═══════════════════════════════════════
// Actions
// ═══════════════════════════════════════


MainWindow *FindReplaceDialog::mainWindow() {
    return qobject_cast<MainWindow *>(parent());
}

Editor *FindReplaceDialog::getEditor() {
    auto *mw = mainWindow();
    return mw ? mw->currentEditor() : nullptr;
}

QString FindReplaceDialog::processExtended(const QString &text) {
    // Convert \n, \r, \t, \0, \xNN escape sequences
    QString result;
    result.reserve(text.size());
    for (int i = 0; i < text.size(); i++) {
        if (text[i] == '\\' && i + 1 < text.size()) {
            QChar next = text[i + 1];
            if (next == 'n') { result += '\n'; i++; }
            else if (next == 'r') { result += '\r'; i++; }
            else if (next == 't') { result += '\t'; i++; }
            else if (next == '0') { result += '\0'; i++; }
            else if (next == '\\') { result += '\\'; i++; }
            else if (next == 'x' && i + 3 < text.size()) {
                bool ok;
                int code = text.mid(i + 2, 2).toInt(&ok, 16);
                if (ok) { result += QChar(code); i += 3; }
                else result += text[i];
            }
            else result += text[i];
        } else {
            result += text[i];
        }
    }
    return result;
}

void FindReplaceDialog::doFindNext(bool forward) {
    auto *e = getEditor();
    if (!e) {
        setDialogStatus("No editor tab active", true);
        return;
    }
    QString text = comboText(m_findInput);
    if (text.isEmpty()) {
        setDialogStatus("Type something to find", true);
        return;
    }
    if (m_findInput->findText(text) < 0) m_findInput->insertItem(0, text);

    // Reset the end-of-cycle flag whenever the search context changes —
    // new needle, flipped direction, or first Find Next in this session.
    if (text != m_lastFindText || forward != m_lastFindForward) {
        m_atFindCycleEnd = false;
        m_lastFindText = text;
        m_lastFindForward = forward;
    }

    bool isRegex = m_modeRegex->isChecked();
    QString needle = text;
    if (m_modeExtended->isChecked()) needle = processExtended(needle);

    // First attempt: search from cursor forward (or backward) WITHOUT
    // wrap-around. If we find a match we're still mid-cycle; if not, we
    // hit the end of this iteration.
    bool found = e->findFirst(needle, isRegex, m_matchCase->isChecked(),
                              m_wholeWord->isChecked(), /*wrap=*/false, forward);
    if (found) {
        m_atFindCycleEnd = false;
        setDialogStatus(QString("Found: \"%1\"").arg(text));
        return;
    }

    // Reached end of remaining range. If wrap-around is OFF, just report
    // "not found in remaining document" — never silently jump.
    if (!m_wrapAround->isChecked()) {
        setDialogStatus(QString("Reached %1 of document — \"%2\" not found further")
                            .arg(forward ? "end" : "start").arg(text), true);
        return;
    }

    if (!m_atFindCycleEnd) {
        // First Find Next at the end of the cycle — STOP with a notice.
        // Cursor stays at its current position. Second Find Next will
        // actually wrap.
        m_atFindCycleEnd = true;
        setDialogStatus(QString("Reached %1 of document — press Find %2 again to wrap to %3")
                            .arg(forward ? "end" : "start")
                            .arg(forward ? "Next" : "Previous")
                            .arg(forward ? "top" : "bottom"), true);
        return;
    }

    // Second consecutive Find Next at the end — wrap to the start (or end
    // for backward search) and search again. This RESTARTS the cycle.
    if (forward) {
        e->setCursorPosition(0, 0);
    } else {
        int lastLine = e->lines() - 1;
        if (lastLine < 0) lastLine = 0;
        e->setCursorPosition(lastLine, e->text(lastLine).length());
    }
    found = e->findFirst(needle, isRegex, m_matchCase->isChecked(),
                         m_wholeWord->isChecked(), /*wrap=*/false, forward);
    if (found) {
        m_atFindCycleEnd = false;
        setDialogStatus(QString("Search wrapped — resumed from %1: \"%2\"")
                            .arg(forward ? "top" : "bottom").arg(text));
    } else {
        // Zero matches in the document at all (rare — only if every prior
        // "Found" was on stale text).
        setDialogStatus(QString("Not found anywhere: \"%1\"").arg(text), true);
    }
}

void FindReplaceDialog::findNext() { doFindNext(true); }
void FindReplaceDialog::findPrevious() { doFindNext(false); }

void FindReplaceDialog::doCount() {
    auto *e = getEditor(); if (!e) return;
    QString needle = comboText(m_findInput);
    if (needle.isEmpty()) return;

    if (m_modeExtended->isChecked()) needle = processExtended(needle);

    bool isRegex = m_modeRegex->isChecked();
    QString pattern = needle;
    if (m_wholeWord->isChecked()) {
        pattern = wholeWordWrap(pattern, isRegex);
        isRegex = true;
    }
    size_t count = RustCore::countMatches(e->text(), pattern, isRegex,
                                           m_matchCase->isChecked());
    setDialogStatus(QString("Count: %1 match(es) for \"%2\"").arg(count).arg(needle));
}

void FindReplaceDialog::doFindAllCurrent() {
    auto *e = getEditor(); if (!e) return;
    auto *mw = mainWindow(); if (!mw) return;
    QString needle = comboText(m_findInput);
    if (needle.isEmpty()) return;

    if (m_modeExtended->isChecked()) needle = processExtended(needle);

    auto positions = RustCore::findAll(e->text(), needle,
                                        m_modeRegex->isChecked(),
                                        m_matchCase->isChecked(),
                                        m_wholeWord->isChecked());

    // Show results in bottom panel (like Notepad++).
    // v0.1.46 — DO NOT clear(); each search must stack as a new
    // session so the user can see previous queries collapsed below
    // the current one. clear() wipes the entire history and is
    // reserved for an explicit user action (panel close + reopen).
    auto *sr = mw->searchResults();

    // Real path may be empty (unsaved tab) — stored as-is so double-
    // click activates the existing tab instead of opening "Untitled".
    QString filePath = e->filePath();
    QString fileName = filePath.isEmpty() ? "Untitled" : filePath;
    QStringList lines = e->text().split('\n');

    // Map byte offsets to line numbers — findAll returns UTF-8 byte
    // offsets, so accumulate byte lengths, not UTF-16 code units.
    QVector<QPair<int, QString>> results;
    for (const auto &match : positions) {
        const size_t pos = match.start;
        int charPos = 0, lineNum = 0;
        for (int i = 0; i < lines.size(); i++) {
            int lineLen = lines[i].toUtf8().size() + 1;
            if (charPos + lineLen > (int)pos) { lineNum = i + 1; break; }
            charPos += lineLen;
        }
        if (lineNum > 0 && lineNum <= lines.size())
            results.append({lineNum, lines[lineNum - 1]});
    }

    // v0.1.45 — open a new session FIRST so files+lines land under
    // the new top-level row (Notepad++-style stacked history). Final
    // setHeader updates that row's label with the totals.
    sr->beginSession(needle);
    sr->addFileSection(filePath, results.size(), fileName);
    for (const auto &r : results)
        sr->addResultLine(r.first, r.second, needle);
    sr->setHeader(needle, results.size(), 1);

    // Show the panel
    sr->setVisible(true);
    mw->vertSplitter()->setSizes({500, 200});

    setDialogStatus(QString("Find All in Current Document: %1 hit(s)").arg(positions.size()));
}

void FindReplaceDialog::doFindAllOpened() {
    auto *mw = mainWindow(); if (!mw) return;
    QString needle = comboText(m_findInput);
    if (needle.isEmpty()) return;

    if (m_modeExtended->isChecked()) needle = processExtended(needle);

    auto *sr = mw->searchResults();
    // v0.1.45 — open a new stacked session instead of wiping history.
    // Users can still hit "Clear" via the panel UI to reset everything.
    sr->beginSession(needle);

    int totalHits = 0;
    int fileCount = 0;

    auto *tabs = mw->findChild<QTabWidget *>();
    if (!tabs) return;

    for (int t = 0; t < tabs->count(); t++) {
        auto *ed = qobject_cast<Editor *>(tabs->widget(t));
        if (!ed) continue;

        QString name = ed->filePath().isEmpty() ? tabs->tabText(t) : ed->filePath();
        auto positions = RustCore::findAll(ed->text(), needle,
                                            m_modeRegex->isChecked(),
                                            m_matchCase->isChecked(),
                                            m_wholeWord->isChecked());
        if (positions.isEmpty()) continue;

        fileCount++;
        QStringList lines = ed->text().split('\n');
        // Real path (empty for unsaved tabs) as data, tab text as label.
        sr->addFileSection(ed->filePath(), positions.size(), name);

        for (const auto &match : positions) {
            const size_t pos = match.start;
            int charPos = 0, lineNum = 0;
            for (int i = 0; i < lines.size(); i++) {
                int lineLen = lines[i].toUtf8().size() + 1;
                if (charPos + lineLen > (int)pos) { lineNum = i + 1; break; }
                charPos += lineLen;
            }
            if (lineNum > 0 && lineNum <= lines.size())
                sr->addResultLine(lineNum, lines[lineNum - 1], needle);
            totalHits++;
        }
    }

    sr->setHeader(needle, totalHits, fileCount);
    sr->setVisible(true);
    mw->vertSplitter()->setSizes({500, 200});
    setDialogStatus(QString("Find All in All Opened: %1 hit(s) in %2 file(s)").arg(totalHits).arg(fileCount));
}

void FindReplaceDialog::doReplace() {
    auto *e = getEditor();
    if (!e) { setDialogStatus("No editor tab active", true); return; }
    QString repl = comboText(m_replInput);
    if (m_rModeExtended->isChecked()) repl = processExtended(repl);

    QString text = comboText(m_replFindInput);
    if (m_rModeExtended->isChecked()) text = processExtended(text);

    const bool isRegex   = m_rModeRegex->isChecked();
    const bool matchCase = m_rMatchCase->isChecked();
    const bool wholeWord = m_rWholeWord->isChecked();

    // Replace only what is actually a match, and only report a replacement that
    // actually happened. This is Notepad++'s processReplace() contract, and the
    // previous code met neither half of it:
    //
    //   * didReplace was set from the mere EXISTENCE of a selection. But
    //     QsciScintilla::replace() quietly does nothing unless a previous
    //     findFirst() armed its internal find state — the situation in every
    //     freshly opened tab. So "Replaced 1 occurrence" printed over an
    //     untouched buffer, and because the code then advanced to the next
    //     match, that occurrence was skipped for good.
    //   * Once findFirst() HAD run, replace() targets the selection whatever it
    //     contains, so replacing with an unrelated selection overwrote text the
    //     user never searched for.
    //
    // Re-finding at the selection's own start both verifies the selection and
    // arms the find state, which is what makes the following replace() real.
    bool didReplace = false;
    if (e->hasSelectedText()) {
        const long selStart = e->SendScintilla(QsciScintilla::SCI_GETSELECTIONSTART);
        const long selEnd   = e->SendScintilla(QsciScintilla::SCI_GETSELECTIONEND);
        int selLine = 0, selIndex = 0, endLine = 0, endIndex = 0;
        e->getSelection(&selLine, &selIndex, &endLine, &endIndex);

        if (!text.isEmpty()
            && e->findFirst(text, isRegex, matchCase, wholeWord,
                            /*wrap*/ false, /*forward*/ true, selLine, selIndex)) {
            const long foundStart = e->SendScintilla(QsciScintilla::SCI_GETSELECTIONSTART);
            const long foundEnd   = e->SendScintilla(QsciScintilla::SCI_GETSELECTIONEND);
            if (foundStart == selStart && foundEnd == selEnd) {
                e->replace(repl);
                didReplace = true;
            }
        }
    }

    // Find next
    bool found = e->findFirst(text, isRegex, matchCase, wholeWord,
                              m_rWrapAround->isChecked(), true);
    if (didReplace && found)
        setDialogStatus(QString("Replaced 1 occurrence — next match selected"));
    else if (didReplace)
        setDialogStatus(QString("Replaced 1 occurrence — no further matches"));
    else if (found)
        setDialogStatus(QString("Next match selected — press Replace again to replace it"));
    else
        setDialogStatus(QString("Not found: \"%1\"").arg(text), true);
}

void FindReplaceDialog::doReplaceAll() {
    auto *e = getEditor(); if (!e) return;
    QString findText = comboText(m_replFindInput);
    QString replText = comboText(m_replInput);
    if (findText.isEmpty()) return;

    if (m_rModeExtended->isChecked()) {
        findText = processExtended(findText);
        replText = processExtended(replText);
    }

    // Add to history
    if (m_replFindInput->findText(findText) < 0) m_replFindInput->insertItem(0, findText);
    if (m_replInput->findText(replText) < 0) m_replInput->insertItem(0, replText);

    bool isRegex = m_rModeRegex->isChecked();
    QString pattern = findText;
    if (m_rWholeWord->isChecked()) {
        pattern = wholeWordWrap(pattern, isRegex);
        // Literal promoted to regex — keep user dollars literal.
        if (!isRegex) replText.replace(QLatin1String("$"), QLatin1String("$$"));
        isRegex = true;
    }

    size_t count = RustCore::countMatches(e->text(), pattern, isRegex,
                                           m_rMatchCase->isChecked());
    // Zero matches: leave the document untouched (no dirty flag).
    if (count == 0) {
        setDialogStatus(QString("Replace All: 0 occurrences replaced"), true);
        return;
    }

    // Use Rust core for fast replace
    QString result = RustCore::replaceAll(e->text(), pattern, replText,
                                           isRegex, m_rMatchCase->isChecked());
    e->selectAll();
    e->replaceSelectedText(result);
    setDialogStatus(QString("Replace All: %1 occurrence%2 replaced")
                        .arg(count).arg(count == 1 ? "" : "s"));
}

void FindReplaceDialog::doReplaceAllOpened() {
    auto *mw = mainWindow(); if (!mw) return;
    QString findText = comboText(m_replFindInput);
    QString replText = comboText(m_replInput);
    if (findText.isEmpty()) return;

    if (m_rModeExtended->isChecked()) {
        findText = processExtended(findText);
        replText = processExtended(replText);
    }

    auto *tabs = mw->findChild<QTabWidget *>();
    if (!tabs) return;

    bool isRegex = m_rModeRegex->isChecked();
    QString pattern = findText;
    if (m_rWholeWord->isChecked()) {
        pattern = wholeWordWrap(pattern, isRegex);
        // Literal promoted to regex — keep user dollars literal.
        if (!isRegex) replText.replace(QLatin1String("$"), QLatin1String("$$"));
        isRegex = true;
    }

    int totalReplaced = 0;
    int filesTouched = 0;

    for (int t = 0; t < tabs->count(); t++) {
        auto *ed = qobject_cast<Editor *>(tabs->widget(t));
        if (!ed || ed->isReadOnly()) continue;

        size_t count = RustCore::countMatches(ed->text(), pattern,
                                               isRegex,
                                               m_rMatchCase->isChecked());
        if (count == 0) continue;

        QString result = RustCore::replaceAll(ed->text(), pattern, replText,
                                               isRegex,
                                               m_rMatchCase->isChecked());
        ed->selectAll();
        ed->replaceSelectedText(result);
        totalReplaced += count;
        ++filesTouched;
    }

    setDialogStatus(QString("Replace All in Opened: %1 occurrence(s) across %2 file(s)")
                        .arg(totalReplaced).arg(filesTouched));
}

void FindReplaceDialog::doFindInFiles() {
    auto *mw = mainWindow(); if (!mw) return;
    QString needle = comboText(m_fifFindInput);
    QString dir = comboText(m_fifDirectory);
    QString filterStr = comboText(m_fifFilters);
    if (needle.isEmpty() || dir.isEmpty()) return;

    bool isRegex = m_fifRegex->isChecked();
    QString pattern = needle;
    if (m_fifWholeWord->isChecked()) {
        pattern = wholeWordWrap(pattern, isRegex);
        isRegex = true;
    }
    // Compiled once — the old per-line rebuild was O(lines) compiles.
    const QRegularExpression lineRe(pattern,
        m_fifMatchCase->isChecked() ? QRegularExpression::NoPatternOption
                                    : QRegularExpression::CaseInsensitiveOption);

    // "*.*" is the legacy Windows "any file" pattern and it is what this dialog
    // offers as its FIRST, default filter. Qt matches wildcards literally, so
    // "*.*" demands a dot in the name — passing it straight to QDirIterator
    // silently skipped Makefile, Dockerfile, LICENSE, README, .gitignore and
    // every other extension-less file, then reported "0 hits" as though the
    // text simply were not there. A search tool that confidently says "not
    // found" about a file it never opened is worse than one that is slow.
    // Notepad++ matches with PathMatchSpec, where "*.*" means any file.
    QStringList filters;
    for (const auto &f : filterStr.split(' ', Qt::SkipEmptyParts))
        filters << (f == QLatin1String("*.*") ? QStringLiteral("*") : f);
    if (filters.isEmpty()) filters << QStringLiteral("*");

    QDir::Filters dirFilters = QDir::Files | QDir::NoDotAndDotDot;
    if (m_fifHidden->isChecked()) dirFilters |= QDir::Hidden;

    QDirIterator::IteratorFlags iterFlags;
    if (m_fifSubfolders->isChecked()) iterFlags = QDirIterator::Subdirectories;

    // Publish hits to the dockable SearchResultsPanel — same stacked-
    // session path Find All uses. The old in-dialog output widget was
    // removed in v0.1.48, so results were silently dropped until now.
    auto *sr = mw->searchResults();
    sr->beginSession(needle);

    QDirIterator it(dir, filters, dirFilters, iterFlags);
    int totalHits = 0, fileCount = 0;

    while (it.hasNext()) {
        QString filePath = it.next();
        QFile file(filePath);
        if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) continue;

        // Skip binary/huge files
        if (file.size() > 50 * 1024 * 1024) continue;

        QTextStream stream(&file);
        QString content = stream.readAll();

        size_t count = RustCore::countMatches(content, pattern,
                                               isRegex,
                                               m_fifMatchCase->isChecked());
        if (count == 0) continue;

        fileCount++;
        totalHits += count;

        sr->addFileSection(filePath, (int)count,
                           QDir::toNativeSeparators(filePath));

        const QStringList lines = content.split('\n');
        for (int i = 0; i < lines.size(); i++) {
            bool match;
            if (isRegex) {
                match = lineRe.match(lines[i]).hasMatch();
            } else if (m_fifMatchCase->isChecked()) {
                match = lines[i].contains(needle);
            } else {
                match = lines[i].contains(needle, Qt::CaseInsensitive);
            }
            if (match) sr->addResultLine(i + 1, lines[i], needle);
        }

        if (fileCount >= 1000) break;
    }

    sr->setHeader(needle, totalHits, fileCount);
    sr->setVisible(true);
    mw->vertSplitter()->setSizes({500, 200});

    if (totalHits == 0)
        setDialogStatus(QString("Find in Files: 0 hits for \"%1\"").arg(needle), true);
    else
        setDialogStatus(QString("Find in Files: %1 hit(s) in %2 file(s)").arg(totalHits).arg(fileCount));
}

void FindReplaceDialog::doMarkAll() {
    auto *e = getEditor(); if (!e) return;
    QString needle = comboText(m_markFindInput);
    if (needle.isEmpty()) return;

    // Setup indicator. Scintilla SCI_INDICSETFORE wants Win32 COLORREF
    // (0x00BBGGRR), NOT Qt RGB. Previously passed 0x0000FF expecting blue
    // but byte-swap actually rendered pure RED. Now BGR-packed properly
    // for Tailwind blue-500 (#3B82F6) — a "find/search" blue that's
    // visually distinct from the neon-orange double-click match indicator.
    e->SendScintilla(QsciScintilla::SCI_INDICSETSTYLE, 8, QsciScintilla::INDIC_ROUNDBOX);
    {
        QColor markFg("#3B82F6");
        long bgr = (long(markFg.blue()) << 16) | (long(markFg.green()) << 8) | long(markFg.red());
        e->SendScintilla(QsciScintilla::SCI_INDICSETFORE, 8, bgr);
    }
    e->SendScintilla(QsciScintilla::SCI_INDICSETALPHA, 8, 80);
    e->SendScintilla(QsciScintilla::SCI_SETINDICATORCURRENT, 8);

    // Clear previous marks if purge checked
    if (m_markPurge->isChecked()) {
        e->SendScintilla(QsciScintilla::SCI_INDICATORCLEARRANGE, 0, e->text().toUtf8().size());
    }

    auto positions = RustCore::findAll(e->text(), needle,
                                        m_markRegex->isChecked(),
                                        m_markCase->isChecked(),
                                        m_markWholeWord->isChecked());

    QByteArray utf8 = e->text().toUtf8();

    // v0.1.87 — cap Mark All to 10000 indicators on very large files.
    // SCI_INDICATORFILLRANGE issues a paint invalidation per call; on a
    // 118 MB file with 100k matches the redraw stalls the UI for seconds.
    // 10000 highlights still gives a strong "many matches" signal without
    // freezing the editor. The result label reports the truncation so
    // users understand the count.
    const bool largeFile = utf8.size() > 50 * 1024 * 1024;
    const size_t cap = largeFile ? size_t{10000} : positions.size();
    const size_t drawCount = std::min<size_t>(cap, positions.size());

    for (size_t i = 0; i < drawCount; ++i) {
        // Each match's OWN length. This used to paint needle.toUtf8().size()
        // bytes at every hit, so marking `\d+` highlighted three characters
        // regardless of how many digits actually matched.
        e->SendScintilla(QsciScintilla::SCI_INDICATORFILLRANGE,
                         (int)positions[i].start, (long)positions[i].length);
    }

    if (largeFile && positions.size() > cap) {
        setDialogStatus(
            QString("Marked first %1 of %2 occurrence(s) — capped for performance on large files")
                .arg(drawCount).arg(positions.size()));
    } else {
        setDialogStatus(QString("Marked %1 occurrence(s)").arg(positions.size()));
    }
}

// ═══════════════════════════════════════
// Show methods
// ═══════════════════════════════════════

void FindReplaceDialog::showFind() {
    m_tabs->setCurrentIndex(0);
    auto *e = getEditor();
    if (e && e->hasSelectedText()) m_findInput->setCurrentText(e->selectedText());
    m_findInput->setFocus();
    m_findInput->lineEdit()->selectAll();
    show(); raise();
}

void FindReplaceDialog::showReplace() {
    m_tabs->setCurrentIndex(1);
    auto *e = getEditor();
    if (e && e->hasSelectedText()) m_replFindInput->setCurrentText(e->selectedText());
    m_replFindInput->setFocus();
    show(); raise();
}

void FindReplaceDialog::showFindInFiles() {
    m_tabs->setCurrentIndex(2);
    m_fifFindInput->setFocus();
    show(); raise();
}

void FindReplaceDialog::showMark() {
    m_tabs->setCurrentIndex(3);
    m_markFindInput->setFocus();
    show(); raise();
}

void FindReplaceDialog::showGoto() {
    m_tabs->setCurrentIndex(4);
    m_gotoInput->setFocus();
    m_gotoInput->selectAll();
    show(); raise();
}
