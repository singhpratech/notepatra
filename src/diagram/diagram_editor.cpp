// SPDX-License-Identifier: GPL-3.0-or-later

#include "diagram_editor.h"

#include "diagram_ai_dialog.h"
#include "diagram_view.h"
#include "mermaid_import.h"
#include "npd_parser.h"

#include <QDialog>
#include <QDialogButtonBox>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QFont>
#include <QFontDatabase>
#include <QHBoxLayout>
#include <QLabel>
#include <QMenu>
#include <QMessageBox>
#include <QPalette>
#include <QPlainTextEdit>
#include <QSplitter>
#include <QStyle>
#include <QTextBrowser>
#include <QTimer>
#include <QToolButton>
#include <QVBoxLayout>

namespace {

// ── starter templates (every one parses clean + showcases the grammar) ──
const char *kFlowTemplate =
    "diagram flow\n"
    "title \"Order checkout\"\n"
    "palette auto\n"
    "direction LR\n"
    "\n"
    "# Shapes: (pill) [box] {decision} ([database]) and icon nodes.\n"
    "node start (Customer) green :: \"Signed-in shopper on the web or mobile app\"\n"
    "icon gw :gateway \"API gateway\" #1565c0 :: \"Auth, rate limiting, request routing\"\n"
    "node cart [Cart service] #1565c0 :: \"Owns the basket; idempotent on retry\"\n"
    "node stock {In stock?} orange\n"
    "icon pay :card \"Payments\" #c62828 :: \"Third-party PSP — card capture and 3-D Secure\"\n"
    "node order [Order service] #1565c0 :: \"Writes the order, emits OrderPlaced\"\n"
    "icon bus :queue \"Event bus\" #6a1b9a :: \"Kafka — at-least-once delivery\"\n"
    "icon mail :email \"Receipt mailer\" :: \"Fire-and-forget consumer\"\n"
    "node oos [Back-order] :: \"Customer is told the ETA and may wait or cancel\"\n"
    "node db ([Orders DB]) #6a1b9a :: \"Postgres — source of truth\"\n"
    "icon cache :cache \"Cache\" #6a1b9a :: \"Redis — hot order rows, 5-minute TTL\"\n"
    "\n"
    "# Groups draw a container around their members.\n"
    "group \"Edge\" : start gw\n"
    "group \"Core services\" : cart stock order\n"
    "group \"Data\" : db cache\n"
    "\n"
    "# Edges: chains, labels, a decision with two branches, a dashed async hop,\n"
    "# and a bidirectional edge.\n"
    "start <-> gw : HTTPS\n"
    "gw -> cart : add item\n"
    "cart -> stock\n"
    "stock -> order : yes\n"
    "stock -> oos : no\n"
    "order -> pay : capture\n"
    "order -> db : insert\n"
    "order -> cache : write-through\n"
    "order -.-> bus -.-> mail : receipt\n"
    "\n"
    "# Notes sit beside a node; legend explains any encoding you reuse.\n"
    "note pay \"Card capture is retried three times with exponential backoff.\"\n"
    "note bus \"Consumers must be idempotent.\"\n"
    "legend dashed \"async / best effort\"\n"
    "legend #c62828 \"third-party call\"\n"
    "\n"
    "textbox \"Solid arrows are synchronous; the mailer is fire-and-forget. Hover a node for detail.\"\n";

const char *kErTemplate =
    "diagram er\n"
    "title \"Orders schema\"\n"
    "palette auto\n"
    "\n"
    "node customers ([Customers]) #2e7d32 :: \"id, name, email, created_at\"\n"
    "node products ([Products]) #2e7d32 :: \"id, sku, name, unit_price\"\n"
    "node orders ([Orders]) #1565c0 :: \"id, customer_id, total, placed_at\"\n"
    "node items ([Order items]) #1565c0 :: \"id, order_id, product_id, qty, unit_price\"\n"
    "\n"
    "customers -> orders : places\n"
    "orders -> items : contains\n"
    "products -> items : referenced by\n"
    "\n"
    "textbox \"Colour groups the entities — green = master data, blue = transactional. Colour is optional.\"\n";

const char *kSystemTemplate =
    "diagram system\n"
    "title \"Web app architecture\"\n"
    "palette auto\n"
    "\n"
    "icon user :user \"Browser\" #00838f :: \"End-user web client\"\n"
    "icon cdn :cloud \"CDN\" #00838f :: \"Static assets + edge cache\"\n"
    "node api [API gateway] #1565c0 :: \"Auth, rate-limiting, routing\"\n"
    "icon svc :server \"App service\" #1565c0 :: \"Stateless; horizontally autoscaled\"\n"
    "icon cache :database \"Cache\" #6a1b9a :: \"Redis — sessions + hot rows\"\n"
    "icon db :database \"Primary DB\" #6a1b9a :: \"Source of truth\"\n"
    "\n"
    "user -> cdn\n"
    "user -> api\n"
    "api -> svc\n"
    "svc -> cache\n"
    "svc -> db\n"
    "cache <-> db : warm\n"
    "\n"
    "textbox \"Colour marks the tier — teal = edge, blue = app, purple = data. Colour is optional.\"\n";

}  // namespace

