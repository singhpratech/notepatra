#include "preferences.h"
#include "config.h"

#include <QTabWidget>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGroupBox>
#include <QCheckBox>
#include <QSpinBox>
#include <QComboBox>
#include <QRadioButton>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>

PreferencesDialog::PreferencesDialog(QWidget *parent) : QDialog(parent) {
    setWindowTitle("Preferences");
    setMinimumSize(600, 500);

    auto *layout = new QVBoxLayout(this);
    auto *tabs = new QTabWidget;
    layout->addWidget(tabs);

    // General tab
    auto *general = new QWidget;
    auto *gLayout = new QVBoxLayout(general);
    auto *tbGroup = new QGroupBox("Toolbar");
    auto *tbLay = new QVBoxLayout(tbGroup);
    tbLay->addWidget(new QCheckBox("Hide toolbar"));
    gLayout->addWidget(tbGroup);
    auto *tabGroup = new QGroupBox("Tab Bar");
    auto *tabLay = new QVBoxLayout(tabGroup);
    tabLay->addWidget(new QCheckBox("Double-click to close"));
    tabLay->addWidget(new QCheckBox("Show close button on each tab"));
    gLayout->addWidget(tabGroup);
    gLayout->addStretch();
    tabs->addTab(general, "General");

    // Editing tab
    auto *editing = new QWidget;
    auto *eLay = new QVBoxLayout(editing);
    auto *caretGroup = new QGroupBox("Caret Settings");
    auto *cLay = new QHBoxLayout(caretGroup);
    cLay->addWidget(new QLabel("Width:"));
    auto *caretW = new QSpinBox; caretW->setRange(1, 3); caretW->setValue(2);
    cLay->addWidget(caretW);
    eLay->addWidget(caretGroup);
    eLay->addWidget(new QCheckBox("Highlight current line"));
    eLay->addWidget(new QCheckBox("Enable smooth font"));
    eLay->addStretch();
    tabs->addTab(editing, "Editing");

    // Margins tab
    auto *margins = new QWidget;
    auto *mLay = new QVBoxLayout(margins);
    auto *foldGroup = new QGroupBox("Fold Margin Style");
    auto *fLay = new QVBoxLayout(foldGroup);
    auto *foldCombo = new QComboBox;
    foldCombo->addItems({"Box tree", "Circle tree", "Arrow", "Simple", "None"});
    fLay->addWidget(foldCombo);
    mLay->addWidget(foldGroup);
    mLay->addWidget(new QCheckBox("Display line numbers"));
    mLay->addWidget(new QCheckBox("Display bookmark margin"));
    mLay->addStretch();
    tabs->addTab(margins, "Margins");

    // Tab Settings tab
    auto *tabSettings = new QWidget;
    auto *tsLay = new QVBoxLayout(tabSettings);
    auto *tsGroup = new QGroupBox("Tab Settings");
    auto *tsgLay = new QVBoxLayout(tsGroup);
    auto *tsRow = new QHBoxLayout;
    tsRow->addWidget(new QLabel("Tab size:"));
    auto *tabSize = new QSpinBox; tabSize->setRange(1, 16); tabSize->setValue(4);
    tsRow->addWidget(tabSize); tsRow->addStretch();
    tsgLay->addLayout(tsRow);
    tsgLay->addWidget(new QRadioButton("Replace tabs with spaces"));
    tsgLay->addWidget(new QRadioButton("Use tab character"));
    tsgLay->addWidget(new QCheckBox("Auto-indent"));
    tsLay->addWidget(tsGroup);
    tsLay->addStretch();
    tabs->addTab(tabSettings, "Tab Settings");

    // Auto-Completion tab
    auto *acTab = new QWidget;
    auto *acLay = new QVBoxLayout(acTab);
    acLay->addWidget(new QCheckBox("Enable auto-completion"));
    auto *acRow = new QHBoxLayout;
    acRow->addWidget(new QLabel("Threshold:"));
    auto *acThresh = new QSpinBox; acThresh->setRange(1, 10); acThresh->setValue(3);
    acRow->addWidget(acThresh); acRow->addStretch();
    acLay->addLayout(acRow);
    acLay->addStretch();
    tabs->addTab(acTab, "Auto-Completion");

    // New Document tab
    auto *newDoc = new QWidget;
    auto *ndLay = new QVBoxLayout(newDoc);
    auto *eolGroup = new QGroupBox("Line ending");
    auto *eolLay = new QVBoxLayout(eolGroup);
    eolLay->addWidget(new QRadioButton("Windows (CR LF)"));
    auto *lf = new QRadioButton("Unix (LF)"); lf->setChecked(true);
    eolLay->addWidget(lf);
    eolLay->addWidget(new QRadioButton("Macintosh (CR)"));
    ndLay->addWidget(eolGroup);
    ndLay->addStretch();
    tabs->addTab(newDoc, "New Document");

    // AI Backend tab — pick where the AI features send requests.
    // Defaults to Ollama so existing installations keep working. Users
    // who prefer llama.cpp's llama-server, or any OpenAI-compatible
    // local backend (LM Studio, Jan, vLLM, text-generation-webui,
    // KoboldCpp, llamafile, OpenRouter, etc.) can switch here.
    auto *aiTab = new QWidget;
    auto *aiLay = new QVBoxLayout(aiTab);

    auto *backendGroup = new QGroupBox("AI Backend");
    auto *backendLay = new QVBoxLayout(backendGroup);

    auto *rbOllama = new QRadioButton("Ollama — ollama serve (default, auto-detect)");
    auto *rbLlama  = new QRadioButton("llama.cpp — llama-server --port 8080 (loads GGUF directly)");
    auto *rbOpen   = new QRadioButton("OpenAI-compat — LM Studio / Jan / vLLM / KoboldCpp / custom");
    const QString be = Config::instance().aiBackend;
    if (be.compare("llama.cpp", Qt::CaseInsensitive) == 0)          rbLlama->setChecked(true);
    else if (be.startsWith("OpenAI", Qt::CaseInsensitive) ||
             be.compare("custom", Qt::CaseInsensitive) == 0)         rbOpen->setChecked(true);
    else                                                              rbOllama->setChecked(true);
    backendLay->addWidget(rbOllama);
    backendLay->addWidget(rbLlama);
    backendLay->addWidget(rbOpen);
    aiLay->addWidget(backendGroup);

    auto *urlRow = new QHBoxLayout;
    urlRow->addWidget(new QLabel("Base URL:"));
    auto *urlEdit = new QLineEdit(Config::instance().aiBaseUrl);
    urlEdit->setPlaceholderText("Leave empty to use the default for the chosen backend");
    urlRow->addWidget(urlEdit, 1);
    aiLay->addLayout(urlRow);

    auto *keyRow = new QHBoxLayout;
    keyRow->addWidget(new QLabel("API Key:"));
    auto *keyEdit = new QLineEdit(Config::instance().aiApiKey);
    keyEdit->setEchoMode(QLineEdit::Password);
    keyEdit->setPlaceholderText("Optional — only needed for OpenAI / OpenRouter / authed endpoints");
    keyRow->addWidget(keyEdit, 1);
    aiLay->addLayout(keyRow);

    // Live quick-help that changes with the chosen backend
    auto *help = new QLabel;
    help->setWordWrap(true);
    help->setStyleSheet("color: #888; font-size: 11px; padding-top: 8px;");
    auto updateHelp = [rbOllama, rbLlama, rbOpen, urlEdit, help]() {
        if (rbOllama->isChecked()) {
            urlEdit->setPlaceholderText("default: http://localhost:11434");
            help->setText(
                "<b>Ollama:</b> install from <a href='https://ollama.com'>ollama.com</a>, "
                "run <code>ollama serve</code>, pull a small model: "
                "<code>ollama pull qwen2.5-coder:3b</code>. No auth needed.");
        } else if (rbLlama->isChecked()) {
            urlEdit->setPlaceholderText("default: http://localhost:8080");
            help->setText(
                "<b>llama.cpp:</b> grab any <code>.gguf</code> from "
                "<a href='https://huggingface.co'>huggingface.co</a> and run "
                "<code>llama-server -m model.gguf --port 8080</code>. "
                "No daemon, no config format — pure GGUF.");
        } else {
            urlEdit->setPlaceholderText("e.g. http://localhost:1234 (LM Studio), https://openrouter.ai/api/v1 (OpenRouter)");
            help->setText(
                "<b>OpenAI-compat:</b> works with any server that speaks the "
                "OpenAI <code>/v1/chat/completions</code> API.<br><br>"
                "<b>Local (no account, no key):</b><br>"
                "• LM Studio — <code>http://localhost:1234</code><br>"
                "• Jan — <code>http://localhost:1337</code><br>"
                "• vLLM — <code>http://localhost:8000</code><br>"
                "• KoboldCpp — <code>http://localhost:5001</code><br>"
                "• llamafile — <code>http://localhost:8080</code><br>"
                "• text-generation-webui — <code>http://localhost:5000</code><br><br>"
                "<b>Cloud (API key required — paste in the field below):</b><br>"
                "• <a href='https://openrouter.ai'>OpenRouter</a> — "
                "<code>https://openrouter.ai/api/v1</code>, access to 100+ models "
                "(Claude · GPT · Gemini · Llama · Mistral · DeepSeek · Qwen).<br>"
                "• <a href='https://openai.com'>OpenAI</a> — "
                "<code>https://api.openai.com/v1</code>");
        }
    };
    updateHelp();
    QObject::connect(rbOllama, &QRadioButton::toggled, this, updateHelp);
    QObject::connect(rbLlama,  &QRadioButton::toggled, this, updateHelp);
    QObject::connect(rbOpen,   &QRadioButton::toggled, this, updateHelp);
    help->setOpenExternalLinks(true);
    aiLay->addWidget(help);
    aiLay->addStretch();

    // Persist on save. Fires when Close is clicked (see bottom of ctor).
    auto saveAiSettings = [rbOllama, rbLlama, urlEdit, keyEdit]() {
        auto &cfg = Config::instance();
        cfg.aiBackend = rbOllama->isChecked() ? "Ollama"
                      : rbLlama->isChecked()  ? "llama.cpp"
                      :                          "OpenAI-compat";
        cfg.aiBaseUrl = urlEdit->text().trimmed();
        cfg.aiApiKey  = keyEdit->text();
        cfg.save();
    };
    // Store on the dialog so the Close button's lambda can call it
    setProperty("notepatra.saveAiSettings", QVariant::fromValue<void*>(nullptr));
    m_saveAiSettings = saveAiSettings;

    tabs->addTab(aiTab, "AI");

    auto *btnRow = new QHBoxLayout;
    btnRow->addStretch();
    auto *closeBtn = new QPushButton("Close");
    connect(closeBtn, &QPushButton::clicked, this, [this]() {
        if (m_saveAiSettings) m_saveAiSettings();
        close();
    });
    btnRow->addWidget(closeBtn);
    layout->addLayout(btnRow);
}
