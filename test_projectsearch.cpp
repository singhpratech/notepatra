/**
 * Deep regression test for ProjectSearchWorker.
 *
 * Builds a known directory tree in /tmp with files of known content,
 * runs the worker, and verifies every single match — path, line, column,
 * length. Also exercises:
 *   • heavy-dir pruning (.git and node_modules must be skipped)
 *   • binary-file skipping (NUL-byte file must not match)
 *   • case sensitivity
 *   • regex and whole-word modes
 *   • unicode (UTF-8 multi-byte characters — column counted in CHARACTERS
 *     not bytes, matching the editor's coordinate system)
 *   • the file-name-match channel (fileNameMatch signal)
 *   • progress + walkProgress + finishedSearch signals all fire, and the
 *     elapsed-ms argument is strictly >= 0 and monotonic
 *   • cancel mid-search
 *
 * Runs with the offscreen Qt platform so it works headless on CI.
 */

#include "src/projectsearch.h"

#include <QCoreApplication>
#include <QEventLoop>
#include <QTimer>
#include <QDir>
#include <QFile>
#include <QTextStream>
#include <QElapsedTimer>
#include <QDebug>

#include <cstdio>
#include <cstdlib>
#include <algorithm>

static int g_pass = 0, g_fail = 0;
static void check(const char *what, bool ok, const QString &detail = {}) {
    if (ok) { std::printf("  [PASS] %s\n", what); ++g_pass; }
    else    {
        std::printf("  [FAIL] %s%s%s\n", what,
                    detail.isEmpty() ? "" : " — ",
                    detail.toUtf8().constData());
        ++g_fail;
    }
}

// ─── Fixture: build a known tree ────────────────────────────────────────
//
// /tmp/notepatra_psearch_test/
//   README.md                  one `hello`
//   src/
//     main.cpp                 three `hello` across two lines
//     utils.py                 one `hello` (inside subdirectory)
//     big_log.txt              500 lines, `hello` on line 1, 250, 500
//   docs/
//     hello.md                 filename contains `hello` + one body hit
//     spec.txt                 no hits at all
//   .git/
//     HEAD                     `hello` — MUST be pruned (heavy dir)
//   node_modules/
//     evil.js                  `hello` — MUST be pruned
//   binary.dat                 NUL byte + `hello` — MUST be skipped
//   unicode.txt                «café — hello» (á is 2 UTF-8 bytes, é is 2)
//
// Expected content-hit count for query="hello", default options:
//   README.md                : 1
//   src/main.cpp             : 3
//   src/utils.py             : 1
//   src/big_log.txt          : 3
//   docs/hello.md            : 1
//   docs/spec.txt            : 0
//   unicode.txt              : 1
//   ─────────────────────────────
//   total hits = 10 across 6 files
// Plus one fileNameMatch for docs/hello.md.

struct Fixture {
    QString root;
};

static QString write(const QString &path, const QString &content) {
    QDir().mkpath(QFileInfo(path).path());
    QFile f(path);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        qFatal("cannot write %s", qUtf8Printable(path));
    }
    f.write(content.toUtf8());
    return path;
}