DiagramEditor::DiagramEditor(QWidget *parent) : QWidget(parent) {
    auto *root = new QVBoxLayout(this);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(0);

    // ── toolbar ──
    auto *bar = new QWidget(this);
    bar->setObjectName("diagramToolbar");
    auto *barLay = new QHBoxLayout(bar);
    barLay->setContentsMargins(8, 6, 8, 6);
    barLay->setSpacing(6);

    auto *newBtn = makeMenuButton("New", "Start a new diagram from a template");
    auto *newMenu = new QMenu(newBtn);
    newMenu->addAction("Flow chart", this, [this] { setNpdText(QString::fromUtf8(kFlowTemplate)); m_path.clear(); m_dirty = false; emitTitle(); });
    newMenu->addAction("ER diagram", this, [this] { setNpdText(QString::fromUtf8(kErTemplate)); m_path.clear(); m_dirty = false; emitTitle(); });
    newMenu->addAction("System design", this, [this] { setNpdText(QString::fromUtf8(kSystemTemplate)); m_path.clear(); m_dirty = false; emitTitle(); });
    newBtn->setMenu(newMenu);
    barLay->addWidget(newBtn);

    auto *openBtn = new QToolButton(bar);
    openBtn->setText("Open");
    openBtn->setToolButtonStyle(Qt::ToolButtonTextOnly);
    openBtn->setIcon(style()->standardIcon(QStyle::SP_DialogOpenButton));
    openBtn->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    openBtn->setToolTip("Open a .npd diagram file");
    connect(openBtn, &QToolButton::clicked, this, &DiagramEditor::openFile);
    barLay->addWidget(openBtn);

    auto *saveBtn = new QToolButton(bar);
    saveBtn->setText("Save");
    saveBtn->setIcon(style()->standardIcon(QStyle::SP_DialogSaveButton));
    saveBtn->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    saveBtn->setToolTip("Save the .npd source");
    connect(saveBtn, &QToolButton::clicked, this, [this] { saveFile(); });
    barLay->addWidget(saveBtn);

    auto *aiBtn = new QToolButton(bar);
    aiBtn->setText("AI Generate");
    aiBtn->setToolButtonStyle(Qt::ToolButtonTextOnly);
    aiBtn->setToolTip("Describe a diagram in words; review the generated .npd before inserting");
    connect(aiBtn, &QToolButton::clicked, this, &DiagramEditor::generateWithAi);
    barLay->addWidget(aiBtn);

    auto *mmBtn = new QToolButton(bar);
    mmBtn->setText("Import Mermaid");
    mmBtn->setToolButtonStyle(Qt::ToolButtonTextOnly);
    mmBtn->setToolTip("Paste a Mermaid flowchart and convert it to .npd");
    connect(mmBtn, &QToolButton::clicked, this, &DiagramEditor::importMermaid);
    barLay->addWidget(mmBtn);

    auto *exportBtn = makeMenuButton("Export", "Export the rendered diagram");
    auto *exportMenu = new QMenu(exportBtn);
    // Only list formats this build can actually produce (native Qt: PNG/JPEG/PDF
    // always; WebP only with the qwebp plugin; SVG/HTML only with Qt Svg).
    for (const QString &fmt : DiagramView::supportedExportFormats()) {
        exportMenu->addAction(fmt, this, [this, fmt] { m_pendingExportFmt = fmt.toLower(); exportAs(); });
    }
    exportBtn->setMenu(exportMenu);
    barLay->addWidget(exportBtn);

    auto *fitBtn = new QToolButton(bar);
    fitBtn->setText("Fit");
    fitBtn->setToolButtonStyle(Qt::ToolButtonTextOnly);
    fitBtn->setToolTip("Fit the diagram to the view");
    connect(fitBtn, &QToolButton::clicked, this, [this] { renderNow(); });
    barLay->addWidget(fitBtn);

    auto *helpBtn = new QToolButton(bar);
    helpBtn->setText("Help");
    helpBtn->setIcon(style()->standardIcon(QStyle::SP_MessageBoxQuestion));
    helpBtn->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    helpBtn->setToolTip("How to create a diagram + .npd syntax (shapes, icons, arrows)");
    connect(helpBtn, &QToolButton::clicked, this, &DiagramEditor::showSyntaxHelp);
    barLay->addWidget(helpBtn);

    barLay->addStretch(1);
    m_status = new QLabel("New diagram", bar);
    m_status->setObjectName("diagramStatus");
    barLay->addWidget(m_status);
    root->addWidget(bar);

    // ── split: source | preview ──
    auto *split = new QSplitter(Qt::Horizontal, this);
    m_edit = new QPlainTextEdit(split);
    m_edit->setObjectName("diagramSource");
    m_edit->setLineWrapMode(QPlainTextEdit::NoWrap);
    m_edit->setTabStopDistance(28);
    const QFont mono = QFontDatabase::systemFont(QFontDatabase::FixedFont);
    m_edit->setFont(mono);
    m_edit->setPlaceholderText("Write .npd here — or pick New ▾ for a template.\n\n"
                               "node a (Start)\nnode b [Process]\na -> b : go");
    split->addWidget(m_edit);

    m_preview = new DiagramView(split);
    split->addWidget(m_preview);
    split->setStretchFactor(0, 2);
    split->setStretchFactor(1, 3);
    split->setSizes({420, 680});
    root->addWidget(split, 1);

    // ── live render (debounced) + error surfacing ──
    m_debounce = new QTimer(this);
    m_debounce->setSingleShot(true);
    m_debounce->setInterval(350);
    connect(m_debounce, &QTimer::timeout, this, &DiagramEditor::renderNow);
    connect(m_edit, &QPlainTextEdit::textChanged, this, [this] {
        if (!m_dirty) { m_dirty = true; emitTitle(); }
        scheduleRender();
    });
    connect(m_preview, &DiagramView::renderError, this, [this](const QString &msg) {
        m_status->setText(msg);
        m_status->setStyleSheet("color:#b35900;");
    });

    onThemeChanged();
    setNpdText(QString::fromUtf8(kFlowTemplate));
    m_dirty = false;
    emitTitle();
}

