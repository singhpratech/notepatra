// SPDX-License-Identifier: GPL-3.0-or-later
//
// test_ai_list_dir_denylist — list_dir keeps a deny-listed entry's NAME (so the
// agent knows not to overwrite it) but reports no size for it, nor for a
// symlink whose target is deny-listed or outside the workspace.
// Regression: list_dir returned the size of .env, id_rsa, *.key, ...

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>
#include <cstdio>

#include "ai_tools.h"

static int g_pass = 0, g_fail = 0;

static void check(const QString &label, bool ok) {
    if (ok) { ++g_pass; std::printf("  [PASS] %s\n", qPrintable(label)); }
    else    { ++g_fail; std::printf("  [FAIL] %s\n", qPrintable(label)); }
}

static void put(const QString &path, const QByteArray &bytes) {
    QFile f(path);
    f.open(QIODevice::WriteOnly);
    f.write(bytes);
    f.close();
}

static QJsonObject listing(const QString &ws) {
    AiTools::ToolCall c;
    c.id = "t";
    c.name = "list_dir";
    c.args["path"] = ".";
    const AiTools::ToolResult r = AiTools::execute(c, ws);
    QJsonObject byName;
    const QJsonArray entries = QJsonDocument::fromJson(r.content.toUtf8()).object()
                                   .value("result").toObject().value("entries").toArray();
    for (const QJsonValue &v : entries) byName[v.toObject().value("name").toString()] = v;
    return byName;
}

int main(int argc, char *argv[]) {
    QCoreApplication app(argc, argv);
    setvbuf(stdout, nullptr, _IONBF, 0);
    QTemporaryDir home;
    qputenv("HOME", home.path().toUtf8());
    qputenv("XDG_CONFIG_HOME", (home.path() + "/.config").toUtf8());
    qputenv("APPDATA", (home.path() + "/AppData").toUtf8());
    qputenv("USERPROFILE", home.path().toUtf8());

    QTemporaryDir wsDir, outDir;
    const QString ws = QDir(wsDir.path()).canonicalPath();
    const QString out = QDir(outDir.path()).canonicalPath();
    put(ws + "/readme.txt", "hello world\n");
    put(ws + "/.env", "API_KEY=0123456789\n");
    put(ws + "/id_rsa", "-----BEGIN KEY-----\n");
    put(ws + "/server.key", "key bytes\n");
    put(out + "/elsewhere.txt", "outside the workspace\n");

    std::printf("=== list_dir: deny-listed entries carry no size ===\n");
    QJsonObject e = listing(ws);
    check("readme.txt listed with its size (vacuity)",
          e.value("readme.txt").toObject().value("size").toInt() == 12);
    for (const char *n : {".env", "id_rsa", "server.key"}) {
        const QJsonObject o = e.value(n).toObject();
        check(QString("%1 name still listed").arg(n), o.value("name").toString() == n);
        check(QString("%1 has no size").arg(n), !o.contains("size"));
        check(QString("%1 marked restricted").arg(n), o.value("restricted").toBool());
    }

#ifndef Q_OS_WIN
    std::printf("=== list_dir: symlinks judged by target ===\n");
    QFile::link(ws + "/.env", ws + "/innocent.txt");
    QFile::link(out + "/elsewhere.txt", ws + "/escape.txt");
    QFile::link(ws + "/readme.txt", ws + "/alias.txt");
    const bool linked = QFileInfo(ws + "/innocent.txt").isSymLink()
                        && QFileInfo(ws + "/escape.txt").isSymLink()
                        && QFileInfo(ws + "/alias.txt").isSymLink();
    check("symlinks exist (vacuity guard)", linked);
    e = listing(ws);
    check("link to .env has no size", !e.value("innocent.txt").toObject().contains("size"));
    check("link outside the workspace has no size",
          !e.value("escape.txt").toObject().contains("size"));
    check("in-workspace link keeps its size",
          e.value("alias.txt").toObject().value("size").toInt() == 12);
#else
    std::printf("  [SKIP] symlink cases (Windows)\n");
#endif

    std::printf("\n=== Summary: %d passed, %d failed ===\n", g_pass, g_fail);
    return g_fail == 0 ? 0 : 1;
}
