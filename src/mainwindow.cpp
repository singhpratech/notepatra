#include "mainwindow.h"
#include "editor.h"
#include "preferences.h"
#include "rustbridge.h"

#include <QApplication>
#include <QClipboard>
#include <QCloseEvent>
#include <QInputDialog>
#include <QProcess>
#include <QPrinter>
#include <QPrintDialog>
#include <QTextDocument>
#include <QTextStream>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QTimer>
#include "compare.h"
#include "hexeditor.h"
#include "gitgutter.h"
#include "fmtpanel.h"
#include "ollama.h"
#include "ollamastatus.h"
#include <QRegularExpression>
#include <QFileDialog>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QVBoxLayout>
#include <QMenuBar>
#include <QMessageBox>
#include <QScreen>
#include <QShortcut>
#include <QSplitter>
#include <QToolBar>
#include <QMimeData>
#include <QDragEnterEvent>
#include <QDropEvent>

MainWindow::MainWindow() {
    setWindowTitle("new 1 - Notepatra");
    setMinimumSize(640, 480);
    setAcceptDrops(true);  // Enable drag-and-drop

    // ── Central layout ──
    auto *central = new QWidget;
    auto *centralLayout = new QVBoxLayout(central);
    centralLayout->setContentsMargins(0, 0, 0, 0);
    centralLayout->setSpacing(0);

    // Vertical splitter: editor area (top) + search results (bottom)
    m_vertSplitter = new QSplitter(Qt::Vertical);
    centralLayout->addWidget(m_vertSplitter, 1);

    m_splitter = new QSplitter(Qt::Horizontal);
    m_vertSplitter->addWidget(m_splitter);

    // Search results panel — hidden until Find All is used
    m_searchResults = new SearchResultsPanel;
    m_searchResults->setVisible(false);
    m_searchResults->setMinimumHeight(80);
    m_vertSplitter->addWidget(m_searchResults);
    m_vertSplitter->setSizes({700, 0});

    // Double-click search result → jump to that line in editor
    connect(m_searchResults, &SearchResultsPanel::resultDoubleClicked, this, [this](const QString &file, int line) {
        // Find the tab with this file, or open it
        if (!file.isEmpty()) {
            bool found = false;
            for (int i = 0; i < m_tabs->count(); i++) {
                auto *ed = m_tabs->editorAt(i);
                if (ed && ed->filePath() == file) {
                    m_tabs->setCurrentIndex(i);
                    ed->gotoLine(line);
                    found = true;
                    break;
                }
            }
            if (!found) {
                openFile(file);
                if (auto *ed = m_tabs->currentEditor()) ed->gotoLine(line);
            }
        } else {
            // Same file — just jump to line
            if (auto *ed = m_tabs->currentEditor()) ed->gotoLine(line);
        }
    });

    // File explorer (hidden by default)
    m_explorer = new FileExplorer;
    m_explorer->setMinimumWidth(180);
    m_explorer->setMaximumWidth(400);
    m_explorer->setVisible(false);
    m_splitter->addWidget(m_explorer);

    connect(m_explorer, &FileExplorer::fileOpenRequested, this, &MainWindow::openFile);

    // Center: tabs
    m_tabs = new TabManager;
    m_splitter->addWidget(m_tabs);

    connect(m_tabs, &QTabWidget::tabCloseRequested, this, &MainWindow::closeTab);
    connect(m_tabs, &QTabWidget::currentChanged, this, [this](int) {
        updateTitle();
        updateStatusBar();
    });
    connect(m_tabs, &TabManager::tabContextNew, this, [this]() { newFile(); });
    connect(m_tabs, &TabManager::tabContextClose, this, [this](int idx) { closeTab(idx); });
    connect(m_tabs, &TabManager::tabContextCloseAll, this, [this]() {
        while (m_tabs->count() > 0) closeTab(0);
    });
    connect(m_tabs, &TabManager::tabContextCloseOthers, this, [this](int keep) {
        for (int i = m_tabs->count() - 1; i >= 0; i--) if (i != keep) closeTab(i);
    });
    connect(m_tabs, &TabManager::tabContextCloseLeft, this, [this](int idx) {
        for (int i = idx - 1; i >= 0; i--) closeTab(i);
    });
    connect(m_tabs, &TabManager::tabContextCloseRight, this, [this](int idx) {
        for (int i = m_tabs->count() - 1; i > idx; i--) closeTab(i);
    });
    connect(m_tabs, &TabManager::tabContextSave, this, [this](int idx) {
        auto *ed = m_tabs->editorAt(idx);
        if (ed && !ed->filePath().isEmpty()) { ed->saveFile(); updateTabTitle(idx); }
        else saveFileAs();
    });
    connect(m_tabs, &TabManager::tabContextSaveAs, this, [this](int) { saveFileAs(); });
    connect(m_tabs, &TabManager::tabContextRename, this, [this](int idx) {
        auto *ed = m_tabs->editorAt(idx);
        if (!ed || ed->filePath().isEmpty()) return;
        bool ok;
        QString name = QInputDialog::getText(this, "Rename", "New filename:",
            QLineEdit::Normal, QFileInfo(ed->filePath()).fileName(), &ok);
        if (ok && !name.isEmpty()) {
            QString newPath = QFileInfo(ed->filePath()).dir().filePath(name);
            QFile::rename(ed->filePath(), newPath);
            ed->saveFile(newPath);
            m_tabs->setTabText(idx, name);
            m_tabs->setTabToolTip(idx, newPath);
            updateTitle();
        }
    });

    // Function list (hidden by default)
    m_funcList = new FunctionList;
    m_funcList->setMinimumWidth(180);
    m_funcList->setMaximumWidth(350);
    m_funcList->setVisible(false);
    m_splitter->addWidget(m_funcList);

    connect(m_funcList, &FunctionList::navigateRequested, this, [this](int line) {
        if (auto *e = currentEditor()) e->gotoLine(line);
    });

    // All features (Terminal, AI, Git, SQL Fmt, REST, Markdown) open as new tabs — no panels

    // Status bar
    m_statusBar = new NppStatusBar;
    centralLayout->addWidget(m_statusBar);

    setCentralWidget(central);

    // ── Menus, toolbar, shortcuts ──
    buildMenus();
    buildToolbar();
    setupShortcuts();

    // Apply theme from saved config
    {
        auto themes = allThemes();
        QString themeName = Config::instance().theme;
        if (themes.contains(themeName))
            applyThemeToAll(themes[themeName]);
        else {
            m_statusBar->applyColors("#C8C8C8", "#000000", "#666666");
        }
    }

    // Open first empty tab
    newFile();

    // Restore window geometry from config
    {
        auto &cfg = Config::instance();
        if (cfg.maximized) {
            showMaximized();
        } else if (cfg.windowW > 100 && cfg.windowH > 100) {
            resize(cfg.windowW, cfg.windowH);
            if (cfg.windowX >= 0) move(cfg.windowX, cfg.windowY);
        } else {
            if (auto *screen = QApplication::primaryScreen()) {
                QRect avail = screen->availableGeometry();
                int w = avail.width() * 80 / 100;
                int h = avail.height() * 80 / 100;
                resize(w, h);
                move((avail.width() - w) / 2, (avail.height() - h) / 2);
            }
        }
    }

    // Check for crash recovery first
    checkCrashRecovery();

    // Restore previous session (open files from last time)
    restoreSession();

    // Auto-save session every 10 seconds + recovery every 30 seconds
    m_autoSaveTimer = new QTimer(this);
    connect(m_autoSaveTimer, &QTimer::timeout, this, [this]() {
        saveSession();
        autoSaveRecovery();
        checkFileChanges();
        // Persist window geometry + config
        auto &cfg = Config::instance();
        cfg.windowX = x(); cfg.windowY = y();
        cfg.windowW = width(); cfg.windowH = height();
        cfg.maximized = isMaximized();
        cfg.save();
    });
    m_autoSaveTimer->start(10000);  // every 10 seconds

    // File change watcher — detects external modifications
    setupFileWatcher();
}

Editor *MainWindow::currentEditor() {
    return m_tabs->currentEditor();
}

// ── File operations ──

Editor *MainWindow::newFile() {
    m_newCount++;
    auto *editor = new Editor(this);
    int idx = m_tabs->addTab(editor, QString("new %1").arg(m_newCount));
    m_tabs->setCurrentIndex(idx);

    connect(editor, &QsciScintilla::modificationChanged, this, [this, editor](bool) {
        updateTabTitle(m_tabs->indexOf(editor));
    });
    connect(editor, &Editor::cursorPositionUpdated, this, [this](int line, int col) {
        m_statusBar->updatePosition(line, col);
    });
    connect(editor, &QsciScintilla::textChanged, this, [this]() {
        if (auto *e = currentEditor()) {
            m_statusBar->updateLines(e->lines());
            m_statusBar->updateLength(e->text().length());
        }
    });

    return editor;
}

void MainWindow::openFile(const QString &path) {
    if (path.isEmpty() || !QFileInfo(path).isFile()) return;

    // Check if already open
    for (int i = 0; i < m_tabs->count(); i++) {
        auto *ed = m_tabs->editorAt(i);
        if (ed && ed->filePath() == QFileInfo(path).absoluteFilePath()) {
            m_tabs->setCurrentIndex(i);
            return;
        }
    }

    auto *editor = new Editor(this);
    if (!editor->loadFile(path)) {
        delete editor;
        return;
    }

    int idx = m_tabs->addTab(editor, QFileInfo(path).fileName());
    m_tabs->setTabToolTip(idx, path);
    m_tabs->setCurrentIndex(idx);

    connect(editor, &QsciScintilla::modificationChanged, this, [this, editor](bool) {
        updateTabTitle(m_tabs->indexOf(editor));
    });
    connect(editor, &Editor::cursorPositionUpdated, this, [this](int line, int col) {
        m_statusBar->updatePosition(line, col);
    });
    connect(editor, &QsciScintilla::textChanged, this, [this]() {
        if (auto *e = currentEditor()) {
            m_statusBar->updateLines(e->lines());
            m_statusBar->updateLength(e->text().length());
        }
    });

    updateTitle();
    updateStatusBar();

    // Watch this file for external changes
    QString absPath = QFileInfo(path).absoluteFilePath();
    if (m_fileWatcher) {
        m_fileWatcher->addPath(absPath);
        m_fileTimestamps[absPath] = QFileInfo(absPath).lastModified();
    }

    // Add to recent files
    Config::instance().addRecent(absPath);
    Config::instance().save();
    updateRecentMenu();
}

void MainWindow::saveFile() {
    auto *e = currentEditor();
    if (!e) return;
    if (!e->filePath().isEmpty()) {
        e->saveFile();
        updateTabTitle(m_tabs->currentIndex());
    } else {
        saveFileAs();
    }
}

void MainWindow::saveFileAs() {
    auto *e = currentEditor();
    if (!e) return;
    QString path = QFileDialog::getSaveFileName(this, "Save As", QDir::homePath(), "All Files (*)");
    if (!path.isEmpty()) {
        e->saveFile(path);
        m_tabs->setTabText(m_tabs->currentIndex(), QFileInfo(path).fileName());
        m_tabs->setTabToolTip(m_tabs->currentIndex(), path);
        updateTitle();
    }
}

void MainWindow::closeTab(int index) {
    QWidget *widget = m_tabs->widget(index);
    if (!widget) return;

    // If it's an editor, check for unsaved changes
    auto *editor = qobject_cast<Editor *>(widget);
    if (editor && editor->isModified()) {
        QString name = m_tabs->tabText(index).remove(" *");
        auto result = QMessageBox::question(this, "Save",
            QString("Save changes to %1?").arg(name),
            QMessageBox::Save | QMessageBox::Discard | QMessageBox::Cancel);

        if (result == QMessageBox::Save) {
            if (!editor->filePath().isEmpty())
                editor->saveFile();
            else {
                QString path = QFileDialog::getSaveFileName(this, "Save File");
                if (!path.isEmpty()) editor->saveFile(path);
                else return;
            }
        } else if (result == QMessageBox::Cancel) {
            return;
        }
    }

    // Remove file from watcher if it's an editor
    if (editor && !editor->filePath().isEmpty() && m_fileWatcher) {
        m_fileWatcher->removePath(editor->filePath());
        m_fileTimestamps.remove(editor->filePath());
    }

    m_tabs->removeTab(index);
    delete widget;
    if (m_tabs->count() == 0) newFile();
}

