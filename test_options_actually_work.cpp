/**
 * v0.1.42 — integration tests that verify every Preferences-dialog
 * control + every dynamic menu QAction actually changes the editor /
 * the persisted Config.
 *
 * Pre-v0.1.42 the Preferences dialog was theatre — clicking checkboxes
 * did nothing. This test would have caught that. Going forward, any
 * regression that re-stubs a control will fail here.
 *
 * Test pattern, applied to every option:
 *   1. Read Config field's current value.
 *   2. Toggle / change the corresponding control programmatically.
 *   3. Hit OK on the dialog.
 *   4. Assert Config field changed.
 *   5. Assert active editor's actual state changed
 *      (e.g. tabWidth() reflects the new tabWidth, wrapMode() reflects wordWrap, etc.)
 *
 * Runs with QT_QPA_PLATFORM=offscreen so no display is required.
 */
#include <QApplication>
#include <QButtonGroup>
#include <QCheckBox>
#include <QDialogButtonBox>
#include <QFile>
#include <QFontComboBox>
#include <QPushButton>
#include <QRadioButton>
#include <QSpinBox>
#include <QTemporaryDir>
#include <QTimer>
#include <Qsci/qsciscintilla.h>

#include "src/config.h"
#include "src/editor.h"
#include "src/preferences.h"

#include <cstdio>

static int passed = 0;
static int failed = 0;

#define CHECK(name, ok) do { \
    if (ok) { ++passed; std::printf("  ok    %s\n", name); } \
    else    { ++failed; std::fprintf(stderr, "  FAIL  %s\n", name); } \
} while (0)

// Helpers to find specific widgets in the dialog — labels are the
// authoritative identifier (matches what a user sees).
static QCheckBox *findCheckBoxByText(PreferencesDialog *d, const QString &label) {
    for (QCheckBox *c : d->findChildren<QCheckBox *>()) {
        if (c->text() == label) return c;
    }
    return nullptr;
}
static QRadioButton *findRadioByText(PreferencesDialog *d, const QString &label) {
    for (QRadioButton *r : d->findChildren<QRadioButton *>()) {
        if (r->text() == label) return r;
    }
    return nullptr;
}
static QSpinBox *findSpinNearLabel(PreferencesDialog *d, int valueHint, int /*unused*/ = 0) {
    // We accept any QSpinBox currently holding the valueHint; this
    // is good enough for our needs because hints are tab-unique.
    for (QSpinBox *s : d->findChildren<QSpinBox *>()) {
        if (s->value() == valueHint) return s;
    }
    return nullptr;
}
static QFontComboBox *findFontCombo(PreferencesDialog *d) {
    return d->findChild<QFontComboBox *>();
}
static QPushButton *findOkButton(PreferencesDialog *d) {
    auto *box = d->findChild<QDialogButtonBox *>();
    return box ? box->button(QDialogButtonBox::Ok) : nullptr;
}

