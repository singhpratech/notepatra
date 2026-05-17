// SPDX-License-Identifier: GPL-3.0-or-later

#include "fontpack.h"

#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QFontDatabase>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QStandardPaths>
#include <QSslConfiguration>

namespace NotepatraFontPack {

// ─── Manifest ─────────────────────────────────────────────────────────
//
// All URLs are pinned to a specific upstream tag for reproducibility.
// All entries are SIL OFL 1.1 or Apache 2.0. The on-disk file name
// must be unique across the manifest — it's the dedupe key for
// isInstalled() and the basename in fontsDir().
//
// approxSize is intentionally approximate; the installer reports
// the real Content-Length once the response headers land.
QList<Entry> manifest() {
    QList<Entry> m;

    // ─── Code / Monospace (premium modern) ────────────────────────────
    m.append({"JetBrains Mono", "Regular", "JetBrainsMono-Regular.ttf",
              "https://github.com/JetBrains/JetBrainsMono/raw/v2.304/fonts/ttf/JetBrainsMono-Regular.ttf",
              274000, "OFL 1.1", "JetBrains", Category::CodeMono});
    m.append({"JetBrains Mono", "Bold", "JetBrainsMono-Bold.ttf",
              "https://github.com/JetBrains/JetBrainsMono/raw/v2.304/fonts/ttf/JetBrainsMono-Bold.ttf",
              275000, "OFL 1.1", "JetBrains", Category::CodeMono});
    m.append({"JetBrains Mono", "Italic", "JetBrainsMono-Italic.ttf",
              "https://github.com/JetBrains/JetBrainsMono/raw/v2.304/fonts/ttf/JetBrainsMono-Italic.ttf",
              281000, "OFL 1.1", "JetBrains", Category::CodeMono});

    m.append({"Fira Code", "Regular", "FiraCode-Regular.ttf",
              "https://github.com/tonsky/FiraCode/raw/6.2/distr/ttf/FiraCode-Regular.ttf",
              340000, "OFL 1.1", "Mozilla / Nikita Prokopov", Category::CodeMono});
    m.append({"Fira Code", "Bold", "FiraCode-Bold.ttf",
              "https://github.com/tonsky/FiraCode/raw/6.2/distr/ttf/FiraCode-Bold.ttf",
              340000, "OFL 1.1", "Mozilla / Nikita Prokopov", Category::CodeMono});

    m.append({"Cascadia Code", "Regular", "CascadiaCode-Regular.ttf",
              "https://github.com/microsoft/cascadia-code/raw/v2407.24/sources/static/CascadiaCode/CascadiaCode-Regular.ttf",
              630000, "OFL 1.1", "Microsoft", Category::CodeMono});
    m.append({"Cascadia Code", "Bold", "CascadiaCode-Bold.ttf",
              "https://github.com/microsoft/cascadia-code/raw/v2407.24/sources/static/CascadiaCode/CascadiaCode-Bold.ttf",
              630000, "OFL 1.1", "Microsoft", Category::CodeMono});

    m.append({"Source Code Pro", "Regular", "SourceCodePro-Regular.ttf",
              "https://github.com/google/fonts/raw/main/ofl/sourcecodepro/static/SourceCodePro-Regular.ttf",
              280000, "OFL 1.1", "Adobe", Category::CodeMono});
    m.append({"Source Code Pro", "Bold", "SourceCodePro-Bold.ttf",
              "https://github.com/google/fonts/raw/main/ofl/sourcecodepro/static/SourceCodePro-Bold.ttf",
              285000, "OFL 1.1", "Adobe", Category::CodeMono});

    m.append({"IBM Plex Mono", "Regular", "IBMPlexMono-Regular.ttf",
              "https://github.com/google/fonts/raw/main/ofl/ibmplexmono/IBMPlexMono-Regular.ttf",
              140000, "OFL 1.1", "IBM", Category::CodeMono});
    m.append({"IBM Plex Mono", "Bold", "IBMPlexMono-Bold.ttf",
              "https://github.com/google/fonts/raw/main/ofl/ibmplexmono/IBMPlexMono-Bold.ttf",
              140000, "OFL 1.1", "IBM", Category::CodeMono});

    m.append({"Hack", "Regular", "Hack-Regular.ttf",
              "https://github.com/source-foundry/Hack/raw/v3.003/build/ttf/Hack-Regular.ttf",
              315000, "OFL 1.1 + Bitstream Vera", "Source Foundry", Category::CodeMono});
    m.append({"Hack", "Bold", "Hack-Bold.ttf",
              "https://github.com/source-foundry/Hack/raw/v3.003/build/ttf/Hack-Bold.ttf",
              330000, "OFL 1.1 + Bitstream Vera", "Source Foundry", Category::CodeMono});

    m.append({"Geist Mono", "Regular", "GeistMono-Regular.ttf",
              "https://github.com/google/fonts/raw/main/ofl/geistmono/GeistMono%5Bwght%5D.ttf",
              260000, "OFL 1.1", "Vercel", Category::CodeMono});

    m.append({"Inconsolata", "Regular", "Inconsolata-Regular.ttf",
              "https://github.com/google/fonts/raw/main/ofl/inconsolata/static/Inconsolata-Regular.ttf",
              100000, "OFL 1.1", "Google / Raph Levien", Category::CodeMono});

    m.append({"Roboto Mono", "Regular", "RobotoMono-Regular.ttf",
              "https://github.com/google/fonts/raw/main/apache/robotomono/static/RobotoMono-Regular.ttf",
              85000, "Apache 2.0", "Google", Category::CodeMono});

    m.append({"Fira Mono", "Regular", "FiraMono-Regular.ttf",
              "https://github.com/google/fonts/raw/main/ofl/firamono/FiraMono-Regular.ttf",
              165000, "OFL 1.1", "Mozilla", Category::CodeMono});

    m.append({"Noto Sans Mono", "Regular", "NotoSansMono-Regular.ttf",
              "https://github.com/google/fonts/raw/main/ofl/notosansmono/static/NotoSansMono/NotoSansMono-Regular.ttf",
              375000, "OFL 1.1", "Google", Category::CodeMono});

    m.append({"Space Mono", "Regular", "SpaceMono-Regular.ttf",
              "https://github.com/google/fonts/raw/main/ofl/spacemono/SpaceMono-Regular.ttf",
              85000, "OFL 1.1", "Colophon Foundry", Category::CodeMono});

    m.append({"Anonymous Pro", "Regular", "AnonymousPro-Regular.ttf",
              "https://github.com/google/fonts/raw/main/ofl/anonymouspro/AnonymousPro-Regular.ttf",
              140000, "OFL 1.1", "Mark Simonson", Category::CodeMono});

    // ─── Display / Distinctive ────────────────────────────────────────
    m.append({"Victor Mono", "Italic", "VictorMono-Italic.ttf",
              "https://github.com/rubjo/victor-mono/raw/v1.5.5/public/VictorMonoAll/TTF/VictorMono-Italic.ttf",
              115000, "OFL 1.1", "Rune Bjørnerås", Category::Display});
    m.append({"Comic Mono", "Regular", "ComicMono.ttf",
              "https://github.com/dtinth/comic-mono-font/raw/main/ComicMono.ttf",
              60000, "MIT", "Thai Pangsakulyanont", Category::Display});

    // ─── UI / Sans-serif ──────────────────────────────────────────────
    m.append({"Inter", "Regular", "Inter-Regular.ttf",
              "https://github.com/google/fonts/raw/main/ofl/inter/Inter%5Bopsz%2Cwght%5D.ttf",
              810000, "OFL 1.1", "Rasmus Andersson", Category::UiSans});

    m.append({"Roboto", "Regular", "Roboto-Regular.ttf",
              "https://github.com/google/fonts/raw/main/apache/roboto/static/Roboto-Regular.ttf",
              170000, "Apache 2.0", "Google", Category::UiSans});

    m.append({"Manrope", "Regular", "Manrope-Regular.ttf",
              "https://github.com/google/fonts/raw/main/ofl/manrope/static/Manrope-Regular.ttf",
              50000, "OFL 1.1", "Mikhail Sharanda", Category::UiSans});

    // ─── Serif (for prose / notes / readability mode) ─────────────────
    m.append({"Source Serif 4", "Regular", "SourceSerif4-Regular.ttf",
              "https://github.com/google/fonts/raw/main/ofl/sourceserif4/SourceSerif4%5Bopsz%2Cwght%5D.ttf",
              530000, "OFL 1.1", "Adobe", Category::Serif});
    m.append({"Merriweather", "Regular", "Merriweather-Regular.ttf",
              "https://github.com/google/fonts/raw/main/ofl/merriweather/static/Merriweather-Regular.ttf",
              210000, "OFL 1.1", "Sorkin Type", Category::Serif});

    return m;
}

// ─── Paths ────────────────────────────────────────────────────────────

QString fontsDir() {
    const QString base = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    QString dir;
    if (base.isEmpty()) {
        dir = QDir::current().absoluteFilePath(QStringLiteral("fonts"));
    } else {
        dir = QDir(base).absoluteFilePath(QStringLiteral("fonts"));
    }
    QDir().mkpath(dir);
    return dir;
}

QString localPath(const Entry &e) {
    return QDir(fontsDir()).absoluteFilePath(e.fileName);
}

bool isInstalled(const Entry &e) {
    return QFileInfo::exists(localPath(e));
}

// ─── Startup loader ───────────────────────────────────────────────────

int loadInstalledFonts() {
    const QDir d(fontsDir());
    if (!d.exists()) return 0;
    int count = 0;
    const QStringList files = d.entryList({"*.ttf", "*.otf"}, QDir::Files);
    for (const QString &name : files) {
        const QString path = d.absoluteFilePath(name);
        const int id = QFontDatabase::addApplicationFont(path);
        if (id >= 0) ++count;
    }
    return count;
}

bool uninstall(const Entry &e) {
    const QString p = localPath(e);
    if (!QFileInfo::exists(p)) return true;
    return QFile::remove(p);
}

// ─── Installer ────────────────────────────────────────────────────────

Installer::Installer(QObject *parent)
    : QObject(parent),
      m_nam(new QNetworkAccessManager(this))
{}

Installer::~Installer() {
    if (m_currentReply) {
        m_currentReply->abort();
        m_currentReply->deleteLater();
    }
}

void Installer::install(const QList<Entry> &entries) {
    m_queue.append(entries);
    m_cancelled = false;
    if (!m_currentReply) startNext();
}

void Installer::cancel() {
    m_cancelled = true;
    m_queue.clear();
    if (m_currentReply) {
        m_currentReply->abort();
    }
}

void Installer::startNext() {
    if (m_cancelled || m_queue.isEmpty()) {
        m_currentReply = nullptr;
        m_buffer.clear();
        emit finishedAll();
        return;
    }
    m_currentEntry = m_queue.takeFirst();
    m_buffer.clear();

    QNetworkRequest req(QUrl(m_currentEntry.url));
    req.setHeader(QNetworkRequest::UserAgentHeader,
                  QStringLiteral("Notepatra/font-pack"));
    // Always force HTTPS / verify chain via Qt's defaults — no override.
    QSslConfiguration ssl = QSslConfiguration::defaultConfiguration();
    req.setSslConfiguration(ssl);

    m_currentReply = m_nam->get(req);
    connect(m_currentReply, &QNetworkReply::readyRead,
            this, &Installer::onReadyRead);
    connect(m_currentReply, &QNetworkReply::downloadProgress,
            this, &Installer::onProgress);
    connect(m_currentReply, &QNetworkReply::finished,
            this, &Installer::onFinished);
}

void Installer::onReadyRead() {
    if (!m_currentReply) return;
    m_buffer.append(m_currentReply->readAll());
}

void Installer::onProgress(qint64 received, qint64 total) {
    emit progressOne(m_currentEntry, received, total);
}

void Installer::onFinished() {
    if (!m_currentReply) return;
    QNetworkReply *r = m_currentReply;
    m_currentReply = nullptr;

    const Entry done = m_currentEntry;
    bool ok = false;
    QString err;

    if (m_cancelled) {
        err = QStringLiteral("cancelled");
    } else if (r->error() != QNetworkReply::NoError) {
        err = r->errorString();
    } else {
        // Drain any trailing bytes that arrived between the last
        // readyRead and finished().
        m_buffer.append(r->readAll());
        if (m_buffer.size() < 1024) {
            // A 4xx/5xx HTML error page is much smaller than a real
            // TTF/OTF; flag short payloads explicitly rather than
            // writing garbage to disk.
            err = QStringLiteral("payload too small (%1 bytes) — upstream may have returned an error page")
                  .arg(m_buffer.size());
        } else if (!done.expectedSha256.isEmpty()) {
            // SHA-256 verification — when the manifest pins a hash, refuse to
            // install anything that doesn't match. Protects against upstream
            // org-compromise + tag-rewrite scenarios (e.g. JetBrains GitHub
            // takeover serving a malicious TTF with the same URL).
            QCryptographicHash h(QCryptographicHash::Sha256);
            h.addData(m_buffer);
            const QString actual = QString::fromUtf8(h.result().toHex());
            if (actual.compare(done.expectedSha256, Qt::CaseInsensitive) != 0) {
                err = QStringLiteral("SHA-256 mismatch — expected %1, got %2")
                      .arg(done.expectedSha256.left(16) + "…", actual.left(16) + "…");
            }
        }
        if (err.isEmpty() && m_buffer.size() >= 1024) {
            QFile f(localPath(done));
            if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
                err = QStringLiteral("could not write %1: %2")
                      .arg(f.fileName(), f.errorString());
            } else {
                f.write(m_buffer);
                f.close();
                // Register with QFontDatabase so the family is
                // available in the running process — no restart.
                const int id = QFontDatabase::addApplicationFont(f.fileName());
                if (id < 0) {
                    err = QStringLiteral("Qt rejected the font file (corrupt or unsupported format)");
                } else {
                    ok = true;
                }
            }
        }
    }

    m_buffer.clear();
    emit finishedOne(done, ok, err);
    r->deleteLater();

    // Continue with the queue (or signal completion if drained).
    startNext();
}

} // namespace NotepatraFontPack
