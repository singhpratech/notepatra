// ═══════════════════════════════════════════════════════════════════════
// test_ai_chat_history — v0.1.67 regression suite for the three-vector
// chat-history refactor.
//
// Background:
//   v0.1.39 introduced per-workspace chat persistence backed by a single
//   m_messages vector. Modes (Chat / Coding / Data) shared that one
//   vector, so flipping from Coding into Data mode mixed the coding
//   conversation with the data-analyst system prompt — cross-mode
//   contamination that was visible in the AI's first reply after the
//   swap.
//
//   v0.1.67 replaces m_messages with three independent vectors:
//     - m_chatMessages    (Chat mode, default)
//     - m_codingMessages  (Coding Mode, agentic tools)
//     - m_dataMessages    (Data Analyst, csv_query / query_sql / chart)
//   selected by activeMessages() based on which mode button is checked.
//   The on-disk JSON format also bumps version: 1 → version: 2 to carry
//   the three vectors. v1 files migrate into the chat vector.
//
// What this test covers:
//   1. activeMessages() initially returns the chat vector.
//   2. Appending into chat mode lands in m_chatMessages.
//   3. Switching to coding makes activeMessages() empty (the coding
//      vector hasn't been written to yet).
//   4. Switching to data does the same.
//   5. Switching back to chat resurfaces the original messages — they
//      were never cross-contaminated.
//   6. clearChat() (via the path Reset wires to) empties ONLY the
//      active vector; the other two retain their content.
//   7. Save + reload round-trips all three vectors verbatim.
//   8. Old (v1) flat-array files migrate into the chat vector; coding
//      and data start empty.
//
// Headless / offscreen — QApplication runs with QT_QPA_PLATFORM=offscreen
// so AIPanel can construct its widget tree without a real display.
// We never call app.exec().
// ═══════════════════════════════════════════════════════════════════════

#include "aipanel.h"
#include "config.h"

#include <QAbstractButton>
#include <QApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>
#include <QVector>

#include <cstdio>
#include <cstdlib>

static int g_passed = 0;
static int g_failed = 0;

#define EXPECT_TRUE(label, cond) \
    do { \
        if (cond) { ++g_passed; std::printf("  [PASS] %s\n", label); } \
        else { ++g_failed; std::printf("  [FAIL] %s\n", label); } \
    } while (0)

#define EXPECT_EQ_INT(label, expected, actual) \
    do { \
        const int _e = (expected); const int _a = (actual); \
        if (_e == _a) { ++g_passed; std::printf("  [PASS] %s\n", label); } \
        else { ++g_failed; std::printf("  [FAIL] %s — expected=%d actual=%d\n", \
                                        label, _e, _a); } \
    } while (0)

#define EXPECT_EQ_STR(label, expected, actual) \
    do { \
        const QString _e = (expected); const QString _a = (actual); \
        if (_e == _a) { ++g_passed; std::printf("  [PASS] %s\n", label); } \
        else { ++g_failed; std::printf("  [FAIL] %s — expected='%s' actual='%s'\n", \
                                        label, _e.toUtf8().constData(), \
                                        _a.toUtf8().constData()); } \
    } while (0)

// ───────────────────────────────────────────────────────────────────────
// AIPanelTestAccess — friend that exposes the three private vectors and
// the mode buttons for tests, without leaking those internals into the
// production API. Declared as `friend class AIPanelTestAccess;` inside
// AIPanel (see src/aipanel.h).
// ───────────────────────────────────────────────────────────────────────
class AIPanelTestAccess {
public:
    static QVector<AIPanel::ChatMessage> &chat(AIPanel *p)   { return p->m_chatMessages; }
    static QVector<AIPanel::ChatMessage> &coding(AIPanel *p) { return p->m_codingMessages; }
    static QVector<AIPanel::ChatMessage> &data(AIPanel *p)   { return p->m_dataMessages; }

    static const QVector<AIPanel::ChatMessage> &active(const AIPanel *p) {
        return p->activeMessages();
    }
    static QVector<AIPanel::ChatMessage> &active(AIPanel *p) {
        return p->activeMessages();
    }

