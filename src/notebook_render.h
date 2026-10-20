// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef NOTEPATRA_NOTEBOOK_RENDER_H
#define NOTEPATRA_NOTEBOOK_RENDER_H

// Pure rendering helpers for the Preview pane: Markdown, Jupyter .ipynb and
// "-- %%"-separated SQL notebooks. No widgets here, so tests can link this
// file alone.

#include <QByteArray>
#include <QString>
#include <QVector>

namespace NotebookRender {

struct Colors {
    QString bg, fg, dimFg, codeBg, border, accent, linkFg;
};

// GitHub-dialect Markdown to HTML via QTextDocument (escaped <pre> on Qt < 5.14).
// Code is emitted as monospace <pre>/spans; `colors` (optional) tints code and links.
QString markdownToHtml(const QString &md, const Colors *colors = nullptr);

// Inner part of an HTML document's <body>, or the input when there is none.
QString htmlBodyContent(const QString &html);

QString escapeHtml(const QString &s);

// Removes ANSI CSI / OSC escape sequences (error tracebacks are coloured).
QString stripAnsi(const QString &s);

struct NotebookImage {
    QString name;        // "nb://img/N"
    QByteArray pngBytes; // decoded image/png payload
};

// nbformat 4 notebook to a read-only HTML view. Images are referenced as
// <img src="nb://img/N"> and returned in *images for QTextDocument::addResource.
// Returns "" and sets *err on invalid JSON or an unsupported nbformat.
QString ipynbToHtml(const QByteArray &json, const Colors &c,
                    QVector<NotebookImage> *images, QString *err);

struct SqlCell {
    int startLine = 0;   // 0-based; the "-- %%" separator line (0 for the leading cell)
    int endLine = 0;     // 0-based, inclusive
    QString text;        // cell body without the separator line, trimmed
    bool markdown = false;
};

// Splits on "-- %%" lines (VS Code / Jupytext). "-- %% [markdown]" and
// "-- %% md" start markdown cells. Empty cells are dropped.
QVector<SqlCell> splitSqlCells(const QString &sql);

// Index of the cell containing 0-based `line`, or -1.
int cellIndexForLine(const QVector<SqlCell> &cells, int line);

} // namespace NotebookRender

#endif // NOTEPATRA_NOTEBOOK_RENDER_H
