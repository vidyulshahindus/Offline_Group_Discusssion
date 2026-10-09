#include "Vad.h"

#include <QtGlobal>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <utility>

namespace {

QByteArray concatFrames(const QVector<QByteArray> &parts)
{
    qsizetype total = 0;
    for (const QByteArray &p : parts) {
        total += p.size();
    }
    QByteArray out;
    out.reserve(total);
    for (const QByteArray &p : parts) {
        out.append(p);
    }
    return out;
}

} // namespace

Vad::Vad(QObject *parent)
    : QObject(parent)
{
    setParams(VadParams{});
}

void Vad::setParams(const VadParams &params)
{
    m_params = params;
    m_params.sampleRate = qMax(8000, m_params.sampleRate);
    m_params.frameMs = qBound(10, m_params.frameMs, 100);
    m_params.startFrames = qMax(1, m_params.startFrames);
    m_frameSamples = m_params.sampleRate * m_params.frameMs / 1000;
    reset();
}

int Vad::framesFor(int ms) const
{
    if (ms <= 0) {
        return 0;
    }
    return (ms + m_params.frameMs - 1) / m_params.frameMs;
}

void Vad::reset()
{
    m_remainder.clear();
    m_frameIndex = 0;
    m_calibrationDb.clear();
    m_preRoll.clear();
    m_candidates.clear();
    m_segment.clear();
    m_inSpeech = false;
    m_quietRun = 0;
    m_firstLoud = -1;
    m_lastLoud = -1;
    m_lastDb = -100.0;
    if (!std::isnan(m_params.seedFloorDb)) {
        m_floorDb = qBound(m_params.floorMinDb, m_params.seedFloorDb, m_params.floorMaxDb);
        m_calibrated = true;
    } else {
        m_floorDb = -50.0;
        m_calibrated = framesFor(m_params.calibrationMs) == 0;
    }
}

double Vad::frameDb(const qint16 *samples, int count)
{
    if (!samples || count <= 0) {
        return -100.0;
    }
    double sum = 0.0;
    for (int i = 0; i < count; ++i) {
        const double s = samples[i] / 32768.0;
        sum += s * s;
    }
    const double rms = std::sqrt(sum / count);
    if (rms <= 1e-5) {
        return -100.0;
    }
    return qMax(-100.0, 20.0 * std::log10(rms));
}

void Vad::pushPcm(const QByteArray &pcm16kMono)
{
    if (pcm16kMono.isEmpty()) {
        return;
    }
    m_remainder.append(pcm16kMono);
    const int frameBytes = m_frameSamples * 2;
    qsizetype offset = 0;
    while (m_remainder.size() - offset >= frameBytes) {
        Frame frame;
        frame.pcm = m_remainder.mid(offset, frameBytes);
        // Copy before reading: QByteArray data is not guaranteed to be 2-byte aligned.
        QVector<qint16> samples(m_frameSamples);
        std::memcpy(samples.data(), frame.pcm.constData(), size_t(frameBytes));
        frame.db = frameDb(samples.constData(), m_frameSamples);
        frame.index = m_frameIndex++;
        offset += frameBytes;
        processFrame(frame);
    }
    m_remainder.remove(0, offset);
}

void Vad::pushPreRoll(const Frame &frame)
{
    m_preRoll.append(frame);
    const int cap = framesFor(m_params.preRollMs);
    while (m_preRoll.size() > cap) {
        m_preRoll.removeFirst();
    }
}

void Vad::finishCalibration()
{
    QVector<double> sorted = m_calibrationDb;
    std::sort(sorted.begin(), sorted.end());
    // 20th percentile: robust if the speaker starts talking during calibration.
    const int idx = int(std::floor(0.2 * (sorted.size() - 1)));
    const double floor = sorted.isEmpty() ? -50.0 : sorted.at(idx);
    m_floorDb = qBound(m_params.floorMinDb, floor, m_params.floorMaxDb);
    m_calibrationDb.clear();
    m_calibrated = true;
}

void Vad::updateFloor(double db)
{
    const double a = m_params.noiseAlpha;
    m_floorDb = qBound(m_params.floorMinDb, a * db + (1.0 - a) * m_floorDb, m_params.floorMaxDb);
}