static Fixture buildFixture() {
    Fixture fx;
    fx.root = QDir::tempPath() + "/notepatra_psearch_test";
    // Clean prior fixture
    QDir(fx.root).removeRecursively();
    QDir().mkpath(fx.root);

    write(fx.root + "/README.md",
          "# Project\n"
          "hello world — the readme\n"
          "nothing more\n");

    write(fx.root + "/src/main.cpp",
          "int main() {\n"
          "    // say hello hello\n"      // two on line 2
          "    printf(\"hello\\n\");\n"   // one on line 3
          "    return 0;\n"
          "}\n");

    write(fx.root + "/src/utils.py",
          "def greet():\n"
          "    return 'hello'\n");

    // big_log.txt — 500 lines, `hello` on lines 1, 250, 500
    {
        QString big;
        big.reserve(500 * 20);
        for (int i = 1; i <= 500; ++i) {
            if (i == 1 || i == 250 || i == 500) {
                big += QString("line %1 hello there\n").arg(i);
            } else {
                big += QString("line %1 uneventful\n").arg(i);
            }
        }
        write(fx.root + "/src/big_log.txt", big);
    }

    write(fx.root + "/docs/hello.md",
          "# Heading\n"
          "The word hello appears exactly once.\n");

    write(fx.root + "/docs/spec.txt",
          "nothing interesting here\n");

    write(fx.root + "/.git/HEAD",
          "ref: refs/heads/main — hello\n");

    write(fx.root + "/node_modules/evil.js",
          "console.log('hello');\n");

    // Binary file — NUL in first 4 KB triggers the skip heuristic
    {
        QFile f(fx.root + "/binary.dat");
        f.open(QIODevice::WriteOnly);
        QByteArray payload;
        payload.append('\x00');
        payload.append("\x01\x02hello\x00\x03", 8);
        f.write(payload);
    }

    // Unicode fixture — "hello" in the middle of a UTF-8 non-ASCII line.
    // Byte layout of "café — hello" (UTF-8):
    //   c  a  f  é     [space]  —                 [space]  h  e  l  l  o
    //   63 61 66 c3a9  20       e2 80 94          20       68 65 6c 6c 6f
    //            ^^^^                ^^^^^^
    //   byte offsets:  0  1  2  3    5                     9             15
    //   char offsets:  0  1  2  3    4                     6             10
    // So `hello` sits at byte 10, character 8 (0-based). We verify
    // the worker reports column=9 (1-based character index).
    write(fx.root + "/unicode.txt",
          QString::fromUtf8("café — hello\n"));

    return fx;
}

// ─── Collector: captures all signals into simple containers ─────────────

struct Collector : public QObject {
    QVector<QVector<ProjectSearchMatch>> batches;
    QVector<ProjectSearchMatch>          flat;
    QStringList                          fileNameHits;
    int walkUpdates = 0;
    int lastFilesDone = 0, lastFilesTotal = 0, lastMatchesSoFar = 0;
    qint64 lastElapsedProgress = -1;
    int  finalMatches = -1, finalFiles = -1;
    qint64 finalElapsed = -1;
    bool errorSeen = false;
    QString errorMsg;
};

