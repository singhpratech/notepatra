#include "ai_tools.h"

#include "csvanalyst.h"
#include "dbconnections.h"
#include "git_tools.h"

#include <QDateTime>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QFileInfoList>
#include <QJsonDocument>
#include <QRegularExpression>
#include <QSqlDatabase>
#include <QSqlError>
#include <QSqlQuery>
#include <QSqlRecord>
#include <QTextStream>

#include <cstdio>

namespace AiTools {

// ── Limits ───────────────────────────────────────────────────────────
//
// Sized for local models with smaller context windows than frontier
// cloud models — Notepatra runs against Ollama / llama.cpp where 8k–32k
// context is typical. Larger limits would burn the context window in 1
// or 2 tool calls.
namespace Limits {
    constexpr int kReadDefaultLines = 1500;
    constexpr int kReadMaxLineChars = 2000;
    constexpr int kListMaxEntries   = 500;
    constexpr int kBinarySniffBytes = 8192;
}

// ═══════════════════════════════════════════════════════════════════════
// Path safety — implemented per the recommendation distilled from Aider /
// Cursor / Continue.dev / Cody / Claude Code / Codeium research. Three
// independent layers:
//   1. Workspace anchor (resolveSafePath canonical-startsWith check)
//   2. Symlink resolution (canonicalFilePath follows links)
//   3. Hardcoded deny-list (catches symlinks-to-secrets)
// ═══════════════════════════════════════════════════════════════════════

bool isHardDenied(const QString &absPath) {
    const QString p = absPath.toLower();

    // Substring matches — credential / secret directories. Both forward-
    // and back-slash variants so this works on Windows where canonical
    // paths sometimes preserve backslashes from the OS API. Match on
    // path SEGMENTS (with leading + trailing separators) so we don't
    // false-positive on a project named "ssh" at the workspace root.
    static const QStringList denyContains = {
        "/.ssh/",   "\\.ssh\\",
        "/.gnupg/", "\\.gnupg\\",
        "/.aws/",   "\\.aws\\",
        "/.netrc",  "\\.netrc",
        "/etc/passwd", "/etc/shadow",
        "/.npmrc",     // npm authToken
        "/.pypirc",    // pypi credentials
        "/.docker/config.json",
    };
    for (const QString &needle : denyContains) {
        if (p.contains(needle)) return true;
    }

    // Suffix matches — private-key file extensions. *.pem and *.key are
    // not always credentials (test fixtures sometimes use them) but the
    // false-positive rate is low enough that "refuse and explain" is
    // safer than "leak occasionally".
    static const QStringList denySuffix = { ".pem", ".key", ".pfx", ".p12" };
    for (const QString &suf : denySuffix) {
        if (p.endsWith(suf)) return true;
    }

    // Filename patterns — id_rsa, id_ed25519, id_ecdsa, etc.
    static const QStringList denyFilename = {
        "id_rsa", "id_ed25519", "id_ecdsa", "id_dsa",
        "authorized_keys", "known_hosts",
    };
    for (const QString &name : denyFilename) {
        if (p.contains("/" + name) || p.contains("\\" + name)) return true;
    }

    return false;
}

bool resolveSafePath(const QString &pathArg,
                     const QString &workspaceRoot,
                     QString *outCanonical,
                     QString *outErrorKind) {
    auto setError = [&](const char *kind) {
        if (outErrorKind) *outErrorKind = QString::fromLatin1(kind);
    };

    if (pathArg.trimmed().isEmpty()) { setError("not_found"); return false; }
    if (workspaceRoot.trimmed().isEmpty()) {
        setError("outside_workspace"); return false;
    }

    // Resolve workspace root to its canonical absolute form once. If
    // the workspace itself is bogus / doesn't exist, refuse all reads.
    const QDir wsDir(workspaceRoot);
    QString canonicalWs = wsDir.canonicalPath();
    if (canonicalWs.isEmpty()) {
        setError("outside_workspace"); return false;
    }

    // Build the candidate absolute path. Two cases:
    //   (a) pathArg already absolute — use as-is, then canonicalize.
    //   (b) pathArg relative — join under workspace, then canonicalize.
    QString candidate = pathArg;
    if (!QDir::isAbsolutePath(candidate)) {
        candidate = wsDir.absoluteFilePath(candidate);
    }
    QFileInfo fi(candidate);
    QString canonicalPath = fi.canonicalFilePath();
    if (canonicalPath.isEmpty()) {
        // canonicalFilePath() returns "" if the file doesn't exist or
        // a symlink target is broken. Either way: treat as not_found.
        setError("not_found"); return false;
    }

    // Workspace-anchor check. The canonical path must equal the
    // workspace OR be strictly under it (with a trailing separator
    // after the workspace prefix to avoid /work matching /workspace).
    QString wsPrefix = canonicalWs;
    if (!wsPrefix.endsWith(QDir::separator()) && !wsPrefix.endsWith('/')) {
        wsPrefix += '/';
    }
    if (canonicalPath != canonicalWs && !canonicalPath.startsWith(wsPrefix)) {
        setError("outside_workspace"); return false;
    }

    // Hardcoded deny-list — catches the symlink-to-secret case where
    // workspace contains a link pointing at ~/.ssh/id_rsa.
    if (isHardDenied(canonicalPath)) {
        setError("denied"); return false;
    }

    if (outCanonical) *outCanonical = canonicalPath;
    return true;
}

// ═══════════════════════════════════════════════════════════════════════
// resolveSafeWritePath — same path-safety as resolveSafePath but the
// target file is allowed to NOT exist yet. The parent directory MUST
// exist + be inside the workspace, and the candidate target path is
// also matched against the hardcoded deny-list so writes can't create
// `~/.ssh/foo` even when the parent exists.
//
// Returns the absolute resolved target path in outAbsTarget on success.
// On failure outErrorKind is one of:
//   "outside_workspace" — workspace root bogus, parent dir resolves
//                         outside the workspace, or no workspace open
//   "not_found"         — pathArg empty, or parent directory doesn't
//                         exist (we don't auto-mkdir intermediate dirs
//                         to avoid surprise; agents must list_dir first
//                         and write_file in places they confirmed exist)
//   "denied"            — candidate target matches the credential
//                         deny-list (~/.ssh/, *.pem, *.key, etc.)
// ═══════════════════════════════════════════════════════════════════════
bool resolveSafeWritePath(const QString &pathArg,
                          const QString &workspaceRoot,
                          QString *outAbsTarget,
                          QString *outErrorKind) {
    auto setError = [&](const char *kind) {
        if (outErrorKind) *outErrorKind = QString::fromLatin1(kind);
    };

    if (pathArg.trimmed().isEmpty()) { setError("not_found"); return false; }
    if (workspaceRoot.trimmed().isEmpty()) {
        setError("outside_workspace"); return false;
    }

    const QDir wsDir(workspaceRoot);
    QString canonicalWs = wsDir.canonicalPath();
    if (canonicalWs.isEmpty()) {
        setError("outside_workspace"); return false;
    }

    // Build the candidate absolute path (target may not exist).
    QString candidate = pathArg;
    if (!QDir::isAbsolutePath(candidate)) {
        candidate = wsDir.absoluteFilePath(candidate);
    }
    QFileInfo fi(candidate);

    // Parent must resolve into the workspace. If the parent doesn't
    // exist YET (e.g. write_file("new/hello.py") to an empty workspace
    // where `new/` doesn't exist), mkpath it — but only if the lowest
    // existing ancestor IS inside the workspace. This lets the agent
    // create new subtrees without giving it write access to anywhere
    // outside the workspace via a non-existent traversal.
    const QString parentDir = fi.absolutePath();
    QFileInfo parentFi(parentDir);

    QString wsPrefix = canonicalWs;
    if (!wsPrefix.endsWith(QDir::separator()) && !wsPrefix.endsWith('/')) {
        wsPrefix += '/';
    }

    if (!parentFi.exists() || !parentFi.isDir()) {
        // Walk up to the lowest existing ancestor; canonicalize THAT
        // and verify it's inside the workspace. Only then mkpath the rest.
        QString lowest = parentDir;
        while (!lowest.isEmpty() && !QFileInfo(lowest).exists()) {
            const int slash = lowest.lastIndexOf('/');
            if (slash <= 0) { lowest.clear(); break; }
            lowest = lowest.left(slash);
        }
        if (lowest.isEmpty()) { setError("outside_workspace"); return false; }
        const QString canonicalLowest = QFileInfo(lowest).canonicalFilePath();
        if (canonicalLowest.isEmpty()) { setError("outside_workspace"); return false; }
        if (canonicalLowest != canonicalWs && !canonicalLowest.startsWith(wsPrefix)) {
            setError("outside_workspace"); return false;
        }
        if (!QDir().mkpath(parentDir)) {
            setError("io_error"); return false;
        }
        parentFi = QFileInfo(parentDir);
    }

    QString canonicalParent = parentFi.canonicalFilePath();
    if (canonicalParent.isEmpty()) {
        setError("not_found"); return false;
    }
    if (canonicalParent != canonicalWs && !canonicalParent.startsWith(wsPrefix)) {
        setError("outside_workspace"); return false;
    }

    // Construct the target path under the canonical parent. This is the
    // path we'll actually write to; we keep the user-supplied filename
    // (no canonicalize on the leaf since it doesn't exist yet).
    const QString targetAbs = canonicalParent + '/' + fi.fileName();

    // Apply the hardcoded deny-list to the candidate target — block
    // writes to credential paths even if the parent dir is in workspace.
    if (isHardDenied(targetAbs)) {
        setError("denied"); return false;
    }

    if (outAbsTarget) *outAbsTarget = targetAbs;
    return true;
}

// ═══════════════════════════════════════════════════════════════════════
// Model allowlist — name-pattern check for Ollama models known to be
// tool-trained. Matches Ollama 2026 catalog (per multi-editor research):
// qwen3*, qwen2.5*, llama3.1+, llama3.2, llama3.3, llama4, mistral-nemo,
// mistral-large, command-r*, hermes3, granite3, gpt-oss, firefunction.
// Avoids: phi-3-mini, gemma 2 base, llama 3.2 1b without instruct,
// vision-only tags.
// ═══════════════════════════════════════════════════════════════════════

bool modelCapableOfDataAnalysis(const QString &modelName) {
    if (modelName.isEmpty()) return false;
    const QString m = modelName.toLower();

    // ── Cloud frontier / premium models — always pass ───────────────
    static const QStringList kFrontierFamilies = {
        // Anthropic Claude
        "claude-3.5", "claude-3.7", "claude-4", "claude-opus", "claude-sonnet",
        "anthropic/claude",
        // OpenAI GPT-4 / o1 / o3 / 5
        "gpt-4", "gpt-4o", "gpt-4-turbo", "gpt-4.1", "gpt-4.5", "gpt-5",
        "o1-", "o3-", "openai/gpt-4", "openai/o1", "openai/o3",
        // Google Gemini
        "gemini-1.5-pro", "gemini-2.0", "gemini-2.5", "gemini-3", "gemini-pro",
        "google/gemini-1.5", "google/gemini-2",
        // DeepSeek
        "deepseek-v3", "deepseek-r1", "deepseek-chat", "deepseek-coder-v2",
        "deepseek/deepseek-v3", "deepseek/deepseek-r1",
        // Mistral premium
        "mistral-large", "mixtral-8x22", "mixtral",
        // Command R+
        "command-r-plus",
        // xAI Grok
        "grok-4", "grok-3", "x-ai/grok-3", "x-ai/grok-4",
        // Kimi / GLM / MiniMax
        "kimi-k2", "minimax-m2", "glm-4-plus", "glm-4.5",
    };
    for (const QString &fam : kFrontierFamilies) {
        if (m.contains(fam)) return true;
    }

    // ── Local models — pass only at adequate parameter size ─────────
    // We look for param-count tags like ":7b", "-7b", "7b-instruct", etc.
    // Anything <7B is rejected because real-world data-analyst tasks
    // (multi-table SQL, chart spec emission) need the larger models.
    static const QStringList kStrongLocalFamilies = {
        "qwen2.5-coder", "qwen3-coder", "qwen3",
        "llama3.1", "llama-3.1", "llama3.2", "llama-3.2",
        "llama3.3", "llama-3.3", "llama4", "llama-4",
        "deepseek-coder", "mistral-nemo",
        "command-r",  // base R is borderline; keep for users who picked it explicitly
        "hermes3", "hermes-3",
        "gemma2", "gemma-2", "gemma3", "gemma-3",
        "gpt-oss", "phi-4",
        "devstral", "nemotron",
    };

    // Param-count tag detection.
    static const QRegularExpression kSize(QStringLiteral("[^a-z](\\d+(?:\\.\\d+)?)\\s*b\\b"));
    auto getSizeBillions = [&]() -> double {
        QRegularExpressionMatch mm = kSize.match("." + m);
        if (!mm.hasMatch()) return -1.0;
        bool ok = false;
        double d = mm.captured(1).toDouble(&ok);
        return ok ? d : -1.0;
    };

    bool inStrongFamily = false;
    for (const QString &fam : kStrongLocalFamilies) {
        if (m.contains(fam)) { inStrongFamily = true; break; }
    }
    if (!inStrongFamily) return false;

    double sizeB = getSizeBillions();
    // If we couldn't extract a size, accept the model on family alone —
    // names like "qwen3-coder:latest" (no explicit tag) usually pull a
    // capable default. Vision-only tags fail.
    if (m.contains("vision") && !m.contains("instruct")) return false;
    if (sizeB < 0) return true;
    return sizeB >= 7.0;
}

QStringList suggestedModelsForDataAnalysis() {
    // v0.1.54 — family names, not version pins. Pre-fix the list named
    // specific versions (Claude 4 Opus, GPT-5, Gemini 2.5 Pro, qwen2.5-
    // coder:14b) which would all need refreshing every couple of months
    // as new releases land. Family names ("Claude", "GPT") stay accurate
    // for years.
    return {
        QStringLiteral("Claude (Anthropic)"),
        QStringLiteral("GPT (OpenAI)"),
        QStringLiteral("Gemini (Google)"),
        QStringLiteral("DeepSeek"),
        QStringLiteral("Qwen-Coder ≥7B (local)"),
        QStringLiteral("Llama ≥7B (local)"),
        QStringLiteral("Mistral / Mistral Large (local)"),
    };
}

bool modelLikelySupportsTools(const QString &modelName) {
    if (modelName.isEmpty()) return false;
    const QString m = modelName.toLower();

    // Substring allowlist — model families with native tool training.
    // Covers Ollama-hosted local models AND cloud-hosted models served
    // via OpenAI-compat APIs (OpenRouter, OpenAI, Anthropic-via-proxy,
    // Gemini, Mistral, etc.). Strategy: when in doubt and the backend
    // is OpenAI-compat, the caller can decide to send tools anyway —
    // unsupported models just ignore the tools field on those services.
    static const QStringList toolFamilies = {
        // ── Ollama local models ───────────────────────────────────
        "qwen3", "qwen2.5",
        "llama3.1", "llama3.2", "llama3.3", "llama-3.1", "llama-3.2", "llama-3.3",
        "llama4", "llama-4",
        "mistral-nemo", "mistral-large", "mixtral",
        "command-r", "command-r-plus", "commandr",
        "hermes3", "hermes-3",
        "granite3", "granite-3", "granite-code",
        "firefunction", "gpt-oss",
        "deepseek-v3", "deepseek-r1",
        "kimi-k2", "minimax-m2", "glm-4",
        "devstral", "lfm2", "ministral", "nemotron",
        // ── OpenAI cloud (also reachable via OpenRouter slug) ──────
        "gpt-4", "gpt-4o", "gpt-4-turbo", "gpt-3.5-turbo", "o1-", "o3-",
        "openai/gpt", "openai/o1", "openai/o3",
        // ── Anthropic Claude (OpenRouter slug or Anthropic-compat) ─
        "claude-3", "claude-3.5", "claude-3.7", "claude-4", "claude-opus",
        "claude-sonnet", "claude-haiku",
        "anthropic/claude",
        // ── Google Gemini (Vertex / OpenRouter) ───────────────────
        "gemini-1.5", "gemini-2", "gemini-pro", "gemini-flash",
        "google/gemini",
        // ── Mistral cloud / OpenRouter ────────────────────────────
        "mistral/mistral", "mistralai/", "mistral-medium", "mistral-small",
        // ── DeepSeek cloud ────────────────────────────────────────
        "deepseek/deepseek", "deepseek-chat",
        // ── xAI Grok ──────────────────────────────────────────────
        "grok-", "x-ai/grok",
        // ── Generic OpenRouter slugs (most route to tool-trained models) ─
        "openrouter/",
    };
    for (const QString &fam : toolFamilies) {
        if (m.contains(fam)) {
            // Vision-only tags often lack tool training.
            if (m.contains("vision") && !m.contains("tools")) return false;
            // Tiny 1B / instruct-only base models — known to fail.
            if (m.contains("1b") && !m.contains("instruct")) return false;
            return true;
        }
    }
    return false;
}

// ═══════════════════════════════════════════════════════════════════════
// Tool registry — JSON-Schema definitions sent to the backend so the
// model knows what it can call. Shape per Ollama / OpenAI tools spec.
// ═══════════════════════════════════════════════════════════════════════

QJsonArray availableTools() {
    auto makeTool = [](const QString &name,
                       const QString &description,
                       const QJsonObject &paramsSchema) {
        QJsonObject fn;
        fn["name"] = name;
        fn["description"] = description;
        fn["parameters"] = paramsSchema;
        QJsonObject tool;
        tool["type"] = "function";
        tool["function"] = fn;
        return tool;
    };

    QJsonArray tools;

    // read_file ----------------------------------------------------
    {
        QJsonObject props;
        props["path"] = QJsonObject{
            {"type", "string"},
            {"description", "Workspace-relative path to the file. Absolute paths are accepted only if they resolve inside the workspace."}
        };
        props["offset"] = QJsonObject{
            {"type", "integer"},
            {"description", "1-based starting line for paginated reads. Default 1."}
        };
        props["limit"] = QJsonObject{
            {"type", "integer"},
            {"description", "Maximum lines to return. Default 1500. Files larger than this come back truncated:true."}
        };
        props["with_line_numbers"] = QJsonObject{
            {"type", "boolean"},
            {"description", "Default true. Set false to receive raw file content WITHOUT the leading '      N\\t' line-number prefix. Use this when you plan to copy lines verbatim into apply_diff old_lines (saves you from having to strip the prefix)."}
        };
        QJsonObject params;
        params["type"] = "object";
        params["properties"] = props;
        params["required"] = QJsonArray{ "path" };
        tools.push_back(makeTool(
            "read_file",
            "Read the contents of a text file in the user's workspace. "
            "Returns the file's text with cat -n style line numbers by "
            "default; pass with_line_numbers=false for raw content (recommended "
            "when feeding the result into apply_diff old_lines). Paths must "
            "stay inside the workspace root; secret/credential paths "
            "(~/.ssh/, *.pem, *.key, /etc/passwd, etc.) are refused.",
            params));
    }

    // list_dir -----------------------------------------------------
    {
        QJsonObject props;
        props["path"] = QJsonObject{
            {"type", "string"},
            {"description", "Workspace-relative directory path. Use \".\" for the workspace root."}
        };
        QJsonObject params;
        params["type"] = "object";
        params["properties"] = props;
        params["required"] = QJsonArray{ "path" };
        tools.push_back(makeTool(
            "list_dir",
            "List one level of entries in a directory inside the user's "
            "workspace. Returns entries with type (file|dir) and size. "
            "Capped at 500 entries; .git, node_modules, and build dirs "
            "are filtered out.",
            params));
    }

    // write_file ---------------------------------------------------
    // v0.1.39 — create / overwrite / append a text file in the
    // workspace. Same path-safety as read_file (workspace anchor +
    // canonicalize parent + hardcoded deny-list); plus an "exists"
    // error_kind for mode=create when the target already exists.
    {
        QJsonObject props;
        props["path"] = QJsonObject{
            {"type", "string"},
            {"description", "Workspace-relative path. Parent directory must already exist (the tool does not auto-create directories — use list_dir to confirm structure first)."}
        };
        props["content"] = QJsonObject{
            {"type", "string"},
            {"description", "Full file content (UTF-8). Capped at 5 MB."}
        };
        props["mode"] = QJsonObject{
            {"type", "string"},
            {"enum", QJsonArray{"create", "overwrite", "append"}},
            {"description", "Default 'overwrite'. 'create' fails with error_kind:exists if the file exists. 'append' adds to the end."}
        };
        props["dry_run"] = QJsonObject{
            {"type", "boolean"},
            {"description", "If true, return what WOULD be written instead of writing. Used by Composer mode to preview. The file on disk is untouched; the result carries {dry_run:true, proposed:{path,before,after,mode}} for the UI to render a diff."}
        };
        QJsonObject params;
        params["type"] = "object";
        params["properties"] = props;
        params["required"] = QJsonArray{ "path", "content" };
        tools.push_back(makeTool(
            "write_file",
            "Write or overwrite a text file in the user's workspace. "
            "Use for creating new files or fully replacing existing ones; "
            "use apply_diff for line-level edits to large files. The new "
            "or modified file is auto-opened in the editor. Pass "
            "dry_run:true to PROPOSE the change for user review (Composer "
            "mode) instead of writing immediately.",
            params));
    }

    // search -------------------------------------------------------
    // v0.1.39 — pattern search across the workspace via the existing
    // ProjectSearchWorker (rust-core aho-corasick fast path). Returns
    // up to max_matches matches with file path + line + column +
    // surrounding line as snippet.
    {
        QJsonObject props;
        props["pattern"] = QJsonObject{
            {"type", "string"},
            {"description", "Pattern to search for. Literal substring by default; set regex=true for regex syntax (Rust regex flavour, no PCRE backreferences)."}
        };
        props["path"] = QJsonObject{
            {"type", "string"},
            {"description", "Workspace-relative directory to search. Default: workspace root."}
        };
        props["case_sensitive"] = QJsonObject{
            {"type", "boolean"},
            {"description", "Default false."}
        };
        props["regex"] = QJsonObject{
            {"type", "boolean"},
            {"description", "Default false (literal substring match)."}
        };
        props["glob"] = QJsonObject{
            {"type", "string"},
            {"description", "File-name glob filter, e.g. '*.py,*.js'. Default: all files."}
        };
        props["max_matches"] = QJsonObject{
            {"type", "integer"},
            {"description", "Default 50, max 200."}
        };
        QJsonObject params;
        params["type"] = "object";
        params["properties"] = props;
        params["required"] = QJsonArray{ "pattern" };
        tools.push_back(makeTool(
            "search",
            "Search for a pattern across files in the user's workspace. "
            "Returns up to 50 matches by default with file path, line, "
            "column, and one surrounding line as snippet. Heavy directories "
            "(.git, node_modules, build, etc.) are skipped automatically.",
            params));
    }

    // query_sql ----------------------------------------------------
    // v0.1.43 — run SQL against a saved DB connection (Data Analyst Mode).
    {
        QJsonObject props;
        props["connection_name"] = QJsonObject{
            {"type", "string"},
            {"description", "Name of a connection saved via Manage Connections... in Data Analyst Mode."}
        };
        props["sql"] = QJsonObject{
            {"type", "string"},
            {"description", "SQL to execute. By default only SELECT / WITH / EXPLAIN / PRAGMA / SHOW / DESCRIBE are allowed; for INSERT / UPDATE / DELETE / DDL set confirm=true after the user has explicitly approved."}
        };
        props["max_rows"] = QJsonObject{
            {"type", "integer"},
            {"description", "Cap on returned rows. Default 100, max 500."}
        };
        props["confirm"] = QJsonObject{
            {"type", "boolean"},
            {"description", "Set true ONLY after the user has approved a non-SELECT query (mutation / DDL). Default false."}
        };
        QJsonObject params;
        params["type"] = "object";
        params["properties"] = props;
        params["required"] = QJsonArray{ "connection_name", "sql" };
        tools.push_back(makeTool(
            "query_sql",
            "Run SQL against a saved database connection. SELECT-only by "
            "default. Returns columns + rows. Available only when Data "
            "Analyst Mode is on; the user manages connections via the AI "
            "panel's Manage Connections... button.",
            params));
    }

    // csv_query -----------------------------------------------------
    // v0.1.43 — query an attached CSV via in-memory SQLite. Saves the
    // model from having to scan the whole file as text.
    {
        QJsonObject props;
        props["file_path"] = QJsonObject{
            {"type", "string"},
            {"description", "Workspace-relative path to a CSV/TSV file. Loaded into an in-memory SQLite db with table name 'csv'; column names match the CSV header."}
        };
        props["sql"] = QJsonObject{
            {"type", "string"},
            {"description", "SQLite SQL referencing the 'csv' table, e.g. SELECT category, SUM(revenue) FROM csv GROUP BY category."}
        };
        props["max_rows"] = QJsonObject{
            {"type", "integer"},
            {"description", "Cap on returned rows. Default 500, max 2000."}
        };
        props["max_load_rows"] = QJsonObject{
            {"type", "integer"},
            {"description", "Cap on rows loaded from the CSV into SQLite. Default 250000."}
        };
        QJsonObject params;
        params["type"] = "object";
        params["properties"] = props;
        params["required"] = QJsonArray{ "file_path", "sql" };
        tools.push_back(makeTool(
            "csv_query",
            "Load a CSV into in-memory SQLite and run SQL against the 'csv' "
            "table. Available only when Data Analyst Mode is on. Use this "
            "instead of asking the model to manually scan a CSV — querying "
            "is exact, fast, and cheap on the model's context budget.",
            params));
    }

    // generate_chart -----------------------------------------------
    // v0.1.63 — Data Analyst Mode visualization. The model emits a
    // Vega-Lite spec object (https://vega.github.io/vega-lite/) and
    // AIPanel renders it inline via VegaChartRenderer (QtWebEngine).
    // The tool result echoes the chart id back so the model can
    // reference the chart in subsequent turns ("the second chart
    // shows ..."). Validation is intentionally light — Vega-Lite has
    // its own JSON Schema and the JS shell surfaces parse / runtime
    // errors as renderError signals, which the UI shows inline.
    {
        QJsonObject specProp;
        specProp["type"] = "object";
        specProp["description"] =
            "A complete Vega-Lite v5 spec. Required keys: 'mark' (string or "
            "object) and 'encoding' (object with at least one channel). "
            "Either include a 'data.values' array literal OR a 'data.url' "
            "pointing at a workspace-relative CSV. Keep specs under 100 KB.";
        QJsonObject props;
        props["spec"] = specProp;
        props["title"] = QJsonObject{
            {"type", "string"},
            {"description", "Optional human-readable chart title. If omitted "
                            "and the spec contains spec.title, that wins."}
        };
        props["id"] = QJsonObject{
            {"type", "string"},
            {"description", "Optional stable id the model can use to refer "
                            "to this chart later. Auto-generated if omitted."}
        };
        QJsonObject params;
        params["type"] = "object";
        params["properties"] = props;
        params["required"] = QJsonArray{ "spec" };
        tools.push_back(makeTool(
            "generate_chart",
            "Render a Vega-Lite chart inline in the chat transcript. Pass a "
            "complete Vega-Lite v5 spec object as the 'spec' parameter — the "
            "spec must include 'mark' + 'encoding'. Use this AFTER query_sql "
            "or csv_query has produced numbers to visualize. Available only "
            "when Data Analyst Mode is on.",
            params));
    }

    // git_status ---------------------------------------------------
    // Read-only git inspection — branch, ahead/behind, staged + modified
    // + untracked file lists. Backed by `git status --porcelain=v2`.
    {
        QJsonObject params;
        params["type"] = "object";
        params["properties"] = QJsonObject{};
        params["required"] = QJsonArray{};
        tools.push_back(makeTool(
            "git_status",
            "Show the working-tree status of the user's git workspace. "
            "Returns current branch, upstream, ahead/behind counts, plus "
            "arrays of staged, modified, untracked, and conflicted file "
            "paths. Read-only; does not change anything. Returns error_kind: "
            "no_workspace if no folder is open, not_a_repo if the workspace "
            "is not a git checkout.",
            params));
    }

    // git_diff -----------------------------------------------------
    // Working-tree or staged unified diff. Capped at 32 KB.
    {
        QJsonObject props;
        props["staged"] = QJsonObject{
            {"type", "boolean"},
            {"description", "Default false — diff working tree against HEAD. Set true to diff the staging area (--cached)."}
        };
        props["path"] = QJsonObject{
            {"type", "string"},
            {"description", "Optional workspace-relative path to limit the diff to a single file or subdirectory."}
        };
        QJsonObject params;
        params["type"] = "object";
        params["properties"] = props;
        params["required"] = QJsonArray{};
        tools.push_back(makeTool(
            "git_diff",
            "Show a unified diff of changes in the user's git workspace. "
            "By default diffs the working tree against HEAD; pass staged:true "
            "for the staged-vs-HEAD diff. Optional path argument narrows to a "
            "specific file. Output capped at 32 KB; truncated:true is set "
            "when capped. Read-only.",
            params));
    }

    // git_log ------------------------------------------------------
    // Recent commit list with hash / author / date / subject. Cap 100.
    {
        QJsonObject props;
        props["max_count"] = QJsonObject{
            {"type", "integer"},
            {"description", "Number of commits to return. Default 20, hard cap 100."}
        };
        props["path"] = QJsonObject{
            {"type", "string"},
            {"description", "Optional workspace-relative path — limit log to commits touching that file or directory."}
        };
        QJsonObject params;
        params["type"] = "object";
        params["properties"] = props;
        params["required"] = QJsonArray{};
        tools.push_back(makeTool(
            "git_log",
            "Return the most recent commits in the user's git workspace as a "
            "structured array of {hash, author, date, subject}. Default 20 "
            "commits, max 100. Optional path argument scopes to commits that "
            "touched a particular file. Read-only.",
            params));
    }

    // git_branch_list ----------------------------------------------
    // Local branches + their tracking refs + which is current.
    {
        QJsonObject params;
        params["type"] = "object";
        params["properties"] = QJsonObject{};
        params["required"] = QJsonArray{};
        tools.push_back(makeTool(
            "git_branch_list",
            "List local branches in the user's git workspace, plus the "
            "current branch and each branch's upstream tracking ref. "
            "Read-only.",
            params));
    }

    // git_show -----------------------------------------------------
    // Full commit object — message + diff. Capped at 32 KB.
    {
        QJsonObject props;
        props["commit"] = QJsonObject{
            {"type", "string"},
            {"description", "Commit reference. Accepts a full or abbreviated SHA (e.g. 'a1b2c3d'), a relative ref (HEAD, HEAD~1, HEAD~3), or a branch name. Must NOT start with '-' or contain shell metacharacters."}
        };
        QJsonObject params;
        params["type"] = "object";
        params["properties"] = props;
        params["required"] = QJsonArray{ "commit" };
        tools.push_back(makeTool(
            "git_show",
            "Show full metadata + diff for a single commit (header, author, "
            "message, file changes). Output capped at 32 KB; truncated:true "
            "is set when capped. Read-only.",
            params));
    }

    // apply_diff ---------------------------------------------------
    // v0.1.39 — atomic line-level edits to an existing file. Each
    // hunk has the expected old lines + the replacement; if the file
    // has drifted from the expected old content, the tool returns
    // error_kind:conflict and does NOT modify the file.
    {
        QJsonObject hunkProps;
        hunkProps["old_start_line"] = QJsonObject{
            {"type", "integer"},
            {"description", "1-based start line where the old_lines text begins in the file."}
        };
        hunkProps["old_lines"] = QJsonObject{
            {"type", "string"},
            {"description", "Exact text the tool expects to find at old_start_line. Used for conflict detection."}
        };
        hunkProps["new_lines"] = QJsonObject{
            {"type", "string"},
            {"description", "Replacement text."}
        };
        QJsonObject hunkSchema;
        hunkSchema["type"] = "object";
        hunkSchema["properties"] = hunkProps;
        hunkSchema["required"] = QJsonArray{"old_start_line", "old_lines", "new_lines"};

        QJsonObject hunksProp;
        hunksProp["type"] = "array";
        hunksProp["items"] = hunkSchema;
        hunksProp["description"] = "Array of edit hunks (max 50). Applied atomically.";

        QJsonObject props;
        props["path"] = QJsonObject{
            {"type", "string"},
            {"description", "Workspace-relative path of the file to edit. Must already exist."}
        };
        props["hunks"] = hunksProp;
        props["dry_run"] = QJsonObject{
            {"type", "boolean"},
            {"description", "If true, return what WOULD be written instead of writing. Used by Composer mode to preview. The file on disk is untouched; the result carries {dry_run:true, proposed:{path,before,after,mode}} for the UI to render a diff."}
        };

        QJsonObject params;
        params["type"] = "object";
        params["properties"] = props;
        params["required"] = QJsonArray{ "path", "hunks" };
        tools.push_back(makeTool(
            "apply_diff",
            "Apply line-level edits to an existing file. Pass an array of "
            "hunks; each has old_start_line + old_lines + new_lines. If "
            "the file has drifted from the expected old content, returns "
            "error_kind:conflict and does NOT modify the file (atomic "
            "apply). Use for surgical edits; use write_file to fully "
            "replace a file's content. Pass dry_run:true to PROPOSE the "
            "change for user review (Composer mode) instead of writing "
            "immediately.",
            params));
    }

    return tools;
}

// ═══════════════════════════════════════════════════════════════════════
// Execution dispatch
// ═══════════════════════════════════════════════════════════════════════

namespace {

ToolResult makeError(const ToolCall &call,
                     const QString &errorKind,
                     const QString &humanMessage) {
    ToolResult r;
    r.id = call.id;
    r.name = call.name;
    r.isError = true;
    r.errorKind = errorKind;
    QJsonObject body;
    body["ok"] = false;
    body["error_kind"] = errorKind;
    body["message"] = humanMessage;
    if (call.args.contains("path")) {
        body["path"] = call.args.value("path").toString();
    }
    r.content = QString::fromUtf8(QJsonDocument(body).toJson(QJsonDocument::Compact));
    return r;
}

ToolResult makeSuccess(const ToolCall &call, const QJsonObject &result) {
    ToolResult r;
    r.id = call.id;
    r.name = call.name;
    r.isError = false;
    QJsonObject body;
    body["ok"] = true;
    body["result"] = result;
    r.content = QString::fromUtf8(QJsonDocument(body).toJson(QJsonDocument::Compact));
    return r;
}

bool looksBinary(const QByteArray &sniff) {
    // Standard binary heuristic — NUL byte in the first chunk. UTF-8
    // text files don't contain NUL; binary files almost always do.
    return sniff.contains('\0');
}

ToolResult executeReadFile(const ToolCall &call, const QString &workspaceRoot) {
    const QString pathArg = call.args.value("path").toString();
    int offset = call.args.value("offset").toInt(1);
    int limit  = call.args.value("limit").toInt(Limits::kReadDefaultLines);
    // v0.1.40: opt-out of the "      N\t" line-number prefix. Models that
    // pipe read_file output back into apply_diff's old_lines without
    // stripping the prefix get spurious conflicts; the agent can pass
    // with_line_numbers=false on the read that feeds an apply_diff.
    // Default true preserves v0.1.39 behaviour for every existing caller.
    const bool withLineNums = call.args.contains("with_line_numbers")
        ? call.args.value("with_line_numbers").toBool(true)
        : true;
    if (offset < 1) offset = 1;
    if (limit  < 1) limit  = Limits::kReadDefaultLines;

    QString canonical;
    QString errorKind;
    if (!resolveSafePath(pathArg, workspaceRoot, &canonical, &errorKind)) {
        QString msg;
        if (errorKind == "outside_workspace")
            msg = "Path resolves outside the workspace root.";
        else if (errorKind == "denied")
            msg = "Access to this path is restricted (secret/credential pattern).";
        else
            msg = "File not found.";
        return makeError(call, errorKind, msg);
    }

    QFileInfo fi(canonical);
    if (!fi.exists() || !fi.isFile()) {
        return makeError(call, "not_found", "Not a regular file.");
    }

    QFile f(canonical);
    if (!f.open(QIODevice::ReadOnly)) {
        return makeError(call, "io_error",
                         "Failed to open: " + f.errorString());
    }

    // Binary sniff before slurping.
    QByteArray sniff = f.peek(Limits::kBinarySniffBytes);
    if (looksBinary(sniff)) {
        QJsonObject result;
        result["path"] = QDir(workspaceRoot).relativeFilePath(canonical);
        result["binary"] = true;
        result["size_bytes"] = fi.size();
        f.close();
        return makeError(call, "binary",
                         QString("File appears to be binary (%1 bytes); won't be inlined.")
                             .arg(fi.size()));
    }

    QTextStream in(&f);
    in.setCodec("UTF-8");

    int lineNo = 0;       // 1-based
    int emitted = 0;
    int totalLines = 0;
    QString out;
    out.reserve(qMin(int(fi.size()), 200000));

    while (!in.atEnd()) {
        QString line = in.readLine();
        ++lineNo;
        ++totalLines;
        if (lineNo < offset) continue;
        if (emitted >= limit) {
            // Drain remainder just to count totalLines, but cap so a
            // 100-million-line log file doesn't lock us up.
            int drained = 0;
            while (!in.atEnd() && drained < 100000) {
                in.readLine();
                ++totalLines;
                ++drained;
            }
            break;
        }
        // cat -n style line-number prefix. The 70% token overhead is
        // acceptable on local models where tokens are effectively free,
        // and the model uses these as edit-anchors. v0.1.40: callers can
        // opt out of the prefix via with_line_numbers=false (e.g. when
        // the result will be fed verbatim into apply_diff old_lines).
        if (line.size() > Limits::kReadMaxLineChars) {
            line.truncate(Limits::kReadMaxLineChars);
            line += "  ⟪truncated⟫";
        }
        if (withLineNums)
            out += QString("%1\t%2\n").arg(lineNo, 6).arg(line);
        else
            out += line + "\n";
        ++emitted;
    }
    f.close();

    QJsonObject result;
    result["path"] = QDir(workspaceRoot).relativeFilePath(canonical);
    result["content"] = out;
    result["lines_emitted"] = emitted;
    result["total_lines"] = totalLines;
    result["truncated"] = (totalLines > offset - 1 + emitted);
    result["offset"] = offset;
    return makeSuccess(call, result);
}

ToolResult executeListDir(const ToolCall &call, const QString &workspaceRoot) {
    QString pathArg = call.args.value("path").toString();
    if (pathArg.isEmpty()) pathArg = ".";

    QString canonical;
    QString errorKind;
    if (!resolveSafePath(pathArg, workspaceRoot, &canonical, &errorKind)) {
        QString msg;
        if (errorKind == "outside_workspace")
            msg = "Path resolves outside the workspace root.";
        else if (errorKind == "denied")
            msg = "Access to this path is restricted.";
        else
            msg = "Directory not found.";
        return makeError(call, errorKind, msg);
    }

    QFileInfo fi(canonical);
    if (!fi.exists() || !fi.isDir()) {
        return makeError(call, "not_found", "Not a directory.");
    }

    // Filter VCS / build / vendor cruft per project_search conventions
    // (mirrors the filters in mainwindow.cpp:3478-3502 file-tree walk).
    static const QStringList kSkipDirs = {
        ".git", "node_modules", "build", "target", "dist",
        ".venv", "venv", "__pycache__", ".cache", ".gradle",
        "DerivedData", ".idea", ".vs"
    };

    QDir dir(canonical);
    const QFileInfoList entries = dir.entryInfoList(
        QDir::AllEntries | QDir::NoDotAndDotDot | QDir::Hidden,
        QDir::DirsFirst | QDir::Name);

    QJsonArray items;
    int total = 0;
    int truncated = 0;
    for (const QFileInfo &e : entries) {
        if (e.isDir() && kSkipDirs.contains(e.fileName())) continue;
        ++total;
        if (items.size() >= Limits::kListMaxEntries) {
            ++truncated;
            continue;
        }
        QJsonObject item;
        item["name"] = e.fileName();
        item["type"] = e.isDir() ? "dir" : "file";
        if (!e.isDir()) item["size"] = e.size();
        items.push_back(item);
    }

    QJsonObject result;
    result["path"] = QDir(workspaceRoot).relativeFilePath(canonical);
    result["entries"] = items;
    result["total"] = total;
    result["truncated"] = (truncated > 0);
    return makeSuccess(call, result);
}

// ═══════════════════════════════════════════════════════════════════════
// executeWriteFile — v0.1.39
//
// Create / overwrite / append a text file. Path safety via
// resolveSafeWritePath (workspace anchor, parent must exist, deny-list
// applies). On success returns:
//   {ok, result: {path, bytes_written, mode, created}}
// `created` is true when the target didn't exist before this call.
//
// v0.1.56 — when args carry `dry_run:true`, runs ALL the same safety
// checks (path resolve, hard-deny, mode validation, content cap,
// exists-check for mode=create) but DOES NOT touch the filesystem
// for writes. The "before" snapshot (existing file content, if any)
// is read so the UI can render a diff preview. Returns:
//   {ok, result: {dry_run:true, proposed: {path, before, after, mode},
//                 created (would be), bytes_would_write}}
// Used by Composer mode where the user reviews the proposal in the
// UI and clicks Apply to commit. The Apply path re-issues the same
// call with dry_run:false (or, equivalently, drops the field).
// ═══════════════════════════════════════════════════════════════════════
ToolResult executeWriteFile(const ToolCall &call,
                            const QString &workspaceRoot) {
    const QString pathArg = call.args.value("path").toString();
    const QString content = call.args.value("content").toString();
    QString modeStr = call.args.value("mode").toString().toLower();
    if (modeStr.isEmpty()) modeStr = "overwrite";
    if (modeStr != "create" && modeStr != "overwrite" && modeStr != "append") {
        return makeError(call, "io_error",
                         "Invalid mode: must be 'create', 'overwrite', or 'append'.");
    }
    const bool dryRun = call.args.value("dry_run").toBool(false);

    // Refuse implausibly large content. Typical AI-generated files are
    // kilobytes; 5 MB is a generous cap that catches the model going
    // off the rails (e.g., dumping its training corpus).
    // Apply this BEFORE the dry_run branch so a too-large proposal is
    // rejected at the same gate the real write would have hit.
    constexpr int kMaxContentBytes = 5 * 1024 * 1024;
    if (content.toUtf8().size() > kMaxContentBytes) {
        return makeError(call, "too_large",
                         QString("Content exceeds %1 KB cap.")
                             .arg(kMaxContentBytes / 1024));
    }

    QString absTarget;
    QString errorKind;
    if (!resolveSafeWritePath(pathArg, workspaceRoot, &absTarget, &errorKind)) {
        QString msg;
        if (errorKind == "outside_workspace")
            msg = "Path resolves outside the workspace root.";
        else if (errorKind == "denied")
            msg = "Access to this path is restricted (secret/credential pattern).";
        else
            msg = "Parent directory not found. Use list_dir to confirm structure first.";
        return makeError(call, errorKind, msg);
    }

    QFileInfo fi(absTarget);
    const bool existed = fi.exists();

    if (existed && !fi.isFile()) {
        return makeError(call, "io_error",
                         "Target exists but is not a regular file.");
    }
    if (existed && modeStr == "create") {
        return makeError(call, "exists",
                         "File already exists. Use mode='overwrite' to replace, "
                         "or mode='append' to add to it.");
    }

    if (dryRun) {
        // Read the existing content for the "before" snapshot of the
        // diff preview. If the file doesn't exist (mode=create or fresh
        // overwrite of an absent path), `before` is empty.
        QString before;
        if (existed) {
            QFile fr(absTarget);
            if (fr.open(QIODevice::ReadOnly)) {
                before = QString::fromUtf8(fr.readAll());
                fr.close();
            }
            // Read failure on an existing file is non-fatal for a dry
            // run — we just present an empty `before`. The UI can warn.
        }
        // Compose the "after" snapshot from the proposed mode.
        QString after;
        if (modeStr == "append") {
            after = before + content;
        } else {
            // overwrite / create — replacement is the entire new content.
            after = content;
        }
        QJsonObject proposed;
        proposed["path"]   = QDir(workspaceRoot).relativeFilePath(absTarget);
        proposed["before"] = before;
        proposed["after"]  = after;
        proposed["mode"]   = modeStr;
        QJsonObject result;
        result["dry_run"]           = true;
        result["proposed"]          = proposed;
        result["path"]              = QDir(workspaceRoot).relativeFilePath(absTarget);
        result["abs_path"]          = absTarget;
        result["mode"]              = modeStr;
        result["created"]           = !existed;
        result["bytes_would_write"] = content.toUtf8().size();
        return makeSuccess(call, result);
    }

    QFile f(absTarget);
    QIODevice::OpenMode openMode = (modeStr == "append")
        ? (QIODevice::WriteOnly | QIODevice::Append)
        : (QIODevice::WriteOnly | QIODevice::Truncate);
    if (!f.open(openMode)) {
        return makeError(call, "io_error",
                         "Failed to open for writing: " + f.errorString());
    }
    const QByteArray bytes = content.toUtf8();
    qint64 written = f.write(bytes);
    f.close();
    if (written != bytes.size()) {
        return makeError(call, "io_error",
                         QString("Short write: wrote %1 of %2 bytes.")
                             .arg(written).arg(bytes.size()));
    }

    QJsonObject result;
    result["path"]          = QDir(workspaceRoot).relativeFilePath(absTarget);
    result["abs_path"]      = absTarget;
    result["bytes_written"] = bytes.size();
    result["mode"]          = modeStr;
    result["created"]       = !existed;
    return makeSuccess(call, result);
}

// ═══════════════════════════════════════════════════════════════════════
// executeSearch — v0.1.39
//
// Pattern search across the workspace. Reuses the heavy-dir filtering
// from list_dir (.git, node_modules, etc. skipped). Literal substring
// match by default; regex via flag. Returns up to max_matches matches
// with file path + line + column + the matching line as snippet.
//
// Implementation note: rather than wiring up ProjectSearchWorker (which
// is signal-based and async, designed for the UI panel), we do a
// synchronous walk here — the agent loop is already async at the
// conversation layer; tool execution can block the worker's thread for
// a couple of seconds without affecting UI responsiveness. This avoids
// having to thread an event loop into AiTools::execute().
// ═══════════════════════════════════════════════════════════════════════
ToolResult executeSearch(const ToolCall &call, const QString &workspaceRoot) {
    const QString pattern = call.args.value("pattern").toString();
    if (pattern.isEmpty()) {
        return makeError(call, "io_error", "Empty search pattern.");
    }
    QString pathArg = call.args.value("path").toString();
    if (pathArg.isEmpty()) pathArg = ".";
    const bool caseSensitive = call.args.value("case_sensitive").toBool(false);
    const bool useRegex      = call.args.value("regex").toBool(false);
    const QString glob       = call.args.value("glob").toString();
    int maxMatches = call.args.value("max_matches").toInt(50);
    if (maxMatches <= 0)  maxMatches = 50;
    if (maxMatches > 200) maxMatches = 200;

    QString canonicalRoot;
    QString errorKind;
    if (!resolveSafePath(pathArg, workspaceRoot, &canonicalRoot, &errorKind)) {
        QString msg;
        if (errorKind == "outside_workspace")
            msg = "Search path resolves outside the workspace root.";
        else if (errorKind == "denied")
            msg = "Access to this path is restricted.";
        else
            msg = "Search path not found.";
        return makeError(call, errorKind, msg);
    }

    QFileInfo rootFi(canonicalRoot);
    if (!rootFi.exists()) {
        return makeError(call, "not_found", "Search path does not exist.");
    }

    // Compile regex if requested. For literal mode, use lower-cased
    // substring search when caseSensitive is false.
    QRegularExpression re;
    if (useRegex) {
        QRegularExpression::PatternOptions opts =
            QRegularExpression::NoPatternOption;
        if (!caseSensitive) opts |= QRegularExpression::CaseInsensitiveOption;
        re = QRegularExpression(pattern, opts);
        if (!re.isValid()) {
            return makeError(call, "io_error",
                             "Invalid regex: " + re.errorString());
        }
    }
    const QString needle = caseSensitive ? pattern : pattern.toLower();

    // Heavy-dir filter (matches list_dir's filter list).
    static const QSet<QString> kSkipDirs = {
        ".git", "node_modules", "build", "target", "dist",
        ".venv", "venv", "__pycache__", ".cache", ".gradle",
        "DerivedData", ".idea", ".vs"
    };

    // Glob -> regex (cheap conversion, supports comma-separated list).
    QVector<QRegularExpression> globs;
    if (!glob.isEmpty()) {
        for (const QString &g : glob.split(',', Qt::SkipEmptyParts)) {
            QString g2 = g.trimmed();
            if (g2.isEmpty()) continue;
            // QRegularExpression::wildcardToRegularExpression handles
            // *.py / *.{js,ts} / etc.
            globs.push_back(QRegularExpression(
                QRegularExpression::wildcardToRegularExpression(g2)));
        }
    }
    auto globMatches = [&](const QString &fileName) {
        if (globs.isEmpty()) return true;
        for (const auto &g : globs) {
            if (g.match(fileName).hasMatch()) return true;
        }
        return false;
    };

    QJsonArray matches;
    int totalMatches = 0;
    int filesScanned = 0;
    bool truncated = false;

    QDirIterator it(canonicalRoot,
                    QDir::Files | QDir::NoDotAndDotDot | QDir::Hidden,
                    QDirIterator::Subdirectories);
    while (it.hasNext() && totalMatches < maxMatches) {
        const QString fp = it.next();
        // Skip heavy dirs anywhere in the path.
        bool skip = false;
        for (const QString &part : fp.split('/')) {
            if (kSkipDirs.contains(part)) { skip = true; break; }
        }
        if (skip) continue;

        QFileInfo fi(fp);
        if (!globMatches(fi.fileName())) continue;
        // Skip absurdly large files to keep the tool snappy.
        if (fi.size() > 5 * 1024 * 1024) continue;
        ++filesScanned;

        QFile f(fp);
        if (!f.open(QIODevice::ReadOnly | QIODevice::Text)) continue;
        // Binary sniff (NUL byte in first 4 KB).
        QByteArray sniff = f.peek(4096);
        if (sniff.contains('\0')) { f.close(); continue; }

        QTextStream in(&f);
        in.setCodec("UTF-8");
        int lineNo = 0;
        while (!in.atEnd() && totalMatches < maxMatches) {
            QString line = in.readLine();
            ++lineNo;
            int col = -1;
            if (useRegex) {
                QRegularExpressionMatch m = re.match(line);
                if (m.hasMatch()) col = m.capturedStart() + 1;
            } else {
                const QString hay = caseSensitive ? line : line.toLower();
                int idx = hay.indexOf(needle);
                if (idx >= 0) col = idx + 1;
            }
            if (col > 0) {
                QJsonObject m;
                m["path"]    = QDir(workspaceRoot).relativeFilePath(fp);
                m["line"]    = lineNo;
                m["col"]     = col;
                // Cap snippet at 200 chars so very long lines don't
                // blow the model's context budget.
                m["snippet"] = line.left(200);
                matches.push_back(m);
                ++totalMatches;
            }
        }
        f.close();
    }
    if (totalMatches >= maxMatches && it.hasNext()) truncated = true;

    QJsonObject result;
    result["pattern"]              = pattern;
    result["path"]                 = QDir(workspaceRoot).relativeFilePath(canonicalRoot);
    result["case_sensitive"]       = caseSensitive;
    result["regex"]                = useRegex;
    result["files_scanned"]        = filesScanned;
    result["total_matches"]        = totalMatches;
    result["truncated"]            = truncated;
    result["matches"]              = matches;
    return makeSuccess(call, result);
}

// ═══════════════════════════════════════════════════════════════════════
// executeApplyDiff — v0.1.39
//
// Atomic line-level edits. Each hunk has old_start_line + old_lines +
// new_lines. Algorithm:
//   1. Read the entire file as a list of lines.
//   2. For each hunk, verify file[old_start_line .. +N) == old_lines.
//      If any hunk's old content doesn't match → conflict; abort.
//   3. Apply hunks in REVERSE order (largest line number first) so
//      earlier hunks' line numbers don't shift after later hunks
//      change line counts.
//   4. Write the result via temp-file + rename for atomicity.
//
// v0.1.56 — when args carry `dry_run:true`, runs all of phases 1-3
// (path resolve, hard-deny, hunk validation, in-memory apply) but
// SKIPS phase 4 (the temp-file + rename). Returns a proposal:
//   {ok, result: {dry_run:true,
//                 proposed: {path, before, after, mode:"apply_diff"},
//                 hunks_applied, warnings?}}
// `before` is the file's current content read for hunk validation;
// `after` is the in-memory result after applying all hunks. The file
// is NEVER modified on the dry_run path. A hunk that would conflict
// returns error_kind:"conflict" (same as the real path) and no proposal.
// ═══════════════════════════════════════════════════════════════════════
ToolResult executeApplyDiff(const ToolCall &call, const QString &workspaceRoot) {
    const QString pathArg = call.args.value("path").toString();
    const QJsonValue hunksVal = call.args.value("hunks");
    if (!hunksVal.isArray()) {  // hardening: explicit type check (was an implicit toArray() that yielded empty for bad shapes)
        return makeError(call, "io_error", "hunks must be an array.");
    }
    const QJsonArray hunks = hunksVal.toArray();
    if (hunks.isEmpty()) {
        return makeError(call, "io_error", "hunks array is empty.");
    }
    if (hunks.size() > 50) {
        return makeError(call, "too_large",
                         "Too many hunks (cap is 50). Split into multiple calls.");
    }
    const bool dryRun = call.args.value("dry_run").toBool(false);

    QString canonical;
    QString errorKind;
    if (!resolveSafePath(pathArg, workspaceRoot, &canonical, &errorKind)) {
        QString msg;
        if (errorKind == "outside_workspace")
            msg = "Path resolves outside the workspace root.";
        else if (errorKind == "denied")
            msg = "Access to this path is restricted.";
        else
            msg = "File not found.";
        return makeError(call, errorKind, msg);
    }

    QFileInfo fi(canonical);
    if (!fi.exists() || !fi.isFile()) {
        return makeError(call, "not_found", "Not a regular file.");
    }

    QFile f(canonical);
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text)) {
        return makeError(call, "io_error",
                         "Failed to open: " + f.errorString());
    }
    const QString content = QString::fromUtf8(f.readAll());
    f.close();

