#include "markdownpreview.h"
#include "config.h"
#include <QVBoxLayout>
#include <QLabel>
#include <QRegularExpression>

// Forward declaration
static QString processInline(const QString &text);

namespace {
static bool mdIsDark() {
    const QString &t = Config::instance().theme;
    return t.compare("Dark", Qt::CaseInsensitive) == 0 ||
           t.compare("Monokai", Qt::CaseInsensitive) == 0;
}
}

MarkdownPreview::MarkdownPreview(QWidget *parent) : QWidget(parent) {
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);

    m_header = new QLabel("  Markdown Preview");
    m_header->setMinimumHeight(28);
    m_header->setStyleSheet("QLabel { font-weight: 600; padding: 4px 6px; }");
    layout->addWidget(m_header);

    m_browser = new QTextBrowser;
    m_browser->setOpenExternalLinks(true);
    layout->addWidget(m_browser);

    applyPalette();
}

void MarkdownPreview::applyPalette() {
    const bool dark = mdIsDark();
    if (m_header) {
        m_header->setStyleSheet(QString(
            "font-weight: bold; background: %1; color: %2; "
            "padding: 2px 6px; border-bottom: 1px solid %3;")
            .arg(dark ? "#252526" : "#F0F0F0",
                 dark ? "#D4D4D4" : "#141413",
                 dark ? "#1E1E1E" : "#CCC"));
    }
    if (m_browser) {
        m_browser->setStyleSheet(QString(
            "QTextBrowser { background: %1; color: %2; padding: 12px; border: none; }")
            .arg(dark ? "#1E1E1E" : "white",
                 dark ? "#D4D4D4" : "#141413"));
    }
}

void MarkdownPreview::onThemeChanged() {
    applyPalette();
    if (m_browser && !m_lastMarkdown.isEmpty())
        m_browser->setHtml(markdownToHtml(m_lastMarkdown));
    update();
}

void MarkdownPreview::updatePreview(const QString &markdown) {
    m_lastMarkdown = markdown;
    m_browser->setHtml(markdownToHtml(markdown));
}

