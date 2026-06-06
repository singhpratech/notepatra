// SPDX-License-Identifier: GPL-3.0-or-later
//
// Test suite for src/notes_storage.{h,cpp}.
//
// Uses QtTest (request from the spec). The acceptance criteria expect
// every assert to pass under `ctest`.
//
// Layout: one QObject (TestNotesStorage) with one private slot per
// test, registered in `private slots:`. QTEST_MAIN macro wires up
// main(). Each test creates its own QTemporaryDir to avoid cross-test
// state.

#include "notes_storage.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QObject>
#include <QtTest/QSignalSpy>
#include <QString>
#include <QStringList>
#include <QTemporaryDir>
#include <QThread>
#include <QtTest>

// Real NotesTemplate::shellHtml linked from src/notes_template.cpp.

class TestNotesStorage : public QObject {
    Q_OBJECT

private slots:
    // ── round-trip ──
    void roundTrip_saveThenRead_returnsIdentical();

    // ── atomic write ──
    void atomicWrite_corruptedTmpLeftBehind_doesntBreakRead();
    void atomicWrite_signalEmittedOnSuccess();

    // ── backup ring ──
    void backupRing_fiveSaves_produceBak1ThroughBak5();
    void backupRing_sixthSave_dropsOldest();
    void backupRing_customDepth_respected();

    // ── sanitizer ──
    void sanitizer_xssVectors_neutralized();
    void sanitizer_allowedTags_preserved();
    void sanitizer_dataUrlImageAllowed_otherDataUrlDropped();
    void sanitizer_unknownTag_droppedButTextKept();
    void sanitizer_targetBlank_addsRelNoopener();
    void sanitizer_mismatchedTags_autoClosed();
    void sanitizer_eventHandlerAttributes_stripped();
    void sanitizer_inlineStyleAttribute_stripped();
    void sanitizer_headingTags_preserved();
    void roundTrip_savedHeading_keepsH2();
    // v0.1.112 — Noter AI-Extract region markers: `name` allowed ONLY on
    // <a> and ONLY with the np-extract- prefix.
    void sanitizer_extractAnchorName_keptOnPrefixedA();
    void sanitizer_extractAnchorName_hostileVariantsDropped();
    void roundTrip_extractAnchors_surviveSaveRead();

    // ── filename safety ──
    void filename_twentyEdgeCases();

    // ── listAllNotes filter ──
    // (The per-PID .lock tests that used to sit here were removed in A7
    //  together with the lock API itself — it was dead code with zero
    //  production callers. listAllNotes_filtersOutSidecars still proves
    //  legacy *.lock files are filtered from listings.)
    void listAllNotes_filtersOutSidecars();
    void listAllNotes_sortedByMtimeDesc();

    // ── validate-before-write ──
    void validate_malformedBodyHtml_refused();
    void validate_namedEntities_accepted();

    // ── draft sidecar ──
    void draft_writeAndClear();
    void draft_clearedOnSuccessfulSave();

    // ── plainTextForSearch (v0.1.112 body-content search) ──
    void plainText_stripsTags();
    void plainText_stripsHeadAndTitle();
    void plainText_stripsStyleScriptInBody();
    void plainText_entityOrderAmpLast();
    void plainText_whitespaceCollapsed();
    void plainText_attributesNeverLeak();
    void plainText_casePreserved();
    void plainText_emptyAndTagOnly();
    // ── durability budget (A7) ──
    void durability_oneMbSave_latencyBudget();
    // ── title identity (notepatra-title meta = display-title SSOT) ──
    void title_withTitleMeta_insertionShapes();
    void title_metaRoundTrip_hostileAndUnicode();
    void title_prettyTitleFromFilename_parityTable();
    void title_titleInfo_precedenceAndHeuristic();
    void title_cache_invalidationAndSaveNote();
    void title_saveNote_preservesMetaAndDataAttrs();
};

// ═══════════════════════════════════════════════════════════════════════
// Round trip
// ═══════════════════════════════════════════════════════════════════════

void TestNotesStorage::roundTrip_saveThenRead_returnsIdentical() {
    QTemporaryDir td;
    QVERIFY(td.isValid());
    NotesStorage s(td.path());

    const QString path = td.path() + "/note.html";
    const QString html =
        "<!doctype html><html><head><title>x</title></head>"
        "<body><h1>Hello</h1><p>world</p></body></html>";

    QString err;
    QVERIFY2(s.saveNote(path, html, &err), qPrintable(err));

    QString rerr;
    const QString back = s.readNote(path, &rerr);
    QVERIFY(rerr.isEmpty());
    QVERIFY(back.contains("<h1>Hello</h1>"));
    QVERIFY(back.contains("<p>world</p>"));
}

void TestNotesStorage::roundTrip_savedHeading_keepsH2() {
    // Full writer→reader round-trip for a section header: the body shape
    // QTextDocument::toHtml() produces for a headingLevel-2 block must come
    // back from disk still carrying the <h2> tag — the reload side derives
    // the heading look (level + bold) from it.
    QTemporaryDir td;
    QVERIFY(td.isValid());
    NotesStorage s(td.path());

    const QString path = td.path() + "/note-h2.html";
    const QString html =
        "<!doctype html><html><head><title>x</title></head>"
        "<body><h2 style=\"font-size:15pt;\">"
        "<span style=\" font-weight:600;\">Action Items</span></h2>"
        "<p>follow up</p></body></html>";

    QString err;
    QVERIFY2(s.saveNote(path, html, &err), qPrintable(err));
    const QString back = s.readNote(path, nullptr);
    QVERIFY(back.contains("<h2>"));
    QVERIFY(back.contains("Action Items"));
    QVERIFY(back.contains("</h2>"));
    QVERIFY(!back.contains("style="));
}

// ═══════════════════════════════════════════════════════════════════════
// Atomic write
// ═══════════════════════════════════════════════════════════════════════

void TestNotesStorage::atomicWrite_corruptedTmpLeftBehind_doesntBreakRead() {
    QTemporaryDir td;
    NotesStorage s(td.path());

    const QString path = td.path() + "/note.html";
    const QString html =
        "<html><head></head><body><p>canonical</p></body></html>";
    QVERIFY(s.saveNote(path, html));

    // Simulate a power-loss orphan: rogue .tmp left behind.
    QFile orphan(path + ".tmp");
    QVERIFY(orphan.open(QIODevice::WriteOnly));
    orphan.write("PARTIAL GARBAGE");
    orphan.close();

    // Read of the canonical .html must still succeed and return the
    // good content.
    const QString back = s.readNote(path);
    QVERIFY(back.contains("<p>canonical</p>"));
    QVERIFY(!back.contains("PARTIAL"));
}

