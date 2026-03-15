#include "DenoiseWorker.h"

extern "C" {
#include "platform_gpu.h"
}

DenoiseWorker::DenoiseWorker(QObject *parent) : QObject(parent) {}

void DenoiseWorker::configure(const QString &input, const QString &output,
                               float strength, int window, int tfMode, float spatial,
                               float blackLevel, float shotGain, float readNoise)
{
    m_input = input;
    m_output = output;
    m_strength = strength;
    m_window = window;
    m_tfMode = tfMode;
    m_spatial = spatial;
    m_blackLevel = blackLevel;
    m_shotGain = shotGain;
    m_readNoise = readNoise;
    m_cancel.store(false);
}

int DenoiseWorker::progressCB(int current, int total, void *ctx)
{
    auto *self = static_cast<DenoiseWorker *>(ctx);
    emit self->progressChanged(current, total);
    return self->m_cancel.load() ? 1 : 0;
}

void DenoiseWorker::process()
{
    QByteArray inPath = m_input.toLocal8Bit();
    QByteArray outPath = m_output.toLocal8Bit();

    /* Probe dimensions for GPU init */
    int w = 0, h = 0;
    denoise_probe_dimensions(inPath.data(), &w, &h);
    if (w > 0 && h > 0)
        platform_gpu_ring_init(m_window, w, h);

    /* Configure */
    DenoiseCConfig cfg;
    memset(&cfg, 0, sizeof(cfg));
    cfg.window_size          = m_window;
    cfg.strength             = m_strength;
    cfg.spatial_strength     = m_spatial;
    cfg.temporal_filter_mode = m_tfMode;
    cfg.use_cnn_postfilter   = 1;
    cfg.protect_subjects     = 0;
    cfg.invert_mask          = 0;
    cfg.auto_dark_frame      = 1;
    cfg.output_format        = 0; /* auto */
    cfg.black_level          = m_blackLevel;
    cfg.shot_gain            = m_shotGain;
    cfg.read_noise           = m_readNoise;
    cfg.start_frame          = 0;
    cfg.end_frame            = 0; /* all */

    int result = denoise_file(inPath.data(), outPath.data(), &cfg, progressCB, this);

    emit finished(result);
}
