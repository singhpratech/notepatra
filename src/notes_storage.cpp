// SPDX-License-Identifier: GPL-3.0-or-later

#include "notes_storage.h"

#include <QByteArray>
#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDateTime>
#include <QDebug>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QRegularExpression>
#include <QSet>
#include <QString>
#include <QStringList>
#include <QTextStream>
#include <QUrl>
#include <QXmlStreamReader>

#include <algorithm>
#include <cstdio>
#include <cstdlib>

#if defined(Q_OS_UNIX) || defined(Q_OS_MAC)
    #include <fcntl.h>      // open(O_RDONLY) for the directory fsync
    #include <sys/types.h>
    #include <unistd.h>     // fsync, close
#endif

#ifdef Q_OS_WIN
    #include <windows.h>    // FlushFileBuffers
    #include <io.h>         // _get_osfhandle
#endif

// ═══════════════════════════════════════════════════════════════════════
// Template integration — forward declaration only.
// The actual NotesTemplate::shellHtml lives in notes_template.cpp, written
// by a sibling agent. This forward decl lets notes_storage.cpp compile
// and link standalone as long as SOME translation unit provides the
// symbol at link time. The unit-test build provides a stub via
// test_notes_storage.cpp's own definition.
// ═══════════════════════════════════════════════════════════════════════
namespace NotesTemplate {
    QString shellHtml(const QString &title, const QDateTime &start,
                      const QStringList &attendees);
}

// ═══════════════════════════════════════════════════════════════════════
// Construction / destruction
// ═══════════════════════════════════════════════════════════════════════

NotesStorage::NotesStorage(const QString &notesRoot, QObject *parent)
    : QObject(parent), m_root(notesRoot) {
    if (!m_root.isEmpty()) {
        QDir().mkpath(m_root);
    }
}

NotesStorage::~NotesStorage() = default;

void NotesStorage::setBackupRingDepth(int n) {
    if (n < 1) n = 1;
    if (n > 99) n = 99;        // sanity cap; .bak100 would break the rotate loop
    m_backupDepth = n;
}

// ═══════════════════════════════════════════════════════════════════════
// Template wrapper
// ═══════════════════════════════════════════════════════════════════════

QString NotesStorage::newNoteHtml(const QString &title, const QDateTime &start,
                                  const QStringList &attendees) const {
    return NotesTemplate::shellHtml(title, start, attendees);
}

// ═══════════════════════════════════════════════════════════════════════
// readNote — UTF-8 only. Notes are always our own files; we never have
// to guess encoding the way editor.cpp does for arbitrary user text.
// ═══════════════════════════════════════════════════════════════════════

QString NotesStorage::readNote(const QString &absolutePath, QString *errorOut) const {
    QFile f(absolutePath);
    if (!f.exists()) {
        if (errorOut) *errorOut = QStringLiteral("File does not exist: %1").arg(absolutePath);
        return QString();
    }
    if (!f.open(QIODevice::ReadOnly)) {
        if (errorOut) *errorOut = QStringLiteral("Open failed: %1").arg(f.errorString());
        return QString();
    }
    const QByteArray bytes = f.readAll();
    f.close();
    return QString::fromUtf8(bytes);
}

// ═══════════════════════════════════════════════════════════════════════
// validateBody — parse the body region with QXmlStreamReader so we catch
// unbalanced tags BEFORE we commit them to disk. Notes HTML is
// machine-generated from a sanitizer + template, so it SHOULD always be
// XHTML-clean. If validation fails, we refuse the write instead of
// corrupting the file silently.
// ═══════════════════════════════════════════════════════════════════════

bool NotesStorage::validateBody(const QString &fullHtml, QString *errorOut) const {
    // Locate <body>...</body>. Case-insensitive, tolerates attributes.
    static const QRegularExpression bodyOpenRe(
        QStringLiteral("<body\\b[^>]*>"),
        QRegularExpression::CaseInsensitiveOption);
    static const QRegularExpression bodyCloseRe(
        QStringLiteral("</body\\s*>"),
        QRegularExpression::CaseInsensitiveOption);

    const QRegularExpressionMatch openM = bodyOpenRe.match(fullHtml);
    const QRegularExpressionMatch closeM = bodyCloseRe.match(fullHtml);
    if (!openM.hasMatch() || !closeM.hasMatch() ||
        closeM.capturedStart() <= openM.capturedEnd()) {
        // No body found — treat as already-validated. Caller chose to
        // ship a body-less doc; the template path always produces a body.
        return true;
    }

    const int bodyStart = openM.capturedEnd();
    const int bodyEnd   = closeM.capturedStart();
    QString body = fullHtml.mid(bodyStart, bodyEnd - bodyStart);
    if (body.trimmed().isEmpty()) return true;

    // Wrap in a synthetic XHTML root so QXmlStreamReader has a single
    // top-level element to parse. Define HTML5 named-entity stand-ins
    // that XML doesn't know about — &nbsp; would otherwise blow up the
    // parser even though the input is perfectly valid HTML.
    QString wrapped =
        QStringLiteral(
            "<?xml version=\"1.0\" encoding=\"UTF-8\"?>"
            "<!DOCTYPE root ["
            "  <!ENTITY nbsp \"&#160;\">"
            "  <!ENTITY copy \"&#169;\">"
            "  <!ENTITY reg \"&#174;\">"
            "  <!ENTITY hellip \"&#8230;\">"
            "  <!ENTITY mdash \"&#8212;\">"
            "  <!ENTITY ndash \"&#8211;\">"
            "  <!ENTITY lsquo \"&#8216;\">"
            "  <!ENTITY rsquo \"&#8217;\">"
            "  <!ENTITY ldquo \"&#8220;\">"
            "  <!ENTITY rdquo \"&#8221;\">"
            "  <!ENTITY trade \"&#8482;\">"
            "  <!ENTITY laquo \"&#171;\">"
            "  <!ENTITY raquo \"&#187;\">"
            "  <!ENTITY middot \"&#183;\">"
            "  <!ENTITY bull \"&#8226;\">"
            "  <!ENTITY euro \"&#8364;\">"
            "  <!ENTITY pound \"&#163;\">"
            "  <!ENTITY yen \"&#165;\">"
            "  <!ENTITY cent \"&#162;\">"
            "  <!ENTITY deg \"&#176;\">"
            "  <!ENTITY times \"&#215;\">"
            "  <!ENTITY divide \"&#247;\">"
            "  <!ENTITY plusmn \"&#177;\">"
            "  <!ENTITY larr \"&#8592;\">"
            "  <!ENTITY uarr \"&#8593;\">"
            "  <!ENTITY rarr \"&#8594;\">"
            "  <!ENTITY darr \"&#8595;\">"
            "  <!ENTITY harr \"&#8596;\">"
            "  <!ENTITY infin \"&#8734;\">"
            "  <!ENTITY check \"&#10003;\">"
            "  <!ENTITY cross \"&#10007;\">"
            "  <!ENTITY iexcl \"&#161;\">"
            "  <!ENTITY iquest \"&#191;\">"
            "  <!ENTITY szlig \"&#223;\">"
            "]>"
            "<root>") + body + QStringLiteral("</root>");

    QXmlStreamReader xml(wrapped);
    while (!xml.atEnd()) {
        xml.readNext();
    }
    if (xml.hasError()) {
        if (errorOut) {
            *errorOut = QStringLiteral("HTML body parse error at line %1, col %2: %3")
                            .arg(xml.lineNumber())
                            .arg(xml.columnNumber())
                            .arg(xml.errorString());
        }
        return false;
    }
    return true;
}

