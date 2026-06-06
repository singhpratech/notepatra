// ═══════════════════════════════════════════════════════════════════════
// test_ai_dataanalyst — v0.1.43 regression suite for Data Analyst Mode.
//
// Pure-logic tests (no GUI loop required for most). For the chart-rendering
// portion we instantiate a QApplication with the offscreen platform so
// QChartView can construct.
//
// Coverage:
//   - Intent::DataAnalyst classification with the dataMode flag
//   - system prompt build (data tool layer when toolsActive=true)
//   - shouldAttachWorkspace returns false for DataAnalyst
//   - buildWithProjectContext prepends instruction text and caps at 8KB
//   - readDataAnalystInstructions reads .notepatra/data-analyst.md
//   - CSV schema detection (delimiter sniff, header probe, type inference)
//   - CSV preview text generation + byte cap
//   - CsvAnalyst::looksLikeCsv recognizes csv/tsv/psv/tab
//   - CsvAnalyst::loadIntoSqlite round-trips + COUNT
//   - DbConnections::obfuscatePassword round-trips + empty
//   - DbConnections::Record toJson/fromJson round-trip
//   - DbConnections::driverNeedsNetwork
//   - DbConnections::runQuery on SQLite (SELECT) + non-SELECT rejection
//   - AiTools::modelCapableOfDataAnalysis allowlist + size threshold
//   - AiTools::availableTools includes query_sql + csv_query
//   - ChartRender::looksLikeChartSpec, renderFromSpec valid + malformed
// ═══════════════════════════════════════════════════════════════════════

#include "ai_systemprompt.h"
#include "ai_tools.h"
#include "chartrender.h"
#include "csvanalyst.h"
#include "dbconnections.h"

#include <QApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSqlDatabase>
#include <QSqlQuery>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QTextStream>

#include <cstdio>
#include <cstdlib>

static int g_passed = 0;
static int g_failed = 0;

#define EXPECT_TRUE(label, cond) \
    do { \
        if (cond) { ++g_passed; std::printf("  ✓ %s\n", label); } \
        else { ++g_failed; std::printf("  ✗ %s\n", label); } \
    } while (0)

#define EXPECT_EQ(label, expected, actual) \
    do { \
        if ((expected) == (actual)) { ++g_passed; std::printf("  ✓ %s\n", label); } \
        else { ++g_failed; std::printf("  ✗ %s — expected != actual\n", label); } \
    } while (0)

#define EXPECT_CONTAINS(label, hay, needle) \
    do { \
        if ((hay).contains(needle)) { ++g_passed; std::printf("  ✓ %s\n", label); } \
        else { ++g_failed; std::printf("  ✗ %s — '%s' NOT found in output\n", label, needle); } \
    } while (0)

// ───────────────────────────────────────────────────────────────────────
// Section 1 — Intent + system prompt
// ───────────────────────────────────────────────────────────────────────
static void testIntent() {
    using AiSystemPrompt::classifyIntent;
    using AiSystemPrompt::Intent;

    std::printf("\n=== Intent classification ===\n");
    EXPECT_TRUE("custom + coding=false + data=false → Chat",
                classifyIntent("custom", false, false) == Intent::Chat);
    EXPECT_TRUE("custom + coding=true → CodingStrict (wins over data)",
                classifyIntent("custom", true, true) == Intent::CodingStrict);
    EXPECT_TRUE("custom + coding=false + data=true → DataAnalyst",
                classifyIntent("custom", false, true) == Intent::DataAnalyst);
    EXPECT_TRUE("explain + data=true → DataAnalyst (data absorbs action)",
                classifyIntent("explain", false, true) == Intent::DataAnalyst);
    EXPECT_TRUE("explain + coding=false + data=false → Explain",
                classifyIntent("explain", false, false) == Intent::Explain);
}

