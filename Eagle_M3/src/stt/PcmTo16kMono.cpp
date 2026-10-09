#include "PcmTo16kMono.h"

#include <QtGlobal>

#include <cmath>
#include <cstring>

void PcmTo16kMono::reset(int sampleRate, int channels, SampleType type)
{
    m_rate = qMax(1, sampleRate);
    m_channels = qMax(1, channels);
    m_type = type;
    switch (type) {
    case SampleType::UInt8:
        m_bytesPerSample = 1;
        break;
    case SampleType::Int16:
        m_bytesPerSample = 2;
        break;
    case SampleType::Int32:
    case SampleType::Float:
        m_bytesPerSample = 4;
        break;
    }
    m_ratio = double(m_rate) / double(kTargetRate);
    m_pos = 0.0;
    m_partial.clear();
    m_mono.clear();
}

float PcmTo16kMono::sampleAt(const char *p) const
{
    switch (m_type) {
    case SampleType::UInt8:
        return (float(static_cast<quint8>(*p)) - 128.0f) / 128.0f;
    case SampleType::Int16: {
        qint16 v = 0;
        std::memcpy(&v, p, sizeof(v));
        return float(v) / 32768.0f;
    }
    case SampleType::Int32: {
        qint32 v = 0;
        std::memcpy(&v, p, sizeof(v));
        return float(double(v) / 2147483648.0);
    }
    case SampleType::Float: {
        float v = 0.0f;
        std::memcpy(&v, p, sizeof(v));
        return std::isfinite(v) ? v : 0.0f;
    }
    }
    return 0.0f;
}

QByteArray PcmTo16kMono::convert(const QByteArray &raw)
{
    if (raw.isEmpty()) {
        return {};
    }
    QByteArray data = m_partial;
    data.append(raw);
    const int frameBytes = bytesPerFrame();
    const qsizetype frames = data.size() / frameBytes;
    const char *base = data.constData();
    m_mono.reserve(m_mono.size() + frames);
    for (qsizetype f = 0; f < frames; ++f) {
        const char *frame = base + f * frameBytes;
        float sum = 0.0f;
        for (int c = 0; c < m_channels; ++c) {
            sum += sampleAt(frame + c * m_bytesPerSample);
        }
        m_mono.append(sum / float(m_channels));
    }
    m_partial = data.mid(frames * frameBytes);

    QByteArray out;
    const qsizetype available = m_mono.size();
    out.reserve(qsizetype(double(available) / m_ratio + 4.0) * 2);
    auto push = [&out](float v) {
        const qint16 s = qint16(qRound(qBound(-1.0f, v, 1.0f) * 32767.0f));
        char b[2];
        std::memcpy(b, &s, 2);
        out.append(b, 2);
    };

    if (m_ratio >= 1.0) {
        // Box-filter decimation: the average over each output period is a cheap
        // anti-alias low-pass (48 kHz -> 16 kHz averages 3 samples).
        while (true) {
            const qsizetype s = qsizetype(std::floor(m_pos));
            qsizetype e = qsizetype(std::floor(m_pos + m_ratio));
            if (e <= s) {
                e = s + 1;
            }
            if (e > available) {
                break;
            }
            float acc = 0.0f;
            for (qsizetype i = s; i < e; ++i) {
                acc += m_mono.at(i);
            }
            push(acc / float(e - s));
            m_pos += m_ratio;
        }
    } else {
        // Upsampling (e.g. an 8 kHz headset): linear interpolation.
        while (true) {
            const qsizetype i = qsizetype(std::floor(m_pos));
            if (i + 1 >= available) {
                break;
            }
            const float frac = float(m_pos - double(i));
            push(m_mono.at(i) * (1.0f - frac) + m_mono.at(i + 1) * frac);
            m_pos += m_ratio;
        }
    }

    const qsizetype drop = qMin(qsizetype(std::floor(m_pos)), available);
    if (drop > 0) {
        m_mono.remove(0, drop);
        m_pos -= double(drop);
    }
    return out;
}
