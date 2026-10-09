#pragma once

#include <QByteArray>
#include <QVector>

// Streaming converter: any microphone format -> 16 kHz mono PCM16 (what whisper wants).
// Keeps partial frames and the resampler position between calls, so chunk
// boundaries never cause clicks or drift. Pure logic (no QtMultimedia), unit-tested.
class PcmTo16kMono
{
public:
    enum class SampleType { UInt8, Int16, Int32, Float };

    static constexpr int kTargetRate = 16000;

    void reset(int sampleRate, int channels, SampleType type);
    QByteArray convert(const QByteArray &raw);

    int sourceRate() const { return m_rate; }
    int channels() const { return m_channels; }
    int bytesPerFrame() const { return m_bytesPerSample * m_channels; }

private:
    float sampleAt(const char *p) const;

    int m_rate = kTargetRate;
    int m_channels = 1;
    int m_bytesPerSample = 2;
    SampleType m_type = SampleType::Int16;
    double m_ratio = 1.0;   // source samples per output sample
    double m_pos = 0.0;     // read position in m_mono (fractional)
    QByteArray m_partial;   // incomplete source frame bytes
    QVector<float> m_mono;  // pending mono samples at the source rate
};
