#pragma once
#include <QObject>
#include <QString>
#include <QImage>
#include <QThread>
#include <QVariantMap>
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

    /* First launch + training consent */
    Q_PROPERTY(bool isFirstLaunch READ isFirstLaunch CONSTANT)
    Q_PROPERTY(bool trainingConsent READ trainingConsent WRITE setTrainingConsent NOTIFY trainingConsentChanged)

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

    /* Worker thread */
    QThread *m_thread = nullptr;
    std::atomic<bool> m_cancel{false};

    static int progressCB(int current, int total, void *ctx);
    void setStatus(const QString &s);
    static QImage bayerToQImage(const uint16_t *bayer, int w, int h);
};
