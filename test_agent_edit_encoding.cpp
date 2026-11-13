// SPDX-License-Identifier: GPL-3.0-or-later
//
// test_agent_edit_encoding — the agent's apply_diff / write_file must keep a
// file's line endings, BOM and encoding byte-for-byte outside the edited
// region, and refuse (file untouched) when the bytes cannot be round-tripped.
// Regression: apply_diff read with QIODevice::Text + fromUtf8 and wrote
// toUtf8, so CRLF became LF (or doubled on Windows), a UTF-8 BOM was dropped
// and Latin-1 bytes were rewritten as U+FFFD.

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QString>
#include <QTemporaryDir>
#include <QTextCodec>
#include <cstdio>

#include "ai_tools.h"

static int g_pass = 0, g_fail = 0;

static void check(const char *label, bool ok) {
    if (ok) { ++g_pass; std::printf("  [PASS] %s\n", label); }
    else    { ++g_fail; std::printf("  [FAIL] %s\n", label); }
}

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

static QJsonObject hunk(int start, const QString &oldLines, const QString &newLines) {
    QJsonObject h;
    h["old_start_line"] = start;
    h["old_lines"] = oldLines;
    h["new_lines"] = newLines;
    return h;
}

static AiTools::ToolResult applyDiff(const QString &name, const QJsonArray &hunks,
                                     bool dryRun = false) {
    AiTools::ToolCall c;
    c.id = "t";
    c.name = "apply_diff";
    c.args["path"] = name;
    c.args["hunks"] = hunks;
    if (dryRun) c.args["dry_run"] = true;
    return AiTools::execute(c, g_ws);
}

static AiTools::ToolResult writeFile(const QString &name, const QString &content,
                                     const QString &mode, bool dryRun = false) {
    AiTools::ToolCall c;
    c.id = "t";
    c.name = "write_file";
    c.args["path"] = name;
    c.args["content"] = content;
    c.args["mode"] = mode;
    if (dryRun) c.args["dry_run"] = true;
    return AiTools::execute(c, g_ws);
}

static QString hex(const QByteArray &b) { return QString::fromLatin1(b.toHex(' ')); }

static void expectBytes(const char *label, const QString &name, const QByteArray &want) {
    const QByteArray got = get(name);
    check(label, got == want);
    if (got != want)
        std::printf("         want: %s\n         got:  %s\n", qPrintable(hex(want)),
                    qPrintable(hex(got)));
}

static QByteArray utf16le(const QString &s) {
    QTextCodec::ConverterState st(QTextCodec::IgnoreHeader);
    return QTextCodec::codecForName("UTF-16LE")->fromUnicode(s.constData(), s.size(), &st);
}

