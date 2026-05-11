#include "ai_log_dialog.h"
#include "ai_interaction_log.h"
#include "config.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDateTime>
#include <QFileDialog>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSplitter>
#include <QTableWidget>
#include <QTableWidgetItem>
#include <QTextStream>
#include <QVBoxLayout>

namespace {

QString roleLabel(AiInteractionLog::Role r) {
    using R = AiInteractionLog::Role;
    switch (r) {
        case R::User:        return "user";
        case R::System:      return "system";
        case R::Assistant:   return "assistant";
        case R::ToolCall:    return "tool_call";
        case R::ToolResult:  return "tool_result";
    }
    return "?";
}

QString shortLine(const QString &s, int max = 80) {
    QString trimmed = s.trimmed();
    trimmed.replace('\n', QChar(0x21B5));
    if (trimmed.size() > max) trimmed = trimmed.left(max - 1) + QStringLiteral("…");
    return trimmed;
}

} // namespace

AiLogDialog::AiLogDialog(QWidget *parent) : QDialog(parent) {
    setWindowTitle(tr("AI Interaction Log"));
    resize(1100, 680);

    auto *outer = new QVBoxLayout(this);
    outer->setContentsMargins(14, 14, 14, 14);
    outer->setSpacing(10);

    // Top row: filters + actions.
    auto *filterRow = new QHBoxLayout;
    filterRow->setSpacing(8);

    filterRow->addWidget(new QLabel(tr("Backend:")));
    m_backendFilter = new QComboBox;
    m_backendFilter->addItem(tr("(all)"), "");
    m_backendFilter->addItem("ollama", "ollama");
    m_backendFilter->addItem("ollama-cloud", "ollama-cloud");
    m_backendFilter->addItem("llama.cpp", "llama.cpp");
    m_backendFilter->addItem("openrouter", "openrouter");
    m_backendFilter->addItem("openai", "openai");
    m_backendFilter->addItem("azure-openai", "azure-openai");
    m_backendFilter->addItem("openai-compat", "openai-compat");
    filterRow->addWidget(m_backendFilter);

    filterRow->addWidget(new QLabel(tr("Mode:")));
    m_modeFilter = new QComboBox;
    m_modeFilter->addItem(tr("(all)"), "");
    m_modeFilter->addItem("chat",   "chat");
    m_modeFilter->addItem("coding", "coding");
    m_modeFilter->addItem("data",   "data");
    filterRow->addWidget(m_modeFilter);

    filterRow->addWidget(new QLabel(tr("Model:")));
    m_modelFilter = new QLineEdit;
    m_modelFilter->setPlaceholderText(tr("substring match — empty = all"));
    m_modelFilter->setMinimumWidth(160);
    filterRow->addWidget(m_modelFilter);

    auto *refreshBtn = new QPushButton(tr("Refresh"));
    auto *exportBtn  = new QPushButton(tr("Export JSON…"));
    auto *pruneBtn   = new QPushButton(tr("Prune now"));
    filterRow->addWidget(refreshBtn);
    filterRow->addWidget(exportBtn);
    filterRow->addWidget(pruneBtn);
    filterRow->addStretch();

    outer->addLayout(filterRow);

    // Status row: enable toggle + log path.
    auto *statusRow = new QHBoxLayout;
    m_enabledToggle = new QCheckBox(tr("Log AI interactions (7-day rotation)"));
    m_enabledToggle->setChecked(AiInteractionLog::isEnabled());
    m_enabledToggle->setToolTip(tr(
        "When ON, every request/response sent to any cloud or local LLM "
        "is recorded into ~/.config/notepatra/ai-logs/interactions.db. "
        "Rows older than 7 days are auto-pruned. Total DB size capped at "
        "50 MB. Credentials are scrubbed before write."));
    statusRow->addWidget(m_enabledToggle);
    statusRow->addStretch();
    auto *pathLbl = new QLabel(
        QStringLiteral("<span style='color:#777;'>%1</span>")
            .arg(AiInteractionLog::databasePath()));
    pathLbl->setTextInteractionFlags(Qt::TextSelectableByMouse);
    statusRow->addWidget(pathLbl);
    outer->addLayout(statusRow);

    // Splitter: table on top, detail panel below.
    auto *split = new QSplitter(Qt::Vertical, this);
    split->setChildrenCollapsible(false);

    m_table = new QTableWidget;
    m_table->setColumnCount(7);
    m_table->setHorizontalHeaderLabels({
        tr("When"), tr("Backend"), tr("Model"), tr("Mode"),
        tr("Role"), tr("Preview"), tr("Tokens/Time")});
    m_table->horizontalHeader()->setStretchLastSection(false);
    m_table->horizontalHeader()->setSectionResizeMode(5, QHeaderView::Stretch);
    m_table->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_table->setSelectionMode(QAbstractItemView::SingleSelection);
    m_table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_table->verticalHeader()->setVisible(false);
    split->addWidget(m_table);

    m_detail = new QPlainTextEdit;
    m_detail->setReadOnly(true);
    m_detail->setPlaceholderText(tr("Select a row to view the full content."));
    QFont mono("Monospace"); mono.setStyleHint(QFont::TypeWriter);
    m_detail->setFont(mono);
    split->addWidget(m_detail);
    split->setStretchFactor(0, 3);
    split->setStretchFactor(1, 2);
    outer->addWidget(split, 1);

    auto *bottomRow = new QHBoxLayout;
    bottomRow->addStretch();
    auto *closeBtn = new QPushButton(tr("Close"));
    bottomRow->addWidget(closeBtn);
    outer->addLayout(bottomRow);

    connect(refreshBtn,      &QPushButton::clicked, this, &AiLogDialog::onRefresh);
    connect(exportBtn,       &QPushButton::clicked, this, &AiLogDialog::onExportJson);
    connect(pruneBtn,        &QPushButton::clicked, this, &AiLogDialog::onPruneNow);
    connect(closeBtn,        &QPushButton::clicked, this, &QDialog::accept);
    connect(m_backendFilter, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, [this](int){ onRefresh(); });
    connect(m_modeFilter,    QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, [this](int){ onRefresh(); });
    connect(m_modelFilter,   &QLineEdit::returnPressed, this, &AiLogDialog::onRefresh);
    connect(m_table,         &QTableWidget::currentCellChanged,
            this, [this](int,int,int,int){ onRowChanged(); });
    connect(m_enabledToggle, &QCheckBox::toggled, this, &AiLogDialog::onLoggingToggled);

    reloadFromDb();
}

