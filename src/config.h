// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef CONFIG_H
#define CONFIG_H

#include <QString>
#include <QJsonObject>
#include <QJsonDocument>
#include <QJsonArray>
#include <QFile>
#include <QFileInfo>
#include <QDir>
#include <QDirIterator>

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

    // View → Show Symbol. Notepad++ persists every one of these across
    // restarts; Notepatra used to forget them the moment you closed it.
    // Defaults match Notepad++'s: everything off except the control-character
    // display, which is on because an unannounced U+0007 in a file is a
    // problem you want to see without having to go looking for it.
    bool showWhitespace = false;
    bool showEol = false;
    bool showNonPrintingChars = false;
    bool showControlChars = true;
    bool showWrapSymbol = false;

    // Auto-complete
    bool autoComplete = true;
    int autoCompleteThreshold = 3;

    // Auto-save (global) — session.json + window geometry tick. Applies to
    // every tab type (code files, Notes, etc.). Bounded [1s, 300s].
    int autoSaveIntervalSec = 5;

    // Memory budget (MB) for opening a single file fully into the editor.
    // Default 2048 (2 GB). This is a SOFT ceiling: a larger file is still
    // openable — it just triggers a one-time confirm dialog warning that the
    // load needs that much free RAM. Raise it only if your machine has the
    // memory. Bounded [128 MB, 65536 MB (64 GB)]. Stored in MB; the
    // Preferences control edits it in whole GB.
    int fileMemoryLimitMb = 2048;

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

    // Noter — user's picked AI model for end-meeting sweep. Empty =
    // use whatever the OllamaClient defaults to.
    QString aiNoterModel;
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

    // v0.1.57 — when true, the Coding mode welcome card (Composer / @file /
    // Ctrl+I / agentic git tools introduction) is suppressed. Default false:
    // card shows on first Coding-mode entry of every fresh chat, until the
    // user clicks "Hide".
    bool aiHideCodingWelcome = false;

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

    // v0.1.115 — persisted Chat/Compose/Agent segment (the bottom strip that
    // only applies in Coding mode). 0 = Ask (read-only), 1 = Compose (Edit
    // Plan review), 2 = Agent (approved writes). Default 1 (Compose) keeps the
    // v0.1.110 safe-by-default decision: a re-entrant Coding session never
    // lands in autonomous-write Agent unless the user explicitly chose it.
    int aiChatSegment = 1;

    // v0.1.70 — AI dock visibility persisted across launches. When true
    // (default for new installs), Notepatra opens with the AI dock on
    // screen at 50% width. Toggling the dock via Ctrl+Shift+A / the AI
    // toolbar button / the Tools menu writes this field synchronously
    // so the layout survives a quit/relaunch. AI features (Ctrl+I,
    // Composer, agentic ops) remain enabled in BOTH states — invoking
    // any of them while the dock is hidden auto-opens the dock via
    // MainWindow::showAiDockForInvocation(). This is purely a layout
    // flag, NOT a feature gate: Notepatra is an AI editor; AI is not
    // optional.
    bool aiDockVisible = true;

    // v0.1.71 — AI interaction logging. When true (default), every
    // request/response sent to a cloud or local LLM is recorded into
    // ~/.config/notepatra/ai-logs/interactions.db (SQLite, WAL mode).
    // Retained for 7 days then auto-pruned. User can opt out from
    // Settings → Privacy; opt-out makes the recorder a no-op (the db
    // file isn't even opened). See src/ai_interaction_log.h.
    bool aiInteractionLogging = true;

    // Session
    QStringList recentFiles;
    int maxRecent = 15;

    // Last directory a Save/Open dialog navigated to. Persisted so that a
    // brand-new untitled buffer (which has no path of its own) and every
    // secondary Save/Open dialog reopen where the user last was, instead
    // of always snapping back to the home folder — the behaviour every
    // other editor has and the one users expect. The main Save As still
    // PREFERS the current file's own directory when it has one; lastDir is
    // only the fallback for when there is no such context. Written on both
    // successful opens and saves so either action seeds the next dialog.
    QString lastDir;

    // v0.1.61 — file-explorer hidden-paths set. Right-click any tree
    // node → "Hide" appends to this list; the file-tree's filter proxy
    // refuses to show anything whose absolute path is in here. Persists
    // across launches so vendor / node_modules stay hidden after restart.
    QStringList explorerHiddenPaths;

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
        showWhitespace = o.value("showWhitespace").toBool(showWhitespace);
        showEol = o.value("showEol").toBool(showEol);
        showNonPrintingChars = o.value("showNonPrintingChars").toBool(showNonPrintingChars);
        showControlChars = o.value("showControlChars").toBool(showControlChars);
        showWrapSymbol = o.value("showWrapSymbol").toBool(showWrapSymbol);
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
        autoSaveIntervalSec = qBound(1, o.value("autoSaveIntervalSec").toInt(autoSaveIntervalSec), 300);
        fileMemoryLimitMb = qBound(128, o.value("fileMemoryLimitMb").toInt(fileMemoryLimitMb), 65536);
        hideToolbar = o.value("hideToolbar").toBool(hideToolbar);
        tabsClosable = o.value("tabsClosable").toBool(tabsClosable);
        doubleClickToCloseTab = o.value("doubleClickToCloseTab").toBool(doubleClickToCloseTab);
        smoothFont = o.value("smoothFont").toBool(smoothFont);
        foldStyle = o.value("foldStyle").toString(foldStyle);
        showBookmarkMargin = o.value("showBookmarkMargin").toBool(showBookmarkMargin);
        defaultEol = o.value("defaultEol").toString(defaultEol);
        showWelcomeOnStartup = o.value("showWelcomeOnStartup").toBool(showWelcomeOnStartup);
        aiBackend = o.value("aiBackend").toString(aiBackend);
        aiNoterModel = o.value("aiNoterModel").toString(aiNoterModel);
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
        aiHideCodingWelcome = o.value("aiHideCodingWelcome").toBool(aiHideCodingWelcome);
        aiShareOpenFile = o.value("aiShareOpenFile").toBool(aiShareOpenFile);
        aiChatSegment = o.value("aiChatSegment").toInt(aiChatSegment);
        aiDockVisible = o.value("aiDockVisible").toBool(aiDockVisible);
        aiInteractionLogging = o.value("aiInteractionLogging").toBool(aiInteractionLogging);
        windowX = o.value("windowX").toInt(windowX);
        windowY = o.value("windowY").toInt(windowY);
        windowW = o.value("windowW").toInt(windowW);
        windowH = o.value("windowH").toInt(windowH);
        maximized = o.value("maximized").toBool(maximized);

        lastDir = o.value("lastDir").toString(lastDir);

        recentFiles.clear();
        for (const auto &v : o.value("recentFiles").toArray())
            recentFiles.append(v.toString());

        explorerHiddenPaths.clear();
        for (const auto &v : o.value("explorerHiddenPaths").toArray())
            explorerHiddenPaths.append(v.toString());
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
        o["showWhitespace"] = showWhitespace;
        o["showEol"] = showEol;
        o["showNonPrintingChars"] = showNonPrintingChars;
        o["showControlChars"] = showControlChars;
        o["showWrapSymbol"] = showWrapSymbol;
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
        o["autoSaveIntervalSec"] = autoSaveIntervalSec;
        o["fileMemoryLimitMb"] = fileMemoryLimitMb;
        o["hideToolbar"] = hideToolbar;
        o["tabsClosable"] = tabsClosable;
        o["doubleClickToCloseTab"] = doubleClickToCloseTab;
        o["smoothFont"] = smoothFont;
        o["foldStyle"] = foldStyle;
        o["showBookmarkMargin"] = showBookmarkMargin;
        o["defaultEol"] = defaultEol;
        o["showWelcomeOnStartup"] = showWelcomeOnStartup;
        o["aiBackend"] = aiBackend;
        o["aiNoterModel"] = aiNoterModel;
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
        o["aiHideCodingWelcome"] = aiHideCodingWelcome;
        o["aiShareOpenFile"] = aiShareOpenFile;
        o["aiChatSegment"] = aiChatSegment;
        o["aiDockVisible"] = aiDockVisible;
        o["aiInteractionLogging"] = aiInteractionLogging;
        o["windowX"] = windowX;
        o["windowY"] = windowY;
        o["windowW"] = windowW;
        o["windowH"] = windowH;
        o["maximized"] = maximized;

        o["lastDir"] = lastDir;

        QJsonArray arr;
        for (const auto &f : recentFiles) arr.append(f);
        o["recentFiles"] = arr;

        QJsonArray hidden;
        for (const auto &p : explorerHiddenPaths) hidden.append(p);
        o["explorerHiddenPaths"] = hidden;

        QFile f(configPath());
        if (f.open(QIODevice::WriteOnly))
            f.write(QJsonDocument(o).toJson());
    }

    void addRecent(const QString &path) {
        recentFiles.removeAll(path);
        recentFiles.prepend(path);
        while (recentFiles.size() > maxRecent) recentFiles.removeLast();
    }

    // Record (in memory only) the folder a Save/Open dialog just used.
    // Accepts either a directory or a full file path. Rejects empty or
    // no-longer-existing locations so a deleted folder or an unplugged
    // removable drive can never poison the next dialog's start directory.
    // Does NOT persist — the caller saves (e.g. openFile already writes
    // the config once); standalone callers use noteLastDir() instead.
    void setLastDir(const QString &pathOrDir) {
        if (pathOrDir.isEmpty()) return;
        const QFileInfo fi(pathOrDir);
        const QString dir = fi.isDir() ? fi.absoluteFilePath() : fi.absolutePath();
        if (dir.isEmpty() || !QDir(dir).exists()) return;
        lastDir = dir;
    }

    // Same as setLastDir() but persists immediately. Use from standalone
    // Save/Open dialogs that don't otherwise rewrite the config.
    void noteLastDir(const QString &pathOrDir) {
        const QString before = lastDir;
        setLastDir(pathOrDir);
        if (lastDir != before) save();
    }

    // Directory to start a Save/Open dialog in when there is no better
    // context (an untitled buffer, or a secondary dialog). Returns the
    // remembered directory if it still exists, else the home folder — so
    // the very first dialog on a fresh install behaves exactly as before.
    QString lastDirOrHome() const {
        if (!lastDir.isEmpty() && QDir(lastDir).exists()) return lastDir;
        return QDir::homePath();
    }

    static QString configPath() {
        return appConfigDir() + QStringLiteral("/config.json");
    }

    // v0.1.96 — platform-conventional config dir.
    //
    // Pre-fix all Notepatra builds wrote to `~/.config/notepatra/` on
    // EVERY OS — fine on Linux (matches XDG), wrong on macOS, wrong on
    // Windows. The Windows side bit a user hard: their %APPDATA%
    // appeared empty while session.json sat at `C:\Users\<u>\.config\
    // notepatra\`, and a stale entry there made the app hang on every
    // launch trying to reopen a file. IT tooling, backup tools, group
    // policy — all look in APPDATA on Windows. So this helper now
    // returns the platform-conventional location.
    //
    // Migration: if the new path is empty AND the legacy
    // `~/.config/notepatra/` has content, the contents are COPIED
    // (not moved) into the new path on first call. We don't delete
    // the legacy data so the user can roll back if something goes
    // wrong — that cleanup is a v0.1.97 follow-up.
    static QString appConfigDir() {
#ifdef Q_OS_WIN
        // %APPDATA%\Notepatra  — e.g. C:\Users\<u>\AppData\Roaming\Notepatra
        QString dir = qEnvironmentVariable("APPDATA");
        if (dir.isEmpty()) {
            // Fallback in the rare case APPDATA isn't set.
            dir = QDir::homePath() + QStringLiteral("/AppData/Roaming/Notepatra");
        } else {
            dir = QDir::fromNativeSeparators(dir) + QStringLiteral("/Notepatra");
        }
#elif defined(Q_OS_MAC)
        // ~/Library/Application Support/Notepatra
        QString dir = QDir::homePath() +
                      QStringLiteral("/Library/Application Support/Notepatra");
#else
        // Linux / BSD: $XDG_CONFIG_HOME/notepatra (default ~/.config/notepatra)
        QString xdg = qEnvironmentVariable("XDG_CONFIG_HOME");
        QString dir = xdg.isEmpty()
                          ? QDir::homePath() + QStringLiteral("/.config/notepatra")
                          : xdg + QStringLiteral("/notepatra");
#endif
        QDir().mkpath(dir);
        migrateLegacyConfigDirOnce(dir);
        return dir;
    }

    // Source of truth pre-v0.1.96 — always ~/.config/notepatra. We
    // still read from here as a migration source on Windows / macOS.
    static QString legacyLinuxConfigDir() {
        return QDir::homePath() + QStringLiteral("/.config/notepatra");
    }