// ═══════════════════════════════════════════════════════════════════════
// saveNote — the six-step atomic protocol.
//
// Design choice: we use explicit `.tmp + rename` instead of QSaveFile.
// Two reasons —
//   1. QFile::rename refuses to overwrite the target on Windows;
//      src/ai_tools.cpp already established the manual posix
//      std::rename / Win32 remove+rename idiom and we mirror it here for
//      consistency.
//   2. QSaveFile silently leaves a `.qsave` orphan when the process
//      dies between commitWithReplace and dtor — the explicit form
//      makes the orphan path predictable so listAllNotes can filter it.
// ═══════════════════════════════════════════════════════════════════════

bool NotesStorage::saveNote(const QString &absolutePath, const QString &fullHtml,
                            QString *errorOut) {
    if (absolutePath.isEmpty()) {
        if (errorOut) *errorOut = QStringLiteral("Empty absolutePath");
        return false;
    }

    // (1) Sanitize body. We sanitize the BODY portion only; the <head>
    //     comes from our own template and is trusted.
    static const QRegularExpression bodyOpenRe(
        QStringLiteral("<body\\b[^>]*>"),
        QRegularExpression::CaseInsensitiveOption);
    static const QRegularExpression bodyCloseRe(
        QStringLiteral("</body\\s*>"),
        QRegularExpression::CaseInsensitiveOption);

    QString cleaned = fullHtml;
    const QRegularExpressionMatch openM = bodyOpenRe.match(fullHtml);
    const QRegularExpressionMatch closeM = bodyCloseRe.match(fullHtml);
    if (openM.hasMatch() && closeM.hasMatch() &&
        closeM.capturedStart() > openM.capturedEnd()) {
        const int bodyStart = openM.capturedEnd();
        const int bodyEnd   = closeM.capturedStart();
        const QString body   = fullHtml.mid(bodyStart, bodyEnd - bodyStart);
        const QString sane   = sanitizeBody(body);
        cleaned = fullHtml.left(bodyStart) + sane + fullHtml.mid(bodyEnd);
    } else {
        // No body tags — sanitize the whole document. (Tests sometimes
        // pass body-only fragments.)
        cleaned = sanitizeBody(fullHtml);
    }

    // (2) Validate-before-write.
    QString validationErr;
    if (!validateBody(cleaned, &validationErr)) {
        if (errorOut) *errorOut = validationErr;
        return false;
    }

    // Make sure the target directory exists.
    QFileInfo fi(absolutePath);
    QDir().mkpath(fi.absolutePath());

    // (3) Rotate backups.  Best-effort — failures here are logged but
    //     never abort the save. Power-loss between rotate and write is
    //     fine: the .html is still on disk untouched, the .bak ring is
    //     just one step ahead.
    rotateBackups(absolutePath);

    // (4) Write to a sibling `.tmp` and (5) atomic-rename. Mirror the
    //     src/ai_tools.cpp idiom.
    const QString tmpPath = absolutePath + QStringLiteral(".tmp");
    {
        QFile tmp(tmpPath);
        if (!tmp.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
            if (errorOut)
                *errorOut = QStringLiteral("Failed to open temp file: %1").arg(tmp.errorString());
            return false;
        }
        const QByteArray bytes = cleaned.toUtf8();
        const qint64 n = tmp.write(bytes);
        // Force the data to STABLE STORAGE before the rename. flush()
        // only drains Qt's userspace buffer into the kernel page cache —
        // a power loss between rename and the kernel's writeback used to
        // be able to surface a zero-length (or truncated) note under the
        // canonical name. fsync the tmp file's payload first, so the
        // rename can only ever publish fully-durable bytes. (A7 fix —
        // the old comment claimed fsync-equivalence that never existed.)
        tmp.flush();
#ifdef Q_OS_WIN
        FlushFileBuffers(reinterpret_cast<HANDLE>(_get_osfhandle(tmp.handle())));
#else
        if (tmp.handle() >= 0) ::fsync(tmp.handle());
#endif
        tmp.close();
        if (n != bytes.size()) {
            QFile::remove(tmpPath);
            if (errorOut) *errorOut = QStringLiteral("Short write to temp file");
            return false;
        }
    }

#ifdef Q_OS_WIN
    QFile::remove(absolutePath);
    if (!QFile::rename(tmpPath, absolutePath)) {
        QFile::remove(tmpPath);
        if (errorOut) *errorOut = QStringLiteral("Atomic rename failed");
        return false;
    }
#else
    if (std::rename(tmpPath.toLocal8Bit().constData(),
                    absolutePath.toLocal8Bit().constData()) != 0) {
        QFile::remove(tmpPath);
        if (errorOut) *errorOut = QStringLiteral("Atomic rename failed");
        return false;
    }
    // Flush the DIRECTORY entry too — on POSIX the rename itself lives
    // in the directory's metadata; without this a power loss right after
    // rename(2) could still roll the directory back to the old entry.
    // Best-effort: a failure here never fails the save (the data fsync
    // above already guarantees no torn content). No-op on Windows, where
    // MoveFileEx-style metadata is journalled by NTFS.
    {
        const QByteArray dirPath =
            QFileInfo(absolutePath).absolutePath().toLocal8Bit();
        const int dfd = ::open(dirPath.constData(), O_RDONLY);
        if (dfd >= 0) {
            ::fsync(dfd);
            ::close(dfd);
        }
    }
#endif

    // (6) Emit signal + clear the draft sidecar (a successful save
    //     supersedes any outstanding draft). Drop the title-resolver
    //     cache entry FIRST so any handler re-reading the display title
    //     sees the just-written content — this auto-invalidates every
    //     save path without requiring panel discipline.
    m_titleCache.remove(absolutePath);
    clearDraft(absolutePath);
    emit noteSaved(absolutePath);
    return true;
}