    // Switch mode by directly setting the underlying QAbstractButtons'
    // checked state. Triggers the toggled() signal, which fires
    // applyModeWithCancel and rebuilds the visible transcript — the
    // exact path the user takes when clicking the segmented buttons.
    // Returns false if any mode pointer is null (constructor not yet
    // run — shouldn't happen but defensive).
    static bool switchToChat(AIPanel *p)   { return setChecked(p->m_chatMode); }
    static bool switchToCoding(AIPanel *p) { return setChecked(p->m_codingMode); }
    static bool switchToData(AIPanel *p)   { return setChecked(p->m_dataMode); }

    // Drive the persistence path without spinning the event loop. The
    // public save/load helpers are private, but we're a friend.
    static void setHistoryPath(AIPanel *p, const QString &path) {
        p->m_chatHistoryPath = path;
        // Also point workspaceRoot at the directory so any future
        // updateChatHistoryPath calls don't blow it away.
        p->m_workspaceRoot = QFileInfo(path).absolutePath();
    }
    static void saveNow(AIPanel *p) { p->saveChatHistoryNow(); }
    static void load(AIPanel *p)    { p->loadChatHistory(); }
    static void clearChat(AIPanel *p) { p->clearChat(); }

    // For the "switch back to chat resurfaces" tests, we sometimes want
    // to check the active vector immediately after a switch; renderTranscript
    // is already inside applyMode, but we don't want test failures to
    // depend on the QWidget render path. The active vector is purely
    // pointer arithmetic via activeMessages(), so it's safe.
private:
    static bool setChecked(QAbstractButton *b) {
        if (!b) return false;
        if (!b->isChecked()) b->setChecked(true);
        return true;
    }
};

// ───────────────────────────────────────────────────────────────────────
// Helpers — build a ChatMessage with sensible defaults so the tests
// stay short.
// ───────────────────────────────────────────────────────────────────────
static AIPanel::ChatMessage userMsg(const QString &text) {
    AIPanel::ChatMessage m;
    m.role = AIPanel::ChatMessage::User;
    m.text = text;
    return m;
}
static AIPanel::ChatMessage asstMsg(const QString &text, const QString &model = "test-model") {
    AIPanel::ChatMessage m;
    m.role = AIPanel::ChatMessage::Assistant;
    m.text = text;
    m.model = model;
    m.promptTokens = 42;
    m.evalTokens = 17;
    m.elapsedMs = 1234;
    return m;
}