    // Split into lines preserving final newline behaviour.
    QStringList lines = content.split('\n');
    // If the file ends with a newline, split() leaves an empty trailing
    // element. We handle this by tracking it and re-adding it.
    bool trailingNewline = !lines.isEmpty() && lines.last().isEmpty();
    if (trailingNewline) lines.removeLast();

    // Sort hunks by old_start_line, then validate + apply in REVERSE.
    QVector<QJsonObject> sortedHunks;
    sortedHunks.reserve(hunks.size());
    for (const QJsonValue &v : hunks) {
        if (!v.isObject()) {  // hardening: refuse non-object hunk entries instead of silently coercing to {}
            return makeError(call, "io_error", "Each hunk must be an object.");
        }
        sortedHunks.push_back(v.toObject());
    }
    std::sort(sortedHunks.begin(), sortedHunks.end(),
              [](const QJsonObject &a, const QJsonObject &b) {
                  return a.value("old_start_line").toInt()
                         < b.value("old_start_line").toInt();
              });

    // Phase 1 — validate all hunks against the current file. If ANY
    // mismatches, bail with conflict; the file is untouched.
    //
    // v0.1.40: three-tier match (strict → strip line-num prefix → trimmed)
    // with structured warnings on the relaxed paths. Models often echo back
    // read_file's "      N\t" line-number prefix into apply_diff's old_lines,
    // or get whitespace slightly wrong. The relaxed tiers let the agent make
    // forward progress while the warning lets it self-correct.
    static const QRegularExpression kReadFilePrefix(QStringLiteral("^\\s*\\d+\\t"));
    enum MatchTier { TierStrict = 0, TierStripPrefix = 1, TierTrimmed = 2 };
    QStringList warnings;

