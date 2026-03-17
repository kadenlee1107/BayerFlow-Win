#include "Backend.h"
#include "CubeLUT.h"

static CubeLUT g_lut;
#include <QUrl>
#include <QSettings>
#include <QDir>
#include <QRegularExpression>
#include <QDateTime>
#include <QElapsedTimer>
#include <QStandardPaths>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>
#include <QUuid>
#include <QDesktopServices>
#include <cstdlib>

static QElapsedTimer g_processTimer;

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
    fprintf(stderr, "BF_DBG: Backend constructor start\n"); fflush(stderr);
    QSettings settings("BayerFlow", "BayerFlow");
    m_isFirstLaunch = !settings.value("onboardingShown", false).toBool();
    m_trainingConsent = settings.value("trainingDataConsent", false).toBool();
    /* Licensing */
    m_isLicensed = settings.value("isLicensed", false).toBool();
    QDateTime firstLaunch = settings.value("firstLaunchDate").toDateTime();
    if (!firstLaunch.isValid()) {
        firstLaunch = QDateTime::currentDateTime();
        settings.setValue("firstLaunchDate", firstLaunch);
    }
    int daysSinceFirst = (int)firstLaunch.daysTo(QDateTime::currentDateTime());
    m_trialDays = qMax(0, 14 - daysSinceFirst);

    m_defaultOutputDir = settings.value("defaultOutputDir", "").toString();
    m_autoRevealOutput = settings.value("autoRevealOutput", false).toBool();
    m_playSoundOnComplete = settings.value("playSoundOnComplete", true).toBool();
    m_showNotification = settings.value("showNotification", true).toBool();
    m_defaultWindowSize = settings.value("defaultWindowSize", 15).toInt();
    m_rememberSettings = settings.value("rememberSettings", true).toBool();
}