private:
    // One-time migration: if the new platform-conventional dir is empty
    // and the legacy `~/.config/notepatra/` has content, recursively
    // copy. Runs at most once per launch — guard by static bool.
    // Skipped entirely on Linux because legacy == new there.
    static void migrateLegacyConfigDirOnce(const QString &newDir) {
        static bool done = false;
        if (done) return;
        done = true;
#if defined(Q_OS_WIN) || defined(Q_OS_MAC)
        const QString legacy = legacyLinuxConfigDir();
        if (legacy == newDir) return;
        // Both paths exist?
        if (!QFileInfo::exists(legacy)) return;
        // New path already has a config.json — don't overwrite.
        if (QFileInfo::exists(newDir + QStringLiteral("/config.json"))) return;
        // Walk the legacy tree and copy each file across.
        QDir().mkpath(newDir);
        QDirIterator it(legacy, QDir::Files | QDir::NoDotAndDotDot,
                        QDirIterator::Subdirectories);
        while (it.hasNext()) {
            const QString src = it.next();
            const QString rel = QDir(legacy).relativeFilePath(src);
            const QString dst = newDir + QLatin1Char('/') + rel;
            QDir().mkpath(QFileInfo(dst).path());
            if (!QFileInfo::exists(dst)) QFile::copy(src, dst);
        }
#else
        Q_UNUSED(newDir);
#endif
    }

    Config() { load(); }
};

#endif