static void testSystemPrompt() {
    using AiSystemPrompt::Intent;
    using AiSystemPrompt::build;

    std::printf("\n=== DataAnalyst system prompt ===\n");
    QString sp = build(Intent::DataAnalyst, "python", false);
    EXPECT_CONTAINS("data analyst phrasing", sp, "data analyst");
    EXPECT_CONTAINS("chart fenced block", sp, "```chart");
    // v0.1.76 — system prompt now lists all 12 chart types as one
    // backtick-quoted block instead of a comma-separated sentence.
    EXPECT_CONTAINS("legacy line type still documented",      sp, "`line`");
    EXPECT_CONTAINS("legacy bar type still documented",       sp, "`bar`");
    EXPECT_CONTAINS("legacy pie type still documented",       sp, "`pie`");
    EXPECT_CONTAINS("legacy scatter type still documented",   sp, "`scatter`");
    EXPECT_CONTAINS("new histogram type documented (v0.1.76)",sp, "`histogram`");
    EXPECT_CONTAINS("new boxplot type documented (v0.1.76)",  sp, "`boxplot`");

    // toolsActive=true should switch to the data tool preamble.
    QString spTools = build(Intent::DataAnalyst, "", true);
    EXPECT_CONTAINS("query_sql in tool layer", spTools, "query_sql");
    EXPECT_CONTAINS("csv_query in tool layer",  spTools, "csv_query");

    // Other intents should NOT contain data-analyst phrasing.
    QString chat = build(Intent::Chat, "", false);
    EXPECT_TRUE("Chat prompt does not mention csv_query",
                !chat.contains("csv_query"));
}

// v0.1.112 — apply_diff escalation-ladder contract in the tool-mode layer.
// The system prompt documents the TOOL's recovery contract (true for every
// model): conflict → one re-read then rebuild; same hunk conflicts twice →
// stop retrying (write_file or report); hunk strings carry real newlines;
// new_lines are final content (degenerate_hunk otherwise). Every
// instruction must carry its boundary, and the prompt must stay
// model-agnostic — no model names in ANY built variant.
static void testToolModePromptContract() {
    using AiSystemPrompt::Intent;
    using AiSystemPrompt::build;

    std::printf("\n=== tool-mode prompt: apply_diff escalation ladder ===\n");
    const QString sp = build(Intent::CodingStrict, "", true);

    // Conflict recovery — instruction + boundary pair.
    EXPECT_CONTAINS("documents error_kind:conflict recovery", sp,
                    "error_kind:conflict");
    EXPECT_CONTAINS("conflict → re-read once (with_line_numbers=false)", sp,
                    "re-read the file once with with_line_numbers=false");
    EXPECT_CONTAINS("  boundary: one re-read is sufficient", sp,
                    "that one re-read is sufficient");
    EXPECT_CONTAINS("  boundary: do not re-read repeatedly", sp,
                    "do not re-read repeatedly");

    // Same-hunk-twice escalation — instruction + boundary pair.
    EXPECT_CONTAINS("same hunk conflicts twice → stop retrying", sp,
                    "If the same hunk conflicts twice, stop retrying");
    EXPECT_CONTAINS("  escape hatch: write_file with complete content", sp,
                    "write_file with the complete corrected");
    EXPECT_CONTAINS("  boundary: never resend the same failing call", sp,
                    "never send the same failing call again");

    // Hunk content rules — real newlines, final content, degenerate_hunk.
    EXPECT_CONTAINS("hunk rule: real newline characters", sp,
                    "real newline characters");
    EXPECT_CONTAINS("  boundary: never the two-character backslash-n", sp,
                    "never the two-character text");
    EXPECT_CONTAINS("hunk rule: new_lines are the final content", sp,
                    "new_lines must be the final content");
    EXPECT_CONTAINS("  boundary: never old content plus appended fixes", sp,
                    "never the old content with fixes appended");
    EXPECT_CONTAINS("degenerate_hunk error documented", sp,
                    "degenerate_hunk");
    EXPECT_CONTAINS("hunk rule: current line numbers, tool compensates", sp,
                    "do not renumber later hunks");

    // Model-agnostic: no model-name strings in any built prompt variant.
    const char *modelNames[] = {
        "qwen", "llama", "gemma", "mistral", "claude", "gpt-3", "gpt-4",
        "gpt-5", "gpt-oss", "deepseek", "granite", "hermes", "phi-3",
        "command r", "gemini", "codestral"
    };
    const Intent intents[] = { Intent::Chat, Intent::Explain,
                               Intent::Transform, Intent::CodingStrict,
                               Intent::DataAnalyst };
    bool clean = true;
    QString offender;
    for (Intent it : intents) {
        for (int toolsOn = 0; toolsOn <= 1; ++toolsOn) {
            for (int composer = 0; composer <= 1; ++composer) {
                const QString p = build(it, "", toolsOn != 0,
                                        composer != 0).toLower();
                for (const char *mn : modelNames) {
                    if (p.contains(QString::fromLatin1(mn))) {
                        clean = false;
                        offender = QString::fromLatin1(mn);
                    }
                }
            }
        }
    }
    if (!clean)
        std::printf("    offending model name: %s\n", qPrintable(offender));
    EXPECT_TRUE("no model-name strings in ANY built prompt variant", clean);
}