void TestNotesStorage::atomicWrite_signalEmittedOnSuccess() {
    QTemporaryDir td;
    NotesStorage s(td.path());
    QSignalSpy spy(&s, &NotesStorage::noteSaved);
    const QString path = td.path() + "/note.html";
    QVERIFY(s.saveNote(path,
        "<html><body><p>x</p></body></html>"));
    QCOMPARE(spy.count(), 1);
    QCOMPARE(spy.at(0).at(0).toString(), path);
}

// ═══════════════════════════════════════════════════════════════════════
// Backup ring
// ═══════════════════════════════════════════════════════════════════════

void TestNotesStorage::backupRing_fiveSaves_produceBak1ThroughBak5() {
    QTemporaryDir td;
    NotesStorage s(td.path());
    const QString path = td.path() + "/n.html";

    for (int i = 1; i <= 6; ++i) {
        const QString html = QStringLiteral(
            "<html><body><p>v%1</p></body></html>").arg(i);
        QVERIFY(s.saveNote(path, html));
    }

    // After 6 saves: .html holds v6, .bak1 v5, .bak2 v4, … .bak5 v1.
    QString cur = s.readNote(path);
    QVERIFY(cur.contains("v6"));

    for (int i = 1; i <= 5; ++i) {
        const QString bakPath = path + QStringLiteral(".bak%1").arg(i);
        QVERIFY2(QFile::exists(bakPath), qPrintable(bakPath));
        const QString back = s.readNote(bakPath);
        // .bak1 = v5 (previous version), .bak2 = v4, …
        const int expectedVer = 6 - i;
        QVERIFY2(back.contains(QStringLiteral("v%1").arg(expectedVer)),
                 qPrintable(QStringLiteral("bak%1 expected v%2 got: %3")
                                .arg(i).arg(expectedVer).arg(back)));
    }
}

void TestNotesStorage::backupRing_sixthSave_dropsOldest() {
    QTemporaryDir td;
    NotesStorage s(td.path());
    const QString path = td.path() + "/n.html";

    // Save 7 times. Versions v1..v7. After save 7:
    //   .html  = v7
    //   .bak1  = v6
    //   .bak2  = v5
    //   .bak3  = v4
    //   .bak4  = v3
    //   .bak5  = v2
    //   v1 is dropped.
    for (int i = 1; i <= 7; ++i) {
        QVERIFY(s.saveNote(path, QStringLiteral(
            "<html><body><p>v%1</p></body></html>").arg(i)));
    }

    // No .bak6 should exist.
    QVERIFY(!QFile::exists(path + ".bak6"));
    // v1 is gone from every backup.
    for (int i = 1; i <= 5; ++i) {
        const QString bakPath = path + QStringLiteral(".bak%1").arg(i);
        const QString back = s.readNote(bakPath);
        QVERIFY(!back.contains("v1<"));
    }
}

void TestNotesStorage::backupRing_customDepth_respected() {
    QTemporaryDir td;
    NotesStorage s(td.path());
    s.setBackupRingDepth(3);
    const QString path = td.path() + "/n.html";
    for (int i = 1; i <= 5; ++i) {
        QVERIFY(s.saveNote(path, QStringLiteral(
            "<html><body><p>v%1</p></body></html>").arg(i)));
    }
    QVERIFY( QFile::exists(path + ".bak1"));
    QVERIFY( QFile::exists(path + ".bak2"));
    QVERIFY( QFile::exists(path + ".bak3"));
    // .bak4 / .bak5 should NOT exist with depth=3.
    QVERIFY(!QFile::exists(path + ".bak4"));
    QVERIFY(!QFile::exists(path + ".bak5"));
}

// ═══════════════════════════════════════════════════════════════════════
// Sanitizer — XSS vectors
// ═══════════════════════════════════════════════════════════════════════

void TestNotesStorage::sanitizer_xssVectors_neutralized() {
    struct Vec { const char *label; const char *input; const char *forbiddenSub; };
    const Vec cases[] = {
        { "raw script tag",
          "<p>hi</p><script>alert(1)</script>",                "<script" },
        { "img onerror",
          "<img src=x onerror=\"alert(1)\">",                  "onerror" },
        { "anchor javascript URL",
          "<a href=\"javascript:alert(1)\">click</a>",         "javascript:" },
        { "iframe data URL",
          "<iframe src=\"data:text/html,<script>1</script>\"></iframe>",
                                                               "<iframe" },
        { "svg onload",
          "<svg onload=alert(1)></svg>",                       "<svg" },
        { "svg embedded script",
          "<svg><script>alert(1)</script></svg>",              "<script" },
        { "style block injected in body",
          "<p>before</p><style>body{background:url(javascript:alert(1))}</style><p>after</p>",
                                                               "<style" },
        { "style attribute on div",
          "<div style=\"background:url(javascript:alert(1))\">x</div>",
                                                               "style=" },
        // Entity-encoded javascript: scheme (OWASP cheat sheet).
        { "entity-encoded js scheme",
          "<a href=\"&#106;avascript:alert(1)\">x</a>",        "javascript:" },
        // Numeric entity decode then check.
        { "tab inside scheme",
          "<a href=\"java\tscript:alert(1)\">x</a>",           "javascript:" },
        // vbscript / file: URLs.
        { "vbscript URL",
          "<a href=\"vbscript:msgbox(1)\">x</a>",              "vbscript:" },
        { "file URL",
          "<img src=\"file:///etc/passwd\">",                  "file:" },
        // onclick handler.
        { "onclick handler",
          "<div onclick=\"steal()\">x</div>",                  "onclick" },
        // object / embed.
        { "object tag",
          "<object data=\"x.swf\"></object>",                  "<object" },
        { "embed tag",
          "<embed src=\"x.swf\">",                             "<embed" },
        // form/input/button.
        { "form tag",
          "<form action=\"x\"><input name=\"y\"></form>",      "<form" },
    };

    for (const Vec &v : cases) {
        const QString sane = NotesStorage::sanitizeBody(QString::fromUtf8(v.input));
        const QString lower = sane.toLower();
        QVERIFY2(!lower.contains(QString::fromLatin1(v.forbiddenSub).toLower()),
                 qPrintable(QStringLiteral("XSS vector survived [%1]: %2")
                                .arg(v.label).arg(sane)));
    }
}

