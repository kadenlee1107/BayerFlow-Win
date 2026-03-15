#include "Backend.h"
#include <QUrl>
#include <cstdlib>

extern "C" {
void noise_profile_from_patch(const uint16_t *bayer, int bayer_w, int bayer_h,
                               int px, int py, int pw, int ph, CNoiseProfile *out);
uint16_t *noise_profile_read_frame(const char *path, int frame_index,
                                   int *out_width, int *out_height);
}

/* Simple Bayer→RGB for preview */
static QImage bayerToQImage(const uint16_t *bayer, int w, int h)
{
    QImage img(w / 2, h / 2, QImage::Format_RGB888);
    for (int y = 0; y < h - 1; y += 2) {
        uchar *line = img.scanLine(y / 2);
        for (int x = 0; x < w - 1; x += 2) {
            uint16_t r  = bayer[y * w + x];
            uint16_t gr = bayer[y * w + x + 1];
            uint16_t gb = bayer[(y + 1) * w + x];
            uint16_t b  = bayer[(y + 1) * w + x + 1];
            int px3 = (x / 2) * 3;
            line[px3 + 0] = (uchar)(r >> 8);
            line[px3 + 1] = (uchar)(((gr + gb) / 2) >> 8);
            line[px3 + 2] = (uchar)(b >> 8);
        }
    }
    return img;
}

Backend::Backend(QObject *parent) : QObject(parent) {}

Backend::~Backend()
{
    free(m_bayer);
}

void Backend::setInputPath(const QString &p)
{
    QString path = p;
    if (path.startsWith("file:///")) path = QUrl(path).toLocalFile();
    if (m_inputPath == path) return;
    m_inputPath = path;
    emit inputPathChanged();

    /* Auto-fill output */
    QString out = path;
    int dot = out.lastIndexOf('.');
    if (dot > 0) out.insert(dot, "_denoised");
    setOutputPath(out);
}

void Backend::setOutputPath(const QString &p)
{
    QString path = p;
    if (path.startsWith("file:///")) path = QUrl(path).toLocalFile();
    if (m_outputPath == path) return;
    m_outputPath = path;
    emit outputPathChanged();
}

void Backend::setStatus(const QString &s)
{
    m_statusText = s;
    emit statusChanged();
}

void Backend::loadPreview()
{
    if (m_inputPath.isEmpty()) { setStatus("Select an input file first"); return; }
    setStatus("Loading preview...");

    free(m_bayer);
    m_bayer = noise_profile_read_frame(m_inputPath.toLocal8Bit().data(), 0, &m_bayerW, &m_bayerH);
    if (!m_bayer) { setStatus("Failed to decode frame"); return; }

    m_previewImage = bayerToQImage(m_bayer, m_bayerW, m_bayerH);
    emit previewChanged();
    setStatus(QString("%1x%2 loaded — drag to select noise patch").arg(m_bayerW).arg(m_bayerH));
}

void Backend::profileNoise(int x, int y, int w, int h)
{
    if (!m_bayer) return;

    CNoiseProfile p;
    noise_profile_from_patch(m_bayer, m_bayerW, m_bayerH, x, y, w, h, &p);

    if (!p.valid) { setStatus("Patch too small"); return; }

    m_noiseBlackLevel = p.black_level;
    m_noiseShotGain = p.shot_gain;
    m_noiseReadNoise = p.read_noise;
    m_noiseSigma = p.sigma;
    m_noiseValid = true;
    emit noiseProfileChanged();

    setStatus(QString("Profiled: BL=%1  SG=%2  RN=%3  sigma=%4")
        .arg(p.black_level, 0, 'f', 1).arg(p.shot_gain, 0, 'f', 3)
        .arg(p.read_noise, 0, 'f', 1).arg(p.sigma, 0, 'f', 1));
}

int Backend::progressCB(int current, int total, void *ctx)
{
    auto *self = static_cast<Backend *>(ctx);
    self->m_progressPercent = total > 0 ? current * 100 / total : 0;
    self->m_statusText = QString("Frame %1 / %2").arg(current).arg(total);
    QMetaObject::invokeMethod(self, "progressChanged", Qt::QueuedConnection);
    QMetaObject::invokeMethod(self, "statusChanged", Qt::QueuedConnection);
    return self->m_cancel.load() ? 1 : 0;
}

void Backend::startDenoise()
{
    if (m_inputPath.isEmpty() || m_outputPath.isEmpty()) return;
    if (m_processing) return;

    m_processing = true;
    m_cancel.store(false);
    m_progressPercent = 0;
    emit processingChanged();
    emit progressChanged();
    setStatus("Starting...");

    /* Run in a lambda on QThread */
    m_thread = QThread::create([this]() {
        QByteArray inPath = m_inputPath.toLocal8Bit();
        QByteArray outPath = m_outputPath.toLocal8Bit();

        int w = 0, h = 0;
        denoise_probe_dimensions(inPath.data(), &w, &h);
        if (w > 0 && h > 0)
            platform_gpu_ring_init(m_windowSize, w, h);

        DenoiseCConfig cfg;
        memset(&cfg, 0, sizeof(cfg));
        cfg.window_size          = m_windowSize;
        cfg.strength             = m_strength;
        cfg.spatial_strength     = m_spatialStrength;
        cfg.temporal_filter_mode = m_tfMode;
        cfg.use_cnn_postfilter   = 1;
        cfg.auto_dark_frame      = 1;
        cfg.output_format        = 0;
        cfg.black_level          = m_noiseValid ? m_noiseBlackLevel : 0;
        cfg.shot_gain            = m_noiseValid ? m_noiseShotGain : 0;
        cfg.read_noise           = m_noiseValid ? m_noiseReadNoise : 0;

        int result = denoise_file(inPath.data(), outPath.data(), &cfg, progressCB, this);

        m_processing = false;
        m_progressPercent = result == DENOISE_OK ? 100 : 0;

        if (result == DENOISE_OK)
            m_statusText = "Done! " + m_outputPath;
        else if (result == DENOISE_ERR_CANCELLED)
            m_statusText = "Cancelled";
        else
            m_statusText = QString("Failed (error %1)").arg(result);

        QMetaObject::invokeMethod(this, "processingChanged", Qt::QueuedConnection);
        QMetaObject::invokeMethod(this, "progressChanged", Qt::QueuedConnection);
        QMetaObject::invokeMethod(this, "statusChanged", Qt::QueuedConnection);
    });

    m_thread->start();
}

void Backend::cancelDenoise()
{
    m_cancel.store(true);
    setStatus("Cancelling...");
}