static void testShouldAttachWorkspace() {
    using AiSystemPrompt::Intent;
    using AiSystemPrompt::shouldAttachWorkspace;

    std::printf("\n=== shouldAttachWorkspace for DataAnalyst ===\n");
    EXPECT_TRUE("DataAnalyst rejects workspace attach (always false)",
                !shouldAttachWorkspace(Intent::DataAnalyst, "", "give me top 5 customers"));
    EXPECT_TRUE("DataAnalyst rejects with selection too",
                !shouldAttachWorkspace(Intent::DataAnalyst, "SELECT *", ""));
}

static void testBuildWithProjectContext() {
    using AiSystemPrompt::Intent;
    using AiSystemPrompt::buildWithProjectContext;

    std::printf("\n=== buildWithProjectContext ===\n");
    QString proj = "Treat null in column 'amount' as 0. Always join orders to customers on customer_id.";
    QString sp = buildWithProjectContext(Intent::DataAnalyst, "", false, proj);
    EXPECT_CONTAINS("project ctx prepended", sp, "join orders to customers");
    EXPECT_CONTAINS("project ctx delimiter", sp, "Project data context");

    QString spChat = buildWithProjectContext(Intent::Chat, "", false, proj);
    EXPECT_TRUE("project ctx ignored for non-DataAnalyst intents",
                !spChat.contains("Project data context"));

    // 8KB cap enforcement
    QString huge(20000, QChar('x'));
    QString spHuge = buildWithProjectContext(Intent::DataAnalyst, "", false, huge);
    EXPECT_TRUE("8KB cap on project ctx (truncated marker present)",
                spHuge.contains("[...truncated]"));
    // The base DataAnalyst prompt grew +~1KB in v0.1.108 (filter-correctness
    // idioms: case-insensitive LIKE, bare-code matching, no guessed category
    // literals). The 8KB project-context cap is still the contract under test;
    // total = base + 8KB cap + headers, so the bound is 19KB with margin.
    EXPECT_TRUE("8KB project-ctx cap keeps total prompt under 19KB (safe margin)",
                spHuge.toUtf8().size() < 19 * 1024);
}

static void testReadInstructions(const QTemporaryDir &fixDir) {
    using AiSystemPrompt::readDataAnalystInstructions;

    std::printf("\n=== readDataAnalystInstructions ===\n");
    EXPECT_TRUE("empty workspace returns empty",
                readDataAnalystInstructions("").isEmpty());

    // Create the file in the fixture.
    const QString notepatra = fixDir.filePath(".notepatra");
    QDir().mkpath(notepatra);
    QFile mdFile(notepatra + "/data-analyst.md");
    if (!mdFile.open(QIODevice::WriteOnly)) {
        std::printf("FAIL: could not write data-analyst.md fixture\n");
        std::exit(7);
    }
    mdFile.write("Don't average column 'rate' — it's a percentage.\n");
    mdFile.close();
    QString got = readDataAnalystInstructions(fixDir.path());
    EXPECT_CONTAINS("reads .notepatra/data-analyst.md", got, "percentage");

    // Missing file under a real workspace returns empty.
    QTemporaryDir empty;
    EXPECT_TRUE("missing file returns empty",
                readDataAnalystInstructions(empty.path()).isEmpty());
}