// ── UI updates ──

void MainWindow::updateTitle() {
    auto *e = currentEditor();
    if (e && !e->filePath().isEmpty())
        setWindowTitle(e->filePath() + " - Notepatra");
    else if (e)
        setWindowTitle(m_tabs->tabText(m_tabs->currentIndex()) + " - Notepatra");
    else
        setWindowTitle("Notepatra");
}

void MainWindow::updateStatusBar() {
    auto *e = currentEditor();
    if (!e) return;
    int line, col;
    e->getCursorPosition(&line, &col);
    m_statusBar->updatePosition(line + 1, col + 1);
    m_statusBar->updateLanguage(e->language());
    m_statusBar->updateEncoding(e->encoding());
    m_statusBar->updateEol(e->eolModeName());
    m_statusBar->updateLines(e->lines());
    m_statusBar->updateLength(e->text().length());
}

void MainWindow::updateTabTitle(int index) {
    if (index < 0) return;
    auto *e = m_tabs->editorAt(index);
    if (!e) return;
    QString name = e->filePath().isEmpty()
                       ? QString("new %1").arg(index + 1)
                       : QFileInfo(e->filePath()).fileName();
    if (e->isModified()) name += " *";
    m_tabs->setTabText(index, name);
    updateTitle();
}

// ── Menus ──

