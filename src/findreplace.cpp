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

// Helper: get text from QComboBox reliably
static QString comboText(QComboBox *cb) {
    if (cb->lineEdit()) return cb->lineEdit()->text();
    return cb->currentText();
}

FindReplaceDialog::FindReplaceDialog(QWidget *parent) : QDialog(parent) {
    setWindowTitle("Find / Replace");
    setMinimumWidth(580);
    setMinimumHeight(400);
    setWindowFlags(windowFlags() & ~Qt::WindowContextHelpButtonHint);

    auto *mainLayout = new QVBoxLayout(this);

    m_tabs = new QTabWidget;
    mainLayout->addWidget(m_tabs);

    auto *findTab = new QWidget;    buildFindTab(findTab);      m_tabs->addTab(findTab, "Find");
    auto *replTab = new QWidget;    buildReplaceTab(replTab);    m_tabs->addTab(replTab, "Replace");
    auto *fifTab = new QWidget;     buildFindInFilesTab(fifTab); m_tabs->addTab(fifTab, "Find in Files");
    auto *markTab = new QWidget;    buildMarkTab(markTab);       m_tabs->addTab(markTab, "Mark");
    auto *gotoTab = new QWidget;    buildGotoTab(gotoTab);       m_tabs->addTab(gotoTab, "Go to");

    // Results output panel
    m_resultsOutput = new QTextEdit;
    m_resultsOutput->setReadOnly(true);
    m_resultsOutput->setMaximumHeight(150);
    m_resultsOutput->setFont(notepatraCodeFont(10));
    m_resultsOutput->setPlaceholderText("Search results will appear here...");
    mainLayout->addWidget(m_resultsOutput);
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

    connect(m_modeRegex, &QRadioButton::toggled, m_dotMatchesNewline, &QCheckBox::setEnabled);

    // Direction
    auto *dirGroup = new QGroupBox("Direction");
    auto *dirLay = new QHBoxLayout(dirGroup);
    m_dirUp = new QRadioButton("&Up");
    m_dirDown = new QRadioButton("&Down");
    m_dirDown->setChecked(true);
    dirLay->addWidget(m_dirUp);
    dirLay->addWidget(m_dirDown);
    layout->addWidget(dirGroup, 2, 2, 1, 2);

    m_inSelection = new QCheckBox("In se&lection");
    layout->addWidget(m_inSelection, 3, 0, 1, 2);

    m_resultLabel = new QLabel("");
    const NpPalette pal = npPalette();
    m_resultLabel->setStyleSheet(QString("color: %1; font-weight: bold;").arg(pal.accent));
    layout->addWidget(m_resultLabel, 3, 2, 1, 2);

    // Buttons
    auto *btnLay = new QVBoxLayout;
    auto *findNextBtn = new QPushButton("Find &Next");     findNextBtn->setDefault(true);
    auto *findPrevBtn = new QPushButton("Find Pre&vious");
    auto *countBtn = new QPushButton("Coun&t");
    auto *findAllCurBtn = new QPushButton("Find All in Current");
    auto *findAllOpenBtn = new QPushButton("Find All in All &Opened");
    auto *closeBtn = new QPushButton("Close");

    for (auto *b : {findNextBtn, findPrevBtn, countBtn, findAllCurBtn, findAllOpenBtn, closeBtn})
        b->setFixedWidth(170);

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

    // Buttons
    auto *btnLay = new QVBoxLayout;
    auto *findBtn = new QPushButton("Find &Next");
    auto *replBtn = new QPushButton("&Replace");
    auto *replAllBtn = new QPushButton("Replace &All");
    auto *replAllOpenBtn = new QPushButton("Replace All in All &Opened");
    auto *closeBtn = new QPushButton("Close");

    for (auto *b : {findBtn, replBtn, replAllBtn, replAllOpenBtn, closeBtn})
        b->setFixedWidth(200);

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
    m_fifDirectory->addItem(QDir::homePath());
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
        if (!dir.isEmpty()) m_fifDirectory->setCurrentText(dir);
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
        m_resultLabel->setText("Marks cleared");
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
        m_resultLabel->setText("No editor tab active");
        return;
    }
    QString text = comboText(m_findInput);
    if (text.isEmpty()) {
        m_resultLabel->setText("Type something to find");
        return;
    }

    // Add to history
    if (m_findInput->findText(text) < 0) m_findInput->insertItem(0, text);

    bool isRegex = m_modeRegex->isChecked();
    if (m_modeExtended->isChecked()) text = processExtended(text);

    bool found = e->findFirst(text, isRegex, m_matchCase->isChecked(),
                               m_wholeWord->isChecked(), m_wrapAround->isChecked(),
                               forward);
    if (!found) {
        m_resultLabel->setText("Not found");
    } else {
        m_resultLabel->setText("Found");
    }
}