    for (const QJsonObject &h : sortedHunks) {
        const int startLine = h.value("old_start_line").toInt();
        const QString oldText = h.value("old_lines").toString();
        QStringList oldLines = oldText.split('\n');
        // Drop trailing empty if oldText ends with \n.
        if (!oldLines.isEmpty() && oldLines.last().isEmpty() && oldText.endsWith('\n'))
            oldLines.removeLast();

        if (startLine < 1 || startLine - 1 + oldLines.size() > lines.size()) {
            return makeError(call, "conflict",
                             QString("Hunk at line %1 references lines beyond the file's range.")
                                 .arg(startLine));
        }

        // Try each match tier; first successful tier wins for this hunk.
        MatchTier hunkTier = TierStrict;
        bool matched = true;
        int firstMismatch = -1;

        // Tier 0 — strict equality.
        for (int i = 0; i < oldLines.size(); ++i) {
            if (lines[startLine - 1 + i] != oldLines[i]) {
                matched = false;
                firstMismatch = i;
                break;
            }
        }

        // Tier 1 — strip "      N\t" line-num prefix from old_lines (the model
        // copied read_file output verbatim).
        if (!matched) {
            matched = true;
            firstMismatch = -1;
            for (int i = 0; i < oldLines.size(); ++i) {
                QString oldStripped = oldLines[i];
                oldStripped.remove(kReadFilePrefix);
                if (lines[startLine - 1 + i] != oldStripped) {
                    matched = false;
                    firstMismatch = i;
                    break;
                }
            }
            if (matched) hunkTier = TierStripPrefix;
        }

        // Tier 2 — trimmed comparison (whitespace-only difference).
        if (!matched) {
            matched = true;
            firstMismatch = -1;
            for (int i = 0; i < oldLines.size(); ++i) {
                QString oldStripped = oldLines[i];
                oldStripped.remove(kReadFilePrefix);
                if (lines[startLine - 1 + i].trimmed() != oldStripped.trimmed()) {
                    matched = false;
                    firstMismatch = i;
                    break;
                }
            }
            if (matched) hunkTier = TierTrimmed;
        }

        if (!matched) {
            return makeError(call, "conflict",
                             QString("Hunk at line %1 doesn't match the file's current content "
                                     "(line %2 differs). The file may have been modified since the "
                                     "agent last read it. Re-read the file and try again.")
                                 .arg(startLine).arg(startLine + firstMismatch));
        }

        if (hunkTier == TierStripPrefix) {
            warnings.append(QString("hunk@%1: matched after stripping read_file line-number prefix from old_lines")
                                .arg(startLine));
        } else if (hunkTier == TierTrimmed) {
            warnings.append(QString("hunk@%1: matched only after whitespace normalization — verify indentation in new_lines")
                                .arg(startLine));
        }
    }

