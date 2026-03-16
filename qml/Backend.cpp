#include "Backend.h"
#include <QUrl>
#include <QSettings>
#include <QDir>
#include <cstdlib>

extern "C" {
void noise_profile_from_patch(const uint16_t *bayer, int bayer_w, int bayer_h,
                               int px, int py, int pw, int ph, CNoiseProfile *out);
uint16_t *noise_profile_read_frame(const char *path, int frame_index,
                                   int *out_width, int *out_height);
int denoise_preview_frame(const char *input_path, int frame_index,
                           const DenoiseCConfig *cfg, const char *temp_output_path);
}

/* Simple Bayer→RGB for preview */
QImage Backend::bayerToQImage(const uint16_t *bayer, int w, int h)
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

Backend::Backend(QObject *parent) : QObject(parent)
{
    QSettings settings("BayerFlow", "BayerFlow");
    m_isFirstLaunch = !settings.value("onboardingShown", false).toBool();
    m_trainingConsent = settings.value("trainingDataConsent", false).toBool();
}

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
    m_denoisedImage = QImage();  /* clear old denoised */
    m_showDenoised = false;

    /* Probe frame count */
    m_frameCount = denoise_probe_frame_count(path.toLocal8Bit().data());
    if (m_frameCount <= 0) m_frameCount = 0;
    m_previewFrameIndex = m_frameCount / 2;  /* default to middle frame */

    emit inputPathChanged();
    emit previewFrameChanged();
    emit previewModeChanged();

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

QImage Backend::previewImage() const
{
    if (m_showDenoised && !m_denoisedImage.isNull())
        return m_denoisedImage;
    return m_originalImage;
}

void Backend::loadPreview()
{
    if (m_inputPath.isEmpty()) { setStatus("Select an input file first"); return; }
    setStatus("Loading preview...");

    free(m_bayer);
    m_bayer = noise_profile_read_frame(m_inputPath.toLocal8Bit().data(),
                                        m_previewFrameIndex, &m_bayerW, &m_bayerH);
    if (!m_bayer) { setStatus("Failed to decode frame"); return; }

    m_originalImage = bayerToQImage(m_bayer, m_bayerW, m_bayerH);
    m_previewImage = m_originalImage;
    emit previewChanged();
    emit originalPreviewReady();
    setStatus(QString("%1x%2 frame %3 — drag to select noise patch")
        .arg(m_bayerW).arg(m_bayerH).arg(m_previewFrameIndex));
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

void Backend::generateDenoisedPreview()
{
    if (m_inputPath.isEmpty()) { setStatus("Select an input file first"); return; }
    if (m_previewLoading) return;

    m_previewLoading = true;
    emit previewLoadingChanged();
    setStatus("Generating denoised preview...");

    QThread *t = QThread::create([this]() {
        QByteArray inPath = m_inputPath.toLocal8Bit();

        /* Build config */
        DenoiseCConfig cfg;
        memset(&cfg, 0, sizeof(cfg));
        cfg.window_size          = m_windowSize > 5 ? 5 : m_windowSize;  /* small window for speed */
        cfg.strength             = m_strength;
        cfg.spatial_strength     = m_spatialStrength;
        cfg.temporal_filter_mode = m_tfMode;
        cfg.use_cnn_postfilter   = 1;
        cfg.auto_dark_frame      = 1;
        cfg.black_level          = m_noiseValid ? m_noiseBlackLevel : 0;
        cfg.shot_gain            = m_noiseValid ? m_noiseShotGain : 0;
        cfg.read_noise           = m_noiseValid ? m_noiseReadNoise : 0;

        /* Output to temp file */
        QString tempPath = QDir::tempPath() + "/bayerflow_preview_" +
            QString::number(QDateTime::currentMSecsSinceEpoch()) + ".mov";
        QByteArray tempPathBytes = tempPath.toLocal8Bit();

        int result = denoise_preview_frame(inPath.data(), m_previewFrameIndex,
                                            &cfg, tempPathBytes.data());

        if (result == DENOISE_OK) {
            /* Read the denoised frame from the temp file */
            int w = 0, h = 0;
            uint16_t *denoised = noise_profile_read_frame(tempPathBytes.data(), 0, &w, &h);
            if (denoised) {
                m_denoisedImage = bayerToQImage(denoised, w, h);
                free(denoised);
                m_showDenoised = true;

                QMetaObject::invokeMethod(this, [this]() {
                    emit previewChanged();
                    emit denoisedPreviewReady();
                    emit previewModeChanged();
                    setStatus(QString("Preview ready — click Before/After to compare"));
                }, Qt::QueuedConnection);
            } else {
                QMetaObject::invokeMethod(this, [this]() {
                    setStatus("Preview: failed to read denoised frame");
                }, Qt::QueuedConnection);
            }
        } else {
            QMetaObject::invokeMethod(this, [this, result]() {
                setStatus(QString("Preview failed (error %1)").arg(result));
            }, Qt::QueuedConnection);
        }

        /* Cleanup temp file */
        QFile::remove(tempPath);

        m_previewLoading = false;
        QMetaObject::invokeMethod(this, "previewLoadingChanged", Qt::QueuedConnection);
    });

    t->start();
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
        cfg.collect_training_data = m_trainingConsent ? 1 : 0;
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

void Backend::markOnboardingDone()
{
    QSettings settings("BayerFlow", "BayerFlow");
    settings.setValue("onboardingShown", true);
    m_isFirstLaunch = false;
}

void Backend::setTrainingConsent(bool v)
{
    if (m_trainingConsent == v) return;
    m_trainingConsent = v;
    QSettings settings("BayerFlow", "BayerFlow");
    settings.setValue("trainingDataConsent", v);
    emit trainingConsentChanged();
}
