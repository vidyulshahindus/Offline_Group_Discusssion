#pragma once

#include "PcmTo16kMono.h"

#include <QAudioDevice>
#include <QObject>

class QAudioSource;
class QIODevice;

class AudioCapture : public QObject
{
    Q_OBJECT

public:
    explicit AudioCapture(QObject *parent = nullptr);
    ~AudioCapture() override;

    QList<QAudioDevice> microphones() const;
    void setMicrophone(const QAudioDevice &device);
    bool isCapturing() const { return m_capturing; }

    void start();
    void stop();

signals:
    void pcm16kReady(const QByteArray &pcm);
    void errorOccurred(const QString &message);

private:
    void onAudioReady();

    QAudioDevice m_device;
    QAudioSource *m_source = nullptr;
    QIODevice *m_capture = nullptr;
    PcmTo16kMono m_converter;
    bool m_capturing = false;
};
