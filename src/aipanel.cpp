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
#include <QFileDialog>
#include <QFileInfo>
#include <QFile>
#include <QDir>
#include <QMouseEvent>
#include <QProcess>
#include <QImage>
#include <QBuffer>
#include <QByteArray>

AIPanel::AIPanel(QWidget *parent) : QWidget(parent) {
    // Make the panel comfortably wide so chat bubbles render properly.
    // Like a real chat app — narrow chat looks cramped.
    setMinimumWidth(420);

    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(2);

    // ─── TOP STRIP: Header + model selector + status ────────────────────
    auto *header = new QLabel("  AI Assistant");
    header->setFixedHeight(24);
    header->setStyleSheet("font-weight: bold; background: #2D2D2D; color: #4EC9B0; padding: 4px 8px;");
    layout->addWidget(header);

    auto *modelRow = new QHBoxLayout;
    modelRow->setContentsMargins(8, 4, 8, 2);
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
    m_thinkingCheck = new QCheckBox("Show thinking");
    m_thinkingCheck->setChecked(false);
    m_thinkingCheck->setStyleSheet("font-size: 11px; color: #888; margin-left: 8px;");
    m_thinkingCheck->setToolTip("Show the model's reasoning blocks (Qwen3, DeepSeek-R1). "
                                "Off = faster, cleaner answers. On = see how the model thinks.");
    modelRow->addWidget(m_thinkingCheck);
    m_clearBtn = new QPushButton("Clear");
    m_clearBtn->setFixedWidth(50);
    m_clearBtn->setStyleSheet("font-size: 11px;");
    m_clearBtn->setToolTip("Clear chat history");
    modelRow->addWidget(m_clearBtn);
    layout->addLayout(modelRow);

    m_statusLabel = new QLabel("");
    m_statusLabel->setStyleSheet("color: #888; padding: 0 8px; font-size: 11px;");
    m_statusLabel->setFixedHeight(14);
    layout->addWidget(m_statusLabel);

    // ─── MIDDLE: chat output (TAKES ALL VERTICAL SPACE) ────────────────
    // This is the conversation area. Bubbles flow from top to bottom.
    // Input is at the bottom of the panel like every real chat app.
    m_output = new QTextEdit;
    m_output->setReadOnly(true);
    m_output->setAcceptRichText(true);
    QFont mono("Consolas", 10);
    mono.setStyleHint(QFont::Monospace);
    m_output->setFont(mono);
    m_output->setStyleSheet(
        "QTextEdit { background: #1E1E1E; color: #D4D4D4; border: none; padding: 12px; }");
    m_output->setPlaceholderText(
        "💬 Conversation will appear here.\n"
        "\n"
        "Quick actions are at the bottom — pick one for selected code,\n"
        "or type a custom prompt and press Enter.\n"
        "\n"
        "Requires Ollama running locally: ollama serve");
    layout->addWidget(m_output, 1);  // stretch=1 → takes all spare space

    // ─── BOTTOM STRIP: quick actions + input + send (like a real chat) ──
    // Quick action buttons row 1 (left = main actions on selected code)
    auto *actionsRow1 = new QHBoxLayout;
    actionsRow1->setContentsMargins(8, 4, 8, 0);
    actionsRow1->setSpacing(4);
    auto *explainBtn = new QPushButton("Explain");
    auto *fixBugsBtn = new QPushButton("Find Bugs");
    auto *refactorBtn = new QPushButton("Refactor");
    auto *testsBtn = new QPushButton("Write Tests");
    for (auto *b : {explainBtn, fixBugsBtn, refactorBtn, testsBtn}) {
        b->setFixedHeight(24);
        b->setStyleSheet("font-size: 11px; padding: 0 8px;");
        actionsRow1->addWidget(b);
    }
    layout->addLayout(actionsRow1);

    auto *actionsRow2 = new QHBoxLayout;
    actionsRow2->setContentsMargins(8, 0, 8, 4);
    actionsRow2->setSpacing(4);
    auto *commentBtn = new QPushButton("Add Comments");
    auto *docBtn = new QPushButton("Generate Docs");
    auto *optimizeBtn = new QPushButton("Optimize");
    auto *translateBtn = new QPushButton("Translate");
    for (auto *b : {commentBtn, docBtn, optimizeBtn, translateBtn}) {
        b->setFixedHeight(24);
        b->setStyleSheet("font-size: 11px; padding: 0 8px;");
        actionsRow2->addWidget(b);
    }
    layout->addLayout(actionsRow2);

    // Insert/Replace/Copy mini row — operates on the LAST assistant response
    auto *resultRow = new QHBoxLayout;
    resultRow->setContentsMargins(8, 0, 8, 2);
    resultRow->setSpacing(4);
    auto *insertBtn = new QPushButton("Insert at Cursor");
    auto *replaceBtn = new QPushButton("Replace Selection");
    auto *copyBtn = new QPushButton("Copy");
    for (auto *b : {insertBtn, replaceBtn, copyBtn}) {
        b->setFixedHeight(22);
        b->setStyleSheet("font-size: 10px; color: #888; padding: 0 8px;");
        resultRow->addWidget(b);
    }
    resultRow->addStretch();
    layout->addLayout(resultRow);

    // ─── ATTACHMENT CHIP (shown above input when a file is attached) ────
    m_attachmentChip = new QLabel("");
    m_attachmentChip->setStyleSheet(
        "background: #1E3A3A; color: #4EC9B0; border-radius: 10px; "
        "padding: 4px 10px; margin: 0 8px; font-size: 11px;");
    m_attachmentChip->setFixedHeight(0);  // hidden until something attached
    m_attachmentChip->setVisible(false);
    layout->addWidget(m_attachmentChip);

    // ─── INPUT BAR AT THE BOTTOM (like every real chat / SMS app) ───────
    auto *customRow = new QHBoxLayout;
    customRow->setContentsMargins(8, 6, 8, 8);
    customRow->setSpacing(6);

    // Attach button — accepts any file (images, PDF, DOCX, PPTX, text, code…)
    m_attachBtn = new QPushButton("📎");
    m_attachBtn->setFixedSize(36, 36);
    m_attachBtn->setStyleSheet(
        "QPushButton { background: #2D2D2D; color: #888; border: 1px solid #444; "
        "border-radius: 18px; font-size: 16px; }"
        "QPushButton:hover { background: #3D3D3D; color: #4EC9B0; border: 1px solid #4EC9B0; }");
    m_attachBtn->setToolTip("Attach image, PDF, DOCX, PPTX, code, or any text file as context");
    customRow->addWidget(m_attachBtn);

    m_customInput = new QLineEdit;
    m_customInput->setPlaceholderText("Type a message and press Enter to send...");
    m_customInput->setStyleSheet(
        "QLineEdit { background: #2D2D2D; color: #E8E8E8; border: 1px solid #444; "
        "border-radius: 18px; padding: 8px 16px; font-size: 13px; }"
        "QLineEdit:focus { border: 1px solid #4EC9B0; background: #1E2D2D; }");
    m_customInput->setFixedHeight(36);
    customRow->addWidget(m_customInput, 1);

    auto *sendBtn = new QPushButton("Send");
    sendBtn->setFixedSize(72, 36);
    sendBtn->setStyleSheet(
        "QPushButton { background: #0E639C; color: white; border: none; "
        "border-radius: 18px; font-weight: bold; font-size: 13px; }"
        "QPushButton:hover { background: #1177BB; }"
        "QPushButton:pressed { background: #0A4F7C; }");
    customRow->addWidget(sendBtn);

    m_stopBtn = new QPushButton("Stop");
    m_stopBtn->setFixedSize(56, 36);
    m_stopBtn->setEnabled(false);
    m_stopBtn->setStyleSheet(
        "QPushButton { background: #2D2D2D; color: #555; border: 1px solid #444; border-radius: 18px; font-size: 12px; }"
        "QPushButton:enabled { background: #8B2C2C; color: white; border: 1px solid #A03333; }"
        "QPushButton:hover:enabled { background: #A03333; }");
    customRow->addWidget(m_stopBtn);
    layout->addLayout(customRow);

    // Wire attach button
    connect(m_attachBtn, &QPushButton::clicked, this, &AIPanel::attachFile);

    // Ollama client
    m_ollama = new OllamaClient(this);

    connect(m_ollama, &OllamaClient::tokenReceived, this, [this](const QString &token) {
        streamIntoAssistantBubble(token);
    });
    connect(m_ollama, &OllamaClient::finished, this, [this](const QString &response) {
        m_lastResponse = response;
        m_stopBtn->setEnabled(false);
        endAssistantBubble();
    });
    connect(m_ollama, &OllamaClient::error, this, [this](const QString &msg) {
        endAssistantBubble();
        m_output->moveCursor(QTextCursor::End);
        m_output->insertHtml("<div style='color:#F48771;padding:8px;'>"
                             "<b>✗ Error:</b> " + msg.toHtmlEscaped() + "</div><br>");
        m_stopBtn->setEnabled(false);
    });
    connect(m_clearBtn, &QPushButton::clicked, this, &AIPanel::clearChat);

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

// Read a file and return its content suitable for embedding in a chat
// prompt. For images, returns the base64 of the image (with imageOut set).
// For text-like files, returns plain text. For PDF/DOCX/PPTX/XLSX, tries
// to extract text using system tools (pdftotext, unzip+grep). Returns
// empty string + sets reasonOut on failure.
static QString extractFileContent(const QString &path, const QString &kind,
                                  QString &imageBase64Out, QString &reasonOut) {
    imageBase64Out.clear();
    reasonOut.clear();
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) {
        reasonOut = "could not open file";
        return QString();
    }

    if (kind == "image") {
        // Load as QImage, re-encode as PNG (predictable format), base64
        QImage img;
        if (!img.loadFromData(f.readAll())) {
            reasonOut = "image format not recognised by Qt";
            return QString();
        }
        // Downscale very large images so we don't blow Ollama's context
        if (img.width() > 1280 || img.height() > 1280) {
            img = img.scaled(1280, 1280, Qt::KeepAspectRatio, Qt::SmoothTransformation);
        }
        QByteArray bytes;
        QBuffer buf(&bytes);
        buf.open(QIODevice::WriteOnly);
        img.save(&buf, "PNG");
        imageBase64Out = QString::fromLatin1(bytes.toBase64());
        return QString();   // image goes via images field, not the prompt
    }

    if (kind == "text") {
        QByteArray data = f.readAll();
        // Cap at 100 KB to avoid blowing the context window
        if (data.size() > 100 * 1024) {
            reasonOut = QString("file is %1 KB — truncated to 100 KB").arg(data.size() / 1024);
            data = data.left(100 * 1024);
        }
        return QString::fromUtf8(data);
    }

    if (kind == "pdf") {
        f.close();
        // pdftotext is part of poppler-utils, usually preinstalled on Linux
        QProcess p;
        p.start("pdftotext", {"-layout", path, "-"});
        if (!p.waitForStarted(1500)) {
            reasonOut = "pdftotext not installed (apt install poppler-utils)";
            return QString();
        }
        p.waitForFinished(15000);
        QByteArray text = p.readAllStandardOutput();
        if (text.isEmpty()) {
            reasonOut = "PDF text extraction returned empty (scanned PDF?)";
            return QString();
        }
        if (text.size() > 100 * 1024) text = text.left(100 * 1024);
        return QString::fromUtf8(text);
    }

    if (kind == "docx" || kind == "pptx" || kind == "xlsx") {
        f.close();
        // Office files are zip archives — extract the main XML and strip tags
        QString innerPath;
        if (kind == "docx") innerPath = "word/document.xml";
        else if (kind == "pptx") innerPath = "ppt/slides/slide1.xml";  // simple: first slide only
        else innerPath = "xl/sharedStrings.xml";
        QProcess p;
        p.start("unzip", {"-p", path, innerPath});
        if (!p.waitForStarted(1500)) {
            reasonOut = "unzip not installed (apt install unzip)";
            return QString();
        }
        p.waitForFinished(15000);
        QByteArray xml = p.readAllStandardOutput();
        if (xml.isEmpty()) {
            reasonOut = QString("could not extract %1 from %2").arg(innerPath, path);
            return QString();
        }
        // Strip XML tags crudely — good enough for prompt context
        QString text = QString::fromUtf8(xml);
        text.replace(QRegularExpression("<[^>]+>"), " ");
        text.replace(QRegularExpression("\\s+"), " ");
        text = text.trimmed();
        if (text.size() > 100 * 1024) text = text.left(100 * 1024);
        return text;
    }

    reasonOut = "unsupported file kind: " + kind;
    return QString();
}

