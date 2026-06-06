// SPDX-License-Identifier: GPL-3.0-or-later
//
// notes_storage — bulletproof HTML storage layer for the Notes feature.
//
// Ten failure modes defended against:
//   1.  Power loss mid-save              → write-temp + fsync + atomic
//                                          rename (+ directory fsync on
//                                          POSIX so the rename itself is
//                                          durable)
//   2.  Crash before save                → .draft sidecar, written by
//                                          NotesPanel ~1.5s after the first
//                                          unsaved keystroke; recovery is
//                                          offered on the next open
//   3.  XSS / hostile paste              → whitelist sanitizer (sanitizeBody)
//   4.  Malformed user-supplied HTML     → validate-before-write via
//                                          QXmlStreamReader on body region
//   5.  Disk corruption of newest file   → 5-deep .bak1..bak5 ring
//   6.  Filename-injection on Win/macOS  → safeFilename strips reserved chars
//   7.  Sync-tool partial writes         → list filter skips .tmp/.draft/.bak
//   8.  Backup rotation failure          → best-effort, never blocks save
//   9.  Size cap readiness (future cap)  → readNote takes errorOut so the
//                                          caller can surface size errors
//   10. External edit while open         → NotesPanel stamps mtime/size/hash
//                                          at load + save and reroutes a
//                                          stale save to a conflict copy
//                                          (see notes.cpp, A7)
//
// NOT defended against (deliberately): two *processes* editing the same
// note concurrently. A per-PID .lock protocol existed here as dead code
// (documented, zero callers) and was removed in A7 — multi-instance
// protection needs an explicit opt-in design, not a false promise.

#ifndef NOTES_STORAGE_H
#define NOTES_STORAGE_H

#include <QObject>
#include <QString>
#include <QStringList>
#include <QDateTime>
#include <QHash>

class NotesStorage : public QObject {
    Q_OBJECT
public:
    explicit NotesStorage(const QString &notesRoot, QObject *parent = nullptr);
    ~NotesStorage() override;

    // The full HTML document for a new empty meeting note. Delegates to
    // NotesTemplate::shellHtml — see the extern forward decl in the .cpp.
    QString newNoteHtml(const QString &title, const QDateTime &start,
                        const QStringList &attendees) const;

    // Read raw HTML for an existing note. Returns empty string on
    // failure; populates errorOut if non-null.
    QString readNote(const QString &absolutePath, QString *errorOut = nullptr) const;

    // ATOMIC write — must defend against power-loss mid-write.
    // See the implementation comment block for the full 6-step protocol.
    bool saveNote(const QString &absolutePath, const QString &fullHtml,
                  QString *errorOut = nullptr);

    // Draft sidecar — called by NotesPanel's ~1.5s draft timer (armed on
    // the first unsaved keystroke), so a hard crash loses at most ~2s of
    // typing instead of the full 5s autosave window. Writes
    // absolutePath + ".draft" non-atomically (small fast write). On note
    // open, NotesPanel checks: if a .draft newer than the .html exists,
    // it offers recovery. After successful saveNote() (and after a
    // declined recovery), the draft is removed.
    bool writeDraft(const QString &absolutePath, const QString &html);
    void clearDraft(const QString &absolutePath);

    // Sanitizer — strip dangerous HTML. See the full forbidden / allowed
    // list in the implementation header comment.
    static QString sanitizeBody(const QString &dirtyHtml);

    // v0.1.112 — search-plaintext of a note's full HTML: <head> (incl. the
    // template's <title> + styleBlock CSS) and any <style>/<script> regions
    // dropped, remaining tags replaced by spaces, the 6 common entities
    // decoded (&amp; LAST so &amp;lt; never double-decodes), whitespace
    // collapsed via QString::simplified(). Case PRESERVED — callers match
    // with Qt::CaseInsensitive. Pure function; QtCore only (test_notes_storage
    // links no Qt5::Gui — do NOT switch to QTextDocument::toPlainText()).
    // Known accepted limitations: numeric entities other than &#39; stay
    // encoded (those exact characters won't match a search); HTML comments
    // containing '>' clip early under the <[^>]*> pass — harmless residue
    // for a contains-style search.
    static QString plainTextForSearch(const QString &fullHtml);