    // Phase 2 — apply in reverse so earlier hunks' indices are stable.
    for (int hi = sortedHunks.size() - 1; hi >= 0; --hi) {
        const QJsonObject &h = sortedHunks[hi];
        const int startLine = h.value("old_start_line").toInt();
        QStringList oldLines = h.value("old_lines").toString().split('\n');
        if (!oldLines.isEmpty() && oldLines.last().isEmpty() &&
            h.value("old_lines").toString().endsWith('\n'))
            oldLines.removeLast();
        QStringList newLines = h.value("new_lines").toString().split('\n');
        if (!newLines.isEmpty() && newLines.last().isEmpty() &&
            h.value("new_lines").toString().endsWith('\n'))
            newLines.removeLast();

        // Replace lines [startLine-1 .. startLine-1+oldLines.size()) with newLines.
        for (int i = oldLines.size() - 1; i >= 0; --i) {
            lines.removeAt(startLine - 1 + i);
        }
        for (int i = newLines.size() - 1; i >= 0; --i) {
            lines.insert(startLine - 1, newLines[i]);
        }
    }

    QString out = lines.join('\n');
    if (trailingNewline) out += '\n';

    if (dryRun) {
        // Composer mode preview: return the proposed before/after without
        // touching disk. `content` is the current file (already read above
        // for hunk validation). `out` is the in-memory result of applying
        // the hunks. The file itself is NEVER opened for writing on this
        // path — no temp file, no rename.
        QJsonObject proposed;
        proposed["path"]   = QDir(workspaceRoot).relativeFilePath(canonical);
        proposed["before"] = content;
        proposed["after"]  = out;
        proposed["mode"]   = "apply_diff";
        QJsonObject result;
        result["dry_run"]           = true;
        result["proposed"]          = proposed;
        result["path"]              = QDir(workspaceRoot).relativeFilePath(canonical);
        result["abs_path"]          = canonical;
        result["hunks_applied"]     = hunks.size();
        result["bytes_would_write"] = out.toUtf8().size();
        if (!warnings.isEmpty()) {
            QJsonArray w;
            for (const QString &s : warnings) w.append(s);
            result["warnings"] = w;
        }
        return makeSuccess(call, result);
    }