bool AIPanel::eventFilter(QObject *obj, QEvent *evt) {
    if (obj == m_attachmentChip && evt->type() == QEvent::MouseButtonPress) {
        // Click on the chip → clear the attachment
        m_pendingFilePath.clear();
        m_pendingFileKind.clear();
        m_attachmentChip->setText("");
        m_attachmentChip->setVisible(false);
        m_attachmentChip->setFixedHeight(0);
        setStatus("Attachment removed", false);
        return true;
    }
    return QWidget::eventFilter(obj, evt);
}

void AIPanel::sendPrompt(const QString &action) {
    QString model = m_modelCombo->currentText();
    if (model.startsWith("(") || !m_modelCombo->isEnabled()) {
        clearChat();
        m_output->moveCursor(QTextCursor::End);
        m_output->insertHtml("<div style='color:#F48771;padding:8px;'>"
                             "<b>No Ollama model selected.</b><br>"
                             "1. Install Ollama: <a href='https://ollama.com' style='color:#4EC9B0;'>ollama.com</a><br>"
                             "2. Start it: <code>ollama serve</code><br>"
                             "3. Pull a model: <code>ollama pull qwen2.5:7b</code><br>"
                             "4. Click the ↻ refresh button above"
                             "</div><br>");
        return;
    }
    m_ollama->setModel(model);

    QString systemPrompt = "You are a code assistant. Be concise. Output only code when asked to modify code. "
                           "The user is working in " + m_language + ".";

    // Build the prompt + the user-visible prompt label (just the action name
    // + the code snippet for context — no need to dump the verbose template
    // text into the user bubble)
    QString prompt;
    QString userBubbleText;
    if (action == "explain") {
        prompt = "Explain this code clearly and concisely:\n\n```\n" + m_context + "\n```";
        userBubbleText = "Explain this code:\n\n" + m_context;
    } else if (action == "bugs") {
        prompt = "Find bugs and potential issues in this code. List each bug with a fix:\n\n```\n" + m_context + "\n```";
        userBubbleText = "Find bugs in:\n\n" + m_context;
    } else if (action == "refactor") {
        prompt = "Refactor this code to be cleaner and more readable. Output only the refactored code:\n\n```\n" + m_context + "\n```";
        userBubbleText = "Refactor:\n\n" + m_context;
    } else if (action == "tests") {
        prompt = "Write unit tests for this code:\n\n```\n" + m_context + "\n```";
        userBubbleText = "Write unit tests for:\n\n" + m_context;
    } else if (action == "comment") {
        prompt = "Add clear comments to this code. Output the code with comments:\n\n```\n" + m_context + "\n```";
        userBubbleText = "Add comments to:\n\n" + m_context;
    } else if (action == "docs") {
        prompt = "Generate documentation (docstrings/JSDoc/etc) for this code. Output the code with docs:\n\n```\n" + m_context + "\n```";
        userBubbleText = "Generate docs for:\n\n" + m_context;
    } else if (action == "optimize") {
        prompt = "Optimize this code for performance. Explain what you changed and output the optimized code:\n\n```\n" + m_context + "\n```";
        userBubbleText = "Optimize:\n\n" + m_context;
    } else if (action == "translate") {
        prompt = "Translate this code to Python (if not already Python) or to JavaScript (if already Python). Output only the translated code:\n\n```\n" + m_context + "\n```";
        userBubbleText = "Translate Python ↔ JavaScript:\n\n" + m_context;
    } else if (action == "custom") {
        prompt = m_customInput->text() + "\n\n```\n" + m_context + "\n```";
        userBubbleText = m_customInput->text() +
            (m_context.isEmpty() ? "" : "\n\n" + m_context);
        m_customInput->clear();
    }

    // ─── Resolve attached file (if any) → image base64 OR appended text ──
    QStringList imagesBase64;
    if (!m_pendingFilePath.isEmpty()) {
        QString imageB64;
        QString reason;
        QString fileText = extractFileContent(m_pendingFilePath, m_pendingFileKind, imageB64, reason);
        if (!imageB64.isEmpty()) {
            // Vision-model image attachment
            imagesBase64 << imageB64;
            QFileInfo fi(m_pendingFilePath);
            userBubbleText = QString("[🖼 %1]\n%2").arg(fi.fileName()).arg(userBubbleText);
        } else if (!fileText.isEmpty()) {
            // Text-extracted attachment — embed in the prompt as context
            QFileInfo fi(m_pendingFilePath);
            QString header = QString("\n\n--- Attached file: %1 ---\n").arg(fi.fileName());
            prompt = prompt + header + fileText + "\n--- end file ---\n";
            userBubbleText = QString("[📄 %1]\n%2").arg(fi.fileName()).arg(userBubbleText);
        } else if (!reason.isEmpty()) {
            setStatus("✗ attachment error: " + reason, true);
        }

        // Clear the attachment after sending so the next message is fresh
        m_pendingFilePath.clear();
        m_pendingFileKind.clear();
        m_attachmentChip->setVisible(false);
        m_attachmentChip->setFixedHeight(0);
    }

    appendUserBubble(userBubbleText);
    beginAssistantBubble();
    m_stopBtn->setEnabled(true);
    m_ollama->generate(prompt, systemPrompt, m_thinkingCheck->isChecked(), imagesBase64);
}

