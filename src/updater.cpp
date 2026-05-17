// SPDX-License-Identifier: GPL-3.0-or-later

#include "updater.h"

#include <QApplication>
#include <QCryptographicHash>
#include <QDesktopServices>
#include <QDir>
#include <QEventLoop>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonObject>
#include <QJsonValue>
#include <QMessageBox>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QProgressDialog>
#include <QProcess>
#include <QPushButton>
#include <QStandardPaths>
#include <QSysInfo>
#include <QTimer>
#include <QUrl>

namespace Updater {

// ═══════════════════════════════════════════════════════════════════════
// Pure logic — unit-testable, no I/O, no widgets.
// ═══════════════════════════════════════════════════════════════════════

namespace {

// Does `name` match any of `patterns` (plain substring, case-insensitive)?
bool matchesAny(const QString &name, std::initializer_list<const char *> patterns) {
    const QString n = name.toLower();
    for (const char *p : patterns) {
        if (n.contains(QString::fromLatin1(p).toLower())) return true;
    }
    return false;
}

struct Candidate { int priority; QString name, url; qint64 size; };

Candidate scoreAsset(const QString &name, const QString &url, qint64 size,
                     const QString &platform, const QString &arch) {
    // Lower priority number = better match. 0 = perfect, >=100 = unsuitable.
    // Also reject signature / checksum / provenance files — we download
    // SHA256SUMS separately; we never try to "install" a .sig or .pem.
    const QString nLower = name.toLower();
    if (nLower.endsWith(".sig") || nLower.endsWith(".pem") ||
        nLower.endsWith(".cert") || nLower.endsWith(".sbom.json") ||
        nLower == "sha256sums" || nLower.contains("attestation"))
        return { 999, name, url, size };

    if (platform == "windows") {
        // Prefer MSI (transactional) > NSIS setup > portable zip
        if (matchesAny(name, {".msi"}))            return { 10, name, url, size };
        if (matchesAny(name, {"setup", ".exe"}))   return { 20, name, url, size };
        if (matchesAny(name, {"windows", ".zip"})) return { 30, name, url, size };
        return { 900, name, url, size };
    }
    if (platform == "macos") {
        if (matchesAny(name, {".dmg"})) {
            // Prefer arch-matched DMG if the release separates them.
            if (arch == "arm64" && matchesAny(name, {"arm64", "aarch64", "apple"}))
                return { 10, name, url, size };
            if (arch == "x86_64" && matchesAny(name, {"x86_64", "x64", "intel"}))
                return { 10, name, url, size };
            return { 20, name, url, size };
        }
        return { 900, name, url, size };
    }
    // linux
    if (matchesAny(name, {".appimage"})) {
        if (arch == "aarch64" && matchesAny(name, {"aarch64", "arm64"}))
            return { 10, name, url, size };
        if (arch == "x86_64" && matchesAny(name, {"x86_64", "x64", "amd64"}))
            return { 10, name, url, size };
        // AppImage with no arch in name — probably x86_64 by convention
        if (arch == "x86_64") return { 20, name, url, size };
        return { 900, name, url, size };
    }
    if (matchesAny(name, {".tar.gz", ".tar.xz", ".tgz"})) {
        if (arch == "aarch64" && matchesAny(name, {"aarch64", "arm64"}))
            return { 30, name, url, size };
        if (arch == "x86_64" && matchesAny(name, {"x86_64", "x64", "amd64"}))
            return { 30, name, url, size };
        return { 900, name, url, size };
    }
    return { 900, name, url, size };
}

// Normalise QSysInfo names to our three buckets.
QString detectPlatform() {
#if defined(Q_OS_WIN)
    return "windows";
#elif defined(Q_OS_MAC)
    return "macos";
#else
    return "linux";
#endif
}

QString detectArch() {
    const QString cpu = QSysInfo::currentCpuArchitecture();
    if (cpu.contains("arm64", Qt::CaseInsensitive) ||
        cpu.contains("aarch64", Qt::CaseInsensitive))
        return "aarch64";
    return "x86_64";
}

} // anon namespace

PickedAsset pickAssetForPlatform(const QJsonArray &assets) {
    const QString plat = detectPlatform();
    const QString arch = detectArch();

    int best = 999;
    PickedAsset picked;
    for (const QJsonValue &v : assets) {
        if (!v.isObject()) continue;
        const QJsonObject o = v.toObject();
        const QString name = o.value("name").toString();
        const QString url  = o.value("browser_download_url").toString();
        const qint64 size  = static_cast<qint64>(o.value("size").toDouble());
        if (name.isEmpty() || url.isEmpty()) continue;
        Candidate c = scoreAsset(name, url, size, plat, arch);
        if (c.priority < best) {
            best = c.priority;
            picked.name = c.name;
            picked.downloadUrl = c.url;
            picked.sizeBytes = c.size;
            picked.found = (c.priority < 100);
        }
    }
    return picked;
}

QString parseSha256For(const QString &sha256SumsBody, const QString &filename) {
    // Format per GNU `sha256sum`:  "<64 hex chars>  <filename>"
    // Tolerate Windows CRLFs, blank lines, comment lines, leading `*`
    // on the filename (binary-mode marker).
    const QStringList lines = sha256SumsBody.split(QRegExp("[\r\n]+"),
                                                   Qt::SkipEmptyParts);
    for (const QString &raw : lines) {
        const QString line = raw.trimmed();
        if (line.isEmpty() || line.startsWith('#')) continue;
        // Split on first run of whitespace
        const int sp = line.indexOf(QRegExp("\\s+"));
        if (sp < 0) continue;
        const QString hash = line.left(sp).trimmed().toLower();
        QString file = line.mid(sp).trimmed();
        if (file.startsWith('*')) file.remove(0, 1);
        if (hash.size() != 64) continue;
        // Accept either bare basename or any path ending in basename
        if (file == filename ||
            file.endsWith(QStringLiteral("/") + filename) ||
            file.endsWith(QStringLiteral("\\") + filename))
            return hash;
    }
    return QString();
}

// ═══════════════════════════════════════════════════════════════════════
// Interactive download + install — safety-critical code.
// ═══════════════════════════════════════════════════════════════════════

namespace {

// Best-effort cleanup: delete the .part file if it exists. Never fails
// loudly — if the file is gone we don't care.
void cleanupPart(const QString &partPath) {
    if (partPath.isEmpty()) return;
    QFile f(partPath);
    if (f.exists()) f.remove();
}

// Compute SHA-256 hex digest of a file streamed in 1 MB chunks so huge
// installer payloads don't balloon RAM. Returns empty on open error.
QString sha256OfFile(const QString &path) {
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) return QString();
    QCryptographicHash h(QCryptographicHash::Sha256);
    while (!f.atEnd()) {
        const QByteArray chunk = f.read(1 << 20);   // 1 MB
        if (chunk.isEmpty()) break;
        h.addData(chunk);
    }
    return QString::fromLatin1(h.result().toHex());
}

// Safe downloader used for both the artifact (with progress) and the
// SHA256SUMS file (quick, no progress). Writes to `outPath`. Returns
// the reply's error code. If cancelled via the progress dialog the
// partially-written file is deleted before returning.
QNetworkReply::NetworkError downloadTo(QNetworkAccessManager &nam,
                                       const QUrl &url, const QString &outPath,
                                       QProgressDialog *progress,
                                       qint64 expectedSize) {
    QNetworkRequest req(url);
    req.setRawHeader("User-Agent", "Notepatra-Updater");
    req.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                     QNetworkRequest::NoLessSafeRedirectPolicy);
    req.setAttribute(QNetworkRequest::AutoDeleteReplyOnFinishAttribute, false);

