#ifndef CONFIG_H
#define CONFIG_H

#include <QString>
#include <QJsonObject>
#include <QJsonDocument>
#include <QJsonArray>
#include <QFile>
#include <QDir>

/**
 * Persistent config — saves ALL settings to ~/.config/notepatra/config.json
 */
class Config {
public:
    static Config &instance() { static Config c; return c; }

    // Editor
    // "System" = follow OS preference via themes.h detectSystemTheme()
    // (macOS defaults / Windows registry / GNOME gsettings). Falls back
    // to Light when the system preference can't be read. Users still
    // see Light/Dark/Monokai as explicit choices in Settings → Theme.
    QString theme = "System";
    int tabWidth = 4;
    bool useTabs = false;
    bool autoIndent = true;
    bool wordWrap = false;
    bool showLineNumbers = true;
    bool showIndentGuides = true;
    int fontSize = 11;
    QString fontFamily;
    int edgeColumn = 120;
    bool showEdge = true;
    bool highlightCurrentLine = true;
    int caretWidth = 2;
    bool showDocumentRulers = false;
    bool showCrosshair = false;

    // Auto-complete
    bool autoComplete = true;
    int autoCompleteThreshold = 3;

    // v0.1.42 — fields added so the Preferences dialog stops being theatre.
    // Every control on every Preferences tab now reads from these on init
    // and writes back on OK. Editor::setupEditor() consults each one
    // instead of hardcoding the default.
    bool hideToolbar = false;            // General: Hide toolbar
    bool tabsClosable = true;            // General: Show close button on each tab
    bool doubleClickToCloseTab = false;  // General: Double-click to close tab
    bool smoothFont = true;              // Editing: Enable smooth font (anti-alias)
    QString foldStyle = "BoxedTree";     // Margins: BoxedTree | CircleTree | Plain | Boxed | Circle | None
    bool showBookmarkMargin = true;      // Margins: Display bookmark margin
    QString defaultEol = "Unix";         // New Document: Unix | Windows | Mac

    // Show the Welcome tab on launch when there are no session files to
    // restore. User can dismiss with "Don't show again" checkbox inside
    // the tab itself, or via View menu.
    bool showWelcomeOnStartup = true;

    // AI backend selection. One of "Ollama" (default), "llama.cpp",
    // "OpenAI-compat" (LM Studio / Jan / vLLM / anything speaking the
    // OpenAI /v1/chat/completions API). See OllamaClient::Backend for
    // the full list.
    QString aiBackend = "Ollama";
    // Base URL for the selected backend. Empty = use the default for
    // the backend (11434 for Ollama, 8080 for llama.cpp). Users paste
    // custom URLs here when self-hosting.
    QString aiBaseUrl;
    // Optional Bearer token — passed as Authorization header for
    // OpenAI-compat backends. Ignored for Ollama. Ignored for
    // llama-server too (it doesn't check auth by default).
    QString aiApiKey;

    // v0.1.43 — Data Analyst Mode toggle persisted across launches.
    // When true on startup, the AI panel restores the Data toggle on.
    // Mutually exclusive with future aiCodingMode toggle persistence.
    bool aiDataMode = false;

    // Session
    QStringList recentFiles;
    int maxRecent = 15;

    // Window
    int windowX = -1, windowY = -1, windowW = 1280, windowH = 800;
    bool maximized = false;