void TestNotesStorage::sanitizer_allowedTags_preserved() {
    const QString in = "<h1>Title</h1>"
                       "<p>Para with <strong>bold</strong> and <em>italic</em>.</p>"
                       "<ul><li>one</li><li>two</li></ul>"
                       "<table><thead><tr><th>k</th></tr></thead>"
                       "<tbody><tr><td>v</td></tr></tbody></table>"
                       "<blockquote>quoted</blockquote>"
                       "<pre><code>code()</code></pre>"
                       "<a href=\"https://example.com\">link</a>";
    const QString sane = NotesStorage::sanitizeBody(in);
    QVERIFY(sane.contains("<h1>"));
    QVERIFY(sane.contains("<strong>"));
    QVERIFY(sane.contains("<em>"));
    QVERIFY(sane.contains("<ul>"));
    QVERIFY(sane.contains("<table>"));
    QVERIFY(sane.contains("<thead>"));
    QVERIFY(sane.contains("<blockquote>"));
    QVERIFY(sane.contains("<pre>"));
    QVERIFY(sane.contains("<code>"));
    QVERIFY(sane.contains("href=\"https://example.com\""));
}

void TestNotesStorage::sanitizer_dataUrlImageAllowed_otherDataUrlDropped() {
    const QString ok = NotesStorage::sanitizeBody(
        "<img src=\"data:image/png;base64,iVBORw0KGgo=\">");
    QVERIFY(ok.contains("data:image/png"));

    const QString bad = NotesStorage::sanitizeBody(
        "<img src=\"data:text/html,<script>1</script>\">");
    QVERIFY(!bad.contains("data:text/html"));
}

void TestNotesStorage::sanitizer_unknownTag_droppedButTextKept() {
    const QString sane = NotesStorage::sanitizeBody(
        "<marquee>scroll</marquee><blink>blink</blink><p>after</p>");
    QVERIFY(!sane.contains("<marquee"));
    QVERIFY(!sane.contains("<blink"));
    QVERIFY(sane.contains("scroll"));
    QVERIFY(sane.contains("blink"));
    QVERIFY(sane.contains("<p>after</p>"));
}

void TestNotesStorage::sanitizer_targetBlank_addsRelNoopener() {
    const QString sane = NotesStorage::sanitizeBody(
        "<a href=\"https://x.example\" target=\"_blank\">x</a>");
    QVERIFY(sane.contains("target=\"_blank\""));
    QVERIFY(sane.contains("rel=\"noopener noreferrer\""));
}

void TestNotesStorage::sanitizer_mismatchedTags_autoClosed() {
    const QString sane = NotesStorage::sanitizeBody(
        "<div><p>unclosed paragraph");
    // Auto-closes happen in reverse open order.
    QVERIFY(sane.endsWith("</p></div>"));
}

void TestNotesStorage::sanitizer_eventHandlerAttributes_stripped() {
    const QString sane = NotesStorage::sanitizeBody(
        "<div onclick=\"x\" onmouseover=\"y\" onfocus=\"z\">hi</div>");
    QVERIFY(!sane.contains("onclick"));
    QVERIFY(!sane.contains("onmouseover"));
    QVERIFY(!sane.contains("onfocus"));
    QVERIFY(sane.contains("<div>"));
    QVERIFY(sane.contains("hi"));
}

void TestNotesStorage::sanitizer_inlineStyleAttribute_stripped() {
    const QString sane = NotesStorage::sanitizeBody(
        "<p style=\"color:red;background:url(javascript:0)\">x</p>");
    QVERIFY(!sane.contains("style="));
    QVERIFY(sane.contains("<p>"));
}

void TestNotesStorage::sanitizer_headingTags_preserved() {
    // Section headers are real <h2>/<h3> blocks now (toolbar Insert header
    // + the Extract append path). The allowlist must keep them — including
    // the form QTextDocument::toHtml() emits, where the tag carries an
    // inline style (style is stripped; the tag itself must survive).
    const QString sane = NotesStorage::sanitizeBody(
        "<h2 style=\"margin-top:12px;\">Action Items</h2>"
        "<h3>Sub section</h3>"
        "<p>body text</p>");
    QVERIFY(sane.contains("<h2>"));
    QVERIFY(sane.contains("Action Items"));
    QVERIFY(sane.contains("</h2>"));
    QVERIFY(sane.contains("<h3>"));
    QVERIFY(sane.contains("</h3>"));
    QVERIFY(!sane.contains("style="));
}

void TestNotesStorage::sanitizer_extractAnchorName_keptOnPrefixedA() {
    // v0.1.112 — the AI-Extract apply layer marks its region with
    // invisible <a name="np-extract-…"> anchors. The sanitizer must keep
    // EXACTLY that prefix-gated form so re-runs can find + replace the
    // region after a save/reload (notes_extract_apply.h).
    const QString begin = NotesStorage::sanitizeBody(
        "<p><a name=\"np-extract-begin-aabbccdd\"></a>AI Extract</p>");
    QVERIFY(begin.contains("name=\"np-extract-begin-aabbccdd\""));
    QVERIFY(begin.contains("AI Extract"));

    const QString end = NotesStorage::sanitizeBody(
        "<p><a name=\"np-extract-end\"></a>Extracted by m</p>");
    QVERIFY(end.contains("name=\"np-extract-end\""));
}

void TestNotesStorage::sanitizer_extractAnchorName_hostileVariantsDropped() {
    // Non-prefixed values drop (DOM-clobbering shaped surface stays shut).
    QVERIFY(!NotesStorage::sanitizeBody("<p><a name=\"evil\"></a>x</p>")
                 .contains("name="));
    QVERIFY(!NotesStorage::sanitizeBody("<p><a name=\"window\"></a>x</p>")
                 .contains("name="));
    // name is NOT in the global allowlist — non-<a> tags drop it even
    // with the magic prefix.
    QVERIFY(!NotesStorage::sanitizeBody(
                 "<p><img name=\"np-extract-begin-aabbccdd\" /></p>")
                 .contains("name="));
    QVERIFY(!NotesStorage::sanitizeBody(
                 "<div name=\"np-extract-end\">x</div>")
                 .contains("name="));
    // name combined with on*/style: handlers + style ALWAYS strip; the
    // prefixed name itself survives on <a>.
    const QString combo = NotesStorage::sanitizeBody(
        "<p><a name=\"np-extract-end\" onclick=\"alert(1)\" "
        "style=\"color:red\"></a>x</p>");
    QVERIFY(combo.contains("name=\"np-extract-end\""));
    QVERIFY(!combo.contains("onclick"));
    QVERIFY(!combo.contains("style"));
    QVERIFY(!combo.contains("alert"));
    // Attr-value escaping still applies to the kept name.
    const QString esc = NotesStorage::sanitizeBody(
        "<p><a name='np-extract-x\"><script>alert(1)</script>'></a>x</p>");
    QVERIFY(!esc.contains("<script"));
}

