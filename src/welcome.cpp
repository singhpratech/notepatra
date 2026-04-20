#include "welcome.h"
#include "config.h"
#include "fonts.h"

#include <QCheckBox>
#include <QEvent>
#include <QFileInfo>
#include <QFrame>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QMouseEvent>
#include <QPushButton>
#include <QScrollArea>
#include <QVBoxLayout>
#include <functional>

// Clickable card container — QPushButton with a nested QVBoxLayout was
// clipping the description text because QPushButton's internal sizeHint
// is driven by its text property, not the child layout. Using a QFrame
// that captures mousePressEvent via a std::function callback avoids the
// MOC/signals machinery and lets the layout drive sizing honestly.
namespace {
class ClickableCard : public QFrame {
public:
    explicit ClickableCard(QWidget *parent = nullptr) : QFrame(parent) {
        setCursor(Qt::PointingHandCursor);
        setAttribute(Qt::WA_StyledBackground, true);
    }
    void setOnClick(std::function<void()> cb) { m_cb = std::move(cb); }
protected:
    void mousePressEvent(QMouseEvent *event) override {
        if (event->button() == Qt::LeftButton && m_cb) m_cb();
    }
private:
    std::function<void()> m_cb;
};
}

#ifndef NOTEPATRA_VERSION
#define NOTEPATRA_VERSION "0.0.0-dev"
#endif

namespace {

static bool welcomeIsDark() {
    const QString &t = Config::instance().theme;
    return t.compare("Dark", Qt::CaseInsensitive) == 0 ||
           t.compare("Monokai", Qt::CaseInsensitive) == 0;
}

struct WelcomePalette {
    QString bg;
    QString cardBg;
    QString cardHover;
    QString cardBorder;
    QString textPrimary;
    QString textSecondary;
    QString textMuted;
    QString accent;
    QString accentText;
    QString shortcutBg;
    QString shortcutText;
};

static WelcomePalette welcomePalette() {
    if (welcomeIsDark()) {
        return {
            "#1E1E1E", "#252526", "#2D2D2F", "#3E3E42",
            "#E8E6E3", "#B8B5B1", "#6C6C6C",
            "#CC785C", "#FFFFFF",
            "#3A3A3A", "#E8E6E3"
        };
    }
    return {
        "#FAF9F5", "#FFFFFF", "#F5F4EE", "#E5E4DF",
        "#141413", "#54524E", "#8E8C88",
        "#CC785C", "#FFFFFF",
        "#E8E6E3", "#3A3A3A"
    };
}

static QString cardButtonStyle() {
    const auto p = welcomePalette();
    return QString(
        "QPushButton { "
        "  background: %1; "
        "  color: %2; "
        "  border: 1px solid %3; "
        "  border-radius: 8px; "
        "  padding: 14px 18px; "
        "  text-align: left; "
        "  font-size: 13px; "
        "} "
        "QPushButton:hover { background: %4; border-color: %5; }"
    ).arg(p.cardBg, p.textPrimary, p.cardBorder, p.cardHover, p.accent);
}

static QString primaryActionStyle() {
    const auto p = welcomePalette();
    return QString(
        "QPushButton { "
        "  background: %1; "
        "  color: %2; "
        "  border: none; "
        "  border-radius: 6px; "
        "  padding: 10px 20px; "
        "  font-size: 13px; "
        "  font-weight: 600; "
        "} "
        "QPushButton:hover { background: %3; }"
    ).arg(p.accent, p.accentText, "#B86A4E");
}

static QString secondaryActionStyle() {
    const auto p = welcomePalette();
    return QString(
        "QPushButton { "
        "  background: transparent; "
        "  color: %1; "
        "  border: 1px solid %2; "
        "  border-radius: 6px; "
        "  padding: 10px 20px; "
        "  font-size: 13px; "
        "  font-weight: 500; "
        "} "
        "QPushButton:hover { background: %3; }"
    ).arg(p.textPrimary, p.cardBorder, p.cardHover);
}

} // namespace