// ═══════════════════════════════════════════════════════════════════════
// rotateBackups — shift .bakN-1 → .bakN, then .html → .bak1. Always
// drops anything at .bak<depth>. Best-effort — never throws.
// ═══════════════════════════════════════════════════════════════════════

void NotesStorage::rotateBackups(const QString &absolutePath) {
    // .bak<depth-1> → .bak<depth>, …, .bak1 → .bak2
    for (int i = m_backupDepth; i >= 2; --i) {
        const QString src = absolutePath + QStringLiteral(".bak%1").arg(i - 1);
        const QString dst = absolutePath + QStringLiteral(".bak%1").arg(i);
        if (!QFile::exists(src)) continue;
        QFile::remove(dst);    // ignore failure; rename below will tell us
        if (!QFile::rename(src, dst)) {
            std::fprintf(stderr,
                         "[notes_storage] backup rotate %d→%d failed for %s\n",
                         i - 1, i, absolutePath.toUtf8().constData());
        }
    }

    // .html → .bak1
    if (QFile::exists(absolutePath)) {
        const QString dst = absolutePath + QStringLiteral(".bak1");
        QFile::remove(dst);
        // Use copy not rename so an interrupted save doesn't leave the
        // canonical file missing. We accept the duplicate cost (notes
        // are tiny) in exchange for "canonical .html always exists on
        // disk during the save".
        if (!QFile::copy(absolutePath, dst)) {
            std::fprintf(stderr,
                         "[notes_storage] backup snapshot failed for %s\n",
                         absolutePath.toUtf8().constData());
        }
    }
}

// ═══════════════════════════════════════════════════════════════════════
// Draft sidecar — fast non-atomic write; survives a soft crash, gets
// supplanted by the next saveNote().
// ═══════════════════════════════════════════════════════════════════════

bool NotesStorage::writeDraft(const QString &absolutePath, const QString &html) {
    const QString draftPath = absolutePath + QStringLiteral(".draft");
    QFileInfo fi(absolutePath);
    QDir().mkpath(fi.absolutePath());
    QFile f(draftPath);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate)) return false;
    const QByteArray b = html.toUtf8();
    const qint64 n = f.write(b);
    f.close();
    return n == b.size();
}

void NotesStorage::clearDraft(const QString &absolutePath) {
    QFile::remove(absolutePath + QStringLiteral(".draft"));
}

// ═══════════════════════════════════════════════════════════════════════
// NOTE (A7): the per-PID .lock protocol that used to live here was
// removed. It was dead code — documented in the header, ZERO callers —
// i.e. a false promise of two-instance protection. Multi-instance
// editing needs an explicit opt-in design; until then the header is
// honest about what is and isn't protected. listAllNotes still filters
// stale *.lock sidecars left behind by older builds.
// ═══════════════════════════════════════════════════════════════════════

// ═══════════════════════════════════════════════════════════════════════
// Sanitizer.
//
// FORBIDDEN:
//   <script>, <iframe>, <object>, <embed>, <form>, <input>, <button>,
//   <style>  (in body — head <style> is template-controlled and never
//             reaches the sanitizer),
//   on* attributes (onclick, onload, onerror, …),
//   inline style= attribute,
//   javascript:* URLs (href / src),
//   data:* URLs except image/png|jpg|jpeg|gif|webp|svg+xml.
//
// ALLOWED TAGS:
//   p div span a img h1 h2 h3 h4 h5 h6 ul ol li blockquote pre code
//   strong em b i u s br hr table thead tbody tr td th
//   header footer main section article figure figcaption
//
// ALLOWED ATTRS:
//   class, id, data-*, href, src, alt, title, rowspan, colspan, lang,
//   width (on img/table only), height (on img/table only),
//   target (rewritten to _blank + rel=noopener), rel.
//
// Implementation: a small tag-scanner. Not a full HTML5 parser — but
// the test-suite covers every OWASP XSS Filter Evasion vector listed
// in the spec PLUS the eight project-specified payloads. The
// sanitizer is conservative: if in doubt, DROP.
// ═══════════════════════════════════════════════════════════════════════