void MainWindow::buildMenus() {
    auto *mb = menuBar();
    auto E = [this]() -> Editor* { return currentEditor(); };
    auto FD = [this]() -> FindReplaceDialog* {
        if (!m_findDialog) m_findDialog = new FindReplaceDialog(this);
        return m_findDialog;
    };

    // ═══ FILE ═══
    auto *file = mb->addMenu("&File");
    file->addAction("&New", this, [this]() { newFile(); }, QKeySequence("Ctrl+N"));
    file->addAction("&Open...", this, [this]() {
        for (const auto &p : QFileDialog::getOpenFileNames(this, "Open", QDir::homePath(), "All Files (*)"))
            openFile(p);
    }, QKeySequence("Ctrl+O"));
    file->addAction("Open Folder as Workspace...", this, [this]() {
        QString p = QFileDialog::getExistingDirectory(this, "Open Folder", QDir::homePath());
        if (!p.isEmpty()) { m_explorer->setRoot(p); m_explorer->setVisible(true); }
    });
    file->addAction("Reload from Disk", this, [E]() {
        if (auto *e = E(); e && !e->filePath().isEmpty()) e->loadFile(e->filePath());
    });
    file->addSeparator();
    file->addAction("&Save", this, [this]() { saveFile(); }, QKeySequence("Ctrl+S"));
    file->addAction("Save &As...", this, [this]() { saveFileAs(); }, QKeySequence("Ctrl+Shift+S"));
    file->addAction("Save a Copy As...", this, [E]() {
        if (auto *e = E()) {
            QString p = QFileDialog::getSaveFileName(nullptr, "Save a Copy As", QDir::homePath());
            if (!p.isEmpty()) { QFile f(p); if (f.open(QIODevice::WriteOnly)) { f.write(e->text().toUtf8()); } }
        }
    });
    file->addAction("Save All", this, [this]() {
        for (int i = 0; i < m_tabs->count(); i++) {
            auto *ed = m_tabs->editorAt(i);
            if (ed && ed->isModified() && !ed->filePath().isEmpty()) { ed->saveFile(); updateTabTitle(i); }
        }
    }, QKeySequence("Ctrl+Shift+S"));
    file->addSeparator();
    file->addAction("Rename...", this, [this, E]() {
        auto *e = E(); if (!e || e->filePath().isEmpty()) return;
        bool ok; QString name = QInputDialog::getText(this, "Rename", "New name:", QLineEdit::Normal,
            QFileInfo(e->filePath()).fileName(), &ok);
        if (ok && !name.isEmpty()) {
            QString newPath = QFileInfo(e->filePath()).dir().filePath(name);
            QFile::rename(e->filePath(), newPath);
            e->saveFile(newPath);
            m_tabs->setTabText(m_tabs->currentIndex(), name);
            updateTitle();
        }
    });
    file->addSeparator();
    file->addAction("&Close", this, [this]() { closeTab(m_tabs->currentIndex()); }, QKeySequence("Ctrl+W"));
    file->addAction("Close All", this, [this]() { while (m_tabs->count() > 0) closeTab(0); });
    file->addAction("Close All BUT This", this, [this]() {
        int cur = m_tabs->currentIndex();
        for (int i = m_tabs->count()-1; i >= 0; i--) if (i != cur) closeTab(i);
    });
    file->addAction("Close All to the Left", this, [this]() {
        int cur = m_tabs->currentIndex();
        for (int i = cur-1; i >= 0; i--) closeTab(i);
    });
    file->addAction("Close All to the Right", this, [this]() {
        int cur = m_tabs->currentIndex();
        for (int i = m_tabs->count()-1; i > cur; i--) closeTab(i);
    });
    file->addSeparator();
    file->addAction("&Print...", this, [E]() {
        if (auto *e = E()) {
            QPrinter printer; QPrintDialog dlg(&printer);
            if (dlg.exec() == QPrintDialog::Accepted) {
                QTextDocument doc(e->text());
                doc.print(&printer);
            }
        }
    }, QKeySequence("Ctrl+P"));
    file->addSeparator();
    m_recentMenu = file->addMenu("Recent &Files");
    updateRecentMenu();
    file->addSeparator();
    file->addAction("E&xit", this, &QMainWindow::close, QKeySequence("Alt+F4"));

    // ═══ EDIT ═══
    auto *edit = mb->addMenu("&Edit");
    edit->addAction("&Undo", this, [E]() { if (auto *e = E()) e->undo(); }, QKeySequence("Ctrl+Z"));
    edit->addAction("&Redo", this, [E]() { if (auto *e = E()) e->redo(); }, QKeySequence("Ctrl+Y"));
    edit->addSeparator();
    edit->addAction("Cu&t", this, [E]() { if (auto *e = E()) e->cut(); }, QKeySequence("Ctrl+X"));
    edit->addAction("&Copy", this, [E]() { if (auto *e = E()) e->copy(); }, QKeySequence("Ctrl+C"));
    edit->addAction("&Paste", this, [E]() { if (auto *e = E()) e->paste(); }, QKeySequence("Ctrl+V"));
    edit->addAction("&Delete", this, [E]() { if (auto *e = E(); e && e->hasSelectedText()) e->removeSelectedText(); });
    edit->addAction("Select &All", this, [E]() { if (auto *e = E()) e->selectAll(); }, QKeySequence("Ctrl+A"));
    edit->addSeparator();

    auto *copyClip = edit->addMenu("Copy to Clipboard");
    copyClip->addAction("Copy Full Path", this, [E]() {
        if (auto *e = E(); e && !e->filePath().isEmpty()) QApplication::clipboard()->setText(e->filePath());
    });
    copyClip->addAction("Copy Filename", this, [E]() {
        if (auto *e = E(); e && !e->filePath().isEmpty()) QApplication::clipboard()->setText(QFileInfo(e->filePath()).fileName());
    });
    copyClip->addAction("Copy Directory", this, [E]() {
        if (auto *e = E(); e && !e->filePath().isEmpty()) QApplication::clipboard()->setText(QFileInfo(e->filePath()).path());
    });
    edit->addSeparator();

    // Case
    auto *caseMenu = edit->addMenu("Convert Case to");
    caseMenu->addAction("&UPPERCASE", this, [E]() {
        if (auto *e = E(); e && e->hasSelectedText()) e->replaceSelectedText(RustCore::convertCase(e->selectedText(), 0));
    }, QKeySequence("Ctrl+Shift+U"));
    caseMenu->addAction("&lowercase", this, [E]() {
        if (auto *e = E(); e && e->hasSelectedText()) e->replaceSelectedText(RustCore::convertCase(e->selectedText(), 1));
    }, QKeySequence("Ctrl+U"));
    caseMenu->addAction("&Proper Case", this, [E]() {
        if (auto *e = E(); e && e->hasSelectedText()) e->replaceSelectedText(RustCore::convertCase(e->selectedText(), 2));
    });
    caseMenu->addAction("&Sentence case", this, [E]() {
        if (auto *e = E(); e && e->hasSelectedText()) e->replaceSelectedText(RustCore::convertCase(e->selectedText(), 3));
    });
    caseMenu->addAction("&iNVERT cASE", this, [E]() {
        if (auto *e = E(); e && e->hasSelectedText()) e->replaceSelectedText(RustCore::convertCase(e->selectedText(), 4));
    });

    // Line operations
    auto *lineOps = edit->addMenu("Line Opera&tions");
    lineOps->addAction("&Duplicate Current Line", this, [E]() { if (auto *e = E()) e->duplicateLine(); }, QKeySequence("Ctrl+D"));
    lineOps->addAction("D&elete Current Line", this, [E]() { if (auto *e = E()) e->deleteLine(); }, QKeySequence("Ctrl+Shift+K"));
    lineOps->addAction("Move Line &Up", this, [E]() { if (auto *e = E()) e->moveLineUp(); }, QKeySequence("Ctrl+Shift+Up"));
    lineOps->addAction("Move Line &Down", this, [E]() { if (auto *e = E()) e->moveLineDown(); }, QKeySequence("Ctrl+Shift+Down"));
    lineOps->addSeparator();
    lineOps->addAction("Sort Lexicographically &Ascending", this, [E]() {
        if (auto *e = E()) { e->selectAll(); e->replaceSelectedText(RustCore::sortLines(e->text(), 0)); }
    });
    lineOps->addAction("Sort Lexicographically &Descending", this, [E]() {
        if (auto *e = E()) { e->selectAll(); e->replaceSelectedText(RustCore::sortLines(e->text(), 1)); }
    });
    lineOps->addAction("Sort as &Integers Ascending", this, [E]() {
        if (auto *e = E()) { e->selectAll(); e->replaceSelectedText(RustCore::sortLines(e->text(), 2)); }
    });
    lineOps->addAction("Sort as Integers Descending", this, [E]() {
        if (auto *e = E()) { e->selectAll(); e->replaceSelectedText(RustCore::sortLines(e->text(), 3)); }
    });
    lineOps->addAction("Sort by &Length Ascending", this, [E]() {
        if (auto *e = E()) { e->selectAll(); e->replaceSelectedText(RustCore::sortLines(e->text(), 4)); }
    });
    lineOps->addAction("Sort by Length Descending", this, [E]() {
        if (auto *e = E()) { e->selectAll(); e->replaceSelectedText(RustCore::sortLines(e->text(), 5)); }
    });
    lineOps->addSeparator();
    lineOps->addAction("&Remove Duplicate Lines", this, [E]() {
        if (auto *e = E()) { e->selectAll(); e->replaceSelectedText(RustCore::removeDuplicates(e->text(), 0)); }
    });
    lineOps->addAction("Remove Consecutive Duplicate Lines", this, [E]() {
        if (auto *e = E()) { e->selectAll(); e->replaceSelectedText(RustCore::removeDuplicates(e->text(), 1)); }
    });
    lineOps->addAction("Remove Empty Lines", this, [E]() {
        if (auto *e = E()) { e->selectAll(); e->replaceSelectedText(RustCore::removeEmptyLines(e->text(), 0)); }
    });
    lineOps->addAction("Remove Blank Lines", this, [E]() {
        if (auto *e = E()) { e->selectAll(); e->replaceSelectedText(RustCore::removeEmptyLines(e->text(), 1)); }
    });
    lineOps->addSeparator();
    lineOps->addAction("&Join Lines", this, [E]() {
        if (auto *e = E(); e && e->hasSelectedText()) e->replaceSelectedText(RustCore::joinLines(e->selectedText(), " "));
    });
    lineOps->addAction("&Reverse Line Order", this, [E]() {
        if (auto *e = E()) { e->selectAll(); e->replaceSelectedText(RustCore::reverseLines(e->text())); }
    });

    // Comment
    auto *commentMenu = edit->addMenu("Comment/Uncomment");
    commentMenu->addAction("Toggle &Line Comment", this, [E]() { if (auto *e = E()) e->toggleComment(); }, QKeySequence("Ctrl+/"));

    // Blank operations
    auto *blankMenu = edit->addMenu("Blank Operations");
    blankMenu->addAction("Trim &Trailing Space", this, [E]() {
        if (auto *e = E()) { e->selectAll(); e->replaceSelectedText(RustCore::trimLines(e->text(), 0)); }
    });
    blankMenu->addAction("Trim &Leading Space", this, [E]() {
        if (auto *e = E()) { e->selectAll(); e->replaceSelectedText(RustCore::trimLines(e->text(), 1)); }
    });
    blankMenu->addAction("Trim &Both", this, [E]() {
        if (auto *e = E()) { e->selectAll(); e->replaceSelectedText(RustCore::trimLines(e->text(), 2)); }
    });
    blankMenu->addSeparator();
    blankMenu->addAction("TAB to &Space", this, [E]() {
        if (auto *e = E()) { e->selectAll(); e->replaceSelectedText(RustCore::convertWhitespace(e->text(), e->tabWidth(), 0)); }
    });
    blankMenu->addAction("Space to TA&B", this, [E]() {
        if (auto *e = E()) { e->selectAll(); e->replaceSelectedText(RustCore::convertWhitespace(e->text(), e->tabWidth(), 1)); }
    });

    // EOL conversion
    auto *eolMenu = edit->addMenu("EOL Conversion");
    eolMenu->addAction("Windows (CR LF)", this, [E]() {
        if (auto *e = E()) { e->setEolMode(QsciScintilla::EolWindows); e->convertEols(e->eolMode()); }
    });
    eolMenu->addAction("Unix (LF)", this, [E]() {
        if (auto *e = E()) { e->setEolMode(QsciScintilla::EolUnix); e->convertEols(e->eolMode()); }
    });
    eolMenu->addAction("Macintosh (CR)", this, [E]() {
        if (auto *e = E()) { e->setEolMode(QsciScintilla::EolMac); e->convertEols(e->eolMode()); }
    });

    // ═══ SEARCH ═══
    auto *search = mb->addMenu("&Search");
    search->addAction("&Find...", this, [FD]() { FD()->showFind(); }, QKeySequence("Ctrl+F"));
    search->addAction("Find in Files...", this, [FD]() { FD()->showFind(); }, QKeySequence("Ctrl+Shift+F"));
    search->addAction("Find &Next", this, [this, FD]() {
        if (m_findDialog && !m_findDialog->findInput()->currentText().isEmpty()) m_findDialog->findNext();
        else FD()->showFind();
    }, QKeySequence("F3"));
    search->addAction("Find &Previous", this, [this, FD]() {
        if (m_findDialog && !m_findDialog->findInput()->currentText().isEmpty()) m_findDialog->findPrevious();
        else FD()->showFind();
    }, QKeySequence("Shift+F3"));
    search->addSeparator();
    search->addAction("&Replace...", this, [FD]() { FD()->showReplace(); }, QKeySequence("Ctrl+H"));
    search->addSeparator();
    search->addAction("&Go to Line...", this, [FD]() { FD()->showGoto(); }, QKeySequence("Ctrl+G"));
    search->addAction("Go to Matching &Brace", this, [this, E]() {
        // Try our Editor first
        if (auto *e = E()) { e->goToMatchingBrace(); return; }
        // Try any focused QsciScintilla (formatter panels etc)
        auto *focused = QApplication::focusWidget();
        auto *sci = qobject_cast<QsciScintilla *>(focused);
        if (!sci) return;
        int pos = (int)sci->SendScintilla(QsciScintilla::SCI_GETCURRENTPOS);
        auto isBrace = [](int c) { return c=='('||c==')'||c=='['||c==']'||c=='{'||c=='}'||c=='<'||c=='>'; };
        int bp = -1;
        int ch = (int)sci->SendScintilla(QsciScintilla::SCI_GETCHARAT, (unsigned long)pos, (long)0);
        if (isBrace(ch)) bp = pos;
        else if (pos > 0) { ch = (int)sci->SendScintilla(QsciScintilla::SCI_GETCHARAT, (unsigned long)(pos-1), (long)0); if (isBrace(ch)) bp = pos-1; }
        if (bp < 0) return;
        int mp = (int)sci->SendScintilla(QsciScintilla::SCI_BRACEMATCH, (unsigned long)bp, (long)0);
        if (mp >= 0) {
            sci->SendScintilla(QsciScintilla::SCI_BRACEHIGHLIGHT, (unsigned long)bp, (long)mp);
            sci->SendScintilla(QsciScintilla::SCI_GOTOPOS, (unsigned long)mp);
        } else {
            sci->SendScintilla(QsciScintilla::SCI_BRACEBADLIGHT, (unsigned long)bp);
        }
    }, QKeySequence("Ctrl+B"));
    search->addSeparator();

    auto *bmMenu = search->addMenu("Bookmarks");
    bmMenu->addAction("Toggle Bookmark", this, [E]() {
        if (auto *e = E()) { int l, c; e->getCursorPosition(&l, &c);
            if (e->markersAtLine(l) & 1) e->markerDelete(l, 0); else e->markerAdd(l, 0); }
    }, QKeySequence("Ctrl+F2"));
    bmMenu->addAction("Next Bookmark", this, [E]() {
        if (auto *e = E()) { int l, c; e->getCursorPosition(&l, &c);
            int n = e->markerFindNext(l+1, 1); if (n < 0) n = e->markerFindNext(0, 1);
            if (n >= 0) e->gotoLine(n+1); }
    }, QKeySequence("F2"));
    bmMenu->addAction("Previous Bookmark", this, [E]() {
        if (auto *e = E()) { int l, c; e->getCursorPosition(&l, &c);
            int n = e->markerFindPrevious(l-1, 1); if (n < 0) n = e->markerFindPrevious(e->lines()-1, 1);
            if (n >= 0) e->gotoLine(n+1); }
    }, QKeySequence("Shift+F2"));
    bmMenu->addAction("Clear All Bookmarks", this, [E]() { if (auto *e = E()) e->markerDeleteAll(0); });

    // ═══ VIEW ═══
    auto *view = mb->addMenu("&View");
    view->addAction("Always on Top", this, [this]() {
        auto flags = windowFlags();
        setWindowFlags(flags ^ Qt::WindowStaysOnTopHint);
        show();
    })->setCheckable(true);
    view->addAction("Toggle Full-Screen Mode", this, [this]() {
        isFullScreen() ? showNormal() : showFullScreen();
    }, QKeySequence("F11"));
    view->addSeparator();

    auto *symMenu = view->addMenu("Show Symbol");
    symMenu->addAction("Show All Characters", this, [E]() {
        if (auto *e = E()) { e->toggleWhitespace(); e->toggleEol(); }
    })->setCheckable(true);
    symMenu->addSeparator();
    symMenu->addAction("Show Whitespace and TAB", this, [E]() {
        if (auto *e = E()) e->toggleWhitespace();
    })->setCheckable(true);
    symMenu->addAction("Show End of Line", this, [E]() {
        if (auto *e = E()) e->toggleEol();
    })->setCheckable(true);
    symMenu->addAction("Show Indent Guide", this, [E]() {
        if (auto *e = E()) e->setIndentationGuides(!e->indentationGuides());
    })->setCheckable(true);

    auto *zoomMenu = view->addMenu("Zoom");
    zoomMenu->addAction("Zoom In", this, [E]() { if (auto *e = E()) e->zoomIn(); }, QKeySequence("Ctrl+="));
    zoomMenu->addAction("Zoom Out", this, [E]() { if (auto *e = E()) e->zoomOut(); }, QKeySequence("Ctrl+-"));
    zoomMenu->addAction("Restore Default Zoom", this, [E]() { if (auto *e = E()) e->zoomTo(0); }, QKeySequence("Ctrl+0"));

    view->addSeparator();
    view->addAction("Word Wrap", this, [E]() { if (auto *e = E()) e->toggleWordWrap(); })->setCheckable(true);
    view->addSeparator();

    auto *foldMenu = view->addMenu("Fold All");
    foldMenu->addAction("Fold All", this, [E]() { if (auto *e = E()) e->foldAll(); }, QKeySequence("Alt+0"));
    foldMenu->addAction("Unfold All", this, [E]() { if (auto *e = E()) e->clearFolds(); }, QKeySequence("Alt+Shift+0"));

    view->addSeparator();
    view->addAction("Summary...", this, [E, this]() {
        auto *e = E(); if (!e) return;
        QMessageBox::information(this, "Summary",
            QString("Path: %1\nLines: %2\nLength: %3\nLanguage: %4\nEncoding: %5\nEOL: %6")
            .arg(e->filePath().isEmpty() ? "(unsaved)" : e->filePath())
            .arg(e->lines()).arg(e->text().length())
            .arg(e->language()).arg(e->encoding()).arg(e->eolModeName()));
    });
    view->addSeparator();

    auto *explorerAct = view->addAction("Folder as Workspace");
    explorerAct->setCheckable(true);
    explorerAct->setShortcut(QKeySequence("Ctrl+Shift+E"));
    connect(explorerAct, &QAction::triggered, this, [this, explorerAct]() {
        m_explorer->setVisible(!m_explorer->isVisible());
        explorerAct->setChecked(m_explorer->isVisible());
    });

    auto *funcAct = view->addAction("Function List");
    funcAct->setCheckable(true);
    connect(funcAct, &QAction::triggered, this, [this, funcAct]() {
        m_funcList->setVisible(!m_funcList->isVisible());
        funcAct->setChecked(m_funcList->isVisible());
        if (m_funcList->isVisible()) if (auto *e = currentEditor()) m_funcList->updateSymbols(e->text(), e->language());
    });

    // ═══ FEATURES ═══
    auto *feat = mb->addMenu("F&eatures");

    // --- AI Assistant ---
    auto *aiAct = feat->addAction("AI Assistant — Ollama      Ctrl+Shift+A");
    aiAct->setCheckable(true);
    aiAct->setShortcut(QKeySequence("Ctrl+Shift+A"));
    aiAct->setStatusTip("Opens AI Assistant in a new tab. Select code first. Requires: ollama serve");
    connect(aiAct, &QAction::triggered, this, [this, E]() {
        auto *panel = new AIPanel;
        if (E()) panel->setContext(
            E()->hasSelectedText() ? E()->selectedText() : E()->text(),
            E()->filePath(), E()->language());
        connect(panel, &AIPanel::insertText, this, [this](const QString &text) {
            if (auto *e = currentEditor()) e->insert(text);
        });
        connect(panel, &AIPanel::replaceSelection, this, [this](const QString &text) {
            if (auto *e = currentEditor(); e && e->hasSelectedText()) e->replaceSelectedText(text);
        });
        int idx = m_tabs->addTab(panel, "AI Assistant");
        m_tabs->setCurrentIndex(idx);
    });

    feat->addSeparator();

    // --- Terminal ---
    auto *termAct = feat->addAction("Terminal                  Ctrl+`");
    termAct->setCheckable(true);
    termAct->setShortcut(QKeySequence("Ctrl+`"));
    termAct->setStatusTip("Opens a terminal in a new tab.");
    connect(termAct, &QAction::triggered, this, [this, E]() {
        auto *term = new TerminalWidget;
        if (auto *e = E(); e && !e->filePath().isEmpty())
            term->setWorkingDirectory(QFileInfo(e->filePath()).path());
        int idx = m_tabs->addTab(term, "Terminal");
        m_tabs->setCurrentIndex(idx);
    });

    // --- Markdown Converter ---
    auto *mdMenu = feat->addMenu("Markdown Converter");
    mdMenu->addAction("Selection → Markdown Table", this, [E]() {
        auto *e = E(); if (!e || !e->hasSelectedText()) return;
        // Convert CSV/tab-separated to markdown table
        QStringList lines = e->selectedText().split("\n", Qt::SkipEmptyParts);
        if (lines.isEmpty()) return;
        QString result;
        for (int i = 0; i < lines.size(); i++) {
            QStringList cols = lines[i].split(QRegularExpression("[,\t]"));
            result += "| " + cols.join(" | ") + " |\n";
            if (i == 0) {
                result += "|";
                for (int j = 0; j < cols.size(); j++) result += " --- |";
                result += "\n";
            }
        }
        e->replaceSelectedText(result);
    });
    mdMenu->addAction("Selection → Markdown List", this, [E]() {
        auto *e = E(); if (!e || !e->hasSelectedText()) return;
        QStringList lines = e->selectedText().split("\n", Qt::SkipEmptyParts);
        QString result;
        for (const auto &l : lines) result += "- " + l.trimmed() + "\n";
        e->replaceSelectedText(result);
    });
    mdMenu->addAction("Selection → Markdown Code Block", this, [E]() {
        auto *e = E(); if (!e || !e->hasSelectedText()) return;
        e->replaceSelectedText("```\n" + e->selectedText() + "\n```");
    });
    mdMenu->addAction("Selection → Bold", this, [E]() {
        auto *e = E(); if (!e || !e->hasSelectedText()) return;
        e->replaceSelectedText("**" + e->selectedText() + "**");
    });
    mdMenu->addAction("Selection → Italic", this, [E]() {
        auto *e = E(); if (!e || !e->hasSelectedText()) return;
        e->replaceSelectedText("*" + e->selectedText() + "*");
    });
    mdMenu->addAction("Selection → Link", this, [E]() {
        auto *e = E(); if (!e || !e->hasSelectedText()) return;
        e->replaceSelectedText("[" + e->selectedText() + "](url)");
    });
    mdMenu->addAction("Selection → Heading", this, [E]() {
        auto *e = E(); if (!e || !e->hasSelectedText()) return;
        e->replaceSelectedText("## " + e->selectedText());
    });
    mdMenu->addAction("HTML → Markdown (strip tags)", this, [E]() {
        auto *e = E(); if (!e || !e->hasSelectedText()) return;
        QString text = e->selectedText();
        text.replace(QRegularExpression("<br\\s*/?>"), "\n");
        text.replace(QRegularExpression("<p>(.*?)</p>"), "\\1\n\n");
        text.replace(QRegularExpression("<strong>(.*?)</strong>"), "**\\1**");
        text.replace(QRegularExpression("<b>(.*?)</b>"), "**\\1**");
        text.replace(QRegularExpression("<em>(.*?)</em>"), "*\\1*");
        text.replace(QRegularExpression("<i>(.*?)</i>"), "*\\1*");
        text.replace(QRegularExpression("<h1>(.*?)</h1>"), "# \\1\n");
        text.replace(QRegularExpression("<h2>(.*?)</h2>"), "## \\1\n");
        text.replace(QRegularExpression("<h3>(.*?)</h3>"), "### \\1\n");
        text.replace(QRegularExpression("<a href=\"(.*?)\">(.*?)</a>"), "[\\2](\\1)");
        text.replace(QRegularExpression("<[^>]+>"), "");
        e->replaceSelectedText(text);
    });

    // --- REST Client ---
    auto *restAct = feat->addAction("REST Client (.http)       Ctrl+Shift+R");
    restAct->setCheckable(true);
    restAct->setShortcut(QKeySequence("Ctrl+Shift+R"));
    restAct->setStatusTip("Opens REST client in a new tab. Select HTTP request first.");
    connect(restAct, &QAction::triggered, this, [this, E]() {
        auto *rest = new RestClient;
        if (E() && E()->hasSelectedText()) rest->executeRequest(E()->selectedText());
        int idx = m_tabs->addTab(rest, "REST Client");
        m_tabs->setCurrentIndex(idx);
    });

    // --- Hex Editor ---
    feat->addAction("Hex Editor — View Binary", this, [this, E]() {
        auto *e = E();
        if (e && !e->filePath().isEmpty()) {
            auto *dlg = new HexEditorDialog(e->filePath(), this);
            dlg->show();
        } else {
            QMessageBox::information(this, "Hex Editor", "Save the file first to view in hex mode.");
        }
    });

    feat->addSeparator();

    // --- How features work ---
    feat->addAction("How AI Assistant Works...", this, [this]() {
        QMessageBox::information(this, "AI Assistant — How It Works",
            "AI ASSISTANT (Ollama Integration)\n\n"
            "Prerequisites:\n"
            "  1. Install Ollama:  curl -fsSL https://ollama.com/install.sh | sh\n"
            "  2. Pull a model:    ollama pull qwen3:8b\n"
            "  3. Start server:    ollama serve\n\n"
            "Usage:\n"
            "  1. Select code in the editor\n"
            "  2. Open AI panel:  Ctrl+Shift+A\n"
            "  3. Click an action:\n"
            "       Explain     — explains what the code does\n"
            "       Find Bugs   — spots issues and suggests fixes\n"
            "       Refactor    — rewrites code cleaner\n"
            "       Write Tests — generates unit tests\n"
            "       Add Comments — annotates the code\n"
            "       Generate Docs — adds docstrings/JSDoc\n"
            "       Optimize    — performance improvements\n"
            "       Translate   — converts to another language\n"
            "  4. Or type any custom prompt\n"
            "  5. Click 'Insert at Cursor' or 'Replace Selection'\n\n"
            "Models: qwen3:8b (default), llama3.2:3b, codellama:7b,\n"
            "        deepseek-coder:6.7b, mistral:7b, phi3:mini\n\n"
            "All processing is LOCAL. Nothing leaves your machine.");
    });

    feat->addAction("How REST Client Works...", this, [this]() {
        QMessageBox::information(this, "REST Client — How It Works",
            "REST CLIENT (.http files)\n\n"
            "Write HTTP requests in your editor:\n\n"
            "  GET https://api.github.com/users/octocat\n"
            "  Authorization: Bearer YOUR_TOKEN\n\n"
            "  ###\n\n"
            "  POST https://api.example.com/data\n"
            "  Content-Type: application/json\n\n"
            "  {\"name\": \"test\", \"value\": 42}\n\n"
            "Usage:\n"
            "  1. Select the request block\n"
            "  2. Ctrl+Shift+R to open REST panel\n"
            "  3. Response appears with headers + body\n"
            "  4. JSON is auto-pretty-printed\n\n"
            "Supports: GET, POST, PUT, DELETE, PATCH, HEAD\n"
            "### separates multiple requests in one file");
    });

    // ═══ ENCODING ═══
    auto *enc = mb->addMenu("E&ncoding");
    for (const auto &name : {"ANSI", "UTF-8", "UTF-8-BOM", "UTF-16 BE BOM", "UTF-16 LE BOM"})
        enc->addAction(name)->setCheckable(true);

    // ═══ LANGUAGE — 45 languages ═══
    auto *lang = mb->addMenu("&Language");
    lang->addAction("Normal Text", this, [this, E]() { if (auto *e = E()) { e->setLanguage("Plain Text"); m_statusBar->updateLanguage("Plain Text"); } });
    lang->addSeparator();

    // Common languages
    for (const auto &l : {"Python", "JavaScript", "C", "C++", "C#", "Java", "HTML", "CSS",
                          "JSON", "XML", "SQL", "Bash", "Ruby", "Perl", "Lua", "YAML", "Markdown"}) {
        lang->addAction(l, this, [this, E, l]() { if (auto *e = E()) { e->setLanguage(l); m_statusBar->updateLanguage(l); } });
    }
    lang->addSeparator();

    // SQL variants submenu
    auto *sqlMenu = lang->addMenu("SQL Variants");
    for (const auto &l : {"SQL"}) {
        sqlMenu->addAction("SQL (ANSI / Generic)", this, [this, E]() { if (auto *e = E()) { e->setLanguage("SQL"); m_statusBar->updateLanguage("SQL"); } });
        sqlMenu->addAction("T-SQL (SQL Server)", this, [this, E]() { if (auto *e = E()) { e->setLanguage("SQL"); m_statusBar->updateLanguage("T-SQL (SQL Server)"); } });
        sqlMenu->addAction("PL/SQL (Oracle)", this, [this, E]() { if (auto *e = E()) { e->setLanguage("SQL"); m_statusBar->updateLanguage("PL/SQL (Oracle)"); } });
        sqlMenu->addAction("MySQL", this, [this, E]() { if (auto *e = E()) { e->setLanguage("SQL"); m_statusBar->updateLanguage("MySQL"); } });
        sqlMenu->addAction("PostgreSQL", this, [this, E]() { if (auto *e = E()) { e->setLanguage("SQL"); m_statusBar->updateLanguage("PostgreSQL"); } });
        sqlMenu->addAction("SQLite", this, [this, E]() { if (auto *e = E()) { e->setLanguage("SQL"); m_statusBar->updateLanguage("SQLite"); } });
    }
    lang->addSeparator();

    // More languages submenu
    auto *moreLang = lang->addMenu("More Languages");
    for (const auto &l : {"ASM", "AVS", "Batch", "CMake", "CoffeeScript", "D", "Diff",
                          "Fortran", "Fortran77", "IDL", "IntelHex", "Makefile", "MASM",
                          "Matlab", "NASM", "Octave", "Pascal", "PO", "PostScript", "POV",
                          "Properties", "Spice", "SRecord", "TCL", "TeX", "Verilog", "VHDL"}) {
        moreLang->addAction(l, this, [this, E, l]() { if (auto *e = E()) { e->setLanguage(l); m_statusBar->updateLanguage(l); } });
    }

    // ═══ SETTINGS ═══
    auto *settings = mb->addMenu("&Settings");
    settings->addAction("&Preferences...", this, [this]() { PreferencesDialog dlg(this); dlg.exec(); });
    settings->addSeparator();

    // Theme selector
    auto *themeMenu = settings->addMenu("&Theme");
    themeMenu->addAction("Light", this, [this]() {
        Config::instance().theme = "Light";
        Config::instance().save();
        applyThemeToAll(lightTheme());
    });
    themeMenu->addAction("Dark", this, [this]() {
        Config::instance().theme = "Dark";
        Config::instance().save();
        applyThemeToAll(darkTheme());
    });
    themeMenu->addAction("Monokai", this, [this]() {
        Config::instance().theme = "Monokai";
        Config::instance().save();
        applyThemeToAll(monokaiTheme());
    });
    settings->addSeparator();
    auto *tabMenu = settings->addMenu("Tab Settings");
    tabMenu->addAction("Use Spaces", this, [E]() { if (auto *e = E()) e->setIndentationsUseTabs(false); });
    tabMenu->addAction("Use Tabs", this, [E]() { if (auto *e = E()) e->setIndentationsUseTabs(true); });
    tabMenu->addSeparator();
    for (int s : {2, 4, 8})
        tabMenu->addAction(QString("Tab Width: %1").arg(s), this, [E, s]() { if (auto *e = E()) e->setTabWidth(s); });

    // ═══ TOOLS ═══
    auto *tools = mb->addMenu("&Tools");
    auto *hashMenu = tools->addMenu("Hash");
    for (auto &[name, algo] : std::initializer_list<std::pair<const char*, int>>{{"MD5", 0}, {"SHA-1", 1}, {"SHA-256", 2}, {"SHA-512", 3}}) {
        hashMenu->addAction(name, this, [this, E, algo]() {
            if (auto *e = E()) {
                QByteArray d = e->hasSelectedText() ? e->selectedText().toUtf8() : e->text().toUtf8();
                QString h = RustCore::computeHash(d, algo);
                QMessageBox::information(this, "Hash", h);
                QApplication::clipboard()->setText(h);
            }
        });
    }
    tools->addSeparator();
    tools->addAction("Base64 Encode", this, [E]() {
        if (auto *e = E(); e && e->hasSelectedText()) e->replaceSelectedText(RustCore::base64Encode(e->selectedText().toUtf8()));
    });
    tools->addAction("Base64 Decode", this, [E]() {
        if (auto *e = E(); e && e->hasSelectedText()) e->replaceSelectedText(RustCore::base64Decode(e->selectedText().toUtf8()));
    });
    tools->addSeparator();
    tools->addAction("URL Encode", this, [E]() {
        if (auto *e = E(); e && e->hasSelectedText()) e->replaceSelectedText(RustCore::urlEncode(e->selectedText()));
    });
    tools->addAction("URL Decode", this, [E]() {
        if (auto *e = E(); e && e->hasSelectedText()) e->replaceSelectedText(RustCore::urlDecode(e->selectedText()));
    });
    tools->addSeparator();

    // SQL Formatter
    tools->addAction("Format SQL", this, [E]() {
        if (auto *e = E()) {
            QString input = e->hasSelectedText() ? e->selectedText() : e->text();
            QString formatted = RustCore::formatSql(input, 4, true);
            if (e->hasSelectedText()) e->replaceSelectedText(formatted);
            else { e->selectAll(); e->replaceSelectedText(formatted); }
        }
    });
    tools->addAction("Format SQL (lowercase)", this, [E]() {
        if (auto *e = E()) {
            QString input = e->hasSelectedText() ? e->selectedText() : e->text();
            QString formatted = RustCore::formatSql(input, 4, false);
            if (e->hasSelectedText()) e->replaceSelectedText(formatted);
            else { e->selectAll(); e->replaceSelectedText(formatted); }
        }
    });
    tools->addSeparator();

    // Compare
    tools->addAction("Compare with File...", this, [this, E]() {
        auto *e = E(); if (!e) return;
        QString path = QFileDialog::getOpenFileName(this, "Select file to compare", QDir::homePath());
        if (path.isEmpty()) return;
        QFile file(path);
        if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) return;
        QString rightText = QTextStream(&file).readAll();
        QString leftName = e->filePath().isEmpty() ? "Current" : QFileInfo(e->filePath()).fileName();
        auto *dlg = new CompareDialog(e->text(), leftName, rightText, QFileInfo(path).fileName(), this);
        dlg->show();
    });
    tools->addAction("Compare Two Open Tabs...", this, [this]() {
        if (m_tabs->count() < 2) {
            QMessageBox::information(this, "Compare", "Need at least 2 open tabs to compare.");
            return;
        }
        // Compare current tab with next tab
        int cur = m_tabs->currentIndex();
        int other = (cur + 1) % m_tabs->count();
        auto *left = m_tabs->editorAt(cur);
        auto *right = m_tabs->editorAt(other);
        if (!left || !right) return;
        QString leftName = left->filePath().isEmpty() ? m_tabs->tabText(cur) : QFileInfo(left->filePath()).fileName();
        QString rightName = right->filePath().isEmpty() ? m_tabs->tabText(other) : QFileInfo(right->filePath()).fileName();
        auto *dlg = new CompareDialog(left->text(), leftName, right->text(), rightName, this);
        dlg->show();
    });

    // ═══ PLUGINS ═══
    // Each plugin = checkbox. Check = opens its panel. Inbuilt = grayed checkbox (always available).
    auto *pluginsMenu = mb->addMenu("Pl&ugins");
    QString pluginDir = QDir::homePath() + "/.config/notepatra/plugins";
    m_pluginManager.loadPlugins(pluginDir);

    // SQL Formatter (inbuilt) — opens in a new tab
    pluginsMenu->addAction("SQL Formatter (inbuilt)", this, [this, E]() {
        auto *panel = new SqlFmtPanel;
        if (E()) panel->setInput(E()->hasSelectedText() ? E()->selectedText() : E()->text());
        int idx = m_tabs->addTab(panel, "SQL Formatter");
        m_tabs->setCurrentIndex(idx);
    });

    // Compare with file — opens as a new tab
    // Compare — pick two tabs or a file
    pluginsMenu->addAction("Compare (inbuilt)", this, [this, E]() {
        // Build list of open editor tabs
        QStringList tabNames;
        QVector<int> tabIndices;
        for (int i = 0; i < m_tabs->count(); i++) {
            auto *ed = m_tabs->editorAt(i);
            if (ed) {
                QString name = ed->filePath().isEmpty() ? m_tabs->tabText(i) : QFileInfo(ed->filePath()).fileName();
                tabNames << QString("%1: %2").arg(i + 1).arg(name);
                tabIndices << i;
            }
        }

        if (tabNames.size() < 1) {
            QMessageBox::information(this, "Compare", "Open at least 1 file first.");
            return;
        }

        // Add "Browse file..." option
        tabNames << "Browse file from disk...";

        // Pick LEFT
        bool ok1;
        QString leftPick = QInputDialog::getItem(this, "Compare — Select LEFT file",
            "Left side:", tabNames, 0, false, &ok1);
        if (!ok1) return;

        // Pick RIGHT
        bool ok2;
        QString rightPick = QInputDialog::getItem(this, "Compare — Select RIGHT file",
            "Right side:", tabNames, tabNames.size() > 1 ? 1 : 0, false, &ok2);
        if (!ok2) return;

        // Resolve left
        QString leftText, leftName;
        int leftIdx = tabNames.indexOf(leftPick);
        if (leftIdx >= 0 && leftIdx < tabIndices.size()) {
            auto *ed = m_tabs->editorAt(tabIndices[leftIdx]);
            leftText = ed->text();
            leftName = ed->filePath().isEmpty() ? m_tabs->tabText(tabIndices[leftIdx]) : QFileInfo(ed->filePath()).fileName();
        } else {
            QString path = QFileDialog::getOpenFileName(this, "Select LEFT file", QDir::homePath());
            if (path.isEmpty()) return;
            QFile f(path);
            if (!f.open(QIODevice::ReadOnly | QIODevice::Text)) return;
            leftText = QTextStream(&f).readAll();
            leftName = QFileInfo(path).fileName();
        }

        // Resolve right
        QString rightText, rightName;
        int rightIdx = tabNames.indexOf(rightPick);
        if (rightIdx >= 0 && rightIdx < tabIndices.size()) {
            auto *ed = m_tabs->editorAt(tabIndices[rightIdx]);
            rightText = ed->text();
            rightName = ed->filePath().isEmpty() ? m_tabs->tabText(tabIndices[rightIdx]) : QFileInfo(ed->filePath()).fileName();
        } else {
            QString path = QFileDialog::getOpenFileName(this, "Select RIGHT file", QDir::homePath());
            if (path.isEmpty()) return;
            QFile f(path);
            if (!f.open(QIODevice::ReadOnly | QIODevice::Text)) return;
            rightText = QTextStream(&f).readAll();
            rightName = QFileInfo(path).fileName();
        }

        auto *cmp = new CompareWidget;
        cmp->compare(leftText, leftName, rightText, rightName);
        int idx = m_tabs->addTab(cmp, "Compare");
        m_tabs->setCurrentIndex(idx);
    });

    // Git Integration (inbuilt) — opens Git panel in a new tab
    pluginsMenu->addAction("Git Integration (inbuilt)", this, [this, E]() {
        auto *panel = new GitPanel;
        connect(panel, &GitPanel::fileClicked, this, &MainWindow::openFile);
        if (auto *e = E(); e && !e->filePath().isEmpty()) {
            panel->refresh(e->filePath());
            e->updateGitGutter();
        }
        int idx = m_tabs->addTab(panel, "Git");
        m_tabs->setCurrentIndex(idx);
    });

    // JSON Tools (inbuilt) — opens as tab
    pluginsMenu->addAction("JSON Tools (inbuilt)", this, [this, E]() {
        auto *p = new FormatterPanel("JSON Tools", "JSON");
        p->addButton("Format", [](const QString &s) { return RustCore::formatJson(s, 4); });
        p->addButton("Minify", [](const QString &s) { return RustCore::minifyJson(s); });
        p->addButton("Fix + Format", [](const QString &s) {
            QString report = RustCore::fixJsonReport(s);
            QString fixed = RustCore::fixJson(s);
            QString formatted = RustCore::formatJson(fixed, 4);
            return "/* ═══ FIX REPORT ═══\n" + report + "\n═══════════════════ */\n\n" + formatted;
        });

        // Ollama status + model selector + AI Fix button
        auto *ollamaBar = new OllamaStatus(p);
        // Insert status bar into the panel layout (after buttons, before output)
        auto *panelLayout = p->layout();
        if (panelLayout) panelLayout->addWidget(ollamaBar);

        auto *aiBtn = new QPushButton("AI Fix (Ollama)");
        aiBtn->setFixedHeight(26);
        auto *btnLayout = p->findChild<QHBoxLayout *>();
        if (btnLayout) btnLayout->insertWidget(btnLayout->count() - 2, aiBtn);

        auto *ollama = new OllamaClient(p);

        connect(aiBtn, &QPushButton::clicked, p, [p, ollama, ollamaBar]() {
            QString input = p->inputText();
            if (input.isEmpty()) return;

            if (!ollamaBar->isAvailable()) {
                p->setOutput("Ollama is not running.\n\n"
                             "Setup:\n"
                             "  1. Install:  curl -fsSL https://ollama.com/install.sh | sh\n"
                             "  2. Pull:     ollama pull qwen3.5:9b\n"
                             "  3. Start:    ollama serve\n"
                             "  4. Click AI Fix again");
                return;
            }

            ollama->setModel(ollamaBar->selectedModel());
            p->setOutput("Asking " + ollamaBar->selectedModel() + "...");

            ollama->generate(
                "Fix this broken JSON. Return ONLY the valid JSON, nothing else. "
                "No explanation, no markdown, no code blocks.\n\n" + input,
                "You are a JSON repair tool. Return ONLY valid JSON. Preserve ALL data. "
                "Fix: missing braces, brackets, commas, quotes, unquoted keys.");
        });

        auto *firstToken = new bool(true);
        connect(ollama, &OllamaClient::tokenReceived, p, [p, aiBtn, firstToken](const QString &token) {
            if (*firstToken) { p->setOutput(""); *firstToken = false; }
            p->appendOutput(token);
            aiBtn->setText("AI Fixing...");
        });
        connect(ollama, &OllamaClient::finished, p, [p, aiBtn, firstToken](const QString &response) {
            aiBtn->setText("AI Fix (Ollama)");
            *firstToken = true;
            QString cleaned = response.trimmed();
            if (cleaned.startsWith("```")) {
                int f = cleaned.indexOf('\n'), l = cleaned.lastIndexOf("```");
                if (f > 0 && l > f) cleaned = cleaned.mid(f + 1, l - f - 1).trimmed();
            }
            QString formatted = RustCore::formatJson(cleaned, 4);
            p->setOutput(formatted.length() > 2 ? formatted : cleaned);
        });
        connect(ollama, &OllamaClient::error, p, [p, aiBtn, firstToken](const QString &msg) {
            aiBtn->setText("AI Fix (Ollama)");
            *firstToken = true;
            p->setOutput("Error: " + msg + "\n\nRun: ollama serve");
        });

        if (E()) p->setInput(E()->hasSelectedText() ? E()->selectedText() : E()->text());
        int idx = m_tabs->addTab(p, "JSON Tools");
        m_tabs->setCurrentIndex(idx);
    });

    // HTML Tools (inbuilt) — format, minify, fix, AI fix
    pluginsMenu->addAction("HTML Tools (inbuilt)", this, [this, E]() {
        auto *p = new FormatterPanel("HTML Tools", "HTML");
        p->addButton("Format (2 spaces)", [](const QString &s) { return RustCore::formatHtml(s, 2); });
        p->addButton("Format (4 spaces)", [](const QString &s) { return RustCore::formatHtml(s, 4); });
        p->addButton("Minify", [](const QString &s) {
            // Strip newlines and extra spaces between tags
            QString result = s;
            result.replace(QRegularExpression("\\s*\\n\\s*"), "");
            result.replace(QRegularExpression(">\\s+<"), "><");
            return result;
        });
        p->addButton("Fix + Format", [](const QString &s) {
            // Fix common HTML issues then format
            QString fixed = s;
            // Close unclosed self-closing tags
            QRegularExpression re_img("<img([^>]*[^/])>", QRegularExpression::CaseInsensitiveOption);
            fixed.replace(re_img, "<img\\1 />");
            QRegularExpression re_br("<br\\s*>", QRegularExpression::CaseInsensitiveOption);
            fixed.replace(re_br, "<br />");
            QRegularExpression re_hr("<hr\\s*>", QRegularExpression::CaseInsensitiveOption);
            fixed.replace(re_hr, "<hr />");
            QRegularExpression re_input("<input([^>]*[^/])>", QRegularExpression::CaseInsensitiveOption);
            fixed.replace(re_input, "<input\\1 />");
            QRegularExpression re_meta("<meta([^>]*[^/])>", QRegularExpression::CaseInsensitiveOption);
            fixed.replace(re_meta, "<meta\\1 />");
            QRegularExpression re_link("<link([^>]*[^/])>", QRegularExpression::CaseInsensitiveOption);
            fixed.replace(re_link, "<link\\1 />");

            // Count open/close tags to detect missing closers
            QString report = "/* ═══ HTML FIX REPORT ═══\n";
            QRegularExpression re_open("<([a-zA-Z][a-zA-Z0-9]*)(?:\\s[^>]*)?>");
            QRegularExpression re_close("</([a-zA-Z][a-zA-Z0-9]*)>");
            QStringList voidTags = {"br","hr","img","input","meta","link","area","base","col","embed","source","track","wbr"};

            QMap<QString, int> openCount, closeCount;
            auto openMatches = re_open.globalMatch(fixed);
            while (openMatches.hasNext()) {
                auto m = openMatches.next();
                QString tag = m.captured(1).toLower();
                if (!voidTags.contains(tag)) openCount[tag]++;
            }
            auto closeMatches = re_close.globalMatch(fixed);
            while (closeMatches.hasNext()) {
                auto m = closeMatches.next();
                closeCount[m.captured(1).toLower()]++;
            }
            int issues = 0;
            for (auto it = openCount.begin(); it != openCount.end(); ++it) {
                int diff = it.value() - closeCount.value(it.key(), 0);
                if (diff > 0) {
                    report += QString("Missing %1 </%2> closing tag(s)\n").arg(diff).arg(it.key());
                    // Add missing closing tags at the end
                    for (int i = 0; i < diff; i++) fixed += "</" + it.key() + ">";
                    issues++;
                }
            }
            if (issues == 0) report += "No issues found.\n";
            report += "═══════════════════════ */\n\n";

            return report + RustCore::formatHtml(fixed, 2);
        });

        // Ollama status + AI Fix
        auto *ollamaBar = new OllamaStatus(p);
        auto *panelLayout = p->layout();
        if (panelLayout) panelLayout->addWidget(ollamaBar);

        auto *aiBtn = new QPushButton("AI Fix (Ollama)");
        aiBtn->setFixedHeight(26);
        auto *btnLayout = p->findChild<QHBoxLayout *>();
        if (btnLayout) btnLayout->insertWidget(btnLayout->count() - 2, aiBtn);

        auto *ollama = new OllamaClient(p);

        connect(aiBtn, &QPushButton::clicked, p, [p, ollama, ollamaBar]() {
            QString input = p->inputText();
            if (input.isEmpty()) return;
            if (!ollamaBar->isAvailable()) {
                p->setOutput("Ollama is not running.\n\nSetup:\n  1. curl -fsSL https://ollama.com/install.sh | sh\n  2. ollama pull qwen3.5:9b\n  3. ollama serve");
                return;
            }
            ollama->setModel(ollamaBar->selectedModel());
            p->setOutput("Asking " + ollamaBar->selectedModel() + "...");
            ollama->generate(
                "Fix this broken HTML. Return ONLY valid HTML. No markdown, no explanation.\n\n" + input,
                "You are an HTML repair tool. Return ONLY fixed HTML. Preserve ALL content.");
        });

        auto *firstToken = new bool(true);
        connect(ollama, &OllamaClient::tokenReceived, p, [p, aiBtn, firstToken](const QString &token) {
            if (*firstToken) { p->setOutput(""); *firstToken = false; }
            p->appendOutput(token);
            aiBtn->setText("AI Fixing...");
        });
        connect(ollama, &OllamaClient::finished, p, [p, aiBtn, firstToken](const QString &response) {
            aiBtn->setText("AI Fix (Ollama)");
            *firstToken = true;
            QString cleaned = response.trimmed();
            if (cleaned.startsWith("```")) {
                int f = cleaned.indexOf('\n'), l = cleaned.lastIndexOf("```");
                if (f > 0 && l > f) cleaned = cleaned.mid(f + 1, l - f - 1).trimmed();
            }
            p->setOutput(RustCore::formatHtml(cleaned, 2));
        });
        connect(ollama, &OllamaClient::error, p, [p, aiBtn, firstToken](const QString &msg) {
            aiBtn->setText("AI Fix (Ollama)");
            *firstToken = true;
            p->setOutput("Error: " + msg + "\n\nRun: ollama serve");
        });

        if (E()) p->setInput(E()->hasSelectedText() ? E()->selectedText() : E()->text());
        int idx = m_tabs->addTab(p, "HTML Tools");
        m_tabs->setCurrentIndex(idx);
    });

    // Bracket Tools (inbuilt) — check, fix, AI fix
    pluginsMenu->addAction("Bracket Tools (inbuilt)", this, [this, E]() {
        auto *p = new FormatterPanel("Bracket Tools", "JavaScript");
        p->addButton("Check", [](const QString &s) { return RustCore::checkBrackets(s); });
        p->addButton("Auto-Fix", [](const QString &s) { return RustCore::fixBrackets(s); });

        // Ollama status + AI Fix
        auto *ollamaBar = new OllamaStatus(p);
        auto *panelLayout = p->layout();
        if (panelLayout) panelLayout->addWidget(ollamaBar);

        auto *aiBtn = new QPushButton("AI Fix (Ollama)");
        aiBtn->setFixedHeight(26);
        auto *btnLayout = p->findChild<QHBoxLayout *>();
        if (btnLayout) btnLayout->insertWidget(btnLayout->count() - 2, aiBtn);

        auto *ollama = new OllamaClient(p);

        connect(aiBtn, &QPushButton::clicked, p, [p, ollama, ollamaBar]() {
            QString input = p->inputText();
            if (input.isEmpty()) return;
            if (!ollamaBar->isAvailable()) {
                p->setOutput("Ollama not running.\n\nSetup:\n  1. curl -fsSL https://ollama.com/install.sh | sh\n  2. ollama pull qwen3.5:9b\n  3. ollama serve");
                return;
            }
            ollama->setModel(ollamaBar->selectedModel());
            p->setOutput("Asking " + ollamaBar->selectedModel() + " to fix brackets...");
            ollama->generate(
                "Fix ALL bracket issues in this code. Fix missing (), [], {}, matching begin/end, if/fi, do/done. "
                "Return ONLY the fixed code, nothing else. No explanation. No markdown.\n\n" + input,
                "You are a code bracket repair tool. Fix all mismatched and missing brackets, parentheses, braces. Preserve all code logic.");
        });

        auto *firstToken = new bool(true);
        connect(ollama, &OllamaClient::tokenReceived, p, [p, aiBtn, firstToken](const QString &token) {
            if (*firstToken) { p->setOutput(""); *firstToken = false; }
            p->appendOutput(token);
            aiBtn->setText("AI Fixing...");
        });
        connect(ollama, &OllamaClient::finished, p, [p, aiBtn, firstToken](const QString &response) {
            aiBtn->setText("AI Fix (Ollama)");
            *firstToken = true;
            QString cleaned = response.trimmed();
            if (cleaned.startsWith("```")) {
                int f = cleaned.indexOf('\n'), l = cleaned.lastIndexOf("```");
                if (f > 0 && l > f) cleaned = cleaned.mid(f + 1, l - f - 1).trimmed();
            }
            p->setOutput(cleaned);
        });
        connect(ollama, &OllamaClient::error, p, [p, aiBtn, firstToken](const QString &msg) {
            aiBtn->setText("AI Fix (Ollama)");
            *firstToken = true;
            p->setOutput("Error: " + msg + "\n\nRun: ollama serve");
        });

        if (E()) p->setInput(E()->hasSelectedText() ? E()->selectedText() : E()->text());
        int idx = m_tabs->addTab(p, "Bracket Tools");
        m_tabs->setCurrentIndex(idx);
    });

    pluginsMenu->addSeparator();

    // User plugins — just click to run
    for (int i = 0; i < m_pluginManager.plugins().size(); i++) {
        const auto &p = m_pluginManager.plugins()[i];
        pluginsMenu->addAction(QString("Run: %1 v%2").arg(p.name, p.version), this, [this, E, i]() {
            if (auto *e = E()) {
                QString input = e->hasSelectedText() ? e->selectedText() : e->text();
                QString output = m_pluginManager.runPlugin(i, input);
                if (e->hasSelectedText()) e->replaceSelectedText(output);
                else { e->selectAll(); e->replaceSelectedText(output); }
            }
        });
    }

    pluginsMenu->addSeparator();

    pluginsMenu->addAction("Open Plugins Folder", this, [pluginDir]() {
        QDir().mkpath(pluginDir);
        QProcess::startDetached("xdg-open", {pluginDir});
    });
    pluginsMenu->addAction("How to Write a Plugin...", this, [this, pluginDir]() {
        QMessageBox::information(this, "Write a Plugin",
            "Notepatra Plugin API\n\n"
            "Create myplugin.cpp:\n\n"
            "  extern \"C\" {\n"
            "    const char* notepatra_plugin_name() { return \"Name\"; }\n"
            "    char* notepatra_plugin_run(const char* text, int len) {\n"
            "      // transform text, return malloc'd result\n"
            "    }\n"
            "  }\n\n"
            "Compile:  g++ -shared -fPIC -o myplugin.so myplugin.cpp\n"
            "Drop in:  " + pluginDir + "/\n"
            "Restart Notepatra.");
    });

    // ═══ MACRO ═══
    auto *macro = mb->addMenu("&Macro");
    macro->addAction("Start Recording");
    macro->addAction("Stop Recording");
    macro->addSeparator();
    macro->addAction("Playback");
    macro->addAction("Run Multiple Times...");

    // ═══ RUN ═══
    auto *run = mb->addMenu("&Run");
    run->addAction("Run...", this, [this, E]() {
        bool ok; QString cmd = QInputDialog::getText(this, "Run", "Command:", QLineEdit::Normal, "", &ok);
        if (ok && !cmd.isEmpty()) {
            auto *e = E(); QString dir = (e && !e->filePath().isEmpty()) ? QFileInfo(e->filePath()).path() : QDir::homePath();
            QProcess::startDetached("sh", {"-c", cmd}, dir);
        }
    }, QKeySequence("F5"));
    run->addSeparator();
    run->addAction("Open Containing Folder", this, [E]() {
        if (auto *e = E(); e && !e->filePath().isEmpty()) QProcess::startDetached("xdg-open", {QFileInfo(e->filePath()).path()});
    });
    run->addAction("Open in Terminal", this, [E]() {
        if (auto *e = E(); e && !e->filePath().isEmpty())
            QProcess::startDetached("x-terminal-emulator", {}, QFileInfo(e->filePath()).path());
    });

    // ═══ WINDOW ═══
    auto *window = mb->addMenu("&Window");
    window->addAction("Windows...", this, [this]() {
        QStringList names;
        for (int i = 0; i < m_tabs->count(); i++) {
            auto *ed = m_tabs->editorAt(i);
            names << QString("%1. %2").arg(i+1).arg(ed && !ed->filePath().isEmpty() ? ed->filePath() : m_tabs->tabText(i));
        }
        QMessageBox::information(this, "Windows", names.join("\n"));
    });

    // ═══ ? (HELP) ═══
    auto *help = mb->addMenu("&?");

    help->addAction("Feature List", this, [this]() {
        QMessageBox::information(this, "Notepatra Features",
            "EDITOR\n"
            "  60+ languages with syntax highlighting\n"
            "  Tabbed editing, code folding, bookmarks\n"
            "  Auto-complete, brace matching, indent guides\n"
            "  Double-click word highlight (all occurrences)\n"
            "  Handles files up to 2.5 GB\n\n"
            "PANELS\n"
            "  Built-in Terminal              Ctrl+`\n"
            "  Markdown Preview               Ctrl+Shift+M\n"
            "  AI Assistant (Ollama)          Ctrl+Shift+A\n"
            "  REST Client (.http)            Ctrl+Shift+R\n"
            "  File Explorer / Workspace     Ctrl+Shift+E\n"
            "  Function List\n"
            "  Hex Editor\n\n"
            "SEARCH\n"
            "  Find / Replace                 Ctrl+F / Ctrl+H\n"
            "  Find in Files                  Ctrl+Shift+F\n"
            "  Find/Replace in All Opened Documents\n"
            "  Regular expressions, Extended mode\n"
            "  Mark All occurrences\n"
            "  Go to Line / Offset            Ctrl+G\n"
            "  Bookmarks                      Ctrl+F2 / F2\n\n"
            "BUNDLED PLUGINS\n"
            "  SQL Formatter (uppercase/lowercase)\n"
            "  File Compare (side-by-side diff)\n"
            "  Git Integration (changed lines, branch, GitHub)\n\n"
            "TOOLS\n"
            "  Hash: MD5, SHA-1, SHA-256, SHA-512\n"
            "  Base64 encode/decode\n"
            "  URL encode/decode\n"
            "  Case conversion (5 modes)\n"
            "  Line operations (sort, dedupe, trim, reverse)\n"
            "  Tab/Space conversion\n"
            "  EOL conversion (LF/CRLF/CR)\n\n"
            "PLUGIN SYSTEM\n"
            "  Drop .so files in ~/.config/notepatra/plugins/\n"
            "  Simple C API (2 functions to implement)");
    });

    help->addAction("Keyboard Shortcuts", this, [this]() {
        QMessageBox::information(this, "Keyboard Shortcuts",
            "FILE\n"
            "  Ctrl+N          New\n"
            "  Ctrl+O          Open\n"
            "  Ctrl+S          Save\n"
            "  Ctrl+Shift+S    Save As\n"
            "  Ctrl+W          Close\n"
            "  Ctrl+P          Print\n\n"
            "EDIT\n"
            "  Ctrl+Z          Undo\n"
            "  Ctrl+Y          Redo\n"
            "  Ctrl+D          Duplicate Line\n"
            "  Ctrl+Shift+K    Delete Line\n"
            "  Ctrl+Shift+Up   Move Line Up\n"
            "  Ctrl+Shift+Down Move Line Down\n"
            "  Ctrl+/          Toggle Comment\n"
            "  Ctrl+Shift+U    UPPERCASE\n"
            "  Ctrl+U          lowercase\n\n"
            "SEARCH\n"
            "  Ctrl+F          Find\n"
            "  Ctrl+H          Replace\n"
            "  Ctrl+Shift+F    Find in Files\n"
            "  F3              Find Next\n"
            "  Shift+F3        Find Previous\n"
            "  Ctrl+G          Go to Line\n"
            "  Ctrl+B          Go to Matching Brace\n"
            "  Ctrl+F2         Toggle Bookmark\n"
            "  F2              Next Bookmark\n\n"
            "VIEW\n"
            "  F11             Full Screen\n"
            "  Ctrl+=          Zoom In\n"
            "  Ctrl+-          Zoom Out\n"
            "  Ctrl+0          Zoom Reset\n"
            "  Alt+0           Fold All\n"
            "  Alt+Shift+0     Unfold All\n\n"
            "PANELS\n"
            "  Ctrl+`          Terminal\n"
            "  Ctrl+Shift+M    Markdown Preview\n"
            "  Ctrl+Shift+A    AI Assistant\n"
            "  Ctrl+Shift+R    REST Client\n"
            "  Ctrl+Shift+E    File Explorer\n\n"
            "TABS\n"
            "  Ctrl+Tab        Next Tab\n"
            "  Ctrl+Shift+Tab  Previous Tab\n"
            "  Middle-click    Close Tab\n"
            "  Double-click    New Tab (on empty area)");
    });

    help->addSeparator();

    help->addAction("About Notepatra", this, [this]() {
        QMessageBox::about(this, "About Notepatra",
            "Notepatra v0.1\n\n"
            "The first editor built for the AI era.\n"
            "A blazing-fast native code editor for Linux.\n\n"
            "4.7 MB binary. C++ + Rust. No Electron.\n"
            "60+ languages. Plugin system. 2.5 GB files.\n\n"
            "github.com/singhpratech/notepatra\n\n"
            "Envisioned by Prateek Singh.\n"
            "Inspired by Notepad++. Built by Claude.");
    });
}