// ───────────────────────────────────────────────────────────────────────
// Section 1 — initial state + cross-mode partitioning
// ───────────────────────────────────────────────────────────────────────
static void testCrossModePartition() {
    std::printf("\n=== cross-mode partition ===\n");

    AIPanel panel;

    // Initial state: chat mode is the default; all three vectors are empty.
    EXPECT_TRUE("activeMessages() returns chat vector initially",
                &AIPanelTestAccess::active(&panel) == &AIPanelTestAccess::chat(&panel));
    EXPECT_EQ_INT("chat vector is empty at construction",
                  0, AIPanelTestAccess::chat(&panel).size());
    EXPECT_EQ_INT("coding vector is empty at construction",
                  0, AIPanelTestAccess::coding(&panel).size());
    EXPECT_EQ_INT("data vector is empty at construction",
                  0, AIPanelTestAccess::data(&panel).size());

    // 1. Append 3 messages while in Chat mode → they land in m_chatMessages.
    AIPanelTestAccess::active(&panel).append(userMsg("hello"));
    AIPanelTestAccess::active(&panel).append(asstMsg("hi back!"));
    AIPanelTestAccess::active(&panel).append(userMsg("how are you?"));
    EXPECT_EQ_INT("3 messages appended while in Chat mode",
                  3, AIPanelTestAccess::chat(&panel).size());
    EXPECT_EQ_INT("coding vector still empty after chat-mode appends",
                  0, AIPanelTestAccess::coding(&panel).size());
    EXPECT_EQ_INT("data vector still empty after chat-mode appends",
                  0, AIPanelTestAccess::data(&panel).size());

    // 2. Switch to Coding mode → activeMessages() is empty (the coding vector
    //    hasn't been written to). The chat vector retains its 3 messages.
    EXPECT_TRUE("Coding mode toggle accepted", AIPanelTestAccess::switchToCoding(&panel));
    EXPECT_TRUE("activeMessages() returns coding vector after switch",
                &AIPanelTestAccess::active(&panel) == &AIPanelTestAccess::coding(&panel));
    EXPECT_EQ_INT("activeMessages() is empty after switch to Coding",
                  0, AIPanelTestAccess::active(&panel).size());
    EXPECT_EQ_INT("chat vector still has 3 messages (untouched)",
                  3, AIPanelTestAccess::chat(&panel).size());

    // 3. Append 2 messages in Coding mode → they land in m_codingMessages.
    AIPanelTestAccess::active(&panel).append(userMsg("write me a function"));
    AIPanelTestAccess::active(&panel).append(asstMsg("here's the code", "qwen2.5-coder:7b"));
    EXPECT_EQ_INT("2 messages appended while in Coding mode",
                  2, AIPanelTestAccess::coding(&panel).size());
    EXPECT_EQ_INT("chat vector still 3 (no cross-mode leak)",
                  3, AIPanelTestAccess::chat(&panel).size());

    // 4. Switch to Data mode → activeMessages() is empty.
    EXPECT_TRUE("Data mode toggle accepted", AIPanelTestAccess::switchToData(&panel));
    EXPECT_TRUE("activeMessages() returns data vector after switch",
                &AIPanelTestAccess::active(&panel) == &AIPanelTestAccess::data(&panel));
    EXPECT_EQ_INT("activeMessages() is empty after switch to Data",
                  0, AIPanelTestAccess::active(&panel).size());
    EXPECT_EQ_INT("chat vector still 3", 3, AIPanelTestAccess::chat(&panel).size());
    EXPECT_EQ_INT("coding vector still 2", 2, AIPanelTestAccess::coding(&panel).size());

    // 5. Switch back to Chat → the original 3 messages are still there.
    EXPECT_TRUE("Chat mode toggle accepted", AIPanelTestAccess::switchToChat(&panel));
    EXPECT_TRUE("activeMessages() returns chat vector after switch back",
                &AIPanelTestAccess::active(&panel) == &AIPanelTestAccess::chat(&panel));
    EXPECT_EQ_INT("chat vector still has 3 messages after round-trip",
                  3, AIPanelTestAccess::chat(&panel).size());
    EXPECT_EQ_STR("first chat message text intact",
                  "hello", AIPanelTestAccess::chat(&panel).at(0).text);
    EXPECT_EQ_STR("second chat message text intact",
                  "hi back!", AIPanelTestAccess::chat(&panel).at(1).text);
    EXPECT_EQ_STR("third chat message text intact",
                  "how are you?", AIPanelTestAccess::chat(&panel).at(2).text);
}

// ───────────────────────────────────────────────────────────────────────
// Section 2 — clearChat() clears only the active vector
// ───────────────────────────────────────────────────────────────────────
static void testClearChatLeavesOtherModes() {
    std::printf("\n=== clearChat() clears only the active vector ===\n");

    AIPanel panel;

    // Seed all three vectors so we can prove only one gets cleared.
    AIPanelTestAccess::chat(&panel).append(userMsg("chat msg 1"));
    AIPanelTestAccess::chat(&panel).append(asstMsg("chat reply 1"));
    AIPanelTestAccess::coding(&panel).append(userMsg("coding msg 1"));
    AIPanelTestAccess::coding(&panel).append(asstMsg("coding reply 1"));
    AIPanelTestAccess::data(&panel).append(userMsg("data msg 1"));

    // Default mode is Chat; clearChat() should empty m_chatMessages only.
    AIPanelTestAccess::clearChat(&panel);
    EXPECT_EQ_INT("chat vector empty after clearChat in Chat mode",
                  0, AIPanelTestAccess::chat(&panel).size());
    EXPECT_EQ_INT("coding vector retained after clearChat in Chat mode",
                  2, AIPanelTestAccess::coding(&panel).size());
    EXPECT_EQ_INT("data vector retained after clearChat in Chat mode",
                  1, AIPanelTestAccess::data(&panel).size());

    // Switch to Coding and clear — only the coding vector empties; data
    // is untouched (chat is already empty from the previous step).
    AIPanelTestAccess::switchToCoding(&panel);
    AIPanelTestAccess::clearChat(&panel);
    EXPECT_EQ_INT("coding vector empty after clearChat in Coding mode",
                  0, AIPanelTestAccess::coding(&panel).size());
    EXPECT_EQ_INT("data vector retained after clearChat in Coding mode",
                  1, AIPanelTestAccess::data(&panel).size());
}