void TestNotesStorage::roundTrip_extractAnchors_surviveSaveRead() {
    // Full saveNote→readNote round trip: both region anchors must come
    // back from disk byte-intact (the replace-in-place contract).
    QTemporaryDir td;
    QVERIFY(td.isValid());
    NotesStorage s(td.path());
    const QString path = td.path() + "/extract.html";
    const QString html =
        "<html><head></head><body>"
        "<h2><a name=\"np-extract-begin-0123abcd\"></a>AI Extract</h2>"
        "<p>☐ Ship the build</p>"
        "<p><a name=\"np-extract-end\"></a>Extracted by m · full note</p>"
        "</body></html>";
    QString err;
    QVERIFY2(s.saveNote(path, html, &err), qPrintable(err));
    QString rerr;
    const QString back = s.readNote(path, &rerr);
    QVERIFY(rerr.isEmpty());
    QVERIFY(back.contains("name=\"np-extract-begin-0123abcd\""));
    QVERIFY(back.contains("name=\"np-extract-end\""));
}

// ═══════════════════════════════════════════════════════════════════════
// Filename safety — 20 edge cases
// ═══════════════════════════════════════════════════════════════════════

void TestNotesStorage::filename_twentyEdgeCases() {
    struct Case { const char *in; const char *expected; const char *label; };
    const Case cases[] = {
        // (1) Normal title
        { "Quarterly Plan",              "quarterly-plan",          "normal" },
        // (2) Slashes
        { "ops/q1/notes",                "opsq1notes",              "slashes" },
        // (3) Win32 reserved chars
        { "foo<>:\"|?*bar",              "foobar",                  "win32-reserved" },
        // (4) Accented Latin
        { "Crème Brûlée",                "creme-brulee",            "accents" },
        // (5) Cyrillic (dropped entirely)
        { "Заметка",                     "untitled",                "cyrillic" },
        // (6) Emoji
        { "Meeting 🚀 launch",           "meeting-launch",          "emoji" },
        // (7) Leading dot (hidden file)
        { ".secret",                     "secret",                  "leading-dot" },
        // (8) Trailing dot (Windows)
        { "foo.",                        "foo",                     "trailing-dot" },
        // (9) Only whitespace
        { "    \t\n  ",                  "untitled",                "only-whitespace" },
        // (10) Empty string
        { "",                            "untitled",                "empty" },
        // (11) Multiple dashes collapse
        { "a---b",                       "a-b",                     "multi-dash" },
        // (12) Mixed case
        { "MixedCASE Note",              "mixedcase-note",          "case-fold" },
        // (13) Reserved basename CON
        { "CON",                         "_con",                    "win32-CON" },
        // (14) Reserved basename with extension
        { "NUL.txt",                     "_nul.txt",                "win32-NUL.txt" },
        // (15) Embedded NUL
        { "foo\0bar",                    "foobar",                  "embedded-nul" },
        // (16) Punctuation soup
        { "Hello, world! How's it?",     "hello-world-hows-it",     "punctuation" },
        // (17) German umlaut
        { "Schöne Übung",                "schone-ubung",            "umlaut" },
        // (18) Sharp-s
        { "Straße",                      "strasse",                 "sharp-s" },
        // (19) Polish
        { "Łódź",                        "lodz",                    "polish" },
        // (20) Spanish ñ
        { "Mañana",                      "manana",                  "spanish-tilde" },
    };

    for (const Case &c : cases) {
        // Build the input — Case (15) needs the embedded NUL preserved
        // so use QByteArray with explicit length when we see "\0".
        QString in;
        if (QString::fromLatin1(c.label) == "embedded-nul") {
            QByteArray ba("foo", 3);
            ba.append('\0');
            ba.append("bar", 3);
            // fromUtf8(QByteArray) truncates at NUL on Qt 5.15; the
            // (ptr, len) overload preserves the embedded NUL so we can
            // verify the sanitizer drops it.
            in = QString::fromUtf8(ba.constData(), ba.size());
        } else {
            in = QString::fromUtf8(c.in);
        }
        const QString got = NotesStorage::safeFilename(in);
        QVERIFY(!got.isEmpty());
        QVERIFY2(got == QString::fromUtf8(c.expected),
                 qPrintable(QStringLiteral("[%1] expected '%2' got '%3'")
                                .arg(c.label).arg(c.expected).arg(got)));
    }

    // (21) >200 chars — verify cap.
    const QString huge(500, 'a');
    const QString capped = NotesStorage::safeFilename(huge);
    QVERIFY(capped.size() <= 200);
}

// ═══════════════════════════════════════════════════════════════════════
// listAllNotes filter
// ═══════════════════════════════════════════════════════════════════════

void TestNotesStorage::listAllNotes_filtersOutSidecars() {
    QTemporaryDir td;
    NotesStorage s(td.path());
    QDir(td.path()).mkpath("sub");

    auto touch = [](const QString &p) {
        QFile f(p);
        QVERIFY2(f.open(QIODevice::WriteOnly),
                 qPrintable(QStringLiteral("open %1 failed").arg(p)));
        f.write("x");
        f.close();
    };
    touch(td.path() + "/a.html");
    touch(td.path() + "/sub/b.html");
    // Sidecars — should NOT appear.
    touch(td.path() + "/a.html.bak1");
    touch(td.path() + "/a.html.bak5");
    touch(td.path() + "/a.html.tmp");
    touch(td.path() + "/a.html.draft");
    touch(td.path() + "/a.html.lock");

    const QStringList notes = s.listAllNotes();
    QCOMPARE(notes.size(), 2);
    bool foundA = false, foundB = false;
    for (const QString &p : notes) {
        if (p.endsWith("/a.html"))    foundA = true;
        if (p.endsWith("/sub/b.html")) foundB = true;
        QVERIFY(!p.endsWith(".tmp"));
        QVERIFY(!p.endsWith(".draft"));
        QVERIFY(!p.endsWith(".lock"));
        QVERIFY(!p.endsWith(".bak1"));
        QVERIFY(!p.endsWith(".bak5"));
    }
    QVERIFY(foundA);
    QVERIFY(foundB);
}

