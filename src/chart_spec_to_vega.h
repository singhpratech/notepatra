#ifndef NOTEPATRA_CHART_SPEC_TO_VEGA_H
#define NOTEPATRA_CHART_SPEC_TO_VEGA_H

// v0.1.90 — translate Notepatra simplified chart specs (the
// ```chart {...} fenced block the Data Analyst emits) to Vega-Lite v5
// JSON specs. Pure function — no Qt widget construction here so the
// translator can be unit-tested independent of QApplication.

#include <QJsonObject>
#include <QString>

namespace ChartSpecToVega {

struct Theme {
    bool dark = false;
    QString name;   // "Light" | "Dark" | "Monokai" — used to pick palette
};

// Translate one simplified spec → Vega-Lite v5 JSON spec.
// Returns an empty object when the chart type isn't supported by the
// translator (caller falls back to the QtCharts path).
// Sets *outError to a human message on data shape errors.
QJsonObject translate(const QJsonObject &simplified,
                      const Theme &theme,
                      QString *outError = nullptr);

// True iff translate() knows how to render this type.
bool supported(const QString &type);

}  // namespace ChartSpecToVega

#endif