// ───────────────────────────────────────────────────────────────────────
// Section 3 — save + reload round-trip (all three vectors)
// ───────────────────────────────────────────────────────────────────────
static void testSaveReloadRoundTrip() {
    std::printf("\n=== save + reload round-trip ===\n");

    QTemporaryDir tmp;
    if (!tmp.isValid()) {
        std::printf("  [FAIL] could not create temporary directory\n");
        ++g_failed;
        return;
    }
    const QString histPath = tmp.path() + "/history.json";

    {
        // Phase 1: populate all three vectors, save, destroy panel.
        AIPanel panel;
        AIPanelTestAccess::setHistoryPath(&panel, histPath);

        AIPanelTestAccess::chat(&panel).append(userMsg("chat-q1"));
        AIPanelTestAccess::chat(&panel).append(asstMsg("chat-a1"));

        AIPanelTestAccess::coding(&panel).append(userMsg("coding-q1"));
        AIPanelTestAccess::coding(&panel).append(asstMsg("coding-a1", "qwen-coder"));
        AIPanelTestAccess::coding(&panel).append(userMsg("coding-q2"));

        AIPanelTestAccess::data(&panel).append(userMsg("data-q1"));
        AIPanelTestAccess::data(&panel).append(asstMsg("data-a1", "claude-sonnet"));

        AIPanelTestAccess::saveNow(&panel);
        EXPECT_TRUE("save produced an on-disk file", QFileInfo::exists(histPath));

        // Sanity: the file IS the v2 format.
        QFile f(histPath);
        f.open(QIODevice::ReadOnly);
        const QByteArray bytes = f.readAll();
        f.close();
        const QJsonDocument doc = QJsonDocument::fromJson(bytes);
        EXPECT_TRUE("save format is a JSON object", doc.isObject());
        const QJsonObject root = doc.object();
        EXPECT_EQ_INT("save format declares version 2", 2, root.value("version").toInt(-1));
        EXPECT_TRUE("save format has chat key",   root.contains("chat"));
        EXPECT_TRUE("save format has coding key", root.contains("coding"));
        EXPECT_TRUE("save format has data key",   root.contains("data"));
        EXPECT_EQ_INT("chat array has 2 entries",   2, root.value("chat").toArray().size());
        EXPECT_EQ_INT("coding array has 3 entries", 3, root.value("coding").toArray().size());
        EXPECT_EQ_INT("data array has 2 entries",   2, root.value("data").toArray().size());
    }

    {
        // Phase 2: fresh panel, load from disk, verify every vector round-trips.
        AIPanel panel2;
        AIPanelTestAccess::setHistoryPath(&panel2, histPath);
        AIPanelTestAccess::load(&panel2);

        EXPECT_EQ_INT("chat vector restored to 2",   2, AIPanelTestAccess::chat(&panel2).size());
        EXPECT_EQ_INT("coding vector restored to 3", 3, AIPanelTestAccess::coding(&panel2).size());
        EXPECT_EQ_INT("data vector restored to 2",   2, AIPanelTestAccess::data(&panel2).size());

        EXPECT_EQ_STR("chat[0].text round-trips",
                      "chat-q1", AIPanelTestAccess::chat(&panel2).at(0).text);
        EXPECT_EQ_STR("chat[1].text round-trips",
                      "chat-a1", AIPanelTestAccess::chat(&panel2).at(1).text);
        EXPECT_TRUE("chat[0].role is User",
                    AIPanelTestAccess::chat(&panel2).at(0).role == AIPanel::ChatMessage::User);
        EXPECT_TRUE("chat[1].role is Assistant",
                    AIPanelTestAccess::chat(&panel2).at(1).role == AIPanel::ChatMessage::Assistant);

        EXPECT_EQ_STR("coding[0].text round-trips",
                      "coding-q1", AIPanelTestAccess::coding(&panel2).at(0).text);
        EXPECT_EQ_STR("coding[1].model round-trips",
                      "qwen-coder", AIPanelTestAccess::coding(&panel2).at(1).model);
        EXPECT_EQ_INT("coding[1].promptTokens round-trips",
                      42, AIPanelTestAccess::coding(&panel2).at(1).promptTokens);
        EXPECT_EQ_INT("coding[1].evalTokens round-trips",
                      17, AIPanelTestAccess::coding(&panel2).at(1).evalTokens);
        EXPECT_TRUE("coding[1].elapsedMs round-trips (==1234)",
                    AIPanelTestAccess::coding(&panel2).at(1).elapsedMs == 1234);

        EXPECT_EQ_STR("data[1].model round-trips",
                      "claude-sonnet", AIPanelTestAccess::data(&panel2).at(1).model);
    }
}

