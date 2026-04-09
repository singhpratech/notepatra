#include "aipanel.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QFont>
#include <QScrollBar>
#include <QMenu>
#include <QApplication>
#include <QClipboard>
#include <QTimer>

AIPanel::AIPanel(QWidget *parent) : QWidget(parent) {
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(2);

    // Header
    auto *header = new QLabel("  AI Assistant (Ollama)");
    header->setFixedHeight(22);
    header->setStyleSheet("font-weight: bold; background: #2D2D2D; color: #4EC9B0; padding: 2px 6px;");
    layout->addWidget(header);

    // Model selector — dynamically populated from ollama /api/tags
    auto *modelRow = new QHBoxLayout;
    modelRow->setContentsMargins(4, 2, 4, 2);
    modelRow->addWidget(new QLabel("Model:"));
    m_modelCombo = new QComboBox;
    m_modelCombo->setEditable(true);
    m_modelCombo->addItem("(detecting Ollama…)");
    m_modelCombo->setEnabled(false);
    modelRow->addWidget(m_modelCombo, 1);
    m_refreshBtn = new QPushButton("↻");
    m_refreshBtn->setFixedWidth(28);
    m_refreshBtn->setToolTip("Refresh model list from Ollama");
    modelRow->addWidget(m_refreshBtn);
    layout->addLayout(modelRow);

    // Status line
    m_statusLabel = new QLabel("");
    m_statusLabel->setStyleSheet("color: #888; padding: 0 6px;");
    m_statusLabel->setFixedHeight(16);
    layout->addWidget(m_statusLabel);

    // Quick action buttons
    auto *actionsRow1 = new QHBoxLayout;
    actionsRow1->setContentsMargins(4, 0, 4, 0);
    auto *explainBtn = new QPushButton("Explain");
    auto *fixBugsBtn = new QPushButton("Find Bugs");
    auto *refactorBtn = new QPushButton("Refactor");
    auto *testsBtn = new QPushButton("Write Tests");
    for (auto *b : {explainBtn, fixBugsBtn, refactorBtn, testsBtn}) {
        b->setFixedHeight(26);
        actionsRow1->addWidget(b);
    }
    layout->addLayout(actionsRow1);

    auto *actionsRow2 = new QHBoxLayout;
    actionsRow2->setContentsMargins(4, 0, 4, 0);
    auto *commentBtn = new QPushButton("Add Comments");
    auto *docBtn = new QPushButton("Generate Docs");
    auto *optimizeBtn = new QPushButton("Optimize");
    auto *translateBtn = new QPushButton("Translate");
    for (auto *b : {commentBtn, docBtn, optimizeBtn, translateBtn}) {
        b->setFixedHeight(26);
        actionsRow2->addWidget(b);
    }
    layout->addLayout(actionsRow2);

    // Custom prompt
    auto *customRow = new QHBoxLayout;
    customRow->setContentsMargins(4, 2, 4, 2);
    m_customInput = new QLineEdit;
    m_customInput->setPlaceholderText("Ask AI anything about the selected code...");
    customRow->addWidget(m_customInput, 1);
    auto *sendBtn = new QPushButton("Send");
    sendBtn->setFixedWidth(60);
    customRow->addWidget(sendBtn);
    m_stopBtn = new QPushButton("Stop");
    m_stopBtn->setFixedWidth(50);
    m_stopBtn->setEnabled(false);
    customRow->addWidget(m_stopBtn);
    layout->addLayout(customRow);

    // Output
    m_output = new QTextEdit;
    m_output->setReadOnly(true);
    QFont mono("Consolas", 10);
    mono.setStyleHint(QFont::Monospace);
    m_output->setFont(mono);
    m_output->setStyleSheet("QTextEdit { background: #1E1E1E; color: #D4D4D4; border: none; padding: 8px; }");
    m_output->setPlaceholderText("Select code and click an action, or type a custom prompt.\n\nRequires Ollama running locally: ollama serve");
    layout->addWidget(m_output, 1);

    // Insert/Replace buttons
    auto *resultRow = new QHBoxLayout;
    resultRow->setContentsMargins(4, 2, 4, 2);
    auto *insertBtn = new QPushButton("Insert at Cursor");
    auto *replaceBtn = new QPushButton("Replace Selection");
    auto *copyBtn = new QPushButton("Copy");
    resultRow->addWidget(insertBtn);
    resultRow->addWidget(replaceBtn);
    resultRow->addWidget(copyBtn);
    resultRow->addStretch();
    layout->addLayout(resultRow);

    // Ollama client
    m_ollama = new OllamaClient(this);

    connect(m_ollama, &OllamaClient::tokenReceived, this, [this](const QString &token) {
        m_output->moveCursor(QTextCursor::End);
        m_output->insertPlainText(token);
        m_output->verticalScrollBar()->setValue(m_output->verticalScrollBar()->maximum());
    });
    connect(m_ollama, &OllamaClient::finished, this, [this](const QString &response) {
        m_lastResponse = response;
        m_stopBtn->setEnabled(false);
        m_output->append("\n\n--- Done ---");
    });
    connect(m_ollama, &OllamaClient::error, this, [this](const QString &msg) {
        m_output->append("\n\nError: " + msg);
        m_stopBtn->setEnabled(false);
    });

    // Dynamic model detection
    connect(m_ollama, &OllamaClient::modelsListed, this, [this](const QStringList &models) {
        QString prev = m_modelCombo->currentText();
        m_modelCombo->clear();
        if (models.isEmpty()) {
            m_modelCombo->addItem("(no models installed)");
            m_modelCombo->setEnabled(false);
            setStatus("Ollama running but no models. Run: ollama pull qwen2.5:7b", true);
        } else {
            m_modelCombo->addItems(models);
            m_modelCombo->setEnabled(true);
            int idx = m_modelCombo->findText(prev);
            if (idx >= 0) m_modelCombo->setCurrentIndex(idx);
            setStatus(QString("Ollama: %1 model%2 detected").arg(models.size())
                      .arg(models.size() == 1 ? "" : "s"), false);
        }
    });
    connect(m_ollama, &OllamaClient::modelsError, this, [this](const QString &reason) {
        m_modelCombo->clear();
        m_modelCombo->addItem("(Ollama offline)");
        m_modelCombo->setEnabled(false);
        setStatus(reason, true);
    });
    connect(m_refreshBtn, &QPushButton::clicked, this, &AIPanel::refreshModels);

    // Kick off initial detection
    QTimer::singleShot(100, this, &AIPanel::refreshModels);

    // Connect buttons
    connect(explainBtn, &QPushButton::clicked, this, [this]() { sendPrompt("explain"); });
    connect(fixBugsBtn, &QPushButton::clicked, this, [this]() { sendPrompt("bugs"); });
    connect(refactorBtn, &QPushButton::clicked, this, [this]() { sendPrompt("refactor"); });
    connect(testsBtn, &QPushButton::clicked, this, [this]() { sendPrompt("tests"); });
    connect(commentBtn, &QPushButton::clicked, this, [this]() { sendPrompt("comment"); });
    connect(docBtn, &QPushButton::clicked, this, [this]() { sendPrompt("docs"); });
    connect(optimizeBtn, &QPushButton::clicked, this, [this]() { sendPrompt("optimize"); });
    connect(translateBtn, &QPushButton::clicked, this, [this]() { sendPrompt("translate"); });
    connect(sendBtn, &QPushButton::clicked, this, [this]() {
        if (!m_customInput->text().isEmpty()) sendPrompt("custom");
    });
    connect(m_customInput, &QLineEdit::returnPressed, sendBtn, &QPushButton::click);
    connect(m_stopBtn, &QPushButton::clicked, m_ollama, &OllamaClient::cancel);
    connect(insertBtn, &QPushButton::clicked, this, [this]() {
        if (!m_lastResponse.isEmpty()) emit insertText(m_lastResponse);
    });
    connect(replaceBtn, &QPushButton::clicked, this, [this]() {
        if (!m_lastResponse.isEmpty()) emit replaceSelection(m_lastResponse);
    });
    connect(copyBtn, &QPushButton::clicked, this, [this]() {
        if (!m_lastResponse.isEmpty()) QApplication::clipboard()->setText(m_lastResponse);
    });
}

