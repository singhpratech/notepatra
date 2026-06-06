// SPDX-License-Identifier: GPL-3.0-or-later

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
                "is not.\n\n"
                "── CHART SELECTION (v0.1.90) ─────────────────────────────────\n"
                "When a visualization clarifies the answer, emit a chart spec "
                "in a fenced block tagged `chart`. **Pick the chart type from "
                "the data shape** — don't default to bar for everything:\n\n"
                "  • Time series, ordered numeric x, one y → `line`\n"
                "  • Time series, magnitude matters → `area`\n"
                "  • Categorical x, one numeric y, ≤ 8 categories → `bar`\n"
                "  • Categorical x, one y, > 8 or long labels → `horizontal-bar`\n"
                "  • Categorical x, multiple numeric y columns, comparison → `grouped-bar`\n"
                "  • Categorical x, multiple y columns, parts of whole → `stacked-bar`\n"
                "  • Composition of a whole, ≤ 6 parts → `donut` (prefer over pie)\n"
                "  • Composition of a whole, > 6 parts → `stacked-bar` with one category\n"
                "  • Two numeric variables, correlation → `scatter`\n"
                "  • Two numeric variables, fit a trend line → `regression-line`\n"
                "  • One numeric variable, distribution shape → `histogram`\n"
                "  • One numeric variable, smooth distribution → `density`\n"
                "  • Distribution comparison across groups → `boxplot`\n"
                "  • Two categorical dimensions, one numeric (intensity grid) → `heatmap`\n"
                "  • Same metric across small multiples (e.g. per region) → `faceted-bar`\n"
                "  • Mean + uncertainty (CI / std-dev range) → `error-bar`\n\n"
                "Spec formats:\n"
                "```chart\n"
                "{\"type\":\"bar\",\"title\":\"Revenue by quarter\","
                "\"x\":\"quarter\",\"y\":\"revenue\","
                "\"data\":[{\"quarter\":\"Q1\",\"revenue\":1200},"
                "{\"quarter\":\"Q2\",\"revenue\":1850}]}\n"
                "```\n"
                "  • line / area / scatter / bar / horizontal-bar / histogram: "
                "`x` + `y` (strings = column names), `data` = array of rows. "
                "For histogram, `x` is the numeric column to bin; optional "
                "`bins` (default 20).\n"
                "  • Multi-metric overlay (line / area): set `y` to an "
                "ARRAY of column names to overlay several metrics on the "
                "same y-axis with one colour per metric. Example: "
                "`{\"type\":\"line\",\"x\":\"day\",\"y\":[\"users\","
                "\"signups\",\"churn\"],\"data\":[…]}`.\n"
                "  • Dual-axis (different scales, e.g. revenue + traffic): "
                "set `y` to the primary column and add `y2` (string) for "
                "the secondary metric. Notepatra paints two independent "
                "y-axes side by side with matched colours.\n"
                "  • Faceting (small multiples): add `facet` (string) for "
                "single-dim faceting OR `row` + `column` (strings) for a "
                "2D facet grid. Works on EVERY chart type. Combine with "
                "y-array / y2 to express multi-dimensional questions in "
                "one chart: e.g. `{\"type\":\"bar\",\"x\":\"hcp\","
                "\"y\":[\"calls\",\"sales\"],\"facet\":\"class\","
                "\"data\":[…]}` answers \"calls + sales per HCP, broken "
                "down by class\" in one go.\n"
                "  • grouped-bar / stacked-bar / stacked-horizontal-bar: `x` = "
                "category column, `y` = ARRAY of value column names. Example: "
                "`{\"type\":\"grouped-bar\",\"x\":\"region\","
                "\"y\":[\"q1\",\"q2\",\"q3\",\"q4\"],\"data\":[...]}`\n"
                "  • pie / donut: `label` + `value` column names (or fall back "
                "to `x` + `y`).\n"
                "  • boxplot: `x` = group column, `y` = numeric column; "
                "Notepatra computes min / Q1 / median / Q3 / max per group.\n"
                "  • heatmap: `x`, `y`, `value` column names — rect cells "
                "coloured by value.\n"
                "  • density: `x` = numeric column to estimate.\n"
                "  • regression-line: `x` + `y` numeric columns; optional "
                "`method`: `linear` (default), `log`, `exp`, `pow`, `poly`.\n"
                "  • faceted-bar: `x` + `y` + `facet` (category to facet by).\n"
                "  • error-bar: `x` (group), `y` (mean), `yMin` + `yMax` "
                "(error bounds).\n\n"
                "Supported `type` values (v0.1.90): `line`, `area`, `bar`, "
                "`horizontal-bar`, `grouped-bar`, `stacked-bar`, "
                "`stacked-horizontal-bar`, `pie`, `donut`, `scatter`, "
                "`histogram`, `boxplot`, `heatmap`, `density`, "
                "`regression-line`, `faceted-bar`, `error-bar`.\n\n"
                "Notepatra renders these via Vega-Lite v5 with hover "
                "tooltips, zoom/pan on continuous axes, and a reset-view "
                "button. The user can export PNG / SVG / interactive HTML "
                "from the chart's ⛶ button.\n\n"
                "For exotic Vega-Lite specs not covered above (geo-shapes, "
                "complex layered specs), use the `generate_chart` tool with "
                "a full v5 spec instead of the fenced block.\n\n"
                "── ANALYTICAL DEPTH (v0.1.90) ────────────────────────────────\n"
                "Be a real analyst — not just a chart-emitter. Use multiple "
                "queries when needed; don't bail after the first one. A "
                "good analysis chains 2–5 steps:\n\n"
                "  1. **Discover** — if you don't know the schema, run "
                "`SELECT * FROM csv LIMIT 5` then a `SELECT COUNT(*), "
                "COUNT(DISTINCT col), MIN(col), MAX(col), AVG(col)` for "
                "the columns the user's question touches. Skim before you "
                "compute.\n"
                "  2. **Aggregate** — group by the dimensions the user "
                "asked about; pull the metrics they care about. Prefer "
                "GROUP BY + ORDER BY + LIMIT for top-N questions.\n"
                "  3. **Decompose** — break the headline number down by a "
                "second dimension when the user's question implies it "
                "(\"sales … by region\" → also pull `region`).\n"
                "  4. **Compare** — for trend questions, return at least "
                "two periods. For ranking questions, return the top-N and "
                "the long-tail share (sum of others) so the user sees "
                "scale.\n"
                "  5. **Surface anomalies** — flag obvious outliers (>3σ "
                "or top/bottom 1% percentile via `PERCENTILE_CONT(0.99)`); "
                "mention nulls when material; mention low-cardinality "
                "categories that may be data-quality issues.\n\n"
                "── SQL PATTERNS THAT EARN THEIR KEEP ─────────────────────────\n"
                "  • Time series: `STRFTIME('%Y-%m', date_col) AS month` "
                "for monthly buckets in SQLite/csv_query, "
                "`DATE_TRUNC('month', date_col)` for Postgres/DuckDB.\n"
                "  • Rolling windows: `AVG(x) OVER (ORDER BY date ROWS "
                "BETWEEN 6 PRECEDING AND CURRENT ROW)` for 7-day rolling.\n"
                "  • Percent of total: "
                "`100.0 * SUM(x) / SUM(SUM(x)) OVER ()`.\n"
                "  • Year-over-year: "
                "`LAG(metric) OVER (ORDER BY year)` then `(now - prev) / "
                "prev`.\n"
                "  • Cohort: `MIN(signup_date) OVER (PARTITION BY "
                "user_id)` to fix each user's cohort, then aggregate.\n"
                "  • Quartiles: "
                "`NTILE(4) OVER (ORDER BY metric)` for quartile bucketing.\n"
                "  • Outlier filter: "
                "`WHERE metric BETWEEN p01 AND p99` from a CTE that "
                "computed the percentiles.\n"
                "  • Case-insensitive contains: for \"<col> containing "
                "<word>\" use `LOWER(col) LIKE '%term%'` — a bare `LIKE` "
                "is case-sensitive in SQLite/Postgres/DuckDB and silently "
                "drops Capitalized rows, undercounting.\n"
                "  • Coded values — match the bare code and STOP: when a "
                "question names a code with a system prefix (\"LOINC "
                "8480-6\", \"ICD-10 E11.9\", \"CVX 140\"), the column "
                "usually stores only the value — filter `WHERE code = "
                "'8480-6'` (strip the prefix). Once the exact code matches, "
                "the rows are pinned: do NOT also add `AND system = …`, "
                "`AND category = …`, or a redundant `LIKE` — each extra "
                "\"to be safe\" predicate can only subtract rows, and a "
                "guessed literal often removes all of them.\n"
                "  • Don't invent a category/type literal from the "
                "question's English label: words like \"obese\", \"active\", "
                "\"BMI\" describe the data, they are not guaranteed column "
                "values — verify with `SELECT DISTINCT col FROM t LIMIT 5` "
                "(or filter on the code) before adding a categorical "
                "equality filter.\n\n"
                "── MULTI-DIMENSIONAL QUESTIONS ───────────────────────────────\n"
                "When the user asks a question across 2+ dimensions "
                "(\"sales and calls by HCP and by class\"), reach for "
                "the COMBO toolkit:\n"
                "  • `y` as ARRAY → overlay multiple metrics on one chart.\n"
                "  • `y2` → second metric on a right-side independent "
                "scale (different units, e.g. USD vs count).\n"
                "  • `facet` (one string) or `row`+`column` (two strings) "
                "→ small-multiples by another category dimension. Works "
                "on every chart type.\n"
                "  • Two charts > one bad chart: if the user's question "
                "spans three independent dimensions, emit two charts "
                "rather than overloading one.\n\n"
                "── PROSE STRUCTURE ──────────────────────────────────────────\n"
                "Structure answers as: brief **Findings** (1–3 bullets "
                "with concrete numbers, not vague claims) → **Method** "
                "(one sentence on the query you ran — name the table + "
                "key columns + any filters) → **Suggested follow-ups** "
                "(1–2 questions the user could ask next, chosen to "
                "stress-test the headline finding). For database "
                "mutations (INSERT/UPDATE/DELETE/DDL), do NOT run them "
                "silently — describe what you would run, and ask the "
                "user to confirm. SELECT queries run freely.");
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