QToolButton *DiagramEditor::makeMenuButton(const QString &text, const QString &tip) {
    auto *b = new QToolButton(this);
    b->setText(text);
    b->setToolTip(tip);
    b->setPopupMode(QToolButton::InstantPopup);
    b->setToolButtonStyle(Qt::ToolButtonTextOnly);
    return b;
}

void DiagramEditor::setNpdText(const QString &text) {
    m_edit->setPlainText(text);
    renderNow();
}

QString DiagramEditor::npdText() const { return m_edit->toPlainText(); }

void DiagramEditor::scheduleRender() { m_debounce->start(); }

void DiagramEditor::renderNow() {
    const QString src = m_edit->toPlainText();
    m_preview->setSource(src);
    const Npd::Diagram d = Npd::parse(src);
    if (d.ok()) {
        m_status->setText(QStringLiteral("%1 node%2 · %3 edge%4 · parsed clean")
                              .arg(d.nodes.size()).arg(d.nodes.size() == 1 ? "" : "s")
                              .arg(d.edges.size()).arg(d.edges.size() == 1 ? "" : "s"));
        m_status->setStyleSheet("color:#2e7d32;");
    } else {
        m_status->setText(QStringLiteral("%1 · (%2 node%3)")
                              .arg(d.errors.first())
                              .arg(d.nodes.size()).arg(d.nodes.size() == 1 ? "" : "s"));
        m_status->setStyleSheet("color:#b35900;");
    }
}

void DiagramEditor::newFromTemplate() { setNpdText(QString::fromUtf8(kFlowTemplate)); }

void DiagramEditor::openFile() {
    const QString path = QFileDialog::getOpenFileName(
        this, "Open diagram", QString(), "Notepatra diagram (*.npd);;All files (*)");
    if (path.isEmpty()) return;
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text)) {
        QMessageBox::warning(this, "Open diagram", "Could not read:\n" + path);
        return;
    }
    setNpdText(QString::fromUtf8(f.readAll()));
    m_path = path;
    m_dirty = false;
    emitTitle();
}

bool DiagramEditor::saveFile() {
    QString path = m_path;
    if (path.isEmpty()) {
        path = QFileDialog::getSaveFileName(this, "Save diagram", "diagram.npd",
                                            "Notepatra diagram (*.npd)");
        if (path.isEmpty()) return false;
        if (!path.endsWith(".npd", Qt::CaseInsensitive)) path += ".npd";
    }
    QFile f(path);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Text)) {
        QMessageBox::warning(this, "Save diagram", "Could not write:\n" + path);
        return false;
    }
    f.write(m_edit->toPlainText().toUtf8());
    f.close();
    m_path = path;
    m_dirty = false;
    emitTitle();
    return true;
}

