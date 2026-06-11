// D6 (single-instance protocol hardening) — greeting-gated forward + hung-
// primary fallback + Windows kernel-mutex singleton. Protocol: the primary
// writes the greeting byte on accept (proof its event loop pumps); the
// secondary sends the payload ONLY after the greeting and fully drains its
// write buffer before reporting Acked. Contract pinned here:
//
//   T1  greeting → payload roundtrip + byte-exact integrity → Acked
//   T2  hung primary: connect accepted, no greeting → bounded NoAck AND the
//       pipe carries ZERO payload bytes (a recovering primary must have
//       nothing to drain — this is the double-open fix)
//   T3  no server at all → fast NoServer (incl. the one fast retry)
//   T4  wrong greeting byte → NoAck, payload never sent
//   T5  old-primary simulation (never greets, waits for payload) → NoAck,
//       zero bytes delivered, bounded
//   T6  old-secondary simulation: ackClient() on a dead peer must not crash
//       and the payload must still be fully received
//   T7  (Windows only) kernel-mutex ERROR_ALREADY_EXISTS truth
//   T8  serverName() contract (deterministic, "notepatra-" prefix, < 40 chars)
//   T9  slow-reading primary + large payload: Acked only after the FULL
//       payload left the write buffer (truncated-forward fix)
//
// QCoreApplication only — no GUI, fully offline, no modals. Every case uses a
// unique per-run server name "np-test-d6-<pid>-<n>" (never the real
// serverName()) so a running Notepatra on the dev box cannot interfere, and
// calls QLocalServer::removeServer() first for stale-socket hygiene. Fake
// primaries run in std::thread with ALL Qt socket objects created, used and
// destroyed inside that thread, blocking API only (no event loop). All timing
// ceilings use QElapsedTimer with >= 4x-nominal margins for CI noise.

#include "singleinstance.h"

#include <QByteArray>
#include <QCoreApplication>
#include <QElapsedTimer>
#include <QLocalServer>
#include <QLocalSocket>
#include <QString>

#include <atomic>
#include <chrono>
#include <cstdio>
#include <thread>

#ifdef Q_OS_WIN
#  define NOMINMAX
#  include <windows.h>
#endif

static int g_pass = 0, g_fail = 0;
#define EXPECT(label, cond) \
    do { if (cond) { ++g_pass; std::printf("  [PASS] %s\n", label); } \
         else      { ++g_fail; std::printf("  [FAIL] %s\n", label); } \
         fflush(stdout); } while (0)

// Unique per-run server name — never collides with a real Notepatra instance
// or with a concurrently running copy of this test.
static QString testName(int n) {
    return QStringLiteral("np-test-d6-%1-%2")
        .arg(QCoreApplication::applicationPid())
        .arg(n);
}

// A realistic remote-open payload (exact-JSON integrity is asserted in T1).
static const QByteArray kBody =
    "{\"files\":[\"/tmp/np-d6-probe.txt\"],\"gotoLine\":7}";

static void spinUntil(const std::atomic<bool> &flag) {
    while (!flag.load())
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
}

// Drain the client socket until `expectLen` bytes arrived or the read stalls.
// Tolerates a peer that closes mid-read: data already in flight stays
// buffered on a local socket, so a final readAll() after the failed wait
// still collects it (the T6 old-secondary case depends on this).
static void readPayload(QLocalSocket *c, QByteArray &received, int expectLen,
                        int waitMs) {
    while (received.size() < expectLen) {
        if (c->bytesAvailable() > 0) { received += c->readAll(); continue; }
        if (!c->waitForReadyRead(waitMs)) {
            received += c->readAll();
            break;
        }
    }
}