void AiLogDialog::reloadFromDb() {
    AiInteractionLog::Filter f;
    f.backend = m_backendFilter->currentData().toString();
    f.mode    = m_modeFilter->currentData().toString();
    f.limit   = 1000;
    const QString modelFilterText = m_modelFilter->text().trimmed();

    const QVector<AiInteractionLog::Event> rows = AiInteractionLog::query(f);
    m_table->setRowCount(0);
    for (const auto &e : rows) {
        if (!modelFilterText.isEmpty() &&
            !e.model.contains(modelFilterText, Qt::CaseInsensitive)) continue;

        int r = m_table->rowCount();
        m_table->insertRow(r);
        const QDateTime when = QDateTime::fromSecsSinceEpoch(e.ts);
        auto *whenItem    = new QTableWidgetItem(when.toString("yyyy-MM-dd HH:mm:ss"));
        whenItem->setData(Qt::UserRole, QVariant::fromValue(e.id));
        m_table->setItem(r, 0, whenItem);
        m_table->setItem(r, 1, new QTableWidgetItem(e.backend));
        m_table->setItem(r, 2, new QTableWidgetItem(e.model));
        m_table->setItem(r, 3, new QTableWidgetItem(e.mode));
        m_table->setItem(r, 4, new QTableWidgetItem(roleLabel(e.role)));

        QString preview;
        if (!e.error.isEmpty())          preview = QStringLiteral("⚠ %1").arg(e.error);
        else if (!e.toolName.isEmpty())  preview = QStringLiteral("[%1] %2")
                                            .arg(e.toolName, shortLine(
                                                e.toolArgs.isEmpty() ? e.toolResult : e.toolArgs));
        else                             preview = shortLine(e.content);
        m_table->setItem(r, 5, new QTableWidgetItem(preview));

        QString stats;
        if (e.evalTokens > 0)    stats += QString("%1 tok").arg(e.evalTokens);
        if (e.elapsedMs > 0) {
            if (!stats.isEmpty()) stats += " · ";
            stats += QString("%1 ms").arg(e.elapsedMs);
        }
        m_table->setItem(r, 6, new QTableWidgetItem(stats));
    }
    m_table->resizeColumnToContents(0);
    m_table->resizeColumnToContents(1);
    m_table->resizeColumnToContents(4);
}

void AiLogDialog::onRefresh()    { reloadFromDb(); }