namespace {

const QSet<QString> &allowedTags() {
    static const QSet<QString> s = {
        "p", "div", "span", "a", "img",
        "h1", "h2", "h3", "h4", "h5", "h6",
        "ul", "ol", "li",
        "blockquote", "pre", "code",
        "strong", "em", "b", "i", "u", "s",
        "br", "hr",
        "table", "thead", "tbody", "tr", "td", "th",
        "header", "footer", "main", "section", "article",
        "figure", "figcaption"
    };
    return s;
}

const QSet<QString> &voidTags() {
    static const QSet<QString> s = {
        "br", "hr", "img"
    };
    return s;
}

const QSet<QString> &allowedAttrs() {
    static const QSet<QString> s = {
        "class", "id", "href", "src", "alt", "title",
        "rowspan", "colspan", "lang", "width", "height",
        "target", "rel",
        // Noter-specific — contenteditable enables the editor surface;
        // without it the saved <main> opens as a read-only artifact.
        // spellcheck stays user-controlled.
        "contenteditable", "spellcheck"
    };
    return s;
}

// Image data: URL allowlist. Anything else with a data: scheme is dropped.
bool dataUrlAllowed(const QString &url) {
    // url already lowercased at call site.
    if (!url.startsWith("data:")) return true;
    static const QStringList ok = {
        "data:image/png",     "data:image/jpeg",    "data:image/jpg",
        "data:image/gif",     "data:image/webp",    "data:image/svg+xml"
    };
    for (const QString &p : ok) {
        if (url.startsWith(p)) return true;
    }
    return false;
}

bool urlIsSafe(const QString &raw) {
    // Trim whitespace + decode HTML entities for the common XSS
    // evasions (&#106;avascript: → javascript:).
    QString u = raw.trimmed();
    // Strip wrapping quotes if accidentally still attached.
    if (u.startsWith('"') || u.startsWith('\'')) u = u.mid(1);
    if (u.endsWith('"')   || u.endsWith('\''))   u.chop(1);

    // Decode numeric entities (&#NN;, &#xHH;) since `javascript:` can
    // be encoded as `&#106;avascript:` in attribute context.
    QString decoded;
    decoded.reserve(u.size());
    int i = 0;
    while (i < u.size()) {
        if (u[i] == '&' && i + 1 < u.size() && u[i+1] == '#') {
            int j = i + 2;
            int base = 10;
            if (j < u.size() && (u[j] == 'x' || u[j] == 'X')) { base = 16; ++j; }
            const int start = j;
            while (j < u.size() && u[j] != ';' &&
                   (base == 10 ? u[j].isDigit()
                               : (u[j].isDigit() || (u[j].toLower() >= 'a' && u[j].toLower() <= 'f'))))
                ++j;
            bool ok = false;
            const uint cp = u.mid(start, j - start).toUInt(&ok, base);
            if (ok && cp > 0 && cp < 0x110000) {
                decoded += QChar(static_cast<uint>(cp));
                if (j < u.size() && u[j] == ';') ++j;
                i = j;
                continue;
            }
        }
        // Also strip control chars / NULs inline — `java\x00script:`
        // and `java\tscript:` are textbook evasions.
        if (u[i].unicode() < 0x20 || u[i].unicode() == 0x7F) {
            ++i;
            continue;
        }
        decoded += u[i++];
    }
    const QString lower = decoded.toLower();

    // After decoding, blacklist dangerous schemes.
    if (lower.startsWith("javascript:")) return false;
    if (lower.startsWith("vbscript:"))   return false;
    if (lower.startsWith("file:"))       return false;
    if (lower.startsWith("about:"))      return false;
    if (lower.startsWith("data:") && !dataUrlAllowed(lower)) return false;

    return true;
}

// Parse `key=value` pairs from an opening tag's attribute region.
// Tolerates unquoted, single-quoted, double-quoted values plus
// boolean attrs.
QVector<QPair<QString, QString>> parseAttrs(const QString &attrRegion) {
    QVector<QPair<QString, QString>> out;
    int i = 0;
    const int N = attrRegion.size();
    while (i < N) {
        while (i < N && attrRegion[i].isSpace()) ++i;
        if (i >= N) break;
        // Skip stray / on self-closing tags.
        if (attrRegion[i] == '/') { ++i; continue; }
        // Attr name — accept alnum, '-', '_', ':'.
        const int nameStart = i;
        while (i < N) {
            const QChar c = attrRegion[i];
            if (c.isLetterOrNumber() || c == '-' || c == '_' || c == ':') { ++i; continue; }
            break;
        }
        if (i == nameStart) { ++i; continue; }
        QString name = attrRegion.mid(nameStart, i - nameStart).toLower();
        // Optional whitespace then '='.
        while (i < N && attrRegion[i].isSpace()) ++i;
        QString value;
        if (i < N && attrRegion[i] == '=') {
            ++i;
            while (i < N && attrRegion[i].isSpace()) ++i;
            if (i < N && (attrRegion[i] == '"' || attrRegion[i] == '\'')) {
                const QChar q = attrRegion[i++];
                const int s = i;
                while (i < N && attrRegion[i] != q) ++i;
                value = attrRegion.mid(s, i - s);
                if (i < N) ++i;     // skip closing quote
            } else {
                const int s = i;
                while (i < N && !attrRegion[i].isSpace() && attrRegion[i] != '>')
                    ++i;
                value = attrRegion.mid(s, i - s);
            }
        }
        out.push_back({ name, value });
    }
    return out;
}

QString escapeAttrValue(const QString &v) {
    QString out;
    out.reserve(v.size() + 8);
    for (const QChar c : v) {
        switch (c.unicode()) {
            case '<': out += "&lt;";  break;
            case '>': out += "&gt;";  break;
            case '"': out += "&quot;"; break;
            case '\'': out += "&#39;"; break;
            case '&': out += "&amp;"; break;
            default:  out += c;
        }
    }
    return out;
}

} // anonymous namespace

