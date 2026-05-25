// Noter sweep-prompt regression test. Pure unit test — no QApplication
// is needed because NoterSweepPrompt is a pure-function module. We
// drive every parser robustness case the dialog will lean on (clean
// JSON, fenced JSON, trailing-comma JSON, missing-field JSON,
// completely-malformed reply) plus the HTML-to-plain-context helper.

#include "src/notes_sweep_prompt.h"

#include <QCoreApplication>
#include <QDebug>

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

    std::printf("\n[%d passed, %d failed]\n", g_pass, g_fail);
    return g_fail == 0 ? 0 : 1;
}
