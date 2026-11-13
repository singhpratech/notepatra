// SPDX-License-Identifier: GPL-3.0-or-later
//
// Regression tests for src/atomic_write.h.
//
// WHAT THIS PINS. Two call sites — notes_storage.cpp (every Noter note
// save) and ai_tools.cpp (the AI agent writing the user's SOURCE FILES)
// — implemented the Windows replace as:
//
//     QFile::remove(target);              // target is now GONE
//     if (!QFile::rename(tmp, target)) {
//         QFile::remove(tmp);             // ...and so is the new copy
//         return false;                   // "Atomic rename failed"
//     }
//
// Qt's QFile::rename refuses to overwrite an existing target, hence the
// remove-first. That leaves a window where the file does not exist, and
// if the rename then fails the cleanup deletes the temp too — so both
// copies are gone and the user's file is unrecoverable.
//
// The dangerous interleaving (remove succeeds, rename then fails —
// antivirus grabbing the temp is the real-world trigger) cannot be
// forced deterministically in a unit test. So this file pins the two
// things that CAN be checked everywhere:
//
//   1. The helper's behavioural contract, including the invariant the
//      old code violated: a FAILED replace must leave the target
//      exactly as it was.
//   2. A source-level guard that fails if anyone reintroduces the
//      remove-then-rename idiom. This is the check that would actually
//      have caught the original bug, and it goes red on the pre-fix
//      tree by construction.

#include <QtTest>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QTemporaryDir>
#include <QTextStream>
#include <QRegularExpression>

#include "atomic_write.h"

// Guarded on purpose: an unconditional <unistd.h> in a test file fails to
// compile under MSVC (C1083), which has bitten this repo before.
#ifndef Q_OS_WIN
#  include <unistd.h>      // geteuid
#endif

#ifndef NOTEPATRA_SOURCE_DIR
#  define NOTEPATRA_SOURCE_DIR ""
#endif

namespace {

bool writeFile(const QString &path, const QByteArray &bytes) {
    QFile f(path);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate)) return false;
    const bool ok = f.write(bytes) == bytes.size();
    f.close();
    return ok;
}

QByteArray readFile(const QString &path) {
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) return QByteArray();
    return f.readAll();
}

// Windows ignores a directory's read-only bit for an administrator-owned
// tree, so the induced-failure section cannot run there. Same carve-out
// the Noter storage suite already uses.
bool readOnlyDirsUnenforced() {
#ifdef Q_OS_WIN
    return true;
#else
    return ::geteuid() == 0;   // root ignores permission bits too
#endif
}

}   // namespace

class TestAtomicWrite : public QObject {
    Q_OBJECT

private slots:

    // ── 1. Success replaces the content and removes the temp ──────────
    void replaceOverwritesExistingTarget() {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString target = dir.filePath("note.md");
        const QString tmp    = target + ".tmp";

        QVERIFY(writeFile(target, "OLD CONTENT"));
        QVERIFY(writeFile(tmp, "NEW CONTENT"));

        QString err;
        QVERIFY2(NpAtomic::replaceFile(tmp, target, &err), qPrintable(err));

        QCOMPARE(readFile(target), QByteArray("NEW CONTENT"));
        QVERIFY2(!QFile::exists(tmp), "temp must not survive a successful replace");
        QVERIFY(err.isEmpty());
    }

    // ── 2. Success when the target does not exist yet ─────────────────
    void replaceCreatesWhenTargetAbsent() {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString target = dir.filePath("fresh.md");
        const QString tmp    = target + ".tmp";

        QVERIFY(writeFile(tmp, "FIRST"));
        QVERIFY(!QFile::exists(target));

        QString err;
        QVERIFY2(NpAtomic::replaceFile(tmp, target, &err), qPrintable(err));
        QCOMPARE(readFile(target), QByteArray("FIRST"));
        QVERIFY(!QFile::exists(tmp));
    }

