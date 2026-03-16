#pragma once
#include <QObject>
#include <QString>
#include <QImage>
#include <QThread>
#include <QVariantMap>
#include <QFileSystemWatcher>
#include <QTimer>
#include <QStringList>
#include <atomic>

extern "C" {
#include "denoise_bridge.h"
#include "platform_gpu.h"
}

class Backend : public QObject {
    Q_OBJECT

    /* Properties exposed to QML */
    Q_PROPERTY(QString inputPath READ inputPath WRITE setInputPath NOTIFY inputPathChanged)
    Q_PROPERTY(QString outputPath READ outputPath WRITE setOutputPath NOTIFY outputPathChanged)
    Q_PROPERTY(bool processing READ processing NOTIFY processingChanged)
    Q_PROPERTY(int progressPercent READ progressPercent NOTIFY progressChanged)
    Q_PROPERTY(QString statusText READ statusText NOTIFY statusChanged)
    Q_PROPERTY(QImage previewImage READ previewImage NOTIFY previewChanged)

    /* Noise profile */
    Q_PROPERTY(float noiseBlackLevel READ noiseBlackLevel NOTIFY noiseProfileChanged)
    Q_PROPERTY(float noiseShotGain READ noiseShotGain NOTIFY noiseProfileChanged)
    Q_PROPERTY(float noiseReadNoise READ noiseReadNoise NOTIFY noiseProfileChanged)
    Q_PROPERTY(float noiseSigma READ noiseSigma NOTIFY noiseProfileChanged)
    Q_PROPERTY(bool noiseProfileValid READ noiseProfileValid NOTIFY noiseProfileChanged)

    /* Settings */
    Q_PROPERTY(float strength MEMBER m_strength NOTIFY settingsChanged)
    Q_PROPERTY(int windowSize MEMBER m_windowSize NOTIFY settingsChanged)
    Q_PROPERTY(float spatialStrength MEMBER m_spatialStrength NOTIFY settingsChanged)
    Q_PROPERTY(int tfMode MEMBER m_tfMode NOTIFY settingsChanged)
    Q_PROPERTY(bool useCNN MEMBER m_useCNN NOTIFY settingsChanged)
    Q_PROPERTY(QString preset READ preset WRITE setPreset NOTIFY presetChanged)
    Q_PROPERTY(int startFrame MEMBER m_startFrame NOTIFY settingsChanged)
    Q_PROPERTY(int endFrame MEMBER m_endFrame NOTIFY settingsChanged)
    Q_PROPERTY(int outputFormat MEMBER m_outputFormat NOTIFY settingsChanged)
    Q_PROPERTY(QString etaText READ etaText NOTIFY progressChanged)
    Q_PROPERTY(double fpsValue READ fpsValue NOTIFY progressChanged)

    /* Motion analysis */
    Q_PROPERTY(float motionAvg READ motionAvg NOTIFY motionAnalyzed)
    Q_PROPERTY(bool isAnalyzing READ isAnalyzing NOTIFY motionAnalyzed)
    Q_PROPERTY(QString motionHint READ motionHint NOTIFY motionAnalyzed)

    /* LUT preview */
    Q_PROPERTY(bool lutEnabled MEMBER m_lutEnabled NOTIFY lutChanged)
    Q_PROPERTY(float lutBlend MEMBER m_lutBlend NOTIFY lutChanged)
    Q_PROPERTY(QString lutName READ lutName NOTIFY lutChanged)

    /* First launch + training consent */
    Q_PROPERTY(bool isFirstLaunch READ isFirstLaunch CONSTANT)
    Q_PROPERTY(bool trainingConsent READ trainingConsent WRITE setTrainingConsent NOTIFY trainingConsentChanged)

    /* Watch folder */
    Q_PROPERTY(bool isWatching READ isWatching NOTIFY watchChanged)
    Q_PROPERTY(QString watchFolderPath READ watchFolderPath NOTIFY watchChanged)

    /* Batch queue */
    Q_PROPERTY(QVariantList queueModel READ queueModel NOTIFY queueChanged)
    Q_PROPERTY(bool isQueueRunning READ isQueueRunning NOTIFY queueChanged)
    Q_PROPERTY(int queueCount READ queueCount NOTIFY queueChanged)

    /* Denoised preview */
    Q_PROPERTY(bool isPreviewLoading READ isPreviewLoading NOTIFY previewLoadingChanged)
    Q_PROPERTY(int previewFrameIndex MEMBER m_previewFrameIndex NOTIFY previewFrameChanged)
    Q_PROPERTY(int frameCount READ frameCount NOTIFY inputPathChanged)
    Q_PROPERTY(bool showDenoised MEMBER m_showDenoised NOTIFY previewModeChanged)
    Q_PROPERTY(bool hasOriginal READ hasOriginal NOTIFY originalPreviewReady)
    Q_PROPERTY(bool hasDenoised READ hasDenoised NOTIFY denoisedPreviewReady)

public:
    explicit Backend(QObject *parent = nullptr);
    ~Backend();

    QString inputPath() const { return m_inputPath; }
    QString outputPath() const { return m_outputPath; }
    bool processing() const { return m_processing; }
    int progressPercent() const { return m_progressPercent; }
    QString statusText() const { return m_statusText; }
    QImage previewImage() const;  /* returns original or denoised based on m_showDenoised */

    float noiseBlackLevel() const { return m_noiseBlackLevel; }
    float noiseShotGain() const { return m_noiseShotGain; }
    float noiseReadNoise() const { return m_noiseReadNoise; }
    float noiseSigma() const { return m_noiseSigma; }
    bool noiseProfileValid() const { return m_noiseValid; }