void MainWindow::buildToolbar() {
    auto *tb = addToolBar("Main");
    tb->setMovable(false);
    tb->setFloatable(false);
    auto E = [this]() -> Editor* { return currentEditor(); };
    auto FD = [this]() -> FindReplaceDialog* {
        if (!m_findDialog) m_findDialog = new FindReplaceDialog(this);
        return m_findDialog;
    };

    // File operations
    tb->addAction("New", this, [this]() { newFile(); });
    tb->addAction("Open", this, [this]() {
        for (const auto &p : QFileDialog::getOpenFileNames(this, "Open", QDir::homePath())) openFile(p);
    });
    tb->addAction("Save", this, [this]() { saveFile(); });
    tb->addAction("Save All", this, [this]() {
        for (int i = 0; i < m_tabs->count(); i++) {
            auto *ed = m_tabs->editorAt(i);
            if (ed && ed->isModified() && !ed->filePath().isEmpty()) { ed->saveFile(); updateTabTitle(i); }
        }
    });
    tb->addAction("Close", this, [this]() { closeTab(m_tabs->currentIndex()); });
    tb->addSeparator();

    // Edit operations
    tb->addAction("Cut", this, [E]() { if (auto *e = E()) e->cut(); });
    tb->addAction("Copy", this, [E]() { if (auto *e = E()) e->copy(); });
    tb->addAction("Paste", this, [E]() { if (auto *e = E()) e->paste(); });
    tb->addSeparator();
    tb->addAction("Undo", this, [E]() { if (auto *e = E()) e->undo(); });
    tb->addAction("Redo", this, [E]() { if (auto *e = E()) e->redo(); });
    tb->addSeparator();

    // Search
    tb->addAction("Find", this, [FD]() { FD()->showFind(); });
    tb->addAction("Replace", this, [FD]() { FD()->showReplace(); });
    tb->addSeparator();

    // View
    tb->addAction("Zoom In", this, [E]() { if (auto *e = E()) e->zoomIn(); });
    tb->addAction("Zoom Out", this, [E]() { if (auto *e = E()) e->zoomOut(); });
    tb->addSeparator();
    tb->addAction("Word Wrap", this, [E]() { if (auto *e = E()) e->toggleWordWrap(); });
    tb->addAction("Show All", this, [E]() { if (auto *e = E()) { e->toggleWhitespace(); e->toggleEol(); } });
    tb->addSeparator();

    // Panels
    tb->addAction("Func List", this, [this]() {
        m_funcList->setVisible(!m_funcList->isVisible());
        if (m_funcList->isVisible()) if (auto *e = currentEditor()) m_funcList->updateSymbols(e->text(), e->language());
    });
    tb->addAction("Workspace", this, [this]() { m_explorer->setVisible(!m_explorer->isVisible()); });
}

