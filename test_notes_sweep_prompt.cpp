// Noter sweep-prompt regression test. Pure unit test — no QApplication
// is needed because NoterSweepPrompt is a pure-function module. We
// drive every parser robustness case the dialog will lean on (clean
// JSON, fenced JSON, trailing-comma JSON, missing-field JSON,
// completely-malformed reply) plus the HTML-to-plain-context helper.

#include "src/notes_sweep_prompt.h"

#include <QCoreApplication>
#include <QDebug>
#include <QRegularExpression>

#include <cstdio>

static int g_pass = 0, g_fail = 0;
#define EXPECT(label, cond) \
    do { if (cond) { ++g_pass; std::printf("  [PASS] %s\n", label); } \
         else      { ++g_fail; std::printf("  [FAIL] %s\n", label); } } while (0)

// ─────────────────────────────────────────────────────────────────────
// htmlToPlainContext
// ─────────────────────────────────────────────────────────────────────

static void test_html_to_plain() {
    std::printf("test_html_to_plain\n");

    const QString html =
        "<h1>Roadmap sync</h1>"
        "<p>Discussed the Q3 plan with the team.</p>"
        "<div class=\"b b-dec\">Ship Noter in v0.1.94</div>"
        "<div class=\"b b-act\" data-owner=\"@alice\" data-due=\"2026-05-25T17:00:00Z\">"
        "Wire export menu</div>"
        "<div class=\"b b-q\">What about offline reminders?</div>"
        "<div class=\"b b-risk\">QtWebEngine size on Lite</div>";

    const QString plain = NoterSweepPrompt::htmlToPlainContext(html);
    EXPECT("plain has heading text",       plain.contains("Roadmap sync"));
    EXPECT("plain marks decision",         plain.contains("[DECISION]"));
    EXPECT("plain marks action+owner+due", plain.contains("[ACTION]") &&
                                            plain.contains("@alice") &&
                                            plain.contains("2026-05-25"));
    EXPECT("plain marks question",         plain.contains("[QUESTION]"));
    EXPECT("plain marks risk",             plain.contains("[RISK]"));
    EXPECT("plain stripped all tags",     !plain.contains("<") && !plain.contains(">"));
}

// ─────────────────────────────────────────────────────────────────────
// build()
// ─────────────────────────────────────────────────────────────────────

static void test_build_prompt() {
    std::printf("test_build_prompt\n");

    const QString html =
        "<h1>Roadmap sync</h1><div class=\"b b-dec\">Ship Noter</div>";

    const QString prompt = NoterSweepPrompt::build(html, "Roadmap sync");
    EXPECT("prompt is non-empty", !prompt.isEmpty());
    EXPECT("prompt has schema",   prompt.contains("\"decisions\""));
    EXPECT("prompt has NOW iso",  prompt.contains("NOW is 20"));   // "NOW is 20XX-..."
    EXPECT("prompt asks for a summary field", prompt.contains("\"summary\""));
    EXPECT("prompt gives a concrete due example", prompt.contains("T10:00"));
    EXPECT("prompt has meeting title", prompt.contains("Roadmap sync"));
    EXPECT("prompt has split sentinel (U+001F)", prompt.contains(QChar(0x1F)));
}

