#pragma once
#include <QLabel>
#include <QRubberBand>
#include <QMouseEvent>
#include <QImage>

class PreviewWidget : public QLabel {
    Q_OBJECT
public:
    explicit PreviewWidget(QWidget *parent = nullptr);
    void setPreviewImage(const QImage &img);
    QRect selectedRect() const { return m_imageRect; }

signals:
    void rectSelected(QRect imageRect);

protected:
    void mousePressEvent(QMouseEvent *e) override;
    void mouseMoveEvent(QMouseEvent *e) override;
    void mouseReleaseEvent(QMouseEvent *e) override;
    void resizeEvent(QResizeEvent *e) override;

private:
    QRubberBand *m_rubberBand;
    QPoint m_origin;
    QImage m_fullImage;
    QRect m_imageRect;    /* selected rect in image coords */
    float m_scale = 1.0f; /* display scale factor */
    QPoint m_offset;      /* display offset */

    QRect widgetToImage(QRect wr) const;
    void updateDisplay();
};
