#include "chart_modal.h"
#include "chartrender.h"

#ifdef NOTEPATRA_WITH_WEBENGINE
#include "charts/vega_chart_renderer.h"
#endif

#include <QApplication>
#include <QClipboard>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileDialog>
#include <QJsonDocument>
#include <QHBoxLayout>
#include <QLabel>
#include <QMenu>
#include <QMessageBox>
#include <QPixmap>
#include <QPushButton>
#include <QStandardPaths>
#include <QStyle>
#include <QVBoxLayout>
#include <QVariant>

ChartModalDialog::ChartModalDialog(const QJsonObject &spec, QWidget *parent)
    : QDialog(parent), m_spec(spec)
{
    setWindowTitle(tr("Chart Viewer"));
    setSizeGripEnabled(true);
    resize(1060, 700);

    auto *root = new QVBoxLayout(this);
    root->setContentsMargins(16, 16, 16, 16);
    root->setSpacing(10);

    // Re-render the chart fresh — independent at modal size, no inner
    // expand button (we are the modal — no point recursing).
    QString err;
    QWidget *w = ChartRender::renderFromObject(spec, this, &err, /*withExpandButton=*/false);
    if (!w) {
        auto *errLbl = new QLabel(
            tr("Could not render chart: %1").arg(err.isEmpty() ? tr("unknown error") : err),
            this);
        errLbl->setStyleSheet("color: #c0392b; padding: 20px;");
        errLbl->setWordWrap(true);
        root->addWidget(errLbl, 1);
    } else {
        w->setMinimumSize(820, 540);
        root->addWidget(w, 1);
        m_chartHost = w;
    }

    // Button row — v0.1.90.
    //   [status label  ........................] [Copy image] [Export ▾] [Close]
    auto *btnRow = new QHBoxLayout();
    m_status = new QLabel(this);
    m_status->setStyleSheet("color: #B4B4B4; font-size: 11px;");
    btnRow->addWidget(m_status, 1);

    const QString primaryBtnQss =
        "QPushButton {"
        "  background: #2E7DD8; color: #FFFFFF;"
        "  border: 1px solid #1F66B8; border-radius: 4px;"
        "  padding: 6px 14px; font-weight: 600;"
        "}"
        "QPushButton:hover { background: #3A8DE8; border-color: #2978C8; }"
        "QPushButton:pressed { background: #2467B8; }"
        "QPushButton::menu-indicator { width: 12px; subcontrol-position: right center; }";
    const QString secondaryBtnQss =
        "QPushButton {"
        "  background: #3A3A3D; color: #F0F0F0;"
        "  border: 1px solid #5C5C5F; border-radius: 4px;"
        "  padding: 6px 14px;"
        "}"
        "QPushButton:hover { background: #4A4A4D; border-color: #6C6C6F; }"
        "QPushButton:pressed { background: #2A2A2D; }";
    // QMenu sits OVER the dialog, but Qt scopes per-widget QSS to the
    // spawning widget — see [[feedback_qmenu_cascade_through_widget_qss]].
    const QString menuQss =
        "QMenu { background-color: #2A2A2D; color: #F0F0F0;"
        "        border: 1px solid #4C4C50; padding: 4px; }"
        "QMenu::item { padding: 6px 22px 6px 14px; }"
        "QMenu::item:selected { background-color: #2E7DD8; color: #FFFFFF; }"
        "QMenu::separator { height: 1px; background: #4C4C50; margin: 4px 8px; }";

    auto *copyBtn = new QPushButton(tr("Copy image"), this);
    copyBtn->setIcon(style()->standardIcon(QStyle::SP_DialogResetButton));
    copyBtn->setStyleSheet(secondaryBtnQss);
    btnRow->addWidget(copyBtn);

    auto *exportBtn = new QPushButton(tr("Export…"), this);
    exportBtn->setIcon(style()->standardIcon(QStyle::SP_DialogSaveButton));
    exportBtn->setStyleSheet(primaryBtnQss + "\n" + menuQss);
    exportBtn->setDefault(true);
    auto *exportMenu = new QMenu(exportBtn);
    exportMenu->setStyleSheet(menuQss);
    exportMenu->addAction(tr("PNG · 1x (screen size)"),
                          this, [this]() { onExportPng(1); });
    exportMenu->addAction(tr("PNG · 2x (high-DPI)"),
                          this, [this]() { onExportPng(2); });
    exportMenu->addAction(tr("PNG · 4x (poster)"),
                          this, [this]() { onExportPng(4); });
    exportMenu->addSeparator();
    auto *svgAct = exportMenu->addAction(tr("SVG · vector — interactive in browser"),
                                         this, [this]() { onExportSvg(); });
    auto *htmlAct = exportMenu->addAction(tr("HTML · fully interactive (hover/zoom)"),
                                          this, [this]() { onExportHtml(); });
    exportMenu->addSeparator();
    exportMenu->addAction(tr("Spec JSON — paste into vega-editor"),
                          this, [this]() { onExportSpec(); });
    exportBtn->setMenu(exportMenu);

    // SVG / HTML / Spec only work on Vega-backed charts. QtCharts
    // fallback (lite mode) keeps PNG, disables the rest.
    if (!vegaRenderer()) {
        svgAct->setEnabled(false);
        svgAct->setText(tr("SVG · install Charts Pack to enable"));
        htmlAct->setEnabled(false);
        htmlAct->setText(tr("HTML · install Charts Pack to enable"));
    }
    btnRow->addWidget(exportBtn);

    auto *closeBtn = new QPushButton(tr("Close"), this);
    closeBtn->setIcon(style()->standardIcon(QStyle::SP_DialogCloseButton));
    closeBtn->setStyleSheet(secondaryBtnQss);
    btnRow->addWidget(closeBtn);

    root->addLayout(btnRow);

    connect(copyBtn,  &QPushButton::clicked, this, &ChartModalDialog::onCopyImage);
    connect(closeBtn, &QPushButton::clicked, this, &QDialog::accept);
}