WelcomeWidget::WelcomeWidget(QWidget *parent) : QWidget(parent) {
    const auto p = welcomePalette();
    setStyleSheet(QString("WelcomeWidget { background: %1; }").arg(p.bg));

    auto *outer = new QVBoxLayout(this);
    outer->setContentsMargins(0, 0, 0, 0);
    outer->setSpacing(0);

    auto *scroll = new QScrollArea;
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setStyleSheet(QString("QScrollArea { background: %1; border: none; }").arg(p.bg));

    auto *content = new QWidget;
    content->setStyleSheet(QString("background: %1;").arg(p.bg));
    auto *layout = new QVBoxLayout(content);
    layout->setContentsMargins(60, 40, 60, 40);
    layout->setSpacing(36);

    buildHeroSection(layout);
    buildQuickActions(layout);
    buildRecentFiles(layout);
    buildFeatureCards(layout);
    buildShortcutsSection(layout);
    buildFooter(layout);
    layout->addStretch();

    scroll->setWidget(content);
    outer->addWidget(scroll);
}

void WelcomeWidget::buildHeroSection(QVBoxLayout *parent) {
    const auto p = welcomePalette();

    auto *hero = new QWidget;
    auto *heroLayout = new QVBoxLayout(hero);
    heroLayout->setContentsMargins(0, 20, 0, 0);
    heroLayout->setSpacing(8);

    auto *title = new QLabel("Welcome to Notepatra");
    QFont titleFont = notepatraUiFont();
    titleFont.setPointSize(32);
    titleFont.setWeight(QFont::DemiBold);
    title->setFont(titleFont);
    title->setStyleSheet(QString("color: %1;").arg(p.textPrimary));

    auto *tagline = new QLabel(
        "A native C++/Rust code editor with local AI. "
        "Built for the AI era — free forever, no telemetry, no cloud.");
    QFont taglineFont = notepatraUiFont();
    taglineFont.setPointSize(14);
    tagline->setFont(taglineFont);
    tagline->setStyleSheet(QString("color: %1;").arg(p.textSecondary));
    tagline->setWordWrap(true);

    auto *version = new QLabel(QString("v%1  ·  Linux · macOS · Windows  ·  GPL-3.0")
                                   .arg(NOTEPATRA_VERSION));
    QFont versionFont = notepatraUiFont();
    versionFont.setPointSize(11);
    version->setFont(versionFont);
    version->setStyleSheet(QString("color: %1;").arg(p.textMuted));

    heroLayout->addWidget(title);
    heroLayout->addWidget(tagline);
    heroLayout->addSpacing(4);
    heroLayout->addWidget(version);
    parent->addWidget(hero);
}

void WelcomeWidget::buildQuickActions(QVBoxLayout *parent) {
    auto *row = new QHBoxLayout;
    row->setSpacing(10);

    auto *newBtn = new QPushButton("  New file");
    newBtn->setStyleSheet(primaryActionStyle());
    newBtn->setCursor(Qt::PointingHandCursor);
    connect(newBtn, &QPushButton::clicked, this, &WelcomeWidget::actionNewFile);

    auto *openBtn = new QPushButton("  Open file…");
    openBtn->setStyleSheet(secondaryActionStyle());
    openBtn->setCursor(Qt::PointingHandCursor);
    connect(openBtn, &QPushButton::clicked, this, &WelcomeWidget::actionOpenFile);

    auto *folderBtn = new QPushButton("  Open folder…");
    folderBtn->setStyleSheet(secondaryActionStyle());
    folderBtn->setCursor(Qt::PointingHandCursor);
    connect(folderBtn, &QPushButton::clicked, this, &WelcomeWidget::actionOpenFolder);

    row->addWidget(newBtn);
    row->addWidget(openBtn);
    row->addWidget(folderBtn);
    row->addStretch();

    auto *container = new QWidget;
    container->setLayout(row);
    parent->addWidget(container);
}

