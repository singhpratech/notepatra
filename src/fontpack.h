// SPDX-License-Identifier: GPL-3.0-or-later

// v0.1.74 — Runtime font-pack downloader.
//
// Bundles a curated catalogue of ~25 premium open-source fonts that
// users can install on-demand WITHOUT inflating the bare Notepatra
// binary. Mirrors the DuckDB-extension / plugin pattern: lite binary
// by default, heavy assets fetched at runtime to AppDataLocation.
//
// Flow:
//   1. App start  → NotepatraFontPack::loadInstalledFonts() scans
//      ~/.local/share/notepatra/fonts/ and registers each *.ttf /
//      *.otf with QFontDatabase via addApplicationFont().
//   2. User opens  Settings → Manage Fonts… → picks fonts → Install.
//   3. NotepatraFontPack::Installer downloads each entry from its
//      upstream HTTPS URL, writes to fontsDir(), and registers
//      with QFontDatabase so the font is available without restart.
//   4. The existing notepatraDefaultCodeFamily() chain in src/fonts.h
//      auto-picks installed families — no additional wiring needed.
//
// Licensing: every entry in manifest() is shipped under SIL OFL 1.1,
// Apache 2.0, or MIT — all permissive, all redistributable. We do NOT
// host the bytes; we link to each project's upstream raw GitHub URL
// (tag-pinned where upstream tags releases; google/fonts entries track
// `main`). Display the license in the install dialog.
//
// Cloud-free build (NOTEPATRA_NO_CLOUD): downloads go to github.com /
// raw.githubusercontent.com which are NOT private-network hosts — the
// installer gate refuses them by default. Users in air-gapped /
// regulated environments can manually drop .ttf files into fontsDir()
// and they'll still be picked up by loadInstalledFonts() at next start.

#ifndef NOTEPATRA_FONTPACK_H
#define NOTEPATRA_FONTPACK_H

#include <QObject>
#include <QString>
#include <QList>

class QNetworkAccessManager;
class QNetworkReply;

namespace NotepatraFontPack {

enum class Category {
    CodeMono,   // monospace, for the editor itself
    UiSans,     // sans-serif, for the UI / chat bubbles
    Serif,      // serif, for prose / notes
    Display     // novelty / fun (Comic Mono, Victor Mono italic, …)
};

struct Entry {
    QString family;        // canonical Qt family ("JetBrains Mono")
    QString variant;       // "Regular" / "Bold" / "Italic" — display only
    QString fileName;      // local file basename (must be unique in manifest)
    QString url;           // pinned HTTPS upstream
    qint64  approxSize;    // bytes — for dialog progress + size display
    QString license;       // "OFL 1.1" / "Apache 2.0"
    QString origin;        // "JetBrains" / "Mozilla" / "Microsoft" / …
    Category category;
    QString expectedSha256; // OPTIONAL — lowercase hex; when set the installer
                           // verifies the download and rejects mismatches. Empty
                           // means SHA-pinning hasn't been recorded yet (legacy
                           // entries; entries will be SHA-pinned incrementally).
};

// Curated catalogue. ~25 premium fonts used industry-wide.
QList<Entry> manifest();

// ~/.local/share/notepatra/fonts (cross-platform via QStandardPaths).
// Creates the directory if it doesn't exist.
QString fontsDir();

// Absolute path where Entry.fileName lives (whether or not it's present).
QString localPath(const Entry &e);

// True iff the on-disk file exists. Does not check QFontDatabase state.
bool isInstalled(const Entry &e);

// At-startup scan: registers every *.ttf / *.otf in fontsDir() with
// QFontDatabase::addApplicationFont(). Returns the count registered.
// Safe to call multiple times — Qt deduplicates internally.
int loadInstalledFonts();

// Removes the local file. Returns true if removed or never existed.
// Note: a currently-running Qt process keeps the family registered until
// QFontDatabase::removeApplicationFont() is called or the app restarts.
bool uninstall(const Entry &e);

// Async multi-entry installer. Construct, connect signals, call
// install(). Emits per-font progress + finishedAll() when the queue
// drains. Cancellation supported via cancel().
class Installer : public QObject {
    Q_OBJECT
public:
    explicit Installer(QObject *parent = nullptr);
    ~Installer() override;

    void install(const QList<Entry> &entries);
    void cancel();

    bool isRunning() const { return m_currentReply != nullptr; }

signals:
    void progressOne(const NotepatraFontPack::Entry &e,
                     qint64 received, qint64 total);
    void finishedOne(const NotepatraFontPack::Entry &e,
                     bool ok, const QString &error);
    void finishedAll();

private slots:
    void onReadyRead();
    void onProgress(qint64 received, qint64 total);
    void onFinished();

private:
    void startNext();
    QNetworkAccessManager *m_nam;
    QList<Entry>          m_queue;
    QNetworkReply        *m_currentReply = nullptr;
    Entry                 m_currentEntry;
    QByteArray            m_buffer;
    bool                  m_cancelled = false;
};

} // namespace NotepatraFontPack

#endif // NOTEPATRA_FONTPACK_H
