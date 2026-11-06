// SPDX-License-Identifier: GPL-3.0-or-later
//
// Regression test for MainWindow::renameTabFile — the core shared by the tab
// context-menu Rename and File > Rename... The old code ignored
// QFile::rename's result and then saveFile(newPath), so renaming onto an
// existing name silently overwrote that unrelated file. Never drives a modal.

#include "mainwindow.h"
#include "editor.h"
#include "tabmanager.h"
#include "config.h"

#include <QApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QStandardPaths>
#include <QTemporaryDir>

#include <cstdio>

static int g_pass = 0, g_fail = 0;
#define EXPECT(label, cond) \
    do { if (cond) { ++g_pass; std::printf("  [PASS] %s\n", label); } \
         else      { ++g_fail; std::printf("  [FAIL] %s\n", label); } \
         std::fflush(stdout); } while (0)

static bool writeFile(const QString &path, const QByteArray &bytes) {
    QFile f(path);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate)) return false;
    const bool ok = f.write(bytes) == bytes.size();
    f.close();
    return ok;
}

static QByteArray readFile(const QString &path) {
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) return QByteArray();
    return f.readAll();
}

static int tabFor(TabManager *tm, const QString &path) {
    for (int i = 0; i < tm->count(); ++i)
        if (Editor *e = tm->editorAt(i))
            if (QFileInfo(e->filePath()) == QFileInfo(path)) return i;
    return -1;
}