    void load() {
        QString path = configPath();
        QFile f(path);
        if (!f.open(QIODevice::ReadOnly)) return;
        QJsonObject o = QJsonDocument::fromJson(f.readAll()).object();

        theme = o.value("theme").toString(theme);
        tabWidth = o.value("tabWidth").toInt(tabWidth);
        useTabs = o.value("useTabs").toBool(useTabs);
        autoIndent = o.value("autoIndent").toBool(autoIndent);
        wordWrap = o.value("wordWrap").toBool(wordWrap);
        showLineNumbers = o.value("showLineNumbers").toBool(showLineNumbers);
        showIndentGuides = o.value("showIndentGuides").toBool(showIndentGuides);
        fontSize = o.value("fontSize").toInt(fontSize);
        fontFamily = o.value("fontFamily").toString(fontFamily);
        edgeColumn = o.value("edgeColumn").toInt(edgeColumn);
        showEdge = o.value("showEdge").toBool(showEdge);
        highlightCurrentLine = o.value("highlightCurrentLine").toBool(highlightCurrentLine);
        caretWidth = o.value("caretWidth").toInt(caretWidth);
        showDocumentRulers = o.value("showDocumentRulers").toBool(showDocumentRulers);
        showCrosshair = o.value("showCrosshair").toBool(showCrosshair);
        autoComplete = o.value("autoComplete").toBool(autoComplete);
        autoCompleteThreshold = o.value("autoCompleteThreshold").toInt(autoCompleteThreshold);
        hideToolbar = o.value("hideToolbar").toBool(hideToolbar);
        tabsClosable = o.value("tabsClosable").toBool(tabsClosable);
        doubleClickToCloseTab = o.value("doubleClickToCloseTab").toBool(doubleClickToCloseTab);
        smoothFont = o.value("smoothFont").toBool(smoothFont);
        foldStyle = o.value("foldStyle").toString(foldStyle);
        showBookmarkMargin = o.value("showBookmarkMargin").toBool(showBookmarkMargin);
        defaultEol = o.value("defaultEol").toString(defaultEol);
        showWelcomeOnStartup = o.value("showWelcomeOnStartup").toBool(showWelcomeOnStartup);
        aiBackend = o.value("aiBackend").toString(aiBackend);
        aiBaseUrl = o.value("aiBaseUrl").toString(aiBaseUrl);
        aiApiKey  = o.value("aiApiKey").toString(aiApiKey);
        aiDataMode = o.value("aiDataMode").toBool(aiDataMode);
        windowX = o.value("windowX").toInt(windowX);
        windowY = o.value("windowY").toInt(windowY);
        windowW = o.value("windowW").toInt(windowW);
        windowH = o.value("windowH").toInt(windowH);
        maximized = o.value("maximized").toBool(maximized);

        recentFiles.clear();
        for (const auto &v : o.value("recentFiles").toArray())
            recentFiles.append(v.toString());
    }

    void save() {
        QDir().mkpath(QFileInfo(configPath()).path());
        QJsonObject o;
        o["theme"] = theme;
        o["tabWidth"] = tabWidth;
        o["useTabs"] = useTabs;
        o["autoIndent"] = autoIndent;
        o["wordWrap"] = wordWrap;
        o["showLineNumbers"] = showLineNumbers;
        o["showIndentGuides"] = showIndentGuides;
        o["fontSize"] = fontSize;
        o["fontFamily"] = fontFamily;
        o["edgeColumn"] = edgeColumn;
        o["showEdge"] = showEdge;
        o["highlightCurrentLine"] = highlightCurrentLine;
        o["caretWidth"] = caretWidth;
        o["showDocumentRulers"] = showDocumentRulers;
        o["showCrosshair"] = showCrosshair;
        o["autoComplete"] = autoComplete;
        o["autoCompleteThreshold"] = autoCompleteThreshold;
        o["hideToolbar"] = hideToolbar;
        o["tabsClosable"] = tabsClosable;
        o["doubleClickToCloseTab"] = doubleClickToCloseTab;
        o["smoothFont"] = smoothFont;
        o["foldStyle"] = foldStyle;
        o["showBookmarkMargin"] = showBookmarkMargin;
        o["defaultEol"] = defaultEol;
        o["showWelcomeOnStartup"] = showWelcomeOnStartup;
        o["aiBackend"] = aiBackend;
        o["aiBaseUrl"] = aiBaseUrl;
        o["aiApiKey"]  = aiApiKey;
        o["aiDataMode"] = aiDataMode;
        o["windowX"] = windowX;
        o["windowY"] = windowY;
        o["windowW"] = windowW;
        o["windowH"] = windowH;
        o["maximized"] = maximized;

        QJsonArray arr;
        for (const auto &f : recentFiles) arr.append(f);
        o["recentFiles"] = arr;

        QFile f(configPath());
        if (f.open(QIODevice::WriteOnly))
            f.write(QJsonDocument(o).toJson());
    }

    void addRecent(const QString &path) {
        recentFiles.removeAll(path);
        recentFiles.prepend(path);
        while (recentFiles.size() > maxRecent) recentFiles.removeLast();
    }

    static QString configPath() {
        return QDir::homePath() + "/.config/notepatra/config.json";
    }

private:
    Config() { load(); }
};

#endif