// ── T1 — greeting → payload roundtrip + integrity ───────────────────────────
static void t1_ack_roundtrip() {
    std::printf("T1 — greeting -> payload roundtrip + integrity\n");
    fflush(stdout);
    const QString name = testName(1);
    QLocalServer::removeServer(name);

    std::atomic<bool> listening{false};
    std::atomic<bool> serverOk{false};
    QByteArray received;
    const int expectLen = kBody.size();

    // Fake responsive primary, production order: accept → GREET → read.
    std::thread server([&]() {
        QLocalServer srv;
        if (!srv.listen(name)) { listening = true; return; }
        listening = true;
        if (!srv.waitForNewConnection(8000)) return;
        QLocalSocket *c = srv.nextPendingConnection();
        if (!c) return;
        SingleInstance::ackClient(c);
        c->waitForBytesWritten(1000);
        readPayload(c, received, expectLen, 3000);
        c->disconnectFromServer();
        if (c->state() != QLocalSocket::UnconnectedState)
            c->waitForDisconnected(1000);
        serverOk = true;
    });
    spinUntil(listening);

    QElapsedTimer t;
    t.start();
    const SingleInstance::ForwardResult r =
        SingleInstance::forwardToPrimary(name, kBody);
    const qint64 elapsed = t.elapsed();
    server.join();

    EXPECT("T1: fake primary completed accept/greet/read", serverOk.load());
    EXPECT("T1: forwardToPrimary returned Acked",
           r == SingleInstance::ForwardResult::Acked);
    EXPECT("T1: primary received the payload byte-exact", received == kBody);
    std::printf("  (T1 elapsed: %lld ms)\n", static_cast<long long>(elapsed));
    fflush(stdout);
    EXPECT("T1: roundtrip under 2500 ms", elapsed < 2500);
}

// ── T2 — hung primary (the core fix) ────────────────────────────────────────
static void t2_hung_primary() {
    std::printf("\nT2 — hung primary: backlog connect, never drained\n");
    fflush(stdout);
    const QString name = testName(2);
    QLocalServer::removeServer(name);

    // SAME-THREAD QLocalServer that listen()s and is never drained: no event
    // loop runs and nextPendingConnection() is never called, so the kernel /
    // backlog accepts the connect exactly like a primary whose GUI thread is
    // blocked. Integrator note (Windows CI): if this case ever shows NoServer
    // there (named-pipe handshake needing a server-thread pump), move a
    // listen-then-sleep(6 s) server into a std::thread — semantics identical.
    QLocalServer hung;
    EXPECT("T2: hung server is listening", hung.listen(name));

    QElapsedTimer t;
    t.start();
    const SingleInstance::ForwardResult r =
        SingleInstance::forwardToPrimary(name, kBody, 500, 1500, /*ackMs=*/800);
    const qint64 elapsed = t.elapsed();
    std::printf("  (T2 elapsed: %lld ms)\n", static_cast<long long>(elapsed));
    fflush(stdout);

    // Old fire-and-forget code returns an Acked-equivalent instantly — the
    // broken behavior cannot pass either ceiling below.
    EXPECT("T2: forwardToPrimary returned NoAck (hung primary detected)",
           r == SingleInstance::ForwardResult::NoAck);
    EXPECT("T2: bounded fallback latency under 4000 ms", elapsed < 4000);
    EXPECT("T2: greeting window actually waited (>= 700 ms, not an early "
           "connect failure)", elapsed >= 700);

    // The double-open fix: the primary "recovers" (drains the backlog) AFTER
    // the secondary gave up. The pipe must carry ZERO payload bytes — under
    // the old send-first protocol the full payload sat deliverable here and
    // the recovered primary opened the same files a second time.
    QByteArray leftover;
    if (hung.waitForNewConnection(2000)) {
        if (QLocalSocket *c = hung.nextPendingConnection()) {
            while (true) {
                if (c->bytesAvailable() > 0) { leftover += c->readAll(); continue; }
                if (!c->waitForReadyRead(300)) { leftover += c->readAll(); break; }
            }
        }
    }
    EXPECT("T2: recovered primary drains ZERO bytes (nothing to double-open)",
           leftover.isEmpty());
    hung.close();
}

// ── T3 — no server: fast NoServer fail ──────────────────────────────────────
static void t3_no_server() {
    std::printf("\nT3 — no server: fast NoServer\n");
    fflush(stdout);
    const QString name = testName(3);
    QLocalServer::removeServer(name);  // guarantee nothing is bound here

    QElapsedTimer t;
    t.start();
    const SingleInstance::ForwardResult r =
        SingleInstance::forwardToPrimary(name, kBody, 500, 1500, 800);
    const qint64 elapsed = t.elapsed();
    std::printf("  (T3 elapsed: %lld ms)\n", static_cast<long long>(elapsed));
    fflush(stdout);

    EXPECT("T3: forwardToPrimary returned NoServer",
           r == SingleInstance::ForwardResult::NoServer);
    EXPECT("T3: fast-fail under 3000 ms (incl. the one fast retry)",
           elapsed < 3000);
}

