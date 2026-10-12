#pragma once

#include <QByteArray>
#include <QVector>

class Vad
{
public:
    struct Config {
        int frameMs = 30;
        double startThresholdDb = 9.0;
        int endHangoverMs = 700;
        int minSegmentMs = 500;
        int maxSegmentMs = 12000;
        int preRollMs = 200;
    };

    struct Segment {
        QByteArray pcm16k;
        qint64 startMs = 0;
        qint64 endMs = 0;
    };

    Vad();
    explicit Vad(const Config &config);

    QVector<Segment> pushPcm16k(const QByteArray &pcm16k, qint64 streamMsBase);
    QVector<Segment> flush(qint64 streamMsBase);

private:
    double frameDb(const qint16 *samples, int count) const;
    void forceCutIfNeeded(qint64 streamMsBase);
    Segment finalizeSegment(qint64 streamMsBase);

    Config m_config;
    int m_frameSamples = 480;
    double m_noiseFloorDb = -50.0;
    bool m_inSpeech = false;
    int m_aboveCount = 0;
    int m_belowMs = 0;
    qint64 m_segmentStartMs = 0;
    qint64 m_streamMs = 0;
    QByteArray m_preRoll;
    QByteArray m_segmentPcm;
    int m_maxSegmentSamples = 0;
};
