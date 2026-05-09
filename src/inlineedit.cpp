// ─────────────────────────────────────────────────────────────────────
// inlineedit.cpp — implementation of the Ctrl+I rewrite dialog.
//
// See inlineedit.h for the workflow + design notes.
//
// Diffing strategy: for v1 we do a naive line-by-line equality check
// (good enough for short selections, which is the common case for an
// inline edit). The Rust Myers diff in rust-core/ is overkill here and
// would require pulling in compare_widget.cpp. If we ever want true
// LCS-aware highlighting we can swap renderDiff() to call into it.
// ─────────────────────────────────────────────────────────────────────

#include "inlineedit.h"

#include "ollama.h"

#include <QDialogButtonBox>
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QRegularExpression>
#include <QScreen>
#include <QScrollBar>
#include <QShortcut>
#include <QSplitter>
#include <QStackedWidget>
#include <QTextEdit>
#include <QVBoxLayout>

InlineEditDialog::InlineEditDialog(const QString &selectedText,
                                   const QString &filePath,
                                   const QString &language,
                                   QWidget *parent)
    : QDialog(parent),
      m_selected(selectedText),
      m_filePath(filePath),
      m_language(language)
{
    setWindowTitle("Inline Edit");
    setWindowModality(Qt::ApplicationModal);
    // Resize sensibly. The diff stage needs more horizontal room than
    // the prompt stage; we pick a compromise that avoids re-layouting
    // the dialog in the middle of a stream.
    resize(720, 420);

    auto *root = new QVBoxLayout(this);
    root->setContentsMargins(12, 10, 12, 10);
    root->setSpacing(8);

    // ── Header: language · N lines · file basename ─────────────────
    const int lineCount = m_selected.count('\n') + (m_selected.isEmpty() ? 0 : 1);
    QString basename;
    if (!m_filePath.isEmpty()) {
        const int slash = m_filePath.lastIndexOf('/');
        basename = (slash >= 0) ? m_filePath.mid(slash + 1) : m_filePath;
    }
    const QString langLabel = m_language.isEmpty() ? QStringLiteral("Plain Text") : m_language;
    QString summary = QString("%1 · %2 line%3")
        .arg(langLabel)
        .arg(lineCount)
        .arg(lineCount == 1 ? "" : "s");
    if (!basename.isEmpty()) summary += " · " + basename;

    m_descLabel = new QLabel(summary, this);
    QFont descFont = m_descLabel->font();
    descFont.setBold(true);
    m_descLabel->setFont(descFont);
    root->addWidget(m_descLabel);

    // ── Stacked stages ──
    m_stack = new QStackedWidget(this);
    root->addWidget(m_stack, 1);

    // Stage 1 — Prompt input
    {
        auto *page = new QWidget(this);
        auto *v = new QVBoxLayout(page);
        v->setContentsMargins(0, 0, 0, 0);
        v->setSpacing(6);

        m_promptEdit = new QPlainTextEdit(page);
        m_promptEdit->setPlaceholderText(
            "Tell me what you want… (e.g. \"convert to async/await\", "
            "\"add error handling\", \"rewrite as one expression\")");
        v->addWidget(m_promptEdit, 1);

        auto *row = new QHBoxLayout;
        row->addStretch(1);
        m_cancelBtn = new QPushButton("Cancel", page);
        m_sendBtn   = new QPushButton("Send", page);
        m_sendBtn->setDefault(true);
        row->addWidget(m_cancelBtn);
        row->addWidget(m_sendBtn);
        v->addLayout(row);

        connect(m_cancelBtn, &QPushButton::clicked, this, &QDialog::reject);
        connect(m_sendBtn,   &QPushButton::clicked, this, &InlineEditDialog::onSendClicked);

        // Ctrl+Return / Cmd+Return = Send (parity with chat panels).
        auto *sendShortcut = new QShortcut(QKeySequence("Ctrl+Return"), page);
        connect(sendShortcut, &QShortcut::activated, this, &InlineEditDialog::onSendClicked);

        m_stack->addWidget(page);
    }

    // Stage 2 — Streaming feedback
    {
        auto *page = new QWidget(this);
        auto *v = new QVBoxLayout(page);
        v->setContentsMargins(0, 0, 0, 0);
        v->setSpacing(6);

        m_streamLabel = new QLabel("Generating…", page);
        v->addWidget(m_streamLabel);

        m_streamEdit = new QPlainTextEdit(page);
        m_streamEdit->setReadOnly(true);
        QFont mono = m_streamEdit->font();
        mono.setFamily("monospace");
        m_streamEdit->setFont(mono);
        v->addWidget(m_streamEdit, 1);

        auto *row = new QHBoxLayout;
        row->addStretch(1);
        auto *abortBtn = new QPushButton("Cancel", page);
        row->addWidget(abortBtn);
        v->addLayout(row);

        connect(abortBtn, &QPushButton::clicked, this, [this]() {
            if (m_ollama) m_ollama->cancel();
            reject();
        });

        m_stack->addWidget(page);
    }

    // Stage 3 — Diff
    {
        auto *page = new QWidget(this);
        auto *v = new QVBoxLayout(page);
        v->setContentsMargins(0, 0, 0, 0);
        v->setSpacing(6);

        auto *split = new QSplitter(Qt::Horizontal, page);

        m_oldView = new QTextEdit(page);
        m_oldView->setReadOnly(true);
        m_oldView->setLineWrapMode(QTextEdit::NoWrap);
        QFont mono = m_oldView->font();
        mono.setFamily("monospace");
        m_oldView->setFont(mono);

        m_newView = new QTextEdit(page);
        m_newView->setReadOnly(true);
        m_newView->setLineWrapMode(QTextEdit::NoWrap);
        m_newView->setFont(mono);

        split->addWidget(m_oldView);
        split->addWidget(m_newView);
        split->setSizes({1, 1});
        v->addWidget(split, 1);

        auto *row = new QHBoxLayout;
        auto *legend = new QLabel(
            "<span style='color:#a33'>red</span> = removed · "
            "<span style='color:#2a7'>green</span> = added", page);
        row->addWidget(legend);
        row->addStretch(1);
        m_discardBtn = new QPushButton("Cancel", page);
        m_applyBtn   = new QPushButton("Apply", page);
        m_applyBtn->setDefault(true);
        row->addWidget(m_discardBtn);
        row->addWidget(m_applyBtn);
        v->addLayout(row);

        connect(m_discardBtn, &QPushButton::clicked, this, &QDialog::reject);
        connect(m_applyBtn,   &QPushButton::clicked, this, &InlineEditDialog::onApplyClicked);

        m_stack->addWidget(page);
    }

    // Stage 4 — Error
    {
        auto *page = new QWidget(this);
        auto *v = new QVBoxLayout(page);
        v->setContentsMargins(0, 0, 0, 0);
        v->setSpacing(6);

        m_errorLabel = new QLabel(page);
        m_errorLabel->setWordWrap(true);
        m_errorLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
        m_errorLabel->setStyleSheet("color: #c33;");
        v->addWidget(m_errorLabel, 1, Qt::AlignTop);

        auto *row = new QHBoxLayout;
        row->addStretch(1);
        auto *closeBtn = new QPushButton("Close", page);
        m_retryBtn = new QPushButton("Retry", page);
        row->addWidget(closeBtn);
        row->addWidget(m_retryBtn);
        v->addLayout(row);

        connect(closeBtn,   &QPushButton::clicked, this, &QDialog::reject);
        connect(m_retryBtn, &QPushButton::clicked, this, [this]() { setStage(Stage::Prompt); });

        m_stack->addWidget(page);
    }

    setStage(Stage::Prompt);
    m_promptEdit->setFocus();
}