    // ── 3. THE INVARIANT THE OLD CODE VIOLATED ────────────────────────
    // A failed replace must leave the target present and byte-identical.
    // The pre-fix Windows branch removed the target BEFORE attempting
    // the rename, so on this path it ended with no file at all.
    void failedReplaceLeavesTargetIntact() {
        if (readOnlyDirsUnenforced())
            QSKIP("[SKIP] directory permissions are not enforced here");

        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString subdir = dir.filePath("locked");
        QVERIFY(QDir().mkpath(subdir));

        const QString target = subdir + "/note.md";
        const QString tmp    = target + ".tmp";
        QVERIFY(writeFile(target, "PRECIOUS"));
        QVERIFY(writeFile(tmp, "REPLACEMENT"));

        // Drop write permission on the containing directory so rename(2)
        // fails with EACCES while both files still exist.
        QVERIFY(QFile::setPermissions(
            subdir, QFileDevice::ReadOwner | QFileDevice::ExeOwner));

        QString err;
        const bool ok = NpAtomic::replaceFile(tmp, target, &err);

        // Restore permissions before asserting so the temp dir can clean up.
        QFile::setPermissions(subdir,
                              QFileDevice::ReadOwner | QFileDevice::WriteOwner |
                                  QFileDevice::ExeOwner);

        QVERIFY2(!ok, "replace should have failed with the directory read-only");
        QVERIFY2(!err.isEmpty(), "a failure must explain itself");
        QVERIFY2(QFile::exists(target),
                 "REGRESSION: the target was destroyed by a FAILED replace");
        QCOMPARE(readFile(target), QByteArray("PRECIOUS"));
    }

    // ── 4. The error message must not claim atomicity it did not deliver
    void failureMessageNamesTheFileAndReassures() {
        if (readOnlyDirsUnenforced())
            QSKIP("[SKIP] directory permissions are not enforced here");

        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString subdir = dir.filePath("locked2");
        QVERIFY(QDir().mkpath(subdir));
        const QString target = subdir + "/x.md";
        const QString tmp    = target + ".tmp";
        QVERIFY(writeFile(target, "A"));
        QVERIFY(writeFile(tmp, "B"));
        QVERIFY(QFile::setPermissions(
            subdir, QFileDevice::ReadOwner | QFileDevice::ExeOwner));

        QString err;
        NpAtomic::replaceFile(tmp, target, &err);
        QFile::setPermissions(subdir,
                              QFileDevice::ReadOwner | QFileDevice::WriteOwner |
                                  QFileDevice::ExeOwner);

        QVERIFY2(err.contains(QStringLiteral("left unchanged")),
                 qPrintable("error must tell the user their file survived: " + err));
        QVERIFY2(!err.contains(QStringLiteral("Atomic rename failed")),
                 "the old message asserted an atomicity the code never had");
    }

    // ── 4b. Cross-platform failure: target is a non-empty directory ───
    void failedReplaceOntoDirectoryLeavesItIntact() {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString target = dir.filePath("target");
        QVERIFY(QDir().mkpath(target));
        const QString keep = target + "/keep.txt";
        QVERIFY(writeFile(keep, "KEEP"));
        const QString tmp = dir.filePath("target.tmp");
        QVERIFY(writeFile(tmp, "REPLACEMENT"));

        QString err;
        QVERIFY2(!NpAtomic::replaceFile(tmp, target, &err),
                 "replacing a non-empty directory with a file must fail");
        QVERIFY2(!err.isEmpty(), "a failure must explain itself");
        QVERIFY(QFileInfo(target).isDir());
        QCOMPARE(readFile(keep), QByteArray("KEEP"));
        QVERIFY2(!QFile::exists(tmp), "temp must be cleaned up after a failed replace");
    }

    // ── 4c. Replace keeps the target's permission bits (apply_diff on +x) ─
    void replacePreservesTargetPermissions() {
#ifdef Q_OS_WIN
        QSKIP("[SKIP] POSIX permission bits only");
#else
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString target = dir.filePath("run.sh");
        const QString tmp    = target + ".tmp";
        QVERIFY(writeFile(target, "#!/bin/sh\necho old\n"));
        const QFileDevice::Permissions exe =
            QFileDevice::ReadOwner | QFileDevice::WriteOwner | QFileDevice::ExeOwner |
            QFileDevice::ReadGroup | QFileDevice::ExeGroup;
        QVERIFY(QFile::setPermissions(target, exe));
        QVERIFY(writeFile(tmp, "#!/bin/sh\necho new\n"));
        QVERIFY(QFile::setPermissions(tmp, QFileDevice::ReadOwner | QFileDevice::WriteOwner));

        QString err;
        QVERIFY2(NpAtomic::replaceFile(tmp, target, &err), qPrintable(err));
        QCOMPARE(readFile(target), QByteArray("#!/bin/sh\necho new\n"));
        const QFileDevice::Permissions got = QFile::permissions(target);
        QVERIFY2(got & QFileDevice::ExeOwner, "REGRESSION: replace dropped the +x bit");
        QVERIFY(got & QFileDevice::ExeGroup);
        QVERIFY(!(got & QFileDevice::WriteGroup));
#endif
    }