QString NotesStorage::sanitizeBody(const QString &dirtyHtml) {
    if (dirtyHtml.isEmpty()) return QString();

    QString out;
    out.reserve(dirtyHtml.size());

    // Track opened-and-emitted tags so we can auto-close at the end.
    QStringList openStack;

    const int N = dirtyHtml.size();
    int i = 0;
    while (i < N) {
        const QChar c = dirtyHtml[i];

        if (c == '<') {
            // Comment? <!-- ... -->  Drop entirely.
            if (dirtyHtml.mid(i, 4) == "<!--") {
                const int end = dirtyHtml.indexOf("-->", i + 4);
                i = (end < 0) ? N : end + 3;
                continue;
            }
            // DOCTYPE / processing instruction — drop.
            if (i + 1 < N && (dirtyHtml[i+1] == '!' || dirtyHtml[i+1] == '?')) {
                const int end = dirtyHtml.indexOf('>', i);
                i = (end < 0) ? N : end + 1;
                continue;
            }

            // Closing tag?
            bool isClose = (i + 1 < N && dirtyHtml[i+1] == '/');
            int nameStart = i + 1 + (isClose ? 1 : 0);
            int j = nameStart;
            while (j < N) {
                const QChar nc = dirtyHtml[j];
                if (nc.isLetterOrNumber() || nc == '-' || nc == '_') { ++j; continue; }
                break;
            }
            if (j == nameStart) {
                // Lone '<' — escape literal and move on.
                out += "&lt;";
                ++i;
                continue;
            }
            QString tag = dirtyHtml.mid(nameStart, j - nameStart).toLower();

            // Find end of tag.
            const int end = dirtyHtml.indexOf('>', j);
            if (end < 0) {
                // Unterminated tag — drop the rest.
                break;
            }
            const QString attrRegion = isClose ? QString() : dirtyHtml.mid(j, end - j);
            const bool selfClose = !isClose && attrRegion.trimmed().endsWith('/');
            i = end + 1;

            // Disallowed tags — DROP TAG AND CONTENTS for the script-y ones.
            // For <style> inside body, we drop the contents too.
            static const QSet<QString> dropWithContents = {
                "script", "iframe", "object", "embed", "form", "input",
                "button", "textarea", "select", "option", "style",
                "meta", "link", "base", "frame", "frameset", "applet",
                "noscript", "noframes", "svg", "math"
            };
            if (dropWithContents.contains(tag)) {
                if (!isClose && !selfClose && !voidTags().contains(tag)) {
                    // Skip until matching </tag>, case-insensitive.
                    const QString needle = QStringLiteral("</") + tag;
                    int k = i;
                    while (k < N) {
                        const int hit = dirtyHtml.indexOf(needle, k, Qt::CaseInsensitive);
                        if (hit < 0) { k = N; break; }
                        // Confirm it's terminated with '>' or whitespace.
                        const int after = hit + needle.size();
                        if (after >= N) { k = N; break; }
                        const QChar a = dirtyHtml[after];
                        if (a == '>' || a.isSpace()) {
                            const int closeEnd = dirtyHtml.indexOf('>', after);
                            k = (closeEnd < 0) ? N : closeEnd + 1;
                            break;
                        }
                        k = hit + needle.size();
                    }
                    i = k;
                }
                continue;
            }

            if (!allowedTags().contains(tag)) {
                // Unknown tag — drop the tag but keep the inner text.
                continue;
            }

            if (isClose) {
                if (openStack.contains(tag)) {
                    // Close any tags opened AFTER `tag` so we don't leak
                    // overlap. (Foo<bar>baz</foo> → <foo>baz</foo>.)
                    while (!openStack.isEmpty() && openStack.last() != tag) {
                        out += "</" + openStack.takeLast() + ">";
                    }
                    if (!openStack.isEmpty()) {
                        openStack.removeLast();
                        out += "</" + tag + ">";
                    }
                }
                continue;
            }

            // Opening tag — sanitize attrs.
            const auto attrs = parseAttrs(attrRegion);
            QString rendered = "<" + tag;
            bool addedRelNoopener = false;
            for (const auto &kv : attrs) {
                const QString &k = kv.first;
                QString v = kv.second;

                // on* event handlers — strip outright.
                if (k.startsWith("on")) continue;

                // Inline style — strip outright (no exceptions; this is a
                // store-and-render-untrusted-HTML pipeline, CSS expressions
                // and url(javascript:…) are too risky).
                if (k == "style") continue;

                // data-* always OK.
                if (k.startsWith("data-")) {
                    rendered += " " + k + "=\"" + escapeAttrValue(v) + "\"";
                    continue;
                }

                // v0.1.112 — Noter AI-Extract region markers. `name` is
                // allowed ONLY on <a> and ONLY with the np-extract- prefix
                // (the invisible begin/end anchors the extract-apply layer
                // keys on — see notes_extract_apply.h); every other name=
                // still drops. Deliberately NOT in allowedAttrs(): a
                // blanket name allowance would re-open DOM-clobbering
                // shaped surface on img/form/etc.
                if (k == QLatin1String("name")) {
                    if (tag == QLatin1String("a") &&
                        v.startsWith(QLatin1String("np-extract-")))
                        rendered += " name=\"" + escapeAttrValue(v) + "\"";
                    continue;
                }

                if (!allowedAttrs().contains(k)) continue;

                // URL attrs need scheme validation.
                if (k == "href" || k == "src") {
                    if (!urlIsSafe(v)) continue;
                }

                // width/height only valid on img and table.
                if ((k == "width" || k == "height") &&
                    !(tag == "img" || tag == "table")) {
                    continue;
                }

                // target=_blank → enforce rel=noopener noreferrer.
                if (tag == "a" && k == "target") {
                    rendered += " target=\"_blank\"";
                    if (!addedRelNoopener) {
                        rendered += " rel=\"noopener noreferrer\"";
                        addedRelNoopener = true;
                    }
                    continue;
                }
                if (tag == "a" && k == "rel") {
                    // Force-include noopener/noreferrer regardless of input.
                    if (!addedRelNoopener) {
                        rendered += " rel=\"noopener noreferrer\"";
                        addedRelNoopener = true;
                    }
                    continue;
                }

                rendered += " " + k + "=\"" + escapeAttrValue(v) + "\"";
            }

            if (selfClose || voidTags().contains(tag)) {
                rendered += " />";
                out += rendered;
            } else {
                rendered += ">";
                out += rendered;
                openStack.push_back(tag);
            }
            continue;
        }

        // Plain text — copy verbatim. The browser handles entity decoding.
        out += c;
        ++i;
    }

    // Auto-close any tags still open at body end. Mismatched-tag defense.
    while (!openStack.isEmpty()) {
        out += "</" + openStack.takeLast() + ">";
    }

    return out;
}

// ═══════════════════════════════════════════════════════════════════════
// Filename safety.
//
// Strategy:
//   1. ASCII-fold common Latin-script accented characters (é→e, ä→a, ñ→n).
//      Non-Latin scripts (Cyrillic, Greek, CJK, emoji) get dropped after
//      step 3 because step 4 keeps only [a-z0-9-_.].
//   2. Replace any whitespace run with a single dash.
//   3. Strip Win32 reserved chars < > : " / \ | ? * and ASCII controls
//      and NUL.
//   4. Lowercase.
//   5. Collapse repeated dashes, trim leading/trailing dashes and dots
//      (Windows hates trailing dots; ".hidden" leading dots break some
//      backup tools).
//   6. Cap at 200 chars.
//   7. Win32 reserved basenames (CON, NUL, AUX, PRN, COM1..9, LPT1..9)
//      get prefixed with `_`.
//   8. Empty after all of the above → "untitled".
// ═══════════════════════════════════════════════════════════════════════

