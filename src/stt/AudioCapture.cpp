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
    const QAudioFormat format =
        m_device.isFormatSupported(preferred) ? preferred : m_device.preferredFormat();
    m_converter.setInputFormat(format);

    m_source = new QAudioSource(m_device, format, this);
    m_capture = m_source->start();
    if (!m_capture) {
        m_source->deleteLater();
        m_source = nullptr;
        emit errorOccurred(QStringLiteral("Could not open the selected microphone."));
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