void TestNotesStorage::listAllNotes_sortedByMtimeDesc() {
    QTemporaryDir td;
    NotesStorage s(td.path());

    const QString pOld = td.path() + "/old.html";
    const QString pNew = td.path() + "/new.html";

    QVERIFY(s.saveNote(pOld, "<html><body><p>old</p></body></html>"));
    // Sleep briefly so mtime is strictly different. 1.1 s covers
    // filesystems with second-granularity mtime (HFS+, FAT).
    QThread::msleep(1100);
    QVERIFY(s.saveNote(pNew, "<html><body><p>new</p></body></html>"));

    const QStringList notes = s.listAllNotes();
    QCOMPARE(notes.size(), 2);
    QVERIFY2(notes.at(0).endsWith("/new.html"),
             qPrintable(QStringLiteral("first should be new, got %1")
                            .arg(notes.at(0))));
    QVERIFY2(notes.at(1).endsWith("/old.html"),
             qPrintable(QStringLiteral("second should be old, got %1")
                            .arg(notes.at(1))));
}

// ═══════════════════════════════════════════════════════════════════════
// Validate-before-write
// ═══════════════════════════════════════════════════════════════════════

void TestNotesStorage::validate_malformedBodyHtml_refused() {
    QTemporaryDir td;
    NotesStorage s(td.path());
    const QString path = td.path() + "/n.html";

    // An entity reference that isn't in the validator's HTML5
    // stand-in DTD and isn't numeric. The sanitizer copies text
    // verbatim (per the contract — entity decoding is the browser's
    // job for plain text), and the validator then refuses the doc.
    // This is the exact data-integrity guarantee we promise:
    // structurally broken HTML never reaches disk.
    const QString bad =
        "<html><head></head>"
        "<body><p>has an unknown entity: &totallyNotARealEntity;</p></body></html>";

    QString err;
    const bool ok = s.saveNote(path, bad, &err);
    QVERIFY2(!ok, "Malformed HTML must be refused");
    QVERIFY2(!err.isEmpty(), "errorOut must be populated on refusal");
    // The canonical file MUST NOT have been written.
    QVERIFY(!QFile::exists(path));
}

void TestNotesStorage::validate_namedEntities_accepted() {
    // &nbsp; / &copy; / &mdash; are valid HTML5 but unknown to vanilla
    // XML. The validateBody DOCTYPE preamble must wire them up so
    // perfectly normal notes don't get rejected.
    QTemporaryDir td;
    NotesStorage s(td.path());
    const QString path = td.path() + "/n.html";
    const QString good =
        "<html><head></head>"
        "<body><p>Copyright &copy; 2026 &mdash; &nbsp; spaces.</p></body></html>";
    QString err;
    QVERIFY2(s.saveNote(path, good, &err), qPrintable(err));
}

// ═══════════════════════════════════════════════════════════════════════
// Draft sidecar
// ═══════════════════════════════════════════════════════════════════════

void TestNotesStorage::draft_writeAndClear() {
    QTemporaryDir td;
    NotesStorage s(td.path());
    const QString path = td.path() + "/n.html";
    QDir().mkpath(QFileInfo(path).absolutePath());

    QVERIFY(s.writeDraft(path, "<html><body><p>draft</p></body></html>"));
    QVERIFY(QFile::exists(path + ".draft"));

    s.clearDraft(path);
    QVERIFY(!QFile::exists(path + ".draft"));
}

void TestNotesStorage::draft_clearedOnSuccessfulSave() {
    QTemporaryDir td;
    NotesStorage s(td.path());
    const QString path = td.path() + "/n.html";

    QVERIFY(s.writeDraft(path, "<html><body><p>draft</p></body></html>"));
    QVERIFY(QFile::exists(path + ".draft"));

    QVERIFY(s.saveNote(path, "<html><body><p>saved</p></body></html>"));
    QVERIFY(!QFile::exists(path + ".draft"));
}

// ═══════════════════════════════════════════════════════════════════════
// plainTextForSearch — v0.1.112 body-content search plaintext extractor.
// Pure static; QtCore only (this target links no Qt5::Gui).
// ═══════════════════════════════════════════════════════════════════════

void TestNotesStorage::plainText_stripsTags() {
    QCOMPARE(NotesStorage::plainTextForSearch(
                 QStringLiteral("<p>hello <b>world</b></p>")),
             QStringLiteral("hello world"));
}

void TestNotesStorage::plainText_stripsHeadAndTitle() {
    // A real NotesTemplate-shaped document: <head> holds the <title> plus
    // the full styleBlock CSS (font-family / --sans / serif tokens). The
    // head strip is LOAD-BEARING — without it every note matches "serif".
    QTemporaryDir td;
    NotesStorage s(td.path());
    const QString html = s.newNoteHtml(QStringLiteral("Noter 03"),
                                       QDateTime::currentDateTime(),
                                       QStringList());
    QVERIFY(html.contains(QStringLiteral("font-family")));   // fixture sanity
    const QString plain = NotesStorage::plainTextForSearch(html);
    QVERIFY(plain.contains(QStringLiteral("Noter 03")));      // body <h1> survives
    QVERIFY(!plain.contains(QStringLiteral("font-family")));
    QVERIFY(!plain.contains(QStringLiteral("serif")));
    QVERIFY(!plain.contains(QStringLiteral("--sans")));
    QVERIFY(!plain.contains(QLatin1Char('<')));
    QVERIFY(!plain.contains(QLatin1Char('>')));
}

void TestNotesStorage::plainText_stripsStyleScriptInBody() {
    QCOMPARE(NotesStorage::plainTextForSearch(QStringLiteral(
                 "<style>p{color:red}</style>body<script>x()</script>")),
             QStringLiteral("body"));
}

