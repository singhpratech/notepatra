// test_stream_error.cpp — guards the streaming-error fixes.
//
// Part 1 (offline, deterministic — the A6 Noter-audit fix): the Ollama
// backend's finished-handler must surface transport errors. Pre-fix,
// onFinishedOllama() never checked m_reply->error() before nulling m_reply,
// so "Ollama down → /api/generate connection-refused" produced NEITHER
// error() nor finished() — the caller hung forever (sandboxed repro:
// gotError=0 gotFinished=0). The friendly onError wording ("Ollama not
// running. Start it with: ollama serve") was connected nowhere. These
// sections point the client at a CLOSED localhost port (no live server
// needed — offline-tolerant per project rule) and assert the signal
// contract: exactly one prompt error(), no finished(), no hang, friendly
// message, and a clean state reset so the next request works.
//
// Part 2 (network, skip-tolerant — the v0.1.98 fix): a chat stream that
// fails auth (a bad/expired API key → HTTP 401 "User not found") MUST
// surface an error() promptly — it must NOT hang until the caller's own
// timeout, and it must NOT report a phantom finished().
//
// Before the v0.1.98 fix, OpenRouter's 401 body is a plain JSON error, not
// an SSE "data:" frame, so onReadyReadOpenAI() skipped it; and
// onFinishedOpenAI() nulled m_reply before the safety-net lambda in
// generate() could read m_reply->error(). Net effect: neither finished()
// nor error() ever fired and Extract/chat spun ~60 s with no feedback.
//
// Part 2 is a NETWORK integration test: it needs to reach OpenRouter to get
// a real 401. If the network is unavailable (we get a connectivity error,
// or nothing within the cap), it SKIPs — offline CI is not a regression of
// that code path. It only hard-FAILs if a bad key produces finished() with
// no error, or if an error takes too long to arrive.

#include <QCoreApplication>
#include <QEventLoop>
#include <QHostAddress>
#include <QJsonArray>
#include <QJsonObject>
#include <QTcpServer>
#include <QTimer>
#include <QElapsedTimer>
#include <QTemporaryDir>
#include <cstdio>

#include "config.h"
#include "ollama.h"

static int g_pass = 0, g_fail = 0;
#define EXPECT(label, cond) \
    do { if (cond) { ++g_pass; std::printf("  [PASS] %s\n", label); } \
         else      { ++g_fail; std::printf("  [FAIL] %s\n", label); } } while (0)

// Grab a localhost port that is guaranteed-closed RIGHT NOW: bind an
// ephemeral port, note its number, release it. Connecting to it afterwards
// yields ECONNREFUSED — exactly what a stopped Ollama daemon produces.
static quint16 closedLocalPort() {
    QTcpServer srv;
    if (!srv.listen(QHostAddress::LocalHost, 0)) return 1;  // port 1: never user-listenable
    const quint16 port = srv.serverPort();
    srv.close();
    return port;
}

struct CaseResult {
    int errors = 0;
    int finishes = 0;
    QString lastError;
    qint64 ms = 0;
};

// Fire one generate() and collect the signal outcome. After the first
// terminal signal (or the cap), spin a short settle loop so double-emits
// and phantom finished()-after-error() are caught, not raced past.
static CaseResult runGenerate(OllamaClient &client, const QString &prompt,
                              const QJsonArray &prior = QJsonArray(),
                              int capMs = 10000) {
    CaseResult r;
    QEventLoop loop;
    auto cErr = QObject::connect(&client, &OllamaClient::error,
        [&](const QString &m) { r.lastError = m; ++r.errors; loop.quit(); });
    auto cFin = QObject::connect(&client, &OllamaClient::finished,
        [&](const QString &) { ++r.finishes; loop.quit(); });
    QElapsedTimer t;
    t.start();
    QTimer::singleShot(capMs, &loop, &QEventLoop::quit);
    client.generate(prompt, QString(), /*enableThinking=*/false,
                    QStringList(), QJsonArray(), prior);
    loop.exec();
    r.ms = t.elapsed();
    QEventLoop settle;
    QTimer::singleShot(250, &settle, &QEventLoop::quit);
    settle.exec();
    QObject::disconnect(cErr);
    QObject::disconnect(cFin);
    return r;
}

