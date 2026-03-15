#include "MainWindow.h"
#include "BayerConverter.h"
#include <QFileDialog>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QFormLayout>
#include <QMessageBox>
#include <cstdlib>

/* noise_profile functions — CNoiseProfile already defined via denoise_bridge.h */
extern "C" {
void noise_profile_from_patch(const uint16_t *bayer, int bayer_w, int bayer_h,
                               int px, int py, int pw, int ph, CNoiseProfile *out);
uint16_t *noise_profile_read_frame(const char *path, int frame_index,
                                   int *out_width, int *out_height);
}

MainWindow::MainWindow(QWidget *parent) : QMainWindow(parent)
{
    setWindowTitle("BayerFlow");
    resize(900, 700);
    buildUI();
}

MainWindow::~MainWindow()
{
    free(m_currentBayer);
}

void MainWindow::buildUI()
{
    auto *central = new QWidget(this);
    auto *mainLayout = new QVBoxLayout(central);

    /* ---- File selection ---- */
    auto *fileGroup = new QGroupBox("Files", central);
    auto *fileLayout = new QFormLayout(fileGroup);
    m_inputPath = new QLineEdit(fileGroup);
    auto *browseIn = new QPushButton("Browse...", fileGroup);
    auto *inRow = new QHBoxLayout;
    inRow->addWidget(m_inputPath);
    inRow->addWidget(browseIn);
    fileLayout->addRow("Input:", inRow);

    m_outputPath = new QLineEdit(fileGroup);
    auto *browseOut = new QPushButton("Browse...", fileGroup);
    auto *outRow = new QHBoxLayout;
    outRow->addWidget(m_outputPath);
    outRow->addWidget(browseOut);
    fileLayout->addRow("Output:", outRow);
    mainLayout->addWidget(fileGroup);

    connect(browseIn, &QPushButton::clicked, this, &MainWindow::onBrowseInput);
    connect(browseOut, &QPushButton::clicked, this, &MainWindow::onBrowseOutput);

    /* ---- Preview ---- */
    auto *previewGroup = new QGroupBox("Preview (drag to select noise patch)", central);
    auto *previewLayout = new QVBoxLayout(previewGroup);
    m_preview = new PreviewWidget(previewGroup);
    m_preview->setMinimumHeight(300);
    previewLayout->addWidget(m_preview);
    auto *loadBtn = new QPushButton("Load Frame", previewGroup);
    previewLayout->addWidget(loadBtn);
    mainLayout->addWidget(previewGroup, 1);

    connect(loadBtn, &QPushButton::clicked, this, &MainWindow::onLoadPreview);
    connect(m_preview, &PreviewWidget::rectSelected, this, &MainWindow::onProfileNoise);

    /* ---- Noise profile ---- */
    auto *noiseGroup = new QGroupBox("Noise Profile", central);
    auto *noiseLayout = new QFormLayout(noiseGroup);
    m_noiseBlackLevel = new QLabel("--", noiseGroup);
    m_noiseBlackLevel->setObjectName("noiseValue");
    m_noiseShotGain = new QLabel("--", noiseGroup);
    m_noiseShotGain->setObjectName("noiseValue");
    m_noiseReadNoise = new QLabel("--", noiseGroup);
    m_noiseReadNoise->setObjectName("noiseValue");
    m_noiseSigma = new QLabel("--", noiseGroup);
    m_noiseSigma->setObjectName("noiseValue");
    noiseLayout->addRow("Black Level:", m_noiseBlackLevel);
    noiseLayout->addRow("Shot Gain:", m_noiseShotGain);
    noiseLayout->addRow("Read Noise:", m_noiseReadNoise);
    noiseLayout->addRow("Sigma:", m_noiseSigma);
    mainLayout->addWidget(noiseGroup);

    /* ---- Settings ---- */
    auto *settingsGroup = new QGroupBox("Settings", central);
    auto *settingsLayout = new QFormLayout(settingsGroup);

    m_strength = new QDoubleSpinBox(settingsGroup);
    m_strength->setRange(0.1, 5.0);
    m_strength->setValue(1.5);
    m_strength->setSingleStep(0.1);
    settingsLayout->addRow("Strength:", m_strength);

    m_windowSize = new QSpinBox(settingsGroup);
    m_windowSize->setRange(3, 31);
    m_windowSize->setValue(15);
    settingsLayout->addRow("Window:", m_windowSize);

    m_spatialStrength = new QDoubleSpinBox(settingsGroup);
    m_spatialStrength->setRange(0.0, 5.0);
    m_spatialStrength->setValue(0.0);
    m_spatialStrength->setSingleStep(0.1);
    settingsLayout->addRow("Spatial:", m_spatialStrength);

    m_tfMode = new QComboBox(settingsGroup);
    m_tfMode->addItem("NLM", 0);
    m_tfMode->addItem("VST+Bilateral", 2);
    m_tfMode->setCurrentIndex(1);
    settingsLayout->addRow("TF Mode:", m_tfMode);
    mainLayout->addWidget(settingsGroup);

    /* ---- Start / Cancel / Progress ---- */
    auto *actionLayout = new QHBoxLayout;
    m_startBtn = new QPushButton("Start Denoise", central);
    m_startBtn->setObjectName("startBtn");
    m_cancelBtn = new QPushButton("Cancel", central);
    m_cancelBtn->setObjectName("cancelBtn");
    m_cancelBtn->setEnabled(false);
    m_progressBar = new QProgressBar(central);
    m_progressBar->setRange(0, 100);
    actionLayout->addWidget(m_startBtn);
    actionLayout->addWidget(m_cancelBtn);
    actionLayout->addWidget(m_progressBar, 1);
    mainLayout->addLayout(actionLayout);

    m_statusLabel = new QLabel("Ready", central);
    m_statusLabel->setObjectName("statusLabel");
    mainLayout->addWidget(m_statusLabel);

    connect(m_startBtn, &QPushButton::clicked, this, &MainWindow::onStartDenoise);
    connect(m_cancelBtn, &QPushButton::clicked, this, &MainWindow::onCancelDenoise);

    setCentralWidget(central);
}