void AIPanel::setContext(const QString &selectedText, const QString &filePath, const QString &language) {
    m_context = selectedText;
    m_language = language;
}

void AIPanel::refreshModels() {
    setStatus("Detecting Ollama models...", false);
    m_ollama->listModels();
}

void AIPanel::setStatus(const QString &text, bool isError) {
    m_statusLabel->setText(text);
    m_statusLabel->setStyleSheet(isError
        ? "color: #F48771; padding: 0 6px;"
        : "color: #4EC9B0; padding: 0 6px;");
}

void AIPanel::sendPrompt(const QString &action) {
    QString model = m_modelCombo->currentText();
    if (model.startsWith("(") || !m_modelCombo->isEnabled()) {
        m_output->clear();
        m_output->append("No Ollama model selected.\n\n"
                         "1. Install Ollama: https://ollama.com\n"
                         "2. Start it:      ollama serve\n"
                         "3. Pull a model:  ollama pull qwen2.5:7b\n"
                         "4. Click the ↻ refresh button above.");
        return;
    }
    m_ollama->setModel(model);

    QString systemPrompt = "You are a code assistant. Be concise. Output only code when asked to modify code. "
                           "The user is working in " + m_language + ".";

    QString prompt;
    if (action == "explain") {
        prompt = "Explain this code clearly and concisely:\n\n```\n" + m_context + "\n```";
    } else if (action == "bugs") {
        prompt = "Find bugs and potential issues in this code. List each bug with a fix:\n\n```\n" + m_context + "\n```";
    } else if (action == "refactor") {
        prompt = "Refactor this code to be cleaner and more readable. Output only the refactored code:\n\n```\n" + m_context + "\n```";
    } else if (action == "tests") {
        prompt = "Write unit tests for this code:\n\n```\n" + m_context + "\n```";
    } else if (action == "comment") {
        prompt = "Add clear comments to this code. Output the code with comments:\n\n```\n" + m_context + "\n```";
    } else if (action == "docs") {
        prompt = "Generate documentation (docstrings/JSDoc/etc) for this code. Output the code with docs:\n\n```\n" + m_context + "\n```";
    } else if (action == "optimize") {
        prompt = "Optimize this code for performance. Explain what you changed and output the optimized code:\n\n```\n" + m_context + "\n```";
    } else if (action == "translate") {
        prompt = "Translate this code to Python (if not already Python) or to JavaScript (if already Python). Output only the translated code:\n\n```\n" + m_context + "\n```";
    } else if (action == "custom") {
        prompt = m_customInput->text() + "\n\n```\n" + m_context + "\n```";
    }

    m_output->clear();
    m_output->append("Asking " + m_ollama->model() + "...\n\n");
    m_stopBtn->setEnabled(true);
    m_ollama->generate(prompt, systemPrompt);
}