QObject *ChartModalDialog::vegaRenderer() const {
#ifdef NOTEPATRA_WITH_WEBENGINE
    if (!m_chartHost) return nullptr;
    if (m_chartHost->property("notepatra-chart-kind").toString() != "vega")
        return nullptr;
    const QVariant v = m_chartHost->property("notepatra-vega-renderer-ptr");
    return v.value<QObject *>();
#else
    return nullptr;
#endif
}

QString ChartModalDialog::defaultSavePath(const QString &ext) const {
    QString defaultDir = QStandardPaths::writableLocation(QStandardPaths::PicturesLocation);
    if (defaultDir.isEmpty()) defaultDir = QDir::homePath();
    const QString stamp = QDateTime::currentDateTime().toString("yyyyMMdd-HHmmss");
    return QDir(defaultDir).absoluteFilePath(QStringLiteral("notepatra-chart-%1.%2")
                                                  .arg(stamp, ext));
}

void ChartModalDialog::saveQtChartsAs(const QString &path, const QString &fmt) {
    if (!m_chartHost) return;
    const qreal dpr = devicePixelRatioF();
    QPixmap pix(m_chartHost->size() * dpr);
    pix.setDevicePixelRatio(dpr);
    pix.fill(Qt::white);
    m_chartHost->render(&pix);
    if (!pix.save(path, fmt.toUtf8().constData())) {
        QMessageBox::warning(this, tr("Save failed"),
            tr("Could not write %1").arg(path));
        return;
    }
    m_status->setText(tr("Saved → %1").arg(path));
}

void ChartModalDialog::onCopyImage() {
    if (!m_chartHost) return;
#ifdef NOTEPATRA_WITH_WEBENGINE
    // For Vega charts, prefer the canvas-rendered PNG via the JS bridge
    // so the clipboard image is rasterized at full chart resolution
    // rather than at scaled-down widget size.
    if (auto *raw = vegaRenderer()) {
        auto *r = qobject_cast<VegaChartRenderer *>(raw);
        if (r) {
            r->exportPngAsync(2, [this](const QByteArray &png) {
                if (png.isEmpty()) {
                    m_status->setText(tr("Copy failed — chart not ready."));
                    return;
                }
                QPixmap pix;
                if (pix.loadFromData(png, "PNG")) {
                    QApplication::clipboard()->setPixmap(pix);
                    m_status->setText(tr("Image copied to clipboard"));
                }
            });
            return;
        }
    }
#endif
    const qreal dpr = devicePixelRatioF();
    QPixmap pix(m_chartHost->size() * dpr);
    pix.setDevicePixelRatio(dpr);
    pix.fill(Qt::white);
    m_chartHost->render(&pix);
    QApplication::clipboard()->setPixmap(pix);
    m_status->setText(tr("Image copied to clipboard"));
}