void MainWindow::setupShortcuts() {
    new QShortcut(QKeySequence("Ctrl+Tab"), this, [this]() {
        int idx = (m_tabs->currentIndex() + 1) % qMax(m_tabs->count(), 1);
        m_tabs->setCurrentIndex(idx);
    });
    new QShortcut(QKeySequence("Ctrl+Shift+Tab"), this, [this]() {
        int idx = (m_tabs->currentIndex() - 1 + m_tabs->count()) % qMax(m_tabs->count(), 1);
        m_tabs->setCurrentIndex(idx);
    });
}

// ═══════════════════════════════════════
// Session persistence + crash recovery
// ═══════════════════════════════════════

QString MainWindow::sessionFilePath() {
    return QDir::homePath() + "/.config/notepatra/session.json";
}

QString MainWindow::recoveryDir() {
    return QDir::homePath() + "/.config/notepatra/recovery";
}

void MainWindow::saveSession() {
    QDir().mkpath(QFileInfo(sessionFilePath()).path());

    QJsonArray tabs;
    for (int i = 0; i < m_tabs->count(); i++) {
        auto *e = m_tabs->editorAt(i);
        if (!e || e->filePath().isEmpty()) continue;

        QJsonObject tab;
        tab["path"] = e->filePath();
        int line, col;
        e->getCursorPosition(&line, &col);
        tab["line"] = line;
        tab["col"] = col;
        tab["active"] = (i == m_tabs->currentIndex());
        tabs.append(tab);
    }

    QJsonObject session;
    session["tabs"] = tabs;
    session["windowX"] = x();
    session["windowY"] = y();
    session["windowW"] = width();
    session["windowH"] = height();
    session["maximized"] = isMaximized();

    QFile f(sessionFilePath());
    if (f.open(QIODevice::WriteOnly)) {
        f.write(QJsonDocument(session).toJson());
    }
}