    // Write atomically: write to .tmp, then rename.
    const QString tmpPath = canonical + ".notepatra.tmp";
    QFile tmp(tmpPath);
    if (!tmp.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        return makeError(call, "io_error",
                         "Failed to open temp file: " + tmp.errorString());
    }
    QByteArray outBytes = out.toUtf8();
    qint64 w = tmp.write(outBytes);
    tmp.close();
    if (w != outBytes.size()) {
        QFile::remove(tmpPath);
        return makeError(call, "io_error", "Short write to temp file.");
    }
    // QSaveFile-style rename with overwrite. Qt's QFile::rename refuses
    // to overwrite a target that already exists, so use the platform's
    // posix rename() directly — that IS atomic on Linux/macOS. On
    // Windows the C-runtime rename() also can't overwrite, so do the
    // remove+rename two-step there.
#ifdef Q_OS_WIN
    QFile::remove(canonical);
    if (!QFile::rename(tmpPath, canonical)) {
        QFile::remove(tmpPath);
        return makeError(call, "io_error", "Failed to atomically rename temp file.");
    }
#else
    if (std::rename(tmpPath.toLocal8Bit().constData(),
                    canonical.toLocal8Bit().constData()) != 0) {
        QFile::remove(tmpPath);
        return makeError(call, "io_error", "Failed to atomically rename temp file.");
    }
#endif