void Vad::processFrame(const Frame &frame)
{
    m_lastDb = frame.db;

    if (!m_calibrated) {
        m_calibrationDb.append(frame.db);
        pushPreRoll(frame);
        if (m_calibrationDb.size() >= framesFor(m_params.calibrationMs)) {
            finishCalibration();
        }
        return;
    }

    if (!m_inSpeech) {
        if (frame.db > startThreshold()) {
            m_candidates.append(frame);
            if (m_candidates.size() >= m_params.startFrames) {
                m_segment = m_preRoll;
                m_firstLoud = int(m_segment.size());
                m_segment += m_candidates;
                m_lastLoud = int(m_segment.size()) - 1;
                m_preRoll.clear();
                m_candidates.clear();
                m_quietRun = 0;
                m_inSpeech = true;
                emit speechStarted(m_segment.first().index * m_params.frameMs);
            }
            return;
        }
        // A short loud burst (click, tap) that did not start speech: keep it as pre-roll.
        for (const Frame &c : std::as_const(m_candidates)) {
            pushPreRoll(c);
        }
        m_candidates.clear();
        updateFloor(frame.db);
        pushPreRoll(frame);
        return;
    }

    m_segment.append(frame);
    if (frame.db < endThreshold()) {
        ++m_quietRun;
    } else {
        m_quietRun = 0;
        m_lastLoud = int(m_segment.size()) - 1;
        if (m_firstLoud < 0) {
            m_firstLoud = m_lastLoud;
        }
    }

    if (m_quietRun >= framesFor(m_params.endHangoverMs)) {
        const int keep = (m_lastLoud + 1) + qMin(m_quietRun, framesFor(m_params.tailKeepMs));
        endSegment(keep, true, m_params.minSegmentMs);
    } else if (m_segment.size() >= qMax(2, framesFor(m_params.maxSegmentMs))) {
        forceCut();
    }
}

void Vad::endSegment(int keepFrames, bool checkMinimum, int minMs)
{
    if (m_segment.isEmpty()) {
        m_inSpeech = false;
        return;
    }
    const int keep = qBound(1, keepFrames, int(m_segment.size()));
    const qint64 startMs = m_segment.first().index * m_params.frameMs;
    const qint64 endMs = (m_segment.at(keep - 1).index + 1) * m_params.frameMs;
    const int voicedMs =
        (m_firstLoud >= 0 && m_lastLoud >= m_firstLoud) ? (m_lastLoud - m_firstLoud + 1) * m_params.frameMs : 0;
    const bool emitIt = !checkMinimum || voicedMs >= minMs;

    QVector<QByteArray> parts;
    parts.reserve(keep);
    for (int i = 0; i < keep; ++i) {
        parts.append(m_segment.at(i).pcm);
    }

    // Quiet frames after the kept part become pre-roll for the next utterance.
    const QVector<Frame> rest = m_segment.mid(keep);
    m_segment.clear();
    m_inSpeech = false;
    m_quietRun = 0;
    m_firstLoud = -1;
    m_lastLoud = -1;
    m_preRoll.clear();
    for (const Frame &f : rest) {
        pushPreRoll(f);
    }

    if (emitIt) {
        emit segmentReady(concatFrames(parts), startMs, endMs);
    } else {
        emit segmentDiscarded(startMs, endMs);
    }
    emit speechEnded(endMs);
}

void Vad::forceCut()
{
    const int n = int(m_segment.size());
    const int from = qMax(1, n - qMax(1, framesFor(m_params.cutSearchMs)));
    int quietest = n - 1;
    for (int i = from; i < n; ++i) {
        if (m_segment.at(i).db < m_segment.at(quietest).db) {
            quietest = i;
        }
    }
    const int cut = quietest + 1;

    QVector<QByteArray> parts;
    parts.reserve(cut);
    for (int i = 0; i < cut; ++i) {
        parts.append(m_segment.at(i).pcm);
    }
    const qint64 startMs = m_segment.first().index * m_params.frameMs;
    const qint64 endMs = (m_segment.at(cut - 1).index + 1) * m_params.frameMs;

    m_segment = m_segment.mid(cut);
    m_firstLoud = -1;
    m_lastLoud = -1;
    m_quietRun = 0;
    const double quietDb = endThreshold();
    for (int i = 0; i < m_segment.size(); ++i) {
        if (m_segment.at(i).db >= quietDb) {
            if (m_firstLoud < 0) {
                m_firstLoud = i;
            }
            m_lastLoud = i;
            m_quietRun = 0;
        } else {
            ++m_quietRun;
        }
    }

    emit segmentReady(concatFrames(parts), startMs, endMs);
}

bool Vad::flush(int minMs)
{
    m_candidates.clear();
    m_remainder.clear();
    if (!m_inSpeech) {
        return false;
    }
    const int keep = m_lastLoud >= 0
                         ? (m_lastLoud + 1) + qMin(m_quietRun, framesFor(m_params.tailKeepMs))
                         : int(m_segment.size());
    const int voicedMs =
        (m_firstLoud >= 0 && m_lastLoud >= m_firstLoud) ? (m_lastLoud - m_firstLoud + 1) * m_params.frameMs : 0;
    endSegment(keep, true, minMs);
    return voicedMs >= minMs;
}

void Vad::abandonSegment()
{
    const bool wasInSpeech = m_inSpeech;
    const qint64 endMs = positionMs();
    m_segment.clear();
    m_candidates.clear();
    m_preRoll.clear();
    m_inSpeech = false;
    m_quietRun = 0;
    m_firstLoud = -1;
    m_lastLoud = -1;
    if (wasInSpeech) {
        emit speechEnded(endMs);
    }
}