static QString asciiFold(const QString &in) {
    // Tiny per-codepoint table; covers Latin-1 and Latin Extended-A which
    // is enough for ~95% of European-language names without dragging in
    // ICU. Falls through unchanged for everything else (step 4 strips).
    QString out;
    out.reserve(in.size());
    for (const QChar c : in) {
        const ushort u = c.unicode();
        switch (u) {
            // Vowels
            case 0x00E0: case 0x00E1: case 0x00E2: case 0x00E3:
            case 0x00E4: case 0x00E5: case 0x0101: case 0x0103:
            case 0x0105: out += 'a'; break;
            case 0x00C0: case 0x00C1: case 0x00C2: case 0x00C3:
            case 0x00C4: case 0x00C5: case 0x0100: case 0x0102:
            case 0x0104: out += 'A'; break;
            case 0x00E8: case 0x00E9: case 0x00EA: case 0x00EB:
            case 0x0113: case 0x0115: case 0x0117: case 0x0119:
            case 0x011B: out += 'e'; break;
            case 0x00C8: case 0x00C9: case 0x00CA: case 0x00CB:
            case 0x0112: case 0x0114: case 0x0116: case 0x0118:
            case 0x011A: out += 'E'; break;
            case 0x00EC: case 0x00ED: case 0x00EE: case 0x00EF:
            case 0x0129: case 0x012B: case 0x012D: case 0x012F:
                out += 'i'; break;
            case 0x00CC: case 0x00CD: case 0x00CE: case 0x00CF:
            case 0x0128: case 0x012A: case 0x012C: case 0x012E:
                out += 'I'; break;
            case 0x00F2: case 0x00F3: case 0x00F4: case 0x00F5:
            case 0x00F6: case 0x00F8: case 0x014D: case 0x014F:
            case 0x0151: out += 'o'; break;
            case 0x00D2: case 0x00D3: case 0x00D4: case 0x00D5:
            case 0x00D6: case 0x00D8: case 0x014C: case 0x014E:
            case 0x0150: out += 'O'; break;
            case 0x00F9: case 0x00FA: case 0x00FB: case 0x00FC:
            case 0x0169: case 0x016B: case 0x016D: case 0x016F:
            case 0x0171: case 0x0173: out += 'u'; break;
            case 0x00D9: case 0x00DA: case 0x00DB: case 0x00DC:
            case 0x0168: case 0x016A: case 0x016C: case 0x016E:
            case 0x0170: case 0x0172: out += 'U'; break;
            case 0x00FD: case 0x00FF: case 0x0177: out += 'y'; break;
            case 0x00DD: case 0x0176: case 0x0178: out += 'Y'; break;
            // Consonants
            case 0x00E7: case 0x0107: case 0x0109: case 0x010B:
            case 0x010D: out += 'c'; break;
            case 0x00C7: case 0x0106: case 0x0108: case 0x010A:
            case 0x010C: out += 'C'; break;
            case 0x00F1: case 0x0144: case 0x0146: case 0x0148:
                out += 'n'; break;
            case 0x00D1: case 0x0143: case 0x0145: case 0x0147:
                out += 'N'; break;
            case 0x015B: case 0x015D: case 0x015F: case 0x0161:
                out += 's'; break;
            case 0x015A: case 0x015C: case 0x015E: case 0x0160:
                out += 'S'; break;
            case 0x017A: case 0x017C: case 0x017E: out += 'z'; break;
            case 0x0179: case 0x017B: case 0x017D: out += 'Z'; break;
            case 0x00DF: out += "ss"; break;
            case 0x00E6: out += "ae"; break;
            case 0x00C6: out += "AE"; break;
            case 0x0153: out += "oe"; break;
            case 0x0152: out += "OE"; break;
            case 0x0142: out += 'l'; break;
            case 0x0141: out += 'L'; break;
            case 0x010F: out += 'd'; break;
            case 0x010E: out += 'D'; break;
            case 0x0167: out += 't'; break;
            case 0x0166: out += 'T'; break;
            case 0x0159: case 0x0155: out += 'r'; break;
            case 0x0158: case 0x0154: out += 'R'; break;
            case 0x011F: case 0x011D: case 0x0121: case 0x0123:
                out += 'g'; break;
            case 0x011E: case 0x011C: case 0x0120: case 0x0122:
                out += 'G'; break;
            case 0x0125: case 0x0127: out += 'h'; break;
            case 0x0124: case 0x0126: out += 'H'; break;
            case 0x0135: out += 'j'; break;
            case 0x0134: out += 'J'; break;
            case 0x0137: out += 'k'; break;
            case 0x0136: out += 'K'; break;
            // U+00D7 × multiplication sign — keep going.
            default: out += c;
        }
    }
    return out;
}

// v0.1.112 (retrieval-redos) — strip <head>/<style>/<script> regions in a
// SINGLE linear forward pass built ENTIRELY from QString::indexOf (which
// searches from a start offset WITHOUT re-scanning the prefix). The old form
// removed two DotMatchesEverything regexes — <head\b[^>]*>.*?</head\s*> and
// <(style|script)\b[^>]*>.*?</\1\s*> — whose lazy .*? re-scanned to
// end-of-string at EVERY opening tag, so a malformed/synced/truncated
// multi-block .html with many UNCLOSED opens cost O(opens × bytes): a 2MB note
// froze the GUI thread for ~150s on the body-search path. NOTE: an early
// rewrite used QRegularExpression::match(in, offset) in the loop — that ALSO
// went quadratic (~125s on a 2MB CLOSED-style flood), because the offset form
// still re-validates the whole subject per call. indexOf does not. Each open
// makes exactly one decision — find its matching close (forward search) or, if
// unclosed, drop the remainder and stop — so total work is O(n). Closed
// regions strip byte-identically to the old regexes: same tag names, same
// </name\s*> close form, same case-insensitivity, same \b word boundary (so
// <header>/<styled>/<scripts> are NOT treated as containers).
static QString stripContainerRegions(const QString &in) {
    static const QString kNames[3] = {
        QStringLiteral("head"), QStringLiteral("style"), QStringLiteral("script")
    };
    QString out;
    out.reserve(in.size());
    const int n = in.size();
    int i = 0;
    while (i < n) {
        const int lt = in.indexOf(QLatin1Char('<'), i);
        if (lt < 0) { out += in.mid(i); break; }   // no more tags — keep the tail
        // Is this '<' the start of a <head|style|script container open?
        int which = -1;
        for (int k = 0; k < 3; ++k) {
            const int afterName = lt + 1 + kNames[k].size();
            if (afterName > n) continue;
            if (in.mid(lt + 1, kNames[k].size())
                    .compare(kNames[k], Qt::CaseInsensitive) != 0)
                continue;
            // \b — the char after the name must NOT be a word char (end of
            // input counts as a boundary). Keeps <styled>/<scripts>/<header>.
            if (afterName < n) {
                const QChar c = in.at(afterName);
                if (c.isLetterOrNumber() || c == QLatin1Char('_')) continue;
            }
            which = k;
            break;
        }
        if (which < 0) {                            // ordinary tag — keep '<' and move on
            out += in.mid(i, lt + 1 - i);           // (kTag strips it downstream)
            i = lt + 1;
            continue;
        }
        out += in.mid(i, lt - i);                   // keep text before the container open
        const int gt = in.indexOf(QLatin1Char('>'), lt + 1);
        if (gt < 0) { out += in.mid(lt); break; }   // truncated open — keep residue
        // Find the matching close </name\s*> (replicates the \1 backreference).
        const QString closePrefix = QStringLiteral("</") + kNames[which];
        int regionEnd = -1;
        int from = gt + 1;
        while (true) {
            const int cl = in.indexOf(closePrefix, from, Qt::CaseInsensitive);
            if (cl < 0) break;                      // no real close anywhere
            int p = cl + closePrefix.size();        // require optional \s* then '>'
            while (p < n && in.at(p).isSpace()) ++p;
            if (p < n && in.at(p) == QLatin1Char('>')) { regionEnd = p + 1; break; }
            from = cl + closePrefix.size();         // "</styled>" etc. — not a close, keep looking
        }
        if (regionEnd < 0) break;                   // UNCLOSED — drop remainder, stop (linearity guard)
        i = regionEnd;                              // skip the whole region
    }
    return out;
}

// v0.1.112 — see the contract comment in notes_storage.h. Pure, QtCore-only.
QString NotesStorage::plainTextForSearch(const QString &fullHtml) {
    QString s = fullHtml;
    static const QRegularExpression kTag(QStringLiteral("<[^>]*>"));
    s = stripContainerRegions(s);
    s.replace(kTag, QStringLiteral(" "));
    s.replace(QStringLiteral("&nbsp;"), QStringLiteral(" "));
    s.replace(QStringLiteral("&lt;"),   QStringLiteral("<"));
    s.replace(QStringLiteral("&gt;"),   QStringLiteral(">"));
    s.replace(QStringLiteral("&quot;"), QStringLiteral("\""));
    s.replace(QStringLiteral("&#39;"),  QStringLiteral("'"));
    s.replace(QStringLiteral("&apos;"), QStringLiteral("'"));
    s.replace(QStringLiteral("&amp;"),  QStringLiteral("&"));   // MUST be last
    return s.simplified();
}

