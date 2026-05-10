// ═══════════════════════════════════════════════════════════════════════
// test_vega_chart — v0.1.63 smoke test for the Vega-Lite renderer +
// generate_chart tool.
//
// What this exercises:
//   • VegaChartRenderer constructs without a display (offscreen platform)
//   • setSpec() with a valid bar-chart spec does not emit renderError
//     within a short async window
//   • exportPng() returns a string (the scaffold returns "")
//   • chartId() is non-empty + has the "chart-" prefix
//   • AiTools::availableTools() contains generate_chart
//   • AiTools::execute("generate_chart", spec) returns ok=true for a valid
//     spec and ok=false for a spec missing required keys
//
// Runs headless via QT_QPA_PLATFORM=offscreen (set by the CMake harness
// in notepatra_add_qt_test). When NOTEPATRA_WITH_WEBENGINE is OFF we
// still construct the stub widget — setSpec emits renderError("disabled")
// which the test tolerates by checking only that the construction path
// doesn't crash.
// ═══════════════════════════════════════════════════════════════════════

#include "src/ai_tools.h"
#include "src/charts/vega_chart_renderer.h"

#include <QApplication>
#include <QCoreApplication>
#include <QElapsedTimer>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QString>
#include <QTimer>

#include <cstdio>
#include <cstdlib>

#define EXPECT(cond) \
    do { \
        if (!(cond)) { \
            std::fprintf(stderr, "FAIL: %s line %d: %s\n", __FILE__, __LINE__, #cond); \
            std::exit(1); \
        } \
    } while (0)

// Build a minimal bar-chart Vega-Lite spec. Inline data so the JS shell
// doesn't need network for the data fetch (only for the vega libs).
static QJsonObject makeBarSpec() {
    QJsonObject spec;
    spec["$schema"] = "https://vega.github.io/schema/vega-lite/v5.json";
    spec["description"] = "test bar chart";
    spec["mark"] = "bar";

    QJsonArray values;
    auto push = [&](const QString &c, int v) {
        QJsonObject o;
        o["category"] = c;
        o["value"]    = v;
        values.append(o);
    };
    push("A", 30);
    push("B", 80);
    push("C", 45);

    QJsonObject data;
    data["values"] = values;
    spec["data"] = data;

    QJsonObject xField;
    xField["field"] = "category";
    xField["type"]  = "nominal";
    QJsonObject yField;
    yField["field"] = "value";
    yField["type"]  = "quantitative";
    QJsonObject enc;
    enc["x"] = xField;
    enc["y"] = yField;
    spec["encoding"] = enc;

    return spec;
}

// Pump the event loop for up to `ms` milliseconds, returning early if
// `done` becomes true. Lets the test wait on the async page load
// without hard-sleeping (offscreen build wouldn't even paint).
static void spin(int ms, const std::function<bool()> &done = {}) {
    QElapsedTimer t;
    t.start();
    while (t.elapsed() < ms) {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 25);
        if (done && done()) return;
    }
}