void TestNotesStorage::plainText_entityOrderAmpLast() {
    // &amp; MUST decode last: "&amp;lt;" → "&lt;" (literal), never a
    // double-decode to "<".
    QCOMPARE(NotesStorage::plainTextForSearch(QStringLiteral(
                 "&amp;lt;b&amp;gt; &nbsp; &#39;x&#39;")),
             QStringLiteral("&lt;b&gt; 'x'"));
}

void TestNotesStorage::plainText_whitespaceCollapsed() {
    QCOMPARE(NotesStorage::plainTextForSearch(
                 QStringLiteral("<p>a</p>\n\n<p>b</p>")),
             QStringLiteral("a b"));
}

void TestNotesStorage::plainText_attributesNeverLeak() {
    const QString plain = NotesStorage::plainTextForSearch(QStringLiteral(
        "<div class=\"b-act\" data-id=\"act-x1\" data-due=\"2026-01-01\">"
        "ship it</div>"));
    QCOMPARE(plain, QStringLiteral("ship it"));
    QVERIFY(!plain.contains(QStringLiteral("act-x1")));
}

void TestNotesStorage::plainText_casePreserved() {
    QCOMPARE(NotesStorage::plainTextForSearch(QStringLiteral("<p>ZeBrA</p>")),
             QStringLiteral("ZeBrA"));
}

void TestNotesStorage::plainText_emptyAndTagOnly() {
    QCOMPARE(NotesStorage::plainTextForSearch(QString()), QString());
    QCOMPARE(NotesStorage::plainTextForSearch(QStringLiteral("<p></p>")),
             QString());
}

// ═══════════════════════════════════════════════════════════════════════
// Durability budget — A7. saveNote now fsyncs the .tmp (and the directory
// entry on POSIX) before the atomic rename, so a power loss mid-rename
// can't surface a zero-byte note. The fsync must not blow the save budget:
// a 1 MB note has to stay well under ~50ms per save on a dev box (the
// assert uses a generous 500ms ceiling so slow shared CI runners don't
// flake); the measured numbers are printed for the release notes.
// ═══════════════════════════════════════════════════════════════════════

void TestNotesStorage::durability_oneMbSave_latencyBudget() {
    QTemporaryDir td;
    QVERIFY(td.isValid());
    NotesStorage s(td.path());
    const QString path = td.path() + "/big.html";

    // ~1 MB body of plain paragraphs (sanitizer + validator see realistic
    // markup, not one giant text node).
    QString body;
    body.reserve(1100 * 1024);
    const QString line =
        QStringLiteral("<p>0123456789 abcdefghijklmnopqrstuvwxyz "
                       "lorem ipsum dolor sit amet consectetur.</p>\n");
    while (body.size() < 1024 * 1024) body += line;
    const QString html =
        QStringLiteral("<html><head><title>big</title></head><body>") +
        body + QStringLiteral("</body></html>");

    // Warm-up save (no backup rotation yet; fills caches).
    QString err;
    QVERIFY2(s.saveNote(path, html, &err), qPrintable(err));

    // Timed saves — these include backup-ring rotation, sanitize,
    // validate, write, fsync, rename, dir-fsync: the full autosave cost.
    qint64 total = 0, worst = 0;
    const int runs = 3;
    for (int i = 0; i < runs; ++i) {
        QElapsedTimer t;
        t.start();
        QVERIFY2(s.saveNote(path, html, &err), qPrintable(err));
        const qint64 ms = t.elapsed();
        total += ms;
        worst = qMax(worst, ms);
        qInfo("1 MB saveNote run %d: %lld ms", i + 1, ms);
    }
    qInfo("1 MB saveNote avg: %lld ms, worst: %lld ms", total / runs, worst);
    QVERIFY2(worst < 500,
             qPrintable(QStringLiteral("1 MB save took %1 ms (budget 500 ms)")
                            .arg(worst)));
}

// ═══════════════════════════════════════════════════════════════════════
// Title identity — notepatra-title meta SSOT (resolver + injector).
// No QtGui in this target: Qt-toHtml-shaped inputs are hardcoded fixture
// strings; CJK/emoji fixtures are built from UTF-8 byte escapes so the
// source file stays pure ASCII.
// ═══════════════════════════════════════════════════════════════════════

// Hand-written legacy-shaped note (no notepatra-title meta unless given).
static QString fixtureNoteHtml(const QString &h1Inner,
                               const QString &escapedMetaTitle = QString()) {
    QString head = QStringLiteral(
        "<!DOCTYPE html>\n<html lang=\"en\">\n<head>\n<meta charset=\"UTF-8\">\n");
    if (!escapedMetaTitle.isEmpty())
        head += QStringLiteral("<meta name=\"notepatra-title\" content=\"")
              + escapedMetaTitle + QStringLiteral("\">\n");
    return head
        + QStringLiteral("<title>t</title>\n</head>\n<body>\n"
                         "<h1 class=\"meet-title\">") + h1Inner
        + QStringLiteral("</h1>\n<p>body text</p>\n</body>\n</html>\n");
}

static bool writeRawFile(const QString &path, const QString &content) {
    QFile f(path);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate)) return false;
    const QByteArray b = content.toUtf8();
    return f.write(b) == b.size();
}