    QString etaText() const { return m_etaText; }
    double fpsValue() const { return m_fps; }

    bool isFirstLaunch() const { return m_isFirstLaunch; }
    bool trainingConsent() const { return m_trainingConsent; }
    void setTrainingConsent(bool v);

    bool isPreviewLoading() const { return m_previewLoading; }
    int frameCount() const { return m_frameCount; }
    bool hasOriginal() const { return !m_originalImage.isNull(); }
    bool hasDenoised() const { return !m_denoisedImage.isNull(); }

    QString preset() const { return m_preset; }
    void setPreset(const QString &p);

    void setInputPath(const QString &p);
    void setOutputPath(const QString &p);

public slots:
    void loadPreview();
    void profileNoise(int x, int y, int w, int h);
    void startDenoise();
    void cancelDenoise();
    void generateDenoisedPreview();
    Q_INVOKABLE void markOnboardingDone();

    /* Tab/session management */
    Q_INVOKABLE QVariantMap saveSessionState();
    Q_INVOKABLE void restoreSessionState(const QVariantMap &state);

    /* Session persistence (crash recovery) */
    Q_INVOKABLE void persistSession(const QString &sessionId, const QVariantMap &state);
    Q_INVOKABLE void deletePersistedSession(const QString &sessionId);
    Q_INVOKABLE QVariantList loadPersistedSessions();
    Q_INVOKABLE void deleteAllPersistedSessions();

    /* Batch queue */
    Q_INVOKABLE void addToQueue(const QString &inputPath, const QString &outputPath);
    Q_INVOKABLE void removeFromQueue(int index);
    Q_INVOKABLE void clearQueue();
    Q_INVOKABLE void startQueue();
    Q_INVOKABLE void cancelQueue();

    /* Watch folder */
    Q_INVOKABLE void startWatchFolder(const QString &folderPath);
    Q_INVOKABLE void stopWatchFolder();

    /* LUT */
    Q_INVOKABLE void loadLUT(const QString &path);
    Q_INVOKABLE void clearLUT();

    /* Motion analysis */
    Q_INVOKABLE void analyzeMotion();
    float motionAvg() const { return m_motionAvg; }
    bool isAnalyzing() const { return m_analyzing; }
    QString motionHint() const;
    QString lutName() const { return m_lutName; }
    bool isWatching() const { return m_watching; }
    QString watchFolderPath() const { return m_watchPath; }

signals:
    void inputPathChanged();
    void outputPathChanged();
    void processingChanged();
    void progressChanged();
    void statusChanged();
    void previewChanged();
    void noiseProfileChanged();
    void settingsChanged();
    void trainingConsentChanged();
    void presetChanged();
    void queueChanged();
    void watchChanged();
    void lutChanged();
    void motionAnalyzed();
    void previewLoadingChanged();
    void previewFrameChanged();
    void previewModeChanged();
    void originalPreviewReady();
    void denoisedPreviewReady();

private:
    QString m_inputPath, m_outputPath;
    bool m_processing = false;
    int m_progressPercent = 0;
    QString m_statusText = "Ready";
    QImage m_previewImage;

    /* Noise profile */
    float m_noiseBlackLevel = 0, m_noiseShotGain = 0;
    float m_noiseReadNoise = 0, m_noiseSigma = 0;
    bool m_noiseValid = false;

    /* Settings */
    float m_strength = 1.5f;
    int m_windowSize = 15;
    float m_spatialStrength = 0.0f;
    int m_tfMode = 2;
    bool m_useCNN = true;
    QString m_preset = "Strong";
    int m_startFrame = 0;
    int m_endFrame = 0;  /* 0 = all */
    int m_outputFormat = 0;  /* 0=auto, 1=MOV, 2=DNG, 3=BRAW, 4=EXR */

    /* ETA/fps tracking */
    QString m_etaText;
    double m_fps = 0;
    qint64 m_processStartTime = 0;

    /* First launch + consent */
    bool m_isFirstLaunch = true;
    bool m_trainingConsent = false;

    /* Bayer frame for noise profiling */
    uint16_t *m_bayer = nullptr;
    int m_bayerW = 0, m_bayerH = 0;

    /* Preview */
    QImage m_originalImage;
    QImage m_denoisedImage;
    bool m_previewLoading = false;
    int m_previewFrameIndex = 0;
    int m_frameCount = 0;
    bool m_showDenoised = false;

    /* Batch queue */
    struct QueueItem {
        QString inputPath, outputPath, filename;
        QString status;  /* "pending", "processing", "done", "failed" */
        int progressPercent = 0;
        QString message;
    };
    QList<QueueItem> m_queue;
    bool m_queueRunning = false;
    int m_queueIndex = -1;

    QVariantList queueModel() const;
    bool isQueueRunning() const { return m_queueRunning; }
    int queueCount() const { return m_queue.size(); }
    void processNextQueueItem();

    /* Motion analysis */
    float m_motionAvg = 0;
    bool m_analyzing = false;

    /* LUT */
    bool m_lutEnabled = false;
    float m_lutBlend = 1.0f;
    QString m_lutName;
    QString m_lutPath;

    /* Watch folder */
    QFileSystemWatcher *m_watcher = nullptr;
    QTimer *m_watchDebounce = nullptr;
    QString m_watchPath;
    QStringList m_knownFiles;
    bool m_watching = false;
    void onWatchDirChanged(const QString &path);

    /* Worker thread */
    QThread *m_thread = nullptr;
    std::atomic<bool> m_cancel{false};

    static int progressCB(int current, int total, void *ctx);
    static int queueProgressCB(int current, int total, void *ctx);
    void setStatus(const QString &s);
    static QImage bayerToQImage(const uint16_t *bayer, int w, int h);
};
