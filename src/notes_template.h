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

    // Render a single tagged block as HTML to insert into <main>.
    // Type must be one of: "decision", "action", "question", "risk",
    // "quote", "text". Owner and due are only used for "action".
    // Caller is responsible for the data-id (use newBlockId()).
    QString renderBlock(const QString &type,
                        const QString &text,
                        const QString &timeHHMM,
                        const QString &owner = QString(),
                        const QString &due = QString());

    // Embed widgets — each returns a complete HTML snippet using the
    // CSS class names from design/notes-ux/index.html.
    QString renderCodeRefEmbed(const QString &filePath,
                               int lineStart,
                               int lineEnd,
                               const QStringList &previewLines);

    QString renderPrEmbed(const QString &url,
                          const QString &repo,
                          const QString &number,
                          const QString &title,
                          const QString &author,
                          const QString &status,
                          int plus, int minus,
                          int files,
                          const QString &createdAgo);

    QString renderVideoEmbed(const QString &url,
                             const QString &title,
                             const QString &host,        // "loom.com", "youtube.com"
                             const QString &durationMMSS);

    // Image embed: dataUri must be a complete `data:image/...;base64,...`
    // URL produced by the caller after sanitizing + downscaling.
    QString renderImageEmbed(const QString &dataUri,
                             const QString &filename,
                             int widthPx, int heightPx,
                             const QString &caption);

    QString renderDiffEmbed(const QString &filename,
                            const QString &unifiedDiffText);

    // Terminal log block — leading "$" prompt rendered, body monospace.
    QString renderTerminalEmbed(const QString &logText);

    // UUID v4 for data-id.
    QString newBlockId();

    // Escape user text for safe inclusion in HTML. Escapes &, <, >,
    // ", ' (entity-encoded). Does NOT touch already-encoded entities.
    QString escapeText(const QString &raw);
}

#endif // NOTES_TEMPLATE_H