InlineEditDialog::~InlineEditDialog() {
    // m_ollama is parented to `this`, so Qt destroys it. Just make sure
    // any in-flight stream is aborted before we go away.
    if (m_ollama) m_ollama->cancel();
}

// ─────────────────────────────────────────────────────────────────────

void InlineEditDialog::setStage(Stage s) {
    switch (s) {
        case Stage::Prompt:    m_stack->setCurrentIndex(0); break;
        case Stage::Streaming: m_stack->setCurrentIndex(1); break;
        case Stage::Diff:      m_stack->setCurrentIndex(2); break;
        case Stage::ErrorView: m_stack->setCurrentIndex(3); break;
    }
}

void InlineEditDialog::onSendClicked() {
    const QString instruction = m_promptEdit->toPlainText().trimmed();
    if (instruction.isEmpty()) {
        m_promptEdit->setFocus();
        return;
    }

    // Lazily construct OllamaClient. It self-configures from
    // Config::instance() in its constructor (backend, base URL, key).
    if (!m_ollama) {
        m_ollama = new OllamaClient(this);
        connect(m_ollama, &OllamaClient::tokenReceived, this, &InlineEditDialog::onTokenReceived);
        connect(m_ollama, &OllamaClient::finished,      this, &InlineEditDialog::onResponseFinished);
        connect(m_ollama, &OllamaClient::error,         this, &InlineEditDialog::onResponseError);
    }
    // OllamaClient defaults to "qwen2.5-coder:3b" — a sensible local
    // code-rewrite model. We don't override here because the user's
    // selected model in the AI panel isn't exposed via Config; using
    // the default keeps Ctrl+I self-contained and predictable.

    m_streamEdit->clear();
    m_lastResult.clear();
    m_streamLabel->setText("Generating…");
    setStage(Stage::Streaming);

    // System prompt — keep it tight. Models love to wrap output in
    // ```language fences and add explanatory prose; we strip both
    // server-side via stripFences() but tell the model not to do it
    // anyway so the diff is clean.
    const QString langForPrompt = m_language.isEmpty() ? QStringLiteral("code") : m_language;
    const QString sys = QString(
        "You rewrite a snippet of %1 source code to match the user's instruction. "
        "Output ONLY the rewritten snippet — no markdown fences, no commentary, "
        "no <think> blocks, no preamble. Preserve indentation style of the input. "
        "If the instruction is impossible, output the original snippet unchanged."
    ).arg(langForPrompt);

    const QString user = QString(
        "Instruction: %1\n\n"
        "Original snippet:\n%2\n\n"
        "Output the rewritten snippet now (raw text, no fences)."
    ).arg(instruction, m_selected);

    m_ollama->generate(user, sys, /*enableThinking=*/false);
}

