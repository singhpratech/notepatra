// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef NOTES_TEMPLATE_H
#define NOTES_TEMPLATE_H

#include <QString>
#include <QStringList>
#include <QDateTime>

// HTML render template for the "Notes" feature. Output of these
// functions is the actual .html content that gets written to disk —
// the user's saved meeting notes. The HTML is built to render pixel-
// identical to design/notes-ux/index.html (SURFACE 03 active-meeting
// hero) in any modern browser, with no runtime dependencies beyond
// the Google-Fonts <link> embedded in the document head.
namespace NotesTemplate {

    // The complete <style>...</style> block embedded in every saved
    // .html. Pulled verbatim from design/notes-ux/index.html so a
    // saved note opened in Chrome/Firefox/Safari renders pixel-
    // identical to the mockup.
    QString styleBlock();

    // Full HTML document for a new empty meeting note. Includes
    // <!doctype html>, schema meta header, embedded style, body
    // shell with empty <main contenteditable="true"> containing a
    // single placeholder paragraph block. Returned content is ready
    // to write to disk directly.
    QString shellHtml(const QString &title,
                      const QDateTime &start,
                      const QStringList &attendees);

    // Escape user text for safe inclusion in HTML. Escapes &, <, >,
    // ", ' (entity-encoded). Does NOT touch already-encoded entities.
    QString escapeText(const QString &raw);
}

#endif // NOTES_TEMPLATE_H