void MainWindow::onBrowseInput()
{
    QString path = QFileDialog::getOpenFileName(this, "Select Input",
        QString(), "Video Files (*.mov *.MOV *.braw *.dng *.ari *.crm)");
    if (!path.isEmpty()) {
        m_inputPath->setText(path);
        QString out = path;
        int dot = out.lastIndexOf('.');
        if (dot > 0) out.insert(dot, "_denoised");
        m_outputPath->setText(out);
    }
}

void MainWindow::onBrowseOutput()
{
    QString path = QFileDialog::getSaveFileName(this, "Select Output",
        m_outputPath->text(), "MOV (*.mov);;DNG (*.dng);;EXR (*.exr)");
    if (!path.isEmpty())
        m_outputPath->setText(path);
}

void MainWindow::onLoadPreview()
{
    QString path = m_inputPath->text();
    if (path.isEmpty()) {
        m_statusLabel->setText("Select an input file first");
        return;
    }

    m_statusLabel->setText("Loading preview frame...");

    free(m_currentBayer);
    m_currentBayer = noise_profile_read_frame(
        path.toLocal8Bit().data(), 0, &m_bayerWidth, &m_bayerHeight);

    if (!m_currentBayer) {
        m_statusLabel->setText("Failed to decode frame");
        return;
    }

    QImage img = BayerConverter::toQImage(m_currentBayer, m_bayerWidth, m_bayerHeight);
    m_preview->setPreviewImage(img);
    m_statusLabel->setText(QString("Loaded %1x%2 -- drag to select noise patch")
        .arg(m_bayerWidth).arg(m_bayerHeight));
}

void MainWindow::onProfileNoise(QRect imageRect)
{
    if (!m_currentBayer) return;

    CNoiseProfile profile;
    noise_profile_from_patch(m_currentBayer, m_bayerWidth, m_bayerHeight,
        imageRect.x(), imageRect.y(), imageRect.width(), imageRect.height(),
        &profile);

    if (!profile.valid) {
        m_statusLabel->setText("Patch too small for noise profiling");
        return;
    }

    m_profBlackLevel = profile.black_level;
    m_profShotGain = profile.shot_gain;
    m_profReadNoise = profile.read_noise;

    m_noiseBlackLevel->setText(QString::number(profile.black_level, 'f', 1));
    m_noiseShotGain->setText(QString::number(profile.shot_gain, 'f', 3));
    m_noiseReadNoise->setText(QString::number(profile.read_noise, 'f', 1));
    m_noiseSigma->setText(QString::number(profile.sigma, 'f', 1));

    m_statusLabel->setText(QString("Noise profiled: BL=%1 SG=%2 RN=%3 sigma=%4")
        .arg(profile.black_level, 0, 'f', 1)
        .arg(profile.shot_gain, 0, 'f', 3)
        .arg(profile.read_noise, 0, 'f', 1)
        .arg(profile.sigma, 0, 'f', 1));
}

void MainWindow::onStartDenoise()
{
    if (m_inputPath->text().isEmpty() || m_outputPath->text().isEmpty()) {
        QMessageBox::warning(this, "BayerFlow", "Select input and output files");
        return;
    }

    setProcessingState(true);

    m_worker = new DenoiseWorker;
    m_worker->configure(
        m_inputPath->text(), m_outputPath->text(),
        m_strength->value(), m_windowSize->value(),
        m_tfMode->currentData().toInt(), m_spatialStrength->value(),
        m_profBlackLevel, m_profShotGain, m_profReadNoise);

    m_workerThread = new QThread;
    m_worker->moveToThread(m_workerThread);

    connect(m_workerThread, &QThread::started, m_worker, &DenoiseWorker::process);
    connect(m_worker, &DenoiseWorker::progressChanged, this, &MainWindow::onProgress);
    connect(m_worker, &DenoiseWorker::finished, this, &MainWindow::onDenoiseFinished);
    connect(m_worker, &DenoiseWorker::finished, m_workerThread, &QThread::quit);
    connect(m_workerThread, &QThread::finished, m_worker, &QObject::deleteLater);
    connect(m_workerThread, &QThread::finished, m_workerThread, &QObject::deleteLater);

    m_workerThread->start();
}

void MainWindow::onCancelDenoise()
{
    if (m_worker) m_worker->requestCancel();
    m_statusLabel->setText("Cancelling...");
}

void MainWindow::onProgress(int current, int total)
{
    if (total > 0)
        m_progressBar->setValue(current * 100 / total);
    m_statusLabel->setText(QString("Processing frame %1 / %2...").arg(current).arg(total));
}

void MainWindow::onDenoiseFinished(int result)
{
    setProcessingState(false);
    m_worker = nullptr;
    m_workerThread = nullptr;

    if (result == DENOISE_OK)
        m_statusLabel->setText("Done! Output: " + m_outputPath->text());
    else if (result == DENOISE_ERR_CANCELLED)
        m_statusLabel->setText("Cancelled");
    else
        m_statusLabel->setText(QString("Failed with error code %1").arg(result));
}

void MainWindow::setProcessingState(bool running)
{
    m_startBtn->setEnabled(!running);
    m_cancelBtn->setEnabled(running);
    m_inputPath->setEnabled(!running);
    m_outputPath->setEnabled(!running);
    if (!running) m_progressBar->setValue(0);
}
