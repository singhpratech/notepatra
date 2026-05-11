#include "chartrender.h"
#include "chart_modal.h"

#include <QApplication>
#include <QFrame>
#include <QHBoxLayout>
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
        "grouped-bar", "donut", "histogram", "boxplot"
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

    auto *set = new QBarSet(yCol);
    QStringList categories;
    for (const QJsonValue &row : data) {
        const QJsonObject ro = row.toObject();
        double y;
        if (!toDouble(ro.value(yCol), &y)) continue;
        categories.append(toLabel(ro.value(xCol)));
        *set << y;
    }

    auto *series = new QBarSeries;
    series->append(set);

    auto *chart = new QChart;
    chart->addSeries(series);
    chart->setAnimationOptions(QChart::SeriesAnimations);
    if (spec.contains("title"))
        chart->setTitle(spec.value("title").toString());

    auto *xAxis = new QBarCategoryAxis;
    xAxis->append(categories);
    xAxis->setTitleText(xCol);
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

    auto *set = new QBarSet(yCol);
    QStringList categories;
    for (const QJsonValue &row : data) {
        const QJsonObject ro = row.toObject();
        double y;
        if (!toDouble(ro.value(yCol), &y)) continue;
        categories.append(toLabel(ro.value(xCol)));
        *set << y;
    }
    auto *series = new QHorizontalBarSeries;
    series->append(set);

    auto *chart = new QChart;
    chart->addSeries(series);
    chart->setAnimationOptions(QChart::SeriesAnimations);
    if (spec.contains("title")) chart->setTitle(spec.value("title").toString());

    // The category axis becomes vertical, the value axis horizontal.
    auto *yAxis = new QBarCategoryAxis;
    yAxis->append(categories);
    yAxis->setTitleText(xCol);
    chart->addAxis(yAxis, Qt::AlignLeft);
    series->attachAxis(yAxis);
    auto *xAxis = new QValueAxis;
    xAxis->setTitleText(yCol);
    chart->addAxis(xAxis, Qt::AlignBottom);
    series->attachAxis(xAxis);
    chart->legend()->setVisible(true);
    chart->legend()->setAlignment(Qt::AlignBottom);
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
    chart->addAxis(xAxis, Qt::AlignBottom);
    series->attachAxis(xAxis);
    auto *yAxis = new QValueAxis;
    yAxis->setTitleText(yCol);
    chart->addAxis(yAxis, Qt::AlignLeft);
    series->attachAxis(yAxis);
    chart->legend()->setVisible(false);
    return chart;
}

// v0.1.76 — hover tooltips. QtCharts emits hover signals out of the
// box but doesn't display anything; the user just sees the cursor
// change. This helper walks every series on the chart and wires the
// appropriate hover signal to a QToolTip::showText() call so hovering
// over a bar / line point / scatter point / pie slice / box / area
// point pops up "name · index/x : value" near the cursor.
//
// Categorical x charts (bar / horizontal-bar / stacked / grouped) emit
// `hovered(bool status, int index, QBarSet *barset)` — we resolve the
// category label by index out of the chart's QBarCategoryAxis. Numeric
// x charts (line / area / scatter) emit `hovered(QPointF p, bool s)`
// where p holds the data coordinates directly. Pie / donut emit
// `hovered(QPieSlice*, bool)`. Boxplot emits `hovered(bool, QBoxSet*)`.
static void wireSeriesTooltips(QChart *chart) {
    auto formatNum = [](double v) {
        // 4 significant figures, locale-aware thousands separators.
        return QLocale().toString(v, 'g', 4);
    };

    // Pre-resolve the category labels (if any) so categorical-x bar
    // hover can map index → label without re-walking the axis each event.
    QStringList catLabels;
    for (auto *ax : chart->axes(Qt::Horizontal)) {
        if (auto *bca = qobject_cast<QBarCategoryAxis*>(ax)) {
            catLabels = bca->categories();
            break;
        }
    }
    if (catLabels.isEmpty()) {
        for (auto *ax : chart->axes(Qt::Vertical)) {
            if (auto *bca = qobject_cast<QBarCategoryAxis*>(ax)) {
                catLabels = bca->categories();
                break;
            }
        }
    }

    for (QAbstractSeries *s : chart->series()) {
        if (auto *bars = qobject_cast<QAbstractBarSeries*>(s)) {
            QObject::connect(bars, &QAbstractBarSeries::hovered, chart,
                [bars, catLabels, formatNum](bool status, int index, QBarSet *barset) {
                    if (!status || !barset) { QToolTip::hideText(); return; }
                    const QString cat = (index >= 0 && index < catLabels.size())
                                          ? catLabels.at(index)
                                          : QString::number(index);
                    QToolTip::showText(QCursor::pos(),
                        QString("%1\n%2: %3")
                            .arg(barset->label().isEmpty() ? bars->name() : barset->label())
                            .arg(cat)
                            .arg(formatNum(barset->at(index))));
                });
        } else if (auto *box = qobject_cast<QBoxPlotSeries*>(s)) {
            QObject::connect(box, &QBoxPlotSeries::hovered, chart,
                [box, formatNum](bool status, QBoxSet *bs) {
                    if (!status || !bs) { QToolTip::hideText(); return; }
                    QToolTip::showText(QCursor::pos(),
                        QString("%1\nmin %2 · Q1 %3\nmed %4\nQ3 %5 · max %6")
                            .arg(bs->label())
                            .arg(formatNum(bs->at(QBoxSet::LowerExtreme)))
                            .arg(formatNum(bs->at(QBoxSet::LowerQuartile)))
                            .arg(formatNum(bs->at(QBoxSet::Median)))
                            .arg(formatNum(bs->at(QBoxSet::UpperQuartile)))
                            .arg(formatNum(bs->at(QBoxSet::UpperExtreme))));
                });
        } else if (auto *pie = qobject_cast<QPieSeries*>(s)) {
            QObject::connect(pie, &QPieSeries::hovered, chart,
                [pie, formatNum](QPieSlice *slice, bool state) {
                    if (!state || !slice) { QToolTip::hideText(); return; }
                    QToolTip::showText(QCursor::pos(),
                        QString("%1\n%2 (%3%)")
                            .arg(slice->label())
                            .arg(formatNum(slice->value()))
                            .arg(QString::number(slice->percentage() * 100.0, 'f', 1)));
                });
        } else if (auto *xy = qobject_cast<QXYSeries*>(s)) {
            // QLineSeries, QScatterSeries, and the upper-line of
            // QAreaSeries all inherit from QXYSeries. Make point
            // markers visible so the hover target is easy to hit.
            xy->setPointsVisible(true);
            QObject::connect(xy, &QXYSeries::hovered, chart,
                [xy, formatNum](QPointF p, bool state) {
                    if (!state) { QToolTip::hideText(); return; }
                    QToolTip::showText(QCursor::pos(),
                        QString("%1\nx %2 · y %3")
                            .arg(xy->name())
                            .arg(formatNum(p.x()))
                            .arg(formatNum(p.y())));
                });
        }
        // QAreaSeries itself is not a QXYSeries — its upperSeries() is.
        // We wired the upperSeries above when we walked the chart's
        // direct series list (the area's upper line is added to the
        // chart implicitly).
    }
}