QString NotesStorage::safeFilename(const QString &raw) {
    if (raw.isEmpty()) return QStringLiteral("untitled");

    QString s = asciiFold(raw);

    // Replace whitespace runs with a single dash.
    s.replace(QRegularExpression(QStringLiteral("\\s+")), QStringLiteral("-"));

    // Lowercase first so we can preserve the case-fold (Cyrillic etc.
    // will still be stripped in the next pass).
    s = s.toLower();

    // Strip everything that isn't [a-z0-9-_.].
    QString cleaned;
    cleaned.reserve(s.size());
    for (const QChar c : s) {
        const ushort u = c.unicode();
        if ((u >= 'a' && u <= 'z') ||
            (u >= '0' && u <= '9') ||
            u == '-' || u == '_' || u == '.') {
            cleaned += c;
        }
        // Everything else (Cyrillic, CJK, emoji, < > : " / \ | ? *,
        // ASCII controls, NUL) — DROP.
    }
    s = cleaned;

    // Collapse repeat dashes.
    s.replace(QRegularExpression(QStringLiteral("-{2,}")), QStringLiteral("-"));

    // Trim leading/trailing dashes and dots.
    while (!s.isEmpty() && (s.startsWith('-') || s.startsWith('.'))) s.remove(0, 1);
    while (!s.isEmpty() && (s.endsWith('-')   || s.endsWith('.')))   s.chop(1);

    // Cap at 200 chars. Preserve the extension (if any) so a trim
    // doesn't strip ".html".
    if (s.size() > 200) {
        const int dot = s.lastIndexOf('.');
        if (dot > 0 && (s.size() - dot) <= 8) {
            const QString ext = s.mid(dot);
            const QString base = s.left(dot);
            s = base.left(200 - ext.size()) + ext;
        } else {
            s = s.left(200);
        }
        // Trim again — might end on a dash now.
        while (!s.isEmpty() && (s.endsWith('-') || s.endsWith('.'))) s.chop(1);
    }

    if (s.isEmpty()) return QStringLiteral("untitled");

    // Win32 reserved basenames. Strip the extension just for the test.
    const QString basenameOnly = s.contains('.') ? s.section('.', 0, 0) : s;
    static const QSet<QString> reserved = {
        "con", "nul", "aux", "prn",
        "com1", "com2", "com3", "com4", "com5", "com6", "com7", "com8", "com9",
        "lpt1", "lpt2", "lpt3", "lpt4", "lpt5", "lpt6", "lpt7", "lpt8", "lpt9"
    };
    if (reserved.contains(basenameOnly)) {
        s = "_" + s;
    }

    return s;
}

// ═══════════════════════════════════════════════════════════════════════
// Title identity — the notepatra-title meta is the on-disk single source
// of truth for a note's display title. Everything here is QtCore-only
// (test_notes_storage links Core+Network+Test — no QtGui allowed).
//
// Escaping uses the SAME 5-entity table as NotesTemplate::escapeText so
// the template-written meta and the save-path-injected meta round-trip
// identically. Decode order matters: &amp; LAST, so "&amp;lt;" comes
// back as the literal "&lt;" the user typed, not "<".
// ═══════════════════════════════════════════════════════════════════════

static QString titleAttrEscape(const QString &raw) {
    QString out;
    out.reserve(raw.size() + 16);
    for (const QChar c : raw) {
        switch (c.unicode()) {
            case '&':  out += QStringLiteral("&amp;");  break;
            case '<':  out += QStringLiteral("&lt;");   break;
            case '>':  out += QStringLiteral("&gt;");   break;
            case '"':  out += QStringLiteral("&quot;"); break;
            case '\'': out += QStringLiteral("&#39;");  break;
            default:   out += c;
        }
    }
    return out;
}

static QString titleAttrUnescape(QString s) {
    s.replace(QLatin1String("&lt;"),   QLatin1String("<"));
    s.replace(QLatin1String("&gt;"),   QLatin1String(">"));
    s.replace(QLatin1String("&quot;"), QLatin1String("\""));
    s.replace(QLatin1String("&#39;"),  QLatin1String("'"));
    s.replace(QLatin1String("&amp;"),  QLatin1String("&"));   // LAST
    return s;
}

// The parse regex tolerates single- OR double-quoted content; the writer
// always emits double quotes.
static const QRegularExpression &titleMetaRe() {
    static const QRegularExpression re(
        QStringLiteral("<meta\\s+name=\"notepatra-title\"\\s+content=(['\"])(.*?)\\1"),
        QRegularExpression::CaseInsensitiveOption
            | QRegularExpression::DotMatchesEverythingOption);
    return re;
}

QString NotesStorage::titleMetaIn(const QString &html) {
    const QRegularExpressionMatch m = titleMetaRe().match(html);
    if (!m.hasMatch()) return QString();
    return titleAttrUnescape(m.captured(2));
}

QString NotesStorage::withTitleMeta(QString fullHtml, const QString &title) {
    // Remove ALL existing occurrences first — this is what makes the
    // upsert idempotent (double-apply leaves exactly one tag).
    static const QRegularExpression anyTitleMeta(
        QStringLiteral("<meta\\s+name=\"notepatra-title\"[^>]*>\\n?"),
        QRegularExpression::CaseInsensitiveOption);
    fullHtml.remove(anyTitleMeta);

    if (title.trimmed().isEmpty())
        return fullHtml;          // never write an empty meta

    const QString tag = QStringLiteral("<meta name=\"notepatra-title\" content=\"")
                      + titleAttrEscape(title) + QStringLiteral("\">\n");

    // (a) after the first <head ...> ; (b) else before the first <body ;
    // (c) else prepend. Qt5's QTextDocument::toHtml() does emit a <head>,
    // but the fallbacks guard head-shape variance across point releases.
    static const QRegularExpression headOpenRe(
        QStringLiteral("<head[^>]*>"), QRegularExpression::CaseInsensitiveOption);
    static const QRegularExpression bodyOpenRe(
        QStringLiteral("<body"), QRegularExpression::CaseInsensitiveOption);

    const QRegularExpressionMatch headM = headOpenRe.match(fullHtml);
    if (headM.hasMatch()) {
        fullHtml.insert(headM.capturedEnd(), QStringLiteral("\n") + tag);
        return fullHtml;
    }
    const QRegularExpressionMatch bodyM = bodyOpenRe.match(fullHtml);
    if (bodyM.hasMatch()) {
        fullHtml.insert(bodyM.capturedStart(), tag);
        return fullHtml;
    }
    return tag + fullHtml;
}

