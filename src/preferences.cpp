// SPDX-License-Identifier: GPL-3.0-or-later

#include "preferences.h"
#include "config.h"
#include "network_policy.h"

#include <QButtonGroup>
#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QFontComboBox>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QRadioButton>
#include <QSpinBox>
#include <QTabWidget>
#include <QUrl>
#include <QVBoxLayout>

// v0.1.42 rewrite — every control on every tab is wired to Config.
// Init reads from Config, OK/Apply writes back. Settings-applied signal
// lets MainWindow propagate the change to every open Editor.

PreferencesDialog::PreferencesDialog(QWidget *parent) : QDialog(parent) {
    setWindowTitle("Preferences");
    setMinimumSize(640, 540);

    auto &cfg = Config::instance();

    auto *layout = new QVBoxLayout(this);
    auto *tabs = new QTabWidget;
    layout->addWidget(tabs);

    // ─── General tab ─────────────────────────────────────────────────
    auto *general = new QWidget;
    auto *gLayout = new QVBoxLayout(general);

    auto *tbGroup = new QGroupBox("Toolbar");
    auto *tbLay = new QVBoxLayout(tbGroup);
    auto *cbHideToolbar = new QCheckBox("Hide toolbar");
    cbHideToolbar->setChecked(cfg.hideToolbar);
    tbLay->addWidget(cbHideToolbar);
    gLayout->addWidget(tbGroup);

    auto *tabGroup = new QGroupBox("Tab Bar");
    auto *tabLay = new QVBoxLayout(tabGroup);
    auto *cbDoubleClickClose = new QCheckBox("Double-click to close tab");
    cbDoubleClickClose->setChecked(cfg.doubleClickToCloseTab);
    auto *cbTabsClosable = new QCheckBox("Show close button on each tab");
    cbTabsClosable->setChecked(cfg.tabsClosable);
    tabLay->addWidget(cbDoubleClickClose);
    tabLay->addWidget(cbTabsClosable);
    gLayout->addWidget(tabGroup);

    auto *welcomeGroup = new QGroupBox("Startup");
    auto *welcomeLay = new QVBoxLayout(welcomeGroup);
    auto *cbWelcomeOnStart = new QCheckBox("Show Welcome tab on startup (when no session files to restore)");
    cbWelcomeOnStart->setChecked(cfg.showWelcomeOnStartup);
    welcomeLay->addWidget(cbWelcomeOnStart);
    gLayout->addWidget(welcomeGroup);

    gLayout->addStretch();
    tabs->addTab(general, "General");

    // ─── Editing tab ─────────────────────────────────────────────────
    auto *editing = new QWidget;
    auto *eLay = new QVBoxLayout(editing);

    auto *fontGroup = new QGroupBox("Font");
    auto *fontLay = new QVBoxLayout(fontGroup);
    auto *fontFamilyRow = new QHBoxLayout;
    fontFamilyRow->addWidget(new QLabel("Family:"));
    auto *fontFamilyCombo = new QFontComboBox;
    fontFamilyCombo->setFontFilters(QFontComboBox::MonospacedFonts);
    if (!cfg.fontFamily.isEmpty()) fontFamilyCombo->setCurrentFont(QFont(cfg.fontFamily));
    fontFamilyRow->addWidget(fontFamilyCombo, 1);
    auto *cbAllFonts = new QCheckBox("Show all fonts");
    fontFamilyRow->addWidget(cbAllFonts);
    QObject::connect(cbAllFonts, &QCheckBox::toggled, fontFamilyCombo, [fontFamilyCombo](bool all) {
        fontFamilyCombo->setFontFilters(all ? QFontComboBox::AllFonts : QFontComboBox::MonospacedFonts);
    });
    fontLay->addLayout(fontFamilyRow);

    auto *fontSizeRow = new QHBoxLayout;
    fontSizeRow->addWidget(new QLabel("Size:"));
    auto *fontSizeSpin = new QSpinBox;
    fontSizeSpin->setRange(6, 48);
    fontSizeSpin->setValue(cfg.fontSize > 0 ? cfg.fontSize : 11);
    fontSizeRow->addWidget(fontSizeSpin);
    fontSizeRow->addStretch();
    fontLay->addLayout(fontSizeRow);

    auto *cbSmoothFont = new QCheckBox("Anti-aliased (smooth) font rendering");
    cbSmoothFont->setChecked(cfg.smoothFont);
    fontLay->addWidget(cbSmoothFont);

    eLay->addWidget(fontGroup);

    auto *caretGroup = new QGroupBox("Caret");
    auto *cLay = new QHBoxLayout(caretGroup);
    cLay->addWidget(new QLabel("Width:"));
    auto *caretW = new QSpinBox;
    caretW->setRange(1, 3);
    caretW->setValue(qBound(1, cfg.caretWidth, 3));
    cLay->addWidget(caretW);
    cLay->addStretch();
    eLay->addWidget(caretGroup);

    auto *cbHighlightLine = new QCheckBox("Highlight current line");
    cbHighlightLine->setChecked(cfg.highlightCurrentLine);
    eLay->addWidget(cbHighlightLine);

    auto *cbWordWrap = new QCheckBox("Word wrap");
    cbWordWrap->setChecked(cfg.wordWrap);
    eLay->addWidget(cbWordWrap);

    auto *cbAutoIndent = new QCheckBox("Auto-indent");
    cbAutoIndent->setChecked(cfg.autoIndent);
    eLay->addWidget(cbAutoIndent);

    auto *edgeGroup = new QGroupBox("Right-margin guide");
    auto *edgeLay = new QHBoxLayout(edgeGroup);
    auto *cbShowEdge = new QCheckBox("Show vertical line at column");
    cbShowEdge->setChecked(cfg.showEdge);
    auto *edgeColumnSpin = new QSpinBox;
    edgeColumnSpin->setRange(20, 500);
    edgeColumnSpin->setValue(cfg.edgeColumn > 0 ? cfg.edgeColumn : 120);
    edgeLay->addWidget(cbShowEdge);
    edgeLay->addWidget(edgeColumnSpin);
    edgeLay->addStretch();
    eLay->addWidget(edgeGroup);

    // ─── Large files (memory budget) ─────────────────────────────────
    auto *memGroup = new QGroupBox("Large files");
    auto *memLay = new QVBoxLayout(memGroup);
    auto *memRow = new QHBoxLayout;
    memRow->addWidget(new QLabel("Memory limit for opening a file:"));
    auto *memSpin = new QSpinBox;
    memSpin->setRange(1, 64);
    memSpin->setSuffix(" GB");
    // Config stores MB; round-trip to whole GB for the control.
    memSpin->setValue(qBound(1, (cfg.fileMemoryLimitMb + 512) / 1024, 64));
    memRow->addWidget(memSpin);
    memRow->addStretch();
    memLay->addLayout(memRow);
    auto *memNote = new QLabel(
        "Default is 2 GB. Notepatra can still open files larger than this — it "
        "just asks you to confirm first, because the load needs that much free "
        "RAM. Raise the limit if your machine has the memory to spare.");
    memNote->setWordWrap(true);
    {
        QFont nf = memNote->font();
        nf.setItalic(true);
        memNote->setFont(nf);
        QPalette pal = memNote->palette();           // theme-safe muted text
        QColor c = pal.color(QPalette::WindowText);
        c.setAlpha(160);
        pal.setColor(QPalette::WindowText, c);
        memNote->setPalette(pal);
    }
    memLay->addWidget(memNote);
    eLay->addWidget(memGroup);

    eLay->addStretch();
    tabs->addTab(editing, "Editing");

    // ─── Margins tab ─────────────────────────────────────────────────
    auto *margins = new QWidget;
    auto *mLay = new QVBoxLayout(margins);

    auto *foldGroup = new QGroupBox("Fold margin style");
    auto *fLay = new QVBoxLayout(foldGroup);
    auto *foldCombo = new QComboBox;
    foldCombo->addItem("Boxed tree (default)", "BoxedTree");
    foldCombo->addItem("Circle tree",          "CircleTree");
    foldCombo->addItem("Plain",                "Plain");
    foldCombo->addItem("Boxed",                "Boxed");
    foldCombo->addItem("Circle",               "Circle");
    foldCombo->addItem("None (no folding)",    "None");
    {
        int idx = foldCombo->findData(cfg.foldStyle);
        if (idx < 0) idx = 0;
        foldCombo->setCurrentIndex(idx);
    }
    fLay->addWidget(foldCombo);
    mLay->addWidget(foldGroup);

    auto *cbShowLineNumbers = new QCheckBox("Display line numbers");
    cbShowLineNumbers->setChecked(cfg.showLineNumbers);
    mLay->addWidget(cbShowLineNumbers);

    auto *cbShowBookmarks = new QCheckBox("Display bookmark margin");
    cbShowBookmarks->setChecked(cfg.showBookmarkMargin);
    mLay->addWidget(cbShowBookmarks);

    auto *cbShowIndentGuides = new QCheckBox("Display indent guides");
    cbShowIndentGuides->setChecked(cfg.showIndentGuides);
    mLay->addWidget(cbShowIndentGuides);

    auto *cbShowDocRulers = new QCheckBox("Display document rulers (horizontal + vertical)");
    cbShowDocRulers->setChecked(cfg.showDocumentRulers);
    mLay->addWidget(cbShowDocRulers);

    auto *cbShowCrosshair = new QCheckBox("Show crosshair overlay (caret guide)");
    cbShowCrosshair->setChecked(cfg.showCrosshair);
    mLay->addWidget(cbShowCrosshair);

    mLay->addStretch();
    tabs->addTab(margins, "Margins");

    // ─── Tab Settings tab ────────────────────────────────────────────
    auto *tabSettings = new QWidget;
    auto *tsLay = new QVBoxLayout(tabSettings);

    auto *tsGroup = new QGroupBox("Tab settings");
    auto *tsgLay = new QVBoxLayout(tsGroup);

    auto *tsRow = new QHBoxLayout;
    tsRow->addWidget(new QLabel("Tab size (columns):"));
    auto *tabSize = new QSpinBox;
    tabSize->setRange(1, 16);
    tabSize->setValue(qMax(1, cfg.tabWidth));
    tsRow->addWidget(tabSize);
    tsRow->addStretch();
    tsgLay->addLayout(tsRow);

    auto *rbReplaceTabs = new QRadioButton("Replace tabs with spaces");
    auto *rbUseTabs     = new QRadioButton("Use tab character");
    auto *tabsBg = new QButtonGroup(this);
    tabsBg->addButton(rbReplaceTabs);
    tabsBg->addButton(rbUseTabs);
    if (cfg.useTabs) rbUseTabs->setChecked(true); else rbReplaceTabs->setChecked(true);
    tsgLay->addWidget(rbReplaceTabs);
    tsgLay->addWidget(rbUseTabs);

    tsLay->addWidget(tsGroup);
    tsLay->addStretch();
    tabs->addTab(tabSettings, "Tab Settings");

    // ─── Auto-Completion tab ─────────────────────────────────────────
    auto *acTab = new QWidget;
    auto *acLay = new QVBoxLayout(acTab);

    auto *cbAutoComplete = new QCheckBox("Enable auto-completion");
    cbAutoComplete->setChecked(cfg.autoComplete);
    acLay->addWidget(cbAutoComplete);

    auto *acRow = new QHBoxLayout;
    acRow->addWidget(new QLabel("Trigger after N characters:"));
    auto *acThresh = new QSpinBox;
    acThresh->setRange(1, 10);
    acThresh->setValue(qBound(1, cfg.autoCompleteThreshold, 10));
    acRow->addWidget(acThresh);
    acRow->addStretch();
    acLay->addLayout(acRow);
    acLay->addStretch();
    tabs->addTab(acTab, "Auto-Completion");

    // ─── New Document tab ────────────────────────────────────────────
    auto *newDoc = new QWidget;
    auto *ndLay = new QVBoxLayout(newDoc);

    auto *eolGroup = new QGroupBox("Default line ending for new documents");
    auto *eolLay = new QVBoxLayout(eolGroup);
    auto *rbCrlf = new QRadioButton("Windows (CR LF)");
    auto *rbLf   = new QRadioButton("Unix (LF)");
    auto *rbCr   = new QRadioButton("Macintosh (CR — classic Mac OS)");
    auto *eolBg = new QButtonGroup(this);
    eolBg->addButton(rbCrlf);
    eolBg->addButton(rbLf);
    eolBg->addButton(rbCr);
    if      (cfg.defaultEol == "Windows") rbCrlf->setChecked(true);
    else if (cfg.defaultEol == "Mac")     rbCr->setChecked(true);
    else                                  rbLf->setChecked(true);
    eolLay->addWidget(rbCrlf);
    eolLay->addWidget(rbLf);
    eolLay->addWidget(rbCr);
    ndLay->addWidget(eolGroup);
    ndLay->addStretch();
    tabs->addTab(newDoc, "New Document");

    // ─── AI Backend tab ──────────────────────────────────────────────
    auto *aiTab = new QWidget;
    auto *aiLay = new QVBoxLayout(aiTab);

    auto *backendGroup = new QGroupBox("AI Backend");
    auto *backendLay = new QVBoxLayout(backendGroup);

    auto *rbOllama = new QRadioButton("Ollama — ollama serve (default, auto-detect)");
    auto *rbLlama  = new QRadioButton("llama.cpp — llama-server --port 8080 (loads GGUF directly)");
    auto *rbOpen   = new QRadioButton("Custom OpenAI-compatible endpoint — set Base URL + key below");
    auto *aiBg = new QButtonGroup(this);
    aiBg->addButton(rbOllama);
    aiBg->addButton(rbLlama);
    aiBg->addButton(rbOpen);
    const QString be = cfg.aiBackend;
    if (be.compare("llama.cpp", Qt::CaseInsensitive) == 0)         rbLlama->setChecked(true);
    else if (be.startsWith("OpenAI", Qt::CaseInsensitive) ||
             be.compare("custom", Qt::CaseInsensitive) == 0)        rbOpen->setChecked(true);
    else                                                            rbOllama->setChecked(true);
    backendLay->addWidget(rbOllama);
    backendLay->addWidget(rbLlama);
    backendLay->addWidget(rbOpen);
    aiLay->addWidget(backendGroup);

    auto *urlRow = new QHBoxLayout;
    urlRow->addWidget(new QLabel("Base URL:"));
    auto *urlEdit = new QLineEdit(cfg.aiBaseUrl);
    urlEdit->setPlaceholderText("Leave empty to use the default for the chosen backend");
    urlRow->addWidget(urlEdit, 1);
    aiLay->addLayout(urlRow);

    auto *keyRow = new QHBoxLayout;
    keyRow->addWidget(new QLabel("API Key:"));
    auto *keyEdit = new QLineEdit(cfg.aiApiKey);
    keyEdit->setEchoMode(QLineEdit::Password);
    keyEdit->setPlaceholderText("Optional — only needed for OpenAI / OpenRouter / authed endpoints");
    keyRow->addWidget(keyEdit, 1);
    aiLay->addLayout(keyRow);

    // v0.1.98 — signal that cloud providers exist but are configured in the
    // AI panel (one place for keys), rather than duplicating key fields here.
    // (User: "for OpenRouter, say set it in the AI panel — keep it integrated.")
    auto *cloudNote = new QLabel(
        "Looking for OpenRouter, OpenAI, Azure, or Ollama Cloud? Add those in "
        "the AI panel — open it and use its Settings (gear) button to enter the "
        "API key. Cloud backends are configured there so your keys stay in one place.");
    cloudNote->setWordWrap(true);
    {
        QFont nf = cloudNote->font();
        nf.setItalic(true);
        cloudNote->setFont(nf);
        QPalette pal = cloudNote->palette();           // theme-safe muted text
        QColor c = pal.color(QPalette::WindowText);
        c.setAlpha(160);
        pal.setColor(QPalette::WindowText, c);
        cloudNote->setPalette(pal);
    }
    aiLay->addWidget(cloudNote);

    aiLay->addStretch();
    tabs->addTab(aiTab, "AI");

    // ─── OK / Apply / Cancel ─────────────────────────────────────────
    auto *btnRow = new QDialogButtonBox(
        QDialogButtonBox::Ok | QDialogButtonBox::Apply | QDialogButtonBox::Cancel);
    layout->addWidget(btnRow);

    // Single save lambda — used by both OK and Apply. Returns true on success,
    // false if validation rejected the input (dialog stays open so the user can fix it).
    // (Capture by value implicitly includes `this` in C++17; MSVC rejects
    // the explicit `[=, this]` form when -std=c++17 — keep just `[=]`.)
    auto save = [=]() -> bool {
        // ── AI Base URL validation ──
        // Free-form URL that the AI client sends the API key + prompts to.
        // Hard-reject malformed input and plain-http public hosts; warn-and-confirm
        // for any host outside the vendor allowlist (an attacker-controlled "faster mirror"
        // would otherwise harvest API keys silently).
        const QString proposedBaseUrl = urlEdit->text().trimmed();
        if (!proposedBaseUrl.isEmpty()) {
            const QUrl probe = QUrl::fromUserInput(proposedBaseUrl);
            if (!probe.isValid() || probe.host().isEmpty() ||
                (probe.scheme() != "http" && probe.scheme() != "https")) {
                QMessageBox::warning(this, tr("Invalid Base URL"),
                    tr("The Base URL does not look like a valid http(s) URL.\n\n"
                       "Example: https://api.openai.com/v1\n"
                       "Or leave empty to use the default for the chosen backend."));
                return false;
            }
            if (probe.scheme().compare("http", Qt::CaseInsensitive) == 0 &&
                !NotepatraNetworkPolicy::isPrivateNetworkHost(probe)) {
                QMessageBox::warning(this, tr("Insecure Base URL"),
                    tr("The Base URL uses http:// to a public host. Your API key would "
                       "be sent in plaintext and visible to anyone on the network path.\n\n"
                       "Use https:// or leave the Base URL empty."));
                return false;
            }
            static const QStringList kVendorSuffixes = {
                QStringLiteral("openai.com"),
                QStringLiteral("openai.azure.com"),
                QStringLiteral("openrouter.ai"),
                QStringLiteral("anthropic.com"),
                QStringLiteral("googleapis.com"),
                QStringLiteral("ollama.com"),
                QStringLiteral("mistral.ai"),
                QStringLiteral("groq.com"),
                QStringLiteral("cohere.ai"),
            };
            const QString host = probe.host().toLower();
            bool isVendor = false;
            for (const QString &suffix : kVendorSuffixes) {
                if (host == suffix || host.endsWith(QStringLiteral(".") + suffix)) {
                    isVendor = true;
                    break;
                }
            }
            if (!isVendor && !NotepatraNetworkPolicy::isPrivateNetworkHost(probe)) {
                const auto reply = QMessageBox::question(
                    this, tr("Non-vendor Base URL"),
                    tr("You are about to send your API key and chat prompts to a "
                       "non-vendor host:\n\n    %1\n\n"
                       "Only continue if you explicitly trust this host. A malicious "
                       "\"faster mirror\" URL is a known way to harvest API keys.\n\n"
                       "Continue?").arg(host),
                    QMessageBox::Yes | QMessageBox::No,
                    QMessageBox::No);
                if (reply != QMessageBox::Yes) return false;
            }
        }

        auto &cfg2 = Config::instance();

        // General
        cfg2.hideToolbar           = cbHideToolbar->isChecked();
        cfg2.doubleClickToCloseTab = cbDoubleClickClose->isChecked();
        cfg2.tabsClosable          = cbTabsClosable->isChecked();
        cfg2.showWelcomeOnStartup  = cbWelcomeOnStart->isChecked();

        // Editing
        cfg2.fontFamily            = fontFamilyCombo->currentFont().family();
        cfg2.fontSize              = fontSizeSpin->value();
        cfg2.smoothFont            = cbSmoothFont->isChecked();
        cfg2.caretWidth            = caretW->value();
        cfg2.highlightCurrentLine  = cbHighlightLine->isChecked();
        cfg2.wordWrap              = cbWordWrap->isChecked();
        cfg2.autoIndent            = cbAutoIndent->isChecked();
        cfg2.showEdge              = cbShowEdge->isChecked();
        cfg2.edgeColumn            = edgeColumnSpin->value();
        cfg2.fileMemoryLimitMb     = memSpin->value() * 1024;   // GB → MB

        // Margins
        cfg2.foldStyle             = foldCombo->currentData().toString();
        cfg2.showLineNumbers       = cbShowLineNumbers->isChecked();
        cfg2.showBookmarkMargin    = cbShowBookmarks->isChecked();
        cfg2.showIndentGuides      = cbShowIndentGuides->isChecked();
        cfg2.showDocumentRulers    = cbShowDocRulers->isChecked();
        cfg2.showCrosshair         = cbShowCrosshair->isChecked();

        // Tab Settings
        cfg2.tabWidth              = tabSize->value();
        cfg2.useTabs               = rbUseTabs->isChecked();

        // Auto-Completion
        cfg2.autoComplete          = cbAutoComplete->isChecked();
        cfg2.autoCompleteThreshold = acThresh->value();

        // New Document
        if      (rbCrlf->isChecked()) cfg2.defaultEol = "Windows";
        else if (rbCr->isChecked())   cfg2.defaultEol = "Mac";
        else                          cfg2.defaultEol = "Unix";

        // AI Backend
        if      (rbLlama->isChecked()) cfg2.aiBackend = "llama.cpp";
        else if (rbOpen->isChecked())  cfg2.aiBackend = "OpenAI-compat";
        else                           cfg2.aiBackend = "Ollama";
        cfg2.aiBaseUrl = urlEdit->text().trimmed();
        cfg2.aiApiKey  = keyEdit->text();

        cfg2.save();
        emit settingsApplied();
        return true;
    };

    QObject::connect(btnRow, &QDialogButtonBox::accepted, this, [this, save]() {
        if (save()) accept();
    });
    QObject::connect(btnRow, &QDialogButtonBox::rejected, this, &PreferencesDialog::reject);
    QObject::connect(btnRow->button(QDialogButtonBox::Apply), &QPushButton::clicked, this, [save]() { save(); });
}
