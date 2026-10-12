#include "Vad.h"

#include <QtMath>

#include <cmath>

namespace {

constexpr int kSampleRate = 16000;
constexpr double kMinFloorDb = -70.0;
constexpr double kMaxFloorDb = -25.0;
constexpr double kFloorAlpha = 0.05;

} // namespace

Vad::Vad()
    : Vad(Config())
{
}

Vad::Vad(const Config &config)
    : m_config(config)
{
    m_frameSamples = qMax(1, m_config.frameMs * kSampleRate / 1000);
    m_maxSegmentSamples = qMax(m_frameSamples, m_config.maxSegmentMs * kSampleRate / 1000);
    const int preRollSamples = qMax(0, m_config.preRollMs * kSampleRate / 1000);
    m_preRoll.resize(preRollSamples * 2);
}

double Vad::frameDb(const qint16 *samples, int count) const
{
    if (count <= 0) {
        return kMinFloorDb;
    }
    double acc = 0;
    for (int i = 0; i < count; ++i) {
        const double v = double(samples[i]) / 32768.0;
        acc += v * v;
    }
    const double rms = std::sqrt(acc / count);
    const double db = 20.0 * std::log10(qMax(rms, 1e-9));
    return db;
}

QVector<Vad::Segment> Vad::pushPcm16k(const QByteArray &pcm16k, qint64 streamMsBase)
{
    QVector<Segment> out;
    if (pcm16k.isEmpty()) {
        return out;
    }
    const auto *samples = reinterpret_cast<const qint16 *>(pcm16k.constData());
    const int totalSamples = pcm16k.size() / 2;
    int offset = 0;
    while (offset + m_frameSamples <= totalSamples) {
        const double db = frameDb(samples + offset, m_frameSamples);
        const qint64 frameMs = streamMsBase + m_streamMs;
        const double startThresh = m_noiseFloorDb + m_config.startThresholdDb;
        const double endThresh = startThresh - 3.0;

        if (!m_inSpeech) {
            if (db >= startThresh) {
                ++m_aboveCount;
            } else {
                m_aboveCount = 0;
                if (db < m_noiseFloorDb + 3.0) {
                    m_noiseFloorDb = qBound(kMinFloorDb, m_noiseFloorDb * (1.0 - kFloorAlpha) + db * kFloorAlpha,
                                            kMaxFloorDb);
                }
            }
            if (m_aboveCount >= 3) {
                m_inSpeech = true;
                m_segmentStartMs = frameMs - m_config.preRollMs;
                m_segmentPcm = m_preRoll;
                m_belowMs = 0;
            }
        }

        const QByteArray frameBytes(reinterpret_cast<const char *>(samples + offset),
                                    m_frameSamples * 2);
        if (m_inSpeech) {
            m_segmentPcm.append(frameBytes);
            if (db < endThresh) {
                m_belowMs += m_config.frameMs;
            } else {
                m_belowMs = 0;
            }
            if (m_segmentPcm.size() / 2 >= m_maxSegmentSamples) {
                forceCutIfNeeded(streamMsBase);
                out.append(finalizeSegment(streamMsBase));
            } else if (m_belowMs >= m_config.endHangoverMs) {
                out.append(finalizeSegment(streamMsBase));
            }
        } else {
            if (!m_preRoll.isEmpty()) {
                m_preRoll.remove(0, m_frameSamples * 2);
                m_preRoll.append(frameBytes);
            }
        }

        offset += m_frameSamples;
        m_streamMs += m_config.frameMs;
    }
    return out;
}

void Vad::forceCutIfNeeded(qint64 streamMsBase)
{
    Q_UNUSED(streamMsBase);
}

Vad::Segment Vad::finalizeSegment(qint64 streamMsBase)
{
    Segment seg;
    seg.pcm16k = m_segmentPcm;
    seg.startMs = m_segmentStartMs;
    seg.endMs = streamMsBase + m_streamMs;
    const int durationMs = int((seg.pcm16k.size() / 2) * 1000 / kSampleRate);
    if (durationMs < m_config.minSegmentMs) {
        seg.pcm16k.clear();
    }
    m_inSpeech = false;
    m_aboveCount = 0;
    m_belowMs = 0;
    m_segmentPcm.clear();
    return seg;
}

QVector<Vad::Segment> Vad::flush(qint64 streamMsBase)
{
    QVector<Segment> out;
    if (m_inSpeech && !m_segmentPcm.isEmpty()) {
        const int durationMs = int((m_segmentPcm.size() / 2) * 1000 / kSampleRate);
        if (durationMs >= 300) {
            out.append(finalizeSegment(streamMsBase));
        } else {
            m_inSpeech = false;
            m_segmentPcm.clear();
        }
    }
    return out;
}
