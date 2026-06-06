// SPDX-License-Identifier: GPL-3.0-or-later
//
// Noter AI-Extract marked-region apply layer test (v0.1.112).
//
// Headless: QGuiApplication only (QTextDocument needs QtGui, not
// Widgets), QT_QPA_PLATFORM=offscreen via the ctest ENVIRONMENT.
//
// THE load-bearing case is the round-trip regression: writeRegion →
// toHtml → the REAL NotesStorage::sanitizeBody (same body-region
// transform saveNote applies) → setHtml into a fresh document →
// findExtractRegion still finds the region (the reloaded anchor name
// re-attaches to the FIRST CHARACTER fragment — the Qt 5.15.13 quirk) →
// replace-in-place → second sanitize+reload still found. If the anchors
// ever stop surviving that pipeline, re-runs silently degrade to the old
// stacking behavior.

#include "src/notes_extract_apply.h"
#include "src/notes_storage.h"
#include "src/notes_sweep_prompt.h"

#include <QDateTime>
#include <QGuiApplication>
#include <QRegularExpression>
#include <QTextBlock>
#include <QTextCursor>
#include <QTextDocument>

#include <cstdio>

static int g_pass = 0, g_fail = 0;
#define EXPECT(label, cond) \
    do { if (cond) { ++g_pass; std::printf("  [PASS] %s\n", label); } \
         else      { ++g_fail; std::printf("  [FAIL] %s\n", label); } } while (0)

using namespace NoterExtractApply;

// ─────────────────────────────────────────────────────────────────────
// Shared fixtures
// ─────────────────────────────────────────────────────────────────────

static NoterSweepPrompt::SweepResult fullResult() {
    NoterSweepPrompt::SweepResult r;
    r.summary = QStringLiteral("Quick sync about the build and follow-ups.");
    NoterSweepPrompt::SweepResult::Item a;
    a.text  = QStringLiteral("Ship the build");
    a.owner = QStringLiteral("@prateek");
    a.dueAt = QDateTime(QDate(2026, 12, 25), QTime(10, 0));
    r.actions << a;
    NoterSweepPrompt::SweepResult::Item d;
    d.text = QStringLiteral("Adopt the marked-region design");
    r.decisions << d;
    NoterSweepPrompt::SweepResult::Item q;
    q.text = QStringLiteral("Who owns the rollout?");
    r.questions << q;
    NoterSweepPrompt::SweepResult::Item k;
    k.text = QStringLiteral("CI capacity is tight");
    r.risks << k;
    return r;
}

// Apply (append or sig-matched replace) exactly the way the panel's
// applyExtractResultToNote decides — minus the Keep-both modal, which is
// widget territory (test_notes_panel_widget drives it).
static void applyToDoc(QTextDocument *doc,
                       const NoterSweepPrompt::SweepResult &r,
                       const QString &model, const QDateTime &when,
                       int wordsUsed = 0, int wordsTotal = 0) {
    const QVector<ExtractLine> lines =
        renderExtractLines(r, model, when, wordsUsed, wordsTotal);
    QStringList lineTexts;
    for (const auto &ln : lines)
        lineTexts << (ln.kind == ExtractLine::Check
                          ? QStringLiteral("☐ ") + ln.text : ln.text);
    const QString sig = regionSig(lineTexts);

    const Region reg = findExtractRegion(doc);
    QTextCursor cur(doc);
    cur.beginEditBlock();
    if (reg.found && regionSig(reg.innerTexts) == reg.storedSig) {
        const QSet<QString> doneKeys = collectDoneKeys(reg.innerTexts);
        cur.setPosition(reg.beginBlockPos);
        cur.setPosition(reg.endBlockPos + reg.endBlockLen - 1,
                        QTextCursor::KeepAnchor);
        cur.removeSelectedText();
        writeRegion(cur, lines, sig, doneKeys);
    } else {
        cur.movePosition(QTextCursor::End);
        cur.insertBlock();
        writeRegion(cur, lines, sig, QSet<QString>());
    }
    cur.endEditBlock();
}