    QJsonObject result;
    result["path"]          = QDir(workspaceRoot).relativeFilePath(canonical);
    result["abs_path"]      = canonical;
    result["hunks_applied"] = hunks.size();
    result["bytes_written"] = outBytes.size();
    if (!warnings.isEmpty()) {
        QJsonArray w;
        for (const QString &s : warnings) w.append(s);
        result["warnings"] = w;
    }
    return makeSuccess(call, result);
}

// ═══════════════════════════════════════════════════════════════════════
// executeQuerySql — v0.1.43 (Data Analyst Mode)
//
// Runs SQL against a connection saved in db-connections.json. Defaults to
// SELECT-only; mutations require confirm:true. Returns columns + rows in
// a compact format the model can summarize.
// ═══════════════════════════════════════════════════════════════════════
ToolResult executeQuerySql(const ToolCall &call) {
    const QString connectionName = call.args.value("connection_name").toString();
    const QString sql = call.args.value("sql").toString();
    int maxRows = call.args.value("max_rows").toInt(100);
    if (maxRows <= 0) maxRows = 100;
    if (maxRows > 500) maxRows = 500;
    const bool allowMutation = call.args.value("confirm").toBool(false);

    if (connectionName.isEmpty()) {
        return makeError(call, "io_error", "connection_name is required.");
    }
    if (sql.trimmed().isEmpty()) {
        return makeError(call, "io_error", "sql is required.");
    }

    DbConnections::Record rec;
    if (!DbConnections::findByName(connectionName, &rec)) {
        return makeError(call, "no_connection",
                         QString("No connection named '%1'. Use the Manage "
                                 "Connections... dialog in the AI panel to add one.")
                             .arg(connectionName));
    }

    DbConnections::QueryResult qr =
        DbConnections::runQuery(rec, sql, maxRows, allowMutation);
    if (!qr.ok) {
        return makeError(call, qr.errorKind.isEmpty() ? "exec_failed" : qr.errorKind,
                         qr.error);
    }

    QJsonArray colsJson;
    for (const QString &c : qr.columns) colsJson.append(c);
    QJsonArray rowsJson;
    for (const auto &row : qr.rows) {
        QJsonArray r;
        for (const QString &cell : row) r.append(cell);
        rowsJson.append(r);
    }
    QJsonObject result;
    result["connection_name"] = connectionName;
    result["driver"]          = rec.driver;
    result["columns"]         = colsJson;
    result["rows"]            = rowsJson;
    result["rows_returned"]   = qr.rowsReturned;
    result["truncated"]       = qr.truncated;
    return makeSuccess(call, result);
}

