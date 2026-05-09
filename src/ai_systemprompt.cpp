#include "ai_systemprompt.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QRegularExpression>

namespace AiSystemPrompt {

Intent classifyIntent(const QString &action, bool codingMode, bool dataMode) {
    // Coding Mode always wins. The user has explicitly said "I want code,
    // nothing else" -- the action they picked is just the prompt template,
    // not the output style.
    if (codingMode) return Intent::CodingStrict;

    // Data Analyst Mode is the next-strongest signal — when on, the user
    // is exploring data (CSV / DB), not writing code. We enter DataAnalyst
    // intent for ALL actions in this mode (it absorbs the action context).
    if (dataMode) return Intent::DataAnalyst;

    if (action == QLatin1String("explain") ||
        action == QLatin1String("bugs")    ||
        action == QLatin1String("docs")) {
        return Intent::Explain;
    }
    if (action == QLatin1String("refactor")  ||
        action == QLatin1String("optimize")  ||
        action == QLatin1String("tests")     ||
        action == QLatin1String("comment")   ||
        action == QLatin1String("translate")) {
        return Intent::Transform;
    }
    // "custom" with Coding Mode OFF, or any unknown action: treat as Chat.
    return Intent::Chat;
}

static QString identityLayer() {
    return QStringLiteral(
        "You are Notepatra's AI assistant, embedded in a code editor. "
        "You help the user understand and edit their code.");
}

// The anti-tool-call layer is the load-bearing fix for tool-calling
// fine-tuned models (Qwen3 / Qwen3.5 / Hermes-3 / Llama 3.1+ Instruct,
// Mistral Large, Command R, GLM-4, GPT-OSS). Their training data is
// saturated with tool/function-call output formats, so when they see
// anything that looks like an agent frame (e.g. our workspace-context
// header) they emit JSON instead of chatting.
//
// We are explicit about three things:
//   1. There are NO tools in this environment.
//   2. The expected output is plain text or code blocks (markdown fences).
//   3. The forbidden output is the {"command":..., "output":...} shape
//      itself -- naming it concretely is more effective than a generic
//      "be conversational" because tool-calling models recognise the
//      shape they were trained to produce.
//
// For non-tool-calling models (Llama 3.2, Gemma 2, Phi-3.5, Claude, GPT-4)
// this layer is roughly 30 tokens of redundant instruction. Harmless --
// they ignore it because they were already going to chat in plain text.
static QString antiToolCallLayer() {
    return QStringLiteral(
        "This is a chat interface with no executable tools. "
        "Respond in plain natural language, optionally with markdown code "
        "blocks for code. Do not produce function calls, tool calls, or "
        "structured agent output. Do not wrap your response in JSON like "
        "{\"command\": ..., \"output\": ...} or {\"name\": ..., "
        "\"arguments\": ...}. If you would normally call a tool, instead "
        "describe what you would do in plain language.");
}

static QString modeLayer(Intent intent) {
    switch (intent) {
        case Intent::Chat:
            return QStringLiteral(
                "Be friendly and concise. When the user asks about code, "
                "explain it clearly; when they chat, chat back naturally.");

        case Intent::Explain:
            return QStringLiteral(
                "Explain clearly and concisely. Reference specific lines, "
                "function names, or variables when it helps the explanation. "
                "Use markdown code blocks for any code you reference.");

        case Intent::Transform:
            return QStringLiteral(
                "When you output code, preserve the user's indentation style "
                "(tabs vs spaces) and quote style. A brief one-or-two-sentence "
                "explanation before or after the code is fine. Use markdown "
                "code blocks for the code itself.");

        case Intent::CodingStrict:
            // Byte-identical to the legacy Coding Mode prompt body. Preserved
            // verbatim so existing Coding Mode users see no behaviour change
            // from this refactor. The leading "code-editor agent" framing is
            // intentionally retained -- tool-calling models stay focused on
            // code output because the anti-tool-call layer above already
            // suppresses the JSON-tool-call drift.
            return QStringLiteral(
                "Return ONLY the modified source code. No explanations, no "
                "prose, no preambles like 'Here is the code'. Do NOT wrap "
                "the output in markdown code fences (```). Preserve the "
                "original indentation style (tabs vs spaces). Output must "
                "be directly pasteable into the file -- nothing else.");

        case Intent::DataAnalyst:
            // v0.1.43 — Data Analyst Mode. The model is a senior data analyst
            // with access to attached CSVs, configured SQL connections, and
            // a chart-rendering channel. Differs from Chat in that prose is
            // structured (Findings → Method → Suggested follow-ups) and the
            // model is expected to use the data tools instead of speculating
            // about contents.
            return QStringLiteral(
                "You are a senior data analyst. The user has attached CSV files "
                "and/or configured database connections. Use the data tools "
                "(`csv_query` for in-memory SQLite over attached CSVs, "
                "`query_sql` for configured connections) instead of guessing "
                "at file contents — querying is cheap and accurate; speculation "
                "is not. When a visualization clarifies the answer, emit a "
                "chart spec in a fenced block tagged `chart`:\n"
                "```chart\n"
                "{\"type\":\"bar\",\"title\":\"Revenue by quarter\","
                "\"x\":\"quarter\",\"y\":\"revenue\","
                "\"data\":[{\"quarter\":\"Q1\",\"revenue\":1200},"
                "{\"quarter\":\"Q2\",\"revenue\":1850}]}\n"
                "```\n"
                "Supported `type` values: line, bar, pie, scatter. Use only "
                "`x` + `y` for line/bar/scatter; for pie use `label` + `value`. "
                "The editor renders the chart inline. Structure prose answers "
                "as: brief Findings (1–3 bullets) → Method (one sentence on the "
                "query you ran) → Suggested follow-ups (1–2 questions the user "
                "could ask next). For database mutations (INSERT/UPDATE/DELETE/"
                "DDL), do NOT run them silently — describe what you would run, "
                "and ask the user to confirm. SELECT queries run freely.");
    }
    return QString(); // unreachable; quiets some compilers
}

static QString languageHint(const QString &language) {
    if (language.isEmpty()) return QString();
    return QStringLiteral("The user is working in ") + language +
           QStringLiteral(".");
}

// v0.1.35 — when tools are attached to the request, the anti-tool-call
// layer is replaced with this brief preamble that tells the model the
// tools ARE available and structured. Keeps the model focused on using
// `tool_calls` (the structured field) instead of typing JSON in plain
// text content.
//
// v0.1.43 — split into two flavours: code (the default) lists the file
// tools; data lists the data tools (query_sql, csv_query) plus read_file
// for opening attachment-related text. DataAnalyst intent is the only
// caller that gets the data flavour.
static QString toolModeLayerData() {
    return QStringLiteral(
        "You have data-analyst tools available. "
        "`csv_query(file_path, sql, max_rows?)` runs SQLite SQL against an "
        "attached CSV (the table is named `csv`; column names match the CSV "
        "header). `query_sql(connection_name, sql, max_rows?)` runs SQL "
        "against a saved DB connection — by default SELECT-only; for "
        "INSERT/UPDATE/DELETE/DDL ask the user first and pass "
        "confirm:true only after they say yes. `read_file(path, ...)` is "
        "available for opening text-shaped data attachments. "
        "Use the tool_calls structured field — never type JSON in your "
        "text response. Paths are workspace-relative; secret/credential "
        "paths (.ssh, .pem, .key, /etc/passwd, etc.) are refused. After "
        "querying, summarize findings concisely and emit a "
        "```chart fenced spec when a chart adds value."
    );
}

static QString toolModeLayer() {
    return QStringLiteral(
        "You have agentic workspace tools available. READ side: "
        "`read_file(path, offset?, limit?, with_line_numbers?)` to read a file "
        "(default output prefixes each line with '      N\\t' — "
        "use with_line_numbers=false when you plan to feed lines into "
        "apply_diff old_lines so you do NOT have to strip the prefix yourself), "
        "`list_dir(path)` to list directory entries, "
        "`search(pattern, path?, regex?, glob?, case_sensitive?, max_matches?)` "
        "to find a string/pattern across the workspace. WRITE side: "
        "`write_file(path, content, mode?)` to create or overwrite a file "
        "(mode: 'overwrite' default, 'create' fails if exists, 'append' adds to end), "
        "`apply_diff(path, hunks)` for line-level edits where each hunk has "
        "old_start_line + old_lines (expected current text — must match the "
        "file BYTE-FOR-BYTE; do NOT include the '      N\\t' prefix from "
        "read_file's default output) + new_lines (replacement). Atomic; if "
        "any hunk's old_lines doesn't match the file the call returns "
        "error_kind:conflict and nothing is written. The tool will fall "
        "back to a prefix-strip / whitespace-normalised match and report it "
        "in result.warnings — if you see those warnings, re-read the file "
        "with with_line_numbers=false to avoid them. "
        "Use the tool_calls structured field — never type JSON in your text "
        "response. Paths are workspace-relative; secret/credential paths "
        "(.ssh, .pem, .key, /etc/passwd, etc.) are refused. Prefer write_file "
        "for new files or full rewrites; prefer apply_diff for surgical edits "
        "to large existing files. After writing, the user's editor auto-opens "
        "or reloads the file — no need to ask them to refresh. "
        "GIT (read-only): `git_status()` returns branch + ahead/behind + "
        "staged/modified/untracked file lists; `git_diff(staged?, path?)` "
        "returns the unified diff (capped 32 KB); `git_log(max_count?, path?)` "
        "returns recent commits as {hash, author, date, subject} (default 20, "
        "max 100); `git_branch_list()` returns local branches with upstream "
        "tracking; `git_show(commit)` returns metadata + diff for a sha or "
        "HEAD~N (capped 32 KB). These tools are inspection-only — there is "
        "no add / commit / push / fetch / pull / reset / merge tool, so do "
        "not promise to perform mutations; if asked, describe what you would "
        "do and let the user run it themselves."
    );
}

QString build(Intent intent, const QString &language, bool toolsActive) {
    QString out;
    out.reserve(900);

    out += identityLayer();
    out += QLatin1Char(' ');
    if (toolsActive) {
        // Replace anti-tool-call with tool-mode preamble. DataAnalyst gets
        // a different preamble that advertises the data tools.
        if (intent == Intent::DataAnalyst) {
            out += toolModeLayerData();
        } else {
            out += toolModeLayer();
        }
    } else {
        out += antiToolCallLayer();
    }

    const QString mode = modeLayer(intent);
    if (!mode.isEmpty()) { out += QLatin1Char(' '); out += mode; }

    const QString lang = languageHint(language);
    if (!lang.isEmpty()) { out += QLatin1Char(' '); out += lang; }

    return out;
}

QString buildWithProjectContext(Intent intent,
                                const QString &language,
                                bool toolsActive,
                                const QString &projectContext) {
    QString out = build(intent, language, toolsActive);
    if (intent != Intent::DataAnalyst || projectContext.isEmpty()) return out;

    QString trimmed = projectContext.trimmed();
    if (trimmed.isEmpty()) return out;

    // Cap at 8KB so a runaway instruction file can't blow up the prompt.
    constexpr int kMaxBytes = 8 * 1024;
    if (trimmed.toUtf8().size() > kMaxBytes) {
        // Truncate by characters until under the byte cap. UTF-8 chars are
        // 1-4 bytes; this is safe-ish (won't split a multi-byte sequence
        // because we test the encoded size after each cut).
        int cut = trimmed.size();
        while (cut > 0 && trimmed.left(cut).toUtf8().size() > kMaxBytes - 32) {
            cut -= 64;
        }
        trimmed = trimmed.left(cut).trimmed() + QStringLiteral("\n[...truncated]");
    }

    out += QStringLiteral(
        "\n\n--- Project data context "
        "(.notepatra/data-analyst.md and .notepatra/data-analyst/*) ---\n"
        "These are the project-specific instructions, data dictionary, "
        "business rules, KPIs, and sample queries. Cite the file name "
        "(e.g. \"per business-rules.md\") when you rely on a fact from them.\n");
    out += trimmed;
    out += QStringLiteral("\n--- end project data context ---");
    return out;
}

// Heuristics for the casual-chat detector. Tuned conservatively -- when in
// doubt, attach workspace context (it's only noise for the chat case, and
// the anti-tool-call layer absorbs most of the damage anyway).
static bool looksLikeCasualChat(const QString &userText) {
    const QString t = userText.trimmed();
    if (t.isEmpty()) return true;        // empty input -- definitely not a code task
    if (t.length() < 30) return true;    // "hi", "thanks", "ok", "yes please" all under 30
    return false;
}

static bool hasCodeShape(const QString &userText) {
    // A handful of strong signals that this is a code-shaped question. Any
    // one match flips the classification toward "needs workspace context".
    static const QRegularExpression kCodeChars(QStringLiteral(
        R"([\{\}\(\)\[\];=]|=>|->|::|\bclass\b|\bfunction\b|\bdef\b|\bimport\b|\binclude\b)"
    ));
    if (kCodeChars.match(userText).hasMatch()) return true;

    // File-extension mentions ("fix the bug in main.cpp", "look at app.py")
    static const QRegularExpression kFileExt(QStringLiteral(
        R"(\.(?:py|js|ts|tsx|jsx|cpp|cxx|cc|c|h|hpp|hxx|java|cs|go|rs|rb|php|swift|kt|m|mm|sh|bat|ps1|json|xml|yml|yaml|toml|ini|conf|sql|html|htm|css|scss|md|txt|log)\b)"
    ));
    if (kFileExt.match(userText).hasMatch()) return true;

    return false;
}

static bool hasProjectKeyword(const QString &userText) {
    static const QRegularExpression kProj(QStringLiteral(
        R"(\b(?:project|workspace|codebase|directory|folder|repo|repository|files?|tree)\b)"),
        QRegularExpression::CaseInsensitiveOption);
    return kProj.match(userText).hasMatch();
}

// v0.1.55 — keyword set for "the user is asking about the file currently
// open in the editor". Triggers workspace attachment so the current file's
// content gets pinned into the prompt. Without this, asking "explain this
// file" left the model staring at a system prompt with no actual code.
static bool hasOpenFileKeyword(const QString &userText) {
    static const QRegularExpression kOpen(
        QStringLiteral(
            "\\b(?:"
            "this\\s+(?:file|code|script|module|class|function|method|component|page|view|model|fn|func)"
            "|(?:the|current|open|active)\\s+(?:file|code|script|buffer|editor|tab|module)"
            "|editor|buffer"
            ")\\b"),
        QRegularExpression::CaseInsensitiveOption);
    return kOpen.match(userText).hasMatch();
}

// v0.1.55 — multi-file Data Analyst context. Pre-v0.1.55 we loaded ONE file
// (`.notepatra/data-analyst.md`). Real analyst workflows need more: a data
// dictionary, business rules, KPI definitions, sample queries. The loader
// now reads:
//
//   .notepatra/data-analyst.md             (legacy single-file path; kept)
//   .notepatra/data-analyst/instructions.md (the persona / tone)
//   .notepatra/data-analyst/data-dictionary.md
//   .notepatra/data-analyst/business-rules.md
//   .notepatra/data-analyst/kpis.md
//   .notepatra/data-analyst/sample-queries.sql
//   .notepatra/data-analyst/*.md            (anything else — alphabetical)
//   .notepatra/data-analyst/*.sql           (extra sample queries)
//
// Each file is concatenated under a header comment so the model can cite
// its source ("per business-rules.md, …"). Hard cap: 64 KB total — past
// that we truncate with a marker so a runaway dump doesn't blow the
// model's context window.
QString readDataAnalystInstructions(const QString &workspaceRoot) {
    if (workspaceRoot.trimmed().isEmpty()) return QString();
    constexpr int kPerFileCap = 16 * 1024;
    constexpr int kTotalCap   = 64 * 1024;

    auto readCapped = [&](const QString &path) -> QString {
        QFile f(path);
        if (!f.open(QIODevice::ReadOnly)) return QString();
        const QByteArray bytes = f.read(kPerFileCap);
        f.close();
        return QString::fromUtf8(bytes).trimmed();
    };

    // Curated load order — most important first so if we hit kTotalCap
    // the highest-signal docs are already in.
    QStringList loaded;
    int totalBytes = 0;
    auto addFile = [&](const QString &absPath, const QString &shortName) {
        if (totalBytes >= kTotalCap) return;
        QFileInfo fi(absPath);
        if (!fi.exists() || !fi.isFile()) return;
        QString body = readCapped(absPath);
        if (body.isEmpty()) return;
        const QString block = QStringLiteral("--- %1 ---\n").arg(shortName) + body;
        if (totalBytes + block.size() > kTotalCap) {
            const int remaining = kTotalCap - totalBytes - 80;
            if (remaining > 200) {
                loaded.append(block.left(remaining)
                              + "\n\n[truncated — context cap reached]");
                totalBytes = kTotalCap;
            }
            return;
        }
        loaded.append(block);
        totalBytes += block.size();
    };

    const QDir root(workspaceRoot);

    // 1. Legacy single-file path. Stays first so existing projects with
    //    only this file see no behavior change.
    addFile(root.filePath(".notepatra/data-analyst.md"), "data-analyst.md");

    // 2. Curated section files in priority order.
    const QString subdir = root.filePath(".notepatra/data-analyst");
    static const QStringList kCurated = {
        "instructions.md",
        "data-dictionary.md",
        "business-rules.md",
        "kpis.md",
        "sample-queries.sql",
    };
    for (const QString &name : kCurated) {
        addFile(QDir(subdir).filePath(name), name);
    }

    // 3. Anything else in the subdir — alphabetical, .md / .sql / .yaml /
    //    .yml / .json / .txt only. Skip the curated names already loaded.
    QDir subdirAccess(subdir);
    if (subdirAccess.exists()) {
        QStringList namesAlready = kCurated;
        QStringList allowedExts = {"*.md", "*.sql", "*.yaml", "*.yml", "*.json", "*.txt"};
        QStringList extras = subdirAccess.entryList(allowedExts, QDir::Files, QDir::Name);
        for (const QString &fname : extras) {
            if (namesAlready.contains(fname, Qt::CaseInsensitive)) continue;
            addFile(subdirAccess.filePath(fname), fname);
        }
    }

    if (loaded.isEmpty()) return QString();
    return loaded.join("\n\n");
}

bool shouldAttachWorkspace(Intent intent,
                           const QString &selection,
                           const QString &userText) {
    // Coding Mode strict: code-only output. Workspace context just bloats
    // the prompt and risks the model echoing parts of it back.
    if (intent == Intent::CodingStrict) return false;

    // Data Analyst Mode: model should query data via tools, not get a tree
    // dump. The .notepatra/data-analyst.md project context (separate path)
    // already covers project-level instructions when the user wants them.
    if (intent == Intent::DataAnalyst) return false;

    // Explain / Transform with a selection: the selection IS the focus.
    // Other open files / project tree are noise that distracts the model.
    if (intent == Intent::Explain || intent == Intent::Transform) {
        return selection.isEmpty();
    }

    // Chat with a non-empty selection: same logic -- focus on the selection.
    if (intent == Intent::Chat && !selection.isEmpty()) return false;

    // Chat with no selection: distinguish casual greeting from project query.
    if (intent == Intent::Chat) {
        if (looksLikeCasualChat(userText)) return false;            // "hi", "thanks"
        if (hasOpenFileKeyword(userText))  return true;             // "explain this file"
        if (hasProjectKeyword(userText))   return true;             // "show me my files"
        if (hasCodeShape(userText))        return true;             // mentions code shapes
        // Mid-length conversational message with no code/project signals --
        // probably a generic question ("what's a closure?"). Don't attach.
        return false;
    }

    return true; // safe default for any future Intent values
}

} // namespace AiSystemPrompt
