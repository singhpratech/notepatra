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
    //
    // Pre-v0.1.55 this was the only key field, shared across providers.
    // That meant switching from OpenRouter to OpenAI silently broke
    // because the OpenRouter key would be sent to OpenAI (and rejected).
    // v0.1.55 introduces per-provider key slots below; aiApiKey is kept
    // as a legacy fallback (read on load, never written) so existing
    // configs continue to work for whichever cloud the user last used.
    QString aiApiKey;
    // v0.1.55 — per-provider key storage. The AI Settings dialog (gear
    // icon → Settings) writes to these directly. OllamaClient reads the
    // right slot for whichever backend is active via aiKeyForBackend().
    QString aiOpenRouterKey;
    QString aiOpenAIKey;
    QString aiOllamaCloudKey;

    // v0.1.55 — Azure OpenAI: deployment-scoped, resource-scoped endpoint.
    // Unlike OpenAI direct, Azure routes through `<resource>.openai.azure.com`
    // with a per-deployment URL path and uses an `api-key` header instead
    // of `Authorization: Bearer`. Each user has different Azure setups
    // (different resources, different deployments per resource), so we
    // store all four fields and reconstruct the URL at request time.
    QString aiAzureResource;        // e.g. "my-org-gpt"
    QString aiAzureDeployment;      // e.g. "gpt-4o-prod"
    QString aiAzureApiVersion = "2024-10-21";  // current stable as of late 2025
    QString aiAzureKey;

    // Pick the appropriate saved key for a given backend identifier
    // ("OpenRouter", "OpenAI", "Ollama", "llama.cpp"). Returns empty for
    // backends that don't take a key (Ollama / llama.cpp). Falls back to
    // the legacy aiApiKey if the per-provider slot is empty AND the
    // legacy field is set, which makes upgrades transparent.
    QString aiKeyForBackend(const QString &backend) const {
        // STRICT per-provider lookup. Pre-fix this fell back across
        // providers ("if OpenAI slot is empty, use the legacy aiApiKey")
        // which let an OpenRouter key (sk-or-…) parade as an OpenAI key —
        // the AI panel banner went green, then the actual /v1/models call
        // to api.openai.com bounced with HTTP 401. The fix: cross-provider
        // fallback ONLY when the legacy key's prefix actually matches
        // that provider's documented format.
        auto looksLikeOpenRouter = [](const QString &k) {
            return k.startsWith("sk-or-");
        };
        auto looksLikeOpenAI = [](const QString &k) {
            // OpenAI keys: sk-… or sk-proj-…, but NOT sk-or-… (OpenRouter).
            return k.startsWith("sk-") && !k.startsWith("sk-or-");
        };
        if (backend == "OpenRouter") {
            if (!aiOpenRouterKey.isEmpty()) return aiOpenRouterKey;
            if (looksLikeOpenRouter(aiApiKey)) return aiApiKey;
            return QString();
        }
        if (backend == "OpenAI") {
            if (!aiOpenAIKey.isEmpty()) return aiOpenAIKey;
            if (looksLikeOpenAI(aiApiKey)) return aiApiKey;
            return QString();
        }
        if (backend == "Ollama Cloud") {
            // Ollama Cloud keys are opaque (no documented prefix), so no
            // prefix-based legacy fallback. Either the per-provider slot
            // is set, or we've got nothing.
            return aiOllamaCloudKey;
        }
        if (backend == "Azure OpenAI") {
            return aiAzureKey;
        }
        // Self-hosted / OpenAI-compat servers (llama.cpp, vLLM, LM Studio,
        // Jan, etc.) may still require a Bearer token. Pre-v0.1.55 they
        // read aiApiKey directly; preserve that behaviour so existing
        // configs keep working without forcing users back into Settings.
        return aiApiKey;
    }

    // v0.1.43 — Data Analyst Mode toggle persisted across launches.
    // When true on startup, the AI panel restores the Data toggle on.
    // Mutually exclusive with future aiCodingMode toggle persistence.
    bool aiDataMode = false;

    // v0.1.53 — when true, the Data Analyst welcome card (model capability +
    // connection status + example prompt chips) is suppressed. Default
    // false: card shows on first Data-mode entry of every fresh chat,
    // until the user clicks "Hide".
    bool aiHideDataWelcome = false;

    // v0.1.55 — privacy / safety toggle. When false (default), Notepatra
    // does NOT auto-attach the currently-open editor file to AI prompts:
    //   * workspace-context block is not built (current file + open tabs)
    //   * the "Currently open: foo.py" awareness line is not pinned into
    //     the system prompt
    //   * empty-selection quick actions (Explain, Find Bugs, …) refuse
    //     to fall back to the whole file
    //   * Coding-Mode tools still work (the user explicitly opted in by
    //     entering Coding Mode), but the implicit file pin is gone
    // The toggle is rendered prominently in the AI panel; flipping it on
    // grants the AI visibility, off revokes it. Default-off makes the
    // privacy posture explicit — the user opts INTO sharing, not out.
    bool aiShareOpenFile = false;

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
        aiOpenRouterKey  = o.value("aiOpenRouterKey").toString(aiOpenRouterKey);
        aiOpenAIKey      = o.value("aiOpenAIKey").toString(aiOpenAIKey);
        aiOllamaCloudKey = o.value("aiOllamaCloudKey").toString(aiOllamaCloudKey);
        aiAzureResource   = o.value("aiAzureResource").toString(aiAzureResource);
        aiAzureDeployment = o.value("aiAzureDeployment").toString(aiAzureDeployment);
        aiAzureApiVersion = o.value("aiAzureApiVersion").toString(aiAzureApiVersion);
        aiAzureKey        = o.value("aiAzureKey").toString(aiAzureKey);
        // v0.1.55 — one-time migration of the legacy single-slot aiApiKey
        // into the right per-provider slot. Skip if either per-provider
        // slot is already set (the user has answered the question of which
        // provider this key belongs to). When migrating, clear aiApiKey
        // so the legacy field can never silently impersonate a provider
        // again.
        if (aiOpenRouterKey.isEmpty() && aiOpenAIKey.isEmpty() && !aiApiKey.isEmpty()) {
            if (aiApiKey.startsWith("sk-or-")) {
                aiOpenRouterKey = aiApiKey;
                aiApiKey.clear();
            } else if (aiApiKey.startsWith("sk-")) {
                aiOpenAIKey = aiApiKey;
                aiApiKey.clear();
            }
            // Anything else (custom self-hosted token) stays in aiApiKey
            // since it's not a recognized cloud key shape — preserves the
            // self-hosted llama.cpp / vLLM auth path.
        }
        aiDataMode = o.value("aiDataMode").toBool(aiDataMode);
        aiHideDataWelcome = o.value("aiHideDataWelcome").toBool(aiHideDataWelcome);
        aiShareOpenFile = o.value("aiShareOpenFile").toBool(aiShareOpenFile);
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
        o["aiOpenRouterKey"]  = aiOpenRouterKey;
        o["aiOpenAIKey"]      = aiOpenAIKey;
        o["aiOllamaCloudKey"] = aiOllamaCloudKey;
        o["aiAzureResource"]   = aiAzureResource;
        o["aiAzureDeployment"] = aiAzureDeployment;
        o["aiAzureApiVersion"] = aiAzureApiVersion;
        o["aiAzureKey"]        = aiAzureKey;
        o["aiDataMode"] = aiDataMode;
        o["aiHideDataWelcome"] = aiHideDataWelcome;
        o["aiShareOpenFile"] = aiShareOpenFile;
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