// The exact body-region transform NotesStorage::saveNote applies before
// the artifact hits disk (sanitize ONLY between <body>…</body>; the head
// — including Qt's `p, li { white-space: pre-wrap; }` rule — is ours).
static QString sanitizeLikeSaveNote(const QString &fullHtml) {
    static const QRegularExpression bodyOpenRe(
        QStringLiteral("<body\\b[^>]*>"),
        QRegularExpression::CaseInsensitiveOption);
    static const QRegularExpression bodyCloseRe(
        QStringLiteral("</body\\s*>"),
        QRegularExpression::CaseInsensitiveOption);
    const QRegularExpressionMatch openM = bodyOpenRe.match(fullHtml);
    const QRegularExpressionMatch closeM = bodyCloseRe.match(fullHtml);
    if (openM.hasMatch() && closeM.hasMatch() &&
        closeM.capturedStart() > openM.capturedEnd()) {
        const int bodyStart = openM.capturedEnd();
        const int bodyEnd   = closeM.capturedStart();
        const QString body  = fullHtml.mid(bodyStart, bodyEnd - bodyStart);
        return fullHtml.left(bodyStart) + NotesStorage::sanitizeBody(body) +
               fullHtml.mid(bodyEnd);
    }
    return NotesStorage::sanitizeBody(fullHtml);
}

static int countBeginAnchors(const QString &html) {
    static const QRegularExpression re(
        QStringLiteral("name=\"np-extract-begin-[0-9a-f]{8}\""));
    int n = 0;
    QRegularExpressionMatchIterator it = re.globalMatch(html);
    while (it.hasNext()) { it.next(); ++n; }
    return n;
}

static QStringList docLines(const QTextDocument &doc) {
    QStringList out;
    for (QTextBlock b = doc.begin(); b.isValid(); b = b.next())
        out << b.text();
    return out;
}

// ─────────────────────────────────────────────────────────────────────
// 1. Marker names
// ─────────────────────────────────────────────────────────────────────

static void test_marker_names() {
    std::printf("test_marker_names\n");

    QString sig;
    EXPECT("begin name round-trips the sig",
           isBeginAnchorName(beginAnchorName(QStringLiteral("aabbccdd")), &sig) &&
               sig == QStringLiteral("aabbccdd"));
    EXPECT("end name is the fixed literal",
           kEndAnchorName == QStringLiteral("np-extract-end"));
    EXPECT("lookalike: missing sig rejected",
           !isBeginAnchorName(QStringLiteral("np-extract-begin")));
    EXPECT("lookalike: non-hex sig rejected",
           !isBeginAnchorName(QStringLiteral("np-extract-begin-ZZZZZZZZ")));
    EXPECT("lookalike: 7-hex sig rejected",
           !isBeginAnchorName(QStringLiteral("np-extract-begin-aabbccd")));
    EXPECT("lookalike: 9-hex sig rejected",
           !isBeginAnchorName(QStringLiteral("np-extract-begin-aabbccdd0")));
    EXPECT("lookalike: mangled prefix rejected",
           !isBeginAnchorName(QStringLiteral("np-extract-beginx-aabbccdd")));
    EXPECT("lookalike: end name is not a begin",
           !isBeginAnchorName(kEndAnchorName));
}

// ─────────────────────────────────────────────────────────────────────
// 2. renderExtractLines
// ─────────────────────────────────────────────────────────────────────