int main(int argc, char *argv[]) {
    QTemporaryDir home;
    qputenv("HOME", home.path().toUtf8());
    qputenv("XDG_CONFIG_HOME", home.path().toUtf8());
    qputenv("XDG_DATA_HOME", home.path().toUtf8());
#ifdef Q_OS_WIN
    qputenv("APPDATA", home.path().toUtf8());
    qputenv("USERPROFILE", home.path().toUtf8());
#endif
    QCoreApplication app(argc, argv);

    QTemporaryDir ws;
    g_ws = QDir(ws.path()).canonicalPath();
    std::printf("=== test_agent_edit_encoding ===\n");

    // ── apply_diff: line endings ─────────────────────────────────────
    put("crlf.txt", "one\r\ntwo\r\nthree\r\n");
    {
        auto r = applyDiff("crlf.txt", QJsonArray{hunk(2, "two\n", "TWO\n")});
        check("CRLF: apply_diff succeeds", !r.isError);
        expectBytes("CRLF: line endings kept, only line 2 changed", "crlf.txt",
                    "one\r\nTWO\r\nthree\r\n");
        check("CRLF: no temp file left behind",
              !QFile::exists(g_ws + "/crlf.txt.notepatra.tmp"));
    }
    put("lf.txt", "one\ntwo\nthree\n");
    {
        auto r = applyDiff("lf.txt", QJsonArray{hunk(2, "two\n", "TWO\nextra\n")});
        check("LF: apply_diff succeeds", !r.isError);
        expectBytes("LF: stays LF", "lf.txt", "one\nTWO\nextra\nthree\n");
    }
    put("mixed.txt", "one\r\ntwo\nthree\r\nfour\n");
    {
        auto r = applyDiff("mixed.txt", QJsonArray{hunk(2, "two\n", "TWO\n2b\n"),
                                                   hunk(3, "three\n", "THREE\n")});
        check("mixed: apply_diff succeeds", !r.isError);
        expectBytes("mixed: each edited line keeps its own ending, others untouched",
                    "mixed.txt", "one\r\nTWO\n2b\nTHREE\r\nfour\n");
    }
    put("nonl.txt", "a\r\nb\r\nc");
    {
        auto r = applyDiff("nonl.txt", QJsonArray{hunk(3, "c", "C")});
        check("no trailing newline: apply_diff succeeds", !r.isError);
        expectBytes("no trailing newline: still none, CRLF kept", "nonl.txt", "a\r\nb\r\nC");
        auto r2 = applyDiff("nonl.txt", QJsonArray{hunk(1, "a\n", "A\n")});
        check("no trailing newline: second edit succeeds", !r2.isError);
        expectBytes("no trailing newline: first line edited, EOF untouched", "nonl.txt",
                    "A\r\nb\r\nC");
    }

    // ── apply_diff: BOM + encodings ──────────────────────────────────
    put("bom.txt", "\xEF\xBB\xBF" "alpha\nbeta\n");
    {
        auto r = applyDiff("bom.txt", QJsonArray{hunk(1, "alpha\n", "ALPHA\n")});
        check("UTF-8 BOM: line 1 (next to the BOM) matches and applies", !r.isError);
        expectBytes("UTF-8 BOM: BOM kept", "bom.txt", "\xEF\xBB\xBF" "ALPHA\nbeta\n");
    }
    put("latin1.txt", "caf\xE9\nline2\n");
    {
        auto r = applyDiff("latin1.txt", QJsonArray{hunk(2, "line2\n", "LINE2 na\xC3\xAFve\n")});
        check("Latin-1: apply_diff succeeds", !r.isError);
        // U+00EF arrives as UTF-8 in the JSON and must land as the single byte 0xEF.
        expectBytes("Latin-1: untouched 0xE9 byte kept, new text in the file's encoding",
                    "latin1.txt", "caf\xE9\nLINE2 na\xEFve\n");
        auto bad = applyDiff("latin1.txt",
                             QJsonArray{hunk(2, "LINE2 na\xC3\xAFve\n",
                                             QString::fromUtf8("\xE4\xB8\xAD\n"))});
        check("Latin-1: unencodable new text is refused", bad.isError);
        check("Latin-1: refusal names the encoding problem",
              bad.errorKind == "unsupported_encoding");
        expectBytes("Latin-1: refused edit leaves the file byte-identical", "latin1.txt",
                    "caf\xE9\nLINE2 na\xEFve\n");
    }
    const QByteArray u16 = QByteArray("\xFF\xFE", 2) + utf16le("a\r\nb\r\n");
    put("utf16.txt", u16);
    {
        auto r = applyDiff("utf16.txt", QJsonArray{hunk(2, "b\n", "B\n")});
        check("UTF-16 LE BOM: apply_diff succeeds", !r.isError);
        expectBytes("UTF-16 LE BOM: BOM, encoding and CRLF kept", "utf16.txt",
                    QByteArray("\xFF\xFE", 2) + utf16le("a\r\nB\r\n"));
    }
    const QByteArray binary("ab\0cd\nxy\n", 9);
    put("binary.dat", binary);
    {
        auto r = applyDiff("binary.dat", QJsonArray{hunk(2, "xy\n", "XY\n")});
        check("BOM-less NUL bytes: refused", r.isError && r.errorKind == "unsupported_encoding");
        expectBytes("BOM-less NUL bytes: file untouched", "binary.dat", binary);
    }

    // ── dry run (Compose / approval preview) ─────────────────────────
    put("dry.txt", "one\r\ntwo\r\n");
    {
        auto r = applyDiff("dry.txt", QJsonArray{hunk(1, "one\n", "ONE\n")}, true);
        const QJsonObject body =
            QJsonDocument::fromJson(r.content.toUtf8()).object().value("result").toObject();
        const QString after = body.value("proposed").toObject().value("after").toString();
        check("dry run: proposal keeps CRLF", after == QStringLiteral("ONE\r\ntwo\r\n"));
        expectBytes("dry run: file untouched", "dry.txt", "one\r\ntwo\r\n");
    }

    // ── write_file ───────────────────────────────────────────────────
    put("wf.txt", "\xEF\xBB\xBF" "old\r\nfile\r\n");
    {
        auto r = writeFile("wf.txt", "new\ncontent\n", "overwrite");
        check("write_file overwrite: succeeds", !r.isError);
        expectBytes("write_file overwrite: BOM + CRLF of the existing file kept", "wf.txt",
                    "\xEF\xBB\xBF" "new\r\ncontent\r\n");
    }
    put("wfa.txt", "caf\xE9\r\n");
    {
        auto r = writeFile("wfa.txt", QString::fromUtf8("th\xC3\xA9\n"), "append");
        check("write_file append: succeeds", !r.isError);
        expectBytes("write_file append: appended in the file's encoding + line ending",
                    "wfa.txt", "caf\xE9\r\nth\xE9\r\n");
    }
    {
        auto r = writeFile("fresh.txt", "x\ny\n", "create");
        check("write_file create: succeeds", !r.isError);
        expectBytes("write_file create: new file is plain UTF-8 LF", "fresh.txt", "x\ny\n");
    }

    // ── the temp file never follows a planted symlink (v0.1.133 review) ──
    // writeBytesAtomic once opened the fixed name `<target>.notepatra.tmp`
    // with Truncate: a repository shipping that name as a symlink to a file
    // outside the workspace had the outside file truncated and written on the
    // agent's next edit of the target.
#ifndef Q_OS_WIN
    {
        QTemporaryDir outsideDir;
        const QString outside = outsideDir.path() + "/outside-rc";
        {
            QFile o(outside);
            o.open(QIODevice::WriteOnly);
            o.write("ORIGINAL OUTSIDE CONTENT\n");
        }
        put("main.py", "print(1)\n");
        QFile::setPermissions(g_ws + "/main.py", QFileDevice::ReadOwner | QFileDevice::WriteOwner
                                                     | QFileDevice::ReadGroup);
        const bool planted = QFile::link(outside, g_ws + "/main.py.notepatra.tmp")
                             && QFileInfo(g_ws + "/main.py.notepatra.tmp").isSymLink();
        check("planted symlink: vacuity, <target>.notepatra.tmp is a symlink", planted);
        auto outsideBytes = [&] {
            QFile o(outside);
            return o.open(QIODevice::ReadOnly) ? o.readAll() : QByteArray("<missing>");
        };

        auto w = writeFile("main.py", "print(2)\n", "overwrite");
        check("planted symlink: write_file succeeds", !w.isError);
        check("planted symlink: write_file leaves the outside file byte-identical",
              outsideBytes() == "ORIGINAL OUTSIDE CONTENT\n");
        expectBytes("planted symlink: write_file edit landed", "main.py", "print(2)\n");
        check("planted symlink: main.py is a regular file, not the planted link",
              !QFileInfo(g_ws + "/main.py").isSymLink());

        auto d = applyDiff("main.py", QJsonArray{hunk(1, "print(2)\n", "print(3)\n")});
        check("planted symlink: apply_diff succeeds", !d.isError);
        check("planted symlink: apply_diff leaves the outside file byte-identical",
              outsideBytes() == "ORIGINAL OUTSIDE CONTENT\n");
        expectBytes("planted symlink: apply_diff edit landed", "main.py", "print(3)\n");
        check("planted symlink: the planted link is untouched",
              QFileInfo(g_ws + "/main.py.notepatra.tmp").isSymLink());
        check("existing permission bits kept (0640)",
              (QFile::permissions(g_ws + "/main.py") & QFileDevice::Permissions(0x7077))
                  == (QFileDevice::ReadOwner | QFileDevice::WriteOwner | QFileDevice::ReadGroup));

        // A new file gets the mode a plain create gets (umask), not 0600.
        put("plain.txt", "p\n");
        writeFile("created.txt", "c\n", "create");
        check("new file mode matches a plain create",
              QFile::permissions(g_ws + "/created.txt") == QFile::permissions(g_ws + "/plain.txt"));
    }
#endif
    {
        // Every file this test makes has one dot; a temp (QSaveFile's
        // <name>.XXXXXX, or a leftover .notepatra.tmp) has more. The planted
        // link is the one exception.
        QStringList stray;
        for (const QString &n : QDir(g_ws).entryList(QDir::Files | QDir::Hidden | QDir::System))
            if (n.count('.') > 1 && n != QLatin1String("main.py.notepatra.tmp")) stray << n;
        if (!stray.isEmpty()) std::printf("         stray: %s\n", qPrintable(stray.join(", ")));
        check("no temp file left behind after any write", stray.isEmpty());
    }

    std::printf("\n=== Summary: %d passed, %d failed ===\n", g_pass, g_fail);
    return g_fail == 0 ? 0 : 1;
}