// ───────────────────────────────────────────────────────────────────────
// Section 2 — CSV analyst
// ───────────────────────────────────────────────────────────────────────

static QString writeCsv(const QTemporaryDir &dir, const QString &name, const QString &body) {
    const QString path = dir.filePath(name);
    QFile f(path);
    if (!f.open(QIODevice::WriteOnly)) return QString();
    f.write(body.toUtf8());
    f.close();
    return path;
}

static void testCsvSchema(const QTemporaryDir &dir) {
    std::printf("\n=== CsvAnalyst::detectSchema ===\n");

    // Comma-delimited with header
    QString p1 = writeCsv(dir, "comma.csv",
        "name,age,salary,active\n"
        "Alice,32,75000.5,true\n"
        "Bob,28,52000,false\n"
        "Cleo,45,98000.25,true\n");
    auto s1 = CsvAnalyst::detectSchema(p1);
    EXPECT_TRUE("comma delimiter detected", s1.delimiter == ',');
    EXPECT_TRUE("header detected", s1.hasHeader);
    EXPECT_EQ("4 columns",  4, s1.columns.size());
    EXPECT_TRUE("age inferred Integer",
                s1.columns.size() >= 2 && s1.columns[1].type == CsvAnalyst::ColumnType::Integer);
    EXPECT_TRUE("salary inferred Real",
                s1.columns.size() >= 3 && s1.columns[2].type == CsvAnalyst::ColumnType::Real);
    EXPECT_TRUE("active inferred Boolean",
                s1.columns.size() >= 4 && s1.columns[3].type == CsvAnalyst::ColumnType::Boolean);

    // Tab-delimited
    QString p2 = writeCsv(dir, "tabs.tsv",
        "qtr\trev\n"
        "Q1\t1200\n"
        "Q2\t1850\n");
    auto s2 = CsvAnalyst::detectSchema(p2);
    EXPECT_TRUE("tab delimiter detected", s2.delimiter == '\t');

    // No header
    QString p3 = writeCsv(dir, "noheader.csv",
        "1,2,3\n4,5,6\n7,8,9\n");
    auto s3 = CsvAnalyst::detectSchema(p3);
    EXPECT_TRUE("no header detected", !s3.hasHeader);
    EXPECT_TRUE("col_1 generated", !s3.columns.isEmpty() && s3.columns[0].name == "col_1");

    // Date column
    QString p4 = writeCsv(dir, "dates.csv",
        "event,date\n"
        "Launch,2026-04-01\n"
        "Sunset,2026-04-15\n");
    auto s4 = CsvAnalyst::detectSchema(p4);
    EXPECT_TRUE("date column inferred Date",
                s4.columns.size() == 2 && s4.columns[1].type == CsvAnalyst::ColumnType::Date);
}

static void testCsvPreview(const QTemporaryDir &dir) {
    std::printf("\n=== CsvAnalyst::buildPreviewText ===\n");
    QString p = writeCsv(dir, "preview.csv",
        "id,name,score\n"
        "1,alice,90\n"
        "2,bob,85\n"
        "3,carol,92\n"
        "4,dave,71\n"
        "5,eve,88\n"
        "6,frank,95\n");
    QString preview = CsvAnalyst::buildPreviewText(p, 3, 2, 4096);
    EXPECT_CONTAINS("preview includes filename", preview, "preview.csv");
    EXPECT_CONTAINS("preview includes Schema header", preview, "Schema:");
    EXPECT_CONTAINS("preview includes columns", preview, "score");
    EXPECT_CONTAINS("preview shows head row", preview, "alice");
    EXPECT_CONTAINS("preview shows tail row", preview, "frank");

    // Byte cap
    QString huge(2000, QChar('x'));
    QString hugeRow;
    for (int i = 0; i < 200; ++i) hugeRow += QString::number(i) + "," + huge + "\n";
    QString hp = writeCsv(dir, "huge.csv", "a,b\n" + hugeRow);
    QString preview2 = CsvAnalyst::buildPreviewText(hp, 5, 5, 1024);
    EXPECT_TRUE("preview respects byte cap (~1KB)",
                preview2.toUtf8().size() < 1500);
}

