// v0.1.76 — Chart modal viewer.
//
// Click the small "⛶" expand button on any inline chart in the AI
// chat transcript → this dialog opens with the chart re-rendered at
// 900x600 + a "Save as PNG…" button that writes the chart to disk.
//
// Re-renders the chart from the original spec rather than re-parenting
// the existing QChartView so the inline chat thumbnail keeps its
// independent state (hover tooltips, axis labels, etc.).

#ifndef NOTEPATRA_CHART_MODAL_H
#define NOTEPATRA_CHART_MODAL_H

#include <QDialog>
#include <QJsonObject>

class QWidget;
class QPushButton;
class QLabel;

class ChartModalDialog : public QDialog {
    Q_OBJECT
public:
    explicit ChartModalDialog(const QJsonObject &spec, QWidget *parent = nullptr);

private slots:
    void onSavePng();
    void onCopyImage();

private:
    QJsonObject  m_spec;
    QWidget     *m_chartHost = nullptr;  // the QChartView (or wrapped widget)
    QLabel      *m_status    = nullptr;
};

#endif // NOTEPATRA_CHART_MODAL_H
