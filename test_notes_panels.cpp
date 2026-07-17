// SPDX-License-Identifier: GPL-3.0-or-later
//
// Widget tests for the real NotesPanel Extract reliability layer
// (v0.1.112 busy-state machine + backend pre-flight probe).
//
// Headless: relies on QT_QPA_PLATFORM=offscreen (set in CTest props).

#include "config.h"
#include "notes.h"

#include <QApplication>
#include <QDir>
#include <QHostAddress>
#include <QMessageBox>
#include <QPushButton>
#include <QStandardPaths>
#include <QTcpServer>
#include <QTemporaryDir>
#include <QTextEdit>
#include <QTimer>

#include <cstdio>
#include <cstdlib>

static int g_pass = 0, g_fail = 0;
#define EXPECT(label, cond) \
    do { if (cond) { ++g_pass; std::printf("  [PASS] %s\n", label); } \
         else      { ++g_fail; std::printf("  [FAIL] %s\n", label); } } while (0)
#define REQUIRE(cond, msg) do { \
        if (!(cond)) { std::fprintf(stderr, "FATAL: %s\n", msg); \
                       std::exit(2); } } while (0)

// ─── v0.1.112 — Extract reliability layer (NotesPanel busy state) ────
//
// (3) busy-state machine: in-flight ⇒ wait cursor + button relabeled to
// Cancel (it IS the cancel control); the single cleanup helper restores
// everything from every exit path and is idempotent.
static void testExtractBusyState() {
    std::printf("[test_notes_panels] Extract busy-state machine\n");

    NotesPanel panel;
    auto *btn = panel.findChild<QPushButton *>(
        QStringLiteral("noterExtractBtn"));
    EXPECT("has the Extract button", btn != nullptr);
    if (!btn) return;
    EXPECT("idle: not busy",          !panel.extractBusy());
    EXPECT("idle: button says Extract",
           btn->text() == QStringLiteral("Extract"));

    // ── in flight ──
    panel.beginExtractBusy();
    EXPECT("busy: extractBusy() true",  panel.extractBusy());
    EXPECT("busy: override wait cursor set",
           QApplication::overrideCursor() != nullptr);
    EXPECT("busy: button relabeled to Cancel",
           btn->text() == QStringLiteral("Cancel"));
    EXPECT("busy: button stays enabled (it is the cancel control)",
           btn->isEnabled());

    // ── cleanup helper restores everything ──
    panel.finishExtractCleanup();
    EXPECT("cleanup: not busy",          !panel.extractBusy());
    EXPECT("cleanup: override cursor restored",
           QApplication::overrideCursor() == nullptr);
    EXPECT("cleanup: button back to Extract",
           btn->text() == QStringLiteral("Extract"));
    EXPECT("cleanup: button enabled", btn->isEnabled());

    // Idempotent — a second cleanup must NOT pop someone else's override.
    panel.finishExtractCleanup();
    EXPECT("cleanup: second call is a no-op (no cursor underflow)",
           QApplication::overrideCursor() == nullptr);

    // ── clicking the button while busy cancels (never stacks) ──
    panel.beginExtractBusy();
    EXPECT("re-arm: busy again", panel.extractBusy());
    btn->click();   // routed to cancelExtract() by onExtractClicked
    EXPECT("click-while-busy: cancels the flight", !panel.extractBusy());
    EXPECT("click-while-busy: cursor restored",
           QApplication::overrideCursor() == nullptr);
    EXPECT("click-while-busy: button back to Extract",
           btn->text() == QStringLiteral("Extract"));
}

// win-noter-segfault (v0.1.113): Qt's "offscreen" QPA plugin on Windows
// SIGSEGVs inside a QMessageBox::exec() modal loop (here the static
// QMessageBox::warning at notes.cpp:3517). Plain QDialog::exec() is fine; the
// SHIPPED binary uses the real Windows plugin where this warning renders
// correctly. CI-harness-only artifact — skip the modal DRIVE under
// offscreen-Windows (loud, never silent); the backend-down refusal + cursor/
// busy cleanup is covered on Linux Debug/ASan/Release.
static bool winOffscreenModalUnsafe() {
#if defined(Q_OS_WIN)
    return QGuiApplication::platformName()
               .compare(QLatin1String("offscreen"), Qt::CaseInsensitive) == 0;
#else
    return false;
#endif
}