    QNetworkReply *reply = nam.get(req);

    QFile out(outPath);
    if (!out.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        reply->abort();
        reply->deleteLater();
        return QNetworkReply::ContentAccessDenied;
    }

    bool cancelled = false;
    QEventLoop loop;
    QObject::connect(reply, &QNetworkReply::readyRead, &loop, [reply, &out]() {
        out.write(reply->readAll());
    });
    QObject::connect(reply, &QNetworkReply::downloadProgress, &loop,
        [progress, expectedSize](qint64 received, qint64 total) {
            if (!progress) return;
            qint64 max = total > 0 ? total : expectedSize;
            if (max > 0) {
                progress->setMaximum(int(max / 1024));
                progress->setValue(int(received / 1024));
            }
            const double mbR = received / (1024.0 * 1024.0);
            const double mbT = max / (1024.0 * 1024.0);
            if (max > 0)
                progress->setLabelText(QObject::tr("Downloading — %1 / %2 MB")
                                           .arg(mbR, 0, 'f', 1).arg(mbT, 0, 'f', 1));
            else
                progress->setLabelText(QObject::tr("Downloading — %1 MB")
                                           .arg(mbR, 0, 'f', 1));
        });
    if (progress) {
        QObject::connect(progress, &QProgressDialog::canceled, reply,
            [reply, &cancelled]() {
                cancelled = true;
                reply->abort();
            });
    }
    QObject::connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
    loop.exec();

