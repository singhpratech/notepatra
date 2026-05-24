// SPDX-License-Identifier: GPL-3.0-or-later

#include "notes_template.h"

#include <QChar>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QStringList>
#include <QUuid>

namespace NotesTemplate {

// ─────────────────────────────────────────────────────────────────
// Internal helpers
// ─────────────────────────────────────────────────────────────────

namespace {

    // Map block type → CSS class suffix on .b (e.g. "decision" → "b-dec").
    QString blockClassFor(const QString &type) {
        if (type == QStringLiteral("decision"))  return QStringLiteral("b-dec");
        if (type == QStringLiteral("action"))    return QStringLiteral("b-act");
        if (type == QStringLiteral("question"))  return QStringLiteral("b-q");
        if (type == QStringLiteral("risk"))      return QStringLiteral("b-risk");
        if (type == QStringLiteral("quote"))     return QStringLiteral("b-quote");
        if (type == QStringLiteral("checklist")) return QStringLiteral("b-chk");
        return QString();
    }

    // Map block type → glyph rendered in .b-icn.
    QString blockGlyphFor(const QString &type) {
        if (type == QStringLiteral("decision"))  return QStringLiteral("\xE2\x97\x86"); // ◆
        if (type == QStringLiteral("action"))    return QStringLiteral("\xE2\x86\x92"); // →
        if (type == QStringLiteral("question"))  return QStringLiteral("?");
        if (type == QStringLiteral("risk"))      return QStringLiteral("\xE2\x9A\xA0"); // ⚠
        if (type == QStringLiteral("quote"))     return QStringLiteral("\"");
        if (type == QStringLiteral("checklist")) return QStringLiteral("\xE2\x98\x90"); // ☐
        return QString();
    }