// v0.1.112 — splitPrompt: build() glues system+user with a U+001F
// sentinel; the Extract flow must split them into generate()'s prompt and
// system args. The sentinel is consumed by the split — it must NEVER
// reach the request builder (pre-v0.1.112 the combined string went out
// verbatim as the prompt, control byte included).
static void test_split_prompt_sentinel() {
    std::printf("test_split_prompt_sentinel\n");

    const QString combined = NoterSweepPrompt::build(
        "<h1>Sync</h1><div class=\"b b-act\">Ship the build</div>",
        "Roadmap sync");
    const NoterSweepPrompt::PromptParts parts =
        NoterSweepPrompt::splitPrompt(combined);

    EXPECT("split: system half non-empty", !parts.system.isEmpty());
    EXPECT("split: user half non-empty",   !parts.user.isEmpty());
    EXPECT("split: system half carries the schema",
           parts.system.contains("\"decisions\""));
    EXPECT("split: system rules NOT in the user half",
           !parts.user.contains("\"decisions\""));
    EXPECT("split: user half carries the meeting header",
           parts.user.contains("MEETING: Roadmap sync"));
    EXPECT("split: user half carries the note body",
           parts.user.contains("Ship the build"));
    EXPECT("split: no U+001F in system half",
           !parts.system.contains(QChar(0x1F)));
    EXPECT("split: no U+001F in user half",
           !parts.user.contains(QChar(0x1F)));

    // No-sentinel input degrades to "all user" — nothing dropped.
    const NoterSweepPrompt::PromptParts bare =
        NoterSweepPrompt::splitPrompt(QStringLiteral("just a prompt"));
    EXPECT("split: no-sentinel -> system empty", bare.system.isEmpty());
    EXPECT("split: no-sentinel -> user keeps the text",
           bare.user == QStringLiteral("just a prompt"));

    // Defensive: multiple sentinels are all scrubbed, never forwarded.
    const NoterSweepPrompt::PromptParts multi = NoterSweepPrompt::splitPrompt(
        QStringLiteral("sys") + QChar(0x1F) + QStringLiteral("usr") +
        QChar(0x1F) + QStringLiteral("tail"));
    EXPECT("split: multi-sentinel system clean",
           !multi.system.contains(QChar(0x1F)));
    EXPECT("split: multi-sentinel user clean",
           !multi.user.contains(QChar(0x1F)));
    EXPECT("split: multi-sentinel keeps all user text",
           multi.user.contains("usr") && multi.user.contains("tail"));
}

// v0.1.112 — honest ctx-budget truncation. build() must (a) leave short
// notes byte-identical to the pre-truncation prompt, (b) cut long notes
// at a line boundary AND tell the MODEL the input is partial via the
// TRUNCATED user-turn header, (c) report exact word counts through
// BuildInfo so the dialog notice + persisted caption can tell the USER.
static void test_build_truncation() {
    std::printf("test_build_truncation\n");

    // (a) Short note: untouched, info says full coverage.
    const QString shortHtml =
        "<h1>Sync</h1><div class=\"b b-act\">Ship the build</div>";
    NoterSweepPrompt::BuildInfo shortInfo;
    const QString with2   = NoterSweepPrompt::build(shortHtml, "Sync");
    const QString with3   = NoterSweepPrompt::build(shortHtml, "Sync", &shortInfo);
    EXPECT("short note: not truncated", !shortInfo.truncated);
    EXPECT("short note: wordsUsed == wordsTotal",
           shortInfo.wordsUsed == shortInfo.wordsTotal &&
               shortInfo.wordsTotal > 0);
    // The NOW timestamp lives in the system half — compare the user
    // halves, which must be byte-identical between the 2-arg and 3-arg
    // calls (the defaulted param changes nothing for short notes).
    EXPECT("short note: user half byte-identical with/without info",
           NoterSweepPrompt::splitPrompt(with2).user ==
               NoterSweepPrompt::splitPrompt(with3).user);
    EXPECT("short note: keeps the original NOTE BODY header",
           with3.contains("NOTE BODY (plain text, with bracketed tags"));
    EXPECT("short note: no TRUNCATED header", !with3.contains("TRUNCATED"));

    // (b)+(c) Long note: ~3000 words across many lines.
    QString longHtml = "<h1>Big meeting</h1>";
    for (int i = 0; i < 300; ++i) {
        QString line = "<p>";
        for (int w = 0; w < 10; ++w)
            line += QString("word%1n%2 ").arg(i).arg(w);
        line += "</p>";
        longHtml += line;
    }
    NoterSweepPrompt::BuildInfo longInfo;
    const QString longPrompt =
        NoterSweepPrompt::build(longHtml, "Big meeting", &longInfo);
    EXPECT("long note: truncated", longInfo.truncated);
    EXPECT("long note: wordsUsed < wordsTotal",
           longInfo.wordsUsed > 0 &&
               longInfo.wordsUsed < longInfo.wordsTotal);
    // 300 lines × 10 words + the 2-word heading.
    EXPECT("long note: total = 3002 words", longInfo.wordsTotal == 3002);

    const NoterSweepPrompt::PromptParts parts =
        NoterSweepPrompt::splitPrompt(longPrompt);
    EXPECT("long note: TRUNCATED header tells the model",
           parts.user.contains("NOTE BODY (TRUNCATED — this is only the FIRST " +
                               QString::number(longInfo.wordsUsed) + " of " +
                               QString::number(longInfo.wordsTotal) +
                               " words of a longer note):"));
    EXPECT("long note: sentinel contract unchanged",
           longPrompt.contains(QChar(0x1F)));

    // Cut on a LINE boundary: the kept body's last line must be one of
    // the source lines, complete (ends with its 10th word, "…n9").
    const int bodyStart = parts.user.indexOf("---\n") + 4;
    const int bodyEnd   = parts.user.indexOf("\n---", bodyStart);
    const QString keptBody = parts.user.mid(bodyStart, bodyEnd - bodyStart);
    const QStringList keptLines = keptBody.split('\n', Qt::SkipEmptyParts);
    EXPECT("long note: body retained at least one line", !keptLines.isEmpty());
    EXPECT("long note: cut on a line boundary (last kept line complete)",
           !keptLines.isEmpty() &&
               keptLines.last().trimmed().endsWith("n9"));
    EXPECT("long note: kept word count matches the report",
           keptBody.split(QRegularExpression("\\s+"),
                          Qt::SkipEmptyParts).size() == longInfo.wordsUsed);
    EXPECT("long note: later content NOT in the prompt",
           !parts.user.contains("word299n9"));
}

