#include "AudioCapture.h"

#include <QAudioFormat>
#include <QAudioSource>
#include <QMediaDevices>

AudioCapture::AudioCapture(QObject *parent)
    : QObject(parent)
    , m_device(QMediaDevices::defaultAudioInput())
{
}

AudioCapture::~AudioCapture()
{
    stop();
}

QList<QAudioDevice> AudioCapture::microphones() const
{
    return QMediaDevices::audioInputs();
}

void AudioCapture::setMicrophone(const QAudioDevice &device)
{
    if (m_capturing) {
        return;
    }
    if (!device.isNull()) {
        m_device = device;
    }
}

QString AudioCapture::sampleFormatName(QAudioFormat::SampleFormat fmt)
{
    switch (fmt) {
    case QAudioFormat::UInt8:
        return QStringLiteral("UInt8");
    case QAudioFormat::Int16:
        return QStringLiteral("Int16");
    case QAudioFormat::Int32:
        return QStringLiteral("Int32");
    case QAudioFormat::Float:
        return QStringLiteral("Float");
    default:
        return QStringLiteral("Unknown");
    }
}

QAudio::State AudioCapture::sourceState() const
{
    return m_source ? m_source->state() : QAudio::StoppedState;
}

QAudio::Error AudioCapture::sourceError() const
{
    return m_source ? m_source->error() : QAudio::NoError;
}

void AudioCapture::start()
{
    if (m_capturing) {
        return;
    }
    if (m_device.isNull()) {
        emit errorOccurred(QStringLiteral("No microphone selected."));
        return;
    }

    QAudioFormat preferred;
    preferred.setSampleRate(16000);
    preferred.setChannelCount(1);
    preferred.setSampleFormat(QAudioFormat::Int16);
    m_inputFormat =
        m_device.isFormatSupported(preferred) ? preferred : m_device.preferredFormat();
    m_converter.setInputFormat(m_inputFormat);

    m_source = new QAudioSource(m_device, m_inputFormat, this);
    m_capture = m_source->start();
    if (!m_capture) {
        const QString err = m_source->error() == QAudio::NoError
                                ? QStringLiteral("start() returned null IO device")
                                : QString::number(int(m_source->error()));
        m_source->deleteLater();
        m_source = nullptr;
        emit errorOccurred(QStringLiteral("Could not open the selected microphone: %1").arg(err));
        return;
    }
    connect(m_capture, &QIODevice::readyRead, this, &AudioCapture::onAudioReady);
    m_capturing = true;
}

void AudioCapture::stop()
{
    if (!m_capturing) {
        return;
    }
    if (m_capture) {
        onAudioReady();
    }
    if (m_source) {
        m_source->stop();
        m_source->deleteLater();
        m_source = nullptr;
    }
    m_capture = nullptr;
    m_capturing = false;
}

void AudioCapture::onAudioReady()
{
    if (!m_capture) {
        return;
    }
    const QByteArray raw = m_capture->readAll();
    const QByteArray pcm = m_converter.convert(raw);
    if (!pcm.isEmpty()) {
        emit pcm16kReady(pcm);
    }
}