static void test_render_lines() {
    std::printf("test_render_lines\n");

    const auto lines = renderExtractLines(
        fullResult(), QStringLiteral("test-model"),
        QDateTime(QDate(2026, 6, 6), QTime(12, 0)), 0, 0);

    EXPECT("first line is the AI Extract heading",
           !lines.isEmpty() && lines.first().kind == ExtractLine::Heading &&
               lines.first().text == QStringLiteral("AI Extract"));

    // Section headings appear in canonical order with exact literals.
    QStringList headings;
    for (const auto &ln : lines)
        if (ln.kind == ExtractLine::Heading) headings << ln.text;
    EXPECT("headings in order: AI Extract, Summary, Action Items, "
           "Decisions, Questions, Risks",
           headings == QStringList({
               QStringLiteral("AI Extract"), QStringLiteral("Summary"),
               QStringLiteral("Action Items"), QStringLiteral("Decisions"),
               QStringLiteral("Questions"), QStringLiteral("Risks") }));

    // Action formatting byte-identical to the original writer (two
    // spaces before @owner, two before "(due …)"); no ☐ in the text —
    // writeRegion owns the marker.
    bool actionOk = false;
    for (const auto &ln : lines)
        if (ln.kind == ExtractLine::Check)
            actionOk = (ln.text == QStringLiteral(
                "Ship the build  @prateek  (due Dec 25 10:00)"));
    EXPECT("action line formatting matches the legacy writer", actionOk);

    // Bullets are body lines with the U+2022 glyph.
    bool decisionBullet = false;
    for (const auto &ln : lines)
        if (ln.kind == ExtractLine::Body &&
            ln.text == QStringLiteral("• Adopt the marked-region design"))
            decisionBullet = true;
    EXPECT("decision renders as a '• ' body bullet", decisionBullet);

    // Caption is last, with model + date + coverage.
    EXPECT("caption last: model, date, full-note coverage",
           lines.last().kind == ExtractLine::Body &&
               lines.last().text.contains(QStringLiteral("test-model")) &&
               lines.last().text.contains(QStringLiteral("2026-06-06 12:00")) &&
               lines.last().text.contains(QStringLiteral("full note")));

    // Truncated coverage label.
    const auto tl = renderExtractLines(
        fullResult(), QStringLiteral("m"),
        QDateTime(QDate(2026, 6, 6), QTime(12, 0)), 1200, 5000);
    EXPECT("caption carries the honest truncation coverage",
           tl.last().text.contains(QStringLiteral("first ~")) &&
               tl.last().text.contains(QStringLiteral("words")) &&
               !tl.last().text.contains(QStringLiteral("full note")));

    // Empty sections are omitted.
    NoterSweepPrompt::SweepResult slim;
    slim.summary = QStringLiteral("Just a recap.");
    const auto sl = renderExtractLines(
        slim, QStringLiteral("m"), QDateTime::currentDateTime(), 0, 0);
    QStringList slimHeadings;
    for (const auto &ln : sl)
        if (ln.kind == ExtractLine::Heading) slimHeadings << ln.text;
    EXPECT("empty sections omitted (no Action Items/Decisions/… headings)",
           slimHeadings == QStringList({ QStringLiteral("AI Extract"),
                                         QStringLiteral("Summary") }));
}

// ─────────────────────────────────────────────────────────────────────
// 3. regionSig
// ─────────────────────────────────────────────────────────────────────

static void test_region_sig() {
    std::printf("test_region_sig\n");

    const QStringList base = {
        QStringLiteral("AI Extract"),
        QStringLiteral("Summary"),
        QStringLiteral("Quick sync."),
        QString(),
        QStringLiteral("☐ Ship the build  @prateek  (due Dec 25 10:00)"),
        QStringLiteral("Extracted by m · 2026-06-06 12:00 · full note"),
    };
    const QString sig = regionSig(base);
    EXPECT("sig is 8 lowercase hex chars",
           QRegularExpression(QStringLiteral("^[0-9a-f]{8}$"))
               .match(sig).hasMatch());
    EXPECT("sig deterministic", regionSig(base) == sig);

    // ✓→☐ toggle is NOT an edit.
    QStringList toggled = base;
    toggled[4] = QStringLiteral("✓ Ship the build  @prateek  (due Dec 25 10:00)");
    EXPECT("checkbox toggle keeps the sig", regionSig(toggled) == sig);

    // One-character text edit IS an edit.
    QStringList edited = base;
    edited[2] = QStringLiteral("Quick sync!");
    EXPECT("one-char edit changes the sig", regionSig(edited) != sig);

    // Blank lines are layout, not content.
    QStringList blanky = base;
    blanky.insert(1, QString());
    blanky << QStringLiteral("   ");
    EXPECT("blank lines ignored by the sig", regionSig(blanky) == sig);

    // Whitespace-run collapse — the HTML round trip must never read as
    // an edit even if space runs get rewritten.
    QStringList spaced = base;
    spaced[4] = QStringLiteral("☐ Ship the build @prateek (due Dec 25 10:00)");
    EXPECT("internal whitespace runs collapse in the sig",
           regionSig(spaced) == sig);
}