static void testCsvLooks() {
    std::printf("\n=== CsvAnalyst::looksLikeCsv ===\n");
    EXPECT_TRUE(".csv yes",   CsvAnalyst::looksLikeCsv("data/foo.csv"));
    EXPECT_TRUE(".CSV yes",   CsvAnalyst::looksLikeCsv("DATA.CSV"));
    EXPECT_TRUE(".tsv yes",   CsvAnalyst::looksLikeCsv("foo.tsv"));
    EXPECT_TRUE(".py no",   !CsvAnalyst::looksLikeCsv("script.py"));
    EXPECT_TRUE("no ext no",!CsvAnalyst::looksLikeCsv("README"));
}

static void testCsvLoadSqlite(const QTemporaryDir &dir) {
    std::printf("\n=== CsvAnalyst::loadIntoSqlite ===\n");
    QString p = writeCsv(dir, "load.csv",
        "id,score\n"
        "1,10\n"
        "2,20\n"
        "3,30\n");
    QString cname, err;
    bool truncated = false;
    bool ok = CsvAnalyst::loadIntoSqlite(p, 1000, &cname, &truncated, &err);
    EXPECT_TRUE("load succeeded", ok);
    if (ok) {
        QSqlDatabase db = QSqlDatabase::database(cname);
        QSqlQuery q(db);
        q.exec("SELECT COUNT(*) FROM csv");
        if (q.next()) {
            EXPECT_EQ("row count = 3", 3, q.value(0).toInt());
        } else {
            EXPECT_TRUE("count query returned a row", false);
        }
        q.exec("SELECT SUM(score) FROM csv");
        if (q.next()) {
            EXPECT_EQ("sum(score) = 60", 60, q.value(0).toInt());
        } else {
            EXPECT_TRUE("sum query returned a row", false);
        }
        QSqlDatabase::removeDatabase(cname);
    }
}

// ───────────────────────────────────────────────────────────────────────
// Section 3 — DbConnections
// ───────────────────────────────────────────────────────────────────────

static void testObfuscation() {
    std::printf("\n=== DbConnections::obfuscatePassword ===\n");
    using DbConnections::obfuscatePassword;
    EXPECT_EQ("empty stays empty", QString(), obfuscatePassword(""));
    QString plain = "hunter2!";
    QString once  = obfuscatePassword(plain);
    EXPECT_TRUE("once != plain", once != plain);
    QString twice = obfuscatePassword(once);
    EXPECT_EQ("round-trip restores plain", plain, twice);

    QString odd = "p@ssw0rd with spaces & symbols!";
    EXPECT_EQ("symmetric on weird chars", odd, obfuscatePassword(obfuscatePassword(odd)));
}

static void testRecordRoundTrip() {
    std::printf("\n=== DbConnections::Record toJson / fromJson ===\n");
    DbConnections::Record r;
    r.name = "prod-db";
    r.driver = "QPSQL";
    r.host = "db.example.com";
    r.port = 5432;
    r.database = "shop";
    r.username = "reader";
    r.password = "s3cr3t!";
    r.options = "connect_timeout=10";

    QJsonObject obj = r.toJson();
    EXPECT_TRUE("password obscured at rest (not literal plaintext)",
                obj.value("password").toString() != QStringLiteral("s3cr3t!"));
    DbConnections::Record back = DbConnections::Record::fromJson(obj);
    EXPECT_EQ("name round-trips",     r.name, back.name);
    EXPECT_EQ("driver round-trips",   r.driver, back.driver);
    EXPECT_EQ("port round-trips",     r.port, back.port);
    EXPECT_EQ("password round-trips", r.password, back.password);
}

static void testDriverPredicates() {
    std::printf("\n=== DbConnections::driverNeedsNetwork ===\n");
    EXPECT_TRUE("QSQLITE is local-only",  !DbConnections::driverNeedsNetwork("QSQLITE"));
    EXPECT_TRUE("QPSQL needs network",     DbConnections::driverNeedsNetwork("QPSQL"));
    EXPECT_TRUE("QMYSQL needs network",    DbConnections::driverNeedsNetwork("QMYSQL"));
}

