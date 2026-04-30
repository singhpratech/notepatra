#include "chartrender.h"

#include <QtCharts/QChart>
#include <QtCharts/QChartView>
#include <QtCharts/QLineSeries>
#include <QtCharts/QBarSeries>
#include <QtCharts/QBarSet>
#include <QtCharts/QPieSeries>
#include <QtCharts/QPieSlice>
#include <QtCharts/QScatterSeries>
#include <QtCharts/QValueAxis>
#include <QtCharts/QBarCategoryAxis>
#include <QtCharts/QCategoryAxis>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonValue>
#include <QPainter>
#include <QStringList>

QT_CHARTS_USE_NAMESPACE

namespace ChartRender {

bool looksLikeChartSpec(const QJsonObject &spec) {
    if (!spec.contains("type")) return false;
    const QString t = spec.value("type").toString().toLower();
    static const QStringList kTypes = {"line", "bar", "pie", "scatter"};
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

QWidget *renderFromObject(const QJsonObject &spec,
                          QWidget *parent,
                          QString *outError) {
    const QString type = spec.value("type").toString().toLower();
    QChart *chart = nullptr;
    if (type == "line")         chart = renderLineOrScatter(spec, false, outError);
    else if (type == "scatter") chart = renderLineOrScatter(spec, true,  outError);
    else if (type == "bar")     chart = renderBar(spec, outError);
    else if (type == "pie")     chart = renderPie(spec, outError);
    else {
        if (outError) *outError = QString("Unsupported chart type: %1").arg(type);
        return nullptr;
    }
    if (!chart) return nullptr;

    auto *view = new QChartView(chart, parent);
    view->setRenderHint(QPainter::Antialiasing);
    view->setMinimumHeight(260);
    view->setMinimumWidth(360);
    return view;
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