// ═══════════════════════════════════════════════════════════════════════
// executeCsvQuery — v0.1.43 (Data Analyst Mode)
//
// Loads a workspace CSV into in-memory SQLite (table name 'csv', columns
// match the header), runs the supplied SQL against it, returns the rows.
// Path safety via resolveSafePath. Tolerates non-CSV files only loosely
// (the loader will produce a single TEXT column and load lines as rows).
// ═══════════════════════════════════════════════════════════════════════
ToolResult executeCsvQuery(const ToolCall &call, const QString &workspaceRoot) {
    const QString pathArg = call.args.value("file_path").toString();
    const QString sql = call.args.value("sql").toString();
    int maxRows     = call.args.value("max_rows").toInt(500);
    int maxLoadRows = call.args.value("max_load_rows").toInt(250000);
    if (maxRows <= 0) maxRows = 500;
    if (maxRows > 2000) maxRows = 2000;
    if (maxLoadRows <= 0) maxLoadRows = 250000;

    if (pathArg.isEmpty()) {
        return makeError(call, "io_error", "file_path is required.");
    }
    if (sql.trimmed().isEmpty()) {
        return makeError(call, "io_error", "sql is required.");
    }

    QString canonical;
    QString errorKind;
    if (!resolveSafePath(pathArg, workspaceRoot, &canonical, &errorKind)) {
        QString msg;
        if (errorKind == "outside_workspace")
            msg = "CSV path resolves outside the workspace root.";
        else if (errorKind == "denied")
            msg = "Access to this path is restricted.";
        else
            msg = "CSV file not found.";
        return makeError(call, errorKind, msg);
    }

    QString cname, loadErr;
    bool truncatedLoad = false;
    if (!CsvAnalyst::loadIntoSqlite(canonical, maxLoadRows, &cname, &truncatedLoad, &loadErr)) {
        return makeError(call, "io_error",
                         QString("CSV load failed: %1").arg(loadErr));
    }

    // Run the query — SELECT-only enforced lightly (user-supplied via the
    // model; the agent should only emit SELECTs against in-memory data).
    QJsonObject result;
    {
        QSqlDatabase db = QSqlDatabase::database(cname);
        QSqlQuery q(db);
        if (!q.exec(sql)) {
            const QString err = q.lastError().text();
            QSqlDatabase::removeDatabase(cname);
            return makeError(call, "exec_failed",
                             QString("SQL error: %1").arg(err));
        }
        const QSqlRecord rec = q.record();
        const int cols = rec.count();
        QJsonArray colsJson;
        for (int i = 0; i < cols; ++i) colsJson.append(rec.fieldName(i));

        QJsonArray rowsJson;
        int rowsReturned = 0;
        bool truncatedQuery = false;
        while (q.next()) {
            if (rowsReturned >= maxRows) { truncatedQuery = true; break; }
            QJsonArray row;
            for (int i = 0; i < cols; ++i) {
                QVariant v = q.value(i);
                if (v.isNull()) row.append(QJsonValue::Null);
                else if (v.type() == QVariant::Double || v.type() == QVariant::LongLong ||
                         v.type() == QVariant::Int)
                    row.append(v.toDouble());
                else row.append(v.toString());
            }
            rowsJson.append(row);
            ++rowsReturned;
        }
        result["columns"]       = colsJson;
        result["rows"]          = rowsJson;
        result["rows_returned"] = rowsReturned;
        result["truncated"]     = truncatedQuery;
        result["load_truncated"] = truncatedLoad;
        if (!loadErr.isEmpty()) result["load_warning"] = loadErr;
        result["file_path"]     = QDir(workspaceRoot).relativeFilePath(canonical);
    }
    QSqlDatabase::removeDatabase(cname);
    return makeSuccess(call, result);
}