static void testRunQuery(const QTemporaryDir &dir) {
    std::printf("\n=== DbConnections::runQuery (SQLite) ===\n");
    if (!QSqlDatabase::drivers().contains("QSQLITE")) {
        std::printf("  ! QSQLITE driver missing — skipping runQuery test\n");
        return;
    }
    const QString dbPath = dir.filePath("test.db");
    {
        QSqlDatabase db = QSqlDatabase::addDatabase("QSQLITE", "fixture");
        db.setDatabaseName(dbPath);
        if (!db.open()) {
            std::printf("  ! cannot open SQLite — skipping\n");
            QSqlDatabase::removeDatabase("fixture");
            return;
        }
        QSqlQuery q(db);
        q.exec("CREATE TABLE t (id INTEGER, name TEXT)");
        q.exec("INSERT INTO t VALUES (1, 'one')");
        q.exec("INSERT INTO t VALUES (2, 'two')");
        QSqlDatabase::removeDatabase("fixture");
    }

    DbConnections::Record r;
    r.name = "fixture";
    r.driver = "QSQLITE";
    r.database = dbPath;

    DbConnections::QueryResult qr = DbConnections::runQuery(r, "SELECT id, name FROM t ORDER BY id", 100, false);
    EXPECT_TRUE("SELECT succeeded", qr.ok);
    EXPECT_EQ("rows returned = 2", 2, qr.rowsReturned);
    EXPECT_EQ("columns count = 2", 2, qr.columns.size());

    // Mutation rejected without confirm
    DbConnections::QueryResult qm = DbConnections::runQuery(r, "DELETE FROM t", 100, false);
    EXPECT_TRUE("DELETE rejected without confirm", !qm.ok);
    EXPECT_EQ("error_kind=non_select", QString("non_select"), qm.errorKind);

    // Mutation with confirm: should succeed
    DbConnections::QueryResult qmOk = DbConnections::runQuery(r, "DELETE FROM t WHERE id=1", 100, true);
    EXPECT_TRUE("DELETE allowed with confirm=true", qmOk.ok);
}

// ───────────────────────────────────────────────────────────────────────
// Section 4 — AiTools model gating + tool registry
// ───────────────────────────────────────────────────────────────────────

static void testModelGating() {
    std::printf("\n=== AiTools::modelCapableOfDataAnalysis ===\n");
    using AiTools::modelCapableOfDataAnalysis;

    EXPECT_TRUE("Claude 4 Opus passes",         modelCapableOfDataAnalysis("claude-4-opus"));
    EXPECT_TRUE("Claude 3.5 Sonnet passes",     modelCapableOfDataAnalysis("anthropic/claude-3.5-sonnet"));
    EXPECT_TRUE("GPT-4o passes",                modelCapableOfDataAnalysis("gpt-4o"));
    EXPECT_TRUE("GPT-5 passes",                 modelCapableOfDataAnalysis("gpt-5"));
    EXPECT_TRUE("Gemini 2.5 Pro passes",        modelCapableOfDataAnalysis("gemini-2.5-pro"));
    EXPECT_TRUE("DeepSeek-V3 passes",           modelCapableOfDataAnalysis("deepseek-v3"));
    EXPECT_TRUE("qwen2.5-coder:14b passes",     modelCapableOfDataAnalysis("qwen2.5-coder:14b"));
    EXPECT_TRUE("llama3.1:8b passes",           modelCapableOfDataAnalysis("llama3.1:8b"));
    EXPECT_TRUE("mistral-large passes",         modelCapableOfDataAnalysis("mistral-large"));

    EXPECT_TRUE("empty fails",                  !modelCapableOfDataAnalysis(""));
    EXPECT_TRUE("llama3.2:1b fails (under 7B)", !modelCapableOfDataAnalysis("llama3.2:1b"));
    EXPECT_TRUE("qwen3:4b fails (under 7B)",    !modelCapableOfDataAnalysis("qwen3:4b"));
    EXPECT_TRUE("phi-3-mini fails (not in family)",
                                                !modelCapableOfDataAnalysis("phi-3-mini"));
    EXPECT_TRUE("random model fails",           !modelCapableOfDataAnalysis("nonexistent-model:0.1b"));
}