// ─── Part 1 — offline error-path contract (Noter audit A6) ──────────────
static void runOfflineErrorPathSections() {
    const quint16 port = closedLocalPort();
    const QString base = QStringLiteral("http://127.0.0.1:%1").arg(port);
    std::printf("Part 1: closed-port error contract (base %s)\n",
                qPrintable(base));

    // Keep config on a local backend so no auth/cloud branches interfere.
    Config &cfg = Config::instance();
    cfg.aiBackend = "Ollama";
    cfg.aiBaseUrl = base;

    // Section 1 — Ollama /api/generate path: the original hang.
    {
        std::printf(" Section 1: Ollama /api/generate vs closed port\n");
        OllamaClient client;
        client.setBackend(OllamaClient::Ollama);
        client.setBaseUrl(base);
        client.setModel("test-model");
        const CaseResult r = runGenerate(client, "hello");
        EXPECT("error() fired (pre-fix: neither signal ever fired)", r.errors >= 1);
        EXPECT("exactly one error() — no double-emit", r.errors == 1);
        EXPECT("no phantom finished()", r.finishes == 0);
        EXPECT("error message non-empty", !r.lastError.isEmpty());
        EXPECT("prompt (< 8s), not a hang", r.ms < 8000);
        EXPECT("friendly wording: 'Ollama not running'",
               r.lastError.contains("Ollama not running"));
        std::printf("    error after %lld ms: %s\n", (long long)r.ms,
                    qPrintable(r.lastError));

        // Section 2 — state reset: the SAME client must work again.
        std::printf(" Section 2: second request on the same client\n");
        const CaseResult r2 = runGenerate(client, "hello again");
        EXPECT("second request errors again (m_done re-armed)", r2.errors == 1);
        EXPECT("second request: no phantom finished()", r2.finishes == 0);
        EXPECT("second request prompt too", r2.ms < 8000);

        // Section 3 — Ollama /api/chat path (priorMessages forces the chat
        // endpoint) goes through the same finished-handler short-circuit.
        std::printf(" Section 3: Ollama /api/chat vs closed port\n");
        QJsonObject turn;
        turn["role"] = "user";
        turn["content"] = "earlier turn";
        QJsonArray prior;
        prior.append(turn);
        const CaseResult r3 = runGenerate(client, "hello", prior);
        EXPECT("chat endpoint: exactly one error()", r3.errors == 1);
        EXPECT("chat endpoint: no phantom finished()", r3.finishes == 0);
        EXPECT("chat endpoint: friendly wording",
               r3.lastError.contains("Ollama not running"));
    }

    // Section 4 — OpenAI-compat path (llama.cpp backend): the transport
    // fallback must route through the friendly wording too.
    {
        std::printf(" Section 4: llama.cpp /v1/chat/completions vs closed port\n");
        OllamaClient client;
        client.setBackend(OllamaClient::LlamaCpp);
        client.setBaseUrl(base);
        client.setModel("test-model");
        const CaseResult r = runGenerate(client, "hello");
        EXPECT("openai-compat: exactly one error()", r.errors == 1);
        EXPECT("openai-compat: no phantom finished()", r.finishes == 0);
        EXPECT("openai-compat: friendly wording: 'llama-server not running'",
               r.lastError.contains("llama-server not running"));
        std::printf("    error after %lld ms: %s\n", (long long)r.ms,
                    qPrintable(r.lastError));
    }

    // Section 5 — cancel() right after generate(): the abort path must stay
    // silent (no error(), no finished()), and the client must still work for
    // a follow-up request.
    {
        std::printf(" Section 5: cancel() emits nothing; client still usable\n");
        OllamaClient client;
        client.setBackend(OllamaClient::Ollama);
        client.setBaseUrl(base);
        client.setModel("test-model");
        int errs = 0, fins = 0;
        auto cErr = QObject::connect(&client, &OllamaClient::error,
                                     [&](const QString &) { ++errs; });
        auto cFin = QObject::connect(&client, &OllamaClient::finished,
                                     [&](const QString &) { ++fins; });
        client.generate("hello");
        client.cancel();  // before the event loop delivers anything
        QEventLoop settle;
        QTimer::singleShot(400, &settle, &QEventLoop::quit);
        settle.exec();
        QObject::disconnect(cErr);
        QObject::disconnect(cFin);
        EXPECT("cancel(): no error() emitted", errs == 0);
        EXPECT("cancel(): no finished() emitted", fins == 0);
        const CaseResult r = runGenerate(client, "after cancel");
        EXPECT("post-cancel request still surfaces exactly one error()",
               r.errors == 1 && r.finishes == 0);
    }
}

