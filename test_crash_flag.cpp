// D7 Part B — crashFlagInit / crashFlagWrite cycle (src/crashflag.cpp).
//
// The crash handlers must be async-signal-safe, so crashFlagInit() precomputes
// the flag path into a static 1024-slot buffer (and mkpaths the parent) and
// crashFlagWrite() does a raw CreateFileW/open + 7-byte "crashed" write with
// no allocation. This test pins:
//   - the uninitialized-write no-op guard (g_flagPath[0] == 0),
//   - the parent-dir mkpath,
//   - exact "crashed" content + CREATE_ALWAYS/O_TRUNC idempotence,
//   - re-creation after the post-notice flag removal,
//   - the overlong-path buffer guard INCLUDING that re-init clears the
//     previously stored path (a stale path here would let a "no-op" init
//     keep writing to the old location),
//   - non-ASCII directory components (pins the CreateFileW / encodeName
//     choice — CreateFileA on a non-ASCII %APPDATA% would fail this).
//
// ORDER MATTERS: the path lives in one static buffer, so these sections run
// as a single sequenced scenario, not independent cases.

#include "crashflag.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QString>
#include <QTemporaryDir>

#include <cstdio>

static int g_pass = 0, g_fail = 0;
#define EXPECT(label, cond) \
    do { if (cond) { ++g_pass; std::printf("  [PASS] %s\n", label); } \
         else      { ++g_fail; std::printf("  [FAIL] %s\n", label); } } while (0)

static QByteArray readAllOf(const QString &path) {
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) return QByteArray();
    return f.readAll();
}

int main(int argc, char *argv[]) {
    QCoreApplication app(argc, argv);

    std::printf("=== test_crash_flag ===\n\n");
    fflush(stdout);

    QTemporaryDir tmp;
    EXPECT("temp dir created", tmp.isValid());

    const QString recoveryDir = tmp.path() + QStringLiteral("/recovery");
    const QString flag = recoveryDir + QStringLiteral("/.crash_flag");

    // ── 1. Write BEFORE any init: safe no-op, creates nothing ──
    crashFlagWrite();   // must return without crashing
    EXPECT("write-before-init returns without crashing", true);
    EXPECT("write-before-init created no flag file", !QFile::exists(flag));
    EXPECT("write-before-init created no recovery dir",
           !QDir(recoveryDir).exists());

    // ── 2. Init with a non-existent parent: parent gets mkpath'd ──
    EXPECT("precondition: parent dir absent", !QDir(recoveryDir).exists());
    crashFlagInit(flag);
    EXPECT("init mkpaths the parent dir", QDir(recoveryDir).exists());
    EXPECT("init alone does not write the flag", !QFile::exists(flag));

    // ── 3. First write: file exists with exactly "crashed" (7 bytes) ──
    crashFlagWrite();
    EXPECT("write creates the flag file", QFile::exists(flag));
    EXPECT("flag content is exactly \"crashed\" (7 bytes)",
           readAllOf(flag) == QByteArray("crashed"));

    // ── 4. Second write: idempotent (CREATE_ALWAYS / O_TRUNC) ──
    crashFlagWrite();
    EXPECT("second write: flag still exists", QFile::exists(flag));
    EXPECT("second write: still exactly \"crashed\"",
           readAllOf(flag) == QByteArray("crashed"));

    // ── 5. Remove + write: recreated (next crash after the startup notice
    //       cleared the flag must still be recorded) ──
    EXPECT("flag removed", QFile::remove(flag));
    crashFlagWrite();
    EXPECT("write after removal recreates the flag", QFile::exists(flag));
    EXPECT("recreated flag content is \"crashed\"",
           readAllOf(flag) == QByteArray("crashed"));

    // ── 6. Overlong path (> 1100 chars): init+write is a no-op, no crash,
    //       AND the previously stored valid path is CLEARED ──
    EXPECT("flag removed before overlong-path phase", QFile::remove(flag));
    QString longPath = tmp.path();
    while (longPath.size() <= 1100)
        longPath += QStringLiteral("/abcdefghij");   // short components: the
        // path could be mkpath'd on POSIX, so only the buffer guard (not an
        // incidental filesystem error) can make the write a no-op.
    longPath += QStringLiteral("/.crash_flag");
    EXPECT("overlong path really > 1100 chars", longPath.size() > 1100);
    crashFlagInit(longPath);    // must not crash
    crashFlagWrite();           // must be a no-op
    EXPECT("overlong init+write created no file at the long path",
           !QFile::exists(longPath));
    EXPECT("overlong re-init cleared the previous path (old flag NOT rewritten)",
           !QFile::exists(flag));

    // ── 7. Re-init back to a valid path: write works again ──
    crashFlagInit(flag);
    crashFlagWrite();
    EXPECT("re-init to valid path: write works again", QFile::exists(flag));
    EXPECT("re-init flag content is \"crashed\"",
           readAllOf(flag) == QByteArray("crashed"));

    // ── 8. Non-ASCII directory component (pins CreateFileW / encodeName) ──
    const QString flag2 =
        tmp.path() + QStringLiteral("/") +
        QString::fromUtf8(u8"récovery-тест") +
        QStringLiteral("/.crash_flag");
    crashFlagInit(flag2);
    EXPECT("non-ASCII parent dir mkpath'd",
           QFileInfo(flag2).dir().exists());
    crashFlagWrite();
    EXPECT("non-ASCII flag file created", QFile::exists(flag2));
    EXPECT("non-ASCII flag content is \"crashed\"",
           readAllOf(flag2) == QByteArray("crashed"));

    std::printf("\n%d passed, %d failed\n", g_pass, g_fail);
    fflush(stdout);
    return g_fail;
}