// ───── Chat-bubble rendering ──────────────────────────────────────────

void AIPanel::clearChat() {
    m_output->clear();
    m_currentAssistantText.clear();
    m_inAssistantBubble = false;
}

void AIPanel::appendUserBubble(const QString &text) {
    // Right-aligned blue bubble for user prompts — like SMS / iMessage / WhatsApp.
    // Bright blue, generous padding, rounded corners, soft text color.
    QString safe = text.toHtmlEscaped().replace("\n", "<br>");
    QString html = QString(
        "<table width='100%%' cellpadding='0' cellspacing='0' style='margin:10px 0;'>"
        "<tr><td width='25%%'></td><td width='75%%' align='right'>"
        "<div style='background:#0E639C;color:#FFFFFF;padding:12px 16px;"
        "border-radius:18px;display:inline-block;text-align:left;"
        "max-width:100%%;font-family:Consolas,Menlo,monospace;font-size:12px;"
        "border:1px solid #1177BB;'>"
        "<div style='font-size:9px;color:#B0D8E8;font-weight:bold;letter-spacing:1px;margin-bottom:6px;'>YOU</div>"
        "%1"
        "</div></td></tr></table>"
    ).arg(safe);
    m_output->moveCursor(QTextCursor::End);
    m_output->insertHtml(html);
    m_output->moveCursor(QTextCursor::End);
    m_output->ensureCursorVisible();
}