// ───────────────────────────────────────────────────────────────────────
// Section 4 — v1 migration (flat-array format → chat vector)
// ───────────────────────────────────────────────────────────────────────
static void testV1Migration() {
    std::printf("\n=== v1 migration (flat-array format) ===\n");

    QTemporaryDir tmp;
    if (!tmp.isValid()) {
        std::printf("  [FAIL] could not create temporary directory\n");
        ++g_failed;
        return;
    }
    const QString histPath = tmp.path() + "/history.json";

    // Write a v0.0-style flat array directly. This is the "treat as
    // m_chatMessages, leave coding/data empty" migration path.
    QJsonArray flat;
    QJsonObject m1; m1["role"] = "user";      m1["text"] = "legacy-q";
    QJsonObject m2; m2["role"] = "assistant"; m2["text"] = "legacy-a";
                                              m2["model"] = "legacy-model";
    flat.append(m1);
    flat.append(m2);
    QJsonDocument flatDoc(flat);
    {
        QFile f(histPath);
        f.open(QIODevice::WriteOnly);
        f.write(flatDoc.toJson(QJsonDocument::Compact));
        f.close();
    }

    AIPanel panel;
    AIPanelTestAccess::setHistoryPath(&panel, histPath);
    AIPanelTestAccess::load(&panel);

    EXPECT_EQ_INT("v0.0 flat array → 2 messages in chat vector",
                  2, AIPanelTestAccess::chat(&panel).size());
    EXPECT_EQ_INT("v0.0 flat array → 0 messages in coding vector",
                  0, AIPanelTestAccess::coding(&panel).size());
    EXPECT_EQ_INT("v0.0 flat array → 0 messages in data vector",
                  0, AIPanelTestAccess::data(&panel).size());
    EXPECT_EQ_STR("legacy[0].text preserved",
                  "legacy-q", AIPanelTestAccess::chat(&panel).at(0).text);
    EXPECT_EQ_STR("legacy[1].text preserved",
                  "legacy-a", AIPanelTestAccess::chat(&panel).at(1).text);
    EXPECT_EQ_STR("legacy[1].model preserved",
                  "legacy-model", AIPanelTestAccess::chat(&panel).at(1).model);

    // Also test the v1 {version:1, messages:[...]} format — the actual
    // shape v0.1.39-v0.1.66 wrote.
    const QString v1Path = tmp.path() + "/v1-history.json";
    QJsonObject v1root;
    v1root["version"]  = 1;
    v1root["messages"] = flat;
    {
        QFile f(v1Path);
        f.open(QIODevice::WriteOnly);
        f.write(QJsonDocument(v1root).toJson(QJsonDocument::Compact));
        f.close();
    }

    AIPanel panel2;
    AIPanelTestAccess::setHistoryPath(&panel2, v1Path);
    AIPanelTestAccess::load(&panel2);

    EXPECT_EQ_INT("v1 wrapped format → 2 in chat",
                  2, AIPanelTestAccess::chat(&panel2).size());
    EXPECT_EQ_INT("v1 wrapped format → 0 in coding",
                  0, AIPanelTestAccess::coding(&panel2).size());
    EXPECT_EQ_INT("v1 wrapped format → 0 in data",
                  0, AIPanelTestAccess::data(&panel2).size());
    EXPECT_EQ_STR("v1 wrapped → legacy text preserved",
                  "legacy-q", AIPanelTestAccess::chat(&panel2).at(0).text);
}

// ───────────────────────────────────────────────────────────────────────
// main — offscreen QApplication; no event loop.
// ───────────────────────────────────────────────────────────────────────
int main(int argc, char *argv[]) {
    // Force offscreen platform BEFORE QApplication construction so any
    // QWidget the AIPanel constructor pulls in (status bar, completer,
    // etc.) lives in an offscreen surface.
    qputenv("QT_QPA_PLATFORM", "offscreen");
    QApplication app(argc, argv);

    std::printf("=== AIPanel chat-history v0.1.67 three-vector refactor ===\n");

    testCrossModePartition();
    testClearChatLeavesOtherModes();
    testSaveReloadRoundTrip();
    testV1Migration();

    std::printf("\n=== %d passed · %d failed ===\n", g_passed, g_failed);
    return (g_failed == 0) ? 0 : 1;
}