void DiagramEditor::exportAs() {
    const QString fmt = m_pendingExportFmt.isEmpty() ? QStringLiteral("png") : m_pendingExportFmt;
    const QString ext = fmt;
    const QString base = m_path.isEmpty() ? QStringLiteral("diagram")
                                          : QFileInfo(m_path).completeBaseName();
    const QString path = QFileDialog::getSaveFileName(
        this, "Export diagram", base + "." + ext,
        fmt.toUpper() + " (*." + ext + ");;All files (*)");
    if (path.isEmpty()) return;
    if (m_preview->exportTo(fmt, path)) {
        m_status->setText("Exported " + QFileInfo(path).fileName());
        m_status->setStyleSheet("color:#2e7d32;");
    }
    // failure path emits renderError → already shown in the status strip
}

void DiagramEditor::generateWithAi() {
    DiagramAiDialog dlg(this);
    if (dlg.exec() == QDialog::Accepted) {
        const QString npd = dlg.resultNpd().trimmed();
        if (!npd.isEmpty()) {
            setNpdText(npd);   // single undoable replace → redo/undo for free
            m_path.clear();
            m_dirty = true;
            emitTitle();
        }
    }
}

void DiagramEditor::importMermaid() {
    QDialog dlg(this);
    dlg.setWindowTitle("Import Mermaid");
    dlg.resize(560, 460);
    auto *lay = new QVBoxLayout(&dlg);
    lay->addWidget(new QLabel("Paste a Mermaid flowchart, then Import:", &dlg));
    auto *edit = new QPlainTextEdit(&dlg);
    edit->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
    edit->setPlaceholderText("flowchart TD\n"
                             "  A[Start] --> B{OK?}\n"
                             "  B -->|yes| C[Done]\n"
                             "  B -->|no| A");
    lay->addWidget(edit, 1);
    auto *bb = new QDialogButtonBox(QDialogButtonBox::Cancel, &dlg);
    bb->addButton("Import", QDialogButtonBox::AcceptRole);
    connect(bb, &QDialogButtonBox::accepted, &dlg, &QDialog::accept);
    connect(bb, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);
    lay->addWidget(bb);
    if (dlg.exec() == QDialog::Accepted) {
        const QString npd = Npd::mermaidToNpd(edit->toPlainText());
        if (!npd.trimmed().isEmpty()) {
            setNpdText(npd);
            m_path.clear();
            m_dirty = true;
            emitTitle();
        }
    }
}