    // ── 5. syncData tolerates a closed / invalid handle ───────────────
    void syncDataIsSafeOnClosedFile() {
        QFile f;                       // never opened
        QVERIFY(!NpAtomic::syncData(f));   // must not crash

        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        QFile good(dir.filePath("s.txt"));
        QVERIFY(good.open(QIODevice::WriteOnly));
        good.write("payload");
        QVERIFY(NpAtomic::syncData(good));
        good.close();
    }

    // ── 6. syncDirectoryOf never throws on a missing path ─────────────
    // ── 6b. writeFileNoFollow never writes through a symlink ─────────
    // QSaveFile resolves a symlink at its target name and replaces the file
    // it points to; the no-follow writer replaces the link itself.
    void writeFileNoFollowReplacesALinkInsteadOfFollowingIt() {
#ifdef Q_OS_WIN
        QSKIP("symlinks need privileges on Windows");
#else
        QTemporaryDir dir, outside;
        const QString victim = outside.filePath("victim.txt");
        {
            QFile v(victim);
            QVERIFY(v.open(QIODevice::WriteOnly));
            v.write("VICTIM");
        }
        const QString target = dir.filePath("note.html.draft");
        QVERIFY(QFile::link(victim, target));
        QString err;
        QVERIFY2(NpAtomic::writeFileNoFollow(target, "NEW", &err), qPrintable(err));
        QFile v(victim);
        QVERIFY(v.open(QIODevice::ReadOnly));
        QCOMPARE(v.readAll(), QByteArray("VICTIM"));
        QVERIFY(!QFileInfo(target).isSymLink());
        QFile t(target);
        QVERIFY(t.open(QIODevice::ReadOnly));
        QCOMPARE(t.readAll(), QByteArray("NEW"));

        // A dangling link: nothing is created at its target.
        const QString dangling = outside.filePath("created.txt");
        const QString target2 = dir.filePath("second.draft");
        QVERIFY(QFile::link(dangling, target2));
        QVERIFY2(NpAtomic::writeFileNoFollow(target2, "TWO", &err), qPrintable(err));
        QVERIFY(!QFileInfo::exists(dangling));
        QVERIFY(!QFileInfo(target2).isSymLink());

        // A new file gets the permissions a plain QFile::open gives.
        const QString plain = dir.filePath("plain.txt");
        {
            QFile p(plain);
            QVERIFY(p.open(QIODevice::WriteOnly));
        }
        const QString fresh = dir.filePath("fresh.txt");
        QVERIFY2(NpAtomic::writeFileNoFollow(fresh, "x", &err), qPrintable(err));
        QCOMPARE(QFile::permissions(fresh), QFile::permissions(plain));

        // No temp is left behind.
        const QStringList left = QDir(dir.path()).entryList(
            QStringList() << QStringLiteral("*.tmp"), QDir::Files | QDir::Hidden | QDir::System);
        QVERIFY2(left.isEmpty(), qPrintable(left.join(QLatin1Char(' '))));
#endif
    }

    void syncDirectoryOfToleratesMissingPath() {
        NpAtomic::syncDirectoryOf(QStringLiteral("/nonexistent/path/x.md"));
        // Reaching here without crashing is the assertion.
        QVERIFY(true);
    }

