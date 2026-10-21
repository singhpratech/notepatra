// SPDX-License-Identifier: GPL-3.0-or-later
//
// test_preview_pane — integration test for the Preview side pane, driving a
// real MainWindow under QT_QPA_PLATFORM=offscreen:
//   a. pane hidden at startup; the main splitter still has exactly 3 children
//   b. Markdown render + 250 ms debounced live refresh
//   c. non-previewable tab -> placeholder; switching back re-renders
//   d. .ipynb notebook view        e. .html view
//   f. SQL notebook: scratch SQLite persists across cells; error cell
//   g. saved connection with "Allow writes" OFF rejects a mutation
//   h. toggling the pane off leaves the editor tabs intact
//   i. AI dock opens beside a visible preview
//   j. theme switches (Dark / Monokai / Light) re-render the pane
//   k. Ctrl+Return runs the SQL cell at the cursor only inside the editor tabs

#include "mainwindow.h"
#include "aipanel.h"
#include "dbconnections.h"
#include "editor.h"
#include "preview_pane.h"
#include "sql_notebook.h"

#include <QAction>
#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QMenu>
#include <QSplitter>
#include <QSqlDatabase>
#include <QSqlQuery>
#include <QTabWidget>
#include <QTableWidget>
#include <QTemporaryDir>
#include <QTest>
#include <QTextBrowser>
#include <QTextDocument>
#include <QVariant>

#include <cstdio>

static int g_passed = 0;
static int g_failed = 0;

#define EXPECT_TRUE(label, cond) \
    do { \
        if (cond) { ++g_passed; std::printf("  [PASS] %s\n", label); } \
        else { ++g_failed; std::printf("  [FAIL] %s\n", label); } \
        std::fflush(stdout); \
    } while (0)

static QString writeFile(const QString &dir, const QString &name, const QByteArray &content) {
    const QString path = QDir(dir).absoluteFilePath(name);
    QFile f(path);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate)) return {};
    f.write(content);
    return path;
}

static QAction *findActionByText(QWidget *root, const QString &prefix) {
    for (QAction *a : root->findChildren<QAction *>())
        if (a->text().startsWith(prefix)) return a;
    return nullptr;
}

static QAction *findActionByShortcut(QWidget *root, const QKeySequence &seq) {
    for (QAction *a : root->findChildren<QAction *>())
        if (a->shortcut() == seq) return a;
    return nullptr;
}

static QAction *findThemeAction(QWidget *root, const QString &name) {
    for (QMenu *m : root->findChildren<QMenu *>()) {
        if (m->title() != QLatin1String("&Theme")) continue;
        for (QAction *a : m->actions())
            if (a->text() == name) return a;
    }
    return nullptr;
}

static QTabWidget *tabsOf(QWidget *w) {
    for (QWidget *p = w; p; p = p->parentWidget())
        if (auto *t = qobject_cast<QTabWidget *>(p)) return t;
    return nullptr;
}

static bool waitIdle(SqlNotebookWidget *nb, int capMs = 10000) {
    QElapsedTimer t;
    t.start();
    while (nb->isBusy() && t.elapsed() < capMs) QTest::qWait(20);
    return !nb->isBusy();
}

static int tabIndexForPath(QTabWidget *tabs, const QString &path) {
    for (int i = 0; i < tabs->count(); ++i)
        if (auto *ed = qobject_cast<Editor *>(tabs->widget(i)))
            if (ed->filePath() == path) return i;
    return -1;
}

static QStringList sqliteTables(const QString &path) {
    QStringList tables;
    {
        QSqlDatabase db = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), QStringLiteral("probe"));
        db.setDatabaseName(path);
        if (db.open()) tables = db.tables();
        db.close();
    }
    QSqlDatabase::removeDatabase(QStringLiteral("probe"));
    return tables;
}

static const char *kNotebook = R"NB({
 "nbformat": 4, "nbformat_minor": 5,
 "metadata": {"language_info": {"name": "python"}},
 "cells": [
  {"cell_type": "markdown", "metadata": {}, "source": ["# Notebook Cell Heading\n", "text"]},
  {"cell_type": "code", "execution_count": 1, "metadata": {}, "source": ["print('hello')"],
   "outputs": [{"output_type": "stream", "name": "stdout", "text": ["hello from stdout\n"]}]}
 ]
})NB";