// ─── Part 2 — network 401 contract (v0.1.98) — skip-tolerant ────────────
// Returns 0 (pass or skip) / 1 (fail), same semantics as before.
static int runOpenRouter401Section() {
    // Point at OpenRouter with a deliberately invalid key. The sk-or- prefix
    // makes aiKeyForBackend("OpenRouter") actually hand it to the request, so
    // the server replies 401 rather than us short-circuiting on an empty key.
    Config &cfg = Config::instance();
    cfg.aiBackend = "OpenRouter";
    cfg.aiBaseUrl = "https://openrouter.ai/api/v1";
    // Build the fake key from fragments so no literal "sk-or-…" string sits in
    // source — GitHub push-protection / secret-scanning flags that pattern even
    // for an obviously-fake key. At runtime it still carries the sk-or- prefix
    // so aiKeyForBackend("OpenRouter") routes it; OpenRouter 401s it either way.
    cfg.aiOpenRouterKey = QStringLiteral("sk-") + QStringLiteral("or-")
                        + QStringLiteral("INVALID-TEST-KEY-EXPECT-401");

    OllamaClient client;
    client.setBackend(OllamaClient::OpenAICompat);
    client.setBaseUrl(cfg.aiBaseUrl);
    client.setModel("google/gemini-3.5-flash");

    QString errMsg;
    bool gotError = false, gotFinished = false;
    QObject::connect(&client, &OllamaClient::error,
                     [&](const QString &m) { errMsg = m; gotError = true; });
    QObject::connect(&client, &OllamaClient::finished,
                     [&](const QString &) { gotFinished = true; });

    QElapsedTimer timer;
    timer.start();

    QEventLoop loop;
    QTimer::singleShot(20000, &loop, &QEventLoop::quit);   // hard cap
    QObject::connect(&client, &OllamaClient::error,    &loop, &QEventLoop::quit);
    QObject::connect(&client, &OllamaClient::finished, &loop, &QEventLoop::quit);

    fprintf(stdout, "Part 2: calling generate() with an invalid OpenRouter key...\n");
    client.generate("Say hello in one word.", "You are concise.");
    loop.exec();

    const qint64 ms = timer.elapsed();

    if (gotFinished && !gotError) {
        fprintf(stderr,
                "FAIL: a bad key produced finished() with no error — the 401 "
                "was not surfaced (the original bug).\n");
        return 1;
    }
    if (!gotError) {
        fprintf(stderr,
                "SKIP: no error within 20s — network unavailable or OpenRouter "
                "unreachable. Not a regression of this path.\n");
        return 0;
    }

    fprintf(stdout, "error fired after %lld ms: %s\n",
            (long long)ms, errMsg.toStdString().c_str());

    // The whole point of the fix: the error must be PROMPT, not a ~60 s hang.
    if (ms > 15000) {
        fprintf(stderr,
                "FAIL: error took %lld ms (> 15s) — still effectively hanging.\n",
                (long long)ms);
        return 1;
    }

    const QString lower = errMsg.toLower();
    const bool looksAuth =
        lower.contains("401") || lower.contains("403") ||
        lower.contains("authentication") || lower.contains("api key") ||
        lower.contains("user not found") || lower.contains("unauthor");
    const bool looksConnectivity =
        lower.contains("connection refused") || lower.contains("host") ||
        lower.contains("network") || lower.contains("unreachable") ||
        lower.contains("timed out") || lower.contains("timeout");

    if (looksAuth) {
        fprintf(stdout, "PASS: bad key surfaced a prompt auth error.\n");
        return 0;
    }
    if (looksConnectivity) {
        fprintf(stderr,
                "SKIP: got a connectivity error, not an auth error — network "
                "unavailable. Not a regression.\n");
        return 0;
    }
    // Prompt error, but unclassified — the hang is gone, which is the contract.
    fprintf(stdout, "PASS (prompt error, unclassified — the hang is gone).\n");
    return 0;
}

int main(int argc, char *argv[]) {
    // Hermetic HOME so we never read or clobber the user's real config.json.
    QTemporaryDir tmp;
    if (tmp.isValid()) qputenv("HOME", tmp.path().toUtf8());

    QCoreApplication app(argc, argv);

    // Part 1 — offline, deterministic, must be green even with no network.
    runOfflineErrorPathSections();
    std::printf("Part 1 summary: %d pass, %d fail\n", g_pass, g_fail);

    // Part 2 — network-dependent OpenRouter 401 (skip-tolerant).
    const int netRc = runOpenRouter401Section();

    if (g_fail > 0) {
        fprintf(stderr, "FAIL: %d offline error-path expectation(s) failed.\n",
                g_fail);
        return 1;
    }
    return netRc;
}