// v0.1.112 — normalizeForMatch moved from the dialog's file-static into
// NoterSweepPrompt (the extract-apply done-state carry shares it). The
// dialog's fuzzy already-scheduled dedup keys on this exact behavior.
static void test_normalize_for_match() {
    std::printf("test_normalize_for_match\n");

    EXPECT("lowercases + strips punctuation",
           NoterSweepPrompt::normalizeForMatch("Ship the BUILD!!") ==
               "ship the build");
    EXPECT("drops @owner handles",
           NoterSweepPrompt::normalizeForMatch("Ship the build @alice") ==
               "ship the build");
    EXPECT("collapses whitespace",
           NoterSweepPrompt::normalizeForMatch("  ship   the   build ") ==
               "ship the build");
    EXPECT("reworded-with-handle equals the bare form",
           NoterSweepPrompt::normalizeForMatch("Email the vendor  @bob.") ==
               NoterSweepPrompt::normalizeForMatch("email the vendor"));
    EXPECT("different tasks stay different",
           NoterSweepPrompt::normalizeForMatch("ship the build") !=
               NoterSweepPrompt::normalizeForMatch("sink the build"));
}

// v0.1.112 — classify(): an unparseable reply is an ERROR outcome, not
// "the model found nothing". Pre-v0.1.112 both rendered the same 'no
// actionable items' info box, hiding real failures.
static void test_classify_outcomes() {
    std::printf("test_classify_outcomes\n");

    const auto bad = NoterSweepPrompt::parse(
        QStringLiteral("Sorry, I can't help with that."));
    EXPECT("classify: unparseable -> ParseError (NOT Empty)",
           NoterSweepPrompt::classify(bad) ==
               NoterSweepPrompt::ExtractOutcome::ParseError);
    EXPECT("classify: ParseError keeps raw reply for the details box",
           bad.rawResponse.contains("Sorry"));

    const auto empty = NoterSweepPrompt::parse(QStringLiteral(
        R"({"decisions":[],"actions":[],"questions":[],"risks":[]})"));
    EXPECT("classify: valid zero-item reply -> Empty",
           NoterSweepPrompt::classify(empty) ==
               NoterSweepPrompt::ExtractOutcome::Empty);

    const auto items = NoterSweepPrompt::parse(QStringLiteral(
        R"({"actions":[{"text":"Ship it"}]})"));
    EXPECT("classify: reply with an action -> Items",
           NoterSweepPrompt::classify(items) ==
               NoterSweepPrompt::ExtractOutcome::Items);

    // Truncated JSON (mid-stream drop) must classify as ParseError too.
    const auto truncated = NoterSweepPrompt::parse(QStringLiteral(
        R"({"actions":[{"text":"Ship)"));
    EXPECT("classify: truncated JSON -> ParseError",
           NoterSweepPrompt::classify(truncated) ==
               NoterSweepPrompt::ExtractOutcome::ParseError);
}

// v0.1.98 — summary field + LOCAL wall-clock due handling. The old parser
// force-set no-offset times to UTC, which shifted "10am tomorrow" by the
// user's offset (showed/fired at the wrong hour). Local must stay local; a
// Z/offset instant must convert to the right local wall-clock.
static void test_parse_summary_and_local_date() {
    std::printf("test_parse_summary_and_local_date\n");
    const QString reply = R"({
        "summary": "Team agreed to ship the build and follow up with Priya.",
        "actions": [{"text": "Ship the build", "owner": "@prateek", "due": "2026-05-25T10:00"}],
        "decisions": [], "questions": [], "risks": []
    })";
    auto r = NoterSweepPrompt::parse(reply);
    EXPECT("summary parsed", r.summary.startsWith("Team agreed"));
    EXPECT("local-date: 1 action", r.actions.size() == 1);
    const QDateTime due = r.actions.value(0).dueAt;
    EXPECT("local-date: due valid", due.isValid());
    EXPECT("local-date: kept LOCAL (not forced UTC)", due.timeSpec() == Qt::LocalTime);
    EXPECT("local-date: wall-clock hour stays 10", due.time().hour() == 10);
    EXPECT("local-date: day is 25", due.date().day() == 25);

    const QString replyZ =
        R"({"actions":[{"text":"x","due":"2026-05-25T08:00:00Z"}],)"
        R"("decisions":[],"questions":[],"risks":[]})";
    auto rz = NoterSweepPrompt::parse(replyZ);
    EXPECT("z-date: 1 action", rz.actions.size() == 1);
    const QDateTime expected =
        QDateTime::fromString(QStringLiteral("2026-05-25T08:00:00Z"), Qt::ISODate)
            .toLocalTime();
    EXPECT("z-date: converted to correct local instant",
           rz.actions.value(0).dueAt == expected);
}

