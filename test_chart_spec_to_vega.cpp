// v0.1.90 — translator unit tests.
//
// Verifies the USER-VISIBLE CONTRACT (per
// [[feedback_test_the_user_visible_contract_not_proxies]]):
//   • every supported simplified chart type produces a valid Vega-Lite v5
//     spec (well-formed JSON with $schema + data + either mark or layer);
//   • multi-metric overlay (y as array) emits a fold transform;
//   • dual-axis (y+y2) emits a layered spec with independent y scale;
//   • universal facet wraps the spec under {facet, spec};
//   • theme-aware config block flips correctly for dark vs light;
//   • the supported() predicate matches every type used by the prompt.

#include <QCoreApplication>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QString>
#include <cstdio>

#include "src/chart_spec_to_vega.h"

static int g_pass = 0;
static int g_fail = 0;

static void check(bool cond, const QString &label) {
    if (cond) { ++g_pass; std::printf("  ✓ %s\n", qPrintable(label)); }
    else      { ++g_fail; std::printf("  ✗ %s\n", qPrintable(label)); }
}

static QJsonArray quarterSales() {
    return QJsonArray{
        QJsonObject{{"quarter","Q1"},{"revenue",1200},{"users",340},{"region","NA"}},
        QJsonObject{{"quarter","Q2"},{"revenue",1850},{"users",420},{"region","NA"}},
        QJsonObject{{"quarter","Q3"},{"revenue",2150},{"users",380},{"region","EU"}},
        QJsonObject{{"quarter","Q4"},{"revenue",2480},{"users",460},{"region","EU"}},
    };
}

static bool isVegaSpec(const QJsonObject &v) {
    if (!v.contains("$schema")) return false;
    if (!v.contains("data"))    return false;
    // Either mark, layer, or facet/spec.
    return v.contains("mark") || v.contains("layer") || v.contains("facet") ||
           v.contains("spec");
}

