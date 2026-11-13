// SPDX-License-Identifier: GPL-3.0-or-later
//
// The MCP chart gate in a Full build WITHOUT WebEngine — the macOS Full
// configuration. This target compiles the real src/mcp_bridge.cpp with
// NOTEPATRA_HAVE_DUCKDB=1 and no NOTEPATRA_WITH_WEBENGINE, so it is that
// flavor on every OS (on the macOS CI runner it is exactly the reported build).
//
// Reported from v0.1.134: app_info said edition "Full" while render_chart said
// "charts require the Full edition (WebEngine)". The edition and the chart
// answer must agree, and get_capabilities must say charts are unavailable.
//
// Fake host + real QLocalServer round-trip; no MainWindow, no config writes
// (no endpoint file is published), no message boxes.

#include "mcp_bridge.h"

#include <QApplication>
#include <QElapsedTimer>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLocalSocket>
#include <QTest>

#ifndef NOTEPATRA_HAVE_DUCKDB
#error "test_mcp_chart_gate must be built as a Full (DuckDB) flavor"
#endif
#ifdef NOTEPATRA_WITH_WEBENGINE
#error "test_mcp_chart_gate must be built without WebEngine"
#endif

class TestMcpChartGate : public QObject {
    Q_OBJECT

    McpBridge *m_bridge = nullptr;
    QString m_name;

    static QString platform() {
#if defined(Q_OS_WIN)
        return QStringLiteral("windows");
#elif defined(Q_OS_MACOS)
        return QStringLiteral("macos");
#else
        return QStringLiteral("linux");
#endif
    }

    QJsonObject call(QLocalSocket &s, int id, const QString &verb,
                     const QJsonObject &args = QJsonObject()) {
        QJsonObject req;
        req[QStringLiteral("id")] = id;
        req[QStringLiteral("verb")] = verb;
        req[QStringLiteral("args")] = args;
        s.write(QJsonDocument(req).toJson(QJsonDocument::Compact) + '\n');
        s.flush();
        return readObj(s);
    }

    static QJsonObject readObj(QLocalSocket &s) {
        QElapsedTimer t;
        t.start();
        while (!s.canReadLine() && t.elapsed() < 5000) QTest::qWait(10);
        return s.canReadLine() ? QJsonDocument::fromJson(s.readLine().trimmed()).object()
                               : QJsonObject();
    }

    bool connect(QLocalSocket &s) {
        s.connectToServer(m_name);
        QElapsedTimer t;
        t.start();
        while (s.state() != QLocalSocket::ConnectedState && t.elapsed() < 5000)
            QTest::qWait(10);
        if (s.state() != QLocalSocket::ConnectedState) return false;
        return !readObj(s).isEmpty(); // greeting
    }

private slots:
    void init() {
        McpEditorHost host;
        host.tabCount = [] { return 0; };
        m_name = QStringLiteral("notepatra-mcp-chartgate-%1")
                     .arg(QCoreApplication::applicationPid());
        m_bridge = new McpBridge(host, this, m_name);
        QVERIFY(m_bridge->isListening());
    }

    void cleanup() {
        delete m_bridge;
        m_bridge = nullptr;
    }

    void edition_and_chart_answer_agree() {
        QLocalSocket s;
        QVERIFY(connect(s));
        const QJsonObject info =
            call(s, 1, QStringLiteral("app_info")).value(QLatin1String("result")).toObject();
        QCOMPARE(info.value(QLatin1String("edition")).toString(), QStringLiteral("Full"));

        const QString expected = McpBridge::chartsUnavailableReason(true, false, platform());
        QVERIFY(!expected.isEmpty());
        QCOMPARE(McpBridge::chartsUnavailableReasonForThisBuild(), expected);

        QJsonObject spec;
        spec[QStringLiteral("mark")] = QStringLiteral("bar");
        QJsonObject a;
        a[QStringLiteral("spec")] = spec;
        QJsonObject r = call(s, 2, QStringLiteral("render_chart"), a);
        QCOMPARE(r.value(QLatin1String("ok")).toBool(), false);
        const QString err = r.value(QLatin1String("error")).toString();
        QCOMPARE(err, expected);
        // The v0.1.134 bug: a Full build told to get the Full edition.
        QVERIFY2(!err.contains(QLatin1String("Full edition (WebEngine)")), qPrintable(err));
        QVERIFY2(!err.contains(QLatin1String("require")), qPrintable(err));

        a[QStringLiteral("path")] = QStringLiteral("/nonexistent-dir/chart.png");
        a[QStringLiteral("format")] = QStringLiteral("png");
        r = call(s, 3, QStringLiteral("export_chart"), a);
        QCOMPARE(r.value(QLatin1String("ok")).toBool(), false);
        QCOMPARE(r.value(QLatin1String("error")).toString(), expected);
#if defined(Q_OS_MACOS)
        QVERIFY(err.contains(QLatin1String("not available in the macOS build")));
#endif
    }

    void capabilities_say_charts_unavailable() {
        QLocalSocket s;
        QVERIFY(connect(s));
        const QJsonObject r =
            call(s, 4, QStringLiteral("get_capabilities")).value(QLatin1String("result")).toObject();
        QCOMPARE(r.value(QLatin1String("edition")).toString(), QStringLiteral("Full"));
        const QJsonObject f = r.value(QLatin1String("features")).toObject();
        QCOMPARE(f.value(QLatin1String("duckdb")).toBool(), true);
        QCOMPARE(f.value(QLatin1String("webengine")).toBool(), false);
        QVERIFY(f.contains(QLatin1String("charts")));
        QCOMPARE(f.value(QLatin1String("charts")).toBool(), false);
        QCOMPARE(r.value(QLatin1String("charts_unavailable_reason")).toString(),
                 McpBridge::chartsUnavailableReason(true, false, platform()));
    }
};

QTEST_MAIN(TestMcpChartGate)
#include "test_mcp_chart_gate.moc"