// ─────────────────────────────────────────────────────────────────────
// parse() — five reply shapes
// ─────────────────────────────────────────────────────────────────────

static void test_parse_clean_json() {
    std::printf("test_parse_clean_json\n");
    const QString reply = R"({
        "decisions": [{"text": "Ship Noter in v0.1.94"}],
        "actions":   [{"text": "Wire export menu", "owner": "@alice", "due": "2026-05-25T17:00:00Z"}],
        "questions": [{"text": "Offline reminders?"}],
        "risks":     [{"text": "QtWebEngine size"}]
    })";
    auto r = NoterSweepPrompt::parse(reply);
    EXPECT("clean: no error",       r.errorMessage.isEmpty());
    EXPECT("clean: 1 decision",     r.decisions.size() == 1);
    EXPECT("clean: 1 action",       r.actions.size() == 1);
    EXPECT("clean: action owner",   r.actions.value(0).owner == "@alice");
    EXPECT("clean: action due ok",  r.actions.value(0).dueAt.isValid());
    EXPECT("clean: 1 question",     r.questions.size() == 1);
    EXPECT("clean: 1 risk",         r.risks.size() == 1);
}

static void test_parse_fenced_json() {
    std::printf("test_parse_fenced_json\n");
    const QString reply =
        "```json\n{\n"
        "  \"decisions\": [{\"text\": \"D1\"}],\n"
        "  \"actions\": [], \"questions\": [], \"risks\": []\n"
        "}\n```";
    auto r = NoterSweepPrompt::parse(reply);
    EXPECT("fenced: no error",     r.errorMessage.isEmpty());
    EXPECT("fenced: 1 decision",   r.decisions.size() == 1);
    EXPECT("fenced: decision text", r.decisions.value(0).text == "D1");
}