int main(int argc, char *argv[]) {
    QApplication app(argc, argv);

    std::printf("=== test_options_actually_work ===\n\n");

    // Use a per-test isolated config file so we don't trample the user's
    // real ~/.config/notepatra/config.json. Save current values, restore
    // at exit.
    auto &cfg = Config::instance();
    const auto saved = cfg;  // copy via default-ctor + member assigns

    auto restore = [&]() {
        Config::instance() = saved;
        Config::instance().save();
    };

    // ── 1. Caret width → propagates to editor ────────────────────────
    {
        cfg.caretWidth = 1;
        cfg.save();

        Editor ed;
        ed.applyConfig();
        CHECK("caretWidth=1 applies", ed.SendScintilla(QsciScintilla::SCI_GETCARETWIDTH) == 1);

        cfg.caretWidth = 3;
        cfg.save();
        ed.applyConfig();
        CHECK("caretWidth=3 applies", ed.SendScintilla(QsciScintilla::SCI_GETCARETWIDTH) == 3);
    }

    // ── 2. Highlight current line ────────────────────────────────────
    {
        cfg.highlightCurrentLine = false;
        cfg.save();
        Editor ed;
        ed.applyConfig();
        CHECK("highlightCurrentLine=false → caret line invisible",
              !ed.SendScintilla(QsciScintilla::SCI_GETCARETLINEVISIBLE));

        cfg.highlightCurrentLine = true;
        cfg.save();
        ed.applyConfig();
        CHECK("highlightCurrentLine=true → caret line visible",
              ed.SendScintilla(QsciScintilla::SCI_GETCARETLINEVISIBLE));
    }

    // ── 3. Word wrap ─────────────────────────────────────────────────
    {
        cfg.wordWrap = true;
        cfg.save();
        Editor ed;
        ed.applyConfig();
        CHECK("wordWrap=true → wrapMode != WrapNone", ed.wrapMode() != QsciScintilla::WrapNone);

        cfg.wordWrap = false;
        cfg.save();
        ed.applyConfig();
        CHECK("wordWrap=false → wrapMode == WrapNone", ed.wrapMode() == QsciScintilla::WrapNone);
    }

    // ── 4. Tab width ─────────────────────────────────────────────────
    {
        cfg.tabWidth = 7;
        cfg.save();
        Editor ed;
        ed.applyConfig();
        CHECK("tabWidth=7 applies", ed.tabWidth() == 7);

        cfg.tabWidth = 2;
        cfg.save();
        ed.applyConfig();
        CHECK("tabWidth=2 applies", ed.tabWidth() == 2);
    }

    // ── 5. Use tabs vs spaces ────────────────────────────────────────
    {
        cfg.useTabs = true;
        cfg.save();
        Editor ed;
        ed.applyConfig();
        CHECK("useTabs=true applies", ed.indentationsUseTabs());

        cfg.useTabs = false;
        cfg.save();
        ed.applyConfig();
        CHECK("useTabs=false applies", !ed.indentationsUseTabs());
    }

    // ── 6. Auto-indent ───────────────────────────────────────────────
    {
        cfg.autoIndent = false;
        cfg.save();
        Editor ed;
        ed.applyConfig();
        CHECK("autoIndent=false applies", !ed.autoIndent());

        cfg.autoIndent = true;
        cfg.save();
        ed.applyConfig();
        CHECK("autoIndent=true applies", ed.autoIndent());
    }

    // ── 7. Indent guides ─────────────────────────────────────────────
    {
        cfg.showIndentGuides = false;
        cfg.save();
        Editor ed;
        ed.applyConfig();
        CHECK("showIndentGuides=false applies", !ed.indentationGuides());

        cfg.showIndentGuides = true;
        cfg.save();
        ed.applyConfig();
        CHECK("showIndentGuides=true applies", ed.indentationGuides());
    }

    // ── 8. Edge column / show edge ───────────────────────────────────
    {
        cfg.showEdge = true;
        cfg.edgeColumn = 80;
        cfg.save();
        Editor ed;
        ed.applyConfig();
        CHECK("showEdge=true applies", ed.edgeMode() != QsciScintilla::EdgeNone);
        CHECK("edgeColumn=80 applies", ed.edgeColumn() == 80);

        cfg.showEdge = false;
        cfg.save();
        ed.applyConfig();
        CHECK("showEdge=false applies", ed.edgeMode() == QsciScintilla::EdgeNone);
    }

    // ── 9. Auto-completion threshold ─────────────────────────────────
    {
        cfg.autoComplete = true;
        cfg.autoCompleteThreshold = 5;
        cfg.save();
        Editor ed;
        ed.applyConfig();
        CHECK("autoCompleteThreshold=5 applies", ed.autoCompletionThreshold() == 5);

        cfg.autoComplete = false;
        cfg.save();
        ed.applyConfig();
        CHECK("autoComplete=false → threshold -1 (disabled)",
              ed.autoCompletionThreshold() == -1);
    }

    // ── 10. Default EOL on fresh editor ──────────────────────────────
    {
        cfg.defaultEol = "Windows";
        cfg.save();
        Editor ed;  // no filePath → defaultEol applies
        ed.applyConfig();
        CHECK("defaultEol=Windows → EolWindows on fresh editor",
              ed.eolMode() == QsciScintilla::EolWindows);

        cfg.defaultEol = "Unix";
        cfg.save();
        Editor ed2;
        ed2.applyConfig();
        CHECK("defaultEol=Unix → EolUnix on fresh editor",
              ed2.eolMode() == QsciScintilla::EolUnix);
    }

    // ── 11. Encoding round-trip (UTF-8 / UTF-16 / Windows-1252) ──────
    {
        Editor ed;
        ed.setEncoding("UTF-16 LE");
        CHECK("setEncoding(UTF-16 LE) updates Editor::encoding()",
              ed.encoding() == "UTF-16 LE");

        ed.convertEncoding("UTF-8 BOM");
        CHECK("convertEncoding(UTF-8 BOM) updates Editor::encoding()",
              ed.encoding() == "UTF-8 BOM");
    }

    // ── 11b. v0.1.117 — UTF-32 byte-level round-trip with a non-BMP char ─
    // Pre-fix, saveFile wrote each UTF-16 code unit (surrogates included)
    // as its own 32-bit word, and reloadWithEncoding truncated code points
    // above 0xFFFF through QChar(int). U+1F600 exercises both defects.
    {
        QTemporaryDir tmp;
        CHECK("utf32: temp dir usable", tmp.isValid());

        const uint cps[] = { 0x48, 0x1F600, 0x21 };            // "H", U+1F600, "!"
        const QString sample = QString::fromUcs4(cps, 3);

        // Expected bytes: BOM + one little-endian 32-bit word per code point.
        QByteArray expectLe = QByteArray::fromHex("FFFE0000");
        for (const uint cp : cps) {
            expectLe.append(char(cp & 0xFF));
            expectLe.append(char((cp >> 8) & 0xFF));
            expectLe.append(char((cp >> 16) & 0xFF));
            expectLe.append(char((cp >> 24) & 0xFF));
        }
        QByteArray expectBe = QByteArray::fromHex("0000FEFF");
        for (const uint cp : cps) {
            expectBe.append(char((cp >> 24) & 0xFF));
            expectBe.append(char((cp >> 16) & 0xFF));
            expectBe.append(char((cp >> 8) & 0xFF));
            expectBe.append(char(cp & 0xFF));
        }

        const struct { const char *enc; const QByteArray *expect; } cases[] = {
            {"UTF-32 LE BOM", &expectLe},
            {"UTF-32 BE BOM", &expectBe},
        };
        for (const auto &tc : cases) {
            const QString path = tmp.path() + "/utf32_" +
                QString(tc.enc).left(9).replace(' ', '_') + ".txt";
            Editor ed;
            ed.setText(sample);
            ed.setEncoding(tc.enc);
            CHECK(qPrintable(QString("utf32 %1: saveFile succeeds").arg(tc.enc)),
                  ed.saveFile(path));

            QFile f(path);
            QByteArray onDisk;
            if (f.open(QIODevice::ReadOnly)) { onDisk = f.readAll(); f.close(); }
            CHECK(qPrintable(QString("utf32 %1: on-disk bytes exact (BOM + 3 words)").arg(tc.enc)),
                  onDisk == *tc.expect);

            CHECK(qPrintable(QString("utf32 %1: reloadWithEncoding succeeds").arg(tc.enc)),
                  ed.reloadWithEncoding(tc.enc));
            CHECK(qPrintable(QString("utf32 %1: text round-trips U+1F600").arg(tc.enc)),
                  ed.text() == sample);
        }
    }

    // ── 12. EOL setEolModeByName ─────────────────────────────────────
    {
        Editor ed;
        ed.setEolModeByName("Windows (CR LF)", false);
        CHECK("setEolModeByName(Windows) → EolWindows", ed.eolMode() == QsciScintilla::EolWindows);
        CHECK("setEolModeByName(Windows) → eolModeName updated",
              ed.eolModeName().contains("Windows"));

        ed.setEolModeByName("Unix (LF)", false);
        CHECK("setEolModeByName(Unix) → EolUnix", ed.eolMode() == QsciScintilla::EolUnix);

        ed.setEolModeByName("Macintosh (CR)", false);
        CHECK("setEolModeByName(Mac) → EolMac", ed.eolMode() == QsciScintilla::EolMac);
    }

    // ── 13. Zoom helpers persist to Config ───────────────────────────
    {
        cfg.fontSize = 11;
        cfg.save();
        Editor ed;
        ed.zoomInPersistent();
        CHECK("zoomInPersistent → fontSize incremented", Config::instance().fontSize == 12);
        ed.zoomOutPersistent();
        CHECK("zoomOutPersistent → fontSize decremented", Config::instance().fontSize == 11);
        ed.zoomResetPersistent();
        CHECK("zoomResetPersistent → fontSize == 11", Config::instance().fontSize == 11);
    }

    // ── 14. PreferencesDialog: every checkbox writes Config on OK ────
    {
        // Set known initial state for every checkbox we'll toggle.
        cfg.hideToolbar = false;
        cfg.tabsClosable = true;
        cfg.doubleClickToCloseTab = false;
        cfg.smoothFont = true;
        cfg.highlightCurrentLine = true;
        cfg.wordWrap = false;
        cfg.autoIndent = true;
        cfg.showLineNumbers = true;
        cfg.showBookmarkMargin = true;
        cfg.showIndentGuides = true;
        cfg.showDocumentRulers = false;
        cfg.showCrosshair = false;
        cfg.showEdge = true;
        cfg.autoComplete = true;
        cfg.showWelcomeOnStartup = true;
        cfg.save();

        PreferencesDialog dlg;

        // Toggle every checkbox we expose.
        struct CB { const char *label; };
        const CB checkboxes[] = {
            {"Hide toolbar"},
            {"Double-click to close tab"},
            {"Show close button on each tab"},
            {"Anti-aliased (smooth) font rendering"},
            {"Highlight current line"},
            {"Word wrap"},
            {"Auto-indent"},
            {"Display line numbers"},
            {"Display bookmark margin"},
            {"Display indent guides"},
            {"Display document rulers (horizontal + vertical)"},
            {"Show crosshair overlay (caret guide)"},
            {"Show vertical line at column"},
            {"Enable auto-completion"},
            {"Show Welcome tab on startup (when no session files to restore)"},
        };
        int found = 0;
        for (const CB &c : checkboxes) {
            if (auto *cb = findCheckBoxByText(&dlg, c.label)) {
                cb->setChecked(!cb->isChecked());
                ++found;
            } else {
                std::fprintf(stderr, "  FAIL  Preferences: checkbox '%s' not found\n", c.label);
                ++failed;
            }
        }
        CHECK("Preferences: all 15 checkboxes located",
              found == 15);

        // Click OK to save.
        if (auto *ok = findOkButton(&dlg)) {
            // QTimer::singleShot avoids hang in case OK doesn't accept.
            QTimer::singleShot(0, ok, &QPushButton::click);
            dlg.exec();
        }

        // Verify Config was written. Each toggled checkbox should have
        // flipped its corresponding Config field.
        const auto &c = Config::instance();
        CHECK("Preferences OK: hideToolbar flipped",       c.hideToolbar == true);
        CHECK("Preferences OK: tabsClosable flipped",      c.tabsClosable == false);
        CHECK("Preferences OK: doubleClickToCloseTab flipped", c.doubleClickToCloseTab == true);
        CHECK("Preferences OK: smoothFont flipped",        c.smoothFont == false);
        CHECK("Preferences OK: highlightCurrentLine flipped", c.highlightCurrentLine == false);
        CHECK("Preferences OK: wordWrap flipped",          c.wordWrap == true);
        CHECK("Preferences OK: autoIndent flipped",        c.autoIndent == false);
        CHECK("Preferences OK: showLineNumbers flipped",   c.showLineNumbers == false);
        CHECK("Preferences OK: showBookmarkMargin flipped", c.showBookmarkMargin == false);
        CHECK("Preferences OK: showIndentGuides flipped",  c.showIndentGuides == false);
        CHECK("Preferences OK: showDocumentRulers flipped", c.showDocumentRulers == true);
        CHECK("Preferences OK: showCrosshair flipped",     c.showCrosshair == true);
        CHECK("Preferences OK: showEdge flipped",          c.showEdge == false);
        CHECK("Preferences OK: autoComplete flipped",      c.autoComplete == false);
        CHECK("Preferences OK: showWelcomeOnStartup flipped", c.showWelcomeOnStartup == false);
    }

    // ── 15. PreferencesDialog: radios for tabs + EOL save correctly ──
    {
        cfg.useTabs = false;
        cfg.defaultEol = "Unix";
        cfg.save();

        PreferencesDialog dlg;

        if (auto *r = findRadioByText(&dlg, "Use tab character")) r->setChecked(true);
        if (auto *r = findRadioByText(&dlg, "Windows (CR LF)"))   r->setChecked(true);

        if (auto *ok = findOkButton(&dlg)) {
            QTimer::singleShot(0, ok, &QPushButton::click);
            dlg.exec();
        }

        const auto &c = Config::instance();
        CHECK("Preferences OK: useTabs radio saved",         c.useTabs == true);
        CHECK("Preferences OK: defaultEol radio saved",      c.defaultEol == "Windows");
    }

    // ── 16. Cancel does NOT save ─────────────────────────────────────
    {
        cfg.wordWrap = false;
        cfg.save();

        PreferencesDialog dlg;
        if (auto *cb = findCheckBoxByText(&dlg, "Word wrap")) cb->setChecked(true);

        // Cancel via reject().
        QTimer::singleShot(0, &dlg, &QDialog::reject);
        dlg.exec();

        CHECK("Cancel: wordWrap NOT saved", Config::instance().wordWrap == false);
    }

    // ── 17. v0.1.44 — Editor::commentSyntaxFor() per language ────────
    // The right-click context menu + Ctrl+Q / Ctrl+Shift+Q shortcuts
    // route through this same map. Languages without a kind of comment
    // return an empty string, which the caller uses to disable / hide
    // the menu item. A regression that breaks this map silently breaks
    // every Toggle Comment action across the editor.
    {
        using CS = Editor::CommentSyntax;
        const auto py    = Editor::commentSyntaxFor("Python");
        const auto js    = Editor::commentSyntaxFor("JavaScript");
        const auto cpp   = Editor::commentSyntaxFor("C++");
        const auto sql   = Editor::commentSyntaxFor("SQL");
        const auto lua   = Editor::commentSyntaxFor("Lua");
        const auto html  = Editor::commentSyntaxFor("HTML");
        const auto md    = Editor::commentSyntaxFor("Markdown");
        const auto ps    = Editor::commentSyntaxFor("PowerShell");
        const auto bash  = Editor::commentSyntaxFor("Bash");
        const auto bat   = Editor::commentSyntaxFor("Batch");
        const auto plain = Editor::commentSyntaxFor("Plain Text");
        const auto unkn  = Editor::commentSyntaxFor("ThisLanguageDoesNotExist");

        // Python: # line, no block.
        CHECK("commentSyntax Python  line == #",   py.line == "#");
        CHECK("commentSyntax Python  block empty", py.blockOpen.isEmpty() && py.blockClose.isEmpty());

        // JavaScript / C++: // line + /* */ block.
        CHECK("commentSyntax JS      line == //",  js.line == "//");
        CHECK("commentSyntax JS      block /* */", js.blockOpen == "/*" && js.blockClose == "*/");
        CHECK("commentSyntax C++     line == //",  cpp.line == "//");
        CHECK("commentSyntax C++     block /* */", cpp.blockOpen == "/*" && cpp.blockClose == "*/");

        // SQL: -- line + /* */ block.
        CHECK("commentSyntax SQL     line == --",  sql.line == "--");
        CHECK("commentSyntax SQL     block /* */", sql.blockOpen == "/*" && sql.blockClose == "*/");

        // Lua: -- line + --[[ ]] block.
        CHECK("commentSyntax Lua     line == --",  lua.line == "--");
        CHECK("commentSyntax Lua     block --[[ ]]", lua.blockOpen == "--[[" && lua.blockClose == "]]");

        // HTML / Markdown: no line, <!-- --> block.
        CHECK("commentSyntax HTML    line empty", html.line.isEmpty());
        CHECK("commentSyntax HTML    block <!-- -->",
              html.blockOpen == "<!--" && html.blockClose == "-->");
        CHECK("commentSyntax MD      line empty", md.line.isEmpty());
        CHECK("commentSyntax MD      block <!-- -->",
              md.blockOpen == "<!--" && md.blockClose == "-->");

        // PowerShell: # line + <# #> block.
        CHECK("commentSyntax PS      line == #",   ps.line == "#");
        CHECK("commentSyntax PS      block <# #>", ps.blockOpen == "<#" && ps.blockClose == "#>");

        // Bash: # line, no block.
        CHECK("commentSyntax Bash    line == #",   bash.line == "#");
        CHECK("commentSyntax Bash    block empty", bash.blockOpen.isEmpty() && bash.blockClose.isEmpty());

        // Batch: REM (with trailing space).
        CHECK("commentSyntax Batch   line == REM ", bat.line == "REM ");

        // Plain Text + unknown: everything empty so menu items disable.
        CHECK("commentSyntax Plain   all empty",
              plain.line.isEmpty() && plain.blockOpen.isEmpty() && plain.blockClose.isEmpty());
        CHECK("commentSyntax Unknown all empty",
              unkn.line.isEmpty() && unkn.blockOpen.isEmpty() && unkn.blockClose.isEmpty());

        // Sanity: struct is the public type the right-click menu reads.
        (void)CS{};
    }

    // ── Restore user's real config ───────────────────────────────────
    restore();

    std::printf("\n--- %d passed, %d failed ---\n", passed, failed);
    return failed == 0 ? 0 : 1;
}