    // Flush any tail bytes the readyRead handler missed
    out.write(reply->readAll());
    out.close();

    QNetworkReply::NetworkError err = reply->error();
    reply->deleteLater();

    if (cancelled) {
        cleanupPart(outPath);
        return QNetworkReply::OperationCanceledError;
    }
    if (err != QNetworkReply::NoError) {
        cleanupPart(outPath);
    }
    return err;
}

// Launch the native installer / handoff for the verified file, or
// show the containing folder for Linux. Returns true on successful
// handoff (user may still click through further UI, but we've done
// our part). Never modifies the running binary.
bool handoffToInstaller(QWidget *parent, const QString &finalPath,
                        const QString &platform) {
    const QFileInfo fi(finalPath);
    if (platform == "windows") {
        // Launch the MSI / EXE with UI visible — the installer has its
        // own UAC prompt and rollback. We intentionally do NOT pass
        // /quiet, so the user sees every step and can cancel.
        const QString ext = fi.suffix().toLower();
        QString program;
        QStringList args;
        if (ext == "msi") {
            program = "msiexec";
            args << "/i" << QDir::toNativeSeparators(finalPath);
        } else {
            program = QDir::toNativeSeparators(finalPath);  // setup.exe / portable
        }
        const bool ok = QProcess::startDetached(program, args);
        if (!ok) {
            QMessageBox::warning(parent, QObject::tr("Launch Installer"),
                QObject::tr("Could not launch the installer:\n%1\n\nThe file has been downloaded and verified. You can run it manually.")
                    .arg(QDir::toNativeSeparators(finalPath)));
        }
        return ok;
    }
    if (platform == "macos") {
        // `open` mounts the DMG in Finder. User drags Notepatra.app to
        // /Applications — standard macOS replace-confirm dialog. No
        // way this can break the running app.
        const bool ok = QProcess::startDetached("open", { finalPath });
        if (!ok) {
            QMessageBox::warning(parent, QObject::tr("Open DMG"),
                QObject::tr("Could not open the disk image.\nIt has been downloaded to:\n%1")
                    .arg(finalPath));
        }
        return ok;
    }
    // Linux — the safest handoff: open the containing folder and let
    // the user drop the new AppImage into place. This NEVER modifies
    // the running binary; swapping is a conscious manual step the user
    // controls. Our contribution is picking the right file + verifying
    // its SHA-256, both of which users typically skip.
    QFile(finalPath).setPermissions(QFile(finalPath).permissions() |
                                    QFile::ExeOwner | QFile::ExeUser |
                                    QFile::ExeGroup | QFile::ExeOther);
    const QUrl folderUrl = QUrl::fromLocalFile(fi.absolutePath());
    QDesktopServices::openUrl(folderUrl);
    return true;
}

} // anon namespace