void AIPanel::beginAssistantBubble() {
    // Left-aligned gray bubble for assistant responses. Streamed tokens get
    // appended via streamIntoAssistantBubble(). The bubble closes in
    // endAssistantBubble().
    m_currentAssistantText.clear();
    m_inAssistantBubble = true;

    QString header = QString(
        "<table width='100%' cellpadding='0' cellspacing='0' style='margin:8px 0;'>"
        "<tr><td width='65%%' align='left'>"
        "<div style='background:#2D2D2D;color:#D4D4D4;padding:10px 14px;"
        "border-radius:12px;display:inline-block;text-align:left;"
        "max-width:100%%;font-family:Consolas,Menlo,monospace;font-size:12px;"
        "border-left:3px solid #4EC9B0;'>"
        "<div style='font-size:10px;color:#4EC9B0;font-weight:bold;margin-bottom:4px;'>%1</div>"
        "<span style='white-space:pre-wrap;'>"
    ).arg(m_ollama->model().toHtmlEscaped());

    m_output->moveCursor(QTextCursor::End);
    m_output->insertHtml(header);
    m_output->moveCursor(QTextCursor::End);
}

void AIPanel::streamIntoAssistantBubble(const QString &token) {
    if (!m_inAssistantBubble) beginAssistantBubble();
    m_currentAssistantText += token;
    m_output->moveCursor(QTextCursor::End);
    // insertPlainText preserves whitespace and avoids HTML escaping
    // headaches mid-stream. The bubble's parent <span> already has
    // white-space:pre-wrap so newlines render correctly.
    m_output->insertPlainText(token);
    m_output->verticalScrollBar()->setValue(m_output->verticalScrollBar()->maximum());
}

