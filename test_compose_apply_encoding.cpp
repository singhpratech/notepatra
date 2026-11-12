// SPDX-License-Identifier: GPL-3.0-or-later
//
// test_compose_apply_encoding — Compose "Apply" writes a reviewed file back in
// its own encoding, BOM and line endings, and refuses (file untouched) when the
// new text cannot be encoded in that encoding.
// Regression: applyComposerEdits wrote after.toUtf8() for every file, so a
// Windows-1252 file became UTF-8, a UTF-16 file lost its encoding, and a CRLF
// file proposed with LF text was rewritten with LF.

#include "aipanel.h"
#include "ai_tools.h"
#include "config.h"

#include <QApplication>
#include <QByteArray>
#include <QFile>
#include <QList>
#include <QPair>
#include <QString>
#include <QTemporaryDir>
#include <QTextCodec>
#include <cstdio>

static int g_pass = 0, g_fail = 0;

static void check(const char *label, bool ok) {
    if (ok) { ++g_pass; std::printf("  [PASS] %s\n", label); }
    else    { ++g_fail; std::printf("  [FAIL] %s\n", label); }
}

// Friend of AIPanel (declared in aipanel.h).
class AIPanelTestAccess {
public:
    static void setWorkspace(AIPanel *p, const QString &dir) { p->m_workspaceRoot = dir; }
    static void applyComposerEdits(AIPanel *p, const QList<QPair<QString, QString>> &edits) {
        p->applyComposerEdits(edits);
    }
    static void undoLastApply(AIPanel *p) { p->undoLastApply(); }
    static int lastApplyBatchSize(AIPanel *p) { return p->m_lastApplyBatch.size(); }
};

static QString g_ws;

static void put(const QString &name, const QByteArray &bytes) {
    QFile f(g_ws + "/" + name);
    f.open(QIODevice::WriteOnly);
    f.write(bytes);
    f.close();
}

static QByteArray get(const QString &name) {
    QFile f(g_ws + "/" + name);
    if (!f.open(QIODevice::ReadOnly)) return QByteArray("<missing>");
    return f.readAll();
}

static void apply(AIPanel &panel, const QString &name, const QString &after) {
    QList<QPair<QString, QString>> edits;
    edits.append(qMakePair(g_ws + "/" + name, after));
    AIPanelTestAccess::applyComposerEdits(&panel, edits);
}

static QByteArray utf16le(const QString &s) {
    return QByteArray(reinterpret_cast<const char *>(s.utf16()), s.size() * 2);
}

int main(int argc, char *argv[]) {
    setvbuf(stdout, nullptr, _IONBF, 0);
    qputenv("QT_QPA_PLATFORM", "offscreen");
    QTemporaryDir home;
    qputenv("HOME", home.path().toUtf8());
    qputenv("XDG_CONFIG_HOME", (home.path() + "/.config").toUtf8());
    qputenv("APPDATA", (home.path() + "/AppData").toUtf8());
    qputenv("USERPROFILE", home.path().toUtf8());
    QApplication app(argc, argv);
    Config::instance().aiDataMode = false;

    QTemporaryDir ws;
    g_ws = ws.path();
    AIPanel panel;
    AIPanelTestAccess::setWorkspace(&panel, g_ws);

    std::printf("=== Windows-1252 + CRLF file keeps its encoding and line endings ===\n");
    put("latin.txt", QByteArray("caf\xE9\r\nline2\r\n"));
    apply(panel, "latin.txt", QString::fromUtf8("caf\xC3\xA9\nline2\nna\xC3\xAFve\n"));
    check("latin.txt written as Windows-1252 with CRLF",
          get("latin.txt") == QByteArray("caf\xE9\r\nline2\r\nna\xEFve\r\n"));
    check("apply recorded an undo batch", AIPanelTestAccess::lastApplyBatchSize(&panel) == 1);
    AIPanelTestAccess::undoLastApply(&panel);
    check("undo restores the exact original bytes",
          get("latin.txt") == QByteArray("caf\xE9\r\nline2\r\n"));

    std::printf("=== UTF-16 LE BOM file stays UTF-16 LE with its BOM ===\n");
    const QByteArray bomLE("\xFF\xFE", 2);
    put("wide.txt", bomLE + utf16le("a\r\nb\r\n"));
    apply(panel, "wide.txt", QString::fromUtf8("a\r\nb\r\n\xC3\xBC\r\n"));
    check("wide.txt written as UTF-16 LE + BOM",
          get("wide.txt") == bomLE + utf16le(QString::fromUtf8("a\r\nb\r\n\xC3\xBC\r\n")));

    std::printf("=== UTF-8 BOM file keeps exactly one BOM ===\n");
    const QByteArray bom8("\xEF\xBB\xBF");
    put("bom.txt", bom8 + "x\n");
    apply(panel, "bom.txt", QString(QChar(0xFEFF)) + "x\ny\n");   // proposal text shows the BOM
    check("BOM-prefixed proposal: one BOM", get("bom.txt") == bom8 + "x\ny\n");
    apply(panel, "bom.txt", "x\ny\nz\n");                          // proposal without it
    check("BOM-less proposal: BOM kept", get("bom.txt") == bom8 + "x\ny\nz\n");

    std::printf("=== text the file's encoding cannot hold is refused ===\n");
    put("latin2.txt", QByteArray("caf\xE9\n"));
    apply(panel, "latin2.txt", QString::fromUtf8("caf\xC3\xA9 \xE6\x97\xA5\xE6\x9C\xAC\n"));
    check("unencodable row refused, file unchanged", get("latin2.txt") == QByteArray("caf\xE9\n"));

    std::printf("=== plain UTF-8 and new files are unchanged in behaviour ===\n");
    put("plain.txt", "one\n");
    apply(panel, "plain.txt", QString::fromUtf8("one\ntw\xC3\xB6\n"));
    check("UTF-8 LF file written as UTF-8 LF", get("plain.txt") == QByteArray("one\ntw\xC3\xB6\n"));
    apply(panel, "brand_new.txt", QString::fromUtf8("n\xC3\xA9w\n"));
    check("new file written as UTF-8", get("brand_new.txt") == QByteArray("n\xC3\xA9w\n"));

    std::printf("\n=== Summary: %d passed, %d failed ===\n", g_pass, g_fail);
    return g_fail == 0 ? 0 : 1;
}
