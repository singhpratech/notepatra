// SPDX-License-Identifier: GPL-3.0-or-later

#include "notes_export.h"

#include <QByteArray>
#include <QDir>
#include <QEventLoop>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QPageLayout>
#include <QPageSize>
#include <QRegularExpression>
#include <QRegularExpressionMatch>
#include <QRegularExpressionMatchIterator>
#include <QStringList>
#include <QTextStream>
#include <QTimer>
#include <QUrl>
#include <QWidget>

#ifdef NOTEPATRA_WITH_WEBENGINE
#  include <QWebEnginePage>
#  include <QWebEngineView>
#endif

namespace NoterExport {

// ═══════════════════════════════════════════════════════════════════════
// PDF export — QWebEngineView headless print
// ═══════════════════════════════════════════════════════════════════════
//
// We construct a fresh QWebEngineView per call (not the editor's view)
// because the editor's view may have transparent background / chart
// renderers / cached state that pollutes the print. The view is invisible;
// only the page's printToPdf path is exercised.
//
// Synchronisation is the tricky bit: printToPdf is async (the signal
// pdfPrintingFinished fires when the file lands on disk), and the page's
// loadFinished must fire before we can print. We chain both via a nested
// event loop with a hard timeout so a hung load never deadlocks the UI.

bool exportPdf(const QString &noteHtmlPath,
               const QString &outputPdfPath,
               QString *errorOut) {
#ifndef NOTEPATRA_WITH_WEBENGINE
    Q_UNUSED(noteHtmlPath);
    Q_UNUSED(outputPdfPath);
    if (errorOut) {
        *errorOut = QStringLiteral(
            "PDF export requires the Full build (Qt WebEngine), available on "
            "Linux and Windows. macOS has no Qt5 WebEngine, so this WebEngine-"
            "based PDF export isn't available there.");
    }
    return false;
#else
    QFile in(noteHtmlPath);
    if (!in.exists()) {
        if (errorOut) *errorOut = QStringLiteral("Input note not found: %1").arg(noteHtmlPath);
        return false;
    }
    if (!in.open(QIODevice::ReadOnly)) {
        if (errorOut) *errorOut = QStringLiteral("Could not open input: %1").arg(in.errorString());
        return false;
    }
    const QByteArray html = in.readAll();
    in.close();

    // The QWebEngineView is parented to nothing — we delete it after the
    // event loop exits. Heap-allocate so deletion is explicit (stack-
    // allocated QWebEngineView with no QApplication parent can crash on
    // some Linux distros during destructor shutdown).
    auto *view = new QWebEngineView();
    view->setAttribute(Qt::WA_DontShowOnScreen, true);
    view->resize(800, 1000);

    QEventLoop loop;
    bool ok = false;
    QString errMsg;

    // The output path's parent must exist; QWebEnginePage::printToPdf
    // doesn't mkpath on its own.
    QDir().mkpath(QFileInfo(outputPdfPath).absolutePath());

    auto *page = view->page();

    QPageLayout layout(QPageSize(QPageSize::A4),
                       QPageLayout::Portrait,
                       QMarginsF(18, 18, 18, 18));  // 18 mm margins all round

    QObject::connect(view, &QWebEngineView::loadFinished,
                     view, [page, outputPdfPath, layout, &errMsg](bool loadOk) {
        if (!loadOk) {
            errMsg = QStringLiteral("WebEngine failed to load note HTML");
            return;
        }
        page->printToPdf(outputPdfPath, layout);
    });

    QObject::connect(page, &QWebEnginePage::pdfPrintingFinished,
                     view, [&loop, &ok, &errMsg](const QString &filePath, bool success) {
        Q_UNUSED(filePath);
        ok = success;
        if (!success) errMsg = QStringLiteral("WebEngine printToPdf reported failure");
        loop.quit();
    });

    // Hard timeout: 30 s. Without this, a WebEngine page that never
    // emits loadFinished would freeze the main thread forever.
    QTimer timeout;
    timeout.setSingleShot(true);
    QObject::connect(&timeout, &QTimer::timeout, &loop, [&loop, &errMsg]() {
        errMsg = QStringLiteral("PDF export timed out after 30 s");
        loop.quit();
    });
    timeout.start(30000);

    // Base URL "" means relative refs in the HTML resolve to nothing —
    // which is exactly what we want for self-contained Noter notes.
    view->setHtml(QString::fromUtf8(html));

    loop.exec();
    view->deleteLater();

    if (!ok && errorOut) *errorOut = errMsg;
    // Final sanity check — file exists + non-empty.
    if (ok) {
        QFileInfo fi(outputPdfPath);
        if (!fi.exists() || fi.size() == 0) {
            ok = false;
            if (errorOut)
                *errorOut = QStringLiteral("PDF file did not land on disk");
        }
    }
    return ok;
#endif
}

// ═══════════════════════════════════════════════════════════════════════
// Markdown export — HTML → MD with Noter block awareness
// ═══════════════════════════════════════════════════════════════════════

namespace {

// Decode the entities Noter / Qt rich-text emit. NOT general-purpose —
// just the handful that show up in real notes.
QString decodeEntities(QString s) {
    s.replace("&amp;",  "&");
    s.replace("&lt;",   "<");
    s.replace("&gt;",   ">");
    s.replace("&quot;", "\"");
    s.replace("&apos;", "'");
    s.replace("&#39;",  "'");
    s.replace("&nbsp;", " ");
    return s;
}

// Strip every tag from a snippet and collapse whitespace.
QString stripInlineTags(QString s) {
    // Preserve <strong>, <em>, <code>, <a> first as Markdown.
    static const QRegularExpression strongRe(
        "<(?:strong|b)\\b[^>]*>(.*?)</(?:strong|b)>",
        QRegularExpression::CaseInsensitiveOption | QRegularExpression::DotMatchesEverythingOption);
    static const QRegularExpression emRe(
        "<(?:em|i)\\b[^>]*>(.*?)</(?:em|i)>",
        QRegularExpression::CaseInsensitiveOption | QRegularExpression::DotMatchesEverythingOption);
    static const QRegularExpression codeRe(
        "<code\\b[^>]*>(.*?)</code>",
        QRegularExpression::CaseInsensitiveOption | QRegularExpression::DotMatchesEverythingOption);
    static const QRegularExpression linkRe(
        "<a\\b[^>]*href=\"([^\"]*)\"[^>]*>(.*?)</a>",
        QRegularExpression::CaseInsensitiveOption | QRegularExpression::DotMatchesEverythingOption);

    s.replace(strongRe, "**\\1**");
    s.replace(emRe,     "*\\1*");
    s.replace(codeRe,   "`\\1`");
    s.replace(linkRe,   "[\\2](\\1)");

    s.replace(QRegularExpression("<[^>]+>"), "");
    s = decodeEntities(s);
    s.replace(QRegularExpression("[ \\t]+"), " ");
    return s.trimmed();
}

// Extract the value of one HTML attribute. Returns empty if absent.
QString attr(const QString &tagOpen, const QString &name) {
    QRegularExpression re(name + "=\"([^\"]*)\"",
                          QRegularExpression::CaseInsensitiveOption);
    const auto m = re.match(tagOpen);
    return m.hasMatch() ? m.captured(1) : QString();
}

// Whole-token class match — splits the class attribute on whitespace
// and compares token-by-token, so "b-q" does NOT match "b-quote" and
// "emb" does NOT match "emb-pr". Plain QString::contains() would
// false-match those, which v0.1.93 caught in the b-quote regression.
bool hasClass(const QString &classAttr, const char *tok) {
    const QString needle = QString::fromLatin1(tok);
    const auto tokens = classAttr.split(QRegularExpression("\\s+"),
                                        Qt::SkipEmptyParts);
    for (const QString &t : tokens) {
        if (t == needle) return true;
    }
    return false;
}

// Pull the inner HTML between matching open/close <div> ... </div>
// pairs. We allow nested divs by counting.
QString innerHtml(const QString &html, int openTagEnd) {
    int depth = 1;
    int i = openTagEnd;
    while (i < html.size()) {
        const int nextOpen  = html.indexOf("<div",  i, Qt::CaseInsensitive);
        const int nextClose = html.indexOf("</div>", i, Qt::CaseInsensitive);
        if (nextClose < 0) return html.mid(openTagEnd);
        if (nextOpen >= 0 && nextOpen < nextClose) {
            // Skip past the > of the new open tag.
            const int gt = html.indexOf('>', nextOpen);
            if (gt < 0) return html.mid(openTagEnd);
            depth++;
            i = gt + 1;
        } else {
            depth--;
            if (depth == 0) return html.mid(openTagEnd, nextClose - openTagEnd);
            i = nextClose + 6;
        }
    }
    return html.mid(openTagEnd);
}

}  // namespace

QString htmlToMarkdown(const QString &html,
                       const QString &assetsDirAbs,
                       const QString &assetsRelPrefix) {
    QString out;
    int imgCounter = 0;

    // We scan token-by-token. The cursor advances through the HTML;
    // each iteration finds the next "<div" / "<h1..h6" / "<p" / "<a"
    // / "<img" or any tag, decides what to emit, and advances.
    int i = 0;
    while (i < html.size()) {
        const int lt = html.indexOf('<', i);
        if (lt < 0) {
            out += stripInlineTags(html.mid(i));
            break;
        }
        // Plain text between tags.
        if (lt > i) {
            const QString chunk = stripInlineTags(html.mid(i, lt - i));
            if (!chunk.isEmpty()) out += chunk;
        }
        const int gt = html.indexOf('>', lt);
        if (gt < 0) break;
        const QString tagOpen = html.mid(lt, gt - lt + 1);
        const QString lower   = tagOpen.toLower();

        auto consumeTo = [&](const QString &closeTag) {
            const int closeAt = html.indexOf(closeTag, gt + 1, Qt::CaseInsensitive);
            if (closeAt < 0) {
                const QString body = html.mid(gt + 1);
                i = html.size();
                return body;
            }
            const QString body = html.mid(gt + 1, closeAt - (gt + 1));
            i = closeAt + closeTag.size();
            return body;
        };

        // ── Headings ──
        if (lower.startsWith("<h1")) {
            out += "\n# "  + stripInlineTags(consumeTo("</h1>")) + "\n\n";
            continue;
        }
        if (lower.startsWith("<h2")) {
            out += "\n## " + stripInlineTags(consumeTo("</h2>")) + "\n\n";
            continue;
        }
        if (lower.startsWith("<h3")) {
            out += "\n### " + stripInlineTags(consumeTo("</h3>")) + "\n\n";
            continue;
        }
        if (lower.startsWith("<h4")) {
            out += "\n#### " + stripInlineTags(consumeTo("</h4>")) + "\n\n";
            continue;
        }

        if (lower.startsWith("<br")) {
            out += "\n";
            i = gt + 1;
            continue;
        }

        if (lower.startsWith("<hr")) {
            out += "\n---\n";
            i = gt + 1;
            continue;
        }

        // ── Image (inline or sidecar) ──
        // We handle bare <img src="..."> here. Noter embed-image blocks
        // also use <img> inside <div class="emb-img">, but the outer
        // div handler below intercepts those first.
        if (lower.startsWith("<img")) {
            const QString src = attr(tagOpen, "src");
            const QString alt = attr(tagOpen, "alt");
            QString useSrc = src;
            if (src.startsWith("data:image/") && !assetsDirAbs.isEmpty()) {
                const int comma = src.indexOf(',');
                if (comma > 0) {
                    QString mime = src.mid(5, src.indexOf(';') - 5);
                    if (mime.isEmpty()) mime = "image/png";
                    QString ext = mime.section('/', 1, 1);
                    if (ext.isEmpty()) ext = "png";
                    const QByteArray bytes = QByteArray::fromBase64(
                        src.mid(comma + 1).toLatin1());
                    QDir().mkpath(assetsDirAbs);
                    const QString fname = QString("img-%1.%2")
                                              .arg(++imgCounter).arg(ext);
                    QFile f(assetsDirAbs + "/" + fname);
                    if (f.open(QIODevice::WriteOnly)) {
                        f.write(bytes);
                        f.close();
                        useSrc = assetsRelPrefix.isEmpty()
                                     ? fname
                                     : (assetsRelPrefix + "/" + fname);
                    }
                }
            }
            out += QString("![%1](%2)\n").arg(alt, useSrc);
            i = gt + 1;
            continue;
        }

        // ── Noter-typed div blocks ──
        if (lower.startsWith("<div")) {
            const QString cls = attr(tagOpen, "class");
            const QString body = innerHtml(html, gt + 1);
            // Advance i to past the matching </div>.
            // innerHtml returns the inner text; locate its end.
            // We re-find by scanning depth from gt+1.
            {
                int depth = 1;
                int j = gt + 1;
                while (j < html.size() && depth > 0) {
                    const int nO = html.indexOf("<div", j, Qt::CaseInsensitive);
                    const int nC = html.indexOf("</div>", j, Qt::CaseInsensitive);
                    if (nC < 0) { j = html.size(); break; }
                    if (nO >= 0 && nO < nC) {
                        const int gt2 = html.indexOf('>', nO);
                        if (gt2 < 0) { j = html.size(); break; }
                        depth++;
                        j = gt2 + 1;
                    } else {
                        depth--;
                        j = nC + 6;
                    }
                }
                i = j;
            }

            auto emit_typed = [&](const QString &tag) {
                out += "**" + tag + ":** " + stripInlineTags(body) + "\n\n";
            };

            // ── b-quote MUST come before b-q — bare-substring matching
            //    would treat the longer token as the shorter one. We
            //    use hasClass() for whole-token semantics so the order
            //    is purely defensive, not load-bearing.
            if (hasClass(cls, "b-dec"))   { emit_typed("DECISION"); continue; }
            if (hasClass(cls, "b-quote")) {
                const QString attribution = attr(tagOpen, "data-attribution");
                out += "> " + stripInlineTags(body);
                if (!attribution.isEmpty()) out += "  — " + attribution;
                out += "\n\n";
                continue;
            }
            if (hasClass(cls, "b-q"))     { emit_typed("QUESTION"); continue; }
            if (hasClass(cls, "b-risk"))  { emit_typed("RISK");     continue; }

            if (hasClass(cls, "b-act")) {
                const QString owner = attr(tagOpen, "data-owner");
                const QString due   = attr(tagOpen, "data-due");
                QString line = "- [ ] " + stripInlineTags(body);
                QStringList meta;
                if (!owner.isEmpty()) meta << ("owner: " + owner);
                if (!due.isEmpty())   meta << ("due: "   + due);
                if (!meta.isEmpty()) line += " (" + meta.join(", ") + ")";
                out += line + "\n";
                continue;
            }

            if (hasClass(cls, "emb-pr")) {
                const QString url   = attr(tagOpen, "data-url");
                const QString num   = attr(tagOpen, "data-number");
                const QString title = attr(tagOpen, "data-title");
                const QString aut   = attr(tagOpen, "data-author");
                const QString plus  = attr(tagOpen, "data-additions");
                const QString minus = attr(tagOpen, "data-deletions");
                out += QString("[PR #%1: %2](%3)").arg(num, title, url);
                QStringList meta;
                if (!aut.isEmpty()) meta << aut;
                if (!plus.isEmpty() || !minus.isEmpty()) {
                    meta << QString("+%1/-%2").arg(plus.isEmpty() ? "0" : plus,
                                                   minus.isEmpty() ? "0" : minus);
                }
                if (!meta.isEmpty()) out += " — " + meta.join(", ");
                out += "\n";
                continue;
            }

            if (hasClass(cls, "emb-vid")) {
                const QString url     = attr(tagOpen, "data-url");
                const QString title   = attr(tagOpen, "data-title");
                const QString host    = attr(tagOpen, "data-host");
                const QString duration= attr(tagOpen, "data-duration");
                out += QString("[Video: %1](%2)").arg(title, url);
                QStringList meta;
                if (!host.isEmpty()) meta << host;
                if (!duration.isEmpty()) meta << duration;
                if (!meta.isEmpty()) out += " — " + meta.join(" · ");
                out += "\n";
                continue;
            }

            if (hasClass(cls, "emb-img")) {
                // Recurse so the inner <img> path is exercised; that
                // emits the sidecar-decoded link.
                out += htmlToMarkdown(body, assetsDirAbs, assetsRelPrefix);
                const QString caption = attr(tagOpen, "data-caption");
                if (!caption.isEmpty()) out += "*" + caption + "*\n";
                continue;
            }

            if (hasClass(cls, "emb")) {
                // Generic code-ref embed: code body + file path.
                const QString path = attr(tagOpen, "data-path");
                const QString lang = attr(tagOpen, "data-lang");
                out += "```" + lang + "\n";
                if (!path.isEmpty()) out += "// " + path + "\n";
                out += decodeEntities(stripInlineTags(body)) + "\n";
                out += "```\n";
                continue;
            }

            // ── Untyped div: recurse so its children are flattened. ──
            out += htmlToMarkdown(body, assetsDirAbs, assetsRelPrefix);
            continue;
        }

        // ── Paragraph ──
        if (lower.startsWith("<p")) {
            const QString body = stripInlineTags(consumeTo("</p>"));
            if (!body.isEmpty()) out += body + "\n\n";
            continue;
        }

        if (lower.startsWith("<li")) {
            const QString body = stripInlineTags(consumeTo("</li>"));
            out += "- " + body + "\n";
            continue;
        }

        if (lower.startsWith("<ul") || lower.startsWith("</ul")
            || lower.startsWith("<ol") || lower.startsWith("</ol")) {
            i = gt + 1;
            continue;
        }

        // ── Anchor not inside another typed block (rare in Noter) ──
        if (lower.startsWith("<a")) {
            const QString href = attr(tagOpen, "href");
            const QString body = stripInlineTags(consumeTo("</a>"));
            out += "[" + body + "](" + href + ")";
            continue;
        }

        // ── Inline formatting + everything else: defer to stripInlineTags. ──
        // We do this by finding the matching close tag and treating
        // the contents as inline.
        // For tags we don't specially recognise, just skip the opening
        // tag and let the next loop iteration handle text.
        i = gt + 1;
    }

    // Collapse 3+ consecutive newlines down to a double newline.
    out.replace(QRegularExpression("\n{3,}"), "\n\n");
    return out.trimmed() + "\n";
}

bool exportMarkdown(const QString &noteHtmlPath,
                    const QString &outputMdPath,
                    QString *errorOut) {
    QFile in(noteHtmlPath);
    if (!in.exists()) {
        if (errorOut) *errorOut = QStringLiteral("Input note not found: %1").arg(noteHtmlPath);
        return false;
    }
    if (!in.open(QIODevice::ReadOnly)) {
        if (errorOut) *errorOut = QStringLiteral("Could not open input: %1").arg(in.errorString());
        return false;
    }
    const QString html = QString::fromUtf8(in.readAll());
    in.close();

    QFileInfo fi(outputMdPath);
    const QString assetsDirAbs = outputMdPath + ".assets";
    const QString assetsRelPrefix =
        QFileInfo(assetsDirAbs).fileName();
    const QString md = htmlToMarkdown(html, assetsDirAbs, assetsRelPrefix);

    QDir().mkpath(fi.absolutePath());
    QFile out(outputMdPath);
    if (!out.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        if (errorOut) *errorOut = QStringLiteral("Could not write: %1").arg(out.errorString());
        return false;
    }
    {
        QTextStream s(&out);
        s.setCodec("UTF-8");
        s << md;
    }
    out.close();
    return true;
}

// ═══════════════════════════════════════════════════════════════════════
// chooseExportPath — wrapper around QFileDialog::getSaveFileName
// ═══════════════════════════════════════════════════════════════════════

QString chooseExportPath(QWidget *parent,
                         const QString &noteHtmlPath,
                         const QString &kind) {
    const QString stem = QFileInfo(noteHtmlPath).completeBaseName();
    const QString isPdf = kind.toLower() == "pdf" ? "pdf" : "md";
    const QString defaultName = stem + "." + isPdf;
    const QString defaultDir =
        QFileInfo(noteHtmlPath).absolutePath();

    const QString filter = isPdf == "pdf"
        ? QObject::tr("PDF document (*.pdf)")
        : QObject::tr("Markdown (*.md)");

    QFileDialog dlg(parent, QObject::tr("Export note"));
    dlg.setAcceptMode(QFileDialog::AcceptSave);
    dlg.setDirectory(defaultDir);
    dlg.selectFile(defaultName);
    dlg.setNameFilter(filter);
    dlg.selectNameFilter(filter);
    dlg.setDefaultSuffix(isPdf);
    if (dlg.exec() != QDialog::Accepted) return QString();
    const QStringList sel = dlg.selectedFiles();
    return sel.isEmpty() ? QString() : sel.first();
}

}  // namespace NoterExport