void FindReplaceDialog::findNext() { doFindNext(true); }
void FindReplaceDialog::findPrevious() { doFindNext(false); }

void FindReplaceDialog::doCount() {
    auto *e = getEditor(); if (!e) return;
    QString needle = comboText(m_findInput);
    if (needle.isEmpty()) return;

    if (m_modeExtended->isChecked()) needle = processExtended(needle);

    size_t count = RustCore::countMatches(e->text(), needle,
                                           m_modeRegex->isChecked(),
                                           m_matchCase->isChecked());
    m_resultLabel->setText(QString("Count: %1 match(es)").arg(count));
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

    // Show results in bottom panel (like Notepad++)
    auto *sr = mw->searchResults();
    sr->clear();

    QString fileName = e->filePath().isEmpty() ? "Untitled" : e->filePath();
    QStringList lines = e->text().split('\n');

    // Map byte offsets to line numbers
    QVector<QPair<int, QString>> results;
    for (auto pos : positions) {
        int charPos = 0, lineNum = 0;
        for (int i = 0; i < lines.size(); i++) {
            int lineLen = lines[i].size() + 1;
            if (charPos + lineLen > (int)pos) { lineNum = i + 1; break; }
            charPos += lineLen;
        }
        if (lineNum > 0 && lineNum <= lines.size())
            results.append({lineNum, lines[lineNum - 1]});
    }

    sr->setHeader(needle, results.size(), 1);
    sr->addFileSection(fileName, results.size());
    for (const auto &r : results)
        sr->addResultLine(r.first, r.second, needle);

    // Show the panel
    sr->setVisible(true);
    mw->vertSplitter()->setSizes({500, 200});

    m_resultLabel->setText(QString("%1 hits").arg(positions.size()));
}

