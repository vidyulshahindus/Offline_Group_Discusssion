#include "PcmTo16kMono.h"

#include <QVector>
#include <QtMath>

#include <cstring>

namespace {

float sampleAt(const char *bytes, QAudioFormat::SampleFormat format)
{
    switch (format) {
    case QAudioFormat::UInt8: {
        const auto value = static_cast<quint8>(*bytes);
        return (value - 128) / 128.0f;
    }
    case QAudioFormat::Int16: {
        qint16 value = 0;
        std::memcpy(&value, bytes, sizeof(value));
        return value / 32768.0f;
    }
    case QAudioFormat::Int32: {
        qint32 value = 0;
        std::memcpy(&value, bytes, sizeof(value));
        return value / 2147483648.0f;
    }
    case QAudioFormat::Float: {
        float value = 0.0f;
        std::memcpy(&value, bytes, sizeof(value));
        return value;
    }
    default:
        return 0.0f;
    }
}

} // namespace

void PcmTo16kMono::setInputFormat(const QAudioFormat &format)
{
    m_format = format;
}

QByteArray PcmTo16kMono::convert(const QByteArray &raw) const
{
    const int channels = qMax(1, m_format.channelCount());
    const int bytesPerSample = qMax(1, m_format.bytesPerSample());
    const int frameBytes = bytesPerSample * channels;
    if (frameBytes <= 0 || raw.size() < frameBytes) {
        return {};
    }

    const int frameCount = raw.size() / frameBytes;
    QVector<float> mono;
    mono.reserve(frameCount);
    const char *data = raw.constData();
    for (int frame = 0; frame < frameCount; ++frame) {
        float sum = 0.0f;
        for (int channel = 0; channel < channels; ++channel) {
            sum += sampleAt(data + frame * frameBytes + channel * bytesPerSample,
                            m_format.sampleFormat());
        }
        mono.append(sum / channels);
    }

    const int srcRate = qMax(1, m_format.sampleRate());
    const int dstCount = qMax(1, qRound(mono.size() * (16000.0 / srcRate)));
    QByteArray pcm(dstCount * 2, '\0');
    auto *dst = reinterpret_cast<qint16 *>(pcm.data());
    for (int i = 0; i < dstCount; ++i) {
        const double position = i * (static_cast<double>(srcRate) / 16000.0);
        const int index = qMin(static_cast<int>(position), mono.size() - 1);
        const int next = qMin(index + 1, mono.size() - 1);
        const float fraction = static_cast<float>(position - index);
        float sample = mono[index] * (1.0f - fraction) + mono[next] * fraction;
        sample = qBound(-1.0f, sample, 1.0f);
        dst[i] = static_cast<qint16>(sample * 32767.0f);
    }
    return pcm;
}
