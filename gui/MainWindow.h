#pragma once
#include <QMainWindow>
#include <QLineEdit>
#include <QPushButton>
#include <QProgressBar>
#include <QLabel>
#include <QDoubleSpinBox>
#include <QSpinBox>
#include <QComboBox>
#include <QGroupBox>
#include <QThread>
#include "PreviewWidget.h"
#include "DenoiseWorker.h"

class MainWindow : public QMainWindow {
    Q_OBJECT
public:
    explicit MainWindow(QWidget *parent = nullptr);
    ~MainWindow();

private slots:
    void onBrowseInput();
    void onBrowseOutput();
    void onLoadPreview();
    void onProfileNoise(QRect imageRect);
    void onStartDenoise();
    void onCancelDenoise();
    void onProgress(int current, int total);
    void onDenoiseFinished(int result);

private:
    /* File selection */
    QLineEdit *m_inputPath, *m_outputPath;

    /* Preview */
    PreviewWidget *m_preview;

    /* Noise profile display */
    QLabel *m_noiseBlackLevel, *m_noiseShotGain;
    QLabel *m_noiseReadNoise, *m_noiseSigma;

    /* Settings */
    QDoubleSpinBox *m_strength, *m_spatialStrength;
    QSpinBox *m_windowSize;
    QComboBox *m_tfMode;

    /* Processing */
    QPushButton *m_startBtn, *m_cancelBtn;
    QProgressBar *m_progressBar;
    QLabel *m_statusLabel;

    /* State */
    QThread *m_workerThread = nullptr;
    DenoiseWorker *m_worker = nullptr;
    uint16_t *m_currentBayer = nullptr;
    int m_bayerWidth = 0, m_bayerHeight = 0;

    /* Profiled values */
    float m_profBlackLevel = 0, m_profShotGain = 0;
    float m_profReadNoise = 0;

    void buildUI();
    void setProcessingState(bool running);
};