int main(int argc, char *argv[]) {
    setvbuf(stdout, nullptr, _IONBF, 0);
    qputenv("QT_QPA_PLATFORM", "offscreen");
    QTemporaryDir cfgDir;
    qputenv("XDG_CONFIG_HOME", cfgDir.path().toUtf8());
    qputenv("XDG_DATA_HOME", cfgDir.path().toUtf8());
    qputenv("HOME", cfgDir.path().toUtf8());
    QApplication app(argc, argv);

    QTemporaryDir work;
    const QString mdPath = writeFile(work.path(), "readme.md", "# Hello Preview\n\nFirst para.\n");
    const QString pyPath = writeFile(work.path(), "script.py", "print('x')\n");
    const QString nbPath = writeFile(work.path(), "analysis.ipynb", kNotebook);
    const QString htmlPath = writeFile(work.path(), "page.html",
                                       "<html><body><h1>Html Heading</h1><p>para</p></body></html>\n");
    const QString sqlPath = writeFile(work.path(), "cells.sql",
                                      "create table t(x int);\n"
                                      "-- %%\n"
                                      "insert into t values (1),(2),(3);\n"
                                      "-- %%\n"
                                      "select count(*) as n from t;\n");
    if (mdPath.isEmpty() || sqlPath.isEmpty()) {
        std::printf("FAIL - could not write fixtures\n");
        return 2;
    }

    std::printf("=== test_preview_pane ===\n");
    MainWindow mw;
    mw.resize(1400, 900);
    mw.show();
    mw.runStartupNow();
    QApplication::processEvents();
    mw.activateWindow();
    (void)QTest::qWaitForWindowActive(&mw, 2000);

    auto *pane = mw.findChild<PreviewPane *>();
    QAction *previewAct = mw.findChild<QAction *>(QStringLiteral("actPreviewPane"));
    if (!pane || !previewAct) {
        std::printf("  [FAIL] preview pane / action not found\n=== ABORTED ===\n");
        return 1;
    }
    SqlNotebookWidget *nb = pane->sqlNotebook();

    // ── a ──
    std::printf("-- a: startup layout --\n");
    EXPECT_TRUE("a: pane hidden at startup", !pane->isVisible());
    EXPECT_TRUE("a: action text and shortcut",
                previewAct->text() == QLatin1String("Preview Pane    Ctrl+Shift+V") &&
                    previewAct->shortcut() == QKeySequence("Ctrl+Shift+V") && previewAct->isCheckable());
    QSplitter *mainSplit = nullptr;
    for (AIPanel *p : mw.findChildren<AIPanel *>()) {
        for (QWidget *w = p; w && !mainSplit; w = w->parentWidget()) {
            auto *sp = qobject_cast<QSplitter *>(w->parentWidget());
            if (!sp) continue;
            for (int i = 0; i < sp->count(); ++i)
                if (sp->widget(i)->findChild<QTabWidget *>() && sp->widget(i)->findChild<PreviewPane *>())
                    mainSplit = sp;
        }
        if (mainSplit) break;
    }
    EXPECT_TRUE("a: main splitter located via the AI dock's parent chain", mainSplit != nullptr);
    EXPECT_TRUE("a: main splitter still has exactly 3 children", mainSplit && mainSplit->count() == 3);
    EXPECT_TRUE("a: index 0 is the editor group holding tabs + preview",
                mainSplit && qobject_cast<QSplitter *>(mainSplit->widget(0)) &&
                    mainSplit->widget(0)->findChild<PreviewPane *>() == pane);

    // ── b ──
    std::printf("-- b: markdown --\n");
    mw.openFile(mdPath);
    QApplication::processEvents();
    Editor *mdEd = mw.currentEditor();
    QTabWidget *tabs = tabsOf(mdEd);
    previewAct->trigger();
    QTest::qWait(50);
    EXPECT_TRUE("b: action checked + pane visible", previewAct->isChecked() && pane->isVisible());
    EXPECT_TRUE("b: mode Markdown", pane->mode() == PreviewPane::Mode::Markdown);
    EXPECT_TRUE("b: browser shows the heading",
                pane->browser()->toPlainText().contains(QLatin1String("Hello Preview")));
    EXPECT_TRUE("b: header names the file", pane->headerText().contains(QLatin1String("readme.md")));
    if (mdEd) mdEd->append(QStringLiteral("\n\nSecond para"));
    EXPECT_TRUE("b: not re-rendered synchronously (debounced)",
                !pane->browser()->toPlainText().contains(QLatin1String("Second para")));
    QTest::qWait(400);
    EXPECT_TRUE("b: live refresh after debounce",
                pane->browser()->toPlainText().contains(QLatin1String("Second para")));

    // ── c ──
    std::printf("-- c: tab switching --\n");
    mw.openFile(pyPath);
    QTest::qWait(30);
    EXPECT_TRUE("c: pane still visible on .py tab", pane->isVisible());
    EXPECT_TRUE("c: mode None on .py tab", pane->mode() == PreviewPane::Mode::None);
    EXPECT_TRUE("c: placeholder shown", pane->placeholder()->isVisible() &&
                pane->placeholder()->text().contains(QLatin1String("SQL notebooks")));
    if (tabs) tabs->setCurrentIndex(tabIndexForPath(tabs, mdPath));
    QTest::qWait(30);
    EXPECT_TRUE("c: back on .md -> Markdown again", pane->mode() == PreviewPane::Mode::Markdown &&
                pane->browser()->isVisible() &&
                pane->browser()->toPlainText().contains(QLatin1String("Second para")));

    // ── d ──
    std::printf("-- d: notebook --\n");
    mw.openFile(nbPath);
    QTest::qWait(30);
    EXPECT_TRUE("d: mode Notebook", pane->mode() == PreviewPane::Mode::Notebook);
    EXPECT_TRUE("d: markdown cell heading rendered",
                pane->browser()->toPlainText().contains(QLatin1String("Notebook Cell Heading")));
    EXPECT_TRUE("d: stream output rendered",
                pane->browser()->toPlainText().contains(QLatin1String("hello from stdout")));

    // ── e ──
    std::printf("-- e: html --\n");
    mw.openFile(htmlPath);
    QTest::qWait(30);
    EXPECT_TRUE("e: mode Html", pane->mode() == PreviewPane::Mode::Html);
#ifndef NOTEPATRA_WITH_WEBENGINE
    EXPECT_TRUE("e: Lite renders HTML in the text browser",
                pane->browser()->toPlainText().contains(QLatin1String("Html Heading")) &&
                    !pane->browser()->toPlainText().contains(QLatin1String("<h1>")));
#endif
    {
        // A relative <img> beside the file must resolve (search path = the file's folder).
        const QByteArray png = QByteArray::fromBase64(
            "iVBORw0KGgoAAAANSUhEUgAAAAEAAAABCAYAAAAfFcSJAAAADUlEQVR42mNk+M9QDwADhgGAWjR9awAAAABJRU5ErkJggg==");
        writeFile(work.path(), QStringLiteral("logo.png"), png);
        const QString relPath = writeFile(work.path(), QStringLiteral("rel.html"),
            "<html><body><h1>Rel Heading</h1><p><img src='logo.png' alt='pic'></p></body></html>");
        mw.openFile(relPath);
        QTest::qWait(30);
        EXPECT_TRUE("e: second html file previewed", pane->mode() == PreviewPane::Mode::Html);
#ifndef NOTEPATRA_WITH_WEBENGINE
        const QVariant img = pane->browser()->document()->resource(QTextDocument::ImageResource,
                                                                   QUrl(QStringLiteral("logo.png")));
        EXPECT_TRUE("e: relative image resolved against the file's folder (Lite browser)", img.isValid());
#endif
    }

    // ── f ──
    std::printf("-- f: SQL notebook, scratch SQLite --\n");
    mw.openFile(sqlPath);
    QTest::qWait(30);
    Editor *sqlEd = mw.currentEditor();
    EXPECT_TRUE("f: mode SqlNotebook", pane->mode() == PreviewPane::Mode::SqlNotebook);
    EXPECT_TRUE("f: 3 cells", nb->cellCount() == 3);
    EXPECT_TRUE("f: scratch SQLite selected",
                nb->connectionCombo()->currentText().startsWith(QLatin1String("Scratch (SQLite")));
    EXPECT_TRUE("f: allow writes forced on for scratch",
                nb->allowWritesBox()->isChecked() && !nb->allowWritesBox()->isEnabled());
    for (int i = 0; i < 3; ++i) {
        nb->runCell(i);
        if (i == 0) EXPECT_TRUE("f: busy while the first cell runs", nb->isBusy());
        EXPECT_TRUE("f: cell finished within 10 s", waitIdle(nb));
    }
    QTableWidget *t2 = nb->resultTableAt(2);
    const bool persisted = t2 && t2->rowCount() == 1 && t2->columnCount() == 1 && t2->item(0, 0) &&
                           t2->item(0, 0)->text() == QLatin1String("3") &&
                           t2->horizontalHeaderItem(0)->text() == QLatin1String("n");
    EXPECT_TRUE("f: cell 3 sees rows created by cells 1-2 (n == 3)", persisted);
    if (!persisted) {
        std::printf("     status[0]=%s | status[1]=%s | status[2]=%s\n",
                    qPrintable(nb->statusTextAt(0)), qPrintable(nb->statusTextAt(1)),
                    qPrintable(nb->statusTextAt(2)));
        std::printf("\n=== %d passed, %d failed (aborted after f) ===\n", g_passed, g_failed);
        return 1;
    }
    sqlEd->append(QStringLiteral("\n-- %%\nselect * from nonexistent;\n"));
    QTest::qWait(400);
    EXPECT_TRUE("f: appended cell -> 4 cells", nb->cellCount() == 4);
    EXPECT_TRUE("f: results of unchanged cells survive the edit",
                nb->resultTableAt(2) && nb->resultTableAt(2)->isVisibleTo(nb) &&
                    nb->resultTableAt(2)->item(0, 0) &&
                    nb->resultTableAt(2)->item(0, 0)->text() == QLatin1String("3"));
    nb->runCell(3);
    EXPECT_TRUE("f: error cell finished", waitIdle(nb));
    EXPECT_TRUE("f: error reported in status", nb->statusTextAt(3).startsWith(QLatin1String("Error")) &&
                nb->statusTextAt(3).contains(QLatin1String("nonexistent")));

    // ── g ──
    std::printf("-- g: saved connection, writes off --\n");
    const QString savedDb = QDir(work.path()).absoluteFilePath("saved.sqlite");
    {
        QSqlDatabase db = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), QStringLiteral("seed"));
        db.setDatabaseName(savedDb);
        if (db.open()) QSqlQuery(db).exec(QStringLiteral("create table keep(a int)"));
        db.close();
    }
    QSqlDatabase::removeDatabase(QStringLiteral("seed"));
    DbConnections::Record rec;
    rec.name = QStringLiteral("Notebook Saved Test");
    rec.driver = QStringLiteral("QSQLITE");
    rec.database = savedDb;
    EXPECT_TRUE("g: saved connection written", DbConnections::saveAll({rec}));
    nb->refreshConnections();
    const int savedIdx = nb->connectionCombo()->findText(rec.name);
    EXPECT_TRUE("g: saved connection listed", savedIdx >= 0);
    nb->connectionCombo()->setCurrentIndex(savedIdx);
    EXPECT_TRUE("g: allow writes defaults OFF and is editable",
                !nb->allowWritesBox()->isChecked() && nb->allowWritesBox()->isEnabled());
    sqlEd->append(QStringLiteral("\n-- %%\ncreate table z(a int);\n"));
    pane->refreshNow();
    const int zCell = nb->cellCount() - 1;
    nb->runCell(zCell);
    EXPECT_TRUE("g: mutation cell finished", waitIdle(nb));
    EXPECT_TRUE("g: read-only rejection reported",
                nb->statusTextAt(zCell).startsWith(QLatin1String("Error")) &&
                    nb->statusTextAt(zCell).contains(QLatin1String("read-only")));
    const QStringList savedTables = sqliteTables(savedDb);
    EXPECT_TRUE("g: saved database has no table z", savedTables.contains(QLatin1String("keep")) &&
                !savedTables.contains(QLatin1String("z")));
    nb->connectionCombo()->setCurrentIndex(0);
    EXPECT_TRUE("g: back on scratch, writes forced on again",
                nb->allowWritesBox()->isChecked() && !nb->allowWritesBox()->isEnabled());

    // ── h ──
    std::printf("-- h: toggle off --\n");
    sqlEd->setFocus();
    QTest::keyClick(sqlEd, Qt::Key_V, Qt::ControlModifier | Qt::ShiftModifier);
    QTest::qWait(30);
    EXPECT_TRUE("h: Ctrl+Shift+V in the editor hides the pane, action unchecked",
                !pane->isVisible() && !previewAct->isChecked());
    EXPECT_TRUE("h: editor tabs still visible", tabs && tabs->isVisible());
    EXPECT_TRUE("h: current editor visible and still parented under the tabs",
                mw.currentEditor() && mw.currentEditor()->isVisible() && tabsOf(mw.currentEditor()) == tabs);
    QTest::qWait(50);
    {
        QWidget *group = mainSplit ? mainSplit->widget(0) : nullptr;
        EXPECT_TRUE("h: editor group spans the main splitter after the pane hides",
                    group && mainSplit && group->width() >= mainSplit->width() - 8);
        EXPECT_TRUE("h: tabs reclaim the whole editor group after the pane hides",
                    tabs && group && tabs->width() >= group->width() - 8);
        std::printf("     widths: main=%d group=%d tabs=%d\n", mainSplit ? mainSplit->width() : -1,
                    group ? group->width() : -1, tabs ? tabs->width() : -1);
    }

    // ── i ──
    std::printf("-- i: AI dock beside preview --\n");
    previewAct->trigger();
    QTest::qWait(30);
    QAction *aiToggle = findActionByShortcut(&mw, QKeySequence("Ctrl+Q"));
    EXPECT_TRUE("i: AI Assistant action found", aiToggle != nullptr);
    if (aiToggle) aiToggle->trigger();
    QTest::qWait(60);
    EXPECT_TRUE("i: AI dock visible", mw.isAiDockVisible());
    EXPECT_TRUE("i: editor tabs still visible", tabs && tabs->isVisible());
    EXPECT_TRUE("i: preview still visible", pane->isVisible());
    EXPECT_TRUE("i: main splitter still 3 children", mainSplit && mainSplit->count() == 3);
    mw.setAiDockVisible(false);
    QTest::qWait(30);
    EXPECT_TRUE("i: AI dock closed", !mw.isAiDockVisible());

    // ── j ──
    std::printf("-- j: theme changes --\n");
    if (tabs) tabs->setCurrentIndex(tabIndexForPath(tabs, mdPath));
    QTest::qWait(30);
    const struct { const char *name; const char *bg; } themes[] = {
        {"Dark", "#1e1e1e"}, {"Monokai", "#272822"}, {"Light", "#ffffff"}};
    for (const auto &th : themes) {
        QAction *a = findThemeAction(&mw, QString::fromLatin1(th.name));
        if (a) a->trigger();
        QTest::qWait(30);
        const QString label = QStringLiteral("j: %1 applied, pane re-rendered with its background").arg(th.name);
        EXPECT_TRUE(qPrintable(label),
                    a && pane->isVisible() && pane->mode() == PreviewPane::Mode::Markdown &&
                        pane->browser()->toPlainText().contains(QLatin1String("Hello Preview")) &&
                        pane->browser()->styleSheet().contains(QLatin1String(th.bg)));
    }

    // ── k ──
    std::printf("-- k: Ctrl+Return --\n");
    if (tabs) tabs->setCurrentIndex(tabIndexForPath(tabs, sqlPath));
    QTest::qWait(30);
    sqlEd->append(QStringLiteral("\n-- %%\nselect 42 as answer;\n"));
    pane->refreshNow();
    const int answerCell = nb->cellCount() - 1;
    sqlEd->setCursorPosition(sqlEd->lines() - 2, 0);
    sqlEd->setFocus();
    QTest::keyClick(sqlEd, Qt::Key_Return, Qt::ControlModifier);
    const bool startedByKey = nb->isBusy();
    waitIdle(nb);
    QTableWidget *ta = nb->resultTableAt(answerCell);
    EXPECT_TRUE("k: Ctrl+Return in the SQL editor ran the cell at the cursor",
                startedByKey && ta && ta->item(0, 0) && ta->item(0, 0)->text() == QLatin1String("42"));

    // Ctrl+Return must stay an ordinary key everywhere else.
    struct KeyProbe : QObject {
        int hits = 0;
        bool eventFilter(QObject *, QEvent *e) override {
            if (e->type() == QEvent::KeyPress) {
                auto *ke = static_cast<QKeyEvent *>(e);
                if (ke->key() == Qt::Key_Return && (ke->modifiers() & Qt::ControlModifier)) ++hits;
            }
            return false;
        }
    };
    if (tabs) tabs->setCurrentIndex(tabIndexForPath(tabs, pyPath));
    QTest::qWait(30);
    Editor *pyEd = mw.currentEditor();
    KeyProbe probe;
    pyEd->installEventFilter(&probe);
    pyEd->setFocus();
    previewAct->trigger();  // pane off: baseline
    QTest::qWait(20);
    pyEd->setCursorPosition(0, 0);
    const QString beforeHidden = pyEd->text();
    QTest::keyClick(pyEd, Qt::Key_Return, Qt::ControlModifier);
    const QString deltaHidden = pyEd->text();
    pyEd->setText(beforeHidden);
    previewAct->trigger();  // pane on, non-SQL tab
    QTest::qWait(20);
    pyEd->setFocus();
    pyEd->setCursorPosition(0, 0);
    QTest::keyClick(pyEd, Qt::Key_Return, Qt::ControlModifier);
    const QString deltaShown = pyEd->text();
    pyEd->removeEventFilter(&probe);
    std::printf("     (Ctrl+Return in a .py editor %s the text)\n",
                deltaHidden == beforeHidden ? "does not change" : "changes");
    EXPECT_TRUE("k: Ctrl+Return reaches a non-SQL editor (not swallowed) with pane hidden and shown",
                probe.hits == 2);
    EXPECT_TRUE("k: non-SQL editor behaves identically with the pane shown", deltaShown == deltaHidden);
    EXPECT_TRUE("k: no notebook run from a non-SQL tab", !nb->isBusy());
    pyEd->setText(beforeHidden);

    // With focus inside the AI dock the notebook must not run.
    if (tabs) tabs->setCurrentIndex(tabIndexForPath(tabs, sqlPath));
    QTest::qWait(30);
    sqlEd->append(QStringLiteral("\n-- %%\nselect 7 as seven;\n"));
    pane->refreshNow();
    const int sevenCell = nb->cellCount() - 1;
    sqlEd->setCursorPosition(sqlEd->lines() - 2, 0);
    mw.setAiDockVisible(true);
    QTest::qWait(60);
    // Offline CI has no Ollama, so the chat input is DISABLED ("(Ollama offline)")
    // and setFocus on it is a no-op. The guard only needs focus somewhere inside
    // the panel, so fall back to any enabled, focusable child. Poll: the hand-off
    // is asynchronous under offscreen Qt.
    AIPanel *dockPanel = nullptr;
    QWidget *focus = nullptr;
    bool focusInAi = false;
    for (int tries = 0; tries < 60 && !focusInAi; ++tries) {
        if (!dockPanel)
            for (AIPanel *p : mw.findChildren<AIPanel *>())
                if (p->isVisible()) dockPanel = p;
        if (dockPanel) {
            QApplication::setActiveWindow(&mw);
            dockPanel->focusInput();
            focus = QApplication::focusWidget();
            if (!(focus && dockPanel->isAncestorOf(focus))) {
                for (QWidget *w : dockPanel->findChildren<QWidget *>()) {
                    if (w->isVisible() && w->isEnabled() && (w->focusPolicy() & Qt::TabFocus)) {
                        w->setFocus(Qt::OtherFocusReason);
                        if (QApplication::focusWidget() == w) break;
                    }
                }
            }
        }
        QTest::qWait(50);
        focus = QApplication::focusWidget();
        focusInAi = focus && dockPanel && dockPanel->isAncestorOf(focus);
    }
    if (focus) std::printf("     (focus widget: %s)\n", focus->metaObject()->className());
    EXPECT_TRUE("k: focus is inside the AI panel", focusInAi);
    if (focusInAi) QTest::keyClick(focus, Qt::Key_Return, Qt::ControlModifier);
    EXPECT_TRUE("k: Ctrl+Return in the AI panel did not start a notebook run",
                !nb->isBusy() && nb->statusTextAt(sevenCell).isEmpty());
    mw.setAiDockVisible(false);
    QTest::qWait(30);

    // Pane hidden + SQL tab: the shortcut is disabled and the key reaches the editor.
    previewAct->trigger();
    QTest::qWait(20);
    KeyProbe probe2;
    sqlEd->installEventFilter(&probe2);
    sqlEd->setFocus();
    sqlEd->setCursorPosition(sqlEd->lines() - 2, 0);
    QTest::keyClick(sqlEd, Qt::Key_Return, Qt::ControlModifier);
    sqlEd->removeEventFilter(&probe2);
    EXPECT_TRUE("k: pane hidden -> Ctrl+Return goes to the SQL editor, no run",
                probe2.hits == 1 && !nb->isBusy() && nb->statusTextAt(sevenCell).isEmpty());

    std::printf("\n=== %d passed, %d failed ===\n", g_passed, g_failed);
    return g_failed == 0 ? 0 : 1;
}