void AiLogDialog::onRowChanged() {
    const int r = m_table->currentRow();
    if (r < 0) { m_detail->clear(); return; }
    auto *whenItem = m_table->item(r, 0);
    if (!whenItem) return;
    const qint64 id = whenItem->data(Qt::UserRole).toLongLong();

    // Re-fetch the full content (the table only carries the preview).
    AiInteractionLog::Filter f; f.limit = 1000;
    const QVector<AiInteractionLog::Event> rows = AiInteractionLog::query(f);
    for (const auto &e : rows) {
        if (e.id != id) continue;
        QString body;
        QTextStream ts(&body);
        ts << "When        : " << QDateTime::fromSecsSinceEpoch(e.ts).toString(Qt::ISODate) << "\n"
           << "Session     : " << e.sessionId << "\n"
           << "Backend     : " << e.backend << "\n"
           << "Model       : " << e.model << "\n"
           << "Mode        : " << e.mode << "\n"
           << "Role        : " << roleLabel(e.role) << "\n";
        if (e.promptTokens >= 0) ts << "Prompt tok  : " << e.promptTokens << "\n";
        if (e.evalTokens   >= 0) ts << "Eval tok    : " << e.evalTokens << "\n";
        if (e.elapsedMs    >= 0) ts << "Elapsed ms  : " << e.elapsedMs << "\n";
        if (!e.toolName.isEmpty())   ts << "Tool        : " << e.toolName << "\n";
        if (!e.toolArgs.isEmpty())   ts << "Tool args   :\n" << e.toolArgs << "\n";
        if (!e.toolResult.isEmpty()) ts << "Tool result :\n" << e.toolResult << "\n";
        if (!e.content.isEmpty())    ts << "Content     :\n" << e.content << "\n";
        if (!e.error.isEmpty())      ts << "Error       :\n" << e.error << "\n";
        m_detail->setPlainText(body);
        return;
    }
}

void AiLogDialog::onExportJson() {
    const QString suggest = QDir::homePath() + "/notepatra-ai-log-" +
        QDateTime::currentDateTime().toString("yyyyMMdd-HHmmss") + ".json";
    const QString path = QFileDialog::getSaveFileName(
        this, tr("Export AI interaction log"), suggest,
        tr("JSON files (*.json);;All files (*)"));
    if (path.isEmpty()) return;

    AiInteractionLog::Filter f; f.limit = 10000;
    const QVector<AiInteractionLog::Event> rows = AiInteractionLog::query(f);
    QJsonArray arr;
    for (const auto &e : rows) {
        QJsonObject o;
        o["id"]            = double(e.id);
        o["ts"]            = double(e.ts);
        o["session_id"]    = e.sessionId;
        o["backend"]       = e.backend;
        o["model"]         = e.model;
        o["mode"]          = e.mode;
        o["role"]          = roleLabel(e.role);
        if (!e.content.isEmpty())     o["content"]      = e.content;
        if (!e.toolName.isEmpty())    o["tool_name"]    = e.toolName;
        if (!e.toolArgs.isEmpty())    o["tool_args"]    = e.toolArgs;
        if (!e.toolResult.isEmpty())  o["tool_result"]  = e.toolResult;
        if (e.promptTokens >= 0)      o["prompt_tokens"] = e.promptTokens;
        if (e.evalTokens >= 0)        o["eval_tokens"]   = e.evalTokens;
        if (e.elapsedMs >= 0)         o["elapsed_ms"]    = e.elapsedMs;
        if (!e.error.isEmpty())       o["error"]         = e.error;
        arr.append(o);
    }
    QFile f2(path);
    if (!f2.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        QMessageBox::warning(this, tr("Export failed"),
            tr("Could not open %1 for writing").arg(path));
        return;
    }
    f2.write(QJsonDocument(arr).toJson(QJsonDocument::Indented));
    QMessageBox::information(this, tr("Export complete"),
        tr("Wrote %1 events to %2").arg(arr.size()).arg(path));
}

void AiLogDialog::onPruneNow() {
    AiInteractionLog::pruneOld();
    reloadFromDb();
    QFileInfo fi(AiInteractionLog::databasePath());
    QMessageBox::information(this, tr("Pruned"),
        tr("Rows older than 7 days dropped. DB size now %1 KB.")
            .arg(fi.size() / 1024));
}

void AiLogDialog::onLoggingToggled(bool on) {
    AiInteractionLog::setEnabled(on);
    if (!on) {
        QMessageBox::information(this, tr("Logging disabled"),
            tr("AI interaction recording is now OFF. Existing rows are "
               "preserved but no new events will be written until you "
               "re-enable from this toggle or Settings → Privacy."));
    }
}
