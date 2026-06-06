// SPDX-License-Identifier: GPL-3.0-or-later
//
// notes_storage — bulletproof HTML storage layer for the Notes feature.
//
// Twelve failure modes defended against:
//   1.  Power loss mid-save              → write-temp + atomic rename
//   2.  Crash before save                → 5-second .draft sidecar
//   3.  Two processes editing same file  → per-PID .lock + stale detection
//   4.  XSS / hostile paste              → whitelist sanitizer (sanitizeBody)
//   5.  Malformed user-supplied HTML     → validate-before-write via
//                                          QXmlStreamReader on body region
//   6.  Disk corruption of newest file   → 5-deep .bak1..bak5 ring
//   7.  Filename-injection on Win/macOS  → safeFilename strips reserved chars
//   8.  Stale lock from killed process   → PID liveness probe per OS
//   9.  Sync-tool partial writes         → list filter skips .tmp/.draft/.bak
//   10. Backup rotation failure          → best-effort, never blocks save
//   11. Size cap readiness (future cap)  → readNote takes errorOut so the
//                                          caller can surface size errors
//   12. Forward template integration     → ForwardDecl of NotesTemplate so
//                                          this file compiles standalone

#ifndef NOTES_STORAGE_H
#define NOTES_STORAGE_H

#include <QObject>
#include <QString>
#include <QStringList>
#include <QDateTime>

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

    // Draft sidecar — called by the 5s autosave timer in NotesPanel.
    // Writes absolutePath + ".draft" non-atomically (small fast write).
    // On launch, NotesPanel checks: if .draft mtime > .html mtime, offer
    // recover. After successful saveNote(), draft is removed.
    bool writeDraft(const QString &absolutePath, const QString &html);
    void clearDraft(const QString &absolutePath);

    // Per-PID lock file at absolutePath + ".lock".
    // Contents: "<pid>\t<isoTimestamp>".
    bool acquireLock(const QString &absolutePath);
    void releaseLock(const QString &absolutePath);

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

    // List all .html files under root recursively, sorted by mtime desc.
    // Skip files ending in .bak1..bak<depth>, .draft, .lock, .tmp.
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
};

#endif // NOTES_STORAGE_H
