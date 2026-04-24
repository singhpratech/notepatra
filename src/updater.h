#ifndef NOTEPATRA_UPDATER_H
#define NOTEPATRA_UPDATER_H

#include <QObject>
#include <QString>
#include <QStringList>
#include <QJsonArray>

class QWidget;

/**
 * Safe, Notepad++/WinGup-style updater.
 *
 * Given the JSON payload from GitHub's `releases/latest` endpoint,
 * Updater::installReleaseInteractive():
 *   1. Picks the correct asset for the current platform (Linux x64 /
 *      arm64 AppImage, macOS DMG, Windows MSI > NSIS > zip).
 *   2. Stream-downloads the asset to ~/Downloads/<name>.part with a
 *      cancellable QProgressDialog.
 *   3. Also downloads the release's SHA256SUMS file.
 *   4. Computes SHA-256 of the .part file, compares to the expected
 *      hash. On mismatch: deletes the .part, shows error, done.
 *   5. Atomic rename .part → final name (POSIX atomic on same FS).
 *   6. Hands off to the OS-native installer:
 *        Windows: msiexec /i <file>   (UI visible — MajorUpgrade rolls back)
 *        macOS:   open <dmg>          (Finder → drag to /Applications)
 *        Linux:   open containing folder — user does the swap manually.
 *
 * SAFETY INVARIANTS — upheld at every step:
 *   • The currently-running Notepatra binary is NEVER modified by this
 *     class. No rename, no overwrite, no chmod, nothing. The OS
 *     installer may replace it AFTER the user confirms — we do not.
 *   • Nothing on disk changes until the download completes AND
 *     sha-256 verifies. If any step fails (cancel, network, disk,
 *     bad hash) we delete the .part file and exit cleanly.
 *   • The OS installers we invoke (MSI MajorUpgrade / Finder drag /
 *     user-driven file-manager swap on Linux) all have their own
 *     rollback on failure.
 */
namespace Updater {

struct PickedAsset {
    QString name;          // e.g. "Notepatra-0.1.17-linux-x86_64.AppImage"
    QString downloadUrl;   // direct browser_download_url
    qint64  sizeBytes = 0;
    bool    found = false;
};

// Pure logic, unit-testable: from the GitHub releases/latest `assets` array,
// pick the best artifact for this OS + architecture. Returns found=false if
// no suitable asset exists (e.g. Linux ARM64 user on a release that
// shipped only x86_64 — we won't try to install a mismatched binary).
PickedAsset pickAssetForPlatform(const QJsonArray &assets);

// Parse a SHA256SUMS file body (`<hex>  <filename>` per line) into a map
// {filename → hex hash}. Tolerates trailing whitespace, blank lines,
// comment lines starting with `#`. Pure function, unit-testable.
QString parseSha256For(const QString &sha256SumsBody, const QString &filename);

/**
 * Top-level entry point. Blocks (modally) on a progress dialog the
 * entire time; returns when the user confirms install / cancels / the
 * flow errors out. `parent` owns any dialogs spawned. `assets` is the
 * `assets` array from the GitHub release JSON, `tagName` is the release
 * tag (for display), `releaseHtmlUrl` is used as a "visit release page"
 * fallback if no matching asset is found.
 *
 * Returns true only if the download + verification succeeded AND the
 * installer handoff started. A return of false leaves everything
 * exactly as it was before the call.
 */
bool installReleaseInteractive(QWidget *parent,
                               const QJsonArray &assets,
                               const QString &tagName,
                               const QString &releaseHtmlUrl);

} // namespace Updater

#endif // NOTEPATRA_UPDATER_H
