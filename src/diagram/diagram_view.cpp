// SPDX-License-Identifier: GPL-3.0-or-later
//
// DiagramView — native-Qt diagram widget (no WebEngine). See diagram_view.h.

#include "diagram_view.h"

#include <QBuffer>
#include <QFile>
#include <QFileInfo>
#include <QImage>
#include <QImageWriter>
#include <QMouseEvent>
#include <QPainter>
#include <QPdfWriter>
#include <QPageSize>
#include <QToolTip>
#ifdef NOTEPATRA_WITH_SVG
#include <QSvgGenerator>
#endif
#include <QWheelEvent>
#include <QtMath>
#include <algorithm>

DiagramView::DiagramView(QWidget *parent) : QWidget(parent) {
    setMinimumSize(220, 160);
    setMouseTracking(true);
    setCursor(Qt::OpenHandCursor);
    m_pal = DiagramRender::palette(QStringLiteral("default"));
}

DiagramView::~DiagramView() = default;

QStringList DiagramView::supportedExportFormats() {
    QStringList f; f << QStringLiteral("PNG");
    if (QImageWriter::supportedImageFormats().contains("webp")) f << QStringLiteral("WebP");
    f << QStringLiteral("JPEG");
#ifdef NOTEPATRA_WITH_SVG
    f << QStringLiteral("SVG");
#endif
    f << QStringLiteral("PDF");
#ifdef NOTEPATRA_WITH_SVG
    f << QStringLiteral("HTML");
#endif
    return f;
}

void DiagramView::setSource(const QString &npdText) {
    m_diag = Npd::parse(npdText);
    m_pal  = DiagramRender::palette(m_diag.palette);
    m_lay  = DiagramRender::computeLayout(m_diag);
    m_have = true;
    fitToView();
    update();
    if (!m_diag.ok())
        emit renderError(m_diag.errors.join(QStringLiteral("; ")));
}

void DiagramView::fitToView() {
    if (!m_have || m_lay.canvasW <= 0 || m_lay.canvasH <= 0) { m_zoom = 1.0; m_pan = QPointF(0,0); return; }
    const qreal availW = std::max(1, width()), availH = std::max(1, height());
    m_zoom = std::min(availW / m_lay.canvasW, availH / m_lay.canvasH) * 0.94;
    m_zoom = qBound(0.05, m_zoom, 4.0);
    m_pan = QPointF((availW - m_lay.canvasW * m_zoom) / 2.0,
                    (availH - m_lay.canvasH * m_zoom) / 2.0);
}

QPointF DiagramView::widgetToScene(const QPointF &w) const {
    return QPointF((w.x() - m_pan.x()) / m_zoom, (w.y() - m_pan.y()) / m_zoom);
}

void DiagramView::paintEvent(QPaintEvent *) {
    QPainter p(this);
    p.fillRect(rect(), m_have ? m_pal.bg : QColor("#1e1e1e"));
    if (!m_have) {
        p.setPen(QColor("#9aa4b2"));
        p.drawText(rect(), Qt::AlignCenter, QStringLiteral("Diagram preview — type .npd on the left"));
        return;
    }
    p.translate(m_pan);
    p.scale(m_zoom, m_zoom);
    DiagramRender::paint(p, m_diag, m_lay, m_pal);
}

void DiagramView::wheelEvent(QWheelEvent *e) {
    if (!m_have) return;
    const qreal factor = (e->angleDelta().y() > 0) ? 1.12 : 1.0/1.12;
    const QPointF before = widgetToScene(e->position());
    m_zoom = qBound(0.05, m_zoom * factor, 6.0);
    // keep the scene point under the cursor fixed
    m_pan = e->position() - before * m_zoom;
    update();
}

void DiagramView::mousePressEvent(QMouseEvent *e) {
    if (e->button() == Qt::LeftButton) { m_dragging = true; m_lastPos = e->pos(); setCursor(Qt::ClosedHandCursor); }
}