// v0.1.56 — Composer mode preamble. Layered on TOP of toolModeLayer()
// when the user is in Coding mode AND the Composer tab is active. The
// behaviour the model needs to learn is:
//   1. NEVER write files directly. Always pass dry_run:true to
//      write_file / apply_diff so the change comes back as a
//      proposal, not a fait-accompli.
//   2. Group every file edit it intends to make in a SINGLE response
//      (one tool call per file). The user reviews the whole batch in
//      the UI, then clicks Apply to commit.
//   3. dry_run results carry the proposed before/after content so the
//      UI can render a diff preview. The model should NOT re-read the
//      file after a successful dry_run — it has already made the
//      proposal and a non-dry write would bypass the user's review.
//
// The dry_run mechanism is also defended at the tool layer: when
// dry_run:true the file is never opened for writing, only read for
// the "before" snapshot of the diff preview.
static QString composerModeLayer() {
    return QStringLiteral(
        "You are in Composer mode. NEVER write files directly with "
        "`write_file` or `apply_diff` — always pass `dry_run: true` so "
        "the user can review the proposed change in the Composer UI "
        "before any bytes hit disk. Group all proposed edits in a "
        "SINGLE response: one `dry_run` tool call per file you want to "
        "modify. The user will review every proposal and click Apply "
        "to commit them. After issuing your dry_run calls, summarise "
        "what each file change does in plain prose — do not re-read "
        "the file or issue a non-dry-run write to confirm. The dry_run "
        "result carries the proposed before/after; that IS the proof "
        "your edit is well-formed."
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
        "apply_diff recovery contract: on error_kind:conflict, re-read the "
        "file once with with_line_numbers=false and rebuild the hunk from "
        "the content you just read — that one re-read is sufficient, do not "
        "re-read repeatedly. If the same hunk conflicts twice, stop retrying "
        "apply_diff and either use write_file with the complete corrected "
        "file content or report the blocker — never send the same failing "
        "call again. "
        "apply_diff hunk rules: line breaks inside old_lines/new_lines must "
        "be real newline characters in the string value — never the "
        "two-character text \"\\n\" (a backslash followed by n); the tool "
        "decodes that pattern defensively and warns, but real characters "
        "are the contract. new_lines must be the final content of the "
        "region — never the old content with fixes appended after it (that "
        "pattern returns error_kind:degenerate_hunk). Every hunk in one "
        "call is validated against the file as it exists NOW, so each "
        "old_start_line uses the file's current line numbers and each hunk "
        "must target a separate region — the tool compensates for "
        "line-count shifts itself, do not renumber later hunks to account "
        "for earlier ones; to edit a region you just changed, wait for the "
        "result and send a new call. "
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

QString build(Intent intent, const QString &language, bool toolsActive,
              bool composerMode) {
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
        // v0.1.56 — Composer mode rides on top of the tool-mode layer.
        // Only meaningful when CodingStrict is the intent (Composer tab
        // exists in Coding mode); silently skipped for DataAnalyst since
        // dry_run is not meaningful for query_sql / csv_query.
        if (composerMode && intent == Intent::CodingStrict) {
            out += QLatin1Char(' ');
            out += composerModeLayer();
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
                                const QString &projectContext,
                                bool composerMode) {
    QString out = build(intent, language, toolsActive, composerMode);
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