    // Best-effort lightweight syntax pass for unified diff lines.
    // Routes each line to .add / .del / .ctx span based on leading char.
    QString renderUnifiedDiffBody(const QString &unifiedDiffText) {
        QString body;
        body.reserve(unifiedDiffText.size() + 64);
        const QStringList lines = unifiedDiffText.split(QChar('\n'));
        for (int i = 0; i < lines.size(); ++i) {
            const QString &line = lines.at(i);
            QString cls = QStringLiteral("ctx");
            if (line.startsWith(QChar('+')) && !line.startsWith(QStringLiteral("+++"))) {
                cls = QStringLiteral("add");
            } else if (line.startsWith(QChar('-')) && !line.startsWith(QStringLiteral("---"))) {
                cls = QStringLiteral("del");
            }
            body += QStringLiteral("<span class=\"row %1\">").arg(cls)
                  + escapeText(line)
                  + QStringLiteral("</span>");
            if (i + 1 < lines.size()) body += QChar('\n');
        }
        return body;
    }

}

// ─────────────────────────────────────────────────────────────────
// Public API
// ─────────────────────────────────────────────────────────────────

QString newBlockId() {
    return QUuid::createUuid().toString(QUuid::WithoutBraces);
}

QString escapeText(const QString &raw) {
    QString out;
    out.reserve(raw.size() + 16);
    for (QChar c : raw) {
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

// ─────────────────────────────────────────────────────────────────
// shellHtml
// ─────────────────────────────────────────────────────────────────

QString shellHtml(const QString &title,
                  const QDateTime &start,
                  const QStringList &attendees) {
    // Build the meeting JSON meta payload.
    QJsonObject meta;
    meta.insert(QStringLiteral("title"), title);
    meta.insert(QStringLiteral("start"), start.toString(Qt::ISODate));
    QJsonArray attArr;
    for (const QString &a : attendees) attArr.append(a);
    meta.insert(QStringLiteral("attendees"), attArr);
    const QString metaJson = QString::fromUtf8(
        QJsonDocument(meta).toJson(QJsonDocument::Compact));

    // Format the meeting header pieces (ISO date · HH:MM · @attendees).
    const QString dateStr = start.date().toString(Qt::ISODate);
    const QString timeStr = start.time().toString(QStringLiteral("HH:mm"));

    QStringList attendeeSpans;
    for (const QString &a : attendees) {
        QString s = a;
        if (!s.startsWith(QChar('@'))) s.prepend(QChar('@'));
        attendeeSpans << escapeText(s);
    }
    const QString attendeeLine = attendeeSpans.join(
        QStringLiteral(" <span class=\"meet-meta-sep\">·</span> "));

    QString html;
    html.reserve(64 * 1024);
    html += QStringLiteral("<!DOCTYPE html>\n");
    html += QStringLiteral("<html lang=\"en\">\n");
    html += QStringLiteral("<head>\n");
    html += QStringLiteral("<meta charset=\"UTF-8\">\n");
    html += QStringLiteral("<meta name=\"viewport\" content=\"width=device-width, initial-scale=1\">\n");
    html += QStringLiteral("<meta name=\"notepatra-schema\" content=\"1\">\n");
    html += QStringLiteral("<meta name=\"notepatra-meeting\" content='")
          + escapeText(metaJson)
          + QStringLiteral("'>\n");
    html += QStringLiteral("<title>") + escapeText(title) + QStringLiteral("</title>\n");
    // BLOCKER FIX #6 — no external fetch. Notes used to phone Google
    // Fonts on every load (privacy violation against Notepatra's
    // local-first thesis). Fall back to the system serif/sans/mono
    // stacks already in the CSS variables — slightly less polished
    // typography but zero network requests.
    html += QStringLiteral("<style>\n");
    html += styleBlock();
    html += QStringLiteral("\n</style>\n");
    html += QStringLiteral("</head>\n");
    html += QStringLiteral("<body data-theme=\"light\">\n");
    html += QStringLiteral("  <div class=\"wrap\">\n");
    html += QStringLiteral("    <article class=\"mock\" data-theme=\"light\">\n");
    html += QStringLiteral("      <div class=\"notes-tab\" style=\"grid-template-columns: 1fr;\">\n");
    html += QStringLiteral("        <div class=\"canvas\">\n");
    html += QStringLiteral("          <div class=\"timer\"><span class=\"dot\"></span>00:00</div>\n");
    html += QStringLiteral("          <div class=\"meet-head\">\n");
    html += QStringLiteral("            <h1 class=\"meet-title\">") + escapeText(title) + QStringLiteral("</h1>\n");
    html += QStringLiteral("            <div class=\"meet-meta\">\n");
    html += QStringLiteral("              <span>") + escapeText(dateStr) + QStringLiteral("</span><span class=\"meet-meta-sep\">·</span>\n");
    html += QStringLiteral("              <span>") + escapeText(timeStr) + QStringLiteral("</span><span class=\"meet-meta-sep\">·</span>\n");
    html += QStringLiteral("              <span>") + attendeeLine + QStringLiteral("</span>\n");
    html += QStringLiteral("            </div>\n");
    html += QStringLiteral("          </div>\n");
    html += QStringLiteral("          <div class=\"meet-divider\"></div>\n");
    // contenteditable target — defaultParagraphSeparator is set to "p"
    // by the editor-boot script so Enter produces <p>, not <div>.
    html += QStringLiteral("          <main class=\"notes-body\" contenteditable=\"true\">\n");
    // Truly empty <p> — no <br> placeholder. The CSS pseudo-element
    // p.p:empty::before renders the "Start typing..." hint until the
    // user types. With a <br> child here, Chromium's contenteditable
    // creates a new <p> per keystroke (each char on its own line).
    html += QStringLiteral("            <p class=\"p\" data-id=\"") + newBlockId() + QStringLiteral("\"></p>\n");
    html += QStringLiteral("          </main>\n");
    html += QStringLiteral("        </div>\n");
    html += QStringLiteral("      </div>\n");
    html += QStringLiteral("    </article>\n");
    html += QStringLiteral("  </div>\n");
    html += QStringLiteral("</body>\n");
    html += QStringLiteral("</html>\n");
    return html;
}

// ─────────────────────────────────────────────────────────────────
// renderBlock
// ─────────────────────────────────────────────────────────────────

QString renderBlock(const QString &type,
                    const QString &text,
                    const QString &timeHHMM,
                    const QString &owner,
                    const QString &due) {
    const QString id = newBlockId();
    const QString escText = escapeText(text);
    const QString escTime = escapeText(timeHHMM);

    if (type == QStringLiteral("text") || type.isEmpty()) {
        return QStringLiteral("<p class=\"p\" data-id=\"%1\">%2</p>\n")
            .arg(id, escText);
    }

    const QString cls   = blockClassFor(type);
    const QString glyph = blockGlyphFor(type);

    QString html;
    html.reserve(text.size() + 256);

    if (type == QStringLiteral("action")) {
        const QString escOwner = escapeText(owner);
        const QString escDue   = escapeText(due);
        html += QStringLiteral(
            "<div class=\"b %1\" data-id=\"%2\" data-type=\"%3\" data-time=\"%4\" data-owner=\"%5\" data-due=\"%6\">\n")
            .arg(cls, id, escapeText(type), escTime, escOwner, escDue);
        html += QStringLiteral("  <div class=\"b-icn\">") + glyph + QStringLiteral("</div>\n");
        html += QStringLiteral("  <div class=\"b-body\">\n");
        html += QStringLiteral("    <span class=\"b-lbl\">") + escapeText(type) + QStringLiteral("</span>")
              + escText
              + QStringLiteral("\n");
        if (!owner.isEmpty() || !due.isEmpty()) {
            if (!owner.isEmpty()) {
                html += QStringLiteral("    <span class=\"chip chip-owner\">") + escOwner + QStringLiteral("</span>");
            }
            if (!due.isEmpty()) {
                html += QStringLiteral("<span class=\"chip chip-due\">") + escDue + QStringLiteral("</span>");
            }
            html += QStringLiteral("\n");
        }
        html += QStringLiteral("    <span class=\"b-time\">") + escTime + QStringLiteral("</span>\n");
        html += QStringLiteral("  </div>\n");
        html += QStringLiteral("</div>\n");
        return html;
    }

    if (type == QStringLiteral("quote")) {
        html += QStringLiteral("<div class=\"b %1\" data-id=\"%2\" data-type=\"%3\" data-time=\"%4\">\n")
            .arg(cls, id, escapeText(type), escTime);
        html += QStringLiteral("  <div class=\"b-icn\">") + glyph + QStringLiteral("</div>\n");
        html += QStringLiteral("  <div class=\"b-body\">\n");
        html += QStringLiteral("    ") + escText + QStringLiteral("\n");
        html += QStringLiteral("    <span class=\"attrib\">\xE2\x80\x94 ") + escTime + QStringLiteral("</span>\n");
        html += QStringLiteral("  </div>\n");
        html += QStringLiteral("</div>\n");
        return html;
    }

    // decision · question · risk — all share the labelled inline layout.
    html += QStringLiteral("<div class=\"b %1\" data-id=\"%2\" data-type=\"%3\" data-time=\"%4\">\n")
        .arg(cls, id, escapeText(type), escTime);
    html += QStringLiteral("  <div class=\"b-icn\">") + glyph + QStringLiteral("</div>\n");
    html += QStringLiteral("  <div class=\"b-body\">\n");
    html += QStringLiteral("    <span class=\"b-lbl\">") + escapeText(type) + QStringLiteral("</span>")
          + escText
          + QStringLiteral("<span class=\"b-time\">") + escTime + QStringLiteral("</span>\n");
    html += QStringLiteral("  </div>\n");
    html += QStringLiteral("</div>\n");
    return html;
}

// ─────────────────────────────────────────────────────────────────
// Embed renderers
// ─────────────────────────────────────────────────────────────────

QString renderCodeRefEmbed(const QString &filePath,
                           int lineStart,
                           int lineEnd,
                           const QStringList &previewLines) {
    QString html;
    html.reserve(256 + previewLines.size() * 80);
    html += QStringLiteral("<div class=\"emb\">\n");
    html += QStringLiteral("  <div class=\"emb-head\">\n");
    html += QStringLiteral("    <span class=\"lead\">code</span>\n");
    html += QStringLiteral("    <span class=\"path\">") + escapeText(filePath) + QStringLiteral("</span>\n");
    const QString range = (lineEnd > lineStart)
        ? QStringLiteral("L%1\xE2\x80\x93%2").arg(lineStart).arg(lineEnd)
        : QStringLiteral("L%1").arg(lineStart);
    html += QStringLiteral("    <span class=\"meta\">") + range + QStringLiteral("</span>\n");
    html += QStringLiteral("    <span class=\"ext\">open in editor \xE2\x86\x97</span>\n");
    html += QStringLiteral("  </div>\n");
    html += QStringLiteral("  <div class=\"emb-code\">\n");
    int n = lineStart;
    for (const QString &line : previewLines) {
        html += QStringLiteral("    <span class=\"row\"><span class=\"ln\">")
              + QString::number(n++)
              + QStringLiteral("</span>")
              + escapeText(line)
              + QStringLiteral("</span>\n");
    }
    html += QStringLiteral("  </div>\n");
    html += QStringLiteral("</div>\n");
    return html;
}

QString renderPrEmbed(const QString &url,
                      const QString &repo,
                      const QString &number,
                      const QString &title,
                      const QString &author,
                      const QString &status,
                      int plus, int minus,
                      int files,
                      const QString &createdAgo) {
    QString prNum = number;
    if (!prNum.startsWith(QChar('#'))) prNum.prepend(QChar('#'));
    QString prAuthor = author;
    if (!prAuthor.isEmpty() && !prAuthor.startsWith(QChar('@'))) prAuthor.prepend(QChar('@'));

    QString html;
    html.reserve(512);
    html += QStringLiteral("<div class=\"emb-pr\" data-url=\"") + escapeText(url) + QStringLiteral("\">\n");
    html += QStringLiteral("  <div class=\"pr-head\">\n");
    html += QStringLiteral("    <span class=\"pr-num\">") + escapeText(prNum) + QStringLiteral("</span>\n");
    html += QStringLiteral("    <span class=\"pr-repo\">") + escapeText(repo) + QStringLiteral("</span>\n");
    html += QStringLiteral("    <span class=\"pr-status\">") + escapeText(status) + QStringLiteral("</span>\n");
    html += QStringLiteral("  </div>\n");
    html += QStringLiteral("  <div class=\"pr-title\">") + escapeText(title) + QStringLiteral("</div>\n");
    html += QStringLiteral("  <div class=\"pr-foot\">\n");
    html += QStringLiteral("    <span class=\"author\">") + escapeText(prAuthor) + QStringLiteral("</span>\n");
    html += QStringLiteral("    <span>") + escapeText(createdAgo) + QStringLiteral("</span>\n");
    html += QStringLiteral("    <span>") + QString::number(files) + QStringLiteral(" files</span>\n");
    html += QStringLiteral("    <span><span class=\"plus\">+") + QString::number(plus)
          + QStringLiteral("</span> <span class=\"minus\">\xE2\x88\x92") + QString::number(minus)
          + QStringLiteral("</span></span>\n");
    html += QStringLiteral("  </div>\n");
    html += QStringLiteral("</div>\n");
    return html;
}

QString renderVideoEmbed(const QString &url,
                         const QString &title,
                         const QString &host,
                         const QString &durationMMSS) {
    QString html;
    html.reserve(384);
    html += QStringLiteral("<div class=\"emb-vid\" data-url=\"") + escapeText(url) + QStringLiteral("\">\n");
    html += QStringLiteral("  <div class=\"vid-thumb\">\n");
    html += QStringLiteral("    <div class=\"vid-play\"></div>\n");
    html += QStringLiteral("    <span class=\"vid-thumb-dur\">") + escapeText(durationMMSS) + QStringLiteral("</span>\n");
    html += QStringLiteral("  </div>\n");
    html += QStringLiteral("  <div class=\"vid-meta\">\n");
    html += QStringLiteral("    <div class=\"vid-title\">") + escapeText(title) + QStringLiteral("</div>\n");
    html += QStringLiteral("    <div class=\"vid-host\">") + escapeText(host) + QStringLiteral("</div>\n");
    html += QStringLiteral("  </div>\n");
    html += QStringLiteral("</div>\n");
    return html;
}

QString renderImageEmbed(const QString &dataUri,
                         const QString &filename,
                         int widthPx, int heightPx,
                         const QString &caption) {
    const QString meta = QStringLiteral("%1\xC3\x97%2").arg(widthPx).arg(heightPx);

    QString html;
    html.reserve(dataUri.size() + 512);
    html += QStringLiteral("<div class=\"emb-img\">\n");
    html += QStringLiteral("  <div class=\"emb-img-head\">\n");
    html += QStringLiteral("    <span class=\"lead\">img</span>\n");
    html += QStringLiteral("    <span class=\"path\">") + escapeText(filename) + QStringLiteral("</span>\n");
    html += QStringLiteral("    <span class=\"meta\">") + meta + QStringLiteral("</span>\n");
    html += QStringLiteral("  </div>\n");
    html += QStringLiteral("  <div class=\"emb-img-figure\">\n");
    // dataUri is caller-sanitized; escape for HTML attribute context.
    html += QStringLiteral("    <img src=\"") + escapeText(dataUri)
          + QStringLiteral("\" alt=\"") + escapeText(filename)
          + QStringLiteral("\" width=\"") + QString::number(widthPx)
          + QStringLiteral("\" height=\"") + QString::number(heightPx)
          + QStringLiteral("\" style=\"max-width:100%;height:auto;display:block;\">\n");
    html += QStringLiteral("  </div>\n");
    html += QStringLiteral("  <div class=\"emb-img-cap\">\n");
    html += QStringLiteral("    <span class=\"file\">") + escapeText(filename) + QStringLiteral("</span>\n");
    html += QStringLiteral("    <span>·</span>\n");
    html += QStringLiteral("    <span>") + escapeText(caption) + QStringLiteral("</span>\n");
    html += QStringLiteral("  </div>\n");
    html += QStringLiteral("</div>\n");
    return html;
}

QString renderDiffEmbed(const QString &filename,
                        const QString &unifiedDiffText) {
    QString html;
    html.reserve(unifiedDiffText.size() + 256);
    html += QStringLiteral("<div class=\"emb-diff\">\n");
    html += QStringLiteral("  <div class=\"diff-head\">\n");
    html += QStringLiteral("    <span class=\"lead\">diff</span>\n");
    html += QStringLiteral("    <span class=\"file\">") + escapeText(filename) + QStringLiteral("</span>\n");
    html += QStringLiteral("  </div>\n");
    html += QStringLiteral("  <pre class=\"diff-body\">")
          + renderUnifiedDiffBody(unifiedDiffText)
          + QStringLiteral("</pre>\n");
    html += QStringLiteral("</div>\n");
    return html;
}

QString renderTerminalEmbed(const QString &logText) {
    // Render the first non-empty line as the `$` prompt chrome line; the
    // remainder is the body. The CSS variables --canvas-soft / --ink etc.
    // are inherited from the parent .canvas context.
    QString prompt;
    QString body = logText;
    const int nl = logText.indexOf(QChar('\n'));
    if (nl >= 0) {
        prompt = logText.left(nl);
        body = logText.mid(nl + 1);
    } else {
        prompt = logText;
        body.clear();
    }

    QString html;
    html.reserve(logText.size() + 256);
    html += QStringLiteral("<div class=\"emb emb-term\">\n");
    html += QStringLiteral("  <div class=\"emb-head\">\n");
    html += QStringLiteral("    <span class=\"lead\">term</span>\n");
    html += QStringLiteral("    <span class=\"path\">$ ") + escapeText(prompt) + QStringLiteral("</span>\n");
    html += QStringLiteral("  </div>\n");
    html += QStringLiteral("  <pre class=\"emb-code\">") + escapeText(body) + QStringLiteral("</pre>\n");
    html += QStringLiteral("</div>\n");
    return html;
}

// ─────────────────────────────────────────────────────────────────
// styleBlock — verbatim CSS pulled from design/notes-ux/index.html
// (lines 11–1876, between the <style> and </style> tags).
//
// Pixel-fidelity is the whole point of embedding this — do NOT
// rewrite, summarize, simplify, or change anything. Update by re-
// pasting from the design file when it changes.
// ─────────────────────────────────────────────────────────────────

QString styleBlock() {
    return QStringLiteral(R"CSS(/* ────────────────────────────────────────────────────────────────────
   MEETING THINKPAD — UX design review
   Aesthetic: engineering blueprint × editorial print
   Type: Fraunces (display serif) / IBM Plex Sans (body) / JetBrains Mono (technical)
   Brand: burnt orange #dd5a23 (light) · #f97316 (dark)
   ──────────────────────────────────────────────────────────────────── */

*, *::before, *::after { box-sizing: border-box; margin: 0; padding: 0; }

:root {
  --page-bg: #0a0b0e;
  --page-bg-2: #111319;
  --page-ink: #ebeae3;
  --page-mute: #8a8f9c;
  --page-dim: #555b6a;
  --page-rule: #1b1e25;
  --page-accent: #dd5a23;

  --sans:  'IBM Plex Sans', -apple-system, 'Segoe UI', sans-serif;
  --serif: 'Fraunces', 'Georgia', serif;
  --mono:  'JetBrains Mono', 'SF Mono', 'Menlo', monospace;
}

html, body {
  font-family: var(--sans);
  background: var(--page-bg);
  color: var(--page-ink);
  -webkit-font-smoothing: antialiased;
  -moz-osx-font-smoothing: grayscale;
  line-height: 1.5;
  font-feature-settings: "ss01","cv02","cv11";
}

body {
  padding: 64px 24px 144px;
  min-height: 100vh;
  background-image:
    linear-gradient(rgba(255,255,255,0.014) 1px, transparent 1px),
    linear-gradient(90deg, rgba(255,255,255,0.014) 1px, transparent 1px);
  background-size: 32px 32px;
  background-position: center top;
}

/* ─── DESIGN-REVIEW PAGE CHROME ─────────────────────────────────── */
.wrap { max-width: 1360px; margin: 0 auto; }

.review-head {
  margin-bottom: 80px;
  padding-bottom: 48px;
  border-bottom: 1px solid var(--page-rule);
}
.review-eyebrow {
  font-family: var(--mono);
  font-size: 11px;
  letter-spacing: 0.18em;
  text-transform: uppercase;
  color: var(--page-accent);
  margin-bottom: 22px;
}
.review-title {
  font-family: var(--serif);
  font-variation-settings: 'opsz' 144, 'wght' 380;
  font-size: clamp(48px, 8vw, 104px);
  line-height: 0.92;
  letter-spacing: -0.028em;
  margin-bottom: 26px;
  max-width: 14ch;
}
.review-title em {
  font-style: italic;
  font-variation-settings: 'opsz' 144, 'wght' 300;
  color: var(--page-accent);
}
.review-sub {
  font-size: 18px;
  max-width: 720px;
  color: var(--page-mute);
  line-height: 1.55;
}
.review-meta {
  font-family: var(--mono);
  font-size: 11px;
  color: var(--page-dim);
  letter-spacing: 0.08em;
  text-transform: uppercase;
  margin-top: 36px;
  display: flex;
  gap: 28px;
  flex-wrap: wrap;
}
.review-meta strong { color: var(--page-ink); font-weight: 500; }

.principles {
  margin: 56px 0 0;
  display: grid;
  grid-template-columns: repeat(auto-fit, minmax(220px, 1fr));
  gap: 1px;
  background: var(--page-rule);
  border: 1px solid var(--page-rule);
}
.principle {
  padding: 22px 24px 26px;
  background: var(--page-bg);
}
.principle-num {
  font-family: var(--mono);
  font-size: 10px;
  letter-spacing: 0.18em;
  color: var(--page-accent);
  margin-bottom: 10px;
}
.principle-body {
  font-family: var(--serif);
  font-variation-settings: 'opsz' 36, 'wght' 400;
  font-size: 17px;
  line-height: 1.35;
  letter-spacing: -0.005em;
  color: var(--page-ink);
}
.principle-body em { color: var(--page-accent); font-style: italic; }

/* ─── SURFACE-LEVEL HEADINGS ────────────────────────────────────── */
.surface {
  margin: 0 0 128px;
  scroll-margin-top: 48px;
}
.surface-head {
  display: grid;
  grid-template-columns: auto 1fr auto;
  align-items: end;
  gap: 32px;
  padding-bottom: 22px;
  margin-bottom: 40px;
  border-bottom: 1px solid var(--page-rule);
}
.surface-num {
  font-family: var(--mono);
  font-size: 13px;
  color: var(--page-accent);
  letter-spacing: 0.12em;
  font-weight: 700;
}
.surface-title {
  font-family: var(--serif);
  font-variation-settings: 'opsz' 72, 'wght' 440;
  font-size: 38px;
  letter-spacing: -0.018em;
  line-height: 1.05;
  color: var(--page-ink);
}
.surface-tag {
  font-family: var(--mono);
  font-size: 11px;
  letter-spacing: 0.12em;
  text-transform: uppercase;
  color: var(--page-dim);
}
.surface-desc {
  color: var(--page-mute);
  font-size: 15px;
  max-width: 680px;
  line-height: 1.6;
  margin-bottom: 28px;
}
.surface-desc strong { color: var(--page-ink); font-weight: 500; }

/* ─── MOCK WINDOW (per-surface theme container) ─────────────────── */
[data-theme="light"] {
  --canvas:      #f5f4ee;
  --canvas-soft: #ede9d9;
  --canvas-grid: rgba(15,23,42,0.045);
  --chrome:      #ebe9df;
  --chrome-2:    #ddd9c8;
  --ink:         #0a0d12;
  --ink-mute:    #555c6a;
  --ink-dim:     #888f9c;
  --ink-faint:   #c2bfaf;
  --rule:        #d6d3c5;
  --rule-soft:   rgba(15,23,42,0.06);
  --accent:      #dd5a23;
  --accent-soft: rgba(221,90,35,0.12);

  --decision: #0f766e;
  --action:   #b45309;
  --question: #3b3795;
  --risk:     #9f1239;
  --quote:    #475569;

  --pr-open:  #16a34a;
  --pr-bg:    rgba(22,163,74,0.06);
}

[data-theme="dark"] {
  --canvas:      #0c0d10;
  --canvas-soft: #14161c;
  --canvas-grid: rgba(229,231,235,0.04);
  --chrome:      #15171c;
  --chrome-2:    #1e2128;
  --ink:         #ece9de;
  --ink-mute:    #98a0ae;
  --ink-dim:     #5e6573;
  --ink-faint:   #2f343d;
  --rule:        #1e2128;
  --rule-soft:   rgba(229,231,235,0.05);
  --accent:      #f97316;
  --accent-soft: rgba(249,115,22,0.16);

  --decision: #5eead4;
  --action:   #fbbf24;
  --question: #a5b4fc;
  --risk:     #fb7185;
  --quote:    #94a3b8;

  --pr-open:  #4ade80;
  --pr-bg:    rgba(74,222,128,0.08);
}

.mock {
  background: var(--canvas);
  color: var(--ink);
  border: 1px solid var(--page-rule);
  box-shadow: 0 40px 80px -40px rgba(0,0,0,0.85),
              0 8px 24px -8px rgba(0,0,0,0.45);
  overflow: hidden;
  font-family: var(--sans);
  position: relative;
}

/* ─── WINDOW TITLEBAR (GNOME-style minimal) ─────────────────────── */
.tbar {
  background: var(--chrome);
  border-bottom: 1px solid var(--rule);
  padding: 0 14px;
  display: flex;
  align-items: center;
  gap: 14px;
  font-family: var(--mono);
  font-size: 11px;
  color: var(--ink-mute);
  height: 36px;
  letter-spacing: 0.02em;
}
.tbar-name { flex: 1; text-align: center; }
.tbar-ctrl {
  display: flex;
  gap: 14px;
  color: var(--ink-dim);
  font-size: 12px;
  letter-spacing: 0.04em;
}

/* ─── NOTEPATRA TAB STRIP ───────────────────────────────────────── */
.tabstrip {
  background: var(--chrome);
  display: flex;
  gap: 0;
  border-bottom: 1px solid var(--rule);
  align-items: stretch;
  font-size: 12px;
  height: 38px;
  padding-left: 8px;
}
.tab {
  padding: 0 14px;
  display: flex;
  align-items: center;
  gap: 8px;
  color: var(--ink-mute);
  border-right: 1px solid var(--rule);
  font-size: 12px;
  letter-spacing: 0.005em;
  position: relative;
  font-weight: 400;
  max-width: 240px;
  white-space: nowrap;
  overflow: hidden;
  text-overflow: ellipsis;
}
.tab .icn {
  font-family: var(--mono);
  font-size: 12px;
  color: var(--ink-dim);
}
.tab.active {
  background: var(--canvas);
  color: var(--ink);
  margin-bottom: -1px;
}
.tab.active .icn { color: var(--accent); }
.tab.active::before {
  content: '';
  position: absolute;
  top: 0; left: 0; right: 0;
  height: 2px;
  background: var(--accent);
}
.tab-close {
  color: var(--ink-faint);
  font-size: 9px;
  margin-left: 6px;
  font-family: var(--mono);
}

/* ─── NOTES-TAB BODY (the surface we're designing) ──────────────── */
.notes-tab {
  display: grid;
  grid-template-columns: 36px 1fr;
  background: var(--canvas);
  min-height: 720px;
  position: relative;
}

/* Edge icon strip — the only persistent UI affordance */
.edge {
  background: var(--chrome);
  border-right: 1px solid var(--rule);
  display: flex;
  flex-direction: column;
  align-items: center;
  padding: 12px 0;
  gap: 4px;
  position: relative;
}
.edge-icn {
  width: 28px;
  height: 28px;
  display: flex;
  align-items: center;
  justify-content: center;
  color: var(--ink-dim);
  font-family: var(--mono);
  font-size: 13px;
  font-weight: 600;
  cursor: pointer;
  border-radius: 3px;
  transition: all 0.12s;
  position: relative;
  letter-spacing: 0;
}
.edge-icn:hover {
  background: var(--rule);
  color: var(--ink);
}
.edge-icn.active {
  background: var(--accent-soft);
  color: var(--accent);
}
.edge-icn::after {
  content: attr(data-kbd);
  position: absolute;
  left: calc(100% + 8px);
  top: 50%;
  transform: translateY(-50%);
  background: var(--ink);
  color: var(--canvas);
  padding: 3px 7px;
  font-size: 10px;
  font-family: var(--mono);
  letter-spacing: 0.04em;
  border-radius: 2px;
  opacity: 0;
  pointer-events: none;
  white-space: nowrap;
  transition: opacity 0.15s;
  z-index: 10;
}
.edge-icn:hover::after { opacity: 1; }
.edge-spacer { flex: 1; }
.edge-icn-dim { color: var(--ink-faint); }

/* ─── CANVAS (the writing surface) ──────────────────────────────── */
.canvas {
  background: var(--canvas);
  background-image:
    linear-gradient(var(--canvas-grid) 1px, transparent 1px),
    linear-gradient(90deg, var(--canvas-grid) 1px, transparent 1px);
  background-size: 24px 24px;
  background-position: 0 0;
  padding: 56px 88px 88px;
  position: relative;
  min-height: 700px;
}
.canvas-empty {
  display: flex;
  align-items: center;
  justify-content: center;
  flex-direction: column;
  min-height: 700px;
  padding: 80px 88px;
  text-align: center;
}

/* Empty state */
.empty-hint-kbd {
  font-family: var(--mono);
  font-size: 12px;
  color: var(--ink-mute);
  letter-spacing: 0.12em;
  margin-bottom: 8px;
  text-transform: uppercase;
}
.empty-hint {
  font-family: var(--serif);
  font-variation-settings: 'opsz' 72, 'wght' 380;
  font-size: 38px;
  letter-spacing: -0.018em;
  line-height: 1.1;
  color: var(--ink);
  margin-bottom: 16px;
  max-width: 16ch;
}
.empty-hint em {
  font-style: italic;
  color: var(--accent);
}
.empty-or {
  font-family: var(--mono);
  font-size: 11px;
  color: var(--ink-dim);
  letter-spacing: 0.16em;
  text-transform: uppercase;
  margin: 28px 0 16px;
}
.empty-recent {
  display: flex;
  flex-direction: column;
  gap: 1px;
  background: var(--rule);
  border: 1px solid var(--rule);
  margin-top: 8px;
  min-width: 460px;
}
.recent-row {
  background: var(--canvas);
  padding: 12px 18px;
  display: grid;
  grid-template-columns: auto 1fr auto;
  gap: 18px;
  align-items: baseline;
  font-size: 13px;
  text-align: left;
  cursor: pointer;
  transition: background 0.12s;
}
.recent-row:hover { background: var(--canvas-soft); }
.recent-date {
  font-family: var(--mono);
  font-size: 11px;
  color: var(--ink-dim);
  letter-spacing: 0.04em;
}
.recent-title {
  color: var(--ink);
  font-weight: 400;
  font-feature-settings: "ss01";
}
.recent-dur {
  font-family: var(--mono);
  font-size: 11px;
  color: var(--ink-mute);
}
.recent-row.has-todos .recent-title::after {
  content: '·';
  margin: 0 8px;
  color: var(--ink-faint);
}
.recent-badge {
  font-family: var(--mono);
  font-size: 10px;
  color: var(--action);
  background: rgba(180,83,9,0.12);
  padding: 1px 6px;
  margin-left: 10px;
  letter-spacing: 0.04em;
}
[data-theme="dark"] .recent-badge { background: rgba(251,191,36,0.16); }

/* ─── MEETING HEADER ───────────────────────────────────────────── */
.meet-head {
  position: relative;
  margin-bottom: 28px;
}
.meet-title {
  font-family: var(--serif);
  font-variation-settings: 'opsz' 48, 'wght' 460;
  font-size: 30px;
  letter-spacing: -0.014em;
  line-height: 1.15;
  color: var(--ink);
  margin-bottom: 8px;
  max-width: 720px;
}
.meet-meta {
  font-family: var(--mono);
  font-size: 12px;
  color: var(--ink-mute);
  letter-spacing: 0.005em;
  display: flex;
  align-items: center;
  gap: 12px;
  flex-wrap: wrap;
}
.meet-meta-sep { color: var(--ink-faint); }
.meet-meta-collapse {
  color: var(--ink-dim);
  margin-left: auto;
  cursor: pointer;
  font-size: 11px;
}
.meet-divider {
  height: 1px;
  background: linear-gradient(90deg, var(--accent) 0%, var(--accent) 8%, transparent 60%);
  margin: 24px 0 32px;
  opacity: 0.7;
}

/* Always-visible timer in top-right of canvas */
.timer {
  position: absolute;
  top: 0;
  right: 0;
  font-family: var(--mono);
  font-size: 13px;
  font-weight: 500;
  color: var(--ink-mute);
  letter-spacing: 0.06em;
  display: flex;
  align-items: center;
  gap: 8px;
  background: var(--canvas);
  padding: 6px 12px;
  border: 1px solid var(--rule);
  border-radius: 2px;
}
.timer .dot {
  width: 6px;
  height: 6px;
  background: var(--accent);
  border-radius: 50%;
  animation: pulse 1.4s ease-in-out infinite;
}
@keyframes pulse {
  0%,100% { opacity: 1; }
  50% { opacity: 0.35; }
}

/* ─── BLOCKS ─────────────────────────────────────────────────────── */
.p {
  font-size: 16px;
  line-height: 1.65;
  color: var(--ink);
  margin: 0 0 18px;
  max-width: 720px;
  font-feature-settings: "ss01";
}
.p .at {
  color: var(--accent);
  font-weight: 500;
  font-feature-settings: "ss01";
}
.p .hash {
  color: var(--ink-mute);
  font-family: var(--mono);
  font-size: 14px;
  letter-spacing: 0.005em;
}
.p .ref {
  color: var(--question);
  font-family: var(--mono);
  font-size: 14px;
  border-bottom: 1px dotted var(--question);
  text-decoration: none;
  padding-bottom: 1px;
}

/* Tagged block: inline icon + small-caps label */
.b {
  margin: 22px 0;
  max-width: 760px;
  display: grid;
  grid-template-columns: 22px 1fr;
  gap: 14px;
  align-items: baseline;
  padding: 0;
}
.b-icn {
  font-family: var(--mono);
  font-size: 14px;
  font-weight: 700;
  text-align: center;
  line-height: 1.65;
}
.b-body {
  font-size: 16px;
  line-height: 1.55;
  color: var(--ink);
}
.b-lbl {
  font-family: var(--mono);
  font-size: 10px;
  letter-spacing: 0.2em;
  text-transform: uppercase;
  font-weight: 700;
  margin-right: 10px;
}
.b-time {
  font-family: var(--mono);
  font-size: 10px;
  color: var(--ink-dim);
  letter-spacing: 0.08em;
  margin-left: 10px;
  user-select: none;
}

.b-dec .b-icn,
.b-dec .b-lbl { color: var(--decision); }
.b-act .b-icn,
.b-act .b-lbl { color: var(--action); }
.b-q .b-icn,
.b-q .b-lbl { color: var(--question); }
.b-risk .b-icn,
.b-risk .b-lbl { color: var(--risk); }
.b-quote .b-icn,
.b-quote .b-lbl { color: var(--quote); }
.b-quote .b-body {
  font-style: italic;
  color: var(--ink-mute);
  font-feature-settings: "ss01";
}
.b-quote .b-body .attrib {
  display: block;
  font-style: normal;
  font-family: var(--mono);
  font-size: 11px;
  color: var(--quote);
  margin-top: 4px;
  letter-spacing: 0.04em;
}

/* Owner + due chips on action items */
.chip {
  display: inline-flex;
  align-items: center;
  gap: 4px;
  font-family: var(--mono);
  font-size: 11px;
  padding: 1px 7px;
  margin-left: 8px;
  letter-spacing: 0.02em;
  font-weight: 500;
  vertical-align: 1px;
  border-radius: 2px;
}
.chip-owner {
  color: var(--accent);
  background: var(--accent-soft);
}
.chip-due {
  color: var(--ink-mute);
  background: var(--rule);
  border-left: 2px solid var(--action);
  padding-left: 6px;
}
.chip-done {
  text-decoration: line-through;
  color: var(--ink-dim);
}

/* ─── EMBEDS ────────────────────────────────────────────────────── */
/* Embed: code reference */
.emb {
  margin: 26px 0;
  max-width: 720px;
  background: var(--canvas-soft);
  border: 1px solid var(--rule);
  border-left: 3px solid var(--accent);
  font-family: var(--sans);
}
.emb-head {
  padding: 9px 14px;
  border-bottom: 1px solid var(--rule);
  display: flex;
  align-items: center;
  gap: 8px;
  font-family: var(--mono);
  font-size: 11px;
  color: var(--ink-mute);
  letter-spacing: 0.005em;
}
.emb-head .lead { color: var(--accent); font-weight: 700; }
.emb-head .path { color: var(--ink); font-weight: 500; }
.emb-head .meta { color: var(--ink-dim); }
.emb-head .ext { margin-left: auto; color: var(--ink-dim); cursor: pointer; }
.emb-head .ext:hover { color: var(--accent); }

.emb-code {
  padding: 12px 14px 14px;
  font-family: var(--mono);
  font-size: 12px;
  line-height: 1.7;
  color: var(--ink);
  overflow-x: auto;
}
.emb-code .ln {
  display: inline-block;
  width: 28px;
  text-align: right;
  color: var(--ink-dim);
  margin-right: 14px;
  user-select: none;
}
.emb-code .kw  { color: var(--question); font-weight: 500; }
.emb-code .ty  { color: var(--decision); }
.emb-code .str { color: var(--decision); }
.emb-code .num { color: var(--action); }
.emb-code .com { color: var(--ink-dim); font-style: italic; }
.emb-code .punc { color: var(--ink-mute); }
.emb-code .row { display: block; }
.emb-code .row.hl { background: var(--accent-soft); }

/* Embed: image (CSS-drawn chart placeholder) */
.emb-img {
  margin: 26px 0;
  max-width: 720px;
  border: 1px solid var(--rule);
  background: var(--canvas-soft);
}
.emb-img-head {
  padding: 9px 14px;
  border-bottom: 1px solid var(--rule);
  display: flex;
  align-items: center;
  gap: 8px;
  font-family: var(--mono);
  font-size: 11px;
  color: var(--ink-mute);
}
.emb-img-head .lead { color: var(--accent); }
.emb-img-figure {
  padding: 24px 28px 12px;
  background: linear-gradient(180deg, var(--canvas-soft) 0%, var(--canvas) 100%);
  position: relative;
}
.chart {
  height: 200px;
  position: relative;
  display: flex;
  align-items: end;
  gap: 8px;
  padding: 0 16px 32px;
  background-image: linear-gradient(var(--rule-soft) 1px, transparent 1px);
  background-size: 100% 40px;
  background-position: 0 0;
}
.chart-bar {
  flex: 1;
  background: var(--decision);
  opacity: 0.7;
  max-width: 36px;
  position: relative;
  transition: opacity 0.15s;
}
.chart-bar.hot { background: var(--risk); opacity: 0.85; }
.chart-bar.warn { background: var(--action); opacity: 0.8; }
.chart-bar::after {
  content: attr(data-label);
  position: absolute;
  bottom: -20px;
  left: 50%;
  transform: translateX(-50%);
  font-family: var(--mono);
  font-size: 9px;
  color: var(--ink-dim);
  letter-spacing: 0.04em;
}
.chart-y-label {
  position: absolute;
  left: 6px;
  top: 50%;
  transform: rotate(-90deg) translateX(50%);
  transform-origin: left top;
  font-family: var(--mono);
  font-size: 9px;
  color: var(--ink-dim);
  letter-spacing: 0.04em;
}
.emb-img-cap {
  padding: 10px 14px 12px;
  border-top: 1px solid var(--rule);
  font-size: 12px;
  color: var(--ink-mute);
  font-family: var(--mono);
  display: flex;
  gap: 14px;
  letter-spacing: 0.005em;
}
.emb-img-cap .file { color: var(--ink); }

/* Embed: PR card */
.emb-pr {
  margin: 26px 0;
  max-width: 560px;
  border: 1px solid var(--rule);
  background: var(--canvas-soft);
  border-left: 3px solid var(--pr-open);
}
.pr-head {
  padding: 9px 14px;
  display: flex;
  align-items: center;
  gap: 12px;
  border-bottom: 1px solid var(--rule);
  font-family: var(--mono);
  font-size: 11px;
  color: var(--ink-mute);
  letter-spacing: 0.005em;
}
.pr-num { color: var(--ink); font-weight: 600; }
.pr-repo { color: var(--ink-mute); }
.pr-status {
  display: inline-flex;
  align-items: center;
  gap: 6px;
  color: var(--pr-open);
  font-weight: 700;
  letter-spacing: 0.1em;
  text-transform: uppercase;
  font-size: 10px;
  margin-left: auto;
  background: var(--pr-bg);
  padding: 2px 7px;
  border-radius: 2px;
}
.pr-status::before {
  content: '';
  width: 6px;
  height: 6px;
  border-radius: 50%;
  background: var(--pr-open);
}
.pr-title {
  padding: 9px 14px 6px;
  font-family: var(--sans);
  font-size: 14px;
  font-weight: 500;
  color: var(--ink);
  line-height: 1.4;
}
.pr-foot {
  padding: 4px 14px 10px;
  font-family: var(--mono);
  font-size: 11px;
  color: var(--ink-dim);
  display: flex;
  gap: 12px;
  flex-wrap: wrap;
  letter-spacing: 0.005em;
}
.pr-foot .plus { color: var(--decision); }
.pr-foot .minus { color: var(--risk); }
.pr-foot .author { color: var(--accent); }

/* Embed: video link card */
.emb-vid {
  margin: 26px 0;
  max-width: 560px;
  border: 1px solid var(--rule);
  background: var(--canvas-soft);
  display: grid;
  grid-template-columns: 132px 1fr;
}
.vid-thumb {
  background: linear-gradient(135deg, #0f1320 0%, #1e293b 35%, #475569 65%, #0f1320 100%);
  display: flex;
  align-items: center;
  justify-content: center;
  color: rgba(255,255,255,0.78);
  font-size: 28px;
  position: relative;
  font-family: var(--mono);
}
.vid-thumb::after {
  content: '';
  position: absolute;
  bottom: 0; left: 0; right: 0;
  height: 32%;
  background: linear-gradient(180deg, transparent, rgba(0,0,0,0.55));
  pointer-events: none;
}
.vid-thumb-dur {
  position: absolute;
  bottom: 6px;
  right: 6px;
  background: rgba(0,0,0,0.7);
  color: white;
  padding: 1px 5px;
  font-size: 10px;
  font-family: var(--mono);
  letter-spacing: 0.04em;
  z-index: 1;
}
.vid-play {
  width: 0;
  height: 0;
  border-style: solid;
  border-width: 11px 0 11px 18px;
  border-color: transparent transparent transparent rgba(255,255,255,0.85);
  margin-left: 4px;
  filter: drop-shadow(0 2px 4px rgba(0,0,0,0.5));
}
.vid-meta {
  padding: 11px 14px 12px;
  display: flex;
  flex-direction: column;
  gap: 4px;
}
.vid-title {
  font-family: var(--sans);
  font-size: 13px;
  font-weight: 500;
  color: var(--ink);
  line-height: 1.4;
}
.vid-host {
  font-family: var(--mono);
  font-size: 11px;
  color: var(--ink-dim);
  margin-top: auto;
  letter-spacing: 0.005em;
}

/* Embed: diff */
.emb-diff {
  margin: 26px 0;
  max-width: 720px;
  border: 1px solid var(--rule);
  background: var(--canvas-soft);
  border-left: 3px solid var(--question);
}
.diff-head {
  padding: 9px 14px;
  border-bottom: 1px solid var(--rule);
  font-family: var(--mono);
  font-size: 11px;
  color: var(--ink-mute);
}
.diff-head .lead { color: var(--accent); }
.diff-head .file { color: var(--ink); font-weight: 500; }
.diff-body {
  padding: 8px 14px 10px;
  font-family: var(--mono);
  font-size: 12px;
  line-height: 1.65;
}
.diff-body .add { color: var(--decision); }
.diff-body .del { color: var(--risk); }
.diff-body .ctx { color: var(--ink-dim); }

/* End-meeting CTA */
.meet-cta {
  margin-top: 56px;
  padding-top: 36px;
  border-top: 1px solid var(--rule);
  display: flex;
  align-items: center;
  justify-content: space-between;
  gap: 14px;
  flex-wrap: wrap;
}
.meet-cta-hint {
  font-family: var(--mono);
  font-size: 11px;
  color: var(--ink-dim);
  letter-spacing: 0.04em;
}
.meet-cta-hint kbd {
  display: inline-block;
  background: var(--rule);
  color: var(--ink);
  padding: 1px 6px;
  font-family: var(--mono);
  font-size: 10px;
  margin: 0 2px;
  border-radius: 2px;
}
.btn-cta {
  background: var(--accent);
  color: white;
  padding: 10px 20px;
  font-family: var(--mono);
  font-size: 12px;
  letter-spacing: 0.1em;
  text-transform: uppercase;
  font-weight: 700;
  border: none;
  cursor: pointer;
  transition: filter 0.1s;
  border-radius: 1px;
}
.btn-cta:hover { filter: brightness(1.12); }
.btn-ghost {
  background: transparent;
  color: var(--ink-mute);
  padding: 9px 14px;
  font-family: var(--mono);
  font-size: 11px;
  letter-spacing: 0.08em;
  text-transform: uppercase;
  border: 1px solid var(--rule);
  cursor: pointer;
  border-radius: 1px;
}
.btn-ghost:hover { border-color: var(--ink-mute); color: var(--ink); }

/* ─── SLASH MENU (the inline popover) ───────────────────────────── */
.slash {
  display: inline-block;
  background: #0d0e12;
  color: #ebeae3;
  border: 1px solid #1e2128;
  border-radius: 4px;
  font-family: var(--mono);
  font-size: 12px;
  width: 232px;
  box-shadow: 0 20px 40px -10px rgba(0,0,0,0.7);
  overflow: hidden;
}
.slash-input {
  padding: 8px 12px;
  border-bottom: 1px solid #1e2128;
  color: #f97316;
  letter-spacing: 0.04em;
  display: flex;
  align-items: center;
  gap: 6px;
}
.slash-input .pipe {
  width: 1px;
  height: 12px;
  background: #f97316;
  animation: blink 1s steps(2) infinite;
}
@keyframes blink {
  50% { opacity: 0; }
}
.slash-list { padding: 4px 0; }
.slash-row {
  padding: 5px 12px;
  display: grid;
  grid-template-columns: 18px 76px 1fr;
  gap: 8px;
  align-items: center;
  font-size: 11.5px;
  color: #ebeae3;
  letter-spacing: 0.005em;
}
.slash-row:hover { background: rgba(249,115,22,0.12); }
.slash-row.sel {
  background: rgba(249,115,22,0.16);
  border-left: 2px solid #f97316;
  padding-left: 10px;
}
.slash-key {
  font-size: 10px;
  color: #5b6473;
  background: #1e2128;
  padding: 1px 5px;
  text-align: center;
  border-radius: 1px;
}
.slash-cmd { color: #f97316; }
.slash-desc { color: #98a0ae; font-size: 11px; }
.slash-foot {
  border-top: 1px solid #1e2128;
  padding: 6px 12px;
  font-size: 10px;
  color: #5b6473;
  display: flex;
  gap: 12px;
  letter-spacing: 0.04em;
}
.slash-foot kbd {
  font-family: var(--mono);
  background: #1e2128;
  color: #98a0ae;
  padding: 0 4px;
  margin-right: 2px;
}

/* ─── SLIDE-OVER PANELS (Todos / Reminders / Notebooks) ─────────── */
.slide {
  position: absolute;
  top: 0;
  left: 36px;
  bottom: 0;
  background: var(--canvas-soft);
  border-right: 1px solid var(--rule);
  width: 320px;
  display: flex;
  flex-direction: column;
}
.slide-head {
  padding: 18px 22px 14px;
  border-bottom: 1px solid var(--rule);
  display: flex;
  align-items: center;
  gap: 10px;
}
.slide-icn {
  font-family: var(--mono);
  font-size: 16px;
  color: var(--accent);
}
.slide-title {
  font-family: var(--serif);
  font-variation-settings: 'opsz' 24, 'wght' 500;
  font-size: 18px;
  color: var(--ink);
  letter-spacing: -0.005em;
}
.slide-count {
  margin-left: auto;
  font-family: var(--mono);
  font-size: 10px;
  background: var(--accent);
  color: white;
  padding: 2px 7px;
  letter-spacing: 0.04em;
  font-weight: 700;
  border-radius: 2px;
}
.slide-search {
  padding: 10px 16px;
  border-bottom: 1px solid var(--rule);
  font-family: var(--mono);
  font-size: 12px;
  color: var(--ink-dim);
  display: flex;
  gap: 8px;
  align-items: center;
}
.slide-search .key {
  background: var(--rule);
  padding: 1px 5px;
  font-size: 10px;
  letter-spacing: 0.04em;
  margin-left: auto;
}
.slide-body { flex: 1; overflow-y: auto; }
.slide-group {
  padding: 14px 16px 4px;
}
.slide-group-head {
  font-family: var(--mono);
  font-size: 10px;
  text-transform: uppercase;
  letter-spacing: 0.18em;
  margin-bottom: 8px;
  font-weight: 700;
  display: flex;
  align-items: center;
  gap: 8px;
}
.slide-group-count {
  color: var(--ink-dim);
  font-weight: 400;
}
.slide-group-overdue .slide-group-head { color: var(--risk); }
.slide-group-today .slide-group-head { color: var(--action); }
.slide-group-week .slide-group-head { color: var(--question); }
.slide-group-later .slide-group-head { color: var(--quote); }

.todo {
  padding: 8px 4px;
  display: grid;
  grid-template-columns: 16px 1fr;
  gap: 10px;
  align-items: start;
  border-top: 1px solid var(--rule-soft);
  cursor: pointer;
  transition: background 0.12s;
}
.todo:hover { background: var(--canvas); }
.todo-check {
  width: 14px;
  height: 14px;
  border: 1.5px solid var(--ink-dim);
  border-radius: 2px;
  margin-top: 4px;
  background: transparent;
}
.todo-done .todo-check {
  background: var(--decision);
  border-color: var(--decision);
  position: relative;
}
.todo-done .todo-check::after {
  content: '✓';
  position: absolute;
  inset: 0;
  display: flex;
  align-items: center;
  justify-content: center;
  color: white;
  font-size: 10px;
}
.todo-text {
  font-size: 13px;
  line-height: 1.45;
  color: var(--ink);
}
.todo-done .todo-text {
  text-decoration: line-through;
  color: var(--ink-dim);
}
.todo-meta {
  font-family: var(--mono);
  font-size: 10px;
  color: var(--ink-dim);
  margin-top: 3px;
  letter-spacing: 0.005em;
  display: flex;
  gap: 8px;
  flex-wrap: wrap;
}
.todo-meta .owner { color: var(--accent); }
.todo-meta .meeting { color: var(--ink-mute); }
.todo-meta .due-overdue { color: var(--risk); font-weight: 700; }
.todo-meta .due-today { color: var(--action); font-weight: 700; }
.todo-meta .due-week { color: var(--question); }

/* Notebooks slide-over */
.nb-tree {
  padding: 8px 0;
  flex: 1;
  overflow-y: auto;
  font-size: 13px;
}
.nb-item {
  padding: 5px 16px 5px 24px;
  display: flex;
  align-items: center;
  gap: 8px;
  cursor: pointer;
  color: var(--ink);
  transition: background 0.12s;
  position: relative;
}
.nb-item:hover { background: var(--canvas); }
.nb-item.active {
  background: var(--accent-soft);
  color: var(--accent);
  font-weight: 500;
}
.nb-item.active::before {
  content: '';
  position: absolute;
  left: 0; top: 0; bottom: 0;
  width: 2px;
  background: var(--accent);
}
.nb-tri {
  font-family: var(--mono);
  font-size: 9px;
  color: var(--ink-dim);
  width: 10px;
}
.nb-icn {
  font-family: var(--mono);
  font-size: 12px;
  color: var(--ink-dim);
  width: 14px;
  text-align: center;
}
.nb-l1 { padding-left: 20px; font-weight: 500; }
.nb-l2 { padding-left: 38px; }
.nb-l3 { padding-left: 56px; }
.nb-count {
  margin-left: auto;
  font-family: var(--mono);
  font-size: 10px;
  color: var(--ink-dim);
  letter-spacing: 0.04em;
}

/* Reminders log */
.rem-row {
  padding: 10px 16px;
  border-bottom: 1px solid var(--rule-soft);
  display: grid;
  grid-template-columns: 14px 1fr;
  gap: 10px;
  align-items: start;
}
.rem-dot {
  width: 8px;
  height: 8px;
  border-radius: 50%;
  margin-top: 6px;
}
.rem-dot-fired   { background: var(--decision); }
.rem-dot-snoozed { background: var(--action); }
.rem-dot-pending { background: var(--question); }
.rem-dot-missed  { background: var(--risk); }
.rem-text { font-size: 13px; color: var(--ink); line-height: 1.4; }
.rem-meta {
  font-family: var(--mono);
  font-size: 10px;
  color: var(--ink-dim);
  margin-top: 3px;
  letter-spacing: 0.005em;
}

/* ─── AI SWEEP OVERLAY (when ⏹ End Meeting is clicked) ──────────── */
.sweep-overlay {
  position: absolute;
  inset: 0;
  background: rgba(0,0,0,0.45);
  backdrop-filter: blur(2px);
  display: flex;
  align-items: flex-start;
  justify-content: center;
  padding: 36px 20px;
  z-index: 5;
}
.sweep-card {
  background: var(--canvas);
  border: 1px solid var(--rule);
  width: 100%;
  max-width: 780px;
  box-shadow: 0 30px 60px -10px rgba(0,0,0,0.5);
  display: flex;
  flex-direction: column;
}
.sweep-head {
  padding: 24px 32px 20px;
  border-bottom: 1px solid var(--rule);
}
.sweep-eyebrow {
  font-family: var(--mono);
  font-size: 11px;
  color: var(--accent);
  letter-spacing: 0.16em;
  text-transform: uppercase;
  margin-bottom: 8px;
}
.sweep-title {
  font-family: var(--serif);
  font-variation-settings: 'opsz' 60, 'wght' 460;
  font-size: 28px;
  color: var(--ink);
  letter-spacing: -0.014em;
  line-height: 1.15;
  margin-bottom: 6px;
}
.sweep-meta {
  font-family: var(--mono);
  font-size: 11px;
  color: var(--ink-mute);
  display: flex;
  gap: 14px;
  flex-wrap: wrap;
  letter-spacing: 0.005em;
}
.sweep-meta .ok { color: var(--decision); }
.sweep-body {
  max-height: 520px;
  overflow-y: auto;
  padding: 8px 0;
}
.sweep-section {
  padding: 18px 32px 8px;
  border-bottom: 1px solid var(--rule-soft);
}
.sweep-section-head {
  font-family: var(--mono);
  font-size: 11px;
  text-transform: uppercase;
  letter-spacing: 0.2em;
  font-weight: 700;
  margin-bottom: 12px;
  display: flex;
  align-items: center;
  gap: 10px;
}
.sweep-section-head .cnt {
  color: var(--ink-dim);
  font-weight: 400;
}
.sweep-section-head .tri {
  font-family: var(--mono);
  font-size: 9px;
  color: var(--ink-mute);
}
.sweep-dec .sweep-section-head { color: var(--decision); }
.sweep-act .sweep-section-head { color: var(--action); }
.sweep-q .sweep-section-head { color: var(--question); }
.sweep-r .sweep-section-head { color: var(--risk); }

.sweep-item {
  padding: 8px 0;
  display: grid;
  grid-template-columns: 22px 1fr auto;
  gap: 12px;
  align-items: baseline;
  border-top: 1px solid var(--rule-soft);
  font-size: 14px;
  line-height: 1.5;
  color: var(--ink);
}
.sweep-item:first-of-type { border-top: none; padding-top: 4px; }
.sweep-item-glyph {
  font-family: var(--mono);
  font-size: 13px;
  font-weight: 700;
  text-align: center;
  line-height: 1.5;
}
.sweep-dec .sweep-item-glyph { color: var(--decision); }
.sweep-act .sweep-item-glyph { color: var(--action); }
.sweep-q .sweep-item-glyph   { color: var(--question); }
.sweep-r .sweep-item-glyph   { color: var(--risk); }

.sweep-item .chip { vertical-align: 0; margin-left: 4px; }
.sweep-toggle {
  display: flex;
  align-items: center;
  gap: 8px;
  font-family: var(--mono);
  font-size: 10px;
  color: var(--ink-mute);
  letter-spacing: 0.04em;
  text-transform: uppercase;
}
.sweep-toggle .sw {
  width: 26px;
  height: 14px;
  background: var(--ink-dim);
  border-radius: 99px;
  position: relative;
  cursor: pointer;
}
.sweep-toggle .sw::after {
  content: '';
  position: absolute;
  width: 10px;
  height: 10px;
  background: var(--canvas);
  border-radius: 50%;
  top: 2px;
  left: 2px;
  transition: all 0.15s;
}
.sweep-toggle.on .sw { background: var(--decision); }
.sweep-toggle.on .sw::after { left: 14px; }
.sweep-foot {
  padding: 20px 32px 22px;
  border-top: 1px solid var(--rule);
  display: flex;
  align-items: center;
  justify-content: space-between;
  gap: 12px;
  background: var(--canvas-soft);
  flex-wrap: wrap;
}
.sweep-foot-hint {
  font-family: var(--mono);
  font-size: 11px;
  color: var(--ink-mute);
  letter-spacing: 0.005em;
}
.sweep-foot-hint b { color: var(--ink); font-weight: 500; }

/* ─── NOTIFICATION PREVIEW (libnotify-style toast) ──────────────── */
.notif {
  background: #1a1d24;
  border: 1px solid #2a2f37;
  color: #ebeae3;
  width: 380px;
  border-radius: 8px;
  box-shadow: 0 14px 32px -8px rgba(0,0,0,0.6);
  overflow: hidden;
  font-family: var(--sans);
}
.notif-head {
  padding: 12px 16px 8px;
  display: flex;
  align-items: center;
  gap: 10px;
  border-bottom: 1px solid #2a2f37;
}
.notif-icn {
  width: 24px;
  height: 24px;
  background: linear-gradient(135deg, #dd5a23, #f97316);
  border-radius: 5px;
  display: flex;
  align-items: center;
  justify-content: center;
  color: white;
  font-family: var(--mono);
  font-size: 12px;
  font-weight: 700;
}
.notif-app {
  font-family: var(--mono);
  font-size: 10px;
  color: #98a0ae;
  letter-spacing: 0.1em;
  text-transform: uppercase;
}
.notif-time {
  margin-left: auto;
  font-family: var(--mono);
  font-size: 10px;
  color: #5b6473;
}
.notif-body { padding: 12px 16px 14px; }
.notif-title {
  font-size: 14px;
  font-weight: 500;
  color: #ebeae3;
  margin-bottom: 4px;
  letter-spacing: 0.005em;
}
.notif-title .bell {
  color: #f97316;
  font-family: var(--mono);
  margin-right: 6px;
}
.notif-sub {
  font-family: var(--mono);
  font-size: 12px;
  color: #98a0ae;
  margin-bottom: 3px;
  letter-spacing: 0.005em;
}
.notif-sub .when { color: #f97316; font-weight: 600; }
.notif-src {
  font-family: var(--mono);
  font-size: 11px;
  color: #5b6473;
  letter-spacing: 0.005em;
}
.notif-actions {
  display: grid;
  grid-template-columns: 1fr 1fr 1fr 1fr;
  border-top: 1px solid #2a2f37;
}
.notif-act {
  padding: 10px 6px;
  font-family: var(--mono);
  font-size: 11px;
  color: #98a0ae;
  text-align: center;
  letter-spacing: 0.04em;
  border-right: 1px solid #2a2f37;
  cursor: pointer;
  transition: all 0.12s;
}
.notif-act:last-child { border-right: none; }
.notif-act:hover { background: #2a2f37; color: #ebeae3; }
.notif-act.primary { color: #f97316; font-weight: 600; }

/* ─── POP-OUT WINDOW ────────────────────────────────────────────── */
.popout {
  width: 480px;
  background: var(--canvas);
  border: 1px solid var(--page-rule);
  box-shadow: 0 24px 48px -10px rgba(0,0,0,0.7);
  overflow: hidden;
}
.popout-bar {
  background: var(--chrome);
  padding: 6px 12px;
  border-bottom: 1px solid var(--rule);
  display: flex;
  align-items: center;
  gap: 10px;
  font-family: var(--mono);
  font-size: 11px;
  color: var(--ink-mute);
  height: 30px;
}
.popout-bar .pin {
  color: var(--accent);
  font-family: var(--mono);
}
.popout-bar .name {
  flex: 1;
  color: var(--ink);
  letter-spacing: 0.005em;
}
.popout-bar .timer-mini {
  font-family: var(--mono);
  color: var(--accent);
  font-weight: 700;
}
.popout-bar .ctrl {
  color: var(--ink-dim);
  cursor: pointer;
}
.popout-body {
  padding: 18px 22px;
  min-height: 240px;
  font-size: 14px;
  line-height: 1.55;
  color: var(--ink);
}
.popout-body .p { font-size: 14px; margin-bottom: 12px; }
.popout-body .b { margin: 14px 0; }

/* ─── BLOCK + EMBED TAXONOMY ────────────────────────────────────── */
.tax-grid {
  display: grid;
  grid-template-columns: repeat(auto-fit, minmax(280px, 1fr));
  gap: 1px;
  background: var(--rule);
  border: 1px solid var(--rule);
  background-color: var(--canvas);
}
.tax {
  padding: 22px 24px 26px;
  background: var(--canvas);
}
.tax-icn {
  font-family: var(--mono);
  font-size: 24px;
  font-weight: 700;
  margin-bottom: 14px;
  line-height: 1;
}
.tax-name {
  font-family: var(--mono);
  font-size: 11px;
  letter-spacing: 0.2em;
  text-transform: uppercase;
  font-weight: 700;
  margin-bottom: 4px;
}
.tax-key {
  font-family: var(--mono);
  font-size: 10px;
  color: var(--ink-dim);
  margin-bottom: 14px;
  letter-spacing: 0.04em;
}
.tax-key kbd {
  background: var(--rule);
  color: var(--ink);
  padding: 1px 5px;
  margin: 0 1px;
}
.tax-desc {
  font-size: 13px;
  color: var(--ink-mute);
  margin-bottom: 16px;
  line-height: 1.5;
}
.tax-eg {
  font-family: var(--mono);
  font-size: 11px;
  color: var(--ink);
  background: var(--canvas-soft);
  padding: 10px 12px;
  border-left: 2px solid currentColor;
  line-height: 1.55;
  letter-spacing: 0.005em;
}
.tax-dec { color: var(--decision); }
.tax-act { color: var(--action); }
.tax-q   { color: var(--question); }
.tax-r   { color: var(--risk); }
.tax-quo { color: var(--quote); }
.tax-txt { color: var(--ink-mute); }

.tax-dec .tax-eg,
.tax-act .tax-eg,
.tax-q .tax-eg,
.tax-r .tax-eg,
.tax-quo .tax-eg {
  color: var(--ink);
}

/* ─── RIGHT-CLICK MENU PREVIEW ──────────────────────────────────── */
.ctx {
  background: var(--canvas);
  border: 1px solid var(--rule);
  width: 280px;
  box-shadow: 0 12px 28px -6px rgba(0,0,0,0.5);
  font-family: var(--sans);
  font-size: 12.5px;
  color: var(--ink);
  padding: 4px 0;
}
.ctx-row {
  padding: 6px 14px;
  display: flex;
  align-items: center;
  gap: 12px;
  cursor: pointer;
}
.ctx-row:hover { background: var(--accent-soft); color: var(--accent); }
.ctx-icn {
  font-family: var(--mono);
  width: 16px;
  text-align: center;
  font-size: 12px;
  color: var(--ink-dim);
}
.ctx-row:hover .ctx-icn { color: var(--accent); }
.ctx-name { flex: 1; }
.ctx-kbd {
  font-family: var(--mono);
  font-size: 10px;
  color: var(--ink-dim);
  letter-spacing: 0.04em;
}
.ctx-sep {
  height: 1px;
  background: var(--rule);
  margin: 4px 0;
}
.ctx-sub {
  font-family: var(--mono);
  font-size: 10px;
  text-transform: uppercase;
  letter-spacing: 0.16em;
  color: var(--ink-dim);
  padding: 8px 14px 4px;
  font-weight: 700;
}

.ctx-row.dec { color: var(--decision); }
.ctx-row.act { color: var(--action); }
.ctx-row.q   { color: var(--question); }
.ctx-row.r   { color: var(--risk); }
.ctx-row.quo { color: var(--quote); }
.ctx-row.dec .ctx-icn { color: var(--decision); }
.ctx-row.act .ctx-icn { color: var(--action); }
.ctx-row.q   .ctx-icn { color: var(--question); }
.ctx-row.r   .ctx-icn { color: var(--risk); }
.ctx-row.quo .ctx-icn { color: var(--quote); }

/* ─── DISCOVERY MATRIX ──────────────────────────────────────────── */
.discovery {
  display: grid;
  grid-template-columns: 1fr 1fr;
  gap: 24px;
}
.discovery-block {
  background: var(--page-bg-2);
  border: 1px solid var(--page-rule);
  padding: 24px 26px 28px;
}
.discovery-eye {
  font-family: var(--mono);
  font-size: 10px;
  color: var(--page-accent);
  letter-spacing: 0.18em;
  text-transform: uppercase;
  margin-bottom: 14px;
}
.discovery-title {
  font-family: var(--serif);
  font-variation-settings: 'opsz' 36, 'wght' 460;
  font-size: 22px;
  letter-spacing: -0.012em;
  color: var(--page-ink);
  margin-bottom: 16px;
  line-height: 1.2;
}
.discovery-list {
  list-style: none;
  font-size: 13px;
  line-height: 1.55;
  color: var(--page-mute);
}
.discovery-list li {
  padding: 6px 0;
  border-top: 1px solid var(--page-rule);
  display: grid;
  grid-template-columns: 16px 1fr;
  gap: 12px;
}
.discovery-list li:first-child { border-top: none; padding-top: 0; }
.discovery-list .num {
  font-family: var(--mono);
  font-size: 10px;
  color: var(--page-accent);
  letter-spacing: 0.04em;
}
.discovery-list strong { color: var(--page-ink); font-weight: 500; }

/* ─── KEYBOARD TABLE ────────────────────────────────────────────── */
.kbd-table {
  background: var(--page-bg-2);
  border: 1px solid var(--page-rule);
  font-family: var(--mono);
  font-size: 12px;
  width: 100%;
}
.kbd-row {
  padding: 9px 18px;
  display: grid;
  grid-template-columns: 1fr auto;
  align-items: center;
  border-top: 1px solid var(--page-rule);
  color: var(--page-mute);
}
.kbd-row:first-child { border-top: none; }
.kbd-row strong { color: var(--page-ink); font-weight: 500; }
.kbd-row kbd {
  background: var(--page-bg);
  color: var(--page-accent);
  padding: 3px 8px;
  border: 1px solid var(--page-rule);
  font-family: var(--mono);
  letter-spacing: 0.04em;
}
.kbd-section {
  padding: 10px 18px 6px;
  font-size: 10px;
  letter-spacing: 0.2em;
  text-transform: uppercase;
  color: var(--page-accent);
  background: var(--page-bg);
  border-top: 1px solid var(--page-rule);
  font-weight: 700;
}

/* ─── FOOTER ────────────────────────────────────────────────────── */
.review-foot {
  margin-top: 96px;
  padding: 48px 0 0;
  border-top: 1px solid var(--page-rule);
  font-family: var(--mono);
  font-size: 11px;
  color: var(--page-dim);
  letter-spacing: 0.05em;
  text-align: center;
  text-transform: uppercase;
}

/* ─── UTILITY ────────────────────────────────────────────────────── */
.row { display: flex; gap: 32px; align-items: flex-start; flex-wrap: wrap; }
.col { display: flex; flex-direction: column; gap: 24px; }
.label-mini {
  font-family: var(--mono);
  font-size: 10px;
  color: var(--page-dim);
  letter-spacing: 0.18em;
  text-transform: uppercase;
  margin-bottom: 8px;
}

/* ─── RESPONSIVE ───────────────────────────────────────────────── */
@media (max-width: 920px) {
  body { padding: 32px 14px 64px; }
  .surface-head { grid-template-columns: 1fr; align-items: start; }
  .surface-title { font-size: 28px; }
  .canvas { padding: 36px 28px 60px; }
  .canvas-empty { padding: 60px 20px; }
  .timer { right: 14px; top: 14px; }
  .empty-recent { min-width: 100%; }
}
)CSS");
}

} // namespace NotesTemplate
