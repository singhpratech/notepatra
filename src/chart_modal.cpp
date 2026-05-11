#include "chart_modal.h"
#include "chartrender.h"

#include <QApplication>
#include <QClipboard>
#include <QFileDialog>
#include <QHBoxLayout>
#include <QLabel>
#include <QMessageBox>
#include <QPixmap>
#include <QPushButton>
#include <QStandardPaths>
#include <QStyle>
#include <QVBoxLayout>
#include <QDateTime>

ChartModalDialog::ChartModalDialog(const QJsonObject &spec, QWidget *parent)
    : QDialog(parent), m_spec(spec)
{
    setWindowTitle(tr("Chart Viewer"));
    setSizeGripEnabled(true);
    resize(960, 640);

    auto *root = new QVBoxLayout(this);
    root->setContentsMargins(16, 16, 16, 16);
    root->setSpacing(10);

    // Re-render the chart fresh — an independent QChartView at modal
    // size, with its own hover tooltips wired by renderFromObject().
    QString err;
    QWidget *w = ChartRender::renderFromObject(spec, this, &err);
    if (!w) {
        auto *errLbl = new QLabel(
            tr("Could not render chart: %1").arg(err.isEmpty() ? tr("unknown error") : err),
            this);
        errLbl->setStyleSheet("color: #c0392b; padding: 20px;");
        errLbl->setWordWrap(true);
        root->addWidget(errLbl, 1);
    } else {
        w->setMinimumSize(720, 480);
        root->addWidget(w, 1);
        m_chartHost = w;
    }

    // Button row.
    auto *btnRow = new QHBoxLayout();
    m_status = new QLabel(this);
    m_status->setStyleSheet("color: #6a737d; font-size: 11px;");
    btnRow->addWidget(m_status, 1);

    auto *copyBtn = new QPushButton(tr("Copy image"), this);
    copyBtn->setIcon(style()->standardIcon(QStyle::SP_DialogResetButton));
    btnRow->addWidget(copyBtn);

    auto *saveBtn = new QPushButton(tr("Save as PNG..."), this);
    saveBtn->setIcon(style()->standardIcon(QStyle::SP_DialogSaveButton));
    saveBtn->setDefault(true);
    btnRow->addWidget(saveBtn);

    auto *closeBtn = new QPushButton(tr("Close"), this);
    closeBtn->setIcon(style()->standardIcon(QStyle::SP_DialogCloseButton));
    btnRow->addWidget(closeBtn);

    root->addLayout(btnRow);

    connect(copyBtn,  &QPushButton::clicked, this, &ChartModalDialog::onCopyImage);
    connect(saveBtn,  &QPushButton::clicked, this, &ChartModalDialog::onSavePng);
    connect(closeBtn, &QPushButton::clicked, this, &QDialog::accept);
}

void ChartModalDialog::onSavePng() {
    if (!m_chartHost) return;

    // Default to user's Pictures dir (Linux: ~/Pictures, macOS:
    // ~/Pictures, Windows: %USERPROFILE%/Pictures). Falls back to home
    // when Pictures isn't a real path on the user's system.
    QString defaultDir = QStandardPaths::writableLocation(QStandardPaths::PicturesLocation);
    if (defaultDir.isEmpty()) defaultDir = QDir::homePath();
    const QString stamp = QDateTime::currentDateTime().toString("yyyyMMdd-HHmmss");
    const QString defaultName = QString("notepatra-chart-%1.png").arg(stamp);
    const QString defaultPath = QDir(defaultDir).absoluteFilePath(defaultName);

    const QString path = QFileDialog::getSaveFileName(
        this,
        tr("Save chart as PNG"),
        defaultPath,
        tr("PNG Image (*.png)"));
    if (path.isEmpty()) return;

    // Render at the modal-canvas's current size — what you see is what
    // gets saved. devicePixelRatio() picks up HiDPI so the PNG is crisp
    // on 200 % displays. Adds a small DPR-aware buffer.
    const qreal dpr = devicePixelRatioF();
    QPixmap pix(m_chartHost->size() * dpr);
    pix.setDevicePixelRatio(dpr);
    pix.fill(Qt::white);
    m_chartHost->render(&pix);

    if (!pix.save(path, "PNG")) {
        QMessageBox::warning(this, tr("Save failed"),
            tr("Could not write %1").arg(path));
        return;
    }
    m_status->setText(tr("Saved → %1").arg(path));
}

void ChartModalDialog::onCopyImage() {
    if (!m_chartHost) return;
    const qreal dpr = devicePixelRatioF();
    QPixmap pix(m_chartHost->size() * dpr);
    pix.setDevicePixelRatio(dpr);
    pix.fill(Qt::white);
    m_chartHost->render(&pix);
    QApplication::clipboard()->setPixmap(pix);
    m_status->setText(tr("Image copied to clipboard"));
}
