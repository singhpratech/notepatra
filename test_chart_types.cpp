// v0.1.76 — regression test for the expanded ChartRender catalogue.
//
// Guards against:
//   • A new chart type being added to looksLikeChartSpec() but missing a
//     dispatcher branch in renderFromObject() (or vice versa).
//   • A render helper silently returning null for a valid spec.
//   • A future Qt upgrade dropping a QtCharts series type we depend on.

#include <QApplication>
#include <QJsonArray>
#include <QJsonObject>
#include <QWidget>

#include "src/chartrender.h"

static int g_pass = 0;
static int g_fail = 0;

static void check(bool cond, const QString &label) {
    if (cond) { ++g_pass; printf("  ✓ %s\n", qPrintable(label)); }
    else      { ++g_fail; printf("  ✗ %s\n", qPrintable(label)); }
}

static QJsonArray salesRows() {
    return QJsonArray{
        QJsonObject{{"quarter","Q1"},{"revenue",1200},{"q1",10},{"q2",20},{"q3",15},{"q4",25}},
        QJsonObject{{"quarter","Q2"},{"revenue",1850},{"q1",12},{"q2",22},{"q3",18},{"q4",30}},
        QJsonObject{{"quarter","Q3"},{"revenue",2150},{"q1",15},{"q2",25},{"q3",20},{"q4",35}},
        QJsonObject{{"quarter","Q4"},{"revenue",2480},{"q1",18},{"q2",28},{"q3",24},{"q4",40}},
    };
}

static QJsonArray scoreRows() {
    QJsonArray out;
    // synthetic distribution: 50 values clustered around 70 with a tail.
    const int seeds[] = {65,68,72,71,69,75,73,70,67,74,
                          80,82,78,79,81,77,85,76,83,79,
                          90,88,92,87,91,86,89,93,95,94,
                          60,62,58,64,63,55,57,59,61,56,
                          70,71,72,69,68,70,71,72,70,69};
    for (int s : seeds) out.append(QJsonObject{{"score", s}});
    return out;
}

static QJsonArray priceByProductRows() {
    QJsonArray out;
    auto addBatch = [&](const QString &p, const std::initializer_list<double> &vs){
        for (double v : vs) out.append(QJsonObject{{"product", p}, {"price", v}});
    };
    addBatch("A", {10, 12, 14, 11, 13, 15, 12, 11, 13, 14});
    addBatch("B", {20, 22, 19, 25, 30, 18, 21, 23, 22, 27});
    addBatch("C", {30, 40, 25, 45, 35, 50, 38, 42, 33, 47});
    return out;
}

static QJsonArray scatterRows() {
    QJsonArray out;
    for (int i = 0; i < 20; ++i) {
        out.append(QJsonObject{{"x", double(i)}, {"y", double(i) * 1.5 + (i % 3)}});
    }
    return out;
}

static void renderOk(const QJsonObject &spec, const QString &label) {
    QString err;
    QWidget *w = ChartRender::renderFromObject(spec, nullptr, &err);
    check(w != nullptr, label + " — renders non-null");
    if (!err.isEmpty()) {
        printf("    err: %s\n", qPrintable(err));
    }
    if (w) delete w;  // we own it (parent=nullptr)
}

int main(int argc, char *argv[]) {
    // QtCharts needs QApplication (widget subsystem). Use offscreen platform
    // so the test runs headless in CI.
    qputenv("QT_QPA_PLATFORM", "offscreen");
    QApplication app(argc, argv);

    printf("=== test_chart_types — v0.1.76 expanded chart catalogue ===\n\n");

    // ── Pre-existing 4 types (regression coverage) ──────────────────
    printf("[legacy 4 types]\n");
    renderOk(QJsonObject{
        {"type","line"}, {"x","quarter"}, {"y","revenue"}, {"data", salesRows()}
    }, "line");
    renderOk(QJsonObject{
        {"type","bar"}, {"x","quarter"}, {"y","revenue"}, {"data", salesRows()}
    }, "bar");
    renderOk(QJsonObject{
        {"type","pie"}, {"label","quarter"}, {"value","revenue"}, {"data", salesRows()}
    }, "pie");
    renderOk(QJsonObject{
        {"type","scatter"}, {"x","x"}, {"y","y"}, {"data", scatterRows()}
    }, "scatter");

    // ── v0.1.76 additions ───────────────────────────────────────────
    printf("\n[v0.1.76 new types]\n");
    renderOk(QJsonObject{
        {"type","area"}, {"x","quarter"}, {"y","revenue"}, {"data", salesRows()}
    }, "area");
    renderOk(QJsonObject{
        {"type","horizontal-bar"}, {"x","quarter"}, {"y","revenue"}, {"data", salesRows()}
    }, "horizontal-bar");
    renderOk(QJsonObject{
        {"type","stacked-bar"},
        {"x","quarter"},
        {"y", QJsonArray{"q1","q2","q3","q4"}},
        {"data", salesRows()}
    }, "stacked-bar");
    renderOk(QJsonObject{
        {"type","stacked-horizontal-bar"},
        {"x","quarter"},
        {"y", QJsonArray{"q1","q2","q3","q4"}},
        {"data", salesRows()}
    }, "stacked-horizontal-bar");
    renderOk(QJsonObject{
        {"type","grouped-bar"},
        {"x","quarter"},
        {"y", QJsonArray{"q1","q2","q3","q4"}},
        {"data", salesRows()}
    }, "grouped-bar");
    renderOk(QJsonObject{
        {"type","donut"}, {"label","quarter"}, {"value","revenue"}, {"data", salesRows()}
    }, "donut");
    renderOk(QJsonObject{
        {"type","histogram"}, {"x","score"}, {"bins", 10}, {"data", scoreRows()}
    }, "histogram");
    renderOk(QJsonObject{
        {"type","boxplot"}, {"x","product"}, {"y","price"}, {"data", priceByProductRows()}
    }, "boxplot");

    // ── looksLikeChartSpec accepts every new type ───────────────────
    printf("\n[looksLikeChartSpec dispatcher]\n");
    const QStringList allTypes = {
        "line", "bar", "pie", "scatter",
        "area", "horizontal-bar", "stacked-bar", "stacked-horizontal-bar",
        "grouped-bar", "donut", "histogram", "boxplot"
    };
    for (const QString &t : allTypes) {
        QJsonObject probe;
        probe["type"] = t;
        probe["data"] = QJsonArray{QJsonObject{{"x","a"},{"y",1}}};
        check(ChartRender::looksLikeChartSpec(probe),
              QString("looksLikeChartSpec accepts '%1'").arg(t));
    }

    // ── Unsupported type returns error, not crash ───────────────────
    printf("\n[error paths]\n");
    {
        QJsonObject bad;
        bad["type"] = "candlestick";   // not in catalogue yet
        bad["data"] = QJsonArray{};
        QString err;
        QWidget *w = ChartRender::renderFromObject(bad, nullptr, &err);
        check(w == nullptr,                       "unsupported type → null widget");
        check(err.contains("Unsupported"),        "unsupported type → error message");
        if (w) delete w;
    }

    printf("\n=== %d/%d sub-checks passed ===\n", g_pass, g_pass + g_fail);
    return g_fail == 0 ? 0 : 1;
}