QString NotesStorage::legacyH1In(const QString &html) {
    static const QRegularExpression h1Re(
        QStringLiteral("<h1\\b[^>]*>(.*?)</h1>"),
        QRegularExpression::CaseInsensitiveOption
            | QRegularExpression::DotMatchesEverythingOption);
    const QRegularExpressionMatch m = h1Re.match(html);
    if (!m.hasMatch()) return QString();
    QString inner = m.captured(1);
    static const QRegularExpression tagRe(QStringLiteral("<[^>]*>"));
    inner.replace(tagRe, QStringLiteral(" "));
    inner.replace(QLatin1String("&nbsp;"), QLatin1String(" "));
    inner = titleAttrUnescape(inner);
    return inner.simplified().left(200);
}

QString NotesStorage::prettyTitleFromFilename(const QString &absPath) {
    const QFileInfo fi(absPath);
    QString display = fi.completeBaseName();
    // Trash prefix FIRST so trashed notes prettify like live ones.
    static const QRegularExpression trashedRe(
        QStringLiteral("^\\.trashed-\\d+-"));
    display.remove(trashedRe);
    // Date+time prefix — 4 OR 6 digit time.
    static const QRegularExpression datePrefixRe(
        QStringLiteral("^\\d{4}-\\d{2}-\\d{2}-\\d{4,6}-"));
    display.remove(datePrefixRe);
    display.replace(QChar('-'), QChar(' '));
    if (display.isEmpty()) display = fi.completeBaseName();
    // "untitled meeting 01" elides before the counter shows — collapse
    // to "Untitled 01"; new notes use the "noter-NN" slug → "Noter NN".
    static const QRegularExpression untitledRe(
        QStringLiteral("^untitled meeting\\s*"));
    display.replace(untitledRe, QStringLiteral("Untitled "));
    static const QRegularExpression noterRe(QStringLiteral("^noter\\s*"));
    display.replace(noterRe, QStringLiteral("Noter "));
    return display;
}

NotesStorage::TitleInfo NotesStorage::titleInfoForFile(const QString &absPath) const {
    const QFileInfo fi(absPath);
    const qint64 mtimeMs = fi.lastModified().toMSecsSinceEpoch();
    const qint64 size    = fi.size();

    const auto it = m_titleCache.constFind(absPath);
    if (it != m_titleCache.constEnd() &&
        it->mtimeMs == mtimeMs && it->size == size)
        return it->info;

    TitleInfo info;
    QFile f(absPath);
    if (!f.open(QIODevice::ReadOnly)) {
        // Missing / unreadable → filename fallback. DO NOT cache, so a
        // later permission fix (or the file appearing) self-heals.
        info.display = prettyTitleFromFilename(absPath);
        return info;
    }
    // Cap the read at 512 KB — the template head is ≈30 KB; the cap only
    // guards a corrupt giant file from stalling the sidebar.
    const QByteArray bytes = f.read(512 * 1024);
    f.close();
    const QString raw = QString::fromUtf8(bytes);

    // legacyH1 is ALWAYS populated best-effort — the "Noter NN" counter
    // scan needs it even when the meta wins.
    info.legacyH1 = legacyH1In(raw);

    const QString meta = titleMetaIn(raw);
    if (!meta.isEmpty()) {
        info.display = meta;
    } else {
        // Legacy heuristic (display-only, NO file rewrite). Strip a
        // .trashed prefix so trashed legacy notes resolve like live ones.
        QString stem = fi.completeBaseName();
        static const QRegularExpression trashedRe(
            QStringLiteral("^\\.trashed-\\d+-"));
        stem.remove(trashedRe);
        static const QRegularExpression defaultShapeRe(QStringLiteral(
            "^\\d{4}-\\d{2}-\\d{2}-\\d{4,6}-(?:noter|untitled-meeting)-\\d+(?: \\(\\d+\\))?$"));
        static const QRegularExpression defaultH1Re(
            QStringLiteral("^(?:noter|untitled(?: meeting)?)\\s*\\d+$"),
            QRegularExpression::CaseInsensitiveOption);
        if (defaultShapeRe.match(stem).hasMatch() &&
            !info.legacyH1.isEmpty() &&
            !defaultH1Re.match(info.legacyH1).hasMatch()) {
            // Default filename + customized H1 + no meta: the user titled
            // the note in the body but never renamed the file. Show the H1
            // (the deliberate audit fix — pre-feature this wrongly showed
            // "Noter NN").
            info.display = info.legacyH1;
        } else {
            // Bit-for-bit today's label. A stale "Noter 01" H1 must NEVER
            // override the user's explicit sidebar rename.
            info.display = prettyTitleFromFilename(absPath);
        }
    }

    m_titleCache.insert(absPath, TitleCacheEnt{ mtimeMs, size, info });
    return info;
}

QString NotesStorage::displayTitleForFile(const QString &absPath) const {
    return titleInfoForFile(absPath).display;
}

void NotesStorage::invalidateTitleCache(const QString &absPath) {
    m_titleCache.remove(absPath);
}

// ═══════════════════════════════════════════════════════════════════════
// listAllNotes — recursive, mtime-desc, filters out sidecar files.
// ═══════════════════════════════════════════════════════════════════════

QStringList NotesStorage::listAllNotes() const {
    QStringList out;
    if (m_root.isEmpty() || !QDir(m_root).exists()) return out;

    // Regex matches ".bak1", ".bak42", ".tmp", ".draft", ".lock".
    static const QRegularExpression sidecarRe(
        QStringLiteral("\\.(bak\\d+|tmp|draft|lock)$"));

    QDirIterator it(m_root,
                    QStringList() << QStringLiteral("*.html"),
                    QDir::Files | QDir::NoSymLinks,
                    QDirIterator::Subdirectories);
    QVector<QPair<qint64, QString>> hits;
    while (it.hasNext()) {
        const QString path = it.next();
        if (sidecarRe.match(path).hasMatch()) continue;
        const QFileInfo fi(path);
        hits.push_back({ fi.lastModified().toMSecsSinceEpoch(), path });
    }
    std::sort(hits.begin(), hits.end(),
              [](const QPair<qint64, QString> &a, const QPair<qint64, QString> &b) {
                  return a.first > b.first;
              });
    for (const auto &p : hits) out.append(p.second);
    return out;
}