QString MarkdownPreview::markdownToHtml(const QString &md) {
    const bool dark = mdIsDark();
    const QString fg       = dark ? "#D4D4D4" : "#333";
    const QString hFg1     = dark ? "#E8E6E3" : "#111";
    const QString hFg2     = dark ? "#D4D4D4" : "#222";
    const QString hFg3     = dark ? "#D4D4D4" : "#333";
    const QString hFg4     = dark ? "#B8B5B1" : "#444";
    const QString rule     = dark ? "#2D2D2D" : "#eee";
    const QString codeBg   = dark ? "#252526" : "#F5F5F5";
    const QString codeFg   = dark ? "#F7D774" : "#9A6A20";
    const QString preBg    = dark ? "#111315" : "#F5F4EE";
    const QString preFg    = dark ? "#D4D4D4" : "#141413";
    const QString quoteBg  = dark ? "#252526" : "#F0F7FF";
    const QString quoteEdge= dark ? "#4EC9B0" : "#4A90D9";
    const QString tableBr  = dark ? "#3E3E3E" : "#DDD";
    const QString thBg     = dark ? "#252526" : "#F5F5F5";
    const QString linkFg   = dark ? "#7EC8FF" : "#4A90D9";
    const QString taskDone = dark ? "#76D275" : "#2E7D32";
    const QString taskTodo = dark ? "#6C6C6C" : "#999";

    QString html = QString("<!DOCTYPE html><html><head><style>"
        "body { font-family: -apple-system, Arial, sans-serif; font-size: 14px; line-height: 1.6; color: %1; max-width: 800px; padding: 10px; }"
        "h1 { font-size: 28px; border-bottom: 2px solid %7; padding-bottom: 8px; color: %2; }"
        "h2 { font-size: 22px; border-bottom: 1px solid %7; padding-bottom: 6px; color: %3; }"
        "h3 { font-size: 18px; color: %4; }"
        "h4 { font-size: 16px; color: %5; }"
        "code { background: %8; color: %9; padding: 2px 6px; border-radius: 3px; font-family: Consolas, monospace; font-size: 13px; }"
        "pre { background: %10; color: %11; padding: 12px; border-radius: 6px; overflow-x: auto; }"
        "pre code { background: none; padding: 0; color: %11; }"
        "blockquote { border-left: 4px solid %13; margin: 8px 0; padding: 8px 16px; background: %12; }"
        "table { border-collapse: collapse; width: 100%%; }"
        "th, td { border: 1px solid %14; padding: 8px; text-align: left; }"
        "th { background: %15; font-weight: bold; }"
        "hr { border: none; border-top: 2px solid %7; margin: 16px 0; }"
        "a { color: %16; }"
        "img { max-width: 100%%; }"
        "ul, ol { padding-left: 24px; }"
        "li { margin: 4px 0; }"
        ".task-done { color: %17; }"
        ".task-todo { color: %18; }"
        "</style></head><body>")
        .arg(fg, hFg1, hFg2, hFg3, hFg4, QString()/*unused*/, rule,
             codeBg, codeFg, preBg, preFg, quoteBg, quoteEdge, tableBr, thBg,
             linkFg, taskDone, taskTodo);

    QStringList lines = md.split('\n');
    bool inCodeBlock = false;
    bool inList = false;
    QString codeBlockLang;

    for (int i = 0; i < lines.size(); i++) {
        QString line = lines[i];

        // Code blocks
        if (line.trimmed().startsWith("```")) {
            if (!inCodeBlock) {
                inCodeBlock = true;
                codeBlockLang = line.trimmed().mid(3).trimmed();
                html += "<pre><code>";
            } else {
                inCodeBlock = false;
                html += "</code></pre>";
            }
            continue;
        }
        if (inCodeBlock) {
            html += line.toHtmlEscaped() + "\n";
            continue;
        }

        // Close list if needed
        if (inList && !line.trimmed().startsWith("- ") && !line.trimmed().startsWith("* ") &&
            !QRegularExpression("^\\d+\\.\\s").match(line.trimmed()).hasMatch() && !line.trimmed().isEmpty()) {
            html += "</ul>";
            inList = false;
        }

        // Horizontal rule
        if (QRegularExpression("^(---|\\*\\*\\*|___)\\s*$").match(line.trimmed()).hasMatch()) {
            html += "<hr>";
            continue;
        }

        // Headers
        if (line.startsWith("#### ")) { html += "<h4>" + processInline(line.mid(5)) + "</h4>"; continue; }
        if (line.startsWith("### ")) { html += "<h3>" + processInline(line.mid(4)) + "</h3>"; continue; }
        if (line.startsWith("## ")) { html += "<h2>" + processInline(line.mid(3)) + "</h2>"; continue; }
        if (line.startsWith("# ")) { html += "<h1>" + processInline(line.mid(2)) + "</h1>"; continue; }

        // Blockquote
        if (line.startsWith("> ")) {
            html += "<blockquote>" + processInline(line.mid(2)) + "</blockquote>";
            continue;
        }

        // Task lists
        if (line.trimmed().startsWith("- [x] ")) {
            if (!inList) { html += "<ul>"; inList = true; }
            html += "<li class='task-done'>&#9745; " + processInline(line.trimmed().mid(6)) + "</li>";
            continue;
        }
        if (line.trimmed().startsWith("- [ ] ")) {
            if (!inList) { html += "<ul>"; inList = true; }
            html += "<li class='task-todo'>&#9744; " + processInline(line.trimmed().mid(6)) + "</li>";
            continue;
        }

        // Unordered list
        if (line.trimmed().startsWith("- ") || line.trimmed().startsWith("* ")) {
            if (!inList) { html += "<ul>"; inList = true; }
            html += "<li>" + processInline(line.trimmed().mid(2)) + "</li>";
            continue;
        }

        // Ordered list
        auto olMatch = QRegularExpression("^(\\d+)\\.\\s(.*)").match(line.trimmed());
        if (olMatch.hasMatch()) {
            if (!inList) { html += "<ol>"; inList = true; }
            html += "<li>" + processInline(olMatch.captured(2)) + "</li>";
            continue;
        }

        // Empty line
        if (line.trimmed().isEmpty()) {
            if (inList) { html += "</ul>"; inList = false; }
            html += "<br>";
            continue;
        }

        // Normal paragraph
        html += "<p>" + processInline(line) + "</p>";
    }

    if (inCodeBlock) html += "</code></pre>";
    if (inList) html += "</ul>";
    html += "</body></html>";
    return html;
}

// Process inline markdown: bold, italic, code, links, images
static QString processInline(const QString &text) {
    QString result = text.toHtmlEscaped();

    // Images: ![alt](url)
    result.replace(QRegularExpression("!\\[([^\\]]*)\\]\\(([^\\)]+)\\)"), "<img src='\\2' alt='\\1'>");
    // Links: [text](url)
    result.replace(QRegularExpression("\\[([^\\]]*)\\]\\(([^\\)]+)\\)"), "<a href='\\2'>\\1</a>");
    // Bold+Italic: ***text***
    result.replace(QRegularExpression("\\*\\*\\*(.+?)\\*\\*\\*"), "<strong><em>\\1</em></strong>");
    // Bold: **text**
    result.replace(QRegularExpression("\\*\\*(.+?)\\*\\*"), "<strong>\\1</strong>");
    // Italic: *text*
    result.replace(QRegularExpression("\\*(.+?)\\*"), "<em>\\1</em>");
    // Strikethrough: ~~text~~
    result.replace(QRegularExpression("~~(.+?)~~"), "<del>\\1</del>");
    // Inline code: `text`
    result.replace(QRegularExpression("`([^`]+)`"), "<code>\\1</code>");

    return result;
}