int main(int argc, char *argv[]) {
    QCoreApplication app(argc, argv);

    const ChartSpecToVega::Theme dark{true, "Dark"};
    const ChartSpecToVega::Theme light{false, "Light"};

    std::printf("=== test_chart_spec_to_vega — v0.1.90 ===\n\n");

    // ── Every supported type produces a valid spec ───────────────────
    std::printf("\n=== translate() covers every supported type ===\n");
    const QStringList simpleTypes = {
        "line", "area", "scatter", "bar", "horizontal-bar",
        "pie", "donut", "histogram", "boxplot",
        "heatmap", "density", "regression-line", "faceted-bar", "error-bar",
    };
    for (const QString &t : simpleTypes) {
        QJsonObject spec{
            {"type", t},
            {"x", t == "histogram" || t == "density" ? "revenue" : "quarter"},
            {"y", "revenue"},
            {"data", quarterSales()},
        };
        if (t == "heatmap")       spec["value"] = "revenue";
        if (t == "faceted-bar")   spec["facet"] = "region";
        if (t == "error-bar")     { spec["yMin"] = "revenue"; spec["yMax"] = "users"; }
        QString err;
        QJsonObject out = ChartSpecToVega::translate(spec, dark, &err);
        check(!out.isEmpty() && isVegaSpec(out),
              QString("%1 produces valid Vega-Lite spec").arg(t));
        if (!err.isEmpty()) {
            std::printf("    err: %s\n", qPrintable(err));
        }
    }

    // multi-type bar variants
    {
        QJsonObject s{{"type","grouped-bar"}, {"x","quarter"},
                      {"y", QJsonArray{"revenue","users"}}, {"data", quarterSales()}};
        QJsonObject out = ChartSpecToVega::translate(s, dark);
        check(isVegaSpec(out) && out.contains("transform"),
              "grouped-bar emits fold transform");
    }
    {
        QJsonObject s{{"type","stacked-bar"}, {"x","quarter"},
                      {"y", QJsonArray{"revenue","users"}}, {"data", quarterSales()}};
        QJsonObject out = ChartSpecToVega::translate(s, dark);
        const QString stackVal = out.value("encoding").toObject()
                                    .value("y").toObject().value("stack").toString();
        check(stackVal == "zero", "stacked-bar y.stack == zero");
    }

    // ── Multi-metric overlay ─────────────────────────────────────────
    std::printf("\n=== Multi-metric overlay (y as array) ===\n");
    {
        QJsonObject s{{"type","line"}, {"x","quarter"},
                      {"y", QJsonArray{"revenue","users"}}, {"data", quarterSales()}};
        QJsonObject out = ChartSpecToVega::translate(s, dark);
        check(out.contains("transform"),
              "line + array y emits fold transform");
        check(out.value("encoding").toObject().contains("color"),
              "line + array y uses color channel to disambiguate series");
        const auto folds = out.value("transform").toArray().first()
                              .toObject().value("fold").toArray();
        check(folds.size() == 2 && folds.contains("revenue") && folds.contains("users"),
              "fold transform includes both metric columns");
    }

    // ── Dual-axis ────────────────────────────────────────────────────
    std::printf("\n=== Dual-axis (y + y2) ===\n");
    {
        QJsonObject s{{"type","line"}, {"x","quarter"},
                      {"y","revenue"}, {"y2","users"}, {"data", quarterSales()}};
        QJsonObject out = ChartSpecToVega::translate(s, dark);
        check(out.contains("layer") && out.value("layer").toArray().size() == 2,
              "dual-axis emits 2-layer spec");
        const QJsonObject resolve = out.value("resolve").toObject();
        const QString ymode = resolve.value("scale").toObject().value("y").toString();
        check(ymode == "independent",
              "dual-axis resolves y scale independently (separate scales)");
        const QJsonObject layer2enc = out.value("layer").toArray().at(1)
                                          .toObject().value("encoding").toObject();
        const QString orient = layer2enc.value("y").toObject().value("axis").toObject()
                                  .value("orient").toString();
        check(orient == "right", "y2 axis renders on the right side");
    }

    // ── Universal facet wrapper ──────────────────────────────────────
    std::printf("\n=== Universal facet (works on any chart type) ===\n");
    {
        QJsonObject s{{"type","bar"}, {"x","quarter"}, {"y","revenue"},
                      {"facet","region"}, {"data", quarterSales()}};
        QJsonObject out = ChartSpecToVega::translate(s, dark);
        check(out.contains("facet") && out.contains("spec"),
              "facet wrapper structure: {facet, spec}");
        check(out.value("facet").toObject().value("field").toString() == "region",
              "facet field carried from simplified spec");
    }
    {
        QJsonObject s{{"type","scatter"}, {"x","revenue"}, {"y","users"},
                      {"row","region"}, {"column","quarter"}, {"data", quarterSales()}};
        QJsonObject out = ChartSpecToVega::translate(s, dark);
        const QJsonObject f = out.value("facet").toObject();
        check(f.contains("row") && f.contains("column"),
              "row+column facet produces 2D facet object");
    }
    {
        // The triple-combo: multi-metric × facet (the HCP example).
        QJsonObject s{{"type","bar"}, {"x","quarter"},
                      {"y", QJsonArray{"revenue","users"}},
                      {"facet","region"}, {"data", quarterSales()}};
        QJsonObject out = ChartSpecToVega::translate(s, dark);
        check(out.contains("facet") && out.contains("spec"),
              "multi-metric × facet composes — inner spec keeps fold transform");
        check(out.value("spec").toObject().contains("transform"),
              "fold transform stays inside the facet-wrapped inner spec");
    }

    // ── Theme bridge ────────────────────────────────────────────────
    std::printf("\n=== Theme-aware config ===\n");
    {
        QJsonObject s{{"type","bar"}, {"x","quarter"}, {"y","revenue"},
                      {"data", quarterSales()}};
        const QJsonObject darkOut  = ChartSpecToVega::translate(s, dark);
        const QJsonObject lightOut = ChartSpecToVega::translate(s, light);
        const QString darkLabel = darkOut.value("config").toObject()
                                      .value("axis").toObject().value("labelColor").toString();
        const QString lightLabel = lightOut.value("config").toObject()
                                       .value("axis").toObject().value("labelColor").toString();
        check(darkLabel  == "#D4D4D4", "dark theme axis label is light grey");
        check(lightLabel == "#2D2D2D", "light theme axis label is near-black");
        check(darkLabel != lightLabel, "theme bridge actually flips colours");
    }

    // ── supported() / safety ─────────────────────────────────────────
    std::printf("\n=== supported() / safety ===\n");
    for (const QString &t : simpleTypes) {
        check(ChartSpecToVega::supported(t), QString("supports %1").arg(t));
    }
    check(ChartSpecToVega::supported("grouped-bar"),  "supports grouped-bar");
    check(ChartSpecToVega::supported("stacked-bar"),  "supports stacked-bar");
    check(ChartSpecToVega::supported("stacked-horizontal-bar"),
          "supports stacked-horizontal-bar");
    check(!ChartSpecToVega::supported("nonsense"),    "rejects unknown type");

    // Empty/malformed data must not crash and must return empty.
    {
        QString err;
        QJsonObject bad{{"type","bar"}, {"x","quarter"}, {"y","revenue"}};
        QJsonObject out = ChartSpecToVega::translate(bad, dark, &err);
        check(out.isEmpty() && !err.isEmpty(),
              "missing data array returns empty + error message");
    }
    {
        QString err;
        QJsonObject bad{{"type","bar"}, {"data", quarterSales()}};  // missing x/y
        QJsonObject out = ChartSpecToVega::translate(bad, dark, &err);
        check(out.isEmpty(), "missing x/y returns empty");
    }

    std::printf("\n=== Summary ===\n");
    std::printf("  passed: %d\n", g_pass);
    std::printf("  failed: %d\n", g_fail);
    return g_fail == 0 ? 0 : 1;
}
