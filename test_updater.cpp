/**
 * Deep test for Updater pure logic — asset picker + SHA256SUMS parser.
 *
 * We cannot exercise the full installReleaseInteractive() path in unit
 * tests (it needs real network + a real release + a real GUI), but the
 * two PURE functions are where the risk lies: picking the wrong asset
 * means downloading a file for the wrong platform; parsing SHA256SUMS
 * wrong means verification silently passes on a tampered file.
 *
 * Both cases are safety-critical. Every assertion below maps to a
 * concrete way the updater could silently install the wrong thing.
 *
 * Runs headless with QCoreApplication — no network, no widgets, no
 * event loop.
 */

#include "src/updater.h"
#include "src/build_flavor.h"   // NOTEPATRA_BUILD_IS_FULL / _IS_LOCAL_AI

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QString>
#include <QSysInfo>
#include <QTemporaryDir>

#include <cstdio>

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

// Build a fake `assets` array that looks exactly like GitHub's JSON
static QJsonArray mkAssets(std::initializer_list<std::pair<const char*, const char*>> items) {
    QJsonArray a;
    for (const auto &p : items) {
        QJsonObject o;
        o["name"] = QString::fromLatin1(p.first);
        o["browser_download_url"] = QString::fromLatin1(p.second);
        o["size"] = 1024;
        a.append(o);
    }
    return a;
}