void AIPanel::endAssistantBubble() {
    if (!m_inAssistantBubble) return;
    m_inAssistantBubble = false;
    // Close the <span><div></td></tr></table> wrappers
    m_output->moveCursor(QTextCursor::End);
    m_output->insertHtml("</span></div></td><td></td></tr></table><br>");
    m_output->moveCursor(QTextCursor::End);
    m_output->ensureCursorVisible();
}

// ─── File attachment ──────────────────────────────────────────────────
//
// Open a file picker, accept ANY file (image, PDF, DOCX, PPTX, code, text).
// Stash the path + a kind hint. The actual content extraction happens at
// send time so the user can attach a file, type a message, then hit Send.
void AIPanel::attachFile() {
    QString path = QFileDialog::getOpenFileName(this,
        "Attach file as context",
        QDir::homePath(),
        "All files (*);;"
        "Images (*.png *.jpg *.jpeg *.webp *.gif *.bmp);;"
        "Documents (*.pdf *.docx *.pptx *.xlsx *.odt);;"
        "Text (*.txt *.md *.json *.yaml *.toml *.csv *.xml *.html *.css);;"
        "Code (*.py *.js *.ts *.cpp *.h *.c *.rs *.go *.java *.sql *.sh)");
    if (path.isEmpty()) return;

    QFileInfo fi(path);
    QString ext = fi.suffix().toLower();
    QString kind;
    static const QStringList imageExts = {"png", "jpg", "jpeg", "webp", "gif", "bmp"};
    if (imageExts.contains(ext)) {
        kind = "image";
    } else if (ext == "pdf") {
        kind = "pdf";
    } else if (ext == "docx" || ext == "doc") {
        kind = "docx";
    } else if (ext == "pptx" || ext == "ppt") {
        kind = "pptx";
    } else if (ext == "xlsx" || ext == "xls") {
        kind = "xlsx";
    } else {
        kind = "text";
    }

    m_pendingFilePath = path;
    m_pendingFileKind = kind;

    // Show a chip above the input bar with the file name + kind + a remove [×]
    QString name = fi.fileName();
    qint64 sizeKb = fi.size() / 1024;
    QString icon = "📄";
    if (kind == "image") icon = "🖼";
    else if (kind == "pdf") icon = "📕";
    else if (kind == "docx") icon = "📘";
    else if (kind == "pptx") icon = "📙";
    else if (kind == "xlsx") icon = "📗";

    m_attachmentChip->setText(QString("%1 %2 (%3 KB) — will be included as context. Click chip to remove.")
                              .arg(icon).arg(name).arg(sizeKb));
    m_attachmentChip->setVisible(true);
    m_attachmentChip->setFixedHeight(24);
    m_attachmentChip->installEventFilter(this);  // catch click → clear

    setStatus(QString("✓ Attached: %1 (%2)").arg(name).arg(kind), false);
}
