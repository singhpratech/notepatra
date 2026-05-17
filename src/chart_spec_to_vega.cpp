// v0.1.90 — Notepatra simplified-chart-spec → Vega-Lite v5 translator.
//
// Why this exists: the QtCharts dispatcher in chartrender.cpp has
// structural ceilings (label elision at slot-width regardless of
// rotation, no per-bar colour, no heatmaps, brittle multi-series
// hover). Routing the same simplified spec through Vega-Lite via
// QtWebEngine eliminates those bug classes outright while keeping
// the AI prompt + tool surface unchanged.
//
// The translator is a pure function: simplified spec in, Vega-Lite v5
// JSON out. No widgets, no QApplication needed — unit-testable.

#include "chart_spec_to_vega.h"

#include <QJsonArray>
#include <QJsonValue>
#include <QStringList>
#include <QSet>

namespace ChartSpecToVega {

namespace {

// ── Tableau 20 — palette shared with the QtCharts renderer for visual
//    parity between the two paths. Loops when N > 20.
const QStringList &kPalette() {
    static const QStringList p = {
        "#4E79A7", "#F28E2B", "#E15759", "#76B7B2", "#59A14F",
        "#EDC948", "#B07AA1", "#FF9DA7", "#9C755F", "#BAB0AC",
        "#86BCB6", "#D37295", "#A0CBE8", "#FFBE7D", "#8CD17D",
        "#B6992D", "#499894", "#FABFD2", "#79706E", "#D7B5A6",
    };
    return p;
}

// Decide whether a column "looks numeric" by sampling values.
bool isNumericColumn(const QJsonArray &data, const QString &col) {
    int seen = 0, numeric = 0;
    for (const auto &row : data) {
        if (!row.isObject()) continue;
        const QJsonValue v = row.toObject().value(col);
        if (v.isUndefined() || v.isNull()) continue;
        ++seen;
        if (v.isDouble()) { ++numeric; continue; }
        if (v.isString()) {
            bool ok = false;
            v.toString().toDouble(&ok);
            if (ok) ++numeric;
        }
        if (seen >= 30) break;
    }
    return seen > 0 && numeric * 2 >= seen;
}

// Date-ish column heuristic — matches ISO 8601 / typical analyst column
// names. Vega-Lite's `temporal` type triggers nicer tick formatting and
// time-aware interval selections.
bool isTemporalColumn(const QJsonArray &data, const QString &col) {
    const QString lname = col.toLower();
    if (lname.contains("date") || lname.contains("time") ||
        lname == "day" || lname == "month" || lname == "year" ||
        lname == "week" || lname == "quarter" || lname == "timestamp") {
        // Sample a few values — if they're numeric quarters like "Q1"
        // we want ordinal not temporal, so only return true when we
        // actually see date-looking strings.
        for (const auto &row : data) {
            if (!row.isObject()) continue;
            const QJsonValue v = row.toObject().value(col);
            if (!v.isString()) continue;
            const QString s = v.toString();
            if (s.size() >= 4 && s[0].isDigit() && s[1].isDigit() &&
                s[2].isDigit() && s[3].isDigit()) {
                return true;  // looks like 2024-…
            }
            return false;
        }
        return false;
    }
    return false;
}

// Currency / money / price column heuristic — Vega numberFormat hint.
bool looksLikeMoney(const QString &col) {
    const QString n = col.toLower();
    return n.contains("price") || n.contains("revenue") || n.contains("cost") ||
           n.contains("amount") || n.contains("usd") || n.contains("eur") ||
           n.contains("salary") || n.contains("profit") || n.endsWith("$");
}

// Theme-aware Vega-Lite config block. Builds once per chart, controls
// axis colours, gridline weight, legend positioning, title typography,
// view-frame stroke, and the categorical colour range used when the
// chart's `color` encoding falls back to the default.
QJsonObject buildConfig(const Theme &theme) {
    const bool dark = theme.dark;
    const QString axisFg     = dark ? "#D4D4D4" : "#2D2D2D";
    const QString axisGrid   = dark ? "#3A3A3D" : "#E8E8E8";
    const QString axisDomain = dark ? "#5C5C5F" : "#BFBFBF";
    const QString titleFg    = dark ? "#F0F0F0" : "#1A1A1A";
    const QString viewStroke = "transparent";
    const QString tooltipBg  = dark ? "#242426" : "#FFFFFF";
    const QString tooltipFg  = dark ? "#F0F0F0" : "#1A1A1A";
    const QString tooltipBd  = dark ? "#4EC9B0" : "#2E7DD8";

    QJsonArray paletteArr;
    for (const QString &c : kPalette()) paletteArr.append(c);

    QJsonObject axis;
    axis["labelColor"]  = axisFg;
    axis["titleColor"]  = axisFg;
    axis["gridColor"]   = axisGrid;
    axis["domainColor"] = axisDomain;
    axis["tickColor"]   = axisDomain;
    axis["labelFontSize"] = 11;
    axis["titleFontSize"] = 12;
    axis["labelLimit"] = 160;  // generous — Vega still wraps if needed
    axis["labelOverlap"] = "greedy";  // hide overlapping ticks gracefully

    QJsonObject legend;
    legend["labelColor"] = axisFg;
    legend["titleColor"] = axisFg;
    legend["labelFontSize"] = 11;
    legend["titleFontSize"] = 11;
    legend["padding"] = 8;

    QJsonObject title;
    title["color"]    = titleFg;
    title["fontSize"] = 14;
    title["anchor"]   = "start";
    title["offset"]   = 8;

    QJsonObject view;
    view["stroke"] = viewStroke;
    view["continuousWidth"]  = 380;
    view["continuousHeight"] = 260;

    QJsonObject range;
    range["category"] = paletteArr;

    QJsonObject background;
    QJsonObject config;
    config["background"] = "transparent";
    config["axis"]       = axis;
    config["legend"]     = legend;
    config["title"]      = title;
    config["view"]       = view;
    config["range"]      = range;
    config["font"]       = "-apple-system, 'Segoe UI', system-ui, sans-serif";
    return config;
}

// Build the base spec — common pieces every chart inherits.
QJsonObject baseSpec(const QJsonObject &simplified, const Theme &theme) {
    QJsonObject out;
    out["$schema"] = "https://vega.github.io/schema/vega-lite/v5.json";
    if (simplified.contains("title") &&
        !simplified.value("title").toString().isEmpty()) {
        out["title"] = simplified.value("title").toString();
    }
    out["width"]  = "container";
    // Vega-Lite ignores `height: container` for layered specs in some
    // versions; an explicit pixel default is safer and the WebEngine
    // host resizes the iframe to fit.
    out["height"] = 320;
    out["data"]   = QJsonObject{{"values", simplified.value("data").toArray()}};
    out["config"] = buildConfig(theme);
    out["autosize"] = QJsonObject{
        {"type", "fit"}, {"contains", "padding"}, {"resize", true}};
    return out;
}

// Numeric-axis number format. Money columns get $,.2f, ints get ",", floats get ",.4~f".
QString axisFormat(const QString &col, const QJsonArray &data) {
    if (looksLikeMoney(col)) return "$,.2f";
    // Sample a few to decide int vs float — keep it bounded.
    bool sawFloat = false;
    int seen = 0;
    for (const auto &r : data) {
        if (!r.isObject()) continue;
        const double v = r.toObject().value(col).toDouble();
        if (v != double(qint64(v))) { sawFloat = true; break; }
        if (++seen >= 30) break;
    }
    return sawFloat ? ",.4~f" : ",";
}

// Sets x and y encodings with the right Vega types + tooltips + nice
// formatting. Used by line / area / bar / scatter / histogram.
QJsonObject buildXY(const QJsonObject &simplified,
                    const QString &xCol,
                    const QString &yCol,
                    bool tiltX,
                    bool brushable) {
    const QJsonArray data = simplified.value("data").toArray();
    QJsonObject enc;

    QJsonObject xEnc;
    xEnc["field"] = xCol;
    if (isTemporalColumn(data, xCol))       xEnc["type"] = "temporal";
    else if (isNumericColumn(data, xCol))   xEnc["type"] = "quantitative";
    else                                    xEnc["type"] = "nominal";
    xEnc["title"] = xCol;
    if (tiltX) {
        QJsonObject axis;
        axis["labelAngle"] = -35;
        axis["labelLimit"] = 220;
        xEnc["axis"] = axis;
    }
    if (xEnc["type"].toString() == "quantitative") {
        QJsonObject axis = xEnc["axis"].toObject();
        axis["format"] = axisFormat(xCol, data);
        xEnc["axis"] = axis;
    }
    enc["x"] = xEnc;

    QJsonObject yEnc;
    yEnc["field"] = yCol;
    yEnc["type"]  = "quantitative";
    yEnc["title"] = yCol;
    QJsonObject yAxis;
    yAxis["format"] = axisFormat(yCol, data);
    yEnc["axis"] = yAxis;
    enc["y"] = yEnc;

    enc["tooltip"] = QJsonArray{
        QJsonObject{{"field", xCol}, {"type", xEnc["type"].toString()}, {"title", xCol}},
        QJsonObject{{"field", yCol}, {"type", "quantitative"}, {"title", yCol},
                    {"format", axisFormat(yCol, data)}},
    };
    Q_UNUSED(brushable);
    return enc;
}

// Common interactivity — single hover + brush-zoom interval bound to scales.
QJsonObject paramBrushZoom() {
    return QJsonObject{
        {"name", "brush"},
        {"select", QJsonObject{{"type", "interval"}, {"encodings", QJsonArray{"x"}}}},
        {"bind", "scales"},
    };
}

// ── Per-type recipe ──────────────────────────────────────────────

// Pull the y spec — either a single column name (string) or an ARRAY of
// column names (for multi-metric overlay). Returns the list of columns.
QStringList collectYColumns(const QJsonValue &yVal) {
    QStringList out;
    if (yVal.isString()) { out << yVal.toString(); return out; }
    if (yVal.isArray()) {
        for (const auto &v : yVal.toArray()) out << v.toString();
    }
    return out;
}

QJsonObject mkLineOrArea(const QJsonObject &s, const Theme &th,
                         const QString &mark, QString *err) {
    const QString x = s.value("x").toString();
    const QStringList ys = collectYColumns(s.value("y"));
    const QString y2 = s.value("y2").toString();
    if (x.isEmpty() || ys.isEmpty()) {
        if (err) *err = "line/area: missing 'x' or 'y' column.";
        return {};
    }
    const QJsonArray data = s.value("data").toArray();
    QJsonObject out = baseSpec(s, th);

    // ── Case 1: single y → simple chart (today's path) ─────────────
    if (ys.size() == 1 && y2.isEmpty()) {
        QJsonObject markObj;
        markObj["type"]  = mark;
        markObj["point"] = QJsonObject{{"filled", true}, {"size", 60}};
        markObj["interpolate"] = "monotone";
        if (mark == "area") {
            markObj["opacity"] = 0.55;
            markObj["line"]    = true;
        }
        out["mark"]     = markObj;
        out["encoding"] = buildXY(s, x, ys.first(), false, true);
        out["params"]   = QJsonArray{paramBrushZoom()};
        return out;
    }

    // ── Case 2: y is array (multi-line overlay, shared Y axis) ─────
    // Use fold-transform: melt the wide rows into long {series, value}.
    if (y2.isEmpty()) {
        QJsonArray folds;
        for (const QString &c : ys) folds.append(c);
        out["transform"] = QJsonArray{
            QJsonObject{{"fold", folds}, {"as", QJsonArray{"metric", "value"}}},
        };
        QJsonObject markObj;
        markObj["type"]        = mark;
        markObj["point"]       = QJsonObject{{"filled", true}, {"size", 55}};
        markObj["interpolate"] = "monotone";
        if (mark == "area") { markObj["opacity"] = 0.45; markObj["line"] = true; }
        out["mark"]   = markObj;

        QJsonObject xEnc;
        xEnc["field"] = x;
        xEnc["type"]  = isTemporalColumn(data, x) ? "temporal"
                                                  : (isNumericColumn(data, x) ? "quantitative" : "nominal");
        xEnc["title"] = x;
        QJsonObject yEnc;
        yEnc["field"] = "value";
        yEnc["type"]  = "quantitative";
        yEnc["title"] = ys.join(" / ");
        yEnc["axis"]  = QJsonObject{{"format", ","}};
        QJsonObject colorEnc;
        colorEnc["field"] = "metric";
        colorEnc["type"]  = "nominal";
        colorEnc["legend"] = QJsonObject{{"title", "metric"}};
        out["encoding"] = QJsonObject{
            {"x", xEnc}, {"y", yEnc}, {"color", colorEnc},
            {"tooltip", QJsonArray{
                QJsonObject{{"field", x}, {"type", xEnc["type"].toString()}, {"title", x}},
                QJsonObject{{"field", "metric"}, {"type", "nominal"}},
                QJsonObject{{"field", "value"}, {"type", "quantitative"}, {"format", ","}},
            }},
        };
        out["params"] = QJsonArray{paramBrushZoom()};
        return out;
    }

    // ── Case 3: y + y2 (dual-axis, independent scales) ─────────────
    // Layered spec: one mark per axis, axis labels disambiguate.
    const QString yPrimary = ys.first();
    const QString primaryColor = th.dark ? "#4EC9B0" : "#2E7DD8";
    const QString secondColor  = th.dark ? "#F28E2B" : "#E15759";
    out.remove("mark");
    QJsonObject layer1;
    layer1["mark"] = QJsonObject{{"type", mark}, {"color", primaryColor},
                                  {"point", QJsonObject{{"filled", true}, {"size", 55}}},
                                  {"interpolate", "monotone"}};
    layer1["encoding"] = QJsonObject{
        {"x", QJsonObject{
            {"field", x},
            {"type", isTemporalColumn(data, x) ? "temporal"
                                               : (isNumericColumn(data, x) ? "quantitative" : "nominal")},
            {"title", x}}},
        {"y", QJsonObject{{"field", yPrimary}, {"type", "quantitative"},
                          {"title", yPrimary},
                          {"axis", QJsonObject{{"format", axisFormat(yPrimary, data)},
                                                 {"titleColor", primaryColor},
                                                 {"labelColor", primaryColor}}}}},
        {"tooltip", QJsonArray{
            QJsonObject{{"field", x}}, QJsonObject{{"field", yPrimary},
                {"format", axisFormat(yPrimary, data)}, {"title", yPrimary}}}},
    };
    QJsonObject layer2;
    layer2["mark"] = QJsonObject{{"type", mark}, {"color", secondColor},
                                  {"point", QJsonObject{{"filled", true}, {"size", 55}}},
                                  {"interpolate", "monotone"}};
    layer2["encoding"] = QJsonObject{
        {"x", QJsonObject{{"field", x}, {"type", "temporal"}}},
        {"y", QJsonObject{{"field", y2}, {"type", "quantitative"},
                          {"title", y2},
                          {"axis", QJsonObject{{"orient", "right"},
                                                 {"format", axisFormat(y2, data)},
                                                 {"titleColor", secondColor},
                                                 {"labelColor", secondColor}}}}},
        {"tooltip", QJsonArray{
            QJsonObject{{"field", x}}, QJsonObject{{"field", y2},
                {"format", axisFormat(y2, data)}, {"title", y2}}}},
    };
    out["layer"] = QJsonArray{layer1, layer2};
    // Independent y-scales — that's the whole point of dual-axis.
    out["resolve"] = QJsonObject{
        {"scale", QJsonObject{{"y", "independent"}}}};
    return out;
}

QJsonObject mkScatter(const QJsonObject &s, const Theme &th, QString *err) {
    const QString x = s.value("x").toString();
    const QString y = s.value("y").toString();
    if (x.isEmpty() || y.isEmpty()) {
        if (err) *err = "scatter: missing 'x' or 'y' column.";
        return {};
    }
    QJsonObject out = baseSpec(s, th);
    QJsonObject mark;
    mark["type"]    = "circle";
    mark["size"]    = 70;
    mark["opacity"] = 0.78;
    out["mark"]     = mark;
    QJsonObject enc = buildXY(s, x, y, false, true);
    // Optional color column via "color"
    const QString colorCol = s.value("color").toString();
    if (!colorCol.isEmpty()) {
        enc["color"] = QJsonObject{{"field", colorCol}, {"type", "nominal"}};
    }
    out["encoding"] = enc;
    QJsonObject brushXY{
        {"name", "brush"},
        {"select", QJsonObject{{"type", "interval"}}},
        {"bind", "scales"},
    };
    out["params"] = QJsonArray{brushXY};
    return out;
}

QJsonObject mkBar(const QJsonObject &s, const Theme &th, bool horizontal,
                  QString *err) {
    const QString x = s.value("x").toString();
    const QString y = s.value("y").toString();
    if (x.isEmpty() || y.isEmpty()) {
        if (err) *err = "bar: missing 'x' or 'y' column.";
        return {};
    }
    const QJsonArray data = s.value("data").toArray();
    QJsonObject out = baseSpec(s, th);
    out["mark"] = QJsonObject{{"type", "bar"},
                              {"cornerRadiusEnd", 2},
                              {"tooltip", true}};

    // Vega doesn't auto-truncate — but with labelLimit on axis + labelAngle
    // when there are many or long categories, labels read cleanly.
    const QStringList catSamples = [&]() {
        QStringList o;
        for (const auto &r : data) {
            if (!r.isObject()) continue;
            o << r.toObject().value(x).toString();
            if (o.size() >= 20) break;
        }
        return o;
    }();
    bool tilt = false;
    if (catSamples.size() > 5) tilt = true;
    for (const QString &c : catSamples) if (c.size() > 8) { tilt = true; break; }

    QJsonObject enc;
    QJsonObject cat;
    cat["field"] = x;
    cat["type"]  = "nominal";
    cat["title"] = x;
    cat["sort"]  = QJsonValue("-y");  // descending by value — natural for ranking
    if (tilt && !horizontal) {
        QJsonObject axis;
        axis["labelAngle"] = -35;
        axis["labelLimit"] = 220;
        cat["axis"] = axis;
    }
    QJsonObject val;
    val["field"] = y;
    val["type"]  = "quantitative";
    val["title"] = y;
    val["axis"]  = QJsonObject{{"format", axisFormat(y, data)}};

    if (horizontal) { enc["y"] = cat; enc["x"] = val; cat["sort"] = QJsonValue("-x"); enc["y"] = cat; }
    else            { enc["x"] = cat; enc["y"] = val; }

    // Multi-colour by default (Tableau 20 cycle). Honour explicit opt-out.
    const bool wantMulti = s.value("multiColor").toBool(true);
    if (wantMulti) {
        enc["color"] = QJsonObject{{"field", x},
                                   {"type", "nominal"},
                                   {"legend", QJsonValue::Null}};
    }
    enc["tooltip"] = QJsonArray{
        QJsonObject{{"field", x}, {"type", "nominal"}, {"title", x}},
        QJsonObject{{"field", y}, {"type", "quantitative"},
                    {"title", y}, {"format", axisFormat(y, data)}},
    };
    out["encoding"] = enc;
    return out;
}

QJsonObject mkPieOrDonut(const QJsonObject &s, const Theme &th, bool donut,
                         QString *err) {
    const QString labelCol = s.value("label").toString().isEmpty()
                                 ? s.value("x").toString()
                                 : s.value("label").toString();
    const QString valueCol = s.value("value").toString().isEmpty()
                                 ? s.value("y").toString()
                                 : s.value("value").toString();
    if (labelCol.isEmpty() || valueCol.isEmpty()) {
        if (err) *err = "pie/donut: need label+value (or x+y).";
        return {};
    }
    const QJsonArray data = s.value("data").toArray();
    QJsonObject out = baseSpec(s, th);
    QJsonObject mark;
    mark["type"]    = "arc";
    if (donut) mark["innerRadius"] = 70;
    mark["stroke"]  = th.dark ? "#1E1E1E" : "#FFFFFF";
    mark["strokeWidth"] = 2;
    out["mark"]     = mark;
    out["encoding"] = QJsonObject{
        {"theta", QJsonObject{{"field", valueCol}, {"type", "quantitative"}}},
        {"color", QJsonObject{{"field", labelCol}, {"type", "nominal"},
                              {"legend", QJsonObject{{"title", labelCol}}}}},
        {"tooltip", QJsonArray{
            QJsonObject{{"field", labelCol}, {"type", "nominal"}},
            QJsonObject{{"field", valueCol}, {"type", "quantitative"},
                        {"format", axisFormat(valueCol, data)}},
        }},
    };
    return out;
}

QJsonObject mkHistogram(const QJsonObject &s, const Theme &th, QString *err) {
    const QString x = s.value("x").toString();
    if (x.isEmpty()) {
        if (err) *err = "histogram: missing 'x' column.";
        return {};
    }
    const int bins = s.value("bins").toInt(20);
    QJsonObject out = baseSpec(s, th);
    out["mark"] = QJsonObject{{"type", "bar"}, {"tooltip", true}};
    out["encoding"] = QJsonObject{
        {"x", QJsonObject{{"field", x}, {"type", "quantitative"},
                          {"bin", QJsonObject{{"maxbins", bins}}},
                          {"title", x}}},
        {"y", QJsonObject{{"aggregate", "count"}, {"type", "quantitative"},
                          {"title", "count"},
                          {"axis", QJsonObject{{"format", ","}}}}},
        {"tooltip", QJsonArray{
            QJsonObject{{"field", x}, {"type", "quantitative"},
                        {"bin", true}, {"title", x}},
            QJsonObject{{"aggregate", "count"}, {"type", "quantitative"},
                        {"title", "count"}, {"format", ","}},
        }},
    };
    return out;
}

QJsonObject mkBoxplot(const QJsonObject &s, const Theme &th, QString *err) {
    const QString g = s.value("x").toString();
    const QString v = s.value("y").toString();
    if (g.isEmpty() || v.isEmpty()) {
        if (err) *err = "boxplot: missing 'x' (group) or 'y' (value) column.";
        return {};
    }
    const QJsonArray data = s.value("data").toArray();
    QJsonObject out = baseSpec(s, th);
    out["mark"] = QJsonObject{{"type", "boxplot"},
                              {"extent", "min-max"},
                              {"size", 30}};
    out["encoding"] = QJsonObject{
        {"x", QJsonObject{{"field", g}, {"type", "nominal"}, {"title", g}}},
        {"y", QJsonObject{{"field", v}, {"type", "quantitative"},
                          {"title", v},
                          {"axis", QJsonObject{{"format", axisFormat(v, data)}}}}},
        {"color", QJsonObject{{"field", g}, {"type", "nominal"},
                              {"legend", QJsonValue::Null}}},
    };
    return out;
}

// Stacked / grouped — y is an ARRAY of column names; fold to long format.
QJsonObject mkMultiBar(const QJsonObject &s, const Theme &th,
                       bool stacked, bool horizontal, QString *err) {
    const QString x = s.value("x").toString();
    const QJsonArray ys = s.value("y").toArray();
    if (x.isEmpty() || ys.isEmpty()) {
        if (err) *err = "grouped/stacked-bar: need x + y (array of columns).";
        return {};
    }
    QStringList yCols;
    for (const auto &v : ys) yCols << v.toString();

    QJsonObject out = baseSpec(s, th);
    QJsonArray folds;
    for (const QString &c : yCols) folds.append(c);
    out["transform"] = QJsonArray{
        QJsonObject{{"fold", folds}, {"as", QJsonArray{"series", "value"}}},
    };

    QJsonObject cat;
    cat["field"] = x;
    cat["type"]  = "nominal";
    cat["title"] = x;
    QJsonObject val;
    val["field"] = "value";
    val["type"]  = "quantitative";
    val["title"] = "value";
    val["axis"]  = QJsonObject{{"format", ","}};
    if (stacked) val["stack"] = "zero";
    else         val["stack"] = QJsonValue(false);

    QJsonObject color;
    color["field"] = "series";
    color["type"]  = "nominal";

    QJsonObject enc;
    if (horizontal) { enc["y"] = cat; enc["x"] = val; }
    else            { enc["x"] = cat; enc["y"] = val; }
    if (!stacked) {
        // grouped: xOffset by series for side-by-side bars
        QJsonObject offset{{"field", "series"}, {"type", "nominal"}};
        if (horizontal) enc["yOffset"] = offset; else enc["xOffset"] = offset;
    }
    enc["color"]   = color;
    enc["tooltip"] = QJsonArray{
        QJsonObject{{"field", x}, {"type", "nominal"}, {"title", x}},
        QJsonObject{{"field", "series"}, {"type", "nominal"}, {"title", "series"}},
        QJsonObject{{"field", "value"}, {"type", "quantitative"},
                    {"format", ","}},
    };
    out["mark"] = QJsonObject{{"type", "bar"}, {"tooltip", true},
                              {"cornerRadiusEnd", 2}};
    out["encoding"] = enc;
    return out;
}

// ── P5: NEW chart types ──────────────────────────────────────────

// Heatmap — x category, y category, value heat. Simplified spec:
// {"type":"heatmap","x":"day","y":"hour","value":"count","data":[...]}.
QJsonObject mkHeatmap(const QJsonObject &s, const Theme &th, QString *err) {
    const QString x = s.value("x").toString();
    const QString y = s.value("y").toString();
    const QString v = s.value("value").toString();
    if (x.isEmpty() || y.isEmpty() || v.isEmpty()) {
        if (err) *err = "heatmap: need x, y, value columns.";
        return {};
    }
    const QJsonArray data = s.value("data").toArray();
    QJsonObject out = baseSpec(s, th);
    out["mark"] = QJsonObject{{"type", "rect"}, {"tooltip", true}};
    out["encoding"] = QJsonObject{
        {"x", QJsonObject{{"field", x},
                          {"type", isNumericColumn(data, x) ? "ordinal" : "nominal"},
                          {"title", x}}},
        {"y", QJsonObject{{"field", y},
                          {"type", isNumericColumn(data, y) ? "ordinal" : "nominal"},
                          {"title", y}}},
        {"color", QJsonObject{
            {"field", v}, {"type", "quantitative"},
            {"scale", QJsonObject{{"scheme", th.dark ? "viridis" : "blues"}}},
            {"legend", QJsonObject{{"title", v}}},
        }},
        {"tooltip", QJsonArray{
            QJsonObject{{"field", x}}, QJsonObject{{"field", y}},
            QJsonObject{{"field", v}, {"format", axisFormat(v, data)}},
        }},
    };
    out["config"].toObject()["view"].toObject().value("stroke");
    return out;
}

// Density — kernel density estimate on a numeric column.
// {"type":"density","x":"score","data":[...]}
QJsonObject mkDensity(const QJsonObject &s, const Theme &th, QString *err) {
    const QString x = s.value("x").toString();
    if (x.isEmpty()) {
        if (err) *err = "density: need x column.";
        return {};
    }
    QJsonObject out = baseSpec(s, th);
    out["transform"] = QJsonArray{
        QJsonObject{{"density", x}, {"bandwidth", 0.0},
                    {"as", QJsonArray{x, "density"}}},
    };
    out["mark"] = QJsonObject{{"type", "area"},
                              {"opacity", 0.7},
                              {"interpolate", "monotone"},
                              {"line", true}};
    out["encoding"] = QJsonObject{
        {"x", QJsonObject{{"field", x}, {"type", "quantitative"}, {"title", x}}},
        {"y", QJsonObject{{"field", "density"}, {"type", "quantitative"},
                          {"title", "density"}}},
        {"tooltip", QJsonArray{
            QJsonObject{{"field", x}, {"format", ",.2f"}},
            QJsonObject{{"field", "density"}, {"format", ",.4f"}},
        }},
    };
    return out;
}

// Regression-line — scatter with overlaid regression. type:"regression-line"
// {"type":"regression-line","x":"engine","y":"price","data":[...],
//  "method":"linear"}.
QJsonObject mkRegressionLine(const QJsonObject &s, const Theme &th,
                             QString *err) {
    const QString x = s.value("x").toString();
    const QString y = s.value("y").toString();
    if (x.isEmpty() || y.isEmpty()) {
        if (err) *err = "regression-line: need x + y columns.";
        return {};
    }
    QString method = s.value("method").toString().toLower();
    if (method.isEmpty()) method = "linear";
    QJsonObject out = baseSpec(s, th);
    out.remove("mark");
    QJsonObject scatterLayer;
    scatterLayer["mark"] = QJsonObject{{"type", "circle"},
                                        {"size", 60}, {"opacity", 0.65}};
    scatterLayer["encoding"] = buildXY(s, x, y, false, false);
    QJsonObject regLayer;
    regLayer["transform"] = QJsonArray{
        QJsonObject{{"regression", y}, {"on", x}, {"method", method}},
    };
    regLayer["mark"] = QJsonObject{{"type", "line"},
                                    {"color", th.dark ? "#F28E2B" : "#E15759"},
                                    {"strokeWidth", 2}};
    regLayer["encoding"] = QJsonObject{
        {"x", QJsonObject{{"field", x}, {"type", "quantitative"}}},
        {"y", QJsonObject{{"field", y}, {"type", "quantitative"}}},
    };
    out["layer"] = QJsonArray{scatterLayer, regLayer};
    return out;
}

// Faceted-bar — bar chart per facet column.
// {"type":"faceted-bar","x":"product","y":"sales","facet":"region","data":[...]}
QJsonObject mkFacetedBar(const QJsonObject &s, const Theme &th, QString *err) {
    const QString x = s.value("x").toString();
    const QString y = s.value("y").toString();
    const QString f = s.value("facet").toString();
    if (x.isEmpty() || y.isEmpty() || f.isEmpty()) {
        if (err) *err = "faceted-bar: need x + y + facet columns.";
        return {};
    }
    const QJsonArray data = s.value("data").toArray();
    QJsonObject out = baseSpec(s, th);
    out["mark"] = QJsonObject{{"type", "bar"}, {"tooltip", true},
                               {"cornerRadiusEnd", 2}};
    out["encoding"] = QJsonObject{
        {"x", QJsonObject{{"field", x}, {"type", "nominal"}, {"title", x},
                          {"axis", QJsonObject{{"labelAngle", -35}}}}},
        {"y", QJsonObject{{"field", y}, {"type", "quantitative"},
                          {"title", y},
                          {"axis", QJsonObject{{"format", axisFormat(y, data)}}}}},
        {"color", QJsonObject{{"field", x}, {"type", "nominal"},
                              {"legend", QJsonValue::Null}}},
        {"column", QJsonObject{{"field", f}, {"type", "nominal"},
                               {"title", f}}},
        {"tooltip", QJsonArray{
            QJsonObject{{"field", x}}, QJsonObject{{"field", y}},
            QJsonObject{{"field", f}},
        }},
    };
    // Faceted layout needs more horizontal room — let it expand.
    out["height"] = 260;
    return out;
}

// Error-bar — point + error range.
// {"type":"error-bar","x":"variant","y":"mean","yMin":"low","yMax":"high","data":[...]}
QJsonObject mkErrorBar(const QJsonObject &s, const Theme &th, QString *err) {
    const QString x    = s.value("x").toString();
    const QString y    = s.value("y").toString();
    const QString lo   = s.value("yMin").toString();
    const QString hi   = s.value("yMax").toString();
    if (x.isEmpty() || y.isEmpty() || lo.isEmpty() || hi.isEmpty()) {
        if (err) *err = "error-bar: need x, y, yMin, yMax columns.";
        return {};
    }
    const QJsonArray data = s.value("data").toArray();
    QJsonObject out = baseSpec(s, th);
    out.remove("mark");
    QJsonObject errLayer;
    errLayer["mark"] = QJsonObject{{"type", "errorbar"}, {"ticks", true}};
    errLayer["encoding"] = QJsonObject{
        {"x",  QJsonObject{{"field", x}, {"type", "nominal"}, {"title", x}}},
        {"y",  QJsonObject{{"field", lo}, {"type", "quantitative"},
                           {"title", y},
                           {"axis", QJsonObject{{"format", axisFormat(y, data)}}}}},
        {"y2", QJsonObject{{"field", hi}}},
    };
    QJsonObject pointLayer;
    pointLayer["mark"] = QJsonObject{{"type", "point"},
                                      {"filled", true},
                                      {"size", 70}};
    pointLayer["encoding"] = QJsonObject{
        {"x", QJsonObject{{"field", x}, {"type", "nominal"}}},
        {"y", QJsonObject{{"field", y}, {"type", "quantitative"}}},
        {"color", QJsonObject{{"field", x}, {"type", "nominal"},
                              {"legend", QJsonValue::Null}}},
    };
    out["layer"] = QJsonArray{errLayer, pointLayer};
    return out;
}

// v0.1.90 — universal facet wrapper. If the simplified spec has `facet`
// (single string column → column facet), `row`/`column` (two strings,
// 2D facet grid), wrap the rendered chart in a Vega-Lite facet
// structure so it small-multiples by the given dimension. Works on top
// of ANY core spec — bar / line / scatter / dual-axis / heatmap etc.
QJsonObject applyFacetIfRequested(const QJsonObject &core,
                                  const QJsonObject &simplified) {
    const QString facet = simplified.value("facet").toString();
    const QString row   = simplified.value("row").toString();
    const QString col   = simplified.value("column").toString();
    if (facet.isEmpty() && row.isEmpty() && col.isEmpty()) return core;

    // The inner spec must not carry top-level data — Vega-Lite expects
    // data at the wrapper. Move it out.
    QJsonObject inner = core;
    QJsonObject outer;
    outer["$schema"]  = inner.take("$schema");
    if (inner.contains("title")) outer["title"] = inner.take("title");
    outer["data"]     = inner.take("data");
    outer["config"]   = inner.take("config");
    if (inner.contains("autosize")) outer["autosize"] = inner.take("autosize");

    // Build the facet definition.
    QJsonObject facetDef;
    if (!facet.isEmpty()) {
        facetDef["field"] = facet;
        facetDef["type"]  = "nominal";
        facetDef["title"] = facet;
        // Cap columns at 3 — beyond that the grid gets unreadable.
        outer["facet"]   = facetDef;
        outer["columns"] = 3;
    } else {
        QJsonObject facetObj;
        if (!row.isEmpty()) {
            facetObj["row"] = QJsonObject{{"field", row}, {"type", "nominal"},
                                          {"title", row}};
        }
        if (!col.isEmpty()) {
            facetObj["column"] = QJsonObject{{"field", col}, {"type", "nominal"},
                                             {"title", col}};
        }
        outer["facet"] = facetObj;
    }
    outer["spec"] = inner;
    return outer;
}

}  // namespace

bool supported(const QString &type) {
    static const QSet<QString> kAll = {
        "line", "area", "bar", "horizontal-bar",
        "grouped-bar", "stacked-bar", "stacked-horizontal-bar",
        "pie", "donut", "scatter", "histogram", "boxplot",
        // P5 new types
        "heatmap", "density", "regression-line", "faceted-bar", "error-bar",
    };
    return kAll.contains(type.toLower());
}

QJsonObject translate(const QJsonObject &s, const Theme &theme, QString *err) {
    const QString t = s.value("type").toString().toLower();
    if (!supported(t)) return {};
    if (!s.contains("data") || !s.value("data").isArray()) {
        if (err) *err = "spec is missing 'data' array.";
        return {};
    }
    QJsonObject core;
    // v0.1.90 — if the user wrote `"type":"bar"` (or horizontal-bar)
    // but passed `y` as an ARRAY, auto-route to grouped-bar /
    // stacked-horizontal-bar. UX rule: the model shouldn't have to
    // pick a different `type` value just because they added a metric.
    const bool yIsArray = s.value("y").isArray();
    if (t == "line")                          core = mkLineOrArea(s, theme, "line", err);
    else if (t == "area")                     core = mkLineOrArea(s, theme, "area", err);
    else if (t == "scatter")                  core = mkScatter(s, theme, err);
    else if (t == "bar" && yIsArray)          core = mkMultiBar(s, theme, false, false, err);
    else if (t == "bar")                      core = mkBar(s, theme, false, err);
    else if (t == "horizontal-bar" && yIsArray) core = mkMultiBar(s, theme, false, true, err);
    else if (t == "horizontal-bar")           core = mkBar(s, theme, true, err);
    else if (t == "pie")                      core = mkPieOrDonut(s, theme, false, err);
    else if (t == "donut")                    core = mkPieOrDonut(s, theme, true, err);
    else if (t == "histogram")                core = mkHistogram(s, theme, err);
    else if (t == "boxplot")                  core = mkBoxplot(s, theme, err);
    else if (t == "grouped-bar")              core = mkMultiBar(s, theme, false, false, err);
    else if (t == "stacked-bar")              core = mkMultiBar(s, theme, true,  false, err);
    else if (t == "stacked-horizontal-bar")   core = mkMultiBar(s, theme, true,  true,  err);
    else if (t == "heatmap")                  core = mkHeatmap(s, theme, err);
    else if (t == "density")                  core = mkDensity(s, theme, err);
    else if (t == "regression-line")          core = mkRegressionLine(s, theme, err);
    else if (t == "faceted-bar")              core = mkFacetedBar(s, theme, err);
    else if (t == "error-bar")                core = mkErrorBar(s, theme, err);

    if (core.isEmpty()) return {};
    return applyFacetIfRequested(core, s);
}

}  // namespace ChartSpecToVega