void InlineEditDialog::onTokenReceived(const QString &token) {
    m_streamEdit->insertPlainText(token);
    // Scroll to bottom so the user sees the latest output.
    auto *bar = m_streamEdit->verticalScrollBar();
    if (bar) bar->setValue(bar->maximum());
}

void InlineEditDialog::onResponseFinished(const QString &full) {
    m_lastResult = stripFences(full);
    if (m_lastResult.isEmpty()) {
        // Empty response — surface as an error rather than showing a
        // diff that just deletes the entire selection.
        m_errorLabel->setText(
            "AI returned an empty response. Try a different instruction or model.");
        setStage(Stage::ErrorView);
        return;
    }
    renderDiff(m_selected, m_lastResult);
    setStage(Stage::Diff);
}

void InlineEditDialog::onResponseError(const QString &message) {
    m_errorLabel->setText("AI request failed: " + message);
    setStage(Stage::ErrorView);
}

void InlineEditDialog::onApplyClicked() {
    emit applyRequested(m_lastResult);
    accept();
}

// ─────────────────────────────────────────────────────────────────────

QString InlineEditDialog::stripFences(const QString &raw) {
    QString s = raw.trimmed();

    // 1. Strip <think>...</think> reasoning blocks (defensive — our
    //    generate() call passes enableThinking=false but some models
    //    emit them anyway).
    static const QRegularExpression thinkRe(
        "<think>.*?</think>",
        QRegularExpression::DotMatchesEverythingOption);
    s.remove(thinkRe).replace("</think>", "");
    s = s.trimmed();

    // 2. Strip surrounding ``` fences. Match ```lang\n...\n``` or
    //    plain ```...```. Only strip if the entire payload is wrapped
    //    in a single fence — partial fences would mean the model is
    //    emitting commentary outside which we would lose.
    if (s.startsWith("```")) {
        const int firstNl = s.indexOf('\n');
        if (firstNl > 0 && s.endsWith("```")) {
            s = s.mid(firstNl + 1, s.length() - firstNl - 1 - 3);
            s = s.trimmed();
        }
    }
    return s;
}

// Renders a naive line-aligned diff into the side-by-side QTextEdits.
// Lines that match exactly get a neutral background. Lines on either
// side that don't have an equal counterpart at the same index get a
// red (old) or green (new) tint. For inline edits the selections are
// short enough that a fancier algorithm isn't worth the complexity.
void InlineEditDialog::renderDiff(const QString &oldText, const QString &newText) {
    const QStringList oldLines = oldText.split('\n');
    const QStringList newLines = newText.split('\n');

    auto escape = [](const QString &s) {
        return s.toHtmlEscaped().replace(' ', "&nbsp;");
    };

    const QString redBg   = "background:#fde4e4;color:#5a1212;";
    const QString greenBg = "background:#e3f7e3;color:#0e4d0e;";

    QString oldHtml = "<pre style='margin:0;padding:0;font-family:monospace;'>";
    QString newHtml = oldHtml;

    const int n = qMax(oldLines.size(), newLines.size());
    for (int i = 0; i < n; ++i) {
        const QString o = (i < oldLines.size()) ? oldLines.at(i) : QString();
        const QString nw = (i < newLines.size()) ? newLines.at(i) : QString();
        const bool same = (i < oldLines.size()) && (i < newLines.size()) && (o == nw);

        if (i < oldLines.size()) {
            const QString style = same ? QString() : redBg;
            oldHtml += QString("<div style='%1'>%2</div>")
                .arg(style, escape(o).isEmpty() ? "&nbsp;" : escape(o));
        } else {
            oldHtml += "<div>&nbsp;</div>";
        }

        if (i < newLines.size()) {
            const QString style = same ? QString() : greenBg;
            newHtml += QString("<div style='%1'>%2</div>")
                .arg(style, escape(nw).isEmpty() ? "&nbsp;" : escape(nw));
        } else {
            newHtml += "<div>&nbsp;</div>";
        }
    }
    oldHtml += "</pre>";
    newHtml += "</pre>";

    m_oldView->setHtml(oldHtml);
    m_newView->setHtml(newHtml);
}
