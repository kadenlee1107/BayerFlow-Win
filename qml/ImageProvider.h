#pragma once
#include <QQuickImageProvider>
#include "Backend.h"

class PreviewImageProvider : public QQuickImageProvider {
public:
    PreviewImageProvider(Backend *backend)
        : QQuickImageProvider(QQuickImageProvider::Image), m_backend(backend) {}

    QImage requestImage(const QString &id, QSize *size, const QSize &requestedSize) override {
        QImage img;

        /* Support separate original/denoised requests for compare view */
        if (id.startsWith("original"))
            img = m_backend->originalImage();
        else if (id.startsWith("denoised"))
            img = m_backend->denoisedImage();
        else
            img = m_backend->previewImage();

        if (size) *size = img.size();
        if (requestedSize.isValid() && !img.isNull())
            return img.scaled(requestedSize, Qt::KeepAspectRatio, Qt::SmoothTransformation);
        return img;
    }

private:
    Backend *m_backend;
};