    // ── 7. SOURCE GUARD — the check that would have caught the bug ────
    // Fails if any source file reintroduces "remove the target, then
    // rename onto it". Scoped to a small window so an unrelated
    // QFile::remove elsewhere in a function cannot trip it.
    void noSourceFileReintroducesRemoveThenRename() {
        const QString srcDir = QStringLiteral(NOTEPATRA_SOURCE_DIR) + "/src";
        QDir d(srcDir);
        QVERIFY2(d.exists(), qPrintable("source dir not found: " + srcDir));

        QStringList files;   // relative to src/, includes src/diagram, src/charts, ...
        QDirIterator it(srcDir, QStringList() << "*.cpp" << "*.h", QDir::Files,
                        QDirIterator::Subdirectories);
        while (it.hasNext()) files << d.relativeFilePath(it.next());
        QVERIFY(!files.isEmpty());
        QVERIFY2(files.contains(QStringLiteral("diagram/npd_parser.cpp")),
                 "source guard must scan src/ subdirectories");

        // QFile::remove(<anything>) followed within 3 lines by a
        // QFile::rename(...) onto that same kind of path.
        const QRegularExpression removeRe(
            QStringLiteral(R"(QFile::remove\s*\(\s*([A-Za-z_][A-Za-z0-9_]*)\s*\))"));
        const QRegularExpression renameRe(
            QStringLiteral(R"(QFile::rename\s*\([^,]+,\s*([A-Za-z_][A-Za-z0-9_]*)\s*\))"));

        // A line is code only if it is not a // comment and not inside a
        // /* */ block — the idiom is quoted verbatim in several
        // explanatory comments (including atomic_write.h's own backstory),
        // and those must not trip the gate.
        auto stripComments = [](const QStringList &raw) {
            QStringList out;
            bool inBlock = false;
            for (QString line : raw) {
                if (inBlock) {
                    const int end = line.indexOf(QStringLiteral("*/"));
                    if (end < 0) { out << QString(); continue; }
                    line = line.mid(end + 2);
                    inBlock = false;
                }
                int guard = 0;
                while (guard++ < 8) {
                    const int start = line.indexOf(QStringLiteral("/*"));
                    if (start < 0) break;
                    const int end = line.indexOf(QStringLiteral("*/"), start + 2);
                    if (end < 0) { line = line.left(start); inBlock = true; break; }
                    line = line.left(start) + line.mid(end + 2);
                }
                const int slashes = line.indexOf(QStringLiteral("//"));
                if (slashes >= 0) line = line.left(slashes);
                out << line;
            }
            return out;
        };

        QStringList offenders;
        for (const QString &name : files) {
            QFile f(d.filePath(name));
            if (!f.open(QIODevice::ReadOnly | QIODevice::Text)) continue;
            QTextStream in(&f);
            const QStringList raw   = in.readAll().split(QChar('\n'));
            const QStringList lines = stripComments(raw);

            for (int i = 0; i < lines.size(); ++i) {
                const auto rm = removeRe.match(lines.at(i));
                if (!rm.hasMatch()) continue;
                const QString removedVar = rm.captured(1);

                // An explicit, justified exemption within 6 lines above.
                bool exempt = false;
                for (int k = qMax(0, i - 6); k <= i; ++k)
                    if (raw.at(k).contains(QStringLiteral("NP-ATOMIC-REVIEWED")))
                        exempt = true;
                if (exempt) continue;

                for (int j = i + 1; j < qMin(i + 4, lines.size()); ++j) {
                    const auto rn = renameRe.match(lines.at(j));
                    if (rn.hasMatch() && rn.captured(1) == removedVar) {
                        offenders << QStringLiteral("%1:%2  removes '%3' then renames onto it")
                                         .arg(name).arg(i + 1).arg(removedVar);
                    }
                }
            }
        }

        if (!offenders.isEmpty()) {
            QFAIL(qPrintable(
                QStringLiteral(
                    "remove-then-rename reintroduced — this deletes the user's file "
                    "before the replace and loses BOTH copies when the rename fails. "
                    "Use NpAtomic::replaceFile() from src/atomic_write.h instead.\n  ")
                + offenders.join(QStringLiteral("\n  "))));
        }
    }

    // ── 8. The four historically-broken call sites use the helper ─────
    // (or QSaveFile, which renames over the target on commit and never
    // removes it first; dbconnections.cpp moved to it for owner-only perms)
    void knownCallSitesUseTheHelper() {
        const QStringList mustUse = {
            QStringLiteral("src/notes_storage.cpp"),
            QStringLiteral("src/ai_tools.cpp"),
            QStringLiteral("src/dbconnections.cpp"),
            QStringLiteral("src/aipanel.cpp"),
        };
        QStringList missing;
        for (const QString &rel : mustUse) {
            const QString path = QStringLiteral(NOTEPATRA_SOURCE_DIR) + "/" + rel;
            QFile f(path);
            QVERIFY2(f.open(QIODevice::ReadOnly | QIODevice::Text),
                     qPrintable("cannot read " + path));
            const QString body = QString::fromUtf8(f.readAll());
            if (!body.contains(QStringLiteral("NpAtomic::replaceFile("))
                && !body.contains(QStringLiteral("NpAtomic::writeFileNoFollow("))
                && !body.contains(QStringLiteral("QSaveFile f("))) missing << rel;
        }
        QVERIFY2(missing.isEmpty(),
                 qPrintable("must route the save through NpAtomic::replaceFile / writeFileNoFollow or QSaveFile: " +
                            missing.join(QStringLiteral(", "))));
    }
};

QTEST_MAIN(TestAtomicWrite)
#include "test_atomic_write.moc"