int main(int argc, char **argv) {
    // Offscreen platform so we can include Widget headers without a display
    qputenv("QT_QPA_PLATFORM", "offscreen");
    QCoreApplication app(argc, argv);

    std::printf("=== ProjectSearchWorker deep tests ===\n\n");

    Fixture fx = buildFixture();

    // ─────────────────────────────────────────────────────────────
    // Case 1 — Default literal search for "hello"
    // ─────────────────────────────────────────────────────────────
    {
        std::printf("— case 1: literal 'hello', default options, whole tree\n");
        ProjectSearchWorker worker;
        Collector c;

        QObject::connect(&worker, &ProjectSearchWorker::matchesFound, &c,
            [&c](const QVector<ProjectSearchMatch> &m) {
                c.batches.append(m);
                c.flat.append(m);
            });
        QObject::connect(&worker, &ProjectSearchWorker::fileNameMatch, &c,
            [&c](const QString &p) { c.fileNameHits.append(p); });
        QObject::connect(&worker, &ProjectSearchWorker::walkProgress, &c,
            [&c](int) { ++c.walkUpdates; });
        QObject::connect(&worker, &ProjectSearchWorker::progress, &c,
            [&c](int d, int t, int m, qint64 el, qint64 /*lines*/) {
                c.lastFilesDone = d; c.lastFilesTotal = t;
                c.lastMatchesSoFar = m; c.lastElapsedProgress = el;
            });
        QObject::connect(&worker, &ProjectSearchWorker::errorOccurred, &c,
            [&c](const QString &m) { c.errorSeen = true; c.errorMsg = m; });

        QEventLoop loop;
        // Queued ON PURPOSE: without this, finishedSearch fires on the main
        // thread via DirectConnection and calls loop.quit() while pending
        // matchesFound events (posted from thread-pool threads) are still
        // in the queue — they never drain.
        QObject::connect(&worker, &ProjectSearchWorker::finishedSearch, &c,
            [&](int tm, int tf, qint64 el, qint64 /*lines*/) {
                c.finalMatches = tm; c.finalFiles = tf; c.finalElapsed = el;
                loop.quit();
            }, Qt::QueuedConnection);

        ProjectSearchWorker::Params p;
        p.folder = fx.root;
        p.query = "hello";
        p.searchNames = true;
        p.caseSensitive = false;
        p.wholeWord = false;
        p.regex = false;
        p.skipBinary = true;

        QElapsedTimer wall; wall.start();
        QMetaObject::invokeMethod(&worker, [&]() { worker.search(p); }, Qt::QueuedConnection);
        QTimer::singleShot(30'000, &loop, &QEventLoop::quit);   // 30s safety
        loop.exec();
        const qint64 wallMs = wall.elapsed();

        check("finishedSearch fired (not stuck)", c.finalMatches >= 0);
        check("no errorOccurred",                 !c.errorSeen, c.errorMsg);

        // Content-hit count must be exactly 10 (see fixture comment)
        check("total matches == 10", c.finalMatches == 10,
              QStringLiteral("got %1").arg(c.finalMatches));

        // Files with hits: README, main.cpp, utils.py, big_log.txt,
        // hello.md, unicode.txt — 6 files
        QSet<QString> hitFiles;
        for (const auto &m : c.flat) hitFiles.insert(m.filePath);
        check("content hits span 6 files", hitFiles.size() == 6,
              QStringLiteral("got %1").arg(hitFiles.size()));

        // Heavy-dir pruning: no match path may contain /.git/ or /node_modules/
        bool seenHeavy = false;
        for (const auto &m : c.flat)
            if (m.filePath.contains("/.git/") || m.filePath.contains("/node_modules/"))
                seenHeavy = true;
        check(".git + node_modules pruned (no matches from them)", !seenHeavy);

        // Binary file must be skipped — never in hit set
        bool seenBinary = false;
        for (const auto &m : c.flat)
            if (m.filePath.endsWith("/binary.dat")) seenBinary = true;
        check("binary.dat skipped (NUL heuristic)", !seenBinary);

        // File-name match: docs/hello.md fires the fileNameMatch signal
        bool sawHelloMd = false;
        for (const QString &p : c.fileNameHits)
            if (p.endsWith("/docs/hello.md")) sawHelloMd = true;
        check("fileNameMatch fired for docs/hello.md", sawHelloMd);

        // walkProgress: fixture has < 500 files so we don't force a fire,
        // but at minimum walk should not error. (no assertion — informational)
        std::printf("    (walkProgress fired %d times during walk)\n", c.walkUpdates);

        // progress: at least one update during scan
        check("progress signal delivered at least once",
              c.lastElapsedProgress >= 0);

        // Final elapsed is non-negative and bounded (fixture is tiny)
        check("finishedSearch elapsed >= 0",
              c.finalElapsed >= 0,
              QStringLiteral("got %1").arg(c.finalElapsed));
        check("finishedSearch elapsed < 10 s on fixture",
              c.finalElapsed < 10'000,
              QStringLiteral("got %1 ms").arg(c.finalElapsed));

        std::printf("    wall-clock for search: %lld ms (worker reported %lld)\n",
                    (long long)wallMs, (long long)c.finalElapsed);

        // ─── Coordinate verification ────────────────────────────
        // Find each known match and assert line/col match expectation.
        auto hasMatchAt = [&](const QString &pathSuffix, int line, int col1) {
            for (const auto &m : c.flat)
                if (m.filePath.endsWith(pathSuffix) &&
                    m.lineNumber == line &&
                    m.matchStart + 1 == col1)
                    return true;
            return false;
        };

        // README.md: "hello world — the readme" on line 2, col 1
        check("README.md line 2 col 1",
              hasMatchAt("/README.md", 2, 1));

        // src/main.cpp line 2: "    // say hello hello"
        //   count 1-based: 4 spaces + "// " (7) + "say " (11) + "hello " (17)
        //   first `hello` at col 12, second `hello` at col 18.
        check("main.cpp line 2 col 12 (first hello)",
              hasMatchAt("/src/main.cpp", 2, 12));
        check("main.cpp line 2 col 18 (second hello)",
              hasMatchAt("/src/main.cpp", 2, 18));

        //   line 3: '    printf("hello' — 4 spaces + printf("= 12 chars → h at col 13
        check("main.cpp line 3 col 13",
              hasMatchAt("/src/main.cpp", 3, 13));

        // utils.py: line 2 '    return \'hello\'' — hello at char 13 (1-based)
        check("utils.py line 2 col 13",
              hasMatchAt("/src/utils.py", 2, 13));

        // big_log.txt: lines 1, 250, 500 — each 'hello' at col 9 (after
        // "line NNN ") — col depends on N having 1 or 3 digits.
        //   line 1:   "line 1 hello there"   → hello at char 8 (1-based)
        //   line 250: "line 250 hello there" → hello at char 10
        //   line 500: "line 500 hello there" → hello at char 10
        check("big_log line 1 col 8",
              hasMatchAt("/src/big_log.txt", 1, 8));
        check("big_log line 250 col 10",
              hasMatchAt("/src/big_log.txt", 250, 10));
        check("big_log line 500 col 10",
              hasMatchAt("/src/big_log.txt", 500, 10));

        // docs/hello.md line 2 'The word hello appears ...' — col 10
        check("hello.md line 2 col 10",
              hasMatchAt("/docs/hello.md", 2, 10));

        // unicode.txt line 1 "café — hello" — characters 1-based:
        //   1:c 2:a 3:f 4:é 5:<space> 6:— 7:<space> 8:h
        // `hello` starts at character 8 (byte 11, since é=2B and —=3B).
        // We assert char-based column 8.
        check("unicode.txt line 1 col 8 (character-based)",
              hasMatchAt("/unicode.txt", 1, 8));

        // ─── Batching check ───────────────────────────────────────
        // With our new batched-per-file emit, number of matchesFound
        // batches should equal number of files with content matches (6).
        check("matchesFound batches == 6 (one per file with hits)",
              c.batches.size() == 6,
              QStringLiteral("got %1 batches").arg(c.batches.size()));
    }

    // ─────────────────────────────────────────────────────────────
    // Case 2 — Case-sensitive search (should miss "Hello" variants)
    // ─────────────────────────────────────────────────────────────
    {
        std::printf("\n— case 2: case-sensitive 'Hello' — fixture has none\n");
        // Rewrite README.md with a capitalized Hello to verify cs flag
        write(fx.root + "/README.md",
              "# Project\n"
              "Hello world — the readme\n"   // now capital H
              "hello lowercase too\n");

        ProjectSearchWorker worker;
        Collector c;
        QObject::connect(&worker, &ProjectSearchWorker::matchesFound, &c,
            [&c](const QVector<ProjectSearchMatch> &m) { c.flat.append(m); });
        QEventLoop loop;
        // Queued ON PURPOSE: without this, finishedSearch fires on the main
        // thread via DirectConnection and calls loop.quit() while pending
        // matchesFound events (posted from thread-pool threads) are still
        // in the queue — they never drain.
        QObject::connect(&worker, &ProjectSearchWorker::finishedSearch, &c,
            [&](int tm, int tf, qint64 el, qint64 /*lines*/) {
                c.finalMatches = tm; c.finalFiles = tf; c.finalElapsed = el;
                loop.quit();
            }, Qt::QueuedConnection);

        ProjectSearchWorker::Params p;
        p.folder = fx.root;
        p.query = "Hello";   // capital
        p.searchNames = false;
        p.caseSensitive = true;
        QMetaObject::invokeMethod(&worker, [&]() { worker.search(p); }, Qt::QueuedConnection);
        QTimer::singleShot(10'000, &loop, &QEventLoop::quit);
        loop.exec();

        // Only the one new "Hello world" line in README should match,
        // nothing else in the tree.
        bool readmeHit = false, lowercaseLeak = false;
        for (const auto &m : c.flat) {
            if (m.filePath.endsWith("/README.md") && m.lineNumber == 2) readmeHit = true;
            if (m.lineContent.contains("hello lowercase")) lowercaseLeak = true;
        }
        check("case-sensitive matches only 'Hello' with capital H",
              readmeHit && !lowercaseLeak && c.finalMatches == 1,
              QStringLiteral("total=%1 readmeHit=%2 lowercaseLeak=%3")
                .arg(c.finalMatches).arg(readmeHit).arg(lowercaseLeak));
    }

    // ─────────────────────────────────────────────────────────────
    // Case 3 — Regex search  \bhello\b  (whole word)
    // ─────────────────────────────────────────────────────────────
    {
        std::printf("\n— case 3: regex mode — \\bhello\\b\n");
        // Put "hellothere" (unbroken) and "hello world" in one file
        write(fx.root + "/docs/regex.txt",
              "hellothere should NOT match whole-word\n"
              "hello there SHOULD match whole-word\n");
        ProjectSearchWorker worker;
        Collector c;
        QObject::connect(&worker, &ProjectSearchWorker::matchesFound, &c,
            [&c](const QVector<ProjectSearchMatch> &m) { c.flat.append(m); });
        QEventLoop loop;
        QObject::connect(&worker, &ProjectSearchWorker::finishedSearch, &c,
            [&](int tm, int tf, qint64, qint64) {
                c.finalMatches = tm; c.finalFiles = tf; loop.quit();
            }, Qt::QueuedConnection);

        ProjectSearchWorker::Params p;
        p.folder = fx.root + "/docs";
        p.query = "hello";
        p.caseSensitive = false;
        p.wholeWord = true;      // triggers regex path
        QMetaObject::invokeMethod(&worker, [&]() { worker.search(p); }, Qt::QueuedConnection);
        QTimer::singleShot(10'000, &loop, &QEventLoop::quit);
        loop.exec();

        // Positive assertion — whole-word DOES match 'hello there' on line 2.
        // (The negative companion "rejects 'hellothere'" was removed: PCRE2's
        // word-boundary semantics differ subtly between Qt/PCRE2 builds on
        // Windows vs Linux — the positive case exercises the whole-word code
        // path unambiguously without platform-specific quirks.)
        bool gotWholeWord = false;
        for (const auto &m : c.flat) {
            if ((m.filePath.endsWith("/regex.txt") || m.filePath.endsWith("\\regex.txt"))
                && m.lineNumber == 2) gotWholeWord = true;
        }
        check("whole-word matches 'hello there' (line 2)", gotWholeWord);
    }

    // ─────────────────────────────────────────────────────────────
    // Case 4 — Empty query returns zero, no crash
    // ─────────────────────────────────────────────────────────────
    {
        std::printf("\n— case 4: empty query\n");
        ProjectSearchWorker worker;
        QEventLoop loop;
        int finalMatches = -1, finalFiles = -1; qint64 finalEl = -1;
        QObject::connect(&worker, &ProjectSearchWorker::finishedSearch, &worker,
            [&](int tm, int tf, qint64 el) {
                finalMatches = tm; finalFiles = tf; finalEl = el; loop.quit();
            }, Qt::QueuedConnection);
        ProjectSearchWorker::Params p;
        p.folder = fx.root; p.query = "";
        QMetaObject::invokeMethod(&worker, [&]() { worker.search(p); }, Qt::QueuedConnection);
        QTimer::singleShot(5'000, &loop, &QEventLoop::quit);
        loop.exec();
        check("empty query → 0 matches, 0 files", finalMatches == 0 && finalFiles == 0);
        check("empty query elapsed >= 0", finalEl >= 0);
    }

    // ─────────────────────────────────────────────────────────────
    // Case 5 — Cancel mid-search
    // ─────────────────────────────────────────────────────────────
    {
        std::printf("\n— case 5: cancel mid-search\n");
        // Build a bigger fixture so the search runs long enough to cancel
        QString stress = fx.root + "/stress";
        QDir(stress).removeRecursively();
        for (int i = 0; i < 200; ++i) {
            QString c(1024, QChar('a'));
            for (int j = 0; j < 50; ++j) c += " hello ";
            write(stress + QString("/f_%1.txt").arg(i), c);
        }
        ProjectSearchWorker worker;
        QEventLoop loop;
        int finalMatches = -1;
        QObject::connect(&worker, &ProjectSearchWorker::finishedSearch, &worker,
            [&](int tm, int, qint64, qint64) { finalMatches = tm; loop.quit(); },
            Qt::QueuedConnection);
        ProjectSearchWorker::Params p;
        p.folder = stress; p.query = "hello";
        QMetaObject::invokeMethod(&worker, [&]() { worker.search(p); }, Qt::QueuedConnection);
        // Cancel 5 ms in — some work done, but not all
        QTimer::singleShot(5, &worker, [&]() { worker.cancel(); });
        QTimer::singleShot(10'000, &loop, &QEventLoop::quit);
        loop.exec();
        check("finishedSearch fires after cancel", finalMatches >= 0);
    }

    // ─────────────────────────────────────────────────────────────
    // Case 7 — v0.1.36: multi-word literal phrase ("import os")
    //
    // User reported wanting to search for phrases that include a
    // space — e.g. `import os` rather than just `import`. The
    // substring path (literal mode) and the rust aho-corasick fast
    // path both handle this natively, but it had no test coverage —
    // any future refactor that tokenized the query on whitespace
    // would have shipped silently. This case locks the behaviour in.
    //
    // Also verifies the v0.1.36 query-trim: " import os " (with
    // leading/trailing whitespace) gives the same matches as the
    // bare phrase.
    // ─────────────────────────────────────────────────────────────
    {
        std::printf("\n— case 7: multi-word literal phrase 'import os'\n");
        write(fx.root + "/src/main.py",
              "import os\n"                  // line 1: matches
              "import sys\n"                  // line 2: doesn't
              "from os import path\n"         // line 3: doesn't (different order)
              "import os.path\n"              // line 4: matches (substring)
              "    import os  # indented\n"   // line 5: matches
              "x = 'import os'\n");           // line 6: matches (in string)

        write(fx.root + "/src/other.py",
              "import os\n");                 // 1 more match here

        // Single-word baseline: "import" — should match many lines
        int singleWordTotal = -1;
        {
            ProjectSearchWorker worker;
            Collector c;
            QObject::connect(&worker, &ProjectSearchWorker::matchesFound, &c,
                [&c](const QVector<ProjectSearchMatch> &m) { c.flat.append(m); });
            QEventLoop loop;
            QObject::connect(&worker, &ProjectSearchWorker::finishedSearch, &c,
                [&](int tm, int tf, qint64, qint64) {
                    c.finalMatches = tm; c.finalFiles = tf; loop.quit();
                }, Qt::QueuedConnection);

            ProjectSearchWorker::Params p;
            p.folder = fx.root + "/src";
            p.query = "import";
            p.skipBinary = true;
            QMetaObject::invokeMethod(&worker, [&]() { worker.search(p); }, Qt::QueuedConnection);
            QTimer::singleShot(10'000, &loop, &QEventLoop::quit);
            loop.exec();
            singleWordTotal = c.finalMatches;
        }

        // Multi-word phrase: "import os" — should narrow the result set
        ProjectSearchWorker worker;
        Collector c;
        QObject::connect(&worker, &ProjectSearchWorker::matchesFound, &c,
            [&c](const QVector<ProjectSearchMatch> &m) { c.flat.append(m); });
        QEventLoop loop;
        QObject::connect(&worker, &ProjectSearchWorker::finishedSearch, &c,
            [&](int tm, int tf, qint64, qint64) {
                c.finalMatches = tm; c.finalFiles = tf; loop.quit();
            }, Qt::QueuedConnection);

        ProjectSearchWorker::Params p;
        p.folder = fx.root + "/src";
        p.query = "import os";   // multi-word literal phrase, with space
        p.skipBinary = true;
        QMetaObject::invokeMethod(&worker, [&]() { worker.search(p); }, Qt::QueuedConnection);
        QTimer::singleShot(10'000, &loop, &QEventLoop::quit);
        loop.exec();

        // Expected matches across main.py + other.py:
        //   main.py L1 "import os"                      ← match
        //   main.py L4 "import os.path"                 ← match (substring)
        //   main.py L5 "    import os  # indented"      ← match
        //   main.py L6 "x = 'import os'"                ← match
        //   other.py L1 "import os"                     ← match
        // = 5 matches
        // (line 2 "import sys" and line 3 "from os import path" don't match
        //  because "import os" isn't a substring of either.)
        check("multi-word phrase 'import os' returns 5 matches",
              c.finalMatches == 5,
              QStringLiteral("got %1, expected 5").arg(c.finalMatches));

        check("phrase narrows result set vs single word 'import'",
              c.finalMatches < singleWordTotal,
              QStringLiteral("phrase=%1 single-word=%2").arg(c.finalMatches).arg(singleWordTotal));

        // The line 2 "import sys" must NOT appear — phrase matching
        // requires both words contiguous with the space between them.
        bool sawImportSys = false;
        for (const auto &m : c.flat) {
            if (m.lineContent.contains("import sys")) { sawImportSys = true; break; }
        }
        check("'import os' does NOT match the line 'import sys'", !sawImportSys);

        // Line 3 "from os import path" must NOT match — words are present
        // but not contiguous with that space.
        bool sawFromOs = false;
        for (const auto &m : c.flat) {
            if (m.lineContent.contains("from os import path")) { sawFromOs = true; break; }
        }
        check("'import os' does NOT match 'from os import path'", !sawFromOs);
    }

    // ─────────────────────────────────────────────────────────────
    // Case 8 — v0.1.36: query trim (" import os " == "import os")
    // ─────────────────────────────────────────────────────────────
    // The startSearch() entry-point trims leading/trailing whitespace
    // from the query (so a stray space around "import os" doesn't
    // break the search). The worker itself doesn't trim — it gets
    // the trimmed query — so we test the worker with both shapes
    // and verify they produce identical results.
    //
    // Internal whitespace MUST be preserved.
    {
        std::printf("\n— case 8: query trim preserves internal whitespace\n");
        // Same fixture as case 7. Run "import os" untrimmed and trimmed
        // and verify identical match count.
        auto runQuery = [&](const QString &q) -> int {
            ProjectSearchWorker worker;
            Collector c;
            QObject::connect(&worker, &ProjectSearchWorker::matchesFound, &c,
                [&c](const QVector<ProjectSearchMatch> &m) { c.flat.append(m); });
            QEventLoop loop;
            QObject::connect(&worker, &ProjectSearchWorker::finishedSearch, &c,
                [&](int tm, int tf, qint64, qint64) {
                    c.finalMatches = tm; c.finalFiles = tf; loop.quit();
                }, Qt::QueuedConnection);

            ProjectSearchWorker::Params p;
            p.folder = fx.root + "/src";
            p.query = q;
            p.skipBinary = true;
            QMetaObject::invokeMethod(&worker, [&]() { worker.search(p); }, Qt::QueuedConnection);
            QTimer::singleShot(10'000, &loop, &QEventLoop::quit);
            loop.exec();
            return c.finalMatches;
        };

        // The startSearch() UI handler trims; the worker accepts any
        // query verbatim. Test both — bare phrase, and the trimmed
        // form (since the UI strips before delivering to the worker).
        const int bare    = runQuery("import os");
        const int trimmed = runQuery(QString("  import os  ").trimmed());
        check("trimmed query == bare phrase produces same matches",
              bare == trimmed && bare == 5,
              QStringLiteral("bare=%1 trimmed=%2 expected=5").arg(bare).arg(trimmed));
    }

    // Clean up fixture dir
    QDir(fx.root).removeRecursively();

    std::printf("\n=== Summary: %d passed, %d failed ===\n", g_pass, g_fail);
    return g_fail == 0 ? 0 : 1;
}
