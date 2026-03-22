#include "markdownpreview.h"
#include <QVBoxLayout>
#include <QLabel>
#include <QRegularExpression>

// Forward declaration
static QString processInline(const QString &text);

MarkdownPreview::MarkdownPreview(QWidget *parent) : QWidget(parent) {
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);

    auto *header = new QLabel("  Markdown Preview");
    header->setFixedHeight(22);
    header->setStyleSheet("font-weight: bold; background: #F0F0F0; padding: 2px 6px; border-bottom: 1px solid #CCC;");
    layout->addWidget(header);

    m_browser = new QTextBrowser;
    m_browser->setOpenExternalLinks(true);
    m_browser->setStyleSheet("QTextBrowser { background: white; padding: 12px; border: none; }");
    layout->addWidget(m_browser);
}

void MarkdownPreview::updatePreview(const QString &markdown) {
    m_browser->setHtml(markdownToHtml(markdown));
}

QString MarkdownPreview::markdownToHtml(const QString &md) {
    QString html = "<!DOCTYPE html><html><head><style>"
        "body { font-family: -apple-system, Arial, sans-serif; font-size: 14px; line-height: 1.6; color: #333; max-width: 800px; padding: 10px; }"
        "h1 { font-size: 28px; border-bottom: 2px solid #eee; padding-bottom: 8px; color: #111; }"
        "h2 { font-size: 22px; border-bottom: 1px solid #eee; padding-bottom: 6px; color: #222; }"
        "h3 { font-size: 18px; color: #333; }"
        "h4 { font-size: 16px; color: #444; }"
        "code { background: #F5F5F5; padding: 2px 6px; border-radius: 3px; font-family: Consolas, monospace; font-size: 13px; }"
        "pre { background: #1E1E1E; color: #D4D4D4; padding: 12px; border-radius: 6px; overflow-x: auto; }"
        "pre code { background: none; padding: 0; color: #D4D4D4; }"
        "blockquote { border-left: 4px solid #4A90D9; margin: 8px 0; padding: 8px 16px; background: #F0F7FF; }"
        "table { border-collapse: collapse; width: 100%; }"
        "th, td { border: 1px solid #DDD; padding: 8px; text-align: left; }"
        "th { background: #F5F5F5; font-weight: bold; }"
        "hr { border: none; border-top: 2px solid #eee; margin: 16px 0; }"
        "a { color: #4A90D9; }"
        "img { max-width: 100%; }"
        "ul, ol { padding-left: 24px; }"
        "li { margin: 4px 0; }"
        ".task-done { color: #2E7D32; }"
        ".task-todo { color: #999; }"
        "</style></head><body>";

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