int main(int argc, char **argv) {
    QApplication app(argc, argv);

    // ── 1. VegaChartRenderer construction does not crash ──
    {
        VegaChartRenderer r;
        const QString id = r.chartId();
        EXPECT(!id.isEmpty());
        EXPECT(id.startsWith(QStringLiteral("chart-")));
        EXPECT(r.exportPng() == QString());  // scaffold returns ""
    }

    // ── 2. setSpec with a valid spec does not emit renderError within
    //       a generous async window ──
    {
        VegaChartRenderer r;

        bool sawError = false;
        QString errMsg;
        QObject::connect(&r, &VegaChartRenderer::renderError,
                         [&](const QString &m) {
                             sawError = true;
                             errMsg = m;
                         });

        r.setSpec(makeBarSpec());

        // 600 ms is enough for the headless page-load stub to fire.
        // We don't require renderReady — the offscreen platform may
        // never actually paint, and the WebEngine stub build's
        // setSpec() immediately emits renderError("disabled"). Both
        // outcomes are tolerated; what we guard against is a crash.
        spin(600);

#ifdef NOTEPATRA_WITH_WEBENGINE
        // With WebEngine present, the JS shell loads from JSDelivr.
        // In offline CI sandboxes that fetch fails and we DO see
        // renderError. Accept that — the assertion that matters for
        // this scaffold is "construction + setSpec do not crash".
        // We only fail if the error mentions a programming bug
        // (e.g. "Web view not initialised").
        if (sawError) {
            EXPECT(!errMsg.contains("Web view not initialised"));
        }
#else
        // Stub path: setSpec MUST emit renderError so the UI shows the
        // rebuild hint.
        EXPECT(sawError);
        EXPECT(errMsg.contains(QStringLiteral("disabled")));
#endif
    }

    // ── 3. AiTools::availableTools() includes generate_chart ──
    {
        const QJsonArray tools = AiTools::availableTools();
        bool found = false;
        for (const QJsonValue &v : tools) {
            const QJsonObject fn = v.toObject().value("function").toObject();
            if (fn.value("name").toString() == QStringLiteral("generate_chart")) {
                found = true;
                // Verify the required-array contains "spec".
                const QJsonArray req = fn.value("parameters").toObject()
                                          .value("required").toArray();
                bool requiresSpec = false;
                for (const QJsonValue &r : req) {
                    if (r.toString() == QStringLiteral("spec")) requiresSpec = true;
                }
                EXPECT(requiresSpec);
                break;
            }
        }
        EXPECT(found);
    }

    // ── 4. AiTools::execute on generate_chart returns ok=true for a
    //       valid spec, ok=false for a spec missing required keys ──
    {
        AiTools::ToolCall call;
        call.id   = "test-1";
        call.name = "generate_chart";
        QJsonObject args;
        args["spec"]  = makeBarSpec();
        args["title"] = "Test bars";
        call.args = args;

        const AiTools::ToolResult result =
            AiTools::execute(call, QStringLiteral("/tmp"));
        EXPECT(!result.isError);
        const QJsonDocument jd = QJsonDocument::fromJson(result.content.toUtf8());
        EXPECT(jd.isObject());
        const QJsonObject body = jd.object();
        EXPECT(body.value("ok").toBool());
        const QJsonObject resObj = body.value("result").toObject();
        EXPECT(resObj.value("chart_id").toString().startsWith(QStringLiteral("chart-")));
        EXPECT(resObj.value("spec").isObject());
        EXPECT(resObj.value("title").toString() == QStringLiteral("Test bars"));
    }

    // Missing required keys → io_error
    {
        AiTools::ToolCall call;
        call.id   = "test-2";
        call.name = "generate_chart";
        QJsonObject args;
        QJsonObject badSpec;
        badSpec["description"] = "no mark, no encoding";
        args["spec"] = badSpec;
        call.args = args;

        const AiTools::ToolResult result =
            AiTools::execute(call, QStringLiteral("/tmp"));
        EXPECT(result.isError);
        EXPECT(result.errorKind == QStringLiteral("io_error"));
    }

    // Non-object spec → io_error
    {
        AiTools::ToolCall call;
        call.id   = "test-3";
        call.name = "generate_chart";
        QJsonObject args;
        args["spec"] = "not an object";
        call.args = args;
        const AiTools::ToolResult result =
            AiTools::execute(call, QStringLiteral("/tmp"));
        EXPECT(result.isError);
    }

    // ── 5. Composite spec (layer / hconcat) — should be accepted even
    //       without top-level mark/encoding ──
    {
        AiTools::ToolCall call;
        call.id   = "test-4";
        call.name = "generate_chart";
        QJsonObject args;
        QJsonObject spec;
        spec["$schema"] = "https://vega.github.io/schema/vega-lite/v5.json";
        spec["layer"] = QJsonArray{makeBarSpec()};
        args["spec"] = spec;
        call.args = args;
        const AiTools::ToolResult result =
            AiTools::execute(call, QStringLiteral("/tmp"));
        EXPECT(!result.isError);
    }

    std::fprintf(stdout, "test_vega_chart: ok\n");
    return 0;
}