void MainWindow::restoreSession() {
    QFile f(sessionFilePath());
    if (!f.open(QIODevice::ReadOnly)) return;

    QJsonDocument doc = QJsonDocument::fromJson(f.readAll());
    if (doc.isNull()) return;
    QJsonObject session = doc.object();

    // Restore window geometry
    if (session.contains("windowW")) {
        int sw = session["windowW"].toInt();
        int sh = session["windowH"].toInt();
        if (sw > 100 && sh > 100) {
            resize(sw, sh);
            move(session["windowX"].toInt(), session["windowY"].toInt());
        }
    }
    if (session["maximized"].toBool()) {
        showMaximized();
    }

    // Restore tabs
    QJsonArray tabs = session["tabs"].toArray();
    int activeIdx = 0;

    for (int i = 0; i < tabs.size(); i++) {
        QJsonObject tab = tabs[i].toObject();
        QString path = tab["path"].toString();
        if (path.isEmpty() || !QFileInfo(path).exists()) continue;

        openFile(path);

        // Restore cursor position
        auto *e = m_tabs->editorAt(m_tabs->count() - 1);
        if (e) {
            e->setCursorPosition(tab["line"].toInt(), tab["col"].toInt());
        }

        if (tab["active"].toBool()) activeIdx = m_tabs->count() - 1;
    }

    if (m_tabs->count() > 1) {
        // Remove the initial empty "new 1" tab if we restored files
        auto *first = m_tabs->editorAt(0);
        if (first && first->filePath().isEmpty() && !first->isModified()) {
            m_tabs->removeTab(0);
            if (activeIdx > 0) activeIdx--;
        }
    }

    m_tabs->setCurrentIndex(activeIdx);
}