    // Filename safety — strip control + reserved chars, ASCII-fold,
    // collapse whitespace, lowercase, cap at 200 chars, "untitled"
    // fallback if everything sanitizes away to empty.
    static QString safeFilename(const QString &raw);

    // ── Title identity (single source of truth) ──────────────────────
    // The display title of a note lives in a dedicated head meta:
    //   <meta name="notepatra-title" content="...">
    // The tag name never existed before this feature, so legacy files
    // cannot carry stale values. The resolver below is READ-ONLY (no
    // file is ever rewritten on load/listing); a note adopts the meta
    // the first time it is saved or renamed.
    struct TitleInfo {
        QString display;   // what every UI surface shows
        QString legacyH1;  // first <h1> inner text (best-effort; the
                           // "Noter NN" counter scan needs it even when
                           // the meta wins)
    };

    // Resolve the display title for a note file. Cached by (mtime, size);
    // read failures fall back to the filename prettifier and are NOT
    // cached so a later permission fix self-heals.
    TitleInfo titleInfoForFile(const QString &absPath) const;
    QString   displayTitleForFile(const QString &absPath) const;
    void      invalidateTitleCache(const QString &absPath);

    // Parse the notepatra-title meta out of raw HTML (entity-decoded).
    // Returns empty if absent.
    static QString titleMetaIn(const QString &html);

    // Idempotent upsert of the notepatra-title meta into <head> (falls
    // back to before-<body>, else prepend). An empty title REMOVES any
    // existing tag instead of writing an empty meta.
    static QString withTitleMeta(QString fullHtml, const QString &title);

    // First <h1 ...>...</h1> inner text — tags stripped, entities
    // decoded, simplified, capped at 200 chars. Empty if no h1.
    static QString legacyH1In(const QString &html);

    // Filename → display-title prettifier (the legacy fallback). Strips
    // a leading ".trashed-<ts>-", the date+time prefix (4 OR 6 digit
    // time), maps dashes to spaces, and collapses the default slugs:
    //   <ts>-noter-06.html              → "Noter 06"
    //   <ts>-untitled-meeting-03.html   → "Untitled 03"
    //   <ts>-renamed-by-user.html       → "renamed by user"
    static QString prettyTitleFromFilename(const QString &absPath);

    // List all .html files under root recursively, sorted by mtime desc.
    // Skip files ending in .bak1..bak<depth>, .draft, .tmp — plus .lock,
    // a legacy sidecar from builds that still shipped the (now removed)
    // lock protocol; stale ones may survive on disk.
    QStringList listAllNotes() const;

    // Configurable backup ring depth (default 5). Useful for tests.
    void setBackupRingDepth(int n);

    QString notesRoot() const { return m_root; }

signals:
    void noteSaved(const QString &absolutePath);

private:
    // Backup rotation. Best-effort — if a rotate fails (file-locked on
    // Windows, permission error, etc.) it logs and continues; the save
    // path still proceeds. Backup ring sits at the SAME directory as the
    // canonical .html (so a moved or renamed note doesn't lose history).
    void rotateBackups(const QString &absolutePath);

    // Validate-before-write — parses the <body>...</body> region using
    // QXmlStreamReader after wrapping in an XHTML-ish synthetic root so
    // bare entities / unbalanced tags surface as errors. Returns true if
    // the body is structurally sound. Empty body counts as valid.
    bool validateBody(const QString &fullHtml, QString *errorOut) const;

    QString m_root;
    int     m_backupDepth = 5;

    // Title-resolver cache. Keyed by absolute path, validated against
    // the file's current (mtimeMs, size) so external edits self-heal.
    // saveNote() drops its own entry on every successful save.
    struct TitleCacheEnt { qint64 mtimeMs; qint64 size; TitleInfo info; };
    mutable QHash<QString, TitleCacheEnt> m_titleCache;
};

#endif // NOTES_STORAGE_H