// ─────────────────────────────────────────────────────────────────────
// 4. actionKey / collectDoneKeys
// ─────────────────────────────────────────────────────────────────────

static void test_action_key() {
    std::printf("test_action_key\n");

    EXPECT("strips ✓ prefix + owner + due",
           actionKey(QStringLiteral("✓ ship the build")) ==
               actionKey(QStringLiteral(
                   "Ship the build  @alice  (due Jun 9 10:00)")));
    EXPECT("strips ☐ prefix",
           actionKey(QStringLiteral("☐ Email the vendor")) ==
               actionKey(QStringLiteral("Email the vendor")));
    EXPECT("changed due time keeps the key",
           actionKey(QStringLiteral("Ship the build  (due Jun 9 10:00)")) ==
               actionKey(QStringLiteral("Ship the build  (due Dec 25 09:30)")));
    EXPECT("different tasks differ",
           actionKey(QStringLiteral("Ship the build")) !=
               actionKey(QStringLiteral("Sink the build")));

    const QSet<QString> keys = collectDoneKeys({
        QStringLiteral("AI Extract"),
        QStringLiteral("✓ Ship the build  @prateek  (due Dec 25 10:00)"),
        QStringLiteral("☐ Email the vendor"),
        QStringLiteral("• not a checkbox"),
    });
    EXPECT("collectDoneKeys keeps only ✓ lines",
           keys.size() == 1 &&
               keys.contains(actionKey(QStringLiteral("Ship the build"))));
}

// ─────────────────────────────────────────────────────────────────────
// 5. Fresh apply + region scan
// ─────────────────────────────────────────────────────────────────────

static void test_fresh_apply() {
    std::printf("test_fresh_apply\n");

    QTextDocument doc;
    applyToDoc(&doc, fullResult(), QStringLiteral("test-model"),
               QDateTime(QDate(2026, 6, 6), QTime(12, 0)));

    const QString html = doc.toHtml();
    EXPECT("toHtml carries the begin anchor", countBeginAnchors(html) == 1);
    EXPECT("toHtml carries the end anchor",
           html.contains(QStringLiteral("name=\"np-extract-end\"")));

    const Region reg = findExtractRegion(&doc);
    EXPECT("region found on the fresh document", reg.found);
    EXPECT("region spans begin..end in order",
           reg.found && reg.beginBlockPos < reg.endBlockPos);
    QString expectSig;
    {
        QStringList lineTexts;
        for (const auto &ln : renderExtractLines(
                 fullResult(), QStringLiteral("test-model"),
                 QDateTime(QDate(2026, 6, 6), QTime(12, 0)), 0, 0))
            lineTexts << (ln.kind == ExtractLine::Check
                              ? QStringLiteral("☐ ") + ln.text : ln.text);
        expectSig = regionSig(lineTexts);
    }
    EXPECT("storedSig equals the rendered-content sig",
           reg.storedSig == expectSig);
    EXPECT("region content sig matches storedSig (untouched)",
           regionSig(reg.innerTexts) == reg.storedSig);

    // All four sections actually persisted as document lines.
    const QStringList lines = docLines(doc);
    EXPECT("Decisions heading persisted",
           lines.contains(QStringLiteral("Decisions")));
    EXPECT("Questions heading persisted",
           lines.contains(QStringLiteral("Questions")));
    EXPECT("Risks heading persisted",
           lines.contains(QStringLiteral("Risks")));
    EXPECT("action persisted as an interactive ☐ line",
           lines.contains(QStringLiteral(
               "☐ Ship the build  @prateek  (due Dec 25 10:00)")));

    // Heading levels are REAL h2 (widget test 32's contract).
    bool summaryH2 = false, actionsH2 = false, wrapperH2 = false;
    for (QTextBlock b = doc.begin(); b.isValid(); b = b.next()) {
        if (b.blockFormat().headingLevel() != 2) continue;
        if (b.text() == QStringLiteral("Summary")) summaryH2 = true;
        if (b.text() == QStringLiteral("Action Items")) actionsH2 = true;
        if (b.text() == QStringLiteral("AI Extract")) wrapperH2 = true;
    }
    EXPECT("Summary is a real h2", summaryH2);
    EXPECT("Action Items is a real h2", actionsH2);
    EXPECT("AI Extract wrapper is a real h2", wrapperH2);

    // The markers are INVISIBLE — no marker text in the plain text.
    EXPECT("no visible marker text in the document",
           !doc.toPlainText().contains(QStringLiteral("np-extract")));

    // Degenerate docs: no anchors / begin-only / end-before-begin.
    QTextDocument empty;
    EXPECT("no anchors → not found", !findExtractRegion(&empty).found);

    QTextDocument beginOnly;
    {
        QTextCursor c(&beginOnly);
        QTextCharFormat f;
        f.setAnchor(true);
        f.setAnchorNames({ beginAnchorName(QStringLiteral("aabbccdd")) });
        c.insertText(QStringLiteral("orphan"), f);
    }
    EXPECT("begin-without-end → not found",
           !findExtractRegion(&beginOnly).found);

    QTextDocument reversed;
    {
        QTextCursor c(&reversed);
        QTextCharFormat fe;
        fe.setAnchor(true);
        fe.setAnchorNames({ kEndAnchorName });
        c.insertText(QStringLiteral("end first"), fe);
        c.insertBlock();
        QTextCharFormat fb;
        fb.setAnchor(true);
        fb.setAnchorNames({ beginAnchorName(QStringLiteral("aabbccdd")) });
        c.insertText(QStringLiteral("begin late"), fb);
    }
    EXPECT("end-before-begin → not found",
           !findExtractRegion(&reversed).found);
}

