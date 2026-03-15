#include "PreviewWidget.h"
#include <algorithm>

PreviewWidget::PreviewWidget(QWidget *parent)
    : QLabel(parent), m_rubberBand(new QRubberBand(QRubberBand::Rectangle, this))
{
    setMinimumSize(320, 200);
    setAlignment(Qt::AlignCenter);
    setStyleSheet("background: #1a1a1a;");
    setText("Load a frame to preview");
}

void PreviewWidget::setPreviewImage(const QImage &img)
{
    m_fullImage = img;
    updateDisplay();
}

void PreviewWidget::updateDisplay()
{
    if (m_fullImage.isNull()) return;

    QSize ws = size();
    QSize is = m_fullImage.size();
    float sx = (float)ws.width() / is.width();
    float sy = (float)ws.height() / is.height();
    m_scale = std::min(sx, sy);

    int dw = (int)(is.width() * m_scale);
    int dh = (int)(is.height() * m_scale);
    m_offset = QPoint((ws.width() - dw) / 2, (ws.height() - dh) / 2);

    QImage scaled = m_fullImage.scaled(dw, dh, Qt::KeepAspectRatio, Qt::SmoothTransformation);
    setPixmap(QPixmap::fromImage(scaled));
}

void PreviewWidget::resizeEvent(QResizeEvent *e)
{
    QLabel::resizeEvent(e);
    updateDisplay();
}

QRect PreviewWidget::widgetToImage(QRect wr) const
{
    if (m_scale <= 0) return QRect();
    int ix = (int)((wr.x() - m_offset.x()) / m_scale);
    int iy = (int)((wr.y() - m_offset.y()) / m_scale);
    int iw = (int)(wr.width() / m_scale);
    int ih = (int)(wr.height() / m_scale);

    /* The preview is half-res (Bayer 2x2 → 1 pixel), scale back to full Bayer coords */
    return QRect(ix * 2, iy * 2, iw * 2, ih * 2);
}

void PreviewWidget::mousePressEvent(QMouseEvent *e)
{
    m_origin = e->pos();
    m_rubberBand->setGeometry(QRect(m_origin, QSize()));
    m_rubberBand->show();
}

void PreviewWidget::mouseMoveEvent(QMouseEvent *e)
{
    m_rubberBand->setGeometry(QRect(m_origin, e->pos()).normalized());
}

void PreviewWidget::mouseReleaseEvent(QMouseEvent *e)
{
    QRect wr = QRect(m_origin, e->pos()).normalized();
    m_rubberBand->hide();

    if (wr.width() < 10 || wr.height() < 10) return; /* too small */

    m_imageRect = widgetToImage(wr);
    emit rectSelected(m_imageRect);
}