void ChartModalDialog::onExportPng(int scale) {
    const QString path = QFileDialog::getSaveFileName(
        this, tr("Save chart as PNG"), defaultSavePath("png"),
        tr("PNG Image (*.png)"));
    if (path.isEmpty()) return;

#ifdef NOTEPATRA_WITH_WEBENGINE
    if (auto *raw = vegaRenderer()) {
        auto *r = qobject_cast<VegaChartRenderer *>(raw);
        if (r) {
            m_status->setText(tr("Rendering %1× PNG…").arg(scale));
            r->exportPngAsync(scale, [this, path](const QByteArray &png) {
                if (png.isEmpty()) {
                    QMessageBox::warning(this, tr("Export failed"),
                        tr("Vega-Lite chart did not finish rendering."));
                    return;
                }
                QFile f(path);
                if (!f.open(QIODevice::WriteOnly) || f.write(png) < 0) {
                    QMessageBox::warning(this, tr("Save failed"),
                        tr("Could not write %1").arg(path));
                    return;
                }
                m_status->setText(tr("Saved → %1").arg(path));
            });
            return;
        }
    }
#else
    Q_UNUSED(scale);
#endif
    saveQtChartsAs(path, "PNG");
}

void ChartModalDialog::onExportSvg() {
#ifdef NOTEPATRA_WITH_WEBENGINE
    auto *raw = vegaRenderer();
    if (!raw) {
        QMessageBox::information(this, tr("SVG export"),
            tr("SVG export requires the Charts Pack (Vega-Lite renderer)."));
        return;
    }
    const QString path = QFileDialog::getSaveFileName(
        this, tr("Save chart as SVG"), defaultSavePath("svg"),
        tr("SVG Image (*.svg)"));
    if (path.isEmpty()) return;
    auto *r = qobject_cast<VegaChartRenderer *>(raw);
    if (!r) return;
    m_status->setText(tr("Rendering SVG…"));
    r->exportSvgAsync([this, path](const QByteArray &svg) {
        if (svg.isEmpty()) {
            QMessageBox::warning(this, tr("Export failed"),
                tr("Vega-Lite chart did not finish rendering."));
            return;
        }
        QFile f(path);
        if (!f.open(QIODevice::WriteOnly) || f.write(svg) < 0) {
            QMessageBox::warning(this, tr("Save failed"),
                tr("Could not write %1").arg(path));
            return;
        }
        m_status->setText(tr("Saved → %1 (open in a browser to interact)").arg(path));
    });
#else
    QMessageBox::information(this, tr("SVG export"),
        tr("SVG export requires the Charts Pack."));
#endif
}

void ChartModalDialog::onExportHtml() {
#ifdef NOTEPATRA_WITH_WEBENGINE
    auto *raw = vegaRenderer();
    if (!raw) {
        QMessageBox::information(this, tr("HTML export"),
            tr("Interactive HTML export requires the Charts Pack."));
        return;
    }
    const QString path = QFileDialog::getSaveFileName(
        this, tr("Save chart as interactive HTML"), defaultSavePath("html"),
        tr("HTML Document (*.html)"));
    if (path.isEmpty()) return;
    auto *r = qobject_cast<VegaChartRenderer *>(raw);
    if (!r) return;
    r->exportHtmlAsync([this, path](const QByteArray &html) {
        QFile f(path);
        if (!f.open(QIODevice::WriteOnly) || f.write(html) < 0) {
            QMessageBox::warning(this, tr("Save failed"),
                tr("Could not write %1").arg(path));
            return;
        }
        m_status->setText(tr("Saved → %1 (full hover/zoom in a browser)").arg(path));
    });
#else
    QMessageBox::information(this, tr("HTML export"),
        tr("Interactive HTML export requires the Charts Pack."));
#endif
}

void ChartModalDialog::onExportSpec() {
    const QString path = QFileDialog::getSaveFileName(
        this, tr("Save chart spec as JSON"), defaultSavePath("json"),
        tr("JSON spec (*.json)"));
    if (path.isEmpty()) return;
#ifdef NOTEPATRA_WITH_WEBENGINE
    if (auto *raw = vegaRenderer()) {
        if (auto *r = qobject_cast<VegaChartRenderer *>(raw)) {
            r->exportSpecAsync([this, path](const QByteArray &json) {
                QFile f(path);
                if (!f.open(QIODevice::WriteOnly) || f.write(json) < 0) {
                    QMessageBox::warning(this, tr("Save failed"),
                        tr("Could not write %1").arg(path));
                    return;
                }
                m_status->setText(tr("Saved → %1").arg(path));
            });
            return;
        }
    }
#endif
    // QtCharts path: export the ORIGINAL simplified spec (the AI's
    // ```chart fenced block payload).
    QFile f(path);
    if (!f.open(QIODevice::WriteOnly)) {
        QMessageBox::warning(this, tr("Save failed"),
            tr("Could not write %1").arg(path));
        return;
    }
    f.write(QJsonDocument(m_spec).toJson(QJsonDocument::Indented));
    m_status->setText(tr("Saved → %1").arg(path));
}