// ─────────────────────────────────────────────────────────────────────
// 6. THE round-trip regression — real sanitizer in the loop
// ─────────────────────────────────────────────────────────────────────

static void test_sanitize_roundtrip() {
    std::printf("test_sanitize_roundtrip\n");

    QTextDocument doc;
    {
        // Seed user content above the region.
        QTextCursor c(&doc);
        c.insertText(QStringLiteral("Pre-existing user paragraph."));
    }
    applyToDoc(&doc, fullResult(), QStringLiteral("test-model"),
               QDateTime(QDate(2026, 6, 6), QTime(12, 0)));

    // Cycle 1: editor → sanitizer → fresh document (the save+reload path).
    const QString disk1 = sanitizeLikeSaveNote(doc.toHtml());
    EXPECT("sanitized artifact keeps the begin anchor",
           countBeginAnchors(disk1) == 1);
    EXPECT("sanitized artifact keeps the end anchor",
           disk1.contains(QStringLiteral("name=\"np-extract-end\"")));

    QTextDocument doc2;
    doc2.setHtml(disk1);
    const Region reg2 = findExtractRegion(&doc2);
    EXPECT("region found after sanitize+reload (first-char quirk)",
           reg2.found);
    EXPECT("reloaded region still sig-matches (replace-in-place ready)",
           reg2.found && regionSig(reg2.innerTexts) == reg2.storedSig);
    EXPECT("user paragraph survived cycle 1",
           docLines(doc2).contains(
               QStringLiteral("Pre-existing user paragraph.")));

    // Replace in place on the RELOADED document.
    NoterSweepPrompt::SweepResult v2 = fullResult();
    v2.actions[0].text = QStringLiteral("Ship the build v2");
    applyToDoc(&doc2, v2, QStringLiteral("test-model"),
               QDateTime(QDate(2026, 6, 7), QTime(9, 0)));
    EXPECT("re-apply on reloaded doc replaced, not stacked",
           countBeginAnchors(doc2.toHtml()) == 1);
    EXPECT("v2 item present after replace",
           doc2.toPlainText().contains(QStringLiteral("Ship the build v2")));
    EXPECT("v1 item gone after replace",
           !doc2.toPlainText().contains(QStringLiteral(
               "Ship the build  @prateek")));

    // Cycle 2: sanitize + reload again — still one healthy region.
    QTextDocument doc3;
    doc3.setHtml(sanitizeLikeSaveNote(doc2.toHtml()));
    const Region reg3 = findExtractRegion(&doc3);
    EXPECT("region survives a second sanitize+reload", reg3.found);
    EXPECT("second reload still sig-matches",
           reg3.found && regionSig(reg3.innerTexts) == reg3.storedSig);
    EXPECT("user paragraph survived both cycles",
           docLines(doc3).contains(
               QStringLiteral("Pre-existing user paragraph.")));
}

