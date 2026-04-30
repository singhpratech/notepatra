#ifndef NOTEPATRA_CHARTRENDER_H
#define NOTEPATRA_CHARTRENDER_H

// ═══════════════════════════════════════════════════════════════════════
// v0.1.43 — render the JSON chart specs the Data Analyst AI emits.
//
// Wire format the model is instructed to use:
//   ```chart
//   {
//     "type": "line" | "bar" | "pie" | "scatter",
//     "title": "optional chart title",
//     "x": "<x-axis column name, ignored for pie>",
//     "y": "<y-axis column name, ignored for pie>",
//     "label": "<pie label column>",
//     "value": "<pie value column>",
//     "data": [ { "<col>": <num|str>, ... }, ... ]
//   }
//
// We use QtCharts (Qt5::Charts) to render real interactive charts so the
// user gets zoom / hover / theme-aware visuals in-line in the chat
// transcript. Falls back gracefully (returns nullptr → AIPanel renders
// the JSON as a regular code block) if the spec is malformed.
// ═══════════════════════════════════════════════════════════════════════

#include <QString>
#include <QJsonObject>

class QWidget;

namespace ChartRender {

// Render a chart from its JSON spec. Returns a heap-allocated QWidget
// (QChartView*) on success — caller takes ownership. On parse / data
// failure returns nullptr and writes a human message to *outError.
//
// The widget has a fixed minimum height (260) so it doesn't collapse
// when embedded in a QVBoxLayout-based chat transcript.
QWidget *renderFromSpec(const QString &jsonText,
                        QWidget *parent,
                        QString *outError = nullptr);

// Same as renderFromSpec but takes an already-parsed object (saves
// re-parsing JSON when the caller already did it for validation).
QWidget *renderFromObject(const QJsonObject &spec,
                          QWidget *parent,
                          QString *outError = nullptr);

// Quick sanity check — does this look like a chart spec at all? Used by
// AIPanel to decide whether a ```chart fenced block is worth attempting
// to render. Cheap; doesn't construct any widgets.
bool looksLikeChartSpec(const QJsonObject &spec);

} // namespace ChartRender

#endif // NOTEPATRA_CHARTRENDER_H