int main(int argc, char *argv[]) {
    QTemporaryDir cfg;
    qputenv("HOME", cfg.path().toUtf8());
    qputenv("XDG_CONFIG_HOME", cfg.path().toUtf8());
    qputenv("XDG_DATA_HOME", cfg.path().toUtf8());
#ifdef Q_OS_WIN
    qputenv("APPDATA", cfg.path().toUtf8());
    qputenv("USERPROFILE", cfg.path().toUtf8());
#endif
    qputenv("QT_QPA_PLATFORM", "offscreen");
    QStandardPaths::setTestModeEnabled(true);

    QApplication app(argc, argv);
    std::printf("=== test_rename_tab_file ===\n\n");

    const QString cfgDir = Config::appConfigDir();
    writeFile(cfgDir + QStringLiteral("/config.json"),
              "{\"showWelcomeOnStartup\": false, \"autoSaveIntervalSec\": 300}\n");

    QTemporaryDir work;
    EXPECT("work dir valid", work.isValid());
    const QString d = work.path();

    auto *mw = new MainWindow();
    mw->runStartupNow();
    QApplication::processEvents();
    auto *tm = mw->findChild<TabManager *>();
    EXPECT("found TabManager", tm != nullptr);
    if (!tm) return 1;

    // (a) rename onto an existing, unrelated file must change nothing.
    std::printf("\n(a) rename onto an existing file\n");
    {
        const QString a = d + QStringLiteral("/a.txt");
        const QString b = d + QStringLiteral("/b.txt");
        writeFile(a, "AAA tab content\n");
        writeFile(b, "BBB unrelated precious bytes\n");
        mw->openFile(a);
        QApplication::processEvents();
        const int idx = tabFor(tm, a);
        EXPECT("a.txt opened in a tab", idx >= 0);
        const auto r = mw->renameTabFile(idx, QStringLiteral("b.txt"));
        EXPECT("result is TargetExists", r == MainWindow::RenameResult::TargetExists);
        EXPECT("b.txt bytes unchanged",
               readFile(b) == QByteArray("BBB unrelated precious bytes\n"));
        EXPECT("a.txt still on disk", QFile::exists(a));
        EXPECT("a.txt bytes unchanged", readFile(a) == QByteArray("AAA tab content\n"));
        Editor *e = idx >= 0 ? tm->editorAt(idx) : nullptr;
        EXPECT("tab still points at a.txt",
               e && QFileInfo(e->filePath()) == QFileInfo(a));
    }

    // (b) normal rename moves the file and retargets the tab.
    std::printf("\n(b) normal rename\n");
    {
        const QString c = d + QStringLiteral("/c.txt");
        const QString c2 = d + QStringLiteral("/c-renamed.txt");
        writeFile(c, "CCC content\n");
        mw->openFile(c);
        QApplication::processEvents();
        const int idx = tabFor(tm, c);
        EXPECT("c.txt opened in a tab", idx >= 0);
        const auto r = mw->renameTabFile(idx, QStringLiteral("c-renamed.txt"));
        EXPECT("result is Renamed", r == MainWindow::RenameResult::Renamed);
        EXPECT("old c.txt gone", !QFile::exists(c));
        EXPECT("new file has the content", readFile(c2) == QByteArray("CCC content\n"));
        Editor *e = idx >= 0 ? tm->editorAt(idx) : nullptr;
        EXPECT("tab path updated", e && QFileInfo(e->filePath()) == QFileInfo(c2));
        EXPECT("tab text updated",
               idx >= 0 && tm->tabText(idx).startsWith(QStringLiteral("c-renamed.txt")));
        EXPECT("tab not left modified", e && !e->isModified());
    }

    // (c) a modified buffer is what lands in the renamed file.
    std::printf("\n(c) modified buffer + rename\n");
    {
        const QString m = d + QStringLiteral("/m.txt");
        const QString m2 = d + QStringLiteral("/m2.txt");
        writeFile(m, "on disk\n");
        mw->openFile(m);
        QApplication::processEvents();
        const int idx = tabFor(tm, m);
        Editor *e = idx >= 0 ? tm->editorAt(idx) : nullptr;
        EXPECT("m.txt opened in a tab", e != nullptr);
        if (e) {
            e->setText(QStringLiteral("edited in buffer\n"));
            e->setModified(true);
        }
        const auto r = mw->renameTabFile(idx, QStringLiteral("m2.txt"));
        EXPECT("result is Renamed", r == MainWindow::RenameResult::Renamed);
        EXPECT("new file holds the buffer text",
               readFile(m2) == QByteArray("edited in buffer\n"));
        EXPECT("old m.txt gone", !QFile::exists(m));
        EXPECT("buffer marked saved", e && !e->isModified());
    }

    // (e) rename never rewrites: external edits on disk survive, lexer follows ext.
    std::printf("\n(e) external edit + rename keeps disk bytes; language follows\n");
    {
        const QString x = d + QStringLiteral("/x.txt");
        const QString x2 = d + QStringLiteral("/x.py");
        writeFile(x, "buffer text\n");
        mw->openFile(x);
        QApplication::processEvents();
        const int idx = tabFor(tm, x);
        Editor *e = idx >= 0 ? tm->editorAt(idx) : nullptr;
        EXPECT("x.txt opened in a tab", e != nullptr);
        EXPECT("buffer unmodified", e && !e->isModified());
        const QByteArray external("print('edited outside')\r\n\xEF\xBB\xBF");
        writeFile(x, external);  // behind the editor's back; no event loop turn
        const auto r = mw->renameTabFile(idx, QStringLiteral("x.py"));
        EXPECT("result is Renamed", r == MainWindow::RenameResult::Renamed);
        EXPECT("renamed file holds the on-disk bytes, not the buffer",
               readFile(x2) == external);
        EXPECT("tab path updated", e && QFileInfo(e->filePath()) == QFileInfo(x2));
        EXPECT("language follows the new extension",
               e && e->language() == QStringLiteral("Python"));
        EXPECT("buffer still unmodified", e && !e->isModified());
    }

    // (d) guard rails: same name is a no-op, separators are rejected.
    std::printf("\n(d) same name / invalid name\n");
    {
        const QString s = d + QStringLiteral("/s.txt");
        writeFile(s, "SSS\n");
        mw->openFile(s);
        QApplication::processEvents();
        const int idx = tabFor(tm, s);
        EXPECT("same name is Unchanged",
               mw->renameTabFile(idx, QStringLiteral("s.txt")) ==
                   MainWindow::RenameResult::Unchanged);
        EXPECT("name with '/' is InvalidName",
               mw->renameTabFile(idx, QStringLiteral("sub/x.txt")) ==
                   MainWindow::RenameResult::InvalidName);
        EXPECT("name '..' is InvalidName",
               mw->renameTabFile(idx, QStringLiteral("..")) ==
                   MainWindow::RenameResult::InvalidName);
        EXPECT("s.txt untouched", readFile(s) == QByteArray("SSS\n"));
        EXPECT("no sub dir created", !QFileInfo::exists(d + QStringLiteral("/sub")));
    }

    for (int i = 0; i < tm->count(); ++i)
        if (Editor *e = tm->editorAt(i)) e->setModified(false);
    delete mw;

    std::printf("\n=== %d passed, %d failed ===\n", g_pass, g_fail);
    return g_fail == 0 ? 0 : 1;
}