void TestNotesStorage::title_withTitleMeta_insertionShapes() {
    // (a) Template-shaped input — newNoteHtml already carries the meta
    // from birth; withTitleMeta must REPLACE it, leaving exactly one tag.
    QTemporaryDir td;
    QVERIFY(td.isValid());
    NotesStorage s(td.path());
    const QString tpl = s.newNoteHtml(QStringLiteral("Noter 01"),
                                      QDateTime::currentDateTime(), QStringList());
    QVERIFY(tpl.contains(QStringLiteral("notepatra-title")));
    QCOMPARE(NotesStorage::titleMetaIn(tpl), QStringLiteral("Noter 01"));

    const QString replaced = NotesStorage::withTitleMeta(tpl, QStringLiteral("Renamed"));
    QCOMPARE(replaced.count(QStringLiteral("notepatra-title")), 1);
    QCOMPARE(NotesStorage::titleMetaIn(replaced), QStringLiteral("Renamed"));

    // (b) Hardcoded Qt5-toHtml-shaped fixture (no QtGui in this target).
    const QString qtShape = QStringLiteral(
        "<!DOCTYPE HTML PUBLIC \"-//W3C//DTD HTML 4.0//EN\" "
        "\"http://www.w3.org/TR/REC-html40/strict.dtd\">\n"
        "<html><head><meta name=\"qrichtext\" content=\"1\" />"
        "<style type=\"text/css\">\np, li { white-space: pre-wrap; }\n"
        "</style></head><body style=\" font-family:'Sans Serif'; "
        "font-size:10pt; font-weight:400; font-style:normal;\">\n"
        "<p>x</p></body></html>");
    const QString injected = NotesStorage::withTitleMeta(qtShape, QStringLiteral("T1"));
    const int headEnd = injected.indexOf(QStringLiteral("</head>"));
    const int tagAt   = injected.indexOf(QStringLiteral("notepatra-title"));
    QVERIFY(tagAt > 0);
    QVERIFY(tagAt < headEnd);                       // landed inside <head>
    QCOMPARE(NotesStorage::titleMetaIn(injected), QStringLiteral("T1"));

    // Idempotence: double-apply leaves exactly ONE tag, latest title wins.
    const QString twice = NotesStorage::withTitleMeta(injected, QStringLiteral("T2"));
    QCOMPARE(twice.count(QStringLiteral("notepatra-title")), 1);
    QCOMPARE(NotesStorage::titleMetaIn(twice), QStringLiteral("T2"));

    // (c) No <head> → inserted before <body>.
    const QString noHead = NotesStorage::withTitleMeta(
        QStringLiteral("<html><body><p>x</p></body></html>"), QStringLiteral("NH"));
    const int metaIdx = noHead.indexOf(QStringLiteral("notepatra-title"));
    const int bodyIdx = noHead.indexOf(QStringLiteral("<body"));
    QVERIFY(metaIdx >= 0);
    QVERIFY(metaIdx < bodyIdx);
    QCOMPARE(NotesStorage::titleMetaIn(noHead), QStringLiteral("NH"));

    // (d) No head, no body → prepended.
    const QString frag = NotesStorage::withTitleMeta(
        QStringLiteral("<p>frag</p>"), QStringLiteral("FR"));
    QVERIFY(frag.startsWith(QStringLiteral("<meta name=\"notepatra-title\"")));
    QCOMPARE(NotesStorage::titleMetaIn(frag), QStringLiteral("FR"));

    // (e) titleMetaIn tolerates single-quoted content.
    QCOMPARE(NotesStorage::titleMetaIn(
                 QStringLiteral("<head><meta name=\"notepatra-title\" "
                                "content='Solo'></head>")),
             QStringLiteral("Solo"));
}

void TestNotesStorage::title_metaRoundTrip_hostileAndUnicode() {
    const QString base = QStringLiteral(
        "<html><head></head><body><p>x</p></body></html>");

    const QStringList titles = {
        QStringLiteral("Weekly sync"),
        QStringLiteral("R&D \"quarterly\" <review> & 'more'"),
        // CJK + emoji built from UTF-8 byte escapes (no raw glyphs in
        // this source file — non-UTF-8 corruption class).
        QString::fromUtf8("\xE8\xAE\xBE\xE8\xAE\xA1\xE5\x91\xA8\xE4\xBC\x9A "
                          "\xF0\x9F\x9A\x80"),
        // Pre-escaped literal — decode order (&amp; LAST) must hand back
        // the literal "&lt;tag&gt;" the user typed, not "<tag>".
        QStringLiteral("literal &lt;tag&gt; title"),
    };
    for (const QString &t : titles) {
        const QString html = NotesStorage::withTitleMeta(base, t);
        QVERIFY2(NotesStorage::titleMetaIn(html) == t,
                 qPrintable(QStringLiteral("round-trip failed for '%1' → '%2'")
                                .arg(t, NotesStorage::titleMetaIn(html))));
    }

    // Empty title REMOVES the tag (never write an empty meta).
    const QString withT = NotesStorage::withTitleMeta(base, QStringLiteral("X"));
    QVERIFY(withT.contains(QStringLiteral("notepatra-title")));
    const QString cleared = NotesStorage::withTitleMeta(withT, QString());
    QVERIFY(!cleared.contains(QStringLiteral("notepatra-title")));
    QVERIFY(NotesStorage::titleMetaIn(cleared).isEmpty());
    // Whitespace-only counts as empty too.
    QVERIFY(!NotesStorage::withTitleMeta(withT, QStringLiteral("   "))
                 .contains(QStringLiteral("notepatra-title")));
}

void TestNotesStorage::title_prettyTitleFromFilename_parityTable() {
    // Parity vs the legacy noterDisplayTitleForFile outputs — pins zero
    // display regressions for the QRegExp → QRegularExpression port.
    struct Row { const char *path; const char *expected; const char *label; };
    const Row rows[] = {
        { "/in/2026-05-24-140312-noter-06.html",  "Noter 06",   "6-digit time noter" },
        { "/in/2026-05-24-1403-noter-07.html",    "Noter 07",   "4-digit legacy time" },
        { "/in/2026-05-24-140312-untitled-meeting-03.html",
                                                  "Untitled 03", "untitled collapse" },
        { "/t/.trashed-1717-2026-05-24-1403-untitled-meeting-05.html",
                                                  "Untitled 05", "trashed prefix" },
        { "/t/.trashed-99-2026-01-02-090011-noter-08.html",
                                                  "Noter 08",    "trashed 6-digit" },
        { "/in/2026-05-24-140312-weekly-sync (2).html",
                                                  "weekly sync (2)", "dedup suffix" },
        { "/in/2026-05-24-1403-quarterly-plan.html",
                                                  "quarterly plan",  "custom name" },
        { "/in/custom-name.html",                 "custom name", "no date prefix" },
    };
    for (const Row &r : rows) {
        const QString got =
            NotesStorage::prettyTitleFromFilename(QString::fromUtf8(r.path));
        QVERIFY2(got == QString::fromUtf8(r.expected),
                 qPrintable(QStringLiteral("[%1] expected '%2' got '%3'")
                                .arg(QString::fromUtf8(r.label),
                                     QString::fromUtf8(r.expected), got)));
    }
}

