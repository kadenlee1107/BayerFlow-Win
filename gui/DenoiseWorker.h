#pragma once
#include <QObject>
#include <QString>
#include <atomic>

extern "C" {
#include "denoise_bridge.h"
}

class DenoiseWorker : public QObject {
    Q_OBJECT
public:
    explicit DenoiseWorker(QObject *parent = nullptr);

    void configure(const QString &input, const QString &output,
                   float strength, int window, int tfMode, float spatial,
                   float blackLevel, float shotGain, float readNoise);

public slots:
    void process();
    void requestCancel() { m_cancel.store(true); }

signals:
    void progressChanged(int current, int total);
    void finished(int result);

private:
    QString m_input, m_output;
    float m_strength = 1.5f;
    int m_window = 15;
    int m_tfMode = 2;
    float m_spatial = 0.0f;
    float m_blackLevel = 0.0f;
    float m_shotGain = 0.0f;
    float m_readNoise = 0.0f;
    std::atomic<bool> m_cancel{false};

    static int progressCB(int current, int total, void *ctx);
};
