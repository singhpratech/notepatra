#include "chartrender.h"
#include "chart_modal.h"
#include "chart_spec_to_vega.h"

#ifdef NOTEPATRA_WITH_WEBENGINE
#include "charts/vega_chart_renderer.h"
#endif

#include <QApplication>
#include <QPalette>
#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QStyle>
#include <QVBoxLayout>

#include <QtCharts/QChart>
#include <QtCharts/QChartView>
#include <QtCharts/QLineSeries>
#include <QtCharts/QAreaSeries>
#include <QtCharts/QBarSeries>
#include <QtCharts/QHorizontalBarSeries>
#include <QtCharts/QStackedBarSeries>
#include <QtCharts/QHorizontalStackedBarSeries>
#include <QtCharts/QBarSet>
#include <QtCharts/QPieSeries>
#include <QtCharts/QPieSlice>
#include <QtCharts/QScatterSeries>
#include <QtCharts/QBoxPlotSeries>
#include <QtCharts/QBoxSet>
#include <QtCharts/QValueAxis>
#include <QtCharts/QBarCategoryAxis>
#include <QtCharts/QCategoryAxis>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonValue>
#include <QPainter>
#include <QStringList>
#include <QVector>
#include <QMap>
#include <QToolTip>
#include <QCursor>
#include <QLocale>
#include <algorithm>
#include <cmath>

QT_CHARTS_USE_NAMESPACE