void TestNotesStorage::title_titleInfo_precedenceAndHeuristic() {
    QTemporaryDir td;
    QVERIFY(td.isValid());
    NotesStorage s(td.path());

    // (1) Meta wins; legacyH1 still populated (the counter scan needs it).
    const QString pMeta = td.path() + "/2026-01-01-1200-noter-01.html";
    const QString cafe = QString::fromUtf8("Caf\xC3\xA9 Sync");
    QVERIFY(writeRawFile(pMeta, fixtureNoteHtml(QStringLiteral("Noter 01"),
                                                QString::fromUtf8("Caf\xC3\xA9 Sync"))));
    NotesStorage::TitleInfo ti = s.titleInfoForFile(pMeta);
    QCOMPARE(ti.display, cafe);
    QCOMPARE(ti.legacyH1, QStringLiteral("Noter 01"));

    // (2) The audit fix: default filename + customized H1 + no meta → H1.
    const QString pAudit = td.path() + "/2026-01-01-1200-noter-02.html";
    QVERIFY(writeRawFile(pAudit, fixtureNoteHtml(QStringLiteral("Roadmap review"))));
    QCOMPARE(s.displayTitleForFile(pAudit), QStringLiteral("Roadmap review"));

    // (2b) Dedup default shape "noter-03 (2)" still counts as default.
    const QString pDedup = td.path() + "/2026-01-01-1200-noter-03 (2).html";
    QVERIFY(writeRawFile(pDedup, fixtureNoteHtml(QStringLiteral("Custom T"))));
    QCOMPARE(s.displayTitleForFile(pDedup), QStringLiteral("Custom T"));

    // (3) The no-hijack guard: custom filename + stale default H1 → the
    // user's explicit rename wins, NEVER the stale "Noter 01" H1.
    const QString pRenamed = td.path() + "/2026-01-01-1200-weekly-sync.html";
    QVERIFY(writeRawFile(pRenamed, fixtureNoteHtml(QStringLiteral("Noter 01"))));
    QCOMPARE(s.displayTitleForFile(pRenamed), QStringLiteral("weekly sync"));

    // (4) Custom filename + custom H1 (no meta) → filename-pretty
    // (bit-for-bit today's label — zero regression).
    const QString pBoth = td.path() + "/2026-01-01-1200-team-retro.html";
    QVERIFY(writeRawFile(pBoth, fixtureNoteHtml(QStringLiteral("Big Plans"))));
    QCOMPARE(s.displayTitleForFile(pBoth), QStringLiteral("team retro"));

    // (5) Default H1 in "Untitled NN" form does not trigger the audit fix.
    const QString pUnt = td.path() + "/2026-01-01-1200-untitled-meeting-04.html";
    QVERIFY(writeRawFile(pUnt, fixtureNoteHtml(QStringLiteral("Untitled 04"))));
    QCOMPARE(s.displayTitleForFile(pUnt), QStringLiteral("Untitled 04"));

    // (6) Missing file → filename-pretty, and NOT cached: creating the
    // file afterwards must be picked up immediately.
    const QString pGhost = td.path() + "/2026-01-01-1200-ghost-note.html";
    QCOMPARE(s.displayTitleForFile(pGhost), QStringLiteral("ghost note"));
    QVERIFY(writeRawFile(pGhost, fixtureNoteHtml(QStringLiteral("Boo"),
                                                 QStringLiteral("Now Here"))));
    QCOMPARE(s.displayTitleForFile(pGhost), QStringLiteral("Now Here"));
}

void TestNotesStorage::title_cache_invalidationAndSaveNote() {
    QTemporaryDir td;
    QVERIFY(td.isValid());
    NotesStorage s(td.path());
    const QString p = td.path() + "/2026-01-01-1200-noter-01.html";

    // Resolve once (cache fill).
    QVERIFY(writeRawFile(p, fixtureNoteHtml(QStringLiteral("Noter 01"),
                                            QStringLiteral("One"))));
    QCOMPARE(s.displayTitleForFile(p), QStringLiteral("One"));

    // External rewrite with a different size → (mtime,size) key self-heals.
    QVERIFY(writeRawFile(p, fixtureNoteHtml(QStringLiteral("Noter 01"),
                                            QStringLiteral("Two longer title"))));
    QCOMPARE(s.displayTitleForFile(p), QStringLiteral("Two longer title"));

    // Same-size rewrite + explicit invalidation → fresh value guaranteed
    // even when mtime granularity would have served the stale entry.
    QVERIFY(writeRawFile(p, fixtureNoteHtml(QStringLiteral("Noter 01"),
                                            QStringLiteral("AAA"))));
    QCOMPARE(s.displayTitleForFile(p), QStringLiteral("AAA"));
    QVERIFY(writeRawFile(p, fixtureNoteHtml(QStringLiteral("Noter 01"),
                                            QStringLiteral("BBB"))));
    s.invalidateTitleCache(p);
    QCOMPARE(s.displayTitleForFile(p), QStringLiteral("BBB"));

    // saveNote() drops its own cache entry — no panel discipline needed.
    QCOMPARE(s.displayTitleForFile(p), QStringLiteral("BBB"));   // cached
    QString err;
    QVERIFY2(s.saveNote(p, NotesStorage::withTitleMeta(
                              fixtureNoteHtml(QStringLiteral("Noter 01")),
                              QStringLiteral("Three")), &err),
             qPrintable(err));
    QCOMPARE(s.displayTitleForFile(p), QStringLiteral("Three"));
}

void TestNotesStorage::title_saveNote_preservesMetaAndDataAttrs() {
    // The head passes through saveNote VERBATIM (only the <body> region is
    // sanitized) — the injected meta must survive, and b-act data-* attrs
    // must keep working (the todos reindex parses them post-save).
    QTemporaryDir td;
    QVERIFY(td.isValid());
    NotesStorage s(td.path());
    const QString p = td.path() + "/2026-01-01-1200-noter-01.html";

    const QString html = NotesStorage::withTitleMeta(QStringLiteral(
        "<!doctype html><html><head><title>t</title></head><body>"
        "<h1 class=\"meet-title\">Kept</h1>"
        "<div class=\"b b-act\" data-id=\"abc\" data-status=\"open\">do thing</div>"
        "</body></html>"), QStringLiteral("Kept & Title"));

    QString err;
    QVERIFY2(s.saveNote(p, html, &err), qPrintable(err));
    const QString back = s.readNote(p, nullptr);
    QCOMPARE(NotesStorage::titleMetaIn(back), QStringLiteral("Kept & Title"));
    QVERIFY(back.contains(QStringLiteral("data-id=\"abc\"")));
    QVERIFY(back.contains(QStringLiteral("data-status=\"open\"")));
    QVERIFY(back.contains(QStringLiteral("meet-title")));
}

QTEST_MAIN(TestNotesStorage)
#include "test_notes_storage.moc"