// ─────────────────────────────────────────────────────────────────────
// 7. Idempotency — re-runs replace, never stack, never bloat
// ─────────────────────────────────────────────────────────────────────

static void test_idempotency() {
    std::printf("test_idempotency\n");

    QTextDocument doc;
    {
        QTextCursor c(&doc);
        c.insertText(QStringLiteral("Paragraph ABOVE the region."));
    }
    applyToDoc(&doc, fullResult(), QStringLiteral("m"),
               QDateTime(QDate(2026, 6, 6), QTime(12, 0)));

    // A paragraph BELOW the region (clean plain format, the way the
    // panel's append path leaves the cursor).
    {
        QTextCursor c(&doc);
        c.movePosition(QTextCursor::End);
        c.setCharFormat(QTextCharFormat());
        c.insertBlock();
        c.insertText(QStringLiteral("Paragraph BELOW the region."),
                     QTextCharFormat());
    }

    const int blocksAfterFirst = doc.blockCount();
    const QStringList beforeLines = docLines(doc);

    // Three sig-matched re-runs with identical content → byte-stable.
    for (int run = 0; run < 3; ++run)
        applyToDoc(&doc, fullResult(), QStringLiteral("m"),
                   QDateTime(QDate(2026, 6, 6), QTime(12, 0)));

    EXPECT("3 re-runs keep the block count stable",
           doc.blockCount() == blocksAfterFirst);
    EXPECT("3 re-runs keep exactly one begin anchor",
           countBeginAnchors(doc.toHtml()) == 1);
    EXPECT("3 re-runs leave every line byte-identical",
           docLines(doc) == beforeLines);
    EXPECT("paragraph above intact",
           docLines(doc).first() ==
               QStringLiteral("Paragraph ABOVE the region."));
    EXPECT("paragraph below intact",
           docLines(doc).last() ==
               QStringLiteral("Paragraph BELOW the region."));

    // A re-run with NEW content still replaces in place (sig embedded in
    // the anchor matches the OLD content, which is untouched).
    NoterSweepPrompt::SweepResult v2 = fullResult();
    v2.actions[0].text = QStringLiteral("Totally new action");
    applyToDoc(&doc, v2, QStringLiteral("m"),
               QDateTime(QDate(2026, 6, 8), QTime(8, 0)));
    EXPECT("new-content re-run: still one region",
           countBeginAnchors(doc.toHtml()) == 1);
    EXPECT("new-content re-run: block count still stable",
           doc.blockCount() == blocksAfterFirst);
    EXPECT("new-content re-run: new item in, old item out",
           doc.toPlainText().contains(QStringLiteral("Totally new action")) &&
               !doc.toPlainText().contains(QStringLiteral("Ship the build")));
}

// ─────────────────────────────────────────────────────────────────────
// 8. Done-state carry across a replace
// ─────────────────────────────────────────────────────────────────────

static void test_done_carry() {
    std::printf("test_done_carry\n");

    QTextDocument doc;
    applyToDoc(&doc, fullResult(), QStringLiteral("m"),
               QDateTime(QDate(2026, 6, 6), QTime(12, 0)));

    // User marks the action done — the exact 2-char toggle the click
    // handler performs.
    bool toggled = false;
    for (QTextBlock b = doc.begin(); b.isValid(); b = b.next()) {
        if (!b.text().startsWith(QStringLiteral("☐ Ship the build")))
            continue;
        QTextCursor c(&doc);
        c.setPosition(b.position());
        c.movePosition(QTextCursor::Right, QTextCursor::KeepAnchor, 2);
        c.insertText(QStringLiteral("✓ "));
        toggled = true;
        break;
    }
    EXPECT("flipped the action to ✓", toggled);

    // Re-run with a CHANGED owner + due — the key must still match.
    NoterSweepPrompt::SweepResult v2 = fullResult();
    v2.actions[0].owner = QStringLiteral("@prateek");
    v2.actions[0].dueAt = QDateTime(QDate(2026, 12, 31), QTime(9, 30));
    applyToDoc(&doc, v2, QStringLiteral("m"),
               QDateTime(QDate(2026, 6, 7), QTime(9, 0)));

    EXPECT("replace happened (one region)",
           countBeginAnchors(doc.toHtml()) == 1);
    bool carried = false;
    for (QTextBlock b = doc.begin(); b.isValid(); b = b.next())
        if (b.text() == QStringLiteral(
                "✓ Ship the build  @prateek  (due Dec 31 09:30)"))
            carried = true;
    EXPECT("done-state carried onto the re-written line (new due kept)",
           carried);
}

