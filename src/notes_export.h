// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef NOTEPATRA_NOTES_EXPORT_H
#define NOTEPATRA_NOTES_EXPORT_H

// Noter PDF + Markdown export.
//
// PDF rendering uses the existing v0.1.90 QWebEngineView headless
// print path (same machinery as Vega chart PDF export), so the
// generated PDF looks pixel-identical to the in-app view. PDF export
// is only built when NOTEPATRA_WITH_WEBENGINE is defined — the lite
// build returns "WebEngine not available" via errorOut and is
// expected to nudge the user toward the Charts/Full pack.
//
// Markdown export is a minimal HTML→MD converter targeted at Noter's
// specific block shapes (decision / action / question / risk /
// quote / PR-embed / video-embed / image-embed / code-ref / inline
// formatting). It always builds — independent of WebEngine.

#include <QString>

class QWidget;

namespace NoterExport {

// Render the .html note at noteHtmlPath to a PDF at outputPdfPath.
// Returns true on success. On failure, *errorOut (if non-null) is
// populated with a one-line user-readable reason.
//
// Synchronous: spins a nested event loop while the WebEngine page
// loads + prints. Safe to call from the main thread.
bool exportPdf(const QString &noteHtmlPath,
               const QString &outputPdfPath,
               QString *errorOut = nullptr);

// Render the .html note at noteHtmlPath to a Markdown file at
// outputMdPath. Inline base64 images are extracted to a sidecar
// directory `<outputMdPath>.assets/` and referenced as
// `![alt](<assets-dir>/img-N.png)`. PR / video / code-ref embeds
// degrade to plain Markdown links with a small metadata table or
// italicised note.
bool exportMarkdown(const QString &noteHtmlPath,
                    const QString &outputMdPath,
                    QString *errorOut = nullptr);

// Internal helper exposed for unit tests — converts a self-contained
// HTML string (not a path) to Markdown text + writes any inline
// image assets relative to assetsDirAbs. Returns the Markdown.
// If assetsDirAbs is empty, base64 images become inline placeholders
// instead of sidecar files.
QString htmlToMarkdown(const QString &html,
                       const QString &assetsDirAbs,
                       const QString &assetsRelPrefix);

// Show a QFileDialog asking where to save. Defaults filename to
// "<note-stem>.{pdf|md}" and last-used directory if available.
// kind is "pdf" or "md". Returns empty on cancel.
QString chooseExportPath(QWidget *parent,
                         const QString &noteHtmlPath,
                         const QString &kind);

}  // namespace NoterExport

#endif