void WelcomeWidget::buildRecentFiles(QVBoxLayout *parent) {
    const auto &cfg = Config::instance();
    if (cfg.recentFiles.isEmpty()) return;

    const auto p = welcomePalette();

    auto *header = new QLabel("Recent files");
    QFont hf = notepatraUiFont();
    hf.setPointSize(11);
    hf.setWeight(QFont::DemiBold);
    hf.setLetterSpacing(QFont::AbsoluteSpacing, 0.5);
    header->setFont(hf);
    header->setStyleSheet(QString("color: %1; text-transform: uppercase;").arg(p.textMuted));

    auto *list = new QWidget;
    auto *listLayout = new QVBoxLayout(list);
    listLayout->setContentsMargins(0, 0, 0, 0);
    listLayout->setSpacing(4);

    int shown = 0;
    for (const QString &path : cfg.recentFiles) {
        if (shown >= 8) break;
        QFileInfo fi(path);
        if (!fi.exists()) continue;

        auto *btn = new QPushButton(QString("  %1  ·  %2").arg(fi.fileName(), fi.absolutePath()));
        btn->setStyleSheet(QString(
            "QPushButton { background: transparent; color: %1; border: none; "
            "padding: 6px 8px; text-align: left; font-size: 13px; border-radius: 4px; }"
            "QPushButton:hover { background: %2; color: %3; }"
        ).arg(p.textSecondary, p.cardHover, p.accent));
        btn->setCursor(Qt::PointingHandCursor);
        connect(btn, &QPushButton::clicked, this,
                [this, path]() { emit actionOpenRecent(path); });
        listLayout->addWidget(btn);
        ++shown;
    }

    if (shown == 0) return;

    parent->addWidget(header);
    parent->addWidget(list);
}

QWidget *WelcomeWidget::makeFeatureCard(const QString &icon, const QString &title,
                                        const QString &description, const QString &actionId,
                                        const QString &accentColor) {
    const auto p = welcomePalette();

    auto *card = new ClickableCard;
    // Allow the card to grow as needed for description word-wrap; the grid
    // column width drives the minimum. Larger min-height gives room for
    // icon + title + 2-3 lines of wrapped description without clipping.
    // Each card also gets a soft shadow on hover and a stronger rest-state
    // border so they read as discrete surfaces on the Welcome canvas
    // rather than a single blurred block.
    card->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::MinimumExpanding);
    card->setMinimumHeight(180);
    // A 3-px accent bar on the left of every card, tinted with the
    // feature's accent colour, gives each tile its own identity — similar
    // to the way Xcode's sidebar + Notion's boards separate blocks.
    card->setStyleSheet(QString(
        "ClickableCard { "
        "  background: %1; "
        "  border: 1px solid %2; "
        "  border-left: 3px solid %3; "
        "  border-radius: 12px; "
        "} "
        "ClickableCard:hover { "
        "  border-color: %3; "
        "  border-left: 3px solid %3; "
        "  background: %4; "
        "}"
    ).arg(p.cardBg, p.cardBorder, accentColor, p.cardHover));

    auto *layout = new QVBoxLayout(card);
    layout->setContentsMargins(18, 16, 18, 16);
    layout->setSpacing(8);

    auto *iconLabel = new QLabel(icon);
    QFont iconFont = notepatraUiFont();
    iconFont.setPointSize(22);
    iconLabel->setFont(iconFont);
    iconLabel->setStyleSheet(QString("background: transparent; color: %1;").arg(accentColor));

    auto *titleLabel = new QLabel(title);
    QFont tFont = notepatraUiFont();
    tFont.setPointSize(14);
    tFont.setWeight(QFont::DemiBold);
    titleLabel->setFont(tFont);
    titleLabel->setStyleSheet(QString("background: transparent; color: %1;").arg(p.textPrimary));
    titleLabel->setWordWrap(true);

    auto *descLabel = new QLabel(description);
    QFont dFont = notepatraUiFont();
    dFont.setPointSize(11);
    descLabel->setFont(dFont);
    descLabel->setStyleSheet(QString("background: transparent; color: %1; line-height: 1.4;")
                                 .arg(p.textSecondary));
    descLabel->setWordWrap(true);
    descLabel->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::MinimumExpanding);

    layout->addWidget(iconLabel);
    layout->addWidget(titleLabel);
    layout->addWidget(descLabel, 1);   // description takes remaining space

    card->setOnClick([this, actionId]() { emit actionOpenMenu(actionId); });
    return card;
}