// (4) pre-flight: Extract against a closed localhost port must show the
// "Ollama isn't running" box and bail WITHOUT leaving an override cursor
// or a busy button behind (pre-v0.1.112 this path hung the app forever).
static void testExtractPreflightClosedPort() {
    std::printf("[test_notes_panels] Extract pre-flight (backend down)\n");

    // Grab an ephemeral localhost port, then close it — connecting to it
    // is an instant refusal, never a live service.
    quint16 deadPort = 0;
    {
        QTcpServer srv;
        REQUIRE(srv.listen(QHostAddress::LocalHost, 0),
                "could not bind an ephemeral port");
        deadPort = srv.serverPort();
        srv.close();
    }
    Config::instance().aiBackend = QStringLiteral("Ollama");
    Config::instance().aiBaseUrl =
        QStringLiteral("http://127.0.0.1:%1").arg(deadPort);

    NotesPanel panel;
    panel.newMeetingNote();
    QApplication::processEvents();
    QTextEdit *editor = panel.findChild<QTextEdit *>();
    EXPECT("preflight: editor exists", editor != nullptr);
    if (!editor) return;
    if (winOffscreenModalUnsafe()) {
        std::printf("  [SKIP] backend-down Extract drive — offscreen+Windows "
                    "SIGSEGVs in the static QMessageBox::warning (notes.cpp:3517, "
                    "win-noter-segfault); refusal + cursor/busy cleanup is covered "
                    "on Linux Debug/ASan/Release.\n");
        return;
    }
    editor->setPlainText(QStringLiteral(
        "Discussed the roadmap. @alice ships the build tomorrow 10am."));

    // The pre-flight failure box is modal — poll for it and close it as
    // soon as it shows, recording what it said.
    bool sawBox = false;
    QString boxText;
    QTimer poller;
    poller.setInterval(50);
    QObject::connect(&poller, &QTimer::timeout, [&]() {
        const QList<QWidget *> tops = QApplication::topLevelWidgets();
        for (QWidget *w : tops) {
            auto *mb = qobject_cast<QMessageBox *>(w);
            if (!mb || !mb->isVisible()) continue;
            sawBox = true;
            boxText = mb->text();
            mb->close();
        }
    });
    poller.start();
    panel.endMeetingSweep();   // probe fails fast → warning box → returns
    poller.stop();

    EXPECT("preflight: message box shown", sawBox);
    std::printf("  (box text: %s)\n", qPrintable(boxText));
    EXPECT("preflight: box says how to start Ollama",
           boxText.contains(QStringLiteral("ollama serve")));
    EXPECT("preflight: NO override cursor left behind",
           QApplication::overrideCursor() == nullptr);
    EXPECT("preflight: panel not busy", !panel.extractBusy());
    auto *btn = panel.findChild<QPushButton *>(
        QStringLiteral("noterExtractBtn"));
    EXPECT("preflight: button still reads Extract",
           btn && btn->text() == QStringLiteral("Extract"));
    EXPECT("preflight: button still enabled", btn && btn->isEnabled());
}

int main(int argc, char *argv[]) {
    // DIAG (win-noter-segfault): unbuffered stdout so the LAST section header
    // printed before a hard crash is captured by ctest --output-on-failure,
    // pinpointing the exact failing section on Windows. No behaviour change.
    setvbuf(stdout, nullptr, _IONBF, 0);
    qputenv("QT_QPA_PLATFORM", "offscreen");
    QApplication app(argc, argv);

    // v0.1.112 — the NotesPanel tests construct the REAL panel, which
    // writes under <Documents>/Notepatra/Noter. Sandbox HOME so the
    // user's actual notes are never touched (same pattern as
    // test_notes_panel_widget).
    QTemporaryDir tmpHome;
    REQUIRE(tmpHome.isValid(), "QTemporaryDir failed");
    qputenv("HOME", tmpHome.path().toUtf8());
    qputenv("XDG_DOCUMENTS_DIR", (tmpHome.path() + "/Documents").toUtf8());
    QStandardPaths::setTestModeEnabled(true);
    QDir().mkpath(tmpHome.path() + "/Documents");

    testExtractBusyState();
    testExtractPreflightClosedPort();

    std::printf("\n[test_notes_panels] %d passed, %d failed\n",
                g_pass, g_fail);
    return g_fail == 0 ? 0 : 1;
}