// ─────────────────────────────────────────────────────────────────────
// 9. Edited region → sig mismatch (the panel's Keep-both trigger);
//    deleted markers → safe append
// ─────────────────────────────────────────────────────────────────────

static void test_edit_detection_and_marker_loss() {
    std::printf("test_edit_detection_and_marker_loss\n");

    QTextDocument doc;
    applyToDoc(&doc, fullResult(), QStringLiteral("m"),
               QDateTime(QDate(2026, 6, 6), QTime(12, 0)));

    // Edit a body line INSIDE the region.
    for (QTextBlock b = doc.begin(); b.isValid(); b = b.next()) {
        if (b.text() != QStringLiteral(
                "Quick sync about the build and follow-ups.")) continue;
        QTextCursor c(&doc);
        c.setPosition(b.position() + b.length() - 1);
        c.insertText(QStringLiteral(" EDITED"));
        break;
    }
    const Region reg = findExtractRegion(&doc);
    EXPECT("edited region still found", reg.found);
    EXPECT("edited region sig MISMATCHES (Keep-both trigger)",
           reg.found && regionSig(reg.innerTexts) != reg.storedSig);

    // Delete the first character of the "AI Extract" heading — the
    // anchor goes with it → safe append-mode (status quo, no data loss).
    QTextDocument doc2;
    applyToDoc(&doc2, fullResult(), QStringLiteral("m"),
               QDateTime(QDate(2026, 6, 6), QTime(12, 0)));
    for (QTextBlock b = doc2.begin(); b.isValid(); b = b.next()) {
        if (b.text() != QStringLiteral("AI Extract")) continue;
        QTextCursor c(&doc2);
        c.setPosition(b.position());
        c.movePosition(QTextCursor::Right, QTextCursor::KeepAnchor, 1);
        c.removeSelectedText();
        break;
    }
    // NOTE: deleting the first char removes the 1-char-fragment carrier
    // only on RELOADED regions; on a fresh region the name sits on the
    // whole fragment, so nuke the whole heading text instead.
    for (QTextBlock b = doc2.begin(); b.isValid(); b = b.next()) {
        if (b.text() != QStringLiteral("I Extract")) continue;
        QTextCursor c(&doc2);
        c.setPosition(b.position());
        c.movePosition(QTextCursor::EndOfBlock, QTextCursor::KeepAnchor);
        c.removeSelectedText();
        break;
    }
    EXPECT("begin marker destroyed → region not found (append-mode next)",
           !findExtractRegion(&doc2).found);
    applyToDoc(&doc2, fullResult(), QStringLiteral("m"),
               QDateTime(QDate(2026, 6, 6), QTime(12, 0)));
    EXPECT("append recovery: a fresh healthy region exists",
           findExtractRegion(&doc2).found);
}

// ─────────────────────────────────────────────────────────────────────
// 10. Two regions after a Keep-both → the LAST pair is the target
// ─────────────────────────────────────────────────────────────────────

