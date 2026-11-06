// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef NOTEPATRA_ATOMIC_WRITE_H
#define NOTEPATRA_ATOMIC_WRITE_H

// ────────────────────────────────────────────────────────────────────
// Durable write-temp-then-replace, done correctly on every platform.
//
// Backstory: four call sites independently implemented "atomic save"
// and every one of them got it wrong the same way —
//
//     QFile::remove(target);            // target is now GONE
//     if (!QFile::rename(tmp, target))  // ...and if this fails, the
//         QFile::remove(tmp);           //    new copy goes too
//
// Qt's QFile::rename refuses to overwrite an existing target, so each
// author removed the target first. That opens a window where the file
// does not exist at all, and if the rename then fails the cleanup
// deletes the temp as well — so BOTH copies are gone and the user's
// file is unrecoverable. Nothing about it was atomic; the error string
// said it was.
//
// It shipped in notes_storage.cpp (every Noter note save), ai_tools.cpp
// (the AI agent writing the user's SOURCE FILES), aipanel.cpp (chat
// history, where the rename result was not even checked) and
// dbconnections.cpp (saved database connections). mainwindow.cpp hit it
// on session.json and fixed it locally with MoveFileExW; aipanel.cpp hit
// it again in v0.1.114 and fixed it locally with QSaveFile. Neither fix
// propagated. This is the single source of truth so a fifth site cannot
// get it wrong — and test_atomic_write.cpp scans src/ to enforce that.
//
// NOTE ON THE SPLIT: the implementation deliberately lives in the .cpp
// rather than inline here. The Windows path needs <windows.h>, and
// pulling that into the top of four large translation units risks macro
// collisions that cannot be reproduced on a Linux dev box. Confining it
// to one TU removes the entire question.
// ────────────────────────────────────────────────────────────────────

#include <QString>

class QFile;

namespace NpAtomic {

// Force an open file's payload to STABLE STORAGE. flush() alone only
// drains Qt's userspace buffer into the kernel page cache, so a power
// loss between the replace and the kernel's writeback can surface a
// truncated file under the canonical name. Call BEFORE close().
//
// Returns false if the platform sync call failed or the file has no
// valid descriptor. Callers generally proceed anyway — a failed fsync is
// not by itself a reason to abandon the write.
bool syncData(QFile &file);

// Flush the DIRECTORY entry containing `path`. On POSIX the rename lives
// in the directory's metadata, so without this a power loss right after
// rename(2) can still roll the directory back to the old entry.
// Best-effort; always a no-op on Windows, where NTFS journals the
// MoveFileEx metadata for us.
void syncDirectoryOf(const QString &path);

// Replace `target` with `tmpPath` in a single step.
//
// On success the directory entry is synced too, so the replace is
// durable and not merely visible. On failure `target` is left EXACTLY as
// it was — it is never removed first — the temp is cleaned up, and
// *errorOut (when non-null) carries a platform-specific reason that
// tells the user their file survived.
//
// On POSIX the target's permission bits carry over to the replacement.
//
// The caller is responsible for having already synced the temp file's
// payload via syncData().
bool replaceFile(const QString &tmpPath,
                 const QString &target,
                 QString *errorOut = nullptr);

}   // namespace NpAtomic

#endif   // NOTEPATRA_ATOMIC_WRITE_H