void MainWindow::autoSaveRecovery() {
    QString dir = recoveryDir();
    QDir().mkpath(dir);

    // Write a crash flag
    QFile flag(dir + "/.crash_flag");
    if (flag.open(QIODevice::WriteOnly)) {
        flag.write("running");
        flag.close();
    }

    // Save unsaved/modified content to recovery files
    for (int i = 0; i < m_tabs->count(); i++) {
        auto *e = m_tabs->editorAt(i);
        if (!e || !e->isModified()) continue;

        QString recoveryPath = dir + QString("/recovery_%1.txt").arg(i);
        QFile rf(recoveryPath);
        if (rf.open(QIODevice::WriteOnly)) {
            rf.write(e->text().toUtf8());
        }

        // Save metadata
        QJsonObject meta;
        meta["originalPath"] = e->filePath();
        meta["tabName"] = m_tabs->tabText(i);
        meta["tabIndex"] = i;
        int line, col;
        e->getCursorPosition(&line, &col);
        meta["line"] = line;
        meta["col"] = col;

        QFile mf(recoveryPath + ".meta");
        if (mf.open(QIODevice::WriteOnly)) {
            mf.write(QJsonDocument(meta).toJson());
        }
    }
}

void MainWindow::checkCrashRecovery() {
    QString dir = recoveryDir();
    QFile flag(dir + "/.crash_flag");

    if (!flag.exists()) return;

    // Flag exists = last session didn't close cleanly
    QDir recovDir(dir);
    QStringList recoveryFiles = recovDir.entryList({"recovery_*.txt"}, QDir::Files);

    if (recoveryFiles.isEmpty()) {
        flag.remove();
        return;
    }

    auto result = QMessageBox::question(this, "Crash Recovery",
        QString("Notepatra detected an unclean shutdown.\n\n"
                "%1 unsaved file(s) found in recovery.\n\n"
                "Restore recovered files?").arg(recoveryFiles.size()),
        QMessageBox::Yes | QMessageBox::No);

    if (result == QMessageBox::Yes) {
        for (const QString &rf : recoveryFiles) {
            QString recovPath = dir + "/" + rf;
            QString metaPath = recovPath + ".meta";

            // Read content
            QFile contentFile(recovPath);
            if (!contentFile.open(QIODevice::ReadOnly)) continue;
            QString content = QString::fromUtf8(contentFile.readAll());

            // Read metadata
            QString tabName = "Recovered";
            QString originalPath;
            int line = 0, col = 0;

            QFile metaFile(metaPath);
            if (metaFile.open(QIODevice::ReadOnly)) {
                QJsonObject meta = QJsonDocument::fromJson(metaFile.readAll()).object();
                tabName = meta["tabName"].toString("Recovered");
                originalPath = meta["originalPath"].toString();
                line = meta["line"].toInt();
                col = meta["col"].toInt();
            }

            // Create a new tab with recovered content
            auto *editor = newFile();
            editor->setText(content);
            editor->setCursorPosition(line, col);

            int idx = m_tabs->indexOf(editor);
            m_tabs->setTabText(idx, tabName + " [recovered]");
            if (!originalPath.isEmpty()) {
                m_tabs->setTabToolTip(idx, "Recovered from: " + originalPath);
            }
        }
    }

    // Clean up recovery files
    for (const QString &rf : recovDir.entryList({"recovery_*"}, QDir::Files)) {
        QFile::remove(dir + "/" + rf);
    }
    flag.remove();
}