bool installReleaseInteractive(QWidget *parent,
                               const QJsonArray &assets,
                               const QString &tagName,
                               const QString &releaseHtmlUrl) {
    const QString platform = detectPlatform();

    // ─── Step 1: pick the asset ──────────────────────────────────
    PickedAsset picked = pickAssetForPlatform(assets);
    if (!picked.found) {
        QMessageBox::information(parent, QObject::tr("Update"),
            QObject::tr("No installer matching your platform was found in "
                        "this release. Opening the release page so you can "
                        "pick a download manually.\n\n"
                        "Your current installation is untouched."));
        QDesktopServices::openUrl(QUrl(releaseHtmlUrl));
        return false;
    }

    // ─── Step 2: find SHA256SUMS in the same release ─────────────
    QString sumsUrl;
    for (const QJsonValue &v : assets) {
        if (!v.isObject()) continue;
        const QString n = v.toObject().value("name").toString();
        if (n.compare("SHA256SUMS", Qt::CaseInsensitive) == 0) {
            sumsUrl = v.toObject().value("browser_download_url").toString();
            break;
        }
    }
    if (sumsUrl.isEmpty()) {
        // No SHA256SUMS file published — refuse to auto-install. We
        // will not execute anything we can't verify.
        QMessageBox::warning(parent, QObject::tr("Update"),
            QObject::tr("This release does not publish a SHA256SUMS file, so "
                        "we cannot verify the download. Opening the release "
                        "page so you can download + verify manually.\n\n"
                        "Your current installation is untouched."));
        QDesktopServices::openUrl(QUrl(releaseHtmlUrl));
        return false;
    }

    // ─── Step 3: prepare the download directory ──────────────────
    const QString dlDir = QStandardPaths::writableLocation(
        QStandardPaths::DownloadLocation);
    if (dlDir.isEmpty() || !QDir().mkpath(dlDir)) {
        QMessageBox::warning(parent, QObject::tr("Update"),
            QObject::tr("Could not locate a writable Downloads folder. Aborting.\n\n"
                        "Your current installation is untouched."));
        return false;
    }
    const QString partPath = dlDir + "/" + picked.name + ".part";
    const QString finalPath = dlDir + "/" + picked.name;
    const QString sumsPath = dlDir + "/SHA256SUMS." + tagName;

    // Clean any leftover .part from a previous aborted run
    cleanupPart(partPath);

    // ─── Step 4: progress dialog + download ──────────────────────
    QProgressDialog progress(parent);
    progress.setWindowTitle(QObject::tr("Downloading Notepatra %1").arg(tagName));
    progress.setLabelText(QObject::tr("Starting download…"));
    progress.setMinimum(0);
    progress.setMaximum(0);       // indeterminate until first progress tick
    progress.setCancelButtonText(QObject::tr("Cancel"));
    progress.setAutoClose(false);
    progress.setAutoReset(false);
    progress.setMinimumWidth(460);
    progress.setWindowModality(Qt::WindowModal);
    progress.show();

    QNetworkAccessManager nam;

    const auto partErr = downloadTo(nam, QUrl(picked.downloadUrl), partPath,
                                    &progress, picked.sizeBytes);
    if (partErr == QNetworkReply::OperationCanceledError) {
        // User cancelled — already cleaned up
        progress.cancel();
        return false;
    }
    if (partErr != QNetworkReply::NoError) {
        progress.cancel();
        cleanupPart(partPath);
        QMessageBox::warning(parent, QObject::tr("Download Failed"),
            QObject::tr("The download did not complete. Your current "
                        "installation is untouched.\n\nYou can try again "
                        "later."));
        return false;
    }

    // Fetch SHA256SUMS separately — fast, no progress UI
    progress.setLabelText(QObject::tr("Verifying checksum…"));
    QApplication::processEvents();
    const auto sumsErr = downloadTo(nam, QUrl(sumsUrl), sumsPath, nullptr, 0);
    if (sumsErr != QNetworkReply::NoError) {
        progress.cancel();
        cleanupPart(partPath);
        QFile(sumsPath).remove();
        QMessageBox::warning(parent, QObject::tr("Checksum Unreachable"),
            QObject::tr("Could not download the SHA256SUMS file. Refusing "
                        "to install an unverified download.\n\nYour current "
                        "installation is untouched."));
        return false;
    }

    // ─── Step 5: verify SHA-256 ──────────────────────────────────
    QFile sf(sumsPath);
    QString expected;
    if (sf.open(QIODevice::ReadOnly)) {
        expected = parseSha256For(QString::fromUtf8(sf.readAll()), picked.name);
        sf.close();
    }
    QFile(sumsPath).remove();   // done with it
    if (expected.isEmpty()) {
        progress.cancel();
        cleanupPart(partPath);
        QMessageBox::warning(parent, QObject::tr("Checksum Missing"),
            QObject::tr("The SHA256SUMS file does not list %1. Refusing to "
                        "install an unverified download.\n\nYour current "
                        "installation is untouched.").arg(picked.name));
        return false;
    }
    const QString actual = sha256OfFile(partPath);
    if (actual.compare(expected, Qt::CaseInsensitive) != 0) {
        progress.cancel();
        cleanupPart(partPath);
        QMessageBox::critical(parent, QObject::tr("Checksum Mismatch"),
            QObject::tr("The download does not match the expected SHA-256. "
                        "It may have been corrupted in transit.\n\n"
                        "Expected: %1\nActual:&nbsp;&nbsp; %2\n\n"
                        "The download has been deleted. Your current "
                        "installation is untouched.")
                .arg(expected, actual));
        return false;
    }

    // ─── Step 5.5: optional cosign signature verification ────────
    // If cosign is installed locally, additionally verify the Sigstore
    // signature for this artifact. SHA-256 proves the file matches what
    // GitHub Releases lists — but if an attacker compromised the release
    // (rogue token, GH support takeover) they can recompute SHA256SUMS to
    // match the malicious binary. cosign verifies that the artifact was
    // signed by the keyless OIDC token of this repo's release workflow at
    // this tag, which a compromised release token cannot forge without
    // also compromising Sigstore + the Rekor transparency log.
    //
    // Behaviour:
    //  - cosign not on PATH → silently skip (most users don't have cosign)
    //  - cosign present but .sig/.pem assets missing from release → warn-skip
    //    (old releases pre-v0.1.60 didn't ship sigs)
    //  - cosign present AND sig/pem present → HARD-FAIL on mismatch
    const QString cosignPath = QStandardPaths::findExecutable("cosign");
    if (!cosignPath.isEmpty()) {
        QString sigUrl, pemUrl;
        for (const QJsonValue &v : assets) {
            if (!v.isObject()) continue;
            const QString n = v.toObject().value("name").toString();
            if (n == picked.name + ".sig") {
                sigUrl = v.toObject().value("browser_download_url").toString();
            } else if (n == picked.name + ".pem") {
                pemUrl = v.toObject().value("browser_download_url").toString();
            }
        }
        if (!sigUrl.isEmpty() && !pemUrl.isEmpty()) {
            progress.setLabelText(QObject::tr("Verifying signature…"));
            QApplication::processEvents();
            const QString sigPath = dlDir + "/" + picked.name + ".sig";
            const QString pemPath = dlDir + "/" + picked.name + ".pem";
            const auto sigErr = downloadTo(nam, QUrl(sigUrl), sigPath, nullptr, 0);
            const auto pemErr = downloadTo(nam, QUrl(pemUrl), pemPath, nullptr, 0);
            if (sigErr == QNetworkReply::NoError && pemErr == QNetworkReply::NoError) {
                QProcess proc;
                const QString identityRegex = QString(
                    "^https://github.com/singhpratech/notepatra/"
                    "\\.github/workflows/.+@refs/tags/%1$").arg(tagName);
                const QStringList args = {
                    QStringLiteral("verify-blob"),
                    QStringLiteral("--certificate"), pemPath,
                    QStringLiteral("--signature"), sigPath,
                    QStringLiteral("--certificate-identity-regexp"), identityRegex,
                    QStringLiteral("--certificate-oidc-issuer"),
                    QStringLiteral("https://token.actions.githubusercontent.com"),
                    partPath
                };
                proc.start(cosignPath, args);
                const bool finished = proc.waitForFinished(60000);  // 60s timeout
                const int rc = finished ? proc.exitCode() : -1;
                const QString stderrText = QString::fromUtf8(proc.readAllStandardError());
                QFile(sigPath).remove();
                QFile(pemPath).remove();
                if (rc != 0) {
                    progress.cancel();
                    cleanupPart(partPath);
                    QMessageBox::critical(parent, QObject::tr("Signature Verification Failed"),
                        QObject::tr("<b>Cosign signature verification failed.</b><br><br>"
                                    "The downloaded file (<code>%1</code>) does not appear "
                                    "to be a legitimate signed build of Notepatra %2. "
                                    "Refusing to install.<br><br>"
                                    "The download has been deleted. Your current "
                                    "installation is untouched.<br><br>"
                                    "<small>cosign stderr: %3</small>")
                            .arg(picked.name, tagName, stderrText.toHtmlEscaped()));
                    return false;
                }
                // cosign verified — proceed to atomic rename
            } else {
                QFile(sigPath).remove();
                QFile(pemPath).remove();
                // Could not fetch sig/pem — SHA still verified, proceed
            }
        }
        // sigUrl or pemUrl missing from release → SHA-only path (silent)
    }

    // ─── Step 6: atomic rename .part → final name ────────────────
    // POSIX rename is atomic on the same filesystem. If we crash
    // between this point and handoffToInstaller, we leave a fully
    // verified file in ~/Downloads — not a half-baked anything.
    if (QFile::exists(finalPath)) QFile::remove(finalPath);
    if (!QFile::rename(partPath, finalPath)) {
        cleanupPart(partPath);
        QMessageBox::warning(parent, QObject::tr("Rename Failed"),
            QObject::tr("Could not finalize the download file. Your current "
                        "installation is untouched."));
        return false;
    }

    progress.setValue(progress.maximum());
    progress.close();

    // ─── Step 7: confirm + hand off to OS installer ──────────────
    QString msg;
    if (platform == "windows") {
        msg = QObject::tr(
            "<b>Download verified.</b><br><br>"
            "Notepatra %1 is ready to install.<br><br>"
            "<b>This running copy of Notepatra will close now</b> so the "
            "installer can replace it — otherwise Windows will refuse with "
            "&ldquo;app already running.&rdquo; Make sure you've saved any "
            "open files before continuing.<br><br>"
            "The installer launches immediately after; Windows will ask for "
            "permission — accept it to complete the install.")
              .arg(tagName);
    } else if (platform == "macos") {
        msg = QObject::tr(
            "<b>Download verified.</b><br><br>"
            "The Notepatra %1 disk image will open in Finder. Drag "
            "Notepatra.app into your Applications folder to replace the "
            "current version.<br><br>"
            "Your current installation is untouched until you drop the "
            "new one in.")
              .arg(tagName);
    } else {
        // Linux ships either an AppImage (drop-in replace) or a .tar.gz
        // (extract + replace the bare `notepatra` binary). Tailor the
        // instructions to what was actually downloaded — telling a user to
        // "move the new AppImage into place" when they downloaded a tarball
        // is the bug fixed by https://github.com/singhpratech/notepatra/issues/12.
        const QString lowerName = picked.name.toLower();
        const bool isAppImage = lowerName.endsWith(".appimage");
        const bool isTarball = lowerName.endsWith(".tar.gz")
                            || lowerName.endsWith(".tgz")
                            || lowerName.endsWith(".tar.xz");
        QString action;
        if (isAppImage) {
            action = QObject::tr(
                "move the new AppImage into place (e.g. replace your existing "
                "<code>~/.local/bin/notepatra</code> or wherever you keep it). "
                "Don't forget <code>chmod +x</code> if your file manager strips "
                "the executable bit.");
        } else if (isTarball) {
            action = QObject::tr(
                "extract the tarball and replace the existing "
                "<code>notepatra</code> binary "
                "(typically <code>~/.local/bin/notepatra</code> if you used "
                "the install script, or <code>/usr/local/bin/notepatra</code> "
                "for a system-wide install). Example: "
                "<code>tar xzf %1 &amp;&amp; mv notepatra ~/.local/bin/</code>.")
                .arg(picked.name);
        } else {
            action = QObject::tr(
                "open the downloaded file and replace your current Notepatra "
                "installation with its contents.");
        }
        msg = QObject::tr(
            "<b>Download verified.</b><br><br>"
            "Notepatra %1 has been saved to your Downloads folder:<br>"
            "<code>%2</code><br><br>"
            "We'll open the folder so you can %3 Your current installation "
            "is untouched.")
              .arg(tagName, QDir::toNativeSeparators(finalPath), action);
    }

    QMessageBox confirm(parent);
    confirm.setWindowTitle(QObject::tr("Ready to Install"));
    confirm.setIcon(QMessageBox::Information);
    confirm.setTextFormat(Qt::RichText);
    confirm.setText(msg);
    const QString goLabel = (platform == "windows")
        ? QObject::tr("Close Notepatra && Install")
        : QObject::tr("Continue");
    QPushButton *go = confirm.addButton(goLabel, QMessageBox::AcceptRole);
    confirm.addButton(QObject::tr("Keep file, do it later"),
                      QMessageBox::RejectRole);
    confirm.setDefaultButton(go);
    confirm.exec();

    if (confirm.clickedButton() != go) {
        // User said "later" — the verified file stays in Downloads. No
        // other state changed.
        return false;
    }

    const bool handedOff = handoffToInstaller(parent, finalPath, platform);

    // On Windows the MSI / NSIS installer cannot overwrite a file that's
    // still mapped by the running Notepatra.exe — it errors with "app
    // already running" and the user has to kill the app manually. Quit
    // now that the installer is spawned so it has a clean shot at the
    // binary. macOS drags Notepatra.app into /Applications (no handle on
    // the running bundle), and Linux just shows the folder, so no quit
    // is needed there.
    if (handedOff && platform == "windows") {
        // Give the detached installer a beat to latch on to its own
        // process before we pull our event loop out from under it.
        QTimer::singleShot(500, qApp, &QCoreApplication::quit);
    }

    return handedOff;
}

} // namespace Updater