QWidget *renderFromObject(const QJsonObject &spec,
                          QWidget *parent,
                          QString *outError) {
    const QString type = spec.value("type").toString().toLower();
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
    else {
        if (outError) *outError = QString("Unsupported chart type: %1").arg(type);
        return nullptr;
    }
    if (!chart) return nullptr;

    // v0.1.76 — wire hover tooltips onto every series after the chart is
    // built. Categorical-x bars resolve the index → label via the
    // chart's QBarCategoryAxis, so the call must happen after axes are
    // attached (which all the renderXxx helpers do before returning).
    wireSeriesTooltips(chart);

    // v0.1.76 — wrap the QChartView in a small frame with a top-right
    // "view larger" button. Click opens ChartModalDialog with the same
    // spec re-rendered at 960×640 + a "Save as PNG…" action. Done as a
    // wrap rather than a property on QChartView so callers that just
    // addWidget() it into a chat bubble layout don't need any change.
    auto *wrap = new QFrame(parent);
    wrap->setStyleSheet("background: transparent;");
    auto *wrapLay = new QVBoxLayout(wrap);
    wrapLay->setContentsMargins(0, 0, 0, 0);
    wrapLay->setSpacing(2);

    auto *toolbar = new QHBoxLayout();
    toolbar->setContentsMargins(0, 0, 2, 0);
    toolbar->addStretch();
    auto *expandBtn = new QPushButton(wrap);
    expandBtn->setIcon(qApp->style()->standardIcon(QStyle::SP_TitleBarMaxButton));
    expandBtn->setToolTip(QObject::tr("View larger · save as PNG · copy"));
    expandBtn->setFixedSize(22, 22);
    expandBtn->setFlat(true);
    expandBtn->setCursor(Qt::PointingHandCursor);
    toolbar->addWidget(expandBtn);
    wrapLay->addLayout(toolbar);

    auto *view = new QChartView(chart, wrap);
    view->setRenderHint(QPainter::Antialiasing);
    view->setMinimumHeight(260);
    view->setMinimumWidth(360);
    wrapLay->addWidget(view);

    // Capture the spec by value so the modal can re-render even after
    // the original aipanel transcript is cleared / scrolled away.
    QJsonObject capturedSpec = spec;
    QObject::connect(expandBtn, &QPushButton::clicked, wrap,
        [wrap, capturedSpec]() {
            // WA_DeleteOnClose so the dialog is freed when the user
            // hits Close; parent on wrap so it tracks the chat bubble.
            auto *dlg = new ChartModalDialog(capturedSpec, wrap);
            dlg->setAttribute(Qt::WA_DeleteOnClose);
            dlg->show();
            dlg->raise();
            dlg->activateWindow();
        });

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