static void testToolRegistry() {
    std::printf("\n=== AiTools::availableTools registry ===\n");
    QJsonArray tools = AiTools::availableTools();
    QStringList names;
    for (const QJsonValue &v : tools) names.append(v.toObject().value("function").toObject().value("name").toString());
    EXPECT_TRUE("query_sql registered",  names.contains("query_sql"));
    EXPECT_TRUE("csv_query registered",  names.contains("csv_query"));
    EXPECT_TRUE("read_file still registered (regression)", names.contains("read_file"));
    EXPECT_TRUE("apply_diff still registered (regression)", names.contains("apply_diff"));
}

// ───────────────────────────────────────────────────────────────────────
// Section 5 — Chart renderer
// ───────────────────────────────────────────────────────────────────────

static void testChartRenderer() {
    std::printf("\n=== ChartRender::looksLikeChartSpec / renderFromSpec ===\n");

    QJsonObject ok;
    ok["type"] = "bar";
    ok["x"] = "category";
    ok["y"] = "value";
    QJsonArray data;
    data.append(QJsonObject{{"category", "A"}, {"value", 10}});
    data.append(QJsonObject{{"category", "B"}, {"value", 20}});
    ok["data"] = data;
    EXPECT_TRUE("valid bar spec recognized", ChartRender::looksLikeChartSpec(ok));

    QJsonObject missingType;
    missingType["data"] = data;
    EXPECT_TRUE("missing type rejected", !ChartRender::looksLikeChartSpec(missingType));

    QJsonObject badType = ok;
    // v0.1.76 added histogram + 7 more types; pick something truly absent.
    badType["type"] = "candlestick";
    EXPECT_TRUE("unsupported type rejected", !ChartRender::looksLikeChartSpec(badType));

    // renderFromSpec — needs a QApplication / offscreen platform.
    QString err;
    QWidget *w = ChartRender::renderFromSpec(QJsonDocument(ok).toJson(QJsonDocument::Compact),
                                             nullptr, &err);
    EXPECT_TRUE("bar render returns a widget", w != nullptr);
    if (w) delete w;

    QJsonObject pieSpec;
    pieSpec["type"] = "pie";
    pieSpec["label"] = "category";
    pieSpec["value"] = "value";
    pieSpec["data"] = data;
    QString err2;
    QWidget *w2 = ChartRender::renderFromSpec(QJsonDocument(pieSpec).toJson(), nullptr, &err2);
    EXPECT_TRUE("pie render returns a widget", w2 != nullptr);
    if (w2) delete w2;

    // Malformed JSON
    QString err3;
    QWidget *w3 = ChartRender::renderFromSpec("not json at all", nullptr, &err3);
    EXPECT_TRUE("malformed JSON returns nullptr", w3 == nullptr);
    EXPECT_TRUE("malformed JSON sets error", !err3.isEmpty());
}

// ───────────────────────────────────────────────────────────────────────

int main(int argc, char *argv[]) {
    qputenv("QT_QPA_PLATFORM", "offscreen");
    QApplication app(argc, argv);

    QTemporaryDir dir;
    if (!dir.isValid()) {
        std::printf("FAIL: temp dir creation\n");
        return 2;
    }

    testIntent();
    testSystemPrompt();
    testToolModePromptContract();
    testShouldAttachWorkspace();
    testBuildWithProjectContext();
    testReadInstructions(dir);
    testCsvSchema(dir);
    testCsvPreview(dir);
    testCsvLooks();
    testCsvLoadSqlite(dir);
    testObfuscation();
    testRecordRoundTrip();
    testDriverPredicates();
    testRunQuery(dir);
    testModelGating();
    testToolRegistry();
    testChartRenderer();

    std::printf("\n=== Summary ===\n  passed: %d\n  failed: %d\n", g_passed, g_failed);
    return g_failed == 0 ? 0 : 1;
}