void DiagramEditor::showSyntaxHelp() {
    QDialog dlg(this);
    dlg.setWindowTitle("Diagrams — how to create one");
    dlg.resize(640, 620);
    auto *lay = new QVBoxLayout(&dlg);
    auto *browser = new QTextBrowser(&dlg);
    browser->setOpenExternalLinks(false);
    browser->setHtml(QStringLiteral(R"HTML(
<h2>Three ways to create a diagram</h2>
<ol>
<li><b>AI Generate</b> — click it, describe what you want in plain English
    ("a login flow with validation; users in a database"), review the generated
    diagram, then Insert. <i>No syntax to learn.</i></li>
<li><b>New</b> — start from a Flow, ER, or System template and tweak it.</li>
<li><b>Write .npd</b> in the left pane — the canvas on the right updates as you type.
    (Or <b>Import Mermaid</b> if you already have a Mermaid flowchart.)</li>
</ol>

<h3>Shapes — the outline around a node</h3>
<pre>node a (Start)          pill   — start / end
node b [Process]        box    — default
node c {Valid?}         diamond — decision
node d ([Users])        cylinder — database</pre>

<h3>Icons — rich glyphs</h3>
<pre>icon db :database "Users DB"</pre>
<p>Icon names: <code>database, server, user, patient, hospital, document,
cloud, gear, table, process, decision, chart</code>.</p>

<h3>Arrows &amp; connections</h3>
<pre>a -&gt; b                  arrow a to b
a -&gt; b : yes            arrow with a label (space before the colon)
a &lt;-&gt; b : sync          bidirectional (label optional)
a -.-&gt; b                dashed — async / optional / best-effort
a &lt;.-&gt; b                dashed both ways
a -&gt; b -&gt; c : ok        chain — a to b, then b to c; label rides the last hop</pre>
<p>Colours go on <b>node</b> lines (see below), not on connection lines.</p>

<h3>Keep shapes clean — detail on hover</h3>
<pre>node dash [Dashboard] :: "Loads the saved session and open tabs"</pre>
<p>Short text stays in the shape; the full text shows when you hover the node.</p>

<h3>Colour individual symbols (optional)</h3>
<pre>node start (Start) green
node proc  [Process] #1565c0
node check {Valid?} orange
node err   [Error] red</pre>
<p>Add a colour <b>after the shape</b> — a <code>#hex</code> value (<code>#1565c0</code>)
or a common name (green, blue, orange, red, teal, purple…). On a <b>light</b> palette
(paper, slate) the node is <b>tinted</b>: a soft wash of the colour, a full-strength
border and the normal dark text. On a <b>dark</b> palette the node is filled solid with
auto-contrast text. Either way it stays readable. Nodes with no colour keep the
diagram <b>palette</b>, so leaving them all uncoloured gives the clean monochrome look.</p>
<p>Colour works on <b>every node type and every diagram</b> — boxes, pills, decisions,
database cylinders (<code>node t ([Table]) green</code>) and icons
(<code>icon db :database "DB" #1565c0</code>) in flow, ER <em>and</em> system diagrams alike.
The colour goes after the shape and before any <code>::</code> hover.</p>

<h3>Group, annotate, explain</h3>
<pre>group "Edge tier" : cdn api      draws a labelled container behind those nodes
note api "Rate-limited to 100 rps"   a small card pinned beside the node
legend dashed "async"            legend row — a dashed-line swatch
legend #cc785c "hot path"        legend row — a colour swatch</pre>
<p>Every id in a <code>group</code> must be a real node, and a node can be in
<b>one</b> group only. The legend box appears only when you write at least one
<code>legend</code> line.</p>

<h3>Title, direction, palette, caption</h3>
<pre>diagram flow            flow | er | system
title "User Login"
direction LR            TB (top-down, default) | LR (left-to-right)
palette auto            auto | paper | slate | clay | ocean | forest | mono | default
textbox "A caption shown under the diagram."</pre>
<p><b>auto</b> (the default when you write no palette line) follows the app theme:
the light <b>paper</b> palette in a light theme, the dark <b>default</b> one in a
dark theme. <b>paper</b> and <b>slate</b> are light; the rest are dark.</p>

<h3>Canvas &amp; export</h3>
<p>Drag to pan, scroll to zoom, double-click to fit. Export the rendered diagram
with the <b>Export</b> button — <b>PNG, JPEG and PDF</b> on every build, plus
<b>SVG, HTML and WebP</b> where your Qt has the Svg module / WebP image plugin.
The menu only lists formats this build can actually write.</p>

<h3>A complete example</h3>
<pre>diagram flow
title "User Login"
palette auto
node start (Start) green
node check {Valid?} orange
node dash [Dashboard] #1565c0 :: "Loads saved session + tabs"
icon db :database "Users"
start -&gt; check
check -&gt; dash : yes
check -&gt; start : no
dash -&gt; db</pre>
)HTML"));
    lay->addWidget(browser, 1);
    auto *bb = new QDialogButtonBox(QDialogButtonBox::Close, &dlg);
    connect(bb, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);
    connect(bb, &QDialogButtonBox::accepted, &dlg, &QDialog::accept);
    lay->addWidget(bb);
    dlg.exec();
}

void DiagramEditor::emitTitle() {
    QString name = m_path.isEmpty() ? QStringLiteral("Diagram") : QFileInfo(m_path).fileName();
    if (m_dirty) name += " •";   // bullet = unsaved
    emit titleChanged(name);
}

void DiagramEditor::onThemeChanged() {
    // `palette auto` resolves against the host theme, so a theme switch has to
    // re-render the preview, not just restyle the toolbar.
    if (m_preview && m_edit) renderNow();
    const bool dark = palette().color(QPalette::Window).lightness() < 128;
    const QString barBg = dark ? "#2b2b2b" : "#f3f1ea";
    const QString line = dark ? "#3a3a3a" : "#dcd8cc";
    setStyleSheet(QStringLiteral(
        "#diagramToolbar{background:%1;border-bottom:1px solid %2;}"
        "#diagramToolbar QToolButton{padding:4px 10px;border:1px solid transparent;border-radius:5px;}"
        "#diagramToolbar QToolButton:hover{background:rgba(127,127,127,0.18);}"
        "#diagramStatus{color:%3;padding-right:4px;}")
        .arg(barBg, line, dark ? "#cfcfcf" : "#555"));
}