static void test_two_regions_last_wins() {
    std::printf("test_two_regions_last_wins\n");

    QTextDocument doc;
    applyToDoc(&doc, fullResult(), QStringLiteral("m"),
               QDateTime(QDate(2026, 6, 6), QTime(12, 0)));
    // Simulate Keep-both: force-append a second region with different
    // content (bypass the sig check by appending directly).
    NoterSweepPrompt::SweepResult v2 = fullResult();
    v2.actions[0].text = QStringLiteral("Second region action");
    {
        const auto lines = renderExtractLines(
            v2, QStringLiteral("m"), QDateTime(QDate(2026, 6, 7), QTime(9, 0)),
            0, 0);
        QStringList lineTexts;
        for (const auto &ln : lines)
            lineTexts << (ln.kind == ExtractLine::Check
                              ? QStringLiteral("☐ ") + ln.text : ln.text);
        QTextCursor cur(&doc);
        cur.movePosition(QTextCursor::End);
        cur.insertBlock();
        writeRegion(cur, lines, regionSig(lineTexts), QSet<QString>());
    }
    EXPECT("two begin anchors after Keep-both",
           countBeginAnchors(doc.toHtml()) == 2);

    const Region reg = findExtractRegion(&doc);
    EXPECT("scan picks a region", reg.found);
    EXPECT("scan picked the LAST region (second action inside)",
           reg.found &&
               reg.innerTexts.join(QLatin1Char('\n'))
                   .contains(QStringLiteral("Second region action")));

    // A sig-matched re-run replaces ONLY the last region.
    NoterSweepPrompt::SweepResult v3 = fullResult();
    v3.actions[0].text = QStringLiteral("Third version action");
    applyToDoc(&doc, v3, QStringLiteral("m"),
               QDateTime(QDate(2026, 6, 8), QTime(9, 0)));
    EXPECT("still two regions after the targeted replace",
           countBeginAnchors(doc.toHtml()) == 2);
    EXPECT("first region untouched (its action still present)",
           doc.toPlainText().contains(QStringLiteral("Ship the build")));
    EXPECT("last region replaced (v3 in, v2 out)",
           doc.toPlainText().contains(QStringLiteral("Third version action")) &&
               !doc.toPlainText().contains(QStringLiteral("Second region action")));
}

// ─────────────────────────────────────────────────────────────────────
// 11. Hostile name-attr vectors through the REAL sanitizer
// ─────────────────────────────────────────────────────────────────────

static void test_hostile_name_attrs() {
    std::printf("test_hostile_name_attrs\n");

    const QString kept = NotesStorage::sanitizeBody(QStringLiteral(
        "<p><a name=\"np-extract-begin-aabbccdd\"></a>x</p>"));
    EXPECT("prefix-gated <a name> survives",
           kept.contains(QStringLiteral("name=\"np-extract-begin-aabbccdd\"")));

    EXPECT("non-prefixed <a name> drops",
           !NotesStorage::sanitizeBody(QStringLiteral(
                "<p><a name=\"evil\"></a>x</p>"))
                .contains(QStringLiteral("name=")));
    EXPECT("<img name> drops the attr even with the prefix",
           !NotesStorage::sanitizeBody(QStringLiteral(
                "<p><img name=\"np-extract-begin-aabbccdd\" /></p>"))
                .contains(QStringLiteral("name=")));
    EXPECT("<div name> drops",
           !NotesStorage::sanitizeBody(QStringLiteral(
                "<div name=\"np-extract-end\">x</div>"))
                .contains(QStringLiteral("name=")));
    const QString combo = NotesStorage::sanitizeBody(QStringLiteral(
        "<p><a name=\"np-extract-end\" onclick=\"alert(1)\" "
        "style=\"color:red\"></a>x</p>"));
    EXPECT("name + onclick + style: name kept, handlers/style stripped",
           combo.contains(QStringLiteral("name=\"np-extract-end\"")) &&
               !combo.contains(QStringLiteral("onclick")) &&
               !combo.contains(QStringLiteral("style")));
}

// ─────────────────────────────────────────────────────────────────────

int main(int argc, char *argv[]) {
    QGuiApplication app(argc, argv);

    test_marker_names();
    test_render_lines();
    test_region_sig();
    test_action_key();
    test_fresh_apply();
    test_sanitize_roundtrip();
    test_idempotency();
    test_done_carry();
    test_edit_detection_and_marker_loss();
    test_two_regions_last_wins();
    test_hostile_name_attrs();

    std::printf("\n[%d passed, %d failed]\n", g_pass, g_fail);
    return g_fail == 0 ? 0 : 1;
}