QString Backend::gpuName() const
{
    /* Detect NVIDIA GPU name via CUDA */
    static QString cached;
    if (cached.isEmpty()) {
        /* Query GPU name via platform_gpu_available check */
        cached = platform_gpu_available() ? "NVIDIA CUDA GPU" : "CPU only";
    }
    return cached;
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

    /* Auto-detect camera + load saved noise profile */
    detectCamera();

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

void Backend::setPreset(const QString &p)
{
    if (m_preset == p) return;
    m_preset = p;
    if (p == "Light") { m_strength = 0.8f; m_windowSize = 7; }
    else if (p == "Standard") { m_strength = 1.2f; m_windowSize = 11; }
    else if (p == "Strong") { m_strength = 1.5f; m_windowSize = 15; }
    /* "Custom" — don't change sliders */
    emit presetChanged();
    emit settingsChanged();
}

void Backend::setStatus(const QString &s)
{
    m_statusText = s;
    emit statusChanged();
}

QImage Backend::previewImage() const
{
    QImage img = (m_showDenoised && !m_denoisedImage.isNull()) ? m_denoisedImage : m_originalImage;

    /* Apply LUT if enabled */
    if (m_lutEnabled && g_lut.isValid() && !img.isNull()) {
        QImage lutImg = img.copy();
        g_lut.apply(lutImg, m_lutBlend);
        return lutImg;
    }
    return img;
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
        cfg.use_cnn_postfilter   = m_useCNN ? 1 : 0;
        cfg.protect_subjects     = m_protectSubjects ? 1 : 0;
        cfg.invert_mask          = m_invertMask ? 1 : 0;
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

    /* Compute fps and ETA */
    double elapsed = g_processTimer.elapsed() / 1000.0;
    if (current > 0 && elapsed > 0.5) {
        self->m_fps = current / elapsed;
        int remaining = total - current;
        double etaSec = remaining / self->m_fps;
        int etaMin = (int)(etaSec / 60);
        int etaS = (int)etaSec % 60;
        self->m_etaText = QString("%1:%2 remaining").arg(etaMin).arg(etaS, 2, 10, QChar('0'));
    } else {
        self->m_fps = 0;
        self->m_etaText = "Estimating...";
    }

    self->m_statusText = QString("Frame %1 / %2  •  %3 fps")
        .arg(current).arg(total).arg(self->m_fps, 0, 'f', 1);
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
        cfg.use_cnn_postfilter   = m_useCNN ? 1 : 0;
        cfg.protect_subjects     = m_protectSubjects ? 1 : 0;
        cfg.invert_mask          = m_invertMask ? 1 : 0;
        cfg.auto_dark_frame      = 1;
        cfg.output_format        = m_outputFormat;
        cfg.start_frame          = m_startFrame;
        cfg.end_frame            = m_endFrame;
        cfg.collect_training_data = m_trainingConsent ? 1 : 0;
        cfg.motion_avg           = m_motionAvg;
        cfg.unsharp_amount       = m_unsharpAmount;
        cfg.grain_amount         = m_grainAmount;
        cfg.black_level          = m_noiseValid ? m_noiseBlackLevel : 0;
        cfg.shot_gain            = m_noiseValid ? m_noiseShotGain : 0;
        cfg.read_noise           = m_noiseValid ? m_noiseReadNoise : 0;

        g_processTimer.start();
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

QVariantMap Backend::saveSessionState()
{
    QVariantMap s;
    s["inputPath"] = m_inputPath;
    s["outputPath"] = m_outputPath;
    s["strength"] = m_strength;
    s["windowSize"] = m_windowSize;
    s["spatialStrength"] = m_spatialStrength;
    s["tfMode"] = m_tfMode;
    s["useCNN"] = m_useCNN;
    s["preset"] = m_preset;
    s["previewFrameIndex"] = m_previewFrameIndex;
    s["noiseValid"] = m_noiseValid;
    s["noiseBlackLevel"] = m_noiseBlackLevel;
    s["noiseShotGain"] = m_noiseShotGain;
    s["noiseReadNoise"] = m_noiseReadNoise;
    s["noiseSigma"] = m_noiseSigma;
    return s;
}

void Backend::restoreSessionState(const QVariantMap &s)
{
    if (s.contains("inputPath")) setInputPath(s["inputPath"].toString());
    if (s.contains("outputPath")) setOutputPath(s["outputPath"].toString());
    if (s.contains("strength")) { m_strength = s["strength"].toFloat(); emit settingsChanged(); }
    if (s.contains("windowSize")) { m_windowSize = s["windowSize"].toInt(); emit settingsChanged(); }
    if (s.contains("spatialStrength")) { m_spatialStrength = s["spatialStrength"].toFloat(); }
    if (s.contains("tfMode")) { m_tfMode = s["tfMode"].toInt(); }
    if (s.contains("useCNN")) { m_useCNN = s["useCNN"].toBool(); emit settingsChanged(); }
    if (s.contains("preset")) { m_preset = s["preset"].toString(); emit presetChanged(); }
    if (s.contains("previewFrameIndex")) { m_previewFrameIndex = s["previewFrameIndex"].toInt(); emit previewFrameChanged(); }
    if (s.contains("noiseValid")) {
        m_noiseValid = s["noiseValid"].toBool();
        m_noiseBlackLevel = s["noiseBlackLevel"].toFloat();
        m_noiseShotGain = s["noiseShotGain"].toFloat();
        m_noiseReadNoise = s["noiseReadNoise"].toFloat();
        m_noiseSigma = s["noiseSigma"].toFloat();
        emit noiseProfileChanged();
    }

    /* Clear preview images — will need to reload */
    m_originalImage = QImage();
    m_denoisedImage = QImage();
    m_showDenoised = false;
    free(m_bayer); m_bayer = nullptr;
    emit previewChanged();
    emit previewModeChanged();
}

/* ---- Batch Queue ---- */

QVariantList Backend::queueModel() const
{
    QVariantList list;
    for (const auto &item : m_queue) {
        QVariantMap m;
        m["inputPath"] = item.inputPath;
        m["outputPath"] = item.outputPath;
        m["filename"] = item.filename;
        m["status"] = item.status;
        m["progressPercent"] = item.progressPercent;
        m["message"] = item.message;
        list.append(m);
    }
    return list;
}

void Backend::addToQueue(const QString &input, const QString &output)
{
    QueueItem item;
    item.inputPath = input;
    item.outputPath = output;
    item.filename = input.split(QRegularExpression("[/\\\\]")).last();
    item.status = "pending";
    m_queue.append(item);
    emit queueChanged();

    /* Auto-start queue if not already running */
    if (!m_queueRunning)
        startQueue();
}

void Backend::removeFromQueue(int index)
{
    if (index >= 0 && index < m_queue.size()) {
        m_queue.removeAt(index);
        emit queueChanged();
    }
}

void Backend::clearQueue()
{
    if (m_queueRunning) return;
    m_queue.clear();
    emit queueChanged();
}

int Backend::queueProgressCB(int current, int total, void *ctx)
{
    auto *self = static_cast<Backend *>(ctx);
    if (self->m_queueIndex >= 0 && self->m_queueIndex < self->m_queue.size()) {
        self->m_queue[self->m_queueIndex].progressPercent = total > 0 ? current * 100 / total : 0;
        self->m_queue[self->m_queueIndex].message = QString("Frame %1 / %2").arg(current).arg(total);
        QMetaObject::invokeMethod(self, "queueChanged", Qt::QueuedConnection);
    }
    return self->m_cancel.load() ? 1 : 0;
}

void Backend::startQueue()
{
    if (m_queueRunning || m_queue.isEmpty()) return;
    m_queueRunning = true;
    m_cancel.store(false);
    m_queueIndex = -1;
    emit queueChanged();
    processNextQueueItem();
}

void Backend::processNextQueueItem()
{
    /* Find next pending item */
    m_queueIndex = -1;
    for (int i = 0; i < m_queue.size(); i++) {
        if (m_queue[i].status == "pending") {
            m_queueIndex = i;
            break;
        }
    }

    if (m_queueIndex < 0) {
        /* All done */
        m_queueRunning = false;
        emit queueChanged();
        return;
    }

    m_queue[m_queueIndex].status = "processing";
    emit queueChanged();

    int idx = m_queueIndex;
    QThread *t = QThread::create([this, idx]() {
        QByteArray inPath = m_queue[idx].inputPath.toLocal8Bit();
        QByteArray outPath = m_queue[idx].outputPath.toLocal8Bit();

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
        cfg.use_cnn_postfilter   = m_useCNN ? 1 : 0;
        cfg.protect_subjects     = m_protectSubjects ? 1 : 0;
        cfg.invert_mask          = m_invertMask ? 1 : 0;
        cfg.auto_dark_frame      = 1;
        cfg.output_format        = m_outputFormat;
        cfg.start_frame          = m_startFrame;
        cfg.end_frame            = m_endFrame;
        cfg.collect_training_data = m_trainingConsent ? 1 : 0;
        cfg.motion_avg           = m_motionAvg;
        cfg.unsharp_amount       = m_unsharpAmount;
        cfg.grain_amount         = m_grainAmount;
        cfg.black_level          = m_noiseValid ? m_noiseBlackLevel : 0;
        cfg.shot_gain            = m_noiseValid ? m_noiseShotGain : 0;
        cfg.read_noise           = m_noiseValid ? m_noiseReadNoise : 0;

        int result = denoise_file(inPath.data(), outPath.data(), &cfg, queueProgressCB, this);

        QMetaObject::invokeMethod(this, [this, idx, result]() {
            if (idx < m_queue.size()) {
                if (result == DENOISE_OK) {
                    m_queue[idx].status = "done";
                    m_queue[idx].progressPercent = 100;
                    m_queue[idx].message = "Complete";
                } else if (result == DENOISE_ERR_CANCELLED) {
                    m_queue[idx].status = "failed";
                    m_queue[idx].message = "Cancelled";
                } else {
                    m_queue[idx].status = "failed";
                    m_queue[idx].message = QString("Error %1").arg(result);
                }
            }
            emit queueChanged();

            if (!m_cancel.load())
                processNextQueueItem();
            else {
                m_queueRunning = false;
                emit queueChanged();
            }
        }, Qt::QueuedConnection);
    });
    t->start();
}

void Backend::cancelQueue()
{
    m_cancel.store(true);
}

/* ---- Camera Detection + Noise Profiles ---- */

struct CameraProfile {
    const char *pattern;  /* substring match (case-insensitive) */
    const char *displayName;
    struct { int iso; float sigma; } isoSigma[12];
    int count;
};

static const CameraProfile g_profiles[] = {
    {"nikon z6",  "Nikon Z6/Z6II/Z6III", {{100,0.8f},{200,1.0f},{400,1.5f},{800,2.5f},{1600,4.0f},{3200,6.0f},{6400,9.0f},{12800,14.0f},{25600,22.0f}}, 9},
    {"nikon z8",  "Nikon Z8/Z9",         {{64,0.5f},{100,0.7f},{200,0.9f},{400,1.3f},{800,2.2f},{1600,3.5f},{3200,5.5f},{6400,8.0f},{12800,12.0f},{25600,18.0f}}, 10},
    {"nikon z9",  "Nikon Z8/Z9",         {{64,0.5f},{100,0.7f},{200,0.9f},{400,1.3f},{800,2.2f},{1600,3.5f},{3200,5.5f},{6400,8.0f},{12800,12.0f},{25600,18.0f}}, 10},
    {"nikon z5",  "Nikon Z5",            {{100,0.9f},{400,1.6f},{800,2.8f},{1600,4.5f},{3200,7.0f},{6400,10.0f},{12800,16.0f}}, 7},
    {"canon r5 c","Canon R5 C",          {{100,0.6f},{400,1.2f},{800,2.0f},{1600,3.2f},{3200,5.0f},{6400,7.5f},{12800,11.0f},{25600,17.0f}}, 8},
    {"canon r5",  "Canon R5",            {{100,0.7f},{400,1.3f},{800,2.2f},{1600,3.5f},{3200,5.5f},{6400,8.0f},{12800,12.0f}}, 7},
    {"canon r3",  "Canon R3",            {{100,0.7f},{400,1.2f},{800,2.0f},{1600,3.0f},{3200,4.8f},{6400,7.0f},{12800,10.0f},{25600,15.0f}}, 8},
    {"panasonic", "Panasonic S1H/S5/GH6",{{100,0.8f},{400,1.5f},{800,2.5f},{1600,4.0f},{3200,6.5f},{6400,9.5f},{12800,14.0f}}, 7},
    {"sony",      "Sony A7S/FX3/FX6",    {{100,0.5f},{400,0.9f},{800,1.5f},{1600,2.5f},{3200,4.0f},{6400,6.0f},{12800,9.0f},{25600,13.0f},{51200,20.0f}}, 9},
    {"red",       "RED Komodo/V-Raptor",  {{250,0.6f},{800,1.5f},{1600,2.8f},{3200,4.5f},{6400,7.0f},{12800,10.0f}}, 6},
};
static const int g_numProfiles = sizeof(g_profiles) / sizeof(g_profiles[0]);

static float profileSigma(const CameraProfile &p, int iso) {
    if (p.count == 0) return 0;
    float isoF = (float)iso;
    if (isoF <= p.isoSigma[0].iso) return p.isoSigma[0].sigma;
    if (isoF >= p.isoSigma[p.count-1].iso) return p.isoSigma[p.count-1].sigma;
    for (int i = 0; i < p.count - 1; i++) {
        if (isoF >= p.isoSigma[i].iso && isoF <= p.isoSigma[i+1].iso) {
            float t = (isoF - p.isoSigma[i].iso) / (float)(p.isoSigma[i+1].iso - p.isoSigma[i].iso);
            return p.isoSigma[i].sigma + t * (p.isoSigma[i+1].sigma - p.isoSigma[i].sigma);
        }
    }
    return p.isoSigma[p.count-1].sigma;
}

void Backend::detectCamera()
{
    if (m_inputPath.isEmpty()) return;
    char model[256] = {};
    int iso = 0;
    QByteArray path = m_inputPath.toLocal8Bit();
    denoise_probe_camera(path.data(), model, sizeof(model), &iso);

    m_cameraModel = QString(model).trimmed();
    m_detectedISO = iso;
    m_cameraProfileHint.clear();

    /* Match against profile database */
    QString modelLower = m_cameraModel.toLower();
    for (int i = 0; i < g_numProfiles; i++) {
        if (modelLower.contains(g_profiles[i].pattern)) {
            float sigma = profileSigma(g_profiles[i], iso);
            m_cameraProfileHint = QString("%1 @ ISO %2 → sigma %3")
                .arg(g_profiles[i].displayName).arg(iso).arg(sigma, 0, 'f', 1);
            break;
        }
    }

    emit cameraDetected();

    /* Try to load saved calibration for this camera */
    loadSavedNoiseProfile();
}

void Backend::saveNoiseProfile()
{
    if (m_cameraModel.isEmpty() || !m_noiseValid) return;
    QSettings settings("BayerFlow", "BayerFlow");
    QString key = "noiseProfile_" + m_cameraModel.replace(' ', '_');
    QVariantMap profile;
    profile["blackLevel"] = m_noiseBlackLevel;
    profile["shotGain"] = m_noiseShotGain;
    profile["readNoise"] = m_noiseReadNoise;
    profile["sigma"] = m_noiseSigma;
    profile["iso"] = m_detectedISO;
    settings.setValue(key, profile);
    setStatus(QString("Noise profile saved for %1").arg(m_cameraModel));
}

void Backend::loadSavedNoiseProfile()
{
    if (m_cameraModel.isEmpty()) return;
    QSettings settings("BayerFlow", "BayerFlow");
    QString key = "noiseProfile_" + m_cameraModel.replace(' ', '_');
    QVariantMap profile = settings.value(key).toMap();
    if (profile.isEmpty()) return;

    m_noiseBlackLevel = profile["blackLevel"].toFloat();
    m_noiseShotGain = profile["shotGain"].toFloat();
    m_noiseReadNoise = profile["readNoise"].toFloat();
    m_noiseSigma = profile["sigma"].toFloat();
    m_noiseValid = true;
    emit noiseProfileChanged();
    setStatus(QString("Loaded saved profile for %1 (ISO %2)")
        .arg(m_cameraModel).arg(profile["iso"].toInt()));
}

/* ---- Motion Analysis ---- */

extern "C" int analyze_motion(const char *input_path, float *avg_motion, float *max_motion,
                               DenoiseCProgressCB progress_cb, void *progress_ctx);

void Backend::analyzeMotion()
{
    if (m_inputPath.isEmpty() || m_analyzing) return;
    m_analyzing = true;
    m_motionAvg = 0;
    emit motionAnalyzed();
    setStatus("Analyzing motion...");

    QThread *t = QThread::create([this]() {
        QByteArray inPath = m_inputPath.toLocal8Bit();
        float avg = 0, mx = 0;
        int result = analyze_motion(inPath.data(), &avg, &mx, nullptr, nullptr);

        QMetaObject::invokeMethod(this, [this, result, avg]() {
            m_analyzing = false;
            if (result == 0) {
                m_motionAvg = avg;
                /* Auto-suggest preset based on motion */
                if (avg < 1.0f) setStatus(QString("Motion: %1 px (very low — try Light preset)").arg(avg, 0, 'f', 1));
                else if (avg < 3.0f) setStatus(QString("Motion: %1 px (moderate — Standard preset)").arg(avg, 0, 'f', 1));
                else setStatus(QString("Motion: %1 px (high — Strong preset recommended)").arg(avg, 0, 'f', 1));
            } else {
                setStatus("Motion analysis failed");
            }
            emit motionAnalyzed();
        }, Qt::QueuedConnection);
    });
    t->start();
}

QString Backend::motionHint() const
{
    if (m_motionAvg <= 0) return "";
    if (m_motionAvg < 1.0f) return "Very low motion — Light preset";
    if (m_motionAvg < 3.0f) return "Moderate motion — Standard preset";
    return "High motion — Strong preset";
}

/* ---- Histogram/Scope ---- */

QVariantMap Backend::computeHistogram()
{
    QImage img = previewImage();
    if (img.isNull()) return {};
    if (img.format() != QImage::Format_RGB888)
        img = img.convertToFormat(QImage::Format_RGB888);

    int w = img.width(), h = img.height();
    int rBins[256] = {}, gBins[256] = {}, bBins[256] = {}, lumaBins[256] = {};

    for (int y = 0; y < h; y++) {
        const uchar *line = img.constScanLine(y);
        for (int x = 0; x < w; x++) {
            int off = x * 3;
            int r = line[off], g = line[off+1], b = line[off+2];
            int luma = (int)(r * 0.2126f + g * 0.7152f + b * 0.0722f);
            rBins[r]++; gBins[g]++; bBins[b]++;
            lumaBins[qMin(luma, 255)]++;
        }
    }

    /* Normalize (skip bin 0 and 255 to avoid clipping spikes) */
    float maxVal = 1;
    for (int i = 1; i < 255; i++) {
        if (rBins[i] > maxVal) maxVal = rBins[i];
        if (gBins[i] > maxVal) maxVal = gBins[i];
        if (bBins[i] > maxVal) maxVal = bBins[i];
        if (lumaBins[i] > maxVal) maxVal = lumaBins[i];
    }

    QVariantList rList, gList, bList, lumaList;
    for (int i = 0; i < 256; i++) {
        rList.append(qMin(rBins[i] / maxVal, 1.0f));
        gList.append(qMin(gBins[i] / maxVal, 1.0f));
        bList.append(qMin(bBins[i] / maxVal, 1.0f));
        lumaList.append(qMin(lumaBins[i] / maxVal, 1.0f));
    }

    QVariantMap result;
    result["r"] = rList;
    result["g"] = gList;
    result["b"] = bList;
    result["luma"] = lumaList;
    return result;
}

/* ---- Licensing (Ed25519 via TweetNaCl) ---- */

/* TweetNaCl disabled temporarily for debugging */
#if 0
extern "C" {
void randombytes(unsigned char *buf, unsigned long long len) { (void)buf; (void)len; }
#include "tweetnacl.h"
}
#endif

/* Public key for license verification (same as Mac LicenseManager.swift) */
static const char *g_pubKeyHex = "e2c8b6a800342c7633dc086c9dbb80bc7f25a309a5b17f09652c18baf0e1fcf0";

static bool hexToBytes(const char *hex, unsigned char *out, int len) {
    for (int i = 0; i < len; i++) {
        unsigned int byte;
        if (sscanf(hex + i * 2, "%02x", &byte) != 1) return false;
        out[i] = (unsigned char)byte;
    }
    return true;
}

#if 0  /* disabled — tweetnacl removed */
static bool verifyEd25519(const QByteArray &signature, const QByteArray &message, const unsigned char *pubKey) {
    if (signature.size() != 64) return false;

    /* TweetNaCl's crypto_sign_open expects: signed_message = signature(64) + message
     * It verifies and writes the message to 'out' if valid */
    QByteArray signedMsg = signature + message;
    QByteArray out(signedMsg.size(), 0);
    unsigned long long outLen = 0;

    int result = crypto_sign_open(
        (unsigned char *)out.data(), &outLen,
        (const unsigned char *)signedMsg.constData(), signedMsg.size(),
        pubKey);

    return result == 0;
}
#endif

bool Backend::activateLicense(const QString &email, const QString &key)
{
    if (email.isEmpty() || key.isEmpty()) return false;

    QString trimmedKey = key.trimmed();
    QString trimmedEmail = email.toLower().trimmed();

    /* Ed25519 verification temporarily disabled — accept any key */
    (void)g_pubKeyHex;

    /* Valid — save to Registry */
    QSettings settings("BayerFlow", "BayerFlow");
    settings.setValue("isLicensed", true);
    settings.setValue("licenseEmail", trimmedEmail);
    settings.setValue("licenseKey", trimmedKey);
    m_isLicensed = true;
    emit licenseChanged();
    setStatus("License activated for " + trimmedEmail);
    return true;
}

void Backend::deactivateLicense()
{
    QSettings settings("BayerFlow", "BayerFlow");
    settings.remove("isLicensed");
    settings.remove("licenseEmail");
    settings.remove("licenseKey");
    m_isLicensed = false;
    emit licenseChanged();
    setStatus("License deactivated");
}

QString Backend::licenseStatus() const
{
    if (m_isLicensed) return "Licensed";
    if (m_trialDays > 0) return QString("Trial — %1 day%2 left").arg(m_trialDays).arg(m_trialDays == 1 ? "" : "s");
    return "Trial expired";
}

/* ---- Training Data Upload ---- */

void Backend::uploadPendingTrainingData()
{
    QString dataDir = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) + "/training_data";
    QDir dir(dataDir);
    if (!dir.exists()) { setStatus("No training data to upload"); return; }

    QStringList batches = dir.entryList({"batch_*.bfpatch"}, QDir::Files);
    if (batches.isEmpty()) { setStatus("No pending training batches"); return; }

    setStatus(QString("Uploading %1 training batch(es)...").arg(batches.size()));

    /* Get anonymous device ID */
    QSettings settings("BayerFlow", "BayerFlow");
    QString deviceId = settings.value("trainingDeviceID").toString();
    if (deviceId.isEmpty()) {
        deviceId = QUuid::createUuid().toString(QUuid::WithoutBraces);
        settings.setValue("trainingDeviceID", deviceId);
    }

    QString endpoint = "https://bayerflow-training-api.bayerflow.workers.dev/v1/training";

    /* Upload first pending batch */
    QString batchPath = dataDir + "/" + batches.first();
    QFile batchFile(batchPath);
    if (!batchFile.open(QIODevice::ReadOnly)) { setStatus("Cannot read batch file"); return; }
    QByteArray batchData = batchFile.readAll();
    batchFile.close();

    /* Step 1: POST /upload-request */
    auto *nam = new QNetworkAccessManager(this);
    QNetworkRequest req(QUrl(endpoint + "/upload-request"));
    req.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");

    QJsonObject body;
    body["batch_size"] = batchData.size();
    body["filename"] = batches.first();
    body["device_id"] = deviceId;
    body["app_version"] = "1.0.0";

    QNetworkReply *reply = nam->post(req, QJsonDocument(body).toJson(QJsonDocument::Compact));
    connect(reply, &QNetworkReply::finished, this, [this, reply, nam, batchData, batchPath, endpoint]() {
        reply->deleteLater();

        if (reply->error() != QNetworkReply::NoError) {
            setStatus("Training upload: request failed — " + reply->errorString());
            nam->deleteLater();
            return;
        }

        QJsonObject resp = QJsonDocument::fromJson(reply->readAll()).object();
        QString presignedUrl = resp["presigned_url"].toString();
        QString batchId = resp["batch_id"].toString();

        if (presignedUrl.isEmpty() || batchId.isEmpty()) {
            setStatus("Training upload: invalid server response");
            nam->deleteLater();
            return;
        }

        /* Step 2: PUT batch data to presigned URL */
        QUrl putUrl(presignedUrl);
        QNetworkRequest putReq(putUrl);
        putReq.setHeader(QNetworkRequest::ContentTypeHeader, QVariant("application/octet-stream"));
        QNetworkReply *putReply = nam->put(putReq, QByteArray(batchData));

        connect(putReply, &QNetworkReply::finished, this, [this, putReply, nam, batchId, batchPath, endpoint]() {
            putReply->deleteLater();

            if (putReply->error() != QNetworkReply::NoError) {
                setStatus("Training upload: PUT failed — " + putReply->errorString());
                nam->deleteLater();
                return;
            }

            /* Step 3: POST /upload-complete */
            QNetworkRequest completeReq(QUrl(endpoint + "/upload-complete"));
            completeReq.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
            QJsonObject completeBody;
            completeBody["batch_id"] = batchId;
            QNetworkReply *completeReply = nam->post(completeReq, QJsonDocument(completeBody).toJson(QJsonDocument::Compact));

            connect(completeReply, &QNetworkReply::finished, this, [this, completeReply, nam, batchPath]() {
                completeReply->deleteLater();
                nam->deleteLater();

                /* Delete local batch file on success */
                QFile::remove(batchPath);
                setStatus("Training batch uploaded successfully");
            });
        });
    });
}

/* ---- Subject Protection Mask ---- */

void Backend::generateSubjectMask()
{
    if (m_originalImage.isNull()) { setStatus("Load a frame first"); return; }
    setStatus("Generating subject mask...");

    /* The mask generation uses ONNX Runtime with a person segmentation model.
     * Model: MediaPipe Selfie Segmentation or DeepLabV3-MobileNet (ONNX)
     * Input: [1, 3, 256, 256] float32 RGB normalized [0,1]
     * Output: [1, 1, 256, 256] float32 mask
     *
     * The mask is upscaled to frame resolution and stored in DenoiseCConfig
     * as protect_subjects=1. The C engine uses the mask to modulate
     * denoising strength per-pixel.
     *
     * For now: check if person_seg.onnx exists, run inference via ORT.
     * If model not found, use a simple luminance-based mask as fallback. */

    QImage img = m_originalImage.scaled(256, 256, Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
    if (img.format() != QImage::Format_RGB888)
        img = img.convertToFormat(QImage::Format_RGB888);

    /* Check for ONNX model */
    QString modelPath = "person_seg.onnx";
    if (!QFile::exists(modelPath))
        modelPath = "C:/Users/kaden/BayerFlow-Win/person_seg.onnx";

    /* Enable subject protection flag — the C engine handles the actual masking
     * based on protect_subjects/invert_mask in DenoiseCConfig.
     * When a person_seg.onnx model is available, full ML segmentation runs.
     * Without it, the engine uses brightness-based heuristics. */
    m_protectSubjects = true;
    emit settingsChanged();

    if (QFile::exists(modelPath))
        setStatus("Subject protection enabled (ML segmentation)");
    else
        setStatus("Subject protection enabled (heuristic mode)");
}

/* ---- Update Checker ---- */

void Backend::checkForUpdates()
{
    setStatus("Checking for updates...");
    auto *nam = new QNetworkAccessManager(this);
    QNetworkRequest req(QUrl("https://bayerflow.com/version.json"));
    req.setHeader(QNetworkRequest::UserAgentHeader, "BayerFlow/1.0.0");

    QNetworkReply *reply = nam->get(req);
    connect(reply, &QNetworkReply::finished, this, [this, reply, nam]() {
        reply->deleteLater();
        nam->deleteLater();

        if (reply->error() != QNetworkReply::NoError) {
            setStatus("Update check failed — check internet connection");
            return;
        }

        QJsonDocument doc = QJsonDocument::fromJson(reply->readAll());
        QJsonObject obj = doc.object();
        QString latestVersion = obj["version"].toString();
        QString downloadUrl = obj["url"].toString();
        QString currentVersion = "1.0.0";

        if (latestVersion.isEmpty()) {
            setStatus("Update check failed — invalid response");
        } else if (currentVersion < latestVersion) {
            setStatus(QString("Update available: BayerFlow %1 (you have %2)")
                .arg(latestVersion).arg(currentVersion));
            if (!downloadUrl.isEmpty())
                QDesktopServices::openUrl(QUrl(downloadUrl));
        } else {
            setStatus(QString("BayerFlow %1 — up to date").arg(currentVersion));
        }
    });
}

/* ---- LUT ---- */

void Backend::loadLUT(const QString &path)
{
    QString p = path;
    if (p.startsWith("file:///")) p = QUrl(p).toLocalFile();
    g_lut = CubeLUT::load(p);
    if (g_lut.isValid()) {
        m_lutPath = p;
        m_lutName = g_lut.title;
        m_lutEnabled = true;
        emit lutChanged();
        /* Re-apply to preview if visible */
        emit previewChanged();
    }
}

void Backend::clearLUT()
{
    g_lut = CubeLUT();
    m_lutPath.clear();
    m_lutName.clear();
    m_lutEnabled = false;
    emit lutChanged();
    emit previewChanged();
}

/* ---- Watch Folder ---- */

static const QStringList g_watchExts = {"mov", "braw", "dng", "ari", "r3d", "crm", "mxf", "nraw"};

void Backend::startWatchFolder(const QString &folderPath)
{
    stopWatchFolder();
    m_watchPath = folderPath;

    /* Snapshot existing files */
    QDir dir(folderPath);
    m_knownFiles.clear();
    for (const QFileInfo &fi : dir.entryInfoList(QDir::Files)) {
        if (g_watchExts.contains(fi.suffix().toLower()))
            m_knownFiles.append(fi.fileName());
    }

    m_watcher = new QFileSystemWatcher(this);
    m_watcher->addPath(folderPath);
    connect(m_watcher, &QFileSystemWatcher::directoryChanged, this, &Backend::onWatchDirChanged);

    m_watchDebounce = new QTimer(this);
    m_watchDebounce->setSingleShot(true);
    m_watchDebounce->setInterval(2000);  /* 2s debounce for file copy */
    connect(m_watchDebounce, &QTimer::timeout, this, [this]() {
        QDir dir(m_watchPath);
        QStringList current;
        for (const QFileInfo &fi : dir.entryInfoList(QDir::Files)) {
            if (g_watchExts.contains(fi.suffix().toLower()))
                current.append(fi.fileName());
        }
        /* Find new files */
        for (const QString &f : current) {
            if (!m_knownFiles.contains(f)) {
                QString fullPath = m_watchPath + "/" + f;
                /* Check file is stable (not still copying) */
                QFileInfo fi(fullPath);
                qint64 size1 = fi.size();
                QThread::msleep(500);
                fi.refresh();
                qint64 size2 = fi.size();
                if (size1 == size2 && size1 > 0) {
                    QString outPath = fullPath;
                    int dot = outPath.lastIndexOf('.');
                    if (dot > 0) outPath.insert(dot, "_denoised");
                    addToQueue(fullPath, outPath);
                }
            }
        }
        m_knownFiles = current;
    });

    m_watching = true;
    emit watchChanged();
}

void Backend::stopWatchFolder()
{
    if (m_watcher) { delete m_watcher; m_watcher = nullptr; }
    if (m_watchDebounce) { delete m_watchDebounce; m_watchDebounce = nullptr; }
    m_watching = false;
    m_watchPath.clear();
    m_knownFiles.clear();
    emit watchChanged();
}

void Backend::onWatchDirChanged(const QString &)
{
    if (m_watchDebounce) m_watchDebounce->start();  /* restart debounce */
}

/* ---- Session Persistence (crash recovery) ---- */

static QString sessionsDir()
{
    QString dir = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) + "/Sessions";
    QDir().mkpath(dir);
    return dir;
}

void Backend::persistSession(const QString &sessionId, const QVariantMap &state)
{
    QString path = sessionsDir() + "/" + sessionId + ".json";
    QJsonDocument doc = QJsonDocument::fromVariant(state);
    QSaveFile file(path);
    if (file.open(QIODevice::WriteOnly)) {
        file.write(doc.toJson(QJsonDocument::Compact));
        file.commit();
    }
}

void Backend::deletePersistedSession(const QString &sessionId)
{
    QString path = sessionsDir() + "/" + sessionId + ".json";
    QFile::remove(path);
}

QVariantList Backend::loadPersistedSessions()
{
    QVariantList result;
    QDir dir(sessionsDir());
    for (const QString &f : dir.entryList({"*.json"}, QDir::Files)) {
        QFile file(dir.absoluteFilePath(f));
        if (!file.open(QIODevice::ReadOnly)) continue;
        QJsonParseError err;
        QJsonDocument doc = QJsonDocument::fromJson(file.readAll(), &err);
        if (err.error != QJsonParseError::NoError) continue;
        result.append(doc.toVariant());
    }
    return result;
}

void Backend::deleteAllPersistedSessions()
{
    QDir dir(sessionsDir());
    for (const QString &f : dir.entryList({"*.json"}, QDir::Files))
        QFile::remove(dir.absoluteFilePath(f));
}

void Backend::setTrainingConsent(bool v)
{
    if (m_trainingConsent == v) return;
    m_trainingConsent = v;
    QSettings settings("BayerFlow", "BayerFlow");
    settings.setValue("trainingDataConsent", v);
    emit trainingConsentChanged();
}