void DiagramView::mouseMoveEvent(QMouseEvent *e) {
    if (m_dragging) {
        m_pan += QPointF(e->pos() - m_lastPos);
        m_lastPos = e->pos();
        update();
        return;
    }
    if (!m_have) return;
    const QString id = DiagramRender::nodeAt(m_diag, m_lay, widgetToScene(e->pos()));
    QString hover;
    if (!id.isEmpty()) for (const auto &n : m_diag.nodes) if (n.id == id) { hover = n.hover; break; }
    if (!hover.isEmpty()) QToolTip::showText(e->globalPos(), hover, this);
    else QToolTip::hideText();
}

void DiagramView::mouseReleaseEvent(QMouseEvent *e) {
    if (e->button() == Qt::LeftButton) { m_dragging = false; setCursor(Qt::OpenHandCursor); }
}

void DiagramView::mouseDoubleClickEvent(QMouseEvent *) {
    fitToView();
    update();
}

bool DiagramView::exportTo(const QString &format, const QString &path) {
    if (!m_have) { emit renderError(QStringLiteral("Nothing to export yet.")); return false; }
    const QString fmt = format.toLower();
    const int W = qMax(1, qRound(m_lay.canvasW));
    const int H = qMax(1, qRound(m_lay.canvasH));

    auto paintInto = [&](QPainter &p){ DiagramRender::paint(p, m_diag, m_lay, m_pal); };

    if (fmt=="png" || fmt=="webp" || fmt=="jpeg" || fmt=="jpg") {
        const qreal scale = 2.0;
        QImage img(int(W*scale), int(H*scale), QImage::Format_ARGB32);
        img.fill(m_pal.bg);
        QPainter p(&img); p.scale(scale, scale); paintInto(p); p.end();
        const char *q = (fmt=="jpeg"||fmt=="jpg") ? "JPEG" : (fmt=="webp" ? "WEBP" : "PNG");
        if (!img.save(path, q)) { emit renderError(QStringLiteral("Could not write %1").arg(path)); return false; }
        return true;
    }
    if (fmt=="svg" || fmt=="html") {
#ifndef NOTEPATRA_WITH_SVG
        emit renderError(QStringLiteral("SVG/HTML export needs the Qt Svg module (not available in this build). Use PNG or PDF."));
        return false;
#else
        QByteArray svg;
        { QBuffer buf(&svg);
          QSvgGenerator gen; gen.setOutputDevice(&buf);
          gen.setSize(QSize(W,H)); gen.setViewBox(QRect(0,0,W,H));
          gen.setTitle(m_diag.title); gen.setDescription(QStringLiteral("Notepatra .npd diagram"));
          QPainter p(&gen); paintInto(p); p.end();
        }
        if (fmt=="svg") {
            QFile f(path); if (!f.open(QIODevice::WriteOnly)) { emit renderError(QStringLiteral("Could not write %1").arg(path)); return false; }
            f.write(svg); return true;
        }
        // html: embed the SVG inline (scalable, self-contained, browser-native)
        QFile f(path); if (!f.open(QIODevice::WriteOnly)) { emit renderError(QStringLiteral("Could not write %1").arg(path)); return false; }
        const QString title = m_diag.title.isEmpty() ? QStringLiteral("Notepatra Diagram") : m_diag.title;
        QByteArray html = "<!doctype html>\n<html><head><meta charset=\"utf-8\">\n<title>"
            + title.toUtf8() + "</title>\n<style>body{margin:0;background:"
            + m_pal.bg.name().toUtf8() + ";display:flex;justify-content:center}svg{max-width:100%;height:auto}</style>\n"
            "</head>\n<body>\n" + svg + "\n</body></html>\n";
        f.write(html); return true;
#endif
    }
    if (fmt=="pdf") {
        QPdfWriter pdf(path);
        pdf.setPageSize(QPageSize(QSizeF(W, H), QPageSize::Point, QString(), QPageSize::ExactMatch));
        pdf.setResolution(72);
        QPainter p(&pdf); paintInto(p); p.end();
        return QFileInfo::exists(path);
    }
    emit renderError(QStringLiteral("Unsupported export format: %1").arg(format));
    return false;
}