void WelcomeWidget::buildFeatureCards(QVBoxLayout *parent) {
    const auto p = welcomePalette();

    auto *header = new QLabel("Everything built-in — no plugins to install");
    QFont hf = notepatraUiFont();
    hf.setPointSize(11);
    hf.setWeight(QFont::DemiBold);
    hf.setLetterSpacing(QFont::AbsoluteSpacing, 0.5);
    header->setFont(hf);
    header->setStyleSheet(QString("color: %1; text-transform: uppercase;").arg(p.textMuted));
    parent->addWidget(header);

    auto *grid = new QGridLayout;
    grid->setSpacing(18);   // more breathing room so each card is an island
    grid->setContentsMargins(0, 0, 0, 0);

    struct Feat {
        QString icon, title, desc, actionId, accent;
    };
    const QList<Feat> features = {
        {"🤖", "AI Assistant",
         "Local AI via Ollama. Explain · Find Bugs · Refactor · Write Tests. Zero cloud, zero telemetry, zero API key.",
         "AIAssistant", "#0E639C"},
        {"⌨", "Terminal",
         "Built-in shell tab. Run git, npm, make, cargo — without leaving the editor.",
         "Terminal", "#2D7D46"},
        {"🔀", "Compare (ComparePlus)",
         "Word-level diff with red/green intra-line highlighting. Pick any two tabs or any file on disk.",
         "Compare", "#C27A13"},
        {"{ }", "JSON Tools",
         "Format · Minify · Fix+Format · AI Fix. Tolerant parser fixes missing commas, unquoted keys, single quotes.",
         "JSONTools", "#1769AA"},
        {"</>", "HTML Tools",
         "Format with configurable indent, minify, auto-close unclosed tags, AI Fix suggestions.",
         "HTMLTools", "#C84F2B"},
        {"SQL", "SQL Formatter",
         "T-SQL · PL/SQL · MySQL · PostgreSQL · SQLite. UPPERCASE/lowercase keywords, custom indent.",
         "SQLFormatter", "#6A4FBF"},
        {"{ }", "Bracket Tools",
         "Checks for mismatched brackets / braces / parens. Auto-fix + AI Fix for anything the parser can't repair.",
         "BracketTools", "#8A5A17"},
        {"🌐", "REST Client",
         "Built-in HTTP request tester. Pretty-prints JSON responses. Import curl commands.",
         "RESTClient", "#00838F"},
        {"Git", "Git Integration",
         "Branch panel, push/pull/refresh, open on GitHub. Gutter markers in every editor.",
         "Git", "#B23A48"},
    };

    int col = 0, row = 0;
    for (const Feat &f : features) {
        grid->addWidget(makeFeatureCard(f.icon, f.title, f.desc, f.actionId, f.accent), row, col);
        if (++col >= 3) { col = 0; ++row; }
    }

    auto *gridWrap = new QWidget;
    gridWrap->setLayout(grid);
    parent->addWidget(gridWrap);
}

