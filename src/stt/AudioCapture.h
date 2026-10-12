#pragma once

#include "PcmTo16kMono.h"

#include <QAudio>
#include <QAudioDevice>
#include <QAudioFormat>
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
    QAudioDevice device() const { return m_device; }
    QAudioFormat inputFormat() const { return m_inputFormat; }
    QAudio::State sourceState() const;
    QAudio::Error sourceError() const;

    void start();
    void stop();

signals:
    void pcm16kReady(const QByteArray &pcm);
    void errorOccurred(const QString &message);

private:
    void onAudioReady();
    static QString sampleFormatName(QAudioFormat::SampleFormat fmt);

    QAudioDevice m_device;
    QAudioFormat m_inputFormat;
    QAudioSource *m_source = nullptr;
    QIODevice *m_capture = nullptr;
    PcmTo16kMono m_converter;
    bool m_capturing = false;
};
