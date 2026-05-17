// SPDX-License-Identifier: GPL-3.0-or-later

// v0.1.76 — Chart modal viewer.
// v0.1.90 — extended to drive Vega-Lite exports (PNG, SVG, HTML, spec).
//
// Click the small "⛶" expand button on any inline chart in the AI
// chat transcript → this dialog opens with the chart re-rendered at
// 960x640 + an Export… dropdown that writes PNG / SVG / interactive
// HTML / JSON spec to disk.
//
// Re-renders the chart from the original spec rather than re-parenting
// the existing chart view so the inline chat thumbnail keeps its
// independent state.

#ifndef NOTEPATRA_CHART_MODAL_H
#define NOTEPATRA_CHART_MODAL_H

#include <QDialog>
#include <QJsonObject>

class QWidget;
class QPushButton;
class QLabel;
class QMenu;

class ChartModalDialog : public QDialog {
    Q_OBJECT
public:
    explicit ChartModalDialog(const QJsonObject &spec, QWidget *parent = nullptr);

private slots:
    void onCopyImage();
    void onExportPng(int scale);
    void onExportSvg();
    void onExportHtml();
    void onExportSpec();

private:
    // Returns the underlying VegaChartRenderer if the chart is Vega-
    // backed, else nullptr (QtCharts path).
    QObject *vegaRenderer() const;

    // Pixmap-grab path for the QtCharts fallback. Vega charts go
    // through their own async toImageURL() pipeline instead.
    void saveQtChartsAs(const QString &path, const QString &fmt);

    QString defaultSavePath(const QString &ext) const;

    QJsonObject  m_spec;
    QWidget     *m_chartHost = nullptr;  // wrap from renderFromObject
    QLabel      *m_status    = nullptr;
};

#endif // NOTEPATRA_CHART_MODAL_H