// ─────────────────────────────────────────────────────────────────
// executeGenerateChart — v0.1.63 (Data Analyst Mode)
//
// Validates that the model gave us something that at least *resembles* a
// Vega-Lite spec (object with `mark` + `encoding`), then hands the spec
// + id back to AIPanel via the tool-result body. AIPanel reads the body,
// instantiates a VegaChartRenderer, and renders the chart inline.
//
// We DO NOT execute Vega-Lite here — JSON-schema validation and JS
// runtime errors live in the browser shell. Doing only the shallow
// shape check keeps the executor cheap and avoids dragging vega-lite
// schemas into the C++ side.
// ─────────────────────────────────────────────────────────────────
ToolResult executeGenerateChart(const ToolCall &call) {
    const QJsonValue specVal = call.args.value("spec");
    if (!specVal.isObject()) {
        return makeError(call, "io_error",
                         "generate_chart: 'spec' must be an object (Vega-Lite v5 spec).");
    }
    const QJsonObject spec = specVal.toObject();
    // Cheap shape sanity — every meaningful Vega-Lite spec has `mark`
    // and `encoding`. We accept `layer`/`hconcat`/`vconcat`/`repeat`
    // composite specs as a relaxation (they don't have top-level
    // `mark`/`encoding`). This guards against the model emitting a bare
    // JSON-string-of-spec or a placeholder {}.
    const bool hasMark = spec.contains("mark");
    const bool hasEncoding = spec.contains("encoding");
    const bool isComposite =
        spec.contains("layer") || spec.contains("hconcat") ||
        spec.contains("vconcat") || spec.contains("repeat") ||
        spec.contains("facet") || spec.contains("concat");
    if (!isComposite && (!hasMark || !hasEncoding)) {
        return makeError(call, "io_error",
                         "generate_chart: spec is missing required keys "
                         "'mark' and/or 'encoding'. Use the Vega-Lite v5 "
                         "format: {\"mark\":\"bar\",\"encoding\":{...},"
                         "\"data\":{\"values\":[...]}}.");
    }
    // Hard cap to keep the chat transcript JSON sane. 100 KB is more
    // than enough for any reasonable chart spec; data.values for huge
    // datasets should be sliced server-side before being handed off.
    const QByteArray specBytes =
        QJsonDocument(spec).toJson(QJsonDocument::Compact);
    if (specBytes.size() > 100 * 1024) {
        return makeError(call, "too_large",
                         "generate_chart: spec exceeds 100 KB. Aggregate or "
                         "sample the data before generating the chart.");
    }

    QString chartId = call.args.value("id").toString().trimmed();
    if (chartId.isEmpty()) {
        // Synthesize one — VegaChartRenderer also synthesizes when
        // none is supplied, but echoing one here lets the model
        // reference it in subsequent turns even before the widget
        // exists.
        chartId = QStringLiteral("chart-") +
                  QString::number(QDateTime::currentMSecsSinceEpoch(), 16);
    }
    const QString title = call.args.value("title").toString();

    QJsonObject result;
    result["chart_id"]  = chartId;
    result["spec"]      = spec;
    if (!title.isEmpty()) result["title"] = title;
    result["bytes"]     = specBytes.size();
    result["message"]   = QStringLiteral(
        "Chart rendered inline. Reference it as chart_id=") + chartId +
        QStringLiteral(" in follow-ups.");
    return makeSuccess(call, result);
}

} // anonymous namespace

ToolResult execute(const ToolCall &call, const QString &workspaceRoot) {
    // hardening: empty tool name produced an "Unknown tool: ''" before — guard early.
    if (call.name.isEmpty()) {
        return makeError(call, "io_error", "Empty tool name.");
    }
    if (call.name == "read_file")       return executeReadFile(call, workspaceRoot);
    if (call.name == "list_dir")        return executeListDir(call, workspaceRoot);
    if (call.name == "write_file")      return executeWriteFile(call, workspaceRoot);
    if (call.name == "search")          return executeSearch(call, workspaceRoot);
    if (call.name == "apply_diff")      return executeApplyDiff(call, workspaceRoot);
    if (call.name == "query_sql")       return executeQuerySql(call);
    if (call.name == "csv_query")       return executeCsvQuery(call, workspaceRoot);
    if (call.name == "generate_chart")  return executeGenerateChart(call);
    // Read-only git inspection tools — branch / status / diff / log /
    // show / branch_list. Implemented in src/git_tools.cpp; the QProcess
    // verb list is hard-coded so there's no path through these to mutate
    // the repo.
    if (call.name == "git_status")      return GitTools::executeGitStatus(call, workspaceRoot);
    if (call.name == "git_diff")        return GitTools::executeGitDiff(call, workspaceRoot);
    if (call.name == "git_log")         return GitTools::executeGitLog(call, workspaceRoot);
    if (call.name == "git_branch_list") return GitTools::executeGitBranchList(call, workspaceRoot);
    if (call.name == "git_show")        return GitTools::executeGitShow(call, workspaceRoot);
    return makeError(call, "io_error",
                     "Unknown tool: '" + call.name + "'");
}

} // namespace AiTools