void FindReplaceDialog::doFindAllOpened() {
    auto *mw = mainWindow(); if (!mw) return;
    QString needle = comboText(m_findInput);
    if (needle.isEmpty()) return;

    if (m_modeExtended->isChecked()) needle = processExtended(needle);

    auto *sr = mw->searchResults();
    sr->clear();

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
        sr->addFileSection(name, positions.size());

        for (auto pos : positions) {
            int charPos = 0, lineNum = 0;
            for (int i = 0; i < lines.size(); i++) {
                int lineLen = lines[i].size() + 1;
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
    m_resultLabel->setText(QString("%1 hits in %2 files").arg(totalHits).arg(fileCount));
}

void FindReplaceDialog::doReplace() {
    auto *e = getEditor(); if (!e) return;
    QString repl = comboText(m_replInput);
    if (m_rModeExtended->isChecked()) repl = processExtended(repl);

    if (e->hasSelectedText()) {
        e->replace(repl);
    }
    // Find next
    QString text = comboText(m_replFindInput);
    if (m_rModeExtended->isChecked()) text = processExtended(text);
    e->findFirst(text, m_rModeRegex->isChecked(), m_rMatchCase->isChecked(),
                 m_rWholeWord->isChecked(), m_rWrapAround->isChecked(), true);
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

    // Use Rust core for fast replace
    QString result = RustCore::replaceAll(e->text(), findText, replText,
                                           m_rModeRegex->isChecked(),
                                           m_rMatchCase->isChecked());
    size_t count = RustCore::countMatches(e->text(), findText,
                                           m_rModeRegex->isChecked(),
                                           m_rMatchCase->isChecked());
    e->selectAll();
    e->replaceSelectedText(result);
    m_resultsOutput->clear();
    m_resultsOutput->append(QString("Replaced %1 occurrence(s)").arg(count));
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

    int totalReplaced = 0;
    m_resultsOutput->clear();

    for (int t = 0; t < tabs->count(); t++) {
        auto *ed = qobject_cast<Editor *>(tabs->widget(t));
        if (!ed || ed->isReadOnly()) continue;

        size_t count = RustCore::countMatches(ed->text(), findText,
                                               m_rModeRegex->isChecked(),
                                               m_rMatchCase->isChecked());
        if (count == 0) continue;

        QString result = RustCore::replaceAll(ed->text(), findText, replText,
                                               m_rModeRegex->isChecked(),
                                               m_rMatchCase->isChecked());
        ed->selectAll();
        ed->replaceSelectedText(result);
        totalReplaced += count;

        QString name = ed->filePath().isEmpty() ? tabs->tabText(t) : QFileInfo(ed->filePath()).fileName();
        m_resultsOutput->append(QString("  %1: %2 replacement(s)").arg(name).arg(count));
    }

    m_resultsOutput->insertPlainText(QString("Replace All in All Opened: %1 total replacement(s)\n\n").arg(totalReplaced));
}

void FindReplaceDialog::doFindInFiles() {
    QString needle = comboText(m_fifFindInput);
    QString dir = comboText(m_fifDirectory);
    QString filterStr = comboText(m_fifFilters);
    if (needle.isEmpty() || dir.isEmpty()) return;

    m_resultsOutput->clear();
    m_resultsOutput->append(QString("Searching for \"%1\" in %2...").arg(needle, dir));
    m_resultsOutput->append("");

    QStringList filters;
    for (const auto &f : filterStr.split(' ', Qt::SkipEmptyParts))
        filters << f;
    if (filters.isEmpty()) filters << "*.*";

    QDir::Filters dirFilters = QDir::Files | QDir::NoDotAndDotDot;
    if (m_fifHidden->isChecked()) dirFilters |= QDir::Hidden;

    QDirIterator::IteratorFlags iterFlags;
    if (m_fifSubfolders->isChecked()) iterFlags = QDirIterator::Subdirectories;

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

        size_t count = RustCore::countMatches(content, needle,
                                               m_fifRegex->isChecked(),
                                               m_fifMatchCase->isChecked());
        if (count == 0) continue;

        fileCount++;
        totalHits += count;

        m_resultsOutput->append(QString("  %1 (%2 hits)").arg(filePath).arg(count));

        // Show first few matching lines
        QStringList lines = content.split('\n');
        int shown = 0;
        for (int i = 0; i < lines.size() && shown < 5; i++) {
            bool match;
            if (m_fifRegex->isChecked()) {
                QRegularExpression re(needle, m_fifMatchCase->isChecked() ? QRegularExpression::NoPatternOption
                                                                         : QRegularExpression::CaseInsensitiveOption);
                match = re.match(lines[i]).hasMatch();
            } else if (m_fifMatchCase->isChecked()) {
                match = lines[i].contains(needle);
            } else {
                match = lines[i].contains(needle, Qt::CaseInsensitive);
            }
            if (match) {
                m_resultsOutput->append(QString("    Line %1: %2").arg(i + 1).arg(lines[i].trimmed().left(120)));
                shown++;
            }
        }
        m_resultsOutput->append("");

        if (fileCount >= 1000) {
            m_resultsOutput->append("... (stopped after 1000 files)");
            break;
        }
    }

    m_resultsOutput->insertPlainText(QString("\nSearch complete: %1 hits in %2 files\n").arg(totalHits).arg(fileCount));
}

void FindReplaceDialog::doMarkAll() {
    auto *e = getEditor(); if (!e) return;
    QString needle = comboText(m_markFindInput);
    if (needle.isEmpty()) return;

    // Setup indicator
    e->SendScintilla(QsciScintilla::SCI_INDICSETSTYLE, 8, QsciScintilla::INDIC_ROUNDBOX);
    e->SendScintilla(QsciScintilla::SCI_INDICSETFORE, 8, 0x0000FF);
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
    int needleLen = needle.toUtf8().size();

    for (auto pos : positions) {
        e->SendScintilla(QsciScintilla::SCI_INDICATORFILLRANGE, (int)pos, needleLen);
    }

    m_resultLabel->setText(QString("Marked %1 occurrence(s)").arg(positions.size()));
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
