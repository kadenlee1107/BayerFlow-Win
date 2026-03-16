#include "Backend.h"
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
        cfg.use_cnn_postfilter   = m_useCNN ? 1 : 0;
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
        cfg.auto_dark_frame      = 1;
        cfg.output_format        = m_outputFormat;
        cfg.start_frame          = m_startFrame;
        cfg.end_frame            = m_endFrame;
        cfg.collect_training_data = m_trainingConsent ? 1 : 0;
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
        cfg.auto_dark_frame      = 1;
        cfg.output_format        = m_outputFormat;
        cfg.start_frame          = m_startFrame;
        cfg.end_frame            = m_endFrame;
        cfg.collect_training_data = m_trainingConsent ? 1 : 0;
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