namespace ChartRender {

bool looksLikeChartSpec(const QJsonObject &spec) {
    if (!spec.contains("type")) return false;
    const QString t = spec.value("type").toString().toLower();
    // v0.1.76 — expanded chart-type catalogue. All renderable via QtCharts
    // (no WebEngine), so they work in both lite and full builds.
    static const QStringList kTypes = {
        "line", "bar", "pie", "scatter",
        "area", "horizontal-bar", "stacked-bar", "stacked-horizontal-bar",
        "grouped-bar", "donut", "histogram", "boxplot",
        // v0.1.90 — Vega-Lite-only types. Lite mode shows a friendly
        // "Install Charts Pack" stub via renderFromObject's fallback
        // arm instead of silently dropping.
        "heatmap", "density", "regression-line", "faceted-bar", "error-bar",
    };
    if (!kTypes.contains(t)) return false;
    if (!spec.contains("data") || !spec.value("data").isArray()) return false;
    return true;
}

// ── Helpers ──────────────────────────────────────────────────────────

static bool toDouble(const QJsonValue &v, double *out) {
    if (v.isDouble()) { *out = v.toDouble(); return true; }
    if (v.isString()) {
        bool ok = false;
        double d = v.toString().toDouble(&ok);
        if (ok) { *out = d; return true; }
    }
    if (v.isBool()) { *out = v.toBool() ? 1.0 : 0.0; return true; }
    return false;
}

static QString toLabel(const QJsonValue &v) {
    if (v.isString()) return v.toString();
    if (v.isDouble()) return QString::number(v.toDouble());
    if (v.isBool())   return v.toBool() ? "true" : "false";
    if (v.isNull())   return QString();
    return QJsonDocument(QJsonObject{{"v", v}}).toJson(QJsonDocument::Compact);
}

// v0.1.90 — categorical palette for top-N bar rankings. Tableau 20 plus
// a couple of extras so 3–20 colours cover the common ranking sizes.
// Picked for legibility on both light and dark chart backgrounds.
static const QStringList &chartPalette() {
    static const QStringList kPalette = {
        "#4E79A7", "#F28E2B", "#E15759", "#76B7B2", "#59A14F",
        "#EDC948", "#B07AA1", "#FF9DA7", "#9C755F", "#BAB0AC",
        "#86BCB6", "#D37295", "#A0CBE8", "#FFBE7D", "#8CD17D",
        "#B6992D", "#499894", "#FABFD2", "#79706E", "#D7B5A6",
    };
    return kPalette;
}

// v0.1.90 — auto-tilt rule for categorical x-axes. Vertical labels look
// fine when there are few short categories ("Q1 Q2 Q3 Q4"); they overlap
// or truncate as soon as you cross either threshold.
static bool shouldTiltLabels(const QStringList &cats) {
    if (cats.size() > 5) return true;
    for (const QString &c : cats) {
        if (c.size() > 8) return true;
    }
    return false;
}

// Default theme — dark-ish but readable on light too. We pick the
// neutral "ChartThemeBlueCerulean" which renders well on both light
// and dark backgrounds.

// ── Per-type renderers ───────────────────────────────────────────────

static QChart *renderLineOrScatter(const QJsonObject &spec,
                                   bool scatter,
                                   QString *outError) {
    const QString xCol = spec.value("x").toString();
    const QString yCol = spec.value("y").toString();
    if (xCol.isEmpty() || yCol.isEmpty()) {
        if (outError) *outError = "line/scatter spec needs 'x' and 'y' column names.";
        return nullptr;
    }
    const QJsonArray data = spec.value("data").toArray();
    if (data.isEmpty()) {
        if (outError) *outError = "data array is empty.";
        return nullptr;
    }

    auto *chart = new QChart;
    chart->setAnimationOptions(QChart::SeriesAnimations);
    if (spec.contains("title"))
        chart->setTitle(spec.value("title").toString());

    // First pass — figure out if X is categorical (strings) or numeric.
    // If first non-null X is a string, treat all as categorical.
    bool xIsCategorical = false;
    for (const QJsonValue &row : data) {
        if (!row.isObject()) continue;
        const QJsonValue xv = row.toObject().value(xCol);
        if (xv.isString()) { xIsCategorical = true; break; }
        double xd;
        if (toDouble(xv, &xd)) { xIsCategorical = false; break; }
    }

    if (xIsCategorical) {
        // Numeric x positions = 0, 1, 2, ... with category labels.
        QStringList categories;
        if (scatter) {
            auto *series = new QScatterSeries;
            series->setName(yCol);
            int idx = 0;
            for (const QJsonValue &row : data) {
                const QJsonObject ro = row.toObject();
                double y;
                if (!toDouble(ro.value(yCol), &y)) continue;
                series->append(idx, y);
                categories.append(toLabel(ro.value(xCol)));
                ++idx;
            }
            chart->addSeries(series);
        } else {
            auto *series = new QLineSeries;
            series->setName(yCol);
            int idx = 0;
            for (const QJsonValue &row : data) {
                const QJsonObject ro = row.toObject();
                double y;
                if (!toDouble(ro.value(yCol), &y)) continue;
                series->append(idx, y);
                categories.append(toLabel(ro.value(xCol)));
                ++idx;
            }
            chart->addSeries(series);
        }
        auto *xAxis = new QCategoryAxis;
        for (int i = 0; i < categories.size(); ++i)
            xAxis->append(categories[i], i);
        xAxis->setTitleText(xCol);
        xAxis->setLabelsAngle(-45);
        chart->addAxis(xAxis, Qt::AlignBottom);
        for (auto *s : chart->series()) s->attachAxis(xAxis);
    } else {
        if (scatter) {
            auto *series = new QScatterSeries;
            series->setName(yCol);
            for (const QJsonValue &row : data) {
                const QJsonObject ro = row.toObject();
                double x, y;
                if (!toDouble(ro.value(xCol), &x)) continue;
                if (!toDouble(ro.value(yCol), &y)) continue;
                series->append(x, y);
            }
            chart->addSeries(series);
        } else {
            auto *series = new QLineSeries;
            series->setName(yCol);
            for (const QJsonValue &row : data) {
                const QJsonObject ro = row.toObject();
                double x, y;
                if (!toDouble(ro.value(xCol), &x)) continue;
                if (!toDouble(ro.value(yCol), &y)) continue;
                series->append(x, y);
            }
            chart->addSeries(series);
        }
        auto *xAxis = new QValueAxis;
        xAxis->setTitleText(xCol);
        chart->addAxis(xAxis, Qt::AlignBottom);
        for (auto *s : chart->series()) s->attachAxis(xAxis);
    }
    auto *yAxis = new QValueAxis;
    yAxis->setTitleText(yCol);
    chart->addAxis(yAxis, Qt::AlignLeft);
    for (auto *s : chart->series()) s->attachAxis(yAxis);

    chart->legend()->setVisible(true);
    chart->legend()->setAlignment(Qt::AlignBottom);
    return chart;
}

static QChart *renderBar(const QJsonObject &spec, QString *outError) {
    const QString xCol = spec.value("x").toString();
    const QString yCol = spec.value("y").toString();
    if (xCol.isEmpty() || yCol.isEmpty()) {
        if (outError) *outError = "bar spec needs 'x' and 'y' column names.";
        return nullptr;
    }
    const QJsonArray data = spec.value("data").toArray();
    if (data.isEmpty()) {
        if (outError) *outError = "data array is empty.";
        return nullptr;
    }

    QStringList categories;
    QVector<double> values;
    for (const QJsonValue &row : data) {
        const QJsonObject ro = row.toObject();
        double y;
        if (!toDouble(ro.value(yCol), &y)) continue;
        categories.append(toLabel(ro.value(xCol)));
        values.append(y);
    }
    const int N = categories.size();
    if (N == 0) {
        if (outError) *outError = "no usable rows for bar chart.";
        return nullptr;
    }

    // v0.1.90 — keep vertical-bar layout (don't silently switch
    // chart type, that hides the user's title and disorients them).
    // Long labels are handled below via -90° rotation + an explicit
    // bottom-margin reservation so the rotated labels have room
    // to read in full.
    bool labelsTooLong = false;
    int maxLabelLen = 0;
    for (const QString &c : categories) {
        if (c.size() > maxLabelLen) maxLabelLen = c.size();
        if (c.size() > 10) labelsTooLong = true;
    }

    // v0.1.90 — multi-colour rule for ranking bar charts. When a single-
    // metric `bar` has 3–20 categories, paint each bar in its own palette
    // colour so the visual scans as a ranking comparison rather than a
    // uniform-blue uniform-blue. Implementation: N independent QBarSeries
    // each containing one QBarSet with values [0,…,v_i,…,0]; only the
    // i-th bar at category i is visible. Each series gets its own
    // palette colour. Legend is hidden — the x-axis labels already
    // identify the bars.
    const bool inRange = (N >= 3 && N <= 20);
    const bool optedOut = spec.contains("multiColor")
                          && spec.value("multiColor").isBool()
                          && !spec.value("multiColor").toBool();
    const bool useMultiColor = inRange && !optedOut;

    auto *chart = new QChart;
    chart->setAnimationOptions(QChart::SeriesAnimations);
    if (spec.contains("title"))
        chart->setTitle(spec.value("title").toString());

    auto *xAxis = new QBarCategoryAxis;
    xAxis->append(categories);
    xAxis->setTitleText(xCol);

    // v0.1.90 — long-label vertical-bar polish. Three coordinated
    // adjustments so 30+ char names don't get clipped:
    //   1. -90° rotation (truly vertical labels — budget is bottom
    //      margin height, not slot width).
    //   2. 9 pt label font (~6 px per char vs. ~8 px at default 11 pt).
    //   3. Explicit bottom margin that scales with the longest label.
    if (labelsTooLong) {
        xAxis->setLabelsAngle(-90);
        QFont labelFont = xAxis->labelsFont();
        labelFont.setPointSize(9);
        xAxis->setLabelsFont(labelFont);
        const int charW = 6;
        const int needed = 60 + maxLabelLen * charW;
        chart->setMargins(QMargins(20, 20, 20, qMin(260, needed)));
    } else if (shouldTiltLabels(categories)) {
        xAxis->setLabelsAngle(-45);
    }
    chart->addAxis(xAxis, Qt::AlignBottom);

    auto *yAxis = new QValueAxis;
    yAxis->setTitleText(yCol);
    // Comma-grouped integer ticks for readability — "308,602"
    // instead of "308602.0". POSIX apostrophe flag enables locale
    // thousand-separators; falls back to plain integers on systems
    // whose printf doesn't honour the flag.
    yAxis->setLabelFormat("%'.0f");
    chart->addAxis(yAxis, Qt::AlignLeft);

    if (useMultiColor) {
        const QStringList &palette = chartPalette();
        for (int i = 0; i < N; ++i) {
            auto *set = new QBarSet(categories[i]);
            for (int j = 0; j < N; ++j) *set << (j == i ? values[i] : 0.0);
            set->setColor(QColor(palette[i % palette.size()]));
            auto *series = new QBarSeries;
            series->append(set);
            series->setBarWidth(0.9);
            chart->addSeries(series);
            series->attachAxis(xAxis);
            series->attachAxis(yAxis);
        }
        chart->legend()->setVisible(false);
    } else {
        auto *set = new QBarSet(yCol);
        for (double v : values) *set << v;
        auto *series = new QBarSeries;
        series->append(set);
        chart->addSeries(series);
        series->attachAxis(xAxis);
        series->attachAxis(yAxis);
        chart->legend()->setVisible(true);
        chart->legend()->setAlignment(Qt::AlignBottom);
    }
    return chart;
}

static QChart *renderPie(const QJsonObject &spec, QString *outError) {
    QString labelCol = spec.value("label").toString();
    QString valueCol = spec.value("value").toString();
    if (labelCol.isEmpty()) labelCol = spec.value("x").toString();
    if (valueCol.isEmpty()) valueCol = spec.value("y").toString();
    if (labelCol.isEmpty() || valueCol.isEmpty()) {
        if (outError) *outError = "pie spec needs 'label' + 'value' (or 'x' + 'y') column names.";
        return nullptr;
    }
    const QJsonArray data = spec.value("data").toArray();
    if (data.isEmpty()) {
        if (outError) *outError = "data array is empty.";
        return nullptr;
    }

    auto *series = new QPieSeries;
    for (const QJsonValue &row : data) {
        const QJsonObject ro = row.toObject();
        double v;
        if (!toDouble(ro.value(valueCol), &v)) continue;
        QPieSlice *slice = series->append(toLabel(ro.value(labelCol)), v);
        slice->setLabelVisible(true);
    }
    if (series->count() == 0) {
        delete series;
        if (outError) *outError = "no usable rows for pie chart.";
        return nullptr;
    }

    auto *chart = new QChart;
    chart->addSeries(series);
    chart->setAnimationOptions(QChart::SeriesAnimations);
    if (spec.contains("title"))
        chart->setTitle(spec.value("title").toString());
    chart->legend()->setVisible(true);
    chart->legend()->setAlignment(Qt::AlignRight);
    return chart;
}

// v0.1.76 — area chart. Categorical x or numeric x; one numeric y. Same
// shape as line, but the area below the line is filled. Visually emphasises
// magnitude / volume change over time vs raw value movement.
static QChart *renderArea(const QJsonObject &spec, QString *outError) {
    const QString xCol = spec.value("x").toString();
    const QString yCol = spec.value("y").toString();
    if (xCol.isEmpty() || yCol.isEmpty()) {
        if (outError) *outError = "area spec needs 'x' and 'y' column names.";
        return nullptr;
    }
    const QJsonArray data = spec.value("data").toArray();
    if (data.isEmpty()) {
        if (outError) *outError = "data array is empty.";
        return nullptr;
    }

    bool xIsCategorical = false;
    for (const QJsonValue &row : data) {
        if (!row.isObject()) continue;
        const QJsonValue xv = row.toObject().value(xCol);
        if (xv.isString()) { xIsCategorical = true; break; }
        double xd;
        if (toDouble(xv, &xd)) { xIsCategorical = false; break; }
    }

    auto *line = new QLineSeries;
    line->setName(yCol);
    QStringList categories;
    int idx = 0;
    for (const QJsonValue &row : data) {
        const QJsonObject ro = row.toObject();
        double y;
        if (!toDouble(ro.value(yCol), &y)) continue;
        if (xIsCategorical) {
            line->append(idx, y);
            categories.append(toLabel(ro.value(xCol)));
            ++idx;
        } else {
            double x;
            if (!toDouble(ro.value(xCol), &x)) continue;
            line->append(x, y);
        }
    }

    auto *series = new QAreaSeries(line);
    series->setName(yCol);

    auto *chart = new QChart;
    chart->addSeries(series);
    chart->setAnimationOptions(QChart::SeriesAnimations);
    if (spec.contains("title"))
        chart->setTitle(spec.value("title").toString());

    if (xIsCategorical) {
        auto *xAxis = new QCategoryAxis;
        for (int i = 0; i < categories.size(); ++i)
            xAxis->append(categories[i], i);
        xAxis->setTitleText(xCol);
        xAxis->setLabelsAngle(-45);
        chart->addAxis(xAxis, Qt::AlignBottom);
        series->attachAxis(xAxis);
    } else {
        auto *xAxis = new QValueAxis;
        xAxis->setTitleText(xCol);
        chart->addAxis(xAxis, Qt::AlignBottom);
        series->attachAxis(xAxis);
    }
    auto *yAxis = new QValueAxis;
    yAxis->setTitleText(yCol);
    chart->addAxis(yAxis, Qt::AlignLeft);
    series->attachAxis(yAxis);
    chart->legend()->setVisible(true);
    chart->legend()->setAlignment(Qt::AlignBottom);
    return chart;
}

// v0.1.76 — horizontal-bar. Same data shape as bar, but bars run left
// → right. Better for many categories or long category labels.
static QChart *renderHorizontalBar(const QJsonObject &spec, QString *outError) {
    const QString xCol = spec.value("x").toString();
    const QString yCol = spec.value("y").toString();
    if (xCol.isEmpty() || yCol.isEmpty()) {
        if (outError) *outError = "horizontal-bar spec needs 'x' (category) and 'y' (value) column names.";
        return nullptr;
    }
    const QJsonArray data = spec.value("data").toArray();
    if (data.isEmpty()) { if (outError) *outError = "data array is empty."; return nullptr; }

    QStringList categories;
    QVector<double> values;
    for (const QJsonValue &row : data) {
        const QJsonObject ro = row.toObject();
        double y;
        if (!toDouble(ro.value(yCol), &y)) continue;
        categories.append(toLabel(ro.value(xCol)));
        values.append(y);
    }
    const int N = categories.size();
    if (N == 0) {
        if (outError) *outError = "no usable rows for horizontal-bar chart.";
        return nullptr;
    }
    const bool inRange = (N >= 3 && N <= 20);
    const bool optedOut = spec.contains("multiColor")
                          && spec.value("multiColor").isBool()
                          && !spec.value("multiColor").toBool();
    const bool useMultiColor = inRange && !optedOut;

    auto *chart = new QChart;
    chart->setAnimationOptions(QChart::SeriesAnimations);
    if (spec.contains("title")) chart->setTitle(spec.value("title").toString());

    auto *yAxis = new QBarCategoryAxis;
    yAxis->append(categories);
    yAxis->setTitleText(xCol);
    chart->addAxis(yAxis, Qt::AlignLeft);
    auto *xAxis = new QValueAxis;
    xAxis->setTitleText(yCol);
    chart->addAxis(xAxis, Qt::AlignBottom);

    if (useMultiColor) {
        const QStringList &palette = chartPalette();
        for (int i = 0; i < N; ++i) {
            auto *set = new QBarSet(categories[i]);
            for (int j = 0; j < N; ++j) *set << (j == i ? values[i] : 0.0);
            set->setColor(QColor(palette[i % palette.size()]));
            auto *series = new QHorizontalBarSeries;
            series->append(set);
            series->setBarWidth(0.9);
            chart->addSeries(series);
            series->attachAxis(yAxis);
            series->attachAxis(xAxis);
        }
        chart->legend()->setVisible(false);
    } else {
        auto *set = new QBarSet(yCol);
        for (double v : values) *set << v;
        auto *series = new QHorizontalBarSeries;
        series->append(set);
        chart->addSeries(series);
        series->attachAxis(yAxis);
        series->attachAxis(xAxis);
        chart->legend()->setVisible(true);
        chart->legend()->setAlignment(Qt::AlignBottom);
    }
    return chart;
}

// v0.1.76 — stacked-bar + grouped-bar. Multi-y: spec.y is an ARRAY of
// column names; one QBarSet per column. Stacked uses QStackedBarSeries,
// grouped uses regular QBarSeries with multiple sets. `horizontal:true`
// switches both to their horizontal cousins.
//
// Spec example:
//   {"type":"stacked-bar","x":"quarter","y":["product_a","product_b"],
//    "data":[{"quarter":"Q1","product_a":10,"product_b":4}, ...]}
static QChart *renderMultiBar(const QJsonObject &spec,
                              bool stacked,
                              QString *outError) {
    const QString xCol = spec.value("x").toString();
    if (xCol.isEmpty()) {
        if (outError) *outError = "multi-bar spec needs 'x' (category) column name.";
        return nullptr;
    }
    QStringList yCols;
    const QJsonValue yVal = spec.value("y");
    if (yVal.isArray()) {
        for (const QJsonValue &c : yVal.toArray())
            if (c.isString()) yCols.append(c.toString());
    } else if (yVal.isString()) {
        yCols.append(yVal.toString());
    }
    if (yCols.isEmpty()) {
        if (outError) *outError = "multi-bar spec needs 'y' = array of value column names.";
        return nullptr;
    }
    const QJsonArray data = spec.value("data").toArray();
    if (data.isEmpty()) { if (outError) *outError = "data array is empty."; return nullptr; }
    const bool horizontal = spec.value("horizontal").toBool();

    QStringList categories;
    QVector<QBarSet *> sets;
    for (const QString &c : yCols) sets.append(new QBarSet(c));

    for (const QJsonValue &row : data) {
        const QJsonObject ro = row.toObject();
        categories.append(toLabel(ro.value(xCol)));
        for (int i = 0; i < yCols.size(); ++i) {
            double v = 0.0;
            toDouble(ro.value(yCols[i]), &v);
            *sets[i] << v;
        }
    }

    QAbstractBarSeries *series = nullptr;
    if (stacked) {
        series = horizontal ? static_cast<QAbstractBarSeries *>(new QHorizontalStackedBarSeries)
                            : static_cast<QAbstractBarSeries *>(new QStackedBarSeries);
    } else {
        series = horizontal ? static_cast<QAbstractBarSeries *>(new QHorizontalBarSeries)
                            : static_cast<QAbstractBarSeries *>(new QBarSeries);
    }
    for (auto *s : sets) series->append(s);

    auto *chart = new QChart;
    chart->addSeries(series);
    chart->setAnimationOptions(QChart::SeriesAnimations);
    if (spec.contains("title")) chart->setTitle(spec.value("title").toString());

    auto *catAxis = new QBarCategoryAxis;
    catAxis->append(categories);
    catAxis->setTitleText(xCol);
    auto *valAxis = new QValueAxis;
    valAxis->setTitleText(yCols.size() == 1 ? yCols.first() : QString("value"));

    if (horizontal) {
        chart->addAxis(catAxis, Qt::AlignLeft);
        chart->addAxis(valAxis, Qt::AlignBottom);
    } else {
        chart->addAxis(catAxis, Qt::AlignBottom);
        chart->addAxis(valAxis, Qt::AlignLeft);
        if (shouldTiltLabels(categories)) catAxis->setLabelsAngle(-45);
    }
    series->attachAxis(catAxis);
    series->attachAxis(valAxis);
    chart->legend()->setVisible(true);
    chart->legend()->setAlignment(Qt::AlignBottom);
    return chart;
}

// v0.1.76 — donut = pie with a hole. Pass-through to renderPie, then set
// holeSize on the pie series. Visual choice for "composition of a whole"
// without the criticized solid-pie metaphor.
static QChart *renderDonut(const QJsonObject &spec, QString *outError) {
    QChart *chart = renderPie(spec, outError);
    if (!chart) return nullptr;
    // Find the QPieSeries we just attached and set the hole.
    for (auto *s : chart->series()) {
        auto *pie = qobject_cast<QPieSeries *>(s);
        if (pie) {
            pie->setHoleSize(0.45);
        }
    }
    return chart;
}

// v0.1.76 — histogram. Auto-bins a numeric column into N bins (default 20,
// capped 2..100) and renders the bin counts as bars. Spec format:
//   {"type":"histogram","x":"value_col","bins":20?,"data":[...]}
static QChart *renderHistogram(const QJsonObject &spec, QString *outError) {
    const QString xCol = spec.value("x").toString();
    if (xCol.isEmpty()) {
        if (outError) *outError = "histogram spec needs 'x' (numeric column) name.";
        return nullptr;
    }
    const QJsonArray data = spec.value("data").toArray();
    if (data.isEmpty()) { if (outError) *outError = "data array is empty."; return nullptr; }

    int nBins = spec.value("bins").toInt(20);
    if (nBins < 2) nBins = 2;
    if (nBins > 100) nBins = 100;

    QVector<double> values;
    values.reserve(data.size());
    for (const QJsonValue &row : data) {
        if (!row.isObject()) continue;
        double v;
        if (toDouble(row.toObject().value(xCol), &v))
            values.append(v);
    }
    if (values.isEmpty()) {
        if (outError) *outError = "no numeric values in column '" + xCol + "'.";
        return nullptr;
    }
    auto mm = std::minmax_element(values.begin(), values.end());
    double lo = *mm.first;
    double hi = *mm.second;
    if (hi <= lo) { hi = lo + 1.0; }
    const double w = (hi - lo) / double(nBins);

    QVector<int> counts(nBins, 0);
    for (double v : values) {
        int idx = int((v - lo) / w);
        if (idx >= nBins) idx = nBins - 1;
        if (idx < 0) idx = 0;
        ++counts[idx];
    }

    auto *set = new QBarSet(QString("freq(%1)").arg(xCol));
    QStringList labels;
    labels.reserve(nBins);
    for (int i = 0; i < nBins; ++i) {
        *set << counts[i];
        const double a = lo + i * w;
        const double b = a + w;
        labels.append(QString("[%1, %2)")
                      .arg(QString::number(a, 'g', 3),
                           QString::number(b, 'g', 3)));
    }
    auto *series = new QBarSeries;
    series->append(set);

    auto *chart = new QChart;
    chart->addSeries(series);
    chart->setAnimationOptions(QChart::SeriesAnimations);
    chart->setTitle(spec.value("title").toString(
        QString("Distribution of %1").arg(xCol)));

    auto *xAxis = new QBarCategoryAxis;
    xAxis->append(labels);
    xAxis->setTitleText(xCol);
    xAxis->setLabelsAngle(-45);
    chart->addAxis(xAxis, Qt::AlignBottom);
    series->attachAxis(xAxis);
    auto *yAxis = new QValueAxis;
    yAxis->setTitleText("frequency");
    chart->addAxis(yAxis, Qt::AlignLeft);
    series->attachAxis(yAxis);
    chart->legend()->setVisible(false);
    return chart;
}

// v0.1.76 — boxplot. Group numeric values by category and render
// Tukey-style boxes (min / Q1 / median / Q3 / max) per group.
// Spec: {"type":"boxplot","x":"group_col","y":"numeric_col","data":[...]}
static QChart *renderBoxplot(const QJsonObject &spec, QString *outError) {
    const QString xCol = spec.value("x").toString();
    const QString yCol = spec.value("y").toString();
    if (xCol.isEmpty() || yCol.isEmpty()) {
        if (outError) *outError = "boxplot spec needs 'x' (category) and 'y' (numeric) column names.";
        return nullptr;
    }
    const QJsonArray data = spec.value("data").toArray();
    if (data.isEmpty()) { if (outError) *outError = "data array is empty."; return nullptr; }

    QMap<QString, QVector<double>> buckets;
    QStringList orderedKeys;
    for (const QJsonValue &row : data) {
        if (!row.isObject()) continue;
        const QJsonObject ro = row.toObject();
        const QString key = toLabel(ro.value(xCol));
        double v;
        if (!toDouble(ro.value(yCol), &v)) continue;
        if (!buckets.contains(key)) { buckets.insert(key, {}); orderedKeys.append(key); }
        buckets[key].append(v);
    }
    if (orderedKeys.isEmpty()) {
        if (outError) *outError = "no usable rows for boxplot.";
        return nullptr;
    }

    auto *series = new QBoxPlotSeries;
    series->setName(yCol);

    auto quantile = [](QVector<double> &v, double q) -> double {
        std::sort(v.begin(), v.end());
        if (v.isEmpty()) return 0.0;
        const double idx = q * (v.size() - 1);
        const int lo = int(std::floor(idx));
        const int hi = int(std::ceil(idx));
        if (lo == hi) return v[lo];
        const double t = idx - lo;
        return v[lo] * (1.0 - t) + v[hi] * t;
    };

    QStringList categories;
    for (const QString &key : orderedKeys) {
        QVector<double> v = buckets.value(key);
        if (v.isEmpty()) continue;
        std::sort(v.begin(), v.end());
        const double lo  = v.first();
        const double hi  = v.last();
        const double q1  = quantile(v, 0.25);
        const double med = quantile(v, 0.50);
        const double q3  = quantile(v, 0.75);
        auto *bs = new QBoxSet(lo, q1, med, q3, hi);
        bs->setLabel(key);
        series->append(bs);
        categories.append(key);
    }

    auto *chart = new QChart;
    chart->addSeries(series);
    chart->setAnimationOptions(QChart::SeriesAnimations);
    if (spec.contains("title"))
        chart->setTitle(spec.value("title").toString());

    auto *xAxis = new QBarCategoryAxis;
    xAxis->append(categories);
    xAxis->setTitleText(xCol);
    if (shouldTiltLabels(categories)) xAxis->setLabelsAngle(-45);
    chart->addAxis(xAxis, Qt::AlignBottom);
    series->attachAxis(xAxis);
    auto *yAxis = new QValueAxis;
    yAxis->setTitleText(yCol);
    chart->addAxis(yAxis, Qt::AlignLeft);
    series->attachAxis(yAxis);
    chart->legend()->setVisible(true);
    chart->legend()->setAlignment(Qt::AlignBottom);
    return chart;
}

// v0.1.90 — in-chart hover callout. Previously we used QToolTip
// (the OS popup); user feedback was that the OS-style popup feels
// detached from the chart. This version drives a styled QLabel
// parented to the QChartView so the readout lives inside the chart
// frame, follows the cursor, and matches the panel's dark/light
// theme.
//
// Layout: callout is positioned at cursor + (14, 14), clamped so it
// never spills past the view edges. Hidden when hover ends or on any
// series's hover-off.
static void wireSeriesTooltips(QChart *chart, QChartView *view,
                                QLabel *callout) {
    auto formatNum = [](double v) {
        // v0.1.90 — user-flagged: 'g' format flips to scientific
        // ("6.801e+04") for anything past 4 significant digits.
        // Switch to thousands-separated decimal: integer-shaped
        // numbers as "68,010" and fractions as "1.5" / "0.001".
        QLocale loc;
        const double absv = std::abs(v);
        // Whole-number-shaped values (within float rounding tolerance)
        // render as integers — e.g. 68010.0 → "68,010".
        const double tol = std::max(1.0, absv) * 1e-9;
        if (absv >= 1.0 && std::abs(v - std::round(v)) <= tol
            && absv < 1e15) {
            return loc.toString(qint64(std::round(v)));
        }
        // Fractional values — print 'f' with up to 4 decimals, then
        // trim trailing zeros so "1.5000" becomes "1.5" but "1.5025"
        // stays full.
        QString s = loc.toString(v, 'f', 4);
        if (s.contains(loc.decimalPoint())) {
            while (s.endsWith('0')) s.chop(1);
            if (s.endsWith(loc.decimalPoint())) s.chop(1);
        }
        return s;
    };

    auto showCallout = [view, callout](const QString &text) {
        callout->setText(text);
        callout->adjustSize();
        QPoint p = view->mapFromGlobal(QCursor::pos()) + QPoint(14, 14);
        // Keep the callout inside the view; flip to the left of the
        // cursor when we'd otherwise overflow the right edge.
        if (p.x() + callout->width() > view->width()) {
            p.setX(view->width() - callout->width() - 4);
        }
        if (p.y() + callout->height() > view->height()) {
            p.setY(view->height() - callout->height() - 4);
        }
        p.setX(qMax(2, p.x()));
        p.setY(qMax(2, p.y()));
        callout->move(p);
        callout->show();
        callout->raise();
    };
    auto hideCallout = [callout]() { callout->hide(); };

    // Pre-resolve category labels from either axis type. QBarCategoryAxis
    // is used by bar / horizontal-bar / stacked / grouped / histogram /
    // boxplot. QCategoryAxis is used by line / area / scatter when x is
    // a string column.
    auto collectCategories = [](QChart *c) -> QStringList {
        for (auto orient : {Qt::Horizontal, Qt::Vertical}) {
            for (auto *ax : c->axes(orient)) {
                if (auto *bca = qobject_cast<QBarCategoryAxis*>(ax)) {
                    return bca->categories();
                }
                if (auto *ca = qobject_cast<QCategoryAxis*>(ax)) {
                    return ca->categoriesLabels();
                }
            }
        }
        return {};
    };
    const QStringList catLabels = collectCategories(chart);

    // Pre-compute per-category totals when a series has ≥ 2 bar sets
    // (stacked / grouped). Lets every bar tooltip show share-of-total.
    QVector<double> catTotals;
    for (auto *s : chart->series()) {
        auto *bars = qobject_cast<QAbstractBarSeries*>(s);
        if (!bars || bars->barSets().size() < 2) continue;
        for (auto *bset : bars->barSets()) {
            for (int i = 0; i < bset->count(); ++i) {
                if (i >= catTotals.size()) catTotals.resize(i + 1);
                catTotals[i] += bset->at(i);
            }
        }
        break;
    }

    for (QAbstractSeries *s : chart->series()) {
        if (auto *bars = qobject_cast<QAbstractBarSeries*>(s)) {
            const bool multiSet = bars->barSets().size() >= 2;
            QObject::connect(bars, &QAbstractBarSeries::hovered, chart,
                [bars, catLabels, formatNum, catTotals, multiSet,
                 showCallout, hideCallout](
                    bool status, int index, QBarSet *barset) {
                    if (!status || !barset) { hideCallout(); return; }
                    const double v = barset->at(index);
                    // Skip the zero-height ghost bars that the multi-
                    // colour ranking layout produces. They emit hover
                    // events but have no visual under the cursor.
                    if (qFuzzyIsNull(v)) { hideCallout(); return; }
                    const QString cat = (index >= 0 && index < catLabels.size())
                                          ? catLabels.at(index)
                                          : QString::number(index);
                    QString text = QString("<b>%1</b><br>%2: %3")
                        .arg(barset->label().isEmpty() ? bars->name() : barset->label())
                        .arg(cat)
                        .arg(formatNum(v));
                    if (multiSet && index >= 0 && index < catTotals.size()
                        && catTotals[index] > 0.0) {
                        text += QString("<br>%1% of %2 total")
                                  .arg(QString::number(100.0 * v / catTotals[index],
                                                       'f', 1))
                                  .arg(formatNum(catTotals[index]));
                    }
                    showCallout(text);
                });
        } else if (auto *box = qobject_cast<QBoxPlotSeries*>(s)) {
            QObject::connect(box, &QBoxPlotSeries::hovered, chart,
                [box, formatNum, showCallout, hideCallout](bool status, QBoxSet *bs) {
                    if (!status || !bs) { hideCallout(); return; }
                    showCallout(
                        QString("<b>%1</b><br>min %2 · Q1 %3<br>med %4<br>Q3 %5 · max %6")
                            .arg(bs->label())
                            .arg(formatNum(bs->at(QBoxSet::LowerExtreme)))
                            .arg(formatNum(bs->at(QBoxSet::LowerQuartile)))
                            .arg(formatNum(bs->at(QBoxSet::Median)))
                            .arg(formatNum(bs->at(QBoxSet::UpperQuartile)))
                            .arg(formatNum(bs->at(QBoxSet::UpperExtreme))));
                });
        } else if (auto *pie = qobject_cast<QPieSeries*>(s)) {
            QObject::connect(pie, &QPieSeries::hovered, chart,
                [pie, formatNum, showCallout, hideCallout](QPieSlice *slice, bool state) {
                    if (!state || !slice) { hideCallout(); return; }
                    showCallout(
                        QString("<b>%1</b><br>%2 (%3%)")
                            .arg(slice->label())
                            .arg(formatNum(slice->value()))
                            .arg(QString::number(slice->percentage() * 100.0, 'f', 1)));
                });
        } else if (auto *area = qobject_cast<QAreaSeries*>(s)) {
            // QAreaSeries is not a QXYSeries; its upperSeries() is. Make
            // the upper-line's points visible so hover targets are easy
            // to hit, then wire the QXYSeries hover signal.
            if (auto *up = area->upperSeries()) {
                up->setPointsVisible(true);
                QObject::connect(up, &QXYSeries::hovered, chart,
                    [area, catLabels, formatNum, showCallout, hideCallout](
                        QPointF p, bool state) {
                        if (!state) { hideCallout(); return; }
                        const int idx = int(std::round(p.x()));
                        const QString xLabel = (!catLabels.isEmpty()
                                                 && idx >= 0
                                                 && idx < catLabels.size())
                                                ? catLabels.at(idx)
                                                : formatNum(p.x());
                        showCallout(
                            QString("<b>%1</b><br>x %2 · y %3")
                                .arg(area->name())
                                .arg(xLabel)
                                .arg(formatNum(p.y())));
                    });
            }
        } else if (auto *xy = qobject_cast<QXYSeries*>(s)) {
            // QLineSeries + QScatterSeries inherit from QXYSeries.
            xy->setPointsVisible(true);
            QObject::connect(xy, &QXYSeries::hovered, chart,
                [xy, catLabels, formatNum, showCallout, hideCallout](
                    QPointF p, bool state) {
                    if (!state) { hideCallout(); return; }
                    const int idx = int(std::round(p.x()));
                    const QString xLabel = (!catLabels.isEmpty()
                                             && idx >= 0
                                             && idx < catLabels.size())
                                            ? catLabels.at(idx)
                                            : formatNum(p.x());
                    showCallout(
                        QString("<b>%1</b><br>x %2 · y %3")
                            .arg(xy->name())
                            .arg(xLabel)
                            .arg(formatNum(p.y())));
                });
        }
    }
}

// v0.1.90 — current Notepatra theme, in the shape the translator wants.
// Source of truth: the active QApplication palette. We don't include
// config.h here on purpose — chartrender.cpp is linked into the
// chart-renderer regression tests, which don't pull in Config.
static ChartSpecToVega::Theme currentChartTheme() {
    const QPalette pal = QApplication::palette();
    const bool dark = pal.color(QPalette::Window).lightness() < 128;
    return ChartSpecToVega::Theme{dark, dark ? QStringLiteral("Dark")
                                             : QStringLiteral("Light")};
}

#ifdef NOTEPATRA_WITH_WEBENGINE
// v0.1.90 — Vega-Lite path. Wraps a VegaChartRenderer in the same kind
// of frame the QtCharts path uses (transparent background + optional
// expand button) so callers don't care which renderer they got.
static QWidget *buildVegaWrap(const QJsonObject &vegaSpec,
                              const QJsonObject &originalSpec,
                              QWidget *parent,
                              bool withExpandButton) {
    auto *wrap = new QFrame(parent);
    wrap->setStyleSheet("background: transparent;");
    wrap->setProperty("notepatra-chart-kind", "vega");
    auto *wrapLay = new QVBoxLayout(wrap);
    wrapLay->setContentsMargins(0, 0, 0, 0);
    wrapLay->setSpacing(2);

    if (withExpandButton) {
        auto *toolbar = new QHBoxLayout();
        toolbar->setContentsMargins(0, 0, 2, 0);
        toolbar->addStretch();
        auto *expandBtn = new QPushButton(wrap);
        expandBtn->setIcon(qApp->style()->standardIcon(QStyle::SP_TitleBarMaxButton));
        expandBtn->setToolTip(QObject::tr("View larger · export PNG/SVG/HTML/Spec"));
        expandBtn->setFixedSize(22, 22);
        expandBtn->setFlat(true);
        expandBtn->setCursor(Qt::PointingHandCursor);
        toolbar->addWidget(expandBtn);
        wrapLay->addLayout(toolbar);

        QJsonObject capturedSpec = originalSpec;
        QObject::connect(expandBtn, &QPushButton::clicked, wrap,
            [wrap, capturedSpec]() {
                auto *dlg = new ChartModalDialog(capturedSpec, wrap);
                dlg->setAttribute(Qt::WA_DeleteOnClose);
                dlg->show();
                dlg->raise();
                dlg->activateWindow();
            });
    }

    auto *renderer = new VegaChartRenderer(wrap);
    renderer->setMinimumHeight(320);
    renderer->setSpec(vegaSpec);
    wrap->setProperty("notepatra-vega-renderer-ptr",
                      QVariant::fromValue<QObject *>(renderer));
    wrapLay->addWidget(renderer);
    return wrap;
}
#endif  // NOTEPATRA_WITH_WEBENGINE

QWidget *renderFromObject(const QJsonObject &spec,
                          QWidget *parent,
                          QString *outError,
                          bool withExpandButton) {
    const QString type = spec.value("type").toString().toLower();

#ifdef NOTEPATRA_WITH_WEBENGINE
    // Vega path is the preferred renderer when WebEngine is bundled.
    if (ChartSpecToVega::supported(type)) {
        QString translateErr;
        QJsonObject vega = ChartSpecToVega::translate(
            spec, currentChartTheme(), &translateErr);
        if (!vega.isEmpty()) {
            return buildVegaWrap(vega, spec, parent, withExpandButton);
        }
        // Translator declined this spec (data-shape error). Fall through
        // to QtCharts so the user still sees something — QtCharts will
        // surface its own error message if it also fails.
        if (outError && !translateErr.isEmpty()) *outError = translateErr;
    }
#endif

    QChart *chart = nullptr;
    if (type == "line")                          chart = renderLineOrScatter(spec, false, outError);
    else if (type == "scatter")                  chart = renderLineOrScatter(spec, true,  outError);
    else if (type == "bar")                      chart = renderBar(spec, outError);
    else if (type == "pie")                      chart = renderPie(spec, outError);
    // v0.1.76 — new chart types. All routed through QtCharts so they
    // work in both lite and full builds (no WebEngine required).
    else if (type == "area")                     chart = renderArea(spec, outError);
    else if (type == "horizontal-bar")           chart = renderHorizontalBar(spec, outError);
    else if (type == "stacked-bar")              chart = renderMultiBar(spec, true,  outError);
    else if (type == "stacked-horizontal-bar")   { /* horizontal-stacked: explicit horizontal flag */
        QJsonObject patched = spec; patched.insert("horizontal", true);
        chart = renderMultiBar(patched, true, outError);
    }
    else if (type == "grouped-bar")              chart = renderMultiBar(spec, false, outError);
    else if (type == "donut")                    chart = renderDonut(spec, outError);
    else if (type == "histogram")                chart = renderHistogram(spec, outError);
    else if (type == "boxplot")                  chart = renderBoxplot(spec, outError);
    else if (type == "heatmap" || type == "density" ||
             type == "regression-line" || type == "faceted-bar" ||
             type == "error-bar") {
        // v0.1.90 — these are Vega-Lite-only chart types. They reach
        // this branch only on lite builds (no WebEngine), so the user
        // needs the Charts Pack to render them.
        if (outError) {
            *outError = QString("%1 charts need the Charts Pack — install "
                                "via Tools → Install Charts Pack.").arg(type);
        }
        return nullptr;
    }
    else {
        if (outError) *outError = QString("Unsupported chart type: %1").arg(type);
        return nullptr;
    }
    if (!chart) return nullptr;

    // v0.1.76 — wrap the QChartView in a small frame with a top-right
    // "view larger" button. Click opens ChartModalDialog with the same
    // spec re-rendered at 960×640 + a "Save as PNG…" action. Done as a
    // wrap rather than a property on QChartView so callers that just
    // addWidget() it into a chat bubble layout don't need any change.
    auto *wrap = new QFrame(parent);
    wrap->setStyleSheet("background: transparent;");
    wrap->setProperty("notepatra-chart-kind", "qtcharts");
    auto *wrapLay = new QVBoxLayout(wrap);
    wrapLay->setContentsMargins(0, 0, 0, 0);
    wrapLay->setSpacing(2);

    QPushButton *expandBtn = nullptr;
    if (withExpandButton) {
        auto *toolbar = new QHBoxLayout();
        toolbar->setContentsMargins(0, 0, 2, 0);
        toolbar->addStretch();
        expandBtn = new QPushButton(wrap);
        expandBtn->setIcon(qApp->style()->standardIcon(QStyle::SP_TitleBarMaxButton));
        expandBtn->setToolTip(QObject::tr("View larger · save as PNG · copy"));
        expandBtn->setFixedSize(22, 22);
        expandBtn->setFlat(true);
        expandBtn->setCursor(Qt::PointingHandCursor);
        toolbar->addWidget(expandBtn);
        wrapLay->addLayout(toolbar);
    }

    auto *view = new QChartView(chart, wrap);
    view->setRenderHint(QPainter::Antialiasing);
    view->setMinimumHeight(260);
    view->setMinimumWidth(360);
    wrapLay->addWidget(view);

    // v0.1.90 — in-chart hover callout. Parented to the QChartView so
    // it floats over the chart canvas. Theme-aware: dark mode is the
    // dominant Notepatra theme for the chat panel, so the defaults
    // here are tuned for dark; on light themes the callout still
    // reads well thanks to the explicit foreground colour.
    auto *callout = new QLabel(view);
    callout->setObjectName("chartCallout");
    callout->setTextFormat(Qt::RichText);
    callout->setAttribute(Qt::WA_TransparentForMouseEvents);
    callout->setStyleSheet(
        "QLabel#chartCallout {"
        "  background: rgba(36, 36, 38, 235);"
        "  color: #F0F0F0;"
        "  border: 1px solid #4EC9B0;"
        "  border-radius: 6px;"
        "  padding: 6px 10px;"
        "  font-size: 11px;"
        "}");
    callout->setWordWrap(false);
    callout->hide();

    // v0.1.76 — wire hover tooltips onto every series after the chart is
    // built. Categorical-x bars resolve the index → label via the
    // chart's QBarCategoryAxis, so the call must happen after axes are
    // attached (which all the renderXxx helpers do before returning).
    wireSeriesTooltips(chart, view, callout);

    // Capture the spec by value so the modal can re-render even after
    // the original aipanel transcript is cleared / scrolled away.
    if (expandBtn) {
        QJsonObject capturedSpec = spec;
        QObject::connect(expandBtn, &QPushButton::clicked, wrap,
            [wrap, capturedSpec]() {
                auto *dlg = new ChartModalDialog(capturedSpec, wrap);
                dlg->setAttribute(Qt::WA_DeleteOnClose);
                dlg->show();
                dlg->raise();
                dlg->activateWindow();
            });
    }

    return wrap;
}

QWidget *renderFromSpec(const QString &jsonText,
                        QWidget *parent,
                        QString *outError) {
    QJsonParseError err;
    QJsonDocument doc = QJsonDocument::fromJson(jsonText.toUtf8(), &err);
    if (err.error != QJsonParseError::NoError) {
        if (outError) *outError = QString("Chart spec JSON parse error: %1")
                                       .arg(err.errorString());
        return nullptr;
    }
    if (!doc.isObject()) {
        if (outError) *outError = "Chart spec must be a JSON object.";
        return nullptr;
    }
    return renderFromObject(doc.object(), parent, outError);
}

} // namespace ChartRender