// ═══════════════════════════════════════
// File change watcher — detect external edits
// ═══════════════════════════════════════

void MainWindow::setupFileWatcher() {
    m_fileWatcher = new QFileSystemWatcher(this);

    connect(m_fileWatcher, &QFileSystemWatcher::fileChanged, this, [this](const QString &path) {
        // File was modified externally — find which tab has it
        for (int i = 0; i < m_tabs->count(); i++) {
            auto *e = m_tabs->editorAt(i);
            if (!e || e->filePath() != path) continue;

            // Check if file still exists
            QFileInfo fi(path);
            if (!fi.exists()) {
                // File was deleted
                m_tabs->setCurrentIndex(i);
                auto result = QMessageBox::warning(this, "File Deleted",
                    QString("The file \"%1\" has been deleted by another program.\n\n"
                            "Keep this file in editor?")
                    .arg(QFileInfo(path).fileName()),
                    QMessageBox::Yes | QMessageBox::No);
                if (result == QMessageBox::No) {
                    m_tabs->removeTab(i);
                    delete e;
                    if (m_tabs->count() == 0) newFile();
                }
                return;
            }

            // File was modified — check if content actually changed
            QDateTime newTime = fi.lastModified();
            if (m_fileTimestamps.contains(path) && newTime == m_fileTimestamps[path])
                return;  // same timestamp, ignore

            m_tabs->setCurrentIndex(i);
            auto result = QMessageBox::question(this, "File Changed",
                QString("The file \"%1\" has been modified by another program.\n\n"
                        "Do you want to reload it?\n\n"
                        "  Yes = Reload from disk (lose your changes)\n"
                        "  No = Keep your version")
                .arg(QFileInfo(path).fileName()),
                QMessageBox::Yes | QMessageBox::No);

            if (result == QMessageBox::Yes) {
                e->loadFile(path);
                updateTabTitle(i);
                m_fileTimestamps[path] = fi.lastModified();
            } else {
                // User chose to keep their version — mark as modified
                m_fileTimestamps[path] = fi.lastModified();
            }

            // Re-add to watcher (Qt removes it after signal)
            m_fileWatcher->addPath(path);
            return;
        }
    });
}

void MainWindow::checkFileChanges() {
    // Called periodically to catch any missed changes
    for (int i = 0; i < m_tabs->count(); i++) {
        auto *e = m_tabs->editorAt(i);
        if (!e || e->filePath().isEmpty()) continue;

        QFileInfo fi(e->filePath());
        if (!fi.exists()) continue;

        QDateTime newTime = fi.lastModified();
        if (m_fileTimestamps.contains(e->filePath()) && newTime != m_fileTimestamps[e->filePath()]) {
            // Trigger the watcher manually
            m_fileWatcher->removePath(e->filePath());
            m_fileWatcher->addPath(e->filePath());
        }
    }
}

void MainWindow::closeEvent(QCloseEvent *event) {
    for (int i = 0; i < m_tabs->count(); i++) {
        auto *e = m_tabs->editorAt(i);
        if (e && e->isModified()) {
            m_tabs->setCurrentIndex(i);
            QString name = m_tabs->tabText(i).remove(" *");
            auto result = QMessageBox::question(this, "Save",
                QString("Save changes to %1?").arg(name),
                QMessageBox::Save | QMessageBox::Discard | QMessageBox::Cancel);
            if (result == QMessageBox::Save) {
                if (!e->filePath().isEmpty()) e->saveFile();
                else {
                    QString path = QFileDialog::getSaveFileName(this, "Save");
                    if (!path.isEmpty()) e->saveFile(path);
                    else { event->ignore(); return; }
                }
            } else if (result == QMessageBox::Cancel) {
                event->ignore();
                return;
            }
        }
    }

    // Save session before closing (clean shutdown)
    saveSession();

    // Remove crash flag (clean exit)
    QFile::remove(recoveryDir() + "/.crash_flag");

    // Clean recovery files (not needed on clean exit)
    QDir recovDir(recoveryDir());
    for (const QString &rf : recovDir.entryList({"recovery_*"}, QDir::Files)) {
        QFile::remove(recoveryDir() + "/" + rf);
    }

    event->accept();
}

// ═══════════════════════════════════════
// Drag and drop — open files by dragging onto window
// ═══════════════════════════════════════

void MainWindow::dragEnterEvent(QDragEnterEvent *event) {
    if (event->mimeData()->hasUrls()) event->acceptProposedAction();
}

void MainWindow::dropEvent(QDropEvent *event) {
    for (const QUrl &url : event->mimeData()->urls()) {
        if (url.isLocalFile()) openFile(url.toLocalFile());
    }
}

// ═══════════════════════════════════════
// Recent files menu
// ═══════════════════════════════════════

void MainWindow::updateRecentMenu() {
    if (!m_recentMenu) return;
    m_recentMenu->clear();
    auto &cfg = Config::instance();
    for (int i = 0; i < cfg.recentFiles.size(); i++) {
        const QString &path = cfg.recentFiles[i];
        m_recentMenu->addAction(QString("&%1: %2").arg(i + 1).arg(path), this, [this, path]() {
            openFile(path);
        });
    }
    if (!cfg.recentFiles.isEmpty()) {
        m_recentMenu->addSeparator();
        m_recentMenu->addAction("Clear Recent Files", this, [this]() {
            Config::instance().recentFiles.clear();
            Config::instance().save();
            updateRecentMenu();
        });
    }
}

// ═══════════════════════════════════════
// Apply theme to entire application
// ═══════════════════════════════════════

void MainWindow::applyThemeToAll(const Theme &t) {
    // Editor tabs
    if (m_tabs) for (int i = 0; i < m_tabs->count(); i++) {
        auto *ed = m_tabs->editorAt(i);
        if (ed) {
            ed->setPaper(t.editorBg);
            ed->setColor(t.editorFg);
            ed->setCaretLineBackgroundColor(t.caretLine);
            ed->setCaretForegroundColor(t.caret);
            ed->setSelectionBackgroundColor(t.selection);
            ed->setMarginsBackgroundColor(t.marginBg);
            ed->setMarginsForegroundColor(t.marginFg);
            ed->setFoldMarginColors(t.foldBg, t.foldBg);
            ed->setMatchedBraceBackgroundColor(t.matchedBraceBg);
            ed->setMatchedBraceForegroundColor(t.matchedBraceFg);
            // Re-set lexer paper for all styles
            if (auto *lex = ed->lexer()) {
                lex->setDefaultPaper(t.editorBg);
                lex->setDefaultColor(t.editorFg);
            }
        }
    }

    // Status bar
    if (m_statusBar)
        m_statusBar->applyColors(t.statusBg.name(), t.statusFg.name(),
                                  t.tabBorder.name());

    // Window stylesheet
    setStyleSheet(QString(
        "QMainWindow { background-color: %1; }"
        "QMenuBar { background-color: %2; color: %3; border-bottom: 1px solid %4; }"
        "QMenuBar::item:selected { background-color: %5; }"
        "QMenu { background-color: %2; color: %3; border: 1px solid %4; }"
        "QMenu::item:selected { background-color: %5; }"
        "QToolBar { background-color: %6; border-bottom: 1px solid %4; }"
        "QToolBar QToolButton { color: %7; font-size: 11px; padding: 3px 4px; border: none; }"
        "QToolBar QToolButton:hover { background-color: %5; }"
        "QTabBar::tab { background-color: %8; color: %9; padding: 5px 12px; border-right: 1px solid %4; }"
        "QTabBar::tab:selected { background-color: %10; border-bottom: 2px solid #4A90D9; }"
        "QTabBar::tab:hover:!selected { background-color: %5; }"
        "QScrollBar:vertical { background: %11; width: 14px; }"
        "QScrollBar::handle:vertical { background: %12; min-height: 30px; border-radius: 4px; margin: 2px; }"
        "QScrollBar::handle:vertical:hover { background: %13; }"
        "QScrollBar::add-line:vertical, QScrollBar::sub-line:vertical { height: 0; }"
        "QScrollBar:horizontal { background: %11; height: 14px; }"
        "QScrollBar::handle:horizontal { background: %12; min-width: 30px; border-radius: 4px; margin: 2px; }"
        "QScrollBar::handle:horizontal:hover { background: %13; }"
        "QScrollBar::add-line:horizontal, QScrollBar::sub-line:horizontal { width: 0; }"
    ).arg(t.windowBg.name(), t.menuBg.name(), t.menuFg.name(), t.tabBorder.name(),
          t.menuHover.name(), t.toolbarBg.name(), t.windowFg.name(),
          t.tabBg.name(), t.tabFg.name(), t.tabActiveBg.name(),
          t.scrollBg.name(), t.scrollHandle.name(), t.scrollHover.name()));
}