int main(int argc, char **argv) {
    QCoreApplication app(argc, argv);
    std::printf("=== Updater pure-logic deep tests ===\n\n");
    std::printf("(running on platform: %s / %s)\n\n",
                qUtf8Printable(QSysInfo::kernelType()),
                qUtf8Printable(QSysInfo::currentCpuArchitecture()));

    // ═══════════════════════════════════════════════════════════════
    // Part A — parseSha256For
    // ═══════════════════════════════════════════════════════════════
    std::printf("— parseSha256For ─────────────────────────────────\n");

    {
        const QString body =
            "a1b2c3d4e5f6789012345678901234567890123456789012345678901234abcd  Notepatra-0.1.18-linux-x86_64.AppImage\n"
            "deadbeefcafebabe00112233445566778899aabbccddeeff00112233445566aa  Notepatra-0.1.18.dmg\n"
            "0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef  Notepatra-0.1.18.msi\n";

        check("exact filename match (AppImage)",
              Updater::parseSha256For(body, "Notepatra-0.1.18-linux-x86_64.AppImage")
                  == "a1b2c3d4e5f6789012345678901234567890123456789012345678901234abcd");
        check("exact filename match (DMG)",
              Updater::parseSha256For(body, "Notepatra-0.1.18.dmg")
                  == "deadbeefcafebabe00112233445566778899aabbccddeeff00112233445566aa");
        check("exact filename match (MSI)",
              Updater::parseSha256For(body, "Notepatra-0.1.18.msi")
                  == "0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef");

        check("unknown filename returns empty",
              Updater::parseSha256For(body, "does-not-exist.zip").isEmpty());

        // SECURITY CHECK: must not pick up a hash for a *different* file
        // just because the query name is a substring.
        check("no false-positive substring match",
              Updater::parseSha256For(body, "0.1.18.dmg").isEmpty());
    }

    {
        // BSD-style output with `*` binary-mode marker and CRLFs
        const QString body =
            "# comment line — should be ignored\r\n"
            "\r\n"
            "feedface00000000feedface00000000feedface00000000feedface00000000 *release/Notepatra-0.1.18.msi\r\n";
        check("CRLF + comment + '*' marker + relative path",
              Updater::parseSha256For(body, "Notepatra-0.1.18.msi")
                  == "feedface00000000feedface00000000feedface00000000feedface00000000");
    }

    {
        // Must accept both bare basename AND relative path ending in basename
        const QString body =
            "cafed00dcafed00dcafed00dcafed00dcafed00dcafed00dcafed00dcafed00d  dist/linux/Notepatra-0.1.18-linux-x86_64.AppImage\n";
        check("path-prefixed name — matched by basename",
              Updater::parseSha256For(body, "Notepatra-0.1.18-linux-x86_64.AppImage")
                  == "cafed00dcafed00dcafed00dcafed00dcafed00dcafed00dcafed00dcafed00d");
    }

    {
        // SAFETY: short or malformed hash rows must be rejected, not returned.
        const QString body =
            "shorty  Notepatra-0.1.18.dmg\n"
            "way-too-long-to-be-a-sha-256-hexdigest-right-here  Notepatra-0.1.18.dmg\n";
        check("reject row with non-64-char hash field",
              Updater::parseSha256For(body, "Notepatra-0.1.18.dmg").isEmpty());
    }

    {
        check("empty body returns empty",
              Updater::parseSha256For("", "anything.zip").isEmpty());
    }

    // ═══════════════════════════════════════════════════════════════
    // Part B — pickAssetForPlatform (observes current runtime platform)
    // ═══════════════════════════════════════════════════════════════
    std::printf("\n— pickAssetForPlatform (runtime platform) ────────\n");

#if defined(Q_OS_LINUX)
    const bool isLinux = true, isMac = false, isWin = false;
#elif defined(Q_OS_MAC)
    const bool isLinux = false, isMac = true, isWin = false;
#elif defined(Q_OS_WIN)
    const bool isLinux = false, isMac = false, isWin = true;
#else
    const bool isLinux = false, isMac = false, isWin = false;
#endif

    const QString archLower = QSysInfo::currentCpuArchitecture().toLower();
    const bool isArm64 = archLower.contains("arm64") || archLower.contains("aarch64");

    {
        // A "real" release with all platforms present — on this machine,
        // the picker MUST pick the one matching our OS+arch.
        QJsonArray assets = mkAssets({
            {"Notepatra-0.1.18-linux-x86_64.AppImage",
             "https://example.test/Notepatra-0.1.18-linux-x86_64.AppImage"},
            {"Notepatra-0.1.18-linux-aarch64.AppImage",
             "https://example.test/Notepatra-0.1.18-linux-aarch64.AppImage"},
            {"Notepatra-0.1.18.dmg",
             "https://example.test/Notepatra-0.1.18.dmg"},
            {"Notepatra-0.1.18.msi",
             "https://example.test/Notepatra-0.1.18.msi"},
            {"Notepatra-Setup-0.1.18.exe",
             "https://example.test/Notepatra-Setup-0.1.18.exe"},
            {"Notepatra-0.1.18-windows-x64.zip",
             "https://example.test/Notepatra-0.1.18-windows-x64.zip"},
            {"SHA256SUMS",
             "https://example.test/SHA256SUMS"},
            {"Notepatra-0.1.18-linux-x86_64.AppImage.sig",
             "https://example.test/Notepatra-0.1.18-linux-x86_64.AppImage.sig"},
        });

        auto picked = Updater::pickAssetForPlatform(assets);
        check("picker returns found=true for complete release", picked.found);

        if (isLinux && !isArm64) {
            check("linux/x86_64 → AppImage x86_64",
                  picked.name == "Notepatra-0.1.18-linux-x86_64.AppImage",
                  QStringLiteral("got %1").arg(picked.name));
        } else if (isLinux && isArm64) {
            check("linux/aarch64 → AppImage aarch64",
                  picked.name == "Notepatra-0.1.18-linux-aarch64.AppImage",
                  QStringLiteral("got %1").arg(picked.name));
        } else if (isMac) {
            check("macOS → DMG",
                  picked.name == "Notepatra-0.1.18.dmg",
                  QStringLiteral("got %1").arg(picked.name));
        } else if (isWin) {
            check("windows → MSI (preferred over exe/zip)",
                  picked.name == "Notepatra-0.1.18.msi",
                  QStringLiteral("got %1").arg(picked.name));
        }

        // CRITICAL SAFETY ASSERT: never pick a .sig / .pem / SHA256SUMS
        check("never picks a signature (.sig) file",
              !picked.name.endsWith(".sig"));
        check("never picks the SHA256SUMS file",
              picked.name.compare("SHA256SUMS", Qt::CaseInsensitive) != 0);
    }

    {
        // Release missing this platform's artifact — picker must return
        // found=false so the caller can fall back gracefully.
        QJsonArray onlyMacOS = mkAssets({
            {"Notepatra-0.1.18.dmg", "https://example.test/Notepatra-0.1.18.dmg"},
            {"SHA256SUMS",           "https://example.test/SHA256SUMS"},
        });
        auto p = Updater::pickAssetForPlatform(onlyMacOS);
        if (isLinux || isWin) {
            check("no matching asset → found=false",
                  !p.found,
                  QStringLiteral("got name=%1 found=%2").arg(p.name).arg(p.found));
        }
    }

    {
        // Windows-only release — MSI must beat setup.exe, setup.exe must
        // beat portable zip, so degraded releases still install. This is
        // the priority ladder used in scoreAsset().
        QJsonArray assets = mkAssets({
            {"Notepatra-Setup-0.1.18.exe",  "u1"},
            {"Notepatra-0.1.18-windows-x64.zip", "u2"},
            {"Notepatra-0.1.18.msi",        "u3"},
        });
        auto p = Updater::pickAssetForPlatform(assets);
        if (isWin) {
            check("windows priority ladder: MSI beats exe beats zip",
                  p.name.endsWith(".msi"),
                  QStringLiteral("got %1").arg(p.name));
        }
    }

    {
        // Empty assets array — must not crash, must not return a match.
        auto p = Updater::pickAssetForPlatform({});
        check("empty assets → found=false", !p.found);
        check("empty assets → name empty", p.name.isEmpty());
        check("empty assets → url empty", p.downloadUrl.isEmpty());
    }

    {
        // Malformed entries (missing url, missing name) are ignored.
        QJsonArray garbage;
        QJsonObject bad1; bad1["name"] = "something"; // no url
        QJsonObject bad2; bad2["browser_download_url"] = "https://x"; // no name
        garbage.append(bad1);
        garbage.append(bad2);
        garbage.append(QJsonValue(42));         // wrong type, ignored
        auto p = Updater::pickAssetForPlatform(garbage);
        check("malformed assets → found=false", !p.found);
    }

    // ═══════════════════════════════════════════════════════════════
    // Part C — uniqueDestPath (the v0.1.101 finalize de-dup). Pure path
    // logic, so it runs identically on all three platforms' CI runners —
    // the macOS "could not finalize" bug was a same-named, unremovable
    // prior download; landing on a fresh sibling name is the fix.
    // ═══════════════════════════════════════════════════════════════
    std::printf("\n— uniqueDestPath (finalize de-dup) ───────────────\n");
    {
        QTemporaryDir tmp;
        check("temp dir created", tmp.isValid());
        const QString dl = tmp.path();
        auto touch = [](const QString &p) {
            QFile f(p);
            if (f.open(QIODevice::WriteOnly)) { f.write("x", 1); f.close(); }
        };

        // 1. Nothing there → desired returned unchanged.
        const QString dmg = dl + "/notepatra-macos-arm64.dmg";
        check("free path → returned unchanged",
              Updater::uniqueDestPath(dmg) == dmg);

        // 2. Destination exists (macOS: a still-mounted prior .dmg) → " (1)".
        touch(dmg);
        check("existing .dmg → ' (1).dmg'",
              Updater::uniqueDestPath(dmg) == dl + "/notepatra-macos-arm64 (1).dmg",
              Updater::uniqueDestPath(dmg));

        // 3. base + (1) both exist → " (2)".
        touch(dl + "/notepatra-macos-arm64 (1).dmg");
        check("existing base + (1) → ' (2).dmg'",
              Updater::uniqueDestPath(dmg) == dl + "/notepatra-macos-arm64 (2).dmg",
              Updater::uniqueDestPath(dmg));

        // 4. Linux multi-dot suffix dedupes before the FULL suffix.
        const QString tgz = dl + "/notepatra-linux-x64.tar.gz";
        touch(tgz);
        check("existing .tar.gz → ' (1).tar.gz' (not '.tar (1).gz')",
              Updater::uniqueDestPath(tgz) == dl + "/notepatra-linux-x64 (1).tar.gz",
              Updater::uniqueDestPath(tgz));

        // 5. Windows .msi — version dots must stay in the stem (regression:
        //    a first-dot split produced "notepatra-0 (1).1.101.msi").
        const QString msi = dl + "/notepatra-0.1.101.msi";
        touch(msi);
        check("existing .msi (version dots) → ' (1).msi'",
              Updater::uniqueDestPath(msi) == dl + "/notepatra-0.1.101 (1).msi",
              Updater::uniqueDestPath(msi));

        // 6. Linux .deb (underscore-versioned) and .AppImage (version dots).
        const QString deb = dl + "/notepatra_0.1.101_amd64.deb";
        touch(deb);
        check("existing .deb → ' (1).deb'",
              Updater::uniqueDestPath(deb) == dl + "/notepatra_0.1.101_amd64 (1).deb",
              Updater::uniqueDestPath(deb));

        const QString appimg = dl + "/Notepatra-0.1.101-x86_64.AppImage";
        touch(appimg);
        check("existing .AppImage → ' (1).AppImage'",
              Updater::uniqueDestPath(appimg) == dl + "/Notepatra-0.1.101-x86_64 (1).AppImage",
              Updater::uniqueDestPath(appimg));
    }

    // ═══════════════════════════════════════════════════════════════
    // Part D — flavor-aware, ORDER-INDEPENDENT asset picker. A release
    // ships several installers per platform that differ only in EDITION
    // (Lite/Full × cloud/local-ai). Before the v0.1.116 fix every Windows
    // MSI scored identically, so which variant a user got depended on the
    // order GitHub happened to list the assets — non-deterministic and
    // often the wrong edition. These tests LOCK the deterministic picker:
    // the correct-edition asset must win regardless of array order.
    //
    // We use pickAssetForPlatformEx() — the testing seam that takes the
    // platform / arch / edition explicitly — so this single (Linux, Lite)
    // test binary can exercise every OS × edition combination. Production
    // uses the zero-arg pickAssetForPlatform(), which delegates here with
    // the compile-time edition + runtime platform, so behaviour is identical.
    // ═══════════════════════════════════════════════════════════════
    std::printf("\n— pickAssetForPlatformEx (flavor + order independence) ─\n");

    // Reverse a GitHub-style assets array so we can prove the pick doesn't
    // depend on listing order.
    auto reversed = [](const QJsonArray &a) {
        QJsonArray r;
        for (int i = a.size() - 1; i >= 0; --i) r.append(a.at(i));
        return r;
    };

    // Assert the picker returns `want` for the given platform/arch/edition,
    // AND returns the SAME asset when the array is reversed. The second
    // assertion is the RED-STATE anchor: revert the edition penalty in
    // scoreAsset() and the reversed array picks a different variant → FAIL.
    auto pickCheck = [&](const QByteArray &label, const QJsonArray &assets,
                         const char *plat, const char *arch,
                         bool full, bool ai, const char *want) {
        const auto f = Updater::pickAssetForPlatformEx(assets, plat, arch, full, ai);
        const auto r = Updater::pickAssetForPlatformEx(reversed(assets), plat, arch, full, ai);
        const QByteArray l1 = label + " → picks " + want;
        check(l1.constData(), f.found && f.name == QString::fromLatin1(want),
              QStringLiteral("got %1 (found=%2)").arg(f.name).arg(f.found));
        const QByteArray l2 = label + " → ORDER-INDEPENDENT (reverse == same)";
        check(l2.constData(), r.found && r.name == QString::fromLatin1(want),
              QStringLiteral("forward=%1 reverse=%2").arg(f.name, r.name));
    };

    {
        // All four Windows edition variants of the same release.
        QJsonArray win = mkAssets({
            {"notepatra-0.1.116.msi",              "https://example.test/notepatra-0.1.116.msi"},
            {"notepatra-full-0.1.116.msi",         "https://example.test/notepatra-full-0.1.116.msi"},
            {"notepatra-local-ai-0.1.116.msi",     "https://example.test/notepatra-local-ai-0.1.116.msi"},
            {"notepatra-local-ai-full-0.1.116.msi","https://example.test/notepatra-local-ai-full-0.1.116.msi"},
            {"SHA256SUMS",                         "https://example.test/SHA256SUMS"},
        });

        // Every edition selects its EXACT matching variant, order-independently.
        pickCheck("windows Lite/cloud (full=0 ai=0)", win, "windows", "x86_64",
                  false, false, "notepatra-0.1.116.msi");
        pickCheck("windows Full/cloud (full=1 ai=0)", win, "windows", "x86_64",
                  true, false, "notepatra-full-0.1.116.msi");
        pickCheck("windows Lite/local-ai (full=0 ai=1)", win, "windows", "x86_64",
                  false, true, "notepatra-local-ai-0.1.116.msi");
        pickCheck("windows Full/local-ai (full=1 ai=1)", win, "windows", "x86_64",
                  true, true, "notepatra-local-ai-full-0.1.116.msi");

        // The three-variant array the bug was reported against (no
        // local-ai-full published) — the correct edition still wins.
        QJsonArray win3 = mkAssets({
            {"notepatra-0.1.116.msi",          "https://example.test/notepatra-0.1.116.msi"},
            {"notepatra-full-0.1.116.msi",     "https://example.test/notepatra-full-0.1.116.msi"},
            {"notepatra-local-ai-0.1.116.msi", "https://example.test/notepatra-local-ai-0.1.116.msi"},
        });
        pickCheck("windows 3-variant Full/cloud", win3, "windows", "x86_64",
                  true, false, "notepatra-full-0.1.116.msi");
        pickCheck("windows 3-variant Lite/local-ai", win3, "windows", "x86_64",
                  false, true, "notepatra-local-ai-0.1.116.msi");

        // THIS binary's own compiled flavor must resolve to its matching
        // variant. (test_updater compiles with no flavor macros → Lite/cloud,
        // but assert against the macros so it stays correct if that changes.)
        const bool selfFull = NOTEPATRA_BUILD_IS_FULL != 0;
        const bool selfAI   = NOTEPATRA_BUILD_IS_LOCAL_AI != 0;
        const char *selfWant =
            selfFull ? (selfAI ? "notepatra-local-ai-full-0.1.116.msi"
                               : "notepatra-full-0.1.116.msi")
                     : (selfAI ? "notepatra-local-ai-0.1.116.msi"
                               : "notepatra-0.1.116.msi");
        pickCheck("windows / this binary's compiled flavor", win, "windows",
                  "x86_64", selfFull, selfAI, selfWant);

        // Degraded release: only the WRONG-edition installer is published.
        // A Full user must still get an installable last-resort asset
        // (found=true) rather than being sent to the release page.
        QJsonArray onlyLite = mkAssets({
            {"notepatra-0.1.116.msi", "https://example.test/notepatra-0.1.116.msi"},
            {"SHA256SUMS",            "https://example.test/SHA256SUMS"},
        });
        auto lastResort = Updater::pickAssetForPlatformEx(onlyLite, "windows",
                                                          "x86_64", true, false);
        check("windows wrong-edition-only → last-resort found=true",
              lastResort.found && lastResort.name == "notepatra-0.1.116.msi",
              QStringLiteral("got %1 (found=%2)").arg(lastResort.name).arg(lastResort.found));
    }

    {
        // macOS 2-DMG case: regular vs full share the same arch, differ only
        // by the "-full" suffix. Correct edition wins order-independently.
        QJsonArray mac = mkAssets({
            {"notepatra-macos-arm64.dmg",      "https://example.test/notepatra-macos-arm64.dmg"},
            {"notepatra-macos-arm64-full.dmg", "https://example.test/notepatra-macos-arm64-full.dmg"},
            {"SHA256SUMS",                     "https://example.test/SHA256SUMS"},
        });
        pickCheck("macos regular (full=0)", mac, "macos", "arm64",
                  false, false, "notepatra-macos-arm64.dmg");
        pickCheck("macos full (full=1)", mac, "macos", "arm64",
                  true, false, "notepatra-macos-arm64-full.dmg");
    }

    std::printf("\n=== Summary: %d passed, %d failed ===\n", g_pass, g_fail);
    return g_fail == 0 ? 0 : 1;
}
