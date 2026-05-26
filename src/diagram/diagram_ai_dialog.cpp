// SPDX-License-Identifier: GPL-3.0-or-later

#include "diagram_ai_dialog.h"

#include "../ollama.h"

#include <QComboBox>
#include <QDialogButtonBox>
#include <QFont>
#include <QFontDatabase>
#include <QHBoxLayout>
#include <QLabel>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QRegularExpression>
#include <QVBoxLayout>

namespace {

// Teaches the model our DSL and pins it to emit ONLY .npd (no fences, no prose).
const char *kNpdSystemPrompt =
    "You generate diagrams in Notepatra's .npd text DSL. Output ONLY .npd source "
    "— no markdown fences, no JSON, no commentary, no explanation.\n\n"
    "Grammar (one statement per line; # starts a comment):\n"
    "  diagram flow|er|system          # optional, default flow\n"
    "  title \"...\"                     # optional\n"
    "  palette clay|ocean|forest|mono  # optional\n"
    "  node <id> (Label)               # pill — start / end\n"
    "  node <id> [Label]               # box — process (default)\n"
    "  node <id> {Label}               # decision — diamond\n"
    "  node <id> ([Label])             # database — cylinder\n"
    "  node <id> [Short] :: \"Full detail shown on hover\"\n"
    "  icon <id> :name \"Label\"         # rich-icon node\n"
    "  <a> -> <b>                       # directed edge\n"
    "  <a> -> <b> : label               # labelled edge\n"
    "  <a> <-> <b>                      # bidirectional\n"
    "  textbox \"caption\"\n\n"
    "icon names: database, server, user, patient, hospital, document, cloud, gear, "
    "table, process, decision, chart.\n"
    "Use short ids (a, b, db). Keep shape text concise; put detail after :: for hover. "
    "Choose correct shapes — decisions as {..}, datastores as ([..]) or :database icons.";

// Strip <think> blocks + any ``` fences so we keep clean .npd.
QString cleanNpd(QString s) {
    s.remove(QRegularExpression(QStringLiteral("<think>.*?</think>"),
                                QRegularExpression::DotMatchesEverythingOption
                                    | QRegularExpression::CaseInsensitiveOption));
    // pull the contents of a fenced block if the model wrapped it anyway
    QRegularExpression fence(QStringLiteral("```(?:npd|text|)?\\s*(.*?)```"),
                             QRegularExpression::DotMatchesEverythingOption);
    auto m = fence.match(s);
    if (m.hasMatch()) s = m.captured(1);
    return s.trimmed() + "\n";
}

}  // namespace

DiagramAiDialog::DiagramAiDialog(QWidget *parent) : QDialog(parent) {
    setWindowTitle("Generate diagram with AI");
    resize(640, 560);

    auto *root = new QVBoxLayout(this);

    auto *row = new QHBoxLayout;
    row->addWidget(new QLabel("Model:", this));
    m_model = new QComboBox(this);
    m_model->setEditable(true);
    m_model->setMinimumWidth(220);
    row->addWidget(m_model, 1);
    root->addLayout(row);

    root->addWidget(new QLabel("Describe the diagram you want:", this));
    m_prompt = new QPlainTextEdit(this);
    m_prompt->setPlaceholderText("e.g. A user-login flow: enter credentials, validate, "
                                 "go to the dashboard on success or show an error and retry. "
                                 "Users are stored in a database.");
    m_prompt->setMaximumHeight(96);
    root->addWidget(m_prompt);

    m_genBtn = new QPushButton("Generate", this);
    m_genBtn->setDefault(true);
    auto *genRow = new QHBoxLayout;
    genRow->addStretch(1);
    genRow->addWidget(m_genBtn);
    root->addLayout(genRow);

    root->addWidget(new QLabel("Review — edit freely before inserting:", this));
    m_review = new QPlainTextEdit(this);
    m_review->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
    m_review->setPlaceholderText("Generated .npd appears here for review.");
    root->addWidget(m_review, 1);

    m_status = new QLabel(QString(), this);
    m_status->setStyleSheet("color:#777;");
    root->addWidget(m_status);

    auto *bb = new QDialogButtonBox(this);
    m_insertBtn = bb->addButton("Insert", QDialogButtonBox::AcceptRole);
    bb->addButton(QDialogButtonBox::Cancel);
    m_insertBtn->setEnabled(false);
    connect(bb, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(bb, &QDialogButtonBox::rejected, this, &QDialog::reject);
    root->addWidget(bb);

    m_client = new OllamaClient(this);
    m_client->setMode("diagram");
    connect(m_genBtn, &QPushButton::clicked, this, &DiagramAiDialog::onGenerate);
    connect(m_client, &OllamaClient::finished, this, &DiagramAiDialog::onStreamFinished);
    connect(m_client, &OllamaClient::error, this, &DiagramAiDialog::onError);
    connect(m_client, &OllamaClient::modelsListed, this, [this](const QStringList &models) {
        const QString cur = m_model->currentText();
        m_model->clear();
        m_model->addItems(models);
        if (!cur.isEmpty()) m_model->setCurrentText(cur);
        else if (!models.isEmpty()) m_model->setCurrentIndex(0);
    });
    // re-enable Insert once there's something to insert
    connect(m_review, &QPlainTextEdit::textChanged, this, [this] {
        m_insertBtn->setEnabled(!m_review->toPlainText().trimmed().isEmpty());
    });
    m_client->listModels();
}

void DiagramAiDialog::setBusy(bool busy) {
    m_genBtn->setEnabled(!busy);
    m_prompt->setReadOnly(busy);
    m_genBtn->setText(busy ? "Generating…" : "Generate");
}

void DiagramAiDialog::onGenerate() {
    const QString ask = m_prompt->toPlainText().trimmed();
    if (ask.isEmpty()) {
        m_status->setText("Type a description first.");
        return;
    }
    const QString model = m_model->currentText().trimmed();
    if (!model.isEmpty()) m_client->setModel(model);
    setBusy(true);
    m_status->setText("Generating with " + (model.isEmpty() ? QStringLiteral("the local model") : model) + "…");
    // thinking off → clean, parseable .npd (same rationale as Data mode).
    m_client->generate(ask, QString::fromUtf8(kNpdSystemPrompt), /*enableThinking=*/false);
}

void DiagramAiDialog::onStreamFinished(const QString &full) {
    setBusy(false);
    const QString npd = cleanNpd(full);
    m_review->setPlainText(npd);
    m_status->setText("Review the .npd above, then Insert (or Generate again).");
    m_insertBtn->setEnabled(!npd.trimmed().isEmpty());
}

void DiagramAiDialog::onError(const QString &msg) {
    setBusy(false);
    m_status->setText("Generation failed: " + msg);
    m_status->setStyleSheet("color:#b35900;");
}

QString DiagramAiDialog::resultNpd() const {
    return m_review->toPlainText();
}