// ── T4 — wrong greeting byte must not count as a live primary ───────────────
static void t4_wrong_ack_byte() {
    std::printf("\nT4 — wrong greeting byte ('X' instead of 0x06)\n");
    fflush(stdout);
    const QString name = testName(4);
    QLocalServer::removeServer(name);

    std::atomic<bool> listening{false};
    QByteArray received;

    std::thread server([&]() {
        QLocalServer srv;
        if (!srv.listen(name)) { listening = true; return; }
        listening = true;
        if (!srv.waitForNewConnection(8000)) return;
        QLocalSocket *c = srv.nextPendingConnection();
        if (!c) return;
        const char wrong = 'X';  // anything but SingleInstance::kAckByte
        c->write(&wrong, 1);
        c->flush();
        c->waitForBytesWritten(1000);
        // Try to read a payload that must never arrive.
        while (true) {
            if (c->bytesAvailable() > 0) { received += c->readAll(); continue; }
            if (!c->waitForReadyRead(800)) { received += c->readAll(); break; }
        }
    });
    spinUntil(listening);

    const SingleInstance::ForwardResult r =
        SingleInstance::forwardToPrimary(name, kBody, 500, 1500, 3000);
    server.join();

    EXPECT("T4: wrong greeting byte yields NoAck, not Acked (content verified)",
           r == SingleInstance::ForwardResult::NoAck);
    EXPECT("T4: payload was never sent after a wrong greeting",
           received.isEmpty());
}

// ── T5 — old-primary simulation: accepts, waits for payload, never greets ───
static void t5_close_without_ack() {
    std::printf("\nT5 — old primary: accepts and waits, never greets\n");
    fflush(stdout);
    const QString name = testName(5);
    QLocalServer::removeServer(name);

    std::atomic<bool> listening{false};
    QByteArray received;

    // A ≤ v0.1.113 primary: accepts and waits for a payload that the new
    // greeting-gated secondary will never send; then closes.
    std::thread server([&]() {
        QLocalServer srv;
        if (!srv.listen(name)) { listening = true; return; }
        listening = true;
        if (!srv.waitForNewConnection(8000)) return;
        QLocalSocket *c = srv.nextPendingConnection();
        if (!c) return;
        while (true) {
            if (c->bytesAvailable() > 0) { received += c->readAll(); continue; }
            if (!c->waitForReadyRead(800)) { received += c->readAll(); break; }
        }
        c->disconnectFromServer();
        if (c->state() != QLocalSocket::UnconnectedState)
            c->waitForDisconnected(1000);
    });
    spinUntil(listening);

    QElapsedTimer t;
    t.start();
    const SingleInstance::ForwardResult r =
        SingleInstance::forwardToPrimary(name, kBody);
    const qint64 elapsed = t.elapsed();
    server.join();
    std::printf("  (T5 elapsed: %lld ms)\n", static_cast<long long>(elapsed));
    fflush(stdout);

    // Deliberately NOT treated as success: the secondary falls back to a
    // standalone window, so the open happens exactly ONCE (locally) — the
    // old primary received nothing it could act on later.
    EXPECT("T5: no greeting == NoAck (never silent exit 0)",
           r == SingleInstance::ForwardResult::NoAck);
    EXPECT("T5: fallback decided under 4000 ms", elapsed < 4000);
    EXPECT("T5: zero bytes were delivered to the non-greeting primary",
           received.isEmpty());
}

// ── T6 — old-secondary simulation: ackClient on a dead peer ─────────────────
static void t6_ack_dead_peer() {
    std::printf("\nT6 — old secondary hangs up pre-ACK; ackClient on dead peer\n");
    fflush(stdout);
    const QString name = testName(6);
    QLocalServer::removeServer(name);

    std::atomic<bool> listening{false};
    std::atomic<bool> clientGone{false};
    std::atomic<bool> ackReturned{false};
    QByteArray received;
    const int expectLen = kBody.size();

    std::thread server([&]() {
        QLocalServer srv;
        if (!srv.listen(name)) { listening = true; return; }
        listening = true;
        if (!srv.waitForNewConnection(8000)) return;
        QLocalSocket *c = srv.nextPendingConnection();
        if (!c) return;
        readPayload(c, received, expectLen, 2000);
        // Wait until the secondary is provably destroyed, then ACK the dead
        // peer: write returns <= 0 and is swallowed — must never crash.
        spinUntil(clientGone);
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
        SingleInstance::ackClient(c);
        ackReturned = true;
    });
    spinUntil(listening);

    {
        // Old-secondary (≤ v0.1.113) behavior: write, hang up, vanish —
        // never waits for any ACK.
        QLocalSocket sock;
        sock.connectToServer(name);
        EXPECT("T6: old-style secondary connected", sock.waitForConnected(3000));
        sock.write(kBody);
        sock.flush();
        sock.waitForBytesWritten(1000);
        sock.disconnectFromServer();
        if (sock.state() != QLocalSocket::UnconnectedState)
            sock.waitForDisconnected(1000);
    }  // socket destroyed here — pre-ACK hangup complete
    clientGone = true;
    server.join();

    EXPECT("T6: primary received the full payload despite early hangup",
           received == kBody);
    EXPECT("T6: ackClient() on the dead peer returned without crashing",
           ackReturned.load());
}

