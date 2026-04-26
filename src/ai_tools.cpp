#include "ai_tools.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QFileInfoList>
#include <QJsonDocument>
#include <QRegularExpression>
#include <QTextStream>

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
// Model allowlist — name-pattern check for Ollama models known to be
// tool-trained. Matches Ollama 2026 catalog (per multi-editor research):
// qwen3*, qwen2.5*, llama3.1+, llama3.2, llama3.3, llama4, mistral-nemo,
// mistral-large, command-r*, hermes3, granite3, gpt-oss, firefunction.
// Avoids: phi-3-mini, gemma 2 base, llama 3.2 1b without instruct,
// vision-only tags.
// ═══════════════════════════════════════════════════════════════════════

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
        QJsonObject params;
        params["type"] = "object";
        params["properties"] = props;
        params["required"] = QJsonArray{ "path" };
        tools.push_back(makeTool(
            "read_file",
            "Read the contents of a text file in the user's workspace. "
            "Returns the file's text with cat -n style line numbers. "
            "Paths must stay inside the workspace root; secret/credential "
            "paths (~/.ssh/, *.pem, *.key, /etc/passwd, etc.) are refused.",
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
        // and the model uses these as edit-anchors.
        if (line.size() > Limits::kReadMaxLineChars) {
            line.truncate(Limits::kReadMaxLineChars);
            line += "  ⟪truncated⟫";
        }
        out += QString("%1\t%2\n").arg(lineNo, 6).arg(line);
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

} // anonymous namespace

ToolResult execute(const ToolCall &call, const QString &workspaceRoot) {
    if (call.name == "read_file") return executeReadFile(call, workspaceRoot);
    if (call.name == "list_dir")  return executeListDir(call, workspaceRoot);
    return makeError(call, "io_error",
                     "Unknown tool: '" + call.name + "'");
}

} // namespace AiTools