static void test_parse_trailing_commas() {
    std::printf("test_parse_trailing_commas\n");
    const QString reply = R"({
        "decisions": [{"text": "D1",},],
        "actions": [],
        "questions": [],
        "risks": [],
    })";
    auto r = NoterSweepPrompt::parse(reply);
    EXPECT("trailing-comma: no error", r.errorMessage.isEmpty());
    EXPECT("trailing-comma: 1 decision", r.decisions.size() == 1);
}

static void test_parse_missing_fields() {
    std::printf("test_parse_missing_fields\n");
    // Missing risks, missing owner+due on action — parser must default
    // to empties rather than rejecting the whole reply.
    const QString reply = R"({
        "decisions": [{"text": "D1"}],
        "actions":   [{"text": "Do X"}]
    })";
    auto r = NoterSweepPrompt::parse(reply);
    EXPECT("missing: no error",          r.errorMessage.isEmpty());
    EXPECT("missing: 1 decision",        r.decisions.size() == 1);
    EXPECT("missing: 1 action",          r.actions.size() == 1);
    EXPECT("missing: action owner empty", r.actions.value(0).owner.isEmpty());
    EXPECT("missing: action due invalid", !r.actions.value(0).dueAt.isValid());
    EXPECT("missing: 0 questions",       r.questions.isEmpty());
    EXPECT("missing: 0 risks",           r.risks.isEmpty());
}

static void test_parse_malformed() {
    std::printf("test_parse_malformed\n");
    const QString reply = "Sure! Here's the answer: not JSON at all.";
    auto r = NoterSweepPrompt::parse(reply);
    EXPECT("malformed: error reported",  !r.errorMessage.isEmpty());
    EXPECT("malformed: raw preserved",   r.rawResponse == reply);
    EXPECT("malformed: 0 decisions",     r.decisions.isEmpty());
    EXPECT("malformed: 0 actions",       r.actions.isEmpty());
}

static void test_parse_with_think_block() {
    std::printf("test_parse_with_think_block\n");
    // Qwen3-style replies sometimes leak <think>...</think> despite
    // the system instruction. Our parser must drop them.
    const QString reply =
        "<think>The user wants me to extract... let me think.</think>\n"
        "{\"decisions\":[{\"text\":\"D1\"}], \"actions\":[], "
        "\"questions\":[], \"risks\":[]}";
    auto r = NoterSweepPrompt::parse(reply);
    EXPECT("think-block: no error",   r.errorMessage.isEmpty());
    EXPECT("think-block: 1 decision", r.decisions.size() == 1);
}

int main(int argc, char *argv[]) {
    QCoreApplication app(argc, argv);

    test_html_to_plain();
    test_build_prompt();
    test_parse_clean_json();
    test_parse_fenced_json();
    test_parse_trailing_commas();
    test_parse_missing_fields();
    test_parse_malformed();
    test_parse_with_think_block();
    test_parse_summary_and_local_date();
    test_split_prompt_sentinel();
    test_classify_outcomes();
    test_build_truncation();
    test_normalize_for_match();

    std::printf("\n[%d passed, %d failed]\n", g_pass, g_fail);
    return g_fail == 0 ? 0 : 1;
}