// ── T9 — slow-reading primary + large payload: full drain before Acked ──────
static void t9_slow_reader_full_drain() {
    std::printf("\nT9 — slow reader + large payload: no truncation\n");
    fflush(stdout);
    const QString name = testName(9);
    QLocalServer::removeServer(name);

    // Large enough that one write() cannot fit any kernel pipe buffer —
    // forces multiple waitForBytesWritten cycles (the old single
    // waitForBytesWritten(500) + immediate destruction truncated this).
    QByteArray big = "{\"files\":[\"";
    big += QByteArray(700 * 1024, 'a');
    big += "\"],\"gotoLine\":-1}";

    std::atomic<bool> listening{false};
    QByteArray received;
    const int expectLen = big.size();

    std::thread server([&]() {
        QLocalServer srv;
        if (!srv.listen(name)) { listening = true; return; }
        listening = true;
        if (!srv.waitForNewConnection(8000)) return;
        QLocalSocket *c = srv.nextPendingConnection();
        if (!c) return;
        SingleInstance::ackClient(c);
        c->waitForBytesWritten(1000);
        // Deliberately slow consumer: small sips with pauses between them.
        while (received.size() < expectLen) {
            if (c->bytesAvailable() > 0) {
                received += c->read(64 * 1024);
                std::this_thread::sleep_for(std::chrono::milliseconds(5));
                continue;
            }
            if (!c->waitForReadyRead(2000)) { received += c->readAll(); break; }
        }
    });
    spinUntil(listening);

    const SingleInstance::ForwardResult r =
        SingleInstance::forwardToPrimary(name, big);
    server.join();

    EXPECT("T9: forwardToPrimary returned Acked",
           r == SingleInstance::ForwardResult::Acked);
    EXPECT("T9: slow primary received the LARGE payload byte-exact",
           received == big);
}

// ── T7 — Windows kernel-mutex singleton truth ───────────────────────────────
#ifdef Q_OS_WIN
static void t7_windows_mutex_truth() {
    std::printf("\nT7 — Windows kernel-mutex ERROR_ALREADY_EXISTS truth\n");
    fflush(stdout);
    const QString mux = QStringLiteral("np-test-mux-%1")
                            .arg(QCoreApplication::applicationPid());
    bool a = true, b = false;
    void *h1 = SingleInstance::acquireSingletonMutex(mux, &a);
    void *h2 = SingleInstance::acquireSingletonMutex(mux, &b);

    EXPECT("T7: first acquire returns a handle", h1 != nullptr);
    EXPECT("T7: first acquire reports no pre-existing owner", a == false);
    EXPECT("T7: second acquire reports an existing owner "
           "(gates the never-bind-second-server rule)", b == true);

    if (h1) ::CloseHandle(static_cast<HANDLE>(h1));
    if (h2) ::CloseHandle(static_cast<HANDLE>(h2));
}
#endif

// ── T8 — serverName() contract ──────────────────────────────────────────────
static void t8_server_name_contract() {
    std::printf("\nT8 — serverName() contract\n");
    fflush(stdout);
    const QString n1 = SingleInstance::serverName();
    const QString n2 = SingleInstance::serverName();
    EXPECT("T8: serverName() is deterministic across calls", n1 == n2);
    EXPECT("T8: serverName() starts with \"notepatra-\"",
           n1.startsWith(QStringLiteral("notepatra-")));
    EXPECT("T8: serverName() under 40 chars (named-pipe cap headroom)",
           n1.length() < 40);
}

int main(int argc, char *argv[]) {
    QCoreApplication app(argc, argv);

    std::printf("=== test_single_instance ===\n\n");
    fflush(stdout);

    t1_ack_roundtrip();
    t2_hung_primary();
    t3_no_server();
    t4_wrong_ack_byte();
    t5_close_without_ack();
    t6_ack_dead_peer();
    t9_slow_reader_full_drain();
#ifdef Q_OS_WIN
    t7_windows_mutex_truth();
#endif
    t8_server_name_contract();

    std::printf("\n%d passed, %d failed\n", g_pass, g_fail);
    fflush(stdout);
    return g_fail;
}