QWidget *WelcomeWidget::makeShortcutRow(const QString &keys, const QString &action) {
    const auto p = welcomePalette();

    auto *row = new QWidget;
    auto *h = new QHBoxLayout(row);
    h->setContentsMargins(0, 0, 0, 0);
    h->setSpacing(12);

    auto *kb = new QLabel(keys);
    QFont kbFont = notepatraCodeFont();
    kbFont.setPointSize(11);
    kb->setFont(kbFont);
    kb->setStyleSheet(QString(
        "background: %1; color: %2; border-radius: 4px; "
        "padding: 2px 8px; min-width: 140px;"
    ).arg(p.shortcutBg, p.shortcutText));
    kb->setFixedWidth(160);

    auto *act = new QLabel(action);
    QFont actFont = notepatraUiFont();
    actFont.setPointSize(12);
    act->setFont(actFont);
    act->setStyleSheet(QString("color: %1;").arg(p.textSecondary));

    h->addWidget(kb);
    h->addWidget(act);
    h->addStretch();
    return row;
}

void WelcomeWidget::buildShortcutsSection(QVBoxLayout *parent) {
    const auto p = welcomePalette();

    auto *header = new QLabel("Essential keyboard shortcuts");
    QFont hf = notepatraUiFont();
    hf.setPointSize(11);
    hf.setWeight(QFont::DemiBold);
    hf.setLetterSpacing(QFont::AbsoluteSpacing, 0.5);
    header->setFont(hf);
    header->setStyleSheet(QString("color: %1; text-transform: uppercase;").arg(p.textMuted));
    parent->addWidget(header);

    auto *grid = new QGridLayout;
    grid->setHorizontalSpacing(40);
    grid->setVerticalSpacing(4);

    struct SC { QString keys, action; };
    const QList<SC> shortcuts = {
        {"Ctrl+N",         "New file"},
        {"Ctrl+O",         "Open file"},
        {"Ctrl+S",         "Save"},
        {"Ctrl+Shift+S",   "Save As…"},
        {"Ctrl+Alt+S",     "Save All"},
        {"Ctrl+W",         "Close tab"},
        {"Ctrl+F",         "Find"},
        {"Ctrl+H",         "Replace"},
        {"Ctrl+G",         "Go to line"},
        {"Ctrl+D",         "Duplicate line"},
        {"Ctrl+/",         "Toggle comment"},
        {"Ctrl+B",         "Jump to matching brace"},
        {"F3 / Shift+F3",  "Find next / previous"},
        {"Ctrl+Shift+A",   "AI Assistant"},
        {"Ctrl+`",         "Terminal"},
        {"Alt+0",          "Fold all"},
    };

    int col = 0, rowIdx = 0;
    for (const SC &sc : shortcuts) {
        grid->addWidget(makeShortcutRow(sc.keys, sc.action), rowIdx, col);
        if (++col >= 2) { col = 0; ++rowIdx; }
    }

    auto *wrap = new QWidget;
    wrap->setLayout(grid);
    parent->addWidget(wrap);
}

void WelcomeWidget::buildFooter(QVBoxLayout *parent) {
    const auto p = welcomePalette();

    auto *separator = new QFrame;
    separator->setFrameShape(QFrame::HLine);
    separator->setStyleSheet(QString("color: %1; background: %1; max-height: 1px;")
                                 .arg(p.cardBorder));
    parent->addWidget(separator);

    auto *row = new QHBoxLayout;
    row->setContentsMargins(0, 0, 0, 0);
    row->setSpacing(16);

    auto *hint = new QLabel(
        "💡 Tip: run <code>ollama pull qwen2.5-coder:3b</code> to enable the AI Assistant. "
        "Everything stays on your machine.");
    QFont hintFont = notepatraUiFont();
    hintFont.setPointSize(11);
    hint->setFont(hintFont);
    hint->setStyleSheet(QString("color: %1;").arg(p.textMuted));
    hint->setWordWrap(true);

    auto *dontShow = new QCheckBox("Don't show this tab on startup");
    dontShow->setStyleSheet(QString("color: %1;").arg(p.textSecondary));
    connect(dontShow, &QCheckBox::toggled, this, [this](bool checked) {
        if (checked) emit actionDismissForever();
    });

    row->addWidget(hint, 1);
    row->addWidget(dontShow);

    auto *wrap = new QWidget;
    wrap->setLayout(row);
    parent->addWidget(wrap);
}
